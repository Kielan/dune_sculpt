#include "api_define.h"
#include "api_enum_types.h"
#include "api_types.h"

#include "dune_workspace.h"

#include "ed_render.h"

#include "render_engine.h"

#include "wm_api.h"
#include "wm_types.h"

#include "api_internal.h"

#include "types_workspace.h"

#ifdef API_RUNTIME

#  include "lib_list.h"

#  include "dune_global.h"

#  include "types_object.h"
#  include "types_screen.h"
#  include "types_space.h"

#  include "ed_asset.h"
#  include "ed_paint.h"

#  include "api_access.h"

#  include "wm_toolsystem.h"

static void api_window_update_all(Main *UNUSED(main),
                                  Scene *UNUSED(scene),
                                  ApiPtr *UNUSED(ptr))
{
  wm_main_add_notifier(NC_WINDOW, NULL);
}

void api_workspace_screens_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  WorkSpace *workspace = (WorkSpace *)ptr->owner_id;
  api_iter_list_begin(iter, &workspace->layouts, NULL);
}

static ApiPtr api_workspace_screens_item_get(CollectionPropIter *iter)
{
  WorkSpaceLayout *layout = api_iter_list_get(iter);
  Screen *screen = dune_workspace_layout_screen_get(layout);

  return api_ptr_inherit_refine(&iter->parent, &ApiScreen, screen);
}

/* workspace.owner_ids */

static wmOwnerId *api_WorkSpace_owner_ids_new(WorkSpace *workspace, const char *name)
{
  wmOwnerId *owner_id = mem_callocn(sizeof(*owner_id), __func__);
  lib_addtail(&workspace->owner_ids, owner_id);
  STRNCPY(owner_id->name, name);
  wm_main_add_notifier(NC_WINDOW, NULL);
  return owner_id;
}

static void api_WorkSpace_owner_ids_remove(WorkSpace *workspace,
                                           ReportList *reports,
                                           ApiPtr *wstag_ptr)
{
  wmOwnerId *owner_id = wstag_ptr->data;
  if (lib_remlink_safe(&workspace->owner_ids, owner_id) == false) {
    dune_reportf(reports,
                RPT_ERROR,
                "wmOwnerId '%s' not in workspace '%s'",
                owner_id->name,
                workspace->id.name + 2);
    return;
  }

  mem_freen(owner_id);
  API_PTR_INVALIDATE(wstag_ptr);

  wm_main_add_notifier(NC_WINDOW, NULL);
}

static void api_WorkSpace_owner_ids_clear(WorkSpace *workspace)
{
  lib_freelistn(&workspace->owner_ids);
  wm_main_add_notifier(NC_OBJECT | ND_MOD | NA_REMOVED, workspace);
}

static int api_WorkSpace_asset_library_get(ApiPtr *ptr)
{
  const WorkSpace *workspace = ptr->data;
  return ed_asset_lib_ref_to_enum_value(&workspace->asset_lib_ref);
}

static void api_WorkSpace_asset_lib_set(ApiPtr *ptr, int value)
{
  WorkSpace *workspace = ptr->data;
  workspace->asset_lib_ref = ed_asset_libr_ref_from_enum_value(value);
}

static ToolRef *api_WorkSpace_tools_from_tkey(WorkSpace *workspace,
                                               const ToolKey *tkey,
                                               bool create)
{
  if (create) {
    ToolRef *tref;
    wm_toolsystem_ref_ensure(workspace, tkey, &tref);
    return tref;
  }
  return wm_toolsystem_ref_find(workspace, tkey);
}

static ToolRef *api_WorkSpace_tools_from_space_view3d_mode(WorkSpace *workspace,
                                                            int mode,
                                                            bool create)
{
  return api_WorkSpace_tools_from_tkey(workspace,
                                       &(ToolKey){
                                           .space_type = SPACE_VIEW3D,
                                           .mode = mode,
                                       },
                                       create);
}

static ToolRef *api_WorkSpace_tools_from_space_image_mode(WorkSpace *workspace,
                                                          int mode,
                                                          bool create)
{
  return api_WorkSpace_tools_from_tkey(workspace,
                                       &(ToolKey){
                                           .space_type = SPACE_IMAGE,
                                           .mode = mode,
                                       },
                                       create);
}

static ToolRef *api_WorkSpace_tools_from_space_node(WorkSpace *workspace, bool create)
{
  return api_WorkSpace_tools_from_tkey(workspace,
                                       &(ToolKey){
                                           .space_type = SPACE_NODE,
                                           .mode = 0,
                                       },
                                       create);
}
static ToolRef *api_WorkSpace_tools_from_space_seq(WorkSpace *workspace,
                                                   int mode,
                                                   bool create)
{
  return api_WorkSpace_tools_from_tkey(workspace,
                                       &(ToolKey){
                                           .space_type = SPACE_SEQ,
                                           .mode = mode,
                                       },
                                       create);
}
const EnumPropItem *api_WorkSpace_tools_mode_itemf(Cxt *UNUSED(C),
                                                   ApiPtr *ptr,
                                                   ApiProp *UNUSED(prop),
                                                   bool *UNUSED(r_free))
{
  ToolRef *tref = ptr->data;
  switch (tref->space_type) {
    case SPACE_VIEW3D:
      return api_enum_cxt_mode_items;
    case SPACE_IMAGE:
      return api_enum_space_image_mode_all_items;
    case SPACE_SEQ:
      return api_enum_space_sequencer_view_type_items;
  }
  return ApiDummy_DEFAULT_items;
}

static bool api_WorkSpaceTool_use_paint_canvas_get(ApiPtr *ptr)
{
  ToolRef *tref = ptr->data;
  return ed_paint_tool_use_canvas(NULL, tref);
}

static int api_WorkSpaceTool_index_get(ApiPtr *ptr)
{
  ToolRef *tref = ptr->data;
  return (tref->runtime) ? tref->runtime->index : 0;
}

static bool api_WorkSpaceTool_has_datablock_get(ApiPtr *ptr)
{
  ToolRef *tref = ptr->data;
  return (tref->runtime) ? (tref->runtime->data_block[0] != '\0') : false;
}

static void api_WorkSpaceTool_widget_get(ApiPtr *ptr, char *value)
{
  ToolRef *tref = ptr->data;
  strcpy(value, tref->runtime ? tref->runtime->gizmo_group : "");
}

static int api_WorkSpaceTool_widget_length(ApiPtr *ptr)
{
  ToolRef *tref = ptr->data;
  return tref->runtime ? strlen(tref->runtime->gizmo_group) : 0;
}

#else /* API_RUNTIME */

static void api_def_workspace_owner(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "wmOwnerId", NULL);
  api_def_struct_stype(sapi, "wmOwnerId");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Work Space UI Tag", "");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "");
  api_def_struct_name_prop(sapi, prop);
}

static void api_def_workspace_owner_ids(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "wmOwnerIds");
  sapi = api_def_struct(dapi, "wmOwnerIds", NULL);
  api_def_struct_stype(sapi, "WorkSpace");
  api_def_struct_ui_text(sapi, "WorkSpace UI Tags", "");

  /* add owner_id */
  fn = api_def_fn(sapi, "new", "api_WorkSpace_owner_ids_new");
  api_def_fn_ui_description(fn, "Add ui tag");
  parm = api_def_string(fn, "name", "Name", 0, "", "New name for the tag");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "owner_id", "wmOwnerId", "", "");
  api_def_fn_return(fn, parm);

  /* remove owner_id */
  fn = api_def_fn(sapi, "remove", "api_WorkSpace_owner_ids_remove");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove ui tag");
  /* owner_id to remove */
  parm = api_def_ptr(fn, "owner_id", "wmOwnerId", "", "Tag to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  /* clear all modifiers */
  fn = api_def_fn(sapi, "clear", "api_WorkSpace_owner_ids_clear
  api_def_fn_ui_description(fn, "Remove all tags");
}

static void api_def_workspace_tool(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(sapi, "WorkSpaceTool", NULL);
  api_def_struct_stype(sapi, "bToolRef");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Work Space Tool", "");

  prop = api_def_prop(sapi, "idname", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Id", "");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "idname_fallback", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Id Fallback", "");

  prop = api_def_prop(sapi, "index", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Index", "");
  api_def_prop_int_fns(prop, "api_WorkSpaceTool_index_get", NULL, NULL);

  prop = api_def_prop(sapi, "space_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "space_type");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_enum_items(prop, api_enum_space_type_items);
  api_def_prop_ui_text(prop, "Space Type", "");

  prop = api_def_prop(sapi, "mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mode");
  api_def_prop_enum_items(prop, DummyRNA_DEFAULT_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_WorkSpace_tools_mode_itemf");
  api_def_prop_ui_text(prop, "Tool Mode", "");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "use_paint_canvas", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Index", "");
  api_def_prop_bool_fns(prop, "api_WorkSpaceTool_use_paint_canvas_get", NULL);
  api_def_prop_ui_text(prop, "Use Paint Canvas", "Does this tool use a painting canvas");

  api_define_verify_stype(0);
  prop = api_def_prop(sapi, "has_datablock", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Has Data-Block", "");
  api_def_prop_bool_fns(prop, "api_WorkSpaceTool_has_datablock_get", NULL
  api_define_verify_stype(1);

  prop = api_def_prop(sapi, "widget", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Widget", "");
  api_def_prop_string_fns(
      prop, "api_WorkSpaceTool_widget_get", "api_WorkSpaceTool_widget_length", NULL);

  api_api_workspace_tool(sapi);
}

static void api_def_workspace_tools(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "wmTools");
  sapi = api_def_struct(dapi, "wmTools", NULL);
  api_def_struct_stype(sapi, "WorkSpace");
  api_def_struct_ui_text(sapi, "WorkSpace UI Tags", "");

  /* add owner_id */
  fn = api_def_fn(
      sapi, "from_space_view3d_mode", "api_WorkSpace_tools_from_space_view3d_mode"
  api_def_fn_ui_description(fn, "");
  parm = wm_def_enum(fn, "mode", api_enum_cxt_mode_items, 0, "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "create", false, "Create", "");
  /* return type */
  parm = api_def_ptr(fn, "result", "WorkSpaceTool", "", "");
  api_def_fn_return(fb, parm);

  fn = api_def_fn(
      sapi, "from_space_image_mode", "api_WorkSpace_tools_from_space_image_mode");
  api_def_fn_ui_description(fn, "");
  parm = api_def_enum(fn, "mode", api_enum_space_image_mode_all_items, 0, "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "create", false, "Create", "");
  /* return type */
  parm = api_def_ptr(fn, "result", "WorkSpaceTool", "", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "from_space_node", "api_WorkSpace_tools_from_space_node");
  api_def_fn_ui_description(fn, "");
  api_def_bool(fn, "create", false, "Create", "");
  /* return type */
  parm = api_def_ptr(fn, "result", "WorkSpaceTool", "", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(
      sapi, "from_space_seq", "api_WorkSpace_tools_from_space_seq");
  api_def_fn_ui_description(fn, "");
  parm = api_def_enum(fn, "mode", api_enum_space_seq_view_type_items, 0, "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "create", false, "Create", "");
  /* return type */
  parm = api_def_ptr(fn, "result", "WorkSpaceTool", "", "");
  api_def_fn_return(fn, parm);
}

static void api_def_workspace(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "WorkSpace", "Id");
  api_def_struct_sdna(srna, "WorkSpace");
  api_def_struct_ui_text(
      sapi, "Workspace", "Workspace data-block, defining the working environment for the user");
  /* TODO: real icon, just to show something */
  api_def_struct_ui_icon(srna, ICON_WORKSPACE);

  prop = api_def_prop(sapi, "screens", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "layouts", NULL);
  api_def_prop_struct_type(prop, "Screen");
  api_def_prop_collection_fns(prop,
                              "api_workspace_screens_begin",
                              NULL,
                              NULL,
                              "api_workspace_screens_item_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(prop, "Screens", "Screen layouts of a workspace");

  prop = api_def_prop(sapi, "owner_ids", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "wmOwnerID");
  api_def_prop_ui_text(prop, "UI Tags", "");
  api_def_workspace_owner_ids(dapi, prop);

  prop = api_def_prop(sapi, "tools", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "tools", NULL);
  api_def_prop_struct_type(prop, "WorkSpaceTool");
  api_def_prop_ui_text(prop, "Tools", "");
  api_def_workspace_tools(dapi, prop);

  prop = api_def_prop(sapi, "object_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_workspace_object_mode_items);
  api_def_prop_ui_text(
      prop, "Object Mode", "Switch to this object mode when activating the workspace");

  prop = api_def_prop(sapi, "use_pin_scene", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", WORKSPACE_USE_PIN_SCENE);
  api_def_prop_ui_text(prop,
                       "Pin Scene",
                       "Remember the last used scene for the workspace and switch to it "
                       "whenever this workspace is activated again");
  api_def_prop_update(prop, NC_WORKSPACE, NULL);

  /* Flags */
  prop = api_def_prop(sapi, "use_filter_by_owner", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "flags", WORKSPACE_USE_FILTER_BY_ORIGIN);
  api_def_prop_ui_text(prop, "Use UI Tags", "Filter the UI by tags");
  api_def_prop_update(prop, 0, "api_window_update_all");

  prop = api_def_asset_lib_ref_common(
      sapi, "api_WorkSpace_asset_lib_get", "api_WorkSpace_asset_lib_set");
  api_def_prop_ui_text(prop,
                       "Asset Lib",
                       "Active asset lib to show in the UI, not used by the Asset Browser "
                       "(which has its own active asset library)");
  api_def_prop_update(prop, NC_ASSET | ND_ASSET_LIST_READING, NULL);

  api_workspace(sapi);
}

void api_def_workspace(DuneApi *dapi)
{
  api_def_workspace_owner(dapi);
  api_def_workspace_tool(dapi);

  api_def_workspace(dapi);
}

#endif /* RNA_RUNTIME */
