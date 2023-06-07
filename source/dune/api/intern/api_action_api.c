#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib_utildefines.h"

#include "api_define.h"

#include "types_action.h"

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME

#  include "dune_action.h"

#  include "types_anim.h"
#  include "types_curve.h"

static void api_Action_flip_with_pose(Action *act, ReportList *reports, Object *ob)
{
  if (ob->type != OB_ARMATURE) {
    dune_report(reports, RPT_ERROR, "Only armature objects are supported");
    return;
  }
  dune_action_flip_with_pose(act, ob);

  /* Only for redraw. */
  wm_main_add_notifier(NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);
}

#else

void api_action(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "flip_with_pose", "api_Action_flip_with_pose");
  api_def_fn_ui_description(fn, "Flip the action around the X axis using a pose");
  api_def_fn_flag(fn, FN_USE_REPORTS);

  parm = api_def_ptr(
      fn, "object", "Object", "", "The reference armature object to use when flipping");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
}

#endif
