#include "draw_render.h"

#include "draw_cache_impl.h"
#include "overlay_private.h"

#include "dune_paint.h"
#include "dune_pbvh.h"
#include "dune_subdiv_ccg.h"

void overlay_sculpt_cache_init(OverlayData *vedata)
{
  OVERLAY_PassList *psl = vedata->psl;
  OVERLAY_PrivateData *pd = vedata->stl->pd;
  DRWShadingGroup *grp;

  DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_EQUAL | DRW_STATE_BLEND_MUL;
  DRW_PASS_CREATE(psl->sculpt_mask_ps, state | pd->clipping_state);

  GPUShader *sh = OVERLAY_shader_sculpt_mask();
  pd->sculpt_mask_grp = grp = DRW_shgroup_create(sh, psl->sculpt_mask_ps);
  DRW_shgroup_uniform_float_copy(grp, "maskOpacity", pd->overlay.sculpt_mode_mask_opacity);
  DRW_shgroup_uniform_float_copy(
      grp, "faceSetsOpacity", pd->overlay.sculpt_mode_face_sets_opacity);
}

void overlay_sculpt_cache_populate(OverlayData *vedata, Object *ob)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  struct GPUBatch *sculpt_overlays;
  PBVH *pbvh = ob->sculpt->pbvh;

  const bool use_pbvh = dune_sculptsession_use_pbvh_draw(ob, draw_ctx->v3d);

  if (!pbvh) {
    /* It is possible to have SculptSession without PBVH. This happens, for example, when toggling
     * object mode to sculpt then to edit mode. */
    return;
  }

  if (!pbvh_has_mask(pbvh) && !pbvh_has_face_sets(pbvh)) {
    /* The SculptSession and the PBVH can be created without a Mask data-layer or Face Set
     * data-layer. (masks data-layers are created after using a mask tool), so in these cases there
     * is nothing to draw. */
    return;
  }

  if (use_pbvh) {
    draw_shgroup_call_sculpt(pd->sculpt_mask_grp, ob, false, true);
  }
  else {
    sculpt_overlays = draw_mesh_batch_cache_get_sculpt_overlays(ob->data);
    if (sculpt_overlays) {
      draw_shgroup_call(pd->sculpt_mask_grp, sculpt_overlays, ob);
    }
  }
}

void overlay_sculpt_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlatPrivateData *pd = vedata->stl->pd;
  DefaultFramebufferList *dfbl = draw_viewport_framebuffer_list_get();

  if (draw_state_is_fbo()) {
    gpu_framebuffer_bind(pd->painting.in_front ? dfbl->in_front_fb : dfbl->default_fb);
  }

  draw_pass(psl->sculpt_mask_ps);
}
