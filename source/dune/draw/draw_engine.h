
#pragma once

#include "lib_sys_types.h" /* for bool */

#include "types_object_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ARegion;
struct DrawData;
struct DrawInstanceDataList;
struct Graph;
struct DrawEngineType;
struct Hash;
struct GPUMaterial;
struct GPUOffScreen;
struct GPUViewport;
struct Id;
struct Main;
struct Object;
struct Render;
struct RenderEngine;
struct RenderEngineType;
struct Scene;
struct View3D;
struct ViewLayer;
struct Ctx;
struct rcti;

void draw_engines_register(void);
void draw_engines_register_experimental(void);
void draw_engines_free(void);

bool draw_engine_render_support(struct DrawEngineType *draw_engine_type);
void draw_engine_register(struct DrawEngineType *draw_engine_type);

typedef struct DrawUpdateCtx {
  struct Main *dmain;
  struct Graph *graph;
  struct Scene *scene;
  struct ViewLayer *view_layer;
  struct ARegion *region;
  struct View3D *v3d;
  struct RenderEngineType *engine_type;
} DrawUpdateCtx;
void draw_notify_view_update(const DrawUpdateCtx *update_ctx);

typedef enum eDrawSelectStage {
  DRAW_SELECT_PASS_PRE = 1,
  DRAW_SELECT_PASS_POST,
} eDrawSelectStage;
typedef bool (*DrawSelectPassFn)(eDrawSelectStage stage, void *user_data);
typedef bool (*DrawObjectFilterFn)(struct Object *ob, void *user_data);

/**
 * Everything starts here.
 * This function takes care of calling all cache and rendering functions
 * for each relevant engine / mode engine.
 */
void draw_view(const struct Context *C);
/** Draw render engine info. **/
void draw_region_engine_info(int xoffset, int *yoffset, int line_height);

/**
 * Used for both regular and off-screen drawing.
 * Need to reset DST before calling this function
 */
void draw_render_loop_ex(struct Graph *graph,
                         struct RenderEngineType *engine_type,
                         struct ARegion *region,
                         struct View3D *v3d,
                         struct GPUViewport *viewport,
                         const struct Context *evil_C);
void draw_render_loop(struct Graph *graph,
                      struct ARegion *region,
                      struct View3D *v3d,
                      struct GPUViewport *viewport);
/** param viewport: can be NULL, in this case we create one. **/
void draw_render_loop_offscreen(struct Graph *graph,
                                struct RenderEngineType *engine_type,
                                struct ARegion *region,
                                struct View3D *v3d,
                                bool is_image_render,
                                bool draw_background,
                                bool do_color_management,
                                struct GPUOffScreen *ofs,
                                struct GPUViewport *viewport);
void draw_render_loop_2d_ex(struct Graph *graph,
                                struct ARegion *region,
                                struct GPUViewport *viewport,
                                const struct Context *evil_C);
/** object mode select-loop, see: ed_view3d_draw_select_loop (legacy drawing). **/
void draw_select_loop(struct Graph *graph,
                      struct ARegion *region,
                      struct View3D *v3d,
                      bool use_obedit_skip,
                      bool draw_surface,
                      bool use_nearest,
                      bool do_material_sub_selection,
                      const struct rcti *rect,
                      DrawSelectPassFn select_pass_fn,
                      void *select_pass_user_data,
                      DrawObjectFilterFn object_filter_fn,
                      void *object_filter_user_data);
/** object mode select-loop, see: ed_view3d_draw_depth_loop (legacy drawing). **/
void draw_depth_loop(struct Graph *graph,
                     struct ARegion *region,
                     struct View3D *v3d,
                     struct GPUViewport *viewport);
/** Converted from ed_view3d_draw_depth_dpen (legacy drawing). **/
void draw_depth_loop_dpen(struct Graph *graph,
                          struct ARegion *region,
                          struct View3D *v3d,
                          struct GPUViewport *viewport);
  
/** Clears the Depth Buffer and draws only the specified object. **/
void draw_depth_object(struct Scene *scene,
                       struct ARegion *region,
                       struct View3D *v3d,
                       struct GPUViewport *viewport,
                       struct Object *object);
void draw_select_id(struct Graph *graph,
                    struct ARegion *region,
                    struct View3D *v3d,
                    const struct rcti *rect);

/* Grease pencil render. */

/** Helper to check if exit object type to render. **/
bool draw_render_check_dpen(struct Graph *graph);
void draw_render_dpen(struct RenderEngine *engine, struct Graph *graph);

/**
 * This is here because GPUViewport needs it.
 */
struct DrawInstanceDataList *draw_instance_data_list_create(void);
void draw_instance_data_list_free(struct DrawInstanceDataList *idatalist);
void draw_uniform_attrs_pool_free(struct GHash *table);

void draw_render_ctx_enable(struct Render *render);
void draw_render_ctx_disable(struct Render *render);

void draw_opengl_ctx_create(void);
void draw_opengl_ctx_destroy(void);
void draw_opengl_ctx_enable(void);
void draw_opengl_ctx_disable(void);

#ifdef WITH_XR_OPENXR
/* XXX see comment on draw_xr_opengl_ctx_get() */
void *draw_xr_opengl_ctx_get(void);
void *draw_xr_gpu_ctx_get(void);
void draw_xr_drawing_begin(void);
void draw_xr_drawing_end(void);
#endif

/* For garbage collection */
void draw_cache_free_old_batches(struct Main *bmain);
void draw_cache_free_old_subdiv(void);

/* For the OpenGL evaluators and garbage collected subdivision data. */
void draw_subdiv_free(void);

/* Never use this. Only for closing blender. */
void draw_opengl_ctx_enable_ex(bool restore);
void draw_opengl_ctx_disable_ex(bool restore);

void draw_opengl_render_ctx_enable(void *re_gl_ctx);
void draw_opengl_render_ctx_disable(void *re_gl_ctx);
/**
 * Needs to be called AFTER draw_opengl_render_ctx_enable().
 */
void draw_gpu_render_ctx_enable(void *re_gpu_ctx);
/**
 * Needs to be called BEFORE draw_opengl_render_ctx_disable().
 */
void draw_gpu_render_ctx_disable(void *re_gpu_ctx);

void draw_deferred_shader_remove(struct GPUMaterial *mat);

/**
 * Get DrawData from the given id-block. In order for this to work, we assume that
 * the DrawData pointer is stored in the struct in the same fashion as in IdDdtTemplate.
 */
struct DrawDataList *draw_drawdatalist_from_id(struct ID *id);
void draw_drawdata_free(struct Id *id);

struct DrawData *draw_viewport_data_create(void);
void draw_viewport_data_free(struct DrawData *draw_data);

bool draw_opengl_ctx_release(void);
void draw_opengl_ctx_activate(bool draw_state);

/**
 * We may want to move this into a more general location.
 * This doesn't require the draw context to be in use.
 */
void draw_cursor_2d_ex(const struct ARegion *region, const float cursor[2]);

#ifdef __cplusplus
}
#endif
