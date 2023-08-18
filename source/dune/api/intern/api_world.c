#include <float.h>
#include <stdlib.h>

#include "api_define.h"

#include "api_internal.h"

#include "types_material.h"
#include "types_texture.h"
#include "types_world.h"

#include "wm_types.h"

#ifdef API_RUNTIME

#  include "mem_guardedalloc.h"

#  include "dune_ctx.h"
#  include "dune_layer.h"
#  include "dune_main.h"
#  include "dune_texture.h"

#  include "graph.h"
#  include "graph_build.h"

#  include "ed_node.h"

#  include "wm_api.h"

static ApiPtr api_world_lighting_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiWorldLighting, ptr->owner_id);
}

static ApiPtr api_World_mist_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &Api_WorldMistSettings, ptr->owner_id);
}

static void api_World_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  World *wo = (World *)ptr->owner_id;

  graph_id_tag_update(&wo->id, 0);
  wm_main_add_notifier(NC_WORLD | ND_WORLD, wo);
}

#  if 0
static void api_World_draw_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  World *wo = (World *)ptr->owner_id;

  graph_id_tag_update(&wo->id, 0);
  wm_main_add_notifier(NC_WORLD | ND_WORLD_DRAW, wo);
}
#  endif

static void api_World_draw_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
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
  Scene *scene = cxt_data_scene(C);

  if (wrld->use_nodes && wrld->nodetree == NULL) {
    ed_node_shader_default(C, &wrld->id);
  }

  graph_relations_tag_update(pmain);
  apo_World_update(main, scene, ptr);
  api_World_draw_update(main, scene, ptr);
}

void api_World_lightgroup_get(ApiPtr *ptr, char *value)
{
  LightgroupMembership *lgm = ((World *)ptr->owner_id)->lightgroup;
  char value_buf[sizeof(lgm->name)];
  int len = dune_lightgroup_membership_get(lgm, value_buf);
  memcpy(value, value_buf, len + 1);
}

int api_world_lightgroup_length(ApiPtr *ptr)
{
  LightgroupMembership *lgm = ((World *)ptr->owner_id)->lightgroup;
  return dune_lightgroup_membership_length(lgm);
}

void api_World_lightgroup_set(ApiPtr *ptr, const char *value)
{
  dune_lightgroup_membership_set(&((World *)ptr->owner_id)->lightgroup, value);
}

#else

static void api_def_lighting(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(sapi, "WorldLighting", NULL);
  api_def_struct_stype(sapi, "World");
  api_def_struct_nested(dapi, sapi, "World");
  api_def_struct_ui_text(sapi, "Lighting", "Lighting for a World data-block");

  /* ambient occlusion */
  prop = api_def_prop(sapi, "use_ambient_occlusion", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "mode", WO_AMB_OCC);
  api_def_prop_ui_text(
      prop,
      "Use Ambient Occlusion",
      "Use Ambient Occlusion to add shadowing based on distance between objects");
  api_def_prop_update(prop, 0, "api_World_update");

  prop = api_def_prop(sapi, "ao_factor", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "aoenergy");
  api_def_prope_range(prop, 0, INT_MAX);
  api_def_prop_ui_range(prop, 0, 1, 0.1, 2);
  api_def_prop_ui_text(prop, "Factor", "Factor for ambient occlusion blending");
  api_def_prop_update(prop, 0, "api_World_update");

  prop = api_def_prop(sapi, "distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_pro_float_stype(prop, NULL, "aodist");
  api_def_prop_range(prop, 0, FLT_M);
  api_def_prop_ui_text(
      prop, "Distance", "Length of rays, defines how far away other faces give occlusion effect");
  api_def_prop_update(prop, 0, "api_World_update");
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

  prop = api_def_prop(sapi, "intensity", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "misi")
  api_def_prop_range(prop, 0, 1);
  api_def_prop_ui_text(prop, "Minimum", "Overall minimum intensity of the mist effect");
  api_def_prop_update(prop, 0, "rna_World_draw_update");

  prop = api_def_prop(sapi, "start", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "miststa");
  api_def_prop_range(prop, 0, FLT_MAX);
  api_def_prop_ui_range(prop, 0, 10000, 10, 2);
  api_def_prop_ui_text(
      prop, "Start", "Starting distance of the mist, measured from the camera");
  api_def_prop_update(prop, 0, "rna_World_draw_update");

  prop = api_def_prop(sapi, "depth", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "mistdist");
  api_def_prop_range(prop, 0, FLT_MAX);
  api_def_prop_ui_range(prop, 0, 10000, 10, 2);
  api_def_prop_ui_text(prop, "Depth", "Distance over which the mist effect fades in");
  api_def_prop_update(prop, 0, "rna_World_draw_update");

  prop = api_def_prop(sapi, "height", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "misthi");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop, "Height", "Control how much mist density decreases with height");
  api_def_prop_update(prop, 0, "rna_World_update");

  prop = api_def_prop(sapi, "falloff", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mistype");
  api_def_prop_enum_items(prop, falloff_items);
  api_def_prop_ui_text(prop, "Falloff", "Type of transition used to fade mist");
  api_def_prop_update(prop, 0, "rna_World_draw_update");
}

void api_def_world(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApuProp *prop;

  static float default_world_color[] = {0.05f, 0.05f, 0.05f};

  sapi = api_def_struct(dapi, "World", "Id");
  api_def_struct_ui_text(
      sapi,
      "World",
      "World data-block describing the environment and ambient lighting of a scene");
  api_def_struct_ui_icon(sapi, ICON_WORLD_DATA);

  api_def_animdata_common(srna);

  /* colors */
  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_sdna(prop, NULL, "horr");
  api_def_prop_array(prop, 3);
  api_def_prop_float_array_default(prop, default_world_color);
  api_def_prop_ui_text(prop, "Color", "Color of the background");
  // RNA_def_property_update(prop, 0, "rna_World_update");
  /* render-only uses this */
  api_def_prop_update(prop, 0, "api_world_draw_update");

  /* nested structs */
  prop = api_def_prop(sapi, "light_settings", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "WorldLighting");
  api_def_prop_ptr_fns(prop, "api_World_lighting_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Lighting", "World lighting settings");

  prop = api_def_prop(sapi, "mist_settings", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "WorldMistSettings");
  api_def_prop_ptr_fns(prop, "api_World_mist_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Mist", "World mist settings");

  /* nodes */
  prop = api_def_prop(sapi, "node_tree", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "nodetree");
  api_def_prop_clear_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Node Tree", "Node tree for node based worlds");

  prop = api_def_prop(sapi, "use_nodes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_nodes", 1);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_ui_text(prop, "Use Nodes", "Use shader nodes to render the world");
  api_def_prop_update(prop, 0, "api_World_use_nodes_update");

  /* Lightgroup Membership */
  prop = api_def_prop(sapi, "lightgroup", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(
      prop, "api_World_lightgroup_get", "api_world_lightgroup_length", "api_World_lightgroup_set");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Lightgroup", "Lightgroup that the world belongs to");

  api_def_lighting(dapi);
  api_def_world_mist(dapi);
}

#endif
