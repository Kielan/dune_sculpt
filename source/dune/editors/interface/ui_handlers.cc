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

static void ui_selectcontext_end(uiBut *but, uiSelectContextStore *selctx_data)
{
  if (selctx_data->do_free) {
    if (selctx_data->elems) {
      MEM_freeN(selctx_data->elems);
    }
  }

  but->flag &= ~UI_BUT_IS_SELECT_CONTEXT;
}

static void ui_selectcontext_apply(bContext *C,
                                   uiBut *but,
                                   uiSelectContextStore *selctx_data,
                                   const double value,
                                   const double value_orig)
{
  if (selctx_data->elems) {
    PropertyRNA *prop = but->rnaprop;
    PropertyRNA *lprop = but->rnaprop;
    const int index = but->rnaindex;
    const bool use_delta = (selctx_data->is_copy == false);

    union {
      bool b;
      int i;
      float f;
      char *str;
      PointerRNA p;
    } delta, min, max;

    const bool is_array = RNA_property_array_check(prop);
    const int rna_type = RNA_property_type(prop);

    if (rna_type == PROP_FLOAT) {
      delta.f = use_delta ? (value - value_orig) : value;
      RNA_property_float_range(&but->rnapoin, prop, &min.f, &max.f);
    }
    else if (rna_type == PROP_INT) {
      delta.i = use_delta ? (int(value) - int(value_orig)) : int(value);
      RNA_property_int_range(&but->rnapoin, prop, &min.i, &max.i);
    }
    else if (rna_type == PROP_ENUM) {
      /* Not a delta in fact. */
      delta.i = RNA_property_enum_get(&but->rnapoin, prop);
    }
    else if (rna_type == PROP_BOOLEAN) {
      if (is_array) {
        /* Not a delta in fact. */
        delta.b = RNA_property_boolean_get_index(&but->rnapoin, prop, index);
      }
      else {
        /* Not a delta in fact. */
        delta.b = RNA_property_boolean_get(&but->rnapoin, prop);
      }
    }
    else if (rna_type == PROP_POINTER) {
      /* Not a delta in fact. */
      delta.p = RNA_property_pointer_get(&but->rnapoin, prop);
    }
    else if (rna_type == PROP_STRING) {
      /* Not a delta in fact. */
      delta.str = RNA_property_string_get_alloc(&but->rnapoin, prop, nullptr, 0, nullptr);
    }

#  ifdef USE_ALLSELECT_LAYER_HACK
    /* make up for not having 'handle_layer_buttons' */
    {
      const PropertySubType subtype = RNA_property_subtype(prop);

      if ((rna_type == PROP_BOOLEAN) && ELEM(subtype, PROP_LAYER, PROP_LAYER_MEMBER) && is_array &&
          /* could check for 'handle_layer_buttons' */
          but->func)
      {
        wmWindow *win = CTX_wm_window(C);
        if ((win->eventstate->modifier & KM_SHIFT) == 0) {
          const int len = RNA_property_array_length(&but->rnapoin, prop);
          bool *tmparray = static_cast<bool *>(MEM_callocN(sizeof(bool) * len, __func__));

          tmparray[index] = true;

          for (int i = 0; i < selctx_data->elems_len; i++) {
            uiSelectContextElem *other = &selctx_data->elems[i];
            PointerRNA lptr = other->ptr;
            RNA_property_boolean_set_array(&lptr, lprop, tmparray);
            RNA_property_update(C, &lptr, lprop);
          }

          MEM_freeN(tmparray);

          return;
        }
      }
    }
#  endif

    for (int i = 0; i < selctx_data->elems_len; i++) {
      uiSelectContextElem *other = &selctx_data->elems[i];
      PointerRNA lptr = other->ptr;

      if (rna_type == PROP_FLOAT) {
        float other_value = use_delta ? (other->val_f + delta.f) : delta.f;
        CLAMP(other_value, min.f, max.f);
        if (is_array) {
          RNA_property_float_set_index(&lptr, lprop, index, other_value);
        }
        else {
          RNA_property_float_set(&lptr, lprop, other_value);
        }
      }
      else if (rna_type == PROP_INT) {
        int other_value = use_delta ? (other->val_i + delta.i) : delta.i;
        CLAMP(other_value, min.i, max.i);
        if (is_array) {
          RNA_property_int_set_index(&lptr, lprop, index, other_value);
        }
        else {
          RNA_property_int_set(&lptr, lprop, other_value);
        }
      }
      else if (rna_type == PROP_BOOLEAN) {
        const bool other_value = delta.b;
        if (is_array) {
          RNA_property_boolean_set_index(&lptr, lprop, index, other_value);
        }
        else {
          RNA_property_boolean_set(&lptr, lprop, delta.b);
        }
      }
      else if (rna_type == PROP_ENUM) {
        const int other_value = delta.i;
        BLI_assert(!is_array);
        RNA_property_enum_set(&lptr, lprop, other_value);
      }
      else if (rna_type == PROP_POINTER) {
        const PointerRNA other_value = delta.p;
        RNA_property_pointer_set(&lptr, lprop, other_value, nullptr);
      }
      else if (rna_type == PROP_STRING) {
        const char *other_value = delta.str;
        RNA_property_string_set(&lptr, lprop, other_value);
      }

      RNA_property_update(C, &lptr, prop);
    }
    if (rna_type == PROP_STRING) {
      MEM_freeN(delta.str);
    }
  }
}

#endif /* USE_ALLSELECT */

/** \} */

/* -------------------------------------------------------------------- */
/** \name Button Drag
 * \{ */

static bool ui_but_drag_init(bContext *C,
                             uiBut *but,
                             uiHandleButtonData *data,
                             const wmEvent *event)
{
  /* prevent other WM gestures to start while we try to drag */
  WM_gestures_remove(CTX_wm_window(C));

  /* Clamp the maximum to half the UI unit size so a high user preference
   * doesn't require the user to drag more than half the default button height. */
  const int drag_threshold = min_ii(
      WM_event_drag_threshold(event),
      int((UI_UNIT_Y / 2) * ui_block_to_window_scale(data->region, but->block)));

  if (abs(data->dragstartx - event->xy[0]) + abs(data->dragstarty - event->xy[1]) > drag_threshold)
  {
    button_activate_state(C, but, BUTTON_STATE_EXIT);
    data->cancel = true;
#ifdef USE_DRAG_TOGGLE
    if (ui_drag_toggle_but_is_supported(but)) {
      uiDragToggleHandle *drag_info = MEM_cnew<uiDragToggleHandle>(__func__);
      ARegion *region_prev;

      /* call here because regular mouse-up event won't run,
       * typically 'button_activate_exit()' handles this */
      ui_apply_but_autokey(C, but);

      drag_info->pushed_state = ui_drag_toggle_but_pushed_state(but);
      drag_info->but_cent_start[0] = BLI_rctf_cent_x(&but->rect);
      drag_info->but_cent_start[1] = BLI_rctf_cent_y(&but->rect);
      copy_v2_v2_int(drag_info->xy_init, event->xy);
      copy_v2_v2_int(drag_info->xy_last, event->xy);

      /* needed for toggle drag on popups */
      region_prev = CTX_wm_region(C);
      CTX_wm_region_set(C, data->region);

      WM_event_add_ui_handler(C,
                              &data->window->modalhandlers,
                              ui_handler_region_drag_toggle,
                              ui_handler_region_drag_toggle_remove,
                              drag_info,
                              WM_HANDLER_BLOCKING);

      CTX_wm_region_set(C, region_prev);

      /* Initialize alignment for single row/column regions,
       * otherwise we use the relative position of the first other button dragged over. */
      if (ELEM(data->region->regiontype,
               RGN_TYPE_NAV_BAR,
               RGN_TYPE_HEADER,
               RGN_TYPE_TOOL_HEADER,
               RGN_TYPE_FOOTER,
               RGN_TYPE_ASSET_SHELF_HEADER))
      {
        const int region_alignment = RGN_ALIGN_ENUM_FROM_MASK(data->region->alignment);
        int lock_axis = -1;

        if (ELEM(region_alignment, RGN_ALIGN_LEFT, RGN_ALIGN_RIGHT)) {
          lock_axis = 0;
        }
        else if (ELEM(region_alignment, RGN_ALIGN_TOP, RGN_ALIGN_BOTTOM)) {
          lock_axis = 1;
        }
        if (lock_axis != -1) {
          drag_info->xy_lock[lock_axis] = true;
          drag_info->is_xy_lock_init = true;
        }
      }
    }
    else
#endif
        if (but->type == UI_BTYPE_COLOR)
    {
      bool valid = false;
      uiDragColorHandle *drag_info = MEM_cnew<uiDragColorHandle>(__func__);

      /* TODO: support more button pointer types. */
      if (but->rnaprop && RNA_property_subtype(but->rnaprop) == PROP_COLOR_GAMMA) {
        ui_but_v3_get(but, drag_info->color);
        drag_info->gamma_corrected = true;
        valid = true;
      }
      else if (but->rnaprop && RNA_property_subtype(but->rnaprop) == PROP_COLOR) {
        ui_but_v3_get(but, drag_info->color);
        drag_info->gamma_corrected = false;
        valid = true;
      }
      else if (ELEM(but->pointype, UI_BUT_POIN_FLOAT, UI_BUT_POIN_CHAR)) {
        ui_but_v3_get(but, drag_info->color);
        copy_v3_v3(drag_info->color, (float *)but->poin);
        valid = true;
      }

      if (valid) {
        WM_event_start_drag(C, ICON_COLOR, WM_DRAG_COLOR, drag_info, 0.0, WM_DRAG_FREE_DATA);
      }
      else {
        MEM_freeN(drag_info);
        return false;
      }
    }
    else if (but->type == UI_BTYPE_VIEW_ITEM) {
      const uiButViewItem *view_item_but = (uiButViewItem *)but;
      if (view_item_but->view_item) {
        return UI_view_item_drag_start(C, view_item_but->view_item);
      }
    }
    else {
      ui_but_drag_start(C, but);
    }
    return true;
  }

  return false;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Button Apply
 * \{ */

static void ui_apply_but_IMAGE(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  ui_apply_but_func(C, but);
  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but_HISTOGRAM(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  ui_apply_but_func(C, but);
  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but_WAVEFORM(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  ui_apply_but_func(C, but);
  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but_TRACKPREVIEW(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  ui_apply_but_func(C, but);
  data->retval = but->retval;
  data->applied = true;
}

static void ui_apply_but(
    bContext *C, uiBlock *block, uiBut *but, uiHandleButtonData *data, const bool interactive)
{
  const eButType but_type = but->type; /* Store as const to quiet maybe uninitialized warning. */

  data->retval = 0;

  /* if we cancel and have not applied yet, there is nothing to do,
   * otherwise we have to restore the original value again */
  if (data->cancel) {
    if (!data->applied) {
      return;
    }

    if (data->str) {
      MEM_freeN(data->str);
    }
    data->str = data->origstr;
    data->origstr = nullptr;
    data->value = data->origvalue;
    copy_v3_v3(data->vec, data->origvec);
    /* postpone clearing origdata */
  }
  else {
    /* We avoid applying interactive edits a second time
     * at the end with the #uiHandleButtonData.applied_interactive flag. */
    if (interactive) {
      data->applied_interactive = true;
    }
    else if (data->applied_interactive) {
      return;
    }

#ifdef USE_ALLSELECT
#  ifdef USE_DRAG_MULTINUM
    if (but->flag & UI_BUT_DRAG_MULTI) {
      /* pass */
    }
    else
#  endif
        if (data->select_others.elems_len == 0)
    {
      wmWindow *win = CTX_wm_window(C);
      wmEvent *event = win->eventstate;
      /* May have been enabled before activating, don't do for array pasting. */
      if (data->select_others.is_enabled || IS_ALLSELECT_EVENT(event)) {
        /* See comment for #IS_ALLSELECT_EVENT why this needs to be filtered here. */
        const bool is_array_paste = (event->val == KM_PRESS) &&
                                    (event->modifier & (KM_CTRL | KM_OSKEY)) &&
                                    (event->modifier & KM_SHIFT) == 0 && (event->type == EVT_VKEY);
        if (!is_array_paste) {
          ui_selectcontext_begin(C, but, &data->select_others);
          data->select_others.is_enabled = true;
        }
      }
    }
    if (data->select_others.elems_len == 0) {
      /* Don't check again. */
      data->select_others.elems_len = -1;
    }
#endif
  }

  /* ensures we are writing actual values */
  char *editstr = but->editstr;
  double *editval = but->editval;
  float *editvec = but->editvec;
  ColorBand *editcoba;
  CurveMapping *editcumap;
  CurveProfile *editprofile;
  if (but_type == UI_BTYPE_COLORBAND) {
    uiButColorBand *but_coba = (uiButColorBand *)but;
    editcoba = but_coba->edit_coba;
  }
  else if (but_type == UI_BTYPE_CURVE) {
    uiButCurveMapping *but_cumap = (uiButCurveMapping *)but;
    editcumap = but_cumap->edit_cumap;
  }
  else if (but_type == UI_BTYPE_CURVEPROFILE) {
    uiButCurveProfile *but_profile = (uiButCurveProfile *)but;
    editprofile = but_profile->edit_profile;
  }
  but->editstr = nullptr;
  but->editval = nullptr;
  but->editvec = nullptr;
  if (but_type == UI_BTYPE_COLORBAND) {
    uiButColorBand *but_coba = (uiButColorBand *)but;
    but_coba->edit_coba = nullptr;
  }
  else if (but_type == UI_BTYPE_CURVE) {
    uiButCurveMapping *but_cumap = (uiButCurveMapping *)but;
    but_cumap->edit_cumap = nullptr;
  }
  else if (but_type == UI_BTYPE_CURVEPROFILE) {
    uiButCurveProfile *but_profile = (uiButCurveProfile *)but;
    but_profile->edit_profile = nullptr;
  }

  /* handle different types */
  switch (but_type) {
    case UI_BTYPE_BUT:
    case UI_BTYPE_DECORATOR:
      ui_apply_but_BUT(C, but, data);
      break;
    case UI_BTYPE_TEXT:
    case UI_BTYPE_SEARCH_MENU:
      ui_apply_but_TEX(C, but, data);
      break;
    case UI_BTYPE_BUT_TOGGLE:
    case UI_BTYPE_TOGGLE:
    case UI_BTYPE_TOGGLE_N:
    case UI_BTYPE_ICON_TOGGLE:
    case UI_BTYPE_ICON_TOGGLE_N:
    case UI_BTYPE_CHECKBOX:
    case UI_BTYPE_CHECKBOX_N:
      ui_apply_but_TOG(C, but, data);
      break;
    case UI_BTYPE_ROW:
      ui_apply_but_ROW(C, block, but, data);
      break;
    case UI_BTYPE_VIEW_ITEM:
      ui_apply_but_VIEW_ITEM(C, block, but, data);
      break;
    case UI_BTYPE_LISTROW:
      ui_apply_but_LISTROW(C, block, but, data);
      break;
    case UI_BTYPE_TAB:
      ui_apply_but_TAB(C, but, data);
      break;
    case UI_BTYPE_SCROLL:
    case UI_BTYPE_GRIP:
    case UI_BTYPE_NUM:
    case UI_BTYPE_NUM_SLIDER:
      ui_apply_but_NUM(C, but, data);
      break;
    case UI_BTYPE_MENU:
    case UI_BTYPE_BLOCK:
    case UI_BTYPE_PULLDOWN:
      ui_apply_but_BLOCK(C, but, data);
      break;
    case UI_BTYPE_COLOR:
      if (data->cancel) {
        ui_apply_but_VEC(C, but, data);
      }
      else {
        ui_apply_but_BLOCK(C, but, data);
      }
      break;
    case UI_BTYPE_BUT_MENU:
      ui_apply_but_BUTM(C, but, data);
      break;
    case UI_BTYPE_UNITVEC:
    case UI_BTYPE_HSVCUBE:
    case UI_BTYPE_HSVCIRCLE:
      ui_apply_but_VEC(C, but, data);
      break;
    case UI_BTYPE_COLORBAND:
      ui_apply_but_COLORBAND(C, but, data);
      break;
    case UI_BTYPE_CURVE:
      ui_apply_but_CURVE(C, but, data);
      break;
    case UI_BTYPE_CURVEPROFILE:
      ui_apply_but_CURVEPROFILE(C, but, data);
      break;
    case UI_BTYPE_KEY_EVENT:
    case UI_BTYPE_HOTKEY_EVENT:
      ui_apply_but_BUT(C, but, data);
      break;
    case UI_BTYPE_IMAGE:
      ui_apply_but_IMAGE(C, but, data);
      break;
    case UI_BTYPE_HISTOGRAM:
      ui_apply_but_HISTOGRAM(C, but, data);
      break;
    case UI_BTYPE_WAVEFORM:
      ui_apply_but_WAVEFORM(C, but, data);
      break;
    case UI_BTYPE_TRACK_PREVIEW:
      ui_apply_but_TRACKPREVIEW(C, but, data);
      break;
    default:
      break;
  }

#ifdef USE_DRAG_MULTINUM
  if (data->multi_data.has_mbuts) {
    if ((data->multi_data.init == uiHandleButtonMulti::INIT_ENABLE) &&
        (data->multi_data.skip == false))
    {
      if (data->cancel) {
        ui_multibut_restore(C, data, block);
      }
      else {
        ui_multibut_states_apply(C, data, block);
      }
    }
  }
#endif

#ifdef USE_ALLSELECT
  ui_selectcontext_apply(C, but, &data->select_others, data->value, data->origvalue);
#endif

  if (data->cancel) {
    data->origvalue = 0.0;
    zero_v3(data->origvec);
  }

  but->editstr = editstr;
  but->editval = editval;
  but->editvec = editvec;
  if (but_type == UI_BTYPE_COLORBAND) {
    uiButColorBand *but_coba = (uiButColorBand *)but;
    but_coba->edit_coba = editcoba;
  }
  else if (but_type == UI_BTYPE_CURVE) {
    uiButCurveMapping *but_cumap = (uiButCurveMapping *)but;
    but_cumap->edit_cumap = editcumap;
  }
  else if (but_type == UI_BTYPE_CURVEPROFILE) {
    uiButCurveProfile *but_profile = (uiButCurveProfile *)but;
    but_profile->edit_profile = editprofile;
  }

  if (data->custom_interaction_handle != nullptr) {
    ui_block_interaction_update(
        C, &block->custom_interaction_callbacks, data->custom_interaction_handle);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Button Copy & Paste
 * \{ */

static void ui_but_get_pasted_text_from_clipboard(const bool ensure_utf8,
                                                  char **r_buf_paste,
                                                  int *r_buf_len)
{
  /* get only first line even if the clipboard contains multiple lines */
  int length;
  char *text = WM_clipboard_text_get_firstline(false, ensure_utf8, &length);

  if (text) {
    *r_buf_paste = text;
    *r_buf_len = length;
  }
  else {
    *r_buf_paste = static_cast<char *>(MEM_callocN(sizeof(char), __func__));
    *r_buf_len = 0;
  }
}

static int get_but_property_array_length(uiBut *but)
{
  return RNA_property_array_length(&but->rnapoin, but->rnaprop);
}

static void ui_but_set_float_array(
    bContext *C, uiBut *but, uiHandleButtonData *data, const float *values, const int values_len)
{
  button_activate_state(C, but, BUTTON_STATE_NUM_EDITING);

  for (int i = 0; i < values_len; i++) {
    RNA_property_float_set_index(&but->rnapoin, but->rnaprop, i, values[i]);
  }
  if (data) {
    if (but->type == UI_BTYPE_UNITVEC) {
      BLI_assert(values_len == 3);
      copy_v3_v3(data->vec, values);
    }
    else {
      data->value = values[but->rnaindex];
    }
  }

  button_activate_state(C, but, BUTTON_STATE_EXIT);
}

static void float_array_to_string(const float *values,
                                  const int values_len,
                                  char *output,
                                  int output_maxncpy)
{
  const int values_end = values_len - 1;
  int ofs = 0;
  output[ofs++] = '[';
  for (int i = 0; i < values_len; i++) {
    ofs += BLI_snprintf_rlen(
        output + ofs, output_maxncpy - ofs, (i != values_end) ? "%f, " : "%f]", values[i]);
  }
}

static void ui_but_copy_numeric_array(uiBut *but, char *output, int output_maxncpy)
{
  const int values_len = get_but_property_array_length(but);
  blender::Array<float, 16> values(values_len);
  RNA_property_float_get_array(&but->rnapoin, but->rnaprop, values.data());
  float_array_to_string(values.data(), values_len, output, output_maxncpy);
}

static bool parse_float_array(char *text, float *values, int values_len_expected)
{
  /* can parse max 4 floats for now */
  BLI_assert(0 <= values_len_expected && values_len_expected <= 4);

  float v[5];
  const int values_len_actual = sscanf(
      text, "[%f, %f, %f, %f, %f]", &v[0], &v[1], &v[2], &v[3], &v[4]);

  if (values_len_actual == values_len_expected) {
    memcpy(values, v, sizeof(float) * values_len_expected);
    return true;
  }
  return false;
}

static void ui_but_paste_numeric_array(bContext *C,
                                       uiBut *but,
                                       uiHandleButtonData *data,
                                       char *buf_paste)
{
  const int values_len = get_but_property_array_length(but);
  if (values_len > 4) {
    /* not supported for now */
    return;
  }

  blender::Array<float, 16> values(values_len);

  if (parse_float_array(buf_paste, values.data(), values_len)) {
    ui_but_set_float_array(C, but, data, values.data(), values_len);
  }
  else {
    WM_report(RPT_ERROR, "Expected an array of numbers: [n, n, ...]");
  }
}

static void ui_but_copy_numeric_value(uiBut *but, char *output, int output_maxncpy)
{
  /* Get many decimal places, then strip trailing zeros.
   * NOTE: too high values start to give strange results. */
  ui_but_string_get_ex(but, output, output_maxncpy, UI_PRECISION_FLOAT_MAX, false, nullptr);
  BLI_str_rstrip_float_zero(output, '\0');
}

static void ui_but_paste_numeric_value(bContext *C,
                                       uiBut *but,
                                       uiHandleButtonData *data,
                                       char *buf_paste)
{
  double value;
  if (ui_but_string_eval_number(C, but, buf_paste, &value)) {
    button_activate_state(C, but, BUTTON_STATE_NUM_EDITING);
    data->value = value;
    ui_but_string_set(C, but, buf_paste);
    button_activate_state(C, but, BUTTON_STATE_EXIT);
  }
  else {
    WM_report(RPT_ERROR, "Expected a number");
  }
}

static void ui_but_paste_normalized_vector(bContext *C,
                                           uiBut *but,
                                           uiHandleButtonData *data,
                                           char *buf_paste)
{
  float xyz[3];
  if (parse_float_array(buf_paste, xyz, 3)) {
    if (normalize_v3(xyz) == 0.0f) {
      /* better set Z up then have a zero vector */
      xyz[2] = 1.0;
    }
    ui_but_set_float_array(C, but, data, xyz, 3);
  }
  else {
    WM_report(RPT_ERROR, "Paste expected 3 numbers, formatted: '[n, n, n]'");
  }
}

static void ui_but_copy_color(uiBut *but, char *output, int output_maxncpy)
{
  float rgba[4];

  if (but->rnaprop && get_but_property_array_length(but) == 4) {
    rgba[3] = RNA_property_float_get_index(&but->rnapoin, but->rnaprop, 3);
  }
  else {
    rgba[3] = 1.0f;
  }

  ui_but_v3_get(but, rgba);

  /* convert to linear color to do compatible copy between gamma and non-gamma */
  if (but->rnaprop && RNA_property_subtype(but->rnaprop) == PROP_COLOR_GAMMA) {
    srgb_to_linearrgb_v3_v3(rgba, rgba);
  }

  float_array_to_string(rgba, 4, output, output_maxncpy);
}

static void ui_but_paste_color(bContext *C, uiBut *but, char *buf_paste)
{
  float rgba[4];
  if (parse_float_array(buf_paste, rgba, 4)) {
    if (but->rnaprop) {
      /* Assume linear colors in buffer. */
      if (RNA_property_subtype(but->rnaprop) == PROP_COLOR_GAMMA) {
        linearrgb_to_srgb_v3_v3(rgba, rgba);
      }

      /* Some color properties are RGB, not RGBA. */
      const int array_len = get_but_property_array_length(but);
      BLI_assert(ELEM(array_len, 3, 4));
      ui_but_set_float_array(C, but, nullptr, rgba, array_len);
    }
  }
  else {
    WM_report(RPT_ERROR, "Paste expected 4 numbers, formatted: '[n, n, n, n]'");
  }
}

static void ui_but_copy_text(uiBut *but, char *output, int output_maxncpy)
{
  ui_but_string_get(but, output, output_maxncpy);
}

static void ui_but_paste_text(bContext *C, uiBut *but, uiHandleButtonData *data, char *buf_paste)
{
  BLI_assert(but->active == data);
  UNUSED_VARS_NDEBUG(data);
  ui_but_set_string_interactive(C, but, buf_paste);
}

static void ui_but_copy_colorband(uiBut *but)
{
  if (but->poin != nullptr) {
    memcpy(&but_copypaste_coba, but->poin, sizeof(ColorBand));
  }
}

static void ui_but_paste_colorband(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  if (but_copypaste_coba.tot != 0) {
    if (!but->poin) {
      but->poin = reinterpret_cast<char *>(MEM_cnew<ColorBand>(__func__));
    }

    button_activate_state(C, but, BUTTON_STATE_NUM_EDITING);
    memcpy(data->coba, &but_copypaste_coba, sizeof(ColorBand));
    button_activate_state(C, but, BUTTON_STATE_EXIT);
  }
}

static void ui_but_copy_curvemapping(uiBut *but)
{
  if (but->poin != nullptr) {
    but_copypaste_curve_alive = true;
    BKE_curvemapping_free_data(&but_copypaste_curve);
    BKE_curvemapping_copy_data(&but_copypaste_curve, (CurveMapping *)but->poin);
  }
}

static void ui_but_paste_curvemapping(bContext *C, uiBut *but)
{
  if (but_copypaste_curve_alive) {
    if (!but->poin) {
      but->poin = reinterpret_cast<char *>(MEM_cnew<CurveMapping>(__func__));
    }

    button_activate_state(C, but, BUTTON_STATE_NUM_EDITING);
    BKE_curvemapping_free_data((CurveMapping *)but->poin);
    BKE_curvemapping_copy_data((CurveMapping *)but->poin, &but_copypaste_curve);
    button_activate_state(C, but, BUTTON_STATE_EXIT);
  }
}

static void ui_but_copy_CurveProfile(uiBut *but)
{
  if (but->poin != nullptr) {
    but_copypaste_profile_alive = true;
    BKE_curveprofile_free_data(&but_copypaste_profile);
    BKE_curveprofile_copy_data(&but_copypaste_profile, (CurveProfile *)but->poin);
  }
}

static void ui_but_paste_CurveProfile(bContext *C, uiBut *but)
{
  if (but_copypaste_profile_alive) {
    if (!but->poin) {
      but->poin = reinterpret_cast<char *>(MEM_cnew<CurveProfile>(__func__));
    }

    button_activate_state(C, but, BUTTON_STATE_NUM_EDITING);
    BKE_curveprofile_free_data((CurveProfile *)but->poin);
    BKE_curveprofile_copy_data((CurveProfile *)but->poin, &but_copypaste_profile);
    button_activate_state(C, but, BUTTON_STATE_EXIT);
  }
}

static void ui_but_copy_operator(bContext *C, uiBut *but, char *output, int output_maxncpy)
{
  PointerRNA *opptr = UI_but_operator_ptr_get(but);

  char *str;
  str = WM_operator_pystring_ex(C, nullptr, false, true, but->optype, opptr);
  BLI_strncpy(output, str, output_maxncpy);
  MEM_freeN(str);
}

static bool ui_but_copy_menu(uiBut *but, char *output, int output_maxncpy)
{
  MenuType *mt = UI_but_menutype_get(but);
  if (mt) {
    BLI_snprintf(output, output_maxncpy, "bpy.ops.wm.call_menu(name=\"%s\")", mt->idname);
    return true;
  }
  return false;
}

static bool ui_but_copy_popover(uiBut *but, char *output, int output_maxncpy)
{
  PanelType *pt = UI_but_paneltype_get(but);
  if (pt) {
    BLI_snprintf(output, output_maxncpy, "bpy.ops.wm.call_panel(name=\"%s\")", pt->idname);
    return true;
  }
  return false;
}

static void ui_but_copy(bContext *C, uiBut *but, const bool copy_array)
{
  if (ui_but_contains_password(but)) {
    return;
  }

  /* Arbitrary large value (allow for paths: 'PATH_MAX') */
  char buf[4096] = {0};
  const int buf_maxncpy = sizeof(buf);

  /* Left false for copying internal data (color-band for eg). */
  bool is_buf_set = false;

  const bool has_required_data = !(but->poin == nullptr && but->rnapoin.data == nullptr);

  switch (but->type) {
    case UI_BTYPE_NUM:
    case UI_BTYPE_NUM_SLIDER:
      if (!has_required_data) {
        break;
      }
      if (copy_array && ui_but_has_array_value(but)) {
        ui_but_copy_numeric_array(but, buf, buf_maxncpy);
      }
      else {
        ui_but_copy_numeric_value(but, buf, buf_maxncpy);
      }
      is_buf_set = true;
      break;

    case UI_BTYPE_UNITVEC:
      if (!has_required_data) {
        break;
      }
      ui_but_copy_numeric_array(but, buf, buf_maxncpy);
      is_buf_set = true;
      break;

    case UI_BTYPE_COLOR:
      if (!has_required_data) {
        break;
      }
      ui_but_copy_color(but, buf, buf_maxncpy);
      is_buf_set = true;
      break;

    case UI_BTYPE_TEXT:
    case UI_BTYPE_SEARCH_MENU:
      if (!has_required_data) {
        break;
      }
      ui_but_copy_text(but, buf, buf_maxncpy);
      is_buf_set = true;
      break;

    case UI_BTYPE_COLORBAND:
      ui_but_copy_colorband(but);
      break;

    case UI_BTYPE_CURVE:
      ui_but_copy_curvemapping(but);
      break;

    case UI_BTYPE_CURVEPROFILE:
      ui_but_copy_CurveProfile(but);
      break;

    case UI_BTYPE_BUT:
      if (!but->optype) {
        break;
      }
      ui_but_copy_operator(C, but, buf, buf_maxncpy);
      is_buf_set = true;
      break;

    case UI_BTYPE_MENU:
    case UI_BTYPE_PULLDOWN:
      if (ui_but_copy_menu(but, buf, buf_maxncpy)) {
        is_buf_set = true;
      }
      break;
    case UI_BTYPE_POPOVER:
      if (ui_but_copy_popover(but, buf, buf_maxncpy)) {
        is_buf_set = true;
      }
      break;

    default:
      break;
  }

  if (is_buf_set) {
    WM_clipboard_text_set(buf, false);
  }
}

static void ui_but_paste(bContext *C, uiBut *but, uiHandleButtonData *data, const bool paste_array)
{
  BLI_assert((but->flag & UI_BUT_DISABLED) == 0); /* caller should check */

  int buf_paste_len = 0;
  char *buf_paste;
  ui_but_get_pasted_text_from_clipboard(UI_but_is_utf8(but), &buf_paste, &buf_paste_len);

  const bool has_required_data = !(but->poin == nullptr && but->rnapoin.data == nullptr);

  switch (but->type) {
    case UI_BTYPE_NUM:
    case UI_BTYPE_NUM_SLIDER:
      if (!has_required_data) {
        break;
      }
      if (paste_array && ui_but_has_array_value(but)) {
        ui_but_paste_numeric_array(C, but, data, buf_paste);
      }
      else {
        ui_but_paste_numeric_value(C, but, data, buf_paste);
      }
      break;

    case UI_BTYPE_UNITVEC:
      if (!has_required_data) {
        break;
      }
      ui_but_paste_normalized_vector(C, but, data, buf_paste);
      break;

    case UI_BTYPE_COLOR:
      if (!has_required_data) {
        break;
      }
      ui_but_paste_color(C, but, buf_paste);
      break;

    case UI_BTYPE_TEXT:
    case UI_BTYPE_SEARCH_MENU:
      if (!has_required_data) {
        break;
      }
      ui_but_paste_text(C, but, data, buf_paste);
      break;

    case UI_BTYPE_COLORBAND:
      ui_but_paste_colorband(C, but, data);
      break;

    case UI_BTYPE_CURVE:
      ui_but_paste_curvemapping(C, but);
      break;

    case UI_BTYPE_CURVEPROFILE:
      ui_but_paste_CurveProfile(C, but);
      break;

    default:
      break;
  }

  MEM_freeN((void *)buf_paste);
}

void ui_but_clipboard_free()
{
  BKE_curvemapping_free_data(&but_copypaste_curve);
  BKE_curveprofile_free_data(&but_copypaste_profile);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Button Text Password
 *
 * Functions to convert password strings that should not be displayed
 * to asterisk representation (e.g. 'mysecretpasswd' -> '*************')
 *
 * It converts every UTF-8 character to an asterisk, and also remaps
 * the cursor position and selection start/end.
 *
 * \note remapping is used, because password could contain UTF-8 characters.
 *
 * \{ */

static int ui_text_position_from_hidden(uiBut *but, int pos)
{
  const char *butstr = (but->editstr) ? but->editstr : but->drawstr;
  const char *strpos = butstr;
  const char *str_end = butstr + strlen(butstr);
  for (int i = 0; i < pos; i++) {
    strpos = BLI_str_find_next_char_utf8(strpos, str_end);
  }

  return (strpos - butstr);
}

static int ui_text_position_to_hidden(uiBut *but, int pos)
{
  const char *butstr = (but->editstr) ? but->editstr : but->drawstr;
  return BLI_strnlen_utf8(butstr, pos);
}

void ui_but_text_password_hide(char password_str[UI_MAX_PASSWORD_STR],
                               uiBut *but,
                               const bool restore)
{
  if (!(but->rnaprop && RNA_property_subtype(but->rnaprop) == PROP_PASSWORD)) {
    return;
  }

  char *butstr = (but->editstr) ? but->editstr : but->drawstr;

  if (restore) {
    /* restore original string */
    BLI_strncpy(butstr, password_str, UI_MAX_PASSWORD_STR);

    /* remap cursor positions */
    if (but->pos >= 0) {
      but->pos = ui_text_position_from_hidden(but, but->pos);
      but->selsta = ui_text_position_from_hidden(but, but->selsta);
      but->selend = ui_text_position_from_hidden(but, but->selend);
    }
  }
  else {
    /* convert text to hidden text using asterisks (e.g. pass -> ****) */
    const size_t len = BLI_strlen_utf8(butstr);

    /* remap cursor positions */
    if (but->pos >= 0) {
      but->pos = ui_text_position_to_hidden(but, but->pos);
      but->selsta = ui_text_position_to_hidden(but, but->selsta);
      but->selend = ui_text_position_to_hidden(but, but->selend);
    }

    /* save original string */
    BLI_strncpy(password_str, butstr, UI_MAX_PASSWORD_STR);
    memset(butstr, '*', len);
    butstr[len] = '\0';
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Button Text Selection/Editing
 * \{ */

void ui_but_set_string_interactive(bContext *C, uiBut *but, const char *value)
{
  /* Caller should check. */
  BLI_assert((but->flag & UI_BUT_DISABLED) == 0);

  button_activate_state(C, but, BUTTON_STATE_TEXT_EDITING);
  ui_textedit_string_set(but, but->active, value);

  if (but->type == UI_BTYPE_SEARCH_MENU && but->active) {
    but->changed = true;
    ui_searchbox_update(C, but->active->searchbox, but, true);
  }

  button_activate_state(C, but, BUTTON_STATE_EXIT);
}

void ui_but_active_string_clear_and_exit(bContext *C, uiBut *but)
{
  if (!but->active) {
    return;
  }

  /* most likely nullptr, but let's check, and give it temp zero string */
  if (!but->active->str) {
    but->active->str = static_cast<char *>(MEM_callocN(1, "temp str"));
  }
  but->active->str[0] = 0;

  ui_apply_but_TEX(C, but, but->active);
  button_activate_state(C, but, BUTTON_STATE_EXIT);
}

static void ui_textedit_string_ensure_max_length(uiBut *but,
                                                 uiHandleButtonData *data,
                                                 int str_maxncpy)
{
  BLI_assert(data->is_str_dynamic);
  BLI_assert(data->str == but->editstr);

  if (str_maxncpy > data->str_maxncpy) {
    data->str = but->editstr = static_cast<char *>(
        MEM_reallocN(data->str, sizeof(char) * str_maxncpy));
    data->str_maxncpy = str_maxncpy;
  }
}

static void ui_textedit_string_set(uiBut *but, uiHandleButtonData *data, const char *str)
{
  if (data->is_str_dynamic) {
    ui_textedit_string_ensure_max_length(but, data, strlen(str) + 1);
  }

  if (UI_but_is_utf8(but)) {
    BLI_strncpy_utf8(data->str, str, data->str_maxncpy);
  }
  else {
    BLI_strncpy(data->str, str, data->str_maxncpy);
  }
}

static bool ui_textedit_delete_selection(uiBut *but, uiHandleButtonData *data)
{
  char *str = data->str;
  const int len = strlen(str);
  bool changed = false;
  if (but->selsta != but->selend && len) {
    memmove(str + but->selsta, str + but->selend, (len - but->selend) + 1);
    changed = true;
  }

  but->pos = but->selend = but->selsta;
  return changed;
}

/**
 * \param x: Screen space cursor location - #wmEvent.x
 *
 * \note `but->block->aspect` is used here, so drawing button style is getting scaled too.
 */
static void ui_textedit_set_cursor_pos(uiBut *but, uiHandleButtonData *data, const float x)
{
  /* XXX pass on as arg. */
  uiFontStyle fstyle = UI_style_get()->widget;
  const float aspect = but->block->aspect;

  float startx = but->rect.xmin;
  float starty_dummy = 0.0f;
  char password_str[UI_MAX_PASSWORD_STR];
  /* treat 'str_last' as null terminator for str, no need to modify in-place */
  const char *str = but->editstr, *str_last;

  ui_block_to_window_fl(data->region, but->block, &startx, &starty_dummy);

  ui_fontscale(&fstyle.points, aspect);

  UI_fontstyle_set(&fstyle);

  ui_but_text_password_hide(password_str, but, false);

  if (ELEM(but->type, UI_BTYPE_TEXT, UI_BTYPE_SEARCH_MENU)) {
    if (but->flag & UI_HAS_ICON) {
      startx += UI_ICON_SIZE / aspect;
    }
  }
  startx += (UI_TEXT_MARGIN_X * U.widget_unit - U.pixelsize) / aspect;

  /* mouse dragged outside the widget to the left */
  if (x < startx) {
    int i = but->ofs;

    str_last = &str[but->ofs];

    while (i > 0) {
      if (BLI_str_cursor_step_prev_utf8(str, but->ofs, &i)) {
        /* 0.25 == scale factor for less sensitivity */
        if (BLF_width(fstyle.uifont_id, str + i, (str_last - str) - i) > (startx - x) * 0.25f) {
          break;
        }
      }
      else {
        break; /* unlikely but possible */
      }
    }
    but->ofs = i;
    but->pos = but->ofs;
  }
  /* mouse inside the widget, mouse coords mapped in widget space */
  else {
    but->pos = but->ofs + BLF_str_offset_from_cursor_position(
                              fstyle.uifont_id, str + but->ofs, INT_MAX, int(x - startx));
  }

  ui_but_text_password_hide(password_str, but, true);
}

static void ui_textedit_set_cursor_select(uiBut *but, uiHandleButtonData *data, const float x)
{
  ui_textedit_set_cursor_pos(but, data, x);

  but->selsta = but->pos;
  but->selend = data->sel_pos_init;
  if (but->selend < but->selsta) {
    std::swap(but->selsta, but->selend);
  }

  ui_but_update(but);
}

/**
 * This is used for both utf8 and ascii
 *
 * For unicode buttons, \a buf is treated as unicode.
 */
static bool ui_textedit_insert_buf(uiBut *but,
                                   uiHandleButtonData *data,
                                   const char *buf,
                                   int buf_len)
{
  int len = strlen(data->str);
  const int str_maxncpy_new = len - (but->selend - but->selsta) + 1;
  bool changed = false;

  if (data->is_str_dynamic) {
    ui_textedit_string_ensure_max_length(but, data, str_maxncpy_new + buf_len);
  }

  if (str_maxncpy_new <= data->str_maxncpy) {
    char *str = data->str;
    size_t step = buf_len;

    /* type over the current selection */
    if ((but->selend - but->selsta) > 0) {
      changed = ui_textedit_delete_selection(but, data);
      len = strlen(str);
    }

    if ((len + step >= data->str_maxncpy) && (data->str_maxncpy - (len + 1) > 0)) {
      if (UI_but_is_utf8(but)) {
        /* Shorten 'step' to a utf8 aligned size that fits. */
        BLI_strnlen_utf8_ex(buf, data->str_maxncpy - (len + 1), &step);
      }
      else {
        step = data->str_maxncpy - (len + 1);
      }
    }

    if (step && (len + step < data->str_maxncpy)) {
      memmove(&str[but->pos + step], &str[but->pos], (len + 1) - but->pos);
      memcpy(&str[but->pos], buf, step * sizeof(char));
      but->pos += step;
      changed = true;
    }
  }

  return changed;
}

#ifdef WITH_INPUT_IME
static bool ui_textedit_insert_ascii(uiBut *but, uiHandleButtonData *data, const char ascii)
{
  BLI_assert(isascii(ascii));
  const char buf[2] = {ascii, '\0'};
  return ui_textedit_insert_buf(but, data, buf, sizeof(buf) - 1);
}
#endif

static void ui_textedit_move(uiBut *but,
                             uiHandleButtonData *data,
                             eStrCursorJumpDirection direction,
                             const bool select,
                             eStrCursorJumpType jump)
{
  const char *str = data->str;
  const int len = strlen(str);
  const int pos_prev = but->pos;
  const bool has_sel = (but->selend - but->selsta) > 0;

  ui_but_update(but);

  /* special case, quit selection and set cursor */
  if (has_sel && !select) {
    if (jump == STRCUR_JUMP_ALL) {
      but->selsta = but->selend = but->pos = direction ? len : 0;
    }
    else {
      if (direction) {
        but->selsta = but->pos = but->selend;
      }
      else {
        but->pos = but->selend = but->selsta;
      }
    }
    data->sel_pos_init = but->pos;
  }
  else {
    int pos_i = but->pos;
    BLI_str_cursor_step_utf8(str, len, &pos_i, direction, jump, true);
    but->pos = pos_i;

    if (select) {
      if (has_sel == false) {
        /* Holding shift but with no previous selection. */
        but->selsta = but->pos;
        but->selend = pos_prev;
      }
      else if (but->selsta == pos_prev) {
        /* Previous selection, extending start position. */
        but->selsta = but->pos;
      }
      else {
        /* Previous selection, extending end position. */
        but->selend = but->pos;
      }
    }
    if (but->selend < but->selsta) {
      std::swap(but->selsta, but->selend);
    }
  }
}

static bool ui_textedit_delete(uiBut *but,
                               uiHandleButtonData *data,
                               eStrCursorJumpDirection direction,
                               eStrCursorJumpType jump)
{
  char *str = data->str;
  const int len = strlen(str);

  bool changed = false;

  if (jump == STRCUR_JUMP_ALL) {
    if (len) {
      changed = true;
    }
    str[0] = '\0';
    but->pos = 0;
  }
  else if (direction) { /* delete */
    if ((but->selend - but->selsta) > 0) {
      changed = ui_textedit_delete_selection(but, data);
    }
    else if (but->pos >= 0 && but->pos < len) {
      int pos = but->pos;
      int step;
      BLI_str_cursor_step_utf8(str, len, &pos, direction, jump, true);
      step = pos - but->pos;
      memmove(&str[but->pos], &str[but->pos + step], (len + 1) - (but->pos + step));
      changed = true;
    }
  }
  else { /* backspace */
    if (len != 0) {
      if ((but->selend - but->selsta) > 0) {
        changed = ui_textedit_delete_selection(but, data);
      }
      else if (but->pos > 0) {
        int pos = but->pos;
        int step;

        BLI_str_cursor_step_utf8(str, len, &pos, direction, jump, true);
        step = but->pos - pos;
        memmove(&str[but->pos - step], &str[but->pos], (len + 1) - but->pos);
        but->pos -= step;
        changed = true;
      }
    }
  }

  return changed;
}

static int ui_textedit_autocomplete(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  char *str = data->str;

  int changed;
  if (data->searchbox) {
    changed = ui_searchbox_autocomplete(C, data->searchbox, but, data->str);
  }
  else {
    changed = but->autocomplete_func(C, str, but->autofunc_arg);
  }

  but->pos = strlen(str);
  but->selsta = but->selend = but->pos;

  return changed;
}

/* mode for ui_textedit_copypaste() */
enum {
  UI_TEXTEDIT_PASTE = 1,
  UI_TEXTEDIT_COPY,
  UI_TEXTEDIT_CUT,
};

static bool ui_textedit_copypaste(uiBut *but, uiHandleButtonData *data, const int mode)
{
  bool changed = false;

  /* paste */
  if (mode == UI_TEXTEDIT_PASTE) {
    /* extract the first line from the clipboard */
    int buf_len;
    char *pbuf = WM_clipboard_text_get_firstline(false, UI_but_is_utf8(but), &buf_len);

    if (pbuf) {
      ui_textedit_insert_buf(but, data, pbuf, buf_len);

      changed = true;

      MEM_freeN(pbuf);
    }
  }
  /* cut & copy */
  else if (ELEM(mode, UI_TEXTEDIT_COPY, UI_TEXTEDIT_CUT)) {
    /* copy the contents to the copypaste buffer */
    const int sellen = but->selend - but->selsta;
    char *buf = static_cast<char *>(
        MEM_mallocN(sizeof(char) * (sellen + 1), "ui_textedit_copypaste"));

    memcpy(buf, data->str + but->selsta, sellen);
    buf[sellen] = '\0';

    WM_clipboard_text_set(buf, false);
    MEM_freeN(buf);

    /* for cut only, delete the selection afterwards */
    if (mode == UI_TEXTEDIT_CUT) {
      if ((but->selend - but->selsta) > 0) {
        changed = ui_textedit_delete_selection(but, data);
      }
    }
  }

  return changed;
}

#ifdef WITH_INPUT_IME
/* Enable IME, and setup #uiBut IME data. */
static void ui_textedit_ime_begin(wmWindow *win, uiBut * /*but*/)
{
  /* XXX Is this really needed? */
  int x, y;

  BLI_assert(win->ime_data == nullptr);

  /* enable IME and position to cursor, it's a trick */
  x = win->eventstate->xy[0];
  /* flip y and move down a bit, prevent the IME panel cover the edit button */
  y = win->eventstate->xy[1] - 12;

  wm_window_IME_begin(win, x, y, 0, 0, true);
}

/* Disable IME, and clear #uiBut IME data. */
static void ui_textedit_ime_end(wmWindow *win, uiBut * /*but*/)
{
  wm_window_IME_end(win);
}

void ui_but_ime_reposition(uiBut *but, int x, int y, bool complete)
{
  BLI_assert(but->active);

  ui_region_to_window(but->active->region, &x, &y);
  wm_window_IME_begin(but->active->window, x, y - 4, 0, 0, complete);
}

const wmIMEData *ui_but_ime_data_get(uiBut *but)
{
  if (but->active && but->active->window) {
    return but->active->window->ime_data;
  }
  else {
    return nullptr;
  }
}
#endif /* WITH_INPUT_IME */

static void ui_textedit_begin(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  wmWindow *win = data->window;
  const bool is_num_but = ELEM(but->type, UI_BTYPE_NUM, UI_BTYPE_NUM_SLIDER);
  bool no_zero_strip = false;

  MEM_SAFE_FREE(data->str);

#ifdef USE_DRAG_MULTINUM
  /* this can happen from multi-drag */
  if (data->applied_interactive) {
    /* remove any small changes so canceling edit doesn't restore invalid value: #40538 */
    data->cancel = true;
    ui_apply_but(C, but->block, but, data, true);
    data->cancel = false;

    data->applied_interactive = false;
  }
#endif

#ifdef USE_ALLSELECT
  if (is_num_but) {
    if (IS_ALLSELECT_EVENT(win->eventstate)) {
      data->select_others.is_enabled = true;
      data->select_others.is_copy = true;
    }
  }
#endif

  /* retrieve string */
  data->str_maxncpy = ui_but_string_get_maxncpy(but);
  if (data->str_maxncpy != 0) {
    data->str = static_cast<char *>(MEM_callocN(sizeof(char) * data->str_maxncpy, "textedit str"));
    /* We do not want to truncate precision to default here, it's nice to show value,
     * not to edit it - way too much precision is lost then. */
    ui_but_string_get_ex(
        but, data->str, data->str_maxncpy, UI_PRECISION_FLOAT_MAX, true, &no_zero_strip);
  }
  else {
    data->is_str_dynamic = true;
    data->str = ui_but_string_get_dynamic(but, &data->str_maxncpy);
  }

  if (ui_but_is_float(but) && !ui_but_is_unit(but) &&
      !ui_but_anim_expression_get(but, nullptr, 0) && !no_zero_strip)
  {
    BLI_str_rstrip_float_zero(data->str, '\0');
  }

  if (is_num_but) {
    BLI_assert(data->is_str_dynamic == false);
    ui_but_convert_to_unit_alt_name(but, data->str, data->str_maxncpy);

    ui_numedit_begin_set_values(but, data);
  }

  /* won't change from now on */
  const int len = strlen(data->str);

  data->origstr = BLI_strdupn(data->str, len);
  data->sel_pos_init = 0;

  /* set cursor pos to the end of the text */
  but->editstr = data->str;
  but->pos = len;
  if (bool(but->flag2 & UI_BUT2_ACTIVATE_ON_INIT_NO_SELECT)) {
    but->selsta = len;
  }
  else {
    but->selsta = 0;
  }
  but->selend = len;

  /* Initialize undo history tracking. */
  data->undo_stack_text = ui_textedit_undo_stack_create();
  ui_textedit_undo_push(data->undo_stack_text, but->editstr, but->pos);

  /* optional searchbox */
  if (but->type == UI_BTYPE_SEARCH_MENU) {
    uiButSearch *search_but = (uiButSearch *)but;

    data->searchbox = search_but->popup_create_fn(C, data->region, search_but);
    ui_searchbox_update(C, data->searchbox, but, true); /* true = reset */
  }

  /* reset alert flag (avoid confusion, will refresh on exit) */
  but->flag &= ~UI_BUT_REDALERT;

  ui_but_update(but);

  /* Make sure the edited button is in view. */
  if (data->searchbox) {
    /* Popup blocks don't support moving after creation, so don't change the view for them. */
  }
  else if (UI_block_layout_needs_resolving(but->block)) {
    /* Layout isn't resolved yet (may happen when activating while drawing through
     * #UI_but_active_only()), so can't move it into view yet. This causes
     * #ui_but_update_view_for_active() to run after the layout is resolved. */
    but->changed = true;
  }
  else if ((but->block->flag & UI_BLOCK_CLIP_EVENTS) == 0) {
    /* Blocks with UI_BLOCK_CLIP_EVENTS are overlapping their region, so scrolling
     * that region to ensure it is in view can't work and causes issues. #97530 */
    UI_but_ensure_in_view(C, data->region, but);
  }

  WM_cursor_modal_set(win, WM_CURSOR_TEXT_EDIT);

  /* Temporarily turn off window auto-focus on platforms that support it. */
  GHOST_SetAutoFocus(false);

#ifdef WITH_INPUT_IME
  if (!is_num_but) {
    ui_textedit_ime_begin(win, but);
  }
#endif
}

static void ui_textedit_end(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  wmWindow *win = data->window;

  if (but) {
    if (UI_but_is_utf8(but)) {
      const int strip = BLI_str_utf8_invalid_strip(but->editstr, strlen(but->editstr));
      /* Strip non-UTF8 characters unless buttons support this.
       * This should never happen as all text input should be valid UTF8,
       * there is a small chance existing data contains invalid sequences.
       * This could check could be made into an assertion if `but->editstr`
       * is valid UTF8 when #ui_textedit_begin assigns the string. */
      if (strip) {
        printf("%s: invalid utf8 - stripped chars %d\n", __func__, strip);
      }
    }

    if (data->searchbox) {
      if (data->cancel == false) {
        BLI_assert(but->type == UI_BTYPE_SEARCH_MENU);
        uiButSearch *but_search = (uiButSearch *)but;

        if ((ui_searchbox_apply(but, data->searchbox) == false) &&
            (ui_searchbox_find_index(data->searchbox, but->editstr) == -1) &&
            !but_search->results_are_suggestions)
        {

          if (but->flag & UI_BUT_VALUE_CLEAR) {
            /* It is valid for _VALUE_CLEAR flavor to have no active element
             * (it's a valid way to unlink). */
            but->editstr[0] = '\0';
          }
          data->cancel = true;

          /* ensure menu (popup) too is closed! */
          data->escapecancel = true;

          WM_reportf(RPT_ERROR, "Failed to find '%s'", but->editstr);
          WM_report_banner_show(CTX_wm_manager(C), win);
        }
      }

      ui_searchbox_free(C, data->searchbox);
      data->searchbox = nullptr;
    }

    but->editstr = nullptr;
    but->pos = -1;
  }

  WM_cursor_modal_restore(win);

  /* Turn back on the auto-focusing of windows. */
  GHOST_SetAutoFocus(true);

  /* Free text undo history text blocks. */
  ui_textedit_undo_stack_destroy(data->undo_stack_text);
  data->undo_stack_text = nullptr;

#ifdef WITH_INPUT_IME
  /* See #wm_window_IME_end code-comments for details. */
#  if defined(WIN32) || defined(__APPLE__)
  if (win->ime_data)
#  endif
  {
    ui_textedit_ime_end(win, but);
  }
#endif
}

static void ui_textedit_next_but(uiBlock *block, uiBut *actbut, uiHandleButtonData *data)
{
  /* Label and round-box can overlap real buttons (backdrops...). */
  if (ELEM(actbut->type,
           UI_BTYPE_LABEL,
           UI_BTYPE_SEPR,
           UI_BTYPE_SEPR_LINE,
           UI_BTYPE_ROUNDBOX,
           UI_BTYPE_LISTBOX))
  {
    return;
  }

  for (uiBut *but = actbut->next; but; but = but->next) {
    if (ui_but_is_editable_as_text(but)) {
      if (!(but->flag & UI_BUT_DISABLED)) {
        data->postbut = but;
        data->posttype = BUTTON_ACTIVATE_TEXT_EDITING;
        return;
      }
    }
  }
  for (uiBut *but = static_cast<uiBut *>(block->buttons.first); but != actbut; but = but->next) {
    if (ui_but_is_editable_as_text(but)) {
      if (!(but->flag & UI_BUT_DISABLED)) {
        data->postbut = but;
        data->posttype = BUTTON_ACTIVATE_TEXT_EDITING;
        return;
      }
    }
  }
}

static void ui_textedit_prev_but(uiBlock *block, uiBut *actbut, uiHandleButtonData *data)
{
  /* Label and round-box can overlap real buttons (backdrops...). */
  if (ELEM(actbut->type,
           UI_BTYPE_LABEL,
           UI_BTYPE_SEPR,
           UI_BTYPE_SEPR_LINE,
           UI_BTYPE_ROUNDBOX,
           UI_BTYPE_LISTBOX))
  {
    return;
  }

  for (uiBut *but = actbut->prev; but; but = but->prev) {
    if (ui_but_is_editable_as_text(but)) {
      if (!(but->flag & UI_BUT_DISABLED)) {
        data->postbut = but;
        data->posttype = BUTTON_ACTIVATE_TEXT_EDITING;
        return;
      }
    }
  }
  for (uiBut *but = static_cast<uiBut *>(block->buttons.last); but != actbut; but = but->prev) {
    if (ui_but_is_editable_as_text(but)) {
      if (!(but->flag & UI_BUT_DISABLED)) {
        data->postbut = but;
        data->posttype = BUTTON_ACTIVATE_TEXT_EDITING;
        return;
      }
    }
  }
}

/**
 * Return the jump type used for cursor motion & back-space/delete actions.
 */
static eStrCursorJumpType ui_textedit_jump_type_from_event(const wmEvent *event)
{
/* TODO: Do not enable these Apple-specific modifiers until we also support them in
 * text objects, console, and text editor to keep everything consistent - Harley. */
#if defined(__APPLE__) && 0
  if (event->modifier & KM_OSKEY) {
    return STRCUR_JUMP_ALL;
  }
  if (event->modifier & KM_ALT) {
    return STRCUR_JUMP_DELIM;
  }
#else
  if (event->modifier & KM_CTRL) {
    return STRCUR_JUMP_DELIM;
  }
#endif
  return STRCUR_JUMP_NONE;
}

static void ui_do_but_textedit(
    bContext *C, uiBlock *block, uiBut *but, uiHandleButtonData *data, const wmEvent *event)
{
  int retval = WM_UI_HANDLER_CONTINUE;
  bool changed = false, inbox = false, update = false, skip_undo_push = false;

#ifdef WITH_INPUT_IME
  wmWindow *win = CTX_wm_window(C);
  const wmIMEData *ime_data = win->ime_data;
  const bool is_ime_composing = ime_data && win->ime_data_is_composing;
#else
  const bool is_ime_composing = false;
#endif

  switch (event->type) {
    case MOUSEMOVE:
    case MOUSEPAN:
      if (data->searchbox) {
#ifdef USE_KEYNAV_LIMIT
        if ((event->type == MOUSEMOVE) &&
            ui_mouse_motion_keynav_test(&data->searchbox_keynav_state, event))
        {
          /* pass */
        }
        else {
          ui_searchbox_event(C, data->searchbox, but, data->region, event);
        }
#else
        ui_searchbox_event(C, data->searchbox, but, data->region, event);
#endif
      }
      ui_do_but_extra_operator_icons_mousemove(but, data, event);

      break;
    case RIGHTMOUSE:
    case EVT_ESCKEY:
      if (event->val == KM_PRESS) {
        /* Support search context menu. */
        if (event->type == RIGHTMOUSE) {
          if (data->searchbox) {
            if (ui_searchbox_event(C, data->searchbox, but, data->region, event)) {
              /* Only break if the event was handled. */
              break;
            }
          }
        }

#ifdef WITH_INPUT_IME
        /* skips button handling since it is not wanted */
        if (is_ime_composing) {
          break;
        }
#endif
        data->cancel = true;
        data->escapecancel = true;
        button_activate_state(C, but, BUTTON_STATE_EXIT);
        retval = WM_UI_HANDLER_BREAK;
      }
      break;
    case LEFTMOUSE: {
      /* Allow clicks on extra icons while editing. */
      if (ui_do_but_extra_operator_icon(C, but, data, event)) {
        break;
      }

      const bool had_selection = but->selsta != but->selend;

      /* exit on LMB only on RELEASE for searchbox, to mimic other popups,
       * and allow multiple menu levels */
      if (data->searchbox) {
        inbox = ui_searchbox_inside(data->searchbox, event->xy);
      }

      bool is_press_in_button = false;
      if (ELEM(event->val, KM_PRESS, KM_DBL_CLICK)) {
        float mx = event->xy[0];
        float my = event->xy[1];
        ui_window_to_block_fl(data->region, block, &mx, &my);

        if (ui_but_contains_pt(but, mx, my)) {
          is_press_in_button = true;
        }
      }

      /* for double click: we do a press again for when you first click on button
       * (selects all text, no cursor pos) */
      if (ELEM(event->val, KM_PRESS, KM_DBL_CLICK)) {
        if (is_press_in_button) {
          ui_textedit_set_cursor_pos(but, data, event->xy[0]);
          but->selsta = but->selend = but->pos;
          data->sel_pos_init = but->pos;

          button_activate_state(C, but, BUTTON_STATE_TEXT_SELECTING);
          retval = WM_UI_HANDLER_BREAK;
        }
        else if (inbox == false) {
          /* if searchbox, click outside will cancel */
          if (data->searchbox) {
            data->cancel = data->escapecancel = true;
          }
          button_activate_state(C, but, BUTTON_STATE_EXIT);
          retval = WM_UI_HANDLER_BREAK;
        }
      }

      /* only select a word in button if there was no selection before */
      if (event->val == KM_DBL_CLICK && had_selection == false) {
        if (is_press_in_button) {
          const int str_len = strlen(data->str);
          /* This may not be necessary, additional check to ensure `pos` is never out of range,
           * since negative values aren't acceptable, see: #113154. */
          CLAMP(but->pos, 0, str_len);

          int selsta, selend;
          BLI_str_cursor_step_bounds_utf8(data->str, str_len, but->pos, &selsta, &selend);
          but->pos = short(selend);
          but->selsta = short(selsta);
          but->selend = short(selend);
          /* Anchor selection to the left side unless the last word. */
          data->sel_pos_init = ((selend == str_len) && (selsta != 0)) ? selend : selsta;
          retval = WM_UI_HANDLER_BREAK;
          changed = true;
        }
      }
      else if (inbox) {
        /* if we allow activation on key press,
         * it gives problems launching operators #35713. */
        if (event->val == KM_RELEASE) {
          button_activate_state(C, but, BUTTON_STATE_EXIT);
          retval = WM_UI_HANDLER_BREAK;
        }
      }
      break;
    }
  }

  if (event->val == KM_PRESS && !is_ime_composing) {
    switch (event->type) {
      case EVT_VKEY:
      case EVT_XKEY:
      case EVT_CKEY:
#if defined(__APPLE__)
        if (ELEM(event->modifier, KM_OSKEY, KM_CTRL))
#else
        if (event->modifier == KM_CTRL)
#endif
        {
          if (event->type == EVT_VKEY) {
            changed = ui_textedit_copypaste(but, data, UI_TEXTEDIT_PASTE);
          }
          else if (event->type == EVT_CKEY) {
            changed = ui_textedit_copypaste(but, data, UI_TEXTEDIT_COPY);
          }
          else if (event->type == EVT_XKEY) {
            changed = ui_textedit_copypaste(but, data, UI_TEXTEDIT_CUT);
          }

          retval = WM_UI_HANDLER_BREAK;
        }
        break;
      case EVT_RIGHTARROWKEY:
      case EVT_LEFTARROWKEY: {
        const eStrCursorJumpDirection direction = (event->type == EVT_RIGHTARROWKEY) ?
                                                      STRCUR_DIR_NEXT :
                                                      STRCUR_DIR_PREV;
        const eStrCursorJumpType jump = ui_textedit_jump_type_from_event(event);
        ui_textedit_move(but, data, direction, event->modifier & KM_SHIFT, jump);
        retval = WM_UI_HANDLER_BREAK;
        break;
      }
      case WHEELDOWNMOUSE:
      case EVT_DOWNARROWKEY:
        if (data->searchbox) {
#ifdef USE_KEYNAV_LIMIT
          ui_mouse_motion_keynav_init(&data->searchbox_keynav_state, event);
#endif
          ui_searchbox_event(C, data->searchbox, but, data->region, event);
          break;
        }
        if (event->type == WHEELDOWNMOUSE) {
          break;
        }
        ATTR_FALLTHROUGH;
      case EVT_ENDKEY:
        ui_textedit_move(but, data, STRCUR_DIR_NEXT, event->modifier & KM_SHIFT, STRCUR_JUMP_ALL);
        retval = WM_UI_HANDLER_BREAK;
        break;
      case WHEELUPMOUSE:
      case EVT_UPARROWKEY:
        if (data->searchbox) {
#ifdef USE_KEYNAV_LIMIT
          ui_mouse_motion_keynav_init(&data->searchbox_keynav_state, event);
#endif
          ui_searchbox_event(C, data->searchbox, but, data->region, event);
          break;
        }
        if (event->type == WHEELUPMOUSE) {
          break;
        }
        ATTR_FALLTHROUGH;
      case EVT_HOMEKEY:
        ui_textedit_move(but, data, STRCUR_DIR_PREV, event->modifier & KM_SHIFT, STRCUR_JUMP_ALL);
        retval = WM_UI_HANDLER_BREAK;
        break;
      case EVT_PADENTER:
      case EVT_RETKEY:
        button_activate_state(C, but, BUTTON_STATE_EXIT);
        retval = WM_UI_HANDLER_BREAK;
        break;
      case EVT_DELKEY:
      case EVT_BACKSPACEKEY: {
        const eStrCursorJumpDirection direction = (event->type == EVT_DELKEY) ? STRCUR_DIR_NEXT :
                                                                                STRCUR_DIR_PREV;
        const eStrCursorJumpType jump = ui_textedit_jump_type_from_event(event);
        changed = ui_textedit_delete(but, data, direction, jump);
        retval = WM_UI_HANDLER_BREAK;
        break;
      }

      case EVT_AKEY:

        /* Ctrl-A: Select all. */
#if defined(__APPLE__)
        /* OSX uses Command-A system-wide, so add it. */
        if (ELEM(event->modifier, KM_OSKEY, KM_CTRL))
#else
        if (event->modifier == KM_CTRL)
#endif
        {
          ui_textedit_move(but, data, STRCUR_DIR_PREV, false, STRCUR_JUMP_ALL);
          ui_textedit_move(but, data, STRCUR_DIR_NEXT, true, STRCUR_JUMP_ALL);
          retval = WM_UI_HANDLER_BREAK;
        }
        break;

      case EVT_TABKEY:
        /* There is a key conflict here, we can't tab with auto-complete. */
        if (but->autocomplete_func || data->searchbox) {
          const int autocomplete = ui_textedit_autocomplete(C, but, data);
          changed = autocomplete != AUTOCOMPLETE_NO_MATCH;

          if (autocomplete == AUTOCOMPLETE_FULL_MATCH) {
            button_activate_state(C, but, BUTTON_STATE_EXIT);
          }
        }
        else if ((event->modifier & (KM_CTRL | KM_ALT | KM_OSKEY)) == 0) {
          /* Use standard keys for cycling through buttons Tab, Shift-Tab to reverse. */
          if (event->modifier & KM_SHIFT) {
            ui_textedit_prev_but(block, but, data);
          }
          else {
            ui_textedit_next_but(block, but, data);
          }
          button_activate_state(C, but, BUTTON_STATE_EXIT);
        }
        retval = WM_UI_HANDLER_BREAK;
        break;
      case EVT_ZKEY: {
        /* Ctrl-Z or Ctrl-Shift-Z: Undo/Redo (allowing for OS-Key on Apple). */

        const bool is_redo = (event->modifier & KM_SHIFT);
        if (
#if defined(__APPLE__)
            ((event->modifier & KM_OSKEY) && ((event->modifier & (KM_ALT | KM_CTRL)) == 0)) ||
#endif
            ((event->modifier & KM_CTRL) && ((event->modifier & (KM_ALT | KM_OSKEY)) == 0)))
        {
          int undo_pos;
          const char *undo_str = ui_textedit_undo(
              data->undo_stack_text, is_redo ? 1 : -1, &undo_pos);
          if (undo_str != nullptr) {
            ui_textedit_string_set(but, data, undo_str);

            /* Set the cursor & clear selection. */
            but->pos = undo_pos;
            but->selsta = but->pos;
            but->selend = but->pos;
            changed = true;
          }
          retval = WM_UI_HANDLER_BREAK;
          skip_undo_push = true;
        }
        break;
      }
    }

    if ((event->utf8_buf[0]) && (retval == WM_UI_HANDLER_CONTINUE)
#ifdef WITH_INPUT_IME
        && !is_ime_composing && !WM_event_is_ime_switch(event)
#endif
    )
    {
      char utf8_buf_override[2] = {'\0', '\0'};
      const char *utf8_buf = event->utf8_buf;

      /* Exception that's useful for number buttons, some keyboard
       * numpads have a comma instead of a period. */
      if (ELEM(but->type, UI_BTYPE_NUM, UI_BTYPE_NUM_SLIDER)) { /* Could use `data->min`. */
        if ((event->type == EVT_PADPERIOD) && (utf8_buf[0] == ',')) {
          utf8_buf_override[0] = '.';
          utf8_buf = utf8_buf_override;
        }
      }

      if (utf8_buf[0]) {
        const int utf8_buf_len = BLI_str_utf8_size_or_error(utf8_buf);
        BLI_assert(utf8_buf_len != -1);
        changed = ui_textedit_insert_buf(but, data, utf8_buf, utf8_buf_len);
      }

      retval = WM_UI_HANDLER_BREAK;
    }
    /* textbutton with this flag: do live update (e.g. for search buttons) */
    if (but->flag & UI_BUT_TEXTEDIT_UPDATE) {
      update = true;
    }
  }

#ifdef WITH_INPUT_IME
  if (event->type == WM_IME_COMPOSITE_START) {
    changed = true;
    if (but->selend > but->selsta) {
      ui_textedit_delete_selection(but, data);
    }
  }
  else if (event->type == WM_IME_COMPOSITE_EVENT) {
    changed = true;
    if (ime_data->result_len) {
      if (ELEM(but->type, UI_BTYPE_NUM, UI_BTYPE_NUM_SLIDER) &&
          STREQ(ime_data->str_result, "\xE3\x80\x82"))
      {
        /* Convert Ideographic Full Stop (U+3002) to decimal point when entering numbers. */
        ui_textedit_insert_ascii(but, data, '.');
      }
      else {
        ui_textedit_insert_buf(but, data, ime_data->str_result, ime_data->result_len);
      }
    }
  }
  else if (event->type == WM_IME_COMPOSITE_END) {
    changed = true;
  }
#endif

  if (changed) {
    /* The undo stack may be nullptr if an event exits editing. */
    if ((skip_undo_push == false) && (data->undo_stack_text != nullptr)) {
      ui_textedit_undo_push(data->undo_stack_text, data->str, but->pos);
    }

    /* only do live update when but flag request it (UI_BUT_TEXTEDIT_UPDATE). */
    if (update && data->interactive) {
      ui_apply_but(C, block, but, data, true);
    }
    else {
      ui_but_update_edited(but);
    }
    but->changed = true;

    if (data->searchbox) {
      ui_searchbox_update(C, data->searchbox, but, true); /* true = reset */
    }
  }

  if (changed || (retval == WM_UI_HANDLER_BREAK)) {
    ED_region_tag_redraw(data->region);
  }
}

static void ui_do_but_textedit_select(
    bContext *C, uiBlock *block, uiBut *but, uiHandleButtonData *data, const wmEvent *event)
{
  int retval = WM_UI_HANDLER_CONTINUE;

  switch (event->type) {
    case MOUSEMOVE: {
      int mx = event->xy[0];
      int my = event->xy[1];
      ui_window_to_block(data->region, block, &mx, &my);

      ui_textedit_set_cursor_select(but, data, event->xy[0]);
      retval = WM_UI_HANDLER_BREAK;
      break;
    }
    case LEFTMOUSE:
      if (event->val == KM_RELEASE) {
        button_activate_state(C, but, BUTTON_STATE_TEXT_EDITING);
      }
      retval = WM_UI_HANDLER_BREAK;
      break;
  }

  if (retval == WM_UI_HANDLER_BREAK) {
    ui_but_update(but);
    ED_region_tag_redraw(data->region);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Button Number Editing (various types)
 * \{ */

static void ui_numedit_begin_set_values(uiBut *but, uiHandleButtonData *data)
{
  data->startvalue = ui_but_value_get(but);
  data->origvalue = data->startvalue;
  data->value = data->origvalue;
}

static void ui_numedit_begin(uiBut *but, uiHandleButtonData *data)
{
  if (but->type == UI_BTYPE_CURVE) {
    uiButCurveMapping *but_cumap = (uiButCurveMapping *)but;
    but_cumap->edit_cumap = (CurveMapping *)but->poin;
  }
  else if (but->type == UI_BTYPE_CURVEPROFILE) {
    uiButCurveProfile *but_profile = (uiButCurveProfile *)but;
    but_profile->edit_profile = (CurveProfile *)but->poin;
  }
  else if (but->type == UI_BTYPE_COLORBAND) {
    uiButColorBand *but_coba = (uiButColorBand *)but;
    data->coba = (ColorBand *)but->poin;
    but_coba->edit_coba = data->coba;
  }
  else if (ELEM(but->type, UI_BTYPE_UNITVEC, UI_BTYPE_HSVCUBE, UI_BTYPE_HSVCIRCLE, UI_BTYPE_COLOR))
  {
    ui_but_v3_get(but, data->origvec);
    copy_v3_v3(data->vec, data->origvec);
    but->editvec = data->vec;
  }
  else {
    ui_numedit_begin_set_values(but, data);
    but->editval = &data->value;

    float softmin = but->softmin;
    float softmax = but->softmax;
    float softrange = softmax - softmin;
    const PropertyScaleType scale_type = ui_but_scale_type(but);

    float log_min = (scale_type == PROP_SCALE_LOG) ? max_ff(softmin, UI_PROP_SCALE_LOG_MIN) : 0.0f;

    if ((but->type == UI_BTYPE_NUM) && (ui_but_is_cursor_warp(but) == false)) {
      uiButNumber *number_but = (uiButNumber *)but;

      if (scale_type == PROP_SCALE_LOG) {
        log_min = max_ff(log_min, powf(10, -number_but->precision) * 0.5f);
      }
      /* Use a minimum so we have a predictable range,
       * otherwise some float buttons get a large range. */
      const float value_step_float_min = 0.1f;
      const bool is_float = ui_but_is_float(but);
      const double value_step = is_float ?
                                    double(number_but->step_size * UI_PRECISION_FLOAT_SCALE) :
                                    int(number_but->step_size);
      const float drag_map_softrange_max = UI_DRAG_MAP_SOFT_RANGE_PIXEL_MAX * UI_SCALE_FAC;
      const float softrange_max = min_ff(
          softrange,
          2 * (is_float ? min_ff(value_step, value_step_float_min) *
                              (drag_map_softrange_max / value_step_float_min) :
                          drag_map_softrange_max));

      if (softrange > softrange_max) {
        /* Center around the value, keeping in the real soft min/max range. */
        softmin = data->origvalue - (softrange_max / 2);
        softmax = data->origvalue + (softrange_max / 2);
        if (!isfinite(softmin)) {
          softmin = (data->origvalue > 0.0f ? FLT_MAX : -FLT_MAX);
        }
        if (!isfinite(softmax)) {
          softmax = (data->origvalue > 0.0f ? FLT_MAX : -FLT_MAX);
        }

        if (softmin < but->softmin) {
          softmin = but->softmin;
          softmax = softmin + softrange_max;
        }
        else if (softmax > but->softmax) {
          softmax = but->softmax;
          softmin = softmax - softrange_max;
        }

        /* Can happen at extreme values. */
        if (UNLIKELY(softmin == softmax)) {
          if (data->origvalue > 0.0) {
            softmin = nextafterf(softmin, -FLT_MAX);
          }
          else {
            softmax = nextafterf(softmax, FLT_MAX);
          }
        }

        softrange = softmax - softmin;
      }
    }

    if (softrange == 0.0f) {
      data->dragfstart = 0.0f;
    }
    else {
      switch (scale_type) {
        case PROP_SCALE_LINEAR: {
          data->dragfstart = (float(data->value) - softmin) / softrange;
          break;
        }
        case PROP_SCALE_LOG: {
          BLI_assert(log_min != 0.0f);
          const float base = softmax / log_min;
          data->dragfstart = logf(float(data->value) / log_min) / logf(base);
          break;
        }
        case PROP_SCALE_CUBIC: {
          const float cubic_min = cube_f(softmin);
          const float cubic_max = cube_f(softmax);
          const float cubic_range = cubic_max - cubic_min;
          const float f = (float(data->value) - softmin) * cubic_range / softrange + cubic_min;
          data->dragfstart = (cbrtf(f) - softmin) / softrange;
          break;
        }
      }
    }
    data->dragf = data->dragfstart;

    data->drag_map_soft_min = softmin;
    data->drag_map_soft_max = softmax;
  }

  data->dragchange = false;
  data->draglock = true;
}

static void ui_numedit_end(uiBut *but, uiHandleButtonData *data)
{
  but->editval = nullptr;
  but->editvec = nullptr;
  if (but->type == UI_BTYPE_COLORBAND) {
    uiButColorBand *but_coba = (uiButColorBand *)but;
    but_coba->edit_coba = nullptr;
  }
  else if (but->type == UI_BTYPE_CURVE) {
    uiButCurveMapping *but_cumap = (uiButCurveMapping *)but;
    but_cumap->edit_cumap = nullptr;
  }
  else if (but->type == UI_BTYPE_CURVEPROFILE) {
    uiButCurveProfile *but_profile = (uiButCurveProfile *)but;
    but_profile->edit_profile = nullptr;
  }
  data->dragstartx = 0;
  data->draglastx = 0;
  data->dragchange = false;
  data->dragcbd = nullptr;
  data->dragsel = 0;
}

static void ui_numedit_apply(bContext *C, uiBlock *block, uiBut *but, uiHandleButtonData *data)
{
  if (data->interactive) {
    ui_apply_but(C, block, but, data, true);
  }
  else {
    ui_but_update(but);
  }

  ED_region_tag_redraw(data->region);
}

static void ui_but_extra_operator_icon_apply(bContext *C, uiBut *but, uiButExtraOpIcon *op_icon)
{
  but->active->apply_through_extra_icon = true;

  if (but->active->interactive) {
    ui_apply_but(C, but->block, but, but->active, true);
  }
  button_activate_state(C, but, BUTTON_STATE_EXIT);
  WM_operator_name_call_ptr_with_depends_on_cursor(C,
                                                   op_icon->optype_params->optype,
                                                   op_icon->optype_params->opcontext,
                                                   op_icon->optype_params->opptr,
                                                   nullptr,
                                                   nullptr);

  /* Force recreation of extra operator icons (pseudo update). */
  ui_but_extra_operator_icons_free(but);

  WM_event_add_mousemove(CTX_wm_window(C));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Menu/Popup Begin/End (various popup types)
 * \{ */

static void ui_block_open_begin(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  uiBlockCreateFunc func = nullptr;
  uiBlockHandleCreateFunc handlefunc = nullptr;
  uiMenuCreateFunc menufunc = nullptr;
  uiMenuCreateFunc popoverfunc = nullptr;
  void *arg = nullptr;

  switch (but->type) {
    case UI_BTYPE_BLOCK:
    case UI_BTYPE_PULLDOWN:
      if (but->menu_create_func) {
        menufunc = but->menu_create_func;
        arg = but->poin;
      }
      else {
        func = but->block_create_func;
        arg = but->poin ? but->poin : but->func_argN;
      }
      break;
    case UI_BTYPE_MENU:
    case UI_BTYPE_POPOVER:
      BLI_assert(but->menu_create_func);
      if ((but->type == UI_BTYPE_POPOVER) || ui_but_menu_draw_as_popover(but)) {
        popoverfunc = but->menu_create_func;
      }
      else {
        menufunc = but->menu_create_func;
      }
      arg = but->poin;
      break;
    case UI_BTYPE_COLOR:
      ui_but_v3_get(but, data->origvec);
      copy_v3_v3(data->vec, data->origvec);
      but->editvec = data->vec;

      if (ui_but_menu_draw_as_popover(but)) {
        popoverfunc = but->menu_create_func;
      }
      else {
        handlefunc = ui_block_func_COLOR;
      }
      arg = but;
      break;

      /* quiet warnings for unhandled types */
    default:
      break;
  }

  if (func || handlefunc) {
    data->menu = ui_popup_block_create(C, data->region, but, func, handlefunc, arg, nullptr);
    if (but->block->handle) {
      data->menu->popup = but->block->handle->popup;
    }
  }
  else if (menufunc) {
    data->menu = ui_popup_menu_create(C, data->region, but, menufunc, arg);
    if (MenuType *mt = UI_but_menutype_get(but)) {
      STRNCPY(data->menu->menu_idname, mt->idname);
    }
    if (but->block->handle) {
      data->menu->popup = but->block->handle->popup;
    }
  }
  else if (popoverfunc) {
    data->menu = ui_popover_panel_create(C, data->region, but, popoverfunc, arg);
    if (but->block->handle) {
      data->menu->popup = but->block->handle->popup;
    }
  }

#ifdef USE_ALLSELECT
  {
    if (IS_ALLSELECT_EVENT(data->window->eventstate)) {
      data->select_others.is_enabled = true;
    }
  }
#endif

  /* this makes adjacent blocks auto open from now on */
  // if (but->block->auto_open == 0) {
  //  but->block->auto_open = 1;
  //}
}

static void ui_block_open_end(bContext *C, uiBut *but, uiHandleButtonData *data)
{
  if (but) {
    but->editval = nullptr;
    but->editvec = nullptr;

    but->block->auto_open_last = PIL_check_seconds_timer();
  }

  if (data->menu) {
    ui_popup_block_free(C, data->menu);
    data->menu = nullptr;
  }
}

int ui_but_menu_direction(uiBut *but)
{
  uiHandleButtonData *data = but->active;

  if (data && data->menu) {
    return data->menu->direction;
  }

  return 0;
}

/**
 * Hack for #uiList #UI_BTYPE_LISTROW buttons to "give" events to overlaying #UI_BTYPE_TEXT
 * buttons (Ctrl-Click rename feature & co).
 */
static uiBut *ui_but_list_row_text_activate(bContext *C,
                                            uiBut *but,
                                            uiHandleButtonData *data,
                                            const wmEvent *event,
                                            uiButtonActivateType activate_type)
{
  ARegion *region = CTX_wm_region(C);
  uiBut *labelbut = ui_but_find_mouse_over_ex(region, event->xy, true, false, nullptr, nullptr);

  if (labelbut && labelbut->type == UI_BTYPE_TEXT && !(labelbut->flag & UI_BUT_DISABLED)) {
    /* exit listrow */
    data->cancel = true;
    button_activate_exit(C, but, data, false, false);

    /* Activate the text button. */
    button_activate_init(C, region, labelbut, activate_type);

    return labelbut;
  }
  return nullptr;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Events for Various Button Types
 * \{ */

static uiButExtraOpIcon *ui_but_extra_operator_icon_mouse_over_get(uiBut *but,
                                                                   ARegion *region,
                                                                   const wmEvent *event)
{
  if (BLI_listbase_is_empty(&but->extra_op_icons)) {
    return nullptr;
  }

  int x = event->xy[0], y = event->xy[1];
  ui_window_to_block(region, but->block, &x, &y);
  if (!BLI_rctf_isect_pt(&but->rect, x, y)) {
    return nullptr;
  }

  const float icon_size = 0.8f * BLI_rctf_size_y(&but->rect); /* ICON_SIZE_FROM_BUTRECT */
  float xmax = but->rect.xmax;
  /* Same as in 'widget_draw_extra_icons', icon padding from the right edge. */
  xmax -= 0.2 * icon_size;

  /* Handle the padding space from the right edge as the last button. */
  if (x > xmax) {
    return static_cast<uiButExtraOpIcon *>(but->extra_op_icons.last);
  }

  /* Inverse order, from right to left. */
  LISTBASE_FOREACH_BACKWARD (uiButExtraOpIcon *, op_icon, &but->extra_op_icons) {
    if ((x > (xmax - icon_size)) && x <= xmax) {
      return op_icon;
    }
    xmax -= icon_size;
  }

  return nullptr;
}

static bool ui_do_but_extra_operator_icon(bContext *C,
                                          uiBut *but,
                                          uiHandleButtonData *data,
                                          const wmEvent *event)
{
  uiButExtraOpIcon *op_icon = ui_but_extra_operator_icon_mouse_over_get(but, data->region, event);

  if (!op_icon) {
    return false;
  }

  /* Only act on release, avoids some glitches. */
  if (event->val != KM_RELEASE) {
    /* Still swallow events on the icon. */
    return true;
  }

  ED_region_tag_redraw(data->region);
  button_tooltip_timer_reset(C, but);

  ui_but_extra_operator_icon_apply(C, but, op_icon);
  /* NOTE: 'but', 'data' may now be freed, don't access. */

  return true;
}

static void ui_do_but_extra_operator_icons_mousemove(uiBut *but,
                                                     uiHandleButtonData *data,
                                                     const wmEvent *event)
{
  uiButExtraOpIcon *old_highlighted = nullptr;

  /* Unset highlighting of all first. */
  LISTBASE_FOREACH (uiButExtraOpIcon *, op_icon, &but->extra_op_icons) {
    if (op_icon->highlighted) {
      old_highlighted = op_icon;
    }
    op_icon->highlighted = false;
  }

  uiButExtraOpIcon *hovered = ui_but_extra_operator_icon_mouse_over_get(but, data->region, event);

  if (hovered) {
    hovered->highlighted = true;
  }

  if (old_highlighted != hovered) {
    ED_region_tag_redraw_no_rebuild(data->region);
  }
}

#ifdef USE_DRAG_TOGGLE
/* Shared by any button that supports drag-toggle. */
static bool ui_do_but_ANY_drag_toggle(
    bContext *C, uiBut *but, uiHandleButtonData *data, const wmEvent *event, int *r_retval)
{
  if (data->state == BUTTON_STATE_HIGHLIGHT) {
    if (event->type == LEFTMOUSE && event->val == KM_PRESS && ui_but_is_drag_toggle(but)) {
      ui_apply_but(C, but->block, but, data, true);
      button_activate_state(C, but, BUTTON_STATE_WAIT_DRAG);
      data->dragstartx = event->xy[0];
      data->dragstarty = event->xy[1];
      *r_retval = WM_UI_HANDLER_BREAK;
      return true;
    }
  }
  else if (data->state == BUTTON_STATE_WAIT_DRAG) {
    /* NOTE: the 'BUTTON_STATE_WAIT_DRAG' part of 'ui_do_but_EXIT' could be refactored into
     * its own function */
    data->applied = false;
    *r_retval = ui_do_but_EXIT(C, but, data, event);
    return true;
  }
  return false;
}
#endif /* USE_DRAG_TOGGLE */

static int ui_do_but_BUT(bContext *C, uiBut *but, uiHandleButtonData *data, const wmEvent *event)
{
#ifdef USE_DRAG_TOGGLE
  {
    int retval;
    if (ui_do_but_ANY_drag_toggle(C, but, data, event, &retval)) {
      return retval;
    }
  }
#endif

  if (data->state == BUTTON_STATE_HIGHLIGHT) {
    if (event->type == LEFTMOUSE && event->val == KM_PRESS) {
      button_activate_state(C, but, BUTTON_STATE_WAIT_RELEASE);
      return WM_UI_HANDLER_BREAK;
    }
    if (event->type == LEFTMOUSE && event->val == KM_RELEASE && but->block->handle) {
      /* regular buttons will be 'UI_SELECT', menu items 'UI_HOVER' */
      if (!(but->flag & (UI_SELECT | UI_HOVER))) {
        data->cancel = true;
      }
      button_activate_state(C, but, BUTTON_STATE_EXIT);
      return WM_UI_HANDLER_BREAK;
    }
    if (ELEM(event->type, EVT_PADENTER, EVT_RETKEY) && event->val == KM_PRESS) {
      button_activate_state(C, but, BUTTON_STATE_WAIT_FLASH);
      return WM_UI_HANDLER_BREAK;
    }
  }
  else if (data->state == BUTTON_STATE_WAIT_RELEASE) {
    if (event->type == LEFTMOUSE && event->val == KM_RELEASE) {
      if (!(but->flag & UI_SELECT)) {
        data->cancel = true;
      }
      button_activate_state(C, but, BUTTON_STATE_EXIT);
      return WM_UI_HANDLER_BREAK;
    }
  }

  return WM_UI_HANDLER_CONTINUE;
}


