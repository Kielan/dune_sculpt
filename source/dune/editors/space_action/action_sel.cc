#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_dlrbTree.h"
#include "lib_lasso_2d.h"
#include "lib_utildefines.h"

#include "types_anim.h"
#include "types_pen_legacy.h"
#include "types_mask.h"
#include "types_ob.h"
#include "types_scene.h"

#include "api_access.hh"
#include "api_define.hh"

#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_pen_legacy.h"
#include "dune_pen.hh"
#include "dune_nla.hh"

#include "ui_int.hh"
#include "ui_view2d.hh"

#include "ed_anim_api.hh"
#include "ed_pen_legacy.hh"
#include "ed_pen.hh"
#include "ed_keyframes_edit.hh"
#include "ed_keyframes_keylist.hh"
#include "ed_markers.hh"
#include "ed_mask.hh"
#include "ed_screen.hh"
#include "ed_sel_utils.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "action_intern.hh"


static AnimListElem *actkeys_find_list_element_at_position(AnimCxt *ac,
                                                           eAnimFilter_Flags filter,
                                                            float rgn_x,
                                                            float rgn_y)
{
  View2D *v2d = &ac->rgn->v2d;

  float view_x, view_y;
  int channel_index;
  ui_view2d_rgn_to_view(v2d, rgn_x, rgn_y, &view_x, &view_y);
  ui_view2d_listview_view_to_cell(0,
                                  ANIM_UI_get_channel_step(),
                                  0,
                                  ANIM_UI_get_first_channel_top(v2d),
                                  view_x,
                                  view_y,
                                  nullptr,
                                  &channel_index);

  List anim_data = {nullptr, nullptr};
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimCont_Types(ac->datatype));

  AnimListElem *ale = static_cast<AnimListElem *>(lib_findlink(&anim_data, channel_index));
  if (ale != nullptr) {
    lib_remlink(&anim_data, ale);
    ale->next = ale->prev = nullptr;
  }
  anim_animdata_freelist(&anim_data);

  return ale;
}

static void actkeys_list_elem_to_keylist(AnimCxt *ac,
                                         AnimKeylist *keylist,
                                         AnimListElem *ale)
{
  AnimData *adt = anim_nla_mapping_get(ac, ale);

  DopeSheet *ads = nullptr;
  if (elem(ac->datatype, ANIMCONT_DOPESHEET, ANIMCONT_TIMELINE)) {
    ads = static_cast<DopeSheet *>(ac->data);
  }

  if (ale->key_data) {
    switch (ale->datatype) {
      case ALE_SCE: {
        Scene *scene = (Scene *)ale->key_data;
        scene_to_keylist(ads, scene, keylist, 0);
        break;
      }
      case ALE_OB: {
        Ob *ob = (Ob *)ale->key_data;
        ob_to_keylist(ads, ob, keylist, 0);
        break;
      }
      case ALE_ACT: {
        Action *act = (Act *)ale->key_data;
        act_to_keylist(adt, act, keylist, 0);
        break;
      }
      case ALE_FCURVE: {
        FCurve *fcu = (FCurve *)ale->key_data;
        fcurve_to_keylist(adt, fcu, keylist, 0);
        break;
      }
    }
  }
  else if (ale->type == ANIMTYPE_SUMMARY) {
    /* dopesheet summary covers everything */
    summary_to_keylist(ac, keylist, 0);
  }
  else if (ale->type == ANIMTYPE_GROUP) {
    /* TODO: why don't we just give groups key_data too? */
    ActGroup *agrp = (ActGroup *)ale->data;
    act_group_to_keylist(adt, agrp, keylist, 0);
  }
  else if (ale->type == ANIMTYPE_PEN_LAYER) {
    /* TODO: why don't we just give pen layers key_data too? */
    pen_cels_to_keylist(
        adt, static_cast<const PenLayer *>(ale->data), keylist, 0);
  }
  else if (ale->type == ANIMTYPE_PEN_LAYER_GROUP) {
    /* TODO: why don't we just give pen layers key_data too? */
    pen_layer_group_to_keylist(
        adt, static_cast<const PenLayerTreeGroup *>(ale->data), keylist, 0);
  }
  else if (ale->type == ANIMTYPE_PEN_DATABLOCK) {
    /* TODO: why don't we just give pen layers key_data too? */
    pen_data_block_to_keylist(
        adt, static_cast<const Pen *>(ale->data), keylist, 0, false);
  }
  else if (ale->type == ANIMTYPE_PENLAYER) {
    /* TODO: why don't we just give penlayers key_data too? */
    PenLayer *penlayer = (PenLayer *)ale->data;
    gpl_to_keylist(ads, penlayer, keylist);
  }
  else if (ale->type == ANIMTYPE_MASKLAYER) {
    /* TODO: why don't we just give masklayers key_data too? */
    MaskLayer *masklay = (MaskLayer *)ale->data;
    mask_to_keylist(ads, masklay, keylist);
  }
}

static void actkeys_find_key_in_list_elem(AnimCxt *ac,
                                          AnimListElem *ale,
                                          float rgn_x,
                                          float *r_selx,
                                          float *r_frame,
                                          bool *r_found,
                                          bool *r_is_sel)
{
  *r_found = false;

  View2D *v2d = &ac->rgn->v2d;

  AnimKeylist *keylist = er_keylist_create();
  actkeys_list_elem_to_keylist(ac, keylist, ale);
  ed_keylist_prepare_for_direct_access(keylist);

  AnimData *adt = anim_nla_mapping_get(ac, ale);

  /* standard channel height (to allow for some slop) */
  float key_hsize = anim_ui_get_channel_height() * 0.8f;
  /* half-size (for either side), but rounded up to nearest int (for easier targeting) */
  key_hsize = roundf(key_hsize / 2.0f);

  const Range2f range = {
      ui_view2d_rgn_to_view_x(v2d, rgn_x - int(key_hsize)),
      ui_view2d_rgn_to_view_x(v2d, rgn_x + int(key_hsize)),
  };
  const ActKeyColumn *ak = ed_keylist_find_any_between(keylist, range);
  if (ak) {

    /* set the frame to use, and apply inverse-correction for NLA-mapping
     * so that the frame will get selected by the selection functions without
     * requiring to map each frame once again... */
    *r_selx = dune_nla_tweakedit_remap(adt, ak->cfra, NLATIME_CONVERT_UNMAP);
    *r_frame = ak->cfra;
    *r_found = true;
    *r_is_sel = (ak->sel & SEL) != 0;
  }

  /* cleanup tmp lists */
  ed_keylist_free(keylist);
}

static void actkeys_find_key_at_position(AnimCxt *ac,
                                         eAnimFilter_Flags filter,
                                         float rgn_x,
                                         float rgn_y,
                                         AnimListElem **r_ale,
                                         float *r_selx,
                                         float *r_frame,
                                         bool *r_found,
                                         bool *r_is_sel)

{
  *r_found = false;
  *r_ale = actkeys_find_list_elem_at_position(ac, filter, rgn_x, rgn_y);

  if (*r_ale != nullptr) {
    actkeys_find_key_in_list_elem(
        ac, *r_ale, rgn_x, r_selx, r_frame, r_found, r_is_sel);
  }
}

static bool actkeys_is_key_at_position(AnimCxt *ac, float rgn_x, float rgn_y)
{
  AnimListElem *ale;
  float selx, frame;
  bool found;
  bool is_sel;

  eAnimFilter_Flags filter = ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE |
                             ANIMFILTER_LIST_CHANNELS;
  actkeys_find_key_at_position(
      ac, filter, rgn_x, rgn_y, &ale, &selx, &frame, &found, &is_sel);

  if (ale != nullptr) {
    mem_free(ale);
  }
  return found;
}

/* Desel All Op
 * This op works in one of three ways:
 * 1) (de)sel all (AKEY) - test if select all or desel all.
 * 2) invert all (CTRL-IKEY) - invert sel of all keyframes.
 * 3) (de)sel all - no testing is done; only for use internal tools as normal fn. */

/* Desels keyframes in the act editor
 * - This is called by the desel all op, as well as other on
 * - test: check if sel or deselect all
 * - sel: how to sel keyframes (SEL_*)*/
static void desel_act_keys(AnimCxt *ac, short test, short sel)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilter_Flags filter;

  KeyframeEditData ked = {{nullptr}};
  KeyframeEditFn test_cb, sel_cb;

  /* determine type-based settings */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS);

  /* filter data */
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* init BezTriple looping data */
  test_cb = anim_editkeyframes_ok(BEZT_OK_SELECTED);

  /* See if we should be sel or desel */
  if (test) {
    LIST_FOREACH (AnimListElem *, ale, &anim_data) {
      if (ale->type == ANIMTYPE_PLAYER) {
        if (ed_pen_layer_frame_sel_check(static_cast<PenDatayer *>(ale->data))) {
          sel = SEL_SUBTRACT;
          break;
        }
      }
      else if (ale->type == ANIMTYPE_MASKLAYER) {
        if (ed_masklayer_frame_sel_check(static_cast<MaskLayer *>(ale->data))) {
          sel = SEL_SUBTRACT;
          break;
        }
      }
      else if (ale->type == ANIMTYPE_PEN_LAYER) {
        if (dune::ed::pen::has_any_frame_sel(
                static_cast<PenLayer *>(ale->data)->wrap()))
        {
          sel = SEL_SUBTRACT;
        }
        break;
      }
      else {
        if (anim_fcurve_keyframes_loop(
                &ked, static_cast<FCurve *>(ale->key_data), nullptr, test_cb, nullptr))
        {
          sel = SEL_SUBTRACT;
          break;
        }
      }
    }
  }

  /* convert sel to selmode, and use that to get editor */
  sel_cb = anim_editkeyframes_sel(sel);

  /* Now set the flags */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    if (ale->type == ANIMTYPE_PLAYER) {
      ed_pen_layer_frame_sel_set(static_cast<PenDataLayer *>(ale->data), sel);
      ale->update |= ANIM_UPDATE_DEPS;
    }
    else if (ale->type == ANIMTYPE_MASKLAYER) {
      ed_masklayer_frame_sel_set(static_cast<MaskLayer *>(ale->data), sel);
    }
    else if (ale->type == ANIMTYPE_PEN_LAYER) {
      dune::ed::pen::sel_all_frames(
          static_cast<PenLayer *>(ale->data)->wrap(), sel);
      ale->update |= ANIM_UPDATE_DEPS;
    }
    else {
      anim_fcurve_keyframes_loop(
          &ked, static_cast<FCurve *>(ale->key_data), nullptr, sel_cb, nullptr);
    }
  }

  /* Cleanup */
  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_deselall_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* 'standard' behavior - check if sel, then apply relevant selection */
  const int act = api_enum_get(op->ptr, "action");
  switch (act) {
    case SEL_TOGGLE:
      desel_act_keys(&ac, 1, SEL_ADD);
      break;
    case SEL_SEL:
      desel_act_keys(&ac, 0, SEL_ADD);
      break;
    case SEL_DESEL:
      desel_act_keys(&ac, 0, SEL_SUBTRACT);
    case SEL_INVERT:
      desel_actl_keys(&ac, 0, SEL_INVERT);
      break;
    default:
      lib_assert(0);
      break;
  }

  /* set notifier that keyframe sel have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SEL, nullptr);
  if (anim_animdata_can_have_pen(eAnimCont_Types(eAnimCont_Types(ac.datatype)))) {
    win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SEL, nullptr);
  }
  return OP_FINISHED;
}

void act_ot_sel_all(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel All";
  ot->idname = "act_ot_sel_all";
  ot->description = "Toggle sel of all keyframes";

  /* api cbs */
  ot->ex = actkeys_deselall_ex;
  ot->poll = ed_op_act_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  win_op_props_sel_all(ot);
}

/* Box Sel Op
 *
 * This op currently works in one of three ways:
 * - KEY     - 1) all keyframes within rgn are sel ACTKEYS_BORDERSEL_ALLKEYS.
 * - ALT-BKEY - depending on which axis of the rgn was larger...
 *   - 2) x-axis, sel all frames w/in frame range ACTKEYS_BORDERSEL_FRAMERANGE.
 *   - 3) y-axis, sel all frames w/in channels that rgn included
 *     ACTKEYS_BORDERSEL_CHANNELS **/

/* defines for box_sel mode */
enum {
  ACTKEYS_BORDERSEL_ALLKEYS = 0,
  ACTKEYS_BORDERSEL_FRAMERANGE,
  ACTKEYS_BORDERSEL_CHANNELS,
} /*eActKeysBoxSel_Mode*/;

struct BoxSelData {
  AnimCxt *ac;
  short selmode;

  KeyframeEditData ked;
  KeyframeEditFn ok_cb, sel_cb;
};

static void box_sel_elem(
    BoxSelData *sel_data, AnimListElem *ale, float xmin, float xmax, bool summary)
{
  AnimCxt *ac = sel_data->ac;

  switch (ale->type) {
#if 0 /* Keyframes are not currently shown here */
    case ANIMTYPE_PENDATABLOCK: {
      PenData *pd = ale->data;
      PenDataLayer *pdl;
      for (pdl = pd->layers.first; pdl; pdl = pdl->next) {
        ed_pen_layer_frames_sel_box(pdl, xmin, xmax, data->selmode);
      }
      ale->update |= ANIM_UPDATE_DEPS;
      break;
    }
#endif
    case ANIMTYPE_PEN_DATABLOCK: {
      Pen *pen = static_cast(Pen *>(ale->data);
      for (dune::pen::Layer *layer : pen->layers_for_write()) {
        dune::ed::pen::sel_frames_range(
            layer->wrap().as_node(), xmin, xmax, sel_data->selmode);
      }
      ale->update |= ANIM_UPDATE_DEPS;
      break;
    }
    case ANIMTYPE_PEN_LAYER_GROUP:
    case ANIMTYPE_PEN_LAYER:
      dune::ed::pen::sel_frames_range(
          static_cast<PenLayerTreeNode *>(ale->data)->wrap(),
          xmin,
          xmax,
          sel_data->selmode);
      ale->update |= ANIM_UPDATE_DEPS;
      break;
    case ANIMTYPE_PENLAYER: {
      ed_pen_layer_frames_sel_box(
          static_cast<PenDataLayer *>(ale->data), xmin, xmax, sel_data->selectmode);
      ale->update |= ANIM_UPDATE_DEPS;
      break;
    }
    case ANIMTYPE_MASKDATABLOCK: {
      Mask *mask = static_cast<Mask *>(ale->data);
      MaskLayer *masklay;
      for (masklay = static_cast<MaskLayer *>(mask->masklayers.first); masklay;
           masklay = masklay->next) {
        ed_masklayer_frames_sel_box(masklay, xmin, xmax, sel_data->selmode);
      }
      break;
    }
    case ANIMTYPE_MASKLAYER: {
      ed_masklayer_frames_sel_box(
          static_cast<MaskLayer *>(ale->data), xmin, xmax, sel_data->selmode);
      break;
    }
    default: {
      if (summary) {
        break;
      }

      if (ale->type == ANIMTYPE_SUMMARY) {
        List anim_data = {nullptr, nullptr};
        anim_animdata_filter(
            ac, &anim_data, ANIMFILTER_DATA_VISIBLE, ac->data, eAnimContTypes(ac->datatype));

        LIST_FOREACH (AnimListElem *, ale2, &anim_data) {
          box_sel_elem(sel_data, ale2, xmin, xmax, true);
        }

        anim_animdata_update(ac, &anim_data);
        anim_animdata_freelist(&anim_data);
      }

      if (!elem(ac->datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
        anim_animchannel_keyframes_loop(
            &sel_data->ked, ac->ads, ale, sel_data->ok_cb, sel_data->sel_cb, nullptr);
      }
    }
  }
}

static void box_sel_action(AnimCxt *ac, const rcti rect, short mode, short selmode)
{
  List anim_data = {nullptr, nullptr};
  AnimListElem *ale;
  eAnimFilterFlags filter;

  BoxSelData sel_data{};
  sel_data.ac = ac;
  sel_data.selmode = selmode;

  View2D *v2d = &ac->rgn->v2d;
  rctf rectf;

  /* Convert mouse coordis to frame ranges and channel
   * coords corrected for view pan/zoom. */
  ui_view2d_rgn_to_view(v2d, rect.xmin, rect.ymin + 2, &rectf.xmin, &rectf.ymin);
  ui_view2d_rgn_to_view(v2d, rect.xmax, rect.ymax - 2, &rectf.xmax, &rectf.ymax);

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_LIST_CHANNELS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* Get beztriple editing/validation fns. */
  sel_data.sel_cb = anim_editkeyframes_sel(selmode);

  if (elem(mode, ACTKEYS_BORDERSEL_FRAMERANGE, ACTKEYS_BORDERSEL_ALLKEYS)) {
    sel_data.ok_cb = anim_editkeyframes_ok(BEZT_OK_FRAMERANGE);
  }
  else {
    sel_data.ok_cb = nullptr;
  }

  /* init editing data */
  memset(&sel_data.ked, 0, sizeof(KeyframeEditData));

  float ymax = anim_ui_get_first_channel_top(v2d);
  const float channel_step = anim_ui_get_channel_step();

  /* loop over data, doing box sel */
  for (ale = static_cast<AnimListElem *>(anim_data.first); ale;
       ale = ale->next, ymax -= channel_step)
  {
    AnimData *adt = anim_nla_mapping_get(ac, ale);

    /* get new vertical min extent of channel */
    float ymin = ymax - channel_step;

    /* set horizontal range (if applicable) */
    if (ELEM(mode, ACTKEYS_BORDERSEL_FRAMERANGE, ACTKEYS_BORDERSEL_ALLKEYS)) {
      /* if channel is mapped in NLA, apply correction */
      if (adt) {
        sel_data.ked.iterflags &= ~(KED_F1_NLA_UNMAP | KED_F2_NLA_UNMAP);
        sel_data.ked.f1 = dune_nla_tweakedit_remap(adt, rectf.xmin, NLATIME_CONVERT_UNMAP);
        sel_data.ked.f2 = dune_nla_tweakedit_remap(adt, rectf.xmax, NLATIME_CONVERT_UNMAP);
      }
      else {
        sel_data.ked.iterflags |= (KED_F1_NLA_UNMAP | KED_F2_NLA_UNMAP); /* for summary tracks */
        sel_data.ked.f1 = rectf.xmin;
        sel_data.ked.f2 = rectf.xmax;
      }
    }

    /* perform vertical suitability check (if applicable) */
    if ((mode == ACTKEYS_BORDERSEL_FRAMERANGE) || !((ymax < rectf.ymin) || (ymin > rectf.ymax))) {
      box_sel_elem(&sel_data, ale, rectf.xmin, rectf.xmax, false);
    }
  }

  /* cleanup */
  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_box_sel_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  AnimCxt ac;
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  bool tweak = api_bool_get(op->ptr, "tweak");
  if (tweak) {
    int mval[2];
    win_ev_drag_start_mval(event, ac.region, mval);
    if (actkeys_is_key_at_position(&ac, mval[0], mval[1])) {
      return OP_CANCELLED | OP_PASS_THROUGH;
    }
  }

  return win_gesture_box_invoke(C, op, ev);
}

static int actkeys_box_sel_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  rcti rect;
  short mode = 0;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  const eSelOp sel_op = eSelOp(api_enum_get(op->ptr, "mode"));
  const int selmode = (sel_op != SEL_OP_SUB) ? SEL_ADD : SEl_SUBTRACT;
  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    desel_action_keys(&ac, 1, SEL_SUBTRACT);
  }

  /* get settings from op */
  win_op_props_border_to_rcti(op, &rect);

  /* sel 'mode' depends on whether box_sel rgn only matters on one axis */
  if (api_bool_get(op->ptr, "axis_range")) {
    /* Mode depends on which axis of the range is larger to determine which axis to use:
     * - checking this in rgn-space is fine,
     *   as it's fundamentally still going to be a diff rect size.
     * - the frame-range sel option is favored over the channel one (x over y),
     *   as frame-range one is often used for tweaking timing when "blocking",
     *   while channels is not that useful.. */
    if (lib_rcti_size_x(&rect) >= lib_rcti_size_y(&rect)) {
      mode = ACTKEYS_BORDERSEL_FRAMERANGE;
    }
    else {
      mode = ACTKEYS_BORDERSEL_CHANNELS;
    }
  }
  else {
    mode = ACTKEYS_BORDERSEL_ALLKEYS;
  }

  /* apply box_sel action */
  box_sel_action(&ac, rect, mode, selmode);

  /* set notifier that keyframe sel have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SELECTED, nullptr);
  if (anim_animdata_can_have_pen(eAnimContTypes(ac.datatype))) {
    win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SELECTED, nullptr);
  }
  return OP_FINISHED;
}

void action_ot_sel_box(WinOpType *ot)
{
  /* ids */
  ot->name = "Box Sel";
  ot->idname = "action_ot_sel_box";
  ot->description = "Sel all keyframes within the specified region";

  /* api cbs */
  ot->invoke = actkeys_box_sel_invoke;
  ot->ex = actkeys_box_sel_ex;
  ot->modal = win_gesture_box_modal;
  ot->cancel = win_gesture_box_cancel;

  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* api */
  ot->prop = api_def_bool(ot->srna, "axis_range", false, "Axis Range", "");

  /* props */
  win_op_props_gesture_box(ot);
  win_op_props_sel_op_simple(ot);

  ApiProp *prop = api_def_bool(
      ot->sapi, "tweak", false, "Tweak", "Op has been activated using a click-drag ev");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* Rgn Sel Ops
 * "Rgn Sel" ops include the Lasso and Circle Sel ops.
 * These two ended up being lumped together, as it was easier in the
 * original Graph Editor implementation of these to do it this way. */

struct RgnSelData {
  AnimCxt *ac;
  short mode;
  short selmode;

  KeyframeEditData ked;
  KeyframeEditFn ok_cb, sel_cb;
};

static void rgn_sel_elem(RgnSelData *sel_data, AnimListElem *ale, bool summary)
{
  AnimCxt *ac = sel_data->ac;

  switch (ale->type) {
#if 0 /* Keyframes are not currently shown here */
    case ANIMTYPE_PENDATABLOCK: {
      PenData *pdl = ale->data;
      PenDataLayer *pdl;
      for (pdl = pdl->layers.first; pdl; pdl = pdl->next) {
        ed_pen_layer_frames_sel_rgn(
            &rdata->ked, ale->data, rdata->mode, rdata->selmode);
      }
      break;
    }
#endif
    case ANIMTYPE_PLAYER: {
      ed_pen_layer_frames_sel_rgn(&sel_data->ked,
                                  static_cast<PenDataLayer *>(ale->data),
                                  sel_data->mode,
                                  sel_data->selectmode);
      ale->update |= ANIM_UPDATE_DEPS;
      break;
    }
    case ANIMTYPE_PEN_LAYER_GROUP:
    case ANIMTYPE_PEN_LAYER: {
      dune::ed::pen::sel_frames_rgn(
          &sel_data->ked,
          static_cast<PenLayerTreeNode *>(ale->data)->wrap(),
          sel_data->mode,
          sel_data->selmode);
      ale->update |= ANIM_UPDATE_DEPS;
      break;
    }
    case ANIMTYPE_PEN_DATABLOCK: {
      List anim_data = {nullptr, nullptr};
      anim_animdata_filter(
          ac, &anim_data, ANIMFILTER_DATA_VISIBLE, ac->data, eAnimContTypes(ac->datatype));

      LIST_FOREACH (AnimListElem *, ale2, &anim_data) {
        if ((ale2->type == ANIMTYPE_PEN_LAYER) && (ale2->id == ale->data)) {
          rgn_sel_elem(sel_data, ale2, true);
        }
      }

      anim_animdata_update(ac, &anim_data);
      anim_animdata_freelist(&anim_data);
      break;
    }
    case ANIMTYPE_MASKDATABLOCK: {
      Mask *mask = static_cast<Mask *>(ale->data);
      MaskLayer *masklay;
      for (masklay = static_cast<MaskLayer *>(mask->masklayers.first); masklay;
           masklay = masklay->next) {
        ed_masklayer_frames_sel_rgn(
            &sel_data->ked, masklay, sel_data->mode, sel_data->selmode);
      }
      break;
    }
    case ANIMTYPE_MASKLAYER: {
      ed_masklayer_frames_sel_rgn(&sel_data->ked,
                                  static_cast<MaskLayer *>(ale->data),
                                  sel_data->mode,
                                  sel_data->selmode);
      break;
    }
    default: {
      if (summary) {
        break;
      }

      if (ale->type == ANIMTYPE_SUMMARY) {
        List anim_data = {nullptr, nullptr};
        anim_animdata_filter(
            ac, &anim_data, ANIMFILTER_DATA_VISIBLE, ac->data, eAnimContTypes(ac->datatype));

        LIST_FOREACH (AnimListElem *, ale2, &anim_data) {
          rgn_sel_elem(sel_data, ale2, true);
        }

        anim_animdata_update(ac, &anim_data);
        anim_animdata_freelist(&anim_data);
      }

      if (!elem(ac->datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
        anim_animchannel_keyframes_loop(
            &sel_data->ked, ac->ads, ale, sel_data->ok_cb, sel_data->sel_cb, nullptr);
      }
    }
  }
}

static void rgn_sel_action_keys(
    AnimCxt *ac, const rctf *rectf_view, short mode, short selmode, void *data)
{
  List anim_data = {nullptr, nullptr};
  AnimListElem *ale;
  eAnimFilterFlags filter;

  RhnSelData sel_data{};
  sel_data.ac = ac;
  sel_data.mode = mode;
  sel_data.selmode = selmode;
  View2D *v2d = &ac->rgn->v2d;
  rctf rectf, scaled_rectf;

  /* Convert mouse coords to frame ranges and channel
   * coords corrected for view pan/zoom. */
  ui_view2d_rgn_to_view_rctf(v2d, rectf_view, &rectf);

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_LIST_CHANNELS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* Get beztriple editing/validation functions. */
  sel_data.sel_cb = anim_editkeyframes_sel(selmode);
  sel_data.ok_cb = anim_editkeyframes_ok(mode);

  /* init editing data */
  memset(&sel_data.ked, 0, sizeof(KeyframeEditData));
  if (mode == BEZT_OK_CHANNEL_LASSO) {
    KeyframeEditLassoData *data_lasso = static_cast<KeyframeEditLassoData *>(data);
    data_lasso->rectf_scaled = &scaled_rectf;
    sel_data.ked.data = data_lasso;
  }
  else if (mode == BEZT_OK_CHANNEL_CIRCLE) {
    KeyframeEditCircleData *data_circle = static_cast<KeyframeEditCircleData *>(data);
    data_circle->rectf_scaled = &scaled_rectf;
    sel_data.ked.data = data;
  }
  else {
    sel_data.ked.data = &scaled_rectf;
  }

  float ymax = anim_ui_get_first_channel_top(v2d);
  const float channel_step = anim_ui_get_channel_step();

  /* loop over data, doing rgn sel */
  for (ale = static_cast<AnimListElem *>(anim_data.first); ale;
       ale = ale->next, ymax -= channel_step)
  {
    AnimData *adt = anim_nla_mapping_get(ac, ale);

    /* get new vertical minimum extent of channel */
    const float ymin = ymax - channel_step;

    /* compute midpoint of channel (used for testing if the key is in the region or not) */
    sel_data.ked.channel_y = (ymin + ymax) / 2.0f;

    /* if channel is mapped in NLA, apply correction
     * - Apply to the bounds being checked, not all the keyframe points,
     *   to avoid having scaling everything
     * - Save result to the scaled_rect, which is all that these ops
     *   will read from  */
    if (adt) {
      sel_data.ked.iterflags &= ~(KED_F1_NLA_UNMAP | KED_F2_NLA_UNMAP);
      sel_data.ked.f1 = dune_nla_tweakedit_remap(adt, rectf.xmin, NLATIME_CONVERT_UNMAP);
      sel_data.ked.f2 = dune_nla_tweakedit_remap(adt, rectf.xmax, NLATIME_CONVERT_UNMAP);
    }
    else {
      sel_data.ked.iterflags |= (KED_F1_NLA_UNMAP | KED_F2_NLA_UNMAP); /* for summary tracks */
      sel_data.ked.f1 = rectf.xmin;
      sel_data.ked.f2 = rectf.xmax;
    }

    /* Update vals for scaled_rectf - which is used to compute the mapping in the cbs
     * NOTE: Since summary tracks need late-binding remapping, the cbs may overwrite these
     * w the properly remapped ked.f1/f2 values, when needed  */
    scaled_rectf.xmin = sel_data.ked.f1;
    scaled_rectf.xmax = sel_data.ked.f2;
    scaled_rectf.ymin = ymin;
    scaled_rectf.ymax = ymax;

    /* perform vertical suitability check (if applicable) */
    if ((mode == ACTKEYS_BORDERSEL_FRAMERANGE) || !((ymax < rectf.ymin) || (ymin > rectf.ymax))) {
      rgn_sel_elem(&sel_data, ale, false);
    }
  }

  /* cleanup */
  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_lassosel_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;

  KeyframeEditLassoData data_lasso;
  rcti rect;
  rctf rect_fl;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  data_lasso.rectf_view = &rect_fl;
  data_lasso.mcoords = win_gesture_lasso_path_to_array(C, op, &data_lasso.mcoords_len);
  if (data_lasso.mcoords == nullptr) {
    return OP_CANCELLED;
  }

  const eSelOp sel_op = eSelOp(api_enum_get(op->ptr, "mode"));
  const int selmode = (sel_op != SEL_OP_SUB) ? SEL_ADD : SEL_SUBTRACT;
  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    desel_action_keys(&ac, 1, SEL_SUBTRACT);
  }

  /* get settings from op */
  lib_lasso_boundbox(&rect, data_lasso.mcoords, data_lasso.mcoords_len);
  lib_rctf_rcti_copy(&rect_fl, &rect);

  /* apply box_sel action */
  rgn_sel_action_keys(&ac, &rect_fl, BEZT_OK_CHANNEL_LASSO, selmode, &data_lasso);

  mem_free((void *)data_lasso.mcoords);

  /* send notifier that keyframe sel has changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SEL, nullptr);
  if (anim_animdata_can_have_pen(eAnimCont_Types(ac.datatype))) {
    win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SEL, nullptr);
  }
  return OP_FINISHED;
}

void action_ot_sel_lasso(WinOpType *ot)
{
  /* ids */
  ot->name = "Lasso Sel";
  ot->description = "Sel keyframe points using lasso selection";
  ot->idname = "action_ot_sel_lasso";

  /* api cbs */
  ot->invoke = win_gesture_lasso_invoke;
  ot->modal = win_gesture_lasso_modal;
  ot->ex = actkeys_lassosel_ex;
  ot->poll = ed_op_action_active;
  ot->cancel = win_gesture_lasso_cancel;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_DEPENDS_ON_CURSOR;
  
  /* props */
  win_op_props_gesture_lasso(ot)
  win_op_props_sel_op_simple(ot);
}

static int action_circle_sel_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;

  KeyframeEditCircleData data = {nullptr};
  rctf rect_fl;

  float x = api_int_get(op->ptr, "x");
  float y = api_int_get(op->ptr, "y");
  float radius = api_int_get(op->ptr, "radius");

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  const eSelOp sel_op = ed_sel_op_modal(
      eSelOp(api_enum_get(op->ptr, "mode")),
      win_gesture_is_modal_first(static_cast<WinGesture *>(op->customdata)));
  const short selmode = (sel_op != SEL_OP_SUB) ? SEL_ADD : SEL_SUBTRACT;
  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    desel_action_keys(&ac, 0, SEL_SUBTRACT);
  }

  data.mval[0] = x;
  data.mval[1] = y;
  data.radius_squared = radius * radius;
  data.rectf_view = &rect_fl;

  rect_fl.xmin = x - radius;
  rect_fl.xmax = x + radius;
  rect_fl.ymin = y - radius;
  rect_fl.ymax = y + radius;

  /* apply rgn sel action */
  rgn_sel_action_keys(&ac, &rect_fl, BEZT_OK_CHANNEL_CIRCLE, selmode, &data);

  /* send notifier that keyframe sel has changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SEL, nullptr);
  if (anim_animdata_can_have_pen(eAnimCont_Types(ac.datatype))) {
    win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SEL, nullptr);
  }
  return OP_FINISHED;
}

void action_ot_sel_circle(WinOpType *ot)
{
  ot->name = "Circle Sel";
  ot->description = "Sel keyframe points using circle sel";
  ot->idname = "action_ot_sel_circle";

  ot->invoke = win_gesture_circle_invoke;
  ot->modal = win_gesture_circle_modal;
  ot->ex = action_circle_sel_ex;
  ot->poll = win_oper_action_active;
  ot->cancel = win_gesture_circle_cancel;
  ot->get_name = dd_sel_circle_get_name;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* props */
  win_op_props_gesture_circle(ot);
  win_op_props_sel_op_simple(ot);
}

/* Column Sel Op
 * This op works in one of four ways:
 * - 1) sel all keyframes in the same frame as a sel one  (KKEY)
 * - 2) sel all keyframes in the same frame as the current frame marker (CTRL-KKEY)
 * - 3) sel all keyframes in the same frame as a sel markers (SHIFT-KKEY)
 * - 4) sel all keyframes that occur between sel markers (ALT-KKEY) */

/* defines for column-sel mode */
static const EnumPropItem prop_column_sel_types[] = {
    {ACTKEYS_COLUMNSEL_KEYS, "KEYS", 0, "On Sel Keyframes", ""},
    {ACTKEYS_COLUMNSEL_CFRA, "CFRA", 0, "On Current Frame", ""},
    {ACTKEYS_COLUMNSEL_MARKERS_COLUMN, "MARKERS_COLUMN", 0, "On Sel Markers", ""},
    {ACTKEYS_COLUMNSEL_MARKERS_BETWEEN,
     "MARKERS_BETWEEN",
     0,
     "Between Min/Max Selected Markers",
     ""},
    {0, nullptr, 0, nullptr, nullptr},
};

/* Sels all visible keyframes between the specified markers */
/* TODO: this is almost an _exact_ dup of a fn of the same name in
 * `graph_sel.cc` should de-dup. */
static void markers_selkeys_between(AnimCxt *ac)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  KeyframeEditFn ok_cb, sel_cb;
  KeyframeEditData ked = {{nullptr}};
  float min, max;

  /* get extreme markers */
  ed_markers_get_minmax(ac->markers, 1, &min, &max);
  min -= 0.5f;
  max += 0.5f;

  /* Get editing fns + data. */
  ok_cb = anim_editkeyframes_ok(BEZT_OK_FRAMERANGE);
  select_cb = anim_editkeyframes_sel(SEL_ADD);

  ked.f1 = min;
  ked.f2 = max;

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));
  
  /* sel keys in-between */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    switch (ale->type) {
      case ANIMTYPE_PEN_LAYER:
        dune::ed::pen::sel_frames_range(
            static_cast<PenLayerTreeNode *>(ale->data)->wrap(), min, max, SEL_ADD);
        ale->update |= ANIM_UPDATE_DEPS;
        break;
      case ANIMTYPE_PENLAYER:
        ed_pen_layer_frames_sel_box(
            static_cast<PenDataLayer *>(ale->data), min, max, SEL_ADD);
        ale->update |= ANIM_UPDATE_DEPS;
        break;

      case ANIMTYPE_MASKLAYER:
        ed_masklayer_frames_sel_box(static_cast<MaskLayer *>(ale->data), min, max, SEL_ADD);
        break;

      case ANIMTYPE_FCURVE: {
        AnimData *adt = anim_nla_mapping_get(ac, ale);
        FCurve *fcurve = static_cast<FCurve *>(ale->key_data);
        if (adt) {
          anim_nla_mapping_apply_fcurve(adt, fcurve, false, true);
          anom_fcurve_keyframes_loop(&ked, fcurve, ok_cb, sel_cb, nullptr);
          anim_nla_mapping_apply_fcurve(adt, fcurve, true, true);
        }
        else {
          anim_fcurve_keyframes_loop(&ked, fcurve, ok_cb, sel_cb, nullptr);
        }
        break;
      }

      default:
        lib_assert_msg(false, "Keys cannot be sel into this anim type.");
    }
  }

  /* Cleanup */
  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

/* Sels all visible keyframes in the same frames as the specified elemss */
static void columnsel_action_keys(AnimCxt *ac, short mode)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilter_Flags filter;

  Scene *scene = ac->scene;
  CfraElem *ce;
  KeyframeEditFn sel_cb, ok_cb;
  KeyframeEditData ked = {{nullptr}};

  /* build list of columns */
  switch (mode) {
    case ACTKEYS_COLUMNSEL_KEYS: /* list of sel keys */
      if (ac->datatype == ANIMCONT_PEN) {
        filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE);
        anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

        LIST_FOREACH (AnimListElem *, ale, &anim_data) {
          switch (ale->type) {
            case ANIMTYPE_PLAYER:
              ed_pen_layer_make_cfra_list(
                  static_cast<PenDataLayer *>(ale->data), &ked.list, true);
              break;
            case ANIMTYPE_PEN_LAYER:
              dune::ed::pen ::create_keyframe_edit_data_sel_frames_list(
                  &ked, static_cast<PenLayer *>(ale->data)->wrap());
              break;
            default:
              /* Invalid channel type. */
              lib_assert_unreachable();
          }
        }
      }
      else {
        filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE);
        anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimCont_Types(ac->datatype));

        LIST_FOREACH (AnimListElem *, ale, &anim_data) {
          if (ale->datatype == ALE_GPFRAME) {
            ed_pen_layer_make_cfra_list(static_cast<PenDataLayer *>(ale->data), &ked.list, true);
          }
          else {
            anim_fcurve_keyframes_loop(
                &ked, static_cast<FCurve *>(ale->key_data), nullptr, bezt_to_cfraelem, nullptr);
          }
        }
      }
      anim_animdata_freelist(&anim_data);
      break;

    case ACTKEYS_COLUMNSEL_CFRA: /* current frame */
      /* make a single CfraElem for storing this */
      ce = mem_cnew<CfraElem>("cfraElem");
      lib_addtail(&ked.list, ce);

      ce->cfra = float(scene->r.cfra);
      break;

    case ACTKEYS_COLUMNSEL_MARKERS_COLUMN: /* list of sel markers */
      ed_markers_make_cfra_list(ac->markers, &ked.list, SEL);
      break;

    default: /* invalid option */
      return;
  }

  /* set up BezTriple edit cbs */
  sel_cb = anim_editkeyframes_sel(SEL_ADD);
  ok_cb = anim_editkeyframes_ok(BEZT_OK_FRAME);

  /* loop through all of the keys and sel additional keyframes
   * based on the keys found to be sel above */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    AnimData *adt = anim_nla_mapping_get(ac, ale);

    /* loop over cfraelems (stored in the KeyframeEditData->list)
     * - we need to do this here, as we can apply fewer NLA-mapping conversions */
    LIST_FOREACH (CfraElem *, ce, &ked.list) {
      /* set frame for validation callback to refer to */
      if (adt) {
        ked.f1 = dune_nla_tweakedit_remap(adt, ce->cfra, NLATIME_CONVERT_UNMAP);
      }
      else {
        ked.f1 = ce->cfra;
      }

      /* sel elements with frame number matching cfraelem */
      if (ale->type == ANIMTYPE_GPLAYER) {
        es_pen_sel_frame(static_cast<PenDataLayer *>(ale->data), ce->cfra, SEL_ADD);
        ale->update |= ANIM_UPDATE_DEPS;
      }
      else if (ale->type == ANIMTYPE_PEN_LAYER) {
        dune::ed::pen::sel_frame_at(
            static_cast<enLayer *>(ale->data)->wrap(), ce->cfra, SELECT_ADD);
        ale->update |= ANIM_UPDATE_DEPS;
      }
      else if (ale->type == ANIMTYPE_MASKLAYER) {
        ed_mask_sel_frame(static_cast<MaskLayer *>(ale->data), ce->cfra, SEL_ADD);
      }
      else {
        anim_fcurve_keyframes_loop(
            &ked, static_cast<FCurve *>(ale->key_data), ok_cb, sel_cb, nullptr);
      }
    }
  }

  /* free elements */
  lib_freelist(&ked.list);

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_columnsel_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short mode;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* action to take depends on the mode */
  mode = anim_enum_get(op->ptr, "mode");

  if (mode == ACTKEYS_COLUMNSEL_MARKERS_BETWEEN) {
    markers_selkeys_between(&ac);
  }
  else {
    columnsel_action_keys(&ac, mode);
  }

  /* set notifier that keyframe sel have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SELECTED, nullptr);
  if (anim_animdata_can_have_pen(eAnimContTypes(ac.datatype))) {
    win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SELECTED, nullptr);
  }
  return OP_FINISHED;
}

void action_ot_sel_column(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel All";
  ot->idname = "action_ot_sel_column";
  ot->description = "Sel all keyframes on the specified frame(s)";

  /* api cbs */
  ot->ex = actkeys_columnsel_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(ot->sapi, "mode", prop_column_sel_types, 0, "Mode", "");
  api_def_prop_flag(ot->prop, PROP_HIDDEN);


/* Sel Linked Op */
static int actkeys_sel_linked_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;
  
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  KeyframeEditFn ok_cb = anim_editkeyframes_ok(BEZT_OK_SELECTED);
  KeyframeEditFn sel_cb = anim_editkeyframes_sel(SEL_ADD);

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* loop through all of the keys and sel additional keyframes based on these */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FCURVESONLY |
            ANIMFILTER_NODUPLIS);
  anim_animdata_filter(&ac, &anim_data, filter, ac.data, eAnimContTypes(ac.datatype));

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    FCurve *fcu = (FCurve *)ale->key_data;

    /* check if anything selected? */
    if (anim_fcurve_keyframes_loop(nullptr, fcu, nullptr, ok_cb, nullptr)) {
      /* select every keyframe in this curve then */
      anim_fcurve_keyframes_loop(nullptr, fcu, nullptr, sel_cb, nullptr);
    }
  }

  /* Cleanup */
  anim_animdata_freelist(&anim_data);

  /* set notifier that keyframe sel has changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SELECTED, nullptr);
  if (anim_animdata_can_have_pen(eAnimContTypes(ac.datatype))) {
    win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SELECTED, nullptr);
  }
  return OP_FINISHED;
}

void action_ot_sel_linked(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel Linked";
  ot->idname = "ACTION_OT_sel_linked";
  ot->description = "Sel keyframes occurring in the same F-Curves as selected ones";

  /* api cbs */
  ot->exec = actkeys_sel_linked_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Sel More/Less Ops */
/* Common code to perform sel */
static void sel_moreless_action_keys(AnimCxt *ac, short mode)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  KeyframeEditData ked = {{nullptr}};
  KeyframeEditFn build_cb;

  /* init selmap building data */
  build_cb = anim_editkeyframes_buildselmap(mode);

  /* loop through all of the keys and sel additional keyframes based on these */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FCURVESONLY |
            ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  LISTB_FOREACH (AnimListElem *, ale, &anim_data) {

    /* TODO: other types. */
    if (ale->datatype != ALE_FCURVE) {
      continue;
    }

    /* only continue if F-Curve has keyframes */
    FCurve *fcu = (FCurve *)ale->key_data;
    if (fcu->bezt == nullptr) {
      continue;
    }

    /* build up map of whether F-Curve's keyframes should be sel or not */
    ked.data = mem_calloc(fcu->totvert, "selmap actEdit more");
    anim_fcurve_keyframes_loop(&ked, fcu, nullptr, build_cb, nullptr);

    /* based on this map, adjust the sel status of the keyframes */
    anim_fcurve_keyframes_loop(&ked, fcu, nullptr, bezt_selmap_flush, nullptr);

    /* free the selmap used here */
    mem_free(ked.data);
    ked.data = nullptr;
  }

  /* Cleanup */
  anim_animdata_freelist(&anim_data);
}

static int actkeys_sel_more_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* perform sel changes */
  sel_moreless_action_keys(&ac, SELMAP_MORE);

  /* set notifier that keyframe sel has changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SELECTED, nullptr);
  if (anim_animdata_can_have_pen(eAnimContTypes(ac.datatype))) {
    win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SELECTED, nullptr);
  }
  return OP_FINISHED;
}

void action_ot_sel_more(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel More";
  ot->idname = "actiob_ot_sel_more";
  ot->description = "Sel keyframes beside already selected ones";

  /* api cbs */
  ot->ex = actkeys_sel_more_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int actkeys_sel_less_ex(Cxt *C, WinOp * /*op*/)
{
  AnimCxt ac;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* perform select changes */
  select_moreless_action_keys(&ac, SELMAP_LESS);

  /* set notifier that keyframe sele has changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SELECTED, nullptr);
  if (anim_animdata_can_have_pen(eAnimContTypes(ac.datatype))) {
    win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SELECTED, nullptr);
  }
  return OP_FINISHED;
}

void action_ot_sel_less(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel Less";
  ot->idname = "ACTION_OT_sel_less";
  ot->description = "Desel keyframes on ends of selection islands";

  /* api cbs */
  ot->ex = actkeys_sel_less_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
  
/* Sel Left/Right Op
 * Sel keyframes left/right of the current frame indicators */

/* defines for left-right sel tool */
static const EnumPropItem prop_actkeys_leftright_sel_types[] = {
    {ACTKEYS_LRSEL_TEST, "CHECK", 0, "Check if Sel Left or Right", ""},
    {ACTKEYS_LRSEL_LEFT, "LEFT", 0, "Before Current Frame", ""},
    {ACTKEYS_LRSEL_RIGHT, "RIGHT", 0, "After Current Frame", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static void actkeys_sel_leftright(AnimCxt *ac, short leftright, short sel_mode)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  KeyframeEditFn ok_cb, sel_cb;
  KeyframeEditData ked = {{nullptr}};
  Scene *scene = ac->scene;

  /* if sel mode is replace, desel all keyframes (and channels) first */
  if (sel_mode == SEL_REPLACE) {
    sel_mode = SEL_ADD;

    /* - desel all other keyframes, so that just the newly sel remain
     * - channels aren't deselected, since we don't re-select any as a consequence */
    desel_action_keys(ac, 0, SEL_SUBTRACT);
  }

  /* set cbs and editing data */
  ok_cb = anim_editkeyframes_ok(BEZT_OK_FRAMERANGE);
  select_cb = anim_editkeyframes_sel(sel_mode);

  if (leftright == ACTKEYS_LRSEL_LEFT) {
    ked.f1 = MINAFRAMEF;
    ked.f2 = float(scene->r.cfra + 0.1f);
  }
  else {
    ked.f1 = float(scene->r.cfra - 0.1f);
    ked.f2 = MAXFRAMEF;
  }

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  /* sel keys */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    switch (ale->type) {
      case ANIMTYPE_PEN_LAYER:
        dune::ed::pen::sel_frames_range;
            static_cast<PenLayerTreeNode *>(ale->data)->wrap(),
            ked.f1,
            ked.f2,
            sel_mode);
        ale->update |= ANIM_UPDATE_DEPS;
        break;
      case ANIMTYPE_PLAYER:
            ed_pen_layer_frames_sel_box(
            static_cast<PenDataLayer *>(ale->data), ked.f1, ked.f2, sel_mode);
        ale->update |= ANIM_UPDATE_DEPS;
        break;

      case ANIMTYPE_MASKLAYER:
        ed_masklayer_frames_sel_box(
            static_cast<MaskLayer *>(ale->data), ked.f1, ked.f2, sel_mode);
        break;

      case ANIMTYPE_FCURVE: {
        AnimData *adt = anim_nla_mapping_get(ac, ale);
        FCurve *fcurve = static_cast<FCurve *>(ale->key_data);
        if (adt) {
          anim_nla_mapping_apply_fcurve(adt, fcurve, false, true);
          anim_fcurve_keyframes_loop(&ked, fcurve, ok_cb, sel_cb, nullptr);
          anim_nla_mapping_apply_fcurve(adt, fcurve, true, true);
        }
        else {
          anim_fcurve_keyframes_loop(&ked, fcurve, ok_cb, sel_cb, nullptr);
        }
        break;
      }

      default:
        lib_assert_msg(false, "Keys cannot be sel into this anim type.");
    }
  }

  /* Sync marker support */
  if (sel_mode == SEL_ADD) {
    SpaceAction *saction = (SpaceAction *)ac->sl;

    if ((saction) && (saction->flag & SACTION_MARKERS_MOVE)) {
      List *markers = ed_animcxt_get_markers(ac);
      LIST_FOREACH (TimeMarker *, marker, markers) {
        if (((leftright == ACTKEYS_LRSEL_LEFT) && (marker->frame < scene->r.cfra)) ||
            ((leftright == ACTKEYS_LRSEL_RIGHT) && (marker->frame >= scene->r.cfra)))
        {
          marker->flag |= SEL;
        }
        else {
          marker->flag &= ~SEL;
        }
      }
    }
  }

  /* Cleanup */
  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

static int actkeys_sel_leftright_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  short leftright = api_enum_get(op->ptr, "mode");
  short selectmode;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* select mode is either replace (desel all, then add) or add/extend */
  if (api_bool_get(op->ptr, "extend")) {
    selmode = SEL_INVERT;
  }
  else {
    selmode = SEL_REPLACE;
  }

  /* if "test" mode is set, we don't have any info to set this with */
  if (leftright == ACTKEYS_LRSEL_TEST) {
    return OP_CANCELLED;
  }

  /* do the sel now */
  actkeys_sel_leftright(&ac, leftright, selectmode);

  /* set notifier that keyframe sel (and channels too) have changed */
  won_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SELECTED, nullptr);
  win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SELECTED, nullptr);

  return OP_FINISHED;
}

static int actkeys_sel_leftright_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  AnimCxt ac;
  short leftright = api_enum_get(op->ptr, "mode");

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* handle mode-based testing */
  if (leftright == ACTKEYS_LRSEL_TEST) {
    Scene *scene = ac.scene;
    ARgn *rgn = ac.rgn;
    View2D *v2d = &rgn->v2d;
    float x;

    /* determine which side of the current frame mouse is on */
    x = ui_view2d_rgn_to_view_x(v2d, ev->mval[0]);
    if (x < scene->r.cfra) {
      api_enum_set(op->ptr, "mode", ACTKEYS_LRSEL_LEFT);
    }
    else {
      api_enum_set(op->ptr, "mode", ACTKEYS_LRSEL_RIGHT);
    }
  }

  /* perform sel */
  return actkeys_sel_leftright_ex(C, op);
}

void action_ot_sel_leftright(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Sel Left/Right";
  ot->idname = "action_ot_sel_leftright";
  ot->description = "Sel keyframes to the left or the right of the current frame";

  /* api cbs */
  ot->invoke = actkeys_sel_leftright_invoke;
  ot->ex = actkeys_sel_leftright_ex;
  ot->poll = ed_op_action_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(
      ot->sapi, "mode", prop_actkeys_leftright_sel_types, ACTKEYS_LRSEL_TEST, "Mode", "");
  api_def_prop_flag(ot->prop, PROP_SKIP_SAVE);

  prop = api_def_bool(ot->sapi, "extend", false, "Extend Sel", "");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* Mouse-Click Sel Op
 * This op works in one of three ways:
 * - 1) keyframe under mouse - no special mods
 * - 2) all keyframes on the same side of current frame indicator as mouse - ALT mod
 * - 3) column sel all keyframes in frame under mouse - CTRL modifier
 * - 4) all keyframes in channel under mouse - CTRL+ALT modifiers
 *
 * In addition to these basic options, the SHIFT mod can be used to toggle the
 * sel mode between replacing the sel (without) and inverting the sel (with) */

/* option 1) sel keyframe directly under mouse */
static void actkeys_msel_single(AnimCxt *ac,
                                AnimListElem *ale,
                                short sel_mode,
                                float selx)
{
  KeyframeEditData ked = {{nullptr}};
  KeyframeEditFn sel_cb, ok_cb;

  /* get fns for sel keyframes */
  sel_cb = anim_editkeyframes_sel(sel_mode);
  ok_cb = anim_editkeyframes_ok(BEZT_OK_FRAME);
  ked.f1 = selx;
  ked.iterflags |= KED_F1_NLA_UNMAP;

  /* sel the nominated keyframe on the given frame */
  if (ale->type == ANIMTYPE_PLAYER) {
    ed_pen_sel_frame(static_cast<bGPDlayer *>(ale->data), selx, select_mode);
    ale->update |= ANIM_UPDATE_DEPS;
  }
  else if (ale->type == ANIMTYPE_PEN_LAYER) {
    dune::ed::pen::sel_frame_at(
        static_cast<PenLayer *>(ale->data)->wrap(), selx, select_mode);
    ale->update |= ANIM_UPDATE_DEPS;
  }
  else if (ale->type == ANIMTYPE_PEN_LAYER_GROUP) {
    dune::ed::en::sel_frames_at(
        static_cast<PenLayerTreeGroup *>(ale->data)->wrap(), selx, select_mode);
  }
  else if (ale->type == ANIMTYPE_PEN_DATABLOCK) {
    List anim_data = {nullptr, nullptr};
    eAnimFilterFlags filter;

    filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS);
    anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

    /* Loop over all keys that are represented by this data-block key. */
    LIST_FOREACH (AnimListElem *, ale2, &anim_data) {
      if ((ale2->type != ANIMTYPE_PEN_LAYER) || (ale2->id != ale->data)) {
        continue;
      }
      dune::ed::pen::sel_frame_at(
          static_cast<PenLayer *>(ale2->data)->wrap(), selx, select_mode);
      ale2->update |= ANIM_UPDATE_DEPS;
    }
  }
  else if (ale->type == ANIMTYPE_MASKLAYER) {
    ed_mask_sel_frame(static_cast<MaskLayer *>(ale->data), selx, select_mode);
  }
  else {
    if (ale->type == ANIMTYPE_SUMMARY && ale->datatype == ALE_ALL) {
      List anim_data = {nullptr, nullptr};
      eAnimFilterFlags filter;

      filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS);
      anim_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

      /* Loop over all keys that are represented by this summary key. */
      LIST_FOREACH (bmAnimListElem *, ale2, &anim_data) {
        switch (ale2->type) {
          case ANIMTYPE_PLAYER:
            ed_pen_sel_frame(static_cast<PenDataLayer *>(ale2->data), selx, sel_mode);
            ale2->update |= ANIM_UPDATE_DEPS;
            break;

          case ANIMTYPE_MASKLAYER:
            ed_mask_sel_frame(static_cast<MaskLayer *>(ale2->data), selx, sel_mode);
            break;

          case ANIMTYPE_PEN_LAYER:
            dune::ed::pen::sel_frame_at(
                static_cast<PenLayer *>(ale2->data)->wrap(), selx, sel_mode);
            ale2->update |= ANIM_UPDATE_DEPS;
            break;

          default:
            break;
        }
      }

      anim_animdata_update(ac, &anim_data);
      anim_animdata_freelist(&anim_data);
    }

    if (!ELEM(ac->datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
      anim_animchannel_keyframes_loop(&ked, ac->ads, ale, ok_cb, sel_cb, nullptr);
    }
  }
}

/* Option 2) Sel all the keyframes on either side of the current frame
 * (depends on which side the mouse is on) */
/* (see actkeys_sel_leftright) */

/* Option 3) Seld all visible keyframes in the same frame as the mouse click */
static void actkeys_msel_column(AnimCxt *ac, short select_mode, float selx)
{
  List anim_data = {nullptr, nullptr};
  eAnimFilterFlags filter;

  KeyframeEditFn sel_cb, ok_cb;
  KeyframeEditData ked = {{nullptr}};

  /* set up BezTriple edit callbacks */
  sel_cb = anim_editkeyframes_sel(sel_mode);
  ok_cb = anim_editkeyframes_ok(BEZT_OK_FRAME);

  /* loop through all of the keys and select additional keyframes
   * based on the keys found to be selected above */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS);
  anom_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    /* select elements with frame number matching cfra */
    if (ale->type == ANIMTYPE_PLAYER) {
      ed_pen_sel_frame(static_cast<PenDataLayer *>(ale->data), selx, sel_mode);
      ale->update |= ANIM_UPDATE_DEPS;
    }
    else if (ale->type == ANIMTYPE_MASKLAYER) {
      ee_mask_select_frame(static_cast<MaskLayer *>(ale->data), selx, sel_mode);
    }
    else if (ale->type == ANIMTYPE_PEN_LAYER) {
      dune::ed::pen::sel_frame_at(
          static_cast<PenLayer *>(ale->data)->wrap(), selx, sel_mode);
      ale->update |= ANIM_UPDATE_DEPS;
    }
    else {
      AnimData *adt = anim_nla_mapping_get(ac, ale);

      /* set frame for validation cb to refer to */
      if (adt) {
        ked.f1 = dune_nla_tweakedit_remap(adt, selx, NLATIME_CONVERT_UNMAP);
      }
      else {
        ked.f1 = selx;
      }
      
      anim_fcurve_keyframes_loop(
          &ked, static_cast<FCurve *>(ale->key_data), ok_cb, sel_cb, nullptr);
    }
  }

  /* free elements */
  lib_freelist(&ked.list);

  anim_animdata_update(ac, &anim_data);
  anim_animdata_freelist(&anim_data);
}

/* option 4) sel all keyframes in same channel */
static void actkeys_msel_channel_only(AnimCxt *ac, AnimListElem *ale, short sel_mode)
{
  KeyframeEditFn sel_cb;

  /* get fns for sel keyframes */
  sel_cb = anim_editkeyframes_sel(sel_mode);

  /* select all keyframes in this channel */
  if (ale->type == ANIMTYPE_PLAYER) {
    ed_pen_sel_frames(static_cast<PenDataLayer *>(ale->data), sel_mode);
    ale->update = ANIM_UPDATE_DEPS;
  }
  else if (ale->type == ANIMTYPE_MASKLAYER) {
    ed_mask_sel_frames(static_cast<MaskLayer *>(ale->data), sel_mode);
  }
  else if (ale->type == ANIMTYPE_PEN_LAYER) {
    dune::ed::pen::sel_all_frames(
        static_cast<PenLayer *>(ale->data)->wrap(), sel_mode);
    ale->update |= ANIM_UPDATE_DEPS;
  }
  else {
    if (ale->type == ANIMTYPE_SUMMARY && ale->datatype == ALE_ALL) {
      List anim_data = {nullptr, nullptr};
      eAnimFilterFlags filter;

      filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS);
      ANIM_animdata_filter(ac, &anim_data, filter, ac->data, eAnimContTypes(ac->datatype));

      LIST_FOREACH (AnimListElem *, ale2, &anim_data) {
        if (ale2->type == ANIMTYPE_PLAYER) {
         ed_pen_sel_frames(static_cast<PenLayer *>(ale2->data), sel_mode);
          ale2->update |= ANIM_UPDATE_DEPS;
        }
        else if (ale2->type == ANIMTYPE_MASKLAYER) {
          ed_mask_sel_frames(static_cast<MaskLayer *>(ale2->data), sel_mode);
        }
      }

      anim_animdata_update(ac, &anim_data);
      anim_animdata_freelist(&anim_data);
    }

    if (!elem(ac->datatype, ANIMCONT_PEN, ANIMCONT_MASK)) {
      anim_animchannel_keyframes_loop(nullptr, ac->ads, ale, nullptr, sel_cb, nullptr);
    }
  }
}

static int mouse_action_keys(AnimCxt *ac,
                             const int mval[2],
                             short sel_mode,
                             const bool desel_all,
                             const bool column,
                             const bool same_channel,
                             bool wait_to_desel_others)
{
  eAnimFilterFlags filter = ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE |
                             ANIMFILTER_LIST_CHANNELS;

  AnimListElem *ale = nullptr;
  bool found = false;
  bool is_sel = false;
  float frame = 0.0f; /* frame of keyframe under mouse - NLA corrections not applied/included */
  float selx = 0.0f;  /* frame of keyframe under mouse */
  int ret_val = OP_FINISHED;

  actkeys_find_key_at_position(
      ac, filter, mval[0], mval[1], &ale, &selx, &frame, &found, &is_selected);

  if (sel_mode != SEL_REPLACE) {
    wait_to_desel_others = false;
  }

  /* For replacing sel, if we have something to sel, we have to clear existing selection.
   * The same goes if we found nothing to select, and desel_all is true
   * (desel on nothing behavior). */
  if ((sel_mode == SEL_REPLACE && found) || (!found && desel_all)) {
    /* reset sel  mode for next steps */
    sell_mode = SEL_ADD;

    /* Rather than desel others, users may want to drag to box-sel (drag from empty space)
     * or tweak-translate an alrdy sel item. If these cases may apply, delay dese. */
    if (wait_to_desel_others && (!found || is_sel)) {
      ret_value = OP_RUNNING_MODAL;
    }
    else {
      /* desel all keyframes */
      desel_action_keys(ac, 0, SEL_SUBTRACT);

      /* highlight channel clicked on */
      if (ele(ac->datatype, ANIMCONT_ACTION, ANIMCONT_DOPESHEET, ANIMCONT_TIMELINE)) {
        /* deselect all other channels first */
        anim_anim_channels_sel_set(ac, ACHANNEL_SETFLAG_CLEAR);

        /* Highlight Action-Group or F-Curve? */
        if (ale != nullptr && ale->data) {
          if (ale->type == ANIMTYPE_GROUP) {
            ActionGroup *agrp = static_cast<ActionGroup *>(ale->data);

            agrp->flag |= AGRP_SELECTED;
            anim_set_active_channel(
                ac, ac->data, eAnimContTypes(ac->datatype), filter, agrp, ANIMTYPE_GROUP);
          }
          else if (elem(ale->type, ANIMTYPE_FCURVE, ANIMTYPE_NLACURVE)) {
            FCurve *fcu = static_cast<FCurve *>(ale->data);

            fcu->flag |= FCURVE_SELECTED;
            anim_set_active_channel(ac,
                                    ac->data,
                                    eAnimContTypes(ac->datatype),
                                    filter,
                                    fcu,
                                    eAnimChannelType(ale->type));
          }
          else if (ale->type == ANIMTYPE_PLAYER) {
            PenData *pd = (PenData *)ale->id;
            PenDataLayer *pdl = static_cast<PenDataLayer *>(ale->data);

            ed_pen_set_active_channel(pd, pdl);
          }
        }
      }
      else if (ac->datatype == ANIMCONT_PEN) {
        /* Desel all other channels first. */
        anim_channels_sel_set(ac, ACHANNEL_SETFLAG_CLEAR);

        /* Highlight the pen channel, and set the corresponding layer as active. */
        if (ale != nullptr && ale->data != nullptr && ale->type == ANIMTYPE_PEN_LAYER) {
          dune::ed::pen::sel_layer_channel(
              *reinterpret_cast<Pen *>(ale->id),
              static_cast<dune::pen::Layer *>(ale->data));
        }

        /* Highlight Pen Layer (Legacy). */
        if (ale != nullptr && ale->data != nullptr && ale->type == ANIMTYPE_PLAYER) {
          Pendata *pd = (PenData *)ale->id;
          PenDataLayer *pdl = static_cast<PenDataLayer *>(ale->data);

          ed_pen_set_active_channel(pd, pdl);
        }
      }
      else if (ac->datatype == ANIMCONT_MASK) {
        /* desel all other channels first */
        anim_anim_channels_sel_set(ac, ACHANNEL_SETFLAG_CLEAR);

        if (ale != nullptr && ale->data != nullptr && ale->type == ANIMTYPE_MASKLAYER) {
          MaskLayer *masklay = static_cast<MaskLayer *>(ale->data);

          masklay->flag |= MASK_LAYERFLAG_SEL;
        }
      }
    }
  }

  /* only sel keyframes if we clicked on a valid channel and hit something */
  if (ale != nullptr) {
    if (found) {
      /* apply sel to keyframes */
      if (column) {
        /* sel all keyframes in the same frame as the one we hit on the active channel
         * [#41077]: "frame" not "selx" here (i.e. no NLA corrections yet) as the code here
         * does that itself again as it needs to work on multiple data-blocks. */
        actkeys_msel_column(ac, sel_mode, frame);
      }
      else if (same_channel) {
        /* select all keyframes in the active channel */
        actkeys_msel_channel_only(ac, ale, sel_mode);
      }
      else {
        /* sel the nominated keyframe on the given frame */
        actkeys_msel_single(ac, ale, sel_mode, selx);
      }
    }

    /* flush tagged updates
     * We tmprrly add this channel back to the list so that this can happen  */
    List anim_data = {ale, ale};
    anim_animdata_update(ac, &anim_data);

    /* free this channel */
    mem_free(ale);
  }

  return ret_value;
}

/* handle clicking */
static int actkeys_clicksel_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  int ret_value;

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* get useful ptrs from anim cxt data */
  // rgn = ac.rgn; /* UNUSED. */
  /* sel mode is either replace (desel all, then add) or add/extend */
  const short selmode = api_bool_get(op->ptr, "extend") ? SEL_INVERT : SEL_REPLACE;
  const bool desel_all = api_bool_get(op->ptr, "desel_all");
  const bool wait_to_desel_others = aoi_bool_get(op->ptr, "wait_to_desel_others");
  int mval[2];

  /* column sel */
  const bool column = api_bool_get(op->ptr, "column");
  const bool channel = api_bool_get(op->ptr, "channel");

  mval[0] = api_int_get(op->ptr, "mouse_x");
  mval[1] = api_int_get(op->ptr, "mouse_y");

  /* Sel keyframe(s) based upon mouse position. */
  ret_val = mouse_action_keys(
      &ac, mval, selmode, desel_all, column, channel, wait_to_desel_others);

  /* set notifier that keyframe sel (and channels too) have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_SEL, nullptr);
  win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_SEL, nullptr);

  /* for tweak grab to work */
  return ret_val | OP_PASS_THROUGH;
}

void action_ot_clicksel(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Sel Keyframes";
  ot->idname = "action_ot_clicksel";
  ot->description = "Sel keyframes by clicking on them";

  /* cbs */
  ot->poll = ed_op_action_active;
  ot->ex = actkeys_clicksel_ex;
  ot->invoke = win_generic_sel_invoke;
  ot->modal = win_generic_sel_modal;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* props */
  win_op_props_generic_sel(ot);
  /* Key-map: Enable with `Shift`. */
  prop = api_def_bool(
      ot->sapi,
      "extend",
      false,
      "Extend Sel",
      "Toggle keyframe sel instead of leaving newly sel keyframes only");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  prop = api_def_bool(ot->sapi,
                         "desel_all",
                         false,
                         "Desel On Nothing",
                         "Desel all when nothing under the cursor");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  /* Key-map: Enable with `Alt`. */
  prop = api_def_bool(
      ot->sapi,
      "column",
      false,
      "Column Sel",
      "Sel all keyframes that occur on the same frame as the one under the mouse");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  /* Key-map: Enable with `Ctrl-Alt`. */
  prop = api_def_bool(ot->sapi,
                         "channel",
                         false,
                         "Only Channel",
                         "Sel all the keyframes in the channel under the mouse");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}
