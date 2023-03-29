 /** Methods for constructing depsgraph **/

#include "intern/builder/dgraph_builder_relations.h"

#include <cstdio>
#include <cstdlib>
#include <cstring> /* required for STREQ later on. */

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "types_collection.h"
#include "types_linestyle.h"
#include "types_node.h"
#include "types_object.h"
#include "types_scene.h"

#include "dune_layer.h"
#include "dune_main.h"
#include "dune_node.h"

#include "dgraph.h"
#include "dgraph_build.h"

#include "intern/builder/dgraph_builder.h"
#include "intern/builder/dgraph_builder_pchanmap.h"

#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_component.h"
#include "intern/node/dgraph_node_id.h"
#include "intern/node/dgraph_node_operation.h"

#include "intern/dgraph_type.h"

namespace dune::dgraph {

void DGraphRelationBuilder::build_layer_collections(ListBase *lb)
{
  const int visibility_flag = (graph_->mode == DAG_EVAL_VIEWPORT) ? COLLECTION_HIDE_VIEWPORT :
                                                                    COLLECTION_HIDE_RENDER;

  for (LayerCollection *lc = (LayerCollection *)lb->first; lc; lc = lc->next) {
    if (lc->collection->flag & visibility_flag) {
      continue;
    }
    if ((lc->flag & LAYER_COLLECTION_EXCLUDE) == 0) {
      build_collection(lc, nullptr, lc->collection);
    }
    build_layer_collections(&lc->layer_collections);
  }
}

void DGraphRelationBuilder::build_freestyle_lineset(FreestyleLineSet *fls)
{
  if (fls->group != nullptr) {
    build_collection(nullptr, nullptr, fls->group);
  }
  if (fls->linestyle != nullptr) {
    build_freestyle_linestyle(fls->linestyle);
  }
}

void DGraphRelationBuilder::build_view_layer(Scene *scene,
                                             ViewLayer *view_layer,
                                             eDGraphNodeNode_LinkedState_Type linked_state)
{
  /* Setup currently building context. */
  scene_ = scene;
  dune_view_layer_synced_ensure(scene, view_layer);
  /* Scene objects. */
  /* NOTE: Nodes builder requires us to pass CoW base because it's being
   * passed to the evaluation functions. During relations builder we only
   * do nullptr-pointer check of the base, so it's fine to pass original one. */
  LISTBASE_FOREACH (Base *, base, dune_view_layer_object_bases_get(view_layer)) {
    if (need_pull_base_into_graph(base)) {
      build_object_from_view_layer_base(base->object);
    }
  }

  build_layer_collections(&view_layer->layer_collections);

  if (scene->camera != nullptr) {
    build_object(scene->camera);
  }
  /* Rigidbody. */
  if (scene->rigidbody_world != nullptr) {
    build_rigidbody(scene);
  }
  /* Scene's animation and drivers. */
  if (scene->adt != nullptr) {
    build_animdata(&scene->id);
  }
  /* World. */
  if (scene->world != nullptr) {
    build_world(scene->world);
  }
  /* Masks. */
  LISTBASE_FOREACH (Mask *, mask, &dmain_->masks) {
    build_mask(mask);
  }
  /* Movie clips. */
  LISTBASE_FOREACH (MovieClip *, clip, &dmain_->movieclips) {
    build_movieclip(clip);
  }
  /* Material override. */
  if (view_layer->mat_override != nullptr) {
    build_material(view_layer->mat_override);
  }
  /* Freestyle linesets. */
  LISTBASE_FOREACH (FreestyleLineSet *, fls, &view_layer->freestyle_config.linesets) {
    build_freestyle_lineset(fls);
  }
  /* Scene parameters, compositor and such. */
  build_scene_compositor(scene);
  build_scene_params(scene);
  /* Make final scene evaluation dependent on view layer evaluation. */
  OpKey scene_view_layer_key(
      &scene->id, NodeType::LAYER_COLLECTIONS, OpCode::VIEW_LAYER_EVAL);
  OpKey scene_eval_key(&scene->id, NodeType::PARAMS, OpCode::SCENE_EVAL);
  add_relation(scene_view_layer_key, scene_eval_key, "View Layer -> Scene Eval");
  /* Sequencer. */
  if (linked_state == DGRAPH_ID_LINKED_DIRECTLY) {
    build_scene_audio(scene);
    build_scene_sequencer(scene);
  }
  /* Build all set scenes. */
  if (scene->set != nullptr) {
    ViewLayer *set_view_layer = dune_view_layer_default_render(scene->set);
    build_view_layer(scene->set, set_view_layer, DGRAPH_ID_LINKED_VIA_SET);
  }
}

}  // namespace dune::dgraph
