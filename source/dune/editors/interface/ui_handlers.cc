#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "MEM_guardedalloc.h"

#include "DNA_brush_types.h"
#include "DNA_curveprofile_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BLI_array.hh"
#include "BLI_array_utils.h"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math_geom.h"
#include "BLI_rect.h"
#include "BLI_sort_utils.h"
#include "BLI_string.h"
#include "BLI_string_cursor_utf8.h"
#include "BLI_string_utf8.h"
#include "BLI_utildefines.h"

#include "PIL_time.h"

#include "BKE_animsys.h"
#include "BKE_blender_undo.h"
#include "BKE_brush.hh"
#include "BKE_colorband.h"
#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_curveprofile.h"
#include "BKE_movieclip.h"
#include "BKE_paint.hh"
#include "BKE_report.h"
#include "BKE_screen.hh"
#include "BKE_tracking.h"
#include "BKE_unit.h"

#include "GHOST_C-api.h"

#include "IMB_colormanagement.h"

#include "ED_screen.hh"
#include "ED_undo.hh"

#include "UI_interface.hh"
#include "UI_string_search.hh"
#include "UI_view2d.hh"

#include "BLF_api.h"

#include "interface_intern.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.h"

#include "WM_api.hh"
#include "WM_types.hh"
#include "wm_event_system.h"

#ifdef WITH_INPUT_IME
#  include "BLT_lang.h"
#  include "BLT_translation.h"
#  include "wm_window.hh"
#endif

/* -------------------------------------------------------------------- */
/** \name Feature Defines
 *
 * These defines allow developers to locally toggle functionality which
 * may be useful for testing (especially conflicts in dragging).
 * Ideally the code would be refactored to support this functionality in a less fragile way.
 * Until then keep these defines.
 * \{ */

/** Place the mouse at the scaled down location when un-grabbing. */
#define USE_CONT_MOUSE_CORRECT
/** Support dragging toggle buttons. */
#define USE_DRAG_TOGGLE

/** Support dragging multiple number buttons at once. */
#define USE_DRAG_MULTINUM

/** Allow dragging/editing all other selected items at once. */
#define USE_ALLSELECT

/**
 * Check to avoid very small mouse-moves from jumping away from keyboard navigation,
 * while larger mouse motion will override keyboard input, see: #34936.
 */
#define USE_KEYNAV_LIMIT

/** Support dragging popups by their header. */
#define USE_DRAG_POPUP

/** \} */

/* -------------------------------------------------------------------- */
/** \name Local Defines
 * \{ */

/**
 * The buffer side used for password strings, where the password is stored internally,
 * but not displayed.
 */
#define UI_MAX_PASSWORD_STR 128

/**
 * This is a lower limit on the soft minimum of the range.
 * Usually the derived lower limit from the visible precision is higher,
 * so this number is the backup minimum.
 *
 * Logarithmic scale does not work with a minimum value of zero,
 * but we want to support it anyway. It is set to 0.5e... for
 * correct rounding since when the tweaked value is lower than
 * the log minimum (lower limit), it will snap to 0.
 */
#define UI_PROP_SCALE_LOG_MIN 0.5e-8f
/**
 * This constant defines an offset for the precision change in
 * snap rounding, when going to higher values. It is set to
 * `0.5 - log10(3) = 0.03` to make the switch at `0.3` values.
 */
#define UI_PROP_SCALE_LOG_SNAP_OFFSET 0.03f

/**
 * When #USER_CONTINUOUS_MOUSE is disabled or tablet input is used,
 * Use this as a maximum soft range for mapping cursor motion to the value.
 * Otherwise min/max of #FLT_MAX, #INT_MAX cause small adjustments to jump to large numbers.
 *
 * This is needed for values such as location & dimensions which don't have a meaningful min/max,
 * Instead of mapping cursor motion to the min/max, map the motion to the click-step.
 *
 * This value is multiplied by the click step to calculate a range to clamp the soft-range by.
 * See: #68130
 */
#define UI_DRAG_MAP_SOFT_RANGE_PIXEL_MAX 1000

/** \} */

/* -------------------------------------------------------------------- */
/** \name Local Prototypes
 * \{ */

struct uiBlockInteraction_Handle;

static int ui_do_but_EXIT(bContext *C, uiBut *but, uiHandleButtonData *data, const wmEvent *event);
static bool ui_but_find_select_in_enum__cmp(const uiBut *but_a, const uiBut *but_b);
static void ui_textedit_string_set(uiBut *but, uiHandleButtonData *data, const char *str);
static void button_tooltip_timer_reset(bContext *C, uiBut *but);

static void ui_block_interaction_begin_ensure(bContext *C,
                                              uiBlock *block,
                                              uiHandleButtonData *data,
                                              const bool is_click);
static uiBlockInteraction_Handle *ui_block_interaction_begin(bContext *C,
                                                             uiBlock *block,
                                                             const bool is_click);
static void ui_block_interaction_end(bContext *C,
                                     uiBlockInteraction_CallbackData *callbacks,
                                     uiBlockInteraction_Handle *interaction);
static void ui_block_interaction_update(bContext *C,
                                        uiBlockInteraction_CallbackData *callbacks,
                                        uiBlockInteraction_Handle *interaction);

#ifdef USE_KEYNAV_LIMIT
static void ui_mouse_motion_keynav_init(uiKeyNavLock *keynav, const wmEvent *event);
static bool ui_mouse_motion_keynav_test(uiKeyNavLock *keynav, const wmEvent *event);
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name Structs & Defines
 * \{ */

#define BUTTON_FLASH_DELAY 0.020
#define MENU_SCROLL_INTERVAL 0.1
#define PIE_MENU_INTERVAL 0.01
#define BUTTON_AUTO_OPEN_THRESH 0.2
#define BUTTON_MOUSE_TOWARDS_THRESH 1.0
/** Pixels to move the cursor to get out of keyboard navigation. */
#define BUTTON_KEYNAV_PX_LIMIT 8

/** Margin around the menu, use to check if we're moving towards this rectangle (in pixels). */
#define MENU_TOWARDS_MARGIN 20
/** Tolerance for closing menus (in pixels). */
#define MENU_TOWARDS_WIGGLE_ROOM 64
/** Drag-lock distance threshold (in pixels). */
#define BUTTON_DRAGLOCK_THRESH 3

enum uiButtonActivateType {
  BUTTON_ACTIVATE_OVER,
  BUTTON_ACTIVATE,
  BUTTON_ACTIVATE_APPLY,
  BUTTON_ACTIVATE_TEXT_EDITING,
  BUTTON_ACTIVATE_OPEN,
};

enum uiHandleButtonState {
  BUTTON_STATE_INIT,
  BUTTON_STATE_HIGHLIGHT,
  BUTTON_STATE_WAIT_FLASH,
  BUTTON_STATE_WAIT_RELEASE,
  BUTTON_STATE_WAIT_KEY_EVENT,
  BUTTON_STATE_NUM_EDITING,
  BUTTON_STATE_TEXT_EDITING,
  BUTTON_STATE_TEXT_SELECTING,
  BUTTON_STATE_MENU_OPEN,
  BUTTON_STATE_WAIT_DRAG,
  BUTTON_STATE_EXIT,
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
  /**
   * This is shared between #uiHandleButtonData and #uiAfterFunc,
   * the last user runs the end callback and frees the data.
   *
   * This is needed as the order of freeing changes depending on
   * accepting/canceling the operation.
   */
  int user_count;
};

#ifdef USE_ALLSELECT

/* Unfortunately there's no good way handle more generally:
 * (propagate single clicks on layer buttons to other objects) */
#  define USE_ALLSELECT_LAYER_HACK

struct uiSelectContextElem {
  PointerRNA ptr;
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

static bool ui_selectcontext_begin(bContext *C, uiBut *but, uiSelectContextStore *selctx_data);
static void ui_selectcontext_end(uiBut *but, uiSelectContextStore *selctx_data);
static void ui_selectcontext_apply(bContext *C,
                                   uiBut *but,
                                   uiSelectContextStore *selctx_data,
                                   const double value,
                                   const double value_orig);

/**
 * Ideally we would only respond to events which are expected to be used for multi button editing
 * (additionally checking if this is a mouse[wheel] or return-key event to avoid the ALT conflict
 * with button array pasting, see #108096, but unfortunately wheel events are not part of
 * `win->eventstate` with modifiers held down. Instead, the conflict is avoided by specifically
 * filtering out CTRL ALT V in #ui_apply_but(). */
#  define IS_ALLSELECT_EVENT(event) (((event)->modifier & KM_ALT) != 0)

/** just show a tinted color so users know its activated */
#  define UI_BUT_IS_SELECT_CONTEXT UI_BUT_NODE_ACTIVE

#endif /* USE_ALLSELECT */

#ifdef USE_DRAG_MULTINUM

/**
 * how far to drag before we check for gesture direction (in pixels),
 * NOTE: half the height of a button is about right... */
#  define DRAG_MULTINUM_THRESHOLD_DRAG_X (UI_UNIT_Y / 4)

/**
 * How far to drag horizontally
 * before we stop checking which buttons the gesture spans (in pixels),
 * locking down the buttons so we can drag freely without worrying about vertical movement.
 */
#  define DRAG_MULTINUM_THRESHOLD_DRAG_Y (UI_UNIT_Y / 4)

/**
 * How strict to be when detecting a vertical gesture:
 * [0.5 == sloppy], [0.9 == strict], (unsigned dot-product).
 *
 * \note We should be quite strict here,
 * since doing a vertical gesture by accident should be avoided,
 * however with some care a user should be able to do a vertical movement without _missing_.
 */
#  define DRAG_MULTINUM_THRESHOLD_VERTICAL (0.75f)

/* a simple version of uiHandleButtonData when accessing multiple buttons */
struct uiButMultiState {
  double origvalue;
  uiBut *but;

#  ifdef USE_ALLSELECT
  uiSelectContextStore select_others;
#  endif
};

struct uiHandleButtonMulti {
  enum {
    /** gesture direction unknown, wait until mouse has moved enough... */
    INIT_UNSET = 0,
    /** vertical gesture detected, flag buttons interactively (UI_BUT_DRAG_MULTI) */
    INIT_SETUP,
    /** flag buttons finished, apply horizontal motion to active and flagged */
    INIT_ENABLE,
    /** vertical gesture _not_ detected, take no further action */
    INIT_DISABLE,
  } init;

  bool has_mbuts; /* any buttons flagged UI_BUT_DRAG_MULTI */
  LinkNode *mbuts;
  uiButStore *bs_mbuts;

  bool is_proportional;

  /* In some cases we directly apply the changes to multiple buttons,
   * so we don't want to do it twice. */
  bool skip;

  /* before activating, we need to check gesture direction accumulate signed cursor movement
   * here so we can tell if this is a vertical motion or not. */
  float drag_dir[2];

  /* values copied direct from event->xy
   * used to detect buttons between the current and initial mouse position */
  int drag_start[2];

  /* store x location once INIT_SETUP is set,
   * moving outside this sets INIT_ENABLE */
  int drag_lock_x;
};

#endif /* USE_DRAG_MULTINUM */

struct uiHandleButtonData {
  wmWindowManager *wm;
  wmWindow *window;
  ScrArea *area;
  ARegion *region;

  bool interactive;

  /* overall state */
  uiHandleButtonState state;
  int retval;
  /* booleans (could be made into flags) */
  bool cancel, escapecancel;
  bool applied, applied_interactive;
  /* Button is being applied through an extra icon. */
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
  /**
   * Behave as if #UI_BUT_DISABLED is set (without drawing grayed out).
   * Needed so non-interactive labels can be activated for the purpose of showing tool-tips,
   * without them blocking interaction with nodes, see: #97386.
   */
  bool disable_force;

  /* auto open */
  bool used_mouse;
  wmTimer *autoopentimer;

  /* auto open (hold) */
  wmTimer *hold_action_timer;

  /* text selection/editing */
  /* size of 'str' (including terminator) */
  int str_maxncpy;
  /* Button text selection:
   * extension direction, selextend, inside ui_do_but_TEX */
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

  /** Soft min/max with #UI_DRAG_MAP_SOFT_RANGE_PIXEL_MAX applied. */
  float drag_map_soft_min;
  float drag_map_soft_max;

#ifdef USE_CONT_MOUSE_CORRECT
  /* when ungrabbing buttons which are #ui_but_is_cursor_warp(),
   * we may want to position them.
   * FLT_MAX signifies do-nothing, use #ui_block_to_window_fl()
   * to get this into a usable space. */
  float ungrab_mval[2];
#endif

  /* Menu open, see: #UI_screen_free_active_but_highlight. */
  uiPopupBlockHandle *menu;
  int menuretval;

  /* Search box see: #UI_screen_free_active_but_highlight. */
  ARegion *searchbox;
#ifdef USE_KEYNAV_LIMIT
  uiKeyNavLock searchbox_keynav_state;
#endif

#ifdef USE_DRAG_MULTINUM
  /* Multi-buttons will be updated in unison with the active button. */
  uiHandleButtonMulti multi_data;
#endif

#ifdef USE_ALLSELECT
  uiSelectContextStore select_others;
#endif

  uiBlockInteraction_Handle *custom_interaction_handle;

  /* Text field undo. */
  uiUndoStack_Text *undo_stack_text;

  /* post activate */
  uiButtonActivateType posttype;
  uiBut *postbut;
};

struct uiAfterFunc {
  uiAfterFunc *next, *prev;

  uiButHandleFunc func;
  void *func_arg1;
  void *func_arg2;
  /** C++ version of #func above, without need for void pointer arguments. */
  std::function<void(bContext &)> apply_func;

  uiButHandleNFunc funcN;
  void *func_argN;

  uiButHandleRenameFunc rename_func;
  void *rename_arg1;
  void *rename_orig;

  uiBlockHandleFunc handle_func;
  void *handle_func_arg;
  int retval;

  uiMenuHandleFunc butm_func;
  void *butm_func_arg;
  int a2;

  wmOperator *popup_op;
  wmOperatorType *optype;
  wmOperatorCallContext opcontext;
  PointerRNA *opptr;

  PointerRNA rnapoin;
  PropertyRNA *rnaprop;

  void *search_arg;
  uiFreeArgFunc search_arg_free_fn;

  uiBlockInteraction_CallbackData custom_interaction_callbacks;
  uiBlockInteraction_Handle *custom_interaction_handle;

  std::optional<bContextStore> context;

  char undostr[BKE_UNDO_STR_MAX];
  char drawstr[UI_MAX_DRAW_STR];
};

static void button_activate_init(bContext *C,
                                 ARegion *region,
                                 uiBut *but,
                                 uiButtonActivateType type);
static void button_activate_state(bContext *C, uiBut *but, uiHandleButtonState state);
static void button_activate_exit(
    bContext *C, uiBut *but, uiHandleButtonData *data, const bool mousemove, const bool onfree);
static int ui_handler_region_menu(bContext *C, const wmEvent *event, void *userdata);
static void ui_handle_button_activate(bContext *C,
                                      ARegion *region,
                                      uiBut *but,
                                      uiButtonActivateType type);
static bool ui_do_but_extra_operator_icon(bContext *C,
                                          uiBut *but,
                                          uiHandleButtonData *data,
                                          const wmEvent *event);
static void ui_do_but_extra_operator_icons_mousemove(uiBut *but,
                                                     uiHandleButtonData *data,
                                                     const wmEvent *event);
static void ui_numedit_begin_set_values(uiBut *but, uiHandleButtonData *data);

#ifdef USE_DRAG_MULTINUM
static void ui_multibut_restore(bContext *C, uiHandleButtonData *data, uiBlock *block);
static uiButMultiState *ui_multibut_lookup(uiHandleButtonData *data, const uiBut *but);
#endif

/* buttons clipboard */
static ColorBand but_copypaste_coba = {0};
static CurveMapping but_copypaste_curve = {0};
static bool but_copypaste_curve_alive = false;
static CurveProfile but_copypaste_profile = {0};
static bool but_copypaste_profile_alive = false;

/** \} */

/* -------------------------------------------------------------------- */
/** \name UI Queries
 * \{ */

bool btn_is_editing(const Btn *btn)
{
  const BtnHandleData *data = btn->active;
  return (data && ELEM(data->state, BTN_STATE_TEXT_EDITING, BUTTON_STATE_NUM_EDITING));
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
          (btn_a->point == btn_b->point) && (btn_a->apipoint.type == btn_b->apipoint.type) &&
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
