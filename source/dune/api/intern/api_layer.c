#include "types_layer.h"
#include "types_scene.h"
#include "types_view3d.h"

#include "lang_translation.h"

#include "ed_object.h"
#include "ed_render.h"

#include "render_engine.h"

#include "wm_api.h"
#include "wm_types.h"

#include "api_define.h"

#include "api_internal.h"

#ifde API_RUNTIME

#  ifdef WITH_PYTHON
#    include "BPY_extern.h"
#  endif

#  include "types_collection.h"
#  include "types_object.h"

#  include "api_access.h"

#  include "dune_idprop.h"
#  include "dune_layer.h"
#  include "dune_mesh.h"
#  include "dune_node.h"
#  include "dune_scene.h"

#  include "NOD_composite.h"

#  include "lib_list.h"

#  include "graph_build.h"
#  include "graph_query.h"

/***********************************/

static ApiPtr api_ViewLayer_active_layer_collection_get(ApiPtr *ptr)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  LayerCollection *lc = view_layer->active_collection;
  return api_ptr_inherit_refine(ptr, &ApiLayerCollection, lc);
}

static void api_ViewLayer_active_layer_collection_set(ApiPtr *ptr,
                                                      ApiPtr value,
                                                      struct ReportList *UNUSED(reports))
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  LayerCollection *lc = (LayerCollection *)value.data;
  const int index = dune_layer_collection_findindex(view_layer, lc);
  if (index != -1) {
    dune_layer_collection_activate(view_layer, lc);
  }
}

static ApiPtr api_LayerObjects_active_object_get(ApiPtr *ptr)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  return api_ptr_inherit_refine(
      ptr, &ApiObject, view_layer->basact ? view_layer->basact->object : NULL);
}

static void api_LayerObjects_active_object_set(ApiPtr *ptr,
                                               ApoPtr value,
                                               struct ReportList *reports)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  if (value.data) {
    Object *ob = value.data;
    Base *basact_test = dune_view_layer_base_find(view_layer, ob);
    if (basact_test != NULL) {
      view_layer->basact = basact_test;
    }
    else {
      dune_reportf(reports,
                  RPT_ERROR,
                  "ViewLayer '%s' does not contain object '%s'",
                  view_layer->name,
                  ob->id.name + 2);
    }
  }
  else {
    view_layer->basact = NULL;
  }
}

size_t api_ViewLayer_path_buffer_get(ViewLayer *view_layer,
                                     char *r_api_path,
                                     const size_t api_path_buffer_size)
{
  char name_esc[sizeof(view_layer->name) * 2];
  lib_str_escape(name_esc, view_layer->name, sizeof(name_esc));

  return lib_snprintf_rlen(r_api_path, api_path_buffer_size, "view_layers[\"%s\"]", name_esc);
}

static char *rna_ViewLayer_path(ApiPtr *ptr)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  char api_path[sizeof(view_layer->name) * 3];

  api_ViewLayer_path_buffer_get(view_layer, rna_path, sizeof(rna_path));

  return lib_strdup(api_path);
}

static IdProp **api_ViewLayer_idprops(ApiPtr *ptr)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  return &view_layer->id_props;
}

static bool api_LayerCollection_visible_get(LayerCollection *layer_collection, bContext *C)
{
  View3D *v3d = cxt_wm_view3d(C);

  if ((v3d == NULL) || ((v3d->flag & V3D_LOCAL_COLLECTIONS) == 0)) {
    return (layer_collection->runtime_flag & LAYER_COLLECTION_VISIBLE_VIEW_LAYER) != 0;
  }

  if (v3d->local_collections_uuid & layer_collection->local_collections_bits) {
    return (layer_collection->runtime_flag & LAYER_COLLECTION_HIDE_VIEWPORT) == 0;
  }

  return false;
}

static void api_ViewLayer_update_render_passes(Id *id)
{
  Scene *scene = (Scene *)id;
  if (scene->nodetree) {
    ntreeCompositUpdateRLayers(scene->nodetree);
  }

  RenderEngineType *engine_type = render_engines_find(scene->r.engine);
  if (engine_type->update_render_passes) {
    RenderEngine *engine = render_engine_create(engine_type);
    if (engine) {
      LIST_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
        dune_view_layer_verify_aov(engine, scene, view_layer);
      }
    }
    render_engine_free(engine);
    engine = NULL;
  }
}

static ApiPtr api_ViewLayer_objects_get(CollectionPropIter *iter)
{
  ListIter *internal = &iter->internal.list;

  /* we are actually iterating a ObjectBase list */
  Base *base = (Base *)internal->link;
  return api_ptr_inherit_refine(&iter->parent, &ApiObject, base->object);
}

static int api_ViewLayer_objects_selected_skip(CollectionPropertyIterator *iter,
                                               void *UNUSED(data))
{
  ListIter *internal = &iter->internal.listbase;
  Base *base = (Base *)internal->link;

  if ((base->flag & BASE_SELECTED) != 0) {
    return 0;
  }

  return 1;
};

static ApiPtr api_ViewLayer_graph_get(ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  if (GS(id->name) == ID_SCE) {
    Scene *scene = (Scene *)id;
    ViewLayer *view_layer = (ViewLayer *)ptr->data;
    Graph *graph = dune_scene_get_graph(scene, view_layer);
    return api_ptr_inherit_refine(ptr, &ApiGraph, graph);
  }
  return ApiPtr_NULL;
}

static void api_LayerObjects_selected_begin(CollectionPropIter iter, ApiPtr *ptr)
{
  ViewLayer *view_layer = (ViewLayer *)ptr->data;
  api_iter_list_begin(
      iter, &view_layer->object_bases, api_ViewLayer_objects_selected_skip);
}

static void api_ViewLayer_update_tagged(Id *id_ptr,
                                        ViewLayer *view_layer,
                                        Main *main,
                                        ReportList *reports)
{
  Scene *scene = (Scene *)id_ptr;
  Graph *graph = dune_scene_ensure_graph(main, scene, view_layer);

  if (graph_is_evaluating(depsgraph)) {
    dune_report(reports, RPT_ERROR, "Dependency graph update requested during evaluation");
    return;
  }

#  ifdef WITH_PYTHON
  /* Allow drivers to be evaluated */
  BPy_BEGIN_ALLOW_THREADS;
#  endif

  /* NOTE: This is similar to CTX_data_depsgraph_pointer(). Ideally such access would be
   * de-duplicated across all possible cases, but for now this is safest and easiest way to go.
   *
   * The reason for this is that it's possible to have Python operator which asks view layer to
   * be updated. After re-do of such operator view layer's dependency graph will not be marked
   * as active. */
  graph_make_active(graph);
  dune_scene_graph_update_tagged(graph, main);

#  ifdef WITH_PYTHON
  BPy_END_ALLOW_THREADS;
#  endif
}

static void api_ObjectBase_select_update(Main *UNUSED(bmain),
                                         Scene *UNUSED(scene),
                                         PointerRNA *ptr)
{
  Base *base = (Base *)ptr->data;
  short mode = (base->flag & BASE_SELECTED) ? BA_SELECT : BA_DESELECT;
  ed_object_base_select(base, mode);
}

static void api_ObjectBase_hide_viewport_update(bContext *C, PointerRNA *UNUSED(ptr))
{
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  dune_layer_collection_sync(scene, view_layer);
  graph_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);
  wm_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
}

static void api_LayerCollection_name_get(struct ApiPtr *ptr, char *value)
{
  Id *id = (Id *)((LayerCollection *)ptr->data)->collection;
  lib_strncpy(value, id->name + 2, sizeof(id->name) - 2);
}

int api_LayerCollection_name_length(ApiPtr *ptr)
{
  Id *id = (Id *)((LayerCollection *)ptr->data)->collection;
  return strlen(id->name + 2);
}

static void api_LayerCollection_flag_set(ApiPtr *ptr, const bool value, const int flag)
{
  LayerCollection *layer_collection = (LayerCollection *)ptr->data;
  Collection *collection = layer_collection->collection;

  if (collection->flag & COLLECTION_IS_MASTER) {
    return;
  }

  if (value) {
    layer_collection->flag |= flag;
  }
  else {
    layer_collection->flag &= ~flag;
  }
}

static void api_LayerCollection_exclude_set(PointerRNA *ptr, bool value)
{
  api_LayerCollection_flag_set(ptr, value, LAYER_COLLECTION_EXCLUDE);
}

static void api_LayerCollection_holdout_set(ApiPtr *ptr, bool value)
{
  api_LayerCollection_flag_set(ptr, value, LAYER_COLLECTION_HOLDOUT);
}

static void api_LayerCollection_indirect_only_set(ApiPtr *ptr, bool value)
{
  api_LayerCollection_flag_set(ptr, value, LAYER_COLLECTION_INDIRECT_ONLY);
}

static void api_LayerCollection_hide_viewport_set(ApiPtr *ptr, bool value)
{
  api_LayerCollection_flag_set(ptr, value, LAYER_COLLECTION_HIDE);
}

static void api_LayerCollection_exclude_update(Main *main, Scene *UNUSED(scene), PointerRNA *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  LayerCollection *lc = (LayerCollection *)ptr->data;
  ViewLayer *view_layer = dune_view_layer_find_from_collection(scene, lc);

  /* Set/Unset it recursively to match the behavior of excluding via the menu or shortcuts. */
  const bool exclude = (lc->flag & LAYER_COLLECTION_EXCLUDE) != 0;
  dune_layer_collection_set_flag(lc, LAYER_COLLECTION_EXCLUDE, exclude);

  dune_layer_collection_sync(scene, view_layer);

  graph_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);
  if (!exclude) {
    /* We need to update animation of objects added back to the scene through enabling this view
     * layer. */
    FOREACH_OBJECT_BEGIN (view_layer, ob) {
      graph_id_tag_update(&ob->id, ID_RECALC_ANIMATION);
    }
    FOREACH_OBJECT_END;
  }

  graph_relations_tag_update(bmain);
  wm_main_add_notifier(NC_SCENE | ND_LAYER_CONTENT, NULL);
  if (exclude) {
    ed_object_base_active_refresh(main, scene, view_layer);
  }
}

static void api_LayerCollection_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scene *scene = (Scene *)ptr->owner_id;
  LayerCollection *lc = (LayerCollection *)ptr->data;
  ViewLayer *view_layer = dune_view_layer_find_from_collection(scene, lc);

  dune_layer_collection_sync(scene, view_layer);

  graph_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);

  wm_main_add_notifier(NC_SCENE | ND_LAYER_CONTENT, NULL);
}

static bool api_LayerCollection_has_objects(LayerCollection *lc)
{
  return (lc->runtime_flag & LAYER_COLLECTION_HAS_OBJECTS) != 0;
}

static bool api_LayerCollection_has_selected_objects(LayerCollection *lc, ViewLayer *view_layer)
{
  return dune_layer_collection_has_selected_objects(view_layer, lc);
}

#else

static void api_def_layer_collection(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "LayerCollection", NULL);
  api_def_struct_ui_text(sapi, "Layer Collection", "Layer collection");
  api_def_struct_ui_icon(sapi, ICON_OUTLINER_COLLECTION);

  prop = api_def_prop(sapi, "collection", PROP_POINTER, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE | PROP_ANIMATABLE);
  api_def_prop_struct_type(prop, "Collection");
  api_def_prop_ui_text(prop, "Collection", "Collection this layer collection is wrapping");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "collection->id.name");
  api_def_prop_clear_flag(prop, PROP_EDITABLE | PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Name", "Name of this view layer (same as its collection one)");
  api_def_prop_string_fns(
      prop, "api_LayerCollection_name_get", "rna_LayerCollection_name_length", NULL);
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "children", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_sdna(prop, NULL, "layer_collections", NULL);
  api_def_prop_struct_type(prop, "LayerCollection");
  api_def_prop_ui_text(prop, "Children", "Child layer collections");

  /* Restriction flags. */
  prop = api_def_prop(sapi, "exclude", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LAYER_COLLECTION_EXCLUDE);
  api_def_prop_bool_fns(prop, NULL, "api_LayerCollection_exclude_set");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Exclude from View Layer", "Exclude from view layer");
  api_def_prop_ui_icon(prop, ICON_CHECKBOX_HLT, -1);
  api_def_prop_update(prop, NC_SCENE | ND_LAYER, "api_LayerCollection_exclude_update");

  prop = api_def_prop(sapi, "holdout", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LAYER_COLLECTION_HOLDOUT);
  api_def_prop_bool_fns(prop, NULL, "api_LayerCollection_holdout_set");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_icon(prop, ICON_HOLDOUT_OFF, 1);
  api_def_prop_ui_text(prop, "Holdout", "Mask out objects in collection from view layer");
  api_def_prop_update(prop, NC_SCENE | ND_LAYER, "rna_LayerCollection_update");

  prop = api_def_prop(sapi, "indirect_only", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LAYER_COLLECTION_INDIRECT_ONLY);
  api_def_prop_bool_fns(prop, NULL, "rna_LayerCollection_indirect_only_set");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_icon(prop, ICON_INDIRECT_ONLY_OFF, 1);
  RNA_def_property_ui_text(
      prop,
      "Indirect Only",
      "Objects in collection only contribute indirectly (through shadows and reflections) "
      "in the view layer");
  RNA_def_property_update(prop, NC_SCENE | ND_LAYER, "api_LayerCollection_update");

  prop = api_def_prop(sapi, "hide_viewport", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LAYER_COLLECTION_HIDE);
  api_def_prop_bool_fns(prop, NULL, "rna_LayerCollection_hide_viewport_set");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_icon(prop, ICON_HIDE_OFF, -1);
  api_def_prop_ui_text(prop, "Hide in Viewport", "Temporarily hide in viewport");
  api_def_prop_update(prop, NC_SCENE | ND_LAYER_CONTENT, "api_LayerCollection_update");

  fn = api_def_fn(sapi, "visible_get", "api_LayerCollection_visible_get");
  api_def_fn_ui_description(fn,
                            "Whether this collection is visible, take into account the "
                            "collection parent and the viewport");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_return(fn, api_def_bool(fn, "result", 0, "", ""));

  /* Run-time flags. */
  prop = api_def_prop(sapi, "is_visible", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "runtime_flag", LAYER_COLLECTION_VISIBLE_VIEW_LAYER);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop,
                       "Visible",
                       "Whether this collection is visible for the view layer, take into "
                       "account the collection parent");

  fn = api_def_fn(sapi, "has_objects", "api_LayerCollection_has_objects");
  api_def_fn_ui_description(fn, "");
  api_def_fn_return(fn, api_def_bool(fn, "result", 0, "", ""));

  fn = api_def_fn(
      sapi, "has_selected_objects", "api_LayerCollection_has_selected_objects");
  api_def_fn_ui_description(fn, "");
  prop = api_def_ptr(
      func, "view_layer", "ViewLayer", "", "View layer the layer collection belongs to");
  api_def_param_flags(prop, 0, PARM_REQUIRED);
  api_def_fn_return(fn, api_def_bool(fn, "result", 0, "", ""));
}

static void api_def_layer_objects(BlenderRNA *brna, PropertyRNA *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_prop_sapi(cprop, "LayerObjects");
  sapi = api_def_struct(dapi, "LayerObjects", NULL);
  api_def_struct_stype(sapi, "ViewLayer");
  api_def_struct_ui_text(sapi, "Layer Objects", "Collections of objects");

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_ptr_fns(prop,
                       "api_LayerObjects_active_object_get",
                       "api_LayerObjects_active_object_set",
                       NULL,
                       NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop, "Active Object", "Active object for this layer");
  /* Could call: `ED_object_base_activate(C, view_layer->basact);`
   * but would be a bad level call and it seems the notifier is enough */
  api_def_prop_update(prop, NC_SCENE | ND_OB_ACTIVE, NULL);

  prop = api_def_prop(sapi, "selected", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "object_bases", NULL);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_collection_fns(prop,
                                    "api_LayerObjects_selected_begin",
                                    "api_iter_list_next",
                                    "api_iter_list_end",
                                    "api_ViewLayer_objects_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  api_def_prop_ui_text(prop, "Selected Objects", "All the selected objects of this layer");
}

static void api_def_object_base(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ObjectBase", NULL);
  api_def_struct_stype(sapi, "Base");
  api_def_struct_ui_text(sapi, "Object Base", "An object instance in a render layer");
  api_def_struct_ui_icon(sapi, ICON_OBJECT_DATA);

  prop = api_def_prop(sapi, "object", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "object");
  api_def_prop_ui_text(prop, "Object", "Object this base links to");

  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BASE_SELECTED);
  api_def_prop_ui_text(prop, "Select", "Object base selection state");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_ObjectBase_select_update");

  prop = api_def_prop(sapi, "hide_viewport", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", BASE_HIDDEN);
  api_def_prop_flag(prop, PROP_LIB_EXCEPTION);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_icon(prop, ICON_HIDE_OFF, -1);
  api_def_prop_ui_text(prop, "Hide in Viewport", "Temporarily hide in viewport");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_ObjectBase_hide_viewport_update");
}

void api_def_view_layer(BlenderRNA *brna)
{
  FunctionRNA *func;
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "ViewLayer", NULL);
  RNA_def_struct_ui_text(srna, "View Layer", "View layer");
  RNA_def_struct_ui_icon(srna, ICON_RENDER_RESULT);
  RNA_def_struct_path_func(srna, "rna_ViewLayer_path");
  RNA_def_struct_idprops_func(srna, "rna_ViewLayer_idprops");

  rna_def_view_layer_common(brna, srna, true);

  fn = api_def_fn(sapi, "update_render_passes", "rna_ViewLayer_update_render_passes");
  api_def_fn_ui_description(fn,
                            "Requery the enabled render passes from the render engine");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_NO_SELF);

  prop = api_def_prop(sapi, "layer_collection", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "LayerCollection");
  api_def_prop_ptr_stype(prop, NULL, "layer_collections.first");
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(
      prop,
      "Layer Collection",
      "Root of collections hierarchy of this view layer,"
      "its 'collection' ptr prop is the same as the scene's master collection");

  prop = api_def_prop(sapi, "active_layer_collection", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "LayerCollection");
  api_def_prop_ptr_fns(prop,
                       "rna_ViewLayer_active_layer_collection_get",
                       "rna_ViewLayer_active_layer_collection_set",
                       NULL,
                                 NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_NEVER_NULL);
  RNA_def_property_ui_text(
      prop, "Active Layer Collection", "Active layer collection in this view layer's hierarchy");
  RNA_def_property_update(prop, NC_SCENE | ND_LAYER, NULL);

  prop = RNA_def_property(srna, "objects", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "object_bases", NULL);
  RNA_def_property_struct_type(prop, "Object");
  RNA_def_property_collection_funcs(
      prop, NULL, NULL, NULL, "rna_ViewLayer_objects_get", NULL, NULL, NULL, NULL);
  RNA_def_property_ui_text(prop, "Objects", "All the objects in this layer");
  rna_def_layer_objects(brna, prop);

  /* layer options */
  prop = RNA_def_property(srna, "use", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", VIEW_LAYER_RENDER);
  RNA_def_property_ui_text(prop, "Enabled", "Enable or disable rendering of this View Layer");
  RNA_def_property_update(prop, NC_SCENE | ND_LAYER, NULL);

  prop = RNA_def_property(srna, "use_freestyle", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", VIEW_LAYER_FREESTYLE);
  RNA_def_property_ui_text(prop, "Freestyle", "Render stylized strokes in this Layer");
  RNA_def_property_update(prop, NC_SCENE | ND_LAYER, NULL);

  /* Freestyle */
  rna_def_freestyle_settings(brna);

  prop = RNA_def_property(srna, "freestyle_settings", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NEVER_NULL);
  RNA_def_property_pointer_sdna(prop, NULL, "freestyle_config");
  RNA_def_property_struct_type(prop, "FreestyleSettings");
  RNA_def_property_ui_text(prop, "Freestyle Settings", "");

  /* debug update routine */
  func = RNA_def_function(srna, "update", "rna_ViewLayer_update_tagged");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_USE_MAIN | FUNC_USE_REPORTS);
  RNA_def_function_ui_description(
      func, "Update data tagged to be updated from previous access to data or operators");

  /* Dependency Graph */
  prop = RNA_def_property(srna, "depsgraph", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "Depsgraph");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);
  RNA_def_property_ui_text(prop, "Dependency Graph", "Dependencies in the scene data");
  RNA_def_property_pointer_funcs(prop, "rna_ViewLayer_depsgraph_get", NULL, NULL, NULL);

  /* Nested Data. */
  /* *** Non-Animated *** */
  RNA_define_animate_sdna(false);
  rna_def_layer_collection(brna);
  rna_def_object_base(brna);
  RNA_define_animate_sdna(true);
  /* *** Animated *** */
}

#endif
