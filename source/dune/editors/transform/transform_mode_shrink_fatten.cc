#include <cstdlib>

#include "BLI_math_vector.h"
#include "BLI_string.h"
#include "BLI_task.h"

#include "BKE_context.hh"
#include "BKE_report.h"
#include "BKE_unit.hh"

#include "ED_screen.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "UI_interface.hh"

#include "BLT_translation.h"

#include "transform.hh"
#include "transform_convert.hh"
#include "transform_snap.hh"

#include "transform_mode.hh"

/* -------------------------------------------------------------------- */
/** \name Transform (Shrink-Fatten) Element
 * \{ */

/**
 * \note Small arrays / data-structures should be stored copied for faster memory access.
 */
struct TransDataArgs_ShrinkFatten {
  const TransInfo *t;
  const TransDataContainer *tc;
  float distance;
};

static void transdata_elem_shrink_fatten(const TransInfo *t,
                                         const TransDataContainer * /*tc*/,
                                         TransData *td,
                                         const float distance)
{
  /* Get the final offset. */
  float tdistance = distance * td->factor;
  if (td->ext && (t->flag & T_ALT_TRANSFORM) != 0) {
    tdistance *= td->ext->isize[0]; /* shell factor */
  }

  madd_v3_v3v3fl(td->loc, td->iloc, td->axismtx[2], tdistance);
}

static void transdata_elem_shrink_fatten_fn(void *__restrict iter_data_v,
                                            const int iter,
                                            const TaskParallelTLS *__restrict /*tls*/)
{
  TransDataArgs_ShrinkFatten *data = static_cast<TransDataArgs_ShrinkFatten *>(iter_data_v);
  TransData *td = &data->tc->data[iter];
  if (td->flag & TD_SKIP) {
    return;
  }
  transdata_elem_shrink_fatten(data->t, data->tc, td, data->distance);
}

/* Transform (Shrink-Fatten) */
static eRedrwFlag shrinkfatten_handleEv(TransInfo *t, const WinEv *ev)
{
  lib_assert(t->mode == TFM_SHRINKFATTEN);
  const WinKeyMapItem *kmi = static_cast<const WinKeyMapItem *>(t->custom.mode.data);
  if (kmi && ev->type == kmi->type && ev->val == kmi->val) {
    /* Allows the "Even Thickness" effect to be enabled as a toggle. */
    t->flag ^= T_ALT_TRANSFORM;
    return TREDRW_HARD;
  }
  return TREDRW_NOTHING;
}

static void applyShrinkFatten(TransInfo *t)
{
  float distance;
  int i;
  char str[UI_MAX_DRW_STR];
  size_t ofs = 0;
  UnitSettings *unit = &t->scene->unit;

  distance = t->vals[0] + t->vals_modal_offset[0];

  transform_snap_increment(t, &distance);

  applyNumInput(&t->num, &distance);

  t->values_final[0] = distance;

  /* header print for NumInput */
  ofs += lib_strncpy_rlen(str + ofs, TIP_("Shrink/Fatten: "), sizeof(str) - ofs);
  if (hasNumInput(&t->num)) {
    char c[NUM_STR_REP_LEN];
    outputNumInput(&(t->num), c, unit);
    ofs += lib_snprintf_rlen(str + ofs, sizeof(str) - ofs, "%s", c);
  }
  else {
    /* default header print */
    if (unit != nullptr) {
      ofs += dune_unit_val_as_string(str + ofs,
                                      sizeof(str) - ofs,
                                      distance * unit->scale_length,
                                      4,
                                      B_UNIT_LENGTH,
                                      unit,
                                      true);
    }
    else {
      ofs += lib_snprintf_rlen(str + ofs, sizeof(str) - ofs, "%.4f", distance);
    }
  }

  if (t->proptext[0]) {
    ofs += lib_snprintf_rlen(str + ofs, sizeof(str) - ofs, " %s", t->proptext);
  }
  ofs += lib_strncpy_rlen(str + ofs, ", (", sizeof(str) - ofs);

  const WinKeyMapItem *kmi = static_cast<const WinKeyMapItem *>(t->custom.mode.data);
  if (kmi) {
    ofs += win_keymap_item_to_string(kmi, false, str + ofs, sizeof(str) - ofs);
  }

  lib_snprintf(str + ofs,
               sizeof(str) - ofs,
               TIP_(" or Alt) Even Thickness %s"),
               win_bool_as_string((t->flag & T_ALT_TRANSFORM) != 0));
  /* done with header string */
  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    if (tc->data_len < TRANSDATA_THREAD_LIMIT) {
      TransData *td = tc->data;
      for (i = 0; i < tc->data_len; i++, td++) {
        if (td->flag & TD_SKIP) {
          continue;
        }
        transdata_elem_shrink_fatten(t, tc, td, distance);
      }
    }
    else {
      TransDataArgs_ShrinkFatten data{};
      data.t = t;
      data.tc = tc;
      data.distance = distance;
      TaskParallelSettings settings;
      lib_parallel_range_settings_defaults(&settings);
      lib_task_parallel_range(0, tc->data_len, &data, transdata_elem_shrink_fatten_fn, &settings);
    }
  }

  recalc_data(t);

  ed_area_status_text(t->area, str);
}

static void initShrinkFatten(TransInfo *t, WinOp * /*op*/)
{
  if ((t->flag & T_EDIT) == 0 || (t->obedit_type != OB_MESH)) {
    dune_report(t->reports, RPT_ERROR, "'Shrink/Fatten' meshes is only supported in edit mode");
    t->state = TRANS_CANCEL;
  }

  t->mode = TFM_SHRINKFATTEN;

  initMouseInputMode(t, &t->mouse, INPUT_VERTICAL_ABSOLUTE);

  t->idx_max = 0;
  t->num.idx_max = 0;
  t->snap[0] = 1.0f;
  t->snap[1] = t->snap[0] * 0.1f;

  copy_v3_fl(t->num.val_inc, t->snap[0]);
  t->num.unit_sys = t->scene->unit.sys;
  t->num.unit_type[0] = B_UNIT_LENGTH;

  if (t->keymap) {
    /* Workaround to use the same key as the modal keymap. */
    t->custom.mode.data = (void *)win_modalkeymap_find_propval(t->keymap, TFM_MODAL_RESIZE);
  }
}

TransModeInfo TransModeShrinkfatten = {
    /*flags*/ T_NO_CONSTRAINT,
    /*init_fn*/ initShrinkFatten,
    /*transform_fn*/ applyShrinkFatten,
    /*transform_matrix_fn*/ nullptr,
    /*handle_ev_fn*/ shrinkfatten_handleEv,
    /*snap_distance_fn*/ nullptr,
    /*snap_apply_fn*/ nullptr,
    /*draw_fn*/ nullptr,
};
