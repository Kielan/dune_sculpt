#include "KERNEL_action.hh"

#include "LIB_listbase.h"
#include "LIB_string.h"

#include "_action_types.h"
#include "_anim_types.h"
#include "_armature_types.h"

#include "MEM_guardedalloc.h"

void KERNEL_action_find_fcurves_with_bones(const bAction *action, FoundFCurveCallback callback)
{
  LISTBASE_FOREACH (FCurve *, fcu, &action->curves) {
    char bone_name[MAXBONENAME];
    if (!LIB_str_quoted_substr(fcu->rna_path, "pose.bones[", bone_name, sizeof(bone_name))) {
      continue;
    }
    callback(fcu, bone_name);
  }
}
