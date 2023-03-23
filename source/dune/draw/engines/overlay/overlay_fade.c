#include "dune_paint.h"
#include "draw_render.h"

#include "ed_view3d.h"

#include "overlay_private.h"

void overlay_fade_init(OverlayData *UNUSED(vedata))
{
}

void overlay_fade_cache_init(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;

  for (int i = 0; i < 2; i++) {
    /* Non Meshes Pass (Camera, empties, lights ...) */
    DrawState state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_DEPTH_EQUAL | DRW_STATE_BLEND_ALPHA;
    DRAW_PASS_CREATE(psl->fade_ps[i], state | pd->clipping_state);

    GPUShader *sh = overlay_shader_uniform_color();
    pd->fade_grp[i] = draw_shgroup_create(sh, psl->fade_ps[i]);

    const DrawCtxState *draw_ctx = draw_ctx_state_get();
    float color[4];
    ed_view3d_background_color_get(draw_ctx->scene, draw_ctx->v3d, color);
    color[3] = pd->overlay.fade_alpha;
    if (draw_ctx->v3d->shading.background_type == V3D_SHADING_BACKGROUND_THEME) {
      srgb_to_linearrgb_v4(color, color);
    }
    draw_shgroup_uniform_vec4_copy(pd->fade_grp[i], "color", color);
  }

  if (!pd->use_in_front) {
    pd->fade_grp[IN_FRONT] = pd->fade_grp[NOT_IN_FRONT];
  }
}

void overlay_fade_cache_populate(OVERLAY_Data *vedata, Object *ob)
{
  OverlayPrivateData *pd = vedata->stl->pd;

  if (pd->xray_enabled) {
    return;
  }

  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  const bool use_sculpt_pbvh = dune_sculptsession_use_pbvh_draw(ob, draw_ctx->v3d) &&
                               !draw_state_is_image_render();
  const bool is_xray = (ob->dtx & OB_DRAW_IN_FRONT) != 0;

  if (use_sculpt_pbvh) {
    draw_shgroup_call_sculpt(pd->fade_grp[is_xray], ob, false, false);
  }
  else {
    struct GPUBatch *geom = draw_cache_object_surface_get(ob);
    if (geom) {
      draw_shgroup_call(pd->fade_grp[is_xray], geom, ob);
    }
  }
}

void overlay_fade_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;

  draw_draw_pass(psl->fade_ps[NOT_IN_FRONT]);
}

void overlay_fade_infront_draw(OVERLAY_Data *vedata)
{
  OverlayPassList *psl = vedata->psl;

  DRW_draw_pass(psl->fade_ps[IN_FRONT]);
}
