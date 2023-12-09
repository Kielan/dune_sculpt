#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_math_rotation.h"
#include "lib_math_vector.h"

#include "types_anim.h"
#include "types_armature.h"
#include "types_ob.h"
#include "types_scene.h"

#include "dune_action.h"
#include "dune_anim_data.h"
#include "dune_idprop.h"
#include "dune_layer.h"
#include "dune_ob.hh"

#include "dune_cxt.hh"

#include "graph.hh"

#include "api_access.hh"
#include "api_path.hh"
#include "api_prototypes.h"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_keyframing.hh"

#include "anim_keyframing.hh"

#include "armature_intern.h"

/* Contents of this File:
 * This file contains methods shared between Pose Slide and Pose Lib;
 * primarily the fns in question concern Anim <-> Pose
 * convenience fns, such as applying/getting pose vals
 * and/or inserting keyframes for these */

/* FCurves <-> PoseChannels Links */
/* helper for poseAnim_mapping_get() -> get the relevant F-Curves per PoseChannel */
static void fcurves_to_pchan_links_get(List *pfLinks,
                                       Ob *ob,
                                       Action *act,
                                       PoseChannel *pchan)
{
  List curves = {nullptr, nullptr};
  const eActionTransformFlags transFlags = dune_action_get_item_transform_flags(
      act, ob, pchan, &curves);

  pchan->flag &= ~(POSE_LOC | POSE_ROT | POSE_SIZE | POSE_BBONE_SHAPE);

  /* check if any transforms found... */
  if (transFlags) {
    /* make new linkage data */
    tPChanFCurveLink *pfl = static_cast<tPChanFCurveLink *>(
        mem_calloc(sizeof(tPChanFCurveLink), "tPChanFCurveLink"));

    pfl->ob = ob;
    pfl->fcurves = curves;
    pfl->pchan = pchan;

    /* get the api path to this pchan this needs to be freed! */
    ApiPtr ptr = api_ptr_create((Id *)ob, &ApiPoseBone, pchan);
    pfl->pchan_path = api_path_from_id_to_struct(&ptr);

    /* add linkage data to op data */
    lib_addtail(pfLinks, pfl);

    /* set pchan's transform flags */
    if (transFlags & ACT_TRANS_LOC) {
      pchan->flag |= POSE_LOC;
    }
    if (transFlags & ACT_TRANS_ROT) {
      pchan->flag |= POSE_ROT;
    }
    if (transFlags & ACT_TRANS_SCALE) {
      pchan->flag |= POSE_SIZE;
    }
    if (transFlags & ACT_TRANS_BBONE) {
      pchan->flag |= POSE_BBONE_SHAPE;
    }

    /* store current transforms */
    copy_v3_v3(pfl->oldloc, pchan->loc);
    copy_v3_v3(pfl->oldrot, pchan->eul);
    copy_v3_v3(pfl->oldscale, pchan->size);
    copy_qt_qt(pfl->oldquat, pchan->quat);
    copy_v3_v3(pfl->oldaxis, pchan->rotAxis);
    pfl->oldangle = pchan->rotAngle;

    /* store current bbone vals */
    pfl->roll1 = pchan->roll1;
    pfl->roll2 = pchan->roll2;
    pfl->curve_in_x = pchan->curve_in_x;
    pfl->curve_in_z = pchan->curve_in_z;
    pfl->curve_out_x = pchan->curve_out_x;
    pfl->curve_out_z = pchan->curve_out_z;
    pfl->ease1 = pchan->ease1;
    pfl->ease2 = pchan->ease2;

    copy_v3_v3(pfl->scale_in, pchan->scale_in);
    copy_v3_v3(pfl->scale_out, pchan->scale_out);

    /* make copy of custom props */
    if (pchan->prop && (transFlags & ACT_TRANS_PROP)) {
      pfl->oldprops = IDPCopyProp(pchan->prop);
    }
  }
}

Ob *poseAnim_ob_get(Ob *ob_)
{
  Ob *ob = dune_ob_pose_armature_get(ob_);
  if (!ELEM(nullptr, ob, ob->data, ob->adt, ob->adt->action)) {
    return ob;
  }
  return nullptr;
}

void poseAnim_mapping_get(Cxt *C, List *pfLinks)
{
  /* for each Pose-Channel which gets affected, get the F-Curves for that channel
   * and set the relevant transform flags... */
  Ob *prev_ob, *ob_pose_armature;

  prev_ob = nullptr;
  ob_pose_armature = nullptr;
  CXT_DATA_BEGIN_WITH_ID (C, PoseChannel *, pchan, sel_pose_bones, Ob *, ob) {
    if (ob != prev_ob) {
      prev_ob = ob;
      ob_pose_armature = poseAnim_ob_get(ob);
    }

    if (ob_pose_armature == nullptr) {
      continue;
    }

    fcurves_to_pchan_links_get(pfLinks, ob_pose_armature, ob_pose_armature->adt->action, pchan);
  }
  CXT_DATA_END;

  /* if no PoseChannels were found, try a second pass, doing visible ones instead
   * i.e. if nothing sel, do whole pose */
  if (lib_list_is_empty(pfLinks)) {
    prev_ob = nullptr;
    ob_pose_armature = nullptr;
    CXT_DATA_BEGIN_WITH_ID (C, PoseChannel *, pchan, visible_pose_bones, Ob *, ob) {
      if (ob != prev_ob) {
        prev_ob = ob;
        ob_pose_armature = poseAnim_ob_get(ob);
      }

      if (ob_pose_armature == nullptr) {
        continue;
      }

      fcurves_to_pchan_links_get(pfLinks, ob_pose_armature, ob_pose_armature->adt->action, pchan);
    }
    CXT_DATA_END;
  }
}

void poseAnim_mapping_free(List *pfLinks)
{
  tPChanFCurveLink *pfl, *pfln = nullptr;

  /* free the tmp pchan links and their data */
  for (pfl = static_cast<tPChanFCurveLink *>(pfLinks->first); pfl; pfl = pfln) {
    pfln = pfl->next;

    /* free custom props */
    if (pfl->oldprops) {
      IDP_FreeProp(pfl->oldprops);
    }

    /* free list of F-Curve ref links */
    lib_freelist(&pfl->fcurves);

    /* free pchan api Path */
    mem_free(pfl->pchan_path);

    /* free link itself */
    lib_freelink(pfLinks, pfl);
  }
}

void poseAnim_mapping_refresh(Cxt *C, Scene * /*scene*/, Ob *ob)
{
  graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
  win_ev_add_notifier(C, NC_OB | ND_POSE, ob);

  AnimData *adt = dune_animdata_from_id(&ob->id);
  if (adt && adt->action) {
    graph_id_tag_update(&adt->action->id, ID_RECALC_ANIM_NO_FLUSH);
  }
}

void poseAnim_mapping_reset(List *pfLinks)
{
  /* iter over each pose-channel affected, restoring all channels to their original vals */
  LIST_FOREACH (tPChanFCurveLink *, pfl, pfLinks) {
    PoseChannel *pchan = pfl->pchan;

    /* just copy all the vals over regardless of whether they changed or not */
    copy_v3_v3(pchan->loc, pfl->oldloc);
    copy_v3_v3(pchan->eul, pfl->oldrot);
    copy_v3_v3(pchan->size, pfl->oldscale);
    copy_qt_qt(pchan->quat, pfl->oldquat);
    copy_v3_v3(pchan->rotAxis, pfl->oldaxis);
    pchan->rotAngle = pfl->oldangle;

    /* store current bbone vals */
    pchan->roll1 = pfl->roll1;
    pchan->roll2 = pfl->roll2;
    pchan->curve_in_x = pfl->curve_in_x;
    pchan->curve_in_z = pfl->curve_in_z;
    pchan->curve_out_x = pfl->curve_out_x;
    pchan->curve_out_z = pfl->curve_out_z;
    pchan->ease1 = pfl->ease1;
    pchan->ease2 = pfl->ease2;

    copy_v3_v3(pchan->scale_in, pfl->scale_in);
    copy_v3_v3(pchan->scale_out, pfl->scale_out);

    /* just overwrite vals of props from the stored copies (there should be some) */
    if (pfl->oldprops) {
      IDP_SyncGroupVals(pfl->pchan->prop, pfl->oldprops);
    }
  }
}

void poseAnim_mapping_autoKeyframe(Cxt *C, Scene *scene, List *pfLinks, float cframe)
{
  ViewLayer *view_layer = cxt_data_view_layer(C);
  View3D *v3d = cxt_wm_view3d(C);
  bool skip = true;

  FOREACH_OB_IN_MODE_BEGIN (scene, view_layer, v3d, OB_ARMATURE, OB_MODE_POSE, ob) {
    ob->id.tag &= ~LIB_TAG_DOIT;
    ob = poseAnim_ob_get(ob);

    /* Ensure validity of the settings from the cxt. */
    if (ob == nullptr) {
      continue;
    }

    if (dune::animrig::autokeyframe_cfra_can_key(scene, &ob->id)) {
      ob->id.tag |= LIB_TAG_DOIT;
      skip = false;
    }
  }
  FOREACH_OB_IN_MODE_END;

  if (skip) {
    return;
  }

  /* Insert keyframes as necessary if auto-key-framing. */
  KeyingSet *ks = anim_get_keyingset_for_autokeying(scene, ANIM_KS_WHOLE_CHAR_ID);
  dune::Vector<ApiPtr> sources;

  /* iter over each pose-channel affected, tagging bones to be keyed */
  /* Here we alrdy have the info about what transforms exist, though
   * it might be easier to just overwrite all using normal mechanisms  */
  LIST_FOREACH (tPChanFCurveLink *, pfl, pfLinks) {
    PoseChannel *pchan = pfl->pchan;

    if ((pfl->ob->id.tag & LIB_TAG_DOIT) == 0) {
      continue;
    }

    /* Add data-src override for the PoseChannel, to be used later. */
    anim_relative_keyingset_add_source(sources, &pfl->ob->id, &ApiPoseBone, pchan);
  }

  /* insert keyframes for all relevant bones in one go */
  anim_apply_keyingset(C, &sources, ks, MODIFYKEY_MODE_INSERT, cframe);

  /* do the bone paths
   * - only do this if keyframes should have been added
   * - do not calc unless there are paths alrdy to update..l */
  FOREACH_OB_IN_MODE_BEGIN (scene, view_layer, v3d, OB_ARMATURE, OB_MODE_POSE, ob) {
    if (ob->id.tag & LIB_TAG_DOIT) {
      if (ob->pose->avs.path_bakeflag & MOTIONPATH_BAKE_HAS_PATHS) {
        // ed_pose_clear_paths(C, ob); /* for nowno need to clear. */
        /* TODO: Should ensure we can use more narrow update range here. */
        ed_pose_recalc_paths(C, scene, ob, POSE_PATH_CALC_RANGE_FULL);
      }
    }
  }
  FOREACH_OB_IN_MODE_END;
}

LinkData *poseAnim_mapping_getNextFCurve(List *fcuLinks, LinkData *prev, const char *path)
{
  LinkData *first = static_cast<LinkData *>((prev)     ? prev->next :
                                            (fcuLinks) ? fcuLinks->first :
                                                         nullptr);
  LinkData *ld;

  /* check each link to see if the linked F-Curve has a matching path */
  for (ld = first; ld; ld = ld->next) {
    FCurve *fcu = (FCurve *)ld->data;

    /* check if paths match */
    if (STREQ(path, fcu->api_path)) {
      return ld;
    }
  }

  /* none found */
  return nullptr;
}
