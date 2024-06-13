#include "dune_node.hh"

#include "node_geo.hh"
#include "node_decl.hh"

#include "node_common.h"
#include "node_geo_util.hh"

#include "api_access.hh"

namespace dune::nodes {

static void register_node_type_geo_group()
{
  static NodeType ntype;

  node_type_base_custom(&ntype, "GeoNodeGroup", "Group", "GROUP", NODE_CLASS_GROUP);
  ntype.type = NODE_GROUP;
  ntype.poll = geo_node_poll_default;
  ntype.poll_instance = node_group_poll_instance;
  ntype.insert_link = node_insert_link_default;
  ntype.api_ext.sapi = api_struct_find("GeoNodeGroup");
  lib_assert(ntype.api_ext.sapi != nullptr);
  api_struct_dune_type_set(ntype.api_ext.sapi, &ntype);

  dune::node_type_size(&ntype, 140, 60, 400);
  ntype.labelfn = node_group_label;
  ntype.decl = node_group_decl;

  nodeRegisterType(&ntype);
}
REGISTER_NODE(register_node_type_geo_group)

}  // namespace dune::nodes

void register_node_type_geo_custom_group(NodeType *ntype)
{
  /* These methods can be overridden but need a default implementation otherwise. */
  if (ntype->poll == nullptr) {
    ntype->poll = geo_node_poll_default;
  }
  if (ntype->insert_link == nullptr) {
    ntype->insert_link = node_insert_link_default;
  }
  ntype->declare = dune::nodes::node_group_declare;
}
