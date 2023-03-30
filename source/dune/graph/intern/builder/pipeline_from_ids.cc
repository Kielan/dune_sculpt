#include "pipeline_from_ids.h"

#include "types_layer.h"

#include "intern/builder/graph_builder_nodes.h"
#include "intern/builder/graph_builder_relations.h"
#include "intern/graph.h"

namespace dune::graph {

namespace {

class GraphFromIdsFilter {
 public:
  GraphFromIdsFilter(Span<Id *> ids)
  {
    ids_.add_multiple(ids);
  }

  bool contains(Id *id)
  {
    return ids_.contains(id);
  }

 protected:
  Set<Id *> ids_;
};

class GraphFromIdsNodeBuilder : public DGraphNodeBuilder {
 public:
  GraphFromIdsNodeBuilder(Main *dmain,
                          Graph *graph,
                          GraphBuilderCache *cache,
                          Span<Id *> ids)
      : GraphNodeBuilder(dmain, graph, cache), filter_(ids)
  {
  }

  bool need_pull_base_into_graph(const Base *base) override
  {
    if (!filter_.contains(&base->object->id)) {
      return false;
    }
    return DGraphNodeBuilder::need_pull_base_into_graph(base);
  }

 protected:
  DepsGraphFromIdsFilter filter_;
};

class DGraphFromIdsRelationBuilder : public DGraphRelationBuilder {
 public:
  DGraphFromIdsRelationBuilder(Main *dmain,
                               DGraph *graph,
                               DGraphBuilderCache *cache,
                               Span<Id *> ids)
      : DGraphRelationBuilder(dmain, graph, cache), filter_(ids)
  {
  }

  bool need_pull_base_into_graph(const Base *base) override
  {
    if (!filter_.contains(&base->object->id)) {
      return false;
    }
    return DGraphRelationBuilder::need_pull_base_into_graph(base);
  }

 protected:
  DGraphFromIdsFilter filter_;
};

}  // namespace

FromIdsBuilderPipeline::FromIdsBuilderPipeline(::DGraph *graph, Span<Id *> ids)
    : AbstractBuilderPipeline(graph), ids_(ids)
{
}

unique_ptr<DGraphNodeBuilder> FromIdsBuilderPipeline::construct_node_builder()
{
  return std::make_unique<DGraphFromIdsNodeBuilder>(dmain_, dgraph_, &builder_cache_, ids_);
}

unique_ptr<DGraphRelationBuilder> FromIdsBuilderPipeline::construct_relation_builder()
{
  return std::make_unique<DGraphFromIdsRelationBuilder>(
      dmain_, dgraph_, &builder_cache_, ids_);
}

void FromIdsBuilderPipeline::build_nodes(DGraphNodeBuilder &node_builder)
{
  node_builder.build_view_layer(scene_, view_layer_, DGRAPH_ID_LINKED_DIRECTLY);
  for (Id *id : ids_) {
    node_builder.build_id(id);
  }
}

void FromIdsBuilderPipeline::build_relations(DGraphRelationBuilder &relation_builder)
{
  relation_builder.build_view_layer(scene_, view_layer_, DGRAPH_ID_LINKED_DIRECTLY);
  for (Id *id : ids_) {
    relation_builder.build_id(id);
  }
}

}  // namespace dune::dgraph
