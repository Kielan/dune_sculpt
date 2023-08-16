#include <stdlib.h>

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_scene.h"
#include "types_volume.h"

#include "dune_volume.h"

#include "lib_math_base.h"

#include "lang_translation.h"

const EnumPropItem api_enum_volume_grid_data_type_items[] = {
    {VOLUME_GRID_BOOL, "BOOL", 0, "Bool", "Bool"},
    {VOLUME_GRID_FLOAT, "FLOAT", 0, "Float", "Single precision float"},
    {VOLUME_GRID_DOUBLE, "DOUBLE", 0, "Double", "Double precision"},
    {VOLUME_GRID_INT, "INT", 0, "Integer", "32-bit integer"},
    {VOLUME_GRID_INT64, "INT64", 0, "Integer 64-bit", "64-bit integer"},
    {VOLUME_GRID_MASK, "MASK", 0, "Mask", "No data, bool mask of active voxels"},
    {VOLUME_GRID_VECTOR_FLOAT, "VECTOR_FLOAT", 0, "Float Vector", "3D float vector"},
    {VOLUME_GRID_VECTOR_DOUBLE, "VECTOR_DOUBLE", 0, "Double Vector", "3D double vector"},
    {VOLUME_GRID_VECTOR_INT, "VECTOR_INT", 0, "Integer Vector", "3D integer vector"},
    {VOLUME_GRID_POINTS,
     "POINTS",
     0,
     "Points (Unsupported)",
     "Points grid, currently unsupported by volume objects"},
    {VOLUME_GRID_UNKNOWN, "UNKNOWN", 0, "Unknown", "Unsupported data type"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "graph.h"
#  include "graph_build.h"

#  include "wm_api.h"
#  include "wm_types.h"

static char *api_VolumeRender_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("render");
}

static char *api_VolumeDisplay_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("display");
}

/* Updates */

static void api_Volume_update_display(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Volume *volume = (Volume *)ptr->owner_id;
  wm_main_add_notifier(NC_GEOM | ND_DATA, volume);
}

static void api_Volume_update_filepath(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Volume *volume = (Volume *)ptr->owner_id;
  dune_volume_unload(volume);
  graph_id_tag_update(&volume->id, ID_RECALC_COPY_ON_WRITE);
  wm_main_add_notifier(NC_GEOM | ND_DATA, volume);
}

static void api_Volume_update_is_sequence(Main *main, Scene *scene, ApiPtr *ptr)
{
  api_Volume_update_filepath(main, scene, ptr);
  graph_relations_tag_update(main);
}

static void api_Volume_velocity_grid_set(ApiPtr *ptr, const char *value)
{
  Volume *volume = (Volume *)ptr->data;
  if (!dune_volume_set_velocity_grid_by_name(volume, value)) {
    wm_reportf(RPT_ERROR, "Could not find grid with name %s", value);
  }
  wm_main_add_notifier(NC_GEOM | ND_DATA, volume);
}

/* Grid */
static void api_VolumeGrid_name_get(ApiPtr *ptr, char *value)
{
  VolumeGrid *grid = ptr->data;
  strcpy(value, dune_volume_grid_name(grid));
}

static int api_VolumeGrid_name_length(ApiPtr *ptr)
{
  VolumeGrid *grid = ptr->data;
  return strlen(dune_volume_grid_name(grid));
}

static int api_VolumeGrid_data_type_get(ApiPtr *ptr
{
  const VolumeGrid *grid = ptr->data;
  return dune_volume_grid_type(grid);
}

static int api_VolumeGrid_channels_get(ApiPtr *ptr)
{
  const VolumeGrid *grid = ptr->data;
  return dune_volume_grid_channels(grid);
}

static void api_VolumeGrid_matrix_object_get(ApiPtr *ptr, float *value)
{
  VolumeGrid *grid = ptr->data;
  dune_volume_grid_transform_matrix(grid, (float(*)[4])value);
}

static bool api_VolumeGrid_is_loaded_get(ApiPtr *ptr)
{
  VolumeGrid *grid = ptr->data;
  return dune_volume_grid_is_loaded(grid);
}

static bool api_VolumeGrid_load(Id *id, VolumeGrid *grid)
{
  return dune_volume_grid_load((Volume *)id, grid);
}

static void api_VolumeGrid_unload(Id *id, VolumeGrid *grid)
{
  dune_volume_grid_unload((Volume *)id, grid);
}

/* Grids Iterator */

static void api_Volume_grids_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Volume *volume = ptr->data;
  int num_grids = dune_volume_num_grids(volume);
  iter->internal.count.ptr = volume;
  iter->internal.count.item = 0;
  iter->valid = (iter->internal.count.item < num_grids);
}

static void api_Volume_grids_next(CollectionPropIter *iter)
{
  Volume *volume = iter->internal.count.ptr;
  int num_grids = dune_volume_num_grids(volume);
  iter->internal.count.item++;
  iter->valid = (iter->internal.count.item < num_grids);
}

static void api_Volume_grids_end(CollectionPropIter *UNUSED(iter)) {}

static ApiPtr api_Volume_grids_get(CollectionPropIter *iter)
{
  Volume *volume = iter->internal.count.ptr;
  const VolumeGrid *grid = dune_volume_grid_get_for_read(volume, iter->internal.count.item);
  return api_ptr_inherit_refine(&iter->parent, &Api_VolumeGrid, (void *)grid);
}

static int api_Volume_grids_length(ApiPtr *ptr)
{
  Volume *volume = ptr->data;
  return dune_volume_num_grids(volume);
}

/* Active Grid */
static void api_VolumeGrids_active_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  Volume *volume = (Volume *)ptr->data;
  int num_grids = dune_volume_num_grids(volume);

  *min = 0;
  *max = max_ii(0, num_grids - 1);
}

static int api_VolumeGrids_active_index_get(ApiPtr *ptr)
{
  Volume *volume = (Volume *)ptr->data;
  int num_grids = dune_volume_num_grids(volume);
  return clamp_i(volume->active_grid, 0, max_ii(num_grids - 1, 0));
}

static void api_VolumeGrids_active_index_set(ApiPtr *ptr, int value)
{
  Volume *volume = (Volume *)ptr->data;
  volume->active_grid = value;
}

/* Loading */
static bool api_VolumeGrids_is_loaded_get(ApiPtr *ptr)
{
  Volume *volume = (Volume *)ptr->data;
  return dune_volume_is_loaded(volume);
}

/* Error Message */
static void api_VolumeGrids_error_message_get(ApiPtr *ptr, char *value)
{
  Volume *volume = (Volume *)ptr->data;
  strcpy(value, dune_volume_grids_error_msg(volume));
}

static int api_VolumeGrids_error_message_length(ApiPtr *ptr)
{
  Volume *volume = (Volume *)ptr->data;
  return strlen(dune_volume_grids_error_msg(volume));
}

/* Frame Filepath */
static void api_VolumeGrids_frame_filepath_get(ApiPtr *ptr, char *value)
{
  Volume *volume = (Volume *)ptr->data;
  strcpy(value, dune_volume_grids_frame_filepath(volume));
}

static int api_VolumeGrids_frame_filepath_length(ApiPtr *ptr)
{
  Volume *volume = (Volume *)ptr->data;
  return strlen(dune_volume_grids_frame_filepath(volume));
}

static bool api_Volume_load(Volume *volume, Main *main)
{
  return dune_volume_load(volume, main);
}

static bool api_Volume_save(Volume *volume, Main *main, ReportList *reports, const char *filepath)
{
  return dune_volume_save(volume, main, reports, filepath);
}

#else

static void api_def_volume_grid(DuneApi *api)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(api, "VolumeGrid", NULL);
  api_def_struct_ui_text(sapi, "Volume Grid", "3D volume grid");
  api_def_struct_ui_icon(sapi, ICON_VOLUME_DATA);

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_string_fns(
      prop, "rna_VolumeGrid_name_get", "rna_VolumeGrid_name_length", NULL);
  api_def_prop_ui_text(prop, "Name", "Volume grid name");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "data_type", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_enum_fns(prop, "api_VolumeGrid_data_type_get", NULL, NULL);
  api_def_prop_enum_items(prop, api_enum_volume_grid_data_type_items);
  api_def_prop_ui_text(prop, "Data Type", "Data type of voxel values");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_VOLUME);

  prop = api_def_prop(sapi, "channels", PROP_INT, PROP_UNSIGNED);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_int_fns(prop, "api_VolumeGrid_channels_get", NULL, NULL);
  api_def_prop_ui_text(prop, "Channels", "Number of dimensions of the grid data type");

  prop = api_def_prop(sapi, "matrix_object", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_multi_array(prop, 2, api_matrix_dimsize_4x4);
  api_def_prop_float_fns(prop, "api_VolumeGrid_matrix_object_get", NULL, NULL);
  api_def_prop_ui_text(
      prop, "Matrix Object", "Transformation matrix from voxel index to object space");

  prop = api_def_prop(sapi, "is_loaded", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_VolumeGrid_is_loaded_get", NULL);
  api_def_prop_ui_text(prop, "Is Loaded", "Grid tree is loaded in memory");

  /* API */
  ApiFn *fn;
  ApiProp *parm;

  fb = api_def_fn(sapi, "load", "api_VolumeGrid_load");
  api_def_fn_ui_description(fn, "Load grid tree from file");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  parm = api_def_bool(fm, "success", 0, "", "True if grid tree was successfully loaded");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "unload", "apu_VolumeGrid_unload");
  api_def_fn_flag(fb, FN_USE_SELF_ID);
  api_def_fn_ui_description(
      fn, "Unload grid tree and voxel data from memory, leaving only metadata");
}

static void api_def_volume_grids(BlenderRNA *brna, PropertyRNA *cprop)
{
  ApiStruct *sapi;
  ApuProp *prop;

  api_def_prop_sapi(cprop, "VolumeGrids");
  sapi = api_def_struct(dapi, "VolumeGrids", NULL);
  api_def_struct_stype(sapi, "Volume");
  api_def_struct_ui_text(sapi, "Volume Grids", "3D volume grids");

  prop = api_def_prop(sapi, "active_index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_fns(prop,
                             "rna_VolumeGrids_active_index_get",
                             "rna_VolumeGrids_active_index_set",
                             "rna_VolumeGrids_active_index_range");
  api_def_prop_ui_text(prop, "Active Grid Index", "Index of active volume grid");
  api_def_prop_update(prop, 0, "rna_Volume_update_display");

  prop = api_def_prop(sapi, "error_message", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_string_fns(
      prop, "rna_VolumeGrids_error_message_get", "api_VolumeGrids_error_message_length", NULL);
  api_def_prop_ui_text(
      prop, "Error Message", "If loading grids failed, error message with details");

  prop = api_def_prop(sapo, "is_loaded", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_VolumeGrids_is_loaded_get", NULL);
  api_def_prop_ui_text(prop, "Is Loaded", "List of grids and metadata are loaded in memory");

  prop = api_def_prop(sapi, "frame", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "runtime.frame");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop,
                       "Frame",
                       "Frame number that volume grids will be loaded at, based on scene time "
                       "and volume parameters");

  prop = api_def_prop(sapi, "frame_filepath", PROP_STRING, PROP_FILEPATH);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_string_fns(
      prop, "api_VolumeGrids_frame_filepath_get", "rna_VolumeGrids_frame_filepath_length", NULL);

  api_def_prop_ui_text(prop
                       "Frame File Path",
                       "Volume file used for loading the volume at the current frame. Empty "
                       "if the volume has not be loaded or the frame only exists in memory");

  /* API */
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "load", "api_Volume_load");
  api_def_fn_ui_description(fn, "Load list of grids and metadata from file");
  api_def_fn_flag(fn, FN_USE_MAIN);
  parm = api_def_bool(fn, "success", 0, "", "True if grid list was successfully loaded");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "unload", "dune_volume_unload");
  api_def_fn_ui_description(fn, "Unload all grid and voxel data from memory");

  fn = api_def_fn(sapi, "save", "api_Volume_save");
  api_def_fn_ui_description(fn, "Save grids and metadata to file");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_REPORTS);
  parm = api_def_string_file_path(fn, "filepath", NULL, 0, "", "File path to save to");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn, "success", 0, "", "True if grid list was successfully loaded");
  api_def_n_return(fn, parm);
}

static void api_def_volume_display(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "VolumeDisplay", NULL);
  api_def_struct_ui_text(sapi, "Volume Display", "Volume object display settings for 3D viewport");
  api_def_struct_stype(sapi, "VolumeDisplay");
  api_def_struct_path_fn(sapi, "api_VolumeDisplay_path");

  prop = api_def_prop(sapi, "density", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.00001, FLT_MAX);
  api_def_prop_ui_range(prop, 0.1, 100.0, 1, 3);
  api_def_prop_ui_text(prop, "Density", "Thickness of volume display in the viewport");
  api_def_prop_update(prop, 0, "api_Volume_update_display");

  static const EnumPropItem wireframe_type_items[] = {
      {VOLUME_WIREFRAME_NONE, "NONE", 0, "None", "Don't display volume in wireframe mode"},
      {VOLUME_WIREFRAME_BOUNDS,
       "BOUNDS",
       0,
       "Bounds",
       "Display single bounding box for the entire grid"},
      {VOLUME_WIREFRAME_BOXES,
       "BOXES",
       0,
       "Boxes",
       "Display bounding boxes for nodes in the volume tree"},
      {VOLUME_WIREFRAME_POINTS,
       "POINTS",
       0,
       "Points",
       "Display points for nodes in the volume tree"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem wireframe_detail_items[] = {
      {VOLUME_WIREFRAME_COARSE,
       "COARSE",
       0,
       "Coarse",
       "Display one box or point for each intermediate tree node"},
      {VOLUME_WIREFRAME_FINE,
       "FINE",
       0,
       "Fine",
       "Display box for each leaf node containing 8x8 voxels"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem interpolation_method_items[] = {
      {VOLUME_DISPLAY_INTERP_LINEAR, "LINEAR", 0, "Linear", "Good smoothness and speed"},
      {VOLUME_DISPLAY_INTERP_CUBIC,
       "CUBIC",
       0,
       "Cubic",
       "Smoothed high quality interpolation, but slower"},
      {VOLUME_DISPLAY_INTERP_CLOSEST, "CLOSEST", 0, "Closest", "No interpolation"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem axis_slice_position_items[] = {
      {VOLUME_SLICE_AXIS_AUTO,
       "AUTO",
       0,
       "Auto",
       "Adjust slice direction according to the view direction"},
      {VOLUME_SLICE_AXIS_X, "X", 0, "X", "Slice along the X axis"},
      {VOLUME_SLICE_AXIS_Y, "Y", 0, "Y", "Slice along the Y axis"},
      {VOLUME_SLICE_AXIS_Z, "Z", 0, "Z", "Slice along the Z axis"},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "wireframe_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, wireframe_type_items);
  api_def_prop_ui_text(prop, "Wireframe", "Type of wireframe display");
  api_def_prop_update(prop, 0, "api_Volume_update_display");

  prop = api_def_prop(sapi, "wireframe_detail", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, wireframe_detail_items);
  api_def_prop_ui_text(prop, "Wireframe Detail", "Amount of detail for wireframe display");
  api_def_prop_update(prop, 0, "api_Volume_update_display");

  prop = api_def_prop(sapi, "interpolation_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, interpolation_method_items);
  api_def_prop_ui_text(
      prop, "Interpolation", "Interpolation method to use for volumes in solid mode");
  api_def_prop_update(prop, 0, "api_Volume_update_display");

  prop = api_def_prop(sapi, "use_slice", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "axis_slice_method", VOLUME_AXIS_SLICE_SINGLE);
  api_def_prop_ui_text(prop, "Slice", "Perform a single slice of the domain object");
  api_def_prop_update(prop, 0, "api_Volume_update_display");

  prop = api_def_prop(sapi, "slice_axis", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, axis_slice_position_items);
  api_def_prop_ui_text(prop, "Axis", "");
  api_def_prop_update(prop, 0, "api_Volume_update_display");

  prop = api_def_prop(sapi, "slice_depth", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_range(prop, 0.0, 1.0, 0.1, 3);
  api_def_prop_ui_text(prop, "Position", "Position of the slice");
  api_def_prop_update(prop, 0, "api_Volume_update_display");
}

static void api_def_volume_render(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  srna = RNA_def_struct(brna, "VolumeRender", NULL);
  RNA_def_struct_ui_text(srna, "Volume Render", "Volume object render settings");
  RNA_def_struct_sdna(srna, "VolumeRender");
  RNA_def_struct_path_func(srna, "rna_VolumeRender_path");

  static const EnumPropertyItem precision_items[] = {
      {VOLUME_PRECISION_FULL, "FULL", 0, "Full", "Full float (Use 32 bit for all data)"},
      {VOLUME_PRECISION_HALF, "HALF", 0, "Half", "Half float (Use 16 bit for all data)"},
      {VOLUME_PRECISION_VARIABLE, "VARIABLE", 0, "Variable", "Use variable bit quantization"},
      {0, NULL, 0, NULL, NULL},
  };

  prop = RNA_def_property(srna, "precision", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, precision_items);
  RNA_def_property_ui_text(prop,
                           "Precision",
                           "Specify volume data precision. Lower values reduce memory consumption "
                           "at the cost of detail");
  RNA_def_property_update(prop, 0, "rna_Volume_update_display");

  static const EnumPropertyItem space_items[] = {
      {VOLUME_SPACE_OBJECT,
       "OBJECT",
       0,
       "Object",
       "Keep volume opacity and detail the same regardless of object scale"},
      {VOLUME_SPACE_WORLD,
       "WORLD",
       0,
       "World",
       "Specify volume step size and density in world space"},
      {0, NULL, 0, NULL, NULL},
  };

  prop = RNA_def_property(srna, "space", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, space_items);
  RNA_def_property_ui_text(
      prop, "Space", "Specify volume density and step size in object or world space");
  RNA_def_property_update(prop, 0, "rna_Volume_update_display");

  prop = RNA_def_property(srna, "step_size", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0, 100.0, 1, 3);
  RNA_def_property_ui_text(prop,
                           "Step Size",
                           "Distance between volume samples. Lower values render more detail at "
                           "the cost of performance. If set to zero, the step size is "
                           "automatically determined based on voxel size");
  RNA_def_property_update(prop, 0, "rna_Volume_update_display");

  prop = RNA_def_property(srna, "clipping", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "clipping");
  RNA_def_property_range(prop, 0.0, 1.0);
  RNA_def_property_ui_range(prop, 0.0, 1.0, 0.1, 3);
  RNA_def_property_ui_text(
      prop,
      "Clipping",
      "Value under which voxels are considered empty space to optimize rendering");
  RNA_def_property_update(prop, 0, "rna_Volume_update_display");
}

static void rna_def_volume(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "Volume", "ID");
  RNA_def_struct_ui_text(srna, "Volume", "Volume data-block for 3D volume grids");
  RNA_def_struct_ui_icon(srna, ICON_VOLUME_DATA);

  /* File */
  prop = RNA_def_property(srna, "filepath", PROP_STRING, PROP_FILEPATH);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_ui_text(prop, "File Path", "Volume file used by this Volume data-block");
  RNA_def_property_update(prop, 0, "rna_Volume_update_filepath");

  prop = RNA_def_property(srna, "packed_file", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "packedfile");
  RNA_def_property_ui_text(prop, "Packed File", "");

  /* Sequence */
  prop = RNA_def_property(srna, "is_sequence", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_ui_text(
      prop, "Sequence", "Whether the cache is separated in a series of files");
  RNA_def_property_update(prop, 0, "rna_Volume_update_is_sequence");

  prop = RNA_def_property(srna, "frame_start", PROP_INT, PROP_TIME);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_range(prop, MINAFRAMEF, MAXFRAMEF);
  RNA_def_property_ui_text(
      prop, "Start Frame", "Global starting frame of the sequence, assuming first has a #1");
  RNA_def_property_update(prop, 0, "rna_Volume_update_filepath");

  prop = RNA_def_property(srna, "frame_duration", PROP_INT, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_range(prop, 0, MAXFRAMEF);
  RNA_def_property_ui_text(prop, "Frames", "Number of frames of the sequence to use");
  RNA_def_property_update(prop, 0, "rna_Volume_update_filepath");

  prop = RNA_def_property(srna, "frame_offset", PROP_INT, PROP_NONE);
  RNA_def_property_ui_text(
      prop, "Offset", "Offset the number of the frame to use in the animation");
  RNA_def_property_update(prop, 0, "rna_Volume_update_filepath");

  static const EnumPropertyItem sequence_mode_items[] = {
      {VOLUME_SEQUENCE_CLIP, "CLIP", 0, "Clip", "Hide frames outside the specified frame range"},
      {VOLUME_SEQUENCE_EXTEND,
       "EXTEND",
       0,
       "Extend",
       "Repeat the start frame before, and the end frame after the frame range"},
      {VOLUME_SEQUENCE_REPEAT, "REPEAT", 0, "Repeat", "Cycle the frames in the sequence"},
      {VOLUME_SEQUENCE_PING_PONG,
       "PING_PONG",
       0,
       "Ping-Pong",
       "Repeat the frames, reversing the playback direction every other cycle"},
      {0, NULL, 0, NULL, NULL},
  };

  prop = RNA_def_property(srna, "sequence_mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_enum_items(prop, sequence_mode_items);
  RNA_def_property_ui_text(prop, "Sequence Mode", "Sequence playback mode");
  RNA_def_property_update(prop, 0, "rna_Volume_update_filepath");

  /* Grids */
  prop = RNA_def_property(srna, "grids", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "VolumeGrid");
  RNA_def_property_ui_text(prop, "Grids", "3D volume grids");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Volume_grids_begin",
                                    "rna_Volume_grids_next",
                                    "rna_Volume_grids_end",
                                    "rna_Volume_grids_get",
                                    "rna_Volume_grids_length",
                                    NULL,
                                    NULL,
                                    NULL);
  rna_def_volume_grids(brna, prop);

  /* Materials */
  prop = RNA_def_property(srna, "materials", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "mat", "totcol");
  RNA_def_property_struct_type(prop, "Material");
  RNA_def_property_ui_text(prop, "Materials", "");
  RNA_def_property_srna(prop, "IDMaterials"); /* see rna_ID.c */
  RNA_def_property_collection_funcs(
      prop, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "rna_IDMaterials_assign_int");

  /* Display */
  prop = RNA_def_property(srna, "display", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "display");
  RNA_def_property_struct_type(prop, "VolumeDisplay");
  RNA_def_property_ui_text(prop, "Display", "Volume display settings for 3D viewport");

  /* Render */
  prop = RNA_def_property(srna, "render", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "render");
  RNA_def_property_struct_type(prop, "VolumeRender");
  RNA_def_property_ui_text(prop, "Render", "Volume render settings for 3D viewport");

  /* Velocity. */
  prop = RNA_def_property(srna, "velocity_grid", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "velocity_grid");
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_Volume_velocity_grid_set");
  RNA_def_property_ui_text(
      prop,
      "Velocity Grid",
      "Name of the velocity field, or the base name if the velocity is split into multiple grids");

  prop = RNA_def_property(srna, "velocity_unit", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "velocity_unit");
  RNA_def_property_enum_items(prop, rna_enum_velocity_unit_items);
  RNA_def_property_ui_text(
      prop,
      "Velocity Unit",
      "Define how the velocity vectors are interpreted with regard to time, 'frame' means "
      "the delta time is 1 frame, 'second' means the delta time is 1 / FPS");
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_UNIT);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

  prop = RNA_def_property(srna, "velocity_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "velocity_scale");
  RNA_def_property_range(prop, 0.0f, FLT_MAX);
  RNA_def_property_ui_text(prop, "Velocity Scale", "Factor to control the amount of motion blur");

  /* Scalar grids for velocity. */
  prop = RNA_def_property(srna, "velocity_x_grid", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "runtime.velocity_x_grid");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Velocity X Grid",
                           "Name of the grid for the X axis component of the velocity field if it "
                           "was split into multiple grids");

  prop = RNA_def_property(srna, "velocity_y_grid", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "runtime.velocity_y_grid");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Velocity Y Grid",
                           "Name of the grid for the Y axis component of the velocity field if it "
                           "was split into multiple grids");

  prop = RNA_def_property(srna, "velocity_z_grid", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "runtime.velocity_z_grid");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Velocity Z Grid",
                           "Name of the grid for the Z axis component of the velocity field if it "
                           "was split into multiple grids");

  /* Common */
  rna_def_animdata_common(srna);
}

void RNA_def_volume(BlenderRNA *brna)
{
  rna_def_volume_grid(brna);
  rna_def_volume_display(brna);
  rna_def_volume_render(brna);
  rna_def_volume(brna);
}

#endif
