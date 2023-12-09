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
  WM_operatortype_append(ARMATURE_OT_click_extrude);
  WM_operatortype_append(ARMATURE_OT_fill);
  WM_optype_append(ARMATURE_OT_separate);
  WM_optype_append(ARMATURE_OT_split);

  WM_operatortype_append(ARMATURE_OT_autoside_names);
  WM_operatortype_append(ARMATURE_OT_flip_names);

  WM_operatortype_append(ARMATURE_OT_collection_add);
  WM_operatortype_append(ARMATURE_OT_collection_remove);
  WM_operatortype_append(ARMATURE_OT_collection_move);
  WM_operatortype_append(ARMATURE_OT_collection_assign);
  WM_operatortype_append(ARMATURE_OT_collection_unassign);
  WM_operatortype_append(ARMATURE_OT_collection_unassign_named);
  WM_operatortype_append(ARMATURE_OT_collection_select);
  WM_operatortype_append(ARMATURE_OT_collection_deselect);

  WM_operatortype_append(ARMATURE_OT_move_to_collection);
  WM_operatortype_append(ARMATURE_OT_assign_to_collection);

  /* POSE */
  WM_operatortype_append(POSE_OT_hide);
  WM_operatortype_append(POSE_OT_reveal);

  WM_operatortype_append(POSE_OT_armature_apply);
  WM_operatortype_append(POSE_OT_visual_transform_apply);

  WM_operatortype_append(POSE_OT_rot_clear);
  WM_operatortype_append(POSE_OT_loc_clear);
  WM_operatortype_append(POSE_OT_scale_clear);
  WM_operatortype_append(POSE_OT_transforms_clear);
  WM_operatortype_append(POSE_OT_user_transforms_clear);

  WM_operatortype_append(POSE_OT_copy);
  WM_operatortype_append(POSE_OT_paste);

  WM_operatortype_append(POSE_OT_select_all);

  WM_operatortype_append(POSE_OT_select_parent);
  WM_operatortype_append(POSE_OT_select_hierarchy);
  WM_operatortype_append(POSE_OT_select_linked);
  WM_operatortype_append(POSE_OT_select_linked_pick);
  WM_operatortype_append(POSE_OT_select_constraint_target);
  WM_operatortype_append(POSE_OT_select_grouped);
  WM_operatortype_append(POSE_OT_select_mirror);

  WM_operatortype_append(POSE_OT_paths_calculate);
  WM_operatortype_append(POSE_OT_paths_update);
  WM_operatortype_append(POSE_OT_paths_clear);
  WM_operatortype_append(POSE_OT_paths_range_update);

  WM_operatortype_append(POSE_OT_autoside_names);
  WM_operatortype_append(POSE_OT_flip_names);

  WM_operatortype_append(POSE_OT_rotation_mode_set);

  WM_operatortype_append(POSE_OT_quaternions_flip);

  WM_operatortype_append(POSE_OT_propagate);

  /* POSELIB */
  WM_operatortype_append(POSELIB_OT_apply_pose_asset);
  WM_operatortype_append(POSELIB_OT_blend_pose_asset);

  /* POSE SLIDING */
  WM_operatortype_append(POSE_OT_push);
  WM_operatortype_append(POSE_OT_relax);
  WM_operatortype_append(POSE_OT_blend_with_rest);
  WM_operatortype_append(POSE_OT_breakdown);
  WM_operatortype_append(POSE_OT_blend_to_neighbors);
}

void ED_operatormacros_armature()
{
  wmOperatorType *ot;
  wmOperatorTypeMacro *otmacro;

  ot = WM_operatortype_append_macro(
      "ARMATURE_OT_duplicate_move",
      "Duplicate",
      "Make copies of the selected bones within the same armature and move them",
      OPTYPE_UNDO | OPTYPE_REGISTER);
  WM_operatortype_macro_define(ot, "ARMATURE_OT_duplicate");
  otmacro = WM_operatortype_macro_define(ot, "TRANSFORM_OT_translate");
  RNA_boolean_set(otmacro->ptr, "use_proportional_edit", false);

  ot = WM_operatortype_append_macro("ARMATURE_OT_extrude_move",
                                    "Extrude",
                                    "Create new bones from the selected joints and move them",
                                    OPTYPE_UNDO | OPTYPE_REGISTER);
  otmacro = WM_operatortype_macro_define(ot, "ARMATURE_OT_extrude");
  RNA_boolean_set(otmacro->ptr, "forked", false);
  otmacro = WM_operatortype_macro_define(ot, "TRANSFORM_OT_translate");
  RNA_boolean_set(otmacro->ptr, "use_proportional_edit", false);

  /* XXX would it be nicer to just be able to have standard extrude_move,
   * but set the forked property separate?
   * that would require fixing a properties bug #19733. */
  ot = WM_operatortype_append_macro("ARMATURE_OT_extrude_forked",
                                    "Extrude Forked",
                                    "Create new bones from the sel joints and move them",
                                    OPTYPE_UNDO | OPTYPE_REGISTER);
  otmacro = win_optype_macro_define(ot, "ARMATURE_OT_extrude");
  api_bool_set(otmacro->ptr, "forked", true);
  otmacro = win_optype_macro_define(ot, "TRANSFORM_OT_translate");
  api_bool_set(otmacro->ptr, "use_proportional_edit", false);
}

void ED_keymap_armature(wmKeyConfig *keyconf)
{
  wmKeyMap *keymap;

  /* Armature ------------------------ */
  /* only set in editmode armature, by space_view3d listener */
  keymap = WM_keymap_ensure(keyconf, "Armature", SPACE_EMPTY, RGN_TYPE_WINDOW);
  keymap->poll = ED_operator_editarmature;

  /* Pose ------------------------ */
  /* only set in posemode, by space_view3d listener */
  keymap = WM_keymap_ensure(keyconf, "Pose", SPACE_EMPTY, RGN_TYPE_WINDOW);
  keymap->poll = ED_operator_posemode;
}
