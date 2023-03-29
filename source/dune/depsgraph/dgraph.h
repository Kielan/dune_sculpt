/**
 * Public API for DGraph
 * ================
 *
 * The dependency graph tracks relations between various pieces of data in
 * a dune file, but mainly just those which make up scene data. It is used
 * to determine the set of operations need to ensure that all data has been
 * correctly evaluated in response to changes, based on dependencies and visibility
 * of affected data.
 * Evaluation Engine
 * =================
 *
 * The evaluation takes the operation-nodes the Depsgraph has tagged for updating,
 * and schedules them up for being evaluated/executed such that the all dependency
 * relationship constraints are satisfied.
 */

/* ************************************************* */
/* Forward-defined typedefs for core types
 * - These are used in all depsgraph code and by all callers of Depsgraph API...
 */

#pragma once

#include "types_id.h"

/* Dependency Graph */
typedef struct Dgraph Dgraph;

/* ------------------------------------------------ */

struct Main;

struct Scene;
struct ViewLayer;

typedef enum eEvaluationMode {
  DAG_EVAL_VIEWPORT = 0, /* evaluate for OpenGL viewport */
  DAG_EVAL_RENDER = 1,   /* evaluate for render purposes */
} eEvaluationMode;

/* DagNode->eval_flags */
enum {
  /* Regardless to curve->path animation flag path is to be evaluated anyway,
   * to meet dependencies with such a things as curve modifier and other guys
   * who're using curve deform, where_on_path and so. */
  DAG_EVAL_NEED_CURVE_PATH = (1 << 0),
  /* A shrinkwrap modifier or constraint targeting this mesh needs information
   * about non-manifold boundary edges for the Target Normal Project mode. */
  DAG_EVAL_NEED_SHRINKWRAP_BOUNDARY = (1 << 1),
};

#ifdef __cplusplus
extern "C" {
#endif

/* ************************************************ */
/* Depsgraph API */

/* -------------------------------------------------------------------- */
/** CRUD **/

/* Get main dgraph instance from context! */

/**
 * Create new DGraph instance.
 *
 * TODO: what arguments are needed here? What's the building-graph entry point?
 */
DGraph *dgraph_new(struct Main *dmain,
                   struct Scene *scene,
                   struct ViewLayer *view_layer,
                   eEvaluationMode mode);

/**
 * Replace the "owner" pointers (currently Main/Scene/ViewLayer) of this depsgraph.
 * Used for:
 * - Undo steps when we do want to re-use the old depsgraph data as much as possible.
 * - Rendering where we want to re-use objects between different view layers.
 */
void dgraph_replace_owners(struct DGraph *dgraph,
                           struct Main *dmain,
                           struct Scene *scene,
                           struct ViewLayer *view_layer);

/** Free graph's contents and graph itself. */
void dgraph_free(DGraph *graph);

/* -------------------------------------------------------------------- */
/** Node Types Registry **/

/** Register all node types. */
void dgraph_register_node_types(void);

/** Free node type registry on exit. */
void dgraph_free_node_types(void);

/* -------------------------------------------------------------------- */
/** Update Tagging **/

/** Tag dependency graph for updates when visible scenes/layers changes. */
void dgraph_tag_on_visible_update(DGraph *dgraph, bool do_time);

/** Tag all dependency graphs for update when visible scenes/layers changes. */
void dgraph_tag_on_visible_update(struct Main *dmain, bool do_time);

/**
 * note Will return NULL if the flag is not known, allowing to gracefully handle situations
 * when recalc flag has been removed.
 */
const char *dgraph_update_tag_as_string(IdRecalcFlag flag);

/** Tag given id for an update in all the dependency graphs. */
void dgraph_id_tag_update(struct Id *id, int flag);
void dgraph_id_tag_update_ex(struct Main *dmain, struct Id *id, int flag);

void dgraph_id_tag_update(struct Main *dmain,
                          struct DGraph *dgraph,
                          struct Id *id,
                          int flag);

/** Tag all dependency graphs when time has changed. */
void dgraph_time_tag_update(struct Main *dmain);

/** Tag a dependency graph when time has changed. */
void dgraph_time_tag_update(struct DGraph *dgraph);

/**
 * Mark a particular data-block type as having changing.
 * This does not cause any updates but is used by external
 * render engines to detect if for example a data-block was removed.
 */
void dgraph_id_type_tag(struct DGraph *dgraph, short id_type);
void dgraph_id_type_tag(struct Main *dmain, short id_type);

/**
 * Set a depsgraph to flush updates to editors. This would be done
 * for viewport dgraphs, but not render or export dgraph for example.
 */
void dgraph_enable_editors_update(struct DGraph *dgraph);

/** Check if something was changed in the database and inform editors about this. */
void dgraph_editors_update(struct DGraph *dgraph, bool time);

/** Clear recalc flags after editors or renderers have handled updates. */
void dgraph_ids_clear_recalc(DGraph *dgraph, bool backup);

/**
 * Restore recalc flags, backed up by a previous call to dgraph_ids_clear_recalc.
 * This also clears the backup.
 */
void dgraph_ids_restore_recalc(DGraph *dgraph);

/* ************************************************ */
/* Evaluation Engine API */

/* -------------------------------------------------------------------- */
/** Graph Evaluation **/

/**
 * Frame changed recalculation entry point.
 *
 * The frame-change happened for root scene that graph belongs to.
 */
void dgraph_evaluate_on_framechange(Depsgraph *graph, float frame);

/**
 * Data changed recalculation entry point.
 * Evaluate all nodes tagged for updating.
 */
void dgraph_evaluate_on_refresh(DGraph *graph);

/* -------------------------------------------------------------------- */
/** Editors Integration
 *
 * Mechanism to allow editors to be informed of depsgraph updates,
 * to do their own updates based on changes.
 **/

typedef struct DGraphEditorUpdateCtx {
  struct Main *dmain;
  struct DGraph *dgraph;
  struct Scene *scene;
  struct ViewLayer *view_layer;
} DGraphEditorUpdateCtx;

typedef void (*DGraphEditorUpdateIdCb)(const DGraphEditorUpdateCtx *update_ctx, struct Id *id);
typedef void (*DGraphEditorUpdateSceneCb)(const DGraphEditorUpdateCtx *update_ctx, bool updated);

/** Set callbacks which are being called when dgraph changes. */
void dgraph_editors_set_update_cb(DGraphEditorUpdateIdCb id_fn, DGraphEditorUpdateSceneCb scene_fn);

/* -------------------------------------------------------------------- */
/** Evaluation */

bool dgraph_is_evaluating(const struct DGraph *dgraph);

bool dgraph_is_active(const struct DGraph *dgraph);
void dgraph_make_active(struct DGraph *dgraph);
void dgraph_make_inactive(struct DGraph *dgraph);

/* -------------------------------------------------------------------- */
/** Evaluation Debug **/

void dgraph_debug_print_begin(struct DGraph *dgraph);

void dgraph_debug_print_eval(struct DGraph *dgraph,
                             const char *fn_name,
                             const char *object_name,
                             const void *object_address);

void dgraph_debug_print_eval_subdata(struct DGraph *dgraph,
                                     const char *fn_name,
                                     const char *object_name,
                                     const void *object_address,
                                     const char *subdata_comment,
                                     const char *subdata_name,
                                     const void *subdata_address);

void dgraph_debug_print_eval_subdata_index(struct DGraph *dgraph,
                                           const char *fn_name,
                                           const char *object_name,
                                           const void *object_address,
                                           const char *subdata_comment,
                                           const char *subdata_name,
                                           const void *subdata_address,
                                           int subdata_index);

void dgraph_debug_print_eval_parent_typed(struct DGraph *dgraph,
                                          const char *fn_name,
                                          const char *object_name,
                                          const void *object_address,
                                          const char *parent_comment,
                                          const char *parent_name,
                                          const void *parent_address);

void dgraph_debug_print_eval_time(struct DGraph *dgraph,
                                 const char *fn_name,
                                 const char *object_name,
                                 const void *object_address,
                                 float time);


#ifdef __cplusplus
} /* extern "C" */
#endif
