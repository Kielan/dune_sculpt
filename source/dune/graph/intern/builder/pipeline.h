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
  AbstractBuilderPipeline(::DGraph *graph);
  virtual ~AbstractBuilderPipeline() = default;

  void build();

 protected:
  DGraph *dgraph_;
  Main *dmain_;
  Scene *scene_;
  ViewLayer *view_layer_;
  DGraphBuilderCache builder_cache_;

  virtual unique_ptr<DGraphNodeBuilder> construct_node_builder();
  virtual unique_ptr<DGraphRelationBuilder> construct_relation_builder();

  virtual void build_step_sanity_check();
  void build_step_nodes();
  void build_step_relations();
  void build_step_finalize();

  virtual void build_nodes(DGraphNodeBuilder &node_builder) = 0;
  virtual void build_relations(DGraphRelationBuilder &relation_builder) = 0;
};

}  // namespace dune::dgraph
