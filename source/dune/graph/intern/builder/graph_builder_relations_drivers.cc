/**  Methods for constructing depsgraph relations for drivers. **/

#include "intern/builder/graph_builder_relations_drivers.h"

#include <cstring>

#include "types_anim.h"

#include "dune_anim_data.h"

#include "intern/builder/dgraph_builder_relations.h"
#include "intern/dgraph_relation.h"
#include "intern/node/dgraph_node.h"

namespace dune::dgraph {

DriverDescriptor::DriverDescriptor(ApiPtr *id_ptr, FCurve *fcu)
    : id_ptr_(id_ptr),
      fcu_(fcu),
      driver_relations_needed_(false),
      ptr_api_(),
      prop_api_(nullptr),
      is_array_(false)
{
  driver_relations_needed_ = determine_relations_needed();
  split_api_path();
}

bool DriverDescriptor::determine_relations_needed()
{
  if (fcu_->array_index > 0) {
    /* Drivers on array elements always need relations. */
    is_array_ = true;
    return true;
  }

  if (!resolve_api()) {
    /* Properties that don't exist can't cause threading issues either. */
    return false;
  }

  if (api_prop_array_check(prop_api_)) {
    /* Drivers on array elements always need relations. */
    is_array_ = true;
    return true;
  }

  /* Drivers on Booleans and Enums (when used as bitflags) can write to the same memory location,
   * so they need relations between each other. */
  return ELEM(api_prop_type(prop_api_), PROP_BOOLEAN, PROP_ENUM);
}

bool DriverDescriptor::driver_relations_needed() const
{
  return driver_relations_needed_;
}

bool DriverDescriptor::is_array() const
{
  return is_array_;
}

bool DriverDescriptor::is_same_array_as(const DriverDescriptor &other) const
{
  if (!is_array_ || !other.is_array_) {
    return false;
  }
  return api_suffix == other.api_suffix;
}

OpKey DriverDescriptor::dgraph_key() const
{
  return OpKey(id_ptr_->owner_id,
                      NodeType::PARAMS,
                      OpCode::DRIVER,
                      fcu_->api_path,
                      fcu_->array_index);
}

void DriverDescriptor::split_api_path()
{
  const char *last_dot = strrchr(fcu_->api_path, '.');
  if (last_dot == nullptr || last_dot[1] == '\0') {
    api_prefix = StringRef();
    api_suffix = StringRef(fcu_->api_path);
    return;
  }

  api_prefix = StringRef(fcu_->api_path, last_dot);
  api_suffix = StringRef(last_dot + 1);
}

bool DriverDescriptor::resolve_api()
{
  return api_path_resolve_prop(id_ptr_, fcu_->api_path, &ptr_api_, &prop_api_);
}

static bool is_reachable(const Node *const from, const Node *const to)
{
  if (from == to) {
    return true;
  }

  /* Perform a graph walk from 'to' towards its incoming connections.
   * Walking from 'from' towards its outgoing connections is 10x slower on the Spring rig. */
  deque<const Node *> queue;
  Set<const Node *> seen;
  queue.push_back(to);
  while (!queue.empty()) {
    /* Visit the next node to inspect. */
    const Node *visit = queue.back();
    queue.pop_back();

    if (visit == from) {
      return true;
    }

    /* Queue all incoming relations that we haven't seen before. */
    for (Relation *relation : visit->inlinks) {
      const Node *prev_node = relation->from;
      if (seen.add(prev_node)) {
        queue.push_back(prev_node);
      }
    }
  }
  return false;
}

/* **** DGraphRelationBuilder functions **** */

void DGraphRelationBuilder::build_driver_relations()
{
  for (IdNode *id_node : graph_->id_nodes) {
    build_driver_relations(id_node);
  }
}

void DGraphRelationBuilder::build_driver_relations(IdNode *id_node)
{
  /* Add relations between drivers that write to the same datablock.
   *
   * This prevents threading issues when two separate RNA properties write to
   * the same memory address. For example:
   * - Drivers on individual array elements, as the animation system will write
   *   the whole array back to RNA even when changing individual array value.
   * - Drivers on RNA properties that map to a single bit flag. Changing the RNA
   *   value will write the entire int containing the bit, in a non-thread-safe
   *   way.
   */
  Id *id_orig = id_node->id_orig;
  AnimData *adt = dune_animdata_from_id(id_orig);
  if (adt == nullptr) {
    return;
  }

  /* Mapping from api prefix -> set of driver descriptors: */
  Map<string, Vector<DriverDescriptor>> driver_groups;

  ApiPtr id_ptr;
  api_id_ptr_create(id_orig, &id_ptr);

  LISTBASE_FOREACH (FCurve *, fcu, &adt->drivers) {
    if (fcu->api_path == nullptr) {
      continue;
    }

    DriverDescriptor driver_desc(&id_ptr, fcu);
    if (!driver_desc.driver_relations_needed()) {
      continue;
    }

    driver_groups.lookup_or_add_default_as(driver_desc.api_prefix).append(driver_desc);
  }

  for (Span<DriverDescriptor> prefix_group : driver_groups.values()) {
    /* For each node in the driver group, try to connect it to another node
     * in the same group without creating any cycles. */
    int num_drivers = prefix_group.size();
    if (num_drivers < 2) {
      /* A relation requires two drivers. */
      continue;
    }
    for (int from_index = 0; from_index < num_drivers; ++from_index) {
      const DriverDescriptor &driver_from = prefix_group[from_index];
      Node *op_from = get_node(driver_from.dgraph_key());

      /* Start by trying the next node in the group. */
      for (int to_offset = 1; to_offset < num_drivers; ++to_offset) {
        const int to_index = (from_index + to_offset) % num_drivers;
        const DriverDescriptor &driver_to = prefix_group[to_index];
        Node *op_to = get_node(driver_to.dgraph_key());

        /* Duplicate drivers can exist (see #78615), but cannot be distinguished by OpKey
         * and thus have the same depsgraph node. Relations between those drivers should not be
         * created. This not something that is expected to happen (both the UI and the Python API
         * prevent duplicate drivers), it did happen in a file and it is easy to deal with here. */
        if (op_from == op_to) {
          continue;
        }

        if (from_index < to_index && driver_from.is_same_array_as(driver_to)) {
          /* This is for adding a relation like `color[0]` -> `color[1]`.
           * When the search for another driver wraps around,
           * we cannot blindly add relations any more. */
        }
        else {
          /* Investigate whether this relation would create a dependency cycle.
           * Example graph:
           *     A -> B -> C
           * and investigating a potential connection C->A. Because A->C is an
           * existing transitive connection, adding C->A would create a cycle. */
          if (is_reachable(op_to, op_from)) {
            continue;
          }

          /* No need to directly connect this node if there is already a transitive connection. */
          if (is_reachable(op_from, op_to)) {
            break;
          }
        }

        add_op_relation(
            op_from->get_exit_op(), op_to->get_entry_op(), "Driver Serialization");
        break;
      }
    }
  }
}

}  // namespace dune::dgraph
