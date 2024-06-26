#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_math_vector.h"
#include "lib_string_utils.hh"
#include "lib_utildefines.h"

#include "types_anim.h"
#include "types_ob.h"
#include "types_scene.h"
#include "types_space.h"

#include "dune_action.h"
#include "dune_curve.hh"
#include "dune_fcurve.h"
#include "dune_main.hh"
#include "dune_report.h"
#include "dune_scene.h"

#include "api_access.hh"
#include "api_enum_types.hh"
#include "api_path.hh"

#include "ed_keyframes_edit.hh"

#include "anim_animdata.hh"
#include "anim_fcurve.hh"

/* This file contains code for various keyframe-editing tools which are 'destructive'
 * (i.e. they will modify the order of the keyframes, and change the size of the array).
 * While some of these tools may eventually be moved out into blenkernel, for now, it is
 * fine to have these calls here.
 *
 * There are also a few tools here which cannot be easily coded for in the other system (yet).
 * These may also be moved around at some point, but for now, they are best added here.
 *
 * - Joshua Leung, Dec 2008 */

bool dup_fcurve_keys(FCurve *fcu)
{
  bool changed = false;

  /* this can only work when there is an F-Curve, and also when there are some BezTriples */
  if (ELEM(nullptr, fcu, fcu->bezt)) {
    return changed;
  }

  for (int i = 0; i < fcu->totvert; i++) {
    /* If a key is selected */
    if (fcu->bezt[i].f2 & SELECT) {
      /* Expand the list */
      BezTriple *newbezt = static_cast<BezTriple *>(
          mem_calloc(sizeof(BezTriple) * (fcu->totvert + 1), "beztriple"));

      memcpy(newbezt, fcu->bezt, sizeof(BezTriple) * (i + 1));
      memcpy(newbezt + i + 1, fcu->bezt + i, sizeof(BezTriple));
      memcpy(newbezt + i + 2, fcu->bezt + i + 1, sizeof(BezTriple) * (fcu->totvert - (i + 1)));
      fcu->totvert++;
      changed = true;
      /* reassign ptr... (free old, and add new) */
      mem_free(fcu->bezt);
      fcu->bezt = newbezt;

      /* Unsel the current key */
      BEZT_DESEL_ALL(&fcu->bezt[i]);
      i++;

      /* Sel the copied key */
      BEZT_SEL_ALL(&fcu->bezt[i]);
    }
  }
  return changed;
}

/* Various Tools */
void clean_fcurve(AnimCxt *ac,
                  AnimListElem *ale,
                  float thresh,
                  bool cleardefault,
                  const bool only_sel_keys)
{
  FCurve *fcu = (FCurve *)ale->key_data;
  BezTriple *old_bezts, *bezt, *beztn;
  BezTriple *lastb;
  int totCount, i;

  /* Check if any points. */
  if ((fcu == nullptr) || (fcu->bezt == nullptr) || (fcu->totvert == 0) ||
      (!cleardefault && fcu->totvert == 1))
  {
    return;
  }

  /* make a copy of the old BezTriples, and clear F-Curve */
  old_bezts = fcu->bezt;
  totCount = fcu->totvert;
  fcu->bezt = nullptr;
  fcu->totvert = 0;

  /* now insert first keyframe, as it should be ok */
  bezt = old_bezts;
  dune::animrig::insert_bezt_fcurve(fcu, bezt, eInsertKeyFlags(0));
  if (!(bezt->f2 & SEL)) {
    lastb = fcu->bezt;
    lastb->f1 = lastb->f2 = lastb->f3 = 0;
  }

  /* Loop through BezTriples, comparing them. Skip any that do
   * not fit the criteria for "ok" points. */
  for (i = 1; i < totCount; i++) {
    float prev[2], cur[2], next[2];

    /* get BezTriples and their values */
    if (i < (totCount - 1)) {
      beztn = (old_bezts + (i + 1));
      next[0] = beztn->vec[1][0];
      next[1] = beztn->vec[1][1];
    }
    else {
      beztn = nullptr;
      next[0] = next[1] = 0.0f;
    }
    lastb = (fcu->bezt + (fcu->totvert - 1));
    bezt = (old_bezts + i);

    /* get refs for quicker access */
    prev[0] = lastb->vec[1][0];
    prev[1] = lastb->vec[1][1];
    cur[0] = bezt->vec[1][0];
    cur[1] = bezt->vec[1][1];

    if (only_selected_keys && !(bezt->f2 & SEL)) {
      dune::animrig::insert_bezt_fcurve(fcu, bezt, eInsertKeyFlags(0));
      lastb = (fcu->bezt + (fcu->totvert - 1));
      lastb->f1 = lastb->f2 = lastb->f3 = 0;
      continue;
    }

    /* check if current bezt occurs at same time as last ok */
    if (IS_EQT(cur[0], prev[0], thresh)) {
      /* If there is a next beztriple, and if occurs at the same time, only insert
       * if there is a considerable distance between the points, and also if the
       * current is further away than the next one is to the previous.
       */
      if (beztn && IS_EQT(cur[0], next[0], thresh) && (IS_EQT(next[1], prev[1], thresh) == 0)) {
        /* only add if current is further away from previous */
        if (cur[1] > next[1]) {
          if (IS_EQT(cur[1], prev[1], thresh) == 0) {
            /* add new keyframe */
            blender::animrig::insert_bezt_fcurve(fcu, bezt, eInsertKeyFlags(0));
          }
        }
      }
      else {
        /* only add if values are a considerable distance apart */
        if (IS_EQT(cur[1], prev[1], thresh) == 0) {
          /* add new keyframe */
          blender::animrig::insert_bezt_fcurve(fcu, bezt, eInsertKeyFlags(0));
        }
      }
    }
    else {
      /* checks required are dependent on whether this is last keyframe or not */
      if (beztn) {
        /* does current have same value as previous and next? */
        if (IS_EQT(cur[1], prev[1], thresh) == 0) {
          /* add new keyframe */
          blender::animrig::insert_bezt_fcurve(fcu, bezt, eInsertKeyFlags(0));
        }
        else if (IS_EQT(cur[1], next[1], thresh) == 0) {
          /* add new keyframe */
          dune::animrig::insert_bezt_fcurve(fcu, bezt, eInsertKeyFlags(0));
        }
      }
      else {
        /* add if value doesn't equal that of previous */
        if (IS_EQT(cur[1], prev[1], thresh) == 0) {
          /* add new keyframe */
          dune::animrig::insert_bezt_fcurve(fcu, bezt, eInsertKeyFlags(0));
        }
      }
    }
  }

  /* now free the mem used by the old BezTriples */
  if (old_bezts) {
    mem_free(old_bezts);
  }

  /* final step, if there is just one key in fcurve, check if it's
   * the default val and if is, remove fcurve completely. */
  if (cleardefault && fcu->totvert == 1) {
    float default_val = 0.0f;
    ApiPtr ptr;
    ApiProp *prop;
    ApiPtr id_ptr = api_id_ptr_create(ale->id);

    /* get prop to read from, and get val as appropriate */
    if (api_path_resolve_prop(&id_ptr, fcu->api_path, &ptr, &prop)) {
      if (api_prop_type(prop) == PROP_FLOAT) {
        default_val = api_prop_float_get_default_index(&ptr, prop, fcu->arr_index);
      }
    }

    if (fcu->bezt->vec[1][1] == default_val) {
      dune_fcurve_del_keys_all(fcu);

      /* check if curve is unused and if it is, return signal for del */
      if (dune_fcurve_is_empty(fcu)) {
        AnimData *adt = ale->adt;
        dune::animrig::animdata_fcurve_del(ac, adt, fcu);
        ale->key_data = nullptr;
      }
    }
  }
}

/* Find the first segment of consecutive sel curve points, starting from start_index.
 * Keys that have BEZT_FLAG_IGNORE_TAG set are treated as unsel.
 * param r_segment_start_idx: returns the start index of the segment.
 * param r_segment_len: returns the num of curve points in the segment.
 * return whether such a segment was found or not */
static bool find_fcurve_segment(FCurve *fcu,
                                const int start_index,
                                int *r_segment_start_idx,
                                int *r_segment_len)
{
  *r_segment_start_idx = 0;
  *r_segment_len = 0;

  bool in_segment = false;

  for (int i = start_index; i < fcu->totvert; i++) {
    const bool point_is_sel = fcu->bezt[i].f2 & SEL;
    const bool point_is_ignored = fcu->bezt[i].f2 & BEZT_FLAG_IGNORE_TAG;

    if (point_is_sel && !point_is_ignored) {
      if (!in_segment) {
        *r_segment_start_idx = i;
        in_segment = true;
      }
      (*r_segment_len)++;
    }
    else if (in_segment) {
      /* If the curve point is not selected then we have reached the end of the selected curve
       * segment. */
      return true; /* Segment found. */
    }
  }

  /* If the last curve point was in the segment, `r_segment_len` and `r_segment_start_idx`
   * are alrdy updated and true is returned. */
  return in_segment;
}

List find_fcurve_segments(FCurve *fcu)
{
  List segments = {nullptr, nullptr};

  /* Ignore baked curves. */
  if (!fcu->bezt) {
    return segments;
  }

  int segment_start_idx = 0;
  int segment_len = 0;
  int current_index = 0;

  while (find_fcurve_segment(fcu, current_index, &segment_start_idx, &segment_len)) {
    FCurveSegment *segment;
    segment = static_cast<FCurveSegment *>(mem_calloc(sizeof(*segment), "FCurveSegment"));
    segment->start_index = segment_start_idx;
    segment->length = segment_len;
    lib_addtail(&segments, segment);
    current_index = segment_start_idx + segment_len;
  }
  return segments;
}

static const BezTriple *fcurve_segment_start_get(FCurve *fcu, int index)
{
  const BezTriple *start_bezt = index - 1 >= 0 ? &fcu->bezt[index - 1] : &fcu->bezt[index];
  return start_bezt;
}

static const BezTriple *fcurve_segment_end_get(FCurve *fcu, int index)
{
  const BezTriple *end_bezt = index < fcu->totvert ? &fcu->bezt[index] : &fcu->bezt[index - 1];
  return end_bezt;
}

void dune_to_neighbor_fcurve_segment(FCurve *fcu, FCurveSegment *segment, const float factor)
{
  const BezTriple *target_bezt;
  /* Find which key to blend towards. */
  if (factor < 0) {
    target_bezt = fcurve_segment_start_get(fcu, segment->start_index);
  }
  else {
    target_bezt = fcurve_segment_end_get(fcu, segment->start_index + segment->length);
  }
  const float lerp_factor = fabs(factor);
  /* Blend each key individually. */
  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {
    const float key_y_val = interpf(target_bezt->vec[1][1], fcu->bezt[i].vec[1][1], lerp_factor);
    dune_fcurve_keyframe_move_val_w_handles(&fcu->bezt[i], key_y_val);
  }
}

float get_default_api_val(FCurve *fcu, ApiProp *prop, ApiPtr *ptr)
{
  const int len = api_prop_array_length(ptr, prop);

  float default_val = 0;
  /* Find the default val of that prop. */
  switch (api_prop_type(prop)) {
    case PROP_BOOL:
      if (len) {
        default_val = api_prop_bool_get_default_index(ptr, prop, fcu->arr_index);
      }
      else {
        default_val = api_prop_bool_get_default(ptr, prop);
      }
      break;
    case PROP_INT:
      if (len) {
        default_val = api_prop_int_get_default_index(ptr, prop, fcu->array_index);
      }
      else {
        default_val = api_prop_int_get_default(ptr, prop);
      }
      break;
    case PROP_FLOAT:
      if (len) {
        default_val = api_prop_float_get_default_index(ptr, prop, fcu->array_index);
      }
      else {
        default_val = api_prop_float_get_default(ptr, prop);
      }
      break;

    default:
      break;
  }
  return default_val;
}

void blend_to_default_fcurve(ApiPtr *id_ptr, FCurve *fcu, const float factor)
{
  ApiPtr ptr;
  ApiProp *prop;

  /* Check if path is valid. */
  if (!api_path_resolve_prop(id_ptr, fcu->api_path, &ptr, &prop)) {
    return;
  }

  const float default_val = get_default_api_val(fcu, prop, &ptr);

  /* Blend sel keys to default. */
  for (int i = 0; i < fcu->totvert; i++) {
    if (!(fcu->bezt[i].f2 & SEL)) {
      continue;
    }
    const float key_y_val = interpf(default_val, fcu->bezt[i].vec[1][1], factor);
    dune_fcurve_keyframe_move_val_with_handles(&fcu->bezt[i], key_y_value);
  }
}

void scale_avg_fcurve_segment(FCurve *fcu, FCurveSegment *segment, const float factor)
{
  float y = 0;

  /* Find first the avg of the y vals to then use it in the final calc. */
  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {
    y += fcu->bezt[i].vec[1][1];
  }

  const float y_average = y / segment->length;

  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {
    const float key_y_val = interpf(y_avg, fcu->bezt[i].vec[1][1], 1 - factor);
    dune_fcurve_keyframe_move_val_w_handles(&fcu->bezt[i], key_y_val);
  }
}

struct ButterworthCoefficients {
  double *A, *d1, *d2;
  int filter_order;
};

ButterworthCoefficients *ed_anim_alloc_butterworth_coefficients(const int filter_order)
{
  ButterworthCoefficients *bw_coeff = static_cast<ButterworthCoefficients *>(
      mem_calloc(sizeof(ButterworthCoefficients), "Butterworth Coefficients"));
  bw_coeff->filter_order = filter_order;
  bw_coeff->d1 = static_cast<double *>(
      mem_calloc(sizeof(double) * filter_order, "coeff filtered"));
  bw_coeff->d2 = static_cast<double *>(
      mem_calloc(sizeof(double) * filter_order, "coeff samples"));
  bw_coeff->A = static_cast<double *>(MEM_callocN(sizeof(double) * filter_order, "Butterworth A"));
  return bw_coeff;
}

void ed_anim_free_butterworth_coefficients(ButterworthCoefficients *bw_coeff)
{
  mem_free(bw_coeff->d1);
  mem_free(bw_coeff->d2);
  mem_free(bw_coeff->A);
  mem_free(bw_coeff);
}

void ed_anim_cal_butterworth_coefficients(const float cutoff_frequency,
                                          const float sampling_frequency,
                                          ButterworthCoefficients *bw_coeff)
{
  double s = double(sampling_frequency);
  const double a = tan(M_PI * cutoff_frequency / s);
  const double a2 = a * a;
  double r;
  for (int i = 0; i < bw_coeff->filter_order; ++i) {
    r = sin(M_PI * (2.0 * i + 1.0) / (4.0 * bw_coeff->filter_order));
    s = a2 + 2.0 * a * r + 1.0;
    bw_coeff->A[i] = a2 / s;
    bw_coeff->d1[i] = 2.0 * (1 - a2) / s;
    bw_coeff->d2[i] = -(a2 - 2.0 * a * r + 1.0) / s;
  }
}

static double butterworth_filter_val(
    double x, double *w0, double *w1, double *w2, ButterworthCoefficients *bw_coeff)
{
  for (int i = 0; i < bw_coeff->filter_order; i++) {
    w0[i] = bw_coeff->d1[i] * w1[i] + bw_coeff->d2[i] * w2[i] + x;
    x = bw_coeff->A[i] * (w0[i] + 2.0 * w1[i] + w2[i]);
    w2[i] = w1[i];
    w1[i] = w0[i];
  }
  return x;
}

static float butterworth_calc_blend_val(float *samples,
                                              float *filtered_vals,
                                             const int start_index,
                                              const int end_index,
                                              const int sample_index,
                                              const int blend_in_out)
{
  if (start_index == end_index || blend_in_out == 0) {
    return samples[start_index];
  }

  const float blend_in_y_samples = samples[start_index];
  const float blend_out_y_samples = samples[end_index];

  const float blend_in_y_filtered = filtered_vals[start_index + blend_in_out];
  const float blend_out_y_filtered = filtered_vals[end_index - blend_in_out];

  const float slope_in_samples = samples[start_index] - samples[start_index - 1];
  const float slope_out_samples = samples[end_index] - samples[end_index + 1];
  const float slope_in_filtered = filtered_vals[start_index + blend_in_out - 1] -
                                  filtered_vals[start_index + blend_in_out];
  const float slope_out_filtered = filtered_vals[end_index - blend_in_out] -
                                   filtered_vals[end_index - blend_in_out - 1];

  if (sample_index - start_index <= blend_in_out) {
    const int blend_index = sample_index - start_index;
    const float blend_in_out_factor = clamp_f(float(blend_index) / blend_in_out, 0.0f, 1.0f);
    const float blend_value = interpf(blend_in_y_filtered +
                                          slope_in_filtered * (blend_in_out - blend_index),
                                      blend_in_y_samples + slope_in_samples * blend_index,
                                      blend_in_out_factor);
    return blend_val;
  }
  if (end_index - sample_index <= blend_in_out) {
    const int blend_index = end_index - sample_index;
    const float blend_in_out_factor = clamp_f(float(blend_index) / blend_in_out, 0.0f, 1.0f);
    const float blend_value = interpf(blend_out_y_filtered +
                                          slope_out_filtered * (blend_in_out - blend_index),
                                      blend_out_y_samples + slope_out_samples * blend_index,
                                      blend_in_out_factor);
    return blend_val;
  }
  return 0;
}

/* param samples: Are expected to start at the first frame of the segment with a buffer of size
 * `segment->filter_order` at the left. */
void butterworth_smooth_fcurve_segment(FCurve *fcu,
                                       FCurveSegment *segment,
                                       float *samples,
                                       const int sample_count,
                                       const float factor,
                                       const int blend_in_out,
                                       const int sample_rate,
                                       ButterworthCoefficients *bw_coeff)
{
  const int filter_order = bw_coeff->filter_order;

  float *filtered_vals = static_cast<float *>(
      mem_calloc(sizeof(float) * sample_count, "Butterworth Filtered FCurve Vals"));

  double *w0 = static_cast<double *>(mem_calloc(sizeof(double) * filter_order, "w0"));
  double *w1 = static_cast<double *>(mem_calloc(sizeof(double) * filter_order, "w1"));
  double *w2 = static_cast<double *>(mem_calloc(sizeof(double) * filter_order, "w2"));

  /* The vals need to be offset so the first sample starts at 0. This avoids oscillations at the
   * start and end of the curve. */
  const float fwd_offset = samples[0];

  for (int i = 0; i < sample_count; i++) {
    const double x = double(samples[i] - fwd_offset);
    const double filtered_val = butterworth_filter_val(x, w0, w1, w2, bw_coeff);
    filtered_vals[i] = float(filtered_val) + fwd_offset;
  }

  for (int i = 0; i < filter_order; i++) {
    w0[i] = 0.0;
    w1[i] = 0.0;
    w2[i] = 0.0;
  }

  const float bwd_offset = filtered_values[sample_count - 1];

  /* Run the filter backwards as well to remove phase offset. */
  for (int i = sample_count - 1; i >= 0; i--) {
    const double x = double(filtered_vals[i] - bwd_offset);
    const double filtered_val = butterworth_filter_val(x, w0, w1, w2, bw_coeff);
    filtered_vals[i] = float(filtered_val) + bwd_offset;
  }

  const int segment_end_index = segment->start_index + segment->length;
  BezTriple left_bezt = fcu->bezt[segment->start_index];
  BezTriple right_bezt = fcu->bezt[segment_end_index - 1];

  const int samples_start_index = filter_order * sample_rate;
  const int samples_end_index = int(right_bezt.vec[1][0] - left_bezt.vec[1][0] + filter_order) *
                                sample_rate;

  const int blend_in_out_clamped = min_ii(blend_in_out,
                                          (samples_end_index - samples_start_index) / 2);

  for (int i = segment->start_index; i < segment_end_index; i++) {
    float blend_in_out_factor;
    if (blend_in_out_clamped == 0) {
      blend_in_out_factor = 1;
    }
    else if (i < segment->start_index + segment->length / 2) {
      blend_in_out_factor = min_ff(float(i - segment->start_index) / blend_in_out_clamped, 1.0f);
    }
    else {
      blend_in_out_factor = min_ff(float(segment_end_index - i - 1) / blend_in_out_clamped, 1.0f);
    }

    const float x_delta = fcu->bezt[i].vec[1][0] - left_bezt.vec[1][0] + filter_order;
    /* Using round() instead of casting to int. Casting would introduce a stepping issue when the
     * x-val is just below a full frame. */
    const int filter_index = round(x_delta * sample_rate);
    const float blend_val = butterworth_calc_blend_val(samples,
                                                       filtered_vals,
                                                       samples_start_index,
                                                       samples_end_index,
                                                       filter_index,
                                                       blend_in_out_clamped);

    const float blended_val = interpf(
        filtered_vals[filter_index], blend_val, blend_in_out_factor);
    const float key_y_val= interpf(blended_val, samples[filter_index], factor);

    dune_fcurve_keyframe_move_val_with_handles(&fcu->bezt[i], key_y_val);
  }

  mem_free(filtered_vals);
  mem_free(w0);
  mem_free(w1);
  mem_free(w2);
}

void ed_anim_get_1d_gauss_kernel(const float sigma, const int kernel_size, double *r_kernel)
{
  lib_assert(sigma > 0.0f);
  lib_assert(kernel_size > 0);
  const double sigma_sq = 2.0 * sigma * sigma;
  double sum = 0.0;

  for (int i = 0; i < kernel_size; i++) {
    const double normalized_index = double(i) / (kernel_size - 1);
    r_kernel[i] = exp(-normalized_index * normalized_index / sigma_sq);
    if (i == 0) {
      sum += r_kernel[i];
    }
    else {
      /* We only calc half the kernel,
       * the normalization needs to take that into account. */
      sum += r_kernel[i] * 2;
    }
  }

  /* Normalize kernel vals. */
  for (int i = 0; i < kernel_size; i++) {
    r_kernel[i] /= sum;
  }
}

void smooth_fcurve_segment(FCurve *fcu,
                           FCurveSegment *segment,
                           float *samples,
                           const float factor,
                           const int kernel_size,
                           double *kernel)
{
  const int segment_end_index = segment->start_index + segment->length;
  const float segment_start_x = fcu->bezt[segment->start_index].vec[1][0];
  for (int i = segment->start_index; i < segment_end_index; i++) {
    /* Using round() instead of (int). The latter would create stepping on x-values that are just
     * below a full frame. */
    const int sample_index = round(fcu->bezt[i].vec[1][0] - segment_start_x) + kernel_size;
    /* Apply the kernel. */
    double filter_result = samples[sample_index] * kernel[0];
    for (int j = 1; j <= kernel_size; j++) {
      const double kernel_val = kernel[j];
      filter_result += samples[sample_index + j] * kernel_val;
      filter_result += samples[sample_index - j] * kernel_val;
    }
    const float key_y_val = interpf(float(filter_result), samples[sample_index], factor);
    dune_fcurve_keyframe_move_val_w_handles(&fcu->bezt[i], key_y_val);
  }
}

void ease_fcurve_segment(FCurve *fcu, FCurveSegment *segment, const float factor)
{
  const BezTriple *left_key = fcurve_segment_start_get(fcu, segment->start_index);
  const float left_x = left_key->vec[1][0];
  const float left_y = left_key->vec[1][1];

  const BezTriple *right_key = fcurve_segment_end_get(fcu, segment->start_index + segment->length);

  const float key_x_range = right_key->vec[1][0] - left_x;
  const float key_y_range = right_key->vec[1][1] - left_y;

  /* Happens if there is only 1 key on the FCurve. Needs to be skipped because it
   * would be a divide by 0. */
  if (IS_EQF(key_x_range, 0.0f)) {
    return;
  }

  /* In order to have a curve that favors the right key, the curve needs to be mirrored in x and y.
   * Having an exponent that is a fraction of 1 would produce a similar but inferior result. */
  const bool inverted = factor > 0;
  const float exponent = 1 + fabs(factor) * 4;

  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {
    /* For easy calc of the curve, the values are normalized. */
    const float normalized_x = (fcu->bezt[i].vec[1][0] - left_x) / key_x_range;

    float normalized_y = 0;
    if (inverted) {
      normalized_y = 1 - pow(1 - normalized_x, exponent);
    }
    else {
      normalized_y = pow(normalized_x, exponent);
    }

    const float key_y_val = left_y + normalized_y * key_y_range;
    dune_fcurve_keyframe_move_val_w_handles(&fcu->bezt[i], key_y_val);
  }
}

void blend_offset_fcurve_segment(FCurve *fcu, FCurveSegment *segment, const float factor)
{
  const BezTriple *left_key = fcurve_segment_start_get(fcu, segment->start_index);
  const BezTriple *right_key = fcurve_segment_end_get(fcu, segment->start_index + segment->length);

  float y_delta;

  if (factor > 0) {
    const BezTriple segment_last_key = fcu->bezt[segment->start_index + segment->length - 1];
    y_delta = right_key->vec[1][1] - segment_last_key.vec[1][1];
  }
  else {
    const BezTriple segment_first_key = fcu->bezt[segment->start_index];
    y_delta = left_key->vec[1][1] - segment_first_key.vec[1][1];
  }

  const float offset_val = y_delta * fabs(factor);
  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {
    const float key_y_val = fcu->bezt[i].vec[1][1] + offset_val;
    dune_fcurve_keyframe_move_val_w_handles(&fcu->bezt[i], key_y_val);
  }
}

static float s_curve(float x, float slope, float width, float height, float xshift, float yshift)
{
  /* Formula for 'S' curve we use for the "ease" sliders.
   * The shift vals move the curve vertically or horizontally.
   * The range of the curve used is from 0 to 1 on "x" and "y"
   * so we can scale it (width and height) and move it (`xshift` and y `yshift`)
   * to crop the part of the curve we need. Slope determines how curvy the shape is. */
  float y = height * pow((x - xshift), slope) /
                (pow((x - xshift), slope) + pow((width - (x - xshift)), slope)) +
            yshift;

  /* The curve doesn't do what we want beyond our margins so we clamp the values. */
  if (x > xshift + width) {
    y = height + yshift;
  }
  else if (x < xshift) {
    y = yshift;
  }
  return y;
}

void blend_to_ease_fcurve_segment(FCurve *fcu, FCurveSegment *segment, const float factor)
{
  const BezTriple *left_key = fcurve_segment_start_get(fcu, segment->start_index);
  const BezTriple *right_key = fcurve_segment_end_get(fcu, segment->start_index + segment->length);

  const float key_x_range = right_key->vec[1][0] - left_key->vec[1][0];
  const float key_y_range = right_key->vec[1][1] - left_key->vec[1][1];

  /* Happens if there is only 1 key on the FCurve. Needs to be skipped because it
   * would be a divide by 0. */
  if (IS_EQF(key_x_range, 0.0f)) {
    return;
  }

  const float slope = 3.0;
  /* By doubling the size of the "S" curve we just one side of it, a "C" shape. */
  const float width = 2.0;
  const float height = 2.0;
  float xy_shift;

  /* Shifting the x and y vals we can decide what side of the "S" shape to use. */
  if (factor > 0) {
    xy_shift = -1.0;
  }
  else {
    xy_shift = 0.0;
  }

  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {

    const float x = (fcu->bezt[i].vec[1][0] - left_key->vec[1][0]) / key_x_range;
    const float ease = s_curve(x, slope, width, height, xy_shift, xy_shift);
    const float base = left_key->vec[1][1] + key_y_range * ease;

    float y_delta;
    if (factor > 0) {
      y_delta = base - fcu->bezt[i].vec[1][1];
    }
    else {
      y_delta = fcu->bezt[i].vec[1][1] - base;
    }

    const float key_y_val = fcu->bezt[i].vec[1][1] + y_delta * factor;
    dune_fcurve_keyframe_move_val_w_handles(&fcu->bezt[i], key_y_value);
  }
}

bool match_slope_fcurve_segment(FCurve *fcu, FCurveSegment *segment, const float factor)
{
  const BezTriple *left_key = fcurve_segment_start_get(fcu, segment->start_index);
  const BezTriple *right_key = fcurve_segment_end_get(fcu, segment->start_index + segment->length);

  BezTriple beyond_key;
  const BezTriple *ref_key;

  if (factor >= 0) {
    /* Stop the fn if there is no key beyond the right neighboring one. */
    if (segment->start_index + segment->length >= fcu->totvert - 1) {
      return false;
    }
    ref_key = right_key;
    beyond_key = fcu->bezt[segment->start_index + segment->length + 1];
  }
  else {
    /* Stop the fn if there is no key beyond the left neighboring one. */
    if (segment->start_index <= 1) {
      return false;
    }
    ref_key = left_key;
    beyond_key = fcu->bezt[segment->start_index - 2];
  }

  /* This delta vals are used to get the relationship between the bookend keys and the
   * ref keys beyond those. */
  const float y_delta = beyond_key.vec[1][1] - reference_key->vec[1][1];
  const float x_delta = beyond_key.vec[1][0] - reference_key->vec[1][0];

  /* Avoids dividing by 0. */
  if (x_delta == 0) {
    return false;
  }

  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {

    /* These new deltas are used to determine the relationship between the current key and the
     * bookend ones. */
    const float new_x_delta = fcu->bezt[i].vec[1][0] - reference_key->vec[1][0];
    const float new_y_delta = new_x_delta * y_delta / x_delta;

    const float delta = ref_key->vec[1][1] + new_y_delta - fcu->bezt[i].vec[1][1];

    const float key_y_val = fcu->bezt[i].vec[1][1] + delta * fabs(factor);
    dune_fcurve_keyframe_move_val_w_handles(&fcu->bezt[i], key_y_val);
  }
  return true;
}

void shear_fcurve_segment(FCurve *fcu,
                          FCurveSegment *segment,
                          const float factor,
                          tShearDirection direction)
{
  const BezTriple *left_key = fcurve_segment_start_get(fcu, segment->start_index);
  const BezTriple *right_key = fcurve_segment_end_get(fcu, segment->start_index + segment->length);

  const float key_x_range = right_key->vec[1][0] - left_key->vec[1][0];
  const float key_y_range = right_key->vec[1][1] - left_key->vec[1][1];

  /* Happens if there is only 1 key on the FCurve. Needs to be skipped because it
   * would be a divide by 0. */
  if (IS_EQF(key_x_range, 0.0f)) {
    return;
  }

  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {
    /* For easy calculation of the curve, the  values are normalized. */
    float normalized_x;
    if (direction == SHEAR_FROM_LEFT) {
      normalized_x = (fcu->bezt[i].vec[1][0] - left_key->vec[1][0]) / key_x_range;
    }
    else {
      normalized_x = (right_key->vec[1][0] - fcu->bezt[i].vec[1][0]) / key_x_range;
    }

    const float y_delta = key_y_range * normalized_x;

    const float key_y_val = fcu->bezt[i].vec[1][1] + y_delta * factor;
    dune_fcurve_keyframe_move_val_w_handles(&fcu->bezt[i], key_y_value);
  }
}

void push_pull_fcurve_segment(FCurve *fcu, FCurveSegment *segment, const float factor)
{
  const BezTriple *left_key = fcurve_segment_start_get(fcu, segment->start_index);
  const BezTriple *right_key = fcurve_segment_end_get(fcu, segment->start_index + segment->length);

  const float key_x_range = right_key->vec[1][0] - left_key->vec[1][0];
  const float key_y_range = right_key->vec[1][1] - left_key->vec[1][1];

  /* Happens if there is only 1 key on the FCurve. Needs to be skipped because it
   * would be a divide by 0. */
  if (IS_EQF(key_x_range, 0.0f)) {
    return;
  }

  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {
    /* For easy calculation of the curve, the values are normalized. */
    const float normalized_x = (fcu->bezt[i].vec[1][0] - left_key->vec[1][0]) / key_x_range;

    const float linear = left_key->vec[1][1] + key_y_range * normalized_x;

    const float delta = fcu->bezt[i].vec[1][1] - linear;

    const float key_y_value = linear + delta * factor;
    BKE_fcurve_keyframe_move_value_with_handles(&fcu->bezt[i], key_y_value);
  }
}

/* ---------------- */

void time_offset_fcurve_segment(FCurve *fcu, FCurveSegment *segment, const float frame_offset)
{
  /* Two bookend keys of the fcurve are needed to be able to cycle the values. */
  const BezTriple *last_key = &fcu->bezt[fcu->totvert - 1];
  const BezTriple *first_key = &fcu->bezt[0];

  const float fcu_x_range = last_key->vec[1][0] - first_key->vec[1][0];
  const float fcu_y_range = last_key->vec[1][1] - first_key->vec[1][1];

  const float first_key_x = first_key->vec[1][0];

  /* If we op directly on the fcurve there will be a feedback loop
   * so we need to capture the "y" values on an array to then apply them on a second loop. */
  float *y_vals = static_cast<float *>(
      mem_calloc(sizeof(float) * segment->length, "Time Offset Samples"));

  for (int i = 0; i < segment->length; i++) {
    /* This simulates the fcu curve moving in time. */
    const float time = fcu->bezt[segment->start_index + i].vec[1][0] + frame_offset;
    /* Need to normalize time to first_key to specify that as the wrapping point. */
    const float wrapped_time = mod_f_positive(time - first_key_x, fcu_x_range) + first_key_x;
    const float delta_y = fcu_y_range * floorf((time - first_key_x) / fcu_x_range);

    const float key_y_value = evaluate_fcurve(fcu, wrapped_time) + delta_y;
    y_values[i] = key_y_value;
  }

  for (int i = 0; i < segment->length; i++) {
    BKE_fcurve_keyframe_move_value_with_handles(&fcu->bezt[segment->start_index + i], y_values[i]);
  }
  mem_free(y_vals);
}

void breakdown_fcurve_segment(FCurve *fcu, FCurveSegment *segment, const float factor)
{
  const BezTriple *left_bezt = fcurve_segment_start_get(fcu, segment->start_index);
  const BezTriple *right_bezt = fcurve_segment_end_get(fcu,
                                                       segment->start_index + segment->length);

  const float lerp_factor = (factor + 1) / 2;
  for (int i = segment->start_index; i < segment->start_index + segment->length; i++) {
    const float key_y_val = interpf(right_bezt->vec[1][1], left_bezt->vec[1][1], lerp_factor);
    dune_fcurve_keyframe_move_val_w_handles(&fcu->bezt[i], key_y_val);
  }
}

/* FCurve Decimate */
/* Check if the keyframe interpolation type is supported */
static bool prepare_for_decimate(FCurve *fcu, int i)
{
  switch (fcu->bezt[i].ipo) {
    case BEZT_IPO_BEZ:
      /* We do not need to do anything here as the keyframe already has the required setting.
       */
      return true;
    case BEZT_IPO_LIN:
      /* Convert to a linear bezt curve to be able to use the decimation algorithm. */
      fcu->bezt[i].ipo = BEZT_IPO_BEZ;
      fcu->bezt[i].h1 = HD_FREE;
      fcu->bezt[i].h2 = HD_FREE;

      if (i != 0) {
        float h1[3];
        sub_v3_v3v3(h1, fcu->bezt[i - 1].vec[1], fcu->bezt[i].vec[1]);
        mul_v3_fl(h1, 1.0f / 3.0f);
        add_v3_v3(h1, fcu->bezt[i].vec[1]);
        copy_v3_v3(fcu->bezt[i].vec[0], h1);
      }

      if (i + 1 != fcu->totvert) {
        float h2[3];
        sub_v3_v3v3(h2, fcu->bezt[i + 1].vec[1], fcu->bezt[i].vec[1]);
        mul_v3_fl(h2, 1.0f / 3.0f);
        add_v3_v3(h2, fcu->bezt[i].vec[1]);
        copy_v3_v3(fcu->bezt[i].vec[2], h2);
      }
      return true;
    default:
      /* These are unsupported. */
      return false;
  }
}

/* Decimate the given curve segment. */
static void decimate_fcurve_segment(FCurve *fcu,
                                    int bezt_segment_start_idx,
                                    int bezt_segment_len,
                                    float remove_ratio,
                                    float error_sq_max)
{
  int sel_len = bezt_segment_len;

  /* Make sure that we can remove the start/end point of the segment if they
   * are not the start/end point of the curve. dune_curve_decimate_bezt_array
   * has a check that prevents removal of the first and last index in the
   * passed array. */
  if (bezt_segment_len + bezt_segment_start_idx != fcu->totvert &&
      prepare_for_decimate(fcu, bezt_segment_len + bezt_segment_start_idx))
  {
    bezt_segment_len++;
  }
  if (bezt_segment_start_idx != 0 && prepare_for_decimate(fcu, bezt_segment_start_idx - 1)) {
    bezt_segment_start_idx--;
    bezt_segment_len++;
  }

  const int target_fcurve_verts = ceil(bezt_segment_len - sel_len * remove_ratio);

  dune_curve_decimate_bezt_arr(&fcu->bezt[bezt_segment_start_idx],
                                bezt_segment_len,
                                12, /* The actual resolution displayed in the viewport is dynamic
                                     * so we just pick a value that preserves the curve shape. */
                                false,
                                SEL,
                                BEZT_FLAG_TMP_TAG,
                                error_sq_max,
                                target_fcurve_verts);
}

bool decimate_fcurve(AnimListElem *ale, float remove_ratio, float error_sq_max)
{
  FCurve *fcu = (FCurve *)ale->key_data;
  /* Check if the curve actually has any points. */
  if (fcu == nullptr || fcu->bezt == nullptr || fcu->totvert == 0) {
    return true;
  }

  BezTriple *old_bezts = fcu->bezt;

  bool can_decimate_all_selected = true;

  for (int i = 0; i < fcu->totvert; i++) {
    /* Ignore keyframes that are not supported. */
    if (!prepare_for_decimate(fcu, i)) {
      can_decimate_all_sel = false;
      fcu->bezt[i].f2 |= BEZT_FLAG_IGNORE_TAG;
    }
    /* Make sure that the tmp flag is unset as we use it to determine what to remove. */
    fcu->bezt[i].f2 &= ~BEZT_FLAG_TEMP_TAG;
  }

  List segments = find_fcurve_segments(fcu);
  LIST_FOREACH (FCurveSegment *, segment, &segments) {
    decimate_fcurve_segment(
        fcu, segment->start_index, segment->length, remove_ratio, error_sq_max);
  }
  lib_freelist(&segments);

  uint old_totvert = fcu->totvert;
  fcu->bezt = nullptr;
  fcu->totvert = 0;

  for (int i = 0; i < old_totvert; i++) {
    BezTriple *bezt = (old_bezts + i);
    bezt->f2 &= ~BEZT_FLAG_IGNORE_TAG;
    if ((bezt->f2 & BEZT_FLAG_TEMP_TAG) == 0) {
      dune::animrig::insert_bezt_fcurve(fcu, bezt, eInsertKeyFlags(0));
    }
  }
  /* now free the mem used by the old BezTriples */
  if (old_bezts) {
    mem_free(old_bezts);
  }

  return can_decimate_all_sel;
}

/* FCurve Smooth */

/* tmp struct used for smooth_fcurve */
struct tSmooth_Bezt {
  float *h1, *h2, *h3; /* bezt->vec[0,1,2][1] */
  float y1, y2, y3;    /* averaged before/new/after y-values */
};

void smooth_fcurve(FCurve *fcu)
{
  int totSel = 0;

  if (fcu->bezt == nullptr) {
    return;
  }

  /* first loop through - count how many verts are selected */
  BezTriple *bezt = fcu->bezt;
  for (int i = 0; i < fcu->totvert; i++, bezt++) {
    if (BEZT_ISSEL_ANY(bezt)) {
      totSel++;
    }
  }

  /* if any points were selected, allocate tSmooth_Bezt points to work on */
  if (totSel >= 3) {
    tSmooth_Bezt *tarray, *tsb;

    /* alloc mem in one go */
    tsb = tarray = static_cast<tSmooth_Bezt *>(
        mem_calloc(totSel * sizeof(tSmooth_Bezt), "tSmooth_Bezt Array"));

    /* populate tarr with data of sel points */
    bezt = fcu->bezt;
    for (int i = 0, x = 0; (i < fcu->totvert) && (x < totSel); i++, bezt++) {
      if (BEZT_ISSEL_ANY(bezt)) {
        /* tsb simply needs ptr to vec, and index */
        tsb->h1 = &bezt->vec[0][1];
        tsb->h2 = &bezt->vec[1][1];
        tsb->h3 = &bezt->vec[2][1];

        /* advance to the next tsb to populate */
        if (x < totSel - 1) {
          tsb++;
        }
        else {
          break;
        }
      }
    }

    /* calc the new smoothed F-Curve's with weighted avgs:
     * - this is done with two passes to avoid progressive corruption errors
     * - uses 5 points for each op (which stores in the relevant handles)
     * -   prev: w/a ratio = 3:5:2:1:1
     * -   next: w/a ratio = 1:1:2:5:3 */

    /* round 1: calc smoothing deltas and new vale */
    tsb = tarray;
    for (int i = 0; i < totSel; i++, tsb++) {
      /* Don't touch end points (otherwise, curves slowly explode,
       * bc don't have enough data there). */
      if (ELEM(i, 0, (totSel - 1)) == 0) {
        const tSmooth_Bezt *tP1 = tsb - 1;
        const tSmooth_Bezt *tP2 = (i - 2 > 0) ? (tsb - 2) : (nullptr);
        const tSmooth_Bezt *tN1 = tsb + 1;
        const tSmooth_Bezt *tN2 = (i + 2 < totSel) ? (tsb + 2) : (nullptr);

        const float p1 = *tP1->h2;
        const float p2 = (tP2) ? (*tP2->h2) : (*tP1->h2);
        const float c1 = *tsb->h2;
        const float n1 = *tN1->h2;
        const float n2 = (tN2) ? (*tN2->h2) : (*tN1->h2);

        /* calc prev and next, then new position by avging these */
        tsb->y1 = (3 * p2 + 5 * p1 + 2 * c1 + n1 + n2) / 12;
        tsb->y3 = (p2 + p1 + 2 * c1 + 5 * n1 + 3 * n2) / 12;

        tsb->y2 = (tsb->y1 + tsb->y3) / 2;
      }
    }

    /* round 2: apply new vals */
    tsb = tarray;
    for (int i = 0; i < totSel; i++, tsb++) {
      /* don't touch end points, as their vals weren't touched above */
      if (ELEM(i, 0, (totSel - 1)) == 0) {
        /* y2 takes the avg of the 2 points */
        *tsb->h2 = tsb->y2;

        /* handles are weighted between their original vals and the avg valg */
        *tsb->h1 = ((*tsb->h1) * 0.7f) + (tsb->y1 * 0.3f);
        *tsb->h3 = ((*tsb->h3) * 0.7f) + (tsb->y3 * 0.3f);
      }
    }
    
    /* free mem required for tarray */
    mem_free(tarray);
  }

  /* recalc handles */
  dune_fcurve_handles_recalc(fcu);
}

/* FCurve Sample */
/* little cache for vals... */
struct TmpFrameValCache {
  float frame, val;
};

void sample_fcurve_segment(FCurve *fcu,
                           const float start_frame,
                           const int sample_rate,
                           float *samples,
                           const int sample_count)
{
  for (int i = 0; i < sample_count; i++) {
    const float eval_time = start_frame + (float(i) / sample_rate);
    samples[i] = eval_fcurve(fcu, eval_time);
  }
}

void bake_fcurve_segments(FCurve *fcu)
{
  BezTriple *bezt, *start = nullptr, *end = nullptr;
  TmpFrameValCache *val_cache, *fp;
  int sfra, range;
  int i, n;

  if (fcu->bezt == nullptr) { /* ignore baked */
    return;
  }

  /* Find sel keyframes... once pair has been found, add keyframes. */
  for (i = 0, bezt = fcu->bezt; i < fcu->totvert; i++, bezt++) {
    /* check if sel, and which end this is */
    if (BEZT_ISSEL_ANY(bezt)) {
      if (start) {
        /* If next bezt is also sel, don't start sampling yet,
         * but instead wait for that one to reconsider, to avoid
         * changing the curve when sampling consecutive segments
         * (#53229) */
        if (i < fcu->totvert - 1) {
          BezTriple *next = &fcu->bezt[i + 1];
          if (BEZT_ISSEL_ANY(next)) {
            continue;
          }
        }

        /* set end */
        end = bezt;

        /* cache vals then add keyframes using these vals, as adding
         * keyframes while sampling will affect the outcome...
         * - only start sampling+adding from index=1, so that we don't overwrite original keyframe  */
        range = int(ceil(end->vec[1][0] - start->vec[1][0]));
        sfra = int(floor(start->vec[1][0]));

        if (range) {
          val_cache = static_cast<TmpFrameValCache *>(
              mem_calloc(sizeof(TmpFrameValCache) * range, "IcuFrameValCache"));

          /* sample vals */
          for (n = 1, fp = val_cache; n < range && fp; n++, fp++) {
            fp->frame = float(sfra + n);
            fp->val = eval_fcurve(fcu, fp->frame);
          }

          /* add keyframes w these, tagging as 'breakdowns' */
          for (n = 1, fp = val_cache; n < range && fp; n++, fp++) {
            dune::animrig::insert_vert_fcurve(
                fcu, fp->frame, fp->val, BEZT_KEYTYPE_BREAKDOWN, eInsertKeyFlags(1));
          }

          /* free tmp cache */
          mem_free(val_cache);

          /* as we added keyframes, we need to compensate so that bezt is at the right place */
          bezt = fcu->bezt + i + range - 1;
          i += (range - 1);
        }

        /* the current sel island has ended, so start again from scratch */
        start = nullptr;
        end = nullptr;
      }
      else {
        /* just set start keyframe */
        start = bezt;
        end = nullptr;
      }
    }
  }

  /* recalculate channel's handles? */
  BKE_fcurve_handles_recalc(fcu);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Copy/Paste Tools
 *
 * - The copy/paste buffer currently stores a set of temporary F-Curves containing only the
 *   keyframes that were selected in each of the original F-Curves.
 * - All pasted frames are offset by the same amount.
 *   This is calculated as the difference in the times of the current frame and the
 *   `first keyframe` (i.e. the earliest one in all channels).
 * - The earliest frame is calculated per copy operation.
 * \{ */

/* globals for copy/paste data (like for other copy/paste buffers) */
static ListBase animcopybuf = {nullptr, nullptr};
static float animcopy_firstframe = 999999999.0f;
static float animcopy_lastframe = -999999999.0f;
static float animcopy_cfra = 0.0;

/* datatype for use in copy/paste buffer */
struct tAnimCopybufItem {
  tAnimCopybufItem *next, *prev;

  ID *id;            /* ID which owns the curve */
  bActionGroup *grp; /* Action Group */
  char *rna_path;    /* RNA-Path */
  int array_index;   /* array index */

  int totvert;     /* number of keyframes stored for this channel */
  BezTriple *bezt; /* keyframes in buffer */

  short id_type; /* Result of `GS(id->name)`. */
  bool is_bone;  /* special flag for armature bones */
};

void ANIM_fcurves_copybuf_free()
{
  tAnimCopybufItem *aci, *acn;

  /* free each buffer element */
  for (aci = static_cast<tAnimCopybufItem *>(animcopybuf.first); aci; aci = acn) {
    acn = aci->next;

    /* free keyframes */
    if (aci->bezt) {
      MEM_freeN(aci->bezt);
    }

    /* free RNA-path */
    if (aci->rna_path) {
      MEM_freeN(aci->rna_path);
    }

    /* free ourself */
    BLI_freelinkN(&animcopybuf, aci);
  }

  /* restore initial state */
  BLI_listbase_clear(&animcopybuf);
  animcopy_firstframe = 999999999.0f;
  animcopy_lastframe = -999999999.0f;
}

/* ------------------- */

short copy_animedit_keys(bAnimContext *ac, ListBase *anim_data)
{
  Scene *scene = ac->scene;

  /* clear buffer first */
  ANIM_fcurves_copybuf_free();

  /* assume that each of these is an F-Curve */
  LISTBASE_FOREACH (bAnimListElem *, ale, anim_data) {
    FCurve *fcu = (FCurve *)ale->key_data;
    tAnimCopybufItem *aci;
    BezTriple *bezt, *nbezt, *newbuf;
    int i;

    /* firstly, check if F-Curve has any selected keyframes
     * - skip if no selected keyframes found (so no need to create unnecessary copy-buffer data)
     * - this check should also eliminate any problems associated with using sample-data
     */
    if (ANIM_fcurve_keyframes_loop(
            nullptr, fcu, nullptr, ANIM_editkeyframes_ok(BEZT_OK_SELECTED), nullptr) == 0)
    {
      continue;
    }

    /* init copybuf item info */
    aci = static_cast<tAnimCopybufItem *>(
        MEM_callocN(sizeof(tAnimCopybufItem), "AnimCopybufItem"));
    aci->id = ale->id;
    aci->id_type = GS(ale->id->name);
    aci->grp = fcu->grp;
    aci->rna_path = static_cast<char *>(MEM_dupallocN(fcu->rna_path));
    aci->array_index = fcu->array_index;

    /* Detect if this is a bone. We do that here rather than during pasting because ID pointers
     * will get invalidated if we undo.
     * Storing the relevant information here helps avoiding crashes if we undo-repaste. */
    if ((aci->id_type == ID_OB) && (((Object *)aci->id)->type == OB_ARMATURE) && aci->rna_path) {
      Object *ob = (Object *)aci->id;

      bPoseChannel *pchan;
      char bone_name[sizeof(pchan->name)];
      if (BLI_str_quoted_substr(aci->rna_path, "pose.bones[", bone_name, sizeof(bone_name))) {
        pchan = BKE_pose_channel_find_name(ob->pose, bone_name);
        if (pchan) {
          aci->is_bone = true;
        }
      }
    }

    BLI_addtail(&animcopybuf, aci);

    /* add selected keyframes to buffer */
    /* TODO: currently, we resize array every time we add a new vert -
     * this works ok as long as it is assumed only a few keys are copied */
    for (i = 0, bezt = fcu->bezt; i < fcu->totvert; i++, bezt++) {
      if (BEZT_ISSEL_ANY(bezt)) {
        /* add to buffer */
        newbuf = static_cast<BezTriple *>(
            MEM_callocN(sizeof(BezTriple) * (aci->totvert + 1), "copybuf beztriple"));

        /* assume that since we are just re-sizing the array, just copy all existing data across */
        if (aci->bezt) {
          memcpy(newbuf, aci->bezt, sizeof(BezTriple) * (aci->totvert));
        }

        /* copy current beztriple across too */
        nbezt = &newbuf[aci->totvert];
        *nbezt = *bezt;

        /* ensure copy buffer is selected so pasted keys are selected */
        BEZT_SEL_ALL(nbezt);

        /* free old array and set the new */
        if (aci->bezt) {
          MEM_freeN(aci->bezt);
        }
        aci->bezt = newbuf;
        aci->totvert++;

        /* check if this is the earliest frame encountered so far */
        if (bezt->vec[1][0] < animcopy_firstframe) {
          animcopy_firstframe = bezt->vec[1][0];
        }
        if (bezt->vec[1][0] > animcopy_lastframe) {
          animcopy_lastframe = bezt->vec[1][0];
        }
      }
    }
  }

  /* check if anything ended up in the buffer */
  if (ELEM(nullptr, animcopybuf.first, animcopybuf.last)) {
    return -1;
  }

  /* in case 'relative' paste method is used */
  animcopy_cfra = scene->r.cfra;

  /* everything went fine */
  return 0;
}

static void flip_names(tAnimCopybufItem *aci, char **r_name)
{
  if (!aci->is_bone) {
    return;
  }
  int ofs_start, ofs_end;
  if (!BLI_str_quoted_substr_range(aci->rna_path, "pose.bones[", &ofs_start, &ofs_end)) {
    return;
  }

  char *str_start = aci->rna_path + ofs_start;
  const char *str_end = aci->rna_path + ofs_end;

  /* Swap out the name.
   * NOTE: there is no need to un-escape the string to flip it.
   * However the buffer does need to be twice the size. */
  char bname_new[MAX_VGROUP_NAME * 2];
  char *str_iter;
  int len_old, prefix_l, postfix_l;

  prefix_l = str_start - aci->rna_path;

  len_old = str_end - str_start;
  postfix_l = strlen(str_end);

  /* Temporary substitute with nullptr terminator. */
  BLI_assert(str_start[len_old] == '\"');
  str_start[len_old] = 0;
  const int len_new = BLI_string_flip_side_name(bname_new, str_start, false, sizeof(bname_new));
  str_start[len_old] = '\"';

  str_iter = *r_name = static_cast<char *>(
      MEM_mallocN(sizeof(char) * (prefix_l + postfix_l + len_new + 1), "flipped_path"));

  memcpy(str_iter, aci->rna_path, prefix_l);
  str_iter += prefix_l;
  memcpy(str_iter, bname_new, len_new);
  str_iter += len_new;
  memcpy(str_iter, str_end, postfix_l);
  str_iter[postfix_l] = '\0';
}

/* ------------------- */

/* most strict method: exact matches only */
static tAnimCopybufItem *pastebuf_match_path_full(FCurve *fcu,
                                                  const short from_single,
                                                  const short to_simple,
                                                  bool flip)
{
  tAnimCopybufItem *aci;

  for (aci = static_cast<tAnimCopybufItem *>(animcopybuf.first); aci; aci = aci->next) {
    if (to_simple || (aci->rna_path && fcu->rna_path)) {
      if (!to_simple && flip && aci->is_bone && fcu->rna_path) {
        if ((from_single) || (aci->array_index == fcu->array_index)) {
          char *name = nullptr;
          flip_names(aci, &name);
          if (STREQ(name, fcu->rna_path)) {
            MEM_freeN(name);
            break;
          }
          MEM_freeN(name);
        }
      }
      else if (to_simple || STREQ(aci->rna_path, fcu->rna_path)) {
        if ((from_single) || (aci->array_index == fcu->array_index)) {
          break;
        }
      }
    }
  }

  return aci;
}

/* medium match strictness: path match only (i.e. ignore ID) */
static tAnimCopybufItem *pastebuf_match_path_property(Main *bmain,
                                                      FCurve *fcu,
                                                      const short from_single,
                                                      const short /*to_simple*/)
{
  tAnimCopybufItem *aci;

  for (aci = static_cast<tAnimCopybufItem *>(animcopybuf.first); aci; aci = aci->next) {
    /* check that paths exist */
    if (aci->rna_path && fcu->rna_path) {
      /* find the property of the fcurve and compare against the end of the tAnimCopybufItem
       * more involved since it needs to do path lookups.
       * This is not 100% reliable since the user could be editing the curves on a path that won't
       * resolve, or a bone could be renamed after copying for eg. but in normal copy & paste
       * this should work out ok.
       */
      if (BLI_findindex(which_libbase(bmain, aci->id_type), aci->id) == -1) {
        /* pedantic but the ID could have been removed, and beats crashing! */
        printf("paste_animedit_keys: error ID has been removed!\n");
      }
      else {
        ApiPtr rptr;
        ApiProp *prop;

        ApiPtr id_ptr = api_id_ptr_create(aci->id);

        if (api_path_resolve_prop(&id_ptr, aci->api_path, &rptr, &prop)) {
          const char *id = api_prop_id(prop);
          int len_id = strlen(id);
          int len_path = strlen(fcu->api_path);
          if (len_id <= len_path) {
            /* Paths ending w "] will fail w this test - Anim Id Props. */
            if (STREQ(id, fcu->api_path + (len_path - len_id))) {
              if ((from_single) || (aci->arr_index == fcu->arr_index)) {
                break;
              }
            }
          }
        }
        else {
          printf("paste_animedit_keys: failed to resolve path id:%s, '%s'!\n",
                 aci->id->name,
                 aci->api_path);
        }
      }
    }
  }

  return aci;
}

/* least strict matching heuristic: indices only */
static tAnimCopybufItem *pastebuf_match_index_only(FCurve *fcu,
                                                   const short from_single,
                                                   const short /*to_simple*/)
{
  tAnimCopybufItem *aci;

  for (aci = static_cast<tAnimCopybufItem *>(animcopybuf.first); aci; aci = aci->next) {
    /* check that paths exist */
    if ((from_single) || (aci->arr_index == fcu->arr_index)) {
      break;
    }
  }

  return aci;
}

static void do_curve_mirror_flippping(tAnimCopybufItem *aci, BezTriple *bezt)
{
  if (aci->is_bone) {
    const size_t slength = strlen(aci->api_path);
    bool flip = false;
    if (lib_strn_endswith(aci->api_path, "location", slength) && aci->arr_index == 0) {
      flip = true;
    }
    else if (lib_strn_endswith(aci->api_path, "rotation_quaternion", slength) &&
             ELEM(aci->arr_index, 2, 3))
    {
      flip = true;
    }
    else if (lib_strn_endswith(aci->rna_path, "rotation_euler", slength) &&
             ELEM(aci->arr_index, 1, 2))
    {
      flip = true;
    }
    else if (lib_strn_endswith(aci->api_path, "rotation_axis_angle", slength) &&
             ELEM(aci->arr_index, 2, 3))
    {
      flip = true;
    }

    if (flip) {
      bezt->vec[0][1] = -bezt->vec[0][1];
      bezt->vec[1][1] = -bezt->vec[1][1];
      bezt->vec[2][1] = -bezt->vec[2][1];
    }
  }
}

/* helper for paste_animedit_keys() - performs the actual pasting */
static void paste_animedit_keys_fcurve(
    FCurve *fcu, tAnimCopybufItem *aci, float offset[2], const eKeyMergeMode merge_mode, bool flip)
{
  BezTriple *bezt;
  int i;

  /* First de-sel existing FCurve's keyframes */
  for (i = 0, bezt = fcu->bezt; i < fcu->totvert; i++, bezt++) {
    BEZT_DESEL_ALL(bezt);
  }

  /* mix mode with existing data */
  switch (merge_mode) {
    case KEYFRAME_PASTE_MERGE_MIX:
      /* do-nothing */
      break;

    case KEYFRAME_PASTE_MERGE_OVER:
      /* remove all keys */
      dune_fcurve_del_keys_all(fcu);
      break;

    case KEYFRAME_PASTE_MERGE_OVER_RANGE:
    case KEYFRAME_PASTE_MERGE_OVER_RANGE_ALL: {
      float f_min;
      float f_max;

      if (merge_mode == KEYFRAME_PASTE_MERGE_OVER_RANGE) {
        f_min = aci->bezt[0].vec[1][0] + offset[0];
        f_max = aci->bezt[aci->totvert - 1].vec[1][0] + offset[0];
      }
      else { /* Entire Range */
        f_min = animcopy_firstframe + offset[0];
        f_max = animcopy_lastframe + offset[0];
      }

      /* remove keys in range */
      if (f_min < f_max) {
        /* sel verts in range for removal */
        for (i = 0, bezt = fcu->bezt; i < fcu->totvert; i++, bezt++) {
          if ((f_min < bezt[0].vec[1][0]) && (bezt[0].vec[1][0] < f_max)) {
            bezt->f2 |= SEL;
          }
        }

        /* remove frames in the range */
        dune_fcurve_del_keys_sel(fcu);
      }
      break;
    }
  }

  /* start pasting: 1st keyframe on the current frame and so on */
  for (i = 0, bezt = aci->bezt; i < aci->totvert; i++, bezt++) {
    /* tmp apply offset to src beztriple while copying */
    if (flip) {
      do_curve_mirror_flippping(aci, bezt);
    }

    add_v2_v2(bezt->vec[0], offset);
    add_v2_v2(bezt->vec[1], offset);
    add_v2_v2(bezt->vec[2], offset);

    /* insert the keyframe
     * NOTE: we do not want to inherit handles from existing keyframes in this case! */

    dune::animrig::insert_bezt_fcurve(fcu, bezt, INSERTKEY_OVERWRITE_FULL);

    /* un-apply offset from src beztriple after copying */
    sub_v2_v2(bezt->vec[0], offset);
    sub_v2_v2(bezt->vec[1], offset);
    sub_v2_v2(bezt->vec[2], offset);

    if (flip) {
      do_curve_mirror_flippping(aci, bezt);
    }
  }

  /* recalc F-Curve's handles? */
  dune_fcurve_handles_recalc(fcu);
}

const EnumPropItem api_enum_keyframe_paste_offset_items[] = {
    {KEYFRAME_PASTE_OFFSET_CFRA_START,
     "START",
     0,
     "Frame Start",
     "Paste keys starting at current frame"},
    {KEYFRAME_PASTE_OFFSET_CFRA_END, "END", 0, "Frame End", "Paste keys ending at current frame"},
    {KEYFRAME_PASTE_OFFSET_CFRA_RELATIVE,
     "RELATIVE",
     0,
     "Frame Relative",
     "Paste keys relative to the current frame when copying"},
    {KEYFRAME_PASTE_OFFSET_NONE, "NONE", 0, "No Offset", "Paste keys from original time"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropItem api_enum_keyframe_paste_offset_val_items[] = {
    {KEYFRAME_PASTE_VAL_OFFSET_LEFT_KEY,
     "LEFT_KEY",
     0,
     "Left Key",
     "Paste keys w the first key matching the key left of the cursor"},
    {KEYFRAME_PASTE_VAL_OFFSET_RIGHT_KEY,
     "RIGHT_KEY",
     0,
     "Right Key",
     "Paste keys w the last key matching the key right of the cursor"},
    {KEYFRAME_PASTE_VAL_OFFSET_CFRA,
     "CURRENT_FRAME",
     0,
     "Current Frame Val",
     "Paste keys relative to the val of the curve under the cursor"},
    {KEYFRAME_PASTE_VALUE_OFFSET_CURSOR,
     "CURSOR_VAL",
     0,
     "Cursor Val",
     "Paste keys relative to the Y-Position of the cursor"},
    {KEYFRAME_PASTE_VAL_OFFSET_NONE,
     "NONE",
     0,
     "No Offset",
     "Paste keys with the same value as they were copied"},
    {0, nullptr, 0, nullptr, nullptr},
};

const EnumPropItem api_enum_keyframe_paste_merge_items[] = {
    {KEYFRAME_PASTE_MERGE_MIX, "MIX", 0, "Mix", "Overlay existing with new keys"},
    {KEYFRAME_PASTE_MERGE_OVER, "OVER_ALL", 0, "Overwrite All", "Replace all keys"},
    {KEYFRAME_PASTE_MERGE_OVER_RANGE,
     "OVER_RANGE",
     0,
     "Overwrite Range",
     "Overwrite keys in pasted range"},
    {KEYFRAME_PASTE_MERGE_OVER_RANGE_ALL,
     "OVER_RANGE_ALL",
     0,
     "Overwrite Entire Range",
     "Overwrite keys in pasted range, using the range of all copied keys"},
    {0, nullptr, 0, nullptr, nullptr},
};

static float paste_get_y_offset(AnimCxt *ac,
                                tAnimCopybufItem *aci,
                                AnimListElem *ale,
                                const eKeyPasteValOffset val_offset_mode)
{
  FCurve *fcu = (FCurve *)ale->data;
  const float cfra = dune_scene_frame_get(ac->scene);

  switch (value_offset_mode) {
    case KEYFRAME_PASTE_VAL_OFFSET_CURSOR: {
      SpaceGraph *sipo = (SpaceGraph *)ac->sl;
      const float offset = sipo->cursorVal - aci->bezt[0].vec[1][1];
      return offset;
    }

    case KEYFRAME_PASTE_VAL_OFFSET_CFRA: {
      const float cfra_y = eval_fcurve(fcu, cfra);
      const float offset = cfra_y - aci->bezt[0].vec[1][1];
      return offset;
    }

    case KEYFRAME_PASTE_VAL_OFFSET_LEFT_KEY: {
      bool replace;
      const int fcu_index = dune_fcurve_bezt_binarysearch_index(
          fcu->bezt, cfra, fcu->totvert, &replace);
      BezTriple left_key = fcu->bezt[max_ii(fcu_index - 1, 0)];
      const float offset = left_key.vec[1][1] - aci->bezt[0].vec[1][1];
      return offset;
    }

    case KEYFRAME_PASTE_VAL_OFFSET_RIGHT_KEY: {
      bool replace;
      const int fcu_index = dune_fcurve_bezt_binarysearch_index(
          fcu->bezt, cfra, fcu->totvert, &replace);
      BezTriple right_key = fcu->bezt[min_ii(fcu_index, fcu->totvert - 1)];
      const float offset = right_key.vec[1][1] - aci->bezt[aci->totvert - 1].vec[1][1];
      return offset;
    }

    case KEYFRAME_PASTE_VAL_OFFSET_NONE:
      break;
  }

  return 0.0f;
}

eKeyPasteError paste_animedit_keys(AnimCxt *ac,
                                   List *anim_data,
                                   const eKeyPasteOffset offset_mode,
                                   const eKeyPasteValOffset val_offset_mode,
                                   const eKeyMergeMode merge_mode,
                                   bool flip)
{
  AnimListElem *ale;

  const Scene *scene = (ac->scene);

  const bool from_single = lib_list_is_single(&animcopybuf);
  const bool to_simple = lib_list_is_single(anim_data);

  float offset[2];
  int pass;

  /* check if buf is empty */
  if (lib_list_is_empty(&animcopybuf)) {
    return KEYFRAME_PASTE_NOTHING_TO_PASTE;
  }

  if (lib_list_is_empty(anim_data)) {
    return KEYFRAME_PASTE_NOWHERE_TO_PASTE;
  }

  /* methods of offset */
  switch (offset_mode) {
    case KEYFRAME_PASTE_OFFSET_CFRA_START:
      offset[0] = float(scene->r.cfra - animcopy_firstframe);
      break;
    case KEYFRAME_PASTE_OFFSET_CFRA_END:
      offset[0] = float(scene->r.cfra - animcopy_lastframe);
      break;
    case KEYFRAME_PASTE_OFFSET_CFRA_RELATIVE:
      offset[0] = float(scene->r.cfra - animcopy_cfra);
      break;
    case KEYFRAME_PASTE_OFFSET_NONE:
      offset[0] = 0.0f;
      break;
  }

  if (from_single && to_simple) {
    /* 1:1 match, no tricky checking, just paste */
    FCurve *fcu;
    tAnimCopybufItem *aci;

    ale = static_cast<AnimListElem *>(anim_data->first);
    fcu = (FCurve *)ale->data; /* destination F-Curve */
    aci = static_cast<tAnimCopybufItem *>(animcopybuf.first);

    offset[1] = paste_get_y_offset(ac, aci, ale, val_offset_mode);
    paste_animedit_keys_fcurve(fcu, aci, offset, merge_mode, false);
    ale->update |= ANIM_UPDATE_DEFAULT;
  }
  else {
    /* from sel channels
     * This "passes" system aims to try to find "matching" channels to paste keyframes
     * into with increasingly loose matching heuristics. The process finishes when at least
     * one F-Curve has been pasted into. */
    for (pass = 0; pass < 3; pass++) {
      uint totmatch = 0;

      LIST_FOREACH (AnimListElem *, ale, anim_data) {
        /* Find buf item to paste from:
         * - If names don't matter (i.e. only 1 channel in buffer), don't check id/group
         * - If names do matter, only check if id-type is ok for now
         *   (group check is not that important).
         * - Most importantly, rna-paths should match (array indices are unimportant for now) */
        AnimData *adt = anim_nla_mapping_get(ac, ale);
        FCurve *fcu = (FCurve *)ale->data; /* destination F-Curve */
        tAnimCopybufItem *aci = nullptr;

        switch (pass) {
          case 0:
            /* most strict, must be exact path match data_path & index */
            aci = pastebuf_match_path_full(fcu, from_single, to_simple, flip);
            break;

          case 1:
            /* less strict, just compare prop names */
            aci = pastebuf_match_path_prop(ac->main, fcu, from_single, to_simple);
            break;

          case 2:
            /* Comparing props gave no results, so just do index comparisons */
            aci = pastebuf_match_index_only(fcu, from_single, to_simple);
            break;
        }

        /* copy the relevant data from the matching buffer curve */
        if (aci) {
          totmatch++;

          offset[1] = paste_get_y_offset(ac, aci, ale, value_offset_mode);
          if (adt) {
            anim_nla_mapping_apply_fcurve(adt, static_cast<FCurve *>(ale->key_data), false, false);
            paste_animedit_keys_fcurve(fcu, aci, offset, merge_mode, flip);
            anim_nla_mapping_apply_fcurve(adt, static_cast<FCurve *>(ale->key_data), true, false);
          }
          else {
            paste_animedit_keys_fcurve(fcu, aci, offset, merge_mode, flip);
          }
        }

        ale->update |= ANIM_UPDATE_DEFAULT;
      }

      /* don't continue if some fcurves were pasted */
      if (totmatch) {
        break;
      }
    }
  }

  animdata_update(ac, anim_data);

  return KEYFRAME_PASTE_OK;
}
