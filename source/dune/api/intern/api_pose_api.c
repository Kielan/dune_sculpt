#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib_utildefines.h"

#include "api_define.h"

#include "types_object.h"

/* #include "lib_sys_types.h" */

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME

#  include "dune_animsys.h"
#  include "dune_armature.h"
#  include "dune_context.h"

#  include "types_action.h"
#  include "types_anim.h"

#  include "lib_ghash.h"

static float api_PoseBone_do_envelope(PoseChannel *chan, float vec[3])
{
  Bone *bone = chan->bone;

  float scale = (bone->flag & BONE_MULT_VG_ENV) == BONE_MULT_VG_ENV ? bone->weight : 1.0f;

  return distfactor_to_bone(vec,
                            chan->pose_head,
                            chan->pose_tail,
                            bone->rad_head * scale,
                            bone->rad_tail * scale,
                            bone->dist * scale);
}

static void api_PoseBone_bbone_segment_matrix(
    PoseChannel *pchan, ReportList *reports, float mat_ret[16], int index, bool rest)
{
  if (!pchan->bone || pchan->bone->segments <= 1) {
    dune_reportf(reports, RPT_ERROR, "Bone '%s' is not a B-Bone!", pchan->name);
    return;
  }
  if (pchan->runtime.bbone_segments != pchan->bone->segments) {
    BKE_reportf(reports, RPT_ERROR, "Bone '%s' has out of date B-Bone segment data!", pchan->name);
    return;
  }
  if (index < 0 || index > pchan->runtime.bbone_segments) {
    BKE_reportf(
        reports, RPT_ERROR, "Invalid index %d for B-Bone segments of '%s'!", index, pchan->name);
    return;
  }

  if (rest) {
    copy_m4_m4((float(*)[4])mat_ret, pchan->runtime.bbone_rest_mats[index].mat);
  }
  else {
    copy_m4_m4((float(*)[4])mat_ret, pchan->runtime.bbone_pose_mats[index].mat);
  }
}

static void api_PoseBone_compute_bbone_handles(PoseChannel *pchan,
                                               ReportList *reports,
                                               float ret_h1[3],
                                               float *ret_roll1,
                                               float ret_h2[3],
                                               float *ret_roll2,
                                               bool rest,
                                               bool ease,
                                               bool offsets)
{
  if (!pchan->bone || pchan->bone->segments <= 1) {
    dune_reportf(reports, RPT_ERROR, "Bone '%s' is not a B-Bone!", pchan->name);
    return;
  }

  BBoneSplineParams params;

  dune_pchan_bbone_spline_params_get(pchan, rest, &params);
  dune_pchan_bbone_handles_compute(
      &params, ret_h1, ret_roll1, ret_h2, ret_roll2, ease || offsets, offsets);
}

static void api_Pose_apply_pose_from_action(Id *pose_owner,
                                            Cxt *C,
                                            Action *action,
                                            const float evaluation_time)
{
  lib_assert(GS(pose_owner->name) == ID_OB);
  Object *pose_owner_ob = (Object *)pose_owner;

  AnimEvalCxt anim_eval_cxt = {cxt_data_graph_ptr(C), evaluation_time};
  dune_pose_apply_action_selected_bones(pose_owner_ob, action, &anim_eval_cxt);

  /* Do NOT tag with ID_RECALC_ANIMATION, as that would overwrite the just-applied pose. */
  graph_id_tag_update(pose_owner, ID_RECALC_GEOMETRY);
  wm_event_add_notifier(C, NC_OBJECT | ND_POSE, pose_owner);
}

#else

void api_pose(StructRNA *srna)
{
  FunctionRNA *func;
  PropertyRNA *parm;

  func = RNA_def_function(srna, "apply_pose_from_action", "rna_Pose_apply_pose_from_action");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_NO_SELF | FUNC_USE_CONTEXT);
  RNA_def_function_ui_description(
      func,
      "Apply the given action to this pose by evaluating it at a specific time. Only updates the "
      "pose of selected bones, or all bones if none are selected.");

  parm = api_def_ptr(fn, "action", "Action", "Action", "The Action containing the pose");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  parm = api_def_float(fn,
                       "evaluation_time",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "Evaluation Time",
                       "Time at which the given action is evaluated to obtain the pose",
                       -FLT_MAX,
                       FLT_MAX);
}

void api_api_pose_channel(ApiStruct *sapi)
{
  ApiProp *parm;
  ApiFn *fn;

  fn = api_def_fn(sapi, "evaluate_envelope", "api_PoseBone_do_envelope");
  api_def_fn_ui_description(fn, "Calculate bone envelope at given point");
  parm = api_def_float_vector_xyz(fn,
                                  "point",
                                  3,
                                  NULL,
                                  -FLT_MAX,
                                  FLT_MAX,
                                  "Point",
                                  "Position in 3d space to evaluate",
                                  -FLT_MAX,
                                  FLT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return value */
  parm = api_def_float(
      fn, "factor", 0, -FLT_MAX, FLT_MAX, "Factor", "Envelope factor", -FLT_MAX, FLT_MAX);
  api_def_fn_return(fn, parm);

  /* B-Bone segment matrices */
  fn = api_def_fn(sapi, "bbone_segment_matrix", "rna_PoseBone_bbone_segment_matrix");
  api_def_fn_ui_description(
      fn, "Retrieve the matrix of the joint between B-Bone segments if available");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_prop(fn, "matrix_return", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_multi_array(parm, 2, api_matrix_dimsize_4x4);
  api_def_prop_ui_text(parm, "", "The resulting matrix in bone local space");
  api_def_fn_output(fn, parm);
  parm = api_def_int(fn, "index", 0, 0, INT_MAX, "", "Index of the segment endpoint", 0, 10000);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn, "rest", false, "", "Return the rest pose matrix");

  /* B-Bone custom handle positions */
  fn = api_def_fn(sapi, "compute_bbone_handles", "api_PoseBone_compute_bbone_handles");
  api_def_fn_ui_description(
      fn, "Retrieve the vectors and rolls coming from B-Bone custom handles");
  api_def_fn_flag(fb, FN_USE_REPORTS);
  parm = api_def_prop(fn, "handle1", PROP_FLOAT, PROP_XYZ);
  api_def_prop_array(parm, 3);
  api_def_prop_ui_text(
      parm, "", "The direction vector of the start handle in bone local space");
  api_def_fn_output(fn, parm);
  parm = api_def_float(
      fn, "roll1", 0, -FLT_MAX, FLT_MAX, "", "Roll of the start handle", -FLT_MAX, FLT_MAX);
  api_def_fn_output(fn, parm);
  parm = api_def_prop(fn, "handle2", PROP_FLOAT, PROP_XYZ);
  api_def_prop_array(parm, 3);
  api_def_prop_ui_text(parm, "", "The direction vector of the end handle in bone local space");
  api_def_fn_output(fn, parm);
  parm = api_def_float(
      fn, "roll2", 0, -FLT_MAX, FLT_MAX, "", "Roll of the end handle", -FLT_MAX, FLT_MAX);
  api_def_fn_output(fn, parm);
  parm = api_def_bool(fn, "rest", false, "", "Return the rest pose state");
  parm = api_def_bool(fn, "ease", false, "", "Apply scale from ease values");
  parm = api_def_bool(
      fn, "offsets", false, "", "Apply roll and curve offsets from bone properties");
}

#endif
