#include "pipeline_compositor.h"

#include "intern/builder/dgraph_builder_nodes.h"
#include "intern/builder/dgraph_builder_relations.h"
#include "intern/dgraph.h"

namespace dune::dgraph {

CompositorBuilderPipeline::CompositorBuilderPipeline(::DGraph *graph, DNodeTree *nodetree)
    : AbstractBuilderPipeline(graph), nodetree_(nodetree)
{
  dgraph_->is_render_pipeline_dgraph = true;
}

void CompositorBuilderPipeline::build_nodes(DGraphNodeBuilder &node_builder)
{
  node_builder.build_scene_render(scene_, view_layer_);
  node_builder.build_nodetree(nodetree_);
}

void CompositorBuilderPipeline::build_relations(DGraphRelationBuilder &relation_builder)
{
  relation_builder.build_scene_render(scene_, view_layer_);
  relation_builder.build_nodetree(nodetree_);
}

}  // namespace dune::dgraph
