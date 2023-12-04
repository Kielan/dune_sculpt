#include <cstdio>

#include "types_anim.h"
#include "types_space.h"

#include "mem_guardedalloc.h"

#include "lib_list.h"
#include "lib_math_matrix.h"
#include "lib_math_vector.h"

#include "dune_anim_data.h"
#include "dune_cxt.hh"
#include "dune_nla.h"

#include "ed_anim_api.hh"
#include "ed_markers.hh"

#include "win_api.hh"
#include "won_types.hh"

#include "api_access.hh"
#include "api_prototypes.h"

#include "transform.hh"
#include "transform_convert.hh"
#include "transform_mode.hh"
#include "transform_snap.hh"

/* Used for NLA transform (stored in #TransData.extra pointer). */
struct TransDataNla {
  /* Id-block NLA-data is attached to. */
  Id *id;

  /* Original NLA-Track that the strip belongs to. */
  NlaTrack *oldTrack;
  /* Current NLA-Track that the strip belongs to. */
  NlaTrack *nlt;

  /* NLA-strip this data represents. */
  NlaStrip *strip;

  /* dummy vals for transform to write in - must have 3 elements... */
  /* start handle. */
  float h1[3];
  /* end handle. */
  float h2[3];

  /* index of track that strip is currently in. */
  int trackIndex;

  /* This index is relative to the init first track at the start of transforming and
   * thus can be negative when the tracks list grows downward. */
  int signed_track_index;

  /* handle-index: 0 for dummy entry, -1 for start, 1 for end, 2 for both ends. */
  int handle;
};

static bool is_overlap(const float left_bound_a,
                       const float right_bound_a,
                       const float left_bound_b,
                       const float right_bound_b)
{
  return (left_bound_a < right_bound_b) && (right_bound_a > left_bound_b);
}

static bool nlastrip_is_overlap(const NlaStrip *strip_a,
                                const float offset_a,
                                const NlaStrip *strip_b,
                                const float offset_b)
{
  return is_overlap(strip_a->start + offset_a,
                    strip_a->end + offset_a,
                    strip_b->start + offset_b,
                    strip_b->end + offset_b);
}

/* Assume strips to horizontally translate (shuffle) are tagged w
 * NLASTRIP_FLAG_INVALID_LOCATION.
 * return total sided offset that results in no overlaps
 * between tagged strips and non-tagged strips. */
static float transdata_get_time_shuffle_offset_side(List *trans_datas, const bool shuffle_left)
{
  float total_offset = 0;

  float offset;
  do {
    offset = 0;

    LIST_FOREACH (LinkData *, link, trans_datas) {
      TransDataNla *trans_data = (TransDataNla *)link->data;
      NlaStrip *xformed_strip = trans_data->strip;

      LIST_FOREACH (NlaStrip *, non_xformed_strip, &trans_data->nlt->strips) {

        if (non_xformed_strip->flag & NLASTRIP_FLAG_INVALID_LOCATION) {
          continue;
        }

        /* Allow overlap with transitions. */
        if (non_xformed_strip->type == NLASTRIP_TYPE_TRANSITION) {
          continue;
        }

        if (!nlastrip_is_overlap(non_xformed_strip, 0, xformed_strip, total_offset)) {
          continue;
        }

        offset = shuffle_left ?
                     fmin(offset, non_xformed_strip->start - (xformed_strip->end + total_offset)) :
                     fmax(offset, non_xformed_strip->end - (xformed_strip->start + total_offset));
      }
    }

    total_offset += offset;
  } while (!IS_EQT(offset, 0.0f, 1e-4));
  /* Needs a epsilon greater than FLT_EPS bc strip->start/end could be non-integral,
   * and after those calcs, `offset` could fall outside of FLT_EPS. */

  return total_offset;
}

/* Assumes strips to horizontally translate (shuffle) are tagged with
 * NLASTRIP_FLAG_INVALID_LOCATION.
 * return minimal total signed offset that results in no overlaps between tagged strips and
 * non-tagged strips. */
static float transdata_get_time_shuffle_offset(List trans_datas)
{
  const float offset_left = transdata_get_time_shuffle_offset_side(trans_datas, true);
  const float offset_right = transdata_get_time_shuffle_offset_side(trans_datas, false);
  lib_assert(offset_left <= 0);
  lib_assert(offset_right >= 0);

  return -offset_left < offset_right ? offset_left : offset_right;
}

/* Assumes all of given trans_datas are part of the same ID.
 * param shuffle_direction: the direction the strip is traveling. 1 is towards the bottom
 * of the stack, -1 is away from it
 * param r_total_offset: The minimal total signed offset that results in valid strip track-moves
 * for all strips from \a trans_datas
 * returns true if \a r_total_offset results in a valid offset, false if no solution exists in the
 * desired direction. */
static bool transdata_get_track_shuffle_offset_side(List *trans_datas,
                                                    const int shuffle_direction,
                                                    int *r_total_offset)
{
  *r_total_offset = 0;
  if (lib_list_is_empty(trans_datas)) {
    return false;
  }

  LinkData *first_link = static_cast<LinkData *>(trans_datas->first);
  TransDataNla *first_transdata = static_cast<TransDataNla *>(first_link->data);
  AnimData *adt = dune_animdata_from_id(first_transdata->id);
  List *tracks = &adt->nla_tracks;

  int offset;
  do {
    offset = 0;

    LIST_FOREACH (LinkData *, link, trans_datas) {
      TransDataNla *trans_data = (TransDataNla *)link->data;

      NlaTrack *dst_track = static_cast<NlaTrack *>(
          lib_findlink(tracks, trans_data->trackIndex + *r_total_offset));

      /* Cannot keep moving strip in given track direction. No solution. */
      if (dst_track == nullptr) {
        return false;
      }

      /* Shuffle only if track is locked or library override. */
      if (((dst_track->flag & NLATRACK_PROTECTED) == 0) &&
          !dune_nlatrack_is_nonlocal_in_liboverride(trans_data->id, dst_track))
      {
        continue;
      }

      offset = shuffle_direction;
      break;
    }

    *r_total_offset += offset;
  } while (offset != 0);

  return true;
}

/* Assumes all of given trans_datas are part of the same Id
 * param r_track_offset: The minimal total signed offset that results in valid strip track-moves
 * for all strips from trans_datas.
 * Return true if r_track_offset results in a valid offset
 * false if no solution exists in
 * either direction. */
static bool transdata_get_track_shuffle_offset(List *trans_datas, int *r_track_offset)
{
  int offset_down = 0;
  const bool down_valid = transdata_get_track_shuffle_offset_side(trans_datas, 1, &offset_down);

  int offset_up = 0;
  const bool up_valid = transdata_get_track_shuffle_offset_side(trans_datas, -1, &offset_up);

  if (down_valid && up_valid) {
    if (offset_down < abs(offset_up)) {
      *r_track_offset = offset_down;
    }
    else {
      *r_track_offset = offset_up;
    }
  }
  else if (down_valid) {
    *r_track_offset = offset_down;
  }
  else if (up_valid) {
    *r_track_offset = offset_up;
  }

  return down_valid || up_valid;
}

/* Transform application to NLA strips */
static void nlatrack_truncate_tmp_tracks(AnimCxt *ac)
{
  short filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_ANIMDATA | ANIMFILTER_FCURVESONLY);
  List anim_data = {nullptr, nullptr};
  animdata_filter(
      ac, &anim_data, eAnimFilterFlags(filter), ac->data, eAnimContTypes(ac->datatype));

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    List *nla_tracks = &ale->adt->nla_tracks;

    /* Remove top tracks that weren't necessary. */
    LIST_FOREACH_BACKWARD_MUTABLE (NlaTrack *, track, nla_tracks) {
      if (!(track->flag & NLATRACK_TMP_ADDED)) {
        break;
      }
      if (track->strips.first != nullptr) {
        break;
      }
      dune_nlatrack_remove_and_free(nla_tracks, track, true);
    }

    /* Remove bottom tracks that weren't necessary. */
    LIST_FOREACH_MUTABLE (NlaTrack *, track, nla_tracks) {
      /* Lib override tracks are the first N tracks. 
       * They're never tmp and determine
       * where we start removing tmps. */
      if ((track->flag & NLATRACK_OVERRIDELIB_LOCAL) == 0) {
        continue;
      }
      if (!(track->flag & NLATRACK_TMP_ADDED)) {
        break;
      }
      if (track->strips.first != nullptr) {
        break;
      }
      dune_nlatrack_remove_and_free(nla_tracks, track, true);
    }

    /* Clear tmp flag. */
    LIST_FOREACH_MUTABLE (NlaTrack *, track, nla_tracks) {
      track->flag &= ~NLATRACK_TMP_ADDED;
    }
  }

  animdata_freelist(&anim_data);
}

/* Transform application to NLA strips */
/* Apply translation to the given NlaStrip.
 * param strip_api_ptr: The Api ptr of the NLA strip to mod.
 * param transdata: The transformation info struct. */
static void applyTransformNLA_translation(ApiPtr *strip_api_ptr, const TransDataNla *transdata)
{
  /* We write these twice to avoid truncation errors which can arise when
   * moving the strips a large distance using numeric input #33852 */
  api_float_set(strip_api_ptr, "frame_start", transdata->h1[0]);
  api_float_set(strip_api_ptr, "frame_end", transdata->h2[0]);

  api_float_set(strip_api_ptr, "frame_start", transdata->h1[0]);
  api_float_set(strip_api_ptr, "frame_end", transdata->h2[0]);
}

static void applyTransformNLA_timeScale(ApiPtr *strip_api_ptr, const float val)
{
  api_float_set(strip_rna_ptr, "scale", val);
}

/* Reorder strips for proper nla stack eval while dragging. */
static void nlastrip_overlap_reorder(TransDataNla *tdn, NlaStrip *strip)
{
  while (strip->prev != nullptr && tdn->h1[0] < strip->prev->start) {
    lib_list_swaplinks(&tdn->nlt->strips, strip, strip->prev);
  }
  while (strip->next != nullptr && tdn->h1[0] > strip->next->start) {
    lib_list_swaplinks(&tdn->nlt->strips, strip, strip->next);
  }
}

/* Flag overlaps with adjacent strips.
 * Since the strips are re-ordered as they're transformed, we only have to check adjacent
 * strips for overlap instead of all of them. */
static void nlastrip_flag_overlaps(NlaStrip *strip)
{

  NlaStrip *adj_strip = strip->prev;
  if (adj_strip != nullptr && !(adj_strip->flag & NLASTRIP_FLAG_SEL) &&
      nlastrip_is_overlap(strip, 0, adj_strip, 0))
  {
    strip->flag |= NLASTRIP_FLAG_INVALID_LOCATION;
  }
  adj_strip = strip->next;
  if (adj_strip != nullptr && !(adj_strip->flag & NLASTRIP_FLAG_SEL) &&
      nlastrip_is_overlap(strip, 0, adj_strip, 0))
  {
    strip->flag |= NLASTRIP_FLAG_INVALID_LOCATION;
  }
}

/* Check Transform data at given Strip, fix any overlap.
 * Then apply Transform. */
static void nlastrip_fix_overlapping(TransInfo *t, TransDataNla *tdn, NlaStrip *strip)
{
  /* 1; check if the proposed transform locations would overlap with any neighboring
   * strips (barring transitions) which are absolute barriers since they are not being moved
   * this is done as a iterative procedure (done 5 times max for now). */
  short iter_max = 4;
  NlaStrip *prev = dune_nlastrip_prev_in_track(strip, true);
  NlaStrip *next = dune_nlastrip_next_in_track(strip, true);

  for (short iter = 0; iter <= iter_max; iter++) {
    const bool p_exceeded = (prev != nullptr) && (tdn->h1[0] < prev->end);
    const bool n_exceeded = (next != nullptr) && (tdn->h2[0] > next->start);

    if ((p_exceeded && n_exceeded) || (iter == iter_max)) {
      /* Both endpoints exceeded (or iter ping-pong'd meaning that we need a compromise).
       * - Simply crop strip to fit within the bounds of the strips bounding it.
       * - If there were no neighbors, clear the transforms
       *   (make it default to the strip's current vals).  */
      if (prev && next) {
        tdn->h1[0] = prev->end;
        tdn->h2[0] = next->start;
      }
      else {
        tdn->h1[0] = strip->start;
        tdn->h2[0] = strip->end;
      }
    }
    else if (n_exceeded) {
      /* move backwards */
      float offset = tdn->h2[0] - next->start;

      tdn->h1[0] -= offset;
      tdn->h2[0] -= offset;
    }
    else if (p_exceeded) {
      /* more forwards */
      float offset = prev->end - tdn->h1[0];

      tdn->h1[0] += offset;
      tdn->h2[0] += offset;
    }
    else { /* all is fine and well */
      break;
    }
  }

  /* Use Api to write the vals to ensure that constraints on these are obeyed
   * (e.g. for transition strips, the vals are taken from the neighbors). */
  PointerRNA strip_ptr = api_ptr_create(nullptr, &ApiNlaStrip, strip);

  switch (t->mode) {
    case TFM_TIME_EXTEND:
    case TFM_TIME_SCALE: {
      /* The final scale is the product of the original strip scale (from before the
       * transform op started) and the current scale value of this transform op. */
      const float originalStripScale = tdn->h1[2];
      const float newStripScale = originalStripScale * t->values_final[0];
      applyTransformNLA_timeScale(&strip_ptr, newStripScale);
      applyTransformNLA_translation(&strip_ptr, tdn);
      break;
    }
    case TFM_TRANSLATION:
      applyTransformNLA_translation(&strip_ptr, tdn);
      break;
    default:
      printf("recalcData_nla: unsupported NLA transformation mode %d\n", t->mode);
      break;
  }
}

/* NLA Transform Creation */
static void createTransNlaData(Cxt *C, TransInfo *t)
{
  Scene *scene = t->scene;
  SpaceNla *snla = nullptr;
  TransData *td = nullptr;
  TransDataNla *tdn = nullptr;

  AnimCxt ac;
  List anim_data = {nullptr, nullptr};
  int filter;

  int count = 0;

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  /* determine type of data we are op on */
  if (animdata_get_cxt(C, &ac) == 0) {
    return;
  }
  snla = (SpaceNla *)ac.sl;

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_FCURVESONLY);
  animdata_filter(
      &ac, &anim_data, eAnimFilterFlags(filter), ac.data, eAnimContTypes(ac.datatype));

  /* which side of the current frame should be allowed */
  if (t->mode == TFM_TIME_EXTEND) {
    t->frame_side = transform_convert_frame_side_dir_get(t, float(scene->r.cfra));
  }
  else {
    /* normal transform - both sides of current frame are considered */
    t->frame_side = 'B';
  }

  /* loop 1: count how many strips are sel (consider each strip as 2 points) */
  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    NlaTrack *nlt = (NlaTrack *)ale->data;

    /* make some meta-strips for chains of sel strips */
    dune_nlastrips_make_metas(&nlt->strips, true);

    /* only consider sel strips */
    LIST_FOREACH (NlaStrip *, strip, &nlt->strips) {
      /* TODO: can make strips have handles later on. */
      /* transition strips can't get directly transformed */
      if (strip->type == NLASTRIP_TYPE_TRANSITION) {
        continue;
      }
      if ((strip->flag & NLASTRIP_FLAG_SEL) == 0) {
        continue;
      }
      if (FrameOnMouseSide(t->frame_side, strip->start, float(scene->r.cfra))) {
        count++;
      }
      if (FrameOnMouseSide(t->frame_side, strip->end, float(scene->r.cfra))) {
        count++;
      }
    }
  }

  /* stop if trying to build list if nothing sel */
  if (count == 0) {
    /* clear tmp metas that may have been created but aren't needed now
     * bc they fell on the wrong side of scene->r.cfra */
    LIST_FOREACH (AnimListElem *, ale, &anim_data) {
      NlaTrack *nlt = (NlaTrack *)ale->data;
      dune_nlastrips_clear_metas(&nlt->strips, false, true);
    }

    /* cleanup tmp list */
    animdata_freelist(&anim_data);
    return;
  }

  /* alloc mem for data */
  tc->data_len = count;

  tc->data = static_cast<TransData *>(
      mem_calloc(tc->data_len * sizeof(TransData), "TransData(NLA Editor)"));
  td = tc->data;
  tc->custom.type.data = tdn = static_cast<TransDataNla *>(
      mem_calloc(tc->data_len * sizeof(TransDataNla), "TransDataNla (NLA Editor)"));
  tc->custom.type.use_free = true;

  /* loop 2: build transdata array */
  LISTBASE_FOREACH (bAnimListElem *, ale, &anim_data) {
    /* only if a real NLA-track */
    if (ale->type == ANIMTYPE_NLATRACK) {
      AnimData *adt = ale->adt;
      NlaTrack *nlt = (NlaTrack *)ale->data;

      /* only consider selected strips */
      LIST_FOREACH (NlaStrip *, strip, &nlt->strips) {
        /* We can make strips have handles later on. */
        /* transition strips can't get directly transformed */
        if (strip->type == NLASTRIP_TYPE_TRANSITION) {
          continue;
        }
        if ((strip->flag & NLASTRIP_FLAG_SELECT) == 0) {
          continue;
        }

        /* Our transform data is constructed as follows:
         * - Only the handles on the right side of the current-frame get included.
         * - `td` structs are transform-elements operated on by the transform system and
         *   represent a single handle. The storage/ptr used (`val` or `loc`) depends
         *   on whether we're scaling or transforming. Ultimately tho, the handles the `td`
         *   writes to will simply be a dummy in `tdn`.
         * - For each strip being transformed, a single `tdn` struct is used, so in some
         *   cases, there will need to be 1 of these `tdn` elements in the array skipped. */
        float center[3], yval;

        /* 1: init `tdn` settings. */
        tdn->id = ale->id;
        tdn->oldTrack = tdn->nlt = nlt;
        tdn->strip = strip;
        tdn->trackIndex = lib_findindex(&adt->nla_tracks, nlt);
        tdn->signed_track_index = tdn->trackIndex;

        yval = float(tdn->trackIndex * NLATRACK_STEP(snla));

        tdn->h1[0] = strip->start;
        tdn->h1[1] = yval;
        tdn->h2[0] = strip->end;
        tdn->h2[1] = yval;
        tdn->h1[2] = tdn->h2[2] = strip->scale;

        center[0] = float(scene->r.cfra);
        center[1] = yval;
        center[2] = 0.0f;

        /* set td's based on which handles are applicable */
        if (FrameOnMouseSide(t->frame_side, strip->start, float(scene->r.cfra))) {
          /* set tdn: assume has only 1 handle for now */
          tdn->handle = -1;

          /* Link the transform data to this data. */
          td->loc = tdn->h1;
          copy_v3_v3(td->iloc, tdn->h1);

          if (ELEM(t->mode, TFM_TRANSLATION, TFM_TIME_EXTEND)) {
            /* Store all the other gunk that is req by transform. */
            copy_v3_v3(td->center, center);
            td->axismtx[2][2] = 1.0f;
            td->flag |= TD_SELECTED;
            unit_m3(td->mtx);
            unit_m3(td->smtx);
          }

          td->extra = tdn;
          td++;
        }
        if (FrameOnMouseSide(t->frame_side, strip->end, float(scene->r.cfra))) {
          /* if tdn is alrdy holding the start handle,
           * then we're doing both, otherwise, only end */
          tdn->handle = (tdn->handle) ? 2 : 1;

          /* Now, link the transform data up to this data. */
          td->loc = tdn->h2;
          copy_v3_v3(td->iloc, tdn->h2);

          if (ELEM(t->mode, TFM_TRANSLATION, TFM_TIME_EXTEND)) {
            /* Store all the other gunk that is req by transform. */
            copy_v3_v3(td->center, center);
            td->axismtx[2][2] = 1.0f;
            td->flag |= TD_SELECTED;
            unit_m3(td->mtx);
            unit_m3(td->smtx);
          }

          td->extra = tdn;
          td++;
        }

        /* If both handles were used, skip the next tdn (i.e. leave it blank)
         * since the counting code is dumb.
         * Otherwise, just advance to the next one. */
        if (tdn->handle == 2) {
          tdn += 2;
        }
        else if (tdn->handle) {
          tdn++;
        }
      }
    }
  }

  lib_assert(tdn <= (((TransDataNla *)tc->custom.type.data) + tc->data_len));

  /* cleanup tmp list */
  animdata_freelist(&anim_data);
}

static void recalcData_nla(TransInfo *t)
{
  SpaceNla *snla = (SpaceNla *)t->area->spacedata.first;

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  /* For each strip we've got, perform some additional valid of the vals
   * that got set before using api to set the val (which does some special
   * ops when setting these vals to make sure that everything works ok). */
  TransDataNla *tdn = static_cast<TransDataNla *>(tc->custom.type.data);
  for (int i = 0; i < tc->data_len; i++, tdn++) {
    NlaStrip *strip = tdn->strip;
    int delta_y1, delta_y2;

    /* if this tdn has no handles, that means it is just a dummy that should be skipped */
    if (tdn->handle == 0) {
      continue;
    }
    strip->flag &= ~NLASTRIP_FLAG_INVALID_LOCATION;

    /* set refresh tags for obs using this anim,
     * BUT only if realtime updates are enabled  */
    if ((snla->flag & SNLA_NOREALTIMEUPDATES) == 0) {
      anim_id_update(cxt_data_main(t->cxt), tdn->id);
    }

    /* if canceling transform, just write the vals wo validating, then move on */
    if (t->state == TRANS_CANCEL) {
      /* clear the vals by directly overwriting the originals, but also need to restore
       * endpoints of neighboring transition-strips */

      /* start */
      strip->start = tdn->h1[0];

      if ((strip->prev) && (strip->prev->type == NLASTRIP_TYPE_TRANSITION)) {
        strip->prev->end = tdn->h1[0];
      }

      /* end */
      strip->end = tdn->h2[0];

      if ((strip->next) && (strip->next->type == NLASTRIP_TYPE_TRANSITION)) {
        strip->next->start = tdn->h2[0];
      }

      strip->scale = tdn->h1[2];

      /* flush transforms to child strips (since this should be a meta) */
      dune_nlameta_flush_transforms(strip);

      /* restore to original track (if needed) */
      if (tdn->oldTrack != tdn->nlt) {
        /* Just append to end of list for now,
         * since strips get sorted in special_aftertrans_update(). */
        lib_remlink(&tdn->nlt->strips, strip);
        lib_addtail(&tdn->oldTrack->strips, strip);
      }

      continue;
    }

    const bool nlatrack_isliboverride = dune_nlatrack_is_nonlocal_in_liboverride(tdn->id, tdn->nlt);
    const bool allow_overlap = !nlatrack_isliboverride && ELEM(t->mode, TFM_TRANSLATION);

    if (allow_overlap) {
      nlastrip_overlap_reorder(tdn, strip);

      /* Directly flush. */
      strip->start = tdn->h1[0];
      strip->end = tdn->h2[0];
    }
    else {
      nlastrip_fix_overlapping(t, tdn, strip);
    }

    /* flush transforms to child strips (since this should be a meta) */
    dune_nlameta_flush_transforms(strip);

    /* Now, check if we need to try and move track:
     * we need to calc both,
     * as only one may have been altered by transform if only 1 handle moved. */
    /* In LibOverride case, we cannot move strips across tracks that come from the linked data */
    const bool is_liboverride = ID_IS_OVERRIDE_LIB(tdn->id);
    if (nlatrack_isliboverride) {
      continue;
    }

    delta_y1 = (int(tdn->h1[1]) / NLATRACK_STEP(snla) - tdn->signed_track_index);
    delta_y2 = (int(tdn->h2[1]) / NLATRACK_STEP(snla) - tdn->signed_track_index);

    /* Move strip into track in the requested direction. */
    /* If we cannot find the strip in the track, this strip has moved tracks alrdy (if multiple
     * strips using the same action from equal Ids such as meshes or shape-keys are sel)
     * so can be skipped. */
    if ((delta_y1 || delta_y2) && lib_findindex(&tdn->nlt->strips, strip) != -1) {
      int delta = (delta_y2) ? delta_y2 : delta_y1;

      AnimData *anim_data = dune_animdata_from_id(tdn->id);
      List *nla_tracks = &anim_data->nla_tracks;

      NlaTrack *old_track = tdn->nlt;
      NlaTrack *dst_track = nullptr;

      /* Calc the total new tracks needed
       * Determine dst_track, which will end up being nullptr, the last library override
       * track, or a normal local track. The first two cases lead to delta_new_tracks!=0.
       * The last case leads to `delta_new_tracks == 0`. */
      int delta_new_tracks = delta;

      /* it's possible to drag a strip fast enough to make delta > |1|. We only want to process
       * 1 track shift at a time.  */
      CLAMP(delta_new_tracks, -1, 1);
      dst_track = old_track;

      while (delta_new_tracks < 0) {
        dst_track = dst_track->prev;
        delta_new_tracks++;
      }

      /* Assume all lib tracks are grouped at the bottom of the nla stack.
       * Thus, no need to check for them when moving tracks upward. */
      while (delta_new_tracks > 0) {
        dst_track = dst_track->next;
        delta_new_tracks--;
      }

      for (int j = 0; j < -delta_new_tracks; j++) {
        NlaTrack *new_track = dune_nlatrack_new();
        new_track->flag |= NLATRACK_TMP_ADDED;
        dune_nlatrack_insert_before(
            nla_tracks, (NlaTrack *)nla_tracks->first, new_track, is_liboverride);
        dst_track = new_track;
      }

      for (int j = 0; j < delta_new_tracks; j++) {
        NlaTrack *new_track = dune_nlatrack_new();
        new_track->flag |= NLATRACK_TEMPORARILY_ADDED;

        dune_nlatrack_insert_after(
            nla_tracks, (NlaTrack *)nla_tracks->last, new_track, is_liboverride);
        dst_track = new_track;
      }

      /* If the destination track is null, then we need to go to the last track. */
      if (dst_track == nullptr) {
        dst_track = old_track;
      }

      /* Move strip from old_track to dst_track. */
      if (dst_track != old_track) {
        dune_nlatrack_remove_strip(old_track, strip);
        dune_nlastrips_add_strip_unsafe(&dst_track->strips, strip);

        tdn->nlt = dst_track;
        tdn->signed_track_index += delta;
        tdn->trackIndex = lib_findindex(nla_tracks, dst_track);
      }

      /* Ensure we set the target track as active. */
      dune_nlatrack_set_active(nla_tracks, dst_track);

      if (tdn->nlt->flag & NLATRACK_PROTECTED) {
        strip->flag |= NLASTRIP_FLAG_INVALID_LOCATION;
      }
    }

    nlastrip_flag_overlaps(strip);
  }
}

/* Special After Transform NLA */
struct IdGroupedTransData {
  IdGroupedTransData *next, *prev;
  Id *id;
  List trans_datas;
};

/* horizontally translate (shuffle) the transformed strip to a non-overlapping state. */
static void nlastrip_shuffle_transformed(TransDataContainer *tc, TransDataNla *first_trans_data)
{
  /* Element: IdGroupedTransData. */
  ListBase grouped_trans_datas = {nullptr, nullptr};

  /* Flag all non-lib-override transformed strips so we can distinguish them when shuffling.
   * Group trans_datas by Id so shuffling is unique per Ir */
  {
    TransDataNla *tdn = first_trans_data;
    for (int i = 0; i < tc->data_len; i++, tdn++) {

      /* Skip dummy handles. */
      if (tdn->handle == 0) {
        continue;
      }

      /* For strips w/in lib override tracks, don't do any shuffling at all. Unsure how
       * lib overrides should behave so, for now, they're treated as mostly immutable. */
      if ((tdn->nlt->flag & NLATRACK_OVERRIDELIB_LOCAL) == 0) {
        continue;
      }

      tdn->strip->flag |= NLASTRIP_FLAG_INVALID_LOCATION;

      IdGroupedTransData *dst_group = nullptr;
      /* Find dst_group with matching Id */
      LIST_FOREACH (IdGroupedTransData *, group, &grouped_trans_datas) {
        if (group->id == tdn->id) {
          dst_group = group;
          break;
        }
      }
      if (dst_group == nullptr) {
        dst_group = static_cast<IdGroupedTransData *>(
            mem_calloc(sizeof(IdGroupedTransData), __func__));
        dst_group->id = tdn->id;
        lib_addhead(&grouped_trans_datas, dst_group);
      }

      lib_addtail(&dst_group->trans_datas, lib_genericNodeN(tdn));
    }
  }

  /* Apply shuffling. */
  LIST_FOREACH (IdGroupedTransData *, group, &grouped_trans_datas) {
    List *trans_datas = &group->trans_datas;

    /* Apply vertical shuffle. */
    int minimum_track_offset = 0;
    transdata_get_track_shuffle_offset(trans_datas, &minimum_track_offset);
    if (minimum_track_offset != 0) {
      List *tracks = &dune_animdata_from_id(group->id)->nla_tracks;

      LIST_FOREACH (LinkData *, link, trans_datas) {
        TransDataNla *trans_data = (TransDataNla *)link->data;
        NlaTrack *dst_track = static_cast<NlaTrack *>(
            lib_findlink(tracks, trans_data->trackIndex + minimum_track_offset));

        NlaStrip *strip = trans_data->strip;
        if ((dst_track->flag & NLATRACK_PROTECTED) != 0) {

          dune_nlatrack_remove_strip(trans_data->nlt, strip);
          dune_nlatrack_add_strip(dst_track, strip, false);

          trans_data->nlt = dst_track;
        }
        else {
          /* if dest track is locked, we need revert strip to source track. */
          printf("Cannot moved. Target track '%s' is locked. \n", trans_data->nlt->name);
          int old_track_index = lib_findindex(tracks, trans_data->oldTrack);
          NlaTrack *old_track = static_cast<NlaTrack *>(lib_findlink(tracks, old_track_index));

          dune_nlatrack_remove_strip(trans_data->nlt, strip);
          dune_nlastrips_add_strip_unsafe(&old_track->strips, strip);

          trans_data->nlt = old_track;
        }
      }
    }

    /* Apply horizontal shuffle. */
    const float minimum_time_offset = transdata_get_time_shuffle_offset(trans_datas);
    LIST_FOREACH (LinkData *, link, trans_datas) {
      TransDataNla *trans_data = (TransDataNla *)link->data;
      NlaStrip *strip = trans_data->strip;

      strip->start += minimum_time_offset;
      strip->end += minimum_time_offset;
      dune_nlameta_flush_transforms(strip);
    }
  }

  /* Mem cleanup. */
  LIST_FOREACH (IdGroupedTransData *, group, &grouped_trans_datas) {
    lib_freelist(&group->trans_datas);
  }
  lib_freelist(&grouped_trans_datas);
}

static void special_aftertrans_update__nla(Cxt *C, TransInfo *t)
{
  AnimCxt ac;

  /* init relevant anim-cxt 'cxt' data */
  if (animdata_get_cxt(C, &ac) == 0) {
    return;
  }

  if (!ac.datatype) {
    return;
  }

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);
  TransDataNla *first_trans_data = static_cast<TransDataNla *>(tc->custom.type.data);

  /* Shuffle transformed strips. */
  if (ELEM(t->mode, TFM_TRANSLATION) && t->state != TRANS_CANCEL) {
    nlastrip_shuffle_transformed(tc, first_trans_data);
  }

  /* Clear NLASTRIP_FLAG_INVALID_LOCATION flag. */
  TransDataNla *tdn = first_trans_data;
  for (int i = 0; i < tc->data_len; i++, tdn++) {
    if (tdn->strip == nullptr) {
      continue;
    }

    tdn->strip->flag &= ~NLASTRIP_FLAG_INVALID_LOCATION;
  }

  List anim_data = {nullptr, nullptr};
  short filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_FOREDIT | ANIMFILTER_FCURVESONLY);

  /* get tracks to work on */
  animdata_filter(
      &ac, &anim_data, eAnimFilterFlags(filter), ac.data, eAnimContaTypes(ac.datatype));

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    NlaTrack *nlt = (NlaTrack *)ale->data;

    /* make sure strips are in order again */
    dune_nlatrack_sort_strips(nlt);

    /* remove the tmp metas */
    dune_nlastrips_clear_metas(&nlt->strips, false, true);
  }

  /* General refresh for the outliner because the following might have happened:
   * strips moved between tracks
   * strips swapped order
   * dup-move moves to different track. */
  win_ev_add_notifier(C, NC_ANIM | ND_NLA | NA_ADDED, nullptr);

  /* free tmp mem */
  animdata_freelist(&anim_data);

  /* Truncate temporarily added tracks. */
  nlatrack_truncate_tmp_tracks(&ac);

  /* Perform after-transform validation. */
  ed_nla_postop_refresh(&ac);
}

/** \} */

TransConvertTypeInfo TransConvertType_NLA = {
    /*flags*/ (T_POINTS | T_2D_EDIT),
    /*create_trans_data*/ createTransNlaData,
    /*recalc_data*/ recalcData_nla,
    /*special_aftertrans_update*/ special_aftertrans_update__nla,
};
