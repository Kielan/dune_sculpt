#include <stdlib.h>

#include "types_id.h"
#include "types_userdef.h"

#include "dune_cx.h"
#include "lib_utildefines.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h" /* own include */

const EnumPropItem api_enum_cx_mode_items[] = {
    {CXT_MODE_EDIT_MESH, "EDIT_MESH", 0, "Mesh Edit", ""},
    {CXT_MODE_EDIT_CURVE, "EDIT_CURVE", 0, "Curve Edit", ""},
    {CXT_MODE_EDIT_CURVES, "EDIT_CURVES", 0, "Curves Edit", ""},
    {CXT_MODE_EDIT_SURFACE, "EDIT_SURFACE", 0, "Surface Edit", ""},
    {CXT_MODE_EDIT_TEXT, "EDIT_TEXT", 0, "Text Edit", ""},
    /* PARSKEL reuse will give issues */
    {CXT_MODE_EDIT_ARMATURE, "EDIT_ARMATURE", 0, "Armature Edit", ""},
    {CXT_MODE_EDIT_METABALL, "EDIT_METABALL", 0, "Metaball Edit", ""},
    {CXT_MODE_EDIT_LATTICE, "EDIT_LATTICE", 0, "Lattice Edit", ""},
    {CXT_MODE_POSE, "POSE", 0, "Pose", ""},
    {CXT_MODE_SCULPT, "SCULPT", 0, "Sculpt", ""},
    {CXT_MODE_PAINT_WEIGHT, "PAINT_WEIGHT", 0, "Weight Paint", ""},
    {CXT_MODE_PAINT_VERTEX, "PAINT_VERTEX", 0, "Vertex Paint", ""},
    {CXT_MODE_PAINT_TEXTURE, "PAINT_TEXTURE", 0, "Texture Paint", ""},
    {CXT_MODE_PARTICLE, "PARTICLE", 0, "Particle", ""},
    {CXT_MODE_OBJECT, "OBJECT", 0, "Object", ""},
    {CXT_MODE_PAINT_PEN, "PAINT_PEN", 0, "Pen Paint", ""},
    {CXT_MODE_EDIT_PEN, "EDIT_PEN", 0, "Pen Edit", ""},
    {CXT_MODE_SCULPT_PEN, "SCULPT_PEN", 0, "Pen Sculpt", ""},
    {CXT_MODE_WEIGHT_PEN, "WEIGHT_PEN", 0, "Pen Weight Paint", ""},
    {CXT_MODE_VERTEX_PEN, "VERTEX_PEN", 0, "Pen Vertex Paint", ""},
    {CXT_MODE_SCULPT_CURVES, "SCULPT_CURVES", 0, "Curves Sculpt", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "types_asset.h"

#  ifdef WITH_PYTHON
#    include "BPY_extern.h"
#  endif

#  include "render_engine.h"

static ApiPtr api_cx_manager_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  return api_ptr_inherit_refine(ptr, &ApiWindowManager, cx_wm_manager(C));
}

static ApiPtr api_cx_window_get(ApiPtr *ptr)
{
  Cxt *C = (Cxt *)ptr->data;
  return api_ptr_inherit_refine(ptr, &ApiWindow, cx_wm_window(C));
}

static ApiPtr api_cx_workspace_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  return api_ptr_inherit_refine(ptr, &ApiWorkSpace, cx_wm_workspace(C));
}

static ApiPtr api_cx_screen_get(ApiPtr *ptr)
{
  Cxt *C = (Cx *)ptr->data;
  return api_ptr_inherit_refine(ptr, &ApiScreen, cx_wm_screen(C));
}

static ApiPtr api_cx_area_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  ApiPtr newptr;
  api_ptr_create((Id *)cx_wm_screen(C), &ApiArea, cx_wm_area(C), &newptr);
  return newptr;
}

static ApiPtr api_cx_space_data_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  ApiPtr newptr;
  api_ptr_create((Id *)cx_wm_screen(C), &ApiSpace, cx_wm_space_data(C), &newptr);
  return newptr;
}

static ApiPtr api_cx_rgn_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  ApiPtr newptr;
  api_ptr_create((Id *)cx_wm_screen(C), &ApiRgn, cx_wm_rgn(C), &newptr);
  return newptr;
}

static ApiPtr api_cx_rgn_data_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;

  /* only exists for one space still, no generic system yet */
  if (cx_wm_view3d(C)) {
    ApiPtr newptr;
    api_ptr_create((Id *)cx_wm_screen(C), &ApiRgnView3D, cx_wm_rgn_data(C), &newptr);
    return newptr;
  }

  return ApiPtrNULL;
}

static ApiPtr api_cx_gizmo_group_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  ApiPtr newptr;
  api_ptr_create(NULL, &ApiGizmoGroup, cx_wm_gizmo_group(C), &newptr);
  return newptr;
}

static ApiPtr api_cx_asset_file_handle_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  bool is_handle_valid;
  AssetHandle asset_handle = cx_wm_asset_handle(C, &is_handle_valid);
  if (!is_handle_valid) {
    return ApiPtrNULL;
  }

  ApiPtr newptr;
  /* Have to cast away const, but the file entry API doesn't allow modifications anyway. */
  api_ptr_create(
      NULL, &ApiFileSelectEntry, (struct FileDirEntry *)asset_handle.file_data, &newptr);
  return newptr;
}

static ApiPtr api_cx_main_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  return api_ptr_inherit_refine(ptr, &ApiFnData, cx_data_main(C));
}

static ApiPtr api_cx_scene_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  return api_ptr_inherit_refine(ptr, &ApiScene, cx_data_scene(C));
}

static ApiPtr api_cx_view_layer_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  Scene *scene = cx_data_scene(C);
  ApiPtr scene_ptr;

 api_id_ptr_create(&scene->id, &scene_ptr);
  return api_ptr_inherit_refine(&scene_ptr, &ApiViewLayer, cx_data_view_layer(C));
}

static void api_cx_engine_get(ApiPtr *ptr, char *val)
{
  Cx *C = (Cx *)ptr->data;
  RndrEngineType *engine_type = cx_data_engine_type(C);
  strcpy(val, engine_type->idname);
}

static int api_cx_engine_length(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  RndrEngineType *engine_type = cx_data_engine_type(C);
  return strlen(engine_type->idname);
}

static ApiPtr api_cx_collection_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  return api_ptr_inherit_refine(ptr, &ApiCollection, cx_data_collection(C));
}

static ApiPtr api_cx_layer_collection_get(ApiPtr *ptr)
{
  Cx *C = (cx *)ptr->data;
  ptr->owner_id = &cx_data_scene(C)->id;
  return api_ptr_inherit_refine(ptr, &ApiLayerCollection, cx_data_layer_collection(C));
}

static ApiPtr api_cx_tool_settings_get(ApiPtr *ptr)
{
  Cx *C = (Cx *)ptr->data;
  ptr->owner_id = &cx_data_scene(C)->id;
  return api_ptr_inherit_refine(ptr, &ApiToolSettings, cx_data_tool_settings(C));
}

static ApiPtr api_cx_prefs_get(ApiPtr *UNUSED(ptr))
{
  ApiPtr newptr;
  api_ptr_create(NULL, &ApiPtrs, &U, &newptr);
  return newptr;
}

static int api_cx_mode_get(ApiPtr *ptr)
{
  Cxt *C = (Cxt *)ptr->data;
  return cxt_data_mode_enum(C);
}

static struct Graph *api_cx_evaluated_graph_get(Cxt *C)
{
  struct Graph *graph;

#  ifdef WITH_PYTHON
  /* Allow drivers to be evaluated */
  BPy_BEGIN_ALLOW_THREADS;
#  endif

  graph = cx_data_ensure_evaluated_depsgraph(C);

#  ifdef WITH_PYTHON
  BPy_END_ALLOW_THREADS;
#  endif

  return graph;
}

#else

void api_def_cxt(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "Context", NULL);
  api_def_struct_ui_txt(sapi, "Context", "Current windowmanager and data context");
  api_def_struct_types(sapi, "Cx");

  /* WM */
  prop = api_def_prop(sapi, "window_manager", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "WindowManager");
  api_def_prop_ptr_fns(prop, "api_cx_mngr_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "window", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "Window");
  api_def_prop_ptr_fns(prop, "api_cx_window_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "workspace", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "WorkSpace");
  api_def_prop_ptr_fns(prop, "api_cx_workspace_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "screen", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "Screen");
  api_def_prop_ptr_fns(prop, "api_cx_screen_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "area", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "Area");
  api_def_prop_ptr_fns(prop, "api_cx_area_get", NULL, NULL, NULL);

  prop = api_def_prop(sai, "space_data", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "Space");
  api_def_prop_ptr_fns(prop, "api_cx_space_data_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "rgn", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "Rgn");
  api_def_prop_ptr_fns(prop, "api_cx_rgn_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "region_data", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "RegionView3D");
  api_def_prop_ptr_fns(prop, "api_cxt_rgn_data_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "gizmo_group", PROP_PTR, PROP_l);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "GizmoGroup");
  api_def_prop_ptr_fns(prop, "api_cxt_gizmo_group_get", NULL, NULL, NULL);

  /* TODO can't expose AssetHandle, since there is no permanent storage to it (so we can't
   * return a pointer). Instead provide the FileDirEntry pointer it wraps. */
  prop = api_def_prop(sapi, "asset_file_handle", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "FileSelectEntry");
  api_def_prop_ptr_fns(prop, "api_cxt_asset_file_handle_get", NULL, NULL, NULL);
  api_def_prop_ui_txt(prop,
                       "",
                       "The file of an active asset. Avoid using this, it will be replaced by "
                       "a proper AssetHandle design");

  /* Data */
  prop = api_def_prop(sapi, "blend_data", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "BlendData");
  api_def_prop_ptr_fns(prop, "api_cx_main_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "scene", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "Scene");
  api_def_prop_ptr_fns(prop, "api_cx_scene_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "view_layer", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "ViewLayer");
  api_def_prop_ptr_fns(prop, "api_cx_view_layer_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "engine", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_string_funcs(prop, "api_cx_engine_get", "api_cx_engine_length", NULL);

  prop = api_def_prop(sapi, "collection", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prope_struct_type(prop, "Collection");
  api_def_prop_ptr_fns(prop, "api_cx_collection_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "layer_collection", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "LayerCollection");
  api_def_prop_ptr_fns(prop, "api_cx_layer_collection_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "tool_settings", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "ToolSettings");
  api_def_prop_ptr_fns(prop, "api_cx_tool_settings_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "prefs", PROP_PTR, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "Prefs");
  api_def_prop_ptr_fns(prop, "api_cx_prefs_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_cx_mode_items);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_enum_fns(prop, "api_cx_mode_get", NULL, NULL);

  fn = api_def_fn(sapi, "eval_graph_get", "api_cx_eval_graph_get");
  api_def_fn_ui_description(
      fn,
      "Get the dependency graph for the current scene and view layer, to access to data-blocks "
      "with animation and modifiers applied. If any data-blocks have been edited, the dependency "
      "graph will be updated. This invalidates all references to evaluated data-blocks from the "
      "dependency graph.");
  parm = api_def_ptr(fn, "graph", "Graph", "", "Evaluated dependency graph");
  api_def_fn_return(fn, parm);
}

#endif
