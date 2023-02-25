#include "intern/node/deg_node_time.h"

#include "types_scene.h"

#include "intern/depsgraph.h"
#include "intern/depsgraph_relation.h"

namespace dune::deg {

void TimeSourceNode::tag_update(Depsgraph * /*graph*/, eUpdateSource /*source*/)
{
  tagged_for_update = true;
}

void TimeSourceNode::flush_update_tag(Depsgraph *graph)
{
  if (!tagged_for_update) {
    return;
  }
  for (Relation *rel : outlinks) {
    Node *node = rel->to;
    node->tag_update(graph, DEG_UPDATE_SOURCE_TIME);
  }
}

}  // namespace dune::deg
