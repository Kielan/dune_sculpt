#include "CLG_log.h"

#include "MEM_guardedalloc.h"

#include "DNA_customdata_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BKE_customdata.h"
#include "BKE_data_transfer.h"
#include "BKE_deform.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_mesh_remap.h"
#include "BKE_mesh_runtime.h"
#include "BKE_mesh_wrapper.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_object_deform.h"
#include "BKE_report.h"

#include "data_transfer_intern.h"

static CLG_LogRef LOG = {"bke.data_transfer"};

void BKE_object_data_transfer_dttypes_to_cdmask(const int dtdata_types,
                                                CustomData_MeshMasks *r_data_masks)
{
  for (int i = 0; i < DT_TYPE_MAX; i++) {
    const int dtdata_type = 1 << i;
    int cddata_type;

    if (!(dtdata_types & dtdata_type)) {
      continue;
    }

    cddata_type = BKE_object_data_transfer_dttype_to_cdtype(dtdata_type);
    if (!(cddata_type & CD_FAKE)) {
      if (DT_DATATYPE_IS_VERT(dtdata_type)) {
        r_data_masks->vmask |= 1LL << cddata_type;
      }
      else if (DT_DATATYPE_IS_EDGE(dtdata_type)) {
        r_data_masks->emask |= 1LL << cddata_type;
      }
      else if (DT_DATATYPE_IS_LOOP(dtdata_type)) {
        r_data_masks->lmask |= 1LL << cddata_type;
      }
      else if (DT_DATATYPE_IS_POLY(dtdata_type)) {
        r_data_masks->pmask |= 1LL << cddata_type;
      }
    }
    else if (cddata_type == CD_FAKE_MDEFORMVERT) {
      r_data_masks->vmask |= CD_MASK_MDEFORMVERT; /* Exception for vgroups :/ */
    }
    else if (cddata_type == CD_FAKE_UV) {
      r_data_masks->lmask |= CD_MASK_MLOOPUV;
    }
    else if (cddata_type == CD_FAKE_LNOR) {
      r_data_masks->vmask |= CD_MASK_NORMAL;
      r_data_masks->pmask |= CD_MASK_NORMAL;
      r_data_masks->lmask |= CD_MASK_NORMAL | CD_MASK_CUSTOMLOOPNORMAL;
    }
  }
}

bool BKE_object_data_transfer_get_dttypes_capacity(const int dtdata_types,
                                                   bool *r_advanced_mixing,
                                                   bool *r_threshold)
{
  bool ret = false;

  *r_advanced_mixing = false;
  *r_threshold = false;

  for (int i = 0; (i < DT_TYPE_MAX) && !(ret && *r_advanced_mixing && *r_threshold); i++) {
    const int dtdata_type = 1 << i;

    if (!(dtdata_types & dtdata_type)) {
      continue;
    }

    switch (dtdata_type) {
      /* Vertex data */
      case DT_TYPE_MDEFORMVERT:
        *r_advanced_mixing = true;
        *r_threshold = true;
        ret = true;
        break;
      case DT_TYPE_SKIN:
        *r_threshold = true;
        ret = true;
        break;
      case DT_TYPE_BWEIGHT_VERT:
        ret = true;
        break;
      /* Edge data */
      case DT_TYPE_SHARP_EDGE:
        *r_threshold = true;
        ret = true;
        break;
      case DT_TYPE_SEAM:
        *r_threshold = true;
        ret = true;
        break;
      case DT_TYPE_CREASE:
        ret = true;
        break;
      case DT_TYPE_BWEIGHT_EDGE:
        ret = true;
        break;
      case DT_TYPE_FREESTYLE_EDGE:
        *r_threshold = true;
        ret = true;
        break;
      /* Loop/Poly data */
      case DT_TYPE_UV:
        ret = true;
        break;
      case DT_TYPE_VCOL:
        *r_advanced_mixing = true;
        *r_threshold = true;
        ret = true;
        break;
      case DT_TYPE_LNOR:
        *r_advanced_mixing = true;
        ret = true;
        break;
      case DT_TYPE_SHARP_FACE:
        *r_threshold = true;
        ret = true;
        break;
      case DT_TYPE_FREESTYLE_FACE:
        *r_threshold = true;
        ret = true;
        break;
    }
  }

  return ret;
}

int BKE_object_data_transfer_get_dttypes_item_types(const int dtdata_types)
{
  int i, ret = 0;

  for (i = 0; (i < DT_TYPE_MAX) && (ret ^ (ME_VERT | ME_EDGE | ME_LOOP | ME_POLY)); i++) {
    const int dtdata_type = 1 << i;

    if (!(dtdata_types & dtdata_type)) {
      continue;
    }

    if (DT_DATATYPE_IS_VERT(dtdata_type)) {
      ret |= ME_VERT;
    }
    if (DT_DATATYPE_IS_EDGE(dtdata_type)) {
      ret |= ME_EDGE;
    }
    if (DT_DATATYPE_IS_LOOP(dtdata_type)) {
      ret |= ME_LOOP;
    }
    if (DT_DATATYPE_IS_POLY(dtdata_type)) {
      ret |= ME_POLY;
    }
  }

  return ret;
}

int BKE_object_data_transfer_dttype_to_cdtype(const int dtdata_type)
{
  switch (dtdata_type) {
    case DT_TYPE_MDEFORMVERT:
      return CD_FAKE_MDEFORMVERT;
    case DT_TYPE_SHAPEKEY:
      return CD_FAKE_SHAPEKEY;
    case DT_TYPE_SKIN:
      return CD_MVERT_SKIN;
    case DT_TYPE_BWEIGHT_VERT:
      return CD_FAKE_BWEIGHT;

    case DT_TYPE_SHARP_EDGE:
      return CD_FAKE_SHARP;
    case DT_TYPE_SEAM:
      return CD_FAKE_SEAM;
    case DT_TYPE_CREASE:
      return CD_FAKE_CREASE;
    case DT_TYPE_BWEIGHT_EDGE:
      return CD_FAKE_BWEIGHT;
    case DT_TYPE_FREESTYLE_EDGE:
      return CD_FREESTYLE_EDGE;

    case DT_TYPE_UV:
      return CD_FAKE_UV;
    case DT_TYPE_SHARP_FACE:
      return CD_FAKE_SHARP;
    case DT_TYPE_FREESTYLE_FACE:
      return CD_FREESTYLE_FACE;

    case DT_TYPE_VCOL:
      return CD_MLOOPCOL;
    case DT_TYPE_LNOR:
      return CD_FAKE_LNOR;

    default:
      BLI_assert(0);
  }
  return 0; /* Should never be reached! */
}

int BKE_object_data_transfer_dttype_to_srcdst_index(const int dtdata_type)
{
  switch (dtdata_type) {
    case DT_TYPE_MDEFORMVERT:
      return DT_MULTILAYER_INDEX_MDEFORMVERT;
    case DT_TYPE_SHAPEKEY:
      return DT_MULTILAYER_INDEX_SHAPEKEY;
    case DT_TYPE_UV:
      return DT_MULTILAYER_INDEX_UV;
    case DT_TYPE_VCOL:
      return DT_MULTILAYER_INDEX_VCOL;
    default:
      return DT_MULTILAYER_INDEX_INVALID;
  }
}

/* ********** */

/* Generic pre/post processing, only used by custom loop normals currently. */

static void data_transfer_dtdata_type_preprocess(Mesh *me_src,
                                                 Mesh *me_dst,
                                                 const int dtdata_type,
                                                 const bool dirty_nors_dst)
{
  if (dtdata_type == DT_TYPE_LNOR) {
    /* Compute custom normals into regular loop normals, which will be used for the transfer. */
    MVert *verts_dst = me_dst->mvert;
    const int num_verts_dst = me_dst->totvert;
    MEdge *edges_dst = me_dst->medge;
    const int num_edges_dst = me_dst->totedge;
    MPoly *polys_dst = me_dst->mpoly;
    const int num_polys_dst = me_dst->totpoly;
    MLoop *loops_dst = me_dst->mloop;
    const int num_loops_dst = me_dst->totloop;
    CustomData *ldata_dst = &me_dst->ldata;

    const bool use_split_nors_dst = (me_dst->flag & ME_AUTOSMOOTH) != 0;
    const float split_angle_dst = me_dst->smoothresh;

    /* This should be ensured by cddata_masks we pass to code generating/giving us me_src now. */
    BLI_assert(CustomData_get_layer(&me_src->ldata, CD_NORMAL) != NULL);
    (void)me_src;

    float(*loop_nors_dst)[3];
    short(*custom_nors_dst)[2] = CustomData_get_layer(ldata_dst, CD_CUSTOMLOOPNORMAL);

    /* Cache loop nors into a temp CDLayer. */
    loop_nors_dst = CustomData_get_layer(ldata_dst, CD_NORMAL);
    const bool do_loop_nors_dst = (loop_nors_dst == NULL);
    if (do_loop_nors_dst) {
      loop_nors_dst = CustomData_add_layer(ldata_dst, CD_NORMAL, CD_CALLOC, NULL, num_loops_dst);
      CustomData_set_layer_flag(ldata_dst, CD_NORMAL, CD_FLAG_TEMPORARY);
    }
    if (dirty_nors_dst || do_loop_nors_dst) {
      BKE_mesh_normals_loop_split(verts_dst,
                                  BKE_mesh_vertex_normals_ensure(me_dst),
                                  num_verts_dst,
                                  edges_dst,
                                  num_edges_dst,
                                  loops_dst,
                                  loop_nors_dst,
                                  num_loops_dst,
                                  polys_dst,
                                  BKE_mesh_poly_normals_ensure(me_dst),
                                  num_polys_dst,
                                  use_split_nors_dst,
                                  split_angle_dst,
                                  NULL,
                                  custom_nors_dst,
                                  NULL);
    }
  }
}

static void data_transfer_dtdata_type_postprocess(Object *UNUSED(ob_src),
                                                  Object *UNUSED(ob_dst),
                                                  Mesh *UNUSED(me_src),
                                                  Mesh *me_dst,
                                                  const int dtdata_type,
                                                  const bool changed)
{
  if (dtdata_type == DT_TYPE_LNOR) {
    if (!changed) {
      return;
    }

    /* Bake edited destination loop normals into custom normals again. */
    MVert *verts_dst = me_dst->mvert;
    const int num_verts_dst = me_dst->totvert;
    MEdge *edges_dst = me_dst->medge;
    const int num_edges_dst = me_dst->totedge;
    MPoly *polys_dst = me_dst->mpoly;
    const int num_polys_dst = me_dst->totpoly;
    MLoop *loops_dst = me_dst->mloop;
    const int num_loops_dst = me_dst->totloop;
    CustomData *ldata_dst = &me_dst->ldata;

    const float(*poly_nors_dst)[3] = BKE_mesh_poly_normals_ensure(me_dst);
    float(*loop_nors_dst)[3] = CustomData_get_layer(ldata_dst, CD_NORMAL);
    short(*custom_nors_dst)[2] = CustomData_get_layer(ldata_dst, CD_CUSTOMLOOPNORMAL);

    if (!custom_nors_dst) {
      custom_nors_dst = CustomData_add_layer(
          ldata_dst, CD_CUSTOMLOOPNORMAL, CD_CALLOC, NULL, num_loops_dst);
    }

    /* Note loop_nors_dst contains our custom normals as transferred from source... */
    BKE_mesh_normals_loop_custom_set(verts_dst,
                                     BKE_mesh_vertex_normals_ensure(me_dst),
                                     num_verts_dst,
                                     edges_dst,
                                     num_edges_dst,
                                     loops_dst,
                                     loop_nors_dst,
                                     num_loops_dst,
                                     polys_dst,
                                     poly_nors_dst,
                                     num_polys_dst,
                                     custom_nors_dst);
  }
}

/* ********** */

static MeshRemapIslandsCalc data_transfer_get_loop_islands_generator(const int cddata_type)
{
  switch (cddata_type) {
    case CD_FAKE_UV:
      return BKE_mesh_calc_islands_loop_poly_edgeseam;
    default:
      break;
  }
  return NULL;
}

float data_transfer_interp_float_do(const int mix_mode,
                                    const float val_dst,
                                    const float val_src,
                                    const float mix_factor)
{
  float val_ret;

  if (((mix_mode == CDT_MIX_REPLACE_ABOVE_THRESHOLD && (val_dst < mix_factor)) ||
       (mix_mode == CDT_MIX_REPLACE_BELOW_THRESHOLD && (val_dst > mix_factor)))) {
    return val_dst; /* Do not affect destination. */
  }

  switch (mix_mode) {
    case CDT_MIX_REPLACE_ABOVE_THRESHOLD:
    case CDT_MIX_REPLACE_BELOW_THRESHOLD:
      return val_src;
    case CDT_MIX_MIX:
      val_ret = (val_dst + val_src) * 0.5f;
      break;
    case CDT_MIX_ADD:
      val_ret = val_dst + val_src;
      break;
    case CDT_MIX_SUB:
      val_ret = val_dst - val_src;
      break;
    case CDT_MIX_MUL:
      val_ret = val_dst * val_src;
      break;
    case CDT_MIX_TRANSFER:
    default:
      val_ret = val_src;
      break;
  }
  return interpf(val_ret, val_dst, mix_factor);
}

static void data_transfer_interp_char(const CustomDataTransferLayerMap *laymap,
                                      void *dest,
                                      const void **sources,
                                      const float *weights,
                                      const int count,
                                      const float mix_factor)
{
  const char **data_src = (const char **)sources;
  char *data_dst = (char *)dest;

  const int mix_mode = laymap->mix_mode;
  float val_src = 0.0f;
  const float val_dst = (float)(*data_dst) / 255.0f;

  for (int i = count; i--;) {
    val_src += ((float)(*data_src[i]) / 255.0f) * weights[i];
  }

  val_src = data_transfer_interp_float_do(mix_mode, val_dst, val_src, mix_factor);

  CLAMP(val_src, 0.0f, 1.0f);

  *data_dst = (char)(val_src * 255.0f);
}

/* Helpers to match sources and destinations data layers
 * (also handles 'conversions' in CD_FAKE cases). */

void data_transfer_layersmapping_add_item(ListBase *r_map,
                                          const int cddata_type,
                                          const int mix_mode,
                                          const float mix_factor,
                                          const float *mix_weights,
                                          const void *data_src,
                                          void *data_dst,
                                          const int data_src_n,
                                          const int data_dst_n,
                                          const size_t elem_size,
                                          const size_t data_size,
                                          const size_t data_offset,
                                          const uint64_t data_flag,
                                          cd_datatransfer_interp interp,
                                          void *interp_data)
{
  CustomDataTransferLayerMap *item = MEM_mallocN(sizeof(*item), __func__);

  BLI_assert(data_dst != NULL);

  item->data_type = cddata_type;
  item->mix_mode = mix_mode;
  item->mix_factor = mix_factor;
  item->mix_weights = mix_weights;

  item->data_src = data_src;
  item->data_dst = data_dst;
  item->data_src_n = data_src_n;
  item->data_dst_n = data_dst_n;
  item->elem_size = elem_size;

  item->data_size = data_size;
  item->data_offset = data_offset;
  item->data_flag = data_flag;

  item->interp = interp;
  item->interp_data = interp_data;

  BLI_addtail(r_map, item);
}

static void data_transfer_layersmapping_add_item_cd(ListBase *r_map,
                                                    const int cddata_type,
                                                    const int mix_mode,
                                                    const float mix_factor,
                                                    const float *mix_weights,
                                                    void *data_src,
                                                    void *data_dst,
                                                    cd_datatransfer_interp interp,
                                                    void *interp_data)
{
  uint64_t data_flag = 0;

  if (cddata_type == CD_FREESTYLE_EDGE) {
    data_flag = FREESTYLE_EDGE_MARK;
  }
  else if (cddata_type == CD_FREESTYLE_FACE) {
    data_flag = FREESTYLE_FACE_MARK;
  }

  data_transfer_layersmapping_add_item(r_map,
                                       cddata_type,
                                       mix_mode,
                                       mix_factor,
                                       mix_weights,
                                       data_src,
                                       data_dst,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       data_flag,
                                       interp,
                                       interp_data);
}

/**
 * \note
 * All those layer mapping handlers return false *only* if they were given invalid parameters.
 * This means that even if they do nothing, they will return true if all given parameters were OK.
 * Also, r_map may be NULL, in which case they will 'only' create/delete destination layers
 * according to given parameters.
 */
static bool data_transfer_layersmapping_cdlayers_multisrc_to_dst(ListBase *r_map,
                                                                 const int cddata_type,
                                                                 const int mix_mode,
                                                                 const float mix_factor,
                                                                 const float *mix_weights,
                                                                 const int num_elem_dst,
                                                                 const bool use_create,
                                                                 const bool use_delete,
                                                                 CustomData *cd_src,
                                                                 CustomData *cd_dst,
                                                                 const bool use_dupref_dst,
                                                                 const int tolayers,
                                                                 const bool *use_layers_src,
                                                                 const int num_layers_src,
                                                                 cd_datatransfer_interp interp,
                                                                 void *interp_data)
{
  void *data_src, *data_dst = NULL;
  int idx_src = num_layers_src;
  int idx_dst, tot_dst = CustomData_number_of_layers(cd_dst, cddata_type);
  bool *data_dst_to_delete = NULL;

  if (!use_layers_src) {
    /* No source at all, we can only delete all dest if requested... */
    if (use_delete) {
      idx_dst = tot_dst;
      while (idx_dst--) {
        CustomData_free_layer(cd_dst, cddata_type, num_elem_dst, idx_dst);
      }
    }
    return true;
  }

  switch (tolayers) {
    case DT_LAYERS_INDEX_DST:
      idx_dst = tot_dst;

      /* Find last source actually used! */
      while (idx_src-- && !use_layers_src[idx_src]) {
        /* pass */
      }
      idx_src++;

      if (idx_dst < idx_src) {
        if (use_create) {
          /* Create as much data layers as necessary! */
          for (; idx_dst < idx_src; idx_dst++) {
            CustomData_add_layer(cd_dst, cddata_type, CD_CALLOC, NULL, num_elem_dst);
          }
        }
        else {
          /* Otherwise, just try to map what we can with existing dst data layers. */
          idx_src = idx_dst;
        }
      }
      else if (use_delete && idx_dst > idx_src) {
        while (idx_dst-- > idx_src) {
          CustomData_free_layer(cd_dst, cddata_type, num_elem_dst, idx_dst);
        }
      }
      if (r_map) {
        while (idx_src--) {
          if (!use_layers_src[idx_src]) {
            continue;
          }
          data_src = CustomData_get_layer_n(cd_src, cddata_type, idx_src);
          /* If dest is a evaluated mesh (from modifier),
           * we do not want to overwrite cdlayers of orig mesh! */
          if (use_dupref_dst) {
            data_dst = CustomData_duplicate_referenced_layer_n(
                cd_dst, cddata_type, idx_src, num_elem_dst);
          }
          else {
            data_dst = CustomData_get_layer_n(cd_dst, cddata_type, idx_src);
          }
          data_transfer_layersmapping_add_item_cd(r_map,
                                                  cddata_type,
                                                  mix_mode,
                                                  mix_factor,
                                                  mix_weights,
                                                  data_src,
                                                  data_dst,
                                                  interp,
                                                  interp_data);
        }
      }
      break;
    case DT_LAYERS_NAME_DST:
      if (use_delete) {
        if (tot_dst) {
          data_dst_to_delete = MEM_mallocN(sizeof(*data_dst_to_delete) * (size_t)tot_dst,
                                           __func__);
          memset(data_dst_to_delete, true, sizeof(*data_dst_to_delete) * (size_t)tot_dst);
        }
      }

      while (idx_src--) {
        const char *name;

        if (!use_layers_src[idx_src]) {
          continue;
        }

        name = CustomData_get_layer_name(cd_src, cddata_type, idx_src);
        data_src = CustomData_get_layer_n(cd_src, cddata_type, idx_src);

        if ((idx_dst = CustomData_get_named_layer(cd_dst, cddata_type, name)) == -1) {
          if (use_create) {
            CustomData_add_layer_named(cd_dst, cddata_type, CD_CALLOC, NULL, num_elem_dst, name);
            idx_dst = CustomData_get_named_layer(cd_dst, cddata_type, name);
          }
          else {
            /* If we are not allowed to create missing dst data layers,
             * just skip matching src one. */
            continue;
          }
        }
        else if (data_dst_to_delete) {
          data_dst_to_delete[idx_dst] = false;
        }
        if (r_map) {
          /* If dest is a evaluated mesh (from modifier),
           * we do not want to overwrite cdlayers of orig mesh! */
          if (use_dupref_dst) {
            data_dst = CustomData_duplicate_referenced_layer_n(
                cd_dst, cddata_type, idx_dst, num_elem_dst);
          }
          else {
            data_dst = CustomData_get_layer_n(cd_dst, cddata_type, idx_dst);
          }
          data_transfer_layersmapping_add_item_cd(r_map,
                                                  cddata_type,
                                                  mix_mode,
                                                  mix_factor,
                                                  mix_weights,
                                                  data_src,
                                                  data_dst,
                                                  interp,
                                                  interp_data);
        }
      }

      if (data_dst_to_delete) {
        /* NOTE:
         * This won't affect newly created layers, if any, since tot_dst has not been updated!
         * Also, looping backward ensures us we do not suffer
         * from index shifting when deleting a layer. */
        for (idx_dst = tot_dst; idx_dst--;) {
          if (data_dst_to_delete[idx_dst]) {
            CustomData_free_layer(cd_dst, cddata_type, num_elem_dst, idx_dst);
          }
        }

        MEM_freeN(data_dst_to_delete);
      }
      break;
    default:
      return false;
  }

  return true;
}
