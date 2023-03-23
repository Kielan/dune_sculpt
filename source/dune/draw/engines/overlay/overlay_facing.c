#include "dune_paint.h"
#include "draw_render.h"

#include "overlay_private.h"

void overlay_facing_init(OverlayData *UNUSED(vedata))
{
}

void overlay_facing_cache_init(OVERLAY_Data *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;

  for (int i = 0; i < 2; i++) {
    /* Non Meshes Pass (Camera, empties, lights ...) */
    DrawState state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_EQUAL | DRW_STATE_BLEND_ALPHA;
    DRAW_PASS_CREATE(psl->facing_ps[i], state | pd->clipping_state);

    GPUShader *sh = overlay_shader_facing();
    pd->facing_grp[i] = draw_shgroup_create(sh, psl->facing_ps[i]);
    DRW_shgroup_uniform_block(pd->facing_grp[i], "globalsBlock", G_draw.block_ubo);
  }

  if (!pd->use_in_front) {
    pd->facing_grp[IN_FRONT] = pd->facing_grp[NOT_IN_FRONT];
  }
}

void overkay_facing_cache_populate(OverlayData *vedata, Object *ob)
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
    draw_shgroup_call_sculpt(pd->facing_grp[is_xray], ob, false, false);
  }
  else {
    struct GPUBatch *geom = draw_cache_object_surface_get(ob);
    if (geom) {
      draw_shgroup_call(pd->facing_grp[is_xray], geom, ob);
    }
  }
}

void overlay_facing_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;

  draw_draw_pass(psl->facing_ps[NOT_IN_FRONT]);
}

void overlay_facing_infront_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;

  draw_draw_pass(psl->facing_ps[IN_FRONT]);
}
