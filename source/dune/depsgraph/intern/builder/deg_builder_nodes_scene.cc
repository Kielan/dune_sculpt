#include "intern/builder/deg_builder_nodes.h"

#include "types_scene.h"

namespace dune::deg {

void DGraphNodeBuilder::build_scene_render(Scene *scene, ViewLayer *view_layer)
{
  scene_ = scene;
  view_layer_ = view_layer;
  const bool build_compositor = (scene->r.scemode & R_DOCOMP);
  const bool build_sequencer = (scene->r.scemode & R_DOSEQ);
  IdNode *id_node = add_id_node(&scene->id);
  id_node->linked_state = DEG_ID_LINKED_DIRECTLY;
  add_time_source();
  build_animdata(&scene->id);
  build_scene_params(scene);
  build_scene_audio(scene);
  if (build_compositor) {
    build_scene_compositor(scene);
  }
  if (build_sequencer) {
    build_scene_sequencer(scene);
    build_scene_speakers(scene, view_layer);
  }
  if (scene->camera != nullptr) {
    build_object(-1, scene->camera, DEG_ID_LINKED_DIRECTLY, true);
  }
}

void DGraphNodeBuilder::build_scene_params(Scene *scene)
{
  if (built_map_.checkIsBuiltAndTag(scene, BuilderMap::TAG_PARAMS)) {
    return;
  }
  build_params(&scene->id);
  build_idprops(scene->id.props);
  add_op_node(&scene->id, NodeType::PARAMS, OpCode::SCENE_EVAL);
  /* NOTE: This is a bit overkill and can potentially pull a bit too much into the graph, but:
   *
   * - We definitely need an ID node for the scene's compositor, otherwise re-mapping will no
   *   happen correct and we will risk remapping pointers in the main database.
   * - Alternatively, we should discard compositor tree, but this might cause other headache like
   *   drivers which are coming from the tree.
   *
   * Would be nice to find some reliable way of ignoring compositor here, but it's already pulled
   * in when building scene from view layer, so this particular case does not make things
   * marginally worse. */
  build_scene_compositor(scene);

  LISTBASE_FOREACH (TimeMarker *, marker, &scene->markers) {
    build_idprops(marker->prop);
  }
}

void DGraphNodeBuilder::build_scene_compositor(Scene *scene)
{
  if (built_map_.checkIsBuiltAndTag(scene, BuilderMap::TAG_SCENE_COMPOSITOR)) {
    return;
  }
  if (scene->nodetree == nullptr) {
    return;
  }
  build_nodetree(scene->nodetree);
}

}  // namespace dune::deg
