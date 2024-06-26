#include "draw_render.h"

#include "ed_view3d.h"

#include "types_mesh.h"

#include "dune_customdata.h"
#include "dune_editmesh.h"
#include "dune_object.h"

#include "draw_cache_impl.h"
#include "draw_manager_text.h"

#include "overlay_private.h"

#define OVERLAY_EDIT_TEXT \
  (V3D_OVERLAY_EDIT_EDGE_LEN | V3D_OVERLAY_EDIT_FACE_AREA | V3D_OVERLAY_EDIT_FACE_ANG | \
   V3D_OVERLAY_EDIT_EDGE_ANG | V3D_OVERLAY_EDIT_INDICES)

void overlay_edit_mesh_init(OverlayData *vedata)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  const DrawCtxState *draw_ctx = draw_ctx_state_get();

  pd->edit_mesh.do_zbufclip = XRAY_FLAG_ENABLED(draw_ctx->v3d);

  /* Create view with depth offset */
  DrawView *default_view = (DrawView *)draw_view_default_get();
  pd->view_edit_faces = default_view;
  pd->view_edit_faces_cage = draw_view_create_with_zoffset(default_view, draw_ctx->rv3d, 0.5f);
  pd->view_edit_edges = draw_view_create_with_zoffset(default_view, draw_ctx->rv3d, 1.0f);
  pd->view_edit_verts = draw_view_create_with_zoffset(default_view, draw_ctx->rv3d, 1.5f);
}

void overlay_edit_mesh_cache_init(OverlayData *vedata)
{
  OverlayTextureList *txl = vedata->txl;
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;
  OverlayShadingData *shdata = &pd->shdata;
  DrawShadingGroup *grp = NULL;
  GPUShader *sh = NULL;
  DrawState state = 0;

  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();

  const DRWContextState *draw_ctx = DRW_context_state_get();
  ToolSettings *tsettings = draw_ctx->scene->toolsettings;
  View3D *v3d = draw_ctx->v3d;
  bool select_vert = pd->edit_mesh.select_vert = (tsettings->selectmode & SCE_SELECT_VERTEX) != 0;
  bool select_face = pd->edit_mesh.select_face = (tsettings->selectmode & SCE_SELECT_FACE) != 0;
  bool select_edge = pd->edit_mesh.select_edge = (tsettings->selectmode & SCE_SELECT_EDGE) != 0;

  bool do_occlude_wire = (v3d->overlay.edit_flag & V3D_OVERLAY_EDIT_OCCLUDE_WIRE) != 0;
  bool show_face_dots = (v3d->overlay.edit_flag & V3D_OVERLAY_EDIT_FACE_DOT) != 0 ||
                        pd->edit_mesh.do_zbufclip;

  pd->edit_mesh.do_faces = true;
  pd->edit_mesh.do_edges = true;

  int *mask = shdata->data_mask;
  mask[0] = 0xFF; /* Face Flag */
  mask[1] = 0xFF; /* Edge Flag */

  const int flag = pd->edit_mesh.flag = v3d->overlay.edit_flag;

  SET_FLAG_FROM_TEST(mask[0], flag & V3D_OVERLAY_EDIT_FACES, VFLAG_FACE_SELECTED);
  SET_FLAG_FROM_TEST(mask[0], flag & V3D_OVERLAY_EDIT_FREESTYLE_FACE, VFLAG_FACE_FREESTYLE);
  SET_FLAG_FROM_TEST(mask[1], flag & V3D_OVERLAY_EDIT_FREESTYLE_EDGE, VFLAG_EDGE_FREESTYLE);
  SET_FLAG_FROM_TEST(mask[1], flag & V3D_OVERLAY_EDIT_SEAMS, VFLAG_EDGE_SEAM);
  SET_FLAG_FROM_TEST(mask[1], flag & V3D_OVERLAY_EDIT_SHARP, VFLAG_EDGE_SHARP);
  SET_FLAG_FROM_TEST(mask[2], flag & V3D_OVERLAY_EDIT_CREASES, 0xFF);
  SET_FLAG_FROM_TEST(mask[3], flag & V3D_OVERLAY_EDIT_BWEIGHTS, 0xFF);

  if ((flag & V3D_OVERLAY_EDIT_FACES) == 0) {
    pd->edit_mesh.do_faces = false;
  }
  if ((flag & V3D_OVERLAY_EDIT_EDGES) == 0) {
    if ((tsettings->selectmode & SCE_SELECT_EDGE) == 0) {
      if ((v3d->shading.type < OB_SOLID) || (v3d->shading.flag & V3D_SHADING_XRAY)) {
        /* Special case, when drawing wire, draw edges, see: T67637. */
      }
      else {
        pd->edit_mesh.do_edges = false;
      }
    }
  }

  float backwire_opacity = (pd->edit_mesh.do_zbufclip) ? v3d->overlay.backwire_opacity : 1.0f;
  float face_alpha = (do_occlude_wire || !pd->edit_mesh.do_faces) ? 0.0f : 1.0f;
  GPUTexture **depth_tex = (pd->edit_mesh.do_zbufclip) ? &dtxl->depth : &txl->dummy_depth_tx;

  /* Run Twice for in-front passes. */
  for (int i = 0; i < 2; i++) {
    /* Complementary Depth Pass */
    state = DRW_STATE_WRITE_DEPTH | DRAW_STATE_DEPTH_LESS_EQUAL | DRAW_STATE_CULL_BACK;
    DRAW_PASS_CREATE(psl->edit_mesh_depth_ps[i], state | pd->clipping_state);

    sh = overlay_shader_depth_only();
    pd->edit_mesh_depth_grp[i] = draw_shgroup_create(sh, psl->edit_mesh_depth_ps[i]);
  }
  {
    /* Normals */
    state = DRAW_STATE_WRITE_DEPTH | DRAW_STATE_WRITE_COLOR | DRAW_STATE_DEPTH_LESS_EQUAL |
            (pd->edit_mesh.do_zbufclip ? DRAW_STATE_BLEND_ALPHA : 0);
    DRAW_PASS_CREATE(psl->edit_mesh_normals_ps, state | pd->clipping_state);

    sh = overlay_shader_edit_mesh_normal();
    pd->edit_mesh_normals_grp = grp = draw_shgroup_create(sh, psl->edit_mesh_normals_ps);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    draw_shgroup_uniform_float_copy(grp, "normalSize", v3d->overlay.normals_length);
    draw_shgroup_uniform_float_copy(grp, "alpha", backwire_opacity);
    draw_shgroup_uniform_texture_ref(grp, "depthTex", depth_tex);
    draw_shgroup_uniform_bool_copy(grp,
                                  "isConstantScreenSizeNormals",
                                  (flag & V3D_OVERLAY_EDIT_CONSTANT_SCREEN_SIZE_NORMALS) != 0);
    draw_shgroup_uniform_float_copy(
        grp, "normalScreenSize", v3d->overlay.normals_constant_screen_size);
  }
  {
    /* Mesh Analysis Pass */
    state = DRAW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_BLEND_ALPHA;
    DRAW_PASS_CREATE(psl->edit_mesh_analysis_ps, state | pd->clipping_state);

    sh = overlay_shader_edit_mesh_analysis();
    pd->edit_mesh_analysis_grp = grp = DRW_shgroup_create(sh, psl->edit_mesh_analysis_ps);
    draw_shgroup_uniform_texture(grp, "weightTex", G_draw.weight_ramp);
  }
  /* Run Twice for in-front passes. */
  for (int i = 0; i < 2; i++) {
    GPUShader *edge_sh = overlay_shader_edit_mesh_edge(!select_vert);
    GPUShader *face_sh = overlay_shader_edit_mesh_face();
    const bool do_zbufclip = (i == 0 && pd->edit_mesh.do_zbufclip);
    DrawState state_common = DRAW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL |
                            DRAW_STATE_BLEND_ALPHA;
    /* Faces */
    /* Cage geom needs an offset applied to avoid Z-fighting. */
    for (int j = 0; j < 2; j++) {
      DrawPass **edit_face_ps = (j == 0) ? &psl->edit_mesh_faces_ps[i] :
                                          &psl->edit_mesh_faces_cage_ps[i];
      DrawShadingGroup **shgrp = (j == 0) ? &pd->edit_mesh_faces_grp[i] :
                                           &pd->edit_mesh_faces_cage_grp[i];
      state = state_common;
      DRAW_PASS_CREATE(*edit_face_ps, state | pd->clipping_state);

      grp = *shgrp = DRW_shgroup_create(face_sh, *edit_face_ps);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_ivec4(grp, "dataMask", mask, 1);
      draw_shgroup_uniform_float_copy(grp, "alpha", face_alpha);
      draw_shgroup_uniform_bool_copy(grp, "selectFaces", select_face);
    }

    if (do_zbufclip) {
      state_common |= DRW_STATE_WRITE_DEPTH;
      // state_common &= ~DRW_STATE_BLEND_ALPHA;
    }

    /* Edges */
    /* Change first vertex convention to match blender loop structure. */
    state = state_common | DRAW_STATE_FIRST_VERTEX_CONVENTION;
    DRAW_PASS_CREATE(psl->edit_mesh_edges_ps[i], state | pd->clipping_state);

    grp = pd->edit_mesh_edges_grp[i] = DRW_shgroup_create(edge_sh, psl->edit_mesh_edges_ps[i]);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    draw_shgroup_uniform_ivec4(grp, "dataMask", mask, 1);
    draw_shgroup_uniform_float_copy(grp, "alpha", backwire_opacity);
    draw_shgroup_uniform_texture_ref(grp, "depthTex", depth_tex);
    draw_shgroup_uniform_bool_copy(grp, "selectEdges", pd->edit_mesh.do_edges || select_edge);

    /* Verts */
    state |= DRAW_STATE_WRITE_DEPTH;
    DRAW_PASS_CREATE(psl->edit_mesh_verts_ps[i], state | pd->clipping_state);

    if (select_vert) {
      sh = overlay_shader_edit_mesh_vert();
      grp = pd->edit_mesh_verts_grp[i] = DRW_shgroup_create(sh, psl->edit_mesh_verts_ps[i]);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_float_copy(grp, "alpha", backwire_opacity);
      draw_shgroup_uniform_texture_ref(grp, "depthTex", depth_tex);

      sh = overlay_shader_edit_mesh_skin_root();
      grp = pd->edit_mesh_skin_roots_grp[i] = draw_shgroup_create(sh, psl->edit_mesh_verts_ps[i]);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    }
    /* Face-dots */
    if (select_face && show_face_dots) {
      sh = overlay_shader_edit_mesh_facedot();
      grp = pd->edit_mesh_facedots_grp[i] = draw_shgroup_create(sh, psl->edit_mesh_verts_ps[i]);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_float_copy(grp, "alpha", backwire_opacity);
      draw_shgroup_uniform_texture_ref(grp, "depthTex", depth_tex);
      draw_shgroup_state_enable(grp, DRAW_STATE_WRITE_DEPTH);
    }
    else {
      pd->edit_mesh_facedots_grp[i] = NULL;
    }
  }
}

static void overlay_edit_mesh_add_ob_to_pass(OverlayPrivateData *pd, Object *ob, bool in_front)
{
  struct GPUBatch *geom_tris, *geom_verts, *geom_edges, *geom_fcenter, *skin_roots, *circle;
  DrawShadingGroup *vert_shgrp, *edge_shgrp, *fdot_shgrp, *face_shgrp, *skin_roots_shgrp;

  bool has_edit_mesh_cage = false;
  bool has_skin_roots = false;
  /* TODO: Should be its own function. */
  Mesh *me = (Mesh *)ob->data;
  BMEditMesh *embm = me->edit_mesh;
  if (embm) {
    Mesh *editmesh_eval_final = dune_object_get_editmesh_eval_final(ob);
    Mesh *editmesh_eval_cage = dune_object_get_editmesh_eval_cage(ob);

    has_edit_mesh_cage = editmesh_eval_cage && (editmesh_eval_cage != editmesh_eval_final);
    has_skin_roots = CustomData_get_offset(&embm->bm->vdata, CD_MVERT_SKIN) != -1;
  }

  vert_shgrp = pd->edit_mesh_verts_grp[in_front];
  edge_shgrp = pd->edit_mesh_edges_grp[in_front];
  fdot_shgrp = pd->edit_mesh_facedots_grp[in_front];
  face_shgrp = (has_edit_mesh_cage) ? pd->edit_mesh_faces_cage_grp[in_front] :
                                      pd->edit_mesh_faces_grp[in_front];
  skin_roots_shgrp = pd->edit_mesh_skin_roots_grp[in_front];

  geom_edges = draw_mesh_batch_cache_get_edit_edges(ob->data);
  geom_tris = draw_mesh_batch_cache_get_edit_triangles(ob->data);
  draw_shgroup_call_no_cull(edge_shgrp, geom_edges, ob);
  draw_shgroup_call_no_cull(face_shgrp, geom_tris, ob);

  if (pd->edit_mesh.select_vert) {
    geom_verts = draw_mesh_batch_cache_get_edit_vertices(ob->data);
    draw_shgroup_call_no_cull(vert_shgrp, geom_verts, ob);

    if (has_skin_roots) {
      circle = draw_cache_circle_get();
      skin_roots = draw_mesh_batch_cache_get_edit_skin_roots(ob->data);
      draw_shgroup_call_instances_with_attrs(skin_roots_shgrp, ob, circle, skin_roots);
    }
  }

  if (fdot_shgrp) {
    geom_fcenter = draw_mesh_batch_cache_get_edit_facedots(ob->data);
    draw_shgroup_call_no_cull(fdot_shgrp, geom_fcenter, ob);
  }
}

void overlay_edit_mesh_cache_populate(OverlayData *vedata, Object *ob)
{
  OverlayPrivateData *pd = vedata->stl->pd;
  struct GPUBatch *geom = NULL;

  bool draw_as_solid = (ob->dt > OB_WIRE);
  bool do_in_front = (ob->dtx & OB_DRAW_IN_FRONT) != 0;
  bool do_occlude_wire = (pd->edit_mesh.flag & V3D_OVERLAY_EDIT_OCCLUDE_WIRE) != 0;
  bool do_show_mesh_analysis = (pd->edit_mesh.flag & V3D_OVERLAY_EDIT_STATVIS) != 0;
  bool fnormals_do = (pd->edit_mesh.flag & V3D_OVERLAY_EDIT_FACE_NORMALS) != 0;
  bool vnormals_do = (pd->edit_mesh.flag & V3D_OVERLAY_EDIT_VERT_NORMALS) != 0;
  bool lnormals_do = (pd->edit_mesh.flag & V3D_OVERLAY_EDIT_LOOP_NORMALS) != 0;

  if (do_show_mesh_analysis && !pd->xray_enabled) {
    geom = draw_cache_mesh_surface_mesh_analysis_get(ob);
    if (geom) {
      draw_shgroup_call_no_cull(pd->edit_mesh_analysis_grp, geom, ob);
    }
  }

  if (do_occlude_wire || (do_in_front && draw_as_solid)) {
    geom = draw_cache_mesh_surface_get(ob);
    draw_shgroup_call_no_cull(pd->edit_mesh_depth_grp[do_in_front], geom, ob);
  }

  if (vnormals_do || lnormals_do || fnormals_do) {
    struct GPUBatch *normal_geom = draw_cache_normal_arrow_get();
    if (vnormals_do) {
      geom = draw_mesh_batch_cache_get_edit_vnors(ob->data);
      draw_shgroup_call_instances_with_attrs(pd->edit_mesh_normals_grp, ob, normal_geom, geom);
    }
    if (lnormals_do) {
      geom = draw_mesh_batch_cache_get_edit_lnors(ob->data);
      draw_shgroup_call_instances_with_attrs(pd->edit_mesh_normals_grp, ob, normal_geom, geom);
    }
    if (fnormals_do) {
      geom = draw_mesh_batch_cache_get_edit_facedots(ob->data);
      draw_shgroup_call_instances_with_attrs(pd->edit_mesh_normals_grp, ob, normal_geom, geom);
    }
  }

  if (pd->edit_mesh.do_zbufclip) {
    overlay_edit_mesh_add_ob_to_pass(pd, ob, false);
  }
  else {
    overlay_edit_mesh_add_ob_to_pass(pd, ob, do_in_front);
  }

  if (DRW_state_show_text() && (pd->edit_mesh.flag & OVERLAY_EDIT_TEXT)) {
    const DrawCtxtState *draw_ctx = draw_context_state_get();
    draw_text_edit_mesh_measure_stats(draw_ctx->region, draw_ctx->v3d, ob, &draw_ctx->scene->unit);
  }
}

static void overlay_edit_mesh_draw_components(OVERLAY_PassList *psl,
                                              OVERLAY_PrivateData *pd,
                                              bool in_front)
{
  DRW_view_set_active(pd->view_edit_faces);
  DRW_draw_pass(psl->edit_mesh_faces_ps[in_front]);

  DRW_view_set_active(pd->view_edit_faces_cage);
  DRW_draw_pass(psl->edit_mesh_faces_cage_ps[in_front]);

  DRW_view_set_active(pd->view_edit_edges);
  DRW_draw_pass(psl->edit_mesh_edges_ps[in_front]);

  DRW_view_set_active(pd->view_edit_verts);
  DRW_draw_pass(psl->edit_mesh_verts_ps[in_front]);
}

void OVERLAY_edit_mesh_draw(OVERLAY_Data *vedata)
{
  OVERLAY_PassList *psl = vedata->psl;
  OVERLAY_PrivateData *pd = vedata->stl->pd;
  OVERLAY_FramebufferList *fbl = vedata->fbl;

  if (DRW_state_is_fbo()) {
    GPU_framebuffer_bind(fbl->overlay_default_fb);
  }

  DRW_draw_pass(psl->edit_mesh_analysis_ps);

  DRW_draw_pass(psl->edit_mesh_depth_ps[NOT_IN_FRONT]);

  if (pd->edit_mesh.do_zbufclip) {
    DRW_draw_pass(psl->edit_mesh_depth_ps[IN_FRONT]);

    /* Render face-fill. */
    DRW_view_set_active(pd->view_edit_faces);
    DRW_draw_pass(psl->edit_mesh_faces_ps[NOT_IN_FRONT]);

    draw_view_set_active(pd->view_edit_faces_cage);
    draw_draw_pass(psl->edit_mesh_faces_cage_ps[NOT_IN_FRONT]);

    draw_view_set_active(NULL);

    gpu_framebuffer_bind(fbl->overlay_in_front_fb);
    gpu_framebuffer_clear_depth(fbl->overlay_in_front_fb, 1.0f);
    draw_draw_pass(psl->edit_mesh_normals_ps);

    draw_view_set_active(pd->view_edit_edges);
    draw_draw_pass(psl->edit_mesh_edges_ps[NOT_IN_FRONT]);

    draw_view_set_active(pd->view_edit_verts);
    draw_draw_pass(psl->edit_mesh_verts_ps[NOT_IN_FRONT]);
  }
  else {
    draw_draw_pass(psl->edit_mesh_normals_ps);
    overlay_edit_mesh_draw_components(psl, pd, false);

    if (draw_state_is_fbo()) {
      gpu_framebuffer_bind(fbl->overlay_in_front_fb);
    }

    if (!draw_pass_is_empty(psl->edit_mesh_depth_ps[IN_FRONT])) {
      draw_view_set_active(NULL);
      DRW_draw_pass(psl->edit_mesh_depth_ps[IN_FRONT]);
    }

    overlay_edit_mesh_draw_components(psl, pd, true);
  }
}
