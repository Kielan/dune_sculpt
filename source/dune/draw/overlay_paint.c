
#include "drw_render.h"

#include "dune_img.h"

#include "types_mesh.h"

#include "graph_query.h"

#include "overlay_private.h"

/* Check if the given object is rendered (partially) transparent */
static bool paint_ob_is_rendered_transparent(View3D *v3d, Object *ob)
{
  if (v3d->shading.type == OB_WIRE) {
    return true;
  }
  if (v3d->shading.type == OB_SOLID) {
    if (v3d->shading.flag & V3D_SHADING_XRAY) {
      return true;
    }

    if (ob && v3d->shading.color_type == V3D_SHADING_OBJECT_COLOR) {
      return ob->color[3] < 1.0f;
    }
    if (ob && ob->type == OB_MESH && ob->data &&
        v3d->shading.color_type == V3D_SHADING_MATERIAL_COLOR) {
      Mesh *me = ob->data;
      for (int i = 0; i < me->totcol; i++) {
        Material *mat = dune_ob_material_get_eval(ob, i + 1);
        if (mat && mat->a < 1.0f) {
          return true;
        }
      }
    }
  }

  /* Check ob display types. */
  if (ob && ELEM(ob->dt, OB_WIRE, OB_BOUNDBOX)) {
    return true;
  }

  return false;
}

void overlay_paint_init(OverlayData *vedata)
{
  OverlayStorageList *stl = vedata->stl;
  OverlayPrivateData *pd = stl->pd;
  const DrwCtxState *draw_ctx = drw_cxt_state_get();

  pd->painting.in_front = pd->use_in_front && drw_cxt->obact &&
                          (drw_ctx->obact->dtx & OB_DRW_IN_FRONT);
  pd->painting.alpha_blending = paint_ob_is_rendered_transparent(drw_cxt->v3d,
                                                                 drw_cxt->obact);
}

void overlay_paint_cache_init(OverlayData *vedata)
{
  const DrawCxtState *drw_cxt = drw_cxt_state_get();
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;
  struct GPUShader *sh;
  DrwShadingGroup *grp;
  DrwState state;

  const bool is_edit_mode = (pd->cxt_mode == CXT_MODE_MESH_EDIT);
  const bool drw_contours = !is_edit_mode &&
                             (pd->overlay.wpaint_flag & V3D_OVERLAY_WPAINT_CONTOURS) != 0;
  float opacity = 0.0f;
  pd->paint_depth_grp = NULL;
  psl->paint_depth_ps = NULL;

  switch (pd->cxt_mode) {
    case CXT_MODE_POSE:
    case CXT_MODE_MESH_EDIT:
    case CXT_MODE_PAINT_WEIGHT: {
      opacity = is_edit_mode ? 1.0 : pd->overlay.weight_paint_mode_opacity;
      if (opacity > 0.0f) {
        state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_EQUAL | DRW_STATE_BLEND_ALPHA;
        DRW_PASS_CREATE(psl->paint_color_ps, state | pd->clipping_state);

        const bool do_shading = drw_ctx->v3d->shading.type != OB_WIRE;

        sh = overlay_shader_paint_weight(do_shading);
        pd->paint_surf_grp = grp = drw_shgroup_create(sh, psl->paint_color_ps);
        drw_shgroup_uniform_block(grp, "globalsBlock", G_drw.block_ubo);
        drw_shgroup_uniform_bool_copy(grp, "drwContours", drw_contours);
        drw_shgroup_uniform_float_copy(grp, "opacity", opacity);
        drw_shgroup_uniform_texture(grp, "colorramp", G_drw.weight_ramp);

        /* Arbitrary light to give a hint of the geometry behind the weights. */
        if (do_shading) {
          float light_dir[3];
          copy_v3_fl3(light_dir, 0.0f, 0.5f, 0.86602f);
          normalize_v3(light_dir);
          draw_shgroup_uniform_vec3_copy(grp, "light_dir", light_dir);
        }

        if (pd->painting.alpha_blending) {
          state = DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
          DRW_PASS_CREATE(psl->paint_depth_ps, state | pd->clipping_state);
          sh = overlay_shader_depth_only();
          pd->paint_depth_grp = drw_shgroup_create(sh, psl->paint_depth_ps);
        }
      }
      break;
    }
    case CTX_MODE_PAINT_VERTEX: {
      opacity = pd->overlay.vertex_paint_mode_opacity;
      if (opacity > 0.0f) {
        state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_DEPTH_EQUAL;
        state |= pd->painting.alpha_blending ? DRAW_STATE_BLEND_ALPHA : DRAW_STATE_BLEND_MUL;
        DRAW_PASS_CREATE(psl->paint_color_ps, state | pd->clipping_state);

        sh = overlay_shader_paint_vertcol();
        pd->paint_surf_grp = grp = DRW_shgroup_create(sh, psl->paint_color_ps);
        draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        draw_shgroup_uniform_bool_copy(grp, "useAlphaBlend", pd->painting.alpha_blending);
        draw_shgroup_uniform_float_copy(grp, "opacity", opacity);
      }
      break;
    }
    case CTX_MODE_PAINT_TEXTURE: {
      const ImagePaintSettings *imapaint = &draw_ctx->scene->toolsettings->imapaint;
      const bool mask_enabled = imapaint->flag & IMAGEPAINT_PROJECT_LAYER_STENCIL &&
                                imapaint->stencil != NULL;

      opacity = mask_enabled ? pd->overlay.texture_paint_mode_opacity : 0.0f;
      if (opacity > 0.0f) {
        state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_EQUAL | DRW_STATE_BLEND_ALPHA;
        DRW_PASS_CREATE(psl->paint_color_ps, state | pd->clipping_state);

        GPUTexture *tex = BKE_image_get_gpu_texture(imapaint->stencil, NULL, NULL);

        const bool mask_premult = (imapaint->stencil->alpha_mode == IMA_ALPHA_PREMUL);
        const bool mask_inverted = (imapaint->flag & IMAGEPAINT_PROJECT_LAYER_STENCIL_INV) != 0;
        sh = overlay_shader_paint_texture();
        pd->paint_surf_grp = grp = draw_shgroup_create(sh, psl->paint_color_ps);
        draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        draw_shgroup_uniform_float_copy(grp, "opacity", opacity);
        draw_shgroup_uniform_bool_copy(grp, "maskPremult", mask_premult);
        draw_shgroup_uniform_vec3_copy(grp, "maskColor", imapaint->stencil_col);
        draw_shgroup_uniform_bool_copy(grp, "maskInvertStencil", mask_inverted);
        draw_shgroup_uniform_texture(grp, "maskImage", tex);
      }
      break;
    }
    default:
      lib_assert(0);
      break;
  }

  if (opacity <= 0.0f) {
    psl->paint_color_ps = NULL;
    pd->paint_surf_grp = NULL;
  }

  {
    state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_WRITE_DEPTH | DRAW_STATE_DEPTH_LESS_EQUAL;
    DRAW_PASS_CREATE(psl->paint_overlay_ps, state | pd->clipping_state);
    sh = overlay_shader_paint_face();
    pd->paint_face_grp = grp = draw_shgroup_create(sh, psl->paint_overlay_ps);
    draw_shgroup_uniform_vec4_copy(grp, "color", (float[4]){1.0f, 1.0f, 1.0f, 0.2f});
    draw_shgroup_state_enable(grp, DRAW_STATE_BLEND_ALPHA);

    sh = overlay_shader_paint_wire();
    pd->paint_wire_selected_grp = grp = draw_shgroup_create(sh, psl->paint_overlay_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    draw_shgroup_uniform_bool_copy(grp, "useSelect", true);
    draw_shgroup_state_enable(grp, DRAW_STATE_BLEND_ALPHA);

    pd->paint_wire_grp = grp = draw_shgroup_create(sh, psl->paint_overlay_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    draw_shgroup_uniform_bool_copy(grp, "useSelect", false);
    draw_shgroup_state_enable(grp, DRAW_STATE_BLEND_ALPHA);

    sh = Overlayshader_paint_point();
    pd->paint_point_grp = grp = draw_shgroup_create(sh, psl->paint_overlay_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
  }
}

void overlay_paint_texture_cache_populate(OVERLAY_Data *vedata, Object *ob)
{
  OVERLAY_PrivateData *pd = vedata->stl->pd;
  struct GPUBatch *geom = NULL;

  const Mesh *me_orig = DEG_get_original_object(ob)->data;
  const bool use_face_sel = (me_orig->editflag & ME_EDIT_PAINT_FACE_SEL) != 0;

  if (pd->paint_surf_grp) {
    geom = DRW_cache_mesh_surface_texpaint_single_get(ob);
    DRW_shgroup_call(pd->paint_surf_grp, geom, ob);
  }

  if (use_face_sel) {
    geom = DRW_cache_mesh_surface_get(ob);
    DRW_shgroup_call(pd->paint_face_grp, geom, ob);
  }
}

void OVERLAY_paint_vertex_cache_populate(OVERLAY_Data *vedata, Object *ob)
{
  OVERLAY_PrivateData *pd = vedata->stl->pd;
  struct GPUBatch *geom = NULL;

  const Mesh *me_orig = DEG_get_original_object(ob)->data;
  const bool is_edit_mode = (pd->ctx_mode == CTX_MODE_EDIT_MESH);
  const bool use_wire = !is_edit_mode && (pd->overlay.paint_flag & V3D_OVERLAY_PAINT_WIRE);
  const bool use_face_sel = !is_edit_mode && (me_orig->editflag & ME_EDIT_PAINT_FACE_SEL);
  const bool use_vert_sel = !is_edit_mode && (me_orig->editflag & ME_EDIT_PAINT_VERT_SEL);

  if (ELEM(ob->mode, OB_MODE_WEIGHT_PAINT, OB_MODE_EDIT)) {
    if (pd->paint_surf_grp) {
      geom = DRW_cache_mesh_surface_weights_get(ob);
      DRW_shgroup_call(pd->paint_surf_grp, geom, ob);
    }
    if (pd->paint_depth_grp) {
      geom = DRW_cache_mesh_surface_weights_get(ob);
      DRW_shgroup_call(pd->paint_depth_grp, geom, ob);
    }
  }

  if (use_face_sel || use_wire) {
    geom = DRW_cache_mesh_surface_edges_get(ob);
    DRW_shgroup_call(use_face_sel ? pd->paint_wire_selected_grp : pd->paint_wire_grp, geom, ob);
  }

  if (use_face_sel) {
    geom = DRW_cache_mesh_surface_get(ob);
    DRW_shgroup_call(pd->paint_face_grp, geom, ob);
  }

  if (use_vert_sel) {
    geom = DRW_cache_mesh_all_verts_get(ob);
    DRW_shgroup_call(pd->paint_point_grp, geom, ob);
  }
}

void OVERLAY_paint_weight_cache_populate(OVERLAY_Data *vedata, Object *ob)
{
  OVERLAY_paint_vertex_cache_populate(vedata, ob);
}

void OVERLAY_paint_draw(OVERLAY_Data *vedata)
{
  OVERLAY_StorageList *stl = vedata->stl;
  OVERLAY_PrivateData *pd = stl->pd;

  OVERLAY_PassList *psl = vedata->psl;
  OVERLAY_FramebufferList *fbl = vedata->fbl;

  if (DRW_state_is_fbo()) {
    GPU_framebuffer_bind(pd->painting.in_front ? fbl->overlay_in_front_fb :
                                                 fbl->overlay_default_fb);
  }

  if (psl->paint_depth_ps) {
    DRW_draw_pass(psl->paint_depth_ps);
  }
  if (psl->paint_color_ps) {
    DRW_draw_pass(psl->paint_color_ps);
  }
  if (psl->paint_overlay_ps) {
    DRW_draw_pass(psl->paint_overlay_ps);
  }
}
