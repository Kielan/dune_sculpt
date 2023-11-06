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

struct uiSelectContextStore {
  uiSelectContextElem *elems;
  int elems_len;
  bool do_free;
  bool is_enabled;
  /* When set, simply copy values (don't apply difference).
   * Rules are:
   * - dragging numbers uses delta.
   * - typing in values will assign to all. */
  bool is_copy;
};

static bool ui_selectcxt_begin(Cxt *C, Btn *btn, uiSelectCxtStore *selcxt_data);
static void ui_selectcxt_end(Btn *btn, uiSelectCxtStore *selcxt_data);
static void ui_selectcxt_apply(Cxt *C,
                               Btn *btn,
                               uiSelectCxtStore *selcxt_data,
                               const double value,
                               const double value_orig);

/* Ideally we would only respond to events which are expected to be used for multi btn editing
 * (additionally checking if this is a mouse[wheel] or return-key event to avoid the ALT conflict
 * with button array pasting, see #108096, but unfortunately wheel events are not part of
 * `win->evstate` with modifiers held down. Instead, the conflict is avoided by specifically
 * filtering out CTRL ALT V in #ui_apply_but(). */
#  define IS_ALLSELECT_EVENT(event) (((event)->mod & KM_ALT) != 0)

/* just show a tinted color so users know its activated */
#  define BTN_IS_SELECT_CXT BTN_NODE_ACTIVE

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
  uiBut *but;

#  ifdef USE_ALLSELECT
  uiSelectCxtStore select_others;
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
  wmTimer *flashtimer;

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

#ifdef USE_ALLSELECT
  uiSelectCxtStore select_others;
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
