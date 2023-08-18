#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib_utildefines.h"

#include "api_define.h"

#include "types_object.h"
#include "types_wm.h"
#include "types_workspace.h"

#include "api_enum_types.h" /* own include */

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME

#  include "dune_paint.h"

#  include "ed_screen.h"

static void api_WorkSpaceTool_setup(Id *id,
                                    ToolRef *tref,
                                    Cxt *C,
                                    const char *idname,
                                    /* Args for: 'bToolRef_Runtime'. */
                                    int cursor,
                                    const char *keymap,
                                    const char *gizmo_group,
                                    const char *data_block,
                                    const char *op_idname,
                                    int index,
                                    int options,
                                    const char *idname_fallback,
                                    const char *keymap_fallback)
{
  ToolRef_Runtime tref_rt = {0};

  tref_rt.cursor = cursor;
  STRNCPY(tref_rt.keymap, keymap);
  STRNCPY(tref_rt.gizmo_group, gizmo_group);
  STRNCPY(tref_rt.data_block, data_block);
  STRNCPY(tref_rt.op, op_idname);
  tref_rt.index = index;
  tref_rt.flag = options;

  /* While it's logical to assign both these values from setup,
   * it's useful to stored this in DNA for re-use, exceptional case: write to the 'tref'. */
  STRNCPY(tref->idname_fallback, idname_fallback);
  STRNCPY(tref_rt.keymap_fallback, keymap_fallback);

  wm_toolsystem_ref_set_from_runtime(C, (WorkSpace *)id, tref, &tref_rt, idname);
}

static void api_WorkSpaceTool_refresh_from_cxt(Id *id, ToolRef *tref, Main *main)
{
  wm_toolsystem_ref_sync_from_cxt(main, (WorkSpace *)id, tref);
}

static ApiPtr api_WorkSpaceTool_op_props(ToolRef *tref,
                                         ReportList *reports,
                                         const char *idname)
{
  wmOpType *ot = wm_optype_find(idname, true);

  if (ot != NULL) {
    ApiPtr ptr;
    wm_toolsystem_ref_props_ensure_from_op(tref, ot, &ptr);
    return ptr;
  } else {
    dune_reportf(reports, RPT_ERROR, "Op '%s' not found!", idname);
  }
  return ApiPtr_NULL;
}

static ApiPtr api_WorkSpaceTool_gizmo_group_props(ToolRef *tref,
                                                  ReportList *reports,
                                                  const char *idname)
{
  wmGizmoGroupType *gzgt = wm_gizmogrouptype_find(idname, false);
  if (gzgt != NULL) {
    ApiPtr ptr;
    wm_toolsystem_ref_props_ensure_from_gizmo_group(tref, gzgt, &ptr);
    return ptr;
  } else {
    dune_reportf(reports, RPT_ERROR, "Gizmo group '%s' not found!", idname);
  }
  return ApiPtr_NULL;
}

#else

void api_workspace(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "status_text_set_internal", "ed_workspace_status_text");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  api_def_fn_ui_description(
      fn, "Set the status bar text, typically key shortcuts for modal operators");
  parm = api_def_string(
      fn, "text", NULL, 0, "Text", "New string for the status bar, None clears the text");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_prop_clear_flag(parm, PROP_NEVER_NULL);
}

void api_workspace_tool(ApiStruct *sapi)
{
  ApiProp *parm;
  ApiFn *fn;

  static EnumPropItem options_items[] = {
      {TOOLREF_FLAG_FALLBACK_KEYMAP, "KEYMAP_FALLBACK", 0, "Fallback", ""},
      {0, NULL, 0, NULL, NULL},
  };

  fn = api_def_fn(sapi, "setup", "api_WorkSpaceTool_setup");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_CXT);
  api_def_fn_ui_description(fn, "Set the tool settings");

  parm = api_def_string(fn, "idname", NULL, MAX_NAME, "Id", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* 'ToolRef_Runtime' */
  parm = api_def_prop(fn, "cursor", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(parm, api_enum_window_cursor_items);
  api_def_string(fn, "keymap", NULL, KMAP_MAX_NAME, "Key Map", "");
  api_def_string(fn, "gizmo_group", NULL, MAX_NAME, "Gizmo Group", "");
  api_def_string(fn, "data_block", NULL, MAX_NAME, "Data Block", "");
  api_def_string(fn, "op", NULL, MAX_NAME, "Op", "");
  api_def_int(fn, "index", 0, INT_MIN, INT_MAX, "Index", "", INT_MIN, INT_MAX);
  api_def_enum_flag(fn, "options", options_items, 0, "Tool Options", "");

  api_def_string(fn, "idname_fallback", NULL, MAX_NAME, "Fallback Id", "");
  api_def_string(fn, "keymap_fallback", NULL, KMAP_MAX_NAME, "Fallback Key Map", "");

  /* Access tool operator options (optionally create). */
  fn = api_def_fn(sapi, "op_props", "api_WorkSpaceTool_op_props");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_string(fn, "operator", NULL, 0, "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return */
  parm = RNA_def_pointer(func, "result", "OperatorProperties", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_RNAPTR);
  RNA_def_function_return(func, parm);

  /* Access gizmo-group options (optionally create). */
  func = RNA_def_function(
      srna, "gizmo_group_properties", "rna_WorkSpaceTool_gizmo_group_properties");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_string(func, "group", NULL, 0, "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return */
  parm = RNA_def_pointer(func, "result", "GizmoGroupProperties", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_RNAPTR);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "refresh_from_context", "rna_WorkSpaceTool_refresh_from_context");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_USE_MAIN);
}

#endif
