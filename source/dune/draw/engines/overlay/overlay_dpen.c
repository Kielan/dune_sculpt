#include "draw_render.h"

#include "dune_dpen.h"

#include "ui_resources.h"

#include "types_dpen.h"

#include "dgraph_query.h"

#include "ed_view3d.h"

#include "overlay_private.h"

#include "draw_common.h"
#include "draw_manager_text.h"

void overlay_edit_dpen_cache_init(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;
  struct GPUShader *sh;
  DrawShadingGroup *grp;

  /* Default: Display nothing. */
  pd->edit_dpen_points_grp = NULL;
  pd->edit_dpen_wires_grp = NULL;
  psl->edit_dpen_ps = NULL;

  pd->edit_dpen_curve_handle_grp = NULL;
  pd->edit_dpen_curve_points_grp = NULL;
  psl->edit_dpen_curve_ps = NULL;

  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  View3D *v3d = draw_ctx->v3d;
  Object *ob = draw_ctx->obact;
  DPdata *dpd = ob ? (DPdata *)ob->data : NULL;
  Scene *scene = draw_ctx->scene;
  ToolSettings *ts = scene->toolsettings;

  if (dpd == NULL || ob->type != OB_DPEN) {
    return;
  }

  /* For sculpt show only if mask mode, and only points if not stroke mode. */
  const bool use_sculpt_mask = (DPEN_SCULPT_MODE(gpd) &&
                                DPEN_ANY_SCULPT_MASK(ts->dpen_selectmode_sculpt));
  const bool show_sculpt_points = (DPEN_SCULPT_MODE(dpd) &&
                                   (ts->dpen_selectmode_sculpt &
                                    (DP_SCULPT_MASK_SELECTMODE_POINT |
                                     DP_SCULPT_MASK_SELECTMODE_SEGMENT)));

  /* For vertex paint show only if mask mode, and only points if not stroke mode. */
  bool use_vertex_mask = (DPEN_VERTEX_MODE(dpd) &&
                          DPEN_ANY_VERTEX_MASK(ts->dpen_selectmode_vertex));
  const bool show_vertex_points = (DPEN_VERTEX_MODE(dpd) &&
                                   (ts->dpen_selectmode_vertex &
                                    (DP_VERTEX_MASK_SELECTMODE_POINT |
                                     DP_VERTEX_MASK_SELECTMODE_SEGMENT)));

  /* If Sculpt or Vertex mode and the mask is disabled, the select must be hidden. */
  const bool hide_select = ((DPEN_SCULPT_MODE(dpd) && !use_sculpt_mask) ||
                            (DPEN_VERTEX_MODE(dpd) && !use_vertex_mask));

  const bool do_multiedit = DPEN_MULTIEDIT_SESSIONS_ON(dpd);
  const bool show_multi_edit_lines = (do_multiedit) &&
                                     ((v3d->dp_flag & (V3D_DP_SHOW_MULTIEDIT_LINES |
                                                       V3D_DP_SHOW_EDIT_LINES)) != 0);

  const bool show_lines = (v3d->dp_flag & V3D_DP_SHOW_EDIT_LINES) || show_multi_edit_lines;

  const bool hide_lines = !DPEN_EDIT_MODE(dpd) && !DPEN_WEIGHT_MODE(dpd) &&
                          !use_sculpt_mask && !use_vertex_mask && !show_lines;

  /* Special case when vertex paint and multiedit lines. */
  if (do_multiedit && show_multi_edit_lines && DPEN_VERTEX_MODE(dpd)) {
    use_vertex_mask = true;
  }

  const bool is_weight_paint = (dpd) && (dpd->flag & DP_DATA_STROKE_WEIGHTMODE);

  /* Show Edit points if:
   *  Edit mode: Not in Stroke selection mode
   *  Sculpt mode: If use Mask and not Stroke mode
   *  Weight mode: Always
   *  Vertex mode: If use Mask and not Stroke mode
   */
  const bool show_points = show_sculpt_points || is_weight_paint || show_vertex_points ||
                           (DPEN_EDIT_MODE(dpd) &&
                            (ts->dpen_selectmode_edit != DP_SELECTMODE_STROKE));

  if ((!DPEN_CURVE_EDIT_SESSIONS_ON(dpd)) &&
      ((!DPEN_VERTEX_MODE(dpd) && !DPEN_PAINT_MODE(dpd)) || use_vertex_mask)) {
    DrawState state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL |
                     DRAW_STATE_BLEND_ALPHA;
    DRAW_PASS_CREATE(psl->edit_dpen_ps, state | pd->clipping_state);

    if (show_lines && !hide_lines) {
      sh = overlay_shader_edit_dpen_wire();
      pd->edit_dpen_wires_grp = grp = draw_shgroup_create(sh, psl->edit_dpen_ps);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_bool_copy(grp, "doMultiframe", show_multi_edit_lines);
      draw_shgroup_uniform_bool_copy(grp, "doWeightColor", is_weight_paint);
      draw_shgroup_uniform_bool_copy(grp, "hideSelect", hide_select);
      draw_shgroup_uniform_float_copy(grp, "gpEditOpacity", v3d->vertex_opacity);
      draw_shgroup_uniform_texture(grp, "weightTex", G_draw.weight_ramp);
    }

    if (show_points && !hide_select) {
      sh = overlay_shader_edit_gpencil_point();
      pd->edit_dpen_points_grp = grp = DRW_shgroup_create(sh, psl->edit_gpencil_ps);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_bool_copy(grp, "doMultiframe", do_multiedit);
      draw_shgroup_uniform_bool_copy(grp, "doWeightColor", is_weight_paint);
      draw_shgroup_uniform_float_copy(grp, "gpEditOpacity", v3d->vertex_opacity);
      draw_shgroup_uniform_texture(grp, "weightTex", G_draw.weight_ramp);
    }
  }

  /* Handles and curve point for Curve Edit submode. */
  if (DPEN_CURVE_EDIT_SESSIONS_ON(dpd)) {
    DrawState state = DRAW_STATE_WRITE_COLOR;
    DRAW_PASS_CREATE(psl->edit_gpencil_curve_ps, state | pd->clipping_state);

    /* Edit lines. */
    if (show_lines) {
      sh = overlay_shader_edit_dpen_wire();
      pd->edit_dpen_wires_grp = grp = draw_shgroup_create(sh, psl->edit_dpen_curve_ps);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_bool_copy(grp, "doMultiframe", show_multi_edit_lines);
      draw_shgroup_uniform_bool_copy(grp, "doWeightColor", is_weight_paint);
      draw_shgroup_uniform_bool_copy(grp, "hideSelect", hide_select);
      draw_shgroup_uniform_float_copy(grp, "gpEditOpacity", v3d->vertex_opacity);
      draw_shgroup_uniform_texture(grp, "weightTex", G_draw.weight_ramp);
    }

    sh = overlay_shader_edit_curve_handle();
    pd->edit_dpen_curve_handle_grp = grp = DRW_shgroup_create(sh, psl->edit_gpencil_curve_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    draw_shgroup_uniform_bool_copy(grp, "showCurveHandles", pd->edit_curve.show_handles);
    draw_shgroup_uniform_int_copy(grp, "curveHandleDisplay", pd->edit_curve.handle_display);
    draw_shgroup_state_enable(grp, DRAW_STATE_BLEND_ALPHA);

    sh = overlay_shader_edit_curve_point();
    pd->edit_dpen_curve_points_grp = grp = DRAW_shgroup_create(sh, psl->edit_gpencil_curve_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    draw_shgroup_uniform_bool_copy(grp, "showCurveHandles", pd->edit_curve.show_handles);
    draw_shgroup_uniform_int_copy(grp, "curveHandleDisplay", pd->edit_curve.handle_display);
  }

  /* control points for primitives and speed guide */
  const bool is_cppoint = (dpd->runtime.tot_cp_points > 0);
  const bool is_speed_guide = (ts->dp_sculpt.guide.use_guide &&
                               (draw_ctx->object_mode == OB_MODE_PAINT_DPEN));
  const bool is_show_gizmo = (((v3d->gizmo_flag & V3D_GIZMO_HIDE) == 0) &&
                              ((v3d->gizmo_flag & V3D_GIZMO_HIDE_TOOL) == 0));

  if ((is_cppoint || is_speed_guide) && (is_show_gizmo)) {
    DrawState state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_BLEND_ALPHA;
    DRAW_PASS_CREATE(psl->edit_dpen_gizmos_ps, state);

    sh = overay_shader_edit_dpen_guide_point();
    grp = draw_shgroup_create(sh, psl->edit_dpen_gizmos_ps);

    if (dpd->runtime.cp_points != NULL) {
      for (int i = 0; i < dpd->runtime.tot_cp_points; i++) {
        DPenDataControlpoint *cp = &dpd->runtime.cp_points[i];
        grp = draw_shgroup_create_sub(grp);
        draw_shgroup_uniform_vec3_copy(grp, "pPosition", &cp->x);
        draw_shgroup_uniform_float_copy(grp, "pSize", cp->size * 0.8f * G_draw.block.sizePixel);
        draw_shgroup_uniform_vec4_copy(grp, "pColor", cp->color);
        draw_shgroup_call_procedural_points(grp, NULL, 1);
      }
    }

    if (ts->dp_sculpt.guide.use_guide) {
      float color[4];
      if (ts->gp_sculpt.guide.reference_point == DP_GUIDE_REF_CUSTOM) {
        ui_GetThemeColor4fv(TH_GIZMO_PRIMARY, color);
        draw_shgroup_uniform_vec3_copy(grp, "pPosition", ts->dp_sculpt.guide.location);
      }
      else if (ts->dp_sculpt.guide.reference_point == DP_GUIDE_REF_OBJECT &&
               ts->dp_sculpt.guide.reference_object != NULL) {
        ui_GetThemeColor4fv(TH_GIZMO_SECONDARY, color);
        draw_shgroup_uniform_vec3_copy(grp, "pPosition", ts->gp_sculpt.guide.reference_object->loc);
      }
      else {
        ui_GetThemeColor4fv(TH_REDALERT, color);
        draw_shgroup_uniform_vec3_copy(grp, "pPosition", scene->cursor.location);
      }
      draw_shgroup_uniform_vec4_copy(grp, "pColor", color);
      draw_shgroup_uniform_float_copy(grp, "pSize", 8.0f * G_draw.block.sizePixel);
      draw_shgroup_call_procedural_points(grp, NULL, 1);
    }
  }
}

void overlay_dpen_cache_init(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;
  struct GPUShader *sh;
  DrawShadingGroup *dsg;

  /* Default: Display nothing. */
  psl->dpen_canvas_ps = NULL;

  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  View3D *v3d = draw_ctx->v3d;
  Object *ob = draw_ctx->obact;
  DPenData *gpd = ob ? (DPenData *)ob->data : NULL;
  Scene *scene = draw_ctx->scene;
  ToolSettings *ts = scene->toolsettings;
  const View3DCursor *cursor = &scene->cursor;

  pd->edit_curve.show_handles = v3d->overlay.handle_display != CURVE_HANDLE_NONE;
  pd->edit_curve.handle_display = v3d->overlay.handle_display;

  if (dpd == NULL || ob->type != OB_DPEN) {
    return;
  }

  const bool show_overlays = (v3d->flag2 & V3D_HIDE_OVERLAYS) == 0;
  const bool show_grid = (v3d->dp_flag & V3D_GP_SHOW_GRID) != 0 &&
                         ((ts->dpen_v3d_align &
                           (DP_PROJECT_DEPTH_VIEW | GP_PROJECT_DEPTH_STROKE)) == 0);
  const bool grid_xray = (v3d->gp_flag & V3D_GP_SHOW_GRID_XRAY);

  if (show_grid && show_overlays) {
    const char *grid_unit = NULL;
    float mat[4][4];
    float col_grid[4];
    float size[2];

    /* set color */
    copy_v3_v3(col_grid, dpd->grid.color);
    col_grid[3] = max_ff(v3d->overlay.dpen_grid_opacity, 0.01f);

    copy_m4_m4(mat, ob->obmat);

    /* Rotate and scale except align to cursor. */
    DPenDataLayer *dpl = dune_dpen_layer_active_get(dpd);
    if (dpl != NULL) {
      if (ts->dp_sculpt.lock_axis != DP_LOCKAXIS_CURSOR) {
        float matrot[3][3];
        copy_m3_m4(matrot, dpl->layer_mat);
        mul_m4_m4m3(mat, mat, matrot);
      }
    }

    float viewinv[4][4];
    /* Set the grid in the selected axis */
    switch (ts->dp_sculpt.lock_axis) {
      case DP_LOCKAXIS_X:
        swap_v4_v4(mat[0], mat[2]);
        break;
      case DP_LOCKAXIS_Y:
        swap_v4_v4(mat[1], mat[2]);
        break;
      case DP_LOCKAXIS_Z:
        /* Default. */
        break;
      case DP_LOCKAXIS_CURSOR:
        loc_eul_size_to_mat4(mat, cursor->location, cursor->rotation_euler, (float[3]){1, 1, 1});
        break;
      case DP_LOCKAXIS_VIEW:
        /* view aligned */
        draw_view_viewmat_get(NULL, viewinv, true);
        copy_v3_v3(mat[0], viewinv[0]);
        copy_v3_v3(mat[1], viewinv[1]);
        break;
    }

    /* Move the grid to the right location depending of the align type.
     * This is required only for 3D Cursor or Origin. */
    if (ts->dpen_v3d_align & DP_PROJECT_CURSOR) {
      copy_v3_v3(mat[3], cursor->location);
    }
    else if (ts->dpen_v3d_align & DP_PROJECT_VIEWSPACE) {
      copy_v3_v3(mat[3], ob->obmat[3]);
    }

    translate_m4(mat, dpd->grid.offset[0], dpd->grid.offset[1], 0.0f);
    mul_v2_v2fl(size, dpd->grid.scale, 2.0f * ed_scene_grid_scale(scene, &grid_unit));
    rescale_m4(mat, (float[3]){size[0], size[1], 0.0f});

    /* Apply layer loc transform, except cursor mode. */
    if ((dpl != NULL) && (ts->dpen_v3d_align & DP_PROJECT_CURSOR) == 0) {
      add_v3_v3(mat[3], dpl->layer_mat[3]);
    }

    const int gridlines = (dpd->grid.lines <= 0) ? 1 : gpd->grid.lines;
    int line_ct = gridlines * 4 + 2;

    DrawState state = DRAW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA;
    state |= (grid_xray) ? DRAW_STATE_DEPTH_ALWAYS : DRAW_STATE_DEPTH_LESS_EQUAL;

    DRAW_PASS_CREATE(psl->dpen_canvas_ps, state);

    sh = overlay_shader_dpen_canvas();
    grp = draw_shgroup_create(sh, psl->dpen_canvas_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    draw_shgroup_uniform_vec4_copy(grp, "color", col_grid);
    draw_shgroup_uniform_vec3_copy(grp, "xAxis", mat[0]);
    draw_shgroup_uniform_vec3_copy(grp, "yAxis", mat[1]);
    draw_shgroup_uniform_vec3_copy(grp, "origin", mat[3]);
    draw_shgroup_uniform_int_copy(grp, "halfLineCount", line_ct / 2);
    draw_shgroup_call_procedural_lines(grp, NULL, line_ct);
  }
}

static void overlay_edit_dpen_cache_populate(OverlayData *vedata, Object *ob)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  DPenData *dpd = (DPenData *)ob->data;
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  View3D *v3d = draw_ctx->v3d;

  /* Overlay is only for active object. */
  if (ob != draw_ctx->obact) {
    return;
  }

  if (pd->edit_dpen_wires_grp) {
    DrawShadingGroup *grp = draw_shgroup_create_sub(pd->edit_dpen_wires_grp);
    draw_shgroup_uniform_vec4_copy(grp, "dpEditColor", dpd->line_color);

    struct GPUBatch *geom = draw_cache_dpen_edit_lines_get(ob, pd->cfra);
    draw_shgroup_call_no_cull(pd->edit_dpen_wires_grp, geom, ob);
  }

  if (pd->edit_dpen_points_grp) {
    const bool show_direction = (v3d->gp_flag & V3D_GP_SHOW_STROKE_DIRECTION) != 0;

    DrawShadingGroup *grp = draw_shgroup_create_sub(pd->edit_dpen_points_grp);
    draw_shgroup_uniform_float_copy(grp, "doStrokeEndpoints", show_direction);

    struct GPUBatch *geom = draw_cache_dpen_edit_points_get(ob, pd->cfra);
    draw_shgroup_call_no_cull(grp, geom, ob);
  }

  if (pd->edit_dpen_curve_handle_grp) {
    struct GPUBatch *geom = draw_cache_dpen_edit_curve_handles_get(ob, pd->cfra);
    if (geom) {
      draw_shgroup_call_no_cull(pd->edit_dpen_curve_handle_grp, geom, ob);
    }
  }

  if (pd->edit_gpencil_curve_points_grp) {
    struct GPUBatch *geom = draw_cache_dpen_edit_curve_points_get(ob, pd->cfra);
    if (geom) {
      draw_shgroup_call_no_cull(pd->edit_dpen_curve_points_grp, geom, ob);
    }
  }
}

static void overlay_dpen_draw_stroke_color_name(DPenDataLayer *UNUSED(dpl),
                                                   DPenDataFrame *UNUSED(dpf),
                                                   DPenDataStroke *dps,
                                                   void *thunk)
{
  Object *ob = (Object *)thunk;
  Material *ma = dune_object_material_get_eval(ob, dps->mat_nr + 1);
  if (ma == NULL) {
    return;
  }
  MaterialDPenStyle *dp_style = ma->dp_style;
  /* skip stroke if it doesn't have any valid data */
  if ((dps->points == NULL) || (dps->totpoints < 1) || (gp_style == NULL)) {
    return;
  }
  /* check if the color is visible */
  if (dp_style->flag & DP_MATERIAL_HIDE) {
    return;
  }
  /* only if selected */
  if (dps->flag & DPEN_STROKE_SELECT) {
    for (int i = 0; i < dps->totpoints; i++) {
      DPenStrokePoint *pt = &dps->points[i];
      /* Draw name at the first selected point. */
      if (pt->flag & GP_SPOINT_SELECT) {
        const DrawCtxState *draw_ctx = draw_ctx_state_get();
        ViewLayer *view_layer = draw_ctx->view_layer;

        int theme_id = draw_object_wire_theme_get(ob, view_layer, NULL);
        uchar color[4];
        ui_GetThemeColor4ubv(theme_id, color);

        float fpt[3];
        mul_v3_m4v3(fpt, ob->obmat, &pt->x);

        struct DrawTextStore *dt = draw_text_cache_ensure();
        draw_text_cache_add(dt,
                           fpt,
                           ma->id.name + 2,
                           strlen(ma->id.name + 2),
                           10,
                           0,
                           DRAW_TEXT_CACHE_GLOBALSPACE | DRW_TEXT_CACHE_STRING_PTR,
                           color);
        break;
      }
    }
  }
}

static void overlay_dpen_color_names(Object *ob)
{
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  int cfra = dgraph_get_ctime(draw_ctx->dgraph);

  dune_dpen_visible_stroke_advanced_iter(
      NULL, ob, NULL, overlay_dpen_draw_stroke_color_name, ob, false, cfra);
}

void overlay_dpen_cache_populate(OverlayData *vedata, Object *ob)
{
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  View3D *v3d = draw_ctx->v3d;

  DPenData *dpd = (DPenData *)ob->data;
  if (dpd == NULL) {
    return;
  }

  if (DPEN_ANY_MODE(dpd)) {
    overlay_edit_dpen_cache_populate(vedata, ob);
  }

  /* don't show object extras in set's */
  if ((ob->base_flag & (BASE_FROM_SET | BASE_FROM_DUPLI)) == 0) {
    if ((v3d->gp_flag & V3D_GP_SHOW_MATERIAL_NAME) && (ob->mode == OB_MODE_EDIT_DPEN) &&
        draw_state_show_text()) {
      overlay_dpen_color_names(ob);
    }
  }
}

void overlay_dpen_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;

  if (psl->dpen_canvas_ps) {
    draw_draw_pass(psl->dpen_canvas_ps);
  }
}

void overlay_edit_dpen_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;

  if (psl->edit_dpen_gizmos_ps) {
    draw_draw_pass(psl->edit_dpen_gizmos_ps);
  }

  if (psl->edit_dpen_ps) {
    draw_draw_pass(psl->edit_dpen_ps);
  }

  /* Curve edit handles. */
  if (psl->edit_dpen_curve_ps) {
    draw_draw_pass(psl->edit_dpen_curve_ps);
  }
}
