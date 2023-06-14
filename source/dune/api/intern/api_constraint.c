#include <stdlib.h>

#include "lib_math.h"

#include "mem_guardedalloc.h"

#include "lang.h"

#include "types_action.h"
#include "types_constraint.h"
#include "types_modifier.h"
#include "types_object.h"
#include "types_scene.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_types.h"

#include "ed_object.h"

/* please keep the names in sync with constraint.c */
const EnumPropItem api_enum_constraint_type_items[] = {
    {0, "", 0, N_("Motion Tracking"), ""},
    {CONSTRAINT_TYPE_CAMERASOLVER, "CAMERA_SOLVER", ICON_CON_CAMERASOLVER, "Camera Solver", ""},
    {CONSTRAINT_TYPE_FOLLOWTRACK, "FOLLOW_TRACK", ICON_CON_FOLLOWTRACK, "Follow Track", ""},
    {CONSTRAINT_TYPE_OBJECTSOLVER, "OBJECT_SOLVER", ICON_CON_OBJECTSOLVER, "Object Solver", ""},
    {0, "", 0, N_("Transform"), ""},
    {CONSTRAINT_TYPE_LOCLIKE,
     "COPY_LOCATION",
     ICON_CON_LOCLIKE,
     "Copy Location",
     "Copy the location of a target (with an optional offset), so that they move together"},
    {CONSTRAINT_TYPE_ROTLIKE,
     "COPY_ROTATION",
     ICON_CON_ROTLIKE,
     "Copy Rotation",
     "Copy the rotation of a target (with an optional offset), so that they rotate together"},
    {CONSTRAINT_TYPE_SIZELIKE,
     "COPY_SCALE",
     ICON_CON_SIZELIKE,
     "Copy Scale",
     "Copy the scale factors of a target (with an optional offset), so that they are scaled by "
     "the same amount"},
    {CONSTRAINT_TYPE_TRANSLIKE,
     "COPY_TRANSFORMS",
     ICON_CON_TRANSLIKE,
     "Copy Transforms",
     "Copy all the transformations of a target, so that they move together"},
    {CONSTRAINT_TYPE_DISTLIMIT,
     "LIMIT_DISTANCE",
     ICON_CON_DISTLIMIT,
     "Limit Distance",
     "Restrict movements to within a certain distance of a target (at the time of constraint "
     "evaluation only)"},
    {CONSTRAINT_TYPE_LOCLIMIT,
     "LIMIT_LOCATION",
     ICON_CON_LOCLIMIT,
     "Limit Location",
     "Restrict movement along each axis within given ranges"},
    {CONSTRAINT_TYPE_ROTLIMIT,
     "LIMIT_ROTATION",
     ICON_CON_ROTLIMIT,
     "Limit Rotation",
     "Restrict rotation along each axis within given ranges"},
    {CONSTRAINT_TYPE_SIZELIMIT,
     "LIMIT_SCALE",
     ICON_CON_SIZELIMIT,
     "Limit Scale",
     "Restrict scaling along each axis with given ranges"},
    {CONSTRAINT_TYPE_SAMEVOL,
     "MAINTAIN_VOLUME",
     ICON_CON_SAMEVOL,
     "Maintain Volume",
     "Compensate for scaling one axis by applying suitable scaling to the other two axes"},
    {CONSTRAINT_TYPE_TRANSFORM,
     "TRANSFORM",
     ICON_CON_TRANSFORM,
     "Transformation",
     "Use one transform property from target to control another (or same) property on owner"},
    {CONSTRAINT_TYPE_TRANSFORM_CACHE,
     "TRANSFORM_CACHE",
     ICON_CON_TRANSFORM_CACHE,
     "Transform Cache",
     "Look up the transformation matrix from an external file"},
    {0, "", 0, N_("Tracking"), ""},
    {CONSTRAINT_TYPE_CLAMPTO,
     "CLAMP_TO",
     ICON_CON_CLAMPTO,
     "Clamp To",
     "Restrict movements to lie along a curve by remapping location along curve's longest axis"},
    {CONSTRAINT_TYPE_DAMPTRACK,
     "DAMPED_TRACK",
     ICON_CON_TRACKTO,
     "Damped Track",
     "Point towards a target by performing the smallest rotation necessary"},
    {CONSTRAINT_TYPE_KINEMATIC,
     "IK",
     ICON_CON_KINEMATIC,
     "Inverse Kinematics",
     "Control a chain of bones by specifying the endpoint target (Bones only)"},
    {CONSTRAINT_TYPE_LOCKTRACK,
     "LOCKED_TRACK",
     ICON_CON_LOCKTRACK,
     "Locked Track",
     "Rotate around the specified ('locked') axis to point towards a target"},
    {CONSTRAINT_TYPE_SPLINEIK,
     "SPLINE_IK",
     ICON_CON_SPLINEIK,
     "Spline IK",
     "Align chain of bones along a curve (Bones only)"},
    {CONSTRAINT_TYPE_STRETCHTO,
     "STRETCH_TO",
     ICON_CON_STRETCHTO,
     "Stretch To",
     "Stretch along Y-Axis to point towards a target"},
    {CONSTRAINT_TYPE_TRACKTO,
     "TRACK_TO",
     ICON_CON_TRACKTO,
     "Track To",
     "Legacy tracking constraint prone to twisting artifacts"},
    {0, "", 0, N_("Relationship"), ""},
    {CONSTRAINT_TYPE_ACTION,
     "ACTION",
     ICON_ACTION,
     "Action",
     "Use transform property of target to look up pose for owner from an Action"},
    {CONSTRAINT_TYPE_ARMATURE,
     "ARMATURE",
     ICON_CON_ARMATURE,
     "Armature",
     "Apply weight-blended transformation from multiple bones like the Armature modifier"},
    {CONSTRAINT_TYPE_CHILDOF,
     "CHILD_OF",
     ICON_CON_CHILDOF,
     "Child Of",
     "Make target the 'detachable' parent of owner"},
    {CONSTRAINT_TYPE_MINMAX,
     "FLOOR",
     ICON_CON_FLOOR,
     "Floor",
     "Use position (and optionally rotation) of target to define a 'wall' or 'floor' that the "
     "owner can not cross"},
    {CONSTRAINT_TYPE_FOLLOWPATH,
     "FOLLOW_PATH",
     ICON_CON_FOLLOWPATH,
     "Follow Path",
     "Use to animate an object/bone following a path"},
    {CONSTRAINT_TYPE_PIVOT,
     "PIVOT",
     ICON_CON_PIVOT,
     "Pivot",
     "Change pivot point for transforms (buggy)"},
#if 0
    {CONSTRAINT_TYPE_RIGIDBODYJOINT,
     "RIGID_BODY_JOINT",
     ICON_CONSTRAINT_DATA,
     "Rigid Body Joint",
     "Use to define a Rigid Body Constraint (for Game Engine use only)"},
    {CONSTRAINT_TYPE_PYTHON,
     "SCRIPT",
     ICON_CONSTRAINT_DATA,
     "Script",
     "Custom constraint(s) written in Python (Not yet implemented)"},
#endif
    {CONSTRAINT_TYPE_SHRINKWRAP,
     "SHRINKWRAP",
     ICON_CON_SHRINKWRAP,
     "Shrinkwrap",
     "Restrict movements to surface of target mesh"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem target_space_pchan_items[] = {
    {CONSTRAINT_SPACE_WORLD,
     "WORLD",
     0,
     "World Space",
     "The transformation of the target is evaluated relative to the world "
     "coordinate system"},
    {CONSTRAINT_SPACE_CUSTOM,
     "CUSTOM",
     0,
     "Custom Space",
     "The transformation of the target is evaluated relative to a custom object/bone/vertex "
     "group"},
    {0, "", 0, NULL, NULL},
    {CONSTRAINT_SPACE_POSE,
     "POSE",
     0,
     "Pose Space",
     "The transformation of the target is only evaluated in the Pose Space, "
     "the target armature object transformation is ignored"},
    {CONSTRAINT_SPACE_PARLOCAL,
     "LOCAL_WITH_PARENT",
     0,
     "Local With Parent",
     "The transformation of the target bone is evaluated relative to its rest pose "
     "local coordinate system, thus including the parent-induced transformation"},
    {CONSTRAINT_SPACE_LOCAL,
     "LOCAL",
     0,
     "Local Space",
     "The transformation of the target is evaluated relative to its local "
     "coordinate system"},
    {CONSTRAINT_SPACE_OWNLOCAL,
     "LOCAL_OWNER_ORIENT",
     0,
     "Local Space (Owner Orientation)",
     "The transformation of the target bone is evaluated relative to its local coordinate "
     "system, followed by a correction for the difference in target and owner rest pose "
     "orientations. When applied as local transform to the owner produces the same global "
     "motion as the target if the parents are still in rest pose"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem owner_space_pchan_items[] = {
    {CONSTRAINT_SPACE_WORLD,
     "WORLD",
     0,
     "World Space",
     "The constraint is applied relative to the world coordinate system"},
    {CONSTRAINT_SPACE_CUSTOM,
     "CUSTOM",
     0,
     "Custom Space",
     "The constraint is applied in local space of a custom object/bone/vertex group"},
    {0, "", 0, NULL, NULL},
    {CONSTRAINT_SPACE_POSE,
     "POSE",
     0,
     "Pose Space",
     "The constraint is applied in Pose Space, the object transformation is ignored"},
    {CONSTRAINT_SPACE_PARLOCAL,
     "LOCAL_WITH_PARENT",
     0,
     "Local With Parent",
     "The constraint is applied relative to the rest pose local coordinate system "
     "of the bone, thus including the parent-induced transformation"},
    {CONSTRAINT_SPACE_LOCAL,
     "LOCAL",
     0,
     "Local Space",
     "The constraint is applied relative to the local coordinate system of the object"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem track_axis_items[] = {
    {TRACK_X, "TRACK_X", 0, "X", ""},
    {TRACK_Y, "TRACK_Y", 0, "Y", ""},
    {TRACK_Z, "TRACK_Z", 0, "Z", ""},
    {TRACK_nX, "TRACK_NEGATIVE_X", 0, "-X", ""},
    {TRACK_nY, "TRACK_NEGATIVE_Y", 0, "-Y", ""},
    {TRACK_nZ, "TRACK_NEGATIVE_Z", 0, "-Z", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem euler_order_items[] = {
    {CONSTRAINT_EULER_AUTO, "AUTO", 0, "Default", "Euler using the default rotation order"},
    {CONSTRAINT_EULER_XYZ, "XYZ", 0, "XYZ Euler", "Euler using the XYZ rotation order"},
    {CONSTRAINT_EULER_XZY, "XZY", 0, "XZY Euler", "Euler using the XZY rotation order"},
    {CONSTRAINT_EULER_YXZ, "YXZ", 0, "YXZ Euler", "Euler using the YXZ rotation order"},
    {CONSTRAINT_EULER_YZX, "YZX", 0, "YZX Euler", "Euler using the YZX rotation order"},
    {CONSTRAINT_EULER_ZXY, "ZXY", 0, "ZXY Euler", "Euler using the ZXY rotation order"},
    {CONSTRAINT_EULER_ZYX, "ZYX", 0, "ZYX Euler", "Euler using the ZYX rotation order"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

static const EnumPropItem space_object_items[] = {
    {CONSTRAINT_SPACE_WORLD,
     "WORLD",
     0,
     "World Space",
     "The transformation of the target is evaluated relative to the world coordinate system"},
    {CONSTRAINT_SPACE_CUSTOM,
     "CUSTOM",
     0,
     "Custom Space",
     "The transformation of the target is evaluated relative to a custom object/bone/vertex "
     "group"},
    {CONSTRAINT_SPACE_LOCAL,
     "LOCAL",
     0,
     "Local Space",
     "The transformation of the target is evaluated relative to its local coordinate system"},
    {0, NULL, 0, NULL, NULL},
};

#  include "types_cachefile_types.h"

#  include "dune_action.h"
#  include "dune_animsys.h"
#  include "dune_constraint.h"
#  include "dune_context.h"

#  ifdef WITH_ALEMBIC
#    include "ABC_alembic.h"
#  endif

static ApiStruct *api_ConstraintType_refine(struct PointerRNA *ptr)
{
  Constraint *con = (Constraint *)ptr->data;

  switch (con->type) {
    case CONSTRAINT_TYPE_CHILDOF:
      return &ApiChildOfConstraint;
    case CONSTRAINT_TYPE_TRACKTO:
      return &ApiTrackToConstraint;
    case CONSTRAINT_TYPE_KINEMATIC:
      return &ApiKinematicConstraint;
    case CONSTRAINT_TYPE_FOLLOWPATH:
      return &ApiFollowPathConstraint;
    case CONSTRAINT_TYPE_ROTLIKE:
      return &ApiCopyRotationConstraint;
    case CONSTRAINT_TYPE_LOCLIKE:
      return &ApiCopyLocationConstraint;
    case CONSTRAINT_TYPE_SIZELIKE:
      return &ApiCopyScaleConstraint;
    case CONSTRAINT_TYPE_SAMEVOL:
      return &ApiMaintainVolumeConstraint;
    case CONSTRAINT_TYPE_PYTHON:
      return &ApiPythonConstraint;
    case CONSTRAINT_TYPE_ARMATURE:
      return &ApiArmatureConstraint;
    case CONSTRAINT_TYPE_ACTION:
      return &ApiActionConstraint;
    case CONSTRAINT_TYPE_LOCKTRACK:
      return &ApiLockedTrackConstraint;
    case CONSTRAINT_TYPE_STRETCHTO:
      return &ApiStretchToConstraint;
    case CONSTRAINT_TYPE_MINMAX:
      return &ApiFloorConstraint;
    case CONSTRAINT_TYPE_CLAMPTO:
      return &ApiClampToConstraint;
    case CONSTRAINT_TYPE_TRANSFORM:
      return &ApiTransformConstraint;
    case CONSTRAINT_TYPE_ROTLIMIT:
      return &ApiLimitRotationConstraint;
    case CONSTRAINT_TYPE_LOCLIMIT:
      return &ApiLimitLocationConstraint;
    case CONSTRAINT_TYPE_SIZELIMIT:
      return &ApiLimitScaleConstraint;
    case CONSTRAINT_TYPE_DISTLIMIT:
      return &ApiLimitDistanceConstraint;
    case CONSTRAINT_TYPE_SHRINKWRAP:
      return &ApiShrinkwrapConstraint;
    case CONSTRAINT_TYPE_DAMPTRACK:
      return &ApiDampedTrackConstraint;
    case CONSTRAINT_TYPE_SPLINEIK:
      return &ApiSplineIKConstraint;
    case CONSTRAINT_TYPE_TRANSLIKE:
      return &ApiCopyTransformsConstraint;
    case CONSTRAINT_TYPE_PIVOT:
      return &ApiPivotConstraint;
    case CONSTRAINT_TYPE_FOLLOWTRACK:
      return &ApiFollowTrackConstraint;
    case CONSTRAINT_TYPE_CAMERASOLVER:
      return &ApiCameraSolverConstraint;
    case CONSTRAINT_TYPE_OBJECTSOLVER:
      return &RNA_ObjectSolverConstraint;
    case CONSTRAINT_TYPE_TRANSFORM_CACHE:
      return &RNA_TransformCacheConstraint;
    default:
      return &RNA_UnknownType;
  }
}

static void rna_ConstraintTargetBone_target_set(PointerRNA *ptr,
                                                PointerRNA value,
                                                struct ReportList *UNUSED(reports))
{
  bConstraintTarget *tgt = (bConstraintTarget *)ptr->data;
  Object *ob = value.data;

  if (!ob || ob->type == OB_ARMATURE) {
    id_lib_extern((ID *)ob);
    tgt->tar = ob;
  }
}

static void rna_Constraint_name_set(PointerRNA *ptr, const char *value)
{
  bConstraint *con = ptr->data;
  char oldname[sizeof(con->name)];

  /* make a copy of the old name first */
  BLI_strncpy(oldname, con->name, sizeof(con->name));

  /* copy the new name into the name slot */
  BLI_strncpy_utf8(con->name, value, sizeof(con->name));

  /* make sure name is unique */
  if (ptr->owner_id) {
    Object *ob = (Object *)ptr->owner_id;
    ListBase *list = ED_object_constraint_list_from_constraint(ob, con, NULL);

    /* if we have the list, check for unique name, otherwise give up */
    if (list) {
      BKE_constraint_unique_name(con, list);
    }
  }

  /* fix all the animation data which may link to this */
  BKE_animdata_fix_paths_rename_all(NULL, "constraints", oldname, con->name);
}

static char *rna_Constraint_do_compute_path(Object *ob, bConstraint *con)
{
  bPoseChannel *pchan;
  ListBase *lb = ED_object_constraint_list_from_constraint(ob, con, &pchan);

  if (lb == NULL) {
    printf("%s: internal error, constraint '%s' not found in object '%s'\n",
           __func__,
           con->name,
           ob->id.name);
  }

  if (pchan) {
    char name_esc_pchan[sizeof(pchan->name) * 2];
    char name_esc_const[sizeof(con->name) * 2];
    BLI_str_escape(name_esc_pchan, pchan->name, sizeof(name_esc_pchan));
    BLI_str_escape(name_esc_const, con->name, sizeof(name_esc_const));
    return BLI_sprintfN("pose.bones[\"%s\"].constraints[\"%s\"]", name_esc_pchan, name_esc_const);
  }
  else {
    char name_esc_const[sizeof(con->name) * 2];
    BLI_str_escape(name_esc_const, con->name, sizeof(name_esc_const));
    return BLI_sprintfN("constraints[\"%s\"]", name_esc_const);
  }
}

static char *rna_Constraint_path(PointerRNA *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  bConstraint *con = ptr->data;

  return rna_Constraint_do_compute_path(ob, con);
}

static bConstraint *rna_constraint_from_target(PointerRNA *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  bConstraintTarget *tgt = ptr->data;

  return BKE_constraint_find_from_target(ob, tgt, NULL);
}

static char *rna_ConstraintTarget_path(PointerRNA *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  bConstraintTarget *tgt = ptr->data;
  bConstraint *con = rna_constraint_from_target(ptr);
  int index = -1;

  if (con != NULL) {
    if (con->type == CONSTRAINT_TYPE_ARMATURE) {
      bArmatureConstraint *acon = con->data;
      index = BLI_findindex(&acon->targets, tgt);
    }
    else if (con->type == CONSTRAINT_TYPE_PYTHON) {
      bPythonConstraint *pcon = con->data;
      index = BLI_findindex(&pcon->targets, tgt);
    }
  }

  if (index >= 0) {
    char *con_path = rna_Constraint_do_compute_path(ob, con);
    char *result = BLI_sprintfN("%s.targets[%d]", con_path, index);

    MEM_freeN(con_path);
    return result;
  }
  else {
    printf("%s: internal error, constraint '%s' of object '%s' does not contain the target\n",
           __func__,
           con->name,
           ob->id.name);
  }

  return NULL;
}

static void rna_Constraint_update(Main *bmain, Scene *UNUSED(scene), PointerRNA *ptr)
{
  ED_object_constraint_tag_update(bmain, (Object *)ptr->owner_id, ptr->data);
}

static void rna_Constraint_dependency_update(Main *bmain, Scene *UNUSED(scene), PointerRNA *ptr)
{
  ED_object_constraint_dependency_tag_update(bmain, (Object *)ptr->owner_id, ptr->data);
}

static void rna_ConstraintTarget_update(Main *bmain, Scene *UNUSED(scene), PointerRNA *ptr)
{
  ED_object_constraint_tag_update(bmain, (Object *)ptr->owner_id, rna_constraint_from_target(ptr));
}

static void rna_ConstraintTarget_dependency_update(Main *bmain,
                                                   Scene *UNUSED(scene),
                                                   PointerRNA *ptr)
{
  ED_object_constraint_dependency_tag_update(
      bmain, (Object *)ptr->owner_id, rna_constraint_from_target(ptr));
}

static void rna_Constraint_influence_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  Object *ob = (Object *)ptr->owner_id;

  if (ob->pose) {
    ob->pose->flag |= (POSE_LOCKED | POSE_DO_UNLOCK);
  }

  rna_Constraint_update(bmain, scene, ptr);
}

/* Update only needed so this isn't overwritten on first evaluation. */
static void rna_Constraint_childof_inverse_matrix_update(Main *bmain,
                                                         Scene *scene,
                                                         PointerRNA *ptr)
{
  bConstraint *con = ptr->data;
  bChildOfConstraint *data = con->data;
  data->flag &= ~CHILDOF_SET_INVERSE;
  rna_Constraint_update(bmain, scene, ptr);
}

static void rna_Constraint_ik_type_set(struct PointerRNA *ptr, int value)
{
  bConstraint *con = ptr->data;
  bKinematicConstraint *ikdata = con->data;

  if (ikdata->type != value) {
    /* the type of IK constraint has changed, set suitable default values */
    /* in case constraints reuse same fields incompatible */
    switch (value) {
      case CONSTRAINT_IK_COPYPOSE:
        break;
      case CONSTRAINT_IK_DISTANCE:
        break;
    }
    ikdata->type = value;
  }
}

/* DEPRECATED: use_offset replaced with mix_mode */
static bool rna_Constraint_RotLike_use_offset_get(struct PointerRNA *ptr)
{
  bConstraint *con = ptr->data;
  bRotateLikeConstraint *rotlike = con->data;
  return rotlike->mix_mode != ROTLIKE_MIX_REPLACE;
}

static void rna_Constraint_RotLike_use_offset_set(struct PointerRNA *ptr, bool value)
{
  bConstraint *con = ptr->data;
  bRotateLikeConstraint *rotlike = con->data;
  bool curval = (rotlike->mix_mode != ROTLIKE_MIX_REPLACE);
  if (curval != value) {
    rotlike->mix_mode = (value ? ROTLIKE_MIX_OFFSET : ROTLIKE_MIX_REPLACE);
  }
}

static const EnumPropertyItem *rna_Constraint_owner_space_itemf(bContext *UNUSED(C),
                                                                PointerRNA *ptr,
                                                                PropertyRNA *UNUSED(prop),
                                                                bool *UNUSED(r_free))
{
  Object *ob = (Object *)ptr->owner_id;
  bConstraint *con = (bConstraint *)ptr->data;

  if (BLI_findindex(&ob->constraints, con) == -1) {
    return owner_space_pchan_items;
  }
  else {
    /* object */
    return space_object_items;
  }
}

static const EnumPropertyItem *rna_Constraint_target_space_itemf(bContext *UNUSED(C),
                                                                 PointerRNA *ptr,
                                                                 PropertyRNA *UNUSED(prop),
                                                                 bool *UNUSED(r_free))
{
  bConstraint *con = (bConstraint *)ptr->data;
  const bConstraintTypeInfo *cti = BKE_constraint_typeinfo_get(con);
  ListBase targets = {NULL, NULL};
  bConstraintTarget *ct;

  if (cti && cti->get_constraint_targets) {
    cti->get_constraint_targets(con, &targets);

    for (ct = targets.first; ct; ct = ct->next) {
      if (ct->tar && ct->tar->type == OB_ARMATURE) {
        break;
      }
    }

    if (cti->flush_constraint_targets) {
      cti->flush_constraint_targets(con, &targets, 1);
    }

    if (ct) {
      return target_space_pchan_items;
    }
  }

  return space_object_items;
}

static bConstraintTarget *rna_ArmatureConstraint_target_new(ID *id, bConstraint *con, Main *bmain)
{
  bArmatureConstraint *acon = con->data;
  bConstraintTarget *tgt = MEM_callocN(sizeof(bConstraintTarget), "Constraint Target");

  tgt->weight = 1.0f;
  BLI_addtail(&acon->targets, tgt);

  ED_object_constraint_dependency_tag_update(bmain, (Object *)id, con);
  return tgt;
}

static void rna_ArmatureConstraint_target_remove(
    ID *id, bConstraint *con, Main *bmain, ReportList *reports, PointerRNA *target_ptr)
{
  bArmatureConstraint *acon = con->data;
  bConstraintTarget *tgt = target_ptr->data;

  if (BLI_findindex(&acon->targets, tgt) < 0) {
    BKE_report(reports, RPT_ERROR, "Target is not in the constraint target list");
    return;
  }

  BLI_freelinkN(&acon->targets, tgt);

  ED_object_constraint_dependency_tag_update(bmain, (Object *)id, con);
}

static void rna_ArmatureConstraint_target_clear(ID *id, bConstraint *con, Main *bmain)
{
  bArmatureConstraint *acon = con->data;

  BLI_freelistN(&acon->targets);

  ED_object_constraint_dependency_tag_update(bmain, (Object *)id, con);
}

static void rna_ActionConstraint_mix_mode_set(PointerRNA *ptr, int value)
{
  bConstraint *con = (bConstraint *)ptr->data;
  bActionConstraint *acon = (bActionConstraint *)con->data;

  acon->mix_mode = value;

  /* The After mode can be computed in world space for efficiency
   * and backward compatibility, while Before or Split requires Local. */
  if (ELEM(value, ACTCON_MIX_AFTER, ACTCON_MIX_AFTER_FULL)) {
    con->ownspace = CONSTRAINT_SPACE_WORLD;
  }
  else {
    con->ownspace = CONSTRAINT_SPACE_LOCAL;
  }
}

static void rna_ActionConstraint_minmax_range(
    PointerRNA *ptr, float *min, float *max, float *UNUSED(softmin), float *UNUSED(softmax))
{
  bConstraint *con = (bConstraint *)ptr->data;
  bActionConstraint *acon = (bActionConstraint *)con->data;

  /* 0, 1, 2 = magic numbers for rotX, rotY, rotZ */
  if (ELEM(acon->type, 0, 1, 2)) {
    *min = -180.0f;
    *max = 180.0f;
  }
  else {
    *min = -1000.0f;
    *max = 1000.0f;
  }
}

static int rna_SplineIKConstraint_joint_bindings_get_length(PointerRNA *ptr,
                                                            int length[RNA_MAX_ARRAY_DIMENSION])
{
  bConstraint *con = (bConstraint *)ptr->data;
  bSplineIKConstraint *ikData = (bSplineIKConstraint *)con->data;

  if (ikData) {
    length[0] = ikData->numpoints;
  }
  else {
    length[0] = 0;
  }

  return length[0];
}

static void rna_SplineIKConstraint_joint_bindings_get(PointerRNA *ptr, float *values)
{
  bConstraint *con = (bConstraint *)ptr->data;
  bSplineIKConstraint *ikData = (bSplineIKConstraint *)con->data;

  memcpy(values, ikData->points, ikData->numpoints * sizeof(float));
}

static void rna_SplineIKConstraint_joint_bindings_set(PointerRNA *ptr, const float *values)
{
  bConstraint *con = (bConstraint *)ptr->data;
  bSplineIKConstraint *ikData = (bSplineIKConstraint *)con->data;

  memcpy(ikData->points, values, ikData->numpoints * sizeof(float));
}

static int rna_ShrinkwrapConstraint_face_cull_get(PointerRNA *ptr)
{
  bConstraint *con = (bConstraint *)ptr->data;
  bShrinkwrapConstraint *swc = (bShrinkwrapConstraint *)con->data;
  return swc->flag & CON_SHRINKWRAP_PROJECT_CULL_MASK;
}

static void rna_ShrinkwrapConstraint_face_cull_set(struct PointerRNA *ptr, int value)
{
  bConstraint *con = (bConstraint *)ptr->data;
  bShrinkwrapConstraint *swc = (bShrinkwrapConstraint *)con->data;
  swc->flag = (swc->flag & ~CON_SHRINKWRAP_PROJECT_CULL_MASK) | value;
}

static bool rna_Constraint_cameraObject_poll(PointerRNA *ptr, PointerRNA value)
{
  Object *ob = (Object *)value.data;

  if (ob) {
    if (ob->type == OB_CAMERA && ob != (Object *)ptr->owner_id) {
      return 1;
    }
  }

  return 0;
}

static void rna_Constraint_followTrack_camera_set(PointerRNA *ptr,
                                                  PointerRNA value,
                                                  struct ReportList *UNUSED(reports))
{
  bConstraint *con = (bConstraint *)ptr->data;
  bFollowTrackConstraint *data = (bFollowTrackConstraint *)con->data;
  Object *ob = (Object *)value.data;

  if (ob) {
    if (ob->type == OB_CAMERA && ob != (Object *)ptr->owner_id) {
      data->camera = ob;
      id_lib_extern((ID *)ob);
    }
  }
  else {
    data->camera = NULL;
  }
}

static void rna_Constraint_followTrack_depthObject_set(PointerRNA *ptr,
                                                       PointerRNA value,
                                                       struct ReportList *UNUSED(reports))
{
  bConstraint *con = (bConstraint *)ptr->data;
  bFollowTrackConstraint *data = (bFollowTrackConstraint *)con->data;
  Object *ob = (Object *)value.data;

  if (ob) {
    if (ob->type == OB_MESH && ob != (Object *)ptr->owner_id) {
      data->depth_ob = ob;
      id_lib_extern((ID *)ob);
    }
  }
  else {
    data->depth_ob = NULL;
  }
}

static bool rna_Constraint_followTrack_depthObject_poll(PointerRNA *ptr, PointerRNA value)
{
  Object *ob = (Object *)value.data;

  if (ob) {
    if (ob->type == OB_MESH && ob != (Object *)ptr->owner_id) {
      return 1;
    }
  }

  return 0;
}

static void rna_Constraint_objectSolver_camera_set(PointerRNA *ptr,
                                                   PointerRNA value,
                                                   struct ReportList *UNUSED(reports))
{
  bConstraint *con = (bConstraint *)ptr->data;
  bObjectSolverConstraint *data = (bObjectSolverConstraint *)con->data;
  Object *ob = (Object *)value.data;

  if (ob) {
    if (ob->type == OB_CAMERA && ob != (Object *)ptr->owner_id) {
      data->camera = ob;
      id_lib_extern((ID *)ob);
    }
  }
  else {
    data->camera = NULL;
  }
}

#else

static const EnumPropertyItem constraint_distance_items[] = {
    {LIMITDIST_INSIDE,
     "LIMITDIST_INSIDE",
     0,
     "Inside",
     "The object is constrained inside a virtual sphere around the target object, "
     "with a radius defined by the limit distance"},
    {LIMITDIST_OUTSIDE,
     "LIMITDIST_OUTSIDE",
     0,
     "Outside",
     "The object is constrained outside a virtual sphere around the target object, "
     "with a radius defined by the limit distance"},
    {LIMITDIST_ONSURFACE,
     "LIMITDIST_ONSURFACE",
     0,
     "On Surface",
     "The object is constrained on the surface of a virtual sphere around the target object, "
     "with a radius defined by the limit distance"},
    {0, NULL, 0, NULL, NULL},
};
