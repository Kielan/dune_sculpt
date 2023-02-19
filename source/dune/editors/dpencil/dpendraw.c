#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "lib_sys_types.h"

#include "lib_math.h"
#include "lib_polyfill_2d.h"
#include "lib_utildefines.h"

#include "BLF_api.h"
#include "i18n_translation.h"

#include "types_brush_types.h"
#include "types_gpencil_types.h"
#include "types_material_types.h"
#include "types_object_types.h"
#include "types_scene_types.h"
#include "types_screen_types.h"
#include "types_space_types.h"
#include "types_userdef_types.h"
#include "types_view3d_types.h"

#include "dune_brush.h"
#include "dune_context.h"
#include "dune_global.h"
#include "dune_dpen.h"
#include "dune_image.h"
#include "dune_material.h"
#include "dune_paint.h"

#include "DEG_depsgraph.h"

#include "wm_api.h"

#include "gpu_batch.h"
#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gpu_shader_shared.h"
#include "gpu_state.h"
#include "gpu_uniform_buffer.h"

#include "ed_dpen.h"
#include "ed_screen.h"
#include "ed_space_api.h"
#include "ed_view3d.h"

#include "UI_interface_icons.h"
#include "UI_resources.h"

#include "IMB_imbuf_types.h"

#include "dpen_intern.h"

/* ************************************************** */
/* DUNE PEN DRAWING */

/* ----- General Defines ------ */
/* flags for sflag */
typedef enum eDrawStrokeFlags {
  /** don't draw status info */
  DPEN_DRAWDATA_NOSTATUS = (1 << 0),
  /** only draw 3d-strokes */
  DPEN_DRAWDATA_ONLY3D = (1 << 1),
  /** only draw 'canvas' strokes */
  DPEN_DRAWDATA_ONLYV2D = (1 << 2),
  /** only draw 'image' strokes */
  DPEN_DRAWDATA_ONLYI2D = (1 << 3),
  /** special hack for drawing strokes in Image Editor (weird coordinates) */
  DPEN_DRAWDATA_IEDITHACK = (1 << 4),
  /** don't draw xray in 3D view (which is default) */
  DPEN_DRAWDATA_NO_XRAY = (1 << 5),
  /** no onionskins should be drawn (for animation playback) */
  DPEN_DRAWDATA_NO_ONIONS = (1 << 6),
  /** draw strokes as "volumetric" circular billboards */
  DPEN_DRAWDATA_VOLUMETRIC = (1 << 7),
  /** fill insides/bounded-regions of strokes */
  DPEN_DRAWDATA_FILL = (1 << 8),
} eDrawStrokeFlags;

/* thickness above which we should use special drawing */
#if 0
#  define DPEN_DRAWTHICKNESS_SPECIAL 3
#endif

/* conversion utility (float --> normalized unsigned byte) */
#define F2UB(x) (uchar)(255.0f * x)

/* ----- Tool Buffer Drawing ------ */
/* helper functions to set color of buffer point */

static void dpen_set_point_varying_color(const DPenPoint *pt,
                                            const float ink[4],
                                            uint attr_id,
                                            bool fix_strength)
{
  float alpha = ink[3] * pt->strength;
  if ((fix_strength) && (alpha >= 0.1f)) {
    alpha = 1.0f;
  }
  CLAMP(alpha, DPEN_STRENGTH_MIN, 1.0f);
  immAttr4ub(attr_id, F2UB(ink[0]), F2UB(ink[1]), F2UB(ink[2]), F2UB(alpha));
}

/* ----------- Volumetric Strokes --------------- */

/* draw a 3D stroke in "volumetric" style */
static void dpen_draw_stroke_volumetric_3d(const DPenPoint *points,
                                              int totpoints,
                                              short thickness,
                                              const float ink[4])
{
  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  uint size = GPU_vertformat_attr_add(format, "size", GPU_COMP_F32, 1, GPU_FETCH_FLOAT);
  uint color = GPU_vertformat_attr_add(
      format, "color", GPU_COMP_U8, 4, GPU_FETCH_INT_TO_FLOAT_UNIT);

  immBindBuiltinProgram(GPU_SHADER_3D_POINT_VARYING_SIZE_VARYING_COLOR);
  GPU_program_point_size(true);
  immBegin(GPU_PRIM_POINTS, totpoints);

  const DPenPoint *pt = points;
  for (int i = 0; i < totpoints && pt; i++, pt++) {
    dpen_set_point_varying_color(pt, ink, color, false);
    /* TODO: scale based on view transform */
    immAttr1f(size, pt->pressure * thickness);
    /* we can adjust size in vertex shader based on view/projection! */
    immVertex3fv(pos, &pt->x);
  }

  immEnd();
  immUnbindProgram();
  gpu_program_point_size(false);
}

/* ----- Existing Strokes Drawing (3D and Point) ------ */

/* draw a given stroke in 3d (i.e. in 3d-space) */
static void dpen_draw_stroke_3d(DPenDraw *tdpw,
                                   short thickness,
                                   const float ink[4],
                                   bool cyclic)
{
  DPenPoint *points = tdpw->dps->points;
  int totpoints = tdpw->dps->totpoints;

  const float viewport[2] = {(float)tgpw->winx, (float)tgpw->winy};
  const float min_thickness = 0.05f;

  float fpt[3];

  /* if cyclic needs more vertex */
  int cyclic_add = (cyclic) ? 1 : 0;

  GPUVertFormat *format = immVertexFormat();
  const struct {
    uint pos, color, thickness;
  } attr_id = {
      .pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT),
      .color = gpu_vertformat_attr_add(
          format, "color", GPU_COMP_U8, 4, GPU_FETCH_INT_TO_FLOAT_UNIT),
      .thickness = gpu_vertformat_attr_add(format, "thickness", GPU_COMP_F32, 1, GPU_FETCH_FLOAT),
  };

  immBindBuiltinProgram(GPU_SHADER_DPEN_STROKE);

  float obj_scale = tdpw->ob ?
                        (tdpw->ob->scale[0] + tdpw->ob->scale[1] + tdpw->ob->scale[2]) / 3.0f :
                        1.0f;

  struct DPenStrokeData dpen_stroke_data;
  copy_v2_v2(gpencil_stroke_data.viewport, viewport);
  dpen_stroke_data.pixsize = tdpw->rv3d->pixsize;
  dpen_stroke_data.objscale = obj_scale;
  int keep_size = (int)((tdpw->dpd) && (tdpw->dpd->flag & DPEN_DATA_STROKE_KEEPTHICKNESS));
  dpen_stroke_data.keep_size = keep_size;
  dpen_stroke_data.pixfactor = tdpw->dpd->pixfactor;
  /* xray mode always to 3D space to avoid wrong zdepth calculation (T60051) */
  dpen_stroke_data.xraymode = DPEN_XRAY_3DSPACE;
  dpen_stroke_data.caps_start = tdpw->dps->caps[0];
  dpen_stroke_data.caps_end = tdpw->dps->caps[1];
  dpen_stroke_data.fill_stroke = tdpw->is_fill_stroke;

  GPUUniformBuf *ubo = gpu_uniformbuf_create_ex(
      sizeof(struct DPenStrokeData), &dpen_stroke_data, __func__);
  immBindUniformBuf("rpen_stroke_data", ubo);

  /* draw stroke curve */
  immBeginAtMost(GPU_PRIM_LINE_STRIP_ADJ, totpoints + cyclic_add + 2);
  const DPenPoint *pt = points;

  for (int i = 0; i < totpoints; i++, pt++) {
    /* first point for adjacency (not drawn) */
    if (i == 0) {
      dpen_set_point_varying_color(points, ink, attr_id.color, (bool)tgdpw->is_fill_stroke);

      if ((cyclic) && (totpoints > 2)) {
        immAttr1f(attr_id.thickness,
                  max_ff((points + totpoints - 1)->pressure * thickness, min_thickness));
        mul_v3_m4v3(fpt, tdpw->diff_mat, &(points + totpoints - 1)->x);
      }
      else {
        immAttr1f(attr_id.thickness, max_ff((points + 1)->pressure * thickness, min_thickness));
        mul_v3_m4v3(fpt, tdpw->diff_mat, &(points + 1)->x);
      }
      immVertex3fv(attr_id.pos, fpt);
    }
    /* set point */
    dpen_set_point_varying_color(pt, ink, attr_id.color, (bool)tdpw->is_fill_stroke);
    immAttr1f(attr_id.thickness, max_ff(pt->pressure * thickness, min_thickness));
    mul_v3_m4v3(fpt, tdpw->diff_mat, &pt->x);
    immVertex3fv(attr_id.pos, fpt);
  }

  if (cyclic && totpoints > 2) {
    /* draw line to first point to complete the cycle */
    immAttr1f(attr_id.thickness, max_ff(points->pressure * thickness, 1.0f));
    mul_v3_m4v3(fpt, tdpw->diff_mat, &points->x);
    immVertex3fv(attr_id.pos, fpt);

    /* now add adjacency point (not drawn) */
    immAttr1f(attr_id.thickness, max_ff((points + 1)->pressure * thickness, 1.0f));
    mul_v3_m4v3(fpt, tdpw->diff_mat, &(points + 1)->x);
    immVertex3fv(attr_id.pos, fpt);
  }
  /* last adjacency point (not drawn) */
  else {
    dpen_set_point_varying_color(
        points + totpoints - 2, ink, attr_id.color, (bool)tdpw->is_fill_stroke);

    immAttr1f(attr_id.thickness, max_ff((points + totpoints - 2)->pressure * thickness, 1.0f));
    mul_v3_m4v3(fpt, tdpw->diff_mat, &(points + totpoints - 2)->x);
    immVertex3fv(attr_id.pos, fpt);
  }

  immEnd();
  immUnbindProgram();

  gpu_uniformbuf_free(ubo);
}

/* ----- Strokes Drawing ------ */

/* Helper for doing all the checks on whether a stroke can be drawn */
static bool dpen_can_draw_stroke(const DPenStroke *dps, const int dflag)
{
  /* skip stroke if it isn't in the right display space for this drawing context */
  /* 1) 3D Strokes */
  if ((dflag & DPEN_DRAWDATA_ONLY3D) && !(dps->flag & DPEN_STROKE_3DSPACE)) {
    return false;
  }
  if (!(dflag & DPEN_DRAWDATA_ONLY3D) && (dps->flag & DPEN_STROKE_3DSPACE)) {
    return false;
  }

  /* 2) Screen Space 2D Strokes */
  if ((dflag & DPEN_DRAWDATA_ONLYV2D) && !(dps->flag & DPEN_STROKE_2DSPACE)) {
    return false;
  }
  if (!(dflag & DPEN_DRAWDATA_ONLYV2D) && (dps->flag & DPEN_STROKE_2DSPACE)) {
    return false;
  }

  /* 3) Image Space (2D) */
  if ((dflag & DPEN_DRAWDATA_ONLYI2D) && !(gps->flag & GP_STROKE_2DIMAGE)) {
    return false;
  }
  if (!(dflag & GP_DRAWDATA_ONLYI2D) && (gps->flag & GP_STROKE_2DIMAGE)) {
    return false;
  }

  /* skip stroke if it doesn't have any valid data */
  if ((gps->points == NULL) || (gps->totpoints < 1)) {
    return false;
  }

  /* stroke can be drawn */
  return true;
}

/* draw a set of strokes */
static void dpen_draw_strokes(DPenDraw *tdpw)
{
  float tcolor[4];
  short sthickness;
  float ink[4];
  const bool is_unique = (tdpw->dps != NULL);
  const bool use_mat = (tdpw->dpd->mat != NULL);

  gpu_program_point_size(true);

  /* Do not write to depth (avoid self-occlusion). */
  bool prev_depth_mask = gpu_depth_mask_get();
  gpu_depth_mask(false);

  DPenStroke *dps_init = (tdpw->dps) ? tdpw->dps : tdpw->t_dpf->strokes.first;

  for (DPenDstroke *dps = dps_init; dps; dps = dps->next) {
    /* check if stroke can be drawn */
    if (dpen_can_draw_stroke(dps, tdpw->dflag) == false) {
      continue;
    }
    /* check if the color is visible */
    Material *ma = (use_mat) ? tgpw->gpd->mat[gps->mat_nr] : BKE_material_default_gpencil();
    MaterialGPencilStyle *gp_style = (ma) ? ma->gp_style : NULL;

    if ((gp_style == NULL) || (gp_style->flag & GP_MATERIAL_HIDE) ||
        /* If onion and ghost flag do not draw. */
        (tgpw->onion && (gp_style->flag & GP_MATERIAL_HIDE_ONIONSKIN))) {
      continue;
    }

    /* if disable fill, the colors with fill must be omitted too except fill boundary strokes */
    if ((tgpw->disable_fill == 1) && (gp_style->fill_rgba[3] > 0.0f) &&
        ((gps->flag & GP_STROKE_NOFILL) == 0) && (gp_style->flag & GP_MATERIAL_FILL_SHOW)) {
      continue;
    }

    /* calculate thickness */
    sthickness = gps->thickness + tgpw->lthick;

    if (tgpw->is_fill_stroke) {
      sthickness = (short)max_ii(1, sthickness / 2);
    }

    if (sthickness <= 0) {
      continue;
    }

    /* check which stroke-drawer to use */
    if (tgpw->dflag & GP_DRAWDATA_ONLY3D) {
      const int no_xray = (tgpw->dflag & GP_DRAWDATA_NO_XRAY);

      if (no_xray) {
        GPU_depth_test(GPU_DEPTH_LESS_EQUAL);

        /* first arg is normally rv3d->dist, but this isn't
         * available here and seems to work quite well without */
        GPU_polygon_offset(1.0f, 1.0f);
      }

      /* 3D Stroke */
      /* set color using material tint color and opacity */
      if (!tgpw->onion) {
        interp_v3_v3v3(tcolor, gp_style->stroke_rgba, tgpw->tintcolor, tgpw->tintcolor[3]);
        tcolor[3] = gp_style->stroke_rgba[3] * tgpw->opacity;
        copy_v4_v4(ink, tcolor);
      }
      else {
        if (tgpw->custonion) {
          copy_v4_v4(ink, tgpw->tintcolor);
        }
        else {
          ARRAY_SET_ITEMS(tcolor, UNPACK3(gp_style->stroke_rgba), tgpw->opacity);
          copy_v4_v4(ink, tcolor);
        }
      }

      /* if used for fill, set opacity to 1 */
      if (tgpw->is_fill_stroke) {
        if (ink[3] >= GPENCIL_ALPHA_OPACITY_THRESH) {
          ink[3] = 1.0f;
        }
      }

      if (gp_style->mode == GP_MATERIAL_MODE_DOT) {
        /* volumetric stroke drawing */
        if (tgpw->disable_fill != 1) {
          gpencil_draw_stroke_volumetric_3d(gps->points, gps->totpoints, sthickness, ink);
        }
      }
      else {
        /* 3D Lines - OpenGL primitives-based */
        if (gps->totpoints > 1) {
          tgpw->gps = gps;
          gpencil_draw_stroke_3d(tgpw, sthickness, ink, gps->flag & GP_STROKE_CYCLIC);
        }
      }
      if (no_xray) {
        GPU_depth_test(GPU_DEPTH_NONE);

        GPU_polygon_offset(0.0f, 0.0f);
      }
    }
    /* if only one stroke, exit from loop */
    if (is_unique) {
      break;
    }
  }

  GPU_depth_mask(prev_depth_mask);
  GPU_program_point_size(false);
}

/* ----- General Drawing ------ */

void ed_dpen_draw_fill(DPenDraw *tdpw)
{
  dpen_draw_strokes(tdpw);
}
