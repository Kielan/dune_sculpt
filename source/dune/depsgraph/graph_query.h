/** Public API for Querying Depsgraph. **/

#pragma once

#include "lib_iterator.h"

#include "graph.h"
#include "graph_build.h"

/* Needed for the instance iterator. */
#include "types_object_types.h"

struct lib_Iterator;
struct CustomData_MeshMasks;
struct Graph;
struct DupliObject;
struct Id;
struct ListBase;
struct ApiPtr;
struct Scene;
struct ViewLayer;

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------- */
/** dgraph input data **/

/** Get scene that dgraph was built for. */
struct Scene *graph_get_input_scene(const Graph *graph);

/** Get view layer that depsgraph was built for. */
struct ViewLayer *graph_get_input_view_layer(const Graph *graph);

/** Get dmain that depsgraph was built for. */
struct Main *graph_get_dmain(const Graph *graph);

/** Get evaluation mode that depsgraph was built for. */
eEvaluationMode graph_get_mode(const Graph *graph);

/** Get time that depsgraph is being evaluated or was last evaluated at. */
float graph_get_ctime(const Graph *graph);

/* -------------------------------------------------------------------- */
/** graph evaluated data **/

/** Check if given id type was tagged for update. */
bool graph_id_type_updated(const struct Graph *graph, short id_type);
bool graph_id_type_any_updated(const struct Graph *graph);

/** Check if given id type is present in the dgraph */
bool graph_id_type_any_exists(const struct Graph *graph, short id_type);

/** Get additional evaluation flags for the given id. */
uint32_t graph_get_eval_flags_for_id(const struct Graph *graph, struct Id *id);

/** Get additional mesh CustomData_MeshMasks flags for the given object. */
void graph_get_customdata_mask_for_object(const struct Graph *graph,
                                          struct Object *object,
                                          struct CustomData_MeshMasks *r_mask);

/**
 * Get scene at its evaluated state.
 *
 * Technically, this is a copied-on-written and fully evaluated version of the input scene.
 * This function will check that the data-block has been expanded (and copied) from the original
 * one. Assert will happen if it's not.
 */
struct Scene *graph_get_evaluated_scene(const struct Graph *graph);

/**
 * Get view layer at its evaluated state.
 * This is a shortcut for accessing active view layer from evaluated scene.
 */
struct ViewLayer *graph_get_evaluated_view_layer(const struct Graph *graph);

/** Get evaluated version of object for given original one. */
struct Object *graph_get_evaluated_object(const struct Graph *graph, struct Object *object);

/** Get evaluated version of given ID data-block. */
struct Id *graph_get_evaluated_id(const struct Graph *graph, struct ID *id);

/** Get evaluated version of data pointed to by RNA pointer */
void graph_get_evaluated_rna_pointer(const struct DGraph *dgraph,
                                     struct ApiPtr *ptr,
                                     struct ApiPtr *r_ptr_eval);

/** Get original version of object for given evaluated one. */
struct Object *graph_get_original_object(struct Object *object);

/** Get original version of given evaluated ID data-block. */
struct Id *graph_get_original_id(struct Id *id);

/**
 * Check whether given id is an original,
 *
 * Original ids are considered all the IDs which are not covered by copy-on-write system and are
 * not out-of-main localized data-blocks.
 */
bool graph_is_original_id(const struct Id *id);
bool graph_is_original_object(const struct Object *object);

/* Opposite of the above.
 *
 * If the data-block is not original it must be evaluated, and vice versa. */

bool graph_is_evaluated_id(const struct Id *id);
bool graph_is_evaluated_object(const struct Object *object);

/**
 * Check whether depsgraph is fully evaluated. This includes the following checks:
 * - Relations are up-to-date.
 * - Nothing is tagged for update.
 */
bool graph_is_fully_evaluated(const struct Graph *graph);

/* -------------------------------------------------------------------- */
/** graph object iterators **/

enum {
  GRAPH_ITER_OBJECT_FLAG_LINKED_DIRECTLY = (1 << 0),
  GRAPH_ITER_OBJECT_FLAG_LINKED_INDIRECTLY = (1 << 1),
  GRAPH_ITER_OBJECT_FLAG_LINKED_VIA_SET = (1 << 2),
  GRAPH_ITER_OBJECT_FLAG_VISIBLE = (1 << 3),
  GRAPH_ITER_OBJECT_FLAG_DUPLI = (1 << 4),
};

typedef struct GraphObjectIterData {
  struct Graph *graph;
  int flag;

  struct Scene *scene;

  eEvaluationMode eval_mode;

  struct Object *next_object;

  /* **** Iteration over dupli-list. *** */

  /* Object which created the dupli-list. */
  struct Object *dupli_parent;
  /* List of duplicated objects. */
  struct ListBase *dupli_list;
  /* Next duplicated object to step into. */
  struct DupliObject *dupli_object_next;
  /* Corresponds to current object: current iterator object is evaluated from
   * this duplicated object. */
  struct DupliObject *dupli_object_current;
  /* Temporary storage to report fully populated DNA to the render engine or
   * other users of the iterator. */
  struct Object temp_dupli_object;

  /* **** Iteration over Id nodes **** */
  size_t id_node_index;
  size_t num_id_nodes;
} GraphObjectIterData;

void graph_iterator_objects_begin(struct lib_Iterator *iter, GraphObjectIterData *data);
void graph_iterator_objects_next(struct lib_Iterator *iter);
void graph_iterator_objects_end(struct lib_Iterator *iter);

/**
 * NOTE: Be careful with DGRAPH_ITER_OBJECT_FLAG_LINKED_INDIRECTLY objects.
 * Although they are available they have no overrides (collection_properties)
 * and will crash if you try to access it.
 */
#define GRAPH_OBJECT_ITER_BEGIN(graph_, instance_, flag_) \
  { \
    GraphObjectIterData data_ = { \
        graph_, \
        flag_, \
    }; \
\
    ITER_BEGIN (graph_iterator_objects_begin, \
                graph_iterator_objects_next, \
                graph_iterator_objects_end, \
                &data_, \
                Object *, \
                instance_)

#define GRAPH_OBJECT_ITER_END \
  ITER_END; \
  } \
  ((void)0)

/**
 * DGraph objects iterator for draw manager and final render
 */
#define GRAPH_OBJECT_ITER_FOR_RENDER_ENGINE_BEGIN(graph_, instance_) \
  GRAPH_OBJECT_ITER_BEGIN (graph_, \
                         instance_, \
                         GRAPH_ITER_OBJECT_FLAG_LINKED_DIRECTLY | \
                             GRAPH_ITER_OBJECT_FLAG_LINKED_VIA_SET | GRAPH_ITER_OBJECT_FLAG_VISIBLE | \
                             GRAPH_ITER_OBJECT_FLAG_DUPLI)

#define GRAPH_OBJECT_ITER_FOR_RENDER_ENGINE_END GRAPH_OBJECT_ITER_END

/* -------------------------------------------------------------------- */
/** graph id iterators **/

typedef struct GraphIdIterData {
  struct Graph *graph;
  bool only_updated;
  size_t id_node_index;
  size_t num_id_nodes;
} DGraphIdIterData;

void graph_iterator_ids_begin(struct lib_Iterator *iter, GraphIdIterData *data);
void graph_iterator_ids_next(struct lib_Iterator *iter);
void graph_iterator_ids_end(struct lib_Iterator *iter);

/* -------------------------------------------------------------------- */
/** dgraph traversal **/

typedef void (*GraphForeachIdCb)(Id *id, void *user_data);
typedef void (*GraphForeachIdComponentCb)(Id *id,
                                           eGraphObjectComponentType component,
                                           void *user_data);

/**
 * Modifies runtime flags in depsgraph nodes,
 * so can not be used in parallel. Keep an eye on that!
 */
void graph_foreach_ancestor_id(const Graph *graph,
                                const Id *id,
                                GraphForeachIdCb cb,
                                void *user_data);
void graph_foreach_dependent_id(const Graph *graph,
                                 const Id *id,
                                 GraphForeachIdCb cb,
                                 void *user_data);

/**
 * Starts traversal from given component of the given id, invokes callback for every other
 * component  which is directly on indirectly dependent on the source one.
 */
enum {
  /* Ignore transform solvers which depends on multiple inputs and affects final transform.
   * Is used for cases like snapping objects which are part of a rigid body simulation:
   * without this there will be "false-positive" dependencies between transform components of
   * objects:
   *
   *     object 1 transform before solver ---> solver ------> object 1 final transform
   *     object 2 transform before solver -----^     \------> object 2 final transform
   */
  GRAPH_FOREACH_COMPONENT_IGNORE_TRANSFORM_SOLVERS = (1 << 0),
};
void graph_foreach_dependent_id_component(const Graph *graph,
                                           const Id *id,
                                           eGraphObjectComponentType source_component_type,
                                           int flags,
                                           GraphForeachIdComponentCb cb,
                                           void *user_data);

void graph_foreach_id(const Graph *graph, GraphForeachIdCb cb, void *user_data);


#ifdef __cplusplus
} /* extern "C" */
#endif
