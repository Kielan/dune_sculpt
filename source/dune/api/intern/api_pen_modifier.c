#include <float.h>
#include <limits.h>
#include <stdlib.h>

#include "DNA_armature_types.h"
#include "DNA_brush_types.h"
#include "DNA_cachefile_types.h"
#include "DNA_gpencil_modifier_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_force_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_rand.h"
#include "BLI_string_utils.h"

#include "BLT_translation.h"

#include "BKE_animsys.h"
#include "BKE_data_transfer.h"
#include "BKE_dynamicpaint.h"
#include "BKE_effect.h"
#include "BKE_fluid.h" /* For BKE_fluid_modifier_free & BKE_fluid_modifier_create_type_data */
#include "BKE_mesh_mapping.h"
#include "BKE_mesh_remap.h"
#include "BKE_multires.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "WM_api.h"
#include "WM_types.h"

const EnumPropertyItem rna_enum_object_greasepencil_modifier_type_items[] = {
    {0, "", 0, N_("Modify"), ""},
    {eGpencilModifierType_Texture,
     "GP_TEXTURE",
     ICON_MOD_UVPROJECT,
     "Texture Mapping",
     "Change stroke uv texture values"},
    {eGpencilModifierType_Time, "GP_TIME", ICON_MOD_TIME, "Time Offset", "Offset keyframes"},
    {eGpencilModifierType_WeightAngle,
     "GP_WEIGHT_ANGLE",
     ICON_MOD_VERTEX_WEIGHT,
     "Vertex Weight Angle",
     "Generate Vertex Weights base on stroke angle"},
    {eGpencilModifierType_WeightProximity,
     "GP_WEIGHT_PROXIMITY",
     ICON_MOD_VERTEX_WEIGHT,
     "Vertex Weight Proximity",
     "Generate Vertex Weights base on distance to object"},
    {0, "", 0, N_("Generate"), ""},
    {eGpencilModifierType_Array,
     "GP_ARRAY",
     ICON_MOD_ARRAY,
     "Array",
     "Create array of duplicate instances"},
    {eGpencilModifierType_Build,
     "GP_BUILD",
     ICON_MOD_BUILD,
     "Build",
     "Create duplication of strokes"},
    {eGpencilModifierType_Dash,
     "GP_DASH",
     ICON_MOD_DASH,
     "Dot Dash",
     "Generate dot-dash styled strokes"},
    {eGpencilModifierType_Length,
     "GP_LENGTH",
     ICON_MOD_LENGTH,
     "Length",
     "Extend or shrink strokes"},
    {eGpencilModifierType_Lineart,
     "GP_LINEART",
     ICON_MOD_LINEART,
     "Line Art",
     "Generate line art strokes from selected source"},
    {eGpencilModifierType_Mirror,
     "GP_MIRROR",
     ICON_MOD_MIRROR,
     "Mirror",
     "Duplicate strokes like a mirror"},
    {eGpencilModifierType_Multiply,
     "GP_MULTIPLY",
     ICON_GP_MULTIFRAME_EDITING,
     "Multiple Strokes",
     "Produce multiple strokes along one stroke"},
    {eGpencilModifierType_Simplify,
     "GP_SIMPLIFY",
     ICON_MOD_SIMPLIFY,
     "Simplify",
     "Simplify stroke reducing number of points"},
    {eGpencilModifierType_Subdiv,
     "GP_SUBDIV",
     ICON_MOD_SUBSURF,
     "Subdivide",
     "Subdivide stroke adding more control points"},
    {0, "", 0, N_("Deform"), ""},
    {eGpencilModifierType_Armature,
     "GP_ARMATURE",
     ICON_MOD_ARMATURE,
     "Armature",
     "Deform stroke points using armature object"},
    {eGpencilModifierType_Hook,
     "GP_HOOK",
     ICON_HOOK,
     "Hook",
     "Deform stroke points using objects"},
    {eGpencilModifierType_Lattice,
     "GP_LATTICE",
     ICON_MOD_LATTICE,
     "Lattice",
     "Deform strokes using lattice"},
    {eGpencilModifierType_Noise, "GP_NOISE", ICON_MOD_NOISE, "Noise", "Add noise to strokes"},
    {eGpencilModifierType_Offset,
     "GP_OFFSET",
     ICON_MOD_OFFSET,
     "Offset",
     "Change stroke location, rotation or scale"},
    {eGpencilModifierType_Shrinkwrap,
     "SHRINKWRAP",
     ICON_MOD_SHRINKWRAP,
     "Shrinkwrap",
     "Project the shape onto another object"},
    {eGpencilModifierType_Smooth, "GP_SMOOTH", ICON_MOD_SMOOTH, "Smooth", "Smooth stroke"},
    {eGpencilModifierType_Thick,
     "GP_THICK",
     ICON_MOD_THICKNESS,
     "Thickness",
     "Change stroke thickness"},
    {0, "", 0, N_("Color"), ""},
    {eGpencilModifierType_Color,
     "GP_COLOR",
     ICON_MOD_HUE_SATURATION,
     "Hue/Saturation",
     "Apply changes to stroke colors"},
    {eGpencilModifierType_Opacity,
     "GP_OPACITY",
     ICON_MOD_OPACITY,
     "Opacity",
     "Opacity of the strokes"},
    {eGpencilModifierType_Tint, "GP_TINT", ICON_MOD_TINT, "Tint", "Tint strokes with new color"},
    {0, NULL, 0, NULL, NULL},
};

#ifndef RNA_RUNTIME
static const EnumPropertyItem modifier_modify_color_items[] = {
    {GP_MODIFY_COLOR_BOTH, "BOTH", 0, "Stroke & Fill", "Modify fill and stroke colors"},
    {GP_MODIFY_COLOR_STROKE, "STROKE", 0, "Stroke", "Modify stroke color only"},
    {GP_MODIFY_COLOR_FILL, "FILL", 0, "Fill", "Modify fill color only"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem modifier_modify_opacity_items[] = {
    {GP_MODIFY_COLOR_BOTH, "BOTH", 0, "Stroke & Fill", "Modify fill and stroke colors"},
    {GP_MODIFY_COLOR_STROKE, "STROKE", 0, "Stroke", "Modify stroke color only"},
    {GP_MODIFY_COLOR_FILL, "FILL", 0, "Fill", "Modify fill color only"},
    {GP_MODIFY_COLOR_HARDNESS, "HARDNESS", 0, "Hardness", "Modify stroke hardness"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem modifier_gphook_falloff_items[] = {
    {eGPHook_Falloff_None, "NONE", 0, "No Falloff", ""},
    {eGPHook_Falloff_Curve, "CURVE", 0, "Curve", ""},
    {eGPHook_Falloff_Smooth, "SMOOTH", ICON_SMOOTHCURVE, "Smooth", ""},
    {eGPHook_Falloff_Sphere, "SPHERE", ICON_SPHERECURVE, "Sphere", ""},
    {eGPHook_Falloff_Root, "ROOT", ICON_ROOTCURVE, "Root", ""},
    {eGPHook_Falloff_InvSquare, "INVERSE_SQUARE", ICON_ROOTCURVE, "Inverse Square", ""},
    {eGPHook_Falloff_Sharp, "SHARP", ICON_SHARPCURVE, "Sharp", ""},
    {eGPHook_Falloff_Linear, "LINEAR", ICON_LINCURVE, "Linear", ""},
    {eGPHook_Falloff_Const, "CONSTANT", ICON_NOCURVE, "Constant", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem rna_enum_time_mode_items[] = {
    {GP_TIME_MODE_NORMAL, "NORMAL", 0, "Regular", "Apply offset in usual animation direction"},
    {GP_TIME_MODE_REVERSE, "REVERSE", 0, "Reverse", "Apply offset in reverse animation direction"},
    {GP_TIME_MODE_FIX, "FIX", 0, "Fixed Frame", "Keep frame and do not change with time"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem gpencil_subdivision_type_items[] = {
    {GP_SUBDIV_CATMULL, "CATMULL_CLARK", 0, "Catmull-Clark", ""},
    {GP_SUBDIV_SIMPLE, "SIMPLE", 0, "Simple", ""},
    {0, NULL, 0, NULL, NULL},
};
static const EnumPropertyItem gpencil_tint_type_items[] = {
    {GP_TINT_UNIFORM, "UNIFORM", 0, "Uniform", ""},
    {GP_TINT_GRADIENT, "GRADIENT", 0, "Gradient", ""},
    {0, NULL, 0, NULL, NULL},
};
static const EnumPropertyItem gpencil_length_mode_items[] = {
    {GP_LENGTH_RELATIVE, "RELATIVE", 0, "Relative", "Length in ratio to the stroke's length"},
    {GP_LENGTH_ABSOLUTE, "ABSOLUTE", 0, "Absolute", "Length in geometry space"},
    {0, NULL, 0, NULL, NULL},
};
#endif

#ifdef RNA_RUNTIME

#  include "DNA_curve_types.h"
#  include "DNA_fluid_types.h"
#  include "DNA_material_types.h"
#  include "DNA_particle_types.h"

#  include "BKE_cachefile.h"
#  include "BKE_context.h"
#  include "BKE_gpencil.h"
#  include "BKE_gpencil_modifier.h"
#  include "BKE_object.h"

#  include "DEG_depsgraph.h"
#  include "DEG_depsgraph_build.h"

static StructRNA *rna_GpencilModifier_refine(struct PointerRNA *ptr)
{
  GpencilModifierData *md = (GpencilModifierData *)ptr->data;

  switch ((GpencilModifierType)md->type) {
    case eGpencilModifierType_Noise:
      return &RNA_NoiseGpencilModifier;
    case eGpencilModifierType_Subdiv:
      return &RNA_SubdivGpencilModifier;
    case eGpencilModifierType_Simplify:
      return &RNA_SimplifyGpencilModifier;
    case eGpencilModifierType_Thick:
      return &RNA_ThickGpencilModifier;
    case eGpencilModifierType_Tint:
      return &RNA_TintGpencilModifier;
    case eGpencilModifierType_Time:
      return &RNA_TimeGpencilModifier;
    case eGpencilModifierType_WeightProximity:
      return &RNA_WeightProxGpencilModifier;
    case eGpencilModifierType_WeightAngle:
      return &RNA_WeightAngleGpencilModifier;
    case eGpencilModifierType_Color:
      return &RNA_ColorGpencilModifier;
    case eGpencilModifierType_Array:
      return &RNA_ArrayGpencilModifier;
    case eGpencilModifierType_Build:
      return &RNA_BuildGpencilModifier;
    case eGpencilModifierType_Opacity:
      return &RNA_OpacityGpencilModifier;
    case eGpencilModifierType_Lattice:
      return &RNA_LatticeGpencilModifier;
    case eGpencilModifierType_Length:
      return &RNA_LengthGpencilModifier;
    case eGpencilModifierType_Mirror:
      return &RNA_MirrorGpencilModifier;
    case eGpencilModifierType_Shrinkwrap:
      return &RNA_ShrinkwrapGpencilModifier;
    case eGpencilModifierType_Smooth:
      return &RNA_SmoothGpencilModifier;
    case eGpencilModifierType_Hook:
      return &RNA_HookGpencilModifier;
    case eGpencilModifierType_Offset:
      return &RNA_OffsetGpencilModifier;
    case eGpencilModifierType_Armature:
      return &RNA_ArmatureGpencilModifier;
    case eGpencilModifierType_Multiply:
      return &RNA_MultiplyGpencilModifier;
    case eGpencilModifierType_Texture:
      return &RNA_TextureGpencilModifier;
    case eGpencilModifierType_Lineart:
      return &RNA_LineartGpencilModifier;
    case eGpencilModifierType_Dash:
      return &RNA_DashGpencilModifierData;
      /* Default */
    case eGpencilModifierType_None:
    case NUM_GREASEPENCIL_MODIFIER_TYPES:
      return &RNA_GpencilModifier;
  }

  return &RNA_GpencilModifier;
}

static void rna_GpencilModifier_name_set(PointerRNA *ptr, const char *value)
{
  GpencilModifierData *gmd = ptr->data;
  char oldname[sizeof(gmd->name)];

  /* Make a copy of the old name first. */
  BLI_strncpy(oldname, gmd->name, sizeof(gmd->name));

  /* Copy the new name into the name slot. */
  BLI_strncpy_utf8(gmd->name, value, sizeof(gmd->name));

  /* Make sure the name is truly unique. */
  if (ptr->owner_id) {
    Object *ob = (Object *)ptr->owner_id;
    BKE_gpencil_modifier_unique_name(&ob->greasepencil_modifiers, gmd);
  }

  /* Fix all the animation data which may link to this. */
  BKE_animdata_fix_paths_rename_all(NULL, "grease_pencil_modifiers", oldname, gmd->name);
}

static char *rna_GpencilModifier_path(PointerRNA *ptr)
{
  GpencilModifierData *gmd = ptr->data;
  char name_esc[sizeof(gmd->name) * 2];

  BLI_str_escape(name_esc, gmd->name, sizeof(name_esc));
  return BLI_sprintfN("grease_pencil_modifiers[\"%s\"]", name_esc);
}

static void rna_GpencilModifier_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  DEG_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
  WM_main_add_notifier(NC_OBJECT | ND_MODIFIER, ptr->owner_id);
}

static void rna_GpencilModifier_dependency_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  rna_GpencilModifier_update(bmain, scene, ptr);
  DEG_relations_tag_update(bmain);
}

/* Vertex Groups */

#  define RNA_GP_MOD_VGROUP_NAME_SET(_type, _prop) \
    static void rna_##_type##GpencilModifier_##_prop##_set(PointerRNA *ptr, const char *value) \
    { \
      _type##GpencilModifierData *tmd = (_type##GpencilModifierData *)ptr->data; \
      rna_object_vgroup_name_set(ptr, value, tmd->_prop, sizeof(tmd->_prop)); \
    }

RNA_GP_MOD_VGROUP_NAME_SET(Noise, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Thick, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Opacity, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Lattice, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Smooth, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Hook, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Offset, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Armature, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Texture, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Tint, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(WeightProx, target_vgname);
RNA_GP_MOD_VGROUP_NAME_SET(WeightProx, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(WeightAngle, target_vgname);
RNA_GP_MOD_VGROUP_NAME_SET(WeightAngle, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Lineart, vgname);
RNA_GP_MOD_VGROUP_NAME_SET(Shrinkwrap, vgname);

#  undef RNA_GP_MOD_VGROUP_NAME_SET

/* Objects */

static void greasepencil_modifier_object_set(Object *self,
                                             Object **ob_p,
                                             int type,
                                             PointerRNA value)
{
  Object *ob = value.data;

  if (!self || ob != self) {
    if (!ob || type == OB_EMPTY || ob->type == type) {
      id_lib_extern((ID *)ob);
      *ob_p = ob;
    }
  }
}

#  define RNA_GP_MOD_OBJECT_SET(_type, _prop, _obtype) \
    static void rna_##_type##GpencilModifier_##_prop##_set( \
        PointerRNA *ptr, PointerRNA value, struct ReportList *UNUSED(reports)) \
    { \
      _type##GpencilModifierData *tmd = (_type##GpencilModifierData *)ptr->data; \
      greasepencil_modifier_object_set((Object *)ptr->owner_id, &tmd->_prop, _obtype, value); \
    }

RNA_GP_MOD_OBJECT_SET(Armature, object, OB_ARMATURE);
RNA_GP_MOD_OBJECT_SET(Lattice, object, OB_LATTICE);
RNA_GP_MOD_OBJECT_SET(Mirror, object, OB_EMPTY);
RNA_GP_MOD_OBJECT_SET(WeightProx, object, OB_EMPTY);
RNA_GP_MOD_OBJECT_SET(Shrinkwrap, target, OB_MESH);
RNA_GP_MOD_OBJECT_SET(Shrinkwrap, aux_target, OB_MESH);

#  undef RNA_GP_MOD_OBJECT_SET

static void rna_HookGpencilModifier_object_set(PointerRNA *ptr,
                                               PointerRNA value,
                                               struct ReportList *UNUSED(reports))
{
  HookGpencilModifierData *hmd = ptr->data;
  Object *ob = (Object *)value.data;

  hmd->object = ob;
  id_lib_extern((ID *)ob);
  BKE_object_modifier_gpencil_hook_reset(ob, hmd);
}

static void rna_TintGpencilModifier_object_set(PointerRNA *ptr,
                                               PointerRNA value,
                                               struct ReportList *UNUSED(reports))
{
  TintGpencilModifierData *hmd = ptr->data;
  Object *ob = (Object *)value.data;

  hmd->object = ob;
  id_lib_extern((ID *)ob);
}

static void rna_TimeModifier_start_frame_set(PointerRNA *ptr, int value)
{
  TimeGpencilModifierData *tmd = ptr->data;
  CLAMP(value, MINFRAME, MAXFRAME);
  tmd->sfra = value;

  if (tmd->sfra >= tmd->efra) {
    tmd->efra = MIN2(tmd->sfra, MAXFRAME);
  }
}

static void rna_TimeModifier_end_frame_set(PointerRNA *ptr, int value)
{
  TimeGpencilModifierData *tmd = ptr->data;
  CLAMP(value, MINFRAME, MAXFRAME);
  tmd->efra = value;

  if (tmd->sfra >= tmd->efra) {
    tmd->sfra = MAX2(tmd->efra, MINFRAME);
  }
}

static void rna_GpencilOpacity_range(
    PointerRNA *ptr, float *min, float *max, float *softmin, float *softmax)
{
  OpacityGpencilModifierData *md = (OpacityGpencilModifierData *)ptr->data;

  *min = 0.0f;
  *softmin = 0.0f;

  *softmax = (md->flag & GP_OPACITY_NORMALIZE) ? 1.0f : 2.0f;
  *max = *softmax;
}

static void rna_GpencilOpacity_max_set(PointerRNA *ptr, float value)
{
  OpacityGpencilModifierData *md = (OpacityGpencilModifierData *)ptr->data;

  md->factor = value;
  if (md->flag & GP_OPACITY_NORMALIZE) {
    if (md->factor > 1.0f) {
      md->factor = 1.0f;
    }
  }
}

static void rna_GpencilModifier_opacity_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  OpacityGpencilModifierData *md = (OpacityGpencilModifierData *)ptr->data;
  if (md->flag & GP_OPACITY_NORMALIZE) {
    if (md->factor > 1.0f) {
      md->factor = 1.0f;
    }
  }

  rna_GpencilModifier_update(bmain, scene, ptr);
}

bool rna_GpencilModifier_material_poll(PointerRNA *ptr, PointerRNA value)
{
  Object *ob = (Object *)ptr->owner_id;
  Material *ma = (Material *)value.owner_id;

  return BKE_gpencil_object_material_index_get(ob, ma) != -1;
}

static void rna_GpencilModifier_material_set(PointerRNA *ptr,
                                             PointerRNA value,
                                             Material **ma_target,
                                             struct ReportList *reports)
{
  Object *ob = (Object *)ptr->owner_id;
  Material *ma = (Material *)value.owner_id;

  if (ma == NULL || BKE_gpencil_object_material_index_get(ob, ma) != -1) {
    id_lib_extern((ID *)ob);
    *ma_target = ma;
  }
  else {
    BKE_reportf(
        reports,
        RPT_ERROR,
        "Cannot assign material '%s', it has to be used by the grease pencil object already",
        ma->id.name);
  }
}

static void rna_LineartGpencilModifier_material_set(PointerRNA *ptr,
                                                    PointerRNA value,
                                                    struct ReportList *reports)
{
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)ptr->data;
  Material **ma_target = &lmd->target_material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_NoiseGpencilModifier_material_set(PointerRNA *ptr,
                                                  PointerRNA value,
                                                  struct ReportList *reports)
{
  NoiseGpencilModifierData *nmd = (NoiseGpencilModifierData *)ptr->data;
  Material **ma_target = &nmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_SmoothGpencilModifier_material_set(PointerRNA *ptr,
                                                   PointerRNA value,
                                                   struct ReportList *reports)
{
  SmoothGpencilModifierData *smd = (SmoothGpencilModifierData *)ptr->data;
  Material **ma_target = &smd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_SubdivGpencilModifier_material_set(PointerRNA *ptr,
                                                   PointerRNA value,
                                                   struct ReportList *reports)
{
  SubdivGpencilModifierData *smd = (SubdivGpencilModifierData *)ptr->data;
  Material **ma_target = &smd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_SimplifyGpencilModifier_material_set(PointerRNA *ptr,
                                                     PointerRNA value,
                                                     struct ReportList *reports)
{
  SimplifyGpencilModifierData *smd = (SimplifyGpencilModifierData *)ptr->data;
  Material **ma_target = &smd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_ThickGpencilModifier_material_set(PointerRNA *ptr,
                                                  PointerRNA value,
                                                  struct ReportList *reports)
{
  ThickGpencilModifierData *tmd = (ThickGpencilModifierData *)ptr->data;
  Material **ma_target = &tmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_WeightProxGpencilModifier_material_set(PointerRNA *ptr,
                                                       PointerRNA value,
                                                       struct ReportList *reports)
{
  WeightProxGpencilModifierData *tmd = (WeightProxGpencilModifierData *)ptr->data;
  Material **ma_target = &tmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_WeightAngleGpencilModifier_material_set(PointerRNA *ptr,
                                                        PointerRNA value,
                                                        struct ReportList *reports)
{
  WeightAngleGpencilModifierData *tmd = (WeightAngleGpencilModifierData *)ptr->data;
  Material **ma_target = &tmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_OffsetGpencilModifier_material_set(PointerRNA *ptr,
                                                   PointerRNA value,
                                                   struct ReportList *reports)
{
  OffsetGpencilModifierData *omd = (OffsetGpencilModifierData *)ptr->data;
  Material **ma_target = &omd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_TintGpencilModifier_material_set(PointerRNA *ptr,
                                                 PointerRNA value,
                                                 struct ReportList *reports)
{
  TintGpencilModifierData *tmd = (TintGpencilModifierData *)ptr->data;
  Material **ma_target = &tmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_ColorGpencilModifier_material_set(PointerRNA *ptr,
                                                  PointerRNA value,
                                                  struct ReportList *reports)
{
  ColorGpencilModifierData *cmd = (ColorGpencilModifierData *)ptr->data;
  Material **ma_target = &cmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_ArrayGpencilModifier_material_set(PointerRNA *ptr,
                                                  PointerRNA value,
                                                  struct ReportList *reports)
{
  ArrayGpencilModifierData *amd = (ArrayGpencilModifierData *)ptr->data;
  Material **ma_target = &amd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_OpacityGpencilModifier_material_set(PointerRNA *ptr,
                                                    PointerRNA value,
                                                    struct ReportList *reports)
{
  OpacityGpencilModifierData *omd = (OpacityGpencilModifierData *)ptr->data;
  Material **ma_target = &omd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_LatticeGpencilModifier_material_set(PointerRNA *ptr,
                                                    PointerRNA value,
                                                    struct ReportList *reports)
{
  LatticeGpencilModifierData *lmd = (LatticeGpencilModifierData *)ptr->data;
  Material **ma_target = &lmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_MirrorGpencilModifier_material_set(PointerRNA *ptr,
                                                   PointerRNA value,
                                                   struct ReportList *reports)
{
  MirrorGpencilModifierData *mmd = (MirrorGpencilModifierData *)ptr->data;
  Material **ma_target = &mmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_HookGpencilModifier_material_set(PointerRNA *ptr,
                                                 PointerRNA value,
                                                 struct ReportList *reports)
{
  HookGpencilModifierData *hmd = (HookGpencilModifierData *)ptr->data;
  Material **ma_target = &hmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_MultiplyGpencilModifier_material_set(PointerRNA *ptr,
                                                     PointerRNA value,
                                                     struct ReportList *reports)
{
  MultiplyGpencilModifierData *mmd = (MultiplyGpencilModifierData *)ptr->data;
  Material **ma_target = &mmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_TextureGpencilModifier_material_set(PointerRNA *ptr,
                                                    PointerRNA value,
                                                    struct ReportList *reports)
{
  TextureGpencilModifierData *tmd = (TextureGpencilModifierData *)ptr->data;
  Material **ma_target = &tmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_ShrinkwrapGpencilModifier_material_set(PointerRNA *ptr,
                                                       PointerRNA value,
                                                       struct ReportList *reports)
{
  ShrinkwrapGpencilModifierData *tmd = (ShrinkwrapGpencilModifierData *)ptr->data;
  Material **ma_target = &tmd->material;

  rna_GpencilModifier_material_set(ptr, value, ma_target, reports);
}

static void rna_Lineart_start_level_set(PointerRNA *ptr, int value)
{
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)ptr->data;

  CLAMP(value, 0, 128);
  lmd->level_start = value;
  lmd->level_end = MAX2(value, lmd->level_end);
}

static void rna_Lineart_end_level_set(PointerRNA *ptr, int value)
{
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)ptr->data;

  CLAMP(value, 0, 128);
  lmd->level_end = value;
  lmd->level_start = MIN2(value, lmd->level_start);
}

static void rna_GpencilDash_segments_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  DashGpencilModifierData *dmd = (DashGpencilModifierData *)ptr->data;
  rna_iterator_array_begin(
      iter, dmd->segments, sizeof(DashGpencilModifierSegment), dmd->segments_len, false, NULL);
}

static char *rna_DashGpencilModifierSegment_path(PointerRNA *ptr)
{
  DashGpencilModifierSegment *ds = (DashGpencilModifierSegment *)ptr->data;

  DashGpencilModifierData *dmd = (DashGpencilModifierData *)ds->dmd;

  BLI_assert(dmd != NULL);

  char name_esc[sizeof(dmd->modifier.name) * 2 + 1];

  BLI_str_escape(name_esc, dmd->modifier.name, sizeof(name_esc));

  return BLI_sprintfN("grease_pencil_modifiers[\"%s\"].segments[\"%s\"]", name_esc, ds->name);
}

static bool dash_segment_name_exists_fn(void *arg, const char *name)
{
  const DashGpencilModifierData *dmd = (const DashGpencilModifierData *)arg;
  for (int i = 0; i < dmd->segments_len; i++) {
    if (STREQ(dmd->segments[i].name, name)) {
      return true;
    }
  }
  return false;
}

static void rna_DashGpencilModifierSegment_name_set(PointerRNA *ptr, const char *value)
{
  DashGpencilModifierSegment *ds = ptr->data;

  char oldname[sizeof(ds->name)];
  BLI_strncpy(oldname, ds->name, sizeof(ds->name));

  BLI_strncpy_utf8(ds->name, value, sizeof(ds->name));

  BLI_assert(ds->dmd != NULL);
  BLI_uniquename_cb(
      dash_segment_name_exists_fn, ds->dmd, "Segment", '.', ds->name, sizeof(ds->name));

  char prefix[256];
  sprintf(prefix, "grease_pencil_modifiers[\"%s\"].segments", ds->dmd->modifier.name);

  /* Fix all the animation data which may link to this. */
  BKE_animdata_fix_paths_rename_all(NULL, prefix, oldname, ds->name);
}

static int rna_ShrinkwrapGpencilModifier_face_cull_get(PointerRNA *ptr)
{
  ShrinkwrapGpencilModifierData *swm = (ShrinkwrapGpencilModifierData *)ptr->data;
  return swm->shrink_opts & MOD_SHRINKWRAP_CULL_TARGET_MASK;
}

static void rna_ShrinkwrapGpencilModifier_face_cull_set(struct PointerRNA *ptr, int value)
{
  ShrinkwrapGpencilModifierData *swm = (ShrinkwrapGpencilModifierData *)ptr->data;
  swm->shrink_opts = (swm->shrink_opts & ~MOD_SHRINKWRAP_CULL_TARGET_MASK) | value;
}

#else

static void rna_def_modifier_gpencilnoise(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "NoiseGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Noise Modifier", "Noise effect modifier");
  RNA_def_struct_sdna(srna, "NoiseGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_NOISE);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_NoiseGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "vgname");
  RNA_def_property_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_NoiseGpencilModifier_vgname_set");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "factor", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "factor");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 1.0, 0.1, 2);
  RNA_def_property_ui_text(prop, "Offset Factor", "Amount of noise to apply");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "factor_strength", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "factor_strength");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 1.0, 0.1, 2);
  RNA_def_property_ui_text(prop, "Strength Factor", "Amount of noise to apply to opacity");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "factor_thickness", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "factor_thickness");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 1.0, 0.1, 2);
  RNA_def_property_ui_text(prop, "Thickness Factor", "Amount of noise to apply to thickness");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "factor_uvs", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "factor_uvs");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 1.0, 0.1, 2);
  RNA_def_property_ui_text(prop, "UV Factor", "Amount of noise to apply uv rotation");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_random", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_NOISE_USE_RANDOM);
  RNA_def_property_ui_text(prop, "Random", "Use random values over time");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "seed", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_ui_text(prop, "Noise Seed", "Random seed");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "noise_scale", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "noise_scale");
  RNA_def_property_range(prop, 0.0, 1.0);
  RNA_def_property_ui_text(prop, "Noise Scale", "Scale the noise frequency");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "noise_offset", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "noise_offset");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 100.0, 0.1, 3);
  RNA_def_property_ui_text(prop, "Noise Offset", "Offset the noise along the strokes");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_custom_curve", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_NOISE_CUSTOM_CURVE);
  RNA_def_property_ui_text(
      prop, "Custom Curve", "Use a custom curve to define noise effect along the strokes");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "curve", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "curve_intensity");
  RNA_def_property_ui_text(prop, "Curve", "Custom curve to apply effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "step", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "step");
  RNA_def_property_range(prop, 1, 100);
  RNA_def_property_ui_text(
      prop, "Step", "Number of frames before recalculate random values again");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_NOISE_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_NOISE_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_NOISE_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_vertex", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_NOISE_INVERT_VGROUP);
  RNA_def_property_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_NOISE_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}
static void rna_def_modifier_gpencilsmooth(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "SmoothGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Smooth Modifier", "Smooth effect modifier");
  RNA_def_struct_sdna(srna, "SmoothGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_SMOOTH);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_SmoothGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "vgname");
  RNA_def_property_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_SmoothGpencilModifier_vgname_set");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "factor", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "factor");
  RNA_def_property_range(prop, 0, 1);
  RNA_def_property_ui_text(prop, "Factor", "Amount of smooth to apply");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_edit_position", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_MOD_LOCATION);
  RNA_def_property_ui_text(
      prop, "Affect Position", "The modifier affects the position of the point");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_edit_strength", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_MOD_STRENGTH);
  RNA_def_property_ui_text(
      prop, "Affect Strength", "The modifier affects the color strength of the point");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_edit_thickness", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_MOD_THICKNESS);
  RNA_def_property_ui_text(
      prop, "Affect Thickness", "The modifier affects the thickness of the point");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_edit_uv", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_MOD_UV);
  RNA_def_property_ui_text(
      prop, "Affect UV", "The modifier affects the UV rotation factor of the point");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "step", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "step");
  RNA_def_property_range(prop, 1, 10);
  RNA_def_property_ui_text(
      prop, "Step", "Number of times to apply smooth (high numbers can reduce fps)");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_vertex", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_INVERT_VGROUP);
  RNA_def_property_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_custom_curve", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SMOOTH_CUSTOM_CURVE);
  RNA_def_property_ui_text(
      prop, "Custom Curve", "Use a custom curve to define smooth effect along the strokes");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "curve", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "curve_intensity");
  RNA_def_property_ui_text(prop, "Curve", "Custom curve to apply effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpencilsubdiv(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "SubdivGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Subdivision Modifier", "Subdivide Stroke modifier");
  RNA_def_struct_sdna(srna, "SubdivGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_SUBSURF);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_SubdivGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "level", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "level");
  RNA_def_property_range(prop, 0, 5);
  RNA_def_property_ui_text(prop, "Level", "Number of subdivisions");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "subdivision_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "type");
  RNA_def_property_enum_items(prop, gpencil_subdivision_type_items);
  RNA_def_property_ui_text(prop, "Subdivision Type", "Select type of subdivision algorithm");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SUBDIV_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SUBDIV_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SUBDIV_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SUBDIV_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpencilsimplify(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static EnumPropertyItem prop_gpencil_simplify_mode_items[] = {
      {GP_SIMPLIFY_FIXED,
       "FIXED",
       ICON_IPO_CONSTANT,
       "Fixed",
       "Delete alternating vertices in the stroke, except extremes"},
      {GP_SIMPLIFY_ADAPTIVE,
       "ADAPTIVE",
       ICON_IPO_EASE_IN_OUT,
       "Adaptive",
       "Use a Ramer-Douglas-Peucker algorithm to simplify the stroke preserving main shape"},
      {GP_SIMPLIFY_SAMPLE,
       "SAMPLE",
       ICON_IPO_EASE_IN_OUT,
       "Sample",
       "Re-sample the stroke with segments of the specified length"},
      {GP_SIMPLIFY_MERGE,
       "MERGE",
       ICON_IPO_EASE_IN_OUT,
       "Merge",
       "Simplify the stroke by merging vertices closer than a given distance"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "SimplifyGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Simplify Modifier", "Simplify Stroke modifier");
  RNA_def_struct_sdna(srna, "SimplifyGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_SIMPLIFY);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_SimplifyGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "factor", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "factor");
  RNA_def_property_range(prop, 0, 100.0);
  RNA_def_property_ui_range(prop, 0, 5.0f, 1.0f, 3);
  RNA_def_property_ui_text(prop, "Factor", "Factor of Simplify");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SIMPLIFY_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SIMPLIFY_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SIMPLIFY_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_SIMPLIFY_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  /* Mode */
  prop = RNA_def_property(srna, "mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, prop_gpencil_simplify_mode_items);
  RNA_def_property_ui_text(prop, "Mode", "How to simplify the stroke");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "step", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "step");
  RNA_def_property_range(prop, 1, 50);
  RNA_def_property_ui_text(prop, "Iterations", "Number of times to apply simplify");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  /* Sample */
  prop = RNA_def_property(srna, "length", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "length");
  RNA_def_property_range(prop, 0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0, 1.0, 0.01, 3);
  RNA_def_property_ui_text(prop, "Length", "Length of each segment");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "sharp_threshold", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "sharp_threshold");
  RNA_def_property_range(prop, 0, M_PI);
  RNA_def_property_ui_range(prop, 0, M_PI, 1.0, 1);
  RNA_def_property_ui_text(
      prop, "Sharp Threshold", "Preserve corners that have sharper angle than this threshold");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  /* Merge */
  prop = RNA_def_property(srna, "distance", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "distance");
  RNA_def_property_range(prop, 0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0, 1.0, 0.01, 3);
  RNA_def_property_ui_text(prop, "Distance", "Distance between points");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpencilthick(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "ThickGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Thick Modifier", "Subdivide and Smooth Stroke modifier");
  RNA_def_struct_sdna(srna, "ThickGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_THICKNESS);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_ThickGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "vgname");
  RNA_def_property_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_ThickGpencilModifier_vgname_set");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "thickness", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "thickness");
  RNA_def_property_range(prop, -100, 500);
  RNA_def_property_ui_text(prop, "Thickness", "Absolute thickness to apply everywhere");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "thickness_factor", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "thickness_fac");
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 10.0, 0.1, 3);
  RNA_def_property_ui_text(prop, "Thickness Factor", "Factor to multiply the thickness with");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_weight_factor", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_THICK_WEIGHT_FACTOR);
  RNA_def_property_ui_text(prop, "Weighted", "Use weight to modulate effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_THICK_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_THICK_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_THICK_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_vertex", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_THICK_INVERT_VGROUP);
  RNA_def_property_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_THICK_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_custom_curve", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_THICK_CUSTOM_CURVE);
  RNA_def_property_ui_text(
      prop, "Custom Curve", "Use a custom curve to define thickness change along the strokes");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_normalized_thickness", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_THICK_NORMALIZE);
  RNA_def_property_ui_text(prop, "Uniform Thickness", "Replace the stroke thickness");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "curve", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "curve_thickness");
  RNA_def_property_ui_text(prop, "Curve", "Custom curve to apply effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}

static void rna_def_modifier_gpenciloffset(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "OffsetGpencilModifier", "GpencilModifier");
  RNA_def_struct_ui_text(srna, "Offset Modifier", "Offset Stroke modifier");
  RNA_def_struct_sdna(srna, "OffsetGpencilModifierData");
  RNA_def_struct_ui_icon(srna, ICON_MOD_OFFSET);

  RNA_define_lib_overridable(true);

  prop = RNA_def_property(srna, "layer", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "layername");
  RNA_def_property_ui_text(prop, "Layer", "Layer name");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "material", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_pointer_funcs(prop,
                                 NULL,
                                 "rna_OffsetGpencilModifier_material_set",
                                 NULL,
                                 "rna_GpencilModifier_material_poll");
  RNA_def_property_ui_text(prop, "Material", "Material used for filtering effect");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "vertex_group", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "vgname");
  RNA_def_property_ui_text(prop, "Vertex Group", "Vertex group name for modulating the deform");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_OffsetGpencilModifier_vgname_set");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "pass_index");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_OFFSET_INVERT_LAYER);
  RNA_def_property_ui_text(prop, "Inverse Layers", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_materials", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_OFFSET_INVERT_MATERIAL);
  RNA_def_property_ui_text(prop, "Inverse Materials", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_material_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_OFFSET_INVERT_PASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_vertex", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_OFFSET_INVERT_VGROUP);
  RNA_def_property_ui_text(prop, "Inverse VertexGroup", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "layer_pass", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "layer_pass");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Pass", "Layer pass index");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "invert_layer_pass", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_OFFSET_INVERT_LAYERPASS);
  RNA_def_property_ui_text(prop, "Inverse Pass", "Inverse filter");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_float_sdna(prop, NULL, "loc");
  RNA_def_property_ui_text(prop, "Location", "Values for change location");
  RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "rotation", PROP_FLOAT, PROP_EULER);
  RNA_def_property_float_sdna(prop, NULL, "rot");
  RNA_def_property_ui_text(prop, "Rotation", "Values for changes in rotation");
  RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "scale", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_float_sdna(prop, NULL, "scale");
  RNA_def_property_ui_text(prop, "Scale", "Values for changes in scale");
  RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "random_offset", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_float_sdna(prop, NULL, "rnd_offset");
  RNA_def_property_ui_text(prop, "Random Offset", "Value for changes in location");
  RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "random_rotation", PROP_FLOAT, PROP_EULER);
  RNA_def_property_float_sdna(prop, NULL, "rnd_rot");
  RNA_def_property_ui_text(prop, "Random Rotation", "Value for changes in rotation");
  RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "random_scale", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_float_sdna(prop, NULL, "rnd_scale");
  RNA_def_property_ui_text(prop, "Scale", "Value for changes in scale");
  RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "seed", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_ui_text(prop, "Seed", "Random seed");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  prop = RNA_def_property(srna, "use_uniform_random_scale", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", GP_OFFSET_UNIFORM_RANDOM_SCALE);
  RNA_def_property_ui_text(
      prop, "Uniform Scale", "Use the same random seed for each scale axis for a uniform scale");
  RNA_def_property_update(prop, 0, "rna_GpencilModifier_update");

  RNA_define_lib_overridable(false);
}
