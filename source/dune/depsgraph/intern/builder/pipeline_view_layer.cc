#include "pipeline_view_layer.h"

#include "intern/builder/dgraph_builder_nodes.h"
#include "intern/builder/dgraph_builder_relations.h"
#include "intern/dgraph.h"

namespace dune::dgraph {

ViewLayerBuilderPipeline::ViewLayerBuilderPipeline(::DGraph *graph)
    : AbstractBuilderPipeline(graph)
{
}

void ViewLayerBuilderPipeline::build_nodes(DepsgraphNodeBuilder &node_builder)
{
  node_builder.build_view_layer(scene_, view_layer_, DGRAPH_ID_LINKED_DIRECTLY);
}

void ViewLayerBuilderPipeline::build_relations(DGraphRelationBuilder &relation_builder)
{
  relation_builder.build_view_layer(scene_, view_layer_, DGRAPH_ID_LINKED_DIRECTLY);
}

}  // namespace dune::dgraph
