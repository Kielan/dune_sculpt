#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_anim_types.h"
#include "DNA_object_types.h"
#include "DNA_text_types.h"

#include "BLI_blenlib.h"
#include "BLI_easing.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_sort_utils.h"

#include "BKE_anim_data.h"
#include "BKE_animsys.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_fcurve.h"
#include "BKE_fcurve_driver.h"
#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_lib_query.h"
#include "BKE_nla.h"

#include "BLO_read_write.h"

#include "RNA_access.h"

#include "CLG_log.h"

#define SMALL -1.0e-10
#define SELECT 1

static CLG_LogRef LOG = {"bke.fcurve"};

/* -------------------------------------------------------------------- */
/** \name F-Curve Data Create
 * \{ */

FCurve *BKE_fcurve_create(void)
{
  FCurve *fcu = MEM_callocN(sizeof(FCurve), __func__);
  return fcu;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name F-Curve Data Free
 * \{ */

void BKE_fcurve_free(FCurve *fcu)
{
  if (fcu == NULL) {
    return;
  }

  /* Free curve data. */
  MEM_SAFE_FREE(fcu->bezt);
  MEM_SAFE_FREE(fcu->fpt);

  /* Free RNA-path, as this were allocated when getting the path string. */
  MEM_SAFE_FREE(fcu->rna_path);

  /* Free extra data - i.e. modifiers, and driver. */
  fcurve_free_driver(fcu);
  free_fmodifiers(&fcu->modifiers);

  /* Free the f-curve itself. */
  MEM_freeN(fcu);
}

void BKE_fcurves_free(ListBase *list)
{
  FCurve *fcu, *fcn;

  /* Sanity check. */
  if (list == NULL) {
    return;
  }

  /* Free data - no need to call remlink before freeing each curve,
   * as we store reference to next, and freeing only touches the curve
   * it's given.
   */
  for (fcu = list->first; fcu; fcu = fcn) {
    fcn = fcu->next;
    BKE_fcurve_free(fcu);
  }

  /* Clear pointers just in case. */
  BLI_listbase_clear(list);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name F-Curve Data Copy
 * \{ */

FCurve *BKE_fcurve_copy(const FCurve *fcu)
{
  FCurve *fcu_d;

  /* Sanity check. */
  if (fcu == NULL) {
    return NULL;
  }

  /* Make a copy. */
  fcu_d = MEM_dupallocN(fcu);

  fcu_d->next = fcu_d->prev = NULL;
  fcu_d->grp = NULL;

  /* Copy curve data. */
  fcu_d->bezt = MEM_dupallocN(fcu_d->bezt);
  fcu_d->fpt = MEM_dupallocN(fcu_d->fpt);

  /* Copy rna-path. */
  fcu_d->rna_path = MEM_dupallocN(fcu_d->rna_path);

  /* Copy driver. */
  fcu_d->driver = fcurve_copy_driver(fcu_d->driver);

  /* Copy modifiers. */
  copy_fmodifiers(&fcu_d->modifiers, &fcu->modifiers);

  /* Return new data. */
  return fcu_d;
}

void BKE_fcurves_copy(ListBase *dst, ListBase *src)
{
  FCurve *dfcu, *sfcu;

  /* Sanity checks. */
  if (ELEM(NULL, dst, src)) {
    return;
  }

  /* Clear destination list first. */
  BLI_listbase_clear(dst);

  /* Copy one-by-one. */
  for (sfcu = src->first; sfcu; sfcu = sfcu->next) {
    dfcu = BKE_fcurve_copy(sfcu);
    BLI_addtail(dst, dfcu);
  }
}

void BKE_fcurve_foreach_id(FCurve *fcu, LibraryForeachIDData *data)
{
  ChannelDriver *driver = fcu->driver;

  if (driver != NULL) {
    LISTBASE_FOREACH (DriverVar *, dvar, &driver->variables) {
      /* only used targets */
      DRIVER_TARGETS_USED_LOOPER_BEGIN (dvar) {
        BKE_LIB_FOREACHID_PROCESS_ID(data, dtar->id, IDWALK_CB_NOP);
      }
      DRIVER_TARGETS_LOOPER_END;
    }
  }

  LISTBASE_FOREACH (FModifier *, fcm, &fcu->modifiers) {
    switch (fcm->type) {
      case FMODIFIER_TYPE_PYTHON: {
        FMod_Python *fcm_py = (FMod_Python *)fcm->data;
        BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, fcm_py->script, IDWALK_CB_NOP);

        BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
            data,
            IDP_foreach_property(fcm_py->prop,
                                 IDP_TYPE_FILTER_ID,
                                 BKE_lib_query_idpropertiesForeachIDLink_callback,
                                 data));
        break;
      }
      default:
        break;
    }
  }
}

/* ----------------- Finding F-Curves -------------------------- */

FCurve *id_data_find_fcurve(
    ID *id, void *data, StructRNA *type, const char *prop_name, int index, bool *r_driven)
{
  /* Anim vars */
  AnimData *adt = BKE_animdata_from_id(id);
  FCurve *fcu = NULL;

  /* Rna vars */
  PointerRNA ptr;
  PropertyRNA *prop;
  char *path;

  if (r_driven) {
    *r_driven = false;
  }

  /* Only use the current action ??? */
  if (ELEM(NULL, adt, adt->action)) {
    return NULL;
  }

  RNA_pointer_create(id, type, data, &ptr);
  prop = RNA_struct_find_property(&ptr, prop_name);
  if (prop == NULL) {
    return NULL;
  }

  path = RNA_path_from_ID_to_property(&ptr, prop);
  if (path == NULL) {
    return NULL;
  }

  /* Animation takes priority over drivers. */
  if (adt->action && adt->action->curves.first) {
    fcu = BKE_fcurve_find(&adt->action->curves, path, index);
  }

  /* If not animated, check if driven. */
  if (fcu == NULL && adt->drivers.first) {
    fcu = BKE_fcurve_find(&adt->drivers, path, index);
    if (fcu && r_driven) {
      *r_driven = true;
    }
    fcu = NULL;
  }

  MEM_freeN(path);

  return fcu;
}

FCurve *BKE_fcurve_find(ListBase *list, const char rna_path[], const int array_index)
{
  FCurve *fcu;

  /* Sanity checks. */
  if (ELEM(NULL, list, rna_path) || (array_index < 0)) {
    return NULL;
  }

  /* Check paths of curves, then array indices... */
  for (fcu = list->first; fcu; fcu = fcu->next) {
    /* Simple string-compare (this assumes that they have the same root...) */
    if (fcu->rna_path && STREQ(fcu->rna_path, rna_path)) {
      /* Now check indices. */
      if (fcu->array_index == array_index) {
        return fcu;
      }
    }
  }

  return NULL;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name FCurve Iteration
 * \{ */

FCurve *BKE_fcurve_iter_step(FCurve *fcu_iter, const char rna_path[])
{
  FCurve *fcu;

  /* Sanity checks. */
  if (ELEM(NULL, fcu_iter, rna_path)) {
    return NULL;
  }

  /* Check paths of curves, then array indices... */
  for (fcu = fcu_iter; fcu; fcu = fcu->next) {
    /* Simple string-compare (this assumes that they have the same root...) */
    if (fcu->rna_path && STREQ(fcu->rna_path, rna_path)) {
      return fcu;
    }
  }

  return NULL;
}

int BKE_fcurves_filter(ListBase *dst, ListBase *src, const char *dataPrefix, const char *dataName)
{
  FCurve *fcu;
  int matches = 0;

  /* Sanity checks. */
  if (ELEM(NULL, dst, src, dataPrefix, dataName)) {
    return 0;
  }
  if ((dataPrefix[0] == 0) || (dataName[0] == 0)) {
    return 0;
  }

  const size_t quotedName_size = strlen(dataName) + 1;
  char *quotedName = alloca(quotedName_size);

  /* Search each F-Curve one by one. */
  for (fcu = src->first; fcu; fcu = fcu->next) {
    /* Check if quoted string matches the path. */
    if (fcu->rna_path == NULL) {
      continue;
    }
    /* Skipping names longer than `quotedName_size` is OK since we're after an exact match. */
    if (!BLI_str_quoted_substr(fcu->rna_path, dataPrefix, quotedName, quotedName_size)) {
      continue;
    }
    if (!STREQ(quotedName, dataName)) {
      continue;
    }

    /* Check if the quoted name matches the required name. */
    LinkData *ld = MEM_callocN(sizeof(LinkData), __func__);

    ld->data = fcu;
    BLI_addtail(dst, ld);

    matches++;
  }
  /* Return the number of matches. */
  return matches;
}

FCurve *BKE_fcurve_find_by_rna(PointerRNA *ptr,
                               PropertyRNA *prop,
                               int rnaindex,
                               AnimData **r_adt,
                               bAction **r_action,
                               bool *r_driven,
                               bool *r_special)
{
  return BKE_fcurve_find_by_rna_context_ui(
      NULL, ptr, prop, rnaindex, r_adt, r_action, r_driven, r_special);
}

FCurve *BKE_fcurve_find_by_rna_context_ui(bContext *C,
                                          PointerRNA *ptr,
                                          PropertyRNA *prop,
                                          int rnaindex,
                                          AnimData **r_animdata,
                                          bAction **r_action,
                                          bool *r_driven,
                                          bool *r_special)
{
  FCurve *fcu = NULL;
  PointerRNA tptr = *ptr;

  *r_driven = false;
  *r_special = false;

  if (r_animdata) {
    *r_animdata = NULL;
  }
  if (r_action) {
    *r_action = NULL;
  }

  /* Special case for NLA Control Curves... */
  if (BKE_nlastrip_has_curves_for_property(ptr, prop)) {
    NlaStrip *strip = ptr->data;

    /* Set the special flag, since it cannot be a normal action/driver
     * if we've been told to start looking here...
     */
    *r_special = true;

    /* The F-Curve either exists or it doesn't here... */
    fcu = BKE_fcurve_find(&strip->fcurves, RNA_property_identifier(prop), rnaindex);
    return fcu;
  }

  /* There must be some RNA-pointer + property combo. */
  if (prop && tptr.owner_id && RNA_property_animateable(&tptr, prop)) {
    AnimData *adt = BKE_animdata_from_id(tptr.owner_id);
    int step = (
        /* Always 1 in case we have no context (can't check in 'ancestors' of given RNA ptr). */
        C ? 2 : 1);
    char *path = NULL;

    if (!adt && C) {
      path = RNA_path_from_ID_to_property(&tptr, prop);
      adt = BKE_animdata_from_id(tptr.owner_id);
      step--;
    }

    /* Standard F-Curve - Animation (Action) or Drivers. */
    while (adt && step--) {
      if ((adt->action == NULL || adt->action->curves.first == NULL) &&
          (adt->drivers.first == NULL)) {
        continue;
      }

      /* XXX This function call can become a performance bottleneck. */
      if (step) {
        path = RNA_path_from_ID_to_property(&tptr, prop);
      }
      if (path == NULL) {
        continue;
      }

      /* XXX: The logic here is duplicated with a function up above. */
      /* animation takes priority over drivers. */
      if (adt->action && adt->action->curves.first) {
        fcu = BKE_fcurve_find(&adt->action->curves, path, rnaindex);

        if (fcu && r_action) {
          *r_action = adt->action;
        }
      }

      /* If not animated, check if driven. */
      if (!fcu && (adt->drivers.first)) {
        fcu = BKE_fcurve_find(&adt->drivers, path, rnaindex);

        if (fcu) {
          if (r_animdata) {
            *r_animdata = adt;
          }
          *r_driven = true;
        }
      }

      if (fcu && r_action) {
        if (r_animdata) {
          *r_animdata = adt;
        }
        *r_action = adt->action;
        break;
      }

      if (step) {
        char *tpath = path ? path : RNA_path_from_ID_to_property(&tptr, prop);
        if (tpath && tpath != path) {
          MEM_freeN(path);
          path = tpath;
          adt = BKE_animdata_from_id(tptr.owner_id);
        }
        else {
          adt = NULL;
        }
      }
    }
    MEM_SAFE_FREE(path);
  }

  return fcu;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Finding Keyframes/Extents
 * \{ */

/* Binary search algorithm for finding where to insert BezTriple,
 * with optional argument for precision required.
 * Returns the index to insert at (data already at that index will be offset if replace is 0)
 */
static int BKE_fcurve_bezt_binarysearch_index_ex(const BezTriple array[],
                                                 const float frame,
                                                 const int arraylen,
                                                 const float threshold,
                                                 bool *r_replace)
{
  int start = 0, end = arraylen;
  int loopbreaker = 0, maxloop = arraylen * 2;

  /* Initialize replace-flag first. */
  *r_replace = false;

  /* Sneaky optimizations (don't go through searching process if...):
   * - Keyframe to be added is to be added out of current bounds.
   * - Keyframe to be added would replace one of the existing ones on bounds.
   */
  if ((arraylen <= 0) || (array == NULL)) {
    CLOG_WARN(&LOG, "encountered invalid array");
    return 0;
  }

  /* Check whether to add before/after/on. */
  float framenum;

  /* 'First' Keyframe (when only one keyframe, this case is used) */
  framenum = array[0].vec[1][0];
  if (IS_EQT(frame, framenum, threshold)) {
    *r_replace = true;
    return 0;
  }
  if (frame < framenum) {
    return 0;
  }

  /* 'Last' Keyframe */
  framenum = array[(arraylen - 1)].vec[1][0];
  if (IS_EQT(frame, framenum, threshold)) {
    *r_replace = true;
    return (arraylen - 1);
  }
  if (frame > framenum) {
    return arraylen;
  }

  /* Most of the time, this loop is just to find where to put it
   * 'loopbreaker' is just here to prevent infinite loops.
   */
  for (loopbreaker = 0; (start <= end) && (loopbreaker < maxloop); loopbreaker++) {
    /* Compute and get midpoint. */

    /* We calculate the midpoint this way to avoid int overflows... */
    int mid = start + ((end - start) / 2);

    float midfra = array[mid].vec[1][0];

    /* Check if exactly equal to midpoint. */
    if (IS_EQT(frame, midfra, threshold)) {
      *r_replace = true;
      return mid;
    }

    /* Repeat in upper/lower half. */
    if (frame > midfra) {
      start = mid + 1;
    }
    else if (frame < midfra) {
      end = mid - 1;
    }
  }

  /* Print error if loop-limit exceeded. */
  if (loopbreaker == (maxloop - 1)) {
    CLOG_ERROR(&LOG, "search taking too long");

    /* Include debug info. */
    CLOG_ERROR(&LOG,
               "\tround = %d: start = %d, end = %d, arraylen = %d",
               loopbreaker,
               start,
               end,
               arraylen);
  }

  /* Not found, so return where to place it. */
  return start;
}

int BKE_fcurve_bezt_binarysearch_index(const BezTriple array[],
                                       const float frame,
                                       const int arraylen,
                                       bool *r_replace)
{
  /* This is just a wrapper which uses the default threshold. */
  return BKE_fcurve_bezt_binarysearch_index_ex(
      array, frame, arraylen, BEZT_BINARYSEARCH_THRESH, r_replace);
}

/* ...................................... */

/* Helper for calc_fcurve_* functions -> find first and last BezTriple to be used. */
static short get_fcurve_end_keyframes(FCurve *fcu,
                                      BezTriple **first,
                                      BezTriple **last,
                                      const bool do_sel_only)
{
  bool found = false;

  /* Init outputs. */
  *first = NULL;
  *last = NULL;

  /* Sanity checks. */
  if (fcu->bezt == NULL) {
    return found;
  }

  /* Only include selected items? */
  if (do_sel_only) {
    BezTriple *bezt;

    /* Find first selected. */
    bezt = fcu->bezt;
    for (int i = 0; i < fcu->totvert; bezt++, i++) {
      if (BEZT_ISSEL_ANY(bezt)) {
        *first = bezt;
        found = true;
        break;
      }
    }

    /* Find last selected. */
    bezt = ARRAY_LAST_ITEM(fcu->bezt, BezTriple, fcu->totvert);
    for (int i = 0; i < fcu->totvert; bezt--, i++) {
      if (BEZT_ISSEL_ANY(bezt)) {
        *last = bezt;
        found = true;
        break;
      }
    }
  }
  else {
    /* Use the whole array. */
    *first = fcu->bezt;
    *last = ARRAY_LAST_ITEM(fcu->bezt, BezTriple, fcu->totvert);
    found = true;
  }

  return found;
}

bool BKE_fcurve_calc_bounds(FCurve *fcu,
                            float *xmin,
                            float *xmax,
                            float *ymin,
                            float *ymax,
                            const bool do_sel_only,
                            const bool include_handles)
{
  float xminv = 999999999.0f, xmaxv = -999999999.0f;
  float yminv = 999999999.0f, ymaxv = -999999999.0f;
  bool foundvert = false;

  if (fcu->totvert) {
    if (fcu->bezt) {
      BezTriple *bezt_first = NULL, *bezt_last = NULL;

      if (xmin || xmax) {
        /* Get endpoint keyframes. */
        foundvert = get_fcurve_end_keyframes(fcu, &bezt_first, &bezt_last, do_sel_only);

        if (bezt_first) {
          BLI_assert(bezt_last != NULL);

          if (include_handles) {
            xminv = min_fff(xminv, bezt_first->vec[0][0], bezt_first->vec[1][0]);
            xmaxv = max_fff(xmaxv, bezt_last->vec[1][0], bezt_last->vec[2][0]);
          }
          else {
            xminv = min_ff(xminv, bezt_first->vec[1][0]);
            xmaxv = max_ff(xmaxv, bezt_last->vec[1][0]);
          }
        }
      }

      /* Only loop over keyframes to find extents for values if needed. */
      if (ymin || ymax) {
        BezTriple *bezt, *prevbezt = NULL;

        int i;
        for (bezt = fcu->bezt, i = 0; i < fcu->totvert; prevbezt = bezt, bezt++, i++) {
          if ((do_sel_only == false) || BEZT_ISSEL_ANY(bezt)) {
            /* Keyframe itself. */
            yminv = min_ff(yminv, bezt->vec[1][1]);
            ymaxv = max_ff(ymaxv, bezt->vec[1][1]);

            if (include_handles) {
              /* Left handle - only if applicable.
               * NOTE: for the very first keyframe,
               * the left handle actually has no bearings on anything. */
              if (prevbezt && (prevbezt->ipo == BEZT_IPO_BEZ)) {
                yminv = min_ff(yminv, bezt->vec[0][1]);
                ymaxv = max_ff(ymaxv, bezt->vec[0][1]);
              }

              /* Right handle - only if applicable. */
              if (bezt->ipo == BEZT_IPO_BEZ) {
                yminv = min_ff(yminv, bezt->vec[2][1]);
                ymaxv = max_ff(ymaxv, bezt->vec[2][1]);
              }
            }

            foundvert = true;
          }
        }
      }
    }
    else if (fcu->fpt) {
      /* Frame range can be directly calculated from end verts. */
      if (xmin || xmax) {
        xminv = min_ff(xminv, fcu->fpt[0].vec[0]);
        xmaxv = max_ff(xmaxv, fcu->fpt[fcu->totvert - 1].vec[0]);
      }

      /* Only loop over keyframes to find extents for values if needed. */
      if (ymin || ymax) {
        FPoint *fpt;
        int i;

        for (fpt = fcu->fpt, i = 0; i < fcu->totvert; fpt++, i++) {
          if (fpt->vec[1] < yminv) {
            yminv = fpt->vec[1];
          }
          if (fpt->vec[1] > ymaxv) {
            ymaxv = fpt->vec[1];
          }

          foundvert = true;
        }
      }
    }
  }

  if (foundvert) {
    if (xmin) {
      *xmin = xminv;
    }
    if (xmax) {
      *xmax = xmaxv;
    }

    if (ymin) {
      *ymin = yminv;
    }
    if (ymax) {
      *ymax = ymaxv;
    }
  }
  else {
    if (G.debug & G_DEBUG) {
      printf("F-Curve calc bounds didn't find anything, so assuming minimum bounds of 1.0\n");
    }

    if (xmin) {
      *xmin = 0.0f;
    }
    if (xmax) {
      *xmax = 1.0f;
    }

    if (ymin) {
      *ymin = 0.0f;
    }
    if (ymax) {
      *ymax = 1.0f;
    }
  }

  return foundvert;
}

bool BKE_fcurve_calc_range(
    FCurve *fcu, float *start, float *end, const bool do_sel_only, const bool do_min_length)
{
  float min = 999999999.0f, max = -999999999.0f;
  bool foundvert = false;

  if (fcu->totvert) {
    if (fcu->bezt) {
      BezTriple *bezt_first = NULL, *bezt_last = NULL;

      /* Get endpoint keyframes. */
      get_fcurve_end_keyframes(fcu, &bezt_first, &bezt_last, do_sel_only);

      if (bezt_first) {
        BLI_assert(bezt_last != NULL);

        min = min_ff(min, bezt_first->vec[1][0]);
        max = max_ff(max, bezt_last->vec[1][0]);

        foundvert = true;
      }
    }
    else if (fcu->fpt) {
      min = min_ff(min, fcu->fpt[0].vec[0]);
      max = max_ff(max, fcu->fpt[fcu->totvert - 1].vec[0]);

      foundvert = true;
    }
  }

  if (foundvert == false) {
    min = max = 0.0f;
  }

  if (do_min_length) {
    /* Minimum length is 1 frame. */
    if (min == max) {
      max += 1.0f;
    }
  }

  *start = min;
  *end = max;

  return foundvert;
}

float *BKE_fcurves_calc_keyed_frames_ex(FCurve **fcurve_array,
                                        int fcurve_array_len,
                                        const float interval,
                                        int *r_frames_len)
{
  /* Use `1e-3f` as the smallest possible value since these are converted to integers
   * and we can be sure `MAXFRAME / 1e-3f < INT_MAX` as it's around half the size. */
  const double interval_db = max_ff(interval, 1e-3f);
  GSet *frames_unique = BLI_gset_int_new(__func__);
  for (int fcurve_index = 0; fcurve_index < fcurve_array_len; fcurve_index++) {
    const FCurve *fcu = fcurve_array[fcurve_index];
    for (int i = 0; i < fcu->totvert; i++) {
      const BezTriple *bezt = &fcu->bezt[i];
      const double value = round((double)bezt->vec[1][0] / interval_db);
      BLI_assert(value > INT_MIN && value < INT_MAX);
      BLI_gset_add(frames_unique, POINTER_FROM_INT((int)value));
    }
  }

  const size_t frames_len = BLI_gset_len(frames_unique);
  float *frames = MEM_mallocN(sizeof(*frames) * frames_len, __func__);

  GSetIterator gs_iter;
  int i = 0;
  GSET_ITER_INDEX (gs_iter, frames_unique, i) {
    const int value = POINTER_AS_INT(BLI_gsetIterator_getKey(&gs_iter));
    frames[i] = (double)value * interval_db;
  }
  BLI_gset_free(frames_unique, NULL);

  qsort(frames, frames_len, sizeof(*frames), BLI_sortutil_cmp_float);
  *r_frames_len = frames_len;
  return frames;
}

float *BKE_fcurves_calc_keyed_frames(FCurve **fcurve_array,
                                     int fcurve_array_len,
                                     int *r_frames_len)
{
  return BKE_fcurves_calc_keyed_frames_ex(fcurve_array, fcurve_array_len, 1.0f, r_frames_len);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Active Keyframe
 * \{ */

void BKE_fcurve_active_keyframe_set(FCurve *fcu, const BezTriple *active_bezt)
{
  if (active_bezt == NULL) {
    fcu->active_keyframe_index = FCURVE_ACTIVE_KEYFRAME_NONE;
    return;
  }

  /* Gracefully handle out-of-bounds pointers. Ideally this would do a BLI_assert() as well, but
   * then the unit tests would break in debug mode. */
  ptrdiff_t offset = active_bezt - fcu->bezt;
  if (offset < 0 || offset >= fcu->totvert) {
    fcu->active_keyframe_index = FCURVE_ACTIVE_KEYFRAME_NONE;
    return;
  }

  /* The active keyframe should always be selected. */
  BLI_assert_msg(BEZT_ISSEL_ANY(active_bezt), "active keyframe must be selected");

  fcu->active_keyframe_index = (int)offset;
}

int BKE_fcurve_active_keyframe_index(const FCurve *fcu)
{
  const int active_keyframe_index = fcu->active_keyframe_index;

  /* Array access boundary checks. */
  if ((fcu->bezt == NULL) || (active_keyframe_index >= fcu->totvert) ||
      (active_keyframe_index < 0)) {
    return FCURVE_ACTIVE_KEYFRAME_NONE;
  }

  const BezTriple *active_bezt = &fcu->bezt[active_keyframe_index];
  if (((active_bezt->f1 | active_bezt->f2 | active_bezt->f3) & SELECT) == 0) {
    /* The active keyframe should always be selected. If it's not selected, it can't be active. */
    return FCURVE_ACTIVE_KEYFRAME_NONE;
  }

  return active_keyframe_index;
}

/** \} */

void BKE_fcurve_keyframe_move_value_with_handles(struct BezTriple *keyframe, const float new_value)
{
  const float value_delta = new_value - keyframe->vec[1][1];
  keyframe->vec[0][1] += value_delta;
  keyframe->vec[1][1] = new_value;
  keyframe->vec[2][1] += value_delta;
}

/* -------------------------------------------------------------------- */
/** \name Status Checks
 * \{ */

bool BKE_fcurve_are_keyframes_usable(FCurve *fcu)
{
  /* F-Curve must exist. */
  if (fcu == NULL) {
    return false;
  }

  /* F-Curve must not have samples - samples are mutually exclusive of keyframes. */
  if (fcu->fpt) {
    return false;
  }

  /* If it has modifiers, none of these should "drastically" alter the curve. */
  if (fcu->modifiers.first) {
    FModifier *fcm;

    /* Check modifiers from last to first, as last will be more influential. */
    /* TODO: optionally, only check modifier if it is the active one... (Joshua Leung 2010) */
    for (fcm = fcu->modifiers.last; fcm; fcm = fcm->prev) {
      /* Ignore if muted/disabled. */
      if (fcm->flag & (FMODIFIER_FLAG_DISABLED | FMODIFIER_FLAG_MUTED)) {
        continue;
      }

      /* Type checks. */
      switch (fcm->type) {
        /* Clearly harmless - do nothing. */
        case FMODIFIER_TYPE_CYCLES:
        case FMODIFIER_TYPE_STEPPED:
        case FMODIFIER_TYPE_NOISE:
          break;

        /* Sometimes harmful - depending on whether they're "additive" or not. */
        case FMODIFIER_TYPE_GENERATOR: {
          FMod_Generator *data = (FMod_Generator *)fcm->data;

          if ((data->flag & FCM_GENERATOR_ADDITIVE) == 0) {
            return false;
          }
          break;
        }
        case FMODIFIER_TYPE_FN_GENERATOR: {
          FMod_FunctionGenerator *data = (FMod_FunctionGenerator *)fcm->data;

          if ((data->flag & FCM_GENERATOR_ADDITIVE) == 0) {
            return false;
          }
          break;
        }
        /* Always harmful - cannot allow. */
        default:
          return false;
      }
    }
  }

  /* Keyframes are usable. */
  return true;
}
