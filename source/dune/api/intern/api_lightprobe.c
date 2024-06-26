#include <stdlib.h>

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_lightprobe.h"

#include "wm_types.h"

#ifdef API_RUNTIME

#  include "mem_guardedalloc.h"

#  include "dune_main.h"
#  include "graph.h"

#  include "types_collection.h"
#  include "types_object.h"

#  include "wm_api.h"

static void api_LightProbe_recalc(Main *UNUSED(main), Scene *UNUSED(scene), Atr *ptr)
{
  graph_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
}

#else

static EnumPropItem parallax_type_items[] = {
    {LIGHTPROBE_SHAPE_ELIPSOID, "ELIPSOID", ICON_NONE, "Sphere", ""},
    {LIGHTPROBE_SHAPE_BOX, "BOX", ICON_NONE, "Box", ""},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropItem lightprobe_type_items[] = {
    {LIGHTPROBE_TYPE_CUBE,
     "CUBEMAP",
     ICON_LIGHTPROBE_CUBEMAP,
     "Reflection Cubemap",
     "Capture reflections"},
    {LIGHTPROBE_TYPE_PLANAR, "PLANAR", ICON_LIGHTPROBE_PLANAR, "Reflection Plane", ""},
    {LIGHTPROBE_TYPE_GRID,
     "GRID",
     ICON_LIGHTPROBE_GRID,
     "Irradiance Volume",
     "Volume used for precomputing indirect lighting"},
    {0, NULL, 0, NULL, NULL},
};

static void api_def_lightprobe(DuneApi *dapi)
{
  ApiStruct *sapu;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "LightProbe", "ID");
  api_def_struct_ui_text(
      sapi, "LightProbe", "Light Probe data-block for lighting capture objects");
  api_def_struct_ui_icon(sapi, ICON_OUTLINER_DATA_LIGHTPROBE);
  
  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, lightprobe_type_items);
  api_def_prop_ui_text(prop, "Type", "Type of light probe");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "clip_start", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "clipsta");
  api_def_prop_range(prop, 1e-6f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  api_def_prop_ui_text(
      prop, "Clip Start", "Probe clip start, below which objects will not appear in reflections");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, "rna_LightProbe_recalc");

  prop = api_def_prop(sapi, "clip_end", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "clipend");
  api_def_prop_range(prop, 1e-6f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  apu_def_prop_ui_text(
      prop, "Clip End", "Probe clip end, beyond which objects will not appear in reflections");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, "rna_LightProbe_recalc");

  prop = api_def_prop(sapi, "show_clip", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LIGHTPROBE_FLAG_SHOW_CLIP_DIST);
  api_def_prop_ui_text(prop, "Clipping", "Show the clipping distances in the 3D view");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = api_def_prop(sapi, "influence_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "attenuation_type");
  api_def_prop_enum_items(prop, parallax_type_items);
  api_def_prop_ui_text(prop, "Type", "Type of influence volume");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = api_def_prop(sapi, "show_influence", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LIGHTPROBE_FLAG_SHOW_INFLUENCE);
  api_def_prop_ui_text(prop, "Influence", "Show the influence volume in the 3D view");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = api_def_prop(sapi, "influence_distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "distinf");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_text(prop, "Influence Distance", "Influence distance of the probe");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = api_def_prop(sapi, "falloff", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Falloff", "Control how fast the probe influence decreases");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = api_def_prop(sapi, "use_custom_parallax", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LIGHTPROBE_FLAG_CUSTOM_PARALLAX);
  api_def_prop_ui_text(
      prop, "Use Custom Parallax", "Enable custom settings for the parallax correction volume");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = api_def_prop(sapi, "show_parallax", PROP_BOOLEAN, PROP_NONE);
  apu_def_prop_bool_stype(prop, NULL, "flag", LIGHTPROBE_FLAG_SHOW_PARALLAX);
  api_def_prop_ui_text(prop, "Parallax", "Show the parallax correction volume in the 3D view");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = api_def_prop(sapi, "parallax_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, parallax_type_items);
  api_def_prop_ui_text(prop, "Type", "Type of parallax volume");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = api_def_prop(sapi, "parallax_distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "distpar");
  api_def_property_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_text(prop, "Parallax Radius", "Lowest corner of the parallax bounding box");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  /* irradiance grid */
  prop = api_def_prop(sapi, "grid_resolution_x", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 1, 256);
  api_def_prop_ui_text(
      prop, "Resolution X", "Number of sample along the x axis of the volume");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, "rna_LightProbe_recalc");

  prop = api_def_prop(sapi, "grid_resolution_y", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 1, 256);
  api_def_prop_ui_text(
      prop, "Resolution Y", "Number of sample along the y axis of the volume");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, "rna_LightProbe_recalc");

  prop = api_def_prop(sapi, "grid_resolution_z", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 1, 256);
  api_def_prop_ui_text(
      prop, "Resolution Z", "Number of sample along the z axis of the volume");
  RNA_def_property_update(prop, NC_MATERIAL | ND_SHADING, "rna_LightProbe_recalc");

  prop = api_def_prop(sapi, "visibility_buffer_bias", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "vis_bias");
  api_def_prop_range(prop, 0.001f, 9999.0f);
  api_def_prop_ui_range(prop, 0.001f, 5.0f, 1.0, 3);
  api_def_prop_ui_text(prop, "Visibility Bias", "Bias for reducing self shadowing");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = api_def_prop(sapi, "visibility_bleed_bias", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "vis_bleedbias");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Visibility Bleed Bias", "Bias for reducing light-bleed on variance shadow maps");
  RNA_def_property_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  prop = RNA_def_property(srna, "visibility_blur", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "vis_blur");
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_text(prop, "Visibility Blur", "Filter size of the visibility blur");
  RNA_def_prop_update(prop, NC_MATERIAL | ND_SHADING, "rna_LightProbe_recalc");

  prop = api_def_prop(sapi, "intensity", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_sdna(prop, NULL, "intensity");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 3.0f, 1.0, 3);
  api_def_prop_ui_text(
      prop, "Intensity", "Modify the intensity of the lighting captured by this probe");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, "rna_LightProbe_recalc");

  prop = api_def_prop(sapi, "visibility_collection", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "Collection");
  api_def_prop_ptr_sapi(prop, NULL, "visibility_grp");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(
      prop, "Visibility Collection", "Restrict objects visible for this probe");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, "rna_LightProbe_recalc");

  prop = api_def_prop(sapi, "invert_visibility_collection", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LIGHTPROBE_FLAG_INVERT_GROUP);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Invert Collection", "Invert visibility collection");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, "rna_LightProbe_recalc");

  /* Data preview */
  prop = api_def_prop(sapi, "show_data", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LIGHTPROBE_FLAG_SHOW_DATA);
  api_def_prop_ui_text(prop,
                           "Show Preview Plane",
                           "Show captured lighting data into the 3D view for debugging purpose");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING, NULL);

  /* common */
  api_def_animdata_common(api);
}

void api_def_lightprobe(DuneApi *dapi)
{
  api_def_lightprobe(dapi);
}

#endif
