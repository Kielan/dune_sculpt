#include "types_collection.h"
#include "types_mesh.h"
#include "types_particle.h"
#include "types_view3d.h"
#include "types_volume.h"

#include "dune_curve.h"
#include "dune_displist.h"
#include "dune_duplist.h"
#include "dune_meshedit.h"
#include "dune_global.h"
#include "dune_ob.h"
#include "dune_paint.h"
#include "dune_particle.h"

#include "lib_hash.h"

#include "drw_render.h"
#include "gpu_shader.h"

#include "ed_view3d.h"

#include "overlay_private.h"

void overlay_wireframe_init(OverlayData *vedata)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  const DrwCtxState *drw_ctx = drw_cxt_state_get();
  DrwView *default_view = (DrwView *)drw_view_default_get();
  pd->view_wires = drw_view_create_with_zoffset(default_view, draw_ctx->rv3d, 0.5f);
}

void overlay_wireframe_cache_init(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayTextureList *txl = vedata->txl;
  OverlayPrivateData *pd = vedata->stl->pd;
  const DrwCtxState *drw_ctx = drw_cxt_state_get();
  DrwShadingGroup *grp = NULL;

  View3DShading *shading = &draw_ctx->v3d->shading;

  pd->shdata.wire_step_param = pd->overlay.wireframe_threshold - 254.0f / 255.0f;
  pd->shdata.wire_opacity = pd->overlay.wireframe_opacity;

  bool is_wire_shmode = (shading->type == OB_WIRE);
  bool is_material_shmode = (shading->type > OB_SOLID);
  bool is_ob_color = is_wire_shmode && (shading->wire_color_type == V3D_SHADING_OBJECT_COLOR);
  bool is_random_color = is_wire_shmode && (shading->wire_color_type == V3D_SHADING_RANDOM_COLOR);

  const bool use_sel = (drw_state_is_sel() || drw_state_is_depth());
  GPUShader *wires_sh = use_select ? overlay_shader_wireframe_select() :
                                     overlay_shader_wireframe(pd->antialiasing.enabled);

  for (int xray = 0; xray < (is_material_shmode ? 1 : 2); xray++) {
    DrawState state = DRAW_STATE_FIRST_VERTEX_CONVENTION | DRAW_STATE_WRITE_COLOR |
                     DRAW_STATE_WRITE_DEPTH | DRAW_STATE_DEPTH_LESS_EQUAL;
    DrawPass *pass;
    GPUTexture **depth_tx = ((!pd->xray_enabled || pd->xray_opacity > 0.0f) &&
                             draw_state_is_fbo()) ?
                                &txl->temp_depth_tx :
                                &txl->dummy_depth_tx;

    if (xray == 0) {
      DRAW_PASS_CREATE(psl->wireframe_ps, state | pd->clipping_state);
      pass = psl->wireframe_ps;
    }
    else {
      DRAW_PASS_CREATE(psl->wireframe_xray_ps, state | pd->clipping_state);
      pass = psl->wireframe_xray_ps;
    }

    for (int use_coloring = 0; use_coloring < 2; use_coloring++) {
      pd->wires_grp[xray][use_coloring] = grp = DRW_shgroup_create(wires_sh, pass);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_texture_ref(grp, "depthTex", depth_tx);
      draw_shgroup_uniform_float_copy(grp, "wireStepParam", pd->shdata.wire_step_param);
      draw_shgroup_uniform_float_copy(grp, "wireOpacity", pd->shdata.wire_opacity);
      draw_shgroup_uniform_bool_copy(grp, "useColoring", use_coloring);
      draw_shgroup_uniform_bool_copy(grp, "isTransform", (G.moving & G_TRANSFORM_OBJ) != 0);
      draw_shgroup_uniform_bool_copy(grp, "isObjectColor", is_object_color);
      draw_shgroup_uniform_bool_copy(grp, "isRandomColor", is_random_color);
      draw_shgroup_uniform_bool_copy(grp, "isHair", false);

      pd->wires_all_grp[xray][use_coloring] = grp = DRW_shgroup_create(wires_sh, pass);
      draw_shgroup_uniform_float_copy(grp, "wireStepParam", 1.0f);

      pd->wires_hair_grp[xray][use_coloring] = grp = DRW_shgroup_create(wires_sh, pass);
      draw_shgroup_uniform_bool_copy(grp, "isHair", true);
      draw_shgroup_uniform_float_copy(grp, "wireStepParam", 10.0f);
    }

    pd->wires_sculpt_grp[xray] = grp = DRW_shgroup_create(wires_sh, pass);
    draw_shgroup_uniform_texture_ref(grp, "depthTex", depth_tx);
    draw_shgroup_uniform_float_copy(grp, "wireStepParam", 10.0f);
    draw_shgroup_uniform_bool_copy(grp, "useColoring", false);
    draw_shgroup_uniform_bool_copy(grp, "isHair", false);
  }

  if (is_material_shmode) {
    /* Make all drawcalls go into the non-xray shading groups. */
    for (int use_coloring = 0; use_coloring < 2; use_coloring++) {
      pd->wires_grp[1][use_coloring] = pd->wires_grp[0][use_coloring];
      pd->wires_all_grp[1][use_coloring] = pd->wires_all_grp[0][use_coloring];
      pd->wires_hair_grp[1][use_coloring] = pd->wires_hair_grp[0][use_coloring];
    }
    pd->wires_sculpt_grp[1] = pd->wires_sculpt_grp[0];
    psl->wireframe_xray_ps = NULL;
  }
}

static void wireframe_hair_cache_populate(OVERLAY_Data *vedata, Object *ob, ParticleSystem *psys)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  const bool is_xray = (ob->dtx & OB_DRAW_IN_FRONT) != 0;

  Object *dupli_parent = draw_object_get_dupli_parent(ob);
  DupliObject *dupli_object = draw_object_get_dupli(ob);

  float dupli_mat[4][4];
  if ((dupli_parent != NULL) && (dupli_object != NULL)) {
    if (dupli_object->type & OB_DUPLICOLLECTION) {
      unit_m4(dupli_mat);
      Collection *collection = dupli_parent->instance_collection;
      if (collection != NULL) {
        sub_v3_v3(dupli_mat[3], collection->instance_offset);
      }
      mul_m4_m4m4(dupli_mat, dupli_parent->obmat, dupli_mat);
    }
    else {
      copy_m4_m4(dupli_mat, dupli_object->ob->obmat);
      invert_m4(dupli_mat);
      mul_m4_m4m4(dupli_mat, ob->obmat, dupli_mat);
    }
  }
  else {
    unit_m4(dupli_mat);
  }

  struct GPUBatch *hairs = draw_cache_particles_get_hair(ob, psys, NULL);

  const bool use_coloring = true;
  DrawShadingGroup *shgrp = draw_shgroup_create_sub(pd->wires_hair_grp[is_xray][use_coloring]);
  draw_shgroup_uniform_vec4_array_copy(shgrp, "hairDupliMatrix", dupli_mat, 4);
  draw_shgroup_call_no_cull(shgrp, hairs, ob);
}

void overlay_wireframe_cache_populate(OVERLAY_Data *vedata,
                                      Object *ob,
                                      OVERLAY_DupliData *dupli,
                                      bool init_dupli)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  const bool all_wires = (ob->dtx & OB_DRAW_ALL_EDGES) != 0;
  const bool is_xray = (ob->dtx & OB_DRAW_IN_FRONT) != 0;
  const bool is_mesh = ob->type == OB_MESH;
  const bool is_edit_mode = DRW_object_is_in_edit_mode(ob);
  bool has_edit_mesh_cage = false;
  bool is_mesh_verts_only = false;
  if (is_mesh) {
    /* TODO: Should be its own function. */
    Mesh *me = ob->data;
    if (is_edit_mode) {
      lib_assert(me->edit_mesh);
      Mesh *editmesh_eval_final = BKE_object_get_editmesh_eval_final(ob);
      Mesh *editmesh_eval_cage = BKE_object_get_editmesh_eval_cage(ob);
      has_edit_mesh_cage = editmesh_eval_cage && (editmesh_eval_cage != editmesh_eval_final);
      if (editmesh_eval_final) {
        me = editmesh_eval_final;
      }
    }
    is_mesh_verts_only = me->totedge == 0 && me->totvert > 0;
  }

  const bool use_wire = !is_mesh_verts_only && ((pd->overlay.flag & V3D_OVERLAY_WIREFRAMES) ||
                                                (ob->dtx & OB_DRAWWIRE) || (ob->dt == OB_WIRE));

  if (use_wire && pd->wireframe_mode && ob->particlesystem.first) {
    for (ParticleSystem *psys = ob->particlesystem.first; psys != NULL; psys = psys->next) {
      if (!draw_object_is_visible_psys_in_active_context(ob, psys)) {
        continue;
      }
      ParticleSettings *part = psys->part;
      const int draw_as = (part->draw_as == PART_DRAW_REND) ? part->ren_as : part->draw_as;
      if (draw_as == PART_DRAW_PATH) {
        wireframe_hair_cache_populate(vedata, ob, psys);
      }
    }
  }

  if (ELEM(ob->type, OB_CURVES_LEGACY, OB_FONT, OB_SURF)) {
    OverlayExtraCallBuffers *cb = OVERLAY_extra_call_buffer_get(vedata, ob);
    float *color;
    draw_object_wire_theme_get(ob, draw_ctx->view_layer, &color);

    struct GPUBatch *geom = NULL;
    switch (ob->type) {
      case OB_CURVES_LEGACY:
        geom = draw_cache_curve_edge_wire_get(ob);
        break;
      case OB_FONT:
        geom = draw_cache_text_edge_wire_get(ob);
        break;
      case OB_SURF:
        geom = draw_cache_surf_edge_wire_get(ob);
        break;
    }

    if (geom) {
      overlay_extra_wire(cb, geom, ob->obmat, color);
    }
  }

  /* Fast path for duplis. */
  if (dupli && !init_dupli) {
    if (dupli->wire_shgrp && dupli->wire_geom) {
      if (dupli->base_flag == ob->base_flag) {
        /* Check for the special cases used below, assign specific theme colors to the shaders. */
        OverlayExtraCallBuffers *cb = overlay_extra_call_buffer_get(vedata, ob);
        if (dupli->wire_shgrp == cb->extra_loose_points) {
          float *color;
          draw_object_wire_theme_get(ob, draw_ctx->view_layer, &color);
          overlay_extra_loose_points(cb, dupli->wire_geom, ob->obmat, color);
        }
        else if (dupli->wire_shgrp == cb->extra_wire) {
          float *color;
          draw_object_wire_theme_get(ob, draw_ctx->view_layer, &color);
          overlay_extra_wire(cb, dupli->wire_geom, ob->obmat, color);
        }
        else {
          draw_shgroup_call(dupli->wire_shgrp, dupli->wire_geom, ob);
        }
        return;
      }
    }
    else {
      /* Nothing to draw for this dupli. */
      return;
    }
  }

  if (use_wire && ELEM(ob->type, OB_VOLUME, OB_POINTCLOUD)) {
    bool draw_as_points = true;
    if (ob->type == OB_VOLUME) {
      /* Volume object as points exception. */
      Volume *volume = ob->data;
      draw_as_points = volume->display.wireframe_type == VOLUME_WIREFRAME_POINTS;
    }

    if (draw_as_points) {
      float *color;
      OverlayExtraCallBuffers *cb = overlay_extra_call_buffer_get(vedata, ob);
      draw_object_wire_theme_get(ob, draw_ctx->view_layer, &color);

      struct GPUBatch *geom = draw_cache_object_face_wireframe_get(ob);
      if (geom) {
        overlay_extra_loose_points(cb, geom, ob->obmat, color);
      }
      return;
    }
  }

  DrawShadingGroup *shgrp = NULL;
  struct GPUBatch *geom = NULL;

  /* Don't do that in edit Mesh mode, unless there is a modifier preview. */
  if (use_wire && (!is_mesh || (!is_edit_mode || has_edit_mesh_cage))) {
    const bool is_sculpt_mode = ((ob->mode & OB_MODE_SCULPT) != 0) && (ob->sculpt != NULL);
    const bool use_sculpt_pbvh = dune_sculptsession_use_pbvh_draw(ob, draw_ctx->v3d) &&
                                 !draw_state_is_image_render();
    const bool is_instance = (ob->base_flag & BASE_FROM_DUPLI);
    const bool instance_parent_in_edit_mode = is_instance ? DRW_object_is_in_edit_mode(
                                                                DRW_object_get_dupli_parent(ob)) :
                                                            false;
    const bool use_coloring = (use_wire && !is_edit_mode && !is_sculpt_mode &&
                               !has_edit_mesh_cage && !instance_parent_in_edit_mode);
    geom = draw_cache_object_face_wireframe_get(ob);

    if (geom || use_sculpt_pbvh) {
      if (use_sculpt_pbvh) {
        shgrp = pd->wires_sculpt_grp[is_xray];
      }
      else if (all_wires) {
        shgrp = pd->wires_all_grp[is_xray][use_coloring];
      }
      else {
        shgrp = pd->wires_grp[is_xray][use_coloring];
      }

      if (ob->type == OB_DPEN) {
        /* TODO: Make DPen objects have correct bound-box. */
        draw_shgroup_call_no_cull(shgrp, geom, ob);
      }
      else if (use_sculpt_pbvh) {
        draw_shgroup_call_sculpt(shgrp, ob, true, false);
      }
      else {
        draw_shgroup_call(shgrp, geom, ob);
      }
    }
  }
  else if (is_mesh && (!is_edit_mode || has_edit_mesh_cage)) {
    OverlayExtraCallBuffers *cb = overlay_extra_call_buffer_get(vedata, ob);
    float *color;
    draw_object_wire_theme_get(ob, draw_ctx->view_layer, &color);

    /* Draw loose geometry. */
    if (is_mesh_verts_only) {
      geom = draw_cache_mesh_all_verts_get(ob);
      if (geom) {
        overlay_extra_loose_points(cb, geom, ob->obmat, color);
        shgrp = cb->extra_loose_points;
      }
    }
    else {
      geom = draw_cache_mesh_loose_edges_get(ob);
      if (geom) {
        overlay_extra_wire(cb, geom, ob->obmat, color);
        shgrp = cb->extra_wire;
      }
    }
  }

  if (dupli) {
    dupli->wire_shgrp = shgrp;
    dupli->wire_geom = geom;
  }
}

void overlay_wireframe_draw(OverlayData *data)
{
  OverlayPassList *psl = data->psl;
  OverlayPrivateData *pd = data->stl->pd;

  draw_view_set_active(pd->view_wires);
  draw_draw_pass(psl->wireframe_ps);

  draw_view_set_active(NULL);
}

void overlay_wireframe_in_front_draw(OVERLAY_Data *data)
{
  OverlayPassList *psl = data->psl;
  OverlayPrivateData *pd = data->stl->pd;

  if (psl->wireframe_xray_ps) {
    draw_view_set_active(pd->view_wires);
    draw_draw_pass(psl->wireframe_xray_ps);

    draw_view_set_active(NULL);
  }
}
