#pragma once

#include "pipeline_view_layer.h"

namespace dune::graph {

/* Builds a dependency graph that contains all objects in the view layer.
 * This is contrary to the regular ViewLayerBuilderPipeline, which is limited to visible objects
 * (and their dependencies). */
class AllObjectsBuilderPipeline : public ViewLayerBuilderPipeline {
 public:
  AllObjectsBuilderPipeline(::Graph *graph);

 protected:
  virtual unique_ptr<GraphraphNodeBuilder> construct_node_builder() override;
  virtual unique_ptr<GraphRelationBuilder> construct_relation_builder() override;
};

}  // namespace dune::graph
