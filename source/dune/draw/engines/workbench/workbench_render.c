/** Render functions for final render output. */

#include "lib_rect.h"

#include "types_node.h"

#include "dune_report.h"

#include "draw_render.h"

#include "ed_view3d.h"

#include "gpu_shader.h"

#include "dgraph.h"
#include "dgraph_query.h"

#include "render_pipeline.h"

#include "workbench_private.h"

static void workbench_render_cache(void *vedata,
                                   struct Object *ob,
                                   struct RenderEngine *UNUSED(engine),
                                   struct Depsgraph *UNUSED(dgraph))
{
  workbench_cache_populate(vedata, ob);
}

static void workbench_render_matrices_init(RenderEngine *engine, DGraph *dgraph)
{
  /* TODO: Shall render hold pointer to an evaluated camera instead? */
  struct Object *ob_camera_eval = dgraph_get_evaluated_object(dgraph, render_GetCamera(engine->re));

  /* Set the perspective, view and window matrix. */
  float winmat[4][4], viewmat[4][4], viewinv[4][4];

  render_GetCameraWindow(engine->re, ob_camera_eval, winmat);
  render_GetCameraModelMatrix(engine->re, ob_camera_eval, viewinv);

  invert_m4_m4(viewmat, viewinv);

  rawView *view = draw_view_create(viewmat, winmat, NULL, NULL, NULL);
  draw_view_default_set(view);
  draw_view_set_active(view);
}

static bool workbench_render_framebuffers_init(void)
{
  /* For image render, allocate own buffers because we don't have a viewport. */
  const float *viewport_size = draw_viewport_size_get();
  const int size[2] = {(int)viewport_size[0], (int)viewport_size[1]};

  DefaultTextureList *dtxl = draw_viewport_texture_list_get();

  /* When doing a multi view rendering the first view will allocate the buffers
   * the other views will reuse these buffers */
  if (dtxl->color == NULL) {
    lib_assert(dtxl->depth == NULL);
    dtxl->color = gpu_texture_create_2d("txl.color", UNPACK2(size), 1, GPU_RGBA16F, NULL);
    dtxl->depth = gpu_texture_create_2d("txl.depth", UNPACK2(size), 1, GPU_DEPTH24_STENCIL8, NULL);
  }

  if (!(dtxl->depth && dtxl->color)) {
    return false;
  }

  DefaultFramebufferList *dfbl = draw_viewport_framebuffer_list_get();

  gou_framebuffer_ensure_config(
      &dfbl->default_fb,
      {GPU_ATTACHMENT_TEXTURE(dtxl->depth), GPU_ATTACHMENT_TEXTURE(dtxl->color)});

  gpu_framebuffer_ensure_config(&dfbl->depth_only_fb,
                                {GPU_ATTACHMENT_TEXTURE(dtxl->depth), GPU_ATTACHMENT_NONE});

  gpu_framebuffer_ensure_config(&dfbl->color_only_fb,
                                {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(dtxl->color)});

  bool ok = true;
  ok = ok && gpu_framebuffer_check_valid(dfbl->default_fb, NULL);
  ok = ok && gpu_framebuffer_check_valid(dfbl->color_only_fb, NULL);
  ok = ok && gpu_framebuffer_check_valid(dfbl->depth_only_fb, NULL);

  return ok;
}

static void workbench_render_result_z(struct RenderLayer *rl,
                                      const char *viewname,
                                      const rcti *rect)
{
  DefaultFramebufferList *dfbl = draw_viewport_framebuffer_list_get();
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  ViewLayer *view_layer = draw_ctx->view_layer;

  if ((view_layer->passflag & SCE_PASS_Z) != 0) {
    RenderPass *rp = render_pass_find_by_name(rl, RE_PASSNAME_Z, viewname);

    gpu_framebuffer_bind(dfbl->default_fb);
    gpu_framebuffer_read_depth(dfbl->default_fb,
                               rect->xmin,
                               rect->ymin,
                               lib_rcti_size_x(rect),
                               lib_rcti_size_y(rect),
                               GPU_DATA_FLOAT,
                               rp->rect);

    float winmat[4][4];
    draw_view_winmat_get(NULL, winmat, false);

    int pix_ct = lib_rcti_size_x(rect) * BLI_rcti_size_y(rect);

    /* Convert ogl depth [0..1] to view Z [near..far] */
    if (draw_view_is_persp_get(NULL)) {
      for (int i = 0; i < pix_ct; i++) {
        if (rp->rect[i] == 1.0f) {
          rp->rect[i] = 1e10f; /* Background */
        }
        else {
          rp->rect[i] = rp->rect[i] * 2.0f - 1.0f;
          rp->rect[i] = winmat[3][2] / (rp->rect[i] + winmat[2][2]);
        }
      }
    }
    else {
      /* Keep in mind, near and far distance are negatives. */
      float near = draw_view_near_distance_get(NULL);
      float far = draw_view_far_distance_get(NULL);
      float range = fabsf(far - near);

      for (int i = 0; i < pix_ct; i++) {
        if (rp->rect[i] == 1.0f) {
          rp->rect[i] = 1e10f; /* Background */
        }
        else {
          rp->rect[i] = rp->rect[i] * range - near;
        }
      }
    }
  }
}

void workbench_render(void *ved, RenderEngine *engine, RenderLayer *render_layer, const rcti *rect)
{
  DBenchData *data = ved;
  DefaultFramebufferList *dfbl = draw_viewport_framebuffer_list_get();
  const DRWContextState *draw_ctx = draw_ctx_state_get();
  Depsgraph *depsgraph = draw_ctx->dgraph;
  workbench_render_matrices_init(engine, dgraph);

  if (!workbench_render_framebuffers_init()) {
    RE_engine_report(engine, RPT_ERROR, "Failed to allocate OpenGL buffers");
    return;
  }

  workbench_private_data_alloc(data->stl);
  data->stl->wpd->cam_original_ob = dgraph_get_evaluated_object(depsgraph, RE_GetCamera(engine->re));
  workbench_engine_init(data);

  workbench_cache_init(data);
  draw_render_object_iter(data, engine, depsgraph, workbench_render_cache);
  workbench_cache_finish(data);

  draw_render_instance_buffer_finish();

  /* Also we weed to have a correct FBO bound for #DRW_hair_update */
  gpu_framebuffer_bind(dfbl->default_fb);
  draw_hair_update();

  gpu_framebuffer_bind(dfbl->default_fb);
  gpu_framebuffer_clear_depth(dfbl->default_fb, 1.0f);

  DBenchPrivateData *wpd = data->stl->wpd;
  while (wpd->taa_sample < max_ii(1, wpd->taa_sample_len)) {
    if (RE_engine_test_break(engine)) {
      break;
    }
    workbench_update_world_ubo(wpd);
    workbench_draw_sample(data);
  }

  workbench_draw_finish(data);

  /* Write render output. */
  const char *viewname = RE_GetActiveRenderView(engine->re);
  RenderPass *rp = RE_pass_find_by_name(render_layer, RE_PASSNAME_COMBINED, viewname);

  gpu_framebuffer_bind(dfbl->default_fb);
  gpu_framebuffer_read_color(dfbl->default_fb,
                             rect->xmin,
                             rect->ymin,
                             lib_rcti_size_x(rect),
                             lib_rcti_size_y(rect),
                             4,
                             0,
                             GPU_DATA_FLOAT,
                             rp->rect);

  workbench_render_result_z(render_layer, viewname, rect);
}

void workbench_render_update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer)
{
  RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_COMBINED, 4, "RGBA", SOCK_RGBA);
}
