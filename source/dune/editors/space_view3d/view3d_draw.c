#include <math.h>

#include "lib_jitter_2d.h"
#include "lib_listbase.h"
#include "lib_math.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_string_utils.h"
#include "lib_threads.h"

#include "dune_armature.h"
#include "dune_camera.h"
#include "dune_collection.h"
#include "dune_cxt.h"
#include "dune_customdata.h"
#include "dune_global.h"
#include "dune_img.h"
#include "dune_key.h"
#include "dune_layer.h"
#include "dune_main.h"
#include "dune_obj.h"
#include "dune_paint.h"
#include "dune_scene.h"
#include "dune_studiolight.h"
#include "dune_unit.h"

#include "font.h"

#include "lang.h"

#include "types_armature.h"
#include "types_brush.h"
#include "types_camera.h"
#include "types_key.h"
#include "types_mesh.h"
#include "types_obj.h"
#include "types_view3d.h"
#include "types_wm.h"

#include "draw_engine.h"
#include "draw_sel_buffer.h"

#include "ed_pen.h"
#include "ed_info.h"
#include "ed_keyframing.h"
#include "ed_screen.h"
#include "ed_screen_types.h"
#include "ed_transform.h"
#include "ed_view3d_offscreen.h"

#include "graph_query.h"

#include "gpu_batch.h"
#include "gpu_batch_presets.h"
#include "gpu_framebuffer.h"
#include "gpu_immediate.h"
#include "gpu_immediate_util.h"
#include "gpu_material.h"
#include "gpu_matrix.h"
#include "gpu_state.h"
#include "gpu_viewport.h"

#include "mem_guardedalloc.h"

#include "ui.h"
#include "ui_resources.h"

#include "render_engine.h"

#include "win_api.h"
#include "win_types.h"

#include "api_access.h"

#include "imbuf.h"
#include "imbuf_types.h"

#include "view3d_intern.h" /* own include */

#define M_GOLDEN_RATIO_CONJUGATE 0.618033988749895f

#define VIEW3D_OVERLAY_LINEHEIGHT (0.9f * U.widget_unit)

/* General Fns */
void ed_view3d_update_viewmat(Graph *graph,
                              const Scene *scene,
                              View3D *v3d,
                              ARgn *rhn,
                              const float viewmat[4][4],
                              const float winmat[4][4],
                              const rcti *rect,
                              bool offscreen)
{
  RgnView3D *rv3d = rgn->rgndata;

  /* setup win matrices */
  if (winmat) {
    copy_m4_m4(rv3d->winmat, winmat);
  }
  else {
    view3d_winmatrix_set(graph, rgn, v3d, rect);
  }

  /* setup view matrix */
  if (viewmat) {
    copy_m4_m4(rv3d->viewmat, viewmat);
  }
  else {
    float rect_scale[2];
    if (rect) {
      rect_scale[0] = (float)lib_rcti_size_x(rect) / (float)rgn->winx;
      rect_scale[1] = (float)lib_rcti_size_y(rect) / (float)rgn->winy;
    }
    /* calls dune_obj_where_is_calc for camera... */
    view3d_viewmatrix_set(graph, scene, v3d, rv3d, rect ? rect_scale : NULL);
  }
  /* update utility matrices */
  mul_m4_m4m4(rv3d->persmat, rv3d->winmat, rv3d->viewmat);
  invert_m4_m4(rv3d->persinv, rv3d->persmat);
  invert_m4_m4(rv3d->viewinv, rv3d->viewmat);

  /* calculate GLSL view dependent values */
  /* store win coordinates scaling/offset */
  if (!offscreen && rv3d->persp == RV3D_CAMOB && v3d->camera) {
    rctf cameraborder;
    ed_view3d_calc_camera_border(scene, graph, rgn, v3d, rv3d, &cameraborder, false);
    rv3d->viewcamtexcofac[0] = (float)rgn->winx / lib_rctf_size_x(&cameraborder);
    rv3d->viewcamtexcofac[1] = (float)rgn->winy / lib_rctf_size_y(&cameraborder);

    rv3d->viewcamtexcofac[2] = -rv3d->viewcamtexcofac[0] * cameraborder.xmin / (float)region->winx;
    rv3d->viewcamtexcofac[3] = -rv3d->viewcamtexcofac[1] * cameraborder.ymin / (float)region->winy;
  }
  else {
    rv3d->viewcamtexcofac[0] = rv3d->viewcamtexcofac[1] = 1.0f;
    rv3d->viewcamtexcofac[2] = rv3d->viewcamtexcofac[3] = 0.0f;
  }

  /* Calculate pixel-size factor once, this is used for lights and obj-centers. */
  {
    /* '1.0f / len_v3(v1)'  replaced  'len_v3(rv3d->viewmat[0])'
     * because of float point precision problems at large values T23908. */
    float v1[3], v2[3];
    float len_px, len_sc;

    v1[0] = rv3d->persmat[0][0];
    v1[1] = rv3d->persmat[1][0];
    v1[2] = rv3d->persmat[2][0];

    v2[0] = rv3d->persmat[0][1];
    v2[1] = rv3d->persmat[1][1];
    v2[2] = rv3d->persmat[2][1];

    len_px = 2.0f / sqrtf(min_ff(len_squared_v3(v1), len_squared_v3(v2)));

    if (rect) {
      len_sc = (float)max_ii(lib_rcti_size_x(rect), lib_rcti_size_y(rect));
    }
    else {
      len_sc = (float)MAX2(rgn->winx, rgn->winy);
    }

    rv3d->pixsize = len_px / len_sc;
  }
}

static void view3d_main_rgn_setup_view(Graph *graph,
                                          Scene *scene,
                                          View3D *v3d,
                                          Rgn *rgn,
                                          const float viewmat[4][4],
                                          const float winmat[4][4],
                                          const rcti *rect)
{
  RgnView3D *rv3d = rgn->rgndata;

  ed_view3d_update_viewmat(graph, scene, v3d, rg, viewmat, winmat, rect, false);

  /* set for opengl */
  gpu_matrix_projection_set(rv3d->winmat);
  gpu_matrix_set(rv3d->viewmat);
}

static void view3d_main_rgn_setup_offscreen(Graph *graph,
                                            const Scene *scene,
                                            View3D *v3d,
                                            ARgn *rgn,
                                            const float viewmat[4][4],
                                            const float winmat[4][4])
{
  RgnView3D *rv3d = rgn->rgndata;
  ed_view3d_update_viewmat(graph, scene, v3d, rgn, viewmat, winmat, NULL, true);

  /* set for opengl */
  gpu_matrix_projection_set(rv3d->winmat);
  gpu_matrix_set(rv3d->viewmat);
}

static bool view3d_stereo3d_active(Win *win,
                                   const Scene *scene,
                                   View3D *v3d,
                                   RgnView3D *rv3d)
{
  if ((scene->r.scemode & R_MULTIVIEW) == 0) {
    return false;
  }

  if ((v3d->camera == NULL) || (v3d->camera->type != OB_CAMERA) || rv3d->persp != RV3D_CAMOB) {
    return false;
  }

  switch (v3d->stereo3d_camera) {
    case STEREO_MONO_ID:
      return false;
      break;
    case STEREO_3D_ID:
      /* win will be NULL when calling this from the selection or draw loop. */
      if ((win == NULL) || (won_stereo3d_enabled(win, true) == false)) {
        return false;
      }
      if (((scene->r.views_format & SCE_VIEWS_FORMAT_MULTIVIEW) != 0) &&
          !dune_scene_multiview_is_stereo3d(&scene->r)) {
        return false;
      }
      break;
    /* We always need the stereo calculation for left and right cameras. */
    case STEREO_LEFT_ID:
    case STEREO_RIGHT_ID:
    default:
      break;
  }
  return true;
}

/* setup the view and win matrices for the multiview cameras
 *
 * unlike view3d_stereo3d_setup_offscreen, when view3d_stereo3d_setup is called
 * we have no winmatrix (i.e., projection matrix) defined at that time.
 * Since the camera and the camera shift are needed for the winmat calculation
 * we do a small hack to replace it temporarily so we don't need to change the
 * view3d)main_rgn_setup_view() code to account for that.
 */
static void view3d_stereo3d_setup(
    Graph *graph, Scene *scene, View3D *v3d, ARgn *rgn, const rcti *rect)
{
  bool is_left;
  const char *names[2] = {STEREO_LEFT_NAME, STEREO_RIGHT_NAME};
  const char *viewname;

  /* show only left or right camera */
  if (v3d->stereo3d_camera != STEREO_3D_ID) {
    v3d->multiview_eye = v3d->stereo3d_camera;
  }

  is_left = v3d->multiview_eye == STEREO_LEFT_ID;
  viewname = names[is_left ? STEREO_LEFT_ID : STEREO_RIGHT_ID];

  /* update the viewport matrices with the new camera */
  if (scene->r.views_format == SCE_VIEWS_FORMAT_STEREO_3D) {
    Camera *data, *data_eval;
    float viewmat[4][4];
    float shiftx;

    data = (Camera *)v3d->camera->data;
    data_eval = (Camera *)graph_get_eval_id(graph, &data->id);

    shiftx = data_eval->shiftx;

    lib_thread_lock(LOCK_VIEW3D);
    data_eval->shiftx = dune_camera_multiview_shift_x(&scene->r, v3d->camera, viewname);

    dune_camera_multiview_view_matrix(&scene->r, v3d->camera, is_left, viewmat);
    view3d_main_rgn_setup_view(graph, scene, v3d, rgn, viewmat, NULL, rect);

    data_eval->shiftx = shiftx;
    lib_thread_unlock(LOCK_VIEW3D);
  }
  else { /* SCE_VIEWS_FORMAT_MULTIVIEW */
    float viewmat[4][4];
    Obj *view_ob = v3d->camera;
    Obj *camera = dune_camera_multiview_render(scene, v3d->camera, viewname);

    lib_thread_lock(LOCK_VIEW3D);
    v3d->camera = camera;

    dune_camera_multiview_view_matrix(&scene->r, camera, false, viewmat);
    view3d_main_rgn_setup_view(graph, scene, v3d, rgn, viewmat, NULL, rect);

    v3d->camera = view_ob;
    lib_thread_unlock(LOCK_VIEW3D);
  }
}

#ifdef WITH_XR_OPENXR
static void view3d_xr_mirror_setup(const WinMngr *wm,
                                   Graph graph,
                                   Scene *scene,
                                   View3D *v3d,
                                   ARgn *rgn,
                                   const rcti *rect)
{
  RgnView3D *rv3d = rgn->rgndata;
  float viewmat[4][4];
  const float lens_old = v3d->lens;

  if (!win_xr_session_state_viewer_pose_matrix_info_get(&win->xr, viewmat, &v3d->lens)) {
    /* Can't get info from XR session, use fallback values. */
    copy_m4_m4(viewmat, rv3d->viewmat);
    v3d->lens = lens_old;
  }
  view3d_main_rgn_setup_view(graph, scene, v3d, rgn, viewmat, NULL, rect);

  /* Set draw flags. */
  SET_FLAG_FROM_TEST(v3d->flag2,
                     (win->xr.session_settings.draw_flags & V3D_OFSDRAW_XR_SHOW_CONTROLLERS) != 0,
                     V3D_XR_SHOW_CONTROLLERS);
  SET_FLAG_FROM_TEST(v3d->flag2,
                     (win->xr.session_settings.draw_flags & V3D_OFSDRAW_XR_SHOW_CUSTOM_OVERLAYS) !=
                         0,
                     V3D_XR_SHOW_CUSTOM_OVERLAYS);
  /* Hide navigation gizmo since it gets distorted if the view matrix has a scale factor. */
  v3d->gizmo_flag |= V3D_GIZMO_HIDE_NAV;

  /* Reset overridden View3D data. */
  v3d->lens = lens_old;
}
#endif /* WITH_XR_OPENXR */

void ed_view3d_draw_setup_view(const WinMngr *wm,
                               Win *win,
                               Graph *graph,
                               Scene *scene,
                               ARgn *rgn,
                               View3D *v3d,
                               const float viewmat[4][4],
                               const float winmat[4][4],
                               const rcti *rect)
{
  RgnView3D *rv3d = rgn->rgndata;

#ifdef WITH_XR_OPENXR
  /* Setup the view matrix. */
  if (ed_view3d_is_rgn_xr_mirror_active(wm, v3d, rgn)) {
    view3d_xr_mirror_setup(wm, graph, scene, v3d, rgn, rect);
  }
  else
#endif
      if (view3d_stereo3d_active(win, scene, v3d, rv3d)) {
    view3d_stereo3d_setup(graph, scene, v3d, rgn, rect);
  }
  else {
    view3d_main_rgn_setup_view(graph, scene, v3d, rgn, viewmat, winmat, rect);
  }

#ifndef WITH_XR_OPENXR
  UNUSED_VARS(wm);
#endif
}


static void view3d_camera_border(const Scene *scene,
                                 struct Graph *graph,
                                 const ARgn *rgn,
                                 const View3D *v3d,
                                 const RgnView3D *rv3d,
                                 rctf *r_viewborder,
                                 const bool no_shift,
                                 const bool no_zoom)
{
  CameraParams params;
  rctf rect_view, rect_camera;
  Obj *camera_eval = graph_get_eval_obj(graph, v3d->camera);

  /* get viewport viewplane */
  dune_camera_params_init(&params);
  dune_camera_params_from_view3d(&params, graph, v3d, rv3d);
  if (no_zoom) {
    params.zoom = 1.0f;
  }
  dune_camera_params_compute_viewplane(&params, rgn->winx, rgn->winy, 1.0f, 1.0f);
  rect_view = params.viewplane;

  /* get camera viewplane */
  dune_camera_params_init(&params);
  /* fallback for non camera objs */
  params.clip_start = v3d->clip_start;
  params.clip_end = v3d->clip_end;
  dune_camera_params_from_object(&params, camera_eval);
  if (no_shift) {
    params.shiftx = 0.0f;
    params.shifty = 0.0f;
  }
  dune_camera_params_compute_viewplane(
      &params, scene->r.xsch, scene->r.ysch, scene->r.xasp, scene->r.yasp);
  rect_camera = params.viewplane;

  /* get camera border within viewport */
  r_viewborder->xmin = ((rect_camera.xmin - rect_view.xmin) / lib_rctf_size_x(&rect_view)) *
                       rgn->winx;
  r_viewborder->xmax = ((rect_camera.xmax - rect_view.xmin) / lib_rctf_size_x(&rect_view)) *
                       rgn->winx;
  r_viewborder->ymin = ((rect_camera.ymin - rect_view.ymin) / lib_rctf_size_y(&rect_view)) *
                       rgn->winy;
  r_viewborder->ymax = ((rect_camera.ymax - rect_view.ymin) / lib_rctf_size_y(&rect_view)) *
                       rgn->winy;
}

void ed_view3d_calc_camera_border_size(const Scene *scene,
                                       Graph *graph,
                                       const ARgn *rgn,
                                       const View3D *v3d,
                                       const RgnView3D *rv3d,
                                       float r_size[2])
{
  rctf viewborder;

  view3d_camera_border(scene, graph, rgn, v3d, rv3d, &viewborder, true, true);
  r_size[0] = lib_rctf_size_x(&viewborder);
  r_size[1] = lib_rctf_size_y(&viewborder);
}

void ed_view3d_calc_camera_border(const Scene *scene,
                                  Graph *graph,
                                  const ARgn *rgn,
                                  const View3D *v3d,
                                  const RgnView3D *rv3d,
                                  rctf *r_viewborder,
                                  const bool no_shift)
{
  view3d_camera_border(scene, graph, rgn, v3d, rv3d, r_viewborder, no_shift, false);
}

static void drawviewborder_grid3(uint shdr_pos, float x1, float x2, float y1, float y2, float fac)
{
  float x3, y3, x4, y4;

  x3 = x1 + fac * (x2 - x1);
  y3 = y1 + fac * (y2 - y1);
  x4 = x1 + (1.0f - fac) * (x2 - x1);
  y4 = y1 + (1.0f - fac) * (y2 - y1);

  immBegin(GPU_PRIM_LINES, 8);

  immVertex2f(shdr_pos, x1, y3);
  immVertex2f(shdr_pos, x2, y3);

  immVertex2f(shdr_pos, x1, y4);
  immVertex2f(shdr_pos, x2, y4);

  immVertex2f(shdr_pos, x3, y1);
  immVertex2f(shdr_pos, x3, y2);

  immVertex2f(shdr_pos, x4, y1);
  immVertex2f(shdr_pos, x4, y2);

  immEnd();
}

/* harmonious triangle */
static void drawviewborder_triangle(
    uint shdr_pos, float x1, float x2, float y1, float y2, const char golden, const char dir)
{
  float ofs;
  float w = x2 - x1;
  float h = y2 - y1;

  immBegin(GPU_PRIM_LINES, 6);

  if (w > h) {
    if (golden) {
      ofs = w * (1.0f - M_GOLDEN_RATIO_CONJUGATE);
    }
    else {
      ofs = h * (h / w);
    }
    if (dir == 'B') {
      SWAP(float, y1, y2);
    }

    immVertex2f(shdr_pos, x1, y1);
    immVertex2f(shdr_pos, x2, y2);

    immVertex2f(shdr_pos, x2, y1);
    immVertex2f(shdr_pos, x1 + (w - ofs), y2);

    immVertex2f(shdr_pos, x1, y2);
    immVertex2f(shdr_pos, x1 + ofs, y1);
  }
  else {
    if (golden) {
      ofs = h * (1.0f - M_GOLDEN_RATIO_CONJUGATE);
    }
    else {
      ofs = w * (w / h);
    }
    if (dir == 'B') {
      SWAP(float, x1, x2);
    }

    immVertex2f(shdr_pos, x1, y1);
    immVertex2f(shdr_pos, x2, y2);

    immVertex2f(shdr_pos, x2, y1);
    immVertex2f(shdr_pos, x1, y1 + ofs);

    immVertex2f(shdr_pos, x1, y2);
    immVertex2f(shdr_pos, x2, y1 + (h - ofs));
  }

  immEnd();
}

static void drawviewborder(Scene *scene, Graph *graph, ARgn *rgn, View3D *v3d)
{
  float x1, x2, y1, y2;
  float x1i, x2i, y1i, y2i;

  rctf viewborder;
  Camera *ca = NULL;
  RgnView3D *rv3d = rgn->rgndata;

  if (v3d->camera == NULL) {
    return;
  }
  if (v3d->camera->type == OB_CAMERA) {
    ca = v3d->camera->data;
  }

  ed_view3d_calc_camera_border(scene, graph, rgn, v3d, rv3d, &viewborder, false);
  /* the offsets */
  x1 = viewborder.xmin;
  y1 = viewborder.ymin;
  x2 = viewborder.xmax;
  y2 = viewborder.ymax;

  gpu_line_width(1.0f);

  /* apply offsets so the real 3D camera shows through */

  /* bad code wo this extra
   * 0.0001 on the lower left the 2D border can
   * obscures the 3D camera border */
  /* With VIEW3D_CAMERA_BORDER_HACK defined this error isn't noticeable
   * keep it here in case we need to remove the workaround */
  x1i = (int)(x1 - 1.0001f);
  y1i = (int)(y1 - 1.0001f);
  x2i = (int)(x2 + (1.0f - 0.0001f));
  y2i = (int)(y2 + (1.0f - 0.0001f));

  uint shdr_pos = gpu_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  /* First, solid lines. */
  {
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);

    /* passepartout, specified in camera edit btns */
    if (ca && (ca->flag & CAM_SHOWPASSEPARTOUT) && ca->passepartalpha > 0.000001f) {
      const float winx = (rgn->winx + 1);
      const float winy = (rgn->winy + 1);

      float alpha = 1.0f;

      if (ca->passepartalpha != 1.0f) {
        gpu_blend(GPU_BLEND_ALPHA);
        alpha = ca->passepartalpha;
      }

      immUniformColor4f(0.0f, 0.0f, 0.0f, alpha);

      if (x1i > 0.0f) {
        immRectf(shdr_pos, 0.0f, winy, x1i, 0.0f);
      }
      if (x2i < winx) {
        immRectf(shdr_pos, x2i, winy, winx, 0.0f);
      }
      if (y2i < winy) {
        immRectf(shdr_pos, x1i, winy, x2i, y2i);
      }
      if (y2i > 0.0f) {
        immRectf(shdr_pos, x1i, y1i, x2i, 0.0f);
      }

      gpu_blend(GPU_BLEND_NONE);
      immUniformThemeColor3(TH_BACK);
      imm_draw_box_wire_2d(shdr_pos, x1i, y1i, x2i, y2i);
    }

#ifdef VIEW3D_CAMERA_BORDER_HACK
    if (view3d_camera_border_hack_test == true) {
      immUniformColor3ubv(view3d_camera_border_hack_col);
      imm_draw_box_wire_2d(shdr_pos, x1i + 1, y1i + 1, x2i - 1, y2i - 1);
      view3d_camera_border_hack_test = false;
    }
#endif

    immUnbindProgram();
  }

  /* When overlays are disabled, only show camera outline & passepartout. */
  if (v3d->flag2 & V3D_HIDE_OVERLAYS) {
    return;
  }

  /* And now, the dashed lines! */
  immBindBuiltinProgram(GPU_SHADER_2D_LINE_DASHED_UNIFORM_COLOR);

  {
    float viewport_size[4];
    gpu_viewport_size_get_f(viewport_size);
    immUniform2f("viewport_size", viewport_size[2], viewport_size[3]);

    immUniform1i("colors_len", 0); /* "simple" mode */
    immUniform1f("dash_width", 6.0f);
    immUniform1f("dash_factor", 0.5f);

    /* outer line not to confuse with obj sel */
    if (v3d->flag2 & V3D_LOCK_CAMERA) {
      immUniformThemeColor(TH_REDALERT);
      imm_draw_box_wire_2d(shdr_pos, x1i - 1, y1i - 1, x2i + 1, y2i + 1);
    }

    immUniformThemeColor3(TH_VIEW_OVERLAY);
    imm_draw_box_wire_2d(shdr_pos, x1i, y1i, x2i, y2i);
  }

  /* Render Border. */
  if (scene->r.mode & R_BORDER) {
    float x3, y3, x4, y4;

    x3 = floorf(x1 + (scene->r.border.xmin * (x2 - x1))) - 1;
    y3 = floorf(y1 + (scene->r.border.ymin * (y2 - y1))) - 1;
    x4 = floorf(x1 + (scene->r.border.xmax * (x2 - x1))) + (U.pixelsize - 1);
    y4 = floorf(y1 + (scene->r.border.ymax * (y2 - y1))) + (U.pixelsize - 1);

    immUniformColor3f(1.0f, 0.25f, 0.25f);
    imm_draw_box_wire_2d(shdr_pos, x3, y3, x4, y4);
  }

  /* safety border */
  if (ca) {
    gpu_blend(GPU_BLEND_ALPHA);
    immUniformThemeColorAlpha(TH_VIEW_OVERLAY, 0.75f);

    if (ca->dtx & CAM_DTX_CENTER) {
      float x3, y3;

      x3 = x1 + 0.5f * (x2 - x1);
      y3 = y1 + 0.5f * (y2 - y1);

      immBegin(GPU_PRIM_LINES, 4);

      immVertex2f(shdr_pos, x1, y3);
      immVertex2f(shdr_pos, x2, y3);

      immVertex2f(shdr_pos, x3, y1);
      immVertex2f(shdr_pos, x3, y2);

      immEnd();
    }

    if (ca->dtx & CAM_DTX_CENTER_DIAG) {
      immBegin(GPU_PRIM_LINES, 4);

      immVertex2f(shdr_pos, x1, y1);
      immVertex2f(shdr_pos, x2, y2);

      immVertex2f(shdr_pos, x1, y2);
      immVertex2f(shdr_pos, x2, y1);

      immEnd();
    }

    if (ca->dtx & CAM_DTX_THIRDS) {
      drawviewborder_grid3(shdr_pos, x1, x2, y1, y2, 1.0f / 3.0f);
    }

    if (ca->dtx & CAM_DTX_GOLDEN) {
      drawviewborder_grid3(shdr_pos, x1, x2, y1, y2, 1.0f - M_GOLDEN_RATIO_CONJUGATE);
    }

    if (ca->dtx & CAM_DTX_GOLDEN_TRI_A) {
      drawviewborder_triangle(shdr_pos, x1, x2, y1, y2, 0, 'A');
    }

    if (ca->dtx & CAM_DTX_GOLDEN_TRI_B) {
      drawviewborder_triangle(shdr_pos, x1, x2, y1, y2, 0, 'B');
    }

    if (ca->dtx & CAM_DTX_HARMONY_TRI_A) {
      drawviewborder_triangle(shdr_pos, x1, x2, y1, y2, 1, 'A');
    }

    if (ca->dtx & CAM_DTX_HARMONY_TRI_B) {
      drawviewborder_triangle(shdr_pos, x1, x2, y1, y2, 1, 'B');
    }

    if (ca->flag & CAM_SHOW_SAFE_MARGINS) {
      ui_draw_safe_areas(shdr_pos,
                         &(const rctf){
                             .xmin = x1,
                             .xmax = x2,
                             .ymin = y1,
                             .ymax = y2,
                         },
                         scene->safe_areas.title,
                         scene->safe_areas.action);

      if (ca->flag & CAM_SHOW_SAFE_CENTER) {
        ui_draw_safe_areas(shdr_pos,
                           &(const rctf){
                               .xmin = x1,
                               .xmax = x2,
                               .ymin = y1,
                               .ymax = y2,
                           },
                           scene->safe_areas.title_center,
                           scene->safe_areas.action_center);
      }
    }

    if (ca->flag & CAM_SHOWSENSOR) {
      /* determine sensor fit, and get sensor x/y, for auto fit we
       * assume and square sensor and only use sensor_x */
      float sizex = scene->r.xsch * scene->r.xasp;
      float sizey = scene->r.ysch * scene->r.yasp;
      int sensor_fit = dune_camera_sensor_fit(ca->sensor_fit, sizex, sizey);
      float sensor_x = ca->sensor_x;
      float sensor_y = (ca->sensor_fit == CAMERA_SENSOR_FIT_AUTO) ? ca->sensor_x : ca->sensor_y;

      /* determine sensor plane */
      rctf rect;

      if (sensor_fit == CAMERA_SENSOR_FIT_HOR) {
        float sensor_scale = (x2i - x1i) / sensor_x;
        float sensor_height = sensor_scale * sensor_y;

        rect.xmin = x1i;
        rect.xmax = x2i;
        rect.ymin = (y1i + y2i) * 0.5f - sensor_height * 0.5f;
        rect.ymax = rect.ymin + sensor_height;
      }
      else {
        float sensor_scale = (y2i - y1i) / sensor_y;
        float sensor_width = sensor_scale * sensor_x;

        rect.xmin = (x1i + x2i) * 0.5f - sensor_width * 0.5f;
        rect.xmax = rect.xmin + sensor_width;
        rect.ymin = y1i;
        rect.ymax = y2i;
      }

      /* draw */
      immUniformThemeColorShadeAlpha(TH_VIEW_OVERLAY, 100, 255);

      /* TODO: Was using:
       * `ui_draw_roundbox_4fv(false, rect.xmin, rect.ymin, rect.xmax, rect.ymax, 2.0f, color);`
       * We'll probably need a new imm_draw_line_roundbox_dashed or that tho in practice the
       * 2.0f round corner effect was nearly not visible anyway. */
      imm_draw_box_wire_2d(shdr_pos, rect.xmin, rect.ymin, rect.xmax, rect.ymax);
    }

    gpu_blend(GPU_BLEND_NONE);
  }

  immUnbindProgram();
  /* end dashed lines */

  /* camera name draw in highlighted text color */
  if (ca && ((v3d->overlay.flag & V3D_OVERLAY_HIDE_TXT) == 0) && (ca->flag & CAM_SHOWNAME)) {
    ui_FontThemeColor(font_default(), TH_TXT_HI);
    font_draw_default(x1i,
                     y1i - (0.7f * U.widget_unit),
                     0.0f,
                     v3d->camera->id.name + 2,
                     sizeof(v3d->camera->id.name) - 2);
  }
}

static void drawrenderborder(ARgn *rgn, View3D *v3d)
{
  /* use the same program for everything */
  uint shdr_pos = gpu_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  gpu_line_width(1.0f);

  immBindBuiltinProgram(GPU_SHADER_2D_LINE_DASHED_UNIFORM_COLOR);

  float viewport_size[4];
  gpu_viewport_size_get_f(viewport_size);
  immUniform2f("viewport_size", viewport_size[2], viewport_size[3]);

  immUniform1i("colors_len", 0); /* "simple" mode */
  immUniform4f("color", 1.0f, 0.25f, 0.25f, 1.0f);
  immUniform1f("dash_width", 6.0f);
  immUniform1f("dash_factor", 0.5f);

  imm_draw_box_wire_2d(shdr_pos,
                       v3d->render_border.xmin * region->winx,
                       v3d->render_border.ymin * region->winy,
                       v3d->render_border.xmax * region->winx,
                       v3d->render_border.ymax * region->winy);

  immUnbindProgram();
}


/* Other Elements */
float ed_scene_grid_scale(const Scene *scene, const char **r_grid_unit)
{
  /* apply units */
  if (scene->unit.system) {
    const void *usys;
    int len;

    dune_unit_system_get(scene->unit.system, B_UNIT_LENGTH, &usys, &len);

    if (usys) {
      int i = dune_unit_base_get(usys);
      if (r_grid_unit) {
        *r_grid_unit = dune_unit_display_name_get(usys, i);
      }
      return (float)dune_unit_scalar_get(usys, i) / scene->unit.scale_length;
    }
  }

  return 1.0f;
}

float ed_view3d_grid_scale(const Scene *scene, View3D *v3d, const char **r_grid_unit)
{
  return v3d->grid * ed_scene_grid_scale(scene, r_grid_unit);
}

#define STEPS_LEN 8
void ed_view3d_grid_steps(const Scene *scene,
                          View3D *v3d,
                          RgnView3D *rv3d,
                          float r_grid_steps[STEPS_LEN])
{
  const void *usys;
  int len;
  dune_unit_system_get(scene->unit.system, B_UNIT_LENGTH, &usys, &len);
  float grid_scale = v3d->grid;
  lib_assert(STEPS_LEN >= len);

  if (usys) {
    if (rv3d->view == RV3D_VIEW_USER) {
      /* Skip steps */
      len = dune_unit_base_get(usys) + 1;
    }

    grid_scale /= scene->unit.scale_length;

    int i;
    for (i = 0; i < len; i++) {
      r_grid_steps[i] = (float)DUNE_unit_scalar_get(usys, len - 1 - i) * grid_scale;
    }
    for (; i < STEPS_LEN; i++) {
      /* Fill last slots */
      r_grid_steps[i] = 10.0f * r_grid_steps[i - 1];
    }
  }
  else {
    if (rv3d->view != RV3D_VIEW_USER) {
      /* Allow 3 more subdivisions. */
      grid_scale /= powf(v3d->gridsubdiv, 3);
    }
    int subdiv = 1;
    for (int i = 0;; i++) {
      r_grid_steps[i] = grid_scale * subdiv;

      if (i == STEPS_LEN - 1) {
        break;
      }
      subdiv *= v3d->gridsubdiv;
    }
  }
}

float ed_view3d_grid_view_scale(Scene *scene,
                                View3D *v3d,
                                ARgn *rgn,
                                const char **r_grid_unit)
{
  float grid_scale;
  RgnView3D *rv3d = rgn->rgndata;
  if (!rv3d->is_persp && RV3D_VIEW_IS_AXIS(rv3d->view)) {
    /* Decrease the distance between grid snap points depending on zoom. */
    float dist = 12.0f / (rgn->sizex * rv3d->winmat[0][0]);
    float grid_steps[STEPS_LEN];
    ed_view3d_grid_steps(scene, v3d, rv3d, grid_steps);
    /* Skip last item, in case the 'mid_dist' is greater than the largest unit. */
    int i;
    for (i = 0; i < ARRAY_SIZE(grid_steps) - 1; i++) {
      grid_scale = grid_steps[i];
      if (grid_scale > dist) {
        break;
      }
    }

    if (r_grid_unit) {
      const void *usys;
      int len;
      dune_unit_system_get(scene->unit.system, B_UNIT_LENGTH, &usys, &len);

      if (usys) {
        *r_grid_unit = dune_unit_display_name_get(usys, len - i - 1);
      }
    }
  }
  else {
    grid_scale = ed_view3d_grid_scale(scene, v3d, r_grid_unit);
  }

  return grid_scale;
}

#undef STEPS_LEN

static void draw_view_axis(RgnView3D *rv3d, const rcti *rect)
{
  const float k = U.rvisize * U.pixelsize; /* axis size */
  /* axis alpha offset (rvibright has range 0-10) */
  const int bright = -20 * (10 - U.rvibright);

  /* Axis center in screen coordinates.
   * - Unit size offset so small text doesn't draw outside the screen
   * - Extra X offset because of the panel expander.  */
  const float startx = rect->xmax - (k + UNIT_X * 1.5);
  const float starty = rect->ymax - (k + UNIT_Y);

  float axis_pos[3][2];
  uchar axis_col[3][4];

  int axis_order[3] = {0, 1, 2};
  axis_sort_v3(rv3d->viewinv[2], axis_order);

  for (int axis_i = 0; axis_i < 3; axis_i++) {
    int i = axis_order[axis_i];

    /* get position of each axis tip on screen */
    float vec[3] = {0.0f};
    vec[i] = 1.0f;
    mul_qt_v3(rv3d->viewquat, vec);
    axis_pos[i][0] = startx + vec[0] * k;
    axis_pos[i][1] = starty + vec[1] * k;

    /* get color of each axis */
    ui_GetThemeColorShade3ubv(TH_AXIS_X + i, bright, axis_col[i]); /* rgb */
    axis_col[i][3] = 255 * hypotf(vec[0], vec[1]);                 /* alpha */
  }

  /* draw axis lines */
  gpu_line_width(2.0f);
  gpu_line_smooth(true);
  gpu_blend(GPU_BLEND_ALPHA);

  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  uint col =gpu_vertformat_attr_add(format, "color", GPU_COMP_U8, 4, GPU_FETCH_INT_TO_FLOAT_UNIT);

  immBindBuiltinProgram(GPU_SHADER_2D_FLAT_COLOR);
  immBegin(GPU_PRIM_LINES, 6);

  for (int axis_i = 0; axis_i < 3; axis_i++) {
    int i = axis_order[axis_i];

    immAttr4ubv(col, axis_col[i]);
    immVertex2f(pos, startx, starty);
    immAttr4ubv(col, axis_col[i]);
    immVertex2fv(pos, axis_pos[i]);
  }

  immEnd();
  immUnbindProgram();
  gpu_line_smooth(false);

  /* draw axis names */
  for (int axis_i = 0; axis_i < 3; axis_i++) {
    int i = axis_order[axis_i];

    const char axis_text[2] = {'x' + i, '\0'};
    font_color4ubv(font_default(), axis_col[i]);
    font_draw_default(axis_pos[i][0] + 2, axis_pos[i][1] + 2, 0.0f, axis_text, 1);
  }
}

#ifdef WITH_INPUT_NDOF
/* draw center and axis of rotation for ongoing 3D mouse nav */
static void draw_rotation_guide(const RgnView3D *rv3d)
{
  float o[3];   /* center of rotation */
  float end[3]; /* endpoints for drawing */

  uchar color[4] = {0, 108, 255, 255}; /* bright blue so it matches device LEDs */

  negate_v3_v3(o, rv3d->ofs);

  gpu_blend(GPU_BLEND_ALPHA);
  gpu_depth_mask(false); /* Don't overwrite the Z-buffer. */

  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  uint col = gpi_vertformat_attr_add(format, "color", GPU_COMP_U8, 4, GPU_FETCH_INT_TO_FLOAT_UNIT);

  immBindBuiltinProgram(GPU_SHADER_3D_SMOOTH_COLOR);

  if (rv3d->rot_angle != 0.0f) {
    /* -- draw rotation axis -- */
    float scaled_axis[3];
    const float scale = rv3d->dist;
    mul_v3_v3fl(scaled_axis, rv3d->rot_axis, scale);

    immBegin(GPU_PRIM_LINE_STRIP, 3);
    color[3] = 0; /* more transparent toward the ends */
    immAttr4ubv(col, color);
    add_v3_v3v3(end, o, scaled_axis);
    immVertex3fv(pos, end);

#  if 0
    color[3] = 0.2f + fabsf(rv3d->rot_angle); /* modulate opacity with angle */
    /* ^^ neat idea, but angle is frame-rate dependent, so it's usually close to 0.2 */
#  endif

    color[3] = 127; /* more opaque toward the center */
    immAttr4ubv(col, color);
    immVertex3fv(pos, o);

    color[3] = 0;
    immAttr4ubv(col, color);
    sub_v3_v3v3(end, o, scaled_axis);
    immVertex3fv(pos, end);
    immEnd();

    /* draw ring around rotation center */
    {
#  define ROT_AXIS_DETAIL 13

      const float s = 0.05f * scale;
      const float step = 2.0f * (float)(M_PI / ROT_AXIS_DETAIL);

      float q[4]; /* rotate ring so it's perpendicular to axis */
      const int upright = fabsf(rv3d->rot_axis[2]) >= 0.95f;
      if (!upright) {
        const float up[3] = {0.0f, 0.0f, 1.0f};
        float vis_angle, vis_axis[3];

        cross_v3_v3v3(vis_axis, up, rv3d->rot_axis);
        vis_angle = acosf(dot_v3v3(up, rv3d->rot_axis));
        axis_angle_to_quat(q, vis_axis, vis_angle);
      }

      immBegin(GPU_PRIM_LINE_LOOP, ROT_AXIS_DETAIL);
      color[3] = 63; /* somewhat faint */
      immAttr4ubv(col, color);
      float angle = 0.0f;
      for (int i = 0; i < ROT_AXIS_DETAIL; i++, angle += step) {
        float p[3] = {s * cosf(angle), s * sinf(angle), 0.0f};

        if (!upright) {
          mul_qt_v3(q, p);
        }

        add_v3_v3(p, o);
        immVertex3fv(pos, p);
      }
      immEnd();

#  undef ROT_AXIS_DETAIL
    }

    color[3] = 255; /* solid dot */
  }
  else {
    color[3] = 127; /* see-through dot */
  }

  immUnbindProgram();

  /* draw rotation center -- */
  immBindBuiltinProgram(GPU_SHADER_3D_POINT_FIXED_SIZE_VARYING_COLOR);
  gpu_point_size(5.0f);
  immBegin(GPU_PRIM_POINTS, 1);
  immAttr4ubv(col, color);
  immVertex3fv(pos, o);
  immEnd();
  immUnbindProgram();

  gpu_blend(GPU_BLEND_NONE);
  gou_depth_mask(true);
}
#endif /* WITH_INPUT_NDOF */

/* Render and camera border */
static void view3d_draw_border(const Cxt *C, ARgn *rgn)
{
  Scene *scene = cxt_data_scene(C);
  Graph *graph = cxt_data_expect_eval_graph(C);
  RgnView3D *rv3d = rgn->rgndata;
  View3D *v3d = cxt_win_view3d(C);

  if (rv3d->persp == RV3D_CAMOB) {
    drawviewborder(scene, graph, rgn, v3d);
  }
  else if (v3d->flag2 & V3D_RENDER_BORDER) {
    drawrenderborder(rgn, v3d);
  }
}

/* Draw Txt & Info */
/* Draw Info **/
static void view3d_draw_pen(const Cxt *UNUSED(C))
{
  /* TODO: viewport. */
}

/* Viewport Name */
static const char *view3d_get_name(View3D *v3d, RgnView3D *rv3d)
{
  const char *name = NULL;

  switch (rv3d->view) {
    case RV3D_VIEW_FRONT:
      if (rv3d->persp == RV3D_ORTHO) {
        name = IFACE_("Front Orthographic");
      }
      else {
        name = IFACE_("Front Perspective");
      }
      break;
    case RV3D_VIEW_BACK:
      if (rv3d->persp == RV3D_ORTHO) {
        name = IFACE_("Back Orthographic");
      }
      else {
        name = IFACE_("Back Perspective");
      }
      break;
    case RV3D_VIEW_TOP:
      if (rv3d->persp == RV3D_ORTHO) {
        name = IFACE_("Top Orthographic");
      }
      else {
        name = IFACE_("Top Perspective");
      }
      break;
    case RV3D_VIEW_BOTTOM:
      if (rv3d->persp == RV3D_ORTHO) {
        name = IFACE_("Bottom Orthographic");
      }
      else {
        name = IFACE_("Bottom Perspective");
      }
      break;
    case RV3D_VIEW_RIGHT:
      if (rv3d->persp == RV3D_ORTHO) {
        name = IFACE_("Right Orthographic");
      }
      else {
        name = IFACE_("Right Perspective");
      }
      break;
    case RV3D_VIEW_LEFT:
      if (rv3d->persp == RV3D_ORTHO) {
        name = IFACE_("Left Orthographic");
      }
      else {
        name = IFACE_("Left Perspective");
      }
      break;

    default:
      if (rv3d->persp == RV3D_CAMOB) {
        if ((v3d->camera) && (v3d->camera->type == OB_CAMERA)) {
          Camera *cam;
          cam = v3d->camera->data;
          if (cam->type == CAM_PERSP) {
            name = IFACE_("Camera Perspective");
          }
          else if (cam->type == CAM_ORTHO) {
            name = IFACE_("Camera Orthographic");
          }
          else {
            lib_assert(cam->type == CAM_PANO);
            name = IFACE_("Camera Panoramic");
          }
        }
        else {
          name = IFACE_("Object as Camera");
        }
      }
      else {
        name = (rv3d->persp == RV3D_ORTHO) ? IFACE_("User Orthographic") :
                                             IFACE_("User Perspective");
      }
  }

  return name;
}

static void draw_viewport_name(ARgn *rgn, View3D *v3d, int xoffset, int *yoffset)
{
  RgnView3D *rv3d = rgn->rgndata;
  const char *name = view3d_get_name(v3d, rv3d);
  const char *name_array[3] = {name, NULL, NULL};
  int name_array_len = 1;
  const int font_id = font_default();

  /* 6 is the maximum size of the axis roll text. */
  /* increase size for unicode langs (Chinese in utf-8...) */
  char tmpstr[96 + 6];

  font_enable(font_id, FONT_SHADOW);
  font_shadow(font_id, 5, (const float[4]){0.0f, 0.0f, 0.0f, 1.0f});
  font_shadow_offset(font_id, 1, -1);

  if (RV3D_VIEW_IS_AXIS(rv3d->view) && (rv3d->view_axis_roll != RV3D_VIEW_AXIS_ROLL_0)) {
    const char *axis_roll;
    switch (rv3d->view_axis_roll) {
      case RV3D_VIEW_AXIS_ROLL_90:
        axis_roll = " 90\xC2\xB0";
        break;
      case RV3D_VIEW_AXIS_ROLL_180:
        axis_roll = " 180\xC2\xB0";
        break;
      default:
        axis_roll = " -90\xC2\xB0";
        break;
    }
    name_array[name_array_len++] = axis_roll;
  }

  if (v3d->localvd) {
    name_array[name_array_len++] = IFACE_(" (Local)");
  }

  /* Indicate that clipping rgn is enabled. */
  if (rv3d->rflag & RV3D_CLIPPING) {
    name_array[name_array_len++] = IFACE_(" (Clipped)");
  }

  if (name_array_len > 1) {
    lib_string_join_array(tmpstr, sizeof(tmpstr), name_array, name_array_len);
    name = tmpstr;
  }

  ui_FontThemeColor(font_default(), TH_TXT_HI);

  *yoffset -= VIEW3D_OVERLAY_LINEHEIGHT;

  font_draw_default(xoffset, *yoffset, 0.0f, name, sizeof(tmpstr));

  font_disable(font_id, FONT_SHADOW);
}

/* Draw info beside axes in top-left corner:
 * frame-number, collection, object name, bone name (if available), marker name (if available) */
static void draw_sel_name(
    Scene *scene, ViewLayer *view_layer, Obj *ob, int xoffset, int *yoffset)
{
  const int cfra = CFRA;
  const char *msg_pin = " (Pinned)";
  const char *msg_sep = " : ";

  const int font_id = font_default();

  char info[300];
  char *s = info;

  s += sprintf(s, "(%d)", cfra);

  if ((ob == NULL) || (ob->mode == OB_MODE_OBJECT)) {
    LayerCollection *layer_collection = view_layer->active_collection;
    s += sprintf(s,
                 " %s%s",
                 dune_collection_ui_name_get(layer_collection->collection),
                 (ob == NULL) ? "" : " |");
  }

  /* Info can contain:
   * - A frame `(7 + 2)`.
   * - A collection name `(MAX_NAME + 3)`.
   * - 3 obj names `(MAX_NAME)`.
   * - 2 BREAD_CRUMB_SEPARATOR(s) `(6)`.
   * - A SHAPE_KEY_PINNED marker and a trailing '\0' `(9+1)` - translated, so give some room!
   * - A marker name `(MAX_NAME + 3)`. */

  /* get name of marker on current frame (if available) */
  const char *markern = dune_scene_find_marker_name(scene, cfra);

  /* check if there is an obj */
  if (ob) {
    *s++ = ' ';
    s += lib_strcpy_rlen(s, ob->id.name + 2);

    /* name(s) to display depends on type of object */
    if (ob->type == OB_ARMATURE) {
      Armature *arm = ob->data;

      /* show name of active bone too (if possible) */
      if (arm->edbo) {
        if (arm->act_edbone) {
          s += lib_strcpy_rlen(s, msg_sep);
          s += lib_strcpy_rlen(s, arm->act_edbone->name);
        }
      }
      else if (ob->mode & OB_MODE_POSE) {
        if (arm->act_bone) {

          if (arm->act_bone->layer & arm->layer) {
            s += lib_strcpy_rlen(s, msg_sep);
            s += lib_strcpy_rlen(s, arm->act_bone->name);
          }
        }
      }
    }
    else if (ELEM(ob->type, OB_MESH, OB_LATTICE, OB_CURVES_LEGACY)) {
      /* try to display active bone and active shapekey too (if they exist) */
      if (ob->type == OB_MESH && ob->mode & OB_MODE_WEIGHT_PAINT) {
        Obj *armobj = dune_obj_pose_armature_get(ob);
        if (armobj && armobj->mode & OB_MODE_POSE) {
          Armature *arm = armobj->data;
          if (arm->act_bone) {
            if (arm->act_bone->layer & arm->layer) {
              s += lib_strcpy_rlen(s, msg_sep);
              s += lib_strcpy_rlen(s, arm->act_bone->name);
            }
          }
        }
      }

      Key *key = dune_key_from_object(ob);
      if (key) {
        KeyBlock *kb = lib_findlink(&key->block, ob->shapenr - 1);
        if (kb) {
          s += lib_strcpy_rlen(s, msg_sep);
          s += lib_strcpy_rlen(s, kb->name);
          if (ob->shapeflag & OB_SHAPE_LOCK) {
            s += lib_strcpy_rlen(s, IFACE_(msg_pin));
          }
        }
      }
    }

    /* color deps on whether there is a keyframe */
    if (id_frame_has_keyframe(
            (Id *)ob, /* dune_scene_ctime_get(scene) */ (float)cfra, ANIMFILTER_KEYS_LOCAL)) {
      ui_FontThemeColor(font_id, TH_TIME_KEYFRAME);
    }
    else if (ed_pen_has_keyframe_v3d(scene, ob, cfra)) {
      ui_FontThemeColor(font_id, TH_TIME_GP_KEYFRAME);
    }
    else {
      ui_FontThemeColor(font_id, TH_TEXT_HI);
    }
  }
  else {
    /* no obj */
    if (ed_pen_has_keyframe_v3d(scene, NULL, cfra)) {
      ui_FontThemeColor(font_id, TH_TIME_GP_KEYFRAME);
    }
    else {
      ui_FontThemeColor(font_id, TH_TXT_HI);
    }
  }

  if (markern) {
    s += sprintf(s, " <%s>", markern);
  }

  font_enable(font_id, FONT_SHADOW);
  font_shadow(font_id, 5, (const float[4]){0.0f, 0.0f, 0.0f, 1.0f});
  font_shadow_offset(font_id, 1, -1);

  *yoffset -= VIEW3D_OVERLAY_LINEHEIGHT;
  font_draw_default(xoffset, *yoffset, 0.0f, info, sizeof(info));

  font_disable(font_id, FONT_SHADOW);
}

static void draw_grid_unit_name(
    Scene *scene, ARgn *rgn, View3D *v3d, int xoffset, int *yoffset)
{
  RgnView3D *rv3d = rgn->rgndata;
  if (!rv3d->is_persp && RV3D_VIEW_IS_AXIS(rv3d->view)) {
    const char *grid_unit = NULL;
    int font_id = font_default();
    ed_view3d_grid_view_scale(scene, v3d, rgn, &grid_unit);

    if (grid_unit) {
      char numstr[32] = "";
      ui_FontThemeColor(font_id, TH_TEXT_HI);
      if (v3d->grid != 1.0f) {
        lib_snprintf(numstr, sizeof(numstr), "%s x %.4g", grid_unit, v3d->grid);
      }

      *yoffset -= VIEW3D_OVERLAY_LINEHEIGHT;
      font_enable(font_id, FONT_SHADOW);
      font_shadow(font_id, 5, (const float[4]){0.0f, 0.0f, 0.0f, 1.0f});
      font_shadow_offset(font_id, 1, -1);
      font_draw_default(xoffset, *yoffset, 0.0f, numstr[0] ? numstr : grid_unit, sizeof(numstr));
      font_disable(font_id, FONT_SHADOW);
    }
  }
}

void view3d_draw_rgn_info(const Cxt *C, ARgn *rgn)
{
  RgnView3D *rv3d = rgn->rgndata;
  View3D *v3d = cxt_win_view3d(C);
  Scene *scene = cxt_data_scene(C);
  WinMngr *wm = cxt_wm(C);
  Main *Main = cxt_data_main(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);

#ifdef WITH_INPUT_NDOF
  if ((U.ndof_flag & NDOF_SHOW_GUIDE) && ((RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ROTATION) == 0) &&
      (rv3d->persp != RV3D_CAMOB)) {
    /* TODO:_ draw something else (but not this) during fly mode */
    draw_rotation_guide(rv3d);
  }
#endif

  /* correct projection matrix */
  ed_rgn_pixelspace(rgn);

  /* local coordinate visible rect inside rgn, to accommodate overlapping ui */
  const rcti *rect = ed_rgn_visible_rect(rgn);

  view3d_draw_border(C, rgn);
  view3d_draw_pen(C);

  font_batch_draw_begin();

  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_NAV)) {
    /* pass */
  }
  else {
    switch ((eUserpref_MiniAxisType)U.mini_axis_type) {
      case USER_MINI_AXIS_TYPE_GIZMO:
        /* The gizmo handles its own drawing. */
        break;
      case USER_MINI_AXIS_TYPE_MINIMAL:
        draw_view_axis(rv3d, rect);
      case USER_MINI_AXIS_TYPE_NONE:
        break;
    }
  }

  int xoffset = rect->xmin + (0.5f * U.widget_unit);
  int yoffset = rect->ymax - (0.1f * U.widget_unit);

  if ((v3d->flag2 & V3D_HIDE_OVERLAYS) == 0 && (v3d->overlay.flag & V3D_OVERLAY_HIDE_TEXT) == 0) {
    if ((U.uiflag & USER_SHOW_FPS) && ed_screen_anim_no_scrub(wm)) {
      ed_scene_draw_fps(scene, xoffset, &yoffset);
    }
    else if (U.uiflag & USER_SHOW_VIEWPORTNAME) {
      draw_viewport_name(rgn, v3d, xoffset, &yoffset);
    }

    if (U.uiflag & USER_DRAWVIEWINFO) {
      Object *ob = OBACT(view_layer);
      draw_sel_name(scene, view_layer, ob, xoffset, &yoffset);
    }

    if (v3d->gridflag & (V3D_SHOW_FLOOR | V3D_SHOW_X | V3D_SHOW_Y | V3D_SHOW_Z)) {
      /* draw below the viewport name */
      draw_grid_unit_name(scene, region, v3d, xoffset, &yoffset);
    }

    draw_rgn_engine_info(xoffset, &yoffset, VIEW3D_OVERLAY_LINEHEIGHT);
  }

  if ((v3d->flag2 & V3D_HIDE_OVERLAYS) == 0 && (v3d->overlay.flag & V3D_OVERLAY_STATS)) {
    View3D *v3d_local = v3d->localvd ? v3d : NULL;
    ed_info_draw_stats(
        Main, scene, view_layer, v3d_local, xoffset, &yoffset, VIEW3D_OVERLAY_LINEHEIGHT);
  }

  font_batch_draw_end();
}

/* Draw Viewport Contents */
static void view3d_draw_view(const Cxt *C, ARgn *rgn)
{
  ed_view3d_draw_setup_view(cxt_wm(C),
                            cxt_win(C),
                            cxt_data_expect_eval_graph(C),
                            cxt_data_scene(C),
                            rgn,
                            cxt_win_view3d(C),
                            NULL,
                            NULL,
                            NULL);

  /* Only 100% compliant on new spec goes below */
  draw_draw_view(C);
}

RenderEngineType *ed_view3d_engine_type(const Scene *scene, int drawtype)
{
  return type;
}

void view3d_main_rgn_draw(const Cxt *C, ARgn *rgn)
{
  Main *Main = cxt_data_main(C);
  View3D *v3d = cxt_win_view3d(C);

  view3d_draw_view(C, rgn);

  draw_cache_free_old_subdiv();
  draw_cache_free_old_batches(Main);
  dune_img_free_old_gputextures(Main);
  gpu_pass_cache_garbage_collect();

  /* No depth test for drawing action zones afterwards. */
  gpu_depth_test(GPU_DEPTH_NONE);

  v3d->runtime.flag &= ~V3D_RUNTIME_DEPTHBUF_OVERRIDDEN;
  /* TODO: Clear cache? */
}

/* Off-screen Drawing */
static void view3d_stereo3d_setup_offscreen(Graph *graph,
                                            const Scene *scene,
                                            View3D *v3d,
                                            ARgn *rgn,
                                            const float winmat[4][4],
                                            const char *viewname)
{
  /* update the viewport matrices with the new camera */
  if (scene->r.views_format == SCE_VIEWS_FORMAT_STEREO_3D) {
    float viewmat[4][4];
    const bool is_left = STREQ(viewname, STEREO_LEFT_NAME);

    dune_camera_multiview_view_matrix(&scene->r, v3d->camera, is_left, viewmat);
    view3d_main_rgn_setup_offscreen(graph, scene, v3d, rgn, viewmat, winmat);
  }
  else { /* SCE_VIEWS_FORMAT_MULTIVIEW */
    float viewmat[4][4];
    Object *camera = dune_camera_multiview_render(scene, v3d->camera, viewname);

    dune_camera_multiview_view_matrix(&scene->r, camera, false, viewmat);
    view3d_main_rgn_setup_offscreen(graph, scene, v3d, rgn, viewmat, winmat);
  }
}

void ed_view3d_draw_offscreen(Graph *graph,
                              const Scene *scene,
                              eDrawType drawtype,
                              View3D *v3d,
                              ARgn *rgn,
                              int winx,
                              int winy,
                              const float viewmat[4][4],
                              const float winmat[4][4],
                              bool is_image_render,
                              bool draw_background,
                              const char *viewname,
                              const bool do_color_management,
                              const bool restore_rv3d_mats,
                              GPUOffScreen *ofs,
                              GPUViewport *viewport)
{
  RgnView3D *rv3d = rgn->rgndata;
  RenderEngineType *engine_type = ed_view3d_engine_type(scene, drawtype);

  /* Store `orig` variables. */
  struct {
    struct ThemeState theme_state;

    /* View3D */
    eDrawType v3d_shading_type;

    /* Rgn */
    int rgn_winx, rgn_winy;
    rcti rgn_winrct;

    /* RgnView3D */
    /* Needed so the value won't be left overwritten,
     * Wo this the WinPaintCursor can't use the pixel size & view matrices for drawing. */
    struct RV3DMatrixStore *rv3d_mats;
  } orig = {
      .v3d_shading_type = v3d->shading.type,

      .rhn_winx = rgn->winx,
      .rgn_winy = rgnn->winy,
      .rgn_winrct = rgnn->winrct,

      .rv3d_mats = ed_view3d_mats_rv3d_backup(rgn->rgndata),
  };

  ui_Theme_Store(&orig.theme_state);
  ui_SetTheme(SPACE_VIEW3D, RGN_TYPE_WIN);

  /* Set temp new size. */
  rgn->winx = winx;
  rgn->winy = winy;
  rgn->winrct.xmin = 0;
  rgn->winrct.ymin = 0;
  rgn->winrct.xmax = winx;
  rgn->winrct.ymax = winy;

  /* There are too many fns inside the draw manager that check the shading type,
   * so use a temp override instead. */
  v3d->shading.type = drawtype;

  /* Set flags. */
  G.f |= G_FLAG_RENDER_VIEWPORT;

  {
    /* Free images which can have changed on frame-change.
     * WARNING: can be slow so only free anim images. */
    dune_image_free_anim_gputextures(G.main);
  }

  gpu_matrix_push_projection();
  gpu_matrix_id_set();
  gpu_matrix_push();
  gpu_matrix_identity_set();

  if ((viewname != NULL && viewname[0] != '\0') && (viewmat == NULL) &&
      rv3d->persp == RV3D_CAMOB && v3d->camera) {
    view3d_stereo3d_setup_offscreen(graph, scene, v3d, rgn, winmat, viewname);
  }
  else {
    view3d_main_rgn_setup_offscreen(graph, scene, v3d, rgn, viewmat, winmat);
  }

  /* main drawing call */
  draw_render_loop_offscreen(graph,
                             engine_type,
                             rgn,
                             v3d,
                             is_img_render,
                             draw_background,
                             do_color_management,
                             ofs,
                             viewport);
  gpu_matrix_pop_projection();
  gpu_matrix_pop();

  /* Restore all `orig` members. */
  rgn->winx = orig.rgn_winx;
  rgn->winy = orig.rgn_winy;
  rgn->winrct = orig.rgn_winrct;

  /* Optionally do _not_ restore rv3d matrices (e.g. they are used/stored in the ImBuff for
   * reprojection, see texture_paint_image_from_view_ex(). */
  if (restore_rv3d_mats) {
    ed_view3d_mats_rv3d_restore(rgn->rgndata, orig.rv3d_mats);
  }
  mem_free(orig.rv3d_mats);

  ui_Theme_Restore(&orig.theme_state);
  v3d->shading.type = orig.v3d_shading_type;

  G.f &= ~G_FLAG_RENDER_VIEWPORT;
}

void ed_view3d_draw_offscreen_simple(Graph *graph,
                                     Scene *scene,
                                     View3DShading *shading_override,
                                     eDrawType drawtype,
                                     int winx,
                                     int winy,
                                     uint draw_flags,
                                     const float viewmat[4][4],
                                     const float winmat[4][4],
                                     float clip_start,
                                     float clip_end,
                                     bool is_xr_surface,
                                     bool is_image_render,
                                     bool draw_background,
                                     const char *viewname,
                                     const bool do_color_management,
                                     GPUOffScreen *ofs,
                                     GPUViewport *viewport)
{
  View3D v3d = {NULL};
  ARgn ar = {NULL};
  RhnView3D rv3d = {{{0}}};

  v3d.rgnbase.first = v3d.rgnbase.last = &ar;
  ar.rgndata = &rv3d;
  ar.rgntype = RGN_TYPE_WIN;

  View3DShading *source_shading_settings = &scene->display.shading;
  if (draw_flags & V3D_OFSDRAW_OVERRIDE_SCENE_SETTINGS && shading_override != NULL) {
    source_shading_settings = shading_override;
  }
  memcpy(&v3d.shading, source_shading_settings, sizeof(View3DShading));
  v3d.shading.type = drawtype;

  if (shading_override) {
    /* Pass. */
  }
  else if (drawtype == OB_MATERIAL) {
    v3d.shading.flag = V3D_SHADING_SCENE_WORLD | V3D_SHADING_SCENE_LIGHTS;
  }

  if ((draw_flags & ~V3D_OFSDRAW_OVERRIDE_SCENE_SETTINGS) == V3D_OFSDRAW_NONE) {
    v3d.flag2 = V3D_HIDE_OVERLAYS;
  }
  else {
    if (draw_flags & V3D_OFSDRAW_SHOW_ANNOTATION) {
      v3d.flag2 |= V3D_SHOW_ANNOTATION;
    }
    if (draw_flags & V3D_OFSDRAW_SHOW_GRIDFLOOR) {
      v3d.gridflag |= V3D_SHOW_FLOOR | V3D_SHOW_X | V3D_SHOW_Y;
      v3d.grid = 1.0f;
      v3d.gridlines = 16;
      v3d.gridsubdiv = 10;
    }
    if (draw_flags & V3D_OFSDRAW_SHOW_SEL) {
      v3d.flag |= V3D_SEL_OUTLINE;
    }
    if (draw_flags & V3D_OFSDRAW_XR_SHOW_CONTROLLERS) {
      v3d.flag2 |= V3D_XR_SHOW_CONTROLLERS;
    }
    if (draw_flags & V3D_OFSDRAW_XR_SHOW_CUSTOM_OVERLAYS) {
      v3d.flag2 |= V3D_XR_SHOW_CUSTOM_OVERLAYS;
    }
    /* Disable other overlays (set all available _HIDE_ flags). */
    v3d.overlay.flag |= V3D_OVERLAY_HIDE_CURSOR | V3D_OVERLAY_HIDE_TXT |
                        V3D_OVERLAY_HIDE_MOTION_PATHS | V3D_OVERLAY_HIDE_BONES |
                        V3D_OVERLAY_HIDE_OBJECT_XTRAS | V3D_OVERLAY_HIDE_OBJECT_ORIGINS;
    v3d.flag |= V3D_HIDE_HELPLINES;
  }

  if (is_xr_surface) {
    v3d.flag |= V3D_XR_SESSION_SURFACE;
  }

  rv3d.persp = RV3D_PERSP;
  v3d.clip_start = clip_start;
  v3d.clip_end = clip_end;
  /* Actually not used since we pass in the projection matrix. */
  v3d.lens = 0;

  ed_view3d_draw_offscreen(graph,
                           scene,
                           drawtype,
                           &v3d,
                           &ar,
                           winx,
                           winy,
                           viewmat,
                           winmat,
                           is_image_render,
                           draw_background,
                           viewname,
                           do_color_management,
                           true,
                           ofs,
                           viewport);
}

ImBuf *ed_view3d_draw_offscreen_imbuf(Graph *graph,
                                      Scene *scene,
                                      eDrawType drawtype,
                                      View3D *v3d,
                                      ARgn *rgn,
                                      int sizex,
                                      int sizey,
                                      eImBufFlags imbuf_flag,
                                      int alpha_mode,
                                      const char *viewname,
                                      const bool restore_rv3d_mats,
                                      /* output vars */
                                      GPUOffScreen *ofs,
                                      char err_out[256])
{
  RgnView3D *rv3d = rgn->rgndata;
  const bool draw_sky = (alpha_mode == R_ADDSKY);

  /* view state */
  bool is_ortho = false;
  float winmat[4][4];

  if (ofs && ((gpu_offscreen_width(ofs) != sizex) || (gpu_offscreen_height(ofs) != sizey))) {
    /* sizes differ, can't reuse */
    ofs = NULL;
  }

  GPUFrameBuffer *old_fb = gou_framebuffer_active_get();

  if (old_fb) {
    gpu_framebuffer_restore();
  }

  const bool own_ofs = (ofs == NULL);
  draw_opengl_cxt_enable();

  if (own_ofs) {
    /* bind */
    ofs = gpu_offscreen_create(sizex, sizey, true, GPU_RGBA8, err_out);
    if (ofs == NULL) {
      draw_opengl_cxt_disable();
      return NULL;
    }
  }

  gpu_offscreen_bind(ofs, true);

  /* read in pixels & stamp */
  ImBuf *ibuf = imbuf_alloc(sizex, sizey, 32, imbuf_flag);

  /* render 3d view */
  if (rv3d->persp == RV3D_CAMOB && v3d->camera) {
    CameraParams params;
    Obj *camera = dune_camera_multiview_render(scene, v3d->camera, viewname);
    const Obj *camera_eval = graph_get_eval_obj(graph, camera);

    dune_camera_params_init(&params);
    /* fallback for non camera objects */
    params.clip_start = v3d->clip_start;
    params.clip_end = v3d->clip_end;
    dune_camera_params_from_obj(&params, camera_eval);
    dune_camera_multiview_params(&scene->r, &params, camera_eval, viewname);
    dune_camera_params_compute_viewplane(&params, sizex, sizey, scene->r.xasp, scene->r.yasp);
    dune_camera_params_compute_matrix(&params);

    is_ortho = params.is_ortho;
    copy_m4_m4(winmat, params.winmat);
  }
  else {
    rctf viewplane;
    float clip_start, clipend;

    is_ortho = er_view3d_viewplane_get(
        graph, v3d, rv3d, sizex, sizey, &viewplane, &clip_start, &clipend, NULL);
    if (is_ortho) {
      orthographic_m4(winmat,
                      viewplane.xmin,
                      viewplane.xmax,
                      viewplane.ymin,
                      viewplane.ymax,
                      -clipend,
                      clipend);
    }
    else {
      perspective_m4(winmat,
                     viewplane.xmin,
                     viewplane.xmax,
                     viewplane.ymin,
                     viewplane.ymax,
                     clip_start,
                     clipend);
    }
  }

  /* `do_color_management` should be controlled by the caller. Currently when doing a
   * viewport render anim and saving to an 8bit file format, color management would be applied
   * twice. Once here, and once when saving to disk. In this case the Save As Render
   * option cannot be controlled either. But when doing an off-screen render you want to do the
   * color management here.
   *
   * This option was added here to increase the performance for quick view-port preview renders.
   * When using workbench the color diffs haven't been reported as a bug. But users also use
   * the viewport rendering to render Eevee scenes. In the later situation the saved colors are
   * totally wrong. */
  const bool do_color_management = (ibuf->rect_float == NULL);
  ed_view3d_draw_offscreen(graph,
                           scene,
                           drawtype,
                           v3d,
                           rgn,
                           sizex,
                           sizey,
                           NULL,
                           winmat,
                           true,
                           draw_sky,
                           viewname,
                           do_color_management,
                           restore_rv3d_mats,
                           ofs,
                           NULL);

  if (ibuf->rect_float) {
    gpu_offscreen_read_pixels(ofs, GPU_DATA_FLOAT, ibuf->rect_float);
  }
  else if (ibuf->rect) {
    gpu_offscreen_read_pixels(ofs, GPU_DATA_UBYTE, ibuf->rect);
  }

  /* unbind */
  gpu_offscreen_unbind(ofs, true);

  if (own_ofs) {
    gpi_offscreen_free(ofs);
  }

  draw_opengl_cxt_disable();

  if (old_fb) {
    gpu_framebuffer_bind(old_fb);
  }

  if (ibuf->rect_float && ibuf->rect) {
    imbuf_rect_from_float(ibuf);
  }

  return ibuf;
}

ImBuf *ed_view3d_draw_offscreen_imbuf_simple(Graph *graph,
                                             Scene *scene,
                                             View3DShading *shading_override,
                                             eDrawType drawtype,
                                             Obj *camera,
                                             int width,
                                             int height,
                                             eImBufFlags imbuf_flag,
                                             eV3DOffscreenDrawFlag draw_flags,
                                             int alpha_mode,
                                             const char *viewname,
                                             GPUOffScreen *ofs,
                                             char err_out[256])
{
  View3D v3d = {NULL};
  ARgn rgn = {NULL};
  RgnView3D rv3d = {{{0}}};

  /* connect data */
  v3d.rgnbase.first = v3d.rgnbase.last = &rgn;
  rgn.rgndata = &rv3d;
  rgn.rhntype = RGN_TYPE_WIN;

  v3d.camera = camera;
  View3DShading *source_shading_settings = &scene->display.shading;
  if (draw_flags & V3D_OFSDRAW_OVERRIDE_SCENE_SETTINGS && shading_override != NULL) {
    source_shading_settings = shading_override;
  }
  memcpy(&v3d.shading, source_shading_settings, sizeof(View3DShading));

  if (drawtype == OB_RENDER) {
    /* Don't use external engines for preview. Fall back to solid instead of Eevee as rendering
     * with Eevee is potentially slow due to compiling shaders and loading textures, and the
     * depsgraph may not have been updated to have all the right geometry attributes. */
    if (!dune_scene_uses_workbench(scene)) {
      drawtype = OB_SOLID;
    }
  }

  if (drawtype == OB_MATERIAL) {
    v3d.shading.flag = V3D_SHADING_SCENE_WORLD | V3D_SHADING_SCENE_LIGHTS;
    v3d.shading.render_pass = SCE_PASS_COMBINED;
  }
  else if (drawtype == OB_RENDER) {
    v3d.shading.flag = V3D_SHADING_SCENE_WORLD_RENDER | V3D_SHADING_SCENE_LIGHTS_RENDER;
    v3d.shading.render_pass = SCE_PASS_COMBINED;
  }
  else if (drawtype == OB_TEXTURE) {
    drawtype = OB_SOLID;
    v3d.shading.light = V3D_LIGHTING_STUDIO;
    v3d.shading.color_type = V3D_SHADING_TEXTURE_COLOR;
  }
  v3d.shading.type = drawtype;

  v3d.flag2 = V3D_HIDE_OVERLAYS;
  /* HACK: When rendering pen objs this opacity is used to mix vertex colors in when not in
   * render mode. */
  v3d.overlay.pen_vertex_paint_opacity = 1.0f;

  if (draw_flags & V3D_OFSDRAW_SHOW_ANNOTATION) {
    v3d.flag2 |= V3D_SHOW_ANNOTATION;
  }
  if (draw_flags & V3D_OFSDRAW_SHOW_GRIDFLOOR) {
    v3d.gridflag |= V3D_SHOW_FLOOR | V3D_SHOW_X | V3D_SHOW_Y;
  }

  v3d.shading.background_type = V3D_SHADING_BACKGROUND_WORLD;

  rv3d.persp = RV3D_CAMOB;

  copy_m4_m4(rv3d.viewinv, v3d.camera->obmat);
  normalize_m4(rv3d.viewinv);
  invert_m4_m4(rv3d.viewmat, rv3d.viewinv);

  {
    CameraParams params;
    const Obj *view_camera_eval = graph_get_eval_obj(
        graph, dune_camera_multiview_render(scene, v3d.camera, viewname));

    dune_camera_params_init(&params);
    dune_camera_params_from_object(&params, view_camera_eval);
    dune_camera_multiview_params(&scene->r, &params, view_camera_eval, viewname);
    dune_camera_params_compute_viewplane(&params, width, height, scene->r.xasp, scene->r.yasp);
    dune_camera_params_compute_matrix(&params);

    copy_m4_m4(rv3d.winmat, params.winmat);
    v3d.clip_start = params.clip_start;
    v3d.clip_end = params.clip_end;
    v3d.lens = params.lens;
  }

  mul_m4_m4m4(rv3d.persmat, rv3d.winmat, rv3d.viewmat);
  invert_m4_m4(rv3d.persinv, rv3d.viewinv);

  return ed_view3d_draw_offscreen_imbuf(graph,
                                        scene,
                                        v3d.shading.type,
                                        &v3d,
                                        &rgn,
                                        width,
                                        height,
                                        imbuf_flag,
                                        alpha_mode,
                                        viewname,
                                        true,
                                        ofs,
                                        err_out);
}
