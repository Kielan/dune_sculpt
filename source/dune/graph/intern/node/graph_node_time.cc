#include "intern/node/graph_node_time.h"

#include "types_scene.h"

#include "intern/graph.h"
#include "intern/graph_relation.h"

namespace dune::graph {

void TimeSourceNode::tag_update(Graph * /*graph*/, eUpdateSource /*source*/)
{
  tagged_for_update = true;
}

void TimeSourceNode::flush_update_tag(Graph *graph)
{
  if (!tagged_for_update) {
    return;
  }
  for (Relation *rel : outlinks) {
    Node *node = rel->to;
    node->tag_update(graph, GRAPH_UPDATE_SOURCE_TIME);
  }
}

}  // namespace dune::graph
