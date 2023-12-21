#include "drw_render.h"

#include "drw_cache_impl.h"
#include "overlay_private.h"

#include "dune_paint.h"
#include "dune_pbvh.h"
#include "dune_subdiv_ccg.h"

void overlay_sculpt_cache_init(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;
  DrwShadingGroup *grp;

  DrwState state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_EQUAL | DRW_STATE_BLEND_MUL;
  DRW_PASS_CREATE(psl->sculpt_mask_ps, state | pd->clipping_state);

  GPUShader *sh = overlay_shader_sculpt_mask();
  pd->sculpt_mask_grp = grp = drw_shgroup_create(sh, psl->sculpt_mask_ps);
  drw_shgroup_uniform_float_copy(grp, "maskOpacity", pd->overlay.sculpt_mode_mask_opacity);
  drw_shgroup_uniform_float_copy(
      grp, "faceSetsOpacity", pd->overlay.sculpt_mode_face_sets_opacity);
}

void overlay_sculpt_cache_populate(OverlayData *vedata, Ob *ob)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  const DrwCxtState *drw_cxt = drw_cxt_state_get();
  struct GPUBatch *sculpt_overlays;
  PBVH *pbvh = ob->sculpt->pbvh;

  const bool use_pbvh = dune_sculptsession_use_pbvh_drw(ob, drw_cxt->v3d);

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
    drw_shgroup_call_sculpt(pd->sculpt_mask_grp, ob, false, true);
  }
  else {
    sculpt_overlays = drw_mesh_batch_cache_get_sculpt_overlays(ob->data);
    if (sculpt_overlays) {
      drw_shgroup_call(pd->sculpt_mask_grp, sculpt_overlays, ob);
    }
  }
}

void overlay_sculpt_drw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlatPrivateData *pd = vedata->stl->pd;
  DefaultFramebufList *dfbl = drw_viewport_framebuf_list_get();

  if (drw_state_is_fbo()) {
    gpu_framebuf_bind(pd->painting.in_front ? dfbl->in_front_fb : dfbl->default_fb);
  }

  drw_pass(psl->sculpt_mask_ps);
}
