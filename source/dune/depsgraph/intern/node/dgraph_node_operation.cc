#include "intern/node/dgraph_node_op.h"

#include "MEM_guardedalloc.h"

#include "lib_utildefines.h"

#include "intern/dgraph.h"
#include "intern/node/dgraph_node_component.h"
#include "intern/node/dgraph_node_factory.h"
#include "intern/node/dgraph_node_id.h"

namespace dune::dgraph {

const char *opCodeAsString(OpCode opcode)
{
  switch (opcode) {
    /* Generic Operations. */
    case OpCode::OPERATION:
      return "OPERATION";
    case OpCode::ID_PROPERTY:
      return "ID_PROPERTY";
    case OpCode::PARAMS_ENTRY:
      return "PARAMS_ENTRY";
    case OpCode::PARAMS_EVAL:
      return "PARAMS_EVAL:
    case OpCode::PARAMS_EXIT:
      return "PARAMS_EXIT";
    /* Animation, Drivers, etc. */
    case OpCode::ANIMATION_ENTRY:
      return "ANIMATION_ENTRY";
    case OpCode::ANIMATION_EVAL:
      return "ANIMATION_EVAL";
    case OpCode::ANIMATION_EXIT:
      return "ANIMATION_EXIT";
    case OpCode::DRIVER:
      return "DRIVER";
    /* Scene related. */
    case OpCode::SCENE_EVAL:
      return "SCENE_EVAL";
    case OpCode::AUDIO_ENTRY:
      return "AUDIO_ENTRY";
    case OpCode::AUDIO_VOLUME:
      return "AUDIO_VOLUME";
    /* Object related. */
    case OpCode::OBJECT_FROM_LAYER_ENTRY:
      return "OBJECT_FROM_LAYER_ENTRY";
    case OpCode::OBJECT_BASE_FLAGS:
      return "OBJECT_BASE_FLAGS";
    case OpCode::OBJECT_FROM_LAYER_EXIT:
      return "OBJECT_FROM_LAYER_EXIT";
    case OpCode::DIMENSIONS:
      return "DIMENSIONS";
    /* Transform. */
    case OpCode::TRANSFORM_INIT:
      return "TRANSFORM_INIT";
    case OpCode::TRANSFORM_LOCAL:
      return "TRANSFORM_LOCAL";
    case OpCode::TRANSFORM_PARENT:
      return "TRANSFORM_PARENT";
    case OpCode::TRANSFORM_CONSTRAINTS:
      return "TRANSFORM_CONSTRAINTS";
    case OpCode::TRANSFORM_FINAL:
      return "TRANSFORM_FINAL";
    case OpCode::TRANSFORM_EVAL:
      return "TRANSFORM_EVAL";
    case OpCode::TRANSFORM_SIMULATION_INIT:
      return "TRANSFORM_SIMULATION_INIT";
    /* Rigid body. */
    case OpCode::RIGIDBODY_REBUILD:
      return "RIGIDBODY_REBUILD";
    case OpCode::RIGIDBODY_SIM:
      return "RIGIDBODY_SIM";
    case OpCode::RIGIDBODY_TRANSFORM_COPY:
      return "RIGIDBODY_TRANSFORM_COPY";
    /* Geometry. */
    case OpCode::GEOMETRY_EVAL_INIT:
      return "GEOMETRY_EVAL_INIT";
    case OpCode::GEOMETRY_EVAL:
      return "GEOMETRY_EVAL";
    case OpCode::GEOMETRY_EVAL_DONE:
      return "GEOMETRY_EVAL_DONE";
    case OpCode::GEOMETRY_SHAPEKEY:
      return "GEOMETRY_SHAPEKEY";
    /* Object data. */
    case OpCode::LIGHT_PROBE_EVAL:
      return "LIGHT_PROBE_EVAL";
    case OpCode::SPEAKER_EVAL:
      return "SPEAKER_EVAL";
    case OpCode::SOUND_EVAL:
      return "SOUND_EVAL";
    case OpCode::ARMATURE_EVAL:
      return "ARMATURE_EVAL";
    /* Pose. */
    case OpCode::POSE_INIT:
      return "POSE_INIT";
    case OpCode::POSE_INIT_IK:
      return "POSE_INIT_IK";
    case OpCode::POSE_CLEANUP:
      return "POSE_CLEANUP";
    case OpCode::POSE_DONE:
      return "POSE_DONE";
    case OpCode::POSE_IK_SOLVER:
      return "POSE_IK_SOLVER";
    case OpCode::POSE_SPLINE_IK_SOLVER:
      return "POSE_SPLINE_IK_SOLVER";
    /* Bone. */
    case OpCode::BONE_LOCAL:
      return "BONE_LOCAL";
    case OpCode::BONE_POSE_PARENT:
      return "BONE_POSE_PARENT";
    case OpCode::BONE_CONSTRAINTS:
      return "BONE_CONSTRAINTS";
    case OpCode::BONE_READY:
      return "BONE_READY";
    case OpCode::BONE_DONE:
      return "BONE_DONE";
    case OpCode::BONE_SEGMENTS:
      return "BONE_SEGMENTS";
    /* Particle System. */
    case OpCode::PARTICLE_SYSTEM_INIT:
      return "PARTICLE_SYSTEM_INIT";
    case OpCode::PARTICLE_SYSTEM_EVAL:
      return "PARTICLE_SYSTEM_EVAL";
    case OpCode::PARTICLE_SYSTEM_DONE:
      return "PARTICLE_SYSTEM_DONE";
    /* Particles Settings. */
    case OpCode::PARTICLE_SETTINGS_INIT:
      return "PARTICLE_SETTINGS_INIT";
    case OpCode::PARTICLE_SETTINGS_EVAL:
      return "PARTICLE_SETTINGS_EVAL";
    case OpCode::PARTICLE_SETTINGS_RESET:
      return "PARTICLE_SETTINGS_RESET";
    /* Point Cache. */
    case OpCode::POINT_CACHE_RESET:
      return "POINT_CACHE_RESET";
    /* File cache. */
    case OpCode::FILE_CACHE_UPDATE:
      return "FILE_CACHE_UPDATE";
    /* Batch cache. */
    case OpCode::GEOMETRY_SELECT_UPDATE:
      return "GEOMETRY_SELECT_UPDATE";
    /* Masks. */
    case OpCode::MASK_ANIMATION:
      return "MASK_ANIMATION";
    case OpCode::MASK_EVAL:
      return "MASK_EVAL";
    /* Collections. */
    case OpCode::VIEW_LAYER_EVAL:
      return "VIEW_LAYER_EVAL";
    /* Copy on write. */
    case OpCode::COPY_ON_WRITE:
      return "COPY_ON_WRITE";
    /* Shading. */
    case OpCode::SHADING:
      return "SHADING";
    case OpCode::MATERIAL_UPDATE:
      return "MATERIAL_UPDATE";
    case OpCode::LIGHT_UPDATE:
      return "LIGHT_UPDATE";
    case OpCode::WORLD_UPDATE:
      return "WORLD_UPDATE";
    /* Node Tree. */
    case OpCode::NTREE_OUTPUT:
      return "NTREE_OUTPUT";
    /* Movie clip. */
    case OpCode::MOVIECLIP_EVAL:
      return "MOVIECLIP_EVAL";
    case OpCode::MOVIECLIP_SELECT_UPDATE:
      return "MOVIECLIP_SELECT_UPDATE";
    /* Image. */
    case OpCode::IMAGE_ANIMATION:
      return "IMAGE_ANIMATION";
    /* Synchronization. */
    case OpCode::SYNCHRONIZE_TO_ORIGINAL:
      return "SYNCHRONIZE_TO_ORIGINAL";
    /* Generic datablock. */
    case OpCode::GENERIC_DATABLOCK_UPDATE:
      return "GENERIC_DATABLOCK_UPDATE";
    /* Sequencer. */
    case OpCode::SEQUENCES_EVAL:
      return "SEQUENCES_EVAL";
    /* instancing/duplication. */
    case OpCode::DUPLI:
      return "DUPLI";
    case OpCode::SIMULATION_EVAL:
      return "SIMULATION_EVAL";
  }
  lib_assert_msg(0, "Unhandled operation code, should never happen.");
  return "UNKNOWN";
}

OpNode::OpNode() : name_tag(-1), flag(0)
{
}

string Op::id() const
{
  return string(opCodeAsString(opcode)) + "(" + name + ")";
}

string OpNode::full_identifier() const
{
  string owner_str = owner->owner->name;
  if (owner->type == NodeType::BONE || !owner->name.empty()) {
    owner_str += "/" + owner->name;
  }
  return owner_str + "/" + identifier();
}

void OpNode::tag_update(Depsgraph *graph, eUpdateSource source)
{
  if ((flag & DGRAPH_OP_FLAG_NEEDS_UPDATE) == 0) {
    graph->add_entry_tag(this);
  }
  /* Tag for update, but also note that this was the source of an update. */
  flag |= (DGRAPH_OP_FLAG_NEEDS_UPDATE | DEPSOP_FLAG_DIRECTLY_MODIFIED);
  switch (source) {
    case DGRAPH_UPDATE_SOURCE_TIME:
    case DGRAPH_UPDATE_SOURCE_RELATIONS:
    case DGRAPH_UPDATE_SOURCE_VISIBILITY:
      /* Currently nothing. */
      break;
    case DGRAPH_UPDATE_SOURCE_USER_EDIT:
      flag |= DEPSOP_FLAG_USER_MODIFIED;
      break;
  }
}

void OpNode::set_as_entry()
{
  lib_assert(owner != nullptr);
  owner->set_entry_op(this);
}

void OpNode::set_as_exit()
{
  lib_assert(owner != nullptr);
  owner->set_exit_op(this);
}

DGRAPH_NODE_DEFINE(OpNode, NodeType::OP, "Operation");
static DGraphNodeFactoryImpl<OpNode> DNTI_OP;

void dgraph_register_op_dgraphnodes()
{
  register_node_typeinfo(&DNTI_OP);
}

}  // namespace dune::dgraph
