#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* BrushPenSettings->preset_type.
 * Use a range for each group and not continuous vals. */
typedef enum ePenBrushPresets {
  P_BRUSH_PRESET_UNKNOWN = 0,

  /* Draw 1-99. */
  P_BRUSH_PRESET_AIRBRUSH = 1,
  P_BRUSH_PRESET_INK_PEN = 2,
  P_BRUSH_PRESET_INK_PEN_ROUGH = 3,
  P_BRUSH_PRESET_MARKER_BOLD = 4,
  P_BRUSH_PRESET_MARKER_CHISEL = 5,
  P_BRUSH_PRESET_PEN = 6,
  P_BRUSH_PRESET_PEN_SOFT = 7,
  P_BRUSH_PRESET_PEN = 8,
  P_BRUSH_PRESET_FILL_AREA = 9,
  P_BRUSH_PRESET_ERASER_SOFT = 10,
  P_BRUSH_PRESET_ERASER_HARD = 11,
  P_BRUSH_PRESET_ERASER_POINT = 12,
  P_BRUSH_PRESET_ERASER_STROKE = 13,
  P_BRUSH_PRESET_TINT = 14,

  /* Vertex Paint 100-199. */
  P_BRUSH_PRESET_VERT_DRW = 100,
  P_BRUSH_PRESET_VERT_BLUR = 101,
  P_BRUSH_PRESET_VERT_AVG = 102,
  P_BRUSH_PRESET_VERT_SMEAR = 103,
  P_BRUSH_PRESET_VERT_REPLACE = 104,

  /* Sculpt 200-299. */
  P_BRUSH_PRESET_SMOOTH_STROKE = 200,
  P_BRUSH_PRESET_STRENGTH_STROKE = 201,
  P_BRUSH_PRESET_THICKNESS_STROKE = 202,
  P_BRUSH_PRESET_GRAB_STROKE = 203,
  P_BRUSH_PRESET_PUSH_STROKE = 204,
  P_BRUSH_PRESET_TWIST_STROKE = 205,
  P_BRUSH_PRESET_PINCH_STROKE = 206,
  P_BRUSH_PRESET_RANDOMIZE_STROKE = 207,
  P_BRUSH_PRESET_CLONE_STROKE = 208,

  /* Weight Paint 300-399. */
  P_BRUSH_PRESET_DRW_WEIGHT = 300,
} ePenBrushPresets;

/* BrushPenSettings->flag */
typedef enum ePenBrush_Flag {
  /* brush use pressure */
  P_BRUSH_USE_PRESSURE = (1 << 0),
  /* brush use pressure for alpha factor */
  P_BRUSH_USE_STRENGTH_PRESSURE = (1 << 1),
  /* brush use pressure for alpha factor */
  P_BRUSH_USE_JITTER_PRESSURE = (1 << 2),
  /* Disable automatic zoom for filling. */
  P_BRUSH_FILL_FIT_DISABLE = (1 << 3),
  /* Show extend fill help lines. */
  P_BRUSH_FILL_SHOW_EXTENDLINES = (1 << 4),
  /* fill hide transparent */
  P_BRUSH_FILL_HIDE = (1 << 6),
  /* show fill help lines */
  P_BRUSH_FILL_SHOW_HELPLINES = (1 << 7),
  /* lazy mouse */
  P_BRUSH_STABILIZE_MOUSE = (1 << 8),
  /* lazy mouse override (internal only) */
  P_BRUSH_STABILIZE_MOUSE_TEMP = (1 << 9),
  /* default eraser brush for quick switch */
  P_BRUSH_DEFAULT_ERASER = (1 << 10),
  /* settings group */
  P_BRUSH_GROUP_SETTINGS = (1 << 11),
  /* Random settings group */
  P_BRUSH_GROUP_RANDOM = (1 << 12),
  /* Keep material assigned to brush */
  P_BRUSH_MATERIAL_PINNED = (1 << 13),
  /* Do not show fill color while drawing (no lasso mode) */
  P_BRUSH_DISSABLE_LASSO = (1 << 14),
  /* Do not erase strokes oLcluded */
  P_BRUSH_OCCLUDE_ERASER = (1 << 15),
  /* Post process trim stroke */
  P_BRUSH_TRIM_STROKE = (1 << 16),
} ePenBrush_Flag;

typedef enum ePenBrush_Flag2 {
  /* Brush use random Hue at stroke level */
  P_BRUSH_USE_HUE_AT_STROKE = (1 << 0),
  /* Brush use random Saturation at stroke level */
  P_BRUSH_USE_SAT_AT_STROKE = (1 << 1),
  /* Brush use random Value at stroke level */
  P_BRUSH_USE_VAL_AT_STROKE = (1 << 2),
  /* Brush use random Pressure at stroke level */
  P_BRUSH_USE_PRESS_AT_STROKE = (1 << 3),
  /* Brush use random Strength at stroke level */
  P_BRUSH_USE_STRENGTH_AT_STROKE = (1 << 4),
  /* Brush use random UV at stroke level */
  P_BRUSH_USE_UV_AT_STROKE = (1 << 5),
  /* Brush use Hue random pressure */
  P_BRUSH_USE_HUE_RAND_PRESS = (1 << 6),
  /* Brush use Saturation random pressure */
  P_BRUSH_USE_SAT_RAND_PRESS = (1 << 7),
  /* Brush use Val random pressure */
  P_BRUSH_USE_VAL_RAND_PRESS = (1 << 8),
  /* Brush use Pressure random pressure */
  P_BRUSH_USE_PRESSURE_RAND_PRESS = (1 << 9),
  /* Brush use Strength random pressure */
  P_BRUSH_USE_STRENGTH_RAND_PRESS = (1 << 10),
  /* Brush use UV random pressure */
  P_BRUSH_USE_UV_RAND_PRESS = (1 << 11),
} ePenBrush_Flag2;

/* BrushPenSettings->p_fill_drw_mode */
typedef enum ePen_FillDrwModes {
  P_FILL_DMODE_BOTH = 0,
  P_FILL_DMODE_STROKE = 1,
  P_FILL_DMODE_CONTROL = 2,
} ePen_FillDrwModes;

/* BrushPenSettings->fill_layer_mode */
typedef enum ePen_FillLayerModes {
  P_FILL_GPLMODE_VISIBLE = 0,
  P_FILL_GPLMODE_ACTIVE = 1,
  P_FILL_GPLMODE_ALL_ABOVE = 2,
  P_FILL_GPLMODE_ALL_BELOW = 3,
  P_FILL_GPLMODE_ABOVE = 4,
  P_FILL_GPLMODE_BELOW = 5,
} ePen_FillLayerModes;

/* BrushPenSettings->p_eraser_mode */
typedef enum ePen_BrushEraserMode {
  P_BRUSH_ERASER_SOFT = 0,
  P_BRUSH_ERASER_HARD = 1,
  P_BRUSH_ERASER_STROKE = 2,
} ePen_BrushEraserMode;

/* BrushPenSettings->brush_drw_mode */
typedef enum ePen_BrushMode {
  P_BRUSH_MODE_ACTIVE = 0,
  P_BRUSH_MODE_MATERIAL = 1,
  P_BRUSH_MODE_VERTEXCOLOR = 2,
} ePen_BrushMode;

/* BrushGpencilSettings default brush icons */
typedef enum ePen_BrushIcons {
  P_BRUSH_ICON_PEN = 1,
  P_BRUSH_ICON_PEN = 2,
  P_BRUSH_ICON_INK = 3,
  P_BRUSH_ICON_INKNOISE = 4,
  P_BRUSH_ICON_BLOCK = 5,
  P_BRUSH_ICON_MARKER = 6,
  P_BRUSH_ICON_FILL = 7,
  P_BRUSH_ICON_ERASE_SOFT = 8,
  P_BRUSH_ICON_ERASE_HARD = 9,
  P_BRUSH_ICON_ERASE_STROKE = 10,
  P_BRUSH_ICON_AIRBRUSH = 11,
  P_BRUSH_ICON_CHISEL = 12,
  P_BRUSH_ICON_TINT = 13,
  P_BRUSH_ICON_VERT_DRW = 14,
  P_BRUSH_ICON_VERT_BLUR = 15,
  P_BRUSH_ICON_VERT_AVG = 16,
  P_BRUSH_ICON_VERT_SMEAR = 17,
  P_BRUSH_ICON_VERT_REPLACE = 18,
  P_BRUSH_ICON_BRUSH_SMOOTH = 19,
  P_BRUSH_ICON_BRUSH_THICKNESS = 20,
  P_BRUSH_ICON_BRUSH_STRENGTH = 21,
  P_BRUSH_ICON_BRUSH_RANDOMIZE = 22,
  P_BRUSH_ICON_BRUSH_GRAB = 23,
  P_BRUSH_ICON_BRUSH_PUSH = 24,
  P_BRUSH_ICON_BRUSH_TWIST = 25,
  P_BRUSH_ICON_BRUSH_PINCH = 26,
  P_BRUSH_ICON_BRUSH_CLONE = 27,
  P_BRUSH_ICON_BRUSH_WEIGHT = 28,
} ePen_BrushIcons;

typedef enum eBrushCurvePreset {
  BRUSH_CURVE_CUSTOM = 0,
  BRUSH_CURVE_SMOOTH = 1,
  BRUSH_CURVE_SPHERE = 2,
  BRUSH_CURVE_ROOT = 3,
  BRUSH_CURVE_SHARP = 4,
  BRUSH_CURVE_LIN = 5,
  BRUSH_CURVE_POW4 = 6,
  BRUSH_CURVE_INVSQUARE = 7,
  BRUSH_CURVE_CONSTANT = 8,
  BRUSH_CURVE_SMOOTHER = 9,
} eBrushCurvePreset;

typedef enum eBrushDeformTarget {
  BRUSH_DEFORM_TARGET_GEOMETRY = 0,
  BRUSH_DEFORM_TARGET_CLOTH_SIM = 1,
} eBrushDeformTarget;

typedef enum eBrushElasticDeformType {
  BRUSH_ELASTIC_DEFORM_GRAB = 0,
  BRUSH_ELASTIC_DEFORM_GRAB_BISCALE = 1,
  BRUSH_ELASTIC_DEFORM_GRAB_TRISCALE = 2,
  BRUSH_ELASTIC_DEFORM_SCALE = 3,
  BRUSH_ELASTIC_DEFORM_TWIST = 4,
} eBrushElasticDeformType;

typedef enum eBrushClothDeformType {
  BRUSH_CLOTH_DEFORM_DRAG = 0,
  BRUSH_CLOTH_DEFORM_PUSH = 1,
  BRUSH_CLOTH_DEFORM_GRAB = 2,
  BRUSH_CLOTH_DEFORM_PINCH_POINT = 3,
  BRUSH_CLOTH_DEFORM_PINCH_PERPENDICULAR = 4,
  BRUSH_CLOTH_DEFORM_INFLATE = 5,
  BRUSH_CLOTH_DEFORM_EXPAND = 6,
  BRUSH_CLOTH_DEFORM_SNAKE_HOOK = 7,
} eBrushClothDeformType;

typedef enum eBrushSmoothDeformType {
  BRUSH_SMOOTH_DEFORM_LAPLACIAN = 0,
  BRUSH_SMOOTH_DEFORM_SURFACE = 1,
} eBrushSmoothDeformType;

typedef enum eBrushClothForceFalloffType {
  BRUSH_CLOTH_FORCE_FALLOFF_RADIAL = 0,
  BRUSH_CLOTH_FORCE_FALLOFF_PLANE = 1,
} eBrushClothForceFalloffType;

typedef enum eBrushClothSimAreaType {
  BRUSH_CLOTH_SIM_AREA_LOCAL = 0,
  BRUSH_CLOTH_SIM_AREA_GLOBAL = 1,
  BRUSH_CLOTH_SIM_AREA_DYNAMIC = 2,
} eBrushClothSimAreaType;

typedef enum eBrushPoseDeformType {
  BRUSH_POSE_DEFORM_ROTATE_TWIST = 0,
  BRUSH_POSE_DEFORM_SCALE_TRANSLATE = 1,
  BRUSH_POSE_DEFORM_SQUASH_STRETCH = 2,
} eBrushPoseDeformType;

typedef enum eBrushPoseOriginType {
  BRUSH_POSE_ORIGIN_TOPOLOGY = 0,
  BRUSH_POSE_ORIGIN_FACE_SETS = 1,
  BRUSH_POSE_ORIGIN_FACE_SETS_FK = 2,
} eBrushPoseOriginType;

typedef enum eBrushSmearDeformType {
  BRUSH_SMEAR_DEFORM_DRAG = 0,
  BRUSH_SMEAR_DEFORM_PINCH = 1,
  BRUSH_SMEAR_DEFORM_EXPAND = 2,
} eBrushSmearDeformType;

typedef enum eBrushSlideDeformType {
  BRUSH_SLIDE_DEFORM_DRAG = 0,
  BRUSH_SLIDE_DEFORM_PINCH = 1,
  BRUSH_SLIDE_DEFORM_EXPAND = 2,
} eBrushSlideDeformType;

typedef enum eBrushBoundaryDeformType {
  BRUSH_BOUNDARY_DEFORM_BEND = 0,
  BRUSH_BOUNDARY_DEFORM_EXPAND = 1,
  BRUSH_BOUNDARY_DEFORM_INFLATE = 2,
  BRUSH_BOUNDARY_DEFORM_GRAB = 3,
  BRUSH_BOUNDARY_DEFORM_TWIST = 4,
  BRUSH_BOUNDARY_DEFORM_SMOOTH = 5,
} eBrushBushBoundaryDeformType;

typedef enum eBrushBoundaryFalloffType {
  BRUSH_BOUNDARY_FALLOFF_CONSTANT = 0,
  BRUSH_BOUNDARY_FALLOFF_RADIUS = 1,
  BRUSH_BOUNDARY_FALLOFF_LOOP = 2,
  BRUSH_BOUNDARY_FALLOFF_LOOP_INVERT = 3,
} eBrushBoundaryFalloffType;

typedef enum eBrushSnakeHookDeformType {
  BRUSH_SNAKE_HOOK_DEFORM_FALLOFF = 0,
  BRUSH_SNAKE_HOOK_DEFORM_ELASTIC = 1,
} eBrushSnakeHookDeformType;

/* Pensettings.Vertex_mode */
typedef enum ePen_Vert_Mode {
  /* Affect to Stroke only. */
  PPAINT_MODE_STROKE = 0,
  /* Affect to Fill only. */
  PPAINT_MODE_FILL = 1,
  /* Affect to both. */
  PPAINT_MODE_BOTH = 2,
} ePen_Vert_Mode;

/* sculpt_flag */
typedef enum ePen_Sculpt_Flag {
  /* invert the effect of the brush */
  P_SCULPT_FLAG_INVERT = (1 << 0),
  /* temp invert action */
  P_SCULPT_FLAG_TMP_INVERT = (1 << 3),
} ePen_Sculpt_Flag;

/* sculpt_mode_flag */
typedef enum eGP_Sculpt_Mode_Flag {
  /* apply brush to position */
  P_SCULPT_FLAGMODE_APPLY_POSITION = (1 << 0),
  /* apply brush to strength */
  P_SCULPT_FLAGMODE_APPLY_STRENGTH = (1 << 1),
  /* apply brush to thickness */
  P_SCULPT_FLAGMODE_APPLY_THICKNESS = (1 << 2),
  /* apply brush to uv data */
  P_SCULPT_FLAGMODE_APPLY_UV = (1 << 3),
} eP_Sculpt_Mode_Flag;

typedef enum eAutomasking_flag {
  BRUSH_AUTOMASKING_TOPOLOGY = (1 << 0),
  BRUSH_AUTOMASKING_FACE_SETS = (1 << 1),
  BRUSH_AUTOMASKING_BOUNDARY_EDGES = (1 << 2),
  BRUSH_AUTOMASKING_BOUNDARY_FACE_SETS = (1 << 3),
} eAutomasking_flag;

typedef enum ePaintBrush_flag {
  BRUSH_PAINT_HARDNESS_PRESSURE = (1 << 0),
  BRUSH_PAINT_HARDNESS_PRESSURE_INVERT = (1 << 1),
  BRUSH_PAINT_FLOW_PRESSURE = (1 << 2),
  BRUSH_PAINT_FLOW_PRESSURE_INVERT = (1 << 3),
  BRUSH_PAINT_WET_MIX_PRESSURE = (1 << 4),
  BRUSH_PAINT_WET_MIX_PRESSURE_INVERT = (1 << 5),
  BRUSH_PAINT_WET_PERSISTENCE_PRESSURE = (1 << 6),
  BRUSH_PAINT_WET_PERSISTENCE_PRESSURE_INVERT = (1 << 7),
  BRUSH_PAINT_DENSITY_PRESSURE = (1 << 8),
  BRUSH_PAINT_DENSITY_PRESSURE_INVERT = (1 << 9),
} ePaintBrush_flag;

/* Brush.gradient_src */
typedef enum eBrushGradientSrcStroke {
  BRUSH_GRADIENT_PRESSURE = 0,       /* gradient from pressure */
  BRUSH_GRADIENT_SPACING_REPEAT = 1, /* gradient from spacing */
  BRUSH_GRADIENT_SPACING_CLAMP = 2,  /* gradient from spacing */
} eBrushGradientSrcStroke;

typedef enum eBrushGradientSrcFill {
  BRUSH_GRADIENT_LINEAR = 0, /* gradient from pressure */
  BRUSH_GRADIENT_RADIAL = 1, /* gradient from spacing */
} eBrushGradientSrcFill;

/* Brush.flag */
typedef enum eBrushFlags {
  BRUSH_AIRBRUSH = (1 << 0),
  BRUSH_INVERT_TO_SCRAPE_FILL = (1 << 1),
  BRUSH_ALPHA_PRESSURE = (1 << 2),
  BRUSH_SIZE_PRESSURE = (1 << 3),
  BRUSH_JITTER_PRESSURE = (1 << 4),
  BRUSH_SPACING_PRESSURE = (1 << 5),
  BRUSH_ORIGINAL_PLANE = (1 << 6),
  BRUSH_GRAB_ACTIVE_VERTEX = (1 << 7),
  BRUSH_ANCHORED = (1 << 8),
  BRUSH_DIR_IN = (1 << 9),
  BRUSH_SPACE = (1 << 10),
  BRUSH_SMOOTH_STROKE = (1 << 11),
  BRUSH_PERSISTENT = (1 << 12),
  BRUSH_ACCUMULATE = (1 << 13),
  BRUSH_LOCK_ALPHA = (1 << 14),
  BRUSH_ORIGINAL_NORMAL = (1 << 15),
  BRUSH_OFFSET_PRESSURE = (1 << 16),
  BRUSH_SCENE_SPACING = (1 << 17),
  BRUSH_SPACE_ATTEN = (1 << 18),
  BRUSH_ADAPTIVE_SPACE = (1 << 19),
  BRUSH_LOCK_SIZE = (1 << 20),
  BRUSH_USE_GRADIENT = (1 << 21),
  BRUSH_EDGE_TO_EDGE = (1 << 22),
  BRUSH_DRAG_DOT = (1 << 23),
  BRUSH_INVERSE_SMOOTH_PRESSURE = (1 << 24),
  BRUSH_FRONTFACE_FALLOFF = (1 << 25),
  BRUSH_PLANE_TRIM = (1 << 26),
  BRUSH_FRONTFACE = (1 << 27),
  BRUSH_CUSTOM_ICON = (1 << 28),
  BRUSH_LINE = (1 << 29),
  BRUSH_ABSOLUTE_JITTER = (1 << 30),
  BRUSH_CURVE = (1u << 31),
} eBrushFlags;

/* Brush.sampling_flag */
typedef enum eBrushSamplingFlags {
  BRUSH_PAINT_ANTIALIASING = (1 << 0),
} eBrushSamplingFlags;

/* Brush.flag2 */
typedef enum eBrushFlags2 {
  BRUSH_MULTIPLANE_SCRAPE_DYNAMIC = (1 << 0),
  BRUSH_MULTIPLANE_SCRAPE_PLANES_PREVIEW = (1 << 1),
  BRUSH_POSE_IK_ANCHORED = (1 << 2),
  BRUSH_USE_CONNECTED_ONLY = (1 << 3),
  BRUSH_CLOTH_PIN_SIMULATION_BOUNDARY = (1 << 4),
  BRUSH_POSE_USE_LOCK_ROTATION = (1 << 5),
  BRUSH_CLOTH_USE_COLLISION = (1 << 6),
  BRUSH_AREA_RADIUS_PRESSURE = (1 << 7),
  BRUSH_GRAB_SILHOUETTE = (1 << 8),
} eBrushFlags2;

typedef enum {
  BRUSH_MASK_PRESSURE_RAMP = (1 << 1),
  BRUSH_MASK_PRESSURE_CUTOFF = (1 << 2),
} BrushMaskPressureFlags;

/* Brush.overlay_flags */
typedef enum eOverlayFlags {
  BRUSH_OVERLAY_CURSOR = (1),
  BRUSH_OVERLAY_PRIMARY = (1 << 1),
  BRUSH_OVERLAY_SECONDARY = (1 << 2),
  BRUSH_OVERLAY_CURSOR_OVERRIDE_ON_STROKE = (1 << 3),
  BRUSH_OVERLAY_PRIMARY_OVERRIDE_ON_STROKE = (1 << 4),
  BRUSH_OVERLAY_SECONDARY_OVERRIDE_ON_STROKE = (1 << 5),
} eOverlayFlags;

#define BRUSH_OVERLAY_OVERRIDE_MASK \
  (BRUSH_OVERLAY_CURSOR_OVERRIDE_ON_STROKE | BRUSH_OVERLAY_PRIMARY_OVERRIDE_ON_STROKE | \
   BRUSH_OVERLAY_SECONDARY_OVERRIDE_ON_STROKE)

/* Brush.sculpt_tool */
typedef enum eBrushSculptTool {
  SCULPT_TOOL_DRW = 1,
  SCULPT_TOOL_SMOOTH = 2,
  SCULPT_TOOL_PINCH = 3,
  SCULPT_TOOL_INFLATE = 4,
  SCULPT_TOOL_GRAB = 5,
  SCULPT_TOOL_LAYER = 6,
  SCULPT_TOOL_FLATTEN = 7,
  SCULPT_TOOL_CLAY = 8,
  SCULPT_TOOL_FILL = 9,
  SCULPT_TOOL_SCRAPE = 10,
  SCULPT_TOOL_NUDGE = 11,
  SCULPT_TOOL_THUMB = 12,
  SCULPT_TOOL_SNAKE_HOOK = 13,
  SCULPT_TOOL_ROTATE = 14,
  SCULPT_TOOL_SIMPLIFY = 15,
  SCULPT_TOOL_CREASE = 16,
  SCULPT_TOOL_BLOB = 17,
  SCULPT_TOOL_CLAY_STRIPS = 18,
  SCULPT_TOOL_MASK = 19,
  SCULPT_TOOL_DRW_SHARP = 20,
  SCULPT_TOOL_ELASTIC_DEFORM = 21,
  SCULPT_TOOL_POSE = 22,
  SCULPT_TOOL_MULTIPLANE_SCRAPE = 23,
  SCULPT_TOOL_SLIDE_RELAX = 24,
  SCULPT_TOOL_CLAY_THUMB = 25,
  SCULPT_TOOL_CLOTH = 26,
  SCULPT_TOOL_DRW_FACE_SETS = 27,
  SCULPT_TOOL_PAINT = 28,
  SCULPT_TOOL_SMEAR = 29,
  SCULPT_TOOL_BOUNDARY = 30,
  SCULPT_TOOL_DISPLACEMENT_ERASER = 31,
  SCULPT_TOOL_DISPLACEMENT_SMEAR = 32,
} eBrushSculptTool;

/* Brush.uv_sculpt_tool */
typedef enum eBrushUVSculptTool {
  UV_SCULPT_TOOL_GRAB = 0,
  UV_SCULPT_TOOL_RELAX = 1,
  UV_SCULPT_TOOL_PINCH = 2,
} eBrushUVSculptTool;

/* Brush.curves_sculpt_tool. */
typedef enum eBrushCurvesSculptTool {
  CURVES_SCULPT_TOOL_COMB = 0,
  CURVES_SCULPT_TOOL_DELETE = 1,
  CURVES_SCULPT_TOOL_SNAKE_HOOK = 2,
  CURVES_SCULPT_TOOL_ADD = 3,
  CURVES_SCULPT_TOOL_TEST1 = 4,
  CURVES_SCULPT_TOOL_TEST2 = 5,
} eBrushCurvesSculptTool;

/** When #BRUSH_ACCUMULATE is used */
#define SCULPT_TOOL_HAS_ACCUMULATE(t) \
  ELEM(t, \
       SCULPT_TOOL_DRW, \
       SCULPT_TOOL_DRW_SHARP, \
       SCULPT_TOOL_SLIDE_RELAX, \
       SCULPT_TOOL_CREASE, \
       SCULPT_TOOL_BLOB, \
       SCULPT_TOOL_INFLATE, \
       SCULPT_TOOL_CLAY, \
       SCULPT_TOOL_CLAY_STRIPS, \
       SCULPT_TOOL_CLAY_THUMB, \
       SCULPT_TOOL_ROTATE, \
       SCULPT_TOOL_SCRAPE, \
       SCULPT_TOOL_FLATTEN)

#define SCULPT_TOOL_HAS_NORMAL_WEIGHT(t) \
  ELEM(t, SCULPT_TOOL_GRAB, SCULPT_TOOL_SNAKE_HOOK, SCULPT_TOOL_ELASTIC_DEFORM)

#define SCULPT_TOOL_HAS_RAKE(t) ELEM(t, SCULPT_TOOL_SNAKE_HOOK)

#define SCULPT_TOOL_HAS_DYNTOPO(t) \
  (ELEM(t, /* These brushes, as currently coded, cannot support dynamic topology */ \
        SCULPT_TOOL_GRAB, \
        SCULPT_TOOL_ROTATE, \
        SCULPT_TOOL_CLOTH, \
        SCULPT_TOOL_THUMB, \
        SCULPT_TOOL_LAYER, \
        SCULPT_TOOL_DISPLACEMENT_ERASER, \
        SCULPT_TOOL_DRW_SHARP, \
        SCULPT_TOOL_SLIDE_RELAX, \
        SCULPT_TOOL_ELASTIC_DEFORM, \
        SCULPT_TOOL_BOUNDARY, \
        SCULPT_TOOL_POSE, \
        SCULPT_TOOL_DRW_FACE_SETS, \
        SCULPT_TOOL_PAINT, \
        SCULPT_TOOL_SMEAR, \
\
        /* These brushes could handle dynamic topology, \ \
         * but user feedback indicates it's better not to */ \
        SCULPT_TOOL_SMOOTH, \
        SCULPT_TOOL_MASK) == 0)

#define SCULPT_TOOL_HAS_TOPOLOGY_RAKE(t) \
  (ELEM(t, /* These brushes, as currently coded, cannot support topology rake. */ \
        SCULPT_TOOL_GRAB, \
        SCULPT_TOOL_ROTATE, \
        SCULPT_TOOL_THUMB, \
        SCULPT_TOOL_DRW_SHARP, \
        SCULPT_TOOL_DISPLACEMENT_ERASER, \
        SCULPT_TOOL_SLIDE_RELAX, \
        SCULPT_TOOL_MASK) == 0)

/* #ImgPaintSettings.tool */
typedef enum eBrushImgPaintTool {
  PAINT_TOOL_DRW = 0,
  PAINT_TOOL_SOFTEN = 1,
  PAINT_TOOL_SMEAR = 2,
  PAINT_TOOL_CLONE = 3,
  PAINT_TOOL_FILL = 4,
  PAINT_TOOL_MASK = 5,
} eBrushImgPaintTool;

/* The enums here should be kept in sync with the weight paint tool.
 * This is because #smooth_brush_toggle_on and #smooth_brush_toggle_off
 * assumes that the blur brush has the same enum value. */
typedef enum eBrushVertPaintTool {
  VPAINT_TOOL_DRW = 0,
  VPAINT_TOOL_BLUR = 1,
  VPAINT_TOOL_AVG = 2,
  VPAINT_TOOL_SMEAR = 3,
} eBrushVertPaintTool;

/* See eBrushVertPaintTool when changing this definition. */
typedef enum eBrushWeightPaintTool {
  WPAINT_TOOL_DRW = 0,
  WPAINT_TOOL_BLUR = 1,
  WPAINT_TOOL_AVG = 2,
  WPAINT_TOOL_SMEAR = 3,
} eBrushWeightPaintTool;

/* BrushPenSettings->brush type */
typedef enum eBrushPaintTool {
  PAINT_TOOL_DRW = 0,
  PAINT_TOOL_FILL = 1,
  PAINT_TOOL_ERASE = 2,
  PAINT_TOOL_TINT = 3,
} eBrushPaintTool;

/* BrushPenSettings->brush type */
typedef enum eBrushPenVertTool {
  PVERTEX_TOOL_DRW = 0,
  PVERTEX_TOOL_BLUR = 1,
  PVERTEX_TOOL_AVG = 2,
  PVERTEX_TOOL_TINT = 3,
  PVERTEX_TOOL_SMEAR = 4,
  PVERTEX_TOOL_REPLACE = 5,
} eBrushPenVertTool;

/* BrushPenSettings->brush type */
typedef enum eBrushPenSculptTool {
  PSCULPT_TOOL_SMOOTH = 0,
  PSCULPT_TOOL_THICKNESS = 1,
  PSCULPT_TOOL_STRENGTH = 2,
  PSCULPT_TOOL_GRAB = 3,
  PSCULPT_TOOL_PUSH = 4,
  PSCULPT_TOOL_TWIST = 5,
  PSCULPT_TOOL_PINCH = 6,
  PSCULPT_TOOL_RANDOMIZE = 7,
  PSCULPT_TOOL_CLONE = 8,
} eBrushPenSculptTool;

/* BrushPenSettings->brush type */
typedef enum eBrushPenWeightTool {
  PENWEIGHT_TOOL_DRW = 0,
} eBrushPenWeightTool;

/* direction that the brush displaces along */
enum {
  SCULPT_DISP_DIR_AREA = 0,
  SCULPT_DISP_DIR_VIEW = 1,
  SCULPT_DISP_DIR_X = 2,
  SCULPT_DISP_DIR_Y = 3,
  SCULPT_DISP_DIR_Z = 4,
};

typedef enum {
  BRUSH_MASK_DRW = 0,
  BRUSH_MASK_SMOOTH = 1,
} BrushMaskTool;

/* blur kernel types, Brush.blur_mode */
typedef enum eBlurKernelType {
  KERNEL_GAUSSIAN = 0,
  KERNEL_BOX = 1,
} eBlurKernelType;

/* Brush.falloff_shape */
typedef enum eBrushFalloffShape {
  PAINT_FALLOFF_SHAPE_SPHERE = 0,
  PAINT_FALLOFF_SHAPE_TUBE = 1,
} eBrushFalloffShape;

#define MAX_BRUSH_PIXEL_RADIUS 500
#define GP_MAX_BRUSH_PIXEL_RADIUS 1000

#ifdef __cplusplus
}
#endif
