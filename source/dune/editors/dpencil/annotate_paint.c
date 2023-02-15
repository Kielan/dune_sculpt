#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_math_geom.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "PIL_time.h"

#include "BKE_callbacks.h"
#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_layer.h"
#include "BKE_main.h"
#include "BKE_report.h"
#include "BKE_screen.h"
#include "BKE_tracking.h"

#include "DNA_gpencil_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_windowmanager_types.h"

#include "UI_view2d.h"

#include "ED_clip.h"
#include "ED_gpencil.h"
#include "ED_screen.h"
#include "ED_view3d.h"

#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_state.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_prototypes.h"

#include "WM_api.h"
#include "WM_types.h"

#include "DEG_depsgraph.h"

#include "gpencil_intern.h"

/* ******************************************* */
/* 'Globals' and Defines */

#define DEPTH_INVALID 1.0f

/* values for tGPsdata->status */
typedef enum eGPencil_PaintStatus {
  GP_STATUS_IDLING = 0, /* stroke isn't in progress yet */
  GP_STATUS_PAINTING,   /* a stroke is in progress */
  GP_STATUS_ERROR,      /* something wasn't correctly set up */
  GP_STATUS_DONE,       /* painting done */
  GP_STATUS_CAPTURE     /* capture event, but cancel */
} eGPencil_PaintStatus;

/* Return flags for adding points to stroke buffer */
typedef enum eGP_StrokeAdd_Result {
  GP_STROKEADD_INVALID = -2,  /* error occurred - insufficient info to do so */
  GP_STROKEADD_OVERFLOW = -1, /* error occurred - cannot fit any more points */
  GP_STROKEADD_NORMAL,        /* point was successfully added */
  GP_STROKEADD_FULL,          /* cannot add any more points to buffer */
} eGP_StrokeAdd_Result;

/* Runtime flags */
typedef enum eGPencil_PaintFlags {
  GP_PAINTFLAG_FIRSTRUN = (1 << 0), /* operator just started */
  GP_PAINTFLAG_STROKEADDED = (1 << 1),
  GP_PAINTFLAG_V3D_ERASER_DEPTH = (1 << 2),
  GP_PAINTFLAG_SELECTMASK = (1 << 3),
  /* Flags used to indicate if stabilization is being used. */
  GP_PAINTFLAG_USE_STABILIZER = (1 << 7),
  GP_PAINTFLAG_USE_STABILIZER_TEMP = (1 << 8),
} eGPencil_PaintFlags;

/* Temporary 'Stroke' Operation data
 *   "p" = op->customdata
 */
typedef struct tGPsdata {
  Main *bmain;
  /** current scene from context. */
  Scene *scene;
  struct Depsgraph *depsgraph;

  /** window where painting originated. */
  wmWindow *win;
  /** area where painting originated. */
  ScrArea *area;
  /** region where painting originated. */
  ARegion *region;
  /** needed for GP_STROKE_2DSPACE. */
  View2D *v2d;
  /** For operations that require occlusion testing. */
  ViewDepths *depths;
  /** for using the camera rect within the 3d view. */
  rctf *subrect;
  rctf subrect_data;

  /** settings to pass to gp_points_to_xy(). */
  GP_SpaceConversion gsc;

  /** pointer to owner of gp-datablock. */
  PointerRNA ownerPtr;
  /** gp-datablock layer comes from. */
  bGPdata *gpd;
  /** layer we're working on. */
  bGPDlayer *gpl;
  /** frame we're working on. */
  bGPDframe *gpf;

  /** projection-mode flags (toolsettings - eGPencil_Placement_Flags) */
  char *align_flag;

  /** current status of painting. */
  eGPencil_PaintStatus status;
  /** mode for painting. */
  eGPencil_PaintModes paintmode;
  /** flags that can get set during runtime (eGPencil_PaintFlags) */
  eGPencil_PaintFlags flags;

  /** radius of influence for eraser. */
  short radius;

  /* Stabilizer. */
  float stabilizer_factor;
  char stabilizer_radius;
  void *stabilizer_cursor;

  /** current mouse-position. */
  float mval[2];
  /** previous recorded mouse-position. */
  float mvalo[2];

  /** current stylus pressure. */
  float pressure;
  /** previous stylus pressure. */
  float opressure;

  /* These need to be doubles, as (at least under unix) they are in seconds since epoch,
   * float (and its 7 digits precision) is definitively not enough here!
   * double, with its 15 digits precision,
   * ensures us millisecond precision for a few centuries at least.
   */
  /** Used when converting to path. */
  double inittime;
  /** Used when converting to path. */
  double curtime;
  /** Used when converting to path. */
  double ocurtime;

  /** Inverted transformation matrix applying when converting coords from screen-space
   * to region space. */
  float imat[4][4];
  float mat[4][4];

  /** custom color - hack for enforcing a particular color for track/mask editing. */
  float custom_color[4];

  /** radial cursor data for drawing eraser. */
  void *erasercursor;

  /** 1: line horizontal, 2: line vertical, other: not defined, second element position. */
  short straight[2];

  /** key used for invoking the operator. */
  short keymodifier;
} tGPsdata;

/* ------ */

/* Macros for accessing sensitivity thresholds... */
/* minimum number of pixels mouse should move before new point created */
#define MIN_MANHATTAN_PX (U.gp_manhattandist)
/* minimum length of new segment before new point can be added */
#define MIN_EUCLIDEAN_PX (U.gp_euclideandist)

static bool annotation_stroke_added_check(tGPsdata *p)
{
  return (p->gpf && p->gpf->strokes.last && p->flags & GP_PAINTFLAG_STROKEADDED);
}

static void annotation_stroke_added_enable(tGPsdata *p)
{
  BLI_assert(p->gpf->strokes.last != NULL);
  p->flags |= GP_PAINTFLAG_STROKEADDED;
}

/* ------ */
/* Forward defines for some functions... */

static void annotation_session_validatebuffer(tGPsdata *p);

/* ******************************************* */
/* Context Wrangling... */

/* check if context is suitable for drawing */
static bool annotation_draw_poll(bContext *C)
{
  if (ED_operator_regionactive(C)) {
    /* check if current context can support GPencil data */
    if (ED_annotation_data_get_pointers(C, NULL) != NULL) {
      /* check if Grease Pencil isn't already running */
      if (ED_gpencil_session_active() == 0) {
        return true;
      }
      CTX_wm_operator_poll_msg_set(C, "Annotation operator is already active");
    }
    else {
      CTX_wm_operator_poll_msg_set(C, "Failed to find Annotation data to draw into");
    }
  }
  else {
    CTX_wm_operator_poll_msg_set(C, "Active region not set");
  }

  return false;
}

/* check if projecting strokes into 3d-geometry in the 3D-View */
static bool annotation_project_check(tGPsdata *p)
{
  bGPdata *gpd = p->gpd;
  return ((gpd->runtime.sbuffer_sflag & GP_STROKE_3DSPACE) &&
          (*p->align_flag & (GP_PROJECT_DEPTH_VIEW | GP_PROJECT_DEPTH_STROKE)));
}

/* ******************************************* */
/* Calculations/Conversions */

/* Utilities --------------------------------- */

/* get the reference point for stroke-point conversions */
static void annotation_get_3d_reference(tGPsdata *p, float vec[3])
{
  const float *fp = p->scene->cursor.location;

  /* use 3D-cursor */
  copy_v3_v3(vec, fp);
}

/* Stroke Editing ---------------------------- */

/* check if the current mouse position is suitable for adding a new point */
static bool annotation_stroke_filtermval(tGPsdata *p, const float mval[2], const float pmval[2])
{
  int dx = (int)fabsf(mval[0] - pmval[0]);
  int dy = (int)fabsf(mval[1] - pmval[1]);

  /* if buffer is empty, just let this go through (i.e. so that dots will work) */
  if (p->gpd->runtime.sbuffer_used == 0) {
    return true;
  }

  /* check if mouse moved at least certain distance on both axes (best case)
   * - aims to eliminate some jitter-noise from input when trying to draw straight lines freehand
   */

  /* If lazy mouse, check minimum distance. */
  if (p->flags & GP_PAINTFLAG_USE_STABILIZER_TEMP) {
    if ((dx * dx + dy * dy) > (p->stabilizer_radius * p->stabilizer_radius)) {
      return true;
    }

    /* If the mouse is moving within the radius of the last move,
     * don't update the mouse position. This allows sharp turns. */
    copy_v2_v2(p->mval, p->mvalo);
    return false;
  }

  if ((dx > MIN_MANHATTAN_PX) && (dy > MIN_MANHATTAN_PX)) {
    return true;
  }

  /* Check if the distance since the last point is significant enough:
   * - Prevents points being added too densely
   * - Distance here doesn't use sqrt to prevent slowness.
   *   We should still be safe from overflows though.
   */
  if ((dx * dx + dy * dy) > MIN_EUCLIDEAN_PX * MIN_EUCLIDEAN_PX) {
    return true;
  }

  /* mouse 'didn't move' */
  return false;
}

/* convert screen-coordinates to buffer-coordinates */
static void annotation_stroke_convertcoords(tGPsdata *p,
                                            const float mval[2],
                                            float out[3],
                                            float *depth)
{
  bGPdata *gpd = p->gpd;
  if (depth && (*depth == DEPTH_INVALID)) {
    depth = NULL;
  }

  /* in 3d-space - pt->x/y/z are 3 side-by-side floats */
  if (gpd->runtime.sbuffer_sflag & GP_STROKE_3DSPACE) {
    int mval_i[2];
    round_v2i_v2fl(mval_i, mval);
    if (annotation_project_check(p) &&
        (ED_view3d_autodist_simple(p->region, mval_i, out, 0, depth))) {
      /* projecting onto 3D-Geometry
       * - nothing more needs to be done here, since view_autodist_simple() has already done it
       */
    }
    else {
      float mval_prj[2];
      float rvec[3];

      /* Current method just converts each point in screen-coordinates to
       * 3D-coordinates using the 3D-cursor as reference. In general, this
       * works OK, but it could of course be improved.
       *
       * TODO:
       * - investigate using nearest point(s) on a previous stroke as
       *   reference point instead or as offset, for easier stroke matching
       */

      annotation_get_3d_reference(p, rvec);
      const float zfac = ED_view3d_calc_zfac(p->region->regiondata, rvec);

      if (ED_view3d_project_float_global(p->region, rvec, mval_prj, V3D_PROJ_TEST_NOP) ==
          V3D_PROJ_RET_OK) {
        float dvec[3];
        float xy_delta[2];
        sub_v2_v2v2(xy_delta, mval_prj, mval);
        ED_view3d_win_to_delta(p->region, xy_delta, zfac, dvec);
        sub_v3_v3v3(out, rvec, dvec);
      }
      else {
        zero_v3(out);
      }
    }
  }

  /* 2d - on 'canvas' (assume that p->v2d is set) */
  else if ((gpd->runtime.sbuffer_sflag & GP_STROKE_2DSPACE) && (p->v2d)) {
    UI_view2d_region_to_view(p->v2d, mval[0], mval[1], &out[0], &out[1]);
    mul_v3_m4v3(out, p->imat, out);
  }

  /* 2d - relative to screen (viewport area) */
  else {
    if (p->subrect == NULL) { /* normal 3D view */
      out[0] = (float)(mval[0]) / (float)(p->region->winx) * 100;
      out[1] = (float)(mval[1]) / (float)(p->region->winy) * 100;
    }
    else { /* camera view, use subrect */
      out[0] = ((mval[0] - p->subrect->xmin) / BLI_rctf_size_x(p->subrect)) * 100;
      out[1] = ((mval[1] - p->subrect->ymin) / BLI_rctf_size_y(p->subrect)) * 100;
    }
  }
}

/**
 * Apply smooth to buffer while drawing
 * to smooth point C, use 2 before (A, B) and current point (D):
 *
 * `A----B-----C------D`
 *
 * \param p: Temp data
 * \param inf: Influence factor
 * \param idx: Index of the last point (need minimum 3 points in the array)
 */
static void annotation_smooth_buffer(tGPsdata *p, float inf, int idx)
{
  bGPdata *gpd = p->gpd;
  short num_points = gpd->runtime.sbuffer_used;

  /* Do nothing if not enough points to smooth out */
  if ((num_points < 3) || (idx < 3) || (inf == 0.0f)) {
    return;
  }

  tGPspoint *points = (tGPspoint *)gpd->runtime.sbuffer;
  float steps = 4.0f;
  if (idx < 4) {
    steps--;
  }

  tGPspoint *pta = idx >= 4 ? &points[idx - 4] : NULL;
  tGPspoint *ptb = idx >= 3 ? &points[idx - 3] : NULL;
  tGPspoint *ptc = idx >= 2 ? &points[idx - 2] : NULL;
  tGPspoint *ptd = &points[idx - 1];

  float sco[2] = {0.0f};
  float a[2], b[2], c[2], d[2];
  const float average_fac = 1.0f / steps;

  /* Compute smoothed coordinate by taking the ones nearby */
  if (pta) {
    copy_v2_v2(a, pta->m_xy);
    madd_v2_v2fl(sco, a, average_fac);
  }
  if (ptb) {
    copy_v2_v2(b, ptb->m_xy);
    madd_v2_v2fl(sco, b, average_fac);
  }
  if (ptc) {
    copy_v2_v2(c, ptc->m_xy);
    madd_v2_v2fl(sco, c, average_fac);
  }
  if (ptd) {
    copy_v2_v2(d, ptd->m_xy);
    madd_v2_v2fl(sco, d, average_fac);
  }

  /* Based on influence factor, blend between original and optimal smoothed coordinate */
  interp_v2_v2v2(c, c, sco, inf);
  copy_v2_v2(ptc->m_xy, c);
}
