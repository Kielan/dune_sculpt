#include "KERNEL_action.hh"
#include "KERNEL_animsys.h"
#include "KERNEL_armature.hh"

#include "LIB_function_ref.hh"
#include "LIB_set.hh"

#include "structs_action_types.h"
#include "structs_anim_types.h"
#include "structs_armature_types.h"
#include "structs_object_types.h"

#include "API_access.h"

using ActionApplier =
    dune_FunctionRef<void(PointerAPI *, duneAction *, const AnimationEvalContext *)>;

/* Forward declarations. */
void pose_apply_disable_fcurves_for_unselected_bones(duneAction *action,
                                                     const BoneNameSet &selected_bone_names);
void pose_apply_restore_fcurves(duneAction *action);

void pose_apply(struct Object *ob,
                struct duneAction *action,
                struct AnimationEvalContext *anim_eval_context,
                ActionApplier applier);

}  // namespace

void KERNEL_pose_apply_action_selected_bones(struct Object *ob,
                                          struct duneAction *action,
                                          struct AnimationEvalContext *anim_eval_context)
{
  auto evaluate_and_apply =
      [](PointerAPI *ptr, duneAction *act, const AnimationEvalContext *anim_eval_context) {
        animsys_evaluate_action(ptr, act, anim_eval_context, false);
      };

  pose_apply(ob, action, anim_eval_context, evaluate_and_apply);
}

void KERNEL_pose_apply_action_all_bones(struct Object *ob,
                                     struct duneAction *action,
                                     struct AnimationEvalContext *anim_eval_context)
{
  PointerAPI pose_owner_ptr;
  API_id_pointer_create(&ob->id, &pose_owner_ptr);
  animsys_evaluate_action(&pose_owner_ptr, action, anim_eval_context, false);
}

void KERNEL_pose_apply_action_dune(struct Object *ob,
                                 struct duneAction *action,
                                 struct AnimationEvalContext *anim_eval_context,
                                 const float dune_factor)
{
  auto evaluate_and_dune = [dune_factor](PointerAPI *ptr,
                                           duneAction *act,
                                           const AnimationEvalContext *anim_eval_context) {
    animsys_dune_in_action(ptr, act, anim_eval_context, dune_factor);
  };

  pose_apply(ob, action, anim_eval_context, evaluate_and_blend);
}

namespace {
void pose_apply(struct Object *ob,
                struct duneAction *action,
                struct AnimationEvalContext *anim_eval_context,
                ActionApplier applier)
{
  bPose *pose = ob->pose;
  if (pose == nullptr) {
    return;
  }

  const duneArmature *armature = (duneArmature *)ob->data;
  const BoneNameSet selected_bone_names = KERNEL_armature_find_selected_bone_names(armature);
  const bool limit_to_selected_bones = !selected_bone_names.is_empty();

  if (limit_to_selected_bones) {
    /* Mute all FCurves that are not associated with selected bones. This separates the concept of
     * bone selection from the FCurve evaluation code. */
    pose_apply_disable_fcurves_for_unselected_bones(action, selected_bone_names);
  }

  /* Apply the Action. */
  PointerAPI pose_owner_ptr;
  API_id_pointer_create(&ob->id, &pose_owner_ptr);

  applier(&pose_owner_ptr, action, anim_eval_context);

  if (limit_to_selected_bones) {
    pose_apply_restore_fcurves(action);
  }
}

void pose_apply_restore_fcurves(duneAction *action)
{
  /* TODO(Sybren): Restore the FCurve flags, instead of just erasing the 'disabled' flag. */
  LISTBASE_FOREACH (FCurve *, fcu, &action->curves) {
    fcu->flag &= ~FCURVE_DISABLED;
  }
}

void pose_apply_disable_fcurves_for_unselected_bones(duneAction *action,
                                                     const BoneNameSet &selected_bone_names)
{
  auto disable_unselected_fcurve = [&](FCurve *fcu, const char *bone_name) {
    const bool is_bone_selected = selected_bone_names.contains(bone_name);
    if (!is_bone_selected) {
      fcu->flag |= FCURVE_DISABLED;
    }
  };
  KERNEL_action_find_fcurves_with_bones(action, disable_unselected_fcurve);
}
