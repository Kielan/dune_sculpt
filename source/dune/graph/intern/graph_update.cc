#include "intern/graph_update.h"

#include "graph.h"

#include "intern/graph_type.h"

namespace graph = dune::graph;

namespace dune::graph {

static GraphEditorUpdateIdCb graph_editor_update_id_cb = nullptr;
static GraphEditorUpdateSceneCb graph_editor_update_scene_cb = nullptr;

void graph_editors_id_update(const GraphEditorUpdateCtx *update_ctx, Id *id)
{
  if (graph_editor_update_id_cb != nullptr) {
    graph_editor_update_id_cb(update_ctx, id);
  }
}

void graph_editors_scene_update(const GraphEditorUpdateCtx *update_ctx, bool updated)
{
  if (graph_editor_update_scene_cb != nullptr) {
    graph_editor_update_scene_cb(update_ctx, updated);
  }
}

}  // namespace dune::dgraph

void graph_editors_set_update_cb(GraphEditorUpdateIdCb id_fn, GraphEditorUpdateSceneCb scene_fn)
{
  graph::graph_editor_update_id_cb = id_fn;
  graph::graph_editor_update_scene_cb = scene_fn;
}
