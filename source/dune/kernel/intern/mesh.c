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
