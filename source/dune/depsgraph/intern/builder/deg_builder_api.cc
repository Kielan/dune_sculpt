#include "intern/builder/dgraph_builder_api.h"

#include <cstring>

#include "MEM_guardedalloc.h"

#include "lib_listbase.h"
#include "lib_utildefines.h"

#include "types_action.h"
#include "types_armature.h"
#include "types_constraint.h"
#include "types_key.h"
#include "types_object.h"
#include "types_sequence.h"

#include "dune_constraint.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "intern/builder/dgraphbuilder.h"
#include "intern/dgraph.h"
#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_component.h"
#include "intern/node/dgraph_node_id.h"
#include "intern/node/dgraph_node_operation.h"

namespace dune::deg {

/* ********************************* ID Data ******************************** */

class ApiNodeQueryIdData {
 public:
  explicit ApiNodeQueryIdData(const Id *id) : id_(id)
  {
  }

  ~ApiNodeQueryIdData()
  {
    delete constraint_to_pchan_map_;
  }

  const DPoseChannel *get_pchan_for_constraint(const DConstraint *constraint)
  {
    ensure_constraint_to_pchan_map();
    return constraint_to_pchan_map_->lookup_default(constraint, nullptr);
  }

  void ensure_constraint_to_pchan_map()
  {
    if (constraint_to_pchan_map_ != nullptr) {
      return;
    }
    lib_assert(GS(id_->name) == ID_OB);
    const Object *object = reinterpret_cast<const Object *>(id_);
    constraint_to_pchan_map_ = new Map<const DConstraint *, const DPoseChannel *>();
    if (object->pose != nullptr) {
      LISTBASE_FOREACH (const DPoseChannel *, pchan, &object->pose->chanbase) {
        LISTBASE_FOREACH (const DConstraint *, constraint, &pchan->constraints) {
          constraint_to_pchan_map_->add_new(constraint, pchan);
        }
      }
    }
  }

 protected:
  /* Id this data corresponds to. */
  const Id *id_;

  /* indexed by DConstraint*, returns pose channel which contains that
   * constraint. */
  Map<const DConstraint *, const DPoseChannel *> *constraint_to_pchan_map_ = nullptr;
};

/* ***************************** Node Identifier **************************** */

ApiNodeId::apiNodeId()
    : id(nullptr),
      type(NodeType::UNDEFINED),
      component_name(""),
      op_code(OpCode::OPERATION),
      op_name(),
      op_name_tag(-1)
{
}

bool apiNodeId::is_valid() const
{
  return id != nullptr && type != NodeType::UNDEFINED;
}

/* ********************************** Query ********************************* */

ApiNodeQuery::apiNodeQuery(DGraph *dgraph, DGraphBuilder *builder)
    : dgraph_(dgraph), builder_(builder)
{
}

ApiNodeQuery::~ApiNodeQuery() = default;

Node *ApiNodeQuery::find_node(const ApiPtr *ptr,
                              const ApiProp *prop,
                              ApiPtrSource source)
{
  const ApiNodeId node_id = construct_node_id(ptr, prop, source);
  if (!node_id.is_valid()) {
    return nullptr;
  }
  IdNode *id_node = dgraph_->find_id_node(node_id.id);
  if (id_node == nullptr) {
    return nullptr;
  }
  ComponentNode *comp_node = id_node->find_component(node_id.type,
                                                     node_id.component_name);
  if (comp_node == nullptr) {
    return nullptr;
  }
  if (node_id.op_code == OpCode::OPERATION) {
    return comp_node;
  }
  return comp_node->find_op(node_id.op_code,
                                   node_id.op_name,
                                   node_id.op_name_tag);
}

bool ApiNodeQuery::contains(const char *prop_id, const char *api_path_component)
{
  const char *substr = strstr(prop_id, api_path_component);
  if (substr == nullptr) {
    return false;
  }

  /* If `substr != prop_id`, it means that the sub-string is found further in
   * `prop_id`, and that thus index -1 is a valid memory location. */
  const bool start_ok = substr == prop_id || substr[-1] == '.';
  if (!start_ok) {
    return false;
  }

  const size_t component_len = strlen(api_path_component);
  const bool end_ok = ELEM(substr[component_len], '\0', '.', '[');
  return end_ok;
}

ApiNodeId ApiNodeQuery::construct_node_id(const ApiPtr *ptr,
                                          const ApiProp *prop,
                                          ApiPtrSource source)
{
  ApiNodeId node_id;
  if (ptr->type == nullptr) {
    return node_id;
  }
  /* Set default values for returns. */
  node_id.id = ptr->owner_id;
  node_id.component_name = "";
  node_id.op_code = OpCode::OPERATION;
  node_id.op_name = "";
  node_id.op_name_tag = -1;
  /* Handling of commonly known scenarios. */
  if (api_prop_affects_params_node(ptr, prop)) {
    /* Custom properties of bones are placed in their components to improve granularity. */
    if (api_struct_is_a(ptr->type, &Api_PoseBone)) {
      const DPoseChannel *pchan = static_cast<const DPoseChannel *>(ptr->data);
      node_id.type = NodeType::BONE;
      node_id.component_name = pchan->name;
    }
    else {
      node_id.type = NodeType::PARAMS;
    }
    node_id.op_code = OpCode::ID_PROP;
    node_id.op_name = api_prop_id(
        reinterpret_cast<const PropApi *>(prop));
    return node_id;
  }
  if (ptr->type == &Api_PoseBone) {
    const DPoseChannel *pchan = static_cast<const DPoseChannel *>(ptr->data);
    /* Bone - generally, we just want the bone component. */
    node_id.type = NodeType::BONE;
    node_id.component_name = pchan->name;
    /* However check property name for special handling. */
    if (prop != nullptr) {
      Object *object = reinterpret_cast<Object *>(node_id.id);
      const char *prop_name = api_prop_id(prop);
      /* B-Bone properties should connect to the final operation. */
      if (STRPREFIX(prop_name, "bbone_")) {
        if (builder_->check_pchan_has_bbone_segments(object, pchan)) {
          node_id.op_code = OpCode::BONE_SEGMENTS;
        }
        else {
          node_id.op_code = OpCode::BONE_DONE;
        }
      }
      /* Final transform properties go to the Done node for the exit. */
      else if (STR_ELEM(prop_name, "head", "tail", "length") || STRPREFIX(prop_name, "matrix")) {
        if (source == ApiPtrSource::EXIT) {
          node_id.op_code = OpCode::BONE_DONE;
        }
      }
      /* And other properties can always go to the entry operation. */
      else {
        node_id.op_code = OpCode::BONE_LOCAL;
      }
    }
    return node_id;
  }
  if (ptr->type == &Api_Bone) {
    /* Armature-level bone mapped to Armature Eval, and thus Pose Init.
     * Drivers have special code elsewhere that links them to the pose
     * bone components, instead of using this generic code. */
    node_id.type = NodeType::ARMATURE;
    node_id.op_code = OpCode::ARMATURE_EVAL;
    /* If trying to look up via an Object, e.g. due to lookup via
     * obj.pose.bones[].bone in a driver attached to the Object,
     * redirect to its data. */
    if (GS(node_id.id->name) == ID_OB) {
      node_id.id = (Id *)((Object *)node_identifier.id)->data;
    }
    return node_id;
  }

  const char *prop_id = prop != nullptr ? api_prop_id((ApiProp *)prop) :
                                                  "";

  if (api_struct_is_a(ptr->type, &Api_Constraint)) {
    const Object *object = reinterpret_cast<const Object *>(ptr->owner_id);
    const DConstraint *constraint = static_cast<const DConstraint *>(ptr->data);
    ApiNodeQueryIdData *id_data = ensure_id_data(&object->id);
    /* Check whether is object or bone constraint. */
    /* NOTE: Currently none of the area can address transform of an object
     * at a given constraint, but for rigging one might use constraint
     * influence to be used to drive some corrective shape keys or so. */
    const DPoseChannel *pchan = id_data->get_pchan_for_constraint(constraint);
    if (pchan == nullptr) {
      node_id.type = NodeType::TRANSFORM;
      node_id.op_code = OpCode::TRANSFORM_LOCAL;
    }
    else {
      node_id.type = NodeType::BONE;
      node_id.op_code = OpCode::BONE_LOCAL;
      node_id.component_name = pchan->name;
    }
    return node_id;
  }
  if (ELEM(ptr->type, &Api_ConstraintTarget, &Api_ConstraintTargetBone)) {
    Object *object = reinterpret_cast<Object *>(ptr->owner_id);
    DConstraintTarget *tgt = (DConstraintTarget *)ptr->data;
    /* Check whether is object or bone constraint. */
    DPoseChannel *pchan = nullptr;
    DConstraint *con = dune_constraint_find_from_target(object, tgt, &pchan);
    if (con != nullptr) {
      if (pchan != nullptr) {
        node_id.type = NodeType::BONE;
        node_id.op_code = OpCode::BONE_LOCAL;
        node_id.component_name = pchan->name;
      }
      else {
        node_id.type = NodeType::TRANSFORM;
        node_id.op_code = OpCode::TRANSFORM_LOCAL;
      }
      return node_id;
    }
  }
  else if (api_struct_is_a(ptr->type, &api_Modifier) &&
           (contains(prop_id, "show_viewport") ||
            contains(prop_id, "show_render"))) {
    node_id.type = NodeType::GEOMETRY;
    node_id.op_code = OpCode::VISIBILITY;
    return node_id;
  }
  else if (api_struct_is_a(ptr->type, &Api_Mesh) || api_struct_is_a(ptr->type, &Api_Modifier) ||
           api_struct_is_a(ptr->type, &Api_DpenModifier) ||
           api_struct_is_a(ptr->type, &Api_Spline) || api_struct_is_a(ptr->type, &Api_TextBox) ||
           api_struct_is_a(ptr->type, &Api_DPenLayer) ||
           api_struct_is_a(ptr->type, &Api_LatticePoint) ||
           api_struct_is_a(ptr->type, &Api_MeshUVLoop) ||
           api_struct_is_a(ptr->type, &Api_MeshLoopColor) ||
           api_struct_is_a(ptr->type, &Api_VertexGroupElement) ||
           api_struct_is_a(ptr->type, &Api_ShaderFx)) {
    /* When modifier is used as FROM operation this is likely referencing to
     * the property (for example, modifier's influence).
     * But when it's used as TO operation, this is geometry component. */
    switch (source) {
      case ApiPtrSource::ENTRY:
        node_id.type = NodeType::GEOMETRY;
        break;
      case ApiPtrSource::EXIT:
        node_id.type = NodeType::PARAMS;
        node_id.op_code = OpCode::PARAMS_EVAL;
        break;
    }
    return node_id;
  }
  else if (ptr->type == &Api_Object) {
    /* Transforms props? */
    if (prop != nullptr) {
      /* TODO: How to optimize this? */
      if (contains(prop_id, "location") || contains(prop_id, "matrix_basis") ||
          contains(prop_id, "matrix_channel") ||
          contains(prop_id, "matrix_inverse") ||
          contains(prop_id, "matrix_local") ||
          contains(prop_id, "matrix_parent_inverse") ||
          contains(prop_id, "matrix_world") ||
          contains(prop_id, "rotation_axis_angle") ||
          contains(prop_id, "rotation_euler") ||
          contains(prop_id, "rotation_mode") ||
          contains(prop_id, "rotation_quaternion") || contains(prop_id, "scale") ||
          contains(prop_id, "delta_location") ||
          contains(prop_id, "delta_rotation_euler") ||
          contains(prop_id, "delta_rotation_quaternion") ||
          contains(prop_id, "delta_scale")) {
        node_id.type = NodeType::TRANSFORM;
        return node_id;
      }
      if (contains(prop_id, "data")) {
        /* We access object.data, most likely a geometry.
         * Might be a bone tho. */
        node_id.type = NodeType::GEOMETRY;
        return node_id;
      }
      if (STR_ELEM(prop_id, "hide_viewport", "hide_render")) {
        node_id.type = NodeType::OBJECT_FROM_LAYER;
        return node_id;
      }
      if (STREQ(prop_id, "dimensions")) {
        node_id.type = NodeType::PARAMS;
        node_id.op_code = OpCode::DIMENSIONS;
        return node_id;
      }
    }
  }
  else if (ptr->type == &Api_ShapeKey) {
    KeyBlock *key_block = static_cast<KeyBlock *>(ptr->data);
    node_id.id = ptr->owner_id;
    node_id.type = NodeType::PARAMS;
    node_id.op_code = OpCode::PARAMS_EVAL;
    node_id.op_name = key_block->name;
    return node_id;
  }
  else if (ptr->type == &Api_Key) {
    node_id.id = ptr->owner_id;
    node_id.type = NodeType::GEOMETRY;
    return node_id;
  }
  else if (api_struct_is_a(ptr->type, &Api_Sequence)) {
    /* Sequencer strip */
    node_id.type = NodeType::SEQUENCER;
    return node_id;
  }
  else if (api_struct_is_a(ptr->type, &Api_NodeSocket)) {
    node_id.type = NodeType::NTREE_OUTPUT;
    return node_id;
  }
  else if (api_struct_is_a(ptr->type, &Api_ShaderNode)) {
    node_id.type = NodeType::SHADING;
    return node_id;
  }
  else if (ELEM(ptr->type, &Api_Curve, &Api_TextCurve)) {
    node_id.id = ptr->owner_id;
    node_id.type = NodeType::GEOMETRY;
    return node_id;
  }
  else if (ELEM(ptr->type, &Api_BezierSplinePoint, &Api_SplinePoint)) {
    node_id.id = ptr->owner_id;
    node_id.type = NodeType::GEOMETRY;
    return node_id;
  }
  else if (api_struct_is_a(ptr->type, &Api_ImageUser)) {
    if (GS(node_id.id->name) == ID_NT) {
      node_id.type = NodeType::IMAGE_ANIMATION;
      node_id.op_code = OpCode::IMAGE_ANIMATION;
      return node_id;
    }
  }
  else if (ELEM(ptr->type, &Api_MeshVertex, &Api_MeshEdge, &Api_MeshLoop, &Api_MeshPolygon)) {
    node_id.type = NodeType::GEOMETRY;
    return node_id;
  }
  if (prop != nullptr) {
    /* All unknown data effectively falls under "parameter evaluation". */
    node_id.type = NodeType::PARAMS;
    node_id.op_code = OpCode::PARAMS_EVAL;
    node_id.op_name = "";
    node_id.op_name_tag = -1;
    return node_id;
  }
  return node_id;
}

ApiNodeQueryIdData *apiNodeQuery::ensure_id_data(const Id *id)
{
  unique_ptr<ApiNodeQueryIdData> &id_data = id_data_map_.lookup_or_add_cb(
      id, [&]() { return std::make_unique<ApiNodeQueryIdData>(id); });
  return id_data.get();
}

bool api_prop_affects_params_node(const ApiPtr *ptr, const ApiProp *prop)
{
  return prop != nullptr && api_prop_is_idprop(prop) &&
         /* ID properties in the geometry nodes modifier don't affect that parameters node.
          * Instead they affect the modifier and therefore the geometry node directly. */
         !api_struct_is_a(ptr->type, &api_NodesModifier);
}

}  // namespace dune::deg
