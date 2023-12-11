#include "MEM_guardedalloc.h"

#include "CLG_log.h"

#include "types_armature.h"
#include "types_layer.h"
#include "types_ob.h"
#include "types_scene.h"

#include "lib_array_utils.h"
#include "lib_list.h"
#include "lib_map.hh"
#include "lib_string.h"

#include "dune_armature.hh"
#include "dune_cxt.hh"
#include "dune_idprop.h"
#include "dune_layer.h"
#include "dune_main.hh"
#include "dune_ob.hh"
#include "dune_undo_sys.h"

#include "graph.hh"

#include "ed_armature.hh"
#include "ed_ob.hh"
#include "ed_undo.hh"
#include "ed_util.hh"

#include "anim_bone_collections.hh"

#include "win_api.hh"
#include "win_types.hh"

using namespace dune::animrig;

/* Only need this locally. */
static CLG_LogRef LOG = {"ed.undo.armature"};

/* Util fns */

/* Remaps edit-bone collection membership.
 *
 * This is intended to be used in combination with ed_armature_ebone_list_copy()
 * and anim_bonecoll_list_copy() to make a full dup of both edit
 * bones and collections together. */
static void remap_ebone_bone_collection_refs(
    List /* EditBone */ *edit_bones,
    const dune::Map<BoneCollection *, BoneCollection *> &bcoll_map)
{
  LIST_FOREACH (EditBone *, ebone, edit_bones) {
    LIST_FOREACH (BoneCollectionRef *, bcoll_ref, &ebone->bone_collections) {
      bcoll_ref->bcoll = bcoll_map.lookup(bcoll_ref->bcoll);
    }
  }
}

/* Undo Conversion */
struct UndoArmature {
  EditBone *act_edbone;
  char active_collection_name[MAX_NAME];
  List /* EditBone */ ebones;
  BoneCollection **collection_array;
  int collection_array_num;
  size_t undo_size;
};

static void undoarm_to_editarm(UndoArmature *uarm, Armature *arm)
{
  /* Copy edit bones. */
  ed_armature_ebone_list_free(arm->edbo, true);
  ed_armature_ebone_list_copy(arm->edbo, &uarm->ebones, true);

  /* Active bone. */
  if (uarm->act_edbone) {
    EditBone *ebone;
    ebone = uarm->act_edbone;
    arm->act_edbone = ebone->tmp.ebone;
  }
  else {
    arm->act_edbone = nullptr;
  }

  ed_armature_ebone_list_tmp_clear(arm->edbo);

  /* Copy bone collections. */
  anim_bonecoll_array_free(&arm->collection_array, &arm->collection_array_num, true);
  auto bcoll_map = anim_bonecoll_array_copy_no_membership(&arm->collection_array,
                                                          &arm->collection_array_num,
                                                          uarm->collection_array,
                                                          uarm->collection_array_num,
                                                          true);

  /* Always do a lookup-by-name and assignment. Even when the name of the active collection is
   * still the same, the order may have changed and thus the index needs to be updated. */
  BoneCollection *active_bcoll = anim_armature_bonecoll_get_by_name(arm,
                                                                    uarm->active_collection_name);
  anim_armature_bonecoll_active_set(arm, active_bcoll);

  remap_ebone_bone_collection_refs(arm->edbo, bcoll_map);

  anim_armature_runtime_refresh(arm);
}

static void *undoarm_from_editarm(UndoArmature *uarm, Armature *arm)
{
  lib_assert(lib_array_is_zeroed(uarm, 1));

  /* Copy edit bones. */
  ed_armature_ebone_list_copy(&uarm->ebones, arm->edbo, false);

  /* Active bone. */
  if (arm->act_edbone) {
    EditBone *ebone = arm->act_edbone;
    uarm->act_edbone = ebone->tmp.ebone;
  }

  ed_armature_ebone_list_tmp_clear(&uarm->ebones);

  /* Copy bone collections. */
  auto bcoll_map = anim_bonecoll_array_copy_no_membership(&uarm->collection_array,
                                                          &uarm->collection_array_num,
                                                          arm->collection_array,
                                                          arm->collection_array_num,
                                                          false);
  STRNCPY(uarm->active_collection_name, arm->active_collection_name);

  /* Point the new edit bones at the new collections. */
  remap_ebone_bone_collection_references(&uarm->ebones, bcoll_map);

  /* Undo size.
   * TODO: include size of Id-props. */
  uarm->undo_size = 0;
  LIST_FOREACH (EditBone *, ebone, &uarm->ebones) {
    uarm->undo_size += sizeof(EditBone);
    uarm->undo_size += sizeof(BoneCollectionRef) *
                       lib_list_count(&ebone->bone_collections);
  }
  /* Size of the bone collections + the size of the ptrs to those
   * bone collections in the bone collection array. */
  uarm->undo_size += (sizeof(BoneCollection) + sizeof(BoneCollection *)) *
                     uarm->collection_array_num;

  return uarm;
}

static void undoarm_free_data(UndoArmature *uarm)
{
  ed_armature_ebone_list_free(&uarm->ebones, false);
  anim_bonecoll_array_free(&uarm->collection_array, &uarm->collection_array_num, false);
}

static Ob *editarm_ob_from_cxt(Cxt *C)
{
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  dune_view_layer_synced_ensure(scene, view_layer);
  Ob *obedit = dune_view_layer_edit_ob_get(view_layer);
  if (obedit && obedit->type == OB_ARMATURE) {
    Armature *arm = static_cast<Armature *>(obedit->data);
    if (arm->edbo != nullptr) {
      return obedit;
    }
  }
  return nullptr;
}

/* Implements ed Undo Sys
 * This is similar for all edit-mode types. */
struct ArmatureUndoStepElem {
  ArmatureUndoStepElem *next, *prev;
  UndoRefIdOb obedit_ref;
  UndoArmature data;
};

struct ArmatureUndoStep {
  UndoStep step;
  /* See ed_undo_ob_editmode_validate_scene_from_windows code comment for details. */
  UndoRefIdScene scene_ref;
  ArmatureUndoStepElem *elems;
  uint elems_len;
};

static bool armature_undosys_poll(Cxt *C)
{
  return editarm_ob_from_cxt(C) != nullptr;
}

static bool armature_undosys_step_encode(Cxt *C, Main *main, UndoStep *us_p)
{
  ArmatureUndoStep *us = (ArmatureUndoStep *)us_p;

  /* Important not to use the 3D view when getting objects because all obs
   * outside of this list will be moved out of edit-mode when reading back undo steps. */
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  uint obs_len = 0;
  Ob **obs = ed_undo_editmode_obs_from_view_layer(scene, view_layer, &obs_len);

  us->scene_ref.ptr = scene;
  us->elems = static_cast<ArmatureUndoStepElem *>(
      mem_calloc(sizeof(*us->elems) * obs_len, __func__));
  us->elems_len = objects_len;

  for (uint i = 0; i < objs_len; i++) {
    Ob *ob = obs[i];
    ArmatureUndoStepElem *elem = &us->elems[i];

    elem->obedit_ref.ptr = ob;
    Armature *arm = static_cast<Armature *>(elem->obedit_ref.ptr->data);
    undoarm_from_editarm(&elem->data, arm);
    arm->needs_flush_to_id = 1;
    us->step.data_size += elem->data.undo_size;
  }
  mem_free(obs);

  main->is_memfile_undo_flush_needed = true;

  return true;
}

static void armature_undosys_step_decode(
    Cxt *C, Main *main, UndoStep *us_p, const eUndoStepDir /*dir*/, bool /*is_final*/)
{
  ArmatureUndoStep *us = (ArmatureUndoStep *)us_p;
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);

  ed_undo_ob_editmode_validate_scene_from_windows(
      cxt_win(C), us->scene_ref.ptr, &scene, &view_layer);
  ed_undo_ob_editmode_restore_helper(
      scene, view_layer, &us->elems[0].obedit_ref.ptr, us->elems_len, sizeof(*us->elems));

  lib_assert(dune_ob_is_in_editmode(us->elems[0].obedit_ref.ptr));

  for (uint i = 0; i < us->elems_len; i++) {
    ArmatureUndoStepElem *elem = &us->elems[i];
    Ob *obedit = elem->obedit_ref.ptr;
    Armature *arm = static_cast<Armature *>(obedit->data);
    if (arm->edbo == nullptr) {
      /* Should never fail, may not crash but can give odd behavior. */
      CLOG_ERROR(&LOG,
                 "name='%s', failed to enter edit-mode for object '%s', undo state invalid",
                 us_p->name,
                 obedit->id.name);
      continue;
    }
    undoarm_to_editarm(&elem->data, arm);
    arm->needs_flush_to_id = 1;
    graph_id_tag_update(&arm->id, ID_RECALC_GEOMETRY);
  }

  /* The 1st element is always active */
  ed_undo_ob_set_active_or_warn(
      scene, view_layer, us->elems[0].obedit_ref.ptr, us_p->name, &LOG);

  /* Check after setting active (unless undoing into another scene). */
  lib_assert(armature_undosys_poll(C) || (scene != cxt_data_scene(C)));

  main->is_memfile_undo_flush_needed = true;

  win_ev_add_notifier(C, NC_GEOM | ND_DATA, nullptr);
}

static void armature_undosys_step_free(UndoStep *us_p)
{
  ArmatureUndoStep *us = (ArmatureUndoStep *)us_p;

  for (uint i = 0; i < us->elems_len; i++) {
    ArmatureUndoStepElem *elem = &us->elems[i];
    undoarm_free_data(&elem->data);
  }
  mem_free(us->elems);
}

static void armature_undosys_foreach_id_ref(UndoStep *us_p,
                                            UndoTypeForEachIdRefFn foreach_id_ref_fn,
                                            void *user_data)
{
  ArmatureUndoStep *us = (ArmatureUndoStep *)us_p;

  foreach_id_ref_fn(user_data, ((UndoRefId *)&us->scene_ref));
  for (uint i = 0; i < us->elems_len; i++) {
    ArmatureUndoStepElem *elem = &us->elems[i];
    foreach_id_ref_fn(user_data, ((UndoRefId *)&elem->obedit_ref));
  }
}

void ed_armature_undosys_type(UndoType *ut)
{
  ut->name = "Edit Armature";
  ut->poll = armature_undosys_poll;
  ut->step_encode = armature_undosys_step_encode;
  ut->step_decode = armature_undosys_step_decode;
  ut->step_free = armature_undosys_step_free;

  ut->step_foreach_id_ref = armature_undosys_foreach_id_ref;

  ut->flags = UNDOTYPE_FLAG_NEED_CXT_FOR_ENCODE;

  ut->step_size = sizeof(ArmatureUndoStep);
}
