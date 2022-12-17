#include "MEM_guardedalloc.h"

/* Allow using deprecated functionality for .blend file I/O. */
#define STRUCTS_DEPRECATED_ALLOW

#include "STRUCTS_defaults.h"
#include "STRUCTS_key_types.h"
#include "STRUCTS_material_types.h"
#include "STRUCTS_mesh_types.h"
#include "STRUCTS_meshdata_types.h"
#include "STRUCTS_object_types.h"

#include "LI_bitmap.h"
#include "LI_edgehash.h"
#include "LI_endian_switch.h"
#include "LI_ghash.h"
#include "LI_hash.h"
#include "LI_index_range.hh"
#include "LI_linklist.h"
#include "LI_listbase.h"
#include "LI_math.h"
#include "LI_math_vector.hh"
#include "LI_memarena.h"
#include "LI_string.h"
#include "LI_task.hh"
#include "LI_utildefines.h"

#include "TRANSLATION_translation.h"

#include "KERNEL_anim_data.h"
#include "KERNEL_bpath.h"
#include "KERNEL_deform.h"
#include "KERNEL_editmesh.h"
#include "KERNEL_global.h"
#include "KERNEL_idtype.h"
#include "KERNEL_key.h"
#include "KERNEL_lib_id.h"
#include "KERNEL_lib_query.h"
#include "KERNEL_main.h"
#include "KERNEL_material.h"
#include "KERNEL_mesh.h"
#include "KERNEL_mesh_runtime.h"
#include "KERNEL_mesh_wrapper.h"
#include "KERNEL_modifier.h"
#include "KERNEL_multires.h"
#include "KERNEL_object.h"

#include "PIL_time.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "LOADER_read_write.h"

static void mesh_clear_geometry(Mesh *mesh);
static void mesh_tessface_clear_intern(Mesh *mesh, int free_customdata);

static void mesh_init_data(ID *id)
{
  Mesh *mesh = (Mesh *)id;

  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(mesh, id));

  MEMCPY_STRUCT_AFTER(mesh, DNA_struct_default_get(Mesh), id);

  CustomData_reset(&mesh->vdata);
  CustomData_reset(&mesh->edata);
  CustomData_reset(&mesh->fdata);
  CustomData_reset(&mesh->pdata);
  CustomData_reset(&mesh->ldata);

  BKE_mesh_runtime_init_data(mesh);

  /* A newly created mesh does not have normals, so tag them dirty. This will be cleared
   * by #BKE_mesh_vertex_normals_clear_dirty or #BKE_mesh_poly_normals_ensure. */
  BKE_mesh_normals_tag_dirty(mesh);

  mesh->face_sets_color_seed = BLI_hash_int(PIL_check_seconds_timer_i() & UINT_MAX);
}

static void mesh_copy_data(Main *bmain, ID *id_dst, const ID *id_src, const int flag)
{
  Mesh *mesh_dst = (Mesh *)id_dst;
  const Mesh *mesh_src = (const Mesh *)id_src;

  BKE_mesh_runtime_reset_on_copy(mesh_dst, flag);
  if ((mesh_src->id.tag & LIB_TAG_NO_MAIN) == 0) {
    /* This is a direct copy of a main mesh, so for now it has the same topology. */
    mesh_dst->runtime.deformed_only = true;
  }
  /* This option is set for run-time meshes that have been copied from the current objects mode.
   * Currently this is used for edit-mesh although it could be used for sculpt or other
   * kinds of data specific to an objects mode.
   *
   * The flag signals that the mesh hasn't been modified from the data that generated it,
   * allowing us to use the object-mode data for drawing.
   *
   * While this could be the callers responsibility, keep here since it's
   * highly unlikely we want to create a duplicate and not use it for drawing. */
  mesh_dst->runtime.is_original = false;

  /* Only do tessface if we have no polys. */
  const bool do_tessface = ((mesh_src->totface != 0) && (mesh_src->totpoly == 0));

  CustomData_MeshMasks mask = CD_MASK_MESH;

  if (mesh_src->id.tag & LIB_TAG_NO_MAIN) {
    /* For copies in depsgraph, keep data like origindex and orco. */
    CustomData_MeshMasks_update(&mask, &CD_MASK_DERIVEDMESH);
  }

  mesh_dst->mat = (Material **)MEM_dupallocN(mesh_src->mat);

  BKE_defgroup_copy_list(&mesh_dst->vertex_group_names, &mesh_src->vertex_group_names);

  const eCDAllocType alloc_type = (flag & LIB_ID_COPY_CD_REFERENCE) ? CD_REFERENCE : CD_DUPLICATE;
  CustomData_copy(&mesh_src->vdata, &mesh_dst->vdata, mask.vmask, alloc_type, mesh_dst->totvert);
  CustomData_copy(&mesh_src->edata, &mesh_dst->edata, mask.emask, alloc_type, mesh_dst->totedge);
  CustomData_copy(&mesh_src->ldata, &mesh_dst->ldata, mask.lmask, alloc_type, mesh_dst->totloop);
  CustomData_copy(&mesh_src->pdata, &mesh_dst->pdata, mask.pmask, alloc_type, mesh_dst->totpoly);
  if (do_tessface) {
    CustomData_copy(&mesh_src->fdata, &mesh_dst->fdata, mask.fmask, alloc_type, mesh_dst->totface);
  }
  else {
    mesh_tessface_clear_intern(mesh_dst, false);
  }

  BKE_mesh_update_customdata_pointers(mesh_dst, do_tessface);

  mesh_dst->cd_flag = mesh_src->cd_flag;

  mesh_dst->edit_mesh = nullptr;

  mesh_dst->mselect = (MSelect *)MEM_dupallocN(mesh_dst->mselect);

  /* Set normal layers dirty. They should be dirty by default on new meshes anyway, but being
   * explicit about it is safer. Alternatively normal layers could be copied if they aren't dirty,
   * avoiding recomputation in some cases. However, a copied mesh is often changed anyway, so that
   * idea is not clearly better. With proper reference counting, all custom data layers could be
   * copied as the cost would be much lower. */
  BKE_mesh_normals_tag_dirty(mesh_dst);

  /* TODO: Do we want to add flag to prevent this? */
  if (mesh_src->key && (flag & LIB_ID_COPY_SHAPEKEY)) {
    BKE_id_copy_ex(bmain, &mesh_src->key->id, (ID **)&mesh_dst->key, flag);
    /* XXX This is not nice, we need to make BKE_id_copy_ex fully re-entrant... */
    mesh_dst->key->from = &mesh_dst->id;
  }

  BKE_mesh_assert_normals_dirty_or_calculated(mesh_dst);
}

void BKE_mesh_free_editmesh(struct Mesh *mesh)
{
  if (mesh->edit_mesh == nullptr) {
    return;
  }

  if (mesh->edit_mesh->is_shallow_copy == false) {
    BKE_editmesh_free_data(mesh->edit_mesh);
  }
  MEM_freeN(mesh->edit_mesh);
  mesh->edit_mesh = nullptr;
}

static void mesh_free_data(ID *id)
{
  Mesh *mesh = (Mesh *)id;

  BLI_freelistN(&mesh->vertex_group_names);

  BKE_mesh_free_editmesh(mesh);

  BKE_mesh_runtime_free_data(mesh);
  mesh_clear_geometry(mesh);
  MEM_SAFE_FREE(mesh->mat);
}

static void mesh_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Mesh *mesh = (Mesh *)id;
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, mesh->texcomesh, IDWALK_CB_NEVER_SELF);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, mesh->key, IDWALK_CB_USER);
  for (int i = 0; i < mesh->totcol; i++) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, mesh->mat[i], IDWALK_CB_USER);
  }
}

static void mesh_foreach_path(ID *id, BPathForeachPathData *bpath_data)
{
  Mesh *me = (Mesh *)id;
  if (me->ldata.external) {
    BKE_bpath_foreach_path_fixed_process(bpath_data, me->ldata.external->filename);
  }
}

static void mesh_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  Mesh *mesh = (Mesh *)id;
  const bool is_undo = BLO_write_is_undo(writer);

  CustomDataLayer *vlayers = nullptr, vlayers_buff[CD_TEMP_CHUNK_SIZE];
  CustomDataLayer *elayers = nullptr, elayers_buff[CD_TEMP_CHUNK_SIZE];
  CustomDataLayer *flayers = nullptr, flayers_buff[CD_TEMP_CHUNK_SIZE];
  CustomDataLayer *llayers = nullptr, llayers_buff[CD_TEMP_CHUNK_SIZE];
  CustomDataLayer *players = nullptr, players_buff[CD_TEMP_CHUNK_SIZE];

  /* cache only - don't write */
  mesh->mface = nullptr;
  mesh->totface = 0;
  memset(&mesh->fdata, 0, sizeof(mesh->fdata));
  memset(&mesh->runtime, 0, sizeof(mesh->runtime));
  flayers = flayers_buff;

  /* Do not store actual geometry data in case this is a library override ID. */
  if (ID_IS_OVERRIDE_LIBRARY(mesh) && !is_undo) {
    mesh->mvert = nullptr;
    mesh->totvert = 0;
    memset(&mesh->vdata, 0, sizeof(mesh->vdata));
    vlayers = vlayers_buff;

    mesh->medge = nullptr;
    mesh->totedge = 0;
    memset(&mesh->edata, 0, sizeof(mesh->edata));
    elayers = elayers_buff;

    mesh->mloop = nullptr;
    mesh->totloop = 0;
    memset(&mesh->ldata, 0, sizeof(mesh->ldata));
    llayers = llayers_buff;

    mesh->mpoly = nullptr;
    mesh->totpoly = 0;
    memset(&mesh->pdata, 0, sizeof(mesh->pdata));
    players = players_buff;
  }
  else {
    CustomData_blend_write_prepare(&mesh->vdata, &vlayers, vlayers_buff, ARRAY_SIZE(vlayers_buff));
    CustomData_blend_write_prepare(&mesh->edata, &elayers, elayers_buff, ARRAY_SIZE(elayers_buff));
    CustomData_blend_write_prepare(&mesh->ldata, &llayers, llayers_buff, ARRAY_SIZE(llayers_buff));
    CustomData_blend_write_prepare(&mesh->pdata, &players, players_buff, ARRAY_SIZE(players_buff));
  }

  BLO_write_id_struct(writer, Mesh, id_address, &mesh->id);
  BKE_id_blend_write(writer, &mesh->id);

  /* direct data */
  if (mesh->adt) {
    BKE_animdata_blend_write(writer, mesh->adt);
  }

  BKE_defbase_blend_write(writer, &mesh->vertex_group_names);

  BLO_write_pointer_array(writer, mesh->totcol, mesh->mat);
  BLO_write_raw(writer, sizeof(MSelect) * mesh->totselect, mesh->mselect);

  CustomData_blend_write(
      writer, &mesh->vdata, vlayers, mesh->totvert, CD_MASK_MESH.vmask, &mesh->id);
  CustomData_blend_write(
      writer, &mesh->edata, elayers, mesh->totedge, CD_MASK_MESH.emask, &mesh->id);
  /* fdata is really a dummy - written so slots align */
  CustomData_blend_write(
      writer, &mesh->fdata, flayers, mesh->totface, CD_MASK_MESH.fmask, &mesh->id);
  CustomData_blend_write(
      writer, &mesh->ldata, llayers, mesh->totloop, CD_MASK_MESH.lmask, &mesh->id);
  CustomData_blend_write(
      writer, &mesh->pdata, players, mesh->totpoly, CD_MASK_MESH.pmask, &mesh->id);

  /* Free temporary data */

  /* Free custom-data layers, when not assigned a buffer value. */
#define CD_LAYERS_FREE(id) \
  if (id && id != id##_buff) { \
    MEM_freeN(id); \
  } \
  ((void)0)

  CD_LAYERS_FREE(vlayers);
  CD_LAYERS_FREE(elayers);
  // CD_LAYER_FREE(flayers); /* Never allocated. */
  CD_LAYERS_FREE(llayers);
  CD_LAYERS_FREE(players);

#undef CD_LAYERS_FREE
}

static void mesh_blend_read_data(BlendDataReader *reader, ID *id)
{
  Mesh *mesh = (Mesh *)id;
  BLO_read_pointer_array(reader, (void **)&mesh->mat);

  BLO_read_data_address(reader, &mesh->mvert);
  BLO_read_data_address(reader, &mesh->medge);
  BLO_read_data_address(reader, &mesh->mface);
  BLO_read_data_address(reader, &mesh->mloop);
  BLO_read_data_address(reader, &mesh->mpoly);
  BLO_read_data_address(reader, &mesh->tface);
  BLO_read_data_address(reader, &mesh->mtface);
  BLO_read_data_address(reader, &mesh->mcol);
  BLO_read_data_address(reader, &mesh->dvert);
  BLO_read_data_address(reader, &mesh->mloopcol);
  BLO_read_data_address(reader, &mesh->mloopuv);
  BLO_read_data_address(reader, &mesh->mselect);

  /* animdata */
  BLO_read_data_address(reader, &mesh->adt);
  BKE_animdata_blend_read_data(reader, mesh->adt);

  /* Normally BKE_defvert_blend_read should be called in CustomData_blend_read,
   * but for backwards compatibility in do_versions to work we do it here. */
  BKE_defvert_blend_read(reader, mesh->totvert, mesh->dvert);
  BLO_read_list(reader, &mesh->vertex_group_names);

  CustomData_blend_read(reader, &mesh->vdata, mesh->totvert);
  CustomData_blend_read(reader, &mesh->edata, mesh->totedge);
  CustomData_blend_read(reader, &mesh->fdata, mesh->totface);
  CustomData_blend_read(reader, &mesh->ldata, mesh->totloop);
  CustomData_blend_read(reader, &mesh->pdata, mesh->totpoly);

  mesh->texflag &= ~ME_AUTOSPACE_EVALUATED;
  mesh->edit_mesh = nullptr;

  memset(&mesh->runtime, 0, sizeof(mesh->runtime));
  BKE_mesh_runtime_init_data(mesh);

  /* happens with old files */
  if (mesh->mselect == nullptr) {
    mesh->totselect = 0;
  }

  if (BLO_read_requires_endian_switch(reader) && mesh->tface) {
    TFace *tf = mesh->tface;
    for (int i = 0; i < mesh->totface; i++, tf++) {
      BLI_endian_switch_uint32_array(tf->col, 4);
    }
  }

  /* We don't expect to load normals from files, since they are derived data. */
  BKE_mesh_normals_tag_dirty(mesh);
  BKE_mesh_assert_normals_dirty_or_calculated(mesh);
}

static void mesh_blend_read_lib(BlendLibReader *reader, ID *id)
{
  Mesh *me = (Mesh *)id;
  /* this check added for python created meshes */
  if (me->mat) {
    for (int i = 0; i < me->totcol; i++) {
      BLO_read_id_address(reader, me->id.lib, &me->mat[i]);
    }
  }
  else {
    me->totcol = 0;
  }

  BLO_read_id_address(reader, me->id.lib, &me->ipo);  // XXX: deprecated: old anim sys
  BLO_read_id_address(reader, me->id.lib, &me->key);
  BLO_read_id_address(reader, me->id.lib, &me->texcomesh);
}

static void mesh_read_expand(BlendExpander *expander, ID *id)
{
  Mesh *me = (Mesh *)id;
  for (int a = 0; a < me->totcol; a++) {
    BLO_expand(expander, me->mat[a]);
  }

  BLO_expand(expander, me->key);
  BLO_expand(expander, me->texcomesh);
}

IDTypeInfo IDType_ID_ME = {
    /* id_code */ ID_ME,
    /* id_filter */ FILTER_ID_ME,
    /* main_listbase_index */ INDEX_ID_ME,
    /* struct_size */ sizeof(Mesh),
    /* name */ "Mesh",
    /* name_plural */ "meshes",
    /* translation_context */ BLT_I18NCONTEXT_ID_MESH,
    /* flags */ IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    /* asset_type_info */ nullptr,

    /* init_data */ mesh_init_data,
    /* copy_data */ mesh_copy_data,
    /* free_data */ mesh_free_data,
    /* make_local */ nullptr,
    /* foreach_id */ mesh_foreach_id,
    /* foreach_cache */ nullptr,
    /* foreach_path */ mesh_foreach_path,
    /* owner_get */ nullptr,

    /* blend_write */ mesh_blend_write,
    /* blend_read_data */ mesh_blend_read_data,
    /* blend_read_lib */ mesh_blend_read_lib,
    /* blend_read_expand */ mesh_read_expand,

    /* blend_read_undo_preserve */ nullptr,

    /* lib_override_apply_post */ nullptr,
};

enum {
  MESHCMP_DVERT_WEIGHTMISMATCH = 1,
  MESHCMP_DVERT_GROUPMISMATCH,
  MESHCMP_DVERT_TOTGROUPMISMATCH,
  MESHCMP_LOOPCOLMISMATCH,
  MESHCMP_LOOPUVMISMATCH,
  MESHCMP_LOOPMISMATCH,
  MESHCMP_POLYVERTMISMATCH,
  MESHCMP_POLYMISMATCH,
  MESHCMP_EDGEUNKNOWN,
  MESHCMP_VERTCOMISMATCH,
  MESHCMP_CDLAYERS_MISMATCH,
  MESHCMP_ATTRIBUTE_VALUE_MISMATCH,
};

static const char *cmpcode_to_str(int code)
{
  switch (code) {
    case MESHCMP_DVERT_WEIGHTMISMATCH:
      return "Vertex Weight Mismatch";
    case MESHCMP_DVERT_GROUPMISMATCH:
      return "Vertex Group Mismatch";
    case MESHCMP_DVERT_TOTGROUPMISMATCH:
      return "Vertex Doesn't Belong To Same Number Of Groups";
    case MESHCMP_LOOPCOLMISMATCH:
      return "Vertex Color Mismatch";
    case MESHCMP_LOOPUVMISMATCH:
      return "UV Mismatch";
    case MESHCMP_LOOPMISMATCH:
      return "Loop Mismatch";
    case MESHCMP_POLYVERTMISMATCH:
      return "Loop Vert Mismatch In Poly Test";
    case MESHCMP_POLYMISMATCH:
      return "Loop Vert Mismatch";
    case MESHCMP_EDGEUNKNOWN:
      return "Edge Mismatch";
    case MESHCMP_VERTCOMISMATCH:
      return "Vertex Coordinate Mismatch";
    case MESHCMP_CDLAYERS_MISMATCH:
      return "CustomData Layer Count Mismatch";
    case MESHCMP_ATTRIBUTE_VALUE_MISMATCH:
      return "Attribute Value Mismatch";
    default:
      return "Mesh Comparison Code Unknown";
  }
}

/** Thresh is threshold for comparing vertices, UV's, vertex colors, weights, etc. */
static int customdata_compare(
    CustomData *c1, CustomData *c2, const int total_length, Mesh *m1, Mesh *m2, const float thresh)
{
  const float thresh_sq = thresh * thresh;
  CustomDataLayer *l1, *l2;
  int layer_count1 = 0, layer_count2 = 0, j;
  const uint64_t cd_mask_non_generic = CD_MASK_MVERT | CD_MASK_MEDGE | CD_MASK_MPOLY |
                                       CD_MASK_MLOOPUV | CD_MASK_MLOOPCOL | CD_MASK_MDEFORMVERT;
  const uint64_t cd_mask_all_attr = CD_MASK_PROP_ALL | cd_mask_non_generic;

  for (int i = 0; i < c1->totlayer; i++) {
    l1 = &c1->layers[i];
    if ((CD_TYPE_AS_MASK(l1->type) & cd_mask_all_attr) && l1->anonymous_id == nullptr) {
      layer_count1++;
    }
  }

  for (int i = 0; i < c2->totlayer; i++) {
    l2 = &c2->layers[i];
    if ((CD_TYPE_AS_MASK(l2->type) & cd_mask_all_attr) && l2->anonymous_id == nullptr) {
      layer_count2++;
    }
  }

  if (layer_count1 != layer_count2) {
    return MESHCMP_CDLAYERS_MISMATCH;
  }

  l1 = c1->layers;
  l2 = c2->layers;

  for (int i1 = 0; i1 < c1->totlayer; i1++) {
    l1 = c1->layers + i1;
    for (int i2 = 0; i2 < c2->totlayer; i2++) {
      l2 = c2->layers + i2;
      if (l1->type != l2->type || !STREQ(l1->name, l2->name) || l1->anonymous_id != nullptr ||
          l2->anonymous_id != nullptr) {
        continue;
      }
      /* At this point `l1` and `l2` have the same name and type, so they should be compared. */

      switch (l1->type) {

        case CD_MVERT: {
          MVert *v1 = (MVert *)l1->data;
          MVert *v2 = (MVert *)l2->data;
          int vtot = m1->totvert;

          for (j = 0; j < vtot; j++, v1++, v2++) {
            for (int k = 0; k < 3; k++) {
              if (compare_threshold_relative(v1->co[k], v2->co[k], thresh)) {
                return MESHCMP_VERTCOMISMATCH;
              }
            }
          }
          break;
        }

        /* We're order-agnostic for edges here. */
        case CD_MEDGE: {
          MEdge *e1 = (MEdge *)l1->data;
          MEdge *e2 = (MEdge *)l2->data;
          int etot = m1->totedge;
          EdgeHash *eh = BLI_edgehash_new_ex(__func__, etot);

          for (j = 0; j < etot; j++, e1++) {
            BLI_edgehash_insert(eh, e1->v1, e1->v2, e1);
          }

          for (j = 0; j < etot; j++, e2++) {
            if (!BLI_edgehash_lookup(eh, e2->v1, e2->v2)) {
              return MESHCMP_EDGEUNKNOWN;
            }
          }
          BLI_edgehash_free(eh, nullptr);
          break;
        }
        case CD_MPOLY: {
          MPoly *p1 = (MPoly *)l1->data;
          MPoly *p2 = (MPoly *)l2->data;
          int ptot = m1->totpoly;

          for (j = 0; j < ptot; j++, p1++, p2++) {
            MLoop *lp1, *lp2;
            int k;

            if (p1->totloop != p2->totloop) {
              return MESHCMP_POLYMISMATCH;
            }

            lp1 = m1->mloop + p1->loopstart;
            lp2 = m2->mloop + p2->loopstart;

            for (k = 0; k < p1->totloop; k++, lp1++, lp2++) {
              if (lp1->v != lp2->v) {
                return MESHCMP_POLYVERTMISMATCH;
              }
            }
          }
          break;
        }
        case CD_MLOOP: {
          MLoop *lp1 = (MLoop *)l1->data;
          MLoop *lp2 = (MLoop *)l2->data;
          int ltot = m1->totloop;

          for (j = 0; j < ltot; j++, lp1++, lp2++) {
            if (lp1->v != lp2->v) {
              return MESHCMP_LOOPMISMATCH;
            }
          }
          break;
        }
        case CD_MLOOPUV: {
          MLoopUV *lp1 = (MLoopUV *)l1->data;
          MLoopUV *lp2 = (MLoopUV *)l2->data;
          int ltot = m1->totloop;

          for (j = 0; j < ltot; j++, lp1++, lp2++) {
            if (len_squared_v2v2(lp1->uv, lp2->uv) > thresh_sq) {
              return MESHCMP_LOOPUVMISMATCH;
            }
          }
          break;
        }
        case CD_MLOOPCOL: {
          MLoopCol *lp1 = (MLoopCol *)l1->data;
          MLoopCol *lp2 = (MLoopCol *)l2->data;
          int ltot = m1->totloop;

          for (j = 0; j < ltot; j++, lp1++, lp2++) {
            if (lp1->r != lp2->r || lp1->g != lp2->g || lp1->b != lp2->b || lp1->a != lp2->a) {
              return MESHCMP_LOOPCOLMISMATCH;
            }
          }
          break;
        }
        case CD_MDEFORMVERT: {
          MDeformVert *dv1 = (MDeformVert *)l1->data;
          MDeformVert *dv2 = (MDeformVert *)l2->data;
          int dvtot = m1->totvert;

          for (j = 0; j < dvtot; j++, dv1++, dv2++) {
            int k;
            MDeformWeight *dw1 = dv1->dw, *dw2 = dv2->dw;

            if (dv1->totweight != dv2->totweight) {
              return MESHCMP_DVERT_TOTGROUPMISMATCH;
            }

            for (k = 0; k < dv1->totweight; k++, dw1++, dw2++) {
              if (dw1->def_nr != dw2->def_nr) {
                return MESHCMP_DVERT_GROUPMISMATCH;
              }
              if (fabsf(dw1->weight - dw2->weight) > thresh) {
                return MESHCMP_DVERT_WEIGHTMISMATCH;
              }
            }
          }
          break;
        }
        case CD_PROP_FLOAT: {
          const float *l1_data = (float *)l1->data;
          const float *l2_data = (float *)l2->data;

          for (int i = 0; i < total_length; i++) {
            if (compare_threshold_relative(l1_data[i], l2_data[i], thresh)) {
              return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
            }
          }
          break;
        }
        case CD_PROP_FLOAT2: {
          const float(*l1_data)[2] = (float(*)[2])l1->data;
          const float(*l2_data)[2] = (float(*)[2])l2->data;

          for (int i = 0; i < total_length; i++) {
            if (compare_threshold_relative(l1_data[i][0], l2_data[i][0], thresh)) {
              return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
            }
            if (compare_threshold_relative(l1_data[i][1], l2_data[i][1], thresh)) {
              return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
            }
          }
          break;
        }
        case CD_PROP_FLOAT3: {
          const float(*l1_data)[3] = (float(*)[3])l1->data;
          const float(*l2_data)[3] = (float(*)[3])l2->data;

          for (int i = 0; i < total_length; i++) {
            if (compare_threshold_relative(l1_data[i][0], l2_data[i][0], thresh)) {
              return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
            }
            if (compare_threshold_relative(l1_data[i][1], l2_data[i][1], thresh)) {
              return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
            }
            if (compare_threshold_relative(l1_data[i][2], l2_data[i][2], thresh)) {
              return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
            }
          }
          break;
        }
        case CD_PROP_INT32: {
          const int *l1_data = (int *)l1->data;
          const int *l2_data = (int *)l2->data;

          for (int i = 0; i < total_length; i++) {
            if (l1_data[i] != l2_data[i]) {
              return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
            }
          }
          break;
        }
        case CD_PROP_INT8: {
          const int8_t *l1_data = (int8_t *)l1->data;
          const int8_t *l2_data = (int8_t *)l2->data;

          for (int i = 0; i < total_length; i++) {
            if (l1_data[i] != l2_data[i]) {
              return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
            }
          }
          break;
        }
        case CD_PROP_BOOL: {
          const bool *l1_data = (bool *)l1->data;
          const bool *l2_data = (bool *)l2->data;

          for (int i = 0; i < total_length; i++) {
            if (l1_data[i] != l2_data[i]) {
              return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
            }
          }
          break;
        }
        case CD_PROP_COLOR: {
          const MPropCol *l1_data = (MPropCol *)l1->data;
          const MPropCol *l2_data = (MPropCol *)l2->data;

          for (int i = 0; i < total_length; i++) {
            for (j = 0; j < 4; j++) {
              if (compare_threshold_relative(l1_data[i].color[j], l2_data[i].color[j], thresh)) {
                return MESHCMP_ATTRIBUTE_VALUE_MISMATCH;
              }
            }
          }
          break;
        }
        default: {
          break;
        }
      }
    }
  }

  return 0;
}

const char *BKE_mesh_cmp(Mesh *me1, Mesh *me2, float thresh)
{
  int c;

  if (!me1 || !me2) {
    return "Requires two input meshes";
  }

  if (me1->totvert != me2->totvert) {
    return "Number of verts don't match";
  }

  if (me1->totedge != me2->totedge) {
    return "Number of edges don't match";
  }

  if (me1->totpoly != me2->totpoly) {
    return "Number of faces don't match";
  }

  if (me1->totloop != me2->totloop) {
    return "Number of loops don't match";
  }

  if ((c = customdata_compare(&me1->vdata, &me2->vdata, me1->totvert, me1, me2, thresh))) {
    return cmpcode_to_str(c);
  }

  if ((c = customdata_compare(&me1->edata, &me2->edata, me1->totedge, me1, me2, thresh))) {
    return cmpcode_to_str(c);
  }

  if ((c = customdata_compare(&me1->ldata, &me2->ldata, me1->totloop, me1, me2, thresh))) {
    return cmpcode_to_str(c);
  }

  if ((c = customdata_compare(&me1->pdata, &me2->pdata, me1->totpoly, me1, me2, thresh))) {
    return cmpcode_to_str(c);
  }

  return nullptr;
}

static void mesh_ensure_tessellation_customdata(Mesh *me)
{
  if (UNLIKELY((me->totface != 0) && (me->totpoly == 0))) {
    /* Pass, otherwise this function  clears 'mface' before
     * versioning 'mface -> mpoly' code kicks in T30583.
     *
     * Callers could also check but safer to do here - campbell */
  }
  else {
    const int tottex_original = CustomData_number_of_layers(&me->ldata, CD_MLOOPUV);
    const int totcol_original = CustomData_number_of_layers(&me->ldata, CD_MLOOPCOL);

    const int tottex_tessface = CustomData_number_of_layers(&me->fdata, CD_MTFACE);
    const int totcol_tessface = CustomData_number_of_layers(&me->fdata, CD_MCOL);

    if (tottex_tessface != tottex_original || totcol_tessface != totcol_original) {
      BKE_mesh_tessface_clear(me);

      CustomData_from_bmeshpoly(&me->fdata, &me->ldata, me->totface);

      /* TODO: add some `--debug-mesh` option. */
      if (G.debug & G_DEBUG) {
        /* NOTE(campbell): this warning may be un-called for if we are initializing the mesh for
         * the first time from #BMesh, rather than giving a warning about this we could be smarter
         * and check if there was any data to begin with, for now just print the warning with
         * some info to help troubleshoot what's going on. */
        printf(
            "%s: warning! Tessellation uvs or vcol data got out of sync, "
            "had to reset!\n    CD_MTFACE: %d != CD_MLOOPUV: %d || CD_MCOL: %d != CD_MLOOPCOL: "
            "%d\n",
            __func__,
            tottex_tessface,
            tottex_original,
            totcol_tessface,
            totcol_original);
      }
    }
  }
}

