#include "types_volume.h"

#include "draw_render.h"
#include "gpu_shader.h"

#include "overlay_private.h"

void overlay_volume_cache_init(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;
  const bool is_select = draw_state_is_select();

  if (is_select) {
    DrawState state = DRAW_STATE_WRITE_DEPTH | DRAW_STATE_DEPTH_LESS_EQUAL;
    DRAW_PASS_CREATE(psl->volume_ps, state | pd->clipping_state);
    GPUShader *sh = overlay_shader_depth_only();
    DrawShadingGroup *grp = draw_shgroup_create(sh, psl->volume_ps);
    pd->volume_selection_surface_grp = grp;
  }
  else {
    psl->volume_ps = NULL;
    pd->volume_selection_surface_grp = NULL;
  }
}

void overlay_volume_cache_populate(OverlayData *vedata, Object *ob)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  const bool is_select = draw_state_is_select();

  if (is_select) {
    struct GPUBatch *geom = draw_cache_volume_selection_surface_get(ob);
    if (geom != NULL) {
      draw_shgroup_call(pd->volume_selection_surface_grp, geom, ob);
    }
  }
}

void kernel overlay_volume_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;

  if (psl->volume_ps) {
    draw_draw_pass(psl->volume_ps);
  }
}
