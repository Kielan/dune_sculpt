/** Evaluation engine entry-points for Depsgraph Engine. **/

#include "mem_guardedalloc.h"

#include "lib_listbase.h"
#include "lib_utildefines.h"

#include "dune_scene.h"

#include "types_object.h"
#include "types_scene.h"

#include "dgraph.h"
#include "dgraph_query.h"

#include "intern/eval/dgraph_eval.h"
#include "intern/eval/dgraph_eval_flush.h"

#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_operation.h"
#include "intern/node/dgraph_node_time.h"

#include "intern/dgraph.h"
#include "intern/dgraph_tag.h"

namespace dgraph = dune::dgraph;

static void dgraph_flush_updates_and_refresh(dgraph::DGraph *dgraph)
{
  /* Update the time on the cow scene. */
  if (dgraph->scene_cow) {
    dune_scene_frame_set(dgraph->scene_cow, dgraph->frame);
  }

  dgraph::graph_tag_ids_for_visible_update(dgraph);
  dgraph::graph_flush_updates(dgraph);
  dgraph::dgraph_evaluate_on_refresh(dgraph);
}

void dgraph_evaluate_on_refresh(DGraph *graph)
{
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(graph);
  const Scene *scene = dgraph_get_input_scene(graph);
  const float frame = dune_scene_frame_get(scene);
  const float ctime = dune_scene_ctime_get(scene);

  if (dgraph->frame != frame || ctime != dgraph->ctime) {
    dgraph->tag_time_source();
    dgraph->frame = frame;
    dgraph->ctime = ctime;
  }
  else if (scene->id.recalc & ID_RECALC_FRAME_CHANGE) {
    /* Comparing graph & scene frame fails in the case of undo,
     * since the undo state is stored before updates from the frame change have been applied.
     * In this case reading back the undo state will behave as if no updates on frame change
     * is needed as the #Depsgraph.ctime & frame will match the values in the input scene.
     * Use #ID_RECALC_FRAME_CHANGE to detect that recalculation is necessary. see: T66913. */
    dgraph->tag_time_source();
  }

  dgraph_flush_updates_and_refresh(dgraph);
}

void dgraph_evaluate_on_framechange(DGraph *graph, float frame)
{
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(graph);
  const Scene *scene = dgraph_get_input_scene(graph);

  dgraph->tag_time_source();
  dgraph->frame = frame;
  dgraph->ctime = dune_scene_frame_to_ctime(scene, frame);
  dgraph_flush_updates_and_refresh(dgraph);
}
