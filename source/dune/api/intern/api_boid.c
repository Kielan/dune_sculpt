#include <float.h>
#include <limits.h>
#include <stdlib.h>

#include "types_boid.h"
#include "types_object.h"
#include "types_particle.h"
#include "types_scene.h"

#include "lib_utildefines.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_api.h"
#include "wm_types.h"

const EnumPropItem api_enum_boidrule_type_items[] = {
    {eBoidRuleType_Goal,
     "GOAL",
     0,
     "Goal",
     "Go to assigned object or loudest assigned signal source"},
    {eBoidRuleType_Avoid,
     "AVOID",
     0,
     "Avoid",
     "Get away from assigned object or loudest assigned signal source"},
    {eBoidRuleType_AvoidCollision,
     "AVOID_COLLISION",
     0,
     "Avoid Collision",
     "Maneuver to avoid collisions with other boids and deflector objects in "
     "near future"},
    {eBoidRuleType_Separate, "SEPARATE", 0, "Separate", "Keep from going through other boids"},
    {eBoidRuleType_Flock,
     "FLOCK",
     0,
     "Flock",
     "Move to center of neighbors and match their velocity"},
    {eBoidRuleType_FollowLeader,
     "FOLLOW_LEADER",
     0,
     "Follow Leader",
     "Follow a boid or assigned object"},
    {eBoidRuleType_AverageSpeed,
     "AVERAGE_SPEED",
     0,
     "Average Speed",
     "Maintain speed, flight level or wander"},
    {eBoidRuleType_Fight, "FIGHT", 0, "Fight", "Go to closest enemy and attack when in range"},
#if 0
    {eBoidRuleType_Protect,
     "PROTECT",
     0,
     "Protect",
     "Go to enemy closest to target and attack when in range"},
    {eBoidRuleType_Hide,
     "HIDE",
     0,
     "Hide",
     "Find a deflector move to its other side from closest enemy"},
    {eBoidRuleType_FollowPath,
     "FOLLOW_PATH",
     0,
     "Follow Path",
     "Move along a assigned curve or closest curve in a group"},
    {eBoidRuleType_FollowWall,
     "FOLLOW_WALL",
     0,
     "Follow Wall",
     "Move next to a deflector object's in direction of its tangent"},
#endif
    {0, NULL, 0, NULL, NULL},
};

#ifndef API_RUNTIME
static const EnumPropItem boidruleset_type_items[] = {
    {eBoidRulesetType_Fuzzy,
     "FUZZY",
     0,
     "Fuzzy",
     "Rules are gone through top to bottom (only the first rule which effect is above "
     "fuzziness threshold is evaluated)"},
    {eBoidRulesetType_Random, "RANDOM", 0, "Random", "A random rule is selected for each boid"},
    {eBoidRulesetType_Average, "AVERAGE", 0, "Average", "All rules are averaged"},
    {0, NULL, 0, NULL, NULL},
};
#endif

#ifdef API_RUNTIME

#  include "lib_math_base.h"

#  include "dune_context.h"
#  include "dune_particle.h"

#  include "graph.h"
#  include "graph_build.h"

static void api_Boids_reset(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  if (ptr->type == &ApiParticleSystem) {
    ParticleSystem *psys = (ParticleSystem *)ptr->data;

    psys->recalc = ID_RECALC_PSYS_RESET;

    graph_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
  }
  else {
    gaph_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY | ID_RECALC_PSYS_RESET);
  }

  wm_main_add_notifier(NC_OBJECT | ND_PARTICLE | NA_EDITED, NULL);
}
static void api_Boids_reset_graph(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  if (ptr->type == &ApiParticleSystem) {
    ParticleSystem *psys = (ParticleSystem *)ptr->data;

    psys->recalc = ID_RECALC_PSYS_RESET;

    graph_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
  }
  else {
    graph_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY | ID_RECALC_PSYS_RESET);
  }

  graph_relations_tag_update(main);

  wm_main_add_notifier(NC_OBJECT | ND_PARTICLE | NA_EDITED, NULL);
}

static ApiStruct *api_BoidRule_refine(struct ApiPtr *ptr)
{
  BoidRule *rule = (BoidRule *)ptr->data;

  switch (rule->type) {
    case eBoidRuleType_Goal:
      return &ApiBoidRuleGoal;
    case eBoidRuleType_Avoid:
      return &ApiBoidRuleAvoid;
    case eBoidRuleType_AvoidCollision:
      return &ApiBoidRuleAvoidCollision;
    case eBoidRuleType_FollowLeader:
      return &ApiBoidRuleFollowLeader;
    case eBoidRuleType_AverageSpeed:
      return &ApiBoidRuleAverageSpeed;
    case eBoidRuleType_Fight:
      return &ApiBoidRuleFight;
    default:
      return &ApiBoidRule;
  }
}

static char *Api_BoidRule_path(ApiPtr *ptr)
{
  BoidRule *rule = (BoidRule *)ptr->data;
  char name_esc[sizeof(rule->name) * 2];

  lib_str_escape(name_esc, rule->name, sizeof(name_esc));

  return lib_sprintfn("rules[\"%s\"]", name_esc); /* XXX not unique */
}

static ApiPtr api_BoidState_active_boid_rule_get(ApiPtr *ptr)
{
  BoidState *state = (BoidState *)ptr->data;
  BoidRule *rule = (BoidRule *)state->rules.first;

  for (; rule; rule = rule->next) {
    if (rule->flag & BOIDRULE_CURRENT) {
      return api_ptr_inherit_refine(ptr, &ApiBoidRule, rule);
    }
  }
  return api_ptr_inherit_refine(ptr, &ApiBoidRule, NULL);
}
static void api_BoidState_active_boid_rule_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  BoidState *state = (BoidState *)ptr->data;
  *min = 0;
  *max = max_ii(0, lib_list_count(&state->rules) - 1);
}

static int api_BoidState_active_boid_rule_index_get(ApiPtr *ptr)
{
  BoidState *state = (BoidState *)ptr->data;
  BoidRule *rule = (BoidRule *)state->rules.first;
  int i = 0;

  for (; rule; rule = rule->next, i++) {
    if (rule->flag & BOIDRULE_CURRENT) {
      return i;
    }
  }
  return 0;
}

static void api_BoidState_active_boid_rule_index_set(struct ApiPtr *ptr, int value)
{
  BoidState *state = (BoidState *)ptr->data;
  BoidRule *rule = (BoidRule *)state->rules.first;
  int i = 0;

  for (; rule; rule = rule->next, i++) {
    if (i == value) {
      rule->flag |= BOIDRULE_CURRENT;
    }
    else {
      rule->flag &= ~BOIDRULE_CURRENT;
    }
  }
}

static int particle_id_check(ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  return (GS(id->name) == ID_PA);
}

static char *api_BoidSettings_path(ApiPtr *ptr)
{
  BoidSettings *boids = (BoidSettings *)ptr->data;

  if (particle_id_check(ptr)) {
    ParticleSettings *part = (ParticleSettings *)ptr->owner_id;

    if (part->boids == boids) {
      return lib_strdup("boids");
    }
  }
  return NULL;
}

static ApiPtr api_BoidSettings_active_boid_state_get(ApiPtr *ptr)
{
  BoidSettings *boids = (BoidSettings *)ptr->data;
  BoidState *state = (BoidState *)boids->states.first;

  for (; state; state = state->next) {
    if (state->flag & BOIDSTATE_CURRENT) {
      return api_ptr_inherit_refine(ptr, &ApiBoidState, state);
    }
  }
  return api_ptr_inherit_refine(ptr, &ApiBoidState, NULL);
}
static void api_BoidSettings_active_boid_state_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  BoidSettings *boids = (BoidSettings *)ptr->data;
  *min = 0;
  *max = max_ii(0, lib_list_count(&boids->states) - 1);
}

static int api_BoidSettings_active_boid_state_index_get(ApiPtr *ptr)
{
  BoidSettings *boids = (BoidSettings *)ptr->data;
  BoidState *state = (BoidState *)boids->states.first;
  int i = 0;

  for (; state; state = state->next, i++) {
    if (state->flag & BOIDSTATE_CURRENT) {
      return i;
    }
  }
  return 0;
}

static void api_BoidSettings_active_boid_state_index_set(struct ApiPtr *ptr, int value)
{
  BoidSettings *boids = (BoidSettings *)ptr->data;
  BoidState *state = (BoidState *)boids->states.first;
  int i = 0;

  for (; state; state = state->next, i++) {
    if (i == value) {
      state->flag |= BOIDSTATE_CURRENT;
    }
    else {
      state->flag &= ~BOIDSTATE_CURRENT;
    }
  }
}

#else

static void api_def_boidrule_goal(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapoi, "BoidRuleGoal", "BoidRule");
  api_def_struct_ui_text(sapi, "Goal", "");
  api_def_struct_stype(sapi, "BoidRuleGoalAvoid");

  prop = api_def_prop(sapi, "object", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "ob");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Object", "Goal object");
  api_def_prop_update(prop, 0, "api_Boids_reset_deps");

  prop = api_def_prop(sapi, "use_predict", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "options", BRULE_GOAL_AVOID_PREDICT);
  api_def_prop_ui_text(prop, "Predict", "Predict target movement");
  api_def_prop_update(prop, 0, "api_Boids_reset");
}

static void api_def_boidrule_avoid(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = sapi_def_struct(dapi, "BoidRuleAvoid", "BoidRule");
  api_def_struct_ui_text(sapi, "Avoid", "");
  api_def_struct_stype(sapi, "BoidRuleGoalAvoid");

  prop = api_def_prop(sapi, "object", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "ob");
  api_def_prop_flag(prop, PROP_EDITABLE);
  apo_def_prop_ui_text(prop, "Object", "Object to avoid");
  api_def_prop_update(prop, 0, "api_Boids_reset_deps");

  prop = api_def_prop(sapi, "use_predict", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "options", BRULE_GOAL_AVOID_PREDICT);
  api_def_prop_ui_text(prop, "Predict", "Predict target movement");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "fear_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(
      prop, "Fear Factor", "Avoid object if danger from it is above this threshold");
  api_def_prop_update(prop, 0, "api_Boids_reset");
}

static void api_def_boidrule_avoid_collision(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BoidRuleAvoidCollision", "BoidRule");
  api_def_struct_ui_text(sapi, "Avoid Collision", "");

  prop = api_def_prop(sapi, "use_avoid", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "options", BRULE_ACOLL_WITH_BOIDS);
  api_def_prop_ui_text(prop, "Boids", "Avoid collision with other boids");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "use_avoid_collision", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "options", BRULE_ACOLL_WITH_DEFLECTORS);
  api_def_prop_ui_text(prop, "Deflectors", "Avoid collision with deflector objects");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "look_ahead", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(prop, "Look Ahead", "Time to look ahead in seconds");
  api_def_prop_update(prop, 0, "api_Boids_reset");
}

static void api_def_boidrule_follow_leader(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BoidRuleFollowLeader", "BoidRule");
  api_def_struct_ui_text(sapi, "Follow Leader", "");

  prop = api_def_prop(sapi, "object", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "ob");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Object", "Follow this object instead of a boid");
  api_def_prop_update(prop, 0, "api_Boids_reset_deps");

  prop = api_def_prop(sapi, "distance", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(prop, "Distance", "Distance behind leader to follow");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "queue_count", PROP_INT, PROP_NONE);
  api_def_prop_int_sapi(prop, NULL, "queue_size");
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(prop, "Queue Size", "How many boids in a line");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "use_line", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "options", BRULE_LEADER_IN_LINE);
  api_def_prop_ui_text(prop, "Line", "Follow leader in a line");
  api_def_prop_update(prop, 0, "api_Boids_reset");
}

static void api_def_boidrule_average_speed(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BoidRuleAverageSpeed", "BoidRule");
  api_def_struct_ui_text(sapi, "Average Speed", "");

  prop = api_def_prop(sapi, "wander", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Wander", "How fast velocity's direction is randomized");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "level", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Level", "How much velocity's z-component is kept constant");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "speed", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Speed", "Percentage of maximum speed");
  api_def_prop_update(prop, 0, "api_Boids_reset");
}

static void api_def_boidrule_fight(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BoidRuleFight", "BoidRule");
  api_def_struct_ui_text(sapi, "Fight", "");

  prop = api_def_prop(sapi, "distance", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(prop, "Fight Distance", "Attack boids at max this distance");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "flee_distance", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(prop, "Flee Distance", "Flee to this distance");
  api_def_prop_update(prop, 0, "api_Boids_reset");
}

static void api_def_boidrule(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* data */
  sapi = api_def_struct(dapi, "BoidRule", NULL);
  api_def_struct_ui_text(sapi, "Boid Rule", "");
  api_def_struct_refine_fn(sapi, "api_BoidRule_refine");
  api_def_struct_path_fn(sapi, "api_BoidRule_path");

  /* strings */
  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Boid rule name");
  api_def_struct_name_prop(sapi, prop);

  /* enums */
  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_enum_stype(prop, NULL, "type");
  api_def_prop_enum_items(prop, api_enum_boidrule_type_items);
  api_def_prop_ui_text(prop, "Type", "");

  /* flags */
  prop = api_def_prop(sapi, "use_in_air", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BOIDRULE_IN_AIR);
  api_def_prop_ui_text(prop, "In Air", "Use rule when boid is flying");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "use_on_land", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BOIDRULE_ON_LAND);
  api_def_prop_ui_text(prop, "On Land", "Use rule when boid is on land");
  api_def_prop_update(prop, 0, "api_Boids_reset");

#  if 0
  prop = api_def_prop(sapi, "show_expanded", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_NO_DEG_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "mode", eModifierMode_Expanded);
  api_def_prop_ui_text(prop, "Expanded", "Set modifier expanded in the user interface");
#  endif

  /* types */
  api_def_boidrule_goal(dapi);
  api_def_boidrule_avoid(dapi);
  api_def_boidrule_avoid_collision(dapi);
  api_def_boidrule_follow_leader(dapi);
  api_def_boidrule_average_speed(dapi);
  api_def_boidrule_fight(dapi);
}

static void api_def_boidstate(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sali = api_def_struct(dapi, "BoidState", NULL);
  api_def_struct_ui_text(sapi, "Boid State", "Boid state for boid physics");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Boid state name");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "ruleset_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, boidruleset_type_items);
  api_def_prop_ui_text(prop, "Rule Evaluation", "How the rules in the list are evaluated");

  prop = api_def_prop(sapi, "rules", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "BoidRule");
  api_def_prop_ui_text(prop, "Boid Rules", "");

  prop = api_def_prop(sapi, "active_boid_rule", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "BoidRule");
  api_def_prop_ptr_fns(prop, "api_BoidState_active_boid_rule_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Active Boid Rule", "");

  prop = api_def_prop(sapi, "active_boid_rule_index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_fns(prop,
                       "api_BoidState_active_boid_rule_index_get",
                       "api_BoidState_active_boid_rule_index_set",
                       "api_BoidState_active_boid_rule_index_range");
  api_def_prop_ui_text(prop, "Active Boid Rule Index", "");

  prop = api_def_prop(sapi, "rule_fuzzy", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "rule_fuzziness");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "Rule Fuzziness", "");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "volume", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_text(prop, "Volume", "");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "falloff", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 10.0);
  api_def_prop_ui_text(prop, "Falloff", "");
  api_def_prop_update(prop, 0, "api_Boids_reset");
}
static void api_def_boid_settings(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BoidSettings", NULL);
  api_def_struct_path_fn(sapi, "api_BoidSettings_path");
  api_def_struct_ui_text(sapi, "Boid Settings", "Settings for boid physics");

  prop = api_def_prop(sapi, "land_smooth", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "landing_smoothness");
  api_def_prop_range(prop, 0.0, 10.0);
  api_def_prop_ui_text(prop, "Landing Smoothness", "How smoothly the boids land");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "bank", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "banking");
  api_def_prop_range(prop, 0.0, 2.0);
  api_def_prop_ui_text(prop, "Banking", "Amount of rotation around velocity vector on turns");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "pitch", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "pitch");
  api_def_prop_range(prop, 0.0, 2.0);
  api_def_prop_ui_text(prop, "Pitch", "Amount of rotation around side vector");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "height", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 2.0);
  api_def_prop_ui_text(prop, "Height", "Boid height relative to particle size");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  /* states */
  prop = api_def_prop(sapi, "states", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "BoidState");
  api_def_prop_ui_text(prop, "Boid States", "");

  prop = api_def_prop(sapi, "active_boid_state", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "BoidRule");
  api_def_prop_ptr_fns(prop, "api_BoidSettings_active_boid_state_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Active Boid Rule", "");

  prop = api_def_prop(sapi, "active_boid_state_index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_fns(prop,
                       "api_BoidSettings_active_boid_state_index_get",
                       "api_BoidSettings_active_boid_state_index_set",
                       "api_BoidSettings_active_boid_state_index_range");
  api_def_prop_ui_text(prop, "Active Boid State Index", "");

  /* character properties */
  prop = api_def_prop(sapi, "health", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_text(prop, "Health", "Initial boid health when born");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "strength", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_text(prop, "Strength", "Maximum caused damage on attack per second")
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "aggression", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_text(prop, "Aggression", "Boid will fight this times stronger enemy");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = ali_def_prop(sapi, "accuracy", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "Accuracy", "Accuracy of attack");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "range", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_text(prop, "Range", "Maximum distance from which a boid can attack");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  /* physical properties */
  prop = api_def_prop(sapi, "air_speed_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "air_min_speed");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(
      prop, "Min Air Speed", "Minimum speed in air (relative to maximum speed)");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "air_speed_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "air_max_speed");
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_text(prop, "Max Air Speed", "Maximum speed in air");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "air_acc_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "air_max_acc");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(
      prop, "Max Air Acceleration", "Maximum acceleration in air (relative to maximum speed)");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "air_ave_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "air_max_ave");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop,
                           "Max Air Angular Velocity",
                           "Maximum angular velocity in air (relative to 180 degrees)");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "air_personal_space", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 10.0);
  api_def_prop_ui_text(
      prop, "Air Personal Space", "Radius of boids personal space in air (% of particle size)");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "land_jump_speed", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_text(prop, "Jump Speed", "Maximum speed for jumping");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "land_speed_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "land_max_speed");
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_text(prop, "Max Land Speed", "Maximum speed on land");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "land_acc_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "land_max_acc");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(
      prop, "Max Land Acceleration", "Maximum acceleration on land (relative to maximum speed)");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "land_ave_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "land_max_ave");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop,
                       "Max Land Angular Velocity",
                       "Maximum angular velocity on land (relative to 180 degrees)");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "land_personal_space", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 10.0);
  api_def_prop_ui_text(
      prop, "Land Personal Space", "Radius of boids personal space on land (% of particle size)");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "land_stick_force", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 1000.0);
  api_def_prop_ui_text(
      prop, "Land Stick Force", "How strong a force must be to start effecting a boid on land");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  /* options */
  prop = api_def_prop(sapi, "use_flight", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "options", BOID_ALLOW_FLIGHT);
  api_def_prop_ui_text(prop, "Allow Flight", "Allow boids to move in air");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "use_land", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "options", BOID_ALLOW_LAND);
  api_def_prop_ui_text(prop, "Allow Land", "Allow boids to move on land");
  api_def_prop_update(prop, 0, "api_Boids_reset");

  prop = api_def_prop(sapi, "use_climb", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "options", BOID_ALLOW_CLIMB);
  api_def_prop_ui_text(prop, "Allow Climbing", "Allow boids to climb goal objects");
  api_def_prop_update(prop, 0, "api_Boids_reset");
}

void api_def_boid(DuneApi *dapi)
{
  api_def_boidrule(dapi);
  api_def_boidstate(dapi);
  api_def_boid_settings(dapi);
}

#endif
