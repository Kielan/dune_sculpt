#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#ifndef WIN32
#  include <unistd.h>
#else
#  include <io.h>
#endif

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_fileops.h"
#include "lib_ghash.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "types_camera.h"
#include "types_node.h"
#include "types_ob.h"
#include "types_scene.h"
#include "types_screen.h"

#include "dune_colortools.h"
#include "dune_cxt.hh"
#include "dune_global.h"
#include "dune_icons.h"
#include "dune_img.h"
#include "dune_img_format.h"
#include "dune_img_save.h"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_packedFile.h"
#include "dune_report.h"
#include "dune_scene.h"

#include "graph.hh"

#include "gpu_state.h"

#include "imbuf_colormanagement.h"
#include "imbuf.h"
#include "imbuf_types.h"
#include "imbuf_moviecache.h"
#include "imbuf_openexr.h"

#include "render_pipeline.h"

#include "api_access.hh"
#include "api_define.hh"
#include "api_enum_types.hh"
#include "api_prototypes.h"

#include "ed_img.hh"
#include "ed_mask.hh"
#include "ed_paint.hh"
#include "ed_render.hh"
#include "ed_screen.hh"
#include "ed_space_api.hh"
#include "ed_undo.hh"
#include "ed_util.hh"
#include "ed_util_imbuf.hh"
#include "ed_uvedit.hh"

#include "ui.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "PIL_time.h"

#include "render_engine.h"

#include "img_intern.h"

/* View Nav Utils */
static void simg_zoom_set(
    SpaceImg *simh, ARgn *rgn, float zoom, const float location[2], const bool zoom_to_pos)
{
  float oldzoom = simg->zoom;
  int width, height;

  simg->zoom = zoom;

  if (simg->zoom < 0.1f || simg->zoom > 4.0f) {
    /* check zoom limits */
    ed_space_img_get_size(simg, &width, &height);

    width *= simg->zoom;
    height *= simg->zoom;

    if ((width < 4) && (height < 4) && simg->zoom < oldzoom) {
      simg->zoom = oldzoom;
    }
    else if (lib_rcti_size_x(&rgn->winrct) <= simg->zoom) {
      simg->zoom = oldzoom;
    }
    else if (lib_rcti_size_y(&rgn->winrct) <= sima->zoom) {
      simg->zoom = oldzoom;
    }
  }

  if (zoom_to_pos && location) {
    float aspx, aspy, w, h;

    ed_space_img_get_size(simg, &width, &height);
    ed_space_img_get_aspect(simg, &aspx, &aspy);

    w = width * aspx;
    h = height * aspy;

    simg->xof += ((location[0] - 0.5f) * w - simg->xof) * (simg->zoom - oldzoom) / sima->zoom;
    simg->yof += ((location[1] - 0.5f) * h - simg->yof) * (simg->zoom - oldzoom) / sima->zoom;
  }
}

static void simg_zoom_set_factor(SpaceImg *simg,
                                 ARgn *rgn,
                                 float zoomfac,
                                 const float location[2],
                                 const bool zoom_to_pos)
{
  simg_zoom_set(simg, rgn, simg->zoom * zoomfac, location, zoom_to_pos);
}

/* Fits the view to the bounds exactly, caller should add margin if needed. */
static void simg_zoom_set_from_bounds(SpaceImg *simg, ARgn *rgn, const rctf *bounds)
{
  int img_size[2];
  float aspx, aspy;

  ed_space_img_get_size(simg, &img_size[0], &img_size[1]);
  ed_space_img_get_aspect(simg, &aspx, &aspy);

  img_size[0] = img_size[0] * aspx;
  img_size[1] = img_size[1] * aspy;

  /* adjust offset and zoom */
  simg->xof = roundf((lib_rctf_cent_x(bounds) - 0.5f) * img_size[0]);
  simg->yof = roundf((lib_rctf_cent_y(bounds) - 0.5f) * img_size[1]);

  float size_xy[2], size;
  size_xy[0] = lib_rcti_size_x(&rgn->winrct) / lib_rctf_size_x(bounds) * image_size[0]);
  size_xy[1] = lib_rcti_size_y(&rgn->winrct) / (lib_rctf_size_y(bounds) * image_size[1]);

  size = min_ff(size_xy[0], size_xy[1]);
  CLAMP_MAX(size, 100.0f);

  simg_zoom_set(simg, rgn, size, nullptr, false);
}

static Img *img_from_cxt(const Cxt *C)
{
  /* Edit img is set by templates used throughout the interface, so img
   * ops work outside the img editor. */
  Img *img = static_cast<Img *>(cxt_data_ptr_get_type(C, "edit_img", &ApiImg).data);
    
  if (img) {
    return img;
  }

  /* Image editor. */
  SpaceImg *simg = CTX_wm_space_image(C);
  return (sima) ? sima->image : nullptr;
}

static ImageUser *image_user_from_context(const bContext *C)
{
  /* Edit image user is set by templates used throughout the interface, so
   * image operations work outside the image editor. */
  ImageUser *iuser = static_cast<ImageUser *>(
      CTX_data_pointer_get_type(C, "edit_image_user", &RNA_ImageUser).data);

  if (iuser) {
    return iuser;
  }

  /* Image editor. */
  SpaceImage *sima = CTX_wm_space_image(C);
  return (sima) ? &sima->iuser : nullptr;
}

static ImageUser image_user_from_context_and_active_tile(const bContext *C, Image *ima)
{
  /* Try to get image user from context if available, otherwise use default. */
  ImageUser *iuser_context = image_user_from_context(C);
  ImageUser iuser;
  if (iuser_context) {
    iuser = *iuser_context;
  }
  else {
    BKE_imageuser_default(&iuser);
  }

  /* Use the file associated with the active tile. Otherwise use the first tile. */
  if (ima && ima->source == IMA_SRC_TILED) {
    const ImageTile *active = (ImageTile *)BLI_findlink(&ima->tiles, ima->active_tile_index);
    iuser.tile = active ? active->tile_number : ((ImageTile *)ima->tiles.first)->tile_number;
  }

  return iuser;
}

static bool image_from_context_has_data_poll(bContext *C)
{
  Image *ima = image_from_context(C);
  ImageUser *iuser = image_user_from_context(C);

  if (ima == nullptr) {
    return false;
  }

  void *lock;
  ImBuf *ibuf = BKE_image_acquire_ibuf(ima, iuser, &lock);
  const bool has_buffer = (ibuf && (ibuf->byte_buffer.data || ibuf->float_buffer.data));
  BKE_image_release_ibuf(ima, ibuf, lock);
  return has_buffer;
}

/**
 * Use this when the image buffer is accessing the active tile wo the img user. */
static bool img_from_cxt_has_data_poll_active_tile(bContext *C)
{
  Img *img = img_from_cxt(C);
  ImgUser iuser = img_user_from_cxt_and_active_tile(C, img);

  return dune_img_has_ibuf(img, &iuser);
}

static bool img_not_packed_poll(Cxt *C)
{
  /* Do not run 'replace' on packed imgs, it does not give user expected results at all. */
  Img *img = img_from_cxt(C);
  return (img && lib_list_is_empty(&img->packedfiles));
}

static void img_view_all(SpaceImg *simg, ARgn *rgn, WinOp *op)
{
  float aspx, aspy, zoomx, zoomy, w, h;
  int width, height;
  const bool fit_view = api_bool_get(op->ptr, "fit_view");

  ed_space_img_get_size(simg, &width, &height);
  ed_space_img_get_aspect(simg, &aspx, &aspy);

  w = width * aspx;
  h = height * aspy;

  float xof = 0.0f, yof = 0.0f;
  if ((simg->img == nullptr) || (simg->img->src == IMG_SRC_TILED)) {
    /* Extend the shown area to cover all UDIM tiles. */
    int x_tiles, y_tiles;
    if (simg->img == nullptr) {
      x_tiles = simg->tile_grid_shape[0];
      y_tiles = simg->tile_grid_shape[1];
    }
    else {
      x_tiles = y_tiles = 1;
      LIST_FOREACH (ImgTile *, tile, &simg->img->tiles) {
        int tile_x = (tile->tile_number - 1001) % 10;
        int tile_y = (tile->tile_number - 1001) / 10;
        x_tiles = max_ii(x_tiles, tile_x + 1);
        y_tiles = max_ii(y_tiles, tile_y + 1);
      }
    }
    xof = 0.5f * (x_tiles - 1.0f) * w;
    yof = 0.5f * (y_tiles - 1.0f) * h;
    w *= x_tiles;
    h *= y_tiles;
  }

  /* check if the img will fit in the img with (zoom == 1) */
  width = lib_rcti_size_x(&rgn->winrct) + 1;
  height = lib_rcti_size_y(&rgn->winrct) + 1;

  if (fit_view) {
    const int margin = 5; /* margin from border */

    zoomx = float(width) / (w + 2 * margin);
    zoomy = float(height) / (h + 2 * margin);

    simg_zoom_set(simg, rgn, min_ff(zoomx, zoomy), nullptr, false);
  }
  else {
    if ((w >= width || h >= height) && (width > 0 && height > 0)) {
      zoomx = float(width) / w;
      zoomy = float(height) / h;

      /* find the zoom val that will fit the img in the img space */
      simg_zoom_set(simg, rgn, 1.0f / power_of_2(1.0f / min_ff(zoomx, zoomy)), nullptr, false);
    }
    else {
      simg_zoom_set(simg, rgn, 1.0f, nullptr, false);
    }
  }

  simg->xof = xof;
  simg->yof = yof;
}

bool space_img_main_rgn_poll(Cxt *C)
{
  SpaceImg *simg = cxt_win_space_img(C);
  // ARgn *rgn = cxt_win_rgn(C); /* XXX. */

  if (simg) {
    return true; /* XXX (rgn && rgn->type->rgnid == RGN_TYPE_WIN); */
  }
  return false;
}

/* For IMG_OT_curves_point_set to avoid sampling when in uv smooth mode or editmode */
static bool space_img_main_area_not_uv_brush_poll(Cxt *C)
{
  SpaceImg *simg = cxt_win_space_img(C);
  Scene *scene = cxt_data_scene(C);
  ToolSettings *toolsettings = scene->toolsettings;

  if (simg && !toolsettings->uvsculpt && (cxt_data_edit_ob(C) == nullptr)) {
    return true;
  }

  return false;
}

/* View Pan Op */
struct ViewPanData {
  float x, y;
  float xof, yof;
  int launch_ev;
  bool own_cursor;
};

static void img_view_pan_init(Cxt *C, WinOp *op, const WinEv *ev)
{
  Win *win = cxt_win(C);
  SpaceImg *simg = cxt_win_space_img(C);
  ViewPanData *vpd;

  op->customdata = vpd = static_cast<ViewPanData *>(
      mem_calloc(sizeof(ViewPanData), "ImgViewPanData"));

  /* Grab will be set when running from gizmo. */
  vpd->own_cursor = (win->grabcursor == 0);
  if (vpd->own_cursor) {
    win_cursor_modal_set(win, WIN_CURSOR_NSEW_SCROLL);
  }

  vpd->x = ev->xy[0];
  vpd->y = ev->xy[1];
  vpd->xof = simg->xof;
  vpd->yof = simg->yof;
  vpd->launch_ev = win_userdef_ev_type_from_keymap_type(ev->type);

  win_ev_add_modal_handler(C, op);
}

static void img_view_pan_exit(Cxt *C, WinOp *op, bool cancel)
{
  SpaceImg *simg = cxt_win_space_img(C);
  ViewPanData *vpd = static_cast<ViewPanData *>(op->customdata);

  if (cancel) {
    simg->xof = vpd->xof;
    simg->yof = vpd->yof;
    ed_rgn_tag_redrw(cxt_win_rgn(C));
  }

  if (vpd->own_cursor) {
    win_cursor_modal_restore(cxt_win(C));
  }
  mem_free(op->customdata);
}

static int img_view_pan_ex(Cxt *C, WinOp *op)
{
  SpaceImg *simg = cxt_win_space_im(C);
  float offset[2];

  api_float_get_array(op->ptr, "offset", offset);
  simg->xof += offset[0];
  simg->yof += offset[1];

  ed_rgn_tag_redrw(cxt_win_rgn(C));

  return OP_FINISHED;
}

static int img_view_pan_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  if (ev->type == MOUSEPAN) {
    SpaceImg *simg = cxt_win_space_img(C);
    float offset[2];

    offset[0] = (ev->prev_xy[0] - ev->xy[0]) / simg->zoom;
    offset[1] = (ev->prev_xy[1] - ev->xy[1]) / simg->zoom;
    api_float_set_array(op->ptr, "offset", offset);

    img_view_pan_ex(C, op);
    return OP_FINISHED;
  }

  img_view_pan_init(C, op, ev);
  return OP_RUNNING_MODAL;
}

static int img_view_pan_modal(Cxt *C, WinOp *op, const WinEv *ev)
{
  SpaceImg *simg = cxy_win_space_img(C);
  ViewPanData *vpd = static_cast<ViewPanData *>(op->customdata);
  float offset[2];

  switch (ev->type) {
    case MOUSEMOVE:
      simg->xof = vpd->xof;
      simg->yof = vpd->yof;
      offset[0] = (vpd->x - eve->xy[0]) / simg->zoom;
      offset[1] = (vpd->y - ev->xy[1]) / simg->zoom;
      api_float_set_array(op->ptr, "offset", offset);
      img_view_pan_ex(C, op);
      break;
    default:
      if (ev->type == vpd->launch_eve && ev->val == KM_RELEASE) {
        img_view_pan_exit(C, op, false);
        return OP_FINISHED;
      }
      break;
  }

  return OP_RUNNING_MODAL;
}

static void img_view_pan_cancel(Cxt *C, WinOp *op)
{
  img_view_pan_exit(C, op, true);
}

void IMG_OT_view_pan(WinOpType *ot)
{
  /* ids */
  ot->name = "Pan View";
  ot->idname = "IMG_OT_view_pan";
  ot->description = "Pan the view";

  /* api cb */
  ot->ex = img_view_pan_exec;
  ot->invoke = img_view_pan_invoke;
  ot->modal = img_view_pan_modal;
  ot->cancel = image_view_pan_cancel;
  ot->poll = space_img_main_rgn_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_GRAB_CURSOR_XY | OPTYPE_LOCK_BYPASS;

  /* prope */
  api_def_float_vector(ot->sapi,
                       "offset",
                       2,
                       nullptr,
                       -FLT_MAX,
                       FLT_MAX,
                       "Offset",
                       "Offset in floating-point units, 1.0 is the width and height of the image",
                       -FLT_MAX,
                       FLT_MAX);
}

/* View Zoom Op */
struct ViewZoomData {
  float origx, origy;
  float zoom;
  int launch_ev;
  float location[2];

  /* needed for continuous zoom */
  WinTimer *timer;
  double timer_lastdrw;
  bool own_cursor;

  SpaceImg *simg;
  ARgn *rgn;
};

static void img_view_zoom_init(Cxt *C, WinOp *op, const WinEv *ev)
{
  Win *win = cxt_win(C);
  SpaceImg *simg = cxt_win_space_img(C);
  ARgn *rgn = cxt_win_rgn(C);
  ViewZoomData *vpd;

  op->customdata = vpd = static_cast<ViewZoomData *>(
      mem_calloc(sizeof(ViewZoomData), "ImgViewZoomData"));

  /* Grab will be set when running from gizmo. */
  vpd->own_cursor = (win->grabcursor == 0);
  if (vpd->own_cursor) {
    win_cursor_modal_set(win, WIN_CURSOR_NSEW_SCROLL);
  }

  vpd->origx = ev->xy[0];
  vpd->origy = ev->xy[1];
  vpd->zoom = simh->zoom;
  vpd->launch_ev = win_userdef_ev_type_from_keymap_type(ev->type);

  ui_view2d_rgn_to_view(
      &rgn->v2d, ev->mval[0], ev->mval[1], &vpd->location[0], &vpd->location[1]);

  if (U.viewzoom == USER_ZOOM_CONTINUE) {
    /* needs a timer to continue redrwing */
    vpd->timer = win_ev_timer_add(cxt_wm(C), cxt_win(C), TIMER, 0.01f);
    vpd->timer_lastdrw = PIL_check_seconds_timer();
  }

  vpd->simg = simg;
  vpd->rgn = rgn;

  win_ev_add_modal_handler(C, op);
}

static void img_view_zoom_exit(Cxt *C, WinOp *op, bool cancel)
{
  SpaceImg *simg = cxt_win_space_img(C);
  ViewZoomData *vpd = static_cast<ViewZoomData *>(op->customdata);

  if (cancel) {
    simg->zoom = vpd->zoom;
    ed_rgn_tag_redrw(cxt_win_rgn(C));
  }

  if (vpd->timer) {
    win_ev_timer_remove(cxt_win(C), vpd->timer->win, vpd->timer);
  }

  if (vpd->own_cursor) {
    win_cursor_modal_restore(cxt_win(C));
  }
  mem_free(op->customdata);
}

static int img_view_zoom_ex(Cxt *C, WinOp *op)
{
  SpaceImg *simg = cxt_win_space_img(C);
  ARgn *rgn = cxt_win_rgn(C);

  simg_zoom_set_factor(simg, rgn, api_float_get(op->ptr, "factor"), nullptr, false);

  ed_rgn_tag_redrw(rgn);

  return OP_FINISHED;
}

enum {
  VIEW_PASS = 0,
  VIEW_APPLY,
  VIEW_CONFIRM,
};

static int img_view_zoom_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  if (ELEM(ev->type, MOUSEZOOM, MOUSEPAN)) {
    SpaceImg *simg = cxt_win_space_img(C);
    ARgn *rgn = cxt_win_rgn(C);
    float delta, factor, location[2];

    ui_view2d_rgn_to_view(
        &rgn->v2d, ev->mval[0], ev->mval[1], &location[0], &location[1]);

    delta = ev->prev_xy[0] - ev->xy[0] + ev->prev_xy[1] - ev->xy[1];

    if (U.uiflag & USER_ZOOM_INVERT) {
      delta *= -1;
    }

    factor = 1.0f + delta / 300.0f;
    api_float_set(op->ptr, "factor", factor);
    const bool use_cursor_init = api_bool_get(op->ptr, "use_cursor_init");
    simg_zoom_set(simg,
                  rgn,
                  simg->zoom * factor,
                  location,
                  (use_cursor_init && (U.uiflag & USER_ZOOM_TO_MOUSEPOS)));
    ed_rgn_tag_redrw(rgn);

    return OP_FINISHED;
  }

  img_view_zoom_init(C, op, ev);
  return OP_RUNNING_MODAL;
}

static void img_zoom_apply(ViewZoomData *vpd,
                             WinOp *op,
                             const int x,
                             const int y,
                             const short viewzoom,
                             const short zoom_invert,
                             const bool zoom_to_pos)
{
  float factor;
  float delta;

  if (viewzoom != USER_ZOOM_SCALE) {
    if (U.uiflag & USER_ZOOM_HORIZ) {
      delta = float(x - vpd->origx);
    }
    else {
      delta = float(y - vpd->origy);
    }
  }
  else {
    delta = x - vpd->origx + y - vpd->origy;
  }

  delta /= U.pixelsize;

  if (zoom_invert) {
    delta = -delta;
  }

  if (viewzoom == USER_ZOOM_CONTINUE) {
    double time = PIL_check_seconds_timer();
    float time_step = float(time - vpd->timer_lastdrw);
    float zfac;
    zfac = 1.0f + ((delta / 20.0f) * time_step);
    vpd->timer_lastdrw = time;
    /* this is the final zoom, but instead make it into a factor */
    factor = (vpd->simg->zoom * zfac) / vpd->zoom;
  }
  else {
    factor = 1.0f + delta / 300.0f;
  }

  api_float_set(op->ptr, "factor", factor);
  simg_zoom_set(vpd->simg, vpd->rgn, vpd->zoom * factor, vpd->location, zoom_to_pos);
  ed_rgn_tag_redrw(vpd->rgn);
}

static int img_view_zoom_modal(Cxt *C, WinOp *op, const WinEv *ev)
{
  ViewZoomData *vpd = static_cast<ViewZoomData *>(op->customdata);
  short ev_code = VIEW_PASS;
  int ret = OP_RUNNING_MODAL;

  /* Execute the ev. */
  if (ev->type == MOUSEMOVE) {
    ev_code = VIEW_APPLY;
  }
  else if (ev->type == TIMER) {
    /* Continuous zoom. */
    if (ev->customdata == vpd->timer) {
      ev_code = VIEW_APPLY;
    }
  }
  else if (ev->type == vpd->launch_ev) {
    if (ev->val == KM_RELEASE) {
      ev_code = VIEW_CONFIRM;
    }
  }

  switch (ev_code) {
    case VIEW_APPLY: {
      const bool use_cursor_init = api_bool_get(op->ptr, "use_cursor_init");
      img_zoom_apply(vpd,
                       op,
                       ev->xy[0],
                       ev->xy[1],
                       U.viewzoom,
                       (U.uiflag & USER_ZOOM_INVERT) != 0,
                       (use_cursor_init && (U.uiflag & USER_ZOOM_TO_MOUSEPOS)));
      break;
    }
    case VIEW_CONFIRM: {
      ret = OP_FINISHED;
      break;
    }
  }

  if ((ret & OP_RUNNING_MODAL) == 0) {
    img_view_zoom_exit(C, op, false);
  }

  return ret;
}

static void img_view_zoom_cancel(Cxt *C, WinOp *op)
{
  img_view_zoom_exit(C, op, true);
}

void IMG_OT_view_zoom(WinOpType *ot)
{
  ApiProp *prop;

  /* identifiers */
  ot->name = "Zoom View";
  ot->idname = "IMAGE_OT_view_zoom";
  ot->description = "Zoom in/out the image";

  /* api cbs */
  ot->ex = img_view_zoom_ex;
  ot->invoke = img_view_zoom_invoke;
  ot->modal = img_view_zoom_modal;
  ot->cancel = img_view_zoom_cancel;
  ot->poll = space_img_main_rgn_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_GRAB_CURSOR_XY | OPTYPE_LOCK_BYPASS;

  /* props */
  prop = api_def_float(ot->sapi,
                       "factor",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "Factor",
                       "Zoom factor, vals higher than 1.0 zoom in, lower values zoom out",
                       -FLT_MAX,
                       FLT_MAX);
  api_def_prop_flag(prop, PROP_HIDDEN);

  win_op_props_use_cursor_init(ot);
}

#ifdef WITH_INPUT_NDOF

/* NDOF Op */

/* Combined pan/zoom from a 3D mouse device.
 * Z zooms, XY pans
 * "view" (not "paper") ctrl - user moves the viewpoint, not the image being viewed
 * that explains the negative signs in the code below */

static int img_view_ndof_invoke(Cxt *C, WinOp * /*op*/, const WinEv *ev)
{
  if (ev->type != NDOF_MOTION) {
    return OP_CANCELLED;
  }

  SpaceImg *simg = cxt_win_space_img(C);
  ARgn *rgn = cxt_win_rgn(C);
  float pan_vec[3];

  const WinNDOFMotionData *ndof = static_cast<const WinNDOFMotionData *>(ev->customdata);
  const float pan_speed = NDOF_PIXELS_PER_SECOND;

  win_ev_ndof_pan_get(ndof, pan_vec, true);

  mul_v3_fl(pan_vec, ndof->dt);
  mul_v2_fl(pan_vec, pan_speed / simg->zoom);

  simg_zoom_set_factor(simg, rgn, max_ff(0.0f, 1.0f - pan_vec[2]), nullptr, false);
  simg->xof += pan_vec[0];
  simg->yof += pan_vec[1];

  ed_rgn_tag_redrw(rgn);

  return OP_FINISHED;
}

void IMG_OT_view_ndof(WinOpType *ot)
{
  /* ids */
  ot->name = "NDOF Pan/Zoom";
  ot->idname = "IMG_OT_view_ndof";
  ot->description = "Use a 3D mouse device to pan/zoom the view";

  /* api cbs */
  ot->invoke = img_view_ndof_invoke;
  ot->poll = space_img_main_rgn_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;
}

#endif /* WITH_INPUT_NDOF */

/* View All Op */

/* Updates the fields of the View2D member of the SpaceImg struct.
 * Default behavior is to reset the position of the img and set the zoom to 1
 * If the img will not fit within the win rectangle, the zoom is adjusted */

static int img_view_all_ex(Cxt *C, WinOp *op)
{
  SpaceImg *simg;
  ARgn *rgn;

  /* retrieve state */
  simg = cxt_win_space_img(C);
  rgn = cxt_win_rgn(C);

  img_view_all(simg, rgn, op);

  ed_rgn_tag_redrw(rgn);

  return OP_FINISHED;
}

void IMG_OT_view_all(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Frame All";
  ot->idname = "IMG_OT_view_all";
  ot->description = "View the entire img";

  /* api cbs  */
  ot->ex = img_view_all_ex;
  ot->poll = space_img_main_rgn_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;

  /* props */
  prop = api_def_bool(ot->sapi, "fit_view", false, "Fit View", "Fit frame to the viewport");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* Cursor To Center View Op */
static int view_cursor_center_ex(Cxt *C, WinOp *op)
{
  SpaceImg *simg;
  ARgn *rgn;

  simg = cxt_win_space_img(C);
  rgn = cxt_win_region(C);

  img_view_all(simg, rgn, op);

  simg->cursor[0] = 0.5f;
  simg->cursor[1] = 0.5f;

  /* Needed for updating the cursor. */
  wib_ev_add_notifier(C, NC_SPACE | ND_SPACE_IMG, nullptr);

  return OP_FINISHED;
}

void IMG_OT_view_cursor_center(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Cursor To Center View";
  ot->description = "Set 2D Cursor To Center View location";
  ot->idname = "IMG_OT_view_cursor_center";

  /* api cbs */
  ot->ex = view_cursor_center_ex;
  ot->poll = ed_space_img_cursor_poll;

  /* props */
  prop = api_def_bool(ot->sapi, "fit_view", false, "Fit View", "Fit frame to the viewport");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* Center View To Cursor Op */
static int view_center_cursor_ex(Cxt *C, WinOp * /*op*/)
{
  SpaceImg *simg = cxt_win_space_img(C);
  ARgn *rgn = cxt_win_rgn(C);
    
  ed_img_view_center_to_point(simg, simg->cursor[0], simg->cursor[1]);

  ed_rgn_tag_redrw(rgn);

  return OP_FINISHED;
}

void IMG_OT_view_center_cursor(WinOpType *ot)
{
  /* ids */
  ot->name = "Center View to Cursor";
  ot->description = "Center the view so that the cursor is in the middle of the view";
  ot->idname = "IMG_OT_view_center_cursor";

  /* api cbs */
  ot->ex = view_center_cursor_ex;
  ot->poll = ed_space_img_cursor_poll;
}

/* Frame Sel Op */
static int img_view_sel_ex(Cxt *C, WinOp * /*op*/)
{
  SpaceImg *simg;
  ARgn *rgn;
  Scene *scene;
  ViewLayer *view_layer;
  Ob *obedit;

  /* retrieve state */
  simg = cxt_win_space_img(C);
  rgn = cxt_win_rgn(C);
  scene = cxt_data_scene(C);
  view_layer = cxt_data_view_layer(C);
  obedit = cxt_data_edit_ob(C);

  /* get bounds */
  float min[2], max[2];
  if (ed_space_img_show_uvedit(simg, obedit)) {
    uint objs_len = 0;
    Ob **objs = dune_view_layer_array_from_objs_in_edit_mode_unique_data_with_uvs(
        scene, view_layer, ((View3D *)nullptr), &objs_len);
    bool success = ed_uvedit_minmax_multi(scene, objs, objs_len, min, max);
    mem_free(objs);
    if (!success) {
      return OP_CANCELLED;
    }
  }
  else if (ed_space_img_check_show_maskedit(simg, obedit)) {
    if (!ed_mask_sel_minmax(C, min, max, false)) {
      return OP_CANCELLED;
    }
  }
  rctf bounds{};
  bounds.xmin = min[0];
  bounds.ymin = min[1];
  bounds.xmax = max[0];
  bounds.ymax = max[1];

  /* add some margin */
  lib_rctf_scale(&bounds, 1.4f);

  simg_zoom_set_from_bounds(simg, rgn, &bounds);

  ed_rgn_tag_redrw(rgn);

  return OP_FINISHED;
}

static bool img_view_sel_poll(Cxt *C)
{
  return (space_img_main_rgn_poll(C) && (ed_op_uvedit(C) || ed_maskedit_poll(C)));
}

void IMG_OT_view_sel(WinOpType *ot)
{
  /* ids */
  ot->name = "View Center";
  ot->idname = "IMG_OT_view_sel";
  ot->description = "View all sel UVs";

  /* api cbs */
  ot->ex = img_view_sel_ex;
  ot->poll = img_view_sel_poll;
}

/* View Zoom In/Out Op */
static int img_view_zoom_in_ex(Cxt *C, WinOp *op)
{
  SpaceImg *simg = cxt_win_space_img(C);
  ARgn *rgn = cxt_win_rgn(C);
  float location[2];

  api_float_get_array(op->ptr, "location", location);

  simg_zoom_set_factor(
      simg, rgn, powf(2.0f, 1.0f / 3.0f), location, U.uiflag & USER_ZOOM_TO_MOUSEPOS);

  ed_rgn_tag_redrw(rgn);

  return OP_FINISHED;
}

static int img_view_zoom_in_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  ARgn *rgn = cxt_win_rgn(C);
  float location[2];

  ui_view2d_rgn_to_view(
      &rgn->v2d, ev->mval[0], ev->mval[1], &location[0], &location[1]);
  api_float_set_array(op->ptr, "location", location);

  return img_view_zoom_in_ex(C, op);
}

void IMG_OT_view_zoom_in(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Zoom In";
  ot->idname = "IMG_OT_view_zoom_in";
  ot->description = "Zoom in the img (centered around 2D cursor)";

  /* api cbs */
  ot->invoke = img_view_zoom_in_invoke;
  ot->ex = img_view_zoom_in_ex;
  ot->poll = space_img_main_rgn_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;

  /* props */
  prop = api_def_float_vector(ot->sapi,
                              "location",
                              2,
                              nullptr,
                              -FLT_MAX,
                              FLT_MAX,
                              "Location",
                              "Cursor location in screen coordinates",
                              -10.0f,
                              10.0f);
  api_def_prop_flag(prop, PROP_HIDDEN);
}

static int img_view_zoom_out_ex(Cxt *C, WinOp *op)
{
  SpaceImg *simg = cxt_win_space_img(C);
  ARgn *rgn = cxt_win_rgn(C);
  float location[2];

  api_float_get_array(op->ptr, "location", location);

  simg_zoom_set_factor(
      simg, rgn, powf(0.5f, 1.0f / 3.0f), location, U.uiflag & USER_ZOOM_TO_MOUSEPOS);

  ed_rgn_tag_redrw(rgn);

  return OP_FINISHED;
}

static int img_view_zoom_out_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  ARgn *rgn = cxt_win_rgn(C);
  float location[2];

  ui_view2d_rgn_to_view(
      &rgn->v2d, ev->mval[0], ev->mval[1], &location[0], &location[1]);
  api_float_set_array(op->ptr, "location", location);

  return img_view_zoom_out_ex(C, op);
}

void IMG_OT_view_zoom_out(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Zoom Out";
  ot->idname = "IMG_OT_view_zoom_out";
  ot->description = "Zoom out the img (centered around 2D cursor)";

  /* api cbs */
  ot->invoke = img_view_zoom_out_invoke;
  ot->ex = img_view_zoom_out_ex;
  ot->poll = space_img_main_region_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;

  /* props*/
  prop = api_def_float_vector(ot->sapi,
                              "location",
                              2,
                              nullptr,
                              -FLT_MAX,
                              FLT_MAX,
                              "Location",
                              "Cursor location in screen coordinates",
                              -10.0f,
                              10.0f);
  api_def_prop_flag(prop, PROP_HIDDEN);
}

/* View Zoom Ratio Op */
static int img_view_zoom_ratio_ex(Cxt *C, WinOp *op)
{
  SpaceImg *simg = cxt_win_space_img(C);
  ARgn *rgn = cxt_win_rgn(C);

  simg_zoom_set(simg, rgn, api_float_get(op->ptr, "ratio"), nullptr, false);

  /* ensure pixel exact locations for draw */
  simg->xof = int(simg->xof);
  simg->yof = int(simg->yof);

  ed_rgn_tag_redrw(rgn);

  return OP_FINISHED;
}

void IMG_OT_view_zoom_ratio(WinOpType *ot)
{
  /* ids */
  ot->name = "View Zoom Ratio";
  ot->idname = "IMG_OT_view_zoom_ratio";
  ot->description = "Set zoom ratio of the view";

  /* api cbs */
  ot->ex = img_view_zoom_ratio_exe;
  ot->poll = space_img_main_rgn_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;

  /* props */
  api_def_float(ot->sapi,
                "ratio",
                0.0f,
                -FLT_MAX,
                FLT_MAX,
                "Ratio",
                "Zoom ratio, 1.0 is 1:1, higher is zoomed in, lower is zoomed out",
                -FLT_MAX,
                FLT_MAX);
}

/* View Border-Zoom Op */
static int img_view_zoom_border_ex(Cxt *C, WinOp *op)
{
  SpaceImg *simg = cxt_win_space_img(C);
  ARgn *rgn = cxt_win_rgn(C);
  rctf bounds;
  const bool zoom_in = !api_bool_get(op->ptr, "zoom_out");

  win_op_props_border_to_rctf(op, &bounds);

  ui_view2d_rgn_to_view_rctf(&rgn->v2d, &bounds, &bounds);

  struct {
    float xof;
    float yof;
    float zoom;
  } simg_view_prev{};
  simg_view_prev.xof = simg->xof;
  simg_view_prev.yof = simg->yof;
  simg_view_prev.zoom = simg->zoom;

  simg_zoom_set_from_bounds(simg, rgn, &bounds);

  /* zoom out */
  if (!zoom_in) {
    simg->xof = simg_view_prev.xof + (simg->xof - simg_view_prev.xof);
    simg->yof = simg_view_prev.yof + (simg->yof - simg_view_prev.yof);
    simg->zoom = simg_view_prev.zoom * (simg_view_prev.zoom / simg->zoom);
  }

  ed_rgn_tag_redrw(rgn);

  return OP_FINISHED;
}

void IMG_OT_view_zoom_border(WinOpType *ot)
{
  /* ids */
  ot->name = "Zoom to Border";
  ot->description = "Zoom in the view to the nearest item contained in the border";
  ot->idname = "IMG_OT_view_zoom_border";

  /* api cbs */
  ot->invoke = win_gesture_box_invoke;
  ot->ex = img_view_zoom_border_ex;
  ot->modal = win_gesture_box_modal;
  ot->cancel = win_gesture_box_cancel;

  ot->poll = space_img_main_rgn_poll;

  /* api */
  win_op_props_gesture_box_zoom(ot);
}

/* load/replace/save cbs */
static void img_filesel(Cxt *C, WinOp *op, const char *path)
{
  api_string_set(op->ptr, "filepath", path);
  win_ev_add_filesel(C, op);


/* Open Img Op */
struct ImgOpenData {
  ApiPropPtr pprop;
  ImgUser *iuser;
  ImgFormatData im_format;
};

static void img_open_init(Cxt *C, WinOp *op)
{
  ImgOpenData *iod;
  op->customdata = iod = static_cast<ImgOpenData *>(
      mem_calloc(sizeof(ImgOpenData), __func__));
  iod->iuser = static_cast<ImgUser *>(
      cxt_data_ptr_get_type(C, "img_user", &ApiImgUser).data);
  ui_cxt_active_btn_prop_get_templateId(C, &iod->pprop.ptr, &iod->pprop.prop);
}

static void img_open_cancel(Cxt * /*C*/, WinOp *op)
{
  mem_free(op->customdata);
  op->customdata = nullptr;
}

static Img *img_open_single(Main *bmain,
                            WinOp *op,
                            ImgFrameRange *range,
                            bool use_multiview)
{
  bool exists = false;
  Img *img = nullptr;

  errno = 0;
  img = dune_img_load_exists_ex(main, range->filepath, &exists);

  if (!img) {
    if (op->customdata) {
      mem_free(op->customdata);
    }
    dune_reportf(op->reports,
                RPT_ERROR,
                "Cannot read '%s': %s",
                range->filepath,
                errno ? strerror(errno) : TIP_("unsupported img format"));
    return nullptr;
  }

  /* If img alrdy exists, update its file path based on relative path prop, see: #109561. */
  if (exists) {
    STRNCPY(img->filepath, range->filepath);
    return img;
  }

  /* handle multiview imgs */
  if (use_multiview) {
    ImgOpenData *iod = static_cast<ImgOpenData *>(op->customdata);
    ImgFormatData *imf = &iod->im_format;

    img->flag |= IMG_USE_VIEWS;
    img->views_format = imf->views_format;
    *img->stereo3d_format = imf->stereo3d_format;
  }
  else {
    img->flag &= ~IMG_USE_VIEWS;
    dune_img_free_views(img);
  }

  if (img->src == IMG_SRC_FILE) {
    if (range->udims_detected && range->udim_tiles.first) {
      img->src = IMG_SRC_TILED;
      ImgTile *first_tile = static_cast<ImgTile *>(img->tiles.first);
      first_tile->tile_number = range->offset;
      LIST_FOREACH (LinkData *, node, &range->udim_tiles) {
        dune_img_add_tile(img, PTR_AS_INT(node->data), nullptr);
      }
    }
    else if (range->length > 1) {
      img->src = IMG_SRC_SEQ;
    }
  }

  return img;
}

static int img_open_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  ScrArea *area = cxt_wm_area(C);
  Scene *scene = cxt_data_scene(C);
  ImgUser *iuser = nullptr;
  Img *img = nullptr;
  int frame_seq_len = 0;
  int frame_ofs = 1;

  const bool use_multiview = api_bool_get(op->ptr, "use_multiview");
  const bool use_udim = api_bool_get(op->ptr, "use_udim_detecting");

  if (!op->customdata) {
    img_open_init(C, op);
  }

  List ranges = ed_img_filesel_detect_seqs(main, op, use_udim);
  LIST_FOREACH (ImgFrameRange *, range, &ranges) {
    Img *img_range = img_open_single(main, op, range, use_multiview);

    /* take the first image */
    if ((img == nullptr) && img_range) {
      img = img_range;
      frame_seq_len = range->length;
      frame_ofs = range->offset;
    }

    lib_freelist(&range->udim_tiles);
  }
  lib_freelist(&ranges);

  if (img == nullptr) {
    return OP_CANCELLED;
  }

  /* hook into UI */
  ImgOpenData *iod = static_cast<ImgOpenData *>(op->customdata);

  if (iod->pprop.prop) {
    /* when creating new Id blocks, use is already 1, but API
     * ptr use also increases user, so this compensates it */
    id_us_min(&img->id);

    ApiPtr imgptr = api_id_ptr_create(&img->id);
    api_prop_ptr_set(&iod->pprop.ptr, iod->pprop.prop, imgptr, nullptr);
    api_prop_update(C, &iod->pprop.ptr, iod->pprop.prop);
  }

  if (iod->iuser) {
    iuser = iod->iuser;
  }
  else if (area && area->spacetype == SPACE_IMG) {
    SpaceImg *simg = static_cast<SpaceImg *>(area->spacedata.first);
    ed_space_img_set(main, simg, img, false);
    iuser = &simg->iuser;
  }
  else {
    Tex *tex = static_cast<Tex *>(cxt_data_ptr_get_type(C, "texture", &ApiTexture).data);
    if (tex && tex->type == TEX_IMG) {
      iuser = &tex->iuser;
    }

    if (iuser == nullptr) {
      Camera *cam = static_cast<Camera *>(
          cxt_data_ptr_get_type(C, "camera", ApiCamera).data);
      if (cam) {
        LIST_FOREACH (CameraBGImg *, bgpic, &cam->bg_imgs) {
          if (bgpic->img == img) {
            iuser = &bgpic->iuser;
            break;
          }
        }
      }
    }
  }

  /* init bc of new img */
  if (iuser) {
    /* If the seq was a tiled img, we only have one frame. */
    iuser->frames = (img->stc == IMG_SRC_SEQ) ? frame_seq_len : 1;
    iuser->sfra = 1;
    iuser->framenr = 1;
    if (img->src == IMG_SRC_MOVIE) {
      iuser->offset = 0;
    }
    else {
      iuser->offset = frame_ofs - 1;
    }
    iuser->scene = scene;
    dune_img_init_imgeuser(img, iuser);
  }

  /* dune_packedfile_unpack_img frees img bufs */
  ed_preview_kill_jobs(cxt_wm(C), main);

  dune_img_signal(main, img, iuser, IMG_SIGNAL_RELOAD);
  win_ev_add_notifier(C, NC_IMG | NA_EDITED, img);

  mem_free(op->customdata);

  return OP_FINISHED;
}

static int img_open_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  SpaceImg *simg = cxt_win_space_img(C); /* XXX other space types can call */
  const char *path = U.textudir;
  Img *img = nullptr;
  Scene *scene = cxt_data_scene(C);

  if (simg) {
    img = simg->img;
  }

  if (img == nullptr) {
    Tex *tex = static_cast<Tex *>(cxt_data_ptr_get_type(C, "texture", &ApiTexture).data);
    if (tex && tex->type == TEX_IMG) {
      img = tex->img;
    }
  }

  if (img == nullptr) {
    ApiPtr ptr;
    ApiProp *prop;

    /* hook into UI */
    ui_cxt_active_btn_prop_get_templateId(C, &ptr, &prop);

    if (prop) {
      ApiPtr oldptr;
      Img *oldimg;

      oldptr = api_prop_ptr_get(&ptr, prop);
      oldimg = (Img *)oldptr.owner_id;
      /* unlikely to fail but better avoid strange crash */
      if (oldimg && GS(oldimg->id.name) == ID_IM) {
        img = oldimg;
      }
    }
  }

  if (img) {
    path = img->filepath;
  }

  if (api_struct_prop_is_set(op->ptr, "filepath")) {
    return img_open_ex(C, op);
  }

  img_open_init(C, op);

  /* Show multi-view save options only if scene has multi-views. */
  ApiProp *prop;
  prop = api_struct_find_prop(op->ptr, "show_multiview");
  api_prop_bool_set(op->ptr, prop, (scene->r.scemode & R_MULTIVIEW) != 0);

  img_filesel(C, op, path);

  return OP_RUNNING_MODAL;
}

static bool img_open_drw_check_prop(ApiPtr * /*ptr*/,
                                    ApiProp *prop,
                                    void * /*user_data*/)
{
  const char *prop_id = api_prop_id(prop);

  return !STR_ELEM(prop_id, "filepath", "directory", "filename");
}

static void img_open_drw(Cxt * /*C*/, WinOp *op)
{
  uiLayout *layout = op->layout;
  ImgOpenData *iod = static_cast<ImgOpenData *>(op->customdata);
  ImgFormatData *imf = &iod->im_format;

  /* main draw call */
  uiDefAutoBtnsApi(layout,
                   op->ptr,
                   img_open_drw_check_prop,
                   nullptr,
                   nullptr,
                   UI_BTN_LABEL_ALIGN_NONE,
                   false);

  /* image template */
  PointerRNA imf_ptr = api_ptr_create(nullptr, &ApiImgFormatSettings, imf);

  /* multiview template */
  if (api_bool_get(op->ptr, "show_multiview")) {
    uiTemplateImgFormatViews(layout, &imf_ptr, op->ptr);
  }
}

static void img_op_prop_allow_tokens(WinOpType *ot)
{
  ApiProp *prop = api_def_bool(
      ot->sapi, "allow_path_tokens", true, "", "Allow the path to contain substitution tokens");
  api_def_prop_flag(prop, PROP_HIDDEN);
}

void IMG_OT_open(WinOpType *ot)
{
  /* ids */
  ot->name = "Open Img";
  ot->description = "Open img";
  ot->idname = "IMG_OT_open";

  /* api cbs */
  ot->ex = img_open_ex;
  ot->invoke = img_open_invoke;
  ot->cancel = img_open_cancel;
  ot->ui = img_open_drw;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  img_op_prop_allow_tokens(ot);
  win_op_props_filesel(ot,
                       FILE_TYPE_FOLDER | FILE_TYPE_IMG | FILE_TYPE_MOVIE,
                       FILE_SPECIAL,
                       FILE_OPENFILE,
                       WIN_FILESEL_FILEPATH | WIN_FILESEL_DIRECTORY | WM_FILESEL_FILES |
                       WIN_FILESEL_RELPATH,
                       FILE_DEFAULTDISPLAY,
                       FILE_SORT_DEFAULT);

  api_def_bool(
      ot->sapi,
      "use_seq_detection",
      true,
      "Detect Seqs",
      "Automatically detect animd seqs in seld imgs (based on file names)");
  api_def_bool(ot->sapi,
                  "use_udim_detecting",
                  true,
                  "Detect UDIMs",
                  "Detect seld UDIM files and load all matching tiles");
}

/* Browse Img Op */
static int img_file_browse_ex(Cxt *C, WinOp *op)
{
  Img *img = static_cast<Img *>(op->customdata);
  if (img == nullptr) {
    return OP_CANCELLED;
  }

  char filepath[FILE_MAX];
  api_string_get(op->ptr, "filepath", filepath);

  /* If loading into a tiled texture, ensure that the filename is tokenized. */
  if (img->src == IMG_SRC_TILED) {
    dune_img_ensure_tile_token(filepath, sizeof(filepath));
  }

  ApiProp *imgprop;
  ApiPtr imgptr = api_id_ptr_create(&img->id);
  imgprop = api_struct_find_prop(&imgptr, "filepath");

  api_prop_string_set(&imgptr, imgprop, filepath);
  api_prop_update(C, &imgptr, imgprop);

  return OP_FINISHED;
}

static int image_file_browse_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  Img *img = img_from_cxt(C);
  if (!img) {
    return OP_CANCELLED;
  }

  char filepath[FILE_MAX];
  STRNCPY(filepath, img->filepath);

  /* Shift+Click to open the file, Alt+Click to browse a folder in the OS's browser. */
  if (ev->mod & (KM_SHIFT | KM_ALT)) {
    WinOpType *ot = win_optype_find("WIN_OT_path_open", true);
    ApiPtr props_ptr;

    if (ev->mod & KM_ALT) {
      char *lslash = (char *)lib_path_slash_rfind(filepath);
      if (lslash) {
        *lslash = '\0';
      }
    }
    else if (img->src == IMG_SRC_TILED) {
      ImgUser iuser = img_user_from_cxt_and_active_tile(C, img);
      dune_img_user_file_path(&iuser, img, filepath);
    }

    win_op_props_create_ptr(&props_ptr, ot);
    api_string_set(&props_ptr, "filepath", filepath);
    win_op_name_call_ptr(C, ot, WIN_OP_EXEC_DEFAULT, &props_ptr, nullptr);
    win_op_props_free(&props_ptr);

    return OP_CANCELLED;
  }

  /* The img is typically passed to the op via layout/btn cxt (e.g.
   * uiLayoutSetCxtPtr()). The File Browser doesn't support restoring this cxt
   * when calling `ex()` though, so we have to pass it the img via custom data. */
  op->customdata = img;

  img_filesel(C, op, filepath);

  return OP_RUNNING_MODAL;
}

static bool img_file_browse_poll(Cxt *C)
{
  return img_from_cxt(C) != nullptr;
}

void IMG_OT_file_browse(WinOpType *ot)
{
  /* ids */
  ot->name = "Browse Img";
  ot->description =
      "Open an img file browser, hold Shift to open the file, Alt to browse containing "
      "directory";
  ot->idname = "IMG_OT_file_browse";

  /* api cbs */
  ot->ex = img_file_browse_ex;
  ot->invoke = img_file_browse_invoke;
  ot->poll = img_file_browse_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* props */
  win_op_props_filesel(ot,
                       FILE_TYPE_FOLDER | FILE_TYPE_IMG | FILE_TYPE_MOVIE,
                       FILE_SPECIAL,
                       FILE_OPENFILE,
                       WIN_FILESEL_FILEPATH | WIN_FILESEL_RELPATH,
                       FILE_DEFAULTDISPLAY,
                       FILE_SORT_DEFAULT);
}

/* Match Movie Length Op */
static int img_match_len_ex(Cxt *C, WinOp * /*op*/)
{
  Scene *scene = cxt_data_scene(C);
  Img *img = img_from_cxt(C);
  ImgUser *iuser = img_user_from_cxt(C);

  if (!img || !iuser) {
    /* Try to get a Texture, or a SpaceImg from cxt... */
    Tex *tex = static_cast<Tex *>(cxt_data_ptr_get_type(C, "texture", &ApiTexture).data);
    if (tex && tex->type == TEX_IMG) {
      img = tex->img;
      iuser = &tex->iuser;
    }
  }

  if (!img || !iuser || !dune_img_has_anim(img)) {
    return OP_CANCELLED;
  }

  anim *anim = ((ImgAnim *)img->anims.first)->anim;
  if (!anim) {
    return OP_CANCELLED;
  }
  iuser->frames = imb_anim_get_duration(anim, IMB_TC_RECORD_RUN);
  dune_img_user_frame_calc(img, iuser, scene->r.cfra);

  return OP_FINISHED;
}

void IMG_OT_match_movie_length(WinOpType *ot)
{
  /* ids */
  ot->name = "Match Movie Length";
  ot->description = "Set image's user's length to the one of this video";
  ot->idname = "IMG_OT_match_movie_length";

  /* api bs */
  ot->ex = img_match_len_ex;

  /* flags */
  /* Don't think we need undo for that. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_INTERNAL /* | OPTYPE_UNDO */;
}

/* Replace Img Op */

static int img_replace_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  SpaceImg *simg = cxt_win_space_img(C);
  char filepath[FILE_MAX];

  if (!simg->img) {
    return OP_CANCELLED;
  }

  api_string_get(op->ptr, "filepath", filepath);

  /* we can't do much if the filepath is longer than FILE_MAX :/ */
  STRNCPY(simg->img->filepath, filepath);

  if (simg->img->src == IMG_SRC_GENERATED) {
    simg->img->src = IMG_SRC_FILE;
    dune_img_signal(main, simg->img, &simg->iuser, IMG_SIGNAL_SRC_CHANGE);
  }

  if (lib_path_extension_check_array(filepath, imb_ext_movie)) {
    simg->img->src = IMG_SRC_MOVIE;
  }
  else {
    simg->img->src = IMG_SRC_FILE;
  }

  /* dune_packedfile_unpack_img frees img bufs */
  ed_preview_kill_jobs(cxt_wm(C), cxt_data_main(C));

  dune_icon_changed(dune_icon_id_ensure(&simg->img->id));
  dune_img_signal(main, simg->img, &simg->iuser, IMG_SIGNAL_RELOAD);
  win_ev_add_notifier(C, NC_IMG | NA_EDITED, simg->img);

  return OP_FINISHED;
}

static int img_replace_invoke(Cxt *C, WinOp *op, const WinEv * /*event*/)
{
  SpaceImg *simg = cxt_win_space_img(C);

  if (!simg->img) {
    return OP_CANCELLED;
  }

  if (api_struct_prop_is_set(op->ptr, "filepath")) {
    return img_replace_ex(C, op);
  }
    
  if (!api_struct_prop_is_set(op->ptr, "relative_path")) {
    api_bool_set(op->ptr, "relative_path", lib_path_is_rel(simg->img->filepath));
  }

  img_filesel(C, op, sima->img->filepath);

  return OP_RUNNING_MODAL;
}

void IMG_OT_replace(WinOpType *ot)
{
  /* ids */
  ot->name = "Replace Img";
  ot->idname = "IMG_OT_replace";
  ot->description = "Replace current image by another one from disk";

  /* api cbs */
  ot->ex = img_replace_ex;
  ot->invoke = img_replace_invoke;
  ot->poll = img_not_packed_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  win_op_props_filesel(ot,
                       FILE_TYPE_FOLDER | FILE_TYPE_IMG | FILE_TYPE_MOVIE,
                       FILE_SPECIAL,
                       FILE_OPENFILE,
                       WIN_FILESEL_FILEPATH | WIN_FILESEL_RELPATH,
                       FILE_DEFAULTDISPLAY,
                       FILE_SORT_DEFAULT);
}

/* Save Img As Op */
struct ImgSaveData {
  ImgUser *iuser;
  Img *img;
  ImgSaveOptions opts;
};

static void img_save_options_from_op(Main *main, ImgSaveOptions *opts, WinOp *op)
{
  if (api_struct_prop_is_set(op->ptr, "filepath")) {
    api_string_get(op->ptr, "filepath", opts->filepath);
    lib_path_abs(opts->filepath, dune_main_dunefile_path(main));
  }

  opts->relative = (api_struct_find_prop(op->ptr, "relative_path") &&
                    api_bool_get(op->ptr, "relative_path"));
  opts->save_copy = api_struct_find_prop(op->ptr, "copy") &&
                     api_bool_get(op->ptr, "copy"));
  opts->save_as_render = (api_struct_find_prop(op->ptr, "save_as_render") &&
                          api_bool_get(op->ptr, "save_as_render"));
}

static bool save_img_op(
    Main *main, Img *img, ImgUser *iuser, WinOp *op, const ImgSaveOptions *opts)
{
  win_cursor_wait(true);

  bool ok = dune_img_save(op->reports, main, img, iuser, opts);

  win_cursor_wait(false);

  /* Remember file path for next save. */
  STRNCPY(G.filepath_last_img, opts->filepath);

  win_main_add_notifier(NC_IMG | NA_EDITED, img);

  return ok;
}

static ImgSaveData *img_save_as_init(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Img *img = img_from_cxt(C);
  ImgUser *iuser = img_user_from_cxt(C);
  Scene *scene = cxt_data_scene(C);

  ImgSaveData *isd = static_cast<ImgSaveData *>(mem_calloc(sizeof(*isd), __func__));
  isd->img = img;
  isd->iuser = iuser;

  if (!dune_img_save_options_init(&isd->opts, main, scene, img, iuser, true, false)) {
    dune_img_save_options_free(&isd->opts);
    mem_free(isd);
    return nullptr;
  }

  isd->opts.do_newpath = true;

  if (!api_struct_prop_is_set(op->ptr, "filepath")) {
    api_string_set(op->ptr, "filepath", isd->opts.filepath);
  }

  /* Enable save_copy by default for render results. */
  if (img->src == IMG_SRC_VIEWER && !api_struct_prop_is_set(op->ptr, "copy")) {
    api_bool_set(op->ptr, "copy", true);
  }

  if (!api_struct_prop_is_set(op->ptr, "save_as_render")) {
    api_bool_set(op->ptr, "save_as_render", isd->opts.save_as_render);
  }

  /* Show multi-view save options only if image has multi-views. */
  ApiProp *prop;
  prop = api_struct_find_prop(op->ptr, "show_multiview");
  api_prop_bool_set(op->ptr, prop, dune_img_is_multiview(img));
  prop = api_struct_find_prop(op->ptr, "use_multiview");
  api_prop_bool_set(op->ptr, prop, dune_img_is_multiview(img));

  op->customdata = isd;

  return isd;
}

static void img_save_as_free(WinOp *op)
{
  if (op->customdata) {
    ImgSaveData *isd = static_cast<ImgSaveData *>(op->customdata);
    dune_img_save_options_free(&isd->opts);

    mem_free le(op->customdata);
    op->customdata = nullptr;
  }
}

static int img_save_as_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  ImgSaveData *isd;

  if (op->customdata) {
    isd = static_cast<ImgSaveData *>(op->customdata);
  }
  else {
    isd = img_save_as_init(C, op);
    if (isd == nullptr) {
      return OP_CANCELLED;
    }
  }

  img_save_options_from_op(main, &isd->opts, op);
  dune_img_save_options_update(&isd->opts, isd->img);

  save_img_op(main, isd->img, isd->iuser, op, &isd->opts);

  if (isd->opts.save_copy == false) {
    dune_img_free_packedfiles(isd->img);
  }

  img_save_as_free(op);

  return OP_FINISHED;
}

static bool img_save_as_check(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  ImgSaveData *isd = static_cast<ImgSaveData *>(op->customdata);

  img_save_options_from_op(main, &isd->opts, op);
  dune_img_save_options_update(&isd->opts, isd->img);

  return win_op_filesel_ensure_ext_imtype(op, &isd->opts.im_format);
}

static int img_save_as_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  if (api_struct_prop_is_set(op->ptr, "filepath")) {
    return img_save_as_ex(C, op);
  }

  ImgSaveData *isd = img_save_as_init(C, op);
  if (isd == nullptr) {
    return OP_CANCELLED;
  }

  img_filesel(C, op, isd->opts.filepath);

  return OP_RUNNING_MODAL;
}

static void img_save_as_cancel(Cxt * /*C*/, WinOp *op)
{
  img_save_as_free(op);
}

static bool img_save_as_drw_check_prop(ApiPtr *ptr, ApiProp *prop, void *user_data)
{
  ImgSaveData *isd = static_cast<ImgSaveData *>(user_data);
  const char *prop_id = api_prop_id(prop);

  return !(STREQ(prop_id, "filepath") || STREQ(prop_id, "directory") ||
           STREQ(prop_id, "filename") ||
           /* when saving a copy, relative path has no effect */
           (STREQ(prop_id, "relative_path") && api_bool_get(ptr, "copy")) ||
           (STREQ(prop_id, "save_as_render") && isd->img->source == IMG_SRC_VIEWER));
}

static void img_save_as_drw(Cxt * /*C*/, WinOp *op)
{
  uiLayout *layout = op->layout;
  ImgSaveData *isd = static_cast<ImgSaveData *>(op->customdata);
  const bool is_multiview = api_bool_get(op->ptr, "show_multiview");
  const bool save_as_render = api_bool_get(op->ptr, "save_as_render");

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  /* Operator settings. */
  uiDefAutoBtnsApi(layout,
                   op->ptr,
                   img_save_as_drw_check_prop,
                   isd,
                   nullptr,
                   BTN_LABEL_ALIGN_NONE,
                   false);

  uiItemS(layout);

  /* Img format settings. */
  ApiPtr imf_ptr = api_ptr_create(nullptr, &ApiImgFormatSettings, &isd->opts.im_format);
  uiTemplateImgSettings(layout, &imf_ptr, save_as_render);

  if (!save_as_render) {
    ApiPtr linear_settings_ptr = api_pt_get(&imf_ptr, "linear_colorspace_settings");
    uiLayout *col = uiLayoutColumn(layout, true);
    uiItemS(col);
    uiItemR(col, &linear_settings_ptr, "name", UI_ITEM_NONE, IFACE_("Color Space"), ICON_NONE);
  }

  /* Multiview settings. */
  if (is_multiview) {
    uiTemplateImgFormatViews(layout, &imf_ptr, op->ptr);
  }
}

static bool img_save_as_poll(Cxt *C)
{
  if (!img_from_cxt_has_data_poll(C)) {
    return false;
  }

  if (G.is_rendering) {
    /* no need to nullptr check here */
    Img *img = img_from_cxt(C);

    if (img->src == IMG_SRC_VIEWER) {
      cxt_win_op_poll_msg_set(C, "can't save image while rendering");
      return false;
    }
  }

  return true;
}

void IMG_OT_save_as(WinOpType *ot)
{
  /* ids */
  ot->name = "Save As Img";
  ot->idname = "IMG_OT_save_as";
  ot->description = "Save the image with another name and/or settings";

  /* api cbs */
  ot->ex = img_save_as_ex;
  ot->check = img_save_as_check;
  ot->invoke = img_save_as_invoke;
  ot->cancel = img_save_as_cancel;
  ot->ui = img_save_as_drw;
  ot->poll = img_save_as_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ApiProp *prop;
  prop = api_def_bool(
      ot->sapi,
      "save_as_render",
      false,
      "Save As Render",
      "Save img with render color management.\n"
      "For display img formats like PNG, apply view and display transform.\n"
      "For intermediate image formats like OpenEXR, use the default render output color space");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_bool(ot->sapi,
                         "copy",
                         false,
                         "Copy",
                         "Create a new image file without modifying the current image in Blender");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  img_op_prop_allow_tokens(ot);
  win_operator_prop_filesel(ot,
                            FILE_TYPE_FOLDER | FILE_TYPE_IMG | FILE_TYPE_MOVIE,
                            FILE_SPECIAL,
                            FILE_SAVE,
                            WIN_FILESEL_FILEPATH | WIN_FILESEL_RELPATH | WIN_FILESEL_SHOW_PROPS,
                            FILE_DEFAULTDISPLAY,
                            FILE_SORT_DEFAULT);
}

/* Save ImG Op */

/* param iuser: Img user or nullptr when called outside the image space. */
static bool img_file_format_writable(Img *img, ImgUser *iuser)
{
  void *lock;
  ImBuf *ibuf = dune_img_acquire_ibuf(img, iuser, &lock);
  bool ret = false;

  if (ibuf && dune_img_buf_format_writable(ibuf)) {
    ret = true;
  }

  dune_img_release_ibuf(img, ibuf, lock);
  return ret;
}

static bool img_save_poll(Cxt *C)
{
  /* Can't save if there are no pixels. */
  if (img_from_cxt_has_data_poll(C) == false) {
    return false;
  }

  /* Check if there is a valid file path and image format we can write
   * outside of the 'poll' so we can show a report with a pop-up. */

  /* Can always repack imgs.
   * Images without a filepath will go to "Save As". */
  return true;
}

static int img_save_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Img *image = img_from_cxt(C);
  ImgUser *iuser = img_user_from_cxt(C);
  Scene *scene = cxt_data_scene(C);
  ImgSaveOptions opts;
  bool ok = false;

  if (dune_img_has_packedfile(img)) {
    /* Save packed files to memory. */
    dune_img_memorypack(img);
    /* Report since this can be called from key shortcuts. */
    dune_reportf(op->reports, RPT_INFO, "Packed to mem img \"%s\"", img->filepath);
    return OP_FINISHED;
  }

  if (!dune_img_save_options_init(&opts, main, scene, img, iuser, false, false)) {
    dune_img_save_options_free(&opts);
    return OP_CANCELLED;
  }
  img_save_options_from_op(main, &opts, op);

  /* Check if file write permission is ok. */
  if (lib_exists(opts.filepath) && !lib_file_is_writable(opts.filepath)) {
    dune_reportf(
        op->reports, RPT_ERROR, "Cannot save img, path \"%s\" is not writable", opts.filepath);
  }
  else if (save_img_op(main, img, iuser, op, &opts)) {
    /* Report since this can be called from key shortcuts. */
    dune_reportf(op->reports, RPT_INFO, "Saved img \"%s\"", opts.filepath);
    ok = true;
  }

  dune_img_save_options_free(&opts);

  if (ok) {
    return OP_FINISHED;
  }

  return OP_CANCELLED;
}

static int img_save_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  Img *img = img_from_cxt(C);
  ImgUser *iuser = img_user_from_cxt(C);

  /* Not writable formats or imgs without a file-path will go to "Save As". */
  if (!dune_img_has_packedfile(img) &&
      (!dune_img_has_filepath(img) || !img_file_format_writable(img, iuser)))
  {
    win_op_name_call(C, "IMG_OT_save_as", WIN_OP_INVOKE_DEFAULT, nullptr, ev);
    return OP_CANCELLED;
  }
  return img_save_ex(C, op);
}

void IMG_OT_save(WinOpType *ot)
{
  /* ids */
  ot->name = "Save Img";
  ot->idname = "IMG_OT_save";
  ot->description = "Save the image with current name and settings";

  /* api cbs */
  ot->ex = img_save_ex;
  ot->invoke = img_save_invoke;
  ot->poll = img_save_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Save Seq Op */
static int img_save_seq_ex(Cxt *C, WinOp *op)
{
  Img *img = img_from_cxt(C);
  ImBuf *ibuf, *first_ibuf = nullptr;
  int tot = 0;
  char di[FILE_MAX];
  MovieCacheIter *iter;

  if (image == nullptr) {
    return OP_CANCELLED;
  }

  if (img->src != IMG_SRC_SEQ) {
    dune_report(op->reports, RPT_ERROR, "Can only save seq on img seqs");
    return OP_CANCELLED;
  }

  if (img->type == IMG_TYPE_MULTILAYER) {
    dune_report(op->reports, RPT_ERROR, "Cannot save multilayer seqs");
    return OP_CANCELLED;
  }

  /* get total dirty bufs and first dirty buf which is used for menu */
  ibuf = nullptr;
  if (img->cache != nullptr) {
    iter = imbuf_moviecacheIter_new(img->cache);
    while (!imbuf_moviecacheIter_done(iter)) {
      ibuf = imbuf_moviecacheIter_getImBuf(iter);
      if (ibuf != nullptr && ibuf->userflags & IB_BITMAPDIRTY) {
        if (first_ibuf == nullptr) {
          first_ibuf = ibuf;
        }
        tot++;
      }
      imbuf_moviecacheIter_step(iter);
    }
    imbuf_moviecacheIter_free(iter);
  }

  if (tot == 0) {
    dune_report(op->reports, RPT_WARNING, "No images have been changed");
    return OP_CANCELLED;
  }

  /* get a filename for menu */
  lib_path_split_dir_part(first_ibuf->filepath, di, sizeof(di));
  dune_reportf(op->reports, RPT_INFO, "%d img(s) will be saved in %s", tot, di);

  iter = imbuf_moviecacheIter_new(img->cache);
  while (!imbuf_moviecacheIter_done(iter)) {
    ibuf = imbuf_moviecacheIter_getImBuf(iter);

    if (ibuf != nullptr && ibuf->userflags & IB_BITMAPDIRTY) {
      if (0 == imbuf_saveiff(ibuf, ibuf->filepath, IB_rect)) {
        imbuf_reportf(op->reports, RPT_ERROR, "Could not write img: %s", strerror(errno));
        break;
      }

      dune_reportf(op->reports, RPT_INFO, "Saved %s", ibuf->filepath);
      ibuf->userflags &= ~IB_BITMAPDIRTY;
    }

    imbuf_moviecacheIter_step(iter);
  }
  imbuf_moviecacheIter_free(iter);

  return OP_FINISHED;
}

void IMG_OT_save_seq(WinOpType *ot)
{
  /* ids */
  ot->name = "Save Seq";
  ot->idname = "IMG_OT_save_seq";
  ot->description = "Save a seq of imgs";

  /* api cbs */
  ot->ex = img_save_seq_ex;
  ot->poll = img_from_cxt_has_data_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Save All Op */
static bool img_should_be_saved_when_modified(Img *img)
{
  return !ELEM(img->type, IMG_TYPE_R_RESULT, IMG_TYPE_COMPOSITE);
}

static bool img_should_be_saved(Img *img, bool *is_format_writable)
{
  if (dune_img_is_dirty_writable(img, is_format_writable) &&
      ELEM(img->src, IMG_SRC_FILE, IMG_SRC_GENERATED, IMG_SRC_TILED))
  {
    return img_should_be_saved_when_modified(img);
  }
  return false;
}

static bool img_has_valid_path(Img *img)
{
  return strchr(img->filepath, '\\') || strchr(img->filepath, '/');
}

static bool img_should_pack_during_save_all(const Img *img)
{
  /* Imgs without a filepath (implied with IMG_SRC_GENERATED) should
   * be packed during a save_all op. */
  return (img->src == IMG_SRC_GENERATED) ||
         (img->src == IMA_SRC_TILED && !dune_img_has_filepath(img));
}

bool ed_img_should_save_modified(const Main *main)
{
  ReportList reports;
  dune_reports_init(&reports, RPT_STORE);

  uint modified_imgs_count = ed_img_save_all_modified_info(main, &reports);
  bool should_save = modified_imgs_count || !lib_list_is_empty(&reports.list);

  dune_reports_free(&reports);

  return should_save;
}

int ed_img_save_all_modified_info(const Main *main, ReportList *reports)
{
  GSet *unique_paths = lib_gset_str_new(__func__);

  int num_saveable_imgs = 0;

  for (Img *img = static_cast<Img *>(main->imgs.first); img;
       img = static_cast<Img *>(img->id.next))
  {
    bool is_format_writable;

    if (img_should_be_saved(img, &is_format_writable)) {
      if (dune_img_has_packedfile(img) || img_should_pack_during_save_all(img)) {
        if (!ID_IS_LINKED(img)) {
          num_saveable_imgs++;
        }
        else {
          dune_reportf(reports,
                      RPT_WARNING,
                      "Packed lib img can't be saved: \"%s\" from \"%s\"",
                      img->id.name + 2,
                      img->id.lib->filepath);
        }
      }
      else if (!is_format_writable) {
        dune_reportf(reports,
                    RPT_WARNING,
                    "Img can't be saved, use a different file format: \"%s\"",
                    img->id.name + 2);
      }
      else {
        if (img_has_valid_path(img)) {
          num_saveable_imgs++;
          if (lib_gset_haskey(unique_paths, img->filepath)) {
            dune_reportf(reports,
                        RPT_WARNING,
                        "Multiple imgs can't be saved to an identical path: \"%s\"",
                        img->filepath);
          }
          else {
            lib_gset_insert(unique_paths, lib_strdup(img->filepath));
          }
        }
        else {
          dune_reportf(reports,
                      RPT_WARNING,
                      "Img can't be saved, no valid file path: \"%s\"",
                      img->filepath);
        }
      }
    }
  }

  lib_gset_free(unique_paths, mem_free);
  return num_saveable_imgs;
}

bool ed_img_save_all_modified(const Cxt *C, ReportList *reports)
{
  Main *main = cxt_data_main(C);

  ed_img_save_all_modified_info(main, reports);

  bool ok = true;

  for (Img *img = static_cast<Img *>(main->imgs.first); imgs;
       im = static_cast<Img *>(img->id.next))
  {
    bool is_format_writable;

    if (img_should_be_saved(img, &is_format_writable)) {
      if (dune_img_has_packedfile(img) || img_should_pack_during_save_all(img)) {
        dune_img_memorypack(img);
      }
      else if (is_format_writable) {
        if (img_has_valid_path(img)) {
          ImgSaveOptions opts;
          Scene *scene = cxt_data_scene(C);
          if (dune_img_save_options_init(&opts, main, scene, img, nullptr, false, false)) {
            bool saved_successfully = dune_img_save(reports, main, img, nullptr, &opts);
            ok = ok && saved_successfully;
          }
          dune_img_save_options_free(&opts);
        }
      }
    }
  }
  return ok;
}

static bool img_save_all_modified_poll(Cxt *C)
{
  int num_files = ed_img_save_all_modified_info(cxt_data_main(C), nullptr);
  return num_files > 0;
}

static int img_save_all_modified_ex(Cxt *C, WinOp *op)
{
  ed_img_save_all_modified(C, op->reports);
  return OP_FINISHED;
}

void IMG_OT_save_all_modified(WinOpType *ot)
{
  /* ids */
  ot->name = "Save All Modified";
  ot->idname = "IMG_OT_save_all_modified";
  ot->description = "Save all modified imgs";

  /* api cbs */
  ot->ex = img_save_all_modified_ex;
  ot->poll = img_save_all_modified_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Reload Img Op */
static int img_reload_ex(Cxt *C, WinOp * /*op*/)
{
  Main *main = cxt_data_main(C);
  Img *img = img_from_cxt(C);
  ImgUser *iuser = img_user_from_cxt(C);

  if (!img) {
    return OP_CANCELLED;
  }

  /* dune_packedfile_unpack_img frees img bufs */
  ed_preview_kill_jobs(cxt_wm(C), cxt_data_main(C));

  dune_img_signal(main, img, iuser, IMG_SIGNAL_RELOAD);
  graph_id_tag_update(&img->id, 0);

  win_ev_add_notifier(C, NC_IMG | NA_EDITED, img);

  return OP_FINISHED;
}

void IMG_OT_reload(WinOpType *ot)
{
  /* identifiers */
  ot->name = "Reload Img";
  ot->idname = "IMG_OT_reload";
  ot->description = "Reload current img from disk";

  /* api cbs */
  ot->ex = img_reload_ex;

  /* flags */
  ot->flag = OPTYPE_REGISTER; /* no undo, img buf is not handled by undo */
}

/* New Img Op */
#define IMG_DEF_NAME N_("Untitled")

enum {
  GEN_CXT_NONE = 0,
  GEN_CXT_PAINT_CANVAS = 1,
  GEN_CXT_PAINT_STENCIL = 2,
};

struct ImgNewData {
  ApiPropPtr pprop;
};

static ImgNewData *img_new_init(Cxt *C, WinOp *op)
{
  if (op->customdata) {
    return static_cast<ImgNewData *>(op->customdata);
  }

  ImgNewData *data = static_cast<ImgNewData *>(mem_calloc(sizeof(ImgNewData), __func__));
  ui_cxt_active_btn_prop_get_templateId(C, &data->pprop.ptr, &data->pprop.prop);
  op->customdata = data;
  return data;
}

static void img_new_free(WinOp *op)
{
  MEM_SAFE_FREE(op->customdata);
}

static int img_new_ex(Cxt *C, WinOp *op)
{
  SpaceImg *simg;
  Img *img;
  Main *main;
  ApiProp *prop;
  char name_buffer[MAX_ID_NAME - 2];
  const char *name;
  float color[4];
  int width, height, floatbuf, gen_type, alpha;
  int stereo3d;

  /* retrieve state */
  simg = cxt_win_space_img(C);
  main = cxt_data_main(C);

  prop = api_struct_find_prop(op->ptr, "name");
  api_prop_string_get(op->ptr, prop, name_buf);
  if (!api_prop_is_set(op->ptr, prop)) {
    /* Default value, we can translate! */
    name = DATA_(name_buf);
  }
  else {
    name = name_buf;
  }
  width = api_int_get(op->ptr, "width");
  height = api_int_get(op->ptr, "height");
  floatbuf = api_bool_get(op->ptr, "float");
  gen_type = api_enum_get(op->ptr, "generated_type");
  api_float_get_array(op->ptr, "color", color);
  alpha = api_bool_get(op->ptr, "alpha");
  stereo3d = api_bool_get(op->ptr, "use_stereo_3d");
  bool tiled = api_bool_get(op->ptr, "tiled");

  if (!alpha) {
    color[3] = 1.0f;
  }

  img = dune_img_add_generated(main,
                                width,
                                height,
                                name,
                                alpha ? 32 : 24,
                                floatbuf,
                                gen_type,
                                color,
                                stereo3d,
                                false,
                                tiled);

  if (!img) {
    img_new_free(op);
    return OP_CANCELLED;
  }

  /* hook into UI */
  ImgNewData *data = img_new_init(C, op);

  if (data->pprop.prop) {
    /* when creating new ID blocks, use is already 1, but RNA
     * pointer use also increases user, so this compensates it */
    id_us_min(&img->id);

    ApiPtr imgptr = api_id_ptr_create(&img->id);
    api_prop_ptr_set(&data->pprop.ptr, data->pprop.prop, imaptr, nullptr);
    api_prop_update(C, &data->pprop.ptr, data->pprop.prop);
  }
  else if (simg) {
    ed_space_img_set(main, simg, img, false);
  }
  else {
    /* dune_img_add_generated creates one user by default, remove it if image is not linked to
     * anything. ref. #94599. */
    id_us_min(&img->id);
  }

  dune_img_signal(main, img, (simg) ? &simg->iuser : nullptr, IMG_SIGNAL_USER_NEW_IMG);

  win_ev_add_notifier(C, NC_IMG | NA_ADDED, img);

  img_new_free(op);

  return OP_FINISHED;
}

static int img_new_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  /* Get prop in advance, it doesn't work after win_op_props_dialog_popup. */
  ImgNewData *data;
  op->customdata = data = static_cast<ImgNewData *>(mem_calloc(sizeof(ImgNewData), __func__));
  ui_cxt_active_btn_prop_get_templateId(C, &data->pprop.ptr, &data->pprop.prop);

  /* Better for user feedback. */
  api_string_set(op->ptr, "name", DATA_(IMG_DEF_NAME));
  return win_op_props_dialog_popup(C, op, 300);
}

static void img_new_drw(Cxt * /*C*/, WinOp *op)
{
  uiLayout *col;
  uiLayout *layout = op->layout;
#if 0
  Scene *scene = cxt_data_scene(C);
  const bool is_multiview = (scene->r.scemode & R_MULTIVIEW) != 0;
#endif

  /* copy of win_op_props_dialog_popup() layout */
  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  col = uiLayoutColumn(layout, false);
  uiItemR(col, op->ptr, "name", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "width", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "height", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "color", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "alpha", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "generated_type", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "float", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "tiled", UI_ITEM_NONE, nullptr, ICON_NONE);

#if 0
  if (is_multiview) {
    uiItemL(col[0], "", ICON_NONE);
    uiItemR(col[1], op->ptr, "use_stereo_3d", 0, nullptr, ICON_NONE);
  }
#endif
}

static void img_new_cancel(Cxt * /*C*/, WinOp *op)
{
  img_new_free(op);
}

void IMG_OT_new(WinOpType *ot)
{
  ApiProp *prop;
  static float default_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};

  /* ids */
  ot->name = "New Img";
  ot->description = "Create a new img";
  ot->idname = "IMG_OT_new";

  /* api cbs */
  ot->ex = img_new_ex;
  ot->invoke = img_new_invoke;
  ot->ui = img_new_drw;
  ot->cancel = img_new_cancel;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* props */
  api_def_string(ot->sapi, "name", IMG_DEF_NAME, MAX_ID_NAME - 2, "Name", "Img data-block name");
  prop = api_def_int(ot->sapi, "width", 1024, 1, INT_MAX, "Width", "Img width", 1, 16384);
  api_def_prop_subtype(prop, PROP_PIXEL);
  prop = api_def_int(ot->sapi, "height", 1024, 1, INT_MAX, "Height", "Img height", 1, 16384);
  api_def_prop_subtype(prop, PROP_PIXEL);
  prop = api_def_float_color(
      ot->sapi, "color", 4, nullptr, 0.0f, FLT_MAX, "Color", "Default fill color", 0.0f, 1.0f);
  api_def_prop_subtype(prop, PROP_COLOR_GAMMA);
  api_def_prop_float_array_default(prop, default_color);
  api_def_bool(ot->sapi, "alpha", true, "Alpha", "Create an img with an alpha channel");
  api_def_enum(ot->sapi,
               "generated_type",
               api_enum_image_generated_type_items,
               IMG_GENTYPE_BLANK,
               "Generated Type",
               "Fill the img with a grid for UV map testing");
  api_def_bool(ot->sapi,
                  "float",
                  false,
                  "32-bit Float",
                  "Create img with 32-bit floating-point bit depth");
  api_def_prop_flag(prop, PROP_HIDDEN);
  prop = api_def_bool(
      ot->sapi, "use_stereo_3d", false, "Stereo 3D", "Create an img with left and right views");
  api_def_prop_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
  prop = api_def_bool(ot->sapi, "tiled", false, "Tiled", "Create a tiled img");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

#undef IMG_DEF_NAME

/* Flip Op */
static int img_flip_ex(Cxt *C, WinOp *op)
{
  Img *img = img_from_cxt(C);
  ImgUser iuser = img_user_from_cxt_and_active_tile(C, img);
  ImBuf *ibuf = dune_image_acquire_ibuf(img, &iuser, nullptr);
  SpaceImg *sim = cxt_win_space_img(C);
  const bool is_paint = ((simg != nullptr) && (simg->mode == SI_MODE_PAINT));

  if (ibuf == nullptr) {
    /* TODO: this should actually never happen, but does for render-results -> cleanup. */
    return OP_CANCELLED;
  }

  const bool use_flip_x = api_bool_get(op->ptr, "use_flip_x");
  const bool use_flip_y = api_bool_get(op->ptr, "use_flip_y");

  if (!use_flip_x && !use_flip_y) {
    dune_img_release_ibuf(img, ibuf, nullptr);
    return OP_FINISHED;
  }

  ed_img_undo_push_begin_with_img(op->type->name, img, ibuf, &iuser);

  if (is_paint) {
    ed_imgpaint_clear_partial_redrw();
  }

  const int size_x = ibuf->x;
  const int size_y = ibuf->y;

  if (ibuf->float_buffer.data) {
    float *float_pixels = ibuf->float_buffer.data;

    float *orig_float_pixels = static_cast<float *>(MEM_dupallocN(float_pixels));
    for (int x = 0; x < size_x; x++) {
      const int src_pixel_x = use_flip_x ? size_x - x - 1 : x;
      for (int y = 0; y < size_y; y++) {
        const int src_pixel_y = use_flip_y ? size_y - y - 1 : y;

        const float *src_pixel =
            &orig_float_pixels[4 * (src_pixel_x + src_pixel_y * size_x)];
        float *target_pixel = &float_pixels[4 * (x + y * size_x)];

        copy_v4_v4(target_pixel, src_pixel);
      }
    }
    mem_free(orig_float_pixels);

    if (ibuf->byte_buffer.data) {
      imbuf_rect_from_float(ibuf);
    }
  }
  else if (ibuf->byte_buffer.data) {
    uchar *char_pixels = ibuf->byte_buffer.data;
    uchar *orig_char_pixels = static_cast<uchar *>(mem_dupalloc(char_pixels));
    for (int x = 0; x < size_x; x++) {
      const int src_pixel_x = use_flip_x ? size_x - x - 1 : x;
      for (int y = 0; y < size_y; y++) {
        const int src_pixel_y = use_flip_y ? size_y - y - 1 : y;

        const uchar *src_pixel =
            &orig_char_pixels[4 * (src_pixel_x + src_pixel_y * size_x)];
        uchar *target_pixel = &char_pixels[4 * (x + y * size_x)];

        copy_v4_v4_uchar(target_pixel, src_pixel);
      }
    }
    mem_free(orig_char_pixels);
  }
  else {
    dune_img_release_ibuf(img, ibuf, nullptr);
    return OP_CANCELLED;
  }

  ibuf->userflags |= IB_DISPLAY_BUF_INVALID;
  dune_img_mark_dirty(img, ibuf);

  if (ibuf->mipmap[0]) {
    ibuf->userflags |= IB_MIPMAP_INVALID;
  }

  ed_img_undo_push_end();

  dune_img_partial_update_mark_full_update(img);

  win_ev_add_notifier(C, NC_IMG | NA_EDITED, img);

  dune_img_release_ibuf(img, ibuf, nullptr);

  return OP_FINISHED;
}

void IMG_OT_flip(WinOpType *ot)
{
  /* ids */
  ot->name = "Flip Img";
  ot->idname = "IMG_OT_flip";
  ot->description = "Flip the img";

  /* api cbs */
  ot->ex = img_flip_ex;
  ot->poll = img_from_cxt_has_data_poll_active_tile;

  /* props */
  ApiProp *prop;
  prop = api_def_bool(
      ot->sapi, "use_flip_x", false, "Horizontal", "Flip the img horizontally");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_bool(ot->sapi, "use_flip_y", false, "Vertical", "Flip the img vertically");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/* Clipboard Copy Op */
static int img_clipboard_copy_ex(Cxt *C, WinOp *op)
{
  Img *img = img_from_cxt(C);
  if (img == nullptr) {
    return false;
  }

  if (G.is_rendering && ima->source == IMG_SRC_VIEWER) {
    dune_report(op->reports, RPT_ERROR, "Imgs cannot be copied while rendering");
    return false;
  }

  ImgUser *iuser = img_user_from_cxt(C);
  win_cursor_set(cxt_win(C), WIN_CURSOR_WAIT);

  void *lock;
  ImBuf *ibuf = dune_img_acquire_ibuf(img, iuser, &lock);
  if (ibuf == nullptr) {
    dune_img_release_ibuf(img, ibuf, lock);
    win_cursor_set(cxt_win(C), WIN_CURSOR_DEFAULT);
    return OP_CANCELLED;
  }

  win_clipboard_img_set(ibuf);
  dune_img_release_ibuf(img, ibuf, lock);
  win_cursor_set(cxt_win(C), WIN_CURSOR_DEFAULT);

  return OP_FINISHED;
}

static bool img_clipboard_copy_poll(Cxt *C)
{
  if (!img_from_cxt_has_data_poll(C)) {
    cxt_win_op_poll_msg_set(C, "No imgs available");
    return false;
  }

  return true;
}

void IMG_OT_clipboard_copy(WinOpType *ot)
{
  /* ids */
  ot->name = "Copy Img";
  ot->idname = "IMG_OT_clipboard_copy";
  ot->description = "Copy the img to the clipboard";

  /* api cbs */
  ot->ex = img_clipboard_copy_ex;
  ot->poll = img_clipboard_copy_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/* Clipboard Paste Op */
static int img_clipboard_paste_ex(Cxt *C, WinOp *op)
{
  win_cursor_set(cxt_win(C), WIN_CURSOR_WAIT);

  ImBuf *ibuf = win_clipboard_img_get();
  if (!ibuf) {
    win_cursor_set(cxt_win(C), WIN_CURSOR_DEFAULT);
    return OP_CANCELLED;
  }

  ed_undo_push_op(C, op);

  Main *main = cxt_data_main(C);
  SpaceImg *simg = cxt_win_space_img(C);
  Img *img = dune_img_add_from_imbuf(main, ibuf, "Clipboard");
  imbuf_free(ibuf);

  ed_space_img_set(main, simg, img, false);
  dune_img_signal(main, img, (simg) ? &simg->iuser : nullptr, IMG_SIGNAL_USER_NEW_IMG);
  win_ev_add_notifier(C, NC_IMG | NA_ADDED, img);

  win_cursor_set(cxt_win(C), WIN_CURSOR_DEFAULT);

  return OP_FINISHED;
}

static bool img_clipboard_paste_poll(Cxt *C)
{
  if (!win_clipboard_img_available()) {
    cxt_win_op_poll_msg_set(C, "No compatible imgs are on the clipboard");
    return false;
  }

  return true;
}

void IMG_OT_clipboard_paste(WinOpType *ot)
{
  /* ids */
  ot->name = "Paste Img";
  ot->idname = "IMG_OT_clipboard_paste";
  ot->description = "Paste new img from the clipboard";

  /* api cbs */
  ot->ex = img_clipboard_paste_ex;
  ot->poll = img_clipboard_paste_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/* Invert Ops */
static int img_invert_ex(Cxt *C, WinOp *op)
{
  Img *img = img_from_cxt(C);
  ImgUser iuser = img_user_from_cxt_and_active_tile(C, img);
  ImBuf *ibuf = dune_img_acquire_ibuf(img, &iuser, nullptr);
  SpaceImg *simg = cxt_win_space_img(C);
  const bool is_paint = ((simg != nullptr) && (simg->mode == SI_MODE_PAINT));

  /* flags indicate if this channel should be inverted */
  const bool r = api_bool_get(op->ptr, "invert_r");
  const bool g = api_bool_get(op->ptr, "invert_g");
  const bool b = api_bool_get(op->ptr, "invert_b");
  const bool a = api_bool_get(op->ptr, "invert_a");

  size_t i;

  if (ibuf == nullptr) {
    /* TODO: this should actually never happen, but does for render-results -> cleanup */
    return OP_CANCELLED;
  }

  ed_img_undo_push_begin_with_img(op->type->name, img, ibuf, &iuser);

  if (is_paint) {
    ed_imgpaint_clear_partial_redrw();
  }

  /* TODO: make this into an imbuf_invert_channels(ibuf,r,g,b,a) method!? */
  if (ibuf->float_buf.data) {

    float *fp = ibuf->float_buf.data;
    for (i = size_t(ibuf->x) * ibuf->y; i > 0; i--, fp += 4) {
      if (r) {
        fp[0] = 1.0f - fp[0];
      }
      if (g) {
        fp[1] = 1.0f - fp[1];
      }
      if (b) {
        fp[2] = 1.0f - fp[2];
      }
      if (a) {
        fp[3] = 1.0f - fp[3];
      }
    }

    if (ibuf->byte_buf.data) {
      imbuf_rect_from_float(ibuf);
    }
  }
  else if (ibuf->byte_buf.data) {

    uchar *cp = ibuf->byte_buf.data;
    for (i = size_t(ibuf->x) * ibuf->y; i > 0; i--, cp += 4) {
      if (r) {
        cp[0] = 255 - cp[0];
      }
      if (g) {
        cp[1] = 255 - cp[1];
      }
      if (b) {
        cp[2] = 255 - cp[2];
      }
      if (a) {
        cp[3] = 255 - cp[3];
      }
    }
  }
  else {
    dune_img_release_ibuf(img, ibuf, nullptr);
    return OP_CANCELLED;
  }

  ibuf->userflags |= IB_DISPLAY_BUF_INVALID;
  dune_img_mark_dirty(ima, ibuf);

  if (ibuf->mipmap[0]) {
    ibuf->userflags |= IB_MIPMAP_INVALID;
  }

  ed_img_undo_push_end();

  dune_img_partial_update_mark_full_update(img);

  ed_ev_add_notifier(C, NC_IMG | NA_EDITED, img);

  dune_img_release_ibuf(img, ibuf, nullptr);

  return OP_FINISHED;
}

void IMG_OT_invert(WinOpType *ot)
{
  ApiProp *prop;

  /* identifiers */
  ot->name = "Invert Channels";
  ot->idname = "IMAGE_OT_invert";
  ot->description = "Invert image's channels";

  /* api callbacks */
  ot->exec = image_invert_exec;
  ot->poll = image_from_context_has_data_poll_active_tile;

  /* properties */
  prop = RNA_def_boolean(ot->srna, "invert_r", false, "Red", "Invert red channel");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "invert_g", false, "Green", "Invert green channel");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "invert_b", false, "Blue", "Invert blue channel");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "invert_a", false, "Alpha", "Invert alpha channel");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Scale Operator
 * \{ */

static int image_scale_invoke(bContext *C, wmOperator *op, const wmEvent * /*event*/)
{
  Image *ima = image_from_context(C);
  ImageUser iuser = image_user_from_context_and_active_tile(C, ima);
  PropertyRNA *prop = RNA_struct_find_property(op->ptr, "size");
  if (!RNA_property_is_set(op->ptr, prop)) {
    ImBuf *ibuf = BKE_image_acquire_ibuf(ima, &iuser, nullptr);
    const int size[2] = {ibuf->x, ibuf->y};
    RNA_property_int_set_array(op->ptr, prop, size);
    BKE_image_release_ibuf(ima, ibuf, nullptr);
  }
  return WM_operator_props_dialog_popup(C, op, 200);
}

static int image_scale_exec(bContext *C, wmOperator *op)
{
  Image *ima = image_from_context(C);
  ImageUser iuser = image_user_from_context_and_active_tile(C, ima);
  ImBuf *ibuf = BKE_image_acquire_ibuf(ima, &iuser, nullptr);
  SpaceImage *sima = CTX_wm_space_image(C);
  const bool is_paint = ((sima != nullptr) && (sima->mode == SI_MODE_PAINT));

  if (ibuf == nullptr) {
    /* TODO: this should actually never happen, but does for render-results -> cleanup */
    return OPERATOR_CANCELLED;
  }

  if (is_paint) {
    ED_imapaint_clear_partial_redraw();
  }

  PropertyRNA *prop = RNA_struct_find_property(op->ptr, "size");
  int size[2];
  if (RNA_property_is_set(op->ptr, prop)) {
    RNA_property_int_get_array(op->ptr, prop, size);
  }
  else {
    size[0] = ibuf->x;
    size[1] = ibuf->y;
    RNA_property_int_set_array(op->ptr, prop, size);
  }

  ED_image_undo_push_begin_with_image(op->type->name, ima, ibuf, &iuser);

  ibuf->userflags |= IB_DISPLAY_BUFFER_INVALID;
  IMB_scaleImBuf(ibuf, size[0], size[1]);
  BKE_image_mark_dirty(ima, ibuf);
  BKE_image_release_ibuf(ima, ibuf, nullptr);

  ED_image_undo_push_end();

  BKE_image_partial_update_mark_full_update(ima);

  DEG_id_tag_update(&ima->id, 0);
  WM_event_add_notifier(C, NC_IMAGE | NA_EDITED, ima);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_resize(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Resize Image";
  ot->idname = "IMAGE_OT_resize";
  ot->description = "Resize the image";

  /* api callbacks */
  ot->invoke = image_scale_invoke;
  ot->exec = image_scale_exec;
  ot->poll = image_from_context_has_data_poll_active_tile;

  /* properties */
  RNA_def_int_vector(ot->srna, "size", 2, nullptr, 1, INT_MAX, "Size", "", 1, SHRT_MAX);

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Pack Operator
 * \{ */

static bool image_pack_test(bContext *C, wmOperator *op)
{
  Image *ima = image_from_context(C);

  if (!ima) {
    return false;
  }

  if (ELEM(ima->source, IMA_SRC_SEQUENCE, IMA_SRC_MOVIE)) {
    BKE_report(op->reports, RPT_ERROR, "Packing movies or image sequences not supported");
    return false;
  }

  return true;
}

static int image_pack_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Image *ima = image_from_context(C);

  if (!image_pack_test(C, op)) {
    return OPERATOR_CANCELLED;
  }

  if (BKE_image_is_dirty(ima)) {
    BKE_image_memorypack(ima);
  }
  else {
    BKE_image_packfiles(op->reports, ima, ID_BLEND_PATH(bmain, &ima->id));
  }

  WM_event_add_notifier(C, NC_IMAGE | NA_EDITED, ima);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_pack(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Pack Image";
  ot->description = "Pack an image as embedded data into the .blend file";
  ot->idname = "IMAGE_OT_pack";

  /* api callbacks */
  ot->exec = image_pack_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Unpack Operator
 * \{ */

static int image_unpack_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Image *ima = image_from_context(C);
  int method = RNA_enum_get(op->ptr, "method");

  /* find the supplied image by name */
  if (RNA_struct_property_is_set(op->ptr, "id")) {
    char imaname[MAX_ID_NAME - 2];
    RNA_string_get(op->ptr, "id", imaname);
    ima = static_cast<Image *>(BLI_findstring(&bmain->images, imaname, offsetof(ID, name) + 2));
    if (!ima) {
      ima = image_from_context(C);
    }
  }

  if (!ima || !BKE_image_has_packedfile(ima)) {
    return OPERATOR_CANCELLED;
  }

  if (ELEM(ima->source, IMA_SRC_SEQUENCE, IMA_SRC_MOVIE)) {
    BKE_report(op->reports, RPT_ERROR, "Unpacking movies or image sequences not supported");
    return OPERATOR_CANCELLED;
  }

  if (G.fileflags & G_FILE_AUTOPACK) {
    BKE_report(op->reports,
               RPT_WARNING,
               "AutoPack is enabled, so image will be packed again on file save");
  }

  /* XXX BKE_packedfile_unpack_image frees image buffers */
  ED_preview_kill_jobs(CTX_wm_manager(C), CTX_data_main(C));

  BKE_packedfile_unpack_image(CTX_data_main(C), op->reports, ima, ePF_FileStatus(method));

  WM_event_add_notifier(C, NC_IMAGE | NA_EDITED, ima);

  return OPERATOR_FINISHED;
}

static int image_unpack_invoke(bContext *C, wmOperator *op, const wmEvent * /*event*/)
{
  Image *ima = image_from_context(C);

  if (RNA_struct_property_is_set(op->ptr, "id")) {
    return image_unpack_exec(C, op);
  }

  if (!ima || !BKE_image_has_packedfile(ima)) {
    return OPERATOR_CANCELLED;
  }

  if (ELEM(ima->source, IMA_SRC_SEQUENCE, IMA_SRC_MOVIE)) {
    BKE_report(op->reports, RPT_ERROR, "Unpacking movies or image sequences not supported");
    return OPERATOR_CANCELLED;
  }

  if (G.fileflags & G_FILE_AUTOPACK) {
    BKE_report(op->reports,
               RPT_WARNING,
               "AutoPack is enabled, so image will be packed again on file save");
  }

  unpack_menu(C,
              "IMAGE_OT_unpack",
              ima->id.name + 2,
              ima->filepath,
              "textures",
              BKE_image_has_packedfile(ima) ?
                  ((ImagePackedFile *)ima->packedfiles.first)->packedfile :
                  nullptr);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_unpack(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Unpack Image";
  ot->description = "Save an image packed in the .blend file to disk";
  ot->idname = "IMAGE_OT_unpack";

  /* api callbacks */
  ot->exec = image_unpack_exec;
  ot->invoke = image_unpack_invoke;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_enum(
      ot->srna, "method", rna_enum_unpack_method_items, PF_USE_LOCAL, "Method", "How to unpack");
  /* XXX, weak!, will fail with library, name collisions */
  RNA_def_string(
      ot->srna, "id", nullptr, MAX_ID_NAME - 2, "Image Name", "Image data-block name to unpack");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Sample Image Operator
 * \{ */

bool ED_space_image_get_position(SpaceImage *sima,
                                 ARegion *region,
                                 const int mval[2],
                                 float r_fpos[2])
{
  void *lock;
  ImBuf *ibuf = ED_space_image_acquire_buffer(sima, &lock, 0);

  if (ibuf == nullptr) {
    ED_space_image_release_buffer(sima, ibuf, lock);
    return false;
  }

  UI_view2d_region_to_view(&region->v2d, mval[0], mval[1], &r_fpos[0], &r_fpos[1]);

  ED_space_image_release_buffer(sima, ibuf, lock);
  return true;
}

bool ED_space_image_color_sample(
    SpaceImage *sima, ARegion *region, const int mval[2], float r_col[3], bool *r_is_data)
{
  if (r_is_data) {
    *r_is_data = false;
  }
  if (sima->image == nullptr) {
    return false;
  }
  float uv[2];
  UI_view2d_region_to_view(&region->v2d, mval[0], mval[1], &uv[0], &uv[1]);
  int tile = BKE_image_get_tile_from_pos(sima->image, uv, uv, nullptr);

  void *lock;
  ImBuf *ibuf = ED_space_image_acquire_buffer(sima, &lock, tile);
  bool ret = false;

  if (ibuf == nullptr) {
    ED_space_image_release_buffer(sima, ibuf, lock);
    return false;
  }

  if (uv[0] >= 0.0f && uv[1] >= 0.0f && uv[0] < 1.0f && uv[1] < 1.0f) {
    const float *fp;
    uchar *cp;
    int x = int(uv[0] * ibuf->x), y = int(uv[1] * ibuf->y);

    CLAMP(x, 0, ibuf->x - 1);
    CLAMP(y, 0, ibuf->y - 1);

    if (ibuf->float_buffer.data) {
      fp = (ibuf->float_buffer.data + (ibuf->channels) * (y * ibuf->x + x));
      copy_v3_v3(r_col, fp);
      ret = true;
    }
    else if (ibuf->byte_buffer.data) {
      cp = ibuf->byte_buffer.data + 4 * (y * ibuf->x + x);
      rgb_uchar_to_float(r_col, cp);
      IMB_colormanagement_colorspace_to_scene_linear_v3(r_col, ibuf->byte_buffer.colorspace);
      ret = true;
    }
  }

  if (r_is_data) {
    *r_is_data = (ibuf->colormanage_flag & IMB_COLORMANAGE_IS_DATA) != 0;
  }

  ED_space_image_release_buffer(sima, ibuf, lock);
  return ret;
}

void IMAGE_OT_sample(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Sample Color";
  ot->idname = "IMAGE_OT_sample";
  ot->description = "Use mouse to sample a color in current image";

  /* api callbacks */
  ot->invoke = ED_imbuf_sample_invoke;
  ot->modal = ED_imbuf_sample_modal;
  ot->cancel = ED_imbuf_sample_cancel;
  ot->poll = ED_imbuf_sample_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING;

  PropertyRNA *prop;
  prop = RNA_def_int(ot->srna, "size", 1, 1, 128, "Sample Size", "", 1, 64);
  RNA_def_property_subtype(prop, PROP_PIXEL);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Sample Line Operator
 * \{ */

static int image_sample_line_exec(bContext *C, wmOperator *op)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ARegion *region = CTX_wm_region(C);
  Scene *scene = CTX_data_scene(C);
  Image *ima = ED_space_image(sima);

  int x_start = RNA_int_get(op->ptr, "xstart");
  int y_start = RNA_int_get(op->ptr, "ystart");
  int x_end = RNA_int_get(op->ptr, "xend");
  int y_end = RNA_int_get(op->ptr, "yend");

  float uv1[2], uv2[2], ofs[2];
  UI_view2d_region_to_view(&region->v2d, x_start, y_start, &uv1[0], &uv1[1]);
  UI_view2d_region_to_view(&region->v2d, x_end, y_end, &uv2[0], &uv2[1]);

  /* If the image has tiles, shift the positions accordingly. */
  int tile = BKE_image_get_tile_from_pos(ima, uv1, uv1, ofs);
  sub_v2_v2(uv2, ofs);

  void *lock;
  ImBuf *ibuf = ED_space_image_acquire_buffer(sima, &lock, tile);
  Histogram *hist = &sima->sample_line_hist;

  if (ibuf == nullptr) {
    ED_space_image_release_buffer(sima, ibuf, lock);
    return OPERATOR_CANCELLED;
  }
  /* hmmmm */
  if (ibuf->channels < 3) {
    ED_space_image_release_buffer(sima, ibuf, lock);
    return OPERATOR_CANCELLED;
  }

  copy_v2_v2(hist->co[0], uv1);
  copy_v2_v2(hist->co[1], uv2);

  /* enable line drawing */
  hist->flag |= HISTO_FLAG_SAMPLELINE;

  BKE_histogram_update_sample_line(hist, ibuf, &scene->view_settings, &scene->display_settings);

  /* reset y zoom */
  hist->ymax = 1.0f;

  ED_space_image_release_buffer(sima, ibuf, lock);

  ED_area_tag_redraw(CTX_wm_area(C));

  return OPERATOR_FINISHED;
}

static int image_sample_line_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  SpaceImage *sima = CTX_wm_space_image(C);

  Histogram *hist = &sima->sample_line_hist;
  hist->flag &= ~HISTO_FLAG_SAMPLELINE;

  if (!ED_space_image_has_buffer(sima)) {
    return OPERATOR_CANCELLED;
  }

  return WM_gesture_straightline_invoke(C, op, event);
}

void IMAGE_OT_sample_line(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Sample Line";
  ot->idname = "IMAGE_OT_sample_line";
  ot->description = "Sample a line and show it in Scope panels";

  /* api callbacks */
  ot->invoke = image_sample_line_invoke;
  ot->modal = WM_gesture_straightline_modal;
  ot->exec = image_sample_line_exec;
  ot->poll = space_image_main_region_poll;
  ot->cancel = WM_gesture_straightline_cancel;

  /* flags */
  ot->flag = 0; /* no undo/register since this operates on the space */

  WM_operator_properties_gesture_straightline(ot, WM_CURSOR_EDIT);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Set Curve Point Operator
 * \{ */

void IMAGE_OT_curves_point_set(wmOperatorType *ot)
{
  static const EnumPropertyItem point_items[] = {
      {0, "BLACK_POINT", 0, "Black Point", ""},
      {1, "WHITE_POINT", 0, "White Point", ""},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* identifiers */
  ot->name = "Set Curves Point";
  ot->idname = "IMAGE_OT_curves_point_set";
  ot->description = "Set black point or white point for curves";

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* api callbacks */
  ot->invoke = ED_imbuf_sample_invoke;
  ot->modal = ED_imbuf_sample_modal;
  ot->cancel = ED_imbuf_sample_cancel;
  ot->poll = space_image_main_area_not_uv_brush_poll;

  /* properties */
  RNA_def_enum(
      ot->srna, "point", point_items, 0, "Point", "Set black point or white point for curves");

  PropertyRNA *prop;
  prop = RNA_def_int(ot->srna, "size", 1, 1, 128, "Sample Size", "", 1, 64);
  RNA_def_property_subtype(prop, PROP_PIXEL);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Cycle Render Slot Operator
 * \{ */

static bool image_cycle_render_slot_poll(bContext *C)
{
  Image *ima = image_from_context(C);

  return (ima && ima->type == IMA_TYPE_R_RESULT);
}

static int image_cycle_render_slot_exec(bContext *C, wmOperator *op)
{
  Image *ima = image_from_context(C);
  const int direction = RNA_boolean_get(op->ptr, "reverse") ? -1 : 1;

  if (!ED_image_slot_cycle(ima, direction)) {
    return OPERATOR_CANCELLED;
  }

  WM_event_add_notifier(C, NC_IMAGE | ND_DRAW, nullptr);

  /* no undo push for browsing existing */
  RenderSlot *slot = BKE_image_get_renderslot(ima, ima->render_slot);
  if ((slot && slot->render) || ima->render_slot == ima->last_render_slot) {
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

void IMAGE_OT_cycle_render_slot(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Cycle Render Slot";
  ot->idname = "IMAGE_OT_cycle_render_slot";
  ot->description = "Cycle through all non-void render slots";

  /* api callbacks */
  ot->exec = image_cycle_render_slot_exec;
  ot->poll = image_cycle_render_slot_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;

  RNA_def_boolean(ot->srna, "reverse", false, "Cycle in Reverse", "");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Render Slot Operator
 * \{ */

static int image_clear_render_slot_exec(bContext *C, wmOperator * /*op*/)
{
  Image *ima = image_from_context(C);
  ImageUser *iuser = image_user_from_context(C);

  if (!BKE_image_clear_renderslot(ima, iuser, ima->render_slot)) {
    return OPERATOR_CANCELLED;
  }

  WM_event_add_notifier(C, NC_IMAGE | ND_DRAW, nullptr);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_clear_render_slot(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Render Slot";
  ot->idname = "IMAGE_OT_clear_render_slot";
  ot->description = "Clear the currently selected render slot";

  /* api callbacks */
  ot->exec = image_clear_render_slot_exec;
  ot->poll = image_cycle_render_slot_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Add Render Slot Operator */

static int image_add_render_slot_exec(bContext *C, wmOperator * /*op*/)
{
  Image *ima = image_from_context(C);

  RenderSlot *slot = BKE_image_add_renderslot(ima, nullptr);
  ima->render_slot = BLI_findindex(&ima->renderslots, slot);

  WM_event_add_notifier(C, NC_IMAGE | ND_DRAW, nullptr);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_add_render_slot(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add Render Slot";
  ot->idname = "IMAGE_OT_add_render_slot";
  ot->description = "Add a new render slot";

  /* api callbacks */
  ot->exec = image_add_render_slot_exec;
  ot->poll = image_cycle_render_slot_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Remove Render Slot Operator
 * \{ */

static int image_remove_render_slot_exec(bContext *C, wmOperator * /*op*/)
{
  Image *ima = image_from_context(C);
  ImageUser *iuser = image_user_from_context(C);

  if (!BKE_image_remove_renderslot(ima, iuser, ima->render_slot)) {
    return OPERATOR_CANCELLED;
  }

  WM_event_add_notifier(C, NC_IMAGE | ND_DRAW, nullptr);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_remove_render_slot(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Render Slot";
  ot->idname = "IMAGE_OT_remove_render_slot";
  ot->description = "Remove the current render slot";

  /* api callbacks */
  ot->exec = image_remove_render_slot_exec;
  ot->poll = image_cycle_render_slot_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/* Change Frame Op */
static bool change_frame_poll(bContext *C)
{
  /* prevent changes during render */
  if (G.is_rendering) {
    return false;
  }

  return space_image_main_region_poll(C);
}

static void change_frame_apply(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);

  /* set the new frame number */
  scene->r.cfra = RNA_int_get(op->ptr, "frame");
  FRAMENUMBER_MIN_CLAMP(scene->r.cfra);
  scene->r.subframe = 0.0f;

  /* do updates */
  DEG_id_tag_update(&scene->id, ID_RECALC_FRAME_CHANGE);
  WM_event_add_notifier(C, NC_SCENE | ND_FRAME, scene);
}

static int change_frame_exec(bContext *C, wmOperator *op)
{
  change_frame_apply(C, op);

  return OPERATOR_FINISHED;
}

static int frame_from_event(bContext *C, const wmEvent *event)
{
  ARegion *region = CTX_wm_region(C);
  Scene *scene = CTX_data_scene(C);
  int framenr = 0;

  if (region->regiontype == RGN_TYPE_WINDOW) {
    float sfra = scene->r.sfra, efra = scene->r.efra, framelen = region->winx / (efra - sfra + 1);

    framenr = sfra + event->mval[0] / framelen;
  }
  else {
    float viewx, viewy;

    UI_view2d_region_to_view(&region->v2d, event->mval[0], event->mval[1], &viewx, &viewy);

    framenr = round_fl_to_int(viewx);
  }

  return framenr;
}

static int change_frame_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  ARegion *region = CTX_wm_region(C);

  if (region->regiontype == RGN_TYPE_WINDOW) {
    const SpaceImage *sima = CTX_wm_space_image(C);
    if (!ED_space_image_show_cache_and_mval_over(sima, region, event->mval)) {
      return OPERATOR_PASS_THROUGH;
    }
  }

  RNA_int_set(op->ptr, "frame", frame_from_event(C, event));

  change_frame_apply(C, op);

  /* add temp handler */
  WM_event_add_modal_handler(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static int change_frame_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  switch (event->type) {
    case EVT_ESCKEY:
      return OPERATOR_FINISHED;

    case MOUSEMOVE:
      RNA_int_set(op->ptr, "frame", frame_from_event(C, event));
      change_frame_apply(C, op);
      break;

    case LEFTMOUSE:
    case RIGHTMOUSE:
      if (event->val == KM_RELEASE) {
        return OPERATOR_FINISHED;
      }
      break;
  }

  return OPERATOR_RUNNING_MODAL;
}

void IMAGE_OT_change_frame(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Change Frame";
  ot->idname = "IMAGE_OT_change_frame";
  ot->description = "Interactively change the current frame number";

  /* api callbacks */
  ot->exec = change_frame_exec;
  ot->invoke = change_frame_invoke;
  ot->modal = change_frame_modal;
  ot->poll = change_frame_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_UNDO;

  /* rna */
  RNA_def_int(ot->srna, "frame", 0, MINAFRAME, MAXFRAME, "Frame", "", MINAFRAME, MAXFRAME);
}

/* Reload cached render results... */
/* goes over all scenes, reads render layers */
static int image_read_viewlayers_exec(bContext *C, wmOperator * /*op*/)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  SpaceImage *sima = CTX_wm_space_image(C);
  Image *ima;

  ima = BKE_image_ensure_viewer(bmain, IMA_TYPE_R_RESULT, "Render Result");
  if (sima->image == nullptr) {
    ED_space_image_set(bmain, sima, ima, false);
  }

  RE_ReadRenderResult(scene, scene);

  WM_event_add_notifier(C, NC_IMAGE | NA_EDITED, ima);
  return OPERATOR_FINISHED;
}

void IMAGE_OT_read_viewlayers(wmOperatorType *ot)
{
  ot->name = "Open Cached Render";
  ot->idname = "IMAGE_OT_read_viewlayers";
  ot->description = "Read all the current scene's view layers from cache, as needed";

  ot->poll = space_image_main_region_poll;
  ot->exec = image_read_viewlayers_exec;

  /* flags */
  ot->flag = 0;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Render Border Operator
 * \{ */

static int render_border_exec(bContext *C, wmOperator *op)
{
  ARegion *region = CTX_wm_region(C);
  Scene *scene = CTX_data_scene(C);
  Render *re = RE_GetSceneRender(scene);
  SpaceImage *sima = CTX_wm_space_image(C);

  if (re == nullptr) {
    /* Shouldn't happen, but better be safe close to the release. */
    return OPERATOR_CANCELLED;
  }

  /* Get information about the previous render, or current scene if no render yet. */
  int width, height;
  BKE_render_resolution(&scene->r, false, &width, &height);
  const RenderData *rd = ED_space_image_has_buffer(sima) ? RE_engine_get_render_data(re) :
                                                           &scene->r;

  /* Get rectangle from the operator. */
  rctf border;
  WM_operator_properties_border_to_rctf(op, &border);
  UI_view2d_region_to_view_rctf(&region->v2d, &border, &border);

  /* Adjust for cropping. */
  if ((rd->mode & (R_BORDER | R_CROP)) == (R_BORDER | R_CROP)) {
    border.xmin = rd->border.xmin + border.xmin * (rd->border.xmax - rd->border.xmin);
    border.xmax = rd->border.xmin + border.xmax * (rd->border.xmax - rd->border.xmin);
    border.ymin = rd->border.ymin + border.ymin * (rd->border.ymax - rd->border.ymin);
    border.ymax = rd->border.ymin + border.ymax * (rd->border.ymax - rd->border.ymin);
  }

  CLAMP(border.xmin, 0.0f, 1.0f);
  CLAMP(border.ymin, 0.0f, 1.0f);
  CLAMP(border.xmax, 0.0f, 1.0f);
  CLAMP(border.ymax, 0.0f, 1.0f);

  /* Drawing a border surrounding the entire camera view switches off border rendering
   * or the border covers no pixels. */
  if ((border.xmin <= 0.0f && border.xmax >= 1.0f && border.ymin <= 0.0f && border.ymax >= 1.0f) ||
      (border.xmin == border.xmax || border.ymin == border.ymax))
  {
    scene->r.mode &= ~R_BORDER;
  }
  else {
    /* Snap border to pixel boundaries, so drawing a border within a pixel selects that pixel. */
    border.xmin = floorf(border.xmin * width) / width;
    border.xmax = ceilf(border.xmax * width) / width;
    border.ymin = floorf(border.ymin * height) / height;
    border.ymax = ceilf(border.ymax * height) / height;

    /* Set border. */
    scene->r.border = border;
    scene->r.mode |= R_BORDER;
  }

  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
  WM_event_add_notifier(C, NC_SCENE | ND_RENDER_OPTIONS, nullptr);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_render_border(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Render Region";
  ot->description = "Set the boundaries of the render region and enable render region";
  ot->idname = "IMAGE_OT_render_border";

  /* api callbacks */
  ot->invoke = WM_gesture_box_invoke;
  ot->exec = render_border_exec;
  ot->modal = WM_gesture_box_modal;
  ot->cancel = WM_gesture_box_cancel;
  ot->poll = image_cycle_render_slot_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* rna */
  WM_operator_properties_border(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Render Border Operator
 * \{ */

static int clear_render_border_exec(bContext *C, wmOperator * /*op*/)
{
  Scene *scene = CTX_data_scene(C);
  scene->r.mode &= ~R_BORDER;
  WM_event_add_notifier(C, NC_SCENE | ND_RENDER_OPTIONS, nullptr);
  BLI_rctf_init(&scene->r.border, 0.0f, 1.0f, 0.0f, 1.0f);
  return OPERATOR_FINISHED;
}

void IMAGE_OT_clear_render_border(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Render Region";
  ot->description = "Clear the boundaries of the render region and disable render region";
  ot->idname = "IMAGE_OT_clear_render_border";

  /* api callbacks */
  ot->exec = clear_render_border_exec;
  ot->poll = image_cycle_render_slot_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Add Tile Operator
 * \{ */

static bool do_fill_tile(PointerRNA *ptr, Image *ima, ImageTile *tile)
{
  RNA_float_get_array(ptr, "color", tile->gen_color);
  tile->gen_type = RNA_enum_get(ptr, "generated_type");
  tile->gen_x = RNA_int_get(ptr, "width");
  tile->gen_y = RNA_int_get(ptr, "height");
  bool is_float = RNA_boolean_get(ptr, "float");

  tile->gen_flag = is_float ? IMA_GEN_FLOAT : 0;
  tile->gen_depth = RNA_boolean_get(ptr, "alpha") ? 32 : 24;

  return BKE_image_fill_tile(ima, tile);
}

static void draw_fill_tile(PointerRNA *ptr, uiLayout *layout)
{
  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  uiLayout *col = uiLayoutColumn(layout, false);
  uiItemR(col, ptr, "color", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "width", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "height", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "alpha", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "generated_type", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "float", UI_ITEM_NONE, nullptr, ICON_NONE);
}

static void tile_fill_init(PointerRNA *ptr, Image *ima, ImageTile *tile)
{
  ImageUser iuser;
  BKE_imageuser_default(&iuser);
  if (tile != nullptr) {
    iuser.tile = tile->tile_number;
  }

  /* Acquire ibuf to get the default values.
   * If the specified tile has no ibuf, try acquiring the main tile instead
   * (unless the specified tile already was the first tile). */
  ImBuf *ibuf = BKE_image_acquire_ibuf(ima, &iuser, nullptr);
  if (ibuf == nullptr && (tile != nullptr) && (tile != ima->tiles.first)) {
    ibuf = BKE_image_acquire_ibuf(ima, nullptr, nullptr);
  }

  if (ibuf != nullptr) {
    /* Initialize properties from reference tile. */
    RNA_int_set(ptr, "width", ibuf->x);
    RNA_int_set(ptr, "height", ibuf->y);
    RNA_boolean_set(ptr, "float", ibuf->float_buffer.data != nullptr);
    RNA_boolean_set(ptr, "alpha", ibuf->planes > 24);

    BKE_image_release_ibuf(ima, ibuf, nullptr);
  }
}

static void def_fill_tile(StructOrFunctionRNA *srna)
{
  PropertyRNA *prop;
  static float default_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  prop = RNA_def_float_color(
      srna, "color", 4, nullptr, 0.0f, FLT_MAX, "Color", "Default fill color", 0.0f, 1.0f);
  RNA_def_property_subtype(prop, PROP_COLOR_GAMMA);
  RNA_def_property_float_array_default(prop, default_color);
  RNA_def_enum(srna,
               "generated_type",
               rna_enum_image_generated_type_items,
               IMA_GENTYPE_BLANK,
               "Generated Type",
               "Fill the image with a grid for UV map testing");
  prop = RNA_def_int(srna, "width", 1024, 1, INT_MAX, "Width", "Image width", 1, 16384);
  RNA_def_property_subtype(prop, PROP_PIXEL);
  prop = RNA_def_int(srna, "height", 1024, 1, INT_MAX, "Height", "Image height", 1, 16384);
  RNA_def_property_subtype(prop, PROP_PIXEL);

  /* Only needed when filling the first tile. */
  RNA_def_boolean(
      srna, "float", false, "32-bit Float", "Create image with 32-bit floating-point bit depth");
  RNA_def_boolean(srna, "alpha", true, "Alpha", "Create an image with an alpha channel");
}

static bool tile_add_poll(bContext *C)
{
  Image *ima = CTX_data_edit_image(C);

  return (ima != nullptr && ima->source == IMA_SRC_TILED && BKE_image_has_ibuf(ima, nullptr));
}

static int tile_add_exec(bContext *C, wmOperator *op)
{
  Image *ima = CTX_data_edit_image(C);

  int start_tile = RNA_int_get(op->ptr, "number");
  int end_tile = start_tile + RNA_int_get(op->ptr, "count") - 1;

  if (start_tile < 1001 || end_tile > IMA_UDIM_MAX) {
    BKE_report(op->reports, RPT_ERROR, "Invalid UDIM index range was specified");
    return OPERATOR_CANCELLED;
  }

  bool fill_tile = RNA_boolean_get(op->ptr, "fill");
  char *label = RNA_string_get_alloc(op->ptr, "label", nullptr, 0, nullptr);

  /* BKE_image_add_tile assumes a pre-sorted list of tiles. */
  BKE_image_sort_tiles(ima);

  ImageTile *last_tile_created = nullptr;
  for (int tile_number = start_tile; tile_number <= end_tile; tile_number++) {
    ImageTile *tile = BKE_image_add_tile(ima, tile_number, label);

    if (tile != nullptr) {
      if (fill_tile) {
        do_fill_tile(op->ptr, ima, tile);
      }

      last_tile_created = tile;
    }
  }
  MEM_freeN(label);

  if (!last_tile_created) {
    BKE_report(op->reports, RPT_WARNING, "No UDIM tiles were created");
    return OPERATOR_CANCELLED;
  }

  ima->active_tile_index = BLI_findindex(&ima->tiles, last_tile_created);

  WM_event_add_notifier(C, NC_IMAGE | ND_DRAW, nullptr);
  return OPERATOR_FINISHED;
}

static int tile_add_invoke(bContext *C, wmOperator *op, const wmEvent * /*event*/)
{
  Image *ima = CTX_data_edit_image(C);

  /* Find the first gap in tile numbers or the number after the last if
   * no gap exists. */
  int next_number = 0;
  LISTBASE_FOREACH (ImageTile *, tile, &ima->tiles) {
    next_number = tile->tile_number + 1;
    if (tile->next == nullptr || tile->next->tile_number > next_number) {
      break;
    }
  }

  ImageTile *tile = static_cast<ImageTile *>(BLI_findlink(&ima->tiles, ima->active_tile_index));
  tile_fill_init(op->ptr, ima, tile);

  RNA_int_set(op->ptr, "number", next_number);
  RNA_int_set(op->ptr, "count", 1);
  RNA_string_set(op->ptr, "label", "");

  return WM_operator_props_dialog_popup(C, op, 300);
}

static void tile_add_drw(bContext * /*C*/, wmOperator *op)
{
  uiLayout *static bool change_frame_poll(bContext *C)
{
  /* prevent changes during render */
  if (G.is_rendering) {
    return false;
  }

  return space_image_main_region_poll(C);
}

static void change_frame_apply(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);

  /* set the new frame number */
  scene->r.cfra = RNA_int_get(op->ptr, "frame");
  FRAMENUMBER_MIN_CLAMP(scene->r.cfra);
  scene->r.subframe = 0.0f;

  /* do updates */
  DEG_id_tag_update(&scene->id, ID_RECALC_FRAME_CHANGE);
  WM_event_add_notifier(C, NC_SCENE | ND_FRAME, scene);
}

static int change_frame_exec(bContext *C, wmOperator *op)
{
  change_frame_apply(C, op);

  return OPERATOR_FINISHED;
}

static int frame_from_event(bContext *C, const wmEvent *event)
{
  ARegion *region = CTX_wm_region(C);
  Scene *scene = CTX_data_scene(C);
  int framenr = 0;

  if (region->regiontype == RGN_TYPE_WINDOW) {
    float sfra = scene->r.sfra, efra = scene->r.efra, framelen = region->winx / (efra - sfra + 1);

    framenr = sfra + event->mval[0] / framelen;
  }
  else {
    float viewx, viewy;

    UI_view2d_region_to_view(&region->v2d, event->mval[0], event->mval[1], &viewx, &viewy);

    framenr = round_fl_to_int(viewx);
  }

  return framenr;
}

static int change_frame_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  ARegion *region = CTX_wm_region(C);

  if (region->regiontype == RGN_TYPE_WINDOW) {
    const SpaceImage *sima = CTX_wm_space_image(C);
    if (!ED_space_image_show_cache_and_mval_over(sima, region, event->mval)) {
      return OPERATOR_PASS_THROUGH;
    }
  }

  RNA_int_set(op->ptr, "frame", frame_from_event(C, event));

  change_frame_apply(C, op);

  /* add temp handler */
  WM_event_add_modal_handler(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static int change_frame_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  switch (event->type) {
    case EVT_ESCKEY:
      return OPERATOR_FINISHED;

    case MOUSEMOVE:
      RNA_int_set(op->ptr, "frame", frame_from_event(C, event));
      change_frame_apply(C, op);
      break;

    case LEFTMOUSE:
    case RIGHTMOUSE:
      if (event->val == KM_RELEASE) {
        return OPERATOR_FINISHED;
      }
      break;
  }

  return OPERATOR_RUNNING_MODAL;
}

void IMAGE_OT_change_frame(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Change Frame";
  ot->idname = "IMAGE_OT_change_frame";
  ot->description = "Interactively change the current frame number";

  /* api callbacks */
  ot->exec = change_frame_exec;
  ot->invoke = change_frame_invoke;
  ot->modal = change_frame_modal;
  ot->poll = change_frame_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_UNDO;

  /* rna */
  RNA_def_int(ot->srna, "frame", 0, MINAFRAME, MAXFRAME, "Frame", "", MINAFRAME, MAXFRAME);
}

/* Reload cached render results... */
/* goes over all scenes, reads render layers */
static int image_read_viewlayers_exec(bContext *C, wmOperator * /*op*/)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  SpaceImage *sima = CTX_wm_space_image(C);
  Image *ima;

  ima = BKE_image_ensure_viewer(bmain, IMA_TYPE_R_RESULT, "Render Result");
  if (sima->image == nullptr) {
    ED_space_image_set(bmain, sima, ima, false);
  }

  RE_ReadRenderResult(scene, scene);

  WM_event_add_notifier(C, NC_IMAGE | NA_EDITED, ima);
  return OPERATOR_FINISHED;
}

void IMAGE_OT_read_viewlayers(wmOperatorType *ot)
{
  ot->name = "Open Cached Render";
  ot->idname = "IMAGE_OT_read_viewlayers";
  ot->description = "Read all the current scene's view layers from cache, as needed";

  ot->poll = space_image_main_region_poll;
  ot->exec = image_read_viewlayers_exec;

  /* flags */
  ot->flag = 0;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Render Border Operator
 * \{ */

static int render_border_exec(bContext *C, wmOperator *op)
{
  ARegion *region = CTX_wm_region(C);
  Scene *scene = CTX_data_scene(C);
  Render *re = RE_GetSceneRender(scene);
  SpaceImage *sima = CTX_wm_space_image(C);

  if (re == nullptr) {
    /* Shouldn't happen, but better be safe close to the release. */
    return OPERATOR_CANCELLED;
  }

  /* Get information about the previous render, or current scene if no render yet. */
  int width, height;
  BKE_render_resolution(&scene->r, false, &width, &height);
  const RenderData *rd = ED_space_image_has_buffer(sima) ? RE_engine_get_render_data(re) :
                                                           &scene->r;

  /* Get rectangle from the operator. */
  rctf border;
  WM_operator_properties_border_to_rctf(op, &border);
  UI_view2d_region_to_view_rctf(&region->v2d, &border, &border);

  /* Adjust for cropping. */
  if ((rd->mode & (R_BORDER | R_CROP)) == (R_BORDER | R_CROP)) {
    border.xmin = rd->border.xmin + border.xmin * (rd->border.xmax - rd->border.xmin);
    border.xmax = rd->border.xmin + border.xmax * (rd->border.xmax - rd->border.xmin);
    border.ymin = rd->border.ymin + border.ymin * (rd->border.ymax - rd->border.ymin);
    border.ymax = rd->border.ymin + border.ymax * (rd->border.ymax - rd->border.ymin);
  }

  CLAMP(border.xmin, 0.0f, 1.0f);
  CLAMP(border.ymin, 0.0f, 1.0f);
  CLAMP(border.xmax, 0.0f, 1.0f);
  CLAMP(border.ymax, 0.0f, 1.0f);

  /* Drawing a border surrounding the entire camera view switches off border rendering
   * or the border covers no pixels. */
  if ((border.xmin <= 0.0f && border.xmax >= 1.0f && border.ymin <= 0.0f && border.ymax >= 1.0f) ||
      (border.xmin == border.xmax || border.ymin == border.ymax))
  {
    scene->r.mode &= ~R_BORDER;
  }
  else {
    /* Snap border to pixel boundaries, so drawing a border within a pixel selects that pixel. */
    border.xmin = floorf(border.xmin * width) / width;
    border.xmax = ceilf(border.xmax * width) / width;
    border.ymin = floorf(border.ymin * height) / height;
    border.ymax = ceilf(border.ymax * height) / height;

    /* Set border. */
    scene->r.border = border;
    scene->r.mode |= R_BORDER;
  }

  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
  WM_event_add_notifier(C, NC_SCENE | ND_RENDER_OPTIONS, nullptr);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_render_border(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Render Region";
  ot->description = "Set the boundaries of the render region and enable render region";
  ot->idname = "IMAGE_OT_render_border";

  /* api callbacks */
  ot->invoke = WM_gesture_box_invoke;
  ot->exec = render_border_exec;
  ot->modal = WM_gesture_box_modal;
  ot->cancel = WM_gesture_box_cancel;
  ot->poll = image_cycle_render_slot_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* rna */
  WM_operator_properties_border(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Render Border Operator
 * \{ */

static int clear_render_border_exec(bContext *C, wmOperator * /*op*/)
{
  Scene *scene = CTX_data_scene(C);
  scene->r.mode &= ~R_BORDER;
  WM_event_add_notifier(C, NC_SCENE | ND_RENDER_OPTIONS, nullptr);
  BLI_rctf_init(&scene->r.border, 0.0f, 1.0f, 0.0f, 1.0f);
  return OPERATOR_FINISHED;
}

void IMAGE_OT_clear_render_border(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Render Region";
  ot->description = "Clear the boundaries of the render region and disable render region";
  ot->idname = "IMAGE_OT_clear_render_border";

  /* api callbacks */
  ot->exec = clear_render_border_exec;
  ot->poll = image_cycle_render_slot_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Add Tile Operator
 * \{ */

static bool do_fill_tile(PointerRNA *ptr, Image *ima, ImageTile *tile)
{
  RNA_float_get_array(ptr, "color", tile->gen_color);
  tile->gen_type = RNA_enum_get(ptr, "generated_type");
  tile->gen_x = RNA_int_get(ptr, "width");
  tile->gen_y = RNA_int_get(ptr, "height");
  bool is_float = RNA_boolean_get(ptr, "float");

  tile->gen_flag = is_float ? IMA_GEN_FLOAT : 0;
  tile->gen_depth = RNA_boolean_get(ptr, "alpha") ? 32 : 24;

  return BKE_image_fill_tile(ima, tile);
}

static void draw_fill_tile(PointerRNA *ptr, uiLayout *layout)
{
  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  uiLayout *col = uiLayoutColumn(layout, false);
  uiItemR(col, ptr, "color", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "width", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "height", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "alpha", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "generated_type", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, ptr, "float", UI_ITEM_NONE, nullptr, ICON_NONE);
}

static void tile_fill_init(PointerRNA *ptr, Image *ima, ImageTile *tile)
{
  ImageUser iuser;
  BKE_imageuser_default(&iuser);
  if (tile != nullptr) {
    iuser.tile = tile->tile_number;
  }

  /* Acquire ibuf to get the default values.
   * If the specified tile has no ibuf, try acquiring the main tile instead
   * (unless the specified tile already was the first tile). */
  ImBuf *ibuf = BKE_image_acquire_ibuf(ima, &iuser, nullptr);
  if (ibuf == nullptr && (tile != nullptr) && (tile != ima->tiles.first)) {
    ibuf = BKE_image_acquire_ibuf(ima, nullptr, nullptr);
  }

  if (ibuf != nullptr) {
    /* Initialize properties from reference tile. */
    RNA_int_set(ptr, "width", ibuf->x);
    RNA_int_set(ptr, "height", ibuf->y);
    RNA_boolean_set(ptr, "float", ibuf->float_buffer.data != nullptr);
    RNA_boolean_set(ptr, "alpha", ibuf->planes > 24);

    BKE_image_release_ibuf(ima, ibuf, nullptr);
  }
}

static void def_fill_tile(StructOrFunctionRNA *srna)
{
  PropertyRNA *prop;
  static float default_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  prop = RNA_def_float_color(
      srna, "color", 4, nullptr, 0.0f, FLT_MAX, "Color", "Default fill color", 0.0f, 1.0f);
  RNA_def_property_subtype(prop, PROP_COLOR_GAMMA);
  RNA_def_property_float_array_default(prop, default_color);
  RNA_def_enum(srna,
               "generated_type",
               rna_enum_image_generated_type_items,
               IMA_GENTYPE_BLANK,
               "Generated Type",
               "Fill the image with a grid for UV map testing");
  prop = RNA_def_int(srna, "width", 1024, 1, INT_MAX, "Width", "Image width", 1, 16384);
  RNA_def_property_subtype(prop, PROP_PIXEL);
  prop = RNA_def_int(srna, "height", 1024, 1, INT_MAX, "Height", "Image height", 1, 16384);
  RNA_def_property_subtype(prop, PROP_PIXEL);

  /* Only needed when filling the first tile. */
  RNA_def_boolean(
      srna, "float", false, "32-bit Float", "Create image with 32-bit floating-point bit depth");
  RNA_def_boolean(srna, "alpha", true, "Alpha", "Create an image with an alpha channel");
}

static bool tile_add_poll(bContext *C)
{
  Image *ima = CTX_data_edit_image(C);

  return (ima != nullptr && ima->source == IMA_SRC_TILED && BKE_image_has_ibuf(ima, nullptr));
}

static int tile_add_exec(bContext *C, wmOperator *op)
{
  Image *ima = CTX_data_edit_image(C);

  int start_tile = RNA_int_get(op->ptr, "number");
  int end_tile = start_tile + RNA_int_get(op->ptr, "count") - 1;

  if (start_tile < 1001 || end_tile > IMA_UDIM_MAX) {
    BKE_report(op->reports, RPT_ERROR, "Invalid UDIM index range was specified");
    return OPERATOR_CANCELLED;
  }

  bool fill_tile = RNA_boolean_get(op->ptr, "fill");
  char *label = RNA_string_get_alloc(op->ptr, "label", nullptr, 0, nullptr);

  /* BKE_image_add_tile assumes a pre-sorted list of tiles. */
  BKE_image_sort_tiles(ima);

  ImageTile *last_tile_created = nullptr;
  for (int tile_number = start_tile; tile_number <= end_tile; tile_number++) {
    ImageTile *tile = BKE_image_add_tile(ima, tile_number, label);

    if (tile != nullptr) {
      if (fill_tile) {
        do_fill_tile(op->ptr, ima, tile);
      }

      last_tile_created = tile;
    }
  }
  MEM_freeN(label);

  if (!last_tile_created) {
    BKE_report(op->reports, RPT_WARNING, "No UDIM tiles were created");
    return OPERATOR_CANCELLED;
  }

  img->active_tile_index = lib_findindex(&img->tiles, last_tile_created);

  win_ev_add_notifier(C, NC_IMG | ND_DRW, nullptr);
  return OP_FINISHED;
}

static int tile_add_invoke(Cxt *C, WinOp *op, const WinEv * /*event*/)
{
  Img *img = cxt_data_edit_img(C);

  /* Find the first gap in tile numbers or the number after the last if
   * no gap exists. */
  int next_number = 0;
  LIST_FOREACH (ImgTile *, tile, &img->tiles) {
    next_number = tile->tile_number + 1;
    if (tile->next == nullptr || tile->next->tile_number > next_number) {
      break;
    }
  }

  ImgTile *tile = static_cast<ImgTile *>(lib_findlink(&img->tiles, img->active_tile_index));
  tile_fill_init(op->ptr, img, tile);

  api_int_set(op->ptr, "number", next_number);
  api_int_set(op->ptr, "count", 1);
  api_string_set(op->ptr, "label", "");

  return win_op_props_dialog_popup(C, op, 300);
}

static void tile_add_drw(Cxt * /*C*/, WinOp *op)
{
  uiLayout *col;
  uiLayout *layout = op->layout;

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  col = uiLayoutColumn(layout, false);
  uiItemR(col, op->ptr, "number", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "count", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "label", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(layout, op->ptr, "fill", UI_ITEM_NONE, nullptr, ICON_NONE);

  if (api_bool_get(op->ptr, "fill")) {
    drw_fill_tile(op->ptr, layout);
  }
}

void IMG_OT_tile_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Tile";
  ot->description = "Adds a tile to the img";
  ot->idname = "IMG_OT_tile_add";

  /* api cbs */
  ot->poll = tile_add_poll;
  ot->ex = tile_add_ex;
  ot->invoke = tile_add_invoke;
  ot->ui = tile_add_draw;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_int(ot->sapi,
              "number",
              1002,
              1001,
              IMA_UDIM_MAX,
              "Number",
              "UDIM number of the tile",
              1001,
              1099);
  api_def_int(ot->sapi, "count", 1, 1, INT_MAX, "Count", "How many tiles to add", 1, 1000);
  api_def_string(ot->sapi, "label", nullptr, 0, "Label", "Optional tile label");
  api_def_bool(ot->sapi, "fill", true, "Fill", "Fill new tile with a generated img");
  api_fill_tile(ot->sapi);
}

/* Remove Tile Op */
static bool tile_remove_poll(Cxt *C)
{
  Img *img = cxt_data_edit_img(C);

  return (img != nullptr && img->src == IMG_SRC_TILED && !lib_list_is_single(&img->tiles));
}

static int tile_remove_ex(Cxt *C, WinOp * /*op*/)
{
  Img *img = cxt_data_edit_img(C);

  ImTile *tile = static_cast<ImgTile *>(lib_findlink(&img->tiles, ima->active_tile_index));
  if (!dune_img_remove_tile(img, tile)) {
    return OP_CANCELLED;
  }

  /* Ensure that the active index is valid. */
  img->active_tile_index = min_ii(img->active_tile_index, lib_list_count(&img->tiles) - 1);

  win_ev_add_notifier(C, NC_IMG | ND_DRW, nullptr);

  return OP_FINISHED;
}

void IMG_OT_tile_remove(WinOpType *ot)
{
  /* ids */
  ot->name = "Remove Tile";
  ot->description = "Removes a tile from the img";
  ot->idname = "IMG_OT_tile_remove";

  /* api cbs */
  ot->poll = tile_remove_poll;
  ot->ex = tile_remove_ex;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Fill Tile Op */
static bool tile_fill_poll(Cxt *C)
{
  Img *img = cxt_data_edit_img(C);

  if (img != nullptr && img->src == IMG_SRC_TILED) {
    /* Filling secondary tiles is only allowed if the primary tile exists. */
    return (img->active_tile_index == 0) || dune_img_has_ibuf(img, nullptr);
  }
  return false;
}

static int tile_fill_ex(Cxt *C, WinOp *op)
{
  Img *img = cxt_data_edit_img(C);

  ImgTile *tile = static_cast<ImgTile *>(lib_findlink(&img->tiles, img->active_tile_index));
  if (!do_fill_tile(op->ptr, img, tile)) {
    return OP_CANCELLED;
  }

  win_ev_add_notifier(C, NC_IMG | ND_DRW, nullptr);

  return OP_FINISHED;
}

static int tile_fill_invoke(Cxt *C, WinOp *op, const WinEv * /*event*/)
{
  tile_fill_init(op->ptr, cxt_data_edit_img(C), nullptr);

  return win_op_props_dialog_popup(C, op, 300);
}

static void tile_fill_drw(Cxt * /*C*/, WinOp *op)
{
  drw_fill_tile(op->ptr, op->layout);
}

void IMG_OT_tile_fill(WinOpType *ot)
{
  /* ids */
  ot->name = "Fill Tile";
  ot->description = "Fill the current tile with a generated img";
  ot->idname = "IMG_OT_tile_fill";

  /* api cbs */
  ot->poll = tile_fill_poll;
  ot->ex = tile_fill_ex;
  ot->invoke = tile_fill_invoke;
  ot->ui = tile_fill_drw;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  def_fill_tile(ot->srna);
}
  uiLayout *layout = op->layout;

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  col = uiLayoutColumn(layout, false);
  uiItemR(col, op->ptr, "number", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "count", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(col, op->ptr, "label", UI_ITEM_NONE, nullptr, ICON_NONE);
  uiItemR(layout, op->ptr, "fill", UI_ITEM_NONE, nullptr, ICON_NONE);

  if (api_bool_get(op->ptr, "fill")) {
    drw_fill_tile(op->ptr, layout);
  }
}

void IMG_OT_tile_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Tile";
  ot->description = "Adds a tile to the img";
  ot->idname = "IMG_OT_tile_add";

  /* api cbs */
  ot->poll = tile_add_poll;
  ot->ex = tile_add_ex;
  ot->invoke = tile_add_invoke;
  ot->ui = tile_add_drw;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_int(ot->sapi,
              "number",
              1002,
              1001,
              IMA_UDIM_MAX,
              "Number",
              "UDIM number of the tile",
              1001,
              1099);
  api_def_int(ot->sapi, "count", 1, 1, INT_MAX, "Count", "How many tiles to add", 1, 1000);
  api_def_string(ot->sapi, "label", nullptr, 0, "Label", "Optional tile label");
  api_def_bool(ot->sapi, "fill", true, "Fill", "Fill new tile with a generated img");
  def_fill_tile(ot->sapi);
}

/* Remove Tile Op */
static bool tile_remove_poll(Cxt *C)
{
  Img *img = cxt_data_edit_img(C);

  return (img != nullptr && img->srx == IMG_SRC_TILED && !lib_list_is_single(&img->tiles));
}

static int tile_remove_ex(Cxt *C, WinOp * /*op*/)
{
  Img *img = cxt_data_edit_img(C);

  ImgTile *tile = static_cast<ImgTile *>(lib_findlink(&img->tiles, img->active_tile_index));
  if (!dune_img_remove_tile(img, tile)) {
    return OP_CANCELLED;
  }

  /* Ensure that the active index is valid. */
  ima->active_tile_index = min_ii(img->active_tile_index, lib_list_count(&ima->tiles) - 1);

  win_ev_add_notifier(C, NC_IMG | ND_DRW, nullptr);

  return OP_FINISHED;
}

void IMG_OT_tile_remove(WinOpType *ot)
{
  /* ids */
  ot->name = "Remove Tile";
  ot->description = "Removes a tile from the img";
  ot->idname = "IMG_OT_tile_remove";

  /* api cbs */
  ot->poll = tile_remove_poll;
  ot->ex = tile_remove_ex;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Fill Tile Op */
static bool tile_fill_poll(Cxt *C)
{
  Img *img = cxt_data_edit_img(C);

  if (img != nullptr && img->src == IMG_SRC_TILED) {
    /* Filling secondary tiles is only allowed if the primary tile exists. */
    return (img->active_tile_index == 0) || dune_img_has_ibuf(img, nullptr);
  }
  return false;
}

static int tile_fill_ex(Cxt *C, WinOp *op)
{
  Img *img = cxt_data_edit_img(C);

  ImgTile *tile = static_cast<ImgTile *>(lib_findlink(&ima->tiles, ima->active_tile_index));
  if (!do_fill_tile(op->ptr, ima, tile)) {
    return OP_CANCELLED;
  }

  win_ev_add_notifier(C, NC_IMG | ND_DRW, nullptr);

  return OP_FINISHED;
}

static int tile_fill_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  tile_fill_init(op->ptr, cxt_data_edit_img(C), nullptr);

  return won_op_props_dialog_popup(C, op, 300);
}

static void tile_fill_drw(Cxt * /*C*/, WinOp *op)
{
  draw_fill_tile(op->ptr, op->layout);
}

void IMG_OT_tile_fill(WinOpType *ot)
{
  /* ids */
  ot->name = "Fill Tile";
  ot->description = "Fill the current tile with a generated img";
  ot->idname = "IMG_OT_tile_fill";

  /* api cbs */
  ot->poll = tile_fill_poll;
  ot->ex = tile_fill_ex;
  ot->invoke = tile_fill_invoke;
  ot->ui = tile_fill_drw;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  def_fill_tile(ot->srna);
}
