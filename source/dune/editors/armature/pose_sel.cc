#include <cstring>

#include "types_anim.h"
#include "types_armature.h"
#include "types_constraint.h"
#include "types_pen_mod.h"
#include "types_ob.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"

#include "dune_action.h"
#include "dune_armature.hh"
#include "dune_constraint.h"
#include "dune_cxt.hh"
#include "dune_pen_mod_legacy.h"
#include "dune_layer.h"
#include "dune_mod.hh"
#include "dune_ob.hh"
#include "dune_report.h"

#include "graph.hh"

#include "api_access.hh"
#include "api_define.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_keyframing.hh"
#include "ed_mesh.hh"
#include "ed_ob.hh"
#include "ed_outliner.hh"
#include "ed_screen.hh"
#include "ed_sel_utils.hh"
#include "ed_view3d.hh"

#include "anim_bone_collections.hh"
#include "anim_bonecolor.hh"

#include "armature_intern.h"

/* util macros for storing a tmp int in the bone (sel flag) */
#define PBONE_PREV_FLAG_GET(pchan) ((void)0, PTR_AS_INT((pchan)->tmp))
#define PBONE_PREV_FLAG_SET(pchan, val) ((pchan)->tmp = PTR_FROM_INT(val))

/* Pose Sel Utils */
/* NOTE: SEL_TOGGLE is assumed to have alrdy been handled! */
static void pose_do_bone_sel(PoseChannel *pchan, const int sel_mode)
{
  /* sel pchan only if cansel, but desel works always */
  switch (sel_mode) {
    case SEL_SEL:
      if (!(pchan->bone->flag & BONE_UNSELECTABLE)) {
        pchan->bone->flag |= BONE_SEL;
      }
      break;
    case SEL_DESEL:
      pchan->bone->flag &= ~(BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
      break;
    case SEL_INVERT:
      if (pchan->bone->flag & BONE_SEL) {
        pchan->bone->flag &= ~(BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
      }
      else if (!(pchan->bone->flag & BONE_UNSELECTABLE)) {
        pchan->bone->flag |= BONE_SEL;
      }
      break;
  }
}

void ed_pose_bone_sel_tag_update(Ob *ob)
{
  lib_assert(ob->type == OB_ARMATURE);
  Armature *arm = static_cast<Armature *>(ob->data);
  win_main_add_notifier(NC_OB | ND_BONE_SEL, ob);
  win_main_add_notifier(NC_GEOM | ND_DATA, ob);

  if (arm->flag & ARM_HAS_VIZ_DEPS) {
    /* mask mod ('armature' mode), etc. */
    graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
  }

  graph_id_tag_update(&arm->id, ID_RECALC_SEL);
}

void ed_pose_bone_sel(Ob *ob, PoseChannel *pchan, bool sel, bool change_active)
{
  Armature *arm;

  /* sanity checks */
  /* actually, we can probably still get away with no ob - at most we have no updates */
  if (ELEM(nullptr, ob, ob->pose, pchan, pchan->bone)) {
    return;
  }

  arm = static_cast<Armature *>(ob->data);

  /* can only change sel state if bone can be modified */
  if (PBONE_SELECTABLE(arm, pchan->bone)) {
    /* change sel state - activate too if sel */
    if (sel) {
      pchan->bone->flag |= BONE_SEL;
      if (change_active) {
        arm->act_bone = pchan->bone;
      }
    }
    else {
      pchan->bone->flag &= ~BONE_SEL;
      if (change_active) {
        arm->act_bone = nullptr;
      }
    }

    /* TODO: sel and activate corresponding vgroup? */
    ed_pose_bone_sel_tag_update(ob);
  }
}

bool ed_armature_pose_sel_pick_bone(const Scene *scene,
                                    ViewLayer *view_layer,
                                    View3D *v3d,
                                    Ob *ob,
                                    Bone *bone,
                                    const SelPickParams *params)
{
  bool found = false;
  bool changed = false;

  if (ob || ob->pose) {
    if (bone && ((bone->flag & BONE_UNSELECTABLE) == 0)) {
      found = true;
    }
  }

  if (params->sel_op == SEL_OP_SET) {
    if ((found && params->sel_passthrough) && (bone->flag & BONE_SEL)) {
      found = false;
    }
    else if (found || params->deselect_all) {
      /* Desel everything. */
      /* Don't use 'dune_ob_pose_base_array_get_unique'
       * bc we may be sel from object mode. */
      FOREACH_VISIBLE_BASE_BEGIN (scene, view_layer, v3d, base_iter) {
        Ob *ob_iter = base_iter->ob;
        if ((ob_iter->type == OB_ARMATURE) && (ob_iter->mode & OB_MODE_POSE)) {
          if (ed_pose_desel_all(ob_iter, SEL_DESEL, true)) {
            ed_pose_bone_sel_tag_update(ob_iter);
          }
        }
      }
      FOREACH_VISIBLE_BASE_END;
      changed = true;
    }
  }

  if (found) {
    dune_view_layer_synced_ensure(scene, view_layer);
    Ob *ob_act = dune_view_layer_active_ob_get(view_layer);
    lib_assert(dune_view_layer_edit_ob_get(view_layer) == nullptr);

    /* If the bone cannot be affected, don't do anything. */
    Armature *arm = static_cast<Armature *>(ob->data);

    /* Since we do unified sel, we don't shift+sel a bone if the
     * armature ob was not active yet.
     * NOTE: Special exception for armature mode so we can do multi-sel
     * we could check for multi-sel explicitly but think its fine to
     * always give predictable behavior in weight paint mode. */
    if ((ob_act == nullptr) || ((ob_act != ob) && (ob_act->mode & OB_MODE_ALL_WEIGHT_PAINT) == 0))
    {
      /* When we are entering into posemode via toggle-sel,
       * from another active ob - always sel the bone. */
      if (params->sel_op == SEL_OP_SET) {
        /* Re-sel the bone again later in this fn. */
        bone->flag &= ~BONE_SEL;
      }
    }

    switch (params->sel_op) {
      case SEL_OP_ADD: {
        bone->flag |= (BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
        arm->act_bone = bone;
        break;
      }
      case SEL_OP_SUB: {
        bone->flag &= ~(BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
        break;
      }
      case SEL_OP_XOR: {
        if (bone->flag & BONE_SEL) {
          /* If not active, we make it active. */
          if (bone != arm->act_bone) {
            arm->act_bone = bone;
          }
          else {
            bone->flag &= ~(BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
          }
        }
        else {
          bone->flag |= (BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
          arm->act_bone = bone;
        }
        break;
      }
      case SEL_OP_SET: {
        bone->flag |= (BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL);
        arm->act_bone = bone;
        break;
      }
      case SEL_OP_AND: {
        lib_assert_unreachable(); /* Doesn't make sense for picking. */
        break;
      }
    }

    if (ob_act) {
      /* In weight-paint we sel the associated vertex group too. */
      if (ob_act->mode & OB_MODE_ALL_WEIGHT_PAINT) {
        if (bone == arm->act_bone) {
          ed_vgroup_sel_by_name(ob_act, bone->name);
          graph_id_tag_update(&ob_act->id, ID_RECALC_GEOMETRY);
        }
      }
      /* If there are some dependencies for visualizing armature state
       * (e.g. Mask Mod in 'Armature' mode), force */
      else if (arm->flag & ARM_HAS_VIZ_DEPS) {
        /* Ob not ob_act here is intentional: it's the src of the
         * bones being sel [#37247]. */
        graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
      }

      /* Tag armature for copy-on-write update (since act_bone is in armature not object). */
      graph_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);
    }

    changed = true;
  }

  return changed || found;
}

bool ed_armature_pose_sel_pick_w_buf(const Scene *scene,
                                           ViewLayer *view_layer,
                                           View3D *v3d,
                                           Base *base,
                                           const GPUSelResult *hit_results,
                                           const int hits,
                                           const SelPickParams *params,
                                           bool do_nearest)
{
  Object *ob = base->ob;
  Bone *nearBone;

  if (!ob || !ob->pose) {
    return false;
  }

  /* Callers happen to already get the active base */
  Base *base_dummy = nullptr;
  nearBone = ed_armature_pick_bone_from_selbuf(
      &base, 1, hit_results, hits, true, do_nearest, &base_dummy);

  return ed_armature_pose_sel_pick_bone(scene, view_layer, v3d, ob, nearBone, params);
}

void ed_armature_pose_sel_in_wpaint_mode(const Scene *scene,
                                         ViewLayer *view_layer,
                                         Base *base_sel)
{
  lib_assert(base_sel && (base_sel->ob->type == OB_ARMATURE));
  dune_view_layer_synced_ensure(scene, view_layer);
  Ob *ob_active = dune_view_layer_active_ob_get(view_layer);
  lib_assert(ob_active && (ob_active->mode & OB_MODE_ALL_WEIGHT_PAINT));

  if (ob_active->type == OB_PEN_LEGACY) {
    PenVirtualModData virtual_mod_data;
    PenModData *md = dune_pen_mods_get_virtual_modlist(
        ob_active, &virtual_mod_data);
    for (; md; md = md->next) {
      if (md->type == ePenModTypeArmature) {
        ArmaturePenModData *agmd = (ArmaturePenModData *)md;
        Ob *ob_arm = agmd->ob;
        if (ob_arm != nullptr) {
          Base *base_arm = dune_view_layer_base_find(view_layer, ob_arm);
          if ((base_arm != nullptr) && (base_arm != base_sel) &&
              (base_arm->flag & BASE_SEL)) {
            ed_ob_base_sel(base_arm, BA_DESEL);
          }
        }
      }
    }
  }
  else {
    VirtualModData virtual_mod_data;
    ModData *md = dune_mods_get_virtual_modlist(ob_active, &virtual_mod_data);
    for (; md; md = md->next) {
      if (md->type == eModTypeArmature) {
        ArmatureModData *amd = (ArmatureModData *)md;
        Ob *ob_arm = amd->ob;
        if (ob_arm != nullptr) {
          Base *base_arm = dune_view_layer_base_find(view_layer, ob_arm);
          if ((base_arm != nullptr) && (base_arm != base_sel) &&
              (base_arm->flag & BASE_SEL)) {
            ed_ob_base_sel(base_arm, BA_DESEL);
          }
        }
      }
    }
  }
  if ((base_sel->flag & BASE_SEL) == 0) {
    ed_ob_base_sel(base_sel, BA_SEL);
  }
}

bool ed_pose_desel_all(Ob *ob, int sel_mode, const bool ignore_visibility)
{
  Armature *arm = static_cast<Armature *>(ob->data);

  /* we call this from outliner too */
  if (ob->pose == nullptr) {
    return false;
  }

  /* Determine if mode sel or desel */
  if (sel_mode == SEL_TOGGLE) {
    sel_mode = SEL_SEL;
    LIST_FOREACH (PoseChannel *, pchan, &ob->pose->chanbase) {
      if (ignore_visibility || PBONE_VISIBLE(arm, pchan->bone)) {
        if (pchan->bone->flag & BONE_SEL) {
          sel_mode = SEL_DESEL;
          break;
        }
      }
    }
  }

  /* Set the flags accordingly */
  bool changed = false;
  LIST_FOREACH (PoseChannel *, pchan, &ob->pose->chanbase) {
    /* ignore the pchan if it isn't visible or if its sel cannot be changed */
    if (ignore_visibility || PBONE_VISIBLE(arm, pchan->bone)) {
      int flag_prev = pchan->bone->flag;
      pose_do_bone_sel(pchan, select_mode);
      changed = (changed || flag_prev != pchan->bone->flag);
    }
  }
  return changed;
}

static bool ed_pose_is_any_selected(Ob *ob, bool ignore_visibility)
{
  Armature *arm = static_cast<Armature *>(ob->data);
  LIST_FOREACH (PoseChannel *, pchan, &ob->pose->chanbase) {
    if (ignore_visibility || PBONE_VISIBLE(arm, pchan->bone)) {
      if (pchan->bone->flag & BONE_SEL) {
        return true;
      }
    }
  }
  return false;
}

static bool ed_pose_is_any_sel_multi(Base **bases, uint bases_len, bool ignore_visibility)
{
  for (uint base_index = 0; base_index < bases_len; base_index++) {
    Ob *ob_iter = bases[base_index]->ob;
    if (ed_pose_is_any_sel(ob_iter, ignore_visibility)) {
      return true;
    }
  }
  return false;
}

bool ed_pose_desel_all_multi_ex(Base **bases,
                                uint bases_len,
                                int sel_mode,
                                const bool ignore_visibility)
{
  if (sel_mode == SEL_TOGGLE) {
    sel_mode = ed_pose_is_any_sel_multi(bases, bases_len, ignore_visibility) ?
                      SEL_DESEL :
                      SEL_SEL;
  }

  bool changed_multi = false;
  for (uint base_index = 0; base_index < bases_len; base_index++) {
    Ob *ob_iter = bases[base_index]->ob;
    if (ed_pose_desel_all(ob_iter, sel_mode, ignore_visibility)) {
      ed_pose_bone_sel_tag_update(ob_iter);
      changed_multi = true;
    }
  }
  return changed_multi;
}

bool ed_pose_desel_all_multi(Cxt *C, int sel_mode, const bool ignore_visibility)
{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  ViewCxt vc = ed_view3d_viewcxt_init(C, graph);
  uint bases_len = 0;

  Base **bases = dune_ob_pose_base_array_get_unique(
      vc.scene, vc.view_layer, vc.v3d, &bases_len);
  bool changed_multi = ed_pose_desel_all_multi_ex(
      bases, bases_len, sel_mode, ignore_visibility);
  mem_free(bases);
  return changed_multi;
}

/* Selections */
static void selconnected_posebonechildren(Ob *ob, Bone *bone, int extend)
{
  /* stop when unconnected child is encountered, or when unselectable bone is encountered */
  if (!(bone->flag & BONE_CONNECTED) || (bone->flag & BONE_UNSELECTABLE)) {
    return;
  }

  if (extend) {
    bone->flag &= ~BONE_SEL;
  }
  else {
    bone->flag |= BONE_SEL;
  }

  LIST_FOREACH (Bone *, curBone, &bone->childbase) {
    selconnected_posebonechildren(ob, curBone, extend);
  }
}

/* within active ob cxt */
/* previously known as "selconnected_posearmature" */
static int pose_sel_connected_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  Bone *bone, *curBone, *next = nullptr;
  const bool extend = api_bool_get(op->ptr, "extend");

  view3d_op_needs_opengl(C);

  Base *base = nullptr;
  bone = ed_armature_pick_bone(C, ev->mval, !extend, &base);

  if (!bone) {
    return OP_CANCELLED;
  }

  /* Sel parents */
  for (curBone = bone; curBone; curBone = next) {
    /* ignore bone if cannot be sel */
    if ((curBone->flag & BONE_UNSELECTABLE) == 0) {
      if (extend) {
        curBone->flag &= ~BONE_SEL;
      }
      else {
        curBone->flag |= BONE_SEL;
      }

      if (curBone->flag & BONE_CONNECTED) {
        next = curBone->parent;
      }
      else {
        next = nullptr;
      }
    }
    else {
      next = nullptr;
    }
  }

  /* Sel children */
  LIST_FOREACH (Bone *, curBone, &bone->childbase) {
    selconnected_posebonechildren(base->ob, curBone, extend);
  }

  ed_outliner_sel_sync_from_pose_bone_tag(C);

  ed_pose_bone_sel_tag_update(base->ob);

  return OP_FINISHED;
}

static bool pose_sel_linked_pick_poll(Cxt *C)
{
  return (ed_op_view3d_active(C) && ed_op_posemode(C));
}

void POSE_OT_sel_linked_pick(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Sel Connected";
  ot->idname = "POSE_OT_sel_linked_pick";
  ot->description = "Sel bones linked by parent/child connections under the mouse cursor";

  /* cbs */
  /* leave 'ex' unset */
  ot->invoke = pose_sel_connected_invoke;
  ot->poll = pose_sel_linked_pick_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  prop = api_def_bool(ot->sapi,
                         "extend",
                         false,
                         "Extend",
                         "Extend sel instead of desel everything first");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

static int pose_sel_linked_ex(Cxt *C, WinOp * /*op*/)
{
  Bone *curBone, *next = nullptr;

  CXT_DATA_BEGIN_W_ID (C, PoseChannel *, pchan, visible_pose_bones, Ob *, ob) {
    if ((pchan->bone->flag & BONE_SEL) == 0) {
      continue;
    }

    Armature *arm = static_cast<Armature *>(ob->data);

    /* Select parents */
    for (curBone = pchan->bone; curBone; curBone = next) {
      if (PBONE_SELECTABLE(arm, curBone)) {
        curBone->flag |= BONE_SEL;

        if (curBone->flag & BONE_CONNECTED) {
          next = curBone->parent;
        }
        else {
          next = nullptr;
        }
      }
      else {
        next = nullptr;
      }
    }

    /* Sel children */
    LIST_FOREACH (Bone *, curBone, &pchan->bone->childbase) {
      selconnected_posebonechildren(ob, curBone, false);
    }
    ed_pose_bone_sel_tag_update(ob);
  }
  CXT_DATA_END;

  ed_outliner_sel_sync_from_pose_bone_tag(C);

  return OP_FINISHED;
}

void POSE_OT_sel_linked(WinOpType *ot)
{
  /* ids */
  ot->name = "Select Connected";
  ot->idname = "POSE_OT_sel_linked";
  ot->description = "Sel all bones linked by parent/child connections to the current sel";

  /* cbs */
  ot->ex = pose_sel_linked_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int pose_de_sel_all_ex(Cxt *C, WinOp *op)
{
  int action = api_enum_get(op->ptr, "action");

  Scene *scene = cxt_data_scene(C);
  int multipaint = scene->toolsettings->multipaint;

  if (action == SEL_TOGGLE) {
    action = CXT_DATA_COUNT(C, sel_pose_bones) ? SEL_DESEL : SEL_SEL;
  }

  Ob *ob_prev = nullptr;

  /* Set the flags. */
  CXT_DATA_BEGIN_W_ID (C, PoseChannel *, pchan, visible_pose_bones, Ob *, ob) {
    Armature *arm = static_cast<Armature *>(ob->data);
    pose_do_bone_sel(pchan, action);

    if (ob_prev != ob) {
      /* Weight-paint or mask mods need graph updates. */
      if (multipaint || (arm->flag & ARM_HAS_VIZ_DEPS)) {
        graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
      }
      /* need to tag armature for cow updates, or else sel doesn't update */
      graph_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);
      ob_prev = ob;
    }
  }
  CXT_DATA_END;

  ed_outliner_sel_sync_from_pose_bone_tag(C);

  win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, nullptr);

  return OP_FINISHED;
}

void POSE_OT_sel_all(WinOpType *ot)
{
  /* ids */
  ot->name = "(De)sel All";
  ot->idname = "POSE_OT_sel_all";
  ot->description = "Toggle sel status of all bones";

  /* api callbacks */
  ot->exec = pose_de_select_all_exec;
  ot->poll = ED_operator_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  win_op_props_sel_all(ot);
}

static int pose_sel_parent_ex(Cxt *C, WinOp * /*op*/)
{
  Object *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));
  Armature *arm = (Armature *)ob->data;
  PoseChannel *pchan, *parent;

  /* Determine if there is an active bone */
  pchan = cxt_data_active_pose_bone(C);
  if (pchan) {
    parent = pchan->parent;
    if ((parent) && !(parent->bone->flag & (BONE_HIDDEN_P | BONE_UNSELECTABLE))) {
      parent->bone->flag |= BONE_SEL;
      arm->act_bone = parent->bone;
    }
    else {
      return OP_CANCELLED;
    }
  }
  else {
    return OP_CANCELLED;
  }

  ed_outliner_sel_sync_from_pose_bone_tag(C);

  ed_pose_bone_sel_tag_update(ob);
  return OP_FINISHED;
}

void POSE_OT_sel_parent(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel Parent Bone";
  ot->idname = "POSE_OT_sel_parent";
  ot->description = "Sel bones that are parents of the currently sel bones";

  /* api cbs */
  ot->ex = pose_sel_parent_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int pose_sel_constraint_target_ex(Cxt *C, WinOp * /*op*/)
{
  int found = 0;

  CXT_DATA_BEGIN (C, PoseChannel *, pchan, visible_pose_bones) {
    if (pchan->bone->flag & BONE_SEL) {
      LIST_FOREACH (Constraint *, con, &pchan->constraints) {
        List targets = {nullptr, nullptr};
        if (dune_constraint_targets_get(con, &targets)) {
          LIST_FOREACH (ConstraintTarget *, ct, &targets) {
            Ob *ob = ct->tar;

            /* Any armature that is also in pose mode should be sel. */
            if ((ct->subtarget[0] != '\0') && (ob != nullptr) && (ob->type == OB_ARMATURE) &&
                (ob->mode == OB_MODE_POSE))
            {
              PoseChannel *pchanc = dune_pose_channel_find_name(ob->pose, ct->subtarget);
              if ((pchanc) && !(pchanc->bone->flag & BONE_UNSELECTABLE)) {
                pchanc->bone->flag |= BONE_SEL | BONE_TIPSEL | BONE_ROOTSEL;
                ed_pose_bone_sel_tag_update(ob);
                found = 1;
              }
            }
          }

          dune_constraint_targets_flush(con, &targets, true);
        }
      }
    }
  }
  CXT_DATA_END;

  if (!found) {
    return OP_CANCELLED;
  }

  ed_outliner_sel_sync_from_pose_bone_tag(C);

  return OP_FINISHED;
}

void POSE_OT_sel_constraint_target(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel Constraint Target";
  ot->idname = "POSE_OT_sel_constraint_target";
  ot->description = "Sel bones used as targets for the currently selected bones";

  /* api cbs */
  ot->ex = pose_sel_constraint_target_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* No need to convert to multi-obs. Just like we keep the non-active bones
 * sele we then keep the non-active obs untouched (sel/unsel). */
static int pose_sel_hierarchy_ex(Cxt *C, WinOp *op)
{
  Ob *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));
  Armature *arm = static_cast<Armature *>(ob->data);
  PoseChannel *pchan_act;
  int direction = api_enum_get(op->ptr, "direction");
  const bool add_to_sel = api_bool_get(op->ptr, "extend");
  bool changed = false;

  pchan_act = dune_pose_channel_active_if_bonecoll_visible(ob);
  if (pchan_act == nullptr) {
    return OP_CANCELLED;
  }

  if (direction == BONE_SEL_PARENT) {
    if (pchan_act->parent) {
      Bone *bone_parent;
      bone_parent = pchan_act->parent->bone;

      if (PBONE_SELECTABLE(arm, bone_parent)) {
        if (!add_to_sel) {
          pchan_act->bone->flag &= ~BONE_SEL;
        }
        bone_parent->flag |= BONE_SEL;
        arm->act_bone = bone_parent;

        changed = true;
      }
    }
  }
  else { /* direction == BONE_SELECT_CHILD */
    Bone *bone_child = nullptr;
    int pass;

    /* first pass, only connected bones (the logical direct child) */
    for (pass = 0; pass < 2 && (bone_child == nullptr); pass++) {
      LIST_FOREACH (PoseChannel *, pchan_iter, &ob->pose->chanbase) {
        /* possible we have multiple children, some invisible */
        if (PBONE_SELECTABLE(arm, pchan_iter->bone)) {
          if (pchan_iter->parent == pchan_act) {
            if ((pass == 1) || (pchan_iter->bone->flag & BONE_CONNECTED)) {
              bone_child = pchan_iter->bone;
              break;
            }
          }
        }
      }
    }

    if (bone_child) {
      arm->act_bone = bone_child;

      if (!add_to_sel) {
        pchan_act->bone->flag &= ~BONE_SEL;
      }
      bone_child->flag |= BONE_SEL;

      changed = true;
    }
  }

  if (changed == false) {
    return OP_CANCELLED;
  }

  ed_outliner_sel_sync_from_pose_bone_tag(C);

  ed_pose_bone_sel_tag_update(ob);

  return OP_FINISHED;
}

void POSE_OT_sel_hierarchy(WinOpType *ot)
{
  static const EnumPropItem direction_items[] = {
      {BONE_SEL_PARENT, "PARENT", 0, "Sel Parent", ""},
      {BONE_SEL_CHILD, "CHILD", 0, "Sel Child", ""},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* ids */
  ot->name = "Sel Hierarchy";
  ot->idname = "POSE_OT_sel_hierarchy";
  ot->description = "Sel immediate parent/children of sel bones";

  /* api cbs */
  ot->ex = pose_sel_hierarchy_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(
      ot->sapi, "direction", direction_items, BONE_SEL_PARENT, "Direction", "");
  api_def_bool(ot->sapi, "extend", false, "Extend", "Extend the selection");
}

/* modes for sel same */
enum ePoseSelSameMode {
  POSE_SEL_SAME_COLLECTION = 0,
  POSE_SEL_SAME_COLOR = 1,
  POSE_SEL_SAME_KEYINGSET = 2,
};

static bool pose_sel_same_color(Cxt *C, const bool extend)
{
  /* Get a set of all the colors of the sel bones. */
  dune::Set<dune::animrig::BoneColor> used_colors;
  dune::Set<Ob *> updated_obs;
  bool changed_any_sel = false;

  /* Old approach that we may want to reinstate behind some option at some point. This will match
   * against the colors of all sel bones, instead of just the active one. It also explains why
   * there is a set of colors to begin with.
   *
   * CXT_DATA_BEGIN (C, PoseChannel *, pchan, sel_pose_bones) {
   *   auto color = dune::animrig::ANIM_bonecolor_posebone_get(pchan);
   *   used_colors.add(color);
   * }
   * CXT_DATA_END; */
  if (!extend) {
    CXT_DATA_BEGIN_W_ID (C, PoseChannel *, pchan, sel_pose_bones, Ob *, ob) {
      pchan->bone->flag &= ~BONE_SEL;
      updated_obs.add(ob);
      changed_any_sel = true;
    }
    CXT_DATA_END;
  }

  /* Use the color of the active pose bone. */
  PoseChannel *active_pose_bone = cxt_data_active_pose_bone(C);
  auto color = dune::animrig::anim_bonecolor_posebone_get(active_pose_bone);
  used_colors.add(color);

  /* Sel all visible bones that have the same color. */
  CXT_DATA_BEGIN_W_ID (C, PoseChannel *, pchan, visible_pose_bones, Ob *, ob) {
    Bone *bone = pchan->bone;
    if (bone->flag & (BONE_UNSELECTABLE | BONE_SEL)) {
      /* Skip bones that are unselectable or alrdy sel. */
      continue;
    }

    auto color = dune::animrig::anim_bonecolor_posebone_get(pchan);
    if (!used_colors.contains(color)) {
      continue;
    }

    bone->flag |= BONE_SEL;
    changed_any_sel = true;
    updated_obs.add(ob);
  }
  CXT_DATA_END;

  if (!changed_any_sel) {
    return false;
  }

  for (Ob *ob : updated_obs) {
    ed_pose_bone_sel_tag_update(ob);
  }
  return true;
}

static bool pose_sel_same_collection(Cxt *C, const bool extend)
{
  bool changed_any_sel = false;
  dune::Set<Ob *> updated_obs;

  /* Refuse to do anything if there is no active pose bone. */
  PoseChannel *active_pchan = cxt_data_active_pose_bone(C);
  if (!active_pchan) {
    return false;
  }

  if (!extend) {
    /* Desel all the bones. */
    CXT_DATA_BEGIN_W_ID (C, PoseChannel *, pchan, sel_pose_bones, Ob *, ob) {
      pchan->bone->flag &= ~BONE_SEL;
      updated_obs.add(ob);
      changed_any_sel = true;
    }
    CXT_DATA_END;
  }

  /* Build a set of bone collection names, to allow cross-Armature sel. */
  dune::Set<std::string> collection_names;
  LIST_FOREACH (BoneCollectionRef *, bcoll_ref, &active_pchan->bone->runtime.collections)
  {
    collection_names.add(bcoll_ref->bcoll->name);
  }

  /* Select all bones that match any of the collection names. */
  CXT_DATA_BEGIN_W_ID (C, PoseChannel *, pchan, visible_pose_bones, Ob *, ob) {
    Bone *bone = pchan->bone;
    if (bone->flag & (BONE_UNSELECTABLE | BONE_SEL)) {
      continue;
    }

    LIST_FOREACH (BoneCollectionRef *, bcoll_ref, &bone->runtime.collections) {
      if (!collection_names.contains(bcoll_ref->bcoll->name)) {
        continue;
      }

      bone->flag |= BONE_SEL;
      changed_any_sel = true;
      updated_obs.add(ob);
    }
  }
  CXT_DATA_END;

  for (Ob *ob : updated_obs) {
    ed_pose_bone_sel_tag_update(ob);
  }

  return changed_any_sel;
}

static bool pose_sel_same_keyingset(Cxt *C, ReportList *reports, bool extend)
{
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  bool changed_multi = false;
  KeyingSet *ks = anim_scene_get_active_keyingset(cxt_data_scene(C));

  /* sanity checks: validate Keying Set and ob */
  if (ks == nullptr) {
    dune_report(reports, RPT_ERROR, "No active Keying Set to use");
    return false;
  }
  if (anim_validate_keyingset(C, nullptr, ks) != 0) {
    if (ks->paths.first == nullptr) {
      if ((ks->flag & KEYINGSET_ABSOLUTE) == 0) {
        dune_report(reports,
                   RPT_ERROR,
                   "Use another Keying Set, as the active one depends on the currently "
                   "selected items or cannot find any targets due to unsuitable context");
      }
      else {
        BKE_report(reports, RPT_ERROR, "Keying Set does not contain any paths");
      }
    }
    return false;
  }

  /* if not extending sel, desel all sel first */
  if (extend == false) {
    CXT_DATA_BEGIN (C, PoseChannel *, pchan, visible_pose_bones) {
      if ((pchan->bone->flag & BONE_UNSELECTABLE) == 0) {
        pchan->bone->flag &= ~BONE_SEL;
      }
    }
    CXT_DATA_END;
  }

  uint objs_len = 0;
  Ob **obs = dune_ob_pose_array_get_unique(
      scene, view_layer, cxt_win_view3d(C), &obs_len);

  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = dune_ob_pose_armature_get(obs[ob_index]);
    Armature *arm = static_cast<Armature *>((ob) ? ob->data : nullptr);
    Pose *pose = (ob) ? ob->pose : nullptr;
    bool changed = false;

    /* Sanity checks. */
    if (ELEM(nullptr, ob, pose, arm)) {
      continue;
    }

    /* iter over elements in the Keying Set, setting sel depending on whether
     * that bone is visible or not... */
    LIST_FOREACH (KS_Path *, ksp, &ks->paths) {
      /* only items related to this ob will be relevant */
      if ((ksp->id == &ob->id) && (ksp->api_path != nullptr)) {
        PoseChannel *pchan = nullptr;
        char boneName[sizeof(pchan->name)];
        if (!lib_str_quoted_substr(ksp->api_path, "bones[", boneName, sizeof(boneName))) {
          continue;
        }
        pchan = dune_pose_channel_find_name(pose, boneName);

        if (pchan) {
          /* sel if bone is visible and can be affected */
          if (PBONE_SELECTABLE(arm, pchan->bone)) {
            pchan->bone->flag |= BONE_SEL;
            changed = true;
          }
        }
      }
    }

    if (changed || !extend) {
      ed_pose_bone_sel_tag_update(ob);
      changed_multi = true;
    }
  }
  mem_free(obs);

  return changed_multi;
}

static int pose_sel_grouped_ex(Cxt *C, WinOp *op)
{
  Ob *ob = dune_ob_pose_armature_get(cxt_data_active_ob(C));
  const ePoseSelSameMode type = ePoseSelSameMode(api_enum_get(op->ptr, "type"));
  const bool extend = api_bool_get(op->ptr, "extend");
  bool changed = false;

  /* sanity check */
  if (ob->pose == nullptr) {
    return OP_CANCELLED;
  }

  /* sel types */
  switch (type) {
    case POSE_SEL_SAME_COLLECTION:
      changed = pose_sel_same_collection(C, extend);
      break;

    case POSE_SEL_SAME_COLOR:
      changed = pose_sel_same_color(C, extend);
      break;

    case POSE_SEL_SAME_KEYINGSET: /* Keying Set */
      changed = pose_sel_same_keyingset(C, op->reports, extend);
      break;

    default:
      printf("pose_sel_grouped() - Unknown sel type %d\n", type);
      break;
  }

  /* report done status */
  if (changed) {
    ed_outliner_sel_sync_from_pose_bone_tag(C);

    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

void POSE_OT_sel_grouped(WinOpType *ot)
{
  static const EnumPropItem prop_sel_grouped_types[] = {
      {POSE_SEL_SAME_COLLECTION,
       "COLLECTION",
       0,
       "Collection",
       "Same collections as the active bone"},
      {POSE_SEL_SAME_COLOR, "COLOR", 0, "Color", "Same color as the active bone"},
      {POSE_SEL_SAME_KEYINGSET,
       "KEYINGSET",
       0,
       "Keying Set",
       "All bones affected by active Keying Set"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* ids */
  ot->name = "Sel Grouped";
  ot->description = "Sel all visible bones grouped by similar properties";
  ot->idname = "POSE_OT_sel_grouped";

  /* api cbs */
  ot->invoke = WM_menu_invoke;
  ot->ex = pose_sel_grouped_ex;
  ot->poll = ed_op_posemode; /* TODO: expand to support edit mode as well. */

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_bool(ot->sapi,
                  "extend",
                  false,
                  "Extend",
                  "Extend sel instead of desel everything first");
  ot->prop = api_def_enum(ot->sapi, "type", prop_sel_grouped_types, 0, "Type", "");
}
l
/* clone of armature_sel_mirror_ex keep in sync */
static int pose_sel_mirror_ex(Cxt *C, WinOp *op)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob_active = cxt_data_active_ob(C);

  const bool is_weight_paint = (ob_active->mode & OB_MODE_WEIGHT_PAINT) != 0;
  const bool active_only = api_bool_get(op->ptr, "only_active");
  const bool extend = api_bool_get(op->ptr, "extend");

  uint obs_len = 0;
  Ob **obs = dune_ob_pose_array_get_unique(
      scene, view_layer, cxt_win_view3d(C), &obs_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    PoseChannel *pchan_mirror_act = nullptr;

    LIST_FOREACH (PoseChannel *, pchan, &ob->pose->chanbase) {
      const int flag = (pchan->bone->flag & BONE_SEL);
      PBONE_PREV_FLAG_SET(pchan, flag);
    }

    LIST_FOREACH (PoseChannel *, pchan, &ob->pose->chanbase) {
      if (PBONE_SELECTABLE(arm, pchan->bone)) {
        PoseChannel *pchan_mirror;
        int flag_new = extend ? PBONE_PREV_FLAG_GET(pchan) : 0;

        if ((pchan_mirror = dune_pose_channel_get_mirrored(ob->pose, pchan->name)) &&
            PBONE_VISIBLE(arm, pchan_mirror->bone))
        {
          const int flag_mirror = PBONE_PREV_FLAG_GET(pchan_mirror);
          flag_new |= flag_mirror;

          if (pchan->bone == arm->act_bone) {
            pchan_mirror_act = pchan_mirror;
          }

          /* Skip all but the active or its mirror. */
          if (active_only && !ELEM(arm->act_bone, pchan->bone, pchan_mirror->bone)) {
            continue;
          }
        }

        pchan->bone->flag = (pchan->bone->flag & ~(BONE_SELECTED | BONE_TIPSEL | BONE_ROOTSEL)) |
                            flag_new;
      }
    }

    if (pchan_mirror_act) {
      arm->act_bone = pchan_mirror_act->bone;

      /* In weight-paint we sel the associated vertex group too. */
      if (is_weight_paint) {
        ed_vgroup_sel_by_name(ob_active, pchan_mirror_act->name);
        graph_id_tag_update(&ob_active->id, ID_RECALC_GEOMETRY);
      }
    }

    win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, ob);

    /* Need to tag armature for cow updates, or else sel doesn't update. */
    graph_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);
  }
  mem_free(obs);

ed_outliner_sel_sync_from_pose_bone_tag(C);

  return OP_FINISHED;
}

void POSE_OT_sel_mirror(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel Mirror";
  ot->idname = "POSE_OT_sel_mirror";
  ot->description = "Mirror the bone sel"

  /* api cbs */
  ot->ex = pose_sel_mirror_ex;
  ot->poll = ed_op_posemode;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_bool(
      ot->sapi, "only_active", false, "Active Only", "Only operate on the active bone");
  api_def_bool(ot->srna, "extend", false, "Extend", "Extend the selection");
}
