#include "lib_dunelib.h"
#include "lib_dial_2d.h"
#include "lib_math.h"

#include "NODE_context.h"

#include "win_api.h"

#include "api_access.h"
#include "api_define.h"

#include "ed_screen.h"

#include "view3d_intern.h"
#include "view3d_nav.h" /* own include */

/* View Roll Op */
/* param use_axis_view: When true, keep axis-aligned orthographic views
 * (when rotating in 90 degree increments). While this may seem obscure some NDOF
 * devices have key shortcuts to do this (see NDOF_BTN_ROLL_CW & NDOF_BTN_ROLL_CCW). */
static void view_roll_angle(ARgn *rgn,
                            float quat[4],
                            const float orig_quat[4],
                            const float dvec[3],
                            float angle,
                            bool use_axis_view)
{
  RgnView3D *rv3d = rgn->rgndata;
  float quat_mul[4];

  /* camera axis */
  axis_angle_normalized_to_quat(quat_mul, dvec, angle);

  mul_qt_qtqt(quat, orig_quat, quat_mul);

  /* avoid precision loss over time */
  normalize_qt(quat);

  if (use_axis_view && RV3D_VIEW_IS_AXIS(rv3d->view) && (fabsf(angle) == (float)M_PI_2)) {
    if (ed_view3d_quat_to_axis_view(quat, 0.01f, &rv3d->view, &rv3d->view_axis_roll)) {
      if (rv3d->view != RV3D_VIEW_USER) {
        ed_view3d_quat_from_axis_view(rv3d->view, rv3d->view_axis_roll, quat_mul);
      }
    }
  }
  else {
    rv3d->view = RV3D_VIEW_USER;
  }
}

static void viewroll_apply(ViewOpsData *vod, int x, int y)
{
  float angle = lib_dial_angle(vod->init.dial, (const float[2]){x, y});

  if (angle != 0.0f) {
    view_roll_angle(
        vod->rgn, vod->rv3d->viewquat, vod->init.quat, vod->init.mousevec, angle, false);
  }

  if (vod->use_dyn_ofs) {
    view3d_orbit_apply_dyn_ofs(
        vod->rv3d->ofs, vod->init.ofs, vod->init.quat, vod->rv3d->viewquat, vod->dyn_ofs);
  }

  if (RV3D_LOCK_FLAGS(vod->rv3d) & RV3D_BOXVIEW) {
    view3d_boxview_sync(vod->area, vod->rgn);
  }

  ed_view3d_camera_lock_sync(vod->graph, vod->v3d, vod->rv3d);

  ed_rgn_tag_redraw(vod->rgn);
}

static int viewroll_modal(Cxt *C, WinOp *op, const WinEv *ev)
{
  ViewOpsData *vod = op->customdata;
  short ev_code = VIEW_PASS;
  bool use_autokey = false;
  int ret = OP_RUNNING_MODAL;

  /* ex the evs */
  if (ev->type == MOUSEMOVE) {
    ev_code = VIEW_APPLY;
  }
  else if (ev->type == EV_MODAL_MAP) {
    switch (ev->val) {
      case VIEW_MODAL_CONFIRM:
        ev_code = VIEW_CONFIRM;
        break;
      case VIEWROT_MODAL_SWITCH_MOVE:
        win_op_name_call(C, "VIEW3D_OT_move", WIN_OP_INVOKE_DEFAULT, NULL, ev);
        ev_code = VIEW_CONFIRM;
        break;
      case VIEWROT_MODAL_SWITCH_ROTATE:
        win_op_name_call(C, "VIEW3D_OT_rotate", WIN_OP_INVOKE_DEFAULT, NULL, ev);
        ev_code = VIEW_CONFIRM;
        break;
    }
  }
  else if (ELEM(ev->type, EV_ESCKEY, RIGHTMOUSE)) {
    /* Note this does not remove auto-keys on locked cameras. */
    copy_qt_qt(vod->rv3d->viewquat, vod->init.quat);
    ed_view3d_camera_lock_sync(vod->graph, vod->v3d, vod->rv3d);
    viewops_data_free(C, op->customdata);
    op->customdata = NULL;
    return OP_CANCELLED;
  }
  else if (ev->type == vod->init.ev_type && ev->val == KM_RELEASE) {
    ev_code = VIEW_CONFIRM;
  }

  if (ev_code == VIEW_APPLY) {
    viewroll_apply(vod, ev->xy[0], ev->xy[1]);
    if (ed_screen_anim_playing(cxt_wm(C))) {
      use_autokey = true;
    }
  }
  else if (ev_code == VIEW_CONFIRM) {
    use_autokey = true;
    ret = OPERATOR_FINISHED;
  }

  if (use_autokey) {
    ed_view3d_camera_lock_autokey(vod->v3d, vod->rv3d, C, true, false);
  }

  if (ret & OP_FINISHED) {
    viewops_data_free(C, op->customdata);
    op->customdata = NULL;
  }

  return ret;
}

enum {
  V3D_VIEW_STEPLEFT = 1,
  V3D_VIEW_STEPRIGHT,
};

static const EnumPropItem prop_view_roll_items[] = {
    {0, "ANGLE", 0, "Roll Angle", "Roll the view using an angle value"},
    {V3D_VIEW_STEPLEFT, "LEFT", 0, "Roll Left", "Roll the view around to the left"},
    {V3D_VIEW_STEPRIGHT, "RIGHT", 0, "Roll Right", "Roll the view around to the right"},
    {0, NULL, 0, NULL, NULL},
};

static int viewroll_ex(Cxt *C, WinOp *op)
{
  View3D *v3d;
  RgnView3D *rv3d;
  ARgn *rgn;

  if (op->customdata) {
    ViewOpsData *vod = op->customdata;
    rgn = vod->rgn;
    v3d = vod->v3d;
  }
  else {
    ed_view3d_cxt_user_rgn(C, &v3d, &rgn);
  }

  rv3d = rgn->rgndata;
  if ((rv3d->persp != RV3D_CAMOB) || ed_view3d_camera_lock_check(v3d, rv3d)) {

    ed_view3d_smooth_view_force_finish(C, v3d, rgn);

    int type = api_enum_get(op->ptr, "type");
    float angle = (type == 0) ? api_float_get(op->ptr, "angle") : DEG2RADF(U.pad_rot_angle);
    float mousevec[3];
    float quat_new[4];

    const int smooth_viewtx = win_op_smooth_viewtx_get(op);

    if (type == V3D_VIEW_STEPLEFT) {
      angle = -angle;
    }

    normalize_v3_v3(mousevec, rv3d->viewinv[2]);
    negate_v3(mousevec);
    view_roll_angle(rgn, quat_new, rv3d->viewquat, mousevec, angle, true);

    const float *dyn_ofs_pt = NULL;
    float dyn_ofs[3];
    if (U.uiflag & USER_ORBIT_SEL) {
      if (view3d_orbit_calc_center(C, dyn_ofs)) {
        negate_v3(dyn_ofs);
        dyn_ofs_pt = dyn_ofs;
      }
    }

    ed_view3d_smooth_view(C,
                          v3d,
                          rgn,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .quat = quat_new,
                              .dyn_ofs = dyn_ofs_pt,
                          });

    viewops_data_free(C, op->customdata);
    op->customdata = NULL;
    return OP_FINISHED;
  }

  viewops_data_free(C, op->customdata);
  op->customdata = NULL;
  return OP_CANCELLED;
}

static int viewroll_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  ViewOpsData *vod;

  bool use_angle = api_enum_get(op->ptr, "type") != 0;

  if (use_angle || api_struct_prop_is_set(op->ptr, "angle")) {
    viewroll_ex(C, op);
  }
  else {
    /* makes op->customdata */
    vod = op->customdata = viewops_data_create(C, ev, viewops_flag_from_prefs());
    vod->init.dial = lib_dial_init((const float[2]){lib_rcti_cent_x(&vod->region->winrct),
                                                    lib_rcti_cent_y(&vod->region->winrct)},
                                   FLT_EPSILON);

    ed_view3d_smooth_view_force_finish(C, vod->v3d, vod->rgn);

    /* overwrite the mouse vector with the view direction */
    normalize_v3_v3(vod->init.mousevec, vod->rv3d->viewinv[2]);
    negate_v3(vod->init.mousevec);

    if (ev->type == MOUSEROTATE) {
      vod->init.ev_xy[0] = vod->prev.ev_xy[0] = ev->xy[0];
      viewroll_apply(vod, ev->prev_xy[0], ev->prev_xy[1]);

      viewops_data_free(C, op->customdata);
      op->customdata = NULL;
      return OP_FINISHED;
    }

    /* add tmp handler */
    win_ev_add_modal_handler(C, op);
    return OP_RUNNING_MODAL;
  }
  return OP_FINISHED;
}

static void viewroll_cancel(Cxt *C, WinOp *op)
{
  viewops_data_free(C, op->customdata);
  op->customdata = NULL;
}

void VIEW3D_OT_view_roll(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "View Roll";
  ot->description = "Roll the view";
  ot->idname = "VIEW3D_OT_view_roll";

  /* api cbs */
  ot->invoke = viewroll_invoke;
  ot->ex = viewroll_ex;
  ot->modal = viewroll_modal;
  ot->poll = ed_op_rv3d_user_rgn_poll;
  ot->cancel = viewroll_cancel;

  /* flags */
  ot->flag = 0;

  /* props */
  ot->prop = prop = api_def_float(
      ot->sapi, "angle", 0, -FLT_MAX, FLT_MAX, "Roll", "", -FLT_MAX, FLT_MAX);
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_enum(ot->sapi,
                      "type",
                      prop_view_roll_items,
                      0,
                      "Roll Angle Source",
                      "How roll angle is calculated");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}
