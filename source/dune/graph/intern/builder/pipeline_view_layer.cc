#include "pipeline_view_layer.h"

#include "intern/builder/graph_builder_nodes.h"
#include "intern/builder/graph_builder_relations.h"
#include "intern/graph.h"

namespace dune::graph {

ViewLayerBuilderPipeline::ViewLayerBuilderPipeline(::DGraph *graph)
    : AbstractBuilderPipeline(graph)
{
}

void ViewLayerBuilderPipeline::build_nodes(GraphNodeBuilder &node_builder)
{
  node_builder.build_view_layer(scene_, view_layer_, GRAPH_ID_LINKED_DIRECTLY);
}

void ViewLayerBuilderPipeline::build_relations(GraphRelationBuilder &relation_builder)
{
  relation_builder.build_view_layer(scene_, view_layer_, GRAPH_ID_LINKED_DIRECTLY);
}

}  // namespace dune::graph
