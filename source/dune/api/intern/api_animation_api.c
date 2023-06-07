#include <stdio.h>
#include <stdlib.h>

#include "api_define.h"
#include "api_enum_types.h"

#include "types_anim.h"
#include "types_object.h"
#include "types_scene.h"

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME

#  include "dune_context.h"
#  include "dune_nla.h"
#  include "dune_report.h"

#  include "ed_keyframing.h"

static void api_KeyingSet_cxt_refresh(KeyingSet *ks, Cxt *C, ReportList *reports)
{
  /* TODO: enable access to providing a list of overrides (dsources)? */
  const eModifyKey_Returns error = ANIM_validate_keyingset(C, NULL, ks);

  if (error != 0) {
    switch (error) {
      case MODIFYKEY_INVALID_CXT:
        dune_report(reports, RPT_ERROR, "Invalid context for keying set");
        break;

      case MODIFYKEY_MISSING_TYPEINFO:
        dune_report(
            reports, RPT_ERROR, "Incomplete built-in keying set, appears to be missing type info");
        break;
    }
  }
}

static float api_AnimData_nla_tweak_strip_time_to_scene(AnimData *adt, float frame, bool invert)
{
  return dune_nla_tweakedit_remap(adt, frame, invert ? NLATIME_CONVERT_UNMAP : NLATIME_CONVERT_MAP);
}

#else

void api_keyingset(ApiStruct *sapi)
{
  ApiFn *fn;
  // ApiProp *parm;

  /* validate relative Keying Set (used to ensure paths are ok for context) */
  fn = api_def_fn(sapi, "refresh", "api_KeyingSet_cxt_refresh");
  api_def_fn_ui_description(
      fn,
      "Refresh Keying Set to ensure that it is valid for the current context "
      "(call before each use of one)");
  api_def_fn_flag(fn, FN_USE_CXT | FN_USE_REPORTS);
}

void api_animdata(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  /* Convert between action time and scene time when tweaking a NLA strip. */
  fn = api_def_fn(
      sapi, "nla_tweak_strip_time_to_scene", "rna_AnimData_nla_tweak_strip_time_to_scene");
  api_def_fn_ui_description(fn,
                            "Convert a time value from the local time of the tweaked strip "
                            "to scene time, exactly as done by built-in key editing tools. "
                            "Returns the input time unchanged if not tweaking.");
  parm = api_def_float(
      fn, "frame", 0.0, MINAFRAME, MAXFRAME, "", "Input time", MINAFRAME, MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "invert", false, "Invert", "Convert scene time to action time");
  parm = api_def_float(
      fn, "result", 0.0, MINAFRAME, MAXFRAME, "", "Converted time", MINAFRAME, MAXFRAME);
  api_def_fn_return(fn, parm);
}

#endif
