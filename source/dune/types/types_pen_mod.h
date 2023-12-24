#pragma once

#include "types_defs.h"
#include "types_list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct LatticeDeformData;
struct ShrinkwrapTreeData;

/* WARNING ALERT! TYPEDEF VALS ARE WRITTEN IN FILES! SO DO NOT CHANGE!
 * (ONLY ADD NEW ITEMS AT THE END) */

typedef enum PenModType {
  eGpenModType_None = 0,
  eGpenModType_Noise = 1,
  eGpenModType_Subdiv = 2,
  eGpenModType_Thick = 3,
  eGpenModType_Tint = 4,
  eGpenModType_Array = 5,
  eGpenModType_Build = 6,
  eGpenModType_Opacity = 7,
  eGpencilModType_Color = 8,
  eGpencilModType_Lattice = 9,
  eGpencilModType_Simplify = 10,
  eGpencilModifierType_Smooth = 11,
  eGpencilModifierType_Hook = 12,
  eGpencilModifierType_Offset = 13,
  eGpencilModifierType_Mirror = 14,
  eGpencilModifierType_Armature = 15,
  eGpencilModifierType_Time = 16,
  eGpencilModifierType_Multiply = 17,
  eGpencilModifierType_Texture = 18,
  eGpencilModifierType_Lineart = 19,
  eGpencilModifierType_Length = 20,
  eGpencilModifierType_WeightProximity = 21,
  eGpencilModifierType_Dash = 22,
  eGpencilModifierType_WeightAngle = 23,
  eGpencilModType_Shrinkwrap = 24,
  /* Keep last. */
  NUM_PEN_MOD_TYPES,
} GpenModType;

typedef enum PenModMode {
  eGpenModMode_Realtime = (1 << 0),
  eGpenModMode_Render = (1 << 1),
  eGpenModMode_Editmode = (1 << 2),
#ifdef TYPES_DEPRECATED_ALLOW
  eGpencilModMode_Expanded_DEPRECATED = (1 << 3),
#endif
  eGpenModMode_Virtual = (1 << 4),
} PenModMode;

typedef enum {
  /* This mod has been inserted in local override, and hence can be fully edited. */
  ePenModFlagOverrideLibLocal = (1 << 0),
} penModFlag;

typedef struct PenModData {
  struct PenModData *next, *prev;

  int type, mode;
  char _pad0[4];
  short flag;
  /* An "expand" bit for each of the modifier's (sub)panels (uiPanelDataExpansion). */
  short ui_expand_flag;
  /* MAX_NAME. */
  char name[64];

  char *error;
} PenModData;

typedef struct NoisePenModData {
  PenModData modifier;
  /* Material for filtering. */
  struct Material *material;
  /* Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] DNA_DEPRECATED;
  /** Optional vertexgroup name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Several flags. */
  int flag;
  /** Factor of noise. */
  float factor;
  float factor_strength;
  float factor_thickness;
  float factor_uvs;
  /** Noise Frequency scaling */
  float noise_scale;
  float noise_offset;
  char _pad[4];
  /** How many frames before recalculate randoms. */
  int step;
  /** Custom index for passes. */
  int layer_pass;
  /** Random seed */
  int seed;
  struct CurveMapping *curve_intensity;
} NoiseGpencilModifierData;

typedef enum eNoiseGpencil_Flag {
  GP_NOISE_USE_RANDOM = (1 << 0),
  GP_NOISE_MOD_LOCATION = (1 << 1),  /* Deprecated (only for versioning). */
  GP_NOISE_MOD_STRENGTH = (1 << 2),  /* Deprecated (only for versioning). */
  GP_NOISE_MOD_THICKNESS = (1 << 3), /* Deprecated (only for versioning). */
  GP_NOISE_FULL_STROKE = (1 << 4),
  GP_NOISE_CUSTOM_CURVE = (1 << 5),
  GP_NOISE_INVERT_LAYER = (1 << 6),
  GP_NOISE_INVERT_PASS = (1 << 7),
  GP_NOISE_INVERT_VGROUP = (1 << 8),
  GP_NOISE_MOD_UV = (1 << 9), /* Deprecated (only for versioning). */
  GP_NOISE_INVERT_LAYERPASS = (1 << 10),
  GP_NOISE_INVERT_MATERIAL = (1 << 11),
} eNoiseGpencil_Flag;

typedef struct SubdivGpencilModifierData {
  GpencilModifierData modifier;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] DNA_DEPRECATED;
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Factor of subdivision. */
  int level;
  /** Custom index for passes. */
  int layer_pass;
  /** Type of subdivision */
  short type;
  char _pad[6];
} SubdivGpencilModifierData;

typedef enum eSubdivGpencil_Flag {
  GP_SUBDIV_INVERT_LAYER = (1 << 1),
  GP_SUBDIV_INVERT_PASS = (1 << 2),
  GP_SUBDIV_INVERT_LAYERPASS = (1 << 3),
  GP_SUBDIV_INVERT_MATERIAL = (1 << 4),
} eSubdivGpencil_Flag;

typedef enum eSubdivGpencil_Type {
  GP_SUBDIV_CATMULL = 0,
  GP_SUBDIV_SIMPLE = 1,
} eSubdivGpencil_Type;

typedef struct ThickGpencilModifierData {
  GpencilModifierData modifier;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] DNA_DEPRECATED;
  /** Optional vertexgroup name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Relative thickness factor. */
  float thickness_fac;
  /** Absolute thickness override. */
  int thickness;
  /** Custom index for passes. */
  int layer_pass;
  char _pad[4];
  struct CurveMapping *curve_thickness;
} ThickGpencilModifierData;

typedef enum eThickGpencil_Flag {
  GP_THICK_INVERT_LAYER = (1 << 0),
  GP_THICK_INVERT_PASS = (1 << 1),
  GP_THICK_INVERT_VGROUP = (1 << 2),
  GP_THICK_CUSTOM_CURVE = (1 << 3),
  GP_THICK_NORMALIZE = (1 << 4),
  GP_THICK_INVERT_LAYERPASS = (1 << 5),
  GP_THICK_INVERT_MATERIAL = (1 << 6),
  GP_THICK_WEIGHT_FACTOR = (1 << 7),
} eThickGpencil_Flag;

typedef struct TimeGpencilModifierData {
  GpencilModifierData modifier;
  /** Layer name. */
  char layername[64];
  /** Custom index for passes. */
  int layer_pass;
  /** Flags. */
  int flag;
  int offset;
  /** Animation scale. */
  float frame_scale;
  int mode;
  /** Start and end frame for custom range. */
  int sfra, efra;
  char _pad[4];
} TimeGpencilModifierData;

typedef enum eTimeGpencil_Flag {
  GP_TIME_INVERT_LAYER = (1 << 0),
  GP_TIME_KEEP_LOOP = (1 << 1),
  GP_TIME_INVERT_LAYERPASS = (1 << 2),
  GP_TIME_CUSTOM_RANGE = (1 << 3),
} eTimeGpencil_Flag;

typedef enum eTimeGpencil_Mode {
  GP_TIME_MODE_NORMAL = 0,
  GP_TIME_MODE_REVERSE = 1,
  GP_TIME_MODE_FIX = 2,
} eTimeGpencil_Mode;

typedef enum eModifyColorGpencil_Flag {
  GP_MODIFY_COLOR_BOTH = 0,
  GP_MODIFY_COLOR_STROKE = 1,
  GP_MODIFY_COLOR_FILL = 2,
  GP_MODIFY_COLOR_HARDNESS = 3,
} eModifyColorGpencil_Flag;

typedef enum eOpacityModesGpencil_Flag {
  GP_OPACITY_MODE_MATERIAL = 0,
  GP_OPACITY_MODE_STRENGTH = 1,
} eOpacityModesGpencil_Flag;

typedef struct ColorGpencilModifierData {
  GpencilModifierData modifier;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] DNA_DEPRECATED;
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** HSV factors. */
  float hsv[3];
  /** Modify stroke, fill or both. */
  char modify_color;
  char _pad[3];
  /** Custom index for passes. */
  int layer_pass;

  char _pad1[4];
  struct CurveMapping *curve_intensity;
} ColorGpencilModifierData;

typedef enum eColorGpencil_Flag {
  GP_COLOR_INVERT_LAYER = (1 << 1),
  GP_COLOR_INVERT_PASS = (1 << 2),
  GP_COLOR_INVERT_LAYERPASS = (1 << 3),
  GP_COLOR_INVERT_MATERIAL = (1 << 4),
  GP_COLOR_CUSTOM_CURVE = (1 << 5),
} eColorGpencil_Flag;

typedef struct OpacityGpencilModifierData {
  GpencilModifierData modifier;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] DNA_DEPRECATED;
  /** Optional vertexgroup name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Main Opacity factor. */
  float factor;
  /** Modify stroke, fill or both. */
  char modify_color;
  char _pad[3];
  /** Custom index for passes. */
  int layer_pass;

  float hardeness;
  struct CurveMapping *curve_intensity;
} OpacityGpencilModifierData;

typedef enum eOpacityGpencil_Flag {
  GP_OPACITY_INVERT_LAYER = (1 << 0),
  GP_OPACITY_INVERT_PASS = (1 << 1),
  GP_OPACITY_INVERT_VGROUP = (1 << 2),
  GP_OPACITY_INVERT_LAYERPASS = (1 << 4),
  GP_OPACITY_INVERT_MATERIAL = (1 << 5),
  GP_OPACITY_CUSTOM_CURVE = (1 << 6),
  GP_OPACITY_NORMALIZE = (1 << 7),
  GP_OPACITY_WEIGHT_FACTOR = (1 << 8),
} eOpacityGpencil_Flag;

typedef struct ArrayGpencilModifierData {
  GpencilModifierData modifier;
  struct Object *object;
  /** Material for filtering. */
  struct Material *material;
  /** Number of elements in array. */
  int count;
  /** Several flags. */
  int flag;
  /** Location increments. */
  float offset[3];
  /** Shift increment. */
  float shift[3];
  /** Random Offset. */
  float rnd_offset[3];
  /** Random Rotation. */
  float rnd_rot[3];
  /** Random Scales. */
  float rnd_scale[3];
  char _pad[4];
  /** (first element is the index) random values. */
  int seed;

  /** Custom index for passes. */
  int pass_index;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] DNA_DEPRECATED;
  /** Material replace (0 keep default). */
  int mat_rpl;
  /** Custom index for passes. */
  int layer_pass;
} ArrayGpencilModifierData;

typedef enum eArrayGpencil_Flag {
  GP_ARRAY_INVERT_LAYER = (1 << 2),
  GP_ARRAY_INVERT_PASS = (1 << 3),
  GP_ARRAY_INVERT_LAYERPASS = (1 << 5),
  GP_ARRAY_INVERT_MATERIAL = (1 << 6),
  GP_ARRAY_USE_OFFSET = (1 << 7),
  GP_ARRAY_USE_RELATIVE = (1 << 8),
  GP_ARRAY_USE_OB_OFFSET = (1 << 9),
  GP_ARRAY_UNIFORM_RANDOM_SCALE = (1 << 10),
} eArrayGpencil_Flag;

typedef struct BuildGpencilModifierData {
  GpencilModifierData modifier;
  /** Material for filtering. */
  struct Material *material;

  /** If set, restrict modifier to operating on this layer. */
  char layername[64];
  int pass_index;

  /** Material name. */
  char materialname[64] DNA_DEPRECATED;

  /** Custom index for passes. */
  int layer_pass;

  /**
   * If GP_BUILD_RESTRICT_TIME is set,
   * the defines the frame range where GP frames are considered.
   */
  float start_frame;
  float end_frame;

  /** For each pair of gp keys, number of frames before strokes start appearing. */
  float start_delay;
  /** For each pair of gp keys, number of frames that build effect must be completed within. */
  float length;

  /** (eGpencilBuild_Flag) Options for controlling modifier behavior. */
  short flag;

  /** (eGpencilBuild_Mode) How are strokes ordered. */
  short mode;
  /** (eGpencilBuild_Transition) In what order do stroke points appear/disappear. */
  short transition;

  /**
   * (eGpencilBuild_TimeAlignment)
   * For the "Concurrent" mode, when should "shorter" strips start/end.
   */
  short time_alignment;
  /** Factor of the stroke (used instead of frame evaluation. */
  float percentage_fac;
  char _pad[4];
} BuildGpencilModifierData;

typedef enum eBuildGpencil_Mode {
  /* Strokes are shown one by one until all have appeared */
  GP_BUILD_MODE_SEQUENTIAL = 0,
  /* All strokes start at the same time */
  GP_BUILD_MODE_CONCURRENT = 1,
  /* Only the new strokes are built */
  GP_BUILD_MODE_ADDITIVE = 2,
} eBuildGpencil_Mode;

typedef enum eBuildGpencil_Transition {
  /* Show in forward order */
  GP_BUILD_TRANSITION_GROW = 0,
  /* Hide in reverse order */
  GP_BUILD_TRANSITION_SHRINK = 1,
  /* Hide in forward order */
  GP_BUILD_TRANSITION_FADE = 2,
} eBuildGpencil_Transition;

typedef enum eBuildGpencil_TimeAlignment {
  /* All strokes start at same time */
  GP_BUILD_TIMEALIGN_START = 0,
  /* All strokes end at same time */
  GP_BUILD_TIMEALIGN_END = 1,

  /* TODO: Random Offsets, Stretch-to-Fill */
} eBuildGpencil_TimeAlignment;

typedef enum eBuildGpencil_Flag {
  /* Restrict modifier to particular layer/passes? */
  GP_BUILD_INVERT_LAYER = (1 << 0),
  GP_BUILD_INVERT_PASS = (1 << 1),

  /* Restrict modifier to only operating between the nominated frames */
  GP_BUILD_RESTRICT_TIME = (1 << 2),
  GP_BUILD_INVERT_LAYERPASS = (1 << 3),

  /* Use a percentage instead of frame number to evaluate strokes. */
  GP_BUILD_PERCENTAGE = (1 << 4),
} eBuildGpencil_Flag;

typedef struct LatticeGpencilModifierData {
  GpencilModifierData modifier;
  struct Object *object;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] DNA_DEPRECATED;
  /** Optional vertexgroup name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  float strength;
  /** Custom index for passes. */
  int layer_pass;
  /** Runtime only. */
  struct LatticeDeformData *cache_data;
} LatticeGpencilModifierData;

typedef enum eLatticeGpencil_Flag {
  P_LATTICE_INVERT_LAYER = (1 << 0),
  P_LATTICE_INVERT_PASS = (1 << 1),
  P_LATTICE_INVERT_VGROUP = (1 << 2),
  P_LATTICE_INVERT_LAYERPASS = (1 << 3),
  P_LATTICE_INVERT_MATERIAL = (1 << 4),
} eLatticePen_Flag;

typedef struct LengthPenModData {
  PenModData mod;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Custom index for passes. */
  int layer_pass;
  /** Length. */
  float start_fac, end_fac;
  /** Random length factors. */
  float rand_start_fac, rand_end_fac, rand_offset;
  /** Overshoot trajectory factor. */
  float overshoot_fac;
  /** (first element is the index) random values. */
  int seed;
  /** How many frames before recalculate randoms. */
  int step;
  /** Mod mode. */
  int mode;
  char _pad[4];
  /* Curvature parameters. */
  float point_density;
  float segment_influence;
  float max_angle;
} LengthPenModData;

typedef enum eLengthPen_Flag {
  PEN_LENGTH_INVERT_LAYER = (1 << 0),
  PEN_LENGTH_INVERT_PASS = (1 << 1),
  PEN_LENGTH_INVERT_LAYERPASS = (1 << 2),
  PEN_LENGTH_INVERT_MATERIAL = (1 << 3),
  PEN_LENGTH_USE_CURVATURE = (1 << 4),
  PEN_LENGTH_INVERT_CURVATURE = (1 << 5),
  PEN_LENGTH_USE_RANDOM = (1 << 6),
} eLengthPen_Flag;

typedef enum eLengthPen_Type {
  PEN_LENGTH_RELATIVE = 0,
  PEN_LENGTH_ABSOLUTE = 1,
} eLengthPen_Type;

typedef struct DashPenModSegment {
  char name[64];
  /* For path reference. */
  struct DashPenModData *dmd;
  int dash;
  int gap;
  float radius;
  float opacity;
  int mat_nr;
  int _pad;
} DashPenModSegment;

typedef struct DashPenModData {
  PenModData mod;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Custom index for passes. */
  int layer_pass;

  int dash_offset;

  DashPenModSegment *segments;
  int segments_len;
  int segment_active_index;

} DashPenModData;

typedef struct MirrorPenModData {
  PenModData mod;
  struct Object *object;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] TYPES_DEPRECATED;
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Custom index for passes. */
  int layer_pass;
  char _pad[4];
} MirrorPenModData;

typedef enum eMirrorPen_Flag {
  PEN_MIRROR_INVERT_LAYER = (1 << 0),
  PEN_MIRROR_INVERT_PASS = (1 << 1),
  PEN_MIRROR_CLIPPING = (1 << 2),
  PEN_MIRROR_AXIS_X = (1 << 3),
  PEN_MIRROR_AXIS_Y = (1 << 4),
  PEN_MIRROR_AXIS_Z = (1 << 5),
  PEN_MIRROR_INVERT_LAYERPASS = (1 << 6),
  PEN_MIRROR_INVERT_MATERIAL = (1 << 7),
} eMirrorPen_Flag;

typedef struct HookPenModData {
  PenModData mod;

  struct Object *object;
  /** Material for filtering. */
  struct Material *material;
  /** Optional name of bone target, MAX_ID_NAME-2. */
  char subtarget[64];
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] TYPES_DEPRECATED;
  /** Optional vertexgroup name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Custom index for passes. */
  int layer_pass;
  char _pad[4];

  int flag;
  /** Use enums from WarpPenMod (exact same functionality). */
  char falloff_type;
  char _pad1[3];
  /** Matrix making current transform unmodified. */
  float parentinv[4][4];
  /** Visualization of hook. */
  float cent[3];
  /** If not zero, falloff is distance where influence zero. */
  float falloff;
  float force;
  struct CurveMapping *curfalloff;
} HookPenModData;

typedef enum eHookPen_Flag {
  PEN_HOOK_INVERT_LAYER = (1 << 0),
  PEN_HOOK_INVERT_PASS = (1 << 1),
  PEN_HOOK_INVERT_VGROUP = (1 << 2),
  PEN_HOOK_UNIFORM_SPACE = (1 << 3),
  PEN_HOOK_INVERT_LAYERPASS = (1 << 4),
  PEN_HOOK_INVERT_MATERIAL = (1 << 5),
} eHookPen_Flag;

typedef enum eHookPen_Falloff {
  ePenHook_Falloff_None = 0,
  ePenHook_Falloff_Curve = 1,
  ePenHook_Falloff_Sharp = 2,
  ePenHook_Falloff_Smooth = 3,
  ePenHook_Falloff_Root = 4,
  ePenHook_Falloff_Linear = 5,
  ePenHook_Falloff_Const = 6,
  ePenHook_Falloff_Sphere = 7,
  ePenHook_Falloff_InvSquare = 8,
} eHookPen_Falloff;

typedef struct SimplifyPenModData {
  PenModData mod;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] TYPES_DEPRECATED;
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Factor of simplify. */
  float factor;
  /** Type of simplify. */
  short mode;
  /** Every n vertex to keep. */
  short step;
  /** Custom index for passes. */
  int layer_pass;
  /** Sample length */
  float length;
  /** Sample sharp threshold */
  float sharp_threshold;
  /** Merge distance */
  float distance;
} SimplifyPenModData;

typedef enum eSimplifyPen_Flag {
  PEN_SIMPLIFY_INVERT_LAYER = (1 << 0),
  PEN_SIMPLIFY_INVERT_PASS = (1 << 1),
  PEN_SIMPLIFY_INVERT_LAYERPASS = (1 << 2),
  PEN_SIMPLIFY_INVERT_MATERIAL = (1 << 3),
} eSimplifyPen_Flag;

typedef enum eSimplifyPen_Mode {
  /* Keep only one vertex every n vertices */
  PEN_SIMPLIFY_FIXED = 0,
  /* Use RDP algorithm */
  PEN_SIMPLIFY_ADAPTIVE = 1,
  /* Sample the stroke using a fixed length */
  PEN_SIMPLIFY_SAMPLE = 2,
  /* Sample the stroke doing vertex merge */
  PEN_SIMPLIFY_MERGE = 3,
} eSimplifyPen_Mode;

typedef struct OffsetPenModData {
  PenModData mod;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] TYPES_DEPRECATED;
  /** Optional vertexgroup name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  float loc[3];
  float rot[3];
  float scale[3];
  /** Random Offset. */
  float rnd_offset[3];
  /** Random Rotation. */
  float rnd_rot[3];
  /** Random Scales. */
  float rnd_scale[3];
  /** (first element is the index) random values. */
  int seed;
  /** Custom index for passes. */
  int layer_pass;
} OffsetPenModData;

typedef enum eOffsetPen_Flag {
  PEN_OFFSET_INVERT_LAYER = (1 << 0),
  PEN_OFFSET_INVERT_PASS = (1 << 1),
  PEN_OFFSET_INVERT_VGROUP = (1 << 2),
  PEN_OFFSET_INVERT_LAYERPASS = (1 << 3),
  PEN_OFFSET_INVERT_MATERIAL = (1 << 4),
  PEN_OFFSET_UNIFORM_RANDOM_SCALE = (1 << 5),
} eOffsetPen_Flag;

typedef struct SmoothPenModData {
  PenModData mod;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] TYPES_DEPRECATED;
  /** Optional vertexgroup name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Several flags. */
  int flag;
  /** Factor of smooth. */
  float factor;
  /** How many times apply smooth. */
  int step;
  /** Custom index for passes. */
  int layer_pass;

  char _pad1[4];
  struct CurveMapping *curve_intensity;
} SmoothPenModData;

typedef enum eSmoothGpencil_Flag {
  PEN_SMOOTH_MOD_LOCATION = (1 << 0),
  PEN_SMOOTH_MOD_STRENGTH = (1 << 1),
  PEN_SMOOTH_MOD_THICKNESS = (1 << 2),
  PEN_SMOOTH_INVERT_LAYER = (1 << 3),
  PEN_SMOOTH_INVERT_PASS = (1 << 4),
  PEN_SMOOTH_INVERT_VGROUP = (1 << 5),
  PEN_SMOOTH_MOD_UV = (1 << 6),
  PEN_SMOOTH_INVERT_LAYERPASS = (1 << 7),
  PEN_SMOOTH_INVERT_MATERIAL = (1 << 4),
  PEN_SMOOTH_CUSTOM_CURVE = (1 << 8),
} eSmoothPen_Flag;

typedef struct ArmaturePenModData {
  PenModData mod;
  /** #eArmature_DeformFlag use instead of #bArmature.deformflag. */
  short deformflag, multi;
  int _pad;
  struct Object *object;
  /** Stored input of previous modifier, for vertex-group blending. */
  float (*vert_coords_prev)[3];
  /** MAX_VGROUP_NAME. */
  char vgname[64];

} ArmaturePenModData;

typedef struct MultiplyPenModData {
  PenModData mod;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] TYPES_DEPRECATED;
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Custom index for passes. */
  int layer_pass;

  int flags;

  int duplications;
  float distance;
  /* -1:inner 0:middle 1:outer */
  float offset;

  float fading_center;
  float fading_thickness;
  float fading_opacity;

} MultiplyPenModData;

typedef enum eMultiplyPen_Flag {
  /* PEN_MULTIPLY_ENABLE_ANGLE_SPLITTING = (1 << 1),  Deprecated. */
  PEN_MULTIPLY_ENABLE_FADING = (1 << 2),
} eMultiplyPen_Flag;

typedef struct TintPenModData {
  PenModData mod;

  struct Object *object;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] DNA_DEPRECATED;
  /** Optional vertexgroup name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Custom index for passes. */
  int layer_pass;
  /** Flags. */
  int flag;
  /** Mode (Stroke/Fill/Both). */
  int mode;

  float factor;
  float radius;
  /** Simple Tint color. */
  float rgb[3];
  /** Type of Tint. */
  int type;

  struct CurveMapping *curve_intensity;

  struct ColorBand *colorband;
} TintPenModData;

typedef enum eTintPen_Type {
  PEN_TINT_UNIFORM = 0,
  PEN_TINT_GRADIENT = 1,
} eTintPen_Type;

typedef enum eTintPen_Flag {
  PEN_TINT_INVERT_LAYER = (1 << 0),
  PEN_TINT_INVERT_PASS = (1 << 1),
  PEN_TINT_INVERT_VGROUP = (1 << 2),
  PEN_TINT_INVERT_LAYERPASS = (1 << 4),
  PEN_TINT_INVERT_MATERIAL = (1 << 5),
  PEN_TINT_CUSTOM_CURVE = (1 << 6),
  PEN_TINT_WEIGHT_FACTOR = (1 << 7),
} eTintPen_Flag;

typedef struct TexturePenModData {
  PenModData mod;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Material name. */
  char materialname[64] TYPES_DEPRECATED;
  /** Optional vertexgroup name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Offset value to add to uv_fac. */
  float uv_offset;
  float uv_scale;
  float fill_rotation;
  float fill_offset[2];
  float fill_scale;
  /** Custom index for passes. */
  int layer_pass;
  /** Texture fit options. */
  short fit_method;
  short mode;
  /** Dot texture rotation */
  float alignment_rotation;
  char _pad[4];
} TexturePenModData;

typedef enum eTexturePen_Flag {
  PEN_TEX_INVERT_LAYER = (1 << 0),
  PEN_TEX_INVERT_PASS = (1 << 1),
  PEN_TEX_INVERT_VGROUP = (1 << 2),
  PEN_TEX_INVERT_LAYERPASS = (1 << 3),
  PEN_TEX_INVERT_MATERIAL = (1 << 4),
} eTexturePen_Flag;

/* Texture->fit_method */
typedef enum eTexturePen_Fit {
  PEN_TEX_FIT_STROKE = 0,
  PEN_TEX_CONSTANT_LENGTH = 1,
} eTexturePen_Fit;

/* Texture->mode */
typedef enum eTexturePen_Mode {
  STROKE = 0,
  FILL = 1,
  STROKE_AND_FILL = 2,
} eTexturePen_Mode;

typedef struct WeightProxPenModData {
  PenModData mod;
  /** Target vertexgroup name, MAX_VGROUP_NAME. */
  char target_vgname[64];
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Optional vertexgroup filter name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Minimum valid weight (clamp value). */
  float min_weight;
  /** Custom index for passes. */
  int layer_pass;
  /** Start/end distances. */
  float dist_start;
  float dist_end;

  /** Ref object */
  struct Object *object;
} WeightProxPenModData;

typedef struct WeightAnglePenModData {
  PenModData mod;
  /** Target vertexgroup name, MAX_VGROUP_NAME. */
  char target_vgname[64];
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Optional vertexgroup filter name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Minimum valid weight (clamp value). */
  float min_weight;
  /** Custom index for passes. */
  int layer_pass;
  /** Axis. */
  short axis;
  /** Space (Local/World). */
  short space;
  /** Angle */
  float angle;
} WeightAnglePenModData;

typedef enum eWeightPen_Flag {
  PEN_WEIGHT_INVERT_LAYER = (1 << 0),
  PEN_WEIGHT_INVERT_PASS = (1 << 1),
  PEN_WEIGHT_INVERT_VGROUP = (1 << 2),
  PEN_WEIGHT_INVERT_LAYERPASS = (1 << 3),
  PEN_WEIGHT_INVERT_MATERIAL = (1 << 4),
  PEN_WEIGHT_MULTIPLY_DATA = (1 << 5),
  PEN_WEIGHT_INVERT_OUTPUT = (1 << 6),
} eWeightPen_Flag;

typedef enum ePenModSpace {
  PEN_SPACE_LOCAL = 0,
  PEN_SPACE_WORLD = 1,
} ePenModSpace;

typedef enum eLineartPenModSource {
  LRT_SOURCE_COLLECTION = 0,
  LRT_SOURCE_OBJECT = 1,
  LRT_SOURCE_SCENE = 2,
} eLineartPenModSource;

/* This enum is for modifier internal state only. */
typedef enum eLineArtPenModFlags {
  /* These two moved to #eLineartMainFlags to keep consistent with flag variable purpose. */
  /* LRT_PEN_INVERT_SOURCE_VGROUP = (1 << 0), */
  /* LRT_PEN_MATCH_OUTPUT_VGROUP = (1 << 1), */
  LRT_PEN_BINARY_WEIGHTS = (1 << 2) /* Deprecated, this is removed for lack of use case. */,
  LRT_PEN_IS_BAKED = (1 << 3),
  LRT_PEN_USE_CACHE = (1 << 4),
  LRT_PEN_OFFSET_TOWARDS_CUSTOM_CAMERA = (1 << 5),
  LRT_PEN_INVERT_COLLECTION = (1 << 6),
} eLineArtPenModFlags;

typedef enum eLineartPenMaskSwitches {
  LRT_PEN_MATERIAL_MASK_ENABLE = (1 << 0),
  /** When set, material mask bit comparisons are done with bit wise "AND" instead of "OR". */
  LRT_PEN_MATERIAL_MASK_MATCH = (1 << 1),
  LRT_PEN_INTERSECTION_MATCH = (1 << 2),
} eLineartPenMaskSwitches;

struct LineartCache;

struct LineartCache;

typedef struct LineartPenModData {
  PenModData mod;

  /** Line type enable flags, bits in #eLineartEdgeFlag. */
  short edge_types;

  /** Object or Collection, from #eLineartPenModSource. */
  char source_type;

  char use_multiple_levels;
  short level_start;
  short level_end;

  struct Object *source_camera;

  struct Object *source_object;
  struct Collection *source_collection;

  struct Material *target_material;
  char target_layer[64];

  /* These two variables are to pass on vertex group information from mesh to strokes.
   * `vgname` specifies which vertex groups our strokes from source_vertex_group will go to. */
  char source_vertex_group[64];
  char vgname[64];

  /* Camera focal length is divided by `1 + overscan`, before calculation, which give a wider FOV,
   * this doesn't change coordinates range internally (-1, 1), but makes the calculated frame
   * bigger than actual output. This is for the easier shifting calculation. A value of 0.5 means
   * the "internal" focal length become 2/3 of the actual camera.*/
  float overscan;

  float opacity;
  short thickness;

  unsigned char mask_switches; /* #eLineartPenMaskSwitches */
  unsigned char material_mask_bits;
  unsigned char intersection_mask;

  char _pad[3];

  /** `0..1` range for cosine angle */
  float crease_threshold;

  /** `0..PI` angle, for splitting strokes at sharp points. */
  float angle_splitting_threshold;

  /** Strength for smoothing jagged chains. */
  float chain_smooth_tolerance;

  /* CPU mode */
  float chaining_image_threshold;

  /* Ported from SceneLineArt flags. */
  int calculation_flags;

  /* #eLineArtGPencilModifierFlags, modifier internal state. */
  int flags;

  /* Move strokes towards camera to avoid clipping while preserve depth for the viewport. */
  float stroke_depth_offset;

  /* Runtime data. */

  /* Because we can potentially only compute features lines once per modifier stack (Use Cache), we
   * need to have these override values to ensure that we have the data we need is computed and
   * stored in the cache. */
  char level_start_override;
  char level_end_override;
  short edge_types_override;

  struct LineartCache *cache;
  /* Keep a pointer to the render buffer so we can call destroy from ModifierData. */
  struct LineartRenderBuffer *render_buffer_ptr;

} LineartGpencilModifierData;

typedef struct ShrinkwrapPenModData {
  PenModData mod;
  /** Shrink target. */
  struct Object *target;
  /** Additional shrink target. */
  struct Object *aux_target;
  /** Material for filtering. */
  struct Material *material;
  /** Layer name. */
  char layername[64];
  /** Optional vertexgroup filter name, MAX_VGROUP_NAME. */
  char vgname[64];
  /** Custom index for passes. */
  int pass_index;
  /** Flags. */
  int flag;
  /** Custom index for passes. */
  int layer_pass;
  /** Distance offset to keep from mesh/projection point. */
  float keep_dist;
  /** Shrink type projection. */
  short shrink_type;
  /** Shrink options. */
  char shrink_opts;
  /** Shrink to surface mode. */
  char shrink_mode;
  /** Limit the projection ray cast. */
  float proj_limit;
  /** Axis to project over. */
  char proj_axis;

  /** If using projection over vertex normal this controls the level of subsurface that must be
   * done before getting the vertex coordinates and normal */
  char subsurf_levels;
  char _pad[6];
  /** Factor of smooth. */
  float smooth_factor;
  /** How many times apply smooth. */
  int smooth_step;

  /** Runtime only. */
  struct ShrinkwrapTreeData *cache_data;
} ShrinkwrapPenModData;

typedef enum eShrinkwrapPen_Flag {
  PEN_SHRINKWRAP_INVERT_LAYER = (1 << 0),
  PEN_SHRINKWRAP_INVERT_PASS = (1 << 1),
  PEN_SHRINKWRAP_INVERT_LAYERPASS = (1 << 3),
  PEN_SHRINKWRAP_INVERT_MATERIAL = (1 << 4),
  /* Keep next bit as is to be equals to mesh modifier flag to reuse functions. */
  PEN_SHRINKWRAP_INVERT_VGROUP = (1 << 6),
} eShrinkwrapPen_Flag;

#ifdef __cplusplus
}
#endif
