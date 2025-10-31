#include <stdlib.h>

#include "types_collection.h"

#include "types_lineart.h"

#include "lib_utildefines.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_types.h"

const EnumPropItem api_enum_collection_color_items[] = {
    {COLLECTION_COLOR_NONE, "NONE", ICON_X, "None", "Assign no color tag to the collection"},
    {COLLECTION_COLOR_01, "COLOR_01", ICON_COLLECTION_COLOR_01, "Color 01", ""},
    {COLLECTION_COLOR_02, "COLOR_02", ICON_COLLECTION_COLOR_02, "Color 02", ""},
    {COLLECTION_COLOR_03, "COLOR_03", ICON_COLLECTION_COLOR_03, "Color 03", ""},
    {COLLECTION_COLOR_04, "COLOR_04", ICON_COLLECTION_COLOR_04, "Color 04", ""},
    {COLLECTION_COLOR_05, "COLOR_05", ICON_COLLECTION_COLOR_05, "Color 05", ""},
    {COLLECTION_COLOR_06, "COLOR_06", ICON_COLLECTION_COLOR_06, "Color 06", ""},
    {COLLECTION_COLOR_07, "COLOR_07", ICON_COLLECTION_COLOR_07, "Color 07", ""},
    {COLLECTION_COLOR_08, "COLOR_08", ICON_COLLECTION_COLOR_08, "Color 08", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef APU_RUNTIME

#  include "types_object.h"
#  include "types_scene.h"

#  include "graph.h"
#  include "graph_build.h"

#  include "dune_collection.h"
#  include "dune_global.h"
#  include "dune_layer.h"

#  include "wm_api.h"

#  include "api_access.h"

static void api_Collection_all_objects_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Collection *collection = (Collection *)ptr->data;
  ListApi collection_objects = dune_collection_object_cache_get(collection);
  api_iter_list_begin(iter, &collection_objects, NULL);
}

static ApiPtr api_Collection_all_objects_get(CollectionPropIter *iter)
{
  ListIter *internal = &iter->internal.list;

  /* we are actually iterating a ObjectBase list, so override get */
  Base *base = (Base *)internal->link;
  return api_ptr_inherit_refine(&iter->parent, &ApiObject, base->object);
}

static void api_Collection_objects_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Collection *collection = (Collection *)ptr->data;
  api_iter_list_begin(iter, &collection->gobject, NULL);
}

static ApiPtr api_Collection_objects_get(CollectionPropIter *iter)
{
  ListIter *internal = &iter->internal.list;

  /* we are actually iterating a ObjectBase list, so override get */
  CollectionObject *cob = (CollectionObject *)internal->link;
  return api_ptr_inherit_refine(&iter->parent, &ApiObject, cob->ob);
}

static void api_Collection_objects_link(Collection *collection,
                                        Main *main,
                                        ReportList *reports,
                                        Object *object)
{
  /* Currently this should not be allowed (might be supported in the future though...). */
  if (ID_IS_OVERRIDE_LIB(&collection->id)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Could not link the object '%s' because the collection '%s' is overridden",
                object->id.name + 2,
                collection->id.name + 2);
    return;
  }
  if (ID_IS_LINKED(&collection->id)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Could not link the object '%s' because the collection '%s' is linked",
                object->id.name + 2,
                collection->id.name + 2);
    return;
  }
  if (!dune_collection_object_add(bmain, collection, object)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Object '%s' already in collection '%s'",
                object->id.name + 2,
                collection->id.name + 2);
    return;
  }

  graph_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  graph_relations_tag_update(bmain);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, &object->id);
}

static void api_Collection_objects_unlink(Collection *collection,
                                          Main *main,
                                          ReportList *reports,
                                          Object *object)
{
  if (!dune_collection_object_remove(main, collection, object, false)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Object '%s' not in collection '%s'",
                object->id.name + 2,
                collection->id.name + 2);
    return;
  }

  graph_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  graph_relations_tag_update(main);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, &object->id);
}

static bool api_Collection_objects_override_apply(Main *main,
                                                  ApiPtr *ptr_dst,
                                                  ApiPtr *UNUSED(ptr_src),
                                                  ApiPtr *UNUSED(ptr_storage),
                                                  ApiProp *UNUSED(prop_dst),
                                                  ApiProp *UNUSED(prop_src),
                                                  ApiProp *UNUSED(prop_storage),
                                                  const int UNUSED(len_dst),
                                                  const int UNUSED(len_src),
                                                  const int UNUSED(len_storage),
                                                  ApiPtr *ptr_item_dst,
                                                  ApiPtr *ptr_item_src,
                                                  ApiPtr *UNUSED(ptr_item_storage),
                                                  IdOverrideLibPropOp *opop)
{
  lib_assert(opop->oper == IDOVERRIDE_LIBRARY_OP_REPLACE &&
             "Unsupported api override operation on collections' objects");
  UNUSED_VARS_NDEBUG(opop);

  Collection *coll_dst = (Collection *)ptr_dst->owner_id;

  if (ptr_item_dst->type == NULL || ptr_item_src->type == NULL) {
    // lib_assert_msg(0, "invalid source or destination object.");
    return false;
  }

  Object *ob_dst = ptr_item_dst->data;
  Object *ob_src = ptr_item_src->data;

  if (ob_src == ob_dst) {
    return true;
  }

  CollectionObject *cob_dst = BLI_findptr(
      &coll_dst->gobject, ob_dst, offsetof(CollectionObject, ob));

  if (cob_dst == NULL) {
    lib_assert_msg(0, "Could not find destination object in destination collection!");
    return false;
  }

  /* XXX TODO: We most certainly rather want to have a 'swap object pointer in collection'
   * util in dune_collection. This is only temp quick dirty test! */
  id_us_min(&cob_dst->ob->id);
  cob_dst->ob = ob_src;
  id_us_plus(&cob_dst->ob->id);

  if (dune_collection_is_in_scene(coll_dst)) {
    dune_main_collection_sync(bmain);
  }

  return true;
}

static void api_Collection_children_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Collection *collection = (Collection *)ptr->data;
  api_iter_list_begin(iter, &collection->children, NULL);
}

static ApiPtr api_Collection_children_get(CollectionPropIter *iter)
{
  ListIter *internal = &iter->internal.listbase;

  /* we are actually iterating a CollectionChild list, so override get */
  CollectionChild *child = (CollectionChild *)internal->link;
  return api_ptr_inherit_refine(&iter->parent, &ApiCollection, child->collection);
}

static void api_Collection_children_link(Collection *collection,
                                         Main *main,
                                         ReportList *reports,
                                         Collection *child)
{
  if (!dune_collection_child_add(main, collection, child)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Collection '%s' already in collection '%s'",
                child->id.name + 2,
                collection->id.name + 2);
    return;
  }

  graph_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  graph_relations_tag_update(main);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, &child->id);
}

static void api_Collection_children_unlink(Collection *collection,
                                           Main *main,
                                           ReportList *reports,
                                           Collection *child)
{
  if (!dune_collection_child_remove(main, collection, child)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Collection '%s' not in collection '%s'",
                child->id.name + 2,
                collection->id.name + 2);
    return;
  }

  graph_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  graph_relations_tag_update(main);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, &child->id);
}

static bool api_Collection_children_override_apply(Main *main,
                                                   ApiPtr *ptr_dst,
                                                   ApiPtr *UNUSED(ptr_src),
                                                   ApiPtr *UNUSED(ptr_storage),
                                                   ApiProp *UNUSED(prop_dst),
                                                   ApiProp *UNUSED(prop_src),
                                                   ApiProp *UNUSED(prop_storage),
                                                   const int UNUSED(len_dst),
                                                   const int UNUSED(len_src),
                                                   const int UNUSED(len_storage),
                                                   ApiPtr *ptr_item_dst,
                                                   ApiPtr *ptr_item_src,
                                                   ApiPtr *UNUSED(ptr_item_storage),
                                                   IdOverrideLibPropOp *opop)
{
  lib_assert(opop->op == IDOVERRIDE_LIB_OP_REPLACE &&
             "Unsupported api override operation on collections' children");
  UNUSED_VARS_NDEBUG(opop);

  Collection *coll_dst = (Collection *)ptr_dst->owner_id;

  if (ptr_item_dst->type == NULL || ptr_item_src->type == NULL) {
    /* This can happen when reference and overrides differ, just ignore then. */
    return false;
  }

  Collection *subcoll_dst = ptr_item_dst->data;
  Collection *subcoll_src = ptr_item_src->data;

  CollectionChild *collchild_dst = lib_findptr(
      &coll_dst->children, subcoll_dst, offsetof(CollectionChild, collection));

  if (collchild_dst == NULL) {
    lib_assert_msg(0, "Could not find destination sub-collection in destination collection!");
    return false;
  }

  /* XXX TODO: We most certainly rather want to have a 'swap object pointer in collection'
   * util in BKE_collection. This is only temp quick dirty test! */
  id_us_min(&collchild_dst->collection->id);
  collchild_dst->collection = subcoll_src;
  id_us_plus(&collchild_dst->collection->id);

  dune_collection_object_cache_free(coll_dst);
  dune_main_collection_sync(main);

  return true;
}

static void api_Collection_flag_set(ApiPtr *ptr, const bool value, const int flag)
{
  Collection *collection = (Collection *)ptr->data;

  if (collection->flag & COLLECTION_IS_MASTER) {
    return;
  }

  if (value) {
    collection->flag |= flag;
  }
  else {
    collection->flag &= ~flag;
  }
}

static void api_Collection_hide_select_set(ApiPtr *ptr, bool value)
{
  api_Collection_flag_set(ptr, value, COLLECTION_HIDE_SELECT);
}

static void api_Collection_hide_viewport_set(ApiPtr *ptr, bool value)
{
  api_Collection_flag_set(ptr, value, COLLECTION_HIDE_VIEWPORT);
}

static void api_Collection_hide_render_set(PointerRNA *ptr, bool value)
{
  api_Collection_flag_set(ptr, value, COLLECTION_HIDE_RENDER);
}

static void api_Collection_flag_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  Collection *collection = (Collection *)ptr->data;
  dune_collection_object_cache_free(collection);
  dune_main_collection_sync(main);

  graph_id_tag_update(&collection->id, ID_RECALC_COPY_ON_WRITE);
  graph_relations_tag_update(main);
  wm_main_add_notifier(NC_SCENE | ND_OB_SELECT, scene);
}

static int api_Collection_color_tag_get(struct ApiPtr *ptr)
{
  Collection *collection = (Collection *)ptr->data;

  return collection->color_tag;
}

static void api_Collection_color_tag_set(struct ApiPtr *ptr, int value)
{
  Collection *collection = (Collection *)ptr->data;

  if (collection->flag & COLLECTION_IS_MASTER) {
    return;
  }

  collection->color_tag = value;
}

static void api_Collection_color_tag_update(Main *UNUSED(main),
                                            Scene *scene,
                                            ApiPtr *UNUSED(ptr))
{
  wm_main_add_notifier(NC_SCENE | ND_LAYER_CONTENT, scene);
}

#else

/* collection.objects */
static void api_def_collection_objects(BlenderRNA *brna, PropertyRNA *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "CollectionObjects");
  sapi = api_def_struct(dapi, "CollectionObjects", NULL);
  api_def_struct_stype(sapi, "Collection");
  api_def_struct_ui_text(sapi, "Collection Objects", "Collection of collection objects");

  /* add object */
  fn = api_def_fn(sapi, "link", "api_Collection_objects_link");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_MAIN);
  api_def_fn_ui_description(fn, "Add this object to a collection");
  parm = api_def_ptr(fn, "object", "Object", "", "Object to add");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* remove object */
  fn = api_def_fn(sapi, "unlink", "api_Collection_objects_unlink");
  api_def_fn_ui_description(fn, "Remove this object from a collection");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_MAIN);
  parm = api_def_ptr(fn, "object", "Object", "", "Object to remove");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

/* collection.children */
static void api_def_collection_children(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "CollectionChildren");
  sapi = api_def_struct(dapi, "CollectionChildren", NULL);
  api_def_struct_stype(sapi, "Collection");
  api_def_struct_ui_text(sapi, "Collection Children", "Collection of child collections");

  /* add child */
  fn = api_def_fn(, "link", "api_Collection_children_link");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_MAIN);
  api_def_fn_ui_description(fn, "Add this collection as child of this collection");
  parm = api_def_ptr(fn, "child", "Collection", "", "Collection to add");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* remove child */
  fn = api_def_fn(sapi, "unlink", "api_Collection_children_unlink");
  api_def_fn_ui_description(fn, "Remove this child collection from a collection");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_MAIN);
  parm = api_def_ptr(fn, "child", "Collection", "", "Collection to remove");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_def_collections(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Collection", "ID");
  api_def_struct_ui_txt(sapi, "Collection", "Collection of Object data-blocks");
  api_def_struct_ui_icon(sapi, ICON_OUTLINER_COLLECTION);
  /* This is done on save/load in readfile.c,
   * removed if no objects are in the collection and not in a scene. */
  api_def_struct_clear_flag(sapi, STRUCT_ID_REFCOUNT);

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "instance_offset", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_ui_txt(
      prop, "Instance Offset", "Offset from the origin to use when instancing");
  api_def_prop_ui_range(prop, -10000.0, 10000.0, 10, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, NC_OBJ | ND_DRW, NULL);

  prop = api_def_prop(sapi, "objects", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_override_fns(prop, NULL, NULL, "api_Collection_objects_override_apply");
  api_def_prop_ui_txt(prop, "Objects", "Objects that are directly in this collection");
  api_def_prop_collection_fns(prop,
                                    "rna_Collection_objects_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_Collection_objects_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  api_def_collection_objects(dapi, prop);

  prop = api_def_prop(sapi, "all_objects", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_ui_text(
      prop, "All Objects", "Objects that are in this collection and its child collections");
  api_def_prop_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);
  api_def_prop_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_collection_fns(prop,
                                    "api_Collection_all_objects_begin",
                                    "api_iter_lib_next",
                                    "api_iter_lib_end",
                                    "api_Collection_all_objects_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);

  prop = api_def_prop(sapi, "children", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "Collection");
  api_def_prop_override_fns(prop, NULL, NULL, "rna_Collection_children_override_apply");
  api_def_prop_ui_text(
      prop, "Children", "Collections that are immediate children of this collection");
  api_def_prop_collection_fns(prop,
                              "rna_Collection_children_begin",
                              "rna_iterator_listbase_next",
                              "rna_iterator_listbase_end",
                              "rna_Collection_children_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_collection_children(dapi, prop);

  /* Flags */
  prop = api_def_prop(sapi, "hide_select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", COLLECTION_HIDE_SELECT);
  api_def_prop_bool_fns(prop, NULL, "api_Collection_hide_select_set");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_icon(prop, ICON_RESTRICT_SELECT_OFF, -1);
  api_def_prop_ui_text(prop, "Disable Selection", "Disable selection in viewport");
  api_def_prop_update(prop, NC_SCENE | ND_LAYER_CONTENT, "rna_Collection_flag_update");

  prop = api_def_prop(sapi, "hide_viewport", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", COLLECTION_HIDE_VIEWPORT);
  api_def_prop_bool_fns(prop, NULL, "rna_Collection_hide_viewport_set");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_icon(prop, ICON_RESTRICT_VIEW_OFF, -1);
  api_def_prop_ui_text(prop, "Disable in Viewports", "Globally disable in viewports");
  api_def_prop_update(prop, NC_SCENE | ND_LAYER_CONTENT, "api_Collection_flag_update");

  prop = api_def_prop(sapi, "hide_render", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", COLLECTION_HIDE_RENDER);
  api_def_prop_bool_fns(prop, NULL, "api_Collection_hide_render_set");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_icon(prop, ICON_RESTRICT_RNDR_OFF, -1);
  api_def_prop_ui_text(prop, "Disable in Renders", "Globally disable in renders");
  api_def_prop_update(prop, NC_SCENE | ND_LAYER_CONTENT, "api_Collection_flag_update");

  static const EnumPropItem api_collection_lineart_usage[] = {
      {COLLECTION_LRT_INCLUDE,
       "INCLUDE",
       0,
       "Include",
       "Generate feature lines for this collection"},
      {COLLECTION_LRT_OCCLUSION_ONLY,
       "OCCLUSION_ONLY",
       0,
       "Occlusion Only",
       "Only use the collection to produce occlusion"},
      {COLLECTION_LRT_EXCLUDE, "EXCLUDE", 0, "Exclude", "Don't use this collection in line art"},
      {COLLECTION_LRT_INTERSECTION_ONLY,
       "INTERSECTION_ONLY",
       0,
       "Intersection Only",
       "Only generate intersection lines for this collection"},
      {COLLECTION_LRT_NO_INTERSECTION,
       "NO_INTERSECTION",
       0,
       "No Intersection",
       "Include this collection but do not generate intersection lines"},
      {0, NULL, 0, NULL, NULL}};

  prop = api_def_prop(sapi, "lineart_usage", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_collection_lineart_usage);
  api_def_prop_ui_text(prop, "Usage", "How to use this collection in line art");
  api_def_prop_update(prop, NC_SCENE, NULL);

  prop = api_def_prop(sapi, "lineart_use_intersection_mask", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "lineart_flags", 1);
  api_def_prop_ui_text(
      prop, "Use Intersection Masks", "Use custom intersection mask for faces in this collection");
  api_def_prop_update(prop, NC_SCENE, NULL);

  prop = api_def_prop(sapi, "lineart_intersection_mask", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "lineart_intersection_mask", 1);
  api_def_prop_array(prop, 8);
  api_def_prop_ui_text(
      prop, "Masks", "Intersection generated by this collection will have this mask value");
  api_def_prop_update(prop, NC_SCENE, NULL);

  prop = api_def_prop(sapi, "color_tag", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "color_tag");
  api_def_prop_enum_fns(
      prop, "api_Collection_color_tag_get", "api_Collection_color_tag_set", NULL);
  api_def_prop_enum_items(prop, api_enum_collection_color_items);
  api_def_prop_ui_text(prop, "Collection Color", "Color tag for a collection");
  api_def_prop_update(prop, NC_SCENE | ND_LAYER_CONTENT, "api_Collection_color_tag_update");

  api_define_lib_overridable(false);
}

#endif
