#include "MEM_guardedalloc.h"

#include "DNA_gpencil_types.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_string.h"

#include "BLT_translation.h"

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

#include "UI_interface.h"

#include "ED_gpencil.h"
#include "ED_numinput.h"
#include "ED_screen.h"
#include "ED_space_api.h"
#include "ED_util.h"
#include "ED_view3d.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "gpencil_intern.h"

typedef struct GpUvData {
  Object *ob;
  bGPdata *gpd;
  GP_SpaceConversion gsc;
  float ob_scale;

  float initial_length;
  float initial_transform[2];
  float pixel_size; /* use when mouse input is interpreted as spatial distance */

  /* Arrays of original loc/rot/scale by stroke. */
  float (*array_loc)[2];
  float *array_rot;
  float *array_scale;

  /* modal only */
  float mcenter[2];
  float mouse[2];

  /** Vector with the original orientation. */
  float vinit_rotation[2];

  void *draw_handle_pixel;
} GpUvData;

enum {
  GP_UV_ROTATE = 0,
  GP_UV_TRANSLATE = 1,
  GP_UV_SCALE = 2,
  GP_UV_ALL = 3,
};

#define SMOOTH_FACTOR 0.3f

static void gpencil_uv_transform_update_header(wmOperator *op, bContext *C)
{
  const int mode = RNA_enum_get(op->ptr, "mode");
  const char *str = TIP_("Confirm: Enter/LClick, Cancel: (Esc/RClick) %s");

  char msg[UI_MAX_DRAW_STR];
  ScrArea *area = CTX_wm_area(C);

  if (area) {
    char flts_str[NUM_STR_REP_LEN * 2];
    switch (mode) {
      case GP_UV_TRANSLATE: {
        float location[2];
        RNA_float_get_array(op->ptr, "location", location);
        BLI_snprintf(
            flts_str, NUM_STR_REP_LEN, ", Translation: (%f, %f)", location[0], location[1]);
        break;
      }
      case GP_UV_ROTATE: {
        BLI_snprintf(flts_str,
                     NUM_STR_REP_LEN,
                     ", Rotation: %f",
                     RAD2DEG(RNA_float_get(op->ptr, "rotation")));
        break;
      }
      case GP_UV_SCALE: {
        BLI_snprintf(
            flts_str, NUM_STR_REP_LEN, ", Scale: %f", RAD2DEG(RNA_float_get(op->ptr, "scale")));
        break;
      }
      default:
        break;
    }
    BLI_snprintf(msg, sizeof(msg), str, flts_str, flts_str + NUM_STR_REP_LEN);
    ED_area_status_text(area, msg);
  }
}

/* Helper: Get stroke center. */
static void gpencil_stroke_center(bGPDstroke *gps, float r_center[3])
{
  bGPDspoint *pt;
  int i;

  zero_v3(r_center);
  if (gps->totpoints > 0) {
    for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
      add_v3_v3(r_center, &pt->x);
    }

    mul_v3_fl(r_center, 1.0f / gps->totpoints);
  }
}

static bool gpencil_uv_transform_init(bContext *C, wmOperator *op)
{
  GpUvData *opdata;

  op->customdata = opdata = MEM_mallocN(sizeof(GpUvData), __func__);

  opdata->ob = CTX_data_active_object(C);
  opdata->gpd = (bGPdata *)opdata->ob->data;
  gpencil_point_conversion_init(C, &opdata->gsc);
  opdata->array_loc = NULL;
  opdata->array_rot = NULL;
  opdata->array_scale = NULL;
  opdata->ob_scale = mat4_to_scale(opdata->ob->obmat);

  opdata->vinit_rotation[0] = 1.0f;
  opdata->vinit_rotation[1] = 0.0f;

  ARegion *region = CTX_wm_region(C);

  opdata->draw_handle_pixel = ED_region_draw_cb_activate(
      region->type, ED_region_draw_mouse_line_cb, opdata->mcenter, REGION_DRAW_POST_PIXEL);

  /* Calc selected strokes center. */
  zero_v2(opdata->mcenter);
  float center[3] = {0.0f};
  int i = 0;
  /* Need use evaluated to get the viewport final position. */
  GP_EVALUATED_STROKES_BEGIN (gpstroke_iter, C, gpl, gps) {
    if (gps->flag & GP_STROKE_SELECT) {
      float r_center[3];
      gpencil_stroke_center(gps, r_center);
      /* Add object location. */
      add_v3_v3(r_center, opdata->ob->obmat[3]);
      add_v3_v3(center, r_center);
      i++;
    }
  }
  GP_EVALUATED_STROKES_END(gpstroke_iter);

  if (i > 0) {
    mul_v3_fl(center, 1.0f / i);
    /* Create arrays to save all transformations. */
    opdata->array_loc = MEM_calloc_arrayN(i, sizeof(float[2]), __func__);
    opdata->array_rot = MEM_calloc_arrayN(i, sizeof(float), __func__);
    opdata->array_scale = MEM_calloc_arrayN(i, sizeof(float), __func__);
    i = 0;
    GP_EDITABLE_STROKES_BEGIN (gpstroke_iter, C, gpl, gps) {
      if (gps->flag & GP_STROKE_SELECT) {
        copy_v2_v2(opdata->array_loc[i], gps->uv_translation);
        opdata->array_rot[i] = gps->uv_rotation;
        opdata->array_scale[i] = gps->uv_scale;
        i++;
      }
    }
    GP_EDITABLE_STROKES_END(gpstroke_iter);
  }
  /* Convert to 2D. */
  gpencil_point_3d_to_xy(&opdata->gsc, GP_STROKE_3DSPACE, center, opdata->mcenter);

  return true;
}

static void gpencil_uv_transform_exit(bContext *C, wmOperator *op)
{
  GpUvData *opdata;
  ScrArea *area = CTX_wm_area(C);

  opdata = op->customdata;

  ARegion *region = CTX_wm_region(C);

  ED_region_draw_cb_exit(region->type, opdata->draw_handle_pixel);

  WM_cursor_set(CTX_wm_window(C), WM_CURSOR_DEFAULT);

  if (area) {
    ED_area_status_text(area, NULL);
  }
  WM_main_add_notifier(NC_GEOM | ND_DATA, NULL);

  MEM_SAFE_FREE(opdata->array_loc);
  MEM_SAFE_FREE(opdata->array_rot);
  MEM_SAFE_FREE(opdata->array_scale);
  MEM_SAFE_FREE(op->customdata);
}

static void gpencil_transform_fill_cancel(bContext *C, wmOperator *op)
{
  GpUvData *opdata = op->customdata;
  UNUSED_VARS(opdata);

  gpencil_uv_transform_exit(C, op);

  /* need to force redisplay or we may still view the modified result */
  ED_region_tag_redraw(CTX_wm_region(C));
}

static bool gpencil_uv_transform_calc(bContext *C, wmOperator *op)
{
  const int mode = RNA_enum_get(op->ptr, "mode");
  GpUvData *opdata = op->customdata;
  bGPdata *gpd = opdata->gpd;

  bool changed = false;
  /* Get actual vector. */
  float vr[2];
  float mdiff[2];

  sub_v2_v2v2(vr, opdata->mouse, opdata->mcenter);
  normalize_v2(vr);

  float uv_rotation = angle_signed_v2v2(opdata->vinit_rotation, vr);

  int i = 0;

  /* Translate. */
  if (mode == GP_UV_TRANSLATE) {

    mdiff[0] = opdata->mouse[0] - opdata->initial_transform[0];
    /* Y axis is inverted. */
    mdiff[1] = (opdata->mouse[1] - opdata->initial_transform[1]) * -1.0f;

    /* Apply a big amount of smooth always for translate to get smooth result. */
    mul_v2_fl(mdiff, 0.002f);
    RNA_float_set_array(op->ptr, "location", mdiff);

    GP_EDITABLE_STROKES_BEGIN (gpstroke_iter, C, gpl, gps) {
      if (gps->flag & GP_STROKE_SELECT) {

        sub_v2_v2v2(gps->uv_translation, opdata->array_loc[i], mdiff);
        changed = true;

        /* Calc geometry data. */
        BKE_gpencil_stroke_geometry_update(gpd, gps);
        i++;
      }
    }
    GP_EDITABLE_STROKES_END(gpstroke_iter);
  }

  /* Rotate. */
  if (mode == GP_UV_ROTATE) {
    changed |= (bool)(uv_rotation != 0.0f);
    RNA_float_set(op->ptr, "rotation", uv_rotation);

    if (changed) {
      GP_EDITABLE_STROKES_BEGIN (gpstroke_iter, C, gpl, gps) {
        if (gps->flag & GP_STROKE_SELECT) {
          gps->uv_rotation = opdata->array_rot[i] - uv_rotation;

          /* Calc geometry data. */
          BKE_gpencil_stroke_geometry_update(gpd, gps);
          i++;
        }
      }
      GP_EDITABLE_STROKES_END(gpstroke_iter);
    }
  }

  /* Scale. */
  if (mode == GP_UV_SCALE) {
    mdiff[0] = opdata->mcenter[0] - opdata->mouse[0];
    mdiff[1] = opdata->mcenter[1] - opdata->mouse[1];
    float scale = ((len_v2(mdiff) - opdata->initial_length) * opdata->pixel_size) /
                  opdata->ob_scale;

    scale *= SMOOTH_FACTOR;
    RNA_float_set(op->ptr, "scale", scale);

    changed |= (bool)(scale != 0.0f);

    if (changed) {
      GP_EDITABLE_STROKES_BEGIN (gpstroke_iter, C, gpl, gps) {
        if (gps->flag & GP_STROKE_SELECT) {
          gps->uv_scale = opdata->array_scale[i] + scale;
          /* Calc geometry data. */
          BKE_gpencil_stroke_geometry_update(gpd, gps);
          i++;
        }
      }
      GP_EDITABLE_STROKES_END(gpstroke_iter);
    }
  }

  if (changed) {
    /* Update cursor line. */
    DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);
    WM_main_add_notifier(NC_GEOM | ND_DATA, NULL);
    WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  }

  return changed;
}

static bool gpencil_transform_fill_poll(bContext *C)
{
  if (!ED_operator_view3d_active(C)) {
    return false;
  }
  Object *ob = CTX_data_active_object(C);
  if ((ob == NULL) || (ob->type != OB_GPENCIL)) {
    return false;
  }
  bGPdata *gpd = (bGPdata *)ob->data;
  if (dpd == NULL) {
    return false;
  }

  bGPDlayer *dpl = dune_dpen_layer_active_get(dpd);

  if ((gpl == NULL) || (ob->mode != OB_MODE_EDIT_DPEN)) {
    return false;
  }

  return true;
}

static int dpen_transform_fill_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  RegionView3D *rv3d = ctx_wm_region_view3d(C);
  float mlen[2];
  float center_3d[3];

  if (!dpen_uv_transform_init(C, op)) {
    return OP_CANCELLED;
  }

  DpUvData *opdata = op->customdata;
  /* initialize mouse values */
  opdata->mouse[0] = event->mval[0];
  opdata->mouse[1] = event->mval[1];

  copy_v3_v3(center_3d, opdata->ob->loc);
  mlen[0] = event->mval[0] - opdata->mcenter[0];
  mlen[1] = event->mval[1] - opdata->mcenter[1];
  opdata->initial_length = len_v2(mlen);

  /* Consider initial offset as zero position. */
  copy_v2fl_v2i(opdata->initial_transform, event->mval);

  /* Consider initial position as the orientation vector. */
  const int mode = api_enum_get(op->ptr, "mode");
  if (mode == DPEN_UV_ROTATE) {
    opdata->vinit_rotation[0] = mlen[0];
    opdata->vinit_rotation[1] = mlen[1];
    normalize_v2(opdata->vinit_rotation);
  }

  opdata->pixel_size = rv3d ? ed_view3d_pixel_size(rv3d, center_3d) : 1.0f;

  dpen_uv_transform_calc(C, op);

  dpen_uv_transform_update_header(op, C);
  wm_cursor_set(ctx_wm_window(C), WM_CURSOR_EW_ARROW);

  wm_event_add_modal_handler(C, op);
  return OP_RUNNING_MODAL;
}

static int dpen_transform_fill_modal(dContext *C, wmOperator *op, const wmEvent *event)
{
  DpUvData *opdata = op->customdata;

  switch (event->type) {
    case EVT_ESCKEY:
    case RIGHTMOUSE: {
      dpen_transform_fill_cancel(C, op);
      return OP_CANCELLED;
    }
    case MOUSEMOVE: {
      opdata->mouse[0] = event->mval[0];
      opdata->mouse[1] = event->mval[1];

      if (dpen_uv_transform_calc(C, op)) {
        dpen_uv_transform_update_header(op, C);
      }
      else {
        gpencil_transform_fill_cancel(C, op);
        return OP_CANCELLED;
      }
      break;
    }
    case LEFTMOUSE:
    case EVT_PADENTER:
    case EVT_RETKEY: {
      if ((event->val == KM_PRESS) ||
          ((event->val == KM_RELEASE) && RNA_boolean_get(op->ptr, "release_confirm"))) {
        dpen_uv_transform_calc(C, op);
        dpen_uv_transform_exit(C, op);
        return OP_FINISHED;
      }
      break;
    }
  }

  return OPERATOR_RUNNING_MODAL;
}

void DPEN_OT_transform_fill(wmOperatorType *ot)
{
  static const EnumPropItem uv_mode[] = {
      {DPEN_UV_TRANSLATE, "TRANSLATE", 0, "Translate", ""},
      {DPEN_UV_ROTATE, "ROTATE", 0, "Rotate", ""},
      {DPEN_UV_SCALE, "SCALE", 0, "Scale", ""},
      {0, NULL, 0, NULL, NULL},
  };

  ApiProp *prop;

  /* identifiers */
  ot->name = "Transform Stroke Fill";
  ot->idname = "DPEN_OT_transform_fill";
  ot->description = "Transform dune pen stroke fill";

  /* api callbacks */
  ot->invoke = dpen_transform_fill_invoke;
  ot->modal = dpen_transform_fill_modal;
  ot->cancel = dpen_transform_fill_cancel;
  ot->poll = dpen_transform_fill_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_GRAB_CURSOR_XY | OPTYPE_BLOCKING;

  /* properties */
  ot->prop = api_def_enum(ot->srna, "mode", uv_mode, GP_UV_ROTATE, "Mode", "");

  prop = api_def_float_vector(
      ot->srna, "location", 2, NULL, -FLT_MAX, FLT_MAX, "Location", "", -FLT_MAX, FLT_MAX);
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  prop = api_def_float_rotation(ot->srna,
                                "rotation",
                                0,
                                NULL,
                                DEG2RADF(-360.0f),
                                DEG2RADF(360.0f),
                                "Rotation",
                                "",
                                DEG2RADF(-360.0f),
                                DEG2RADF(360.0f));
  api_def_prop_float_default(prop, DEG2RADF(0.0f));
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  prop = api_def_float(ot->srna, "scale", 1.0f, 0.001f, 100.0f, "Scale", "", 0.001f, 100.0f);
  api_def_prop_float_default(prop, 0.0f);
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  prop = RNA_def_boolean(ot->srna, "release_confirm", 0, "Confirm on Release", "");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
}

/* Clear UV transformations. */
static int dpen_reset_transform_fill_exec(dContext *C, wmOperator *op)
{
  const int mode = api_enum_get(op->ptr, "mode");
  Object *ob = ctx_data_active_object(C);
  DPenData *gpd = (DPenData *)ob->data;
  bool changed = false;

  /* Loop all selected strokes and reset. */
  DPEN_EDITABLE_STROKES_BEGIN (dpstroke_iter, C, dpl, dps) {
    if (dps->flag & DPEN_STROKE_SELECT) {
      if (ELEM(mode, DPEN_UV_TRANSLATE, DPEN_UV_ALL)) {
        zero_v2(gps->uv_translation);
      }
      if (ELEM(mode, DPEN_UV_ROTATE, DPEN_UV_ALL)) {
        dps->uv_rotation = 0.0f;
      }
      if (ELEM(mode, DPEN_UV_SCALE, DPEN_UV_ALL)) {
        dps->uv_scale = 1.0f;
      }
      /* Calc geometry data. */
      dune_dpen_stroke_geometry_update(gpd, gps);
      changed = true;
    }
  }
  DPEN_EDITABLE_STROKES_END(dpstroke_iter);

  /* notifiers */
  if (changed) {
    DEG_id_tag_update(&dpd->id, ID_RECALC_GEOMETRY);
    wm_event_add_notifier(C, NC_DPEN | ND_DATA | NA_EDITED, NULL);
  }

  return OP_FINISHED;
}

void DPEN_OT_reset_transform_fill(wmOperatorType *ot)
{
  static const EnumPropItem uv_clear_mode[] = {
      {DPEN_UV_ALL, "ALL", 0, "All", ""},
      {DPEN_UV_TRANSLATE, "TRANSLATE", 0, "Translate", ""},
      {DPEN_UV_ROTATE, "ROTATE", 0, "Rotate", ""},
      {DPEN_UV_SCALE, "SCALE", 0, "Scale", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Reset Fill Transformations";
  ot->idname = "DPEN_OT_reset_transform_fill";
  ot->description = "Reset any UV transformation and back to default values";

  /* callbacks */
  ot->exec = dpen_reset_transform_fill_exec;
  ot->poll = dpen_transform_fill_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = api_def_enum(ot->srna, "mode", uv_clear_mode, GP_UV_ALL, "Mode", "");
}
