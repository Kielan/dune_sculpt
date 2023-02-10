#pragma once

#include "BLI_compiler_attrs.h"
#include "BLI_rect.h"

#include "DNA_listBase.h"
#include "RNA_types.h"
#include "UI_interface.h"
#include "UI_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ARegion;
struct AnimationEvalContext;
struct CurveMapping;
struct CurveProfile;
struct ID;
struct ImBuf;
struct Scene;
struct bContext;
struct bContextStore;
struct uiHandleButtonData;
struct uiLayout;
struct uiStyle;
struct uiUndoStack_Text;
struct uiWidgetColors;
struct wmEvent;
struct wmKeyConfig;
struct wmOperatorType;
struct wmTimer;

/* ****************** general defines ************** */

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

/** Popover width (multiplied by #U.widget_unit) */
#define UI_POPOVER_WIDTH_UNITS 10

/** #uiBut.flag */
enum {
  /** Use when the button is pressed. */
  UI_SELECT = (1 << 0),
  /** Temporarily hidden (scrolled out of the view). */
  UI_SCROLLED = (1 << 1),
  UI_ACTIVE = (1 << 2),
  UI_HAS_ICON = (1 << 3),
  UI_HIDDEN = (1 << 4),
  /** Display selected, doesn't impact interaction. */
  UI_SELECT_DRAW = (1 << 5),
  /** Property search filter is active and the button does not match. */
  UI_SEARCH_FILTER_NO_MATCH = (1 << 6),
  /* WARNING: rest of #uiBut.flag in UI_interface.h */
};

/** #uiBut.dragflag */
enum {
  UI_BUT_DRAGPOIN_FREE = (1 << 0),
};

/** #uiBut.pie_dir */
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

/* bit button defines */
/* Bit operations */
#define UI_BITBUT_TEST(a, b) (((a) & (1 << (b))) != 0)
#define UI_BITBUT_VALUE_TOGGLED(a, b) ((a) ^ (1 << (b)))
#define UI_BITBUT_VALUE_ENABLED(a, b) ((a) | (1 << (b)))
#define UI_BITBUT_VALUE_DISABLED(a, b) ((a) & ~(1 << (b)))

/* bit-row */
#define UI_BITBUT_ROW(min, max) \
  (((max) >= 31 ? 0xFFFFFFFF : (1 << ((max) + 1)) - 1) - ((min) ? ((1 << (min)) - 1) : 0))

/** Split number-buttons by ':' and align left/right. */
#define USE_NUMBUTS_LR_ALIGN

/** Use new 'align' computation code. */
#define USE_UIBUT_SPATIAL_ALIGN

/** #PieMenuData.flags */
enum {
  /** Pie menu item collision is detected at 90 degrees. */
  UI_PIE_DEGREES_RANGE_LARGE = (1 << 0),
  /** Use initial center of pie menu to calculate direction. */
  UI_PIE_INITIAL_DIRECTION = (1 << 1),
  /** Pie menu is drag style. */
  UI_PIE_DRAG_STYLE = (1 << 2),
  /** Mouse not far enough from center position. */
  UI_PIE_INVALID_DIR = (1 << 3),
  /** Pie menu changed to click style, click to confirm. */
  UI_PIE_CLICK_STYLE = (1 << 4),
  /** Pie animation finished, do not calculate any more motion. */
  UI_PIE_ANIMATION_FINISHED = (1 << 5),
  /** Pie gesture selection has been done, now wait for mouse motion to end. */
  UI_PIE_GESTURE_END_WAIT = (1 << 6),
};

#define PIE_CLICK_THRESHOLD_SQ 50.0f

/** The maximum number of items a radial menu (pie menu) can contain. */
#define PIE_MAX_ITEMS 8

struct uiBut {
  struct uiBut *next, *prev;

  /** Pointer back to the layout item holding this button. */
  uiLayout *layout;
  int flag, drawflag;
  eButType type;
  eButPointerType pointype;
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

  /**
   * For #uiBut.type:
   * - UI_BTYPE_LABEL:        Use `(a1 == 1.0f)` to use a2 as a blending factor (imaginative!).
   * - UI_BTYPE_SCROLL:       Use as scroll size.
   * - UI_BTYPE_SEARCH_MENU:  Use as number or rows.
   */
  float a1;

  /**
   * For #uiBut.type:
   * - UI_BTYPE_HSVCIRCLE:    Use to store the luminosity.
   * - UI_BTYPE_LABEL:        If `(a1 == 1.0f)` use a2 as a blending factor.
   * - UI_BTYPE_SEARCH_MENU:  Use as number or columns.
   */
  float a2;

  uchar col[4];

  uiButHandleFunc func;
  void *func_arg1;
  void *func_arg2;

  uiButHandleNFunc funcN;
  void *func_argN;

  struct bContextStore *context;

  uiButCompleteFunc autocomplete_func;
  void *autofunc_arg;

  uiButHandleRenameFunc rename_func;
  void *rename_arg1;
  void *rename_orig;

  /** Run an action when holding the button down. */
  uiButHandleHoldFunc hold_func;
  void *hold_argN;

  const char *tip;
  uiButToolTipFunc tip_func;
  void *tip_arg;
  uiFreeArgFunc tip_arg_free;

  /** info on why button is disabled, displayed in tooltip */
  const char *disabled_info;

  BIFIconID icon;
  /** Copied from the #uiBlock.emboss */
  eUIEmbossType emboss;
  /** direction in a pie menu, used for collision detection (RadialDirection) */
  signed char pie_dir;
  /** could be made into a single flag */
  bool changed;
  /** so buttons can support unit systems which are not RNA */
  uchar unit_type;
  short modifier_key;
  short iconadd;

  /** #UI_BTYPE_BLOCK data */
  uiBlockCreateFunc block_create_func;

  /** #UI_BTYPE_PULLDOWN / #UI_BTYPE_MENU data */
  uiMenuCreateFunc menu_create_func;

  uiMenuStepFunc menu_step_func;

  /* RNA data */
  struct PointerRNA rnapoin;
  struct PropertyRNA *rnaprop;
  int rnaindex;

  /* Operator data */
  struct wmOperatorType *optype;
  struct PointerRNA *opptr;
  wmOperatorCallContext opcontext;

  /** When non-zero, this is the key used to activate a menu items (`a-z` always lower case). */
  uchar menu_key;

  ListBase extra_op_icons; /** #uiButExtraOpIcon */

  /* Drag-able data, type is WM_DRAG_... */
  char dragtype;
  short dragflag;
  void *dragpoin;
  struct ImBuf *imb;
  float imb_scale;

  /** Active button data (set when the user is hovering or interacting with a button). */
  struct uiHandleButtonData *active;

  /** Custom button data (borrowed, not owned). */
  void *custom_data;

  char *editstr;
  double *editval;
  float *editvec;

  uiButPushedStateFunc pushed_state_func;
  const void *pushed_state_arg;

  /* pointer back */
  uiBlock *block;
};

/** Derived struct for #UI_BTYPE_NUM */
typedef struct uiButNumber {
  uiBut but;

  float step_size;
  float precision;
} uiButNumber;

/** Derived struct for #UI_BTYPE_COLOR */
typedef struct uiButColor {
  uiBut but;

  bool is_pallete_color;
  int palette_color_index;
} uiButColor;

/** Derived struct for #UI_BTYPE_TAB */
typedef struct uiButTab {
  uiBut but;
  struct MenuType *menu;
} uiButTab;

/** Derived struct for #UI_BTYPE_SEARCH_MENU */
typedef struct uiButSearch {
  uiBut but;

  uiButSearchCreateFn popup_create_fn;
  uiButSearchUpdateFn items_update_fn;
  void *item_active;

  void *arg;
  uiFreeArgFunc arg_free_fn;

  uiButSearchContextMenuFn item_context_menu_fn;
  uiButSearchTooltipFn item_tooltip_fn;

  const char *item_sep_string;

  struct PointerRNA rnasearchpoin;
  struct PropertyRNA *rnasearchprop;

  /**
   * The search box only provides suggestions, it does not force
   * the string to match one of the search items when applying.
   */
  bool results_are_suggestions;
} uiButSearch;

/** Derived struct for #UI_BTYPE_DECORATOR */
typedef struct uiButDecorator {
  uiBut but;

  struct PointerRNA rnapoin;
  struct PropertyRNA *rnaprop;
  int rnaindex;
} uiButDecorator;

/** Derived struct for #UI_BTYPE_PROGRESS_BAR. */
typedef struct uiButProgressbar {
  uiBut but;

  /* 0..1 range */
  float progress;
} uiButProgressbar;

/** Derived struct for #UI_BTYPE_TREEROW. */
typedef struct uiButTreeRow {
  uiBut but;

  uiTreeViewItemHandle *tree_item;
  int indentation;
} uiButTreeRow;

/** Derived struct for #UI_BTYPE_HSVCUBE. */
typedef struct uiButHSVCube {
  uiBut but;

  eButGradientType gradient_type;
} uiButHSVCube;

/** Derived struct for #UI_BTYPE_COLORBAND. */
typedef struct uiButColorBand {
  uiBut but;

  struct ColorBand *edit_coba;
} uiButColorBand;

/** Derived struct for #UI_BTYPE_CURVEPROFILE. */
typedef struct uiButCurveProfile {
  uiBut but;

  struct CurveProfile *edit_profile;
} uiButCurveProfile;

/** Derived struct for #UI_BTYPE_CURVE. */
typedef struct uiButCurveMapping {
  uiBut but;

  struct CurveMapping *edit_cumap;
  eButGradientType gradient_type;
} uiButCurveMapping;

/**
 * Additional, superimposed icon for a button, invoking an operator.
 */
typedef struct uiButExtraOpIcon {
  struct uiButExtraOpIcon *next, *prev;

  BIFIconID icon;
  struct wmOperatorCallParams *optype_params;

  bool highlighted;
  bool disabled;
} uiButExtraOpIcon;

typedef struct ColorPicker {
  struct ColorPicker *next, *prev;

  /** Color in HSV or HSL, in color picking color space. Used for HSV cube,
   * circle and slider widgets. The color picking space is perceptually
   * linear for intuitive editing. */
  float hsv_perceptual[3];
  /** Initial color data (to detect changes). */
  float hsv_perceptual_init[3];
  bool is_init;

  /** HSV or HSL color in scene linear color space value used for number
   * buttons. This is scene linear so that there is a clear correspondence
   * to the scene linear RGB values. */
  float hsv_scene_linear[3];

  /** Cubic saturation for the color wheel. */
  bool use_color_cubic;
  bool use_color_lock;
  bool use_luminosity_lock;
  float luminosity_lock_value;
} ColorPicker;

typedef struct ColorPickerData {
  ListBase list;
} ColorPickerData;

struct PieMenuData {
  /** store title and icon to allow access when pie levels are created */
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

/** #uiBlock.content_hints */
enum eBlockContentHints {
  /** In a menu block, if there is a single sub-menu button, we add some
   * padding to the right to put nicely aligned triangle icons there. */
  UI_BLOCK_CONTAINS_SUBMENU_BUT = (1 << 0),
};

/**
 * A group of button references, used by property search to keep track of sets of buttons that
 * should be searched together. For example, in property split layouts number buttons and their
 * labels (and even their decorators) are separate buttons, but they must be searched and
 * highlighted together.
 */
typedef struct uiButtonGroup {
  void *next, *prev;
  ListBase buttons; /* #LinkData with #uiBut data field. */
  short flag;
} uiButtonGroup;

/* #uiButtonGroup.flag. */
typedef enum uiButtonGroupFlag {
  /** While this flag is set, don't create new button groups for layout item calls. */
  UI_BUTTON_GROUP_LOCK = (1 << 0),
  /** The buttons in this group are inside a panel header. */
  UI_BUTTON_GROUP_PANEL_HEADER = (1 << 1),
} uiButtonGroupFlag;

struct uiBlock {
  uiBlock *next, *prev;

  ListBase buttons;
  struct Panel *panel;
  uiBlock *oldblock;

  /** Used for `UI_butstore_*` runtime function. */
  ListBase butstore;

  ListBase button_groups; /* #uiButtonGroup. */

  ListBase layouts;
  struct uiLayout *curlayout;

  ListBase contexts;

  /** A block can store "views" on data-sets. Currently tree-views (#AbstractTreeView) only.
   * Others are imaginable, e.g. table-views, grid-views, etc. These are stored here to support
   * state that is persistent over redraws (e.g. collapsed tree-view items). */
  ListBase views;

  char name[UI_MAX_NAME_STR];

  float winmat[4][4];

  rctf rect;
  float aspect;

  /** Unique hash used to implement popup menu memory. */
  uint puphash;

  uiButHandleFunc func;
  void *func_arg1;
  void *func_arg2;

  uiButHandleNFunc funcN;
  void *func_argN;

  uiMenuHandleFunc butm_func;
  void *butm_func_arg;

  uiBlockHandleFunc handle_func;
  void *handle_func_arg;

  /** Custom interaction data. */
  uiBlockInteraction_CallbackData custom_interaction_callbacks;

  /** Custom extra event handling. */
  int (*block_event_func)(const struct bContext *C, struct uiBlock *, const struct wmEvent *);

  /** Custom extra draw function for custom blocks. */
  void (*drawextra)(const struct bContext *C, void *idv, void *arg1, void *arg2, rcti *rect);
  void *drawextra_arg1;
  void *drawextra_arg2;

  int flag;
  short alignnr;
  /** Hints about the buttons of this block. Used to avoid iterating over
   * buttons to find out if some criteria is met by any. Instead, check this
   * criteria when adding the button and set a flag here if it's met. */
  short content_hints; /* #eBlockContentHints */

  char direction;
  /** UI_BLOCK_THEME_STYLE_* */
  char theme_style;
  /** Copied to #uiBut.emboss */
  eUIEmbossType emboss;
  bool auto_open;
  char _pad[5];
  double auto_open_last;

  const char *lockstr;

  bool lock;
  /** To keep blocks while drawing and free them afterwards. */
  bool active;
  /** To avoid tool-tip after click. */
  bool tooltipdisabled;
  /** True when #UI_block_end has been called. */
  bool endblock;

  /** for doing delayed */
  eBlockBoundsCalc bounds_type;
  /** Offset to use when calculating bounds (in pixels). */
  int bounds_offset[2];
  /** for doing delayed */
  int bounds, minbounds;

  /** Pull-downs, to detect outside, can differ per case how it is created. */
  rctf safety;
  /** #uiSafetyRct list */
  ListBase saferct;

  uiPopupBlockHandle *handle;

  /** use so presets can find the operator,
   * across menus and from nested popups which fail for operator context. */
  struct wmOperator *ui_operator;

  /** XXX hack for dynamic operator enums */
  void *evil_C;

  /** unit system, used a lot for numeric buttons so include here
   * rather than fetching through the scene every time. */
  struct UnitSettings *unit;
  /** \note only accessed by color picker templates. */
  ColorPickerData color_pickers;

  /** Block for color picker with gamma baked in. */
  bool is_color_gamma_picker;

  /**
   * Display device name used to display this block,
   * used by color widgets to transform colors from/to scene linear.
   */
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
/**
 * For mouse cursor.
 */
extern void ui_window_to_block_fl(const struct ARegion *region,
                                  uiBlock *block,
                                  float *x,
                                  float *y);
extern void ui_window_to_block(const struct ARegion *region, uiBlock *block, int *x, int *y);
extern void ui_window_to_block_rctf(const struct ARegion *region,
                                    uiBlock *block,
                                    rctf *rct_dst,
                                    const rctf *rct_src);
extern void ui_window_to_region(const struct ARegion *region, int *x, int *y);
extern void ui_window_to_region_rcti(const struct ARegion *region,
                                     rcti *rect_dst,
                                     const rcti *rct_src);
extern void ui_window_to_region_rctf(const struct ARegion *region,
                                     rctf *rect_dst,
                                     const rctf *rct_src);
extern void ui_region_to_window(const struct ARegion *region, int *x, int *y);
/**
 * Popups will add a margin to #ARegion.winrct for shadow,
 * for interactivity (point-inside tests for eg), we want the winrct without the margin added.
 */
extern void ui_region_winrct_get_no_margin(const struct ARegion *region, struct rcti *r_rect);

/**
 * Reallocate the button (new address is returned) for a new button type.
 * This should generally be avoided and instead the correct type be created right away.
 *
 * \note Only the #uiBut data can be kept. If the old button used a derived type (e.g. #uiButTab),
 *       the data that is not inside #uiBut will be lost.
 */
uiBut *ui_but_change_type(uiBut *but, eButType new_type);

extern double ui_but_value_get(uiBut *but);
extern void ui_but_value_set(uiBut *but, double value);
/**
 * For picker, while editing HSV.
 */
extern void ui_but_hsv_set(uiBut *but);
/**
 * For buttons pointing to color for example.
 */
extern void ui_but_v3_get(uiBut *but, float vec[3]);
/**
 * For buttons pointing to color for example.
 */
extern void ui_but_v3_set(uiBut *but, const float vec[3]);

extern void ui_hsvcircle_vals_from_pos(
    const rcti *rect, float mx, float my, float *r_val_rad, float *r_val_dist);
/**
 * Cursor in HSV circle, in float units -1 to 1, to map on radius.
 */
extern void ui_hsvcircle_pos_from_vals(
    const ColorPicker *cpicker, const rcti *rect, const float *hsv, float *xpos, float *ypos);
extern void ui_hsvcube_pos_from_vals(
    const struct uiButHSVCube *hsv_but, const rcti *rect, const float *hsv, float *xp, float *yp);

/**
 * \param float_precision: For number buttons the precision
 * to use or -1 to fallback to the button default.
 * \param use_exp_float: Use exponent representation of floats
 * when out of reasonable range (outside of 1e3/1e-3).
 */
extern void ui_but_string_get_ex(uiBut *but,
                                 char *str,
                                 size_t maxlen,
                                 int float_precision,
                                 bool use_exp_float,
                                 bool *r_use_exp_float) ATTR_NONNULL(1, 2);
extern void ui_but_string_get(uiBut *but, char *str, size_t maxlen) ATTR_NONNULL();
/**
 * A version of #ui_but_string_get_ex for dynamic buffer sizes
 * (where #ui_but_string_get_max_length returns 0).
 *
 * \param r_str_size: size of the returned string (including terminator).
 */
extern char *ui_but_string_get_dynamic(uiBut *but, int *r_str_size);
/**
 * \param str: will be overwritten.
 */
extern void ui_but_convert_to_unit_alt_name(uiBut *but, char *str, size_t maxlen) ATTR_NONNULL();
extern bool ui_but_string_set(struct bContext *C, uiBut *but, const char *str) ATTR_NONNULL();
extern bool ui_but_string_eval_number(struct bContext *C,
                                      const uiBut *but,
                                      const char *str,
                                      double *value) ATTR_NONNULL();
extern int ui_but_string_get_max_length(uiBut *but);
/**
 * Clear & exit the active button's string..
 */
extern void ui_but_active_string_clear_and_exit(struct bContext *C, uiBut *but) ATTR_NONNULL();
/**
 * Use handling code to set a string for the button. Handles the case where the string is set for a
 * search button while the search menu is open, so the results are updated accordingly.
 * This is basically the same as pasting the string into the button.
 */
extern void ui_but_set_string_interactive(struct bContext *C, uiBut *but, const char *value);
extern uiBut *ui_but_drag_multi_edit_get(uiBut *but);

void ui_def_but_icon(uiBut *but, int icon, int flag);
/**
 * Avoid using this where possible since it's better not to ask for an icon in the first place.
 */
void ui_def_but_icon_clear(uiBut *but);

void ui_but_extra_operator_icons_free(uiBut *but);

extern void ui_but_rna_menu_convert_to_panel_type(struct uiBut *but, const char *panel_type);
extern void ui_but_rna_menu_convert_to_menu_type(struct uiBut *but, const char *menu_type);
extern bool ui_but_menu_draw_as_popover(const uiBut *but);

void ui_but_range_set_hard(uiBut *but);
void ui_but_range_set_soft(uiBut *but);

bool ui_but_context_poll_operator(struct bContext *C, struct wmOperatorType *ot, const uiBut *but);
/**
 * Check if the operator \a ot poll is successful with the context given by \a but (optionally).
 * \param but: The button that might store context. Can be NULL for convenience (e.g. if there is
 *             no button to take context from, but we still want to poll the operator).
 */
bool ui_but_context_poll_operator_ex(struct bContext *C,
                                     const uiBut *but,
                                     const struct wmOperatorCallParams *optype_params);

extern void ui_but_update(uiBut *but);
extern void ui_but_update_edited(uiBut *but);
extern PropertyScaleType ui_but_scale_type(const uiBut *but) ATTR_WARN_UNUSED_RESULT;
extern bool ui_but_is_float(const uiBut *but) ATTR_WARN_UNUSED_RESULT;
extern bool ui_but_is_bool(const uiBut *but) ATTR_WARN_UNUSED_RESULT;
extern bool ui_but_is_unit(const uiBut *but) ATTR_WARN_UNUSED_RESULT;
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
