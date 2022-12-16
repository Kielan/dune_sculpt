/**
 * Functions for evaluating the mask beziers into points for the outline and feather.
 */

#include <stddef.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "LIB_math.h"
#include "LIB_utildefines.h"

#include "STRUCTS_mask_types.h"
#include "STRUCTS_object_types.h"

#include "KERNEL_curve.h"
#include "KERNEL_mask.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

unsigned int KERNEL_mask_spline_resolution(MaskSpline *spline, int width, int height)
{
  float max_segment = 0.01f;
  unsigned int i, resol = 1;

  if (width != 0 && height != 0) {
    max_segment = 1.0f / (float)max_ii(width, height);
  }

  for (i = 0; i < spline->tot_point; i++) {
    MaskSplinePoint *point = &spline->points[i];
    BezTriple *bezt_curr, *bezt_next;
    float a, b, c, len;
    unsigned int cur_resol;

    bezt_curr = &point->bezt;
    bezt_next = BKE_mask_spline_point_next_bezt(spline, spline->points, point);

    if (bezt_next == NULL) {
      break;
    }

    a = len_v3v3(bezt_curr->vec[1], bezt_curr->vec[2]);
    b = len_v3v3(bezt_curr->vec[2], bezt_next->vec[0]);
    c = len_v3v3(bezt_next->vec[0], bezt_next->vec[1]);

    len = a + b + c;
    cur_resol = len / max_segment;

    resol = MAX2(resol, cur_resol);

    if (resol >= MASK_RESOL_MAX) {
      break;
    }
  }

  return CLAMPIS(resol, 1, MASK_RESOL_MAX);
}

unsigned int KERNEL_mask_spline_feather_resolution(MaskSpline *spline, int width, int height)
{
  const float max_segment = 0.005;
  unsigned int resol = BKE_mask_spline_resolution(spline, width, height);
  float max_jump = 0.0f;

  /* avoid checking the featrher if we already hit the maximum value */
  if (resol >= MASK_RESOL_MAX) {
    return MASK_RESOL_MAX;
  }

  for (int i = 0; i < spline->tot_point; i++) {
    MaskSplinePoint *point = &spline->points[i];

    float prev_u = 0.0f;
    float prev_w = point->bezt.weight;

    for (int j = 0; j < point->tot_uw; j++) {
      const float w_diff = (point->uw[j].w - prev_w);
      const float u_diff = (point->uw[j].u - prev_u);

      /* avoid divide by zero and very high values,
       * though these get clamped eventually */
      if (u_diff > FLT_EPSILON) {
        float jump = fabsf(w_diff / u_diff);

        max_jump = max_ff(max_jump, jump);
      }

      prev_u = point->uw[j].u;
      prev_w = point->uw[j].w;
    }
  }

  resol += max_jump / max_segment;

  return CLAMPIS(resol, 1, MASK_RESOL_MAX);
}

int KERNEL_mask_spline_differentiate_calc_total(const MaskSpline *spline, const unsigned int resol)
{
  if (spline->flag & MASK_SPLINE_CYCLIC) {
    return spline->tot_point * resol;
  }

  return ((spline->tot_point - 1) * resol) + 1;
}

float (*KERNEL_mask_spline_differentiate_with_resolution(MaskSpline *spline,
                                                      const unsigned int resol,
                                                      unsigned int *r_tot_diff_point))[2]
{
  MaskSplinePoint *points_array = KERNEL_mask_spline_point_array(spline);

  MaskSplinePoint *point_curr, *point_prev;
  float(*diff_points)[2], (*fp)[2];
  const int tot = KERNEL_mask_spline_differentiate_calc_total(spline, resol);
  int a;

  if (spline->tot_point <= 1) {
    /* nothing to differentiate */
    *r_tot_diff_point = 0;
    return NULL;
  }

  /* len+1 because of 'forward_diff_bezier' function */
  *r_tot_diff_point = tot;
  diff_points = fp = MEM_mallocN((tot + 1) * sizeof(*diff_points), "mask spline vets");

  a = spline->tot_point - 1;
  if (spline->flag & MASK_SPLINE_CYCLIC) {
    a++;
  }

  point_prev = points_array;
  point_curr = point_prev + 1;

  while (a--) {
    BezTriple *bezt_prev;
    BezTriple *bezt_curr;
    int j;

    if (a == 0 && (spline->flag & MASK_SPLINE_CYCLIC)) {
      point_curr = points_array;
    }

    bezt_prev = &point_prev->bezt;
    bezt_curr = &point_curr->bezt;

    for (j = 0; j < 2; j++) {
      KERNEL_curve_forward_diff_bezier(bezt_prev->vec[1][j],
                                    bezt_prev->vec[2][j],
                                    bezt_curr->vec[0][j],
                                    bezt_curr->vec[1][j],
                                    &(*fp)[j],
                                    resol,
                                    sizeof(float[2]));
    }

    fp += resol;

    if (a == 0 && (spline->flag & MASK_SPLINE_CYCLIC) == 0) {
      copy_v2_v2(*fp, bezt_curr->vec[1]);
    }

    point_prev = point_curr;
    point_curr++;
  }

  return diff_points;
}
