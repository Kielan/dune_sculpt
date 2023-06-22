#include <stdlib.h>

#include "lib_path_util.h"
#include "lib_utildefines.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_object.h"

#include "graph.h"

#define STATS_MAX_SIZE 16384

#ifdef API_RUNTIME

#  ifdef WITH_PYTHON
#    include "BPY_extern.h"
#  endif

#  include "lib_iter.h"
#  include "lib_math.h"

#  include "api_access.h"

#  include "dune_duplilist.h"
#  include "dune_object.h"
#  include "dune_scene.h"

#  include "graph_build.h"
#  include "graph_debug.h"
#  include "graph_query.h"

#  include "mem_guardedalloc.h"

/* **************** Object Instance **************** */

static ApiPtr api_GraphObjectInstance_object_get(ApiPtr *ptr)
{
  LibIter *iter = ptr->data;
  return apo_ptr_inherit_refine(ptr, &ApiObject, iter->current);
}

static int api_GraphObjectInstance_is_instance_get(ApiPtr *ptr)
{
  LibIter *iter = ptr->data;
  GraphObjectIterData *graph_iter = (GraphObjectIterData *)iter->data;
  return (graph_iter->dupli_object_current != NULL);
}

static ApiPtr api_graphObjectInstance_instance_object_get(ApiPtr *ptr)
{
  LibIter *iter = ptr->data;
  GraphObjectIterData *graph_iter = (GraphObjectIterData *)iter->data;
  Object *instance_object = NULL;
  if (graph_iter->dupli_object_current != NULL) {
    instance_object = graph_iter->dupli_object_current->ob;
  }
  return api_ptr_inherit_refine(ptr, &ApiObject, instance_object);
}

static bool api_GraphObjectInstance_show_self_get(ApiPtr *ptr)
{
  LibIter *iter = ptr->data;
  GraphObjectIterData *graph_iter = (GraphObjectIterData *)iter->data;
  int ob_visibility = dune_object_visibility(iter->current, graph_iter->eval_mode);
  return (ob_visibility & OB_VISIBLE_SELF) != 0;
}

static bool api_GraphObjectInstance_show_particles_get(ApiPtr *ptr)
{
  LibIter *iter = ptr->data;
  GraphObjectIterData *graph_iter = (GraphObjectIterData *)iter->data;
  int ob_visibility = dune_object_visibility(iter->current, graph_iter->eval_mode);
  return (ob_visibility & OB_VISIBLE_PARTICLES) != 0;
}

static ApiPtr api_GraphObjectInstance_parent_get(ApiPtr *ptr)
{
  LibIter *iter = ptr->data;
  GraphObjectIterData *graph_iter = (GraphObjectIterData *)iter->data;
  Object *dupli_parent = NULL;
  if (graph_iter->dupli_object_current != NULL) {
    dupli_parent = deg_iter->dupli_parent;
  }
  return api_ptr_inherit_refine(ptr, &ApiObject, dupli_parent);
}

static ApiPtr api_GraphObjectInstance_particle_system_get(ApiPtr *ptr)
{
  BLI_Iterator *iterator = ptr->data;
  DEGObjectIterData *deg_iter = (DEGObjectIterData *)iterator->data;
  struct ParticleSystem *particle_system = NULL;
  if (deg_iter->dupli_object_current != NULL) {
    particle_system = deg_iter->dupli_object_current->particle_system;
  }
  return api_ptr_inherit_refine(ptr, &ApiParticleSystem, particle_system);
}

static void api_GraphObjectInstance_persistent_id_get(PointerRNA *ptr, int *persistent_id)
{
  LibIter *iter = ptr->data;
  GraphObjectIterData *graph_iter = (GraphObjectIterData *)iterator->data;
  if (graph_iter->dupli_object_current != NULL) {
    memcpy(persistent_id,
           graph_iter->dupli_object_current->persistent_id,
           sizeof(graph_iter->dupli_object_current->persistent_id));
  }
  else {
    memset(persistent_id, 0, sizeof(graph_iter->dupli_object_current->persistent_id));
  }
}

static unsigned int api_GraphObjectInstance_random_id_get(PointerRNA *ptr)
{
  LibIter *iter = ptr->data;
  GraohObjectIterData *deg_iter = (GraphObjectIterData *)iterator->data;
  if (deg_iter->dupli_object_current != NULL) {
    return deg_iter->dupli_object_current->random_id;
  }
  else {
    return 0;
  }
}

static void api_GraphObjectInstance_matrix_world_get(ApiPtr *ptr, float *mat)
{
  LibIter iter->data;
  GraphObjectIterData *graph_iter = (GraphObjectIterData *)iter->data;
  if (graph_iter->dupli_object_current != NULL) {
    copy_m4_m4((float(*)[4])mat, graph_iter->dupli_object_current->mat);
  }
  else {
    /* We can return actual object's matrix here, no reason to return identity matrix
     * when this is not actually an instance... */
    Object *ob = (Object *)iterator->current;
    copy_m4_m4((float(*)[4])mat, ob->obmat);
  }
}

static void api_GraphObjectInstance_orco_get(PointerRNA *ptr, float *orco)
{
  LibIter *iter = ptr->data;
  GraphObjectIterData *graph_iter = (GraphObjectIterData *)iterator->data;
  if (graph_iter->dupli_object_current != NULL) {
    copy_v3_v3(orco, deg_iter->dupli_object_current->orco);
  }
  else {
    zero_v3(orco);
  }
}

static void api_GraphObjectInstance_uv_get(ApiPtr *ptr, float *uv)
{
  LibIter *iter = ptr->data;
  GraphObjectIterData *graph_iter = (GraphObjectIterData *)iter->data;
  if (graph_iter->dupli_object_current != NULL) {
    copy_v2_v2(uv, graph_iter->dupli_object_current->uv);
  }
  else {
    zero_v2(uv);
  }
}

/* ******************** Sorted  ***************** */

static int api_Graph_mode_get(ApiPtr *ptr)
{
  Graph *graph = ptr->data;
  return graph_get_mode(graph);
}

/* ******************** Updates ***************** */

static ApiPtr api_GraphUpdate_id_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiId, ptr->data);
}

static bool api_GraphUpdate_is_updated_transform_get(ApiPtr *ptr)
{
  Id *id = ptr->data;
  return ((id->recalc & ID_RECALC_TRANSFORM) != 0);
}

static bool api_GraphUpdate_is_updated_shading_get(ApiPtr *ptr)
{
  /* Assume any animated parameters can affect shading, we don't have fine
   * grained enough updates to distinguish this. */
  Id *id = ptr->data;
  return ((id->recalc & (ID_RECALC_SHADING | ID_RECALC_ANIMATION)) != 0);
}

static bool api_GraphUpdate_is_updated_geometry_get(ApiPtr *ptr)
{
  Id *id = ptr->data;
  if (id->recalc & ID_RECALC_GEOMETRY) {
    return true;
  }
  if (GS(id->name) != ID_OB) {
    return false;
  }
  Object *object = (Object *)id;
  Id *data = object->data;
  if (data == NULL) {
    return false;
  }
  return ((data->recalc & ID_RECALC_ALL) != 0);
}

/* **************** Depsgraph **************** */

static void api_graph_debug_relations_graphviz(Graph *graph, const char *filename)
{
  FILE *f = fopen(filename, "w");
  if (f == NULL) {
    return;
  }
  graph_debug_relations_graphviz(graph, f, "Graph");
  fclose(f);
}

static void api_graph_debug_stats_gnuplot(Graph *graph,
                                          const char *filename,
                                          const char *output_filename)
{
  FILE *f = fopen(filename, "w");
  if (f == NULL) {
    return;
  }
  graph_debug_stats_gnuplot(graph, f, "Timing Statistics", output_filename);
  fclose(f);
}

static void api_graph_debug_tag_update(graph *graph)
{
  graph_tag_relations_update(graph);
}

static void api_graph_debug_stats(Graph *graph, char *result)
{
  size_t outer, ops, rels;
  graph_stats_simple(graph, &outer, &ops, &rels);
  lib_snprintf(result,
               STATS_MAX_SIZE,
               "Approx %zu Operations, %zu Relations, %zu Outer Nodes",
               ops,
               rels,
               outer);
}

static void api_graph_update(Graph *graph, Main *main, ReportList *reports)
{
  if (graph_is_evaluating(graph)) {
    dune_report(reports, RPT_ERROR, "Dependency graph update requested during evaluation");
    return;
  }

#  ifdef WITH_PYTHON
  /* Allow drivers to be evaluated */
  BPy_BEGIN_ALLOW_THREADS;
#  endif

  dune_scene_graph_update_tagged(graph, main);

#  ifdef WITH_PYTHON
  BPy_END_ALLOW_THREADS;
#  endif
}

/* Iteration over objects, simple version */

static void api_graph_objects_begin(CollectionPtrIter *iter, ApiPtr *ptr)
{
  iter->internal.custom = mem_callocn(sizeof(LibIter), __func__);
  GraphObjectIterData *data = mem_callocn(sizeof(GraphObjectIterData), __func__);

  data->graph = (Graph *)ptr->data;
  data->flag = GRAPH_ITER_OBJECT_FLAG_LINKED_DIRECTLY | GRAPH_ITER_OBJECT_FLAG_VISIBLE |
               GRAPH_ITER_OBJECT_FLAG_LINKED_VIA_SET;

  ((LibIter *)iter->internal.custom)->valid = true;
  graph_iter_objects_begin(iter->internal.custom, data);
  iter->valid = ((LibIter *)iter->internal.custom)->valid;
}

static void api_graph_objects_next(CollectionPropIter *iter)
{
  graph_iter_objects_next(iter->internal.custom);
  iter->valid = ((LibIter *)iter->internal.custom)->valid;
}

static void api_graph_objects_end(CollectionPropIter *iter)
{
  graph_iter_objects_end(iter->internal.custom);
  mem_freen(((LibIter *)iter->internal.custom)->data));
  mem_freen(iter->internal.custom);
}

static ApiPtr api_Graph_objects_get(CollectionPropIter *iter)
{
  Object *ob = ((LibIter *)iter->internal.custom)->current;
  return api_ptr_inherit_refine(&iter->parent, &ApiObject, ob);
}

/* Iteration over objects, extended version
 *
 * Contains extra information about duplicator and persistent Id. */

/* XXX Ugly python seems to query next item of an iterator before using current one (see T57558).
 * This forces us to use that nasty ping-pong game between two sets of iterator data,
 * so that previous one remains valid memory for python to access to. Yuck. */
typedef struct ApiGraphInstancesIter {
  LibIter iters[2];
  GraphObjectIterData graph_data[2];
  DupliObject dupli_object_current[2];
  int counter;
} ApiGraphInstancesIter;

static void api_graph_object_instances_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  api_Graph_Instances_Iter *di_it = iter->internal.custom = mem_callocn(sizeof(*di_it),
                                                                        __func__);

  GraphObjectIterData *data = &di_it->deg_data[0];
  data->graph = (Graph *)ptr->data;
  data->flag = GRAPH_ITER_OBJECT_FLAG_LINKED_DIRECTLY | GRAPH_ITER_OBJECT_FLAG_LINKED_VIA_SET |
               GRAPH_ITER_OBJECT_FLAG_VISIBLE | GRAPH_ITER_OBJECT_FLAG_DUPLI;

  di_it->iters[0].valid = true;
  graph_iter_objects_begin(&di_it->iters[0], data);
  iter->valid = di_it->iters[0].valid;
}

static void api_graph_object_instances_next(CollectionPropIter *iter)
{
  api_graph_Instances_Iterator *di_it = (ApiGraphInstancesIter *)
                                                iter->internal.custom;

  /* We need to copy current iterator status to next one being worked on. */
  di_it->iters[(di_it->counter + 1) % 2] = di_it->iterators[di_it->counter % 2];
  di_it->graph_data[(di_it->counter + 1) % 2] = di_it->deg_data[di_it->counter % 2];
  di_it->counter++;

  di_it->iters[di_it->counter % 2].data = &di_it->deg_data[di_it->counter % 2];
  graph_iter_objects_next(&di_it->iterators[di_it->counter % 2]);
  /* Dupli_object_current is also temp memory generated during the iterations,
   * it may be freed when last item has been iterated,
   * so we have same issue as with the iterator itself:
   * we need to keep a local copy, which memory remains valid a bit longer,
   * for Python accesses to work. */
  if (di_it->graph_data[di_it->counter % 2].dupli_object_current != NULL) {
    di_it->dupli_object_current[di_it->counter % 2] =
        *di_it->deg_data[di_it->counter % 2].dupli_object_current;
    di_it->graph_data[di_it->counter % 2].dupli_object_current =
        &di_it->dupli_object_current[di_it->counter % 2];
  }
  iter->valid = di_it->iters[di_it->counter % 2].valid;
}

static void api_graph_object_instances_end(CollectionPropIter *iter)
{
  api_graph_instances_iter *di_it = (ApiGraphInstancesIter *)
  iter->internal.custom;
  graph_iter_objects_end(&di_it->iters[0]);
  graph_iter_objects_end(&di_it->iters[1]);
  mem_freen(di_it);
}

static ApiPtr api_graph_object_instances_get(CollectionPropIter *iter)
{
  api_graph_Instances_Iterator *di_it = (ApiGraphInstancesIt *)
                                                iter->internal.custom;
  LibIter *iter = &di_it->iters[di_it->counter % 2];
  return api_ptr_inherit_refine(&iter->parent, &ApiGraphObjectInstance, iter);
}

/* Iteration over evaluated IDs */

static void api_graph_ids_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  iter->internal.custom = mem_callocn(sizeof(LibIter), __func__);
  GraphIdIterData *data = mem_callocn(sizeof(GraphIdIterData), __func__);

  data->graph = (Graph *)ptr->data;

  ((LibIter *)iter->internal.custom)->valid = true;
  graph_iter_ids_begin(iter->internal.custom, data);
  iter->valid = ((LibIter *)iter->internal.custom)->valid;
}

static void api_graph_ids_next(CollectionPropIter *iter)
{
  graph_iter_ids_next(iter->internal.custom);
  iter->valid = ((LibIter *)iter->internal.custom)->valid;
}

static void api_Graph_ids_end(CollectionPropIter *iter)
{
  graph_iter_ids_end(iter->internal.custom);
  mem_freen(((LibIter *)iter->internal.custom)->data);
  mem_freen(iter->internal.custom);
}

static ApiPtr api_Graph_ids_get(CollectionPropIter *iter)
{
  Id *id = ((LibIter *)iter->internal.custom)->current;
  return api_ptr_inherit_refine(&iter->parent, &ApiId, id);
}

static void api_graph_updates_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  iter->internal.custom = mem_callocn(sizeof(LibIter), __func__);
  GraphIdIterData *data = mem_callocm(sizeof(GraphIdIterData), __func__);

  data->graph = (Graph *)ptr->data;
  data->only_updated = true;

  ((LibIter *)iter->internal.custom)->valid = true;
  graph_iter_ids_begin(iter->internal.custom, data);
  iter->valid = ((LibIter *)iter->internal.custom)->valid;
}

static ApiPtr api_graph_updates_get(CollectionPropIter *iter)
{
  Id *id = ((LibIter *)iter->internal.custom)->current;
  return api_ptr_inherit_refine(&iter->parent, &ApiGraphUpdate, id);
}

static Id *api_graph_id_eval_get(Graph *graph, Id *id_orig)
{
  return graph_get_evaluated_id(graph, id_orig);
}

static bool api_graph_id_type_updated(Graph *graph, int id_type)
{
  return graph_id_type_updated(graph, id_type);
}

static ApiPtr api_graph_scene_get(ApiPtr *ptr)
{
  Graph *graph = (Graph *)ptr->data;
  Scene *scene = graph_get_input_scene(graph);
  ApiPtr newptr;
  api_ptr_create(&scene->id, &ApiScene, scene, &newptr);
  return newptr;
}

static ApiPte api_graph_view_layer_get(ApiPtr *ptr)
{
  Graph *graph = (Graph *)ptr->data;
  Scene *scene = graph_get_input_scene(graph);
  ViewLayer *view_layer = graph_get_input_view_layer(graph);
  ApiPtr newptr;
  api_ptr_create(&scene->id, &ApiViewLayer, view_layer, &newptr);
  return newptr;
}

static ApiPointer api_graph_scene_eval_get(ApiPtr *ptr)
{
  Graph *graph = (Graph *)ptr->data;
  Scene *scene_eval = graph_get_evaluated_scene(graph);
  ApiPtr newptr;
  api_ptr_create(&scene_eval->id, &ApiScene, scene_eval, &newptr);
  return newptr;
}

static ApiPtr api_graph_view_layer_eval_get(ApiPtr *ptr)
{
  Graph *graph = (Graph *)ptr->data;
  Scene *scene_eval = graph_get_evaluated_scene(graph);
  ViewLayer *view_layer_eval = graph_get_evaluated_view_layer(graph);
  ApiPtr newptr;
  api_ptr_create(&scene_eval->id, &ApiViewLayer, view_layer_eval, &newptr);
  return newptr;
}

#else

static void api_def_graph_instance(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "GraphObjectInstance", NULL);
  api_def_struct_ui_text(sapi,
                         "Dependency Graph Object Instance",
                         "Extended information about dependency graph object iterator "
                         "(Warning: All data here is 'evaluated' one, not original .blend IDs)");

  prop = api_def_prop(srna, "object", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_ui_text(prop, "Object", "Evaluated object the iterator points to");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_pointer_fns(prop, "api_GraphObjectInstance_object_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "show_self", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Show Self", "The object geometry itself should be visible in the render");
  api_def_prop_bool_fns(prop, "api_GraphObjectInstance_show_self_get", NULL);

  prop = api_def_prop(sapi, "show_particles", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Show Particles", "Particles part of the object should be visible in the render");
  api_def_prop_bool_fns(prop, "api_GraphObjectInstance_show_particles_get", NULL);

  prop = api_def_prop(sapi, "is_instance", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Is Instance", "Denotes if the object is generated by another object");
  api_def_prop_bool_fns(prop, "api_GraphObjectInstance_is_instance_get", NULL);

  prop = api_def_prop(sapi, "instance_object", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_ui_text(
      prop, "Instance Object", "Evaluated object which is being instanced by this iterator");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ptr_fns(
      prop, "api_GraphObjectInstance_instance_object_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "parent", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_ui_text(
      prop, "Parent", "If the object is an instance, the parent object that generated it");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ptr_fns(prop, "api_GraphObjectInstance_parent_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "particle_system", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "ParticleSystem");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Particle System", "Evaluated particle system that this object was instanced from");
  api_def_prop_ptr_fns(
      prop, "api_GraphObjectInstance_particle_system_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "persistent_id", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(
      prop,
      "Persistent ID",
      "Persistent identifier for inter-frame matching of objects with motion blur");
  api_def_prop_array(prop, MAX_DUPLI_RECUR);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_int_fns(prop, "api_GraphObjectInstance_persistent_id_get", NULL, NULL);

  prop = api_def_prop(sapi, "random_id", PROP_INT, PROP_UNSIGNED);
  aoi_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Instance Random ID", "Random id for this instance, typically for randomized shading");
  api_def_prop_int_fns(prop, "api_GraphObjectInstance_random_id_get", NULL, NULL);

  prop = api_def_prop(sapi, "matrix_world", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_multi_array(prop, 2, api_matrix_dimsize_4x4);
  api_def_prop_ui_text(prop, "Generated Matrix", "Generated transform matrix in world space");
  api_def_prop_float_funcs(prop, "api_GraphObjectInstance_matrix_world_get", NULL, NULL);

  prop = api_def_prop(sapi, "orco", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Generated Coordinates", "Generated coordinates in parent object space");
  api_def_prop_float_fns(prop, "api_GraphObjectInstance_orco_get", NULL, NULL);

  prop = api_def_prop(sapi, "uv", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(prop, "UV Coordinates", "UV coordinates in parent object space");
  api_def_prop_array(prop, 2);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_float_fns(prop, "api_GraphObjectInstance_uv_get", NULL, NULL);
}

static void api_def_graph_update(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "GraphUpdate", NULL);
  api_def_struct_ui_text(sapi, "Dependency Graph Update", "Information about ID that was updated");

  prop = api_def_prop(sapi, "id", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "ID");
  api_def_prop_ui_text(prop, "ID", "Updated data-block");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ptr_fns(prop, "api_GraphUpdate_id_get", NULL, NULL, NULL);

  /* Use term 'is_updated' instead of 'is_dirty' here because this is a signal
   * that users of the depsgraph may want to update their data (render engines for eg). */
  prop = api_def_prop(sapi, "is_updated_transform", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Transform", "Object transformation is updated");
  api_def_prop_bool_fns(prop, "api_GraphUpdate_is_updated_transform_get", NULL);

  prop = api_def_prop(sapi, "is_updated_geometry", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Geometry", "Object geometry is updated");
  api_def_prop_bool_fns(prop, "api_GraphUpdate_is_updated_geometry_get", NULL);

  prop = api_def_prop(sapi, "is_updated_shading", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE | PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Shading", "Object shading is updated");
  api_def_prop_bool_fns(prop, "api_GraphUpdate_is_updated_shading_get", NULL);
}

static void api_def_graph(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;
  ApiProp *prop;

  static EnumPropItem enum_graph_mode_items[] = {
      {DAG_EVAL_VIEWPORT, "VIEWPORT", 0, "Viewport", "Viewport non-rendered mode"},
      {DAG_EVAL_RENDER, "RENDER", 0, "Render", "Render"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "graph", NULL);
  api_def_struct_ui_text(sapi, "Dependency Graph", "");

  prop = api_def_enum(sapi, "mode", enum_graph_mode_items, 0, "Mode", "Evaluation mode");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_enum_fns(prop, "api_graph_mode_get", NULL, NULL);

  /* Debug helpers. */

  fn = api_def_fn(
      sapi, "debug_relations_graphviz", "api_graph_debug_relations_graphviz");
  parm = api_def_string_file_path(
      fn, "filename", NULL, FILE_MAX, "File Name", "Output path for the graphviz debug file");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "debug_stats_gnuplot", "api_graph_debug_stats_gnuplot");
  parm = api_def_string_file_path(
      fn, "filename", NULL, FILE_MAX, "File Name", "Output path for the gnuplot debug file");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string_file_path(fn,
                                  "output_filename",
                                  NULL,
                                  FILE_MAX,
                                  "Output File Name",
                                  "File name where gnuplot script will save the result");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "debug_tag_update", "api_graph_debug_tag_update");

  fn = api_def_fn(sapi, "debug_stats", "api_graph_debug_stats");
  api_def_fn_ui_description(fn, "Report the number of elements in the Dependency Graph");
  /* weak!, no way to return dynamic string type */
  parm = api_def_string(fn, "result", NULL, STATS_MAX_SIZE, "result", "");
  api_def_param_flags(parm, PROP_THICK_WRAP, 0); /* needed for string return value */
  api_def_fn_output(fn, parm);

  /* Updates. */

  fn = api_def_fn(sapi, "update", "api_graph_update");
  api_def_fn_ui_description(
      fn,
      "Re-evaluate any modified data-blocks, for example for animation or modifiers. "
      "This invalidates all references to evaluated data-blocks from this dependency graph.");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_REPORTS);

  /* Queries for original data-blocks (the ones depsgraph is built for). */

  prop = api_def_prop(sapi, "scene", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Scene");
  api_def_prop_ptr_fns(prop, "api_graph_scene_get", NULL, NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Scene", "Original scene dependency graph is built for");

  prop = api_def_prop(sapi, "view_layer", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "ViewLayer");
  api_def_prop_pointer_funcs(prop, "api_graph_view_layer_get", NULL, NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "View Layer", "Original view layer dependency graph is built for");

  /* Queries for evaluated data-blocks (the ones depsgraph is evaluating). */

  fn = api_def_fn(sapi, "id_eval_get", "api_graph_id_eval_get");
  parm = api_def_ptr(
      fn, "id", "ID", "", "Original ID to get evaluated complementary part for");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "id_eval", "ID", "", "Evaluated ID for the given original one");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "id_type_updated", "api_graph_id_type_updated");
  parm = api_def_enum(fn, "id_type", api_enum_id_type_items, 0, "ID Type", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn,
                      "updated",
                      false,
                      "Updated",
                      "True if any datablock with this type was added, updated or removed");
  api_def_fn_return(fn, parm);

  prop = api_def_prop(sapi, "scene_eval", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Scene");
  api_def_prop_ptr_fns(prop, "api_graph_scene_eval_get", NULL, NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Scene", "Original scene dependency graph is built for");

  prop = api_def_prop(sapi, "view_layer_eval", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "ViewLayer");
  api_def_prop_ptr_fns(prop, "api_graph_view_layer_eval_get", NULL, NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "View Layer", "Original view layer dependency graph is built for");

  /* Iterators. */
  prop = api_def_prop(sapi, "ids", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "ID");
  api_def_prop_collection_fns(prop,
                              "api_graph_ids_begin",
                              "api_graph_ids_next",
                              "api_graph_ids_end",
                              "api_graph_ids_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(prop, "IDs", "All evaluated data-blocks");

  prop = api_def_prop(sapi, "objects", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_collection_fns(prop,
                              "api_graph_objects_begin",
                              "api_graph_objects_next",
                              "api_graph_objects_end",
                              "api_graph_objects_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(prop, "Objects", "Evaluated objects in the dependency graph");

  prop = api_def_prop(sapi, "object_instances", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "GraphObjectInstance");
  api_def_prop_collection_fns(prop,
                              "api_graph_object_instances_begin",
                              "api_graph_object_instances_next",
                              "api_graph_object_instances_end",
                              "api_graph_object_instances_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(prop,
                      "Object Instances",
                      "All object instances to display or render "
                      "(Warning: Only use this as an iterator, never as a sequence, "
                      "and do not keep any references to its items)");

  prop = api_def_prop(sapi, "updates", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "graphUpdate");
  api_def_prop_collection_fns(prop,
                              "api_graph_updates_begin",
                              "api_graph_ids_next",
                              "api_graph_ids_end",
                              "api_graph_updates_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(prop, "Updates", "Updates to data-blocks");
}

void api_def_graph(DuneApi *dapi)
{
  api_def_graph_instance(dapi);
  api_def_graph_update(dapi);
  api_def_graph(dapi);
}

#endif
