#pragma once

#include "lib_ghash.h"
#include "lib_sys_types.h"
#include "dune_id.h"
#include "intern/node/dgraph_node.h"

namespace dune {
namespace dgraph {

struct ComponentNode;

typedef uint64_t IdComponentsMask;

/* NOTE: We use max comparison to mark an id node that is linked more than once
 * So keep this enum ordered accordingly. */
enum eDGraphNodeLinkedState_Type {
  /* Generic indirectly linked id node. */
  DGRAPH_ID_LINKED_INDIRECTLY = 0,
  /* Id node present in the set (background) only. */
  DGRAPH_ID_LINKED_VIA_SET = 1,
  /* Id node directly linked via the SceneLayer. */
  DGRAPH_ID_LINKED_DIRECTLY = 2,
};
const char *linkedStateAsString(eDGraphNodeLinkedStateType linked_state);

/* id-block Reference */
struct IsNode : public Node {
  struct ComponentIdKey {
    ComponentIdKey(NodeType type, const char *name = "");
    uint64_t hash() const;
    bool op==(const ComponentIdKey &other) const;

    NodeType type;
    const char *name;
  };

  /** Initialize 'id' node - from pointer data given. */
  virtual void init(const Id *id, const char *subdata) override;
  void init_copy_on_write(Id *id_cow_hint = nullptr);
  ~IdNode();
  void destroy();

  virtual string id() const override;

  ComponentNode *find_component(NodeType type, const char *name = "") const;
  ComponentNode *add_component(NodeType type, const char *name = "");

  virtual void tag_update(DGraph *graph, eUpdateSource source) override;

  void finalize_build(DGraph *graph);

  IdComponentsMask get_visible_components_mask() const;

  /* Type of the id stored separately, so it's possible to perform check whether CoW is needed
   * without de-referencing the id_cow (which is not safe when ID is NOT covered by CoW and has
   * been deleted from the main database.) */
  IdType id_type;

  /* ID Block referenced. */
  Id *id_orig;

  /* Session-wide UUID of the id_orig.
   * Is used on relations update to map evaluated state from old nodes to the new ones, without
   * relying on pointers (which are not guaranteed to be unique) and without dereferencing id_orig
   * which could be "stale" pointer. */
  uint id_orig_session_uuid;

  /* Evaluated data-block.
   * Will be covered by the copy-on-write system if the ID Type needs it. */
  Id *id_cow;

  /* Hash to make it faster to look up components. */
  Map<ComponentIDKey, ComponentNode *> components;

  /* Additional flags needed for scene evaluation.
   * TODO: Only needed for until really granular updates
   * of all the entities. */
  uint32_t eval_flags;
  uint32_t previous_eval_flags;

  /* Extra customdata mask which needs to be evaluated for the mesh object. */
  DGraphCustomDataMeshMasks customdata_masks;
  DGraphCustomDataMeshMasks previous_customdata_masks;

  eDGraphNodeLinkedStateType linked_state;

  /* Indicates the data-block is visible in the evaluated scene. */
  bool is_directly_visible;

  /* For the collection type of id, denotes whether collection was fully
   * recursed into. */
  bool is_collection_fully_expanded;

  /* Is used to figure out whether object came to the dependency graph via a base. */
  bool has_base;

  /* Accumulated flag from operation. Is initialized and used during updates flush. */
  bool is_user_modified;

  /* Copy-on-Write component has been explicitly tagged for update. */
  bool is_cow_explicitly_tagged;

  /* Accumulate recalc flags from multiple update passes. */
  int id_cow_recalc_backup;

  IdComponentsMask visible_components_mask;
  IdComponentsMask previously_visible_components_mask;

  DGRAPH_NODE_DECLARE;
};

}  // namespace dgraph
}  // namespace dune
