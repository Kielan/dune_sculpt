#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "mem_guardedalloc.h"

#include "types_brush.h"
#include "types_curveprofile.h"
#include "types_scene.h"
#include "types_screen.h"

#include "lib_array.hh"
#include "lib_array_utils.h"
#include "lib_linklist.h"
#include "lib_list.h"
#include "lib_math_geom.h"
#include "lib_rect.h"
#include "lib_sort_utils.h"
#include "lib_string.h"
#include "lib_string_cursor_utf8.h"
#include "lib_string_utf8.h"
#include "lib_utildefines.h"

#include "PIL_time.h"

#include "dune_animsys.h"
#include "dune_undo.h"
#include "dune_brush.hh"
#include "dune_colorband.h"
#include "dune_colortools.h"
#include "dune_cxt.h"
#include "dune_curveprofile.h"
#include "dune_movieclip.h"
#include "dune_paint.hh"
#include "dune_report.h"
#include "dune_screen.hh"
#include "dune_tracking.h"
#include "dune_unit.h"

#include "GHOST_C-api.h"

#include "imbuf_colormanagement.h"

#include "ed_screen.hh"
#include "ed_undo.hh"

#include "ui.hh"
#include "ui_string_search.hh"
#include "ui_view2d.hh"

#include "font_api.h"

#include "ui_intern.hh"

#include "api_access.hh"
#include "api_prototypes.h"

#include "win_api.hh"
#include "win_types.hh"
#include "win_ev_system.h"

#ifdef WITH_INPUT_IME
#  include "lang.h"
#  include "lang_translation.h"
#  include "win.hh"
#endif

/* Feature Defines
 *
 * These defines allow developers to locally toggle functionality which
 * may be useful for testing (especially conflicts in dragging).
 * Ideally the code would be refactored to support this functionality in a less fragile way.
 * Until then keep these defines. */

/* Place the mouse at the scaled down location when un-grabbing. */
#define USE_CONT_MOUSE_CORRECT
/* Support dragging toggle btns. */
#define USE_DRAG_TOGGLE

/* Support dragging multiple number btns at once. */
#define USE_DRAG_MULTINUM

/* Allow dragging/editing all other selected items at once. */
#define USE_ALLSELECT

/* Check to avoid very small mouse-moves from jumping away from keyboard nav,
 * while larger mouse motion will override keyboard input, see: #34936. */
#define USE_KEYNAV_LIMIT

/* Support dragging popups by their header. */
#define USE_DRAG_POPUP

/* Local Defines */
/* The buffer side used for password strings, where the password is stored internally,
 * but not displayed. */
#define UI_MAX_PASSWORD_STR 128

/* This is a lower limit on the soft minimum of the range.
 * Usually the derived lower limit from the visible precision is higher,
 * so this number is the backup minimum.
 *
 * Logarithmic scale does not work with a minimum value of zero,
 * but we want to support it anyway. It is set to 0.5e... for
 * correct rounding since when the tweaked value is lower than
 * the log minimum (lower limit), it will snap to 0. */
#define UI_PROP_SCALE_LOG_MIN 0.5e-8f
/* This constant defines an offset for the precision change in
 * snap rounding, when going to higher values. It is set to
 * `0.5 - log10(3) = 0.03` to make the switch at `0.3` values. */
#define UI_PROP_SCALE_LOG_SNAP_OFFSET 0.03f

/* When USER_CONTINUOUS_MOUSE is disabled or tablet input is used,
 * Use this as a max soft range for mapping cursor motion to the value.
 * Otherwise min/max of FLT_MAX, INT_MAX cause small adjustments to jump to large numbers.
 *
 * This is needed for values such as location & dimensions which don't have a meaningful min/max,
 * Instead of mapping cursor motion to the min/max, map the motion to the click-step.
 *
 * This value is multiplied by the click step to calculate a range to clamp the soft-range by.
 * See: #68130
 */
#define UI_DRAG_MAP_SOFT_RANGE_PIXEL_MAX 1000

/* Local Prototypes */
struct uiBlockInteraction_Handle;

static int btn_EXIT(Cxt *C, Btn *btn, uiHandleBtnData *data, const WinEv *ev);
static bool btn_find_select_in_enum__cmp(const Btn *btn_a, const Btn *btn_b);
static void ui_textedit_string_set(Btn *btn, uiHandleBtnData *data, const char *str);
static void btn_tooltip_timer_reset(Cxt *C, Btn *btn);

static void ui_block_interaction_begin_ensure(Cxt *C,
                                              uiBlock *block,
                                              uiHandleBtnData *data,
                                              const bool is_click);
static uiBlockInteraction_Handle *ui_block_interaction_begin(Cxt *C,
                                                             uiBlock *block,
                                                             const bool is_click);
static void ui_block_interaction_end(Cxt *C,
                                     uiBlockInteraction_CbData *cbs,
                                     uiBlockInteraction_Handle *interaction);
static void ui_block_interaction_update(Cxt *C,
                                        uiBlockInteraction_CbData *cbs,
                                        uiBlockInteraction_Handle *interaction);

#ifdef USE_KEYNAV_LIMIT
static void ui_mouse_motion_keynav_init(uiKeyNavLock *keynav, const WinEv *ev);
static bool ui_mouse_motion_keynav_test(uiKeyNavLock *keynav, const WinEv *ev);
#endif

/* Structs & Defines */

#define BTN_FLASH_DELAY 0.020
#define MENU_SCROLL_INTERVAL 0.1
#define PIE_MENU_INTERVAL 0.01
#define BTN_AUTO_OPEN_THRESH 0.2
#define BTN_MOUSE_TOWARDS_THRESH 1.0
/** Pixels to move the cursor to get out of keyboard navigation. */
#define BTN_KEYNAV_PX_LIMIT 8

/* Margin around the menu, use to check if we're moving towards this rectangle (in pixels). */
#define MENU_TOWARDS_MARGIN 20
/* Tolerance for closing menus (in pixels). */
#define MENU_TOWARDS_WIGGLE_ROOM 64
/* Drag-lock distance threshold (in pixels). */
#define BTN_DRAGLOCK_THRESH 3

enum BtnActivateType {
  BTN_ACTIVATE_OVER,
  BTN_ACTIVATE,
  BTN_ACTIVATE_APPLY,
  BTN_ACTIVATE_TEXT_EDITING,
  BTN_ACTIVATE_OPEN,
};

enum uiHandleBtnState {
  BTN_STATE_INIT,
  BTN_STATE_HIGHLIGHT,
  BTN_STATE_WAIT_FLASH,
  BTN_STATE_WAIT_RELEASE,
  BTN_STATE_WAIT_KEY_EVENT,
  BTN_STATE_NUM_EDITING,
  BTN_STATE_TEXT_EDITING,
  BTN_STATE_TEXT_SELECTING,
  BTN_STATE_MENU_OPEN,
  BTN_STATE_WAIT_DRAG,
  BTN_STATE_EXIT,
};

enum uiMenuScrollType {
  MENU_SCROLL_UP,
  MENU_SCROLL_DOWN,
  MENU_SCROLL_TOP,
  MENU_SCROLL_BOTTOM,
};

struct uiBlockInteraction_Handle {
  uiBlockInteraction_Params params;
  void *user_data;
  /* This is shared between #uiHandleButtonData and #uiAfterFunc,
   * the last user runs the end callback and frees the data.
   *
   * This is needed as the order of freeing changes depending on
   * accepting/canceling the operation. */
  int user_count;
};

#ifdef USE_ALLSELECT

/* Unfortunately there's no good way handle more generally:
 * (propagate single clicks on layer buttons to other objects) */
#  define USE_ALLSELECT_LAYER_HACK

struct uiSelectContextElem {
  ApiPtrPointerRNA ptr;
  union {
    bool val_b;
    int val_i;
    float val_f;
  };
};

struct uiSelCxtStore {
  uiSelCxtElem *elems;
  int elems_len;
  bool do_free;
  bool is_enabled;
  /* When set, simply copy values (don't apply difference).
   * Rules are:
   * - dragging numbers uses delta.
   * - typing in values will assign to all. */
  bool is_copy;
};

static bool ui_selcxt_begin(Cxt *C, Btn *btn, uiSelectCxtStore *selcxt_data);
static void ui_selcxt_end(Btn *btn, uiSelectCxtStore *selcxt_data);
static void ui_selcxt_apply(Cxt *C,
                               Btn *btn,
                               uiSelectCxtStore *selcxt_data,
                               const double value,
                               const double value_orig);

/* Ideally we would only respond to events which are expected to be used for multi btn editing
 * (additionally checking if this is a mouse[wheel] or return-key event to avoid the ALT conflict
 * with button array pasting, see #108096, but unfortunately wheel events are not part of
 * `win->evstate` with modifiers held down. Instead, the conflict is avoided by specifically
 * filtering out CTRL ALT V in #ui_apply_but(). */
#  define IS_ALLSEL_EV(event) (((event)->mod & KM_ALT) != 0)

/* just show a tinted color so users know its activated */
#  define BTN_IS_SEL_CXT BTN_NODE_ACTIVE

#endif /* USE_ALLSELECT */

#ifdef USE_DRAG_MULTINUM

/* how far to drag before we check for gesture direction (in pixels),
 * NOTE: half the height of a btn is about right... */
#  define DRAG_MULTINUM_THRESHOLD_DRAG_X (UI_UNIT_Y / 4)

/* How far to drag horizontally
 * before we stop checking which btns the gesture spans (in pixels),
 * locking down the btns so we can drag freely without worrying about vertical movement. */
#  define DRAG_MULTINUM_THRESHOLD_DRAG_Y (UI_UNIT_Y / 4)

/* How strict to be when detecting a vertical gesture:
 * [0.5 == sloppy], [0.9 == strict], (unsigned dot-product).
 *
 * \note We should be quite strict here,
 * since doing a vertical gesture by accident should be avoided,
 * however with some care a user should be able to do a vertical movement without _missing_. */
#  define DRAG_MULTINUM_THRESHOLD_VERTICAL (0.75f)

/* a simple version of uiHandleButtonData when accessing multiple buttons */
struct BtnMultiState {
  double origvalue;
  Btn *btn;

#  ifdef USE_ALLSELECT
  uiSelectCxtStore sel_others;
#  endif
};

struct uiHandleBtnMulti {
  enum {
    /* gesture direction unknown, wait until mouse has moved enough... */
    INIT_UNSET = 0,
    /* vertical gesture detected, flag btns interactively (UI_BTN_DRAG_MULTI) */
    INIT_SETUP,
    /* flag btns finished, apply horizontal motion to active and flagged */
    INIT_ENABLE,
    /* vertical gesture _not_ detected, take no further action */
    INIT_DISABLE,
  } init;

  bool has_mbuts; /* any btns flagged UI_BTN_DRAG_MULTI */
  LinkNode *mbuts;
  uiBtnStore *bs_mbtns;

  bool is_proportional;

  /* In some cases we directly apply the changes to multiple buttons,
   * so we don't want to do it twice. */
  bool skip;

  /* before activating, we need to check gesture direction accumulate signed cursor movement
   * here so we can tell if this is a vertical motion or not. */
  float drag_dir[2];

  /* values copied direct from event->xy
   * used to detect btns between the current and initial mouse position */
  int drag_start[2];

  /* store x location once INIT_SETUP is set,
   * moving outside this sets INIT_ENABLE */
  int drag_lock_x;
};

#endif /* USE_DRAG_MULTINUM */

struct uiHandleBtnData {
  WinManager *wm;
  Win *win;
  ScrArea *area;
  ARgn *rgn;

  bool interactive;

  /* overall state */
  uiHandleBtnState state;
  int retval;
  /* booleans (could be made into flags) */
  bool cancel, escapecancel;
  bool applied, applied_interactive;
  /* Btn is being applied through an extra icon. */
  bool apply_through_extra_icon;
  bool changed_cursor;
  WinTimer *flashtimer;

  /* edited value */
  /* use 'ui_textedit_string_set' to assign new strings */
  char *str;
  char *origstr;
  double value, origvalue, startvalue;
  float vec[3], origvec[3];
  ColorBand *coba;

  /* True when alt is held and the preference for displaying tooltips should be ignored. */
  bool tooltip_force;
  /* Behave as if BTN_DISABLED is set (without drawing grayed out).
   * Needed so non-interactive labels can be activated for the purpose of showing tool-tips,
   * without them blocking interaction with nodes, see: #97386. */
  bool disable_force;

  /* auto open */
  bool used_mouse;
  WinTimer *autoopentimer;

  /* auto open (hold) */
  WinTimer *hold_action_timer;

  /* text selection/editing */
  /* size of 'str' (including terminator) */
  int str_maxncpy;
  /* Btn text selection:
   * extension direction, selextend, inside btn_TXT */
  int sel_pos_init;
  /* Allow reallocating str/editstr and using 'maxlen' to track alloc size (maxlen + 1) */
  bool is_str_dynamic;

  /* number editing / dragging */
  /* coords are Window/uiBlock relative (depends on the button) */
  int draglastx, draglasty;
  int dragstartx, dragstarty;
  int draglastvalue;
  int dragstartvalue;
  bool dragchange, draglock;
  int dragsel;
  float dragf, dragfstart;
  CBData *dragcbd;

  /** Soft min/max with UI_DRAG_MAP_SOFT_RANGE_PIXEL_MAX applied. */
  float drag_map_soft_min;
  float drag_map_soft_max;

#ifdef USE_CONT_MOUSE_CORRECT
  /* when ungrabbing btns which are btn_is_cursor_warp(),
   * we may want to position them.
   * FLT_MAX signifies do-nothing, use ui_block_to_win_fl()
   * to get this into a usable space. */
  float ungrab_mval[2];
#endif

  /* Menu open, see: UI_screen_free_active_btn_highlight. */
  uiPopupBlockHandle *menu;
  int menuretval;

  /* Search box see: UI_screen_free_active_btn_highlight. */
  ARgn *searchbox;
#ifdef USE_KEYNAV_LIMIT
  uiKeyNavLock searchbox_keynav_state;
#endif

#ifdef USE_DRAG_MULTINUM
  /* Multi-btns will be updated in unison with the active btn. */
  uiHandleBtnMulti multi_data;
#endif

#ifdef USE_ALLSEL
  uiSelCxtStore select_others;
#endif

  uiBlockInteraction_Handle *custom_interaction_handle;

  /* Text field undo. */
  uiUndoStack_Text *undo_stack_text;

  /* post activate */
  BtnActivateType posttype;
  Btn *postbtn;
};

struct uiAfterFn {
  uiAfterFn *next, *prev;

  BtnHandleFn fn;
  void *fn_arg1;
  void *fn_arg2;
  /* C++ version of fn above, without need for void ptr arguments. */
  std::fn<void(Cxt &)> apply_fn;

  BtnHandleNFn fn;
  void *fn_arg;

  BtnHandleRenameFn rename_fn;
  void *rename_arg1;
  void *rename_orig;

  uiBlockHandleFn handle_fn;
  void *handle_fn_arg;
  int retval;

  uiMenuHandleFn btnm_fn;
  void *btnm_fn_arg;
  int a2;

  WinOp *popup_op;
  WinOpType *optype;
  WinOpCallCxt opcxt;
  ApiPtr *opptr;

  ApiPtr apiptr;
  ApiProp *apiprop;

  void *search_arg;
  uiFreeArgFn search_arg_free_fn;

  uiBlockInteraction_CbData custom_interaction_cbs;
  uiBlockInteraction_Handle *custom_interaction_handle;

  std::optional<CxtStore> cxt;

  char undostr[DUNE_UNDO_STR_MAX];
  char drawstr[UI_MAX_DRAW_STR];
};

static void btn_activate_init(Cxt *C,
                              ARgn *rgn,
                              Btn *btn,
                              BtnActivateType type);
static void btn_activate_state(Cxt *C, Btn *btn, uiHandleBtnState state);
static void btn_activate_exit(
    Cxt *C, Btn *btn, uiHandleBtnData *data, const bool mousemove, const bool onfree);
static int ui_handler_rgn_menu(Cxt *C, const WinEv *ev, void *userdata);
static void ui_handle_btn_activate(Cxt *C,
                                   ARgn *rgn,
                                   Btn *btn,
                                   BtnActivateType type);
static bool btn_do_extra_op_icon(Cxt *C,
                                 Btn *btn,
                                 uiHandleBtnData *data,
                                 const WinEv *ev);
static void ui_do_but_extra_op_icons_mousemove(Btn *by ,
                                                     uiHandleBtnData *data,
                                                     const WinEv *ev);
static void ui_numedit_begin_set_values(Btn *btn, uiHandleBtnData *data);

#ifdef USE_DRAG_MULTINUM
static void multibtn_restore(bContext *C, uiHandleBtnData *data, uiBlock *block);
static BtnMultiState *ui_multibtn_lookup(uiHandleBtnData *data, const uiBtn *btn);
#endif

/* btns clipboard */
static ColorBand btn_copypaste_coba = {0};
static CurveMapping bt _copypaste_curve = {0};
static bool btn_copypaste_curve_alive = false;
static CurveProfile btn_copypaste_profile = {0};
static bool btn_copypaste_profile_alive = false;

/* UI Queries */
bool btn_is_editing(const Btn *btn)
{
  const BtnHandleData *data = btn->active;
  return (data && ELEM(data->state, BTN_STATE_TEXT_EDITING, BTN_STATE_NUM_EDITING));
}

void ui_pan_to_scroll(const WinEv *ev, int *type, int *val)
{
  static int lastdy = 0;
  const int dy = win_ev_absolute_delta_y(ev);

  /* This event should be originally from event->type,
   * converting wrong event into wheel is bad, see #33803. */
  lib_assert(*type == MOUSEPAN);

  /* sign differs, reset */
  if ((dy > 0 && lastdy < 0) || (dy < 0 && lastdy > 0)) {
    lastdy = dy;
  }
  else {
    lastdy += dy;

    if (abs(lastdy) > int(UI_UNIT_Y)) {
      *val = KM_PRESS;

      if (dy > 0) {
        *type = WHEELUPMOUSE;
      }
      else {
        *type = WHEELDOWNMOUSE;
      }

      lastdy = 0;
    }
  }
}

static bool btn_find_select_in_enum__cmp(const Btn *btn_a, const Btn *btn_b)
{
  return ((btn_a->type == btn_b->type) && (btn_a->alignnr == btn_b->alignnr) &&
          (btn_a->ptr == btn_b->ptr) && (btn_a->apiptr.type == btn_b->apiptr.type) &&
          (btn_a->apiprop == btn_b->apiprop));
}

Btn *btn_find_select_in_enum(Btn *btn, int direction)
{
  Btn *btn_iter = btn;
  Btn *btn_found = nullptr;
  lib_assert(ELEM(direction, -1, 1));

  while ((btn_iter->prev) && btn_find_select_in_enum__cmp(btn_iter->prev, btn)) {
    btn_iter = btn_iter->prev;
  }

  while (btn_iter && btn_find_select_in_enum__cmp(btn_iter, btn)) {
    if (btn_iter->flag & UI_SELECT) {
      btn_found = btn_iter;
      if (direction == 1) {
        break;
      }
    }
    btn_iter = btn_iter->next;
  }

  return btn_found;
}

static float ui_mouse_scale_warp_factor(const bool shift)
{
  return shift ? 0.05f : 1.0f;
}

static void ui_mouse_scale_warp(BtnHandleData *data,
                                const float mx,
                                const float my,
                                float *r_mx,
                                float *r_my,
                                const bool shift)
{
  const float fac = ui_mouse_scale_warp_factor(shift);

  /* slow down the mouse, this is fairly picky */
  *r_mx = (data->dragstartx * (1.0f - fac) + mx * fac);
  *r_my = (data->dragstarty * (1.0f - fac) + my * fac);
}

/* UI Utils */
/* Ignore mouse movements within some horizontal pixel threshold before starting to drag */
static bool btn_dragedit_update_mval(uiHandleButtonData *data, int mx)
{
  if (mx == data->draglastx) {
    return false;
  }

  if (data->draglock) {
    if (abs(mx - data->dragstartx) <= BTN_DRAGLOCK_THRESH) {
      return false;
    }
#ifdef USE_DRAG_MULTINUM
    if (ELEM(data->multi_data.init,
             uiHandleBtnMulti::INIT_UNSET,
             uiHandleBtnMulti::INIT_SETUP)) {
      return false;
    }
#endif
    data->draglock = false;
    data->dragstartx = mx; /* ignore mouse movement within drag-lock */
  }

  return true;
}

static bool ui_api_is_userdef(ApiPter *ptr, ApiProp *prop)
{
  /* Not very elegant, but ensures pref changes force re-save. */

  if (!prop) {
    return false;
  }
  if (api_prop_flag(prop) & PROP_NO_GRAPH_UPDATE) {
    return false;
  }

  ApuStruct *base = api_struct_base(ptr->type);
  if (base == nullptr) {
    base = ptr->type;
  }
  return ELEM(base,
              &ApiAddonPrefs,
              &ApiKeyConfigPrefs,
              &ApiKeyMapItem,
              &ApiUserAssetLib);
}

bool btn_is_userdef(const Btn *btn)
{
  /* This is read-only, Api isn't using const when it could. */
  return ui_api_is_userdef((ApiPtr *)&btn->apiptr, btm->apiprop);
}

static void ui_api_update_prefs_dirty(ApiPtr *ptr, ApiProp *prop)
{
  if (ui_api_is_userdef(ptr, prop)) {
    U.runtime.is_dirty = true;
    win_main_add_notifier(NC_WIN, nullptr);
  }
}

static void btn_update_prefs_dirty(Btn *byn)
{
  ui_api_update_prefs_dirty(&btn->apiptr, btn->apiprop);
}

static void ui_afterfn_update_prefs_dirty(uiAfterFn *after)
{
  ui_api_update_prefs_dirty(&after->apiptr, after->apiprop);
}

/* Btn Snap Values */
enum eSnapType {
  SNAP_OFF = 0,
  SNAP_ON,
  SNAP_ON_SMALL,
};

static enum eSnapType ui_event_to_snap(const WinEv *ev)
{
  return (ev->mod & KM_CTRL) ? (ev->mod & KM_SHIFT) ? SNAP_ON_SMALL : SNAP_ON :
                                       SNAP_OFF;
}

static bool ui_ev_is_snap(const WinEv *ev)
{
  return (ELEM(ev->type, EV_LEFTCTRLKEY, EV_RIGHTCTRLKEY) ||
          ELEM(ev->type, EV_LEFTSHIFTKEY, EV_RIGHTSHIFTKEY));
}

static void ui_color_snap_hue(const enum eSnapType snap, float *r_hue)
{
  const float snap_increment = (snap == SNAP_ON_SMALL) ? 24 : 12;
  lib_assert(snap != SNAP_OFF);
  *r_hue = roundf((*r_hue) * snap_increment) / snap_increment;
}

/* Btn Apply/Revert */
static List UIAfterFns = {nullptr, nullptr};

static uiAfterFn *ui_afterfn_new()
{
  uiAfterFn *after = mem_new<uiAfterFn>(__func__);
  /* Safety asserts to check if members were 0 initialized properly. */
  lib_assert(after->next == nullptr && after->prev == nullptr);
  lib_assert(after->undostr[0] == '\0');

  lib_addtail(&UIAfterFns, after);

  return after;
}

/* For executing ops after the btn is pressed.
 * (some non op btns need to trigger ops), see: #37795.
 *
 * param cxt_btn: A btn from which to get the cxt from (`Btn.cxt`) for the
 *                     op execution.
 *
 * \note Ownership over \a properties is moved here. The #uiAfterFunc owns it now.
 * \note Can only call while handling buttons.
 */
static void ui_handle_afterfn_add_operator_ex(wmOperatorType *ot,
                                                PointerRNA **properties,
                                                wmOperatorCallContext opcontext,
                                                const uiBut *context_but)
{
  uiAfterFunc *after = ui_afterfunc_new();

  after->optype = ot;
  after->opcontext = opcontext;
  if (properties) {
    after->opptr = *properties;
    *properties = nullptr;
  }

  if (context_but && context_but->context) {
    after->context = *context_but->context;
  }

  if (context_but) {
    ui_but_drawstr_without_sep_char(context_but, after->drawstr, sizeof(after->drawstr));
  }
}

void ui_handle_afterfunc_add_operator(wmOperatorType *ot, wmOperatorCallContext opcontext)
{
  ui_handle_afterfunc_add_operator_ex(ot, nullptr, opcontext, nullptr);
}

static void popup_check(bContext *C, wmOperator *op)
{
  if (op && op->type->check) {
    op->type->check(C, op);
  }
}

/**
 * Check if a #uiAfterFunc is needed for this button.
 */
static bool ui_afterfunc_check(const uiBlock *block, const uiBut *but)
{
  return (but->func || but->apply_func || but->funcN || but->rename_func || but->optype ||
          but->rnaprop || block->handle_func ||
          (but->type == UI_BTYPE_BUT_MENU && block->butm_func) ||
          (block->handle && block->handle->popup_op));
}

/**
 * These functions are postponed and only executed after all other
 * handling is done, i.e. menus are closed, in order to avoid conflicts
 * with these functions removing the buttons we are working with.
 */
static void ui_apply_but_func(bContext *C, uiBut *but)
{
  uiBlock *block = but->block;
  if (!ui_afterfunc_check(block, but)) {
    return;
  }

  uiAfterFunc *after = ui_afterfunc_new();

  if (but->func && ELEM(but, but->func_arg1, but->func_arg2)) {
    /* exception, this will crash due to removed button otherwise */
    but->func(C, but->func_arg1, but->func_arg2);
  }
  else {
    after->func = but->func;
  }

  after->func_arg1 = but->func_arg1;
  after->func_arg2 = but->func_arg2;

  after->apply_func = but->apply_func;

  after->funcN = but->funcN;
  after->func_argN = (but->func_argN) ? MEM_dupallocN(but->func_argN) : nullptr;

  after->rename_func = but->rename_func;
  after->rename_arg1 = but->rename_arg1;
  after->rename_orig = but->rename_orig; /* needs free! */

  after->handle_func = block->handle_func;
  after->handle_func_arg = block->handle_func_arg;
  after->retval = but->retval;

  if (but->type == UI_BTYPE_BUT_MENU) {
    after->butm_func = block->butm_func;
    after->butm_func_arg = block->butm_func_arg;
    after->a2 = but->a2;
  }

  if (block->handle) {
    after->popup_op = block->handle->popup_op;
  }

  after->optype = btn->optype;
  after->opcxt = btn->opcxt;
  after->opptr = btn->opptr;

  after->apiptr = btn->apiptr;
  after->apiprop = btn-apiprop;

  if (btn->type == BTYPE_SEARCH_MENU) {
    BtnSearch *search_btn = (BtnSearch *)btn;
    after->search_arg_free_fn = search_btn->arg_free_fn;
    after->search_arg = search_btn->arg;
    search_btn->arg_free_fn = nullptr;
    search_btn->arg = nullptr;
  }

  if (btn->active != nullptr) {
    uiHandleBtnData *data = btn->active;
    if (data->custom_interaction_handle != nullptr) {
      after->custom_interaction_callbacks = block->custom_interaction_cbs;
      after->custom_interaction_handle = data->custom_interaction_handle;

      /* Ensure this callback runs once and last. */
      uiAfterFn *after_prev = after->prev;
      if (after_prev && (after_prev->custom_interaction_handle == data->custom_interaction_handle))
      {
        after_prev->custom_interaction_handle = nullptr;
        memset(&after_prev->custom_interaction_callbacks,
               0x0,
               sizeof(after_prev->custom_interaction_cbs));
      }
      else {
        after->custom_interaction_handle->user_count++;
      }
    }
  }

  if (btn->cxt) {
    after->cxt = *btn->cxt;
  }

  btn_drawstr_without_sep_char(btn, after->drawstr, sizeof(after->drawstr));

  btn->optype = nullptr;
  btn->opcxt = WinOpCallCxt(0);
  btn->opptr = nullptr;
}

/* typically call ui_apply_btn_undo(), ui_apply_btn_autokey() */
static void ui_apply_but_undo(Btn *btn)
{
  if (!(but->flag & UI_BUT_UNDO)) {
    return;
  }

  const char *str = nullptr;
  size_t str_len_clip = SIZE_MAX - 1;
  bool skip_undo = false;

  /* define which string to use for undo */
  if (btn->type == BTYPE_MENU) {
    str = btn->drawstr;
    str_len_clip = ubtn_drawstr_len_without_sep_char(btn);
  }
  else if (btn->drawstr[0]) {
    str = btn->drawstr;
    str_len_clip = btn_drawstr_len_without_sep_char(btn);
  }
  else {
    str = btn->tip;
    str_len_clip = btn_tip_len_only_first_line(btn);
  }

  /* fallback, else we don't get an undo! */
  if (str == nullptr || str[0] == '\0' || str_len_clip == 0) {
    str = "Unknown Action";
    str_len_clip = strlen(str);
  }

  /* Optionally override undo when undo system doesn't support storing properties. */
  if (btn->apipoin.owner_id) {
    /* Exception for renaming ID data, we always need undo pushes in this case,
     * because undo systems track data by their ID, see: #67002. */
    /* Exception for active shape-key, since changing this in edit-mode updates
     * the shape key from object mode data. */
    if (ELEM(btn->apiprop, &api_id_name, &api_Object_active_shape_key_index)) {
      /* pass */
    }
    else {
      Id *id = btn->apipoin.owner_id;
      if (!ed_undo_is_legacy_compatible_for_prop(static_cast<bContext *>(but->block->evil_C),
                                                     id)) {
        skip_undo = true;
      }
    }
  }

  if (skip_undo == false) {
    /* disable all undo pushes from UI changes from sculpt mode as they cause memfile undo
     * steps to be written which cause lag: #71434. */
    if (dune_paintmode_get_active_from_context(static_cast<bContext *>(but->block->evil_C)) ==
        PAINT_MODE_SCULPT)
    {
      skip_undo = true;
    }
  }

  if (skip_undo) {
    str = "";
  }

  /* Delayed, after all other fns run, popups are closed, etc. */
  uiAfterFn *after = ui_afterfn_new();
  lib_strncpy(after->undostr, str, min_zz(str_len_clip + 1, sizeof(after->undostr)));
}

static void ui_apply_btn_autokey(Cxt *C, uiBut *but)
{
  Scene *scene = cxt_data_scene(C);

  /* try autokey */
  btn_anim_autokey(C, btn, scene, scene->r.cfra);

  if (!btn->apiprop) {
    return;
  }

  if (api_prop_subtype(btn->apiprop) == PROP_PASSWORD) {
    return;
  }

  /* make a little report about what we've done! */
  char *buf = win_prop_pystring_assign(C, &by ->apiptr, btn->apiprop, btn->apiindex);
  if (buf) {
    dune_report(cxt_win_reports(C), RPT_PROP, buf);
    mem_free(buf);

    win_ev_add_notifier(C, NC_SPACE | ND_SPACE_INFO_REPORT, nullptr);
  }
}

static void ui_apply_btn_fns_after(Cxt *C)
{
  /* Copy to avoid recursive calls. */
  ListBase fns = UIAfterFns;
  BLI_list_clear(&UIAfterFns);

  LIST_FOREACH_MUTABLE (uiAfterFn *, afterf, &fns) {
    uiAfterFn after = *afterf; /* Copy to avoid memory leak on exit(). */
    lib_remlink(&fns, afterf);
    mem_delete(afterf);

    if (after.cxt) {
      cxt_store_set(C, &after.cxt.value());
    }

    if (after.popup_op) {
      popup_check(C, after.popup_op);
    }

    PointerRNA opptr;
    if (after.opptr) {
      /* free in advance to avoid leak on exit */
      opptr = *after.opptr;
      mem_free(after.opptr);
    }

    if (after.optype) {
      win_op_name_call_ptr_with_depends_on_cursor(C,
                                                       after.optype,
                                                       after.opcxt,
                                                       (after.opptr) ? &opptr : nullptr,
                                                       nullptr,
                                                       after.drawstr);
    }

    if (after.opptr) {
      win_op_props_free(&opptr);
    }

    if (after.apiptr.data) {
      api_prop_update(C, &after.apiptr, after.apiprop);
    }

    if (after.cxt) {
      cxt_store_set(C, nullptr);
    }

    if (after.fn) {
      after.fn(C, after.fn_arg1, after.fn_arg2);
    }
    if (after.apply_fn) {
      after.apply_fn(*C);
    }
    if (after.fn) {
      after.fn(C, after.fn_arg, after.fn_arg2);
    }
    if (after.fn_arg) {
      mem_free(after.fn_arg);
    }

    if (after.handle_fn) {
      after.handle_fn(C, after.handle_func_arg, after.retval);
    }
    if (after.btnm_fn) {
      after.btnm_fn(C, after.btnm_fn_arg, after.a2);
    }

    if (after.rename_fn) {
      after.rename_fn(C, after.rename_arg1, static_cast<char *>(after.rename_orig));
    }
    if (after.rename_orig) {
      mem_free(after.rename_orig);
    }

    if (after.search_arg_free_fn) {
      after.search_arg_free_fn(after.search_arg);
    }

    if (after.custom_interaction_handle != nullptr) {
      after.custom_interaction_handle->user_count--;
      lib_assert(after.custom_interaction_handle->user_count >= 0);
      if (after.custom_interaction_handle->user_count == 0) {
        ui_block_interaction_update(
            C, &after.custom_interaction_cbs, after.custom_interaction_handle);
        ui_block_interaction_end(
            C, &after.custom_interaction_cbs, after.custom_interaction_handle);
      }
      after.custom_interaction_handle = nullptr;
    }

    ui_afterfn_update_prefs_dirty(&after);

    if (after.undostr[0]) {
      ed_undo_push(C, after.undostr);
    }
  }
}

static void btn_apply_BTN(Cxt *C, Btn *btn, uiHandleBtnData *data)
{
  btn_apply_fn(C, btn);

  data->retval = btn->retval;
  data->applied = true;
}

static void ui_apply_btn_BTNM(Cxt *C, Btn *btn, uiHandleBtnData *data)
{
  btn_value_set(btn, btn->hardmin);
  apply_btn_fn(C, btn);

  data->retval = btn->retval;
  data->applied = true;
}

static void btn_apply_BLOCK(Cxt *C, Btn *byn, uiHandleBtnData *data)
{
  if (btn->type == BTYPE_MENU) {
    btn_value_set(btn, data->value);
  }

  btn_update_edited(btn);
  btn_apply_fn(C, btn);
  data->retval = btn->retval;
  data->applied = true;
}

static void btn_apply_TOG(Cxt *C, Btn *btn, uiHandleBtnData *data)
{
  const double value = btn_value_get(byn);
  int value_toggle;
  if (b5->bit) {
    value_toggle = BITBTN_VALUE_TOGGLED(int(value), btn->bitnr);
  }
  else {
    value_toggle = (value == 0.0);
  }

  btn_value_set(btn, double(value_toggle));
  if (ELEM(btn->type, BTYPE_ICON_TOGGLE, BTYPE_ICON_TOGGLE_N)) {
    btn_update_edited(btn);
  }

  btn_apply_fn(C, btn);

  data->retval = btn->retval;
  data->applied = true;
}

static void ui_apply_btn_ROW(Cxt *C, uiBlock *block, Btn *btn, uiHandleBtnData *data)
{
  ui_btn_value_set(btn, btn->hardmax);

  ui_apply_btn_fn(C, btn);

  /* states of other row btns */
  LIST_FOREACH (Btn *, bt, &block->btns) {
    if (bt != btn && bt->ptr == btn->ptr && ELEM(bt->type, BTYPE_ROW, BTYPE_LISTROW)) {
      btn_update_edited(bt);
    }
  }

  data->retval = btn->retval;
  data->applied = true;
}

static void ui_apply_btn_VIEW_ITEM(Cxt *C,
                                   uiBlock *block,
                                   uiBtn *btn,
                                   uiHandleBtnData *data)
{
  if (data->apply_through_extra_icon) {
    /* Don't apply this, it would cause unintended tree-row toggling when clicking on extra icons.
     */
    return;
  }
  ui_apply_btn_ROW(C, block, btn, data);
}

/* note Ownership of props is moved here. The uiAfterFn owns it now.
 *
 * param cxt_btn: The btn to use cxt from when calling or polling the op.
 *
 * returns true if the op was ex'd, otherwise false. */
static bool ui_list_invoke_item_op(Cxt *C,
                                         const Btn *cxt_btn,
                                         WinOpType *ot,
                                         ApiPtr **props)
{
  if (!btn_cxt_poll_op(C, ot, cxt_btn)) {
    return false;
  }

  /* Allow the context to be set from the hovered button, so the list item draw callback can set
   * context for the operators. */
  ui_handle_afterfn_add_op_ex(ot, props, WIN_OP_INVOKE_DEFAULT, cxt_btn);
  return true;
}

static void ui_apply_but_LISTROW(Cxt *C, uiBlock *block, Btn *btn, uiHandleBtnData *data)
{
  Btn *listbox = ui_list_find_from_row(data->region, btn);
  if (listbox) {
    uiList *list = static_cast<uiList *>(listbox->custom_data);
    if (list && list->dyn_data->custom_activate_optype) {
      ui_list_invoke_item_operator(
          C, but, list->dyn_data->custom_activate_optype, &list->dyn_data->custom_activate_opptr);
    }
  }

  ui_apply_btn_ROW(C, block, but, data);
}

static void ui_apply_but_TEX(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  if (!data->str) {
    return;
  }

  ui_but_string_set(C, but, data->str);
  ui_but_update_edited(but);

  /* give butfunc a copy of the original text too.
   * feature used for bone renaming, channels, etc.
   * afterfunc frees rename_orig */
  if (data->origstr && (but->flag & UI_BUT_TEXTEDIT_UPDATE)) {
    /* In this case, we need to keep origstr available,
     * to restore real org string in case we cancel after having typed something already. */
    but->rename_orig = BLI_strdup(data->origstr);
  }
  /* only if there are afterfuncs, otherwise 'renam_orig' isn't freed */
  else if (ui_afterfunc_check(but->block, but)) {
    but->rename_orig = data->origstr;
    data->origstr = nullptr;
  }

  void *orig_arg2 = but->func_arg2;

  /* If arg2 isn't in use already, pass the active search item through it. */
  if ((but->func_arg2 == nullptr) && (but->type == UI_BTYPE_SEARCH_MENU)) {
    uiButSearch *search_but = (uiButSearch *)but;
    but->func_arg2 = search_but->item_active;
    if ((U.flag & USER_FLAG_RECENT_SEARCHES_DISABLE) == 0) {
      blender::ui::string_search::add_recent_search(search_but->item_active_str);
    }
  }

  ui_apply_but_func(C, but);

  but->func_arg2 = orig_arg2;

  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but_TAB(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  if (data->str) {
    ui_but_string_set(C, but, data->str);
    ui_but_update_edited(but);
  }
  else {
    ui_but_value_set(but, but->hardmax);
    ui_apply_but_func(C, but);
  }

  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but_NUM(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  if (data->str) {
    /* This is intended to avoid unnecessary updates when the value stays the same, however there
     * are issues with the current implementation. It does not work with multi-button editing
     * (#89996) or operator popups where a number button requires an update even if the value is
     * unchanged (#89996).
     *
     * Trying to detect changes at this level is not reliable. Instead it could be done at the
     * level of RNA update/set, skipping RNA update if RNA set did not change anything, instead
     * of skipping all button updates. */
#if 0
    double value;
    /* Check if the string value is a number and cancel if it's equal to the startvalue. */
    if (ui_but_string_eval_number(C, but, data->str, &value) && (value == data->startvalue)) {
      data->cancel = true;
      return;
    }
#endif

    if (ui_but_string_set(C, but, data->str)) {
      data->value = ui_but_value_get(but);
    }
    else {
      data->cancel = true;
      return;
    }
  }
  else {
    ui_but_value_set(but, data->value);
  }

  ui_but_update_edited(but);
  ui_apply_but_func(C, but);

  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but_VEC(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  ui_but_v3_set(but, data->vec);
  ui_but_update_edited(but);
  ui_apply_but_func(C, but);

  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but_COLORBAND(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  ui_apply_but_func(C, but);
  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but_CURVE(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  ui_apply_but_func(C, but);
  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but_CURVEPROFILE(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  ui_apply_but_func(C, but);
  data->retval = but->retval;
  data->applied = true;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Button Drag Multi-Number
 * \{ */

#ifdef USE_DRAG_MULTINUM

/* small multi-but api */
static void ui_multibut_add(uiHandleButtonData *data, uiBut *but)
{
  BLI_assert(but->flag & UI_BUT_DRAG_MULTI);
  BLI_assert(data->multi_data.has_mbuts);

  uiButMultiState *mbut_state = MEM_cnew<uiButMultiState>(__func__);
  mbut_state->but = but;
  mbut_state->origvalue = ui_but_value_get(but);
#  ifdef USE_ALLSELECT
  mbut_state->select_others.is_copy = data->select_others.is_copy;
#  endif

  BLI_linklist_prepend(&data->multi_data.mbuts, mbut_state);

  UI_butstore_register(data->multi_data.bs_mbuts, &mbut_state->but);
}

static uiButMultiState *ui_multibut_lookup(uiHandleButtonData *data, const uiBut *but)
{
  for (LinkNode *l = data->multi_data.mbuts; l; l = l->next) {
    uiButMultiState *mbut_state = static_cast<uiButMultiState *>(l->link);

    if (mbut_state->but == but) {
      return mbut_state;
    }
  }

  return nullptr;
}

static void ui_multibut_restore(bContext *C, uiHandleButtonData *data, uiBlock *block)
{
  LISTBASE_FOREACH (uiBut *, but, &block->buttons) {
    if (but->flag & UI_BUT_DRAG_MULTI) {
      uiButMultiState *mbut_state = ui_multibut_lookup(data, but);
      if (mbut_state) {
        ui_but_value_set(but, mbut_state->origvalue);

#  ifdef USE_ALLSELECT
        if (mbut_state->select_others.elems_len > 0) {
          ui_selectcontext_apply(
              C, but, &mbut_state->select_others, mbut_state->origvalue, mbut_state->origvalue);
        }
#  else
        UNUSED_VARS(C);
#  endif
      }
    }
  }
}

static void ui_multibut_free(uiHandleButtonData *data, uiBlock *block)
{
#  ifdef USE_ALLSELECT
  if (data->multi_data.mbuts) {
    LinkNode *list = data->multi_data.mbuts;
    while (list) {
      LinkNode *next = list->next;
      uiButMultiState *mbut_state = static_cast<uiButMultiState *>(list->link);

      if (mbut_state->select_others.elems) {
        MEM_freeN(mbut_state->select_others.elems);
      }

      MEM_freeN(list->link);
      MEM_freeN(list);
      list = next;
    }
  }
#  else
  BLI_linklist_freeN(data->multi_data.mbuts);
#  endif

  data->multi_data.mbuts = nullptr;

  if (data->multi_data.bs_mbuts) {
    UI_butstore_free(block, data->multi_data.bs_mbuts);
    data->multi_data.bs_mbuts = nullptr;
  }
}

static bool ui_multibut_states_tag(uiBut *but_active,
                                   uiHandleButtonData *data,
                                   const wmEvent *event)
{
  float seg[2][2];
  bool changed = false;

  seg[0][0] = data->multi_data.drag_start[0];
  seg[0][1] = data->multi_data.drag_start[1];

  seg[1][0] = event->xy[0];
  seg[1][1] = event->xy[1];

  BLI_assert(data->multi_data.init == uiHandleButtonMulti::INIT_SETUP);

  ui_window_to_block_fl(data->region, but_active->block, &seg[0][0], &seg[0][1]);
  ui_window_to_block_fl(data->region, but_active->block, &seg[1][0], &seg[1][1]);

  data->multi_data.has_mbuts = false;

  /* follow ui_but_find_mouse_over_ex logic */
  LISTBASE_FOREACH (uiBut *, but, &but_active->block->buttons) {
    bool drag_prev = false;
    bool drag_curr = false;

    /* re-set each time */
    if (but->flag & UI_BUT_DRAG_MULTI) {
      but->flag &= ~UI_BUT_DRAG_MULTI;
      drag_prev = true;
    }

    if (ui_but_is_interactive(but, false)) {

      /* drag checks */
      if (but_active != but) {
        if (ui_but_is_compatible(but_active, but)) {

          BLI_assert(but->active == nullptr);

          /* finally check for overlap */
          if (BLI_rctf_isect_segment(&but->rect, seg[0], seg[1])) {

            but->flag |= UI_BUT_DRAG_MULTI;
            data->multi_data.has_mbuts = true;
            drag_curr = true;
          }
        }
      }
    }

    changed |= (drag_prev != drag_curr);
  }

  return changed;
}

static void ui_multibut_states_create(uiBut *but_active, uiHandleButtonData *data)
{
  BLI_assert(data->multi_data.init == uiHandleButtonMulti::INIT_SETUP);
  BLI_assert(data->multi_data.has_mbuts);

  data->multi_data.bs_mbuts = UI_butstore_create(but_active->block);

  LISTBASE_FOREACH (uiBut *, but, &but_active->block->buttons) {
    if (but->flag & UI_BUT_DRAG_MULTI) {
      ui_multibut_add(data, but);
    }
  }

  /* Edit buttons proportionally to each other.
   * NOTE: if we mix buttons which are proportional and others which are not,
   * this may work a bit strangely. */
  if ((but_active->rnaprop && (RNA_property_flag(but_active->rnaprop) & PROP_PROPORTIONAL)) ||
      ELEM(but_active->unit_type, RNA_SUBTYPE_UNIT_VALUE(PROP_UNIT_LENGTH)))
  {
    if (data->origvalue != 0.0) {
      data->multi_data.is_proportional = true;
    }
  }
}

static void ui_multibut_states_apply(bContext *C, uiHandleButtonData *data, uiBlock *block)
{
  ARegion *region = data->region;
  const double value_delta = data->value - data->origvalue;
  const double value_scale = data->multi_data.is_proportional ? (data->value / data->origvalue) :
                                                                0.0;

  BLI_assert(data->multi_data.init == uiHandleButtonMulti::INIT_ENABLE);
  BLI_assert(data->multi_data.skip == false);

  LISTBASE_FOREACH (uiBut *, but, &block->buttons) {
    if (!(but->flag & UI_BUT_DRAG_MULTI)) {
      continue;
    }

    BtnMultiState *mbtn_state = ui_multibut_lookup(data, but);

    if (mbut_state == nullptr) {
      /* Highly unlikely. */
      printf("%s: Can't find btn\n", __func__);
      /* While this avoids crashing, multi-button dragging will fail,
       * which is still a bug from the user perspective. See #83651. */
      continue;
    }

    void *active_back;
    btn_ex_begin(C, region, but, &active_back);

#  ifdef USE_ALLSELECT
    if (data->sel_others.is_enabled) {
      /* init once! */
      if (mbut_state->sel_others.elems_len == 0) {
        ui_selectcontext_begin(C, btn, &mbtn_state->sel_others);
      }
      if (mbtn_state->select_others.elems_len == 0) {
        mbtn_state->select_others.elems_len = -1;
      }
    }

    /* Needed so we apply the right deltas. */
    btn->active->origvalue = mbut_state->origvalue;
    btn->active->select_others = mbut_state->select_others;
    btn->active->select_others.do_free = false;
#  endif

    lib_assert(active_back == nullptr);
    /* No need to check 'data->state' here. */
    if (data->str) {
      /* Entering text (set all). */
      btn->active->value = data->value;
      btn_string_set(C, but, data->str);
    }
    else {
      /* Dragging (use delta). */
      if (data->multi_data.is_proportional) {
        btn->active->value = mbut_state->origvalue * value_scale;
      }
      else {
        btn->active->value = mbut_state->origvalue + value_delta;
      }

      /* Clamp based on soft limits, see #40154. */
      CLAMP(btn->active->value, double(btn->softmin), double(but->softmax));
    }

    btn_ex_end(C, rgn, btn, active_back);
  }
}

#endif /* USE_DRAG_MULTINUM */

/* Btn Drag Toggle */
#ifdef USE_DRAG_TOGGLE

/* Helpers that wrap bool fns, to support different kinds of btns. */
static bool btn_drag_toggle_is_supported(const Btn *btn)
{
  if (btn->flag & BTN_DISABLED) {
    return false;
  }
  if (btn_is_bool(btn)) {
    return true;
  }
  if (btn_is_decorator(btn)) {
    return ELEM(but->icon,
                ICON_DECORATE,
                ICON_DECORATE_KEYFRAME,
                ICON_DECORATE_ANIM,
                ICON_DECORATE_OVERRIDE);
  }
  return false;
}

/* Button pushed state to compare if other btns match. Can be more
 * then just true or false for toggle btns with more than 2 states. */
static int btn_drag_toggle_pushed_state(Btn *btn)
{
  if (btn->apiptr.data == nullptr && btn->ptr == nullptr && btn->icon) {
    /* Assume icon ids a unique state, for btns that
     * work through fns cbs and don't have an bool
     * value that indicates the state. */
    return btn->icon + btn->iconadd;
  }
  if (btn_is_bool(btn)) {
    return btn_is_pushed(btn);
  }
  return 0;
}

struct uiDragToggleHandle {
  /* init */
  int pushed_state;
  float but_cent_start[2];

  bool is_xy_lock_init;
  bool xy_lock[2];

  int xy_init[2];
  int xy_last[2];
};

static bool ui_drag_toggle_set_xy_xy(
    Cxt *C, ARgn *rgn, const int pushed_state, const int xy_src[2], const int xy_dst[2])
{
  /* popups such as layers won't re-evaluate on redraw */
  const bool do_check = (rgn->rgntype == RGN_TYPE_TEMPORARY);
  bool changed = false;

  LIST_FOREACH (uiBlock *, block, &rgn->uiblocks) {
    float xy_a_block[2] = {float(xy_src[0]), float(xy_src[1])};
    float xy_b_block[2] = {float(xy_dst[0]), float(xy_dst[1])};

    ui_win_to_block_fl(region, block, &xy_a_block[0], &xy_a_block[1]);
    ui_win_to_block_fl(region, block, &xy_b_block[0], &xy_b_block[1]);

    LIST_FOREACH (Btn *, btn, &block->btns) {
      /* NOTE: ctrl is always true here because (at least for now)
       * we always want to consider text control in this case, even when not embossed. */

      if (!btn_is_interactive(btn, true)) {
        continue;
      }
      if (!lib_rctf_isect_segment(&btn->rect, xy_a_block, xy_b_block)) {
        continue;
      }
      if (!ui_drag_toggle_btn_is_supported(but)) {
        continue;
      }
      /* is it pressed? */
      const int pushed_state_btn = ui_drag_toggle_btn_pushed_state(btn);
      if (pushed_state_btn == pushed_state) {
        continue;
      }

      /* execute the btn */
      btn_ex(C, rgn, btn);
      if (do_check) {
        btn_update_edited(btn);
      }
      if (U.runtime.is_dirty == false) {
        btn_update_prefs_dirty(btn);
      }
      changed = true;
    }
  }

  if (changed) {
    /* apply now, not on release (or if handlers are canceled for whatever reason) */
    btn_apply_fns_after(C);
  }

  return changed;
}

static void ui_drag_toggle_set(Cxt *C, uiDragToggleHandle *drag_info, const int xy_input[2])
{
  ARgn *rgn = cxt_win_region(C);
  bool do_draw = false;

  /* Initialize Locking:
   *
   * Check if we need to initialize the lock axis by finding if the first
   * btn we mouse over is X or Y aligned, then lock the mouse to that axis after.  */
  if (drag_info->is_xy_lock_init == false) {
    /* first store the buttons original coords */
    Btn *btn = btn_find_mouse_over_ex(rgn, xy_input, true, false, nullptr, nullptr);

    if (btn) {
      if (btn->flag & BTN_DRAG_LOCK) {
        const float btn_cent_new[2] = {
            lib_rctf_cent_x(&btn->rect),
            lib_rctf_cent_y(&btn->rect),
        };

        /* check if this is a different btn,
         * chances are high the btn won't move about :) */
        if (len_manhattan_v2v2(drag_info->tn_cent_start, btn_cent_new) > 1.0f) {
          if (fabsf(drag_info->btn_cent_start[0] - btn_cent_new[0]) <
              fabsf(drag_info->btn_cent_start[1] - btn_cent_new[1]))
          {
            drag_info->xy_lock[0] = true;
          }
          else {
            drag_info->xy_lock[1] = true;
          }
          drag_info->is_xy_lock_init = true;
        }
      }
      else {
        drag_info->is_xy_lock_init = true;
      }
    }
  }
  /* done with axis locking */

  int xy[2];
  xy[0] = (drag_info->xy_lock[0] == false) ? xy_input[0] : drag_info->xy_last[0];
  xy[1] = (drag_info->xy_lock[1] == false) ? xy_input[1] : drag_info->xy_last[1];

  /* touch all buttons between last mouse coord and this one */
  do_draw = ui_drag_toggle_set_xy_xy(C, rgn, drag_info->pushed_state, drag_info->xy_last, xy);

  if (do_draw) {
    ed_rgn_tag_redraw(region);
  }

  copy_v2_v2_int(drag_info->xy_last, xy);
}

static void ui_handler_rgn_drag_toggle_remove(Cxt * /*C*/, void *userdata)
{
  uiDragToggleHandle *drag_info = static_cast<uiDragToggleHandle *>(userdata);
  mem_free(drag_info);
}

static int ui_handler_rgn_drag_toggle(Cxt *C, const WinEv *ev, void *userdata)
{
  uiDragToggleHandle *drag_info = static_cast<uiDragToggleHandle *>(userdata);
  bool done = false;

  switch (ev->type) {
    case LEFTMOUSE: {
      if (ev->val == KM_RELEASE) {
        done = true;
      }
      break;
    }
    case MOUSEMOVE: {
      ui_drag_toggle_set(C, drag_info, ev->xy);
      break;
    }
  }

  if (done) {
    Win *win = cxt_win(C);
    const ARgn *rhn = cxt_win_rgn(C);
    Btn *btn = btn_find_mouse_over_ex(
        region, drag_info->xy_init, true, false, nullptr, nullptr);

    if (btn) {
      btn_apply_undo(btn);
    }

    win_ev_remove_ui_handler(&win->modalhandlers,
                               ui_handler_region_drag_toggle,
                               ui_handler_region_drag_toggle_remove,
                               drag_info,
                               false);
    ui_handler_rgn_drag_toggle_remove(C, drag_info);

    win_ev_add_mousemove(win);
    return WM_UI_HANDLER_BREAK;
  }
  return WM_UI_HANDLER_CONTINUE;
}

static bool btn_is_drag_toggle(const Btn *btn)
{
  return ((ui_drag_toggle_btn_is_supported(btn) == true) &&
          /* Menu check is important so the btn dragged over isn't removed instantly. */
          (ui_block_is_menu(but->block) == false));
}

#endif /* USE_DRAG_TOGGLE */

#ifdef USE_ALLSEL

static bool ui_selcxt_begin(Cxt *C, Btn *btn, uiSelCxtStore *selcxt_data)
{
  ApiPtr lptr;
  ApiProp *lprop;
  bool success = false;

  char *path = nullptr;
  List list = {nullptr};

  ApiPtr ptr = btn->apiptr;
  ApiProp *prop = btn->apiprop;
  const int index = btn->apiindex;

  /* for now don't support whole colors */
  if (index == -1) {
    return false;
  }

  /* if there is a valid property that is editable... */
  if (ptr.data && prop) {
    bool use_path_from_id;

    /* some facts we want to know */
    const bool is_array = api_prop_array_check(prop);
    const int api_type = api_prop_type(prop);

    if (ui_cxt_copy_to_selected_list(C, &ptr, prop, &lb, &use_path_from_id, &path) &&
        !lib_list_is_empty(&lb))
    {
      selcxt_data->elems_len = lib_list_count(&list);
      selcxt_data->elems = static_cast<uiSelCxtElem *>(
          mem_malloc(sizeof(uiSelCxtElem) * selcxt_data->elems_len, __func__));
      int i;
      LIST_FOREACH_INDEX (CollectionPtrLink *, link, &lb, i) {
        if (i >= selcxt_data->elems_len) {
          break;
        }

        if (!ui_cxt_copy_to_selected_check(
                &ptr, &link->ptr, prop, path, use_path_from_id, &lptr, &lprop))
        {
          selcxt_data->elems_len -= 1;
          i -= 1;
          continue;
        }

        uiSelCxtElem *other = &selcxt_data->elems[i];
        other->ptr = lptr;
        if (is_array) {
          if (api_type == PROP_FLOAT) {
            other->val_f = api_prop_float_get_index(&lptr, lprop, index);
          }
          else if (api_type == PROP_INT) {
            other->val_i = api_prop_int_get_index(&lptr, lprop, index);
          }
          /* ignored for now */
#  if 0
          else if (api_type == PROP_BOOL) {
            other->val_b = api_prop_bool_get_index(&lptr, lprop, index);
          }
#  endif
        }
        else {
          if (api_type == PROP_FLOAT) {
            other->val_f = api_prop_float_get(&lptr, lprop);
          }
          else if (api_type == PROP_INT) {
            other->val_i = api_prop_int_get(&lptr, lprop);
          }
          /* ignored for now */
#  if 0
          else if (api_type == PROP_BOOL) {
            other->val_b = api_prop_bool_get(&lptr, lprop);
          }
          else if (rna_type == PROP_ENUM) {
            other->val_i = api_prop_enum_get(&lptr, lprop);
          }
#  endif
        }
      }
      success = (selcxt_data->elems_len != 0);
    }
  }

  if (selcxt_data->elems_len == 0) {
    MEM_SAFE_FREE(selcxt_data->elems);
  }

  MEM_SAFE_FREE(path);
  lib_freelist(&list);

  /* caller can clear */
  selcxt_data->do_free = true;

  if (success) {
    btn->flag |= BTN_IS_SEL_CXT;
  }

  return success;
}
