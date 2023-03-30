#pragma once

#include "pipeline.h"

namespace dune::graph {

class ViewLayerBuilderPipeline : public AbstractBuilderPipeline {
 public:
  ViewLayerBuilderPipeline(::Graph *graph);

 protected:
  virtual void build_nodes(GraphNodeBuilder &node_builder) override;
  virtual void build_relations(GraphRelationBuilder &relation_builder) override;
};

}  // namespace dune::graph
