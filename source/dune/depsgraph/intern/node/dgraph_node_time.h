#pragma once

#include "intern/node/dgraph_node.h"

namespace dune {
namespace dgraph {

/* Time Source Node. */
struct TimeSourceNode : public Node {
  bool tagged_for_update = false;

  // TODO: evaluate() operation needed

  virtual void tag_update(DGraph *graph, eUpdateSource source) override;

  void flush_update_tag(DGraph *graph);

  DEG_DEPSNODE_DECLARE;
};

}  // namespace deg
}  // namespace dune
