#include <cstdlib>

#include "lib_math_matrix.h"
#include "lib_math_vector.h"
#include "lib_string.h"
#include "lib_task.h"

#include "dune_cxt.hh"
#include "dune_unit.hh"

#include "ed_screen.hh"

#include "ui.hh"

#include "lang.h"

#include "transform.hh"
#include "transform_constraints.hh"
#include "transform_convert.hh"
#include "transform_snap.hh"

#include "transform_mode.hh"

/* Transform (Push/Pull) Element */
/* Small arrays/data-structs should be stored copied for faster mem access. */
struct TransDataArgsPushPull {
  const TransInfo *t;
  const TransDataContainer *tc;
  float distance;
  float axis_global[3];
  bool is_lock_constraint;
  bool is_data_space;
};

static void transdata_elem_push_pull(const TransInfo *t,
                                     const TransDataContainer *tc,
                                     TransData *td,
                                     const float distance,
                                     const float axis_global[3],
                                     const bool is_lock_constraint,
                                     const bool is_data_space)
{
  float vec[3];
  sub_v3_v3v3(vec, tc->center_local, td->center);
  if (t->con.applyRot && t->con.mode & CON_APPLY) {
    float axis[3];
    copy_v3_v3(axis, axis_global);
    t->con.applyRot(t, tc, td, axis, nullptr);

    mul_m3_v3(td->smtx, axis);
    if (is_lock_constraint) {
      float dvec[3];
      project_v3_v3v3(dvec, vec, axis);
      sub_v3_v3(vec, dvec);
    }
    else {
      project_v3_v3v3(vec, vec, axis);
    }
  }
  normalize_v3_length(vec, distance * td->factor);
  if (is_data_space) {
    mul_m3_v3(td->smtx, vec);
  }

  add_v3_v3v3(td->loc, td->iloc, vec);
}

static void transdata_elem_push_pull_fn(void *__restrict iter_data_v,
                                        const int iter,
                                        const TaskParallelTLS *__restrict /*tls*/)
{
  TransDataArgsPushPull *data = static_cast<TransDataArgsPushPull *>(iter_data_v);
  TransData *td = &data->tc->data[iter];
  if (td->flag & TD_SKIP) {
    return;
  }
  transdata_elem_push_pull(data->t,
                           data->tc,
                           td,
                           data->distance,
                           data->axis_global,
                           data->is_lock_constraint,
                           data->is_data_space);
}


/* Transform (Push/Pull) */
static void applyPushPull(TransInfo *t)
{
  float axis_global[3];
  float distance;
  int i;
  char str[UI_MAX_DRW_STR];

  distance = t->vals[0] + t->vals_modal_offset[0];

  transform_snap_increment(t, &distance);

  applyNumInput(&t->num, &distance);

  t->vals_final[0] = distance;

  /* header print for NumInput */
  if (hasNumInput(&t->num)) {
    char c[NUM_STR_REP_LEN];

    outputNumInput(&(t->num), c, &t->scene->unit);

    SNPRINTF(str, TIP_("Push/Pull: %s%s %s"), c, t->con.text, t->proptext);
  }
  else {
    /* default header print */
    SNPRINTF(str, TIP_("Push/Pull: %.4f%s %s"), distance, t->con.text, t->proptext);
  }

  if (t->con.applyRot && t->con.mode & CON_APPLY) {
    t->con.applyRot(t, nullptr, nullptr, axis_global, nullptr);
  }

  const bool is_lock_constraint = isLockConstraint(t);
  const bool is_data_space = (t->options & CXT_POSE_BONE) != 0;

  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    if (tc->data_len < TRANSDATA_THREAD_LIMIT) {
      TransData *td = tc->data;
      for (i = 0; i < tc->data_len; i++, td++) {
        if (td->flag & TD_SKIP) {
          continue;
        }
        transdata_elem_push_pull(
            t, tc, td, distance, axis_global, is_lock_constraint, is_data_space);
      }
    }
    else {
      TransDataArgsPushPull data{};
      data.t = t;
      data.tc = tc;
      data.distance = distance;
      copy_v3_v3(data.axis_global, axis_global);
      data.is_lock_constraint = is_lock_constraint;
      data.is_data_space = is_data_space;
      TaskParallelSettings settings;
      lib_parallel_range_settings_defaults(&settings);
      lib_task_parallel_range(0, tc->data_len, &data, transdata_elem_push_pull_fn, &settings);
    }
  }

  recalc_data(t);

  ed_area_status_text(t->area, str);
}

static void initPushPull(TransInfo *t, WinOp * /*op*/)
{
  t->mode = TFM_PUSHPULL;

  initMouseInputMode(t, &t->mouse, INPUT_VERT_ABSOLUTE);

  t->idx_max = 0;
  t->num.idx_max = 0;
  t->snap[0] = 1.0f;
  t->snap[1] = t->snap[0] * 0.1f;

  copy_v3_fl(t->num.val_inc, t->snap[0]);
  t->num.unit_sys = t->scene->unit.system;
  t->num.unit_type[0] = B_UNIT_LENGTH;
}

TransModeInfo TransModePushpull = {
    /*flags*/ 0,
    /*init_fn*/ initPushPull,
    /*transform_fn*/ applyPushPull,
    /*transform_matrix_fn*/ nullptr,
    /*handle_ev_fn*/ nullptr,
    /*snap_distance_fn*/ nullptr,
    /*snap_apply_fn*/ nullptr,
    /*drw_fn*/ nullptr,
};
