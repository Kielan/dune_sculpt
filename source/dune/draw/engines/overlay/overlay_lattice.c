#include "draw_render.h"

#include "overlay_private.h"

void overlay_edit_lattice_cache_init(OVERLAY_Data *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;
  struct GPUShader *sh;
  DrawShadingGroup *grp;

  {
    DrawState state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
    DRAW_PASS_CREATE(psl->edit_lattice_ps, state | pd->clipping_state);

    sh = overlay_shader_edit_lattice_wire();
    pd->edit_lattice_wires_grp = grp = draw_shgroup_create(sh, psl->edit_lattice_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    draw_shgroup_uniform_texture(grp, "weightTex", G_draw.weight_ramp);

    sh = overlay_shader_edit_lattice_point();
    pd->edit_lattice_points_grp = grp = draw_shgroup_create(sh, psl->edit_lattice_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
  }
}

void overlay_edit_lattice_cache_populate(OverlayData *vedata, Object *ob)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  struct GPUBatch *geom;

  geom = draw_cache_lattice_wire_get(ob, true);
  draw_shgroup_call(pd->edit_lattice_wires_grp, geom, ob);

  geom = draw_cache_lattice_vert_overlay_get(ob);
  draw_shgroup_call(pd->edit_lattice_points_grp, geom, ob);
}

void overlay_lattice_cache_populate(OverlayData *vedata, Object *ob)
{
  OverlayExtraCallBuffers *cb = overlay_extra_call_buffer_get(vedata, ob);
  const DrawCtxState *draw_ctx = draw_ctx_state_get();

  float *color;
  draw_object_wire_theme_get(ob, draw_ctx->view_layer, &color);

  struct GPUBatch *geom = draw_cache_lattice_wire_get(ob, false);
  overlay_extra_wire(cb, geom, ob->obmat, color);
}

void overlay_edit_lattice_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayFramebufferList *fbl = vedata->fbl;

  if (draw_state_is_fbo()) {
    gpu_framebuffer_bind(fbl->overlay_default_fb);
  }

  draw_draw_pass(psl->edit_lattice_ps);
}
