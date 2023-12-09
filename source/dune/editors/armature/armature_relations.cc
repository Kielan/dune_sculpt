#include "mem_guardedalloc.h"

#include "types_anim.h"
#include "types_armature.h"
#include "types_constraint.h"
#include "types_ob.h"
#include "types_scene.h"

#include "lib_dunelib.h"
#include "lib_ghash.h"
#include "lib_map.hh"
#include "lib_math_matrix.h"
#include "lib_math_vector.h"

#include "lang.h"

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_animsys.h"
#include "dune_armature.hh"
#include "dune_constraint.h"
#include "dune_cxt.hh"
#include "dune_fcurve_driver.h"
#include "dune_layer.h"
#include "dune_main.hh"
#include "dune_report.h"

#include "graph.hh"
#include "graph_build.hh"

#include "api_access.hh"
#include "api_define.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_ob.hh"
#include "ed_outliner.hh"
#include "ed_screen.hh"

#include "ui.hh"
#include "ui_resources.hh"

#include "anim_bone_collections.hh"

#include "armature_intern.h"

/* Edit Armature Join
 * No op define here as this is exported to the Ob-level op. */

static void joined_armature_fix_links_constraints(Main *main,
                                                  Ob *ob,
                                                  Ob *tarArm,
                                                  Ob *srcArm,
                                                  PoseChannel *pchan,
                                                  EditBone *curbone,
                                                  List *list)
{
  bool changed = false;

  LIST_FOREACH (Constraint *, con, list) {
    List targets = {nullptr, nullptr};

    /* constraint targets */
    if (dune_constraint_targets_get(con, &targets)) {
      LIST_FOREACH (ConstraintTarget *, ct, &targets) {
        if (ct->tar == srcArm) {
          if (ct->subtarget[0] == '\0') {
            ct->tar = tarArm;
            changed = true;
          }
          else if (STREQ(ct->subtarget, pchan->name)) {
            ct->tar = tarArm;
            STRNCPY(ct->subtarget, curbone->name);
            changed = true;
          }
        }
      }

      dune_constraint_targets_flush(con, &targets, false);
    }

    /* action constraint? (pose constraints only) */
    if (con->type == CONSTRAINT_TYPE_ACTION) {
      ActionConstraint *data = static_cast<ActionConstraint *>(con->data);

      if (data->act) {
        dune_action_fix_paths_rename(
            &tarArm->id, data->act, "pose.bones[", pchan->name, curbone->name, 0, 0, false);

        graph_id_tag_update_ex(main, &data->act->id, ID_RECALC_COPY_ON_WRITE);
      }
    }
  }

  if (changed) {
    graph_id_tag_update_ex(main, &ob->id, ID_RECALC_COPY_ON_WRITE);
  }
}

/* User-data for joined_armature_fix_animdata_cb(). */
struct tJoinArmatureAdtFixData {
  Main *main;

  Ob *srcArm;
  Ob *tarArm;

  GHash *names_map;
};

/* Cb to pass to dune_animdata_main_cb() for fixing driver Id's to point to the new Id */
/* FIXME: For now, we only care about drivers here.
 * When editing rigs, it's very rare to have anim on the rigs being edited alrdy,
 * so it should be safe to skip these. */
static void joined_armature_fix_animdata_cb(Id *id, FCurve *fcu, void *user_data)
{
  tJoinArmatureAdtFixData *afd = (tJoinArmatureAdtFixData *)user_data;
  Id *src_id = &afd->srcArm->id;
  Id *dst_id = &afd->tarArm->id;

  GHashIterator gh_iter;
  bool changed = false;

  /* Fix paths - If this is the target ob, it will have some "dirty" paths */
  if ((id == src_id) && strstr(fcu->apu_path, "pose.bones[")) {
    GHASH_ITER (gh_iter, afd->names_map) {
      const char *old_name = static_cast<const char *>(lib_ghashIter_getKey(&gh_iter));
      const char *new_name = static_cast<const char *>(lib_ghashIter_getVal(&gh_iter));

      /* only remap if changed; this still means there will be some
       * waste if there aren't many drivers/keys */
      if (!STREQ(old_name, new_name) && strstr(fcu->api_path, old_name)) {
        fcu->api_path = dune_animsys_fix_api_path_rename(
            id, fcu->api_path, "pose.bones", old_name, new_name, 0, 0, false);

        changed = true;

        /* we don't want to apply a second remapping on this driver now,
         * so stop trying names, but keep fixing drivers */
        break;
      }
    }
  }

  /* Driver targets */
  if (fcu->driver) {
    ChannelDriver *driver = fcu->driver;

    /* Ensure that invalid drivers gets re-eval in case they become valid once the join
     * op is finished. */
    fcu->flag &= ~FCURVE_DISABLED;
    driver->flag &= ~DRIVER_FLAG_INVALID;

    /* Fix driver refs to invalid ID's */
    LIST_FOREACH (DriverVar *, dvar, &driver->vars) {
      /* only change the used targets, since the others will need fixing manually anyway */
      DRIVER_TARGETS_USED_LOOPER_BEGIN (dvar) {
        /* change the Id's used... */
        if (dtar->id == src_id) {
          dtar->id = dst_id;

          changed = true;

          /* also check on the subtarget...
           * We dup the logic from drivers_path_rename_fix() here, with our own
           * little twists so that we know that it isn't going to clobber the wrong data */
          if ((dtar->api_path && strstr(dtar->rna_path, "pose.bones[")) || (dtar->pchan_name[0])) {
            GHASH_ITER (gh_iter, afd->names_map) {
              const char *old_name = static_cast<const char *>(lib_ghashIter_getKey(&gh_iter));
              const char *new_name = static_cast<const char *>(
                  lib_ghashIter_getVal(&gh_iter));

              /* only remap if changed */
              if (!STREQ(old_name, new_name)) {
                if ((dtar->api_path) && strstr(dtar->rna_path, old_name)) {
                  /* Fix up path */
                  dtar->api_path = dune_animsys_fix_api_path_rename(
                      id, dtar->api_path, "pose.bones", old_name, new_name, 0, 0, false);
                  break; /* no need to try any more names for bone path */
                }
                if (STREQ(dtar->pchan_name, old_name)) {
                  /* Change target bone name */
                  STRNCPY(dtar->pchan_name, new_name);
                  break; /* no need to try any more names for bone subtarget */
                }
              }
            }
          }
        }
      }
      DRIVER_TARGETS_LOOPER_END;
    }
  }

  if (changed) {
    graph_id_tag_update_ex(afd->main, id, ID_RECALC_COPY_ON_WRITE);
  }
}

/* Helper fn for armature joining - link fixing */
static void joined_armature_fix_links(
    Main *main, Ob *tarArm, Ob *srcArm, PoseChannel *pchan, EditBone *curbone)
{
  Ob *ob;
  Pose *pose;

  /* let's go thru all obs in database */
  for (ob = static_cast<Ob *>(main->obs.first); ob;
       ob = static_cast<Ob *>(ob->id.next)) {
    /* do some ob-type specific things */
    if (ob->type == OB_ARMATURE) {
      pose = ob->pose;
      LIST_FOREACH (PoseChannel *, pchant, &pose->chanbase) {
        joined_armature_fix_links_constraints(
            main, ob, tarArm, srcArm, pchan, curbone, &pchant->constraints);
      }
    }

    /* fix ob-level constraints */
    if (ob != srcArm) {
      joined_armature_fix_links_constraints(
          main, ob, tarArm, srcArm, pchan, curbone, &ob->constraints);
    }

    /* See if an ob is parented to this armature */
    if (ob->parent && (ob->parent == srcArm)) {
      /* Is ob parented to a bone of this src armature? */
      if (ob->partype == PARBONE) {
        /* bone name in ob */
        if (STREQ(ob->parsubstr, pchan->name)) {
          STRNCPY(ob->parsubstr, curbone->name);
        }
      }

      /* make tar armature be new parent */
      ob->parent = tarArm;

      graph_id_tag_update_ex(main, &ob->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
}

int ed_armature_join_obs_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  Ob *ob_active = cxt_data_active_ob(C);
  Armature *arm = static_cast<Armature *>((ob_active) ? ob_active->data : nullptr);
  Pose *pose, *opose;
  PoseChannel *pchan, *pchann;
  EditBone *curbone;
  float mat[4][4], oimat[4][4];
  bool ok = false;

  /* Ensure we're not in edit-mode and that the active object is an armature. */
  if (!ob_active || ob_active->type != OB_ARMATURE) {
    return OP_CANCELLED;
  }
  if (!arm || arm->edbo) {
    return OP_CANCELLED;
  }

  CXT_DATA_BEGIN (C, Ob *, ob_iter, sel_editable_obs) {
    if (ob_iter == ob_active) {
      ok = true;
      break;
    }
  }
  CXT_DATA_END;

  /* that way the active ob is always sel */
  if (ok == false) {
    dune_report(op->reports, RPT_WARNING, "Active ob is not a sel armature");
    return OP_CANCELLED;
  }

  /* Inverse transform for all sel armatures in this ob,
   * See ob_join_ex for detailed comment on why the safe version is used. */
  invert_m4_m4_safe_ortho(oimat, ob_active->ob_to_world);

  /* Index bone collections by name.  This is also used later to keep track
   * of collections added from other armatures. */
  dune::Map<std::string, BoneCollection *> bone_collection_by_name;
  for (BoneCollection *bcoll : arm->collections_span()) {
    bone_collection_by_name.add(bcoll->name, bcoll);
  }

  /* Used to track how bone collections should be remapped after merging
   * other armatures. */
  dune::Map<BoneCollection *, BoneCollection *> bone_collection_remap;

  /* Get edit-bones of active armature to add edit-bones to */
  ed_armature_to_edit(arm);

  /* Get pose of active ob and move it out of pose-mode */
  pose = ob_active->pose;
  ob_active->mode &= ~OB_MODE_POSE;

  CXT_DATA_BEGIN (C, Ob *, ob_iter, sel_editable_obs) {
    if ((ob_iter->type == OB_ARMATURE) && (ob_iter != ob_active)) {
      tJoinArmature_AdtFixData afd = {nullptr};
      Armature *curarm = static_cast<Armature *>(ob_iter->data);

      /* we assume that each armature datablock is only used in a single place */
      lib_assert(ob_active->data != ob_iter->data);

      /* init cb data for fixing up AnimData links later */
      afd.main = main;
      afd.srcArm = ob_iter;
      afd.tarArm = ob_active;
      afd.names_map = lib_ghash_str_new("join_armature_adt_fix");

      /* Make a list of edit-bones in current armature */
      ED_armature_to_edit(curarm);

      /* Move new bone collections, and store their remapping info.
       * TODO armatures can potentially have multiple users, so these should
       * actually be copied, not moved.  However, the armature join code is
       * already broken in that situation.  When that gets fixed, this should
       * also get fixed.  Note that copying the collections should include
       * copying their custom props. */
       for (BoneCollection *bcoll : curarm->collections_span()) {
        BoneCollection *mapped = bone_collection_by_name.lookup_default(bcoll->name, nullptr);
        if (!mapped) {
          BoneCollection *new_bcoll =anim_armature_bonecoll_new(arm, bcoll->name);
          bone_collection_by_name.add(bcoll->name, new_bcoll);
          mapped = new_bcoll;
        }

        bone_collection_remap.add(bcoll, mapped);
      }

      /* Get Pose of current armature */
      opose = ob_iter->pose;
      ob_iter->mode &= ~OB_MODE_POSE;
      // BASACT->flag &= ~OB_MODE_POSE;

      /* Find the diff matrix */
      mul_m4_m4m4(mat, oimat, ob_iter->ob_to_world);

      /* Copy bones and posechannels from the ob to the edit armature */
      for (pchan = static_cast<PoseChannel *>(opose->chanbase.first); pchan; pchan = pchann) {
        pchann = pchan->next;
        curbone = ed_armature_ebone_find_name(curarm->edbo, pchan->name);

        /* Get new name */
        ed_armature_ebone_unique_name(arm->edbo, curbone->name, nullptr);
        lib_ghash_insert(afd.names_map, lib_strdup(pchan->name), curbone->name);

        /* Transform the bone */
        {
          float premat[4][4];
          float postmat[4][4];
          float difmat[4][4];
          float imat[4][4];
          float temp[3][3];

          /* Get the premat */
          ed_armature_ebone_to_mat3(curbone, tmp);

          unit_m4(premat); /* mul_m4_m3m4 only sets 3x3 part */
          mul_m4_m3m4(premat, tmp, mat);

          mul_m4_v3(mat, curbone->head);
          mul_m4_v3(mat, curbone->tail);

          /* Get the postmat */
          ed_armature_ebone_to_mat3(curbone, tmp);
          copy_m4_m3(postmat, temp);

          /* Find the roll */
          invert_m4_m4(imat, premat);
          mul_m4_m4m4(difmat, imat, postmat);

          curbone->roll -= atan2f(difmat[2][0], difmat[2][2]);
        }

        /* Fix Constraints and Other Links to this Bone and Armature */
        joined_armature_fix_links(main, ob_active, ob_iter, pchan, curbone);

        /* Rename pchan */
        STRNCPY(pchan->name, curbone->name);

        /* Jump Ship! */
        lib_remlink(curarm->edbo, curbone);
        lib_addtail(arm->edbo, curbone);

        /* Pose channel is moved from one storage to another, its UUID is still unique. */
        lib_remlink(&opose->chanbase, pchan);
        lib_addtail(&pose->chanbase, pchan);
        dune_pose_channels_hash_free(opose);
        dune_pose_channels_hash_free(pose);

        /* Remap collections. */
        LIST_FOREACH (BoneCollectionReference *, bcoll_ref, &curbone->bone_collections) {
          bcoll_ref->bcoll = bone_collection_remap.lookup(bcoll_ref->bcoll);
        }
      }

      /* Armature Id itself is not freed below, however it has been modded (and is now completely
       * empty). This needs to be told to the graph, it will also ensure that the global
       * memfile undo sys properly detects the change.
       *
       * FIXME: Modding an existing obdata bc we are joining an ob using it into another
       * ob is a very questionable behavior, which also does not match w other ob types
       * joining. */
      graph_id_tag_update_ex(main, &curarm->id, ID_RECALC_GEOMETRY);

      /* Fix all the drivers (and animation data) */
      dune_fcurves_main_cb(main, joined_armature_fix_animdata_cb, &afd);
      lib_ghash_free(afd.names_map, mem_free, nullptr);

      /* Only copy over animdata now, after all the remapping has been done,
       * so that we don't have to worry about ambiguities re which armature
       * a bone came from! */
      if (ob_iter->adt) {
        if (ob_active->adt == nullptr) {
          /* no animdata, so just use a copy of the whole thing */
          ob_active->adt = dune_animdata_copy(main, ob_iter->adt, 0);
        }
        else {
          /* merge in data: we fix the drivers manually */
          dune_animdata_merge_copy(
              main, &ob_active->id, &ob_iter->id, ADT_MERGECOPY_KEEP_DST, false);
        }
      }

      if (curarm->adt) {
        if (arm->adt == nullptr) {
          /* no animdata, so just use a copy of the whole thing */
          arm->adt = dune_animdata_copy(main, curarm->adt, 0);
        }
        else {
          /* merge in data we'll fix the drivers manually */
          dune_animdata_merge_copy(main, &arm->id, &curarm->id, ADT_MERGECOPY_KEEP_DST, false);
        }
      }

      /* Free the old ob data */
      ed_ob_base_free_and_unlink(main, scene, ob_iter);
    }
  }
  CXT_DATA_END;

  graph_tag_update(main); /* bc we removed ob(s) */

  ed_armature_from_edit(main, arm);
  ed_armature_edit_free(arm);

  graph_id_tag_update(&scene->id, ID_RECALC_SEL);
  win_ev_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);
  win_ev_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);

  return OP_FINISHED;
}

/* Edit Armature Separate */
/* Helper fn for armature separating link fixing */
static void separated_armature_fix_links(Main *main, Ob *origArm, Ob *newArm)
{
  Ob *ob;
  List *opchans, *npchans;

  /* Get ref to list of bones in original and new armatures. */
  opchans = &origArm->pose->chanbase;
  npchans = &newArm->pose->chanbase;

  /* let's go thru all obs in database */
  for (ob = static_cast<Ob *>(main->obs.first); ob;
       ob = static_cast<Ob *>(ob->id.next)) {
    /* do some ob-type specific things */
    if (ob->type == OB_ARMATURE) {
      LIST_FOREACH (PoseChannel *, pchan, &ob->pose->chanbase) {
        LIST_FOREACH (Constraint *, con, &pchan->constraints) {
          List targets = {nullptr, nullptr};

          /* constraint targets */
          if (dune_constraint_targets_get(con, &targets)) {
            LIST_FOREACH (ConstraintTarget *, ct, &targets) {
              /* Any targets which point to original armature
               * are redirected to the new one only if:
               * - The target isn't origArm/newArm itself.
               * - The target is one that can be found in newArm/origArm.
               */
              if (ct->subtarget[0] != 0) {
                if (ct->tar == origArm) {
                  if (lib_findstring(npchans, ct->subtarget, offsetof(PoseChannel, name))) {
                    ct->tar = newArm;
                  }
                }
                else if (ct->tar == newArm) {
                  if (lib_findstring(opchans, ct->subtarget, offsetof(PoseChannel, name))) {
                    ct->tar = origArm;
                  }
                }
              }
            }

            dune_constraint_targets_flush(con, &targets, false);
          }
        }
      }
    }

    /* fix ob-level constraints */
    if (ob != origArm) {
      LIST_FOREACH (bConstraint *, con, &ob->constraints) {
        List targets = {nullptr, nullptr};

        /* constraint targets */
        if (dune_constraint_targets_get(con, &targets)) {
          LIST_FOREACH (ConstraintTarget *, ct, &targets) {
            /* any targets which point to original armature are redirected to the new one only if:
             * - the target isn't origArm/newArm itself
             * - the target is one that can be found in newArm/origArm */
            if (ct->subtarget[0] != '\0') {
              if (ct->tar == origArm) {
                if (lib_findstring(npchans, ct->subtarget, offsetof(PoseChannel, name))) {
                  ct->tar = newArm;
                }
              }
              else if (ct->tar == newArm) {
                if (lib_findstring(opchans, ct->subtarget, offsetof(PoseChannel, name))) {
                  ct->tar = origArm;
                }
              }
            }
          }

          dune_constraint_targets_flush(con, &targets, false);
        }
      }
    }

    /* See if an ob is parented to this armature */
    if (ob->parent && (ob->parent == origArm)) {
      /* Is ob parented to a bone of this src armature? */
      if ((ob->partype == PARBONE) && (ob->parsubstr[0] != '\0')) {
        if (lib_findstring(npchans, ob->parsubstr, offsetof(PoseChannel, name))) {
          ob->parent = newArm;
        }
      }
    }
  }
}

/* Helper fn for armature separating - remove certain bones from the given armature.
 *
 * param ob: Armature ob (must not be is not in edit-mode).
 * param is_sel: remove sel bones from the armature,
 * otherwise the unsel bones are removed. */
static void separate_armature_bones(Main *main, Ob *ob, const bool is_sel)
{
  Armature *arm = (Armature *)ob->data;
  PoseChannel *pchan, *pchann;
  EditBone *curbone;

  /* make local set of edit-bones to manipulate here */
  ed_armature_to_edit(arm);

  /* go through pose-channels, checking if a bone should be removed */
  for (pchan = static_cast<PoseChannel *>(ob->pose->chanbase.first); pchan; pchan = pchann) {
    pchann = pchan->next;
    curbone = ed_armature_ebone_find_name(arm->edbo, pchan->name);

    /* check if bone needs to be removed */
    if (is_sel == (EBONE_VISIBLE(arm, curbone) && (curbone->flag & BONE_SEL))) {

      /* Clear the bone->parent var of any bone that had this as its parent. */
      LIST_FOREACH (EditBone *, ebo, arm->edbo) {
        if (ebo->parent == curbone) {
          ebo->parent = nullptr;
          /* this is needed to prevent random crashes with in ed_armature_from_edit */
          ebo->tmp.p = nullptr;
          ebo->flag &= ~BONE_CONNECTED;
        }
      }

      /* clear the pchan->parent var of any pchan that had this as its parent */
      LIST_FOREACH (PoseChannel *, pchn, &ob->pose->chanbase) {
        if (pchn->parent == pchan) {
          pchn->parent = nullptr;
        }
        if (pchn->bbone_next == pchan) {
          pchn->bbone_next = nullptr;
        }
        if (pchn->bbone_prev == pchan) {
          pchn->bbone_prev = nullptr;
        }
      }

      /* Free any of the extra-data this pchan might have. */
      dune_pose_channel_free(pchan);
      dune_pose_channels_hash_free(ob->pose);

      /* get rid of unneeded bone */
      bone_free(arm, curbone);
      lib_freelink(&ob->pose->chanbase, pchan);
    }
  }

  /* Exit edit-mode (recalcs pose-channels too). */
  ed_armature_edit_desel_all(ob);
  ed_armature_from_edit(main, static_cast<Armature *>(ob->data));
  ed_armature_edit_free(static_cast<Armature *>(ob->data));
}

/* separate sel bones into their armature */
static int separate_armature_ex(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  bool ok = false;

  /* set wait cursor in case this takes a while */
  win_cursor_wait(true);

  uint bases_len = 0;
  Base **bases = dune_view_layer_array_from_bases_in_edit_mode_unique_data(
      scene, view_layer, cxt_win_view3d(C), &bases_len);

  for (uint base_index = 0; base_index < bases_len; base_index++) {
    Base *base_old = bases[base_index];
    Ob *ob_old = base_old->ob;

    {
      Armature *arm_old = static_cast<bArmature *>(ob_old->data);
      bool has_sel_bone = false;
      bool has_sel_any = false;
      LIST_FOREACH (EditBone *, ebone, arm_old->edbo) {
        if (EBONE_VISIBLE(arm_old, ebone)) {
          if (ebone->flag & BONE_SEL) {
            has_sel_bone = true;
            break;
          }
          if (ebone->flag & (BONE_TIPSEL | BONE_ROOTSEL)) {
            has_sel_any = true;
          }
        }
      }
      if (has_sel_bone == false) {
        if (has_sel_any) {
          /* Wo this, we may leave head/tail set
           * which isn't expected after separating. */
          ed_armature_edit_desel_all(ob_old);
        }
        continue;
      }
    }

    /* We are going to do this as follows (unlike every other instance of separate):
     * 1. Exit edit-mode & pose-mode for active armature/base. Take note of what this is.
     * 2. Dup base - BASACT is the new one now
     * 3. For each of the two armatures,
     *    enter edit-mode -> remove appropriate bones -> exit edit-mode + recalc.
     * 4. Fix constraint links
     * 5. Make original armature active and enter edit-mode */

    /* 1) store starting settings and exit edit-mode */
    ob_old->mode &= ~OB_MODE_POSE;

    ed_armature_from_edit(main, static_cast<Armature *>(ob_old->data));
    ed_armature_edit_free(static_cast<Armature *>(ob_old->data));

    /* 2) dup base */
    /* Only dup linked armature but take into account
     * user prefs for dup'ing actions. */
    short dupflag = USER_DUP_ARM | (U.dupflag & USER_DUP_ACT);
    Base *base_new = ed_ob_add_dup(
        main, scene, view_layer, base_old, eDupDFlags(dupflag));
    Ob *ob_new = base_new->ob;

    graph_tag_update(main);

    /* 3) remove bones that shouldn't still be around on both armatures */
    separate_armature_bones(main, ob_old, true);
    separate_armature_bones(main, ob_new, false);

    /* 4) fix links before graph flushes, err... or after? */
    separated_armature_fix_links(main, ob_old, ob_new);

    graph_id_tag_update(&ob_old->id, ID_RECALC_GEOMETRY); /* this is the original one */
    graph_id_tag_update(&ob_new->id, ID_RECALC_GEOMETRY); /* this is the separated one */

    /* 5) restore original conditions */
    ed_armature_to_edit(static_cast<Armature *>(ob_old->data));

    /* parents tips remain sel when connected children are removed. */
    ed_armature_edit_desel_all(ob_old);

    ok = true;

    /* NOTE: notifier might evolve. */
    win_ev_add_notifier(C, NC_OBJECT | ND_POSE, ob_old);
  }
  mem_free(bases);

  /* Recalc/redrw + cleanup */
  win_cursor_wait(false);

  if (ok) {
    dune_report(op->reports, RPT_INFO, "Separated bones");
    ed_outliner_sel_sync_from_ob_tag(C);
  }

  return OP_FINISHED;
}

void ARMATURE_OT_separate(WinOpType *ot)
{
  /* ids */
  ot->name = "Separate Bones";
  ot->idname = "ARMATURE_OT_separate";
  ot->description = "Isolate sel bones into a separate armature";

  /* callbacks */
  ot->invoke = WM_operator_confirm_or_exec;
  ot->exec = separate_armature_exec;
  ot->poll = ED_operator_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
  WM_operator_properties_confirm_or_exec(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Armature Parenting
 * \{ */

/* armature parenting options */
#define ARM_PAR_CONNECT 1
#define ARM_PAR_OFFSET 2

/* armature un-parenting options */
#define ARM_PAR_CLEAR 1
#define ARM_PAR_CLEAR_DISCONNECT 2

/* check for null, before calling! */
static void bone_connect_to_existing_parent(EditBone *bone)
{
  bone->flag |= BONE_CONNECTED;
  copy_v3_v3(bone->head, bone->parent->tail);
  bone->rad_head = bone->parent->rad_tail;
}

static void bone_connect_to_new_parent(ListBase *edbo,
                                       EditBone *selbone,
                                       EditBone *actbone,
                                       short mode)
{
  EditBone *ebone;
  float offset[3];

  if ((selbone->parent) && (selbone->flag & BONE_CONNECTED)) {
    selbone->parent->flag &= ~BONE_TIPSEL;
  }

  /* make actbone the parent of selbone */
  selbone->parent = actbone;

  /* in actbone tree we cannot have a loop */
  for (ebone = actbone->parent; ebone; ebone = ebone->parent) {
    if (ebone->parent == selbone) {
      ebone->parent = nullptr;
      ebone->flag &= ~BONE_CONNECTED;
    }
  }

  if (mode == ARM_PAR_CONNECT) {
    /* Connected: Child bones will be moved to the parent tip */
    selbone->flag |= BONE_CONNECTED;
    sub_v3_v3v3(offset, actbone->tail, selbone->head);

    copy_v3_v3(selbone->head, actbone->tail);
    selbone->rad_head = actbone->rad_tail;

    add_v3_v3(selbone->tail, offset);

    /* offset for all its children */
    LISTBASE_FOREACH (EditBone *, ebone, edbo) {
      EditBone *par;

      for (par = ebone->parent; par; par = par->parent) {
        if (par == selbone) {
          add_v3_v3(ebone->head, offset);
          add_v3_v3(ebone->tail, offset);
          break;
        }
      }
    }
  }
  else {
    /* Offset: Child bones will retain their distance from the parent tip */
    selbone->flag &= ~BONE_CONNECTED;
  }
}

static const EnumPropertyItem prop_editarm_make_parent_types[] = {
    {ARM_PAR_CONNECT, "CONNECTED", 0, "Connected", ""},
    {ARM_PAR_OFFSET, "OFFSET", 0, "Keep Offset", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static int armature_parent_set_exec(bContext *C, wmOperator *op)
{
  Object *ob = CTX_data_edit_object(C);
  bArmature *arm = (bArmature *)ob->data;
  EditBone *actbone = CTX_data_active_bone(C);
  EditBone *actmirb = nullptr;
  short val = RNA_enum_get(op->ptr, "type");

  /* there must be an active bone */
  if (actbone == nullptr) {
    BKE_report(op->reports, RPT_ERROR, "Operation requires an active bone");
    return OPERATOR_CANCELLED;
  }
  if (arm->flag & ARM_MIRROR_EDIT) {
    /* For X-Axis Mirror Editing option, we may need a mirror copy of actbone:
     * - If there's a mirrored copy of selbone, try to find a mirrored copy of actbone
     *   (i.e.  selbone="child.L" and actbone="parent.L", find "child.R" and "parent.R").
     *   This is useful for arm-chains, for example parenting lower arm to upper arm.
     * - If there's no mirrored copy of actbone (i.e. actbone = "parent.C" or "parent")
     *   then just use actbone. Useful when doing upper arm to spine.
     */
    actmirb = ED_armature_ebone_get_mirrored(arm->edbo, actbone);
    if (actmirb == nullptr) {
      actmirb = actbone;
    }
  }

  /* If there is only 1 selected bone, we assume that it is the active bone,
   * since a user will need to have clicked on a bone (thus selecting it) to make it active. */
  bool is_active_only_selected = false;
  if (actbone->flag & BONE_SELECTED) {
    is_active_only_selected = true;
    LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_EDITABLE(ebone) && (ebone->flag & BONE_SELECTED)) {
        if (ebone != actbone) {
          is_active_only_selected = false;
          break;
        }
      }
    }
  }

  if (is_active_only_selected) {
    /* When only the active bone is selected, and it has a parent,
     * connect it to the parent, as that is the only possible outcome.
     */
    if (actbone->parent) {
      bone_connect_to_existing_parent(actbone);

      if ((arm->flag & ARM_MIRROR_EDIT) && (actmirb->parent)) {
        bone_connect_to_existing_parent(actmirb);
      }
    }
  }
  else {
    /* Parent 'selected' bones to the active one:
     * - The context iterator contains both selected bones and their mirrored copies,
     *   so we assume that unselected bones are mirrored copies of some selected bone.
     * - Since the active one (and/or its mirror) will also be selected, we also need
     *   to check that we are not trying to operate on them, since such an operation
     *   would cause errors.
     */

    /* Parent selected bones to the active one. */
    LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_EDITABLE(ebone) && (ebone->flag & BONE_SELECTED)) {
        if (ebone != actbone) {
          bone_connect_to_new_parent(arm->edbo, ebone, actbone, val);
        }

        if (arm->flag & ARM_MIRROR_EDIT) {
          EditBone *ebone_mirror = ED_armature_ebone_get_mirrored(arm->edbo, ebone);
          if (ebone_mirror && (ebone_mirror->flag & BONE_SELECTED) == 0) {
            if (ebone_mirror != actmirb) {
              bone_connect_to_new_parent(arm->edbo, ebone_mirror, actmirb, val);
            }
          }
        }
      }
    }
  }

  /* NOTE: notifier might evolve. */
  WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, ob);
  DEG_id_tag_update(&ob->id, ID_RECALC_SELECT);

  return OPERATOR_FINISHED;
}

static int armature_parent_set_invoke(bContext *C, wmOperator * /*op*/, const wmEvent * /*event*/)
{
  /* False when all selected bones are parented to the active bone. */
  bool enable_offset = false;
  /* False when all selected bones are connected to the active bone. */
  bool enable_connect = false;
  {
    Object *ob = CTX_data_edit_object(C);
    bArmature *arm = static_cast<bArmature *>(ob->data);
    EditBone *actbone = arm->act_edbone;
    LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
      if (!EBONE_EDITABLE(ebone) || !(ebone->flag & BONE_SELECTED)) {
        continue;
      }
      if (ebone == actbone) {
        continue;
      }

      if (ebone->parent != actbone) {
        enable_offset = true;
        enable_connect = true;
        break;
      }
      if (!(ebone->flag & BONE_CONNECTED)) {
        enable_connect = true;
      }
    }
  }

  uiPopupMenu *pup = UI_popup_menu_begin(
      C, CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, "Make Parent"), ICON_NONE);
  uiLayout *layout = UI_popup_menu_layout(pup);

  uiLayout *row_offset = uiLayoutRow(layout, false);
  uiLayoutSetEnabled(row_offset, enable_offset);
  uiItemEnumO(row_offset, "ARMATURE_OT_parent_set", nullptr, ICON_NONE, "type", ARM_PAR_OFFSET);

  uiLayout *row_connect = uiLayoutRow(layout, false);
  uiLayoutSetEnabled(row_connect, enable_connect);
  uiItemEnumO(row_connect, "ARMATURE_OT_parent_set", nullptr, ICON_NONE, "type", ARM_PAR_CONNECT);

  UI_popup_menu_end(C, pup);

  return OPERATOR_INTERFACE;
}

void ARMATURE_OT_parent_set(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Make Parent";
  ot->idname = "ARMATURE_OT_parent_set";
  ot->description = "Set the active bone as the parent of the selected bones";

  /* api callbacks */
  ot->invoke = armature_parent_set_invoke;
  ot->exec = armature_parent_set_exec;
  ot->poll = ED_operator_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_enum(
      ot->srna, "type", prop_editarm_make_parent_types, 0, "Parent Type", "Type of parenting");
}

static const EnumPropertyItem prop_editarm_clear_parent_types[] = {
    {ARM_PAR_CLEAR, "CLEAR", 0, "Clear Parent", ""},
    {ARM_PAR_CLEAR_DISCONNECT, "DISCONNECT", 0, "Disconnect Bone", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static void editbone_clear_parent(EditBone *ebone, int mode)
{
  if (ebone->parent) {
    /* for nice selection */
    ebone->parent->flag &= ~BONE_TIPSEL;
  }

  if (mode == 1) {
    ebone->parent = nullptr;
  }
  ebone->flag &= ~BONE_CONNECTED;
}

static int armature_parent_clear_exec(bContext *C, wmOperator *op)
{
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const int val = RNA_enum_get(op->ptr, "type");

  CTX_DATA_BEGIN (C, EditBone *, ebone, selected_editable_bones) {
    editbone_clear_parent(ebone, val);
  }
  CTX_DATA_END;

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      scene, view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *ob = objects[ob_index];
    bArmature *arm = static_cast<bArmature *>(ob->data);
    bool changed = false;

    LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
      if (EBONE_EDITABLE(ebone)) {
        changed = true;
        break;
      }
    }

    if (!changed) {
      continue;
    }

    ED_armature_edit_sync_selection(arm->edbo);

    /* NOTE: notifier might evolve. */
    WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, ob);
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

static int armature_parent_clear_invoke(bContext *C,
                                        wmOperator * /*op*/,
                                        const wmEvent * /*event*/)
{
  /* False when no selected bones are connected to the active bone. */
  bool enable_disconnect = false;
  /* False when no selected bones are parented to the active bone. */
  bool enable_clear = false;
  {
    Object *ob = CTX_data_edit_object(C);
    bArmature *arm = static_cast<bArmature *>(ob->data);
    LISTBASE_FOREACH (EditBone *, ebone, arm->edbo) {
      if (!EBONE_EDITABLE(ebone) || !(ebone->flag & BONE_SELECTED)) {
        continue;
      }
      if (ebone->parent == nullptr) {
        continue;
      }
      enable_clear = true;

      if (ebone->flag & BONE_CONNECTED) {
        enable_disconnect = true;
        break;
      }
    }
  }

  uiPopupMenu *pup = UI_popup_menu_begin(
      C, CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, "Clear Parent"), ICON_NONE);
  uiLayout *layout = UI_popup_menu_layout(pup);

  uiLayout *row_clear = uiLayoutRow(layout, false);
  uiLayoutSetEnabled(row_clear, enable_clear);
  uiItemEnumO(row_clear, "ARMATURE_OT_parent_clear", nullptr, ICON_NONE, "type", ARM_PAR_CLEAR);

  uiLayout *row_disconnect = uiLayoutRow(layout, false);
  uiLayoutSetEnabled(row_disconnect, enable_disconnect);
  uiItemEnumO(row_disconnect,
              "ARMATURE_OT_parent_clear",
              nullptr,
              ICON_NONE,
              "type",
              ARM_PAR_CLEAR_DISCONNECT);

  UI_popup_menu_end(C, pup);

  return OPERATOR_INTERFACE;
}

void ARMATURE_OT_parent_clear(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Parent";
  ot->idname = "ARMATURE_OT_parent_clear";
  ot->description =
      "Remove the parent-child relationship between selected bones and their parents";

  /* api cbs */
  ot->invoke = armature_parent_clear_invoke;
  ot->ex = armature_parent_clear_exec;
  ot->poll = ED_operator_editarmature;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = RNA_def_enum(ot->srna,
                          "type",
                          prop_editarm_clear_parent_types,
                          0,
                          "Clear Type",
                          "What way to clear parenting");
}

/** \} */
