#include <cmath>
#include <cstring>

#include "AS_asset_representation.hh"

#include "mem_guardedalloc.h"

#include "lib_string.h"

#include "lang.h"

#include "types_armature.h"

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_animsys.h"
#include "dune_armature.hh"
#include "dune_context.hh"
#include "dune_lib_id.h"
#include "dune_ob.hh"
#include "dune_pose_backup.h"
#include "dune_report.h"

#include "graph.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_prototypes.h"

#include "win_api.hh"
#include "win_types.hh"

#include "ui.hh"

#include "ed_asset.hh"
#include "ed_keyframing.hh"
#include "ed_screen.hh"
#include "wd_util.hh"

#include "dune_bone_collections.hh"
#include "anim_keyframing.hh"

#include "armature_intern.h"

enum ePoseBlendState {
  POSE_BLEND_INIT,
  POSE_BLEND_BLENDING,
  POSE_BLEND_ORIGINAL,
  POSE_BLEND_CONFIRM,
  POSE_BLEND_CANCEL,
};

struct PoseBlendData {
  ePoseBlendState state;
  bool needs_redrw;

  struct {
    bool use_release_confirm;
    int init_ev_type;

  } release_confirm_info;

  /* For temp-loading the Action from the pose library. */
  AssetTmpIdConsumer *tmp_id_consumer;

  /* Blend factor for interpolating between current and given pose.
   * 1.0 means "100% pose asset". Negative vals and vals > 1.0 will be used as-is, and can
   * cause interesting effects. */
  float blend_factor;
  bool is_flipped;
  PoseBackup *pose_backup;

  Ob *ob;           /* Object to work on. */
  Action *act;         /* Pose to blend into the current pose. */
  Action *act_flipped; /* Flipped copy of `act`. */

  Scene *scene;  /* For auto-keying. */
  ScrArea *area; /* For drawing status text. */

  tSlider *slider; /* Slider UI and event handling. */

  /* Info-txt to print in header. */
  char headerstr[UI_MAX_DRW_STR];
};

/* Return the Action that should be blended.
 * This is either `pbd->act` or `pbd->act_flipped`, depending on `is_flipped`. */
static Action *poselib_action_to_blend(PoseBlendData *pbd)
{
  return pbd->is_flipped ? pbd->act_flipped : pbd->act;
}

/* Makes a copy of the current pose for restoration purposes - doesn't do constraints currently */
static void poselib_backup_posecopy(PoseBlendData *pbd)
{
  const Action *action = poselib_action_to_blend(pbd);
  pbd->pose_backup = dune_pose_backup_create_sel_bones(pbd->ob, action);

  if (pbd->state == POSE_BLEND_INIT) {
    /* Ready for blending now. */
    pbd->state = POSE_BLEND_BLENDING;
  }
}

/* Auto-key/tag bones affected by the pose Action. */
static void poselib_keytag_pose(Cxt *C, Scene *scene, PoseBlendData *pbd)
{
  if (!dune::animrig::autokeyframe_cfra_can_key(scene, &pbd->ob->id)) {
    return;
  }

  AnimData *adt = dune_animdata_from_id(&pbd->ob->id);
  if (adt != nullptr && adt->action != nullptr &&
      !dune_id_is_editable(cxt_data_main(C), &adt->action->id))
  {
    /* Changes to linked-in Actions are not allowed. */
    return;
  }

  Pose *pose = pbd->ob->pose;
  Action *act = poselib_action_to_blend(pbd);

  KeyingSet *ks = anim_get_keyingset_for_autokeying(scene, ANIM_KS_WHOLE_CHARACTER_ID);
  dune::Vector<ApiPtr> sources;

  /* start tagging/keying */
  const Armature *armature = static_cast<const Armature *>(pbd->ob->data);
  LIST_FOREACH (ActionGroup *, agrp, &act->groups) {
    /* Only for sel bones unless there aren't any selected, in which case all are included. */
    PoseChannel *pchan = dune_pose_channel_find_name(pose, agrp->name);
    if (pchan == nullptr) {
      continue;
    }

    if (dune_pose_backup_is_sel_relevant(pbd->pose_backup) &&
        !PBONE_SEL(armature, pchan->bone))
    {
      continue;
    }

    /* Add data-source override for the PoseChannel, to be used later. */
    anim_relative_keyingset_add_src(sources, &pbd->ob->id, &ApiPoseBone, pchan);
  }

  /* Perform actual auto-keying. */
  anim_apply_keyingset(C, &sources, ks, MODIFYKEY_MODE_INSERT, float(scene->r.cfra));

  /* send notifiers for this */
  win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_EDITED, nullptr);
}

/* Apply the relevant changes to the pose */
static void poselib_blend_apply(Cxt *C, WinOp *op)
{
  PoseBlendData *pbd = (PoseBlendData *)op->customdata;

  if (!pbd->needs_redrw) {
    return;
  }
  pbd->needs_redrw = false;

  dune_pose_backup_restore(pbd->pose_backup);

  /* The pose needs updating, whether it's for restoring the original pose or for showing the
   * result of the blend. */
  graph_id_tag_update(&pbd->ob->id, ID_RECALC_GEOMETRY);
  win_ev_add_notifier(C, NC_OB | ND_POSE, pbd->ob);

  if (pbd->state != POSE_BLEND_BLENDING) {
    return;
  }

  /* Perform the actual blending. */
  Graph *graph = cxt_data_graph_ptr(C);
  AnimEvalCxt anim_eval_cxt = dune_animsys_eval_cxt_construct(graph, 0.0f);
  Action *to_blend = poselib_action_to_blend(pbd);
  dune_pose_apply_action_blend(pbd->ob, to_blend, &anim_eval_cxt, pbd->blend_factor);
}

static void poselib_blend_set_factor(PoseBlendData *pbd, const float new_factor)
{
  pbd->blend_factor = new_factor;
  pbd->needs_redrw = true;
}

static void poselib_set_flipped(PoseBlendData *pbd, const bool new_flipped)
{
  if (pbd->is_flipped == new_flipped) {
    return;
  }

  /* The pose will toggle between flipped and normal. This means the pose
   * backup has to change, as it only contains the bones for one side. */
  dune_pose_backup_restore(pbd->pose_backup);
  dune_pose_backup_free(pbd->pose_backup);

  pbd->is_flipped = new_flipped;
  pbd->needs_redrw = true;

  poselib_backup_posecopy(pbd);
}

/* Return op return val. */
static int poselib_blend_handle_ev(Cxt * /*C*/, WinOp *op, const WinEv *ev)
{
  PoseBlendData *pbd = static_cast<PoseBlendData *>(op->customdata);

  ed_slider_modal(pbd->slider, ev);
  const float factor = ed_slider_factor_get(pbd->slider);
  poselib_blend_set_factor(pbd, factor);

  if (ev->type == MOUSEMOVE) {
    return OP_RUNNING_MODAL;
  }

  /* Handle the release confirm event directly, it has priority over others. */
  if (pbd->release_confirm_info.use_release_confirm &&
      (event->type == pbd->release_confirm_info.init_ev_type) && (ev->val == KM_RELEASE))
  {
    pbd->state = POSE_BLEND_CONFIRM;
    return OP_RUNNING_MODAL;
  }

  /* Ctrl manages the 'flipped' state. */
  poselib_set_flipped(pbd, ev->mod & KM_CTRL);

  /* only accept 'press' event, and ignore 'release', so that we don't get double actions */
  if (ELEM(ev->val, KM_PRESS, KM_NOTHING) == 0) {
    return OP_RUNNING_MODAL;
  }

  /* NORMAL EV HANDLING... */
  /* search takes priority over normal activity */
  switch (ev->type) {
    /* Exit - cancel. */
    case EV_ESCKEY:
    case RIGHTMOUSE:
      pbd->state = POSE_BLEND_CANCEL;
      break;

    /* Exit - confirm. */
    case LEFTMOUSE:
    case EV_RETKEY:
    case EV_PADENTER:
    case EV_SPACEKEY:
      pbd->state = POSE_BLEND_CONFIRM;
      break;

    /* TODO: toggle between original pose and poselib pose. */
    case EV_TABKEY:
      pbd->state = pbd->state == POSE_BLEND_BLENDING ? POSE_BLEND_ORIGINAL : POSE_BLEND_BLENDING;
      pbd->needs_redrw = true;
      break;
  }

  return OP_RUNNING_MODAL;
}

static Ob *get_poselib_ob(Cxt *C)
{
  if (C == nullptr) {
    return nullptr;
  }
  return dune_ob_pose_armature_get(cxt_data_active_ob(C));
}

static void poselib_tmpload_exit(PoseBlendData *pbd)
{
  ed_asset_tmp_id_consumer_free(&pbd->tmp_id_consumer);
}

static Action *poselib_blend_init_get_action(Cxt *C, WinOp *op)
{
  const AssetRepresentationHandle *asset = cxt_win_asset(C);

  PoseBlendData *pbd = static_cast<PoseBlendData *>(op->customdata);

  pbd->tmp_id_consumer = ed_asset_tmp_id_consumer_create(asset);
  return (Action *)ed_asset_tmp_id_consumer_ensure_local_id(
      pbd->tmp_id_consumer, ID_AC, cxt_data_main(C), op->reports);
}

static Action *flip_pose(Cxt *C, Ob *ob, Action *action)
{
  Action *action_copy = (Action *)dune_id_copy_ex(
      nullptr, &action->id, nullptr, LIB_ID_COPY_LOCALIZE);

  /* Lock the win while flipping the pose. Flipping requires tmp modding the
   * pose, which can cause unwanted visual glitches. */
  Win *win = cxt_win(C);
  const bool ui_was_locked = cxt_win_ui_locked(C);
  win_set_locked_ui(win, true);

  dune_action_flip_with_pose(action_copy, ob);

  win_set_locked_ui(win, interface_was_locked);
  return action_copy;
}

/* Return true on success, false if the cxt isn't suitable. */
static bool poselib_blend_init_data(Cxt *C, WinOp *op, const WinEv *ev)
{
  op->customdata = nullptr;

  /* check if valid poselib */
  Ob *ob = get_poselib_object(C);
  if (ELEM(nullptr, ob, ob->pose, ob->data)) {
    dune_report(op->reports, RPT_ERROR, TIP_("Pose lib is only for armatures in pose mode"));
    return false;
  }

  /* Set up blend state info. */
  PoseBlendData *pbd;
  op->customdata = pbd = static_cast<PoseBlendData *>(
      mem_calloc(sizeof(PoseBlendData), "PoseLib Preview Data"));

  pbd->act = poselib_blend_init_get_action(C, op);
  if (pbd->act == nullptr) {
    return false;
  }

  pbd->is_flipped = api_bool_get(op->ptr, "flipped");
  pbd->blend_factor = api_float_get(op->ptr, "blend_factor");

  /* Only construct the flipped pose if there is a chance it's actually needed. */
  const bool is_interactive = (ev != nullptr);
  if (is_interactive || pbd->is_flipped) {
    pbd->act_flipped = flip_pose(C, ob, pbd->act);
  }

  /* Get the basic data. */
  pbd->ob = ob;
  pbd->ob->pose = ob->pose;

  pbd->scene = cxt_data_scene(C);
  pbd->area = cxt_win_area(C);

  pbd->state = POSE_BLEND_INIT;
  pbd->needs_redrw = true;

  /* Just to avoid a clang-analyzer warning (false positive), it's set properly below. */
  pbd->release_confirm_info.use_release_confirm = false;

  /* Release confirm data. Only available if there's an event to work with. */
  if (is_interactive) {
    ApiProp *release_confirm_prop = api_struct_find_prop(op->ptr, "release_confirm");
    if (release_confirm_prop && api_prop_is_set(op->ptr, release_confirm_prop)) {
      pbd->release_confirm_info.use_release_confirm = api_prop_bool_get(
          op->ptr, release_confirm_prop);
    }
    else {
      pbd->release_confirm_info.use_release_confirm = event->val != KM_RELEASE;
    }

    pbd->slider = ed_slider_create(C);
    ed_slider_init(pbd->slider, ev);
    ed_slider_factor_set(pbd->slider, pbd->blend_factor);
    ed_slider_allow_overshoot_set(pbd->slider, true, true);
    ed_slider_allow_increments_set(pbd->slider, false);
    ed_slider_factor_bounds_set(pbd->slider, -1, 1);
  }

  if (pbd->release_confirm_info.use_release_confirm) {
    lib_assert(is_interactive);
    pbd->release_confirm_info.init_ev_type = win_userdef_ev_type_from_keymap_type(
        event->type);
  }

  /* Make backups for blending and restoring the pose. */
  poselib_backup_posecopy(pbd);

  /* Set pose flags to ensure the depsgraph evaluation doesn't overwrite it. */
  pbd->ob->pose->flag &= ~POSE_DO_UNLOCK;
  pbd->ob->pose->flag |= POSE_LOCKED;

  return true;
}

static void poselib_blend_cleanup(Cxt *C, WinOp *op)
{
  PoseBlendData *pbd = static_cast<PoseBlendData *>(op->customdata);
  Win *win = cxt_win(C);

  /* Redrw the header so that it doesn't show any of our stuff anymore. */
  ed_area_status_txt(pbd->area, nullptr);
  ed_workspace_status_txt(C, nullptr);

  if (pbd->slider) {
    ed_slider_destroy(C, pbd->slider);
  }

  /* This signals the graph to unlock and reevaluate the pose on the next evaluation. */
  Pose *pose = pbd->ob->pose;
  pose->flag |= POSE_DO_UNLOCK;

  switch (pbd->state) {
    case POSE_BLEND_CONFIRM: {
      Scene *scene = pbd->scene;
      poselib_keytag_pose(C, scene, pbd);

      /* Ensure the redo panel has the actually-used value, instead of the initial value. */
      api_float_set(op->ptr, "blend_factor", pbd->blend_factor);
      api_bool_set(op->ptr, "flipped", pbd->is_flipped);
      break;
    }

    case POSE_BLEND_INIT:
    case POSE_BLEND_BLENDING:
    case POSE_BLEND_ORIGINAL:
      /* Cleanup should not be called directly from these states. */
      lib_assert_msg(0, "poselib_blend_cleanup: unexpected pose blend state");
      dune_report(op->reports, RPT_ERROR, "Internal pose lib error, canceling operator");
      ATTR_FALLTHROUGH;
    case POSE_BLEND_CANCEL:
      dune_pose_backup_restore(pbd->pose_backup);
      break;
  }

  graoh_id_tag_update(&pbd->ob->id, ID_RECALC_GEOMETRY);
  win_ev_add_notifier(C, NC_OB | ND_POSE, pbd->ob);
  /* Update mouse-hover highlights. */
  win_ev_add_mousemove(win);
}

static void poselib_blend_free(WinOp *op)
{
  PoseBlendData *pbd = static_cast<PoseBlendData *>(op->customdata);
  if (pbd == nullptr) {
    return;
  }

  if (pbd->act_flipped) {
    dune_id_free(nullptr, pbd->act_flipped);
  }
  poselib_tmpload_exit(pbd);

  /* Free temp data for op */
  dune_pose_backup_free(pbd->pose_backup);
  pbd->pose_backup = nullptr;

  MEM_SAFE_FREE(op->customdata);
}

static int poselib_blend_exit(Cxt *C, WinOp *op)
{
  PoseBlendData *pbd = static_cast<PoseBlendData *>(op->customdata);
  const ePoseBlendState exit_state = pbd->state;

  poselib_blend_cleanup(C, op);
  poselib_blend_free(op);

  Win *win = cxt_win(C);
  win_cursor_modal_restore(win);

  if (exit_state == POSE_BLEND_CANCEL) {
    return OP_CANCELLED;
  }
  return OP_FINISHED;
}

/* Cancel previewing operation (called when exiting Blender) */
static void poselib_blend_cancel(Cxt *C, WinOp *op)
{
  PoseBlendData *pbd = static_cast<PoseBlendData *>(op->customdata);
  pbd->state = POSE_BLEND_CANCEL;
  poselib_blend_exit(C, op);
}

/* Main modal status check. */
static int poselib_blend_modal(Cxt *C, WinOp *op, const WinEv *ev)
{
  const int op_result = poselib_blend_handle_ev(C, op, ev);

  const PoseBlendData *pbd = static_cast<const PoseBlendData *>(op->customdata);
  if (ELEM(pbd->state, POSE_BLEND_CONFIRM, POSE_BLEND_CANCEL)) {
    return poselib_blend_exit(C, op);
  }

  if (pbd->needs_redrw) {
    char status_string[UI_MAX_DRW_STR];
    char slider_string[UI_MAX_DRW_STR];
    char tab_string[50];

    ed_slider_status_string_get(pbd->slider, slider_string, sizeof(slider_string));

    if (pbd->state == POSE_BLEND_BLENDING) {
      STRNCPY(tab_string, TIP_("[Tab] - Show original pose"));
    }
    else {
      STRNCPY(tab_string, TIP_("[Tab] - Show blended pose"));
    }

    SNPRINTF(status_string, "%s | %s | [Ctrl] - Flip Pose", tab_string, slider_string);
    ed_workspace_status_txt(C, status_string);

    poselib_blend_apply(C, op);
  }

  return op_result;
}

/* Modal Op init. */
static int poselib_blend_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  if (!poselib_blend_init_data(C, op, ev)) {
    poselib_blend_free(op);
    return OP_CANCELLED;
  }

  wmWindow *win = cxt_win_win(C);
  WM_cursor_modal_set(win, WM_CURSOR_EW_SCROLL);

  /* Do init apply to have something to look at. */
  poselib_blend_apply(C, op);

  win_ev_add_modal_handler(C, op);
  return OP_RUNNING_MODAL;
}

/* Single-shot apply. */
static int poselib_blend_ex(Cxt *C, WinOp *op)
{
  if (!poselib_blend_init_data(C, op, nullptr)) {
    poselib_blend_free(op);
    return OPERATOR_CANCELLED;
  }

  poselib_blend_apply(C, op);

  PoseBlendData *pbd = static_cast<PoseBlendData *>(op->customdata);
  pbd->state = POSE_BLEND_CONFIRM;
  return poselib_blend_exit(C, op);
}

static bool poselib_asset_in_cxt(Cxt *C)
{
  /* Check whether the cxt provides the asset data needed to add a pose. */
  const AssetRepresentationHandle *asset = cxt_win_asset(C);
  return asset && (asset->get_id_type() == ID_AC);
}

/* Poll cb for ops that require existing PoseLib data (with poses) to work. */
static bool poselib_blend_poll(Cxt *C)
{
  Ob *ob = get_poselib_ob(C);
  if (ELEM(nullptr, ob, ob->pose, ob->data)) {
    /* Pose lib is only for armatures in pose mode. */
    return false;
  }

  return poselib_asset_in_cxt(C);
}

void POSELIB_OT_apply_pose_asset(WinOpType *ot)
{
  ApiProp *prop;

  /* Ids: */
  ot->name = "Apply Pose Asset";
  ot->idname = "POSELIB_OT_apply_pose_asset";
  ot->description = "Apply the given Pose Action to the rig";

  /* Cbs: */
  ot->ex = poselib_blend_ex;
  ot->poll = poselib_blend_poll;

  /* Flags: */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Props: */
  api_def_float_factor(ot->sapi,
                       "blend_factor",
                       1.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "Blend Factor",
                       "Amount that the pose is applied on top of the existing poses. A negative "
                       "val will subtract the pose instead of adding it",
                       -1.0f,
                       1.0f);
  prop = api_def_bool(ot->sapi,
                         "flipped",
                         false,
                         "Apply Flipped",
                         "When enabled, applies the pose flipped over the X-axis");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

void POSELIB_OT_blend_pose_asset(WinOpType *ot)
{
  ApiProp *prop;

  /* Ids: */
  ot->name = "Blend Pose Asset";
  ot->idname = "POSELIB_OT_blend_pose_asset";
  ot->description = "Blend the given Pose Action to the rig";

  /* Cbs: */
  ot->invoke = poselib_blend_invoke;
  ot->modal = poselib_blend_modal;
  ot->cancel = poselib_blend_cancel;
  ot->exec = poselib_blend_exec;
  ot->poll = poselib_blend_poll;

  /* Flags: */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_BLOCKING | OPTYPE_GRAB_CURSOR_X;

  /* Properties: */
  prop = api_def_float_factor(ot->saoi,
                              "blend_factor",
                              0.0f,
                              -FLT_MAX,
                              FLT_MAX,
                              "Blend Factor",
                              "Amount that the pose is applied on top of the existing poses. A "
                              "negative value will subtract the pose instead of adding it",
                              -1.0f,
                              1.0f);
  /* Blending should always start at 0%, and not at whatever percent was last used. This api
   * prop exists for symmetry w the Apply op (and thus simplicity of the rest of
   * the code, which can assume this prop exists). */
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  prop = api_def_bool(ot->sapi,
                         "flipped",
                         false,
                         "Apply Flipped",
                         "When enabled, applies the pose flipped over the X-axis");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  prop = api_def_bool(ot->sapi,
                         "release_confirm",
                         false,
                         "Confirm on Release",
                         "Always confirm op when releasing btn");
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
}
