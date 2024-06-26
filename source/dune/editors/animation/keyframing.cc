#include <cstddef>
#include <cstdio>

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"

#include "lang.h"

#include "types_id.h"
#include "types_action.h"
#include "types_anim.h"
#include "types_armature.h"
#include "types_constraint.h"
#include "types_key.h"
#include "types_material.h"
#include "types_ob.h"
#include "types_scene.h"

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_animsys.h"
#include "dune_armature.hh"
#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_global.h"
#include "dune_idtype.h"
#include "dune_lib_id.h"
#include "dune_nla.h"
#include "dune_report.h"
#include "dune_scene.h"

#include "graph.hh"
#include "graph_build.hh"

#include "ed_keyframing.hh"
#include "ed_ob.hh"
#include "ed_screen.hh"

#include "animdata.hh"
#include "anim_bone_collections.hh"
#include "anim_fcurve.hh"
#include "anim_keyframing.hh"
#include "anim_api.hh"
#include "anim_visualkey.hh"

#include "ui.hh"
#include "ui_resources.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_enum_types.hh"
#include "api_path.hh"
#include "api_prototypes.h"

#include "anim_intern.h"

static KeyingSet *keyingset_get_from_op_with_error(WinOp *op,
                                                   ApiProp *prop,
                                                   Scene *scene);

static int del_key_using_keying_set(Cxt *C, WinOp *op, KeyingSet *ks);

/* Keyframing Setting Wrangling */
eInsertKeyFlags anim_get_keyframing_flags(Scene *scene, const bool use_autokey_mode)
{
  using namespace dune::animrig;
  eInsertKeyFlags flag = INSERTKEY_NOFLAGS;

  /* standard flags */
  {
    /* visual keying */
    if (is_autokey_flag(scene, AUTOKEY_FLAG_VISUALKEY)) {
      flag |= INSERTKEY_MATRIX;
    }

    /* only needed */
    if (is_autokey_flag(scene, AUTOKEY_FLAG_INSERTNEEDED)) {
      flag |= INSERTKEY_NEEDED;
    }
  }

  /* only if including settings from the autokeying mode... */
  /* TODO: This flag needs to be passed as true: confusing bc its unclear
   * why those 2 flags would be exclusive to autokeying.
   * Refactor flags to be separate between normal keying and autokeying. */
  if (use_autokey_mode) {
    /* keyframing mode - only replace existing keyframes */
    if (is_autokey_mode(scene, AUTOKEY_MODE_EDITKEYS)) {
      flag |= INSERTKEY_REPLACE;
    }

    /* cycle-aware keyframe insertion - preserve cycle period and flow */
    if (is_autokey_flag(scene, AUTOKEY_FLAG_CYCLEAWARE)) {
      flag |= INSERTKEY_CYCLE_AWARE;
    }
  }

  return flag;
}

/* Anim Data Validation */
Action *ed_id_action_ensure(Main *main, Id *id)
{
  AnimData *adt;

  /* init animdata if none available yet */
  adt = dune_animdata_from_id(id);
  if (adt == nullptr) {
    adt = dune_animdata_ensure_id(id);
  }
  if (adt == nullptr) {
    /* if still none (as not allowed to add, or Id doesn't have animdata for some reason) */
    printf("ERROR: Couldn't add AnimData (Id = %s)\n", (id) ? (id->name) : "<None>");
    return nullptr;
  }

  /* init action if none available yet */
  /* TODO: need some wizardry to handle NLA stuff correct */
  if (adt->action == nullptr) {
    /* init action name from name of ID block */
    char actname[sizeof(id->name) - 2];
    SNPRINTF(actname, "%sAction", id->name + 2);

    /* create action */
    adt->action = dune_action_add(bmain, actname);

    /* set Id-type from Id-block that this is going to be assigned to
     * so that users can't accidentally break actions by assigning them
     * to the wrong places */
    dune_animdata_action_ensure_idroot(id, adt->action);

    /* Tag graph to be rebuilt to include time dependency. */
    graph_tag_update(main);
  }

  graph_id_tag_update(&adt->action->id, ID_RECALC_ANIM_NO_FLUSH);

  /* return the action */
  return adt->action;
}

/* Helper for update_autoflags_fcurve(). */
void update_autoflags_fcurve_direct(FCurve *fcu, ApiProp *prop)
{
  /* set additional flags for the F-Curve (i.e. only int vals) */
  fcu->flag &= ~(FCURVE_INT_VALS | FCURVE_DISCRETE_VALS);
  switch (api_prop_type(prop)) {
    case PROP_FLOAT:
      /* do nothing */
      break;
    case PROP_INT:
      /* do integer (only 'whole' numbers) interpolation between all points */
      fcu->flag |= FCURVE_INT_VALS;
      break;
    default:
      /* do 'discrete' (i.e. enum, bool vals which cannot take any intermediate
       * vals at all) interpolation between all points
       *    - however, we must also ensure that eval vals are only ints still */
      fcu->flag |= (FCURVE_DISCRETE_VALS | FCURVE_INT_VALS);
      break;
  }
}

void update_autoflags_fcurve(FCurve *fcu, Cxt *C, ReportList *reports, ApiPtr *ptr)
{
  ApiPtr tmp_ptr;
  ApiProp *prop;
  int old_flag = fcu->flag;

  if ((ptr->owner_id == nullptr) && (ptr->data == nullptr)) {
    dune_report(reports, RPT_ERROR, "No api ptr available to retrieve vals for this F-curve");
    return;
  }

  /* try to get prop we should be affecting */
  if (api_path_resolve_prop(ptr, fcu->api_path, &tmp_ptr, &prop) == false) {
    /* prop not found... */
    const char *idname = (ptr->owner_id) ? ptr->owner_id->name : TIP_("<No Id ptr>");

    dune_reportf(reports,
                RPT_ERROR,
                "Could not update flags for this F-curve, as RNA path is invalid for the given ID "
                "(ID = %s, path = %s)",
                idname,
                fcu->rna_path);
    return;
  }

  /* update F-Curve flags */
  update_autoflags_fcurve_direct(fcu, prop);

  if (old_flag != fcu->flag) {
    /* Same as if keyframes had been changed */
    win_ev_add_notifier(C, NC_ANIM | ND_KEYFRAME | NA_EDITED, nullptr);
  }
}

/* ------------------------- Insert Key API ------------------------- */

void ed_keyframes_add(FCurve *fcu, int num_keys_to_add)
{
  lib_assert_msg(num_keys_to_add >= 0, "cannot remove keyframes with this function");

  if (num_keys_to_add == 0) {
    return;
  }

  fcu->bezt = static_cast<BezTriple *>(
      mem_recalloc(fcu->bezt, sizeof(BezTriple) * (fcu->totvert + num_keys_to_add)));
  BezTriple *bezt = fcu->bezt + fcu->totvert; /* Pointer to the first new one. '*/

  fcu->totvert += num_keys_to_add;

  /* Iterate over the new keys to update their settings. */
  while (num_keys_to_add--) {
    /* Defaults, ignoring user-preference gives predictable results for API. */
    bezt->f1 = bezt->f2 = bezt->f3 = SEL;
    bezt->ipo = BEZT_IPO_BEZ;
    bezt->h1 = bezt->h2 = HD_AUTO_ANIM;
    bezt++;
  }
}

/* KEYFRAME MODIFICATION */
/* mode for commonkey_modifykey */
enum {
  COMMONKEY_MODE_INSERT = 0,
  COMMONKEY_MODE_DEL,
} /*eCommonModifyKey_Modes*/;

/* Polling cb for use with `anim_*_keyframe()` operators
 * This is based on the standard ed_op_areaactive cb,
 * except that it does special checks for a few space-types too. */
static bool modify_key_op_poll(Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  Scene *scene = cxt_data_scene(C);

  /* if no area or active scene */
  if (ELEM(nullptr, area, scene)) {
    return false;
  }

  /* should be fine */
  return true;
}

/* Insert Key Op */
static int insert_key_with_keyingset(Cxt *C, Op *op, KeyingSet *ks)
{
  Scene *scene = cxt_data_scene(C);
  Ob *obedit = cxt_data_edit_ob(C);
  bool ob_edit_mode = false;

  const float cfra = dune_scene_frame_get(scene);
  const bool confirm = op->flag & OP_IS_INVOKE;
  /* exit the edit mode to make sure that those object data properties that have been
   * updated since the last switching to the edit mode will be keyframed correctly
   */
  if (obedit && anim_keyingset_find_id(ks, (ID *)obedit->data)) {
    ed_ob_mode_set(C, OB_MODE_OBJECT);
    ob_edit_mode = true;
  }

  /* try to insert keyframes for the channels specified by KeyingSet */
  const int num_channels = anim_apply_keyingset(C, nullptr, ks, MODIFYKEY_MODE_INSERT, cfra);
  if (G.debug & G_DEBUG) {
    dune_reportf(op->reports,
                RPT_INFO,
                "Keying set '%s' - successfully added %d keyframes",
                ks->name,
                num_channels);
  }

  /* restore the edit mode if necessary */
  if (ob_edit_mode) {
    ed_ob_mode_set(C, OB_MODE_EDIT);
  }

  /* report failure or do updates? */
  if (num_channels < 0) {
    dune_report(op->reports, RPT_ERROR, "No suitable context info for active keying set");
    return OP_CANCELLED;
  }

  if (num_channels > 0) {
    /* send notifiers that keyframes have been changed */
    WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_ADDED, nullptr);
  }

  if (confirm) {
    /* if called by invoke (from the UI), make a note that we've inserted keyframes */
    if (num_channels > 0) {
      BKE_reportf(op->reports,
                  RPT_INFO,
                  "Successfully added %d keyframes for keying set '%s'",
                  num_channels,
                  ks->name);
    }
    else {
      BKE_report(op->reports, RPT_WARNING, "Keying set failed to insert any keyframes");
    }
  }

  return OPERATOR_FINISHED;
}

static blender::Vector<std::string> construct_rna_paths(PointerRNA *ptr)
{
  eRotationModes rotation_mode;
  IDProperty *properties;
  blender::Vector<std::string> paths;

  if (ptr->type == &RNA_PoseBone) {
    bPoseChannel *pchan = static_cast<bPoseChannel *>(ptr->data);
    rotation_mode = eRotationModes(pchan->rotmode);
    properties = pchan->prop;
  }
  else if (ptr->type == &RNA_Object) {
    Object *ob = static_cast<Object *>(ptr->data);
    rotation_mode = eRotationModes(ob->rotmode);
    properties = ob->id.properties;
  }
  else {
    /* Pointer type not supported. */
    return paths;
  }

  eKeyInsertChannels insert_channel_flags = eKeyInsertChannels(U.key_insert_channels);
  if (insert_channel_flags & USER_ANIM_KEY_CHANNEL_LOCATION) {
    paths.append("location");
  }
  if (insert_channel_flags & USER_ANIM_KEY_CHANNEL_ROTATION) {
    switch (rotation_mode) {
      case ROT_MODE_QUAT:
        paths.append("rotation_quaternion");
        break;
      case ROT_MODE_AXISANGLE:
        paths.append("rotation_axis_angle");
        break;
      case ROT_MODE_XYZ:
      case ROT_MODE_XZY:
      case ROT_MODE_YXZ:
      case ROT_MODE_YZX:
      case ROT_MODE_ZXY:
      case ROT_MODE_ZYX:
        paths.append("rotation_euler");
      default:
        break;
    }
  }
  if (insert_channel_flags & USER_ANIM_KEY_CHANNEL_SCALE) {
    paths.append("scale");
  }
  if (insert_channel_flags & USER_ANIM_KEY_CHANNEL_ROTATION_MODE) {
    paths.append("rotation_mode");
  }
  if (insert_channel_flags & USER_ANIM_KEY_CHANNEL_CUSTOM_PROPERTIES) {
    if (properties) {
      LISTBASE_FOREACH (IDProperty *, prop, &properties->data.group) {
        std::string name = prop->name;
        std::string rna_path = "[\"" + name + "\"]";
        paths.append(rna_path);
      }
    }
  }
  return paths;
}

/* Fill the list with CollectionPointerLink depending on the mode of the context. */
static bool get_selection(bContext *C, ListBase *r_selection)
{
  const eContextObjectMode context_mode = CTX_data_mode_enum(C);

  switch (context_mode) {
    case CTX_MODE_OBJECT: {
      CTX_data_selected_objects(C, r_selection);
      break;
    }
    case CTX_MODE_POSE: {
      CTX_data_selected_pose_bones(C, r_selection);
      break;
    }
    default:
      return false;
  }

  return true;
}

static int insert_key(bContext *C, wmOperator *op)
{
  using namespace blender;

  ListBase selection = {nullptr, nullptr};
  const bool found_selection = get_selection(C, &selection);
  if (!found_selection) {
    BKE_reportf(op->reports, RPT_ERROR, "Unsupported context mode");
  }

  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  const float scene_frame = BKE_scene_frame_get(scene);

  /* Passing autokey mode as true because that is needed to get the cycle aware keying flag. */
  const bool use_autokey_mode = true;
  const eInsertKeyFlags insert_key_flags = ANIM_get_keyframing_flags(scene, use_autokey_mode);
  const eBezTriple_KeyframeType key_type = eBezTriple_KeyframeType(
      scene->toolsettings->keyframe_type);

  LISTBASE_FOREACH (CollectionPointerLink *, collection_ptr_link, &selection) {
    ID *selected_id = collection_ptr_link->ptr.owner_id;
    if (!BKE_id_is_editable(bmain, selected_id)) {
      BKE_reportf(op->reports, RPT_ERROR, "'%s' is not editable", selected_id->name + 2);
      continue;
    }
    PointerRNA id_ptr = collection_ptr_link->ptr;
    Vector<std::string> rna_paths = construct_rna_paths(&collection_ptr_link->ptr);

    animrig::insert_key_rna(
        &id_ptr, rna_paths.as_span(), scene_frame, insert_key_flags, key_type, bmain, op->reports);
  }

  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_ADDED, nullptr);
  BLI_freelistN(&selection);

  return OPERATOR_FINISHED;
}

static int insert_key_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  /* Use the active keying set if there is one. */
  KeyingSet *ks = ANIM_keyingset_get_from_enum_type(scene, scene->active_keyingset);
  if (ks) {
    return insert_key_with_keyingset(C, op, ks);
  }
  return insert_key(C, op);
}

void ANIM_OT_keyframe_insert(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Insert Keyframe";
  ot->idname = "ANIM_OT_keyframe_insert";
  ot->description =
      "Insert keyframes on the current frame using either the active keying set, or the user "
      "preferences if no keying set is active";

  /* callbacks */
  ot->exec = insert_key_exec;
  ot->poll = modify_key_op_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int keyframe_insert_with_keyingset_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks = keyingset_get_from_op_with_error(op, op->type->prop, scene);
  if (ks == nullptr) {
    return OPERATOR_CANCELLED;
  }
  return insert_key_with_keyingset(C, op, ks);
}

void ANIM_OT_keyframe_insert_by_name(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Insert Keyframe (by name)";
  ot->idname = "ANIM_OT_keyframe_insert_by_name";
  ot->description = "Alternate access to 'Insert Keyframe' for keymaps to use";

  /* callbacks */
  ot->exec = keyframe_insert_with_keyingset_exec;
  ot->poll = modify_key_op_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* keyingset to use (idname) */
  prop = RNA_def_string(
      ot->srna, "type", nullptr, MAX_ID_NAME - 2, "Keying Set", "The Keying Set to use");
  RNA_def_property_string_search_func_runtime(
      prop, ANIM_keyingset_visit_for_search_no_poll, PROP_STRING_SEARCH_SUGGESTION);
  RNA_def_property_flag(prop, PROP_HIDDEN);
  ot->prop = prop;
}

/* Insert Key Operator (With Menu) ------------------------ */
/* This operator checks if a menu should be shown for choosing the KeyingSet to use,
 * then calls the menu if necessary before
 */

static int insert_key_menu_invoke(bContext *C, wmOperator *op, const wmEvent * /*event*/)
{
  Scene *scene = CTX_data_scene(C);

  /* When there is an active keying set and no request to prompt, keyframe immediately. */
  if ((scene->active_keyingset != 0) && !RNA_boolean_get(op->ptr, "always_prompt")) {
    /* Just call the exec() on the active keying-set. */
    RNA_enum_set(op->ptr, "type", 0);
    return op->type->exec(C, op);
  }

  /* Show a menu listing all keying-sets, the enum is expanded here to make use of the
   * operator that accesses the keying-set by name. This is important for the ability
   * to assign shortcuts to arbitrarily named keying sets. See #89560.
   * These menu items perform the key-frame insertion (not this operator)
   * hence the #OPERATOR_INTERFACE return. */
  uiPopupMenu *pup = UI_popup_menu_begin(
      C, WM_operatortype_name(op->type, op->ptr).c_str(), ICON_NONE);
  uiLayout *layout = UI_popup_menu_layout(pup);

  /* Even though `ANIM_OT_keyframe_insert_menu` can show a menu in one line,
   * prefer `ANIM_OT_keyframe_insert_by_name` so users can bind keys to specific
   * keying sets by name in the key-map instead of the index which isn't stable. */
  PropertyRNA *prop = RNA_struct_find_property(op->ptr, "type");
  const EnumPropertyItem *item_array = nullptr;
  int totitem;
  bool free;

  RNA_property_enum_items_gettexted(C, op->ptr, prop, &item_array, &totitem, &free);

  for (int i = 0; i < totitem; i++) {
    const EnumPropertyItem *item = &item_array[i];
    if (item->identifier[0] != '\0') {
      uiItemStringO(layout,
                    item->name,
                    item->icon,
                    "ANIM_OT_keyframe_insert_by_name",
                    "type",
                    item->identifier);
    }
    else {
      /* This enum shouldn't contain headings, assert there are none.
       * NOTE: If in the future the enum includes them, additional layout code can be
       * added to show them - although that doesn't seem likely. */
      BLI_assert(item->name == nullptr);
      uiItemS(layout);
    }
  }

  if (free) {
    MEM_freeN((void *)item_array);
  }

  UI_popup_menu_end(C, pup);

  return OPERATOR_INTERFACE;
}

void ANIM_OT_keyframe_insert_menu(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Insert Keyframe Menu";
  ot->idname = "ANIM_OT_keyframe_insert_menu";
  ot->description =
      "Insert Keyframes for specified Keying Set, with menu of available Keying Sets if undefined";

  /* callbacks */
  ot->invoke = insert_key_menu_invoke;
  ot->exec = keyframe_insert_with_keyingset_exec;
  ot->poll = ED_operator_areaactive;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* keyingset to use (dynamic enum) */
  prop = RNA_def_enum(
      ot->srna, "type", rna_enum_dummy_DEFAULT_items, 0, "Keying Set", "The Keying Set to use");
  RNA_def_enum_funcs(prop, ANIM_keying_sets_enum_itemf);
  RNA_def_property_flag(prop, PROP_HIDDEN);
  ot->prop = prop;

  /* whether the menu should always be shown
   * - by default, the menu should only be shown when there is no active Keying Set (2.5 behavior),
   *   although in some cases it might be useful to always shown (pre 2.5 behavior)
   */
  prop = RNA_def_boolean(ot->srna, "always_prompt", false, "Always Show Menu", "");
  RNA_def_property_flag(prop, PROP_HIDDEN);
}

/* Delete Key Operator ------------------------ */

static int delete_key_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks = keyingset_get_from_op_with_error(op, op->type->prop, scene);
  if (ks == nullptr) {
    return OPERATOR_CANCELLED;
  }

  return delete_key_using_keying_set(C, op, ks);
}

static int delete_key_using_keying_set(bContext *C, wmOperator *op, KeyingSet *ks)
{
  Scene *scene = CTX_data_scene(C);
  float cfra = BKE_scene_frame_get(scene);
  int num_channels;
  const bool confirm = op->flag & OP_IS_INVOKE;

  /* try to delete keyframes for the channels specified by KeyingSet */
  num_channels = ANIM_apply_keyingset(C, nullptr, ks, MODIFYKEY_MODE_DELETE, cfra);
  if (G.debug & G_DEBUG) {
    printf("KeyingSet '%s' - Successfully removed %d Keyframes\n", ks->name, num_channels);
  }

  /* report failure or do updates? */
  if (num_channels < 0) {
    BKE_report(op->reports, RPT_ERROR, "No suitable context info for active keying set");
    return OPERATOR_CANCELLED;
  }

  if (num_channels > 0) {
    WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_REMOVED, nullptr);
  }

  if (confirm) {
    /* if called by invoke (from the UI), make a note that we've removed keyframes */
    if (num_channels > 0) {
      BKE_reportf(op->reports,
                  RPT_INFO,
                  "Successfully removed %d keyframes for keying set '%s'",
                  num_channels,
                  ks->name);
    }
    else {
      BKE_report(op->reports, RPT_WARNING, "Keying set failed to remove any keyframes");
    }
  }
  return OPERATOR_FINISHED;
}

void ANIM_OT_keyframe_delete(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Delete Keying-Set Keyframe";
  ot->idname = "ANIM_OT_keyframe_delete";
  ot->description =
      "Delete keyframes on the current frame for all properties in the specified Keying Set";

  /* callbacks */
  ot->exec = delete_key_exec;
  ot->poll = modify_key_op_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* keyingset to use (dynamic enum) */
  prop = RNA_def_enum(
      ot->srna, "type", rna_enum_dummy_DEFAULT_items, 0, "Keying Set", "The Keying Set to use");
  RNA_def_enum_funcs(prop, ANIM_keying_sets_enum_itemf);
  RNA_def_property_flag(prop, PROP_HIDDEN);
  ot->prop = prop;
}

void ANIM_OT_keyframe_delete_by_name(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Delete Keying-Set Keyframe (by name)";
  ot->idname = "ANIM_OT_keyframe_delete_by_name";
  ot->description = "Alternate access to 'Delete Keyframe' for keymaps to use";

  /* callbacks */
  ot->exec = delete_key_exec;
  ot->poll = modify_key_op_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* keyingset to use (idname) */
  prop = RNA_def_string(
      ot->srna, "type", nullptr, MAX_ID_NAME - 2, "Keying Set", "The Keying Set to use");
  RNA_def_property_string_search_func_runtime(
      prop, ANIM_keyingset_visit_for_search_no_poll, PROP_STRING_SEARCH_SUGGESTION);
  RNA_def_property_flag(prop, PROP_HIDDEN);
  ot->prop = prop;
}

/* Delete Key Operator ------------------------ */
/* NOTE: Although this version is simpler than the more generic version for KeyingSets,
 * it is more useful for animators working in the 3D view.
 */

static int clear_anim_v3d_exec(bContext *C, wmOperator * /*op*/)
{
  bool changed = false;

  CTX_DATA_BEGIN (C, Object *, ob, selected_objects) {
    /* just those in active action... */
    if ((ob->adt) && (ob->adt->action)) {
      AnimData *adt = ob->adt;
      bAction *act = adt->action;
      FCurve *fcu, *fcn;

      for (fcu = static_cast<FCurve *>(act->curves.first); fcu; fcu = fcn) {
        bool can_delete = false;

        fcn = fcu->next;

        /* in pose mode, only delete the F-Curve if it belongs to a selected bone */
        if (ob->mode & OB_MODE_POSE) {
          if (fcu->rna_path) {
            /* Get bone-name, and check if this bone is selected. */
            bPoseChannel *pchan = nullptr;
            char bone_name[sizeof(pchan->name)];
            if (BLI_str_quoted_substr(fcu->rna_path, "pose.bones[", bone_name, sizeof(bone_name)))
            {
              pchan = BKE_pose_channel_find_name(ob->pose, bone_name);
              /* Delete if bone is selected. */
              if ((pchan) && (pchan->bone)) {
                if (pchan->bone->flag & BONE_SELECTED) {
                  can_delete = true;
                }
              }
            }
          }
        }
        else {
          /* object mode - all of Object's F-Curves are affected */
          can_delete = true;
        }

        /* delete F-Curve completely */
        if (can_delete) {
          blender::animrig::animdata_fcurve_delete(nullptr, adt, fcu);
          DEG_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);
          changed = true;
        }
      }

      /* Delete the action itself if it is empty. */
      if (blender::animrig::animdata_remove_empty_action(adt)) {
        changed = true;
      }
    }
  }
  CTX_DATA_END;

  if (!changed) {
    return OPERATOR_CANCELLED;
  }

  /* send updates */
  WM_event_add_notifier(C, NC_OBJECT | ND_KEYS, nullptr);

  return OPERATOR_FINISHED;
}

void ANIM_OT_keyframe_clear_v3d(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Animation";
  ot->description = "Remove all keyframe animation for selected objects";
  ot->idname = "ANIM_OT_keyframe_clear_v3d";

  /* callbacks */
  ot->invoke = WM_operator_confirm_or_exec;
  ot->exec = clear_anim_v3d_exec;

  ot->poll = ED_operator_areaactive;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
  WM_operator_properties_confirm_or_exec(ot);
}

static int delete_key_v3d_without_keying_set(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  const float cfra = BKE_scene_frame_get(scene);

  int selected_objects_len = 0;
  int selected_objects_success_len = 0;
  int success_multi = 0;

  const bool confirm = op->flag & OP_IS_INVOKE;

  CTX_DATA_BEGIN (C, Object *, ob, selected_objects) {
    ID *id = &ob->id;
    int success = 0;

    selected_objects_len += 1;

    /* just those in active action... */
    if ((ob->adt) && (ob->adt->action)) {
      AnimData *adt = ob->adt;
      bAction *act = adt->action;
      FCurve *fcu, *fcn;
      const float cfra_unmap = BKE_nla_tweakedit_remap(adt, cfra, NLATIME_CONVERT_UNMAP);

      for (fcu = static_cast<FCurve *>(act->curves.first); fcu; fcu = fcn) {
        fcn = fcu->next;

        /* don't touch protected F-Curves */
        if (BKE_fcurve_is_protected(fcu)) {
          BKE_reportf(op->reports,
                      RPT_WARNING,
                      "Not deleting keyframe for locked F-Curve '%s', object '%s'",
                      fcu->rna_path,
                      id->name + 2);
          continue;
        }

        /* Special exception for bones, as this makes this operator more convenient to use
         * NOTE: This is only done in pose mode.
         * In object mode, we're dealing with the entire object.
         */
        if (ob->mode & OB_MODE_POSE) {
          bPoseChannel *pchan = nullptr;

          /* Get bone-name, and check if this bone is selected. */
          char bone_name[sizeof(pchan->name)];
          if (!BLI_str_quoted_substr(fcu->rna_path, "pose.bones[", bone_name, sizeof(bone_name))) {
            continue;
          }
          pchan = BKE_pose_channel_find_name(ob->pose, bone_name);

          /* skip if bone is not selected */
          if ((pchan) && (pchan->bone)) {
            /* bones are only selected/editable if visible... */
            bArmature *arm = (bArmature *)ob->data;

            /* skipping - not visible on currently visible layers */
            if (!ANIM_bonecoll_is_visible_pchan(arm, pchan)) {
              continue;
            }
            /* skipping - is currently hidden */
            if (pchan->bone->flag & BONE_HIDDEN_P) {
              continue;
            }

            /* selection flag... */
            if ((pchan->bone->flag & BONE_SELECTED) == 0) {
              continue;
            }
          }
        }

        /* delete keyframes on current frame
         * WARNING: this can delete the next F-Curve, hence the "fcn" copying
         */
        success += blender::animrig::delete_keyframe_fcurve(adt, fcu, cfra_unmap);
      }
      DEG_id_tag_update(&ob->adt->action->id, ID_RECALC_ANIMATION_NO_FLUSH);
    }

    /* Only for reporting. */
    if (success) {
      selected_objects_success_len += 1;
      success_multi += success;
    }

    DEG_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);
  }
  CTX_DATA_END;

  if (selected_objects_success_len) {
    /* send updates */
    WM_event_add_notifier(C, NC_OBJECT | ND_KEYS, nullptr);
  }

  if (confirm) {
    /* if called by invoke (from the UI), make a note that we've removed keyframes */
    if (selected_objects_success_len) {
      BKE_reportf(op->reports,
                  RPT_INFO,
                  "%d object(s) successfully had %d keyframes removed",
                  selected_objects_success_len,
                  success_multi);
    }
    else {
      BKE_reportf(
          op->reports, RPT_ERROR, "No keyframes removed from %d object(s)", selected_objects_len);
    }
  }
  return OPERATOR_FINISHED;
}

static int delete_key_v3d_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks = ANIM_scene_get_active_keyingset(scene);

  if (ks == nullptr) {
    return delete_key_v3d_without_keying_set(C, op);
  }

  return delete_key_using_keying_set(C, op, ks);
}

void ANIM_OT_keyframe_delete_v3d(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Delete Keyframe";
  ot->description = "Remove keyframes on current frame for selected objects and bones";
  ot->idname = "ANIM_OT_keyframe_delete_v3d";

  /* callbacks */
  ot->invoke = WM_operator_confirm_or_exec;
  ot->exec = delete_key_v3d_exec;

  ot->poll = ED_operator_areaactive;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
  WM_operator_properties_confirm_or_exec(ot);
}

/* Insert Key Button Operator ------------------------ */

static int insert_key_button_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ToolSettings *ts = scene->toolsettings;
  PointerRNA ptr = {nullptr};
  PropertyRNA *prop = nullptr;
  char *path;
  uiBut *but;
  const AnimationEvalContext anim_eval_context = BKE_animsys_eval_context_construct(
      CTX_data_depsgraph_pointer(C), BKE_scene_frame_get(scene));
  bool changed = false;
  int index;
  const bool all = RNA_boolean_get(op->ptr, "all");
  eInsertKeyFlags flag = INSERTKEY_NOFLAGS;

  /* flags for inserting keyframes */
  flag = ANIM_get_keyframing_flags(scene, true);

  if (!(but = UI_context_active_but_prop_get(C, &ptr, &prop, &index))) {
    /* pass event on if no active button found */
    return (OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH);
  }

  if ((ptr.owner_id && ptr.data && prop) && RNA_property_animateable(&ptr, prop)) {
    if (ptr.type == &RNA_NlaStrip) {
      /* Handle special properties for NLA Strips, whose F-Curves are stored on the
       * strips themselves. These are stored separately or else the properties will
       * not have any effect.
       */
      NlaStrip *strip = static_cast<NlaStrip *>(ptr.data);
      FCurve *fcu = BKE_fcurve_find(&strip->fcurves, RNA_property_identifier(prop), index);

      if (fcu) {
        changed = blender::animrig::insert_keyframe_direct(
            op->reports,
            ptr,
            prop,
            fcu,
            &anim_eval_context,
            eBezTriple_KeyframeType(ts->keyframe_type),
            nullptr,
            eInsertKeyFlags(0));
      }
      else {
        BKE_report(op->reports,
                   RPT_ERROR,
                   "This property cannot be animated as it will not get updated correctly");
      }
    }
    else if (UI_but_flag_is_set(but, UI_BUT_DRIVEN)) {
      /* Driven property - Find driver */
      FCurve *fcu;
      bool driven, special;

      fcu = BKE_fcurve_find_by_rna_context_ui(
          C, &ptr, prop, index, nullptr, nullptr, &driven, &special);

      if (fcu && driven) {
        changed = blender::animrig::insert_keyframe_direct(
            op->reports,
            ptr,
            prop,
            fcu,
            &anim_eval_context,
            eBezTriple_KeyframeType(ts->keyframe_type),
            nullptr,
            INSERTKEY_DRIVER);
      }
    }
    else {
      /* standard properties */
      path = RNA_path_from_ID_to_property(&ptr, prop);

      if (path) {
        const char *identifier = RNA_property_identifier(prop);
        const char *group = nullptr;

        /* Special exception for keyframing transforms:
         * Set "group" for this manually, instead of having them appearing at the bottom
         * (ungrouped) part of the channels list.
         * Leaving these ungrouped is not a nice user behavior in this case.
         *
         * TODO: Perhaps we can extend this behavior in future for other properties...
         */
        if (ptr.type == &RNA_PoseBone) {
          bPoseChannel *pchan = static_cast<bPoseChannel *>(ptr.data);
          group = pchan->name;
        }
        else if ((ptr.type == &RNA_Object) &&
                 (strstr(identifier, "location") || strstr(identifier, "rotation") ||
                  strstr(identifier, "scale")))
        {
          /* NOTE: Keep this label in sync with the "ID" case in
           * keyingsets_utils.py :: get_transform_generators_base_info()
           */
          group = "Object Transforms";
        }

        if (all) {
          /* -1 indicates operating on the entire array (or the property itself otherwise) */
          index = -1;
        }

        changed = (blender::animrig::insert_keyframe(bmain,
                                                     op->reports,
                                                     ptr.owner_id,
                                                     nullptr,
                                                     group,
                                                     path,
                                                     index,
                                                     &anim_eval_context,
                                                     eBezTriple_KeyframeType(ts->keyframe_type),
                                                     flag) != 0);

        MEM_freeN(path);
      }
      else {
        BKE_report(op->reports,
                   RPT_WARNING,
                   "Failed to resolve path to property, "
                   "try manually specifying this using a Keying Set instead");
      }
    }
  }
  else {
    if (prop && !RNA_property_animateable(&ptr, prop)) {
      BKE_reportf(op->reports,
                  RPT_WARNING,
                  "\"%s\" property cannot be animated",
                  RNA_property_identifier(prop));
    }
    else {
      BKE_reportf(op->reports,
                  RPT_WARNING,
                  "Button doesn't appear to have any property information attached (ptr.data = "
                  "%p, prop = %p)",
                  ptr.data,
                  (void *)prop);
    }
  }

  if (changed) {
    ID *id = ptr.owner_id;
    AnimData *adt = BKE_animdata_from_id(id);
    if (adt->action != nullptr) {
      DEG_id_tag_update(&adt->action->id, ID_RECALC_ANIMATION_NO_FLUSH);
    }
    DEG_id_tag_update(id, ID_RECALC_ANIMATION_NO_FLUSH);

    /* send updates */
    UI_context_update_anim_flag(C);

    /* send notifiers that keyframes have been changed */
    WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_ADDED, nullptr);
  }

  return (changed) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_keyframe_insert_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Insert Keyframe (Buttons)";
  ot->idname = "ANIM_OT_keyframe_insert_button";
  ot->description = "Insert a keyframe for current UI-active property";

  /* callbacks */
  ot->exec = insert_key_button_exec;
  ot->poll = modify_key_op_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;

  /* properties */
  RNA_def_boolean(ot->srna, "all", true, "All", "Insert a keyframe for all element of the array");
}

/* Delete Key Button Operator ------------------------ */

static int delete_key_button_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  PointerRNA ptr = {nullptr};
  PropertyRNA *prop = nullptr;
  Main *bmain = CTX_data_main(C);
  char *path;
  const float cfra = BKE_scene_frame_get(scene);
  bool changed = false;
  int index;
  const bool all = RNA_boolean_get(op->ptr, "all");

  if (!UI_context_active_but_prop_get(C, &ptr, &prop, &index)) {
    /* pass event on if no active button found */
    return (OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH);
  }

  if (ptr.owner_id && ptr.data && prop) {
    if (BKE_nlastrip_has_curves_for_property(&ptr, prop)) {
      /* Handle special properties for NLA Strips, whose F-Curves are stored on the
       * strips themselves. These are stored separately or else the properties will
       * not have any effect.
       */
      ID *id = ptr.owner_id;
      NlaStrip *strip = static_cast<NlaStrip *>(ptr.data);
      FCurve *fcu = BKE_fcurve_find(&strip->fcurves, RNA_property_identifier(prop), 0);

      if (fcu) {
        if (BKE_fcurve_is_protected(fcu)) {
          BKE_reportf(
              op->reports,
              RPT_WARNING,
              "Not deleting keyframe for locked F-Curve for NLA Strip influence on %s - %s '%s'",
              strip->name,
              BKE_idtype_idcode_to_name(GS(id->name)),
              id->name + 2);
        }
        else {
          /* remove the keyframe directly
           * NOTE: cannot use delete_keyframe_fcurve(), as that will free the curve,
           *       and delete_keyframe() expects the FCurve to be part of an action
           */
          bool found = false;
          int i;

          /* try to find index of beztriple to get rid of */
          i = BKE_fcurve_bezt_binarysearch_index(fcu->bezt, cfra, fcu->totvert, &found);
          if (found) {
            /* delete the key at the index (will sanity check + do recalc afterwards) */
            BKE_fcurve_delete_key(fcu, i);
            BKE_fcurve_handles_recalc(fcu);
            changed = true;
          }
        }
      }
    }
    else {
      /* standard properties */
      path = RNA_path_from_ID_to_property(&ptr, prop);

      if (path) {
        if (all) {
          /* -1 indicates operating on the entire array (or the property itself otherwise) */
          index = -1;
        }

        changed = blender::animrig::delete_keyframe(
                      bmain, op->reports, ptr.owner_id, nullptr, path, index, cfra) != 0;
        MEM_freeN(path);
      }
      else if (G.debug & G_DEBUG) {
        printf("Button Delete-Key: no path to property\n");
      }
    }
  }
  else if (G.debug & G_DEBUG) {
    printf("ptr.data = %p, prop = %p\n", ptr.data, (void *)prop);
  }

  if (changed) {
    /* send updates */
    UI_context_update_anim_flag(C);

    /* send notifiers that keyframes have been changed */
    WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_REMOVED, nullptr);
  }

  return (changed) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_keyframe_delete_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Delete Keyframe (Buttons)";
  ot->idname = "ANIM_OT_keyframe_delete_button";
  ot->description = "Delete current keyframe of current UI-active property";

  /* callbacks */
  ot->exec = delete_key_button_exec;
  ot->poll = modify_key_op_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;

  /* properties */
  RNA_def_boolean(ot->srna, "all", true, "All", "Delete keyframes from all elements of the array");
}

/* Clear Key Button Operator ------------------------ */

static int clear_key_button_exec(bContext *C, wmOperator *op)
{
  PointerRNA ptr = {nullptr};
  PropertyRNA *prop = nullptr;
  Main *bmain = CTX_data_main(C);
  char *path;
  bool changed = false;
  int index;
  const bool all = RNA_boolean_get(op->ptr, "all");

  if (!UI_context_active_but_prop_get(C, &ptr, &prop, &index)) {
    /* pass event on if no active button found */
    return (OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH);
  }

  if (ptr.owner_id && ptr.data && prop) {
    path = RNA_path_from_ID_to_property(&ptr, prop);

    if (path) {
      if (all) {
        /* -1 indicates operating on the entire array (or the property itself otherwise) */
        index = -1;
      }

      changed |=
          (blender::animrig::clear_keyframe(
               bmain, op->reports, ptr.owner_id, nullptr, path, index, eInsertKeyFlags(0)) != 0);
      MEM_freeN(path);
    }
    else if (G.debug & G_DEBUG) {
      printf("Button Clear-Key: no path to property\n");
    }
  }
  else if (G.debug & G_DEBUG) {
    printf("ptr.data = %p, prop = %p\n", ptr.data, (void *)prop);
  }

  if (changed) {
    /* send updates */
    UI_context_update_anim_flag(C);

    /* send notifiers that keyframes have been changed */
    WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_REMOVED, nullptr);
  }

  return (changed) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_keyframe_clear_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Keyframe (Buttons)";
  ot->idname = "ANIM_OT_keyframe_clear_button";
  ot->description = "Clear all keyframes on the currently active property";

  /* callbacks */
  ot->exec = clear_key_button_exec;
  ot->poll = modify_key_op_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;

  /* properties */
  RNA_def_boolean(ot->srna, "all", true, "All", "Clear keyframes from all elements of the array");
}

/* ******************************************* */
/* KEYFRAME DETECTION */

/* --------------- API/Per-Datablock Handling ------------------- */

bool fcurve_frame_has_keyframe(const FCurve *fcu, float frame)
{
  /* quick sanity check */
  if (ELEM(nullptr, fcu, fcu->bezt)) {
    return false;
  }

  if ((fcu->flag & FCURVE_MUTED) == 0) {
    bool replace;
    int i = BKE_fcurve_bezt_binarysearch_index(fcu->bezt, frame, fcu->totvert, &replace);

    /* BKE_fcurve_bezt_binarysearch_index will set replace to be 0 or 1
     * - obviously, 1 represents a match
     */
    if (replace) {
      /* sanity check: 'i' may in rare cases exceed arraylen */
      if ((i >= 0) && (i < fcu->totvert)) {
        return true;
      }
    }
  }

  return false;
}

bool fcurve_is_changed(ApiPtr ptr,
                       ApiProp *prop,
                       FCurve *fcu,
                       const AnimEvalCxt *anim_eval_cxt)
{
  PathResolvedRNA anim_rna;
  anim_api.ptr = ptr;
  anim_api.prop = prop;
  anim_api.prop_index = fcu->array_index;

  int index = fcu->array_index;
  dune::Vector<float> vals = dune::animrig::get_api_vals(&ptr, prop);

  float fcurve_val = calc_fcurve(&anim_api, fcu, anim_eval_cxt);
  float cur_val = (index >= 0 && index < vals.size()) ? vals[index] : 0.0f;

  return !compare_ff_relative(fcurve_val, cur_val, FLT_EPSILON, 64);
}

/* Checks whether an Action has a keyframe for a given frame
 * Since we're only concerned whether a keyframe exists,
 * Simple loop until a match is found. */
static bool action_frame_has_keyframe(Action *act, float frame)
{
  /* can only find if there is data */
  if (act == nullptr) {
    return false;
  }

  if (act->flag & ACT_MUTED) {
    return false;
  }

  /* loop over F-Curves, using binary-search to try to find matches
   * Assumes that keyframes are only beztriples */
  LIST_FOREACH (FCurve *, fcu, &act->curves) {
    /* only check if there are keyframes (currently only of type BezTriple) */
    if (fcu->bezt && fcu->totvert) {
      if (fcurve_frame_has_keyframe(fcu, frame)) {
        return true;
      }
    }
  }

  /* nothing found */
  return false;
}

/* Checks whether an Ob has a keyframe for a given frame */
static bool ob_frame_has_keyframe(Ob *ob, float frame)
{
  /* error checking */
  if (ob == nullptr) {
    return false;
  }

  /* check own anim data - specifically, the action it contains */
  if ((ob->adt) && (ob->adt->action)) {
    /* #41525 - When the active action is a NLA strip being edited,
     * we need to correct the frame number to "look inside" the
     * remapped action */
    float ob_frame = dune_nla_tweakedit_remap(ob->adt, frame, NLATIME_CONVERT_UNMAP);

    if (action_frame_has_keyframe(ob->adt->action, ob_frame)) {
      return true;
    }
  }

  /* nothing found */
  return false;
}

/* API */

bool id_frame_has_keyframe(Id *id, float frame)
{
  /* sanity checks */
  if (id == nullptr) {
    return false;
  }

  /* perform special checks for 'macro' types */
  switch (GS(id->name)) {
    case ID_OB: /* object */
      return ob_frame_has_keyframe((Object *)id, frame);
#if 0
    /* XXX TODO... for now, just use 'normal' behavior */
    case ID_SCE: /* scene */
      break;
#endif
    default: /* 'normal type' */
    {
      AnimData *adt = dune_animdata_from_id(id);

      /* only check keyframes in active action */
      if (adt) {
        return action_frame_has_keyframe(adt->action, frame);
      }
      break;
    }
  }

  /* no keyframe found */
  return false;
}

/* Internal Utils */
/* Use for insert/delete key-frame. */
static KeyingSet *keyingset_get_from_op_with_error(WinOp *op, ApiProp *prop, Scene *scene)
{
  KeyingSet *ks = nullptr;
  const int prop_type = api_prop_type(prop);
  if (prop_type == PROP_ENUM) {
    int type = api_prop_enum_get(op->ptr, prop);
    ks = anim_keyingset_get_from_enum_type(scene, type);
    if (ks == nullptr) {
      dune_report(op->reports, RPT_ERROR, "No active Keying Set");
    }
  }
  else if (prop_type == PROP_STRING) {
    char type_id[MAX_ID_NAME - 2];
    api_prop_string_get(op->ptr, prop, type_id);
    ks = anim_keyingset_get_from_idname(scene, type_id);

    if (ks == nullptr) {
      BKE_reportf(op->reports, RPT_ERROR, "Keying set '%s' not found", type_id);
    }
  }
  else {
    BLI_assert(0);
  }
  return ks;
}

/** \} */
