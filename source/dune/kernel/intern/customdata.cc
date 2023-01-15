/** \file
 * \ingroup bke
 * Implementation of CustomData.
 *
 * BKE_customdata.h contains the function prototypes for this file.
 */

#include "MEM_guardedalloc.h"

/* Since we have versioning code here (CustomData_verify_versions()). */
#define DNA_DEPRECATED_ALLOW

#include "DNA_ID.h"
#include "DNA_customdata_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_bitmap.h"
#include "BLI_color.hh"
#include "BLI_endian_switch.h"
#include "BLI_math.h"
#include "BLI_math_color_blend.h"
#include "BLI_math_vector.hh"
#include "BLI_mempool.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_string_utils.h"
#include "BLI_utildefines.h"

#ifndef NDEBUG
#  include "BLI_dynstr.h"
#endif

#include "BLT_translation.h"

#include "BKE_anonymous_attribute.h"
#include "BKE_customdata.h"
#include "BKE_customdata_file.h"
#include "BKE_deform.h"
#include "BKE_main.h"
#include "BKE_mesh_mapping.h"
#include "BKE_mesh_remap.h"
#include "BKE_multires.h"
#include "BKE_subsurf.h"

#include "BLO_read_write.h"

#include "bmesh.h"

#include "CLG_log.h"

/* only for customdata_data_transfer_interp_normal_normals */
#include "data_transfer_intern.h"

/* number of layers to add when growing a CustomData object */
#define CUSTOMDATA_GROW 5

/* ensure typemap size is ok */
BLI_STATIC_ASSERT(ARRAY_SIZE(((CustomData *)nullptr)->typemap) == CD_NUMTYPES, "size mismatch");

static CLG_LogRef LOG = {"bke.customdata"};

void CustomData_MeshMasks_update(CustomData_MeshMasks *mask_dst,
                                 const CustomData_MeshMasks *mask_src)
{
  mask_dst->vmask |= mask_src->vmask;
  mask_dst->emask |= mask_src->emask;
  mask_dst->fmask |= mask_src->fmask;
  mask_dst->pmask |= mask_src->pmask;
  mask_dst->lmask |= mask_src->lmask;
}

bool CustomData_MeshMasks_are_matching(const CustomData_MeshMasks *mask_ref,
                                       const CustomData_MeshMasks *mask_required)
{
  return (((mask_required->vmask & mask_ref->vmask) == mask_required->vmask) &&
          ((mask_required->emask & mask_ref->emask) == mask_required->emask) &&
          ((mask_required->fmask & mask_ref->fmask) == mask_required->fmask) &&
          ((mask_required->pmask & mask_ref->pmask) == mask_required->pmask) &&
          ((mask_required->lmask & mask_ref->lmask) == mask_required->lmask));
}

/********************* Layer type information **********************/
struct LayerTypeInfo {
  int size; /* the memory size of one element of this layer's data */

  /** name of the struct used, for file writing */
  const char *structname;
  /** number of structs per element, for file writing */
  int structnum;

  /**
   * default layer name.
   *
   * \note when null this is a way to ensure there is only ever one item
   * see: CustomData_layertype_is_singleton().
   */
  const char *defaultname;

  /**
   * a function to copy count elements of this layer's data
   * (deep copy if appropriate)
   * if null, memcpy is used
   */
  cd_copy copy;

  /**
   * a function to free any dynamically allocated components of this
   * layer's data (note the data pointer itself should not be freed)
   * size should be the size of one element of this layer's data (e.g.
   * LayerTypeInfo.size)
   */
  void (*free)(void *data, int count, int size);

  /**
   * a function to interpolate between count source elements of this
   * layer's data and store the result in dest
   * if weights == null or sub_weights == null, they should default to 1
   *
   * weights gives the weight for each element in sources
   * sub_weights gives the sub-element weights for each element in sources
   *    (there should be (sub element count)^2 weights per element)
   * count gives the number of elements in sources
   *
   * \note in some cases \a dest pointer is in \a sources
   *       so all functions have to take this into account and delay
   *       applying changes while reading from sources.
   *       See bug T32395 - Campbell.
   */
  cd_interp interp;

  /** a function to swap the data in corners of the element */
  void (*swap)(void *data, const int *corner_indices);

  /**
   * a function to set a layer's data to default values. if null, the
   * default is assumed to be all zeros */
  void (*set_default)(void *data, int count);

  /** A function used by mesh validating code, must ensures passed item has valid data. */
  cd_validate validate;

  /** functions necessary for geometry collapse */
  bool (*equal)(const void *data1, const void *data2);
  void (*multiply)(void *data, float fac);
  void (*initminmax)(void *min, void *max);
  void (*add)(void *data1, const void *data2);
  void (*dominmax)(const void *data1, void *min, void *max);
  void (*copyvalue)(const void *source, void *dest, const int mixmode, const float mixfactor);

  /** a function to read data from a cdf file */
  bool (*read)(CDataFile *cdf, void *data, int count);

  /** a function to write data to a cdf file */
  bool (*write)(CDataFile *cdf, const void *data, int count);

  /** a function to determine file size */
  size_t (*filesize)(CDataFile *cdf, const void *data, int count);

  /** a function to determine max allowed number of layers,
   * should be null or return -1 if no limit */
  int (*layers_max)();
};

static void layerCopy_mdeformvert(const void *source, void *dest, int count)
{
  int i, size = sizeof(MDeformVert);

  memcpy(dest, source, count * size);

  for (i = 0; i < count; i++) {
    MDeformVert *dvert = static_cast<MDeformVert *>(POINTER_OFFSET(dest, i * size));

    if (dvert->totweight) {
      MDeformWeight *dw = static_cast<MDeformWeight *>(
          MEM_malloc_arrayN(dvert->totweight, sizeof(*dw), __func__));

      memcpy(dw, dvert->dw, dvert->totweight * sizeof(*dw));
      dvert->dw = dw;
    }
    else {
      dvert->dw = nullptr;
    }
  }
}

static void layerFree_mdeformvert(void *data, int count, int size)
{
  for (int i = 0; i < count; i++) {
    MDeformVert *dvert = static_cast<MDeformVert *>(POINTER_OFFSET(data, i * size));

    if (dvert->dw) {
      MEM_freeN(dvert->dw);
      dvert->dw = nullptr;
      dvert->totweight = 0;
    }
  }
}

/* copy just zeros in this case */
static void layerCopy_bmesh_elem_py_ptr(const void *UNUSED(source), void *dest, int count)
{
  const int size = sizeof(void *);

  for (int i = 0; i < count; i++) {
    void **ptr = (void **)POINTER_OFFSET(dest, i * size);
    *ptr = nullptr;
  }
}

#ifndef WITH_PYTHON
void bpy_bm_generic_invalidate(struct BPy_BMGeneric *UNUSED(self))
{
  /* dummy */
}
#endif

static void layerFree_bmesh_elem_py_ptr(void *data, int count, int size)
{
  for (int i = 0; i < count; i++) {
    void **ptr = (void **)POINTER_OFFSET(data, i * size);
    if (*ptr) {
      bpy_bm_generic_invalidate(static_cast<BPy_BMGeneric *>(*ptr));
    }
  }
}

static void layerInterp_mdeformvert(const void **sources,
                                    const float *weights,
                                    const float *UNUSED(sub_weights),
                                    int count,
                                    void *dest)
{
  /* a single linked list of MDeformWeight's
   * use this to avoid double allocs (which LinkNode would do) */
  struct MDeformWeight_Link {
    struct MDeformWeight_Link *next;
    MDeformWeight dw;
  };

  MDeformVert *dvert = static_cast<MDeformVert *>(dest);
  struct MDeformWeight_Link *dest_dwlink = nullptr;
  struct MDeformWeight_Link *node;

  /* build a list of unique def_nrs for dest */
  int totweight = 0;
  for (int i = 0; i < count; i++) {
    const MDeformVert *source = static_cast<const MDeformVert *>(sources[i]);
    float interp_weight = weights[i];

    for (int j = 0; j < source->totweight; j++) {
      MDeformWeight *dw = &source->dw[j];
      float weight = dw->weight * interp_weight;

      if (weight == 0.0f) {
        continue;
      }

      for (node = dest_dwlink; node; node = node->next) {
        MDeformWeight *tmp_dw = &node->dw;

        if (tmp_dw->def_nr == dw->def_nr) {
          tmp_dw->weight += weight;
          break;
        }
      }

      /* if this def_nr is not in the list, add it */
      if (!node) {
        struct MDeformWeight_Link *tmp_dwlink = static_cast<MDeformWeight_Link *>(
            alloca(sizeof(*tmp_dwlink)));
        tmp_dwlink->dw.def_nr = dw->def_nr;
        tmp_dwlink->dw.weight = weight;

        /* Inline linked-list. */
        tmp_dwlink->next = dest_dwlink;
        dest_dwlink = tmp_dwlink;

        totweight++;
      }
    }
  }

  /* Delay writing to the destination in case dest is in sources. */

  /* now we know how many unique deform weights there are, so realloc */
  if (dvert->dw && (dvert->totweight == totweight)) {
    /* pass (fast-path if we don't need to realloc). */
  }
  else {
    if (dvert->dw) {
      MEM_freeN(dvert->dw);
    }

    if (totweight) {
      dvert->dw = static_cast<MDeformWeight *>(
          MEM_malloc_arrayN(totweight, sizeof(*dvert->dw), __func__));
    }
  }

  if (totweight) {
    dvert->totweight = totweight;
    int i = 0;
    for (node = dest_dwlink; node; node = node->next, i++) {
      if (node->dw.weight > 1.0f) {
        node->dw.weight = 1.0f;
      }
      dvert->dw[i] = node->dw;
    }
  }
  else {
    memset(dvert, 0, sizeof(*dvert));
  }
}

static void layerInterp_normal(const void **sources,
                               const float *weights,
                               const float *UNUSED(sub_weights),
                               int count,
                               void *dest)
{
  /* NOTE: This is linear interpolation, which is not optimal for vectors.
   * Unfortunately, spherical interpolation of more than two values is hairy,
   * so for now it will do... */
  float no[3] = {0.0f};

  while (count--) {
    madd_v3_v3fl(no, (const float *)sources[count], weights[count]);
  }

  /* Weighted sum of normalized vectors will **not** be normalized, even if weights are. */
  normalize_v3_v3((float *)dest, no);
}

static void layerCopyValue_normal(const void *source,
                                  void *dest,
                                  const int mixmode,
                                  const float mixfactor)
{
  const float *no_src = (const float *)source;
  float *no_dst = (float *)dest;
  float no_tmp[3];

  if (ELEM(mixmode,
           CDT_MIX_NOMIX,
           CDT_MIX_REPLACE_ABOVE_THRESHOLD,
           CDT_MIX_REPLACE_BELOW_THRESHOLD)) {
    /* Above/below threshold modes are not supported here, fallback to nomix (just in case). */
    copy_v3_v3(no_dst, no_src);
  }
  else { /* Modes that support 'real' mix factor. */
    /* Since we normalize in the end, MIX and ADD are the same op here. */
    if (ELEM(mixmode, CDT_MIX_MIX, CDT_MIX_ADD)) {
      add_v3_v3v3(no_tmp, no_dst, no_src);
      normalize_v3(no_tmp);
    }
    else if (mixmode == CDT_MIX_SUB) {
      sub_v3_v3v3(no_tmp, no_dst, no_src);
      normalize_v3(no_tmp);
    }
    else if (mixmode == CDT_MIX_MUL) {
      mul_v3_v3v3(no_tmp, no_dst, no_src);
      normalize_v3(no_tmp);
    }
    else {
      copy_v3_v3(no_tmp, no_src);
    }
    interp_v3_v3v3_slerp_safe(no_dst, no_dst, no_tmp, mixfactor);
  }
}

static void layerCopy_tface(const void *source, void *dest, int count)
{
  const MTFace *source_tf = (const MTFace *)source;
  MTFace *dest_tf = (MTFace *)dest;
  for (int i = 0; i < count; i++) {
    dest_tf[i] = source_tf[i];
  }
}

static void layerInterp_tface(
    const void **sources, const float *weights, const float *sub_weights, int count, void *dest)
{
  MTFace *tf = static_cast<MTFace *>(dest);
  float uv[4][2] = {{0.0f}};

  const float *sub_weight = sub_weights;
  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    const MTFace *src = static_cast<const MTFace *>(sources[i]);

    for (int j = 0; j < 4; j++) {
      if (sub_weights) {
        for (int k = 0; k < 4; k++, sub_weight++) {
          madd_v2_v2fl(uv[j], src->uv[k], (*sub_weight) * interp_weight);
        }
      }
      else {
        madd_v2_v2fl(uv[j], src->uv[j], interp_weight);
      }
    }
  }

  /* Delay writing to the destination in case dest is in sources. */
  *tf = *(MTFace *)(*sources);
  memcpy(tf->uv, uv, sizeof(tf->uv));
}

static void layerSwap_tface(void *data, const int *corner_indices)
{
  MTFace *tf = static_cast<MTFace *>(data);
  float uv[4][2];

  for (int j = 0; j < 4; j++) {
    const int source_index = corner_indices[j];
    copy_v2_v2(uv[j], tf->uv[source_index]);
  }

  memcpy(tf->uv, uv, sizeof(tf->uv));
}

static void layerDefault_tface(void *data, int count)
{
  static MTFace default_tf = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
  MTFace *tf = (MTFace *)data;

  for (int i = 0; i < count; i++) {
    tf[i] = default_tf;
  }
}

static int layerMaxNum_tface()
{
  return MAX_MTFACE;
}

static void layerCopy_propFloat(const void *source, void *dest, int count)
{
  memcpy(dest, source, sizeof(MFloatProperty) * count);
}

static void layerInterp_propFloat(const void **sources,
                                  const float *weights,
                                  const float *UNUSED(sub_weights),
                                  int count,
                                  void *dest)
{
  float result = 0.0f;
  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    const float src = *(const float *)sources[i];
    result += src * interp_weight;
  }
  *(float *)dest = result;
}

static bool layerValidate_propFloat(void *data, const uint totitems, const bool do_fixes)
{
  MFloatProperty *fp = static_cast<MFloatProperty *>(data);
  bool has_errors = false;

  for (int i = 0; i < totitems; i++, fp++) {
    if (!isfinite(fp->f)) {
      if (do_fixes) {
        fp->f = 0.0f;
      }
      has_errors = true;
    }
  }

  return has_errors;
}

static void layerCopy_propInt(const void *source, void *dest, int count)
{
  memcpy(dest, source, sizeof(MIntProperty) * count);
}

static void layerCopy_propString(const void *source, void *dest, int count)
{
  memcpy(dest, source, sizeof(MStringProperty) * count);
}

static void layerCopy_origspace_face(const void *source, void *dest, int count)
{
  const OrigSpaceFace *source_tf = (const OrigSpaceFace *)source;
  OrigSpaceFace *dest_tf = (OrigSpaceFace *)dest;

  for (int i = 0; i < count; i++) {
    dest_tf[i] = source_tf[i];
  }
}

static void layerInterp_origspace_face(
    const void **sources, const float *weights, const float *sub_weights, int count, void *dest)
{
  OrigSpaceFace *osf = static_cast<OrigSpaceFace *>(dest);
  float uv[4][2] = {{0.0f}};

  const float *sub_weight = sub_weights;
  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    const OrigSpaceFace *src = static_cast<const OrigSpaceFace *>(sources[i]);

    for (int j = 0; j < 4; j++) {
      if (sub_weights) {
        for (int k = 0; k < 4; k++, sub_weight++) {
          madd_v2_v2fl(uv[j], src->uv[k], (*sub_weight) * interp_weight);
        }
      }
      else {
        madd_v2_v2fl(uv[j], src->uv[j], interp_weight);
      }
    }
  }

  /* Delay writing to the destination in case dest is in sources. */
  memcpy(osf->uv, uv, sizeof(osf->uv));
}

static void layerSwap_origspace_face(void *data, const int *corner_indices)
{
  OrigSpaceFace *osf = static_cast<OrigSpaceFace *>(data);
  float uv[4][2];

  for (int j = 0; j < 4; j++) {
    copy_v2_v2(uv[j], osf->uv[corner_indices[j]]);
  }
  memcpy(osf->uv, uv, sizeof(osf->uv));
}

static void layerDefault_origspace_face(void *data, int count)
{
  static OrigSpaceFace default_osf = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
  OrigSpaceFace *osf = (OrigSpaceFace *)data;

  for (int i = 0; i < count; i++) {
    osf[i] = default_osf;
  }
}

static void layerSwap_mdisps(void *data, const int *ci)
{
  MDisps *s = static_cast<MDisps *>(data);

  if (s->disps) {
    int nverts = (ci[1] == 3) ? 4 : 3; /* silly way to know vertex count of face */
    int corners = multires_mdisp_corners(s);
    int cornersize = s->totdisp / corners;

    if (corners != nverts) {
      /* happens when face changed vertex count in edit mode
       * if it happened, just forgot displacement */

      MEM_freeN(s->disps);
      s->totdisp = (s->totdisp / corners) * nverts;
      s->disps = (float(*)[3])MEM_calloc_arrayN(s->totdisp, sizeof(float[3]), "mdisp swap");
      return;
    }

    float(*d)[3] = (float(*)[3])MEM_calloc_arrayN(s->totdisp, sizeof(float[3]), "mdisps swap");

    for (int S = 0; S < corners; S++) {
      memcpy(d + cornersize * S, s->disps + cornersize * ci[S], sizeof(float[3]) * cornersize);
    }

    MEM_freeN(s->disps);
    s->disps = d;
  }
}

static void layerCopy_mdisps(const void *source, void *dest, int count)
{
  const MDisps *s = static_cast<const MDisps *>(source);
  MDisps *d = static_cast<MDisps *>(dest);

  for (int i = 0; i < count; i++) {
    if (s[i].disps) {
      d[i].disps = static_cast<float(*)[3]>(MEM_dupallocN(s[i].disps));
      d[i].hidden = static_cast<unsigned int *>(MEM_dupallocN(s[i].hidden));
    }
    else {
      d[i].disps = nullptr;
      d[i].hidden = nullptr;
    }

    /* still copy even if not in memory, displacement can be external */
    d[i].totdisp = s[i].totdisp;
    d[i].level = s[i].level;
  }
}

static void layerFree_mdisps(void *data, int count, int UNUSED(size))
{
  MDisps *d = static_cast<MDisps *>(data);

  for (int i = 0; i < count; i++) {
    if (d[i].disps) {
      MEM_freeN(d[i].disps);
    }
    if (d[i].hidden) {
      MEM_freeN(d[i].hidden);
    }
    d[i].disps = nullptr;
    d[i].hidden = nullptr;
    d[i].totdisp = 0;
    d[i].level = 0;
  }
}

static bool layerRead_mdisps(CDataFile *cdf, void *data, int count)
{
  MDisps *d = static_cast<MDisps *>(data);

  for (int i = 0; i < count; i++) {
    if (!d[i].disps) {
      d[i].disps = (float(*)[3])MEM_calloc_arrayN(d[i].totdisp, sizeof(float[3]), "mdisps read");
    }

    if (!cdf_read_data(cdf, sizeof(float[3]) * d[i].totdisp, d[i].disps)) {
      CLOG_ERROR(&LOG, "failed to read multires displacement %d/%d %d", i, count, d[i].totdisp);
      return false;
    }
  }

  return true;
}

static bool layerWrite_mdisps(CDataFile *cdf, const void *data, int count)
{
  const MDisps *d = static_cast<const MDisps *>(data);

  for (int i = 0; i < count; i++) {
    if (!cdf_write_data(cdf, sizeof(float[3]) * d[i].totdisp, d[i].disps)) {
      CLOG_ERROR(&LOG, "failed to write multires displacement %d/%d %d", i, count, d[i].totdisp);
      return false;
    }
  }

  return true;
}

static size_t layerFilesize_mdisps(CDataFile *UNUSED(cdf), const void *data, int count)
{
  const MDisps *d = static_cast<const MDisps *>(data);
  size_t size = 0;

  for (int i = 0; i < count; i++) {
    size += sizeof(float[3]) * d[i].totdisp;
  }

  return size;
}
static void layerInterp_paint_mask(const void **sources,
                                   const float *weights,
                                   const float *UNUSED(sub_weights),
                                   int count,
                                   void *dest)
{
  float mask = 0.0f;
  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    const float *src = static_cast<const float *>(sources[i]);
    mask += (*src) * interp_weight;
  }
  *(float *)dest = mask;
}

static void layerCopy_grid_paint_mask(const void *source, void *dest, int count)
{
  const GridPaintMask *s = static_cast<const GridPaintMask *>(source);
  GridPaintMask *d = static_cast<GridPaintMask *>(dest);

  for (int i = 0; i < count; i++) {
    if (s[i].data) {
      d[i].data = static_cast<float *>(MEM_dupallocN(s[i].data));
      d[i].level = s[i].level;
    }
    else {
      d[i].data = nullptr;
      d[i].level = 0;
    }
  }
}

static void layerFree_grid_paint_mask(void *data, int count, int UNUSED(size))
{
  GridPaintMask *gpm = static_cast<GridPaintMask *>(data);

  for (int i = 0; i < count; i++) {
    MEM_SAFE_FREE(gpm[i].data);
    gpm[i].level = 0;
  }
}

/* --------- */
static void layerCopyValue_mloopcol(const void *source,
                                    void *dest,
                                    const int mixmode,
                                    const float mixfactor)
{
  const MLoopCol *m1 = static_cast<const MLoopCol *>(source);
  MLoopCol *m2 = static_cast<MLoopCol *>(dest);
  unsigned char tmp_col[4];

  if (ELEM(mixmode,
           CDT_MIX_NOMIX,
           CDT_MIX_REPLACE_ABOVE_THRESHOLD,
           CDT_MIX_REPLACE_BELOW_THRESHOLD)) {
    /* Modes that do a full copy or nothing. */
    if (ELEM(mixmode, CDT_MIX_REPLACE_ABOVE_THRESHOLD, CDT_MIX_REPLACE_BELOW_THRESHOLD)) {
      /* TODO: Check for a real valid way to get 'factor' value of our dest color? */
      const float f = ((float)m2->r + (float)m2->g + (float)m2->b) / 3.0f;
      if (mixmode == CDT_MIX_REPLACE_ABOVE_THRESHOLD && f < mixfactor) {
        return; /* Do Nothing! */
      }
      if (mixmode == CDT_MIX_REPLACE_BELOW_THRESHOLD && f > mixfactor) {
        return; /* Do Nothing! */
      }
    }
    m2->r = m1->r;
    m2->g = m1->g;
    m2->b = m1->b;
    m2->a = m1->a;
  }
  else { /* Modes that support 'real' mix factor. */
    unsigned char src[4] = {m1->r, m1->g, m1->b, m1->a};
    unsigned char dst[4] = {m2->r, m2->g, m2->b, m2->a};

    if (mixmode == CDT_MIX_MIX) {
      blend_color_mix_byte(tmp_col, dst, src);
    }
    else if (mixmode == CDT_MIX_ADD) {
      blend_color_add_byte(tmp_col, dst, src);
    }
    else if (mixmode == CDT_MIX_SUB) {
      blend_color_sub_byte(tmp_col, dst, src);
    }
    else if (mixmode == CDT_MIX_MUL) {
      blend_color_mul_byte(tmp_col, dst, src);
    }
    else {
      memcpy(tmp_col, src, sizeof(tmp_col));
    }

    blend_color_interpolate_byte(dst, dst, tmp_col, mixfactor);

    m2->r = (char)dst[0];
    m2->g = (char)dst[1];
    m2->b = (char)dst[2];
    m2->a = (char)dst[3];
  }
}

static bool layerEqual_mloopcol(const void *data1, const void *data2)
{
  const MLoopCol *m1 = static_cast<const MLoopCol *>(data1);
  const MLoopCol *m2 = static_cast<const MLoopCol *>(data2);
  float r, g, b, a;

  r = m1->r - m2->r;
  g = m1->g - m2->g;
  b = m1->b - m2->b;
  a = m1->a - m2->a;

  return r * r + g * g + b * b + a * a < 0.001f;
}

static void layerMultiply_mloopcol(void *data, float fac)
{
  MLoopCol *m = static_cast<MLoopCol *>(data);

  m->r = (float)m->r * fac;
  m->g = (float)m->g * fac;
  m->b = (float)m->b * fac;
  m->a = (float)m->a * fac;
}

static void layerAdd_mloopcol(void *data1, const void *data2)
{
  MLoopCol *m = static_cast<MLoopCol *>(data1);
  const MLoopCol *m2 = static_cast<const MLoopCol *>(data2);

  m->r += m2->r;
  m->g += m2->g;
  m->b += m2->b;
  m->a += m2->a;
}

static void layerDoMinMax_mloopcol(const void *data, void *vmin, void *vmax)
{
  const MLoopCol *m = static_cast<const MLoopCol *>(data);
  MLoopCol *min = static_cast<MLoopCol *>(vmin);
  MLoopCol *max = static_cast<MLoopCol *>(vmax);

  if (m->r < min->r) {
    min->r = m->r;
  }
  if (m->g < min->g) {
    min->g = m->g;
  }
  if (m->b < min->b) {
    min->b = m->b;
  }
  if (m->a < min->a) {
    min->a = m->a;
  }
  if (m->r > max->r) {
    max->r = m->r;
  }
  if (m->g > max->g) {
    max->g = m->g;
  }
  if (m->b > max->b) {
    max->b = m->b;
  }
  if (m->a > max->a) {
    max->a = m->a;
  }
}

static void layerInitMinMax_mloopcol(void *vmin, void *vmax)
{
  MLoopCol *min = static_cast<MLoopCol *>(vmin);
  MLoopCol *max = static_cast<MLoopCol *>(vmax);

  min->r = 255;
  min->g = 255;
  min->b = 255;
  min->a = 255;

  max->r = 0;
  max->g = 0;
  max->b = 0;
  max->a = 0;
}

static void layerDefault_mloopcol(void *data, int count)
{
  MLoopCol default_mloopcol = {255, 255, 255, 255};
  MLoopCol *mlcol = (MLoopCol *)data;
  for (int i = 0; i < count; i++) {
    mlcol[i] = default_mloopcol;
  }
}

static void layerInterp_mloopcol(const void **sources,
                                 const float *weights,
                                 const float *UNUSED(sub_weights),
                                 int count,
                                 void *dest)
{
  MLoopCol *mc = static_cast<MLoopCol *>(dest);
  struct {
    float a;
    float r;
    float g;
    float b;
  } col = {0};

  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    const MLoopCol *src = static_cast<const MLoopCol *>(sources[i]);
    col.r += src->r * interp_weight;
    col.g += src->g * interp_weight;
    col.b += src->b * interp_weight;
    col.a += src->a * interp_weight;
  }

  /* Subdivide smooth or fractal can cause problems without clamping
   * although weights should also not cause this situation */

  /* Also delay writing to the destination in case dest is in sources. */
  mc->r = round_fl_to_uchar_clamp(col.r);
  mc->g = round_fl_to_uchar_clamp(col.g);
  mc->b = round_fl_to_uchar_clamp(col.b);
  mc->a = round_fl_to_uchar_clamp(col.a);
}

static int layerMaxNum_mloopcol()
{
  return MAX_MCOL;
}

static void layerCopyValue_mloopuv(const void *source,
                                   void *dest,
                                   const int mixmode,
                                   const float mixfactor)
{
  const MLoopUV *luv1 = static_cast<const MLoopUV *>(source);
  MLoopUV *luv2 = static_cast<MLoopUV *>(dest);

  /* We only support a limited subset of advanced mixing here -
   * namely the mixfactor interpolation. */

  if (mixmode == CDT_MIX_NOMIX) {
    copy_v2_v2(luv2->uv, luv1->uv);
  }
  else {
    interp_v2_v2v2(luv2->uv, luv2->uv, luv1->uv, mixfactor);
  }
}

static bool layerEqual_mloopuv(const void *data1, const void *data2)
{
  const MLoopUV *luv1 = static_cast<const MLoopUV *>(data1);
  const MLoopUV *luv2 = static_cast<const MLoopUV *>(data2);

  return len_squared_v2v2(luv1->uv, luv2->uv) < 0.00001f;
}

static void layerMultiply_mloopuv(void *data, float fac)
{
  MLoopUV *luv = static_cast<MLoopUV *>(data);

  mul_v2_fl(luv->uv, fac);
}

static void layerInitMinMax_mloopuv(void *vmin, void *vmax)
{
  MLoopUV *min = static_cast<MLoopUV *>(vmin);
  MLoopUV *max = static_cast<MLoopUV *>(vmax);

  INIT_MINMAX2(min->uv, max->uv);
}

static void layerDoMinMax_mloopuv(const void *data, void *vmin, void *vmax)
{
  const MLoopUV *luv = static_cast<const MLoopUV *>(data);
  MLoopUV *min = static_cast<MLoopUV *>(vmin);
  MLoopUV *max = static_cast<MLoopUV *>(vmax);

  minmax_v2v2_v2(min->uv, max->uv, luv->uv);
}

static void layerAdd_mloopuv(void *data1, const void *data2)
{
  MLoopUV *l1 = static_cast<MLoopUV *>(data1);
  const MLoopUV *l2 = static_cast<const MLoopUV *>(data2);

  add_v2_v2(l1->uv, l2->uv);
}

static void layerInterp_mloopuv(const void **sources,
                                const float *weights,
                                const float *UNUSED(sub_weights),
                                int count,
                                void *dest)
{
  float uv[2];
  int flag = 0;

  zero_v2(uv);

  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    const MLoopUV *src = static_cast<const MLoopUV *>(sources[i]);
    madd_v2_v2fl(uv, src->uv, interp_weight);
    if (interp_weight > 0.0f) {
      flag |= src->flag;
    }
  }

  /* Delay writing to the destination in case dest is in sources. */
  copy_v2_v2(((MLoopUV *)dest)->uv, uv);
  ((MLoopUV *)dest)->flag = flag;
}

static bool layerValidate_mloopuv(void *data, const uint totitems, const bool do_fixes)
{
  MLoopUV *uv = static_cast<MLoopUV *>(data);
  bool has_errors = false;

  for (int i = 0; i < totitems; i++, uv++) {
    if (!is_finite_v2(uv->uv)) {
      if (do_fixes) {
        zero_v2(uv->uv);
      }
      has_errors = true;
    }
  }

  return has_errors;
}

/* origspace is almost exact copy of mloopuv's, keep in sync */
static void layerCopyValue_mloop_origspace(const void *source,
                                           void *dest,
                                           const int UNUSED(mixmode),
                                           const float UNUSED(mixfactor))
{
  const OrigSpaceLoop *luv1 = static_cast<const OrigSpaceLoop *>(source);
  OrigSpaceLoop *luv2 = static_cast<OrigSpaceLoop *>(dest);

  copy_v2_v2(luv2->uv, luv1->uv);
}

static bool layerEqual_mloop_origspace(const void *data1, const void *data2)
{
  const OrigSpaceLoop *luv1 = static_cast<const OrigSpaceLoop *>(data1);
  const OrigSpaceLoop *luv2 = static_cast<const OrigSpaceLoop *>(data2);

  return len_squared_v2v2(luv1->uv, luv2->uv) < 0.00001f;
}

static void layerMultiply_mloop_origspace(void *data, float fac)
{
  OrigSpaceLoop *luv = static_cast<OrigSpaceLoop *>(data);

  mul_v2_fl(luv->uv, fac);
}

static void layerInitMinMax_mloop_origspace(void *vmin, void *vmax)
{
  OrigSpaceLoop *min = static_cast<OrigSpaceLoop *>(vmin);
  OrigSpaceLoop *max = static_cast<OrigSpaceLoop *>(vmax);

  INIT_MINMAX2(min->uv, max->uv);
}

static void layerDoMinMax_mloop_origspace(const void *data, void *vmin, void *vmax)
{
  const OrigSpaceLoop *luv = static_cast<const OrigSpaceLoop *>(data);
  OrigSpaceLoop *min = static_cast<OrigSpaceLoop *>(vmin);
  OrigSpaceLoop *max = static_cast<OrigSpaceLoop *>(vmax);

  minmax_v2v2_v2(min->uv, max->uv, luv->uv);
}

static void layerAdd_mloop_origspace(void *data1, const void *data2)
{
  OrigSpaceLoop *l1 = static_cast<OrigSpaceLoop *>(data1);
  const OrigSpaceLoop *l2 = static_cast<const OrigSpaceLoop *>(data2);

  add_v2_v2(l1->uv, l2->uv);
}

static void layerInterp_mloop_origspace(const void **sources,
                                        const float *weights,
                                        const float *UNUSED(sub_weights),
                                        int count,
                                        void *dest)
{
  float uv[2];
  zero_v2(uv);

  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    const OrigSpaceLoop *src = static_cast<const OrigSpaceLoop *>(sources[i]);
    madd_v2_v2fl(uv, src->uv, interp_weight);
  }

  /* Delay writing to the destination in case dest is in sources. */
  copy_v2_v2(((OrigSpaceLoop *)dest)->uv, uv);
}
/* --- end copy */

static void layerInterp_mcol(
    const void **sources, const float *weights, const float *sub_weights, int count, void *dest)
{
  MCol *mc = static_cast<MCol *>(dest);
  struct {
    float a;
    float r;
    float g;
    float b;
  } col[4] = {{0.0f}};

  const float *sub_weight = sub_weights;
  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];

    for (int j = 0; j < 4; j++) {
      if (sub_weights) {
        const MCol *src = static_cast<const MCol *>(sources[i]);
        for (int k = 0; k < 4; k++, sub_weight++, src++) {
          const float w = (*sub_weight) * interp_weight;
          col[j].a += src->a * w;
          col[j].r += src->r * w;
          col[j].g += src->g * w;
          col[j].b += src->b * w;
        }
      }
      else {
        const MCol *src = static_cast<const MCol *>(sources[i]);
        col[j].a += src[j].a * interp_weight;
        col[j].r += src[j].r * interp_weight;
        col[j].g += src[j].g * interp_weight;
        col[j].b += src[j].b * interp_weight;
      }
    }
  }

  /* Delay writing to the destination in case dest is in sources. */
  for (int j = 0; j < 4; j++) {

    /* Subdivide smooth or fractal can cause problems without clamping
     * although weights should also not cause this situation */
    mc[j].a = round_fl_to_uchar_clamp(col[j].a);
    mc[j].r = round_fl_to_uchar_clamp(col[j].r);
    mc[j].g = round_fl_to_uchar_clamp(col[j].g);
    mc[j].b = round_fl_to_uchar_clamp(col[j].b);
  }
}

static void layerSwap_mcol(void *data, const int *corner_indices)
{
  MCol *mcol = static_cast<MCol *>(data);
  MCol col[4];

  for (int j = 0; j < 4; j++) {
    col[j] = mcol[corner_indices[j]];
  }

  memcpy(mcol, col, sizeof(col));
}

static void layerDefault_mcol(void *data, int count)
{
  static MCol default_mcol = {255, 255, 255, 255};
  MCol *mcol = (MCol *)data;

  for (int i = 0; i < 4 * count; i++) {
    mcol[i] = default_mcol;
  }
}

static void layerDefault_origindex(void *data, int count)
{
  copy_vn_i((int *)data, count, ORIGINDEX_NONE);
}

static void layerInterp_bweight(const void **sources,
                                const float *weights,
                                const float *UNUSED(sub_weights),
                                int count,
                                void *dest)
{
  float **in = (float **)sources;

  if (count <= 0) {
    return;
  }

  float f = 0.0f;

  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    f += *in[i] * interp_weight;
  }

  /* Delay writing to the destination in case dest is in sources. */
  *((float *)dest) = f;
}

static void layerInterp_shapekey(const void **sources,
                                 const float *weights,
                                 const float *UNUSED(sub_weights),
                                 int count,
                                 void *dest)
{
  float **in = (float **)sources;

  if (count <= 0) {
    return;
  }

  float co[3];
  zero_v3(co);

  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    madd_v3_v3fl(co, in[i], interp_weight);
  }

  /* Delay writing to the destination in case dest is in sources. */
  copy_v3_v3((float *)dest, co);
}

static void layerDefault_mvert_skin(void *data, int count)
{
  MVertSkin *vs = static_cast<MVertSkin *>(data);

  for (int i = 0; i < count; i++) {
    copy_v3_fl(vs[i].radius, 0.25f);
    vs[i].flag = 0;
  }
}

static void layerCopy_mvert_skin(const void *source, void *dest, int count)
{
  memcpy(dest, source, sizeof(MVertSkin) * count);
}

static void layerInterp_mvert_skin(const void **sources,
                                   const float *weights,
                                   const float *UNUSED(sub_weights),
                                   int count,
                                   void *dest)
{
  float radius[3];
  zero_v3(radius);

  for (int i = 0; i < count; i++) {
    const float interp_weight = weights[i];
    const MVertSkin *vs_src = static_cast<const MVertSkin *>(sources[i]);

    madd_v3_v3fl(radius, vs_src->radius, interp_weight);
  }

  /* Delay writing to the destination in case dest is in sources. */
  MVertSkin *vs_dst = static_cast<MVertSkin *>(dest);
  copy_v3_v3(vs_dst->radius, radius);
  vs_dst->flag &= ~MVERT_SKIN_ROOT;
}

static void layerSwap_flnor(void *data, const int *corner_indices)
{
  short(*flnors)[4][3] = static_cast<short(*)[4][3]>(data);
  short nors[4][3];
  int i = 4;

  while (i--) {
    copy_v3_v3_short(nors[i], (*flnors)[corner_indices[i]]);
  }

  memcpy(flnors, nors, sizeof(nors));
}
