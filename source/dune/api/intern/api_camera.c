#include <stdlib.h>

#include "types_camera.h"

#include "lib_math.h"

#include "api_access.h"
#include "api_define.h"

#include "api_internal.h"

#include "wm_api.h"
#include "wm_types.h"

#ifdef API_RUNTIME

#  include "dune_camera.h"
#  include "dune_object.h"

#  include "graph.h"
#  include "graph_build.h"

#  include "seq_relations.h"

static float api_Camera_angle_get(ApiPtr *ptr)
{
  Camera *cam = (Camera *)ptr->owner_id;
  float sensor = dune_camera_sensor_size(cam->sensor_fit, cam->sensor_x, cam->sensor_y);
  return focallength_to_fov(cam->lens, sensor);
}

static void api_Camera_angle_set(ApiPtr *ptr, float value)
{
  Camera *cam = (Camera *)ptr->owner_id;
  float sensor = dune_camera_sensor_size(cam->sensor_fit, cam->sensor_x, cam->sensor_y);
  cam->lens = fov_to_focallength(value, sensor);
}

static float api_Camera_angle_x_get(ApiPtr *ptr)
{
  Camera *cam = (Camera *)ptr->owner_id;
  return focallength_to_fov(cam->lens, cam->sensor_x);
}

static void api_Camera_angle_x_set(ApiPtr *ptr, float value)
{
  Camera *cam = (Camera *)ptr->owner_id;
  cam->lens = fov_to_focallength(value, cam->sensor_x);
}

static float api_Camera_angle_y_get(ApiPtr *ptr)
{
  Camera *cam = (Camera *)ptr->owner_id;
  return focallength_to_fov(cam->lens, cam->sensor_y);
}

static void api_Camera_angle_y_set(ApiPtr *ptr, float value)
{
  Camera *cam = (Camera *)ptr->owner_id;
  cam->lens = fov_to_focallength(value, cam->sensor_y);
}

static void api_Camera_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Camera *camera = (Camera *)ptr->owner_id;

  graph_id_tag_update(&camera->id, 0);
}

static void api_Camera_dependency_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  Camera *camera = (Camera *)ptr->owner_id;
  graph_relations_tag_update(main);
  graph_id_tag_update(&camera->id, 0);
}

static CameraBGImage *api_Camera_background_images_new(Camera *cam)
{
  CameraBGImage *bgpic = dune_camera_background_image_new(cam);

  wm_main_add_notifier(NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, cam);

  return bgpic;
}

static void api_Camera_background_images_remove(Camera *cam,
                                                ReportList *reports,
                                                ApiPtr *bgpic_ptr)
{
  CameraBGImage *bgpic = bgpic_ptr->data;
  if (lib_findindex(&cam->bg_images, bgpic) == -1) {
    dune_report(reports, RPT_ERROR, "Background image cannot be removed");
  }

  dune_camera_background_image_remove(cam, bgpic);
  API_PTR_INVALIDATE(bgpic_ptr);

  wm_main_add_notifier(NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, cam);
}

static void api_Camera_background_images_clear(Camera *cam)
{
  dune_camera_background_image_clear(cam);

  wm_main_add_notifier(NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, cam);
}

static void api_Camera_dof_update(Main *main, Scene *scene, ApiPtr *UNUSED(ptr))
{
  seq_relations_invalidate_scene_strips(main, scene);
  wm_main_add_notifier(NC_SCENE | ND_SEQUENCER, scene);
}

char *api_CameraDOFSettings_path(ApiPtr *ptr)
{
  /* if there is ID-data, resolve the path using the index instead of by name,
   * since the name used is the name of the texture assigned, but the texture
   * may be used multiple times in the same stack
   */
  if (ptr->owner_id) {
    if (GS(ptr->owner_id->name) == ID_CA) {
      return lib_strdup("dof");
    }
  }

  return lib_strdup("");
}

static void api_CameraDOFSettings_aperture_blades_set(ApiPtr *ptr, const int value)
{
  CameraDOFSettings *dofsettings = (CameraDOFSettings *)ptr->data;

  if (ELEM(value, 1, 2)) {
    if (dofsettings->aperture_blades == 0) {
      dofsettings->aperture_blades = 3;
    }
    else {
      dofsettings->aperture_blades = 0;
    }
  }
  else {
    dofsettings->aperture_blades = value;
  }
}

#else

static void api_def_camera_background_image(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem bgpic_source_items[] = {
      {CAM_BGIMG_SOURCE_IMAGE, "IMAGE", 0, "Image", ""},
      {CAM_BGIMG_SOURCE_MOVIE, "MOVIE_CLIP", 0, "Movie Clip", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem bgpic_camera_frame_items[] = {
      {0, "STRETCH", 0, "Stretch", ""},
      {CAM_BGIMG_FLAG_CAMERA_ASPECT, "FIT", 0, "Fit", ""},
      {CAM_BGIMG_FLAG_CAMERA_ASPECT | CAM_BGIMG_FLAG_CAMERA_CROP, "CROP", 0, "Crop", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem bgpic_display_depth_items[] = {
      {0, "BACK", 0, "Back", ""},
      {CAM_BGIMG_FLAG_FOREGROUND, "FRONT", 0, "Front", ""},
      {0, NULL, 0, NULL, NULL},
  };

  srna = api_def_struct(dapi, "CameraBackgroundImage", NULL);
  api_def_struct_stype(sapi, "CameraBGImage");
  api_def_struct_ui_text(
      sapi, "Background Image", "Image and settings for display in the 3D View background");

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "source", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "source");
  api_def_prop_enum_items(prop, bgpic_source_items);
  api_def_prop_ui_text(prop, "Background Source", "Data source used for background");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "image", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "ima");
  api_def_prop_ui_text(prop, "Image", "Image displayed and edited in this space");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "clip", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "clip");
  api_def_prop_ui_text(prop, "MovieClip", "Movie clip displayed and edited in this space");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "image_user", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "iuser");
  api_def_prop_ui_text(
      prop,
      "Image User",
      "Parameters defining which layer, pass and frame of the image is displayed");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "clip_user", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "MovieClipUser");
  api_def_prop_ptr_stype(prop, NULL, "cuser");
  api_def_prop_ui_text(
      prop, "Clip User", "Parameters defining which frame of the movie clip is displayed");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "offset", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "offset");
  api_def_prop_ui_text(prop, "Offset", "");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 0.1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "scale");
  api_def_prop_ui_text(prop, "Scale", "Scale the background image");
  api_def_prop_range(prop, 0.0, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0, 10.0, 0.100, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "rotation", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "rotation");
  api_def_prope_ui_text(
      prop, "Rotation", "Rotation for the background image (ortho view only)");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "use_flip_x", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_BGIMG_FLAG_FLIP_X);
  api_def_prop_ui_text(prop, "Flip Horizontally", "Flip the background image horizontally");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "use_flip_y", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_BGIMG_FLAG_FLIP_Y);
  api_def_prop_ui_text(prop, "Flip Vertically", "Flip the background image vertically");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "alpha", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "alpha");
  api_def_prop_ui_text(
      prop, "Opacity", "Image opacity to blend the image against the background color");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "show_expanded", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_NO_DEG_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_BGIMG_FLAG_EXPANDED);
  api_def_prop_ui_text(prop, "Show Expanded", "Show the expanded in the user interface");
  api_def_prop_ui_icon(prop, ICON_DISCLOSURE_TRI_RIGHT, 1);

  prop = api_def_prop(sapi, "use_camera_clip", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_BGIMG_FLAG_CAMERACLIP);
  api_def_prop_ui_text(prop, "Camera Clip", "Use movie clip from active scene camera");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "show_background_image", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", CAM_BGIMG_FLAG_DISABLED);
  api_def_prop_ui_text(prop, "Show Background Image", "Show this image as background");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "show_on_foreground", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_BGIMG_FLAG_FOREGROUND);
  api_def_prop_ui_text(
      prop, "Show On Foreground", "Show this image in front of objects in viewport");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  /* expose 1 flag as a enum of 2 items */
  prop = api_def_prop(sapi, "display_depth", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, bgpic_display_depth_items);
  api_def_prop_ui_text(prop, "Depth", "Display under or over everything");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  /* expose 2 flags as a enum of 3 items */
  prop = api_def_prop(sapi, "frame_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, bgpic_camera_frame_items);
  api_def_prop_ui_text(prop, "Frame Method", "How the image fits in the camera frame");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  api_define_lib_overridable(false);
}

static void api_def_camera_background_images(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "CameraBackgroundImages");
  sapi = api_def_struct(dapi, "CameraBackgroundImages", NULL);
  api_def_struct_stype(sapi, "Camera");
  api_def_struct_ui_text(sapi, "Background Images", "Collection of background images");

  fn = api_def_fn(sapi, "new", "api_Camera_background_images_new");
  api_def_fn_ui_description(fn, "Add new background image");
  parm = api_def_ptr(
      fn, "image", "CameraBackgroundImage", "", "Image displayed as viewport background");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Camera_background_images_remove");
  api_def_fn_ui_description(fn, "Remove background image");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(
      fn, "image", "CameraBackgroundImage", "", "Image displayed as viewport background");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "clear", "api_Camera_background_images_clear");
  api_def_fn_ui_description(fn, "Remove all background images");
}

static void api_def_camera_stereo_data(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem convergence_mode_items[] = {
      {CAM_S3D_OFFAXIS, "OFFAXIS", 0, "Off-Axis", "Off-axis frustums converging in a plane"},
      {CAM_S3D_PARALLEL, "PARALLEL", 0, "Parallel", "Parallel cameras with no convergence"},
      {CAM_S3D_TOE, "TOE", 0, "Toe-in", "Rotated cameras, looking at the convergence distance"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem pivot_items[] = {
      {CAM_S3D_PIVOT_LEFT, "LEFT", 0, "Left", ""},
      {CAM_S3D_PIVOT_RIGHT, "RIGHT", 0, "Right", ""},
      {CAM_S3D_PIVOT_CENTER, "CENTER", 0, "Center", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(sapi, "CameraStereoData", NULL);
  api_def_struct_sdna(sapi, "CameraStereoSettings");
  api_def_struct_nested(dapi, sapi, "Camera");
  api_def_struct_ui_text(sapi, "Stereo", "Stereoscopy settings for a Camera data-block");

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "convergence_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, convergence_mode_items);
  api_def_prop_ui_text(prop, "Mode", "");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "pivot", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, pivot_items);
  api_def_prop_ui_text(prop, "Pivot", "");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "interocular_distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 1e4f, 1, 3);
  api_def_prop_ui_text(
      prop,
      "Interocular Distance",
      "Set the distance between the eyes - the stereo plane distance / 30 should be fine");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "convergence_distance", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_range(prop, 0.00001f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.00001f, 15.0f, 1, 3);
  api_def_prop_ui_text(prop,
                       "Convergence Plane Distance",
                       "The converge point for the stereo cameras "
                       "(often the distance between a projector and the projection screen)");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "use_spherical_stereo", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_S3D_SPHERICAL);
  api_def_prop_ui_text(prop,
                       "Spherical Stereo",
                       "Render every pixel rotating the camera around the "
                       "middle of the interocular distance");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "use_pole_merge", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_S3D_POLE_MERGE);
  api_def_prop_ui_text(
      prop, "Use Pole Merge", "Fade interocular distance to 0 after the given cutoff angle");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "pole_merge_angle_from", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_range(prop, 0.0f, M_PI_2);
  api_def_prop_ui_text(
      prop, "Pole Merge Start Angle", "Angle at which interocular distance starts to fade to 0");
  RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "pole_merge_angle_to", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_range(prop, 0.0f, M_PI_2);
  api_def_prop_ui_text(
      prop, "Pole Merge End Angle", "Angle at which interocular distance is 0");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  api_define_lib_overridable(false);
}

static void api_def_camera_dof_settings_data(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "CameraDOFSettings", NULL);
  api_def_struct_stype(sapi, "CameraDOFSettings");
  api_def_struct_path_fn(sapi, "api_CameraDOFSettings_path");
  api_def_struct_ui_text(sapi, "Depth of Field", "Depth of Field settings");

  api_define_lib_overridable(true);

  prop = api_def_prop(sapi, "use_dof", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_DOF_ENABLED);
  api_def_prop_ui_text(prop, "Depth of Field", "Use Depth of Field");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "rna_Camera_dof_update");

  prop = api_def_prop(sapi, "focus_object", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_ptr_stype(prop, NULL, "focus_object");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(
      prop, "Focus Object", "Use this object to define the depth of field focal point");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "rna_Camera_dependency_update");

  prop = api_def_prop(sapi, "focus_distance", PROP_FLOAT, PROP_DISTANCE);
  // api_def_prop_ptr_stype(prop, NULL, "focus_distance");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 5000.0f, 1, 2);
  api_def_prop_ui_text(
      prop, "Focus Distance", "Distance to the focus point for depth of field");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_dof_update");

  prop = api_def_prop(sapi, "aperture_fstop", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(
      prop,
      "F-Stop",
      "F-Stop ratio (lower numbers give more defocus, higher numbers give a sharper image)");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.1f, 128.0f, 10, 1);
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_dof_update");

  prop = api_def_prop(sapi, "aperture_blades", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(
      prop, "Blades", "Number of blades in aperture for polygonal bokeh (at least 3)");
  api_def_prop_range(prop, 0, 16);
  api_def_prop_int_fns(prop, NULL, "api_CameraDOFSettings_aperture_blades_set", NULL);
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_dof_update");

  prop = apu_def_prop(sapi, "aperture_rotation", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_ui_text(prop, "Rotation", "Rotation of blades in aperture");
  api_def_prop_range(prop, -M_PI, M_PI);
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_dof_update");

  prop = api_def_prop(sapi, "aperture_ratio", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(prop, "Ratio", "Distortion to simulate anamorphic lens bokeh");
  api_def_prop_range(prop, 0.01f, FLT_MAX);
  api_def_prop_ui_range(prop, 1.0f, 2.0f, 0.1, 3);
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "rna_Camera_dof_update");

  api_define_lib_overridable(false);
}

void api_def_camera(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;
  static const EnumPropItem prop_type_items[] = {
      {CAM_PERSP, "PERSP", 0, "Perspective", ""},
      {CAM_ORTHO, "ORTHO", 0, "Orthographic", ""},
      {CAM_PANO, "PANO", 0, "Panoramic", ""},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropItem prop_lens_unit_items[] = {
      {0, "MILLIMETERS", 0, "Millimeters", "Specify the lens in millimeters"},
      {CAM_ANGLETOGGLE,
       "FOV",
       0,
       "Field of View",
       "Specify the lens as the field of view's angle"},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropItem sensor_fit_items[] = {
      {CAMERA_SENSOR_FIT_AUTO,
       "AUTO",
       0,
       "Auto",
       "Fit to the sensor width or height depending on image resolution"},
      {CAMERA_SENSOR_FIT_HOR, "HORIZONTAL", 0, "Horizontal", "Fit to the sensor width"},
      {CAMERA_SENSOR_FIT_VERT, "VERTICAL", 0, "Vertical", "Fit to the sensor height"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "Camera", "ID");
  api_def_struct_ui_text(sapi, "Camera", "Camera data-block for storing camera settings");
  api_def_struct_ui_icon(sapi, ICON_CAMERA_DATA);

  api_define_lib_overridable(true);

  /* Enums */
  prop = apu_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, prop_type_items);
  api_def_prop_ui_text(prop, "Type", "Camera types");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "rna_Camera_update");

  prop = api_def_prop(sapi, "sensor_fit", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "sensor_fit");
  api_def_prop_enum_items(prop, sensor_fit_items);
  api_def_prop_ui_text(
      prop, "Sensor Fit", "Method to fit image and field of view angle inside the sensor");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "rna_Camera_update");

  /* Number values */

  prop = api_def_prop(sapi, "passepartout_alpha", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "passepartalpha");
  api_def_prop_ui_text(
      prop, "Passepartout Alpha", "Opacity (alpha) of the darkened overlay in Camera view");
  api_def_prope_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "angle_x", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_range(prop, DEG2RAD(0.367), DEG2RAD(172.847));
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Horizontal FOV", "Camera lens horizontal field of view");
  api_def_prop_float_fns(prop, "api_Camera_angle_x_get", "api_Camera_angle_x_set", NULL);
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_update");

  prop = api_def_prop(sapi, "angle_y", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_range(prop, DEG2RAD(0.367), DEG2RAD(172.847));
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Vertical FOV", "Camera lens vertical field of view");
  api_def_prop_float_fns(prop, "api_Camera_angle_y_get", "api_Camera_angle_y_set", NULL);
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_update");

  prop = api_def_prop(sapi, "angle", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_range(prop, DEG2RAD(0.367), DEG2RAD(172.847));
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Field of View", "Camera lens field of view");
  api_def_prop_float_fns(prop, "api_Camera_angle_get", "api_Camera_angle_set", NULL);
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_update");

  prop = api_def_prop(sapi, "clip_start", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_range(prop, 1e-6f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  api_def_prop_ui_text(prop, "Clip Start", "Camera near clipping distance");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "clip_end", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_range(prop, 1e-6f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  api_def_prop_ui_text(prop, "Clip End", "Camera far clipping distance");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "lens", PROP_FLOAT, PROP_DISTANCE_CAMERA);
  api_def_prop_float_stype(prop, NULL, "lens");
  api_def_prop_range(prop, 1.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 1.0f, 5000.0f, 100, 4);
  api_def_prop_ui_text(prop, "Focal Length", "Perspective Camera lens value in millimeters");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "rna_Camera_update");

  prop = api_def_prop(sapi, "sensor_width", PROP_FLOAT, PROP_DISTANCE_CAMERA);
  api_def_prop_float_stype(prop, NULL, "sensor_x");
  api_def_prop_range(prop, 1.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 1.0f, 100.0f, 100, 4);
  api_def_prop_ui_text(
      prop, "Sensor Width", "Horizontal size of the image sensor area in millimeters");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_update");

  prop = api_def_prop(sapi, "sensor_height", PROP_FLOAT, PROP_DISTANCE_CAMERA);
  api_def_prop_float_stype(prop, NULL, "sensor_y");
  api_def_prop_range(prop, 1.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 1.0f, 100.0f, 100, 4);
  api_def_prop_ui_text(
      prop, "Sensor Height", "Vertical size of the image sensor area in millimeters");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_update");

  prop = api_def_prop(sapi, "ortho_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "ortho_scale");
  api_def_prop_range(prop, FLT_MIN, FLT_MAX);
  api_def_prop_ui_range(prop, 0.001f, 10000.0f, 10, 3);
  api_def_prop_ui_text(
      prop, "Orthographic Scale", "Orthographic Camera scale (similar to zoom)");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_update");

  prop = api_def_prop(sapi, "display_size", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "drawsize");
  api_def_prop_range(prop, 0.01f, 1000.0f);
  api_def_prop_ui_range(prop, 0.01, 100, 1, 2);
  api_def_prop_ui_text(
      prop, "Display Size", "Apparent size of the Camera object in the 3D View");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "shift_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "shiftx");
  api_def_prop_range(prop, -10.0f, 10.0f);
  api_def_prop_ui_range(prop, -2.0, 2.0, 1, 3);
  api_def_prop_ui_text(prop, "Shift X", "Camera horizontal shift");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_update");

  prop = api_def_prop(sapi, "shift_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "shifty");
  api_def_prop_range(prop, -10.0f, 10.0f);
  api_def_prop_ui_range(prop, -2.0, 2.0, 1, 3);
  api_def_prop_ui_text(prop, "Shift Y", "Camera vertical shift");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, "api_Camera_update");

  /* Stereo Settings */
  prop = api_def_prop(sapi, "stereo", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stypes(prop, NULL, "stereo");
  api_def_prop_struct_type(prop, "CameraStereoData");
  api_def_prop_ui_text(prop, "Stereo", "");

  /* flag */
  prop = api_def_prop(sapi, "show_limits", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_SHOWLIMITS);
  api_def_prop_ui_text(
      prop, "Show Limits", "Display the clipping range and focus point on the camera");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "show_mist", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_SHOWMIST);
  api_def_prop_ui_text(
      prop, "Show Mist", "Display a line from the Camera to indicate the mist area");
  api_def_prop_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "show_passepartout", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_SHOWPASSEPARTOUT);
  api_def_prop_ui_text(
      prop, "Show Passepartout", "Show a darkened overlay outside the image area in Camera view");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "show_safe_areas", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_SHOW_SAFE_MARGINS);
  api_def_prop_ui_text(
      prop, "Show Safe Areas", "Show TV title safe and action safe areas in Camera view");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "show_safe_center", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_SHOW_SAFE_CENTER);
  api_def_prop_ui_text(prop,
                       "Show Center-Cut Safe Areas",
                       "Show safe areas to fit content in a different aspect ratio");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "show_name", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_SHOWNAME);
  api_def_prop_ui_text(prop, "Show Name", "Show the active Camera's name in Camera view");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "show_sensor", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_SHOWSENSOR);
  api_def_prop_ui_text(
      prop, "Show Sensor Size", "Show sensor size (film gate) in Camera view");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = api_def_prop(sapi, "show_background_images", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CAM_SHOW_BG_IMAGE);
  api_def_prop_ui_text(
      prop, "Display Background Images", "Display reference images behind objects in the 3D View");
  api_def_prop_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = RNA_def_prop(sapi, "lens_unit", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_bitflag_stype(prop, NULL, "flag");
  RNA_def_property_enum_items(prop, prop_lens_unit_items);
  RNA_def_property_ui_text(prop, "Lens Unit", "Unit to edit lens in for the user interface");

  /* dtx */
  prop = RNA_def_property(srna, "show_composition_center", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "dtx", CAM_DTX_CENTER);
  RNA_def_property_ui_text(
      prop, "Center", "Display center composition guide inside the camera view");
  RNA_def_property_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = RNA_def_property(srna, "show_composition_center_diagonal", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "dtx", CAM_DTX_CENTER_DIAG);
  RNA_def_property_ui_text(
      prop, "Center Diagonal", "Display diagonal center composition guide inside the camera view");
  RNA_def_property_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = RNA_def_property(srna, "show_composition_thirds", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "dtx", CAM_DTX_THIRDS);
  RNA_def_property_ui_text(
      prop, "Thirds", "Display rule of thirds composition guide inside the camera view");
  RNA_def_property_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = RNA_def_property(srna, "show_composition_golden", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "dtx", CAM_DTX_GOLDEN);
  RNA_def_property_ui_text(
      prop, "Golden Ratio", "Display golden ratio composition guide inside the camera view");
  RNA_def_property_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = RNA_def_property(srna, "show_composition_golden_tria_a", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "dtx", CAM_DTX_GOLDEN_TRI_A);
  RNA_def_property_ui_text(prop,
                           "Golden Triangle A",
                           "Display golden triangle A composition guide inside the camera view");
  RNA_def_property_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = RNA_def_property(srna, "show_composition_golden_tria_b", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "dtx", CAM_DTX_GOLDEN_TRI_B);
  RNA_def_property_ui_text(prop,
                           "Golden Triangle B",
                           "Display golden triangle B composition guide inside the camera view");
  RNA_def_property_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = RNA_def_property(srna, "show_composition_harmony_tri_a", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "dtx", CAM_DTX_HARMONY_TRI_A);
  RNA_def_property_ui_text(
      prop, "Harmonious Triangle A", "Display harmony A composition guide inside the camera view");
  RNA_def_property_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  prop = RNA_def_property(srna, "show_composition_harmony_tri_b", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "dtx", CAM_DTX_HARMONY_TRI_B);
  RNA_def_property_ui_text(
      prop, "Harmonious Triangle B", "Display harmony B composition guide inside the camera view");
  RNA_def_property_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  /* pointers */
  prop = RNA_def_property(srna, "dof", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "CameraDOFSettings");
  RNA_def_property_ui_text(prop, "Depth Of Field", "");
  RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, NULL);

  prop = RNA_def_property(srna, "background_images", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "bg_images", NULL);
  RNA_def_property_struct_type(prop, "CameraBackgroundImage");
  RNA_def_property_ui_text(prop, "Background Images", "List of background images");
  RNA_def_property_update(prop, NC_CAMERA | ND_DRAW_RENDER_VIEWPORT, NULL);

  RNA_define_lib_overridable(false);

  rna_def_animdata_common(srna);

  rna_def_camera_background_image(brna);
  rna_def_camera_background_images(brna, prop);

  /* Nested Data. */
  RNA_define_animate_sdna(true);

  /* *** Animated *** */
  rna_def_camera_stereo_data(brna);
  rna_def_camera_dof_settings_data(brna);

  /* Camera API */
  RNA_api_camera(srna);
}

#endif
