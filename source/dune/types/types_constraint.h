#pragma once

#include "types_id.h"
#include "types_defs.h"
#include "types_list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Action;
struct Ipo;
struct Txt;

/* channels reside in Ob or Action (List) constraintChannels */
/* Deprecated. old AnimSys. */
typedef struct ConstraintChannel {
  struct ConstraintChannel *next, *prev;
  struct Ipo *ipo;
  short flag;
  char name[30];
} ConstraintChannel;

/* A Constraint */
typedef struct Constraint {
  struct Constraint *next, *prev;

  /* Constraint data (a valid constraint type). */
  void *data;
  /* Constraint type. */
  short type;
  /* Flag - General Settings. */
  short flag;

  /* Space that owner should be eval in. */
  char ownspace;
  /* Space that target should be eval in (only used if 1 target). */
  char tarspace;

  /* An "expand" bit for each of the constraint's (sub)pnls (uiPanelDataExpansion). */
  short ui_expand_flag;

  /* Ob to use as target for Custom Space of owner. */
  struct Ob *space_ob;
  /* Subtarget for Custom Space of owner - pchan or vgroup name, MAX_ID_NAME-2. */
  char space_subtarget[64];

  /* Constraint name, MAX_NAME. */
  char name[64];

  /* Amount of influence exerted by constraint (0.0-1.0). */
  float enforce;
  /* Point along `subtarget` bone where the actual target is. 0=head (default for all), 1=tail. */
  float headtail;

  /* old anim sys, deprecated for 2.5. */
  /* Local influence ipo or driver */
  struct Ipo *ipo TYPES_DEPRECATED;

  /* Below are read-only fields that are set at runtime
   * by the solver for use in the GE (only IK at the moment). */
  /* Residual error on constraint expressed in dune unit. */
  float lin_error;
  /* Residual error on constraint expressed in radiant. */
  float rot_error;
} Constraint;

/* Multiple-target constraints */
/* This struct defines a constraint target.
 * It is used during constraint solving regardless of how many targets the
 * constraint has. */
typedef struct ConstraintTarget {
  struct ConstraintTarget *next, *prev;

  /* Ob to use as target. */
  struct Ob *tar;
  /* Subtarget - pchan or vgroup name, MAX_ID_NAME-2. */
  char subtarget[64];

  /* Matrix used during constraint solving - should be cleared before each use. */
  float matrix[4][4];

  /* Space that target should be eval in (overrides Constraint->tarspace). */
  short space;
  /* Runtime settings (for editor, etc.). */
  short flag;
  /* Type of target (eConstraintObType). */
  short type;
  /* Rotation order for target (as defined in BLI_math.h). */
  short rotOrder;
  /* Weight for armature deform. */
  float weight;
  char _pad[4];
} ConstraintTarget;

/* ConstraintTarget -> flag */
typedef enum eConstraintTargetFlag {
  /* tmp target-struct that needs to be freed after use */
  CONSTRAINT_TAR_TMP = (1 << 0),
} eConstraintTargetFlag;

/* bConstraintTarget/bConstraintOb -> type */
typedef enum eConstraintObType {
  /* string is "" */
  CONSTRAINT_OBTYPE_OB = 1,
  /* string is bone-name */
  CONSTRAINT_OBTYPE_BONE = 2,
  /* string is vertex-group name */
  CONSTRAINT_OBTYPE_VERT = 3,
  /* string is vertex-group name - is not available until curves get vgroups */
  /* CONSTRAINT_OBTYPE_CV = 4, */ /* UNUSED */
} eConstraintObType;

/* Python Script Constraint */
typedef struct PythonConstraint {
  /* Text-buf (containing script) to execute. */
  struct Txt *txt;
  /* 'id-props' used to store custom properties for constraint. */
  IdProp *prop;

  /* General settings/state indicators accessed by bitmapping. */
  int flag;
  /* Num of targets - usually only 1-3 are needed. */
  int tarnum;

  /* A list of targets that this constraint has (bConstraintTarget-s). */
  List targets;

  /* Target from prev implementation
   * (version-patch sets this to NULL on file-load). */
  struct Ob *tar;
  /* Subtarget from prev implementation
   * (version-patch sets this to "" on file-load), MAX_ID_NAME-2. */
  char subtarget[64];
} PythonConstraint;

/* Inverse-Kinematics (IK) constraint
 * This constraint supports a variety of mode determine by the type field
 * according to eConstraint_IK_Type.
 * Some fields are used by all types, some are specific to some types
 * This is indicated in the comments for each field */
typedef struct KinematicConstraint {
  /* All: target object in case constraint needs a target. */
  struct Ob *tar;
  /* All: Max num of iterations to try. */
  short iters;
  /* All & CopyPose: some options Like CONSTRAINT_IK_TIP. */
  short flag;
  /* All: index to rootbone, if zero go all the way to mother bone. */
  short rootbone;
  /* CopyPose: for auto-ik, maximum length of chain. */
  short max_rootbone;
  /* All: String to specify sub-object target, MAX_ID_NAME-2. */
  char subtarget[64];
  /* All: Pole vector target. */
  struct Ob *poletar;
  /* All: Pole vector sub-object target, MAX_ID_NAME-2. */
  char polesubtarget[64];
  /* All: Pole vector rest angle. */
  float poleangle;
  /* All: Weight of constraint in IK tree. */
  float weight;
  /* CopyPose: Amount of rotation a target applies on chain. */
  float orientweight;
  /* CopyPose: for target-less IK. */
  float grabtarget[3];
  /* Subtype of IK constraint: eConstraint_IK_Type. */
  short type;
  /* Distance: how to limit in relation to clamping sphere: LIMITDIST_... */
  short mode;
  /* Distance: distance (radius of clamping sphere) from target. */
  float dist;
} KinematicConstraint;

typedef enum eConstraint_IK_Type {
  /* 'standard' IK constraint: match position and/or orientation of target */
  CONSTRAINT_IK_COPYPOSE = 0,
  /* maintain distance with target */
  CONSTRAINT_IK_DISTANCE = 1,
} eConstraint_IK_Type;

/* Spline IK Constraint
 * Aligns 'n' bones to the curvature defined by the curve,
 * with the chain ending on the bone that owns this constraint,
 * and starting on the nth parent. */
typedef struct SplineIKConstraint {
  /* target(s) */
  /* Curve ob (with follow path enabled) which drives the bone chain. */
  struct Ob *tar;

  /* binding details */
  /* Array of numpoints items,
   * denoting parametric positions along curve that joints should follow. */
  float *points;
  /* Num of points to bound in points array. */
  short numpoints;
  /* Num of bones ('n') that are in the chain. */
  short chainlen;

  /* settings */
  /* General settings for constraint. */
  short flag;
  /* Method used for determining the x & z scaling of the bones. */
  short xzScaleMode;
  /* Method used for determining the y scaling of the bones. */
  short yScaleMode;
  short _pad[3];

  /* volume preservation settings */
  float bulge;
  float bulge_min;
  float bulge_max;
  float bulge_smooth;
} SplineIKConstraint;

/* Armature Constraint */
typedef struct ArmatureConstraint {
  /* General settings/state indicators accessed by bitmapping. */
  int flag;
  char _pad[4];

  /* A list of targets that this constraint has (ConstraintTarget-s). */
  List targets;
} ArmatureConstraint;

/* Single-target subobject constraints */

/* Track To Constraint */
typedef struct TrackToConstraint {
  struct Ob *tar;
  /* I'll be using reserved1 and reserved2 as Track and Up flags,
   * not sure if that's what they were intended for anyway.
   * Not sure either if it would create backward incompatibility if I were to rename them */
  int reserved1;
  int reserved2;
  int flags;
  char _pad[4];
  /* MAX_ID_NAME-2. */
  char subtarget[64];
} TrackToConstraint;

/* Copy Rotation Constraint */
typedef struct RotateLikeConstraint {
  struct Ob *tar;
  int flag;
  char euler_order;
  char mix_mode;
  char _pad[2];
  /* MAX_ID_NAME-2. */
  char subtarget[64];
} RotateLikeConstraint;

/* Copy Location Constraint */
typedef struct LocateLikeConstraint {
  struct Ob *tar;
  int flag;
  int reserved1;
  /* MAX_ID_NAME-2. */
  char subtarget[64];
} LocateLikeConstraint;

/* Copy Scale Constraint */
typedef struct SizeLikeConstraint {
  struct Ob *tar;
  int flag;
  float power;
  /* MAX_ID_NAME-2. */
  char subtarget[64];
} SizeLikeConstraint;

/* Maintain Volume Constraint */
typedef struct SameVolumeConstraint {
  char free_axis;
  char mode;
  char _pad[2];
  float volume;
} SameVolumeConstraint;

/* Copy Transform Constraint */
typedef struct TransLikeConstraint {
  struct Ob *tar;
  int flag;
  char mix_mode;
  char _pad[3];
  /* MAX_ID_NAME-2. */
  char subtarget[64];
} TransLikeConstraint;

/* Floor Constraint */
typedef struct MinMaxConstraint {
  struct Ob *tar;
  int minmaxflag;
  float offset;
  int flag;
  /* MAX_ID_NAME-2. */
  char subtarget[64];
  int _pad;
} MinMaxConstraint;

/* Action Constraint */
typedef struct ActionConstraint {
  struct Ob *tar;
  /* What transform 'channel' drives the result. */
  short type;
  /* Was used in versions prior to the Constraints recode. */
  short local;
  int start;
  int end;
  float min;
  float max;
  int flag;
  char mix_mode;
  char _pad[3];
  float eval_time; /* Only used when flag ACTCON_USE_EVAL_TIME is set. */
  struct Action *act;
  /* MAX_ID_NAME-2. */
  char subtarget[64];
} ActionConstraint;

/* Locked Axis Tracking constraint */
typedef struct LockTrackConstraint {
  struct Ob *tar;
  int trackflag;
  int lockflag;
  /* MAX_ID_NAME-2. */
  char subtarget[64];
} LockTrackConstraint;

/* Damped Tracking constraint */
typedef struct DampTrackConstraint {
  struct Ob *tar;
  int trackflag;
  char _pad[4];
  /* MAX_ID_NAME-2. */
  char subtarget[64];
} DampTrackConstraint;

/* Follow Path constraints */
typedef struct FollowPathConstraint {
  /* Must be path ob. */
  struct Ob *tar;

  /* Offset in time on the path (in frames), when NOT using 'fixed position'. */
  float offset;
  /* Parametric offset factor defining position along path, when using 'fixed position'. */
  float offset_fac;

  int followflag;

  short trackflag;
  short upflag;
} FollowPathConstraint;

/* Stretch to constraint */
typedef struct StretchToConstraint {
  struct Ob *tar;
  int flag;
  int volmode;
  int plane;
  float orglength;
  float bulge;
  float bulge_min;
  float bulge_max;
  float bulge_smooth;
  /* MAX_ID_NAME-2. */
  char subtarget[64];
} StretchToConstraint;

/* Rigid Body constraint */
typedef struct RigidBodyJointConstraint {
  struct Ob *tar;
  struct Ob *child;
  int type;
  float pivX;
  float pivY;
  float pivZ;
  float axX;
  float axY;
  float axZ;
  float minLimit[6];
  float maxLimit[6];
  float extraFz;
  short flag;
  char _pad[6];
} RigidBodyJointConstraint;

/* Clamp-To Constraint */
typedef struct ClampToConstraint {
  /* 'target' must be a curve. */
  struct Ob *tar;
  /* Which axis/plane to compare owner's location on. */
  int flag;
  /* For legacy reasons, this is flag2. used for any extra settings. */
  int flag2;
} ClampToConstraint;

/* Child Of Constraint */
typedef struct ChildOfConstraint {
  /* Ob which will act as parent (or target comes from). */
  struct Ob *tar;
  /* Settings. */
  int flag;
  char _pad[4];
  /* Parent-inverse matrix to use. */
  float invmat[4][4];
  /* String to specify a subobject target, MAX_ID_NAME-2. */
  char subtarget[64];
} ChildOfConstraint;

/* Generic Transform->Transform Constraint */
typedef struct TransformConstraint {
  /* Target (i.e. 'driver' ob/bone). */
  struct Ob *tar;
  /* MAX_ID_NAME-2. */
  char subtarget[64];

  /* Can be loc(0), rot(1) or size(2). */
  short from, to;
  /* Defines which target-axis deform is copied by each owner-axis. */
  char map[3];
  /* Extrapolate motion? if 0, confine to ranges. */
  char expo;

  /* Input rotation type - uses the same values as driver targets. */
  char from_rotation_mode;
  /* Output euler order override. */
  char to_euler_order;

  /* Mixing modes for location, rotation, and scale. */
  char mix_mode_loc;
  char mix_mode_rot;
  char mix_mode_scale;

  char _pad[3];

  /* From_min/max defines range of target transform. */
  float from_min[3];
  /* To map on to to_min/max range. */
  float from_max[3];
  /* Range of motion on owner caused by target. */
  float to_min[3];
  float to_max[3];

  /* From_min/max defines range of target transform. */
  float from_min_rot[3];
  /* To map on to to_min/max range. */
  float from_max_rot[3];
  /* Range of motion on owner caused by target. */
  float to_min_rot[3];
  float to_max_rot[3];

  /* From_min/max defines range of target transform. */
  float from_min_scale[3];
  /* To map on to to_min/max range. */
  float from_max_scale[3];
  /* Range of motion on owner caused by target. */
  float to_min_scale[3];
  float to_max_scale[3];
} TransformConstraint;

/* Pivot Constraint */
typedef struct PivotConstraint {
  /* Pivot Point:
   * Either target object + offset, or just offset is used */
  /* Target ob (optional). */
  struct Ob *tar;
  /* Subtarget name (optional), MAX_ID_NAME-2. */
  char subtarget[64];
  /* Offset from the target to use, regardless of whether it exists. */
  float offset[3];

  /* Rotation-driven activation:
   * This option provides easier one-stop setups for foot-rolls.  */
  /* Rotation axes to consider for this (ePivotConstraint_Axis). */
  short rotAxis;

  /* General flags */
  /* ePivotConstraint_Flag. */
  short flag;
} PivotConstraint;

/* transform limiting constraints - zero target */
/* Limit Location Constraint */
typedef struct LocLimitConstraint {
  float xmin, xmax;
  float ymin, ymax;
  float zmin, zmax;
  short flag;
  short flag2;
} LocLimitConstraint;

/* Limit Rotation Constraint */
typedef struct RotLimitConstraint {
  float xmin, xmax;
  float ymin, ymax;
  float zmin, zmax;
  short flag;
  short flag2;
  char euler_order;
  char _pad[3];
} RotLimitConstraint;

/* Limit Scale Constraint */
typedef struct SizeLimitConstraint {
  float xmin, xmax;
  float ymin, ymax;
  float zmin, zmax;
  short flag;
  short flag2;
} SizeLimitConstraint;

/* Limit Distance Constraint */
typedef struct DistLimitConstraint {
  struct Ob *tar;
  /* MAX_ID_NAME-2. */
  char subtarget[64];

  /* Distance (radius of clamping sphere) from target. */
  float dist;
  /* Distance from clamping-sphere to start applying 'fade'. */
  float soft;

  /* Settings. */
  short flag;
  /* How to limit in relation to clamping sphere. */
  short mode;
  char _pad[4];
} DistLimitConstraint;

/* ShrinkWrap Constraint */
typedef struct ShrinkwrapConstraint {
  struct Ob *target;
  /* Distance to kept from target. */
  float dist;
  /* Shrink type (look on MOD shrinkwrap for values). */
  short shrinkType;
  /* Axis to project/constrain. */
  char projAxis;
  /* Space to project axis in. */
  char projAxisSpace;
  /* Distance to search. */
  float projLimit;
  /* Inside/outside/on surface (see MOD shrinkwrap). */
  char shrinkMode;
  /* Options. */
  char flag;
  /* Axis to align to normal. */
  char trackAxis;
  char _pad;
} ShrinkwrapConstraint;

/* Follow Track constraints */
typedef struct FollowTrackConstraint {
  struct MovieClip *clip;
  /* MAX_NAME. */
  char track[64];
  int flag;
  int frame_method;
  /* MAX_NAME. */
  char ob[64];
  struct Ob *camera;
  struct Ob *depth_ob;
} FollowTrackConstraint;

/* Camera Solver constraints */
typedef struct CameraSolverConstraint {
  struct MovieClip *clip;
  int flag;
  char _pad[4];
} CameraSolverConstraint;

/* Camera Solver constraints */
typedef struct ObSolverConstraint {
  struct MovieClip *clip;
  int flag;
  char _pad[4];
  /* MAX_NAME. */
  char ob[64];
  /* Parent-inverse matrix to use. */
  float invmat[4][4];
  struct Ob *camera;
} ObSolverConstraint;

/* Transform matrix cache constraint */
typedef struct TransformCacheConstraint {
  struct CacheFile *cache_file;
  /* FILE_MAX. */
  char ob_path[1024];

  /* Runtime. */
  struct CacheReader *reader;
  char reader_ob_path[1024];
} TransformCacheConstraint;

/* Constraint->type
 * - Do not ever change the order of these, or else files could get
 *   broken as their correct val cannot be resolve */
typedef enum eConstraintTypes {
  /* Invalid/legacy constraint */
  CONSTRAINT_TYPE_NULL = 0,
  /* Unimplemented non longer :) - during constraints recode, Aligorith */
  CONSTRAINT_TYPE_CHILDOF = 1,
  CONSTRAINT_TYPE_TRACKTO = 2,
  CONSTRAINT_TYPE_KINEMATIC = 3,
  CONSTRAINT_TYPE_FOLLOWPATH = 4,
  /* Unimplemented no longer :) - Aligorith */
  CONSTRAINT_TYPE_ROTLIMIT = 5,
  /* Unimplemented no longer :) - Aligorith */
  CONSTRAINT_TYPE_LOCLIMIT = 6,
  /* Unimplemented no longer :) - Aligorith */
  CONSTRAINT_TYPE_SIZELIMIT = 7,
  CONSTRAINT_TYPE_ROTLIKE = 8,
  CONSTRAINT_TYPE_LOCLIKE = 9,
  CONSTRAINT_TYPE_SIZELIKE = 10,
  /* Unimplemented no longer :) - Aligorith. Scripts */
  CONSTRAINT_TYPE_PYTHON = 11,
  CONSTRAINT_TYPE_ACTION = 12,
  /* New Tracking constraint that locks an axis in place - theeth */
  CONSTRAINT_TYPE_LOCKTRACK = 13,
  /* limit distance */
  CONSTRAINT_TYPE_DISTLIMIT = 14,
  /* claiming this to be mine :) is in tuhopuu bjornmose */
  CONSTRAINT_TYPE_STRETCHTO = 15,
  /* floor constraint */
  CONSTRAINT_TYPE_MINMAX = 16,
  /* CONSTRAINT_TYPE_DEPRECATED = 17 */
  /* clampto constraint */
  CONSTRAINT_TYPE_CLAMPTO = 18,
  /* transformation (loc/rot/size -> loc/rot/size) constraint */
  CONSTRAINT_TYPE_TRANSFORM = 19,
  /* shrinkwrap (loc/rot) constraint */
  CONSTRAINT_TYPE_SHRINKWRAP = 20,
  /* New Tracking constraint that minimizes twisting */
  CONSTRAINT_TYPE_DAMPTRACK = 21,
  /* Spline-IK - Align 'n' bones to a curve */
  CONSTRAINT_TYPE_SPLINEIK = 22,
  /* Copy transform matrix */
  CONSTRAINT_TYPE_TRANSLIKE = 23,
  /* Maintain volume during scaling */
  CONSTRAINT_TYPE_SAMEVOL = 24,
  /* Pivot Constraint */
  CONSTRAINT_TYPE_PIVOT = 25,
  /* Follow Track Constraint */
  CONSTRAINT_TYPE_FOLLOWTRACK = 26,
  /* Camera Solver Constraint */
  CONSTRAINT_TYPE_CAMERASOLVER = 27,
  /* Object Solver Constraint */
  CONSTRAINT_TYPE_OBSOLVER = 28,
  /* Transform Cache Constraint */
  CONSTRAINT_TYPE_TRANSFORM_CACHE = 29,
  /* Armature Deform Constraint */
  CONSTRAINT_TYPE_ARMATURE = 30,

  /* NOTE: no constraints are allowed to be added after this */
  NUM_CONSTRAINT_TYPES,
} eConstraintTypes;

/* Constraint->flag */
/* flags 0x2 (1 << 1) and 0x8 (1 << 3) were used in past */
/* flag 0x20 (1 << 5) was used to indicate that a constraint was evaluated
 *                  using a 'local' hack for posebones only. */
typedef enum eConstraintFlags {
#ifdef TYPES_DEPRECATED_ALLOW
  /* Expansion for old box constraint layouts. Just for versioning. */
  CONSTRAINT_EXPAND_DEPRECATED = (1 << 0),
#endif
  /* pre-check for illegal object name or bone name */
  CONSTRAINT_DISABLE = (1 << 2),
  /* to indicate which Ipo should be shown, maybe for 3d access later too */
  CONSTRAINT_ACTIVE = (1 << 4),
  /* to indicate that the owner's space should only be changed into ownspace, but not out of it */
  CONSTRAINT_SPACEONCE = (1 << 6),
  /* influence ipo is on constraint itself, not in action channel */
  CONSTRAINT_OWN_IPO = (1 << 7),
  /* indicates that constraint is temporarily disabled (only used in GE) */
  CONSTRAINT_OFF = (1 << 9),
  /* use bbone curve shape when calculating headtail values (also used by dependency graph!) */
  CONSTRAINT_BBONE_SHAPE = (1 << 10),
  /* That constraint has been inserted in local override (i.e. it can be fully edited!). */
  CONSTRAINT_OVERRIDE_LIB_LOCAL = (1 << 11),
  /* use full transformation (not just segment locations) - only set at runtime. */
  CONSTRAINT_BBONE_SHAPE_FULL = (1 << 12),
} eConstraintFlags;

/* Constraint->ownspace/tarspace */
typedef enum eConstraintSpaceTypes {
  /* Default for all - world-space. */
  CONSTRAINT_SPACE_WORLD = 0,
  /* For all - custom space. */
  CONSTRAINT_SPACE_CUSTOM = 5,
  /* For obs (relative to parent/wo parent influence),
   * for bones (along normals of bone, wo parent/rest-positions). */
  CONSTRAINT_SPACE_LOCAL = 1,
  /* For posechannels - pose space. */
  CONSTRAINT_SPACE_POSE = 2,
  /* For posechannels - local with parent. */
  CONSTRAINT_SPACE_PARLOCAL = 3,
  /* For posechannels - local converted to the owner bone orientation. */
  CONSTRAINT_SPACE_OWNLOCAL = 6,
  /* For files from between 2.43-2.46 (should have been parlocal). */
  CONSTRAINT_SPACE_INVALID = 4, /* do not exchange for anything! */
} eConstraintSpaceTypes;

/* Common enum for constraints that support override. */
typedef enum eConstraintEulerOrder {
  /* Automatic euler mode. */
  CONSTRAINT_EULER_AUTO = 0,

  /* Explicit euler rotation modes - must sync with BLI_math_rotation.h defines. */
  CONSTRAINT_EULER_XYZ = 1,
  CONSTRAINT_EULER_XZY = 2,
  CONSTRAINT_EULER_YXZ = 3,
  CONSTRAINT_EULER_YZX = 4,
  CONSTRAINT_EULER_ZXY = 5,
  CONSTRAINT_EULER_ZYX = 6,
} eConstraintEulerOrder;

/* RotateLikeConstraint.flag */
typedef enum eCopyRotationFlags {
  ROTLIKE_X = (1 << 0),
  ROTLIKE_Y = (1 << 1),
  ROTLIKE_Z = (1 << 2),
  ROTLIKE_X_INVERT = (1 << 4),
  ROTLIKE_Y_INVERT = (1 << 5),
  ROTLIKE_Z_INVERT = (1 << 6),
#ifdef TYPES_DEPRECATED_ALLOW
  ROTLIKE_OFFSET = (1 << 7),
#endif
} eCopyRotationFlags;

/* RotateLikeConstraint.mix_mode */
typedef enum eCopyRotationMixMode {
  /* Replace rotation channel vals. */
  ROTLIKE_MIX_REPLACE = 0,
  /* Legacy Offset mode - don't use. */
  ROTLIKE_MIX_OFFSET = 1,
  /* Add Euler components together. */
  ROTLIKE_MIX_ADD = 2,
  /* Multiply the copied rotation on the left. */
  ROTLIKE_MIX_BEFORE = 3,
  /* Multiply the copied rotation on the right. */
  ROTLIKE_MIX_AFTER = 4,
} eCopyRotationMixMode;

/* LocateLikeConstraint.flag */
typedef enum eCopyLocationFlags {
  LOCLIKE_X = (1 << 0),
  LOCLIKE_Y = (1 << 1),
  LOCLIKE_Z = (1 << 2),
  /* LOCLIKE_TIP is a deprecated option... use headtail=1.0f instead */
  LOCLIKE_TIP = (1 << 3),
  LOCLIKE_X_INVERT = (1 << 4),
  LOCLIKE_Y_INVERT = (1 << 5),
  LOCLIKE_Z_INVERT = (1 << 6),
  LOCLIKE_OFFSET = (1 << 7),
} eCopyLocationFlags;

/* SizeLikeConstraint.flag */
typedef enum eCopyScaleFlags {
  SIZELIKE_X = (1 << 0),
  SIZELIKE_Y = (1 << 1),
  SIZELIKE_Z = (1 << 2),
  SIZELIKE_OFFSET = (1 << 3),
  SIZELIKE_MULTIPLY = (1 << 4),
  SIZELIKE_UNIFORM = (1 << 5),
} eCopyScaleFlags;

/* TransLikeConstraint.flag */
typedef enum eCopyTransformsFlags {
  /* Remove shear from the target matrix. */
  TRANSLIKE_REMOVE_TARGET_SHEAR = (1 << 0),
} eCopyTransformsFlags;

/* TransLikeConstraint.mix_mode */
typedef enum eCopyTransformsMixMode {
  /* Replace rotation channel values. */
  TRANSLIKE_MIX_REPLACE = 0,
  /* Multiply the copied transformation on the left, with anti-shear scale handling. */
  TRANSLIKE_MIX_BEFORE = 1,
  /* Multiply the copied transformation on the right, with anti-shear scale handling. */
  TRANSLIKE_MIX_AFTER = 2,
  /* Multiply the copied transformation on the left, handling loc/rot/scale separately. */
  TRANSLIKE_MIX_BEFORE_SPLIT = 3,
  /* Multiply the copied transformation on the right, handling loc/rot/scale separately. */
  TRANSLIKE_MIX_AFTER_SPLIT = 4,
  /* Multiply the copied transformation on the left, using simple matrix multiplication. */
  TRANSLIKE_MIX_BEFORE_FULL = 5,
  /* Multiply the copied transformation on the right, using simple matrix multiplication. */
  TRANSLIKE_MIX_AFTER_FULL = 6,
} eCopyTransformsMixMode;

/* TransformConstraint.to/from */
typedef enum eTransformToFrom {
  TRANS_LOCATION = 0,
  TRANS_ROTATION = 1,
  TRANS_SCALE = 2,
} eTransformToFrom;

/* TransformConstraint.mix_mode_loc */
typedef enum eTransformMixModeLoc {
  /* Add component values together (default). */
  TRANS_MIXLOC_ADD = 0,
  /* Replace component values. */
  TRANS_MIXLOC_REPLACE = 1,
} eTransformMixModeLoc;

/* TransformConstraint.mix_mode_rot */
typedef enum eTransformMixModeRot {
  /* Add component values together (default). */
  TRANS_MIXROT_ADD = 0,
  /* Replace component vals. */
  TRANS_MIXROT_REPLACE = 1,
  /* Multiply the generated rotation on the left. */
  TRANS_MIXROT_BEFORE = 2,
  /* Multiply the generated rotation on the right. */
  TRANS_MIXROT_AFTER = 3,
} eTransforMixModeRot;

/* TransformConstraint.mix_mode_scale */
typedef enum eTransformMixModeScale {
  /* Replace component vals (default). */
  TRANS_MIXSCALE_REPLACE = 0,
  /* Multiply component vals together. */
  TRANS_MIXSCALE_MULTIPLY = 1,
} eTransformMixModeScale;

/* bSameVolumeConstraint.free_axis */
typedef enum eSameVolumeAxis {
  SAMEVOL_X = 0,
  SAMEVOL_Y = 1,
  SAMEVOL_Z = 2,
} eSameVolumeAxis;

/* SameVolumeConstraint.mode */
typedef enum eSameVolumeMode {
  /* Strictly maintain the volume, overriding non-free axis scale. */
  SAMEVOL_STRICT = 0,
  /* Maintain the volume when scale is uniform, pass non-uniform other axis scale through. */
  SAMEVOL_UNIFORM = 1,
  /* Maintain the volume when scaled only on the free axis, pass other axis scale through. */
  SAMEVOL_SINGLE_AXIS = 2,
} eSameVolumeMode;

/* ActionConstraint.flag */
typedef enum eActionConstraintFlags {
  /* Bones use "object" part of target action, instead of "same bone name" part */
  ACTCON_BONE_USE_OBJECT_ACTION = (1 << 0),
  /* Ignore the transform of 'tar' and use 'eval_time' instead: */
  ACTCON_USE_EVAL_TIME = (1 << 1),
} eActionConstraintFlags;

/* ActionConstraint.mix_mode */
typedef enum eActionConstraint_MixMode {
  /* Multiply the action transformation on the right. */
  ACTCON_MIX_AFTER_FULL = 0,
  /* Multiply the action transformation on the left. */
  ACTCON_MIX_BEFORE_FULL = 3,
  /* Multiply the action transformation on the right, with anti-shear scale handling. */
  ACTCON_MIX_AFTER = 1,
  /* Multiply the action transformation on the left, with anti-shear scale handling. */
  ACTCON_MIX_BEFORE = 2,
  /* Separately combine Translation, Rotation and Scale, with rotation on the right. */
  ACTCON_MIX_AFTER_SPLIT = 4,
  /* Separately combine Translation, Rotation and Scale, with rotation on the left. */
  ACTCON_MIX_BEFORE_SPLIT = 5,
} eActionConstraintMixMode;

/* Locked-Axis Vals (Locked Track) */
typedef enum eLockAxis_Modes {
  LOCK_X = 0,
  LOCK_Y = 1,
  LOCK_Z = 2,
} eLockAxis_Modes;

/* Up-Axis Vals (TrackTo and Locked Track) */
typedef enum eUpAxisModes {
  UP_X = 0,
  UP_Y = 1,
  UP_Z = 2,
} eUpAxisModes;

/* Tracking axis (TrackTo, Locked Track, Damped Track) and minmax (floor) constraint */
typedef enum eTrackToAxisModes {
  TRACK_X = 0,
  TRACK_Y = 1,
  TRACK_Z = 2,
  TRACK_nX = 3,
  TRACK_nY = 4,
  TRACK_nZ = 5,
} eTrackToAxisModes;

/* Shrinkwrap flags */
typedef enum eShrinkwrapFlags {
  /* Also ray-cast in the opposite direction. */
  CON_SHRINKWRAP_PROJECT_OPPOSITE = (1 << 0),
  /* Invert the cull mode when projecting opposite. */
  CON_SHRINKWRAP_PROJECT_INVERT_CULL = (1 << 1),
  /* Align the specified axis to the target normal. */
  CON_SHRINKWRAP_TRACK_NORMAL = (1 << 2),

  /* Ignore front faces in project; same value as MOD_SHRINKWRAP_CULL_TARGET_FRONTFACE */
  CON_SHRINKWRAP_PROJECT_CULL_FRONTFACE = (1 << 3),
  /* Ignore back faces in project; same value as MOD_SHRINKWRAP_CULL_TARGET_BACKFACE */
  CON_SHRINKWRAP_PROJECT_CULL_BACKFACE = (1 << 4),
} eShrinkwrapFlags;

#define CON_SHRINKWRAP_PROJECT_CULL_MASK \
  (CON_SHRINKWRAP_PROJECT_CULL_FRONTFACE | CON_SHRINKWRAP_PROJECT_CULL_BACKFACE)

/* FollowPath flags */
typedef enum eFollowPathFlags {
  FOLLOWPATH_FOLLOW = (1 << 0),
  FOLLOWPATH_STATIC = (1 << 1),
  FOLLOWPATH_RADIUS = (1 << 2),
} eFollowPathFlags;

/* TrackToConstraint->flags */
typedef enum eTrackToFlags {
  TARGET_Z_UP = (1 << 0),
} eTrackToFlags;

/* Stretch To Constraint -> volmode */
typedef enum eStretchToVolMode {
  VOLUME_XZ = 0,
  VOLUME_X = 1,
  VOLUME_Z = 2,
  NO_VOLUME = 3,
} eStretchToVolMode;

/* Stretch To Constraint -> plane mode */
typedef enum eStretchToPlaneMode {
  PLANE_X = 0,
  SWING_Y = 1,
  PLANE_Z = 2,
} eStretchToPlaneMode;

/* Clamp-To Constraint ->flag */
typedef enum eClampToModes {
  CLAMPTO_AUTO = 0,
  CLAMPTO_X = 1,
  CLAMPTO_Y = 2,
  CLAMPTO_Z = 3,
} eClampToModes;

/* ClampTo Constraint ->flag2 */
typedef enum eClampToFlags {
  CLAMPTO_CYCLIC = (1 << 0),
} eClampTo_lFlags;

/* KinematicConstraint->flag */
typedef enum eKinematicFlags {
  CONSTRAINT_IK_TIP = (1 << 0),
  CONSTRAINT_IK_ROT = (1 << 1),
  /* targetless */
  CONSTRAINT_IK_AUTO = (1 << 2),
  /* autoik */
  CONSTRAINT_IK_TMP = (1 << 3),
  CONSTRAINT_IK_STRETCH = (1 << 4),
  CONSTRAINT_IK_POS = (1 << 5),
  CONSTRAINT_IK_SETANGLE = (1 << 6),
  CONSTRAINT_IK_GETANGLE = (1 << 7),
  /* limit axis */
  CONSTRAINT_IK_NO_POS_X = (1 << 8),
  CONSTRAINT_IK_NO_POS_Y = (1 << 9),
  CONSTRAINT_IK_NO_POS_Z = (1 << 10),
  CONSTRAINT_IK_NO_ROT_X = (1 << 11),
  CONSTRAINT_IK_NO_ROT_Y = (1 << 12),
  CONSTRAINT_IK_NO_ROT_Z = (1 << 13),
  /* axis relative to target */
  CONSTRAINT_IK_TARGETAXIS = (1 << 14),
} eKinematicFlags;

/* SplineIKConstraint->flag */
typedef enum eSplineIKFlags {
  /* chain has been attached to spline */
  CONSTRAINT_SPLINEIK_BOUND = (1 << 0),
  /* root of chain is not influenced by the constraint */
  CONSTRAINT_SPLINEIK_NO_ROOT = (1 << 1),
#ifdef TYPES_DEPRECATED_ALLOW
  /* bones in the chain should not scale to fit the curve */
  CONSTRAINT_SPLINEIK_SCALE_LIMITED = (1 << 2),
#endif
  /* evenly distribute the bones along the path regardless of length */
  CONSTRAINT_SPLINEIK_EVENSPLITS = (1 << 3),
  /* don't adjust the x and z scaling of the bones by the curve radius */
  CONSTRAINT_SPLINEIK_NO_CURVERAD = (1 << 4),

  /* for "volumetric" xz scale mode, limit the minimum or maximum scale values */
  CONSTRAINT_SPLINEIK_USE_BULGE_MIN = (1 << 5),
  CONSTRAINT_SPLINEIK_USE_BULGE_MAX = (1 << 6),

  /* apply volume preservation over original scaling of the bone */
  CONSTRAINT_SPLINEIK_USE_ORIGINAL_SCALE = (1 << 7),
} eSplineIKFlags;

/* SplineIKConstraint->xzScaleMode */
typedef enum eSplineIKXZScaleModes {
  /* no x/z scaling */
  CONSTRAINT_SPLINEIK_XZS_NONE = 0,
  /* bones in the chain should take their x/z scales from the original scaling */
  CONSTRAINT_SPLINEIK_XZS_ORIGINAL = 1,
  /* x/z scales are the inverse of the y-scale */
  CONSTRAINT_SPLINEIK_XZS_INVERSE = 2,
  /* x/z scales are computed using a volume preserving technique (from Stretch To constraint) */
  CONSTRAINT_SPLINEIK_XZS_VOLUMETRIC = 3,
} eSplineIKXZScaleModes;

/* SplineIKConstraint->yScaleMode */
typedef enum eSplineIKYScaleModes {
  /* no y scaling */
  CONSTRAINT_SPLINEIK_YS_NONE = 0,
  /* bones in the chain should be scaled to fit the length of the curve */
  CONSTRAINT_SPLINEIK_YS_FIT_CURVE = 1,
  /* bones in the chain should take their y scales from the original scaling */
  CONSTRAINT_SPLINEIK_YS_ORIGINAL = 2,
} eSplineIKYScaleModes;

/* ArmatureConstraint -> flag */
typedef enum eArmatureFlags {
  /** use dual quaternion blending */
  CONSTRAINT_ARMATURE_QUATERNION = (1 << 0),
  /* use envelopes */
  CONSTRAINT_ARMATURE_ENVELOPE = (1 << 1),
  /* use current bone location */
  CONSTRAINT_ARMATURE_CUR_LOCATION = (1 << 2),
} eArmatureFlags;

/* MinMax (floor) flags */
typedef enum eFloorFlags {
  /* MINMAX_STICKY = (1 << 0), */ /* Deprecated. */
  /* MINMAX_STUCK = (1 << 1), */  /* Deprecated. */
  MINMAX_USEROT = (1 << 2),
} eFloorFlags;

/* transform limiting constraints -> flag2 */
typedef enum eTransformLimitsFlags2 {
  /* not used anymore - for older Limit Location constraints only */
  /* LIMIT_NOPARENT = (1 << 0), */ /* UNUSED */
  /* for all Limit constraints - allow to be used during transform? */
  LIMIT_TRANSFORM = (1 << 1),
} eTransformLimitsFlags2;

/* transform limiting constraints -> flag (own flags). */
typedef enum eTransformLimitsFlags {
  LIMIT_XMIN = (1 << 0),
  LIMIT_XMAX = (1 << 1),
  LIMIT_YMIN = (1 << 2),
  LIMIT_YMAX = (1 << 3),
  LIMIT_ZMIN = (1 << 4),
  LIMIT_ZMAX = (1 << 5),
} eTransformLimitsFlags;

/* limit rotation constraint -> flag (own flags). */
typedef enum eRotLimitFlags {
  LIMIT_XROT = (1 << 0),
  LIMIT_YROT = (1 << 1),
  LIMIT_ZROT = (1 << 2),
} eRotLimitFlags;

/* distance limit constraint */
/* DistLimitConstraint->flag */
typedef enum eDistLimitFlag {
  /* "soft" cushion effect when reaching the limit sphere */ /* NOT IMPLEMENTED! */
  LIMITDIST_USESOFT = (1 << 0),
  /* as for all Limit constraints - allow to be used during transform? */
  LIMITDIST_TRANSFORM = (1 << 1),
} eDistLimitFlag;

/* DistLimitConstraint->mode */
typedef enum eDistLimitModes {
  LIMITDIST_INSIDE = 0,
  LIMITDIST_OUTSIDE = 1,
  LIMITDIST_ONSURFACE = 2,
} eDistLimitModes;

/* python constraint -> flag */
typedef enum ePyConstraint_Flags {
  PYCON_USETARGETS = (1 << 0),
  PYCON_SCRIPTERROR = (1 << 1),
} ePyConstraint_Flags;

/* ChildOf Constraint -> flag */
typedef enum eChildOfFlags {
  CHILDOF_LOCX = (1 << 0),
  CHILDOF_LOCY = (1 << 1),
  CHILDOF_LOCZ = (1 << 2),
  CHILDOF_ROTX = (1 << 3),
  CHILDOF_ROTY = (1 << 4),
  CHILDOF_ROTZ = (1 << 5),
  CHILDOF_SIZEX = (1 << 6),
  CHILDOF_SIZEY = (1 << 7),
  CHILDOF_SIZEZ = (1 << 8),
  CHILDOF_ALL = 511,
  /* Tmp flag used by the Set Inverse operator. */
  CHILDOF_SET_INVERSE = (1 << 9),
} eChildOfFlags;

/* Pivot Constraint */
/* Restrictions for Pivot Constraint axis to consider for enabling constraint */
typedef enum ePivotConstraintAxis {
  /* do not consider this activity-clamping */
  PIVOTCON_AXIS_NONE = -1,

  /* consider -ve x-axis rotations */
  PIVOTCON_AXIS_X_NEG = 0,
  /* consider -ve y-axis rotations */
  PIVOTCON_AXIS_Y_NEG = 1,
  /* consider -ve z-axis rotations */
  PIVOTCON_AXIS_Z_NEG = 2,

  /* consider +ve x-axis rotations */
  PIVOTCON_AXIS_X = 3,
  /* consider +ve y-axis rotations */
  PIVOTCON_AXIS_Y = 4,
  /* consider +ve z-axis rotations */
  PIVOTCON_AXIS_Z = 5,
} ePivotConstraintAxis;

/* settings for Pivot Constraint in general */
typedef enum ePivotConstraintFlag {
  /* offset is to be interpreted as being a fixed-point in space */
  PIVOTCON_FLAG_OFFSET_ABS = (1 << 0),
  /* rotation-based activation uses negative rotation to drive result */
  PIVOTCON_FLAG_ROTACT_NEG = (1 << 1),
} ePivotConstraintFlag;

typedef enum eFollowTrackFlags {
  FOLLOWTRACK_ACTIVECLIP = (1 << 0),
  FOLLOWTRACK_USE_3D_POSITION = (1 << 1),
  FOLLOWTRACK_USE_UNDISTORTION = (1 << 2),
} eFollowTrackFlags;

typedef enum eFollowTrackFrameMethod {
  FOLLOWTRACK_FRAME_STRETCH = 0,
  FOLLOWTRACK_FRAME_FIT = 1,
  FOLLOWTRACK_FRAME_CROP = 2,
} eFollowTrackFrameMethod;

/* CameraSolver Constraint -> flag */
typedef enum eCameraSolverFlags {
  CAMERASOLVER_ACTIVECLIP = (1 << 0),
} eCameraSolverFlags;

/* ObSolver Constraint -> flag */
typedef enum eObSolverFlags {
  OBJECTSOLVER_ACTIVECLIP = (1 << 0),
  /* Tmp flag used by the Set Inverse operator. */
  OBSOLVER_SET_INVERSE = (1 << 1),
} eObSolverFlags;

/* ObSolver Constraint -> flag */
typedef enum eStretchToFlags {
  STRETCHTOCON_USE_BULGE_MIN = (1 << 0),
  STRETCHTOCON_USE_BULGE_MAX = (1 << 1),
} eStretchToFlags;

#ifdef __cplusplus
}
#endif
