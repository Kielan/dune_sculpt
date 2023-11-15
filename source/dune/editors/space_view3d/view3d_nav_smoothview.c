#include "types_camera.h"

#include "mem_guardedalloc.h"

#include "lib_math.h"

#include "dune_cxt.h"

#include "graph_query.h"

#include "win_api.h"

#include "ed_screen.h"

#include "view3d_intern.h"
#include "view3d_nav.h" /* own include */

/* Smooth View Op & Utils
 * Use for view transitions to have smooth (animd) transitions */

/* This op is one of the 'timer refresh' ones like anim playback */
struct SmoothView3DState {
  float dist;
  float lens;
  float quat[4];
  float ofs[3];
};

struct SmoothView3DStore {
  /* Source. */
  struct SmoothView3DState src; /* source */
  struct SmoothView3DState dst; /* destination */
  struct SmoothView3DState org; /* original */

  bool to_camera;

  bool use_dyn_ofs;
  float dyn_ofs[3];

  /* When smooth-view is enabled, store the 'rv3d->view' here,
   * assign back when the view motion is completed. */
  char org_view;

  double time_allowed;
};

static void view3d_smooth_view_state_backup(struct SmoothView3DState *sms_state,
                                            const View3D *v3d,
                                            const RgnView3D *rv3d)
{
  copy_v3_v3(sms_state->ofs, rv3d->ofs);
  copy_qt_qt(sms_state->quat, rv3d->viewquat);
  sms_state->dist = rv3d->dist;
  sms_state->lens = v3d->lens;
}

static void view3d_smooth_view_state_restore(const struct SmoothView3DState *sms_state,
                                             View3D *v3d,
                                             RgnView3D *rv3d)
{
  copy_v3_v3(rv3d->ofs, sms_state->ofs);
  copy_qt_qt(rv3d->viewquat, sms_state->quat);
  rv3d->dist = sms_state->dist;
  v3d->lens = sms_state->lens;
}

/* will start timer if appropriate */
void ed_view3d_smooth_view_ex(
    /* avoid passing in the cxt */
    const Graph *graph,
    WinMngr *wm,
    Win *win,
    ScrArea *area,
    View3D *v3d,
    ARgn *rgn,
    const int smooth_viewtx,
    const V3D_SmoothParams *sview)
{
  RgnView3D *rv3d = rgn->rgndata;
  struct SmoothView3DStore sms = {{0}};

  /* init sms */
  view3d_smooth_view_state_backup(&sms.dst, v3d, rv3d);
  view3d_smooth_view_state_backup(&sms.src, v3d, rv3d);
  /* If smooth-view runs multiple times. */
  if (rv3d->sms == NULL) {
    view3d_smooth_view_state_backup(&sms.org, v3d, rv3d);
  }
  else {
    sms.org = rv3d->sms->org;
  }
  sms.org_view = rv3d->view;

  /* sms.to_camera = false; */ /* initialized to zero anyway */

  /* note on camera locking, this is a little confusing but works ok.
   * we may be changing the view 'as if' there is no active camera, but in fact
   * there is an active camera which is locked to the view.
   *
   * In the case where smooth view is moving _to_ a camera we don't want that
   * camera to be moved or changed, so only when the camera is not being set should
   * we allow camera option locking to initialize the view settings from the camera.
   */
  if (sview->camera == NULL && sview->camera_old == NULL) {
    ed_view3d_camera_lock_init(graph, v3d, rv3d);
  }

  /* store the options we want to end with */
  if (sview->ofs) {
    copy_v3_v3(sms.dst.ofs, sview->ofs);
  }
  if (sview->quat) {
    copy_qt_qt(sms.dst.quat, sview->quat);
  }
  if (sview->dist) {
    sms.dst.dist = *sview->dist;
  }
  if (sview->lens) {
    sms.dst.lens = *sview->lens;
  }

  if (sview->dyn_ofs) {
    lib_assert(sview->ofs == NULL);
    lib_assert(sview->quat != NULL);

    copy_v3_v3(sms.dyn_ofs, sview->dyn_ofs);
    sms.use_dyn_ofs = true;

    /* calculate the final destination offset */
    view3d_orbit_apply_dyn_ofs(sms.dst.ofs, sms.src.ofs, sms.src.quat, sms.dst.quat, sms.dyn_ofs);
  }

  if (sview->camera) {
    Ob *ob_camera_eval = graph_get_eval_ob(graph, sview->camera);
    if (sview->ofs != NULL) {
      sms.dst.dist = ed_view3d_offset_distance(
          ob_camera_eval->obmat, sview->ofs, VIEW3D_DIST_FALLBACK);
    }
    ed_view3d_from_ob(ob_camera_eval, sms.dst.ofs, sms.dst.quat, &sms.dst.dist, &sms.dst.lens);
    sms.to_camera = true; /* restore view3d values in end */
  }

  if ((sview->camera_old == sview->camera) &&   /* Camera. */
      (sms.dst.dist == rv3d->dist) &&           /* Distance. */
      (sms.dst.lens == v3d->lens) &&            /* Lens. */
      equals_v3v3(sms.dst.ofs, rv3d->ofs) &&    /* Offset. */
      equals_v4v4(sms.dst.quat, rv3d->viewquat) /* Rotation. */
  ) {
    /* Early return if nothing changed. */
    return;
  }

  /* Skip smooth viewing for external render engine draw. */
  if (smooth_viewtx && !(v3d->shading.type == OB_RENDER && rv3d->render_engine)) {

    /* original values */
    if (sview->camera_old) {
      Ob *ob_camera_old_eval = graph_get_eval_ob(graph, sview->camera_old);
      if (sview->ofs != NULL) {
        sms.src.dist = ed_view3d_offset_distance(ob_camera_old_eval->obmat, sview->ofs, 0.0f);
      }
      ed_view3d_from_ob(
          ob_camera_old_eval, sms.src.ofs, sms.src.quat, &sms.src.dist, &sms.src.lens);
    }
    /* grid draw as floor */
    if ((RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ROTATION) == 0) {
      /* use existing if exists, means multiple calls to smooth view
       * won't lose the original 'view' setting */
      rv3d->view = RV3D_VIEW_USER;
    }

    sms.time_allowed = (double)smooth_viewtx / 1000.0;

    /* If this is view rotation only we can decrease the time allowed by the angle between quats
     * this means small rotations won't lag. */
    if (sview->quat && !sview->ofs && !sview->dist) {
      /* scale the time allowed by the rotation */
      /* 180deg == 1.0 */
      sms.time_allowed *= (double)fabsf(angle_signed_normalized_qtqt(sms.dst.quat, sms.src.quat)) /
                          M_PI;
    }

    /* ensure it shows correct */
    if (sms.to_camera) {
      /* use ortho if we move from an ortho view to an ortho camera */
      Ob *ob_camera_eval = graph_get_eval_ob(graph, sview->camera);
      rv3d->persp = (((rv3d->is_persp == false) && (ob_camera_eval->type == OB_CAMERA) &&
                      (((Camera *)ob_camera_eval->data)->type == CAM_ORTHO)) ?
                         RV3D_ORTHO :
                         RV3D_PERSP);
    }

    rv3d->rflag |= RV3D_NAVIGATING;

    /* not essential but in some cases the caller will tag the area for redraw, and in that
     * case we can get a flicker of the 'org' user view but we want to see 'src' */
    view3d_smooth_view_state_restore(&sms.src, v3d, rv3d);

    /* keep track of running timer! */
    if (rv3d->sms == NULL) {
      rv3d->sms = mem_malloc(sizeof(struct SmoothView3DStore), "smoothview v3d");
    }
    *rv3d->sms = sms;
    if (rv3d->smooth_timer) {
      win_ev_remove_timer(wm, win, rv3d->smooth_timer);
    }
    /* TIMER1 is hard-coded in key-map. */
    rv3d->smooth_timer = win_ev_add_timer(wm, win, TIMER1, 1.0 / 100.0);
  }
  else {
    /* Anim is disabled, apply immediately. */
    if (sms.to_camera == false) {
      copy_v3_v3(rv3d->ofs, sms.dst.ofs);
      copy_qt_qt(rv3d->viewquat, sms.dst.quat);
      rv3d->dist = sms.dst.dist;
      v3d->lens = sms.dst.lens;

      ed_view3d_camera_lock_sync(graph, v3d, rv3d);
    }

    if (RV3D_LOCK_FLAGS(rv3d) & RV3D_BOXVIEW) {
      view3d_boxview_copy(area, rgn);
    }

    ed_rgn_tag_redraw(rgn);

    win_ev_add_mousemove(win);
  }
}

void ed_view3d_smooth_view(Cxt *C,
                           View3D *v3d,
                           ARgn *rgn,
                           const int smooth_viewtx,
                           const struct V3D_SmoothParams *sview)
{
  const Graph *graph = cxt_data_ensure_eval_graph(C);
  WinMngr *wm = cxt_wm(C);
  Win *win = cxt_win(C);
  ScrArea *area = cxt_win_area(C);

  ed_view3d_smooth_view_ex(graph, wm, win, area, v3d, rgn, smooth_viewtx, sview);
}

/* only meant for timer usage */
static void view3d_smoothview_apply(Cxt *C, View3D *v3d, ARgn *rgn, bool sync_boxview)
{
  WinMngr *wm = cxt_wm(C);
  RgnView3D *rv3d = rgn->rgndata;
  struct SmoothView3DStore *sms = rv3d->sms;
  float step, step_inv;

  if (sms->time_allowed != 0.0) {
    step = (float)((rv3d->smooth_timer->duration) / sms->time_allowed);
  }
  else {
    step = 1.0f;
  }

  /* end timer */
  if (step >= 1.0f) {
    Win *win = cxt_win(C);

    /* if we went to camera, store the original */
    if (sms->to_camera) {
      rv3d->persp = RV3D_CAMOB;
      view3d_smooth_view_state_restore(&sms->org, v3d, rv3d);
    }
    else {
      const Graph *graph = cxt_data_ensure_eval_graph(C);

      view3d_smooth_view_state_restore(&sms->dst, v3d, rv3d);

      ed_view3d_camera_lock_sync(graph, v3d, rv3d);
      ed_view3d_camera_lock_autokey(v3d, rv3d, C, true, true);
    }

    if ((RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ROTATION) == 0) {
      rv3d->view = sms->org_view;
    }

    mem_free(rv3d->sms);
    rv3d->sms = NULL;

    win_ev_remove_timer(wm, win, rv3d->smooth_timer);
    rv3d->smooth_timer = NULL;
    rv3d->rflag &= ~RV3D_NAVIGATING;

    /* Ev handling won't know if a UI item has been moved under the ptr. */
    win_ev_add_mousemove(win);
  }
  else {
    /* ease in/out */
    step = (3.0f * step * step - 2.0f * step * step * step);

    step_inv = 1.0f - step;

    interp_qt_qtqt(rv3d->viewquat, sms->src.quat, sms->dst.quat, step);

    if (sms->use_dyn_ofs) {
      view3d_orbit_apply_dyn_ofs(
          rv3d->ofs, sms->src.ofs, sms->src.quat, rv3d->viewquat, sms->dyn_ofs);
    }
    else {
      interp_v3_v3v3(rv3d->ofs, sms->src.ofs, sms->dst.ofs, step);
    }

    rv3d->dist = sms->dst.dist * step + sms->src.dist * step_inv;
    v3d->lens = sms->dst.lens * step + sms->src.lens * step_inv;

    const Graph *graph = cxt_data_ensure_eval_graph(C);
    ed_view3d_camera_lock_sync(graph, v3d, rv3d);
    if (ed_screen_anim_playing(wm)) {
      ed_view3d_camera_lock_autokey(v3d, rv3d, C, true, true);
    }
  }

  if (sync_boxview && (RV3D_LOCK_FLAGS(rv3d) & RV3D_BOXVIEW)) {
    view3d_boxview_copy(cxt_win_area(C), rgn);
  }

  /* This doesn't work right because the v3d->lens is now used in ortho mode r51636,
   * when switching camera in quad-view the other ortho views would zoom & reset.
   * For now only redraw all rgns when smooth-view finishes. */
  if (step >= 1.0f) {
    win_ev_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, v3d);
  }
  else {
    ed_rgn_tag_redraw(rgn);
  }
}

static int view3d_smoothview_invoke(Cxt *C, WinOp *UNUSED(op), const WinEv *ev)
{
  View3D *v3d = cxt_win_view3d(C);
  ARgn *rgn = cxt_win_rgn(C);
  RgnView3D *rv3d = rgn->rgndata;

  /* escape if not our timer */
  if (rv3d->smooth_timer == NULL || rv3d->smooth_timer != ev->customdata) {
    return OP_PASS_THROUGH;
  }

  view3d_smoothview_apply(C, v3d, rgn, true);

  return OP_FINISHED;
}

void ed_view3d_smooth_view_force_finish(Cxt *C, View3D *v3d, ARgn *rgn)
{
  RgnView3D *rv3d = rgn->rgndata;

  if (rv3d && rv3d->sms) {
    rv3d->sms->time_allowed = 0.0; /* force finishing */
    view3d_smoothview_apply(C, v3d, region, false);

    /* force update of view matrix so tools that run immediately after
     * can use them without redrawing first */
    Graph *graph = cxt_data_ensure_eval_graph(C);
    Scene *scene = cxt_data_scene(C);
    ed_view3d_update_viewmat(graph, scene, v3d, rgn, NULL, NULL, NULL, false);
  }
}

void VIEW3D_OT_smoothview(WinOpType *ot)
{
  /* ids */
  ot->name = "Smooth View";
  ot->idname = "VIEW3D_OT_smoothview";

  /* api cbs */
  ot->invoke = view3d_smoothview_invoke;

  /* flags */
  ot->flag = OPTYPE_INTERNAL;

  ot->poll = ed_op_view3d_active;
}
