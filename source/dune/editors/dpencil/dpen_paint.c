#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_hash.h"
#include "BLI_math.h"
#include "BLI_math_geom.h"
#include "BLI_rand.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "PIL_time.h"

#include "DNA_brush_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_windowmanager_types.h"

#include "BKE_brush.h"
#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_deform.h"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_curve.h"
#include "BKE_gpencil_geom.h"
#include "BKE_gpencil_update_cache.h"
#include "BKE_layer.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_paint.h"
#include "BKE_report.h"
#include "BKE_screen.h"
#include "BKE_tracking.h"

#include "UI_view2d.h"

#include "ED_clip.h"
#include "ED_gpencil.h"
#include "ED_keyframing.h"
#include "ED_object.h"
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
#include "DEG_depsgraph_query.h"

#include "gpencil_intern.h"

/* ******************************************* */
/* 'Globals' and Defines */

/* values for tGPsdata->status */
typedef enum eGPencil_PaintStatus {
  GP_STATUS_IDLING = 0, /* stroke isn't in progress yet */
  GP_STATUS_PAINTING,   /* a stroke is in progress */
  GP_STATUS_ERROR,      /* something wasn't correctly set up */
  GP_STATUS_DONE,       /* painting done */
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
  GP_PAINTFLAG_SELECTMASK = (1 << 3),
  GP_PAINTFLAG_HARD_ERASER = (1 << 4),
  GP_PAINTFLAG_STROKE_ERASER = (1 << 5),
  GP_PAINTFLAG_REQ_VECTOR = (1 << 6),
} eGPencil_PaintFlags;

/* Temporary Guide data */
typedef struct tGPguide {
  /** guide spacing */
  float spacing;
  /** half guide spacing */
  float half_spacing;
  /** origin */
  float origin[2];
  /** rotated point */
  float rot_point[2];
  /** rotated point */
  float rot_angle;
  /** initial stroke direction */
  float stroke_angle;
  /** initial origin direction */
  float origin_angle;
  /** initial origin distance */
  float origin_distance;
  /** initial line for guides */
  float unit[2];
} tGPguide;

/* Temporary 'Stroke' Operation data
 *   "p" = op->customdata
 */
typedef struct tGPsdata {
  bContext *C;

  /** main database pointer. */
  Main *bmain;
  /** current scene from context. */
  Scene *scene;
  struct Depsgraph *depsgraph;

  /** Current object. */
  Object *ob;
  /** Evaluated object. */
  Object *ob_eval;
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

  /** current mouse-position. */
  float mval[2];
  /** previous recorded mouse-position. */
  float mvalo[2];
  /** initial recorded mouse-position */
  float mvali[2];

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

  float diff_mat[4][4];

  /** custom color - hack for enforcing a particular color for track/mask editing. */
  float custom_color[4];

  /** radial cursor data for drawing eraser. */
  void *erasercursor;

  /* mat settings are only used for 3D view */
  /** current material */
  Material *material;
  /** current drawing brush */
  Brush *brush;
  /** default eraser brush */
  Brush *eraser;

  /** 1: line horizontal, 2: line vertical, other: not defined */
  short straight;
  /** lock drawing to one axis */
  int lock_axis;
  /** the stroke is no fill mode */
  bool disable_fill;

  RNG *rng;

  /** key used for invoking the operator */
  short keymodifier;
  /** shift modifier flag */
  bool shift;
  /** size in pixels for uv calculation */
  float totpixlen;
  /** Special mode for fill brush. */
  bool disable_stabilizer;
  /* guide */
  tGPguide guide;

  ReportList *reports;

  /** Random settings by stroke */
  GpRandomSettings random_settings;

} tGPsdata;

/* ------ */

#define STROKE_HORIZONTAL 1
#define STROKE_VERTICAL 2

/* Macros for accessing sensitivity thresholds... */
/* minimum number of pixels mouse should move before new point created */
#define MIN_MANHATTEN_PX (U.gp_manhattandist)
/* minimum length of new segment before new point can be added */
#define MIN_EUCLIDEAN_PX (U.gp_euclideandist)

static void gpencil_update_cache(bGPdata *gpd)
{
  if (gpd) {
    DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
    gpd->flag |= GP_DATA_CACHE_IS_DIRTY;
  }
}

/* ------ */
/* Forward defines for some functions... */

static void gpencil_session_validatebuffer(tGPsdata *p);

/* ******************************************* */
/* Context Wrangling... */

/* check if context is suitable for drawing */
static bool gpencil_draw_poll(bContext *C)
{
  if (ED_operator_regionactive(C)) {
    ScrArea *area = CTX_wm_area(C);
    /* 3D Viewport */
    if (area->spacetype != SPACE_VIEW3D) {
      return false;
    }

    /* check if Grease Pencil isn't already running */
    if (ED_gpencil_session_active() != 0) {
      CTX_wm_operator_poll_msg_set(C, "Grease Pencil operator is already active");
      return false;
    }

    /* only grease pencil object type */
    Object *ob = CTX_data_active_object(C);
    if ((ob == NULL) || (ob->type != OB_GPENCIL)) {
      return false;
    }

    bGPdata *gpd = (bGPdata *)ob->data;
    if (!GPENCIL_PAINT_MODE(gpd)) {
      return false;
    }

    ToolSettings *ts = CTX_data_scene(C)->toolsettings;
    if (!ts->gp_paint->paint.brush) {
      CTX_wm_operator_poll_msg_set(C, "Grease Pencil has no active paint tool");
      return false;
    }

    return true;
  }

  CTX_wm_operator_poll_msg_set(C, "Active region not set");
  return false;
}

/* check if projecting strokes into 3d-geometry in the 3D-View */
static bool gpencil_project_check(tGPsdata *p)
{
  bGPdata *gpd = p->gpd;
  return ((gpd->runtime.sbuffer_sflag & GP_STROKE_3DSPACE) &&
          (*p->align_flag & (GP_PROJECT_DEPTH_VIEW | GP_PROJECT_DEPTH_STROKE)));
}

/* ******************************************* */
/* Calculations/Conversions */

/* Utilities --------------------------------- */

/* get the reference point for stroke-point conversions */
static void gpencil_get_3d_reference(tGPsdata *p, float vec[3])
{
  Object *ob = NULL;
  if (p->ownerPtr.type == &RNA_Object) {
    ob = (Object *)p->ownerPtr.data;
  }
  ED_gpencil_drawing_reference_get(p->scene, ob, *p->align_flag, vec);
}

/* Stroke Editing ---------------------------- */
/* check if the current mouse position is suitable for adding a new point */
static bool gpencil_stroke_filtermval(tGPsdata *p, const float mval[2], const float mvalo[2])
{
  Brush *brush = p->brush;
  int dx = (int)fabsf(mval[0] - mvalo[0]);
  int dy = (int)fabsf(mval[1] - mvalo[1]);
  brush->gpencil_settings->flag &= ~GP_BRUSH_STABILIZE_MOUSE_TEMP;

  /* if buffer is empty, just let this go through (i.e. so that dots will work) */
  if (p->gpd->runtime.sbuffer_used == 0) {
    return true;
  }
  /* if lazy mouse, check minimum distance */
  if (GPENCIL_LAZY_MODE(brush, p->shift) && (!p->disable_stabilizer)) {
    brush->gpencil_settings->flag |= GP_BRUSH_STABILIZE_MOUSE_TEMP;
    if ((dx * dx + dy * dy) > (brush->smooth_stroke_radius * brush->smooth_stroke_radius)) {
      return true;
    }

    /* If the mouse is moving within the radius of the last move,
     * don't update the mouse position. This allows sharp turns. */
    copy_v2_v2(p->mval, p->mvalo);
    return false;
  }
  /* check if mouse moved at least certain distance on both axes (best case)
   * - aims to eliminate some jitter-noise from input when trying to draw straight lines freehand
   */
  if ((dx > MIN_MANHATTEN_PX) && (dy > MIN_MANHATTEN_PX)) {
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

/* reproject stroke to plane locked to axis in 3d cursor location */
static void gpencil_reproject_toplane(tGPsdata *p, bGPDstroke *gps)
{
  bGPdata *gpd = p->gpd;
  Object *obact = (Object *)p->ownerPtr.data;

  float origin[3];
  RegionView3D *rv3d = p->region->regiondata;

  /* verify the stroke mode is CURSOR 3d space mode */
  if ((gpd->runtime.sbuffer_sflag & GP_STROKE_3DSPACE) == 0) {
    return;
  }
  if ((*p->align_flag & GP_PROJECT_VIEWSPACE) == 0) {
    return;
  }
  if ((*p->align_flag & GP_PROJECT_DEPTH_VIEW) || (*p->align_flag & GP_PROJECT_DEPTH_STROKE)) {
    return;
  }

  /* get drawing origin */
  gpencil_get_3d_reference(p, origin);
  ED_gpencil_project_stroke_to_plane(p->scene, obact, rv3d, p->gpl, gps, origin, p->lock_axis - 1);
}

/* convert screen-coordinates to buffer-coordinates */
static void gpencil_stroke_convertcoords(tGPsdata *p,
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

    /* add small offset to keep stroke over the surface */
    if ((depth) && (gpd->zdepth_offset > 0.0f) && (*p->align_flag & GP_PROJECT_DEPTH_VIEW)) {
      *depth *= (1.0f - (gpd->zdepth_offset / 1000.0f));
    }

    int mval_i[2];
    float rmval[2];
    rmval[0] = mval[0] - 0.5f;
    rmval[1] = mval[1] - 0.5f;
    round_v2i_v2fl(mval_i, rmval);

    if (gpencil_project_check(p) &&
        (ED_view3d_autodist_simple(p->region, mval_i, out, 0, depth))) {
      /* projecting onto 3D-Geometry
       * - nothing more needs to be done here, since view_autodist_simple() has already done it
       */

      /* verify valid zdepth, if it's wrong, the default drawing mode is used
       * and the function doesn't return now */
      if ((depth == NULL) || (*depth <= 1.0f)) {
        return;
      }
    }

    float mval_prj[2];
    float rvec[3];

    /* Current method just converts each point in screen-coordinates to
     * 3D-coordinates using the 3D-cursor as reference. In general, this
     * works OK, but it could of course be improved. */

    gpencil_get_3d_reference(p, rvec);
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

/* Apply jitter to stroke point. */
static void gpencil_brush_jitter(bGPdata *gpd, tGPspoint *pt, const float amplitude)
{
  const float axis[2] = {0.0f, 1.0f};
  /* Jitter is applied perpendicular to the mouse movement vector (2D space). */
  float mvec[2];
  /* Mouse movement in ints -> floats. */
  if (gpd->runtime.sbuffer_used > 1) {
    tGPspoint *pt_prev = pt - 1;
    sub_v2_v2v2(mvec, pt->m_xy, pt_prev->m_xy);
    normalize_v2(mvec);
    /* Rotate mvec by 90 degrees... */
    float angle = angle_v2v2(mvec, axis);
    /* Reduce noise in the direction of the stroke. */
    mvec[0] *= cos(angle);
    mvec[1] *= sin(angle);

    /* Scale by displacement amount, and apply. */
    madd_v2_v2fl(pt->m_xy, mvec, amplitude * 10.0f);
  }
}

/* Apply pressure change depending of the angle of the stroke to simulate a pen with shape */
static void gpencil_brush_angle(bGPdata *gpd, Brush *brush, tGPspoint *pt, const float mval[2])
{
  float mvec[2];
  float sen = brush->gpencil_settings->draw_angle_factor; /* sensitivity */
  float fac;

  /* default angle of brush in radians */
  float angle = brush->gpencil_settings->draw_angle;
  /* angle vector of the brush with full thickness */
  const float v0[2] = {cos(angle), sin(angle)};

  /* Apply to first point (only if there are 2 points because before no data to do it ) */
  if (gpd->runtime.sbuffer_used == 1) {
    sub_v2_v2v2(mvec, mval, (pt - 1)->m_xy);
    normalize_v2(mvec);

    /* uses > 1.0f to get a smooth transition in first point */
    fac = 1.4f - fabs(dot_v2v2(v0, mvec)); /* 0.0 to 1.0 */
    (pt - 1)->pressure = (pt - 1)->pressure - (sen * fac);

    CLAMP((pt - 1)->pressure, GPENCIL_ALPHA_OPACITY_THRESH, 1.0f);
  }

  /* apply from second point */
  if (gpd->runtime.sbuffer_used >= 1) {
    sub_v2_v2v2(mvec, mval, (pt - 1)->m_xy);
    normalize_v2(mvec);

    fac = 1.0f - fabs(dot_v2v2(v0, mvec)); /* 0.0 to 1.0 */
    /* interpolate with previous point for smoother transitions */
    pt->pressure = interpf(pt->pressure - (sen * fac), (pt - 1)->pressure, 0.3f);
    CLAMP(pt->pressure, GPENCIL_ALPHA_OPACITY_THRESH, 1.0f);
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
static void gpencil_smooth_buffer(tGPsdata *p, float inf, int idx)
{
  bGPdata *gpd = p->gpd;
  GP_Sculpt_Guide *guide = &p->scene->toolsettings->gp_sculpt.guide;
  const short num_points = gpd->runtime.sbuffer_used;

  /* Do nothing if not enough points to smooth out */
  if ((num_points < 3) || (idx < 3) || (inf == 0.0f)) {
    return;
  }

  tGPspoint *points = (tGPspoint *)gpd->runtime.sbuffer;
  const float steps = (idx < 4) ? 3.0f : 4.0f;

  tGPspoint *pta = idx >= 4 ? &points[idx - 4] : NULL;
  tGPspoint *ptb = idx >= 3 ? &points[idx - 3] : NULL;
  tGPspoint *ptc = idx >= 2 ? &points[idx - 2] : NULL;
  tGPspoint *ptd = &points[idx - 1];

  float sco[2] = {0.0f};
  float a[2], b[2], c[2], d[2];
  float pressure = 0.0f;
  float strength = 0.0f;
  const float average_fac = 1.0f / steps;

  /* Compute smoothed coordinate by taking the ones nearby */
  if (pta) {
    copy_v2_v2(a, pta->m_xy);
    madd_v2_v2fl(sco, a, average_fac);
    pressure += pta->pressure * average_fac;
    strength += pta->strength * average_fac;
  }
  if (ptb) {
    copy_v2_v2(b, ptb->m_xy);
    madd_v2_v2fl(sco, b, average_fac);
    pressure += ptb->pressure * average_fac;
    strength += ptb->strength * average_fac;
  }
  if (ptc) {
    copy_v2_v2(c, ptc->m_xy);
    madd_v2_v2fl(sco, c, average_fac);
    pressure += ptc->pressure * average_fac;
    strength += ptc->strength * average_fac;
  }
  if (ptd) {
    copy_v2_v2(d, ptd->m_xy);
    madd_v2_v2fl(sco, d, average_fac);
    pressure += ptd->pressure * average_fac;
    strength += ptd->strength * average_fac;
  }

  /* Based on influence factor, blend between original and optimal smoothed coordinate but not
   * for Guide mode. */
  if (!guide->use_guide) {
    interp_v2_v2v2(c, c, sco, inf);
    copy_v2_v2(ptc->m_xy, c);
  }
  /* Interpolate pressure. */
  ptc->pressure = interpf(ptc->pressure, pressure, inf);
  /* Interpolate strength. */
  ptc->strength = interpf(ptc->strength, strength, inf);
}

/* Helper: Apply smooth to segment from Index to Index */
static void gpencil_smooth_segment(bGPdata *gpd, const float inf, int from_idx, int to_idx)
{
  const short num_points = to_idx - from_idx;
  /* Do nothing if not enough points to smooth out */
  if ((num_points < 3) || (inf == 0.0f)) {
    return;
  }

  if (from_idx <= 2) {
    return;
  }

  tGPspoint *points = (tGPspoint *)gpd->runtime.sbuffer;
  const float average_fac = 0.25f;

  for (int i = from_idx; i < to_idx + 1; i++) {

    tGPspoint *pta = i >= 3 ? &points[i - 3] : NULL;
    tGPspoint *ptb = i >= 2 ? &points[i - 2] : NULL;
    tGPspoint *ptc = i >= 1 ? &points[i - 1] : &points[i];
    tGPspoint *ptd = &points[i];

    float sco[2] = {0.0f};
    float pressure = 0.0f;
    float strength = 0.0f;

    /* Compute smoothed coordinate by taking the ones nearby */
    if (pta) {
      madd_v2_v2fl(sco, pta->m_xy, average_fac);
      pressure += pta->pressure * average_fac;
      strength += pta->strength * average_fac;
    }
    else {
      madd_v2_v2fl(sco, ptc->m_xy, average_fac);
      pressure += ptc->pressure * average_fac;
      strength += ptc->strength * average_fac;
    }

    if (ptb) {
      madd_v2_v2fl(sco, ptb->m_xy, average_fac);
      pressure += ptb->pressure * average_fac;
      strength += ptb->strength * average_fac;
    }
    else {
      madd_v2_v2fl(sco, ptc->m_xy, average_fac);
      pressure += ptc->pressure * average_fac;
      strength += ptc->strength * average_fac;
    }

    madd_v2_v2fl(sco, ptc->m_xy, average_fac);
    pressure += ptc->pressure * average_fac;
    strength += ptc->strength * average_fac;

    madd_v2_v2fl(sco, ptd->m_xy, average_fac);
    pressure += ptd->pressure * average_fac;
    strength += ptd->strength * average_fac;

    /* Based on influence factor, blend between original and optimal smoothed coordinate. */
    interp_v2_v2v2(ptc->m_xy, ptc->m_xy, sco, inf);

    /* Interpolate pressure. */
    ptc->pressure = interpf(ptc->pressure, pressure, inf);
    /* Interpolate strength. */
    ptc->strength = interpf(ptc->strength, strength, inf);
  }
}

static void gpencil_apply_randomness(tGPsdata *p,
                                     BrushGpencilSettings *brush_settings,
                                     tGPspoint *pt,
                                     const bool press,
                                     const bool strength,
                                     const bool uv)
{
  bGPdata *gpd = p->gpd;
  GpRandomSettings random_settings = p->random_settings;
  float value = 0.0f;
  /* Apply randomness to pressure. */
  if ((brush_settings->draw_random_press > 0.0f) && (press)) {
    if ((brush_settings->flag2 & GP_BRUSH_USE_PRESS_AT_STROKE) == 0) {
      float rand = BLI_rng_get_float(p->rng) * 2.0f - 1.0f;
      value = 1.0 + rand * 2.0 * brush_settings->draw_random_press;
    }
    else {
      value = 1.0 + random_settings.pressure * brush_settings->draw_random_press;
    }

    /* Apply random curve. */
    if (brush_settings->flag2 & GP_BRUSH_USE_PRESSURE_RAND_PRESS) {
      value *= BKE_curvemapping_evaluateF(
          brush_settings->curve_rand_pressure, 0, random_settings.pen_press);
    }

    pt->pressure *= value;
    CLAMP(pt->pressure, 0.1f, 1.0f);
  }

  /* Apply randomness to color strength. */
  if ((brush_settings->draw_random_strength) && (strength)) {
    if ((brush_settings->flag2 & GP_BRUSH_USE_STRENGTH_AT_STROKE) == 0) {
      float rand = BLI_rng_get_float(p->rng) * 2.0f - 1.0f;
      value = 1.0 + rand * brush_settings->draw_random_strength;
    }
    else {
      value = 1.0 + random_settings.strength * brush_settings->draw_random_strength;
    }

    /* Apply random curve. */
    if (brush_settings->flag2 & GP_BRUSH_USE_STRENGTH_RAND_PRESS) {
      value *= BKE_curvemapping_evaluateF(
          brush_settings->curve_rand_pressure, 0, random_settings.pen_press);
    }

    pt->strength *= value;
    CLAMP(pt->strength, GPENCIL_STRENGTH_MIN, 1.0f);
  }

  /* Apply randomness to uv texture rotation. */
  if ((brush_settings->uv_random > 0.0f) && (uv)) {
    if ((brush_settings->flag2 & GP_BRUSH_USE_UV_AT_STROKE) == 0) {
      float rand = BLI_hash_int_01(BLI_hash_int_2d((int)pt->m_xy[0], gpd->runtime.sbuffer_used)) *
                       2.0f -
                   1.0f;
      value = rand * M_PI_2 * brush_settings->uv_random;
    }
    else {
      value = random_settings.uv * M_PI_2 * brush_settings->uv_random;
    }

    /* Apply random curve. */
    if (brush_settings->flag2 & GP_BRUSH_USE_UV_RAND_PRESS) {
      value *= BKE_curvemapping_evaluateF(
          brush_settings->curve_rand_uv, 0, random_settings.pen_press);
    }

    pt->uv_rot += value;
    CLAMP(pt->uv_rot, -M_PI_2, M_PI_2);
  }
}

/* add current stroke-point to buffer (returns whether point was successfully added) */
static short gpencil_stroke_addpoint(tGPsdata *p,
                                     const float mval[2],
                                     float pressure,
                                     double curtime)
{
  bGPdata *gpd = p->gpd;
  Brush *brush = p->brush;
  BrushGpencilSettings *brush_settings = p->brush->gpencil_settings;
  tGPspoint *pt;
  Object *obact = (Object *)p->ownerPtr.data;
  RegionView3D *rv3d = p->region->regiondata;

  /* check painting mode */
  if (p->paintmode == GP_PAINTMODE_DRAW_STRAIGHT) {
    /* straight lines only - i.e. only store start and end point in buffer */
    if (gpd->runtime.sbuffer_used == 0) {
      /* first point in buffer (start point) */
      pt = (tGPspoint *)(gpd->runtime.sbuffer);

      /* store settings */
      copy_v2_v2(pt->m_xy, mval);
      /* T44932 - Pressure vals are unreliable, so ignore for now */
      pt->pressure = 1.0f;
      pt->strength = 1.0f;
      pt->time = (float)(curtime - p->inittime);

      /* increment buffer size */
      gpd->runtime.sbuffer_used++;
    }
    else {
      /* just reset the endpoint to the latest value
       * - assume that pointers for this are always valid...
       */
      pt = ((tGPspoint *)(gpd->runtime.sbuffer) + 1);

      /* store settings */
      copy_v2_v2(pt->m_xy, mval);
      /* T44932 - Pressure vals are unreliable, so ignore for now */
      pt->pressure = 1.0f;
      pt->strength = 1.0f;
      pt->time = (float)(curtime - p->inittime);

      /* now the buffer has 2 points (and shouldn't be allowed to get any larger) */
      gpd->runtime.sbuffer_used = 2;
    }

    /* can keep carrying on this way :) */
    return GP_STROKEADD_NORMAL;
  }

  if (p->paintmode == GP_PAINTMODE_DRAW) { /* normal drawing */
    /* check if still room in buffer or add more */
    gpd->runtime.sbuffer = ED_gpencil_sbuffer_ensure(
        gpd->runtime.sbuffer, &gpd->runtime.sbuffer_size, &gpd->runtime.sbuffer_used, false);

    /* Check the buffer was created. */
    if (gpd->runtime.sbuffer == NULL) {
      return GP_STROKEADD_INVALID;
    }

    /* get pointer to destination point */
    pt = ((tGPspoint *)(gpd->runtime.sbuffer) + gpd->runtime.sbuffer_used);

    /* store settings */
    pt->strength = brush_settings->draw_strength;
    pt->pressure = 1.0f;
    pt->uv_rot = 0.0f;
    copy_v2_v2(pt->m_xy, mval);

    /* pressure */
    if (brush_settings->flag & GP_BRUSH_USE_PRESSURE) {
      pt->pressure *= BKE_curvemapping_evaluateF(brush_settings->curve_sensitivity, 0, pressure);
    }

    /* color strength */
    if (brush_settings->flag & GP_BRUSH_USE_STRENGTH_PRESSURE) {
      pt->strength *= BKE_curvemapping_evaluateF(brush_settings->curve_strength, 0, pressure);
      CLAMP(pt->strength, MIN2(GPENCIL_STRENGTH_MIN, brush_settings->draw_strength), 1.0f);
    }

    /* Set vertex colors for buffer. */
    ED_gpencil_sbuffer_vertex_color_set(p->depsgraph,
                                        p->ob,
                                        p->scene->toolsettings,
                                        p->brush,
                                        p->material,
                                        p->random_settings.hsv,
                                        p->random_settings.pen_press);

    if (brush_settings->flag & GP_BRUSH_GROUP_RANDOM) {
      /* Apply jitter to position */
      if (brush_settings->draw_jitter > 0.0f) {
        float rand = BLI_rng_get_float(p->rng) * 2.0f - 1.0f;
        float jitpress = 1.0f;
        if (brush_settings->flag & GP_BRUSH_USE_JITTER_PRESSURE) {
          jitpress = BKE_curvemapping_evaluateF(brush_settings->curve_jitter, 0, pressure);
        }
        /* FIXME the +2 means minimum jitter is 4 which is a bit strange for UX. */
        const float exp_factor = brush_settings->draw_jitter + 2.0f;
        const float fac = rand * square_f(exp_factor) * jitpress;
        gpencil_brush_jitter(gpd, pt, fac);
      }

      /* Apply other randomness. */
      gpencil_apply_randomness(p, brush_settings, pt, true, true, true);
    }

    /* apply angle of stroke to brush size */
    if (brush_settings->draw_angle_factor != 0.0f) {
      gpencil_brush_angle(gpd, brush, pt, mval);
    }

    /* point time */
    pt->time = (float)(curtime - p->inittime);

    /* point uv (only 3d view) */
    if (gpd->runtime.sbuffer_used > 0) {
      tGPspoint *ptb = (tGPspoint *)gpd->runtime.sbuffer + gpd->runtime.sbuffer_used - 1;
      bGPDspoint spt, spt2;

      /* get origin to reproject point */
      float origin[3];
      gpencil_get_3d_reference(p, origin);
      /* reproject current */
      ED_gpencil_tpoint_to_point(p->region, origin, pt, &spt);
      ED_gpencil_project_point_to_plane(
          p->scene, obact, p->gpl, rv3d, origin, p->lock_axis - 1, &spt);

      /* reproject previous */
      ED_gpencil_tpoint_to_point(p->region, origin, ptb, &spt2);
      ED_gpencil_project_point_to_plane(
          p->scene, obact, p->gpl, rv3d, origin, p->lock_axis - 1, &spt2);
      p->totpixlen += len_v3v3(&spt.x, &spt2.x);
      pt->uv_fac = p->totpixlen;
    }
    else {
      p->totpixlen = 0.0f;
      pt->uv_fac = 0.0f;
    }

    /* increment counters */
    gpd->runtime.sbuffer_used++;

    /* Smooth while drawing previous points with a reduction factor for previous. */
    if (brush->gpencil_settings->active_smooth > 0.0f) {
      for (int s = 0; s < 3; s++) {
        gpencil_smooth_buffer(p,
                              brush->gpencil_settings->active_smooth * ((3.0f - s) / 3.0f),
                              gpd->runtime.sbuffer_used - s);
      }
    }

    /* Update evaluated data. */
    ED_gpencil_sbuffer_update_eval(gpd, p->ob_eval);

    return GP_STROKEADD_NORMAL;
  }
  /* return invalid state for now... */
  return GP_STROKEADD_INVALID;
}

static void gpencil_stroke_unselect(bGPdata *gpd, bGPDstroke *gps)
{
  gps->flag &= ~GP_STROKE_SELECT;
  BKE_gpencil_stroke_select_index_reset(gps);
  for (int i = 0; i < gps->totpoints; i++) {
    gps->points[i].flag &= ~GP_SPOINT_SELECT;
  }
  /* Update the selection from the stroke to the curve. */
  if (gps->editcurve) {
    BKE_gpencil_editcurve_stroke_sync_selection(gpd, gps, gps->editcurve);
  }
}

/* make a new stroke from the buffer data */
static void gpencil_stroke_newfrombuffer(tGPsdata *p)
{
  bGPdata *gpd = p->gpd;
  bGPDlayer *gpl = p->gpl;
  bGPDstroke *gps;
  bGPDspoint *pt;
  tGPspoint *ptc;
  MDeformVert *dvert = NULL;
  Brush *brush = p->brush;
  BrushGpencilSettings *brush_settings = brush->gpencil_settings;
  ToolSettings *ts = p->scene->toolsettings;
  Depsgraph *depsgraph = p->depsgraph;
  Object *obact = (Object *)p->ownerPtr.data;
  RegionView3D *rv3d = p->region->regiondata;
  const int def_nr = gpd->vertex_group_active_index - 1;
  const bool have_weight = (bool)BLI_findlink(&gpd->vertex_group_names, def_nr);
  const char align_flag = ts->gpencil_v3d_align;
  const bool is_depth = (bool)(align_flag & (GP_PROJECT_DEPTH_VIEW | GP_PROJECT_DEPTH_STROKE));
  const bool is_lock_axis_view = (bool)(ts->gp_sculpt.lock_axis == 0);
  const bool is_camera = is_lock_axis_view && (rv3d->persp == RV3D_CAMOB) && (!is_depth);
  int totelem;

  /* For very low pressure at the end, truncate stroke. */
  if (p->paintmode == GP_PAINTMODE_DRAW) {
    int last_i = gpd->runtime.sbuffer_used - 1;
    while (last_i > 0) {
      ptc = (tGPspoint *)gpd->runtime.sbuffer + last_i;
      if (ptc->pressure > 0.001f) {
        break;
      }
      gpd->runtime.sbuffer_used = last_i - 1;
      CLAMP_MIN(gpd->runtime.sbuffer_used, 1);

      last_i--;
    }
  }
  /* Since strokes are so fine,
   * when using their depth we need a margin otherwise they might get missed. */
  int depth_margin = (ts->gpencil_v3d_align & GP_PROJECT_DEPTH_STROKE) ? 4 : 0;

  /* get total number of points to allocate space for
   * - drawing straight-lines only requires the endpoints
   */
  if (p->paintmode == GP_PAINTMODE_DRAW_STRAIGHT) {
    totelem = (gpd->runtime.sbuffer_used >= 2) ? 2 : gpd->runtime.sbuffer_used;
  }
  else {
    totelem = gpd->runtime.sbuffer_used;
  }

  /* exit with error if no valid points from this stroke */
  if (totelem == 0) {
    return;
  }

  /* allocate memory for a new stroke */
  gps = MEM_callocN(sizeof(bGPDstroke), "gp_stroke");

  /* copy appropriate settings for stroke */
  gps->totpoints = totelem;
  gps->thickness = brush->size;
  gps->fill_opacity_fac = 1.0f;
  gps->hardeness = brush->gpencil_settings->hardeness;
  copy_v2_v2(gps->aspect_ratio, brush->gpencil_settings->aspect_ratio);
  gps->flag = gpd->runtime.sbuffer_sflag;
  gps->inittime = p->inittime;
  gps->uv_scale = 1.0f;

  /* Set stroke caps. */
  gps->caps[0] = gps->caps[1] = (short)brush->gpencil_settings->caps_type;

  /* allocate enough memory for a continuous array for storage points */
  const int subdivide = brush->gpencil_settings->draw_subdivide;

  gps->points = MEM_callocN(sizeof(bGPDspoint) * gps->totpoints, "gp_stroke_points");
  gps->dvert = NULL;

  /* set pointer to first non-initialized point */
  pt = gps->points + (gps->totpoints - totelem);
  if (gps->dvert != NULL) {
    dvert = gps->dvert + (gps->totpoints - totelem);
  }

  /* Apply the vertex color to fill. */
  ED_gpencil_fill_vertex_color_set(ts, brush, gps);

  /* copy points from the buffer to the stroke */
  if (p->paintmode == GP_PAINTMODE_DRAW_STRAIGHT) {
    /* straight lines only -> only endpoints */
    {
      /* first point */
      ptc = gpd->runtime.sbuffer;

      /* convert screen-coordinates to appropriate coordinates (and store them) */
      gpencil_stroke_convertcoords(p, ptc->m_xy, &pt->x, NULL);
      /* copy pressure and time */
      pt->pressure = ptc->pressure;
      pt->strength = ptc->strength;
      CLAMP(pt->strength, MIN2(GPENCIL_STRENGTH_MIN, brush_settings->draw_strength), 1.0f);
      copy_v4_v4(pt->vert_color, ptc->vert_color);
      pt->time = ptc->time;
      /* Apply the vertex color to point. */
      ED_gpencil_point_vertex_color_set(ts, brush, pt, ptc);

      pt++;

      if ((ts->gpencil_flags & GP_TOOL_FLAG_CREATE_WEIGHTS) && (have_weight)) {
        BKE_gpencil_dvert_ensure(gps);
        MDeformWeight *dw = BKE_defvert_ensure_index(dvert, def_nr);
        if (dw) {
          dw->weight = ts->vgroup_weight;
        }
        dvert++;
      }
      else {
        if (dvert != NULL) {
          dvert->totweight = 0;
          dvert->dw = NULL;
          dvert++;
        }
      }
    }

    if (totelem == 2) {
      /* last point if applicable */
      ptc = ((tGPspoint *)gpd->runtime.sbuffer) + (gpd->runtime.sbuffer_used - 1);

      /* convert screen-coordinates to appropriate coordinates (and store them) */
      gpencil_stroke_convertcoords(p, ptc->m_xy, &pt->x, NULL);
      /* copy pressure and time */
      pt->pressure = ptc->pressure;
      pt->strength = ptc->strength;
      CLAMP(pt->strength, MIN2(GPENCIL_STRENGTH_MIN, brush_settings->draw_strength), 1.0f);
      pt->time = ptc->time;
      /* Apply the vertex color to point. */
      ED_gpencil_point_vertex_color_set(ts, brush, pt, ptc);

      if ((ts->gpencil_flags & GP_TOOL_FLAG_CREATE_WEIGHTS) && (have_weight)) {
        BKE_gpencil_dvert_ensure(gps);
        MDeformWeight *dw = BKE_defvert_ensure_index(dvert, def_nr);
        if (dw) {
          dw->weight = ts->vgroup_weight;
        }
      }
      else {
        if (dvert != NULL) {
          dvert->totweight = 0;
          dvert->dw = NULL;
        }
      }
    }

    /* reproject to plane (only in 3d space) */
    gpencil_reproject_toplane(p, gps);
    pt = gps->points;
    for (int i = 0; i < gps->totpoints; i++, pt++) {
      /* if parented change position relative to parent object */
      gpencil_apply_parent_point(depsgraph, obact, gpl, pt);
    }

    /* If camera view or view projection, reproject flat to view to avoid perspective effect. */
    if ((!is_depth) &&
        (((align_flag & GP_PROJECT_VIEWSPACE) && is_lock_axis_view) || (is_camera))) {
      ED_gpencil_project_stroke_to_view(p->C, p->gpl, gps);
    }
  }
  else {
    float *depth_arr = NULL;

    /* get an array of depths, far depths are blended */
    if (gpencil_project_check(p)) {
      int mval_i[2], mval_prev[2] = {0};
      int interp_depth = 0;
      int found_depth = 0;

      depth_arr = MEM_mallocN(sizeof(float) * gpd->runtime.sbuffer_used, "depth_points");

      const ViewDepths *depths = p->depths;
      int i;
      for (i = 0, ptc = gpd->runtime.sbuffer; i < gpd->runtime.sbuffer_used; i++, ptc++, pt++) {

        round_v2i_v2fl(mval_i, ptc->m_xy);

        if ((ED_view3d_depth_read_cached(depths, mval_i, depth_margin, depth_arr + i) == 0) &&
            (i && (ED_view3d_depth_read_cached_seg(
                       depths, mval_i, mval_prev, depth_margin + 1, depth_arr + i) == 0))) {
          interp_depth = true;
        }
        else {
          found_depth = true;
        }

        copy_v2_v2_int(mval_prev, mval_i);
      }

      if (found_depth == false) {
        /* Unfortunately there is not much we can do when the depth isn't found,
         * ignore depth in this case, use the 3D cursor. */
        for (i = gpd->runtime.sbuffer_used - 1; i >= 0; i--) {
          depth_arr[i] = 0.9999f;
        }
      }
      else {
        if ((ts->gpencil_v3d_align & GP_PROJECT_DEPTH_STROKE) &&
            ((ts->gpencil_v3d_align & GP_PROJECT_DEPTH_STROKE_ENDPOINTS) ||
             (ts->gpencil_v3d_align & GP_PROJECT_DEPTH_STROKE_FIRST))) {
          int first_valid = 0;
          int last_valid = 0;

          /* find first valid contact point */
          for (i = 0; i < gpd->runtime.sbuffer_used; i++) {
            if (depth_arr[i] != DEPTH_INVALID) {
              break;
            }
          }
          first_valid = i;

          /* find last valid contact point */
          if (ts->gpencil_v3d_align & GP_PROJECT_DEPTH_STROKE_FIRST) {
            last_valid = first_valid;
          }
          else {
            for (i = gpd->runtime.sbuffer_used - 1; i >= 0; i--) {
              if (depth_arr[i] != DEPTH_INVALID) {
                break;
              }
            }
            last_valid = i;
          }
          /* invalidate any other point, to interpolate between
           * first and last contact in an imaginary line between them */
          for (i = 0; i < gpd->runtime.sbuffer_used; i++) {
            if (!ELEM(i, first_valid, last_valid)) {
              depth_arr[i] = DEPTH_INVALID;
            }
          }
          interp_depth = true;
        }

        if (interp_depth) {
          interp_sparse_array(depth_arr, gpd->runtime.sbuffer_used, DEPTH_INVALID);
        }
      }
    }

    pt = gps->points;
    dvert = gps->dvert;

    /* convert all points (normal behavior) */
    int i;
    for (i = 0, ptc = gpd->runtime.sbuffer; i < gpd->runtime.sbuffer_used && ptc;
         i++, ptc++, pt++) {
      /* convert screen-coordinates to appropriate coordinates (and store them) */
      gpencil_stroke_convertcoords(p, ptc->m_xy, &pt->x, depth_arr ? depth_arr + i : NULL);

      /* copy pressure and time */
      pt->pressure = ptc->pressure;
      pt->strength = ptc->strength;
      CLAMP(pt->strength, MIN2(GPENCIL_STRENGTH_MIN, brush_settings->draw_strength), 1.0f);
      copy_v4_v4(pt->vert_color, ptc->vert_color);
      pt->time = ptc->time;
      pt->uv_fac = ptc->uv_fac;
      pt->uv_rot = ptc->uv_rot;
      /* Apply the vertex color to point. */
      ED_gpencil_point_vertex_color_set(ts, brush, pt, ptc);

      if (dvert != NULL) {
        dvert->totweight = 0;
        dvert->dw = NULL;
        dvert++;
      }
    }

    /* subdivide and smooth the stroke */
    if ((brush->gpencil_settings->flag & GP_BRUSH_GROUP_SETTINGS) && (subdivide > 0)) {
      gpencil_subdivide_stroke(gpd, gps, subdivide);
    }

    /* Smooth stroke after subdiv - only if there's something to do for each iteration,
     * the factor is reduced to get a better smoothing
     * without changing too much the original stroke. */
    if ((brush->gpencil_settings->flag & GP_BRUSH_GROUP_SETTINGS) &&
        (brush->gpencil_settings->draw_smoothfac > 0.0f)) {
      float reduce = 0.0f;
      for (int r = 0; r < brush->gpencil_settings->draw_smoothlvl; r++) {
        for (i = 0; i < gps->totpoints - 1; i++) {
          BKE_gpencil_stroke_smooth_point(
              gps, i, brush->gpencil_settings->draw_smoothfac - reduce, false);
          BKE_gpencil_stroke_smooth_strength(gps, i, brush->gpencil_settings->draw_smoothfac);
        }
        reduce += 0.25f; /* reduce the factor */
      }
    }
    /* If reproject the stroke using Stroke mode, need to apply a smooth because
     * the reprojection creates small jitter. */
    if (ts->gpencil_v3d_align & GP_PROJECT_DEPTH_STROKE) {
      float ifac = (float)brush->gpencil_settings->input_samples / 10.0f;
      float sfac = interpf(1.0f, 0.2f, ifac);
      for (i = 0; i < gps->totpoints - 1; i++) {
        BKE_gpencil_stroke_smooth_point(gps, i, sfac, false);
        BKE_gpencil_stroke_smooth_strength(gps, i, sfac);
      }
    }

    /* Simplify adaptive */
    if ((brush->gpencil_settings->flag & GP_BRUSH_GROUP_SETTINGS) &&
        (brush->gpencil_settings->simplify_f > 0.0f)) {
      BKE_gpencil_stroke_simplify_adaptive(gpd, gps, brush->gpencil_settings->simplify_f);
    }

    /* reproject to plane (only in 3d space) */
    gpencil_reproject_toplane(p, gps);
    /* change position relative to parent object */
    gpencil_apply_parent(depsgraph, obact, gpl, gps);
    /* If camera view or view projection, reproject flat to view to avoid perspective effect. */
    if ((!is_depth) && (((align_flag & GP_PROJECT_VIEWSPACE) && is_lock_axis_view) || is_camera)) {
      ED_gpencil_project_stroke_to_view(p->C, p->gpl, gps);
    }

    if (depth_arr) {
      MEM_freeN(depth_arr);
    }
  }

  /* Save material index */
  gps->mat_nr = BKE_gpencil_object_material_get_index_from_brush(p->ob, p->brush);
  if (gps->mat_nr < 0) {
    if (p->ob->actcol - 1 < 0) {
      gps->mat_nr = 0;
    }
    else {
      gps->mat_nr = p->ob->actcol - 1;
    }
  }

  /* add stroke to frame, usually on tail of the listbase, but if on back is enabled the stroke
   * is added on listbase head because the drawing order is inverse and the head stroke is the
   * first to draw. This is very useful for artist when drawing the background.
   */
  if (ts->gpencil_flags & GP_TOOL_FLAG_PAINT_ONBACK) {
    BLI_addhead(&p->gpf->strokes, gps);
  }
  else {
    BLI_addtail(&p->gpf->strokes, gps);
  }
  /* add weights */
  if ((ts->gpencil_flags & GP_TOOL_FLAG_CREATE_WEIGHTS) && (have_weight)) {
    BKE_gpencil_dvert_ensure(gps);
    for (int i = 0; i < gps->totpoints; i++) {
      MDeformVert *ve = &gps->dvert[i];
      MDeformWeight *dw = BKE_defvert_ensure_index(ve, def_nr);
      if (dw) {
        dw->weight = ts->vgroup_weight;
      }
    }
  }

  /* post process stroke */
  if ((p->brush->gpencil_settings->flag & GP_BRUSH_GROUP_SETTINGS) &&
      p->brush->gpencil_settings->flag & GP_BRUSH_TRIM_STROKE) {
    BKE_gpencil_stroke_trim(gpd, gps);
  }

  /* Join with existing strokes. */
  if (ts->gpencil_flags & GP_TOOL_FLAG_AUTOMERGE_STROKE) {
    if ((gps->prev != NULL) || (gps->next != NULL)) {
      BKE_gpencil_stroke_boundingbox_calc(gps);
      float diff_mat[4][4], ctrl1[2], ctrl2[2];
      BKE_gpencil_layer_transform_matrix_get(depsgraph, p->ob, gpl, diff_mat);
      ED_gpencil_stroke_extremes_to2d(&p->gsc, diff_mat, gps, ctrl1, ctrl2);

      int pt_index = 0;
      bool doit = true;
      while (doit && gps) {
        bGPDstroke *gps_target = ED_gpencil_stroke_nearest_to_ends(p->C,
                                                                   &p->gsc,
                                                                   gpl,
                                                                   gpl->actframe,
                                                                   gps,
                                                                   ctrl1,
                                                                   ctrl2,
                                                                   GPENCIL_MINIMUM_JOIN_DIST,
                                                                   &pt_index);

        if (gps_target != NULL) {
          /* Unselect all points of source and destination strokes. This is required to avoid
           * a change in the resolution of the original strokes during the join. */
          gpencil_stroke_unselect(gpd, gps);
          gpencil_stroke_unselect(gpd, gps_target);
          gps = ED_gpencil_stroke_join_and_trim(p->gpd, p->gpf, gps, gps_target, pt_index);
        }
        else {
          doit = false;
        }
      }
    }
    ED_gpencil_stroke_close_by_distance(gps, 0.02f);
  }

  /* Calc geometry data. */
  BKE_gpencil_stroke_geometry_update(gpd, gps);

  /* In Multiframe mode, duplicate the stroke in other frames. */
  if (GPENCIL_MULTIEDIT_SESSIONS_ON(p->gpd)) {
    const bool tail = (ts->gpencil_flags & GP_TOOL_FLAG_PAINT_ONBACK);
    BKE_gpencil_stroke_copy_to_keyframes(gpd, gpl, p->gpf, gps, tail);
  }

  gpencil_update_cache(p->gpd);
  BKE_gpencil_tag_full_update(p->gpd, gpl, p->gpf, NULL);
}

/* --- 'Eraser' for 'Paint' Tool ------ */

/* only erase stroke points that are visible */
static bool gpencil_stroke_eraser_is_occluded(
    tGPsdata *p, bGPDlayer *gpl, bGPDspoint *pt, const int x, const int y)
{
  Object *obact = (Object *)p->ownerPtr.data;
  Brush *brush = p->brush;
  Brush *eraser = p->eraser;
  BrushGpencilSettings *gp_settings = NULL;

  if (brush->gpencil_tool == GPAINT_TOOL_ERASE) {
    gp_settings = brush->gpencil_settings;
  }
  else if ((eraser != NULL) & (eraser->gpencil_tool == GPAINT_TOOL_ERASE)) {
    gp_settings = eraser->gpencil_settings;
  }

  if ((gp_settings != NULL) && (gp_settings->flag & GP_BRUSH_OCCLUDE_ERASER)) {
    RegionView3D *rv3d = p->region->regiondata;

    const int mval_i[2] = {x, y};
    float mval_3d[3];
    float fpt[3];

    float diff_mat[4][4];
    /* calculate difference matrix if parent object */
    BKE_gpencil_layer_transform_matrix_get(p->depsgraph, obact, gpl, diff_mat);

    float p_depth;
    if (ED_view3d_depth_read_cached(p->depths, mval_i, 0, &p_depth)) {
      ED_view3d_depth_unproject_v3(p->region, mval_i, (double)p_depth, mval_3d);

      const float depth_mval = ED_view3d_calc_depth_for_comparison(rv3d, mval_3d);

      mul_v3_m4v3(fpt, diff_mat, &pt->x);
      const float depth_pt = ED_view3d_calc_depth_for_comparison(rv3d, fpt);

      /* Checked occlusion flag. */
      pt->flag |= GP_SPOINT_TEMP_TAG;
      if (depth_pt > depth_mval) {
        /* Is occluded. */
        pt->flag |= GP_SPOINT_TEMP_TAG2;
        return true;
      }
    }
  }
  return false;
}

/* apply a falloff effect to brush strength, based on distance */
static float gpencil_stroke_eraser_calc_influence(tGPsdata *p,
                                                  const float mval[2],
                                                  const int radius,
                                                  const int co[2])
{
  Brush *brush = p->brush;
  /* Linear Falloff... */
  int mval_i[2];
  round_v2i_v2fl(mval_i, mval);
  float distance = (float)len_v2v2_int(mval_i, co);
  float fac;

  CLAMP(distance, 0.0f, (float)radius);
  fac = 1.0f - (distance / (float)radius);

  /* apply strength factor */
  fac *= brush->gpencil_settings->draw_strength;

  /* Control this further using pen pressure */
  if (brush->gpencil_settings->flag & GP_BRUSH_USE_PRESSURE) {
    fac *= p->pressure;
  }
  /* Return influence factor computed here */
  return fac;
}

/* helper to free a stroke */
static void gpencil_free_stroke(bGPdata *gpd, bGPDframe *gpf, bGPDstroke *gps)
{
  if (gps->points) {
    MEM_freeN(gps->points);
  }

  if (gps->dvert) {
    BKE_gpencil_free_stroke_weights(gps);
    MEM_freeN(gps->dvert);
  }

  if (gps->triangles) {
    MEM_freeN(gps->triangles);
  }
  BLI_freelinkN(&gpf->strokes, gps);
  gpencil_update_cache(gpd);
}

/**
 * Analyze points to be removed when soft eraser is used
 * to avoid that segments gets the end points rounded.
 * The round caps breaks the artistic effect.
 */
static void gpencil_stroke_soft_refine(bGPDstroke *gps)
{
  bGPDspoint *pt = NULL;
  bGPDspoint *pt2 = NULL;
  int i;

  /* Check if enough points. */
  if (gps->totpoints < 3) {
    return;
  }

  /* loop all points to untag any point that next is not tagged */
  pt = gps->points;
  for (i = 1; i < gps->totpoints - 1; i++, pt++) {
    if (pt->flag & GP_SPOINT_TAG) {
      pt2 = &gps->points[i + 1];
      if ((pt2->flag & GP_SPOINT_TAG) == 0) {
        pt->flag &= ~GP_SPOINT_TAG;
      }
    }
  }

  /* loop reverse all points to untag any point that previous is not tagged */
  pt = &gps->points[gps->totpoints - 1];
  for (i = gps->totpoints - 1; i > 0; i--, pt--) {
    if (pt->flag & GP_SPOINT_TAG) {
      pt2 = &gps->points[i - 1];
      if ((pt2->flag & GP_SPOINT_TAG) == 0) {
        pt->flag &= ~GP_SPOINT_TAG;
      }
    }
  }
}

/* eraser tool - evaluation per stroke */
static void gpencil_stroke_eraser_dostroke(tGPsdata *p,
                                           bGPDlayer *gpl,
                                           bGPDframe *gpf,
                                           bGPDstroke *gps,
                                           const float mval[2],
                                           const int radius,
                                           const rcti *rect)
{
  Brush *eraser = p->eraser;
  bGPDspoint *pt0, *pt1, *pt2;
  int pc0[2] = {0};
  int pc1[2] = {0};
  int pc2[2] = {0};
  int i;
  int mval_i[2];
  round_v2i_v2fl(mval_i, mval);

  if (gps->totpoints == 0) {
    /* just free stroke */
    gpencil_free_stroke(p->gpd, gpf, gps);
  }
  else if (gps->totpoints == 1) {
    /* only process if it hasn't been masked out... */
    if (!(p->flags & GP_PAINTFLAG_SELECTMASK) || (gps->points->flag & GP_SPOINT_SELECT)) {
      bGPDspoint pt_temp;
      gpencil_point_to_parent_space(gps->points, p->diff_mat, &pt_temp);
      gpencil_point_to_xy(&p->gsc, gps, &pt_temp, &pc1[0], &pc1[1]);
      /* Do bound-box check first. */
      if ((!ELEM(V2D_IS_CLIPPED, pc1[0], pc1[1])) && BLI_rcti_isect_pt(rect, pc1[0], pc1[1])) {
        /* only check if point is inside */
        if (len_v2v2_int(mval_i, pc1) <= radius) {
          /* free stroke */
          gpencil_free_stroke(p->gpd, gpf, gps);
        }
      }
    }
  }
  else if ((p->flags & GP_PAINTFLAG_STROKE_ERASER) ||
           (eraser->gpencil_settings->eraser_mode == GP_BRUSH_ERASER_STROKE)) {
    for (i = 0; (i + 1) < gps->totpoints; i++) {

      /* only process if it hasn't been masked out... */
      if ((p->flags & GP_PAINTFLAG_SELECTMASK) && !(gps->points->flag & GP_SPOINT_SELECT)) {
        continue;
      }

      /* get points to work with */
      pt1 = gps->points + i;
      bGPDspoint npt;
      gpencil_point_to_parent_space(pt1, p->diff_mat, &npt);
      gpencil_point_to_xy(&p->gsc, gps, &npt, &pc1[0], &pc1[1]);

      /* Do bound-box check first. */
      if ((!ELEM(V2D_IS_CLIPPED, pc1[0], pc1[1])) && BLI_rcti_isect_pt(rect, pc1[0], pc1[1])) {
        /* only check if point is inside */
        if (len_v2v2_int(mval_i, pc1) <= radius) {
          /* free stroke */
          gpencil_free_stroke(p->gpd, gpf, gps);
          return;
        }
      }
    }
  }
  else {
    /* Pressure threshold at which stroke should be culled */
    const float cull_thresh = 0.005f;

    /* Amount to decrease the pressure of each point with each stroke */
    const float strength = 0.1f;

    /* Perform culling? */
    bool do_cull = false;

    /* Clear Tags
     *
     * NOTE: It's better this way, as we are sure that
     * we don't miss anything, though things will be
     * slightly slower as a result
     */
    for (i = 0; i < gps->totpoints; i++) {
      bGPDspoint *pt = &gps->points[i];
      pt->flag &= ~GP_SPOINT_TAG;
      /* Occlusion already checked. */
      pt->flag &= ~GP_SPOINT_TEMP_TAG;
      /* Point is occluded. */
      pt->flag &= ~GP_SPOINT_TEMP_TAG2;
    }

    /* First Pass: Loop over the points in the stroke
     *   1) Thin out parts of the stroke under the brush
     *   2) Tag "too thin" parts for removal (in second pass)
     */
    for (i = 0; (i + 1) < gps->totpoints; i++) {
      /* get points to work with */
      pt0 = i > 0 ? gps->points + i - 1 : NULL;
      pt1 = gps->points + i;
      pt2 = gps->points + i + 1;

      float inf1 = 0.0f;
      float inf2 = 0.0f;

      /* only process if it hasn't been masked out... */
      if ((p->flags & GP_PAINTFLAG_SELECTMASK) && !(gps->points->flag & GP_SPOINT_SELECT)) {
        continue;
      }

      bGPDspoint npt;
      gpencil_point_to_parent_space(pt1, p->diff_mat, &npt);
      gpencil_point_to_xy(&p->gsc, gps, &npt, &pc1[0], &pc1[1]);

      gpencil_point_to_parent_space(pt2, p->diff_mat, &npt);
      gpencil_point_to_xy(&p->gsc, gps, &npt, &pc2[0], &pc2[1]);

      if (pt0) {
        gpencil_point_to_parent_space(pt0, p->diff_mat, &npt);
        gpencil_point_to_xy(&p->gsc, gps, &npt, &pc0[0], &pc0[1]);
      }
      else {
        /* avoid null values */
        copy_v2_v2_int(pc0, pc1);
      }

      /* Check that point segment of the bound-box of the eraser stroke. */
      if (((!ELEM(V2D_IS_CLIPPED, pc0[0], pc0[1])) && BLI_rcti_isect_pt(rect, pc0[0], pc0[1])) ||
          ((!ELEM(V2D_IS_CLIPPED, pc1[0], pc1[1])) && BLI_rcti_isect_pt(rect, pc1[0], pc1[1])) ||
          ((!ELEM(V2D_IS_CLIPPED, pc2[0], pc2[1])) && BLI_rcti_isect_pt(rect, pc2[0], pc2[1]))) {
        /* Check if point segment of stroke had anything to do with
         * eraser region  (either within stroke painted, or on its lines)
         * - this assumes that line-width is irrelevant.
         */
        if (gpencil_stroke_inside_circle(mval, radius, pc0[0], pc0[1], pc2[0], pc2[1])) {

          bool is_occluded_pt0 = true, is_occluded_pt1 = true, is_occluded_pt2 = true;
          if (pt0) {
            is_occluded_pt0 = ((pt0->flag & GP_SPOINT_TEMP_TAG) != 0) ?
                                  ((pt0->flag & GP_SPOINT_TEMP_TAG2) != 0) :
                                  gpencil_stroke_eraser_is_occluded(p, gpl, pt0, pc0[0], pc0[1]);
          }
          if (is_occluded_pt0) {
            is_occluded_pt1 = ((pt1->flag & GP_SPOINT_TEMP_TAG) != 0) ?
                                  ((pt1->flag & GP_SPOINT_TEMP_TAG2) != 0) :
                                  gpencil_stroke_eraser_is_occluded(p, gpl, pt1, pc1[0], pc1[1]);
            if (is_occluded_pt1) {
              is_occluded_pt2 = ((pt2->flag & GP_SPOINT_TEMP_TAG) != 0) ?
                                    ((pt2->flag & GP_SPOINT_TEMP_TAG2) != 0) :
                                    gpencil_stroke_eraser_is_occluded(p, gpl, pt2, pc2[0], pc2[1]);
            }
          }

          if (!is_occluded_pt0 || !is_occluded_pt1 || !is_occluded_pt2) {
            /* Point is affected: */
            /* Adjust thickness
             *  - Influence of eraser falls off with distance from the middle of the eraser
             *  - Second point gets less influence, as it might get hit again in the next segment
             */

            /* Adjust strength if the eraser is soft */
            if (eraser->gpencil_settings->eraser_mode == GP_BRUSH_ERASER_SOFT) {
              float f_strength = eraser->gpencil_settings->era_strength_f / 100.0f;
              float f_thickness = eraser->gpencil_settings->era_thickness_f / 100.0f;
              float influence = 0.0f;

              if (pt0) {
                influence = gpencil_stroke_eraser_calc_influence(p, mval, radius, pc0);
                pt0->strength -= influence * strength * f_strength * 0.5f;
                CLAMP_MIN(pt0->strength, 0.0f);
                pt0->pressure -= influence * strength * f_thickness * 0.5f;
              }

              influence = gpencil_stroke_eraser_calc_influence(p, mval, radius, pc1);
              pt1->strength -= influence * strength * f_strength;
              CLAMP_MIN(pt1->strength, 0.0f);
              pt1->pressure -= influence * strength * f_thickness;

              influence = gpencil_stroke_eraser_calc_influence(p, mval, radius, pc2);
              pt2->strength -= influence * strength * f_strength * 0.5f;
              CLAMP_MIN(pt2->strength, 0.0f);
              pt2->pressure -= influence * strength * f_thickness * 0.5f;

              /* if invisible, delete point */
              if ((pt0) && ((pt0->strength <= GPENCIL_ALPHA_OPACITY_THRESH) ||
                            (pt0->pressure < cull_thresh))) {
                pt0->flag |= GP_SPOINT_TAG;
                do_cull = true;
              }
              if ((pt1->strength <= GPENCIL_ALPHA_OPACITY_THRESH) ||
                  (pt1->pressure < cull_thresh)) {
                pt1->flag |= GP_SPOINT_TAG;
                do_cull = true;
              }
              if ((pt2->strength <= GPENCIL_ALPHA_OPACITY_THRESH) ||
                  (pt2->pressure < cull_thresh)) {
                pt2->flag |= GP_SPOINT_TAG;
                do_cull = true;
              }

              inf1 = 1.0f;
              inf2 = 1.0f;
            }
            else {
              /* Erase point. Only erase if the eraser is on top of the point. */
              inf1 = gpencil_stroke_eraser_calc_influence(p, mval, radius, pc1);
              if (inf1 > 0.0f) {
                pt1->pressure = 0.0f;
                pt1->flag |= GP_SPOINT_TAG;
                do_cull = true;
              }
              inf2 = gpencil_stroke_eraser_calc_influence(p, mval, radius, pc2);
              if (inf2 > 0.0f) {
                pt2->pressure = 0.0f;
                pt2->flag |= GP_SPOINT_TAG;
                do_cull = true;
              }
            }

            /* 2) Tag any point with overly low influence for removal in the next pass */
            if ((inf1 > 0.0f) &&
                (((pt1->pressure < cull_thresh) || (p->flags & GP_PAINTFLAG_HARD_ERASER) ||
                  (eraser->gpencil_settings->eraser_mode == GP_BRUSH_ERASER_HARD)))) {
              pt1->flag |= GP_SPOINT_TAG;
              do_cull = true;
            }
            if ((inf1 > 2.0f) &&
                (((pt2->pressure < cull_thresh) || (p->flags & GP_PAINTFLAG_HARD_ERASER) ||
                  (eraser->gpencil_settings->eraser_mode == GP_BRUSH_ERASER_HARD)))) {
              pt2->flag |= GP_SPOINT_TAG;
              do_cull = true;
            }
          }
        }
      }
    }

    /* Second Pass: Remove any points that are tagged */
    if (do_cull) {
      /* if soft eraser, must analyze points to be sure the stroke ends
       * don't get rounded */
      if (eraser->gpencil_settings->eraser_mode == GP_BRUSH_ERASER_SOFT) {
        gpencil_stroke_soft_refine(gps);
      }

      BKE_gpencil_stroke_delete_tagged_points(
          p->gpd, gpf, gps, gps->next, GP_SPOINT_TAG, false, false, 0);
    }
    gpencil_update_cache(p->gpd);
  }
}

/* erase strokes which fall under the eraser strokes */
static void gpencil_stroke_doeraser(tGPsdata *p)
{
  const bool is_multiedit = (bool)GPENCIL_MULTIEDIT_SESSIONS_ON(p->gpd);

  rcti rect;
  Brush *brush = p->brush;
  Brush *eraser = p->eraser;
  bool use_pressure = false;
  float press = 1.0f;
  BrushGpencilSettings *gp_settings = NULL;

  /* detect if use pressure in eraser */
  if (brush->gpencil_tool == GPAINT_TOOL_ERASE) {
    use_pressure = (bool)(brush->gpencil_settings->flag & GP_BRUSH_USE_PRESSURE);
    gp_settings = brush->gpencil_settings;
  }
  else if ((eraser != NULL) & (eraser->gpencil_tool == GPAINT_TOOL_ERASE)) {
    use_pressure = (bool)(eraser->gpencil_settings->flag & GP_BRUSH_USE_PRESSURE);
    gp_settings = eraser->gpencil_settings;
  }
  if (use_pressure) {
    press = p->pressure;
    CLAMP(press, 0.01f, 1.0f);
  }
  /* rect is rectangle of eraser */
  const int calc_radius = (int)p->radius * press;
  rect.xmin = p->mval[0] - calc_radius;
  rect.ymin = p->mval[1] - calc_radius;
  rect.xmax = p->mval[0] + calc_radius;
  rect.ymax = p->mval[1] + calc_radius;

  if ((gp_settings != NULL) && (gp_settings->flag & GP_BRUSH_OCCLUDE_ERASER)) {
    View3D *v3d = p->area->spacedata.first;
    view3d_region_operator_needs_opengl(p->win, p->region);
    ED_view3d_depth_override(p->depsgraph, p->region, v3d, NULL, V3D_DEPTH_NO_GPENCIL, &p->depths);
  }

  /* loop over all layers too, since while it's easy to restrict editing to
   * only a subset of layers, it is harder to perform the same erase operation
   * on multiple layers...
   */
  LISTBASE_FOREACH (bGPDlayer *, gpl, &p->gpd->layers) {
    /* only affect layer if it's editable (and visible) */
    if (BKE_gpencil_layer_is_editable(gpl) == false) {
      continue;
    }

    bGPDframe *init_gpf = (is_multiedit) ? gpl->frames.first : gpl->actframe;
    if (init_gpf == NULL) {
      continue;
    }

    for (bGPDframe *gpf = init_gpf; gpf; gpf = gpf->next) {
      if ((gpf == gpl->actframe) || ((gpf->flag & GP_FRAME_SELECT) && (is_multiedit))) {
        if (gpf == NULL) {
          continue;
        }
        /* calculate difference matrix */
        BKE_gpencil_layer_transform_matrix_get(p->depsgraph, p->ob, gpl, p->diff_mat);

        /* loop over strokes, checking segments for intersections */
        LISTBASE_FOREACH_MUTABLE (bGPDstroke *, gps, &gpf->strokes) {
          /* check if the color is editable */
          if (ED_gpencil_stroke_material_editable(p->ob, gpl, gps) == false) {
            continue;
          }

          /* Check if the stroke collide with mouse. */
          if (!ED_gpencil_stroke_check_collision(
                  &p->gsc, gps, p->mval, calc_radius, p->diff_mat)) {
            continue;
          }

          /* Not all strokes in the datablock may be valid in the current editor/context
           * (e.g. 2D space strokes in the 3D view, if the same datablock is shared)
           */
          if (ED_gpencil_stroke_can_use_direct(p->area, gps)) {
            gpencil_stroke_eraser_dostroke(p, gpl, gpf, gps, p->mval, calc_radius, &rect);
          }
        }

        /* If not multi-edit, exit loop. */
        if (!is_multiedit) {
          break;
        }
      }
    }
  }
}
/* ******************************************* */
/* Sketching Operator */
