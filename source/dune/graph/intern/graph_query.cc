/** Implementation of Querying API */

#include "mem_guardedalloc.h"

#include <cstring> /* XXX: memcpy */

#include "lib_listbase.h"
#include "lib_utildefines.h"

#include "dune_action.h" /* XXX: BKE_pose_channel_find_name */
#include "dune_customdata.h"
#include "dune_idtype.h"
#include "dune_main.h"

#include "types_object.h"
#include "types_scene.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "graph.h"
#include "graph_query.h"

#include "intern/graph.h"
#include "intern/eval/graph_eval_copy_on_write.h"
#include "intern/node/graph_node_id.h"

namespace graph = dune::graph;

struct Scene *graph_get_input_scene(const Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  return graph->scene;
}

struct ViewLayer *graph_get_input_view_layer(const Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  return graph->view_layer;
}

struct Main *graph_get_dmain(const Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  return graph->dmain;
}

eEvaluationMode graph_get_mode(const Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  return graph->mode;
}

float graph_get_ctime(const Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const dune::Graph *>(graph);
  return graph->ctime;
}

bool graph_id_type_updated(const Graph *graph, short id_type)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  return graph->id_type_updated[dune_idtype_idcode_to_index(id_type)] != 0;
}

bool graph_id_type_any_updated(const Graph *graph)
{
  const dune::Graph *graph = reinterpret_cast<const dune::Graph *>(graph);

  /* Loop over all id types. */
  for (char id_type_index : graph->id_type_updated) {
    if (id_type_index) {
      return true;
    }
  }

  return false;
}

bool graph_id_type_any_exists(const Graph *graph, short id_type)
{
  const dune::Graph *graph = reinterpret_cast<const dune::Graph *>(graph);
  return Graph->id_type_exist[dune_idtype_idcode_to_index(id_type)] != 0;
}

uint32_t graph_get_eval_flags_for_id(const Graph *graph, Id *id)
{
  if (graph == nullptr) {
    /* Happens when converting objects to mesh from a python script
     * after modifying scene graph.
     *
     * Currently harmless because it's only called for temporary
     * objects which are out of the DAG anyway. */
    return 0;
  }

  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  const graph::IdNode *id_node = graph->find_id_node(graph_get_original_id(id));
  if (id_node == nullptr) {
    /* TODO: Does it mean we need to check set scene? */
    return 0;
  }

  return id_node->eval_flags;
}

void graph_get_customdata_mask_for_object(const Graph *graph,
                                          Object *ob,
                                          CustomData_MeshMasks *r_mask)
{
  if (graph == nullptr) {
    /* Happens when converting objects to mesh from a python script
     * after modifying scene graph.
     *
     * Currently harmless because it's only called for temporary
     * objects which are out of the DAG anyway. */
    return;
  }

  const dune::Graph *graph = reinterpret_cast<const dune::Graph *>(graph);
  const dune::IdNode *id_node = graph->find_id_node(graph_get_original_id(&ob->id));
  if (id_node == nullptr) {
    /* TODO: Does it mean we need to check set scene? */
    return;
  }

  r_mask->vmask |= id_node->customdata_masks.vert_mask;
  r_mask->emask |= id_node->customdata_masks.edge_mask;
  r_mask->fmask |= id_node->customdata_masks.face_mask;
  r_mask->lmask |= id_node->customdata_masks.loop_mask;
  r_mask->pmask |= id_node->customdata_masks.poly_mask;
}

Scene *graph_get_evaluated_scene(const Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  Scene *scene_cow = graph->scene_cow;
  /* TODO: Shall we expand data-block here? Or is it OK to assume
   * that caller is OK with just a pointer in case scene is not updated yet? */
  lib_assert(scene_cow != nullptr && graph::graph_copy_on_write_is_expanded(&scene_cow->id));
  return scene_cow;
}

ViewLayer *graph_get_evaluated_view_layer(const Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  Scene *scene_cow = graph_get_scene_eval(graph);
  if (scene_cow == nullptr) {
    return nullptr; /* Happens with new, not-yet-built/evaluated graphs. */
  }
  /* Do name-based lookup. */
  /* TODO: Can this be optimized? */
  ViewLayer *view_layer_orig = graph->view_layer;
  ViewLayer *view_layer_cow = (ViewLayer *)lib_findstring(
      &scene_cow->view_layers, view_layer_orig->name, offsetof(ViewLayer, name));
  lib_assert(view_layer_cow != nullptr);
  return view_layer_cow;
}

Object *graph_get_evaluated_object(const graph *graph, Object *object)
{
  return (Object *)graph_get_evaluated_id(graph, &object->id);
}

Id *graph_get_evaluated_id(const Graph *graph, Id *id)
{
  if (id == nullptr) {
    return nullptr;
  }
  /* TODO: This is a duplicate of DGraph::get_cow_id(),
   * but here we never do assert, since we don't know nature of the
   * incoming id data-block. */
  const graph::Graph *graph = (const graph::Graph *)graph;
  const graph::IdNode *id_node = graph->find_id_node(id);
  if (id_node == nullptr) {
    return id;
  }
  return id_node->id_cow;
}

void graph_api_ptr_get_eval(const Graph *graph, ApiPtr *ptr, ApiPtr *r_ptr_eval)
{
  if ((ptr == nullptr) || (r_ptr_eval == nullptr)) {
    return;
  }
  Id *orig_id = ptr->owner_id;
  Id *cow_id = graph_get_evaluated_id(graph, orig_id);
  if (ptr->owner_id == ptr->data) {
    /* For Id pointers, it's easy... */
    r_ptr_eval->owner_id = cow_id;
    r_ptr_eval->data = (void *)cow_id;
    r_ptr_eval->type = ptr->type;
  }
  else if (ptr->type == &api_PoseBone) {
    /* HACK: Since bone keyframing is quite commonly used,
     * speed things up for this case by doing a special lookup
     * for bones */
    const Object *ob_eval = (Object *)cow_id;
    PoseChannel *pchan = (PoseChannel *)ptr->data;
    const PoseChannel *pchan_eval = dune_pose_channel_find_name(ob_eval->pose, pchan->name);
    r_ptr_eval->owner_id = cow_id;
    r_ptr_eval->data = (void *)pchan_eval;
    r_ptr_eval->type = ptr->type;
  }
  else {
    /* For everything else, try to get api path of the Main-pointer,
     * then use that to look up what the COW-domain one should be
     * given the COW ID pointer as the new lookup point */
    /* TODO: Find a faster alternative, or implement support for other
     * common types too above (e.g. modifiers) */
    char *path = RNA_path_from_ID_to_struct(ptr);
    if (path) {
      ApiPtr cow_id_ptr;
      api_id_ptr_create(cow_id, &cow_id_ptr);
      if (!api_path_resolve(&cow_id_ptr, path, r_ptr_eval, nullptr)) {
        /* Couldn't find COW copy of data */
        fprintf(stderr,
                "%s: Couldn't resolve RNA path ('%s') relative to COW ID (%p) for '%s'\n",
                __func__,
                path,
                (void *)cow_id,
                orig_id->name);
      }
    }
    else {
      /* Path resolution failed - XXX: Hide this behind a debug flag */
      fprintf(stderr,
              "%s: Couldn't get api path for %s relative to %s\n",
              __func__,
              api_struct_id(ptr->type),
              orig_id->name);
    }
  }
}

Object *graph_get_original_object(Object *object)
{
  return (Object *)graph_get_original_id(&object->id);
}

Id *graph_get_original_id(Id *id)
{
  if (id == nullptr) {
    return nullptr;
  }
  if (id->orig_id == nullptr) {
    return id;
  }
  lib_assert((id->tag & LIB_TAG_COPIED_ON_WRITE) != 0);
  return (Id *)id->orig_id;
}

bool graph_is_original_id(const Id *id)
{
  /* Some explanation of the logic.
   *
   * What we want here is to be able to tell whether given ID is a result of dependency graph
   * evaluation or not.
   *
   * All the data-blocks which are created by copy-on-write mechanism will have will be tagged with
   * LIB_TAG_COPIED_ON_WRITE tag. Those data-blocks can not be original.
   *
   * Modifier stack evaluation might create special data-blocks which have all the modifiers
   * applied, and those will be tagged with LIB_TAG_COPIED_ON_WRITE_EVAL_RESULT. Such data-blocks
   * can not be original as well.
   *
   * Localization is usually happening from evaluated data-block, or will have some special pointer
   * magic which will make them to act as evaluated.
   *
   * NOTE: We consider ID evaluated if ANY of those flags is set. We do NOT require ALL of them. */
  if (id->tag &
      (LIB_TAG_COPIED_ON_WRITE | LIB_TAG_COPIED_ON_WRITE_EVAL_RESULT | LIB_TAG_LOCALIZED)) {
    return false;
  }
  return true;
}

bool graph_is_original_object(const Object *object)
{
  return graph_is_original_id(&object->id);
}

bool graph_is_evaluated_id(const Id *id)
{
  return !graph_is_original_id(id);
}

bool graph_is_evaluated_object(const Object *object)
{
  return !graph_is_original_object(object);
}

bool graph_is_fully_evaluated(const struct Graph *graph)
{
  const graph::Graph *graph = (const dune::Graph *)graph;
  /* Check whether relations are up to date. */
  if (graph->need_update) {
    return false;
  }
  /* Check whether IDs are up to date. */
  if (!graph->entry_tags.is_empty()) {
    return false;
  }
  return true;
}
