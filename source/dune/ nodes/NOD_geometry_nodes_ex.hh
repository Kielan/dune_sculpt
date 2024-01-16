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
struct GeometrySet;
}
struct IdProp;
struct Ob;
namespace dune::nodes {
struct GeoNodesCallData;
namespace geo_eval_log {
class GeoModifierLog;
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

/* return Whether using an attribute to input vals of this type is supported. */
bool socket_type_has_attribute_toggle(eNodeSocketDatatype type);

/* return Whether using an attribute to input values of this type is supported, and the node
 * group's input for this socket accepts a field rather than just single values. */
bool input_has_attribute_toggle(const bNodeTree &node_tree, const int socket_index);

bool id_property_type_matches_socket(const bNodeTreeInterfaceSocket &socket,
                                     const IdProp &prop);

std::unique_ptr<IDProperty, bke::idprop::IDPropertyDeleter> id_property_create_from_socket(
    const bNodeTreeInterfaceSocket &socket);

dune::GeometrySet ex_geometry_nodes_on_geometry(const bNodeTree &btree,
                                                    const IDProperty *properties,
                                                    const ComputeContext &base_compute_context,
                                                    GeoNodesCallData &call_data,
                                                    bke::GeometrySet input_geometry);

void update_input_properties_from_node_tree(const bNodeTree &tree,
                                            const IDProperty *old_properties,
                                            bool use_bool_for_use_attribute,
                                            IDProperty &properties);

void update_output_properties_from_node_tree(const bNodeTree &tree,
                                             const IDProperty *old_properties,
                                             IDProperty &properties);

}  // namespace blender::nodes
