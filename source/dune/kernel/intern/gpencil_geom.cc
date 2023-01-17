#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CLG_log.h"

#include "MEM_guardedalloc.h"

#include "BLI_array_utils.h"
#include "BLI_blenlib.h"
#include "BLI_ghash.h"
#include "BLI_hash.h"
#include "BLI_heap.h"
#include "BLI_math_vec_types.hh"
#include "BLI_math_vector.h"
#include "BLI_polyfill_2d.h"
#include "BLI_span.hh"

#include "BLT_translation.h"

#include "DNA_gpencil_modifier_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BLT_translation.h"

#include "BKE_context.h"
#include "BKE_deform.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_curve.h"
#include "BKE_gpencil_geom.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_object.h"

#include "DEG_depsgraph_query.h"

using blender::float3;
using blender::Span;

/* -------------------------------------------------------------------- */
/** \name Grease Pencil Object: Bound-box Support
 * \{ */

bool BKE_gpencil_stroke_minmax(const bGPDstroke *gps,
                               const bool use_select,
                               float r_min[3],
                               float r_max[3])
{
  if (gps == nullptr) {
    return false;
  }

  bool changed = false;
  if (use_select) {
    for (const bGPDspoint &pt : Span(gps->points, gps->totpoints)) {
      if (pt.flag & GP_SPOINT_SELECT) {
        minmax_v3v3_v3(r_min, r_max, &pt.x);
        changed = true;
      }
    }
  }
  else {
    for (const bGPDspoint &pt : Span(gps->points, gps->totpoints)) {
      minmax_v3v3_v3(r_min, r_max, &pt.x);
      changed = true;
    }
  }

  return changed;
}

bool BKE_gpencil_data_minmax(const bGPdata *gpd, float r_min[3], float r_max[3])
{
  bool changed = false;

  INIT_MINMAX(r_min, r_max);

  if (gpd == nullptr) {
    return changed;
  }

  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    bGPDframe *gpf = gpl->actframe;

    if (gpf != nullptr) {
      LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
        changed |= BKE_gpencil_stroke_minmax(gps, false, r_min, r_max);
      }
    }
  }

  return changed;
}

void BKE_gpencil_centroid_3d(bGPdata *gpd, float r_centroid[3])
{
  float3 min;
  float3 max;
  BKE_gpencil_data_minmax(gpd, min, max);

  const float3 tot = min + max;
  mul_v3_v3fl(r_centroid, tot, 0.5f);
}

void BKE_gpencil_stroke_boundingbox_calc(bGPDstroke *gps)
{
  INIT_MINMAX(gps->boundbox_min, gps->boundbox_max);
  BKE_gpencil_stroke_minmax(gps, false, gps->boundbox_min, gps->boundbox_max);
}

/**
 * Create bounding box values.
 * \param ob: Grease pencil object
 */
static void boundbox_gpencil(Object *ob)
{
  if (ob->runtime.bb == nullptr) {
    ob->runtime.bb = MEM_cnew<BoundBox>("GPencil boundbox");
  }

  BoundBox *bb = ob->runtime.bb;
  bGPdata *gpd = (bGPdata *)ob->data;

  float3 min;
  float3 max;
  if (!BKE_gpencil_data_minmax(gpd, min, max)) {
    min = float3(-1);
    max = float3(1);
  }

  BKE_boundbox_init_from_minmax(bb, min, max);

  bb->flag &= ~BOUNDBOX_DIRTY;
}

BoundBox *BKE_gpencil_boundbox_get(Object *ob)
{
  if (ELEM(nullptr, ob, ob->data)) {
    return nullptr;
  }

  bGPdata *gpd = (bGPdata *)ob->data;
  if ((ob->runtime.bb) && ((gpd->flag & GP_DATA_CACHE_IS_DIRTY) == 0)) {
    return ob->runtime.bb;
  }

  boundbox_gpencil(ob);

  Object *ob_orig = (Object *)DEG_get_original_id(&ob->id);
  /* Update orig object's boundbox with re-computed evaluated values. This function can be
   * called with the evaluated object and need update the original object bound box data
   * to keep both values synchronized. */
  if (!ELEM(ob_orig, nullptr, ob)) {
    if (ob_orig->runtime.bb == nullptr) {
      ob_orig->runtime.bb = MEM_cnew<BoundBox>("GPencil boundbox");
    }
    for (int i = 0; i < 8; i++) {
      copy_v3_v3(ob_orig->runtime.bb->vec[i], ob->runtime.bb->vec[i]);
    }
  }

  return ob->runtime.bb;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Stroke Sample
 * \{ */

static int stroke_march_next_point(const bGPDstroke *gps,
                                   const int index_next_pt,
                                   const float *current,
                                   const float dist,
                                   float *result,
                                   float *pressure,
                                   float *strength,
                                   float *vert_color,
                                   float *ratio_result,
                                   int *index_from,
                                   int *index_to)
{
  float remaining_till_next = 0.0f;
  float remaining_march = dist;
  float step_start[3];
  float point[3];
  int next_point_index = index_next_pt;
  bGPDspoint *pt = nullptr;

  if (next_point_index == gps->totpoints) {
    next_point_index = 0;
  }

  copy_v3_v3(step_start, current);
  pt = &gps->points[next_point_index];
  copy_v3_v3(point, &pt->x);
  remaining_till_next = len_v3v3(point, step_start);

  while (remaining_till_next < remaining_march && next_point_index) {
    remaining_march -= remaining_till_next;
    pt = &gps->points[next_point_index];
    if (pt->flag & GP_SPOINT_TEMP_TAG) {
      pt = &gps->points[next_point_index];
      copy_v3_v3(result, &pt->x);
      *pressure = gps->points[next_point_index].pressure;
      *strength = gps->points[next_point_index].strength;
      memcpy(vert_color, gps->points[next_point_index].vert_color, sizeof(float[4]));

      *index_from = next_point_index == 0 ? (gps->totpoints - 1) : (next_point_index - 1);
      *index_to = next_point_index;
      *ratio_result = 1.0f;
      next_point_index++;
      return next_point_index == 0 ? gps->totpoints : next_point_index;
    }
    next_point_index++;
    copy_v3_v3(point, &pt->x);
    copy_v3_v3(step_start, point);
    if (!(next_point_index < gps->totpoints)) {
      if (gps->flag & GP_STROKE_CYCLIC) {
        next_point_index = 0;
      }
      else {
        next_point_index = gps->totpoints - 1;
        break;
      }
    }
    pt = &gps->points[next_point_index];
    copy_v3_v3(point, &pt->x);
    remaining_till_next = len_v3v3(point, step_start);
  }
  if (remaining_till_next < remaining_march) {
    pt = &gps->points[next_point_index];
    copy_v3_v3(result, &pt->x);
    *pressure = gps->points[next_point_index].pressure;
    *strength = gps->points[next_point_index].strength;
    memcpy(vert_color, gps->points[next_point_index].vert_color, sizeof(float[4]));

    *index_from = next_point_index == 0 ? (gps->totpoints - 1) : (next_point_index - 1);
    *index_to = next_point_index;
    *ratio_result = 1.0f;

    return 0;
  }

  *index_from = next_point_index == 0 ? (gps->totpoints - 1) : (next_point_index - 1);
  *index_to = next_point_index;

  float ratio = remaining_march / remaining_till_next;
  interp_v3_v3v3(result, step_start, point, ratio);
  *ratio_result = ratio;

  *pressure = interpf(
      gps->points[next_point_index].pressure, gps->points[*index_from].pressure, ratio);
  *strength = interpf(
      gps->points[next_point_index].strength, gps->points[*index_from].strength, ratio);
  interp_v4_v4v4(vert_color,
                 gps->points[*index_from].vert_color,
                 gps->points[next_point_index].vert_color,
                 ratio);

  return next_point_index == 0 ? gps->totpoints : next_point_index;
}

static int stroke_march_next_point_no_interp(const bGPDstroke *gps,
                                             const int index_next_pt,
                                             const float *current,
                                             const float dist,
                                             const float sharp_threshold,
                                             float *result)
{
  float remaining_till_next = 0.0f;
  float remaining_march = dist;
  float step_start[3];
  float point[3];
  int next_point_index = index_next_pt;
  bGPDspoint *pt = nullptr;

  if (next_point_index == gps->totpoints) {
    next_point_index = 0;
  }

  copy_v3_v3(step_start, current);
  pt = &gps->points[next_point_index];
  copy_v3_v3(point, &pt->x);
  remaining_till_next = len_v3v3(point, step_start);

  while (remaining_till_next < remaining_march && next_point_index) {
    remaining_march -= remaining_till_next;
    pt = &gps->points[next_point_index];
    if (next_point_index < gps->totpoints - 1 &&
        angle_v3v3v3(&gps->points[next_point_index - 1].x,
                     &gps->points[next_point_index].x,
                     &gps->points[next_point_index + 1].x) < sharp_threshold) {
      copy_v3_v3(result, &pt->x);
      pt->flag |= GP_SPOINT_TEMP_TAG;
      next_point_index++;
      return next_point_index == 0 ? gps->totpoints : next_point_index;
    }
    next_point_index++;
    copy_v3_v3(point, &pt->x);
    copy_v3_v3(step_start, point);
    if (!(next_point_index < gps->totpoints)) {
      if (gps->flag & GP_STROKE_CYCLIC) {
        next_point_index = 0;
      }
      else {
        next_point_index = gps->totpoints - 1;
        break;
      }
    }
    pt = &gps->points[next_point_index];
    copy_v3_v3(point, &pt->x);
    remaining_till_next = len_v3v3(point, step_start);
  }
  if (remaining_till_next < remaining_march) {
    pt = &gps->points[next_point_index];
    copy_v3_v3(result, &pt->x);
    /* Stroke marching only terminates here. */
    return 0;
  }

  float ratio = remaining_march / remaining_till_next;
  interp_v3_v3v3(result, step_start, point, ratio);
  return next_point_index == 0 ? gps->totpoints : next_point_index;
}

static int stroke_march_count(const bGPDstroke *gps, const float dist, const float sharp_threshold)
{
  int point_count = 0;
  float point[3];
  int next_point_index = 1;
  bGPDspoint *pt = nullptr;

  pt = &gps->points[0];
  copy_v3_v3(point, &pt->x);
  point_count++;

  /* Sharp points will be tagged by the stroke_march_next_point_no_interp() call below. */
  for (int i = 0; i < gps->totpoints; i++) {
    gps->points[i].flag &= (~GP_SPOINT_TEMP_TAG);
  }

  while ((next_point_index = stroke_march_next_point_no_interp(
              gps, next_point_index, point, dist, sharp_threshold, point)) > -1) {
    point_count++;
    if (next_point_index == 0) {
      break; /* last point finished */
    }
  }
  return point_count;
}

static void stroke_defvert_create_nr_list(MDeformVert *dv_list,
                                          int count,
                                          ListBase *result,
                                          int *totweight)
{
  LinkData *ld;
  MDeformVert *dv;
  MDeformWeight *dw;
  int i, j;
  int tw = 0;
  for (i = 0; i < count; i++) {
    dv = &dv_list[i];

    /* find def_nr in list, if not exist, then create one */
    for (j = 0; j < dv->totweight; j++) {
      bool found = false;
      dw = &dv->dw[j];
      for (ld = (LinkData *)result->first; ld; ld = ld->next) {
        if (ld->data == POINTER_FROM_INT(dw->def_nr)) {
          found = true;
          break;
        }
      }
      if (!found) {
        ld = MEM_cnew<LinkData>("def_nr_item");
        ld->data = POINTER_FROM_INT(dw->def_nr);
        BLI_addtail(result, ld);
        tw++;
      }
    }
  }

  *totweight = tw;
}

static MDeformVert *stroke_defvert_new_count(int count, int totweight, ListBase *def_nr_list)
{
  int i, j;
  LinkData *ld;
  MDeformVert *dst = (MDeformVert *)MEM_mallocN(count * sizeof(MDeformVert), "new_deformVert");

  for (i = 0; i < count; i++) {
    dst[i].dw = (MDeformWeight *)MEM_mallocN(sizeof(MDeformWeight) * totweight,
                                             "new_deformWeight");
    dst[i].totweight = totweight;
    j = 0;
    /* re-assign deform groups */
    for (ld = (LinkData *)def_nr_list->first; ld; ld = ld->next) {
      dst[i].dw[j].def_nr = POINTER_AS_INT(ld->data);
      j++;
    }
  }

  return dst;
}

static void stroke_interpolate_deform_weights(
    bGPDstroke *gps, int index_from, int index_to, float ratio, MDeformVert *vert)
{
  const MDeformVert *vl = &gps->dvert[index_from];
  const MDeformVert *vr = &gps->dvert[index_to];

  for (int i = 0; i < vert->totweight; i++) {
    float wl = BKE_defvert_find_weight(vl, vert->dw[i].def_nr);
    float wr = BKE_defvert_find_weight(vr, vert->dw[i].def_nr);
    vert->dw[i].weight = interpf(wr, wl, ratio);
  }
}

bool BKE_gpencil_stroke_sample(bGPdata *gpd,
                               bGPDstroke *gps,
                               const float dist,
                               const bool select,
                               const float sharp_threshold)
{
  bGPDspoint *pt = gps->points;
  bGPDspoint *pt1 = nullptr;
  bGPDspoint *pt2 = nullptr;
  LinkData *ld;
  ListBase def_nr_list = {nullptr};

  if (gps->totpoints < 2 || dist < FLT_EPSILON) {
    return false;
  }
  /* TODO: Implement feature point preservation. */
  int count = stroke_march_count(gps, dist, sharp_threshold);
  const bool is_cyclic = (gps->flag & GP_STROKE_CYCLIC) != 0;
  if (is_cyclic) {
    count--;
  }

  bGPDspoint *new_pt = (bGPDspoint *)MEM_callocN(sizeof(bGPDspoint) * count,
                                                 "gp_stroke_points_sampled");
  MDeformVert *new_dv = nullptr;

  int result_totweight;

  if (gps->dvert != nullptr) {
    stroke_defvert_create_nr_list(gps->dvert, gps->totpoints, &def_nr_list, &result_totweight);
    new_dv = stroke_defvert_new_count(count, result_totweight, &def_nr_list);
  }

  int next_point_index = 1;
  int i = 0;
  float pressure, strength, ratio_result;
  float vert_color[4];
  int index_from, index_to;
  float last_coord[3];

  /*  1st point is always at the start */
  pt1 = &gps->points[0];
  copy_v3_v3(last_coord, &pt1->x);
  pt2 = &new_pt[i];
  copy_v3_v3(&pt2->x, last_coord);
  new_pt[i].pressure = pt[0].pressure;
  new_pt[i].strength = pt[0].strength;
  memcpy(new_pt[i].vert_color, pt[0].vert_color, sizeof(float[4]));
  if (select) {
    new_pt[i].flag |= GP_SPOINT_SELECT;
  }
  i++;

  if (new_dv) {
    stroke_interpolate_deform_weights(gps, 0, 0, 0, &new_dv[0]);
  }

  /* The rest. */
  while ((next_point_index = stroke_march_next_point(gps,
                                                     next_point_index,
                                                     last_coord,
                                                     dist,
                                                     last_coord,
                                                     &pressure,
                                                     &strength,
                                                     vert_color,
                                                     &ratio_result,
                                                     &index_from,
                                                     &index_to)) > -1) {
    if (is_cyclic && next_point_index == 0) {
      break; /* last point finished */
    }
    pt2 = &new_pt[i];
    copy_v3_v3(&pt2->x, last_coord);
    new_pt[i].pressure = pressure;
    new_pt[i].strength = strength;
    memcpy(new_pt[i].vert_color, vert_color, sizeof(float[4]));
    if (select) {
      new_pt[i].flag |= GP_SPOINT_SELECT;
    }

    if (new_dv) {
      stroke_interpolate_deform_weights(gps, index_from, index_to, ratio_result, &new_dv[i]);
    }

    i++;
    if (next_point_index == 0) {
      break; /* last point finished */
    }
  }

  gps->points = new_pt;
  /* Free original vertex list. */
  MEM_freeN(pt);

  if (new_dv) {
    /* Free original weight data. */
    KERNEL_gpencil_free_stroke_weights(gps);
    MEM_freeN(gps->dvert);
    while ((ld = (LinkData *)BLI_pophead(&def_nr_list))) {
      MEM_freeN(ld);
    }

    gps->dvert = new_dv;
  }

  LIB_assert(i == count);
  gps->totpoints = i;

  /* Calc geometry data. */
  KERNEL_gpencil_stroke_geometry_update(gpd, gps);

  return true;
}

/**
 * Give extra stroke points before and after the original tip points.
 * param gps: Target stroke
 * param count_before: how many extra points to be added before a stroke
 * param count_after: how many extra points to be added after a stroke
 */
static bool KERNEL_gpencil_stroke_extra_points(bGPDstroke *gps,
                                            const int count_before,
                                            const int count_after)
{
  bGPDspoint *pts = gps->points;

  LIB_assert(count_before >= 0);
  LIB_assert(count_after >= 0);
  if (!count_before && !count_after) {
    return false;
  }

  const int new_count = count_before + count_after + gps->totpoints;

  bGPDspoint *new_pts = (bGPDspoint *)MEM_mallocN(sizeof(bGPDspoint) * new_count, __func__);

  for (int i = 0; i < count_before; i++) {
    memcpy(&new_pts[i], &pts[0], sizeof(bGPDspoint));
  }
  memcpy(&new_pts[count_before], pts, sizeof(bGPDspoint) * gps->totpoints);
  for (int i = new_count - count_after; i < new_count; i++) {
    memcpy(&new_pts[i], &pts[gps->totpoints - 1], sizeof(bGPDspoint));
  }

  if (gps->dvert) {
    MDeformVert *new_dv = (MDeformVert *)MEM_mallocN(sizeof(MDeformVert) * new_count, __func__);

    for (int i = 0; i < new_count; i++) {
      MDeformVert *dv = &gps->dvert[CLAMPIS(i - count_before, 0, gps->totpoints - 1)];
      int inew = i;
      new_dv[inew].flag = dv->flag;
      new_dv[inew].totweight = dv->totweight;
      new_dv[inew].dw = (MDeformWeight *)MEM_mallocN(sizeof(MDeformWeight) * dv->totweight,
                                                     __func__);
      memcpy(new_dv[inew].dw, dv->dw, sizeof(MDeformWeight) * dv->totweight);
    }
    KERNEL_gpencil_free_stroke_weights(gps);
    MEM_freeN(gps->dvert);
    gps->dvert = new_dv;
  }

  MEM_freeN(gps->points);
  gps->points = new_pts;
  gps->totpoints = new_count;

  return true;
}

bool KERNEL_gpencil_stroke_stretch(bGPDstroke *gps,
                                const float dist,
                                const float overshoot_fac,
                                const short mode,
                                const bool follow_curvature,
                                const int extra_point_count,
                                const float segment_influence,
                                const float max_angle,
                                const bool invert_curvature)
{
#define BOTH 0
#define START 1
#define END 2

  const bool do_start = ELEM(mode, BOTH, START);
  const bool do_end = ELEM(mode, BOTH, END);
  float used_percent_length = overshoot_fac;
  CLAMP(used_percent_length, 1e-4f, 1.0f);
  if (!isfinite(used_percent_length)) {
    /* #used_percent_length must always be finite, otherwise a segfault occurs.
     * Since this function should never segfault, set #used_percent_length to a safe fallback. */
    /* NOTE: This fallback is used if gps->totpoints == 2, see MOD_gpencillength.c */
    used_percent_length = 0.1f;
  }

  if (gps->totpoints <= 1 || dist < FLT_EPSILON || extra_point_count <= 0) {
    return false;
  }

  /* NOTE: When it's just a straight line, we don't need to do the curvature stuff. */
  if (!follow_curvature || gps->totpoints <= 2) {
    /* Not following curvature, just straight line. */
    /* NOTE: #overshoot_point_param can not be zero. */
    float overshoot_point_param = used_percent_length * (gps->totpoints - 1);
    float result[3];

    if (do_start) {
      int index1 = floor(overshoot_point_param);
      int index2 = ceil(overshoot_point_param);
      interp_v3_v3v3(result,
                     &gps->points[index1].x,
                     &gps->points[index2].x,
                     fmodf(overshoot_point_param, 1.0f));
      sub_v3_v3(result, &gps->points[0].x);
      if (UNLIKELY(is_zero_v3(result))) {
        sub_v3_v3v3(result, &gps->points[1].x, &gps->points[0].x);
      }
      madd_v3_v3fl(&gps->points[0].x, result, -dist / len_v3(result));
    }

    if (do_end) {
      int index1 = gps->totpoints - 1 - floor(overshoot_point_param);
      int index2 = gps->totpoints - 1 - ceil(overshoot_point_param);
      interp_v3_v3v3(result,
                     &gps->points[index1].x,
                     &gps->points[index2].x,
                     fmodf(overshoot_point_param, 1.0f));
      sub_v3_v3(result, &gps->points[gps->totpoints - 1].x);
      if (UNLIKELY(is_zero_v3(result))) {
        sub_v3_v3v3(
            result, &gps->points[gps->totpoints - 2].x, &gps->points[gps->totpoints - 1].x);
      }
      madd_v3_v3fl(&gps->points[gps->totpoints - 1].x, result, -dist / len_v3(result));
    }
    return true;
  }

  /* Curvature calculation. */

  /* First allocate the new stroke size. */
  const int first_old_index = do_start ? extra_point_count : 0;
  const int last_old_index = gps->totpoints - 1 + first_old_index;
  const int orig_totpoints = gps->totpoints;
  KERNEL_gpencil_stroke_extra_points(gps, first_old_index, do_end ? extra_point_count : 0);

  /* The fractional amount of points to query when calculating the average curvature of the
   * strokes. */
  const float overshoot_parameter = used_percent_length * (orig_totpoints - 2);
  int overshoot_pointcount = ceil(overshoot_parameter);
  CLAMP(overshoot_pointcount, 1, orig_totpoints - 2);

  /* Do for both sides without code duplication. */
  float no[3], vec1[3], vec2[3], total_angle[3];
  for (int k = 0; k < 2; k++) {
    if ((k == 0 && !do_start) || (k == 1 && !do_end)) {
      continue;
    }

    const int start_i = k == 0 ? first_old_index :
                                 last_old_index;  // first_old_index, last_old_index
    const int dir_i = 1 - k * 2;                  // 1, -1

    sub_v3_v3v3(vec1, &gps->points[start_i + dir_i].x, &gps->points[start_i].x);
    zero_v3(total_angle);
    float segment_length = normalize_v3(vec1);
    float overshoot_length = 0.0f;

    /* Accumulate rotation angle and length. */
    int j = 0;
    for (int i = start_i; j < overshoot_pointcount; i += dir_i, j++) {
      /* Don't fully add last segment to get continuity in overshoot_fac. */
      float fac = fmin(overshoot_parameter - j, 1.0f);

      /* Read segments. */
      copy_v3_v3(vec2, vec1);
      sub_v3_v3v3(vec1, &gps->points[i + dir_i * 2].x, &gps->points[i + dir_i].x);
      const float len = normalize_v3(vec1);
      float angle = angle_normalized_v3v3(vec1, vec2) * fac;

      /* Add half of both adjacent legs of the current angle. */
      const float added_len = (segment_length + len) * 0.5f * fac;
      overshoot_length += added_len;
      segment_length = len;

      if (angle > max_angle) {
        continue;
      }
      if (angle > M_PI * 0.995f) {
        continue;
      }

      angle *= powf(added_len, segment_influence);

      cross_v3_v3v3(no, vec1, vec2);
      normalize_v3_length(no, angle);
      add_v3_v3(total_angle, no);
    }

    if (UNLIKELY(overshoot_length == 0.0f)) {
      /* Don't do a proper extension if the used points are all in the same position. */
      continue;
    }

    sub_v3_v3v3(vec1, &gps->points[start_i].x, &gps->points[start_i + dir_i].x);
    /* In general curvature = 1/radius. For the case without the
     * weights introduced by #segment_influence, the calculation is:
     * `curvature = delta angle/delta arclength = len_v3(total_angle) / overshoot_length` */
    float curvature = normalize_v3(total_angle) / overshoot_length;
    /* Compensate for the weights powf(added_len, segment_influence). */
    curvature /= powf(overshoot_length / fminf(overshoot_parameter, (float)j), segment_influence);
    if (invert_curvature) {
      curvature = -curvature;
    }
    const float angle_step = curvature * dist / extra_point_count;
    float step_length = dist / extra_point_count;
    if (fabsf(angle_step) > FLT_EPSILON) {
      /* Make a direct step length from the assigned arc step length. */
      step_length *= sin(angle_step * 0.5f) / (angle_step * 0.5f);
    }
    else {
      zero_v3(total_angle);
    }
    const float prev_length = normalize_v3_length(vec1, step_length);

    /* Build rotation matrix here to get best performance. */
    float rot[3][3];
    float q[4];
    axis_angle_to_quat(q, total_angle, angle_step);
    quat_to_mat3(rot, q);

    /* Rotate the starting direction to account for change in edge lengths. */
    axis_angle_to_quat(q,
                       total_angle,
                       fmaxf(0.0f, 1.0f - fabs(segment_influence)) *
                           (curvature * prev_length - angle_step) / 2.0f);
    mul_qt_v3(q, vec1);

    /* Now iteratively accumulate the segments with a rotating added direction. */
    for (int i = start_i - dir_i, j = 0; j < extra_point_count; i -= dir_i, j++) {
      mul_v3_m3v3(vec1, rot, vec1);
      add_v3_v3v3(&gps->points[i].x, vec1, &gps->points[i + dir_i].x);
    }
  }
  return true;
}

/* -------------------------------------------------------------------- */
/** Stroke Trim **/
