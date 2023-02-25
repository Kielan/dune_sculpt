#pragma once

#include "pipeline.h"

namespace dune::deg {

class RenderBuilderPipeline : public AbstractBuilderPipeline {
 public:
  RenderBuilderPipeline(::Depsgraph *graph);

 protected:
  virtual void build_nodes(DepsgraphNodeBuilder &node_builder) override;
  virtual void build_relations(DepsgraphRelationBuilder &relation_builder) override;
};

}  // namespace dune::deg
