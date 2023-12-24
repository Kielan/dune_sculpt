/* Types and defines for representing Rigid Body entities */

#pragma once

#include "types_list.h"
#include "types_ob_force.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Collection;

struct EffectorWeights;

/* RigidBody World */
/* Container for data shared by original and evald copies of RigidBodyWorld. */
typedef struct RigidBodyWorldShared {
  /* cache */
  struct PointCache *pointcache;
  struct List ptcaches;

  /* Refs to Physics Sim obs. Exist at runtime only ---------------------- */
  /* Phys sim world (i.e. tDiscreteDynamicsWorld). */
  void *phys_world;
} RigidBodyWorldShared;

/* RigidBodyWorld (rbw)
 * Represents a "simulation scene" existing within the parent scene. */
typedef struct RigidBodyWorld {
  /* Sim World Settings */
  /* Effectors info. */
  struct EffectorWeights *effector_weights;

  /* Group containing obs to use for Rigid Bodies. */
  struct Collection *group;
  /* Array to access group obs by index, only used at runtime. */
  struct Ob **obs;

  /* Group containing objects to use for Rigid Body Constraints. */
  struct Collection *constraints;

  char _pad[4];
  /* Last frame world was evaluated for (internal). */
  float ltime;

  /** This pointer is shared between all evaluated copies. */
  struct RigidBodyWorld_Shared *shared;
  /* Moved to `shared->pointcache`. */
  struct PointCache *pointcache TYPES_DEPRECATED;
  /* Moved to `shared->ptcaches`. */
  struct List ptcaches TYPES_DEPRECATED;
  /* Num of obs in rigid body group. */
  int numbodies;

  /* Num of simulation sub-steps steps taken per frame. */
  short substeps_per_frame;
  /* Num of constraint solver iterations made per simulation step. */
  short num_solver_iters;

  /* (eRigidBodyWorld_Flag) settings for this RigidBodyWorld. */
  int flag;
  /* Used to speed up or slow down the simulation. */
  float time_scale;
} RigidBodyWorld;

/* RigidBodyWorld.flag */
typedef enum eRigidBodyWorldFlag {
  /* should sim world be skipped when evaluating (user setting) */
  RBW_FLAG_MUTED = (1 << 0),
  /* sim data needs to be rebuilt */
  /* RBW_FLAG_NEEDS_REBUILD = (1 << 1), */ /* UNUSED */
  /* Use split impulse when stepping the sim. */
  RBW_FLAG_USE_SPLIT_IMPULSE = (1 << 2),
} eRigidBodyWorld_Flag;

/* RigidBody Ob */
/* Container for data that is shared among CoW copies.
 * This is placed in a separate struct so that, for example, the phys_shape
 * pointer can be replaced without having to update all CoW copies. */
#
#
typedef struct RigidBodyObShared {
  /* Refs to Phys Sim obs. Exist at runtime only */
  /* Phys ob representation (i.e. tRigidBody). */
  void *phys_ob;
  /* Collision shape used by phys sim (i.e. tCollisionShape). */
  void *phys_shape;
} RigidBodyObShared;

/* RigidBodyOb (rbo)
 * Represents an ob participating in a RigidBody sim.
 * This is attached to each ob that is currently
 * participating in a sim. */
typedef struct RigidBodyOb {
  /* General Settings for this RigidBodyOb */
  /* (eRigidBodyObType) role of RigidBody in sim. */
  short type;
  /* (eRigidBodyShape) collision shape to use. */
  short shape;

  /* (eRigidBodyObFlag). */
  int flag;
  /* Collision groups that determines which rigid bodies can collide with each other. */
  int col_groups;
  /* (eRigidBodyMeshSrc) mesh src for mesh based collision shapes. */
  short mesh_src;
  char _pad[2];

  /* Phys Params */
  /* How much ob 'weighs' (i.e. absolute 'amount of stuff' it holds). */
  float mass;

  /* Resistance of ob to movement. */
  float friction;
  /* How 'bouncy' ob is when it collides. */
  float restitution;

  /* Tolerance for detecting collisions. */
  float margin;

  /* Damping for linear velocities. */
  float lin_damping;
  /* Damping for angular velocities. */
  float ang_damping;

  /* Deactivation threshold for linear velocities. */
  float lin_sleep_thresh;
  /* Deactivation threshold for angular velocities. */
  float ang_sleep_thresh;

  /* Rigid body orientation. */
  float orn[4];
  /* Rigid body position. */
  float pos[3];
  char _pad1[4];

  /* This pointer is shared between all eval copies. */
  struct RigidBodyObShared *shared;
} RigidBodyOb;

/* RigidBodyOb.type */
typedef enum eRigidBodyObType {
  /* active geometry participant in sim. is directly controlled by sim */
  RBO_TYPE_ACTIVE = 0,
  /* passive geometry participant in sim. is directly controlled by animsys */
  RBO_TYPE_PASSIVE = 1,
} eRigidBodyObType;

/* RigidBodyOb.flag */
typedef enum eRigidBodyObFlag {
  /* rigidbody is kinematic (controlled by the animation system) */
  RBO_FLAG_KINEMATIC = (1 << 0),
  /* rigidbody needs to be validated (usually set after duplicating and not hooked up yet) */
  RBO_FLAG_NEEDS_VALIDATE = (1 << 1),
  /* rigidbody shape needs refreshing (usually after exiting editmode) */
  RBO_FLAG_NEEDS_RESHAPE = (1 << 2),
  /* rigidbody can be deactivated */
  RBO_FLAG_USE_DEACTIVATION = (1 << 3),
  /* rigidbody is deactivated at the beginning of sim */
  RBO_FLAG_START_DEACTIVATED = (1 << 4),
  /* rigidbody is not dynamically sim */
  RBO_FLAG_DISABLED = (1 << 5),
  /* collision margin is not embedded (only used by convex hull shapes for now) */
  RBO_FLAG_USE_MARGIN = (1 << 6),
  /* collision shape deforms during sim (only for passive triangle mesh shapes) */
  RBO_FLAG_USE_DEFORM = (1 << 7),
} eRigidBodyObFlag;

/* Rigid Body Collision Shape. */
typedef enum eRigidBodyShape {
  /* Simple box (i.e. bounding box). */
  RB_SHAPE_BOX = 0,
  /* Sphere. */
  RB_SHAPE_SPHERE = 1,
  /* Rounded "pill" shape (i.e. calcium tablets). */
  RB_SHAPE_CAPSULE = 2,
  /* Cylinder (i.e. tin of beans). */
  RB_SHAPE_CYLINDER = 3,
  /* Cone (i.e. party hat). */
  RB_SHAPE_CONE = 4,

  /* Convex hull (minimal shrink-wrap encompassing all verts). */
  RB_SHAPE_CONVEXH = 5,
  /* Triangulated mesh. */
  RB_SHAPE_TRIMESH = 6,

  /* concave mesh approximated using primitives */
  RB_SHAPE_COMPOUND = 7,
} eRigidBodyShape;

typedef enum eRigidBodyMeshSrc {
  /* base mesh */
  RBO_MESH_BASE = 0,
  /* only deformations */
  RBO_MESH_DEFORM = 1,
  /* final derived mesh */
  RBO_MESH_FINAL = 2,
} eRigidBodyMeshSrc;

/* RigidBody Constraint */
/* RigidBodyConstraint (rbc)
 * Represents an constraint connecting two rigid bodies. */
typedef struct RigidBodyCon {
  /* First ob influenced by the constraint. */
  struct Ob *ob1;
  /* Second ob influenced by the constraint. */
  struct Ob *ob2;

  /* General Settings for this RigidBodyCon */
  /* (eRigidBodyConType) role of RigidBody in sim. */
  short type;
  /* Num of constraint solver iters made per sim step. */
  short num_solver_iters;

  /* (eRigidBodyCon_Flag). */
  int flag;

  /* Breaking impulse threshold. */
  float breaking_threshold;
  /* Spring implementation to use. */
  char spring_type;
  char _pad[3];

  /* limits */
  /* translation limits */
  float limit_lin_x_lower;
  float limit_lin_x_upper;
  float limit_lin_y_lower;
  float limit_lin_y_upper;
  float limit_lin_z_lower;
  float limit_lin_z_upper;
  /* rotation limits */
  float limit_ang_x_lower;
  float limit_ang_x_upper;
  float limit_ang_y_lower;
  float limit_ang_y_upper;
  float limit_ang_z_lower;
  float limit_ang_z_upper;

  /* spring settings */
  /* resistance to deformation */
  float spring_stiffness_x;
  float spring_stiffness_y;
  float spring_stiffness_z;
  float spring_stiffness_ang_x;
  float spring_stiffness_ang_y;
  float spring_stiffness_ang_z;
  /* amount of velocity lost over time */
  float spring_damping_x;
  float spring_damping_y;
  float spring_damping_z;
  float spring_damping_ang_x;
  float spring_damping_ang_y;
  float spring_damping_ang_z;

  /* motor settings */
  /* Linear velocity the motor tries to hold. */
  float motor_lin_target_velocity;
  /* Angular velocity the motor tries to hold. */
  float motor_ang_target_velocity;
  /* Max force used to reach linear target velocity. */
  float motor_lin_max_impulse;
  /* Max force used to reach angular target velocity. */
  float motor_ang_max_impulse;

  /* Refs to Phys Sim ob. Exist at runtime only */
  /* Phys ob representation (i.e. tTypedConstraint). */
  void *phys_constraint;
} RigidBodyCon;

/* Participation types for RigidBodyOb.type */
typedef enum eRigidBodyConType {
  /* lets bodies rotate around a specified point */
  RBC_TYPE_POINT = 0,
  /* lets bodies rotate around a specified axis */
  RBC_TYPE_HINGE = 1,
  /* simulates wheel suspension */
  /* RBC_TYPE_HINGE2 = 2, */ /* UNUSED */
  /* Restricts moment to a specified axis. */
  RBC_TYPE_SLIDER = 3,
  /* lets ob rotate within a specified cone */
  /* RBC_TYPE_CONE_TWIST = 4, */ /* UNUSED */
  /* allows user to specify constraint axes */
  RBC_TYPE_6DOF = 5,
  /* like 6DOF but has springs */
  RBC_TYPE_6DOF_SPRING = 6,
  /* simulates a universal joint */
  /* RBC_TYPE_UNIVERSAL = 7, */ /* UNUSED */
  /* glues two bodies together */
  RBC_TYPE_FIXED = 8,
  /* similar to slider but also allows rotation around slider axis */
  RBC_TYPE_PISTON = 9,
  /* Simplified spring constraint with only once axis that's
   * automatically placed between the connected bodies */
  /* RBC_TYPE_SPRING = 10, */ /* UNUSED */
  /* Drives bodies by applying linear and angular forces. */
  RBC_TYPE_MOTOR = 11,
} eRigidBodyConType;

/* Spring implementation type for RigidBodyOb. */
typedef enum eRigidBodyConSpringType {
  RBC_SPRING_TYPE1 = 0, /* btGeneric6DofSpringConstraint */
  RBC_SPRING_TYPE2 = 1, /* btGeneric6DofSpring2Constraint */
} eRigidBodyConSpringType;

/* RigidBodyCon.flag */
typedef enum eRigidBodyConFlag {
  /* constraint influences rigid body motion */
  RBC_FLAG_ENABLED = (1 << 0),
  /* constraint needs to be validated */
  RBC_FLAG_NEEDS_VALIDATE = (1 << 1),
  /* allow constrained bodies to collide */
  RBC_FLAG_DISABLE_COLLISIONS = (1 << 2),
  /* constraint can break */
  RBC_FLAG_USE_BREAKING = (1 << 3),
  /* constraint use custom number of constraint solver iterations */
  RBC_FLAG_OVERRIDE_SOLVER_ITERS = (1 << 4),
  /* limits */
  RBC_FLAG_USE_LIMIT_LIN_X = (1 << 5),
  RBC_FLAG_USE_LIMIT_LIN_Y = (1 << 6),
  RBC_FLAG_USE_LIMIT_LIN_Z = (1 << 7),
  RBC_FLAG_USE_LIMIT_ANG_X = (1 << 8),
  RBC_FLAG_USE_LIMIT_ANG_Y = (1 << 9),
  RBC_FLAG_USE_LIMIT_ANG_Z = (1 << 10),
  /* springs */
  RBC_FLAG_USE_SPRING_X = (1 << 11),
  RBC_FLAG_USE_SPRING_Y = (1 << 12),
  RBC_FLAG_USE_SPRING_Z = (1 << 13),
  /* motors */
  RBC_FLAG_USE_MOTOR_LIN = (1 << 14),
  RBC_FLAG_USE_MOTOR_ANG = (1 << 15),
  /* angular springs */
  RBC_FLAG_USE_SPRING_ANG_X = (1 << 16),
  RBC_FLAG_USE_SPRING_ANG_Y = (1 << 17),
  RBC_FLAG_USE_SPRING_ANG_Z = (1 << 18),
} eRigidBodyConFlag;


#ifdef __cplusplus
}
#endif
