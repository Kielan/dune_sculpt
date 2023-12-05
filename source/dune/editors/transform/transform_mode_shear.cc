#include <cstdlib>

#include "types_pen_legacy.h"

#include "lib_math_matrix.h"
#include "lib_math_vector.h"
#include "lib_string.h"
#include "lib_task.h"

#include "dune_cxt.hh"
#include "dune_unit.hh"

#include "ed_screen.hh"

#include "win_types.hh"

#include "ui.hh"

#include "lang.h"

#include "transform.hh"
#include "transform_convert.hh"
#include "transform_snap.hh"

#include "transform_mode.hh"

/* Transform (Shear) Element */
/* Small arrays/data-structs should be stored copied for faster mem access. */
struct TransDataArgsShear {
  const TransInfo *t;
  const TransDataContainer *tc;
  float mat_final[3][3];
  bool is_local_center;
};

static void transdata_elem_shear(const TransInfo *t,
                                 const TransDataContainer *tc,
                                 TransData *td,
                                 const float mat_final[3][3],
                                 const bool is_local_center)
{
  float tmat[3][3];
  const float *center;
  if (t->flag & T_EDIT) {
    mul_m3_series(tmat, td->smtx, mat_final, td->mtx);
  }
  else {
    copy_m3_m3(tmat, mat_final);
  }

  if (is_local_center) {
    center = td->center;
  }
  else {
    center = tc->center_local;
  }

  float vec[3];
  sub_v3_v3v3(vec, td->iloc, center);
  mul_m3_v3(tmat, vec);
  add_v3_v3(vec, center);
  sub_v3_v3(vec, td->iloc);

  if (t->options & CXT_PEN_STROKES) {
    /* pen multi-frame falloff. */
    PenStroke *ps = (PenStroke *)td->extra;
    if (ps != nullptr) {
      mul_v3_fl(vec, td->factor * ps->runtime.multi_frame_falloff);
    }
    else {
      mul_v3_fl(vec, td->factor);
    }
  }
  else {
    mul_v3_fl(vec, td->factor);
  }

  add_v3_v3v3(td->loc, td->iloc, vec);
}

static void transdata_elem_shear_fn(void *__restrict iter_data_v,
                                    const int iter,
                                    const TaskParallelTLS *__restrict /*tls*/)
{
  TransDataArgsShear *data = static_cast<TransDataArgsShear *>(iter_data_v);
  TransData *td = &data->tc->data[iter];
  if (td->flag & TD_SKIP) {
    return;
  }
  transdata_elem_shear(data->t, data->tc, td, data->mat_final, data->is_local_center);
}

/* Transform (Shear) */
static void initShear_mouseInputMode(TransInfo *t)
{
  float dir[3];
  bool dir_flip = false;
  copy_v3_v3(dir, t->spacemtx[t->orient_axis_ortho]);

  /* Needed for axis aligned view gizmo. */
  if (t->orient[t->orient_curr].type == V3D_ORIENT_VIEW) {
    if (t->orient_axis_ortho == 0) {
      if (t->center2d[1] > t->mouse.imval[1]) {
        dir_flip = !dir_flip;
      }
    }
    else if (t->orient_axis_ortho == 1) {
      if (t->center2d[0] > t->mouse.imval[0]) {
        dir_flip = !dir_flip;
      }
    }
  }

  /* Wo this, half the gizmo handles move in the opposite direction. */
  if ((t->orient_axis_ortho + 1) % 3 != t->orient_axis) {
    dir_flip = !dir_flip;
  }

  if (dir_flip) {
    negate_v3(dir);
  }

  mul_mat3_m4_v3(t->viewmat, dir);
  if (normalize_v2(dir) == 0.0f) {
    dir[0] = 1.0f;
  }
  setCustomPointsFromDirection(t, &t->mouse, dir);

  initMouseInputMode(t, &t->mouse, INPUT_CUSTOM_RATIO);
}

static eRedrwFlag handleEvShear(TransInfo *t, const WinEv *ev)
{
  eRedrwFlag status = TREDRW_NOTHING;

  if (ev->type == MIDDLEMOUSE && ev->val == KM_PRESS) {
    /* Use custom.mode.data ptr to signal Shear direction */
    do {
      t->orient_axis_ortho = (t->orient_axis_ortho + 1) % 3;
    } while (t->orient_axis_ortho == t->orient_axis);

    initShear_mouseInputMode(t);

    status = TREDRW_HARD;
  }
  else if (ev->type == EV_XKEY && ev->val == KM_PRESS) {
    t->orient_axis_ortho = (t->orient_axis + 1) % 3;
    initShear_mouseInputMode(t);

    status = TREDRW_HARD;
  }
  else if (ev->type == EV_YKEY && ev->val == KM_PRESS) {
    t->orient_axis_ortho = (t->orient_axis + 2) % 3;
    initShear_mouseInputMode(t);

    status = TREDRW_HARD;
  }

  return status;
}

static void apply_shear_value(TransInfo *t, const float value)
{
  float smat[3][3];
  unit_m3(smat);
  smat[1][0] = value;

  float axismat_inv[3][3];
  copy_v3_v3(axismat_inv[0], t->spacemtx[t->orient_axis_ortho]);
  copy_v3_v3(axismat_inv[2], t->spacemtx[t->orient_axis]);
  cross_v3_v3v3(axismat_inv[1], axismat_inv[0], axismat_inv[2]);
  float axismat[3][3];
  invert_m3_m3(axismat, axismat_inv);

  float mat_final[3][3];
  mul_m3_series(mat_final, axismat_inv, smat, axismat);

  const bool is_local_center = transdata_check_local_center(t, t->around);

  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    if (tc->data_len < TRANSDATA_THREAD_LIMIT) {
      TransData *td = tc->data;
      for (int i = 0; i < tc->data_len; i++, td++) {
        if (td->flag & TD_SKIP) {
          continue;
        }
        transdata_elem_shear(t, tc, td, mat_final, is_local_center);
      }
    }
    else {
      TransDataArgs_Shear data{};
      data.t = t;
      data.tc = tc;
      data.is_local_center = is_local_center;
      copy_m3_m3(data.mat_final, mat_final);

      TaskParallelSettings settings;
      lib_parallel_range_settings_defaults(&settings);
      lib_task_parallel_range(0, tc->data_len, &data, transdata_elem_shear_fn, &settings);
    }
  }
}

static bool uv_shear_in_clip_bounds_test(const TransInfo *t, const float value)
{
  const int axis = t->orient_axis_ortho;
  if (axis < 0 || 1 < axis) {
    return true; /* Non standard axis, nothing to do. */
  }
  const float *center = t->center_global;
  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    TransData *td = tc->data;
    for (int i = 0; i < tc->data_len; i++, td++) {
      if (td->flag & TD_SKIP) {
        continue;
      }
      if (td->factor < 1.0f) {
        continue; /* Proportional edit, will get picked up in next phase. */
      }

      float uv[2];
      sub_v2_v2v2(uv, td->iloc, center);
      uv[axis] = uv[axis] + value * uv[1 - axis] * (2 * axis - 1);
      add_v2_v2(uv, center);
      /* TODO: UDIM support. */
      if (uv[axis] < 0.0f || 1.0f < uv[axis]) {
        return false;
      }
    }
  }
  return true;
}

static bool clip_uv_transform_shear(const TransInfo *t, float *vec, float *vec_inside_bounds)
{
  float value = vec[0];
  if (uv_shear_in_clip_bounds_test(t, value)) {
    vec_inside_bounds[0] = value; /* Store for next iteration. */
    return false;                 /* Nothing to do. */
  }
  float value_inside_bounds = vec_inside_bounds[0];
  if (!uv_shear_in_clip_bounds_test(t, value_inside_bounds)) {
    return false; /* No known way to fix, may as well shear anyway. */
  }
  const int max_i = 32; /* Limit iter, mainly for debugging. */
  for (int i = 0; i < max_i; i++) {
    /* Binary search. */
    const float val_mid = (val_inside_bounds + val) / 2.0f;
    if (ELEM(val_mid, val_inside_bounds, val)) {
      break; /* float precision reached. */
    }
    if (uv_shear_in_clip_bounds_test(t, value_mid)) {
      val_inside_bounds = value_mid;
    }
    else {
      value = val_mid;
    }
  }

  vec_inside_bounds[0] = val_inside_bounds; /* Store for next iteration. */
  vec[0] = val_inside_bounds;               /* Update shear value. */
  return true;
}

static void apply_shear(TransInfo *t)
{
  float val = t->vals[0] + t->vals_modal_offset[0];
  transform_snap_increment(t, &val);
  applyNumInput(&t->num, &val);
  t->values_final[0] = val;

  apply_shear_val(t, val);

  if (t->flag & T_CLIP_UV) {
    if (clip_uv_transform_shear(t, t->values_final, t->values_inside_constraints)) {
      apply_shear_value(t, t->values_final[0]);
    }

    /* Not ideal, see #clipUVData code-comment. */
    if (t->flag & T_PROP_EDIT) {
      clipUVData(t);
    }
  }

  recalc_data(t);

  char str[UI_MAX_DRAW_STR];
  /* header print for NumInput */
  if (hasNumInput(&t->num)) {
    char c[NUM_STR_REP_LEN];
    outputNumInput(&(t->num), c, &t->scene->unit);
    SNPRINTF(str, TIP_("Shear: %s %s"), c, t->proptext);
  }
  else {
    /* default header print */
    SNPRINTF(str, TIP_("Shear: %.3f %s (Press X or Y to set shear axis)"), value, t->proptext);
  }

  ed_area_status_text(t->area, str);
}

static void initShear(TransInfo *t, wmOperator * /*op*/)
{
  t->mode = TFM_SHEAR;

  if (t->orient_axis == t->orient_axis_ortho) {
    t->orient_axis = 2;
    t->orient_axis_ortho = 1;
  }

  initShear_mouseInputMode(t);

  t->idx_max = 0;
  t->num.idx_max = 0;
  t->snap[0] = 0.1f;
  t->snap[1] = t->snap[0] * 0.1f;

  copy_v3_fl(t->num.val_inc, t->snap[0]);
  t->num.unit_sys = t->scene->unit.system;
  t->num.unit_type[0] = B_UNIT_NONE; /* Don't think we have any unit here? */

  transform_mode_default_modal_orientation_set(t, V3D_ORIENT_VIEW);
}

TransModeInfo TransMode_shear = {
    /*flags*/ T_NO_CONSTRAINT,
    /*init_fn*/ initShear,
    /*transform_fn*/ apply_shear,
    /*transform_matrix_fn*/ nullptr,
    /*handle_ev_fn*/ handleEventShear,
    /*snap_distance_fn*/ nullptr,
    /*snap_apply_fn*/ nullptr,
    /*draw_fn*/ nullptr,
};
