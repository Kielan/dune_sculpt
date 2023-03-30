#pragma once

#include "pipeline.h"

namespace dune::graph {

class RenderBuilderPipeline : public AbstractBuilderPipeline {
 public:
  RenderBuilderPipeline(::Graph *graph);

 protected:
  virtual void build_nodes(GraphNodeBuilder &node_builder) override;
  virtual void build_relations(GraphRelationBuilder &relation_builder) override;
};

}  // namespace dune::graph
