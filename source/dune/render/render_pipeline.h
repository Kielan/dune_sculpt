#pragma once

#include "graph.h"
#include "types_listBase.h"
#include "types_vec_types.h"

struct ImBuf;
struct Image;
struct ImageFormatData;
struct Main;
struct Object;
struct RenderData;
struct RenderResult;
struct ReportList;
struct Scene;
struct StampData;
struct ViewLayer;
struct MovieHandle;

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/* this include is what is exposed of render to outside world */
/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* length of the scene name + passname */
#define RE_MAXNAME ((MAX_ID_NAME - 2) + 10)

/* only used as handle */
typedef struct Render Render;

/* Render Result usage:
 *
 * - render engine allocates/frees and delivers raw floating point rects
 * - right now it's full rects, but might become tiles or file
 * - the display client has to allocate display rects, sort out what to display,
 *   and how it's converted
 */

typedef struct RenderView {
  struct RenderView *next, *prev;
  char name[64]; /* EXR_VIEW_MAXNAME */

  /* if this exists, result of composited layers */
  float *rectf;
  /* if this exists, result of composited layers */
  float *rectz;
  /* optional, 32 bits version of picture, used for sequencer, ogl render and image curves */
  int *rect32;

} RenderView;

typedef struct RenderPass {
  struct RenderPass *next, *prev;
  int channels;
  char name[64];   /* amount defined in IMB_openexr.h */
  char chan_id[8]; /* amount defined in IMB_openexr.h */
  float *rect;
  int rectx, recty;

  char fullname[64]; /* EXR_PASS_MAXNAME */
  char view[64];     /* EXR_VIEW_MAXNAME */
  int view_id;       /* quick lookup */

  int pad;
} RenderPass;

/* a renderlayer is a full image, but with all passes and samples */
/* size of the rects is defined in RenderResult */
/* after render, the Combined pass is in combined,
 * for renderlayers read from files it is a real pass */
typedef struct RenderLayer {
  struct RenderLayer *next, *prev;

  /** copy of RenderData */
  char name[RE_MAXNAME];
  int layflag, passflag, pass_xor;

  int rectx, recty;

  /** Optional saved endresult on disk. */
  void *exrhandle;

  ListBase passes;

} RenderLayer;

typedef struct RenderResult {
  struct RenderResult *next, *prev;

  /* target image size */
  int rectx, recty;
  short sample_nr;

  /* The following rect32, rectf and rectz buffers are for temporary storage only,
   * for RenderResult structs created in #RE_AcquireResultImage - which do not have RenderView */

  /* optional, 32 bits version of picture, used for ogl render and image curves */
  int *rect32;
  /* if this exists, a copy of one of layers, or result of composited layers */
  float *rectf;
  /* if this exists, a copy of one of layers, or result of composited layers */
  float *rectz;

  /* coordinates within final image (after cropping) */
  rcti tilerect;
  /* offset to apply to get a border render in full image */
  int xof, yof;

  /* the main buffers */
  ListBase layers;

  /* multiView maps to a StringVector in OpenEXR */
  ListBase views; /* RenderView */

  /* allowing live updates: */
  rcti renrect;
  RenderLayer *renlay;

  /* for render results in Image, verify validity for sequences */
  int framenr;

  /* for acquire image, to indicate if it there is a combined layer */
  int have_combined;

  /* render info text */
  char *text;
  char *error;

  struct StampData *stamp_data;

  bool passes_allocated;
} RenderResult;

typedef struct RenderStats {
  int cfra;
  bool localview;
  double starttime, lastframetime;
  const char *infostr, *statstr;
  char scene_name[MAX_ID_NAME - 2];
  float mem_used, mem_peak;
} RenderStats;

/* *********************** API ******************** */

/**
 * The name is used as identifier, so elsewhere in blender the result can retrieved.
 * Calling a new render with same name, frees automatic existing render.
 */
struct Render *render_NewRender(const char *name);
struct Render *render_GetRender(const char *name);

struct Scene;
struct Render *render_NewSceneRender(const struct Scene *scene);
struct Render *render_GetSceneRender(const struct Scene *scene);

/* Assign default dummy callbacks. */

/**
 * Called for new renders and when finishing rendering
 * so we always have valid callbacks on a render.
 */
void render_InitRenderCB(struct Render *re);

/**
 * Use free render as signal to do everything over (previews).
 *
 * Only call this while you know it will remove the link too.
 */
void render_FreeRender(struct Render *re);
/** Only called on exit. */
void render_FreeAllRender(void);

/**
 * On file load, free render results.
 */
void render_FreeAllRenderResults(void);
/**
 * On file load or changes engines, free persistent render data.
 * Assumes no engines are currently rendering.
 */
void render_FreeAllPersistentData(void);
/**
 * Free persistent render data, optionally only for the given scene.
 */
void render_FreePersistentData(const struct Scene *scene);

/**
 * Get results and statistics.
 */
void RE_FreeRenderResult(struct RenderResult *rr);
/**
 * If you want to know exactly what has been done.
 */
struct RenderResult *RE_AcquireResultRead(struct Render *re);
struct RenderResult *RE_AcquireResultWrite(struct Render *re);
void RE_ReleaseResult(struct Render *re);
/**
 * Same as #RE_AcquireResultImage but creating the necessary views to store the result
 * fill provided result struct with a copy of thew views of what is done so far the
 * #RenderResult.views #ListBase needs to be freed after with #RE_ReleaseResultImageViews
 */
void RE_AcquireResultImageViews(struct Render *re, struct RenderResult *rr);
/**
 * Clear temporary #RenderResult struct.
 */
void RE_ReleaseResultImageViews(struct Render *re, struct RenderResult *rr);

/**
 * Fill provided result struct with what's currently active or done.
 * This #RenderResult struct is the only exception to the rule of a #RenderResult
 * always having at least one #RenderView.
 */
void RE_AcquireResultImage(struct Render *re, struct RenderResult *rr, int view_id);
void RE_ReleaseResultImage(struct Render *re);
void RE_SwapResult(struct Render *re, struct RenderResult **rr);
void RE_ClearResult(struct Render *re);
struct RenderStats *RE_GetStats(struct Render *re);

/**
 * Caller is responsible for allocating `rect` in correct size!
 */
void RE_ResultGet32(struct Render *re, unsigned int *rect);
/**
 * Only for acquired results, for lock.
 *
 * \note The caller is responsible for allocating `rect` in correct size!
 */
void RE_AcquiredResultGet32(struct Render *re,
                            struct RenderResult *result,
                            unsigned int *rect,
                            int view_id);

void RE_render_result_full_channel_name(char *fullname,
                                        const char *layname,
                                        const char *passname,
                                        const char *viewname,
                                        const char *chan_id,
                                        const int channel);

struct ImBuf *render_result_rect_to_ibuf(struct RenderResult *rr,
                                         const struct ImageFormatData *imf,
                                         const float dither,
                                         const int view_id);
void render_result_rect_from_ibuf(struct RenderResult *rr,
                                  const struct ImBuf *ibuf,
                                  const int view_id);

struct RenderLayer *render_GetRenderLayer(struct RenderResult *rr, const char *name);
float *render_RenderLayerGetPass(struct RenderLayer *rl, const char *name, const char *viewname);

bool render_HasSingleLayer(struct Render *re);

/**
 * Add passes for grease pencil.
 * Create a render-layer and render-pass for pen layer.
 */
struct RenderPass *render_create_gp_pass(struct RenderResult *rr,
                                     const char *layername,
                                     const char *viewname);

void render_create_render_pass(struct RenderResult *rr,
                           const char *name,
                           int channels,
                           const char *chan_id,
                           const char *layername,
                           const char *viewname,
                           bool allocate);

/**
 * Obligatory initialize call, doesn't change during entire render sequence.
 * param disprect: is optional. if NULL it assumes full window render.
 */
void render_InitState(struct Render *re,
                  struct Render *source,
                  struct RenderData *rd,
                  struct ListBase *render_layers,
                  struct ViewLayer *single_layer,
                  int winx,
                  int winy,
                  rcti *disprect);

/** Set up the view-plane/perspective matrix, three choices.
 ** return camera override if set. */
struct Object *render_GetCamera(struct Render *re);
void render_SetOverrideCamera(struct Render *re, struct Object *cam_ob);
/**
 * Per render, there's one persistent view-plane. Parts will set their own view-planes.
 *
 * note call this after render_InitState().
 */
void render_SetCamera(struct Render *re, const struct Object *cam_ob);

/** Get current view and window transform. */
void render_GetViewPlane(struct Render *re, rctf *r_viewplane, rcti *r_disprect);

/**
 * Set the render threads based on the command-line and auto-threads setting. */
void render_init_threadcount(Render *re);

bool render_WriteRenderViewsMovie(struct ReportList *reports,
                              struct RenderResult *rr,
                              struct Scene *scene,
                              struct RenderData *rd,
                              struct MovieHandle *mh,
                              void **movie_ctx_arr,
                              int totvideos,
                              bool preview);

/**
 * General Dune frame render call.
 *
 * note Only render_NewRender() needed, main Blender render calls.
 *
 * param write_still: Saves frames to disk (typically disabled). Useful for batch-operations
 * (rendering from Python for e.g.) when an additional save action for is inconvenient.
 * This is the default behavior for render_RenderAnim.
 */
void render_RenderFrame(struct Render *re,
                        struct Main *main,
                        struct Scene *scene,
                        struct ViewLayer *single_layer,
                        struct Object *camera_override,
                        const int frame,
                        const float subframe,
                        bool write_still);
/** A version of render_RenderFrame that saves images to disk. */
void render_RenderAnim(struct Render *re,
                       struct Main *main,
                       struct Scene *         
                       struct ViewLayer *single_layer,
                       struct Object *camera_override,
                       int sfra,
                       int efra,
                       int tfra);
#ifdef WITH_FREESTYLE
void render_RenderFreestyleStrokes(struct Render *re,
                               struct Main *main,
                               struct Scene *scene,
                               int render);
void render_RenderFreestyleExternal(struct Render *re);
#endif

void render_SetActiveRenderView(struct Render *re, const char *viewname);
const char *render_GetActiveRenderView(struct Render *re);

/** Error reporting.  **/
void render_SetReports(struct Render *re, struct ReportList *reports);

/** Main preview render call. */
void render_PreviewRender(struct Render *re, struct Main *bmain, struct Scene *scene);

/** Only the temp file! */
bool render_ReadRenderResult(struct Scene *scene, struct Scene *scenode);

struct RenderResult *render_MultilayerConvert(
    void *exrhandle, const char *colorspace, bool predivide, int rectx, int recty);

/* Display and event callbacks. */

/** Image and movie output has to move to either imbuf or kernel. */
void render_display_init_cb(struct Render *re,
                        void *handle,
                        void (*f)(void *handle, RenderResult *rr));
void render_display_clear_cb(struct Render *re,
                         void *handle,
                         void (*f)(void *handle, RenderResult *rr));
void render_display_update_cb(struct Render *re,
                          void *handle,
                          void (*f)(void *handle, RenderResult *rr, struct rcti *rect));
void render_stats_draw_cb(struct Render *re, void *handle, void (*f)(void *handle, RenderStats *rs));
void render_progress_cb(struct Render *re, void *handle, void (*f)(void *handle, float));
void render_draw_lock_cb(struct Render *re, void *handle, void (*f)(void *handle, bool lock));
void render_test_break_cb(struct Render *re, void *handle, int (*f)(void *handle));
void render_current_scene_update_cb(struct Render *re,
                                void *handle,
                                void (*f)(void *handle, struct Scene *scene));

void render_gl_ctx_create(Render *re);
void render_gl_ctx_destroy(Render *re);
void *render_gl_ctx_get(Render *re);
void *render_gpu_ctx_get(Render *re);

/**
 * param x: ranges from -1 to 1.
 *
 * TODO: Should move to kernel once... still unsure on how/where.
 */
float render_filter_value(int type, float x);

int render_seq_render_active(struct Scene *scene, struct RenderData *rd);

/** Used in the interface to decide whether to show layers or passes. */
bool render_layers_have_name(struct RenderResult *result);
bool render_passes_have_name(struct RenderLayer *rl);

struct RenderPass *render_pass_find_by_name(struct RenderLayer *rl,
                                            const char *name,
                                            const char *viewname);
/** Only provided for API compatibility, don't use this in new code! */
struct RenderPass *render_pass_find_by_type(struct RenderLayer *rl,
                                        int passtype,
                                        const char *viewname);

/* shaded view or baking options */
#define RENDER_BAKE_NORMALS 0
#define RENDER_BAKE_DISPLACEMENT 1
#define RENDER_BAKE_AO 2

void render_GetCameraWindow(struct Render *re, const struct Object *camera, float mat[4][4]);
/**
 * Must be called after render_GetCameraWindow(), does not change `re->winmat`.
 */
void render_GetCameraWindowWithOverscan(const struct Render *re, float overscan, float r_winmat[4][4]);
void render_GetCameraModelMatrix(const struct Render *re,
                                 const struct Object *camera,
                                 float r_modelmat[4][4]);

struct Scene *render_GetScene(struct Render *re);
void render_SetScene(struct Render *re, struct Scene *sce);

bool render_is_rendering_allowed(struct Scene *scene,
                             struct ViewLayer *single_layer,
                             struct Object *camera_override,
                             struct ReportList *reports);

bool render_allow_render_generic_object(struct Object *ob);

/******* defined in render_result.c *********/

bool render_HasCombinedLayer(const RenderResult *res);
bool render_HasFloatPixels(const RenderResult *res);
bool render_RenderResult_is_stereo(const RenderResult *res);
struct RenderView *render_RenderViewGetById(struct RenderResult *rr, int view_id);
struct RenderView *render_RenderViewGetByName(struct RenderResult *rr, const char *viewname);

RenderResult *render_DuplicateRenderResult(RenderResult *rr);

#ifdef __cplusplus
}
#endif
