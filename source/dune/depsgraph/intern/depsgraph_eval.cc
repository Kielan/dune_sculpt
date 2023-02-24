/** Evaluation engine entry-points for Depsgraph Engine. **/

#include "MEM_guardedalloc.h"

#include "lib_listbase.h"
#include "lib_utildefines.h"

#include "dune_scene.h"

#include "types_object.h"
#include "types_scene.h"

#include "deg_depsgraph.h"
#include "deg_depsgraph_query.h"

#include "intern/eval/deg_eval.h"
#include "intern/eval/deg_eval_flush.h"

#include "intern/node/deg_node.h"
#include "intern/node/deg_node_operation.h"
#include "intern/node/deg_node_time.h"

#include "intern/depsgraph.h"
#include "intern/depsgraph_tag.h"

namespace deg = dune::deg;

static void deg_flush_updates_and_refresh(deg::Depsgraph *deg_graph)
{
  /* Update the time on the cow scene. */
  if (deg_graph->scene_cow) {
    dune_scene_frame_set(deg_graph->scene_cow, deg_graph->frame);
  }

  deg::graph_tag_ids_for_visible_update(deg_graph);
  deg::deg_graph_flush_updates(deg_graph);
  deg::deg_evaluate_on_refresh(deg_graph);
}

void deg_evaluate_on_refresh(Depsgraph *graph)
{
  deg::Depsgraph *deg_graph = reinterpret_cast<deg::Depsgraph *>(graph);
  const Scene *scene = deg_get_input_scene(graph);
  const float frame = dune_scene_frame_get(scene);
  const float ctime = dune_scene_ctime_get(scene);

  if (deg_graph->frame != frame || ctime != deg_graph->ctime) {
    deg_graph->tag_time_source();
    deg_graph->frame = frame;
    deg_graph->ctime = ctime;
  }
  else if (scene->id.recalc & ID_RECALC_FRAME_CHANGE) {
    /* Comparing depsgraph & scene frame fails in the case of undo,
     * since the undo state is stored before updates from the frame change have been applied.
     * In this case reading back the undo state will behave as if no updates on frame change
     * is needed as the #Depsgraph.ctime & frame will match the values in the input scene.
     * Use #ID_RECALC_FRAME_CHANGE to detect that recalculation is necessary. see: T66913. */
    deg_graph->tag_time_source();
  }

  deg_flush_updates_and_refresh(deg_graph);
}

void deg_evaluate_on_framechange(Depsgraph *graph, float frame)
{
  deg::Depsgraph *deg_graph = reinterpret_cast<deg::Depsgraph *>(graph);
  const Scene *scene = deg_get_input_scene(graph);

  deg_graph->tag_time_source();
  deg_graph->frame = frame;
  deg_graph->ctime = dune_scene_frame_to_ctime(scene, frame);
  deg_flush_updates_and_refresh(deg_graph);
}
