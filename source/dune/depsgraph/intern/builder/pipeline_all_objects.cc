#include "pipeline_all_objects.h"

#include "intern/builder/dgraph_builder_nodes.h"
#include "intern/builder/dgraph_builder_relations.h"
#include "intern/dgraph.h"

#include "types_layer.h"

namespace dune::dgraph {

namespace {

class AllObjectsNodeBuilder : public DGraphNodeBuilder {
 public:
  AllObjectsNodeBuilder(Main *dmain, DGraph *graph, DGraphBuilderCache *cache)
      : DGraphNodeBuilder(dmain, graph, cache)
  {
  }

  bool need_pull_base_into_graph(const Base * /*base*/) override
  {
    return true;
  }
};

class AllObjectsRelationBuilder : public DGraphRelationBuilder {
 public:
  AllObjectsRelationBuilder(Main *dmain, DGraph *graph, DGraphBuilderCache *cache)
      : DGraphRelationBuilder(dmain, graph, cache)
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

unique_ptr<DGraphNodeBuilder> AllObjectsBuilderPipeline::construct_node_builder()
{
  return std::make_unique<AllObjectsNodeBuilder>(dmain_, dgraph_, &builder_cache_);
}

unique_ptr<DGraphRelationBuilder> AllObjectsBuilderPipeline::construct_relation_builder()
{
  return std::make_unique<AllObjectsRelationBuilder>(dmain_, dgraph_, &builder_cache_);
}

}  // namespace dune::dgraph
