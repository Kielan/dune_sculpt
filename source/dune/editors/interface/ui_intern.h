#pragma once

#include "lib_compiler_attrs.h"
#include "lib_rect.h"

#include "types_list.h"
#include "api_types.h"
#include "ui.h"
#include "ui_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ARegion;
struct AnimEvalCxt;
struct CurveMapping;
struct CurveProfile;
struct Id;
struct ImBuf;
struct Scene;
struct Cxt;
struct CxtStore;
struct uiHandleBtnData;
struct uiLayout;
struct uiStyle;
struct uiUndoStackTxt;
struct uiWidgetColors;
struct WinEvent;
struct WinKeyConfig;
struct WinOpType;
struct WinTimer;

/* general defines */

#define RNA_NO_INDEX -1
#define RNA_ENUM_VALUE -2

#define UI_MENU_PADDING (int)(0.2f * UI_UNIT_Y)

#define UI_MENU_WIDTH_MIN (UI_UNIT_Y * 9)
/** Some extra padding added to menus containing sub-menu icons. */
#define UI_MENU_SUBMENU_PADDING (6 * UI_DPI_FAC)

/* menu scrolling */
#define UI_MENU_SCROLL_ARROW (12 * UI_DPI_FAC)
#define UI_MENU_SCROLL_MOUSE (UI_MENU_SCROLL_ARROW + 2 * UI_DPI_FAC)
#define UI_MENU_SCROLL_PAD (4 * UI_DPI_FAC)

/* panel limits */
#define UI_PANEL_MINX 100
#define UI_PANEL_MINY 70

/* Popover width (multiplied by #U.widget_unit) */
#define UI_POPOVER_WIDTH_UNITS 10

/* uiBtn.flag */
enum {
  /* Use when the btn is pressed. */
  UI_SELECT = (1 << 0),
  /* Temp hidden (scrolled out of the view). */
  UI_SCROLLED = (1 << 1),
  UI_ACTIVE = (1 << 2),
  UI_HAS_ICON = (1 << 3),
  UI_HIDDEN = (1 << 4),
  /* Display selected, doesn't impact interaction. */
  UI_SELECT_DRAW = (1 << 5),
  /* Prop search filter is active and the bn does not match. */
  UI_SEARCH_FILTER_NO_MATCH = (1 << 6),
  /* WARNING: rest of #uiBut.flag in UI_interface.h */
};

/* uiBtn.dragflag */
enum {
  UI_BTN_DRAGPOIN_FREE = (1 << 0),
};

/* uiBtn.pie_dir */
typedef enum RadialDirection {
  UI_RADIAL_NONE = -1,
  UI_RADIAL_N = 0,
  UI_RADIAL_NE = 1,
  UI_RADIAL_E = 2,
  UI_RADIAL_SE = 3,
  UI_RADIAL_S = 4,
  UI_RADIAL_SW = 5,
  UI_RADIAL_W = 6,
  UI_RADIAL_NW = 7,
} RadialDirection;

extern const char ui_radial_dir_order[8];
extern const char ui_radial_dir_to_numpad[8];
extern const short ui_radial_dir_to_angle[8];

/* internal panel drawing defines */
#define PNL_HEADER (UI_UNIT_Y * 1.25) /* 24 default */

/* bit btn defines */
/* Bit opns */
#define UI_BITBTN_TEST(a, b) (((a) & (1 << (b))) != 0)
#define UI_BITBTN_VALUE_TOGGLED(a, b) ((a) ^ (1 << (b)))
#define UI_BITBTN_VALUE_ENABLED(a, b) ((a) | (1 << (b)))
#define UI_BITBTN_VALUE_DISABLED(a, b) ((a) & ~(1 << (b)))

/* bit-row */
#define UI_BITBTN_ROW(min, max) \
  (((max) >= 31 ? 0xFFFFFFFF : (1 << ((max) + 1)) - 1) - ((min) ? ((1 << (min)) - 1) : 0))

/* Split number-btns by ':' and align left/right. */
#define USE_NUMBTNS_LR_ALIGN

/* Use new 'align' computation code. */
#define USE_UIBTN_SPATIAL_ALIGN

/* PieMenuData.flags */
enum {
  /* Pie menu item collision is detected at 90 degrees. */
  UI_PIE_DEGREES_RANGE_LARGE = (1 << 0),
  /* Use initial center of pie menu to calculate direction. */
  UI_PIE_INITIAL_DIRECTION = (1 << 1),
  /* Pie menu is drag style. */
  UI_PIE_DRAG_STYLE = (1 << 2),
  /* Mouse not far enough from center position. */
  UI_PIE_INVALID_DIR = (1 << 3),
  /* Pie menu changed to click style, click to confirm. */
  UI_PIE_CLICK_STYLE = (1 << 4),
  /** Pie animation finished, do not calculate any more motion. */
  UI_PIE_ANIM_FINISHED = (1 << 5),
  /** Pie gesture selection has been done, now wait for mouse motion to end. */
  UI_PIE_GESTURE_END_WAIT = (1 << 6),
};

#define PIE_CLICK_THRESHOLD_SQ 50.0f

/* The max num of items a radial menu (pie menu) can contain. */
#define PIE_MAX_ITEMS 8

struct uiBtn {
  struct uiBtn *next, *prev;

  /* Ptr back to the layout item holding this btn. */
  uiLayout *layout;
  int flag, drawflag;
  eBtnType type;
  eBtnPtrType pointype;
  short bit, bitnr, retval, strwidth, alignnr;
  short ofs, pos, selsta, selend;

  char *str;
  char strdata[UI_MAX_NAME_STR];
  char drawstr[UI_MAX_DRAW_STR];

  rctf rect; /* block relative coords */

  char *poin;
  float hardmin, hardmax, softmin, softmax;

  /* both these values use depends on the button type
   * (polymorphic struct or union would be nicer for this stuff) */

  /* For uiBtn.type:
   * - UI_BTYPE_LABEL:        Use `(a1 == 1.0f)` to use a2 as a blending factor (imaginative!).
   * - UI_BTYPE_SCROLL:       Use as scroll size.
   * - UI_BTYPE_SEARCH_MENU:  Use as number or rows. */
  float a1;

  /* For uiBtn.type:
   * - UI_BTYPE_HSVCIRCLE:    Use to store the luminosity.
   * - UI_BTYPE_LABEL:        If `(a1 == 1.0f)` use a2 as a blending factor.
   * - UI_BTYPE_SEARCH_MENU:  Use as number or columns. */
  float a2;

  uchar col[4];

  uiBtnHandleFn fn;
  void *fn_arg1;
  void *fn_arg2;

  uiBtnHandleNFn fn;
  void *fn_arg;

  struct CxtStore *cxt;

  uiBtnCompleteFn autocomplete_fn;
  void *autofn_arg;

  uiBtnHandleRenameFn rename_fn;
  void *rename_arg1;
  void *rename_orig;

  /* Run an action when holding the btn down. */
  uiBtnHandleHoldFn hold_fn;
  void *hold_argN;

  const char *tip;
  uiBtnToolTipFn tip_fn;
  void *tip_arg;
  uiFreeArgFn tip_arg_free;

  /* info on why btn is disabled, displayed in tooltip */
  const char *disabled_info;

  BIFIconId icon;
  /* Copied from the uiBlock.emboss */
  eUIEmbossType emboss;
  /* direction in a pie menu, used for collision detection (RadialDirection) */
  signed char pie_dir;
  /* could be made into a single flag */
  bool changed;
  /* so btns can support unit systems which are not RNA */
  uchar unit_type;
  short mod_key;
  short iconadd;

  /* UI_BTYPE_BLOCK data */
  uiBlockCreateFn block_create_fn;

  /* UI_BTYPE_PULLDOWN / UI_BTYPE_MENU data */
  uiMenuCreateFn menu_create_fn;

  uiMenuStepFn menu_step_fn;

  /* api data */
  struct ApiPtr apiptr;
  struct ApiProp *apiprop;
  int apiindex;

  /* Op data */
  struct WinOpType *optype;
  struct ApiPtr *opptr;
  WinOpCallCxt opcxt;

  /* When non-zero, this is the key used to activate a menu items (`a-z` always lower case). */
  uchar menu_key;

  List extra_op_icons; /* uiBtnExtraOpIcon */

  /* Drag-able data, type is WIN_DRAG_... */
  char dragtype;
  short dragflag;
  void *dragptr;
  struct ImBuf *imbuf;
  float imb_scale;

  /* Active btn data (set when the user is hovering or interacting with a btn). */
  struct uiHandleBtnData *active;

  /* Custom butn data (borrowed, not owned). */
  void *custom_data;

  char *editstr;
  double *editval;
  float *editvec;

  uiBtnPushedStateFn pushed_state_fn;
  const void *pushed_state_arg;

  /* ptr back */
  uiBlock *block;
};

/* Derived struct for UI_BTYPE_NUM */
typedef struct uiBtnNumber {
  uiBtn btn;

  float step_size;
  float precision;
} uiBtnNumber;

/* Derived struct for UI_BTYPE_COLOR */
typedef struct uiBtnColor {
  uiBtn btn;

  bool is_pallete_color;
  int palette_color_index;
} uiBtnColor;

/* Derived struct for UI_BTYPE_TAB */
typedef struct uiBtnTab {
  uiBtn btn;
  struct MenuType *menu;
} uiBtnTab;

/* Derived struct for UI_BTYPE_SEARCH_MENU */
typedef struct BtnSearch {
  Btn btn;

  BtnSearchCreateFn popup_create_fn;
  BtnSearchUpdateFn items_update_fn;
  void *item_active;

  void *arg;
  uiFreeArgFn arg_free_fn;

  BtnSearchCxtMenuFn item_cxt_menu_fn;
  BtnSearchTooltipFn item_tooltip_fn;

  const char *item_sep_string;

  struct ApiPtr apisearchptr;
  struct ApiProp *apisearchprop;

  /* The search box only provides suggestions, it does not force
   * the string to match one of the search items when applying. */
  bool results_are_suggestions;
} BtnSearch;

/* Derived struct for BTYPE_DECORATOR */
typedef struct BtnDecorator {
  uiBtn btn;

  struct ApiPtr apiptr;
  struct ApiProp *apiprop;
  int apiindex;
} BtnDecorator;

/* Derived struct for BTYPE_PROGRESS_BAR. */
typedef struct BtnProgressbar {
  Btn btn;

  /* 0..1 range */
  float progress;
} BtnProgressbar;

/* Derived struct for BTYPE_TREEROW. */
typedef struct BtnTreeRow {
  Btn btn;

  uiTreeViewItemHandle *tree_item;
  int indentation;
} BtnTreeRow;

/* Derived struct for BTYPE_HSVCUBE. */
typedef struct BtnHSVCube {
  Btn btn;

  eBtnGradientType gradient_type;
} BtnHSVCube;

/* Derived struct for BTYPE_COLORBAND. */
typedef struct BtnColorBand {
  uiBtn btn;

  struct ColorBand *edit_coba;
} BtnColorBand;

/* Derived struct for BTYPE_CURVEPROFILE. */
typedef struct BtnCurveProfile {
  Btn btn;

  struct CurveProfile *edit_profile;
} BtnCurveProfile;

/* Derived struct for BTYPE_CURVE. */
typedef struct BtnCurveMapping {
  Btn btn;

  struct CurveMapping *edit_cumap;
  eBtnGradientType gradient_type;
} BtnCurveMapping;

/* Additional, superimposed icon for a btn, invoking an op */
typedef struct BtnExtraOpIcon {
  struct BtnExtraOpIcon *next, *prev;

  BIFIconId icon;
  struct WinOpCallParams *optype_params;

  bool highlighted;
  bool disabled;
} BtnExtraOpIcon;

typedef struct ColorPicker {
  struct ColorPicker *next, *prev;

  /* Color in HSV or HSL, in color picking color space. Used for HSV cube,
   * circle and slider widgets. The color picking space is perceptually
   * linear for intuitive editing. */
  float hsv_perceptual[3];
  /** Initial color data (to detect changes). */
  float hsv_perceptual_init[3];
  bool is_init;

  /* HSV or HSL color in scene linear color space value used for number
   * buttons. This is scene linear so that there is a clear correspondence
   * to the scene linear RGB values. */
  float hsv_scene_linear[3];

  /* Cubic saturation for the color wheel. */
  bool use_color_cubic;
  bool use_color_lock;
  bool use_luminosity_lock;
  float luminosity_lock_value;
} ColorPicker;

typedef struct ColorPickerData {
  List list;
} ColorPickerData;

struct PieMenuData {
  /* store title and icon to allow access when pie levels are created */
  const char *title;
  int icon;

  float pie_dir[2];
  float pie_center_init[2];
  float pie_center_spawned[2];
  float last_pos[2];
  double duration_gesture;
  int flags;
  /** Initial event used to fire the pie menu, store here so we can query for release */
  short event_type;
  float alphafac;
};

/* uiBlock.content_hints */
enum eBlockContentHints {
  /* In a menu block, if there is a single sub-menu btn, we add some
   * padding to the right to put nicely aligned triangle icons there. */
  UI_BLOCK_CONTAINS_SUBMENU_BTN = (1 << 0),
};

/* A group of btn refs, used by prop search to keep track of sets of btns that
 * should be searched together. For example, in prop split layouts number btns and their
 * labels (and even their decorators) are separate btns, but they must be searched and
 * highlighted together. */
typedef struct BtnGroup {
  void *next, *prev;
  List btns; /* LinkData with Btn data field. */
  short flag;
} BtnGroup;

/* BtnGroup.flag. */
typedef enum BtnGroupFlag {
  /* While this flag is set, don't create new btn groups for layout item calls. */
  BTN_GROUP_LOCK = (1 << 0),
  /* The btns in this group are inside a panel header. */
  UI_BTN_GROUP_PANEL_HEADER = (1 << 1),
} BtnGroupFlag;

struct uiBlock {
  uiBlock *next, *prev;

  List btns;
  struct Panel *panel;
  uiBlock *oldblock;

  /* Used for `ui_btnstore_*` runtime fn */
  List btnstore;

  List btn_groups; /* BtnGroup. */

  List layouts;
  struct uiLayout *curlayout;

  List cxts;

  /* A block can store "views" on data-sets. Currently tree-views (AbstractTreeView) only.
   * Others are imaginable, e.g. table-views, grid-views, etc. These are stored here to support
   * state that is persistent over redraws (e.g. collapsed tree-view items). */
  List views;

  char name[UI_MAX_NAME_STR];

  float winmat[4][4];

  rctf rect;
  float aspect;

  /* Unique hash used to implement popup menu memory. */
  uint puphash;

  uiBtnHandleFn fn;
  void *fn_arg1;
  void *fn_arg2;

  BtnHandleNFn fnN;
  void *fn_argN;

  MenuHandleFn btn_menu_fn;
  void *btn_menu_fn_arg;

  uiBlockHandleFn handle_fn;
  void *handle_fn_arg;

  /* Custom interaction data. */
  uiBlockInteraction_CbData custom_interaction_cbs;

  /* Custom extra event handling. */
  int (*block_event_fn)(const struct Cxt *C, struct uiBlock *, const struct WinEvent *);

  /* Custom extra draw function for custom blocks. */
  void (*drawextra)(const struct Cxt *C, void *idv, void *arg1, void *arg2, rcti *rect);
  void *drawextra_arg1;
  void *drawextra_arg2;

  int flag;
  short alignnr;
  /* Hints about the buttons of this block. Used to avoid itering over
   * btns to find out if some criteria is met by any. Instead, check this
   * criteria when adding the btn and set a flag here if it's met. */
  short content_hints; /* eBlockContentHints */

  char direction;
  /* BLOCK_THEME_STYLE_* */
  char theme_style;
  /* Copied to Btn.emboss */
  eUIEmbossType emboss;
  bool auto_open;
  char _pad[5];
  double auto_open_last;

  const char *lockstr;

  bool lock;
  /* To keep blocks while drawing and free them afterwards. */
  bool active;
  /* To avoid tool-tip after click. */
  bool tooltipdisabled;
  /* True when ui_block_end has been called. */
  bool endblock;

  /* for doing delayed */
  eBlockBoundsCalc bounds_type;
  /* Offset to use when calculating bounds (in pixels). */
  int bounds_offset[2];
  /* for doing delayed */
  int bounds, minbounds;

  /* Pull-downs, to detect outside, can differ per case how it is created. */
  rctf safety;
  /* uiSafetyRct list */
  List saferct;

  uiPopupBlockHandle *handle;

  /* use so presets can find the op,
   * across menus and from nested popups which fail for op cxt. */
  struct WinOp *ui_op;

  /* hack for dynamic op enums */
  void *evil_C;

  /* unit system, used a lot for numeric btns so include here
   * rather than fetching through the scene every time. */
  struct UnitSettings *unit;
  /* only accessed by color picker templates. */
  ColorPickerData color_pickers;

  /* Block for color picker with gamma baked in. */
  bool is_color_gamma_picker;

  /* Display device name used to display this block,
   * used by color widgets to transform colors from/to scene linear */
  char display_device[64];

  struct PieMenuData pie_data;
};

typedef struct uiSafetyRct {
  struct uiSafetyRct *next, *prev;
  rctf parent;
  rctf safety;
} uiSafetyRct;

/* interface.c */
void ui_fontscale(float *points, float aspect);

extern void ui_block_to_region_fl(const struct ARegion *region,
                                  uiBlock *block,
                                  float *r_x,
                                  float *r_y);
extern void ui_block_to_window_fl(const struct ARegion *region,
                                  uiBlock *block,
                                  float *x,
                                  float *y);
extern void ui_block_to_window(const struct ARegion *region, uiBlock *block, int *x, int *y);
extern void ui_block_to_region_rctf(const struct ARegion *region,
                                    uiBlock *block,
                                    rctf *rct_dst,
                                    const rctf *rct_src);
extern void ui_block_to_window_rctf(const struct ARegion *region,
                                    uiBlock *block,
                                    rctf *rct_dst,
                                    const rctf *rct_src);
extern float ui_block_to_window_scale(const struct ARegion *region, uiBlock *block);
/* For mouse cursor. */
extern void ui_win_to_block_fl(const struct ARegion *region,
                               uiBlock *block,
                               float *x,
                               float *y);
extern void ui_win_to_block(const struct ARegion *region, uiBlock *block, int *x, int *y);
extern void ui_win_to_block_rctf(const struct ARegion *region,
                                 uiBlock *block,
                                 rctf *rct_dst,
                                 const rctf *rct_src);
extern void ui_win_to_region(const struct ARegion *region, int *x, int *y);
extern void ui_win_to_region_rcti(const struct ARegion *region,
                                  rcti *rect_dst,
                                  const rcti *rct_src);
extern void ui_win_to_region_rctf(const struct ARegion *region,
                                     rctf *rect_dst,
                                     const rctf *rct_src);
extern void ui_region_to_win(const struct ARegion *region, int *x, int *y);
/* Popups will add a margin to ARegion.winrct for shadow,
 * for interactivity (point-inside tests for eg), we want the winrct without the margin added. */
extern void ui_region_winrct_get_no_margin(const struct ARegion *region, struct rcti *r_rect);

/* Reallocate the btn (new address is returned) for a new btn type.
 * This should generally be avoided and instead the correct type be created right away.
 *
 * Only the Btn data can be kept. If the old btn used a derived type (e.g. BtnTab),
 * the data that is not inside Btn will be lost. */
uiBut *btn_change_type(Btn *btn, eBtnType new_type);

extern double btn_value_get(Btn *btn);
extern void btn_value_set(Btn *btn, double value);
/* For picker, while editing HSV */
extern void btn_hsv_set(uiBtn *btn);
/* For btns pointing to color for example. */
extern void btn_v3_get(uiBtn *btn, float vec[3]);
/* For btns pointing to color for example */
extern void btn_v3_set(Btn *btn, const float vec[3]);

extern void ui_hsvcircle_vals_from_pos(
    const rcti *rect, float mx, float my, float *r_val_rad, float *r_val_dist);
/* Cursor in HSV circle, in float units -1 to 1, to map on radius. */
extern void ui_hsvcircle_pos_from_vals(
    const ColorPicker *cpicker, const rcti *rect, const float *hsv, float *xpos, float *ypos);
extern void ui_hsvcube_pos_from_vals(
    const struct BtnHSVCube *hsv_btn, const rcti *rect, const float *hsv, float *xp, float *yp);

/* param float_precision: For number btns the precision
 * to use or -1 to fallback to the btn default.
 * param use_exp_float: Use exponent representation of floats
 * when out of reasonable range (outside of 1e3/1e-3) */
extern void btn_string_get_ex(Btn *btn,
                                 char *str,
                                 size_t maxlen,
                                 int float_precision,
                                 bool use_exp_float,
                                 bool *r_use_exp_float) ATTR_NONNULL(1, 2);
extern void btn_string_get(Btn *btn, char *str, size_t maxlen) ATTR_NONNULL();
/* A version of btn_string_get_ex for dynamic buffer sizes
 * (where btn_string_get_max_length returns 0).
 * param r_str_size: size of the returned string (including terminator). */
extern char *ui_btn_string_get_dynamic(Btn *btn, int *r_str_size);
/* param str: will be overwritten. */
extern void btn_convert_to_unit_alt_name(Btn *btn, char *str, size_t maxlen) ATTR_NONNULL();
extern bool btn_string_set(struct Cxt *C, Btn *btn, const char *str) ATTR_NONNULL();
extern bool btn_string_eval_number(struct Cxt *C,
                                   const Btn *btn,
                                   const char *str,
                                   double *value) ATTR_NONNULL();
extern int btn_string_get_max_length(Btn *btn);
/* Clear & exit the active btn's string */
extern void btn_active_string_clear_and_exit(struct Cxt *C, Btn *btn) ATTR_NONNULL();
/* Use handling code to set a string for the btn. Handles the case where the string is set for a
 * search btn while the search menu is open, so the results are updated accordingly.
 * This is basically the same as pasting the string into the btn. */
extern void btn_set_string_interactive(struct Cxt *C, Btn *btn, const char *value);
extern Btn *btn_drag_multi_edit_get(Btn *btn);

void btn_def_icon(Btn *btn, int icon, int flag);
/* Avoid using this where possible since it's better not to ask for an icon in the first place. */
void btn_def_icon_clear(Btn *btn);

void btn_extra_op_icons_free(Btn *btn);

extern void btn_api_menu_convert_to_panel_type(struct Btn *btn, const char *panel_type);
extern void btn_api_menu_convert_to_menu_type(struct Btn *btn, const char *menu_type);
extern bool btn_menu_draw_as_popover(const Btn *btn);

void btn_range_set_hard(Btn *btn);
void btn_range_set_soft(Btn *btn);

bool btn_cxt_poll_op(struct Cxt *C, struct WinOpType *ot, const Btn *btn);
/* Check if the op ot poll is successful with the context given by btn (optionally).
 * param btn: The btn that might store cxt. Can be NULL for convenience (e.g. if there is
 * no btn to take cxt from, but we still want to poll the op). */
bool btn_cxt_poll_op_ex(struct Cxt *C,
                        const Btn *btn,
                        const struct WinOpCallParams *optype_params);

extern void btn_update(Btn *btn);
extern void btn_update_edited(Btn *btn);
extern PropScaleType btn_scale_type(const Btn *btn) ATTR_WARN_UNUSED_RESULT;
extern bool btn_is_float(const Btn *btn) ATTR_WARN_UNUSED_RESULT;
extern bool btn_is_bool(const Btn *btn) ATTR_WARN_UNUSED_RESULT;
extern bool btn_is_unit(const Btn *btn) ATTR_WARN_UNUSED_RESULT;
/**
 * Check if this button is similar enough to be grouped with another.
 */
extern bool ui_but_is_compatible(const uiBut *but_a, const uiBut *but_b) ATTR_WARN_UNUSED_RESULT;
extern bool ui_but_is_rna_valid(uiBut *but) ATTR_WARN_UNUSED_RESULT;
/**
 * Checks if the button supports cycling next/previous menu items (ctrl+mouse-wheel).
 */
extern bool ui_but_supports_cycling(const uiBut *but) ATTR_WARN_UNUSED_RESULT;

/**
 * Check if the button is pushed, this is only meaningful for some button types.
 *
 * \return (0 == UNSELECT), (1 == SELECT), (-1 == DO-NOTHING)
 */
extern int ui_but_is_pushed_ex(uiBut *but, double *value) ATTR_WARN_UNUSED_RESULT;
extern int ui_but_is_pushed(uiBut *but) ATTR_WARN_UNUSED_RESULT;

void ui_but_override_flag(struct Main *bmain, uiBut *but);

extern void ui_block_bounds_calc(uiBlock *block);

extern struct ColorManagedDisplay *ui_block_cm_display_get(uiBlock *block);
void ui_block_cm_to_display_space_v3(uiBlock *block, float pixel[3]);

/* interface_regions.c */

struct uiKeyNavLock {
  /** Set when we're using keyboard-input. */
  bool is_keynav;
  /** Only used to check if we've moved the cursor. */
  int event_xy[2];
};

typedef uiBlock *(*uiBlockHandleCreateFunc)(struct bContext *C,
                                            struct uiPopupBlockHandle *handle,
                                            void *arg1);

struct uiPopupBlockCreate {
  uiBlockCreateFunc create_func;
  uiBlockHandleCreateFunc handle_create_func;
  void *arg;
  uiFreeArgFunc arg_free;

  int event_xy[2];

  /** Set when popup is initialized from a button. */
  struct ARegion *butregion;
  uiBut *but;
};

struct uiPopupBlockHandle {
  /* internal */
  struct ARegion *region;

  /** Use only for #UI_BLOCK_MOVEMOUSE_QUIT popups. */
  float towards_xy[2];
  double towardstime;
  bool dotowards;

  bool popup;
  void (*popup_func)(struct bContext *C, void *arg, int event);
  void (*cancel_func)(struct bContext *C, void *arg);
  void *popup_arg;

  /** Store data for refreshing popups. */
  struct uiPopupBlockCreate popup_create_vars;
  /** True if we can re-create the popup using #uiPopupBlockHandle.popup_create_vars. */
  bool can_refresh;
  bool refresh;

  struct wmTimer *scrolltimer;
  float scrolloffset;

  struct uiKeyNavLock keynav_state;

  /* for operator popups */
  struct wmOperator *popup_op;
  struct ScrArea *ctx_area;
  struct ARegion *ctx_region;

  /* return values */
  int butretval;
  int menuretval;
  int retvalue;
  float retvec[4];

  /** Menu direction. */
  int direction;

  /* Previous values so we don't resize or reposition on refresh. */
  rctf prev_block_rect;
  rctf prev_butrct;
  short prev_dir1, prev_dir2;
  int prev_bounds_offset[2];

  /* Maximum estimated size to avoid having to reposition on refresh. */
  float max_size_x, max_size_y;

  /* #ifdef USE_DRAG_POPUP */
  bool is_grab;
  int grab_xy_prev[2];
  /* #endif */
};
