#pragma once

#include "types_list.h"
#include "types_node.h"
#include "types_scene.h"
#include "rndr_bake.h"
#include "api_types.h"

#include "lib_threads.h"

struct BakePixel;
struct Graph;
struct Main;
struct Ob;
struct Rndr;
struct RndrData;
struct RndrEngine;
struct RndrEngineType;
struct RndrLayer;
struct RndrPass;
struct RndrResult;
struct ReportList;
struct Scene;
struct ViewLayer;
struct Node;
struct NodeTree;

#ifdef __cplusplus
extern "C" {
#endif

/* External Engine */
/* RndrEngineType.flag */
#define RNDR_INTERNAL 1
/* #define RNDR_FLAG_DEPRECATED   2 */
#define RNDR_USE_PREVIEW 4
#define RNDR_USE_POSTPROCESS 8
#define RNDR_USE_EEVEE_VIEWPORT 16
/* #define RENDER_USE_SAVE_BUFFERS_DEPRECATED 32 */
#define RNDR_USE_SHADING_NODES_CUSTOM 64
#define RNDR_USE_SPHERICAL_STEREO 128
#define RNDR_USE_STEREO_VIEWPORT 256
#define RNDR_USE_GPU_CXT 512
#define RNDR_USE_CUSTOM_FREESTYLE 1024
#define RNDR_USE_NO_IMG_SAVE 2048
#define RNDR_USE_ALEMBIC_PROCEDURAL 4096

/* RenderEngine.flag */
#define RNDR_ENGINE_ANIM 1
#define RNDR_ENGINE_PREVIEW 2
#define RNDR_ENGINE_DO_DRW 4
#define RNDR_ENGINE_DO_UPDATE 8
#define RNDR_ENGINE_RENDERING 16
#define RNDR_ENGINE_HIGHLIGHT_TILES 32
#define RNDR_ENGINE_CAN_DRAW 64

extern List R_engines;

typedef struct RndrEngineType {
  struct RndrEngineType *next, *prev;

  /* type info */
  char idname[64]; /* best keep the same size as DUNE_ST_MAXNAME. */
  char name[64];
  int flag;

  void (*update)(struct RndrEngine *engine, struct Main *main, struct Graph *graph);

  void (*render)(struct RndrEngine *engine, struct Graph *graph);

  /* Offline rndring is finished - no more view layers will be rendered.
   * All the pending data is to be communicated from the engine back to Dune. In a possibly
   * most mem-efficient manner (engine might free its database before making Dune to allocate
   * full-frame render result). */
  void (*render_frame_finish)(struct RenderEngine *engine);

  void (*drw)(struct RndrEngine *engine,
               const struct Cxt *cxt,
               struct Graph *graph);

  void (*bake)(struct RndrEngine *engine,
               struct Graph *graph,
               struct Ob *ob,
               int pass_type,
               int pass_filter,
               int width,
               int height);

  void (*view_update)(struct RndrEngine *engine,
                      const struct Cxt *cxt,
                      struct Graph *graph);
  void (*view_draw)(struct RndrEngine *engine,
                    const struct Cxt *cxt,
                    struct Graph *graph);

  void (*update_script_node)(struct RndrEngine *engine,
                             truct NodeTree *ntree,
                             struct Node *node);
  void (*update_rndr_passes)(struct RndrEngine *engine,
                             struct Scene *scene,
                             struct ViewLayer *view_layer);

  struct DrwEngineType *drw_engine;

  /* Api integration */
  ExtensionApi api_ext;
} RndrEngineType;

typedef void (*update_rndr_passes_cb_t)(void *userdata,
                                        struct Scene *scene,
                                        struct ViewLayer *view_layer,
                                        const char *name,
                                        int channels,
                                        const char *chanid,
                                        eNodeSocketDatatype type);

typedef struct RndrEngine {
  RndrEngineType *type;
  void *py_instance;

  int flag;
  struct Ob *camera_override;
  unsigned int layer_override;

  struct Rndr *re;
  List fullresult;
  char txt[512]; /* IMA_MAX_RENDER_TEXT */

  int resolution_x, resolution_y;

  struct ReportList *reports;

  struct {
    const struct BakePixel *pixels;
    float *result;
    int width, height, depth;
    int ob_id;
  } bake;

  /* Graph */
  struct Graph *graph;
  bool has_pen;

  /* cb for render pass query */
  ThreadMutex update_render_passes_mutex;
  update_render_passes_cb_t update_render_passes_cb;
  void *update_render_passes_data;

  rctf last_viewplane;
  rcti last_disprect;
  float last_viewmat[4][4];
  int last_winx, last_winy;
} RndrEngine;

RndrEngine *rndr_engine_create(RndrEngineType *type);
void rndr_engine_free(RndrEngine *engine);

/* Loads in img into a result, size must match
 * x/y offsets are only used on a partial copy when dimensions don't match. */
void render_layer_load_from_file(
    struct RenderLayer *layer, struct ReportList *reports, const char *filename, int x, int y);
void renderm_result_load_from_file(struct RndrResult *result,
                                   struct ReportList *reports,
                                   const char *filename);

struct RndrResult *rndr_engine_begin_result(
    RndrEngine *engine, int x, int y, int w, int h, const char *layername, const char *viewname);
void rndr_engine_update_result(RndrEngine *engine, struct RenderResult *result);
void rndr_engine_add_pass(RndrEngine *engine,
                            const char *name,
                            int channels,
                            const char *chan_id,
                            const char *layername);
void rndr_engine_end_result(RndrEngine *engine,
                              struct RndrResult *result,
                              bool cancel,
                              bool highlight,
                              bool merge_results);
struct RenderResult *render_engine_get_result(struct RenderEngine *engine);

struct RenderPass *render_engine_pass_by_index_get(struct RenderEngine *engine,
                                               const char *layer_name,
                                               int index);

const char *render_engine_active_view_get(RenderEngine *engine);
void render_engine_active_view_set(RenderEngine *engine, const char *viewname);
float render_engine_get_camera_shift_x(RenderEngine *engine,
                                       struct Object *camera,
                                       bool use_spherical_stereo);
void render_engine_get_camera_model_matrix(RenderEngine *engine,
                                           struct Object *camera,
                                           bool use_spherical_stereo,
                                           float r_modelmat[16]);
bool render_engine_get_spherical_stereo(RenderEngine *engine, struct Object *camera);

bool render_engine_test_break(RenderEngine *engine);
void render_engine_update_stats(RenderEngine *engine, const char *stats, const char *info);
void render_engine_update_progress(RenderEngine *engine, float progress);
void render_engine_update_memory_stats(RenderEngine *engine, float mem_used, float mem_peak);
void render_engine_report(RenderEngine *engine, int type, const char *msg);
void RE_engine_set_error_message(RenderEngine *engine, const char *msg);

bool render_engine_render(struct Render *re, bool do_all);

bool render_engine_is_external(const struct Render *re);

void render_engine_frame_set(struct RenderEngine *engine, int frame, float subframe);

void render_engine_update_render_passes(struct RenderEngine *engine,
                                        struct Scene *scene,
                                        struct ViewLayer *view_layer,
                                        update_render_passes_cb_t callback,
                                        void *cb_data);
void render_engine_register_pass(struct RenderEngine *engine,
                                 struct Scene *scene,
                                 struct ViewLayer *view_layer,
                                 const char *name,
                                 int channels,
                                 const char *chanid,
                                 eNodeSocketDatatype type);

bool render_engine_use_persistent_data(struct RenderEngine *engine);

struct RenderEngine *render_engine_get(const struct Render *re);

/* Acquire render engine for drawing via its `draw()` callback.
 *
 * If drawing is not possible false is returned. If drawing is possible then the engine is
 * "acquired" so that it can not be freed by the render pipeline.
 *
 * Drawing is possible if the engine has the `draw()` callback and it is in its `render()`
 * callback. */
bool render_engine_draw_acquire(struct Render *re);
void render_engine_draw_release(struct Render *re);

/* NOTE: Only used for Cycles's DuneGPUDisplay integration with the draw manager. A subject
 * for re-consideration. Do not use this functionality. */
bool render_engine_has_render_ctx(struct RenderEngine *engine);
void render_engine_render_ctx_enable(struct RenderEngine *engine);
void render_engine_render_ctx_disable(struct RenderEngine *engine);

/* Engine Types */
void render_engines_init(void);
void render_engines_init_experimental(void);
void render_engines_exit(void);
void render_engines_register(RenderEngineType *render_type);

bool render_engine_is_opengl(RenderEngineType *render_type);

/**
 * Return true if the RenderEngineType has native support for direct loading of Alembic data. For
 * Cycles, this also checks that the experimental feature set is enabled.
 */
bool render_engine_supports_alembic_procedural(const RenderEngineType *render_type, Scene *scene);

RenderEngineType *render_engines_find(const char *idname);

rcti *render_engine_get_current_tiles(struct Render *re, int *r_total_tiles, bool *r_needs_free);
struct RenderData *render_engine_get_render_data(struct Render *re);
void render_bake_engine_set_engine_params(struct Render *re,
                                          struct Main *main,
                                          struct Scene *scene);

void render_engine_free_dune_memory(struct RenderEngine *engine);

void render_engine_tile_highlight_set(
    struct RenderEngine *engine, int x, int y, int width, int height, bool highlight);
void render_engine_tile_highlight_clear_all(struct RenderEngine *engine);

#ifdef __cplusplus
}
#endif
