#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "types_dpen.h"
#include "types_scene.h"

#include "dune_fcurve.h"
#include "dune_dpen.h"
#include "dune_report.h"

#include "ed_anim_api.h"
#include "ed_dpen.h"
#include "ed_keyframes_edit.h"
#include "ed_markers.h"

#include "wm_api.h"

#include "DEG_depsgraph.h"

/* ***************************************** */
/* NOTE ABOUT THIS FILE:
 * This file contains code for editing Dune Pen data in the Action Editor
 * as a 'keyframes', so that a user can adjust the timing of DPen drawings.
 * Functions for selecting DPen frames.
 */
/* ***************************************** */
/* Generics - Loopers */

bool ed_dpen_layer_frames_looper(DPenLayer *dpl,
                                    Scene *scene,
                                    bool (*dpf_cb)(DPenFrame *, Scene *))
{
  /* error checker */
  if (dpl == NULL) {
    return false;
  }

  /* do loop */
  LISTBASE_FOREACH (DPenFrame *, dpf, &dpl->frames) {
    /* execute callback */
    if (dpf_cb(dpf, scene)) {
      return true;
    }
  }

  /* nothing to return */
  return false;
}

/* ****************************************** */
/* Data Conversion Tools */

void ed_dpen_layer_make_cfra_list(DPenLayer *dpl, ListBase *elems, bool onlysel)
{
  CfraElem *ce;

  /* error checking */
  if (ELEM(NULL, dpl, elems)) {
    return;
  }

  /* loop through dp-frames, adding */
  LISTBASE_FOREACH (DPenFrame *, dpf, &dpl->frames) {
    if ((onlysel == 0) || (dpf->flag & DPEN_FRAME_SELECT)) {
      ce = MEM_callocN(sizeof(CfraElem), "CfraElem");

      ce->cfra = (float)dpf->framenum;
      ce->sel = (dpf->flag & DPEN_FRAME_SELECT) ? 1 : 0;

      lib_addtail(elems, ce);
    }
  }
}

/* ***************************************** */
/* Selection Tools */

bool ed_dpen_layer_frame_select_check(const DPenLayer *dpl)
{
  /* error checking */
  if (dpl == NULL) {
    return false;
  }

  /* stop at the first one found */
  LISTBASE_FOREACH (const DPenFrame *, dpf, &dpl->frames) {
    if (dpf->flag & DPEN_FRAME_SELECT) {
      return true;
    }
  }

  /* not found */
  return false;
}

/* helper function - select gp-frame based on SELECT_* mode */
static void dpen_frame_select(DPenFrame *dpf, short select_mode)
{
  if (dpf == NULL) {
    return;
  }

  switch (select_mode) {
    case SELECT_ADD:
      dpf->flag |= DPEN_FRAME_SELECT;
      break;
    case SELECT_SUBTRACT:
      dpf->flag &= ~DPEN_FRAME_SELECT;
      break;
    case SELECT_INVERT:
      dpf->flag ^= DPEN_FRAME_SELECT;
      break;
  }
}

void ed_dpen_select_frames(DPenLayer *dpl, short select_mode)
{
  /* error checking */
  if (dpl == NULL) {
    return;
  }

  /* handle according to mode */
  LISTBASE_FOREACH (DPenFrame *, dpf, &dpl->frames) {
    dpen_frame_select(dpf, select_mode);
  }
}

void ed_dpen_layer_frame_select_set(DPenLayer *dpl, short mode)
{
  /* error checking */
  if (dpl == NULL) {
    return;
  }

  /* now call the standard function */
  ed_dpen_select_frames(dpl, mode);
}

void ed_dpen_select_frame(DPenLayer *dpl, int selx, short select_mode)
{
  DPenFrame *dpf;

  if (dpl == NULL) {
    return;
  }

  dpf = dune_dpen_layer_frame_find(dpl, selx);

  if (dpf) {
    dpen_frame_select(dpf, select_mode);
  }
}

void ed_dpen_layer_frames_select_box(DPenLayer *dpl, float min, float max, short select_mode)
{
  if (dpl == NULL) {
    return;
  }

  /* only select those frames which are in bounds */
  LISTBASE_FOREACH (DPenFrame *, dpf, &dpl->frames) {
    if (IN_RANGE(dpf->framenum, min, max)) {
      dpen_frame_select(dpf, select_mode);
    }
  }
}

void ed_dpen_layer_frames_select_region(KeyframeEditData *ked,
                                           DPenLayer *dpl,
                                           short tool,
                                           short select_mode)
{
  if (dpl == NULL) {
    return;
  }

  /* only select frames which are within the region */
  LISTBASE_FOREACH (DPenframe *, dpf, &dpl->frames) {
    /* construct a dummy point coordinate to do this testing with */
    float pt[2] = {0};

    pt[0] = dpf->framenum;
    pt[1] = ked->channel_y;

    /* check the necessary regions */
    if (tool == BEZT_OK_CHANNEL_LASSO) {
      /* Lasso */
      if (keyframe_region_lasso_test(ked->data, pt)) {
        dpen_frame_select(dpf, select_mode);
      }
    }
    else if (tool == BEZT_OK_CHANNEL_CIRCLE) {
      /* Circle */
      if (keyframe_region_circle_test(ked->data, pt)) {
        dpen_frame_select(dpf, select_mode);
      }
    }
  }
}

/* ***************************************** */
/* Frame Editing Tools */

bool ed_dpen_layer_frames_delete(DPenLayer *dpl)
{
  bool changed = false;

  /* error checking */
  if (dpl == NULL) {
    return false;
  }

  /* check for frames to delete */
  LISTBASE_FOREACH_MUTABLE (DPenFrame *, dpf, &dpl->frames) {
    if (dpf->flag & DPEN_FRAME_SELECT) {
      dune_pen_layer_frame_delete(dpl, dpf);
      changed = true;
    }
  }

  return changed;
}

void ed_dpen_layer_frames_duplicate(DPenLayer *dpl)
{
  /* error checking */
  if (dpl == NULL) {
    return;
  }

  /* Duplicate selected frames. */
  LISTBASE_FOREACH_MUTABLE (DPenFrame *, dpf, &dpl->frames) {

    /* duplicate this frame */
    if (dpf->flag & DPEN_FRAME_SELECT) {
      DPenFrame *dpfd;

      /* duplicate frame, and deselect self */
      dpfd = dune_dpen_frame_duplicate(dpf, true);
      dpf->flag &= ~DPEN_FRAME_SELECT;

      lib_insertlinkafter(&dpl->frames, dpf, dpfd);
    }
  }
}

void ED_gpencil_layer_frames_keytype_set(DPenLayer *dpl, short type)
{
  if (dpl == NULL) {
    return;
  }

  LISTBASE_FOREACH (DPenFrame *, dpf, &dpl->frames) {
    if (dpf->flag & DPEN_FRAME_SELECT) {
      dpf->key_type = type;
    }
  }
}

/* -------------------------------------- */
/* Copy and Paste Tools:
 * - The copy/paste buffer currently stores a set of DPEN_Layers, with temporary
 *   DPEN_Frames with the necessary strokes
 * - Unless there is only one element in the buffer,
 *   names are also tested to check for compatibility.
 * - All pasted frames are offset by the same amount.
 *   This is calculated as the difference in the times of the current frame and the
 *   'first keyframe' (i.e. the earliest one in all channels).
 * - The earliest frame is calculated per copy operation.
 */

/* globals for copy/paste data (like for other copy/paste buffers) */
static ListBase dpen_anim_copybuf = {NULL, NULL};
static int dpen_anim_copy_firstframe = 999999999;
static int dpen_anim_copy_lastframe = -999999999;
static int pen_anim_copy_cfra = 0;

void ed_dpen_anim_copybuf_free(void)
{
  dune_dpen_free_layers(&dpen_anim_copybuf);
  lib_listbase_clear(&dpen_anim_copybuf);

  dpen_anim_copy_firstframe = 999999999;
  dpen_anim_copy_lastframe = -999999999;
  dpen_anim_copy_cfra = 0;
}

bool ed_dpen_anim_copybuf_copy(DAnimContext *ac)
{
  ListBase anim_data = {NULL, NULL};
  DAnimListElem *ale;
  int filter;

  Scene *scene = ac->scene;

  /* clear buffer first */
  ed_dpen_anim_copybuf_free();

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* assume that each of these is a GP layer */
  for (ale = anim_data.first; ale; ale = ale->next) {
    ListBase copied_frames = {NULL, NULL};
    DPenLayer *dpl = (DPenLayer *)ale->data;

    /* loop over frames, and copy only selected frames */
    LISTBASE_FOREACH (DPenFrame *, dpf, &dpl->frames) {
      /* if frame is selected, make duplicate it and its strokes */
      if (dpf->flag & DPEN_FRAME_SELECT) {
        /* make a copy of this frame */
        DPenFrame *new_frame = dune_dpen_frame_duplicate(dpf, true);
        lib_addtail(&copied_frames, new_frame);

        /* extend extents for keyframes encountered */
        if (dpf->framenum < dpen_anim_copy_firstframe) {
          dpen_anim_copy_firstframe = dpf->framenum;
        }
        if (dpf->framenum > dpen_anim_copy_lastframe) {
          dpen_anim_copy_lastframe = dpf->framenum;
        }
      }
    }

    /* create a new layer in buffer if there were keyframes here */
    if (lib_listbase_is_empty(&copied_frames) == false) {
      DPenLayer *new_layer = MEM_callocN(sizeof(DPenLayer), "DPenCopyPasteLayer");
      lib_addtail(&dpen_anim_copybuf, new_layer);

      /* move over copied frames */
      lib_movelisttolist(&new_layer->frames, &copied_frames);
      lib_assert(copied_frames.first == NULL);

      /* make a copy of the layer's name - for name-based matching later... */
      lib_strncpy(new_layer->info, dpl->info, sizeof(new_layer->info));
    }
  }

  /* in case 'relative' paste method is used */
  dpen_anim_copy_cfra = CFRA;

  /* clean up */
  anim_animdata_freelist(&anim_data);

  /* check if anything ended up in the buffer */
  if (ELEM(NULL, dpen_anim_copybuf.first, dpen_anim_copybuf.last)) {
    dune_report(ac->reports, RPT_ERROR, "No keyframes copied to keyframes copy/paste buffer");
    return false;
  }

  /* report success */
  return true;
}

bool ed_dpen_anim_copybuf_paste(DAnimContext *ac, const short offset_mode)
{
  ListBase anim_data = {NULL, NULL};
  DAnimListElem *ale;
  int filter;

  Scene *scene = ac->scene;
  bool no_name = false;
  int offset = 0;

  /* check if buffer is empty */
  if (lib_listbase_is_empty(&dpen_anim_copybuf)) {
    dune_report(ac->reports, RPT_ERROR, "No data in buffer to paste");
    return false;
  }

  /* Check if single channel in buffer (disregard names if so). */
  if (dpen_anim_copybuf.first == dpen_anim_copybuf.last) {
    no_name = true;
  }

  /* methods of offset (eKeyPasteOffset) */
  switch (offset_mode) {
    case KEYFRAME_PASTE_OFFSET_CFRA_START:
      offset = (CFRA - dpen_anim_copy_firstframe);
      break;
    case KEYFRAME_PASTE_OFFSET_CFRA_END:
      offset = (CFRA - dpen_anim_copy_lastframe);
      break;
    case KEYFRAME_PASTE_OFFSET_CFRA_RELATIVE:
      offset = (CFRA - dpen_anim_copy_cfra);
      break;
    case KEYFRAME_PASTE_OFFSET_NONE:
      offset = 0;
      break;
  }

  /* filter data */
  /* TODO: try doing it with selection, then without selection limits. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_LIST_VISIBLE | ANIMFILTER_SEL |
            ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS);
  anim_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* from selected channels */
  for (ale = anim_data.first; ale; ale = ale->next) {
    DPenLayer *dpld = (DPenLayer *)ale->data;
    DPenLayer *dpls = NULL;
    DPenFrame *dpfs, *dpf;

    /* find suitable layer from buffer to use to paste from */
    for (dpls = dpen_anim_copybuf.first; dpls; dpls = dpls->next) {
      /* check if layer name matches */
      if ((no_name) || STREQ(dpls->info, dpld->info)) {
        break;
      }
    }

    /* this situation might occur! */
    if (dpls == NULL) {
      continue;
    }

    /* add frames from buffer */
    for (dpfs = dpls->frames.first; dpfs; dpfs = dpfs->next) {
      /* temporarily apply offset to buffer-frame while copying */
      dpfs->framenum += offset;

      /* get frame to copy data into (if no frame returned, then just ignore) */
      dpf = dune_dpen_layer_frame_get(dpld, dpfs->framenum, DPEN_GETFRAME_ADD_NEW);
      if (gpf) {
        /* Ensure to use same keyframe type. */
        dpf->key_type = dpfs->key_type;

        DPenStroke *dps, *dpsn;

        /* This should be the right frame... as it may be a pre-existing frame,
         * must make sure that only compatible stroke types get copied over
         * - We cannot just add a duplicate frame, as that would cause errors
         * - For now, we don't check if the types will be compatible since we
         *   don't have enough info to do so. Instead, we simply just paste,
         *   if it works, it will show up.
         */
        for (dps = dpfs->strokes.first; dps; dps = dps->next) {
          /* make a copy of stroke, then of its points array */
          dpsn = dune_dpen_stroke_duplicate(dps, true, true);

          /* append stroke to frame */
          lib_addtail(&dpf->strokes, dpsn);
        }

        /* if no strokes (i.e. new frame) added, free gpf */
        if (lib_listbase_is_empty(&dpf->strokes)) {
          dune_dpen_layer_frame_delete(dpld, dpf);
        }
      }

      /* unapply offset from buffer-frame */
      dpfs->framenum -= offset;
    }

    /* Tag destination datablock. */
    DEG_id_tag_update(ale->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  }

  /* clean up */
  anim_animdata_freelist(&anim_data);
  return true;
}

/* -------------------------------------- */
/* Snap Tools */

static bool dpen_frame_snap_nearest(DPenFrame *UNUSED(dpf), Scene *UNUSED(scene))
{
#if 0 /* NOTE: gpf->framenum is already an int! */
  if (dpf->flag & DPEN_FRAME_SELECT) {
    dpf->framenum = (int)(floor(dpf->framenum + 0.5));
  }
#endif
  return false;
}

static bool dpen_frame_snap_nearestsec(DPenFrame *dpf, Scene *scene)
{
  float secf = (float)FPS;
  if (dpf->flag & DPEN_FRAME_SELECT) {
    dpf->framenum = (int)(floorf(dpf->framenum / secf + 0.5f) * secf);
  }
  return false;
}

static bool dpen_frame_snap_cframe(DFrame *dpf, Scene *scene)
{
  if (dpf->flag & DPEN_FRAME_SELECT) {
    dpf->framenum = (int)CFRA;
  }
  return false;
}

static bool dpen_frame_snap_nearmarker(DPenFrame *dpf, Scene *scene)
{
  if (dpf->flag & DPEN_FRAME_SELECT) {
    dpf->framenum = (int)ed_markers_find_nearest_marker_time(&scene->markers,
                                                             (float)dpf->framenum);
  }
  return false;
}

void ed_dpen_layer_snap_frames(DPenLayer *dpl, Scene *scene, short mode)
{
  switch (mode) {
    case SNAP_KEYS_NEARFRAME: /* snap to nearest frame */
      ed_dpen_layer_frames_looper(dpl, scene, dpen_frame_snap_nearest);
      break;
    case SNAP_KEYS_CURFRAME: /* snap to current frame */
      ed_dpen_layer_frames_looper(dpl, scene, dpen_frame_snap_cframe);
      break;
    case SNAP_KEYS_NEARMARKER: /* snap to nearest marker */
      ed_dpen_layer_frames_looper(dpl, scene, dpen_frame_snap_nearmarker);
      break;
    case SNAP_KEYS_NEARSEC: /* snap to nearest second */
      ed_dpen_layer_frames_looper(dpl, scene, dpen_frame_snap_nearestsec);
      break;
    default: /* just in case */
      break;
  }
}

/* -------------------------------------- */
/* Mirror Tools */

static bool dpen_frame_mirror_cframe(DPenFrame *dpf, Scene *scene)
{
  int diff;

  if (dpf->flag & DPEN_FRAME_SELECT) {
    diff = CFRA - dpf->framenum;
    dpf->framenum = CFRA + diff;
  }

  return false;
}

static bool dpen_frame_mirror_yaxis(DPenFrame *dpf, Scene *UNUSED(scene))
{
  int diff;

  if (dpf->flag & DPEN_FRAME_SELECT) {
    diff = -dpf->framenum;
    dpf->framenum = diff;
  }

  return false;
}

static bool dpen_frame_mirror_xaxis(DPenFrame *dpf, Scene *UNUSED(scene))
{
  int diff;

  /* NOTE: since we can't really do this, we just do the same as for yaxis... */
  if (dpf->flag & DPEN_FRAME_SELECT) {
    diff = -dpf->framenum;
    dpf->framenum = diff;
  }

  return false;
}

static bool dpen_frame_mirror_marker(DPenFrame *dpf, Scene *scene)
{
  static TimeMarker *marker;
  static short initialized = 0;
  int diff;

  /* In order for this mirror function to work without
   * any extra arguments being added, we use the case
   * of gpf==NULL to denote that we should find the
   * marker to mirror over. The static pointer is safe
   * to use this way, as it will be set to null after
   * each cycle in which this is called.
   */

  if (df != NULL) {
    /* mirroring time */
    if ((df->flag & DPEN_FRAME_SELECT) && (marker)) {
      diff = (marker->frame - dpf->framenum);
      dpf->framenum = (marker->frame + diff);
    }
  }
  else {
    /* initialization time */
    if (initialized) {
      /* reset everything for safety */
      marker = NULL;
      initialized = 0;
    }
    else {
      /* try to find a marker */
      marker = ed_markers_get_first_selected(&scene->markers);
      if (marker) {
        initialized = 1;
      }
    }
  }

  return false;
}

void ed_dpen_layer_mirror_frames(DPenLayer *dpl, Scene *scene, short mode)
{
  switch (mode) {
    case MIRROR_KEYS_CURFRAME: /* mirror over current frame */
      ed_dpen_layer_frames_looper(dpl, scene, dpen_frame_mirror_cframe);
      break;
    case MIRROR_KEYS_YAXIS: /* mirror over frame 0 */
      ed_dpen_layer_frames_looper(dpl, scene, dpen_frame_mirror_yaxis);
      break;
    case MIRROR_KEYS_XAXIS: /* mirror over value 0 */
      ed_dpen_layer_frames_looper(dpl, scene, dpen_frame_mirror_xaxis);
      break;
    case MIRROR_KEYS_MARKER: /* mirror over marker */
      dpen_frame_mirror_marker(NULL, scene);
      ed_dpen_layer_frames_looper(dpl, scene, dpen_frame_mirror_marker);
      dpen_frame_mirror_marker(NULL, scene);
      break;
    default: /* just in case */
      ed_dpen_layer_frames_looper(dpl, scene, dpen_frame_mirror_yaxis);
      break;
  }
}
