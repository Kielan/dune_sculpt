#pragma once

#include "pipeline.h"

namespace dune::dgraph {

class RenderBuilderPipeline : public AbstractBuilderPipeline {
 public:
  RenderBuilderPipeline(::DGraph *graph);

 protected:
  virtual void build_nodes(DGraphNodeBuilder &node_builder) override;
  virtual void build_relations(DGraphRelationBuilder &relation_builder) override;
};

}  // namespace dune::dgraph
