#include "types_curve.h"
#include "types_pen.h"

#include "mem_guardedalloc.h"

#include "lib_math.h"
#include "lib_rect.h"

#include "lang.h"

#include "dune_armature.h"
#include "dune_cxt.h"
#include "dune_pen_geom.h"
#include "dune_layer.h"
#include "dune_obj.h"
#include "dune_paint.h"
#include "dune_scene.h"
#include "dune_screen.h"
#include "dune_vfont.h"

#include "graph_query.h"

#include "ed_mesh.h"
#include "ed_particle.h"
#include "ed_screen.h"
#include "ed_transform.h"

#include "win_api.h"
#include "win_msg.h"

#include "api_access.h"
#include "api_define.h"

#include "ui_resources.h"

#include "view3d_intern.h"

#include "view3d_nav.h" /* own include */

/* Nav Polls */
static bool view3d_nav_poll_impl(Cxt *C, const char viewlock)
{
  if (!ed_op_rgn_view3d_active(C)) {
    return false;
  }

  const RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  return !(RV3D_LOCK_FLAGS(rv3d) & viewlock);
}

bool view3d_location_poll(Cxt *C)
{
  return view3d_nav_poll_impl(C, RV3D_LOCK_LOCATION);
}

bool view3d_rotation_poll(Cxt *C)
{
  return view3d_nav_poll_impl(C, RV3D_LOCK_ROTATION);
}

bool view3d_zoom_or_dolly_poll(Cxt *C)
{
  return view3d_nav_poll_impl(C, RV3D_LOCK_ZOOM_AND_DOLLY);
}

/* Generic View Op Props */
void view3d_op_props_common(WinOpType *ot, const enum eV3D_OpPropFlag flag)
{
  if (flag & V3D_OP_PROP_MOUSE_CO) {
    ApiProp *prop;
    prop = api_def_int(ot->sapi, "mx", 0, 0, INT_MAX, "Rgn Position X", "", 0, INT_MAX);
    api_def_prop_flag(prop, PROP_HIDDEN);
    prop = api_def_int(ot->srna, "my", 0, 0, INT_MAX, "Region Position Y", "", 0, INT_MAX);
    api_def_prop_flag(prop, PROP_HIDDEN);
  }
  if (flag & V3D_OP_PROP_DELTA) {
    api_def_int(ot->sapi, "delta", 0, INT_MIN, INT_MAX, "Delta", "", INT_MIN, INT_MAX);
  }
  if (flag & V3D_OP_PROP_USE_ALL_RGNS) {
    ApiProp *prop;
    prop = api_def_bool(
        ot->sapi, "use_all_rgns", 0, "All Rgns", "View selected for all rgns");
    api_def_prop_flag(prop, PROP_SKIP_SAVE);
  }
  if (flag & V3D_OP_PROP_USE_MOUSE_INIT) {
    win_op_props_use_cursor_init(ot);
  }
}

/* Generic View Op Custom-Data */
void calctrackballvec(const rcti *rect, const int ev_xy[2], float r_dir[3])
{
  const float radius = V3D_OP_TRACKBALLSIZE;
  const float t = radius / (float)M_SQRT2;
  const float size[2] = {lib_rcti_size_x(rect), lib_rcti_size_y(rect)};
  /* Aspect correct so dragging in a non-square view doesn't squash the direction.
   * So diagonal motion rotates the same direction the cursor is moving. */
  const float size_min = min_ff(size[0], size[1]);
  const float aspect[2] = {size_min / size[0], size_min / size[1]};

  /* Normalize x and y. */
  r_dir[0] = (ev_xy[0] - lib_rcti_cent_x(rect)) / ((size[0] * aspect[0]) / 2.0);
  r_dir[1] = (ev_xy[1] - lib_rcti_cent_y(rect)) / ((size[1] * aspect[1]) / 2.0);
  const float d = len_v2(r_dir);
  if (d < t) {
    /* Inside sphere. */
    r_dir[2] = sqrtf(square_f(radius) - square_f(d));
  }
  else {
    /* On hyperbola. */
    r_dir[2] = square_f(t) / d;
  }
}

void view3d_orbit_apply_dyn_ofs(float r_ofs[3],
                                const float ofs_old[3],
                                const float viewquat_old[4],
                                const float viewquat_new[4],
                                const float dyn_ofs[3])
{
  float q[4];
  invert_qt_qt_normalized(q, viewquat_old);
  mul_qt_qtqt(q, q, viewquat_new);

  invert_qt_normalized(q);

  sub_v3_v3v3(r_ofs, ofs_old, dyn_ofs);
  mul_qt_v3(q, r_ofs);
  add_v3_v3(r_ofs, dyn_ofs);
}

void viewrotate_apply_dyn_ofs(ViewOpsData *vod, const float viewquat_new[4])
{
  if (vod->use_dyn_ofs) {
    RgnView3D *rv3d = vod->rv3d;
    view3d_orbit_apply_dyn_ofs(
        rv3d->ofs, vod->init.ofs, vod->init.quat, viewquat_new, vod->dyn_ofs);
  }
}

bool view3d_orbit_calc_center(Cxt *C, float r_dyn_ofs[3])
{
  static float lastofs[3] = {0, 0, 0};
  bool is_set = false;

  const Graph *graph = cxt_data_ensure_eval_graph(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer_eval = graph_get_eval_view_layer(graph);
  View3D *v3d = cxt_win_view3d(C);
  Ob *ob_act_eval = OBACT(view_layer_eval);
  Ob *ob_act = graph_get_original_ob(ob_act_eval);

  if (ob_act && (ob_act->mode & OB_MODE_ALL_PAINT) &&
      /* with weight-paint + pose-mode, fall through to using calculateTransformCenter */
      ((ob_act->mode & OB_MODE_WEIGHT_PAINT) && dune_ob_pose_armature_get(ob_act)) == 0) {
    /* in case of sculpting use last average stroke position as a rotation
     * center, in other cases it's not clear what rotation center shall be
     * so just rotate around ob origin */
    if (ob_act->mode &
        (OB_MODE_SCULPT | OB_MODE_TEXTURE_PAINT | OB_MODE_VERTEX_PAINT | OB_MODE_WEIGHT_PAINT)) {
      float stroke[3];
      dune_paint_stroke_get_average(scene, ob_act_eval, stroke);
      copy_v3_v3(lastofs, stroke);
    }
    else {
      copy_v3_v3(lastofs, ob_act_eval->obmat[3]);
    }
    is_set = true;
  }
  else if (ob_act && (ob_act->mode & OB_MODE_EDIT) && (ob_act->type == OB_FONT)) {
    Curve *cu = ob_act_eval->data;
    EditFont *ef = cu->editfont;

    zero_v3(lastofs);
    for (int i = 0; i < 4; i++) {
      add_v2_v2(lastofs, ef->txtcurs[i]);
    }
    mul_v2_fl(lastofs, 1.0f / 4.0f);

    mul_m4_v3(ob_act_eval->obmat, lastofs);

    is_set = true;
  }
  else if (ob_act == NULL || ob_act->mode == OB_MODE_OB) {
    /* ob mode use boundbox centers */
    Base *base_eval;
    uint tot = 0;
    float sel_center[3];

    zero_v3(sel_center);
    for (base_eval = FIRSTBASE(view_layer_eval); base_eval; base_eval = base_eval->next) {
      if (BASE_SELECTED(v3d, base_eval)) {
        /* use the boundbox if we can */
        Ob *ob_eval = base_eval->ob;

        if (ob_eval->runtime.bb && !(ob_eval->runtime.bb->flag & BOUNDBOX_DIRTY)) {
          float cent[3];

          dune_boundbox_calc_center_aabb(ob_eval->runtime.bb, cent);

          mul_m4_v3(ob_eval->obmat, cent);
          add_v3_v3(sel_center, cent);
        }
        else {
          add_v3_v3(sel_center, ob_eval->obmat[3]);
        }
        tot++;
      }
    }
    if (tot) {
      mul_v3_fl(sel_center, 1.0f / (float)tot);
      copy_v3_v3(lastofs, sel_center);
      is_set = true;
    }
  }
  else {
    /* If there's no selection, `lastofs` is unmodified and last value since static. */
    is_set = calculateTransformCenter(C, V3D_AROUND_CENTER_MEDIAN, lastofs, NULL);
  }

  copy_v3_v3(r_dyn_ofs, lastofs);

  return is_set;
}

static enum eViewOpsFlag viewops_flag_from_args(bool use_sel, bool use_depth)
{
  enum eViewOpsFlag flag = 0;
  if (use_sel) {
    flag |= VIEWOPS_FLAG_ORBIT_SEL;
  }
  if (use_depth) {
    flag |= VIEWOPS_FLAG_DEPTH_NAV;
  }

  return flag;
}

enum eViewOpsFlag viewops_flag_from_prefs(void)
{
  return viewops_flag_from_args((U.uiflag & USER_ORBIT_SEL) != 0,
                                (U.uiflag & USER_DEPTH_NAV) != 0);
}

ViewOpsData *viewops_data_create(Cxt *C, const WinEv *ev, enum eViewOpsFlag viewops_flag)
{
  ViewOpsData *vod = mem_calloc(sizeof(ViewOpsData), __func__);

  /* Store data. */
  vod->main = cxt_data_main(C);
  vod->graph = cxt_data_ensure_eval_graph(C);
  vod->scene = cxt_data_scene(C);
  vod->area = cxt_win_area(C);
  vod->rgn = cxt_win_rgn(C);
  vod->v3d = vod->area->spacedata.first;
  vod->rv3d = vod->rgn->rhndata;

  Graph *graph = vod->graph;
  RgnView3D *rv3d = vod->rv3d;

  /* Could do this more nicely. */
  if ((viewops_flag & VIEWOPS_FLAG_USE_MOUSE_INIT) == 0) {
    viewops_flag &= ~VIEWOPS_FLAG_DEPTH_NAV;
  }

  /* we need the depth info before changing any viewport options */
  if (viewops_flag & VIEWOPS_FLAG_DEPTH_NAV) {
    float fallback_depth_pt[3];

    view3d_op_needs_opengl(C); /* Needed for Z-buffer drawing. */

    negate_v3_v3(fallback_depth_pt, rv3d->ofs);

    vod->use_dyn_ofs = ed_view3d_autodist(
        graph, vod->rgn, vod->v3d, ev->mval, vod->dyn_ofs, true, fallback_depth_pt);
  }
  else {
    vod->use_dyn_ofs = false;
  }

  if (viewops_flag & VIEWOPS_FLAG_PERSP_ENSURE) {
    if (ed_view3d_persp_ensure(graph, vod->v3d, vod->region)) {
      /* If we're switching from camera view to the perspective one,
       * need to tag viewport update, so camera view and borders are properly updated. */
      ed_rgn_tag_redraw(vod->region);
    }
  }

  /* set the view from the camera, if view locking is enabled.
   * we may want to make this optional but for now its needed always */
  ed_view3d_camera_lock_init(graph, vod->v3d, vod->rv3d);

  vod->init.persp = rv3d->persp;
  vod->init.dist = rv3d->dist;
  vod->init.camzoom = rv3d->camzoom;
  copy_qt_qt(vod->init.quat, rv3d->viewquat);
  copy_v2_v2_int(vod->init.event_xy, ev->xy);
  copy_v2_v2_int(vod->prev.event_xy, ev->xy);

  if (viewops_flag & VIEWOPS_FLAG_USE_MOUSE_INIT) {
    zero_v2_int(vod->init.ev_xy_offset);
  }
  else {
    /* Simulate the ev starting in the middle of the rgn. */
    vod->init.ev_xy_offset[0] = lib_rcti_cent_x(&vod->rgn->winrct) - ev->xy[0];
    vod->init.ev_xy_offset[1] = lib_rcti_cent_y(&vod->rgn->winrct) - ev->xy[1];
  }

  vod->init.ev_type = ev->type;
  copy_v3_v3(vod->init.ofs, rv3d->ofs);

  copy_qt_qt(vod->curr.viewquat, rv3d->viewquat);

  if (viewops_flag & VIEWOPS_FLAG_ORBIT_SEL) {
    float ofs[3];
    if (view3d_orbit_calc_center(C, ofs) || (vod->use_dyn_ofs == false)) {
      vod->use_dyn_ofs = true;
      negate_v3_v3(vod->dyn_ofs, ofs);
      viewops_flag &= ~VIEWOPS_FLAG_DEPTH_NAVIGATE;
    }
  }

  if (viewops_flag & VIEWOPS_FLAG_DEPTH_NAV) {
    if (vod->use_dyn_ofs) {
      if (rv3d->is_persp) {
        float my_origin[3]; /* Original RgnView3D.ofs. */
        float my_pivot[3];  /* View pivot. */
        float dvec[3];

        /* locals for dist correction */
        float mat[3][3];
        float upvec[3];

        negate_v3_v3(my_origin, rv3d->ofs); /* ofs is flipped */

        /* Set the dist value to be the distance from this 3d point this means you'll
         * always be able to zoom into it and panning won't go bad when dist was zero. */

        /* remove dist value */
        upvec[0] = upvec[1] = 0;
        upvec[2] = rv3d->dist;
        copy_m3_m4(mat, rv3d->viewinv);

        mul_m3_v3(mat, upvec);
        sub_v3_v3v3(my_pivot, rv3d->ofs, upvec);
        negate_v3(my_pivot); /* ofs is flipped */

        /* find a new ofs value that is along the view axis
         * (rather than the mouse location) */
        closest_to_line_v3(dvec, vod->dyn_ofs, my_pivot, my_origin);
        vod->init.dist = rv3d->dist = len_v3v3(my_pivot, dvec);

        negate_v3_v3(rv3d->ofs, dvec);
      }
      else {
        const float mval_rgn_mid[2] = {(float)vod->rgn->winx / 2.0f,
                                          (float)vod->rgn->winy / 2.0f};

        ed_view3d_win_to_3d(vod->v3d, vod->rgn, vod->dyn_ofs, mval_rgn_mid, rv3d->ofs);
        negate_v3(rv3d->ofs);
      }
      negate_v3(vod->dyn_ofs);
      copy_v3_v3(vod->init.ofs, rv3d->ofs);
    }
  }

  /* For dolly */
  ed_view3d_win_to_vector(vod->rgn, (const float[2]){UNPACK2(ev->mval)}, vod->init.mousevec);

  {
    int ev_xy_offset[2];
    add_v2_v2v2_int(ev_xy_offset, ev->xy, vod->init.ev_xy_offset);

    /* For rotation with trackball rotation. */
    calctrackballvec(&vod->rgn->winrct, ev_xy_offset, vod->init.trackvec);
  }

  {
    float tvec[3];
    negate_v3_v3(tvec, rv3d->ofs);
    vod->init.zfac = ed_view3d_calc_zfac(rv3d, tvec);
  }

  vod->reverse = 1.0f;
  if (rv3d->persmat[2][1] < 0.0f) {
    vod->reverse = -1.0f;
  }

  rv3d->rflag |= RV3D_NAVIGATING;

  return vod;
}

void viewops_data_free(Cxt *C, ViewOpsData *vod)
{
  ARgn *rgn;
  if (vod) {
    rgn = vod->rgn;
    vod->rv3d->rflag &= ~RV3D_NAVIGATING;

    if (vod->timer) {
      win_ev_remove_timer(cxt_wm(C), vod->timer->win, vod->timer);
    }

    if (vod->init.dial) {
      mem_free(vod->init.dial);
    }

    mem_free(vod);
  }
  else {
    rgn = cxt_wm(C);
  }

  /* Need to redraw bc drawing code uses RV3D_NAVIGATING to draw
   * faster while nav op runs. */
  ed_rgn_tag_redraw(rgn);
}

/* Generic View Op Utils **/
/* param align_to_quat: When not NULL, set the axis relative to this rotation */
static void axis_set_view(Cxt *C,
                          View3D *v3d,
                          ARgn *rgn,
                          const float quat_[4],
                          char view,
                          char view_axis_roll,
                          int perspo,
                          const float *align_to_quat,
                          const int smooth_viewtx)
{
  RgnView3D *rv3d = rgn->rgndata; /* no NULL check is needed, poll checks */
  float quat[4];
  const short orig_persp = rv3d->persp;

  normalize_qt_qt(quat, quat_);

  if (align_to_quat) {
    mul_qt_qtqt(quat, quat, align_to_quat);
    rv3d->view = view = RV3D_VIEW_USER;
    rv3d->view_axis_roll = RV3D_VIEW_AXIS_ROLL_0;
  }

  if (align_to_quat == NULL) {
    rv3d->view = view;
    rv3d->view_axis_roll = view_axis_roll;
  }

  if (RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ROTATION) {
    ed_rgn_tag_redraw(rgn);
    return;
  }

  if (U.uiflag & USER_AUTOPERSP) {
    rv3d->persp = RV3D_VIEW_IS_AXIS(view) ? RV3D_ORTHO : perspo;
  }
  else if (rv3d->persp == RV3D_CAMOB) {
    rv3d->persp = perspo;
  }

  if (rv3d->persp == RV3D_CAMOB && v3d->camera) {
    /* to camera */
    ed_view3d_smooth_view(C,
                          v3d,
                          rgn,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .camera_old = v3d->camera,
                              .ofs = rv3d->ofs,
                              .quat = quat,
                          });
  }
  else if (orig_persp == RV3D_CAMOB && v3d->camera) {
    /* from camera */
    float ofs[3], dist;

    copy_v3_v3(ofs, rv3d->ofs);
    dist = rv3d->dist;

    /* so we anim _from_ the camera location */
    Ob *camera_eval = graph_get_eval_ob(cxt_data_ensure_eval_graph(C),
                                                   v3d->camera);
    ed_view3d_from_ob(camera_eval, rv3d->ofs, NULL, &rv3d->dist, NULL);

    ed_view3d_smooth_view(C,
                          v3d,
                          rgn,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .camera_old = camera_eval,
                              .ofs = ofs,
                              .quat = quat,
                              .dist = &dist,
                          });
  }
  else {
    /* rotate around sel */
    const float *dyn_ofs_pt = NULL;
    float dyn_ofs[3];

    if (U.uiflag & USER_ORBIT_SEL) {
      if (view3d_orbit_calc_center(C, dyn_ofs)) {
        negate_v3(dyn_ofs);
        dyn_ofs_pt = dyn_ofs;
      }
    }

    /* no camera involved */
    ed_view3d_smooth_view(C,
                          v3d,
                          rgn,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .quat = quat,
                              .dyn_ofs = dyn_ofs_pt,
                          });
  }
}

void viewmove_apply(ViewOpsData *vod, int x, int y)
{
  const float ev_ofs[2] = {
      vod->prev.ev_xy[0] - x,
      vod->prev.ev_xy[1] - y,
  };

  if ((vod->rv3d->persp == RV3D_CAMOB) && !ed_view3d_camera_lock_check(vod->v3d, vod->rv3d)) {
    ed_view3d_camera_view_pan(vod->rgn, ev_ofs);
  }
  else if (ed_view3d_offset_lock_check(vod->v3d, vod->rv3d)) {
    vod->rv3d->ofs_lock[0] -= (event_ofs[0] * 2.0f) / (float)vod->rgn->winx;
    vod->rv3d->ofs_lock[1] -= (event_ofs[1] * 2.0f) / (float)vod->rgn->winy;
  }
  else {
    float dvec[3];

    ed_view3d_win_to_delta(vod->rgn, ev_ofs, vod->init.zfac, dvec);

    sub_v3_v3(vod->rv3d->ofs, dvec);

    if (RV3D_LOCK_FLAGS(vod->rv3d) & RV3D_BOXVIEW) {
      view3d_boxview_sync(vod->area, vod->rgn);
    }
  }

  vod->prev.ev_xy[0] = x;
  vod->prev.ev_xy[1] = y;

  ed_view3d_camera_lock_sync(vod->graph, vod->v3d, vod->rv3d);

  ed_rgn_tag_redraw(vod->rgn);
}

/* View All Op
 * Move & Zoom the view to fit all of its contents. */
static bool view3d_ob_skip_minmax(const View3D *v3d,
                                  const RgnView3D *rv3d,
                                  const Ob *ob,
                                  const bool skip_camera,
                                  bool *r_only_center)
{
  lib_assert(ob->id.orig_id == NULL);
  *r_only_center = false;

  if (skip_camera && (ob == v3d->camera)) {
    return true;
  }

  if ((ob->type == OB_EMPTY) && (ob->empty_drawtype == OB_EMPTY_IMG) &&
      !dune_ob_empty_img_frame_is_visible_in_view3d(ob, rv3d)) {
    *r_only_center = true;
    return false;
  }

  return false;
}

static void view3d_ob_calc_minmax(Graph *graph,
                                  Scene *scene,
                                  Ob *ob_eval,
                                  const bool only_center,
                                  float min[3],
                                  float max[3])
{
  /* Account for duplis. */
  if (dune_ob_minmax_dupli(graph, scene, ob_eval, min, max, false) == 0) {
    /* Use if duplis aren't found. */
    if (only_center) {
      minmax_v3v3_v3(min, max, ob_eval->obmat[3]);
    }
    else {
      dune_ob_minmax(ob_eval, min, max, false);
    }
  }
}

static void view3d_from_minmax(Cxt *C,
                               View3D *v3d,
                               ARgn *rgn,
                               const float min[3],
                               const float max[3],
                               bool ok_dist,
                               const int smooth_viewtx)
{
  RgnView3D *rv3d = rgn->rgndata;
  float afm[3];
  float size;

  ed_view3d_smooth_view_force_finish(C, v3d, rgn);

  /* SMOOTHVIEW */
  float new_ofs[3];
  float new_dist;

  sub_v3_v3v3(afm, max, min);
  size = max_fff(afm[0], afm[1], afm[2]);

  if (ok_dist) {
    char persp;

    if (rv3d->is_persp) {
      if (rv3d->persp == RV3D_CAMOB && ED_view3d_camera_lock_check(v3d, rv3d)) {
        persp = RV3D_CAMOB;
      }
      else {
        persp = RV3D_PERSP;
      }
    }
    else { /* ortho */
      if (size < 0.0001f) {
        /* bounding box was a single point so do not zoom */
        ok_dist = false;
      }
      else {
        /* adjust zoom so it looks nicer */
        persp = RV3D_ORTHO;
      }
    }

    if (ok_dist) {
      Graph *graph = cxt_data_ensure_eval_graph(C);
      new_dist = ed_view3d_radius_to_dist(
          v3d, rgn, graph, persp, true, (size / 2) * VIEW3D_MARGIN);
      if (rv3d->is_persp) {
        /* don't zoom closer than the near clipping plane */
        new_dist = max_ff(new_dist, v3d->clip_start * 1.5f);
      }
    }
  }

  mid_v3_v3v3(new_ofs, min, max);
  negate_v3(new_ofs);

  if (rv3d->persp == RV3D_CAMOB && !ed_view3d_camera_lock_check(v3d, rv3d)) {
    rv3d->persp = RV3D_PERSP;
    ed_view3d_smooth_view(C,
                          v3d,
                          rgn,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .camera_old = v3d->camera,
                              .ofs = new_ofs,
                              .dist = ok_dist ? &new_dist : NULL,
                          });
  }
  else {
    ed_view3d_smooth_view(C,
                          v3d,
                          rgn,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .ofs = new_ofs,
                              .dist = ok_dist ? &new_dist : NULL,
                          });
  }

  /* Smooth-view does view-lock RV3D_BOXVIEW copy. */
}

/* Same as view3d_from_minmax but for all rgns except cameras */
static void view3d_from_minmax_multi(Cxt *C,
                                     View3D *v3d,
                                     const float min[3],
                                     const float max[3],
                                     const bool ok_dist,
                                     const int smooth_viewtx)
{
  ScrArea *area = cxt_win_area(C);
  ARrn *rgn;
  for (rgn = area->rgnbase.first; rgn; rgn = rgn->next) {
    if (rgn->rgntype == RGN_TYPE_WIN) {
      RgnView3D *rv3d = rgn->rgndata;
      /* when using all rgns, don't jump out of camera view,
       * but _do_ allow locked cameras to be moved */
      if ((rv3d->persp != RV3D_CAMOB) || ed_view3d_camera_lock_check(v3d, rv3d)) {
        view3d_from_minmax(C, v3d, rgn, min, max, ok_dist, smooth_viewtx);
      }
    }
  }
}

static int view3d_all_ex(Cxt *C, WinOp *op)
{
  ARgn *rgn = cxt_win_rgn(C);
  View3D *v3d = cxt_win_view3d(C);
  RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  Scene *scene = cxt_data_scene(C);
  Graph *graph = cxt_data_ensure_eval_graph(C);
  ViewLayer *view_layer_eval = graph_get_eval_view_layer(graph);
  Base *base_eval;
  const bool use_all_rgns = api_bool_get(op->ptr, "use_all_rgns");
  const bool skip_camera = (ed_view3d_camera_lock_check(v3d, rgn->rgndata) ||
                            /* any one of the rgns may be locked */
                            (use_all_rgns && v3d->flag2 & V3D_LOCK_CAMERA));
  const bool center = api_bool_get(op->ptr, "center");
  const int smooth_viewtx = win_op_smooth_viewtx_get(op);

  float min[3], max[3];
  bool changed = false;

  if (center) {
    /* in 2.4x this also move the cursor to (0, 0, 0) (with shift+c). */
    View3DCursor *cursor = &scene->cursor;
    zero_v3(min);
    zero_v3(max);
    zero_v3(cursor->location);
    float mat3[3][3];
    unit_m3(mat3);
    dune_scene_cursor_mat3_to_rot(cursor, mat3, false);
  }
  else {
    INIT_MINMAX(min, max);
  }

  for (base_eval = view_layer_eval->ob_bases.first; base_eval; base_eval = base_eval->next) {
    if (BASE_VISIBLE(v3d, base_eval)) {
      bool only_center = false;
      Ob *ob = graph_get_original_ob(base_eval->ob);
      if (view3d_ob_skip_minmax(v3d, rv3d, ob, skip_camera, &only_center)) {
        continue;
      }
      view3d_ob_calc_minmax(graph, scene, base_eval->ob, only_center, min, max);
      changed = true;
    }
  }

  if (center) {
    struct WinMsgBus *mbus = cxt_win_msg_bus(C);
    win_msg_publish_api_prop(mbus, &scene->id, &scene->cursor, View3DCursor, location);

    graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
  }

  if (!changed) {
    ed_rgn_tag_redraw(rgn);
    /* TODO: should this be cancel?
     * I think no, because we always move the cursor, with or without
     * ob, but in this case there is no change in the scene,
     * only the cursor so I choice a ed_rgn_tag like
     * view3d_smooth_view do for the center_cursor.
     * See bug T22640.  */
    return OP_FINISHED;
  }

  if (RV3D_CLIPPING_ENABLED(v3d, rv3d)) {
    /* This is an approximation, see fn documentation for details */
    ed_view3d_clipping_clamp_minmax(rv3d, min, max);
  }

  if (use_all_rgns) {
    view3d_from_minmax_multi(C, v3d, min, max, true, smooth_viewtx);
  }
  else {
    view3d_from_minmax(C, v3d, rgn, min, max, true, smooth_viewtx);
  }

  return OP_FINISHED;
}

void VIEW3D_OT_view_all(WinOpType *ot)
{
  /* ids */
  ot->name = "Frame All";
  ot->description = "View all obs in scene";
  ot->idname = "VIEW3D_OT_view_all";

  /* api cbs */
  ot->ex = view3d_all_ex;
  ot->poll = ed_op_rgn_view3d_active;

  /* flags */
  ot->flag = 0;

  /* props */
  view3d_op_props_common(ot, V3D_OP_PROP_USE_ALL_RGNS);
  api_def_bool(ot->sapi, "center", 0, "Center", "");
}

/* Frame Selected Op
 * Move & Zoom the view to fit selected contents */
static int viewselected_ex(Cxt *C, WinOp *op)
{
  ARgn *rgn = cxt_win_rgn(C);
  View3D *v3d = cxt_win_view3d(C);
  RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  Scene *scene = cxt_data_scene(C);
  Graph *graph = cxt_data_ensure_eval_graph(C);
  ViewLayer *view_layer_eval = graph_get_eval_view_layer(graph);
  Ob *ob_eval = OBACT(view_layer_eval);
  Ob *obedit = cxt_data_edit_ob(C);
  const PenData *pdata_eval = ob_eval && (ob_eval->type == OB_PEN) ? ob_eval->data : NULL;
  const bool is_pen_edit = pdata_eval ? PEN_ANY_MODE(pdata_eval) : false;
  const bool is_face_map = ((is_pen_edit == false) && rgn->gizmo_map &&
                            win_gizmomap_is_any_selected(rgn->gizmo_map));
  float min[3], max[3];
  bool ok = false, ok_dist = true;
  const bool use_all_rgns = api_bool_get(op->ptr, "use_all_regns");
  const bool skip_camera = (ed_view3d_camera_lock_check(v3d, rgn->rgndata) ||
                            /* any one of the rgns may be locked */
                            (use_all_rgns && v3d->flag2 & V3D_LOCK_CAMERA));
  const int smooth_viewtx = win_op_smooth_viewtx_get(op);

  INIT_MINMAX(min, max);
  if (is_face_map) {
    ob_eval = NULL;
  }

  if (ob_eval && (ob_eval->mode & OB_MODE_WEIGHT_PAINT)) {
    /* hard-coded exception, we look for the one selected armature */
    /* this is weak code this way, we should make a generic
     * active/selection cb interface once... */
    Base *base_eval;
    for (base_eval = view_layer_eval->ob_bases.first; base_eval; base_eval = base_eval->next) {
      if (BASE_SELECTED_EDITABLE(v3d, base_eval)) {
        if (base_eval->ob->type == OB_ARMATURE) {
          if (base_eval->ob->mode & OB_MODE_POSE) {
            break;
          }
        }
      }
    }
    if (base_eval) {
      ob_eval = base_eval->ob;
    }
  }

  if (is_pen_edit) {
    CXT_DATA_BEGIN (C, PenDataStroke *, pds, editable_pen_strokes) {
      /* we're only interested in selected points here... */
      if ((pds->flag & PEN_STROKE_SEL) && (pds->flag & PEN_STROKE_3DSPACE)) {
        ok |= dune_pen_stroke_minmax(pds, true, min, max);
      }
      if (pds->editcurve != NULL) {
        for (int i = 0; i < pds->editcurve->tot_curve_points; i++) {
          BezTriple *bezt = &pds->editcurve->curve_points[i].bezt;
          if ((bezt->f1 & SEL)) {
            minmax_v3v3_v3(min, max, bezt->vec[0]);
            ok = true;
          }
          if ((bezt->f2 & SEL)) {
            minmax_v3v3_v3(min, max, bezt->vec[1]);
            ok = true;
          }
          if ((bezt->f3 & SEL)) {
            minmax_v3v3_v3(min, max, bezt->vec[2]);
            ok = true;
          }
        }
      }
    }
    CXT_DATA_END;

    if ((ob_eval) && (ok)) {
      mul_m4_v3(ob_eval->obmat, min);
      mul_m4_v3(ob_eval->obmat, max);
    }
  }
  else if (is_face_map) {
    ok = win_gizmomap_minmax(rgn->gizmo_map, true, true, min, max);
  }
  else if (obedit) {
    /* only selected */
    FOREACH_OB_IN_MODE_BEGIN (view_layer_eval, v3d, obedit->type, obedit->mode, ob_eval_iter) {
      ok |= ed_view3d_minmax_verts(ob_eval_iter, min, max);
    }
    FOREACH_OB_IN_MODE_END;
  }
  else if (ob_eval && (ob_eval->mode & OB_MODE_POSE)) {
    FOREACH_OB_IN_MODE_BEGIN (
        view_layer_eval, v3d, ob_eval->type, ob_eval->mode, ob_eval_iter) {
      ok |= dune_pose_minmax(ob_eval_iter, min, max, true, true);
    }
    FOREACH_OB_IN_MODE_END;
  }
  else if (dune_paint_sel_face_test(ob_eval)) {
    ok = paintface_minmax(ob_eval, min, max);
  }
  else if (ob_eval && (ob_eval->mode & OB_MODE_PARTICLE_EDIT)) {
    ok = PE_minmax(graph, scene, cxt_data_view_layer(C), min, max);
  }
  else if (ob_eval && (ob_eval->mode & (OB_MODE_SCULPT | OB_MODE_VERTEX_PAINT |
                                        OB_MODE_WEIGHT_PAINT | OB_MODE_TEXTURE_PAINT))) {
    dune_paint_stroke_get_average(scene, ob_eval, min);
    copy_v3_v3(max, min);
    ok = true;
    ok_dist = 0; /* don't zoom */
  }
  else {
    Base *base_eval;
    for (base_eval = FIRSTBASE(view_layer_eval); base_eval; base_eval = base_eval->next) {
      if (BASE_SELECTED(v3d, base_eval)) {
        bool only_center = false;
        Ob *ob = _get_original_ob(base_eval->ob);
        if (view3d_ob_skip_minmax(v3d, rv3d, ob, skip_camera, &only_center)) {
          continue;
        }
        view3d_ob_calc_minmax(graph, scene, base_eval->ob, only_center, min, max);
        ok = 1;
      }
    }
  }

  if (ok == 0) {
    return OP_FINISHED;
  }

  if (RV3D_CLIPPING_ENABLED(v3d, rv3d)) {
    /* This is an approximation, see fn documentation for details. */
    ed_view3d_clipping_clamp_minmax(rv3d, min, max);
  }

  if (use_all_rgns) {
    view3d_from_minmax_multi(C, v3d, min, max, ok_dist, smooth_viewtx);
  }
  else {
    view3d_from_minmax(C, v3d, rgn, min, max, ok_dist, smooth_viewtx);
  }

  return OP_FINISHED;
}

void VIEW3D_OT_view_selected(WinOpType *ot)
{
  /* ids */
  ot->name = "Frame Selected";
  ot->description = "Move the view to the sel center";
  ot->idname = "VIEW3D_OT_view_selected";

  /* api cbs */
  ot->ex = viewselected_ex;
  ot->poll = view3d_zoom_or_dolly_poll;

  /* flags */
  ot->flag = 0;

  /* props */
  view3d_op_props_common(ot, V3D_OP_PROP_USE_ALL_RGNS);
}

/* View Center Cursor Op */
static int viewcenter_cursor_ex(Cxt *C, WinOp *op)
{
  View3D *v3d = cxt_win_view3d(C);
  RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  Scene *scene = cxt_data_scene(C);

  if (rv3d) {
    ARgn *rgn = cxt_win_rgn(C);
    const int smooth_viewtx = win_op_smooth_viewtx_get(op);

    ed_view3d_smooth_view_force_finish(C, v3d, rgn);

    /* non camera center */
    float new_ofs[3];
    negate_v3_v3(new_ofs, scene->cursor.location);
    ed_view3d_smooth_view(
        C, v3d, rgn, smooth_viewtx, &(const V3D_SmoothParams){.ofs = new_ofs});

    /* Smooth view does view-lock RV3D_BOXVIEW copy. */
  }

  return OP_FINISHED;
}

void VIEW3D_OT_view_center_cursor(WinOpType *ot)
{
  /* ids */
  ot->name = "Center View to Cursor";
  ot->description = "Center the view so that the cursor is in the middle of the view";
  ot->idname = "VIEW3D_OT_view_center_cursor";

  /* api cbs */
  ot->ex = viewcenter_cursor_ex;
  ot->poll = view3d_location_poll;

  /* flags */
  ot->flag = 0;
}


/* View Center Pick Op */
static int viewcenter_pick_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  View3D *v3d = cxt_win_view3d(C);
  RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  ARgn *rgn = cxt_win_rgn(C);

  if (rv3d) {
    struct Graph *graph = cxt_data_ensure_eval_graph(C);
    float new_ofs[3];
    const int smooth_viewtx = win_op_smooth_viewtx_get(op);

    ed_view3d_smooth_view_force_finish(C, v3d, rgn);

    view3d_op_needs_opengl(C);

    if (ed_view3d_autodist(graph, rgn, v3d, ev->mval, new_ofs, false, NULL)) {
      /* pass */
    }
    else {
      /* fallback to simple pan */
      negate_v3_v3(new_ofs, rv3d->ofs);
      ed_view3d_win_to_3d_int(v3d, rgn, new_ofs, ev->mval, new_ofs);
    }
    negate_v3(new_ofs);
    ed_view3d_smooth_view(
        C, v3d, rgn, smooth_viewtx, &(const V3D_SmoothParams){.ofs = new_ofs});
  }

  return OP_FINISHED;
}

void VIEW3D_OT_view_center_pick(WinOpType *ot)
{
  /* ids */
  ot->name = "Center View to Mouse";
  ot->description = "Center the view to the Z-depth position under the mouse cursor";
  ot->idname = "VIEW3D_OT_view_center_pick";

  /* api cbs */
  ot->invoke = viewcenter_pick_invoke;
  ot->poll = view3d_location_poll;

  /* flags */
  ot->flag = 0;
}

/* View Axis Op */
static const EnumPropItem prop_view_items[] = {
    {RV3D_VIEW_LEFT, "LEFT", ICON_TRIA_LEFT, "Left", "View from the left"},
    {RV3D_VIEW_RIGHT, "RIGHT", ICON_TRIA_RIGHT, "Right", "View from the right"},
    {RV3D_VIEW_BOTTOM, "BOTTOM", ICON_TRIA_DOWN, "Bottom", "View from the bottom"},
    {RV3D_VIEW_TOP, "TOP", ICON_TRIA_UP, "Top", "View from the top"},
    {RV3D_VIEW_FRONT, "FRONT", 0, "Front", "View from the front"},
    {RV3D_VIEW_BACK, "BACK", 0, "Back", "View from the back"},
    {0, NULL, 0, NULL, NULL},
};

static int view_axis_ex(Cxt *C, WinOp *op)
{
  View3D *v3d;
  ARgn *rgn;
  RgnView3D *rv3d;
  static int perspo = RV3D_PERSP;
  int viewnum;
  int view_axis_roll = RV3D_VIEW_AXIS_ROLL_0;
  const int smooth_viewtx = win_op_smooth_viewtx_get(op);

  /* no NULL check is needed, poll checks */
  ed_view3d_cxt_user_rgn(C, &v3d, &rgn);
  rv3d = rgn->rgndata;

  ed_view3d_smooth_view_force_finish(C, v3d, rgn);

  viewnum = API_enum_get(op->ptr, "type");

  float align_quat_buf[4];
  float *align_quat = NULL;

  if (api_bool_get(op->ptr, "align_active")) {
    /* align to active ob */
    Ob *obact = cxt_data_active_ob(C);
    if (obact != NULL) {
      float twmat[3][3];
      struct ViewLayer *view_layer = cx_data_view_layer(C);
      Ob *obedit = cxt_data_edit_ob(C);
      /* same as transform gizmo when normal is set */
      ed_getTransformOrientationMatrix(view_layer, v3d, obact, obedit, V3D_AROUND_ACTIVE, twmat);
      align_quat = align_quat_buf;
      mat3_to_quat(align_quat, twmat);
      invert_qt_normalized(align_quat);
    }
  }

  if (api_bool_get(op->ptr, "relative")) {
    float quat_rotate[4];
    float quat_test[4];

    if (viewnum == RV3D_VIEW_LEFT) {
      axis_angle_to_quat(quat_rotate, rv3d->viewinv[1], -M_PI_2);
    }
    else if (viewnum == RV3D_VIEW_RIGHT) {
      axis_angle_to_quat(quat_rotate, rv3d->viewinv[1], M_PI_2);
    }
    else if (viewnum == RV3D_VIEW_TOP) {
      axis_angle_to_quat(quat_rotate, rv3d->viewinv[0], -M_PI_2);
    }
    else if (viewnum == RV3D_VIEW_BOTTOM) {
      axis_angle_to_quat(quat_rotate, rv3d->viewinv[0], M_PI_2);
    }
    else if (viewnum == RV3D_VIEW_FRONT) {
      unit_qt(quat_rotate);
    }
    else if (viewnum == RV3D_VIEW_BACK) {
      axis_angle_to_quat(quat_rotate, rv3d->viewinv[0], M_PI);
    }
    else {
      lib_assert(0);
    }

    mul_qt_qtqt(quat_test, rv3d->viewquat, quat_rotate);

    float angle_best = FLT_MAX;
    int view_best = -1;
    int view_axis_roll_best = -1;
    for (int i = RV3D_VIEW_FRONT; i <= RV3D_VIEW_BOTTOM; i++) {
      for (int j = RV3D_VIEW_AXIS_ROLL_0; j <= RV3D_VIEW_AXIS_ROLL_270; j++) {
        float quat_axis[4];
        ed_view3d_quat_from_axis_view(i, j, quat_axis);
        if (align_quat) {
          mul_qt_qtqt(quat_axis, quat_axis, align_quat);
        }
        const float angle_test = fabsf(angle_signed_qtqt(quat_axis, quat_test));
        if (angle_best > angle_test) {
          angle_best = angle_test;
          view_best = i;
          view_axis_roll_best = j;
        }
      }
    }
    if (view_best == -1) {
      view_best = RV3D_VIEW_FRONT;
      view_axis_roll_best = RV3D_VIEW_AXIS_ROLL_0;
    }

    /* Disallow non-upright views in turn-table modes,
     * it's too difficult to nav out of them. */
    if ((U.flag & USER_TRACKBALL) == 0) {
      if (!ELEM(view_best, RV3D_VIEW_TOP, RV3D_VIEW_BOTTOM)) {
        view_axis_roll_best = RV3D_VIEW_AXIS_ROLL_0;
      }
    }

    viewnum = view_best;
    view_axis_roll = view_axis_roll_best;
  }

  /* Use this to test if we started out with a camera */
  const int nextperspo = (rv3d->persp == RV3D_CAMOB) ? rv3d->lpersp : perspo;
  float quat[4];
  ed_view3d_quat_from_axis_view(viewnum, view_axis_roll, quat);
  axis_set_view(
      C, v3d, rgn, quat, viewnum, view_axis_roll, nextperspo, align_quat, smooth_viewtx);

  perspo = rv3d->persp;

  return OP_FINISHED;
}

void VIEW3D_OT_view_axis(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "View Axis";
  ot->description = "Use a preset viewpoint";
  ot->idname = "VIEW3D_OT_view_axis";

  /* api cbs */
  ot->ex = view_axis_ex;
  ot->poll = ED_operator_rv3d_user_region_poll;

  /* flags */
  ot->flag = 0;

  ot->prop = API_def_enum(ot->srna, "type", prop_view_items, 0, "View", "Preset viewpoint to use");
  API_def_property_flag(ot->prop, PROP_SKIP_SAVE);
  API_def_property_translation_context(ot->prop, LANG_I18NCONTEXT_EDITOR_VIEW3D);

  prop = API_def_boolean(
      ot->srna, "align_active", 0, "Align Active", "Align to the active object's axis");
  API_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = API_def_boolean(
      ot->srna, "relative", 0, "Relative", "Rotate relative to the current orientation");
  API_def_property_flag(prop, PROP_SKIP_SAVE);
}

/* -------------------------------------------------------------------- */
/** View Camera Operator **/

static int view_camera_exec(bContext *C, wmOperator *op)
{
  View3D *v3d;
  ARegion *region;
  RegionView3D *rv3d;
  const int smooth_viewtx = WM_operator_smooth_viewtx_get(op);

  /* no NULL check is needed, poll checks */
  ED_view3d_context_user_region(C, &v3d, &region);
  rv3d = region->regiondata;

  ED_view3d_smooth_view_force_finish(C, v3d, region);

  if ((RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ANY_TRANSFORM) == 0) {
    ViewLayer *view_layer = CTX_data_view_layer(C);
    Scene *scene = CTX_data_scene(C);

    if (rv3d->persp != RV3D_CAMOB) {
      Object *ob = OBACT(view_layer);

      if (!rv3d->smooth_timer) {
        /* store settings of current view before allowing overwriting with camera view
         * only if we're not currently in a view transition */

        ED_view3d_lastview_store(rv3d);
      }

      /* first get the default camera for the view lock type */
      if (v3d->scenelock) {
        /* sets the camera view if available */
        v3d->camera = scene->camera;
      }
      else {
        /* use scene camera if one is not set (even though we're unlocked) */
        if (v3d->camera == NULL) {
          v3d->camera = scene->camera;
        }
      }

      /* if the camera isn't found, check a number of options */
      if (v3d->camera == NULL && ob && ob->type == OB_CAMERA) {
        v3d->camera = ob;
      }

      if (v3d->camera == NULL) {
        v3d->camera = DUNE_view_layer_camera_find(view_layer);
      }

      /* couldn't find any useful camera, bail out */
      if (v3d->camera == NULL) {
        return OPERATOR_CANCELLED;
      }

      /* important these don't get out of sync for locked scenes */
      if (v3d->scenelock && scene->camera != v3d->camera) {
        scene->camera = v3d->camera;
        DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
      }

      /* finally do snazzy view zooming */
      rv3d->persp = RV3D_CAMOB;
      ED_view3d_smooth_view(C,
                            v3d,
                            region,
                            smooth_viewtx,
                            &(const V3D_SmoothParams){
                                .camera = v3d->camera,
                                .ofs = rv3d->ofs,
                                .quat = rv3d->viewquat,
                                .dist = &rv3d->dist,
                                .lens = &v3d->lens,
                            });
    }
    else {
      /* return to settings of last view */
      /* does view3d_smooth_view too */
      axis_set_view(C,
                    v3d,
                    region,
                    rv3d->lviewquat,
                    rv3d->lview,
                    rv3d->lview_axis_roll,
                    rv3d->lpersp,
                    NULL,
                    smooth_viewtx);
    }
  }

  return OPERATOR_FINISHED;
}

void VIEW3D_OT_view_camera(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "View Camera";
  ot->description = "Toggle the camera view";
  ot->idname = "VIEW3D_OT_view_camera";

  /* api callbacks */
  ot->exec = view_camera_exec;
  ot->poll = ED_operator_rv3d_user_region_poll;

  /* flags */
  ot->flag = 0;
}

/* -------------------------------------------------------------------- */
/** View Orbit Operator
 * Rotate (orbit) in incremental steps. For interactive orbit see VIEW3D_OT_rotate.
 **/

enum {
  V3D_VIEW_STEPLEFT = 1,
  V3D_VIEW_STEPRIGHT,
  V3D_VIEW_STEPDOWN,
  V3D_VIEW_STEPUP,
};

static const EnumPropertyItem prop_view_orbit_items[] = {
    {V3D_VIEW_STEPLEFT, "ORBITLEFT", 0, "Orbit Left", "Orbit the view around to the left"},
    {V3D_VIEW_STEPRIGHT, "ORBITRIGHT", 0, "Orbit Right", "Orbit the view around to the right"},
    {V3D_VIEW_STEPUP, "ORBITUP", 0, "Orbit Up", "Orbit the view up"},
    {V3D_VIEW_STEPDOWN, "ORBITDOWN", 0, "Orbit Down", "Orbit the view down"},
    {0, NULL, 0, NULL, NULL},
};

static int vieworbit_exec(duneContext *C, wmOperator *op)
{
  View3D *v3d;
  ARegion *region;
  RegionView3D *rv3d;
  int orbitdir;
  char view_opposite;
  PropertyAPI *prop_angle = API_struct_find_property(op->ptr, "angle");
  float angle = API_property_is_set(op->ptr, prop_angle) ?
                    API_property_float_get(op->ptr, prop_angle) :
                    DEG2RADF(U.pad_rot_angle);

  /* no NULL check is needed, poll checks */
  v3d = CTX_wm_view3d(C);
  region = CTX_wm_region(C);
  rv3d = region->regiondata;

  /* support for switching to the opposite view (even when in locked views) */
  view_opposite = (fabsf(angle) == (float)M_PI) ? ED_view3d_axis_view_opposite(rv3d->view) :
                                                  RV3D_VIEW_USER;
  orbitdir = API_enum_get(op->ptr, "type");

  if ((RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ROTATION) && (view_opposite == RV3D_VIEW_USER)) {
    /* no NULL check is needed, poll checks */
    ED_view3d_context_user_region(C, &v3d, &region);
    rv3d = region->regiondata;
  }

  ED_view3d_smooth_view_force_finish(C, v3d, region);

  if ((RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ROTATION) == 0 || (view_opposite != RV3D_VIEW_USER)) {
    if ((rv3d->persp != RV3D_CAMOB) || ED_view3d_camera_lock_check(v3d, rv3d)) {
      int smooth_viewtx = WM_operator_smooth_viewtx_get(op);
      float quat_mul[4];
      float quat_new[4];

      if (view_opposite == RV3D_VIEW_USER) {
        const Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
        ED_view3d_persp_ensure(depsgraph, v3d, region);
      }

      if (ELEM(orbitdir, V3D_VIEW_STEPLEFT, V3D_VIEW_STEPRIGHT)) {
        if (orbitdir == V3D_VIEW_STEPRIGHT) {
          angle = -angle;
        }

        /* z-axis */
        axis_angle_to_quat_single(quat_mul, 'Z', angle);
      }
      else {

        if (orbitdir == V3D_VIEW_STEPDOWN) {
          angle = -angle;
        }

        /* horizontal axis */
        axis_angle_to_quat(quat_mul, rv3d->viewinv[0], angle);
      }

      mul_qt_qtqt(quat_new, rv3d->viewquat, quat_mul);

      /* avoid precision loss over time */
      normalize_qt(quat_new);

      if (view_opposite != RV3D_VIEW_USER) {
        rv3d->view = view_opposite;
        /* avoid float in-precision, just get a new orientation */
        ED_view3d_quat_from_axis_view(view_opposite, rv3d->view_axis_roll, quat_new);
      }
      else {
        rv3d->view = RV3D_VIEW_USER;
      }

      float dyn_ofs[3], *dyn_ofs_pt = NULL;

      if (U.uiflag & USER_ORBIT_SELECTION) {
        if (view3d_orbit_calc_center(C, dyn_ofs)) {
          negate_v3(dyn_ofs);
          dyn_ofs_pt = dyn_ofs;
        }
      }

      ED_view3d_smooth_view(C,
                            v3d,
                            region,
                            smooth_viewtx,
                            &(const V3D_SmoothParams){
                                .quat = quat_new,
                                .dyn_ofs = dyn_ofs_pt,
                            });

      return OPERATOR_FINISHED;
    }
  }

  return OPERATOR_CANCELLED;
}

void VIEW3D_OT_view_orbit(wmOperatorType *ot)
{
  PropertyAPI *prop;

  /* identifiers */
  ot->name = "View Orbit";
  ot->description = "Orbit the view";
  ot->idname = "VIEW3D_OT_view_orbit";

  /* api callbacks */
  ot->exec = vieworbit_exec;
  ot->poll = ED_operator_rv3d_user_region_poll;

  /* flags */
  ot->flag = 0;

  /* properties */
  prop = API_def_float(ot->srna, "angle", 0, -FLT_MAX, FLT_MAX, "Roll", "", -FLT_MAX, FLT_MAX);
  API_def_property_flag(prop, PROP_SKIP_SAVE);

  ot->prop = API_def_enum(
      ot->srna, "type", prop_view_orbit_items, 0, "Orbit", "Direction of View Orbit");
}

/* -------------------------------------------------------------------- */
/** View Pan Operator
 * Move (pan) in incremental steps. For interactive pan see VIEW3D_OT_move.
 **/

enum {
  V3D_VIEW_PANLEFT = 1,
  V3D_VIEW_PANRIGHT,
  V3D_VIEW_PANDOWN,
  V3D_VIEW_PANUP,
};

static const EnumPropertyItem prop_view_pan_items[] = {
    {V3D_VIEW_PANLEFT, "PANLEFT", 0, "Pan Left", "Pan the view to the left"},
    {V3D_VIEW_PANRIGHT, "PANRIGHT", 0, "Pan Right", "Pan the view to the right"},
    {V3D_VIEW_PANUP, "PANUP", 0, "Pan Up", "Pan the view up"},
    {V3D_VIEW_PANDOWN, "PANDOWN", 0, "Pan Down", "Pan the view down"},
    {0, NULL, 0, NULL, NULL},
};

static int viewpan_invoke(duneContext *C, wmOperator *op, const wmEvent *event)
{
  int x = 0, y = 0;
  int pandir = API_enum_get(op->ptr, "type");

  if (pandir == V3D_VIEW_PANRIGHT) {
    x = -32;
  }
  else if (pandir == V3D_VIEW_PANLEFT) {
    x = 32;
  }
  else if (pandir == V3D_VIEW_PANUP) {
    y = -25;
  }
  else if (pandir == V3D_VIEW_PANDOWN) {
    y = 25;
  }

  ViewOpsData *vod = viewops_data_create(
      C, event, (viewops_flag_from_prefs() & ~VIEWOPS_FLAG_ORBIT_SELECT));

  viewmove_apply(vod, vod->prev.event_xy[0] + x, vod->prev.event_xy[1] + y);

  viewops_data_free(C, vod);

  return OPERATOR_FINISHED;
}

void VIEW3D_OT_view_pan(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Pan View Direction";
  ot->description = "Pan the view in a given direction";
  ot->idname = "VIEW3D_OT_view_pan";

  /* api callbacks */
  ot->invoke = viewpan_invoke;
  ot->poll = view3d_location_poll;

  /* flags */
  ot->flag = 0;

  /* Properties */
  ot->prop = API_def_enum(
      ot->srna, "type", prop_view_pan_items, 0, "Pan", "Direction of View Pan");
}
