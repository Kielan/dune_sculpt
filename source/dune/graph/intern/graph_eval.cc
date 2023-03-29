/** Evaluation engine entry-points for Depsgraph Engine. **/

#include "mem_guardedalloc.h"

#include "lib_listbase.h"
#include "lib_utildefines.h"

#include "dune_scene.h"

#include "types_object.h"
#include "types_scene.h"

#include "graph.h"
#include "graph_query.h"

#include "intern/eval/graph_eval.h"
#include "intern/eval/graph_eval_flush.h"

#include "intern/node/graph_node.h"
#include "intern/node/graph_node_op.h"
#include "intern/node/graph_node_time.h"

#include "intern/graph.h"
#include "intern/graph_tag.h"

namespace graph = dune::graph;

static void graph_flush_updates_and_refresh(graph::Graph *graph)
{
  /* Update the time on the cow scene. */
  if (graph->scene_cow) {
    dune_scene_frame_set(graph->scene_cow, graph->frame);
  }

  graph::graph_tag_ids_for_visible_update(graph);
  graph::graph_flush_updates(graph);
  graph::graph_evaluate_on_refresh(graph);
}

void graph_evaluate_on_refresh(Graph *graph)
{
  graph::Graph *graph = reinterpret_cast<graph::Graph *>(graph);
  const Scene *scene = graph_get_input_scene(graph);
  const float frame = dune_scene_frame_get(scene);
  const float ctime = dune_scene_ctime_get(scene);

  if (graph->frame != frame || ctime != graph->ctime) {
    graph->tag_time_source();
    graph->frame = frame;
    graph->ctime = ctime;
  }
  else if (scene->id.recalc & ID_RECALC_FRAME_CHANGE) {
    /* Comparing graph & scene frame fails in the case of undo,
     * since the undo state is stored before updates from the frame change have been applied.
     * In this case reading back the undo state will behave as if no updates on frame change
     * is needed as the #Depsgraph.ctime & frame will match the values in the input scene.
     * Use ID_RECALC_FRAME_CHANGE to detect that recalculation is necessary. see: T66913. */
    graph->tag_time_source();
  }

  graph_flush_updates_and_refresh(graph);
}

void graph_evaluate_on_framechange(Graph *graph, float frame)
{
  graph::Graph *graph = reinterpret_cast<graph::Graph *>(graph);
  const Scene *scene = graph_get_input_scene(graph);

  graph->tag_time_source();
  graph->frame = frame;
  graph->ctime = dune_scene_frame_to_ctime(scene, frame);
  graph_flush_updates_and_refresh(graph);
}
