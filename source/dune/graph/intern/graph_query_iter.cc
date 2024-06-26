/**
 * Implementation of Querying and Filtering API's
 */

/* Silence warnings from copying deprecated fields. */
#define TYPES_DEPRECATED_ALLOW

#include "mem_guardedalloc.h"

#include "dune_duplilist.h"
#include "dune_geometry_set.hh"
#include "dune_idprop.h"
#include "dune_layer.h"
#include "dune_node.h"
#include "dune_object.h"

#include "lib_math.h"
#include "lib_utildefines.h"

#include "types_object_types.h"
#include "types_scene_types.h"

#include "graph.h"
#include "graph_query.h"

#include "intern/graph.h"
#include "intern/node/graph_node_id.h"

#ifndef NDEBUG
#  include "intern/eval/graph_eval_copy_on_write.h"
#endif

/* If defined, all working data will be set to an invalid state, helping
 * to catch issues when areas accessing data which is considered to be no
 * longer available. */
#undef INVALIDATE_WORK_DATA

#ifndef NDEBUG
#  define INVALIDATE_WORK_DATA
#endif

namespace graph = dune::graph;

/* ************************ DEG ITERATORS ********************* */

namespace {

void graph_invalidate_iterator_work_data(GraphObjectIterData *data)
{
#ifdef INVALIDATE_WORK_DATA
  lib_assert(data != nullptr);
  memset(&data->temp_dupli_object, 0xff, sizeof(data->temp_dupli_object));
#else
  (void)data;
#endif
}

void ensure_id_props_freed(const Object *dupli_object, Object *temp_dupli_object)
{
  if (temp_dupli_object->id.properties == nullptr) {
    /* No ID properties in temp data-block -- no leak is possible. */
    return;
  }
  if (temp_dupli_object->id.props == dupli_object->id.props) {
    /* Temp copy of object did not modify ID properties. */
    return;
  }
  /* Free memory which is owned by temporary storage which is about to get overwritten. */
  IDP_FreeProp(temp_dupli_object->id.props);
  temp_dupli_object->id.props = nullptr;
}

void ensure_boundbox_freed(const Object *dupli_object, Object *temp_dupli_object)
{
  if (temp_dupli_object->runtime.bb == nullptr) {
    /* No Bounding Box in temp data-block -- no leak is possible. */
    return;
  }
  if (temp_dupli_object->runtime.bb == dupli_object->runtime.bb) {
    /* Temp copy of object did not modify Bounding Box. */
    return;
  }
  /* Free memory which is owned by temporary storage which is about to get overwritten. */
  mem_freen(temp_dupli_object->runtime.bb);
  temp_dupli_object->runtime.bb = nullptr;
}

void free_owned_memory(DraphObjectIterData *data)
{
  if (data->dupli_object_current == nullptr) {
    /* We didn't enter duplication yet, so we can't have any dangling pointers. */
    return;
  }

  const Object *dupli_object = data->dupli_object_current->ob;
  Object *temp_dupli_object = &data->temp_dupli_object;

  ensure_id_props_freed(dupli_object, temp_dupli_object);
  ensure_boundbox_freed(dupli_object, temp_dupli_object);
}

bool graph_object_hide_original(eEvaluationMode eval_mode, Object *ob, DupliObject *dob)
{
  /* Automatic hiding if this object is being instanced on verts/faces/frames
   * by its parent. Ideally this should not be needed, but due to the wrong
   * dependency direction in the data design there is no way to keep the object
   * visible otherwise. The better solution eventually would be for objects
   * to specify which object they instance, instead of through parenting.
   *
   * This function should not be used for meta-balls. They have custom visibility rules, as hiding
   * the base meta-ball will also hide all the other balls in the group. */
  if (eval_mode == DAG_EVAL_RENDER || dob) {
    const int hide_original_types = OB_DUPLIVERTS | OB_DUPLIFACES;

    if (!dob || !(dob->type & hide_original_types)) {
      if (ob->parent && (ob->parent->transflag & hide_original_types)) {
        return true;
      }
    }
  }

  return false;
}

void graph_iterator_duplis_init(GraphObjectIterData *data, Object *object)
{
  if ((data->flag & GRAPH_ITER_OBJECT_FLAG_DUPLI) &&
      ((object->transflag & OB_DUPLI) || object->runtime.geometry_set_eval != nullptr)) {
    data->dupli_parent = object;
    data->dupli_list = object_duplilist(data->graph, data->scene, object);
    data->dupli_object_next = (DupliObject *)data->dupli_list->first;
  }
}

/* Returns false when iterator is exhausted. */
bool graph_iterator_duplis_step(GraphObjectIterData *data)
{
  if (data->dupli_list == nullptr) {
    return false;
  }

  while (data->dupli_object_next != nullptr) {
    DupliObject *dob = data->dupli_object_next;
    Object *obd = dob->ob;

    data->dupli_object_next = data->dupli_object_next->next;

    if (dob->no_draw) {
      continue;
    }
    if (obd->type == OB_MBALL) {
      continue;
    }
    if (graph_object_hide_original(data->eval_mode, dob->ob, dob)) {
      continue;
    }

    free_owned_memory(data);

    data->dupli_object_current = dob;

    /* Temporary object to evaluate. */
    Object *dupli_parent = data->dupli_parent;
    Object *temp_dupli_object = &data->temp_dupli_object;
    *temp_dupli_object = *dob->ob;
    temp_dupli_object->base_flag = dupli_parent->base_flag | BASE_FROM_DUPLI;
    temp_dupli_object->base_local_view_bits = dupli_parent->base_local_view_bits;
    temp_dupli_object->runtime.local_collections_bits =
        dupli_parent->runtime.local_collections_bits;
    temp_dupli_object->dt = MIN2(temp_dupli_object->dt, dupli_parent->dt);
    copy_v4_v4(temp_dupli_object->color, dupli_parent->color);
    temp_dupli_object->runtime.select_id = dupli_parent->runtime.select_id;
    if (dob->ob->data != dob->ob_data) {
      /* Do not modify the original boundbox. */
      temp_dupli_object->runtime.bb = nullptr;
      dune_object_replace_data_on_shallow_copy(temp_dupli_object, dob->ob_data);
    }

    /* Duplicated elements shouldn't care whether their original collection is visible or not. */
    temp_dupli_object->base_flag |= BASE_VISIBLE_DEPSGRAPH;

    int ob_visibility = dune_object_visibility(temp_dupli_object, data->eval_mode);
    if ((ob_visibility & (OB_VISIBLE_SELF | OB_VISIBLE_PARTICLES)) == 0) {
      continue;
    }

    /* This could be avoided by refactoring make_dupli() in order to track all negative scaling
     * recursively. */
    bool is_neg_scale = is_negative_m4(dob->mat);
    SET_FLAG_FROM_TEST(data->temp_dupli_object.transflag, is_neg_scale, OB_NEG_SCALE);

    copy_m4_m4(data->temp_dupli_object.obmat, dob->mat);
    invert_m4_m4(data->temp_dupli_object.imat, data->temp_dupli_object.obmat);
    data->next_object = &data->temp_dupli_object;
    lib_assert(graph::graph_validate_copy_on_write_datablock(&data->temp_dupli_object.id));
    return true;
  }

  free_owned_memory(data);
  free_object_duplilist(data->dupli_list);
  data->dupli_parent = nullptr;
  data->dupli_list = nullptr;
  data->dupli_object_next = nullptr;
  data->dupli_object_current = nullptr;
  graph_invalidate_iterator_work_data(data);
  return false;
}

/* Returns false when iterator is exhausted. */
bool graph_iterator_objects_step(GraphObjectIterData *data)
{
  graph::Graph *graph = reinterpret_cast<graph::Graph *>(data->graph);

  for (; data->id_node_index < data->num_id_nodes; data->id_node_index++) {
    graph::IdNode *id_node = graph->id_nodes[data->id_node_index];

    if (!id_node->is_directly_visible) {
      continue;
    }

    const IdType id_type = GS(id_node->id_orig->name);

    if (id_type != ID_OB) {
      continue;
    }

    switch (id_node->linked_state) {
      case graph::GRAPH_ID_LINKED_DIRECTLY:
        if ((data->flag & GRAPH_ITER_OBJECT_FLAG_LINKED_DIRECTLY) == 0) {
          continue;
        }
        break;
      case graph::GRAPH_ID_LINKED_VIA_SET:
        if ((data->flag & DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) == 0) {
          continue;
        }
        break;
      case graph::GRAPH_ID_LINKED_INDIRECTLY:
        if ((data->flag & GRAPH_ITER_OBJECT_FLAG_LINKED_INDIRECTLY) == 0) {
          continue;
        }
        break;
    }

    Object *object = (Object *)id_node->id_cow;
    lib_assert(graph::graph_validate_copy_on_write_datablock(&object->id));

    int ob_visibility = OB_VISIBLE_ALL;
    if (data->flag & GRAPH_ITER_OBJECT_FLAG_VISIBLE) {
      ob_visibility = dune_object_visibility(object, data->eval_mode);

      if (object->type != OB_MBALL && graph_object_hide_original(data->eval_mode, object, nullptr)) {
        continue;
      }
    }

    object->runtime.select_id = graph_get_original_object(object)->runtime.select_id;
    if (ob_visibility & OB_VISIBLE_INSTANCES) {
      graph_iterator_duplis_init(data, object);
    }

    if (ob_visibility & (OB_VISIBLE_SELF | OB_VISIBLE_PARTICLES)) {
      data->next_object = object;
    }
    data->id_node_index++;
    return true;
  }
  return false;
}

}  // namespace

void graph_iterator_objects_begin(LibIterator *iter, GraphObjectIterData *data)
{
  Graph *graph = data->graph;
  graph::Graph *graph = reinterpret_cast<graph::Graph *>(graph);
  const size_t num_id_nodes = graph->id_nodes.size();

  iter->data = data;

  if (num_id_nodes == 0) {
    iter->valid = false;
    return;
  }

  data->next_object = nullptr;
  data->dupli_parent = nullptr;
  data->dupli_list = nullptr;
  data->dupli_object_next = nullptr;
  data->dupli_object_current = nullptr;
  data->scene = graph_get_evaluated_scene(graph);
  data->id_node_index = 0;
  data->num_id_nodes = num_id_nodes;
  data->eval_mode = graph_get_mode(graph);
  graph_invalidate_iterator_work_data(data);

  graph_iterator_objects_next(iter);
}

void DEG_iterator_objects_next(LibIterator *iter)
{
  GraphObjectIterData *data = (GraphObjectIterData *)iter->data;
  while (true) {
    if (data->next_object != nullptr) {
      iter->current = data->next_object;
      data->next_object = nullptr;
      return;
    }
    if (graph_iterator_duplis_step(data)) {
      continue;
    }
    if (graph_iterator_objects_step(data)) {
      continue;
    }
    iter->valid = false;
    break;
  }
}

void graph_iterator_objects_end(LibIterator *iter)
{
  GraphObjectIterData *data = (GraphObjectIterData *)iter->data;
  if (data != nullptr) {
    /* Force crash in case the iterator data is referenced and accessed down
     * the line. (T51718) */
    deg_invalidate_iterator_work_data(data);
  }
}

/* ************************ DEG ID ITERATOR ********************* */

static void graph_iterator_ids_step(LibIterator *iter, graph::IdNode *id_node, bool only_updated)
{
  Id *id_cow = id_node->id_cow;

  if (!id_node->is_directly_visible) {
    iter->skip = true;
    return;
  }
  if (only_updated && !(id_cow->recalc & ID_RECALC_ALL)) {
    /* Node-tree is considered part of the data-block. */
    NodeTree *ntree = ntreeFromId(id_cow);
    if (ntree == nullptr) {
      iter->skip = true;
      return;
    }
    if ((ntree->id.recalc & ID_RECALC_NTREE_OUTPUT) == 0) {
      iter->skip = true;
      return;
    }
  }

  iter->current = id_cow;
  iter->skip = false;
}

void graph_iterator_ids_begin(LibIterator *iter, GraphIdIterData *data)
{
  Graph *graph = data->graph;
  graph::Graph *graph = reinterpret_cast<graph::Graph *>(graph);
  const size_t num_id_nodes = graph->id_nodes.size();

  iter->data = data;

  if ((num_id_nodes == 0) || (data->only_updated && !graph_id_type_any_updated(graph))) {
    iter->valid = false;
    return;
  }

  data->id_node_index = 0;
  data->num_id_nodes = num_id_nodes;

  graph::IdNode *id_node = graph->id_nodes[data->id_node_index];
  graph_iterator_ids_step(iter, id_node, data->only_updated);

  if (iter->skip) {
    graph_iterator_ids_next(iter);
  }
}

void graph_iterator_ids_next(LibIterator *iter)
{
  GraphIdIterData *data = (GraphIdIterData *)iter->data;
  Graph *graph = data->graph;
  graph::Graph *graph = reinterpret_cast<graph::Graph *>(graph);

  do {
    iter->skip = false;

    ++data->id_node_index;
    if (data->id_node_index == data->num_id_nodes) {
      iter->valid = false;
      return;
    }

    graph::IdNode *id_node = graph->id_nodes[data->id_node_index];
    graph_iterator_ids_step(iter, id_node, data->only_updated);
  } while (iter->skip);
}

void graph_iterator_ids_end(LibIterator *UNUSED(iter))
{
}
