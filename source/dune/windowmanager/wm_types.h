/* Overview of Win structs
 *
 * - WndwMngr.wins -> Win <br>
 *   Win stores a list of windows.
 *
 *   - Win.screen -> Screen <br>
 *     Win has an active screen.
 *
 *     - Screen.areabase -> ScrArea <br>
 *       Link to ScrArea.
 *
 *       - ScrArea.spacedata <br>
 *         Stores multiple spaces via space links.
 *
 *         - SpaceLink <br>
 *           Base struct for space data for all different space types.
 *
 *       - ScrArea.rgnbase -> ARegion <br>
 *         Stores multiple rgns.
 *
 *     - Screen.rgnbase -> ARgn <br>
 *       Global screen level rgns, e.g. popups, popovers, menus.
 *
 *   - Win.global_areas -> ScrAreaMap <br>
 *     Global screen via 'areabase', e.g. top-bar & status-bar.
 *
 *
 * Win Layout
 *
 * Win -> Screen
 * +----------------------------------------------------------+
 * |+-----------------------------------------+-------------+ |
 * ||ScrArea (links to 3D view)               |ScrArea      | |
 * ||+-------++----------+-------------------+|(links to    | |
 * |||ARgn||          |ARegion (quad view)|| properties) | |
 * |||(tools)||          |                   ||             | |
 * |||       ||          |                   ||             | |
 * |||       ||          |                   ||             | |
 * |||       ||          |                   ||             | |
 * |||       |+----------+-------------------+|             | |
 * |||       ||          |                   ||             | |
 * |||       ||          |                   ||             | |
 * |||       ||          |                   ||             | |
 * |||       ||          |                   ||             | |
 * |||       ||          |                   ||             | |
 * ||+-------++----------+-------------------+|             | |
 * |+-----------------------------------------+-------------+ |
 * +----------------------------------------------------------+
 *
 * Space Data
 *
 * ScrArea's store a list of space data (SpaceLinks), each of unique type.
 * The first one is the displayed in the UI, others are added as needed.
 *
 * +----------------------------+  <-- area->spacedata.first;
 * |                            |
 * |                            |---+  <-- other inactive SpaceLink's stored.
 * |                            |   |
 * |                            |   |---+
 * |                            |   |   |
 * |                            |   |   |
 * |                            |   |   |
 * |                            |   |   |
 * +----------------------------+   |   |
 *    |                             |   |
 *    +-----------------------------+   |
 *       |                              |
 *       +------------------------------+
 *
 * A common way to get the space from the ScrArea:
 * code{.c}
 * if (area->spacetype == SPACE_VIEW3D) {
 *     View3D *v3d = area->spacedata.first;
 *     ... */

#pragma once

struct Id;
struct ImBuf;
struct Cxt;
struct WinDrag;
struct WinDropBox;
struct WinEv;
struct WinOp;
struct WinMngr;

#include "lib_compiler_attrs.h"
#include "lib_utildefines.h"
#include "types_list.h"
#include "types_uuid_types.h"
#include "types_vec_types.h"
#include "types_xr_types.h"
#include "api_types.h"

/* exported types for Win */
#include "gizmo/win_gizmo_types.h"
#include "win_cursors.h"
#include "win_ev_types.h"

/* Include external gizmo API's */
#include "gizmo/win_gizmo_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*WinGenericUserDataFreeFn)(void *data);

typedef struct WinGenericUserData {
  void *data;
  /* When NULL, use MEM_freeN. */
  WinGenericUserDataFreeFn free_fn;
  bool use_free;
} WinGenericUserData;

typedef void (*WinGenericCbFn)(struct Cxt *C, void *user_data);

typedef struct WinGenericCb {
  WinGenericCbFn ex;
  void *user_data;
  WinGenericUserDataFreeFn free_user_data;
} WinGenericCb;

/* WinOpType */
/* WinOpType.flag */
enum {
  /* Register ops in stack after finishing (needed for redo). */
  OPTYPE_REGISTER = (1 << 0),
  /* Do an undo push after the runs. */
  OPTYPE_UNDO = (1 << 1),
  /* Let Dune grab all input from the WM (X11). */
  OPTYPE_BLOCKING = (1 << 2),
  OPTYPE_MACRO = (1 << 3),

  /* Grabs the cursor and optionally enables continuous cursor wrapping. */
  OPTYPE_GRAB_CURSOR_XY = (1 << 4),
  /* Only warp on the X axis. */
  OPTYPE_GRAB_CURSOR_X = (1 << 5),
  /* Only warp on the Y axis. */
  OPTYPE_GRAB_CURSOR_Y = (1 << 6),

  /* Show preset menu. */
  OPTYPE_PRESET = (1 << 7),

  /* Some ops are mainly for internal use and don't make sense
   * to be accessed from the search menu, even if poll() returns true.
   * Currently only used for the search toolbox.l */
  OPTYPE_INTERNAL = (1 << 8),

  /* Allow op to run when interface is locked. */
  OPTYPE_LOCK_BYPASS = (1 << 9),
  /* Special type of undo which doesn't store itself multiple times. */
  OPTYPE_UNDO_GROUPED = (1 << 10),

  /* Depends on the cursor location, when activated from a menu wait for mouse press.
   *
   * In practice these operators often end up being accessed:
   * - Directly from key bindings.
   * - As tools in the toolbar.
   *
   * Even so, accessing from the menu should behave usefully. */
  OPTYPE_DEPENDS_ON_CURSOR = (1 << 11),
};

/* For wm_cursor_grab_enable wrap axis. */
enum {
  WIN_CURSOR_WRAP_NONE = 0,
  WIN_CURSOR_WRAP_X,
  WIN_CURSOR_WRAP_Y,
  WIN_CURSOR_WRAP_XY,
};

/* Cxt to call op in for win_op_name_call.
 * api_ui.c contains EnumPropItem's of these, keep in sync. */
typedef enum WinOpCallCxt {
  /* if there's invoke, call it, otherwise ex */
  WIN_OP_INVOKE_DEFAULT,
  WIN_OP_INVOKE_RGN_WIN,
  WIN_OP_INVOKE_RGN_CHANNELS,
  WIN_OP_INVOKE_RGN_PREVIEW,
  WIN_OP_INVOKE_AREA,
  WIN_OP_INVOKE_SCREEN,
  /* only call exec */
  WIN_OP_EX_DEFAULT,
  WIN_OP_EX_RGN_WIN,
  WIN_OP_EX_RGN_CHANNELS,
  WIN_OP_EX_RGN_PREVIEW,
  WIN_OP_EX_AREA,
  Win_OP_EX_SCREEN,
} WinOpCallCxt;

#define WIN_OP_CXT_HAS_AREA(type) \
  (CHECK_TYPE_INLINE(type, WinOpCallCxt), \
   !ELEM(type, WIN_OP_INVOKE_SCREEN, WIN_OP_EX_SCREEN))
#define WIN_OP_CXT_HAS_RGN(type) \
  (WIN_OP_CXT_HAS_AREA(type) && !ELEM(type, WIN_OP_INVOKE_AREA, WIN_OP_EX_AREA))

/* prop tags for ApiOpProps */
typedef enum eOpPropTags {
  OP_PROP_TAG_ADVANCED = (1 << 0),
} eOpPropTags;
#define OP_PROP_TAG_ADVANCED ((eOpPropTags)OP_PROP_TAG_ADVANCED)

/* WinKeyMapIte */
/* Mod keys, not actually used for WinKeyMapItem (never stored in Types), used for:
 * - WinEv.mod wo the `KM_*_ANY` flags.
 * - win_keymap_add_item & wim_modalkeymap_add_item */
enum {
  KM_SHIFT = (1 << 0),
  KM_CTRL = (1 << 1),
  KM_ALT = (1 << 2),
  /** Use for Windows-Key on MS-Windows, Command-key on macOS and Super on Linux. */
  KM_OSKEY = (1 << 3),

  /* Used for key-map item creation fn args. */
  KM_SHIFT_ANY = (1 << 4),
  KM_CTRL_ANY = (1 << 5),
  KM_ALT_ANY = (1 << 6),
  KM_OSKEY_ANY = (1 << 7),
};

/* `KM_MOD_*` flags for WinKeyMapItem and `WinEv.alt/shift/oskey/ctrl`. */
/* Note that KM_ANY and KM_NOTHING are used with these defines too. */
#define KM_MOD_HELD 1

/* WinKeyMapItem.type
 * Most types are defined in `win_ev_types.h` */
enum {
  KM_TXTINPUT = -2,
};

/* WinKeyMapItem.val */
enum {
  KM_ANY = -1,
  KM_NOTHING = 0,
  KM_PRESS = 1,
  KM_RELEASE = 2,
  KM_CLICK = 3,
  KM_DBL_CLICK = 4,
  /* The cursor location at the point dragging starts is set to WinEv.prev_press_xy
   * some ops such as box sel should use this location instead of WinEv.xy. */
  KM_CLICK_DRAG = 5,
};

/* WinKeyMapItem.direction
 * Direction set for KM_CLICK_DRAG key-map items. KM_ANY (-1) to ignore direction */
enum {
  KM_DIRECTION_N = 1,
  KM_DIRECTION_NE = 2,
  KM_DIRECTION_E = 3,
  KM_DIRECTION_SE = 4,
  KM_DIRECTION_S = 5,
  KM_DIRECTION_SW = 6,
  KM_DIRECTION_W = 7,
  KM_DIRECTION_NW = 8,
};

/* UI Handler */
#define WIN_UI_HANDLER_CONTINUE 0
#define WIN_UI_HANDLER_BREAK 1

/* Notifiers */
typedef struct WinNotifier {
  struct  WinNotifier *next, *prev;
  const struct Win *win;
  unsigned int category, data, subtype, action;
  void *ref;
} WinNotifier;

/* 4 levels
 *
 * 0xFF000000; category
 * 0x00FF0000; data
 * 0x0000FF00; data subtype (unused?)
 * 0x000000FF; action */

/* category */
#define NOTE_CATEGORY 0xFF000000
#define NC_WM (1 << 24)
#define NC_WIN (2 << 24)
#define NC_SCREEN (3 << 24)
#define NC_SCENE (4 << 24)
#define NC_OB (5 << 24)
#define NC_MATERIAL (6 << 24)
#define NC_TEXTURE (7 << 24)
#define NC_LAMP (8 << 24)
#define NC_GROUP (9 << 24)
#define NC_IMG (10 << 24)
#define NC_BRUSH (11 << 24)
#define NC_TXT (12 << 24)
#define NC_WORLD (13 << 24)
#define NC_ANIM (14 << 24)
/* When passing a space as ref data w this (e.g. `win_ev_add_notifier(..., space)`),
 * the notifier will only be sent to this space. That avoids unnecessary updates for unrelated
 * spaces. */
#define NC_SPACE (15 << 24)
#define NC_GEOM (16 << 24)
#define NC_NODE (17 << 24)
#define NC_ID (18 << 24)
#define NC_PAINTCURVE (19 << 24)
#define NC_MOVIECLIP (20 << 24)
#define NC_MASK (21 << 24)
#define NC_PEN (22 << 24)
#define NC_LINESTYLE (23 << 24)
#define NC_CAMERA (24 << 24)
#define NC_LIGHTPROBE (25 << 24)
/* Changes to asset data in the current .blend. */
#define NC_ASSET (26 << 24)

/* data type, 256 entries is enough, it can overlap */
#define NOTE_DATA 0x00FF0000

/* NC_WIN winmngr */
#define ND_FILEREAD (1 << 16)
#define ND_FILESAVE (2 << 16)
#define ND_DATACHANGED (3 << 16)
#define ND_HISTORY (4 << 16)
#define ND_JOB (5 << 16)
#define ND_UNDO (6 << 16)
#define ND_XR_DATA_CHANGED (7 << 16)
#define ND_LIB_OVERRIDE_CHANGED (8 << 16)

/* NC_SCREEN */
#define ND_LAYOUTBROWSE (1 << 16)
#define ND_LAYOUTDELETE (2 << 16)
#define ND_ANIMPLAY (4 << 16)
#define ND_PEN (5 << 16)
#define ND_LAYOUTSET (6 << 16)
#define ND_SKETCH (7 << 16)
#define ND_WORKSPACE_SET (8 << 16)
#define ND_WORKSPACE_DELETE (9 << 16)

/* NC_SCENE Scene */
#define ND_SCENEBROWSE (1 << 16)
#define ND_MARKERS (2 << 16)
#define ND_FRAME (3 << 16)
#define ND_RENDER_OPTIONS (4 << 16)
#define ND_NODES (5 << 16)
#define ND_SEQ (6 << 16)
/* NOTE: If an ob was added, removed, merged/joined, ..., it is not enough to notify with
 * this. This affects the layer so also send a layer change notifier (e.g. ND_LAYER_CONTENT)! */
#define ND_OB_ACTIVE (7 << 16)
/* See comment on ND_OB_ACTIVE. */
#define ND_OB_SEL (8 << 16)
#define ND_OB_VISIBLE (9 << 16)
#define ND_OB_RENDER (10 << 16)
#define ND_MODE (11 << 16)
#define ND_RENDER_RESULT (12 << 16)
#define ND_COMPO_RESULT (13 << 16)
#define ND_KEYINGSET (14 << 16)
#define ND_TOOLSETTINGS (15 << 16)
#define ND_LAYER (16 << 16)
#define ND_FRAME_RANGE (17 << 16)
#define ND_TRANSFORM_DONE (18 << 16)
#define ND_WORLD (92 << 16)
#define ND_LAYER_CONTENT (101 << 16)

/* NC_OB Ob */
#define ND_TRANSFORM (18 << 16)
#define ND_OB_SHADING (19 << 16)
#define ND_POSE (20 << 16)
#define ND_BONE_ACTIVE (21 << 16)
#define ND_BONE_SEL (22 << 16)
#define ND_DRW (23 << 16)
#define ND_MOD (24 << 16)
#define ND_KEYS (25 << 16)
#define ND_CONSTRAINT (26 << 16)
#define ND_PARTICLE (27 << 16)
#define ND_POINTCACHE (28 << 16)
#define ND_PARENT (29 << 16)
#define ND_LOD (30 << 16)
#define ND_DRW_RENDER_VIEWPORT \
  (31 << 16) /* for camera & seq viewport update, also /w NC_SCENE */
#define ND_SHADERFX (32 << 16)
/* For updating motion paths in 3dview. */
#define ND_DRW_ANIMVIZ (33 << 16)

/* NC_MATERIAL Material */
#define ND_SHADING (30 << 16)
#define ND_SHADING_DRW (31 << 16)
#define ND_SHADING_LINKS (32 << 16)
#define ND_SHADING_PREVIEW (33 << 16)

/* NC_LAMP Light */
#define ND_LIGHTING (40 << 16)
#define ND_LIGHTING_DRW (41 << 16)

/* NC_WORLD World */
#define ND_WORLD_DRW (45 << 16)

/* NC_TXT Text */
#define ND_CURSOR (50 << 16)
#define ND_DISPLAY (51 << 16)

/* NC_ANIM Animator */
#define ND_KEYFRAME (70 << 16)
#define ND_KEYFRAME_PROP (71 << 16)
#define ND_ANIMCHAN (72 << 16)
#define ND_NLA (73 << 16)
#define ND_NLA_ACTCHANGE (74 << 16)
#define ND_FCURVES_ORDER (75 << 16)
#define ND_NLA_ORDER (76 << 16)

/* NC_PEN */
#define ND_PEN_EDITMODE (85 << 16)

/* NC_GEOM Geometry */
/* Mesh, Curve, MetaBall, Armature, etc. */
#define ND_SEL (90 << 16)
#define ND_DATA (91 << 16)
#define ND_VERTEX_GROUP (92 << 16)

/* NC_NODE Nodes */

/* NC_SPACE */
#define ND_SPACE_CONSOLE (1 << 16)     /* general redraw */
#define ND_SPACE_INFO_REPORT (2 << 16) /* update for reports, could specify type */
#define ND_SPACE_INFO (3 << 16)
#define ND_SPACE_IMG (4 << 16)
#define ND_SPACE_FILE_PARAMS (5 << 16)
#define ND_SPACE_FILE_LIST (6 << 16)
#define ND_SPACE_ASSET_PARAMS (7 << 16)
#define ND_SPACE_NODE (8 << 16)
#define ND_SPACE_OUTLINER (9 << 16)
#define ND_SPACE_VIEW3D (10 << 16)
#define ND_SPACE_PROPERTIES (11 << 16)
#define ND_SPACE_TEXT (12 << 16)
#define ND_SPACE_TIME (13 << 16)
#define ND_SPACE_GRAPH (14 << 16)
#define ND_SPACE_DOPESHEET (15 << 16)
#define ND_SPACE_NLA (16 << 16)
#define ND_SPACE_SEQ (17 << 16)
#define ND_SPACE_NODE_VIEW (18 << 16)
/* Sent to a new editor type after it's replaced an old one. */
#define ND_SPACE_CHANGED (19 << 16)
#define ND_SPACE_CLIP (20 << 16)
#define ND_SPACE_FILE_PREVIEW (21 << 16)
#define ND_SPACE_SPREADSHEET (22 << 16)

/* NC_ASSET */
/* Denotes that the AssetList is done reading some previews. NOT that the preview generation of
 * assets is done. */
#define ND_ASSET_LIST (1 << 16)
#define ND_ASSET_LIST_PREVIEW (2 << 16)
#define ND_ASSET_LIST_READING (3 << 16)
/* Catalog data changed, requiring a redraw of catalog UIs. Note that this doesn't denote a
 * reloading of asset libraries & their catalogs should happen. That only happens on explicit user
 * action. */
#define ND_ASSET_CATALOGS (4 << 16)

/* subtype, 256 entries too */
#define NOTE_SUBTYPE 0x0000FF00

/* subtype scene mode */
#define NS_MODE_OB (1 << 8)

#define NS_EDITMODE_MESH (2 << 8)
#define NS_EDITMODE_CURVE (3 << 8)
#define NS_EDITMODE_SURFACE (4 << 8)
#define NS_EDITMODE_TXT (5 << 8)
#define NS_EDITMODE_MBALL (6 << 8)
#define NS_EDITMODE_LATTICE (7 << 8)
#define NS_EDITMODE_ARMATURE (8 << 8)
#define NS_MODE_POSE (9 << 8)
#define NS_MODE_PARTICLE (10 << 8)
#define NS_EDITMODE_CURVES (11 << 8)

/* subtype 3d view editing */
#define NS_VIEW3D_GPU (16 << 8)
#define NS_VIEW3D_SHADING (17 << 8)

/* subtype layer editing */
#define NS_LAYER_COLLECTION (24 << 8)

/* action classification */
#define NOTE_ACTION (0x000000FF)
#define NA_ED 1
#define NA_EVAL 2
#define NA_ADD 3
#define NA_REMOVE 4
#define NA_RENAME 5
#define NA_SEL 6
#define NA_ACTIVATE 7
#define NA_PAINT 8
#define NA_JOB_FINISH 9

/* Gesture Manager data */
/* WinGesture->type */
#define WIN_GESTURE_LINES 1
#define WIN_GESTURE_RECT 2
#define WIN_GESTURE_CROSS_RECT 3
#define WIN_GESTURE_LASSO 4
#define WIN_GESTURE_CIRCLE 5
#define WIN_GESTURE_STRAIGHTLINE 6

/* WinGesture is registered to Win.gesture, handled by op cbs. */
typedef struct WinGesture {
  struct WinGesture *next, *prev;
  /* WinEv.type */
  int ev_type;
  /* WinEv.mod */
  uint8_t ev_mod;
  /* WinEv.keymod */
  short ev_keymod;
  /* Gesture type define. */
  int type;
  /* bounds of rgn to drw gesture within. */
  rcti winrct;
  /* optional, amount of points stored. */
  int points;
  /* optional, max amount of points stored. */
  int points_alloc;
  int modal_state;
  /* optional, drw the active side of the straightline gesture. */
  bool drw_active_side;

  /* For modal ops which may be running idle, waiting for an ev to activate the gesture.
   * Typically this is set when the user is click-dragging the gesture
   * (box and circle select for eg).  */
  uint is_active : 1;
  /* Prev val of is-active (use to detect first run & edge cases). */
  uint is_active_prev : 1;
  /* Use for gestures that support both immediate or delayed activation. */
  uint wait_for_input : 1;
  /* Use for gestures that can be moved, like box selection */
  uint move : 1;
  /* For gestures that support snapping, stores if snapping is enabled using the modal keymap
   * toggle. */
  uint use_snap : 1;
  /* For gestures that support flip, stores if flip is enabled using the modal keymap
   * toggle. */
  uint use_flip : 1;

  /* customdata
   * - for border is a rcti.
   * - for circle is recti, (xmin, ymin) is center, xmax radius.
   * - for lasso is short array.
   * - for straight line is a recti: (xmin,ymin) is start, (xmax, ymax) is end. */
  void *customdata;

  /* Free ptrr to use for op allocs (if set, its freed on exit). */
  WinGenericUserData user_data;
} WinGesture;

/* WinEv */
typedef enum eWinEvFlag {
  /* True if the operating system inverted the delta x/y vals and resulting
   * `prev_xy` vals, for natural scroll direction.
   * For absolute scroll direction, the delta must be negated again  */
  WIN_EV_SCROLL_INVERT = (1 << 0),
  /* Generated by auto-repeat, note that this must only ever be set for keyboard events
   * where `ISKEYBOARD(ev->type) == true`.
   * See KMI_REPEAT_IGNORE for details on how key-map handling uses this. */
  WIN_EV_IS_REPEAT = (1 << 1),
  /* Mouse-move events may have this flag set to force creating a click-drag event
   * even when the threshold has not been met. */
  WIN_EV_FORCE_DRAG_THRESHOLD = (1 << 2),
} eWinEvFlag;

typedef struct WinTabletData {
  /* 0=EV_TABLET_NONE, 1=EV_TABLET_STYLUS, 2=EV_TABLET_ERASER. */
  int active;
  /* range 0.0 (not touching) to 1.0 (full pressure). */
  float pressure;
  /* range 0.0 (upright) to 1.0 (tilted fully against the tablet surface). */
  float x_tilt;
  /* as above. */
  float y_tilt;
  /* Interpret mouse motion as absolute as typical for tablets. */
  char is_motion_absolute;
} WinTabletData;

/* Each ev should have full mod state.
 * event comes from ev manager and from keymap.
 *
 * Prev State (`prev_*`)
 * Evs hold information about the prev ev.
 *
 * - Prev vals are only set for evs types that generate KM_PRESS.
 *   See: ISKEYBOARD_OR_BTN.
 *
 * - Prev x/y are exceptions: WinEv.prev
 *   these are set on mouse motion, see MOUSEMOVE & track-pad events.
 *
 * - Modal key-map handling sets `prev_val` & `prev_type` to `val` & `type`,
 *   this allows modal keys-maps to check the original values (needed in some cases).
 *
 * Press State (`prev_press_*`)
 *
 * Evs hold info about the state when the last KM_PRESS ev was added.
 * This is used for generating KM_CLICK, KM_DBL_CLICK & KM_CLICK_DRAG evs.
 * See win_handlers_do for the implementation.
 *
 * - Prev vals are only set when a KM_PRESS ev is detected.
 *   See: ISKEYBOARD_OR_BTN.
 *
 * - The reason to differentiate between "press" and the prev ev state is
 *   the prev ev may be set by key-release evs. In the case of a single key click
 *   this isn't a problem however releasing other keys such as mods prevents click/click-drag
 *   evs from being detected, see: T89989.
 *
 * - Mouse-wheel evs are excluded even though they generate KM_PRESS
 *   as clicking and dragging don't make sense for mouse wheel evs. */
typedef struct WinEv {
  struct WinEv *next, *prev;

  /* Ev code itself (short, is also in key-map). */
  short type;
  /* Press, release, scroll-val. */
  short val;
  /* Mouse ptrr position, screen coord. */
  int xy[2];
  /* Rgn relative mouse position (name convention before Blender 2.5). */
  int mval[2];
  /* From, ghost if utf8 is enabled for the platform,
   * llib_str_utf8_size() must _always_ be valid, check
   * when assigning s we don't need to check on every access after. */
  char utf8_buf[6];
  /* From ghost, fallback if utf8 isn't set. */
  char ascii;

  /* Mod states: KM_SHIFT, KM_CTRL, KM_ALT & KM_OSKEY. */
  uint8_t mod;

  /* The direction (for KM_CLICK_DRAG evs only). */
  int8_t direction;

  /* Raw-key mod (allow using any key as a mod).
   * Compatible with vals in `type`. */
  short keymod;

  /* Tablet info, available for mouse move and button evs. */
  WinTabletData tablet;

  eWinEvFlag flag;

  /* Custom data. */
  /* Custom data type, stylus, 6-DOF, see `win_ev_types.h`. */
  short custom;
  short customdata_free;
  /* Ascii, unicode, mouse-coords, angles, vectors, NDOF data, drag-drop info. */
  void *customdata;

  /* Prev State. */
  /* The prev val of `type`. */
  short prev_type;
  /* The prev val of `val`. */
  short prev_val;
  /* The prev val of WinEv.xy,
   * Unlike other prev state vars, this is set on any mouse motion.
   * Use `prev_press_*` for the val at time of pressing. */
  int prev_xy[2];

  /* Prev Press State (when `val == KM_PRESS`). */
  /* The `type` at the point of the press action. */
  short prev_press_type;
  /* The location when the key is pressed.
   * used to enforce drag threshold & calc the `direction` */
  int prev_press_xy[2];
  /* The `mod` at the point of the press action. */
  uint8_t prev_press_mod;
  /* The `keymod` at the point of the press action. */
  short prev_press_keymod;
  /* The time when the key is pressed, see PIL_check_seconds_timer.
   * Used to detect double-click events.  */
  double prev_press_time;
} WinEv;

/* Vals below are ignored when detecting if the user intentionally moved the cursor.
 * Keep this very small since it's used for sel cycling for eg,
 * where we want intended adjustments to pass this threshold and sel new items.
 *
 * Always check for <= this val since it may be zero. */
#define WIN_EV_CURSOR_MOTION_THRESHOLD ((float)U.move_threshold * U.dpi_fac)

/* Motion progress, for modal handlers. */
typedef enum {
  P_NOT_STARTED,
  P_STARTING,    /* <-- */
  P_IN_PROGRESS, /* <-- only these are sent for NDOF motion. */
  P_FINISHING,   /* <-- */
  P_FINISHED,
} WinProgress;

#ifdef WITH_INPUT_NDOF
typedef struct WinNDOFMotionData {
  /* awfully similar to GHOST_TEventNDOFMotionData... */
  /* Each component normally ranges from -1 to +1, but can exceed that.
   * These use dune standard view coords,
   * with positive rotations being CCW about the axis. */
  /* Translation. */
  float tvec[3];
  /* Rotation.
   * axis = (rx,ry,rz).normalized.
   * amount = (rx,ry,rz).magnitude [in revolutions, 1.0 = 360 deg] */
  float rvec[3];
  /* Time since previous NDOF Motion event. */
  float dt;
  /* Is this the first event, the last, or one of many in between? */
  WinProgress progress;
} WinNDOFMotionData;
#endif /* WITH_INPUT_NDOF */

#ifdef WITH_XR_OPENXR
/* Similar to GHOST_XrPose. */
typedef struct WinXrPose {
  float position[3];
  /* Dune convention (w, x, y, z) */
  float orientation_quat[4];
} WinXrPose;

typedef struct WinXrActionState {
  union {
    bool state_bool;
    float state_float;
    float state_vector2f[2];
    WinXrPose state_pose;
  };
  int type; /* eXrActionType */
} WinXrActionState;

typedef struct WinXrActionData {
  /* Action set name. */
  char action_set[64];
  /* Action name. */
  char action[64];
  /* Type. */
  eXrActionType type;
  /* State. Set appropriately based on type. */
  float state[2];
  /* State of the other sub-action path for bimanual actions. */
  float state_other[2];

  /* Input threshold for float/vector2f actions. */
  float float_threshold;

  /* Controller aim pose corresponding to the action's sub-action path. */
  float controller_loc[3];
  float controller_rot[4];
  /* Controller aim pose of the other sub-action path for bimanual actions. */
  float controller_loc_other[3];
  float controller_rot_other[4];

  /* Op. */
  struct WinOpType *ot;
  struct IdProp *op_props;

  /* Whether bimanual interaction is occurring. */
  bool bimanual;
} WinXrActionData;
#endif

/* Timer flags. */
typedef enum {
  /* Do not attempt to free custom-data pointer even if non-NULL. */
  WIN_TIMER_NO_FREE_CUSTOM_DATA = 1 << 0,
} WinTimerFlags;

typedef struct WinTimer {
  struct WinTimer *next, *prev;

  /* Win this timer is attached to (optional). */
  struct Win *win;

  /* Set by timer user. */
  double timestep;
  /* Set by timer user, goes to event system. */
  int event_type;
  /* Various flags controlling timer options, see below. */
  WinTimerFlags flags;
  /* Set by timer user, to allow custom values. */
  void *customdata;

  /* Total running time in seconds. */
  double duration;
  /* Time since previous step in seconds. */
  double delta;

  /* Internal, last time timer was activated. */
  double ltime;
  /* Internal, next time we want to activate the timer. */
  double ntime;
  /* Internal, when the timer started. */
  double stime;
  /* Internal, put timers to sleep when needed. */
  bool sleep;
} WinTimer;

typedef struct WinOpType {
  /* Text for UI, undo. */
  const char *name;
  /* Unique id. */
  const char *idname;
  const char *lang_cxt;
  /* Use for tool-tips and Python docs. */
  const char *description;
  /* Id to group ops together. */
  const char *undo_group;

  /* This cb ex the op wo any interactive input,
   * params may be provided through op props. cannot use
   * any interface code or input device state.
   * See defines below for return values */
  int (*ex)(struct Cxt *, struct wmOp *) ATTR_WARN_UNUSED_RESULT;

  /* This cb executes on a running op whenever as prop
   * is changed. It can correct its own props or report errors for
   * invalid settings in exceptional cases.
   * Bool return value, True denotes a change has been made and to redraw. */
  bool (*check)(struct Cxt *, struct WinOp *);

  /* For modal temporary ops, initially invoke is called. then
   * any further events are handled in modal. if the op is
   * canceled due to some external reason, cancel is called
   * See defines below for return vals. */
  int (*invoke)(struct Cxt *,
                struct WinOp *,
                const struct WinEv *) ATTR_WARN_UNUSED_RESULT;

  /* Called when a modal op is canceled (not used often).
   * Internal cleanup can be done here if needed */
  void (*cancel)(struct Cxt *, struct wmOp *);

  /* Modal is used for ops which continuously run, eg:
   * fly mode, knife tool, circle select are all examples of modal ops.
   * Modal ops can handle events which would normally access other ops,
   * they keep running until they don't return `OP_RUNNING_MODAL`. */
  int (*modal)(struct Cxt *,
               struct WinOp *,
               const struct WinEv *) ATTR_WARN_UNUSED_RESULT;

  /* Verify if the op can be ex in the current cxt, note
   * that the op might still fail to ex even if this return true. */
  bool (*poll)(struct Cxt *) ATTR_WARN_UNUSED_RESULT;

  /* Use to check if props should be displayed in auto-generated UI.
   * Use 'check' cb to enforce refreshing. */
  bool (*poll_prop)(const struct Cxt *C,
                        struct WinOp *op,
                        const ApiProp *prop) ATTR_WARN_UNUSED_RESULT;

  /* Optional pnl for redo and repeat, auto-generated if not set. */
  void (*ui)(struct Cxt *, struct wmOperator *);

  /* Return a different name to use in the user interface, based on prop values.
   * The returned string does not need to be freed. */
  const char *(*get_name)(struct wmOpType *, struct ApiPtr *);

  /* Return a different description to use in the user interface, based on prop values.
   * The returned string must be freed by the caller, unless */
  char *(*get_description)(struct bContext *C, struct wmOpType *, struct ApiPtr *);

  /* api for props */
  struct ApiStruct *sapi;

  /* prev settings - for initializing on re-use */
  struct IdProp *last_props;

  /* Default api prop to use for generic invoke fns.
   * menus, enum search... etc. Example: Enum 'type' for a Delete menu.
   * When assigned a string/number prop,
   * immediately edit the value when used in a popup. see: UI_BTN_ACTIVATE_ON_INIT.  */
  ApiProp *prop;

  /* struct WinOpTypeMacro */
  List macro;

  /* ptr to modal keymap, do not free! */
  struct WinKeyMap *modalkeymap;

  /* python needs the operator type as well */
  bool (*pyop_poll)(struct Cxt *, struct wmOpType *ot) ATTR_WARN_UNUSED_RESULT;

  /* api integration */
  ExtensionApi api_ext;

  /* Cursor to use when waiting for cursor input, see: OPTYPE_DEPENDS_ON_CURSOR. */
  int cursor_pending;

  /* Flag last for padding */
  short flag;

} WinOpType;

/* Wrapper to ref a WinOpType together with some set props and other relevant
 * info to invoke the op in a customizable way. */
typedef struct WinOpCallParams {
  struct WinOpType *optype;
  struct ApiPtr *opptr;
  WinOpCallCxt opcxt;
} WinOpCallParams;

#ifdef WITH_INPUT_IME
/* Input Method Editor (IME) */
/* note similar to GHOST_TEventImeData. */
typedef struct WinIMEData {
  size_t result_len, composite_len;

  /* utf8 encoding */
  char *str_result;
  /* utf8 encoding */
  char *str_composite;

  /* Cursor position in the IME composition. */
  int cursor_pos;
  /* Beginning of the selection. */
  int sel_start;
  /* End of the sel. */
  int sel_end;

  bool is_ime_composing;
} WinIMEData;
#endif

/* Paint Cursor */
typedef void (*WinPaintCursorDrw)(struct Cxt *C, int, int, void *customdata);

/* Drag and drop */

#define WM_DRAG_ID 0
#define WM_DRAG_ASSET 1
/** The user is dragging multiple assets. This is only supported in few specific cases, proper
 * multi-item support for dragging isn't supported well yet. Therefore this is kept separate from
 * WM_DRAG_ASSET. */
#define WM_DRAG_ASSET_LIST 2
#define WM_DRAG_RNA 3
#define WM_DRAG_PATH 4
#define WM_DRAG_NAME 5
#define WM_DRAG_VAL 6
#define WM_DRAG_COLOR 7
#define WM_DRAG_DATASTACK 8
#define WM_DRAG_ASSET_CATALOG 9

typedef enum eWM_DragFlags {
  WIN_DRAG_NOP = 0,
  WIN_DRAG_FREE_DATA = 1,
} eWM_DragFlags;
ENUM_OPS(eWM_DragFlags, WM_DRAG_FREE_DATA)

/* NOTE: structs need not exported? */

typedef struct wmDragId {
  struct wmDragId *next, *prev;
  struct Id *id;
  struct Id *from_parent;
} wmDragId;

typedef struct wmDragAsset {
  /* NOTE: Can't store the #AssetHandle here, since the #FileDirEntry it wraps may be freed while
   * dragging. So store necessary data here directly. */

  char name[64]; /* MAX_NAME */
  /* Always freed. */
  const char *path;
  int id_type;
  struct AssetMetaData *metadata;
  int import_type; /* eFileAssetImportType */

  /* FIXME: This is temporary evil solution to get scene/view-layer/etc in the copy callback of the
   * wmDropBox.
   * TODO: Handle link/append in op called at the end of the drop process, and NOT in its
   * copy cb */
  struct Cxt *evil_C;
} wmDragAsset;

typedef struct wmDragAssetCatalog {
  bUUID drag_catalog_id;
} wmDragAssetCatalog;

/* For some specific cases we support dragging multiple assets (#WM_DRAG_ASSET_LIST). There is no
 * proper support for dragging multiple items in the `wmDrag`/`wmDrop` API yet, so this is really
 * just to enable specific features for assets.
 *
 * This struct basically contains a tagged union to either store a local ID pointer, or information
 * about an externally stored assets */
typedef struct wmDragAssetListItem {
  struct wmDragAssetListItem *next, *prev;

  union {
    struct Id *local_id;
    wmDragAsset *external_info;
  } asset_data;

  bool is_external;
} wmDragAssetListItem;

typedef char *(*WMDropboxTooltipFn)(struct Cxt *,
                                    struct wmDrag *,
                                    const int xy[2],
                                    struct wmDropBox *drop);

typedef struct wmDragActiveDropState {
  /* Informs which dropbox is activated with the drag item.
   * When this value changes, the draw_activate and #draw_deactivate dropbox callbacks are
   * triggered. */
  struct wmDropBox *active_dropbox;

  /* If `active_dropbox` is set, the area it successfully polled in. To restore the context of it
   * as needed. */
  struct ScrArea *area_from;
  /* If `active_dropbox` is set, the region it successfully polled in. To restore the context of
   * it as needed. */
  struct ARegion *region_from;

  /* If `active_dropbox` is set, additional cxt provided by the active (i.e. hovered) button.
   * Activated before cxt sensitive ops (polling, drawing, dropping). */
  struct CxtStore *ui_cxt;

  /* Text to show when a dropbox poll succeeds (so the dropbox itself is available) but the
   * op poll fails. Typically the message the op set with
   * cxt_wm_op_poll_msg_set(). */
  const char *disabled_info;
  bool free_disabled_info;
} wmDragActiveDropState;

typedef struct wmDrag {
  struct wmDrag *next, *prev;

  int icon;
  /** See 'WM_DRAG_' defines above. */
  int type;
  void *poin;
  char path[1024]; /* FILE_MAX */
  double value;

  /* If no icon but imbuf should be drawn around cursor. */
  struct ImBuf *imb;
  float imbuf_scale;

  wmDragActiveDropState drop_state;

  eWM_DragFlags flags;

  /* List of wmDragIds, all are guaranteed to have the same ID type. */
  List ids;
  /* List of `wmDragAssetListItem`s. */
  List asset_items;
} wmDrag;

/* Dropboxes are like keymaps, part of the screen/area/region definition.
 * Allocation and free is on startup and exit.
 *
 * The op is polled and invoked with the current context (WM_OP_INVOKE_DEFAULT), there is no
 * way to override that (by design, since dropboxes should act on the exact mouse position). So the
 * drop-boxes are supposed to check the required area and region context in their poll. */
typedef struct wmDropBox {
  struct wmDropBox *next, *prev;

  /* Test if the dropbox is active. */
  bool (*poll)(struct Cxt *C, struct wmDrag *drag, const wmEvent *event);

  /* Before ex, this copies drag info to #wmDrop properties. */
  void (*copy)(struct wmDrag *drag, struct wmDropBox *drop);

  /* If the op is canceled (returns `OPERATOR_CANCELLED`), this can be used for cleanup of
   * `copy()` resources. */
  void (*cancel)(struct Main *main, struct wmDrag *drag, struct wmDropBox *drop);

  /**
   * Override the default drawing function.
   * param xy: Cursor location in window coordinates (#wmEvent.xy compatible). */
  void (*draw)(struct Cxt *C, struct wmWindow *win, struct wmDrag *drag, const int xy[2]);

  /* Called when pool returns true the first time. */
  void (*draw_activate)(struct wmDropBox *drop, struct wmDrag *drag);

  /* Called when pool returns false the first time or when the drag event ends. */
  void (*draw_deactivate)(struct wmDropBox *drop, struct wmDrag *drag);

  /* Custom data for drawing. */
  void *draw_data;

  /**l Custom tooltip shown during dragging. */
  WMDropboxTooltipFn tooltip;

  /* If poll succeeds, operator is called.
   * Not saved in file, so can be pointer.  */
  wmOpeType *ot;

  /* Op props, assigned to ptr->data and can be written to a file. */
  struct IdProp *props;
  /* api ptr to access props. */
  struct ApiPtr *ptr;
} wmDropBox;

/* Struct to store tool-tip timer and possible creation if the time is reached.
 * Allows UI code to call #WM_tooltip_timer_init without each user having to handle the timer. */
typedef struct wmTooltipState {
  /* Create tooltip on this event. */
  struct wmTimer *timer;
  /* The area the tooltip is created in. */
  struct ScrArea *area_from;
  /* The region the tooltip is created in. */
  struct ARegion *region_from;
  /* The tooltip region. */
  struct ARegion *region;
  /* Create the tooltip region (assign to 'region'). */
  struct ARgn *(*init)(struct Cxt *C,
                          struct ARgn *rgn,
                          int *pass,
                          double *pass_delay,
                          bool *r_exit_on_ev);
  /* Exit on any event, not needed for btns since their highlight state is used. */
  bool exit_on_ev;
  /* Cursor location at the point of tooltip creation. */
  int ev_xy[2];
  /* Pass, use when we want multiple tips, count down to zero. */
  int pass;
} WinTooltipState;

/* migrated stuff, clean later? */
typedef struct RecentFile {
  struct RecentFile *next, *prev;
  char *filepath;
} RecentFile;

/* Logging */
struct CLG_LogRef;
/* win_init_exit.c */
extern struct CLG_LogRef *WIN_LOG_OPS;
extern struct CLG_LogRef *WIN_LOG_HANDLERS;
extern struct CLG_LogRef *WIN_LOG_EVS;
extern struct CLG_LogRef *WIN_LOG_KEYMAPS;
extern struct CLG_LogRef *WIN_LOG_TOOLS;
extern struct CLG_LogRef *WIN_LOG_MSGBUS_PUB;
extern struct CLG_LogRef *WIN_LOG_MSGBUS_SUB;

#ifdef __cplusplus
}
