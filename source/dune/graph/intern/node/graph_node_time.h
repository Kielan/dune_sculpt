#pragma once

#include "intern/node/graph_node.h"

namespace dune {
namespace graph {

/* Time Source Node. */
struct TimeSourceNode : public Node {
  bool tagged_for_update = false;

  // TODO: evaluate() operation needed

  virtual void tag_update(Graph *graph, eUpdateSource source) override;

  void flush_update_tag(Graph *graph);

  GRAPH_NODE_DECLARE;
};

}  // namespace graph
}  // namespace dune
