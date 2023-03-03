#pragma once

#include "pipeline.h"

namespace dune::dgraph {

/* Optimized builders for dependency graph built from a given set of IDs.
 *
 * General notes:
 *
 * - We pull in all bases if their objects are in the set of IDs. This allows to have proper
 *   visibility and other flags assigned to the objects.
 *   All other bases (the ones which points to object which is outside of the set of IDs) are
 *   completely ignored.
 */

class FromIdsBuilderPipeline : public AbstractBuilderPipeline {
 public:
  FromIdsBuilderPipeline(::DGraph *graph, Span<Id *> ids);

 protected:
  virtual unique_ptr<DGraphNodeBuilder> construct_node_builder() override;
  virtual unique_ptr<DGraphRelationBuilder> construct_relation_builder() override;

  virtual void build_nodes(DGraphNodeBuilder &node_builder) override;
  virtual void build_relations(DGraphRelationBuilder &relation_builder) override;

 private:
  Span<Id *> ids_;
};

}  // namespace dune::dgraph
