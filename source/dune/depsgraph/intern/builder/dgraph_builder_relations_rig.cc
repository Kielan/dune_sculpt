/** Methods for constructing depsgraph **/

#include "intern/builder/dgraph_builder_relations.h"

#include <cstdio>
#include <cstdlib>
#include <cstring> /* required for STREQ later on. */

#include "MEM_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "types_action.h"
#include "types_anim.h"
#include "types_armature.h"
#include "types_constraint.h"
#include "types_customdata.h"
#include "types_object.h"

#include "dune_action.h"
#include "dune_armature.h"
#include "dune_constraint.h"

#include "api_prototypes.h"

#include "dgraph.h"
#include "dgraph_build.h"

#include "intern/builder/dgraph_builder.h"
#include "intern/builder/dgraph_builder_cache.h"
#include "intern/builder/dgraph_builder_pchanmap.h"
#include "intern/debug/dgraph_debug.h"
#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_component.h"
#include "intern/node/dgraph_node_op.h"

#include "intern/dgraph_relation.h"
#include "intern/dgraph_type.h"

namespace dune::dgraph {

/* IK Solver Eval Steps */
void DGraphRelationBuilder::build_ik_pose(Object *object,
                                          DPoseChannel *pchan,
                                          DConstraint *con,
                                          RootPChanMap *root_map)
{
  if ((con->flag & CONSTRAINT_DISABLE) != 0) {
    /* Do not add disabled IK constraints to the relations. If these needs to be temporarily
     * enabled, they will be added as temporary constraints during transform. */
    return;
  }

  DKinematicConstraint *data = (DKinematicConstraint *)con->data;
  /* Attach owner to IK Solver to. */
  DPoseChannel *rootchan = dune_armature_ik_solver_find_root(pchan, data);
  if (rootchan == nullptr) {
    return;
  }
  OpKey pchan_local_key(
      &object->id, NodeType::BONE, pchan->name, OpCode::BONE_LOCAL);
  OpKey init_ik_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_INIT_IK);
  OpKey solver_key(
      &object->id, NodeType::EVAL_POSE, rootchan->name, OpCode::POSE_IK_SOLVER);
  OpKey pose_cleanup_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_CLEANUP);
  /* If any of the constraint parameters are animated, connect the relation. Since there is only
   * one Init IK node per armature, this link has quite high risk of spurious dependency cycles.
   */
  const bool is_itasc = (object->pose->iksolver == IKSOLVER_ITASC);
  ApiPtr con_ptr;
  api_ptr_create(&object->id, &ApiConstraint, con, &con_ptr);
  if (is_itasc || cache_->isAnyPropAnimated(&con_ptr)) {
    add_relation(pchan_local_key, init_ik_key, "IK Constraint -> Init IK Tree");
  }
  add_relation(init_ik_key, solver_key, "Init IK -> IK Solver");
  /* Never cleanup before solver is run. */
  add_relation(solver_key, pose_cleanup_key, "IK Solver -> Cleanup", RELATION_FLAG_GODMODE);
  /* The ITASC solver currently accesses the target transforms in init tree :(
   * TODO: Fix ITASC and remove this.
   */
  OpKey target_dependent_key = is_itasc ? init_ik_key : solver_key;
  /* IK target */
  /* TODO: This should get handled as part of the constraint code. */
  if (data->tar != nullptr) {
    /* Different object - requires its transform. */
    if (data->tar != object) {
      ComponentKey target_key(&data->tar->id, NodeType::TRANSFORM);
      add_relation(target_key, target_dependent_key, con->name);
      /* Ensure target CoW is ready by the time IK tree is built just in case. */
      ComponentKey target_cow_key(&data->tar->id, NodeType::COPY_ON_WRITE);
      add_relation(
          target_cow_key, init_ik_key, "IK Target CoW -> Init IK Tree", RELATION_CHECK_BEFORE_ADD);
    }
    /* Subtarget references: */
    if ((data->tar->type == OB_ARMATURE) && (data->subtarget[0])) {
      /* Bone - use the final transformation. */
      OpKey target_key(
          &data->tar->id, NodeType::BONE, data->subtarget, OpCode::BONE_DONE);
      add_relation(target_key, target_dependent_key, con->name);
    }
    else if (data->subtarget[0] && ELEM(data->tar->type, OB_MESH, OB_LATTICE)) {
      /* Vertex group target. */
      /* NOTE: for now, we don't need to represent vertex groups
       * separately. */
      ComponentKey target_key(&data->tar->id, NodeType::GEOMETRY);
      add_relation(target_key, target_dependent_key, con->name);
      add_customdata_mask(data->tar, DGraphCustomDataMeshMasks::MaskVert(CD_MASK_MDEFORMVERT));
    }
    if (data->tar == object && data->subtarget[0]) {
      /* Prevent target's constraints from linking to anything from same
       * chain that it controls. */
      root_map->add_bone(data->subtarget, rootchan->name);
    }
  }
  /* Pole Target. */
  /* TODO: This should get handled as part of the constraint code. */
  if (data->poletar != nullptr) {
    /* Different object - requires its transform. */
    if (data->poletar != object) {
      ComponentKey target_key(&data->poletar->id, NodeType::TRANSFORM);
      add_relation(target_key, target_dependent_key, con->name);
      /* Ensure target CoW is ready by the time IK tree is built just in case. */
      ComponentKey target_cow_key(&data->poletar->id, NodeType::COPY_ON_WRITE);
      add_relation(
          target_cow_key, init_ik_key, "IK Target CoW -> Init IK Tree", RELATION_CHECK_BEFORE_ADD);
    }
    /* Subtarget references: */
    if ((data->poletar->type == OB_ARMATURE) && (data->polesubtarget[0])) {
      /* Bone - use the final transformation. */
      OpKey target_key(
          &data->poletar->id, NodeType::BONE, data->polesubtarget, OpCode::BONE_DONE);
      add_relation(target_key, target_dependent_key, con->name);
    }
    else if (data->polesubtarget[0] && ELEM(data->poletar->type, OB_MESH, OB_LATTICE)) {
      /* Vertex group target. */
      /* NOTE: for now, we don't need to represent vertex groups
       * separately. */
      ComponentKey target_key(&data->poletar->id, NodeType::GEOMETRY);
      add_relation(target_key, target_dependent_key, con->name);
      add_customdata_mask(data->poletar, DGraphCustomDataMeshMasks::MaskVert(CD_MASK_MDEFORMVERT));
    }
  }
  DEG_DEBUG_PRINTF((::DGraph *)graph_,
                   BUILD,
                   "\nStarting IK Build: pchan = %s, target = (%s, %s), "
                   "segcount = %d\n",
                   pchan->name,
                   data->tar ? data->tar->id.name : "nullptr",
                   data->subtarget,
                   data->rootbone);
  DPoseChannel *parchan = pchan;
  /* Exclude tip from chain if needed. */
  if (!(data->flag & CONSTRAINT_IK_TIP)) {
    parchan = pchan->parent;
  }
  root_map->add_bone(parchan->name, rootchan->name);
  OpKey parchan_transforms_key(
      &object->id, NodeType::BONE, parchan->name, OpCode::BONE_READY);
  add_relation(parchan_transforms_key, solver_key, "IK Solver Owner");
  /* Walk to the chain's root. */
  int segcount = 0;
  while (parchan != nullptr) {
    /* Make IK-solver dependent on this bone's result, since it can only run
     * after the standard results of the bone are know. Validate links step
     * on the bone will ensure that users of this bone only grab the result
     * with IK solver results. */
    if (parchan != pchan) {
      OpKey parent_key(
          &object->id, NodeType::BONE, parchan->name, OpCode::BONE_READY);
      add_relation(parent_key, solver_key, "IK Chain Parent");
      OpKey bone_done_key(
          &object->id, NodeType::BONE, parchan->name, OpCode::BONE_DONE);
      add_relation(solver_key, bone_done_key, "IK Chain Result");
    }
    else {
      OpKey final_transforms_key(
          &object->id, NodeType::BONE, parchan->name, OpCode::BONE_DONE);
      add_relation(solver_key, final_transforms_key, "IK Solver Result");
    }
    parchan->flag |= POSE_DONE;
    root_map->add_bone(parchan->name, rootchan->name);
    /* continue up chain, until we reach target number of items. */
    DEG_DEBUG_PRINTF((::DGraph *)graph_, BUILD, "  %d = %s\n", segcount, parchan->name);
    /* TODO: This is an arbitrary value, which was just following
     * old code convention. */
    segcount++;
    if ((segcount == data->rootbone) || (segcount > 255)) {
      break;
    }
    parchan = parchan->parent;
  }
  OpKey pose_done_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_DONE);
  add_relation(solver_key, pose_done_key, "PoseEval Result-Bone Link");

  /* Add relation when the root of this IK chain is influenced by another IK chain. */
  build_inter_ik_chains(object, solver_key, rootchan, root_map);
}

/* Spline IK Eval Steps */
void DGraphRelationBuilder::build_splineik_pose(Object *object,
                                                DPoseChannel *pchan,
                                                DConstraint *con,
                                                RootPChanMap *root_map)
{
  DSplineIKConstraint *data = (DSplineIKConstraint *)con->data;
  DPoseChannel *rootchan = dune_armature_splineik_solver_find_root(pchan, data);
  OpKey transforms_key(&object->id, NodeType::BONE, pchan->name, OpCode::BONE_READY);
  OpKey init_ik_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_INIT_IK);
  OpKey solver_key(
      &object->id, NodeType::EVAL_POSE, rootchan->name, OpCode::POSE_SPLINE_IK_SOLVER);
  OpKey pose_cleanup_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_CLEANUP);
  /* Solver depends on initialization. */
  add_relation(init_ik_key, solver_key, "Init IK -> IK Solver");
  /* Never cleanup before solver is run. */
  add_relation(solver_key, pose_cleanup_key, "IK Solver -> Cleanup");
  /* Attach owner to IK Solver. */
  add_relation(transforms_key, solver_key, "Spline IK Solver Owner", RELATION_FLAG_GODMODE);
  /* Attach path dependency to solver. */
  if (data->tar != nullptr) {
    ComponentKey target_geometry_key(&data->tar->id, NodeType::GEOMETRY);
    add_relation(target_geometry_key, solver_key, "Curve.Path -> Spline IK");
    ComponentKey target_transform_key(&data->tar->id, NodeType::TRANSFORM);
    add_relation(target_transform_key, solver_key, "Curve.Transform -> Spline IK");
    add_special_eval_flag(&data->tar->id, DAG_EVAL_NEED_CURVE_PATH);
  }
  pchan->flag |= POSE_DONE;
  OpKey final_transforms_key(
      &object->id, NodeType::BONE, pchan->name, OpCode::BONE_DONE);
  add_relation(solver_key, final_transforms_key, "Spline IK Result");
  root_map->add_bone(pchan->name, rootchan->name);
  /* Walk to the chain's root/ */
  int segcount = 1;
  for (DPoseChannel *parchan = pchan->parent; parchan != nullptr && segcount < data->chainlen;
       parchan = parchan->parent, segcount++) {
    /* Make Spline IK solver dependent on this bone's result, since it can
     * only run after the standard results of the bone are know. Validate
     * links step on the bone will ensure that users of this bone only grab
     * the result with IK solver results. */
    OpKey parent_key(&object->id, NodeType::BONE, parchan->name, OpCode::BONE_READY);
    add_relation(parent_key, solver_key, "Spline IK Solver Update");
    OpKey bone_done_key(
        &object->id, NodeType::BONE, parchan->name, OpCode::BONE_DONE);
    add_relation(solver_key, bone_done_key, "Spline IK Solver Result");
    parchan->flag |= POSE_DONE;
    root_map->add_bone(parchan->name, rootchan->name);
  }
  OpKey pose_done_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_DONE);
  add_relation(solver_key, pose_done_key, "PoseEval Result-Bone Link");

  /* Add relation when the root of this IK chain is influenced by another IK chain. */
  build_inter_ik_chains(object, solver_key, rootchan, root_map);
}

void DGraphRelationBuilder::build_inter_ik_chains(Object *object,
                                                  const OpKey &solver_key,
                                                  const DPoseChannel *rootchan,
                                                  const RootPChanMap *root_map)
{
  DPoseChannel *deepest_root = nullptr;
  const char *root_name = rootchan->name;

  /* Find shared IK chain root. */
  for (DPoseChannel *parchan = rootchan->parent; parchan; parchan = parchan->parent) {
    if (!root_map->has_common_root(root_name, parchan->name)) {
      break;
    }
    deepest_root = parchan;
  }
  if (deepest_root == nullptr) {
    return;
  }

  OpKey other_bone_key(
      &object->id, NodeType::BONE, deepest_root->name, OpCode::BONE_DONE);
  add_relation(other_bone_key, solver_key, "IK Chain Overlap");
}

/* Pose/Armature Bones Graph */
void DGraphRelationBuilder::build_rig(Object *object)
{
  /* Armature-Data */
  DArmature *armature = (DArmature *)object->data;
  // TODO: selection status?
  /* Attach links between pose operations. */
  ComponentKey local_transform(&object->id, NodeType::TRANSFORM);
  OpKey pose_init_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_INIT);
  OpKey pose_init_ik_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_INIT_IK);
  OpKey pose_cleanup_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_CLEANUP);
  OpKey pose_done_key(&object->id, NodeType::EVAL_POSE, OpCode::POSE_DONE);
  add_relation(local_transform, pose_init_key, "Local Transform -> Pose Init");
  add_relation(pose_init_key, pose_init_ik_key, "Pose Init -> Pose Init IK");
  add_relation(pose_init_ik_key, pose_done_key, "Pose Init IK -> Pose Cleanup");
  /* Make sure pose is up-to-date with armature updates. */
  build_armature(armature);
  OpKey armature_key(&armature->id, NodeType::ARMATURE, OpCode::ARMATURE_EVAL);
  add_relation(armature_key, pose_init_key, "Data dependency");
  /* Run cleanup even when there are no bones. */
  add_relation(pose_init_key, pose_cleanup_key, "Init -> Cleanup");
  /* IK Solvers.
   *
   * - These require separate processing steps are pose-level to be executed
   *   between chains of bones (i.e. once the base transforms of a bunch of
   *   bones is done).
   *
   * - We build relations for these before the dependencies between operations
   *   in the same component as it is necessary to check whether such bones
   *   are in the same IK chain (or else we get weird issues with either
   *   in-chain references, or with bones being parented to IK'd bones).
   *
   * Unsolved Issues:
   * - Care is needed to ensure that multi-headed trees work out the same as
   *   in ik-tree building
   * - Animated chain-lengths are a problem. */
  RootPChanMap root_map;
  bool pose_depends_on_local_transform = false;
  LISTBASE_FOREACH (DPoseChannel *, pchan, &object->pose->chanbase) {
    const BuilderStack::ScopedEntry stack_entry = stack_.trace(*pchan);

    LISTBASE_FOREACH (DConstraint *, con, &pchan->constraints) {
      const BuilderStack::ScopedEntry stack_entry = stack_.trace(*con);

      switch (con->type) {
        case CONSTRAINT_TYPE_KINEMATIC:
          build_ik_pose(object, pchan, con, &root_map);
          pose_depends_on_local_transform = true;
          break;
        case CONSTRAINT_TYPE_SPLINEIK:
          build_splineik_pose(object, pchan, con, &root_map);
          pose_depends_on_local_transform = true;
          break;
        /* Constraints which needs world's matrix for transform. */
        case CONSTRAINT_TYPE_ROTLIKE:
        case CONSTRAINT_TYPE_SIZELIKE:
        case CONSTRAINT_TYPE_LOCLIKE:
        case CONSTRAINT_TYPE_TRANSLIKE:
          /* TODO: Add used space check. */
          pose_depends_on_local_transform = true;
          break;
        default:
          break;
      }
    }
  }
  // root_map.print_debug();
  if (pose_depends_on_local_transform) {
    /* TODO: Once partial updates are possible use relation between
     * object transform and solver itself in its build function. */
    ComponentKey pose_key(&object->id, NodeType::EVAL_POSE);
    ComponentKey local_transform_key(&object->id, NodeType::TRANSFORM);
    add_relation(local_transform_key, pose_key, "Local Transforms");
  }
  /* Links between operations for each bone. */
  LISTBASE_FOREACH (DPoseChannel *, pchan, &object->pose->chanbase) {
    const BuilderStack::ScopedEntry stack_entry = stack_.trace(*pchan);

    build_idprops(pchan->prop);
    OpKey bone_local_key(
        &object->id, NodeType::BONE, pchan->name, OpCode::BONE_LOCAL);
    OpKey bone_pose_key(
        &object->id, NodeType::BONE, pchan->name, OpCode::BONE_POSE_PARENT);
    OpKey bone_ready_key(
        &object->id, NodeType::BONE, pchan->name, OpCode::BONE_READY);
    OpKey bone_done_key(&object->id, NodeType::BONE, pchan->name, OpCode::BONE_DONE);
    pchan->flag &= ~POSE_DONE;
    /* Pose init to bone local. */
    add_relation(pose_init_key, bone_local_key, "Pose Init - Bone Local", RELATION_FLAG_GODMODE);
    /* Local to pose parenting operation. */
    add_relation(bone_local_key, bone_pose_key, "Bone Local - Bone Pose");
    /* Parent relation. */
    if (pchan->parent != nullptr) {
      OpCode parent_key_opcode;
      /* NOTE: this difference in handling allows us to prevent lockups
       * while ensuring correct poses for separate chains. */
      if (root_map.has_common_root(pchan->name, pchan->parent->name)) {
        parent_key_opcode = OpCode::BONE_READY;
      }
      else {
        parent_key_opcode = OpCode::BONE_DONE;
      }

      OpKey parent_key(&object->id, NodeType::BONE, pchan->parent->name, parent_key_opcode);
      add_relation(parent_key, bone_pose_key, "Parent Bone -> Child Bone");
    }
    /* Build constraints. */
    if (pchan->constraints.first != nullptr) {
      /* Build relations for indirectly linked objects. */
      BuilderWalkUserData data;
      data.builder = this;
      dune_constraints_id_loop(&pchan->constraints, constraint_walk, &data);
      /* Constraints stack and constraint dependencies. */
      build_constraints(&object->id, NodeType::BONE, pchan->name, &pchan->constraints, &root_map);
      /* Pose -> constraints. */
      OpKey constraints_key(
          &object->id, NodeType::BONE, pchan->name, OpCode::BONE_CONSTRAINTS);
      add_relation(bone_pose_key, constraints_key, "Pose -> Constraints Stack");
      add_relation(bone_local_key, constraints_key, "Local -> Constraints Stack");
      /* Constraints -> ready/ */
      /* TODO: When constraint stack is exploded, this step should
       * occur before the first IK solver. */
      add_relation(constraints_key, bone_ready_key, "Constraints -> Ready");
    }
    else {
      /* Pose -> Ready */
      add_relation(bone_pose_key, bone_ready_key, "Pose -> Ready");
    }
    /* Bone ready -> Bone done.
     * NOTE: For bones without IK, this is all that's needed.
     *       For IK chains however, an additional rel is created from IK
     *       to done, with transitive reduction removing this one. */
    add_relation(bone_ready_key, bone_done_key, "Ready -> Done");
    /* B-Bone shape is the real final step after Done if present. */
    if (check_pchan_has_bbone(object, pchan)) {
      OpKey bone_segments_key(
          &object->id, NodeType::BONE, pchan->name, OpCode::BONE_SEGMENTS);
      /* B-Bone shape depends on the final position of the bone. */
      add_relation(bone_done_key, bone_segments_key, "Done -> B-Bone Segments");
      /* B-Bone shape depends on final position of handle bones. */
      DPoseChannel *prev, *next;
      dune_pchan_bbone_handles_get(pchan, &prev, &next);
      if (prev) {
        OpCode opcode = OpCode::BONE_DONE;
        /* Inheriting parent roll requires access to prev handle's B-Bone properties. */
        if ((pchan->bone->bbone_flag & BBONE_ADD_PARENT_END_ROLL) != 0 &&
            check_pchan_has_bbone_segments(object, prev)) {
          opcode = OpCode::BONE_SEGMENTS;
        }
        OpKey prev_key(&object->id, NodeType::BONE, prev->name, opcode);
        add_relation(prev_key, bone_segments_key, "Prev Handle -> B-Bone Segments");
      }
      if (next) {
        OpKey next_key(&object->id, NodeType::BONE, next->name, OpKeyCode::BONE_DONE);
        add_relation(next_key, bone_segments_key, "Next Handle -> B-Bone Segments");
      }
      /* Pose requires the B-Bone shape. */
      add_relation(
          bone_segments_key, pose_done_key, "PoseEval Result-Bone Link", RELATION_FLAG_GODMODE);
      add_relation(bone_segments_key, pose_cleanup_key, "Cleanup dependency");
    }
    else {
      /* Assume that all bones must be done for the pose to be ready
       * (for deformers). */
      add_relation(bone_done_key, pose_done_key, "PoseEval Result-Bone Link");

      /* Bones must be traversed before cleanup. */
      add_relation(bone_done_key, pose_cleanup_key, "Done -> Cleanup");

      add_relation(bone_ready_key, pose_cleanup_key, "Ready -> Cleanup");
    }
    /* Custom shape. */
    if (pchan->custom != nullptr) {
      build_object(pchan->custom);
      add_visibility_relation(&pchan->custom->id, &armature->id);
    }
  }
}

}  // namespace dune::deg
