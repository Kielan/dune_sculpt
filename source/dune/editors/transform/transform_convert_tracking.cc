#include "types_space.h"

#include "mem_guardedalloc.h"

#include "lib_math_matrix.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_main.hh"
#include "dune_movieclip.h"
#include "dune_node_tree_update.hh"
#include "dune_tracking.h"

#include "ed_clip.hh"

#include "win_api.hh"

#include "transform.hh"
#include "transform_convert.hh"

struct TransDataTracking {
  int mode;
  int flag;

  /* tracks transformation from main Win */
  int area;
  const float *relative, *loc;
  float soffset[2], srelative[2];
  float offset[2];

  float (*smarkers)[2];
  int markersnr;
  int framenr;
  MovieTrackingMarker *markers;

  MovieTrackingTrack *track;
  MovieTrackingPlaneTrack *plane_track;
};

enum transDataTracking_Mode {
  transDataTracking_ModeTracks = 0,
  transDataTracking_ModePlaneTracks = 1,
};

/* Clip Editor Motion Tracking Transform Creation */
struct TransformInitCxt {
  SpaceClip *space_clip;

  TransInfo *t;
  TransDataContainer *tc;

  /* These ptrs will be `nullptr` during counting step.
   * This means, that the transformation data init fns are to increment
   * `tc->data_len` instead of filling in the transform data when these pointers are
   * `nullptr`. For simplicity, check the `current.td` against `nullptr`.
   * Do not `tc->data_len` when filling in the transform data. */
  struct {
    TransData *td;
    TransData2D *td2d;
    TransDataTracking *tdt;
  } current;
};

static void markerToTransDataInit(TransformInitCxt *init_cxt,
                                  MovieTrackingTrack *track,
                                  MovieTrackingMarker *marker,
                                  int area,
                                  float loc[2],
                                  const float rel[2],
                                  const float off[2],
                                  const float aspect[2])
{
  TransData *td = init_cxt->current.td;
  TransData2D *td2d = init_cxt->current.td2d;
  TransDataTracking *tdt = init_cxt->current.tdt;

  if (td == nullptr) {
    init_cxt->tc->data_len++;
    return;
  }

  int anchor = area == TRACK_AREA_POINT && off;

  tdt->flag = marker->flag;
  tdt->framenr = marker->framenr;
  tdt->mode = transDataTracking_ModeTracks;

  if (anchor) {
    td2d->loc[0] = rel[0] * aspect[0]; /* hold original location */
    td2d->loc[1] = rel[1] * aspect[1];

    tdt->loc = loc;
    td2d->loc2d = loc; /* current location */
  }
  else {
    td2d->loc[0] = loc[0] * aspect[0]; /* hold original location */
    td2d->loc[1] = loc[1] * aspect[1];

    td2d->loc2d = loc; /* current location */
  }
  td2d->loc[2] = 0.0f;

  tdt->relative = rel;
  tdt->area = area;

  tdt->markersnr = track->markersnr;
  tdt->markers = track->markers;
  tdt->track = track;

  if (rel) {
    if (!anchor) {
      td2d->loc[0] += rel[0] * aspect[0];
      td2d->loc[1] += rel[1] * aspect[1];
    }

    copy_v2_v2(tdt->srelative, rel);
  }

  if (off) {
    copy_v2_v2(tdt->soffset, off);
  }

  td->flag = 0;
  td->loc = td2d->loc;
  copy_v3_v3(td->iloc, td->loc);

  // copy_v3_v3(td->center, td->loc);
  td->flag |= TD_INDIVIDUAL_SCALE;
  td->center[0] = marker->pos[0] * aspect[0];
  td->center[1] = marker->pos[1] * aspect[1];

  memset(td->axismtx, 0, sizeof(td->axismtx));
  td->axismtx[2][2] = 1.0f;

  td->ext = nullptr;
  td->val = nullptr;

  td->flag |= TD_SELECTED;
  td->dist = 0.0;

  unit_m3(td->mtx);
  unit_m3(td->smtx);

  init_cxt->current.td++;
  init_cxt->current.td2d++;
  init_cxt->current.tdt++;
}

static void trackToTransData(TransformInitCxt *init_cxt,
                             const int framenr,
                             MovieTrackingTrack *track,
                             const float aspect[2])
{
  MovieTrackingMarker *marker = dune_tracking_marker_ensure(track, framenr);

  markerToTransDataInit(init_cxt,
                        track,
                        marker,
                        TRACK_AREA_POINT,
                        track->offset,
                        marker->pos,
                        track->offset,
                        aspect);

  if (track->flag & SEL) {
    markerToTransDataInit(
        init_cxt, track, marker, TRACK_AREA_POINT, marker->pos, nullptr, nullptr, aspect);
  }

  if (track->pat_flag & SEL) {
    int a;

    for (a = 0; a < 4; a++) {
      markerToTransDataInit(init_cxt,
                            track,
                            marker,
                            TRACK_AREA_PAT,
                            marker->pattern_corners[a],
                            marker->pos,
                            nullptr,
                            aspect);
    }
  }

  if (track->search_flag & SEL) {
    markerToTransDataInit(init_cxt,
                          track,
                          marker,
                          TRACK_AREA_SEARCH,
                          marker->search_min,
                          marker->pos,
                          nullptr,
                          aspect);

    markerToTransDataInit(init_cxt,
                          track,
                          marker,
                          TRACK_AREA_SEARCH,
                          marker->search_max,
                          marker->pos,
                          nullptr,
                          aspect);
  }

  if (init_cxt->current.td != nullptr) {
    marker->flag &= ~(MARKER_DISABLED | MARKER_TRACKED);
  }
}

static void trackToTransDataIfNeeded(TransformInitCxt *init_cxt,
                                     const int framenr,
                                     MovieTrackingTrack *track,
                                     const float aspect[2])
{
  if (!TRACK_VIEW_SEL(init_cxt->space_clip, track)) {
    return;
  }
  if (track->flag & TRACK_LOCKED) {
    return;
  }
  trackToTransData(init_cxt, framenr, track, aspect);
}

static void planeMarkerToTransDataInit(TransformInitCxt *init_cxt,
                                       MovieTrackingPlaneTrack *plane_track,
                                       MovieTrackingPlaneMarker *plane_marker,
                                       float corner[2],
                                       const float aspect[2])
{
  TransData *td = init_cxt->current.td;
  TransData2D *td2d = init_cxt->current.td2d;
  TransDataTracking *tdt = init_cxt->current.tdt;

  if (td == nullptr) {
    init_cxt->tc->data_len++;
    return;
  }

  tdt->flag = plane_marker->flag;
  tdt->framenr = plane_marker->framenr;
  tdt->mode = transDataTracking_ModePlaneTracks;
  tdt->plane_track = plane_track;

  td2d->loc[0] = corner[0] * aspect[0]; /* hold original location */
  td2d->loc[1] = corner[1] * aspect[1];

  td2d->loc2d = corner; /* current location */
  td2d->loc[2] = 0.0f;

  td->flag = 0;
  td->loc = td2d->loc;
  copy_v3_v3(td->iloc, td->loc);
  copy_v3_v3(td->center, td->loc);

  memset(td->axismtx, 0, sizeof(td->axismtx));
  td->axismtx[2][2] = 1.0f;

  td->ext = nullptr;
  td->val = nullptr;

  td->flag |= TD_SELECTED;
  td->dist = 0.0;

  unit_m3(td->mtx);
  unit_m3(td->smtx);

  init_cxt->current.td++;
  init_cxt->current.td2d++;
  init_cxt->current.tdt++;
}

static void planeTrackToTransData(TransformInitCxt *init_cxt,
                                  const int framenr,
                                  MovieTrackingPlaneTrack *plane_track,
                                  const float aspect[2])
{
  MovieTrackingPlaneMarker *plane_marker = dune_tracking_plane_marker_ensure(plane_track, framenr);

  for (int i = 0; i < 4; i++) {
    planeMarkerToTransDataInit(
        init_cxt, plane_track, plane_marker, plane_marker->corners[i], aspect);
  }

  if (init_cxt->current.td != nullptr) {
    plane_marker->flag &= ~PLANE_MARKER_TRACKED;
  }
}

static void planeTrackToTransDataIfNeeded(TransformInitCxt *init_cxt,
                                          const int framenr,
                                          MovieTrackingPlaneTrack *plane_track,
                                          const float aspect[2])
{
  if (!PLANE_TRACK_VIEW_SEL(plane_track)) {
    return;
  }
  planeTrackToTransData(init_cxt, framenr, plane_track, aspect);
}

static void transDataTrackingFree(TransInfo * /*t*/,
                                  TransDataContainer * /*tc*/,
                                  TransCustomData *custom_data)
{
  if (custom_data->data) {
    TransDataTracking *tdt = static_cast<TransDataTracking *>(custom_data->data);
    if (tdt->smarkers) {
      mem_free(tdt->smarkers);
    }

    mem_free(tdt);
    custom_data->data = nullptr;
  }
}

static void createTransTrackingTracksData(Cxt *C, TransInfo *t)
{
  SpaceClip *space_clip = cxt_win_space_clip(C);
  MovieClip *clip = ed_space_clip_get_clip(space_clip);
  const MovieTrackingOb *tracking_ob = dune_tracking_ob_get_active(&clip->tracking);
  const int framenr = ed_space_clip_get_clip_frame_number(space_clip);

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  TransformInitCxt init_cxt = {nullptr};
  init_cxt.space_clip = space_clip;
  init_cxt.t = t;
  init_cxt.tc = tc;

  /* Count required transform data. */
  tc->data_len = 0;

  LIST_FOREACH (MovieTrackingTrack *, track, &tracking_ob->tracks) {
    trackToTransDataIfNeeded(&init_cxt, framenr, track, t->aspect);
  }

  LIST_FOREACH (MovieTrackingPlaneTrack *, plane_track, &tracking_ob->plane_tracks) {
    planeTrackToTransDataIfNeeded(&init_cxt, framenr, plane_track, t->aspect);
  }

  if (tc->data_len == 0) {
    return;
  }

  tc->data = static_cast<TransData *>(
      mem_calloc(tc->data_len * sizeof(TransData), "TransTracking TransData"));
  tc->data_2d = static_cast<TransData2D *>(
      mem_calloc(tc->data_len * sizeof(TransData2D), "TransTracking TransData2D"));
  tc->custom.type.data = mem_calloc(tc->data_len * sizeof(TransDataTracking),
                                     "TransTracking TransDataTracking");
  tc->custom.type.free_cb = transDataTrackingFree;

  init_cxt.current.td = tc->data;
  init_cxt.current.td2d = tc->data_2d;
  init_cxt.current.tdt = static_cast<TransDataTracking *>(tc->custom.type.data);

  /* Create actual transform data. */
  LIST_FOREACH (MovieTrackingTrack *, track, &tracking_ob->tracks) {
    trackToTransDataIfNeeded(&init_cxt, framenr, track, t->aspect);
  }

  LIST_FOREACH (MovieTrackingPlaneTrack *, plane_track, &tracking_ob->plane_tracks) {
    planeTrackToTransDataIfNeeded(&init_cxt, framenr, plane_track, t->aspect);
  }
}

static void createTransTrackingData(Cxt *C, TransInfo *t)
{
  SpaceClip *sc = cxt_win_space_clip(C);
  MovieClip *clip = ed_space_clip_get_clip(sc);
  int width, height;

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  tc->data_len = 0;

  if (!clip) {
    return;
  }

  dune_movieclip_get_size(clip, &sc->user, &width, &height);

  if (width == 0 || height == 0) {
    return;
  }

  createTransTrackingTracksData(C, t);
}

/* recalc Motion Tracking TransData */
static void cancelTransTracking(TransInfo *t)
{
  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);
  TransDataTracking *tdt_array = static_cast<TransDataTracking *>(tc->custom.type.data);

  int i = 0;
  while (i < tc->data_len) {
    TransDataTracking *tdt = &tdt_array[i];

    if (tdt->mode == transDataTracking_ModeTracks) {
      MovieTrackingTrack *track = tdt->track;
      MovieTrackingMarker *marker = dune_tracking_marker_get_exact(track, tdt->framenr);

      lib_assert(marker != nullptr);

      marker->flag = tdt->flag;

      if (track->flag & SEL) {
        i++;
      }

      if (track->pat_flag & SEL) {
        i += 4;
      }

      if (track->search_flag & SEL) {
        i += 2;
      }
    }
    else if (tdt->mode == transDataTracking_ModePlaneTracks) {
      MovieTrackingPlaneTrack *plane_track = tdt->plane_track;
      MovieTrackingPlaneMarker *plane_marker = dune_tracking_plane_marker_get_exact(plane_track,
                                                                                   tdt->framenr);

      lib_assert(plane_marker != nullptr);

      plane_marker->flag = tdt->flag;
      i += 3;
    }

    i++;
  }
}

static void flushTransTracking(TransInfo *t)
{
  TransData *td;
  TransData2D *td2d;
  TransDataTracking *tdt;
  int td_index;

  if (t->state == TRANS_CANCEL) {
    cancelTransTracking(t);
  }

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  /* flush to 2d vector from internally used 3d vector */
  for (td_index = 0,
      td = tc->data,
      td2d = tc->data_2d,
      tdt = static_cast<TransDataTracking *>(tc->custom.type.data);
       td_index < tc->data_len;
       td_index++, td2d++, td++, tdt++)
  {
    if (tdt->mode == transDataTracking_ModeTracks) {
      float loc2d[2];

      if (t->mode == TFM_ROTATION && tdt->area == TRACK_AREA_SEARCH) {
        continue;
      }

      loc2d[0] = td2d->loc[0] / t->aspect[0];
      loc2d[1] = td2d->loc[1] / t->aspect[1];

      if (t->flag & T_ALT_TRANSFORM) {
        if (t->mode == TFM_RESIZE) {
          if (tdt->area != TRACK_AREA_PAT && !(t->state == TRANS_CANCEL)) {
            continue;
          }
        }
        else if (t->mode == TFM_TRANSLATION) {
          if (tdt->area == TRACK_AREA_POINT && tdt->relative) {
            float d[2], d2[2];

            if (!tdt->smarkers) {
              tdt->smarkers = static_cast<float(*)[2]>(mem_calloc(
                  sizeof(*tdt->smarkers) * tdt->markersnr, "flushTransTracking markers"));
              for (int a = 0; a < tdt->markersnr; a++) {
                copy_v2_v2(tdt->smarkers[a], tdt->markers[a].pos);
              }
            }

            sub_v2_v2v2(d, loc2d, tdt->soffset);
            sub_v2_v2(d, tdt->srelative);

            sub_v2_v2v2(d2, loc2d, tdt->srelative);

            for (int a = 0; a < tdt->markersnr; a++) {
              add_v2_v2v2(tdt->markers[a].pos, tdt->smarkers[a], d2);
            }

            negate_v2_v2(td2d->loc2d, d);
          }
        }
      }

      if (tdt->area != TRACK_AREA_POINT || tdt->relative == nullptr) {
        td2d->loc2d[0] = loc2d[0];
        td2d->loc2d[1] = loc2d[1];

        if (tdt->relative) {
          sub_v2_v2(td2d->loc2d, tdt->relative);
        }
      }
    }
    else if (tdt->mode == transDataTracking_ModePlaneTracks) {
      td2d->loc2d[0] = td2d->loc[0] / t->aspect[0];
      td2d->loc2d[1] = td2d->loc[1] / t->aspect[1];
    }
  }
}

static void recalcData_tracking(TransInfo *t)
{
  SpaceClip *sc = static_cast<SpaceClip *>(t->area->spacedata.first);

  if (ed_space_clip_check_show_trackedit(sc)) {
    MovieClip *clip = ed_space_clip_get_clip(sc);
    const MovieTrackingOb *tracking_ob = dune_tracking_ob_get_active(&clip->tracking);
    const int framenr = ed_space_clip_get_clip_frame_number(sc);

    flushTransTracking(t);

    LIST_FOREACH (MovieTrackingTrack *, track, &tracking_ob->tracks) {
      if (TRACK_VIEW_SEL(sc, track) && (track->flag & TRACK_LOCKED) == 0) {
        MovieTrackingMarker *marker = dune_tracking_marker_get(track, framenr);

        if (t->mode == TFM_TRANSLATION) {
          if (TRACK_AREA_SEL(track, TRACK_AREA_PAT)) {
            dune_tracking_marker_clamp_pattern_position(marker);
          }
          if (TRACK_AREA_SEL(track, TRACK_AREA_SEARCH)) {
            dune_tracking_marker_clamp_search_position(marker);
          }
        }
        else if (t->mode == TFM_RESIZE) {
          if (TRACK_AREA_SEL(track, TRACK_AREA_PAT)) {
            dune_tracking_marker_clamp_search_size(marker);
          }
          if (TRACK_AREA_SEL(track, TRACK_AREA_SEARCH)) {
            dune_tracking_marker_clamp_search_size(marker);
          }
        }
        else if (t->mode == TFM_ROTATION) {
          if (TRACK_AREA_SEL(track, TRACK_AREA_PAT)) {
            dune_tracking_marker_clamp_pattern_position(marker);
          }
        }
      }
    }

    graph_id_tag_update(&clip->id, 0);
  }
}

/* Special After Transform Tracking */
static void special_aftertrans_update_movieclip(Cxt *C, TransInfo *t)
{
  SpaceClip *sc = static_cast<SpaceClip *>(t->area->spacedata.first);
  MovieClip *clip = ed_space_clip_get_clip(sc);
  const MovieTrackingOb *tracking_ob = dune_tracking_ob_get_active(&clip->tracking);
  const int framenr = ed_space_clip_get_clip_frame_number(sc);
  /* Update coords of mod plane tracks. */
  LIST_FOREACH (MovieTrackingPlaneTrack *, plane_track, &tracking_ob->plane_tracks) {
    bool do_update = false;
    if (plane_track->flag & PLANE_TRACK_HIDDEN) {
      continue;
    }
    do_update |= PLANE_TRACK_VIEW_SEL(plane_track) != 0;
    if (do_update == false) {
      if ((plane_track->flag & PLANE_TRACK_AUTOKEY) == 0) {
        int i;
        for (i = 0; i < plane_track->point_tracksnr; i++) {
          MovieTrackingTrack *track = plane_track->point_tracks[i];
          if (TRACK_VIEW_SEL(sc, track)) {
            do_update = true;
            break;
          }
        }
      }
    }
    if (do_update) {
      dune_tracking_track_plane_from_existing_motion(plane_track, framenr);
    }
  }
  if (t->scene->nodetree != nullptr) {
    /* Tracks can be used for stabilization nodes,
     * flush update for such nodes. */
    if (t->cxt != nullptr) {
      Main *main = cxt_data_main(C);
      dune_ntree_update_tag_id_changed(main, &clip->id);
      dune_ntree_update_main(main, nullptr);
      win_ev_add_notifier(C, NC_SCENE | ND_NODES, nullptr);
    }
  }
}

TransConvertTypeInfo TransConvertType_Tracking = {
    /*flags*/ (T_POINTS | T_2D_EDIT),
    /*create_trans_data*/ createTransTrackingData,
    /*recalc_data*/ recalcData_tracking,
    /*special_aftertrans_update*/ special_aftertrans_update__movieclip,
};
