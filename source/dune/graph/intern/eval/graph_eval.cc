/**
 * Evaluation engine entry-points for Depsgraph Engine.
 */

#include "intern/eval/graph_eval.h"

#include "PIL_time.h"

#include "lib_compiler_attrs.h"
#include "lib_fn_ref.hh"
#include "lib_gsqueue.h"
#include "lib_task.h"
#include "lib_utildefines.h"

#include "dune_global.h"

#include "types_node.h"
#include "types_object.h"
#include "types_scene.h"

#include "graph.h"
#include "graph_query.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

#include "atomic_ops.h"

#include "intern/graph.h"
#include "intern/graph_relation.h"
#include "intern/graph_tag.h"
#include "intern/eval/graph_eval_copy_on_write.h"
#include "intern/eval/graph_eval_flush.h"
#include "intern/eval/graph_eval_stats.h"
#include "intern/eval/graph_eval_visibility.h"
#include "intern/node/graph_node.h"
#include "intern/node/graph_node_component.h"
#include "intern/node/graph_node_id.h"
#include "intern/node/graph_node_op.h"
#include "intern/node/graph_node_time.h"

namespace dune::graph {

namespace {

struct GraphEvalState;

void deg_task_run_fn(TaskPool *pool, void *taskdata);

void schedule_children(GraphEvalState *state,
                       OpNode *node,
                       FnRef<void(OpNode *node)> schedule_fn);

/* Denotes which part of dependency graph is being evaluated. */
enum class EvaluationStage {
  /* Stage 1: Only  Copy-on-Write operations are to be evaluated, prior to anything else.
   * This allows other operations to access its dependencies when there is a dependency cycle
   * involved. */
  COPY_ON_WRITE,

  /* Evaluate actual ids nodes visibility based on the current state of animation and drivers. */
  DYNAMIC_VISIBILITY,

  /* Threaded evaluation of all possible operations. */
  THREADED_EVALUATION,

  /* Workaround for areas which can not be evaluated in threads.
   *
   * For example, meta-balls, which are iterating over all bases and are requesting dupli-lists
   * to see whether there are meta-balls inside. */
  SINGLE_THREADED_WORKAROUND,
};

struct GraphEvalState {
  DGraph *graph;
  bool do_stats;
  EvaluationStage stage;
  bool need_update_pending_parents = true;
  bool need_single_thread_pass = false;
};

void evaluate_node(const GraphEvalState *state, OpNode *op_node)
{
  ::Graph *graph = reinterpret_cast<::Graph *>(state->graph);

  /* Sanity checks. */
  lib_assert_msg(!op_node->is_noop(), "NOOP nodes should not actually be scheduled");
  /* Perform operation. */
  if (state->do_stats) {
    const double start_time = PIL_check_seconds_timer();
    op_node->evaluate(graph);
    op_node->stats.current_time += PIL_check_seconds_timer() - start_time;
  }
  else {
    op_node->evaluate(graph);
  }

  /* Clear the flag early on, allowing partial updates without re-evaluating the same node multiple
   * times.
   * This is a thread-safe modification as the node's flags are only read for a non-scheduled nodes
   * and this node has been scheduled. */
  op_node->flag &= ~GRAPH_OP_FLAG_CLEAR_ON_EVAL;
}

void dgraph_task_run_func(TaskPool *pool, void *taskdata)
{
  void *userdata_v = lib_task_pool_user_data(pool);
  GraphEvalState *state = (GraphEvalState *)userdata_v;

  /* Evaluate node. */
  OpNode *op_node = reinterpret_cast<OpNode *>(taskdata);
  evaluate_node(state, op_node);

  /* Schedule children. */
  schedule_children(state, op_node, [&](OpNode *node) {
    lib_task_pool_push(pool, graph_task_run_fn, node, false, nullptr);
  });
}

bool check_op_node_visible(const GraphEvalState *state, OpNode *op_node)
{
  const ComponentNode *comp_node = op_node->owner;
  /* Special case for copy on write component: it is to be always evaluated, to keep copied
   * "database" in a consistent state. */
  if (comp_node->type == NodeType::COPY_ON_WRITE) {
    return true;
  }

  /* Special case for dynamic visibility pass: the actual visibility is not yet known, so limit to
   * only operations which affects visibility. */
  if (state->stage == EvaluationStage::DYNAMIC_VISIBILITY) {
    return op_node->flag & OpFlag::GRAPH_OP_FLAG_AFFECTS_VISIBILITY;
  }

  return comp_node->affects_visible_id;
}

void calculate_pending_parents_for_node(const DGraphEvalState *state, OperationNode *node)
{
  /* Update counters, applies for both visible and invisible ids. */
  node->num_links_pending = 0;
  node->scheduled = false;
  /* Invisible ids requires no pending operations. */
  if (!check_op_node_visible(state, node)) {
    return;
  }
  /* No need to bother with anything if node is not tagged for update. */
  if ((node->flag & GRAPH_OP_FLAG_NEEDS_UPDATE) == 0) {
    return;
  }
  for (Relation *rel : node->inlinks) {
    if (rel->from->type == NodeType::OP && (rel->flag & RELATION_FLAG_CYCLIC) == 0) {
      OpNode *from = (OpNode *)rel->from;
      /* TODO: This is how old layer system was checking for the
       * calculation, but how is it possible that visible object depends
       * on an invisible? This is something what is prohibited after
       * graph_build_flush_layers(). */
      if (!check_op_node_visible(state, from)) {
        continue;
      }
      /* No need to wait for operation which is up to date. */
      if ((from->flag & GRAPH_OP_FLAG_NEEDS_UPDATE) == 0) {
        continue;
      }
      ++node->num_links_pending;
    }
  }
}

void calculate_pending_parents_if_needed(GraphEvalState *state)
{
  if (!state->need_update_pending_parents) {
    return;
  }

  for (OpNode *node : state->graph->ops) {
    calculate_pending_parents_for_node(state, node);
  }

  state->need_update_pending_parents = false;
}

void initialize_execution(GraphEvalState *state, Graph *graph)
{
  /* Clear tags and other things which needs to be clear. */
  if (state->do_stats) {
    for (OpNode *node : graph->operations) {
      node->stats.reset_current();
    }
  }
}

bool is_metaball_object_op(const OpNode *op_node)
{
  const ComponentNode *component_node = op_node->owner;
  const IdNode *id_node = component_node->owner;
  if (GS(id_node->id_cow->name) != ID_OB) {
    return false;
  }
  const Object *object = reinterpret_cast<const Object *>(id_node->id_cow);
  return object->type == OB_MBALL;
}

bool need_evaluate_operation_at_stage(DepsgraphEvalState *state,
                                      const OperationNode *operation_node)
{
  const ComponentNode *component_node = operation_node->owner;
  switch (state->stage) {
    case EvaluationStage::COPY_ON_WRITE:
      return (component_node->type == NodeType::COPY_ON_WRITE);

    case EvaluationStage::DYNAMIC_VISIBILITY:
      return operation_node->flag & OperationFlag::DEPSOP_FLAG_AFFECTS_VISIBILITY;

    case EvaluationStage::THREADED_EVALUATION:
      if (is_metaball_object_operation(operation_node)) {
        state->need_single_thread_pass = true;
        return false;
      }
      return true;

    case EvaluationStage::SINGLE_THREADED_WORKAROUND:
      return true;
  }
  BLI_assert_msg(0, "Unhandled evaluation stage, should never happen.");
  return false;
}

/* Schedule a node if it needs evaluation.
 *   dec_parents: Decrement pending parents count, true when child nodes are
 *                scheduled after a task has been completed.
 */
void schedule_node(DGraphEvalState *state,
                   OpNode *node,
                   bool dec_parents,
                   const FnRef<void(OpNode *node)> schedule_fn)
{
  /* No need to schedule nodes of invisible ID. */
  if (!check_op_node_visible(state, node)) {
    return;
  }
  /* No need to schedule operations which are not tagged for update, they are
   * considered to be up to date. */
  if ((node->flag & DGRAPH_OP_FLAG_NEEDS_UPDATE) == 0) {
    return;
  }
  /* TODO: This is not strictly speaking safe to read
   * num_links_pending. */
  if (dec_parents) {
    lib_assert(node->num_links_pending > 0);
    atomic_sub_and_fetch_uint32(&node->num_links_pending, 1);
  }
  /* Cal not schedule operation while its dependencies are not yet
   * evaluated. */
  if (node->num_links_pending != 0) {
    return;
  }
  /* During the COW stage only schedule COW nodes. */
  if (!need_evaluate_operation_at_stage(state, node)) {
    return;
  }
  /* Actually schedule the node. */
  bool is_scheduled = atomic_fetch_and_or_uint8((uint8_t *)&node->scheduled, uint8_t(true));
  if (!is_scheduled) {
    if (node->is_noop()) {
      /* Clear flags to avoid affecting subsequent update propagation.
       * For normal nodes these are cleared when it is evaluated. */
      node->flag &= ~DGRAPH_OP_FLAG_CLEAR_ON_EVAL;

      /* skip NOOP node, schedule children right away */
      schedule_children(state, node, schedule_fn);
    }
    else {
      /* children are scheduled once this task is completed */
      schedule_fn(node);
    }
  }
}

void schedule_graph(DGraphEvalState *state,
                    const FnRef<void(OpNode *node)> schedule_fn)
{
  for (OpNode *node : state->graph->ops) {
    schedule_node(state, node, false, schedule_fn);
  }
}

void schedule_children(DGraphEvalState *state,
                       OpNode *node,
                       const FnRef<void(OpNode *node)> schedule_fn)
{
  for (Relation *rel : node->outlinks) {
    OpNode *child = (OpNode *)rel->to;
    lib_assert(child->type == NodeType::OP);
    if (child->scheduled) {
      /* Happens when having cyclic dependencies. */
      continue;
    }
    schedule_node(state, child, (rel->flag & RELATION_FLAG_CYCLIC) == 0, schedule_fn);
  }
}

/* Evaluate given stage of the dependency graph evaluation using multiple threads.
 *
 * NOTE: Will assign the `state->stage` to the given stage. */
void evaluate_graph_threaded_stage(DepsgraphEvalState *state,
                                   TaskPool *task_pool,
                                   const EvaluationStage stage)
{
  state->stage = stage;

  calculate_pending_parents_if_needed(state);

  schedule_graph(state, [&](OpNode *node) {
    lib_task_pool_push(task_pool, dgraph_task_run_func, node, false, nullptr);
  });
  lib_task_pool_work_and_wait(task_pool);
}

/* Evaluate remaining operations of the dependency graph in a single threaded manner. */
void evaluate_graph_single_threaded_if_needed(DGraphEvalState *state)
{
  if (!state->need_single_thread_pass) {
    return;
  }

  lib_assert(!state->need_update_pending_parents);

  state->stage = EvaluationStage::SINGLE_THREADED_WORKAROUND;

  GSQueue *evaluation_queue = lib_gsqueue_new(sizeof(OpNode *));
  auto schedule_node_to_queue = [&](OpNode *node) {
    lib_gsqueue_push(evaluation_queue, &node);
  };
  schedule_graph(state, schedule_node_to_queue);

  while (!lib_gsqueue_is_empty(evaluation_queue)) {
    OpNode *op_node;
    lib_gsqueue_pop(evaluation_queue, &operation_node);

    evaluate_node(state, operation_node);
    schedule_children(state, operation_node, schedule_node_to_queue);
  }

  BLI_gsqueue_free(evaluation_queue);
}

void depsgraph_ensure_view_layer(Depsgraph *graph)
{
  /* We update copy-on-write scene in the following cases:
   * - It was not expanded yet.
   * - It was tagged for update of CoW component.
   * This allows us to have proper view layer pointer. */
  Scene *scene_cow = graph->scene_cow;
  if (deg_copy_on_write_is_expanded(&scene_cow->id) &&
      (scene_cow->id.recalc & ID_RECALC_COPY_ON_WRITE) == 0) {
    return;
  }

  const IDNode *scene_id_node = graph->find_id_node(&graph->scene->id);
  deg_update_copy_on_write_datablock(graph, scene_id_node);
}

TaskPool *deg_evaluate_task_pool_create(DepsgraphEvalState *state)
{
  if (G.debug & G_DEBUG_DEPSGRAPH_NO_THREADS) {
    return BLI_task_pool_create_no_threads(state);
  }

  return BLI_task_pool_create_suspended(state, TASK_PRIORITY_HIGH);
}

}  // namespace

void dgraph_evaluate_on_refresh(DGraph *graph)
{
  /* Nothing to update, early out. */
  if (graph->entry_tags.is_empty()) {
    return;
  }

  graph->debug.begin_graph_evaluation();

#ifdef WITH_PYTHON
  /* Release the GIL so that Python drivers can be evaluated. See #91046. */
  BPy_BEGIN_ALLOW_THREADS;
#endif

  graph->is_evaluating = true;
  dgraph_ensure_view_layer(graph);

  /* Set up evaluation state. */
  DGraphEvalState state;
  state.graph = graph;
  state.do_stats = graph->debug.do_time_debug();

  /* Prepare all nodes for evaluation. */
  initialize_execution(&state, graph);

  /* Evaluation happens in several incremental steps:
   *
   * - Start with the copy-on-write operations which never form dependency cycles. This will ensure
   *   that if a dependency graph has a cycle evaluation functions will always "see" valid expanded
   *   datablock. It might not be evaluated yet, but at least the datablock will be valid.
   *
   * - If there is potentially dynamically changing visibility in the graph update the actual
   *   nodes visibilities, so that actual heavy data evaluation can benefit from knowledge that
   *   something heavy is not currently visible.
   *
   * - Multi-threaded evaluation of all possible nodes.
   *   Certain operations (and their subtrees) could be ignored. For example, meta-balls are not
   *   safe from threading point of view, so the threaded evaluation will stop at the metaball
   *   operation node.
   *
   * - Single-threaded pass of all remaining operations. */

  TaskPool *task_pool = deg_evaluate_task_pool_create(&state);

  evaluate_graph_threaded_stage(&state, task_pool, EvaluationStage::COPY_ON_WRITE);

  if (graph->has_animated_visibility || graph->need_update_nodes_visibility) {
    /* Update pending parents including only the ones which are affecting operations which are
     * affecting visibility. */
    state.need_update_pending_parents = true;

    evaluate_graph_threaded_stage(&state, task_pool, EvaluationStage::DYNAMIC_VISIBILITY);

    dgraph_flush_visibility_flags_if_needed(graph);

    /* Update parents to an updated visibility and evaluation stage.
     *
     * Need to do it regardless of whether visibility is actually changed or not: current state of
     * the pending parents are all zeroes because it was previously calculated for only visibility
     * related nodes and those are fully evaluated by now. */
    state.need_update_pending_parents = true;
  }

  evaluate_graph_threaded_stage(&state, task_pool, EvaluationStage::THREADED_EVALUATION);

  lib_task_pool_free(task_pool);

  evaluate_graph_single_threaded_if_needed(&state);

  /* Finalize statistics gathering. This is because we only gather single
   * operation timing here, without aggregating anything to avoid any extra
   * synchronization. */
  if (state.do_stats) {
    deg_eval_stats_aggregate(graph);
  }

  /* Clear any uncleared tags. */
  dgraph_clear_tags(graph);
  graph->is_evaluating = false;

#ifdef WITH_PYTHON
  BPy_END_ALLOW_THREADS;
#endif

  graph->debug.end_graph_evaluation();
}

}  // namespace dune::deg
