#include <cstdlib>

#include "lib_math_vector.h"
#include "lib_string.h"
#include "lib_task.h"

#include "dune_cxt.hh"
#include "dune_unit.hh"

#include "ed_screen.hh"

#include "ui.hh"

#include "lang.h"

#include "transform.hh"
#include "transform_convert.hh"
#include "transform_snap.hh"

#include "transform_mode.hh"

/* Transform Element */
/* Small arrays/data-structs should be stored copied for faster mem access. */
struct TransDataArgsVal {
  const TransInfo *t;
  const TransDataContainer *tc;
  float value;
};

static void transdata_elem_val(const TransInfo * /*t*/,
                               const TransDataContainer * /*tc*/,
                               TransData *td,
                               const float val)
{
  if (td->val == nullptr) {
    return;
  }

  *td->val = td->ival + val * td->factor;
  CLAMP(*td->val, 0.0f, 1.0f);
}

static void transdata_elem_val_fn(void *__restrict iter_data_v,
                                  const int iter,
                                  const TaskParallelTLS *__restrict /*tls*/)
{
  TransDataArgsVal *data = static_cast<TransDataArgsVal *>(iter_data_v);
  TransData *td = &data->tc->data[iter];
  if (td->flag & TD_SKIP) {
    return;
  }
  transdata_elem_val(data->t, data->tc, td, data->val);
}


/* Transform Val */
static void apply_val_impl(TransInfo *t, const char *val_name)
{
  float val;
  int i;
  char str[UI_MAX_DRW_STR];

  val = t->vals[0] + t->vals_modal_offset[0];

  CLAMP_MAX(val, 1.0f);

  transform_snap_increment(t, &val);

  applyNumInput(&t->num, &val);

  t->vals_final[0] = val;

  /* header print for NumInput */
  if (hasNumInput(&t->num)) {
    char c[NUM_STR_REP_LEN];

    outputNumInput(&(t->num), c, &t->scene->unit);

    if (val >= 0.0f) {
      SNPRINTF(str, "%s: +%s %s", val_name, c, t->proptext);
    }
    else {
      SNPRINTF(str, "%s: %s %s", val_name, c, t->proptext);
    }
  }
  else {
    /* default header print */
    if (val >= 0.0f) {
      SNPRINTF(str, "%s: +%.3f %s", val_name, val, t->proptext);
    }
    else {
      SNPRINTF(str, "%s: %.3f %s", val_name, val, t->proptext);
    }
  }

  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    if (tc->data_len < TRANSDATA_THREAD_LIMIT) {
      TransData *td = tc->data;
      for (i = 0; i < tc->data_len; i++, td++) {
        if (td->flag & TD_SKIP) {
          continue;
        }
        transdata_elem_val(t, tc, td, val);
      }
    }
    else {
      TransDataArgsVal data{};
      data.t = t;
      data.tc = tc;
      data.val = value;
      TaskParallelSettings settings;
      lib_parallel_range_settings_defaults(&settings);
      lib_task_parallel_range(0, tc->data_len, &data, transdata_elem_val_fn, &settings);
    }
  }

  recalc_data(t);

  ed_area_status_txt(t->area, str);
}

static void applyCrease(TransInfo *t)
{
  apply_val_impl(t, TIP_("Crease"));
}

static void applyBevelWeight(TransInfo *t)
{
  apply_val_impl(t, TIP_("Bevel Weight"));
}

static void init_mode_impl(TransInfo *t)
{
  initMouseInputMode(t, &t->mouse, INPUT_SPRING_DELTA);

  t->idx_max = 0;
  t->num.idx_max = 0;
  t->snap[0] = 0.1f;
  t->snap[1] = t->snap[0] * 0.1f;

  copy_v3_fl(t->num.val_inc, t->snap[0]);
  t->num.unit_sys = t->scene->unit.sys;
  t->num.unit_type[0] = B_UNIT_NONE;
}

static void initEgdeCrease(TransInfo *t, WinOp * /*op*/)
{
  init_mode_impl(t);
  t->mode = TFM_EDGE_CREASE;
}

static void initVertCrease(TransInfo *t, WinOp * /*op*/)
{
  init_mode_impl(t);
  t->mode = TFM_VERT_CREASE;
}

static void initBevelWeight(TransInfo *t, WinOp * /*op*/)
{
  init_mode_impl(t);
  t->mode = TFM_BWEIGHT;
}

TransModeInfo TransMode_edgecrease = {
    /*flags*/ T_NO_CONSTRAINT | T_NO_PROJECT,
    /*init_fn*/ initEgdeCrease,
    /*transform_fn*/ applyCrease,
    /*transform_matrix_fn*/ nullptr,
    /*handle_ev_fn*/ nullptr,
    /*snap_distance_fn*/ nullptr,
    /*snap_apply_fn*/ nullptr,
    /*draw_fn*/ nullptr,
};

TransModeInfo TransMode_vertcrease = {
    /*flags*/ T_NO_CONSTRAINT | T_NO_PROJECT,
    /*init_fn*/ initVertCrease,
    /*transform_fn*/ applyCrease,
    /*transform_matrix_fn*/ nullptr,
    /*handle_ev_fn*/ nullptr,
    /*snap_distance_fn*/ nullptr,
    /*snap_apply_fn*/ nullptr,
    /*draw_fn*/ nullptr,
};

TransModeInfo TransMode_bevelweight = {
    /*flags*/ T_NO_CONSTRAINT | T_NO_PROJECT,
    /*init_fn*/ initBevelWeight,
    /*transform_fn*/ applyBevelWeight,
    /*transform_matrix_fn*/ nullptr,
    /*handle_ev_fn*/ nullptr,
    /*snap_distance_fn*/ nullptr,
    /*snap_apply_fn*/ nullptr,
    /*draw_fn*/ nullptr,
};
