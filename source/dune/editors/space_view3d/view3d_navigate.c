#include "TYPES_curve.h"
#include "TYPES_gpencil.h"

#include "MEM_guardedalloc.h"

#include "LIB_math.h"
#include "LIB_rect.h"

#include "LANG_translation.h"

#include "DUNE_armature.h"
#include "DUNE_context.h"
#include "DUNE_gpencil_geom.h"
#include "DUNE_layer.h"
#include "DUNE_object.h"
#include "DUNE_paint.h"
#include "DUNE_scene.h"
#include "DUNE_screen.h"
#include "DUNE_vfont.h"

#include "DEG_depsgraph_query.h"

#include "ED_mesh.h"
#include "ED_particle.h"
#include "ED_screen.h"
#include "ED_transform.h"

#include "WM_api.h"
#include "WM_message.h"

#include "API_access.h"
#include "API_define.h"

#include "UI_resources.h"

#include "view3d_intern.h"

#include "view3d_navigate.h" /* own include */

/* -------------------------------------------------------------------- */
/** Navigation Polls **/

static bool view3d_navigation_poll_impl(duneContext *C, const char viewlock)
{
  if (!ED_operator_region_view3d_active(C)) {
    return false;
  }

  const RegionView3D *rv3d = CTX_wm_region_view3d(C);
  return !(RV3D_LOCK_FLAGS(rv3d) & viewlock);
}

bool view3d_location_poll(duneContext *C)
{
  return view3d_navigation_poll_impl(C, RV3D_LOCK_LOCATION);
}

bool view3d_rotation_poll(duneContext *C)
{
  return view3d_navigation_poll_impl(C, RV3D_LOCK_ROTATION);
}

bool view3d_zoom_or_dolly_poll(dContext *C)
{
  return view3d_navigation_poll_impl(C, RV3D_LOCK_ZOOM_AND_DOLLY);
}

/* -------------------------------------------------------------------- */
/** \name Generic View Operator Properties
 * \{ */

void view3d_operator_properties_common(wmOperatorType *ot, const enum eV3D_OpPropFlag flag)
{
  if (flag & V3D_OP_PROP_MOUSE_CO) {
    PropertyRNA *prop;
    prop = RNA_def_int(ot->srna, "mx", 0, 0, INT_MAX, "Region Position X", "", 0, INT_MAX);
    RNA_def_property_flag(prop, PROP_HIDDEN);
    prop = RNA_def_int(ot->srna, "my", 0, 0, INT_MAX, "Region Position Y", "", 0, INT_MAX);
    RNA_def_property_flag(prop, PROP_HIDDEN);
  }
  if (flag & V3D_OP_PROP_DELTA) {
    RNA_def_int(ot->srna, "delta", 0, INT_MIN, INT_MAX, "Delta", "", INT_MIN, INT_MAX);
  }
  if (flag & V3D_OP_PROP_USE_ALL_REGIONS) {
    PropertyRNA *prop;
    prop = RNA_def_boolean(
        ot->srna, "use_all_regions", 0, "All Regions", "View selected for all regions");
    RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  }
  if (flag & V3D_OP_PROP_USE_MOUSE_INIT) {
    WM_operator_properties_use_cursor_init(ot);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Generic View Operator Custom-Data
 * \{ */

void calctrackballvec(const rcti *rect, const int event_xy[2], float r_dir[3])
{
  const float radius = V3D_OP_TRACKBALLSIZE;
  const float t = radius / (float)M_SQRT2;
  const float size[2] = {BLI_rcti_size_x(rect), BLI_rcti_size_y(rect)};
  /* Aspect correct so dragging in a non-square view doesn't squash the direction.
   * So diagonal motion rotates the same direction the cursor is moving. */
  const float size_min = min_ff(size[0], size[1]);
  const float aspect[2] = {size_min / size[0], size_min / size[1]};

  /* Normalize x and y. */
  r_dir[0] = (event_xy[0] - BLI_rcti_cent_x(rect)) / ((size[0] * aspect[0]) / 2.0);
  r_dir[1] = (event_xy[1] - BLI_rcti_cent_y(rect)) / ((size[1] * aspect[1]) / 2.0);
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
    RegionView3D *rv3d = vod->rv3d;
    view3d_orbit_apply_dyn_ofs(
        rv3d->ofs, vod->init.ofs, vod->init.quat, viewquat_new, vod->dyn_ofs);
  }
}

bool view3d_orbit_calc_center(bContext *C, float r_dyn_ofs[3])
{
  static float lastofs[3] = {0, 0, 0};
  bool is_set = false;

  const Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer_eval = DEG_get_evaluated_view_layer(depsgraph);
  View3D *v3d = CTX_wm_view3d(C);
  Object *ob_act_eval = OBACT(view_layer_eval);
  Object *ob_act = DEG_get_original_object(ob_act_eval);

  if (ob_act && (ob_act->mode & OB_MODE_ALL_PAINT) &&
      /* with weight-paint + pose-mode, fall through to using calculateTransformCenter */
      ((ob_act->mode & OB_MODE_WEIGHT_PAINT) && BKE_object_pose_armature_get(ob_act)) == 0) {
    /* in case of sculpting use last average stroke position as a rotation
     * center, in other cases it's not clear what rotation center shall be
     * so just rotate around object origin
     */
    if (ob_act->mode &
        (OB_MODE_SCULPT | OB_MODE_TEXTURE_PAINT | OB_MODE_VERTEX_PAINT | OB_MODE_WEIGHT_PAINT)) {
      float stroke[3];
      BKE_paint_stroke_get_average(scene, ob_act_eval, stroke);
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
      add_v2_v2(lastofs, ef->textcurs[i]);
    }
    mul_v2_fl(lastofs, 1.0f / 4.0f);

    mul_m4_v3(ob_act_eval->obmat, lastofs);

    is_set = true;
  }
  else if (ob_act == NULL || ob_act->mode == OB_MODE_OBJECT) {
    /* object mode use boundbox centers */
    Base *base_eval;
    uint tot = 0;
    float select_center[3];

    zero_v3(select_center);
    for (base_eval = FIRSTBASE(view_layer_eval); base_eval; base_eval = base_eval->next) {
      if (BASE_SELECTED(v3d, base_eval)) {
        /* use the boundbox if we can */
        Object *ob_eval = base_eval->object;

        if (ob_eval->runtime.bb && !(ob_eval->runtime.bb->flag & BOUNDBOX_DIRTY)) {
          float cent[3];

          BKE_boundbox_calc_center_aabb(ob_eval->runtime.bb, cent);

          mul_m4_v3(ob_eval->obmat, cent);
          add_v3_v3(select_center, cent);
        }
        else {
          add_v3_v3(select_center, ob_eval->obmat[3]);
        }
        tot++;
      }
    }
    if (tot) {
      mul_v3_fl(select_center, 1.0f / (float)tot);
      copy_v3_v3(lastofs, select_center);
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

static enum eViewOpsFlag viewops_flag_from_args(bool use_select, bool use_depth)
{
  enum eViewOpsFlag flag = 0;
  if (use_select) {
    flag |= VIEWOPS_FLAG_ORBIT_SELECT;
  }
  if (use_depth) {
    flag |= VIEWOPS_FLAG_DEPTH_NAVIGATE;
  }

  return flag;
}

enum eViewOpsFlag viewops_flag_from_prefs(void)
{
  return viewops_flag_from_args((U.uiflag & USER_ORBIT_SELECTION) != 0,
                                (U.uiflag & USER_DEPTH_NAVIGATE) != 0);
}

ViewOpsData *viewops_data_create(bContext *C, const wmEvent *event, enum eViewOpsFlag viewops_flag)
{
  ViewOpsData *vod = MEM_callocN(sizeof(ViewOpsData), __func__);

  /* Store data. */
  vod->bmain = CTX_data_main(C);
  vod->depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  vod->scene = CTX_data_scene(C);
  vod->area = CTX_wm_area(C);
  vod->region = CTX_wm_region(C);
  vod->v3d = vod->area->spacedata.first;
  vod->rv3d = vod->region->regiondata;

  Depsgraph *depsgraph = vod->depsgraph;
  RegionView3D *rv3d = vod->rv3d;

  /* Could do this more nicely. */
  if ((viewops_flag & VIEWOPS_FLAG_USE_MOUSE_INIT) == 0) {
    viewops_flag &= ~VIEWOPS_FLAG_DEPTH_NAVIGATE;
  }

  /* we need the depth info before changing any viewport options */
  if (viewops_flag & VIEWOPS_FLAG_DEPTH_NAVIGATE) {
    float fallback_depth_pt[3];

    view3d_operator_needs_opengl(C); /* Needed for Z-buffer drawing. */

    negate_v3_v3(fallback_depth_pt, rv3d->ofs);

    vod->use_dyn_ofs = ED_view3d_autodist(
        depsgraph, vod->region, vod->v3d, event->mval, vod->dyn_ofs, true, fallback_depth_pt);
  }
  else {
    vod->use_dyn_ofs = false;
  }

  if (viewops_flag & VIEWOPS_FLAG_PERSP_ENSURE) {
    if (ED_view3d_persp_ensure(depsgraph, vod->v3d, vod->region)) {
      /* If we're switching from camera view to the perspective one,
       * need to tag viewport update, so camera view and borders are properly updated. */
      ED_region_tag_redraw(vod->region);
    }
  }

  /* set the view from the camera, if view locking is enabled.
   * we may want to make this optional but for now its needed always */
  ED_view3d_camera_lock_init(depsgraph, vod->v3d, vod->rv3d);

  vod->init.persp = rv3d->persp;
  vod->init.dist = rv3d->dist;
  vod->init.camzoom = rv3d->camzoom;
  copy_qt_qt(vod->init.quat, rv3d->viewquat);
  copy_v2_v2_int(vod->init.event_xy, event->xy);
  copy_v2_v2_int(vod->prev.event_xy, event->xy);

  if (viewops_flag & VIEWOPS_FLAG_USE_MOUSE_INIT) {
    zero_v2_int(vod->init.event_xy_offset);
  }
  else {
    /* Simulate the event starting in the middle of the region. */
    vod->init.event_xy_offset[0] = BLI_rcti_cent_x(&vod->region->winrct) - event->xy[0];
    vod->init.event_xy_offset[1] = BLI_rcti_cent_y(&vod->region->winrct) - event->xy[1];
  }

  vod->init.event_type = event->type;
  copy_v3_v3(vod->init.ofs, rv3d->ofs);

  copy_qt_qt(vod->curr.viewquat, rv3d->viewquat);

  if (viewops_flag & VIEWOPS_FLAG_ORBIT_SELECT) {
    float ofs[3];
    if (view3d_orbit_calc_center(C, ofs) || (vod->use_dyn_ofs == false)) {
      vod->use_dyn_ofs = true;
      negate_v3_v3(vod->dyn_ofs, ofs);
      viewops_flag &= ~VIEWOPS_FLAG_DEPTH_NAVIGATE;
    }
  }

  if (viewops_flag & VIEWOPS_FLAG_DEPTH_NAVIGATE) {
    if (vod->use_dyn_ofs) {
      if (rv3d->is_persp) {
        float my_origin[3]; /* Original #RegionView3D.ofs. */
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
        const float mval_region_mid[2] = {(float)vod->region->winx / 2.0f,
                                          (float)vod->region->winy / 2.0f};

        ED_view3d_win_to_3d(vod->v3d, vod->region, vod->dyn_ofs, mval_region_mid, rv3d->ofs);
        negate_v3(rv3d->ofs);
      }
      negate_v3(vod->dyn_ofs);
      copy_v3_v3(vod->init.ofs, rv3d->ofs);
    }
  }

  /* For dolly */
  ED_view3d_win_to_vector(vod->region, (const float[2]){UNPACK2(event->mval)}, vod->init.mousevec);

  {
    int event_xy_offset[2];
    add_v2_v2v2_int(event_xy_offset, event->xy, vod->init.event_xy_offset);

    /* For rotation with trackball rotation. */
    calctrackballvec(&vod->region->winrct, event_xy_offset, vod->init.trackvec);
  }

  {
    float tvec[3];
    negate_v3_v3(tvec, rv3d->ofs);
    vod->init.zfac = ED_view3d_calc_zfac(rv3d, tvec);
  }

  vod->reverse = 1.0f;
  if (rv3d->persmat[2][1] < 0.0f) {
    vod->reverse = -1.0f;
  }

  rv3d->rflag |= RV3D_NAVIGATING;

  return vod;
}

void viewops_data_free(bContext *C, ViewOpsData *vod)
{
  ARegion *region;
  if (vod) {
    region = vod->region;
    vod->rv3d->rflag &= ~RV3D_NAVIGATING;

    if (vod->timer) {
      WM_event_remove_timer(CTX_wm_manager(C), vod->timer->win, vod->timer);
    }

    if (vod->init.dial) {
      MEM_freeN(vod->init.dial);
    }

    MEM_freeN(vod);
  }
  else {
    region = CTX_wm_region(C);
  }

  /* Need to redraw because drawing code uses RV3D_NAVIGATING to draw
   * faster while navigation operator runs. */
  ED_region_tag_redraw(region);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Generic View Operator Utilities
 * \{ */

/**
 * \param align_to_quat: When not NULL, set the axis relative to this rotation.
 */
static void axis_set_view(bContext *C,
                          View3D *v3d,
                          ARegion *region,
                          const float quat_[4],
                          char view,
                          char view_axis_roll,
                          int perspo,
                          const float *align_to_quat,
                          const int smooth_viewtx)
{
  RegionView3D *rv3d = region->regiondata; /* no NULL check is needed, poll checks */
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
    ED_region_tag_redraw(region);
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
    ED_view3d_smooth_view(C,
                          v3d,
                          region,
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

    /* so we animate _from_ the camera location */
    Object *camera_eval = DEG_get_evaluated_object(CTX_data_ensure_evaluated_depsgraph(C),
                                                   v3d->camera);
    ED_view3d_from_object(camera_eval, rv3d->ofs, NULL, &rv3d->dist, NULL);

    ED_view3d_smooth_view(C,
                          v3d,
                          region,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .camera_old = camera_eval,
                              .ofs = ofs,
                              .quat = quat,
                              .dist = &dist,
                          });
  }
  else {
    /* rotate around selection */
    const float *dyn_ofs_pt = NULL;
    float dyn_ofs[3];

    if (U.uiflag & USER_ORBIT_SELECTION) {
      if (view3d_orbit_calc_center(C, dyn_ofs)) {
        negate_v3(dyn_ofs);
        dyn_ofs_pt = dyn_ofs;
      }
    }

    /* no camera involved */
    ED_view3d_smooth_view(C,
                          v3d,
                          region,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .quat = quat,
                              .dyn_ofs = dyn_ofs_pt,
                          });
  }
}

void viewmove_apply(ViewOpsData *vod, int x, int y)
{
  const float event_ofs[2] = {
      vod->prev.event_xy[0] - x,
      vod->prev.event_xy[1] - y,
  };

  if ((vod->rv3d->persp == RV3D_CAMOB) && !ED_view3d_camera_lock_check(vod->v3d, vod->rv3d)) {
    ED_view3d_camera_view_pan(vod->region, event_ofs);
  }
  else if (ED_view3d_offset_lock_check(vod->v3d, vod->rv3d)) {
    vod->rv3d->ofs_lock[0] -= (event_ofs[0] * 2.0f) / (float)vod->region->winx;
    vod->rv3d->ofs_lock[1] -= (event_ofs[1] * 2.0f) / (float)vod->region->winy;
  }
  else {
    float dvec[3];

    ED_view3d_win_to_delta(vod->region, event_ofs, vod->init.zfac, dvec);

    sub_v3_v3(vod->rv3d->ofs, dvec);

    if (RV3D_LOCK_FLAGS(vod->rv3d) & RV3D_BOXVIEW) {
      view3d_boxview_sync(vod->area, vod->region);
    }
  }

  vod->prev.event_xy[0] = x;
  vod->prev.event_xy[1] = y;

  ED_view3d_camera_lock_sync(vod->depsgraph, vod->v3d, vod->rv3d);

  ED_region_tag_redraw(vod->region);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name View All Operator
 *
 * Move & Zoom the view to fit all of its contents.
 * \{ */

static bool view3d_object_skip_minmax(const View3D *v3d,
                                      const RegionView3D *rv3d,
                                      const Object *ob,
                                      const bool skip_camera,
                                      bool *r_only_center)
{
  BLI_assert(ob->id.orig_id == NULL);
  *r_only_center = false;

  if (skip_camera && (ob == v3d->camera)) {
    return true;
  }

  if ((ob->type == OB_EMPTY) && (ob->empty_drawtype == OB_EMPTY_IMAGE) &&
      !BKE_object_empty_image_frame_is_visible_in_view3d(ob, rv3d)) {
    *r_only_center = true;
    return false;
  }

  return false;
}

static void view3d_object_calc_minmax(Depsgraph *depsgraph,
                                      Scene *scene,
                                      Object *ob_eval,
                                      const bool only_center,
                                      float min[3],
                                      float max[3])
{
  /* Account for duplis. */
  if (BKE_object_minmax_dupli(depsgraph, scene, ob_eval, min, max, false) == 0) {
    /* Use if duplis aren't found. */
    if (only_center) {
      minmax_v3v3_v3(min, max, ob_eval->obmat[3]);
    }
    else {
      BKE_object_minmax(ob_eval, min, max, false);
    }
  }
}

static void view3d_from_minmax(bContext *C,
                               View3D *v3d,
                               ARegion *region,
                               const float min[3],
                               const float max[3],
                               bool ok_dist,
                               const int smooth_viewtx)
{
  RegionView3D *rv3d = region->regiondata;
  float afm[3];
  float size;

  ED_view3d_smooth_view_force_finish(C, v3d, region);

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
      Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
      new_dist = ED_view3d_radius_to_dist(
          v3d, region, depsgraph, persp, true, (size / 2) * VIEW3D_MARGIN);
      if (rv3d->is_persp) {
        /* don't zoom closer than the near clipping plane */
        new_dist = max_ff(new_dist, v3d->clip_start * 1.5f);
      }
    }
  }

  mid_v3_v3v3(new_ofs, min, max);
  negate_v3(new_ofs);

  if (rv3d->persp == RV3D_CAMOB && !ED_view3d_camera_lock_check(v3d, rv3d)) {
    rv3d->persp = RV3D_PERSP;
    ED_view3d_smooth_view(C,
                          v3d,
                          region,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .camera_old = v3d->camera,
                              .ofs = new_ofs,
                              .dist = ok_dist ? &new_dist : NULL,
                          });
  }
  else {
    ED_view3d_smooth_view(C,
                          v3d,
                          region,
                          smooth_viewtx,
                          &(const V3D_SmoothParams){
                              .ofs = new_ofs,
                              .dist = ok_dist ? &new_dist : NULL,
                          });
  }

  /* Smooth-view does view-lock #RV3D_BOXVIEW copy. */
}

/**
 * Same as #view3d_from_minmax but for all regions (except cameras).
 */
static void view3d_from_minmax_multi(bContext *C,
                                     View3D *v3d,
                                     const float min[3],
                                     const float max[3],
                                     const bool ok_dist,
                                     const int smooth_viewtx)
{
  ScrArea *area = CTX_wm_area(C);
  ARegion *region;
  for (region = area->regionbase.first; region; region = region->next) {
    if (region->regiontype == RGN_TYPE_WINDOW) {
      RegionView3D *rv3d = region->regiondata;
      /* when using all regions, don't jump out of camera view,
       * but _do_ allow locked cameras to be moved */
      if ((rv3d->persp != RV3D_CAMOB) || ED_view3d_camera_lock_check(v3d, rv3d)) {
        view3d_from_minmax(C, v3d, region, min, max, ok_dist, smooth_viewtx);
      }
    }
  }
}

static int view3d_all_exec(bContext *C, wmOperator *op)
{
  ARegion *region = CTX_wm_region(C);
  View3D *v3d = CTX_wm_view3d(C);
  RegionView3D *rv3d = CTX_wm_region_view3d(C);
  Scene *scene = CTX_data_scene(C);
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  ViewLayer *view_layer_eval = DEG_get_evaluated_view_layer(depsgraph);
  Base *base_eval;
  const bool use_all_regions = RNA_boolean_get(op->ptr, "use_all_regions");
  const bool skip_camera = (ED_view3d_camera_lock_check(v3d, region->regiondata) ||
                            /* any one of the regions may be locked */
                            (use_all_regions && v3d->flag2 & V3D_LOCK_CAMERA));
  const bool center = RNA_boolean_get(op->ptr, "center");
  const int smooth_viewtx = WM_operator_smooth_viewtx_get(op);

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
    BKE_scene_cursor_mat3_to_rot(cursor, mat3, false);
  }
  else {
    INIT_MINMAX(min, max);
  }

  for (base_eval = view_layer_eval->object_bases.first; base_eval; base_eval = base_eval->next) {
    if (BASE_VISIBLE(v3d, base_eval)) {
      bool only_center = false;
      Object *ob = DEG_get_original_object(base_eval->object);
      if (view3d_object_skip_minmax(v3d, rv3d, ob, skip_camera, &only_center)) {
        continue;
      }
      view3d_object_calc_minmax(depsgraph, scene, base_eval->object, only_center, min, max);
      changed = true;
    }
  }

  if (center) {
    struct wmMsgBus *mbus = CTX_wm_message_bus(C);
    WM_msg_publish_rna_prop(mbus, &scene->id, &scene->cursor, View3DCursor, location);

    DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
  }

  if (!changed) {
    ED_region_tag_redraw(region);
    /* TODO: should this be cancel?
     * I think no, because we always move the cursor, with or without
     * object, but in this case there is no change in the scene,
     * only the cursor so I choice a ED_region_tag like
     * view3d_smooth_view do for the center_cursor.
     * See bug T22640.
     */
    return OPERATOR_FINISHED;
  }

  if (RV3D_CLIPPING_ENABLED(v3d, rv3d)) {
    /* This is an approximation, see function documentation for details. */
    ED_view3d_clipping_clamp_minmax(rv3d, min, max);
  }

  if (use_all_regions) {
    view3d_from_minmax_multi(C, v3d, min, max, true, smooth_viewtx);
  }
  else {
    view3d_from_minmax(C, v3d, region, min, max, true, smooth_viewtx);
  }

  return OPERATOR_FINISHED;
}

void VIEW3D_OT_view_all(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Frame All";
  ot->description = "View all objects in scene";
  ot->idname = "VIEW3D_OT_view_all";

  /* api callbacks */
  ot->exec = view3d_all_exec;
  ot->poll = ED_operator_region_view3d_active;

  /* flags */
  ot->flag = 0;

  /* properties */
  view3d_operator_properties_common(ot, V3D_OP_PROP_USE_ALL_REGIONS);
  RNA_def_boolean(ot->srna, "center", 0, "Center", "");
}
