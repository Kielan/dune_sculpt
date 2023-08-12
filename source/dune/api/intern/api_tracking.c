#include <limits.h>
#include <stdlib.h>

#include "mem_guardedalloc.h"

#include "dune_movieclip.h"
#include "dune_node_tree_update.h"
#include "dune_tracking.h"

#include "lang_translation.h"

#include "api_access.h"
#include "api_define.h"

#include "api_internal.h"

#include "types_defaults.h"
#include "types_movieclip.h"
#include "types_object.h" /* SELECT */
#include "types_scene.h"

#include "wm_types.h"

#ifdef API_RUNTIME

#  include "lib_math.h"

#  include "types_anim.h"

#  include "dune_anim_data.h"
#  include "dune_animsys.h"
#  include "dune_node.h"
#  include "dune_report.h"

#  include "graph.h"

#  include "imbuf.h"

#  include "wm_api.h"

static char *api_tracking_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("tracking");
}

static void api_tracking_defaultSettings_patternUpdate(Main *UNUSED(main),
                                                       Scene *UNUSED(scene),
                                                       ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTracking *tracking = &clip->tracking;
  MovieTrackingSettings *settings = &tracking->settings;

  if (settings->default_search_size < settings->default_pattern_size) {
    settings->default_search_size = settings->default_pattern_size;
  }
}

static void api_tracking_defaultSettings_searchUpdate(Main *UNUSED(main),
                                                      Scene *UNUSED(scene),
                                                      ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTracking *tracking = &clip->tracking;
  MovieTrackingSettings *settings = &tracking->settings;

  if (settings->default_pattern_size > settings->default_search_size) {
    settings->default_pattern_size = settings->default_search_size;
  }
}

static char *api_trackingTrack_path(const ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingTrack *track = (MovieTrackingTrack *)ptr->data;
  /* Escaped object name, escaped track name, rest of the path. */
  char api_path[MAX_NAME * 4 + 64];
  dune_tracking_get_api_path_for_track(&clip->tracking, track, api_path, sizeof(api_path));
  return lib_strdup(api_path);
}

static void api_trackingTracks_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingObject *tracking_camera_object = dune_tracking_object_get_camera(&clip->tracking);

  api_iter_list_begin(iter, &tracking_camera_object->tracks, NULL);
}

static void api_trackingPlaneTracks_begin(CollectionPropIter *iter, PointerRNA *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingObject *tracking_camera_object = dune_tracking_object_get_camera(&clip->tracking);

  api_iter_list_begin(iter, &tracking_camera_object->plane_tracks, NULL);
}

static ApiPtt api_trackingReconstruction_get(ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingObject *tracking_camera_object = dune_tracking_object_get_camera(&clip->tracking);

  return api_ptr_inherit_refine(
      ptr, &Api_MovieTrackingReconstruction, &tracking_camera_object->reconstruction);
}

static void api_trackingObjects_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;

  api_iter_list_begin(iter, &clip->tracking.objects, NULL);
}

static int api_tracking_active_object_index_get(ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;

  return clip->tracking.objectnr;
}

static void api_tracking_active_object_index_set(ApiPtr *ptr, int value)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;

  clip->tracking.objectnr = value;
  dune_tracking_dopesheet_tag_update(&clip->tracking);
}

static void api_tracking_active_object_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;

  *min = 0;
  *max = max_ii(0, clip->tracking.tot_object - 1);
}

static ApiPtr api_tracking_active_track_get(ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  const MovieTrackingObject *tracking_object = dune_tracking_object_get_active(&clip->tracking);

  return api_ptr_inherit_refine(ptr, &Api_MovieTrackingTrack, tracking_object->active_track);
}

static void api_tracking_active_track_set(ApiPtr *ptr,
                                          ApiPtr value,
                                          struct ReportList *reports)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingTrack *track = (MovieTrackingTrack *)value.data;
  MovieTrackingObject *tracking_object = dune_tracking_object_get_active(&clip->tracking);
  int index = lib_findindex(&tracking_object->tracks, track);

  if (index != -1) {
    tracking_object->active_track = track;
  }
  else {
    dunr_reportf(reports,
                 RPT_ERROR,
                 "Track '%s' is not found in the tracking object %s",
                 track->name,
                 tracking_object->name);
  }
}

static ApiPtr api_tracking_active_plane_track_get(ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  const MovieTrackingObject *tracking_object = dune_tracking_object_get_active(&clip->tracking);

  return api_ptr_inherit_refine(
      ptr, &Api_MovieTrackingPlaneTrack, tracking_object->active_plane_track);
}

static void api_tracking_active_plane_track_set(ApiPtr *ptr,
                                                ApiPtr value,
                                                struct ReportList *reports)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingPlaneTrack *plane_track = (MovieTrackingPlaneTrack *)value.data;
  MovieTrackingObject *tracking_object = dune_tracking_object_get_active(&clip->tracking);
  int index = lib_findindex(&tracking_object->plane_tracks, plane_track);

  if (index != -1) {
    tracking_object->active_plane_track = plane_track;
  }
  else {
    dune_reportf(reports,
                 RPT_ERROR,
                 "Plane track '%s' is not found in the tracking object %s",
                 plane_track->name,
                 tracking_object->name);
  }
}

static ApiPtr api_tracking_object_active_track_get(ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  const MovieTrackingObject *tracking_object = dune_tracking_object_get_active(&clip->tracking);

  return api_ptr_inherit_refine(ptr, &Api_MovieTrackingTrack, tracking_object->active_track);
}

static void api_tracking_object_active_track_set(ApiPtr *ptr,
                                                 ApiPtr value,
                                                 struct ReportList *reports)
{
  MovieTrackingTrack *track = (MovieTrackingTrack *)value.data;
  MovieTrackingObject *tracking_object = (MovieTrackingObject *)ptr->data;
  int index = lib_findindex(&tracking_object->tracks, track);

  if (index != -1) {
    tracking_object->active_track = track;
  }
  else {
    dune_reportf(reports,
                RPT_ERROR,
                "Track '%s' is not found in the tracking object %s",
                track->name,
                tracking_object->name);
  }
}

static ApiPtr api_tracking_object_active_plane_track_get(ApiPtr *ptr)
{
  MovieTrackingObject *tracking_object = (MovieTrackingObject *)ptr->data;

  return api_ptr_inherit_refine(
      ptr, &Api_MovieTrackingPlaneTrack, tracking_object->active_plane_track);
}

static void api_tracking_object_active_plane_track_set(ApiPtr *ptr,
                                                       ApiPtr value,
                                                       struct ReportList *reports)
{
  MovieTrackingPlaneTrack *plane_track = (MovieTrackingPlaneTrack *)value.data;
  MovieTrackingObject *tracking_object = (MovieTrackingObject *)ptr->data;
  int index = lib_findindex(&tracking_object->plane_tracks, plane_track);

  if (index != -1) {
    tracking_object->active_plane_track = plane_track;
  }
  else {
    dune_reportf(reports,
                RPT_ERROR,
                "Plane track '%s' is not found in the tracking object %s",
                plane_track->name,
                tracking_object->name);
  }
}

static void api_trackingTrack_name_set(ApiPtr *ptr, const char *value)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingTrack *track = (MovieTrackingTrack *)ptr->data;
  MovieTrackingObject *tracking_object = dune_tracking_find_object_for_track(&clip->tracking,
                                                                            track);
  /* Store old name, for the animation fix later. */
  char old_name[sizeof(track->name)];
  STRNCPY(old_name, track->name);
  /* Update the name, */
  STRNCPY(track->name, value);
  dune_tracking_track_unique_name(&tracking_object->tracks, track);
  /* Fix animation paths. */
  AnimData *adt = dune_animdata_from_id(&clip->id);
  if (adt != NULL) {
    char api_path[MAX_NAME * 2 + 64];
    dune_tracking_get_api_path_prefix_for_track(&clip->tracking, track, api_path, sizeof(api_path));
    dune_animdata_fix_paths_rename(&clip->id, adt, NULL, api_path, old_name, track->name, 0, 0, 1);
  }
}

static bool api_trackingTrack_select_get(ApiPtr *ptr)
{
  MovieTrackingTrack *track = (MovieTrackingTrack *)ptr->data;

  return TRACK_SELECTED(track);
}

static void api_trackingTrack_select_set(ApiPtr *ptr, bool value)
{
  MovieTrackingTrack *track = (MovieTrackingTrack *)ptr->data;

  if (value) {
    track->flag |= SELECT;
    track->pat_flag |= SELECT;
    track->search_flag |= SELECT;
  } else {
    track->flag &= ~SELECT;
    track->pat_flag &= ~SELECT;
    track->search_flag &= ~SELECT;
  }
}

static void api_trackingPlaneMarker_frame_set(ApiPtr *ptr, int value)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTracking *tracking = &clip->tracking;
  MovieTrackingPlaneMarker *plane_marker = (MovieTrackingPlaneMarker *)ptr->data;
  MovieTrackingPlaneTrack *plane_track_of_marker = NULL;

  LIST_FOREACH (MovieTrackingObject *, tracking_object, &tracking->objects) {
    LIST_FOREACH (MovieTrackingPlaneTrack *, plane_track, &tracking_object->plane_tracks) {
      if (plane_marker >= plane_track->markers &&
          plane_marker < plane_track->markers + plane_track->markersnr)
      {
        plane_track_of_marker = plane_track;
        break;
      }
    }

    if (plane_track_of_marker) {
      break;
    }
  }

  if (plane_track_of_marker) {
    MovieTrackingPlaneMarker new_plane_marker = *plane_marker;
    new_plane_marker.framenr = value;

    dune_tracking_plane_marker_delete(plane_track_of_marker, plane_marker->framenr);
    dune_tracking_plane_marker_insert(plane_track_of_marker, &new_plane_marker);
  }
}

static char *api_trackingPlaneTrack_path(const ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingPlaneTrack *plane_track = (MovieTrackingPlaneTrack *)ptr->data;
  /* Escaped object name, escaped track name, rest of the path. */
  char api_path[MAX_NAME * 4 + 64];
  dune_tracking_get_api_path_for_plane_track(
      &clip->tracking, plane_track, api_path, sizeof(api_path));
  return lib_strdup(api_path);
}

static void api_trackingPlaneTrack_name_set(ApiPtr *ptr, const char *value)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingPlaneTrack *plane_track = (MovieTrackingPlaneTrack *)ptr->data;
  MovieTrackingObject *tracking_object = dune_tracking_find_object_for_plane_track(&clip->tracking,
                                                                                  plane_track);
  /* Store old name, for the animation fix later. */
  char old_name[sizeof(plane_track->name)];
  STRNCPY(old_name, plane_track->name);
  /* Update the name, */
  STRNCPY(plane_track->name, value);
  dune_tracking_plane_track_unique_name(&tracking_object->plane_tracks, plane_track);
  /* Fix animation paths. */
  AnimData *adt = dune_animdata_from_id(&clip->id);
  if (adt != NULL) {
    char api_path[MAX_NAME * 2 + 64];
    dune_tracking_get_api_path_prefix_for_plane_track(
        &clip->tracking, plane_track, api_path, sizeof(rna_path));
    dune_animdata_fix_paths_rename(
        &clip->id, adt, NULL, api_path, old_name, plane_track->name, 0, 0, 1);
  }
}

static char *api_trackingCamera_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("tracking.camera");
}

static float api_trackingCamera_focal_mm_get(PointerRNA *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingCamera *camera = &clip->tracking.camera;
  float val = camera->focal;

  if (clip->lastsize[0]) {
    val = val * camera->sensor_width / (float)clip->lastsize[0];
  }

  return val;
}

static void api_trackingCamera_focal_mm_set(ApiPtr *ptr, float value)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingCamera *camera = &clip->tracking.camera;

  if (clip->lastsize[0]) {
    value = clip->lastsize[0] * value / camera->sensor_width;
  }

  if (value >= 0.0001f) {
    camera->focal = value;
  }
}

static void api_trackingCamera_principal_point_pixels_get(ApiPtr *ptr,
                                                          float *r_principal_point_pixels)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  dune_tracking_camera_principal_point_pixel_get(clip, r_principal_point_pixels);
}

static void api_trackingCamera_principal_point_pixels_set(ApiPtr *ptr,
                                                          const float *principal_point_pixels)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  dune_tracking_camera_principal_point_pixel_set(clip, principal_point_pixels);
}

static char *api_trackingStabilization_path(const ApiPtr *UNUSED(ptr))
{
  return lib_strdup("tracking.stabilization");
}

static int api_track_2d_stabilization(CollectionPropIter *UNUSED(iter), void *data)
{
  MovieTrackingTrack *track = (MovieTrackingTrack *)data;

  if ((track->flag & TRACK_USE_2D_STAB) == 0) {
    return 1;
  }

  return 0;
}

static int api_track_2d_stabilization_rotation(CollectionPropIter *UNUSED(iter),
                                               void *data)
{
  MovieTrackingTrack *track = (MovieTrackingTrack *)data;

  if ((track->flag & TRACK_USE_2D_STAB_ROT) == 0) {
    return 1;
  }

  return 0;
}

static void api_tracking_stabTracks_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingObject *tracking_camera_object = dune_tracking_object_get_camera(&clip->tracking);
  api_iter_list_begin(iter, &tracking_camera_object->tracks, api_track_2d_stabilization);
}

static int api_tracking_stabTracks_active_index_get(ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  return clip->tracking.stabilization.act_track;
}

static void api_tracking_stabTracks_active_index_set(ApiPtr *ptr, int value)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  clip->tracking.stabilization.act_track = value;
}

static void api_tracking_stabTracks_active_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;

  *min = 0;
  *max = max_ii(0, clip->tracking.stabilization.tot_track - 1);
}

static void api_tracking_stabRotTracks_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingObject *tracking_camera_object = dune_tracking_object_get_camera(&clip->tracking);
  apu_iter_list_begin(
      iter, &tracking_camera_object->tracks, api_track_2d_stabilization_rotation);
}

static int api_tracking_stabRotTracks_active_index_get(ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  return clip->tracking.stabilization.act_rot_track;
}

static void api_tracking_stabRotTracks_active_index_set(ApiPtr *ptr, int value)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  clip->tracking.stabilization.act_rot_track = value;
}

static void api_tracking_stabRotTracks_active_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;

  *min = 0;
  *max = max_ii(0, clip->tracking.stabilization.tot_rot_track - 1);
}

static void api_tracking_flushUpdate(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;

  dune_ntree_update_tag_id_changed(main, &clip->id);
  dune_ntree_update_main(main, NULL);

  wm_main_add_notifier(NC_SCENE | ND_NODES, NULL);
  wm_main_add_notifier(NC_SCENE, NULL);
  graph_id_tag_update(&clip->id, 0);
}

static void api_tracking_resetIntrinsics(Main *UNUSED(main),
                                         Scene *UNUSED(scene),
                                         ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTracking *tracking = &clip->tracking;

  if (tracking->camera.intrinsics) {
    dune_tracking_distortion_free(tracking->camera.intrinsics);
    tracking->camera.intrinsics = NULL;
  }
}

static void api_trackingObject_tracks_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  MovieTrackingObject *tracking_object = (MovieTrackingObject *)ptr->data;
  api_iter_list_begin(iter, &tracking_object->tracks, NULL);
}

static void api_trackingObject_plane_tracks_begin(CollectionPropIter *iter,
                                                  ApiPtr *ptr)
{
  MovieTrackingObject *tracking_object = (MovieTrackingObject *)ptr->data;
  api_iter_list_begin(iter, &tracking_object->plane_tracks, NULL);
}

static ApiPtr api_trackingObject_reconstruction_get(ApiPtr *ptr)
{
  MovieTrackingObject *tracking_object = (MovieTrackingObject *)ptr->data;
  return api_prr_inherit_refine(
      ptr, &Api_MovieTrackingReconstruction, &tracking_object->reconstruction);
}

static ApiPtr api_tracking_active_object_get(ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingObject *tracking_object = lib_findlink(&clip->tracking.objects,
                                                      clip->tracking.objectnr);

  return rna_pointer_inherit_refine(ptr, &RNA_MovieTrackingObject, tracking_object);
}

static void api_tracking_active_object_set(ApiPtr *ptr,
                                           ApiPtr value,
                                           struct ReportList *UNUSED(reports))
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingObject *tracking_object = (MovieTrackingObject *)value.data;
  const int index = lib_findindex(&clip->tracking.objects, tracking_object);

  if (index != -1) {
    clip->tracking.objectnr = index;
  } else {
    clip->tracking.objectnr = 0;
  }
}

static void api_trackingObject_name_set(ApiPtr *ptr, const char *value)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingObject *tracking_object = (MovieTrackingObject *)ptr->data;

  STRNCPY(tracking_object->name, value);

  dune_tracking_object_unique_name(&clip->tracking, tracking_object);
}

static void api_trackingObject_flushUpdate(Main *UNUSED(main),
                                           Scene *UNUSED(scene),
                                           ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;

  wm_main_add_notifier(NC_OBJECT | ND_TRANSFORM, NULL);
  graph_id_tag_update(&clip->id, 0);
}

static void api_trackingMarker_frame_set(ApiPtr *ptr, int value)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTracking *tracking = &clip->tracking;
  MovieTrackingMarker *marker = (MovieTrackingMarker *)ptr->data;
  MovieTrackingTrack *track_of_marker = NULL;

  LIST_FOREACH (MovieTrackingObject *, tracking_object, &tracking->objects) {
    LIST_FOREACH (MovieTrackingTrack *, track, &tracking_object->tracks) {
      if (marker >= track->markers && marker < track->markers + track->markersnr) {
        track_of_marker = track;
        break;
      }
    }

    if (track_of_marker) {
      break;
    }
  }

  if (track_of_marker) {
    MovieTrackingMarker new_marker = *marker;
    new_marker.framenr = value;

    dune_tracking_marker_delete(track_of_marker, marker->framenr);
    dune_tracking_marker_insert(track_of_marker, &new_marker);
  }
}

static void api_tracking_markerPattern_update(Main *UNUSED(main),
                                              Scene *UNUSED(scene),
                                              ApiPtr *ptr)
{
  MovieTrackingMarker *marker = (MovieTrackingMarker *)ptr->data;

  dune_tracking_marker_clamp_search_size(marker);
}

static void api_tracking_markerSearch_update(Main *UNUSED(main),
                                             Scene *UNUSED(scene),
                                             ApiPtr *ptr)
{
  MovieTrackingMarker *marker = (MovieTrackingMarker *)ptr->data;

  dune_tracking_marker_clamp_search_size(marker);
}

static void api_tracking_markerPattern_boundbox_get(ApiPtr *ptr, float *values)
{
  MovieTrackingMarker *marker = (MovieTrackingMarker *)ptr->data;
  float min[2], max[2];

  dune_tracking_marker_pattern_minmax(marker, min, max);

  copy_v2_v2(values, min);
  copy_v2_v2(values + 2, max);
}

static void api_trackingDopesheet_tagUpdate(Main *UNUSED(main),
                                            Scene *UNUSED(scene),
                                            ApiPtr *ptr)
{
  MovieClip *clip = (MovieClip *)ptr->owner_id;
  MovieTrackingDopesheet *dopesheet = &clip->tracking.dopesheet;

  dopesheet->ok = 0;
}

/* API */
static MovieTrackingTrack *add_track_to_base(
    MovieClip *clip, MovieTracking *tracking, List *tracksbase, const char *name, int frame)
{
  int width, height;
  MovieClipUser user = *type_struct_default_get(MovieClipUser);
  MovieTrackingTrack *track;

  user.framenr = 1;

  dune_movieclip_get_size(clip, &user, &width, &height);

  track = dune_tracking_track_add(tracking, tracksbase, 0, 0, frame, width, height);

  if (name && name[0]) {
    STRNCPY(track->name, name);
    dune_tracking_track_unique_name(tracksbase, track);
  }

  return track;
}

static MovieTrackingTrack *api_trackingTracks_new(Id *id,
                                                  MovieTracking *tracking,
                                                  const char *name,
                                                  int frame)
{
  MovieClip *clip = (MovieClip *)id;
  MovieTrackingObject *tracking_camera_object = dune_tracking_object_get_camera(&clip->tracking);
  MovieTrackingTrack *track = add_track_to_base(
      clip, tracking, &tracking_camera_object->tracks, name, frame);

  wm_main_add_notifier(NC_MOVIECLIP | NA_EDITED, clip);

  return track;
}

static MovieTrackingTrack *api_trackingObject_tracks_new(Id *id,
                                                         MovieTrackingObject *tracking_object,
                                                         const char *name,
                                                         int frame)
{
  MovieClip *clip = (MovieClip *)id;
  MovieTrackingTrack *track = add_track_to_base(
      clip, &clip->tracking, &tracking_object->tracks, name, frame);

  wm_main_add_notifier(NC_MOVIECLIP | NA_EDITED, NULL);

  return track;
}

static MovieTrackingObject *api_trackingObject_new(MovieTracking *tracking, const char *name)
{
  MovieTrackingObject *tracking_object = dune_tracking_object_add(tracking, name);

  wm_main_add_notifier(NC_MOVIECLIP | NA_EDITED, NULL);

  return tracking_object;
}

static void api_trackingObject_remove(MovieTracking *tracking,
                                      ReportList *reports,
                                      ApiPtr *object_ptr)
{
  MovieTrackingObject *tracking_object = object_ptr->data;
  if (dune_tracking_object_delete(tracking, tracking_object) == false) {
    dune_reportf(reports, RPT_ERROR, "MovieTracking '%s' cannot be removed", tracking_object->name);
    return;
  }

  API_PTR_INVALIDATE(object_ptr);

  wm_main_add_notifier(NC_MOVIECLIP | NA_EDITED, NULL);
}

static MovieTrackingMarker *api_trackingMarkers_find_frame(MovieTrackingTrack *track,
                                                           int framenr,
                                                           bool exact)
{
  if (exact) {
    return dune_tracking_marker_get_exact(track, framenr);
  } else {
    return dune_tracking_marker_get(track, framenr);
  }
}

static MovieTrackingMarker *api_trackingMarkers_insert_frame(MovieTrackingTrack *track,
                                                             int framenr,
                                                             float co[2])
{
  MovieTrackingMarker marker, *new_marker;

  memset(&marker, 0, sizeof(marker));
  marker.framenr = framenr;
  copy_v2_v2(marker.pos, co);

  /* a bit arbitrary, but better than creating markers with zero pattern
   * which is forbidden actually */
  copy_v2_v2(marker.pattern_corners[0], track->markers[0].pattern_corners[0]);
  copy_v2_v2(marker.pattern_corners[1], track->markers[0].pattern_corners[1]);
  copy_v2_v2(marker.pattern_corners[2], track->markers[0].pattern_corners[2]);
  copy_v2_v2(marker.pattern_corners[3], track->markers[0].pattern_corners[3]);

  new_marker = dune_tracking_marker_insert(track, &marker);

  wm_main_add_notifier(NC_MOVIECLIP | NA_EDITED, NULL);

  return new_marker;
}

static void api_trackingMarkers_delete_frame(MovieTrackingTrack *track, int framenr)
{
  if (track->markersnr == 1) {
    return;
  }

  dune_tracking_marker_delete(track, framenr);

  wm_main_add_notifier(NC_MOVIECLIP | NA_EDITED, NULL);
}

static MovieTrackingPlaneMarker *api_trackingPlaneMarkers_find_frame(
    MovieTrackingPlaneTrack *plane_track, int framenr, bool exact)
{
  if (exact) {
    return dune_tracking_plane_marker_get_exact(plane_track, framenr);
  } else {
    return dune_tracking_plane_marker_get(plane_track, framenr);
  }
}

static MovieTrackingPlaneMarker *api_trackingPlaneMarkers_insert_frame(
    MovieTrackingPlaneTrack *plane_track, int framenr)
{
  MovieTrackingPlaneMarker plane_marker, *new_plane_marker;

  memset(&plane_marker, 0, sizeof(plane_marker));
  plane_marker.framenr = framenr;

  /* a bit arbitrary, but better than creating zero markers */
  copy_v2_v2(plane_marker.corners[0], plane_track->markers[0].corners[0]);
  copy_v2_v2(plane_marker.corners[1], plane_track->markers[0].corners[1]);
  copy_v2_v2(plane_marker.corners[2], plane_track->markers[0].corners[2]);
  copy_v2_v2(plane_marker.corners[3], plane_track->markers[0].corners[3]);

  new_plane_marker = dune_tracking_plane_marker_insert(plane_track, &plane_marker);

  wm_main_add_notifier(NC_MOVIECLIP | NA_EDITED, NULL);

  return new_plane_marker;
}

static void api_trackingPlaneMarkers_delete_frame(MovieTrackingPlaneTrack *plane_track,
                                                  int framenr)
{
  if (plane_track->markersnr == 1) {
    return;
  }

  dune_tracking_plane_marker_delete(plane_track, framenr);

  wm_main_add_notifier(NC_MOVIECLIP | NA_EDITED, NULL);
}

static MovieTrackingObject *find_object_for_reconstruction(
    MovieTracking *tracking, MovieTrackingReconstruction *reconstruction)
{
  LIST_FOREACH (MovieTrackingObject *, tracking_object, &tracking->objects) {
    if (&tracking_object->reconstruction == reconstruction) {
      return tracking_object;
    }
  }

  return NULL;
}

static MovieReconstructedCamera *api_trackingCameras_find_frame(
    Id *id, MovieTrackingReconstruction *reconstruction, int framenr)
{
  MovieClip *clip = (MovieClip *)id;
  MovieTracking *tracking = &clip->tracking;
  MovieTrackingObject *tracking_object = find_object_for_reconstruction(tracking, reconstruction);
  return dune_tracking_camera_get_reconstructed(tracking, tracking_object, framenr);
}

static void api_trackingCameras_matrix_from_frame(Id *id,
                                                  MovieTrackingReconstruction *reconstruction,
                                                  int framenr,
                                                  float matrix[16])
{
  float mat[4][4];

  MovieClip *clip = (MovieClip *)id;
  MovieTracking *tracking = &clip->tracking;
  MovieTrackingObject *tracking_object = find_object_for_reconstruction(tracking, reconstruction);
  dune_tracking_camera_get_reconstructed_interpolate(tracking, tracking_object, framenr, mat);

  memcpy(matrix, mat, sizeof(float[4][4]));
}

#else

static const EnumPropItem tracker_motion_model[] = {
    {TRACK_MOTION_MODEL_HOMOGRAPHY,
     "Perspective",
     0,
     "Perspective",
     "Search for markers that are perspectively deformed (homography) between frames"},
    {TRACK_MOTION_MODEL_AFFINE,
     "Affine",
     0,
     "Affine",
     "Search for markers that are affine-deformed (t, r, k, and skew) between frames"},
    {TRACK_MOTION_MODEL_TRANSLATION_ROTATION_SCALE,
     "LocRotScale",
     0,
     "Location, Rotation & Scale",
     "Search for markers that are translated, rotated, and scaled between frames"},
    {TRACK_MOTION_MODEL_TRANSLATION_SCALE,
     "LocScale",
     0,
     "Location & Scale",
     "Search for markers that are translated and scaled between frames"},
    {TRACK_MOTION_MODEL_TRANSLATION_ROTATION,
     "LocRot",
     0,
     "Location & Rotation",
     "Search for markers that are translated and rotated between frames"},
    {TRACK_MOTION_MODEL_TRANSLATION,
     "Loc",
     0,
     "Location",
     "Search for markers that are translated between frames"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem pattern_match_items[] = {
    {TRACK_MATCH_KEYFRAME, "KEYFRAME", 0, "Keyframe", "Track pattern from keyframe to next frame"},
    {TRACK_MATCH_PREVIOS_FRAME,
     "PREV_FRAME",
     0,
     "Previous frame",
     "Track pattern from current frame to next frame"},
    {0, NULL, 0, NULL, NULL},
};

static void api_def_trackingSettings(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem speed_items[] = {
      {0, "FASTEST", 0, "Fastest", "Track as fast as possible"},
      {TRACKING_SPEED_DOUBLE, "DOUBLE", 0, "Double", "Track with double speed"},
      {TRACKING_SPEED_REALTIME, "REALTIME", 0, "Realtime", "Track with realtime speed"},
      {TRACKING_SPEED_HALF, "HALF", 0, "Half", "Track with half of realtime speed"},
      {TRACKING_SPEED_QUARTER, "QUARTER", 0, "Quarter", "Track with quarter of realtime speed"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem cleanup_items[] = {
      {TRACKING_CLEAN_SELECT, "SELECT", 0, "Select", "Select unclean tracks"},
      {TRACKING_CLEAN_DELETE_TRACK, "DELETE_TRACK", 0, "Delete Track", "Delete unclean tracks"},
      {TRACKING_CLEAN_DELETE_SEGMENT,
       "DELETE_SEGMENTS",
       0,
       "Delete Segments",
       "Delete unclean segments of tracks"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "MovieTrackingSettings", NULL);
  api_def_struct_ui_text(sapi, "Movie tracking settings", "Match moving settings");

  /* speed */
  prop = api_def_prop(sapi, "speed", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, speed_items);
  api_def_prop_ui_text(prop,
                       "Speed",
                       "Limit speed of tracking to make visual feedback easier "
                       "(this does not affect the tracking quality)");

  /* use keyframe selection */
  prop = api_def_prop(sapi, "use_keyframe_selection", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(
      prop, NULL, "reconstruction_flag", TRACKING_USE_KEYFRAME_SELECTION);
  api_def_prop_ui_text(prop,
                       "Keyframe Selection",
                       "Automatically select keyframes when solving camera/object motion");

  /* intrinsics refinement during bundle adjustment */
  prop = api_def_prop(sapi, "refine_intrinsics_focal_length", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "refine_camera_intrinsics", REFINE_FOCAL_LENGTH);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Refine Focal Length", "Refine focal length during camera solving");

  prop = api_def_prop(sapi, "refine_intrinsics_principal_point", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "refine_camera_intrinsics", REFINE_PRINCIPAL_POINT);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Refine Principal Point", "Refine principal point during camera solving");

  prop = api_def_prop(sapi, "refine_intrinsics_radial_distortion", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "refine_camera_intrinsics", REFINE_RADIAL_DISTORTION);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop,
                           "Refine Radial",
                           "Refine radial coefficients of distortion model during camera solving");

  prop = api_def_prop(
      sapi, "refine_intrinsics_tangential_distortion", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "refine_camera_intrinsics", REFINE_TANGENTIAL_DISTORTION);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop,
      "Refine Tangential",
      "Refine tangential coefficients of distortion model during camera solving");

  /* tool settings */

  /* distance */
  prop = api_def_prop(sapi, "distance", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "dist");
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_text(
      prop, "Distance", "Distance between two bundles used for scene scaling");

  /* frames count */
  prop = api_def_prop(sapi, "clean_frames", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "clean_frames");
  api_def_prop_range(prop, 0, INT_MAX);
  api_def_prop_ui_text(
      prop,
      "Tracked Frames",
      "Effect on tracks which are tracked less than the specified amount of frames");

  /* re-projection error */
  prop = api_def_prop(sapi, "clean_error", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "clean_error");
  api_def_prop_range(prop, 0, FLT_MAX);
  api_def_prop_ui_text(
      prop, "Reprojection Error", "Effect on tracks which have a larger re-projection error");

  /* cleanup action */
  prop = api_def_prop(sapi, "clean_action", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "clean_action");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, cleanup_items);
  api_def_prop_ui_text(prop, "Action", "Cleanup action to execute");

  /* solver settings */
  prop = api_def_prop(sapi, "use_tripod_solver", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "motion_flag", TRACKING_MOTION_TRIPOD);
  api_def_prop_ui_text(
      prop,
      "Tripod Motion",
      "Use special solver to track a stable camera position, such as a tripod");

  /* default_limit_frames */
  prop = api_def_prop(sapi, "default_frames_limit", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "default_frames_limit");
  api_def_prop_range(prop, 0, SHRT_MAX);
  api_def_prop_ui_text(
      prop, "Frames Limit", "Every tracking cycle, this number of frames are tracked");

  /* default_pattern_match */
  prop = api_def_prop(sapi, "default_pattern_match", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_stype(prop, NULL, "default_pattern_match");
  api_def_prop_enum_items(prop, pattern_match_items);
  api_def_prop_ui_text(
      prop, "Pattern Match", "Track pattern from given frame when tracking marker to next frame");

  /* default_margin */
  prop = api_def_prop(sapi, "default_margin", PROP_INT, PROP_PIXEL);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "default_margin");
  api_def_prop_range(prop, 0, 300);
  api_def_prop_ui_text(
      prop, "Margin", "Default distance from image boundary at which marker stops tracking");

  /* default_tracking_motion_model */
  prop = api_def_prop(sapi, "default_motion_model", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, tracker_motion_model);
  api_def_prop_ui_text(prop, "Motion Model", "Default motion model to use for tracking");

  /* default_use_brute */
  prop = api_def_prop(sapi, "use_default_brute", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "default_algorithm_flag", TRACK_ALGORITHM_FLAG_USE_BRUTE);
  api_def_prop_ui_text(
      prop, "Prepass", "Use a brute-force translation-only initialization when tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* default_use_brute */
  prop = api_def_prop(sapi, "use_default_mask", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "default_algorithm_flag", TRACK_ALGORITHM_FLAG_USE_MASK);
  api_def_prop_ui_text(
      prop,
      "Use Mask",
      "Use a grease pencil data-block as a mask to use only specified areas of pattern "
      "when tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* default_use_normalization */
  prop = api_def_prop(sapi, "use_default_normalization", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "default_algorithm_flag", TRACK_ALGORITHM_FLAG_USE_NORMALIZATION);
  api_def_prop_ui_text(
      prop, "Normalize", "Normalize light intensities while tracking (slower)");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* default minimal correlation */
  prop = api_def_prop(sapi, "default_correlation_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "default_minimum_correlation");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.05, 3);
  api_def_prop_ui_text(
      prop,
      "Correlation",
      "Default minimum value of correlation between matched pattern and reference "
      "that is still treated as successful tracking");

  /* default pattern size */
  prop = api_def_prop(sapi, "default_pattern_size", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "default_pattern_size");
  api_def_prop_range(prop, 5, 1000);
  api_def_prop_update(prop, 0, "api_tracking_defaultSettings_patternUpdate");
  api_def_prop_ui_text(prop, "Pattern Size", "Size of pattern area for newly created tracks");

  /* default search size */
  prop = api_def_prop(sapi, "default_search_size", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_sdna(prop, NULL, "default_search_size");
  api_def_prop_range(prop, 5, 1000);
  api_def_prop_update(prop, 0, "rna_tracking_defaultSettings_searchUpdate");
  api_def_prop_ui_text(prop, "Search Size", "Size of search area for newly created tracks");

  /* default use_red_channel */
  prop = api_def_prop(sapi, "use_default_red_channel", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "default_flag", TRACK_DISABLE_RED);
  api_def_prop_ui_text(prop, "Use Red Channel", "Use red channel from footage for tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* default_use_green_channel */
  prop = api_def_prop(sapi, "use_default_green_channel", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_sdna(prop, NULL, "default_flag", TRACK_DISABLE_GREEN);
  api_def_prop_ui_text(
      prop, "Use Green Channel", "Use green channel from footage for tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* default_use_blue_channel */
  prop = api_def_prop(sapi, "use_default_blue_channel", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "default_flag", TRACK_DISABLE_BLUE);
  api_def_prop_ui_text(prop, "Use Blue Channel", "Use blue channel from footage for tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  prop = api_def_prop(sapi, "default_weight", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Weight", "Influence of newly created track on a final solution");

  /* ** object tracking ** */

  /* object distance */
  prop = api_def_prop(sapi, "object_distance", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "object_distance");
  api_def_prop_ui_text(
      prop, "Distance", "Distance between two bundles used for object scaling");
  api_def_prop_range(prop, 0.001, 10000);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_range(prop, 0.001, 10000.0, 1, 3);
}

static void api_def_trackingCamera(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem distortion_model_items[] = {
      {TRACKING_DISTORTION_MODEL_POLYNOMIAL,
       "POLYNOMIAL",
       0,
       "Polynomial",
       "Radial distortion model which fits common cameras"},
      {TRACKING_DISTORTION_MODEL_DIVISION,
       "DIVISION",
       0,
       "Divisions",
       "Division distortion model which "
       "better represents wide-angle cameras"},
      {TRACKING_DISTORTION_MODEL_NUKE, "NUKE", 0, "Nuke", "Nuke distortion model"},
      {TRACKING_DISTORTION_MODEL_BROWN, "BROWN", 0, "Brown", "Brown-Conrady distortion model"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem camera_units_items[] = {
      {CAMERA_UNITS_PX, "PIXELS", 0, "px", "Use pixels for units of focal length"},
      {CAMERA_UNITS_MM, "MILLIMETERS", 0, "mm", "Use millimeters for units of focal length"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "MovieTrackingCamera", NULL);
  api_def_struct_path_fn(sapi, "api_trackingCamera_path");
  api_def_struct_ui_text(
      sapi, "Movie tracking camera data", "Match-moving camera data for tracking");

  /* Distortion model */
  prop = api_def_prop(sapi, "distortion_model", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, distortion_model_items);
  api_def_prop_ui_text(prop, "Distortion Model", "Distortion model used for camera lenses");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_resetIntrinsics");

  /* Sensor */
  prop = api_def_prop(salo, "sensor_width", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "sensor_width");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, 500.0f);
  api_def_prop_ui_text(prop, "Sensor", "Width of CCD sensor in millimeters");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* Focal Length */
  prop = api_def_prop(sapi, "focal_length", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "focal");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0001f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0001f, 5000.0f, 1, 2);
  api_def_prop_float_fns(
      prop, "api_trackingCamera_focal_mm_get", "api_trackingCamera_focal_mm_set", NULL);
  api_def_prop_ui_text(prop, "Focal Length", "Camera's focal length");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* Focal Length in pixels */
  prop = api_def_prop(sapi, "focal_length_pixels", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "focal");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0f, 5000.0f, 1, 2);
  api_def_prop_ui_text(prop, "Focal Length", "Camera's focal length");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* Units */
  prop = api_def_prop(sapi, "units", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "units");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, camera_units_items);
  api_def_prop_ui_text(prop, "Units", "Units used for camera focal length");

  /* Principal Point */
  prop = api_def_prop(sapi, "principal_point", PROP_FLOAT, PROP_NONE);
  api_def_prop_array(prop, 2);
  api_def_prop_float_stype(prop, NULL, "principal_point");
  api_def_prop_range(prop, -1, 1);
  api_def_prop_ui_range(prop, -1, 1, 0.1, 3);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Principal Point", "Optical center of lens");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* Principal Point, in pixels */
  prop = api_def_prop(sapi, "principal_point_pixels", PROP_FLOAT, PROP_PIXEL);
  api_def_prop_array(prop, 2);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_fns(prop,
                         "api_trackingCamera_principal_point_pixels_get",
                         "api_trackingCamera_principal_point_pixels_set",
                         NULL);
  api_def_prop_ui_text(prop, "Principal Point", "Optical center of lens in pixels");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* Radial distortion parameters */
  prop = api_def_prop(sapi, "k1", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "k1");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(
      prop, "K1", "First coefficient of third order polynomial radial distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  prop = api_def_prop(sapi, "k2", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "k2");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(
      prop, "K2", "Second coefficient of third order polynomial radial distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  prop = api_def_prop(sapi, "k3", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "k3");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(
      prop, "K3", "Third coefficient of third order polynomial radial distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  /* Division distortion parameters */
  prop = api_def_prop(sapi, "division_k1", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(prop, "K1", "First coefficient of second order division distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  prop = api_def_prop(sapi, "division_k2", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(prop, "K2", "Second coefficient of second order division distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  /* Nuke distortion parameters */
  prop = api_def_prop(sapi, "nuke_k1", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(prop, "K1", "First coefficient of second order Nuke distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  prop = api_def_prop(sapi, "nuke_k2", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(prop, "K2", "Second coefficient of second order Nuke distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  /* Brown-Conrady distortion parameters */
  prop = api_def_prop(sapi, "brown_k1", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(
      prop, "K1", "First coefficient of fourth order Brown-Conrady radial distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  prop = api_def_prop(sapi, "brown_k2", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(
      prop, "K2", "Second coefficient of fourth order Brown-Conrady radial distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  prop = api_def_prop(sapi, "brown_k3", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(
      prop, "K3", "Third coefficient of fourth order Brown-Conrady radial distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  prop = api_def_prop(sapi, "brown_k4", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(
      prop, "K4", "Fourth coefficient of fourth order Brown-Conrady radial distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  prop = api_def_prop(sapi, "brown_p1", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(
      prop, "P1", "First coefficient of second order Brown-Conrady tangential distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  prop = api_def_prop(sapi, "brown_p2", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_range(prop, -10, 10, 0.1, 3);
  api_def_prop_ui_text(
      prop, "P2", "Second coefficient of second order Brown-Conrady tangential distortion");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_flushUpdate");

  /* pixel aspect */
  prop = api_def_prop(sapi, "pixel_aspect", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "pixel_aspect");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.1f, FLT_MAX);
  api_def_prop_ui_range(prop, 0.1f, 5000.0f, 1, 2);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_text(prop, "Pixel Aspect Ratio", "Pixel aspect ratio");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");
}

static void api_def_trackingMarker(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static int boundbox_dimsize[] = {2, 2};

  sapi = api_def_struct(dapi, "MovieTrackingMarker", NULL);
  api_def_struct_ui_text(
      sapi, "Movie tracking marker data", "Match-moving marker data for tracking");

  /* position */
  prop = api_def_prop(sapi, "co", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 2);
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_float_stype(prop, NULL, "pos");
  api_def_prop_ui_text(prop, "Position", "Marker position at frame in normalized coordinates");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* frame */
  prop = api_def_prop(sapi, "frame", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "framenr");
  api_def_prop_ui_text(prop, "Frame", "Frame number marker is keyframed on");
  api_def_prop_int_fns(prop, NULL, "rna_trackingMarker_frame_set", NULL);
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* enable */
  prop = api_def_prop(sapi, "mute", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", MARKER_DISABLED);
  api_def_prop_ui_text(prop, "Mode", "Is marker muted for current frame");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* pattern */
  prop = api_def_prop(sapi, "pattern_corners", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_float_stype(prop, NULL, "pattern_corners");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_multi_array(prop, 2, rna_matrix_dimsize_4x2);
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  api_def_prop_ui_text(prop,
                       "Pattern Corners",
                       "Array of coordinates which represents pattern's corners in "
                       "normalized coordinates relative to marker position");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "rna_tracking_markerPattern_update");

  prop = api_def_prop(sapi, "pattern_bound_box", PROP_FLOAT, PROP_NONE);
  api_def_prop_multi_array(prop, 2, boundbox_dimsize);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_float_fns(prop, "api_tracking_markerPattern_boundbox_get", NULL, NULL);
  api_def_prop_ui_text(
      prop, "Pattern Bounding Box", "Pattern area bounding box in normalized coordinates");

  /* search */
  prop = api_def_prop(sapi, "search_min", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 2);
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, ApPI_TRANSLATION_PREC_DEFAULT);
  api_def_prop_float_stype(prop, NULL, "search_min");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop,
                       "Search Min",
                       "Left-bottom corner of search area in normalized coordinates relative "
                       "to marker position");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_markerSearch_update");

  prop = api_def_prop(sapi, "search_max", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 2);
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_float_stype(prop, NULL, "search_max");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop,
                       "Search Max",
                       "Right-bottom corner of search area in normalized coordinates relative "
                       "to marker position");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, "api_tracking_markerSearch_update");

  /* is marker keyframed */
  prop = api_def_prop(sapi, "is_keyed", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", MARKER_TRACKED);
  api_def_prop_ui_text(
      prop, "Keyframed", "Whether the position of the marker is keyframed or tracked");
}

static void api_def_trackingMarkers(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "MovieTrackingMarkers");
  sapi = api_def_struct(dapi, "MovieTrackingMarkers", NULL);
  api_def_struct_sdna(sapi, "MovieTrackingTrack");
  api_def_struct_ui_text(
      sapi, "Movie Tracking Markers", "Collection of markers for movie tracking track");

  fn = api_def_fn(sapi, "find_frame", "api_trackingMarkers_find_frame");
  api_def_fn_ui_description(fn, "Get marker for specified frame");
  parm = api_def_int(fn,
                     "frame",
                     1,
                     MINFRAME,
                     MAXFRAME,
                     "Frame",
                     "Frame number to find marker for",
                     MINFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn,
                  "exact",
                  true,
                  "Exact",
                  "Get marker at exact frame number rather than get estimated marker");
  parm = api_def_ptr(fc, "marker", "MovieTrackingMarker", "", "Marker for specified frame");
  api_def_fn_return(fc, parm);

  fn = api_def_fn(sapi, "insert_frame", "rna_trackingMarkers_insert_frame");
  api_def_fn_ui_description(fn, "Insert a new marker at the specified frame");
  parm = api_def_int(fn,
                     "frame",
                     1,
                     MINFRAME,
                     MAXFRAME,
                     "Frame",
                     "Frame number to insert marker to",
                     MINFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_float_vector(
      fn,
      "co",
      2,
      NULL,
      -1.0,
      1.0,
      "Coordinate",
      "Place new marker at the given frame using specified in normalized space coordinates",
      -1.0,
      1.0);
  api_def_ptr_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "marker", "MovieTrackingMarker", "", "Newly created marker");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "delete_frame", "api_trackingMarkers_delete_frame");
  api_def_fn_ui_description(fn, "Delete marker at specified frame");
  parm = api_def_int(fn,
                     "frame",
                     1,
                     MINFRAME,
                     MAXFRAME,
                     "Frame",
                     "Frame number to delete marker from",
                     MINFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

static void api_def_trackingTrack(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_trackingMarker(dapi);

  sapi = api_def_struct(dapi, "MovieTrackingTrack", NULL);
  api_def_struct_path_fn(sapi, "api_trackingTrack_path");
  api_def_struct_ui_text(
      sapi, "Movie tracking track data", "Match-moving track data for tracking");
  apk_def_struct_ui_icon(srna, ICON_ANIM_DATA);

  /* name */
  prop = apk_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Unique name of track");
  api_def_prop_string_fns(prop, NULL, NULL, "api_trackingTrack_name_set");
  api_def_prop_string_maxlength(prop, MAX_ID_NAME - 2);
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);
  api_def_struct_name_prop(sapi, prop);

  /* limit frames */
  prop = api_def_prop(sapi, "frames_limit", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "frames_limit");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0, SHRT_MAX);
  api_def_prop_ui_text(
      prop, "Frames Limit", "Every tracking cycle, this number of frames are tracked");

  /* pattern match */
  prop = api_def_prop(sapi, "pattern_match", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_stype(prop, NULL, "pattern_match");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, pattern_match_items);
  api_def_prop_ui_text(
      prop, "Pattern Match", "Track pattern from given frame when tracking marker to next frame");

  /* margin */
  prop = api_def_prop(sapi, "margin", PROP_INT, PROP_PIXEL);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_stype(prop, NULL, "margin");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0, 300);
  api_def_prop_ui_text(
      prop, "Margin", "Distance from image boundary at which marker stops tracking");

  /* tracking motion model */
  prop = api_def_prop(sapi, "motion_model", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, tracker_motion_model);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Motion Model", "Default motion model to use for tracking");

  /* minimum correlation */
  prop = api_def_prop(srna, "correlation_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "minimum_correlation");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.05, 3);
  api_def_prop_ui_text(prop,
                       "Correlation",
                       "Minimal value of correlation between matched pattern and reference "
                       "that is still treated as successful tracking");

  /* use_brute */
  prop = api_def_prop(sapi, "use_brute", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "algorithm_flag", TRACK_ALGORITHM_FLAG_USE_BRUTE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Prepass", "Use a brute-force translation only pre-track before refinement");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* use_brute */
  prop = api_def_prop(sapi, "use_mask", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "algorithm_flag", TRACK_ALGORITHM_FLAG_USE_MASK);
  api_def_prop_ui_text(
      prop,
      "Use Mask",
      "Use a pen data-block as a mask to use only specified areas of pattern "
      "when tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* use_normalization */
  prop = api_def_prop(sapi, "use_normalization", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "algorithm_flag", TRACK_ALGORITHM_FLAG_USE_NORMALIZATION);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Normalize", "Normalize light intensities while tracking. Slower");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* markers */
  prop = api_def_prop(sapi, "markers", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "MovieTrackingMarker");
  api_def_prop_collection_sdna(prop, NULL, "markers", "markersnr");
  api_def_prop_ui_text(prop, "Markers", "Collection of markers in track");
  api_def_trackingMarkers(dapi, prop);

  /* ** channels ** */

  /* use_red_channel */
  prop = api_def_prop(sapi, "use_red_channel", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", TRACK_DISABLE_RED);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Use Red Channel", "Use red channel from footage for tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* use_green_channel */
  prop = api_def_prop(sapi, "use_green_channel", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", TRACK_DISABLE_GREEN);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Use Green Channel", "Use green channel from footage for tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* use_blue_channel */
  prop = api_def_prop(sapi, "use_blue_channel", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", TRACK_DISABLE_BLUE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Use Blue Channel", "Use blue channel from footage for tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* preview_grayscale */
  prop = api_def_prop(sapi, "use_grayscale_preview", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACK_PREVIEW_GRAYSCALE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Grayscale", "Display what the tracking algorithm sees in the preview");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* preview_alpha */
  prop = api_def_prop(sapi, "use_alpha_preview", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACK_PREVIEW_ALPHA);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Alpha", "Apply track's mask on displaying preview");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* has bundle */
  prop = api_def_prop(sapi, "has_bundle", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACK_HAS_BUNDLE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Has Bundle", "True if track has a valid bundle");

  /* bundle position */
  prop = api_def_prop(sapi, "bundle", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 3);
  api_def_prop_float_stype(prop, NULL, "bundle_pos");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Bundle", "Position of bundle reconstructed from this track");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);

  /* hide */
  prop = api_def_prop(sapi, "hide", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACK_HIDDEN);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Hide", "Track is hidden");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* select */
  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(
      prop, "api_trackingTrack_select_get", "api_trackingTrack_select_set");
  api_def_prop_ui_text(prop, "Select", "Track is selected");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* select_anchor */
  prop = api_def_prop(sapi, "select_anchor", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SELECT);
  api_def_prop_ui_text(prop, "Select Anchor", "Track's anchor point is selected");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* select_pattern */
  prop = api_def_prop(sapi, "select_pattern", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pat_flag", SELECT);
  api_def_prop_ui_text(prop, "Select Pattern", "Track's pattern area is selected");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* select_search */
  prop = api_def_prop(sapi, "select_search", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "search_flag", SELECT);
  api_def_prop_ui_text(prop, "Select Search", "Track's search area is selected");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* locked */
  prop = api_def_prop(sapi, "lock", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACK_LOCKED);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Lock", "Track is locked and all changes to it are disabled");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* custom color */
  prop = api_def_prop(sapi, "use_custom_color", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACK_CUSTOMCOLOR);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Custom Color", "Use custom color instead of theme-defined");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* color */
  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop,
      "Color",
      "Color of the track in the Movie Clip Editor and the 3D viewport after a solve");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* average error */
  prop = api_def_prop(sapi, "average_error", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "error");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Average Error", "Average error of re-projection");

  /* dune pen */
  prop = api_def_prop(sapi, "pen", PROP_PTR, PROP_NONE);
  RNA_def_prop_ptr_stype(prop, NULL, "gpd");
  RNA_def_prop_struct_type(prop, "Pen");
  RNA_def_prop_ptr_fns(
      prop, NULL, NULL, NULL, "api_Pen_datablocks_annotations_poll");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_REFCOUNT);
  api_def_prop_ui_text(prop, "Pen", "Pen data for this track");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* weight */
  prop = api_def_prop(sapi, "weight", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "weight");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Weight", "Influence of this track on a final solution");

  /* weight_stab */
  prop = api_def_prop(sapi, "weight_stab", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "weight_stab");
  RNA_def_prop_range(prop, 0.0f, 1.0f);
  RNA_def_prop_ui_text(prop, "Stab Weight", "Influence of this track on 2D stabilization");

  /* offset */
  prop = api_def_prop(sapi, "offset", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_prop_array(prop, 2);
  RNA_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  RNA_def_prop_float_stype(prop, NULL, "offset");
  RNA_def_prop_ui_text(prop, "Offset", "Offset of track from the parenting point");
  RNA_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);
}

static void api_def_trackingPlaneMarker(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MovieTrackingPlaneMarker", NULL);
  api_def_struct_ui_text(
      sapi, "Movie Tracking Plane Marker Data", "Match-moving plane marker data for tracking");

  /* frame */
  prop = api_def_prop(sapi, "frame", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "framenr");
  api_def_prop_ui_text(prop, "Frame", "Frame number marker is keyframed on");
  api_def_prop_int_fns(prop, NULL, "api_trackingPlaneMarker_frame_set", NULL);
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* Corners */
  prop = api_def_prop(sapi, "corners", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_float_stype(prop, NULL, "corners");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_multi_array(prop, 2, api_matrix_dimsize_4x2);
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  api_def_prop_ui_text(prop,
                       "Corners",
                       "Array of coordinates which represents UI rectangle corners in "
                       "frame normalized coordinates");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);

  /* enable */
  prop = api_def_prop(sapi, "mute", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PLANE_MARKER_DISABLED);
  api_def_prop_ui_text(prop, "Mode", "Is marker muted for current frame");
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);
}

static void api_def_trackingPlaneMarkers(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "MovieTrackingPlaneMarkers");
  sapi = api_def_struct(dapi, "MovieTrackingPlaneMarkers", NULL);
  api_def_struct_stype(sapi, "MovieTrackingPlaneTrack");
  api_def_struct_ui_text(sapi,
                         "Movie Tracking Plane Markers",
                         "Collection of markers for movie tracking plane track");

  fn = api_def_fn(sapi, "find_frame", "api_trackingPlaneMarkers_find_frame");
  api_def_fn_ui_description(fn, "Get plane marker for specified frame");
  parm = api_def_int(fn,
                     "frame",
                     1,
                     MINFRAME,
                     MAXFRAME,
                     "Frame",
                     "Frame number to find marker for",
                     MINFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn,
               "exact",
               true,
               "Exact",
               "Get plane marker at exact frame number rather than get estimated marker");
  parm = api_def_ptr(
      func, "plane_marker", "MovieTrackingPlaneMarker", "", "Plane marker for specified frame");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "insert_frame", "api_trackingPlaneMarkers_insert_frame");
  api_def_fn_ui_description(fn, "Insert a new plane marker at the specified frame");
  parm = api_def_int(fn,
                     "frame",
                     1,
                     MINFRAME,
                     MAXFRAME,
                     "Frame",
                     "Frame number to insert marker to",
                     MINFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "plane_marker", "MovieTrackingPlaneMarker", "", "Newly created plane marker");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "delete_frame", "api_trackingPlaneMarkers_delete_frame");
  api_def_fn_ui_description(fn, "Delete plane marker at specified frame");
  parm = api_def_int(fn,
                     "frame",
                     1,
                     MINFRAME,
                     MAXFRAME,
                     "Frame",
                     "Frame number to delete plane marker from",
                     MINFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

static void api_def_trackingPlaneTrack(DuneApi *dapu)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_trackingPlaneMarker(dapi);

  srna = api_def_struct(dapi, "MovieTrackingPlaneTrack", NULL);
  api_def_struct_path_fn(sapi, "api_trackingPlaneTrack_path");
  api_def_struct_ui_text(
      srna, "Movie tracking plane track data", "Match-moving plane track data for tracking");
  api_def_struct_ui_icon(sapi, ICON_ANIM_DATA);

  /* name */
  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Unique name of track");
  api_def_prop_string_fns(prop, NULL, NULL, "api_trackingPlaneTrack_name_set");
  api_def_prop_string_maxlength(prop, MAX_ID_NAME - 2);
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);
  api_def_struct_name_prop(sapi, prop);

  /* markers */
  prop = api_def_prop(sapi, "markers", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "MovieTrackingPlaneMarker");
  api_def_prop_collection_stype(prop, NULL, "markers", "markersnr");
  api_def_prop_ui_text(prop, "Markers", "Collection of markers in track");
  api_def_trackingPlaneMarkers(dapi, prop);

  /* select */
  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SELECT);
  api_def_prop_ui_text(prop, "Select", "Plane track is selected");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* auto keyframing */
  prop = api_def_prop(sapi, "use_auto_keying", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", PLANE_TRACK_AUTOKEY);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Auto Keyframe", "Automatic keyframe insertion when moving plane corners");
  api_def_prop_ui_icon(prop, ICON_REC, 0);

  /* image */
  prop = api_def_prop(sapi, "image", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Image");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Image", "Image displayed in the track during editing in clip editor");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* image opacity */
  prop = api_def_prop(sapi, "image_opacity", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, 0.0, 1.0);
  api_def_prop_ui_text(prop, "Image Opacity", "Opacity of the image");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);
}

static void api_def_trackingStabilization(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem filter_items[] = {
      {TRACKING_FILTER_NEAREST,
       "NEAREST",
       0,
       "Nearest",
       "No interpolation, use nearest neighbor pixel"},
      {TRACKING_FILTER_BILINEAR,
       "BILINEAR",
       0,
       "Bilinear",
       "Simple interpolation between adjacent pixels"},
      {TRACKING_FILTER_BICUBIC, "BICUBIC", 0, "Bicubic", "High quality pixel interpolation"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "MovieTrackingStabilization", NULL);
  api_def_struct_path_fn(sapi, "api_trackingStabilization_path");
  api_def_struct_ui_text(
      sapi, "Movie tracking stabilization data", "2D stabilization based on tracking markers");

  /* 2d stabilization */
  prop = api_def_prop(sapi, "use_2d_stabilization", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACKING_2D_STABILIZATION);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(prop, "Use 2D Stabilization", "Use 2D stabilization for footage");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* use_stabilize_rotation */
  prop = api_def_prop(sapi, "use_stabilize_rotation", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACKING_STABILIZE_ROTATION);
  api_def_prop_ui_text(
      prop, "Stabilize Rotation", "Stabilize detected rotation around center of frame");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* use_stabilize_scale */
  prop = api_def_prop(sapi, "use_stabilize_scale", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACKING_STABILIZE_SCALE);
  api_def_prop_ui_text(
      prop, "Stabilize Scale", "Compensate any scale changes relative to center of rotation");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* tracks */
  prop = api_def_prop(sapi, "tracks", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_fns(prop,
                              "api_tracking_stabTracks_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_struct_type(prop, "MovieTrackingTrack");
  api_def_prop_ui_text(
      prop, "Translation Tracks", "Collection of tracks used for 2D stabilization (translation)");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* active track index */
  prop = api_def_prop(sapi, "active_track_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "act_track");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_fns(prop,
                       "api_tracking_stabTracks_active_index_get",
                       "apu_tracking_stabTracks_active_index_set",
                       "api_tracking_stabTracks_active_index_range");
  api_def_prop_ui_text(prop,
                       "Active Track Index",
                       "Index of active track in translation stabilization tracks list");

  /* tracks used for rotation stabilization */
  prop = api_def_prop(sapi, "rotation_tracks", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_fns(prop,
                              "api_tracking_stabRotTracks_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_struct_type(prop, "MovieTrackingTrack");
  api_def_prop_ui_text(
      prop, "Rotation Tracks", "Collection of tracks used for 2D stabilization (translation)");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* active rotation track index */
  prop = api_def_prop(sapi, "active_rotation_track_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "act_rot_track");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_fns(prop,
                       "api_tracking_stabRotTracks_active_index_get",
                       "api_tracking_stabRotTracks_active_index_set",
                       "api_tracking_stabRotTracks_active_index_range");
  api_def_prop_ui_text(prop,
                       "Active Rotation Track Index",
                       "Index of active track in rotation stabilization tracks list");

  /* anchor frame */
  prop = api_def_prop(sapi, "anchor_frame", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "anchor_frame");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_range(prop, MINFRAME, MAXFRAME);
  api_def_prop_ui_text(prop,
                       "Anchor Frame",
                       "Reference point to anchor stabilization "
                       "(other frames will be adjusted relative to this frame's position)");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "rna_tracking_flushUpdate");

  /* target position */
  prop = api_def_prop(sapi, "target_position", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 2);
  api_def_prop_ui_range(
      prop, -FLT_MAX, FLT_MAX, 1, 3); /* increment in steps of 0.01 and show 3 digit after point */
  api_def_prop_float_stype(prop, NULL, "target_pos");
  api_def_prop_ui_text(prop,
                       "Expected Position",
                       "Known relative offset of original shot, will be subtracted "
                       "(e.g. for panning shot, can be animated)");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* target rotation */
  prop = api_def_prop(sapi, "target_rotation", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "target_rot");
  api_def_prop_range(prop, -FLT_MAX, FLT_MAX);
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 10.0f, 3);
  api_def_prop_ui_text(
      prop,
      "Expected Rotation",
      "Rotation present on original shot, will be compensated (e.g. for deliberate tilting)");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* target scale */
  prop = api_def_prop(sapi, "target_scale", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "scale");
  api_def_prop_range(prop, FLT_EPSILON, FLT_MAX);
  api_def_prop_ui_range(
      prop, 0.01f, 10.0f, 0.001f, 3); /* increment in steps of 0.001. Show 3 digit after point */
  api_def_prop_ui_text(prop,
                           "Expected Scale",
                           "Explicitly scale resulting frame to compensate zoom of original shot");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* Auto-scale. */
  prop = api_def_prop(sapi, "use_autoscale", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACKING_AUTOSCALE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "Autoscale", "Automatically scale footage to cover unfilled areas when stabilizing");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* max scale */
  prop = api_def_prop(sapi, "scale_max", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "maxscale");
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_text(prop, "Maximal Scale", "Limit the amount of automatic scaling");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* influence_location */
  prop = api_def_prop(sapi, "influence_location", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_style(prop, NULL, "locinf");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Location Influence", "Influence of stabilization algorithm on footage location");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* influence_scale */
  prop = api_def_prop(sapi, "influence_scale", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "scaleinf");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Scale Influence", "Influence of stabilization algorithm on footage scale");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* influence_rotation */
  prop = api_def_prop(sapi, "influence_rotation", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "rotinf");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Rotation Influence", "Influence of stabilization algorithm on footage rotation");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* filter */
  prop = api_def_prop(sapi, "filter_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "filter");
  api_def_prop_enum_items(prop, filter_items);
  api_def_prop_ui_text(
      prop,
      "Interpolate",
      "Interpolation to use for sub-pixel shifts and rotations due to stabilization");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, "api_tracking_flushUpdate");

  /* UI display : show participating tracks */
  prop = api_def_prop(sapi, "show_tracks_expanded", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACKING_SHOW_STAB_TRACKS);
  api_def_prop_ui_text(
      prop, "Show Tracks", "Show UI list of tracks participating in stabilization");
  api_def_prop_ui_icon(prop, ICON_DISCLOSURE_TRI_RIGHT, 1);
}

static void api_def_reconstructedCamera(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MovieReconstructedCamera", NULL);
  api_def_struct_ui_text(sapi,
                         "Movie tracking reconstructed camera data",
                         "Match-moving reconstructed camera data from tracker");

  /* frame */
  prop = api_def_prop(sapi, "frame", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_int_stype(prop, NULL, "framenr");
  api_def_prop_ui_text(prop, "Frame", "Frame number marker is keyframed on");

  /* matrix */
  prop = api_def_prop(sapi, "matrix", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_float_stype(prop, NULL, "mat");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_multi_array(prop, 2, api_matrix_dimsize_4x4);
  api_def_prop_ui_text(prop, "Matrix", "Worldspace transformation matrix");

  /* average_error */
  prop = api_def_prop(sapi, "average_error", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "error");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Average Error", "Average error of reconstruction");
}

static void api_def_trackingReconstructedCameras(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "MovieTrackingReconstructedCameras", NULL);
  api_def_struct_stype(sapi, "MovieTrackingReconstruction");
  api_def_struct_ui_text(sapi, "Reconstructed Cameras", "Collection of solved cameras");

  fn = api_def_fn(sapi, "find_frame", "rna_trackingCameras_find_frame");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Find a reconstructed camera for a give frame number");
  api_def_int(fn,
              "frame",
              1,
              MINFRAME,
              MAXFRAME,
              "Frame",
              "Frame number to find camera for",
              MINFRAME,
              MAXFRAME);
  parm = api_def_ptr(
      func, "camera", "MovieReconstructedCamera", "", "Camera for a given frame");
  api_def_fn_return(fn, parm);

  fn = api_def_function(sapi, "matrix_from_frame", "api_trackingCameras_matrix_from_frame");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Return interpolated camera matrix for a given frame");
  api_def_int(fn,
              "frame",
              1,
              MINFRAME,
              MAXFRAME,
              "Frame",
              "Frame number to find camera for",
              MINFRAME,
              MAXFRAME);
  parm = api_def_float_matrix(fn,
                              "matrix",
                              4,
                              4,
                              NULL,
                              -FLT_MAX,
                              FLT_MAX,
                              "Matrix",
                              "Interpolated camera matrix for a given frame",
                              -FLT_MAX,
                              FLT_MAX);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0); /* needed for string return value */
  api_def_fn_output(fn, parm);
}

static void api_def_trackingReconstruction(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_reconstructedCamera(dapi);

  sapi = api_def_struct(dapi, "MovieTrackingReconstruction", NULL);
  api_def_struct_ui_text(
      sapi, "Movie tracking reconstruction data", "Match-moving reconstruction data from tracker");

  /* is_valid */
  prop = api_def_prop(sapi, "is_valid", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACKING_RECONSTRUCTED);
  api_def_prop_ui_text(
      prop, "Reconstructed", "Is tracking data contains valid reconstruction information");

  /* average_error */
  prop = api_def_prop(sapi, "average_error", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "error");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Average Error", "Average error of reconstruction");

  /* cameras */
  prop = api_def_prop(sapi, "cameras", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "MovieReconstructedCamera");
  api_def_prop_collection_stype(prop, NULL, "cameras", "camnr");
  api_def_prop_ui_text(prop, "Cameras", "Collection of solved cameras");
  api_def_prop_sapi(prop, "MovieTrackingReconstructedCameras");
}

static void api_def_trackingTracks(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *prop;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "MovieTrackingTracks", NULL);
  api_def_struct_stype(sapi, "MovieTracking");
  api_def_struct_ui_text(sapi, "Movie Tracks", "Collection of movie tracking tracks");

  fn = api_def_function(sapi, "new", "api_trackingTracks_new");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Create new motion track in this movie clip");
  api_def_string(fn, "name", NULL, 0, "", "Name of new track");
  api_def_int(fn,
              "frame",
              1,
              MINFRAME,
              MAXFRAME,
              "Frame",
              "Frame number to add track on",
              MINFRAME,
              MAXFRAME);
  parm = api_def_ptr(fn, "track", "MovieTrackingTrack", "", "Newly created track");
  api_def_fn_return(fn, parm);

  /* active track */
  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "MovieTrackingTrack");
  api_def_prop_ptr_fns(
      prop, "api_tracking_active_track_get", "rna_tracking_active_track_set", NULL, NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop,
                       "Active Track",
                       "Active track in this tracking data object. "
                       "Deprecated, use objects[name].tracks.active");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_MOVIECLIP);
  api_def_prop_update(prop, NC_MOVIECLIP | ND_SELECT, NULL);
}

static void api_def_trackingPlaneTracks(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MovieTrackingPlaneTracks", NULL);
  api_def_struct_stype(sapi, "MovieTracking");
  api_def_struct_ui_text(sapi, "Movie Plane Tracks", "Collection of movie tracking plane tracks");

  /* TODO: Add API to create new plane tracks */

  /* active plane track */
  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "MovieTrackingPlaneTrack");
  api_def_prop_ptr_fns(prop,
                           "api_tracking_active_plane_track_get",
                           "api_tracking_active_plane_track_set",
                           NULL,
                           NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop,
                       "Active Plane Track",
                       "Active plane track in this tracking data object. "
                       "Deprecated, use objects[name].plane_tracks.active");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_SELECT, NULL);
}

static void api_def_trackingObjectTracks(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *prop;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "MovieTrackingObjectTracks", NULL);
  api_def_struct_stype(sapi, "MovieTrackingObject");
  api_def_struct_ui_text(sapi, "Movie Tracks", "Collection of movie tracking tracks");

  fn = api_def_fn(sapi, "new", "api_trackingObject_tracks_new");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "create new motion track in this movie clip");
  api_def_string(fn, "name", NULL, 0, "", "Name of new track");
  api_def_int(fn,
              "frame",
              1,
              MINFRAME,
              MAXFRAME,
              "Frame",
              "Frame number to add tracks on",
              MINFRAME,
              MAXFRAME);
  parm = api_def_ptr(fn, "track", "MovieTrackingTrack", "", "Newly created track");
  api_def_fn_return(fn, parm);

  /* active track */
  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "MovieTrackingTrack");
  api_def_prop_ptr_fns(prop,
                      "api_tracking_object_active_track_get",
                      "api_tracking_object_active_track_set",
                      NULL,
                      NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop, "Active Track", "Active track in this tracking data object");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_MOVIECLIP);
  api_def_prop_update(prop, NC_MOVIECLIP | ND_SELECT, NULL);
}

static void api_def_trackingObjectPlaneTracks(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MovieTrackingObjectPlaneTracks", NULL);
  api_def_struct_stype(sapi, "MovieTrackingObject");
  api_def_struct_ui_text(sapi, "Plane Tracks", "Collection of tracking plane tracks");

  /* active track */
  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "MovieTrackingTrack");
  api_def_prop_ptr_fns(prop,
                       "api_tracking_object_active_plane_track_get",
                       "api_tracking_object_active_plane_track_set",
                       NULL,
                       NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop, "Active Track", "Active track in this tracking data object");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_MOVIECLIP);
  api_def_prop_update(prop, NC_MOVIECLIP | ND_SELECT, NULL);
}

static void api_def_trackingObject(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MovieTrackingObject", NULL);
  api_def_struct_ui_text(
      sapi, "Movie tracking object data", "Match-moving object tracking and reconstruction data");

  /* name */
  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Unique name of object");
  api_def_prop_string_fns(prop, NULL, NULL, "api_trackingObject_name_set");
  api_def_prop_string_maxlength(prop, MAX_ID_NAME - 2);
  api_def_prop_update(prop, NC_MOVIECLIP | NA_EDITED, NULL);
  api_def_struct_name_prop(sapi, prop);

  /* is_camera */
  prop = api_def_prop(sapi, "is_camera", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", TRACKING_OBJECT_CAMERA);
  api_def_prop_ui_text(prop, "Camera", "Object is used for camera tracking");
  api_def_prop_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* tracks */
  prop = api_def_prop(sapi, "tracks", PROP_COLLECTION, PROP_NONE);
  RNA_def_prop_collection_fns(prop,
                              "api_trackingObject_tracks_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  RNA_def_prop_struct_type(prop, "MovieTrackingTrack");
  RNA_def_prop_ui_text(prop, "Tracks", "Collection of tracks in this tracking data object");
  RNA_def_prop_sap(prop, "MovieTrackingObjectTracks");

  /* plane tracks */
  prop = RNA_def_property(srna, "plane_tracks", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_funcs(prop,
                                    "rna_trackingObject_plane_tracks_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MovieTrackingPlaneTrack");
  RNA_def_property_ui_text(
      prop, "Plane Tracks", "Collection of plane tracks in this tracking data object");
  RNA_def_property_srna(prop, "MovieTrackingObjectPlaneTracks");

  /* reconstruction */
  prop = RNA_def_property(srna, "reconstruction", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MovieTrackingReconstruction");
  RNA_def_property_pointer_funcs(prop, "rna_trackingObject_reconstruction_get", NULL, NULL, NULL);

  /* scale */
  prop = RNA_def_property(srna, "scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_float_sdna(prop, NULL, "scale");
  RNA_def_property_range(prop, 0.0001f, 10000.0f);
  RNA_def_property_ui_range(prop, 0.0001f, 10000.0, 1, 4);
  RNA_def_property_float_default(prop, 1.0f);
  RNA_def_property_ui_text(prop, "Scale", "Scale of object solution in camera space");
  RNA_def_property_update(prop, NC_MOVIECLIP | NA_EDITED, "rna_trackingObject_flushUpdate");

  /* keyframe_a */
  prop = RNA_def_property(srna, "keyframe_a", PROP_INT, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_int_sdna(prop, NULL, "keyframe1");
  RNA_def_property_ui_text(
      prop, "Keyframe A", "First keyframe used for reconstruction initialization");
  RNA_def_property_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* keyframe_b */
  prop = RNA_def_property(srna, "keyframe_b", PROP_INT, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_int_sdna(prop, NULL, "keyframe2");
  RNA_def_property_ui_text(
      prop, "Keyframe B", "Second keyframe used for reconstruction initialization");
  RNA_def_property_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);
}

static void rna_def_trackingObjects(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "MovieTrackingObjects");
  srna = RNA_def_struct(brna, "MovieTrackingObjects", NULL);
  RNA_def_struct_sdna(srna, "MovieTracking");
  RNA_def_struct_ui_text(srna, "Movie Objects", "Collection of movie tracking objects");

  func = RNA_def_function(srna, "new", "rna_trackingObject_new");
  RNA_def_function_ui_description(func, "Add tracking object to this movie clip");
  parm = RNA_def_string(func, "name", NULL, 0, "", "Name of new object");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "object", "MovieTrackingObject", "", "New motion tracking object");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_trackingObject_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove tracking object from this movie clip");
  parm = RNA_def_pointer(
      func, "object", "MovieTrackingObject", "", "Motion tracking object to be removed");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

  /* active object */
  prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MovieTrackingObject");
  RNA_def_property_pointer_funcs(
      prop, "rna_tracking_active_object_get", "rna_tracking_active_object_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  RNA_def_property_ui_text(prop, "Active Object", "Active object in this tracking data object");
}

static void rna_def_trackingDopesheet(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem sort_items[] = {
      {TRACKING_DOPE_SORT_NAME, "NAME", 0, "Name", "Sort channels by their names"},
      {TRACKING_DOPE_SORT_LONGEST,
       "LONGEST",
       0,
       "Longest",
       "Sort channels by longest tracked segment"},
      {TRACKING_DOPE_SORT_TOTAL,
       "TOTAL",
       0,
       "Total",
       "Sort channels by overall amount of tracked segments"},
      {TRACKING_DOPE_SORT_AVERAGE_ERROR,
       "AVERAGE_ERROR",
       0,
       "Average Error",
       "Sort channels by average reprojection error of tracks after solve"},
      {TRACKING_DOPE_SORT_START, "START", 0, "Start Frame", "Sort channels by first frame number"},
      {TRACKING_DOPE_SORT_END, "END", 0, "End Frame", "Sort channels by last frame number"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "MovieTrackingDopesheet", NULL);
  RNA_def_struct_ui_text(srna, "Movie Tracking Dopesheet", "Match-moving dopesheet data");

  /* dopesheet sort */
  prop = RNA_def_property(srna, "sort_method", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "sort_method");
  RNA_def_property_enum_items(prop, sort_items);
  RNA_def_property_ui_text(
      prop, "Dopesheet Sort Field", "Method to be used to sort channels in dopesheet view");
  RNA_def_property_update(prop, NC_MOVIECLIP | NA_EDITED, "rna_trackingDopesheet_tagUpdate");

  /* invert_dopesheet_sort */
  prop = RNA_def_property(srna, "use_invert_sort", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TRACKING_DOPE_SORT_INVERSE);
  RNA_def_property_ui_text(
      prop, "Invert Dopesheet Sort", "Invert sort order of dopesheet channels");
  RNA_def_property_update(prop, NC_MOVIECLIP | NA_EDITED, "rna_trackingDopesheet_tagUpdate");

  /* show_only_selected */
  prop = RNA_def_property(srna, "show_only_selected", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TRACKING_DOPE_SELECTED_ONLY);
  RNA_def_property_ui_text(
      prop, "Only Show Selected", "Only include channels relating to selected objects and data");
  RNA_def_property_ui_icon(prop, ICON_RESTRICT_SELECT_OFF, 0);
  RNA_def_property_update(prop, NC_MOVIECLIP | NA_EDITED, "rna_trackingDopesheet_tagUpdate");

  /* show_hidden */
  prop = RNA_def_property(srna, "show_hidden", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TRACKING_DOPE_SHOW_HIDDEN);
  RNA_def_property_ui_text(
      prop, "Display Hidden", "Include channels from objects/bone that aren't visible");
  RNA_def_property_ui_icon(prop, ICON_GHOST_ENABLED, 0);
  RNA_def_property_update(prop, NC_MOVIECLIP | NA_EDITED, "rna_trackingDopesheet_tagUpdate");
}

static void rna_def_tracking(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  rna_def_trackingSettings(dapi);
  rna_def_trackingCamera(dapi);
  rna_def_trackingTrack(dapi);
  rna_def_trackingPlaneTrack(dapi);
  rna_def_trackingTracks(dapi);
  rna_def_trackingPlaneTracks(dapi);
  rna_def_trackingObjectTracks(dapi);
  rna_def_trackingObjectPlaneTracks(dapi);
  rna_def_trackingStabilization(dapi);
  rna_def_trackingReconstructedCameras(dapi);
  rna_def_trackingReconstruction(dapi);
  rna_def_trackingObject(brna);
  rna_def_trackingDopesheet(brna);

  srna = RNA_def_struct(brna, "MovieTracking", NULL);
  RNA_def_struct_path_func(srna, "rna_tracking_path");
  RNA_def_struct_ui_text(srna, "Movie tracking data", "Match-moving data for tracking");

  /* settings */
  prop = RNA_def_property(srna, "settings", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MovieTrackingSettings");

  /* camera properties */
  prop = RNA_def_property(srna, "camera", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MovieTrackingCamera");

  /* tracks */
  prop = RNA_def_property(srna, "tracks", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_funcs(prop,
                                    "rna_trackingTracks_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MovieTrackingTrack");
  RNA_def_property_ui_text(prop,
                           "Tracks",
                           "Collection of tracks in this tracking data object. "
                           "Deprecated, use objects[name].tracks");
  RNA_def_property_srna(prop, "MovieTrackingTracks");

  /* tracks */
  prop = RNA_def_property(srna, "plane_tracks", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_funcs(prop,
                                    "rna_trackingPlaneTracks_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MovieTrackingPlaneTrack");
  RNA_def_property_ui_text(prop,
                           "Plane Tracks",
                           "Collection of plane tracks in this tracking data object. "
                           "Deprecated, use objects[name].plane_tracks");
  RNA_def_property_srna(prop, "MovieTrackingPlaneTracks");

  /* stabilization */
  prop = RNA_def_property(srna, "stabilization", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MovieTrackingStabilization");

  /* reconstruction */
  prop = RNA_def_property(srna, "reconstruction", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "reconstruction_legacy");
  RNA_def_property_pointer_funcs(prop, "rna_trackingReconstruction_get", NULL, NULL, NULL);
  RNA_def_property_struct_type(prop, "MovieTrackingReconstruction");

  /* objects */
  prop = RNA_def_property(srna, "objects", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_funcs(prop,
                                    "rna_trackingObjects_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MovieTrackingObject");
  RNA_def_property_ui_text(prop, "Objects", "Collection of objects in this tracking data object");
  rna_def_trackingObjects(brna, prop);

  /* active object index */
  prop = RNA_def_property(srna, "active_object_index", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "objectnr");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_int_funcs(prop,
                             "rna_tracking_active_object_index_get",
                             "rna_tracking_active_object_index_set",
                             "rna_tracking_active_object_index_range");
  RNA_def_property_ui_text(prop, "Active Object Index", "Index of active object");
  RNA_def_property_update(prop, NC_MOVIECLIP | ND_DISPLAY, NULL);

  /* dopesheet */
  prop = RNA_def_property(srna, "dopesheet", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MovieTrackingDopesheet");
}

void RNA_def_tracking(BlenderRNA *brna)
{
  rna_def_tracking(brna);
}

#endif
