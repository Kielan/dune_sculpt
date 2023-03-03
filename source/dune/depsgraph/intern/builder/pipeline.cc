#include "pipeline.h"

#include "PIL_time.h"

#include "dune_global.h"

#include "types_scene.h"

#include "dgraph_builder_cycle.h"
#include "dgraph_builder_nodes.h"
#include "dgraph_builder_relations.h"
#include "dgraph_builder_transitive.h"

namespace dune::dgraph {

AbstractBuilderPipeline::AbstractBuilderPipeline(::DGraph *graph)
    : dgraph_(reinterpret_cast<DGraph *>(graph)),
      dmain_(dgraph_->dmain),
      scene_(dgraph_->scene),
      view_layer_(dgraph_->view_layer)
{
}

void AbstractBuilderPipeline::build()
{
  double start_time = 0.0;
  if (G.debug & (G_DEBUG_DEPSGRAPH_BUILD | G_DEBUG_DEPSGRAPH_TIME)) {
    start_time = PIL_check_seconds_timer();
  }

  build_step_sanity_check();
  build_step_nodes();
  build_step_relations();
  build_step_finalize();

  if (G.debug & (G_DEBUG_DGRAPH_BUILD | G_DEBUG_DGRAPH_TIME)) {
    printf("Depsgraph built in %f seconds.\n", PIL_check_seconds_timer() - start_time);
  }
}

void AbstractBuilderPipeline::build_step_sanity_check()
{
  lib_assert(lib_findindex(&scene_->view_layers, view_layer_) != -1);
  lib_assert(dgraph_->scene == scene_);
  lib_assert(dgraph_->view_layer == view_layer_);
}

void AbstractBuilderPipeline::build_step_nodes()
{
  /* Generate all the nodes in the graph first */
  unique_ptr<DGraphNodeBuilder> node_builder = construct_node_builder();
  node_builder->begin_build();
  build_nodes(*node_builder);
  node_builder->end_build();
}

void AbstractBuilderPipeline::build_step_relations()
{
  /* Hook up relationships between operations - to determine evaluation order. */
  unique_ptr<DGraphRelationBuilder> relation_builder = construct_relation_builder();
  relation_builder->begin_build();
  build_relations(*relation_builder);
  relation_builder->build_copy_on_write_relations();
  relation_builder->build_driver_relations();
}

void AbstractBuilderPipeline::build_step_finalize()
{
  /* Detect and solve cycles. */
  dgraph_detect_cycles(dgraph_);
  /* Simplify the graph by removing redundant relations (to optimize
   * traversal later). */
  /* TODO: it would be useful to have an option to disable this in cases where
   *       it is causing trouble. */
  if (G.debug_value == 799) {
    dgraph_transitive_reduction(dgraph_);
  }
  /* Store pointers to commonly used evaluated datablocks. */
  dgraph_->scene_cow = (Scene *)dgraph_->get_cow_id(&dgraph_->scene->id);
  /* Flush visibility layer and re-schedule nodes for update. */
  dgraph_build_finalize(dmain_, dgraph_);
  dgraph_tag_on_visible_update(reinterpret_cast<::DGraph *>(dgraph_), false);
#if 0
  if (!dgraph_debug_consistency_check(dgraph_)) {
    printf("Consistency validation failed, ABORTING!\n");
    abort();
  }
#endif
  /* Relations are up to date. */
  dgraph_->need_update_relations = false;
}

unique_ptr<DGraphNodeBuilder> AbstractBuilderPipeline::construct_node_builder()
{
  return std::make_unique<DGraphNodeBuilder>(dmain_, dgraph_, &builder_cache_);
}

unique_ptr<DGraphRelationBuilder> AbstractBuilderPipeline::construct_relation_builder()
{
  return std::make_unique<DGraphRelationBuilder>(dmain_, dgraph_, &builder_cache_);
}

}  // namespace dune::dgraph
