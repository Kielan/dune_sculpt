/**
 * Transparent Pipeline:
 *
 * Use Weight Dune Order Independent Transparency to render transparent surfaces.
 *
 * The rendering is broken down in two passes:
 * - the accumulation pass where we render all the surfaces and accumulate all the weights.
 * - the resolve pass where we divide the accumulated information by the weights.
 *
 * An additional re-render of the transparent surfaces is sometime done in order to have their
 * correct depth and object ids correctly written.
 */

#include "draw_render.h"

#include "ed_view3d.h"

#include "workbench_engine.h"
#include "workbench_private.h"

void workbench_transparent_engine_init(DBenchData *data)
{
  DBenchFramebufferList *fbl = data->fbl;
  DBenchPrivateData *wpd = data->stl->wpd;
  DefaultTextureList *dtxl = draw_viewport_texture_list_get();
  DrawEngineType *owner = (DrawEngineType *)&workbench_transparent_engine_init;

  /* Reuse same format as opaque pipeline to reuse the textures. */
  /* NOTE: Floating point texture is required for the reveal_tex as it is used for
   * the alpha accumulation component (see accumulation shader for more explanation). */
  const eGPUTextureFormat accum_tex_format = GPU_RGBA16F;
  const eGPUTextureFormat reveal_tex_format = NORMAL_ENCODING_ENABLED() ? GPU_RG16F : GPU_RGBA32F;

  wpd->accum_buffer_tx = draw_texture_pool_query_fullscreen(accum_tex_format, owner);
  wpd->reveal_buffer_tx = draw_texture_pool_query_fullscreen(reveal_tex_format, owner);

  gpu_framebuffer_ensure_config(&fbl->transp_accum_fb,
                                {
                                    GPU_ATTACHMENT_TEXTURE(dtxl->depth),
                                    GPU_ATTACHMENT_TEXTURE(wpd->accum_buffer_tx),
                                    GPU_ATTACHMENT_TEXTURE(wpd->reveal_buffer_tx),
                                });
}

static void workbench_transparent_lighting_uniforms(DBenchPrivateData *wpd,
                                                    DrawShadingGroup *grp)
{
  draw_shgroup_uniform_block(grp, "world_data", wpd->world_ubo);
  draw_shgroup_uniform_bool_copy(grp, "forceShadowing", false);

  if (STUDIOLIGHT_TYPE_MATCAP_ENABLED(wpd)) {
    dune_studiolight_ensure_flag(wpd->studio_light,
                                STUDIOLIGHT_MATCAP_DIFFUSE_GPUTEXTURE |
                                    STUDIOLIGHT_MATCAP_SPECULAR_GPUTEXTURE);
    struct GPUTexture *diff_tx = wpd->studio_light->matcap_diffuse.gputexture;
    struct GPUTexture *spec_tx = wpd->studio_light->matcap_specular.gputexture;
    const bool use_spec = workbench_is_specular_highlight_enabled(wpd);
    spec_tx = (use_spec && spec_tx) ? spec_tx : diff_tx;
    draw_shgroup_uniform_texture(grp, "matcap_diffuse_tx", diff_tx);
    draw_shgroup_uniform_texture(grp, "matcap_specular_tx", spec_tx);
  }
}

void workbench_transparent_cache_init(DBenchData *vedata)
{
  DBenchPassList *psl = vedata->psl;
  DBenchPrivateData *wpd = vedata->stl->wpd;
  struct GPUShader *sh;
  DrawShadingGroup *grp;

  {
    int transp = 1;
    for (int infront = 0; infront < 2; infront++) {
      DRWState state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_DEPTH_LESS_EQUAL | DRAW_STATE_BLEND_OIT |
                       wpd->cull_state | wpd->clip_state;

      DrawPass *pass;
      if (infront) {
        psl->transp_accum_infront_ps = pass = draw_pass_create("transp_accum_infront", state);
        DRAW_PASS_INSTANCE_CREATE(
            psl->transp_depth_infront_ps, pass, state | DRAW_STATE_WRITE_DEPTH);
      }
      else {
        psl->transp_accum_ps = pass = draw_pass_create("transp_accum", state);
        DRAW_PASS_INSTANCE_CREATE(psl->transp_depth_ps, pass, state | DRAW_STATE_WRITE_DEPTH);
      }

      for (eDBenchDataType data = 0; data < WORKBENCH_DATATYPE_MAX; data++) {
        wpd->prepass[transp][infront][data].material_hash = lib_ghash_ptr_new(__func__);

        sh = workbench_shader_transparent_get(wpd, data);

        wpd->prepass[transp][infront][data].common_shgrp = grp = draw_shgroup_create(sh, pass);
        draw_shgroup_uniform_block(grp, "materials_data", wpd->material_ubo_curr);
        draw_shgroup_uniform_int_copy(grp, "materialIndex", -1);
        workbench_transparent_lighting_uniforms(wpd, grp);

        wpd->prepass[transp][infront][data].vcol_shgrp = grp = draw_shgroup_create(sh, pass);
        draw_shgroup_uniform_block(grp, "materials_data", wpd->material_ubo_curr);
        draw_shgroup_uniform_int_copy(grp, "materialIndex", 0); /* Default material. (uses vcol) */

        sh = workbench_shader_transparent_image_get(wpd, data, false);

        wpd->prepass[transp][infront][data].image_shgrp = grp = draw_shgroup_create(sh, pass);
        draw_shgroup_uniform_block(grp, "materials_data", wpd->material_ubo_curr);
        draw_shgroup_uniform_int_copy(grp, "materialIndex", 0); /* Default material. */
        workbench_transparent_lighting_uniforms(wpd, grp);

        sh = workbench_shader_transparent_image_get(wpd, data, true);

        wpd->prepass[transp][infront][data].image_tiled_shgrp = grp = draw_shgroup_create(sh, pass);
        draw_shgroup_uniform_block(grp, "materials_data", wpd->material_ubo_curr);
        draw_shgroup_uniform_int_copy(grp, "materialIndex", 0); /* Default material. */
        workbench_transparent_lighting_uniforms(wpd, grp);
      }
    }
  }
  {
    DrawState state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_BLEND_ALPHA;

    DRAW_PASS_CREATE(psl->transp_resolve_ps, state);

    sh = workbench_shader_transparent_resolve_get(wpd);

    grp = draw_shgroup_create(sh, psl->transp_resolve_ps);
    draw_shgroup_uniform_texture(grp, "transparentAccum", wpd->accum_buffer_tx);
    draw_shgroup_uniform_texture(grp, "transparentRevealage", wpd->reveal_buffer_tx);
    draw_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
}

void workbench_transparent_draw_depth_pass(DBenchData *data)
{
  DBenchPrivateData *wpd = data->stl->wpd;
  DBenchFramebufferList *fbl = data->fbl;
  DBenchPassList *psl = data->psl;

  const bool do_xray_depth_pass = !XRAY_FLAG_ENABLED(wpd) || XRAY_ALPHA(wpd) > 0.0f;
  const bool do_transparent_depth_pass = psl->outline_ps || wpd->dof_enabled || do_xray_depth_pass;

  if (do_transparent_depth_pass) {

    if (!draw_pass_is_empty(psl->transp_depth_ps)) {
      gpu_framebuffer_bind(fbl->opaque_fb);
      /* TODO: Disable writing to first two buffers. Unnecessary waste of bandwidth. */
      draw_draw_pass(psl->transp_depth_ps);
    }

    if (!draw_pass_is_empty(psl->transp_depth_infront_ps)) {
      gpu_framebuffer_bind(fbl->opaque_infront_fb);
      /* TODO: Disable writing to first two buffers. Unnecessary waste of bandwidth. */
      draw_draw_pass(psl->transp_depth_infront_ps);
    }
  }
}
