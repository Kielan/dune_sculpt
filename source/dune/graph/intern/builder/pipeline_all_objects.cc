#include "pipeline_all_objects.h"

#include "intern/builder/graph_builder_nodes.h"
#include "intern/builder/graph_builder_relations.h"
#include "intern/graph.h"

#include "types_layer.h"

namespace dune::graph {

namespace {

class AllObjectsNodeBuilder : public GraphNodeBuilder {
 public:
  AllObjectsNodeBuilder(Main *dmain, Graph *graph, GraphBuilderCache *cache)
      : GraphNodeBuilder(dmain, graph, cache)
  {
  }

  bool need_pull_base_into_graph(const Base * /*base*/) override
  {
    return true;
  }
};

class AllObjectsRelationBuilder : public GraphRelationBuilder {
 public:
  AllObjectsRelationBuilder(Main *dmain, Graph *graph, GraphBuilderCache *cache)
      : GraphRelationBuilder(dmain, graph, cache)
  {
  }

  bool need_pull_base_into_graph(const Base * /*base*/) override
  {
    return true;
  }
};

}  // namespace

AllObjectsBuilderPipeline::AllObjectsBuilderPipeline(::DGraph *graph)
    : ViewLayerBuilderPipeline(graph)
{
}

unique_ptr<GraphNodeBuilder> AllObjectsBuilderPipeline::construct_node_builder()
{
  return std::make_unique<AllObjectsNodeBuilder>(dmain_, graph_, &builder_cache_);
}

unique_ptr<GraphRelationBuilder> AllObjectsBuilderPipeline::construct_relation_builder()
{
  return std::make_unique<AllObjectsRelationBuilder>(dmain_, graph_, &builder_cache_);
}

}  // namespace dune::graph
