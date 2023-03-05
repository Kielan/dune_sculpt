#include "intern/dgraph_update.h"

#include "dgraph.h"

#include "intern/depsgraph_type.h"

namespace deg = dune::deg;

namespace dune::deg {

static DEG_EditorUpdateIDCb deg_editor_update_id_cb = nullptr;
static DEG_EditorUpdateSceneCb deg_editor_update_scene_cb = nullptr;

void deg_editors_id_update(const DEGEditorUpdateContext *update_ctx, ID *id)
{
  if (deg_editor_update_id_cb != nullptr) {
    deg_editor_update_id_cb(update_ctx, id);
  }
}

void deg_editors_scene_update(const DEGEditorUpdateContext *update_ctx, bool updated)
{
  if (deg_editor_update_scene_cb != nullptr) {
    deg_editor_update_scene_cb(update_ctx, updated);
  }
}

}  // namespace dune::deg

void deg_editors_set_update_cb(deg_EditorUpdateIDCb id_func, DEG_EditorUpdateSceneCb scene_func)
{
  deg::deg_editor_update_id_cb = id_func;
  deg::deg_editor_update_scene_cb = scene_func;
}
