/** Brush based operators for editing Dune-Pen strokes. **/

#include "MEM_guardedalloc.h"

#include "lib_blenlib.h"
#include "lib_math.h"

#include "i18n_translation.h"

#include "types_armature_types.h"
#include "types_brush_types.h"
#include "types_gpencil_types.h"

#include "dune_action.h"
#include "dune_brush.h"
#include "dune_colortools.h"
#include "dune_context.h"
#include "dune_deform.h"
#include "dune_gpencil.h"
#include "dune_main.h"
#include "dune_modifier.h"
#include "dune_object_deform.h"
#include "dune_report.h"
#include "types_meshdata_types.h"

#include "wm_api.h"
#include "wm_types.h"

#include "api_access.h"
#include "api_define.h"
#include "api_prototypes.h"

#include "ui_view2d.h"

#include "ed_dpen.h"
#include "ed_screen.h"
#include "ed_view3d.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "dpen_intern.h"

/* ************************************************ */
/* General Brush Editing Context */
#define DPEN_SELECT_BUFFER_CHUNK 256

/* Grid of Colors for Smear. */
typedef struct DPenGrid {
  /** Lower right corner of rectangle of grid cell. */
  float bottom[2];
  /** Upper left corner of rectangle of grid cell. */
  float top[2];
  /** Average Color */
  float color[4];
  /** Total points included. */
  int totcol;

} DPenGrid;

/* List of points affected by brush. */
typedef struct DPenSelected {
  /** Referenced stroke. */
  DPenStroke *dps;
  /** Point index in points array. */
  int pt_index;
  /** Position */
  int pc[2];
  /** Color */
  float color[4];
} DPenSelected;

/* Context for brush operators */
typedef struct DPenBrushWeightpaintData {
  struct Main *dmain;
  Scene *scene;
  Object *object;

  ARegion *region;

  /* Current DPen datablock */
  DPenData *dpd;

  Brush *brush;

  /* Space Conversion Data */
  DPenSpaceConversion dsc;

  /* Is the brush currently painting? */
  bool is_painting;

  /* Start of new paint */
  bool first;

  /* Is multi-frame editing enabled, and are we using falloff for that? */
  bool is_multiframe;
  bool use_multiframe_falloff;

  /* active vertex group */
  int vrgroup;

  /* Brush Runtime Data: */
  /* - position and pressure
   * - the *_prev variants are the previous values
   */
  float mval[2], mval_prev[2];
  float pressure, pressure_prev;

  /* - Effect 2D vector */
  float dvec[2];

  /* - multi-frame falloff factor. */
  float mf_falloff;

  /* brush geometry (bounding box). */
  rcti brush_rect;

  /* Temp data to save selected points */
  /** Stroke buffer. */
  DPenSelected *pbuffer;
  /** Number of elements currently used in cache. */
  int pbuffer_used;
  /** Number of total elements available in cache. */
  int pbuffer_size;
} DPenBrushWeightpaintData;

/* Ensure the buffer to hold temp selected point size is enough to save all points selected. */
static DPenSelected *dpen_select_buffer_ensure(DPenSelected *buffer_array,
                                                  int *buffer_size,
                                                  int *buffer_used,
                                                  const bool clear)
{
  DPenSelected *p = NULL;

  /* By default a buffer is created with one block with a predefined number of free slots,
   * if the size is not enough, the cache is reallocated adding a new block of free slots.
   * This is done in order to keep cache small and improve speed. */
  if (*buffer_used + 1 > *buffer_size) {
    if ((*buffer_size == 0) || (buffer_array == NULL)) {
      p = MEM_callocN(sizeof(struct DPenSelected) * DPEN_SELECT_BUFFER_CHUNK, __func__);
      *buffer_size = DPEN_SELECT_BUFFER_CHUNK;
    }
    else {
      *buffer_size += DPEN_SELECT_BUFFER_CHUNK;
      p = MEM_recallocN(buffer_array, sizeof(struct DPenSelected) * *buffer_size);
    }

    if (p == NULL) {
      *buffer_size = *buffer_used = 0;
    }

    buffer_array = p;
  }

  /* clear old data */
  if (clear) {
    *buffer_used = 0;
    if (buffer_array != NULL) {
      memset(buffer_array, 0, sizeof(DPenSelected) * *buffer_size);
    }
  }

  return buffer_array;
}

/* Brush Operations ------------------------------- */

/* Compute strength of effect. */
static float brush_influence_calc(DPenBrushWeightpaintData *dpbwd, const int radius, const int co[2])
{
  Brush *brush = dpbwd->brush;

  /* basic strength factor from brush settings */
  float influence = brush->alpha;

  /* use pressure? */
  if (brush->dpen_settings->flag & DPEN_BRUSH_USE_PRESSURE) {
    influence *= dpbwd->pressure;
  }

  /* distance fading */
  int mval_i[2];
  round_v2i_v2fl(mval_i, dpbwd->mval);
  float distance = (float)len_v2v2_int(mval_i, co);
  influence *= 1.0f - (distance / max_ff(radius, 1e-8));

  /* Apply Brush curve. */
  float brush_falloff = dune_brush_curve_strength(brush, distance, (float)radius);
  influence *= brush_falloff;

  /* apply multi-frame falloff */
  influence *= dpbwd->mf_falloff;

  /* return influence */
  return influence;
}

/* Compute effect vector for directional brushes. */
static void brush_calc_dvec_2d(DPenBrushWeightpaintData *dpbwd)
{
  dpbwd->dvec[0] = (float)(dpbwd->mval[0] - dpbwd->mval_prev[0]);
  dpbwd->dvec[1] = (float)(dpbwd->mval[1] - dpbwd->mval_prev[1]);

  normalize_v2(dpbwd->dvec);
}

/* ************************************************ */
/* Brush Callbacks
 * This section defines the callbacks used by each brush to perform their magic.
 * These are called on each point within the brush's radius. */

/* Draw Brush */
static bool brush_draw_apply(DPenBrushWeightpaintData *dpbwd,
                             DPenStroke *dps,
                             int pt_index,
                             const int radius,
                             const int co[2])
{
  /* create dvert */
  dune_dpen_dvert_ensure(dps);

  MDeformVert *dvert = dps->dvert + pt_index;
  float inf;

  /* Compute strength of effect */
  inf = brush_influence_calc(dpbwd, radius, co);

  /* need a vertex group */
  if (dpbwd->vrgroup == -1) {
    if (dpbwd->object) {
      Object *ob_armature = dune_modifiers_is_deformed_by_armature(dpbwd->object);
      if ((ob_armature != NULL)) {
        Bone *actbone = ((dArmature *)ob_armature->data)->act_bone;
        if (actbone != NULL) {
          DPoseChannel *pchan = dune_pose_channel_find_name(ob_armature->pose, actbone->name);
          if (pchan != NULL) {
            DDeformGroup *dg = dune_object_defgroup_find_name(dpbwd->object, pchan->name);
            if (dg == NULL) {
              dg = dune_object_defgroup_add_name(dpbwd->object, pchan->name);
            }
          }
        }
      }
      else {
        dune_object_defgroup_add(dpbwd->object);
      }
      DEG_relations_tag_update(gso->bmain);
      dpbwd->vrgroup = 0;
    }
  }
  else {
    DDeformGroup *defgroup = lib_findlink(&dbwd->dpd->vertex_group_names, dbwd->vrgroup);
    if (defgroup->flag & DG_LOCK_WEIGHT) {
      return false;
    }
  }
  /* Get current weight and blend. */
  MDeformWeight *dw = dune_defvert_ensure_index(dvert, dbwd->vrgroup);
  if (dw) {
    dw->weight = interpf(dbwd->brush->weight, dw->weight, inf);
    CLAMP(dw->weight, 0.0f, 1.0f);
  }
  return true;
}

/* ************************************************ */
/* Header Info */
static void dpen_weightpaint_brush_header_set(dContext *C)
{
  ed_workspace_status_text(C, TIP_("DPen Weight Paint: LMB to paint | RMB/Escape to Exit"));
}

/* ************************************************ */
/* Dune Pen Weight Paint Operator */

/* Init/Exit ----------------------------------------------- */

static bool dpen_weightpaint_brush_init(dContext *C, wmOperator *op)
{
  Scene *scene = ctx_data_scene(C);
  ToolSettings *ts = ctx_data_tool_settings(C);
  Object *ob = ctx_data_active_object(C);
  Paint *paint = &ts->dpen_weightpaint->paint;

  /* set the brush using the tool */
  DPenBrushWeightpaintData *dpbwd;

  /* setup operator data */
  dpbwd = MEM_callocN(sizeof(DPenBrushWeightpaintData), "DPenBrushWeightpaintData");
  op->customdata = dpbwd;

  dpbwd->dmain = ctx_data_main(C);

  dpbwd->brush = paint->brush;
  dune_curvemapping_init(dpbwd->brush->curve);

  dpbwd->is_painting = false;
  dpbwd->first = true;

  dpbwd->pbuffer = NULL;
  dpbwd->pbuffer_size = 0;
  dpbwd->pbuffer_used = 0;

  dpbwd->dpd = ed_dpen_data_get_active(C);
  dpbwd->scene = scene;
  dpbwd->object = ob;
  if (ob) {
    dpbwd->vrgroup = dpbwd->dpd->vertex_group_active_index - 1;
    if (!lib_findlink(&dpbwd->dpd->vertex_group_names, dpbwd->vrgroup)) {
      dpbwd->vrgroup = -1;
    }
  }
  else {
    dpbwd->vrgroup = -1;
  }

  dpbwd->region = ctx_wm_region(C);

  /* Multiframe settings. */
  dpbwd->is_multiframe = (bool)DPEN_MULTIEDIT_SESSIONS_ON(dpbwd->dpd);
  dpbwd->use_multiframe_falloff = (ts->dpen_sculpt.flag & DPEN_SCULPT_SETT_FLAG_FRAME_FALLOFF) != 0;

  /* Init multi-edit falloff curve data before doing anything,
   * so we won't have to do it again later. */
  if (dpbwd->is_multiframe) {
    dune_curvemapping_init(ts->dpen_sculpt.cur_falloff);
  }

  /* Setup space conversions. */
  dpen_point_conversion_init(C, &dpbwd->dsc);

  /* Update header. */
  dpen_weightpaint_brush_header_set(C);

  return true;
}

static void dpen_weightpaint_brush_exit(dContext *C, wmOperator *op)
{
  tGP_BrushWeightpaintData *dpbwd = op->customdata;

  /* Disable headerprints. */
  ED_workspace_status_text(C, NULL);

  /* Free operator data */
  MEM_SAFE_FREE(gso->pbuffer);
  MEM_SAFE_FREE(gso);
  op->customdata = NULL;
}

/* Poll callback for stroke weight paint operator. */
static bool gpencil_weightpaint_brush_poll(bContext *C)
{
  /* NOTE: this is a bit slower, but is the most accurate... */
  return CTX_DATA_COUNT(C, editable_gpencil_strokes) != 0;
}

/* Helper to save the points selected by the brush. */
static void gpencil_save_selected_point(tGP_BrushWeightpaintData *gso,
                                        bGPDstroke *gps,
                                        int index,
                                        int pc[2])
{
  tGP_Selected *selected;
  bGPDspoint *pt = &gps->points[index];

  /* Ensure the array to save the list of selected points is big enough. */
  gso->pbuffer = gpencil_select_buffer_ensure(
      gso->pbuffer, &gso->pbuffer_size, &gso->pbuffer_used, false);

  selected = &gso->pbuffer[gso->pbuffer_used];
  selected->gps = gps;
  selected->pt_index = index;
  copy_v2_v2_int(selected->pc, pc);
  copy_v4_v4(selected->color, pt->vert_color);

  gso->pbuffer_used++;
}

/* Select points in this stroke and add to an array to be used later. */
static void gpencil_weightpaint_select_stroke(tGP_BrushWeightpaintData *gso,
                                              bGPDstroke *gps,
                                              const float diff_mat[4][4],
                                              const float bound_mat[4][4])
{
  GP_SpaceConversion *gsc = &gso->gsc;
  rcti *rect = &gso->brush_rect;
  Brush *brush = gso->brush;
  const int radius = (brush->flag & GP_BRUSH_USE_PRESSURE) ? gso->brush->size * gso->pressure :
                                                             gso->brush->size;
  bGPDstroke *gps_active = (gps->runtime.gps_orig) ? gps->runtime.gps_orig : gps;
  bGPDspoint *pt_active = NULL;

  bGPDspoint *pt1, *pt2;
  bGPDspoint *pt = NULL;
  int pc1[2] = {0};
  int pc2[2] = {0};
  int i;
  int index;
  bool include_last = false;

  /* Check if the stroke collide with brush. */
  if (!ED_gpencil_stroke_check_collision(gsc, gps, gso->mval, radius, bound_mat)) {
    return;
  }

  if (gps->totpoints == 1) {
    bGPDspoint pt_temp;
    pt = &gps->points[0];
    gpencil_point_to_parent_space(gps->points, diff_mat, &pt_temp);
    gpencil_point_to_xy(gsc, gps, &pt_temp, &pc1[0], &pc1[1]);

    pt_active = (pt->runtime.pt_orig) ? pt->runtime.pt_orig : pt;
    /* Do bound-box check first. */
    if ((!ELEM(V2D_IS_CLIPPED, pc1[0], pc1[1])) && BLI_rcti_isect_pt(rect, pc1[0], pc1[1])) {
      /* only check if point is inside */
      int mval_i[2];
      round_v2i_v2fl(mval_i, gso->mval);
      if (len_v2v2_int(mval_i, pc1) <= radius) {
        /* apply operation to this point */
        if (pt_active != NULL) {
          gpencil_save_selected_point(gso, gps_active, 0, pc1);
        }
      }
    }
  }
  else {
    /* Loop over the points in the stroke, checking for intersections
     * - an intersection means that we touched the stroke
     */
    for (i = 0; (i + 1) < gps->totpoints; i++) {
      /* Get points to work with */
      pt1 = gps->points + i;
      pt2 = gps->points + i + 1;

      bGPDspoint npt;
      gpencil_point_to_parent_space(pt1, diff_mat, &npt);
      gpencil_point_to_xy(gsc, gps, &npt, &pc1[0], &pc1[1]);

      gpencil_point_to_parent_space(pt2, diff_mat, &npt);
      gpencil_point_to_xy(gsc, gps, &npt, &pc2[0], &pc2[1]);

      /* Check that point segment of the bound-box of the selection stroke */
      if (((!ELEM(V2D_IS_CLIPPED, pc1[0], pc1[1])) && BLI_rcti_isect_pt(rect, pc1[0], pc1[1])) ||
          ((!ELEM(V2D_IS_CLIPPED, pc2[0], pc2[1])) && BLI_rcti_isect_pt(rect, pc2[0], pc2[1]))) {
        /* Check if point segment of stroke had anything to do with
         * brush region  (either within stroke painted, or on its lines)
         * - this assumes that line-width is irrelevant.
         */
        if (gpencil_stroke_inside_circle(gso->mval, radius, pc1[0], pc1[1], pc2[0], pc2[1])) {

          /* To each point individually... */
          pt = &gps->points[i];
          pt_active = pt->runtime.pt_orig;
          if (pt_active != NULL) {
            index = (pt->runtime.pt_orig) ? pt->runtime.idx_orig : i;
            gpencil_save_selected_point(gso, gps_active, index, pc1);
          }

          /* Only do the second point if this is the last segment,
           * and it is unlikely that the point will get handled
           * otherwise.
           *
           * NOTE: There is a small risk here that the second point wasn't really
           *       actually in-range. In that case, it only got in because
           *       the line linking the points was!
           */
          if (i + 1 == gps->totpoints - 1) {
            pt = &gps->points[i + 1];
            pt_active = pt->runtime.pt_orig;
            if (pt_active != NULL) {
              index = (pt->runtime.pt_orig) ? pt->runtime.idx_orig : i + 1;
              gpencil_save_selected_point(gso, gps_active, index, pc2);
              include_last = false;
            }
          }
          else {
            include_last = true;
          }
        }
        else if (include_last) {
          /* This case is for cases where for whatever reason the second vert (1st here)
           * doesn't get included because the whole edge isn't in bounds,
           * but it would've qualified since it did with the previous step
           * (but wasn't added then, to avoid double-ups).
           */
          pt = &gps->points[i];
          pt_active = pt->runtime.pt_orig;
          if (pt_active != NULL) {
            index = (pt->runtime.pt_orig) ? pt->runtime.idx_orig : i;
            gpencil_save_selected_point(gso, gps_active, index, pc1);

            include_last = false;
          }
        }
      }
    }
  }
}

/* Apply weight paint brushes to strokes in the given frame. */
static bool gpencil_weightpaint_brush_do_frame(bContext *C,
                                               tGP_BrushWeightpaintData *gso,
                                               bGPDlayer *gpl,
                                               bGPDframe *gpf,
                                               const float diff_mat[4][4],
                                               const float bound_mat[4][4])
{
  Object *ob = CTX_data_active_object(C);
  char tool = gso->brush->gpencil_weight_tool;
  const int radius = (gso->brush->flag & GP_BRUSH_USE_PRESSURE) ?
                         gso->brush->size * gso->pressure :
                         gso->brush->size;
  tGP_Selected *selected = NULL;
  int i;

  /*---------------------------------------------------------------------
   * First step: select the points affected. This step is required to have
   * all selected points before apply the effect, because it could be
   * required to do some step. Now is not used, but the operator is ready.
   *--------------------------------------------------------------------- */
  LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
    /* Skip strokes that are invalid for current view. */
    if (ED_gpencil_stroke_can_use(C, gps) == false) {
      continue;
    }
    /* Check if the color is editable. */
    if (ED_gpencil_stroke_material_editable(ob, gpl, gps) == false) {
      continue;
    }

    /* Check points below the brush. */
    gpencil_weightpaint_select_stroke(gso, gps, diff_mat, bound_mat);
  }

  /*---------------------------------------------------------------------
   * Second step: Apply effect.
   *--------------------------------------------------------------------- */
  bool changed = false;
  for (i = 0; i < gso->pbuffer_used; i++) {
    changed = true;
    selected = &gso->pbuffer[i];

    switch (tool) {
      case GPWEIGHT_TOOL_DRAW: {
        brush_draw_apply(gso, selected->gps, selected->pt_index, radius, selected->pc);
        changed |= true;
        break;
      }
      default:
        printf("ERROR: Unknown type of GPencil Weight Paint brush\n");
        break;
    }
  }
  /* Clear the selected array, but keep the memory allocation. */
  gso->pbuffer = gpencil_select_buffer_ensure(
      gso->pbuffer, &gso->pbuffer_size, &gso->pbuffer_used, true);

  return changed;
}

/* Apply brush effect to all layers. */
static bool gpencil_weightpaint_brush_apply_to_layers(bContext *C, tGP_BrushWeightpaintData *gso)
{
  ToolSettings *ts = CTX_data_tool_settings(C);
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  Object *obact = gso->object;
  bool changed = false;

  Object *ob_eval = (Object *)DEG_get_evaluated_id(depsgraph, &obact->id);
  bGPdata *gpd = (bGPdata *)ob_eval->data;

  /* Find visible strokes, and perform operations on those if hit */
  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    /* If locked or no active frame, don't do anything. */
    if ((!BKE_gpencil_layer_is_editable(gpl)) || (gpl->actframe == NULL)) {
      continue;
    }

    /* Calculate transform matrix. */
    float diff_mat[4][4], bound_mat[4][4];
    BKE_gpencil_layer_transform_matrix_get(depsgraph, obact, gpl, diff_mat);
    copy_m4_m4(bound_mat, diff_mat);
    mul_m4_m4m4(diff_mat, diff_mat, gpl->layer_invmat);

    /* Active Frame or MultiFrame? */
    if (gso->is_multiframe) {
      /* init multi-frame falloff options */
      int f_init = 0;
      int f_end = 0;

      if (gso->use_multiframe_falloff) {
        BKE_gpencil_frame_range_selected(gpl, &f_init, &f_end);
      }

      LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
        /* Always do active frame; Otherwise, only include selected frames */
        if ((gpf == gpl->actframe) || (gpf->flag & GP_FRAME_SELECT)) {
          /* Compute multi-frame falloff factor. */
          if (gso->use_multiframe_falloff) {
            /* Falloff depends on distance to active frame
             * (relative to the overall frame range). */
            gso->mf_falloff = BKE_gpencil_multiframe_falloff_calc(
                gpf, gpl->actframe->framenum, f_init, f_end, ts->gp_sculpt.cur_falloff);
          }
          else {
            /* No falloff */
            gso->mf_falloff = 1.0f;
          }

          /* affect strokes in this frame */
          changed |= gpencil_weightpaint_brush_do_frame(C, gso, gpl, gpf, diff_mat, bound_mat);
        }
      }
    }
    else {
      if (gpl->actframe != NULL) {
        /* Apply to active frame's strokes */
        gso->mf_falloff = 1.0f;
        changed |= gpencil_weightpaint_brush_do_frame(
            C, gso, gpl, gpl->actframe, diff_mat, bound_mat);
      }
    }
  }

  return changed;
}

/* Calculate settings for applying brush */
static void gpencil_weightpaint_brush_apply(bContext *C, wmOperator *op, PointerRNA *itemptr)
{
  tGP_BrushWeightpaintData *gso = op->customdata;
  Brush *brush = gso->brush;
  const int radius = ((brush->flag & GP_BRUSH_USE_PRESSURE) ? gso->brush->size * gso->pressure :
                                                              gso->brush->size);
  float mousef[2];
  int mouse[2];
  bool changed = false;

  /* Get latest mouse coordinates */
  RNA_float_get_array(itemptr, "mouse", mousef);
  gso->mval[0] = mouse[0] = (int)(mousef[0]);
  gso->mval[1] = mouse[1] = (int)(mousef[1]);

  gso->pressure = RNA_float_get(itemptr, "pressure");

  /* Store coordinates as reference, if operator just started running */
  if (gso->first) {
    gso->mval_prev[0] = gso->mval[0];
    gso->mval_prev[1] = gso->mval[1];
    gso->pressure_prev = gso->pressure;
  }

  /* Update brush_rect, so that it represents the bounding rectangle of brush. */
  gso->brush_rect.xmin = mouse[0] - radius;
  gso->brush_rect.ymin = mouse[1] - radius;
  gso->brush_rect.xmax = mouse[0] + radius;
  gso->brush_rect.ymax = mouse[1] + radius;

  /* Calculate 2D direction vector and relative angle. */
  brush_calc_dvec_2d(gso);

  changed = gpencil_weightpaint_brush_apply_to_layers(C, gso);

  /* Updates */
  if (changed) {
    DEG_id_tag_update(&gso->gpd->id, ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  }

  /* Store values for next step */
  gso->mval_prev[0] = gso->mval[0];
  gso->mval_prev[1] = gso->mval[1];
  gso->pressure_prev = gso->pressure;
  gso->first = false;
}

/* Running --------------------------------------------- */

/* helper - a record stroke, and apply paint event */
static void gpencil_weightpaint_brush_apply_event(bContext *C,
                                                  wmOperator *op,
                                                  const wmEvent *event)
{
  tGP_BrushWeightpaintData *gso = op->customdata;
  PointerRNA itemptr;
  float mouse[2];

  mouse[0] = event->mval[0] + 1;
  mouse[1] = event->mval[1] + 1;

  /* fill in stroke */
  RNA_collection_add(op->ptr, "stroke", &itemptr);

  RNA_float_set_array(&itemptr, "mouse", mouse);
  RNA_boolean_set(&itemptr, "pen_flip", event->modifier & KM_CTRL);
  RNA_boolean_set(&itemptr, "is_start", gso->first);

  /* Handle pressure sensitivity (which is supplied by tablets). */
  float pressure = event->tablet.pressure;
  CLAMP(pressure, 0.0f, 1.0f);
  RNA_float_set(&itemptr, "pressure", pressure);

  /* apply */
  gpencil_weightpaint_brush_apply(C, op, &itemptr);
}

/* reapply */
static int gpencil_weightpaint_brush_exec(bContext *C, wmOperator *op)
{
  if (!gpencil_weightpaint_brush_init(C, op)) {
    return OPERATOR_CANCELLED;
  }

  RNA_BEGIN (op->ptr, itemptr, "stroke") {
    gpencil_weightpaint_brush_apply(C, op, &itemptr);
  }
  RNA_END;

  gpencil_weightpaint_brush_exit(C, op);

  return OPERATOR_FINISHED;
}

/* start modal painting */
static int gpencil_weightpaint_brush_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  tGP_BrushWeightpaintData *gso = NULL;
  const bool is_modal = RNA_boolean_get(op->ptr, "wait_for_input");
  const bool is_playing = ED_screen_animation_playing(CTX_wm_manager(C)) != NULL;

  /* the operator cannot work while play animation */
  if (is_playing) {
    BKE_report(op->reports, RPT_ERROR, "Cannot Paint while play animation");

    return OPERATOR_CANCELLED;
  }

  /* init painting data */
  if (!gpencil_weightpaint_brush_init(C, op)) {
    return OPERATOR_CANCELLED;
  }

  gso = op->customdata;

  /* register modal handler */
  WM_event_add_modal_handler(C, op);

  /* start drawing immediately? */
  if (is_modal == false) {
    ARegion *region = CTX_wm_region(C);

    /* apply first dab... */
    gso->is_painting = true;
    gpencil_weightpaint_brush_apply_event(C, op, event);

    /* redraw view with feedback */
    ED_region_tag_redraw(region);
  }

  return OPERATOR_RUNNING_MODAL;
}

/* painting - handle events */
static int gpencil_weightpaint_brush_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  tGP_BrushWeightpaintData *gso = op->customdata;
  const bool is_modal = RNA_boolean_get(op->ptr, "wait_for_input");
  bool redraw_region = false;
  bool redraw_toolsettings = false;

  /* The operator can be in 2 states: Painting and Idling */
  if (gso->is_painting) {
    /* Painting. */
    switch (event->type) {
      /* Mouse Move = Apply somewhere else */
      case MOUSEMOVE:
      case INBETWEEN_MOUSEMOVE:
        /* apply brush effect at new position */
        gpencil_weightpaint_brush_apply_event(C, op, event);

        /* force redraw, so that the cursor will at least be valid */
        redraw_region = true;
        break;

      /* Painting mbut release = Stop painting (back to idle) */
      case LEFTMOUSE:
        if (is_modal) {
          /* go back to idling... */
          gso->is_painting = false;
        }
        else {
          /* end painting, since we're not modal */
          gso->is_painting = false;

          gpencil_weightpaint_brush_exit(C, op);
          return OPERATOR_FINISHED;
        }
        break;

      /* Abort painting if any of the usual things are tried */
      case MIDDLEMOUSE:
      case RIGHTMOUSE:
      case EVT_ESCKEY:
        gpencil_weightpaint_brush_exit(C, op);
        return OPERATOR_FINISHED;
    }
  }
  else {
    /* Idling */
    BLI_assert(is_modal == true);

    switch (event->type) {
      /* Painting mbut press = Start painting (switch to painting state) */
      case LEFTMOUSE:
        /* do initial "click" apply */
        gso->is_painting = true;
        gso->first = true;

        gpencil_weightpaint_brush_apply_event(C, op, event);
        break;

      /* Exit modal operator, based on the "standard" ops */
      case RIGHTMOUSE:
      case EVT_ESCKEY:
        gpencil_weightpaint_brush_exit(C, op);
        return OPERATOR_FINISHED;

      /* MMB is often used for view manipulations */
      case MIDDLEMOUSE:
        return OPERATOR_PASS_THROUGH;

      /* Mouse movements should update the brush cursor - Just redraw the active region */
      case MOUSEMOVE:
      case INBETWEEN_MOUSEMOVE:
        redraw_region = true;
        break;

      /* Change Frame - Allowed */
      case EVT_LEFTARROWKEY:
      case EVT_RIGHTARROWKEY:
      case EVT_UPARROWKEY:
      case EVT_DOWNARROWKEY:
        return OPERATOR_PASS_THROUGH;

      /* Camera/View Gizmo's - Allowed */
      /* (See rationale in gpencil_paint.c -> gpencil_draw_modal()) */
      case EVT_PAD0:
      case EVT_PAD1:
      case EVT_PAD2:
      case EVT_PAD3:
      case EVT_PAD4:
      case EVT_PAD5:
      case EVT_PAD6:
      case EVT_PAD7:
      case EVT_PAD8:
      case EVT_PAD9:
        return OPERATOR_PASS_THROUGH;

      /* Unhandled event */
      default:
        break;
    }
  }

  /* Redraw region? */
  if (redraw_region) {
    ED_region_tag_redraw(CTX_wm_region(C));
  }

  /* Redraw toolsettings (brush settings)? */
  if (redraw_toolsettings) {
    DEG_id_tag_update(&gso->gpd->id, ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_SCENE | ND_TOOLSETTINGS, NULL);
  }

  return OPERATOR_RUNNING_MODAL;
}

void GPENCIL_OT_weight_paint(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Stroke Weight Paint";
  ot->idname = "GPENCIL_OT_weight_paint";
  ot->description = "Paint stroke points with a color";

  /* api callbacks */
  ot->exec = gpencil_weightpaint_brush_exec;
  ot->invoke = gpencil_weightpaint_brush_invoke;
  ot->modal = gpencil_weightpaint_brush_modal;
  ot->cancel = gpencil_weightpaint_brush_exit;
  ot->poll = gpencil_weightpaint_brush_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_BLOCKING;

  /* properties */
  PropertyRNA *prop;
  prop = RNA_def_collection_runtime(ot->srna, "stroke", &RNA_OperatorStrokeElement, "Stroke", "");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  prop = RNA_def_boolean(ot->srna, "wait_for_input", true, "Wait for Input", "");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
}
