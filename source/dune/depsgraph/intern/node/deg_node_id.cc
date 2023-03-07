#include "intern/node/dgraph_node_id.h"

#include <cstdio>
#include <cstring> /* required for STREQ later on. */

#include "lib_string.h"
#include "lib_utildefines.h"

#include "types_id.h"
#include "types_anim.h"

#include "dune_lib_id.h"

#include "dgraph.h"

#include "intern/eval/dgraph_eval_copy_on_write.h"
#include "intern/node/dgraph_node_component.h"
#include "intern/node/dgraph_node_factory.h"
#include "intern/node/dgraph_node_time.h"

namespace dune::dgraph {

const char *linkedStateAsString(eDGraphNodeLinkedStateType linked_state)
{
  switch (linked_state) {
    case DGRAPH_ID_LINKED_INDIRECTLY:
      return "INDIRECTLY";
    case DGRAPH_ID_LINKED_VIA_SET:
      return "VIA_SET";
    case DGRAPH_ID_LINKED_DIRECTLY:
      return "DIRECTLY";
  }
  lib_assert_msg(0, "Unhandled linked state, should never happen.");
  return "UNKNOWN";
}

IdNode::ComponentIdKey::ComponentIdKey(NodeType type, const char *name) : type(type), name(name)
{
}

bool IdNode::ComponentIdKey::op==(const ComponentIdKey &other) const
{
  return type == other.type && STREQ(name, other.name);
}

uint64_t IdNode::ComponentIdKey::hash() const
{
  const int type_as_int = static_cast<int>(type);
  return lib_ghashutil_combine_hash(lib_ghashutil_uinthash(type_as_int),
                                    lib_ghashutil_strhash_p(name));
}

void IdNode::init(const Id *id, const char *UNUSED(subdata))
{
  lib_assert(id != nullptr);
  /* Store id-pointer. */
  id_type = GS(id->name);
  id_orig = (Id *)id;
  id_orig_session_uuid = id->session_uuid;
  eval_flags = 0;
  previous_eval_flags = 0;
  customdata_masks = DGraphCustomDataMeshMasks();
  previous_customdata_masks = DGraphCustomDataMeshMasks();
  linked_state = DGRAPH_ID_LINKED_INDIRECTLY;
  is_directly_visible = true;
  is_collection_fully_expanded = false;
  has_base = false;
  is_user_modified = false;
  id_cow_recalc_backup = 0;

  visible_components_mask = 0;
  previously_visible_components_mask = 0;
}

void IdNode::init_copy_on_write(Id *id_cow_hint)
{
  /* Create pointer as early as possible, so we can use it for function
   * bindings. Rest of data we'll be copying to the new datablock when
   * it is actually needed. */
  if (id_cow_hint != nullptr) {
    // lib_assert(dgraph_copy_on_write_is_needed(id_orig));
    if (dgraph_copy_on_write_is_needed(id_orig)) {
      id_cow = id_cow_hint;
    }
    else {
      id_cow = id_orig;
    }
  }
  else if (dgraph_copy_on_write_is_needed(id_orig)) {
    id_cow = (Id *)dune_libblock_alloc_notest(GS(id_orig->name));
    DGRAPH_COW_PRINT(
        "Create shallow copy for %s: id_orig=%p id_cow=%p\n", id_orig->name, id_orig, id_cow);
    dgraph_tag_copy_on_write_id(id_cow, id_orig);
  }
  else {
    id_cow = id_orig;
  }
}

/* Free 'id' node. */
IdNode::~IdNode()
{
  destroy();
}

void IdNode::destroy()
{
  if (id_orig == nullptr) {
    return;
  }

  for (ComponentNode *comp_node : components.values()) {
    delete comp_node;
  }

  /* Free memory used by this CoW ID. */
  if (!ELEM(id_cow, id_orig, nullptr)) {
    dgraph_free_copy_on_write_datablock(id_cow);
    MEM_freeN(id_cow);
    id_cow = nullptr;
    DGRAPH_COW_PRINT("Destroy CoW for %s: id_orig=%p id_cow=%p\n", id_orig->name, id_orig, id_cow);
  }

  /* Tag that the node is freed. */
  id_orig = nullptr;
}

string IDNode::id() const
{
  char orig_ptr[24], cow_ptr[24];
  lib_snprintf(orig_ptr, sizeof(orig_ptr), "%p", id_orig);
  lib_snprintf(cow_ptr, sizeof(cow_ptr), "%p", id_cow);
  return string(nodeTypeAsString(type)) + " : " + name + " (orig: " + orig_ptr +
         ", eval: " + cow_ptr + ", is_directly_visible " +
         (is_directly_visible ? "true" : "false") + ")";
}

ComponentNode *IdNode::find_component(NodeType type, const char *name) const
{
  ComponentIdKey key(type, name);
  return components.lookup_default(key, nullptr);
}

ComponentNode *IdNode::add_component(NodeType type, const char *name)
{
  ComponentNode *comp_node = find_component(type, name);
  if (!comp_node) {
    DGraphNodeFactory *factory = type_get_factory(type);
    comp_node = (ComponentNode *)factory->create_node(this->id_orig, "", name);

    /* Register. */
    ComponentIdKey key(type, name);
    components.add_new(key, comp_node);
    comp_node->owner = this;
  }
  return comp_node;
}

void IdNode::tag_update(DGraph *graph, eUpdateSource source)
{
  for (ComponentNode *comp_node : components.values()) {
    /* Relations update does explicit animation update when needed. Here we ignore animation
     * component to avoid loss of possible unkeyed changes. */
    if (comp_node->type == NodeType::ANIMATION && source == DEG_UPDATE_SOURCE_RELATIONS) {
      continue;
    }
    comp_node->tag_update(graph, source);
  }
}

void IDNode::finalize_build(Depsgraph *graph)
{
  /* Finalize build of all components. */
  for (ComponentNode *comp_node : components.values()) {
    comp_node->finalize_build(graph);
  }
  visible_components_mask = get_visible_components_mask();
}

IDComponentsMask IDNode::get_visible_components_mask() const
{
  IDComponentsMask result = 0;
  for (ComponentNode *comp_node : components.values()) {
    if (comp_node->affects_directly_visible) {
      const int component_type_as_int = static_cast<int>(comp_node->type);
      lib_assert(component_type_as_int < 64);
      result |= (1ULL << component_type_as_int);
    }
  }
  return result;
}

}  // namespace dune::dgraph
