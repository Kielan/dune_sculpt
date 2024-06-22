#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Graph;
struct Id;
struct Main;
struct ApiPtr;

/* Callbacks for One Off Actions
 *
 * - `{ACTION}` use in cases where only a single cb is required,
 *   `VERSION_UPDATE` and `RNDR_STATS` for example.
 *
 * avoid single cbs if there is a chance `PRE/POST` are useful to differentiate
 * since renaming cbs may break Python scripts.
 *
 * Cbs for Common Actions
 *
 * - `{ACTION}_PRE` run before the action.
 * - `{ACTION}_POST` run after the action.
 *
 * Optional Additional Cbs
 *
 * - `{ACTION}_INIT` when the handler may manipulate the cxt used to run the action.
 *
 *   Examples where `INIT` fns may be useful are:
 *
 *   - When rendering, an `INIT` fn may change the camera or rndr settings,
 *     things which a `PRE` fn can't support as this info has alrdy been used.
 *   - When saving an `INIT` fn could temporarily change the preferences.
 *
 * - `{ACTION}_POST_FAIL` should be included if the action may fail.
 *
 *   Use this so a call to the `PRE` cb always has a matching call to `POST` or `POST_FAIL`.
 *
 * in most cases only `PRE/POST` are required.
 *
 * Callbacks for Background/Modal Tasks
 *
 * - `{ACTION}_INIT`
 * - `{ACTION}_COMPLETE` when a background job has finished.
 * - `{ACTION}_CANCEL` When a background job is canceled partway through.
 *
 *   While cancellation may be caused by any number of reasons, common causes may include:
 *
 *   - Explicit user cancellation.
 *   - Exiting Dune.
 *   - Failure to acquire resources (such as disk-full, out of memory ... etc).
 *
 * `PRE/POST` handlers may be used along side modal task handlers
 * as is the case for rndring, where rndring an anim uses modal task handlers,
 * rndring a single frame has `PRE/POST` handlers.
 *
 * Python Access
 * =============
 *
 * All cbs here must be exposed via the Python module `bpy.app.handlers`,
 * see `bpy_app_handlers.c`. */
typedef enum {
  DUNE_CB_EV_FRAME_CHANGE_PRE,
  DUNE_CB_EV_FRAME_CHANGE_POST,
  DUNE_CB_EV_RNDR_PRE,
  DUNE_CB_EV_RNDR_POST,
  DUNE_CB_EV_RNDR_WRITE,
  DUNE_CB_EV_RNDR_STATS,
  DUNE_CB_EV_RNDR_INIT,
  DUNE_CB_EV_RNDR_COMPLETE,
  DUNE_CB_EV_RNDR_CANCEL,
  DUNE_CB_EV_LOAD_PRE,
  DUNE_CB_EV_LOAD_POST,
  DUNE_CB_EV_SAVE_PRE,
  DUNE_CB_EV_SAVE_POST,
  DUNE_CB_EV_UNDO_PRE,
  DUNE_CB_EV_UNDO_POST,
  DUNE_CB_EV_REDO_PRE,
  DUNE_CB_EV_REDO_POST,
  DUNE_CB_EV_GRAPH_UPDATE_PRE,
  DUNE_CB_EV_GRAPH_UPDATE_POST,
  DUNE_CB_EV_VERSION_UPDATE,
  DUNE_CB_EV_LOAD_FACTORY_USERDEF_POST,
  DUNE_CB_EV_LOAD_FACTORY_STARTUP_POST,
  DUNE_CB_EV_XR_SESS_START_PRE,
  DUNE_CB_EV_ANNOTATION_PRE,
  DUNE_CB_EV_ANNOTATION_POST,
  DUNE_CB_EV_TOT,
} eCbEv;

typedef struct CbFnStore {
  struct CbFnStore *next, *prev;
  void (*fn)(struct Main *, struct ApiPtr **, int num_ptrs, void *arg);
  void *arg;
  short alloc;
} CbFnStore;

void dune_cb_ex(struct Main *main,
                struct ApiPtr **ptrs,
                int num_ptrs,
                eCbEv evt);
void dune_cb_ex_null(struct Main *main, eCbEv evt);
void dune_cb_ex_id(struct Main *main, struct Id *id, eCbEv ev);
void dune_cb_ex_id_graph(struct Main *main,
                         struct Id *id,
                         struct Graph *graph,
                         eCbEvent evt);
void dune_cb_add(CbFnStore *fnstore, eCbEv ev);
void dune_cb_remove(CbFnStore *fnstore, eCbEv ev);

void dune_cb_global_init(void);
/* Call on application exit. */
void dune_cb_global_finalize(void);

#ifdef __cplusplus
}
