#pragma once

#include "intern/node/graph_node_id.h"

#include <iostream>

#include "types_id.h"
#include "types_object.h"
#include "types_rigidbody.h"

namespace dune::graph {

template<typename KeyType>
OpNode *GraphRelationBuilder::find_op_node(const KeyType &key)
{
  Node *node = get_node(key);
  return node != nullptr ? node->get_exit_operation() : nullptr;
}

template<typename KeyFrom, typename KeyTo>
Relation *GraphRelationBuilder::add_relation(const KeyFrom &key_from,
                                                 const KeyTo &key_to,
                                                 const char *description,
                                                 int flags)
{
  Node *node_from = get_node(key_from);
  Node *node_to = get_node(key_to);
  OpNode *op_from = node_from ? node_from->get_exit_op() : nullptr;
  OpNode *op_to = node_to ? node_to->get_entry_op() : nullptr;

  if (op_from && op_to) {
    return add_op_relation(op_from, op_to, description, flags);
  }

  /* TODO: Report error in the interface. */

  std::cerr << "--------------------------------------------------------------------\n";
  std::cerr << "Failed to add relation \"" << description << "\"\n";

  if (!op_from) {
    std::cerr << "Could not find op_from: " << key_from.id() << "\n";
  }

  if (!op_to) {
    std::cerr << "Could not find op_to: " << key_to.id() << "\n";
  }

  if (!stack_.is_empty()) {
    std::cerr << "\nTrace:\n\n";
    stack_.print_backtrace(std::cerr);
    std::cerr << "\n";
  }

  return nullptr;
}

template<typename KeyTo>
Relation *DGraphRelationBuilder::add_relation(const TimeSourceKey &key_from,
                                              const KeyTo &key_to,
                                              const char *description,
                                              int flags)
{
  TimeSourceNode *time_from = get_node(key_from);
  Node *node_to = get_node(key_to);
  OpNode *op_to = node_to ? node_to->get_entry_op() : nullptr;
  if (time_from != nullptr && op_to != nullptr) {
    return add_time_relation(time_from, op_to, description, flags);
  }
  return nullptr;
}

template<typename KeyType>
Relation *DGraphRelationBuilder::add_node_handle_relation(const KeyType &key_from,
                                                          const DepsNodeHandle *handle,
                                                          const char *description,
                                                          int flags)
{
  Node *node_from = get_node(key_from);
  OpNode *op_from = node_from ? node_from->get_exit_op() : nullptr;
  OpNode *op_to = handle->node->get_entry_op();
  if (op_from != nullptr && op_to != nullptr) {
    return add_op_relation(op_from, op_to, description, flags);
  }
  else {
    if (!op_from) {
      fprintf(stderr,
              "add_node_handle_relation(%s) - Could not find op_from (%s)\n",
              description,
              key_from.id().c_str());
    }
    if (!op_to) {
      fprintf(stderr,
              "add_node_handle_relation(%s) - Could not find op_to (%s)\n",
              description,
              key_from.id().c_str());
    }
  }
  return nullptr;
}

static inline bool rigidbody_object_depends_on_evaluated_geometry(const RigidBodyOb *rbo)
{
  if (rbo == nullptr) {
    return false;
  }
  if (ELEM(rbo->shape, RB_SHAPE_CONVEXH, RB_SHAPE_TRIMESH)) {
    if (rbo->mesh_source != RBO_MESH_BASE) {
      return true;
    }
  }
  return false;
}

template<typename KeyTo>
Relation *DGraphRelationBuilder::add_depends_on_transform_relation(ID *id,
                                                                      const KeyTo &key_to,
                                                                      const char *description,
                                                                      int flags)
{
  if (GS(id->name) == ID_OB) {
    Object *object = reinterpret_cast<Object *>(id);
    if (rigidbody_object_depends_on_evaluated_geometry(object->rigidbody_object)) {
      OpKey transform_key(&object->id, NodeType::TRANSFORM, OpCode::TRANSFORM_EVAL);
      return add_relation(transform_key, key_to, description, flags);
    }
  }
  ComponentKey transform_key(id, NodeType::TRANSFORM);
  return add_relation(transform_key, key_to, description, flags);
}

template<typename KeyType>
DGraphNodeHandle DGraphRelationBuilder::create_node_handle(const KeyType &key,
                                                         const char *default_name)
{
  return DGraphNodeHandle(this, get_node(key), default_name);
}

/* Rig compatibility: we check if bone is using local transform as a variable
 * for driver on itself and ignore those relations to avoid "false-positive"
 * dependency cycles.
 */
template<typename KeyFrom, typename KeyTo>
bool DGraphRelationBuilder::is_same_bone_dependency(const KeyFrom &key_from,
                                                    const KeyTo &key_to)
{
  /* Get operations for requested keys. */
  Node *node_from = get_node(key_from);
  Node *node_to = get_node(key_to);
  if (node_from == nullptr || node_to == nullptr) {
    return false;
  }
  OpNode *op_from = node_from->get_exit_op();
  OpNode *op_to = node_to->get_entry_op();
  if (op_from == nullptr || op_to == nullptr) {
    return false;
  }
  /* Different armatures, bone can't be the same. */
  if (op_from->owner->owner != op_to->owner->owner) {
    return false;
  }
  /* We are only interested in relations like BONE_DONE -> BONE_LOCAL... */
  if (!(op_from->opcode == OpCode::BONE_DONE &&
        op_to->opcode == OpCode::BONE_LOCAL)) {
    return false;
  }
  /* ... BUT, we also need to check if it's same bone. */
  if (op_from->owner->name != op_to->owner->name) {
    return false;
  }
  return true;
}

template<typename KeyFrom, typename KeyTo>
bool DGraphRelationBuilder::is_same_nodetree_node_dependency(const KeyFrom &key_from,
                                                             const KeyTo &key_to)
{
  /* Get operations for requested keys. */
  Node *node_from = get_node(key_from);
  Node *node_to = get_node(key_to);
  if (node_from == nullptr || node_to == nullptr) {
    return false;
  }
  OpNode *op_from = node_from->get_exit_op();
  OpNode *op_to = node_to->get_entry_op();
  if (op_from == nullptr || op_to == nullptr) {
    return false;
  }
  /* Check if this is actually a node tree. */
  if (GS(op_from->owner->owner->id_orig->name) != ID_NT) {
    return false;
  }
  /* Different node trees. */
  if (op_from->owner->owner != op_to->owner->owner) {
    return false;
  }
  /* We are only interested in relations like BONE_DONE -> BONE_LOCAL... */
  if (!(op_from->opcode == OpCode::PARAMS_EVAL &&
        op_to->opcode == OpCode::PARAMS_EVAL)) {
    return false;
  }
  return true;
}

}  // namespace dune::deg
