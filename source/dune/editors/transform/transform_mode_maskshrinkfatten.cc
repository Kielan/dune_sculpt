#include <cstdlib>

#include "lib_math_vector.h"
#include "lib_string.h"

#include "dune_cxt.hh"
#include "dune_unit.hh"

#include "ed_screen.hh"

#include "ui.hh"

#include "lang.h"

#include "transform.hh"
#include "transform_convert.hh"
#include "transform_snap.hh"

#include "transform_mode.hh"

/* Transform (Mask Shrink/Fatten) */
static void applyMaskShrinkFatten(TransInfo *t)
{
  float ratio;
  int i;
  bool initial_feather = false;
  char str[UI_MAX_DRW_STR];

  ratio = t->vals[0] + t->vals_modal_offset[0];

  transform_snap_increment(t, &ratio);

  applyNumInput(&t->num, &ratio);

  t->vals_final[0] = ratio;

  /* header print for NumInput */
  if (hasNumInput(&t->num)) {
    char c[NUM_STR_REP_LEN];

    outputNumInput(&(t->num), c, &t->scene->unit);
    SNPRINTF(str, TIP_("Feather Shrink/Fatten: %s"), c);
  }
  else {
    SNPRINTF(str, TIP_("Feather Shrink/Fatten: %3f"), ratio);
  }

  /* detect if no points have feather yet */
  if (ratio > 1.0f) {
    initial_feather = true;

    FOREACH_TRANS_DATA_CONTAINER (t, tc) {
      TransData *td = tc->data;
      for (i = 0; i < tc->data_len; i++, td++) {
        if (td->flag & TD_SKIP) {
          continue;
        }

        if (td->ival >= 0.001f) {
          initial_feather = false;
        }
      }
    }
  }

  /* apply shrink/fatten */
  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    TransData *td;
    for (td = tc->data, i = 0; i < tc->data_len; i++, td++) {
      if (td->flag & TD_SKIP) {
        continue;
      }

      if (td->val) {
        if (initial_feather) {
          *td->val = td->ival + (ratio - 1.0f) * 0.01f;
        }
        else {
          *td->val = td->ival * ratio;
        }

        /* Apply proportional editing. */
        *td->val = interpf(*td->val, td->ival, td->factor);
        if (*td->val <= 0.0f) {
          *td->val = 0.001f;
        }
      }
    }
  }

  recalc_data(t);

  ed_area_status_text(t->area, str);
}

static void initMaskShrinkFatten(TransInfo *t, WinOp * /*op*/)
{
  t->mode = TFM_MASK_SHRINKFATTEN;

  initMouseInputMode(t, &t->mouse, INPUT_SPRING);

  t->idx_max = 0;
  t->num.idx_max = 0;
  t->snap[0] = 0.1f;
  t->snap[1] = t->snap[0] * 0.1f;

  copy_v3_fl(t->num.val_inc, t->snap[0]);
  t->num.unit_sys = t->scene->unit.system;
  t->num.unit_type[0] = B_UNIT_NONE;

#ifdef USE_NUM_NO_ZERO
  t->num.val_flag[0] |= NUM_NO_ZERO;
#endif
}

TransModeInfo TransModeMaskshrinkfatten = {
    /*flags*/ T_NO_CONSTRAINT,
    /*init_fn*/ initMaskShrinkFatten,
    /*transform_fn*/ applyMaskShrinkFatten,
    /*transform_matrix_fn*/ nullptr,
    /*handle_ev_fn*/ nullptr,
    /*snap_distance_fn*/ nullptr,
    /*snap_apply_fn*/ nullptr,
    /*drw_fn*/ nullptr,
};
