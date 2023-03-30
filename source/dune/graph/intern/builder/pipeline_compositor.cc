#include "pipeline_compositor.h"

#include "intern/builder/graph_builder_nodes.h"
#include "intern/builder/graph_builder_relations.h"
#include "intern/graph.h"

namespace dune::graph {

CompositorBuilderPipeline::CompositorBuilderPipeline(::Graph *graph, NodeTree *nodetree)
    : AbstractBuilderPipeline(graph), nodetree_(nodetree)
{
  graph_->is_render_pipeline_dgraph = true;
}

void CompositorBuilderPipeline::build_nodes(GraphNodeBuilder &node_builder)
{
  node_builder.build_scene_render(scene_, view_layer_);
  node_builder.build_nodetree(nodetree_);
}

void CompositorBuilderPipeline::build_relations(GraphRelationBuilder &relation_builder)
{
  relation_builder.build_scene_render(scene_, view_layer_);
  relation_builder.build_nodetree(nodetree_);
}

}  // namespace dune::graph
