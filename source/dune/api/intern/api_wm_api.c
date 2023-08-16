#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "lib_utildefines.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "types_screen.h"
#include "types_space.h"
#include "types_windowmanager.h"

#include "ui.h"

#include "wm_cursors.h"
#include "wm_event_types.h"

#include "wm_api.h"
#include "wm_types.h"

#include "api_internal.h" /* own include */

/* confusing 2 enums mixed up here */
const EnumPropItem api_enum_window_cursor_items[] = {
    {WM_CURSOR_DEFAULT, "DEFAULT", 0, "Default", ""},
    {WM_CURSOR_NONE, "NONE", 0, "None", ""},
    {WM_CURSOR_WAIT, "WAIT", 0, "Wait", ""},
    {WM_CURSOR_EDIT, "CROSSHAIR", 0, "Crosshair", ""},
    {WM_CURSOR_X_MOVE, "MOVE_X", 0, "Move-X", ""},
    {WM_CURSOR_Y_MOVE, "MOVE_Y", 0, "Move-Y", ""},

    /* new */
    {WM_CURSOR_KNIFE, "KNIFE", 0, "Knife", ""},
    {WM_CURSOR_TEXT_EDIT, "TEXT", 0, "Text", ""},
    {WM_CURSOR_PAINT_BRUSH, "PAINT_BRUSH", 0, "Paint Brush", ""},
    {WM_CURSOR_PAINT, "PAINT_CROSS", 0, "Paint Cross", ""},
    {WM_CURSOR_DOT, "DOT", 0, "Dot Cursor", ""},
    {WM_CURSOR_ERASER, "ERASER", 0, "Eraser", ""},
    {WM_CURSOR_HAND, "HAND", 0, "Hand", ""},
    {WM_CURSOR_EW_SCROLL, "SCROLL_X", 0, "Scroll-X", ""},
    {WM_CURSOR_NS_SCROLL, "SCROLL_Y", 0, "Scroll-Y", ""},
    {WM_CURSOR_NSEW_SCROLL, "SCROLL_XY", 0, "Scroll-XY", ""},
    {WM_CURSOR_EYEDROPPER, "EYEDROPPER", 0, "Eyedropper", ""},
    {WM_CURSOR_PICK_AREA, "PICK_AREA", 0, "Pick Area", ""},
    {WM_CURSOR_STOP, "STOP", 0, "Stop", ""},
    {WM_CURSOR_COPY, "COPY", 0, "Copy", ""},
    {WM_CURSOR_CROSS, "CROSS", 0, "Cross", ""},
    {WM_CURSOR_MUTE, "MUTE", 0, "Mute", ""},
    {WM_CURSOR_ZOOM_IN, "ZOOM_IN", 0, "Zoom In", ""},
    {WM_CURSOR_ZOOM_OUT, "ZOOM_OUT", 0, "Zoom Out", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "dune_cxt.h"
#  include "dune_undo_system.h"

#  include "wm_types.h"

/* Needed since api doesn't use `const` in function signatures. */
static bool api_KeyMapItem_compare(struct wmKeyMapItem *k1, struct wmKeyMapItem *k2)
{
  return wm_keymap_item_compare(k1, k2);
}

static void api_KeyMapItem_to_string(wmKeyMapItem *kmi, bool compact, char *result)
{
  wm_keymap_item_to_string(kmi, compact, result, UI_MAX_SHORTCUT_STR);
}

static wmKeyMap *api_keymap_active(wmKeyMap *km, Ctx *C)
{
  wmWindowManager *wm = cxt_wm_manager(C);
  return wm_keymap_active(wm, km);
}

static void api_keymap_restore_to_default(wmKeyMap *km, Cxt *C)
{
  wm_keymap_restore_to_default(km, cxt_wm_manager(C));
}

static void api_keymap_restore_item_to_default(wmKeyMap *km, Cxt *C, wmKeyMapItem *kmi)
{
  wm_keymap_item_restore_to_default(cxt_wm_manager(C), km, kmi);
}

static void api_op_report(wmOp *op, int type, const char *msg)
{
  dune_report(op->reports, type, msg);
}

static bool api_op_is_repeat(wmOp *op, Cxt *C)
{
  return wm_op_is_repeat(C, op);
}

/* since event isn't needed... */
static void api_op_enum_search_invoke(Cxt *C, wmOp *op)
{
  wm_enum_search_invoke(C, op, NULL);
}

static bool api_event_modal_handler_add(struct Cxt *C, struct wmOp *op)
{
  return wm_event_add_modal_handler(C, op) != NULL;
}

/* XXX, need a way for python to know event types, 0x0110 is hard coded */
static wmTimer *api_event_timer_add(struct wmWindowManager *wm, float time_step, wmWindow *win)
{
  return wm_event_add_timer(wm, win, 0x0110, time_step);
}

static void api_event_timer_remove(struct wmWindowManager *wm, wmTimer *timer)
{
  wm_event_remove_timer(wm, timer->win, timer);
}

static wmGizmoGroupType *wm_gizmogrouptype_find_for_add_remove(ReportList *reports,
                                                               const char *idname)
{
  wmGizmoGroupType *gzgt = wm_gizmogrouptype_find(idname, true);
  if (gzgt == NULL) {
    dune_reportf(reports, RPT_ERROR, "Gizmo group type '%s' not found!", idname);
    return NULL;
  }
  if (gzgt->flag & WM_GIZMOGROUPTYPE_PERSISTENT) {
    dube_reportf(reports, RPT_ERROR, "Gizmo group '%s' has 'PERSISTENT' option set!", idname);
    return NULL;
  }
  return gzgt;
}

static void api_gizmo_group_type_ensure(ReportList *reports, const char *idname)
{
  wmGizmoGroupType *gzgt = wm_gizmogrouptype_find_for_add_remove(reports, idname);
  if (gzgt != NULL) {
    wm_gizmo_group_type_ensure_ptr(gzgt);
  }
}

static void api_gizmo_group_type_unlink_delayed(ReportList *reports, const char *idname)
{
  wmGizmoGroupType *gzgt = wm_gizmogrouptype_find_for_add_remove(reports, idname);
  if (gzgt != NULL) {
    wm_gizmo_group_type_unlink_delayed_ptr(gzgt);
  }
}

/* Placeholder data for final implementation of a true progress-bar. */
static struct wmStaticProgress {
  float min;
  float max;
  bool is_valid;
} wm_progress_state = {0, 0, false};

static void api_progress_begin(struct wmWindowManager *UNUSED(wm), float min, float max)
{
  float range = max - min;
  if (range != 0) {
    wm_progress_state.min = min;
    wm_progress_state.max = max;
    wm_progress_state.is_valid = true;
  } else {
    wm_progress_state.is_valid = false;
  }
}

static void api_progress_update(struct wmWindowManager *wm, float value)
{
  if (wm_progress_state.is_valid) {
    /* Map to cursor_time range [0,9999] */
    wmWindow *win = wm->winactive;
    if (win) {
      int val = (int)(10000 * (value - wm_progress_state.min) /
                      (wm_progress_state.max - wm_progress_state.min));
      wm_cursor_time(win, val);
    }
  }
}

static void api_progress_end(struct wmWindowManager *wm)
{
  if (wm_progress_state.is_valid) {
    wmWindow *win = wm->winactive;
    if (win) {
      wm_cursor_modal_restore(win);
      wm_progress_state.is_valid = false;
    }
  }
}

/* wrap these because of 'const wmEvent *' */
static int api_op_confirm(Cxt *C, wmOp *op, wmEvent *event)
{
  return wm_op_confirm(C, op, event);
}
static int api_op_props_popup(Cxt *C, wmOp *op, wmEvent *event)
{
  return wm_op_props_popup(C, op, event);
}

static int keymap_item_mod_flag_from_args(bool any, int shift, int ctrl, int alt, int oskey)
{
  int mod = 0;
  if (any) {
    mod = KM_ANY;
  }
  else {
    if (shift == KM_MOD_HELD) {
      mod |= KM_SHIFT;
    } else if (shift == KM_ANY) {
      mod |= KM_SHIFT_ANY;
    }

    if (ctrl == KM_MOD_HELD) {
      mod |= KM_CTRL;
    } else if (ctrl == KM_ANY) {
      mod |= KM_CTRL_ANY;
    }

    if (alt == KM_MOD_HELD) {
      mod |= KM_ALT;
    } else if (alt == KM_ANY) {
      mod |= KM_ALT_ANY;
    }

    if (oskey == KM_MOD_HELD) {
      mod |= KM_OSKEY;
    } else if (oskey == KM_ANY) {
      mod |= KM_OSKEY_ANY;
    }
  }
  return mod;
}

static wmKeyMapItem *api_KeyMap_item_new(wmKeyMap *km,
                                         ReportList *reports,
                                         const char *idname,
                                         int type,
                                         int value,
                                         bool any,
                                         int shift,
                                         int ctrl,
                                         int alt,
                                         int oskey,
                                         int keymod,
                                         int direction,
                                         bool repeat,
                                         bool head)
{
  /* only on non-modal maps */
  if (km->flag & KEYMAP_MODAL) {
    dune_report(reports, RPT_ERROR, "Not a non-modal keymap");
    return NULL;
  }

  // wmWindowManager *wm = cxt_wm_manager(C);
  wmKeyMapItem *kmi = NULL;
  char idname_bl[OP_MAX_TYPENAME];
  const int mod = keymap_item_mod_flag_from_args(any, shift, ctrl, alt, oskey);

  wm_op_bl_idname(idname_bl, idname);

  /* create keymap item */
  kmi = wm_keymap_add_item(km,
                           idname_bl,
                           &(const KeyMapItem_Params){
                               .type = type,
                               .value = value,
                               .mod = mod,
                               .keymod = keymod,
                               .direction = direction,
                           });

  if (!repeat) {
    kmi->flag |= KMI_REPEAT_IGNORE;
  }

  /* #32437 allow scripts to define hotkeys that get added to start of keymap
   *          so that they stand a chance against catch-all defines later on*/
  if (head) {
    lib_remlink(&km->items, kmi);
    lib_addhead(&km->items, kmi);
  }

  return kmi;
}

static wmKeyMapItem *api_KeyMap_item_new_from_item(wmKeyMap *km,
                                                   ReportList *reports,
                                                   wmKeyMapItem *kmi_src,
                                                   bool head)
{
  // wmWindowManager *wm = cxt_wm_manager(C);

  if ((km->flag & KEYMAP_MODAL) == (kmi_src->idname[0] != '\0')) {
    dune_report(reports, RPT_ERROR, "Can not mix modal/non-modal items");
    return NULL;
  }

  /* create keymap item */
  wmKeyMapItem *kmi = wm_keymap_add_item_copy(km, kmi_src);
  if (head) {
    lib_remlink(&km->items, kmi);
    lib_addhead(&km->items, kmi);
  }
  return kmi;
}

static wmKeyMapItem *api_KeyMap_item_new_modal(wmKeyMap *km,
                                               ReportList *reports,
                                               const char *propvalue_str,
                                               int type,
                                               int value,
                                               bool any,
                                               int shift,
                                               int ctrl,
                                               int alt,
                                               int oskey,
                                               int keymod,
                                               int direction,
                                               bool repeat)
{
  /* only modal maps */
  if ((km->flag & KEYMAP_MODAL) == 0) {
    dune_report(reports, RPT_ERROR, "Not a modal keymap");
    return NULL;
  }

  wmKeyMapItem *kmi = NULL;
  const int mod = keymap_item_mod_flag_from_args(any, shift, ctrl, alt, oskey);
  int propvalue = 0;

  KeyMapItem_Params params = {
      .type = type,
      .value = value,
      .mod = mod,
      .keymod = keymod,
      .direction = direction,
  };

  /* not initialized yet, do delayed lookup */
  if (!km->modal_items) {
    kmi = wm_modalkeymap_add_item_str(km, &params, propvalue_str);
  } else {
    if (api_enum_value_from_id(km->modal_items, propvalue_str, &propvalue) == 0) {
      dune_report(reports, RPT_WARNING, "Prop value not in enumeration");
    }
    kmi = wm_modalkeymap_add_item(km, &params, propvalue);
  }

  if (!repeat) {
    kmi->flag |= KMI_REPEAT_IGNORE;
  }

  return kmi;
}

static void api_KeyMap_item_remove(wmKeyMap *km, ReportList *reports, ApiPtr *kmi_ptr)
{
  wmKeyMapItem *kmi = kmi_ptr->data;

  if (wm_keymap_remove_item(km, kmi) == false)
    dune_reportf(reports,
                RPT_ERROR,
                "KeyMapItem '%s' cannot be removed from '%s'",
                kmi->idname,
                km->idname);
    return;
  }

  API_PTR_INVALIDATE(kmi_ptr);
}

static ApiPtr api_KeyMap_item_find_from_op(Id *id,
                                           wmKeyMap *km,
                                           const char *idname,
                                           ApiPtr *props,
                                           int include_mask,
                                           int exclude_mask)
{
  char idname_bl[OP_MAX_TYPENAME];
  wn_op_bl_idname(idname_bl, idname);

  wmKeyMapItem *kmi = wm_key_event_op_from_keymap(
      km, idname_bl, props->data, include_mask, exclude_mask);
  ApiPtr kmi_ptr;
  api_ptr_create(id, &ApiKeyMapItem, kmi, &kmi_ptr);
  return kmi_ptr;
}

static ApiPtr api_KeyMap_item_match_event(Id *id, wmKeyMap *km, Cxt *C, wmEvent *event)
{
  wmKeyMapItem *kmi = wm_event_match_keymap_item(C, km, event);
  ApiPtr kmi_ptr;
  api_ptr_create(id, &ApiKeyMapItem, kmi, &kmi_ptr);
  return kmi_ptr;
}

static wmKeyMap *api_keymap_new(wmKeyConfig *keyconf,
                                ReportList *reports,
                                const char *idname,
                                int spaceid,
                                int regionid,
                                bool modal,
                                bool tool)
{
  if (modal) {
    /* Sanity check: Don't allow add-ons to override internal modal key-maps
     * because this isn't supported, the restriction can be removed when
     * add-ons can define modal key-maps.
     * Currently this is only useful for add-ons to override built-in modal keymaps
     * which is not the intended use for add-on keymaps. */
    wmWindowManager *wm = G_MAIN->wm.first;
    if (keyconf == wm->addonconf) {
      dune_reportf(reports, RPT_ERROR, "Modal key-maps not supported for add-on key-config");
      return NULL;
    }
  }

  wmKeyMap *keymap;

  if (modal == 0) {
    keymap = wm_keymap_ensure(keyconf, idname, spaceid, regionid);
  } else {
    keymap = wm_modalkeymap_ensure(keyconf, idname, NULL); /* items will be lazy init */
  }

  if (keymap && tool) {
    keymap->flag |= KEYMAP_TOOL;
  }

  return keymap;
}

static wmKeyMap *api_keymap_find(wmKeyConfig *keyconf,
                                 const char *idname,
                                 int spaceid,
                                 int regionid)
{
  return wm_keymap_list_find(&keyconf->keymaps, idname, spaceid, regionid);
}

static wmKeyMap *api_keymap_find_modal(wmKeyConfig *UNUSED(keyconf), const char *idname)
{
  wmOpType *ot = wm_optype_find(idname, 0);

  if (!ot) {
    return NULL;
  } else {
    return ot->modalkeymap;
  }
}

static void api_KeyMap_remove(wmKeyConfig *keyconfig, ReportList *reports, ApiPtr *keymap_ptr)
{
  wmKeyMap *keymap = keymap_ptr->data;

  if (wm_keymap_remove(keyconfig, keymap) == false) {
    dune_reportf(reports, RPT_ERROR, "KeyConfig '%s' cannot be removed", keymap->idname);
    return;
  }

  API_PTR_INVALIDATE(keymap_ptr);
}

static void api_KeyConfig_remove(wmWindowManager *wm, ReportList *reports, ApiPtr *keyconf_ptr)
{
  wmKeyConfig *keyconf = keyconf_ptr->data;

  if (wm_keyconfig_remove(wm, keyconf) == false) {
    dune_reportf(reports, RPT_ERROR, "KeyConfig '%s' cannot be removed", keyconf->idname);
    return;
  }

  API_PTR_INVALIDATE(keyconf_ptr);
}

static ApiPtr api_KeyConfig_find_item_from_op(wmWindowManager *wm,
                                              Cxt *C,
                                              const char *idname,
                                              int opctx,
                                              ApiPtr *props,
                                              int include_mask,
                                              int exclude_mask,
                                              ApiPtr *km_ptr)
{
  char idname_bl[OP_MAX_TYPENAME];
  wm_op_bl_idname(idname_bl, idname);

  wmKeyMap *km = NULL;
  wmKeyMapItem *kmi = wm_key_event_op(
      C, idname_bl, opctx, props->data, include_mask, exclude_mask, &km);
  ApiPtr kmi_ptr;
  api_ptr_create(&wm->id, &ApiKeyMap, km, km_ptr);
  api_ptr_create(&wm->id, &ApiKeyMapItem, kmi, &kmi_ptr);
  return kmi_ptr;
}

static void api_KeyConfig_update(wmWindowManager *wm)
{
  wm_keyconfig_update(wm);
}

/* popup menu wrapper */
static ApiPtr api_PopMenuBegin(Cxt *C, const char *title, int icon)
{
  ApiPtr r_ptr;
  void *data;

  data = (void *)ui_popup_menu_begin(C, title, icon);

  api_ptr_create(NULL, &ApiUIPopupMenu, data, &r_ptr);

  return r_ptr;
}

static void api_PopMenuEnd(Cxt *C, ApiPtr *handle)
{
  ui_popup_menu_end(C, handle->data);
}

/* popover wrapper */
static ApiPtr api_PopoverBegin(Cxt *C, int ui_units_x, bool from_active_btn)
{
  ApiPtr r_ptr;
  void *data;

  data = (void *)ui_popover_begin(C, U.widget_unit * ui_units_x, from_active_btn);

  api_ptr_create(NULL, &ApiUIPopover, data, &r_ptr);

  return r_ptr;
}

static void api_PopoverEnd(Cxt *C, ApiPtr *handle, wmKeyMap *keymap)
{
  ui_popover_end(C, handle->data, keymap);
}

/* pie menu wrapper */
static ApiPtr api_PieMenuBegin(Cxt *C, const char *title, int icon, ApiPtr *event)
{
  ApiPtr r_ptr;
  void *data;

  data = (void *)ui_pie_menu_begin(C, title, icon, event->data);

  api_ptr_create(NULL, &ApiUIPieMenu, data, &r_ptr);

  return r_ptr;
}

static void api_PieMenuEnd(Cxt *C, ApiPtr *handler)
{
  ui_pie_menu_end(C, handle->data);
}

static void api_WindowManager_print_undo_steps(wmWindowManager *wm)
{
  dune_undosys_print(wm->undo_stack);
}

static void api_WindowManager_tag_script_reload(void)
{
  wm_script_tag_reload();
  wm_main_add_notifier(NC_WINDOW, NULL);
}

static ApiPtr api_WindoManager_oper_props_last(const char *idname)
{
  wmOpType *ot = wm_optype_find(idname, true);

  if (ot != NULL) {
    ApiPtr ptr;
    wm_op_last_props_ensure(ot, &ptr);
    return ptr;
  }
  return ApiPtr_NULL;
}

static wmEvent *api_Window_event_add_simulate(wmWindow *win,
                                              ReportList *reports,
                                              int type,
                                              int value,
                                              const char *unicode,
                                              int x,
                                              int y,
                                              bool shift,
                                              bool ctrl,
                                              bool alt,
                                              bool oskey)
{
  if ((G.f & G_FLAG_EVENT_SIMULATE) == 0) {
    dune_report(reports, RPT_ERROR, "Not running with '--enable-event-simulate' enabled");
    return NULL;
  }

  if (!ELEM(value, KM_PRESS, KM_RELEASE, KM_NOTHING)) {
    dune_report(reports, RPT_ERROR, "Value: only 'PRESS/RELEASE/NOTHING' are supported");
    return NULL;
  }
  if (ISKEYBOARD(type) || ISMOUSE_BUTTON(type)) {
    if (!ELEM(value, KM_PRESS, KM_RELEASE)) {
      dune_report(reports, RPT_ERROR, "Value: must be 'PRESS/RELEASE' for keyboard/buttons");
      return NULL;
    }
  }
  if (ISMOUSE_MOTION(type)) {
    if (value != KM_NOTHING) {
      dune_report(reports, RPT_ERROR, "Value: must be 'NOTHING' for motion");
      return NULL;
    }
  }
  if (unicode != NULL) {
    if (value != KM_PRESS) {
      dune_report(reports, RPT_ERROR, "Value: must be 'PRESS' when unicode is set");
      return NULL;
    }
  }
  /* TODO: validate NDOF. */

  if (unicode != NULL) {
    int len = lib_str_utf8_size(unicode);
    if (len == -1 || unicode[len] != '\0') {
      dune_report(reports, RPT_ERROR, "Only a single character supported");
      return NULL;
    }
  }

  wmEvent e = *win->eventstate;
  e.type = type;
  e.val = value;
  e.flag = 0;
  e.xy[0] = x;
  e.xy[1] = y;

  e.mod = 0;
  if (shift) {
    e.mod |= KM_SHIFT;
  }
  if (ctrl) {
    e.mod |= KM_CTRL;
  }
  if (alt) {
    e.mod |= KM_ALT;
  }
  if (oskey) {
    e.mod |= KM_OSKEY;
  }

  e.utf8_buf[0] = '\0';
  if (unicode != NULL) {
    STRNCPY(e.utf8_buf, unicode);
  }

  /* Until we expose setting tablet values here. */
  wm_event_tablet_data_default_set(&e.tablet);

  return wm_event_add_simulate(win, &e);
}

#else

#  define WM_GEN_INVOKE_EVENT (1 << 0)
#  define WM_GEN_INVOKE_SIZE (1 << 1)
#  define WM_GEN_INVOKE_RETURN (1 << 2)

static void api_generic_op_invoke(ApiFn *fn, int flag)
{
  ApiProp *parm;

  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CTX);
  parm = api_def_ptr(fn, "op", "Op", "", "Op to call");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  if (flag & WM_GEN_INVOKE_EVENT) {
    parm = api_def_ptr(fn, "event", "Event", "", "Event");
    api_def_param_flags(parm, 0, PARM_REQUIRED);
  }

  if (flag & WM_GEN_INVOKE_SIZE) {
    api_def_int(fn, "width", 300, 0, INT_MAX, "", "Width of the popup", 0, INT_MAX);
  }

  if (flag & WM_GEN_INVOKE_RETURN) {
    parm = api_def_enum_flag(
        fn, "result", api_enum_op_return_items, OP_FINISHED, "result", "");
    api_def_fn_return(fn, parm);
  }
}

void api_window(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "cursor_warp", "wm_cursor_warp");
  parm = api_def_int(fn, "x", 0, INT_MIN, INT_MAX, "", "", INT_MIN, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "y", 0, INT_MIN, INT_MAX, "", "", INT_MIN, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_fn_ui_description(fn, "Set the cursor position");

  fn = api_def_fn(sapi, "cursor_set", "wm_cursor_set");
  parm = api_def_prop(fn, "cursor", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(parm, api_enum_window_cursor_items);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  pi_def_fn_ui_description(func, "Set the cursor");

  fn = api_def_fn(sapi, "cursor_modal_set", "wm_cursor_modal_set");
  parm = api_def_prop(fn, "cursor", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(parm, api_enum_window_cursor_items);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_fn_ui_description(fn, "Set the cursor, so the previous cursor can be restored");

  api_def_fn(sapi, "cursor_modal_restore", "wm_cursor_modal_restore");
  api_def_fn_ui_description(
      fn, "Restore the previous cursor after calling ``cursor_modal_set``");

  /* Arguments match 'rna_KeyMap_item_new'. */
  fn = api_def_fn(sapi, "event_simulate", "api_Window_event_add_simulate");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_enum(fn, "type", api_enum_event_type_items, 0, "Type", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn, "value", api_enum_event_value_items, 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "unicode", NULL, 0, "", "");
  api_def_param_clear_flags(parm, PROP_NEVER_NULL, 0);

  api_def_int(fn, "x", 0, INT_MIN, INT_MAX, "", "", INT_MIN, INT_MAX);
  api_def_int(fn, "y", 0, INT_MIN, INT_MAX, "", "", INT_MIN, INT_MAX);

  api_def_bool(fn, "shift", 0, "Shift", "");
  api_def_bool(fn, "ctrl", 0, "Ctrl", "");
  api_def_bool(fn, "alt", 0, "Alt", "");
  api_def_bool(fn, "oskey", 0, "OS Key", "");
  parm = api_def_ptr(fn, "event", "Event", "Item", "Added key map item");
  api_def_fn_return(fn, parm);
}

void api_wm(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "fileselect_add", "wm_event_add_fileselect");
  api_def_fn_ui_description(
      fn,
      "Opens a file selector with an operator. "
      "The string properties 'filepath', 'filename', 'directory' and a 'files' "
      "collection are assigned when present in the operator");
  api_generic_op_invoke(fn, 0);

  fn = api_def_fn(sapi, "modal_handler_add", "api_event_modal_handler_add");
  api_def_fn_ui_description(
      fn,
      "Add a modal handler to the window manager, for the given modal operator "
      "(called by invoke() with self, just before returning {'RUNNING_MODAL'})");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CTX);
  parm = api_def_ptr(fn, "operator", "Operator", "", "Op to call");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_fn_return(
      fn, api_def_bool(fn, "handle", 1, "", "Whether adding the handler was successful"));

  fn = api_def_fn(sapi, "event_timer_add", "rna_event_timer_add");
  api_def_fn_ui_description(
      fn, "Add a timer to the given window, to generate periodic 'TIMER' events");
  parm = api_def_prop(fn, "time_step", PROP_FLOAT, PROP_NONE);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_prop_range(parm, 0.0, FLT_MAX);
  api_def_prop_ui_text(parm, "Time Step", "Interval in seconds between timer events");
  api_def_ptr(fn, "window", "Window", "", "Window to attach the timer to, or None");
  parm = api_def_ptr(fn, "result", "Timer", "", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "event_timer_remove", "api_event_timer_remove"
  parm = api_def_ptr(fn, "timer", "Timer", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  fn = api_def_fn(sapi, "gizmo_group_type_ensure", "rna_gizmo_group_type_ensure");
  api_def_fn_ui_description(
      fn, "Activate an existing widget group (when the persistent option isn't set)");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_REPORTS);
  parm = api_def_string(fn, "identifier", NULL, 0, "", "Gizmo group type name");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(
      sapi, "gizmo_group_type_unlink_delayed", "api_gizmo_group_type_unlink_delayed");
  api_def_fn_ui_description(fn,
                            "Unlink a widget group (when the persistent option is set)");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_REPORTS);
  parm = api_def_string(fn, "identifier", NULL, 0, "", "Gizmo group type name");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* Progress bar interface */
  fn = api_def_fn(sapi, "progress_begin", "rna_progress_begin");
  api_def_fn_ui_description(fn, "Start progress report");
  parm = api_def_prop(fn, "min", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(parm, "min", "any value in range [0,9999]");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_prop(fn, "max", PROP_FLOAT, PROP_NONE);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_prop_ui_text(parm, "max", "any value in range [min+1,9998]");

  fn = api_def_fn(sapi, "progress_update", "api_progress_update");
  api_def_fn_ui_description(fn, "Update the progress feedback");
  parm = api_def_prop(fn, "value", PROP_FLOAT, PROP_NONE);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_prop_ui_text(
      parm, "value", "Any value between min and max as set in progress_begin()");

  fn = api_def_fn(sapi, "progress_end", "api_progress_end");
  api_def_fn_ui_description(fn, "Terminate progress report");

  /* invoke functions, for use with python */
  fn = api_def_fn(sapi, "invoke_props_popup", "api_op_props_popup");
  api_def_fn_ui_description(
      fn,
      "Op popup invoke "
      "(show op props and execute it automatically on changes)");
  api_generic_op_invoke(fn, WM_GEN_INVOKE_EVENT | WM_GEN_INVOKE_RETURN);

  /* invoked dialog opens popup with OK button, does not auto-exec operator. */
  fn = api_def_fn(sapi, "invoke_props_dialog", "wm_op_props_dialog_popup");
  api_def_fn_ui_description(
      fn,
      "Op dialog (non-autoexec popup) invoke "
      "(show op props and only execute it on click on OK button)");
  api_generic_op_invoke(fn, WM_GEN_INVOKE_SIZE | WM_GEN_INVOKE_RETURN);

  /* invoke enum */
  fn = api_def_fn(sapi, "invoke_search_popup", "rna_Operator_enum_search_invoke");
  api_def_fn_ui_description(
      func,
      "Operator search popup invoke which "
      "searches values of the operator's :class:`bpy.types.Operator.bl_property` "
      "(which must be an EnumProp), executing it on confirmation");
  api_generic_op_invoke(fn, 0);

  /* invoke functions, for use with python */
  fn = api_def_fn(sapi, "invoke_popup", "wm_op_ui_popup");
  api_def_fn_ui_description(fn,
                            "Op popup invoke "
                            "(only shows op's props, without executing it)");
  api_generic_op_invoke(fn, WM_GEN_INVOKE_SIZE | WM_GEN_INVOKE_RETURN);

  fn = api_def_fn(sapi, "invoke_confirm", "api_op_confirm");
  api_def_fn_ui_description(
      fn,
      "Op confirmation popup "
      "(only to let user confirm the execution, no op props shown)");
  api_generic_op_invoke(fn, WM_GEN_INVOKE_EVENT | WM_GEN_INVOKE_RETURN);

  /* wrap UI_popup_menu_begin */
  fn = api_def_fn(sapi, "popmenu_begin__internal", "api_PopMenuBegin");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  parm = api_def_string(fn, "title", NULL, 0, "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_prop(fn, "icon", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(parm, api_enum_icon_items);
  /* return */
  parm = api_def_ptr(fn, "menu", "UIPopupMenu", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_APIPTR);
  api_def_fn_return(fn, parm);

  /* wrap UI_popup_menu_end */
  fn = api_def_fn(sapi, "popmenu_end__internal", "api_PopMenuEnd");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  parm = api_def_ptr(fn, "menu", "UIPopupMenu", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_APIPTR | PARM_REQUIRED);

  /* wrap UI_popover_begin */
  sapi = api_def_fn(sapi, "popover_begin__internal", "api_PopoverBegin");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  api_def_prop(fn, "ui_units_x", PROP_INT, PROP_UNSIGNED);
  /* return */
  parm = api_def_ptr(fn, "menu", "UIPopover", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_APIPTR);
  api_def_fn_return(fn, parm);
  api_def_bool(
      fn, "from_active_button", 0, "Use Button", "Use the active button for positioning");

  /* wrap UI_popover_end */
  func = api_def_fn(sapi, "popover_end__internal", "api_PopoverEnd");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  parm = api_def_ptr(fn, "menu", "UIPopover", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_RNAPTR | PARM_REQUIRED);
  api_def_ptr(fn, "keymap", "KeyMap", "Key Map", "Active key map");

  /* wrap uiPieMenuBegin */
  fb = api_def_fn(sapi, "piemenu_begin__internal", "api_PieMenuBegin");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  parm = api_def_string(fn, "title", NULL, 0, "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_prop(fn, "icon", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(parm, api_enum_icon_items);
  parm = api_def_ptr(fn, "event", "Event", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_APIPTR);
  /* return */
  parm = api_def_ptr(fn, "menu_pie", "UIPieMenu", "", "");
  api_def_patam_flags(parm, PROP_NEVER_NULL, PARM_APIPTR);
  api_def_fn_return(fn, parm);

  /* wrap uiPieMenuEnd */
  fn = api_def_fn(sapi, "piemenu_end__internal", "rna_PieMenuEnd");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CTX);
  parm = api_def_ptr(fn, "menu", "UIPieMenu", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_RNAPTR | PARM_REQUIRED);

  /* access last operator options (optionally create). */
  fn = api_def_fn(
      sapi, "op_props_last", "api_WindoManager_op_props_last");
  api_def_fn_flag(fn, FN_NO_SELF);
  parm = api_def_string(fn, "operator", NULL, 0, "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return */
  parm = api_def_ptr(fn, "result", "OperatorProperties", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_APIPTR);
  api_def_fn_return(fn, parm);

  api_def_fn(sapi, "print_undo_steps", "api_WindowManager_print_undo_steps");

  /* Used by (#SCRIPT_OT_reload). */
  fn = api_def_fn(sapi, "tag_script_reload", "api_WindowManager_tag_script_reload");
  api_def_fn_ui_description(
      func, "Tag for refreshing the interface after scripts have been reloaded");
  api_def_fn_flag(fb, FN_NO_SELF);

  parm = api_def_prop(sapi, "is_interface_locked", PROP_BOOL, PROP_NONE);
  api_def_prop_ui_text(
      parm,
      "Is Interface Locked",
      "If true, the interface is currently locked by a running job and data shouldn't be modified "
      "from application timers. Otherwise, the running job might conflict with the handler "
      "causing unexpected results or even crashes");
  api_def_prop_clear_flag(parm, PROP_EDITABLE);
}

void api_op(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  /* utility, not for registering */
  fn = api_def_fn(sapi, "report", "api_op_report");
  parm = api_def_enum_flag(fb, "type", api_enum_wm_report_items, 0, "Type", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fb, "message", NULL, 0, "Report Message", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* utility, not for registering */
  fn = api_def_fn(sapi, "is_repeat", "api_op_is_repeat");
  api_def_fn_flag(fn, FN_USE_CTX);
  /* return */
  parm = api_def_bool(fn, "result", 0, "result", "");
  api_def_fn_return(fn, parm);

  /* Registration */

  /* poll */
  fn = api_def_fn(sapi, "poll", NULL);
  api_def_fn_ui_description(fn, "Test if the operator can be called or not");
  api_def_fn_flag(fn, FN_NO_SELF | FN_REGISTER_OPTIONAL);
  api_def_fn_return(fn, api_def_bool(fn, "visible", 1, "", ""));
  parm = api_def_ptr(fn, "ctx", "Ctx", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* exec */
  fn = api_def_fn(sapi, "execute", NULL);
  api_def_fn_ui_description(fn, "Execute the operator");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FN_ALLOW_WRITE);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* better name? */
  parm = api_def_enum_flag(
      func, "result", api_enum_op_return_items, OP_FINISHED, "result", "");
  api_def_fn_return(fn, parm);

  /* check */
  fn = api_def_fn(sapi, "check", NULL);
  api_def_fn_ui_description(
      fn, "Check the operator settings, return True to signal a change to redraw");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FN_ALLOW_WRITE);
  parm = api_def_ptr(fn, "ctx", "Ctx", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  parm = api_def_bool(fn, "result", 0, "result", ""); /* better name? */
  api_def_fn_return(fn, parm);

  /* invoke */
  func = api_def_fn(sapi, "invoke", NULL);
  api_def_fn_ui_description(fn, "Invoke the operator");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_ptr(fn, "event", "Event", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* better name? */
  parm = api_def_enum_flag(
      fn, "result", api_enum_op_return_items, OP_FINISHED, "result", "n");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "modal", NULL); /* same as invoke */
  api_def_fn_ui_description(fn, "Modal operator function");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_ptr(fn, "event", "Event", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* better name? */
  parm = api_def_enum_flag(
      fn, "result", api_enum_op_return_items, OP_FINISHED, "result", "");
  api_def_fn_return(fn, parm);

  /* draw */
  fn = api_def_fn(sapi, "draw", NULL);
  api_def_fn_ui_description(fn, "Draw function for the operator");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL);
  parm = api_def_ptr(fn, "ctx", "Ctx", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* cancel */
  func = api_def_fn(sapi, "cancel", NULL);
  api_def_fn_ui_description(fn, "Called when the operator is canceled");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FN_ALLOW_WRITE);
  parm = api_def_ptr(fn, "ctx", "Ctx", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* description */
  fn = api_def_fn(sapi, "description", NULL);
  api_def_fn_ui_description(fn, "Compute a description string that depends on parameters");
  api_def_fn_flag(fn, FN_NO_SELF | FN_REGISTER_OPTIONAL);
  parm = api_def_string(fn, "result", NULL, 4096, "result", "");
  api_def_param_clear_flags(parm, PROP_NEVER_NULL, 0);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_ptr(fn, "properties", "OperatorProperties", "", "");
  api_def_par_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
}

void api_macro(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  /* utility, not for registering */
  func = api_def_fn(sapi, "report", "rna_Operator_report");
  parm = api_def_enum_flag(fn, "type", rna_enum_wm_report_items, 0, "Type", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "message", NULL, 0, "Report Message", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* Registration */

  /* poll */
  fn = api_def_fn(sapi, "poll", NULL);
  api_def_fn_ui_description(fn, "Test if the operator can be called or not");
  api_def_fn_flag(fn, FN_NO_SELF | FN_REGISTER_OPTIONAL);
  api_def_fn_return(fn, api_def_bool(fn, "visible", 1, "", ""));
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* draw */
  fn = api_def_fn(sapi, "draw", NULL);
  api_def_fn_ui_description(fn, "Draw function for the operator");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL);
  parm = api_def_ptr(fn, "ctx", "Cyx", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
}

void api_keyconfig(ApiStruct *UNUSED(sapi))
{
  /* ApiFn *func; */
  /* ApiProp *parm; */
}

void api_keymap(ApiStruct *sapi
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "active", "api_keymap_active");
  api_def_fn_flag(fn, FN_USE_CTX);
  parm = api_def_ptr(fn, "keymap", "KeyMap", "Key Map", "Active key map");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "restore_to_default", "api_keymap_restore_to_default");
  api_def_fn_flag(fn, FN_USE_CTX);

  fn = api_def_fn(sapi, "restore_item_to_default", "api_keymap_restore_item_to_default");
  api_def_fn_flag(fn, FN_USE_CTX);
  parm = api_def_ptr(fn, "item", "KeyMapItem", "Item", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
}

void api_keymapitem(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "compare", "api_KeyMapItem_compare");
  parm = api_def_ptr(fn, "item", "KeyMapItem", "Item", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn, "result", 0, "Comparison result", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "to_string", "api_KeyMapItem_to_string");
  api_def_bool(fn, "compact", false, "Compact", "");
  parm = api_def_string(fn, "result", NULL, UI_MAX_SHORTCUT_STR, "result", "");
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);
}

void api_api_keymapitems(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "new", "api_KeyMap_item_new");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_string(fn, "idname", NULL, 0, "Op Id", "");
  ap_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn, "type", api_enum_event_type_items, 0, "Type", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn, "value", api_enum_event_value_items, 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "any", 0, "Any", "");
  api_def_int(fn, "shift", KM_NOTHING, KM_ANY, KM_MOD_HELD, "Shift", "", KM_ANY, KM_MOD_HELD);
  api_def_int(fn, "ctrl", KM_NOTHING, KM_ANY, KM_MOD_HELD, "Ctrl", "", KM_ANY, KM_MOD_HELD);
  api_def_int(fn, "alt", KM_NOTHING, KM_ANY, KM_MOD_HELD, "Alt", "", KM_ANY, KM_MOD_HELD);
  api_def_int(fn, "oskey", KM_NOTHING, KM_ANY, KM_MOD_HELD, "OS Key", "", KM_ANY, KM_MOD_HELD);
  api_def_enum(fn, "key_mod", api_enum_event_type_items, 0, "Key Modifier", "");
  api_def_enum(fn, "direction", api_enum_event_direction_items, KM_ANY, "Direction", "");
  api_def_bool(fn, "repeat", false, "Repeat", "When set, accept key-repeat events");
  api_def_bool(fn,
               "head",
               0,
               "At Head",
               "Force item to be added at start (not end) of key map so that "
               "it doesn't get blocked by an existing key map item");
  parm = api_def_ptr(fn, "item", "KeyMapItem", "Item", "Added key map item");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_modal", "api_KeyMap_item_new_modal");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_string(fn, "propvalue", NULL, 0, "Property Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn, "type", api_enum_event_type_items, 0, "Type", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn, "value", api_enum_event_value_items, 0, "Value", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "any", 0, "Any", "");
  api_def_int(fn, "shift", KM_NOTHING, KM_ANY, KM_MOD_HELD, "Shift", "", KM_ANY, KM_MOD_HELD);
  api_def_int(fn, "ctrl", KM_NOTHING, KM_ANY, KM_MOD_HELD, "Ctrl", "", KM_ANY, KM_MOD_HELD);
  api_def_int(fn, "alt", KM_NOTHING, KM_ANY, KM_MOD_HELD, "Alt", "", KM_ANY, KM_MOD_HELD);
  api_def_int(fn, "oskey", KM_NOTHING, KM_ANY, KM_MOD_HELD, "OS Key", "", KM_ANY, KM_MOD_HELD);
  api_def_enum(fn, "key_modifier", api_enum_event_type_items, 0, "Key Modifier", "");
  api_def_enum(fn, "direction", api_enum_event_direction_items, KM_ANY, "Direction", "");
  api_def_bool(fn, "repeat", false, "Repeat", "When set, accept key-repeat events");
  parm = api_def_ptr(fn, "item", "KeyMapItem", "Item", "Added key map item");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_from_item", "api_KeyMap_item_new_from_item");
  RNA_def_function_flag(func, FN_USE_REPORTS);
  parm = RNA_def_pointer(func, "item", "KeyMapItem", "Item", "Item to use as a reference");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  RNA_def_boolean(func, "head", 0, "At Head", "");
  parm = RNA_def_pointer(func, "result", "KeyMapItem", "Item", "Added key map item");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_KeyMap_item_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "item", "KeyMapItem", "Item", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

  func = RNA_def_function(srna, "from_id", "WM_keymap_item_find_id");
  parm = RNA_def_property(func, "id", PROP_INT, PROP_NONE);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_property_ui_text(parm, "id", "ID of the item");
  parm = RNA_def_pointer(func, "item", "KeyMapItem", "Item", "");
  RNA_def_function_return(func, parm);

  /* Keymap introspection
   * Args follow: KeyConfigs.find_item_from_operator */
  func = RNA_def_function(srna, "find_from_operator", "rna_KeyMap_item_find_from_operator");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID);
  parm = RNA_def_string(func, "idname", NULL, 0, "Operator Identifier", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "properties", "OperatorProperties", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_enum_flag(
      func, "include", rna_enum_event_type_mask_items, EVT_TYPE_MASK_ALL, "Include", "");
  RNA_def_enum_flag(func, "exclude", rna_enum_event_type_mask_items, 0, "Exclude", "");
  parm = RNA_def_pointer(func, "item", "KeyMapItem", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "match_event", "rna_KeyMap_item_match_event");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_USE_CONTEXT);
  parm = RNA_def_pointer(func, "event", "Event", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "item", "KeyMapItem", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);
}

void RNA_api_keymaps(StructRNA *srna)
{
  FunctionRNA *func;
  PropertyRNA *parm;

  func = RNA_def_function(srna, "new", "rna_keymap_new"); /* add_keymap */
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(
      func,
      "Ensure the keymap exists. This will return the one with the given name/space type/region "
      "type, or create a new one if it does not exist yet.");

  parm = api_def_string(fn, "name", NULL, 0, "Name", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_enum(fn, "space_type", rna_enum_space_type_items, SPACE_EMPTY, "Space Type", "");
  api_def_enum(
      fn, "region_type", api_enum_region_type_items, RGN_TYPE_WINDOW, "Region Type", "");
  api_def_bool(fb, "modal", 0, "Modal", "Keymap for modal operators");
  api_def_bool(fn, "tool", 0, "Tool", "Keymap for active tools");
  parm = api_def_ptr(fn, "keymap", "KeyMap", "Key Map", "Added key map");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(srna, "remove", "rna_KeyMap_remove"); /* remove_keymap */
  api_def_fn_flag(fn, FN_USE_REPORT
  parm = api_def_ptr(func, "keymap", "KeyMap", "Key Map", "Removed key map");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_function(sapi, "find", "rna_keymap_find"); /* find_keymap */
  parm = api_def_string(fn, "name", NULL, 0, "Name", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_enum(fn, "space_type", rna_enum_space_type_items, SPACE_EMPTY, "Space Type", "");
  api_def_enum(
      func, "region_type", api_enum_region_type_items, RGN_TYPE_WINDOW, "Region Type", "");
  parm = api_def_ptr(fn, "keymap", "KeyMap", "Key Map", "Corresponding key map");
  api_def_fn_return(fn, parm);

  func = api_def_fn(sapi, "find_modal", "rna_keymap_find_modal"); /* find_keymap_modal */
  parm = api_def_string(fn, "name", NULL, 0, "Operator Name", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "keymap", "KeyMap", "Key Map", "Corresponding key map");
  api_def_fn_return(fn, parm);
}

void api_keyconfigs(ApiStruct *sapi)
{
  FunctionRNA *func;
  PropertyRNA *parm;

  func = api_def_fn(sapi, "new", "WM_keyconfig_new_user"); /* add_keyconfig */
  parm = api_def_string(fn, "name", NULL, 0, "Name", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      func, "keyconfig", "KeyConfig", "Key Configuration", "Added key configuration");
  api_def_fn_return(fn, parm);

  func = api_def_fn(sapi, "remove", "api_KeyConfig_remove"); /* remove_keyconfig */
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(
      fn, "keyconfig", "KeyConfig", "Key Configuration", "Removed key configuration"
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  /* Helper functions */

  /* Keymap introspection */
  fn = api_def_fn(
      sapi, "find_item_from_op", "api_KeyConfig_find_item_from_op");
  api_def_fn_flag(fn, FN_USE_CTX);
  parm = api_def_string(fn, "idname", NULL, 0, "Op Id", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_prop(fn, "context", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(parm, api_enum_oper_ctx_items);
  parm = api_def_ptr(fn, "properties", "OperatorProperties", "", "");
  api_def_param_flags(parm, 0, PARM_RNAPTR);
  api_def_enum_flag(
      fn, "include", api_enum_event_type_mask_items, EVT_TYPE_MASK_ALL, "Include", "");
  api_def_enum_flag(fn, "exclude", api_enum_event_type_mask_items, 0, "Exclude", "");
  parm = api_def_ptr(fn, "keymap", "KeyMap", "", "");
  api_def_param_flags(parm, 0, PARM_APIPTR | PARM_OUTPUT);
  parm = api_def_ptr(fn, "item", "KeyMapItem", "", "");
  api_def_param_flags(parm, 0, PARM_APIPTR);
  api_def_fn_return(fn, parm);

  api_def_fn(sapi, "update", "api_KeyConfig_update"); /* WM_keyconfig_update */
}

#endif
