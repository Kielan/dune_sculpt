#include <cstdlib>

#include "mem_guardedalloc.h"

#include "lib_blenlib.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_unit.hh"

#include "ed_screen.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ui.hh"
#include "ui_view2d.hh"

#include "seq_iter.hh"
#include "seq_seq.hh"
#include "seq_time.hh"

#include "lang.h"

#include "transform.hh"
#include "transform_convert.hh"
#include "transform_mode.hh"
#include "transform_snap.hh"

/* Transform (Seq Slide) */
static void headerSeqSlide(TransInfo *t, const float val[2], char str[UI_MAX_DRAW_STR])
{
  char tvec[NUM_STR_REP_LEN * 3];
  size_t ofs = 0;

  if (hasNumInput(&t->num)) {
    outputNumInput(&(t->num), tvec, &t->scene->unit);
  }
  else {
    lib_snprintf(&tvec[0], NUM_STR_REP_LEN, "%.0f, %.0f", val[0], val[1]);
  }

  ofs += lib_snprintf_rlen(
      str + ofs, UI_MAX_DRW_STR - ofs, TIP_("Seq Slide: %s%s"), &tvec[0], t->con.text);
}

static void applySeqSlideVal(TransInfo *t, const float val[2])
{
  int i;

  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    TransData *td = tc->data;
    for (i = 0; i < tc->data_len; i++, td++) {
      if (td->flag & TD_SKIP) {
        continue;
      }

      madd_v2_v2v2fl(td->loc, td->iloc, val, td->factor);
    }
  }
}

static void applySeqSlide(TransInfo *t)
{
  char str[UI_MAX_DRW_STR];
  float vals_final[3] = {0.0f};

  if (applyNumInput(&t->num, vals_final)) {
    if (t->con.mode & CON_APPLY) {
      if (t->con.mode & CON_AXIS0) {
        mul_v2_v2fl(vals_final, t->spacemtx[0], vals_final[0]);
      }
      else {
        mul_v2_v2fl(vals_final, t->spacemtx[1], vals_final[0]);
      }
    }
  }
  else {
    copy_v2_v2(vals_final, t->vals);
    transform_snap_mixed_apply(t, vals_final);
    transform_convert_seq_channel_clamp(t, vals_final);

    if (t->con.mode & CON_APPLY) {
      t->con.applyVec(t, nullptr, nullptr, vals_final, vals_final);
    }
  }

  vals_final[0] = floorf(vals_final[0] + 0.5f);
  vals_final[1] = floorf(vals_final[1] + 0.5f);
  copy_v2_v2(t->values_final, vals_final);

  headerSeqSlide(t, t->vals_final, str);
  applySeqSlideVal(t, t->vals_final);

  recalc_data(t);

  ed_area_status_txt(t->area, str);
}

static void initSeqSlide(TransInfo *t, WinOp * /*op*/)
{
  initMouseInputMode(t, &t->mouse, INPUT_VECTOR);

  t->idx_max = 1;
  t->num.flag = 0;
  t->num.idx_max = t->idx_max;

  t->snap[0] = floorf(t->scene->r.frs_sec / t->scene->r.frs_sec_base);
  t->snap[1] = 10.0f;

  copy_v3_fl(t->num.val_inc, t->snap[0]);
  t->num.unit_sys = t->scene->unit.system;
  /* Would be nice to have a time handling in units as well
   * (supporting frames in addition to "natural" time...). */
  t->num.unit_type[0] = B_UNIT_NONE;
  t->num.unit_type[1] = B_UNIT_NONE;

  if (t->keymap) {
    /* Workaround to use the same key as the modal keymap. */
    t->custom.mode.data = (void *)win_modalkeymap_find_propval(t->keymap, TFM_MODAL_TRANSLATE);
  }
}

TransModeInfo TransModeSeqSlide = {
    /*flags*/ 0,
    /*init_fn*/ initSeqSlide,
    /*transform_fn*/ applySeqSlide,
    /*transform_matrix_fn*/ nullptr,
    /*handle_ev_fn*/ nullptr,
    /*snap_distance_fn*/ nullptr,
    /*snap_apply_fn*/ transform_snap_seq_apply_translate,
    /*drw_fn*/ nullptr,
};
