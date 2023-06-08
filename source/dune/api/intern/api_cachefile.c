#include "type_cachefile.h"
#include "type_scene.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#ifdef API_RUNTIME

#  include "lib_math.h"
#  include "lib_string.h"

#  include "dune_cachefile.h"

#  include "graph.h"
#  include "graph_build.h"

#  include "wm_api.h"
#  include "wm_types.h"

#  ifdef WITH_ALEMBIC
#    include "ABC_alembic.h"
#  endif

static void api_CacheFile_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->data;

  graph_id_tag_update(&cache_file->id, ID_RECALC_COPY_ON_WRITE);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
}

static void api_CacheFileLayer_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;

  graph_id_tag_update(&cache_file->id, ID_RECALC_COPY_ON_WRITE);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
}

static void api_CacheFile_dependency_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  rna_CacheFile_update(bmain, scene, ptr);
  DEG_relations_tag_update(bmain);
}

static void api_CacheFile_object_paths_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->data;
  api_iter_list_begin(iter, &cache_file->object_paths, NULL);
}

static PointerRNA rna_CacheFile_active_layer_get(ApiPtr *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  return api_ptr_inherit_refine(
      ptr, &ApiCacheFileLayer, dune_cachefile_get_active_layer(cache_file));
}

static void api_CacheFile_active_layer_set(ApiPtr *ptr,
                                           ApiPtr value,
                                           struct ReportList *reports)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  int index = lib_findindex(&cache_file->layers, value.data);
  if (index == -1) {
    dune_reportf(reports,
                RPT_ERROR,
                "Layer '%s' not found in object '%s'",
                ((CacheFileLayer *)value.data)->filepath,
                cache_file->id.name + 2);
    return;
  }

  cache_file->active_layer = index + 1;
}

static int api_CacheFile_active_layer_index_get(ApiPtr *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  return cache_file->active_layer - 1;
}

static void api_CacheFile_active_layer_index_set(ApiPtr *ptr, int value)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  cache_file->active_layer = value + 1;
}

static void api_CacheFile_active_layer_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;

  *min = 0;
  *max = max_ii(0, lib_list_count(&cache_file->layers) - 1);
}

static void api_CacheFileLayer_hidden_flag_set(ApiPtr *ptr, const bool value)
{
  CacheFileLayer *layer = (CacheFileLayer *)ptr->data;

  if (value) {
    layer->flag |= CACHEFILE_LAYER_HIDDEN;
  }
  else {
    layer->flag &= ~CACHEFILE_LAYER_HIDDEN;
  }
}

static CacheFileLayer *api_CacheFile_layer_new(CacheFile *cache_file,
                                               Cxt *C,
                                               ReportList *reports,
                                               const char *filepath)
{
  CacheFileLayer *layer = dune_cachefile_add_layer(cache_file, filepath);
  if (layer == NULL) {
    dune_reportf(
        reports, RPT_ERROR, "Cannot add a layer to CacheFile '%s'", cache_file->id.name + 2);
    return NULL;
  }

  Graph *graph = cxt_data_ensure_evaluated_graph(C);
  dune_cachefile_reload(graph, cache_file);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
  return layer;
}

static void api_CacheFile_layer_remove(CacheFile *cache_file, Cxt *C, ApiPtr *layer_ptr)
{
  CacheFileLayer *layer = layer_ptr->data;
  dune_cachefile_remove_layer(cache_file, layer);
  Graph *graph = cxt_data_ensure_evaluated_graph(C);
  dune_cachefile_reload(graph, cache_file);
  wm_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
}

#else

/* cachefile.object_paths */
static void api_def_alembic_object_path(DuneApi *dapi)
{
  ApiStruct *sapi = api_def_struct(dapi, "CacheObjectPath", NULL);
  api_def_struct_stypr(sapi, "CacheObjectPath");
  api_def_struct_ui_text(sapi, "Object Path", "Path of an object inside of an Alembic archive");
  api_def_struct_ui_icon(sapi, ICON_NONE);

  api_define_lib_overridable(true);

  ApiProp *prop = api_def_prop(sapi, "path", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Path", "Object path");
  api_def_struct_name_prop(sapi, prop);

  api_define_lib_overridable(false);
}

/* cachefile.object_paths */
static void api_def_cachefile_object_paths(DuneApi *dapi, ApiProp *cprop)
{
  api_def_prop_stype(cprop, "CacheObjectPaths");
  ApiStruct *sapi = api_def_struct(dapi, "CacheObjectPaths", NULL);
  api_def_struct_stype(sapi, "CacheFile");
  spi_def_struct_ui_text(sapi, "Object Paths", "Collection of object paths");
}

static void api_def_cachefile_layer(DuneApi *dapi)
{
  ApiStruct *sapi = api_def_struct(dapi, "CacheFileLayer", NULL);
  api_def_struct_stype(sapi, "CacheFileLayer");
  api_def_struct_ui_text(
      sapi,
      "Cache Layer",
      "Layer of the cache, used to load or override data from the first the first layer");

  ApiProp *prop = api_def_prop(sapi, "filepath", PROP_STRING, PROP_FILEPATH);
  api_def_prop_ui_text(prop, "File Path", "Path to the archive");
  api_def_prop_update(prop, 0, "api_CacheFileLayer_update");

  prop = api_def_prop(sapi, "hide_layer", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CACHEFILE_LAYER_HIDDEN);
  api_def_prop_bool_fns(prop, NULL, "api_CacheFileLayer_hidden_flag_set");
  api_def_prop_ui_icon(prop, ICON_HIDE_OFF, -1);
  api_def_prop_ui_text(prop, "Hide Layer", "Do not load data from this layer");
  api_def_prop_update(prop, 0, "api_CacheFileLayer_update");
}

static void api_def_cachefile_layers(DuneApi *dapi, ApiProp *cprop)
{
  api_def_prop_sapi(cprop, "CacheFileLayers");
  ApiStruct *sapi = api_def_struct(dapi, "CacheFileLayers", NULL);
  api_def_struct_stype(sapi, "CacheFile");
  api_def_struct_ui_text(sapi, "Cache Layers", "Collection of cache layers");

  ApiProp *prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "CacheFileLayer");
  api_def_prop_ptr_fns(
      prop, "api_CacheFile_active_layer_get", "api_CacheFile_active_layer_set", NULL, NULL);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Active Layer", "Active layer of the CacheFile");

  /* Add a layer. */
  ApiFn *fn = api_def_fn(sapi, "new", "api_CacheFile_layer_new");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_CXT);
  api_def_fn_ui_description(fn, "Add a new layer");
  ApiProp *parm = api_def_string(
      fn, "filepath", "File Path", 0, "", "File path to the archive used as a layer");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* Return type. */
  parm = api_def_ptr(fn, "layer", "CacheFileLayer", "", "Newly created layer");
  api_def_fn_return(fn, parm);

  /* Remove a layer. */
  fn = api_def_fn(sapi, "remove", "api_CacheFile_layer_remove");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_ui_description(fn, "Remove an existing layer from the cache file");
  parm = api_def_ptr(fn, "layer", "CacheFileLayer", "", "Layer to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_cachefile(DuneApi *dapi)
{
  ApiStruct *sapi = api_def_struct(dapi, "CacheFile", "ID");
  api_def_struct_sdna(sapi, "CacheFile");
  api_def_struct_ui_text(sapi, "CacheFile", "");
  api_def_struct_ui_icon(sapi, ICON_FILE);

  api_define_lib_overridable(true);

  ApiProp *prop = api_def_prop(sapi, "filepath", PROP_STRING, PROP_FILEPATH);
  api_def_prop_ui_text(prop, "File Path", "Path to external displacements file");
  api_def_prop_update(prop, 0, "api_CacheFile_update");

  prop = api_def_prop(sapi, "is_sequence", PROP_BOOL, PROP_NONE);
  api_def_prop_ui_text(
      prop, "Sequence", "Whether the cache is separated in a series of files");
  api_def_prop_update(prop, 0, "api_CacheFile_update");

  prop = api_def_prop(sapi, "use_render_procedural", PROP_BOOL, PROP_NONE);
  api_def_prop_ui_text(
      prop,
      "Use Render Engine Procedural",
      "Display boxes in the viewport as placeholders for the objects, Cycles will use a "
      "procedural to load the objects during viewport rendering in experimental mode, "
      "other render engines will also receive a placeholder and should take care of loading the "
      "Alembic data themselves if possible");
  api_def_prop_update(prop, 0, "api_CacheFile_dependency_update");

  /* ----------------- For Scene time ------------------- */

  prop = api_def_prop(sapi, "override_frame", PROP_BOOL, PROP_NONE);
  api_def_prop_ui_text(prop,
                       "Override Frame",
                       "Whether to use a custom frame for looking up data in the cache file,"
                       " instead of using the current scene frame");
  api_def_prop_update(prop, 0, "api_CacheFile_update");

  prop = api_def_prop(sapi, "frame", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "frame");
  api_def_prop_range(prop, -MAXFRAME, MAXFRAME);
  api_def_prop_ui_text(prop,
                           "Frame",
                           "The time to use for looking up the data in the cache file,"
                           " or to determine which file to use in a file sequence");
  api_def_prop_update(prop, 0, "api_CacheFile_update");

  prop = api_def_prop(sapi, "frame_offset", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "frame_offset");
  api_def_prop_range(prop, -MAXFRAME, MAXFRAME);
  api_def_prop_ui_text(prop,
                       "Frame Offset",
                       "Subtracted from the current frame to use for "
                       "looking up the data in the cache file, or to "
                       "determine which file to use in a file sequence");
  api_def_prop_update(prop, 0, "api_CacheFile_update");

  /* ----------------- Cache controls ----------------- */

  prop = api_def_prop(sapi, "use_prefetch", PROP_BOOL, PROP_NONE);
  api_def_prop_ui_text(
      prop,
      "Use Prefetch",
      "When enabled, the Cycles Procedural will preload animation data for faster updates");
  api_def_prop_update(prop, 0, "api_CacheFile_update");

  prop = api_def_prop(sapi, "prefetch_cache_size", PROP_INT, PROP_UNSIGNED);
  api_def_prop_ui_text(
      prop,
      "Prefetch Cache Size",
      "Memory usage limit in megabytes for the Cycles Procedural cache, if the data does not "
      "fit within the limit, rendering is aborted");
  api_def_prop_update(prop, 0, "api_CacheFile_update");

  /* ----------------- Axis Conversion ----------------- */

  prop = api_def_prop(sapi, "forward_axis", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "forward_axis");
  api_def_prop_enum_items(prop, api_enum_object_axis_items);
  api_def_prop_ui_text(prop, "Forward", "");
  api_def_prop_update(prop, 0, "api_CacheFile_update");

  prop = api_def_prop(sapi, "up_axis", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "up_axis");
  api_def_prop_enum_items(prop, api_enum_object_axis_items);
  api_def_prop_ui_text(prop, "Up", "");
  api_def_prop_update(prop, 0, "api_CacheFile_update");

  prop = api_def_prop(sapi, "scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "scale");
  api_def_prop_range(prop, 0.0001f, 1000.0f);
  api_def_prop_ui_text(
      prop,
      "Scale",
      "Value by which to enlarge or shrink the object with respect to the world's origin"
      " (only applicable through a Transform Cache constraint)");
  api_def_prop_update(prop, 0, "rna_CacheFile_update");

  /* object paths */
  prop = api_def_prop(sapi, "object_paths", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "object_paths", NULL);
  api_def_prop_collection_fns(prop,
                              "api_CacheFile_object_paths_begin",
                               "api_iter_list_next",
                               "api_iter_list_end",
                               "api_iter_list_get",
                               NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "CacheObjectPath");
  RNA_def_property_srna(prop, "CacheObjectPaths");
  RNA_def_property_ui_text(
      prop, "Object Paths", "Paths of the objects inside the Alembic archive");

  /* ----------------- Alembic Velocity Attribute ----------------- */

  prop = RNA_def_property(srna, "velocity_name", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(prop,
                           "Velocity Attribute",
                           "Name of the Alembic attribute used for generating motion blur data");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

  static const EnumPropertyItem velocity_unit_items[] = {
      {CACHEFILE_VELOCITY_UNIT_SECOND, "SECOND", 0, "Second", ""},
      {CACHEFILE_VELOCITY_UNIT_FRAME, "FRAME", 0, "Frame", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = RNA_def_property(srna, "velocity_unit", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "velocity_unit");
  RNA_def_property_enum_items(prop, velocity_unit_items);
  RNA_def_property_ui_text(
      prop,
      "Velocity Unit",
      "Define how the velocity vectors are interpreted with regard to time, 'frame' means "
      "the delta time is 1 frame, 'second' means the delta time is 1 / FPS");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

  /* ----------------- Alembic Layers ----------------- */

  prop = RNA_def_property(srna, "layers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "layers", NULL);
  RNA_def_property_struct_type(prop, "CacheFileLayer");
  RNA_def_property_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Cache Layers", "Layers of the cache");
  rna_def_cachefile_layers(brna, prop);

  prop = RNA_def_property(srna, "active_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_int_sdna(prop, NULL, "active_layer");
  RNA_def_property_int_funcs(prop,
                             "rna_CacheFile_active_layer_index_get",
                             "rna_CacheFile_active_layer_index_set",
                             "rna_CacheFile_active_layer_index_range");

  RNA_define_lib_overridable(false);

  rna_def_cachefile_object_paths(brna, prop);

  rna_def_animdata_common(srna);
}

void RNA_def_cachefile(BlenderRNA *brna)
{
  rna_def_cachefile(brna);
  rna_def_alembic_object_path(brna);
  rna_def_cachefile_layer(brna);
}

#endif
