#include "draw_render.h"

#include "dune_global.h"
#include "dune_pen.h"

#include "dune_ob.h"

#include "types_pen.h"

#include "ui.h"

#include "overlay_private.h"

/* Returns the normal plane in NDC space. */
static void pen_depth_plane(Ob *ob, float r_plane[4])
{
  /* Put that into private data. */
  float viewinv[4][4];
  draw_view_viewmat_get(NULL, viewinv, true);
  float *camera_z_axis = viewinv[2];
  float *camera_pos = viewinv[3];

  /* Find the normal most likely to represent the grease pencil object. */
  /* TODO: This does not work quite well if you use
   * strokes not aligned with the ob axes. Maybe we could try to
   * compute the minimum axis of all strokes. But this would be more
   * computationally heavy and should go into the GPData evaluation. */
  BoundBox *bbox = dune_ob_boundbox_get(ob);
  /* Convert bbox to matrix */
  float mat[4][4], size[3], center[3];
  dune_boundbox_calc_size_aabb(bbox, size);
  dune_boundbox_calc_center_aabb(bbox, center);
  unit_m4(mat);
  copy_v3_v3(mat[3], center);
  /* Avoid division by 0.0 later. */
  add_v3_fl(size, 1e-8f);
  rescale_m4(mat, size);
  /* BBox space to World. */
  mul_m4_m4m4(mat, ob->obmat, mat);
  /* BBox center in world space. */
  copy_v3_v3(center, mat[3]);
  /* View Vector. */
  if (draw_view_is_persp_get(NULL)) {
    /* BBox center to camera vector. */
    sub_v3_v3v3(r_plane, camera_pos, mat[3]);
  }
  else {
    copy_v3_v3(r_plane, camera_z_axis);
  }
  /* World to BBox space. */
  invert_m4(mat);
  /* Normalize the vector in BBox space. */
  mul_mat3_m4_v3(mat, r_plane);
  normalize_v3(r_plane);

  transpose_m4(mat);
  /* mat is now a "normal" matrix which will transform
   * BBox space normal to world space. */
  mul_mat3_m4_v3(mat, r_plane);
  normalize_v3(r_plane);

  plane_from_point_normal_v3(r_plane, center, r_plane);
}

void overlay_outline_init(OverlayData *vedata)
{
  OverlayFramebufList *fbl = vedata->fbl;
  OverlayTextureList *txl = vedata->txl;
  OverlayPrivateData *pd = vedata->stl->pd;
  DefaultTextureList *dtxl = draw_viewport_texture_list_get();

  if (drw_state_is_fbo()) {
    /* TODO: only alloc if needed. */
    drw_texture_ensure_fullscreen_2d(&txl->temp_depth_tx, GPU_DEPTH24_STENCIL8, 0);
    drw_texture_ensure_fullscreen_2d(&txl->outlines_id_tx, GPU_R16UI, 0);

    gpu_framebuf_ensure_config(
        &fbl->outlines_prepass_fb,
        {GPU_ATTACHMENT_TEXTURE(txl->temp_depth_tx), GPU_ATTACHMENT_TEXTURE(txl->outlines_id_tx)});

    if (pd->antialiasing.enabled) {
      gpu_framebuf_ensure_config(&fbl->outlines_resolve_fb,
                                    {
                                        GPU_ATTACHMENT_NONE,
                                        GPU_ATTACHMENT_TEXTURE(txl->overlay_color_tx),
                                        GPU_ATTACHMENT_TEXTURE(txl->overlay_line_tx),
                                    });
    }
    else {
      gpu_framebuffer_ensure_config(&fbl->outlines_resolve_fb,
                                    {
                                        GPU_ATTACHMENT_NONE,
                                        GPU_ATTACHMENT_TEXTURE(dtxl->color_overlay),
                                    });
    }
  }
}

void overlay_outline_cache_init(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayTextureList *txl = vedata->txl;
  OverlayPrivateData *pd = vedata->stl->pd;
  DefaultTextureList *dtxl = drw_viewport_texture_list_get();
  DrwShadingGroup *grp = NULL;

  const float outline_width = ui_GetThemeValf(TH_OUTLINE_WIDTH);
  const bool do_expand = (U.pixelsize > 1.0) || (outline_width > 2.0f);

  {
    DrwState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
    DRW_PASS_CREATE(psl->outlines_prepass_ps, state | pd->clipping_state);

    GPUShader *sh_geom = ovelay_shader_outline_prepass(pd->xray_enabled_and_not_wire);

    pd->outlines_grp = grp = drw_shgroup_create(sh_geom, psl->outlines_prepass_ps);
    draw_shgroup_uniform_bool_copy(grp, "isTransform", (G.moving & G_TRANSFORM_OBJ) != 0);

    GPUShader *sh_geom_ptcloud = overlay_shader_outline_prepass_pointcloud();

    pd->outlines_ptcloud_grp = grp = drw_shgroup_create(sh_geom_ptcloud, psl->outlines_prepass_ps);
    drw_shgroup_uniform_bool_copy(grp, "isTransform", (G.moving & G_TRANSFORM_OBJ) != 0);

    GPUShader *sh_gpencil = overlay_shader_outline_prepass_pen();

    pd->outlines_gpencil_grp = grp = drw_shgroup_create(sh_pen, psl->outlines_prepass_ps);
    drw_shgroup_uniform_bool_copy(grp, "isTransform", (G.moving & G_TRANSFORM_OBJ) != 0);
  }

  /* outlines_prepass_ps is still needed for selection of probes. */
  if (!(pd->v3d_flag & V3D_SEL_OUTLINE)) {
    return;
  }

  {
    /* We can only do alpha blending with lineOutput just after clearing the buffer. */
    DrwState state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA_PREMUL;
    DRW_PASS_CREATE(psl->outlines_detect_ps, state);

    GPUShader *sh = overlay_shader_outline_detect();

    grp = drw_shgroup_create(sh, psl->outlines_detect_ps);
    /* Don't occlude the "outline" detection pass if in xray mode (too much flickering). */
    drw_shgroup_uniform_float_copy(grp, "alphaOcclu", (pd->xray_enabled) ? 1.0f : 0.35f);
    drw_shgroup_uniform_bool_copy(grp, "doThickOutlines", do_expand);
    drw_shgroup_uniform_bool_copy(grp, "doAntiAliasing", pd->antialiasing.enabled);
    drw_shgroup_uniform_bool_copy(grp, "isXrayWires", pd->xray_enabled_and_not_wire);
    drw_shgroup_uniform_texture_ref(grp, "outlineId", &txl->outlines_id_tx);
    drw_shgroup_uniform_texture_ref(grp, "sceneDepth", &dtxl->depth);
    drw_shgroup_uniform_texture_ref(grp, "outlineDepth", &txl->tmp_depth_tx);
    drw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    drw_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
}

typedef struct iterData {
  Object *ob;
  DrawShadingGroup *stroke_grp;
  DrawShadingGroup *fill_grp;
  int cfra;
  float plane[4];
} iterData;

static void dpen_layer_cache_populate(DPenLayer *dpl,
                                      DPenFrame *UNUSED(dpf),
                                      DPenStroke *UNUSED(dps),
                                      void *thunk)
{
  iterData *iter = (iterData *)thunk;
  DPenData *dpd = (DPenData *)iter->ob->data;

  const bool is_screenspace = (dpd->flag & DP_DATA_STROKE_KEEPTHICKNESS) != 0;
  const bool is_stroke_order_3d = (dpd->draw_mode == DP_DRAWMODE_3D);

  float object_scale = mat4_to_scale(iter->ob->obmat);
  /* Negate thickness sign to tag that strokes are in screen space.
   * Convert to world units (by default, 1 meter = 2000 pixels). */
  float thickness_scale = (is_screenspace) ? -1.0f : (dpd->pixfactor / 2000.0f);

  DrawShadingGroup *grp = iter->stroke_grp = draw_shgroup_create_sub(iter->stroke_grp);
  draw_shgroup_uniform_bool_copy(grp, "strokeOrder3d", is_stroke_order_3d);
  draw_shgroup_uniform_vec2_copy(grp, "sizeViewportInv", draw_viewport_invert_size_get());
  draw_shgroup_uniform_vec2_copy(grp, "sizeViewport", draw_viewport_size_get());
  draw_shgroup_uniform_float_copy(grp, "thicknessScale", object_scale);
  draw_shgroup_uniform_float_copy(grp, "thicknessOffset", (float)dpl->line_change);
  draw_shgroup_uniform_float_copy(grp, "thicknessWorldScale", thickness_scale);
  draw_shgroup_uniform_vec4_copy(grp, "dPenDepthPlane", iter->plane);
}

static void dpen_stroke_cache_populate(DPenLayer *UNUSED(dpl),
                                          DPenFrame *UNUSED(dpf),
                                          DPenStroke *dps,
                                          void *thunk)
{
  iterData *iter = (iterData *)thunk;

  MaterialDPenStyle *gp_style = dune_dpen_material_settings(iter->ob, dps->mat_nr + 1);

  bool hide_material = (dp_style->flag & DP_MATERIAL_HIDE) != 0;
  bool show_stroke = (dp_style->flag & DP_MATERIAL_STROKE_SHOW) != 0;
  // TODO: What about simplify Fill?
  bool show_fill = (dps->tot_triangles > 0) && (dp_style->flag & DP_MATERIAL_FILL_SHOW) != 0;

  if (hide_material) {
    return;
  }

  if (show_fill) {
    struct GPUBatch *geom = draw_cache_dpen_fills_get(iter->ob, iter->cfra);
    int vfirst = dps->runtime.fill_start * 3;
    int vcount = dps->tot_triangles * 3;
    draw_shgroup_call_range(iter->fill_grp, iter->ob, geom, vfirst, vcount);
  }

  if (show_stroke) {
    struct GPUBatch *geom = draw_cache_dpen_strokes_get(iter->ob, iter->cfra);
    /* Start one vert before to have gl_InstanceID > 0 (see shader). */
    int vfirst = dps->runtime.stroke_start - 1;
    /* Include "potential" cyclic vertex and start adj vertex (see shader). */
    int vcount = dps->totpoints + 1 + 1;
    draw_shgroup_call_instance_range(iter->stroke_grp, iter->ob, geom, vfirst, vcount);
  }
}

static void overlay_outline_dpen(OverlayPrivateData *pd, Object *ob)
{
  /* No outlines in edit mode. */
  DPenData *dpd = (DPenData *)ob->data;
  if (dpd && DPEN_ANY_MODE(dpd)) {
    return;
  }

  iterData iter = {
      .ob = ob,
      .stroke_grp = pd->outlines_dpen_grp,
      .fill_grp = draw_shgroup_create_sub(pd->outlines_dpen_grp),
      .cfra = pd->cfra,
  };

  if (dpd->draw_mode == GP_DRAWMODE_2D) {
    dpen_depth_plane(ob, iter.plane);
  }

  dune_dpen_visible_stroke_advanced_iter(NULL,
                                         ob,
                                         dpen_layer_cache_populate,
                                         dpen_stroke_cache_populate,
                                         &iter,
                                         false,
                                         pd->cfra);
}

static void overlay_outline_volume(OverlayPrivateData *pd, Object *ob)
{
  struct GPUBatch *geom = draw_cache_volume_selection_surface_get(ob);
  if (geom == NULL) {
    return;
  }

  DRWShadingGroup *shgroup = pd->outlines_grp;
  DRW_shgroup_call(shgroup, geom, ob);
}

void overlay_outline_cache_populate(OverlayData *vedata,
                                    Object *ob,
                                    OverlayDupliData *dupli,
                                    bool init_dupli)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  struct GPUBatch *geom;
  DrawShadingGroup *shgroup = NULL;
  const bool draw_outline = ob->dt > OB_BOUNDBOX;

  /* Early exit: outlines of bounding boxes are not drawn. */
  if (!draw_outline) {
    return;
  }

  if (ob->type == OB_DPEN) {
    overlay_outline_dpen(pd, ob);
    return;
  }

  if (ob->type == OB_VOLUME) {
    overlay_outline_volume(pd, ob);
    return;
  }

  if (ob->type == OB_POINTCLOUD && pd->wireframe_mode) {
    /* Looks bad in this case. Could be relaxed if we draw a
     * wireframe of some sort in the future. */
    return;
  }

  if (dupli && !init_dupli) {
    geom = dupli->outline_geom;
    shgroup = dupli->outline_shgrp;
  }
  else {
    /* This fixes only the biggest case which is a plane in ortho view. */
    int flat_axis = 0;
    bool is_flat_object_viewed_from_side = ((draw_ctx->rv3d->persp == RV3D_ORTHO) &&
                                            draw_object_is_flat(ob, &flat_axis) &&
                                            draw_object_axis_orthogonal_to_view(ob, flat_axis));

    if (pd->xray_enabled_and_not_wire || is_flat_object_viewed_from_side) {
      geom = draw_cache_object_edge_detection_get(ob, NULL);
    }
    else {
      geom = draw_cache_object_surface_get(ob);
    }

    if (geom) {
      shgroup = (ob->type == OB_POINTCLOUD) ? pd->outlines_ptcloud_grp : pd->outlines_grp;
    }
  }

  if (shgroup && geom) {
    if (ob->type == OB_POINTCLOUD) {
      /* Draw range to avoid drawcall batching messing up the instance attribute. */
      draw_shgroup_call_instance_range(shgroup, ob, geom, 0, 0);
    }
    else {
      draw_shgroup_call(shgroup, geom, ob);
    }
  }

  if (init_dupli) {
    dupli->outline_shgrp = shgroup;
    dupli->outline_geom = geom;
  }
}

void overlay_outline_draw(OverlayData *vedata)
{
  OverlayFramebufferList *fbl = vedata->fbl;
  OverlayPassList *psl = vedata->psl;
  const float clearcol[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  bool do_outlines = psl->outlines_prepass_ps != NULL &&
                     !draw_pass_is_empty(psl->outlines_prepass_ps);

  if (draw_state_is_fbo() && do_outlines) {
    draw_stats_group_start("Outlines");

    /* Render filled polygon on a separate framebuffer */
    gpu_framebuffer_bind(fbl->outlines_prepass_fb);
    gpu_framebuffer_clear_color_depth_stencil(fbl->outlines_prepass_fb, clearcol, 1.0f, 0x00);
    draw_pass(psl->outlines_prepass_ps);

    /* Search outline pixels */
    gpu_framebuffer_bind(fbl->outlines_resolve_fb);
    draw_pass(psl->outlines_detect_ps);

    draw_stats_group_end();
  }
}
