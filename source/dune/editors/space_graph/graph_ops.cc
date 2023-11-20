#include <cmath>
#include <cstdlib>

#include "types_scene.h"

#include "lib_dunelib.h"
#include "lib_math_base.h"
#include "lib_utildefines.h"

#include "dune_cxt.hh"
#include "dune_global.h"

#include "ui_view2d.hh"

#include "ed_anim_api.hh"
#include "ed_screen.hh"
#include "ed_transform.hh"

#include "graph_intern.h"

#include "api_access.hh"
#include "api_define.hh"

#include "graph.hh"

#include "win_api.hh"
#include "win_types.hh"

/* view-based ops */
/* should these rly be here? */

/* Set Cursor
 * The 'cursor' in the Graph Editor consists of two parts:
 * 1) Current Frame Indicator (as per ANIM_OT_change_frame)
 * 2) Value Indicator (stored per Graph Editor instance) */

static bool graphview_cursor_poll(Cxt *C)
{
  /* prevent changes during render */
  if (G.is_rendering) {
    return false;
  }

  return ed_op_graphedit_active(C);
}

/* Set the new frame number */
static void graphview_cursor_apply(Cxt *C, WinOp *op)
{
  Scene *scene = cxt_data_scene(C);
  SpaceGraph *sipo = cxt_win_space_graph(C);
  /* this isn't technically "frame", but it'll do... */
  float frame = api_float_get(op->ptr, "frame");

  /* adjust the frame or the cursor x-value */
  if (sipo->mode == SIPO_MODE_DRIVERS) {
    /* adjust cursor x-val */
    sipo->cursorTime = frame;
  }
  else {
    /* adjust the frame
     * sync this part of code with ANIM_OT_change_frame */
    /* 1) frame is rounded to the nearest int, since frames are ints */
    scene->r.cfra = round_fl_to_int(frame);

    if (scene->r.flag & SCER_LOCK_FRAME_SEL) {
      /* Clip to preview range
       * Preview range won't go into negative val,
       * so only clamping once should be fine. */
      CLAMP(scene->r.cfra, PSFRA, PEFRA);
    }
    else {
      /* Prevent negative frames */
      FRAMENUMBER_MIN_CLAMP(scene->r.cfra);
    }

    scene->r.subframe = 0.0f;
    graph_id_tag_update(&scene->id, ID_RECALC_FRAME_CHANGE);
  }

  /* set the cursor val */
  sipo->cursorVal = api_float_get(op->ptr, "val");

  /* send notifiers - notifiers for frame should force an update for both vars ok... */
  win_ev_add_notifier(C, NC_SCENE | ND_FRAME, scene);
}

/* Non-modal cb for running op wo user input */
static int graphview_cursor_ex(Cxt *C, WinOp *op)
{
  graphview_cursor_apply(C, op);
  return OP_FINISHED;
}

/* set the op props from the initial ev */
static void graphview_cursor_setprops(Cxt *C, WinOp *op, const WinEv *ev)
{
  ARgn *rgn = cxt_win_rgn(C);
  float viewx, viewy;

  /* abort if not active rgn (should not really be possible) */
  if (rgm == nullptr) {
    return;
  }

  /* convert from rgn coords to View2D 'tot' space */
  ui_view2d_rgn_to_view(&rgn->v2d, ev->mval[0], ev->mval[1], &viewx, &viewy);

  /* store the vals in the op props */
  /* we don't clamp frame here, as it might be used for the drivers cursor */
  api_float_set(op->ptr, "frame", viewx);
  api_float_set(op->ptr, "value", viewy);
}

/* Modal Op init */
static int graphview_cursor_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  Screen *screen = cxt_win_screen(C);

  /* Change to frame that mouse is over before adding modal handler,
   * as user could click on a single frame (jump to frame) as well as
   * click-dragging over a range (modal scrubbing). Apply this change. */
  graphview_cursor_setprops(C, op, ev);
  graphview_cursor_apply(C, op);

  /* Signal that a scrubbing op is starting */
  if (screen) {
    screen->scrubbing = true;
  }

  /* add tmp handler */
  win_ev_add_modal_handler(C, op);
  return OP_RUNNING_MODAL;
}

/* Modal ev handling of cursor changing */
static int graphview_cursor_modal(Cxt *C, WinOp *op, const WinEv *ev)
{
  Screen *screen = cxt_win_screen(C);
  Scene *scene = cxt_data_scene(C);

  /* ex the evs */
  switch (ev->type) {
    case EV_ESCKEY:
      if (screen) {
        screen->scrubbing = false;
      }

      win_ev_add_notifier(C, NC_SCENE | ND_FRAME, scene);
      return OP_FINISHED;

    case MOUSEMOVE:
      /* set the new vals */
      graphview_cursor_setprops(C, op, ev);
      graphview_cursor_apply(C, op);
      break;

    case LEFTMOUSE:
    case RIGHTMOUSE:
    case MIDDLEMOUSE:
      /* We check for either mouse-btn to end, to work with all user keymaps. */
      if (ev->val == KM_RELEASE) {
        if (screen) {
          screen->scrubbing = false;
        }

        win_ev_add_notifier(C, NC_SCENE | ND_FRAME, scene);
        return OP_FINISHED;
      }
      break;
  }

  return OP_RUNNING_MODAL;
}

static void GRAPH_OT_cursor_set(WinOpType *ot)
{
  /* ids */
  ot->name = "Set Cursor";
  ot->idname = "GRAPH_OT_cursor_set";
  ot->description = "Interactively set the current frame and va cursor";

  /* api cbs */
  ot->ex = graphview_cursor_ex;
  ot->invoke = graphview_cursor_invoke;
  ot->modal = graphview_cursor_modal;
  ot->poll = graphview_cursor_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_GRAB_CURSOR_X | OPTYPE_UNDO;

  /* api */
  api_def_float(ot->sapi, "frame", 0, MINAFRAMEF, MAXFRAMEF, "Frame", "", MINAFRAMEF, MAXFRAMEF);
  api_def_float(ot->sapi, "val", 0, -FLT_MAX, FLT_MAX, "Val", "", -100.0f, 100.0f);

/* Hide/Reveal */
static int graphview_curves_hide_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  List anim_data = {nullptr, nullptr};
  List all_data = {nullptr, nullptr};
  int filter;
  const bool unsel = api_bool_get(op->ptr, "unsel");

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* get list of all channels that sel may need to be flushed to
   * hierarchy must not affect what we have access to here... */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_FCURVESONLY | ANIMFILTER_LIST_CHANNELS |
            ANIMFILTER_NODUPLIS);
  anim_animdata_filter(
      &ac, &all_data, eAnimFilterFlags(filter), ac.data, eAnimContTypes(ac.datatype));

  /* filter data
   * of the remaining visible curves, hide the ones that are
   * sel/unsel (depending on "unsel" prop) */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_FCURVESONLY | ANIMFILTER_CURVE_VISIBLE |
            ANIMFILTER_NODUPLIS);
  if (unsel) {
    filter |= ANIMFILTER_UNSEL;
  }
  else {
    filter |= ANIMFILTER_SEL;
  }

  anim_animdata_filter(
      &ac, &anim_data, eAnimFilterFlags(filter), ac.data, eAnimContTypes(ac.datatype));

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    /* hack: skip ob channels for now, since flushing those will always flush everything,
     * but they are always included */
    /* TODO: find out why this is the case, and fix that */
    if (ale->type == ANIMTYPE_OB) {
      continue;
    }

    /* change the hide setting, and unselect it... */
    anim_channel_setting_set(&ac, ale, ACHANNEL_SETTING_VISIBLE, ACHANNEL_SETFLAG_CLEAR);
    anim_channel_setting_set(&ac, ale, ACHANNEL_SETTING_SEL, ACHANNEL_SETFLAG_CLEAR);

    /* now, also flush sel status up/down as appropriate */
    ANIM_flush_setting_anim_channels(
        &ac, &all_data, ale, ACHANNEL_SETTING_VISIBLE, ACHANNEL_SETFLAG_CLEAR);
  }

  /* cleanup */
  anim_animdata_freelist(&anim_data);
  lib_freelist(&all_data);

  /* unhide sel */
  if (unsel) {
    /* turn off requirement for visible */
    filter = ANIMFILTER_SEL | ANIMFILTER_NODUPLIS | ANIMFILTER_LIST_CHANNELS |
             ANIMFILTER_FCURVESONLY;

    /* flushing has been done */
    anim_animdata_filter(
        &ac, &anim_data, eAnimFilterFlags(filter), ac.data, eAnimContTypes(ac.datatype));

    LIST_FOREACH (AnimListElem *, ale, &anim_data) {
      /* hack: skip ob channels for now, since flushing those
       * will always flush everything, but they are always included */

      /* TODO: find out why this is the case, and fix that */
      if (ale->type == ANIMTYPE_OB) {
        continue;
      }

      /* change the hide setting, and unselect it... */
      anim_channel_setting_set(&ac, ale, ACHANNEL_SETTING_VISIBLE, ACHANNEL_SETFLAG_ADD);
      anim_channel_setting_set(&ac, ale, ACHANNEL_SETTING_SELECT, ACHANNEL_SETFLAG_ADD);

      /* now, also flush sel status up/down as appropriate */
      anim_flush_setting_anim_channels(
          &ac, &anim_data, ale, ACHANNEL_SETTING_VISIBLE, ACHANNEL_SETFLAG_ADD);
    }
    anim_animdata_freelist(&anim_data);
  }

  /* send notifier that things have changed */
  win_ev_add_notifier(C, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, nullptr);

  return OP_FINISHED;
}

static void GRAPH_OT_hide(WinOpType *ot)
{
  /* ids */
  ot->name = "Hide Curves";
  ot->idname = "GRAPH_OT_hide";
  ot->description = "Hide sel curves from Graph Editor view";

  /* api cbs */
  ot->ex = graphview_curves_hide_ex;
  ot->poll = ed_op_graphedit_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_bool(
      ot->sapi, "unsel", false, "Unsel", "Hide unsel rather than sel curves");
}

static int graphview_curves_reveal_ex(Cxt *C, WinOp *op)
{
  AnimCxt ac;
  List anim_data = {nullptr, nullptr};
  List all_data = {nullptr, nullptr};
  int filter;
  const bool sel = api_bool_get(op->ptr, "sel");

  /* get editor data */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return OP_CANCELLED;
  }

  /* get list of all channels that sel may need to be flushed to
   * - hierarchy must not affect what we have access to here.. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_CHANNELS | ANIMFILTER_NODUPLIS |
            ANIMFILTER_FCURVESONLY);
  anim_animdata_filter(
      &ac, &all_data, eAnimFilter_Flags(filter), ac.data, eAnimContTypes(ac.datatype));

  /* filter data
   * - Traverse all visible channels, ensuring that everything is set to be curve-visible  */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS |
            ANIMFILTER_FCURVESONLY);
  anim_animdata_filter(
      &ac, &anim_data, eAnimFilterFlags(filter), ac.data, eAnimContTypes(ac.datatype));

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    /* hack: skip ob channels for now, since flushing those will always flush everything,
     * but they are always included. */
    /* TODO: find out why this is the case, and fix that */
    if (ale->type == ANIMTYPE_OB) {
      continue;
    }

    /* sel if it is not visible */
    if (anim_channel_setting_get(&ac, ale, ACHANNEL_SETTING_VISIBLE) == 0) {
      anim_channel_setting_set(&ac,
                               ale,
                               ACHANNEL_SETTING_SEL,
                               sel ? ACHANNEL_SETFLAG_ADD : ACHANNEL_SETFLAG_CLEAR);
    }

    /* change the visibility setting */
    anim_channel_setting_set(&ac, ale, ACHANNEL_SETTING_VISIBLE, ACHANNEL_SETFLAG_ADD);

    /* now, also flush selection status up/down as appropriate */
    anim_flush_setting_anim_channels(
        &ac, &all_data, ale, ACHANNEL_SETTING_VISIBLE, eAnimChannelsSetFlag(true));
  }

  /* cleanup */
  anim_animdata_freelist(&anim_data);
  lib_freelist(&all_data);

  /* send notifier that things have changed */
  win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN | NA_EDITED, nullptr);

  return OP_FINISHED;
}

static void GRAPH_OT_reveal(WinOpType *ot)
{
  /* ids */
  ot->name = "Reveal Curves";
  ot->idname = "GRAPH_OT_reveal";
  ot->description = "Make previously hidden curves visible again in Graph Editor view";

  /* api cbs */
  ot->ex = graphview_curves_reveal_ex;
  ot->poll = ed_op_graphedit_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->sapi, "sel", true, "Sel", "");
}

/* Registration: op types */
void graphedit_optypes()
{
  /* view */
  win_optype_append(GRAPH_OT_cursor_set);

  win_optype_append(GRAPH_OT_previewrange_set);
  win_optype_append(GRAPH_OT_view_all);
  win_optype_append(GRAPH_OT_view_sel);
  win_optype_append(GRAPH_OT_view_frame);

  win_optype_append(GRAPH_OT_ghost_curves_create);
  win_optype_append(GRAPH_OT_ghost_curves_clear);

  win_optype_append(GRAPH_OT_hide);
  win_optype_append(GRAPH_OT_reveal);

  /* keyframes */
  /* sel */
  win_optype_append(GRAPH_OT_clicksel);
  win_optype_append(GRAPH_OT_sel_all);
  win_optype_append(GRAPH_OT_sel_box);
  win_optype_append(GRAPH_OT_sel_lasso);
  win_optype_append(GRAPH_OT_sel_circle);
  win_optype_append(GRAPH_OT_sel_column);
  win_optype_append(GRAPH_OT_sel_linked);
  win_optype_append(GRAPH_OT_sel_more);
  win_optype_append(GRAPH_OT_sel_less);
  win_optype_append(GRAPH_OT_sel_leftright);
  win_optype_append(GRAPH_OT_sel_key_handles);

  /* editing */
  win_optype_append(GRAPH_OT_snap);
  win_optype_append(GRAPH_OT_equalize_handles);
  win_optype_append(GRAPH_OT_mirror);
  win_optype_append(GRAPH_OT_frame_jump);
  win_optype_append(GRAPH_OT_keyframe_jump);
  WM_optype_append(GRAPH_OT_snap_cursor_value);
  WM_optype_append(GRAPH_OT_handle_type);
  WM_optype_append(GRAPH_OT_interpolation_type);
  WM_optype_append(GRAPH_OT_extrapolation_type);
  WM_optype_append(GRAPH_OT_easing_type);
  WM_optype_append(GRAPH_OT_bake_keys);
  WM_optype_append(GRAPH_OT_keys_to_samples);
  WM_optype_append(GRAPH_OT_samples_to_keys);
  WM_optype_append(GRAPH_OT_sound_to_samples);
  win_optype_append(GRAPH_OT_smooth);
  win_optype_append(GRAPH_OT_clean);
  win_optype_append(GRAPH_OT_decimate);
  win_optype_append(GRAPH_OT_blend_to_neighbor);
  win_optype_append(GRAPH_OT_breakdown);
  win_optype_append(GRAPH_OT_ease);
  win_optype_append(GRAPH_OT_shear);
  win_optype_append(GRAPH_OT_scale_average);
  WM_optype_append(GRAPH_OT_blend_offset);
  WM_operatortype_append(GRAPH_OT_blend_to_ease);
  WM_operatortype_append(GRAPH_OT_match_slope);
  WM_operatortype_append(GRAPH_OT_time_offset);
  WM_operatortype_append(GRAPH_OT_blend_to_default);
  WM_operatortype_append(GRAPH_OT_push_pull);
  WM_operatortype_append(GRAPH_OT_gaussian_smooth);
  WM_operatortype_append(GRAPH_OT_butterworth_smooth);
  WM_operatortype_append(GRAPH_OT_euler_filter);
  WM_operatortype_append(GRAPH_OT_delete);
  WM_operatortype_append(GRAPH_OT_duplicate);

  WM_operatortype_append(GRAPH_OT_copy);
  WM_operatortype_append(GRAPH_OT_paste);

  WM_operatortype_append(GRAPH_OT_keyframe_insert);
  WM_operatortype_append(GRAPH_OT_click_insert);

  /* F-Curve Modifiers */
  WM_operatortype_append(GRAPH_OT_fmodifier_add);
  WM_operatortype_append(GRAPH_OT_fmodifier_copy);
  WM_operatortype_append(GRAPH_OT_fmodifier_paste);

  /* Drivers */
  WM_operatortype_append(GRAPH_OT_driver_variables_copy);
  WM_operatortype_append(GRAPH_OT_driver_variables_paste);
  WM_operatortype_append(GRAPH_OT_driver_delete_invalid);
}

void ED_operatormacros_graph()
{
  wmOperatorType *ot;
  wmOperatorTypeMacro *otmacro;

  ot = win_optype_append_macro("GRAPH_OT_dup_move",
                                    "Duplicate",
                                    "Make a copy of all sel keyframes and move them",
                                    OPTYPE_UNDO | OPTYPE_REGISTER);
  win_optype_macro_define(ot, "GRAPH_OT_duplicate");
  otmacro = win_optype_macro_define(ot, "TRANSFORM_OT_translate");
  api_bool_set(otmacro->ptr, "use_dup_keyframes", true);
  api_bool_set(otmacro->ptr, "use_proportional_edit", false);
}

/* Registration: Key-Maps */
void graphedit_keymap(WinKeyConfig *keyconf)
{
  /* keymap for all rgns */
  win_keymap_ensure(keyconf, "Graph Editor Generic", SPACE_GRAPH, RGN_TYPE_WIN);

  /* channels */
  /* Channels are not directly handled by the Graph Editor module,
   * but are inherited from the Animation module.
   * All the relevant operations, keymaps, drawing, etc.
   * can therefore all be found in that module instead,
   * as these are all used for the Graph Editor too.
   */

  /* keyframes */
  WM_keymap_ensure(keyconf, "Graph Editor", SPACE_GRAPH, RGN_TYPE_WINDOW);
}

/** \} */
