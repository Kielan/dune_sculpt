#include "mem_guardedalloc.h"

#include "types_win.h"

#include "dune_cxt.hh"

#include "ed_screen.hh"
#include "ed_transform_snap_ob_cxt.hh"

#include "transform.hh"
#include "transform_convert.hh"
#include "transform_gizmo.hh"
#include "transform_snap.hh"

#include "transform_mode.hh"

#define RESET_TRANSFORMATION
#define REMOVE_GIZMO

using namespace dune;

/* Transform Element */
/* Small arrays/datastructs should be stored copied for faster mem access. */
struct SnapSrcCustomData {
  TransModeInfo *mode_info_prev;
  void *customdata_mode_prev;

  eSnapTargetOP target_op_prev;
  eSnapMode snap_mode_confirm;

  struct {
    void (*apply)(TransInfo *t, MouseInput *mi, const double mval[2], float output[3]);
    void (*post)(TransInfo *t, float vals[3]);
    bool use_virtual_mval;
  } mouse_prev;
};

static void snapsrc_end(TransInfo *t)
{
  t->mods &= ~MOD_EDIT_SNAP_SRC;

  /* Restore. */
  SnapSrcCustomData *customdata = static_cast<SnapSrcCustomData *>(t->custom.mode.data);
  t->mode_info = customdata->mode_info_prev;
  t->custom.mode.data = customdata->customdata_mode_prev;

  t->tsnap.target_op = customdata->target_op_prev;

  t->mouse.apply = customdata->mouse_prev.apply;
  t->mouse.post = customdata->mouse_prev.post;
  t->mouse.use_virtual_mval = customdata->mouse_prev.use_virtual_mval;

  mem_free(customdata);

  transform_gizmo_3d_model_from_constraint_and_mode_set(t);
  tranform_snap_src_restore_cxt(t);
}

static void snapsrc_confirm(TransInfo *t)
{
  lib_assert(t->mods & MOD_EDIT_SNAP_SRC);
  getSnapPoint(t, t->tsnap.snap_src);
  t->tsnap.snap_src_fn = nullptr;
  t->tsnap.src_type = t->tsnap.target_type;
  t->tsnap.status |= SNAP_SRC_FOUND;

  SnapSrcCustomData *customdata = static_cast<SnapSrcCustomData *>(t->custom.mode.data);
  t->tsnap.mode = customdata->snap_mode_confirm;

  float2 mval;
#ifndef RESET_TRANSFORMATION
  if (true) {
    if (t->transform_matrix) {
      float mat_inv[4][4];
      unit_m4(mat_inv);
      t->transform_matrix(t, mat_inv);
      invert_m4(mat_inv);
      mul_m4_v3(mat_inv, t->tsnap.snap_src);
    }
    else {
      float mat_inv[3][3];
      invert_m3_m3(mat_inv, t->mat);

      mul_m3_v3(mat_inv, t->tsnap.snap_src);
      sub_v3_v3(t->tsnap.snap_src, t->vec);
    }

    projectFloatView(t, t->tsnap.snap_src, mval);
  }
  else
#endif
  {
    mval = t->mval;
  }

  snapsrc_end(t);
  transform_input_reset(t, mval);

  /* Remote individual snap projection since this mode does not use the new `snap_src`. */
  t->tsnap.mode &= ~(SCE_SNAP_INDIVIDUAL_PROJECT | SCE_SNAP_INDIVIDUAL_NEAREST);
}

static eRedrwFlag snapsrc_handle_ev_fn(TransInfo *t, const WinEv *ev)
{
  if (ev->type == EV_MODAL_MAP) {
    switch (ev->val) {
      case TFM_MODAL_CONFIRM:
      case TFM_MODAL_EDIT_SNAP_SRC_ON:
      case TFM_MODAL_EDIT_SNAP_SRC_OFF:
        if (t->mods & MOD_EDIT_SNAP_SRC) {
          snapsrc_confirm(t);

          lib_assert(t->state != TRANS_CONFIRM);
        }
        else {
          t->mods |= MOD_EDIT_SNAP_SRC;
        }
        break;
      case TFM_MODAL_CANCEL:
        snapsrc_end(t);
        t->state = TRANS_CANCEL;
        return TREDRW_SOFT;
      default:
        break;
    }
  }
  else if (ev->val == KM_RELEASE && t->state == TRANS_CONFIRM) {
    if (t->flag & T_RELEASE_CONFIRM && t->modifiers & MOD_EDIT_SNAP_SRC) {
      snapsrc_confirm(t);
      t->flag &= ~T_RELEASE_CONFIRM;
      t->state = TRANS_RUNNING;
    }
  }
  return TREDRW_NOTHING;
}

static void snapsrc_transform_fn(TransInfo *t)
{
  lib_assert(t->mods & MOD_EDIT_SNAP_SRC);

  t->tsnap.snap_target_fn(t, nullptr);
  if (t->tsnap.status & SNAP_MULTI_POINTS) {
    getSnapPoint(t, t->tsnap.snap_src);
  }
  t->redrw |= TREDRW_SOFT;
}

void transform_mode_snap_src_init(TransInfo *t, WinOp * /*op*/)
{
  if (t->mode_info == &TransModeSnapSrc) {
    /* Alrdy running. */
    return;
  }

  if (!t->tsnap.snap_target_fn) {
    /* A `snap_target_fn` is required for the op to work.
     * `snap_target_fn` can be `nullptr` when transforming camera in camera view. */
    return;
  }

  if (ELEM(t->mode, TFM_INIT, TFM_DUMMY)) {
    /* Fallback */
    transform_mode_init(t, nullptr, TFM_TRANSLATION);
  }

  SnapSrcCustomData *customdata = static_cast<SnapSrcCustomData *>(
      mem_calloc(sizeof(*customdata), __func__));
  customdata->mode_info_prev = t->mode_info;

  customdata->target_op_prev = t->tsnap.target_op;

  customdata->mouse_prev.apply = t->mouse.apply;
  customdata->mouse_prev.post = t->mouse.post;
  customdata->mouse_prev.use_virtual_mval = t->mouse.use_virtual_mval;

  customdata->customdata_mode_prev = t->custom.mode.data;
  t->custom.mode.data = customdata;

  if (!(t->mods & MOD_SNAP) || !transformModeUseSnap(t)) {
    t->mods |= (MOD_SNAP | MOD_SNAP_FORCED);
  }

  t->mode_info = &TransMode_snapsrc;
  t->flag |= T_DRW_SNAP_SRC;
  t->tsnap.target_op = SCE_SNAP_TARGET_ALL;
  t->tsnap.status &= ~SNAP_SRC_FOUND;

  customdata->snap_mode_confirm = t->tsnap.mode;
  t->tsnap.mode &= ~(SCE_SNAP_TO_EDGE_PERPENDICULAR | SCE_SNAP_INDIVIDUAL_PROJECT |
                     SCE_SNAP_INDIVIDUAL_NEAREST);

  if ((t->tsnap.mode & ~(SCE_SNAP_TO_INCREMENT | SCE_SNAP_TO_GRID)) == 0) {
    /* Init snap modes for geometry. */
    t->tsnap.mode &= ~(SCE_SNAP_TO_INCREMENT | SCE_SNAP_TO_GRID);
    t->tsnap.mode |= SCE_SNAP_TO_GEOM & ~SCE_SNAP_TO_EDGE_PERPENDICULAR;

    if (!(customdata->snap_mode_confirm & SCE_SNAP_TO_EDGE_PERPENDICULAR)) {
      customdata->snap_mode_confirm = t->tsnap.mode;
    }
  }

  if (t->data_type == &TransConvertTypeMesh) {
    ed_transform_snap_ob_cxt_set_meshedit_cbs(
        t->tsnap.ob_cxt, nullptr, nullptr, nullptr, nullptr);
  }

#ifdef RESET_TRANSFORMATION
  /* Tmp disable snapping.
   * We don't want SCE_SNAP_PROJECT to affect `recalc_data` for example. */
  t->tsnap.flag &= ~SCE_SNAP;

  restoreTransObs(t);

  /* Restore snapping status. */
  transform_snap_flag_from_mods_set(t);

  /* Reset init vals to restore gizmo position. */
  applyMouseInput(t, &t->mouse, t->mouse.imval, t->vals_final);
#endif

#ifdef REMOVE_GIZMO
  wmGizmo *gz = win_gizmomap_get_modal(t->rgn->gizmo_map);
  if (gz) {
    const WinEv *ev = cxt_win(t->cxt)->evstate;
#  ifdef RESET_TRANSFORMATION
    WinGizmoFnModal modal_fn = gz->custom_modal ? gz->custom_modal : gz->type->modal;
    modal_fn(t->cxt, gz, ev, eWinGizmoFlagTweak(0));
#  endif

    win_gizmo_modal_set_while_modal(t->gn->gizmo_map, t->cxt, nullptr, ev);
  }
#endif

  t->mouse.apply = nullptr;
  t->mouse.post = nullptr;
  t->mouse.use_virtual_mval = false;
}

/** \} */

TransModeInfo TransMode_snapsource = {
    /*flags*/ 0,
    /*init_fn*/ transform_mode_snap_source_init,
    /*transform_fn*/ snapsource_transform_fn,
    /*transform_matrix_fn*/ nullptr,
    /*handle_event_fn*/ snapsource_handle_event_fn,
    /*snap_distance_fn*/ nullptr,
    /*snap_apply_fn*/ nullptr,
    /*draw_fn*/ nullptr,
};
