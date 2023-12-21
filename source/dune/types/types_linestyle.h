#pragma once

#include "types_id.h"
#include "types_list.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_MTEX
#  define MAX_MTEX 18
#endif

/* texco (also in types_material.h) */
#define TEXCO_STROKE 16 /* actually its UV */

struct AnimData;
struct ColorBand;
struct CurveMapping;
struct MTex;
struct Ob;
struct NodeTree;

typedef struct LineStyleMod {
  struct LineStyleMod *next, *prev;

  /* MAX_NAME. */
  char name[64];
  int type;
  float influence;
  int flags;
  int blend;
} LineStyleMod;

/* LineStyleModifier::type */
#define LS_MOD_ALONG_STROKE 1
#define LS_MOD_DISTANCE_FROM_CAMERA 2
#define LS_MOD_DISTANCE_FROM_OBJECT 3
#define LS_MOD_MATERIAL 4
#define LS_MOD_SAMPLING 5
#define LS_MOD_BEZIER_CURVE 6
#define LS_MOD_SINUS_DISPLACEMENT 7
#define LS_MOD_SPATIAL_NOISE 8
#define LS_MOD_PERLIN_NOISE_1D 9
#define LS_MOD_PERLIN_NOISE_2D 10
#define LS_MOD_BACKBONE_STRETCHER 11
#define LS_MOD_TIP_REMOVER 12
#define LS_MOD_CALLIGRAPHY 13
#define LS_MOD_POLYGONIZATION 14
#define LS_MOD_GUIDING_LINES 15
#define LS_MOD_BLUEPRINT 16
#define LS_MOD_2D_OFFSET 17
#define LS_MOD_2D_TRANSFORM 18
#define LS_MOD_TANGENT 19
#define LS_MOD_NOISE 20
#define LS_MOD_CREASE_ANGLE 21
#define LS_MOD_SIMPLIFICATION 22
#define LS_MOD_CURVATURE_3D 23
#define LS_MOD_NUM 24

/* LineStyleMod::flags */
#define LS_MOD_ENABLED 1
#define LS_MOD_EXPANDED 2

/* flags (for color) */
#define LS_MOD_USE_RAMP 1

/* flags (for alpha & thickness) */
#define LS_MOD_USE_CURVE 1
#define LS_MOD_INVERT 2

/* flags (for asymmetric thickness application) */
#define LS_THICKNESS_ASYMMETRIC 1

/* blend (for alpha & thickness) */
#define LS_VAL_BLEND 0
#define LS_VAL_ADD 1
#define LS_VAL_MULT 2
#define LS_VAL_SUB 3
#define LS_VAL_DIV 4
#define LS_VAL_DIFF 5
#define LS_VAL_MIN 6
#define LS_VAL_MAX 7

/* Along Stroke mods */
typedef struct LineStyleColorModAlongStroke {
  struct LineStyleMod mod;

  struct ColorBand *color_ramp;
} LineStyleColorModAlongStroke;

typedef struct LineStyleAlphaModAlongStroke {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  char _pad[4];
} LineStyleAlphaModAlongStroke;

typedef struct LineStyleThicknessModAlongStroke {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  float val_min, val_max;
  char _pad[4];
} LineStyleThicknessModAlongStroke;

/* Distance from Camera mods */
typedef struct LineStyleColorModDistanceFromCamera {
  struct LineStyleMod mod;

  struct ColorBand *color_ramp;
  float range_min, range_max;
} LineStyleColorModDistanceFromCamera;

typedef struct LineStyleAlphaModDistanceFromCamera {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  float range_min, range_max;
  char _pad[4];
} LineStyleAlphaModDistanceFromCamera;

typedef struct LineStyleThicknessModDistanceFromCamera {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  float range_min, range_max;
  float val_min, val_max;
  char _pad[4];
} LineStyleThicknessModDistanceFromCamera;

/* Distance from Ob mods */
typedef struct LineStyleColorModDistanceFromOb {
  struct LineStyleMod mod;

  struct Ob *target;
  struct ColorBand *color_ramp;
  float range_min, range_max;
} LineStyleColorModDistanceFromOb;

typedef struct LineStyleAlphaModDistanceFromOb {
  struct LineStyleMod mod;

  struct Ob *target;
  struct CurveMapping *curve;
  int flags;
  float range_min, range_max;
  char _pad[4];
} LineStyleAlphaModDistanceFromOb;

typedef struct LineStyleThicknessModDistanceFromOb {
  struct LineStyleMod mod;

  struct Ob *target;
  struct CurveMapping *curve;
  int flags;
  float range_min, range_max;
  float val_min, val_max;
  char _pad[4];
} LineStyleThicknessModDistanceFromOb;

/* 3D curvature mods */
typedef struct LineStyleColorModCurvature_3D {
  struct LineStyleMod mod;

  float min_curvature, max_curvature;
  struct ColorBand *color_ramp;
  float range_min, range_max;
} LineStyleColorModCurvature_3D;

typedef struct LineStyleAlphaModCurvature_3D {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  float min_curvature, max_curvature;
  char _pad[4];
} LineStyleAlphaModCurvature_3D;

typedef struct LineStyleThicknessModCurvature_3D {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  char _pad[4];
  float min_curvature, max_curvature;
  float min_thickness, max_thickness;
} LineStyleThicknessModCurvature_3D;

/* Noise mods (for color, alpha and thickness) */
typedef struct LineStyleColorModNoise {
  struct LineStyleMod mod;

  struct ColorBand *color_ramp;
  float period, amplitude;
  int seed;
  char _pad[4];
} LineStyleColorModNoise;

typedef struct LineStyleAlphaModNoise {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  float period, amplitude;
  int seed;
} LineStyleAlphaModNoise;

typedef struct LineStyleThicknessModNoise {
  struct LineStyleMod mod;

  float period, amplitude;
  int flags;
  int seed;
} LineStyleThicknessModNoise;

/* Crease Angle mods */

typedef struct LineStyleColorModCreaseAngle {
  struct LineStyleMod mod;

  struct ColorBand *color_ramp;
  float min_angle, max_angle;
} LineStyleColorModCreaseAngle;

typedef struct LineStyleAlphaModCreaseAngle {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  float min_angle, max_angle;
  char _pad[4];
} LineStyleAlphaModCreaseAngle;

typedef struct LineStyleThicknessModCreaseAngle {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  char _pad[4];
  float min_angle, max_angle;
  float min_thickness, max_thickness;
} LineStyleThicknessModCreaseAngle;

/* Tangent mods */
typedef struct LineStyleColorModTangent {
  struct LineStyleMod mod;

  struct ColorBand *color_ramp;
} LineStyleColorModTangent;

typedef struct LineStyleAlphaModTangent {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  char _pad[4];
} LineStyleAlphaModTangent;

typedef struct LineStyleThicknessModTangent {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  float min_thickness, max_thickness;
  char _pad[4];
} LineStyleThicknessModTangent;

/* Material mods */
/* mat_attr */
#define LS_MOD_MATERIAL_DIFF 1
#define LS_MOD_MATERIAL_DIFF_R 2
#define LS_MOD_MATERIAL_DIFF_G 3
#define LS_MOD_MATERIAL_DIFF_B 4
#define LS_MOD_MATERIAL_SPEC 5
#define LS_MOD_MATERIAL_SPEC_R 6
#define LS_MOD_MATERIAL_SPEC_G 7
#define LS_MOD_MATERIAL_SPEC_B 8
#define LS_MOD_MATERIAL_SPEC_HARD 9
#define LS_MOD_MATERIAL_ALPHA 10
#define LS_MOD_MATERIAL_LINE 11
#define LS_MOD_MATERIAL_LINE_R 12
#define LS_MOD_MATERIAL_LINE_G 13
#define LS_MOD_MATERIAL_LINE_B 14
#define LS_MOD_MATERIAL_LINE_A 15

typedef struct LineStyleColorModMaterial {
  struct LineStyleMod mod;

  struct ColorBand *color_ramp;
  int flags;
  int mat_attr;
} LineStyleColorModMaterial;

typedef struct LineStyleAlphaModMaterial {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  int mat_attr;
} LineStyleAlphaModMaterial;

typedef struct LineStyleThicknessModMaterial {
  struct LineStyleMod mod;

  struct CurveMapping *curve;
  int flags;
  float val_min, val_max;
  int mat_attr;
} LineStyleThicknessModMaterial;

/* Geometry mods */
typedef struct LineStyleGeometryModSampling {
  struct LineStyleMod mod;

  float sampling;
  char _pad[4];
} LineStyleGeometryModSampling;

typedef struct LineStyleGeometryModBezierCurve {
  struct LineStyleMod mod;

  float error;
  char _pad[4];
} LineStyleGeometryModBezierCurve;

typedef struct LineStyleGeometryModSinusDisplacement {
  struct LineStyleMod mod;

  float wavelength, amplitude, phase;
  char _pad[4];
} LineStyleGeometryModSinusDisplacement;

/* LineStyleGeometryModSpatialNoise::flags */
#define LS_MOD_SPATIAL_NOISE_SMOOTH 1
#define LS_MOD_SPATIAL_NOISE_PURERANDOM 2

typedef struct LineStyleGeometryModSpatialNoise {
  struct LineStyleMod mod;

  float amplitude, scale;
  unsigned int octaves;
  int flags;
} LineStyleGeometryModSpatialNoise;

typedef struct LineStyleGeometryModPerlinNoise1D {
  struct LineStyleMod mod;

  float frequency, amplitude;
  /* In radians. */
  float angle;
  unsigned int octaves;
  int seed;
  char _pad1[4];
} LineStyleGeometryModPerlinNoise1D;

typedef struct LineStyleGeometryModPerlinNoise2D {
  struct LineStyleMod mod;

  float frequency, amplitude;
  /* In radians. */
  float angle;
  unsigned int octaves;
  int seed;
  char _pad1[4];
} LineStyleGeometryModPerlinNoise2D;

typedef struct LineStyleGeometryModBackboneStretcher {
  struct LineStyleMod mod;

  float backbone_length;
  char _pad[4];
} LineStyleGeometryModBackboneStretcher;

typedef struct LineStyleGeometryModTipRemover {
  struct LineStyleMod mod;

  float tip_length;
  char _pad[4];
} LineStyleGeometryModTipRemover;

typedef struct LineStyleGeometryModPolygonalization {
  struct LineStyleMod mod;

  float error;
  char _pad[4];
} LineStyleGeometryModPolygonalization;

typedef struct LineStyleGeometryModGuidingLines {
  struct LineStyleMod mod;

  float offset;
  char _pad[4];
} LineStyleGeometryModGuidingLines;

/* LineStyleGeometryModBluePrintLines::shape */
#define LS_MOD_BLUEPRINT_CIRCLES 1
#define LS_MOD_BLUEPRINT_ELLIPSES 2
#define LS_MOD_BLUEPRINT_SQUARES 4

typedef struct LineStyleGeometryModBlueprint {
  struct LineStyleMod mod;

  int flags;
  unsigned int rounds;
  float backbone_length;
  unsigned int random_radius;
  unsigned int random_center;
  unsigned int random_backbone;
} LineStyleGeometryModBlueprint;

typedef struct LineStyleGeometryMod2DOffset {
  struct LineStyleMod mod;

  float start, end;
  float x, y;
} LineStyleGeometryMod2DOffset;

/* LineStyleGeometryMod2DTransform::pivot */
#define LS_MOD_2D_TRANSFORM_PIVOT_CENTER 1
#define LS_MOD_2D_TRANSFORM_PIVOT_START 2
#define LS_MOD_2D_TRANSFORM_PIVOT_END 3
#define LS_MOD_2D_TRANSFORM_PIVOT_PARAM 4
#define LS_MOD_2D_TRANSFORM_PIVOT_ABSOLUTE 5

typedef struct LineStyleGeometryMod_2DTransform {
  struct LineStyleMod mod;

  int pivot;
  float scale_x, scale_y;
  /* In radians. */
  float angle;
  float pivot_u;
  float pivot_x, pivot_y;
  char _pad[4];
} LineStyleGeometryMod2DTransform;

typedef struct LineStyleGeometryMod_Simplification {
  struct LineStyleMod mod;

  float tolerance;
  char _pad[4];
} LineStyleGeometryModSimplification;

/* Calligraphic thickness mod */
typedef struct LineStyleThicknessModCalligraphy {
  struct LineStyleMod mod;

  float min_thickness, max_thickness;
  /* In radians. */
  float orientation;
  char _pad[4];
} LineStyleThicknessMod_Calligraphy;

/* FreestyleLineStyle::pnl */
#define LS_PNL_STROKES 1
#define LS_PNL_COLOR 2
#define LS_PNL_ALPHA 3
#define LS_PNL_THICKNESS 4
#define LS_PNL_GEOMETRY 5
#define LS_PNL_TEXTURE 6
#define LS_PNL_MISC 7

/* FreestyleLineStyle::flag */
#define LS_DS_EXPAND (1 << 0) /* for animation editors */
#define LS_SAME_OB (1 << 1)
#define LS_DASHED_LINE (1 << 2)
#define LS_MATERIAL_BOUNDARY (1 << 3)
#define LS_MIN_2D_LENGTH (1 << 4)
#define LS_MAX_2D_LENGTH (1 << 5)
#define LS_NO_CHAINING (1 << 6)
#define LS_MIN_2D_ANGLE (1 << 7)
#define LS_MAX_2D_ANGLE (1 << 8)
#define LS_SPLIT_LENGTH (1 << 9)
#define LS_SPLIT_PATTERN (1 << 10)
#define LS_NO_SORTING (1 << 11)
#define LS_REVERSE_ORDER (1 << 12) /* for sorting */
#define LS_TEXTURE (1 << 13)
#define LS_CHAIN_COUNT (1 << 14)

/* FreestyleLineStyle::chaining */
#define LS_CHAINING_PLAIN 1
#define LS_CHAINING_SKETCHY 2

/* FreestyleLineStyle::caps */
#define LS_CAPS_BUTT 1
#define LS_CAPS_ROUND 2
#define LS_CAPS_SQUARE 3

/* FreestyleLineStyle::thickness_position */
#define LS_THICKNESS_CENTER 1
#define LS_THICKNESS_INSIDE 2
#define LS_THICKNESS_OUTSIDE 3
#define LS_THICKNESS_RELATIVE 4 /* thickness_ratio is used */

/* FreestyleLineStyle::sort_key */
#define LS_SORT_KEY_DISTANCE_FROM_CAMERA 1
#define LS_SORT_KEY_2D_LENGTH 2
#define LS_SORT_KEY_PROJECTED_X 3
#define LS_SORT_KEY_PROJECTED_Y 4

/* FreestyleLineStyle::integration_type */
#define LS_INTEGRATION_MEAN 1
#define LS_INTEGRATION_MIN 2
#define LS_INTEGRATION_MAX 3
#define LS_INTEGRATION_FIRST 4
#define LS_INTEGRATION_LAST 5

typedef struct FreestyleLineStyle {
  Id id;
  struct AnimData *adt;

  float r, g, b, alpha;
  float thickness;
  int thickness_position;
  float thickness_ratio;
  int flag, caps;
  int chaining;
  unsigned int rounds;
  float split_length;
  /* In radians, for splitting. */
  float min_angle, max_angle;
  float min_length, max_length;
  unsigned int chain_count;
  unsigned short split_dash1, split_gap1;
  unsigned short split_dash2, split_gap2;
  unsigned short split_dash3, split_gap3;
  int sort_key, integration_type;
  float texstep;
  short texact, pr_texture;
  short use_nodes;
  char _pad[6];
  unsigned short dash1, gap1, dash2, gap2, dash3, gap3;
  /* For UI. */
  int pnl;
  /* MAX_MTEX. */
  struct MTex *mtex[18];
  /* nodes */
  struct NodeTree *nodetree;

  List color_mods;
  List alpha_mods;
  List thickness_mods;
  List geometry_mods;
} FreestyleLineStyle;

#ifdef __cplusplus
}
#endif
