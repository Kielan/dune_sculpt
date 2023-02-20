#include <stdio.h>

#include "MEM_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_ghash.h"
#include "lib_math.h"
#include "lib_stack.h"
#include "lib_utildefines.h"

#include "i18n_translation.h"

#include "types_brush_types.h"
#include "types_pen_types.h"
#include "types_image_types.h"
#include "types_material_types.h"
#include "types_meshdata_types.h"
#include "types_object_types.h"
#include "types_windowmanager_types.h"

#include "dune_brush.h"
#include "dune_context.h"
#include "dune_deform.h"
#include "dune_dpen.h"
#include "dune_dpen_geom.h"
#include "dune_image.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_material.h"
#include "dune_paint.h"
#include "dune_report.h"
#include "dune_screen.h"

#include "ed_dpen.h"
#include "ed_keyframing.h"
#include "ed_screen.h"
#include "ed_space_api.h"
#include "ed_view3d.h"

#include "api_access.h"
#include "api_define.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "gpu_framebuffer.h"
#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gpu_state.h"

#include "ui_interface.h"

#include "wm_api.h"
#include "wm_types.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "dpen_intern.h"

#define LEAK_HORZ 0
#define LEAK_VERT 1
#define MIN_WINDOW_SIZE 128

/* Set to 1 to debug filling internal image. By default, the value must be 0. */
#define FILL_DEBUG 0

/* Duplicated: etempFlags */
enum {
  DPEN_DRAWFILLS_NOSTATUS = (1 << 0), /* don't draw status info */
  DPEN_DRAWFILLS_ONLY3D = (1 << 1),   /* only draw 3d-strokes */
};

/* Temporary fill operation data `op->customdata`. */
typedef struct DPenFill {
  dContext *C;
  struct Main *dmain;
  struct Depsgraph *depsgraph;
  /** window where painting originated */
  struct wmWindow *win;
  /** current scene from context */
  struct Scene *scene;
  /** current active gp object */
  struct Object *ob;
  /** area where painting originated */
  struct ScrArea *area;
  /** region where painting originated */
  struct RegionView3D *rv3d;
  /** view3 where painting originated */
  struct View3D *v3d;
  /** region where painting originated */
  struct ARegion *region;
  /** Current DPen data-block. */
  struct DPenData *dpd;
  /** current material */
  struct Material *mat;
  /** current brush */
  struct Brush *brush;
  /** layer */
  struct DPenLayer *dpl;
  /** frame */
  struct DPenFrame *dpf;
  /** Temp mouse position stroke. */
  struct DPenStroke *dps_mouse;
  /** Pointer to report messages. */
  struct ReportList *reports;
  /** For operations that require occlusion testing. */
  struct ViewDepths *depths;
  /** flags */
  short flag;
  /** avoid too fast events */
  short oldkey;
  /** send to back stroke */
  bool on_back;
  /** Flag for render mode */
  bool is_render;
  /** Flag to check something was done. */
  bool done;
  /** mouse fill center position */
  int mouse[2];
  /** windows width */
  int sizex;
  /** window height */
  int sizey;
  /** lock to viewport axis */
  int lock_axis;

  /** number of pixel to consider the leak is too small (x 2) */
  short fill_leak;
  /** factor for transparency */
  float fill_threshold;
  /** number of simplify steps */
  int fill_simplylvl;
  /** boundary limits drawing mode */
  int fill_draw_mode;
  /* scaling factor */
  float fill_factor;

  /* Frame to use. */
  int active_cfra;

  /** number of elements currently in cache */
  short sbuffer_used;
  /** temporary points */
  void *sbuffer;
  /** depth array for reproject */
  float *depth_arr;

  /** temp image */
  Image *ima;
  /** temp points data */
  DLibStack *stack;
  /** handle for drawing strokes while operator is running 3d stuff */
  void *draw_handle_3d;

  /* Temporary size x. */
  int bwinx;
  /* Temporary size y. */
  int bwiny;
  rcti brect;

  /* Space Conversion Data */
  DPenSpaceConversion dpsc;

  /** Zoom factor. */
  float zoom;

  /** Factor of extension. */
  float fill_extend_fac;

} DPenFill;

bool skip_layer_check(short fill_layer_mode, int dpl_active_index, int dpl_index);
static void dpen_draw_boundary_lines(const struct dContext *UNUSED(C), struct DPenFill *tdpf);

/* Delete any temporary stroke. */
static void dpen_delete_temp_stroke_extension(DPenFill *tgpf, const bool all_frames)
{
  LISTBASE_FOREACH (DPenLayer *, dpl, &tdpf->dpd->layers) {
    if (dpl->flag & GP_LAYER_HIDE) {
      continue;
    }

    DPenFrame *init_dpf = (all_frames) ? dpl->frames.first :
                                         dune_dpen_layer_frame_get(
                                             dpl, tdpf->active_cfra, DPEN_GETFRAME_USE_PREV);
    if (init_dpf == NULL) {
      continue;
    }
    for (DPenFrame *dpf = init_dpf; dpf; dpf = dpf->next) {
      LISTBASE_FOREACH_MUTABLE (DPenStroke *, dps, &dpf->strokes) {
        /* free stroke */
        if ((dps->flag & DPEN_STROKE_NOFILL) && (dps->flag & DPEN_STROKE_TAG)) {
          lib_remlink(&dpf->strokes, dps);
          dune_dpen_free_stroke(dps);
        }
      }
      if (!all_frames) {
        break;
      }
    }
  }
}

static void extrapolate_points_by_length(DPenPoint *a,
                                         DPenPoint *b,
                                         float length,
                                         float r_point[3])
{
  float ab[3];
  sub_v3_v3v3(ab, &b->x, &a->x);
  normalize_v3(ab);
  mul_v3_fl(ab, length);
  add_v3_v3v3(r_point, &b->x, ab);
}

/* Loop all layers create stroke extensions. */
static void dpen_create_extensions(DPenFill *tdpf)
{
  Object *ob = tdpf->ob;
  DPenData *gpd = tdpf->dpd;
  Brush *brush = tdpf->brush;
  BrushDPenSettings *brush_settings = brush->dpen_settings;

  DPenLayer *dpl_active = dune_dpen_layer_active_get(dpd);
  lib_assert(dpl_active != NULL);

  const int dpl_active_index = lib_findindex(&dpd->layers, gpl_active);
  lib_assert(dpl_active_index >= 0);

  LISTBASE_FOREACH (DPenLayer *, dpl, &dpd->layers) {
    if (dpl->flag & DPEN_LAYER_HIDE) {
      continue;
    }

    /* Decide if the strokes of layers are included or not depending on the layer mode. */
    const int gpl_index = BLI_findindex(&gpd->layers, gpl);
    bool skip = skip_layer_check(brush_settings->fill_layer_mode, gpl_active_index, gpl_index);
    if (skip) {
      continue;
    }

    bGPDframe *gpf = BKE_gpencil_layer_frame_get(gpl, tgpf->active_cfra, GP_GETFRAME_USE_PREV);
    if (gpf == NULL) {
      continue;
    }

    LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
      /* Check if stroke can be drawn. */
      if ((gps->points == NULL) || (gps->totpoints < 2)) {
        continue;
      }
      if (gps->flag & (GP_STROKE_NOFILL | GP_STROKE_TAG)) {
        continue;
      }
      /* Check if the color is visible. */
      MaterialGPencilStyle *gp_style = BKE_gpencil_material_settings(ob, gps->mat_nr + 1);
      if ((gp_style == NULL) || (gp_style->flag & GP_MATERIAL_HIDE)) {
        continue;
      }

      /* Extend start. */
      bGPDspoint *pt0 = &gps->points[1];
      bGPDspoint *pt1 = &gps->points[0];
      bGPDstroke *gps_new = BKE_gpencil_stroke_new(gps->mat_nr, 2, gps->thickness);
      gps_new->flag |= GP_STROKE_NOFILL | GP_STROKE_TAG;
      BLI_addtail(&gpf->strokes, gps_new);

      bGPDspoint *pt = &gps_new->points[0];
      copy_v3_v3(&pt->x, &pt1->x);
      pt->strength = 1.0f;
      pt->pressure = 1.0f;

      pt = &gps_new->points[1];
      pt->strength = 1.0f;
      pt->pressure = 1.0f;
      extrapolate_points_by_length(pt0, pt1, tgpf->fill_extend_fac * 0.1f, &pt->x);

      /* Extend end. */
      pt0 = &gps->points[gps->totpoints - 2];
      pt1 = &gps->points[gps->totpoints - 1];
      gps_new = BKE_gpencil_stroke_new(gps->mat_nr, 2, gps->thickness);
      gps_new->flag |= GP_STROKE_NOFILL | GP_STROKE_TAG;
      BLI_addtail(&gpf->strokes, gps_new);

      pt = &gps_new->points[0];
      copy_v3_v3(&pt->x, &pt1->x);
      pt->strength = 1.0f;
      pt->pressure = 1.0f;

      pt = &gps_new->points[1];
      pt->strength = 1.0f;
      pt->pressure = 1.0f;
      extrapolate_points_by_length(pt0, pt1, tgpf->fill_extend_fac * 0.1f, &pt->x);
    }
  }
}

static void gpencil_update_extend(tGPDfill *tgpf)
{
  gpencil_delete_temp_stroke_extension(tgpf, false);

  if (tgpf->fill_extend_fac > 0.0f) {
    gpencil_create_extensions(tgpf);
  }
  WM_event_add_notifier(tgpf->C, NC_GPENCIL | NA_EDITED, NULL);
}

static bool gpencil_stroke_is_drawable(tGPDfill *tgpf, bGPDstroke *gps)
{
  if (tgpf->is_render) {
    return true;
  }

  const bool show_help = (tgpf->flag & GP_BRUSH_FILL_SHOW_HELPLINES) != 0;
  const bool show_extend = (tgpf->flag & GP_BRUSH_FILL_SHOW_EXTENDLINES) != 0;
  const bool is_extend = (gps->flag & GP_STROKE_NOFILL) && (gps->flag & GP_STROKE_TAG);

  if ((!show_help) && (show_extend)) {
    if (!is_extend) {
      return false;
    }
  }

  if ((show_help) && (!show_extend)) {
    if (is_extend) {
      return false;
    }
  }

  return true;
}

/* draw a given stroke using same thickness and color for all points */
static void gpencil_draw_basic_stroke(tGPDfill *tgpf,
                                      bGPDstroke *gps,
                                      const float diff_mat[4][4],
                                      const bool cyclic,
                                      const float ink[4],
                                      const int flag,
                                      const float thershold,
                                      const float thickness)
{
  bGPDspoint *points = gps->points;

  Material *ma = tgpf->mat;
  MaterialGPencilStyle *gp_style = ma->gp_style;

  int totpoints = gps->totpoints;
  float fpt[3];
  float col[4];
  const float extend_col[4] = {0.0f, 1.0f, 1.0f, 1.0f};
  const bool is_extend = (gps->flag & GP_STROKE_NOFILL) && (gps->flag & GP_STROKE_TAG);

  if (!gpencil_stroke_is_drawable(tgpf, gps)) {
    return;
  }

  if ((is_extend) && (!tgpf->is_render)) {
    copy_v4_v4(col, extend_col);
  }
  else {
    copy_v4_v4(col, ink);
  }
  /* if cyclic needs more vertex */
  int cyclic_add = (cyclic) ? 1 : 0;

  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  uint color = GPU_vertformat_attr_add(format, "color", GPU_COMP_F32, 4, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_FLAT_COLOR);

  /* draw stroke curve */
  GPU_line_width((!is_extend) ? thickness : thickness * 2.0f);
  immBeginAtMost(GPU_PRIM_LINE_STRIP, totpoints + cyclic_add);
  const bGPDspoint *pt = points;

  for (int i = 0; i < totpoints; i++, pt++) {

    if (flag & GP_BRUSH_FILL_HIDE) {
      float alpha = gp_style->stroke_rgba[3] * pt->strength;
      CLAMP(alpha, 0.0f, 1.0f);
      col[3] = alpha <= thershold ? 0.0f : 1.0f;
    }
    else {
      col[3] = 1.0f;
    }
    /* set point */
    immAttr4fv(color, col);
    mul_v3_m4v3(fpt, diff_mat, &pt->x);
    immVertex3fv(pos, fpt);
  }

  if (cyclic && totpoints > 2) {
    /* draw line to first point to complete the cycle */
    immAttr4fv(color, col);
    mul_v3_m4v3(fpt, diff_mat, &points->x);
    immVertex3fv(pos, fpt);
  }

  immEnd();
  immUnbindProgram();
}

static void draw_mouse_position(tGPDfill *tgpf)
{
  if (tgpf->gps_mouse == NULL) {
    return;
  }
  uchar mouse_color[4] = {0, 0, 255, 255};

  bGPDspoint *pt = &tgpf->gps_mouse->points[0];
  float point_size = (tgpf->zoom == 1.0f) ? 4.0f * tgpf->fill_factor :
                                            (0.5f * tgpf->zoom) + tgpf->fill_factor;
  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  uint col = GPU_vertformat_attr_add(format, "color", GPU_COMP_U8, 4, GPU_FETCH_INT_TO_FLOAT_UNIT);

  /* Draw mouse click position in Blue. */
  immBindBuiltinProgram(GPU_SHADER_3D_POINT_FIXED_SIZE_VARYING_COLOR);
  GPU_point_size(point_size);
  immBegin(GPU_PRIM_POINTS, 1);
  immAttr4ubv(col, mouse_color);
  immVertex3fv(pos, &pt->x);
  immEnd();
  immUnbindProgram();
}

/* Helper: Check if must skip the layer */
bool skip_layer_check(short fill_layer_mode, int gpl_active_index, int gpl_index)
{
  bool skip = false;

  switch (fill_layer_mode) {
    case GP_FILL_GPLMODE_ACTIVE: {
      if (gpl_index != gpl_active_index) {
        skip = true;
      }
      break;
    }
    case GP_FILL_GPLMODE_ABOVE: {
      if (gpl_index != gpl_active_index + 1) {
        skip = true;
      }
      break;
    }
    case GP_FILL_GPLMODE_BELOW: {
      if (gpl_index != gpl_active_index - 1) {
        skip = true;
      }
      break;
    }
    case GP_FILL_GPLMODE_ALL_ABOVE: {
      if (gpl_index <= gpl_active_index) {
        skip = true;
      }
      break;
    }
    case GP_FILL_GPLMODE_ALL_BELOW: {
      if (gpl_index >= gpl_active_index) {
        skip = true;
      }
      break;
    }
    case GP_FILL_GPLMODE_VISIBLE:
    default:
      break;
  }

  return skip;
}

/* Loop all layers to draw strokes. */
static void gpencil_draw_datablock(tGPDfill *tgpf, const float ink[4])
{
  Object *ob = tgpf->ob;
  bGPdata *gpd = tgpf->gpd;
  Brush *brush = tgpf->brush;
  BrushGpencilSettings *brush_settings = brush->gpencil_settings;
  ToolSettings *ts = tgpf->scene->toolsettings;

  tGPDdraw tgpw;
  tgpw.rv3d = tgpf->rv3d;
  tgpw.depsgraph = tgpf->depsgraph;
  tgpw.ob = ob;
  tgpw.gpd = gpd;
  tgpw.offsx = 0;
  tgpw.offsy = 0;
  tgpw.winx = tgpf->sizex;
  tgpw.winy = tgpf->sizey;
  tgpw.dflag = 0;
  tgpw.disable_fill = 1;
  tgpw.dflag |= (GP_DRAWFILLS_ONLY3D | GP_DRAWFILLS_NOSTATUS);

  GPU_blend(GPU_BLEND_ALPHA);

  bGPDlayer *gpl_active = BKE_gpencil_layer_active_get(gpd);
  BLI_assert(gpl_active != NULL);

  const int gpl_active_index = BLI_findindex(&gpd->layers, gpl_active);
  BLI_assert(gpl_active_index >= 0);

  /* Draw blue point where click with mouse. */
  draw_mouse_position(tgpf);

  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    /* do not draw layer if hidden */
    if (gpl->flag & GP_LAYER_HIDE) {
      continue;
    }

    /* calculate parent position */
    BKE_gpencil_layer_transform_matrix_get(tgpw.depsgraph, ob, gpl, tgpw.diff_mat);

    /* Decide if the strokes of layers are included or not depending on the layer mode.
     * Cannot skip the layer because it can use boundary strokes and must be used. */
    const int gpl_index = BLI_findindex(&gpd->layers, gpl);
    bool skip = skip_layer_check(brush_settings->fill_layer_mode, gpl_active_index, gpl_index);

    /* if active layer and no keyframe, create a new one */
    if (gpl == tgpf->gpl) {
      if ((gpl->actframe == NULL) || (gpl->actframe->framenum != tgpf->active_cfra)) {
        short add_frame_mode;
        if (IS_AUTOKEY_ON(tgpf->scene)) {
          if (ts->gpencil_flags & GP_TOOL_FLAG_RETAIN_LAST) {
            add_frame_mode = GP_GETFRAME_ADD_COPY;
          }
          else {
            add_frame_mode = GP_GETFRAME_ADD_NEW;
          }
        }
        else {
          add_frame_mode = GP_GETFRAME_USE_PREV;
        }

        BKE_gpencil_layer_frame_get(gpl, tgpf->active_cfra, add_frame_mode);
      }
    }

    /* get frame to draw */
    bGPDframe *gpf = BKE_gpencil_layer_frame_get(gpl, tgpf->active_cfra, GP_GETFRAME_USE_PREV);
    if (gpf == NULL) {
      continue;
    }

    LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
      /* check if stroke can be drawn */
      if ((gps->points == NULL) || (gps->totpoints < 2)) {
        continue;
      }
      /* check if the color is visible */
      MaterialGPencilStyle *gp_style = BKE_gpencil_material_settings(ob, gps->mat_nr + 1);
      if ((gp_style == NULL) || (gp_style->flag & GP_MATERIAL_HIDE)) {
        continue;
      }

      /* If the layer must be skipped, but the stroke is not boundary, skip stroke. */
      if ((skip) && ((gps->flag & GP_STROKE_NOFILL) == 0)) {
        continue;
      }

      tgpw.gps = gps;
      tgpw.gpl = gpl;
      tgpw.gpf = gpf;
      tgpw.t_gpf = gpf;

      tgpw.is_fill_stroke = (tgpf->fill_draw_mode == GP_FILL_DMODE_CONTROL) ? false : true;
      /* Reduce thickness to avoid gaps. */
      tgpw.lthick = gpl->line_change;
      tgpw.opacity = 1.0;
      copy_v4_v4(tgpw.tintcolor, ink);
      tgpw.onion = true;
      tgpw.custonion = true;

      /* Normal strokes. */
      if (ELEM(tgpf->fill_draw_mode, GP_FILL_DMODE_STROKE, GP_FILL_DMODE_BOTH)) {
        if (gpencil_stroke_is_drawable(tgpf, gps) && ((gps->flag & GP_STROKE_TAG) == 0)) {
          ED_gpencil_draw_fill(&tgpw);
        }
      }

      /* 3D Lines with basic shapes and invisible lines */
      if (ELEM(tgpf->fill_draw_mode, GP_FILL_DMODE_CONTROL, GP_FILL_DMODE_BOTH)) {
        gpencil_draw_basic_stroke(tgpf,
                                  gps,
                                  tgpw.diff_mat,
                                  gps->flag & GP_STROKE_CYCLIC,
                                  ink,
                                  tgpf->flag,
                                  tgpf->fill_threshold,
                                  1.0f);
      }
    }
  }

  GPU_blend(GPU_BLEND_NONE);
}

/* Draw strokes in off-screen buffer. */
static bool gpencil_render_offscreen(tGPDfill *tgpf)
{
  bool is_ortho = false;
  float winmat[4][4];

  if (!tgpf->gpd) {
    return false;
  }

  /* set temporary new size */
  tgpf->bwinx = tgpf->region->winx;
  tgpf->bwiny = tgpf->region->winy;
  tgpf->brect = tgpf->region->winrct;

  /* resize region */
  tgpf->region->winrct.xmin = 0;
  tgpf->region->winrct.ymin = 0;
  tgpf->region->winrct.xmax = max_ii((int)tgpf->region->winx * tgpf->fill_factor, MIN_WINDOW_SIZE);
  tgpf->region->winrct.ymax = max_ii((int)tgpf->region->winy * tgpf->fill_factor, MIN_WINDOW_SIZE);
  tgpf->region->winx = (short)abs(tgpf->region->winrct.xmax - tgpf->region->winrct.xmin);
  tgpf->region->winy = (short)abs(tgpf->region->winrct.ymax - tgpf->region->winrct.ymin);

  /* save new size */
  tgpf->sizex = (int)tgpf->region->winx;
  tgpf->sizey = (int)tgpf->region->winy;

  char err_out[256] = "unknown";
  GPUOffScreen *offscreen = GPU_offscreen_create(
      tgpf->sizex, tgpf->sizey, true, GPU_RGBA8, err_out);
  if (offscreen == NULL) {
    printf("GPencil - Fill - Unable to create fill buffer\n");
    return false;
  }

  GPU_offscreen_bind(offscreen, true);
  uint flag = IB_rectfloat;
  ImBuf *ibuf = IMB_allocImBuf(tgpf->sizex, tgpf->sizey, 32, flag);

  rctf viewplane;
  float clip_start, clip_end;

  is_ortho = ED_view3d_viewplane_get(tgpf->depsgraph,
                                     tgpf->v3d,
                                     tgpf->rv3d,
                                     tgpf->sizex,
                                     tgpf->sizey,
                                     &viewplane,
                                     &clip_start,
                                     &clip_end,
                                     NULL);

  /* Rescale `viewplane` to fit all strokes. */
  float width = viewplane.xmax - viewplane.xmin;
  float height = viewplane.ymax - viewplane.ymin;

  float width_new = width * tgpf->zoom;
  float height_new = height * tgpf->zoom;
  float scale_x = (width_new - width) / 2.0f;
  float scale_y = (height_new - height) / 2.0f;

  viewplane.xmin -= scale_x;
  viewplane.xmax += scale_x;
  viewplane.ymin -= scale_y;
  viewplane.ymax += scale_y;

  if (is_ortho) {
    orthographic_m4(winmat,
                    viewplane.xmin,
                    viewplane.xmax,
                    viewplane.ymin,
                    viewplane.ymax,
                    -clip_end,
                    clip_end);
  }
  else {
    perspective_m4(winmat,
                   viewplane.xmin,
                   viewplane.xmax,
                   viewplane.ymin,
                   viewplane.ymax,
                   clip_start,
                   clip_end);
  }

  GPU_matrix_push_projection();
  GPU_matrix_identity_projection_set();
  GPU_matrix_push();
  GPU_matrix_identity_set();

  GPU_depth_mask(true);
  GPU_clear_color(0.0f, 0.0f, 0.0f, 0.0f);
  GPU_clear_depth(1.0f);

  ed_view3d_update_viewmat(
      tdpf->depsgraph, tdpf->scene, tdpf->v3d, tdpf->region, NULL, winmat, NULL, true);
  /* set for opengl */
  gpu_matrix_projection_set(tdpf->rv3d->winmat);
  gpu_matrix_set(tdpf->rv3d->viewmat);

  /* draw strokes */
  const float ink[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  dpen_draw_datablock(tdpf, ink);

  gpu_depth_mask(false);

  gpu_matrix_pop_projection();
  gpu_matrix_pop();

  /* create a image to see result of template */
  if (ibuf->rect_float) {
    gou_offscreen_read_pixels(offscreen, GPU_DATA_FLOAT, ibuf->rect_float);
  }
  else if (ibuf->rect) {
    gpu_offscreen_read_pixels(offscreen, GPU_DATA_UBYTE, ibuf->rect);
  }
  if (ibuf->rect_float && ibuf->rect) {
    IMB_rect_from_float(ibuf);
  }

  tdpf->ima = dune_image_add_from_imbuf(tgpf->dmain, ibuf, "DPen_fill");
  tdpf->ima->id.tag |= LIB_TAG_DOIT;

  dune_image_release_ibuf(tgpf->ima, ibuf, NULL);

  /* Switch back to window-system-provided frame-buffer. */
  gpu_offscreen_unbind(offscreen, true);
  gpu_offscreen_free(offscreen);

  return true;
}
