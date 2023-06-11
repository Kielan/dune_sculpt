#include <limits.h>
#include <stdlib.h>

#include "types_cloth.h"
#include "types_object.h"
#include "types_scene.h"

#include "api_define.h"

#include "api_internal.h"

#include "lib_math.h"

#include "dune_cloth.h"
#include "dune_mod.h"

#include "SIM_mass_spring.h"

#include "wm_api.h"
#include "wm_types.h"

#ifdef API_RUNTIME

#  include "dune_context.h"
#  include "graph.h"
#  include "graph_build.h"

static void api_cloth_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;

  DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
  WM_main_add_notifier(NC_OBJECT | ND_MODIFIER, ob);
}

static void api_cloth_dependency_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  graph_relations_tag_update(main);
  api_cloth_update(main, scene, ptr);
}

static void api_cloth_pinning_changed(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  /*  ClothSimSettings *settings = (ClothSimSettings *)ptr->data; */
  ClothModData *clmd = (ClothModData *)dune_mods_findby_type(eModType_Cloth);

  cloth_free_mod(clmd);

  graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
  wm_main_add_notifier(NC_OBJECT | ND_MOD, ob);
}

static void api_ClothSettings_bending_set(struct ApiPtr *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  settings->bending = value;

  /* check for max clipping */
  if (value > settings->max_bend) {
    settings->max_bend = value;
  }
}

static void api_ClothSettings_max_bend_set(struct PointerRNA *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  /* check for clipping */
  if (value < settings->bending) {
    value = settings->bending;
  }

  settings->max_bend = value;
}

static void api_ClothSettings_tension_set(struct PointerRNA *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  settings->tension = value;

  /* check for max clipping */
  if (value > settings->max_tension) {
    settings->max_tension = value;
  }
}

static void api_ClothSettings_max_tension_set(struct PointerRNA *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  /* check for clipping */
  if (value < settings->tension) {
    value = settings->tension;
  }

  settings->max_tension = value;
}

static void api_ClothSettings_compression_set(struct PointerRNA *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  settings->compression = value;

  /* check for max clipping */
  if (value > settings->max_compression) {
    settings->max_compression = value;
  }
}

static void api_ClothSettings_max_compression_set(struct PointerRNA *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  /* check for clipping */
  if (value < settings->compression) {
    value = settings->compression;
  }

  settings->max_compression = value;
}

static void api_ClothSettings_shear_set(struct PointerRNA *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  settings->shear = value;

  /* check for max clipping */
  if (value > settings->max_shear) {
    settings->max_shear = value;
  }
}

static void api_ClothSettings_max_shear_set(struct ApiPtr *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  /* check for clipping */
  if (value < settings->shear) {
    value = settings->shear;
  }

  settings->max_shear = value;
}

static void api_ClothSettings_max_sewing_set(struct ApiPtr *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  /* check for clipping */
  if (value < 0.0f) {
    value = 0.0f;
  }

  settings->max_sewing = value;
}

static void api_ClothSettings_shrink_min_set(struct ApiPtr *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  settings->shrink_min = value;

  /* check for max clipping */
  if (value > settings->shrink_max) {
    settings->shrink_max = value;
  }
}

static void api_ClothSettings_shrink_max_set(struct ApiPtr *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  /* check for clipping */
  if (value < settings->shrink_min) {
    value = settings->shrink_min;
  }

  settings->shrink_max = value;
}

static void api_ClothSettings_internal_tension_set(struct ApiPtr *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  settings->internal_tension = value;

  /* check for max clipping */
  if (value > settings->max_internal_tension) {
    settings->max_internal_tension = value;
  }
}

static void api_ClothSettings_max_internal_tension_set(struct PointerRNA *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  /* check for clipping */
  if (value < settings->internal_tension) {
    value = settings->internal_tension;
  }

  settings->max_internal_tension = value;
}

static void api_ClothSettings_internal_compression_set(struct PointerRNA *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  settings->internal_compression = value;

  /* check for max clipping */
  if (value > settings->max_internal_compression) {
    settings->max_internal_compression = value;
  }
}

static void api_ClothSettings_max_internal_compression_set(struct ApiPtr *ptr, float value)
{
  ClothSimSettings *settings = (ClothSimSettings *)ptr->data;

  /* check for clipping */
  if (value < settings->internal_compression) {
    value = settings->internal_compression;
  }

  settings->max_internal_compression = value;
}

static void api_ClothSettings_mass_vgroup_get(ApiPtr *ptr, char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_get(ptr, value, sim->vgroup_mass);
}

static int api_ClothSettings_mass_vgroup_length(ApiPtr *ptr)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  return api_object_vgroup_name_index_length(ptr, sim->vgroup_mass);
}

static void api_ClothSettings_mass_vgroup_set(ApiPtr *ptr, const char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_set(ptr, value, &sim->vgroup_mass);
}

static void api_ClothSettings_shrink_vgroup_get(PointerRNA *ptr, char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_get(ptr, value, sim->vgroup_shrink);
}

static int api_ClothSettings_shrink_vgroup_length(ApiPtr *ptr)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  return api_object_vgroup_name_index_length(ptr, sim->vgroup_shrink);
}

static void api_ClothSettings_shrink_vgroup_set(ApiPtr *ptr, const char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_set(ptr, value, &sim->vgroup_shrink);
}

static void api_ClothSettings_struct_vgroup_get(PointerRNA *ptr, char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_get(ptr, value, sim->vgroup_struct);
}

static int api_ClothSettings_struct_vgroup_length(ApiPtr *ptr)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  return api_object_vgroup_name_index_length(ptr, sim->vgroup_struct);
}

static void api_ClothSettings_struct_vgroup_set(ApiPtr *ptr, const char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_set(ptr, value, &sim->vgroup_struct);
}

static void api_ClothSettings_shear_vgroup_get(ApiPtr *ptr, char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_get(ptr, value, sim->vgroup_shear);
}

static int api_ClothSettings_shear_vgroup_length(ApiPtr *ptr)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  return api_object_vgroup_name_index_length(ptr, sim->vgroup_shear);
}

static void api_ClothSettings_shear_vgroup_set(ApiPtr *ptr, const char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_set(ptr, value, &sim->vgroup_shear);
}

static void api_ClothSettings_bend_vgroup_get(ApiPtr *ptr, char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_get(ptr, value, sim->vgroup_bend);
}

static int api_ClothSettings_bend_vgroup_length(ApiPtr *ptr)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  return api_object_vgroup_name_index_length(ptr, sim->vgroup_bend);
}

static void api_ClothSettings_bend_vgroup_set(ApiPtr *ptr, const char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_set(ptr, value, &sim->vgroup_bend);
}

static void api_ClothSettings_internal_vgroup_get(ApiPtr *ptr, char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_get(ptr, value, sim->vgroup_intern);
}

static int api_ClothSettings_internal_vgroup_length(ApiPtr *ptr)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  return api_object_vgroup_name_index_length(ptr, sim->vgroup_intern);
}

static void api_ClothSettings_internal_vgroup_set(ApiPtr *ptr, const char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_set(ptr, value, &sim->vgroup_intern);
}

static void api_ClothSettings_pressure_vgroup_get(ApiPtr *ptr, char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_get(ptr, value, sim->vgroup_pressure);
}

static int api_ClothSettings_pressure_vgroup_length(PointerRNA *ptr)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  return api_object_vgroup_name_index_length(ptr, sim->vgroup_pressure);
}

static void api_ClothSettings_pressure_vgroup_set(ApiPtr *ptr, const char *value)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;
  api_object_vgroup_name_index_set(ptr, value, &sim->vgroup_pressure);
}

static void api_CollSettings_selfcol_vgroup_get(PointerRNA *ptr, char *value)
{
  ClothCollSettings *coll = (ClothCollSettings *)ptr->data;
  api_object_vgroup_name_index_get(ptr, value, coll->vgroup_selfcol);
}

static int api_CollSettings_selfcol_vgroup_length(ApiPtr *ptr)
{
  ClothCollSettings *coll = (ClothCollSettings *)ptr->data;
  return api_object_vgroup_name_index_length(ptr, coll->vgroup_selfcol);
}

static void rna_CollSettings_selfcol_vgroup_set(PointerRNA *ptr, const char *value)
{
  ClothCollSettings *coll = (ClothCollSettings *)ptr->data;
  rna_object_vgroup_name_index_set(ptr, value, &coll->vgroup_selfcol);
}

static void rna_CollSettings_objcol_vgroup_get(PointerRNA *ptr, char *value)
{
  ClothCollSettings *coll = (ClothCollSettings *)ptr->data;
  rna_object_vgroup_name_index_get(ptr, value, coll->vgroup_objcol);
}

static int api_CollSettings_objcol_vgroup_length(PointerRNA *ptr)
{
  ClothCollSettings *coll = (ClothCollSettings *)ptr->data;
  return api_object_vgroup_name_index_length(ptr, coll->vgroup_objcol);
}

static void api_CollSettings_objcol_vgroup_set(ApiPtr *ptr, const char *value)
{
  ClothCollSettings *coll = (ClothCollSettings *)ptr->data;
  api_object_vgroup_name_index_set(ptr, value, &coll->vgroup_objcol);
}

static ApiPtr api_ClothSettings_rest_shape_key_get(ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;

  return api_object_shapekey_index_get(ob->data, sim->shapekey_rest);
}

static void api_ClothSettings_rest_shape_key_set(PointerRNA *ptr,
                                                 PointerRNA value,
                                                 struct ReportList *UNUSED(reports))
{
  Object *ob = (Object *)ptr->owner_id;
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;

  sim->shapekey_rest = rna_object_shapekey_index_set(ob->data, value, sim->shapekey_rest);
}

static void api_ClothSettings_gravity_get(PointerRNA *ptr, float *values)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;

  values[0] = sim->gravity[0];
  values[1] = sim->gravity[1];
  values[2] = sim->gravity[2];
}

static void api_ClothSettings_gravity_set(PointerRNA *ptr, const float *values)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;

  sim->gravity[0] = values[0];
  sim->gravity[1] = values[1];
  sim->gravity[2] = values[2];
}

static char *api_ClothSettings_path(ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  ModData *md = dune_mods_findby_type(ob, eModType_Cloth);

  if (md) {
    char name_esc[sizeof(md->name) * 2];
    lib_str_escape(name_esc, md->name, sizeof(name_esc));
    return lib_sprintfn("mods[\"%s\"].settings", name_esc);
  }
  else {
    return NULL;
  }
}

static char *api_ClothCollisionSettings_path(ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  ModData *md = dune_mods_findby_type(ob, eModType_Cloth);

  if (md) {
    char name_esc[sizeof(md->name) * 2];
    lib_str_escape(name_esc, md->name, sizeof(name_esc));
    return lib_sprintfn("modifiers[\"%s\"].collision_settings", name_esc);
  }
  else {
    return NULL;
  }
}

static int api_ClothSettings_internal_editable(struct PointerRNA *ptr, const char **r_info)
{
  ClothSimSettings *sim = (ClothSimSettings *)ptr->data;

  if (sim && (sim->bending_model == CLOTH_BENDING_LINEAR)) {
    *r_info = "Only available with angular bending springs.";
    return 0;
  }

  return sim ? PROP_EDITABLE : 0;
}

#else

static void api_def_cloth_solver_result(DuneApi *api)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem status_items[] = {
      {SIM_SOLVER_SUCCESS, "SUCCESS", 0, "Success", "Computation was successful"},
      {SIM_SOLVER_NUMERICAL_ISSUE,
       "NUMERICAL_ISSUE",
       0,
       "Numerical Issue",
       "The provided data did not satisfy the prerequisites"},
      {SIM_SOLVER_NO_CONVERGENCE,
       "NO_CONVERGENCE",
       0,
       "No Convergence",
       "Iterative procedure did not converge"},
      {SIM_SOLVER_INVALID_INPUT,
       "INVALID_INPUT",
       0,
       "Invalid Input",
       "The inputs are invalid, or the algorithm has been improperly called"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "ClothSolverResult", NULL);
  api_def_struct_ui_text(sapi, "Solver Result", "Result of cloth solver iteration");

  api_define_verify_stype(0);

  prop = api_def_prop(sapi, "status", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, status_items);
  api_def_prop_lib_enum_stype(prop, NULL, "status");
  api_def_prop_flag(prop, PROP_ENUM_FLAG);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Status", "Status of the solver iteration");

  prop = api_def_prop(sapi, "max_error", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_error");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Maximum Error", "Maximum error during substeps");

  prop = api_def_prop(sapi, "min_error", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "min_error");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Minimum Error", "Minimum error during substeps");

  prop = api_def_prop(sapi, "avg_error", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stypes(prop, NULL, "avg_error");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Average Error", "Average error during substeps");

  prop = api_def_prop(sapi, "max_iterations", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "max_iterations");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Maximum Iterations", "Maximum iterations during substeps");

  prop = api_def_prop(sapi, "min_iterations", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "min_iterations");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Minimum Iterations", "Minimum iterations during substeps");

  prop = api_def_prop(sapi, "avg_iterations", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "avg_iterations");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Average Iterations", "Average iterations during substeps");

  api_define_verify_sdna(1);
}

static void api_def_cloth_sim_settings(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem prop_bending_model_items[] = {
      {CLOTH_BENDING_ANGULAR, "ANGULAR", 0, "Angular", "Cloth model with angular bending springs"},
      {CLOTH_BENDING_LINEAR,
       "LINEAR",
       0,
       "Linear",
       "Cloth model with linear bending springs (legacy)"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "ClothSettings", NULL);
  api_def_struct_ui_text(sapi, "Cloth Settings", "Cloth simulation settings for an object");
  api_def_struct_stype(sapi, "ClothSimSettings");
  api_def_struct_path_fn(sapi, "api_ClothSettings_path");

  api_define_lib_overridable(true);

  /* goal */

  prop = api_def_prop(sapi, "goal_min", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "mingoal");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Goal Minimum", "Goal minimum, vertex group weights are scaled to match this range");
  api_def_prop_update(prop, 0, "rna_cloth_update"
  prop = api_def_prop(sapi, "goal_max", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "maxgoal");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Goal Maximum", "Goal maximum, vertex group weights are scaled to match this range");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "goal_default", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stypes(prop, NULL, "defgoal");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop,
      "Goal Default",
      "Default Goal (vertex target position) value, when no Vertex Group used");
  api_def_prop_update(prop, 0, "rna_cloth_update");

  prop = api_def_prop(sapi, "goal_spring", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "goalspring");
  api_def_prop_range(prop, 0.0f, 0.999f);
  api_def_prop_ui_text(
      prop, "Goal Stiffness", "Goal (vertex target position) spring stiffness");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "goal_friction", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "goalfrict");
  api_def_prop_range(prop, 0.0f, 50.0f);
  api_def_prop_ui_text(prop, "Goal Damping", "Goal (vertex target position) friction");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "internal_friction", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "velocity_smooth");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Internal Friction", "");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "collider_friction", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "collider_friction");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Collider Friction", "");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "density_target", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "density_target");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_ui_text(prop, "Target Density", "Maximum density of hair");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "density_strength", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "density_strength");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Target Density Strength", "Influence of target density on the simulation");
  api_def_prop_update(prop, 0, "api_cloth_update");

  /* mass */

  prop = api_def_prop(sapi, "mass", PROP_FLOAT, PROP_UNIT_MASS);
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_text(prop, "Vertex Mass", "The mass of each vertex on the cloth material");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "vertex_group_mass", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(prop,
                          "api_ClothSettings_mass_vgroup_get",
                          "api_ClothSettings_mass_vgroup_length",
                          "api_ClothSettings_mass_vgroup_set");
  api_def_prop_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop, "Mass Vertex Group", "Vertex Group for pinning of vertices");
  api_def_prop_update(prop, 0, "api_cloth_pinning_changed");

  prop = api_def_prop(sapi, "gravity", PROP_FLOAT, PROP_ACCELERATION);
  api_def_prop_array(prop, 3);
  api_def_prop_range(prop, -100.0, 100.0);
  api_def_prop_float_fns(
      prop, "api_ClothSettings_gravity_get", "api_ClothSettings_gravity_set", NULL);
  api_def_prop_ui_text(prop, "Gravity", "Gravity or external force vector");
  api_def_prop_update(prop, 0, "api_cloth_update");

  /* various */

  prop = api_def_prop(sapi, "air_damping", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "Cvi");
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_text(
      prop, "Air Damping", "Air has normally some thickness which slows falling things down");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "pin_stiffness", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "goalspring");
  api_def_prop_range(prop, 0.0f, 50.0);
  api_def_prop_ui_text(prop, "Pin Stiffness", "Pin (vertex target position) spring stiffness");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "quality", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "stepsPerFrame");
  api_def_prop_range(prop, 1, INT_MAX);
  api_def_prop_ui_range(prop, 1, 80, 1, -1);
  api_def_prop_ui_text(
      prop,
      "Quality",
      "Quality of the simulation in steps per frame (higher is better quality but slower)");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "time_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "time_scale");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 10.0f, 10, 3);
  api_def_prop_ui_text(prop, "Speed", "Cloth speed is multiplied by this value");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "vertex_group_shrink", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(prop,
                          "api_ClothSettings_shrink_vgroup_get",
                          "api_ClothSettings_shrink_vgroup_length",
                          "api_ClothSettings_shrink_vgroup_set");
  api_def_prop_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop, "Shrink Vertex Group", "Vertex Group for shrinking cloth");
  api_def_prop_update(prop, 0, "rna_cloth_update");

  prop = api_def_prop(sapi, "shrink_min", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "shrink_min");
  api_def_prop_range(prop, -FLT_MAX, 1.0f);
  api_def_prop_ui_range(prop, -1.0f, 1.0f, 0.05f, 3);
  api_def_prop_float_fns(prop, NULL, "rna_ClothSettings_shrink_min_set", NULL);
  api_def_prop_ui_text(prop, "Shrink Factor", "Factor by which to shrink cloth");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "shrink_max", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "shrink_max");
  api_def_prop_range(prop, -FLT_MAX, 1.0f);
  api_def_prop_ui_range(prop, -1.0f, 1.0f, 0.05f, 3);
  api_def_prop_float_fns(prop, NULL, "api_ClothSettings_shrink_max_set", NULL);
  api_def_prop_ui_text(prop, "Shrink Factor Max", "Max amount to shrink cloth by");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "voxel_cell_size", PROP_FLOAT, PROP_UNSIGNED);
  api_def_prop_float_stype(prop, NULL, "voxel_cell_size");
  api_def_prop_range(prop, 0.0001f, 10000.0f);
  api_def_prop_ui_text(
      prop, "Voxel Grid Cell Size", "Size of the voxel grid cells for interaction effects");
  api_def_prop_update(prop, 0, "api_cloth_update");

  /* springs */
  prop = api_def_prop(sapi, "tension_damping", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "tension_damp");
  api_def_prop_range(prop, 0.0f, 50.0f);
  api_def_prop_ui_text(
      prop, "Tension Spring Damping", "Amount of damping in stretching behavior");
  api_def_prop_update(prop, 0, "rna_cloth_update");

  prop = api_def_prop(sapi, "compression_damping", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "compression_damp");
  api_def_prop_range(prop, 0.0f, 50.0f);
  api_def_prop_ui_text(
      prop, "Compression Spring Damping", "Amount of damping in compression behavior");
  api_def_prop_update(prop, 0, "api_cloth_update");
                      
  prop = api_def_prop(sapi, "shear_damping", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "shear_damp");
  api_def_prop_range(prop, 0.0f, 50.0f);
  api_def_prop_ui_text(prop, "Shear Spring Damping", "Amount of damping in shearing behavior");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(api, "tension_stiffness", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "tension");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_float_fns(prop, NULL, "rna_ClothSettings_tension_set", NULL);
  api_def_prop_ui_text(prop, "Tension Stiffness", "How much the material resists stretching");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "tension_stiffness_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_tension");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_float_fns(prop, NULL, "rna_ClothSettings_max_tension_set", NULL);
  api_def_prop_ui_text(prop, "Tension Stiffness Maximum", "Maximum tension stiffness value");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "compression_stiffness", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "compression");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_float_fns(prop, NULL, "api_ClothSettings_compression_set", NULL);
  api_def_prop_ui_text(
      prop, "Compression Stiffness", "How much the material resists compression");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "compression_stiffness_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_compression");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_float_fns(prop, NULL, "api_ClothSettings_max_compression_set", NULL);
  api_def_prop_ui_text(
      prop, "Compression Stiffness Maximum", "Maximum compression stiffness value");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "shear_stiffness", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "shear");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_float_fns(prop, NULL, "api_ClothSettings_shear_set", NULL);
  api_def_prop_ui_text(prop, "Shear Stiffness", "How much the material resists shearing");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "shear_stiffness_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_sdna(prop, NULL, "max_shear");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_float_fns(prop, NULL, "api_ClothSettings_max_shear_set", NULL);
  api_def_prop_ui_text(prop, "Shear Stiffness Maximum", "Maximum shear scaling value");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "sewing_force_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_sewing");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_float_fns(prop, NULL, "api_ClothSettings_max_sewing_set", NULL);
  api_def_prop_ui_text(prop, "Sewing Force Max", "Maximum sewing force");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "vertex_group_structural_stiffness", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(prop,
                          "api_ClothSettings_struct_vgroup_get",
                          "api_ClothSettings_struct_vgroup_length",
                          "api_ClothSettings_struct_vgroup_set");
  api_def_prop_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop,
                       "Structural Stiffness Vertex Group",
                       "Vertex group for fine control over structural stiffness");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "vertex_group_shear_stiffness", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(prop,
                         "api_ClothSettings_shear_vgroup_get",
                         "api_ClothSettings_shear_vgroup_length",
                         "api_ClothSettings_shear_vgroup_set");
  api_def_prop_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(
      prop, "Shear Stiffness Vertex Group", "Vertex group for fine control over shear stiffness");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "bending_stiffness", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "bending");
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_float_fns(prop, NULL, "rna_ClothSettings_bending_set", NULL);
  api_def_prop_ui_text(prop, "Bending Stiffness", "How much the material resists bending");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "bending_stiffness_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max_
  api_def_prop_range(prop, 0.0f, 10000.0f);
  api_def_prop_float_fns(prop, NULL, "api_ClothSettings_max_bend_set", NULL);
  api_def_prop_ui_text(prop, "Bending Stiffness Maximum", "Maximum bending stiffness value");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "bending_damping", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "bending_damping");
  api_def_prop_range(prop, 0.0f, 1000.0f);
  api_def_prop_ui_text(
      prop, "Bending Spring Damping", "Amount of damping in bending behavior");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "use_sewing_springs", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", CLOTH_SIMSETTINGS_FLAG_SEW);
  api_def_prop_ui_text(prop, "Sew Cloth", "Pulls loose edges together");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "vertex_group_bending", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(prop,
                                "api_ClothSettings_bend_vgroup_get",
                                "api_ClothSettings_bend_vgroup_length",
                                "api_ClothSettings_bend_vgroup_set");
  api_def_prop_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop,
                           "Bending Stiffness Vertex Group",
                           "Vertex group for fine control over bending stiffness");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "effector_weights", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "EffectorWeights");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
                          apidef_prop_ui_text(prop, "Effector Weights", "");

  prop = api_def_prop(sapi, "rest_shape_key", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "ShapeKey");
  api_def_prop_ptr_fns(prop,
                       "api_ClothSettings_rest_shape_key_get",
                       "api_ClothSettings_rest_shape_key_set",
                       NULL,
                       NULL);
  api_def_prop_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(
      prop, "Rest Shape Key", "Shape key to use the rest spring lengths from");
  api_def_prop_update(prop, 0, "api_cloth_update");

  prop = api_def_prop(sapi, "use_dynamic_mesh", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", CLOTH_SIMSETTINGS_FLAG_DYNAMIC_BASEMESH);
  api_def_prop_ui_text(
      prop, "Dynamic Base Mesh", "Make simulation respect deformations in the base mesh");
  api_def_prop_update(prop, 0, "api_cloth_update");
  spu_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "bending_model", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "bending_model");
  api_def_prop_enum_items(prop, prop_bending_model_items);
  api_def_prop_ui_text(prop, "Bending Model", "Physical model for simulating bending forces");
  api_def_prop_update(prop, 0, "api_cloth_update");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);

  prop = api_def_prop(sapi, "use_internal_springs", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", CLOTH_SIMSETTINGS_FLAG_INTERNAL_SPRINGS);
  api_def_prop_ui_text(prop,
                           "Create Internal Springs",
                           "Simulate an internal volume structure by creating springs connecting "
                           "the opposite sides of the mesh");
  RNA_def_property_update(prop, 0, "rna_cloth_update");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

  prop = RNA_def_property(srna, "internal_spring_normal_check", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(
      prop, NULL, "flags", CLOTH_SIMSETTINGS_FLAG_INTERNAL_SPRINGS_NORMAL);
  RNA_def_property_ui_text(prop,
                           "Check Internal Spring Normals",
                           "Require the points the internal springs connect to have opposite "
                           "normal directions");
  RNA_def_property_editable_func(prop, "rna_ClothSettings_internal_editable");
  RNA_def_property_update(prop, 0, "rna_cloth_update");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

  prop = RNA_def_property(srna, "internal_spring_max_length", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "internal_spring_max_length");
  RNA_def_property_range(prop, 0.0f, 1000.0f);
  RNA_def_property_ui_text(
      prop,
      "Internal Spring Max Length",
      "The maximum length an internal spring can have during creation. If the distance between "
      "internal points is greater than this, no internal spring will be created between these "
      "points. "
      "A length of zero means that there is no length limit");
  RNA_def_property_editable_func(prop, "rna_ClothSettings_internal_editable");
  RNA_def_property_update(prop, 0, "rna_cloth_update");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

  prop = RNA_def_property(srna, "internal_spring_max_diversion", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "internal_spring_max_diversion");
  RNA_def_property_range(prop, 0.0f, M_PI_4);
  RNA_def_property_ui_text(prop,
                           "Internal Spring Max Diversion",
                           "How much the rays used to connect the internal points can diverge "
                           "from the vertex normal");
  RNA_def_property_editable_func(prop, "rna_ClothSettings_internal_editable");
  RNA_def_property_update(prop, 0, "rna_cloth_update");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

  prop = RNA_def_property(srna, "internal_tension_stiffness", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "internal_tension");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_float_funcs(prop, NULL, "rna_ClothSettings_internal_tension_set", NULL);
  RNA_def_property_ui_text(prop, "Tension Stiffness", "How much the material resists stretching");
  RNA_def_property_editable_func(prop, "rna_ClothSettings_internal_editable");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "internal_tension_stiffness_max", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "max_internal_tension");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_float_funcs(prop, NULL, "rna_ClothSettings_max_internal_tension_set", NULL);
  RNA_def_property_ui_text(prop, "Tension Stiffness Maximum", "Maximum tension stiffness value");
  RNA_def_property_editable_func(prop, "rna_ClothSettings_internal_editable");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "internal_compression_stiffness", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "internal_compression");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_float_funcs(prop, NULL, "rna_ClothSettings_internal_compression_set", NULL);
  RNA_def_property_ui_text(
      prop, "Compression Stiffness", "How much the material resists compression");
  RNA_def_property_editable_func(prop, "rna_ClothSettings_internal_editable");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "internal_compression_stiffness_max", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "max_internal_compression");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_float_funcs(prop, NULL, "rna_ClothSettings_max_internal_compression_set", NULL);
  RNA_def_property_ui_text(
      prop, "Compression Stiffness Maximum", "Maximum compression stiffness value");
  RNA_def_property_editable_func(prop, "rna_ClothSettings_internal_editable");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = api_def_prop(sapi, "vertex_group_intern", PROP_STRING, PROP_NONE);
  RNA_def_property_string_fns(prop,
                              "api_ClothSettings_internal_vgroup_get",
                              "api_ClothSettings_internal_vgroup_length",
                              "api_ClothSettings_internal_vgroup_set");
  RNA_def_property_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop,
                           "Internal Springs Vertex Group",
                           "Vertex group for fine control over the internal spring stiffness");
  RNA_def_property_editable_func(prop, "rna_ClothSettings_internal_editable");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  /* Pressure */

  prop = RNA_def_property(srna, "use_pressure", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", CLOTH_SIMSETTINGS_FLAG_PRESSURE);
  RNA_def_property_ui_text(prop, "Use Pressure", "Simulate pressure inside a closed cloth mesh");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "use_pressure_volume", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", CLOTH_SIMSETTINGS_FLAG_PRESSURE_VOL);
  RNA_def_property_ui_text(prop,
                           "Use Custom Volume",
                           "Use the Target Volume parameter as the initial volume, instead "
                           "of calculating it from the mesh itself");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "uniform_pressure_force", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "uniform_pressure_force");
  RNA_def_property_range(prop, -10000.0f, 10000.0f);
  RNA_def_property_float_default(prop, 0.0f);
  RNA_def_property_ui_text(prop,
                           "Pressure",
                           "The uniform pressure that is constantly applied to the mesh, in units "
                           "of Pressure Scale. Can be negative");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "target_volume", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "target_volume");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_float_default(prop, 0.0f);
  RNA_def_property_ui_text(prop,
                           "Target Volume",
                           "The mesh volume where the inner/outer pressure will be the same. If "
                           "set to zero the change in volume will not affect pressure");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "pressure_factor", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "pressure_factor");
  RNA_def_property_range(prop, 0.0f, 10000.0f);
  RNA_def_property_ui_text(prop,
                           "Pressure Scale",
                           "Ambient pressure (kPa) that balances out between the inside and "
                           "outside of the object when it has the target volume");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "fluid_density", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "fluid_density");
  RNA_def_property_ui_range(prop, -2.0f, 2.0f, 0.05f, 4);
  RNA_def_property_ui_text(
      prop,
      "Fluid Density",
      "Density (kg/l) of the fluid contained inside the object, used to create "
      "a hydrostatic pressure gradient simulating the weight of the internal fluid, "
      "or buoyancy from the surrounding fluid if negative");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "vertex_group_pressure", PROP_STRING, PROP_NONE);
  RNA_def_property_string_funcs(prop,
                                "rna_ClothSettings_pressure_vgroup_get",
                                "rna_ClothSettings_pressure_vgroup_length",
                                "rna_ClothSettings_pressure_vgroup_set");
  RNA_def_property_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop,
      "Pressure Vertex Group",
      "Vertex Group for where to apply pressure. Zero weight means no "
      "pressure while a weight of one means full pressure. Faces with a vertex "
      "that has zero weight will be excluded from the volume calculation");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  /* unused */

  /* unused still */
#  if 0
  prop = RNA_def_property(srna, "effector_force_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "eff_force_scale");
  RNA_def_property_range(prop, 0.0f, 100.0f);
  RNA_def_property_ui_text(prop, "Effector Force Scale", "");
#  endif
  /* unused still */
#  if 0
  prop = api_def_prop(sapi, "effector_wind_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "eff_wind_scale");
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(prop, "Effector Wind Scale", "");
#  endif
  /* unused still */
#  if 0
  prop = api_def_prop(sapi, "tearing", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", CLOTH_SIMSETTINGS_FLAG_TEARING);
  api_def_prop_ui_text(prop, "Tearing", "");
#  endif
  /* unused still */
#  if 0
  prop = api_def_prop(sapi, "max_spring_extensions", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "maxspringlen");
  api_def_prop_range(prop, 1.0, 1000.0);
  api_def_prop_ui_text(
      prop, "Maximum Spring Extension", "Maximum extension before spring gets cut");
#  endif

  api_define_lib_overridable(false);
}

static void rna_def_cloth_collision_settings(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "ClothCollisionSettings", NULL);
  RNA_def_struct_ui_text(
      srna,
      "Cloth Collision Settings",
      "Cloth simulation settings for self collision and collision with other objects");
  RNA_def_struct_sdna(srna, "ClothCollSettings");
  RNA_def_struct_path_func(srna, "rna_ClothCollisionSettings_path");

  RNA_define_lib_overridable(true);

  /* general collision */

  prop = RNA_def_property(srna, "use_collision", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", CLOTH_COLLSETTINGS_FLAG_ENABLED);
  RNA_def_property_ui_text(prop, "Enable Collision", "Enable collisions with other objects");
  RNA_def_property_update(prop, 0, "rna_cloth_dependency_update");

  prop = RNA_def_property(srna, "distance_min", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "epsilon");
  RNA_def_property_range(prop, 0.001f, 1.0f);
  RNA_def_property_ui_text(
      prop,
      "Minimum Distance",
      "Minimum distance between collision objects before collision response takes effect");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "friction", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.0f, 80.0f);
  RNA_def_property_ui_text(
      prop, "Friction", "Friction force if a collision happened (higher = less movement)");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "damping", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "damping");
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_float_default(prop, 1.0f);
  RNA_def_property_ui_text(prop, "Restitution", "Amount of velocity lost on collision");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "collision_quality", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "loop_count");
  RNA_def_property_range(prop, 1, SHRT_MAX);
  RNA_def_property_ui_range(prop, 1, 20, 1, -1);
  RNA_def_property_ui_text(
      prop,
      "Collision Quality",
      "How many collision iterations should be done. (higher is better quality but slower)");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "impulse_clamp", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "clamp");
  RNA_def_property_range(prop, 0.0f, 100.0f);
  RNA_def_property_ui_text(
      prop,
      "Impulse Clamping",
      "Clamp collision impulses to avoid instability (0.0 to disable clamping)");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  /* self collision */

  prop = RNA_def_property(srna, "use_self_collision", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flags", CLOTH_COLLSETTINGS_FLAG_SELF);
  RNA_def_property_ui_text(prop, "Enable Self Collision", "Enable self collisions");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "self_distance_min", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "selfepsilon");
  RNA_def_property_range(prop, 0.001f, 0.1f);
  RNA_def_property_ui_text(
      prop,
      "Self Minimum Distance",
      "Minimum distance between cloth faces before collision response takes effect");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "self_friction", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.0f, 80.0f);
  RNA_def_property_ui_text(prop, "Self Friction", "Friction with self contact");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "collection", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "group");
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Collision Collection", "Limit colliders to this Collection");
  RNA_def_property_update(prop, 0, "rna_cloth_dependency_update");

  prop = RNA_def_property(srna, "vertex_group_self_collisions", PROP_STRING, PROP_NONE);
  RNA_def_property_string_funcs(prop,
                                "rna_CollSettings_selfcol_vgroup_get",
                                "rna_CollSettings_selfcol_vgroup_length",
                                "rna_CollSettings_selfcol_vgroup_set");
  RNA_def_property_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop,
      "Selfcollision Vertex Group",
      "Triangles with all vertices in this group are not used during self collisions");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "vertex_group_object_collisions", PROP_STRING, PROP_NONE);
  RNA_def_property_string_funcs(prop,
                                "rna_CollSettings_objcol_vgroup_get",
                                "rna_CollSettings_objcol_vgroup_length",
                                "rna_CollSettings_objcol_vgroup_set");
  RNA_def_property_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop,
      "Collision Vertex Group",
      "Triangles with all vertices in this group are not used during object collisions");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  prop = RNA_def_property(srna, "self_impulse_clamp", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "self_clamp");
  RNA_def_property_range(prop, 0.0f, 100.0f);
  RNA_def_property_ui_text(
      prop,
      "Impulse Clamping",
      "Clamp collision impulses to avoid instability (0.0 to disable clamping)");
  RNA_def_property_update(prop, 0, "rna_cloth_update");

  RNA_define_lib_overridable(false);
}

void api_def_cloth(DuneApi *dapi)
{
  api_def_cloth_solver_result(dapi);
  api_def_cloth_sim_settings(dapi);
  api_def_cloth_collision_settings(dapi);
}

#endif
