/* Implementation of Bone Collection ops and editing API's. */
#include <cstring>

#include "anim_bone_collections.hh"

#include "types_id.h"
#include "types_ob.h"

#include "dune_action.h"
#include "dune_cxt.hh"
#include "dune_layer.h"
#include "dune_report.h"

#include "lang.h"

#include "graph.hh"

#include "api_access.hh"
#include "api_define.hh"
#include "api_enum_types.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_ob.hh"
#include "ed_outliner.hh"
#include "ed_screen.hh"

#include "ui.hh"
#include "ui_resources.hh"

#include "armature_intern.h"

struct WinOp;

/* Bone collections */
static bool bone_collection_add_poll(Cxt *C)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return false;
  }

  if (ob->type != OB_ARMATURE) {
    cxt_win_op_poll_msg_set(C, "Bone collections can only be added to an Armature");
    return false;
  }

  if (ID_IS_LINKED(ob->data)) {
    cxt_win_op_poll_msg_set(
        C, "Cannot add bone collections to a linked Armature wo an override");
    return false;
  }

  return true;
}

/* Allow edits of local bone collection only (full local or local override). */
static bool active_bone_collection_poll(Cxt *C)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return false;
  }

  if (ob->type != OB_ARMATURE) {
    cxt_win_op_poll_msg_set(C, "Bone collections can only be edited on an Armature");
    return false;
  }

  Armature *armature = static_cast<Armature *>(ob->data);
  BoneCollection *bcoll = armature->runtime.active_collection;

  if (bcoll == nullptr) {
    cxt_win_op_poll_msg_set(C, "Armature has no active bone collection, sel one first");
    return false;
  }

  if (!anim_armature_bonecoll_is_editable(armature, bcoll)) {
    cxt_win_op_poll_msg_set(
        C, "Cannot edit bone collections that are linked from another dune file");
    return false;
  }
  return true;
}

static int bone_collection_add_ex(Cxt *C, WinOp * /*op*/)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return OP_CANCELLED;
  }

  Armature *armature = static_cast<Armature *>(ob->data);
  BoneCollection *bcoll = anim_armature_bonecoll_new(armature, nullptr);
  anim_armature_bonecoll_active_set(armature, bcoll);

  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
  return OP_FINISHED;
}

void ARMATURE_OT_collection_add(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Bone Collection";
  ot->idname = "ARMATURE_OT_collection_add";
  ot->description = "Add a new bone collection";

  /* api cbs */
  ot->ex = bone_collection_add_ex;
  ot->poll = bone_collection_add_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int bone_collection_remove_ex(Cxt *C, WinOp * /*op*/)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return OP_CANCELLED;
  }

  /* The poll fn ensures armature->active_collection is not NULL. */
  Armature *armature = static_cast<Armature *>(ob->data);
  anim_armature_bonecoll_remove(armature, armature->runtime.active_collection);

  /* notifiers for updates */
  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
  graph_id_tag_update(&armature->id, ID_RECALC_SEL);

  return OP_FINISHED;
}

void ARMATURE_OT_collection_remove(WinOpType *ot)
{
  /* ids */
  ot->name = "Remove Bone Collection";
  ot->idname = "ARMATURE_OT_collection_remove";
  ot->description = "Remove the active bone collection";

  /* api cbs */
  ot->ex = bone_collection_remove_ex;
  ot->poll = active_bone_collection_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int bone_collection_move_ex(Cxt *C, WinOp *op)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return OP_CANCELLED;
  }
  const int direction = api_enum_get(op->ptr, "direction");

  /* Poll fn makes sure this is valid. */
  Armature *armature = static_cast<Armature *>(ob->data);

  const bool ok = anim_armature_bonecoll_move(
      armature, armature->runtime.active_collection, direction);
  if (!ok) {
    return OP_CANCELLED;
  }

  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
  return OP_FINISHED;
}

void ARMATURE_OT_collection_move(WinOpType *ot)
{
  static const EnumPropItem bcoll_slot_move[] = {
      {-1, "UP", 0, "Up", ""},
      {1, "DOWN", 0, "Down", ""},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* ids */
  ot->name = "Move Bone Collection";
  ot->idname = "ARMATURE_OT_collection_move";
  ot->description = "Change position of active Bone Collection in list of Bone collections";

  /* api cbs */
  ot->ex = bone_collection_move_ex;
  ot->poll = active_bone_collection_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_enum(ot->sapi,
               "direction",
               bcoll_slot_move,
               0,
               "Direction",
               "Direction to move the active Bone Collection towards");
}

typedef enum eMayCreate {
  FAIL_IF_MISSING = 0,
  CREATE_IF_MISSING = 1,
} eMayCreate;

static BoneCollection *get_bonecoll_named_or_active(Cxt * /*C*/,
                                                    WinOp *op,
                                                    Ob *ob,
                                                    const eMayCreate may_create)
{
  Armature *armature = static_cast<Armature *>(ob->data);

  char bcoll_name[MAX_NAME];
  api_string_get(op->ptr, "name", bcoll_name);

  if (bcoll_name[0] == '\0') {
    return armature->runtime.active_collection;
  }

  BoneCollection *bcoll = anim_armature_bonecoll_get_by_name(armature, bcoll_name);
  if (bcoll) {
    return bcoll;
  }

  switch (may_create) {
    case CREATE_IF_MISSING:
      bcoll = anim_armature_bonecoll_new(armature, bcoll_name);
      anim_armature_bonecoll_active_set(armature, bcoll);
      return bcoll;
    case FAIL_IF_MISSING:
      win_reportf(RPT_ERROR, "No bone collection named '%s'", bcoll_name);
      return nullptr;
  }

  return nullptr;
}

using assign_bone_fn = bool (*)(BoneCollection *bcoll, Bone *bone);
using assign_ebone_fn = bool (*)(BoneCollection *bcoll, EditBone *ebone);

/* The following 3 fns either assign or unassign, depending on the
 * 'assign_bone_fn'/'assign_ebone_fn' they get passed. */
static void bone_collection_assign_pchans(Cxt *C,
                                          Ob *ob,
                                          BoneCollection *bcoll,
                                          assign_bone_fn assign_fn,
                                          bool *made_any_changes,
                                          bool *had_bones_to_assign)
{
  /* TODO: support multi-ob pose mode. */
  FOREACH_PCHAN_SEL_IN_OB_BEGIN (ob, pchan) {
    *made_any_changes |= assign_fn(bcoll, pchan->bone);
    *had_bones_to_assign = true;
  }
  FOREACH_PCHAN_SEL_IN_OB_END;

  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);

  Armature *arm = static_cast<Armature *>(ob->data);
  graph_id_tag_update(&arm->id, ID_RECALC_SEL); /* Recreate the drw bufs. */
}

static void bone_collection_assign_editbones(Cxt *C,
                                             Ob *ob,
                                             BoneCollection *bcoll,
                                             assign_ebone_fn assign_fn,
                                             bool *made_any_changes,
                                             bool *had_bones_to_assign)
{
  Armature *arm = static_cast<Armature *>(ob->data);
  ed_armature_edit_sync_sel(arm->edbo);

  LIST_FOREACH (EditBone *, ebone, arm->edbo) {
    if (!EBONE_EDITABLE(ebone)) {
      continue;
    }
    *made_any_changes |= assign_fn(bcoll, ebone);
    *had_bones_to_assign = true;
  }

  ed_armature_edit_sync_sel(arm->edbo);
  win_ev_add_notifier(C, NC_OB | ND_BONE_COLLECTION, ob);
  graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
}

/* Assign or unassign all sel bones to/from the given bone collection.
 * return whether the current mode is actually supported. */
static bool bone_collection_assign_mode_specific(Cxt *C,
                                                 Ob *ob,
                                                 BoneCollection *bcoll,
                                                 assign_bone_fn assign_bone_fn,
                                                 assign_ebone_fn assign_ebone_fn,
                                                 bool *made_any_changes,
                                                 bool *had_bones_to_assign)
{
  switch (cxt_data_mode_enum(C)) {
    case CXT_MODE_POSE: {
      bone_collection_assign_pchans(
          C, ob, bcoll, assign_bone_fn, made_any_changes, had_bones_to_assign);
      return true;
    }

    case CXT_MODE_EDIT_ARMATURE: {
      uint obs_len = 0;
      Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
          cxt_data_scene(C), cxt_data_view_layer(C), cxt_win_view3d(C), &obs_len);

      for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
        Ob *ob = objects[ob_index];
        bone_collection_assign_editbones(
            C, ob, bcoll, assign_ebone_fn, made_any_changes, had_bones_to_assign);
      }

      mem_free(obs);
      ed_outliner_sel_sync_from_edit_bone_tag(C);
      return true;
    }

    default:
      return false;
  }
}

/* Assign or unassign the named bone to/from the given bone 
 * return whether the current mode is actually supported. */
static bool bone_collection_assign_named_mode_specific(Cxt *C,
                                                       Ob *ob,
                                                       BoneCollection *bcoll,
                                                       const char *bone_name,
                                                       assign_bone_fn assign_bone_fn,
                                                       assign_ebone_fn assign_ebone_fn,
                                                       bool *made_any_changes,
                                                       bool *had_bones_to_assign)
{
  Armature *arm = static_cast<Armature *>(ob->data);

  switch (cxt_data_mode_enum(C)) {
    case CXT_MODE_POSE: {
      PoseChannel *pchan = dune_pose_channel_find_name(ob->pose, bone_name);
      if (!pchan) {
        return true;
      }

      *had_bones_to_assign = true;
      *made_any_changes |= assign_bone_fn(bcoll, pchan->bone);

      win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
      win_ev_add_notifier(C, NC_OB | ND_BONE_COLLECTION, ob);
      graph_id_tag_update(&arm->id, ID_RECALC_SEL); /* Recreate the drw bufs. */
      return true;
    }

    case CXT_MODE_EDIT_ARMATURE: {
      EditBone *ebone = ed_armature_ebone_find_name(arm->edbo, bone_name);
      if (!ebone) {
        return true;
      }

      *had_bones_to_assign = true;
      *made_any_changes |= assign_ebone_fn(bcoll, ebone);

      ed_armature_edit_sync_sel(arm->edbo);
      win_ev_add_notifier(C, NC_OB | ND_BONE_COLLECTION, ob);
      graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
      return true;
    }

    default:
      return false;
  }
}

static bool bone_collection_assign_poll(Cxt *C)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return false;
  }

  if (ob->type != OB_ARMATURE) {
    cxt_win_op_poll_msg_set(C, "Bone collections can only be edited on an Armature");
    return false;
  }

  if (ID_IS_LINKED(ob->data)) {
    cxt_win_op_poll_msg_set(
        C, "Cannot edit bone collections on linked Armatures without override");
    return false;
  }

  /* The target bone collection can be specified by name in an op prop, but that's not
   * available here. So just allow in the poll fn, and do the final check in the execute. */
  return true;
}

/* Assign sel pchans to the bone collection that the user sels */
static int bone_collection_assign_ex(Cxt *C, WinOp *op)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return OP_CANCELLED;
  }

  BoneCollection *bcoll = get_bonecoll_named_or_active(C, op, ob, CREATE_IF_MISSING);
  if (bcoll == nullptr) {
    return OP_CANCELLED;
  }

  Armature *armature = static_cast<Armature *>(ob->data);
  if (!anim_armature_bonecoll_is_editable(armature, bcoll)) {
    win_reportf(RPT_ERROR, "Cannot assign to linked bone collection %s", bcoll->name);
    return OP_CANCELLED;
  }

  bool made_any_changes = false;
  bool had_bones_to_assign = false;
  const bool mode_is_supported = bone_collection_assign_mode_specific(
      C,
      ob,
      bcoll,
      anim_armature_bonecoll_assign,
      anim_armature_bonecoll_assign_editbone,
      &made_any_changes,
      &had_bones_to_assign);

  if (!mode_is_supported) {
    win_report(RPT_ERROR, "This op only works in pose mode and armature edit mode");
    return OP_CANCELLED;
  }
  if (!had_bones_to_assign) {
    win_report(RPT_WARNING, "No bones sel, nothing to assign to bone collection");
    return OP_CANCELLED;
  }
  if (!made_any_changes) {
    win_report(RPT_WARNING, "All sel bones were alrdy part of this collection");
    return OP_CANCELLED;
  }

  win_main_add_notifier(NC_OB | ND_BONE_COLLECTION, &ob->id);
  return OP_FINISHED;
}

void ARMATURE_OT_collection_assign(WinOpType *ot)
{
  /* ids */
  ot->name = "Add Sel Bones to Collection";
  ot->idname = "ARMATURE_OT_collection_assign";
  ot->description = "Add sel bones to the chosen bone collection";

  /* api cbs */
  // TODO: reinstate the menu?
  // ot->invoke = bone_collections_menu_invoke;
  ot->ex = bone_collection_assign_ex;
  ot->poll = bone_collection_assign_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_string(ot->sapi,
                 "name",
                 nullptr,
                 MAX_NAME,
                 "Bone Collection",
                 "Name of the bone collection to assign this bone to; empty to assign to the "
                 "active bone collection");
}

static int bone_collection_unassign_ex(Cxt *C, WinOp *op)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return OP_CANCELLED;
  }

  BoneCollection *bcoll = get_bonecoll_named_or_active(C, op, ob, FAIL_IF_MISSING);
  if (bcoll == nullptr) {
    return OP_CANCELLED;
  }

  bool made_any_changes = false;
  bool had_bones_to_unassign = false;
  const bool mode_is_supported = bone_collection_assign_mode_specific(
      C,
      ob,
      bcoll,
      anim_armature_bonecoll_unassign,
      anim_armature_bonecoll_unassign_editbone,
      &made_any_changes,
      &had_bones_to_unassign);

  if (!mode_is_supported) {
    win_report(RPT_ERROR, "This op only works in pose mode and armature edit mode");
    return OP_CANCELLED;
  }
  if (!had_bones_to_unassign) {
    win_report(RPT_WARNING, "No bones sel, nothing to unassign from bone collection");
    return OP_CANCELLED;
  }
  if (!made_any_changes) {
    win_report(RPT_WARNING, "None of the sel bones were assigned to this collection");
    return OP_CANCELLED;
  }
  return OP_FINISHED;
}

void ARMATURE_OT_collection_unassign(WinOpType *ot)
{
  /* ids */
  ot->name = "Remove Sel from Bone collections";
  ot->idname = "ARMATURE_OT_collection_unassign";
  ot->description = "Remove sel bones from the active bone collection";

  /* api cbs */
  ot->ex = bone_collection_unassign_ex;
  ot->poll = bone_collection_assign_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_string(ot->sapi,
                 "name",
                 nullptr,
                 MAX_NAME,
                 "Bone Collection",
                 "Name of the bone collection to unassign this bone from; empty to unassign from "
                 "the active bone collection");
}

static int bone_collection_unassign_named_ex(Cxt *C, WinOp *op)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return OP_CANCELLED;
  }

  BoneCollection *bcoll = get_bonecoll_named_or_active(C, op, ob, FAIL_IF_MISSING);
  if (bcoll == nullptr) {
    return OP_CANCELLED;
  }

  char bone_name[MAX_NAME];
  api_string_get(op->ptr, "bone_name", bone_name);
  if (!bone_name[0]) {
    win_report(RPT_ERROR, "Missing bone name");
    return OP_CANCELLED;
  }

  bool made_any_changes = false;
  bool had_bones_to_unassign = false;
  const bool mode_is_supported = bone_collection_assign_named_mode_specific(
      C,
      ob,
      bcoll,
      bone_name,
      anim_armature_bonecoll_unassign,
      anim_armature_bonecoll_unassign_editbone,
      &made_any_changes,
      &had_bones_to_unassign);

  if (!mode_is_supported) {
    win_report(RPT_ERROR, "This op only works in pose mode and armature edit mode");
    return OP_CANCELLED;
  }
  if (!had_bones_to_unassign) {
    win_reportf(RPT_WARNING, "Could not find bone '%s'", bone_name);
    return OP_CANCELLED;
  }
  if (!made_any_changes) {
    win_reportf(
        RPT_WARNING, "Bone '%s' was not assigned to collection '%s'", bone_name, bcoll->name);
    return OP_CANCELLED;
  }
  return OP_FINISHED;
}

void ARMATURE_OT_collection_unassign_named(WinOpType *ot)
{
  /* ids */
  ot->name = "Remove Bone from Bone collections";
  ot->idname = "ARMATURE_OT_collection_unassign_named";
  ot->description = "Unassign the bone from this bone collection";

  /* api cbs */
  ot->ex = bone_collection_unassign_named_ex;
  ot->poll = bone_collection_assign_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_string(ot->sapi,
                 "name",
                 nullptr,
                 MAX_NAME,
                 "Bone Collection",
                 "Name of the bone collection to unassign this bone from; empty to unassign from "
                 "the active bone collection");
  api_def_string(ot->sapi,
                 "bone_name",
                 nullptr,
                 MAX_NAME,
                 "Bone Name",
                 "Name of the bone to unassign from the collection; empty to use the active bone");
}

static bool editbone_is_member(const EditBone *ebone, const BoneCollection *bcoll)
{
  LIST_FOREACH (BoneCollectionRef *, ref, &ebone->bone_collections) {
    if (ref->bcoll == bcoll) {
      return true;
    }
  }
  return false;
}

static bool armature_bone_sel_poll(Cxt *C)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr || ob->type != OB_ARMATURE) {
    return false;
  }

  /* For bone sel, at least the pose should be editable to actually store
   * the sel state. */
  if (ID_IS_LINKED(ob) && !ID_IS_OVERRIDE_LIB(ob)) {
    cxt_win_op_poll_msg_set(
        C, "Cannot (de)sel bones on linked ob, that would need an override");
    return false;
  }

  const Armature *armature = reinterpret_cast<Armature *>(ob->data);
  if (armature->runtime.active_collection == nullptr) {
    cxt_win_op_poll_msg_set(C, "No active bone collection");
    return false;
  }
  return true;
}

static void bone_collection_sel(Cxt *C,
                                Ob *ob,
                                BoneCollection *bcoll,
                                const bool select)
{
  Armature *armature = static_cast<Armature *>(ob->data);
  const bool is_editmode = armature->edbo != nullptr;

  if (is_editmode) {
    LIST_FOREACH (EditBone *, ebone, armature->edbo) {
      if (!EBONE_SEL(armature, ebone)) {
        continue;
      }
      if (!editbone_is_member(ebone, bcoll)) {
        continue;
      }
      ed_armature_ebone_sel_set(ebone, sel);
    }
  }
  else {
    LIST_FOREACH (BoneCollectionMember *, member, &bcoll->bones) {
      Bone *bone = member->bone;
      if (!anim_bone_is_visible(armature, bone)) {
        continue;
      }
      if (bone->flag & BONE_UNSELECTABLE) {
        continue;
      }

      if (sel) {
        bone->flag |= BONE_SEL;
      }
      else {
        bone->flag &= ~BONE_SEL;
      }
    }
  }

  graph_id_tag_update(&armature->id, ID_RECALC_SEL);
  win_ev_add_notifier(C, NC_OB | ND_BONE_COLLECTION, ob);

  if (is_editmode) {
    ed_outliner_sel_sync_from_edit_bone_tag(C);
  }
  else {
    ed_outliner_sel_sync_from_pose_bone_tag(C);
  }
}

static int bone_collection_sel_ex(Cxt *C, WinOp * /*op*/)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return OP_CANCELLED;
  }

  Armature *armature = reinterpret_cast<Armature *>(ob->data);
  BoneCollection *bcoll = armature->runtime.active_collection;
  if (bcoll == nullptr) {
    return OP_CANCELLED;
  }

  bone_collection_sel(C, ob, bcoll, true);
  return OP_FINISHED;
}

void ARMATURE_OT_collection_sel(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel Bones of Bone Collection";
  ot->idname = "ARMATURE_OT_collection_sel";
  ot->description = "Sel bones in active Bone Collection";

  /* api cbs */
  ot->ex = bone_collection_sel_ex;
  ot->poll = armature_bone_sel_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int bone_collection_desel_ex(Cxt *C, WinOp * /*op*/)
{
  Ob *ob = ed_ob_xt(C);
  if (ob == nullptr) {
    return OP_CANCELLED;
  }

  Armature *armature = reinterpret_cast<Armature *>(ob->data);
  BoneCollection *bcoll = armature->runtime.active_collection;
  if (bcoll == nullptr) {
    return OP_CANCELLED;
  }

  bone_collection_sel(C, ob, bcoll, false);
  return OP_FINISHED;
}

void ARMATURE_OT_collection_desel(WinOpType *ot)
{
  /* ids */
  ot->name = "Desel Bone Collection";
  ot->idname = "ARMATURE_OT_collection_desel";
  ot->description = "Desel bones of active Bone Collection";

  /* api cbs */
  ot->ex = bone_collection_desel_ex;
  ot->poll = armature_bone_sel_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static BoneCollection *add_or_move_to_collection_bcoll(WinOp *op, Armature *arm)
{
  const int collection_index = api_enum_get(op->ptr, "collection");
  BoneCollection *target_bcoll;

  if (collection_index < 0) {
    /* TODO: check this with linked, non-overridden armatures. */
    char new_collection_name[MAX_NAME];
    api_string_get(op->ptr, "new_collection_name", new_collection_name);
    target_bcoll = anim_armature_bonecoll_new(arm, new_collection_name);
    lib_assert_msg(target_bcoll,
                   "It should always be possible to create a new bone collection on an armature");
    anim_armature_bonecoll_active_set(arm, target_bcoll);
  }
  else {
    if (collection_index >= arm->collection_array_num) {
      dune_reportf(op->reports,
                  RPT_ERROR,
                  "Bone collection with index %d not found on Armature %s",
                  collection_index,
                  arm->id.name + 2);
      return nullptr;
    }
    target_bcoll = arm->collection_array[collection_index];
  }

  if (!anim_armature_bonecoll_is_editable(arm, target_bcoll)) {
    dune_reportf(op->reports,
                RPT_ERROR,
                "Bone collection %s is not editable, maybe add an override on the armature?",
                target_bcoll->name);
    return nullptr;
  }

  return target_bcoll;
}

static int add_or_move_to_collection_ex(Cxt *C,
                                        WinOp *op,
                                        const assign_bone_fn assign_fn_bone,
                                        const assign_ebone_fn assign_fn_ebone)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob->mode == OB_MODE_POSE) {
    ob = ed_pose_ob_from_cxt(C);
  }
  if (!ob) {
    dune_reportf(op->reports, RPT_ERROR, "No ob found to op on");
    return OP_CANCELLED;
  }

  Armature *arm = static_cast<Armature *>(ob->data);
  BoneCollection *target_bcoll = add_or_move_to_collection_bcoll(op, arm);

  bool made_any_changes = false;
  bool had_bones_to_assign = false;
  const bool mode_is_supported = bone_collection_assign_mode_specific(C,
                                                                      ob,
                                                                      target_bcoll,
                                                                      assign_fn_bone,
                                                                      assign_fn_ebone,
                                                                      &made_any_changes,
                                                                      &had_bones_to_assign);

  if (!mode_is_supported) {
    win_report(RPT_ERROR, "This op only works in pose mode and armature edit mode");
    return OP_CANCELLED;
  }
  if (!had_bones_to_assign) {
    win_report(RPT_WARNING, "No bones sel, nothing to assign to bone collection");
    return OP_CANCELLED;
  }
  if (!made_any_changes) {
    win_report(RPT_WARNING, "All sel bones were alrdy part of this collection");
    return OP_CANCELLED;
  }

  graph_id_tag_update(&arm->id, ID_RECALC_SEL); /* Recreate the drw bufs. */

  win_ev_add_notifier(C, NC_OB | ND_DATA, ob);
  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
  return OP_FINISHED;
}

static int move_to_collection_ex(Cxt *C, WinOp *op)
{
  return add_or_move_to_collection_ex(C,
                                      op,
                                      anim_armature_bonecoll_assign_and_move,
                                      anim_armature_bonecoll_assign_and_move_editbone);
}

static int assign_to_collection_ex(Cxt *C, WinOp *op)
{
  return add_or_move_to_collection_ex(
      C, op, anim_armature_bonecoll_assign, anim_armature_bonecoll_assign_editbone);
}

static bool move_to_collection_poll(Cxt *C)
{
  Ob *ob = ed_ob_cxt(C);
  if (ob == nullptr) {
    return false;
  }

  if (ob->type != OB_ARMATURE) {
    cxt_win_op_poll_msg_set(C, "Bone collections can only be edited on an Armature");
    return false;
  }

  if (ID_IS_LINKED(ob->data) && !ID_IS_OVERRIDE_LIB(ob->data)) {
    cxt_win_op_poll_msg_set(C, "This needs a local Armature or an override");
    return false;
  }

  /* Ideally this would also check the target bone collection to move/assign to.
   * However, that requires access to the op props, and those are not
   * available in the poll fn. */
  return true;
}

static bool bone_collection_enum_itemf_for_ob(Ob *ob,
                                              EnumPropItem **item,
                                              int *totitem)
{
  EnumPropItem item_tmp = {0};
  Armature *arm = static_cast<Armature *>(ob->data);

  for (int bcoll_index = 0; bcoll_index < arm->collection_array_num; bcoll_index++) {
    BoneCollection *bcoll = arm->collection_array[bcoll_index];
    if (!anim_armature_bonecoll_is_editable(arm, bcoll)) {
      /* Skip bone collections that cannot be assigned to because they're
       * linked and thus uneditable. If there is a way to still show these, but in a disabled
       * state, that would be preferred. */
      continue;
    }
    item_tmp.id = bcoll->name;
    item_tmp.name = bcoll->name;
    item_tmp.val= bcoll_index;
    api_enum_item_add(item, totitem, &item_tmp);
  }

  return true;
}

static const EnumPropItem *bone_collection_enum_itemf(Cxt *C,
                                                      ApiPtr * /*ptr*/,
                                                      ApiProp * /*prop*/,
                                                      bool *r_free)
{
  *r_free = false;

  if (!C) {
    /* This happens when ops are being tested, and not during normal invocation. */
    return api_enum_dummy_NULL_items;
  }

  Ob *ob = ed_ob_cxt(C);
  if (!ob || ob->type != OB_ARMATURE) {
    return api_enum_dummy_NULL_items;
  }

  EnumPropItem *item = nullptr;
  int totitem = 0;
  switch (ob->mode) {
    case OB_MODE_POSE: {
      Ob *obpose = ed_pose_ob_from_cxt(C);
      if (!obpose) {
        return nullptr;
      }
      bone_collection_enum_itemf_for_ob(obpose, &item, &totitem);
      break;
    }
    case OB_MODE_EDIT:
      bone_collection_enum_itemf_for_ob(ob, &item, &totitem);
      break;
    default:
      return api_enum_dummy_NULL_items;
  }

  /* New Collection. */
  EnumPropItem item_tmp = {0};
  item_tmp.id = "__NEW__";
  item_tmp.name = CXT_IFACE_(LANG_CXT_OP_DEFAULT, "New Collection");
  item_tmp.val = -1;
  api_enum_item_add(&item, &totitem, &item_tmp);

  api_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static int move_to_collection_invoke(Cxt *C, WinOp *op, const WinEv * /*ev*/)
{
  ApiProp *prop = api_struct_find_prop(op->ptr, "collection");
  if (api_prop_is_set(op->ptr, prop)) {
    const int collection_index = api_prop_enum_get(op->ptr, prop);
    if (collection_index < 0) {
      return win_op_props_dialog_popup(C, op, 200);
    }
    /* Either call move_to_collection_ex() or assign_to_collection_ex(), depending on which
     * op got invoked. */
    return op->type->ex(C, op);
  }

  const char *title = CXT_IFACE_(op->type->translation_cxt, op->type->name);
  uiPopupMenu *pup = ui_popup_menu_begin(C, title, ICON_NONE);
  uiLayout *layout = ui_popup_menu_layout(pup);
  uiLayoutSetOperatorContext(layout, WIN_OP_INVOKE_DEFAULT);
  uiItemsEnumO(layout, op->idname, "collection");
  ii_popup_menu_end(C, pup);
  return OP_INTERFACE;
}

void ARMATURE_OT_move_to_collection(WinOprType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Move to Collection";
  ot->description = "Move bones to a collection";
  ot->idname = "ARMATURE_OT_move_to_collection";

  /* api cbs */
  ot->ex = move_to_collection_ex;
  ot->invoke = move_to_collection_invoke;
  ot->poll = move_to_collection_poll;

  /* Flags don't include OPTYPE_REGISTER, as the redo pnl doesn't make much sense for this
   * op. The visibility of the api props is determined by the needs of the 'New Catalog'
   * popup, so that a name can be entered. This means that the redo pnl would also only show the
   * 'Name' prop, wo any choice for another collection. */
  ot->flag = OPTYPE_UNDO;

  prop = api_def_enum(ot->sapi,
                      "collection",
                      api_enum_dummy_DEFAULT_items,
                      0,
                      "Collection",
                      "The bone collection to move the selected bones to");
  aoi_def_enum_fns(prop, bone_collection_enum_itemf);
  /* Translation of items is handled by bone_collection_enum_itemf if needed, most are actually
   * data (bone collections) names and therefore should not be translated at all. So disable
   * automatic translation. */
  api_def_prop_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN | PROP_ENUM_NO_TRANSLATE);

  prop = api_def_string(ot->sapi,
                        "new_collection_name",
                        nullptr,
                        MAX_NAME,
                        "Name",
                        "Name of the newly added bone collection");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  ot->prop = prop;
}

void ARMATURE_OT_assign_to_collection(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Assign to Collection";
  ot->description = "Assign bones to a collection";
  ot->idname = "ARMATURE_OT_assign_to_collection";

  /* api cbs */
  ot->ex = assign_to_collection_ex;
  ot->invoke = move_to_collection_invoke;
  ot->poll = move_to_collection_poll;

  /* Flags don't include OPTYPE_REGISTER, the redo pnl isnt sensible for this
   * op. Api props visibility determined by needs of the 'New Catalog'
   * popup, so that a name can be entered. Means that the redo pnl would also only show the
   * 'Name' prop, wo any choice for another collection. */
  ot->flag = OPTYPE_UNDO;

  prop = api_def_enum(ot->sapi,
                      "collection",
                      api_enum_dummy_DEFAULT_items,
                      0,
                      "Collection",
                      "The bone collection to move the sel bones to");
  api_def_enum_fns(prop, bone_collection_enum_itemf);
  api_def_prop_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);

  prop = api_def_string(ot->sapi,
                        "new_collection_name",
                        nullptr,
                        MAX_NAME,
                        "Name",
                        "Name of the newly added bone collection");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  ot->prop = prop;
}
