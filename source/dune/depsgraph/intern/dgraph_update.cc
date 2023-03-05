#include "intern/dgraph_update.h"

#include "dgraph.h"

#include "intern/dgraph_type.h"

namespace dgraph = dune::dgraph;

namespace dune::dgraph {

static DGraphEditorUpdateIdCb dgraph_editor_update_id_cb = nullptr;
static DGraphEditorUpdateSceneCb dgraph_editor_update_scene_cb = nullptr;

void dgraph_editors_id_update(const DGraphEditorUpdateContext *update_ctx, Id *id)
{
  if (dgraph_editor_update_id_cb != nullptr) {
    dgraph_editor_update_id_cb(update_ctx, id);
  }
}

void dgraph_editors_scene_update(const DGraphEditorUpdateContext *update_ctx, bool updated)
{
  if (dgraph_editor_update_scene_cb != nullptr) {
    dgraph_editor_update_scene_cb(update_ctx, updated);
  }
}

}  // namespace dune::dgraph

void dgraph_editors_set_update_cb(DGraphEditorUpdateIdCb id_fn, DGraphEditorUpdateSceneCb scene_fn)
{
  dgraph::dgraph_editor_update_id_cb = id_fn;
  dgraph::dgraph_editor_update_scene_cb = scene_fn;
}
