#include "pipeline_render.h"

#include "intern/builder/graph_builder_nodes.h"
#include "intern/builder/graph_builder_relations.h"
#include "intern/graph.h"

namespace dune::graph {

RenderBuilderPipeline::RenderBuilderPipeline(::DGraph *graph) : AbstractBuilderPipeline(graph)
{
  dgraph_->is_render_pipeline_dgraph = true;
}

void RenderBuilderPipeline::build_nodes(DGraphNodeBuilder &node_builder)
{
  node_builder.build_scene_render(scene_, view_layer_);
}

void RenderBuilderPipeline::build_relations(DGraphRelationBuilder &relation_builder)
{
  relation_builder.build_scene_render(scene_, view_layer_);
}

}  // namespace dune::dgraph
