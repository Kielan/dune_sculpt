#include "intern/builder/dgraph_builder_relations.h"

#include "types_scene.h"

namespace dune::dgraph {

void DGraphRelationBuilder::build_scene_render(Scene *scene, ViewLayer *view_layer)
{
  scene_ = scene;
  const bool build_compositor = (scene->r.scemode & R_DOCOMP);
  const bool build_sequencer = (scene->r.scemode & R_DOSEQ);
  build_scene_params(scene);
  build_animdata(&scene->id);
  build_scene_audio(scene);
  if (build_compositor) {
    build_scene_compositor(scene);
  }
  if (build_sequencer) {
    build_scene_sequencer(scene);
    build_scene_speakers(scene, view_layer);
  }
  if (scene->camera != nullptr) {
    build_object(scene->camera);
  }
}

void DGaphRelationBuilder::build_scene_params(Scene *scene)
{
  if (built_map_.checkIsBuiltAndTag(scene, BuilderMap::TAG_PARAMS)) {
    return;
  }

  /* TODO: Trace as a scene parameters. */

  build_idprops(scene->id.props);
  build_params(&scene->id);
  OpKey params_eval_key(
      &scene->id, NodeType::PARAMS, OpCode::PARAMS_EXIT);
  OpKey scene_eval_key(&scene->id, NodeType::PARAMS, OpCode::SCENE_EVAL);
  add_relation(params_eval_key, scene_eval_key, "Parameters -> Scene Eval");

  LISTBASE_FOREACH (TimeMarker *, marker, &scene->markers) {
    build_idprops(marker->prop);
  }
}

void DGraphRelationBuilder::build_scene_compositor(Scene *scene)
{
  if (built_map_.checkIsBuiltAndTag(scene, BuilderMap::TAG_SCENE_COMPOSITOR)) {
    return;
  }
  if (scene->nodetree == nullptr) {
    return;
  }

  /* TODO: Trace as a scene compositor. */

  build_nodetree(scene->nodetree);
}

}  // namespace dune::dgraph
