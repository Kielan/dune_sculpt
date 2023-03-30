#pragma once

#include "graph_builder_cache.h"

#include "intern/graph_type.h"

struct Graph;
struct Main;
struct Scene;
struct ViewLayer;

namespace dune::graph {

struct Graph;
class GraphNodeBuilder;
class GraphRelationBuilder;

/* Base class for DGraph Builder pipelines.
 *
 * Basically it runs through the following steps:
 * - sanity check
 * - build nodes
 * - build relations
 * - finalize
 */
class AbstractBuilderPipeline {
 public:
  AbstractBuilderPipeline(::Graph *graph);
  virtual ~AbstractBuilderPipeline() = default;

  void build();

 protected:
  Graph *graph_;
  Main *dmain_;
  Scene *scene_;
  ViewLayer *view_layer_;
  GraphBuilderCache builder_cache_;

  virtual unique_ptr<GraphNodeBuilder> construct_node_builder();
  virtual unique_ptr<GraphRelationBuilder> construct_relation_builder();

  virtual void build_step_sanity_check();
  void build_step_nodes();
  void build_step_relations();
  void build_step_finalize();

  virtual void build_nodes(GraphNodeBuilder &node_builder) = 0;
  virtual void build_relations(GraphRelationBuilder &relation_builder) = 0;
};

}  // namespace dune::graph
