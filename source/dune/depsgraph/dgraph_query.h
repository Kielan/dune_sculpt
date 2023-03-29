/** Public API for Querying Depsgraph. **/

#pragma once

#include "lib_iterator.h"

#include "dgraph.h"
#include "dgraph_build.h"

/* Needed for the instance iterator. */
#include "types_object_types.h"

struct lib_Iterator;
struct CustomData_MeshMasks;
struct DGraph;
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
struct Scene *dgraph_get_input_scene(const DGraph *graph);

/** Get view layer that depsgraph was built for. */
struct ViewLayer *graph_get_input_view_layer(const DGraph *graph);

/** Get dmain that depsgraph was built for. */
struct Main *graph_get_dmain(const DGraph *graph);

/** Get evaluation mode that depsgraph was built for. */
eEvaluationMode dgraph_get_mode(const DGraph *graph);

/** Get time that depsgraph is being evaluated or was last evaluated at. */
float dgraph_get_ctime(const DGraph *graph);

/* -------------------------------------------------------------------- */
/** dgraph evaluated data **/

/** Check if given id type was tagged for update. */
bool dgraph_id_type_updated(const struct DGraph *dgraph, short id_type);
bool dgraph_id_type_any_updated(const struct DGraph *dgraph);

/** Check if given id type is present in the dgraph */
bool dgraph_id_type_any_exists(const struct DGraph *dgraph, short id_type);

/** Get additional evaluation flags for the given id. */
uint32_t dgraph_get_eval_flags_for_id(const struct DGraph *graph, struct ID *id);

/** Get additional mesh CustomData_MeshMasks flags for the given object. */
void dgraph_get_customdata_mask_for_object(const struct DGraph *graph,
                                           struct Object *object,
                                           struct CustomData_MeshMasks *r_mask);

/**
 * Get scene at its evaluated state.
 *
 * Technically, this is a copied-on-written and fully evaluated version of the input scene.
 * This function will check that the data-block has been expanded (and copied) from the original
 * one. Assert will happen if it's not.
 */
struct Scene *dgraph_get_evaluated_scene(const struct Depsgraph *graph);

/**
 * Get view layer at its evaluated state.
 * This is a shortcut for accessing active view layer from evaluated scene.
 */
struct ViewLayer *dgraph_get_evaluated_view_layer(const struct Depsgraph *graph);

/** Get evaluated version of object for given original one. */
struct Object *dgraph_get_evaluated_object(const struct Depsgraph *depsgraph, struct Object *object);

/** Get evaluated version of given ID data-block. */
struct Id *dgraph_get_evaluated_id(const struct DGraph *depsgraph, struct ID *id);

/** Get evaluated version of data pointed to by RNA pointer */
void dgraph_get_evaluated_rna_pointer(const struct DGraph *dgraph,
                                   struct ApiPtr *ptr,
                                   struct ApiPtr *r_ptr_eval);

/** Get original version of object for given evaluated one. */
struct Object *dgraph_get_original_object(struct Object *object);

/** Get original version of given evaluated ID data-block. */
struct Id *dgraph_get_original_id(struct ID *id);

/**
 * Check whether given id is an original,
 *
 * Original ids are considered all the IDs which are not covered by copy-on-write system and are
 * not out-of-main localized data-blocks.
 */
bool dgraph_is_original_id(const struct Id *id);
bool dgraph_is_original_object(const struct Object *object);

/* Opposite of the above.
 *
 * If the data-block is not original it must be evaluated, and vice versa. */

bool dgraph_is_evaluated_id(const struct Id *id);
bool dgraph_is_evaluated_object(const struct Object *object);

/**
 * Check whether depsgraph is fully evaluated. This includes the following checks:
 * - Relations are up-to-date.
 * - Nothing is tagged for update.
 */
bool dgraph_is_fully_evaluated(const struct DGraph *dgraph);

/* -------------------------------------------------------------------- */
/** dgraph object iterators **/

enum {
  DGRAPH_ITER_OBJECT_FLAG_LINKED_DIRECTLY = (1 << 0),
  DGRAPH_ITER_OBJECT_FLAG_LINKED_INDIRECTLY = (1 << 1),
  DGRAPH_ITER_OBJECT_FLAG_LINKED_VIA_SET = (1 << 2),
  DGRAPH_ITER_OBJECT_FLAG_VISIBLE = (1 << 3),
  DGRAPH_ITER_OBJECT_FLAG_DUPLI = (1 << 4),
};

typedef struct DGraphObjectIterData {
  struct DGraph *graph;
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
} DGraphObjectIterData;

void dgraph_iterator_objects_begin(struct lib_Iterator *iter, DGraphObjectIterData *data);
void dgraph_iterator_objects_next(struct lib_Iterator *iter);
void dgraph_iterator_objects_end(struct lib_Iterator *iter);

/**
 * NOTE: Be careful with DGRAPH_ITER_OBJECT_FLAG_LINKED_INDIRECTLY objects.
 * Although they are available they have no overrides (collection_properties)
 * and will crash if you try to access it.
 */
#define DGRAPH_OBJECT_ITER_BEGIN(graph_, instance_, flag_) \
  { \
    DGraphObjectIterData data_ = { \
        graph_, \
        flag_, \
    }; \
\
    ITER_BEGIN (dgraph_iterator_objects_begin, \
                dgraph_iterator_objects_next, \
                dgraph_iterator_objects_end, \
                &data_, \
                Object *, \
                instance_)

#define DGRAPH_OBJECT_ITER_END \
  ITER_END; \
  } \
  ((void)0)

/**
 * DGraph objects iterator for draw manager and final render
 */
#define DGRAPH_OBJECT_ITER_FOR_RENDER_ENGINE_BEGIN(graph_, instance_) \
  DGRAPH_OBJECT_ITER_BEGIN (graph_, \
                         instance_, \
                         DGRAPH_ITER_OBJECT_FLAG_LINKED_DIRECTLY | \
                             DGRAPH_ITER_OBJECT_FLAG_LINKED_VIA_SET | DGRAPH_ITER_OBJECT_FLAG_VISIBLE | \
                             DGRAPH_ITER_OBJECT_FLAG_DUPLI)

#define DGRAPH_OBJECT_ITER_FOR_RENDER_ENGINE_END DEG_OBJECT_ITER_END

/* -------------------------------------------------------------------- */
/** graph id iterators **/

typedef struct DGraphIdIterData {
  struct DGraph *graph;
  bool only_updated;
  size_t id_node_index;
  size_t num_id_nodes;
} DGraphIdIterData;

void dgraph_iterator_ids_begin(struct lib_Iterator *iter, DGraphIdIterData *data);
void dgraph_iterator_ids_next(struct lib_Iterator *iter);
void dgraph_iterator_ids_end(struct lib_Iterator *iter);

/* -------------------------------------------------------------------- */
/** dgraph traversal **/

typedef void (*DGraphForeachIdCb)(Id *id, void *user_data);
typedef void (*DGraphForeachIdComponentCb)(Id *id,
                                           eDGraphObjectComponentType component,
                                           void *user_data);

/**
 * Modifies runtime flags in depsgraph nodes,
 * so can not be used in parallel. Keep an eye on that!
 */
void dgraph_foreach_ancestor_id(const DGraph *dgraph,
                                const Id *id,
                                DGraphForeachIdCb cb,
                                void *user_data);
void dgraph_foreach_dependent_id(const DGraph *dgraph,
                                 const Id *id,
                                 DGraphForeachIdCb cb,
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
  DGRAPH_FOREACH_COMPONENT_IGNORE_TRANSFORM_SOLVERS = (1 << 0),
};
void dgraph_foreach_dependent_id_component(const DGraph *dgraph,
                                           const Id *id,
                                           eDGraphObjectComponentType source_component_type,
                                           int flags,
                                           DGraphForeachIDComponentCb cb,
                                           void *user_data);

void dgraph_foreach_id(const DGraph *dgraph, DGraphForeachIdCb cb, void *user_data);


#ifdef __cplusplus
} /* extern "C" */
#endif
