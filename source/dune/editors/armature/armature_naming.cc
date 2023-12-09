/* Ops and API's for renaming bones both in and out of Edit Mode.
 * This file contains fns/API's for renaming bones and/or working with them. */

#include <cstring>

#include "mem_guardedalloc.h"

#include "types_armature.h"
#include "types_camera.h"
#include "types_constraint.h"
#include "types_pen_legacy.h"
#include "types_pen_mod.h"
#include "types_ob_types.h"

#include "lib_dunelib.h"
#include "lib_ghash.h"
#include "lib_string_utils.hh"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_action.h"
#include "dune_animsys.h"
#include "dune_armature.hh"
#include "dune_constraint.h"
#include "dune_cxt.hh"
#include "dune_deform.h"
#include "dune_pen_mod_legacy.h"
#include "dune_layer.h"
#include "dune_main.hh"
#include "dune_mod.hh"

#include "graph.hh"

#include "api_access.hh"
#include "api_define.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_screen.hh"

#include "anim_bone_collections.hh"

#include "armature_intern.h"

/* Unique Bone Name Util (Edit Mode) */

/* there's a ed_armature_bone_unique_name() too! */
static bool editbone_unique_check(void *arg, const char *name)
{
  struct Arg {
    List *list;
    void *bone;
  } *data = static_cast<Arg *>(arg);
  EditBone *dup = ed_armature_ebone_find_name(data->list, name);
  return dup && dup != data->bone;
}

void ed_armature_ebone_unique_name(List *ebones, char *name, EditBone *bone)
{
  struct {
    List *list;
    void *bone;
  } data;
  data.list = ebones;
  data.bone = bone;

  lib_uniquename_cb(editbone_unique_check, &data, DATA_("Bone"), '.', name, sizeof(bone->name));
}

/* Unique Bone Name Util (Ob Mode) */
static bool bone_unique_check(void *arg, const char *name)
{
  return dune_armature_find_bone_name((Armature *)arg, name) != nullptr;
}

static void ed_armature_bone_unique_name(Armature *arm, char *name)
{
  lib_uniquename_cb(bone_unique_check, (void *)arm, DATA_("Bone"), '.', name, sizeof(Bone::name));
}

/* Bone Renaming (Ob & Edit Mode API) */
/* helper call for armature_bone_rename */
static void constraint_bone_name_fix(Ob *ob,
                                     List *conlist,
                                     const char *oldname,
                                     const char *newname)
{
  LIST_FOREACH (Constraint *, curcon, conlist) {
    List targets = {nullptr, nullptr};

    /* constraint targets */
    if (dune_constraint_targets_get(curcon, &targets)) {
      LIST_FOREACH (ConstraintTarget *, ct, &targets) {
        if (ct->tar == ob) {
          if (STREQ(ct->subtarget, oldname)) {
            STRNCPY(ct->subtarget, newname);
          }
        }
      }

      dune_constraint_targets_flush(curcon, &targets, false);
    }

    /* action constraints */
    if (curcon->type == CONSTRAINT_TYPE_ACTION) {
      ActionConstraint *actcon = (ActionConstraint *)curcon->data;
      dune_action_fix_paths_rename(
          &ob->id, actcon->act, "pose.bones", oldname, newname, 0, 0, true);
    }
  }
}

void ed_armature_bone_rename(Main *main,
                             Armature *arm,
                             const char *oldnamep,
                             const char *newnamep)
{
  Ob *ob;
  char newname[MAXBONENAME];
  char oldname[MAXBONENAME];

  /* names better differ! */
  if (!STREQLEN(oldnamep, newnamep, MAXBONENAME)) {

    /* we alter newname string... so make copy */
    STRNCPY(newname, newnamep);
    /* we use oldname for search... so make copy */
    STRNCPY(oldname, oldnamep);

    /* now check if we're in editmode, we need to find the unique name */
    if (arm->edbo) {
      EditBone *eBone = ed_armature_ebone_find_name(arm->edbo, oldname);

      if (eBone) {
        ed_armature_ebone_unique_name(arm->edbo, newname, nullptr);
        STRNCPY(eBone->name, newname);
      }
      else {
        return;
      }
    }
    else {
      Bone *bone = dune_armature_find_bone_name(arm, oldname);

      if (bone) {
        ed_armature_bone_unique_name(arm, newname);

        if (arm->bonehash) {
          lib_assert(lib_ghash_haskey(arm->bonehash, bone->name));
          lib_ghash_remove(arm->bonehash, bone->name, nullptr, nullptr);
        }

        STRNCPY(bone->name, newname);

        if (arm->bonehash) {
          lib_ghash_insert(arm->bonehash, bone->name, bone);
        }
      }
      else {
        return;
      }
    }

    /* force copy on write to update database */
    graph_id_tag_update(&arm->id, ID_RECALC_COPY_ON_WRITE);

    /* do entire dbase - obs */
    for (ob = static_cast<Ob *>(main->obs.first); ob;
         ob = static_cast<Ob *>(ob->id.next)) {

      /* we have the ob using the armature */
      if (arm == ob->data) {
        Ob *cob;

        /* Rename the pose channel, if it exists */
        if (ob->pose) {
          PoseChannel *pchan = dune_pose_channel_find_name(ob->pose, oldname);
          if (pchan) {
            GHash *gh = ob->pose->chanhash;

            /* remove the old hash entry, and replace with the new name */
            if (gh) {
              lib_assert(lib_ghash_haskey(gh, pchan->name));
              lib_ghash_remove(gh, pchan->name, nullptr, nullptr);
            }

            STRNCPY(pchan->name, newname);

            if (gh) {
              lib_ghash_insert(gh, pchan->name, pchan);
            }
          }

          lib_assert(dune_pose_channels_is_valid(ob->pose) == true);
        }

        /* Update any ob constraints to use the new bone name */
        for (cob = static_cast<Ob *>(main->obs.first); cob;
             cob = static_cast<Ob *>(cob->id.next))
        {
          if (cob->constraints.first) {
            constraint_bone_name_fix(ob, &cob->constraints, oldname, newname);
          }
          if (cob->pose) {
            LIST_FOREACH(PoseChannel *, pchan, &cob->pose->chanbase) {
              constraint_bone_name_fix(ob, &pchan->constraints, oldname, newname);
            }
          }
        }
      }

      /* See if an ob is parented to this armature */
      if (ob->parent && (ob->parent->data == arm)) {
        if (ob->partype == PARBONE) {
          /* bone name in ob */
          if (STREQ(ob->parsubstr, oldname)) {
            STRNCPY(ob->parsubstr, newname);
          }
        }
      }

      if (dune_mods_uses_armature(ob, arm) && dune_ob_supports_vertex_groups(ob)) {
        DeformGroup *dg = dune_ob_defgroup_find_name(ob, oldname);
        if (dg) {
          STRNCPY(dg->name, newname);
          graph_id_tag_update(static_cast<Id *>(ob->data), ID_RECALC_GEOMETRY);
        }
      }

      /* fix mods that might be using this name */
      LIST_FOREACH (ModData *, md, &ob->mods) {
        switch (md->type) {
          case eModTypeHook: {
            HookModData *hmd = (HookModData *)md;

            if (hmd->ob && (hmd->ob->data == arm)) {
              if (STREQ(hmd->subtarget, oldname)) {
                STRNCPY(hmd->subtarget, newname);
              }
            }
            break;
          }
          case eModTypeUVWarp: {
            UVWarpModData *umd = (UVWarpModData *)md;

            if (umd->ob_src && (umd->ob_src->data == arm)) {
              if (STREQ(umd->bone_src, oldname)) {
                STRNCPY(umd->bone_src, newname);
              }
            }
            if (umd->ob_dst && (umd->ob_dst->data == arm)) {
              if (STREQ(umd->bone_dst, oldname)) {
                STRNCPY(umd->bone_dst, newname);
              }
            }
            break;
          }
          default:
            break;
        }
      }

      /* fix camera focus */
      if (ob->type == OB_CAMERA) {
        Camera *cam = (Camera *)ob->data;
        if ((cam->dof.focus_ob != nullptr) && (cam->dof.focus_ob->data == arm)) {
          if (STREQ(cam->dof.focus_subtarget, oldname)) {
            STRNCPY(cam->dof.focus_subtarget, newname);
            graph_id_tag_update(&cam->id, ID_RECALC_COPY_ON_WRITE);
          }
        }
      }

      /* fix pen mods and vertex groups */
      if (ob->type == OB_PEN_LEGACY) {

        PenData *pend = (PenData *)ob->data;
        LIST_FOREACH (PenDataLayer *, penl, &pend->layers) {
          if ((penl->parent != nullptr) && (penl->parent->data == arm)) {
            if (STREQ(penl->parsubstr, oldname)) {
              STRNCPY(penl->parsubstr, newname);
            }
          }
        }

        LIST_FOREACH (PenModData *, pen_md, &ob->pen_mods) {
          switch (pen_md->type) {
            case ePenModTypeArmature: {
              ArmaturePenModData *mmd = (ArmaturePenModData *)pen_md;
              if (mmd->ob && mmd->ob->data == arm) {
                DeformGroup *dg = dune_ob_defgroup_find_name(ob, oldname);
                if (dg) {
                  STRNCPY(dg->name, newname);
                  graph_id_tag_update(static_cast<Id *>(ob->data), ID_RECALC_GEOMETRY);
                }
              }
              break;
            }
            case ePenModType_Hook: {
              HookPenModData *hgp_md = (HookPenModData *)pen_md;
              if (hgp_md->ob && (hgp_md->ob->data == arm)) {
                if (STREQ(hgp_md->subtarget, oldname)) {
                  STRNCPY(hgp_md->subtarget, newname);
                }
              }
              break;
            }
            default:
              break;
          }
        }
      }
      graph_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
    }

    /* Fix all animdata that may refer to this bone -
     * we can't just do the ones attached to obs,
     * since other Id-blocks may have drivers referring to this bone #29822. */

    /* TheId here is for armatures,
     * but most bone drivers are actually on the ob instead. */
    {

      dune_animdata_fix_paths_rename_all(&arm->id, "pose.bones", oldname, newname);
    }

    /* correct view locking */
    {
      Screen *screen;
      for (screen = static_cast<Screen *>(main->screens.first); screen;
           screen = static_cast<Screen *>(screen->id.next))
      {
        /* add rgns */
        LIST_FOREACH (ScrArea *, area, &screen->areabase) {
          LIST_FOREACH (SpaceLink *, sl, &area->spacedata) {
            if (sl->spacetype == SPACE_VIEW3D) {
              View3D *v3d = (View3D *)sl;
              if (v3d->ob_center && v3d->ob_center->data == arm) {
                if (STREQ(v3d->ob_center_bone, oldname)) {
                  STRNCPY(v3d->ob_center_bone, newname);
                }
              }
            }
          }
        }
      }
    }
  }
}

/* Bone Flipping (Ob & Edit Mode API) */
struct BoneFlipNameData {
  BoneFlipNameData *next, *prev;
  char *name;
  char name_flip[MAXBONENAME];
};

void ed_armature_bones_flip_names(Main *main,
                                  Armature *arm,
                                  List *bones_names,
                                  const bool do_strip_nums)
{
  List bones_names_conflicts = {nullptr};
  BoneFlipNameData *bfn;

  /* First pass: generate flip names, and blindly rename.
   * If rename did not yield expected result,
   * store both bone's name and expected flipped one into temp list for second pass. */
  LIST_FOREACH (LinkData *, link, bones_names) {
    char name_flip[MAXBONENAME];
    char *name = static_cast<char *>(link->data);

    /* WARNING: if do_strip_nums is set, expect completely mismatched names in cases like
     * Bone.R, Bone.R.001, Bone.R.002, etc. */
    lib_string_flip_side_name(name_flip, name, do_strip_nums, sizeof(name_flip));

    ed_armature_bone_rename(bmain, arm, name, name_flip);

    if (!STREQ(name, name_flip)) {
      bfn = static_cast<BoneFlipNameData *>(alloca(sizeof(BoneFlipNameData)));
      bfn->name = name;
      STRNCPY(bfn->name_flip, name_flip);
      lib_addtail(&bones_names_conflicts, bfn);
    }
  }

  /* 2nd pass to handle the bones that have naming conflicts with other bones.
   * Note that if the other bone was not see, its name was not flipped,
   * so conflict remains and that second rename simply generates a new numbered alternative name. */
  LIST_FOREACH (BoneFlipNameData *, bfn, &bones_names_conflicts) {
    ed_armature_bone_rename(main, arm, bfn->name, bfn->name_flip);
  }
}

/* Flip Bone Names (Edit Mode Op) */
static int armature_flip_names_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob_active = cxt_data_edit_ob(C);

  const bool do_strip_numbers = api_bool_get(op->ptr, "do_strip_numbers");

  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, dune_wm_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);

    /* Paranoia check. */
    if (ob_active->pose == nullptr) {
      continue;
    }

    List bones_names = {nullptr};

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_VISIBLE(arm, ebone)) {
        if (ebone->flag & BONE_SEL) {
          lib_addtail(&bones_names, lib_genericNodeN(ebone->name));

          if (arm->flag & ARM_MIRROR_EDIT) {
            EditBone *flipbone = ed_armature_ebone_get_mirrored(arm->edbo, ebone);
            if ((flipbone) && !(flipbone->flag & BONE_SEL)) {
              lib_addtail(&bones_names, lib_genericNodeN(flipbone->name));
            }
          }
        }
      }
    }

    if (lib_list_is_empty(&bones_names)) {
      continue;
    }

    ed_armature_bones_flip_names(main, arm, &bones_names, do_strip_numbers);

    lib_freelist(&bones_names);

    /* since we renamed stuff... */
    graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

    /* copied from api_Bone_update_renamed */
    /* Redrw Outliner / Dopesheet. */
    win_ev_add_notifier(C, NC_GEOM | ND_DATA | NA_RENAME, ob->data);

    /* update anim channels */
    win_ev_add_notifier(C, NC_ANIM | ND_ANIMCHAN, ob->data);
  }
  mem_free(obs);

  return OP_FINISHED;
}

void ARMATURE_OT_flip_names(WinOpType *ot)
{
  /* ids */
  ot->name = "Flip Names";
  ot->idname = "ARMATURE_OT_flip_names";
  ot->description = "Flips (and corrects) the axis suffixes of the names of selected bones";

  /* api cbs */
  ot->ex = armature_flip_names_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->sapi,
                  "do_strip_numbers",
                  false,
                  "Strip Numbers",
                  "Try to remove right-most dot-number from flipped names.\n"
                  "Warning: May result in incoherent naming in some cases");
}

/* Bone Auto Side Names (Edit Mode Op */
static int armature_autoside_names_ex(Cxt *C, WinOp *op)
{
  const Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Main *main = cxt_data_main(C);
  char newname[MAXBONENAME];
  const short axis = api_enum_get(op->ptr, "type");
  bool changed_multi = false;
  
  uint obs_len = 0;
  Ob **obs = dune_view_layer_array_from_obs_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *ob = obs[ob_index];
    Armature *arm = static_cast<Armature *>(ob->data);
    bool changed = false;

    /* Paranoia checks. */
    if (ELEM(nullptr, ob, ob->pose)) {
      continue;
    }

    LIST_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_EDITABLE(ebone)) {

        /* First need to do the flipped bone, then the original one.
         * Otherwise can't find the flipped one bc of the bone name change. */
        if (arm->flag & ARM_MIRROR_EDIT) {
          EditBone *flipbone = ed_armature_ebone_get_mirrored(arm->edbo, ebone);
          if ((flipbone) && !(flipbone->flag & BONE_SEL)) {
            STRNCPY(newname, flipbone->name);
            if (bone_autoside_name(newname, 1, axis, flipbone->head[axis], flipbone->tail[axis])) {
              ed_armature_bone_rename(main, arm, flipbone->name, newname);
              changed = true;
            }
          }
        }

        STRNCPY(newname, ebone->name);
        if (bone_autoside_name(newname, 1, axis, ebone->head[axis], ebone->tail[axis])) {
          ed_armature_bone_rename(main, arm, ebone->name, newname);
          changed = true;
        }
      }
    }

    if (!changed) {
      continue;
    }

    changed_multi = true;

    /* Since we renamed stuff... */
    graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

    /* NOTE: notifier might evolve. */
    win_ev_add_notifier(C, NC_OB | ND_POSE, ob);
  }
  mem_free(obs);
  return changed_multi ? OP_FINISHED : OP_CANCELLED;
}

void ARMATURE_OT_autoside_names(WinOpType *ot)
{
  static const EnumPropItem axis_items[] = {
      {0, "XAXIS", 0, "X-Axis", "Left/Right"},
      {1, "YAXIS", 0, "Y-Axis", "Front/Back"},
      {2, "ZAXIS", 0, "Z-Axis", "Top/Bottom"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* ids */
  ot->name = "Auto-Name by Axis";
  ot->idname = "ARMATURE_OT_autoside_names";
  ot->description =
      "Automatically renames the sel bones according to which side of the target axis they "
      "fall on";

  /* api cbs */
  ot->invoke = wim_menu_invoke;
  ot->ex = armature_autoside_names_ex;
  ot->poll = ed_op_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* settings */
  ot->prop = api_def_enum(ot->sapi, "type", axis_items, 0, "Axis", "Axis to tag names with");
}
