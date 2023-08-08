#include <stdlib.h>
#include <string.h>

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_collection.h"
#include "types_object.h"
#include "types_rigidbody.h"
#include "types_scene.h"

#include "lib_math.h"
#include "lib_utildefines.h"

#include "graph_build.h"

#include "wm_types.h"

/* roles of objects in RigidBody Sims */
const EnumPropItem api_enum_rigidbody_object_type_items[] = {
    {RBO_TYPE_ACTIVE,
     "ACTIVE",
     0,
     "Active",
     "Object is directly controlled by simulation results"},
    {RBO_TYPE_PASSIVE,
     "PASSIVE",
     0,
     "Passive",
     "Object is directly controlled by animation system"},
    {0, NULL, 0, NULL, NULL},
};

/* collision shapes of objects in rigid body sim */
const EnumPropItem api_enum_rigidbody_object_shape_items[] = {
    {RB_SHAPE_BOX,
     "BOX",
     ICON_MESH_CUBE,
     "Box",
     "Box-like shapes (i.e. cubes), including planes (i.e. ground planes)"},
    {RB_SHAPE_SPHERE, "SPHERE", ICON_MESH_UVSPHERE, "Sphere", ""},
    {RB_SHAPE_CAPSULE, "CAPSULE", ICON_MESH_CAPSULE, "Capsule", ""},
    {RB_SHAPE_CYLINDER, "CYLINDER", ICON_MESH_CYLINDER, "Cylinder", ""},
    {RB_SHAPE_CONE, "CONE", ICON_MESH_CONE, "Cone", ""},
    {RB_SHAPE_CONVEXH,
     "CONVEX_HULL",
     ICON_MESH_ICOSPHERE,
     "Convex Hull",
     "A mesh-like surface encompassing (i.e. shrinkwrap over) all vertices (best results with "
     "fewer vertices)"},
    {RB_SHAPE_TRIMESH,
     "MESH",
     ICON_MESH_MONKEY,
     "Mesh",
     "Mesh consisting of triangles only, allowing for more detailed interactions than convex "
     "hulls"},
    {RB_SHAPE_COMPOUND,
     "COMPOUND",
     ICON_MESH_DATA,
     "Compound Parent",
     "Combines all of its direct rigid body children into one rigid object"},
    {0, NULL, 0, NULL, NULL},
};

/* collision shapes of constraints in rigid body sim */
const EnumPropItem api_enum_rigidbody_constraint_type_items[] = {
    {RBC_TYPE_FIXED, "FIXED", ICON_NONE, "Fixed", "Glue rigid bodies together"},
    {RBC_TYPE_POINT,
     "POINT",
     ICON_NONE,
     "Point",
     "Constrain rigid bodies to move around common pivot point"},
    {RBC_TYPE_HINGE, "HINGE", ICON_NONE, "Hinge", "Restrict rigid body rotation to one axis"},
    {RBC_TYPE_SLIDER,
     "SLIDER",
     ICON_NONE,
     "Slider",
     "Restrict rigid body translation to one axis"},
    {RBC_TYPE_PISTON,
     "PISTON",
     ICON_NONE,
     "Piston",
     "Restrict rigid body translation and rotation to one axis"},
    {RBC_TYPE_6DOF,
     "GENERIC",
     ICON_NONE,
     "Generic",
     "Restrict translation and rotation to specified axes"},
    {RBC_TYPE_6DOF_SPRING,
     "GENERIC_SPRING",
     ICON_NONE,
     "Generic Spring",
     "Restrict translation and rotation to specified axes with springs"},
    {RBC_TYPE_MOTOR, "MOTOR", ICON_NONE, "Motor", "Drive rigid body around or along an axis"},
    {0, NULL, 0, NULL, NULL},
};

/* bullet spring type */
static const EnumPropItem api_enum_rigidbody_constraint_spring_type_items[] = {
    {RBC_SPRING_TYPE1,
     "SPRING1",
     ICON_NONE,
     "Dune 2.7",
     "Spring implementation used in blender 2.7. Damping is capped at 1.0"},
    {RBC_SPRING_TYPE2,
     "SPRING2",
     ICON_NONE,
     "Dune 2.8",
     "New implementation available since 2.8"},
    {0, NULL, 0, NULL, NULL},
};

#ifndef API_RUNTIME
/* mesh source for collision shape creation */
static const EnumPropItem rigidbody_mesh_source_items[] = {
    {RBO_MESH_BASE, "BASE", 0, "Base", "Base mesh"},
    {RBO_MESH_DEFORM, "DEFORM", 0, "Deform", "Deformations (shape keys, deform modifiers)"},
    {RBO_MESH_FINAL, "FINAL", 0, "Final", "All modifiers"},
    {0, NULL, 0, NULL, NULL},
};
#endif

#ifdef API_RUNTIME

#  ifdef WITH_BULLET
#    include "RBI_api.h"
#  endif

#  include "dune_rigidbody.h"

#  include "wm_api.h"

/* ******************************** */

static void api_RigidBodyWorld_reset(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  RigidBodyWorld *rbw = (RigidBodyWorld *)ptr->data;

  dune_rigidbody_cache_reset(rbw);
}

static char *api_RigidBodyWorld_path(ApiPtr *UNUSED(ptr))
{
  return lib_strdup("rigidbody_world");
}

static void api_RigidBodyWorld_num_solver_iter_set(ApiPtr *ptr, int value)
{
  RigidBodyWorld *rbw = (RigidBodyWorld *)ptr->data;

  rbw->num_solver_iter = value;

#  ifdef WITH_BULLET
  if (rbw->shared->phys_world) {
    RB_dworld_set_solver_iterations(rbw->shared->phys_world, value);
  }
#  endif
}

static void api_RigidBodyWorld_split_impulse_set(ApiPtr *ptr, bool value)
{
  RigidBodyWorld *rbw = (RigidBodyWorld *)ptr->data;

  SET_FLAG_FROM_TEST(rbw->flag, value, RBW_FLAG_USE_SPLIT_IMPULSE);

#  ifdef WITH_BULLET
  if (rbw->shared->physics_world) {
    RB_dworld_set_split_impulse(rbw->shared->phys_world, value);
  }
#  endif
}

static void api_RigidBodyWorld_objects_collection_update(Main *main,
                                                         Scene *scene,
                                                         ApiPtr *ptr)
{
  RigidBodyWorld *rbw = (RigidBodyWorld *)ptr->data;
  dune_rigidbody_objects_collection_validate(scene, rbw);
  api_RigidBodyWorld_reset(main, scene, ptr);
}

static void api_RigidBodyWorld_constraints_collection_update(Main *main,
                                                             Scene *scene,
                                                             ApiPtr *ptr)
{
  RigidBodyWorld *rbw = (RigidBodyWorld *)ptr->data;
  dune_rigidbody_constraints_collection_validate(scene, rbw);
  api_RigidBodyWorld_reset(main, scene, ptr);
}

/* ******************************** */

static void api_RigidBodyOb_reset(Main *UNUSED(main), Scene *scene, ApiPtr *UNUSED(ptr))
{
  if (scene != NULL) {
    RigidBodyWorld *rbw = scene->rigidbody_world;
    dune_rigidbody_cache_reset(rbw);
  }
}

static void api_RigidBodyOb_shape_update(Main *main, Scene *scene, PointerRNA *ptr)
{
  Object *ob = (Object *)ptr->owner_id;

  api_RigidBodyOb_reset(main, scene, ptr);
  graph_relations_tag_update(main);

  wm_main_add_notifier(NC_OBJECT | ND_DRAW, ob);
}

static void api_RigidBodyOb_shape_reset(Main *UNUSED(main), Scene *scene, ApiPtr *ptr)
{
  if (scene != NULL) {
    RigidBodyWorld *rbw = scene->rigidbody_world;
    dune_rigidbody_cache_reset(rbw);
  }

  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;
  if (rbo->shared->phys_shape) {
    rbo->flag |= RBO_FLAG_NEEDS_RESHAPE;
  }
}

static void api_RigidBodyOb_mesh_source_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;

  api_RigidBodyOb_reset(main, scene, ptr);
  graph_relations_tag_update(main);

  wm_main_add_notifier(NC_OBJECT | ND_DRAW, ob);
}

static char *api_RigidBodyOb_path(ApiPtr *UNUSED(ptr))
{
  /* NOTE: this hardcoded path should work as long as only Objects have this */
  return lib_strdup("rigid_body");
}

static void api_RigidBodyOb_type_set(ApiPtr *ptr, int value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->type = value;
  rbo->flag |= RBO_FLAG_NEEDS_VALIDATE;
}

static void api_RigidBodyOb_shape_set(ApiPtr *ptr, int value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->shape = value;
  rbo->flag |= RBO_FLAG_NEEDS_VALIDATE;
}

static void api_RigidBodyOb_disabled_set(ApiPtr *ptr, bool value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  SET_FLAG_FROM_TEST(rbo->flag, !value, RBO_FLAG_DISABLED);

#  ifdef WITH_BULLET
  /* update kinematic state if necessary - only needed for active bodies */
  if ((rbo->shared->phys_object) && (rbo->type == RBO_TYPE_ACTIVE)) {
    rbody_set_mass(rbo->shared->phys_object, RBO_GET_MASS(rbo));
    rbody_set_kinematic_state(rbo->shared->phys_object, !value);
    rbo->flag |= RBO_FLAG_NEEDS_VALIDATE;
  }
#  endif
}

static void api_RigidBodyOb_mass_set(ApiPtr *ptr, float value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->mass = value;

#  ifdef WITH_BULLET
  /* only active bodies need mass update */
  if ((rbo->shared->phys_object) && (rbo->type == RBO_TYPE_ACTIVE)) {
    rbody_set_mass(rbo->shared->phys_object, RBO_GET_MASS(rbo));
  }
#  endif
}

static void api_RigidBodyOb_friction_set(ApiPtr *ptr, float value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->friction = value;

#  ifdef WITH_BULLET
  if (rbo->shared->phys_object) {
    rbody_set_friction(rbo->shared->phys_object, value);
  }
#  endif
}

static void api_RigidBodyOb_restitution_set(ApiPtr *ptr, float value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->restitution = value;
#  ifdef WITH_BULLET
  if (rbo->shared->physics_object) {
    rbody_set_restitution(rbo->shared->phys_object, value);
  }
#  endif
}

static void api_RigidBodyOb_collision_margin_set(ApiPtr *ptr, float value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->margin = value;

#  ifdef WITH_BULLET
  if (rbo->shared->physics_shape) {
    rbody_shape_set_margin(rbo->shared->phys_shape, RBO_GET_MARGIN(rbo));
  }
#  endif
}

static void api_RigidBodyOb_collision_collections_set(ApiPtr *ptr, const bool *values)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;
  int i;

  for (i = 0; i < 20; i++) {
    if (values[i]) {
      rbo->col_groups |= (1 << i);
    } else {
      rbo->col_groups &= ~(1 << i);
    }
  }
  rbo->flag |= RBO_FLAG_NEEDS_VALIDATE;
}

static void api_RigidBodyOb_kinematic_state_set(ApiPtr *ptr, bool value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  SET_FLAG_FROM_TEST(rbo->flag, value, RBO_FLAG_KINEMATIC);

#  ifdef WITH_BULLET
  /* update kinematic state if necessary */
  if (rbo->shared->phys_object) {
    rbody_set_mass(rbo->shared->phys_object, RBO_GET_MASS(rbo));
    rbody_set_kinematic_state(rbo->shared->phys_object, value);
    rbo->flag |= RBO_FLAG_NEEDS_VALIDATE;
  }
#  endif
}

static void api_RigidBodyOb_activation_state_set(ApiPtr *ptr, bool value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  SET_FLAG_FROM_TEST(rbo->flag, value, RBO_FLAG_USE_DEACTIVATION);

#  ifdef WITH_BULLET
  /* update activation state if necessary - only active bodies can be deactivated */
  if ((rbo->shared->physics_object) && (rbo->type == RBO_TYPE_ACTIVE)) {
    rbody_set_activation_state(rbo->shared->phys_object, value);
  }
#  endif
}

static void api_RigidBodyOb_linear_sleepThresh_set(ApiPtr *ptr, float value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->lin_sleep_thresh = value;

#  ifdef WITH_BULLET
  /* only active bodies need sleep threshold update */
  if ((rbo->shared->phys_object) && (rbo->type == RBO_TYPE_ACTIVE)) {
    rbody_set_linear_sleep_thresh(rbo->shared->phys_object, value);
  }
#  endif
}

static void api_RigidBodyOb_angular_sleepThresh_set(ApiPtr *ptr, float value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->ang_sleep_thresh = value;

#  ifdef WITH_BULLET
  /* only active bodies need sleep threshold update */
  if ((rbo->shared->phys_object) && (rbo->type == RBO_TYPE_ACTIVE)) {
    rbody_body_set_angular_sleep_thresh(rbo->shared->phys_object, value);
  }
#  endif
}

static void api_RigidBodyOb_linear_damping_set(ApiPtr *ptr, float value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->lin_damping = value;

#  ifdef WITH_BULLET
  /* only active bodies need damping update */
  if ((rbo->shared->physics_object) && (rbo->type == RBO_TYPE_ACTIVE)) {
    rbody_set_linear_damping(rbo->shared->phys_object, value);
  }
#  endif
}

static void api_RigidBodyOb_angular_damping_set(ApiPtr *ptr, float value)
{
  RigidBodyOb *rbo = (RigidBodyOb *)ptr->data;

  rbo->ang_damping = value;

#  ifdef WITH_BULLET
  /* only active bodies need damping update */
  if ((rbo->shared->physics_object) && (rbo->type == RBO_TYPE_ACTIVE)) {
    rbody_set_angular_damping(rbo->shared->phys_object, value);
  }
#  endif
}

static char *api_RigidBodyCon_path(ApiPtr *UNUSED(ptr))
{
  /* NOTE: this hardcoded path should work as long as only Objects have this */
  return lib_strdup("rigid_body_constraint");
}

static void api_RigidBodyCon_type_set(ApiPtr *ptr, int value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->type = value;
  rbc->flag |= RBC_FLAG_NEEDS_VALIDATE;
}

static void api_RigidBodyCon_spring_type_set(ApiPtr *ptr, int value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_type = value;
  rbc->flag |= RBC_FLAG_NEEDS_VALIDATE;
}

static void api_RigidBodyCon_enabled_set(ApiPtr *ptr, bool value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  SET_FLAG_FROM_TEST(rbc->flag, value, RBC_FLAG_ENABLED);

#  ifdef WITH_BULLET
  if (rbc->phys_constraint) {
    rbody_constraint_set_enabled(rbc->phys_constraint, value);
  }
#  endif
}

static void api_RigidBodyCon_disable_collisions_set(ApiPtr *ptr, bool value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  SET_FLAG_FROM_TEST(rbc->flag, value, RBC_FLAG_DISABLE_COLLISIONS);

  rbc->flag |= RBC_FLAG_NEEDS_VALIDATE;
}

static void api_RigidBodyCon_use_breaking_set(ApiPtr *ptr, bool value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  if (value) {
    rbc->flag |= RBC_FLAG_USE_BREAKING;
#  ifdef WITH_BULLET
    if (rbc->phys_constraint) {
      rbody_constraint_set_breaking_threshold(rbc->physics_constraint, rbc->breaking_threshold);
    }
#  endif
  }
  else {
    rbc->flag &= ~RBC_FLAG_USE_BREAKING;
#  ifdef WITH_BULLET
    if (rbc->phys_constraint) {
      rbody_constraint_set_breaking_threshold(rbc->physics_constraint, FLT_MAX);
    }
#  endif
  }
}

static void api_RigidBodyCon_breaking_threshold_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->breaking_threshold = value;

#  ifdef WITH_BULLET
  if (rbc->phys_constraint && (rbc->flag & RBC_FLAG_USE_BREAKING)) {
    rbody_constraint_set_breaking_threshold(rbc->phys_constraint, value);
  }
#  endif
}

static void api_RigidBodyCon_override_solver_iter_set(ApiPtr *ptr, bool value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  if (value) {
    rbc->flag |= RBC_FLAG_OVERRIDE_SOLVER_ITERATIONS;
#  ifdef WITH_BULLET
    if (rbc->phys_constraint) {
      rbody_constraint_set_solver_iter(rbc->phys_constraint, rbc->num_solver_iter);
    }
#  endif
  } else {
    rbc->flag &= ~RBC_FLAG_OVERRIDE_SOLVER_ITERATIONS;
#  ifdef WITH_BULLET
    if (rbc->phys_constraint) {
      rbody_constraint_set_solver_iter(rbc->phys_constraint, -1);
    }
#  endif
  }
}

static void api_RigidBodyCon_num_solver_iter_set(ApiPtr *ptr, int value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->num_solver_iter = value;

#  ifdef WITH_BULLET
  if (rbc->phys_constraint && (rbc->flag & RBC_FLAG_OVERRIDE_SOLVER_ITER)) {
    rbody_constraint_set_solver_iter(rbc->phys_constraint, value);
  }
#  endif
}

#  ifdef WITH_BULLET
static void api_RigidBodyCon_do_set_spring_stiffness(RigidBodyCon *rbc,
                                                     float value,
                                                     int flag,
                                                     int axis)
{
  if (rbc->phys_constraint && rbc->type == RBC_TYPE_6DOF_SPRING && (rbc->flag & flag)) {
    switch (rbc->spring_type) {
      case RBC_SPRING_TYPE1:
        rbody_constraint_set_stiffness_6dof_spring(rbc->physics_constraint, axis, value);
        break;
      case RBC_SPRING_TYPE2:
        rbody_constraint_set_stiffness_6dof_spring2(rbc->physics_constraint, axis, value);
        break;
    }
  }
}
#  endif

static void api_RigidBodyCon_spring_stiffness_x_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_stiffness_x = value;

#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_stiffness(rbc, value, RBC_FLAG_USE_SPRING_X, RB_LIMIT_LIN_X);
#  endif
}

static void api_RigidBodyCon_spring_stiffness_y_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_stiffness_y = value;

#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_stiffness(rbc, value, RBC_FLAG_USE_SPRING_Y, RB_LIMIT_LIN_Y);
#  endif
}

static void api_RigidBodyCon_spring_stiffness_z_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_stiffness_z = value;

#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_stiffness(rbc, value, RBC_FLAG_USE_SPRING_Z, RB_LIMIT_LIN_Z);
#  endif
}

static void api_RigidBodyCon_spring_stiffness_ang_x_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_stiffness_ang_x = value;

#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_stiffness(rbc, value, RBC_FLAG_USE_SPRING_ANG_X, RB_LIMIT_ANG_X);
#  endif
}

static void api_RigidBodyCon_spring_stiffness_ang_y_set(PointerRNA *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_stiffness_ang_y = value;

#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_stiffness(rbc, value, RBC_FLAG_USE_SPRING_ANG_Y, RB_LIMIT_ANG_Y);
#  endif
}

static void api_RigidBodyCon_spring_stiffness_ang_z_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_stiffness_ang_z = value;

#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_stiffness(rbc, value, RBC_FLAG_USE_SPRING_ANG_Z, RB_LIMIT_ANG_Z);
#  endif
}

#  ifdef WITH_BULLET
static void api_RigidBodyCon_do_set_spring_damping(RigidBodyCon *rbc,
                                                   float value,
                                                   int flag,
                                                   int axis)
{
  if (rbc->physics_constraint && rbc->type == RBC_TYPE_6DOF_SPRING && (rbc->flag & flag)) {
    switch (rbc->spring_type) {
      case RBC_SPRING_TYPE1:
        RB_constraint_set_damping_6dof_spring(rbc->physics_constraint, axis, value);
        break;
      case RBC_SPRING_TYPE2:
        RB_constraint_set_damping_6dof_spring2(rbc->physics_constraint, axis, value);
        break;
    }
  }
}
#  endif

static void api_RigidBodyCon_spring_damping_x_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_damping_x = value;

#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_damping(rbc, value, RBC_FLAG_USE_SPRING_X, RB_LIMIT_LIN_X);
#  endif
}

static void api_RigidBodyCon_spring_damping_y_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_damping_y = value;
#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_damping(rbc, value, RBC_FLAG_USE_SPRING_Y, RB_LIMIT_LIN_Y);
#  endif
}

static void api_RigidBodyCon_spring_damping_z_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_damping_z = value;
#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_damping(rbc, value, RBC_FLAG_USE_SPRING_Z, RB_LIMIT_LIN_Z);
#  endif
}

static void api_RigidBodyCon_spring_damping_ang_x_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_damping_ang_x = value;

#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_damping(rbc, value, RBC_FLAG_USE_SPRING_ANG_X, RB_LIMIT_ANG_X);
#  endif
}

static void api_RigidBodyCon_spring_damping_ang_y_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_damping_ang_y = value;
#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_damping(rbc, value, RBC_FLAG_USE_SPRING_ANG_Y, RB_LIMIT_ANG_Y);
#  endif
}

static void api_RigidBodyCon_spring_damping_ang_z_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->spring_damping_ang_z = value;
#  ifdef WITH_BULLET
  api_RigidBodyCon_do_set_spring_damping(rbc, value, RBC_FLAG_USE_SPRING_ANG_Z, RB_LIMIT_ANG_Z);
#  endif
}

static void api_RigidBodyCon_motor_lin_max_impulse_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->motor_lin_max_impulse = value;

#  ifdef WITH_BULLET
  if (rbc->physics_constraint && rbc->type == RBC_TYPE_MOTOR) {
    rbody_constraint_set_max_impulse_motor(
        rbc->physics_constraint, value, rbc->motor_ang_max_impulse);
  }
#  endif
}

static void api_RigidBodyCon_use_motor_lin_set(ApiPtr *ptr, bool value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  SET_FLAG_FROM_TEST(rbc->flag, value, RBC_FLAG_USE_MOTOR_LIN);

#  ifdef WITH_BULLET
  if (rbc->phys_constraint) {
    rbody_constraint_set_enable_motor(rbc->phys_constraint,
                                   rbc->flag & RBC_FLAG_USE_MOTOR_LIN,
                                   rbc->flag & RBC_FLAG_USE_MOTOR_ANG);
  }
#  endif
}

static void api_RigidBodyCon_use_motor_ang_set(ApiPtr *ptr, bool value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  SET_FLAG_FROM_TEST(rbc->flag, value, RBC_FLAG_USE_MOTOR_ANG);

#  ifdef WITH_BULLET
  if (rbc->phys_constraint) {
    rbody_constraint_set_enable_motor(rbc->phys_constraint,
                                   rbc->flag & RBC_FLAG_USE_MOTOR_LIN,
                                   rbc->flag & RBC_FLAG_USE_MOTOR_ANG);
  }
#  endif
}

static void api_RigidBodyCon_motor_lin_target_velocity_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->motor_lin_target_velocity = value;

#  ifdef WITH_BULLET
  if (rbc->phys_constraint && rbc->type == RBC_TYPE_MOTOR) {
    rbody_constraint_set_target_velocity_motor(
        rbc->phys_constraint, value, rbc->motor_ang_target_velocity);
  }
#  endif
}

static void api_RigidBodyCon_motor_ang_max_impulse_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->motor_ang_max_impulse = value;

#  ifdef WITH_BULLET
  if (rbc->phys_constraint && rbc->type == RBC_TYPE_MOTOR) {
    rbody_constraint_set_max_impulse_motor(
        rbc->phys_constraint, rbc->motor_lin_max_impulse, value);
  }
#  endif
}

static void api_RigidBodyCon_motor_ang_target_velocity_set(ApiPtr *ptr, float value)
{
  RigidBodyCon *rbc = (RigidBodyCon *)ptr->data;

  rbc->motor_ang_target_velocity = value;

#  ifdef WITH_BULLET
  if (rbc->phys_constraint && rbc->type == RBC_TYPE_MOTOR) {
    rbody_constraint_set_target_velocity_motor(
        rbc->phys_constraint, rbc->motor_lin_target_velocity, value);
  }
#  endif
}

/* Sweep test */
static void api_RigidBodyWorld_convex_sweep_test(RigidBodyWorld *rbw,
                                                 ReportList *reports,
                                                 Object *object,
                                                 float ray_start[3],
                                                 float ray_end[3],
                                                 float r_location[3],
                                                 float r_hitpoint[3],
                                                 float r_normal[3],
                                                 int *r_hit)
{
#  ifdef WITH_BULLET
  RigidBodyOb *rob = object->rigidbody_object;

  if (rbw->shared->phys_world != NULL && rob->shared->phys_object != NULL) {
    rbody_world_convex_sweep_test(rbw->shared->phys_world,
                               rob->shared->phys_object,
                               ray_start,
                               ray_end,
                               r_location,
                               r_hitpoint,
                               r_normal,
                               r_hit);
    if (*r_hit == -2) {
      dune_report(reports,
                 RPT_ERROR,
                 "A non convex collision shape was passed to the function, use only convex "
                 "collision shapes");
    }
  } else {
    *r_hit = -1;
    dune_report(reports,
               RPT_ERROR,
               "Rigidbody world was not properly initialized, need to step the simulation first");
  }
#  else
  UNUSED_VARS(rbw, reports, object, ray_start, ray_end, r_location, r_hitpoint, r_normal, r_hit);
#  endif
}

static ApiPtr api_RigidBodyWorld_PointCache_get(ApiPtr *ptr)
{
  RigidBodyWorld *rbw = ptr->data;
  return api_ptr_inherit_refine(ptr, &ApiPointCache, rbw->shared->pointcache);
}

#else

static void api_def_rigidbody_world(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "RigidBodyWorld", NULL);
  api_def_struct_stype(sapi, "RigidBodyWorld");
  api_def_struct_ui_text(
      sapi, "Rigid Body World", "Self-contained rigid body simulation environment and settings");
  api_def_struct_path_fn(sapi, "api_RigidBodyWorld_path");

  /* groups */
  prop = api_def_prop(sapi, "collection", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Collection");
  api_def_prop_ptr_stype(prop, NULL, "group");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(
      prop, "Collection", "Collection containing objects participating in this simulation");
  api_def_prop_update(prop, NC_SCENE, "api_RigidBodyWorld_objects_collection_update");

  prop = api_def_prop(sapi, "constraints", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Collection");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(
      prop, "Constraints", "Collection containing rigid body constraint objects");
  api_def_prop_update(prop, NC_SCENE, "api_RigidBodyWorld_constraints_collection_update");

  /* booleans */
  prop = api_def_prop(sapi, "enabled", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", RBW_FLAG_MUTED);
  api_def_prop_ui_text(prop, "Enabled", "Simulation will be evaluated");
  api_def_prop_update(prop, NC_SCENE, NULL);

  /* time scale */
  prop = api_def_prop(sapi, "time_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "time_scale");
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_range(prop, 0.0f, 10.0f, 1, 3);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_text(prop, "Time Scale", "Change the speed of the simulation");
  api_def_prop_update(prop, NC_SCENE, "api_RigidBodyWorld_reset");

  /* timestep */
  prop = api_def_prop(sapi, "substeps_per_frame", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "substeps_per_frame");
  api_def_prop_range(prop, 1, SHRT_MAX);
  api_def_prop_ui_range(prop, 1, 1000, 1, -1);
  api_def_prop_int_default(prop, 10);
  api_def_prop_ui_text(
      prop,
      "Substeps Per Frame",
      "Number of simulation steps taken per frame (higher values are more accurate "
      "but slower)");
  api_def_prop_update(prop, NC_SCENE, "api_RigidBodyWorld_reset");

  /* constraint solver iterations */
  prop = api_def_prop(sapi, "solver_iters", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "num_solver_iters");
  api_def_prop_range(prop, 1, 1000);
  api_def_prop_ui_range(prop, 10, 100, 1, -1);
  api_def_prop_int_default(prop, 10);
  api_def_prop_int_fns(prop, NULL, "api_RigidBodyWorld_num_solver_iterations_set", NULL);
  api_def_prop_ui_text(
      prop,
      "Solver Iterations",
      "Number of constraint solver iterations made per simulation step (higher values are more "
      "accurate but slower)");
  api_def_prop_update(prop, NC_SCENE, "api_RigidBodyWorld_reset");

  /* split impulse */
  prop = api_def_prop(sapi, "use_split_impulse", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBW_FLAG_USE_SPLIT_IMPULSE);
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyWorld_split_impulse_set");
  api_def_prop_ui_text(
      prop,
      "Split Impulse",
      "Reduce extra velocity that can build up when objects collide (lowers simulation "
      "stability a little so use only when necessary)");
  api_def_prop_update(prop, NC_SCENE, "api_RigidBodyWorld_reset");

  /* cache */
  prop = api_def_prop(sapi, "point_cache", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_pointer_fns(prop, "api_RigidBodyWorld_PointCache_get", NULL, NULL, NULL);
  api_def_prop_struct_type(prop, "PointCache");
  api_def_prop_ui_text(prop, "Point Cache", "");

  /* effector weights */
  prop = api_def_prop(sapi, "effector_weights", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "EffectorWeights");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Effector Weights", "");

  /* Sweep test */
  fn = api_def_fn(sapi, "convex_sweep_test", "api_RigidBodyWorld_convex_sweep_test");
  api_def_fn_ui_description(
      fn, "Sweep test convex rigidbody against the current rigidbody world");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(
      fn, "object", "Object", "", "Rigidbody object with a convex collision shape");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
  /* ray start and end */
  parm = api_def_float_vector(fb, "start", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float_vector(fn, "end", 3, NULL, -FLT_MAX, FLT_MAX, "", "", -1e4, 1e4);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float_vector(fn,
                              "object_location",
                              3,
                              NULL,
                              -FLT_MAX,
                              FLT_MAX,
                              "Location",
                              "The hit location of this sweep test",
                              -1e4,
                              1e4);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);
  parm = api_def_float_vector(fn,
                              "hitpoint",
                              3,
                              NULL,
                              -FLT_MAX,
                              FLT_MAX,
                              "Hitpoint",
                              "The hit location of this sweep test",
                              -1e4,
                              1e4);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);
  parm = api_def_float_vector(fn,
                              "normal",
                              3,
                              NULL,
                              -FLT_MAX,
                              FLT_MAX,
                              "Normal",
                              "The face normal at the sweep test hit location",
                              -1e4,
                              1e4);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);
  parm = api_def_int(fn,
                     "has_hit",
                     0,
                     0,
                     0,
                     "",
                     "If the function has found collision point, value is 1, otherwise 0",
                     0,
                     0);
  api_def_fn_output(fn, parm);
}

static void api_def_rigidbody_object(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "RigidBodyObject", NULL);
  api_def_struct_stype(sapi, "RigidBodyOb");
  api_def_struct_ui_text(
      sapi, "Rigid Body Object", "Settings for object participating in Rigid Body Simulation");
  api_def_struct_path_fn(sapi, "api_RigidBodyOb_path");

  /* Enums */
  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "type");
  api_def_prop_enum_items(prop, api_enum_rigidbody_object_type_items);
  api_def_prop_enum_fns(prop, NULL, "api_RigidBodyOb_type_set", NULL);
  api_def_prop_ui_text(prop, "Type", "Role of object in Rigid Body Simulations");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "mesh_source", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mesh_source");
  api_def_prop_enum_items(prop, rigidbody_mesh_source_items);
  api_def_prop_ui_text(
      prop, "Mesh Source", "Source of the mesh used to create collision shape");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_mesh_source_update");

  /* booleans */
  prop = api_def_prop(sapi, "enabled", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", RBO_FLAG_DISABLED);
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyOb_disabled_set");
  api_def_prop_ui_text(prop, "Enabled", "Rigid Body actively participates to the simulation");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "collision_shape", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "shape");
  api_def_prop_enum_items(prop, api_enum_rigidbody_object_shape_items);
  api_def_prop_enum_fns(prop, NULL, "api_RigidBodyOb_shape_set", NULL);
  api_def_prop_ui_text(
      prop, "Collision Shape", "Collision Shape of object in Rigid Body Simulations");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_shape_update");

  prop = api_def_prop(sapi, "kinematic", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBO_FLAG_KINEMATIC);
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyOb_kinematic_state_set");
  api_def_prop_ui_text(
      prop, "Kinematic", "Allow rigid body to be controlled by the animation system");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_deform", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBO_FLAG_USE_DEFORM);
  api_def_prop_ui_text(prop, "Deforming", "Rigid body deforms during simulation");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  /* Physics Parameters */
  prop = api_def_prop(sapi, "mass", PROP_FLOAT, PROP_UNIT_MASS);
  api_def_prop_float_stype(prop, NULL, "mass");
  api_def_prop_range(prop, 0.001f, FLT_MAX); /* range must always be positive (and non-zero) */
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_float_fns(prop, NULL, "rna_RigidBodyOb_mass_set", NULL);
  api_def_prop_ui_text(prop, "Mass", "How much the object 'weighs' irrespective of gravity");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  /* Dynamics Parameters - Activation */
  /* TODO: define and figure out how to implement these. */

  /* Dynamics Parameters - Deactivation */
  prop = api_def_prop(sapi, "use_deactivation", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBO_FLAG_USE_DEACTIVATION);
  api_def_prop_bool_default(prop, true);
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyOb_activation_state_set");
  api_def_prop_ui_text(
      prop,
      "Enable Deactivation",
      "Enable deactivation of resting rigid bodies (increases performance and stability "
      "but can cause glitches)");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_start_deactivated", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBO_FLAG_START_DEACTIVATED);
  api_def_prop_ui_text(
      prop, "Start Deactivated", "Deactivate rigid body at the start of the simulation");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "deactivate_linear_velocity", PROP_FLOAT, PROP_UNIT_VELOCITY);
  api_def_prop_float_stype(prop, NULL, "lin_sleep_thresh");
  api_def_prop_range(
      prop, FLT_MIN, FLT_MAX); /* range must always be positive (and non-zero) */
  api_def_prop_float_default(prop, 0.4f);l
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyOb_linear_sleepThresh_set", NULL);
  api_def_prop_ui_text(prop,
                       "Linear Velocity Deactivation Threshold",
                       "Linear Velocity below which simulation stops simulating object");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(dapi, "deactivate_angular_velocity", PROP_FLOAT, PROP_UNIT_VELOCITY);
  api_def_prop_float_stype(prop, NULL, "ang_sleep_thresh");
  api_def_prop_range(
      prop, FLT_MIN, FLT_MAX); /* range must always be positive (and non-zero) */
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyOb_angular_sleepThresh_set", NULL);
  api_def_prop_ui_text(prop,
                       "Angular Velocity Deactivation Threshold",
                       "Angular Velocity below which simulation stops simulating object");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  /* Dynamics Parameters - Damping Parameters */
  prop = api_def_prop(sapi, "linear_damping", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "lin_damping");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_float_default(prop, 0.04f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyOb_linear_damping_set", NULL);
  api_def_prop_ui_text(
      prop, "Linear Damping", "Amount of linear velocity that is lost over time");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "angular_damping", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "ang_damping");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_float_default(prop, 0.1f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyOb_angular_damping_set", NULL);
  api_def_prop_ui_text(
      prop, "Angular Damping", "Amount of angular velocity that is lost over time");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "rna_RigidBodyOb_reset");

  /* Collision Parameters - Surface Parameters */
  prop = api_def_prop(sapi, "friction", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "friction");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 1, 3);
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyOb_friction_set", NULL);
  api_def_prop_ui_text(prop, "Friction", "Resistance of object to movement");
  apia_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "restitution", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "restitution");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 1, 3);
  api_def_prop_float_default(prop, 0.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyOb_restitution_set", NULL);
  api_def_prop_ui_text(prop,
                       "Restitution",
                       "Tendency of object to bounce after colliding with another "
                       "(0 = stays still, 1 = perfectly elastic)");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  /* Collision Parameters - Sensitivity */
  prop = api_def_prop(sapi, "use_margin", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBO_FLAG_USE_MARGIN);
  api_def_prop_bool_default(prop, false);
  api_def_prop_ui_text(
      prop,
      "Collision Margin",
      "Use custom collision margin (some shapes will have a visible gap around them)");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "rna_RigidBodyOb_shape_reset");

  prop = api_def_prop(sapi, "collision_margin", PROP_FLOAT, PROP_UNIT_LENGTH);
  api_def_prop_float_stype(prop, NULL, "margin");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.01, 3);
  api_def_prop_float_default(prop, 0.04f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyOb_collision_margin_set", NULL);
  api_def_prop_ui_text(
      prop,
      "Collision Margin",
      "Threshold of distance near surface where collisions are still considered "
      "(best results when non-zero)");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "rna_RigidBodyOb_shape_reset");

  prop = api_def_prop(sapi, "collision_collections", PROP_BOOL, PROP_LAYER_MEMBER);
  api_def_prop_bool_stype(prop, NULL, "col_groups", 1);
  api_def_prop_array(prop, 20);
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyOb_collision_collections_set");
  api_def_prop_ui_text(
      prop, "Collision Collections", "Collision collections rigid body belongs to");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");
  api_def_prop_flag(prop, PROP_LIB_EXCEPTION);
}

static void api_def_rigidbody_constraint(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "RigidBodyConstraint", NULL);
  api_def_struct_stype(sapi, "RigidBodyCon");
  api_def_struct_ui_text(sapi,
                         "Rigid Body Constraint",
                         "Constraint influencing Objects inside Rigid Body Simulation");
  api_def_struct_path_fn(sapi, "api_RigidBodyCon_path");

  /* Enums */
  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "type");
  api_def_prop_enum_items(prop, api_enum_rigidbody_constraint_type_items);
  api_def_prop_enum_fns(prop, NULL, "api_RigidBodyCon_type_set", NULL);
  api_def_prop_ui_text(prop, "Type", "Type of Rigid Body Constraint");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prope_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "spring_type");
  api_def_prop_enum_items(prop, api_enum_rigidbody_constraint_spring_type_items);
  api_def_prop_enum_fns(prop, NULL, "api_RigidBodyCon_spring_type_set", NULL);
  api_def_prop_ui_text(prop, "Spring Type", "Which implementation of spring to use");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "enabled", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_ENABL
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyCon_enabled_set");
  api_def_prop_ui_text(prop, "Enabled", "Enable this constraint");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "disable_collisions", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_DISABLE_COLLISIONS);
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyCon_disable_collisions_set");
  api_def_prop_ui_text(
      prop, "Disable Collisions", "Disable collisions between constrained rigid bodies");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "object1", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "ob1");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Object 1", "First Rigid Body Object to be constrained");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "object2", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "ob2");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Object 2", "Second Rigid Body Object to be constrained");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  /* Breaking Threshold */
  prop = api_def_prop(sapi, "use_breaking", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_BREAKING);
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyCon_use_breaking_set");
  api_def_prop_ui_text(
      prop, "Breakable", "Constraint can be broken if it receives an impulse above the threshold");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "breaking_threshold", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stypes(prop, NULL, "breaking_threshold");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 1000.0f, 100.0, 2);
  api_def_prop_float_default(prop, 10.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_breaking_threshold_set", NULL);
  api_def_prop_ui_text(prop,
                       "Breaking Threshold",
                       "Impulse threshold that must be reached for the constraint to break");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  /* Solver Iterations */
  prop = api_def_prop(sapi, "use_override_solver_iters", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_OVERRIDE_SOLVER_ITERS);
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyCon_override_solver_iters_set");
  api_def_prop_ui_text(prop,
                       "Override Solver Iterations",
                       "Override the number of solver iterations for this constraint");
  api_def_prop_update(prop, NC_OBJECT | ND_POINTCACHE, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "solver_iters", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "num_solver_iters");
  api_def_prop_range(prop, 1, 1000);
  api_def_prop_ui_range(prop, 1, 100, 1, -1);
  api_def_prop_int_default(prop, 10);
  api_def_prop_int_fns(prop, NULL, "api_RigidBodyCon_num_solver_iters_set", NULL);
  api_def_prop_ui_text(
      prop,
      "Solver Iterations",
      "Number of constraint solver iterations made per simulation step (higher values are more "
      "accurate but slower)");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset")
  /* Limits */
  prop = api_def_prop(sapi, "use_limit_lin_x", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_LIMIT_LIN_X);
  api_def_prop_ui_text(prop, "X Axis", "Limit translation on X axis");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_limit_lin_y", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_LIMIT_LIN_Y);
  api_def_prop_ui_text(prop, "Y Axis", "Limit translation on Y axis");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_limit_lin_z", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_LIMIT_LIN_Z);
  api_def_prop_ui_text(prop, "Z Axis", "Limit translation on Z axis");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_limit_ang_x", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_LIMIT_ANG_X);
  api_def_prop_ui_text(prop, "X Angle", "Limit rotation around X axis");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_limit_ang_y", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_LIMIT_ANG_Y);
  api_def_prop_ui_text(prop, "Y Angle", "Limit rotation around Y axis");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_limit_ang_z", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_LIMIT_ANG_Z);
  api_def_prop_ui_text(prop, "Z Angle", "Limit rotation around Z axis");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_spring_x", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_SPRING_X);
  api_def_prop_ui_text(prop, "X Spring", "Enable spring on X axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_spring_y", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_SPRING_Y);
  api_def_prop_ui_text(prop, "Y Spring", "Enable spring on Y axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_spring_z", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_SPRING_Z)
  api_def_prop_ui_text(prop, "Z Spring", "Enable spring on Z axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_spring_ang_x", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_SPRING_ANG_X);
  api_def_prop_ui_text(prop, "X Angle Spring", "Enable spring on X rotational axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_spring_ang_y", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_type(prop, NULL, "flag", RBC_FLAG_USE_SPRING_ANG_Y);
  api_def_prop_ui_text(prop, "Y Angle Spring", "Enable spring on Y rotational axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_spring_ang_z", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_SPRING_ANG_Z);
  api_def_prop_ui_text(prop, "Z Angle Spring", "Enable spring on Z rotational axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_motor_lin", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_MOTOR_LIN);
  apu_def_prop_bool_fns(prop, NULL, "api_RigidBodyCon_use_motor_lin_set");
  api_def_prop_ui_text(prop, "Linear Motor", "Enable linear motor");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "use_motor_ang", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", RBC_FLAG_USE_MOTOR_ANG);
  api_def_prop_bool_fns(prop, NULL, "api_RigidBodyCon_use_motor_ang_set");
  api_def_prop_ui_text(prop, "Angular Motor", "Enable angular motor");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_lin_x_lower", PROP_FLOAT, PROP_UNIT_LENGTH);
  api_def_prop_float_stype(prop, NULL, "limit_lin_x_lower");
  api_def_prop_float_default(prop, -1.0f);
  api_def_prop_ui_text(prop, "Lower X Limit", "Lower limit of X axis translation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_lin_x_upper", PROP_FLOAT, PROP_UNIT_LENGTH)
  api_def_prop_float_stype(prop, NULL, "limit_lin_x_upper");
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_text(prop, "Upper X Limit", "Upper limit of X axis translation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_lin_y_lower", PROP_FLOAT, PROP_UNIT_LENGTH);
  api_def_prop_float_stype(prop, NULL, "limit_lin_y_lower");
  api_def_prop_float_default(prop, -1.0f);
  api_def_prop_ui_text(prop, "Lower Y Limit", "Lower limit of Y axis translation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_lin_y_upper", PROP_FLOAT, PROP_UNIT_LENGTH);
  api_def_prop_float_stype(prop, NULL, "limit_lin_y_upper");
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_text(prop, "Upper Y Limit", "Upper limit of Y axis translation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_lin_z_lower", PROP_FLOAT, PROP_UNIT_LENGTH);
  api_def_prop_float_stype(prop, NULL, "limit_lin_z_lower");
  api_def_prop_float_default(prop, -1.0f);
  api_def_prop_ui_text(prop, "Lower Z Limit", "Lower limit of Z axis translation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_lin_z_upper", PROP_FLOAT, PROP_UNIT_LENGTH);
  api_def_prop_float_stype(prop, NULL, "limit_lin_z_upper");
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_text(prop, "Upper Z Limit", "Upper limit of Z axis translation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_ang_x_lower", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limit_ang_x_lower");
  api_def_prop_range(prop, -M_PI * 2, M_PI * 2);
  api_def_prop_float_default(prop, -M_PI_4);
  api_def_prop_ui_text(prop, "Lower X Angle Limit", "Lower limit of X axis rotation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_ang_x_upper", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limit_ang_x_upper");
  api_def_prop_range(prop, -M_PI * 2, M_PI * 2);
  api_def_prop_float_default(prop, M_PI_4);
  api_def_prop_ui_text(prop, "Upper X Angle Limit", "Upper limit of X axis rotation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_ang_y_lower", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limit_ang_y_lower");
  api_def_prop_range(prop, -M_PI * 2, M_PI * 2);
  api_def_prop_float_default(prop, -M_PI_4);
  api_def_prop_ui_text(prop, "Lower Y Angle Limit", "Lower limit of Y axis rotation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_ang_y_upper", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limit_ang_y_upper");
  api_def_prop_range(prop, -M_PI * 2, M_PI * 2);
  api_def_prop_float_default(prop, M_PI_4);
  api_def_prop_ui_text(prop, "Upper Y Angle Limit", "Upper limit of Y axis rotation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_ang_z_lower", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limit_ang_z_lower");
  api_def_prop_range(prop, -M_PI * 2, M_PI * 2);
  api_def_prop_float_default(prop, -M_PI_4);
  api_def_prop_ui_text(prop, "Lower Z Angle Limit", "Lower limit of Z axis rotation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "limit_ang_z_upper", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limit_ang_z_upper");
  api_def_prop_range(prop, -M_PI * 2, M_PI * 2);
  api_def_prop_float_default(prop, M_PI_4)
  api_def_prop_ui_text(prop, "Upper Z Angle Limit", "Upper limit of Z axis rotation");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_stiffness_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_stiffness_x");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 10.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_stiffness_x_set", NULL);
  api_def_prop_ui_text(prop, "X Axis Stiffness", "Stiffness on the X axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_stiffness_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_stiffness_y");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 10.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_stiffness_y_set", NULL);
  api_def_prop_ui_text(prop, "Y Axis Stiffness", "Stiffness on the Y axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_stiffness_z", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_stiffness_z");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 10.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_stiffness_z_set", NULL);
  api_def_prop_ui_text(prop, "Z Axis Stiffness", "Stiffness on the Z axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_stiffness_ang_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_stiffness_ang_x");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 10.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_stiffness_ang_x_set", NULL);
  api_def_prop_ui_text(prop, "X Angle Stiffness", "Stiffness on the X rotational axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_stiffness_ang_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_stiffness_ang_y");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 10.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_stiffness_ang_y_set", NULL);
  api_def_prop_ui_text(prop, "Y Angle Stiffness", "Stiffness on the Y rotational axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_stiffness_ang_z", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_stiffness_ang_z");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 10.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_stiffness_ang_z_set", NULL);
  api_def_prop_ui_text(prop, "Z Angle Stiffness", "Stiffness on the Z rotational axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_damping_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_damping_x");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_damping_x_set", NULL);
  api_def_prop_ui_text(prop, "Damping X", "Damping on the X axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_damping_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_damping_y");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_damping_y_set", NULL);
  api_def_prop_ui_text(prop, "Damping Y", "Damping on the Y axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_damping_z", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_damping_z");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_float_default(prop, 0.5f
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_damping_z_set", NULL);
  api_def_prop_ui_text(prop, "Damping Z", "Damping on the Z axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_damping_ang_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_damping_ang_x");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_damping_ang_x_set", NULL);
  api_def_prop_ui_text(prop, "Damping X Angle", "Damping on the X rotational axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_damping_ang_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_damping_ang_y");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_damping_ang_y_set", NULL);
  api_def_prop_ui_text(prop, "Damping Y Angle", "Damping on the Y rotational axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "spring_damping_ang_z", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "spring_damping_ang_z
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_spring_damping_ang_z_set", NULL);
  api_def_prop_ui_text(prop, "Damping Z Angle", "Damping on the Z rotational axis");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "motor_lin_target_velocity", PROP_FLOAT, PROP_UNIT_VELOCITY);
  api_def_prop_float_stype(prop, NULL, "motor_lin_target_velocity");
  api_def_prop_range(prop, -FLT_MAX, FLT_MAX);
  api_def_prop_ui_range(prop, -100.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_motor_lin_target_velocity_set", NULL);
  api_def_prop_ui_text(prop, "Target Velocity", "Target linear motor velocity");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_props(sapi, "motor_lin_max_impulse", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "motor_lin_max_impulse");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_motor_lin_max_impulse_set", NULL);
  api_def_prop_ui_text(prop, "Max Impulse", "Maximum linear motor impulse");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "motor_ang_target_velocity", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_sttpe(prop, NULL, "motor_ang_target_velocity");
  api_def_prop_range(prop, -FLT_MAX, FLT_MAX);
  api_def_prop_ui_range(prop, -100.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_motor_ang_target_velocity_set", NULL);
  api_def_prop_ui_text(prop, "Target Velocity", "Target angular motor velocity");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");

  prop = api_def_prop(sapi, "motor_ang_max_impulse", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "motor_ang_max_impulse");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 100.0f, 1, 3);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_float_fns(prop, NULL, "api_RigidBodyCon_motor_ang_max_impulse_set", NULL);
  api_def_prop_ui_text(prop, "Max Impulse", "Maximum angular motor impulse");
  api_def_prop_update(prop, NC_OBJECT, "api_RigidBodyOb_reset");
}

void api_def_rigidbody(DuneApi *dapi)
{
  api_def_rigidbody_world(dapi);
  api_def_rigidbody_object(dapi);
  api_def_rigidbody_constraint(dapi);
}

#endif
