#include <float.h>
#include <stdlib.h>

#include "api_define.h"

#include "api_internal.h"

#include "types_material_types.h"
#include "types_texture_types.h"
#include "types_world_types.h"

#include "wm_types.h"

#ifdef API_RUNTIME

#  include "mem_guardedalloc.h"

#  include "dune_ctx.h"
#  include "dune_layer.h"
#  include "dune_main.h"
#  include "dune_texture.h"

#  include "DEG_depsgraph.h"
#  include "DEG_depsgraph_build.h"

#  include "ED_node.h"

#  include "WM_api.h"

static PointerRNA rna_World_lighting_get(PointerRNA *ptr)
{
  return rna_pointer_inherit_refine(ptr, &RNA_WorldLighting, ptr->owner_id);
}

static ApiPtr api_World_mist_get(PointerRNA *ptr)
{
  return api_ptr_inherit_refine(ptr, &RNA_WorldMistSettings, ptr->owner_id);
}

static void api_World_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  World *wo = (World *)ptr->owner_id;

  graph_id_tag_update(&wo->id, 0);
  wm_main_add_notifier(NC_WORLD | ND_WORLD, wo);
}

#  if 0
static void api_World_draw_update(Main *UNUSED(main), Scene *UNUSED(scene), PointerRNA *ptr)
{
  World *wo = (World *)ptr->owner_id;

  graph_id_tag_update(&wo->id, 0);
  wm_main_add_notifier(NC_WORLD | ND_WORLD_DRAW, wo);
}
#  endif

static void api_World_draw_update(Main *UNUSED(main), Scene *UNUSED(scene), PointerRNA *ptr)
{
  World *wo = (World *)ptr->owner_id;

  graph_id_tag_update(&wo->id, 0);
  wm_main_add_notifier(NC_WORLD | ND_WORLD_DRAW, wo);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
}

static void api_World_use_nodes_update(bContext *C, PointerRNA *ptr)
{
  World *wrld = (World *)ptr->data;
  Main *main = cxt_data_main(C);
  Scene *scene = ctx_data_scene(C);

  if (wrld->use_nodes && wrld->nodetree == NULL) {
    ed_node_shader_default(C, &wrld->id);
  }

  graph_relations_tag_update(pmain);
  apo_World_update(main, scene, ptr);
  api_World_draw_update(main, scene, ptr);
}

void rna_World_lightgroup_get(PointerRNA *ptr, char *value)
{
  LightgroupMembership *lgm = ((World *)ptr->owner_id)->lightgroup;
  char value_buf[sizeof(lgm->name)];
  int len = dune_lightgroup_membership_get(lgm, value_buf);
  memcpy(value, value_buf, len + 1);
}

int rna_World_lightgroup_length(PointerRNA *ptr)
{
  LightgroupMembership *lgm = ((World *)ptr->owner_id)->lightgroup;
  return dune_lightgroup_membership_length(lgm);
}

void api_World_lightgroup_set(PointerRNA *ptr, const char *value)
{
  dune_lightgroup_membership_set(&((World *)ptr->owner_id)->lightgroup, value);
}

#else

static void api_def_lighting(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(sapi, "WorldLighting", NULL);
  api_def_struct_sdna(sapi, "World");
  api_def_struct_nested(dapi, sapi, "World");
  api_def_struct_ui_text(sapi, "Lighting", "Lighting for a World data-block");

  /* ambient occlusion */
  prop = api_def_prop(sapi, "use_ambient_occlusion", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mode", WO_AMB_OCC);
  api_def_prop_ui_text(
      prop,
      "Use Ambient Occlusion",
      "Use Ambient Occlusion to add shadowing based on distance between objects");
  api_def_prop_update(prop, 0, "rna_World_update");

  prop = api_def_prop(sapi, "ao_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "aoenergy");
  api_def_prope_range(prop, 0, INT_MAX);
  api_def_prop_ui_range(prop, 0, 1, 0.1, 2);
  api_def_prop_ui_text(prop, "Factor", "Factor for ambient occlusion blending");
  api_def_prop_update(prop, 0, "rna_World_update");

  prop = api_def_prop(sapi, "distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_pro_float_sdna(prop, NULL, "aodist");
  api_def_prop_range(prop, 0, FLT_M
  api_def_prop_ui_text(
      prop, "Distance", "Length of rays, defines how far away other faces give occlusion effect");
  api_def_prop_update(prop, 0, "rna_World_update");
}

static void api_def_world_mist(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem falloff_items[] = {
      {WO_MIST_QUADRATIC, "QUADRATIC", 0, "Quadratic", "Use quadratic progression"},
      {WO_MIST_LINEAR, "LINEAR", 0, "Linear", "Use linear progression"},
      {WO_MIST_INVERSE_QUADRATIC,
       "INVERSE_QUADRATIC",
       0,
       "Inverse Quadratic",
       "Use inverse quadratic progression"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "WorldMistSettings", NULL);
  api_def_struct_stype(sapi, "World");
  api_def_struct_nested(dapi, sapi, "World");
  api_def_struct_ui_text(sapi, "World Mist", "Mist settings for a World data-block");

  prop = api_def_prop(sapi, "use_mist", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mode", WO_MIST);
  api_def_prop_ui_text(
      prop, "Use Mist", "Occlude objects with the environment color as they are further away");
  api_def_prop_update(prop, 0, "rna_World_draw_update");

  prop = api_def_prop(spia, "intensity", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "misi")
  api_def_prop_range(prop, 0, 1);
  api_def_prop_ui_text(prop, "Minimum", "Overall minimum intensity of the mist effect");
  api_def_prop_update(prop, 0, "rna_World_draw_update");

  prop = api_def_prop(sapi, "start", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_sdna(prop, NULL, "miststa");
  api_def_prop_range(prop, 0, FLT_MAX);
  api_def_prop_ui_range(prop, 0, 10000, 10, 2);
  api_def_prop_ui_text(
      prop, "Start", "Starting distance of the mist, measured from the camera");
  RNA_def_property_update(prop, 0, "rna_World_draw_update");

  prop = RNA_def_property(srna, "depth", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "mistdist");
  RNA_def_property_range(prop, 0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0, 10000, 10, 2);
  RNA_def_property_ui_text(prop, "Depth", "Distance over which the mist effect fades in");
  RNA_def_property_update(prop, 0, "rna_World_draw_update");

  prop = RNA_def_property(srna, "height", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "misthi");
  RNA_def_property_range(prop, 0, 100);
  RNA_def_property_ui_text(prop, "Height", "Control how much mist density decreases with height");
  RNA_def_property_update(prop, 0, "rna_World_update");

  prop = RNA_def_property(srna, "falloff", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "mistype");
  RNA_def_property_enum_items(prop, falloff_items);
  RNA_def_property_ui_text(prop, "Falloff", "Type of transition used to fade mist");
  RNA_def_property_update(prop, 0, "rna_World_draw_update");
}

void RNA_def_world(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static float default_world_color[] = {0.05f, 0.05f, 0.05f};

  srna = RNA_def_struct(brna, "World", "ID");
  RNA_def_struct_ui_text(
      srna,
      "World",
      "World data-block describing the environment and ambient lighting of a scene");
  RNA_def_struct_ui_icon(srna, ICON_WORLD_DATA);

  rna_def_animdata_common(srna);

  /* colors */
  prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
  RNA_def_property_float_sdna(prop, NULL, "horr");
  RNA_def_property_array(prop, 3);
  RNA_def_property_float_array_default(prop, default_world_color);
  RNA_def_property_ui_text(prop, "Color", "Color of the background");
  // RNA_def_property_update(prop, 0, "rna_World_update");
  /* render-only uses this */
  RNA_def_property_update(prop, 0, "rna_World_draw_update");

  /* nested structs */
  prop = RNA_def_property(srna, "light_settings", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NEVER_NULL);
  RNA_def_property_struct_type(prop, "WorldLighting");
  RNA_def_property_pointer_funcs(prop, "rna_World_lighting_get", NULL, NULL, NULL);
  RNA_def_property_ui_text(prop, "Lighting", "World lighting settings");

  prop = RNA_def_property(srna, "mist_settings", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NEVER_NULL);
  RNA_def_property_struct_type(prop, "WorldMistSettings");
  RNA_def_property_pointer_funcs(prop, "rna_World_mist_get", NULL, NULL, NULL);
  RNA_def_property_ui_text(prop, "Mist", "World mist settings");

  /* nodes */
  prop = RNA_def_property(srna, "node_tree", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "nodetree");
  RNA_def_property_clear_flag(prop, PROP_PTR_NO_OWNERSHIP);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Node Tree", "Node tree for node based worlds");

  prop = RNA_def_property(srna, "use_nodes", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "use_nodes", 1);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
  RNA_def_property_ui_text(prop, "Use Nodes", "Use shader nodes to render the world");
  RNA_def_property_update(prop, 0, "rna_World_use_nodes_update");

  /* Lightgroup Membership */
  prop = RNA_def_property(srna, "lightgroup", PROP_STRING, PROP_NONE);
  RNA_def_property_string_funcs(
      prop, "rna_World_lightgroup_get", "rna_World_lightgroup_length", "rna_World_lightgroup_set");
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Lightgroup", "Lightgroup that the world belongs to");

  rna_def_lighting(brna);
  rna_def_world_mist(brna);
}

#endif
