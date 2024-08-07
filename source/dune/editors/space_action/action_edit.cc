#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "lib_dune.h"
#include "lib_map.hh"
#include "lib_utildefines.h"

#include "lang.h"

#include "graph.hh"

#include "types_anim.h"
#include "types_pen_legacy.h"
#include "types_key.h"
#include "types_mask.h"
#include "types_ob.h"
#include "types_scene.h"

#include "api_access.hh"
#include "api_define.hh"
#include "api_enum_types.hh"

#include "dune_action.h"
#include "dune_animsys.h"
#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_global.h"
#include "dune_pen_legacy.h"
#include "dune_pen.hh"
#include "dune_key.h"
#include "dune_nla.h"
#include "dune_report.h"

#include "ui_view2d.hh"

#include "anim_animdata.hh"
#include "anim_fcurve.hh"
#include "anim_keyframing.hh"
#include "ed_anim_api.hh"
#include "ed_pen_legacy.hh"
#include "ed_pen.hh"
#include "ed_keyframes_edit.hh"
#include "ed_keyframing.hh"
#include "ed_markers.hh"
#include "ed_mask.hh"
#include "ed_screen.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ui.hh"

#include "action_intern.hh"

/* -------------------------------------------------------------------- */
/* Pose Markers: Localize Markers */

/* ensure that there is:
 * 1) an active action editor
 * 2) that the mode will have an active action available
 * 3) that the set of markers being shown are the scene markers, not the list we're merging
 * 4) that there are some selected markers */
static bool act_markers_make_local_poll(Cxt *C)
{
  SpaceAction *sact = cxt_win_space_action(C);

  /* 1) */
  if (sact == nullptr) {
    return false;
  }

  /* 2) */
  if (elem(sact->mode, SACTCONT_ACTION, SACTCONT_SHAPEKEY) == 0) {
    return false;
  }
  if (sact->action == nullptr) {
    return false;
  }

  /* 3) */
  if (sact->flag & SACTION_POSEMARKERS_SHOW) {
    return false;
  }

  /* 4) */
  return ed_markers_get_first_selected(ed_cxt_get_markers(C)) != nullptr;
}

static int act_markers_make_local_ex(Cxt *C, WinOp * /*op*/)
{
  List *markers = ed_cxt_get_markers(C);

  SpaceAction *sact = cxt_win_space_action(C);
  Action *act = (sact) ? sact->action : nullptr;

  TimeMarker *marker, *markern = nullptr;

  /* sanity checks */
  if (elem(nullptr, markers, act)) {
    return OP_CANCELLED;
  }

  /* migrate markers */
  for (marker = static_cast<TimeMarker *>(markers->first); marker; marker = markern) {
    markern = marker->next;

    /* move if marker is selected */
    if (marker->flag & SEL) {
      lib_remlink(markers, marker);
      lib_addtail(&act->markers, marker);
    }
  }

  /* Now enable the "show pose-markers only" setting,
   * so that we can see that something did happen. */
  sact->flag |= SACTION_POSEMARKERS_SHOW;

  /* notifiers - both sets, as this change affects both */
  win_ev_add_notifier(C, NC_SCENE | ND_MARKERS, nullptr);
  win_ev_add_notifier(C, NC_ANIM | ND_MARKERS, nullptr);

  return OP_FINISHED;
}

void act_ot_markers_make_local(WinOpType *ot)
{
  /* ids */
  ot->name = "Make Markers Local";
  ot->idname = "act_ot_markers_make_local";
  ot->description = "Move sel scene markers to the active Action as local 'pose' markers";

  /* cbs */
  ot->ex = act_markers_make_local_ex;
  ot->poll = act_markers_make_local_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/* Calc Range */

/* Get the min/max keyframes. */
static bool get_keyframe_extents(bAnimContext *ac, float *min, float *max, const short onlySel)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilter_Flags filter;
  bool found = false;

  /* get data to filter, from Action or Dopesheet */
  /* What is sel doing here?!
   *      Commented it, was breaking things (eg. the "auto preview range" tool). */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE /*| ANIMFILTER_SEL */ |
            ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimCont_Types(ac->datatype));

  /* set large vals to try to override */
  *min = 999999999.0f;
  *max = -999999999.0f;

  /* check if any channels to set range with */
  if (anim_data.first) {
    /* go through channels, finding max extents */
    LIST_FOREACH (AnimListElem *, ale, &anim_data) {
      AnimData *adt = anim_nla_mapping_get(ac, ale);
      if (ale->datatype == ALE_PENFRAME) {
        PenDataLayer *pl = static_cast<PenDataLayer *>(ale->data);

        /* Find Pen-frame which is less than or equal to current-frame. */
        LIST_FOREACH (PenDataFrame *, pf, &pl->frames) {
          if (!onlySel || (gpf->flag & PEN_FRAME_SEL)) {
            const float framenum = float(gpf->framenum);
            *min = min_ff(*min, framenum);
            *max = max_ff(*max, framenum);
            found = true;
          }
        }
      }
      else if (ale->datatype == ALE_MASKLAY) {
        MaskLayer *masklay = static_cast<MaskLayer *>(ale->data);
        /* Find mask layer which is less than or equal to current-frame. */
        LIST_FOREACH (MaskLayerShape *, masklay_shape, &masklay->splines_shapes) {
          const float framenum = float(masklay_shape->frame);
          *min = min_ff(*min, framenum);
          *max = max_ff(*max, framenum);
          found = true;
        }
      }
      else if (ale->datatype == ALE_PEN_CEL) {
        const dune::pen::Layer &layer =
            static_cast<PenLayer *>(ale->data)->wrap();

        for (const auto [key, frame] : layer.frames().items()) {
          if (onlySel && !frame.is_sel()) {
            continue;
          }
          *min = min_ff(*min, float(key));
          *max = max_ff(*max, float(key));
          found = true;
        }
      }
      else {
        FCurve *fcu = (FCurve *)ale->key_data;
        float tmin, tmax;

        /* get range and apply necessary scaling before processing */
        if (dune_fcurve_calc_range(fcu, &tmin, &tmax, onlySel)) {

          if (adt) {
            tmin = dune_nla_tweakedit_remap(adt, tmin, NLATIME_CONVERT_MAP);
            tmax = dune_nla_tweakedit_remap(adt, tmax, NLATIME_CONVERT_MAP);
          }

          /* Try to set cur using these values,
           * if they're more extreme than previously set values. */
          *min = min_ff(*min, tmin);
          *max = max_ff(*max, tmax);
          found = true;
        }
      }
    }

    if (fabsf(*max - *min) < 0.001f) {
      *min -= 0.0005f;
      *max += 0.0005f;
    }

    /* free mem */
    anim_animdata_freelist(&anim_data);
  }
  else {
    /* set default range */
    if (ac->scene) {
      *min = float(ac->scene->r.sfra);
      *max = float(ac->scene->r.efra);
    }
    else {
      *min = -5;
      *max = 100;
    }
  }

  return found;
}

/* View: Automatic Preview-Range Op */
static int actkeys_previewrange_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;
  Scene *scene;
  float min, max;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }
  if (ac.scene == nullptr) {
    return OP_CANCELLED;
  }

  scene = ac.scene;

  /* set the range directly */
  get_keyframe_extents(&ac, &min, &max, true);
  scene->r.flag |= SCER_PRV_RANGE;
  scene->r.psfra = floorf(min);
  scene->r.pefra = ceilf(max);

  if (scene->r.psfra == scene->r.pefra) {
    scene->r.pefra = scene->r.psfra + 1;
  }

  /* set notifier that things have changed */
  /* err... there's nothing for frame ranges yet, but this should do fine too */
  win_ev_add_notifier(C, NC_SCENE | ND_FRAME, ac.scene);

  return OP_FINISHED;
}

void act_ot_previewrange_set(WinOpType *ot)
{
  /* ids */
  ot->name = "Set Preview Range to Sel";
  ot->idname = "act_ot_previewrange_set";
  ot->description = "Set Preview Range based on extents of sel Keyframes";

  /* api cbs */
  ot->ex = actkeys_previewrange_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/* View: All Op */

/* Find the extents of the active channel
 *
 * param r_min: Bottom y-extent of channel.
 * param r_max: Top y-extent of channel.
 * return Success of finding a selected channel */
static bool actkeys_channels_get_sel_extents(AnimCxt *ac, float *r_min, float *r_max)
{
  List anim_data = {nullptr, nullptr};
  AnimListElem *ale;
  eAnimFilter_Flags filter;

  /* Not bool bc want to prioritize 
  individual channels over expanders. */
  short found = 0;

  /* get all items - we need to do it this way */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_LIST_CHANNELS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimCont_Types(ac->datatype));

  /* loop through all channels, finding the first one that's selected */
  float ymax = anim_ui_get_first_channel_top(&ac->rgn->v2d);
  const float channel_step = anim_ui_get_channel_step();
  for (ale = static_cast<AnimListElem *>(anim_data.first); ale;
       ale = ale->next, ymax -= channel_step)
  {
    const AnimChannelType *acf = anim_channel_get_typeinfo(ale);

    /* must be sel... */
    if (acf && acf->has_setting(ac, ale, ACHANNEL_SETTING_SEL) &&
        anim_channel_setting_get(ac, ale, ACHANNEL_SETTING_SEL))
    {
      /* update best estimate */
      *r_min = ymax - anim_ui_get_channel_height();
      *r_max = ymax;

      /* is this high enough priority yet? */
      found = acf->channel_role;

      /* only stop our search when we've found an actual channel
       * - data-block expanders get less priority so that we don't abort premature  */
      if (found == ACHANNEL_ROLE_CHANNEL) {
        break;
      }
    }
  }

  /* free all tmp data */
  anim_animdata_freelist(&anim_data);

  return (found != 0);
}

static int actkeys_viewall(Cxt *C, const bool only_sel)
{
  AnimCxt ac;
  View2D *v2d;
  float extra, min, max;
  bool found;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }
  v2d = &ac.rgn->v2d;

  /* set the horizontal range, with an extra offset so that the extreme keys will be in view */
  found = get_keyframe_extents(&ac, &min, &max, only_sel);

  if (only_sel && (found == false)) {
    return OP_CANCELLED;
  }

  if (fabsf(max - min) < 1.0f) {
    /* Exception - center the single keyframe. */
    float xwidth = lib_rctf_size_x(&v2d->cur);

    v2d->cur.xmin = min - xwidth / 2.0f;
    v2d->cur.xmax = max + xwidth / 2.0f;
  }
  else {
    /* Normal case - stretch the two keyframes out to fill the space, with extra spacing */
    v2d->cur.xmin = min;
    v2d->cur.xmax = max;

    extra = 0.125f * lib_rctf_size_x(&v2d->cur);
    v2d->cur.xmin -= extra;
    v2d->cur.xmax += extra;
  }

  /* set vertical range */
  if (only_sel == false) {
    /* view all -> the summary channel is usually the shows everything,
     * and resides right at the top... */
    v2d->cur.ymax = 0.0f;
    v2d->cur.ymin = float(-lib_rcti_size_y(&v2d->mask));
  }
  else {
    /* locate 1st sel channel (or the active one), and frame those */
    float ymin = v2d->cur.ymin;
    float ymax = v2d->cur.ymax;

    if (actkeys_channels_get_sel_extents(&ac, &ymin, &ymax)) {
      /* recenter the view so that this range is in the middle */
      float ymid = (ymax - ymin) / 2.0f + ymin;
      float x_center;

      ui_view2d_center_get(v2d, &x_center, nullptr);
      ui_view2d_center_set(v2d, x_center, ymid);
    }
  }

  /* do View2D syncing */
  ui_view2d_sync(cxt_win_screen(C), cxt_win_area(C), v2d, V2D_LOCK_COPY);

  /* just redrw this view */
  ed_area_tag_redrw(cxt_win_area(C));

  return OP_FINISHED;
}

static int actkeys_viewall_exec(bContext *C, wmOperator * /*op*/)
{
  /* whole range */
  return actkeys_viewall(C, false);
}

static int actkeys_viewsel_exec(bContext *C, wmOperator * /*op*/)
{
  /* only sel */
  return actkeys_viewall(C, true);
}

void act_ot_view_all(WinOpType *ot)
{
  /* ids */
  ot->name = "Frame All";
  ot->idname = "act_ot_view_all";
  ot->description = "Reset viewable area to show full keyframe range";

  /* api cbs */
  ot->ex = actkeys_viewall_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = 0;
}

void act_ot_view_sel(WinOpType *ot)
{
  /* ids */
  ot->name = "Frame Sel";
  ot->idname = "action_ot_view_sel";
  ot->description = "Reset viewable area to show selected keyframes range";

  /* api cbs */
  ot->ex = actkeys_viewsel_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = 0;
}

/* -------------------------------------------------------------------- */
/* View: Frame Op */

static int actkeys_view_frame_exec(Cxt *C, WinOp *op)
{
  const int smooth_viewtx = win_op_smooth_viewtx_get(op);
  anim_center_frame(C, smooth_viewtx);

  return OP_FINISHED;
}

void act_ot_view_frame(WinOpType *ot)
{
  /* ids */
  ot->name = "Go to Current Frame";
  ot->idname = "action_ot_view_frame";
  ot->description = "Move the view to the current frame";

  /* api cbs */
  ot->ex = actkeys_view_frame_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = 0;
}

/* -------------------------------------------------------------------- */
/* Keyframes: Copy/Paste Op */

/* the backend code for this is shared with the graph editor */
static short copy_act_keys(AnimCxt *ac)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;
  short ok = 0;

  /* clear buf 1st */
  anim_fcurves_copybuf_free();

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FCURVESONLY |
            ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* copy keyframes */
  ok = copy_animedit_keys(ac, &anim_data);

  /* clean up */
  anim_animdata_freelist(&anim_data);

  return ok;
}

static eKeyPasteErr paste_action_keys(AnimCxt *ac,
                                      const eKeyPasteOffset offset_mode,
                                      const eKeyMergeMode merge_mode,
                                      bool flip)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  /* filter data
   * - 1st time we try to filter more strictly, allowing only sel channels
   *   to allow copying animation between channels
   * - 2nd time, we loosen things up if nothing was found the 1st time, allowing
   *   users to just paste keyframes back into the original curve again #31670.  */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_FCURVESONLY | ANIMFILTER_NODUPLIS);

  if (anim_animdata_filter(
          ac, &anim_data, filter | ANIMFILTER_SEL, ac->data, eAnimCont_Types(ac->datatype)) == 0)
  {
    anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimCont_Types(ac->datatype));
  }

  /* Val offset is always None bc the user cannot see the effect of it. */
  const eKeyPasteErr ok = paste_animedit_keys(
      ac, &anim_data, offset_mode, KEYFRAME_PASTE_VAL_OFFSET_NONE, merge_mode, flip);

  /* clean up */
  anim_animdata_freelist(&anim_data);

  return ok;
}

static int actkeys_copy_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* copy keyframes */
  if (ac.datatype == ANIMCONT_PEN) {
    if (ed_pen_anim_copybuf_copy(&ac) == false) {
      /* check if anything ended up in the buf */
      dune_report(op->reports, RPT_ERROR, "No keyframes copied to the internal clipboard");
      return OP_CANCELLED;
    }
  }
  else if (ac.datatype == ANIMCONT_MASK) {
    /* FIXME: support this case. */
    dune_report(op->reports, RPT_ERR, "Keyframe pasting is not available for mask mode");
    return OP_CANCELLED;
  }
  else {
    /* Both copy fn needs to be eval to account for mixed sel */
    const short kf_empty = copy_action_keys(&ac);
    const bool gpf_ok = ed_pen_anim_copybuf_copy(&ac);

    if (kf_empty && !gpf_ok) {
      dune_report(op->reports, RPT_ERR, "No keyframes copied to the internal clipboard");
      return OP_CANCELLED;
    }
  }

  return OP_FINISHED;
}

void act_ot_copy(WinOpType *ot)
{
  /* ids */
  ot->name = "Copy Keyframes";
  ot->idname = "act_ot_copy";
  ot->description = "Copy sel keyframes to the internal clipboard";

  /* api cbs */
  ot->ex = actkeys_copy_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int actkeys_paste_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;

  const eKeyPasteOffset offset_mode = eKeyPasteOffset(api_enum_get(op->ptr, "offset"));
  const eKeyMergeMode merge_mode = eKeyMergeMode(api_enum_get(op->ptr, "merge"));
  const bool flipped = api_bool_get(op->ptr, "flipped");

  bool gpframes_inbuf = false;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* ac.reports by default will be the global reports list, which won't show warnings */
  ac.reports = op->reports;

  /* paste keyframes */
  if (ac.datatype == ANIMCONT_PEN) {
    if (ed_pen_anim_copybuf_paste(&ac, offset_mode) == false) {
      dune_report(op->reports, RPT_ERROR, "No data in the internal clipboard to paste");
      return OP_CANCELLED;
    }
  }
  else if (ac.datatype == ANIMCONT_MASK) {
    /* FIXME: support this case. */
    dune_report(op->reports,
               RPT_ERR,
               "Keyframe pasting is not available for pen or mask mode");
    return OP_CANCELLED;
  }
  else {
    /* Both paste fn needs to be evaluated to account for mixed sel */
    const eKeyPasteErr kf_empty = paste_action_keys(&ac, offset_mode, merge_mode, flipped);
    /* non-zero return means an error occurred while trying to paste */
    pframes_inbuf = ed_pen_anim_copybuf_paste(&ac, offset_mode);

    /* Only report an err if nothing was pasted, i.e. when both FCurve and GPencil failed. */
    if (!pframes_inbuf) {
      switch (kf_empty) {
        case KEYFRAME_PASTE_OK:
          /* FCurve paste was ok, so it's all good. */
          break;

        case KEYFRAME_PASTE_NOWHERE_TO_PASTE:
          dune_report(op->reports, RPT_ERR, "No sel F-Curves to paste into");
          return OP_CANCELLED;

        case KEYFRAME_PASTE_NOTHING_TO_PASTE:
          dune_report(op->reports, RPT_ERR, "No data in the internal clipboard to paste");
          return OP_CANCELLED;
      }
    }
  }

  /* Pen needs extra update to refresh the added keyframes. */
  if (ac.datatype == ANIMCONT_PEN || pframes_inbuf) {
    win_ev_add_notifier(C, NC_PEN | ND_DATA, nullptr);
  }
  /* set notifier that keyframes have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_EDITED, nullptr);

  return OP_FINISHED;
}

static std::string actkeys_paste_description(Cxt * /*C*/,
                                             WinOpType * /*ot*/,
                                             ApiPtr *ptr)
{
  /* Custom description if the 'flipped' option is used. */
  if (api_bool_get(ptr, "flipped")) {
    return lib_strdup(TIP_("Paste keyframes from mirrored bones if they exist"));
  }

  /* Use the default description in the other cases. */
  return "";
}

void act_ot_paste(WinOpType *ot)
{
  ApiProp *prop;
  /* ids */
  ot->name = "Paste Keyframes";
  ot->idname = "act_ot_paste";
  ot->description =
      "Paste keyframes from the internal clipboard for the sel channels, starting on the "
      "current "
      "frame";

  /* api cbs */
  //  ot->invoke = win_op_props_popup; /* Better wait for action redo panel. */
  ot->get_description = actkeys_paste_description;
  ot->ex = actkeys_paste_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_enum(ot->sapi,
               "offset",
               api_enum_keyframe_paste_offset_items,
               KEYFRAME_PASTE_OFFSET_CFRA_START,
               "Offset",
               "Paste time offset of keys");
  api_def_enum(ot->sapi,
               "merge",
               api_enum_keyframe_paste_merge_items,
               KEYFRAME_PASTE_MERGE_MIX,
               "Type",
               "Method of merging pasted keys and existing");
  prop = api_def_bool(
      ot->sapi, "flipped", false, "Flipped", "Paste keyframes from mirrored bones if they exist");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* -------------------------------------------------------------------- */
/* Keyframes: Insert Op */

/* defines for insert keyframes tool */
static const EnumPropItem prop_actkeys_insertkey_types[] = {
    {1, "ALL", 0, "All Channels", ""},
    {2, "SEL", 0, "Only Sel Channels", ""},
    /* not in all cases. */
    {3, "GROUP", 0, "In Active Group", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static void insert_pen_key(AnimCxt *ac,
                           AnimListElem *ale,
                           const ePenGetFrame_Mode add_frame_mode,
                           PenData **pd_old)
{
  Scene *scene = ac->scene;
  PenData *pd = (PenData *)ale->id;
  PenLayer *pl = (PenLayer *)ale->data;
  dune_pen_layer_frame_get(pl, scene->r.cfra, add_frame_mode);
  /* Check if the pd changes to tag only once. */
  if (pd != *pd_old) {
    dune_pen_tag(pd);
    *pd_old = pd;
  }
}

static void insert_pen_key(AnimCxt *ac,
                           AnimListElem *ale,
                           const bool hold_prev)
{
  using namespace dune::pen;
  Layer *layer = static_cast<Layer *>(ale->data);
  Pen *pen = reinterpret_cast<Pen *>(ale->id);
  const int curr_frame_num = ac->scene->r.cfra;

  if (layer->frames().contains(curr_frame_num)) {
    return;
  }

  bool changed = false;
  if (hold_prev) {
    const FramesMapKey active_frame_num = layer->frame_key_at(curr_frame_num);
    if ((active_frame_num == -1) || layer->frames().lookup(active_frame_num).is_null()) {
      /* There is no active frame to hold to, or it's a null frame.
       * Thus insert a blank frame. */
      changed = pen->insert_blank_frame(
          *layer, curr_frame_num, 0, BEZT_KEYTYPE_KEYFRAME);
    }
    else {
      /* Dupl the active frame. */
      changed = pen->insert_dupl_frame(
          *layer, active_frame_num, curr_frame_num, false);
    }
  }
  else {
    /* Insert a blank frame. */
    changed = pen->insert_blank_frame(
        *layer, curr_frame_num, 0, BEZT_KEYTYPE_KEYFRAME);
  }

  if (changed) {
    graph_id_tag_update(&pen->id, ID_RECALC_GEO);
  }
}

static void insert_fcurve_key(AnimCxt *ac,
                              AnimListElem *ale,
                              const AnimEvalCxt anim_eval_cxt,
                              eInsertKeyFlags flag)
{
  FCurve *fcu = (FCurve *)ale->key_data;

  ReportList *reports = ac->reports;
  Scene *scene = ac->scene;
  ToolSettings *ts = scene->toolsettings;

  /* Read val from prop the F-Curve represents or from the curve only?
   * - ale->id != nullptr:
   * Typically this means: we have enough info to try resolving the path.
   *
   * - ale->owner != nullptr:
   *   If set, path may not be resolvable from the Id alone,
   *   so it's easier for now to read the F-Curve directly.
   *   (TODO: add the full-blown ApiPtr relative parsing case here...)
   */
  if (ale->id && !ale->owner) {
    dune::animrig::insert_keyframe(ac->main,
                                   reports,
                                   ale->id,
                                   nullptr,
                                   ((fcu->grp) ? (fcu->grp->name) : (nullptr)),
                                      fcu->api_path,
                                      fcu->arr_index,
                                      &anim_eval_cxt,
                                      eBezTripleKeyframeType(ts->keyframe_type),
                                      flag);
  }
  else {
    AnimData *adt = anim_nla_mapping_get(ac, ale);

    /* adjust current frame for NLA-scaling */
    float cfra = anim_eval_cxt.eval_time;
    if (adt) {
      cfra = dune_nla_tweakedit_remap(adt, cfra, NLATIME_CONVERT_UNMAP);
    }

    const float curval = eval_fcurve(fcu, cfra);
    dune::animrig::insert_vert_fcurve(
        fcu, cfra, curval, eBezTriple_KeyframeType(ts->keyframe_type), eInsertKeyFlags(0));
  }

  ale->update |= ANIM_UPDATE_DEFAULT;
}

/* this fn is responsible for inserting new keyframes */
static void insert_act_keys(AnimCxt *ac, short mode)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  Scene *scene = ac->scene;
  ToolSettings *ts = scene->toolsettings;
  eInsertKeyFlags flag;

  ePenGetFrame_Mode add_frame_mode;
  bPenData *pd_old = nullptr;

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  if (mode == 2) {
    filter |= ANIMFILTER_SEL;
  }
  else if (mode == 3) {
    filter |= ANIMFILTER_ACTGROUPED;
  }

  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* Init keyframing flag. */
  flag = anim_get_keyframing_flags(scene, true);

  /* PenLayers specific flags */
  if (ts->pen_flags & PEN_TOOL_FLAG_RETAIN_LAST) {
    add_frame_mode = PEN_GETFRAME_ADD_COPY;
  }
  else {
    add_frame_mode = PEN_GETFRAME_ADD_NEW;
  }
  const bool pen_hold_prev = ((ts->pen_flags & PEN_TOOL_FLAG_RETAIN_LAST) != 0);

  /* insert keyframes */
  const AnimEvalCxt anim_eval_cxt = dune_animsys_eval_cxt_construct(
      ac->graph, float(scene->r.cfra));
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    switch (ale->type) {
      case ANIMTYPE_PLAYER:
        insert_glpen_key(ac, ale, add_frame_mode, &pd_old);
        break;

      case ANIMTYPE_PEN_LAYER:
        insert_pen_key(ac, ale, pen_hold_prev);
        break;

      case ANIMTYPE_FCURVE:
        insert_fcurve_key(ac, ale, anim_eval_cxt, flag);
        break;

      default:
        lib_assert_msg(false, "Keys cannot be inserted into this anim type.");
    }
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_insertkey_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short mode;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  if (ac.datatype == ANIMCONT_MASK) {
    dune_report(op->reports, RPT_ERR, "Insert Keyframes is not yet implemented for this mode");
    return OP_CANCELLED;
  }

  /* what channels to affect? */
  mode = api_enum_get(op->ptr, "type");

  /* insert keyframes */
  insert_act_keys(&ac, mode);

  /* set notifier that keyframes have changed */
  if (ac.datatype == ANIMCONT_PEN) {
    win_ev_add_notifier(C, NC_PEN | ND_DATA | NA_EDITED, nullptr);
  }
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_ADDED, nullptr);

  return OP_FINISHED;
}

void act_ot_keyframe_insert(WinOpType *ot)
{
  /* ids */
  ot->name = "Insert Keyframes";
  ot->idname = "act_ot_keyframe_insert";
  ot->description = "Insert keyframes for the specified channels";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = actkeys_insertkey_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* id-props */
  ot->prop = api_def_enum(ot->sapi, "type", prop_actkeys_insertkey_types, 0, "Type", "");
}

/* -------------------------------------------------------------------- */
/* Keyframes: Dupl Op */

static bool dupl_act_keys(AnimCxt *ac)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;
  bool changed = false;

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* loop through filtered data and delete selected keys */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    if (elem(ale->type, ANIMTYPE_FCURVE, ANIMTYPE_NLACURVE)) {
      changed |= dupl_fcurve_keys((FCurve *)ale->key_data);
    }
    else if (ale->type == ANIMTYPE_PENLAYER) {
      ed_pen_layer_frames_dupl((PenLayer *)ale->data);
      changed |= ed_pen_layer_frame_sel_check((PenLayer *)ale->data);
    }
    else if (ale->type == ANIMTYPE_PEN_LAYER) {
      changed |= dune::ed::pen::dupl_sel_frames(
          *reinterpret_cast<Pen *>(ale->id),
          static_cast<PenLayer *>(ale->data)->wrap());
    }
    else if (ale->type == ANIMTYPE_MASKLAYER) {
      ed_masklayer_frames_dupl((MaskLayer *)ale->data);
    }
    else {
      lib_assert(0);
    }

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);

  return changed;
}

static int actkeys_dupl_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* dupl keyframes */
  if (!dupl_act_keys(&ac)) {
    return OP_CANCELLED;
  }

  /* set notifier that keyframes have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_ADDED, nullptr);

  return OP_FINISHED;
}

void act_ot_dupl(WinOpType *ot)
{
  /* ids */
  ot->name = "Dupl Keyframes";
  ot->idname = "act_ot_dupl";
  ot->description = "Make a copy of all sel keyframes";

  /* api cbs */
  ot->ex = actkeys_dupl_ex;
  ot->poll = ed_op_act_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/* Keyframes: Del Op */

static bool del_act_keys(AnimCxt *ac)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;
  bool changed_final = false;

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* loop through filtered data and del sel keys */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    bool changed = false;

    if (ale->type == ANIMTYPE_PENLAYER) {
      changed = ed_pen_layer_frames_del((PenLayer *)ale->data);
    }
    else if (ale->type == ANIMTYPE_PEN_LAYER) {
      Pen *pen = reinterpret_cast<Pen *>(ale->id);
      changed = dune::ed::pen::remove_all_sel_frames(
          *pen, static_cast<PenLayer *>(ale->data)->wrap());

      if (changed) {
        graph_id_tag_update(&pen->id, ID_RECALC_GEO);
      }
    }
    else if (ale->type == ANIMTYPE_MASKLAYER) {
      changed = ed_masklayer_frames_delete((MaskLayer *)ale->data);
    }
    else {
      FCurve *fcu = (FCurve *)ale->key_data;
      AnimData *adt = ale->adt;

      /* del sel keyframes only */
      changed = dune_fcurve_del_keys_sel(fcu);

      /* Only del curve too if it won't be doing anything anymore */
      if (dune_fcurve_is_empty(fcu)) {
        dune::animrig::animdata_fcurve_del(ac, adt, fcu);
        ale->key_data = nullptr;
      }
    }

    if (changed) {
      ale->update |= ANIM_UPDATE_DEFAULT;
      changed_final = true;
    }
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);

  return changed_final;
}

static int actkeys_del_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* del keyframes */
  if (!del_act_keys(&ac)) {
    return OP_CANCELLED;
  }

  /* set notifier that keyframes have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_REMOVED, nullptr);

  return OP_FINISHED;
}

void act_ot_del(WinOpType *ot)
{
  /* ids */
  ot->name = "Del Keyframes";
  ot->idname = "act_ot_del";
  ot->description = "Remove all sel keyframes";

  /* api cbs */
  ot->invoke = win_op_confirm_or_ex;
  ot->ex = actkeys_del_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
  win_op_props_confirm_or_ex(ot);
}

/* -------------------------------------------------------------------- */
/* Keyframes: Clean Op */

static void clean_act_keys(AnimCxt *ac, float thresh, bool clean_chan)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_FCURVESONLY | ANIMFILTER_NODUPLIS);

  if (clean_chan) {
    filter |= ANIMFILTER_SEL;
  }

  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimCont_Types(ac->datatype));

  const bool only_sel_keys = !clean_chan;
  /* loop through filtered data and clean curves */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    clean_fcurve(ac, ale, thresh, clean_chan, only_sel_keys);

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_clean_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  float thresh;
  bool clean_chan;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  if (elem(ac.datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
    dune_report(op->reports, RPT_ERR, "Not implemented");
    return OP_PASS_THROUGH;
  }

  /* get cleaning threshold */
  thresh = api_float_get(op->ptr, "threshold");
  clean_chan = api_bool_get(op->ptr, "channels");

  /* clean keyframes */
  clean_action_keys(&ac, thresh, clean_chan);

  /* set notifier that keyframes have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_EDITED, nullptr);

  return OP_FINISHED;
}

void act_ot_clean(WinOpType *ot)
{
  /* ids */
  ot->name = "Clean Keyframes";
  ot->idname = "act_ot_clean";
  ot->description = "Simplify F-Curves by removing closely spaced keyframes";

  /* api cbs */
  // ot->invoke =  /* we need that num popup for this! */
  ot->ex = actkeys_clean_ex;
  ot->poll = ed_op_act_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_float(
      ot->sapi, "threshold", 0.001f, 0.0f, FLT_MAX, "Threshold", "", 0.0f, 1000.0f);
  api_def_bool(ot->sapi, "channels", false, "Channels", "");
}

/* -------------------------------------------------------------------- */
/* Keyframes: Sample Op */

/* Evals the curves between each sel keyframe on each frame, and keys the val. */
static void bake_act_keys(AnimCxt *ac)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_FCURVESONLY | ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* Loop through filtered data and add keys between selected keyframes on every frame. */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    bake_fcurve_segments((FCurve *)ale->key_data);

    ale->update |= ANIM_UPDATE_DEPS;
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_bake_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  if (elem(ac.datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
    dune_report(op->reports, RPT_ERR, "Not implemented");
    return OP_PASS_THROUGH;
  }

  /* sample keyframes */
  bake_act_keys(&ac);

  /* set notifier that keyframes have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_EDITED, nullptr);

  return OP_FINISHED;
}

void act_ot_bake_keys(WinOpType *ot)
{
  /* ids */
  ot->name = "Bake Keyframes";
  ot->idname = "act_ot_bake_keys";
  ot->description = "Add keyframes on every frame between the sel keyframes";

  /* api cbs */
  ot->ex = actkeys_bake_ex;
  ot->poll = ed_op_act_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/* Settings: Set Extrapolation-Type Op */

/* defines for make/clear cyclic extrapolation tools */
#define MAKE_CYCLIC_EXPO -1
#define CLEAR_CYCLIC_EXPO -2

/* defines for set extrapolation-type for sel keyframes tool */
static const EnumPropItem prop_actkeys_expo_types[] = {
    {FCURVE_EXTRAPOLATE_CONSTANT,
     "CONSTANT",
     0,
     "Constant Extrapolation",
     "Vals on endpoint keyframes are held"},
    {FCURVE_EXTRAPOLATE_LINEAR,
     "LINEAR",
     0,
     "Linear Extrapolation",
     "Straight-line slope of end segments are extended past the endpoint keyframes"},

    {MAKE_CYCLIC_EXPO,
     "MAKE_CYCLIC",
     0,
     "Make Cyclic (F-Mod)",
     "Add Cycles F-Mod if one doesn't exist already"},
    {CLEAR_CYCLIC_EXPO,
     "CLEAR_CYCLIC",
     0,
     "Clear Cyclic (F-Mod)",
     "Remove Cycles F-Mod if not needed anymore"},
    {0, nullptr, 0, nullptr, nullptr},
};

/* this fn is responsible for setting extrapolation mode for keyframes */
static void setexpo_act_keys(AnimCxt *ac, short mode)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_SEL | ANIMFILTER_FCURVESONLY | ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* loop through setting mode per F-Curve */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    FCurve *fcu = (FCurve *)ale->data;

    if (mode >= 0) {
      /* just set mode setting */
      fcu->extend = mode;
    }
    else {
      /* shortcuts for managing Cycles F-Mods to make it easier to toggle cyclic anim
       * o having to go through FMod UI in Graph Ed to do so */
      if (mode == MAKE_CYCLIC_EXPO) {
        /* only add if one doesn't exist */
        if (list_has_suitable_fmod(&fcu->mods, FMOD_TYPE_CYCLES, -1) == 0) {
          /* TODO: add some more preset versions which set diff extrapolation options? */
          add_fmod(&fcu->mods, FMOD_TYPE_CYCLES, fcu);
        }
      }
      else if (mode == CLEAR_CYCLIC_EXPO) {
        /* remove all the mods fitting this description */
        FMod *fcm, *fcn = nullptr;

        for (fcm = static_cast<FMod *>(fcu->mods.first); fcm; fcm = fcn) {
          fcn = fcm->next;

          if (fcm->type == FMOD_TYPE_CYCLES) {
            remove_fmod(&fcu->mods, fcm);
          }
        }
      }
    }

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_expo_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short mode;
  
  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  if (elem(ac.datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
    dune_report(op->reports, RPT_ERR, "Not implemented");
    return OP_PASS_THROUGH;
  }

  /* get handle setting mode */
  mode = api_enum_get(op->ptr, "type");

  /* set handle type */
  setexpo_act_keys(&ac, mode);

  /* set notifier that keyframe props have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME_PROP, nullptr);

  return OP_FINISHED;
}

void act_ot_extrapolation_type(WinOpType *ot)
{
  /* ids */
  ot->name = "Set F-Curve Extrapolation";
  ot->idname = "act_ot_extrapolation_type";
  ot->description = "Set extrapolation mode for sel F-Curves";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = actkeys_expo_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* id-props */
  ot->prop = api_def_enum(ot->sapi, "type", prop_actkeys_expo_types, 0, "Type", "");
}

/* -------------------------------------------------------------------- */
/* Settings: Set Interpolation-Type Op */

static int actkeys_ipo_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short mode;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  if (elem(ac.datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
    dune_report(op->reports, RPT_ERR, "Not implemented");
    return OP_PASS_THROUGH;
  }

  /* get handle setting mode */
  mode = api_enum_get(op->ptr, "type");

  /* set handle type */
  anim_animdata_keyframe_cb(&ac,
                            (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE |
                            ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS |
                            ANIMFILTER_FCURVESONLY),
                            anim_editkeyframes_ipo(mode));

  /* set notifier that keyframe props have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME_PROP, nullptr);

  return OP_FINISHED;
}

void act_ot_interpolation_type(WinOpType *ot)
{
  /* ids */
  ot->name = "Set Keyframe Interpolation";
  ot->idname = "act_ot_interpolation_type";
  ot->description =
      "Set interpolation mode for the F-Curve segments starting from the selected keyframes";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = actkeys_ipo_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* id-props */
  ot->prop = api_def_enum(
      ot->sapi, "type", api_enum_beztriple_interpolation_mode_items, 0, "Type", "");
  api_def_prop_lang_cxt(ot->prop, LANG_CXT_ID_ACTION);
}

/* -------------------------------------------------------------------- */
/* Settings: Set Easing Op */

static int actkeys_easing_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short mode;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* get handle setting mode */
  mode = api_enum_get(op->ptr, "type");

  /* set handle type */
  anim_animdata_keyframe_cb(&ac,
                            (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE |
                            ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS |
                            ANIMFILTER_FCURVESONLY),
                            anim_editkeyframes_easing(mode));

  /* set notifier that keyframe props have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME_PROP, nullptr);

  return OP_FINISHED;
}

void act_ot_easing_type(WinOpType *ot)
{
  /* ids */
  ot->name = "Set Keyframe Easing Type";
  ot->idname = "act_ot_easing_type";
  ot->description =
      "Set easing type for the F-Curve segments starting from the selected keyframes";

  /* api cb */
  ot->invoke = win_menu_invoke;
  ot->ex = actkeys_easing_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* id-props */
  ot->prop = api_def_enum(
      ot->sapi, "type", api_enum_beztriple_interpolation_easing_items, 0, "Type", "");
}

/* -------------------------------------------------------------------- */
/* Settings: Set Handle-Type Op */

/* this fn is responsible for setting handle-type of sel keyframes */
static void sethandles_action_keys(AnimCxt *ac, short mode)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  KeyframeEditFn edit_cb = anim_editkeyframes_handles(mode);
  KeyframeEditFn sel_cb = anim_editkeyframes_ok(BEZT_OK_SEL);

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_FCURVESONLY | ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* Loop through setting flags for handles
   * we do not supply KeyframeEditData to the looper yet.
   * Currently that's not necessary here. */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    FCurve *fcu = (FCurve *)ale->key_data;

    /* any sel keyframes for editing? */
    if (anim_fcurve_keyframes_loop(nullptr, fcu, nullptr, sel_cb, nullptr)) {
      /* change type of sel handles */
      anim_fcurve_keyframes_loop(nullptr, fcu, nullptr, edit_cb, dune_fcurve_handles_recalc);

      ale->update |= ANIM_UPDATE_DEFAULT;
    }
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_handletype_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short mode;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  if (elem(ac.datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
    dune_report(op->reports, RPT_ERR, "Not implemented");
    return OP_PASS_THROUGH;
  }

  /* get handle setting mode */
  mode = api_enum_get(op->ptr, "type");

  /* set handle type */
  sethandles_act_keys(&ac, mode);

  /* set notifier that keyframe props have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME_PROP, nullptr);

  return OP_FINISHED;
}

void act_ot_handle_type(WinOpType *ot)
{
  /* ids */
  ot->name = "Set Keyframe Handle Type";
  ot->idname = "act_ot_handle_type";
  ot->description = "Set type of handle for selected keyframes";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = actkeys_handletype_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* id-props */
  ot->prop = api_def_enum(ot->sapi, "type", api_enum_keyframe_handle_type_items, 0, "Type", "");
}

/* -------------------------------------------------------------------- */
/* Settings: Set Keyframe-Type Op */

/* this fn is responsible for setting keyframe type for keyframes */
static void setkeytype_act_keys(AnimCxt *ac, short mode)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;
  KeyframeEditFn set_cb = anim_editkeyframes_keytype(mode);

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* Loop through setting BezTriple interpolation
   * NOTE: we do not supply KeyframeEdData to the looper yet.
   * Currently that's not necessary here.  */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    switch (ale->type) {
      case ANIMTYPE_PENLAYER:
        ed_pen_layer_frames_keytype_set(static_cast<PenLayer *>(ale->data), mode);
        ale->update |= ANIM_UPDATE_DEPS;
        break;

      case ANIMTYPE_PEN_LAYER:
        dune::ed::pen::set_sel_frames_type(
            static_cast<PenLayer *>(ale->data)->wrap(),
            static_cast<eBezTripleKeyframeType>(mode));
        ale->update |= ANIM_UPDATE_DEPS;
        break;

      case ANIMTYPE_FCURVE:
        anim_fcurve_keyframes_loop(
            nullptr, static_cast<FCurve *>(ale->key_data), nullptr, set_cb, nullptr);
        ale->update |= ANIM_UPDATE_DEPS | ANIM_UPDATE_HANDLES;
        break;

      default:
        lib_assert_msg(false, "Keytype cannot be set into this anim type.");
    }
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_keytype_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short mode;

  /* get ed data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  if (ac.datatype == ANIMCONT_MASK) {
    dune_report(op->reports, RPT_ERR, "Not implemented for Masks");
    return OP_PASS_THROUGH;
  }

  /* get handle setting mode */
  mode = api_enum_get(op->ptr, "type");

  /* set handle type */
  setkeytype_action_keys(&ac, mode);

  /* set notifier that keyframe props have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME_PROP, nullptr);

  return OP_FINISHED;
}

void act_ot_keyframe_type(WinOpType *ot)
{
  /* ids */
  ot->name = "Set Keyframe Type";
  ot->idname = "act_ot_keyframe_type";
  ot->description = "Set type of keyframe for the selected keyframes";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = actkeys_keytype_exec;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* id-props */
  ot->prop = api_def_enum(ot->sapi, "type", api_enum_beztriple_keyframe_type_items, 0, "Type", "");
}

/* -------------------------------------------------------------------- */
/* Transform: Jump to Sel Frames Op */

static bool actkeys_framejump_poll(Cxt *C)
{
  /* prevent changes during rndr */
  if (G.is_rndring) {
    return false;
  }

  return ed_op_act_active(C);
}

/* snap current-frame indicator to 'avg time' of sel keyframe */
static int actkeys_framejump_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;
  KeyframeEdData ked = {{nullptr}};

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* init edit data */
  /* loop over act data, averaging vals */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS);
  anim_animdata_filter(&ac, &anim_data, filter, ac.data, eAnimContTypes(ac.datatype));

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    switch (ale->datatype) {
      case ALE_PENFRAME: {
        PenLayer *pl = static_cast<PenLayer *>(ale->data);

        LIST_FOREACH (PenDataFrame *, pf, &pl->frames) {
          /* only if sel */
          if (!(pf->flag & PEN_FRAME_SEL)) {
            continue;
          }
          /* store avg time in float 1 (only do rounding at last step) */
          ked.f1 += pf->framenum;

          /* increment num of items */
          ked.i1++;
        }
        break;
      }

      case ALE_PEN_CEL: {
        using namespace dune::pen;
        const Layer &layer = *static_cast<Layer *>(ale->data);
        for (auto [frame_num, frame] : layer.frames().items()) {
          if (!frame.is_sel()) {
            continue;
          }
          ked.f1 += frame_num;
          ked.i1++;
        }
        break;
      }

      case ALE_FCURVE: {
        AnimData *adt = ankm_nla_mapping_get(&ac, ale);
        FCurve *fcurve = static_cast<FCurve *>(ale->key_data);
        if (adt) {
          anim_nla_mapping_apply_fcurve(adt, fcurve, false, true);
          anim_fcurve_keyframes_loop(&ked, fcurve, nullptr, bezt_calc_average, nullptr);
          anom_nla_mapping_apply_fcurve(adt, fcurve, true, true);
        }
        else {
          anim_fcurve_keyframes_loop(&ked, fcurve, nullptr, bezt_calc_average, nullptr);
        }
        break;
      }

      default:
        lib_assert_msg(false, "Cannot jump to keyframe into this animation type.");
    }
  }

  anim_animdata_freelist(&anim_data);

  /* set new curr frame val, based on the avg time */
  if (ked.i1) {
    Scene *scene = ac.scene;
    scene->r.cfra = round_fl_to_int(ked.f1 / ked.i1);
    scene->r.subframe = 0.0f;
  }

  /* set notifier that things have changed */
  win_ev_add_notifier(C, NC_SCENE | ND_FRAME, ac.scene);

  return OP_FINISHED;
}

void act_ot_frame_jump(WinOpType *ot)
{
  /* ids */
  ot->name = "Jump to Keyframes";
  ot->idname = "act_ot_frame_jump";
  ot->description = "Set the current frame to the average frame value of selected keyframes";

  /* api cbs */
  ot->ex = actkeys_framejump_ex;
  ot->poll = actkeys_framejump_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Transform: Snap Keyframes Op */

/* defines for snap keyframes tool */
static const EnumPropItem prop_actkeys_snap_types[] = {
    {ACTKEYS_SNAP_CFRA,
     "CFRA",
     0,
     "Sel to Current Frame",
     "Snap sel keyframes to the current frame"},
    {ACTKEYS_SNAP_NEAREST_FRAME,
     "NEAREST_FRAME",
     0,
     "Sel to Nearest Frame",
     "Snap sel keyframes to the nearest (whole) frame "
     "(use to fix accidental subframe offsets)"},
    {ACTKEYS_SNAP_NEAREST_SECOND,
     "NEAREST_SECOND",
     0,
     "Sel to Nearest Second",
     "Snap sel keyframes to the nearest second"},
    {ACTKEYS_SNAP_NEAREST_MARKER,
     "NEAREST_MARKER",
     0,
     "Sel to Nearest Marker",
     "Snap sel keyframes to the nearest marker"},
    {0, nullptr, 0, nullptr, nullptr},
};

/* this fn is responsible for snapping keyframes to frame-times */
static void snap_action_keys(AnimCxt *ac, short mode)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  KeyframeEditData ked = {{nullptr}};
  KeyframeEditFn edit_cb;

  /* filter data */
  if (elem(ac->datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
    filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT);
  }
  else {
    filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
              ANIMFILTER_NODUPLIS);
  }
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimCont_Types(ac->datatype));

  /* get beztriple editing cbs */
  edit_cb = anim_editkeyframes_snap(mode);

  ked.scene = ac->scene;
  if (mode == ACTKEYS_SNAP_NEAREST_MARKER) {
    ked.list.first = (ac->markers) ? ac->markers->first : nullptr;
    ked.list.last = (ac->markers) ? ac->markers->last : nullptr;
  }

  /* snap keyframes */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    AnimData *adt = anim_nla_mapping_get(ac, ale);

    if (ale->type == ANIMTYPE_GPLAYER) {
      ed_pen_layer_snap_frames(static_cast<PenDataLayer *>(ale->data), ac->scene, mode);
    }
    else if (ale->type == ANIMTYPE_PEN_LAYER) {
      Pen *pen = reinterpret_cast<Pen *>(ale->id);
      PenLayer *layer = static_cast<PenLayer *>(ale->data);

      const bool changed = dune::ed::pen::snap_sel_frames(
          *pen, layer->wrap(), *(ac->scene), static_cast<eEditKeyframesSnap>(mode));

      if (changed) {
        graph_id_tag_update(&pen->id, ID_RECALC_GEOMETRY);
      }
    }
    else if (ale->type == ANIMTYPE_MASKLAYER) {
      ed_masklayer_snap_frames(static_cast<MaskLayer *>(ale->data), ac->scene, mode);
    }
    else if (adt) {
      FCurve *fcurve = static_cast<FCurve *>(ale->key_data);
      anim_nla_mapping_apply_fcurve(adt, fcurve, false, false);
      anom_fcurve_keyframes_loop(&ked, fcurve, nullptr, edit_cb, dune_fcurve_handles_recalc);
      dune_fcurve_merge_dup_keys(
          fcurve, SEL, false); /* only use handles in graph editor */
      anim_nla_mapping_apply_fcurve(adt, fcurve, true, false);
    }
    else {
      FCurve *fcurve = static_cast<FCurve *>(ale->key_data);
      anim_fcurve_keyframes_loop(&ked, fcurve, nullptr, edit_cb, dune_fcurve_handles_recalc);
      dune_fcurve_merge_dup_keys(
          fcurve, SEL, false); /* only use handles in graph editor */
    }

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_snap_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short mode;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* get snapping mode */
  mode = api_enum_get(op->ptr, "type");

  /* snap keyframes */
  snap_action_keys(&ac, mode);

  /* set notifier that keyframes have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_EDITED, nullptr);

  return OP_FINISHED;
}

void act_ot_snap(WinOpType *ot)
{
  /* ids */
  ot->name = "Snap Keys";
  ot->idname = "act_ot_snap";
  ot->description = "Snap sel keyframes to the times specified";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = actkeys_snap_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* id-props */
  ot->prop = api_def_enum(ot->sapi, "type", prop_actkeys_snap_types, 0, "Type", "");
}

/* Transform: Mirror Keyframes Op */
/* defines for mirror keyframes tool */
static const EnumPropItem prop_actkeys_mirror_types[] = {
    {ACTKEYS_MIRROR_CFRA,
     "CFRA",
     0,
     "By Times Over Current Frame",
     "Flip times of sel keyframes using the current frame as the mirror line"},
    {ACTKEYS_MIRROR_XAXIS,
     "XAXIS",
     0,
     "By Vals Over Zero Val",
     "Flip vals of sel keyframes (i.e. negative values become positive, and vice versa)"},
    {ACTKEYS_MIRROR_MARKER,
     "MARKER",
     0,
     "By Times Over First Sel Marker",
     "Flip times of selected keyframes using the first sel marker as the reference point"},
    {0, nullptr, 0, nullptr, nullptr},
};

/* this fn is responsible for mirroring keyframes */
static void mirror_act_keys(AnimCxt *ac, short mode)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  KeyframeEditData ked = {{nullptr}};
  KeyframeEditFn edit_cb;

  /* get beztriple editing cbs */
  edit_cb = anim_editkeyframes_mirror(mode);

  ked.scene = ac->scene;

  /* for 'first sel marker' mode, need to find first sel marker first! */
  /* should this be made into a helper func in the API? */
  if (mode == ACTKEYS_MIRROR_MARKER) {
    TimeMarker *marker = ed_markers_get_first_sel(ac->markers);

    if (marker) {
      ked.f1 = float(marker->frame);
    }
    else {
      return;
    }
  }

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* mirror keyframes */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    AnimData *adt = anim_nla_mapping_get(ac, ale);

    if (ale->type == ANIMTYPE_GPLAYER) {
      ed_pen_layer_mirror_frames(static_cast<PenDataLayer *>(ale->data), ac->scene, mode);
    }
    else if (ale->type == ANIMTYPE_PEN_LAYER) {
      Pen *pen = reinterpret_cast<Pen *>(ale->id);
      PenLayer *layer = static_cast<PenLayer *>(ale->data);

      const bool changed = dune::ed::pen::mirror_sel_frames(
          *pen, layer->wrap(), *(ac->scene), static_cast<eEditKeyframesMirror>(mode));

      if (changed) {
        graph_id_tag_update(&pen->id, ID_RECALC_GEO);
      }
    }
    else if (ale->type == ANIMTYPE_MASKLAYER) {
      /* TODO */
    }
    else if (adt) {
      FCurve *fcurve = static_cast<FCurve *>(ale->key_data);
      anim_nla_mapping_apply_fcurve(adt, fcurve, false, false);
      anim_fcurve_keyframes_loop(&ked, fcurve, nullptr, edit_cb, dune_fcurve_handles_recalc);
      anim_nla_mapping_apply_fcurve(adt, fcurve, true, false);
    }
    else {
      anim_fcurve_keyframes_loop(
          &ked, static_cast<FCurve *>(ale->key_data), nullptr, edit_cb, duen_fcurve_handles_recalc);
    }

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_mirror_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short mode;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* get mirroring mode */
  mode = api_enum_get(op->ptr, "type");

  /* mirror keyframes */
  mirror_act_keys(&ac, mode);

  /* set notifier that keyframes have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_EDITED, nullptr);

  return OP_FINISHED;
}

void act_ot_mirror(WinOpType *ot)
{
  /* ids */
  ot->name = "Mirror Keys";
  ot->idname = "act_ot_mirror";
  ot->description = "Flip sel keyframes over the sel mirror line";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->exec = actkeys_mirror_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* id-props */
  ot->prop = api_def_enum(ot->sapi, "type", prop_actkeys_mirror_types, 0, "Type", "");
}
