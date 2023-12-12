/* Pose Mode API's and Ops for Pose Mode armatures. */

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_math_vector.h"

#include "lang.h"

#include "types_anim.h"
#include "types_armature.h"
#include "types_ob.h"
#include "types_scene.h"

#include "dune_action.h"
#include "dune_anim_visualization.h"
#include "dune_armature.hh"
#include "dune_cxt.hh"
#include "dune_deform.h"
#include "dune_global.h"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_main.hh"
#include "dune_ob.hh"
#include "dune_report.h"
#include "dune_scene.h"

#include "graph.hh"
#include "graph_query.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_enum_types.hh"
#include "api_prototypes.h"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_anim_api.hh"
#include "ed_armature.hh"
#include "ed_keyframing.hh"
#include "ed_ob.hh"
#include "ed_screen.hh"
#include "ed_view3d.hh"

#include "anim_bone_collections.hh"
#include "anim_keyframing.hh"

#include "ui.hh"

#include "armature_intern.h"

#undef DEBUG_TIME

#include "PIL_time.h"
#ifdef DEBUG_TIME
#  include "PIL_time_utildefines.h"
#endif

Ob *ed_pose_ob_from_cxt(Cxt *C)
{
  /* matches logic w ed_op_posemode_cxt(). */
  ScrArea *area = cxt_win_area(C);
  Ob *ob;

  /* Since this call may also be used from the btns win,
   * we need to check for where to get the ob. */
  if (area && area->spacetype == SPACE_PROPS) {
    ob = ed_ob_cxt(C);
  }
  else {
    ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));
  }

  return ob;
}

bool ed_ob_posemode_enter_ex(Main *main, Ob *ob)
{
  lib_assert(dune_id_is_editable(main, &ob->id));
  bool ok = false;

  switch (ob->type) {
    case OB_ARMATURE:
      ob->restore_mode = ob->mode;
      ob->mode |= OB_MODE_POSE;
      /* Inform all CoW versions that we changed the mode. */
      graph_id_tag_update_ex(main, &ob->id, ID_RECALC_COPY_ON_WRITE);
      ok = true;

      break;
    default:
      break;
  }

  return ok;
}
bool ed_ob_posemode_enter(Cxt *C, Ob *ob)
{
  ReportList *reports = cxt_win_reports(C);
  Main *main = cxt_data_main(C);
  if (!dune_id_is_editable(main, &ob->id)) {
    dune_report(reports, RPT_WARNING, "Cannot pose libdata");
    return false;
  }
  bool ok = ed_ob_posemode_enter_ex(main, ob);
  if (ok) {
    win_ev_add_notifier(C, NC_SCENE | ND_MODE | NS_MODE_POSE, nullptr);
  }
  return ok;
}

bool ed_ob_posemode_exit_ex(Main *main, Ob *ob)
{
  bool ok = false;
  if (ob) {
    ob->restore_mode = ob->mode;
    ob->mode &= ~OB_MODE_POSE;

    /* Inform all CoW versions that we changed the mode. */
    graph_id_tag_update_ex(main, &ob->id, ID_RECALC_COPY_ON_WRITE);
    ok = true;
  }
  return ok;
}
bool ed_ob_posemode_exit(Cxt *C, Ob *ob)
{
  Main *main = cxt_data_main(C);
  bool ok = ed_ob_posemode_exit_ex(main, ob);
  if (ok) {
    win_ev_add_notifier(C, NC_SCENE | ND_MODE | NS_MODE_OB, nullptr);
  }
  return ok;
}

/* Motion Paths */
static eAnimvizCalcRange pose_path_convert_range(ePosePathCalcRange range)
{
  switch (range) {
    case POSE_PATH_CALC_RANGE_CURRENT_FRAME:
      return ANIMVIZ_CALC_RANGE_CURRENT_FRAME;
    case POSE_PATH_CALC_RANGE_CHANGED:
      return ANIMVIZ_CALC_RANGE_CHANGED;
    case POSE_PATH_CALC_RANGE_FULL:
      return ANIMVIZ_CALC_RANGE_FULL;
  }
  return ANIMVIZ_CALC_RANGE_FULL;
}

void ed_pose_recalc_paths(Cxt *C, Scene *scene, Ob *ob, ePosePathCalcRange range)
{
  /* Transform doesn't always have cxt available to do update. */
  if (C == nullptr) {
    return;
  }

  Main *main = cxt_data_main(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);

  Graph *graph;
  bool free_graph = false;

  List targets = {nullptr, nullptr};
  /* set flag to force recalc, then grab the relevant bones to target */
  ob->pose->avs.recalc |= ANIMVIZ_RECALC_PATHS;
  animviz_get_ob_motionpaths(ob, &targets);

/* recalculate paths, then free */
#ifdef DEBUG_TIME
  TIMEIT_START(pose_path_calc);
#endif

  /* For a single frame update it's faster to re-use existing dependency graph and avoid overhead
   * of building all the relations and so on for a tmp one. */
  if (range == POSE_PATH_CALC_RANGE_CURRENT_FRAME) {
    /* Graph will be eval at all the frames, but we first need to access some
     * nested ptrs, like anim data. */
    graph = cxt_data_ensure_eval_graph(C);
    free_graph = false;
  }
  else {
    graph = animviz_graph_build(main, scene, view_layer, &targets);
    free_graph = true;
  }

  animviz_calc_motionpaths(
      graph, main, scene, &targets, pose_path_convert_range(range), !free_graph);

#ifdef DEBUG_TIME
  TIMEIT_END(pose_path_calc);
#endif

  lib_freelist(&targets);

  if (range != POSE_PATH_CALC_RANGE_CURRENT_FRAME) {
    /* Tag armature object for copy on write so paths will drw/redrw.
     * For currently frame only we update eval ob directly. */
    graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
  }

  /* Free tmp graph. */
  if (free_graph) {
    graph_free(graph);
  }
}

/* show popup to determine settings */
static int pose_calc_paths_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  Ob *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));

  if (ELEM(nullptr, ob, ob->pose)) {
    return OP_CANCELLED;
  }

  /* set default settings from existing/stored settings */
  {
    AnimVizSettings *avs = &ob->pose->avs;

    ApiPtr avs_ptr = api_ptr_create(nullptr, &RNA_AnimVizMotionPaths, avs);
    api_enum_set(op->ptr, "display_type", RNA_enum_get(&avs_ptr, "type"));
    api_enum_set(op->ptr, "range", RNA_enum_get(&avs_ptr, "range"));
    api_enum_set(op->ptr, "bake_location", RNA_enum_get(&avs_ptr, "bake_location"));
  }

  /* show popup dialog to allow editing of range... */
  /* FIXME: hard-coded dimensions here are just arbitrary. */
  return win_op_props_dialog_popup(C, op, 270);
}

/* For the ob with pose/action: create path curves for selected bones
 * This recalcs the WHOLE path within the `pchan->pathsf` and `pchan->pathef` range. */
static int pose_calc_paths_ex(Cxt *C, WinOp *op)
{
  Ob *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));
  Scene *scene = cxt_data_scene(C);

  if (ELEM(nullptr, ob, ob->pose)) {
    return OP_CANCELLED;
  }

  /* grab baking settings from op settings */
  {
    bAnimVizSettings *avs = &ob->pose->avs;

    avs->path_type = api_enum_get(op->ptr, "display_type");
    avs->path_range = api_enum_get(op->ptr, "range");
    animviz_motionpath_compute_range(ob, scene);

    ApiPtr avs_ptr = api_ptr_create(nullptr, &ApiAnimVizMotionPaths, avs);
    api_enum_set(&avs_ptr, "bake_location", api_enum_get(op->ptr, "bake_location"));
  }

  /* set up path data for bones being calculated */
  CXT_DATA_BEGIN (C, PoseChannel *, pchan, sel_pose_bones_from_active_ob) {
    /* verify makes sure that the selected bone has a bone with the appropriate settings */
    animviz_verify_motionpaths(op->reports, scene, ob, pchan);
  }
  CTX_DATA_END;

#ifdef DEBUG_TIME
  TIMEIT_START(recalc_pose_paths);
#endif

  /* Calc the bones that now have motion-paths. */
  /* TODO: only make for the selected bones? */
  ed_pose_recalc_paths(C, scene, ob, POSE_PATH_CALC_RANGE_FULL);

#ifdef DEBUG_TIME
  TIMEIT_END(recalc_pose_paths);
#endif

  /* notifiers for updates */
  win_ev_add_notifier(C, NC_OBJECT | ND_POSE, ob);

  return OP_FINISHED;
}

void POSE_OT_paths_calculate(wmOperatorType *ot)
{
  /* ids */
  ot->name = "Calc Bone Paths";
  ot->idname = "POSE_OT_paths_calculate";
  ot->description = "Calculate paths for the selected bones";

  /* api cbs */
  ot->invoke = pose_calc_paths_invoke;
  ot->ex = pose_calc_paths_ex;
  ot->poll = ed_op_posemode_exclusive;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_enum(ot->sapi,
               "display_type",
               api_enum_motionpath_display_type_items,
               MOTIONPATH_TYPE_RANGE,
               "Display type",
               "");
  api_def_enum(ot->sapi,
               "range",
               api_enum_motionpath_range_items,
               MOTIONPATH_RANGE_SCENE,
               "Computation Range",
               "");

  api_def_enum(ot->sapi,
               "bake_location",
               api_enum_motionpath_bake_location_items,
               MOTIONPATH_BAKE_HEADS,
               "Bake Location",
               "Which point on the bones is used when calc paths");
}

static bool pose_update_paths_poll(Cxt *C)
{
  if (ed_op_posemode_exclusive(C)) {
    Ob *ob = cxt_data_active_ob(C);
    return (ob->pose->avs.path_bakeflag & MOTIONPATH_BAKE_HAS_PATHS) != 0;
  }

  return false;
}

static int pose_update_paths_ex(Cxt *C, WinOp *op)
{
  Ob *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));
  Scene *scene = cxt_data_scene(C);

  if (ELEM(nullptr, ob, scene)) {
    return OP_CANCELLED;
  }
  animviz_motionpath_compute_range(ob, scene);

  /* set up path data for bones being calc */
  CXT_DATA_BEGIN (C, PoseChannel *, pchan, sel_pose_bones_from_active_ob) {
    animviz_verify_motionpaths(op->reports, scene, ob, pchan);
  }
  CXT_DATA_END;

  /* Calc bones that now have motion-paths. */
  /* TODO: only make for the sel bones? */
  ed_pose_recalc_paths(C, scene, ob, POSE_PATH_CALC_RANGE_FULL);

  /* notifiers for updates */
  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);

  return OP_FINISHED;
}

void POSE_OT_paths_update(WinOpType *ot)
{
  /* ids */
  ot->name = "Update Bone Paths";
  ot->idname = "POSE_OT_paths_update";
  ot->description = "Recalc paths for bones that alrdy have them";

  /* api cbs */
  ot->ex = pose_update_paths_ex;
  ot->poll = pose_update_paths_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* for the ob with pose/action: clear path curves for sel bones only */
static void ed_pose_clear_paths(Ob *ob, bool only_sel)
{
  bool skipped = false;

  if (ELEM(nullptr, ob, ob->pose)) {
    return;
  }

  /* free the motionpath blocks for all bones - This is easier for users to quickly clear all */
  LIST_FOREACH (PoseChannel *, pchan, &ob->pose->chanbase) {
    if (pchan->mpath) {
      if ((only_sel == false) || ((pchan->bone) && (pchan->bone->flag & BONE_SEL))) {
        animviz_free_motionpath(pchan->mpath);
        pchan->mpath = nullptr;
      }
      else {
        skipped = true;
      }
    }
  }

  /* if nothing was skipped, there should be no paths left! */
  if (skipped == false) {
    ob->pose->avs.path_bakeflag &= ~MOTIONPATH_BAKE_HAS_PATHS;
  }

  /* tag armature ob for copy on write - so removed paths don't still show */
  graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
}

/* Op cb - wrapper for the back-end fn. */
static int pose_clear_paths_ex(Cxt *C, WinOp *op)
{
  Ob *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));
  bool only_sel = api_bool_get(op->ptr, "only_sel");

  /* only continue if there's an ob */
  if (ELEM(nullptr, ob, ob->pose)) {
    return OP_CANCELLED;
  }

  /* use the backend fn for this */
  ed_pose_clear_paths(ob, only_sel);

  /* notifiers for updates */
  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);

  return OP_FINISHED;
}

static std::string pose_clear_paths_description(Cxt * /*C*/,
                                                WinOpType * /*ot*/,
                                                ApiPtr *ptr)
{
  const bool only_sel = api_bool_get(ptr, "only_sel");
  if (only_sel) {
    return TIP_("Clear motion paths of sel bones");
  }
  return TIP_("Clear motion paths of all bones");
}

void POSE_OT_paths_clear(WinOpType *ot)
{
  /* ids */
  ot->name = "Clear Bone Paths";
  ot->idname = "POSE_OT_paths_clear";

  /* api cbs */
  ot->ex = pose_clear_paths_ex;
  ot->poll = ed_op_posemode_exclusive;
  ot->get_description = pose_clear_paths_description;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_bool(ot->sapi,
                             "only_sel",
                             false,
                             "Only Sel",
                             "Only clear motion paths of sel bones");
  api_def_prop_flag(ot->prop, PROP_SKIP_SAVE);
}

static int pose_update_paths_range_ex(Cxt *C, WinOp * /*op*/)
{
  Scene *scene = cxt_data_scene(C);
  Ob *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));

  if (ELEM(nullptr, scene, ob, ob->pose)) {
    return OP_CANCELLED;
  }

  /* use Preview Range or Full Frame Range - whichever is in use */
  ob->pose->avs.path_sf = PSFRA;
  ob->pose->avs.path_ef = PEFRA;

  /* tag for updates */
  graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);

  return OP_FINISHED;
}

void POSE_OT_paths_range_update(WinOpType *ot)
{
  /* ids */
  ot->name = "Update Range from Scene";
  ot->idname = "POSE_OT_paths_range_update";
  ot->description = "Update frame range for motion paths from the Scene's current frame range";

  /* cbs */
  ot->ex = pose_update_paths_range_ex;
  ot->poll = ed_op_posemode_exclusive;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int pose_flip_names_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  View3D *v3d = cxt_win_view3d(C);
  const bool do_strip_numbers = api_bool_get(op->ptr, "do_strip_numbers");

  FOREACH_OB_IN_MODE_BEGIN (scene, view_layer, v3d, OB_ARMATURE, OB_MODE_POSE, ob) {
    Armature *arm = static_cast<Armature *>(ob->data);
    List bones_names = {nullptr};

    FOREACH_PCHAN_SEL_IN_OB_BEGIN (ob, pchan) {
      lib_addtail(&bones_names, lib_genericNodeN(pchan->name));
    }
    FOREACH_PCHAN_SEL_IN_OB_END;

    ed_armature_bones_flip_names(main, arm, &bones_names, do_strip_numbers);

    lib_freelist(&bones_names);

    /* since we renamed stuff... */
    graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

    /* NOTE: notifier might evolve. */
    win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
  }
  FOREACH_OB_IN_MODE_END;

  return OP_FINISHED;
}

void POSE_OT_flip_names(WinOpType *ot)
{
  /* ids */
  ot->name = "Flip Names";
  ot->idname = "POSE_OT_flip_names";
  ot->description = "Flips (and corrects) the axis suffixes of the names of selected bones";

  /* api cbs */
  ot->ex = pose_flip_names_ex;
  ot->poll = ed_op_posemode_local;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->sapi,
               "do_strip_numbers",
                false,
                "Strip Numbers",
                "Try to remove right-most dot-number from flipped names.\n"
                "Warning: May result in incoherent naming in some cases");
}

static int pose_autoside_names_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  char newname[MAXBONENAME];
  short axis = api_enum_get(op->ptr, "axis");
  Ob *ob_prev = nullptr;

  /* loop through sel bones, auto-naming them */
  CXT_DATA_BEGIN_WITH_ID (C, PoseChannel *, pchan, sel_pose_bones, Ob *, ob) {
    Armature *arm = static_cast<Armature *>(ob->data);
    STRNCPY(newname, pchan->name);
    if (bone_autoside_name(newname, 1, axis, pchan->bone->head[axis], pchan->bone->tail[axis])) {
      ed_armature_bone_rename(main, arm, pchan->name, newname);
    }

    if (ob_prev != ob) {
      /* since we renamed stuff... */
      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

      /* NOTE: notifier might evolve. */
      win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
      ob_prev = ob;
    }
  }
  CXT_DATA_END;

  return OP_FINISHED;
}

void POSE_OT_autoside_names(WinOpType *ot)
{
  static const EnumPropItem axis_items[] = {
      {0, "XAXIS", 0, "X-Axis", "Left/Right"},
      {1, "YAXIS", 0, "Y-Axis", "Front/Back"},
      {2, "ZAXIS", 0, "Z-Axis", "Top/Bottom"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* ids */
  ot->name = "Auto-Name by Axis";
  ot->idname = "POSE_OT_autoside_names";
  ot->description =
      "Automatically renames the sel bones according to which side of the target axis they "
      "fall on";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = pose_autoside_names_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* settings */
  ot->prop = api_def_enum(ot->sapi, "axis", axis_items, 0, "Axis", "Axis to tag names with");
}

static int pose_bone_rotmode_ex(Cxt *C, WinOp *op)
{
  const int mode = api_enum_get(op->ptr, "type");
  Ob *prev_ob = nullptr;

  /* Set rotation mode of sel bones. */
  CXT_DATA_BEGIN_WITH_ID (C, PoseChannel *, pchan, sel_pose_bones, Ob *, ob) {
    /* use API Method for conversions... */
    dune_rotMode_change_vals(
        pchan->quat, pchan->eul, pchan->rotAxis, &pchan->rotAngle, pchan->rotmode, short(mode));

    /* finally, set the new rotation type */
    pchan->rotmode = mode;

    if (prev_ob != ob) {
      /* Notifiers and updates. */
      graph_id_tag_update((Id *)ob, ID_RECALC_GEOMETRY);
      win_ev_add_notifier(C, NC_OB | ND_TRANSFORM, ob);
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);
      prev_ob = ob;
    }
  }
  CXT_DATA_END;

  return OP_FINISHED;
}

void POSE_OT_rotation_mode_set(WinOpType *ot)
{
  /* ids */
  ot->name = "Set Rotation Mode";
  ot->idname = "POSE_OT_rotation_mode_set";
  ot->description = "Set the rotation representation used by selected bones";

  /* cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = pose_bone_rotmode_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(
      ot->sapi, "type", api_enum_ob_rotation_mode_items, 0, "Rotation Mode", "");
}

/* Show/Hide Bones */
static int hide_pose_bone_fn(Ob *ob, Bone *bone, void *ptr)
{
  Armature *arm = static_cast<Armature *>(ob->data);
  const bool hide_sel = bool(PTR_AS_INT(ptr));
  int count = 0;
  if (anim_bonecoll_is_visible(arm, bone)) {
    if (((bone->flag & BONE_SEL) != 0) == hide_sel) {
      bone->flag |= BONE_HIDDEN_P;
      /* only needed when 'hide_sel' is true, but harmless. */
      bone->flag &= ~BONE_SEL;
      if (arm->act_bone == bone) {
        arm->act_bone = nullptr;
      }
      count += 1;
    }
  }
  return count;
}

/* active ob is armature in posemode, poll checked */
static int pose_hide_ex(Cxt *C, WinOp *op)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  uint obs_len;
  Ob **obs = dune_ob_pose_array_get_unique(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  bool changed_multi = false;

  const int hide_sel = !api_bool_get(op->ptr, "unselected");
  void *hide_sel_p = PTR_FROM_INT(hide_sel);

  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob_iter = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob_iter->data);

    bool changed = bone_looper(ob_iter,
                               static_cast<Bone *>(arm->bonebase.first),
                               hide_sel_p,
                               hide_pose_bone_fn) != 0;
    if (changed) {
      changed_multi = true;
      win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob_iter);
      graph_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  mem_free(obs);

  return changed_multi ? OP_FINISHED : OP_CANCELLED;
}

void POSE_OT_hide(WinOpType *ot)
{
  /* ids */
  ot->name = "Hide Sel";
  ot->idname = "POSE_OT_hide";
  ot->description = "Tag selected bones to not be visible in Pose Mode";

  /* api cbs */
  ot->ex = pose_hide_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_bool(ot->sapi, "unselected", false, "Unselected", "");
}

static int show_pose_bone_cb(Object *ob, Bone *bone, void *data)
{
  const bool sel = PTR_AS_INT(data);

  Armature *arm = static_cast<Armature *>(ob->data);
  int count = 0;
  if (anim_bonecoll_is_visible(arm, bone)) {
    if (bone->flag & BONE_HIDDEN_P) {
      if (!(bone->flag & BONE_UNSELECTABLE)) {
        SET_FLAG_FROM_TEST(bone->flag, sel, BONE_SEL);
      }
      bone->flag &= ~BONE_HIDDEN_P;
      count += 1;
    }
  }

  return count;
}

/* active ob is armature in posemode, poll checked */
static int pose_reveal_ex(Cxt *C, WinOp *op)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  uint objects_len;
  Ob **obs = dune_ob_pose_array_get_unique(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  bool changed_multi = false;
  const bool sel = api_bool_get(op->ptr, "sel");
  void *sel_p = PTR_FROM_INT(sel);

  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob_iter = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob_iter->data);

    bool changed = bone_looper(
        ob_iter, static_cast<Bone *>(arm->bonebase.first), sel_p, show_pose_bone_cb);
    if (changed) {
      changed_multi = true;
      win_ev_add_notifier(C, NC_OBJ | ND_BONE_SEL, ob_iter);
      graph_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  mem_free(obs);

  return changed_multi ? OP_FINISHED : OP_CANCELLED;
}

void POSE_OT_reveal(WinOpType *ot)
{
  /* ids */
  ot->name = "Reveal Sel";
  ot->idname = "POSE_OT_reveal";
  ot->description = "Reveal all bones hidden in Pose Mode";

  /* api cbs */
  ot->exec = pose_reveal_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

 api_def_bool(ot->sapi, "sel", true, "Sel", "");
}

/* Flip Quats */
static int pose_flip_quats_exec(Cxt *C, WinOp * /*op*/)
{
  Scene *scene = cxt_data_scene(C);
  KeyingSet *ks = anim_builtin_keyingset_get_named(ANIM_KS_LOC_ROT_SCALE_ID);

  bool changed_multi = false;

  ViewLayer *view_layer = cxt_data_view_layer(C);
  View3D *v3d = cxt_win_view3d(C);
  FOREACH_OB_IN_MODE_BEGIN (scene, view_layer, v3d, OB_ARMATURE, OB_MODE_POSE, ob_iter) {
    bool changed = false;
    /* loop through all sel pchans, flipping and keying (as needed) */
    FOREACH_PCHAN_SEL_IN_OB_BEGIN (ob_iter, pchan) {
      /* only if bone is using quaternion rotation */
      if (pchan->rotmode == ROT_MODE_QUAT) {
        changed = true;
        /* quaternions have 720 degree range */
        negate_v4(pchan->quat);

        dune::animrig::autokeyframe_pchan(C, scene, ob_iter, pchan, ks);
      }
    }
    FOREACH_PCHAN_SEL_IN_OB_END;

    if (changed) {
      changed_multi = true;
      /* notifiers and updates */
      graph_id_tag_update(&ob_iter->id, ID_RECALC_GEOMETRY);
      win_ev_add_notifier(C, NC_OB | ND_TRANSFORM, ob_iter);
    }
  }
  FOREACH_OB_IN_MODE_END;

  return changed_multi ? OP_FINISHED : OP_CANCELLED;
}

void POSE_OT_quaternions_flip(WinOpType *ot)
{
  /* ids */
  ot->name = "Flip Quats";
  ot->idname = "POSE_OT_quaternions_flip";
  ot->description =
      "Flip quaternion values to achieve desired rotations, while maintaining the same "
      "orientations";

  /* cbs */
  ot->ex = pose_flip_quats_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
