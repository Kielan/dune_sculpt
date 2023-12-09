#include "types_anim.h"
#include "types_armature.h"
#include "types_constraint.h"
#include "types_ob.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_math_matrix.h"
#include "lib_math_rotation.h"
#include "lib_math_vector.h"
#include "lib_string_utils.hh"

#include "dune_action.h"
#include "dune_animsys.h"
#include "dune_appdir.h"
#include "dune_armature.hh"
#include "dune_copybuf.h"
#include "dune_cxt.hh"
#include "dune_deform.h"
#include "dune_idprop.h"
#include "dune_layer.h"
#include "dune_main.hh"
#include "dune_ob.hh"
#include "dune_report.h"

#include "graph.hh"
#include "graph_query.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_prototypes.h"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_keyframing.hh"
#include "ed_screen.hh"
#include "ed_util.hh"

#include "anim_bone_collections.hh"
#include "anim_keyframing.hh"

#include "ui.hh"
#include "ui_resources.hh"

#include "armature_intern.h"

/* Local Utils */
static void pose_copybuf_filepath_get(char filepath[FILE_MAX], size_t filepath_maxncpy)
{
  lib_path_join(filepath, filepath_maxncpy, dune_tmpdir_base(), "copybuf_pose.dune");
}

/* Apply Pose as Rest Pose */
/* helper for apply_armature_pose2bones - fixes parenting of obs
 * that are bone-parented to armature */
static void applyarmature_fix_boneparents(const Cxt *C, Scene *scene, Ob *armob)
{
  /* Graph is ensured to be eval at beginning of the op.
   * Must not eval graph here yet, this will ruin ob matrix which we want to
   * preserve after other changes has been done in the op.
   *
   * TODO: Is very similar to `ignore_parent_tx()`, which was now ensured to work
   * quite reliably. Can we dedup the code? Or at least verify we don't need an extra logic
   * in this fn. */
  Graph *graph = cxt_data_graph_ptr(C);
  Main *main = cxt_data_main(C);
  Ob workob, *ob;

  /* go through all objects in database */
  for (ob = static_cast<Ob *>(main->obs.first); ob;
       ob = static_cast<Ob *>(ob->id.next)) {
    /* if parent is bone in this armature, apply corrections */
    if ((ob->parent == armob) && (ob->partype == PARBONE)) {
      /* apply current transform from parent (not yet destroyed),
       * then calc new parent inverse matrix  */
      dune_ob_apply_mat4(ob, ob->ob_to_world, false, false);

      dune_ob_workob_calc_parent(graph, scene, ob, &workob);
      invert_m4_m4(ob->parentinv, workob.ob_to_world);
    }
  }
}

/* Sets the bone head, tail and roll to match the supplied params. */
static void applyarmature_set_edit_position(EditBone *curbone,
                                            const float pose_mat[4][4],
                                            const float new_tail[3],
                                            float r_new_arm_mat[4][4])
{
  /* Simply copy the head/tail values from pchan over to curbone. */
  copy_v3_v3(curbone->head, pose_mat[3]);
  copy_v3_v3(curbone->tail, new_tail);

  /* Fix roll:
   * 1. find auto-calcd roll val for this bone now
   * 2. remove this from the 'visual' y-rotation */
  {
    float premat[3][3], pmat[3][3];
    float delta[3];

    /* Obtain new auto y-rotation. */
    sub_v3_v3v3(delta, curbone->tail, curbone->head);

    copy_m3_m4(pmat, pose_mat);
    mat3_vec_to_roll(pmat, delta, &curbone->roll);

    /* Compute new rest pose matrix if requested. */
    if (r_new_arm_mat) {
      vec_roll_to_mat3(delta, curbone->roll, premat);
      copy_m4_m3(r_new_arm_mat, premat);
      copy_v3_v3(r_new_arm_mat[3], pose_mat[3]);
    }
  }
}

/* Copy props over from pchan to curbone and reset channels. */
static void applyarmature_transfer_props(EditBone *curbone,
                                         PoseChannel *pchan,
                                         const PoseChannel *pchan_eval)
{
  /* Combine pose and rest vals for bendy bone settings,
   * then clear the pchan vals (so we don't get a double-up). */
  if (pchan->bone->segments > 1) {
    /* Combine rest/pose vals. */
    curbone->curve_in_x += pchan_eval->curve_in_x;
    curbone->curve_in_z += pchan_eval->curve_in_z;
    curbone->curve_out_x += pchan_eval->curve_out_x;
    curbone->curve_out_z += pchan_eval->curve_out_z;
    curbone->roll1 += pchan_eval->roll1;
    curbone->roll2 += pchan_eval->roll2;
    curbone->ease1 += pchan_eval->ease1;
    curbone->ease2 += pchan_eval->ease2;

    mul_v3_v3(curbone->scale_in, pchan_eval->scale_in);
    mul_v3_v3(curbone->scale_out, pchan_eval->scale_out);

    /* Reset pose vals. */
    pchan->curve_in_x = pchan->curve_out_x = 0.0f;
    pchan->curve_in_z = pchan->curve_out_z = 0.0f;
    pchan->roll1 = pchan->roll2 = 0.0f;
    pchan->ease1 = pchan->ease2 = 0.0f;

    copy_v3_fl(pchan->scale_in, 1.0f);
    copy_v3_fl(pchan->scale_out, 1.0f);
  }

  /* Clear transform vals for pchan. */
  zero_v3(pchan->loc);
  zero_v3(pchan->eul);
  unit_qt(pchan->quat);
  unit_axis_angle(pchan->rotAxis, &pchan->rotAngle);
  pchan->size[0] = pchan->size[1] = pchan->size[2] = 1.0f;
}

/* Adjust the current edit position of the bone using the pose space matrix. */
static void applyarmature_adjust_edit_position(Armature *arm,
                                               PoseChannel *pchan,
                                               const float delta_mat[4][4],
                                               float r_new_arm_mat[4][4])
{
  EditBone *curbone = ed_armature_ebone_find_name(arm->edbo, pchan->name);
  float delta[3], new_tail[3], premat[3][3], new_pose[4][4];

  /* Current orientation matrix. */
  sub_v3_v3v3(delta, curbone->tail, curbone->head);
  vec_roll_to_mat3(delta, curbone->roll, premat);

  /* New location and orientation. */
  mul_m4_m4m3(new_pose, delta_mat, premat);
  mul_v3_m4v3(new_pose[3], delta_mat, curbone->head);
  mul_v3_m4v3(new_tail, delta_mat, curbone->tail);

  applyarmature_set_edit_position(curbone, new_pose, new_tail, r_new_arm_mat);
}

/* Data about parent position for Apply To Sel mode. */
struct ApplyArmatureParentState {
  Bone *bone;

  /* New rest position of the bone with scale included. */
  float new_rest_mat[4][4];
  /* New arm_mat of the bone == new_rest_mat wo scale. */
  float new_arm_mat[4][4];
};

/* Recursive walk for Apply To Sel mode; pstate nullptr unless child of an applied bone. */
static void applyarmature_process_sel_recursive(Armature *arm,
                                                Pose *pose,
                                                Pose *pose_eval,
                                                Bone *bone,
                                                List *sel,
                                                ApplyArmatureParentState *pstate)
{
  PoseChannel *pchan = dune_pose_channel_find_name(pose, bone->name);
  const PoseChannel *pchan_eval = dune_pose_channel_find_name(pose_eval, bone->name);

  if (!pchan || !pchan_eval) {
    return;
  }

  ApplyArmatureParentState new_pstate{};
  new_pstate.bone = bone;

  if (lib_findptr(sel, pchan, offsetof(CollectionPtrLink, ptr.data))) {
    /* SEL BONE: Snap to final pose transform minus un-applied parent effects.
     * I.e. bone position with accumulated parent effects but no local
     * transform will match the original final pose_mat.
     * Pose channels are reset as expected. */
    EditBone *curbone = ed_armature_ebone_find_name(arm->edbo, pchan->name);
    BoneParentTransform invparent;
    float new_tail[3];

    if (pchan->parent) {
      BoneParentTransform old_bpt, new_bpt;
      float offs_bone[4][4];

      /* Parent effects on the bone transform that have to be removed. */
      dune_bone_offset_matrix_get(bone, offs_bone);
      dune_bone_parent_transform_calc_from_matrices(bone->flag,
                                                   bone->inherit_scale_mode,
                                                   offs_bone,
                                                   bone->parent->arm_mat,
                                                   pchan_eval->parent->pose_mat,
                                                   &old_bpt);

      /* Applied parent effects that have to be kept, if any. */
      float(*new_parent_pose)[4] = pstate ? pstate->new_rest_mat : bone->parent->arm_mat;
      dune_bone_parent_transform_calc_from_matrices(bone->flag,
                                                   bone->inherit_scale_mode,
                                                   offs_bone,
                                                   bone->parent->arm_mat,
                                                   new_parent_pose,
                                                   &new_bpt);

      dune_bone_parent_transform_invert(&old_bpt);
      dune_bone_parent_transform_combine(&new_bpt, &old_bpt, &invparent);
    }
    else {
      dune_bone_parent_transform_clear(&invparent);
    }

    /* Apply change wo inherited unapplied parent transformations. */
    dune_bone_parent_transform_apply(&invparent, pchan_eval->pose_mat, new_pstate.new_rest_mat);

    copy_v3_fl3(new_tail, 0.0, bone->length, 0.0);
    mul_m4_v3(new_pstate.new_rest_mat, new_tail);

    applyarmature_set_edit_position(
        curbone, new_pstate.new_rest_mat, new_tail, new_pstate.new_arm_mat);
    applyarmature_transfer_props(curbone, pchan, pchan_eval);

    pstate = &new_pstate;
  }
  else if (pstate) {
    /* UNSEL CHILD OF SEL: Include applied parent effects.
     * The inherited transform of applied (sel) bones is baked
     * into the rest pose so that the final bone position doesn't
     * change.
     * Pose channels are not changed, with the exception of the inherited
     * applied parent scale being baked into the location pose channel. */
    BoneParentTransform bpt;
    float offs_bone[4][4], delta[4][4], old_chan_loc[3];

    /* Include applied parent effects. */
    dune_bone_offset_matrix_get(bone, offs_bone);
    dune_bone_parent_transform_calc_from_matrices(bone->flag,
                                                 bone->inherit_scale_mode,
                                                 offs_bone,
                                                 pstate->bone->arm_mat,
                                                 pstate->new_rest_mat,
                                                 &bpt);

    unit_m4(new_pstate.new_rest_mat);
    dune_bone_parent_transform_apply(&bpt, new_pstate.new_rest_mat, new_pstate.new_rest_mat);

    /* Bone location channel in pose space relative to bone head. */
    mul_v3_mat3_m4v3(old_chan_loc, bpt.loc_mat, pchan_eval->loc);

    /* Apply the change to the rest bone position. */
    invert_m4_m4(delta, bone->arm_mat);
    mul_m4_m4m4(delta, new_pstate.new_rest_mat, delta);

    applyarmature_adjust_edit_position(arm, pchan, delta, new_pstate.new_arm_mat);

    /* Location pose channel has to be updated, because it is affected
     * by parent scaling, and the rest pose has no scale by definition. */
    if (!(bone->flag & BONE_CONNECTED) && !is_zero_v3(old_chan_loc)) {
      float inv_parent_arm[4][4];

      /* Compute the channel coordinate space matrices for the new rest state. */
      invert_m4_m4(inv_parent_arm, pstate->new_arm_mat);
      mul_m4_m4m4(offs_bone, inv_parent_arm, new_pstate.new_arm_mat);
      dune_bone_parent_transform_calc_from_matrices(bone->flag,
                                                   bone->inherit_scale_mode,
                                                   offs_bone,
                                                   pstate->new_arm_mat,
                                                   pstate->new_arm_mat,
                                                   &bpt);

      /* Re-apply the location to keep the final effect. */
      invert_m4(bpt.loc_mat);
      mul_v3_mat3_m4v3(pchan->loc, bpt.loc_mat, old_chan_loc);
    }

    pstate = &new_pstate;
  }

  LIST_FOREACH (Bone *, child, &bone->childbase) {
    applyarmature_process_sel_recursive(arm, pose, pose_eval, child, sel, pstate);
  }
}

/* Reset bone constraint so that it is correct after the pose has been applied. */
static void applyarmature_reset_bone_constraint(const Constraint *constraint)
{
  /* TODO: This fn needs too much knowledge of the internals of specific constraints.
   * When it is extended w 1 or 2 more constraints, move the functionality into a
   * ConstraintTypeInfo cb fn. */
  switch (constraint->type) {
    case CONSTRAINT_TYPE_STRETCHTO: {
      StretchToConstraint *stretch_to = (StretchToConstraint *)constraint->data;
      stretch_to->orglength = 0.0f; /* Force recalc on next eval. */
      break;
    }
    default:
      /* Most constraints don't need resetting. */
      break;
  }
}

/* Reset bone constraints of the given pose channel so that they are correct after the pose has
 * been applied. */
static void applyarmature_reset_bone_constraints(const PoseChannel *pchan)
{
  LIST_FOREACH (Constraint *, constraint, &pchan->constraints) {
    applyarmature_reset_bone_constraint(constraint);
  }
}

/* Reset all (or only se) bone constraints so that they are correct after the pose has been
 * applied. */
static void applyarmature_reset_constraints(Pose *pose, const bool use_sel)
{
  LIST_FOREACH (PoseChannel *, pchan, &pose->chanbase) {
    lib_assert(pchan->bone != nullptr);
    if (use_sel && (pchan->bone->flag & BONE_SEL) == 0) {
      continue;
    }
    applyarmature_reset_bone_constraints(pchan);
  }
}

/* Set the current pose as the rest-pose. */
static int apply_armature_pose2bones_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Graph *graph = cxt_data_ensure_eval_graph(C);
  Scene *scene = cxt_data_scene(C);
  /* must be active ob, not edit-ob */
  Ob *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));
  const Ob *ob_eval = graph_get_eval_ob(graph, ob);
  Armature *arm = dune_armature_from_ob(ob);
  Pose *pose;
  List sel_bones;

  const bool use_sel = api_bool_get(op->ptr, "sel");

  /* don't check if editmode (should be done by caller) */
  if (ob->type != OB_ARMATURE) {
    return OP_CANCELLED;
  }
  if (dune_ob_obdata_is_libdata(ob)) {
    dune_report(op->reports, RPT_ERROR, "Cannot apply pose to lib-linked armature");
    return OP_CANCELLED;
  }

  /* helpful warnings... */
  /* TODO: add warnings to be careful about actions, applying deforms first, etc. */
  if (ob->adt && ob->adt->action) {
    dune_report(op->reports,
               RPT_WARNING,
               "Actions on this armature will be destroyed by this new rest pose as the "
               "transforms stored are relative to the old rest pose");
  }

  /* Find sel bones before switching to edit mode. */
  if (use_sel) {
    cxt_data_sel_pose_bones(C, &sel_bones);

    if (!sel_bones.first) {
      return OP_CANCELLED;
    }
  }

  /* Get edit-bones of active armature to alter. */
  ed_armature_to_edit(arm);

  /* Get pose of active ob and move it out of pose-mode. */
  pose = ob->pose;

  if (use_sel) {
    /* The sel only mode requires a recursive walk to handle parent-child relations. */
    LIST_FOREACH (Bone *, bone, &arm->bonebase) {
      applyarmature_process_sel_recursive(
          arm, pose, ob_eval->pose, bone, &sel_bones, nullptr);
    }

    lib_freelist(&sel_bones);
  }
  else {
    LIST_FOREACH (PoseChannel *, pchan, &pose->chanbase) {
      const PoseChannel *pchan_eval = dune_pose_channel_find_name(ob_eval->pose, pchan->name);
      EditBone *curbone = dune_armature_ebone_find_name(arm->edbo, pchan->name);

      applyarmature_set_edit_position(
          curbone, pchan_eval->pose_mat, pchan_eval->pose_tail, nullptr);
      applyarmature_transfer_properties(curbone, pchan, pchan_eval);
    }
  }

  /* Convert edit-bones back to bones, and then free the edit-data. */
  ed_armature_from_edit(main, arm);
  ed_armature_edit_free(arm);

  /* Flush positions of pose-bones. */
  dune_pose_where_is(graph, scene, ob);

  /* fix parenting of obs which are bone-parented */
  applyarmature_fix_boneparents(C, scene, ob);

  /* For the affected bones, reset specific constraints that are now known to be invalid. */
  applyarmature_reset_constraints(pose, use_sel);

  /* NOTE: notifier might evolve. */
  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
  graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);

  return OP_FINISHED;
}

static void apply_armature_pose2bones_ui(Cxt *C, WinOp *op)
{
  uiLayout *layout = op->layout;
  Win *win = cxt_win(C);

  ApiPtr ptr = api_ptr_create(&win->id, op->type->sapi, op->props);

  uiItemR(layout, &ptr, "sel", UI_ITEM_NONE, nullptr, ICON_NONE);
}

void POSE_OT_armature_apply(WinOpType *ot)
{
  /* ids */
  ot->name = "Apply Pose as Rest Pose";
  ot->idname = "POSE_OT_armature_apply";
  ot->description = "Apply the current pose as the new rest pose";

  /* cbs */
  ot->ex = apply_armature_pose2bones_ex;
  ot->poll = ed_op_posemode;
  ot->ui = apply_armature_pose2bones_ui;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->sapi,
               "sel",
               false,
               "Sel Only",
               "Only apply the sel bones (with propagation to children)");
}

/* Apply Visual Transform Op
 * Set the current pose as the rest-pose. */
static int pose_visual_transform_apply_ex(Cxt *C, WinOp * /*op*/)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  View3D *v3d = cxt_win_view3d(C);

  /* Needed to ensure PoseChannel.pose_mat are up to date. */
  cxt_data_ensure_eval_graph(C);

  FOREACH_OB_IN_MODE_BEGIN (scene, view_layer, v3d, OB_ARMATURE, OB_MODE_POSE, ob) {
    const Armature *arm = static_cast<const Armature *>(ob->data);

    int chanbase_len = lib_list_count(&ob->pose->chanbase);
    /* Storage for the calcd matrices to prevent reading from modded vals.
     * This could be avoided if children were always calc before parents
     * however ensuring this is involved and doesn't give any significant advantage. */
    struct XFormArray {
      float matrix[4][4];
      bool is_set;
    } *pchan_xform_array = static_cast<XFormArray *>(
        mem_malloc(sizeof(*pchan_xform_array) * chanbase_len, __func__));
    bool changed = false;

    int i;
    LIST_FOREACH_INDEX (PoseChannel *, pchan, &ob->pose->chanbase, i) {
      if (!((pchan->bone->flag & BONE_SEL) && PBONE_VISIBLE(arm, pchan->bone))) {
        pchan_xform_array[i].is_set = false;
        continue;
      }

      /* `chan_mat` alrdy contains the delta transform from rest pose to pose-mode pose
       * as that is baked into there so that B-Bones will work. Once we've set this as the
       * new raw-transform components, don't recalc the poses yet, otherwise IK result will
       * change, thus changing the result we may be trying to record. */

      /* For some reason `pchan->chan_mat` can't be used here as it gives odd
       * rotation/offset, see #38251.
       * Using `pchan->pose_mat` and bringing it back in bone space seems to work as expected!
       * This matches how visual key-framing works. */
      dune_armature_mat_pose_to_bone(pchan, pchan->pose_mat, pchan_xform_array[i].matrix);
      pchan_xform_array[i].is_set = true;
      changed = true;
    }

    if (changed) {
      /* Perform separately to prevent feedback loop. */
      LIST_FOREACH_INDEX (PoseChannel *, pchan, &ob->pose->chanbase, i) {
        if (!pchan_xform_array[i].is_set) {
          continue;
        }
        dune_pchan_apply_mat4(pchan, pchan_xform_array[i].matrix, true);
      }

      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

      /* Notifier might evolve. */
      win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
    }

    mem_free(pchan_xform_array);
  }
  FOREACH_OB_IN_MODE_END;

  return OP_FINISHED;
}

void POSE_OT_visual_transform_apply(WinOpType *ot)
{
  /* ids */
  ot->name = "Apply Visual Transform to Pose";
  ot->idname = "POSE_OT_visual_transform_apply";
  ot->description = "Apply final constrained position of pose bones to their transform";

  /* cbs */
  ot->ex = pose_visual_transform_apply_exec;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Copy/Paste Utils */

/* This fn is used to indicate that a bone is sel
 * and needs to be included in copy buf (used to be for inserting keys) */
static void set_pose_keys(Ob *ob)
{
  Armature *arm = static_cast<Armature *>(ob->data);

  if (ob->pose) {
    LIST_FOREACH (PoseChannel *, chan, &ob->pose->chanbase) {
      Bone *bone = chan->bone;
      if ((bone) && (bone->flag & BONE_SEL) && anim_bonecoll_is_visible(arm, bone)) {
        chan->flag |= POSE_KEY;
      }
      else {
        chan->flag &= ~POSE_KEY;
      }
    }
  }
}

/* Perform paste pose, for a single bone.
 * param ob: Ob where bone to paste to lives
 * param chan: Bone that pose to paste comes from
 * param selOnly: Only paste on sel bones
 * param flip: Flip on x-axis
 * return The channel of the bone that was pasted to, or nullptr if no paste was performed. */
static PoseChannel *pose_bone_do_paste(Ob *ob,
                                       PoseChannel *chan,
                                       const bool selOnly,
                                       const bool flip)
{
  char name[MAXBONENAME];

  /* get the name - if flipping, we must flip this first */
  if (flip) {
    lib_string_flip_side_name(name, chan->name, false, sizeof(name));
  }
  else {
    STRNCPY(name, chan->name);
  }

  /* only copy when:
   *  1) channel exists - poses are not meant to add random channels to anymore
   *  2) if sel-masking is on, channel is sel -
   *     only sel bones get pasted on, allowing making both sides symmetrical. */
  PoseChannel *pchan = dune_pose_channel_find_name(ob->pose, name);
  if (pchan == nullptr) {
    return nullptr;
  }
  if (selOnly && (pchan->bone->flag & BONE_SEL) == 0) {
    return nullptr;
  }

  /* only loc rot size
   * - only copies transform info for the pose */
  copy_v3_v3(pchan->loc, chan->loc);
  copy_v3_v3(pchan->size, chan->size);
  pchan->flag = chan->flag;

  /* check if rotation modes are compatible (i.e. do they need any conversions) */
  if (pchan->rotmode == chan->rotmode) {
    /* copy the type of rotation in use */
    if (pchan->rotmode > 0) {
      copy_v3_v3(pchan->eul, chan->eul);
    }
    else if (pchan->rotmode == ROT_MODE_AXISANGLE) {
      copy_v3_v3(pchan->rotAxis, chan->rotAxis);
      pchan->rotAngle = chan->rotAngle;
    }
    else {
      copy_qt_qt(pchan->quat, chan->quat);
    }
  }
  else if (pchan->rotmode > 0) {
    /* quat/axis-angle to euler */
    if (chan->rotmode == ROT_MODE_AXISANGLE) {
      axis_angle_to_eulO(pchan->eul, pchan->rotmode, chan->rotAxis, chan->rotAngle);
    }
    else {
      quat_to_eulO(pchan->eul, pchan->rotmode, chan->quat);
    }
  }
  else if (pchan->rotmode == ROT_MODE_AXISANGLE) {
    /* quat/euler to axis angle */
    if (chan->rotmode > 0) {
      eulO_to_axis_angle(pchan->rotAxis, &pchan->rotAngle, chan->eul, chan->rotmode);
    }
    else {
      quat_to_axis_angle(pchan->rotAxis, &pchan->rotAngle, chan->quat);
    }
  }
  else {
    /* euler/axis-angle to quat */
    if (chan->rotmode > 0) {
      eulO_to_quat(pchan->quat, chan->eul, chan->rotmode);
    }
    else {
      axis_angle_to_quat(pchan->quat, chan->rotAxis, pchan->rotAngle);
    }
  }

  /* B-Bone posing options should also be included... */
  pchan->curve_in_x = chan->curve_in_x;
  pchan->curve_in_z = chan->curve_in_z;
  pchan->curve_out_x = chan->curve_out_x;
  pchan->curve_out_z = chan->curve_out_z;

  pchan->roll1 = chan->roll1;
  pchan->roll2 = chan->roll2;
  pchan->ease1 = chan->ease1;
  pchan->ease2 = chan->ease2;

  copy_v3_v3(pchan->scale_in, chan->scale_in);
  copy_v3_v3(pchan->scale_out, chan->scale_out);

  /* paste flipped pose? */
  if (flip) {
    pchan->loc[0] *= -1;

    pchan->curve_in_x *= -1;
    pchan->curve_out_x *= -1;
    pchan->roll1 *= -1; /* XXX? */
    pchan->roll2 *= -1; /* XXX? */

    /* has to be done as eulers... */
    if (pchan->rotmode > 0) {
      pchan->eul[1] *= -1;
      pchan->eul[2] *= -1;
    }
    else if (pchan->rotmode == ROT_MODE_AXISANGLE) {
      float eul[3];

      axis_angle_to_eulO(eul, EULER_ORDER_DEFAULT, pchan->rotAxis, pchan->rotAngle);
      eul[1] *= -1;
      eul[2] *= -1;
      eulO_to_axis_angle(pchan->rotAxis, &pchan->rotAngle, eul, EULER_ORDER_DEFAULT);
    }
    else {
      float eul[3];

      normalize_qt(pchan->quat);
      quat_to_eul(eul, pchan->quat);
      eul[1] *= -1;
      eul[2] *= -1;
      eul_to_quat(pchan->quat, eul);
    }
  }

  /* Id props */
  if (chan->prop) {
    if (pchan->prop) {
      /* if we have existing props on a bone, just copy over the vals of
       * matching props (i.e. ones which will have some impact) on to the
       * target instead of just blinding replacing all [ */
      IDP_SyncGroupVals(pchan->prop, chan->prop);
    }
    else {
      /* no existing props, so assume that we want copies too? */
      pchan->prop = IDP_CopyProp(chan->prop);
    }
  }

  return pchan;
}

/* Copy Pose Op */
static int pose_copy_ex(Cxt *C, WinOp *op)
{
  Ob *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));
  char filepath[FILE_MAX];
  /* Sanity checking. */
  if (ELEM(nullptr, ob, ob->pose)) {
    dune_report(op->reports, RPT_ERROR, "No pose to copy");
    return OP_CANCELLED;
  }
  /* Sets chan->flag to POSE_KEY if bone sel. */
  set_pose_keys(ob);
  /* Construct a local main and only put ob and its data into it,
   * o this way we don't expand any other obs into the copy buf
   * file.
   *
   * TODO: Find an easier way to tell copy buf to only store
   * data we are actually interested in. Maybe pass it a flag to skip
   * any datablock expansion? */
  Main *tmp_main = dune_main_new();
  STRNCPY(tmp_main->filepath, dune_main_dunefile_path_from_global());

  Ob ob_copy = dune::types::shallow_copy(*ob);
  ob_copy.adt = nullptr;

  /* Copy the armature wo using the default copy constructor. This prevents
   * the compiler from complaining that the `layer`, `layer_used`, and
   * `layer_protected` fields are TYPES_DEPRECATED. */
  Armature arm_copy;
  memcpy(&arm_copy, ob->data, sizeof(arm_copy));

  arm_copy.adt = nullptr;
  ob_copy.data = &arm_copy;
  lib_addtail(&tmp_main->obs, &ob_copy);
  lib_addtail(&tmp_main->armatures, &arm_copy);
  /* begin copy buf on a tmp main. */
  dune_copybuf_copy_begin(tmp_main);
  /* Store the whole ob to the copy buf bc pose can't be
   * existing on its own */
  dune_copybuf_copy_tag_id(&ob_copy.id);

  pose_copybuf_filepath_get(filepath, sizeof(filepath));
  dune_copybuf_copy_end(tmp_main, filepath, op->reports);
  /* We clear the lists so no datablocks gets freed,
   * This is required bc obs in tmp main shares same ptr
   * as the real ones */
  lib_list_clear(&tmp_main->obs);
  lib_list_clear(&tmp_main->armatures);
  dune_main_free(tmp_main);
  /* We are all done! */
  dune_report(op->reports, RPT_INFO, "Copied pose to internal clipboard");
  return OP_FINISHED;
}

void POSE_OT_copy(WinOpType *ot)
{
  /* ids */
  ot->name = "Copy Pose";
  ot->idname = "POSE_OT_copy";
  ot->description = "Copy the current pose of the selected bones to the internal clipboard";

  /* api cbs */
  ot->ex = pose_copy_ex;
  ot->poll = ed_op_posemode;

  /* flag */
  ot->flag = OPTYPE_REGISTER;
}

/* Paste Pose Op */
static int pose_paste_ex(Cxt *C, wmOperator *op)
{
  Object *ob = dune_ob_pose_armature_get(CTX_data_active_object(C));
  Scene *scene = cxt_data_scene(C);
  const bool flip = RNA_boolean_get(op->ptr, "flipped");
  bool selOnly = RNA_boolean_get(op->ptr, "selected_mask");

  /* Get KeyingSet to use. */
  KeyingSet *ks = ANIM_get_keyingset_for_autokeying(scene, ANIM_KS_WHOLE_CHARACTER_ID);

  /* Sanity checks. */
  if (ELEM(nullptr, ob, ob->pose)) {
    return OPERATOR_CANCELLED;
  }

  /* Read copy buffer .blend file. */
  char filepath[FILE_MAX];
  Main *temp_bmain = BKE_main_new();
  STRNCPY(temp_bmain->filepath, BKE_main_blendfile_path_from_global());

  pose_copybuffer_filepath_get(filepath, sizeof(filepath));
  if (!BKE_copybuffer_read(temp_bmain, filepath, op->reports, FILTER_ID_OB)) {
    BKE_report(op->reports, RPT_ERROR, "Internal clipboard is empty");
    BKE_main_free(temp_bmain);
    return OPERATOR_CANCELLED;
  }
  /* Make sure data from this file is usable for pose paste. */
  if (!BLI_listbase_is_single(&temp_bmain->objects)) {
    BKE_report(op->reports, RPT_ERROR, "Internal clipboard is not from pose mode");
    BKE_main_free(temp_bmain);
    return OPERATOR_CANCELLED;
  }

  Object *object_from = static_cast<Object *>(temp_bmain->objects.first);
  bPose *pose_from = object_from->pose;
  if (pose_from == nullptr) {
    BKE_report(op->reports, RPT_ERROR, "Internal clipboard has no pose");
    BKE_main_free(temp_bmain);
    return OPERATOR_CANCELLED;
  }

  /* If selOnly option is enabled, if user hasn't selected any bones,
   * just go back to default behavior to be more in line with other
   * pose tools.
   */
  if (selOnly) {
    if (CTX_DATA_COUNT(C, selected_pose_bones) == 0) {
      selOnly = false;
    }
  }

  /* Safely merge all of the channels in the buffer pose into any
   * existing pose.
   */
  LISTBASE_FOREACH (bPoseChannel *, chan, &pose_from->chanbase) {
    if (chan->flag & POSE_KEY) {
      /* Try to perform paste on this bone. */
      bPoseChannel *pchan = pose_bone_do_paste(ob, chan, selOnly, flip);
      if (pchan != nullptr) {
        /* Keyframing tagging for successful paste, */
        blender::animrig::autokeyframe_pchan(C, scene, ob, pchan, ks);
      }
    }
  }
  BKE_main_free(temp_bmain);

  /* Update event for pose and deformation children. */
  DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

  /* Recalculate paths if any of the bones have paths... */
  if (ob->pose->avs.path_bakeflag & MOTIONPATH_BAKE_HAS_PATHS) {
    ED_pose_recalculate_paths(C, scene, ob, POSE_PATH_CALC_RANGE_FULL);
  }

  /* Notifiers for updates, */
  WM_event_add_notifier(C, NC_OBJECT | ND_POSE, ob);

  return OPERATOR_FINISHED;
}

void POSE_OT_paste(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Paste Pose";
  ot->idname = "POSE_OT_paste";
  ot->description = "Paste the stored pose on to the current pose";

  /* api callbacks */
  ot->exec = pose_paste_exec;
  ot->poll = ED_operator_posemode;

  /* flag */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  prop = RNA_def_boolean(ot->srna,
                         "flipped",
                         false,
                         "Flipped on X-Axis",
                         "Paste the stored pose flipped on to current pose");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);

  RNA_def_boolean(ot->srna,
                  "selected_mask",
                  false,
                  "On Selected Only",
                  "Only paste the stored pose on to selected bones in the current pose");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Pose Transforms Utilities
 * \{ */

/* clear scale of pose-channel */
static void pchan_clear_scale(bPoseChannel *pchan)
{
  if ((pchan->protectflag & OB_LOCK_SCALEX) == 0) {
    pchan->size[0] = 1.0f;
  }
  if ((pchan->protectflag & OB_LOCK_SCALEY) == 0) {
    pchan->size[1] = 1.0f;
  }
  if ((pchan->protectflag & OB_LOCK_SCALEZ) == 0) {
    pchan->size[2] = 1.0f;
  }

  pchan->ease1 = 0.0f;
  pchan->ease2 = 0.0f;

  copy_v3_fl(pchan->scale_in, 1.0f);
  copy_v3_fl(pchan->scale_out, 1.0f);
}
/* Clear the scale. When X-mirror is enabled,
 * also clear the scale of the mirrored pose channel. */
static void pchan_clear_scale_with_mirrored(const bPose *pose, bPoseChannel *pchan)
{
  if (pose->flag & POSE_MIRROR_EDIT) {
    bPoseChannel *pchan_mirror = BKE_pose_channel_get_mirrored(pose, pchan->name);
    if (pchan_mirror != nullptr) {
      pchan_clear_scale(pchan_mirror);
    }
  }
  pchan_clear_scale(pchan);
}

/* clear location of pose-channel */
static void pchan_clear_loc(bPoseChannel *pchan)
{
  if ((pchan->protectflag & OB_LOCK_LOCX) == 0) {
    pchan->loc[0] = 0.0f;
  }
  if ((pchan->protectflag & OB_LOCK_LOCY) == 0) {
    pchan->loc[1] = 0.0f;
  }
  if ((pchan->protectflag & OB_LOCK_LOCZ) == 0) {
    pchan->loc[2] = 0.0f;
  }
}
/* Clear the Location. When X-mirror is enabled,
 * also clear the location of the mirrored pose channel. */
static void pchan_clear_loc_with_mirrored(const bPose *pose, bPoseChannel *pchan)
{
  if (pose->flag & POSE_MIRROR_EDIT) {
    bPoseChannel *pchan_mirror = BKE_pose_channel_get_mirrored(pose, pchan->name);
    if (pchan_mirror != nullptr) {
      pchan_clear_loc(pchan_mirror);
    }
  }
  pchan_clear_loc(pchan);
}

/* clear rotation of pose-channel */
static void pchan_clear_rot(bPoseChannel *pchan)
{
  if (pchan->protectflag & (OB_LOCK_ROTX | OB_LOCK_ROTY | OB_LOCK_ROTZ | OB_LOCK_ROTW)) {
    /* check if convert to eulers for locking... */
    if (pchan->protectflag & OB_LOCK_ROT4D) {
      /* perform clamping on a component by component basis */
      if (pchan->rotmode == ROT_MODE_AXISANGLE) {
        if ((pchan->protectflag & OB_LOCK_ROTW) == 0) {
          pchan->rotAngle = 0.0f;
        }
        if ((pchan->protectflag & OB_LOCK_ROTX) == 0) {
          pchan->rotAxis[0] = 0.0f;
        }
        if ((pchan->protectflag & OB_LOCK_ROTY) == 0) {
          pchan->rotAxis[1] = 0.0f;
        }
        if ((pchan->protectflag & OB_LOCK_ROTZ) == 0) {
          pchan->rotAxis[2] = 0.0f;
        }

        /* check validity of axis - axis should never be 0,0,0
         * (if so, then we make it rotate about y). */
        if (IS_EQF(pchan->rotAxis[0], pchan->rotAxis[1]) &&
            IS_EQF(pchan->rotAxis[1], pchan->rotAxis[2])) {
          pchan->rotAxis[1] = 1.0f;
        }
      }
      else if (pchan->rotmode == ROT_MODE_QUAT) {
        if ((pchan->protectflag & OB_LOCK_ROTW) == 0) {
          pchan->quat[0] = 1.0f;
        }
        if ((pchan->protectflag & OB_LOCK_ROTX) == 0) {
          pchan->quat[1] = 0.0f;
        }
        if ((pchan->protectflag & OB_LOCK_ROTY) == 0) {
          pchan->quat[2] = 0.0f;
        }
        if ((pchan->protectflag & OB_LOCK_ROTZ) == 0) {
          pchan->quat[3] = 0.0f;
        }
      }
      else {
        /* the flag may have been set for the other modes, so just ignore the extra flag... */
        if ((pchan->protectflag & OB_LOCK_ROTX) == 0) {
          pchan->eul[0] = 0.0f;
        }
        if ((pchan->protectflag & OB_LOCK_ROTY) == 0) {
          pchan->eul[1] = 0.0f;
        }
        if ((pchan->protectflag & OB_LOCK_ROTZ) == 0) {
          pchan->eul[2] = 0.0f;
        }
      }
    }
    else {
      /* perform clamping using euler form (3-components) */
      float eul[3], oldeul[3], quat1[4] = {0};
      float qlen = 0.0f;

      if (pchan->rotmode == ROT_MODE_QUAT) {
        qlen = normalize_qt_qt(quat1, pchan->quat);
        quat_to_eul(oldeul, quat1);
      }
      else if (pchan->rotmode == ROT_MODE_AXISANGLE) {
        axis_angle_to_eulO(oldeul, EULER_ORDER_DEFAULT, pchan->rotAxis, pchan->rotAngle);
      }
      else {
        copy_v3_v3(oldeul, pchan->eul);
      }

      eul[0] = eul[1] = eul[2] = 0.0f;

      if (pchan->protectflag & OB_LOCK_ROTX) {
        eul[0] = oldeul[0];
      }
      if (pchan->protectflag & OB_LOCK_ROTY) {
        eul[1] = oldeul[1];
      }
      if (pchan->protectflag & OB_LOCK_ROTZ) {
        eul[2] = oldeul[2];
      }

      if (pchan->rotmode == ROT_MODE_QUAT) {
        eul_to_quat(pchan->quat, eul);

        /* restore original quat size */
        mul_qt_fl(pchan->quat, qlen);

        /* quaternions flip w sign to accumulate rotations correctly */
        if ((quat1[0] < 0.0f && pchan->quat[0] > 0.0f) ||
            (quat1[0] > 0.0f && pchan->quat[0] < 0.0f)) {
          mul_qt_fl(pchan->quat, -1.0f);
        }
      }
      else if (pchan->rotmode == ROT_MODE_AXISANGLE) {
        eulO_to_axis_angle(pchan->rotAxis, &pchan->rotAngle, eul, EULER_ORDER_DEFAULT);
      }
      else {
        copy_v3_v3(pchan->eul, eul);
      }
    }
  } /* Duplicated in source/blender/editors/object/object_transform.cc */
  else {
    if (pchan->rotmode == ROT_MODE_QUAT) {
      unit_qt(pchan->quat);
    }
    else if (pchan->rotmode == ROT_MODE_AXISANGLE) {
      /* by default, make rotation of 0 radians around y-axis (roll) */
      unit_axis_angle(pchan->rotAxis, &pchan->rotAngle);
    }
    else {
      zero_v3(pchan->eul);
    }
  }

  /* Clear also Bendy Bone stuff - Roll is obvious,
   * but Curve X/Y stuff is also kind of rotational in nature... */
  pchan->roll1 = 0.0f;
  pchan->roll2 = 0.0f;

  pchan->curve_in_x = 0.0f;
  pchan->curve_in_z = 0.0f;
  pchan->curve_out_x = 0.0f;
  pchan->curve_out_z = 0.0f;
}
/* Clear the rotation. When X-mirror is enabled,
 * also clear the rotation of the mirrored pose channel. */
static void pchan_clear_rot_with_mirrored(const bPose *pose, bPoseChannel *pchan)
{
  if (pose->flag & POSE_MIRROR_EDIT) {
    bPoseChannel *pchan_mirror = BKE_pose_channel_get_mirrored(pose, pchan->name);
    if (pchan_mirror != nullptr) {
      pchan_clear_rot(pchan_mirror);
    }
  }
  pchan_clear_rot(pchan);
}

/* clear loc/rot/scale of pose-channel */
static void pchan_clear_transforms(const bPose *pose, bPoseChannel *pchan)
{
  pchan_clear_loc_with_mirrored(pose, pchan);
  pchan_clear_rot_with_mirrored(pose, pchan);
  pchan_clear_scale_with_mirrored(pose, pchan);
}

/* --------------- */

/* generic exec for clear-pose operators */
static int pose_clear_transform_generic_exec(bContext *C,
                                             wmOperator *op,
                                             void (*clear_func)(const bPose *, bPoseChannel *),
                                             const char default_ksName[])
{
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  Scene *scene = CTX_data_scene(C);
  bool changed_multi = false;

  /* sanity checks */
  if (ELEM(nullptr, clear_func, default_ksName)) {
    BKE_report(op->reports,
               RPT_ERROR,
               "Programming error: missing clear transform function or keying set name");
    return OPERATOR_CANCELLED;
  }

  /* only clear relevant transforms for selected bones */
  ViewLayer *view_layer = CTX_data_view_layer(C);
  View3D *v3d = CTX_wm_view3d(C);
  FOREACH_OBJECT_IN_MODE_BEGIN (scene, view_layer, v3d, OB_ARMATURE, OB_MODE_POSE, ob_iter) {
    /* XXX: UGLY HACK (for auto-key + clear transforms). */
    Object *ob_eval = DEG_get_evaluated_object(depsgraph, ob_iter);
    blender::Vector<PointerRNA> sources;
    bool changed = false;

    FOREACH_PCHAN_SELECTED_IN_OBJECT_BEGIN (ob_iter, pchan) {
      /* run provided clearing function */
      clear_func(ob_iter->pose, pchan);
      changed = true;

      /* do auto-keyframing as appropriate */
      if (blender::animrig::autokeyframe_cfra_can_key(scene, &ob_iter->id)) {
        /* tag for autokeying later */
        ANIM_relative_keyingset_add_source(sources, &ob_iter->id, &RNA_PoseBone, pchan);

#if 1 /* XXX: Ugly Hack - Run clearing function on evaluated copy of pchan */
        bPoseChannel *pchan_eval = BKE_pose_channel_find_name(ob_eval->pose, pchan->name);
        clear_func(ob_iter->pose, pchan_eval);
#endif
      }
    }
    FOREACH_PCHAN_SELECTED_IN_OBJECT_END;

    if (changed) {
      changed_multi = true;

      /* perform autokeying on the bones if needed */
      if (!sources.is_empty()) {
        /* get KeyingSet to use */
        KeyingSet *ks = ANIM_get_keyingset_for_autokeying(scene, default_ksName);

        /* insert keyframes */
        ANIM_apply_keyingset(C, &sources, ks, MODIFYKEY_MODE_INSERT, float(scene->r.cfra));

        /* now recalculate paths */
        if (ob_iter->pose->avs.path_bakeflag & MOTIONPATH_BAKE_HAS_PATHS) {
          ED_pose_recalculate_paths(C, scene, ob_iter, POSE_PATH_CALC_RANGE_FULL);
        }
      }

      DEG_id_tag_update(&ob_iter->id, ID_RECALC_GEOMETRY);

      /* NOTE: notifier might evolve. */
      WM_event_add_notifier(C, NC_OBJECT | ND_TRANSFORM, ob_iter);
    }
  }
  FOREACH_OBJECT_IN_MODE_END;

  return changed_multi ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Pose Scale Operator
 * \{ */

static int pose_clear_scale_exec(bContext *C, wmOperator *op)
{
  return pose_clear_transform_generic_exec(
      C, op, pchan_clear_scale_with_mirrored, ANIM_KS_SCALING_ID);
}

void POSE_OT_scale_clear(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Pose Scale";
  ot->idname = "POSE_OT_scale_clear";
  ot->description = "Reset scaling of selected bones to their default values";

  /* api callbacks */
  ot->exec = pose_clear_scale_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Pose Rotation Operator
 * \{ */

static int pose_clear_rot_exec(bContext *C, wmOperator *op)
{
  return pose_clear_transform_generic_exec(
      C, op, pchan_clear_rot_with_mirrored, ANIM_KS_ROTATION_ID);
}

void POSE_OT_rot_clear(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Pose Rotation";
  ot->idname = "POSE_OT_rot_clear";
  ot->description = "Reset rotations of selected bones to their default values";

  /* api callbacks */
  ot->exec = pose_clear_rot_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Pose Location Operator
 * \{ */

static int pose_clear_loc_exec(bContext *C, wmOperator *op)
{
  return pose_clear_transform_generic_exec(
      C, op, pchan_clear_loc_with_mirrored, ANIM_KS_LOCATION_ID);
}

void POSE_OT_loc_clear(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Pose Location";
  ot->idname = "POSE_OT_loc_clear";
  ot->description = "Reset locations of selected bones to their default values";

  /* api callbacks */
  ot->exec = pose_clear_loc_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Pose Transforms Operator
 * \{ */

static int pose_clear_transforms_exec(bContext *C, wmOperator *op)
{
  return pose_clear_transform_generic_exec(
      C, op, pchan_clear_transforms, ANIM_KS_LOC_ROT_SCALE_ID);
}

void POSE_OT_transforms_clear(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Pose Transforms";
  ot->idname = "POSE_OT_transforms_clear";
  ot->description =
      "Reset location, rotation, and scaling of selected bones to their default values";

  /* api callbacks */
  ot->exec = pose_clear_transforms_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear User Transforms Operator
 * \{ */

static int pose_clear_user_transforms_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  View3D *v3d = CTX_wm_view3d(C);
  Scene *scene = CTX_data_scene(C);
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  const AnimationEvalContext anim_eval_context = BKE_animsys_eval_context_construct(
      depsgraph, float(scene->r.cfra));
  const bool only_select = RNA_boolean_get(op->ptr, "only_selected");

  FOREACH_OBJECT_IN_MODE_BEGIN (scene, view_layer, v3d, OB_ARMATURE, OB_MODE_POSE, ob) {
    if ((ob->adt) && (ob->adt->action)) {
      /* XXX: this is just like this to avoid contaminating anything else;
       * just pose values should change, so this should be fine
       */
      bPose *dummyPose = nullptr;
      Object workob{};

      /* execute animation step for current frame using a dummy copy of the pose */
      BKE_pose_copy_data(&dummyPose, ob->pose, false);

      STRNCPY(workob.id.name, "OB<ClearTfmWorkOb>");
      workob.type = OB_ARMATURE;
      workob.data = ob->data;
      workob.adt = ob->adt;
      workob.pose = dummyPose;

      BKE_animsys_evaluate_animdata(
          &workob.id, workob.adt, &anim_eval_context, ADT_RECALC_ANIM, false);

      /* Copy back values, but on selected bones only. */
      LISTBASE_FOREACH (bPoseChannel *, pchan, &dummyPose->chanbase) {
        pose_bone_do_paste(ob, pchan, only_select, false);
      }

      /* free temp data - free manually as was copied without constraints */
      LISTBASE_FOREACH (bPoseChannel *, pchan, &dummyPose->chanbase) {
        if (pchan->prop) {
          IDP_FreeProperty(pchan->prop);
        }
      }

      /* was copied without constraints */
      BLI_freelistN(&dummyPose->chanbase);
      MEM_freeN(dummyPose);
    }
    else {
      /* No animation, so just reset to the rest pose. */
      BKE_pose_rest(ob->pose, only_select);
    }

    /* notifiers and updates */
    DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_OBJECT | ND_TRANSFORM, ob);
  }
  FOREACH_OBJECT_IN_MODE_END;

  return OPERATOR_FINISHED;
}

void POSE_OT_user_transforms_clear(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear User Transforms";
  ot->idname = "POSE_OT_user_transforms_clear";
  ot->description = "Reset pose bone transforms to keyframed state";

  /* callbacks */
  ot->exec = pose_clear_user_transforms_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna, "only_selected", true, "Only Selected", "Only visible/selected bones");
}

/** \} */
