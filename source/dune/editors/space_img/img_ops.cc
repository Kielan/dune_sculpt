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

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_fileops.h"
#include "BLI_ghash.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_camera_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BKE_colortools.h"
#include "BKE_context.hh"
#include "BKE_global.h"
#include "BKE_icons.h"
#include "BKE_image.h"
#include "BKE_image_format.h"
#include "BKE_image_save.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_packedFile.h"
#include "BKE_report.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.hh"

#include "GPU_state.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_moviecache.h"
#include "IMB_openexr.h"

#include "RE_pipeline.h"

#include "RNA_access.hh"
#include "RNA_define.hh"
#include "RNA_enum_types.hh"
#include "RNA_prototypes.h"

#include "ED_image.hh"
#include "ED_mask.hh"
#include "ED_paint.hh"
#include "ED_render.hh"
#include "ED_screen.hh"
#include "ED_space_api.hh"
#include "ED_undo.hh"
#include "ED_util.hh"
#include "ED_util_imbuf.hh"
#include "ED_uvedit.hh"

#include "UI_interface.hh"
#include "UI_resources.hh"
#include "UI_view2d.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "PIL_time.h"

#include "RE_engine.h"

#include "image_intern.h"

/* -------------------------------------------------------------------- */
/** \name View Navigation Utilities
 * \{ */

static void sima_zoom_set(
    SpaceImage *sima, ARegion *region, float zoom, const float location[2], const bool zoom_to_pos)
{
  float oldzoom = sima->zoom;
  int width, height;

  sima->zoom = zoom;

  if (sima->zoom < 0.1f || sima->zoom > 4.0f) {
    /* check zoom limits */
    ED_space_image_get_size(sima, &width, &height);

    width *= sima->zoom;
    height *= sima->zoom;

    if ((width < 4) && (height < 4) && sima->zoom < oldzoom) {
      sima->zoom = oldzoom;
    }
    else if (BLI_rcti_size_x(&region->winrct) <= sima->zoom) {
      sima->zoom = oldzoom;
    }
    else if (BLI_rcti_size_y(&region->winrct) <= sima->zoom) {
      sima->zoom = oldzoom;
    }
  }

  if (zoom_to_pos && location) {
    float aspx, aspy, w, h;

    ED_space_image_get_size(sima, &width, &height);
    ED_space_image_get_aspect(sima, &aspx, &aspy);

    w = width * aspx;
    h = height * aspy;

    sima->xof += ((location[0] - 0.5f) * w - sima->xof) * (sima->zoom - oldzoom) / sima->zoom;
    sima->yof += ((location[1] - 0.5f) * h - sima->yof) * (sima->zoom - oldzoom) / sima->zoom;
  }
}

static void sima_zoom_set_factor(SpaceImage *sima,
                                 ARegion *region,
                                 float zoomfac,
                                 const float location[2],
                                 const bool zoom_to_pos)
{
  sima_zoom_set(sima, region, sima->zoom * zoomfac, location, zoom_to_pos);
}

/**
 * Fits the view to the bounds exactly, caller should add margin if needed.
 */
static void sima_zoom_set_from_bounds(SpaceImage *sima, ARegion *region, const rctf *bounds)
{
  int image_size[2];
  float aspx, aspy;

  ED_space_image_get_size(sima, &image_size[0], &image_size[1]);
  ED_space_image_get_aspect(sima, &aspx, &aspy);

  image_size[0] = image_size[0] * aspx;
  image_size[1] = image_size[1] * aspy;

  /* adjust offset and zoom */
  sima->xof = roundf((BLI_rctf_cent_x(bounds) - 0.5f) * image_size[0]);
  sima->yof = roundf((BLI_rctf_cent_y(bounds) - 0.5f) * image_size[1]);

  float size_xy[2], size;
  size_xy[0] = BLI_rcti_size_x(&region->winrct) / (BLI_rctf_size_x(bounds) * image_size[0]);
  size_xy[1] = BLI_rcti_size_y(&region->winrct) / (BLI_rctf_size_y(bounds) * image_size[1]);

  size = min_ff(size_xy[0], size_xy[1]);
  CLAMP_MAX(size, 100.0f);

  sima_zoom_set(sima, region, size, nullptr, false);
}

static Image *image_from_context(const bContext *C)
{
  /* Edit image is set by templates used throughout the interface, so image
   * operations work outside the image editor. */
  Image *ima = static_cast<Image *>(CTX_data_pointer_get_type(C, "edit_image", &RNA_Image).data);

  if (ima) {
    return ima;
  }

  /* Image editor. */
  SpaceImage *sima = CTX_wm_space_image(C);
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
 * Use this when the image buffer is accessing the active tile without the image user.
 */
static bool image_from_context_has_data_poll_active_tile(bContext *C)
{
  Image *ima = image_from_context(C);
  ImageUser iuser = image_user_from_context_and_active_tile(C, ima);

  return BKE_image_has_ibuf(ima, &iuser);
}

static bool image_not_packed_poll(bContext *C)
{
  /* Do not run 'replace' on packed images, it does not give user expected results at all. */
  Image *ima = image_from_context(C);
  return (ima && BLI_listbase_is_empty(&ima->packedfiles));
}

static void image_view_all(SpaceImage *sima, ARegion *region, wmOperator *op)
{
  float aspx, aspy, zoomx, zoomy, w, h;
  int width, height;
  const bool fit_view = RNA_boolean_get(op->ptr, "fit_view");

  ED_space_image_get_size(sima, &width, &height);
  ED_space_image_get_aspect(sima, &aspx, &aspy);

  w = width * aspx;
  h = height * aspy;

  float xof = 0.0f, yof = 0.0f;
  if ((sima->image == nullptr) || (sima->image->source == IMA_SRC_TILED)) {
    /* Extend the shown area to cover all UDIM tiles. */
    int x_tiles, y_tiles;
    if (sima->image == nullptr) {
      x_tiles = sima->tile_grid_shape[0];
      y_tiles = sima->tile_grid_shape[1];
    }
    else {
      x_tiles = y_tiles = 1;
      LISTBASE_FOREACH (ImageTile *, tile, &sima->image->tiles) {
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

  /* check if the image will fit in the image with (zoom == 1) */
  width = BLI_rcti_size_x(&region->winrct) + 1;
  height = BLI_rcti_size_y(&region->winrct) + 1;

  if (fit_view) {
    const int margin = 5; /* margin from border */

    zoomx = float(width) / (w + 2 * margin);
    zoomy = float(height) / (h + 2 * margin);

    sima_zoom_set(sima, region, min_ff(zoomx, zoomy), nullptr, false);
  }
  else {
    if ((w >= width || h >= height) && (width > 0 && height > 0)) {
      zoomx = float(width) / w;
      zoomy = float(height) / h;

      /* find the zoom value that will fit the image in the image space */
      sima_zoom_set(sima, region, 1.0f / power_of_2(1.0f / min_ff(zoomx, zoomy)), nullptr, false);
    }
    else {
      sima_zoom_set(sima, region, 1.0f, nullptr, false);
    }
  }

  sima->xof = xof;
  sima->yof = yof;
}

bool space_image_main_region_poll(bContext *C)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  // ARegion *region = CTX_wm_region(C); /* XXX. */

  if (sima) {
    return true; /* XXX (region && region->type->regionid == RGN_TYPE_WINDOW); */
  }
  return false;
}

/* For IMAGE_OT_curves_point_set to avoid sampling when in uv smooth mode or editmode */
static bool space_image_main_area_not_uv_brush_poll(bContext *C)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  Scene *scene = CTX_data_scene(C);
  ToolSettings *toolsettings = scene->toolsettings;

  if (sima && !toolsettings->uvsculpt && (CTX_data_edit_object(C) == nullptr)) {
    return true;
  }

  return false;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name View Pan Operator
 * \{ */

struct ViewPanData {
  float x, y;
  float xof, yof;
  int launch_event;
  bool own_cursor;
};

static void image_view_pan_init(bContext *C, wmOperator *op, const wmEvent *event)
{
  wmWindow *win = CTX_wm_window(C);
  SpaceImage *sima = CTX_wm_space_image(C);
  ViewPanData *vpd;

  op->customdata = vpd = static_cast<ViewPanData *>(
      MEM_callocN(sizeof(ViewPanData), "ImageViewPanData"));

  /* Grab will be set when running from gizmo. */
  vpd->own_cursor = (win->grabcursor == 0);
  if (vpd->own_cursor) {
    WM_cursor_modal_set(win, WM_CURSOR_NSEW_SCROLL);
  }

  vpd->x = event->xy[0];
  vpd->y = event->xy[1];
  vpd->xof = sima->xof;
  vpd->yof = sima->yof;
  vpd->launch_event = WM_userdef_event_type_from_keymap_type(event->type);

  WM_event_add_modal_handler(C, op);
}

static void image_view_pan_exit(bContext *C, wmOperator *op, bool cancel)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ViewPanData *vpd = static_cast<ViewPanData *>(op->customdata);

  if (cancel) {
    sima->xof = vpd->xof;
    sima->yof = vpd->yof;
    ED_region_tag_redraw(CTX_wm_region(C));
  }

  if (vpd->own_cursor) {
    WM_cursor_modal_restore(CTX_wm_window(C));
  }
  MEM_freeN(op->customdata);
}

static int image_view_pan_exec(bContext *C, wmOperator *op)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  float offset[2];

  RNA_float_get_array(op->ptr, "offset", offset);
  sima->xof += offset[0];
  sima->yof += offset[1];

  ED_region_tag_redraw(CTX_wm_region(C));

  return OPERATOR_FINISHED;
}

static int image_view_pan_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  if (event->type == MOUSEPAN) {
    SpaceImage *sima = CTX_wm_space_image(C);
    float offset[2];

    offset[0] = (event->prev_xy[0] - event->xy[0]) / sima->zoom;
    offset[1] = (event->prev_xy[1] - event->xy[1]) / sima->zoom;
    RNA_float_set_array(op->ptr, "offset", offset);

    image_view_pan_exec(C, op);
    return OPERATOR_FINISHED;
  }

  image_view_pan_init(C, op, event);
  return OPERATOR_RUNNING_MODAL;
}

static int image_view_pan_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ViewPanData *vpd = static_cast<ViewPanData *>(op->customdata);
  float offset[2];

  switch (event->type) {
    case MOUSEMOVE:
      sima->xof = vpd->xof;
      sima->yof = vpd->yof;
      offset[0] = (vpd->x - event->xy[0]) / sima->zoom;
      offset[1] = (vpd->y - event->xy[1]) / sima->zoom;
      RNA_float_set_array(op->ptr, "offset", offset);
      image_view_pan_exec(C, op);
      break;
    default:
      if (event->type == vpd->launch_event && event->val == KM_RELEASE) {
        image_view_pan_exit(C, op, false);
        return OPERATOR_FINISHED;
      }
      break;
  }

  return OPERATOR_RUNNING_MODAL;
}

static void image_view_pan_cancel(bContext *C, wmOperator *op)
{
  image_view_pan_exit(C, op, true);
}

void IMAGE_OT_view_pan(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Pan View";
  ot->idname = "IMAGE_OT_view_pan";
  ot->description = "Pan the view";

  /* api callbacks */
  ot->exec = image_view_pan_exec;
  ot->invoke = image_view_pan_invoke;
  ot->modal = image_view_pan_modal;
  ot->cancel = image_view_pan_cancel;
  ot->poll = space_image_main_region_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_GRAB_CURSOR_XY | OPTYPE_LOCK_BYPASS;

  /* properties */
  RNA_def_float_vector(ot->srna,
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

/** \} */

/* -------------------------------------------------------------------- */
/** \name View Zoom Operator
 * \{ */

struct ViewZoomData {
  float origx, origy;
  float zoom;
  int launch_event;
  float location[2];

  /* needed for continuous zoom */
  wmTimer *timer;
  double timer_lastdraw;
  bool own_cursor;

  /* */
  SpaceImage *sima;
  ARegion *region;
};

static void image_view_zoom_init(bContext *C, wmOperator *op, const wmEvent *event)
{
  wmWindow *win = CTX_wm_window(C);
  SpaceImage *sima = CTX_wm_space_image(C);
  ARegion *region = CTX_wm_region(C);
  ViewZoomData *vpd;

  op->customdata = vpd = static_cast<ViewZoomData *>(
      MEM_callocN(sizeof(ViewZoomData), "ImageViewZoomData"));

  /* Grab will be set when running from gizmo. */
  vpd->own_cursor = (win->grabcursor == 0);
  if (vpd->own_cursor) {
    WM_cursor_modal_set(win, WM_CURSOR_NSEW_SCROLL);
  }

  vpd->origx = event->xy[0];
  vpd->origy = event->xy[1];
  vpd->zoom = sima->zoom;
  vpd->launch_event = WM_userdef_event_type_from_keymap_type(event->type);

  UI_view2d_region_to_view(
      &region->v2d, event->mval[0], event->mval[1], &vpd->location[0], &vpd->location[1]);

  if (U.viewzoom == USER_ZOOM_CONTINUE) {
    /* needs a timer to continue redrawing */
    vpd->timer = WM_event_timer_add(CTX_wm_manager(C), CTX_wm_window(C), TIMER, 0.01f);
    vpd->timer_lastdraw = PIL_check_seconds_timer();
  }

  vpd->sima = sima;
  vpd->region = region;

  WM_event_add_modal_handler(C, op);
}

static void image_view_zoom_exit(bContext *C, wmOperator *op, bool cancel)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ViewZoomData *vpd = static_cast<ViewZoomData *>(op->customdata);

  if (cancel) {
    sima->zoom = vpd->zoom;
    ED_region_tag_redraw(CTX_wm_region(C));
  }

  if (vpd->timer) {
    WM_event_timer_remove(CTX_wm_manager(C), vpd->timer->win, vpd->timer);
  }

  if (vpd->own_cursor) {
    WM_cursor_modal_restore(CTX_wm_window(C));
  }
  MEM_freeN(op->customdata);
}

static int image_view_zoom_exec(bContext *C, wmOperator *op)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ARegion *region = CTX_wm_region(C);

  sima_zoom_set_factor(sima, region, RNA_float_get(op->ptr, "factor"), nullptr, false);

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

enum {
  VIEW_PASS = 0,
  VIEW_APPLY,
  VIEW_CONFIRM,
};

static int image_view_zoom_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  if (ELEM(event->type, MOUSEZOOM, MOUSEPAN)) {
    SpaceImage *sima = CTX_wm_space_image(C);
    ARegion *region = CTX_wm_region(C);
    float delta, factor, location[2];

    UI_view2d_region_to_view(
        &region->v2d, event->mval[0], event->mval[1], &location[0], &location[1]);

    delta = event->prev_xy[0] - event->xy[0] + event->prev_xy[1] - event->xy[1];

    if (U.uiflag & USER_ZOOM_INVERT) {
      delta *= -1;
    }

    factor = 1.0f + delta / 300.0f;
    RNA_float_set(op->ptr, "factor", factor);
    const bool use_cursor_init = RNA_boolean_get(op->ptr, "use_cursor_init");
    sima_zoom_set(sima,
                  region,
                  sima->zoom * factor,
                  location,
                  (use_cursor_init && (U.uiflag & USER_ZOOM_TO_MOUSEPOS)));
    ED_region_tag_redraw(region);

    return OPERATOR_FINISHED;
  }

  image_view_zoom_init(C, op, event);
  return OPERATOR_RUNNING_MODAL;
}

static void image_zoom_apply(ViewZoomData *vpd,
                             wmOperator *op,
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
    float time_step = float(time - vpd->timer_lastdraw);
    float zfac;
    zfac = 1.0f + ((delta / 20.0f) * time_step);
    vpd->timer_lastdraw = time;
    /* this is the final zoom, but instead make it into a factor */
    factor = (vpd->sima->zoom * zfac) / vpd->zoom;
  }
  else {
    factor = 1.0f + delta / 300.0f;
  }

  RNA_float_set(op->ptr, "factor", factor);
  sima_zoom_set(vpd->sima, vpd->region, vpd->zoom * factor, vpd->location, zoom_to_pos);
  ED_region_tag_redraw(vpd->region);
}

static int image_view_zoom_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  ViewZoomData *vpd = static_cast<ViewZoomData *>(op->customdata);
  short event_code = VIEW_PASS;
  int ret = OPERATOR_RUNNING_MODAL;

  /* Execute the events. */
  if (event->type == MOUSEMOVE) {
    event_code = VIEW_APPLY;
  }
  else if (event->type == TIMER) {
    /* Continuous zoom. */
    if (event->customdata == vpd->timer) {
      event_code = VIEW_APPLY;
    }
  }
  else if (event->type == vpd->launch_event) {
    if (event->val == KM_RELEASE) {
      event_code = VIEW_CONFIRM;
    }
  }

  switch (event_code) {
    case VIEW_APPLY: {
      const bool use_cursor_init = RNA_boolean_get(op->ptr, "use_cursor_init");
      image_zoom_apply(vpd,
                       op,
                       event->xy[0],
                       event->xy[1],
                       U.viewzoom,
                       (U.uiflag & USER_ZOOM_INVERT) != 0,
                       (use_cursor_init && (U.uiflag & USER_ZOOM_TO_MOUSEPOS)));
      break;
    }
    case VIEW_CONFIRM: {
      ret = OPERATOR_FINISHED;
      break;
    }
  }

  if ((ret & OPERATOR_RUNNING_MODAL) == 0) {
    image_view_zoom_exit(C, op, false);
  }

  return ret;
}

static void image_view_zoom_cancel(bContext *C, wmOperator *op)
{
  image_view_zoom_exit(C, op, true);
}

void IMAGE_OT_view_zoom(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Zoom View";
  ot->idname = "IMAGE_OT_view_zoom";
  ot->description = "Zoom in/out the image";

  /* api callbacks */
  ot->exec = image_view_zoom_exec;
  ot->invoke = image_view_zoom_invoke;
  ot->modal = image_view_zoom_modal;
  ot->cancel = image_view_zoom_cancel;
  ot->poll = space_image_main_region_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_GRAB_CURSOR_XY | OPTYPE_LOCK_BYPASS;

  /* properties */
  prop = RNA_def_float(ot->srna,
                       "factor",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "Factor",
                       "Zoom factor, values higher than 1.0 zoom in, lower values zoom out",
                       -FLT_MAX,
                       FLT_MAX);
  RNA_def_property_flag(prop, PROP_HIDDEN);

  WM_operator_properties_use_cursor_init(ot);
}

/** \} */

#ifdef WITH_INPUT_NDOF

/* -------------------------------------------------------------------- */
/** \name NDOF Operator
 * \{ */

/* Combined pan/zoom from a 3D mouse device.
 * Z zooms, XY pans
 * "view" (not "paper") control -- user moves the viewpoint, not the image being viewed
 * that explains the negative signs in the code below
 */

static int image_view_ndof_invoke(bContext *C, wmOperator * /*op*/, const wmEvent *event)
{
  if (event->type != NDOF_MOTION) {
    return OPERATOR_CANCELLED;
  }

  SpaceImage *sima = CTX_wm_space_image(C);
  ARegion *region = CTX_wm_region(C);
  float pan_vec[3];

  const wmNDOFMotionData *ndof = static_cast<const wmNDOFMotionData *>(event->customdata);
  const float pan_speed = NDOF_PIXELS_PER_SECOND;

  WM_event_ndof_pan_get(ndof, pan_vec, true);

  mul_v3_fl(pan_vec, ndof->dt);
  mul_v2_fl(pan_vec, pan_speed / sima->zoom);

  sima_zoom_set_factor(sima, region, max_ff(0.0f, 1.0f - pan_vec[2]), nullptr, false);
  sima->xof += pan_vec[0];
  sima->yof += pan_vec[1];

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_view_ndof(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "NDOF Pan/Zoom";
  ot->idname = "IMAGE_OT_view_ndof";
  ot->description = "Use a 3D mouse device to pan/zoom the view";

  /* api callbacks */
  ot->invoke = image_view_ndof_invoke;
  ot->poll = space_image_main_region_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;
}

/** \} */

#endif /* WITH_INPUT_NDOF */

/* -------------------------------------------------------------------- */
/** \name View All Operator
 * \{ */

/* Updates the fields of the View2D member of the SpaceImage struct.
 * Default behavior is to reset the position of the image and set the zoom to 1
 * If the image will not fit within the window rectangle, the zoom is adjusted */

static int image_view_all_exec(bContext *C, wmOperator *op)
{
  SpaceImage *sima;
  ARegion *region;

  /* retrieve state */
  sima = CTX_wm_space_image(C);
  region = CTX_wm_region(C);

  image_view_all(sima, region, op);

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_view_all(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Frame All";
  ot->idname = "IMAGE_OT_view_all";
  ot->description = "View the entire image";

  /* api callbacks */
  ot->exec = image_view_all_exec;
  ot->poll = space_image_main_region_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;

  /* properties */
  prop = RNA_def_boolean(ot->srna, "fit_view", false, "Fit View", "Fit frame to the viewport");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Cursor To Center View Operator
 * \{ */

static int view_cursor_center_exec(bContext *C, wmOperator *op)
{
  SpaceImage *sima;
  ARegion *region;

  sima = CTX_wm_space_image(C);
  region = CTX_wm_region(C);

  image_view_all(sima, region, op);

  sima->cursor[0] = 0.5f;
  sima->cursor[1] = 0.5f;

  /* Needed for updating the cursor. */
  WM_event_add_notifier(C, NC_SPACE | ND_SPACE_IMAGE, nullptr);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_view_cursor_center(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Cursor To Center View";
  ot->description = "Set 2D Cursor To Center View location";
  ot->idname = "IMAGE_OT_view_cursor_center";

  /* api callbacks */
  ot->exec = view_cursor_center_exec;
  ot->poll = ED_space_image_cursor_poll;

  /* properties */
  prop = RNA_def_boolean(ot->srna, "fit_view", false, "Fit View", "Fit frame to the viewport");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Center View To Cursor Operator
 * \{ */

static int view_center_cursor_exec(bContext *C, wmOperator * /*op*/)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ARegion *region = CTX_wm_region(C);

  ED_image_view_center_to_point(sima, sima->cursor[0], sima->cursor[1]);

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_view_center_cursor(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Center View to Cursor";
  ot->description = "Center the view so that the cursor is in the middle of the view";
  ot->idname = "IMAGE_OT_view_center_cursor";

  /* api callbacks */
  ot->exec = view_center_cursor_exec;
  ot->poll = ED_space_image_cursor_poll;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Frame Selected Operator
 * \{ */

static int image_view_selected_exec(bContext *C, wmOperator * /*op*/)
{
  SpaceImage *sima;
  ARegion *region;
  Scene *scene;
  ViewLayer *view_layer;
  Object *obedit;

  /* retrieve state */
  sima = CTX_wm_space_image(C);
  region = CTX_wm_region(C);
  scene = CTX_data_scene(C);
  view_layer = CTX_data_view_layer(C);
  obedit = CTX_data_edit_object(C);

  /* get bounds */
  float min[2], max[2];
  if (ED_space_image_show_uvedit(sima, obedit)) {
    uint objects_len = 0;
    Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data_with_uvs(
        scene, view_layer, ((View3D *)nullptr), &objects_len);
    bool success = ED_uvedit_minmax_multi(scene, objects, objects_len, min, max);
    MEM_freeN(objects);
    if (!success) {
      return OPERATOR_CANCELLED;
    }
  }
  else if (ED_space_image_check_show_maskedit(sima, obedit)) {
    if (!ED_mask_selected_minmax(C, min, max, false)) {
      return OPERATOR_CANCELLED;
    }
  }
  rctf bounds{};
  bounds.xmin = min[0];
  bounds.ymin = min[1];
  bounds.xmax = max[0];
  bounds.ymax = max[1];

  /* add some margin */
  BLI_rctf_scale(&bounds, 1.4f);

  sima_zoom_set_from_bounds(sima, region, &bounds);

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

static bool image_view_selected_poll(bContext *C)
{
  return (space_image_main_region_poll(C) && (ED_operator_uvedit(C) || ED_maskedit_poll(C)));
}

void IMAGE_OT_view_selected(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "View Center";
  ot->idname = "IMAGE_OT_view_selected";
  ot->description = "View all selected UVs";

  /* api callbacks */
  ot->exec = image_view_selected_exec;
  ot->poll = image_view_selected_poll;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name View Zoom In/Out Operator
 * \{ */

static int image_view_zoom_in_exec(bContext *C, wmOperator *op)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ARegion *region = CTX_wm_region(C);
  float location[2];

  RNA_float_get_array(op->ptr, "location", location);

  sima_zoom_set_factor(
      sima, region, powf(2.0f, 1.0f / 3.0f), location, U.uiflag & USER_ZOOM_TO_MOUSEPOS);

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

static int image_view_zoom_in_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  ARegion *region = CTX_wm_region(C);
  float location[2];

  UI_view2d_region_to_view(
      &region->v2d, event->mval[0], event->mval[1], &location[0], &location[1]);
  RNA_float_set_array(op->ptr, "location", location);

  return image_view_zoom_in_exec(C, op);
}

void IMAGE_OT_view_zoom_in(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Zoom In";
  ot->idname = "IMAGE_OT_view_zoom_in";
  ot->description = "Zoom in the image (centered around 2D cursor)";

  /* api callbacks */
  ot->invoke = image_view_zoom_in_invoke;
  ot->exec = image_view_zoom_in_exec;
  ot->poll = space_image_main_region_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;

  /* properties */
  prop = RNA_def_float_vector(ot->srna,
                              "location",
                              2,
                              nullptr,
                              -FLT_MAX,
                              FLT_MAX,
                              "Location",
                              "Cursor location in screen coordinates",
                              -10.0f,
                              10.0f);
  RNA_def_property_flag(prop, PROP_HIDDEN);
}

static int image_view_zoom_out_exec(bContext *C, wmOperator *op)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ARegion *region = CTX_wm_region(C);
  float location[2];

  RNA_float_get_array(op->ptr, "location", location);

  sima_zoom_set_factor(
      sima, region, powf(0.5f, 1.0f / 3.0f), location, U.uiflag & USER_ZOOM_TO_MOUSEPOS);

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

static int image_view_zoom_out_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  ARegion *region = CTX_wm_region(C);
  float location[2];

  UI_view2d_region_to_view(
      &region->v2d, event->mval[0], event->mval[1], &location[0], &location[1]);
  RNA_float_set_array(op->ptr, "location", location);

  return image_view_zoom_out_exec(C, op);
}

void IMAGE_OT_view_zoom_out(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Zoom Out";
  ot->idname = "IMAGE_OT_view_zoom_out";
  ot->description = "Zoom out the image (centered around 2D cursor)";

  /* api callbacks */
  ot->invoke = image_view_zoom_out_invoke;
  ot->exec = image_view_zoom_out_exec;
  ot->poll = space_image_main_region_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;

  /* properties */
  prop = RNA_def_float_vector(ot->srna,
                              "location",
                              2,
                              nullptr,
                              -FLT_MAX,
                              FLT_MAX,
                              "Location",
                              "Cursor location in screen coordinates",
                              -10.0f,
                              10.0f);
  RNA_def_property_flag(prop, PROP_HIDDEN);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name View Zoom Ratio Operator
 * \{ */

static int image_view_zoom_ratio_exec(bContext *C, wmOperator *op)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ARegion *region = CTX_wm_region(C);

  sima_zoom_set(sima, region, RNA_float_get(op->ptr, "ratio"), nullptr, false);

  /* ensure pixel exact locations for draw */
  sima->xof = int(sima->xof);
  sima->yof = int(sima->yof);

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_view_zoom_ratio(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "View Zoom Ratio";
  ot->idname = "IMAGE_OT_view_zoom_ratio";
  ot->description = "Set zoom ratio of the view";

  /* api callbacks */
  ot->exec = image_view_zoom_ratio_exec;
  ot->poll = space_image_main_region_poll;

  /* flags */
  ot->flag = OPTYPE_LOCK_BYPASS;

  /* properties */
  RNA_def_float(ot->srna,
                "ratio",
                0.0f,
                -FLT_MAX,
                FLT_MAX,
                "Ratio",
                "Zoom ratio, 1.0 is 1:1, higher is zoomed in, lower is zoomed out",
                -FLT_MAX,
                FLT_MAX);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name View Border-Zoom Operator
 * \{ */

static int image_view_zoom_border_exec(bContext *C, wmOperator *op)
{
  SpaceImage *sima = CTX_wm_space_image(C);
  ARegion *region = CTX_wm_region(C);
  rctf bounds;
  const bool zoom_in = !RNA_boolean_get(op->ptr, "zoom_out");

  WM_operator_properties_border_to_rctf(op, &bounds);

  UI_view2d_region_to_view_rctf(&region->v2d, &bounds, &bounds);

  struct {
    float xof;
    float yof;
    float zoom;
  } sima_view_prev{};
  sima_view_prev.xof = sima->xof;
  sima_view_prev.yof = sima->yof;
  sima_view_prev.zoom = sima->zoom;

  sima_zoom_set_from_bounds(sima, region, &bounds);

  /* zoom out */
  if (!zoom_in) {
    sima->xof = sima_view_prev.xof + (sima->xof - sima_view_prev.xof);
    sima->yof = sima_view_prev.yof + (sima->yof - sima_view_prev.yof);
    sima->zoom = sima_view_prev.zoom * (sima_view_prev.zoom / sima->zoom);
  }

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_view_zoom_border(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Zoom to Border";
  ot->description = "Zoom in the view to the nearest item contained in the border";
  ot->idname = "IMAGE_OT_view_zoom_border";

  /* api callbacks */
  ot->invoke = WM_gesture_box_invoke;
  ot->exec = image_view_zoom_border_exec;
  ot->modal = WM_gesture_box_modal;
  ot->cancel = WM_gesture_box_cancel;

  ot->poll = space_image_main_region_poll;

  /* rna */
  WM_operator_properties_gesture_box_zoom(ot);
}

/* load/replace/save callbacks */
static void image_filesel(bContext *C, wmOperator *op, const char *path)
{
  RNA_string_set(op->ptr, "filepath", path);
  WM_event_add_fileselect(C, op);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Open Image Operator
 * \{ */

struct ImageOpenData {
  PropertyPointerRNA pprop;
  ImageUser *iuser;
  ImageFormatData im_format;
};

static void image_open_init(bContext *C, wmOperator *op)
{
  ImageOpenData *iod;
  op->customdata = iod = static_cast<ImageOpenData *>(
      MEM_callocN(sizeof(ImageOpenData), __func__));
  iod->iuser = static_cast<ImageUser *>(
      CTX_data_pointer_get_type(C, "image_user", &RNA_ImageUser).data);
  UI_context_active_but_prop_get_templateID(C, &iod->pprop.ptr, &iod->pprop.prop);
}

static void image_open_cancel(bContext * /*C*/, wmOperator *op)
{
  MEM_freeN(op->customdata);
  op->customdata = nullptr;
}

static Image *image_open_single(Main *bmain,
                                wmOperator *op,
                                ImageFrameRange *range,
                                bool use_multiview)
{
  bool exists = false;
  Image *ima = nullptr;

  errno = 0;
  ima = BKE_image_load_exists_ex(bmain, range->filepath, &exists);

  if (!ima) {
    if (op->customdata) {
      MEM_freeN(op->customdata);
    }
    BKE_reportf(op->reports,
                RPT_ERROR,
                "Cannot read '%s': %s",
                range->filepath,
                errno ? strerror(errno) : TIP_("unsupported image format"));
    return nullptr;
  }

  /* If image already exists, update its file path based on relative path property, see: #109561.
   */
  if (exists) {
    STRNCPY(ima->filepath, range->filepath);
    return ima;
  }

  /* handle multiview images */
  if (use_multiview) {
    ImageOpenData *iod = static_cast<ImageOpenData *>(op->customdata);
    ImageFormatData *imf = &iod->im_format;

    ima->flag |= IMA_USE_VIEWS;
    ima->views_format = imf->views_format;
    *ima->stereo3d_format = imf->stereo3d_format;
  }
  else {
    ima->flag &= ~IMA_USE_VIEWS;
    BKE_image_free_views(ima);
  }

  if (ima->source == IMA_SRC_FILE) {
    if (range->udims_detected && range->udim_tiles.first) {
      ima->source = IMA_SRC_TILED;
      ImageTile *first_tile = static_cast<ImageTile *>(ima->tiles.first);
      first_tile->tile_number = range->offset;
      LISTBASE_FOREACH (LinkData *, node, &range->udim_tiles) {
        BKE_image_add_tile(ima, POINTER_AS_INT(node->data), nullptr);
      }
    }
    else if (range->length > 1) {
      ima->source = IMA_SRC_SEQUENCE;
    }
  }

  return ima;
}

static int image_open_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  ScrArea *area = CTX_wm_area(C);
  Scene *scene = CTX_data_scene(C);
  ImageUser *iuser = nullptr;
  Image *ima = nullptr;
  int frame_seq_len = 0;
  int frame_ofs = 1;

  const bool use_multiview = RNA_boolean_get(op->ptr, "use_multiview");
  const bool use_udim = RNA_boolean_get(op->ptr, "use_udim_detecting");

  if (!op->customdata) {
    image_open_init(C, op);
  }

  ListBase ranges = ED_image_filesel_detect_sequences(bmain, op, use_udim);
  LISTBASE_FOREACH (ImageFrameRange *, range, &ranges) {
    Image *ima_range = image_open_single(bmain, op, range, use_multiview);

    /* take the first image */
    if ((ima == nullptr) && ima_range) {
      ima = ima_range;
      frame_seq_len = range->length;
      frame_ofs = range->offset;
    }

    BLI_freelistN(&range->udim_tiles);
  }
  BLI_freelistN(&ranges);

  if (ima == nullptr) {
    return OPERATOR_CANCELLED;
  }

  /* hook into UI */
  ImageOpenData *iod = static_cast<ImageOpenData *>(op->customdata);

  if (iod->pprop.prop) {
    /* when creating new ID blocks, use is already 1, but RNA
     * pointer use also increases user, so this compensates it */
    id_us_min(&ima->id);

    PointerRNA imaptr = RNA_id_pointer_create(&ima->id);
    RNA_property_pointer_set(&iod->pprop.ptr, iod->pprop.prop, imaptr, nullptr);
    RNA_property_update(C, &iod->pprop.ptr, iod->pprop.prop);
  }

  if (iod->iuser) {
    iuser = iod->iuser;
  }
  else if (area && area->spacetype == SPACE_IMAGE) {
    SpaceImage *sima = static_cast<SpaceImage *>(area->spacedata.first);
    ED_space_image_set(bmain, sima, ima, false);
    iuser = &sima->iuser;
  }
  else {
    Tex *tex = static_cast<Tex *>(CTX_data_pointer_get_type(C, "texture", &RNA_Texture).data);
    if (tex && tex->type == TEX_IMAGE) {
      iuser = &tex->iuser;
    }

    if (iuser == nullptr) {
      Camera *cam = static_cast<Camera *>(
          CTX_data_pointer_get_type(C, "camera", &RNA_Camera).data);
      if (cam) {
        LISTBASE_FOREACH (CameraBGImage *, bgpic, &cam->bg_images) {
          if (bgpic->ima == ima) {
            iuser = &bgpic->iuser;
            break;
          }
        }
      }
    }
  }

  /* initialize because of new image */
  if (iuser) {
    /* If the sequence was a tiled image, we only have one frame. */
    iuser->frames = (ima->source == IMA_SRC_SEQUENCE) ? frame_seq_len : 1;
    iuser->sfra = 1;
    iuser->framenr = 1;
    if (ima->source == IMA_SRC_MOVIE) {
      iuser->offset = 0;
    }
    else {
      iuser->offset = frame_ofs - 1;
    }
    iuser->scene = scene;
    BKE_image_init_imageuser(ima, iuser);
  }

  /* XXX BKE_packedfile_unpack_image frees image buffers */
  ED_preview_kill_jobs(CTX_wm_manager(C), bmain);

  BKE_image_signal(bmain, ima, iuser, IMA_SIGNAL_RELOAD);
  WM_event_add_notifier(C, NC_IMAGE | NA_EDITED, ima);

  MEM_freeN(op->customdata);

  return OPERATOR_FINISHED;
}

static int image_open_invoke(bContext *C, wmOperator *op, const wmEvent * /*event*/)
{
  SpaceImage *sima = CTX_wm_space_image(C); /* XXX other space types can call */
  const char *path = U.textudir;
  Image *ima = nullptr;
  Scene *scene = CTX_data_scene(C);

  if (sima) {
    ima = sima->image;
  }

  if (ima == nullptr) {
    Tex *tex = static_cast<Tex *>(CTX_data_pointer_get_type(C, "texture", &RNA_Texture).data);
    if (tex && tex->type == TEX_IMAGE) {
      ima = tex->ima;
    }
  }

  if (ima == nullptr) {
    PointerRNA ptr;
    PropertyRNA *prop;

    /* hook into UI */
    UI_context_active_but_prop_get_templateID(C, &ptr, &prop);

    if (prop) {
      PointerRNA oldptr;
      Image *oldima;

      oldptr = RNA_property_pointer_get(&ptr, prop);
      oldima = (Image *)oldptr.owner_id;
      /* unlikely to fail but better avoid strange crash */
      if (oldima && GS(oldima->id.name) == ID_IM) {
        ima = oldima;
      }
    }
  }

  if (ima) {
    path = ima->filepath;
  }

  if (RNA_struct_property_is_set(op->ptr, "filepath")) {
    return image_open_exec(C, op);
  }

  image_open_init(C, op);

  /* Show multi-view save options only if scene has multi-views. */
  PropertyRNA *prop;
  prop = RNA_struct_find_property(op->ptr, "show_multiview");
  RNA_property_boolean_set(op->ptr, prop, (scene->r.scemode & R_MULTIVIEW) != 0);

  image_filesel(C, op, path);

  return OPERATOR_RUNNING_MODAL;
}

static bool image_open_draw_check_prop(PointerRNA * /*ptr*/,
                                       PropertyRNA *prop,
                                       void * /*user_data*/)
{
  const char *prop_id = RNA_property_identifier(prop);

  return !STR_ELEM(prop_id, "filepath", "directory", "filename");
}

static void image_open_draw(bContext * /*C*/, wmOperator *op)
{
  uiLayout *layout = op->layout;
  ImageOpenData *iod = static_cast<ImageOpenData *>(op->customdata);
  ImageFormatData *imf = &iod->im_format;

  /* main draw call */
  uiDefAutoButsRNA(layout,
                   op->ptr,
                   image_open_draw_check_prop,
                   nullptr,
                   nullptr,
                   UI_BUT_LABEL_ALIGN_NONE,
                   false);

  /* image template */
  PointerRNA imf_ptr = RNA_pointer_create(nullptr, &RNA_ImageFormatSettings, imf);

  /* multiview template */
  if (RNA_boolean_get(op->ptr, "show_multiview")) {
    uiTemplateImageFormatViews(layout, &imf_ptr, op->ptr);
  }
}

static void image_operator_prop_allow_tokens(wmOperatorType *ot)
{
  PropertyRNA *prop = RNA_def_boolean(
      ot->srna, "allow_path_tokens", true, "", "Allow the path to contain substitution tokens");
  RNA_def_property_flag(prop, PROP_HIDDEN);
}

void IMAGE_OT_open(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Open Image";
  ot->description = "Open image";
  ot->idname = "IMAGE_OT_open";

  /* api callbacks */
  ot->exec = image_open_exec;
  ot->invoke = image_open_invoke;
  ot->cancel = image_open_cancel;
  ot->ui = image_open_draw;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  image_operator_prop_allow_tokens(ot);
  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_IMAGE | FILE_TYPE_MOVIE,
                                 FILE_SPECIAL,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_DIRECTORY | WM_FILESEL_FILES |
                                     WM_FILESEL_RELPATH,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);

  RNA_def_boolean(
      ot->srna,
      "use_sequence_detection",
      true,
      "Detect Sequences",
      "Automatically detect animated sequences in selected images (based on file names)");
  RNA_def_boolean(ot->srna,
                  "use_udim_detecting",
                  true,
                  "Detect UDIMs",
                  "Detect selected UDIM files and load all matching tiles");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Browse Image Operator
 * \{ */

static int image_file_browse_exec(bContext *C, wmOperator *op)
{
  Image *ima = static_cast<Image *>(op->customdata);
  if (ima == nullptr) {
    return OPERATOR_CANCELLED;
  }

  char filepath[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filepath);

  /* If loading into a tiled texture, ensure that the filename is tokenized. */
  if (ima->source == IMA_SRC_TILED) {
    BKE_image_ensure_tile_token(filepath, sizeof(filepath));
  }

  PropertyRNA *imaprop;
  PointerRNA imaptr = RNA_id_pointer_create(&ima->id);
  imaprop = RNA_struct_find_property(&imaptr, "filepath");

  RNA_property_string_set(&imaptr, imaprop, filepath);
  RNA_property_update(C, &imaptr, imaprop);

  return OPERATOR_FINISHED;
}

static int image_file_browse_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  Image *ima = image_from_context(C);
  if (!ima) {
    return OPERATOR_CANCELLED;
  }

  char filepath[FILE_MAX];
  STRNCPY(filepath, ima->filepath);

  /* Shift+Click to open the file, Alt+Click to browse a folder in the OS's browser. */
  if (event->modifier & (KM_SHIFT | KM_ALT)) {
    wmOperatorType *ot = WM_operatortype_find("WM_OT_path_open", true);
    PointerRNA props_ptr;

    if (event->modifier & KM_ALT) {
      char *lslash = (char *)BLI_path_slash_rfind(filepath);
      if (lslash) {
        *lslash = '\0';
      }
    }
    else if (ima->source == IMA_SRC_TILED) {
      ImageUser iuser = image_user_from_context_and_active_tile(C, ima);
      BKE_image_user_file_path(&iuser, ima, filepath);
    }

    WM_operator_properties_create_ptr(&props_ptr, ot);
    RNA_string_set(&props_ptr, "filepath", filepath);
    WM_operator_name_call_ptr(C, ot, WM_OP_EXEC_DEFAULT, &props_ptr, nullptr);
    WM_operator_properties_free(&props_ptr);

    return OPERATOR_CANCELLED;
  }

  /* The image is typically passed to the operator via layout/button context (e.g.
   * #uiLayoutSetContextPointer()). The File Browser doesn't support restoring this context
   * when calling `exec()` though, so we have to pass it the image via custom data. */
  op->customdata = ima;

  image_filesel(C, op, filepath);

  return OPERATOR_RUNNING_MODAL;
}

static bool image_file_browse_poll(bContext *C)
{
  return image_from_context(C) != nullptr;
}

void IMAGE_OT_file_browse(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Browse Image";
  ot->description =
      "Open an image file browser, hold Shift to open the file, Alt to browse containing "
      "directory";
  ot->idname = "IMAGE_OT_file_browse";

  /* api callbacks */
  ot->exec = image_file_browse_exec;
  ot->invoke = image_file_browse_invoke;
  ot->poll = image_file_browse_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_IMAGE | FILE_TYPE_MOVIE,
                                 FILE_SPECIAL,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_RELPATH,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Match Movie Length Operator
 * \{ */

static int image_match_len_exec(bContext *C, wmOperator * /*op*/)
{
  Scene *scene = CTX_data_scene(C);
  Image *ima = image_from_context(C);
  ImageUser *iuser = image_user_from_context(C);

  if (!ima || !iuser) {
    /* Try to get a Texture, or a SpaceImage from context... */
    Tex *tex = static_cast<Tex *>(CTX_data_pointer_get_type(C, "texture", &RNA_Texture).data);
    if (tex && tex->type == TEX_IMAGE) {
      ima = tex->ima;
      iuser = &tex->iuser;
    }
  }

  if (!ima || !iuser || !BKE_image_has_anim(ima)) {
    return OPERATOR_CANCELLED;
  }

  anim *anim = ((ImageAnim *)ima->anims.first)->anim;
  if (!anim) {
    return OPERATOR_CANCELLED;
  }
  iuser->frames = IMB_anim_get_duration(anim, IMB_TC_RECORD_RUN);
  BKE_image_user_frame_calc(ima, iuser, scene->r.cfra);

  return OPERATOR_FINISHED;
}

void IMAGE_OT_match_movie_length(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Match Movie Length";
  ot->description = "Set image's user's length to the one of this video";
  ot->idname = "IMAGE_OT_match_movie_length";

  /* api callbacks */
  ot->exec = image_match_len_exec;

  /* flags */
  /* Don't think we need undo for that. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_INTERNAL /* | OPTYPE_UNDO */;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Replace Image Operator
 * \{ */

static int image_replace_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  SpaceImage *sima = CTX_wm_space_image(C);
  char filepath[FILE_MAX];

  if (!sima->image) {
    return OPERATOR_CANCELLED;
  }

  RNA_string_get(op->ptr, "filepath", filepath);

  /* we can't do much if the filepath is longer than FILE_MAX :/ */
  STRNCPY(sima->image->filepath, filepath);

  if (sima->image->source == IMA_SRC_GENERATED) {
    sima->image->source = IMA_SRC_FILE;
    BKE_image_signal(bmain, sima->image, &sima->iuser, IMA_SIGNAL_SRC_CHANGE);
  }

  if (BLI_path_extension_check_array(filepath, imb_ext_movie)) {
    sima->image->source = IMA_SRC_MOVIE;
  }
  else {
    sima->image->source = IMA_SRC_FILE;
  }

  /* XXX BKE_packedfile_unpack_image frees image buffers */
  ED_preview_kill_jobs(CTX_wm_manager(C), CTX_data_main(C));

  BKE_icon_changed(BKE_icon_id_ensure(&sima->image->id));
  BKE_image_signal(bmain, sima->image, &sima->iuser, IMA_SIGNAL_RELOAD);
  WM_event_add_notifier(C, NC_IMAGE | NA_EDITED, sima->image);

  return OPERATOR_FINISHED;
}

static int image_replace_invoke(bContext *C, wmOperator *op, const wmEvent * /*event*/)
{
  SpaceImage *sima = CTX_wm_space_image(C);

  if (!sima->image) {
    return OPERATOR_CANCELLED;
  }

  if (RNA_struct_property_is_set(op->ptr, "filepath")) {
    return image_replace_exec(C, op);
  }

  if (!RNA_struct_property_is_set(op->ptr, "relative_path")) {
    RNA_boolean_set(op->ptr, "relative_path", BLI_path_is_rel(sima->image->filepath));
  }

  image_filesel(C, op, sima->image->filepath);

  return OPERATOR_RUNNING_MODAL;
}

void IMAGE_OT_replace(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Replace Image";
  ot->idname = "IMAGE_OT_replace";
  ot->description = "Replace current image by another one from disk";

  /* api callbacks */
  ot->exec = image_replace_exec;
  ot->invoke = image_replace_invoke;
  ot->poll = image_not_packed_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_IMAGE | FILE_TYPE_MOVIE,
                                 FILE_SPECIAL,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_RELPATH,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Save Image As Operator
 * \{ */

struct ImageSaveData {
  ImageUser *iuser;
  Image *image;
  ImageSaveOptions opts;
};
