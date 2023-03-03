#pragma once

#include "pipeline.h"

struct DNodeTree;

namespace dune::dgraph {

class CompositorBuilderPipeline : public AbstractBuilderPipeline {
 public:
  CompositorBuilderPipeline(::DGraph *graph, DNodeTree *nodetree);

 protected:
  virtual void build_nodes(DGraphNodeBuilder &node_builder) override;
  virtual void build_relations(DGraphRelationBuilder &relation_builder) override;

 private:
  DNodeTree *nodetree_;
};

}  // namespace dune::dgraph
