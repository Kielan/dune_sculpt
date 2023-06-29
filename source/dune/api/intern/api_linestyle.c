#include <stdio.h>
#include <stdlib.h>

#include "lib_utildefines.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_linestyle.h"
#include "types_material.h"
#include "types_texture.h"

#include "wm_api.h"
#include "wm_types.h"

const EnumPropItem api_enum_linestyle_color_mod_type_items[] = {
    {LS_MOD_ALONG_STROKE, "ALONG_STROKE", ICON_MOD, "Along Stroke", ""},
    {LS_MOD_CREASE_ANGLE, "CREASE_ANGLE", ICON_MOD, "Crease Angle", ""},
    {LS_MOD_CURVATURE_3D, "CURVATURE_3D", ICON_MOD, "Curvature 3D", ""},
    {LS_MOD_DISTANCE_FROM_CAMERA,
     "DISTANCE_FROM_CAMERA",
     ICON_MOD,
     "Distance from Camera",
     ""},
    {LS_MOD_DISTANCE_FROM_OBJECT,
     "DISTANCE_FROM_OBJECT",
     ICON_MOD,
     "Distance from Object",
     ""},
    {LS_MOD_MATERIAL, "MATERIAL", ICON_MODIFIER, "Material", ""},
    {LS_MOD_NOISE, "NOISE", ICON_MODIFIER, "Noise", ""},
    {LS_MOD_TANGENT, "TANGENT", ICON_MODIFIER, "Tangent", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_linestyle_alpha_mod_type_items[] = {
    {LS_MOD_ALONG_STROKE, "ALONG_STROKE", ICON_MOD, "Along Stroke", ""},
    {LS_MOD_CREASE_ANGLE, "CREASE_ANGLE", ICON_MOD, "Crease Angle", ""},
    {LS_MOD_CURVATURE_3D, "CURVATURE_3D", ICON_MOD, "Curvature 3D", ""},
    {LS_MOD_DISTANCE_FROM_CAMERA,
     "DISTANCE_FROM_CAMERA",
     ICON_MOD,
     "Distance from Camera",
     ""},
    {LS_MOD_DISTANCE_FROM_OBJECT,
     "DISTANCE_FROM_OBJECT",
     ICON_MOD,
     "Distance from Object",
     ""},
    {LS_MOD_MATERIAL, "MATERIAL", ICON_MODIFIER, "Material", ""},
    {LS_MOD_NOISE, "NOISE", ICON_MODIFIER, "Noise", ""},
    {LS_MOD_TANGENT, "TANGENT", ICON_MODIFIER, "Tangent", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_linestyle_thickness_mod_type_items[] = {
    {LS_MOD_ALONG_STROKE, "ALONG_STROKE", ICON_MOD, "Along Stroke", ""},
    {LS_MOD_CALLIGRAPHY, "CALLIGRAPHY", ICON_MOD, "Calligraphy", ""},
    {LS_MOD_CREASE_ANGLE, "CREASE_ANGLE", ICON_MOD, "Crease Angle", ""},
    {LS_MOD_CURVATURE_3D, "CURVATURE_3D", ICON_MOD, "Curvature 3D", ""},
    {LS_MOD_DISTANCE_FROM_CAMERA,
     "DISTANCE_FROM_CAMERA",
     ICON_MOD,
     "Distance from Camera",
     ""},
    {LS_MOD_DISTANCE_FROM_OBJECT,
     "DISTANCE_FROM_OBJECT",
     ICON_MOD,
     "Distance from Object",
     ""},
    {LS_MOD_MATERIAL, "MATERIAL", ICON_MOD, "Material", ""},
    {LS_MOD_NOISE, "NOISE", ICON_MOD, "Noise", ""},
    {LS_MOD_TANGENT, "TANGENT", ICON_MOD, "Tangent", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_linestyle_geometry_mod_type_items[] = {
    {LS_MOD_2D_OFFSET, "2D_OFFSET", ICON_MOD, "2D Offset", ""},
    {LS_MOD_2D_TRANSFORM, "2D_TRANSFORM", ICON_MOD, "2D Transform", ""},
    {LS_MOD_BACKBONE_STRETCHER,
     "BACKBONE_STRETCHER",
     ICON_MOD,
     "Backbone Stretcher",
     ""},
    {LS_MOD_BEZIER_CURVE, "BEZIER_CURVE", ICON_MOD, "Bezier Curve", ""},
    {LS_MOD_BLUEPRINT, "BLUEPRINT", ICON_MOD, "Blueprint", ""},
    {LS_MOD_GUIDING_LINES, "GUIDING_LINES", ICON_MOD, "Guiding Lines", ""},
    {LS_MOD_PERLIN_NOISE_1D, "PERLIN_NOISE_1D", ICON_MOD, "Perlin Noise 1D", ""},
    {LS_MOD_PERLIN_NOISE_2D, "PERLIN_NOISE_2D", ICON_MOD, "Perlin Noise 2D", ""},
    {LS_MOD_POLYGONIZATION, "POLYGONIZATION", ICON_MOD, "Polygonization", ""},
    {LS_MOD_SAMPLING, "SAMPLING", ICON_MOD, "Sampling", ""},
    {LS_MOD_SIMPLIFICATION, "SIMPLIFICATION", ICON_MOD, "Simplification", ""},
    {LS_MOD_SINUS_DISPLACEMENT,
     "SINUS_DISPLACEMENT",
     ICON_MOD,
     "Sinus Displacement",
     ""},
    {LS_MOD_SPATIAL_NOISE, "SPATIAL_NOISE", ICON_MOD, "Spatial Noise", ""},
    {LS_MOD_TIP_REMOVER, "TIP_REMOVER", ICON_MOD, "Tip Remover", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "lib_string_utils.h"

#  include "dune_linestyle.h"
#  include "dune_texture.h"

#  include "graph.h"

#  include "ed_node.h"

#  include "api_access.h"

static ApiStruct *api_LineStyle_color_mod_refine(struct ApiPtr *ptr)
{
  LineStyleMod *m = (LineStyleMod *)ptr->data;

  switch (m->type) {
    case LS_MOD_ALONG_STROKE:
      return &Api_LineStyleColorMod_AlongStroke;
    case LS_MOD_DISTANCE_FROM_CAMERA:
      return &Api_LineStyleColorMod_DistanceFromCamera;
    case LS_MOD_DISTANCE_FROM_OBJECT:
      return &Api_LineStyleColorMod_DistanceFromObject;
    case LS_MOD_MATERIAL:
      return &Api_LineStyleColorMod_Material;
    case LS_MOD_TANGENT:
      return &Api_LineStyleColorMod_Tangent;
    case LS_MOD_NOISE:
      return &Api_LineStyleColorMod_Noise;
    case LS_MOD_CREASE_ANGLE:
      return &Api_LineStyleColorMod_CreaseAngle;
    case LS_MOD_CURVATURE_3D:
      return &Api_LineStyleColorMod_Curvature_3D;
    default:
      return &Api_LineStyleColorMod;
  }
}

static ApiStrucy *api_LineStyle_alpha_mod_refine(struct ApiPtr *ptr)
{
  LineStyleMod *m = (LineStyleMod *)ptr->data;

  switch (m->type) {
    case LS_MOD_ALONG_STROKE:
      return &Api_LineStyleAlphaMod_AlongStroke;
    case LS_MOD_DISTANCE_FROM_CAMERA:
      return &Api_LineStyleAlphaMod_DistanceFromCamera;
    case LS_MOD_DISTANCE_FROM_OBJECT:
      return &Api_LineStyleAlphaMod_DistanceFromObject;
    case LS_MOD_MATERIAL:
      return &Api_LineStyleAlphaMod_Material;
    case LS_MOD_TANGENT:
      return &Api_LineStyleAlphaMod_Tangent;
    case LS_MOD_NOISE:
      return &Api_LineStyleAlphaMod_Noise;
    case LS_MOD_CREASE_ANGLE:
      return &Apo_LineStyleAlphaMod_CreaseAngle;
    case LS_MOD_CURVATURE_3D:
      return &Api_LineStyleAlphaMod_Curvature_3D;
    default:
      return &Api_LineStyleAlphaMod;
  }
}

static ApiStruct *api_LineStyle_thickness_mod_refine(struct PointerRNA *ptr)
{
  LineStyleMod *m = (LineStyleMod *)ptr->data;

  switch (m->type) {
    case LS_MOD_ALONG_STROKE:
      return &Api_LineStyleThicknessModifier_AlongStroke;
    case LS_MOD_DISTANCE_FROM_CAMERA:
      return &Api_LineStyleThicknessMod_DistanceFromCamera;
    case LS_MOD_DISTANCE_FROM_OBJECT:
      return &Api_LineStyleThicknessMod_DistanceFromObject;
    case LS_MOD_MATERIAL:
      return &Api_LineStyleThicknessMod_Material;
    case LS_MOD_CALLIGRAPHY:
      return &Api_LineStyleThicknessMod_Calligraphy;
    case LS_MOD_TANGENT:
      return &Api_LineStyleThicknessMod_Tangent;
    case LS_MOD_NOISE:
      return &Api_LineStyleThicknessMod_Noise;
    case LS_MOD_CREASE_ANGLE:
      return &Api_LineStyleThicknessMod_CreaseAngle;
    case LS_MOD_CURVATURE_3D:
      return &Api_LineStyleThicknessMod_Moisture_3D;
    default:
      return &Api_LineStyleThicknessMod
  }
}

static ApiStruct *api_LineStyle_geometry_mod_refine(struct ApiPtr *ptr)
{
  LineStyleMod *m = (LineStyleMod *)ptr->data;

  switch (m->type) {
    case LS_MOD_SAMPLING:
      return &Api_LineStyleGeometryMod_Sampling;
    case LS_MOD_BEZIER_CURVE:
      return &Api_LineStyleGeometryMod_BezierCurve;
    case LS_MOD_SINUS_DISPLACEMENT:
      return &Api_LineStyleGeometryMod_SinusDisplacement;
    case LS_MOD_SPATIAL_NOISE:
      return &Api_LineStyleGeometryMod_SpatialNoise;
    case LS_MOD_PERLIN_NOISE_1D:
      return &Api_LineStyleGeometryMod_PerlinNoise1D;
    case LS_MOD_PERLIN_NOISE_2D:
      return &Api_LineStyleGeometryMod_PerlinNoise2D;
    case LS_MOD_BACKBONE_STRETCHER:
      return &Api_LineStyleGeometryMod_BackboneStretcher;
    case LS_MOD_TIP_REMOVER:
      return &Api_LineStyleGeometryMod_TipRemover;
    case LS_MOD_POLYGONIZATION:
      return &Api_LineStyleGeometryMod_Polygonalization;
    case LS_MOD_GUIDING_LINES:
      return &Api_LineStyleGeometryMod_GuidingLines;
    case LS_MOD_BLUEPRINT:
      return &Api_LineStyleGeometryMod_Blueprint;
    case LS_MOD_2D_OFFSET:
      return &Api_LineStyleGeometryMod_2DOffset;
    case LS_MOD_2D_TRANSFORM:
      return &Api_LineStyleGeometryMod_2DTransform;
    case LS_MOD_SIMPLIFICATION:
      return &Api_LineStyleGeometryMod_Simplification;
    default:
      return &Api_LineStyleGeometryMod;
  }
}

static char *api_LineStyle_color_mod_path(ApiPtr *ptr)
{
  LineStyle *m = (LineStyleModifier *)ptr->data;
  char name_esc[sizeof(m->name) * 2];

  lib_str_escape(name_esc, m->name, sizeof(name_esc));
  return lib_("color_modifiers[\"%s\"]", name_esc);
}

static char *api_LineStyle_alpha_mod_path(ApiPtr *ptr)
{
  LineStyleMod *m = (LineStyleMod *)ptr->data;
  char name_esc[sizeof(m->name) * 2];
  lib_str_escape(name_esc, m->name, sizeof(name_esc));
  return lib_sprintfn("alpha_modifiers[\"%s\"]", name_esc);
}

static char *api_LineStyle_thickness_mod_path(ApiPtr *ptr)
{
  LineStyleMod *m = (LineStyleMod *)ptr->data;
  char name_esc[sizeof(m->name) * 2];
  lib_str_escape(name_esc, m->name, sizeof(name_esc));
  return lib_sprintfn("thickness_mods[\"%s\"]", name_esc);
}

static char *api_LineStyle_geometry_mod_path(ApiPtr *ptr)
{
  LineStyleMod *m = (LineStyleMod *)ptr->data;
  char name_esc[sizeof(m->name) * 2];
  lib_str_escape(name_esc, m->name, sizeof(name_esc));
  return lib_sprintfn("geometry_mods[\"%s\"]", name_esc);
}

static void api_LineStyleColorMod_name_set(ApiPtr *ptr, const char *value)
{
  FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->owner_id;
  LineStyleMod *m = (LineMod *)ptr->data;

  lib_strncpy_utf8(m->name, value, sizeof(m->name));
  lib_uniquename(&linestyle->color_mods,
                 m,
                 "ColorMod",
                 '.',
                 offsetof(LineStyleMod, name),
                 sizeof(m->name));
}

static void api_LineStyleAlphaMod_name_set(ApiPtr *ptr, const char *value)
{
  FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->owner_id;
  LineStyleMod *m = (LineStyleMod *)ptr->data;

  lib_strncpy_utf8(m->name, value, sizeof(m->name));
  lib_uniquename(&linestyle->alpha_modifiers,
                 m,
                 "AlphaMod",
                 '.',
                 offsetof(LineStyleMod, name),
                 sizeof(m->name));
}

static void api_LineStyleThicknessMod_name_set(ApiPtr *ptr, const char *value)
{
  FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->owner_id;
  LineStyleMod *m = (LineStyleMod *)ptr->data;

  lib_strncpy_utf8(m->name, value, sizeof(m->name));
  lib_uniquename(&linestyle->thickness_mods,
                 m,
                 "ThicknessMod",
                 '.',
                 offsetof(LineStyleMod, name),
                 sizeof(m->name));
}

static void api_LineStyleGeometryMod_name_set(ApiPtr *ptr, const char *value)
{
  FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->owner_id;
  LineStyleMod *m = (LineStyleMod *)ptr->data;

  lib_strncpy_utf8(m->name, value, sizeof(m->name));
  lib_uniquename(&linestyle->geometry_mods,
                 m,
                 "GeometryMod",
                 '.',
                 offsetof(LineStyleMod, name),
                 sizeof(m->name));
}

static void api_LineStyle_mtex_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->owner_id;
  api_iter_array_begin(iter, (void *)linestyle->mtex, sizeof(MeshTex *), MAX_MTEX, 0, NULL);
}

static ApiPtr api_LineStyle_active_texture_get(ApiPtr *ptr)
{
  FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->owner_id;
  Tex *tex;

  tex = give_current_linestyle_texture(linestyle);
  return api_ptr_inherit_refine(ptr, &Api_Texture, tex);
}

static void api_LineStyle_active_texture_set(ApiPtr *ptr,
                                             ApiPtr value,
                                             struct ReportList *UNUSED(reports))
{
  FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->owner_id;

  set_current_linestyle_texture(linestyle, value.data);
}

static void api_LineStyle_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->owner_id;

  graph_id_tag_update(&linestyle->id, 0);
  wm_main_add_notifier(NC_LINESTYLE, linestyle);
}

static void api_LineStyle_use_nodes_update(Cxt *C, ApiPtr *ptr)
{
  FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->data;

  if (linestyle->use_nodes && linestyle->nodetree == NULL) {
    dune_linestyle_default_shader(C, linestyle);
  }

  api_LineStyle_update(cxt_data_main(C), cxt_data_scene(C), ptr);
}

static LineStyleMod *api_LineStyle_color_mod_add(FreestyleLineStyle *linestyle,
                                                 ReportList *reports,
                                                 const char *name,
                                                 int type)
{
  LineStyleMod *mod = dune_linestyle_color_mod_add(linestyle, name, type);

  if (!mod) {
    dune_report(reports, RPT_ERROR, "Failed to add the color modifier");
    return NULL;
  }

  graph_id_tag_update(&linestyle->id, 0);
  wm_main_add_notifier(NC_LINESTYLE, linestyle);

  return mod;
}

static void api_LineStyle_color_mod_remove(FreestyleLineStyle *linestyle,
                                           ReportList *reports,
                                           ApiPtr *mod_ptr)
{
  LineStyleMod *mod = mod_ptr->data;

  if (dune_linestyle_color_mod_remove(linestyle, mod) == -1) {
    dune_reportf(reports, RPT_ERROR, "Color modifier '%s' could not be removed", modifier->name);
    return;
  }

  API_PTR_INVALIDATE(mod_ptr);

  graph_id_tag_update(&linestyle->id, 0);
  wm_main_add_notifier(NC_LINESTYLE, linestyle);
}

static LineStyleMod *api_LineStyle_alpha_mod_add(FreestyleLineStyle *linestyle,
                                                 ReportList *reports,
                                                 const char *name,
                                                 int type)
{
  LineStyleMod *mod = dune_linestyle_alpha_mod_add(linestyle, name, type);

  if (!mod) {
    dune_report(reports, RPT_ERROR, "Failed to add the alpha modifier");
    return NULL;
  }

  graph_id_tag_update(&linestyle->id, 0);
  wm_main_add_notifier(NC_LINESTYLE, linestyle);

  return mod;
}

static void api_LineStyle_alpha_mod_remove(FreestyleLineStyle *linestyle,
                                           ReportList *reports,
                                           ApiPtr *mod_ptr)
{
  LineStyleMod *mod = mod_ptr->data;

  if (dune_linestyle_alpha_mod_remove(linestyle, modifier) == -1);
    dune_reportf(reports, RPT_ERROR, "Alpha modifier '%s' could not be removed", modifier->name);
    return;
  }

  API_PTR_INVALIDATE(mod_ptr);

  graph_id_tag_update(&linestyle->id, 0);
  wm_main_add_notifier(NC_LINESTYLE, linestyle);
}

static LineStyleMod *api_LineStyle_thickness_mod_add(FreestyleLineStyle *linestyle,
                                                     ReportList *reports,
                                                     const char *name,
                                                     int type)
{
  LineStyleMod *mod = dune_linestyle_thickness_mod_add(linestyle, name, type);

  if (!mod) {
    dune_report(reports, RPT_ERROR, "Failed to add the thickness modifier");
    return NULL;
  }

  graph_id_tag_update(&linestyle->id, 0);
  wm_main_add_notifier(NC_LINESTYLE, linestyle);

  return mod;
}

static void api_LineStyle_thickness_mod_remove(FreestyleLineStyle *linestyle,
                                               ReportList *reports,
                                               ApiPtr *mod_ptr)
{
  LineStyleMod *mod = mod_ptr->data;

  if (dune_linestyle_thickness_mod_remove(linestyle, mod) == -1) {
    dune_reportf(
        reports, RPT_ERROR, "Thickness mod '%s' could not be removed", modifier->name);
    return;
  }

  API_PTR_INVALIDATE(mod_ptr);

  graph_id_tag_update(&linestyle->id, 0);
  wm_main_add_notifier(NC_LINESTYLE, linestyle);
}

static LineStyleMod *api_LineStyle_geometry_mod_add(FreestyleLineStyle *linestyle,
                                                    ReportList *reports,
                                                    const char *name,
                                                    int type)
{
  LineStyleMod *mod = dune_linestyle_geometry_mod_add(linestyle, name, type);

  if (!modifier) {
    dune_report(reports, RPT_ERROR, "Failed to add the geometry modifier");
    return NULL;
  }

  graph_id_tag_update(&linestyle->id, 0);
  wm_main_add_notifier(NC_LINESTYLE, linestyle);

  return mod;
}

static void api_LineStyle_geometry_mod_remove(FreestyleLineStyle *linestyle,
                                              ReportList *reports,
                                              ApiPtr *mod_ptr)
{
  LineStyleod *mod = mod_ptr->data;
  if (dune_linestyle_geometry_mod_remove(linestyle, mod) == -1) {
    dune_reportf(reports, RPT_ERROR, "Geometry mod '%s' could not be removed", modifier->name);
    return;
  }

  API_PTR_INVALIDATE(mod_ptr);

  graph_id_tag_update(&linestyle->id, 0);
  wm_main_add_notifier(NC_LINESTYLE, linestyle);
}

#else

#  include "lib_math.h"

static void api_def_linestyle_mtex(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem texco_items[] = {
      {TEXCO_WINDOW, "WINDOW", 0, "Window", "Use screen coordinates as texture coordinates"},
      {TEXCO_GLOB, "GLOBAL", 0, "Global", "Use global coordinates for the texture coordinates"},
      {TEXCO_STROKE,
       "ALONG_STROKE",
       0,
       "Along stroke",
       "Use stroke length for texture coordinates"},
      {TEXCO_ORCO,
       "ORCO",
       0,
       "Generated",
       "Use the original undeformed coordinates of the object"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_mapping_items[] = {
      {MTEX_FLAT, "FLAT", 0, "Flat", "Map X and Y coordinates directly"},
      {MTEX_CUBE, "CUBE", 0, "Cube", "Map using the normal vector"},
      {MTEX_TUBE, "TUBE", 0, "Tube", "Map with Z as central axis"},
      {MTEX_SPHERE, "SPHERE", 0, "Sphere", "Map with Z as central axis"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_x_mapping_items[] = {
      {0, "NONE", 0, "None", ""},
      {1, "X", 0, "X", ""},
      {2, "Y", 0, "Y", ""},
      {3, "Z", 0, "Z", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_y_mapping_items[] = {
      {0, "NONE", 0, "None", ""},
      {1, "X", 0, "X", ""},
      {2, "Y", 0, "Y", ""},
      {3, "Z", 0, "Z", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_z_mapping_items[] = {
      {0, "NONE", 0, "None", ""},
      {1, "X", 0, "X", ""},
      {2, "Y", 0, "Y", ""},
      {3, "Z", 0, "Z", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "LineStyleTextureSlot", "TextureSlot");
  api_def_struct_stype(sapi, "MTex");
  api_def_struct_ui_text(
      sapi, "LineStyle Texture Slot", "Texture slot for textures in a LineStyle data-block");

  prop = api_def_prop(sapi, "mapping_x", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "projx");
  api_def_prop_enum_items(prop, prop_x_mapping_items);
  api_def_prop_ui_text(prop, "X Mapping", "");
  api_def_prop_update(prop, 0, "api_LineStyle_update");

  prop = api_def_prop(sapi, "mapping_y", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "projy");
  api_def_prop_enum_items(prop, prop_y_mapping_items);
  api_def_prop_ui_text(prop, "Y Mapping", "");
  api_def_prop_update(prop, 0, "api_LineStyle_update");

  prop = api_def_prop(sapi, "mapping_z", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "projz");
  api_def_prop_enum_items(prop, prop_z_mapping_items);
  api_def_prop_ui_text(prop, "Z Mapping", "");
  api_def_prop_update(prop, 0, "api_LineStyle_update");

  prop = api_def_prop(sapi, "mapping", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, prop_mapping_items);
  api_def_prop_ui_text(prop, "Mapping", "");
  api_def_prop_update(prop, 0, "api_LineStyle_update");

  /* map to */
  prop = api_def_prop(sapi, "use_map_color_diffuse", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "mapto", MAP_COL);
  api_def_prop_ui_text(prop, "Diffuse Color", "The texture affects basic color of the stroke");
  api_def_prop_update(prop, 0, "api_LineStyle_update");

  prop = api_def_prop(sapi, "use_map_alpha", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mapto", MAP_ALPHA);
  api_def_prop_ui_text(prop, "Alpha", "The texture affects the alpha value");
  api_def_prop_update(prop, 0, "api_LineStyle_update");

  prop = api_def_prop(sapi, "texture_coords", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "texco");
  api_def_prop_enum_items(prop, texco_items);
  api_def_prop_ui_text(prop,
                       "Texture Coordinates",
                       "Texture coordinates used to map the texture onto the background");
  api_def_prop_update(prop, 0, "api_LineStyle_update");

  prop = api_def_prop(sapi, "alpha_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "alphafac");
  api_def_prop_ui_range(prop, -1, 1, 10, 3);
  api_def_prop_ui_text(prop, "Alpha Factor", "Amount texture affects alpha");
  api_def_prop_update(prop, 0, "api_LineStyle_update");

  prop = api_def_prop(sapi, "diffuse_color_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "colfac");
  api_def_prop_ui_range(prop, 0, 1, 10, 3);
  api_def_prop_ui_text(prop, "Diffuse Color Factor", "Amount texture affects diffuse color");
  apu_def_prop_update(prop, 0, "api_LineStyle_update");
}

static void api_def_mod_type_common(ApiStruct *sapi,
                                    const EnumPropItem *mod_type_items,
                                    const char *set_name_fn,
                                    const bool blend,
                                    const bool color)
{
  ApiProp *prop;

  /* TODO: Check this is not already defined somewhere else, e.g. in nodes... */
  static const EnumPropItem value_blend_items[] = {
      {LS_VALUE_BLEND, "MIX", 0, "Mix", ""},
      {LS_VALUE_ADD, "ADD", 0, "Add", ""},
      {LS_VALUE_SUB, "SUBTRACT", 0, "Subtract", ""},
      {LS_VALUE_MULT, "MULTIPLY", 0, "Multiply", ""},
      {LS_VALUE_DIV, "DIVIDE", 0, "Divide", ""},
      {LS_VALUE_DIFF, "DIFFERENCE", 0, "Difference", ""},
      {LS_VALUE_MIN, "MINIMUM", 0, "Minimum", ""},
      {LS_VALUE_MAX, "MAXIMUM", 0, "Maximum", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mod.type");
  api_def_prop_enum_items(prop, mod_type_items);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Mod Type", "Type of the modifier");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "mod.name");
  api_def_prop_string_fns(prop, NULL, NULL, set_name_func);
  api_def_prop_ui_text(prop, "Modifier Name", "Name of the modifier");
  api_def_prop_update(prop, NC_LINESTYLE, NULL);
  api_def_struct_name_prop(sapi, prop);

  if (blend) {
    prop = api_def_prop(sapi, "blend", PROP_ENUM, PROP_NONE);
    api_def_prop_enum_stype(prop, NULL, "modifier.blend");
    api_def_prop_enum_items(prop, (color) ? rna_enum_ramp_blend_items : value_blend_items);
    api_def_prop_ui_text(
        prop, "Blend", "Specify how the modifier value is blended into the base value");
    api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

    prop = api_def_prop(sapi, "influence", PROP_FLOAT, PROP_FACTOR);
    api_def_prop_float_stype(prop, NULL, "modifier.influence");
    api_def_prop_range(prop, 0.0f, 1.0f);
    api_def_prop_ui_text(
        prop, "Influence", "Influence factor by which the modifier changes the property");
    api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");
  }

  prop = api_def_prop(sapi, "use", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "modifier.flags", LS_MODIFIER_ENABLED);
  api_def_prop_ui_text(prop, "Use", "Enable or disable this modifier during stroke rendering");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = api_def_prop(sapi, "expanded", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "modifier.flags", LS_MODIFIER_EXPANDED);
  api_def_prop_ui_text(prop, "Expanded", "True if the modifier tab is expanded");
}

static void api_def_color_mod(ApiStruct *sapi)
{
  api_def_mod_type_common(sapi,
                          api_enum_linestyle_color_mod_type_items,
                          "api_LineStyleColorMod_name_set",
                          true,
                          true);
}

static void api_def_alpha_mod(ApiStruct *sapi)
{
  api_def_mod_type_common(sapi,
                          api_enum_linestyle_alpha_mod_type_items,
                          "api_LineStyleAlphaMod_name_set",
                          true,
                          false);
}

static void api_def_thickness_mod(ApiStruct *sapi)
{
  api_def_mod_type_common(sapi,
                          api_enum_linestyle_thickness_mod_type_items,
                          "api_LineStyleThicknessMod_name_set",
                          true,
                          false);
}

static void api_def_geometry_mod(ApiStruct *sapi)
{
  api_def_mod_type_common(sapi,
                          api_enum_linestyle_geometry_mod_type_items,
                          "api_LineStyleGeometryMod_name_set",
                          false,
                          false);
}

static void api_def_mod_color_ramp_common(ApiStruct *sapi, int range)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "color_ramp", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "color_ramp");
  api_def_prop_struct_type(prop, "ColorRamp");
  api_def_prop_ui_text(prop, "Color Ramp", "Color ramp used to change line color");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  if (range) {
    prop = api_def_prop(sapi, "range_min", PROP_FLOAT, PROP_DISTANCE);
    api_def_prop_float_stype(prop, NULL, "range_min");
    api_def_prop_ui_text(
        prop, "Range Min", "Lower bound of the input range the mapping is applied");
    api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

    prop = api_def_prop(sapi, "range_max", PROP_FLOAT, PROP_DISTANCE);
    api_def_prop_float_stype(prop, NULL, "range_max");
    api_def_prop_ui_text(
        prop, "Range Max", "Upper bound of the input range the mapping is applied");
    api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");
  }
}

static void api_def_mod_curve_common(ApiStruct *sapi, bool range, bool value)
{
  ApiProp *prop;

  static const EnumPropItem mapping_items[] = {
      {0, "LINEAR", 0, "Linear", "Use linear mapping"},
      {LS_MOD_USE_CURVE, "CURVE", 0, "Curve", "Use curve mapping"},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "mapping", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flags");
  api_def_prop_enum_items(prop, mapping_items);
  api_def_prop_ui_text(prop, "Mapping", "Select the mapping type");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = api_def_prop(sapi, "invert", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", LS_MODIFIER_INVERT);
  api_def_prop_ui_text(prop, "Invert", "Invert the fade-out direction of the linear mapping");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = api_def_prop(sapi, "curve", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve");
  api_def_prop_struct_type(prop, "CurveMapping");
  api_def_prop_ui_text(prop, "Curve", "Curve used for the curve mapping");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  if (range) {
    prop = api_def_prop(sapi, "range_min", PROP_FLOAT, PROP_DISTANCE);
    api_def_prop_float_stype(prop, NULL, "range_min");
    api_def_prop_ui_text(
        prop, "Range Min", "Lower bound of the input range the mapping is applied");
    api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

    prop = api_def_prop(sapi, "range_max", PROP_FLOAT, PROP_DISTANCE);
    api_def_prop_float_stype(prop, NULL, "range_max");
    api_def_prop_ui_text(
        prop, "Range Max", "Upper bound of the input range the mapping is applied");
    api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");
  }

  if (value) {
    prop = api_def_prop(sapi, "value_min", PROP_FLOAT, PROP_NONE);
    api_def_prop_float_stype(prop, NULL, "value_min");
    api_def_prop_ui_text(prop, "Value Min", "Minimum output value of the mapping");
    api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

    prop = api_def_prop(sapi, "value_max", PROP_FLOAT, PROP_NONE);
    api_def_prop_float_stype(prop, NULL, "value_max");
    api_def_prop_ui_text(prop, "Value Max", "Maximum output value of the mapping");
    api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");
  }
}

static void api_def_mod_material_common(ApiStruct *sapi)
{
  ApiProp *prop;

  static const EnumPropItem mat_attr_items[] = {
      {LS_MOD_MATERIAL_LINE, "LINE", 0, "Line Color", ""},
      {LS_MOD_MATERIAL_LINE_R, "LINE_R", 0, "Line Color Red", ""},
      {LS_MOD_MATERIAL_LINE_G, "LINE_G", 0, "Line Color Green", ""},
      {LS_MOD_MATERIAL_LINE_B, "LINE_B", 0, "Line Color Blue", ""},
      {LS_MOD_MATERIAL_LINE_A, "LINE_A", 0, "Line Color Alpha", ""},
      {LS_MOD_MATERIAL_DIFF, "DIFF", 0, "Diffuse Color", ""},
      {LS_MOD_MATERIAL_DIFF_R, "DIFF_R", 0, "Diffuse Color Red", ""},
      {LS_MOD_MATERIAL_DIFF_G, "DIFF_G", 0, "Diffuse Color Green", ""},
      {LS_MOD_MATERIAL_DIFF_B, "DIFF_B", 0, "Diffuse Color Blue", ""},
      {LS_MOD_MATERIAL_SPEC, "SPEC", 0, "Specular Color", ""},
      {LS_MOD_MATERIAL_SPEC_R, "SPEC_R", 0, "Specular Color Red", ""},
      {LS_MOD_MATERIAL_SPEC_G, "SPEC_G", 0, "Specular Color Green", ""},
      {LS_MOD_MATERIAL_SPEC_B, "SPEC_B", 0, "Specular Color Blue", ""},
      {LS_MOD_MATERIAL_SPEC_HARD, "SPEC_HARD", 0, "Specular Hardness", ""},
      {LS_MOD_MATERIAL_ALPHA, "ALPHA", 0, "Alpha Transparency", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "material_attribute", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mat_attr");
  api_def_prop_enum_items(prop, mat_attr_items);
  api_def_prop_ui_text(prop, "Material Attribute", "Specify which material attribute is used");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");
}

static void api_def_linestyle_mods(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApoProp *prop;

  static const EnumPropItem blueprint_shape_items[] = {
      {LS_MOD_BLUEPRINT_CIRCLES,
       "CIRCLES",
       0,
       "Circles",
       "Draw a blueprint using circular contour strokes"},
      {LS_MOD_BLUEPRINT_ELLIPSES,
       "ELLIPSES",
       0,
       "Ellipses",
       "Draw a blueprint using elliptic contour strokes"},
      {LS_MOD_BLUEPRINT_SQUARES,
       "SQUARES",
       0,
       "Squares",
       "Draw a blueprint using square contour strokes"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem transform_pivot_items[] = {
      {LS_MOD_2D_TRANSFORM_PIVOT_CENTER, "CENTER", 0, "Stroke Center", ""},
      {LS_MOD_2D_TRANSFORM_PIVOT_START, "START", 0, "Stroke Start", ""},
      {LS_MOD_2D_TRANSFORM_PIVOT_END, "END", 0, "Stroke End", ""},
      {LS_MOD_2D_TRANSFORM_PIVOT_PARAM, "PARAM", 0, "Stroke Point Parameter", ""},
      {LS_MOD_2D_TRANSFORM_PIVOT_ABSOLUTE, "ABSOLUTE", 0, "Absolute 2D Point", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "LineStyle", NULL);
  api_def_struct_ui_text(sapi, "Line Style Mod", "Base type to define mods");

  /* line color modifiers */
  sapi = api_def_struct(dapi, "LineStyleColorMod", "LineStyleMod");
  api_def_struct_stype(sapi, "LineStyleMod");
  api_def_struct_refine_fn(sapi, "api_LineStyle_color_mod_refine");
  api_def_struct_path_fn(sapi, "api_LineStyle_color_mod_path");
  api_def_struct_ui_text(
      sapi, "Line Style Color Mod", "Base type to define line color modifiers");

  srna = api_def_struct(dapi, "LineStyleColorMod_AlongStroke", "LineStyleColorModifier");
  api_def_struct_ui_text(sapi, "Along Stroke", "Change line color along stroke");
  api_def_color_mod(sapi);
  api_def_mod_color_ramp_common(sapi, false);

  sapi = api_def_struct(
      dapi, "LineStyleColorMod_DistanceFromCamera", "LineStyleColorModifier");
  api_def_struct_ui_text(
      sapi, "Distance from Camera", "Change line color based on the distance from the camera");
  api_def_color_mod(sapi);
  api_def_mod_color_ramp_common(srna, true);

  sapi = api_def_struct(
      dapi, "LineStyleColorMod_DistanceFromObject", "LineStyleColorMod");
  api_def_struct_ui_text(
      sapi, "Distance from Object", "Change line color based on the distance from an object");
  api_def_color_mod(sapi);
  api_def_mod_color_ramp_common(sapi, true);

  prop = api_def_prop(sapi, "target", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "target");
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Target", "Target object from which the distance is measured");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(dapi, "LineStyleColorMod_Material", "LineStyleColorModifier");
  api_def_struct_ui_text(sapi, "Material", "Change line color based on a material attribute");
  api_def_color_mod(sapi);
  api_def_mod_material_common(sapi);
  api_def_mod_color_ramp_common(sapi, false);

  prop = api_def_prop(sapi, "use_ramp", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", LS_MOD_USE_RAMP);
  api_def_prop_ui_text(prop, "Ramp", "Use color ramp to map the BW average into an RGB color");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(dapi, "LineStyleColorMod_Tangent", "LineStyleColorMod");
  api_def_struct_ui_text(sapi, "Tangent", "Change line color based on the direction of a stroke");
  api_def_color_mod(sapi);
  api_def_mod_color_ramp_common(sapi, false);

  sapi = api_def_struct(dapi, "LineStyleColorMod_Noise", "LineStyleColorMod");
  api_def_struct_ui_text(sapi, "Noise", "Change line color based on random noise");
  api_def_color_mod(sapi);
  api_def_mod_color_ramp_common(sapi, false);

  prop = api_def_prop(sapi, "amplitude", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "amplitude");
  api_def_prop_ui_text(prop, "Amplitude", "Amplitude of the noise");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "period", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "period");
  api_def_prop_ui_text(prop, "Period", "Period of the noise");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "seed", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "seed");
  api_def_prop_range(prop, 1, SHRT_MAX);
  api_def_prop_ui_text(prop, "Seed", "Seed for the noise generation");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(dapi, "LineStyleColorMod_CreaseAngle", "LineStyleColorMod");
  api_def_struct_ui_text(
      srna, "Crease Angle", "Change line color based on the underlying crease angle");
  api_def_color_mod(sapi);
  api_def_mod_color_ramp_common(srna, false);

  prop = api_def_prop(sapi, "angle_min", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "min_angle");
  api_def_prop_ui_text(prop, "Min Angle", "Minimum angle to modify thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "angle_max", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "max_angle");
  api_def_prop_ui_text(prop, "Max Angle", "Maximum angle to modify thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(dapi, "LineStyleColorMod_Curvature_3D", "LineStyleColorMod");
  api_def_struct_ui_text(
      sapi, "Curvature 3D", "Change line color based on the radial curvature of 3D mesh surfaces");
  api_def_color_mod(sapi);
  api_def_mod_color_ramp_common(srna, false);

  prop = api_def_prop(sapi, "curvature_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "min_curvature");
  api_def_prop_ui_text(prop, "Min Curvature", "Minimum Curvature");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "curvature_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_curvature");
  api_def_prop_ui_text(prop, "Max Curvature", "Maximum Curvature");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  /* alpha transparency modifiers */
  srna = api_def_struct(dapi, "LineStyleAlphaMod", "LineStyleMod");
  api_def_struct_stype(sapi, "LineStyleMod");
  api_def_struct_refine_fn(sapi, "api_LineStyle_alpha_mod_refine");
  api_def_struct_path_fn(sapi, "api_LineStyle_alpha_mod_path");
  api_def_struct_ui_text(
      sapu, "Line Style Alpha Modifier", "Base type to define alpha transparency mod");

  sapi = api_def_struct(dapi, "LineStyleAlphaMod_AlongStroke", "LineStyleAlphaMod");
  api_def_struct_ui_text(sapi, "Along Stroke", "Change alpha transparency along stroke");
  sapi_def_alpha_mod(sapi);
  api_def_mod_curve_common(sapi, false, false);

  sapi = api_def_struct(
      dapi, "LineStyleAlphaMod_DistanceFromCamera", "LineStyleAlphaMod");
  api_def_struct_ui_text(sapi,
                         "Distance from Camera",
                         "Change alpha transparency based on the distance from the camera");
  api_def_alpha_mod(sapi);
  api_def_mod_curve_common(sapi, true, false);

  sapi = api_def_struct(
      dapi, "LineStyleAlphaMod_DistanceFromObject", "LineStyleAlphaMod");
  api_def_struct_ui_text(sapi,
                         "Distance from Object",
                         "Change alpha transparency based on the distance from an object");
  api_def_alpha_mod(sapi);
  api_def_mod_curve_common(sapi, true, false);

  prop = api_def_prop(sapi, "target", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "target");
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Target", "Target object from which the distance is measured");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(dapi, "LineStyleModMaterial", "LineStyleAlphaMod");
  api_def_struct_ui_text(
      srna, "Material", "Change alpha transparency based on a material attribute");
  api_def_alpha_mod(sapi);
  api_def_mod_material_common(sapi);
  api_def_mod_curve_common(sapi, false, false);

  sapi = api_def_struct(dapi, "LineStyleAlphaMod_Tangent", "LineStyleAlphaMod");
  api_def_struct_ui_text(
      sapi, "Tangent", "Alpha transparency based on the direction of the stroke");
  api_def_alpha_mod(sapi);
  api_def_mod_curve_common(sapi, false, false);

  sapi = api_def_struct(dapi, "LineStyleAlphaMod_Noise", "LineStyleAlphaModifier");
  api_def_struct_ui_text(sapi, "Noise", "Alpha transparency based on random noise");
  api_def_alpha_mod(sapi);
  api_def_mod_curve_common(sapi, false, false);

  prop = api_def_prop(sapi, "amplitude", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "amplitude");
  api_def_prop_ui_text(prop, "Amplitude", "Amplitude of the noise");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = api_def_prop(sapi, "period", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "period");
  api_def_prop_ui_text(prop, "Period", "Period of the noise");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "seed", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "seed");
  api_def_prop_range(prop, 1, SHRT_MAX);
  api_def_prop_ui_text(prop, "Seed", "Seed for the noise generation");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  sapi = api_def_struct(dapi, "LineStyleAlphaModifier_CreaseAngle", "LineStyleAlphaModifier");
  api_def_struct_ui_text(
       sapi, "Crease Angle", "Alpha transparency based on the angle between two adjacent faces");
  api_def_alpha_mod(sapi);
  api_def_mod_curve_common(sapi, false, false);

  prop = api_def_prop(sapi, "angle_min", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "min_angle");
  api_def_prop_ui_text(prop, "Min Angle", "Minimum angle to modify thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "angle_max", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "max_angle");
  api_def_prop_ui_text(prop, "Max Angle", "Maximum angle to modify thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(dapi, "LineStyleAlphaMod_Curvature_3D", "LineStyleAlphaMod");
  api_def_struct_ui_text(sapi,
                         "Curvature 3D",
                         "Alpha transparency based on the radial curvature of 3D mesh surfaces");
  api_def_alpha_mod(sapi);
  api_def_mod_curve_common(sapi, false, false);

  prop = api_def_prop(sapi, "curvature_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "min_curvature");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Min Curvature", "Minimum Curvature");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = api_def_prop(sapi, "curvature_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_curvature");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Max Curvature", "Maximum Curvature");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  /* line thickness modifiers */
  srna = api_def_struct(dapi, "LineStyleThicknessModifier", "LineStyleModifier");
  api_def_struct_stype(sapi, "LineStyleMod");
  api_def_struct_refine_fn(sapi, "spi_LineStyle_thickness_modifier_refine");
  api_def_struct_path_fn(sapi, "api_LineStyle_thickness_modifier_path");
  api_def_struct_ui_text(
      srna, "Line Style Thickness Modifier", "Base type to define line thickness modifiers");

  srna = api_def_struct(dapi, "LineStyleThicknessMod_Tangent", "LineStyleThicknessModifier");
  api_def_struct_ui_text(sapi, "Tangent", "Thickness based on the direction of the stroke");
  api_def_thickness_mod(sapi);
  api_def_mod_curve_common(srna, false, false);

  prop = api_def_prop(sapi, "thickness_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "min_thickness");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Min Thickness", "Minimum thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = api_def_prop(sapi, "thickness_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_thickness");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Max Thickness", "Maximum thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  sapi = api_def_struct(
      dapi, "LineStyleThicknessModifier_AlongStroke", "LineStyleThicknessModifier");
  api_def_struct_ui_text(sapi, "Along Stroke", "Change line thickness along stroke");
  api_def_thickness_mod(sapi);
  api_def_mod_curve_common(sapi, false, true);

  sapi = api_def_struct(
      dapi, "LineStyleThicknessMod_DistanceFromCamera", "LineStyleThicknessModifier");
  api_def_struct_ui_text(
      sapi, "Distance from Camera", "Change line thickness based on the distance from the camera");
  api_def_thickness_mod(sapi);
  api_def_mod_curve_common(sapi, true, true);

  sapi = api_def_struct(
      dapi, "LineStyleThicknessMod_DistanceFromObject", "LineStyleThicknessModifier");
  api_def_struct_ui_text(
      sapi, "Distance from Object", "Change line thickness based on the distance from an object");
  api_def_thickness_mod(sapi);
  api_def_mod_curve_common(sapi, true, true);

  prop = api_def_prop(sapi, "target", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "target");
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Target", "Target object from which the distance is measured");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(dapi, "LineStyleThicknessMod_Material", "LineStyleThicknessModifier");
  api_def_struct_ui_text(sapi, "Material", "Change line thickness based on a material attribute");
  api_def_thickness_mod(sapi);
  api_def_mod_material_common(sapi);
  api_def_mod_curve_common(sapi, false, true);

  sapi = api_def_struct(
      dapi, "LineStyleThicknessMod_Calligraphy", "LineStyleThicknessModifier");
  api_def_struct_ui_text(
      sapi,
      "Calligraphy",
      "Change line thickness so that stroke looks like made with a calligraphic pen");
  api_def_thickness_mod(sapi);

  prop = api_def_prop(sapi, "orientation", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_style(prop, NULL, "orientation");
  api_def_prop_ui_text(prop, "Orientation", "Angle of the main direction");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "thickness_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "min_thickness");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(
      prop,
      "Min Thickness",
      "Minimum thickness in the direction perpendicular to the main direction");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "thickness_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_thickness");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Max Thickness", "Maximum thickness in the main direction");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");
    
  sapi = api_def_struct(dapi, "LineStyleThicknessMod_Noise", "LineStyleThicknessModifier");
  api_def_struct_ui_text(sapi, "Noise", "Line thickness based on random noise");
  api_def_thickness_mod(sapi);

  prop = api_def_prop(sapi, "amplitude", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "amplitude");
  api_def_prop_ui_text(prop, "Amplitude", "Amplitude of the noise");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(dapi, "period", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "period");
  api_def_prop_ui_text(prop, "Period", "Period of the noise");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "seed", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "seed");
  api_def_prop_range(prop, 1, SHRT_MAX);
  api_def_prop_ui_text(prop, "Seed", "Seed for the noise generation");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "use_asymmetric", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", LS_THICKNESS_ASYMMETRIC);
  api_def_prop_ui_text(prop, "Asymmetric", "Allow thickness to be assigned asymmetrically");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(
      dapi, "LineStyleThicknessMod_Curvature_3D", "LineStyleThicknessModifier");
  api_def_struct_ui_text(
      sapi, "Curvature 3D", "Line thickness based on the radial curvature of 3D mesh surfaces");
  api_def_thickness_mod(srna);
  api_def_mod_curve_common(srna, false, false);

  prop = api_def_prop(sapi, "thickness_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "min_thickness");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Min Thickness", "Minimum thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "thickness_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_thickness");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Max Thickness", "Maximum thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "curvature_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "min_curvature");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Min Curvature", "Minimum Curvature");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "curvature_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_curvature");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Max Curvature", "Maximum Curvature");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(
      dapi, "LineStyleThicknessMod_CreaseAngle", "LineStyleThicknessModifier");
  api_def_struct_ui_text(
      sapi, "Crease Angle", "Line thickness based on the angle between two adjacent faces");
  api_def_thickness_mod(sapi);
  api_def_mod_curve_common(sapi, false, false);

  prop = api_def_prop(sapi, "angle_min", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "min_angle");
  api_def_prop_ui_text(prop, "Min Angle", "Minimum angle to modify thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "angle_max", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "max_angle");
  api_def_prop_ui_text(prop, "Max Angle", "Maximum angle to modify thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "thickness_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_sype(prop, NULL, "min_thickness");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Min Thickness", "Minimum thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  prop = api_def_prop(sapi, "thickness_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_thickness");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Max Thickness", "Maximum thickness");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  /* geometry modifiers */
  srna = api_def_struct(dapi, "LineStyleGeometryMod", "LineStyleMod");
  api_def_struct_stype(sapi, "LineStyleModifier");
  api_def_struct_refine_fn(sapi, "api_LineStyle_geometry_mod_refine");
  api_def_struct_path_fn(sapi, "api_LineStyle_geometry_mod_path");
  api_def_struct_ui_text(
      sapi, "Line Style Geometry Modifier", "Base type to define stroke geometry modifiers");

  srna = api_def_struct(dapi, "LineStyleGeometryModifier_Sampling", "LineStyleGeometryModifier");
  api_def_struct_ui_text(
      sapi,
      "Sampling",
      "Specify a new sampling value that determines the resolution of stroke polylines");
  api_def_geometry_mod(sapi);

  prop = api_def_prop(sapi, "sampling", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "sampling");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(
      prop, "Sampling", "New sampling value to be used for subsequent mods");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(
      dapi, "LineStyleGeometryMod_BezierCurve", "LineStyleGeometryMod");
  api_def_struct_ui_text(sapi,
                         "Bezier Curve",
                         "Replace stroke backbone geometry by a Bezier curve approximation of the "
                         "original backbone geometry");
  api_def_geometry_mod(sapi);

  prop = api_def_prop(sapi, "error", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "error");
  api_def_prop_ui_text(prop,
                           "Error",
                           "Maximum distance allowed between the new Bezier curve and the "
                           "original backbone geometry");
  api_def_prop_update(prop, NC_LINESTYLE, "api_LineStyle_update");

  sapi = api_def_struct(
      dapi, "LineStyleGeometryMod_SinusDisplacement", "LineStyleGeometryModifier");
  api_def_struct_ui_text(
      sapi, "Sinus Displacement", "Add sinus displacement to stroke backbone geometry");
  api_def_geometry_mod(sapi);

  prop = api_def_prop(sapi, "wavelength", PROP_FLOAT, PROP_UNSIGNED);
  api_def_prop_float_stype(prop, NULL, "wavelength");
  api_def_prop_range(prop, 0.0001f, FLT_MAX);
  api_def_prop_ui_text(prop, "Wavelength", "Wavelength of the sinus displacement");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = api_def_prop(sapi, "amplitude", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "amplitude");
  api_def_prop_ui_text(prop, "Amplitude", "Amplitude of the sinus displacement");
  api_def_prop_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = api_def_prop(sapi, "phase", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "phase");
  RNA_def_property_ui_text(prop, "Phase", "Phase of the sinus displacement");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(
      brna, "LineStyleGeometryModifier_SpatialNoise", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(srna, "Spatial Noise", "Add spatial noise to stroke backbone geometry");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "amplitude", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "amplitude");
  RNA_def_property_ui_text(prop, "Amplitude", "Amplitude of the spatial noise");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "scale");
  RNA_def_property_ui_text(prop, "Scale", "Scale of the spatial noise");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "octaves", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "octaves");
  RNA_def_property_ui_text(
      prop, "Octaves", "Number of octaves (i.e., the amount of detail of the spatial noise)");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "smooth", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LS_MODIFIER_SPATIAL_NOISE_SMOOTH);
  RNA_def_property_ui_text(prop, "Smooth", "If true, the spatial noise is smooth");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_pure_random", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", LS_MODIFIER_SPATIAL_NOISE_PURERANDOM);
  RNA_def_property_ui_text(
      prop, "Pure Random", "If true, the spatial noise does not show any coherence");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(
      brna, "LineStyleGeometryModifier_PerlinNoise1D", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(
      srna, "Perlin Noise 1D", "Add one-dimensional Perlin noise to stroke backbone geometry");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "frequency", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "frequency");
  RNA_def_property_ui_text(prop, "Frequency", "Frequency of the Perlin noise");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "amplitude", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "amplitude");
  RNA_def_property_ui_text(prop, "Amplitude", "Amplitude of the Perlin noise");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "octaves", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "octaves");
  RNA_def_property_ui_text(
      prop, "Octaves", "Number of octaves (i.e., the amount of detail of the Perlin noise)");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "angle", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "angle");
  RNA_def_property_ui_text(prop, "Angle", "Displacement direction");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "seed", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "seed");
  RNA_def_property_ui_text(
      prop,
      "Seed",
      "Seed for random number generation (if negative, time is used as a seed instead)");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(
      brna, "LineStyleGeometryModifier_PerlinNoise2D", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(
      srna, "Perlin Noise 2D", "Add two-dimensional Perlin noise to stroke backbone geometry");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "frequency", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "frequency");
  RNA_def_property_ui_text(prop, "Frequency", "Frequency of the Perlin noise");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "amplitude", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "amplitude");
  RNA_def_property_ui_text(prop, "Amplitude", "Amplitude of the Perlin noise");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "octaves", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "octaves");
  RNA_def_property_ui_text(
      prop, "Octaves", "Number of octaves (i.e., the amount of detail of the Perlin noise)");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "angle", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "angle");
  RNA_def_property_ui_text(prop, "Angle", "Displacement direction");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "seed", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "seed");
  RNA_def_property_ui_text(
      prop,
      "Seed",
      "Seed for random number generation (if negative, time is used as a seed instead)");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(
      brna, "LineStyleGeometryModifier_BackboneStretcher", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(
      srna, "Backbone Stretcher", "Stretch the beginning and the end of stroke backbone");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "backbone_length", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "backbone_length");
  RNA_def_property_ui_text(prop, "Backbone Length", "Amount of backbone stretching");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(brna, "LineStyleGeometryModifier_TipRemover", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(
      srna,
      "Tip Remover",
      "Remove a piece of stroke at the beginning and the end of stroke backbone");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "tip_length", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "tip_length");
  RNA_def_property_ui_text(prop, "Tip Length", "Length of tips to be removed");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(
      brna, "LineStyleGeometryModifier_Polygonalization", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(
      srna, "Polygonalization", "Modify the stroke geometry so that it looks more 'polygonal'");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "error", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "error");
  RNA_def_property_ui_text(
      prop,
      "Error",
      "Maximum distance between the original stroke and its polygonal approximation");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(
      brna, "LineStyleGeometryModifier_GuidingLines", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(
      srna,
      "Guiding Lines",
      "Modify the stroke geometry so that it corresponds to its main direction line");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "offset", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "offset");
  RNA_def_property_ui_text(
      prop, "Offset", "Displacement that is applied to the main direction line along its normal");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(brna, "LineStyleGeometryModifier_Blueprint", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(
      srna,
      "Blueprint",
      "Produce a blueprint using circular, elliptic, and square contour strokes");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "shape", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_bitflag_sdna(prop, NULL, "flags");
  RNA_def_property_enum_items(prop, blueprint_shape_items);
  RNA_def_property_ui_text(prop, "Shape", "Select the shape of blueprint contour strokes");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "rounds", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "rounds");
  RNA_def_property_range(prop, 1, 1000);
  RNA_def_property_ui_text(prop, "Rounds", "Number of rounds in contour strokes");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "backbone_length", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "backbone_length");
  RNA_def_property_ui_text(prop, "Backbone Length", "Amount of backbone stretching");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "random_radius", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "random_radius");
  RNA_def_property_ui_text(prop, "Random Radius", "Randomness of the radius");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "random_center", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "random_center");
  RNA_def_property_ui_text(prop, "Random Center", "Randomness of the center");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "random_backbone", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "random_backbone");
  RNA_def_property_ui_text(prop, "Random Backbone", "Randomness of the backbone stretching");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(brna, "LineStyleGeometryModifier_2DOffset", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(
      srna, "2D Offset", "Add two-dimensional offsets to stroke backbone geometry");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "start", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "start");
  RNA_def_property_ui_text(
      prop, "Start", "Displacement that is applied from the beginning of the stroke");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "end", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "end");
  RNA_def_property_ui_text(prop, "End", "Displacement that is applied from the end of the stroke");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "x", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "x");
  RNA_def_property_ui_text(
      prop, "X", "Displacement that is applied to the X coordinates of stroke vertices");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "y", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "y");
  RNA_def_property_ui_text(
      prop, "Y", "Displacement that is applied to the Y coordinates of stroke vertices");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(
      brna, "LineStyleGeometryModifier_2DTransform", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(srna,
                         "2D Transform",
                         "Apply two-dimensional scaling and rotation to stroke backbone geometry");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "pivot", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "pivot");
  RNA_def_property_enum_items(prop, transform_pivot_items);
  RNA_def_property_ui_text(prop, "Pivot", "Pivot of scaling and rotation operations");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "scale_x", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "scale_x");
  RNA_def_property_flag(prop, PROP_PROPORTIONAL);
  RNA_def_property_ui_text(prop, "Scale X", "Scaling factor that is applied along the X axis");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "scale_y", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "scale_y");
  RNA_def_property_flag(prop, PROP_PROPORTIONAL);
  RNA_def_property_ui_text(prop, "Scale Y", "Scaling factor that is applied along the Y axis");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "angle", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "angle");
  RNA_def_property_ui_text(prop, "Rotation Angle", "Rotation angle");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "pivot_u", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "pivot_u");
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_text(prop,
                           "Stroke Point Parameter",
                           "Pivot in terms of the stroke point parameter u (0 <= u <= 1)");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "pivot_x", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "pivot_x");
  RNA_def_property_ui_text(prop, "Pivot X", "2D X coordinate of the absolute pivot");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "pivot_y", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "pivot_y");
  RNA_def_property_ui_text(prop, "Pivot Y", "2D Y coordinate of the absolute pivot");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  srna = RNA_def_struct(
      brna, "LineStyleGeometryModifier_Simplification", "LineStyleGeometryModifier");
  RNA_def_struct_ui_text(srna, "Simplification", "Simplify the stroke set");
  rna_def_geometry_modifier(srna);

  prop = RNA_def_property(srna, "tolerance", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "tolerance");
  RNA_def_property_ui_text(prop, "Tolerance", "Distance below which segments will be merged");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");
}

static void rna_def_freestyle_color_modifiers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "LineStyleColorModifiers");
  srna = RNA_def_struct(brna, "LineStyleColorModifiers", NULL);
  RNA_def_struct_sdna(srna, "FreestyleLineStyle");
  RNA_def_struct_ui_text(srna, "Color Modifiers", "Color modifiers for changing line colors");

  func = RNA_def_function(srna, "new", "rna_LineStyle_color_modifier_add");
  RNA_def_function_ui_description(func, "Add a color modifier to line style");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_string(
      func, "name", "ColorModifier", 0, "", "New name for the color modifier (not unique)");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(func,
                      "type",
                      rna_enum_linestyle_color_modifier_type_items,
                      0,
                      "",
                      "Color modifier type to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(
      func, "modifier", "LineStyleColorModifier", "", "Newly added color modifier");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_LineStyle_color_modifier_remove");
  RNA_def_function_ui_description(func, "Remove a color modifier from line style");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(
      func, "modifier", "LineStyleColorModifier", "", "Color modifier to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void rna_def_freestyle_alpha_modifiers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "LineStyleAlphaModifiers");
  srna = RNA_def_struct(brna, "LineStyleAlphaModifiers", NULL);
  RNA_def_struct_sdna(srna, "FreestyleLineStyle");
  RNA_def_struct_ui_text(srna, "Alpha Modifiers", "Alpha modifiers for changing line alphas");

  func = RNA_def_function(srna, "new", "rna_LineStyle_alpha_modifier_add");
  RNA_def_function_ui_description(func, "Add a alpha modifier to line style");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_string(
      func, "name", "AlphaModifier", 0, "", "New name for the alpha modifier (not unique)");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(func,
                      "type",
                      rna_enum_linestyle_alpha_modifier_type_items,
                      0,
                      "",
                      "Alpha modifier type to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(
      func, "modifier", "LineStyleAlphaModifier", "", "Newly added alpha modifier");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_LineStyle_alpha_modifier_remove");
  RNA_def_function_ui_description(func, "Remove a alpha modifier from line style");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(
      func, "modifier", "LineStyleAlphaModifier", "", "Alpha modifier to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void rna_def_freestyle_thickness_modifiers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "LineStyleThicknessModifiers");
  srna = RNA_def_struct(brna, "LineStyleThicknessModifiers", NULL);
  RNA_def_struct_sdna(srna, "FreestyleLineStyle");
  RNA_def_struct_ui_text(
      srna, "Thickness Modifiers", "Thickness modifiers for changing line thickness");

  func = RNA_def_function(srna, "new", "rna_LineStyle_thickness_modifier_add");
  RNA_def_function_ui_description(func, "Add a thickness modifier to line style");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_string(func,
                        "name",
                        "ThicknessModifier",
                        0,
                        "",
                        "New name for the thickness modifier (not unique)");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(func,
                      "type",
                      rna_enum_linestyle_thickness_modifier_type_items,
                      0,
                      "",
                      "Thickness modifier type to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(
      func, "modifier", "LineStyleThicknessModifier", "", "Newly added thickness modifier");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_LineStyle_thickness_modifier_remove");
  RNA_def_function_ui_description(func, "Remove a thickness modifier from line style");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(
      func, "modifier", "LineStyleThicknessModifier", "", "Thickness modifier to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void rna_def_freestyle_geometry_modifiers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "LineStyleGeometryModifiers");
  srna = RNA_def_struct(brna, "LineStyleGeometryModifiers", NULL);
  RNA_def_struct_sdna(srna, "FreestyleLineStyle");
  RNA_def_struct_ui_text(
      srna, "Geometry Modifiers", "Geometry modifiers for changing line geometries");

  func = RNA_def_function(srna, "new", "rna_LineStyle_geometry_modifier_add");
  RNA_def_function_ui_description(func, "Add a geometry modifier to line style");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_string(
      func, "name", "GeometryModifier", 0, "", "New name for the geometry modifier (not unique)");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(func,
                      "type",
                      rna_enum_linestyle_geometry_modifier_type_items,
                      0,
                      "",
                      "Geometry modifier type to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(
      func, "modifier", "LineStyleGeometryModifier", "", "Newly added geometry modifier");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_LineStyle_geometry_modifier_remove");
  RNA_def_function_ui_description(func, "Remove a geometry modifier from line style");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(
      func, "modifier", "LineStyleGeometryModifier", "", "Geometry modifier to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void rna_def_linestyle(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem panel_items[] = {
    {LS_PANEL_STROKES, "STROKES", 0, "Strokes", "Show the panel for stroke construction"},
    {LS_PANEL_COLOR, "COLOR", 0, "Color", "Show the panel for line color options"},
    {LS_PANEL_ALPHA, "ALPHA", 0, "Alpha", "Show the panel for alpha transparency options"},
    {LS_PANEL_THICKNESS, "THICKNESS", 0, "Thickness", "Show the panel for line thickness options"},
    {LS_PANEL_GEOMETRY, "GEOMETRY", 0, "Geometry", "Show the panel for stroke geometry options"},
    {LS_PANEL_TEXTURE, "TEXTURE", 0, "Texture", "Show the panel for stroke texture options"},
#  if 0 /* hidden for now */
    {LS_PANEL_MISC, "MISC", 0, "Misc", "Show the panel for miscellaneous options"},
#  endif
    {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropertyItem chaining_items[] = {
      {LS_CHAINING_PLAIN, "PLAIN", 0, "Plain", "Plain chaining"},
      {LS_CHAINING_SKETCHY, "SKETCHY", 0, "Sketchy", "Sketchy chaining with a multiple touch"},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropertyItem cap_items[] = {
      {LS_CAPS_BUTT, "BUTT", 0, "Butt", "Butt cap (flat)"},
      {LS_CAPS_ROUND, "ROUND", 0, "Round", "Round cap (half-circle)"},
      {LS_CAPS_SQUARE, "SQUARE", 0, "Square", "Square cap (flat and extended)"},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropertyItem thickness_position_items[] = {
      {LS_THICKNESS_CENTER,
       "CENTER",
       0,
       "Center",
       "Silhouettes and border edges are centered along stroke geometry"},
      {LS_THICKNESS_INSIDE,
       "INSIDE",
       0,
       "Inside",
       "Silhouettes and border edges are drawn inside of stroke geometry"},
      {LS_THICKNESS_OUTSIDE,
       "OUTSIDE",
       0,
       "Outside",
       "Silhouettes and border edges are drawn outside of stroke geometry"},
      {LS_THICKNESS_RELATIVE,
       "RELATIVE",
       0,
       "Relative",
       "Silhouettes and border edges are shifted by a user-defined ratio"},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropertyItem sort_key_items[] = {
      {LS_SORT_KEY_DISTANCE_FROM_CAMERA,
       "DISTANCE_FROM_CAMERA",
       0,
       "Distance from Camera",
       "Sort by distance from camera (closer lines lie on top of further lines)"},
      {LS_SORT_KEY_2D_LENGTH,
       "2D_LENGTH",
       0,
       "2D Length",
       "Sort by curvilinear 2D length (longer lines lie on top of shorter lines)"},
      {LS_SORT_KEY_PROJECTED_X,
       "PROJECTED_X",
       0,
       "Projected X",
       "Sort by the projected X value in the image coordinate system"},
      {LS_SORT_KEY_PROJECTED_Y,
       "PROJECTED_Y",
       0,
       "Projected Y",
       "Sort by the projected Y value in the image coordinate system"},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropertyItem sort_order_items[] = {
      {0, "DEFAULT", 0, "Default", "Default order of the sort key"},
      {LS_REVERSE_ORDER, "REVERSE", 0, "Reverse", "Reverse order"},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropertyItem integration_type_items[] = {
      {LS_INTEGRATION_MEAN,
       "MEAN",
       0,
       "Mean",
       "The value computed for the chain is the mean of the values obtained for chain vertices"},
      {LS_INTEGRATION_MIN,
       "MIN",
       0,
       "Min",
       "The value computed for the chain is the minimum of the values obtained for chain "
       "vertices"},
      {LS_INTEGRATION_MAX,
       "MAX",
       0,
       "Max",
       "The value computed for the chain is the maximum of the values obtained for chain "
       "vertices"},
      {LS_INTEGRATION_FIRST,
       "FIRST",
       0,
       "First",
       "The value computed for the chain is the value obtained for the first chain vertex"},
      {LS_INTEGRATION_LAST,
       "LAST",
       0,
       "Last",
       "The value computed for the chain is the value obtained for the last chain vertex"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "FreestyleLineStyle", "ID");
  RNA_def_struct_ui_text(
      srna, "Freestyle Line Style", "Freestyle line style, reusable by multiple line sets");
  RNA_def_struct_ui_icon(srna, ICON_LINE_DATA);

  rna_def_mtex_common(brna,
                      srna,
                      "rna_LineStyle_mtex_begin",
                      "rna_LineStyle_active_texture_get",
                      "rna_LineStyle_active_texture_set",
                      NULL,
                      "LineStyleTextureSlot",
                      "LineStyleTextureSlots",
                      "rna_LineStyle_update",
                      "rna_LineStyle_update");

  prop = RNA_def_property(srna, "panel", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_bitflag_sdna(prop, NULL, "panel");
  RNA_def_property_enum_items(prop, panel_items);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_ui_text(prop, "Panel", "Select the property panel to be shown");

  prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
  RNA_def_property_float_sdna(prop, NULL, "r");
  RNA_def_property_array(prop, 3);
  RNA_def_property_ui_text(
      prop, "Color", "Base line color, possibly modified by line color modifiers");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "alpha", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "alpha");
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_text(
      prop,
      "Alpha Transparency",
      "Base alpha transparency, possibly modified by alpha transparency modifiers");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "thickness", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "thickness");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_ui_text(
      prop, "Thickness", "Base line thickness, possibly modified by line thickness modifiers");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "thickness_position", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_bitflag_sdna(prop, NULL, "thickness_position");
  RNA_def_property_enum_items(prop, thickness_position_items);
  RNA_def_property_ui_text(prop,
                           "Thickness Position",
                           "Thickness position of silhouettes and border edges (applicable when "
                           "plain chaining is used with the Same Object option)");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "thickness_ratio", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "thickness_ratio");
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_text(
      prop,
      "Thickness Ratio",
      "A number between 0 (inside) and 1 (outside) specifying the relative position of "
      "stroke thickness");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "color_modifiers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "color_modifiers", NULL);
  RNA_def_property_struct_type(prop, "LineStyleColorModifier");
  RNA_def_property_ui_text(prop, "Color Modifiers", "List of line color modifiers");
  rna_def_freestyle_color_modifiers(brna, prop);

  prop = RNA_def_property(srna, "alpha_modifiers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "alpha_modifiers", NULL);
  RNA_def_property_struct_type(prop, "LineStyleAlphaModifier");
  RNA_def_property_ui_text(prop, "Alpha Modifiers", "List of alpha transparency modifiers");
  rna_def_freestyle_alpha_modifiers(brna, prop);

  prop = RNA_def_property(srna, "thickness_modifiers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "thickness_modifiers", NULL);
  RNA_def_property_struct_type(prop, "LineStyleThicknessModifier");
  RNA_def_property_ui_text(prop, "Thickness Modifiers", "List of line thickness modifiers");
  rna_def_freestyle_thickness_modifiers(brna, prop);

  prop = RNA_def_property(srna, "geometry_modifiers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "geometry_modifiers", NULL);
  RNA_def_property_struct_type(prop, "LineStyleGeometryModifier");
  RNA_def_property_ui_text(prop, "Geometry Modifiers", "List of stroke geometry modifiers");
  rna_def_freestyle_geometry_modifiers(brna, prop);

  prop = RNA_def_property(srna, "use_chaining", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", LS_NO_CHAINING);
  RNA_def_property_ui_text(prop, "Chaining", "Enable chaining of feature edges");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "chaining", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "chaining");
  RNA_def_property_enum_items(prop, chaining_items);
  RNA_def_property_ui_text(
      prop, "Chaining Method", "Select the way how feature edges are jointed to form chains");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "rounds", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "rounds");
  RNA_def_property_range(prop, 1, 1000);
  RNA_def_property_ui_text(prop, "Rounds", "Number of rounds in a sketchy multiple touch");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_same_object", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_SAME_OBJECT);
  RNA_def_property_ui_text(
      prop, "Same Object", "If true, only feature edges of the same object are joined");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_split_length", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_SPLIT_LENGTH);
  RNA_def_property_ui_text(
      prop, "Use Split Length", "Enable chain splitting by curvilinear 2D length");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "split_length", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "split_length");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_ui_text(prop, "Split Length", "Curvilinear 2D length for chain splitting");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_angle_min", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_MIN_2D_ANGLE);
  RNA_def_property_ui_text(prop,
                           "Use Min 2D Angle",
                           "Split chains at points with angles smaller than the minimum 2D angle");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "angle_min", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "min_angle");
  RNA_def_property_range(prop, 0.0f, DEG2RADF(180.0f));
  RNA_def_property_ui_text(prop, "Min 2D Angle", "Minimum 2D angle for splitting chains");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_angle_max", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_MAX_2D_ANGLE);
  RNA_def_property_ui_text(prop,
                           "Use Max 2D Angle",
                           "Split chains at points with angles larger than the maximum 2D angle");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "angle_max", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "max_angle");
  RNA_def_property_range(prop, 0.0f, DEG2RADF(180.0f));
  RNA_def_property_ui_text(prop, "Max 2D Angle", "Maximum 2D angle for splitting chains");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_length_min", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_MIN_2D_LENGTH);
  RNA_def_property_ui_text(
      prop, "Use Min 2D Length", "Enable the selection of chains by a minimum 2D length");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "length_min", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "min_length");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_ui_text(
      prop, "Min 2D Length", "Minimum curvilinear 2D length for the selection of chains");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_length_max", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_MAX_2D_LENGTH);
  RNA_def_property_ui_text(
      prop, "Use Max 2D Length", "Enable the selection of chains by a maximum 2D length");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "length_max", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "max_length");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_ui_text(
      prop, "Max 2D Length", "Maximum curvilinear 2D length for the selection of chains");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_chain_count", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_CHAIN_COUNT);
  RNA_def_property_ui_text(prop, "Use Chain Count", "Enable the selection of first N chains");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "chain_count", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "chain_count");
  RNA_def_property_ui_text(prop, "Chain Count", "Chain count for the selection of first N chains");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_split_pattern", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_SPLIT_PATTERN);
  RNA_def_property_ui_text(
      prop, "Use Split Pattern", "Enable chain splitting by dashed line patterns");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "split_dash1", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "split_dash1");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Split Dash 1", "Length of the 1st dash for splitting");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "split_gap1", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "split_gap1");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Split Gap 1", "Length of the 1st gap for splitting");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "split_dash2", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "split_dash2");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Split Dash 2", "Length of the 2nd dash for splitting");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "split_gap2", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "split_gap2");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Split Gap 2", "Length of the 2nd gap for splitting");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "split_dash3", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "split_dash3");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Split Dash 3", "Length of the 3rd dash for splitting");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "split_gap3", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "split_gap3");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Split Gap 3", "Length of the 3rd gap for splitting");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "material_boundary", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_MATERIAL_BOUNDARY);
  RNA_def_property_ui_text(prop,
                           "Material Boundary",
                           "If true, chains of feature edges are split at material boundaries");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_sorting", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", LS_NO_SORTING);
  RNA_def_property_ui_text(prop, "Sorting", "Arrange the stacking order of strokes");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "sort_key", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "sort_key");
  RNA_def_property_enum_items(prop, sort_key_items);
  RNA_def_property_ui_text(
      prop, "Sort Key", "Select the sort key to determine the stacking order of chains");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "sort_order", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
  RNA_def_property_enum_items(prop, sort_order_items);
  RNA_def_property_ui_text(prop, "Sort Order", "Select the sort order");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "integration_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "integration_type");
  RNA_def_property_enum_items(prop, integration_type_items);
  RNA_def_property_ui_text(
      prop, "Integration Type", "Select the way how the sort key is computed for each chain");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_dashed_line", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_DASHED_LINE);
  RNA_def_property_ui_text(prop, "Dashed Line", "Enable or disable dashed line");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "caps", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_bitflag_sdna(prop, NULL, "caps");
  RNA_def_property_enum_items(prop, cap_items);
  RNA_def_property_ui_text(prop, "Caps", "Select the shape of both ends of strokes");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "dash1", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "dash1");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Dash 1", "Length of the 1st dash for dashed lines");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "gap1", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "gap1");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Gap 1", "Length of the 1st gap for dashed lines");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "dash2", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "dash2");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Dash 2", "Length of the 2nd dash for dashed lines");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "gap2", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "gap2");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Gap 2", "Length of the 2nd gap for dashed lines");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "dash3", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "dash3");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Dash 3", "Length of the 3rd dash for dashed lines");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "gap3", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "gap3");
  RNA_def_property_range(prop, 0, USHRT_MAX);
  RNA_def_property_ui_text(prop, "Gap 3", "Length of the 3rd gap for dashed lines");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "use_texture", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", LS_TEXTURE);
  RNA_def_property_ui_text(prop, "Use Textures", "Enable or disable textured strokes");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  prop = RNA_def_property(srna, "texture_spacing", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "texstep");
  RNA_def_property_range(prop, 0.01f, 100.0f);
  RNA_def_property_ui_text(prop, "Texture Spacing", "Spacing for textures along stroke length");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_update");

  /* anim */
  rna_def_animdata_common(srna);

  /* nodes */
  prop = RNA_def_property(srna, "node_tree", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "nodetree");
  RNA_def_property_clear_flag(prop, PROP_PTR_NO_OWNERSHIP);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Node Tree", "Node tree for node-based shaders");

  prop = RNA_def_property(srna, "use_nodes", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "use_nodes", 1);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
  RNA_def_property_ui_text(prop, "Use Nodes", "Use shader nodes for the line style");
  RNA_def_property_update(prop, NC_LINESTYLE, "rna_LineStyle_use_nodes_update");
}

void RNA_def_linestyle(BlenderRNA *brna)
{
  rna_def_linestyle_modifiers(brna);
  rna_def_linestyle(brna);
  rna_def_linestyle_mtex(brna);
}

#endif
