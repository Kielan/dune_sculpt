#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_ghash.h"
#include "lib_lasso_2d.h"
#include "lib_math_vector.h"
#include "lib_rand.h"
#include "lib_utildefines.h"

#include "types_dpen.h"
#include "types_material.h"
#include "types_object.h"
#include "types_scene.h"
#include "types_screen.h"
#include "types_space.h"

#include "dune_context.h"
#include "dune_dpen.h"
#include "dune_dpen_curve.h"
#include "dune_dpen_geom.h"
#include "dune_material.h"
#include "dune_report.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "wm_api.h"
#include "wm_types.h"

#include "api_access.h"
#include "api_define.h"

#include "ui_view2d.h"

#include "ed_dpen.h"
#include "ed_select_utils.h"
#include "ed_view3d.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "dpen_intern.h"

/* -------------------------------------------------------------------- */
/** Shared Utilities */

/* Convert sculpt mask mode to Select mode */
static int dpen_select_mode_from_sculpt(eGP_Sculpt_SelectMaskFlag mode)
{
  if (mode & DPEN_SCULPT_MASK_SELECTMODE_POINT) {
    return DPEN_SELECTMODE_POINT;
  }
  if (mode & DPEN_SCULPT_MASK_SELECTMODE_STROKE) {
    return DPEN_SELECTMODE_STROKE;
  }
  if (mode & DPEN_SCULPT_MASK_SELECTMODE_SEGMENT) {
    return DPEN_SELECTMODE_SEGMENT;
  }
  return GP_SELECTMODE_POINT;
}

/* Convert vertex mask mode to Select mode */
static int dpen_select_mode_from_vertex(eDPEN_Sculpt_SelectMaskFlag mode)
{
  if (mode & DPEN_VERTEX_MASK_SELECTMODE_POINT) {
    return DPEN_SELECTMODE_POINT;
  }
  if (mode & DPEN_VERTEX_MASK_SELECTMODE_STROKE) {
    return DPEN_SELECTMODE_STROKE;
  }
  if (mode & DPEN_VERTEX_MASK_SELECTMODE_SEGMENT) {
    return DPEN_SELECTMODE_SEGMENT;
  }
  return DPEN_SELECTMODE_POINT;
}

static bool dpen_select_poll(dContext *C)
{
  DPenData *gpd = ed_dpen_data_get_active(C);
  ToolSettings *ts = ctx_data_tool_settings(C);

  if (GPENCIL_SCULPT_MODE(gpd)) {
    if (!(GPENCIL_ANY_SCULPT_MASK(ts->gpencil_selectmode_sculpt))) {
      return false;
    }
  }

  if (GPENCIL_VERTEX_MODE(gpd)) {
    if (!(GPENCIL_ANY_VERTEX_MASK(ts->gpencil_selectmode_vertex))) {
      return false;
    }
  }

  /* We just need some visible strokes,
   * and to be in edit-mode or other modes only to catch event. */
  if (GPENCIL_ANY_MODE(gpd)) {
    /* TODO: include a check for visible strokes? */
    if (gpd->layers.first) {
      return true;
    }
  }

  return false;
}

static bool gpencil_3d_point_to_screen_space(ARegion *region,
                                             const float diff_mat[4][4],
                                             const float co[3],
                                             int r_co[2])
{
  float parent_co[3];
  mul_v3_m4v3(parent_co, diff_mat, co);
  int screen_co[2];
  if (ED_view3d_project_int_global(
          region, parent_co, screen_co, V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN) ==
      V3D_PROJ_RET_OK) {
    if (!ELEM(V2D_IS_CLIPPED, screen_co[0], screen_co[1])) {
      copy_v2_v2_int(r_co, screen_co);
      return true;
    }
  }
  r_co[0] = V2D_IS_CLIPPED;
  r_co[1] = V2D_IS_CLIPPED;
  return false;
}

/* helper to deselect all selected strokes/points */
static void deselect_all_selected(bContext *C)
{
  /* Set selection index to 0. */
  Object *ob = CTX_data_active_object(C);
  bGPdata *gpd = ob->data;
  gpd->select_last_index = 0;

  CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
    /* deselect stroke and its points if selected */
    if (gps->flag & GP_STROKE_SELECT) {
      bGPDspoint *pt;
      int i;

      /* deselect points */
      for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
        pt->flag &= ~GP_SPOINT_SELECT;
      }

      /* deselect stroke itself too */
      gps->flag &= ~GP_STROKE_SELECT;
      BKE_gpencil_stroke_select_index_reset(gps);
    }

    /* deselect curve and curve points */
    if (gps->editcurve != NULL) {
      bGPDcurve *gpc = gps->editcurve;
      for (int j = 0; j < gpc->tot_curve_points; j++) {
        bGPDcurve_point *gpc_pt = &gpc->curve_points[j];
        BezTriple *bezt = &gpc_pt->bezt;
        gpc_pt->flag &= ~GP_CURVE_POINT_SELECT;
        BEZT_DESEL_ALL(bezt);
      }

      gpc->flag &= ~GP_CURVE_SELECT;
    }
  }
  CTX_DATA_END;
}

static void select_all_stroke_points(bGPdata *gpd, bGPDstroke *gps, bool select)
{
  for (int i = 0; i < gps->totpoints; i++) {
    bGPDspoint *pt = &gps->points[i];
    if (select) {
      pt->flag |= GP_SPOINT_SELECT;
    }
    else {
      pt->flag &= ~GP_SPOINT_SELECT;
    }
  }

  if (select) {
    gps->flag |= GP_STROKE_SELECT;
    BKE_gpencil_stroke_select_index_set(gpd, gps);
  }
  else {
    gps->flag &= ~GP_STROKE_SELECT;
    BKE_gpencil_stroke_select_index_reset(gps);
  }
}

static void select_all_curve_points(bGPdata *gpd, bGPDstroke *gps, bGPDcurve *gpc, bool deselect)
{
  for (int i = 0; i < gpc->tot_curve_points; i++) {
    bGPDcurve_point *gpc_pt = &gpc->curve_points[i];
    BezTriple *bezt = &gpc_pt->bezt;
    if (deselect == false) {
      gpc_pt->flag |= GP_CURVE_POINT_SELECT;
      BEZT_SEL_ALL(bezt);
    }
    else {
      gpc_pt->flag &= ~GP_CURVE_POINT_SELECT;
      BEZT_DESEL_ALL(bezt);
    }
  }

  if (deselect == false) {
    gpc->flag |= GP_CURVE_SELECT;
    gps->flag |= GP_STROKE_SELECT;
    BKE_gpencil_stroke_select_index_set(gpd, gps);
  }
  else {
    gpc->flag &= ~GP_CURVE_SELECT;
    gps->flag &= ~GP_STROKE_SELECT;
    BKE_gpencil_stroke_select_index_reset(gps);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select All Operator
 * \{ */

static bool gpencil_select_all_poll(bContext *C)
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);

  /* We just need some visible strokes,
   * and to be in edit-mode or other modes only to catch event. */
  if (GPENCIL_ANY_MODE(gpd)) {
    if (gpd->layers.first) {
      return true;
    }
  }

  return false;
}

static int gpencil_select_all_exec(bContext *C, wmOperator *op)
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  int action = RNA_enum_get(op->ptr, "action");
  const bool is_curve_edit = (bool)GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd);

  if (gpd == NULL) {
    BKE_report(op->reports, RPT_ERROR, "No Grease Pencil data");
    return OPERATOR_CANCELLED;
  }

  /* If not edit/sculpt mode, the event has been caught but not processed. */
  if (GPENCIL_NONE_EDIT_MODE(gpd)) {
    return OPERATOR_CANCELLED;
  }

  /* For sculpt mode, if mask is disable, only allows deselect */
  if (GPENCIL_SCULPT_MODE(gpd)) {
    ToolSettings *ts = CTX_data_tool_settings(C);
    if ((!(GPENCIL_ANY_SCULPT_MASK(ts->gpencil_selectmode_sculpt))) && (action != SEL_DESELECT)) {
      return OPERATOR_CANCELLED;
    }
  }

  if (is_curve_edit) {
    ED_gpencil_select_curve_toggle_all(C, action);
  }
  else {
    ED_gpencil_select_toggle_all(C, action);
  }

  /* updates */
  DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);

  /* copy on write tag is needed, or else no refresh happens */
  DEG_id_tag_update(&gpd->id, ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_GPENCIL | NA_SELECTED, NULL);
  WM_event_add_notifier(C, NC_GEOM | ND_SELECT, NULL);
  return OPERATOR_FINISHED;
}

void GPENCIL_OT_select_all(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "(De)select All Strokes";
  ot->idname = "GPENCIL_OT_select_all";
  ot->description = "Change selection of all Grease Pencil strokes currently visible";

  /* callbacks */
  ot->exec = gpencil_select_all_exec;
  ot->poll = gpencil_select_all_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  WM_operator_properties_select_all(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Linked Operator
 * \{ */

static int gpencil_select_linked_exec(bContext *C, wmOperator *op)
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  const bool is_curve_edit = (bool)GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd);

  if (gpd == NULL) {
    BKE_report(op->reports, RPT_ERROR, "No Grease Pencil data");
    return OPERATOR_CANCELLED;
  }

  /* If not edit/sculpt mode, the event has been caught but not processed. */
  if (GPENCIL_NONE_EDIT_MODE(gpd)) {
    return OPERATOR_CANCELLED;
  }

  if (is_curve_edit) {
    GP_EDITABLE_CURVES_BEGIN(gps_iter, C, gpl, gps, gpc)
    {
      if (gpc->flag & GP_CURVE_SELECT) {
        for (int i = 0; i < gpc->tot_curve_points; i++) {
          bGPDcurve_point *gpc_pt = &gpc->curve_points[i];
          BezTriple *bezt = &gpc_pt->bezt;
          gpc_pt->flag |= GP_CURVE_POINT_SELECT;
          BEZT_SEL_ALL(bezt);
        }
      }
    }
    GP_EDITABLE_CURVES_END(gps_iter);
  }
  else {
    /* select all points in selected strokes */
    CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
      if (gps->flag & GP_STROKE_SELECT) {
        bGPDspoint *pt;
        int i;

        for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
          pt->flag |= GP_SPOINT_SELECT;
        }
      }
    }
    CTX_DATA_END;
  }

  /* updates */
  DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);

  /* copy on write tag is needed, or else no refresh happens */
  DEG_id_tag_update(&gpd->id, ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_GPENCIL | NA_SELECTED, NULL);
  WM_event_add_notifier(C, NC_GEOM | ND_SELECT, NULL);
  return OPERATOR_FINISHED;
}

void GPENCIL_OT_select_linked(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Linked";
  ot->idname = "GPENCIL_OT_select_linked";
  ot->description = "Select all points in same strokes as already selected points";

  /* callbacks */
  ot->exec = gpencil_select_linked_exec;
  ot->poll = gpencil_select_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Alternate Operator
 * \{ */

static int gpencil_select_alternate_exec(bContext *C, wmOperator *op)
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  const bool unselect_ends = RNA_boolean_get(op->ptr, "unselect_ends");
  const bool is_curve_edit = (bool)GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd);

  if (gpd == NULL) {
    BKE_report(op->reports, RPT_ERROR, "No Grease Pencil data");
    return OPERATOR_CANCELLED;
  }

  /* If not edit/sculpt mode, the event has been caught but not processed. */
  if (GPENCIL_NONE_EDIT_MODE(gpd)) {
    return OPERATOR_CANCELLED;
  }

  bool changed = false;
  if (is_curve_edit) {
    GP_EDITABLE_CURVES_BEGIN(gps_iter, C, gpl, gps, gpc)
    {
      if ((gps->flag & GP_STROKE_SELECT) && (gps->totpoints > 1)) {
        int idx = 0;
        int start = 0;
        if (unselect_ends) {
          start = 1;
        }

        for (int i = start; i < gpc->tot_curve_points; i++) {
          bGPDcurve_point *gpc_pt = &gpc->curve_points[i];
          if ((idx % 2) == 0) {
            gpc_pt->flag |= GP_SPOINT_SELECT;
            BEZT_SEL_ALL(&gpc_pt->bezt);
          }
          else {
            gpc_pt->flag &= ~GP_SPOINT_SELECT;
            BEZT_DESEL_ALL(&gpc_pt->bezt);
          }
          idx++;
        }

        if (unselect_ends) {
          bGPDcurve_point *gpc_pt = &gpc->curve_points[0];
          gpc_pt->flag &= ~GP_SPOINT_SELECT;
          BEZT_DESEL_ALL(&gpc_pt->bezt);

          gpc_pt = &gpc->curve_points[gpc->tot_curve_points - 1];
          gpc_pt->flag &= ~GP_SPOINT_SELECT;
          BEZT_DESEL_ALL(&gpc_pt->bezt);
        }

        BKE_gpencil_curve_sync_selection(gpd, gps);
        changed = true;
      }
    }
    GP_EDITABLE_CURVES_END(gps_iter);
  }
  else {
    /* select all points in selected strokes */
    CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
      if ((gps->flag & GP_STROKE_SELECT) && (gps->totpoints > 1)) {
        bGPDspoint *pt;
        int row = 0;
        int start = 0;
        if (unselect_ends) {
          start = 1;
        }

        for (int i = start; i < gps->totpoints; i++) {
          pt = &gps->points[i];
          if ((row % 2) == 0) {
            pt->flag |= GP_SPOINT_SELECT;
          }
          else {
            pt->flag &= ~GP_SPOINT_SELECT;
          }
          row++;
        }

        /* unselect start and end points */
        if (unselect_ends) {
          pt = &gps->points[0];
          pt->flag &= ~GP_SPOINT_SELECT;

          pt = &gps->points[gps->totpoints - 1];
          pt->flag &= ~GP_SPOINT_SELECT;
        }

        changed = true;
      }
    }
    CTX_DATA_END;
  }

  if (changed) {
    /* updates */
    DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);

    /* copy on write tag is needed, or else no refresh happens */
    DEG_id_tag_update(&gpd->id, ID_RECALC_COPY_ON_WRITE);

    WM_event_add_notifier(C, NC_GPENCIL | NA_SELECTED, NULL);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, NULL);
  }

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_select_alternate(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Alternated";
  ot->idname = "GPENCIL_OT_select_alternate";
  ot->description = "Select alternative points in same strokes as already selected points";

  /* callbacks */
  ot->exec = gpencil_select_alternate_exec;
  ot->poll = gpencil_select_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna,
                  "unselect_ends",
                  false,
                  "Unselect Ends",
                  "Do not select the first and last point of the stroke");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Random Operator
 * \{ */

static int gpencil_select_random_exec(bContext *C, wmOperator *op)
{
  Object *ob = CTX_data_active_object(C);
  ToolSettings *ts = CTX_data_tool_settings(C);
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  if ((gpd == NULL) || (GPENCIL_NONE_EDIT_MODE(gpd))) {
    return OPERATOR_CANCELLED;
  }

  const bool unselect_ends = RNA_boolean_get(op->ptr, "unselect_ends");
  const bool select = (RNA_enum_get(op->ptr, "action") == SEL_SELECT);
  const float randfac = RNA_float_get(op->ptr, "ratio");
  const int seed = WM_operator_properties_select_random_seed_increment_get(op);
  const int start = (unselect_ends) ? 1 : 0;
  const bool is_curve_edit = (bool)GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd);

  int selectmode;
  if (ob && ob->mode == OB_MODE_SCULPT_GPENCIL) {
    selectmode = gpencil_select_mode_from_sculpt(ts->gpencil_selectmode_sculpt);
  }
  else if (ob && ob->mode == OB_MODE_VERTEX_GPENCIL) {
    selectmode = gpencil_select_mode_from_vertex(ts->gpencil_selectmode_vertex);
  }
  else {
    selectmode = ts->gpencil_selectmode_edit;
  }

  bool changed = false;
  int seed_iter = seed;
  int stroke_idx = 0;

  if (is_curve_edit) {
    GP_EDITABLE_CURVES_BEGIN(gps_iter, C, gpl, gps, gpc)
    {
      /* Only apply to unselected strokes (if select). */
      if (select) {
        if ((gps->flag & GP_STROKE_SELECT) || (gps->totpoints == 0)) {
          continue;
        }
      }
      else {
        if (((gps->flag & GP_STROKE_SELECT) == 0) || (gps->totpoints == 0)) {
          continue;
        }
      }

      /* Different seed by stroke. */
      seed_iter += gps->totpoints + stroke_idx;
      stroke_idx++;

      if (selectmode == GP_SELECTMODE_STROKE) {
        RNG *rng = BLI_rng_new(seed_iter);
        const unsigned int j = BLI_rng_get_uint(rng) % gps->totpoints;
        bool select_stroke = ((gps->totpoints * randfac) <= j) ? true : false;
        select_stroke ^= select;
        /* Curve function has select parameter inverted. */
        select_all_curve_points(gpd, gps, gps->editcurve, !select_stroke);
        changed = true;
        BLI_rng_free(rng);
      }
      else {
        int elem_map_len = 0;
        bGPDcurve_point **elem_map = MEM_mallocN(sizeof(*elem_map) * gpc->tot_curve_points,
                                                 __func__);
        bGPDcurve_point *ptc;
        for (int i = start; i < gpc->tot_curve_points; i++) {
          bGPDcurve_point *gpc_pt = &gpc->curve_points[i];
          elem_map[elem_map_len++] = gpc_pt;
        }

        BLI_array_randomize(elem_map, sizeof(*elem_map), elem_map_len, seed_iter);
        const int count_select = elem_map_len * randfac;
        for (int i = 0; i < count_select; i++) {
          ptc = elem_map[i];
          if (select) {
            ptc->flag |= GP_SPOINT_SELECT;
            BEZT_SEL_ALL(&ptc->bezt);
          }
          else {
            ptc->flag &= ~GP_SPOINT_SELECT;
            BEZT_DESEL_ALL(&ptc->bezt);
          }
        }
        MEM_freeN(elem_map);

        /* unselect start and end points */
        if (unselect_ends) {
          bGPDcurve_point *gpc_pt = &gpc->curve_points[0];
          gpc_pt->flag &= ~GP_SPOINT_SELECT;
          BEZT_DESEL_ALL(&gpc_pt->bezt);

          gpc_pt = &gpc->curve_points[gpc->tot_curve_points - 1];
          gpc_pt->flag &= ~GP_SPOINT_SELECT;
          BEZT_DESEL_ALL(&gpc_pt->bezt);
        }

        BKE_gpencil_curve_sync_selection(gpd, gps);
      }

      changed = true;
    }
    GP_EDITABLE_CURVES_END(gps_iter);
  }
  else {
    CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
      /* Only apply to unselected strokes (if select). */
      if (select) {
        if ((gps->flag & GP_STROKE_SELECT) || (gps->totpoints == 0)) {
          continue;
        }
      }
      else {
        if (((gps->flag & GP_STROKE_SELECT) == 0) || (gps->totpoints == 0)) {
          continue;
        }
      }

      /* Different seed by stroke. */
      seed_iter += gps->totpoints + stroke_idx;
      stroke_idx++;

      if (selectmode == GP_SELECTMODE_STROKE) {
        RNG *rng = BLI_rng_new(seed_iter);
        const unsigned int j = BLI_rng_get_uint(rng) % gps->totpoints;
        bool select_stroke = ((gps->totpoints * randfac) <= j) ? true : false;
        select_stroke ^= select;
        select_all_stroke_points(gpd, gps, select_stroke);
        changed = true;
        BLI_rng_free(rng);
      }
      else {
        int elem_map_len = 0;
        bGPDspoint **elem_map = MEM_mallocN(sizeof(*elem_map) * gps->totpoints, __func__);
        bGPDspoint *pt;
        for (int i = start; i < gps->totpoints; i++) {
          pt = &gps->points[i];
          elem_map[elem_map_len++] = pt;
        }

        BLI_array_randomize(elem_map, sizeof(*elem_map), elem_map_len, seed_iter);
        const int count_select = elem_map_len * randfac;
        for (int i = 0; i < count_select; i++) {
          pt = elem_map[i];
          if (select) {
            pt->flag |= GP_SPOINT_SELECT;
          }
          else {
            pt->flag &= ~GP_SPOINT_SELECT;
          }
        }
        MEM_freeN(elem_map);

        /* unselect start and end points */
        if (unselect_ends) {
          pt = &gps->points[0];
          pt->flag &= ~GP_SPOINT_SELECT;

          pt = &gps->points[gps->totpoints - 1];
          pt->flag &= ~GP_SPOINT_SELECT;
        }

        BKE_gpencil_stroke_sync_selection(gpd, gps);
      }

      changed = true;
    }
    CTX_DATA_END;
  }

  if (changed) {
    /* updates */
    DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);

    /* copy on write tag is needed, or else no refresh happens */
    DEG_id_tag_update(&gpd->id, ID_RECALC_COPY_ON_WRITE);

    WM_event_add_notifier(C, NC_GPENCIL | NA_SELECTED, NULL);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, NULL);
  }

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_select_random(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Random";
  ot->idname = "GPENCIL_OT_select_random";
  ot->description = "Select random points for non selected strokes";

  /* callbacks */
  ot->exec = gpencil_select_random_exec;
  ot->poll = gpencil_select_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  WM_operator_properties_select_random(ot);
  RNA_def_boolean(ot->srna,
                  "unselect_ends",
                  false,
                  "Unselect Ends",
                  "Do not select the first and last point of the stroke");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Grouped Operator
 * \{ */

typedef enum eGP_SelectGrouped {
  /* Select strokes in the same layer */
  GP_SEL_SAME_LAYER = 0,

  /* Select strokes with the same color */
  GP_SEL_SAME_MATERIAL = 1,

  /* TODO: All with same prefix -
   * Useful for isolating all layers for a particular character for instance. */
  /* TODO: All with same appearance - color/opacity/volumetric/fills ? */
} eGP_SelectGrouped;

/* ----------------------------------- */

/* On each visible layer, check for selected strokes - if found, select all others */
static bool gpencil_select_same_layer(bContext *C)
{
  Scene *scene = CTX_data_scene(C);
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  const bool is_curve_edit = (bool)GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd);

  bool changed = false;
  CTX_DATA_BEGIN (C, bGPDlayer *, gpl, editable_gpencil_layers) {
    bGPDframe *gpf = BKE_gpencil_layer_frame_get(gpl, CFRA, GP_GETFRAME_USE_PREV);
    bGPDstroke *gps;
    bool found = false;

    if (gpf == NULL) {
      continue;
    }

    /* Search for a selected stroke */
    for (gps = gpf->strokes.first; gps; gps = gps->next) {
      if (ED_gpencil_stroke_can_use(C, gps)) {
        if (gps->flag & GP_STROKE_SELECT) {
          found = true;
          break;
        }
      }
    }

    /* Select all if found */
    if (found) {
      if (is_curve_edit) {
        for (gps = gpf->strokes.first; gps; gps = gps->next) {
          if (gps->editcurve != NULL && ED_gpencil_stroke_can_use(C, gps)) {
            bGPDcurve *gpc = gps->editcurve;
            for (int i = 0; i < gpc->tot_curve_points; i++) {
              bGPDcurve_point *gpc_pt = &gpc->curve_points[i];
              gpc_pt->flag |= GP_CURVE_POINT_SELECT;
              BEZT_SEL_ALL(&gpc_pt->bezt);
            }
            gpc->flag |= GP_CURVE_SELECT;
            gps->flag |= GP_STROKE_SELECT;
            BKE_gpencil_stroke_select_index_set(gpd, gps);

            changed = true;
          }
        }
      }
      else {
        for (gps = gpf->strokes.first; gps; gps = gps->next) {
          if (ED_gpencil_stroke_can_use(C, gps)) {
            bGPDspoint *pt;
            int i;

            for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
              pt->flag |= GP_SPOINT_SELECT;
            }

            gps->flag |= GP_STROKE_SELECT;
            BKE_gpencil_stroke_select_index_set(gpd, gps);

            changed = true;
          }
        }
      }
    }
  }
  CTX_DATA_END;

  return changed;
}

/* Select all strokes with same colors as selected ones */
static bool gpencil_select_same_material(bContext *C)
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  const bool is_curve_edit = (bool)GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd);
  /* First, build set containing all the colors of selected strokes */
  GSet *selected_colors = BLI_gset_str_new("GP Selected Colors");

  bool changed = false;

  CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
    if (gps->flag & GP_STROKE_SELECT) {
      /* add instead of insert here, otherwise the uniqueness check gets skipped,
       * and we get many duplicate entries...
       */
      BLI_gset_add(selected_colors, &gps->mat_nr);
    }
  }
  CTX_DATA_END;

  /* Second, select any visible stroke that uses these colors */
  if (is_curve_edit) {
    CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
      if (gps->editcurve != NULL && BLI_gset_haskey(selected_colors, &gps->mat_nr)) {
        bGPDcurve *gpc = gps->editcurve;
        for (int i = 0; i < gpc->tot_curve_points; i++) {
          bGPDcurve_point *gpc_pt = &gpc->curve_points[i];
          gpc_pt->flag |= GP_CURVE_POINT_SELECT;
          BEZT_SEL_ALL(&gpc_pt->bezt);
        }
        gpc->flag |= GP_CURVE_SELECT;
        gps->flag |= GP_STROKE_SELECT;
        BKE_gpencil_stroke_select_index_set(gpd, gps);

        changed = true;
      }
    }
    CTX_DATA_END;
  }
  else {
    CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
      if (BLI_gset_haskey(selected_colors, &gps->mat_nr)) {
        /* select this stroke */
        bGPDspoint *pt;
        int i;

        for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
          pt->flag |= GP_SPOINT_SELECT;
        }

        gps->flag |= GP_STROKE_SELECT;
        BKE_gpencil_stroke_select_index_set(gpd, gps);

        changed = true;
      }
    }
    CTX_DATA_END;
  }

  /* Free memory. */
  if (selected_colors != NULL) {
    BLI_gset_free(selected_colors, NULL);
  }

  return changed;
}

/* ----------------------------------- */

static int gpencil_select_grouped_exec(bContext *C, wmOperator *op)
{
  eGP_SelectGrouped mode = RNA_enum_get(op->ptr, "type");
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  /* If not edit/sculpt mode, the event has been caught but not processed. */
  if (GPENCIL_NONE_EDIT_MODE(gpd)) {
    return OPERATOR_CANCELLED;
  }

  bool changed = false;

  switch (mode) {
    case GP_SEL_SAME_LAYER:
      changed = gpencil_select_same_layer(C);
      break;
    case GP_SEL_SAME_MATERIAL:
      changed = gpencil_select_same_material(C);
      break;

    default:
      BLI_assert_msg(0, "unhandled select grouped gpencil mode");
      break;
  }

  if (changed) {
    /* updates */
    DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);

    /* copy on write tag is needed, or else no refresh happens */
    DEG_id_tag_update(&gpd->id, ID_RECALC_COPY_ON_WRITE);

    WM_event_add_notifier(C, NC_GPENCIL | NA_SELECTED, NULL);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, NULL);
  }
  return OPERATOR_FINISHED;
}

void GPENCIL_OT_select_grouped(wmOperatorType *ot)
{
  static const EnumPropertyItem prop_select_grouped_types[] = {
      {GP_SEL_SAME_LAYER, "LAYER", 0, "Layer", "Shared layers"},
      {GP_SEL_SAME_MATERIAL, "MATERIAL", 0, "Material", "Shared materials"},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Select Grouped";
  ot->idname = "GPENCIL_OT_select_grouped";
  ot->description = "Select all strokes with similar characteristics";

  /* callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = gpencil_select_grouped_exec;
  ot->poll = gpencil_select_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = RNA_def_enum(
      ot->srna, "type", prop_select_grouped_types, GP_SEL_SAME_LAYER, "Type", "");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select First
 * \{ */

static int gpencil_select_first_exec(bContext *C, wmOperator *op)
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  const bool is_curve_edit = (bool)GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd);

  /* If not edit/sculpt mode, the event has been caught but not processed. */
  if (GPENCIL_NONE_EDIT_MODE(gpd)) {
    return OPERATOR_CANCELLED;
  }

  const bool only_selected = RNA_boolean_get(op->ptr, "only_selected_strokes");
  const bool extend = RNA_boolean_get(op->ptr, "extend");

  bool changed = false;
  CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
    /* skip stroke if we're only manipulating selected strokes */
    if (only_selected && !(gps->flag & GP_STROKE_SELECT)) {
      continue;
    }

    /* select first point */
    BLI_assert(gps->totpoints >= 1);

    if (is_curve_edit) {
      if (gps->editcurve != NULL) {
        bGPDcurve *gpc = gps->editcurve;
        gpc->curve_points[0].flag |= GP_CURVE_POINT_SELECT;
        BEZT_SEL_ALL(&gpc->curve_points[0].bezt);
        gpc->flag |= GP_CURVE_SELECT;
        gps->flag |= GP_STROKE_SELECT;
        BKE_gpencil_stroke_select_index_set(gpd, gps);

        if ((extend == false) && (gps->totpoints > 1)) {
          for (int i = 1; i < gpc->tot_curve_points; i++) {
            bGPDcurve_point *gpc_pt = &gpc->curve_points[i];
            gpc_pt->flag &= ~GP_CURVE_POINT_SELECT;
            BEZT_DESEL_ALL(&gpc_pt->bezt);
          }
        }
        changed = true;
      }
    }
    else {
      gps->points->flag |= GP_SPOINT_SELECT;
      gps->flag |= GP_STROKE_SELECT;
      BKE_gpencil_stroke_select_index_set(gpd, gps);

      /* deselect rest? */
      if ((extend == false) && (gps->totpoints > 1)) {
        /* start from index 1, to skip the first point that we'd just selected... */
        bGPDspoint *pt = &gps->points[1];
        int i = 1;

        for (; i < gps->totpoints; i++, pt++) {
          pt->flag &= ~GP_SPOINT_SELECT;
        }
      }
      changed = true;
    }
  }
  CTX_DATA_END;

  if (changed) {
    /* updates */
    DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);

    /* copy on write tag is needed, or else no refresh happens */
    DEG_id_tag_update(&gpd->id, ID_RECALC_COPY_ON_WRITE);

    WM_event_add_notifier(C, NC_GPENCIL | NA_SELECTED, NULL);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, NULL);
  }

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_select_first(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select First";
  ot->idname = "GPENCIL_OT_select_first";
  ot->description = "Select first point in Grease Pencil strokes";

  /* callbacks */
  ot->exec = gpencil_select_first_exec;
  ot->poll = gpencil_select_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna,
                  "only_selected_strokes",
                  false,
                  "Selected Strokes Only",
                  "Only select the first point of strokes that already have points selected");

  RNA_def_boolean(ot->srna,
                  "extend",
                  false,
                  "Extend",
                  "Extend selection instead of deselecting all other selected points");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Last
 * \{ */

static int gpencil_select_last_exec(bContext *C, wmOperator *op)
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  const bool is_curve_edit = (bool)GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd);

  /* If not edit/sculpt mode, the event has been caught but not processed. */
  if (GPENCIL_NONE_EDIT_MODE(gpd)) {
    return OPERATOR_CANCELLED;
  }

  const bool only_selected = RNA_boolean_get(op->ptr, "only_selected_strokes");
  const bool extend = RNA_boolean_get(op->ptr, "extend");

  bool changed = false;
  CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
    /* skip stroke if we're only manipulating selected strokes */
    if (only_selected && !(gps->flag & GP_STROKE_SELECT)) {
      continue;
    }

    /* select last point */
    BLI_assert(gps->totpoints >= 1);

    if (is_curve_edit) {
      if (gps->editcurve != NULL) {
        bGPDcurve *gpc = gps->editcurve;
        gpc->curve_points[gpc->tot_curve_points - 1].flag |= GP_CURVE_POINT_SELECT;
        BEZT_SEL_ALL(&gpc->curve_points[gpc->tot_curve_points - 1].bezt);
        gpc->flag |= GP_CURVE_SELECT;
        gps->flag |= GP_STROKE_SELECT;
        BKE_gpencil_stroke_select_index_set(gpd, gps);
        if ((extend == false) && (gps->totpoints > 1)) {
          for (int i = 0; i < gpc->tot_curve_points - 1; i++) {
            bGPDcurve_point *gpc_pt = &gpc->curve_points[i];
            gpc_pt->flag &= ~GP_CURVE_POINT_SELECT;
            BEZT_DESEL_ALL(&gpc_pt->bezt);
          }
        }
        changed = true;
      }
    }
    else {
      gps->points[gps->totpoints - 1].flag |= GP_SPOINT_SELECT;
      gps->flag |= GP_STROKE_SELECT;
      BKE_gpencil_stroke_select_index_set(gpd, gps);

      /* deselect rest? */
      if ((extend == false) && (gps->totpoints > 1)) {
        /* don't include the last point... */
        bGPDspoint *pt = gps->points;
        int i = 0;

        for (; i < gps->totpoints - 1; i++, pt++) {
          pt->flag &= ~GP_SPOINT_SELECT;
        }
      }

      changed = true;
    }
  }
  CTX_DATA_END;

  if (changed) {
    /* updates */
    DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);

    /* copy on write tag is needed, or else no refresh happens */
    DEG_id_tag_update(&gpd->id, ID_RECALC_COPY_ON_WRITE);

    WM_event_add_notifier(C, NC_GPENCIL | NA_SELECTED, NULL);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, NULL);
  }

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_select_last(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Last";
  ot->idname = "GPENCIL_OT_select_last";
  ot->description = "Select last point in Grease Pencil strokes";

  /* callbacks */
  ot->exec = gpencil_select_last_exec;
  ot->poll = gpencil_select_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna,
                  "only_selected_strokes",
                  false,
                  "Selected Strokes Only",
                  "Only select the last point of strokes that already have points selected");

  RNA_def_boolean(ot->srna,
                  "extend",
                  false,
                  "Extend",
                  "Extend selection instead of deselecting all other selected points");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Mode Operator
 * \{ */

static int gpencil_select_more_exec(bContext *C, wmOperator *UNUSED(op))
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  const bool is_curve_edit = (bool)GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd);
  /* If not edit/sculpt mode, the event has been caught but not processed. */
  if (GPENCIL_NONE_EDIT_MODE(gpd)) {
    return OPERATOR_CANCELLED;
  }

  bool changed = false;
  if (is_curve_edit) {
    GP_EDITABLE_STROKES_BEGIN (gp_iter, C, gpl, gps) {
      if (gps->editcurve != NULL && gps->flag & GP_STROKE_SELECT) {
        bGPDcurve *editcurve = gps->editcurve;

        bool prev_sel = false;
        for (int i = 0; i < editcurve->tot_curve_points; i++) {
          bGPDcurve_point *gpc_pt = &editcurve->curve_points[i];
          BezTriple *bezt = &gpc_pt->bezt;
          if (gpc_pt->flag & GP_CURVE_POINT_SELECT) {
            /* selected point - just set flag for next point */
            prev_sel = true;
          }
          else {
            /* unselected point - expand selection if previous was selected... */
            if (prev_sel) {
              gpc_pt->flag |= GP_CURVE_POINT_SELECT;
              BEZT_SEL_ALL(bezt);
              changed = true;
            }
            prev_sel = false;
          }
        }

        prev_sel = false;
        for (int i = editcurve->tot_curve_points - 1; i >= 0; i--) {
          bGPDcurve_point *gpc_pt = &editcurve->curve_points[i];
          BezTriple *bezt = &gpc_pt->bezt;
          if (gpc_pt->flag & GP_CURVE_POINT_SELECT) {
            prev_sel = true;
          }
          else {
            /* unselected point - expand selection if previous was selected... */
            if (prev_sel) {
              gpc_pt->flag |= GP_CURVE_POINT_SELECT;
              BEZT_SEL_ALL(bezt);
              changed = true;
            }
            prev_sel = false;
          }
        }
      }
    }
    GP_EDITABLE_STROKES_END(gp_iter);
  }
  else {
    CTX_DATA_BEGIN (C, bGPDstroke *, gps, editable_gpencil_strokes) {
      if (gps->flag & DPEN_STROKE_SELECT) {
        bGPDspoint *pt;
        int i;
        bool prev_sel;

        /* First Pass: Go in forward order,
         * expanding selection if previous was selected (pre changes).
         * - This pass covers the "after" edges of selection islands
         */
        prev_sel = false;
        for (i = 0, pt = dps->points; i < dps->totpoints; i++, pt++) {
          if (pt->flag & DPEN_SPOINT_SELECT) {
            /* selected point - just set flag for next point */
            prev_sel = true;
          }
          else {
            /* unselected point - expand selection if previous was selected... */
            if (prev_sel) {
              pt->flag |= DPEN_SPOINT_SELECT;
              changed = true;
            }
            prev_sel = false;
          }
        }

        /* Second Pass: Go in reverse order, doing the same as before (except in opposite order)
         * - This pass covers the "before" edges of selection islands
         */
        prev_sel = false;
        for (pt -= 1; i > 0; i--, pt--) {
          if (pt->flag & DPEN_SPOINT_SELECT) {
            prev_sel = true;
          }
          else {
            /* unselected point - expand selection if previous was selected... */
            if (prev_sel) {
              pt->flag |= DPEN_SPOINT_SELECT;
              changed = true;
            }
            prev_sel = false;
          }
        }
      }
    }
    CTX_DATA_END;
  }

  if (changed) {
    /* updates */
    DEG_id_tag_update(&dpd->id, ID_RECALC_GEOMETRY);

    /* copy on write tag is needed, or else no refresh happens */
    DEG_id_tag_update(&dpd->id, ID_RECALC_COPY_ON_WRITE);

    wm_event_add_notifier(C, NC_DPEN | NA_SELECTED, NULL);
    wm_event_add_notifier(C, NC_GEOM | ND_SELECT, NULL);
  }

  return OP_FINISHED;
}
