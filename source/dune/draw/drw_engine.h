
#pragma once

#include "lib_sys_types.h" /* for bool */

#include "types_ob_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ARgn;
struct DrwData;
struct DrwInstanceDataList;
struct Graph;
struct DrwEngineType;
struct Hash;
struct GPUMaterial;
struct GPUOffScreen;
struct GPUViewport;
struct Id;
struct Main;
struct Ob;
struct Rndr;
struct RndrEngine;
struct RndrEngineType;
struct Scene;
struct View3D;
struct ViewLayer;
struct Cx;
struct rcti;

void drw_engines_register(void);
void drw_engines_register_experimental(void);
void drw_engines_free(void);

bool drw_engine_render_support(struct DrwEngineType *drw_engine_type);
void drw_engine_register(struct DrwEngineType *drw_engine_type);

typedef struct DrwUpdateCxt {
  struct Main *main;
  struct Graph *graph;
  struct Scene *scene;
  struct ViewLayer *view_layer;
  struct ARgn *rgn;
  struct View3D *v3d;
  struct RenderEngineType *engine_type;
} DrwUpdateCxt;
void drw_notify_view_update(const DrwUpdateCx *update_cx);

typedef enum eDrwSelStage {
  DRW_SEL_PASS_PRE = 1,
  DRW_SEL_PASS_POST,
} eDrwSelStage;
typedef bool (*DrwSelPassFn)(eDrwSelStage stage, void *user_data);
typedef bool (*DrwObFilterFn)(struct Ob *ob, void *user_data);

/* Everything starts here.
 * This fn takes care of calling all cache and rendering fns
 * for each relevant engine/mode engine. */
void drw_view(const struct Cxt *C);
/* Drw render engine info. **/
void drw_rgn_engine_info(int xoffset, int *yoffset, int line_height);

/* Used for both regular and off-screen drwing.
 * Need to reset DST before calling this fn */
void drw_render_loop_ex(struct Graph *graph,
                         struct RndrEngineType *engine_type,
                         struct ARgn *rgn,
                         struct View3D *v3d,
                         struct GPUViewport *viewport,
                         const struct Cxt *evil_C);
void drw_render_loop(struct Graph *graph,
                      struct ARgn *rgn,
                      struct View3D *v3d,
                      struct GPUViewport *viewport);
/* param viewport: can be NULL, in this case we create one. **/
void drw_render_loop_offscreen(struct Graph *graph,
                                struct RndrEngineType *engine_type,
                                struct ARgn *rgn,
                                struct View3D *v3d,
                                bool is_img_render,
                                bool drw_background,
                                bool do_color_management,
                                struct GPUOffScreen *ofs,
                                struct GPUViewport *viewport);
void drw_render_loop_2d_ex(struct Graph *graph,
                                struct ARgn *rgn,
                                struct GPUViewport *viewport,
                                const struct Context *evil_C);
/* ob mode sel-loop, see: ed_view3d_drw_sel_loop (legacy drawing). **/
void drw_sel_loop(struct Graph *graph,
                  struct ARgn *rgn,
                  struct View3D *v3d,
                  bool use_obedit_skip,
                  bool drw_surface,
                  bool use_nearest,
                  bool do_material_sub_sel,
                  const struct rcti *rect,
                  DrwSelPassFn sel_pass_fn,
                  void *sel_pass_user_data,
                  DrwObFilterFn ob_filter_fn,
                  void *ob_filter_user_data);
/* ob mode sel-loop, see: ed_view3d_drw_depth_loop (legacy drawing). **/
void drw_depth_loop(struct Graph *graph,
                     struct ARgn *rgn,
                     struct View3D *v3d,
                     struct GPUViewport *viewport);
/* Converted from ed_view3d_drw_depth_dpen (legacy drawing). **/
void drw_depth_loop_dpen(struct Graph *graph,
                          struct ARgn *rgn,
                          struct View3D *v3d,
                          struct GPUViewport *viewport);
  
/* Clears the Depth Buf and drws only the specified object. **/
void drw_depth_ob(struct Scene *scene,
                  struct ARgn *rgn,
                  struct View3D *v3d,
                  struct GPUViewport *viewport,
                  struct Ob *ob);
void drw_sel_id(struct Graph *graph,
                struct ARgn *rgn,
                struct View3D *v3d,
                const struct rcti *rect);

/* pen render. */
/* Helper to check if exit ob type to render. **/
bool drw_rndr_check_pen(struct Graph *graph);
void drw_rndr_dpen(struct RndrEngine *engine, struct Graph *graph);

/* This is here bc GPUViewport needs it. */
struct DrwInstanceDataList *drw_instance_data_list_create(void);
void drw_instance_data_list_free(struct DrwInstanceDataList *idatalist);
void drw_uniform_attrs_pool_free(struct GHash *table);

void drw_render_cx_enable(struct Render *rndr);
void drw_render_cx_disable(struct Render *rndr);

void drw_opengl_cx_create(void);
void drw_opengl_cx_destroy(void);
void drw_opengl_cx_enable(void);
void drw_opengl_cx_disable(void);

#ifdef WITH_XR_OPENXR
/* see comment on drw_xr_opengl_cx_get() */
void *drw_xr_opengl_cx_get(void);
void *drw_xr_gpu_cx_get(void);
void drw_xr_drwing_begin(void);
void drw_xr_drwing_end(void);
#endif

/* For garbage collection */
void drw_cache_free_old_batches(struct Main *main);
void drw_cache_free_old_subdiv(void);

/* For the OpenGL evals and garbage collected subdivision data. */
void drw_subdiv_free(void);

/* Never use this. Only for closing dune. */
void drw_opengl_cx_enable_ex(bool restore);
void drw_opengl_cx_disable_ex(bool restore);

void drw_opengl_render_cx_enable(void *re_gl_cx);
void drw_opengl_render_cx_disable(void *re_gl_cx);
/* Needs to be called AFTER drw_opengl_render_cx_enable(). */
void drw_gpu_render_cx_enable(void *re_gpu_cx);
/* Needs to be called BEFORE draw_opengl_render_cx_disable(). */
void drw_gpu_render_cx_disable(void *re_gpu_cx);

void drw_deferred_shader_remove(struct GPUMaterial *mat);

/* Get DrwData from the given id-block. In order for this to work, we assume that
 * the DrwData ptr is stored in the struct in the same fashion as in IdDdtTemplate. */
struct DrwDataList *drw_drwdatalist_from_id(struct Id *id);
void drw_drwdata_free(struct Id *id);

struct DrwData *drw_viewport_data_create(void);
void drw_viewport_data_free(struct DrwData *drw_data);

bool drw_opengl_cxt_release(void);
void drw_opengl_cxt_activate(bool drw_state);

/* We may want to move this into a more general location.
 * This doesn't require the draw cxt to be in use. */
void drw_cursor_2d_ex(const struct ARgn *rgn, const float cursor[2]);

#ifdef __cplusplus
}
#endif
