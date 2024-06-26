#include "draw_render.h"

#include "ui_resources.h"

#include "draw_manager_text.h"
#include "overlay_private.h"

#define BG_SOLID 0
#define BG_GRADIENT 1
#define BG_CHECKER 2
#define BG_RADIAL 3
#define BG_SOLID_CHECKER 4
#define BG_MASK 5

void overlay_background_cache_init(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;
  DefaultTextureList *dtxl = draw_viewport_texture_list_get();
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  const Scene *scene = draw_ctx->scene;
  const RegionView3D *rv3d = draw_ctx->rv3d;
  const BoundBox *bb = rv3d ? rv3d->clipbb : NULL;
  const View3D *v3d = draw_ctx->v3d;
  bool draw_clipping_bounds = (pd->clipping_state != 0);

  {
    DrawState state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_BLEND_BACKGROUND;
    float color_override[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int background_type;

    if (draw_state_is_opengl_render() && !draw_state_draw_background()) {
      background_type = BG_SOLID;
      color_override[3] = 1.0f;
    }
    else if (pd->space_type == SPACE_IMAGE) {
      background_type = BG_SOLID_CHECKER;
    }
    else if (pd->space_type == SPACE_NODE) {
      background_type = BG_MASK;
      state = DRAW_STATE_WRITE_COLOR | DRW_STATE_BLEND_MUL;
    }
    else if (!draw_state_draw_background()) {
      background_type = BG_CHECKER;
    }
    else if (v3d->shading.background_type == V3D_SHADING_BACKGROUND_WORLD && scene->world) {
      background_type = BG_SOLID;
      /* TODO: this is a scene referred linear color. we should convert
       * it to display linear here. */
      copy_v3_v3(color_override, &scene->world->horr);
      color_override[3] = 1.0f;
    }
    else if (v3d->shading.background_type == V3D_SHADING_BACKGROUND_VIEWPORT &&
             v3d->shading.type <= OB_SOLID) {
      background_type = BG_SOLID;
      copy_v3_v3(color_override, v3d->shading.background_color);
      color_override[3] = 1.0f;
    }
    else {
      switch (ui_GetThemeValue(TH_BACKGROUND_TYPE)) {
        case TH_BACKGROUND_GRADIENT_LINEAR:
          background_type = BG_GRADIENT;
          break;
        case TH_BACKGROUND_GRADIENT_RADIAL:
          background_type = BG_RADIAL;
          break;
        default:
        case TH_BACKGROUND_SINGLE_COLOR:
          background_type = BG_SOLID;
          break;
      }
    }

    DRAW_PASS_CREATE(psl->background_ps, state);

    GPUShader *sh = overlay_shader_background();
    DrawShadingGroup *grp = draw_shgroup_create(sh, psl->background_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    draw_shgroup_uniform_texture_ref(grp, "colorBuffer", &dtxl->color);
    draw_shgroup_uniform_texture_ref(grp, "depthBuffer", &dtxl->depth);
    draw_shgroup_uniform_vec4_copy(grp, "colorOverride", color_override);
    draw_shgroup_uniform_int_copy(grp, "bgType", background_type);
    draw_shgroup_call_procedural_triangles(grp, NULL, 1);
  }

  if (draw_clipping_bounds) {
    DrawState state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_BLEND_ALPHA | DRAW_STATE_CULL_BACK;
    DRAW_PASS_CREATE(psl->clipping_frustum_ps, state);

    GPUShader *sh = overlay_shader_clipbound();
    DrawShadingGroup *grp = draw_shgroup_create(sh, psl->clipping_frustum_ps);
    draw_shgroup_uniform_vec4_copy(grp, "color", G_draw.block.colorClippingBorder);
    draw_shgroup_uniform_vec3(grp, "boundbox", &bb->vec[0][0], 8);

    struct GPUBatch *cube = draw_cache_cube_get();
    draw_shgroup_call(grp, cube, NULL);
  }
  else {
    psl->clipping_frustum_ps = NULL;
  }
}

void overlay_background_draw(OverlayData *vedata)
{
  overlay_PassList *psl = vedata->psl;

  if (draw_state_is_fbo()) {
    if (psl->clipping_frustum_ps) {
      draw_draw_pass(psl->clipping_frustum_ps);
    }

    draw_draw_pass(psl->background_ps);
  }
}
