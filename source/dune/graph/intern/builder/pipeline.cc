#include "pipeline.h"

#include "PIL_time.h"

#include "dune_global.h"

#include "types_scene.h"

#include "graph_builder_cycle.h"
#include "graph_builder_nodes.h"
#include "graph_builder_relations.h"
#include "graph_builder_transitive.h"

namespace dune::graph {

AbstractBuilderPipeline::AbstractBuilderPipeline(::Graph *graph)
    : graph_(reinterpret_cast<Graph *>(graph)),
      dmain_(graph_->dmain),
      scene_(graph_->scene),
      view_layer_(graph_->view_layer)
{
}

void AbstractBuilderPipeline::build()
{
  double start_time = 0.0;
  if (G.debug & (G_DEBUG_GRAPH_BUILD | G_DEBUG_GRAPH_TIME)) {
    start_time = PIL_check_seconds_timer();
  }

  build_step_sanity_check();
  build_step_nodes();
  build_step_relations();
  build_step_finalize();

  if (G.debug & (G_DEBUG_GRAPH_BUILD | G_DEBUG_GRAPH_TIME)) {
    printf("Depsgraph built in %f seconds.\n", PIL_check_seconds_timer() - start_time);
  }
}

void AbstractBuilderPipeline::build_step_sanity_check()
{
  lib_assert(lib_findindex(&scene_->view_layers, view_layer_) != -1);
  lib_assert(graph_->scene == scene_);
  lib_assert(graph_->view_layer == view_layer_);
}

void AbstractBuilderPipeline::build_step_nodes()
{
  /* Generate all the nodes in the graph first */
  unique_ptr<GraphNodeBuilder> node_builder = construct_node_builder();
  node_builder->begin_build();
  build_nodes(*node_builder);
  node_builder->end_build();
}

void AbstractBuilderPipeline::build_step_relations()
{
  /* Hook up relationships between operations - to determine evaluation order. */
  unique_ptr<GraphRelationBuilder> relation_builder = construct_relation_builder();
  relation_builder->begin_build();
  build_relations(*relation_builder);
  relation_builder->build_copy_on_write_relations();
  relation_builder->build_driver_relations();
}

void AbstractBuilderPipeline::build_step_finalize()
{
  /* Detect and solve cycles. */
  graph_detect_cycles(graph_);
  /* Simplify the graph by removing redundant relations (to optimize
   * traversal later). */
  /* TODO: it would be useful to have an option to disable this in cases where
   *       it is causing trouble. */
  if (G.debug_value == 799) {
    graph_transitive_reduction(graph_);
  }
  /* Store pointers to commonly used evaluated datablocks. */
  graph_->scene_cow = (Scene *)graph_->get_cow_id(&graph_->scene->id);
  /* Flush visibility layer and re-schedule nodes for update. */
  graph_build_finalize(dmain_, graph_);
  graph_tag_on_visible_update(reinterpret_cast<::Graph *>(graph_), false);
#if 0
  if (!graph_debug_consistency_check(graph_)) {
    printf("Consistency validation failed, ABORTING!\n");
    abort();
  }
#endif
  /* Relations are up to date. */
  graph_->need_update_relations = false;
}

unique_ptr<GraphNodeBuilder> AbstractBuilderPipeline::construct_node_builder()
{
  return std::make_unique<GraphNodeBuilder>(dmain_,graph_, &builder_cache_);
}

unique_ptr<GraphRelationBuilder> AbstractBuilderPipeline::construct_relation_builder()
{
  return std::make_unique<GraphRelationBuilder>(dmain_,graph_, &builder_cache_);
}

}  // namespace dune::graph
