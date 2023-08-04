#include <float.h>
#include <limits.h>
#include <stdlib.h>

#include "types_armature.h"
#include "types_brush.h"
#include "types_cachefile.h"
#include "types_pen_mod.h"
#include "types_pen.h"
#include "types_mesh.h"
#include "typed_mod.h"
#include "types_object_force.h"
#include "types_object.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lib_math.h"
#include "lib_rand.h"
#include "lib_string_utils.h"

#include "lang.h"

#include "dune_animsys.h"
#include "dune_data_transfer.h"
#include "dune_dynamicpaint.h"
#include "dune_effect.h"
#include "dune_fluid.h" /* For dune_fluid_mod_free & dune_fluid_mod_create_type_data */
#include "dune_mesh_mapping.h"
#include "dune_mesh_remap.h"
#include "dune_multires.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_api.h"
#include "wm_types.h"

const EnumPropItem api_enum_object_pen_mod_type_items[] = {
    {0, "", 0, N_("Modify"), ""},
    {ePenModType_Texture,
     "PEN_TEXTURE",
     ICON_MOD_UVPROJECT,
     "Texture Mapping",
     "Change stroke uv texture values"},
    {ePenModType_Time, "P_TIME", ICON_MOD_TIME, "Time Offset", "Offset keyframes"},
    {ePenModType_WeightAngle,
     "PEN_WEIGHT_ANGLE",
     ICON_MOD_VERTEX_WEIGHT,
     "Vertex Weight Angle",
     "Generate Vertex Weights base on stroke angle"},
    {ePenModType_WeightProximity,
     "PEN_WEIGHT_PROXIMITY",
     ICON_MOD_VERTEX_WEIGHT,
     "Vertex Weight Proximity",
     "Generate Vertex Weights base on distance to object"},
    {0, "", 0, N_("Generate"), ""},
    {ePenModType_Array,
     "PEN_ARRAY",
     ICON_MOD_ARRAY,
     "Array",
     "Create array of duplicate instances"},
    {ePenModType_Build,
     "PEN_BUILD",
     ICON_MOD_BUILD,
     "Build",
     "Create duplication of strokes"},
    {ePenModType_Dash,
     "PEN_DASH",
     ICON_MOD_DASH,
     "Dot Dash",
     "Generate dot-dash styled strokes"},
    {ePenModType_Length,
     "PEN_LENGTH",
     ICON_MOD_LENGTH,
     "Length",
     "Extend or shrink strokes"},
    {ePenModType_Lineart,
     "PEN_LINEART",
     ICON_MOD_LINEART,
     "Line Art",
     "Generate line art strokes from selected source"},
    {ePenModType_Mirror,
     "PEN_MIRROR",
     ICON_MOD_MIRROR,
     "Mirror",
     "Duplicate strokes like a mirror"},
    {ePenModType_Multiply,
     "PEN_MULTIPLY",
     ICON_PEN_MULTIFRAME_EDITING,
     "Multiple Strokes",
     "Produce multiple strokes along one stroke"},
    {ePenModType_Simplify,
     "PEN_SIMPLIFY",
     ICON_MOD_SIMPLIFY,
     "Simplify",
     "Simplify stroke reducing number of points"},
    {ePenModType_Subdiv,
     "PEN_SUBDIV",
     ICON_MOD_SUBSURF,
     "Subdivide",
     "Subdivide stroke adding more control points"},
    {0, "", 0, N_("Deform"), ""},
    {ePenModType_Armature,
     "PEN_ARMATURE",
     ICON_MOD_ARMATURE,
     "Armature",
     "Deform stroke points using armature object"},
    {ePenModType_Hook,
     PEN_HOOK",
     ICON_HOOK,
     "Hook",
     "Deform stroke points using objects"},
    {ePenModType_Lattice,
     "PEN_LATTICE",
     ICON_MOD_LATTICE,
     "Lattice",
     "Deform strokes using lattice"},
    {ePenModType_Noise, "PEN_NOISE", ICON_MOD_NOISE, "Noise", "Add noise to strokes"},
    {ePenModType_Offset,
     "PEN_OFFSET",
     ICON_MOD_OFFSET,
     "Offset",
     "Change stroke location, rotation or scale"},
     {ePenModType_Shrinkwrap,
     "SHRINKWRAP",
     ICON_MOD_SHRINKWRAP,
     "Shrinkwrap",
     "Project the shape onto another object"},
    {ePenModType_Smooth, "PEN_SMOOTH", ICON_MOD_SMOOTH, "Smooth", "Smooth stroke"},
    {ePenModType_Thick,
     "PEN_THICK",
     ICON_MOD_THICKNESS,
     "Thickness",
     "Change stroke thickness"},
    {0, "", 0, N_("Color"), ""},
    {ePenModType_Color,
     "PEN_COLOR",
     ICON_MOD_HUE_SATURATION,
     "Hue/Saturation",
     "Apply changes to stroke colors"},
    {ePenModType_Opacity,
     "PEN_OPACITY",
     ICON_MOD_OPACITY,
     "Opacity",
     "Opacity of the strokes"},
    {ePenModType_Tint, "PEN_TINT", ICON_MOD_TINT, "Tint", "Tint strokes with new color"},
    {0, NULL, 0, NULL, NULL},
};

#ifndef API_RUNTIME
static const EnumPropItem mod_modify_color_items[] = {
    {PEN_MODIFY_COLOR_BOTH, "BOTH", 0, "Stroke & Fill", "Modify fill and stroke colors"},
    {PEN_MODIFY_COLOR_STROKE, "STROKE", 0, "Stroke", "Modify stroke color only"},
    {PEN_MODIFY_COLOR_FILL, "FILL", 0, "Fill", "Modify fill color only"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem mod_modify_opacity_items[] = {
    {PEN_MODIFY_COLOR_BOTH, "BOTH", 0, "Stroke & Fill", "Modify fill and stroke colors"},
    {PEN_MODIFY_COLOR_STROKE, "STROKE", 0, "Stroke", "Modify stroke color only"},
    {PEN_MODIFY_COLOR_FILL, "FILL", 0, "Fill", "Modify fill color only"},
    {PEN_MODIFY_COLOR_HARDNESS, "HARDNESS", 0, "Hardness", "Modify stroke hardness"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem mod_penhook_falloff_items[] = {
    {ePenHook_Falloff_None, "NONE", 0, "No Falloff", ""},
    {ePenHook_Falloff_Curve, "CURVE", 0, "Curve", ""},
    {ePenHook_Falloff_Smooth, "SMOOTH", ICON_SMOOTHCURVE, "Smooth", ""},
    {ePenHook_Falloff_Sphere, "SPHERE", ICON_SPHERECURVE, "Sphere", ""},
    {ePenHook_Falloff_Root, "ROOT", ICON_ROOTCURVE, "Root", ""},
    {ePenHook_Falloff_InvSquare, "INVERSE_SQUARE", ICON_ROOTCURVE, "Inverse Square", ""},
    {ePenHook_Falloff_Sharp, "SHARP", ICON_SHARPCURVE, "Sharp", ""},
    {ePenHook_Falloff_Linear, "LINEAR", ICON_LINCURVE, "Linear", ""},
    {ePenHook_Falloff_Const, "CONSTANT", ICON_NOCURVE, "Constant", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_time_mode_items[] = {
    {PEN_TIME_MODE_NORMAL, "NORMAL", 0, "Regular", "Apply offset in usual animation direction"},
    {PEN_TIME_MODE_REVERSE, "REVERSE", 0, "Reverse", "Apply offset in reverse animation direction"},
    {PEN_TIME_MODE_FIX, "FIX", 0, "Fixed Frame", "Keep frame and do not change with time"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem pen_subdivision_type_items[] = {
    {PEN_SUBDIV_CATMULL, "CATMULL_CLARK", 0, "Catmull-Clark", ""},
    {PEN_SUBDIV_SIMPLE, "SIMPLE", 0, "Simple", ""},
    {0, NULL, 0, NULL, NULL},
};
static const EnumPropItem pen_tint_type_items[] = {
    {PEN_TINT_UNIFORM, "UNIFORM", 0, "Uniform", ""},
    {PEN_TINT_GRADIENT, "GRADIENT", 0, "Gradient", ""},
    {0, NULL, 0, NULL, NULL},
};
static const EnumPropItem pen_length_mode_items[] = {
    {PEN_LENGTH_RELATIVE, "RELATIVE", 0, "Relative", "Length in ratio to the stroke's length"},
    {PEN_LENGTH_ABSOLUTE, "ABSOLUTE", 0, "Absolute", "Length in geometry space"},
    {0, NULL, 0, NULL, NULL},
};
#endif

#ifdef API_RUNTIME

#  include "types_curve.h"
#  include "types_fluid.h"
#  include "types_material.h"
#  include "types_particle.h"

#  include "dune_cachefile.h"
#  include "dune_cxt.h"
#  include "dune_pen.h"
#  include "dune_pen_mod.h"
#  include "dune_object.h"

#  include "graph.h"
#  include "graph_build.h"

static ApiStruct *api_PenMod_refine(struct ApiPtr *ptr)
{
  PenModData *md = (PenModData *)ptr->data;

  switch ((PenModType)md->type) {
    case ePenModTypeNoise:
      return &ApiNoisePenMod;
    case ePenModType_Subdiv:
      return &ApiSubdivPenMod;
    case ePenModType_Simplify:
      return &ApiSimplifyPenMod;
    case ePenModType_Thick:
      return &Api_ThickPenMod;
    case ePenModType_Tint:
      return &ApiTintPenMod;
    case ePenModType_T
      return;
    case ePenModType_WeightProximity:
      return &ApiWeightProxPenMod;
    case ePenModType_WeightAngle:
      return &ApiWeightAnglePenMod;
    case ePenModType_Color:
      return &ApiColorPenMod;
    case ePenModType_Array:
      return PenModType;
    case ePenModType_Build:
      return &ApiBuildPenMod;
    case ePenModType_Opacity:
      return &ApiOpacityPenMod;
    case ePenModType_Lattice:
      return &ApiLatticePenMod;
    case ePenModType_Length:
      return &ApiLengthPenMod;
    case ePenModType_Mirror:
      return &ApiMirrorPenMod;
    case ePenModType_Shrinkwrap:
      return &ApiShrinkwrapPenMod;
    case ePenModType_Smooth:
      return &ApiSmoothPenMod;
    case ePenModType_Hook:
      return &ApiHookPenMod;
    case ePenModType_Offset:
      return &ApiOffsetPenMod;
    case ePenModType_Armature:
      return &ApiArmaturePenMod;
    case ePenModType_Multiply:
      return &ApiMultiplyPenMod;
    case ePenModType_Texture:
      return &ApiTexturePenMod;
    case ePenModType_Lineart:
      return &ApiLineartPenMod;
    case ePenModType_Dash:
      return &ApiDashPenlModData;
      /* Default */
    case ePenModType_None:
    case NUM_PEN_MOD_TYPES:
      return &ApiPenMod;
  }

  return &ApiPenMod;
}

static void api_PenMod_name_set(ApiPtr *ptr, const char *value)
{
  PenModData *gmd = ptr->data;
  char oldname[sizeof(gmd->name)];

  /* Make a copy of the old name first. */
  lib_strncpy(oldname, gmd->name, sizeof(gmd->name));

  /* Copy the new name into the name slot. */
  lib_strncpy_utf8(gmd->name, value, sizeof(gmd->name));

  /* Make sure the name is truly unique. */
  if (ptr->owner_id) {
    Object *ob = (Object *)ptr->owner_id;
    dune_pen_mod_unique_name(&ob->pen_mods, gmd);
  }

  /* Fix all the animation data which may link to this. */
  dune_animdata_fix_paths_rename_all(NULL, "pen_mods", oldname, gmd->name);
}

static char *api_PenMod_path(ApiPtr *ptr)
{
  PenModData *gmd = ptr->data;
  char name_esc[sizeof(gmd->name) * 2];

  lib_str_escape(name_esc, gmd->name, sizeof(name_esc));
  return lib_sprintfn("pen_mods[\"%s\"]", name_esc);
}

static void api_PenMod_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  graph_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
  wm_main_add_notifier(NC_OBJECT | ND_MOD, ptr->owner_id);
}

static void api_PenMod_dep_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  api_PenMod_update(main, scene, ptr);
  graph_relations_tag_update(main);
}

/* Vertex Groups */
#  define API_PEN_MOD_VGROUP_NAME_SET(_type, _prop) \
    static void api_##_type##PenMod_##_prop##_set(ApiPtr *ptr, const char *value) \
    { \
      _type##PenModData *tmd = (_type##PenModData *)ptr->data; \
      api_object_vgroup_name_set(ptr, value, tmd->_prop, sizeof(tmd->_prop)); \
    }

API_PEN_MOD_VGROUP_NAME_SET(Noise, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Thick, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Opacity, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Lattice, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Smooth, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Hook, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Offset, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Armature, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Texture, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Tint, vgname);
API_PEN_MOD_VGROUP_NAME_SET(WeightProx, target_vgname);
API_PEN_MOD_VGROUP_NAME_SET(WeightProx, vgname);
API_PEN_MOD_VGROUP_NAME_SET(WeightAngle, target_vgname);
API_PEN_MOD_VGROUP_NAME_SET(WeightAngle, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Lineart, vgname);
API_PEN_MOD_VGROUP_NAME_SET(Shrinkwrap, vgname);

#  undef API_PEN_MOD_VGROUP_NAME_SET

/* Objects */
static void pen_mod_object_set(Object *self,
                               Object **ob_p,
                               int type,
                               ApiPtr value)
{
  Object *ob = value.data;

  if (!self || ob != self) {
    if (!ob || type == OB_EMPTY || ob->type == type) {
      id_lib_extern((Id *)ob);
      *ob_p = ob;
    }
  }
}

#  define API_PEN_MOD_OBJECT_SET(_type, _prop, _obtype) \
    static void api_##_type##PenMod_##_prop##_set( \
        ApiPtr *ptr, ApiPtr value, struct ReportList *UNUSED(reports)) \
    { \
      _type##PenModData *tmd = (_type##PenModeData *)ptr->data; \
      pen_mod_object_set((Object *)ptr->owner_id, &tmd->_prop, _obtype, value); \
    }

API_PEN_MOD_OBJECT_SET(Armature, object, OB_ARMATURE);
API_PEN_MOD_OBJECT_SET(Lattice, object, OB_LATTICE);
API_PEN_MOD_OBJECT_SET(Mirror, object, OB_EMPTY);
API_PEN_MOD_OBJECT_SET(WeightProx, object, OB_EMPTY);
API_PEN_MOD_OBJECT_SET(Shrinkwrap, target, OB_MESH);
API_PEN_MOD_OBJECT_SET(Shrinkwrap, aux_target, OB_MESH);

#  undef API_PEN_MOD_OBJECT_SET

static void api_HookPenMod_object_set(ApiPtr *ptr,
                                      ApiPtr value,
                                      struct ReportList *UNUSED(reports))
{
  HookPenModData *hmd = ptr->data;
  Object *ob = (Object *)value.data;

  hmd->object = ob;
  id_lib_extern((Id *)ob);
  dune_object_mod_pen_hook_reset(ob, hmd);
}

static void api_TintPenMod_object_set(ApiPtr *ptr,
                                      ApiPtr value,
                                      struct ReportList *UNUSED(reports))
{
  TintPenModData *hmd = ptr->data;
  Object *ob = (Object *)value.data;

  hmd->object = ob;
  id_lib_extern((Id *)ob);
}

static void api_TimeMod_start_frame_set(ApiPtr *ptr, int value)
{
  TimePenModData *tmd = ptr->data;
  CLAMP(value, MINFRAME, MAXFRAME);
  tmd->sfra = value;

  if (tmd->sfra >= tmd->efra) {
    tmd->efra = MIN2(tmd->sfra, MAXFRAME);
  }
}

static void api_TimeMod_end_frame_set(ApiPtr *ptr, int value)
{
  TimePenModData *tmd = ptr->data;
  CLAMP(value, MINFRAME, MAXFRAME);
  tmd->efra = value;

  if (tmd->sfra >= tmd->efra) {
    tmd->sfra = MAX2(tmd->efra, MINFRAME);
  }
}

static void api_PenOpacity_range(
    ApiPtr *ptr, float *min, float *max, float *softmin, float *softmax)
{
  OpacityPenModData *md = (OpacityPenModData *)ptr->data;

  *min = 0.0f;
  *softmin = 0.0f;

  *softmax = (md->flag & PEN_OPACITY_NORMALIZE) ? 1.0f : 2.0f;
  *max = *softmax;
}

static void api_PenOpacity_max_set(ApiPtr *ptr, float value)
{
  OpacityPenModData *md = (OpacityPenModData *)ptr->data;

  md->factor = value;
  if (md->flag & PEN_OPACITY_NORMALIZE) {
    if (md->factor > 1.0f) {
      md->factor = 1.0f;
    }
  }
}

static void api_PenMod_opacity_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  OpacityPenModData *md = (OpacityPenData *)ptr->data;
  if (md->flag & PEN_OPACITY_NORMALIZE) {
    if (md->factor > 1.0f) {
      md->factor = 1.0f;
    }
  }

  api_PenMod_update(main, scene, ptr);
}

bool api_PenMod_material_poll(ApiPtr *ptr, ApiPtr value)
{
  Object *ob = (Object *)ptr->owner_id;
  Material *ma = (Material *)value.owner_id;

  return dune_pen_object_material_index_get(ob, ma) != -1;
}

static void api_PenMod_material_set(ApiPtr *ptr,
                                    ApiPtr value,
                                    Material **ma_target,
                                    struct ReportList *reports)
{
  Object *ob = (Object *)ptr->owner_id;
  Material *ma = (Material *)value.owner_id;

  if (ma == NULL || dune_pen_object_material_index_get(ob, ma) != -1) {
    id_lib_extern((Id *)ob);
    *ma_target = ma;
  } else {
    dune_reportf(
        reports,
        RPT_ERROR,
        "Cannot assign material '%s', it has to be used by the grease pencil object already",
        ma->id.name);
  }
}

static void api_LineartPenMod_material_set(ApiPtr *ptr,
                                           ApiPtr value,
                                           struct ReportList *reports)
{
  LineartPenModData *lmd = (LineartPenModData *)ptr->data;
  Material **ma_target = &lmd->target_material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_NoisePenMod_material_set(ApiPtr *ptr,
                                         ApiPtr value,
                                         struct ReportList *reports)
{
  NoisePenModData *nmd = (NoisePenModData *)ptr->data;
  Material **ma_target = &nmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_SmoothPenMod_material_set(ApiPtr *ptr,
                                          ApiPtr value,
                                          struct ReportList *reports)
{
  SmoothPenModData *smd = (SmoothPenModData *)ptr->data;
  Material **ma_target = &smd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_SubdivPenMod_material_set(ApiPtr *ptr,
                                          ApiPtr value,
                                          struct ReportList *reports)
{
  SubdivPenModData *smd = (SubdivPenModData *)ptr->data;
  Material **ma_target = &smd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_SimplifyPenMod_material_set(ApiPtr *ptr,
                                            ApiPtr value,
                                            struct ReportList *reports)
{
  SimplifyPenModData *smd = (SimplifyPenModData *)ptr->data;
  Material **ma_target = &smd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_ThickPenMod_material_set(ApiPtr *ptr,
                                         ApiPtr value,
                                         struct ReportList *reports)
{
  ThickPenModData *tmd = (ThickPenModData *)ptr->data;
  Material **ma_target = &tmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_WeightProxPenMod_material_set(ApiPtr *ptr,
                                              ApiPtr value,
                                              struct ReportList *reports)
{
  WeightProxPenModData *tmd = (WeightProxPenModData *)ptr->data;
  Material **ma_target = &tmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_WeightAnglePenMod_material_set(ApiPtr *ptr,   
                                               ApiPtr value,
                                               struct ReportList *reports)
{
  WeightAnglePenModData *tmd = (WeightAnglePenModData *)ptr->data;
  Material **ma_target = &tmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_OffsetPenMod_material_set(ApiPtr *ptr,
                                          ApiPtr value,
                                          struct ReportList *reports)
{
  OffsetPenModData *omd = (OffsetPenModData *)ptr->data;
  Material **ma_target = &omd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_OffsetPenMod_material_set(ApiPtr *ptr,
                                          ApiPtr value,
                                          struct ReportList *reports)
{
  TintPenModData *tmd = (TintPenModData *)ptr->data;
  Material **ma_target = &tmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_ColorPenMod_material_set(ApiPtr *ptr,
                                         ApiPtr value,
                                         struct ReportList *reports)
{
  ColorPenModData *cmd = (ColorPenModData *)ptr->data;
  Material **ma_target = &cmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_ArrayPenMod_material_set(ApiPtr *ptr,
                                         ApiPtr value,
                                         struct ReportList *reports)
{
  ArrayPenModData *amd = (ArrayPenModData *)ptr->data;
  Material **ma_target = &amd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_PenMod_material_set(ApiPtr *ptr,
                                    ApiPtr value,
                                    struct ReportList *reports)
{
  OpacityPenModData *omd = (OpacityPenModData *)ptr->data;
  Material **ma_target = &omd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_PenMod_material_set(ApiPtr *ptr,
                                    ApiPtr value,
                                    struct ReportList *reports)
{
  LatticePenModData *lmd = (LatticePenModData *)ptr->data;
  Material **ma_target = &lmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_MirrorPenMod_material_set(ApiPtr *ptr,
                                          ApiPtr value,
                                          struct ReportList *reports)
{
  MirrorPenModData *mmd = (MirrorPenModData *)ptr->data;
  Material **ma_target = &mmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_HookPenMod_material_set(ApiPtr *ptr,
                                        ApiPtr value,
                                        struct ReportList *reports)
{
  HookPenModData *hmd = (HookPenModData *)ptr->data;
  Material **ma_target = &hmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_MultiplyPenMod_material_set(ApiPtr *ptr,
                                            ApiPtr value,
                                            struct ReportList *reports)
{
  MultiplyPenModData *mmd = (MultiplyPenModData *)ptr->data;
  Material **ma_target = &mmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_TexturePenMod_material_set(ApiPtr *ptr,
                                           ApiPtr value,         
                                           struct ReportList *reports)
{
  TexturePenModData *tmd = (TexturePenModData *)ptr->data;
  Material **ma_target = &tmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_ShrinkwrapPenMod_material_set(ApiPtr *ptr,
                                              ApiPtr value,
                                              struct ReportList *reports)
{
  ShrinkwrapPenModData *tmd = (ShrinkwrapPenModData *)ptr->data;
  Material **ma_target = &tmd->material;

  api_PenMod_material_set(ptr, value, ma_target, reports);
}

static void api_Lineart_start_level_set(ApiPtr *ptr, int value)
{
  LineartPenModData *lmd = (LineartPenModData *)ptr->data;

  CLAMP(value, 0, 128);
  lmd->level_start = value;
  lmd->level_end = MAX2(value, lmd->level_end);
}

static void api_Lineart_end_level_set(ApiPtr *ptr, int value)
{
  LineartPenModData *lmd = (LineartPenModData *)ptr->data;

  CLAMP(value, 0, 128);
  lmd->level_end = value;
  lmd->level_start = MIN2(value, lmd->level_start);
}

static void api_PenDash_segments_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  DashPenModData *dmd = (DashPenModData *)ptr->data;
  api_iter_array_begin(
      iter, dmd->segments, sizeof(DashPenModSegment), dmd->segments_len, false, NULL);
}

static char *api_DashPenModSegment_path(ApiPtr *ptr)
{
  DashPenModSegment *ds = (DashPenModSegment *)ptr->data;

  DashPenModData *dmd = (DashPenModData *)ds->dmd;

  lib_assert(dmd != NULL);

  char name_esc[sizeof(dmd->mod.name) * 2 + 1];

  lib_str_escape(name_esc, dmd->mod.name, sizeof(name_esc));

  return lib_sprintfn("pen_mods[\"%s\"].segments[\"%s\"]", name_esc, ds->name);
}

static bool dash_segment_name_exists_fn(void *arg, const char *name)
{
  const DashPenModData *dmd = (const DashPenModData *)arg;
  for (int i = 0; i < dmd->segments_len; i++) {
    if (STREQ(dmd->segments[i].name, name)) {
      return true;
    }
  }
  return false;
}

static void api_DashPenModSegment_name_set(ApiPtr *ptr, const char *value)
{
  DashPenModSegment *ds = ptr->data;

  char oldname[sizeof(ds->name)];
  lib_strncpy(oldname, ds->name, sizeof(ds->name));

  lib_strncpy_utf8(ds->name, value, sizeof(ds->name));

  lib_assert(ds->dmd != NULL);
  lib_uniquename_cb(
      dash_segment_name_exists_fn, ds->dmd, "Segment", '.', ds->name, sizeof(ds->name));

  char prefix[256];
  sprintf(prefix, "pen_mods[\"%s\"].segments", ds->dmd->mod.name);

  /* Fix all the animation data which may link to this. */
  dune_animdata_fix_paths_rename_all(NULL, prefix, oldname, ds->name);
}

static int api_ShrinkwrapPenMod_face_cull_get(ApiPtr *ptr)
{
  *swm = (ShrinkwrapPenModData *)ptr->data;
  return swm->shrink_opts & MOD_SHRINKWRAP_CULL_TARGET_MASK;
}

static void api_ShrinkwrapPenMod_face_cull_set(struct ApiPtr *ptr, int value)
{
  ShrinkwrapPenModData *swm = (ShrinkwrapPenModData *)ptr->data;
  swm->shrink_opts = (swm->shrink_opts & ~MOD_SHRINKWRAP_CULL_TARGET_MASK) | value;
}

#else

static void api_def_mod_pennoise(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "NoisePenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Noise Mod", "Noise effect modifier");
  api_def_struct_stype(sapi, "NoisePenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_NOISE);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_NoisePenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgname");
  api_def_prop_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  api_def_prop_string_fns(prop, NULL, NULL, "api_NoisePenMod_vgname_set");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "factor");
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 1.0, 0.1, 2);
  api_def_prop_ui_text(prop, "Offset Factor", "Amount of noise to apply");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "factor_strength", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "factor_strength");
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 1.0, 0.1, 2);
  api_def_prop_ui_text(prop, "Strength Factor", "Amount of noise to apply to opacity");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "factor_thickness", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "factor_thickness");
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 1.0, 0.1, 2);
  api_def_prop_ui_text(prop, "Thickness Factor", "Amount of noise to apply to thickness");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "factor_uvs", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "factor_uvs");
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 1.0, 0.1, 2);
  api_def_prop_ui_text(prop, "UV Factor", "Amount of noise to apply uv rotation");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_random", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_NOISE_USE_RANDOM);
  api_def_prop_ui_text(prop, "Random", "Use random values over time");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "seed", PROP_INT, PROP_UNSIGNED);
  api_def_prop_ui_text(prop, "Noise Seed", "Random seed");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "noise_scale", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "noise_scale");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "Noise Scale", "Scale the noise frequency");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "noise_offset", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "noise_offset");
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 100.0, 0.1, 3);
  api_def_prop_ui_text(prop, "Noise Offset", "Offset the noise along the strokes");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_custom_curve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_NOISE_CUSTOM_CURVE);
  api_def_prop_ui_text(
      prop, "Custom Curve", "Use a custom curve to define noise effect along the strokes");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "curve", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_intensity");
  api_def_prop_ui_text(prop, "Curve", "Custom curve to apply effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "step", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "step");
  api_def_prop_range(prop, 1, 100);
  api_def_prop_ui_text(
      prop, "Step", "Number of frames before recalculate random values again");
  api_def_prop_update(prop, 0, "api_Mod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_NOISE_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_NOISE_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_NOISE_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_vertex", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_NOISE_INVERT_VGROUP);
  api_def_prop_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_NOISE_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}
static void api_def_mod_pensmooth(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "SmoothPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Smooth Mod", "Smooth effect mod");
  apj_def_struct_stype(sapi, "SmoothPenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_SMOOTH);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_SmoothPenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgname");
  api_def_prop_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  api_def_prop_string_fns(prop, NULL, NULL, "api_SmoothPenMod_vgname_set");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "factor");
  api_def_prop_range(prop, 0, 1);
  api_def_prop_ui_text(prop, "Factor", "Amount of smooth to apply");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_edit_position", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SMOOTH_MOD_LOCATION);
  api_def_prop_ui_text(
      prop, "Affect Position", "The mod affects the position of the point");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_edit_strength", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SMOOTH_MOD_STRENGTH);
  api_def_prop_ui_text(
      prop, "Affect Strength", "The mod affects the color strength of the point");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_edit_thickness", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SMOOTH_MOD_THICKNESS);
  api_def_prop_ui_text(
      prop, "Affect Thickness", "The mod affects the thickness of the point");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_edit_uv", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SMOOTH_MOD_UV);
  api_def_prop_ui_text(
      prop, "Affect UV", "The mod affects the UV rotation factor of the point");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "step", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "step");
  api_def_prop_range(prop, 1, 10);
  api_def_prop_ui_text(
      prop, "Step", "Number of times to apply smooth (high numbers can reduce fps)");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SMOOTH_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "flag", PEN_SMOOTH_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "flag", PEN_SMOOTH_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_vertex", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SMOOTH_INVERT_VGROUP);
  api_def_prop_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenModUpdate_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SMOOTH_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_custom_curve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SMOOTH_CUSTOM_CURVE);
  api_def_prop_ui_text(
      prop, "Custom Curve", "Use a custom curve to define smooth effect along the strokes");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "curve", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_intensity");
  api_def_prop_ui_text(prop, "Curve", "Custom curve to apply effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_pensubdiv(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Pen Mod", "PenMod");
  api_def_struct_ui_text(sapi, "Subdivision Mod", "Subdivide Stroke modifier");
  api_def_struct_stype(sapi, "SubdivPenData");
  api_def_struct_ui_icon(sapi, ICON_MOD_SUBSUB);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_l_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "level", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "level");
  api_def_prop_range(prop, 0, 5);
  api_def_prop_ui_text(prop, "Level", "Number of subdivisions");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "subdivision_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "type");
  api_def_prop_enum_items(prop, pen_subdivision_type_items);
  api_def_prop_ui_text(prop, "Subdivision Type", "Select type of subdivision algorithm");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SUBDIV_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SUBDIV_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SUBDIV_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SUBDIV_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_pensimplify(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static EnumPropItem prop_pen_simplify_mode_items[] = {
      {PEN_SIMPLIFY_FIXED,
       "FIXED",
       ICON_IPO_CONSTANT,
       "Fixed",
       "Delete alternating vertices in the stroke, except extremes"},
      {PEN_SIMPLIFY_ADAPTIVE,
       "ADAPTIVE",
       ICON_IPO_EASE_IN_OUT,
       "Adaptive",
       "Use a Ramer-Douglas-Peucker algorithm to simplify the stroke preserving main shape"},
      {PEN_SIMPLIFY_SAMPLE,
       "SAMPLE",
       ICON_IPO_EASE_IN_OUT,
       "Sample",
       "Re-sample the stroke with segments of the specified length"},
      {PEN_SIMPLIFY_MERGE,
       "MERGE",
       ICON_IPO_EASE_IN_OUT,
       "Merge",
       "Simplify the stroke by merging vertices closer than a given distance"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "SimplifyPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Simplify Mod", "Simplify Stroke mod");
  api_def_struct_stype(sapi, "SimplifyPenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_SIMPLIFY);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_SimplifyPenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "factor");
  api_def_prop_range(prop, 0, 100.0);
  api_def_prop_ui_range(prop, 0, 5.0f, 1.0f, 3);
  api_def_prop_ui_text(prop, "Factor", "Factor of Simplify");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SIMPLIFY_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SIMPLIFY_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SIMPLIFY_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_SIMPLIFY_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Mode */
  prop = api_def_prop(sapi, "mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, prop_pen_simplify_mode_items);
  api_def_prop_ui_text(prop, "Mode", "How to simplify the stroke");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "step", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "step");
  api_def_prop_range(prop, 1, 50);
  api_def_prop_ui_text(prop, "Iter", "Number of times to apply simplify");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Sample */
  prop = api_def_prop(sapi, "length", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "length");
  api_def_prop_range(prop, 0, FLT_MAX);
  api_def_prop_ui_range(prop, 0, 1.0, 0.01, 3);
  api_def_prop_ui_text(prop, "Length", "Length of each segment");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "sharp_threshold", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "sharp_threshold");
  api_def_prop_range(prop, 0, M_PI);
  api_def_prop_ui_range(prop, 0, M_PI, 1.0, 1);
  api_def_prop_ui_text(
      prop, "Sharp Threshold", "Preserve corners that have sharper angle than this threshold");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Merge */
  prop = api_def_prop(sapi, "distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "distance");
  api_def_prop_range(prop, 0, FLT_MAX);
  api_def_prop_ui_range(prop, 0, 1.0, 0.01, 3);
  api_def_prop_ui_text(prop, "Distance", "Distance between points");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_penthick(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThickPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Thick Mod", "Subdivide and Smooth Stroke modifier");
  api_def_struct_stype(sapi, "ThickPenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_THICKNESS);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_ThickPenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgname");
  api_def_prop_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  api_def_prop_string_fns(prop, NULL, NULL, "api_ThickPenMod_vgname_set");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "thickness", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "thickness");
  api_def_prop_range(prop, -100, 500);
  api_def_prop_ui_text(prop, "Thickness", "Absolute thickness to apply everywhere");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "thickness_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "thickness_fac");
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 10.0, 0.1, 3);
  api_def_prop_ui_text(prop, "Thickness Factor", "Factor to multiply the thickness with");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_weight_factor", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_THICK_WEIGHT_FACTOR);
  api_def_prop_ui_text(prop, "Weighted", "Use weight to modulate effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_THICK_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_THICK_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(api, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_THICK_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_vertex", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_THICK_INVERT_VGROUP);
  api_def_prop_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  api_def_prop_update(prop, 0, "api_apenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  wpo_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_THICK_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_custom_curve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_THICK_CUSTOM_CURVE);
  api_def_prop_ui_text(
      prop, "Custom Curve", "Use a custom curve to define thickness change along the strokes");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_normalized_thickness", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_THICK_NORMALIZE);
  api_def_prop_ui_text(prop, "Uniform Thickness", "Replace the stroke thickness");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "curve", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_thickness");
  api_def_prop_ui_text(prop, "Curve", "Custom curve to apply effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_penoffset(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "OffsetPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Offset Mod", "Offset Stroke modifier");
  api_def_struct_stype(sapi, "OffsetPenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_OFFSET);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_OffsetPenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_propel_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgname");
  api_def_prop_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  api_def_prop_string_fns(prop, NULL, NULL, "api_OffsetPenMod_vgname_set");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OFFSET_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OFFSET_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OFFSET_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_vertex", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OFFSET_INVERT_VGROUP);
  api_def_prop_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OFFSET_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "location", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_float_stype(prop, NULL, "loc");
  api_def_prop_ui_text(prop, "Location", "Values for change location");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "rotation", PROP_FLOAT, PROP_EULER);
  api_def_prop_float_stype(prop, NULL, "rot");
  api_def_prop_ui_text(prop, "Rotation", "Values for changes in rotation");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "scale", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "scale");
  api_def_prop_ui_text(prop, "Scale", "Values for changes in scale");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "random_offset", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "rnd_offset");
  api_def_prop_ui_text(prop, "Random Offset", "Value for changes in location");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "random_rotation", PROP_FLOAT, PROP_EULER);
  api_def_prop_float_stype(prop, NULL, "rnd_rot");
  api_def_prop_ui_text(prop, "Random Rotation", "Value for changes in rotation");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "random_scale", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "rnd_scale");
  api_def_prop_ui_text(prop, "Scale", "Value for changes in scale");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "seed", PROP_INT, PROP_UNSIGNED);
  api_def_prop_ui_text(prop, "Seed", "Random seed");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_uniform_random_scale", PROP_BOOL, PROP_NONE);
  api_def_prope_bool_stype(prop, NULL, "flag", PEN_OFFSET_UNIFORM_RANDOM_SCALE);
  api_def_prop_ui_text(
      prop, "Uniform Scale", "Use the same random seed for each scale axis for a uniform scale");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_pentint(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* modes */
  static EnumPropItem tint_mode_types_items[] = {
      {PENPAINT_MODE_STROKE, "STROKE", 0, "Stroke", "Vertex Color affects to Stroke only"},
      {PENPAINT_MODE_FILL, "FILL", 0, "Fill", "Vertex Color affects to Fill only"},
      {PENPAINT_MODE_BOTH, "BOTH", 0, "Stroke & Fill", "Vertex Color affects to Stroke and Fill"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "TintPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Tint Mod", "Tint mod");
  api_def_struct_stype(sapi, "TintPenModData");
  api_def_struct_ui_icon(sapi, ICON_COLOR);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "object", PROP_PTR, PROP_NONE);
  api_def_prop_ui_text(prop, "Object", "Parent object to define the center of the effect");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  api_de_prop_ptr_fns(prop, NULL, "api_TintPenMod_object_set", NULL, NULL);
  api_def_prop_update(prop, 0, "api_PenMod_dep_update");

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_TintPenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_aPenMod_update");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgname");
  api_def_prop_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  api_def_prop_string_fns(prop, NULL, NULL, "api_TintPenMod_vgname_set");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype
      (prop, NULL, "flag", PEN_TINT_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TINT_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TINT_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_vertex", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TINT_INVERT_VGROUP);
  api_def_prop_ui_text(prop, "Inverse Vertex Group", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TINT_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "factor");
  api_def_prop_range(prop, 0, 2.0);
  api_def_prop_ui_range(prop, 0, 2.0, 0.1, 2);
  api_def_prop_ui_text(prop, "Strength", "Factor for tinting");
  api_def_prop_update(prop, 0, "api_pen_update");

  prop = api_def_prop(sapi, "use_weight_factor", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TINT_WEIGHT_FACTOR);
  api_def_prop_ui_text(prop, "Weighted", "Use weight to modulate effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "radius", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "radius");
  api_def_prop_range(prop, 1e-6f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001f, FLT_MAX, 1, 3);
  api_def_prop_ui_text(prop, "Radius", "Defines the maximum distance of the effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Mode type. */
  prop = api_def_prop(sapi, "vertex_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "mode");
  api_def_prop_enum_items(prop, tint_mode_types_items);
  api_def_prop_ui_text(prop, "Mode", "Defines how vertex color affect to the strokes");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_PenMode", "op_update");

  /* Type of Tint. */
  prop = api_def_prop(sapi, "tint_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "type");
  api_def_prop_enum_items(prop, pen_tint_type_items);
  api_def_prop_ui_text(prop, "Tint Type", "Select type of tinting algorithm");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Simple Color. */
  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_float_stype(prop, NULL, "rgb");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Color", "Color used for tinting");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Color band. */
  prop = api_def_prop(sapi, "colors", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "colorband");
  api_def_prop_struct_type(prop, "ColorRamp");
  api_def_prop_ui_text(prop, "Colors", "Color ramp used to define tinting colors");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_custom_curve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TINT_CUSTOM_CURVE);
  api_def_prop_ui_text(
      prop, "Custom Curve", "Use a custom curve to define vertex color effect along the strokes");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "curve", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_intensity");
  api_def_prop_ui_text(prop, "Curve", "Custom curve to apply effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_pentime(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "TimePenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Time Offset Mod", "Time offset modifier");
  api_def_struct_stype(sapi, "TimePenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_TIME);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mode");
  api_def_prop_enum_items(prop, api_enum_time_mode_items);
  api_def_prop_ui_text(prop, "Mode", "");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TIME_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TIME_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "offset", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "offset");
  api_def_prop_range(prop, SHRT_MIN, SHRT_MAX);
  api_def_prop_ui_text(
      prop, "Frame Offset", "Number of frames to offset original keyframe number or frame to fix");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "frame_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "frame_scale");
  api_def_prop_range(prop, 0.001f, 100.0f);
  api_def_prop_ui_text(prop, "Frame Scale", "Evaluation time in seconds");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "frame_start", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "sfra");
  api_def_prop_int_fns(prop, NULL, "api_TimeMod_start_frame_set", NULL);
  api_def_prop_range(prop, MINFRAME, MAXFRAME);
  api_def_prop_ui_text(prop, "Start Frame", "First frame of the range");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "frame_end", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "efra");
  api_def_prop_int_fns(prop, NULL, "api_TimeMod_end_frame_set", NULL);
  api_def_prop_range(prop, MINFRAME, MAXFRAME);
  api_def_prop_ui_text(prop, "End Frame", "Final frame of the range");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_keep_loop", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TIME_KEEP_LOOP);
  api_def_prop_ui_text(
      prop, "Keep Loop", "Retiming end frames and move to start of animation to keep loop");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_custom_frame_range", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TIME_CUSTOM_RANGE);
  api_def_prop_ui_text(
      prop, "Custom Range", "Define a custom range of frames to use in modifier");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_pencolor(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ColorPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Hue/Saturation Mod", "Change Hue/Saturation mod");
  api_def_struct_stype(sapi, "ColorPenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_TINT);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "modify_color", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, mod_modify_color_items); /* share the enum */
  api_def_prop_ui_text(prop, "Mode", "Set what colors of the stroke are affected");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_ColorPenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "hue", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_range(prop, 0.0, 1.0, 0.1, 3);
  api_def_prop_float_stype(prop, NULL, "hsv[0]");
  api_def_prop_ui_text(prop, "Hue", "Color Hue");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "saturation", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 2.0, 0.1, 3);
  api_def_prop_float_stype(prop, NULL, "hsv[1]");
  api_def_prop_ui_text(prop, "Saturation", "Color Saturation");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "value", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 2.0, 0.1, 3);
  api_def_prop_float_stype(prop, NULL, "hsv[2]");
  api_def_prop_ui_text(prop, "Value", "Color Value");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_COLOR_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_COLOR_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_COLOR_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_COLOR_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_custom_curve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_COLOR_CUSTOM_CURVE);
  api_def_prop_ui_text(
      prop, "Custom Curve", "Use a custom curve to define color effect along the strokes");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "curve", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_intensity");
  api_def_prop_ui_text(prop, "Curve", "Custom curve to apply effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_penopacity(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "OpacityPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Opacity Mod", "Opacity of Strokes modifier");
  api_def_struct_stype(sapi, "OpacityPenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_OPACITY);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "modify_color", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, mod_modify_opacity_items);
  api_def_prop_ui_text(prop, "Mode", "Set what colors of the stroke are affected");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_OpacityPenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgname");
  api_def_prop_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  api_def_prop_string_fns(prop, NULL, NULL, "api_OpacityPenMod_vgname_set");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "factor");
  api_def_prop_ui_range(prop, 0, 2.0, 0.1, 2);
  api_def_prop_float_fns(
      prop, NULL, "api_PenOpacity_max_set", "api_PenOpacity_range");
  api_def_prop_ui_text(prop, "Opacity Factor", "Factor of Opacity");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "hardness", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "hardeness");
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, FLT_MAX, 0.1, 2);
  api_def_prop_ui_text(prop, "Hardness", "Factor of stroke hardness");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_weight_factor", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OPACITY_WEIGHT_FACTOR);
  api_def_prop_ui_text(prop, "Weighted", "Use weight to modulate effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OPACITY_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OPACITY_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OPACITY_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_vertex", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OPACITY_INVERT_VGROUP);
  api_def_prop_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OPACITY_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_normalized_opacity", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OPACITY_NORMALIZE);
  api_def_prop_ui_text(prop, "Uniform Opacity", "Replace the stroke opacity");
  api_def_prop_update(prop, 0, "api_PenMod_opacity_update");

  prop = api_def_prop(sapi, "use_custom_curve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_OPACITY_CUSTOM_CURVE);
  api_def_prop_ui_text(
      prop, "Custom Curve", "Use a custom curve to define opacity effect along the strokes");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "curve", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_intensity");
  api_def_prop_ui_text(prop, "Curve", "Custom curve to apply effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_penarray(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Pen Mod", "PenMod");
  api_def_struct_ui_text(sapi, "Instance Mod", "Create grid of duplicate instances");
  api_def_struct_stype(sapi, "ArrayPenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_ARRAY);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                      "api_ArrayPenMod_material_set",
                      NULL,
                      "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "count", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 1, SHRT_MAX);
  api_def_prop_ui_range(prop, 1, 50, 1, -1);
  api_def_prop_ui_text(prop, "Count", "Number of items");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Offset parameters */
  prop = api_def_prop(sapi, "offset_object", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "object");
  api_def_prop_ui_text(
      prop,
      "Object Offset",
      "Use the location and rotation of another object to determine the distance and "
      "rotational change between arrayed items");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  api_def_prop_update(prop, 0, "api_PenMod_dep_update");

  prop = api_def_prop(sapi, "constant_offset", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_float_stype(prop, NULL, "offset");
  api_def_prop_ui_text(prop, "Constant Offset", "Value for the distance between items");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "relative_offset", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "shift");
  api_def_prop_ui_text(
      prop,
      "Relative Offset",
      "The size of the geometry will determine the distance between arrayed items");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "random_offset", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "rnd_offset");
  api_def_prop_ui_text(prop, "Random Offset", "Value for changes in location");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  apu_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "random_rotation", PROP_FLOAT, PROP_EULER);
  api_def_prop_float_stype(prop, NULL, "rnd_rot");
  api_def_prop_ui_text(prop, "Random Rotation", "Value for changes in rotation");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "random_scale", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "rnd_scale");
  api_def_prop_ui_text(prop, "Scale", "Value for changes in scale");
  aoi_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "seed", PROP_INT, PROP_UNSIGNED);
  api_def_prop_ui_text(prop, "Seed", "Random seed");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "replace_material", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "mat_rpl");
  api_def_prop_range(prop, 0, SHRT_MAX);
  api_def_prop_ui_text(
      prop,
      "Material",
      "Index of the material used for generated strokes (0 keep original material)");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_ARRAY_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_ARRAY_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_ARRAY_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_ARRAY_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_constant_offset", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_ARRAY_USE_OFFSET);
  api_def_prop_ui_text(prop, "Offset", "Enable offset");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_object_offset", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_ARRAY_USE_OB_OFFSET);
  api_def_prop_ui_text(prop, "Object Offset", "Enable object offset");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_relative_offset", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_ARRAY_USE_RELATIVE);
  api_def_prop_ui_text(prop, "Shift", "Enable shift");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_uniform_random_scale", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_ARRAY_UNIFORM_RANDOM_SCALE);
  api_def_prop_ui_text(
      prop, "Uniform Scale", "Use the same random seed for each scale axis for a uniform scale");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_penbuild(DuneApi *dapi)
{
  static EnumPropItem prop_pen_build_mode_items[] = {
      {PEN_BUILD_MODE_SEQ,
       "SEQ",
       ICON_PARTICLE_POINT,
       "Seq",
       "Strokes appear/disappear one after the other, but only a single one changes at a time"},
      {PEN_BUILD_MODE_CONCURRENT,
       "CONCURRENT",
       ICON_PARTICLE_TIP,
       "Concurrent",
       "Multiple strokes appear/disappear at once"},
      {PEN_BUILD_MODE_ADDITIVE,
       "ADDITIVE",
       ICON_PARTICLE_PATH,
       "Additive",
       "Builds only new strokes (assuming 'additive' drawing)"},
      {0, NULL, 0, NULL, NULL},
  };

  static EnumPropItem prop_pen_build_transition_items[] = {
      {PEN_BUILD_TRANSITION_GROW,
       "GROW",
       0,
       "Grow",
       "Show points in the order they occur in each stroke "
       "(e.g. for animating lines being drawn)"},
      {PEN_BUILD_TRANSITION_SHRINK,
       "SHRINK",
       0,
       "Shrink",
       "Hide points from the end of each stroke to the start "
       "(e.g. for animating lines being erased)"},
      {PEN_BUILD_TRANSITION_FADE,
       "FADE",
       0,
       "Fade",
       "Hide points in the order they occur in each stroke "
       "(e.g. for animating ink fading or vanishing after getting drawn)"},
      {0, NULL, 0, NULL, NULL},
  };

  static EnumPropItem prop_pen_build_time_align_items[] = {
      {PEN_BUILD_TIMEALIGN_START,
       "START",
       0,
       "Align Start",
       "All strokes start at same time (i.e. short strokes finish earlier)"},
      {PEN_BUILD_TIMEALIGN_END,
       "END",
       0,
       "Align End",
       "All strokes end at same time (i.e. short strokes start later)"},
      {0, NULL, 0, NULL, NULL},
  };

  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BuildPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Build Mod", "Animate strokes appearing and disappearing");
  api_def_struct_stype(sapi, "BuildPenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_BUILD);

  api_define_lib_overridable(true);

  /* Mode */
  prop = api_def_prop(sapi, "mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, prop_pen_build_mode_items);
  api_def_prop_ui_text(prop, "Mode", "How many strokes are being animated at a time");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Direction */
  prop = api_def_prop(sapi, "transition", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, prop_pen_build_transition_items);
  api_def_prop_ui_text(
      prop, "Transition", "How are strokes animated (i.e. are they appearing or disappearing)");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Transition Onset Delay + Length */
  prop = api_def_prop(sapi, "start_delay", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "start_delay");
  api_def_prop_ui_text(
      prop,
      "Start Delay",
      "Number of frames after each Pen keyframe before the modifier has any effect");
  api_def_prop_range(prop, 0, MAXFRAMEF);
  api_def_prop_ui_range(prop, 0, 200, 1, -1);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "length", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "length");
  api_def_prop_ui_text(prop,
                       "Length",
                       "Maximum number of frames that the build effect can run for "
                       "(unless another Pen keyframe occurs before this time has elapsed)");
  apu_def_prop_range(prop, 1, MAXFRAMEF);
  api_def_prop_ui_range(prop, 1, 1000, 1, -1);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Concurrent Mode Settings */
  prop = api_def_prop(sapi, "concurrent_time_alignment", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "time_alignment");
  api_def_prop_enum_items(prop, prop_pen_build_time_align_items);
  api_def_prop_ui_text(
      prop, "Time Alignment", "When should strokes start to appear/disappear");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Time Limits */
  prop = api_def_prop(sapi, "use_restrict_frame_range", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BUILD_RESTRICT_TIME);
  api_def_prop_ui_text(
      prop, "Restrict Frame Range", "Only modify strokes during the specified frame range");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Use percentage */
  prop = api_def_prop(sapi, "use_percentage", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BUILD_PERCENTAGE);
  api_def_prop_ui_text(
      prop, "Restrict Visible Points", "Use a percentage factor to determine the visible points");
  api_def_prop_update(prop, 0, "api_PenMod_pdate");

  /* Percentage factor. */
  prop = api_def_prop(sapi, "percentage_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "percentage_fac");
  api_def_prop_ui_text(prop, "Factor", "Defines how much of the stroke is visible");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "frame_start", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "start_frame");
  api_def_prop_ui_text(
      prop, "Start Frame", "Start Frame (when Restrict Frame Range is enabled)");
  api_def_prop_range(prop, MINAFRAMEF, MAXFRAMEF);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "frame_end", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "end_frame");
  api_def_prop_ui_text(prop, "End Frame", "End Frame (when Restrict Frame Range is enabled)");
  api_def_prop_range(prop, MINAFRAMEF, MAXFRAMEF);
  api_def_prop_update(prop, 0, "api_PenMod_update");

  /* Filters - Layer */
  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BUILD_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_propel_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_BUILD_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_penlattice(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "LatticePenMod", "PenMod");
  api_def_struct_ui_text(
      sapi, "Lattice Mod", "Change stroke using lattice to deform modifier");
  api_def_struct_stype(sapi, "LatticePenData");
  api_def_struct_ui_icon(sapi, ICON_MOD_LATTICE);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_LatticePenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgname");
  api_def_prop_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  api_def_prop_string_fns(prop, NULL, NULL, "api_LatticePenMod_vgname_set");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_LATTICE_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_LATTICE_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_LATTICE_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");
    
  prop = api_def_prop(sapi, "invert_vertex", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_LATTICE_INVERT_VGROUP);
  api_def_prop_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_LATTICE_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "object", PROP_PTR, PROP_NONE);
  api_def_prop_ui_text(prop, "Object", "Lattice object to deform with");
  api_def_prop_ptr_fns(
      prop, NULL, "api_LatticePenMod_object_set", NULL, "api_Lattice_object_poll");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  api_def_prop_update(prop, 0, "api_PenMod_dep_update");

  prop = api_def_prop(sapi, "strength", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, -FLT_MAX, FLT_MAX);
  api_def_prop_ui_range(prop, 0, 1, 10, 2);
  api_def_prop_ui_text(prop, "Strength", "Strength of modifier effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_penmirror(DuneApi *dapi)
{
  ApiStruct *api;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MirrorPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Mirror Mod", "Create mirroring strokes");
  api_def_struct_stype(sapi, "MirrorPenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_MIRROR);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_MirrorPenMod_material_set",
                       NULL,
                        "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "object", PROP_PTR, PROP_NONE);
  api_def_prop_ui_text(prop, "Object", "Object used as center");
  api_def_prop_ptr_fns(prop, NULL, "api_MirrorPenMod_object_set", NULL, NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  api_def_prop_update(prop, 0, "api_PenMod_dep_update");

  prop = api_def_prop(sapi, "use_clip", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_CLIPPING);
  api_def_prop_ui_text(prop, "Clip", "Clip points");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_axis_x", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_AXIS_X);
  api_def_prop_ui_text(prop, "X", "Mirror the X axis");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_axis_y", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_AXIS_Y);
  api_def_prop_ui_text(prop, "Y", "Mirror the Y axis");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_axis_z", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_AXIS_Z);
  api_def_prop_ui_text(prop, "Z", "Mirror the Z axis");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_penhook(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "HookPenMod", "PenMod");
  api_def_struct_ui_text(
      sapi, "Hook Mod", "Hook mod to modify the location of stroke points");
  api_def_struct_stype(sapi, "HookPenModData");
  api_def_struct_ui_icon(sapi, ICON_HOOK);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "object", PROP_PTR, PROP_NONE);
  api_def_prop_ui_text(
      prop, "Object", "Parent Object for hook, also recalculates and clears offset");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  api_def_prop_ptr_fns(prop, NULL, "api_HookPenMod_object_set", NULL, NULL);
  api_def_prop_update(prop, 0, "api_PenMod_graph_update");

  prop = api_def_prop(sapi, "subtarget", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "subtarget");
  api_def_prop_ui_text(
      prop,
      "Sub-Target",
      "Name of Parent Bone for hook (if applicable), also recalculates and clears offset");
  api_def_prop_update(prop, 0, "api_PenMod_graph_update");

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_HookPenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgname");
  api_def_prop_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  api_def_prop_string_fns(prop, NULL, NULL, "api_HookPenMod_vgname_set");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_HOOK_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_HOOK_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_HOOK_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_vertex", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_HOOK_INVERT_VGROUP);
  api_def_prop_ui_text(prop, "Inverse Vertex Group", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_HOOK_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "strength", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "force");
  api_def_prop_range(prop, 0, 
  api_def_prop_ui_text(prop, "Strength", "Relative force of the hook");
  apo_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "falloff_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, mod_penhook_falloff_items); /* share the enum */
  api_def_prop_ui_text(prop, "Falloff Type", "");
  api_def_prop_translation_cxt(prop,
                               LANG_CXT_ID_CURVE_LEGACY); /* Abusing id_curve :/ */
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "falloff_radius", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "falloff");
  api_def_prop_range(prop, 0, FLT_MAX);
  api_def_prop_ui_range(prop, 0, 100, 100, 2);
  api_def_prop_ui_text(
      prop, "Radius", "If not zero, the distance from the hook where influence ends");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "falloff_curve", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curfalloff");
  api_def_prop_ui_text(prop, "Falloff Curve", "Custom light falloff curve");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "center", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "cent");
  api_def_prop_ui_text(prop, "Hook Center", "");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "matrix_inverse", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_float_stype(prop, NULL, "parentinv");
  api_def_prop_multi_array(prop, 2, api_matrix_dimsize_4x4);
  api_def_prop_ui_text(
      prop, "Matrix", "Reverse the transformation between this object and its target");
  api_def_prop_update(prop, NC_OBJECT | ND_TRANSFORM, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_falloff_uniform", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_HOOK_UNIFORM_SPACE);
  api_def_prop_ui_text(prop, "Uniform Falloff", "Compensate for non-uniform object scale");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_penarmature(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "PenMod", "PenMod");
  api_def_struct_ui_text(
      sapi, "Armature Mod", "Change stroke using armature to deform modifier");
  api_def_struct_stype(sapi, "ArmaturePenModData");
  api_def_struct_ui_icon(sapi, ICON_MOD_ARMATURE);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "object", PROP_PTR, PROP_NONE);
  api_def_prop_ui_text(prop, "Object", "Armature object to deform with");
  api_def_prop_ptr_fns(
      prop, NULL, "api_ArmaturePenMod_object_set", NULL, "api_Armature_object_poll");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  api_def_prop_update(prop, 0, "api_PenMod_graph_update");

  prop = api_def_prop(sapi, "use_bone_envelopes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "deformflag", ARM_DEF_ENVELOPE);
  api_def_prop_ui_text(prop, "Use Bone Envelopes", "Bind Bone envelopes to armature modifier");
  api_def_prop_update(prop, 0, "api_PenMod_graph_update");

  prop = api_def_prop(sapi, "use_vertex_groups", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "deformflag", ARM_DEF_VGROUP);
  api_def_prop_ui_text(prop, "Use Vertex Groups", "Bind vertex groups to armature modifier");
  api_def_prop_update(prop, 0, "api_PenMod_graph_update");

  prop = api_def_prop(sapi, "use_deform_preserve_volume", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "deformflag", ARM_DEF_QUATERNION);
  api_def_prop_ui_text(
      prop, "Preserve Volume", "Deform rotation interpolation with quaternions");
  api_def_prop_update(prop, 0, "api_PenMod_graph_update");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgname");
  api_def_prop_ui_text(
      prop,
      "Vertex Group",
      "Name of Vertex Group which determines influence of mod per point");
  api_def_prop_string_fns(prop, NULL, NULL, "api_ArmaturePenMod_vgname_set");
  api_def_prop_update(prop, 0, "api_PenMod_graph_update");

  prop = api_def_prop(sapi, "invert_vertex_group", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "deformflag", ARM_DEF_INVERT_VGROUP);
  api_def_prop_ui_text(prop, "Invert", "Invert vertex group influence");
  api_def_prop_update(prop, 0, "api_PenMod_graph_update");

  api_define_lib_overridable(false);
}
static void api_def_mod_penmultiply(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MultipPenMod", "PenMod");
  api_def_struct_ui_text(sapi, "Multiply Mod", "Generate multiple strokes from one stroke");
  api_def_struct_stype(sapi, "MultiplyPenModData");
  api_def_struct_ui_icon(sapi, ICON_PEN_MULTIFRAME_EDITING);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_MultiplyPenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "pass_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pass_index");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_material_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_INVERT_PASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "layer_pass", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "layer_pass");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Pass", "Layer pass index");
  api_def_prop_update(prop, 0, "api_PenMod_update");
    
  prop = api_def_prop(sapi, "invert_layer_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_MIRROR_INVERT_LAYERPASS);
  api_def_prop_ui_text(prop, "Inverse Pass", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "use_fade", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", PEN_MULTIPLY_ENABLE_FADING);
  api_def_prop_ui_text(prop, "Fade", "Fade the stroke thickness for each generated stroke");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "duplicates", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "duplications");
  api_def_prop_range(prop, 0, 999);
  api_def_prop_ui_range(prop, 1, 10, 1, 1);
  api_def_prop_ui_text(prop, "Duplicates", "How many copies of strokes be displayed");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_range(prop, -FLT_MAX, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 1.0, 0.01, 3);
  api_def_prop_ui_text(prop, "Distance", "Distance of duplications");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "offset", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_range(prop, -1, 1, 0.01, 3);
  api_def_prop_ui_text(prop, "Offset", "Offset of duplicates. -1 to 1: inner to outer");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "fading_thickness", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0, 1);
  api_def_prop_ui_text(prop, "Thickness", "Fade influence of stroke's thickness");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "fading_opacity", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0, 1);
  api_def_prop_ui_text(prop, "Opacity", "Fade influence of stroke's opacity");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "fading_center", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0, 1);
  api_def_prop_ui_text(prop, "Center", "Fade center");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  api_define_lib_overridable(false);
}

static void api_def_mod_pentexture(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem fit_type_items[] = {
      {PEN_TEX_CONSTANT_LENGTH,
       "CONSTANT_LENGTH",
       0,
       "Constant Length",
       "Keep the texture at a constant length regardless of the length of each stroke"},
      {PEN_TEX_FIT_STROKE,
       "FIT_STROKE",
       0,
       "Stroke Length",
       "Scale the texture to fit the length of each stroke"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem mode_items[] = {
      {STROKE, "STROKE", 0, "Stroke", "Manipulate only stroke texture coordinates"},
      {FILL, "FILL", 0, "Fill", "Manipulate only fill texture coordinates"},
      {STROKE_AND_FILL,
       "STROKE_AND_FILL",
       0,
       "Stroke & Fill",
       "Manipulate both stroke and fill texture coordinates"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "TexturePenMod", "PenMod");
  api_def_struct_ui_text(
      sapi, "Texture Mod", "Transform stroke texture coordinates Mod");
  api_def_struct_stype(sapi, "TexturePenModData");
  api_def_struct_ui_icon(sapo, ICON_MOD_UVPROJECT);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "layer", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "layername");
  api_def_prop_ui_text(prop, "Layer", "Layer name");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_layers", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TEX_INVERT_LAYER);
  api_def_prop_ui_text(prop, "Inverse Layers", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "material", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop,
                       NULL,
                       "api_TexturePenMod_material_set",
                       NULL,
                       "api_PenMod_material_poll");
  api_def_prop_ui_text(prop, "Material", "Material used for filtering effect");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapi, "invert_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PEN_TEX_INVERT_MATERIAL);
  api_def_prop_ui_text(prop, "Inverse Materials", "Inverse filter");
  api_def_prop_update(prop, 0, "api_PenMod_update");

  prop = api_def_prop(sapo, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_sdna(prop, NULL, "vgname");
  api_def_property_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  RNA_def_property_string_funcs(prop, NULL, NULL, "api_TexturePenMod_vgname_set");
  RNA_def_property_update(prop, 0, "api_PenMod_update");

  prop = RNA_def_property(srna, "invert_vertex", PROP_BOOL, PROP_NONE);
  RNA_def_property_bool_stype(prop, NULL, "flag", PEN_TEX_INVERT_VGROUP);
  RNA_def_prop_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  RNA_def_prop_update(prop, 0, "apo_PenMod_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_TEX_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_TEX_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "uv_offset", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "uv_offset");
  RNA_def_property_range(prop, -FLT_MAX, FLT_MAX);
  RNA_def_property_ui_range(prop, -100.0, 100.0, 0.1, 3);
  RNA_def_property_ui_text(prop, "UV Offset", "Offset value to add to stroke UVs");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "uv_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "uv_scale");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 100.0, 0.1, 3);
  RNA_def_property_ui_text(prop, "UV Scale", "Factor to scale the UVs");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  /* Rotation of Dot Texture. */
  prop = RNA_def_property(srna, "alignment_rotation", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "alignment_rotation");
  RNA_def_property_float_default(prop, 0.0f);
  RNA_def_property_range(prop, -DEG2RADF(90.0f), DEG2RADF(90.0f));
  RNA_def_property_ui_range(prop, -DEG2RADF(90.0f), DEG2RADF(90.0f), 10, 3);
  RNA_def_property_ui_text(
      prop, "Rotation", "Additional rotation applied to dots and square strokes");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "fill_rotation", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "fill_rotation");
  RNA_def_property_ui_text(prop, "Fill Rotation", "Additional rotation of the fill UV");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "fill_offset", PROP_FLOAT, PROP_COORDS);
  RNA_def_property_float_sdna(prop, NULL, "fill_offset");
  RNA_def_property_array(prop, 2);
  RNA_def_property_ui_text(prop, "Fill Offset", "Additional offset of the fill UV");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "fill_scale", PROP_FLOAT, PROP_COORDS);
  RNA_def_property_float_sdna(prop, NULL, "fill_scale");
  RNA_def_property_range(prop, 0.01f, 100.0f);
  RNA_def_property_ui_text(prop, "Fill Scale", "Additional scale of the fill UV");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "fit_method", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "fit_method");
  RNA_def_property_enum_items(prop, fit_type_items);
  RNA_def_property_ui_text(prop, "Fit Method", "");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  prop = RNA_def_property(srna, "mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "mode");
  RNA_def_property_enum_items(prop, mode_items);
  RNA_def_property_ui_text(prop, "Mode", "");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpencilweight_proximity(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "WeightProxGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Weight Modifier Proximity", "Calculate Vertex Weight dynamically");
  RNA_def_struct_sdna(srna, "WeightProxGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_VERTEX_WEIGHT);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "target_vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "target_vgname");
  RNA_def_property_ui_text(prop, "Vertex Group", "Output Vertex group");
  RNA_def_property_string_funcs(
      prop, NULL, NULL, "rna_WeightProxGpencilModifier_target_vgname_set");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_multiply", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_MULTIPLY_DATA);
  RNA_def_property_ui_text(
      prop,
      "Multiply Weights",
      "Multiply the calculated weights with the existing values in the vertex group");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_invert_output", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_OUTPUT);
  RNA_def_property_ui_text(prop, "Invert", "Invert output weight values");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_WeightProxGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "vgname");
  RNA_def_property_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_WeightProxGpencilModifier_vgname_set");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  /* Distance reference object */
  prop = RNA_def_property(srna, "object", PROP_POINTER, PROP_NONE);
  RNA_def_property_ui_text(prop, "Target Object", "Object used as distance reference");
  RNA_def_property_pointer_funcs(
      prop, NULL, "rna_WeightProxGpencilModifier_object_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  prop = RNA_def_property(srna, "distance_start", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "dist_start");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 1000.0, 1.0, 2);
  RNA_def_property_ui_text(prop, "Lowest", "Distance mapping to 0.0 weight");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "minimum_weight", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "min_weight");
  RNA_def_property_ui_text(prop, "Minimum", "Minimum value for vertex weight");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "distance_end", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "dist_end");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 1000.0, 1.0, 2);
  RNA_def_property_ui_text(prop, "Highest", "Distance mapping to 1.0 weight");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_vertex", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_VGROUP);
  RNA_def_property_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpencilweight_angle(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem axis_items[] = {
      {0, "X", 0, "X", ""},
      {1, "Y", 0, "Y", ""},
      {2, "Z", 0, "Z", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem space_items[] = {
      {GP_SPACE_LOCAL, "LOCAL", 0, "Local Space", ""},
      {GP_SPACE_WORLD, "WORLD", 0, "World Space", ""},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "WeightAngleGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Weight Modifier Angle", "Calculate Vertex Weight dynamically");
  RNA_def_struct_sdna(srna, "WeightAngleGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_VERTEX_WEIGHT);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "target_vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "target_vgname");
  RNA_def_property_ui_text(prop, "Vertex Group", "Output Vertex group");
  RNA_def_property_string_funcs(
      prop, NULL, NULL, "rna_WeightAngleGpencilModifier_target_vgname_set");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_multiply", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_MULTIPLY_DATA);
  RNA_def_property_ui_text(
      prop,
      "Multiply Weights",
      "Multiply the calculated weights with the existing values in the vertex group");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_invert_output", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_OUTPUT);
  RNA_def_property_ui_text(prop, "Invert", "Invert output weight values");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "angle", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "angle");
  RNA_def_property_ui_text(prop, "Angle", "Angle");
  RNA_def_property_range(prop, 0.0f, DEG2RAD(180.0f));
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "axis", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "axis");
  RNA_def_property_enum_items(prop, axis_items);
  RNA_def_property_ui_text(prop, "Axis", "");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "space", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "space");
  RNA_def_property_enum_items(prop, space_items);
  RNA_def_property_ui_text(prop, "Space", "Coordinates space");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_WeightAngleGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "vgname");
  RNA_def_property_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_WeightAngleGpencilModifier_vgname_set");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "minimum_weight", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "min_weight");
  RNA_def_property_ui_text(prop, "Minimum", "Minimum value for vertex weight");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_vertex", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_VGROUP);
  RNA_def_property_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_WEIGHT_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpencillineart(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem modifier_lineart_source_type[] = {
      {LRT_SOURCE_COLLECTION, "COLLECTION", 0, "Collection", ""},
      {LRT_SOURCE_OBJECT, "OBJECT", 0, "Object", ""},
      {LRT_SOURCE_SCENE, "SCENE", 0, "Scene", ""},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "LineartGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(
      srna, "Line Art Modifier", "Generate line art strokes from selected source");
  RNA_def_struct_sdna(srna, "LineartGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_LINEART);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "use_custom_camera", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_USE_CUSTOM_CAMERA);
  RNA_def_property_ui_text(
      prop, "Use Custom Camera", "Use custom camera instead of the active camera");
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_fuzzy_intersections", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_INTERSECTION_AS_CONTOUR);
  RNA_def_property_ui_text(prop,
                           "Intersection With Contour",
                           "Treat intersection and contour lines as if they were the same type so "
                           "they can be chained together");
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_fuzzy_all", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_EVERYTHING_AS_CONTOUR);
  RNA_def_property_ui_text(
      prop, "All Lines", "Treat all lines as the same line type so they can be chained together");
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_object_instances", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_ALLOW_DUPLI_OBJECTS);
  RNA_def_property_ui_text(
      prop,
      "Instanced Objects",
      "Support particle objects and face/vertex instances to show in line art");
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_edge_overlap", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_ALLOW_OVERLAPPING_EDGES);
  RNA_def_property_ui_text(
      prop,
      "Handle Overlapping Edges",
      "Allow edges in the same location (i.e. from edge split) to show properly. May run slower");
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_clip_plane_boundaries", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_ALLOW_CLIPPING_BOUNDARIES);
  RNA_def_property_ui_text(prop,
                           "Clipping Boundaries",
                           "Allow lines generated by the near/far clipping plane to be shown");
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "crease_threshold", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_range(prop, 0, DEG2RAD(180.0f));
  RNA_def_property_ui_range(prop, 0.0f, DEG2RAD(180.0f), 0.01f, 1);
  RNA_def_property_ui_text(
      prop, "Crease Threshold", "Angles smaller than this will be treated as creases");
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "split_angle", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "angle_splitting_threshold");
  RNA_def_property_ui_text(
      prop, "Angle Splitting", "Angle in screen space below which a stroke is split in two");
  /* Don't allow value very close to PI, or we get a lot of small segments. */
  RNA_def_property_ui_range(prop, 0.0f, DEG2RAD(179.5f), 0.01f, 1);
  RNA_def_property_range(prop, 0.0f, DEG2RAD(180.0f));
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "smooth_tolerance", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "chain_smooth_tolerance");
  RNA_def_property_ui_text(
      prop, "Smooth Tolerance", "Strength of smoothing applied on jagged chains");
  RNA_def_property_ui_range(prop, 0.0f, 1.0f, 0.05f, 4);
  RNA_def_property_range(prop, 0.0f, 30.0f);
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_remove_doubles", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_REMOVE_DOUBLES);
  RNA_def_property_ui_text(
      prop, "Remove Doubles", "Remove doubles from the source geometry before generating stokes");
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_loose_as_contour", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_LOOSE_AS_CONTOUR);
  RNA_def_property_ui_text(prop, "Loose As Contour", "Loose edges will have contour type");
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_source_vertex_group", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_GPENCIL_INVERT_SOURCE_VGROUP);
  RNA_def_property_ui_text(prop, "Invert Vertex Group", "Invert source vertex group values");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_output_vertex_group_match_by_name", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_GPENCIL_MATCH_OUTPUT_VGROUP);
  RNA_def_property_ui_text(prop, "Match Output", "Match output vertex group based on name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_face_mark", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_FILTER_FACE_MARK);
  RNA_def_property_ui_text(
      prop, "Filter Face Marks", "Filter feature lines using freestyle face marks");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_face_mark_invert", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_FILTER_FACE_MARK_INVERT);
  RNA_def_property_ui_text(prop, "Invert", "Invert face mark filtering");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_face_mark_boundaries", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_FILTER_FACE_MARK_BOUNDARIES);
  RNA_def_property_ui_text(
      prop, "Boundaries", "Filter feature lines based on face mark boundaries");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_face_mark_keep_contour", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(
      prop, NULL, "calculation_flags", LRT_FILTER_FACE_MARK_KEEP_CONTOUR);
  RNA_def_property_ui_text(prop, "Keep Contour", "Preserve contour lines while filtering");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "chaining_image_threshold", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_ui_text(
      prop,
      "Image Threshold",
      "Segments with an image distance smaller than this will be chained together");
  RNA_def_property_ui_range(prop, 0.0f, 0.3f, 0.001f, 4);
  RNA_def_property_range(prop, 0.0f, 0.3f);
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_loose_edge_chain", PROP_BOOLEAN, PROP_NONE);
  RNA_def_prop_bool_stype(prop, NULL, "calculation_flags", LRT_CHAIN_LOOSE_EDGES);
  RNA_def_property_ui_text(prop, "Chain Loose Edges", "Allow loose edges to be chained together");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_geometry_space_chain", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_CHAIN_GEOMETRY_SPACE);
  RNA_def_property_ui_text(
      prop, "Use Geometry Space", "Use geometry distance for chaining instead of image space");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_detail_preserve", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_CHAIN_PRESERVE_DETAILS);
  RNA_def_property_ui_text(
      prop, "Preserve Details", "Keep the zig-zag \"noise\" in initial chaining");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_overlap_edge_type_support", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_ALLOW_OVERLAP_EDGE_TYPES);
  RNA_def_property_ui_text(prop,
                           "Overlapping Edge Types",
                           "Allow an edge to have multiple overlapping types. This will create a "
                           "separate stroke for each overlapping type");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "stroke_depth_offset", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_ui_text(prop,
                           "Stroke Depth Offset",
                           "Move strokes slightly towards the camera to avoid clipping while "
                           "preserve depth for the viewport");
  RNA_def_property_ui_range(prop, 0.0, 0.5, 0.001, 4);
  RNA_def_property_range(prop, -0.1, FLT_MAX);
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_offset_towards_custom_camera", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LRT_GPENCIL_OFFSET_TOWARDS_CUSTOM_CAMERA);
  RNA_def_property_ui_text(prop,
                           "Offset Towards Custom Camera",
                           "Offset strokes towards selected camera instead of the active camera");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "source_camera", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  RNA_def_property_struct_type(prop, "Object");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop, "Camera Object", "Use specified camera object for generating line art");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  prop = RNA_def_property(srna, "source_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, modifier_lineart_source_type);
  RNA_def_property_ui_text(prop, "Source Type", "Line art stroke source type");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  prop = RNA_def_property(srna, "source_object", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  RNA_def_property_struct_type(prop, "Object");
  RNA_def_property_ui_text(prop, "Object", "Generate strokes from this object");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  prop = RNA_def_property(srna, "source_collection", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  RNA_def_property_struct_type(prop, "Collection");
  RNA_def_property_ui_text(
      prop, "Collection", "Generate strokes from the objects in this collection");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  /* types */
  prop = RNA_def_property(srna, "use_contour", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "edge_types", LRT_EDGE_FLAG_CONTOUR);
  RNA_def_property_ui_text(prop, "Use Contour", "Generate strokes from contours lines");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_loose", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "edge_types", LRT_EDGE_FLAG_LOOSE);
  RNA_def_property_ui_text(prop, "Use Loose", "Generate strokes from loose edges");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_crease", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "edge_types", LRT_EDGE_FLAG_CREASE);
  RNA_def_property_ui_text(prop, "Use Crease", "Generate strokes from creased edges");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_material", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "edge_types", LRT_EDGE_FLAG_MATERIAL);
  RNA_def_property_ui_text(
      prop, "Use Material", "Generate strokes from borders between materials");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_edge_mark", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "edge_types", LRT_EDGE_FLAG_EDGE_MARK);
  RNA_def_property_ui_text(prop, "Use Edge Mark", "Generate strokes from freestyle marked edges");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_intersection", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "edge_types", LRT_EDGE_FLAG_INTERSECTION);
  RNA_def_property_ui_text(prop, "Use Intersection", "Generate strokes from intersections");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_multiple_levels", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "use_multiple_levels", 0);
  RNA_def_property_ui_text(
      prop, "Use Occlusion Range", "Generate strokes from a range of occlusion levels");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "level_start", PROP_INT, PROP_NONE);
  RNA_def_property_ui_text(
      prop, "Level Start", "Minimum number of occlusions for the generated strokes");
  RNA_def_property_range(prop, 0, 128);
  RNA_def_property_int_funcs(prop, NULL, "rna_Lineart_start_level_set", NULL);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "level_end", PROP_INT, PROP_NONE);
  RNA_def_property_ui_text(
      prop, "Level End", "Maximum number of occlusions for the generated strokes");
  RNA_def_property_range(prop, 0, 128);
  RNA_def_property_int_funcs(prop, NULL, "rna_Lineart_end_level_set", NULL);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "target_material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "Material");
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_LineartGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(
      prop, "Material", "Grease Pencil material assigned to the generated strokes");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "target_layer", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(prop, "Layer", "Grease Pencil layer assigned to the generated strokes");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "source_vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(
      prop,
      "Source Vertex Group",
      "Match the beginning of vertex group names from mesh objects, match all when left empty");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "vgname");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_LineartGpencilModifier_vgname_set");
  RNA_def_property_ui_text(prop, "Vertex Group", "Vertex group name for selected strokes");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "is_baked", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LRT_GPENCIL_IS_BAKED);
  RNA_def_property_ui_text(prop, "Is Baked", "This modifier has baked data");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_cache", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LRT_GPENCIL_USE_CACHE);
  RNA_def_property_ui_text(prop,
                           "Use Cache",
                           "Use cached scene data from the first line art modifier in the stack. "
                           "Certain settings will be unavailable");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "overscan", PROP_FLOAT, PROP_NONE);
  RNA_def_property_ui_text(
      prop,
      "Overscan",
      "A margin to prevent strokes from ending abruptly at the edge of the image");
  RNA_def_property_ui_range(prop, 0.0f, 0.5f, 0.01f, 3);
  RNA_def_property_range(prop, 0.0f, 0.5f);
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "thickness", PROP_INT, PROP_NONE);
  RNA_def_property_ui_text(prop, "Thickness", "The thickness for the generated strokes");
  RNA_def_property_ui_range(prop, 1, 100, 1, 1);
  RNA_def_property_range(prop, 1, 200);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "opacity", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_ui_text(prop, "Opacity", "The strength value for the generate strokes");
  RNA_def_property_ui_range(prop, 0.0f, 1.0f, 0.01f, 2);
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_material_mask", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "mask_switches", LRT_GPENCIL_MATERIAL_MASK_ENABLE);
  RNA_def_property_ui_text(
      prop, "Use Material Mask", "Use material masks to filter out occluded strokes");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_material_mask_match", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "mask_switches", LRT_GPENCIL_MATERIAL_MASK_MATCH);
  RNA_def_property_ui_text(
      prop, "Match Masks", "Require matching all material masks instead of just one");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_material_mask_bits", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "material_mask_bits", 1);
  RNA_def_property_array(prop, 8);
  RNA_def_property_ui_text(prop, "Masks", "Mask bits to match from Material Line Art settings");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_intersection_match", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "mask_switches", LRT_GPENCIL_INTERSECTION_MATCH);
  RNA_def_property_ui_text(
      prop, "Match Intersection", "Require matching all intersection masks instead of just one");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_intersection_mask", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "intersection_mask", 1);
  RNA_def_property_array(prop, 8);
  RNA_def_property_ui_text(prop, "Masks", "Mask bits to match from Collection Line Art settings");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_crease_on_smooth", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(
      prop, NULL, "calculation_flags", LRT_USE_CREASE_ON_SMOOTH_SURFACES);
  RNA_def_property_ui_text(
      prop, "Crease On Smooth Surfaces", "Allow crease edges to show inside smooth surfaces");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_crease_on_sharp", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_USE_CREASE_ON_SHARP_EDGES);
  RNA_def_property_ui_text(prop, "Crease On Sharp Edges", "Allow crease to show on sharp edges");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_image_boundary_trimming", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_USE_IMAGE_BOUNDARY_TRIMMING);
  RNA_def_property_ui_text(
      prop,
      "Image Boundary Trimming",
      "Trim all edges right at the boundary of image(including overscan region)");

  prop = RNA_def_property(srna, "use_back_face_culling", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "calculation_flags", LRT_USE_BACK_FACE_CULLING);
  RNA_def_property_ui_text(
      prop,
      "Back Face Culling",
      "Remove all back faces to speed up calculation, this will create edges in "
      "different occlusion levels than when disabled");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_invert_collection", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LRT_GPENCIL_INVERT_COLLECTION);
  RNA_def_property_ui_text(prop,
                           "Invert Collection Filtering",
                           "Select everything except lines from specified collection");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpencillength(BlenderRNA *brna)
{
  ApiStruct *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "LengthGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Length Modifier", "Stretch or shrink strokes");
  RNA_def_struct_sdna(srna, "LengthGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_LENGTH);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "start_factor", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "start_fac");
  RNA_def_property_ui_range(prop, -10.0f, 10.0f, 0.1, 2);
  RNA_def_property_ui_text(
      prop, "Start Factor", "Added length to the start of each stroke relative to its length");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "end_factor", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "end_fac");
  RNA_def_property_ui_range(prop, -10.0f, 10.0f, 0.1, 2);
  RNA_def_property_ui_text(
      prop, "End Factor", "Added length to the end of each stroke relative to its length");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "start_length", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "start_fac");
  RNA_def_property_ui_range(prop, -100.0f, 100.0f, 0.1f, 3);
  RNA_def_property_ui_text(
      prop, "Start Factor", "Absolute added length to the start of each stroke");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "end_length", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "end_fac");
  RNA_def_property_ui_range(prop, -100.0f, 100.0f, 0.1f, 3);
  RNA_def_property_ui_text(prop, "End Factor", "Absolute added length to the end of each stroke");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "random_start_factor", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "rand_start_fac");
  RNA_def_property_ui_range(prop, -10.0f, 10.0f, 0.1, 1);
  RNA_def_property_ui_text(
      prop, "Random Start Factor", "Size of random length added to the start of each stroke");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "random_end_factor", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "rand_end_fac");
  RNA_def_property_ui_range(prop, -10.0f, 10.0f, 0.1, 1);
  RNA_def_property_ui_text(
      prop, "Random End Factor", "Size of random length added to the end of each stroke");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "random_offset", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "rand_offset");
  RNA_def_property_ui_range(prop, 0.0f, 100.0f, 0.1, 3);
  RNA_def_property_ui_text(
      prop, "Random Noise Offset", "Smoothly offset each stroke's random value");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_random", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_USE_RANDOM);
  RNA_def_property_ui_text(prop, "Random", "Use random values over time");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "seed", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_ui_text(prop, "Seed", "Random seed");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "step", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "step");
  RNA_def_property_range(prop, 1, 100);
  RNA_def_property_ui_text(
      prop, "Step", "Number of frames before recalculate random values again");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "overshoot_factor", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "overshoot_fac");
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_text(
      prop,
      "Used Length",
      "Defines what portion of the stroke is used for the calculation of the extension");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "mode");
  RNA_def_property_enum_items(prop, gpencil_length_mode_items);
  RNA_def_property_ui_text(prop, "Mode", "Mode to define length");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_curvature", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_USE_CURVATURE);
  RNA_def_property_ui_text(prop, "Use Curvature", "Follow the curvature of the stroke");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_curvature", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_INVERT_CURVATURE);
  RNA_def_property_ui_text(
      prop, "Invert Curvature", "Invert the curvature of the stroke's extension");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "point_density", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.1f, 1000.0f);
  RNA_def_property_ui_range(prop, 0.1f, 1000.0f, 1.0f, 1);
  RNA_def_property_ui_scale_type(prop, PROP_SCALE_CUBIC);
  RNA_def_property_ui_text(
      prop, "Point Density", "Multiplied by Start/End for the total added point count");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "segment_influence", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_range(prop, -2.0f, 3.0f);
  RNA_def_property_ui_range(prop, 0.0f, 1.0f, 0.1f, 2);
  RNA_def_property_ui_text(prop,
                           "Segment Influence",
                           "Factor to determine how much the length of the individual segments "
                           "should influence the final computed curvature. Higher factors makes "
                           "small segments influence the overall curvature less");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "max_angle", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_ui_text(prop,
                           "Filter Angle",
                           "Ignore points on the stroke that deviate from their neighbors by more "
                           "than this angle when determining the extrapolation shape");
  RNA_def_property_range(prop, 0.0f, DEG2RAD(180.0f));
  RNA_def_property_ui_range(prop, 0.0f, DEG2RAD(179.5f), 10.0f, 1);
  RNA_def_property_update(prop, NC_SCENE, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpencildash(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "DashGpencilModifierSegment", NULL);
  RNA_def_struct_ui_text(srna, "Dash Modifier Segment", "Configuration for a single dash segment");
  RNA_def_struct_sdna(srna, "DashGpencilModifierSegment");
  RNA_def_struct_path_func(srna, "rna_DashGpencilModifierSegment_path");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(prop, "Name", "Name of the dash segment");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_DashGpencilModifierSegment_name_set");
  RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER | NA_RENAME, NULL);
  RNA_def_struct_name_property(srna, prop);

  prop = RNA_def_property(srna, "dash", PROP_INT, PROP_NONE);
  RNA_def_property_range(prop, 1, INT16_MAX);
  RNA_def_property_ui_text(
      prop,
      "Dash",
      "The number of consecutive points from the original stroke to include in this segment");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "gap", PROP_INT, PROP_NONE);
  RNA_def_property_range(prop, 1, INT16_MAX);
  RNA_def_property_ui_text(prop, "Gap", "The number of points skipped after this segment");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "radius", PROP_FLOAT, PROP_FACTOR | PROP_UNSIGNED);
  RNA_def_property_ui_range(prop, 0, 1, 0.1, 2);
  RNA_def_property_ui_text(
      prop, "Radius", "The factor to apply to the original point's radius for the new points");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "opacity", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_ui_range(prop, 0, 1, 0.1, 2);
  RNA_def_property_ui_text(
      prop, "Opacity", "The factor to apply to the original point's opacity for the new points");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "mat_nr");
  RNA_def_property_range(prop, -1, INT16_MAX);
  RNA_def_property_ui_text(
      prop,
      "Material Index",
      "Use this index on generated segment. -1 means using the existing material");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  srna = RNA_def_struct(brna, "DashGpencilModifierData", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Dash Modifier", "Create dot-dash effect for strokes");
  RNA_def_struct_sdna(srna, "DashGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_DASH);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "segments", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "DashGpencilModifierSegment");
  RNA_def_property_collection_sdna(prop, NULL, "segments", NULL);
  RNA_def_property_collection_funcs(prop,
                                    "rna_GpencilDash_segments_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_ui_text(prop, "Segments", "");

  prop = RNA_def_property(srna, "segment_active_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_ui_text(prop, "Active Dash Segment Index", "Active index in the segment list");

  prop = RNA_def_property(srna, "dash_offset", PROP_INT, PROP_NONE);
  RNA_def_property_ui_text(
      prop,
      "Offset",
      "Offset into each stroke before the beginning of  the dashed segment generation");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  /* Common properties. */

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_LENGTH_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpencilshrinkwrap(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "ShrinkwrapGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna,
                         "Shrinkwrap Modifier",
                         "Shrink wrapping modifier to shrink wrap and object to a target");
  RNA_def_struct_sdna(srna, "ShrinkwrapGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_SHRINKWRAP);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "wrap_method", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "shrink_type");
  RNA_def_property_enum_items(prop, rna_enum_shrinkwrap_type_items);
  RNA_def_property_ui_text(prop, "Wrap Method", "");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  prop = RNA_def_property(srna, "wrap_mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "shrink_mode");
  RNA_def_property_enum_items(prop, rna_enum_modifier_shrinkwrap_mode_items);
  RNA_def_property_ui_text(
      prop, "Snap Mode", "Select how vertices are constrained to the target surface");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  prop = RNA_def_property(srna, "cull_face", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "shrink_opts");
  RNA_def_property_enum_items(prop, rna_enum_shrinkwrap_face_cull_items);
  RNA_def_property_enum_funcs(prop,
                              "rna_ShrinkwrapGpencilModifier_face_cull_get",
                              "rna_ShrinkwrapGpencilModifier_face_cull_set",
                              NULL);
  RNA_def_property_ui_text(
      prop,
      "Face Cull",
      "Stop vertices from projecting to a face on the target when facing towards/away");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "target", PROP_POINTER, PROP_NONE);
  RNA_def_property_ui_text(prop, "Target", "Mesh target to shrink to");
  RNA_def_property_pointer_funcs(
      prop, NULL, "rna_ShrinkwrapGpencilModifier_target_set", NULL, "rna_Mesh_object_poll");
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  prop = RNA_def_property(srna, "auxiliary_target", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "aux_target");
  RNA_def_property_ui_text(prop, "Auxiliary Target", "Additional mesh target to shrink to");
  RNA_def_property_pointer_funcs(
      prop, NULL, "rna_ShrinkwrapGpencilModifier_aux_target_set", NULL, "rna_Mesh_object_poll");
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_dependency_update");

  prop = RNA_def_property(srna, "offset", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "keep_dist");
  RNA_def_property_range(prop, -FLT_MAX, FLT_MAX);
  RNA_def_property_ui_range(prop, -100, 100, 1, 2);
  RNA_def_property_ui_text(prop, "Offset", "Distance to keep from the target");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "project_limit", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "proj_limit");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0, 100, 1, 2);
  RNA_def_property_ui_text(
      prop, "Project Limit", "Limit the distance used for projection (zero disables)");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_project_x", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "proj_axis", MOD_SHRINKWRAP_PROJECT_OVER_X_AXIS);
  RNA_def_property_ui_text(prop, "X", "");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_project_y", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "proj_axis", MOD_SHRINKWRAP_PROJECT_OVER_Y_AXIS);
  RNA_def_property_ui_text(prop, "Y", "");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_project_z", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "proj_axis", MOD_SHRINKWRAP_PROJECT_OVER_Z_AXIS);
  RNA_def_property_ui_text(prop, "Z", "");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "subsurf_levels", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "subsurf_levels");
  RNA_def_property_range(prop, 0, 6);
  RNA_def_property_ui_range(prop, 0, 6, 1, -1);
  RNA_def_property_ui_text(
      prop,
      "Subdivision Levels",
      "Number of subdivisions that must be performed before extracting vertices' "
      "positions and normals");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_negative_direction", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "shrink_opts", MOD_SHRINKWRAP_PROJECT_ALLOW_NEG_DIR);
  RNA_def_property_ui_text(
      prop, "Negative", "Allow vertices to move in the negative direction of axis");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_positive_direction", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "shrink_opts", MOD_SHRINKWRAP_PROJECT_ALLOW_POS_DIR);
  RNA_def_property_ui_text(
      prop, "Positive", "Allow vertices to move in the positive direction of axis");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_invert_cull", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "shrink_opts", MOD_SHRINKWRAP_INVERT_CULL_TARGET);
  RNA_def_property_ui_text(
      prop, "Invert Cull", "When projecting in the negative direction invert the face cull mode");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_ShrinkwrapGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "vgname");
  RNA_def_property_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_ShrinkwrapGpencilModifier_vgname_set");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SHRINKWRAP_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SHRINKWRAP_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SHRINKWRAP_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_vertex", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", MOD_SHRINKWRAP_INVERT_VGROUP);
  RNA_def_property_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SHRINKWRAP_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "smooth_factor", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "smooth_factor");
  RNA_def_property_range(prop, 0, 1);
  RNA_def_property_ui_text(prop, "Smooth Factor", "Amount of smoothing to apply");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "smooth_step", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "smooth_step");
  RNA_def_property_range(prop, 1, 10);
  RNA_def_property_ui_text(
      prop, "Step", "Number of times to apply smooth (high numbers can reduce FPS)");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

void RNA_def_greasepencil_modifier(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  /* data */
  srna = RNA_def_struct(brna, "GpencilModifier", NULL);
  RNA_def_struct_ui_text(srna, "GpencilModifier", "Modifier affecting the Grease Pencil object");
  RNA_def_struct_refine_func(srna, "rna_GpencilModifier_refine");
  RNA_def_struct_path_func(srna, "rna_GpencilModifier_path");
  RNA_def_struct_sdna(srna, "GpencilModifierData");

  /* strings */
  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_GpencilModifier_name_set");
  RNA_def_property_ui_text(prop, "Name", "Modifier name");
  RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER | NA_RENAME, NULL);
  RNA_def_struct_name_property(srna, prop);

  /* enums */
  prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_enum_sdna(prop, NULL, "type");
  RNA_def_property_enum_items(prop, rna_enum_object_greasepencil_modifier_type_items);
  RNA_def_property_ui_text(prop, "Type", "");

  /* flags */
  prop = RNA_def_property(srna, "show_viewport", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "mode", eGpencilModifierMode_Realtime);
  RNA_def_property_ui_text(prop, "Realtime", "Display modifier in viewport");
  RNA_def_property_flag(prop, PROP_LIB_EXCEPTION);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");
  RNA_def_property_ui_icon(prop, ICON_RESTRICT_VIEW_ON, 1);

  prop = RNA_def_property(srna, "show_render", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "mode", eGpencilModifierMode_Render);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Render", "Use modifier during render");
  RNA_def_property_ui_icon(prop, ICON_RESTRICT_RENDER_ON, 1);
  RNA_def_property_update(prop, NC_OBJECT | ND_MODIFIER, NULL);

  prop = RNA_def_property(srna, "show_in_editmode", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "mode", eGpencilModifierMode_Editmode);
  RNA_def_property_ui_text(prop, "Edit Mode", "Display modifier in Edit mode");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");
  RNA_def_property_ui_icon(prop, ICON_EDITMODE_HLT, 0);

  prop = RNA_def_property(srna, "show_expanded", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NO_DEG_UPDATE);
  RNA_def_property_boolean_sdna(prop, NULL, "ui_expand_flag", 0);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Expanded", "Set modifier expanded in the user interface");
  RNA_def_property_ui_icon(prop, ICON_DISCLOSURE_TRI_RIGHT, 1);

  prop = RNA_def_boolean(srna,
                         "is_override_data",
                         false,
                         "Override Modifier",
                         "In a local override object, whether this modifier comes from the linked "
                         "reference object, or is local to the override");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_boolean_negative_sdna(
      prop, NULL, "flag", eGpencilModifierFlag_OverrideLibrary_Local);

  /* types */
  rna_def_modifier_gpencilnoise(brna);
  rna_def_modifier_gpencilsmooth(brna);
  rna_def_modifier_gpencilsubdiv(brna);
  rna_def_modifier_gpencilsimplify(brna);
  rna_def_modifier_gpencilthick(brna);
  rna_def_modifier_gpenciloffset(brna);
  rna_def_modifier_gpenciltint(brna);
  rna_def_modifier_gpenciltime(brna);
  rna_def_modifier_gpencilcolor(brna);
  rna_def_modifier_gpencilarray(brna);
  rna_def_modifier_gpencilbuild(brna);
  rna_def_modifier_gpencilopacity(brna);
  rna_def_modifier_gpencillattice(brna);
  rna_def_modifier_gpencilmirror(brna);
  rna_def_modifier_gpencilhook(brna);
  rna_def_modifier_gpencilarmature(brna);
  rna_def_modifier_gpencilmultiply(brna);
  rna_def_modifier_gpenciltexture(brna);
  rna_def_modifier_gpencilweight_angle(brna);
  rna_def_modifier_gpencilweight_proximity(brna);
  rna_def_modifier_gpencillineart(brna);
  rna_def_modifier_gpencillength(brna);
  rna_def_modifier_gpencildash(brna);
  rna_def_modifier_gpencilshrinkwrap(brna);
}

#endif
