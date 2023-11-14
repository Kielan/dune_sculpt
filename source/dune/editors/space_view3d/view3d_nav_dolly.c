#include "lib_math.h"

#include "dune_cxt.h"
#include "dune_report.h"

#include "graph.h"

#include "win_api.h"

#include "api_access.h"

#include "ed_screen.h"

#include "view3d_intern.h"
#include "view3d_nav.h" /* own include */

/* View Dolly Op
 * Like zoom but translates the view offset along the view direction
 * which avoids RgnView3D.dist approaching zero. */

/* This is an exact copy of viewzoom_modal_keymap. */
void viewdolly_modal_keymap(WinKeyConfig *keyconf)
{
  static const EnumPropItem modal_items[] = {
      {VIEW_MODAL_CONFIRM, "CONFIRM", 0, "Confirm", ""},

      {VIEWROT_MODAL_SWITCH_ROTATE, "SWITCH_TO_ROTATE", 0, "Switch to Rotate"},
      {VIEWROT_MODAL_SWITCH_MOVE, "SWITCH_TO_MOVE", 0, "Switch to Move"},

      {0, NULL, 0, NULL, NULL},
  };

  wmKeyMap *keymap = win_modalkeymap_find(keyconf, "View3D Dolly Modal");

  /* this fn is called for each spacetype, only needs to add map once */
  if (keymap && keymap->modal_items) {
    return;
  }

  keymap = win_modalkeymap_ensure(keyconf, "View3D Dolly Modal", modal_items);

  /* disabled mode switching for now, can re-implement better, later on */
#if 0
  win_modalkeymap_add_item(
      keymap, LEFTMOUSE, KM_RELEASE, KM_ANY, 0, KM_ANY, VIEWROT_MODAL_SWITCH_ROTATE);
  win_modalkeymap_add_item(
      keymap, LEFTCTRLKEY, KM_RELEASE, KM_ANY, 0, KM_ANY, VIEWROT_MODAL_SWITCH_ROTATE);
  win_modalkeymap_add_item(
      keymap, LEFTSHIFTKEY, KM_PRESS, KM_ANY, 0, KM_ANY, VIEWROT_MODAL_SWITCH_MOVE);
#endif

  /* assign map to ops */
  win_modalkeymap_assign(keymap, "VIEW3D_OT_dolly");
}

static bool viewdolly_offset_lock_check(Cxt *C, WinOp *op)
{
  View3D *v3d = cxt_win_view3d(C);
  RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  if (ed_view3d_offset_lock_check(v3d, rv3d)) {
    dune_report(op->reports, RPT_WARNING, "Cannot dolly when the view offset is locked");
    return true;
  }
  return false;
}

static void view_dolly_to_vector_3d(ARgn *rgn,
                                    const float orig_ofs[3],
                                    const float dvec[3],
                                    float dfac)
{
  RgnView3D *rv3d = rgn->rgndata;
  madd_v3_v3v3fl(rv3d->ofs, orig_ofs, dvec, -(1.0f - dfac));
}

static void viewdolly_apply(ViewOpsData *vod, const int xy[2], const bool zoom_invert)
{
  float zfac = 1.0;

  {
    float len1, len2;

    if (U.uiflag & USER_ZOOM_HORIZ) {
      len1 = (vod->rgn->winrct.xmax - xy[0]) + 5;
      len2 = (vod->rgn->winrct.xmax - vod->init.ev_xy[0]) + 5;
    }
    else {
      len1 = (vod->rgn->winrct.ymax - xy[1]) + 5;
      len2 = (vod->rgn->winrct.ymax - vod->init.ev_xy[1]) + 5;
    }
    if (zoom_invert) {
      SWAP(float, len1, len2);
    }

    zfac = 1.0f + ((len1 - len2) * 0.01f * vod->rv3d->dist);
  }

  if (zfac != 1.0f) {
    view_dolly_to_vector_3d(vod->rgn, vod->init.ofs, vod->init.mousevec, zfac);
  }

  if (RV3D_LOCK_FLAGS(vod->rv3d) & RV3D_BOXVIEW) {
    view3d_boxview_sync(vod->area, vod->rgn);
  }

  ed_view3d_camera_lock_sync(vod->graph, vod->v3d, vod->rv3d);

  ed_rgn_tag_redraw(vod->rgn);
}

static int viewdolly_modal(Cxt *C, WinOp *op, const WinEv *ev)
{
  ViewOpsData *vod = op->customdata;
  short ev_code = VIEW_PASS;
  bool use_autokey = false;
  int ret = OP_RUNNING_MODAL;

  /* ex the events */
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
        event_code = VIEW_CONFIRM;
        break;
      case VIEWROT_MODAL_SWITCH_ROTATE:
        win_op_name_call(C, "VIEW3D_OT_rotate", WIN_OP_INVOKE_DEFAULT, NULL, ev);
        ev_code = VIEW_CONFIRM;
        break;
    }
  }
  else if (ev->type == vod->init.ev_type && ev->val == KM_RELEASE) {
    ev_code = VIEW_CONFIRM;
  }

  if (ev_code == VIEW_APPLY) {
    viewdolly_apply(vod, ev->xy, (U.uiflag & USER_ZOOM_INVERT) != 0);
    if (ed_screen_anim_playing(cxt_wm(C))) {
      use_autokey = true;
    }
  }
  else if (ev_code == VIEW_CONFIRM) {
    use_autokey = true;
    ret = OP_FINISHED;
  }

  if (use_autokey) {
    ed_view3d_camera_lock_autokey(vod->v3d, vod->rv3d, C, false, true);
  }

  if (ret & OP_FINISHED) {
    viewops_data_free(C, vod);
    op->customdata = NULL;
  }

  return ret;
}

static int viewdolly_ex(Cxt *C, WinOp *op)
{
  View3D *v3d;
  RgnView3D *rv3d;
  ScrArea *area;
  ARgn *rgn;
  float mousevec[3];

  const int delta = api_int_get(op->ptr, "delta");

  if (op->customdata) {
    ViewOpsData *vod = op->customdata;

    area = vod->area;
    rgn = vod->rgn;
    copy_v3_v3(mousevec, vod->init.mousevec);
  }
  else {
    area = cxt_win_area(C);
    rgn = cxt_win_rgn(C);
    negate_v3_v3(mousevec, ((RgnView3D *)rgn->rgndata)->viewinv[2]);
    normalize_v3(mousevec);
  }

  v3d = area->spacedata.first;
  rv3d = rgn->rgndata;

  const bool use_cursor_init = api_bool_get(op->ptr, "use_cursor_init");

  /* overwrite the mouse vector with the view direction (zoom into the center) */
  if ((use_cursor_init && (U.uiflag & USER_ZOOM_TO_MOUSEPOS)) == 0) {
    normalize_v3_v3(mousevec, rv3d->viewinv[2]);
    negate_v3(mousevec);
  }

  view_dolly_to_vector_3d(region, rv3d->ofs, mousevec, delta < 0 ? 1.8f : 0.2f);

  if (RV3D_LOCK_FLAGS(rv3d) & RV3D_BOXVIEW) {
    view3d_boxview_sync(area, rgn);
  }

  ed_view3d_camera_lock_sync(cxt_data_ensure_evaluated_depsgraph(C), v3d, rv3d);

  ed_rgn_tag_redraw(rgn);

  viewops_data_free(C, op->customdata);
  op->customdata = NULL;

  return OP_FINISHED;
}

/* copied from viewzoom_invoke(), changes here may apply there */
static int viewdolly_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  ViewOpsData *vod;

  if (viewdolly_offset_lock_check(C, op)) {
    return OP_CANCELLED;
  }

  const bool use_cursor_init = api_bool_get(op->ptr, "use_cursor_init");

  vod = op->customdata = viewops_data_create(
      C,
      ev,
      (viewops_flag_from_prefs() & ~VIEWOPS_FLAG_ORBIT_SEL) |
          (use_cursor_init ? VIEWOPS_FLAG_USE_MOUSE_INIT : 0));

  ed_view3d_smooth_view_force_finish(C, vod->v3d, vod->rhn);

  /* needs to run before 'viewops_data_create' so the backup 'rv3d->ofs' is correct */
  /* switch from camera view when: */
  if (vod->rv3d->persp != RV3D_PERSP) {
    if (vod->rv3d->persp == RV3D_CAMOB) {
      /* ignore rv3d->lpersp because dolly only makes sense in perspective mode */
      const Graph *graph = cxt_data_ensure_eval_graph(C);
      ed_view3d_persp_switch_from_camera(graph, vod->v3d, vod->rv3d, RV3D_PERSP);
    }
    else {
      vod->rv3d->persp = RV3D_PERSP;
    }
    ed_rgn_tag_redraw(vod->rgn);
  }

  /* if one or the other zoom position aren't set, set from ev */
  if (!api_struct_prop_is_set(op->ptr, "mx") || !apu_struct_prop_is_set(op->ptr, "my")) {
    api_int_set(op->ptr, "mx", ev->xy[0]);
    api_int_set(op->ptr, "my", ev->xy[1]);
  }

  if (api_struct_prop_is_set(op->ptr, "delta")) {
    viewdolly_ex(C, op);
  }
  else {
    /* overwrite the mouse vector with the view direction (zoom into the center) */
    if ((use_cursor_init && (U.uiflag & USER_ZOOM_TO_MOUSEPOS)) == 0) {
      negate_v3_v3(vod->init.mousevec, vod->rv3d->viewinv[2]);
      normalize_v3(vod->init.mousevec);
    }

    if (ev->type == MOUSEZOOM) {
      /* Bypass Zoom invert flag for track pads (pass false always) */

      if (U.uiflag & USER_ZOOM_HORIZ) {
        vod->init.ev_xy[0] = vod->prev.ev_xy[0] = ev->xy[0];
      }
      else {
        /* Set y move = x move as MOUSEZOOM uses only x axis to pass magnification value */
        vod->init.ev_xy[1] = vod->prev.ev_xy[1] = vod->init.ev_xy[1] + ev->xy[0] -
                                                        ev->prev_xy[0];
      }
      viewdolly_apply(vod, ev->prev_xy, (U.uiflag & USER_ZOOM_INVERT) == 0);

      viewops_data_free(C, op->customdata);
      op->customdata = NULL;
      return OP_FINISHED;
    }

    /* add temp handler */
    win_ev_add_modal_handler(C, op);
    return OP_RUNNING_MODAL;
  }
  return OP_FINISHED;
}

static void viewdolly_cancel(Cxt *C, WinOp *op)
{
  viewops_data_free(C, op->customdata);
  op->customdata = NULL;
}

void VIEW3D_OT_dolly(WinOpType *ot)
{
  /* ids */
  ot->name = "Dolly View";
  ot->description = "Dolly in/out in the view";
  ot->idname = "VIEW3D_OT_dolly";

  /* api callbacks */
  ot->invoke = viewdolly_invoke;
  ot->ex = viewdolly_ex;
  ot->modal = viewdolly_modal;
  ot->poll = view3d_rotation_poll;
  ot->cancel = viewdolly_cancel;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_GRAB_CURSOR_XY | OPTYPE_DEPENDS_ON_CURSOR;

  /* props */
  view3d_op_props_common(
      ot, V3D_OP_PROP_DELTA | V3D_OP_PROP_MOUSE_CO | V3D_OP_PROP_USE_MOUSE_INIT);
}
