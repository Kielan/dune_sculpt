#include "node_fn_util.hh"
#include "node_util.hh"

#include "node_socket_search_link.hh"

static bool fn_node_poll_default(const NodeType * /*ntype*/,
                                 const NodeTree *ntree,
                                 const char **r_disabled_hint)
{
  /* Fn nodes are only supported in simulation node trees so far. */
  if (!STREQ(ntree->idname, "GeoNodeTree")) {
    *r_disabled_hint = RPT_("Not a geometry node tree");
    return false;
  }
  return true;
}

void fn_node_type_base(NodeType *ntype, int type, const char *name, short nclass)
{
  dune::node_type_base(ntype, type, name, nclass);
  ntype->poll = fn_node_poll_default;
  ntype->insert_link = node_insert_link_default;
  ntype->gather_link_search_ops = dune::nodes::search_link_ops_for_basic_node;
}
