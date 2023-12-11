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

  /* properties */
  RNA_def_enum(ot->srna,
               "display_type",
               rna_enum_motionpath_display_type_items,
               MOTIONPATH_TYPE_RANGE,
               "Display type",
               "");
  RNA_def_enum(ot->srna,
               "range",
               rna_enum_motionpath_range_items,
               MOTIONPATH_RANGE_SCENE,
               "Computation Range",
               "");

  RNA_def_enum(ot->srna,
               "bake_location",
               rna_enum_motionpath_bake_location_items,
               MOTIONPATH_BAKE_HEADS,
               "Bake Location",
               "Which point on the bones is used when calculating paths");
}

/* --------- */

static bool pose_update_paths_poll(bContext *C)
{
  if (ED_operator_posemode_exclusive(C)) {
    Object *ob = CTX_data_active_object(C);
    return (ob->pose->avs.path_bakeflag & MOTIONPATH_BAKE_HAS_PATHS) != 0;
  }

  return false;
}

static int pose_update_paths_exec(bContext *C, wmOperator *op)
{
  Object *ob = BKE_object_pose_armature_get(CTX_data_active_object(C));
  Scene *scene = CTX_data_scene(C);

  if (ELEM(nullptr, ob, scene)) {
    return OPERATOR_CANCELLED;
  }
  animviz_motionpath_compute_range(ob, scene);

  /* set up path data for bones being calculated */
  CTX_DATA_BEGIN (C, bPoseChannel *, pchan, selected_pose_bones_from_active_object) {
    animviz_verify_motionpaths(op->reports, scene, ob, pchan);
  }
  CTX_DATA_END;

  /* Calculate the bones that now have motion-paths. */
  /* TODO: only make for the selected bones? */
  ED_pose_recalculate_paths(C, scene, ob, POSE_PATH_CALC_RANGE_FULL);

  /* notifiers for updates */
  WM_event_add_notifier(C, NC_OBJECT | ND_POSE, ob);

  return OPERATOR_FINISHED;
}

void POSE_OT_paths_update(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Update Bone Paths";
  ot->idname = "POSE_OT_paths_update";
  ot->description = "Recalculate paths for bones that already have them";

  /* api callbacks */
  ot->exec = pose_update_paths_exec;
  ot->poll = pose_update_paths_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* --------- */

/* for the object with pose/action: clear path curves for selected bones only */
static void ED_pose_clear_paths(Object *ob, bool only_selected)
{
  bool skipped = false;

  if (ELEM(nullptr, ob, ob->pose)) {
    return;
  }

  /* free the motionpath blocks for all bones - This is easier for users to quickly clear all */
  LISTBASE_FOREACH (bPoseChannel *, pchan, &ob->pose->chanbase) {
    if (pchan->mpath) {
      if ((only_selected == false) || ((pchan->bone) && (pchan->bone->flag & BONE_SELECTED))) {
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

  /* tag armature object for copy on write - so removed paths don't still show */
  DEG_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
}

/* Operator callback - wrapper for the back-end function. */
static int pose_clear_paths_exec(bContext *C, wmOperator *op)
{
  Object *ob = BKE_object_pose_armature_get(CTX_data_active_object(C));
  bool only_selected = RNA_boolean_get(op->ptr, "only_selected");

  /* only continue if there's an object */
  if (ELEM(nullptr, ob, ob->pose)) {
    return OPERATOR_CANCELLED;
  }

  /* use the backend function for this */
  ED_pose_clear_paths(ob, only_selected);

  /* notifiers for updates */
  WM_event_add_notifier(C, NC_OBJECT | ND_POSE, ob);

  return OPERATOR_FINISHED;
}

static std::string pose_clear_paths_description(bContext * /*C*/,
                                                wmOperatorType * /*ot*/,
                                                PointerRNA *ptr)
{
  const bool only_selected = RNA_boolean_get(ptr, "only_selected");
  if (only_selected) {
    return TIP_("Clear motion paths of selected bones");
  }
  return TIP_("Clear motion paths of all bones");
}

void POSE_OT_paths_clear(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Bone Paths";
  ot->idname = "POSE_OT_paths_clear";

  /* api callbacks */
  ot->exec = pose_clear_paths_exec;
  ot->poll = ED_operator_posemode_exclusive;
  ot->get_description = pose_clear_paths_description;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = RNA_def_boolean(ot->srna,
                             "only_selected",
                             false,
                             "Only Selected",
                             "Only clear motion paths of selected bones");
  RNA_def_property_flag(ot->prop, PROP_SKIP_SAVE);
}

/* --------- */

static int pose_update_paths_range_exec(bContext *C, wmOperator * /*op*/)
{
  Scene *scene = CTX_data_scene(C);
  Object *ob = BKE_object_pose_armature_get(CTX_data_active_object(C));

  if (ELEM(nullptr, scene, ob, ob->pose)) {
    return OPERATOR_CANCELLED;
  }

  /* use Preview Range or Full Frame Range - whichever is in use */
  ob->pose->avs.path_sf = PSFRA;
  ob->pose->avs.path_ef = PEFRA;

  /* tag for updates */
  DEG_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
  WM_event_add_notifier(C, NC_OBJECT | ND_POSE, ob);

  return OPERATOR_FINISHED;
}

void POSE_OT_paths_range_update(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Update Range from Scene";
  ot->idname = "POSE_OT_paths_range_update";
  ot->description = "Update frame range for motion paths from the Scene's current frame range";

  /* callbacks */
  ot->exec = pose_update_paths_range_exec;
  ot->poll = ED_operator_posemode_exclusive;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ********************************************** */

static int pose_flip_names_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  View3D *v3d = CTX_wm_view3d(C);
  const bool do_strip_numbers = RNA_boolean_get(op->ptr, "do_strip_numbers");

  FOREACH_OBJECT_IN_MODE_BEGIN (scene, view_layer, v3d, OB_ARMATURE, OB_MODE_POSE, ob) {
    bArmature *arm = static_cast<bArmature *>(ob->data);
    ListBase bones_names = {nullptr};

    FOREACH_PCHAN_SELECTED_IN_OBJECT_BEGIN (ob, pchan) {
      BLI_addtail(&bones_names, BLI_genericNodeN(pchan->name));
    }
    FOREACH_PCHAN_SELECTED_IN_OBJECT_END;

    ED_armature_bones_flip_names(bmain, arm, &bones_names, do_strip_numbers);

    BLI_freelistN(&bones_names);

    /* since we renamed stuff... */
    DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

    /* NOTE: notifier might evolve. */
    WM_event_add_notifier(C, NC_OBJECT | ND_POSE, ob);
  }
  FOREACH_OBJECT_IN_MODE_END;

  return OPERATOR_FINISHED;
}

void POSE_OT_flip_names(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Flip Names";
  ot->idname = "POSE_OT_flip_names";
  ot->description = "Flips (and corrects) the axis suffixes of the names of selected bones";

  /* api callbacks */
  ot->exec = pose_flip_names_exec;
  ot->poll = ED_operator_posemode_local;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_boolean(ot->srna,
                  "do_strip_numbers",
                  false,
                  "Strip Numbers",
                  "Try to remove right-most dot-number from flipped names.\n"
                  "Warning: May result in incoherent naming in some cases");
}

/* ------------------ */

static int pose_autoside_names_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  char newname[MAXBONENAME];
  short axis = RNA_enum_get(op->ptr, "axis");
  Object *ob_prev = nullptr;

  /* loop through selected bones, auto-naming them */
  CTX_DATA_BEGIN_WITH_ID (C, bPoseChannel *, pchan, selected_pose_bones, Object *, ob) {
    bArmature *arm = static_cast<bArmature *>(ob->data);
    STRNCPY(newname, pchan->name);
    if (bone_autoside_name(newname, 1, axis, pchan->bone->head[axis], pchan->bone->tail[axis])) {
      ED_armature_bone_rename(bmain, arm, pchan->name, newname);
    }

    if (ob_prev != ob) {
      /* since we renamed stuff... */
      DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

      /* NOTE: notifier might evolve. */
      WM_event_add_notifier(C, NC_OBJECT | ND_POSE, ob);
      ob_prev = ob;
    }
  }
  CTX_DATA_END;

  return OPERATOR_FINISHED;
}

void POSE_OT_autoside_names(wmOperatorType *ot)
{
  static const EnumPropertyItem axis_items[] = {
      {0, "XAXIS", 0, "X-Axis", "Left/Right"},
      {1, "YAXIS", 0, "Y-Axis", "Front/Back"},
      {2, "ZAXIS", 0, "Z-Axis", "Top/Bottom"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* identifiers */
  ot->name = "Auto-Name by Axis";
  ot->idname = "POSE_OT_autoside_names";
  ot->description =
      "Automatically renames the selected bones according to which side of the target axis they "
      "fall on";

  /* api callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = pose_autoside_names_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* settings */
  ot->prop = RNA_def_enum(ot->srna, "axis", axis_items, 0, "Axis", "Axis to tag names with");
}

/* ********************************************** */

static int pose_bone_rotmode_exec(bContext *C, wmOperator *op)
{
  const int mode = RNA_enum_get(op->ptr, "type");
  Object *prev_ob = nullptr;

  /* Set rotation mode of selected bones. */
  CTX_DATA_BEGIN_WITH_ID (C, bPoseChannel *, pchan, selected_pose_bones, Object *, ob) {
    /* use API Method for conversions... */
    BKE_rotMode_change_values(
        pchan->quat, pchan->eul, pchan->rotAxis, &pchan->rotAngle, pchan->rotmode, short(mode));

    /* finally, set the new rotation type */
    pchan->rotmode = mode;

    if (prev_ob != ob) {
      /* Notifiers and updates. */
      DEG_id_tag_update((ID *)ob, ID_RECALC_GEOMETRY);
      WM_event_add_notifier(C, NC_OBJECT | ND_TRANSFORM, ob);
      WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, ob);
      prev_ob = ob;
    }
  }
  CTX_DATA_END;

  return OPERATOR_FINISHED;
}

void POSE_OT_rotation_mode_set(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Set Rotation Mode";
  ot->idname = "POSE_OT_rotation_mode_set";
  ot->description = "Set the rotation representation used by selected bones";

  /* callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = pose_bone_rotmode_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = RNA_def_enum(
      ot->srna, "type", rna_enum_object_rotation_mode_items, 0, "Rotation Mode", "");
}

/* ********************************************** */
/* Show/Hide Bones */

static int hide_pose_bone_fn(Object *ob, Bone *bone, void *ptr)
{
  bArmature *arm = static_cast<bArmature *>(ob->data);
  const bool hide_select = bool(POINTER_AS_INT(ptr));
  int count = 0;
  if (ANIM_bonecoll_is_visible(arm, bone)) {
    if (((bone->flag & BONE_SELECTED) != 0) == hide_select) {
      bone->flag |= BONE_HIDDEN_P;
      /* only needed when 'hide_select' is true, but harmless. */
      bone->flag &= ~BONE_SELECTED;
      if (arm->act_bone == bone) {
        arm->act_bone = nullptr;
      }
      count += 1;
    }
  }
  return count;
}

/* active object is armature in posemode, poll checked */
static int pose_hide_exec(bContext *C, wmOperator *op)
{
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len;
  Object **objects = BKE_object_pose_array_get_unique(
      scene, view_layer, CTX_wm_view3d(C), &objects_len);
  bool changed_multi = false;

  const int hide_select = !RNA_boolean_get(op->ptr, "unselected");
  void *hide_select_p = POINTER_FROM_INT(hide_select);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *ob_iter = objects[ob_index];
    bArmature *arm = static_cast<bArmature *>(ob_iter->data);

    bool changed = bone_looper(ob_iter,
                               static_cast<Bone *>(arm->bonebase.first),
                               hide_select_p,
                               hide_pose_bone_fn) != 0;
    if (changed) {
      changed_multi = true;
      WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, ob_iter);
      DEG_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  MEM_freeN(objects);

  return changed_multi ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void POSE_OT_hide(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Hide Selected";
  ot->idname = "POSE_OT_hide";
  ot->description = "Tag selected bones to not be visible in Pose Mode";

  /* api callbacks */
  ot->exec = pose_hide_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_boolean(ot->srna, "unselected", false, "Unselected", "");
}

static int show_pose_bone_cb(Object *ob, Bone *bone, void *data)
{
  const bool select = POINTER_AS_INT(data);

  bArmature *arm = static_cast<bArmature *>(ob->data);
  int count = 0;
  if (ANIM_bonecoll_is_visible(arm, bone)) {
    if (bone->flag & BONE_HIDDEN_P) {
      if (!(bone->flag & BONE_UNSELECTABLE)) {
        SET_FLAG_FROM_TEST(bone->flag, select, BONE_SELECTED);
      }
      bone->flag &= ~BONE_HIDDEN_P;
      count += 1;
    }
  }

  return count;
}

/* active object is armature in posemode, poll checked */
static int pose_reveal_exec(bContext *C, wmOperator *op)
{
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len;
  Object **objects = BKE_object_pose_array_get_unique(
      scene, view_layer, CTX_wm_view3d(C), &objects_len);
  bool changed_multi = false;
  const bool select = RNA_boolean_get(op->ptr, "select");
  void *select_p = POINTER_FROM_INT(select);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *ob_iter = objects[ob_index];
    bArmature *arm = static_cast<bArmature *>(ob_iter->data);

    bool changed = bone_looper(
        ob_iter, static_cast<Bone *>(arm->bonebase.first), select_p, show_pose_bone_cb);
    if (changed) {
      changed_multi = true;
      WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, ob_iter);
      DEG_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
  MEM_freeN(objects);

  return changed_multi ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void POSE_OT_reveal(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reveal Selected";
  ot->idname = "POSE_OT_reveal";
  ot->description = "Reveal all bones hidden in Pose Mode";

  /* api callbacks */
  ot->exec = pose_reveal_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_boolean(ot->srna, "select", true, "Select", "");
}

/* ********************************************** */
/* Flip Quats */

static int pose_flip_quats_exec(bContext *C, wmOperator * /*op*/)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks = ANIM_builtin_keyingset_get_named(ANIM_KS_LOC_ROT_SCALE_ID);

  bool changed_multi = false;

  ViewLayer *view_layer = CTX_data_view_layer(C);
  View3D *v3d = CTX_wm_view3d(C);
  FOREACH_OBJECT_IN_MODE_BEGIN (scene, view_layer, v3d, OB_ARMATURE, OB_MODE_POSE, ob_iter) {
    bool changed = false;
    /* loop through all selected pchans, flipping and keying (as needed) */
    FOREACH_PCHAN_SELECTED_IN_OBJECT_BEGIN (ob_iter, pchan) {
      /* only if bone is using quaternion rotation */
      if (pchan->rotmode == ROT_MODE_QUAT) {
        changed = true;
        /* quaternions have 720 degree range */
        negate_v4(pchan->quat);

        blender::animrig::autokeyframe_pchan(C, scene, ob_iter, pchan, ks);
      }
    }
    FOREACH_PCHAN_SELECTED_IN_OBJECT_END;

    if (changed) {
      changed_multi = true;
      /* notifiers and updates */
      DEG_id_tag_update(&ob_iter->id, ID_RECALC_GEOMETRY);
      WM_ev_add_notifier(C, NC_OBJECT | ND_TRANSFORM, ob_iter);
    }
  }
  FOREACH_OBJECT_IN_MODE_END;

  return changed_multi ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void POSE_OT_quaternions_flip(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Flip Quats";
  ot->idname = "POSE_OT_quaternions_flip";
  ot->description =
      "Flip quaternion values to achieve desired rotations, while maintaining the same "
      "orientations";

  /* callbacks */
  ot->exec = pose_flip_quats_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
