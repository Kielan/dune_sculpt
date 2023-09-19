#include <limits.h>
#include <stdlib.h>

#include "mem_guardedalloc.h"

#include "types_defaults.h"
#include "types_mask.h"
#include "types_object.h" /* SELECT */
#include "types_scene.h"

#include "lang.h"

#include "dune_movieclip.h"
#include "dune_tracking.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_types.h"

#include "imbuf.h"
#include "imbuf_types.h"

#ifdef API_RUNTIME

#  include "lib_math.h"

#  include "types_movieclip.h"

#  include "dune_mask.h"

#  include "graph.h"

#  include "api_access.h"

#  include "wm_api.h"

static void api_Mask_update_data(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  Mask *mask = (Mask *)ptr->owner_id;

  wm_main_add_notifier(NC_MASK | ND_DATA, mask);
  graph_id_tag_update(&mask->id, 0);
}

static void api_Mask_update_parent(Main *main, Scene *scene, ApiPtr *ptr)
{
  MaskParent *parent = ptr->data;

  if (parent->id) {
    if (GS(parent->id->name) == ID_MC) {
      MovieClip *clip = (MovieClip *)parent->id;
      MovieTracking *tracking = &clip->tracking;
      MovieTrackingObject *object = dune_tracking_object_get_named(tracking, parent->parent);

      if (object) {
        int clip_framenr = dune_movieclip_remap_scene_to_clip_frame(clip, scene->r.cfra);

        if (parent->type == MASK_PARENT_POINT_TRACK) {
          MovieTrackingTrack *track = dune_tracking_track_get_named(
              tracking, object, parent->sub_parent);

          if (track) {
            MovieTrackingMarker *marker = dune_tracking_marker_get(track, clip_framenr);
            float marker_pos_ofs[2], parmask_pos[2];
            MovieClipUser user = *types_struct_default_get(MovieClipUser);

            dune_movieclip_user_set_frame(&user, scene->r.cfra);

            add_v2_v2v2(marker_pos_ofs, marker->pos, track->offset);

            dune_mask_coord_from_movieclip(clip, &user, parmask_pos, marker_pos_ofs);

            copy_v2_v2(parent->parent_orig, parmask_pos);
          }
        }
        else /* if (parent->type == MASK_PARENT_PLANE_TRACK) */ {
          MovieTrackingPlaneTrack *plane_track = dune_tracking_plane_track_get_named(
              tracking, object, parent->sub_parent);
          if (plane_track) {
            MovieTrackingPlaneMarker *plane_marker = dune_tracking_plane_marker_get(plane_track,
                                                                                   clip_framenr);

            memcpy(parent->parent_corners_orig,
                   plane_marker->corners,
                   sizeof(parent->parent_corners_orig));
            zero_v2(parent->parent_orig);
          }
        }
      }
    }
  }

  api_Mask_update_data(main, scene, ptr);
}

/* NOTE: this function exists only to avoid id refcounting. */
static void api_MaskParent_id_set(ApiPtr *ptr,
                                  ApiPtr value,
                                  struct ReportList *UNUSED(reports))
{
  MaskParent *mpar = (MaskParent *)ptr->data;

  mpar->id = value.data;
}

static ApiStruct *api_MaskParent_id_typef(ApiPtr *ptr)
{
  MaskParent *mpar = (MaskParent *)ptr->data;

  return id_code_to_api_type(mpar->id_type);
}

static void api_MaskParent_id_type_set(ApiPtr *ptr, int value)
{
  MaskParent *mpar = (MaskParent *)ptr->data;

  /* change ID-type to the new type */
  mpar->id_type = value;

  /* clear the id-block if the type is invalid */
  if ((mpar->id) && (GS(mpar->id->name) != mpar->id_type)) {
    mpar->id = NULL;
  }
}

static void api_Mask_layers_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Mask *mask = (Mask *)ptr->owner_id;

  api_iter_list_begin(iter, &mask->masklayers, NULL);
}

static int api_Mask_layer_active_index_get(ApiPtr *ptr)
{
  Mask *mask = (Mask *)ptr->owner_id;

  return mask->masklay_act;
}

static void api_Mask_layer_active_index_set(ApiPtr *ptr, int value)
{
  Mask *mask = (Mask *)ptr->owner_id;

  mask->masklay_act = value;
}

static void api_Mask_layer_active_index_range(
    ApiPtr *ptr, int *min, int *max, int *softmin, int *softmax)
{
  Mask *mask = (Mask *)ptr->owner_id;

  *min = 0;
  *max = max_ii(0, mask->masklay_tot - 1);

  *softmin = *min;
  *softmax = *max;
}

static char *api_MaskLayer_path(ApiPtr *ptr)
{
  MaskLayer *masklay = (MaskLayer *)ptr->data;
  char name_esc[sizeof(masklay->name) * 2];
  lib_str_escape(name_esc, masklay->name, sizeof(name_esc));
  return lib_sprintfN("layers[\"%s\"]", name_esc);
}

static ApiPtr api_Mask_layer_active_get(ApiPtr *ptr)
{
  Mask *mask = (Mask *)ptr->owner_id;
  MaskLayer *masklay = dune_mask_layer_active(mask);

  return api_ptr_inherit_refine(ptr, &Api_MaskLayer, masklay);
}

static void api_Mask_layer_active_set(ApiPtr *ptr,
                                      ApiPtr value,
                                      struct ReportList *UNUSED(reports))
{
  Mask *mask = (Mask *)ptr->owner_id;
  MaskLayer *masklay = (MaskLayer *)value.data;

  dune_mask_layer_active_set(mask, masklay);
}

static void api_MaskLayer_splines_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  MaskLayer *masklay = (MaskLayer *)ptr->data;

  api_iter_list_begin(iter, &masklay->splines, NULL);
}

static void api_MaskLayer_name_set(ApiPtr *ptr, const char *value)
{
  Mask *mask = (Mask *)ptr->owner_id;
  MaskLayer *masklay = (MaskLayer *)ptr->data;
  char oldname[sizeof(masklay->name)], newname[sizeof(masklay->name)];

  /* need to be on the stack */
  lib_strncpy(oldname, masklay->name, sizeof(masklay->name));
  lib_strncpy_utf8(newname, value, sizeof(masklay->name));

  dune_mask_layer_rename(mask, masklay, oldname, newname);
}

static ApiPtr api_MaskLayer_active_spline_get(ApiPtr *ptr)
{
  MaskLayer *masklay = (MaskLayer *)ptr->data;

  return api_ptr_inherit_refine(ptr, &Api_MaskSpline, masklay->act_spline);
}

static void api_MaskLayer_active_spline_set(ApiPtr *ptr,
                                            ApiPtr value,
                                            struct ReportList *UNUSED(reports))
{
  MaskLayer *masklay = (MaskLayer *)ptr->data;
  MaskSpline *spline = (MaskSpline *)value.data;
  int index = lib_findindex(&masklay->splines, spline);

  if (index != -1) {
    masklay->act_spline = spline;
  }
  else {
    masklay->act_spline = NULL;
  }
}

static ApiPtr api_MaskLayer_active_spline_point_get(ApiPtr *ptr)
{
  MaskLayer *masklay = (MaskLayer *)ptr->data;

  return api_ptr_inherit_refine(ptr, &Api_MaskSplinePoint, masklay->act_point);
}

static void api_MaskLayer_active_spline_point_set(ApiPtr *ptr,
                                                  ApiPtr value,
                                                  struct ReportList *UNUSED(reports))
{
  MaskLayer *masklay = (MaskLayer *)ptr->data;
  MaskSpline *spline;
  MaskSplinePoint *point = (MaskSplinePoint *)value.data;

  masklay->act_point = NULL;

  for (spline = masklay->splines.first; spline; spline = spline->next) {
    if (point >= spline->points && point < spline->points + spline->tot_point) {
      masklay->act_point = point;

      break;
    }
  }
}

static void api_MaskSplinePoint_handle1_get(ApiPtr *ptr, float *values)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;
  copy_v2_v2(values, bezt->vec[0]);
}

static void api_MaskSplinePoint_handle1_set(ApiPtr *ptr, const float *values)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;
  copy_v2_v2(bezt->vec[0], values);
}

static void api_MaskSplinePoint_handle2_get(ApiPtr *ptr, float *values)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;
  copy_v2_v2(values, bezt->vec[2]);
}

static void api_MaskSplinePoint_handle2_set(ApiPtr *ptr, const float *values)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;
  copy_v2_v2(bezt->vec[2], values);
}

static void api_MaskSplinePoint_ctrlpoint_get(ApiPtr *ptr, float *values)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;
  copy_v2_v2(values, bezt->vec[1]);
}

static void api_MaskSplinePoint_ctrlpoint_set(ApiPtr *ptr, const float *values)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;
  copy_v2_v2(bezt->vec[1], values);
}

static int api_MaskSplinePoint_handle_type_get(ApiPtr *ptr)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;

  return bezt->h1;
}

static MaskSpline *mask_spline_from_point(Mask *mask, MaskSplinePoint *point)
{
  MaskLayer *mask_layer;
  for (mask_layer = mask->masklayers.first; mask_layer; mask_layer = mask_layer->next) {
    MaskSpline *spline;
    for (spline = mask_layer->splines.first; spline; spline = spline->next) {
      if (point >= spline->points && point < spline->points + spline->tot_point) {
        return spline;
      }
    }
  }
  return NULL;
}

static void mask_point_check_stick(MaskSplinePoint *point)
{
  BezTriple *bezt = &point->bezt;
  if (bezt->h1 == HD_ALIGN && bezt->h2 == HD_ALIGN) {
    float vec[3];
    sub_v3_v3v3(vec, bezt->vec[0], bezt->vec[1]);
    add_v3_v3v3(bezt->vec[2], bezt->vec[1], vec);
  }
}

static void api_MaskSplinePoint_handle_type_set(ApiPtr *ptr, int value)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;
  MaskSpline *spline = mask_spline_from_point((Mask *)ptr->owner_id, point);

  bezt->h1 = bezt->h2 = value;
  mask_point_check_stick(point);
  dune_mask_calc_handle_point(spline, point);
}

static int api_MaskSplinePoint_handle_left_type_get(ApiPtr *ptr)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;

  return bezt->h1;
}

static void api_MaskSplinePoint_handle_left_type_set(ApiPtr *ptr, int value)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;
  MaskSpline *spline = mask_spline_from_point((Mask *)ptr->owner_id, point);

  bezt->h1 = value;
  mask_point_check_stick(point);
  dune_mask_calc_handle_point(spline, point);
}

static int api_MaskSplinePoint_handle_right_type_get(ApiPtr *ptr)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;

  return bezt->h2;
}

static void api_MaskSplinePoint_handle_right_type_set(ApiPtr *ptr, int value)
{
  MaskSplinePoint *point = (MaskSplinePoint *)ptr->data;
  BezTriple *bezt = &point->bezt;
  MaskSpline *spline = mask_spline_from_point((Mask *)ptr->owner_id, point);

  bezt->h2 = value;
  mask_point_check_stick(point);
  dune_mask_calc_handle_point(spline, point);
}

/* API */
static MaskLayer *api_Mask_layers_new(Mask *mask, const char *name)
{
  MaskLayer *masklay = dune_mask_layer_new(mask, name);

  wm_main_add_notifier(NC_MASK | NA_EDITED, mask);

  return masklay;
}

static void api_Mask_layers_remove(Mask *mask, ReportList *reports, ApiPtr *masklay_ptr)
{
  MaskLayer *masklay = masklay_ptr->data;
  if (lib_findindex(&mask->masklayers, masklay) == -1) {
    dune_reportf(reports,
                RPT_ERROR,
                "Mask layer '%s' not found in mask '%s'",
                masklay->name,
                mask->id.name + 2);
    return;
  }

  dune_mask_layer_remove(mask, masklay);
  API_PTR_INVALIDATE(masklay_ptr);

  wm_main_add_notifier(NC_MASK | NA_EDITED, mask);
}

static void api_Mask_layers_clear(Mask *mask)
{
  dune_mask_layer_free_list(&mask->masklayers);

  wm_main_add_notifier(NC_MASK | NA_EDITED, mask);
}

static MaskSpline *api_MaskLayer_spline_new(Id *id, MaskLayer *mask_layer)
{
  Mask *mask = (Mask *)id;
  MaskSpline *new_spline;

  new_spline = dune_mask_spline_add(mask_layer);

  wm_main_add_notifier(NC_MASK | NA_EDITED, mask);

  return new_spline;
}

static void api_MaskLayer_spline_remove(Id *id,
                                        MaskLayer *mask_layer,
                                        ReportList *reports,
                                        ApiPtr *spline_ptr)
{
  Mask *mask = (Mask *)id;
  MaskSpline *spline = spline_ptr->data;

  if (dune_mask_spline_remove(mask_layer, spline) == false) {
    dune_reportf(
        reports, RPT_ERROR, "Mask layer '%s' does not contain spline given", mask_layer->name);
    return;
  }

  API_PTR_INVALIDATE(spline_ptr);

  graph_id_tag_update(&mask->id, ID_RECALC_GEOMETRY);
}

static void api_Mask_start_frame_set(ApiPtr *ptr, int value)
{
  Mask *data = (Mask *)ptr->data;
  /* MINFRAME not MINAFRAME, since some output formats can't taken negative frames */
  CLAMP(value, MINFRAME, MAXFRAME);
  data->sfra = value;

  if (data->sfra >= data->efra) {
    data->efra = MIN2(data->sfra, MAXFRAME);
  }
}

static void api_Mask_end_frame_set(ApiPtr *ptr, int value)
{
  Mask *data = (Mask *)ptr->data;
  CLAMP(value, MINFRAME, MAXFRAME);
  data->efra = value;

  if (data->sfra >= data->efra) {
    data->sfra = MAX2(data->efra, MINFRAME);
  }
}

static void api_MaskSpline_points_add(Id *id, MaskSpline *spline, int count)
{
  Mask *mask = (Mask *)id;
  MaskLayer *layer;
  int active_point_index = -1;
  int i, spline_shape_index;

  if (count <= 0) {
    return;
  }

  for (layer = mask->masklayers.first; layer; layer = layer->next) {
    if (lib_findindex(&layer->splines, spline) != -1) {
      break;
    }
  }

  if (!layer) {
    /* Shall not happen actually */
    lib_assert_msg(0, "No layer found for the spline");
    return;
  }

  if (layer->act_spline == spline) {
    active_point_index = layer->act_point - spline->points;
  }

  spline->points = mem_recallocn(spline->points,
                                 sizeof(MaskSplinePoint) * (spline->tot_point + count));
  spline->tot_point += count;

  if (active_point_index >= 0) {
    layer->act_point = spline->points + active_point_index;
  }

  spline_shape_index = dune_mask_layer_shape_spline_to_index(layer, spline);

  for (i = 0; i < count; i++) {
    int point_index = spline->tot_point - count + i;
    MaskSplinePoint *new_point = spline->points + point_index;
    new_point->bezt.h1 = new_point->bezt.h2 = HD_ALIGN;
    dune_mask_calc_handle_point_auto(spline, new_point, true);
    dune_mask_parent_init(&new_point->parent);

    /* Not efficient, but there's no other way for now */
    dune_mask_layer_shape_changed_add(layer, spline_shape_index + point_index, true, true);
  }

  wm_main_add_notifier(NC_MASK | ND_DATA, mask);
  graph_id_tag_update(&mask->id, 0);
}

static void api_MaskSpline_point_remove(Id *id,
                                        MaskSpline *spline,
                                        ReportList *reports,
                                        ApiPtr *point_ptr)
{
  Mask *mask = (Mask *)id;
  MaskSplinePoint *point = point_ptr->data;
  MaskSplinePoint *new_point_array;
  MaskLayer *layer;
  int active_point_index = -1;
  int point_index;

  for (layer = mask->masklayers.first; layer; layer = layer->next) {
    if (lib_findindex(&layer->splines, spline) != -1) {
      break;
    }
  }

  if (!layer) {
    /* Shall not happen actually */
    dune_report(reports, RPT_ERROR, "Mask layer not found for given spline");
    return;
  }

  if (point < spline->points || point >= spline->points + spline->tot_point) {
    dune_report(reports, RPT_ERROR, "Point is not found in given spline");
    return;
  }

  if (layer->act_spline == spline) {
    active_point_index = layer->act_point - spline->points;
  }

  point_index = point - spline->points;

  new_point_array = mem_mallocN(sizeof(MaskSplinePoint) * (spline->tot_point - 1),
                                "remove mask point");

  memcpy(new_point_array, spline->points, sizeof(MaskSplinePoint) * point_index);
  memcpy(new_point_array + point_index,
         spline->points + point_index + 1,
         sizeof(MaskSplinePoint) * (spline->tot_point - point_index - 1));

  mem_freen(spline->points);
  spline->points = new_point_array;
  spline->tot_point--;

  if (active_point_index >= 0) {
    if (active_point_index == point_index) {
      layer->act_point = NULL;
    }
    else if (active_point_index < point_index) {
      layer->act_point = spline->points + active_point_index;
    }
    else {
      layer->act_point = spline->points + active_point_index - 1;
    }
  }

  dune_mask_layer_shape_changed_remove(
      layer, dune_mask_layer_shape_spline_to_index(layer, spline) + point_index, 1);

  wm_main_add_notifier(NC_MASK | ND_DATA, mask);
  graph_id_tag_update(&mask->id, 0);

  API_PTR_INVALIDATE(point_ptr);
}

#else
static void api_def_maskParent(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem mask_id_type_items[] = {
      {ID_MC, "MOVIECLIP", ICON_SEQ, "Movie Clip", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem parent_type_items[] = {
      {MASK_PARENT_POINT_TRACK, "POINT_TRACK", 0, "Point Track", ""},
      {MASK_PARENT_PLANE_TRACK, "PLANE_TRACK", 0, "Plane Track", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "MaskParent", NULL);
  api_def_struct_ui_text(sapi, "Mask Parent", "Parenting settings for masking element");

  /* Target Props - ID-block to Drive */
  prop = api_def_prop(sapi, "id", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "ID");
  api_def_prop_flag(prop, PROP_EDITABLE);
  // api_def_prop_editable_func(prop, "rna_maskSpline_id_editable");
  /* NOTE: custom set function is ONLY to avoid rna setting a user for this. */
  api_def_prop_ptr_fns(
      prop, NULL, "api_MaskParent_id_set", "api_MaskParent_id_typef", NULL);
  api_def_prop_ui_text(
      prop, "Id", "ID-block to which masking element would be parented to or to its property");
  api_def_prop_update(prop, 0, "api_Mask_update_parent");

  prop = api_def_prop(sapi, "id_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "id_type");
  api_def_prop_enum_items(prop, mask_id_type_items);
  api_def_prop_enum_default(prop, ID_MC);
  api_def_prop_enum_fns(prop, NULL, "api_MaskParent_id_type_set", NULL);
  // api_def_prope_editable_func(prop, "api_MaskParent_id_type_editable");
  api_def_prop_ui_text(prop, "ID Type", "Type of ID-block that can be used");
  api_def_prop_update(prop, 0, "api_Mask_update_parent");

  /* type */
  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, parent_type_items);
  api_def_prop_ui_text(prop, "Parent Type", "Parent Type");
  api_def_prop_update(prop, 0, "api_Mask_update_parent");

  /* parent */
  prop = api_def_prop(sapi, "parent", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(
      prop, "Parent", "Name of parent object in specified data-block to which parenting happens");
  api_def_prop_string_maxlength(prop, MAX_ID_NAME - 2);
  api_def_prop_update(prop, 0, "api_Mask_update_parent");

  /* sub_parent */
  prop = api_def_prop(sapi, "sub_parent", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(
      prop,
      "Sub Parent",
      "Name of parent sub-object in specified data-block to which parenting happens");
  api_def_prop_string_maxlength(prop, MAX_ID_NAME - 2);
  api_def_prop_update(prop, 0, "api_Mask_update_parent");
}

static void api_def_maskSplinePointUW(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MaskSplinePointUW", NULL);
  api_def_struct_ui_text(
      sapi, "Mask Spline UW Point", "Single point in spline segment defining feather");

  /* u */
  prop = api_def_prop(sapi, "u", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "u");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "U", "U coordinate of point along spline segment");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  /* weight */
  prop = api_def_prop(sapi, "weight", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "w");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "Weight", "Weight of feather point");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  /* select */
  prop = api_def_prop(sapi, "select", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SELECT);
  api_def_prop_ui_text(prop, "Select", "Selection status");
  api_def_prop_update(prop, 0, "api_Mask_update_data");
}

static void api_def_maskSplinePoint(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem handle_type_items[] = {
      {HD_AUTO, "AUTO", 0, "Auto", ""},
      {HD_VECT, "VECTOR", 0, "Vector", ""},
      {HD_ALIGN, "ALIGNED", 0, "Aligned Single", ""},
      {HD_ALIGN_DOUBLESIDE, "ALIGNED_DOUBLESIDE", 0, "Aligned", ""},
      {HD_FREE, "FREE", 0, "Free", ""},
      {0, NULL, 0, NULL, NULL},
  };

  api_def_maskSplinePointUW(dapi);

  sapi = api_def_struct(dapi, "MaskSplinePoint", NULL);
  api_def_struct_ui_text(
      sapi, "Mask Spline Point", "Single point in spline used for defining mask");

  /* Vector values */
  prop = api_def_prop(sapi, "handle_left", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 2);
  api_def_prop_float_fns(
      prop, "api_MaskSplinePoint_handle1_get", "rna_MaskSplinePoint_handle1_set", NULL);
  api_def_prop_ui_text(prop, "Handle 1", "Coordinates of the first handle");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  prop = api_def_prop(sapi, "co", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 2);
  api_def_prop_float_fns(
      prop, "api_MaskSplinePoint_ctrlpoint_get", "api_MaskSplinePoint_ctrlpoint_set", NULL);
  api_def_prop_ui_text(prop, "Control Point", "Coordinates of the control point");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  prop = api_def_prop(sapi, "handle_right", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 2);
  api_def_prop_float_fns(
      prop, "api_MaskSplinePoint_handle2_get", "api_MaskSplinePoint_handle2_set", NULL);
  api_def_prop_ui_text(prop, "Handle 2", "Coordinates of the second handle");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  /* handle_type */
  prop = api_def_prop(sapi, "handle_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_fns(
      prop, "rna_MaskSplinePoint_handle_type_get", "api_MaskSplinePoint_handle_type_set", NULL);
  api_def_prop_enum_items(prop, handle_type_items);
  api_def_prop_ui_text(prop, "Handle Type", "Handle type");
  api_def_prop_update(prop, 0, "sapi_Mask_update_data");

  /* handle_type */
  prop = api_def_prop(sapi, "handle_left_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_fns(prop,
                        "api_MaskSplinePoint_handle_left_type_get",
                        "api_MaskSplinePoint_handle_left_type_set",
                        NULL);
  api_def_prop_enum_items(prop, handle_type_items);
  api_def_prop_ui_text(prop, "Handle 1 Type", "Handle type");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  /* handle_right */
  prop = api_def_prop(sapi, "handle_right_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_fns(prop,
                        "api_MaskSplinePoint_handle_right_type_get",
                        "api_MaskSplinePoint_handle_right_type_set",
                        NULL);
  api_def_prop_enum_items(prop, handle_type_items);
  api_def_prop_ui_text(prop, "Handle 2 Type", "Handle type");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  /* weight */
  prop = api_def_prop(sapi, "weight", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "bezt.weight");
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "Weight", "Weight of the point");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  /* select */
  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "bezt.f1", SELECT);
  api_def_prop_ui_text(prop, "Select", "Selection status");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  /* parent */
  prop = api_def_prop(sapi, "parent", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "MaskParent");

  /* feather points */
  prop = api_def_prop(sapi, "feather_points", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "MaskSplinePointUW");
  api_def_prop_collection_stypes(prop, NULL, "uw", "tot_uw");
  api_def_prop_ui_text(prop, "Feather Points", "Points defining feather");
}

static void api_def_mask_splines(BlenderRNA *brna)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *prop;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "MaskSplines", NULL);
  api_def_struct_stype(sapi, "MaskLayer");
  api_def_struct_ui_text(sapi, "Mask Splines", "Collection of masking splines");

  /* Create new spline */
  fn = api_def_fn(sapi, "new", "api_MaskLayer_spline_new");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Add a new spline to the layer");
  parm = api_def_ptr(fn, "spline", "MaskSpline", "", "The newly created spline");
  apu_def_fn_return(fn, parm);

  /* Remove the spline */
  fn = api_def_fn(sapi, "remove", "api_MaskLayer_spline_remove");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Remove a spline from a layer");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "spline", "MaskSpline", "", "The spline to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  /* active spline */
  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "MaskSpline");
  api_def_prop_ptr_fns(
      prop, "api_MaskLayer_active_spline_get", "api_MaskLayer_active_spline_set", NULL, NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop, "Active Spline", "Active spline of masking layer");

  /* active point */
  prop = api_def_prop(sapi, "active_point", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "MaskSplinePoint");
  api_def_prop_ptr_fn(prop,
                      "api_MaskLayer_active_spline_point_get",
                      "api_MaskLayer_active_spline_point_set",
                      NULL,
                      NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop, "Active Spline", "Active spline of masking layer");
}

static void api_def_maskSplinePoints(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "MaskSplinePoints", NULL);
  api_def_struct_stype(sapi, "MaskSpline");
  api_def_struct_ui_text(sapi, "Mask Spline Points", "Collection of masking spline points");

  /* Create new point */
  fn = api_def_function(sapi, "add", "rna_MaskSpline_points_add");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Add a number of point to this spline");
  parm = api_def_int(
      fn, "count", 1, 0, INT_MAX, "Number", "Number of points to add to the spline", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* Remove the point */
  fn = api_def_fn(sapi, "remove", "api_MaskSpline_point_remove");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Remove a point from a spline");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "point", "MaskSplinePoint", "", "The point to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_maskSpline(DuneApi *dapi)
{
  static const EnumPropItem spline_interpolation_items[] = {
      {MASK_SPLINE_INTERP_LINEAR, "LINEAR", 0, "Linear", ""},
      {MASK_SPLINE_INTERP_EASE, "EASE", 0, "Ease", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem spline_offset_mode_items[] = {
      {MASK_SPLINE_OFFSET_EVEN, "EVEN", 0, "Even", "Calculate even feather offset"},
      {MASK_SPLINE_OFFSET_SMOOTH,
       "SMOOTH",
       0,
       "Smooth",
       "Calculate feather offset as a second curve"},
      {0, NULL, 0, NULL, NULL},
  };

  ApiStruct *sapi;
  ApiProp *prop;

  api_def_maskSplinePoint(dapi);

  sapi = api_def_struct(dapi, "MaskSpline", NULL);
  api_def_struct_ui_text(sapi, "Mask spline", "Single spline used for defining mask shape");

  /* offset mode */
  prop = api_def_prop(sapi, "offset_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "offset_mode");
  api_def_prop_enum_items(prop, spline_offset_mode_items);
  api_def_prop_ui_text(
      prop, "Feather Offset", "The method used for calculating the feather offset");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  /* weight interpolation */
  prop = api_def_prop(sapi, "weight_interpolation", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "weight_interp");
  api_def_prop_enum_items(prop, spline_interpolation_items);
  api_def_prop_ui_text(
      prop, "Weight Interpolation", "The type of weight interpolation for spline");
  api_def_prop_update(prop, 0, "api_Mask_update_data");

  /* cyclic */
  prop = api_def_prop(sapi, "use_cyclic", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", MASK_SPLINE_CYCLIC);
  api_def_prop_ui_text(prop, "Cyclic", "Make this spline a closed loop");
  api_def_prop_update(prop, NC_MASK | NA_EDITED, "api_Mask_update_data");

  /* fill */
  prop = api_def_prop(sapi, "use_fill", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", MASK_SPLINE_NOFILL);
  api_def_prop_ui_text(prop, "Fill", "Make this spline filled");
  api_def_prop_update(prop, NC_MASK | NA_EDITED, "api_Mask_update_data");

  /* self-intersection check */
  prop = api_def_prop(sapi, "use_self_intersection_check", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", MASK_SPLINE_NOINTERSECT);
  api_def_prop_ui_text(
      prop, "Self Intersection Check", "Prevent feather from self-intersections");
  api_def_prop_update(prop, NC_MASK | NA_EDITED, "api_Mask_update_data");

  prop = api_def_prop(sapi, "points", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "MaskSplinePoint");
  api_def_prop_collection_stype(prop, NULL, "points", "tot_point");
  api_def_prop_ui_text(prop, "Points", "Collection of points");
  api_def_prop_srna(prop, "MaskSplinePoints");
}

static void api_def_mask_layer(DuneApi *dapi)
{
  static const EnumPropItem masklay_blend_mode_items[] = {
      {MASK_MERGE_ADD, "MERGE_ADD", 0, "Merge Add", ""},
      {MASK_MERGE_SUBTRACT, "MERGE_SUBTRACT", 0, "Merge Subtract", ""},
      {MASK_ADD, "ADD", 0, "Add", ""},
      {MASK_SUBTRACT, "SUBTRACT", 0, "Subtract", ""},
      {MASK_LIGHTEN, "LIGHTEN", 0, "Lighten", ""},
      {MASK_DARKEN, "DARKEN", 0, "Darken", ""},
      {MASK_MUL, "MUL", 0, "Multiply", ""},
      {MASK_REPLACE, "REPLACE", 0, "Replace", ""},
      {MASK_DIFFERENCE, "DIFFERENCE", 0, "Difference", ""},
      {0, NULL, 0, NULL, NULL},
  };

  ApiStruct *sapi;
  ApiProp *prop;

  api_def_maskSpline(dapi);
  api_def_mask_splines(dapi);
  api_def_maskSplinePoints(dapi);

  //line immediately belownisneither dapi or sapi 
  sapi = api_def_struct(dapi, "MaskLayer", NULL);
  api_def_struct_ui_text(sapi, "Mask Layer", "Single layer used for masking pixels");
  api_def_struct_path_fn(sapi, "api_MaskLayer_path");

  /* name */
  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Unique name of layer");
  api_def_prop_string_fns(prop, NULL, NULL, "api_MaskLayer_name_set");
  api_def_prop_string_maxlength(prop, MAX_ID_NAME - 2);
  api_def_prop_update(prop, 0, "api_Mask_update_data");
  api_def_struct_name_prop(sapi, prop);

  /* splines */
  prop = api_def_prop(sapi, "splines", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_fns(prop,
                              "api_MaskLayer_splines_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_struct_type(prop, "MaskSpline");
  api_def_prop_ui_text(prop, "Splines", "Collection of splines which defines this layer");
  api_def_prop_sapi(prop, "MaskSplines");

  /* restrict */
  prop = api_def_prop(sapi, "hide", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "visibility_flag", MASK_HIDE_VIEW);
  api_def_prop_ui_text(prop, "Restrict View", "Restrict visibility in the viewport");
  api_def_prop_ui_icon(prop, ICON_RESTRICT_VIEW_OFF, -1);
  api_def_prop_update(prop, NC_MASK | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "hide_select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "visibility_flag", MASK_HIDE_SELECT);
  api_def_prop_ui_text(prop, "Restrict Select", "Restrict selection in the viewport");
  api_def_prop_ui_icon(prop, ICON_RESTRICT_SELECT_OFF, -1);
  api_def_prop_update(prop, NC_MASK | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "hide_render", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "visibility_flag", MASK_HIDE_RENDER);
  api_def_prop_ui_text(prop, "Restrict Render", "Restrict renderability");
  api_def_prop_ui_icon(prop, ICON_RESTRICT_RENDER_OFF, -1);
  api_def_prop_update(prop, NC_MASK | NA_EDITED, NULL);

  /* Select (for dope-sheet). */
  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", MASK_LAYERFLAG_SELECT);
  api_def_prop_ui_text(prop, "Select", "Layer is selected for editing in the Dope Sheet");
  //  api_def_prop_update(prop, NC_SCREEN | ND_MASK, NULL);

  /* render settings */
  prop = api_def_prop(sapi, "alpha", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "alpha");
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.1, 3);
  api_def_prop_ui_text(prop, "Opacity", "Render Opacity");
  api_def_prop_update(prop, NC_MASK | NA_EDITED, NULL);

  /* weight interpolation */
  prop = api_def_prop(sapi, "blend", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "blend");
  api_def_prop_enum_items(prop, masklay_blend_mode_items);
  api_def_prop_ui_text(prop, "Blend", "Method of blending mask layers");
  api_def_prop_update(prop, 0, "api_Mask_update_data");
  api_def_prop_update(prop, NC_MASK | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "invert", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "blend_flag", MASK_BLENDFLAG_INVERT);
  api_def_prop_ui_text(prop, "Restrict View", "Invert the mask black/white");
  api_def_prop_update(prop, NC_MASK | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "falloff", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "falloff");
  api_def_prop_enum_items(prop, api_enum_proportional_falloff_curve_only_items);
  api_def_prop_ui_text(prop, "Falloff", "Falloff type the feather");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_CURVE_LEGACY); /* Abusing id_curve :/ */
  api_def_prop_update(prop, NC_MASK | NA_EDITED, NULL);

  /* filling options */
  prop = api_def_prop(sapi, "use_fill_holes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", MASK_LAYERFLAG_FILL_DISCRETE);
  api_def_prop_ui_text(
      prop, "Calculate Holes", "Calculate holes when filling overlapping curves");
  api_def_prop_update(prop, NC_MASK | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "use_fill_overlap", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", MASK_LAYERFLAG_FILL_OVERLAP);
  api_def_prop_ui_text(
      prop, "Calculate Overlap", "Calculate self intersections and overlap before filling");
  api_def_prop_update(prop, NC_MASK | NA_EDITED, NULL);
}

static void api_def_masklayers(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  api_def_prop_sapi(cprop, "MaskLayers");
  sapi = api_def_struct(dapi, "MaskLayers", NULL);
  api_def_struct_stype(sapi, "Mask");
  api_def_struct_ui_text(sapi, "Mask Layers", "Collection of layers used by mask");

  fn = api_def_fn(sapi, "new", "api_Mask_layers_new");
  api_def_fn_ui_description(fn, "Add layer to this mask");
  api_def_string(fn, "name", NULL, 0, "Name", "Name of new layer");
  parm = api_def_ptr(fn, "layer", "MaskLayer", "", "New mask layer");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Mask_layers_remove");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove layer from this mask");
  parm = api_def_ptr(fn, "layer", "MaskLayer", "", "Shape to be removed");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  /* clear all layers */
  fn = api_def_fn(sapi, "clear", "api_Mask_layers_clear");
  api_def_fn_ui_description(fn, "Remove all mask layers");

  /* active layer */
  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "MaskLayer");
  api_def_prop_ptr_fns(
      prop, "api_Mask_layer_active_get", "api_Mask_layer_active_set", NULL, NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop, "Active Shape", "Active layer in this mask");
}

static void api_def_mask(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_mask_layer(dapi);

  sapi = api_def_struct(dapi, "Mask", "ID");
  api_def_struct_ui_text(sapi, "Mask", "Mask data-block defining mask for compositing");
  api_def_struct_ui_icon(sapi, ICON_MOD_MASK);

  /* mask layers */
  prop = api_def_prop(sapi, "layers", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_fns(prop,
                              "api_Mask_layers_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_struct_type(prop, "MaskLayer");
  api_def_prop_ui_text(prop, "Layers", "Collection of layers which defines this mask");
  api_def_masklayers(dapi, prop);

  /* active masklay index */
  prop = api_def_prop(sapi, "active_layer_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "masklay_act");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_fns(prop,
                       "api_Mask_layer_active_index_get",
                       "api_Mask_layer_active_index_set",
                       "api_Mask_layer_active_index_range");
  api_def_prop_ui_text(
      prop, "Active Shape Index", "Index of active layer in list of all mask's layers");
  api_def_prop_update(prop, NC_MASK | ND_DRAW, NULL);

  /* frame range */
  prop = api_def_prop(sapi, "frame_start", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "sfra");
  api_def_prop_int_fns(prop, NULL, "api_Mask_start_frame_set", NULL);
  api_def_prop_range(prop, MINFRAME, MAXFRAME);
  api_def_prop_ui_text(prop, "Start Frame", "First frame of the mask (used for sequencer)");
  api_def_prop_update(prop, NC_MASK | ND_DRAW, NULL);

  prop = api_def_prop(sapi, "frame_end", PROP_INT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "efra");
  api_def_prop_int_fnss(prop, NULL, "api_Mask_end_frame_set", NULL);
  api_def_prop_range(prop, MINFRAME, MAXFRAME);
  api_def_prop_ui_text(prop, "End Frame", "Final frame of the mask (used for sequencer)");
  api_def_prop_update(prop, NC_MASK | ND_DRAW, NULL);

  /* pointers */
  api_def_animdata_common(sapi);
}

void api_def_mask(DuneApi *dapi)
{
  api_def_maskParent(dapi);
  rna_def_mask(dapi);
}

#endif
