#include <cmath>

#include "mem_guardedalloc.h"

#include "lib_list.h"
#include "lib_rect.h"

#include "types_anim.h"
#include "types_scene.h"
#include "types_space.h"

#include "api_access.hh"
#include "api_define.hh"

#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_nla.h"

#include "ui.hh"
#include "ui_view2d.hh"

#include "ed_anim_api.hh"
#include "ed_markers.hh"
#include "ed_screen.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "graph_intern.h"

/* Calc Range */

void get_graph_keyframe_extents(AnimCxt *ac,
                                float *xmin,
                                float *xmax,
                                float *ymin,
                                float *ymax,
                                const bool do_sel_only,
                                const bool include_handles)
{
  Scene *scene = ac->scene;

  List anim_data = {nullptr, nullptr};
  int filter;

  /* Get data to filter, from Dopesheet. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FCURVESONLY |
            ANIMFILTER_NODUPLIS);
  if (U.anim_flag & USER_ANIM_ONLY_SHOW_SEL_CURVE_KEYS) {
    filter |= ANIMFILTER_SEL;
  }

  anim_animdata_filter(
      ac, &anim_data, eAnimFilterFlags(filter), ac->data, eAnimContTypes(ac->datatype));

  /* Set large vals to init vals that will be easy to override. */
  if (xmin) {
    *xmin = 999999999.0f;
  }
  if (xmax) {
    *xmax = -999999999.0f;
  }
  if (ymin) {
    *ymin = 999999999.0f;
  }
  if (ymax) {
    *ymax = -999999999.0f;
  }

  /* Check if any channels to set range with. */
  if (anim_data.first) {
    bool foundBounds = false;

    /* Go through channels, finding max extents. */
    LIST_FOREACH (AnimListElem *, ale, &anim_data) {
      AnimData *adt = anim_nla_mapping_get(ac, ale);
      FCurve *fcu = (FCurve *)ale->key_data;
      rctf bounds;
      float unitFac, offset;

      /* Get range. */
      if (dune_fcurve_calc_bounds(fcu, do_sel_only, include_handles, nullptr, &bounds)) {
        short mapping_flag = anim_get_normalization_flags(ac->sl);

        /* Apply NLA scaling. */
        if (adt) {
          bounds.xmin = dune_nla_tweakedit_remap(adt, bounds.xmin, NLATIME_CONVERT_MAP);
          bounds.xmax = dune_nla_tweakedit_remap(adt, bounds.xmax, NLATIME_CONVERT_MAP);
        }

        /* Apply unit corrections. */
        unitFac = anim_unit_mapping_get_factor(ac->scene, ale->id, fcu, mapping_flag, &offset);
        bounds.ymin += offset;
        bounds.ymax += offset;
        bounds.ymin *= unitFac;
        bounds.ymax *= unitFac;

        /* Try set cur using these vals if they're more extreme than prev set vals. */
        if ((xmin) && (bounds.xmin < *xmin)) {
          *xmin = bounds.xmin;
        }
        if ((xmax) && (bounds.xmax > *xmax)) {
          *xmax = bounds.xmax;
        }
        if ((ymin) && (bounds.ymin < *ymin)) {
          *ymin = bounds.ymin;
        }
        if ((ymax) && (bounds.ymax > *ymax)) {
          *ymax = bounds.ymax;
        }

        foundBounds = true;
      }
    }

    /* Ensure that the extents are not too extreme that view implodes. */
    if (foundBounds) {
      if ((xmin && xmax) && (fabsf(*xmax - *xmin) < 0.001f)) {
        *xmin -= 0.0005f;
        *xmax += 0.0005f;
      }
      if ((ymin && ymax) && (fabsf(*ymax - *ymin) < 0.001f)) {
        *ymax -= 0.0005f;
        *ymax += 0.0005f;
      }
    }
    else {
      if (xmin) {
        *xmin = float(PSFRA);
      }
      if (xmax) {
        *xmax = float(PEFRA);
      }
      if (ymin) {
        *ymin = -5;
      }
      if (ymax) {
        *ymax = 5;
      }
    }

    /* Free memory. */
    anim_animdata_freelist(&anim_data);
  }
  else {
    /* Set default range. */
    if (ac->scene) {
      if (xmin) {
        *xmin = float(PSFRA);
      }
      if (xmax) {
        *xmax = float(PEFRA);
      }
    }
    else {
      if (xmin) {
        *xmin = -5;
      }
      if (xmax) {
        *xmax = 100;
      }
    }

    if (ymin) {
      *ymin = -5;
    }
    if (ymax) {
      *ymax = 5;
    }
  }
}

/* Automatic Preview-Range Op */
static int graphkeys_previewrange_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;
  Scene *scene;
  float min, max;

  /* Get ed data. */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }
  if (ac.scene == nullptr) {
    return OP_CANCELLED;
  }

  scene = ac.scene;

  /* Set the range directly. */
  get_graph_keyframe_extents(&ac, &min, &max, nullptr, nullptr, true, false);
  scene->r.flag |= SCER_PRV_RANGE;
  scene->r.psfra = round_fl_to_int(min);
  scene->r.pefra = round_fl_to_int(max);

  /* Set notifier that things have changed. */
  /* Err... there's nothing for frame ranges yet, but this should do fine too. */
  win_ev_add_notifier(C, NC_SCENE | ND_FRAME, ac.scene);

  return OP_FINISHED;
}

void GRAPH_OT_previewrange_set(WinOpType *ot)
{
  /* Ids */
  ot->name = "Set Preview Range to Sel";
  ot->idname = "GRAPH_OT_previewrange_set";
  ot->description = "Set Preview Range based on range of sel keyframes";

  /* api cbs */
  ot->ex = graphkeys_previewrange_ex;
  /* unchecked poll to get F-samples working too, but makes mod dmg trickier. */
  ot->poll = ed_op_graphedit_active;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}


/* View-All Op */
static int graphkeys_viewall(Cxt *C,
                             const bool do_sel_only,
                             const bool include_handles,
                             const int smooth_viewtx)
{
  AnimCxt ac;
  rctf cur_new;

  /* Get editor data. */
  if (animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* Set horizontal range w an extra offset so that the extreme keys will be in view. */
  get_graph_keyframe_extents(&ac,
                             &cur_new.xmin,
                             &cur_new.xmax,
                             &cur_new.ymin,
                             &cur_new.ymax,
                             do_sel_only,
                             include_handles);

  /* Give some more space at the borders. */
  lib_rctf_scale(&cur_new, 1.1f);

  /* Take rgns into account, that could block the view.
   * Marker rgn is supposed to be larger than the scroll-bar, so prioritize it. */
  float pad_top = UI_TIME_SCRUB_MARGIN_Y;
  float pad_bottom = lib_list_is_empty(ed_cxt_get_markers(C)) ? V2D_SCROLL_HANDLE_HEIGHT :
                                                                UI_MARKER_MARGIN_Y;
  lib_rctf_pad_y(&cur_new, ac.rgn->winy, pad_bottom, pad_top);

  ui_view2d_smooth_view(C, ac.rgn, &cur_new, smooth_viewtx);
  return OP_FINISHED;
}

static int graphkeys_viewall_ex(Cxt *C, WinOp *op)
{
  const bool include_handles = api_bool_get(op->ptr, "include_handles");
  const int smooth_viewtx = win_op_smooth_viewtx_get(op);

  /* Whole range */
  return graphkeys_viewall(C, false, include_handles, smooth_viewtx);
}

static int graphkeys_view_sel_ex(Cxt *C, WinOp *op)
{
  const bool include_handles = api_bool_get(op->ptr, "include_handles");
  const int smooth_viewtx = win_op_smooth_viewtx_get(op);

  /* Only sel */
  return graphkeys_viewall(C, true, include_handles, smooth_viewtx);
}

void GRAPH_OT_view_all(WinOpType *ot)
{
  /* Ids */
  ot->name = "Frame All";
  ot->idname = "GRAPH_OT_view_all";
  ot->description = "Reset viewable area to show full keyframe range";

  /* API cbs */
  ot->ex = graphkeys_viewall_ex;
  /* Unchecked poll to get F-samples working too but makes mod dmg trickier */
  ot->poll = ed_op_graphedit_active;

  /* Flags */
  ot->flag = 0;

  /* Props */
  ot->prop = api_def_bool(ot->sapi,
                          "include_handles",
                          true,
                          "Include Handles",
                          "Include handles of keyframes when calc extents");
}

void GRAPH_OT_view_sel(WinOpType *ot)
{
  /* Ids */
  ot->name = "Frame Sel";
  ot->idname = "GRAPH_OT_view_sel";
  ot->description = "Reset viewable area to show sel keyframe range";

  /* api cbs */
  ot->ex = graphkeys_view_sel_ex;
  /* Unchecked poll to get F-samples working too, but makes mod dmg trickier. */
  ot->poll = ed_op_graphedit_active;

  /* Flags */
  ot->flag = 0;

  /* Props */
  ot->prop = api_def_bool(ot->sapi,
                          "include_handles",
                          true,
                          "Include Handles",
                          "Include handles of keyframes when calc extents");
}

/* View Frame Op */
static int graphkeys_view_frame_ex(Cxt *C, WinOp *op)
{
  const int smooth_viewtx = win_op_smooth_viewtx_get(op);
  anim_center_frame(C, smooth_viewtx);
  return OP_FINISHED;
}

void GRAPH_OT_view_frame(WinOpType *ot)
{
  /* Ids */
  ot->name = "Go to Current Frame";
  ot->idname = "GRAPH_OT_view_frame";
  ot->description = "Move the view to the current frame";

  /* API cbs */
  ot->ex = graphkeys_view_frame_ex;
  ot->poll = ed_op_graphedit_active;

  /* Flags */
  ot->flag = 0;
}

/* Create Ghost-Curves Op
 * This op samples the data of the selected F-Curves to F-Points, storing them
 * as 'ghost curves' in the active Graph Editor */

/* Bake each F-Curve into a set of samples, and store as a ghost curve. */
static void create_ghost_curves(AnimCxt *ac, int start, int end)
{
  SpaceGraph *sipo = (SpaceGraph *)ac->sl;
  List anim_data = {nullptr, nullptr};
  int filter;

  /* Free existing ghost curves. */
  dune_fcurves_free(&sipo->runtime.ghost_curves);

  /* Sanity check. */
  if (start >= end) {
    printf("Error: Frame range for Ghost F-Curve creation is inappropriate\n");
    return;
  }

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FCURVESONLY |
            ANIMFILTER_SEL | ANIMFILTER_NODUPLIS);
  anim_animdata_filter(
      ac, &anim_data, eAnimFilterFlags(filter), ac->data, eAnimContTypes(ac->datatype));

  /* Loop through filtered data and add keys between sel keyframes on every frame. */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    FCurve *fcu = (FCurve *)ale->key_data;
    FCurve *gcu = dune_fcurve_create();
    AnimData *adt = anim_nla_mapping_get(ac, ale);
    ChannelDriver *driver = fcu->driver;
    FPoint *fpt;
    float unitFac, offset;
    int cfra;
    short mapping_flag = anim_get_normalization_flags(ac->sl);

    /* Disable driver so that it don't muck up the sampling process. */
    fcu->driver = nullptr;

    /* Calculate unit-mapping factor. */
    unitFac = anim_unit_mapping_get_factor(ac->scene, ale->id, fcu, mapping_flag, &offset);

    /* Create samples but store them in a new curve.
     * We cannot use fcurve_store_samples().
     * That will only overwrite the original curve. */
    gcu->fpt = fpt = static_cast<FPoint *>(
        mem_calloc(sizeof(FPoint) * (end - start + 1), "Ghost FPoint Samples"));
    gcu->totvert = end - start + 1;

    /* Use the sampling cb at 1-frame intervals from start to end frames. */
    for (cfra = start; cfra <= end; cfra++, fpt++) {
      float cfrae = dune_nla_tweakedit_remap(adt, cfra, NLATIME_CONVERT_UNMAP);

      fpt->vec[0] = cfrae;
      fpt->vec[1] = (fcurve_samplingcb_evalcurve(fcu, nullptr, cfrae) + offset) * unitFac;
    }

    /* Set color of ghost curve
     * make the color slightly darker. */
    gcu->color[0] = fcu->color[0] - 0.07f;
    gcu->color[1] = fcu->color[1] - 0.07f;
    gcu->color[2] = fcu->color[2] - 0.07f;

    /* Store new ghost curve. */
    lib_addtail(&sipo->runtime.ghost_curves, gcu);

    /* Restore driver. */
    fcu->driver = driver;
  }

  /* Admin and redrws. */
  anim_animdata_freelist(&anim_data);
}

static int graphkeys_create_ghostcurves_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;
  View2D *v2d;
  int start, end;

  /* Get editor data. */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* Ghost curves are snapshots of the visible portions of the curves,
   * so set range to be the visible range. */
  v2d = &ac.rgn->v2d;
  start = int(v2d->cur.xmin);
  end = int(v2d->cur.xmax);

  /* Bake sel curves into a ghost curve. */
  create_ghost_curves(&ac, start, end);

  /* Update this editor only. */
  ed_area_tag_redrw(cxt_win_area(C));

  return OP_FINISHED;
}

void GRAPH_OT_ghost_curves_create(WinOpType *ot)
{
  /* Ids */
  ot->name = "Create Ghost Curves";
  ot->idname = "GRAPH_OT_ghost_curves_create";
  ot->description =
      "Create snapshot (Ghosts) of selected F-Curves as background aid for active Graph Editor";

  /* API cbs */
  ot->ex = graphkeys_create_ghostcurves_ex;
  ot->poll = graphop_visible_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* TODO: add props for start/end frames */
}

/* Clear Ghost-Curves Op
 * Clears the 'ghost curves' for the active Graph Editor */
static int graphkeys_clear_ghostcurves_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;
  SpaceGraph *sipo;

  /* Get editor data. */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }
  sipo = (SpaceGraph *)ac.sl;

  /* If no ghost curves, don't do anything. */
  if (lib_list_is_empty(&sipo->runtime.ghost_curves)) {
    return OP_CANCELLED;
  }
  /* Free ghost curves. */
  dune_fcurves_free(&sipo->runtime.ghost_curves);

  /* Update this editor only. */
  ed_area_tag_redrw(cxt_win_area(C));

  return OP_FINISHED;
}

void GRAPH_OT_ghost_curves_clear(WinOpType *ot)
{
  /* Ids */
  ot->name = "Clear Ghost Curves";
  ot->idname = "GRAPH_OT_ghost_curves_clear";
  ot->description = "Clear F-Curve snapshots (Ghosts) for active Graph Editor";

  /* api cbs */
  ot->ex = graphkeys_clear_ghostcurves_ex;
  ot->poll = ed_op_graphedit_active;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
