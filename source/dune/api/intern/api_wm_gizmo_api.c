#include <stdio.h>
#include <stdlib.h>

#include "lib_utildefines.h"

#include "dune_report.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "types_windowmanager.h"

#include "wm_api.h"

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME

#  include "dune_ctx.h"
#  include "ui.h"

#  include "ed_gizmo_lib.h"

static void api_gizmo_draw_preset_box(wmGizmo *gz, float matrix[16], int select_id)
{
  ed_gizmo_draw_preset_box(gz, (float(*)[4])matrix, select_id);
}

static void api_gizmo_draw_preset_arrow(wmGizmo *gz, float matrix[16], int axis, int select_id)
{
  ed_gizmo_draw_preset_arrow(gz, (float(*)[4])matrix, axis, select_id);
}

static void api_gizmo_draw_preset_circle(wmGizmo *gz, float matrix[16], int axis, int select_id)
{
  ed_gizmo_draw_preset_circle(gz, (float(*)[4])matrix, axis, select_id);
}

static void api_gizmo_draw_preset_facemap(
    wmGizmo *gz, struct Ctx *C, struct Object *ob, int facemap, int select_id)
{
  ed_gizmo_draw_preset_facemap(C, gz, ob, facemap, select_id);
}

/* -------------------------------------------------------------------- */
/** Gizmo Prop Define **/

static void api_gizmo_target_set_prop(wmGizmo *gz,
                                      ReportList *reports,
                                      const char *target_propname,
                                      ApiPtr *ptr,
                                      const char *propname,
                                      int index)
{
  const wmGizmoPropType *gz_prop_type = wm_gizmotype_target_property_find(gz->type,
                                                                              target_propname);
  if (gz_prop_type == NULL) {
    dune_reportf(reports,
                RPT_ERROR,
                "Gizmo target prop '%s.%s' not found",
                gz->type->idname,
                target_propname);
    return;
  }

  ApiProp *prop = api_struct_find_prop(ptr, propname);
  if (prop == NULL) {
    dune_reportf(reports,
                RPT_ERROR,
                "Property '%s.%s' not found",
                api_struct_id(ptr->type),
                propname);
    return;
  }

  if (gz_prop_type->data_type != api_prop_type(prop)) {
    const int gizmo_type_index = api_enum_from_value(api_enum_prop_type_items,
                                                     gz_prop_type->data_type);
    const int prop_type_index = api_enum_from_value(api_enum_prop_type_items,
                                                    api_prop_type(prop));
    lib_assert((gizmo_type_index != -1) && (prop_type_index == -1));

    dune_reportf(reports,
                RPT_ERROR,
                "Gizmo target '%s.%s' expects '%s', '%s.%s' is '%s'",
                gz->type->idname,
                target_propname,
                api_enum_prop_type_items[gizmo_type_index].id,
                api_struct_id(ptr->type),
                propname,
                api_enum_prop_type_items[prop_type_index].id);
    return;
  }

  if (api_prop_array_check(prop)) {
    if (index == -1) {
      const int prop_array_length = api_prop_array_length(ptr, prop);
      if (gz_prop_type->array_length != prop_array_length) {
        dune_reportf(reports,
                    RPT_ERROR,
                    "Gizmo target property '%s.%s' expects an array of length %d, found %d",
                    gz->type->idname,
                    target_propname,
                    gz_prop_type->array_length,
                    prop_array_length);
        return;
      }
    }
  }
  else {
    if (gz_prop_type->array_length != 1) {
      BKE_reportf(reports,
                  RPT_ERROR,
                  "Gizmo target property '%s.%s' expects an array of length %d",
                  gz->type->idname,
                  target_propname,
                  gz_prop_type->array_length);
      return;
    }
  }

  if (index >= gz_prop_type->array_length) {
    dune_reportf(reports,
                RPT_ERROR,
                "Gizmo target prop '%s.%s', index %d must be below %d",
                gz->type->idname,
                target_propname,
                index,
                gz_prop_type->array_length);
    return;
  }

  wm_gizmo_target_prop_def_api_ptr(gz, gz_prop_type, ptr, prop, index);
}

static ApiPtr api_gizmo_target_set_operator(wmGizmo *gz,
                                                ReportList *reports,
                                                const char *opname,
                                                int part_index)
{
  wmOpType *ot;

  ot = wm_optype_find(opname, false); /* print error next */
  if (!ot || !ot->sapi) {
    dune_reportf(
        reports, RPT_ERROR, "%s '%s'", ot ? "unknown operator" : "operator missing srna", opname);
    return PointerRNA_NULL;
  }

  /* For the return value to be usable, we need 'ApiPtr.data' to be set. */
  IdProp *props;
  {
    IdPropTemplate val = {0};
    props = IDP_New(IDP_GROUP, &val, "wmGizmoProps");
  }

  return *wm_gizmo_op_set(gz, part_index, ot, props);
}

/* -------------------------------------------------------------------- */
/** Gizmo Prop Access **/

static bool api_gizmo_target_is_valid(wmGizmo *gz,
                                      ReportList *reports,
                                      const char *target_propname)
{
  wmGizmoProp *gz_prop = wm_gizmo_target_prop_find(gz, target_propname);
  if (gz_prop == NULL) {
    dune_reportf(reports,
                RPT_ERROR,
                "Gizmo target prop '%s.%s' not found",
                gz->type->idname,
                target_propname);
    return false;
  }
  return wm_gizmo_target_prop_is_valid(gz_prop);
}

#else

void api_gizmo(ApiStruct *sapi)
{
  /* Utility draw functions, since we don't expose new OpenGL drawing wrappers via Python yet.
   * exactly how these should be exposed isn't totally clear.
   * However it's probably good to have some high level API's for this anyway.
   * Just note that this could be re-worked once tests are done.
   */

  ApiFn *func;
  ApiProp *parm;

  /* -------------------------------------------------------------------- */
  /* Primitive Shapes */

  /* draw_preset_box */
  fn = api_def_fn(srna, "draw_preset_box", "rna_gizmo_draw_preset_box");
  api_def_fn_ui_description(fn, "Draw a box");
  parm = api_def_prop(fn, "matrix", PROP_FLOAT, PROP_MATRIX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  RNA_def_property_multi_array(parm, 2, rna_matrix_dimsize_4x4);
  RNA_def_property_ui_text(parm, "", "The matrix to transform");
  RNA_def_int(func,
              "select_id",
              -1,
              -1,
              INT_MAX,
              "ID to use when gizmo is selectable.  Use -1 when not selecting",
              "",
              -1,
              INT_MAX);

  /* draw_preset_box */
  func = RNA_def_function(srna, "draw_preset_arrow", "rna_gizmo_draw_preset_arrow");
  RNA_def_function_ui_description(func, "Draw a box");
  parm = RNA_def_property(func, "matrix", PROP_FLOAT, PROP_MATRIX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_property_multi_array(parm, 2, rna_matrix_dimsize_4x4);
  RNA_def_property_ui_text(parm, "", "The matrix to transform");
  RNA_def_enum(func, "axis", rna_enum_object_axis_items, 2, "", "Arrow Orientation");
  RNA_def_int(func,
              "select_id",
              -1,
              -1,
              INT_MAX,
              "ID to use when gizmo is selectable.  Use -1 when not selecting",
              "",
              -1,
              INT_MAX);

  func = RNA_def_function(srna, "draw_preset_circle", "rna_gizmo_draw_preset_circle");
  RNA_def_function_ui_description(func, "Draw a box");
  parm = RNA_def_property(func, "matrix", PROP_FLOAT, PROP_MATRIX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_property_multi_array(parm, 2, rna_matrix_dimsize_4x4);
  RNA_def_property_ui_text(parm, "", "The matrix to transform");
  RNA_def_enum(func, "axis", rna_enum_object_axis_items, 2, "", "Arrow Orientation");
  RNA_def_int(func,
              "select_id",
              -1,
              -1,
              INT_MAX,
              "ID to use when gizmo is selectable.  Use -1 when not selecting",
              "",
              -1,
              INT_MAX);

  /* -------------------------------------------------------------------- */
  /* Other Shapes */

  /* draw_preset_facemap */
  func = RNA_def_function(srna, "draw_preset_facemap", "rna_gizmo_draw_preset_facemap");
  RNA_def_function_ui_description(func, "Draw the face-map of a mesh object");
  RNA_def_function_flag(func, FUNC_USE_CONTEXT);
  parm = RNA_def_pointer(func, "object", "Object", "", "Object");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_int(func, "face_map", 0, 0, INT_MAX, "Face map index", "", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_int(func,
              "select_id",
              -1,
              -1,
              INT_MAX,
              "ID to use when gizmo is selectable.  Use -1 when not selecting",
              "",
              -1,
              INT_MAX);

  /* -------------------------------------------------------------------- */
  /* Property API */

  /* Define Properties */
  /* NOTE: 'target_set_handler' is defined in `bpy_rna_gizmo.c`. */
  func = RNA_def_function(srna, "target_set_prop", "rna_gizmo_target_set_prop");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "");
  parm = RNA_def_string(func, "target", NULL, 0, "", "Target property");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* similar to UILayout.prop */
  parm = RNA_def_pointer(func, "data", "AnyType", "", "Data from which to take property");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  parm = RNA_def_string(func, "property", NULL, 0, "", "Identifier of property in data");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_int(func, "index", -1, -1, INT_MAX, "", "", -1, INT_MAX); /* RNA_NO_INDEX == -1 */

  func = RNA_def_function(srna, "target_set_operator", "rna_gizmo_target_set_operator");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func,
                                  "Operator to run when activating the gizmo "
                                  "(overrides property targets)");
  parm = RNA_def_string(func, "operator", NULL, 0, "", "Target operator");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_int(func, "index", 0, 0, 255, "Part index", "", 0, 255);

  /* similar to UILayout.operator */
  parm = RNA_def_pointer(
      func, "properties", "OperatorProperties", "", "Operator properties to fill in");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_function_return(func, parm);

  /* Access Properties */
  /* NOTE: 'target_get', 'target_set' is defined in `bpy_rna_gizmo.c`. */
  func = RNA_def_function(srna, "target_is_valid", "rna_gizmo_target_is_valid");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_string(func, "property", NULL, 0, "", "Property identifier");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_function_ui_description(func, "");
  parm = RNA_def_boolean(func, "result", 0, "", "");
  RNA_def_function_return(func, parm);
}

void RNA_api_gizmogroup(StructRNA *UNUSED(srna))
{
  /* nothing yet */
}

#endif
