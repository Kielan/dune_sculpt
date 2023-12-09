#include "api_access.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_armature.hh"
#include "ed_screen.hh"

#include "armature_intern.h"

void ed_optypes_armature()
{
  /* Both ops `ARMATURE_OT_*` and `POSE_OT_*` are registered here. */
  /* EDIT ARMATURE */
  win_optype_append(ARMATURE_OT_bone_primitive_add);

  win_optype_append(ARMATURE_OT_align);
  win_optype_append(ARMATURE_OT_calc_roll);
  win_optype_append(ARMATURE_OT_roll_clear);
  win_optype_append(ARMATURE_OT_switch_direction);
  win_optype_append(ARMATURE_OT_subdivide);

  win_optype_append(ARMATURE_OT_parent_set);
  win_optype_append(ARMATURE_OT_parent_clear);

  win_optype_append(ARMATURE_OT_sel_all);
  win_optype_append(ARMATURE_OT_sel_mirror);
  win_optype_append(ARMATURE_OT_sel_more);
  win_optype_append(ARMATURE_OT_sel_less);
  win_optype_append(ARMATURE_OT_sel_hierarchy);
  win_optype_append(ARMATURE_OT_sel_linked);
  win_optype_append(ARMATURE_OT_sel_linked_pick);
  win_optype_append(ARMATURE_OT_sel_similar);
  win_optype_append(ARMATURE_OT_shortest_path_pick);

  win_optype_append(ARMATURE_OT_del);
  win_optype_append(ARMATURE_OT_dissolve);
  win_optype_append(ARMATURE_OT_dup);
  win_optype_append(ARMATURE_OT_symmetrize);
  win_optype_append(ARMATURE_OT_extrude);
  win_optype_append(ARMATURE_OT_hide);
  win_optype_append(ARMATURE_OT_reveal);
  win_optype_append(ARMATURE_OT_click_extrude);
  win_optype_append(ARMATURE_OT_fill);
  win_optype_append(ARMATURE_OT_separate);
  win_optype_append(ARMATURE_OT_split);

  win_optype_append(ARMATURE_OT_autoside_names);
  win_optype_append(ARMATURE_OT_flip_names);

  win_optype_append(ARMATURE_OT_collection_add);
  win_optype_append(ARMATURE_OT_collection_remove);
  win_optype_append(ARMATURE_OT_collection_move);
  win_optype_append(ARMATURE_OT_collection_assign);
  win_optype_append(ARMATURE_OT_collection_unassign);
  win_optype_append(ARMATURE_OT_collection_unassign_named);
  win_optype_append(ARMATURE_OT_collection_sel);
  win_optype_append(ARMATURE_OT_collection_desel);

  win_optype_append(ARMATURE_OT_move_to_collection);
  win_optype_append(ARMATURE_OT_assign_to_collection);

  /* POSE */
  win_optype_append(POSE_OT_hide);
  win_optype_append(POSE_OT_reveal);

  win_optype_append(POSE_OT_armature_apply);
  win_optype_append(POSE_OT_visual_transform_apply);

  win_optype_append(POSE_OT_rot_clear);
  win_optype_append(POSE_OT_loc_clear);
  win_optype_append(POSE_OT_scale_clear);
  win_optype_append(POSE_OT_transforms_clear);
  win_optype_append(POSE_OT_user_transforms_clear);

  win_optype_append(POSE_OT_copy);
  win_optype_append(POSE_OT_paste);

  win_optype_append(POSE_OT_sel_all);

  win_optype_append(POSE_OT_sel_parent);
  win_optype_append(POSE_OT_sel_hierarchy);
  win_optype_append(POSE_OT_sel_linked);
  win_optype_append(POSE_OT_sel_linked_pick);
  win_optype_append(POSE_OT_sel_constraint_target);
  win_optype_append(POSE_OT_sel_grouped);
  win_optype_append(POSE_OT_sel_mirror);

  win_optype_append(POSE_OT_paths_calc);
  win_optype_append(POSE_OT_paths_update);
  win_optype_append(POSE_OT_paths_clear);
  win_optype_append(POSE_OT_paths_range_update);

  win_optype_append(POSE_OT_autoside_names);
  win_optype_append(POSE_OT_flip_names);

  win_optype_append(POSE_OT_rotation_mode_set);

  win_optype_append(POSE_OT_quaternions_flip);

  win_optype_append(POSE_OT_propagate);

  /* POSELIB */
  win_optype_append(POSELIB_OT_apply_pose_asset);
  win_optype_append(POSELIB_OT_blend_pose_asset);

  /* POSE SLIDING */
  win_optype_append(POSE_OT_push);
  win_optype_append(POSE_OT_relax);
  win_optype_append(POSE_OT_blend_with_rest);
  win_optype_append(POSE_OT_breakdown);
  win_optype_append(POSE_OT_blend_to_neighbors);
}

void ed_opmacros_armature()
{
  WinOpType *ot;
  WinOpTypeMacro *otmacro;

  ot = win_optype_append_macro(
      "ARMATURE_OT_dup_move",
      "Dup",
      "Make copies of the sel bones within the same armature and move them",
      OPTYPE_UNDO | OPTYPE_REGISTER);
  win_optype_macro_define(ot, "ARMATURE_OT_dup");
  otmacro = win_optype_macro_define(ot, "TRANSFORM_OT_translate");
  api_bool_set(otmacro->ptr, "use_proportional_edit", false);

  ot = win_optype_append_macro("ARMATURE_OT_extrude_move",
                                    "Extrude",
                                    "Create new bones from the selected joints and move them",
                                    OPTYPE_UNDO | OPTYPE_REGISTER);
  otmacro = win_optype_macro_define(ot, "ARMATURE_OT_extrude");
  api_bool_set(otmacro->ptr, "forked", false);
  otmacro = win_optype_macro_define(ot, "TRANSFORM_OT_translate");
  api_bool_set(otmacro->ptr, "use_proportional_edit", false);

  /* would it be nicer to just be able to have standard extrude_move,
   * but set the forked prop separate?
   * that would require fixing a props bug #19733. */
  ot = win_optype_append_macro("ARMATURE_OT_extrude_forked",
                               "Extrude Forked",
                               "Create new bones from the sel joints and move them",
                               OPTYPE_UNDO | OPTYPE_REGISTER);
  otmacro = win_optype_macro_define(ot, "ARMATURE_OT_extrude");
  api_bool_set(otmacro->ptr, "forked", true);
  otmacro = win_optype_macro_define(ot, "TRANSFORM_OT_translate");
  api_bool_set(otmacro->ptr, "use_proportional_edit", false);
}

void ed_keymap_armature(WinKeyConfig *keyconf)
{
  WinKeyMap *keymap;

  /* Armature */
  /* only set in editmode armature, by space_view3d listener */
  keymap = win_keymap_ensure(keyconf, "Armature", SPACE_EMPTY, RGN_TYPE_WIN);
  keymap->poll = ed_op_editarmature;

  /* Pose */
  /* only set in posemode, by space_view3d listener */
  keymap = win_keymap_ensure(keyconf, "Pose", SPACE_EMPTY, RGN_TYPE_WIN);
  keymap->poll = ed_op_posemode;
}
