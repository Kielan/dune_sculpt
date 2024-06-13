#pragma once

#include "lib_compute_cxt.hh"
#include "lib_fn_ref.hh"
#include "lib_multi_val_map.hh"
#include "lib_set.hh"

#include "dune_idprop.hh"
#include "dune_node.h"

struct NodeTree;
struct NodeSocket;
struct NodeTreeInterfaceSocket;
struct Graph;
namespace dune {
struct GeoSet;
}
struct IdProp;
struct Ob;
namespace dune::nodes {
struct GeoNodesCallData;
namespace geo_eval_log {
class GeoModLog;
}  // namespace geo_eval_log
}  // namespace dune::nodes

namespace dune::nodes {

void find_node_tree_deps(const NodeTree &tree,
                         Set<Id *> &r_ids,
                         bool &r_needs_own_transform_relation,
                         bool &r_needs_scene_camera_relation);
StringRef input_use_attr_suffix();
StringRef input_attr_name_suffix();

std::optional<StringRef> input_attr_name_get(const IdProp &props,
                                             const NodeTreeInterfaceSocket &io_input);

/* return Whether using an attr to input vals of this type is supported. */
bool socket_type_has_attr_toggle(eNodeSocketDatatype type);

/* return Whether using an attr to input vals of this type is supported, and the node
 * group's input for this socket accepts a field rather than just single vals. */
bool input_has_attr_toggle(const NodeTree &node_tree, const int socket_index);

bool id_prop_type_matches_socket(const NodeTreeInterfaceSocket &socket,
                                     const IdProp &prop);

std::unique_ptr<IdProp, dune::idprop::IdPropDeleter> id_prop_create_from_socket(
    const NodeTreeInterfaceSocket &socket);

dune::GeoSet ex_geo_nodes_on_geo(const NodeTree &tree,
                                 const IdProp *props,
                                 const ComputeCxt &base_compute_cxt,
                                 GeoNodesCallData &call_data,
                                 dune::GeoSet input_geo);

void update_input_props_from_node_tree(const NodeTree &tree,
                                       const IdProp *old_props,
                                       bool use_bool_for_use_attr,
                                       IdProp &props);

void update_output_props_from_node_tree(const NodeTree &tree,
                                        const IdProp *old_props,
                                        IdProp &props);

}  // namespace dune::nodes
