#include "intern/node/dgraph_node_time.h"

#include "types_scene.h"

#include "intern/dgraph.h"
#include "intern/dgraph_relation.h"

namespace dune::dgraph {

void TimeSourceNode::tag_update(DGraph * /*graph*/, eUpdateSource /*source*/)
{
  tagged_for_update = true;
}

void TimeSourceNode::flush_update_tag(DGraph *graph)
{
  if (!tagged_for_update) {
    return;
  }
  for (Relation *rel : outlinks) {
    Node *node = rel->to;
    node->tag_update(graph, DGRAPH_UPDATE_SOURCE_TIME);
  }
}

}  // namespace dune::dgraph
