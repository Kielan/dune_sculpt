/**
 * Outline Effect:
 *
 * Simple effect that just samples an object id buffer to detect objects outlines.
 */

#include "draw_render.h"

#include "workbench_engine.h"
#include "workbench_private.h"

void workbench_outline_cache_init(DBenchData *data)
{
  DBenchPassList *psl = data->psl;
  DBenchPrivateData *wpd = data->stl->wpd;
  DefaultTextureList *dtxl = draw_viewport_texture_list_get();
  struct GPUShader *sh;
  DrawShadingGroup *grp;

  if (OBJECT_OUTLINE_ENABLED(wpd)) {
    int state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_BLEND_ALPHA_PREMUL;
    DRAW_PASS_CREATE(psl->outline_ps, state);

    sh = workbench_shader_outline_get();

    grp = draw_shgroup_create(sh, psl->outline_ps);
    draw_shgroup_uniform_texture(grp, "objectIdBuffer", wpd->object_id_tx);
    draw_shgroup_uniform_texture(grp, "depthBuffer", dtxl->depth);
    draw_shgroup_uniform_block(grp, "world_data", wpd->world_ubo);
    draw_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
  else {
    psl->outline_ps = NULL;
  }
}
