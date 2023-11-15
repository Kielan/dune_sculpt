#include "types_camera.h"

#include "mem_guardedalloc.h"

#include "lib_math.h"
#include "lib_rect.h"

#include "dune_cxt.h"
#include "dune_report.h"

#include "graph_query.h"

#include "win_api.h"

#include "api_access.h"

#include "view3d_intern.h"
#include "view3d_nav.h" /* own include */

/* Border Zoom Op */
static int view3d_zoom_border_ex(Cxt *C, WinOp *op)
{
  ARgn *rgn = cxt_win_rgn(C);
  View3D *v3d = cxt_win_view3d(C);
  RgnView3D *rv3d = cxt_win_rgn_view3d(C);
  const int smooth_viewtx = win_op_smooth_viewtx_get(op);

  /* Zooms in on a border drawn by the user */
  rcti rect;
  float dvec[3], vb[2], xscale, yscale;
  float dist_range[2];

  /* SMOOTHVIEW */
  float new_dist;
  float new_ofs[3];

  /* ZBuffer depth vars */
  float depth_close = FLT_MAX;
  float cent[2], p[3];

  /* Otherwise opengl won't work. */
  view3d_op_needs_opengl(C);

  /* get box sel vals using api */
  win_op_props_border_to_rcti(op, &rect);

  /* check if zooming in/out view */
  const bool zoom_in = !api_bool_get(op->ptr, "zoom_out");

  ed_view3d_dist_range_get(v3d, dist_range);

  es_view3d_depth_override(
      cxt_data_ensure_eval_graph(C), rgn, v3d, NULL, V3D_DEPTH_NO_PEN, NULL);
  {
    /* avoid allocating the whole depth buffer */
    ViewDepths depth_temp = {0};

    /* avoid view3d_update_depths() for speed. */
    view3d_depths_rect_create(rgn, &rect, &depth_temp);

    /* find the closest Z pixel */
    depth_close = view3d_depth_near(&depth_temp);

    MEM_SAFE_FREE(depth_temp.depths);
  }

  /* Resize border to the same ratio as the win. */
  {
    const float rgn_aspect = (float)rgn->winx / (float)rgn->winy;
    if (((float)lib_rcti_size_x(&rect) / (float)lib_rcti_size_y(&rect)) < rgn_aspect) {
      lib_rcti_resize_x(&rect, (int)(lib_rcti_size_y(&rect) * rgn_aspect));
    }
    else {
      lib_rcti_resize_y(&rect, (int)(lib_rcti_size_x(&rect) / rgn_aspect));
    }
  }

  cent[0] = (((float)rect.xmin) + ((float)rect.xmax)) / 2;
  cent[1] = (((float)rect.ymin) + ((float)rect.ymax)) / 2;

  if (rv3d->is_persp) {
    float p_corner[3];

    /* no depths to use, we can't do anything! */
    if (depth_close == FLT_MAX) {
      dune_report(op->reports, RPT_ERROR, "Depth too large");
      return OP_CANCELLED;
    }
    /* convert border to 3d coordinates */
    if ((!ed_view3d_unproject_v3(rgn, cent[0], cent[1], depth_close, p)) ||
        (!ed_view3d_unproject_v3(rgn, rect.xmin, rect.ymin, depth_close, p_corner))) {
      return OP_CANCELLED;
    }

    sub_v3_v3v3(dvec, p, p_corner);
    negate_v3_v3(new_ofs, p);

    new_dist = len_v3(dvec);

    /* Account for the lens, without this a narrow lens zooms in too close. */
    new_dist *= (v3d->lens / DEFAULT_SENSOR_WIDTH);

    /* ignore dist_range min */
    dist_range[0] = v3d->clip_start * 1.5f;
  }
  else { /* orthographic */
    /* find the current win width and height */
    vb[0] = rgn->winx;
    vb[1] = rgn->winy;

    new_dist = rv3d->dist;

    /* convert the drawn rectangle into 3d space */
    if (depth_close != FLT_MAX &&
        ed_view3d_unproject_v3(rgn, cent[0], cent[1], depth_close, p)) {
      negate_v3_v3(new_ofs, p);
    }
    else {
      float xy_delta[2];
      float zfac;

      /* We can't use the depth, fallback to the old way that doesn't set the center depth */
      copy_v3_v3(new_ofs, rv3d->ofs);

      {
        float tvec[3];
        negate_v3_v3(tvec, new_ofs);
        zfac = ED_view3d_calc_zfac(rv3d, tvec);
      }

      xy_delta[0] = (rect.xmin + rect.xmax - vb[0]) / 2.0f;
      xy_delta[1] = (rect.ymin + rect.ymax - vb[1]) / 2.0f;
      ed_view3d_win_to_delta(rgn, xy_delta, zfac, dvec);
      /* center the view to the center of the rectangle */
      sub_v3_v3(new_ofs, dvec);
    }

    /* work out the ratios, so that everything selected fits when we zoom */
    xscale = (lib_rcti_size_x(&rect) / vb[0]);
    yscale = (lib_rcti_size_y(&rect) / vb[1]);
    new_dist *= max_ff(xscale, yscale);
  }

  if (!zoom_in) {
    sub_v3_v3v3(dvec, new_ofs, rv3d->ofs);
    new_dist = rv3d->dist * (rv3d->dist / new_dist);
    add_v3_v3v3(new_ofs, rv3d->ofs, dvec);
  }

  /* clamp after bc we may have been zooming out */
  CLAMP(new_dist, dist_range[0], dist_range[1]);

  /* TODO: 'is_camera_lock' not currently working well. */
  const bool is_camera_lock = ed_view3d_camera_lock_check(v3d, rv3d);
  if ((rv3d->persp == RV3D_CAMOB) && (is_camera_lock == false)) {
    Graph *graph = cxt_data_ensure_eval_graph(C);
    ed_view3d_persp_switch_from_camera(graph, v3d, rv3d, RV3D_PERSP);
  }

  ed_view3d_smooth_view(C,
                        v3d,
                        rgn,
                        smooth_viewtx,
                        &(const V3D_SmoothParams){
                            .ofs = new_ofs,
                            .dist = &new_dist,
                        });

  if (RV3D_LOCK_FLAGS(rv3d) & RV3D_BOXVIEW) {
    view3d_boxview_sync(cxt_win_area(C), rgn);
  }

  return OP_FINISHED;
}

void VIEW3D_OT_zoom_border(WinOpType *ot)
{
  /* ids */
  ot->name = "Zoom to Border";
  ot->description = "Zoom in the view to the nearest ob contained in the border";
  ot->idname = "VIEW3D_OT_zoom_border";

  /* api cbs */
  ot->invoke = win_gesture_box_invoke;
  ot->ex = view3d_zoom_border_ex;
  ot->modal = win_gesture_box_modal;
  ot->cancel = win_gesture_box_cancel;

  ot->poll = view3d_zoom_or_dolly_poll;

  /* flags */
  ot->flag = 0;

  /* props */
  win_op_props_gesture_box_zoom(ot);
}
