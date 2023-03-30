#include "pipeline_render.h"

#include "intern/builder/graph_builder_nodes.h"
#include "intern/builder/graph_builder_relations.h"
#include "intern/graph.h"

namespace dune::graph {

RenderBuilderPipeline::RenderBuilderPipeline(::Graph *graph) : AbstractBuilderPipeline(graph)
{
 graph_->is_render_pipeline_graph = true;
}

void RenderBuilderPipeline::build_nodes(GraphNodeBuilder &node_builder)
{
  node_builder.build_scene_render(scene_, view_layer_);
}

void RenderBuilderPipeline::build_relations(GraphRelationBuilder &relation_builder)
{
  relation_builder.build_scene_render(scene_, view_layer_);
}

}  // namespace dune::graph
