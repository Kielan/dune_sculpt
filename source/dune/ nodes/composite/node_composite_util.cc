#include "dune_node_runtime.hh"

#include "NOD_socket_search_link.hh"

#include "node_composite_util.hh"

bool cmp_node_poll_default(const NodeType * /*ntype*/,
                           const NodeTree *ntree,
                           const char **r_disabled_hint)
{
  if (!STREQ(ntree->idname, "CompositorNodeTree")) {
    *r_disabled_hint = RPT_("Not a compositor node tree");
    return false;
  }
  return true;
}

void cmp_node_update_default(NodeTree * /*ntree*/, Node *node)
{
  node->runtime->need_ex = 1;
}

void cmp_node_type_base(NodeType *ntype, int type, const char *name, short nclass)
{
  dune::node_type_base(ntype, type, name, nclass);

  ntype->poll = cmp_node_poll_default;
  ntype->updatefn = cmp_node_update_default;
  ntype->insert_link = node_insert_link_default;
  ntype->gather_link_search_ops = dune::nodes::search_link_ops_for_basic_node;
}
