#pragma once

#include "pipeline.h"

struct NodeTree;

namespace dune::graph {

class CompositorBuilderPipeline : public AbstractBuilderPipeline {
 public:
  CompositorBuilderPipeline(::Graph *graph, NodeTree *nodetree);

 protected:
  virtual void build_nodes(GraphNodeBuilder &node_builder) override;
  virtual void build_relations(GraphRelationBuilder &relation_builder) override;

 private:
  NodeTree *nodetree_;
};

}  // namespace dune::graph
