#pragma once

#include "s_defs.h"
#include "s_list.h"
#include "s_vec_types.h"
#include "s_view2d_types.h"

#include "s_ID.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ARgn;
struct ARgnType;
struct PnlType;
struct PtrApi;
struct Scene;
struct SpaceLink;
struct SpaceType;
struct uiBlock;
struct uiLayout;
struct uiList;
struct wmDrawBuffer;
struct wmTimer;
struct wmTooltipState;

/* TODO: Doing this is quite ugly :)
 * Once the top-bar is merged bScreen should be refactored to use ScrAreaMap. */
#define AREAMAP_FROM_SCREEN(screen) ((ScrAreaMap *)&(screen)->vertbase)

typedef struct bScreen {
  ID id;

  /* TODO: Should become ScrAreaMap now.
   * NOTE: KEEP ORDER IN SYNC WITH #ScrAreaMap! (see AREAMAP_FROM_SCREEN macro above). */
  /* Screens have vertices/edges to define areas. */
  List vertbase;
  List edgebase;
  List areabase;
  /* End variables that must be in sync with #ScrAreaMap. */

  /* Screen lvl rgns (menus), runtime only. */
  List rgnbase;

  struct Scene *scene TYPES_DEPRECATED;

  /* General flags. */
  short flag;
  /* Winid from WM, starts with 1. */
  short winid;
  /* User-setting for which editors get redrawn during animation playback. */
  short redraws_flag;

  /* Tmp screen in a tmp window, don't save (like user-prefs). */
  char temp;
  /* Temp screen for img render display or file-select. */
  char state;
  /* Notifier for drawing edges. */
  char do_draw;
  /* Notifier for scale screen, changed screen, etc. */
  char do_refresh;
  /* Notifier for gesture draw. */
  char do_draw_gesture;
  /* Notifier for paint cursor draw. */
  char do_draw_paintcursor;
  /* Notifier for dragging draw. */
  char do_draw_drag;
  /* Set to delay screen handling after switching back from maximized area. */
  char skip_handling;
  /* Set when scrubbing to avoid some costly updates. */
  char scrubbing;
  char _pad[1];

  /* Active rgn that has mouse focus. */
  struct ARgn *active_rgn;

  /* If set, screen has timer handler added in window. */
  struct wmTimer *animtimer;
  /* Cx callback. */
  void /*CxDataCb*/ *cx;

  /* Runtime. */
  struct wmTooltipState *tool_tip;

  PreviewImage *preview;
} Screen;

typedef struct ScrVert {
  struct ScrVert *next, *prev, *newv;
  vec2s vec;
  /* first one used internally, second one for tools */
  short flag, editflag;
} ScrVert;

typedef struct ScrEdge {
  struct ScrEdge *next, *prev;
  ScrVert *v1, *v2;
  /* 1 when at edge of screen. */
  short border;
  short flag;
  char _pad[4];
} ScrEdge;

typedef struct ScrAreaMap {
  /* NOTE: KEEP ORDER IN SYNC WITH LISTS IN Screen! */

  /* ScrVert - screens have verts/edges to define areas. */
  List vertlist;
  /* ScrEdge. */
  List edgelist;
  /* ScrArea. */
  List arealist;
} ScrAreaMap;

typedef struct Pnl_Runtime {
  /* Applied to Pnl.ofsx, but saved separately so we can track changes between redraws. */
  int rgn_ofsx;

  char _pad[4];

  /* Ptr for storing which data the panel corresponds to.
   * Useful when there can be multiple instances of the same pnl type.
   *
   * A pnl and its sub-pnls share the same custom data ptr.
   * This avoids freeing the same ptr twice when pnls are removed. */
  struct PtrApi *custom_data_ptr;

  /* Ptr to the pnl's block. Useful when changes to pnl #uiBlocks
   * need some cx from traversal of the pnl "tree". */
  struct uiBlock *block;

  /* Non-owning pointer. The context is stored in the block. */
  struct CxStore *cx;
} Pnl_Runtime;

/* The part from uiBlock that needs saved in file. */
typedef struct Pnl {
  struct Pnl *next, *prev;

  /* Runtime. */
  struct PnlType *type;
  /* Runtime for drawing. */
  struct uiLayout *layout;

  /* Defined as UI_MAX_NAME_STR. */
  char pnlname[64];
  /* Pnl name is id for restoring location. */
  char drawname[64];
  /* Offset w/in the rgn. */
  int ofsx, ofsy;
  /* Pnl size including children. */
  int sizex, sizey;
  /* Pnl size excluding children. */
  int blocksizex, blocksizey;
  short lblofs;
  short flag, runtime_flag;
  char _pad[6];
  /* Pnls are aligned according to increasing sort-order. */
  int sortorder;
  /* Runtime for pnl manipulation. */
  void *activedata;
  /* Sub pnls. */
  List children;

  Pnl_Runtime runtime;
} Pnl;

/* Used for passing expansion between instanced pnl data and the pnls themselves.
 * There are 16 defines bc the expansion data is typically stored in a short.
 *
 * Expansion for instanced pnls is stored in depth 1st order.
 * Example, the val of UI_SUBPNL_DATA_EXPAND_2 correspond to mean the expansion of
 * the 2nd subpnl or the 1st
 * subpnl's 1st subpnl. */
typedef enum uiPanelDataExpansion {
  UI_PNL_DATA_EXPAND_ROOT = (1 << 0),
  UI_SUBPNL_DATA_EXPAND_1 = (1 << 1),
  UI_SUBPNL_DATA_EXPAND_2 = (1 << 2),
  UI_SUBPNL_DATA_EXPAND_3 = (1 << 3),
  UI_SUBPNL_DATA_EXPAND_4 = (1 << 4),
  UI_SUBPNL_DATA_EXPAND_5 = (1 << 5),
  UI_SUBPNL_DATA_EXPAND_6 = (1 << 6),
  UI_SUBPNL_DATA_EXPAND_7 = (1 << 7),
  UI_SUBPNL_DATA_EXPAND_8 = (1 << 8),
  UI_SUBPNL_DATA_EXPAND_9 = (1 << 9),
  UI_SUBPNL_DATA_EXPAND_10 = (1 << 10),
  UI_SUBPNL_DATA_EXPAND_11 = (1 << 11),
  UI_SUBPNL_DATA_EXPAND_12 = (1 << 12),
  UI_SUBPNL_DATA_EXPAND_13 = (1 << 13),
  UI_SUBPNL_DATA_EXPAND_14 = (1 << 14),
  UI_SUBPNL_DATA_EXPAND_15 = (1 << 15),
  UI_SUBPNL_DATA_EXPAND_16 = (1 << 16),
} uiPnlDataExpansion;

/* Notes on Pnl Categories:
 *
 * - #ARgn.pnls_category (#PnlCategoryDyn)
 *   is a runtime only list of categories collected during draw.
 *
 * - #ARgn.pnls_category_active (#PnlCategoryStack)
 *   is a list of strings (category id's).
 *
 * Clicking on a tab moves it to the front of rgn->pnls_category_active,
 * If the cx changes so this tab is no longer displayed,
 * then the 1st-most tab in #ARgn.pnls_category_active is used.
 *
 * This way you can change modes and always have the tab you last clicked on. */

/* rgn lvl tabs */
#
#
typedef struct PnlCategoryDyn {
  struct PnlCategoryDyn *next, *prev;
  char idname[64];
  rcti rect;
} PnlCategoryDyn;

/* Rgn stack of active tabs. */
typedef struct PnlCategoryStack {
  struct PnlCategoryStack *next, *prev;
  char idname[64];
} PnlCategoryStack;

typedef void (*uiListFreeRuntimeDataFn)(struct uiList *ui_list);

/* uiList dynamic data... */
/* These 2 Lines w # tell makestypes this struct can be excluded. */
#
#
typedef struct uiListDyn {
  /* Cb to free UI data when freeing UI-Lists in Dune. */
  uiListFreeRuntimeDataFn free_runtime_data_fn;

  /* Nmbr of rows needed to draw all elems. */
  int height;
  /* Actual visual height of the list (in rows). */
  int visual_height;
  /* Min visual height of the list (in rows). */
  int visual_height_min;

  /* Nmbr of columns drawn for grid layouts. */
  int columns;

  /* Number of items in collection. */
  int items_len;
  /* Nmbr of items actually visible after filtering. */
  int items_shown;

  /* Those are temp data used during drag-resize with GRIP button
   * (they are in pixels, the meaningful data is the
   * diff between resize_prev and resize)... */
  int resize;
  int resize_prev;

  /* Alloc'd custom data. Freed together with the #uiList (and when re-assigning). */
  void *customdata;

  /* Filtering data. */
  /* Items_len length. */
  int *items_filter_flags;
  /* Org_idx -> new_idx, items_len length. */
  int *items_filter_neworder;

  struct wmOpType *custom_drag_optype;
  struct PtrApi *custom_drag_opptr;
  struct wmOpType *custom_activate_optype;
  struct PtrApi *custom_activate_opptr;
} uiListDyn;

typedef struct uiList { /* some list UI data need to be saved in file */
  struct uiList *next, *prev;

  /* Runtime. */
  struct uiListType *type;

  /* Defined as UI_MAX_NAME_STR. */
  char list_id[64];

  /* How items are laid out in the list. */
  int layout_type;
  int flag;

  int list_scroll;
  int list_grip;
  int list_last_len;
  int list_last_activei;

  /* Filtering data. */
  /* Defined as UI_MAX_NAME_STR. */
  char filter_byname[64];
  int filter_flag;
  int filter_sort_flag;

  /* Op executed when activating an item. */
  const char *custom_activate_opname;
  /* Op executed when dragging an item (item gets activated too, wout running
   * custom_activate_opname above). */
  const char *custom_drag_opname;

  /* Custom sub-classes props. */
  IDProp *props;

  /* Dynamic data (runtime). */
  uiListDyn *dyn_data;
} uiList;

typedef struct TransformOrientation {
  struct TransformOrientation *next, *prev;
  /* MAX_NAME. */
  char name[64];
  float mat[3][3];
  char _pad[4];
} TransformOrientation;

/* Some preview UI data need to be saved in file. */
typedef struct uiPreview {
  struct uiPreview *next, *prev;

  /* Defined as #UI_MAX_NAME_STR. */
  char preview_id[64];
  short height;
  char _pad1[6];
} uiPreview;

typedef struct ScrGlobalAreaData {
  /* Global areas have a non-dynamic size. That means, changing the window
   * size doesn't affect their size at all. However, they can still be
   * 'collapsed', by changing this value. Ignores DPI (ED_area_global_size_y
   * and winx/winy don't) */
  short cur_fixed_height;
  /* For global areas, this is the min and max size they can use depending on
   * if they are 'collapsed' or not. */
  short size_min, size_max;
  /* GlobalAreaAlign. */
  short align;

  /* GlobalAreaFlag. */
  short flag;
  char _pad[2];
} ScrGlobalAreaData;

enum GlobalAreaFlag {
  GLOBAL_AREA_IS_HIDDEN = (1 << 0),
};

typedef enum GlobalAreaAlign {
  GLOBAL_AREA_ALIGN_TOP = 0,
  GLOBAL_AREA_ALIGN_BOTTOM = 1,
} GlobalAreaAlign;

typedef struct ScrArea_Runtime {
  struct ToolRef *tool;
  char is_tool_set;
  char _pad0[7];
} ScrArea_Runtime;

typedef struct ScrArea {
  struct ScrArea *next, *prev;

  /* Ordered (bottom-left, top-left, top-right, bottom-right). */
  ScrVert *v1, *v2, *v3, *v4;
  /* If area==full, this is the parent. */
  Screen *full;

  /* Rect bound by v1 v2 v3 v4. */
  rcti totrct;

  /* eSpace_Type (SPACE_FOO).
   *
   * Temporarily used while switching area type, otherwise this should be SPACE_EMPTY.
   * Also, versioning uses it to nicely replace deprecated * editors.
   * It's been there for ages, name doesn't fit any more. */
  char spacetype;
  /* eSpace_Type (SPACE_FOO). */
  char butspacetype;
  short butspacetype_subtype;

  /* Size. */
  short winx, winy;

  /* OLD! 0=no header, 1= down, 2= up. */
  char headertype DNA_DEPRECATED;
  /* Private, for spacetype refresh callback. */
  char do_refresh;
  short flag;
  /* Index of last used rgn of 'RGN_TYPE_WINDOW'
   * runtime var, updated by executing ops. */
  short rgn_active_win;
  char _pad[2];

  /* Cbs for this space type. */
  struct SpaceType *type;

  /* Non-NULL if this area is global. */
  ScrGlobalAreaData *global;

  /* A list of space links (editors) that were open in this area before. When
   * changing the editor type, we try to reuse old editor data from this list.
   * The 1st item is the active/visible one. */
  /* SpaceLink. */
  List spacedata;
  /* This rgn list is the one from the active/visible editor (1st item in
   * spacedata list). Use SpaceLink.rgnbase if it's inactive (but only then)! */
  /* ARgn. */
  List rgnbase;
  /** wmEvHandler. */
  List handlers;

  /** #AZone. */
  List actionzones;

  ScrArea_Runtime runtime;
} ScrArea;

typedef struct ARgn_Runtime {
  /* Pnl category to use between 'layout' and 'draw'. */
  const char *category;

  /* The visible part of the rgn, use w rgn overlap not to draw
   * on top of the overlapping rgns.
   *
   * Lazy init, zero'd when unset, relative to ARgn.winrct x/y min. */
  rcti visible_rect;

  /* The offset needed to not overlap with window scrollbars. Only used by HUD rgns for now. */
  int offset_x, offset_y;

  /* Maps uiBlock->name to uiBlock for faster lookups. */
  struct GHash *block_name_map;
} ARgn_Runtime;

typedef struct ARgn {
  struct ARgn *next, *prev;

  /* 2D-View scrolling/zoom info (most rgns are 2d anyways). */
  View2D v2d;
  /* Coords of rgn. */
  rcti winrct;
  /* Runtime for partial redraw, same or smaller than winrct. */
  rcti drawrct;
  /* Size. */
  short winx, winy;

  /* Rgn is currently visible on screen. */
  short visible;
  /* Window, header, etc. identifier for drawing. */
  short regiontype;
  /* How it should split. */
  short alignment;
  /* Hide, .... */
  short flag;

  /* Current split size in unscaled pixels (if zero it uses rgntype).
   * To convert to pixels use: `UI_DPI_FAC * region->sizex + 0.5f`.
   * However to get the current rgn size, you should usually use winx/winy from above, not this! */
  short sizex, sizey;

  /* Private, cached notifier events. */
  short do_draw;
  /* Private, cached notifier events. */
  short do_draw_paintcursor;
  /* Private, set for indicate drawing overlapped. */
  short overlap;
  /* Temporary copy of flag settings for clean fullscreen. */
  short flagfullscreen;

  /* Cbs for this rgn type. */
  struct ARgnType *type;

  /** #uiBlock. */
  List uiblocks;
  /* Pnl. */
  List pnls;
  /* Stack of pnl categories. */
  List pnls_category_active;
  /** #uiList. */
  List ui_lists;
  /** #uiPreview. */
  List ui_previews;
  /* #wmEventHandler. */
  List handlers;
  /* Pnl categories runtime. */
  List pnls_category;

  /* Gizmo-map of this rgn. */
  struct wmGizmoMap *gizmo_map;
  /* Blend in/out. */
  struct wmTimer *rgntimer;
  struct wmDrawBuffer *draw_buffer;

  /** Use this string to draw info. */
  char *headerstr;
  /** XXX 2.50, need spacedata equivalent? */
  void *rgndata;

  ARgn_Runtime runtime;
} ARgn;

/* #ScrArea.flag */
enum {
  HEADER_NO_PULLDOWN = (1 << 0),
//  AREA_FLAG_UNUSED_1           = (1 << 1),
//  AREA_FLAG_UNUSED_2           = (1 << 2),
#ifdef TYPES_DEPRECATED_ALLOW
  AREA_TEMP_INFO = (1 << 3), /* versioned to make slot reusable */
#endif
  /* Update size of rgns w/in the area. */
  AREA_FLAG_RGN_SIZE_UPDATE = (1 << 3),
  AREA_FLAG_ACTIVE_TOOL_UPDATE = (1 << 4),
  // AREA_FLAG_UNUSED_5 = (1 << 5),

  AREA_FLAG_UNUSED_6 = (1 << 6), /* cleared */

  /* For temporary full-screens (file browser, image editor render)
   * that are opened above user set full-screens. */
  AREA_FLAG_STACKED_FULLSCREEN = (1 << 7),
  /* Update action zones (even if the mouse is not intersecting them). */
  AREA_FLAG_ACTIONZONES_UPDATE = (1 << 8),
};

#define AREAGRID 4
#define AREAMINX 32
#define HEADER_PADDING_Y 6
#define HEADERY (20 + HEADER_PADDING_Y)

/* #Screen.flag */
enum {
  SCREEN_DEPRECATED = 1,
  SCREEN_COLLAPSE_STATUSBAR = 2,
};

/* #Screen.state */
enum {
  SCREENNORMAL = 0,
  SCREENMAXIMIZED = 1, /* one editor taking over the screen */
  SCREENFULL = 2,      /* one editor taking over the screen with no bare-minimum UI elements */
};

/* #Screen.redraws_flag */
typedef enum eScreen_Redraws_Flag {
  TIME_RGN = (1 << 0),
  TIME_ALL_3D_WIN = (1 << 1),
  TIME_ALL_ANIM_WIN = (1 << 2),
  TIME_ALL_BUTS_WIN = (1 << 3),
  // TIME_WITH_SEQ_AUDIO    = (1 << 4), /* DEPRECATED */
  TIME_SEQ = (1 << 5),
  TIME_ALL_IMAGE_WIN = (1 << 6),
  // TIME_CONTINUE_PHYSICS  = (1 << 7), /* UNUSED */
  TIME_NODES = (1 << 8),
  TIME_CLIPS = (1 << 9),

  TIME_FOLLOW = (1 << 15),
} eScreen_Redraws_Flag;

/* #Pnl.flag */
enum {
  PNL_SEL = (1 << 0),
  PNL_UNUSED_1 = (1 << 1), /* Cleared */
  PNL_CLOSED = (1 << 2),
  // PNL_TABBED = (1 << 3),  /* UNUSED */
  // PNL_OVERLAP = (1 << 4), /* UNUSED */
  PNL_PIN = (1 << 5),
  PNL_POPOVER = (1 << 6),
  /* The panel has been drag-drop reordered and the instanced panel list needs to be rebuilt. */
  PNL_INSTANCED_LIST_ORDER_CHANGED = (1 << 7),
};

/* Fallback pnl category (only for old scripts which need updating) */
#define PNL_CATEGORY_FALLBACK "Misc"

/** #uiList.layout_type */
enum {
  UILST_LAYOUT_DEFAULT = 0,
  UILST_LAYOUT_COMPACT = 1,
  UILST_LAYOUT_GRID = 2,
  UILST_LAYOUT_BIG_PREVIEW_GRID = 3,
};

/* #uiList.flag */
enum {
  /* Scroll list to make active item visible. */
  UILST_SCROLL_TO_ACTIVE_ITEM = 1 << 0,
};

/* Value (in number of items) we have to go below minimum shown items to enable auto size. */
#define UI_LIST_AUTO_SIZE_THRESHOLD 1

/* uiList filter flags (dyn_data) */
/* WARNING! Those values are used by integer RNA too, which does not handle well values > INT_MAX.
 *          So please do not use 32nd bit here. */
enum {
  UILST_FLT_ITEM = 1 << 30, /* This item has passed the filter process successfully. */
};

/* #uiList.filter_flag */
enum {
  UILST_FLT_SHOW = 1 << 0,            /* Show filtering UI. */
  UILST_FLT_EXCLUDE = UILST_FLT_ITEM, /* Exclude filtered items, *must* use this same value. */
};

/* #uiList.filter_sort_flag */
enum {
  /* Plain values (only one is valid at a time, once masked with UILST_FLT_SORT_MASK. */
  /** Just for sake of consistency. */
  /* UILST_FLT_SORT_INDEX = 0, */ /* UNUSED */
  UILST_FLT_SORT_ALPHA = 1,

  /* Bitflags affecting behavior of any kind of sorting. */
  /* Special flag to indicate that order is locked (not user-changeable). */
  UILST_FLT_SORT_LOCK = 1u << 30,
  /* Special value, bitflag used to reverse order! */
  UILST_FLT_SORT_REVERSE = 1u << 31,
};

#define UILST_FLT_SORT_MASK (((unsigned int)(UILST_FLT_SORT_REVERSE | UILST_FLT_SORT_LOCK)) - 1)

/**
 * regiontype, first two are the default set.
 * \warning Do NOT change order, append on end. Types are hard-coded needed.
 */
typedef enum eRgn_Type {
  RGN_TYPE_WINDOW = 0,
  RGN_TYPE_HEADER = 1,
  RGN_TYPE_CHANNELS = 2,
  RGN_TYPE_TEMPORARY = 3,
  RGN_TYPE_UI = 4,
  RGN_TYPE_TOOLS = 5,
  RGN_TYPE_TOOL_PROPS = 6,
  RGN_TYPE_PREVIEW = 7,
  RGN_TYPE_HUD = 8,
  /* Rgn to navigate the main rgn from (RGN_TYPE_WINDOW). */
  RGN_TYPE_NAV_BAR = 9,
  /* A place for btns to trigger execution of something that was set up in other rgns. */
  RGN_TYPE_EXECUTE = 10,
  RGN_TYPE_FOOTER = 11,
  RGN_TYPE_TOOL_HEADER = 12,
  /* Rgn type used exclusively by internal code and add-ons to register draw cbs to the XR
   * cx (surface, mirror view). Does not represent any real rgn. */
  RGN_TYPE_XR = 13,

#define RGN_TYPE_LEN (RGN_TYPE_XR + 1)
} eRgn_Type;

/* use for fnn args */
#define RGN_TYPE_ANY -1

/* Region supports panel tabs (categories). */
#define RGN_TYPE_HAS_CATEGORY_MASK (1 << RGN_TYPE_UI)

/* Check for any kind of header rgn. */
#define RGN_TYPE_IS_HEADER_ANY(rgntype) \
  (((1 << (rgntype)) & \
    ((1 << RGN_TYPE_HEADER) | 1 << (RGN_TYPE_TOOL_HEADER) | (1 << RGN_TYPE_FOOTER))) != 0)

/* #ARgn.alignment */
enum {
  RGN_ALIGN_NONE = 0,
  RGN_ALIGN_TOP = 1,
  RGN_ALIGN_BOTTOM = 2,
  RGN_ALIGN_LEFT = 3,
  RGN_ALIGN_RIGHT = 4,
  RGN_ALIGN_HSPLIT = 5,
  RGN_ALIGN_VSPLIT = 6,
  RGN_ALIGN_FLOAT = 7,
  RGN_ALIGN_QSPLIT = 8,
  /* Max 15. */

  /* Flags start here. */
  RGN_SPLIT_PREV = 32,
};

/* Mask out flags so we can check the alignment. */
#define RGN_ALIGN_ENUM_FROM_MASK(align) ((align) & ((1 << 4) - 1))
#define RGN_ALIGN_FLAG_FROM_MASK(align) ((align) & ~((1 << 4) - 1))

/* #ARgn.flag */
enum {
  RGN_FLAG_HIDDEN = (1 << 0),
  RGN_FLAG_TOO_SMALL = (1 << 1),
  /* Force delayed reinit of region size data, so that region size is calculated
   * just big enough to show all its content (if enough space is available).
   * Note that only ED_region_header supports this right now. */
  RGN_FLAG_DYNAMIC_SIZE = (1 << 2),
  /* Rgn data is NULL'd on read, never written. */
  RGN_FLAG_TEMP_REGIONDATA = (1 << 3),
  /* The region must either use its prefsizex/y or be hidden. */
  RGN_FLAG_PREFSIZE_OR_HIDDEN = (1 << 4),
  /* Size has been clamped (floating regions only). */
  RGN_FLAG_SIZE_CLAMP_X = (1 << 5),
  RGN_FLAG_SIZE_CLAMP_Y = (1 << 6),
  /* When the user sets the region is hidden,
   * needed for floating regions that may be hidden for other reasons. */
  RGN_FLAG_HIDDEN_BY_USER = (1 << 7),
  /* Property search filter is active. */
  RGN_FLAG_SEARCH_FILTER_ACTIVE = (1 << 8),
  /* Update the expansion of the rgn's pnls and switch cx's. Only Set
   * temp when the search filter is updated and cleared at the end of the
   * rgn's layout pass. so that expansion is still interactive, */
  RGN_FLAG_SEARCH_FILTER_UPDATE = (1 << 9),
};

/* #ARgn.do_draw */
enum {
  /* Rgn must be fully redrawn. */
  RGN_DRAW = 1,
  /* Redraw only part of rgn, for sculpting and painting to get smoother
   * stroke painting on heavy meshes. */
  RGN_DRAW_PARTIAL = 2,
  /* For outliner, to do faster redraw w/out rebuilding outliner tree.
   * For 3D viewport, to display a new progressive render sample w/out
   * while other buffers and overlays remain unchanged. */
  RGN_DRAW_NO_REBUILD = 4,

  /* Set while rgn is being drawn. */
  RGN_DRAWING = 8,
  /* For popups, to refresh UI layout along w drawing. */
  RGN_REFRESH_UI = 16,

  /* Only editor overlays (currently gizmos only!) should be redrawn. */
  RGN_DRAW_EDITOR_OVERLAYS = 32,
};
