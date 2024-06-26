#pragma once

#include "intern/eval/graph_eval_copy_on_write.h"
#include "intern/node/graph_node.h"
#include "intern/node/graph_node_id.h"
#include "intern/node/graph_node_operation.h"

#include "lib_string.h"
#include "lib_utildefines.h"

#include "dune_object.h"

#include "types_object.h"

struct Id;
struct PoseChannel;

namespace dune {
namespace graph {

struct BoneComponentNode;
struct Graph;
struct IdNode;
struct OpNode;

/* id Component - Base type for all components */
struct ComponentNode : public Node {
  /* Key used to look up operations within a component */
  struct OpIdKey {
    OpCode opcode;
    const char *name;
    int name_tag;

    OpIdKey();
    OpIdKey(OpCode opcode);
    OpIdKey(OpCode opcode, const char *name, int name_tag);

    string id() const;
    bool op==(const OpIdKey &other) const;
    uint64_t hash() const;
  };

  /* Typedef for container of operations */
  ComponentNode();
  ~ComponentNode();

  /** Initialize 'component' node - from pointer data given. */
  void init(const Id *id, const char *subdata) override;

  virtual string id() const override;

  /* Find an existing operation, if requested operation does not exist
   * nullptr will be returned. */
  OpNode *find_op(OpIdKey key) const;
  OpNode *find_op(OpCode opcode, const char *name, int name_tag) const;

  /* Find an existing operation, will throw an assert() if it does not exist. */
  OpNode *get_op(OpIdKey key) const;
  OpNode *get_op(OpCode opcode, const char *name, int name_tag) const;

  /* Check operation exists and return it. */
  bool has_op(OpIdKey key) const;
  bool has_op(OpCode opcode, const char *name, int name_tag) const;

  /**
   * Create a new node for representing an operation and add this to graph
   * warning If an existing node is found, it will be modified. This helps
   * when node may have been partially created earlier (e.g. parent ref before
   * parent item is added)
   *
   * param opcode: The operation to perform.
   * param name: Id for operation - used to find/locate it again.
   */
  OpNode *add_op(const GraphEvalOpCb &op,
                 OpCode opcode,
                 const char *name,
                 int name_tag);

  /* Entry/exit operations management.
   *
   * Use those instead of direct set since this will perform sanity checks. */
  void set_entry_op(OpNode *op_node);
  void set_exit_op(OpNode *op_node);

  void clear_ops();

  virtual void tag_update(Graph *graph, eUpdateSource source) override;

  virtual OpNode *get_entry_op() override;
  virtual OpNode *get_exit_op() override;

  void finalize_build(Graph *graph);

  IdNode *owner;

  /* ** Inner nodes for this component ** */

  /* Operations stored as a hash map, for faster build.
   * This hash map will be freed when graph is fully built. */
  Map<ComponentNode::OpIdKey, OpNode *> *ops_map;

  /* This is a "normal" list of operations, used by evaluation
   * and other routines after construction. */
  Vector<OpNode *> ops;

  OpNode *entry_op;
  OpNode *exit_op;

  virtual bool depends_on_cow()
  {
    return true;
  }

  /* Denotes whether COW component is to be tagged when this component
   * is tagged for update. */
  virtual bool need_tag_cow_before_update()
  {
    return true;
  }

  /* Denotes whether this component affects (possibly indirectly) on a
   * directly visible object. */
  bool affects_directly_visible;
};

/* ---------------------------------------- */

#define GRAPH_COMPONENT_NODE_DEFINE_TYPEINFO(NodeType, type_, type_name_, id_recalc_tag) \
  const Node::TypeInfo NodeType::typeinfo = Node::TypeInfo(type_, type_name_, id_recalc_tag)

#define GRAPH_COMPONENT_NODE_DECLARE DGRAPH_DEPSNODE_DECLARE

#define GRAPH_COMPONENT_NODE_DEFINE(name, NAME, id_recalc_tag) \
  GRAPH_COMPONENT_NODE_DEFINE_TYPEINFO( \
      name##ComponentNode, NodeType::NAME, #name " Component", id_recalc_tag); \
  static GraphNodeFactoryImpl<name##ComponentNode> DNTI_##NAME

#define GRAPH_COMPONENT_NODE_DECLARE_GENERIC(name) \
  struct name##ComponentNode : public ComponentNode { \
    GRAPH_COMPONENT_NODE_DECLARE; \
  }

#define GRAPH_COMPONENT_NODE_DECLARE_NO_COW_TAG_ON_UPDATE(name) \
  struct name##ComponentNode : public ComponentNode { \
    GRAPH_COMPONENT_NODE_DECLARE; \
    virtual bool need_tag_cow_before_update() \
    { \
      return false; \
    } \
  }

#define DEG_COMPONENT_NODE_DECLARE_NO_COW(name) \
  struct name##ComponentNode : public ComponentNode { \
    GRAPH_COMPONENT_NODE_DECLARE; \
    virtual bool depends_on_cow() \
    { \
      return false; \
    } \
  }

GRAPH_COMPONENT_NODE_DECLARE_GENERIC(Animation);
GRAPH_COMPONENT_NODE_DECLARE_NO_COW_TAG_ON_UPDATE(BatchCache);
GRAPH_COMPONENT_NODE_DECLARE_GENERIC(Cache);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(CopyOnWrite);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Geometry);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(ImageAnimation);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(LayerCollections);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Particles);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(ParticleSettings);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Pose);
DEGRAPH_COMPONENT_NODE_DECLARE_GENERIC(PointCache);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Sequencer);
DGRAPH_COMPONENT_NODE_DECLARE_NO_COW_TAG_ON_UPDATE(Shading);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(ShadingParameters);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Transform);
DGRAPH_COMPONENT_NODE_DECLARE_NO_COW_TAG_ON_UPDATE(ObjectFromLayer);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Dupli);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Synchronization);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Audio);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Armature);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(GenericDatablock);
DGRAPH_COMPONENT_NODE_DECLARE_NO_COW(Visibility);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(Simulation);
DGRAPH_COMPONENT_NODE_DECLARE_GENERIC(NTreeOutput);

/* Bone Component */
struct BoneComponentNode : public ComponentNode {
  /** Initialize 'bone component' node - from pointer data given. */
  void init(const Id *id, const char *subdata);

  struct PoseChannel *pchan; /* the bone that this component represents */

  GRAPH_COMPONENT_NODE_DECLARE;
};

/* Eventually we would not tag parameters in all cases.
 * Support for this each id needs to be added on an individual basis. */
struct ParamsComponentNode : public ComponentNode {
  virtual bool need_tag_cow_before_update() override
  {
    if (ID_TYPE_SUPPORTS_PARAMS_WITHOUT_COW(owner->id_type)) {
      /* Disabled as this is not true for newly added objects, needs investigation. */
      // lib_assert(graph_copy_on_write_is_expanded(owner->id_cow));
      return false;
    }
    return true;
  }

  GRAPH_COMPONENT_NODE_DECLARE;
};

void graph_register_component_nodes();

}  // namespace graph
}  // namespace dune
