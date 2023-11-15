#include "dune_cxt.h"

#include "win_api.h"

#include "api_access.h"
#include "api_define.h"

#include "ed_screen.h"

#include "view3d_intern.h"
#include "view3d_nav.h" /* own include */

/* View Move (Pan) Op **/
/* These defines are saved in keymap files, do not change vals but just add new ones */

void viewmove_modal_keymap(WinKeyConfig *keyconf)
{
  static const EnumPropItem modal_items[] = {
      {VIEW_MODAL_CONFIRM, "CONFIRM", 0, "Confirm", ""},

      {VIEWROT_MODAL_SWITCH_ZOOM, "SWITCH_TO_ZOOM", 0, "Switch to Zoom"},
      {VIEWROT_MODAL_SWITCH_ROTATE, "SWITCH_TO_ROTATE", 0, "Switch to Rotate"},

      {0, NULL, 0, NULL, NULL},
  };

  WinKeyMap *keymap = win_modalkeymap_find(keyconf, "View3D Move Modal");

  /* this function is called for each spacetype, only needs to add map once */
  if (keymap && keymap->modal_items) {
    return;
  }

  keymap = win_modalkeymap_ensure(keyconf, "View3D Move Modal", modal_items);

  /* items for modal map */
  win_modalkeymap_add_item(keymap, MIDDLEMOUSE, KM_RELEASE, KM_ANY, 0, KM_ANY, VIEW_MODAL_CONFIRM);
  win_modalkeymap_add_item(keymap, EV_ESCKEY, KM_PRESS, KM_ANY, 0, KM_ANY, VIEW_MODAL_CONFIRM);

  /* disabled mode switching for now, can re-implement better, later on */
#if 0
  win_modalkeymap_add_item(keymap, LEFTMOUSE, KM_PRESS, KM_ANY, 0, VIEWROT_MODAL_SWITCH_ZOOM);
  win_modalkeymap_add_item(keymap, LEFTCTRLKEY, KM_PRESS, KM_ANY, 0, VIEWROT_MODAL_SWITCH_ZOOM);
  win_modalkeymap_add_item(
      keymap, LEFTSHIFTKEY, KM_RELEASE, KM_ANY, 0, VIEWROT_MODAL_SWITCH_ROTATE);
#endif

  /* assign map to operators */
  WM_modalkeymap_assign(keymap, "VIEW3D_OT_move");
}

static int viewmove_modal(Cxt *C, WinOp *op, const WinEv *ev)
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
      case VIEWROT_MODAL_SWITCH_ZOOM:
        win_op_name_call(C, "VIEW3D_OT_zoom", WIN_OP_INVOKE_DEFAULT, NULL, ev);
        ev_code = VIEW_CONFIRM;
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
    viewmove_apply(vod, ev->xy[0], ev->xy[1]);
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
    viewops_data_free(C, op->customdata);
    op->customdata = NULL;
  }

  return ret;
}

static int viewmove_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  ViewOpsData *vod;

  const bool use_cursor_init = api_bool_get(op->ptr, "use_cursor_init");

  vod = op->customdata = viewops_data_create(
      C,
      ev,
      (viewops_flag_from_prefs() & ~VIEWOPS_FLAG_ORBIT_SEL) |
          (use_cursor_init ? VIEWOPS_FLAG_USE_MOUSE_INIT : 0));
  vod = op->customdata;

  ed_view3d_smooth_view_force_finish(C, vod->v3d, vod->region);

  if (ev->type == MOUSEPAN) {
    /* invert it, trackpad scroll follows same principle as 2d wins this way */
    viewmove_apply(
        vod, 2 * ev->xy[0] - ev->prev_xy[0], 2 * ev->xy[1] - ev->prev_xy[1]);

    viewops_data_free(C, op->customdata);
    op->customdata = NULL;

    return OP_FINISHED;
  }

  /* add tmp handler */
  win_ev_add_modal_handler(C, op);

  return OP_RUNNING_MODAL;
}

static void viewmove_cancel(Cxt *C, WinOp *op)
{
  viewops_data_free(C, op->customdata);
  op->customdata = NULL;
}

void VIEW3D_OT_move(WinOpType *ot)
{

  /* ids */
  ot->name = "Pan View";
  ot->description = "Move the view";
  ot->idname = "VIEW3D_OT_move";

  /* api cbs */
  ot->invoke = viewmove_invoke;
  ot->modal = viewmove_modal;
  ot->poll = view3d_location_poll;
  ot->cancel = viewmove_cancel;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_GRAB_CURSOR_XY;

  /* props */
  view3d_op_props_common(ot, V3D_OP_PROP_USE_MOUSE_INIT);
}
