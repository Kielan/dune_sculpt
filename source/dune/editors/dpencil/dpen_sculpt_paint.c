/**
 * Brush based operators for editing Grease Pencil strokes.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_rand.h"
#include "BLI_utildefines.h"

#include "PIL_time.h"

#include "BLT_translation.h"

#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"

#include "BKE_brush.h"
#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_deform.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_gpencil_update_cache.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_object_deform.h"
#include "BKE_report.h"

#include "UI_interface.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"
#include "RNA_prototypes.h"

#include "UI_view2d.h"

#include "ED_gpencil.h"
#include "ED_keyframing.h"
#include "ED_screen.h"
#include "ED_view3d.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "gpencil_intern.h"

/* ************************************************ */
/* General Brush Editing Context */

/* Context for brush operators */
typedef struct tGP_BrushEditData {
  /* Current editor/region/etc. */
  Depsgraph *depsgraph;
  Main *bmain;
  Scene *scene;
  Object *object;

  ScrArea *area;
  ARegion *region;

  /* Current GPencil datablock */
  bGPdata *gpd;

  /* Brush Settings */
  GP_Sculpt_Settings *settings;
  Brush *brush;
  Brush *brush_prev;

  eGP_Sculpt_Flag flag;
  eGP_Sculpt_SelectMaskFlag mask;

  /* Space Conversion Data */
  GP_SpaceConversion gsc;

  /* Is the brush currently painting? */
  bool is_painting;
  bool is_transformed;

  /* Start of new sculpt stroke */
  bool first;

  /* Is multiframe editing enabled, and are we using falloff for that? */
  bool is_multiframe;
  bool use_multiframe_falloff;

  /* Current frame */
  int cfra;

  /* Brush Runtime Data: */
  /* - position and pressure
   * - the *_prev variants are the previous values
   */
  float mval[2], mval_prev[2];
  float pressure, pressure_prev;

  /* - effect vector (e.g. 2D/3D translation for grab brush) */
  float dvec[3];

  /* rotation for evaluated data */
  float rot_eval;

  /* - multiframe falloff factor */
  float mf_falloff;

  /* active vertex group */
  int vrgroup;

  /* brush geometry (bounding box) */
  rcti brush_rect;

  /* Custom data for certain brushes */
  /* - map from bGPDstroke's to structs containing custom data about those strokes */
  GHash *stroke_customdata;
  /* - general customdata */
  void *customdata;

  /* Timer for in-place accumulation of brush effect */
  wmTimer *timer;
  bool timerTick; /* is this event from a timer */

  /* Object invert matrix */
  float inv_mat[4][4];

  RNG *rng;
} tGP_BrushEditData;

/* Callback for performing some brush operation on a single point */
typedef bool (*GP_BrushApplyCb)(tGP_BrushEditData *gso,
                                bGPDstroke *gps,
                                float rotation,
                                int pt_index,
                                const int radius,
                                const int co[2]);

/* ************************************************ */
/* Utility Functions */

/* apply lock axis reset */
static void gpencil_sculpt_compute_lock_axis(tGP_BrushEditData *gso,
                                             bGPDspoint *pt,
                                             const float save_pt[3])
{
  const ToolSettings *ts = gso->scene->toolsettings;
  const View3DCursor *cursor = &gso->scene->cursor;
  const int axis = ts->gp_sculpt.lock_axis;

  /* lock axis control */
  switch (axis) {
    case GP_LOCKAXIS_X: {
      pt->x = save_pt[0];
      break;
    }
    case GP_LOCKAXIS_Y: {
      pt->y = save_pt[1];
      break;
    }
    case GP_LOCKAXIS_Z: {
      pt->z = save_pt[2];
      break;
    }
    case GP_LOCKAXIS_CURSOR: {
      /* Compute a plane with cursor normal and position of the point before do the sculpt. */
      const float scale[3] = {1.0f, 1.0f, 1.0f};
      float plane_normal[3] = {0.0f, 0.0f, 1.0f};
      float plane[4];
      float mat[4][4];
      float r_close[3];

      loc_eul_size_to_mat4(mat, cursor->location, cursor->rotation_euler, scale);

      mul_mat3_m4_v3(mat, plane_normal);
      plane_from_point_normal_v3(plane, save_pt, plane_normal);

      /* find closest point to the plane with the new position */
      closest_to_plane_v3(r_close, plane, &pt->x);
      copy_v3_v3(&pt->x, r_close);
      break;
    }
    default: {
      break;
    }
  }
}

/* Context ---------------------------------------- */

/* Get the sculpting settings */
static GP_Sculpt_Settings *gpencil_sculpt_get_settings(Scene *scene)
{
  return &scene->toolsettings->gp_sculpt;
}

/* Brush Operations ------------------------------- */

/* Invert behavior of brush? */
static bool gpencil_brush_invert_check(tGP_BrushEditData *gso)
{
  /* The basic setting is the brush's setting (from the panel) */
  bool invert = ((gso->brush->gpencil_settings->sculpt_flag & GP_SCULPT_FLAG_INVERT) != 0) ||
                (gso->brush->gpencil_settings->sculpt_flag & BRUSH_DIR_IN);
  /* During runtime, the user can hold down the Ctrl key to invert the basic behavior */
  if (gso->flag & GP_SCULPT_FLAG_INVERT) {
    invert ^= true;
  }

  /* set temporary status */
  if (invert) {
    gso->brush->gpencil_settings->sculpt_flag |= GP_SCULPT_FLAG_TMP_INVERT;
  }
  else {
    gso->brush->gpencil_settings->sculpt_flag &= ~GP_SCULPT_FLAG_TMP_INVERT;
  }

  return invert;
}

/* Compute strength of effect */
static float gpencil_brush_influence_calc(tGP_BrushEditData *gso,
                                          const int radius,
                                          const int co[2])
{
  Brush *brush = gso->brush;

  /* basic strength factor from brush settings */
  float influence = brush->alpha;

  /* use pressure? */
  if (brush->gpencil_settings->flag & GP_BRUSH_USE_PRESSURE) {
    influence *= gso->pressure;
  }

  /* distance fading */
  int mval_i[2];
  round_v2i_v2fl(mval_i, gso->mval);
  float distance = (float)len_v2v2_int(mval_i, co);

  /* Apply Brush curve. */
  float brush_falloff = BKE_brush_curve_strength(brush, distance, (float)radius);
  influence *= brush_falloff;

  /* apply multiframe falloff */
  influence *= gso->mf_falloff;

  /* return influence */
  return influence;
}

/* Tag stroke to be recalculated. */
static void gpencil_recalc_geometry_tag(bGPDstroke *gps)
{
  bGPDstroke *gps_active = (gps->runtime.gps_orig) ? gps->runtime.gps_orig : gps;
  gps_active->flag |= GP_STROKE_TAG;
}

/* Recalc any stroke tagged. */
static void gpencil_update_geometry(bGPdata *gpd)
{
  if (gpd == NULL) {
    return;
  }

  bool changed = false;

  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
      if ((gpl->actframe != gpf) && ((gpf->flag & GP_FRAME_SELECT) == 0)) {
        continue;
      }

      LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
        if (gps->flag & GP_STROKE_TAG) {
          BKE_gpencil_stroke_geometry_update(gpd, gps);
          BKE_gpencil_tag_full_update(gpd, gpl, gpf, gps);
          gps->flag &= ~GP_STROKE_TAG;
          changed = true;
        }
      }
    }
  }
  if (changed) {
    DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);
    WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  }
}

/* ************************************************ */
/* Brush Callbacks */
/* This section defines the callbacks used by each brush to perform their magic.
 * These are called on each point within the brush's radius.
 */

/* ----------------------------------------------- */
/* Smooth Brush */

/* A simple (but slower + inaccurate)
 * smooth-brush implementation to test the algorithm for stroke smoothing. */
static bool gpencil_brush_smooth_apply(tGP_BrushEditData *gso,
                                       bGPDstroke *gps,
                                       float UNUSED(rot_eval),
                                       int pt_index,
                                       const int radius,
                                       const int co[2])
{
  float inf = gpencil_brush_influence_calc(gso, radius, co);

  /* perform smoothing */
  if (gso->brush->gpencil_settings->sculpt_mode_flag & GP_SCULPT_FLAGMODE_APPLY_POSITION) {
    BKE_gpencil_stroke_smooth_point(gps, pt_index, inf, false);
  }
  if (gso->brush->gpencil_settings->sculpt_mode_flag & GP_SCULPT_FLAGMODE_APPLY_STRENGTH) {
    BKE_gpencil_stroke_smooth_strength(gps, pt_index, inf);
  }
  if (gso->brush->gpencil_settings->sculpt_mode_flag & GP_SCULPT_FLAGMODE_APPLY_THICKNESS) {
    BKE_gpencil_stroke_smooth_thickness(gps, pt_index, inf);
  }
  if (gso->brush->gpencil_settings->sculpt_mode_flag & GP_SCULPT_FLAGMODE_APPLY_UV) {
    BKE_gpencil_stroke_smooth_uv(gps, pt_index, inf);
  }

  return true;
}

/* ----------------------------------------------- */
/* Line Thickness Brush */

/* Make lines thicker or thinner by the specified amounts */
static bool gpencil_brush_thickness_apply(tGP_BrushEditData *gso,
                                          bGPDstroke *gps,
                                          float UNUSED(rot_eval),
                                          int pt_index,
                                          const int radius,
                                          const int co[2])
{
  bGPDspoint *pt = gps->points + pt_index;
  float inf;

  /* Compute strength of effect
   * - We divide the strength by 10, so that users can set "sane" values.
   *   Otherwise, good default values are in the range of 0.093
   */
  inf = gpencil_brush_influence_calc(gso, radius, co) / 10.0f;

  /* apply */
  /* XXX: this is much too strong,
   * and it should probably do some smoothing with the surrounding stuff. */
  if (gpencil_brush_invert_check(gso)) {
    /* make line thinner - reduce stroke pressure */
    pt->pressure -= inf;
  }
  else {
    /* make line thicker - increase stroke pressure */
    pt->pressure += inf;
  }

  /* Pressure should stay within [0.0, 1.0]
   * However, it is nice for volumetric strokes to be able to exceed
   * the upper end of this range. Therefore, we don't actually clamp
   * down on the upper end.
   */
  if (pt->pressure < 0.0f) {
    pt->pressure = 0.0f;
  }

  return true;
}

/* ----------------------------------------------- */
/* Color Strength Brush */

/* Make color more or less transparent by the specified amounts */
static bool gpencil_brush_strength_apply(tGP_BrushEditData *gso,
                                         bGPDstroke *gps,
                                         float UNUSED(rot_eval),
                                         int pt_index,
                                         const int radius,
                                         const int co[2])
{
  bGPDspoint *pt = gps->points + pt_index;
  float inf;

  /* Compute strength of effect */
  inf = gpencil_brush_influence_calc(gso, radius, co) * 0.125f;

  /* Invert effect. */
  if (gpencil_brush_invert_check(gso)) {
    inf *= -1.0f;
  }

  pt->strength = clamp_f(pt->strength + inf, 0.0f, 1.0f);

  return true;
}

/* ----------------------------------------------- */
/* Grab Brush */

/* Custom data per stroke for the Grab Brush
 *
 * This basically defines the strength of the effect for each
 * affected stroke point that was within the initial range of
 * the brush region.
 */
typedef struct tGPSB_Grab_StrokeData {
  /* array of indices to corresponding points in the stroke */
  int *points;
  /* array of influence weights for each of the included points */
  float *weights;
  /* angles to calc transformation */
  float *rot_eval;

  /* capacity of the arrays */
  int capacity;
  /* actual number of items currently stored */
  int size;
} tGPSB_Grab_StrokeData;

/**
 * Initialize custom data for handling this stroke.
 */
static void gpencil_brush_grab_stroke_init(tGP_BrushEditData *gso, bGPDstroke *gps)
{
  tGPSB_Grab_StrokeData *data = NULL;

  BLI_assert(gps->totpoints > 0);

  /* Check if there are buffers already (from a prior run) */
  if (BLI_ghash_haskey(gso->stroke_customdata, gps)) {
    /* Ensure that the caches are empty
     * - Since we reuse these between different strokes, we don't
     *   want the previous invocation's data polluting the arrays
     */
    data = BLI_ghash_lookup(gso->stroke_customdata, gps);
    BLI_assert(data != NULL);

    data->size = 0; /* minimum requirement - so that we can repopulate again */

    memset(data->points, 0, sizeof(int) * data->capacity);
    memset(data->weights, 0, sizeof(float) * data->capacity);
    memset(data->rot_eval, 0, sizeof(float) * data->capacity);
  }
  else {
    /* Create new instance */
    data = MEM_callocN(sizeof(tGPSB_Grab_StrokeData), "GP Stroke Grab Data");

    data->capacity = gps->totpoints;
    data->size = 0;

    data->points = MEM_callocN(sizeof(int) * data->capacity, "GP Stroke Grab Indices");
    data->weights = MEM_callocN(sizeof(float) * data->capacity, "GP Stroke Grab Weights");
    data->rot_eval = MEM_callocN(sizeof(float) * data->capacity, "GP Stroke Grab Rotations");

    /* hook up to the cache */
    BLI_ghash_insert(gso->stroke_customdata, gps, data);
  }
}

/* store references to stroke points in the initial stage */
static bool gpencil_brush_grab_store_points(tGP_BrushEditData *gso,
                                            bGPDstroke *gps,
                                            float rot_eval,
                                            int pt_index,
                                            const int radius,
                                            const int co[2])
{
  tGPSB_Grab_StrokeData *data = BLI_ghash_lookup(gso->stroke_customdata, gps);
  float inf = gpencil_brush_influence_calc(gso, radius, co);

  BLI_assert(data != NULL);
  BLI_assert(data->size < data->capacity);

  /* insert this point into the set of affected points */
  data->points[data->size] = pt_index;
  data->weights[data->size] = inf;
  data->rot_eval[data->size] = rot_eval;
  data->size++;

  /* done */
  return true;
}

/* Compute effect vector for grab brush */
static void gpencil_brush_grab_calc_dvec(tGP_BrushEditData *gso)
{
  /* Convert mouse-movements to movement vector */
  RegionView3D *rv3d = gso->region->regiondata;
  float *rvec = gso->object->loc;
  const float zfac = ED_view3d_calc_zfac(rv3d, rvec);

  float mval_f[2];

  /* convert from 2D screenspace to 3D... */
  mval_f[0] = (float)(gso->mval[0] - gso->mval_prev[0]);
  mval_f[1] = (float)(gso->mval[1] - gso->mval_prev[1]);

  /* apply evaluated data transformation */
  if (gso->rot_eval != 0.0f) {
    const float cval = cos(gso->rot_eval);
    const float sval = sin(gso->rot_eval);
    float r[2];
    r[0] = (mval_f[0] * cval) - (mval_f[1] * sval);
    r[1] = (mval_f[0] * sval) + (mval_f[1] * cval);
    copy_v2_v2(mval_f, r);
  }

  ED_view3d_win_to_delta(gso->region, mval_f, zfac, gso->dvec);
}

/* Apply grab transform to all relevant points of the affected strokes */
static void gpencil_brush_grab_apply_cached(tGP_BrushEditData *gso,
                                            bGPDstroke *gps,
                                            const float diff_mat[4][4])
{
  tGPSB_Grab_StrokeData *data = BLI_ghash_lookup(gso->stroke_customdata, gps);
  /* If a new frame is created, could be impossible find the stroke. */
  if (data == NULL) {
    return;
  }

  float inverse_diff_mat[4][4];
  invert_m4_m4(inverse_diff_mat, diff_mat);

  /* Apply dvec to all of the stored points */
  for (int i = 0; i < data->size; i++) {
    bGPDspoint *pt = &gps->points[data->points[i]];
    float delta[3] = {0.0f};

    /* get evaluated transformation */
    gso->rot_eval = data->rot_eval[i];
    gpencil_brush_grab_calc_dvec(gso);

    /* adjust the amount of displacement to apply */
    mul_v3_v3fl(delta, gso->dvec, data->weights[i]);

    float fpt[3];
    float save_pt[3];
    copy_v3_v3(save_pt, &pt->x);
    /* apply transformation */
    mul_v3_m4v3(fpt, diff_mat, &pt->x);
    /* apply */
    add_v3_v3v3(&pt->x, fpt, delta);
    /* undo transformation to the init parent position */
    mul_m4_v3(inverse_diff_mat, &pt->x);

    /* compute lock axis */
    gpencil_sculpt_compute_lock_axis(gso, pt, save_pt);
  }
}

/* free customdata used for handling this stroke */
static void gpencil_brush_grab_stroke_free(void *ptr)
{
  tGPSB_Grab_StrokeData *data = (tGPSB_Grab_StrokeData *)ptr;

  /* free arrays */
  MEM_SAFE_FREE(data->points);
  MEM_SAFE_FREE(data->weights);
  MEM_SAFE_FREE(data->rot_eval);

  /* ... and this item itself, since it was also allocated */
  MEM_freeN(data);
}

/* ----------------------------------------------- */
/* Push Brush */
/* NOTE: Depends on gpencil_brush_grab_calc_dvec() */
static bool gpencil_brush_push_apply(tGP_BrushEditData *gso,
                                     bGPDstroke *gps,
                                     float UNUSED(rot_eval),
                                     int pt_index,
                                     const int radius,
                                     const int co[2])
{
  bGPDspoint *pt = gps->points + pt_index;
  float save_pt[3];
  copy_v3_v3(save_pt, &pt->x);

  float inf = gpencil_brush_influence_calc(gso, radius, co);
  float delta[3] = {0.0f};

  /* adjust the amount of displacement to apply */
  mul_v3_v3fl(delta, gso->dvec, inf);

  /* apply */
  mul_mat3_m4_v3(gso->inv_mat, delta); /* only rotation component */
  add_v3_v3(&pt->x, delta);

  /* compute lock axis */
  gpencil_sculpt_compute_lock_axis(gso, pt, save_pt);

  /* done */
  return true;
}

/* ----------------------------------------------- */
/* Pinch Brush */
/* Compute reference midpoint for the brush - this is what we'll be moving towards */
static void gpencil_brush_calc_midpoint(tGP_BrushEditData *gso)
{
  /* Convert mouse position to 3D space
   * See: gpencil_paint.c :: gpencil_stroke_convertcoords()
   */
  RegionView3D *rv3d = gso->region->regiondata;
  const float *rvec = gso->object->loc;
  const float zfac = ED_view3d_calc_zfac(rv3d, rvec);

  float mval_prj[2];

  if (ED_view3d_project_float_global(gso->region, rvec, mval_prj, V3D_PROJ_TEST_NOP) ==
      V3D_PROJ_RET_OK) {
    float dvec[3];
    float xy_delta[2];
    sub_v2_v2v2(xy_delta, mval_prj, gso->mval);
    ED_view3d_win_to_delta(gso->region, xy_delta, zfac, dvec);
    sub_v3_v3v3(gso->dvec, rvec, dvec);
  }
  else {
    zero_v3(gso->dvec);
  }
}

/* Shrink distance between midpoint and this point... */
static bool gpencil_brush_pinch_apply(tGP_BrushEditData *gso,
                                      bGPDstroke *gps,
                                      float UNUSED(rot_eval),
                                      int pt_index,
                                      const int radius,
                                      const int co[2])
{
  bGPDspoint *pt = gps->points + pt_index;
  float fac, inf;
  float vec[3];
  float save_pt[3];
  copy_v3_v3(save_pt, &pt->x);

  /* Scale down standard influence value to get it more manageable...
   * - No damping = Unmanageable at > 0.5 strength
   * - Div 10     = Not enough effect
   * - Div 5      = Happy medium... (by trial and error)
   */
  inf = gpencil_brush_influence_calc(gso, radius, co) / 5.0f;

  /* 1) Make this point relative to the cursor/midpoint (dvec) */
  float fpt[3];
  mul_v3_m4v3(fpt, gso->object->obmat, &pt->x);
  sub_v3_v3v3(vec, fpt, gso->dvec);

  /* 2) Shrink the distance by pulling the point towards the midpoint
   *    (0.0 = at midpoint, 1 = at edge of brush region)
   *                         OR
   *    Increase the distance (if inverting the brush action!)
   */
  if (gpencil_brush_invert_check(gso)) {
    /* Inflate (inverse) */
    fac = 1.0f + (inf * inf); /* squared to temper the effect... */
  }
  else {
    /* Shrink (default) */
    fac = 1.0f - (inf * inf); /* squared to temper the effect... */
  }
  mul_v3_fl(vec, fac);

  /* 3) Translate back to original space, with the shrinkage applied */
  add_v3_v3v3(fpt, gso->dvec, vec);
  mul_v3_m4v3(&pt->x, gso->object->imat, fpt);

  /* compute lock axis */
  gpencil_sculpt_compute_lock_axis(gso, pt, save_pt);

  /* done */
  return true;
}

/* ----------------------------------------------- */
/* Twist Brush - Rotate Around midpoint */
/* Take the screenspace coordinates of the point, rotate this around the brush midpoint,
 * convert the rotated point and convert it into "data" space
 */

static bool gpencil_brush_twist_apply(tGP_BrushEditData *gso,
                                      bGPDstroke *gps,
                                      float UNUSED(rot_eval),
                                      int pt_index,
                                      const int radius,
                                      const int co[2])
{
  bGPDspoint *pt = gps->points + pt_index;
  float angle, inf;
  float save_pt[3];
  copy_v3_v3(save_pt, &pt->x);

  /* Angle to rotate by */
  inf = gpencil_brush_influence_calc(gso, radius, co);
  angle = DEG2RADF(1.0f) * inf;

  if (gpencil_brush_invert_check(gso)) {
    /* invert angle that we rotate by */
    angle *= -1;
  }

  /* Rotate in 2D or 3D space? */
  if (gps->flag & GP_STROKE_3DSPACE) {
    /* Perform rotation in 3D space... */
    RegionView3D *rv3d = gso->region->regiondata;
    float rmat[3][3];
    float axis[3];
    float vec[3];

    /* Compute rotation matrix - rotate around view vector by angle */
    negate_v3_v3(axis, rv3d->persinv[2]);
    normalize_v3(axis);

    axis_angle_normalized_to_mat3(rmat, axis, angle);

    /* Rotate point */
    float fpt[3];
    mul_v3_m4v3(fpt, gso->object->obmat, &pt->x);
    sub_v3_v3v3(vec, fpt, gso->dvec); /* make relative to center
                                       * (center is stored in dvec) */
    mul_m3_v3(rmat, vec);
    add_v3_v3v3(fpt, vec, gso->dvec); /* restore */
    mul_v3_m4v3(&pt->x, gso->object->imat, fpt);

    /* compute lock axis */
    gpencil_sculpt_compute_lock_axis(gso, pt, save_pt);
  }
  else {
    const float axis[3] = {0.0f, 0.0f, 1.0f};
    float vec[3] = {0.0f};
    float rmat[3][3];

    /* Express position of point relative to cursor, ready to rotate */
    /* XXX: There is still some offset here, but it's close to working as expected. */
    vec[0] = (float)(co[0] - gso->mval[0]);
    vec[1] = (float)(co[1] - gso->mval[1]);

    /* rotate point */
    axis_angle_normalized_to_mat3(rmat, axis, angle);
    mul_m3_v3(rmat, vec);

    /* Convert back to screen-coordinates */
    vec[0] += (float)gso->mval[0];
    vec[1] += (float)gso->mval[1];

    /* Map from screen-coordinates to final coordinate space */
    if (gps->flag & GP_STROKE_2DSPACE) {
      View2D *v2d = gso->gsc.v2d;
      UI_view2d_region_to_view(v2d, vec[0], vec[1], &pt->x, &pt->y);
    }
    else {
      /* XXX */
      copy_v2_v2(&pt->x, vec);
    }
  }

  /* done */
  return true;
}

/* ----------------------------------------------- */
/* Randomize Brush */
/* Apply some random jitter to the point */
static bool gpencil_brush_randomize_apply(tGP_BrushEditData *gso,
                                          bGPDstroke *gps,
                                          float UNUSED(rot_eval),
                                          int pt_index,
                                          const int radius,
                                          const int co[2])
{
  bGPDspoint *pt = gps->points + pt_index;
  float save_pt[3];
  copy_v3_v3(save_pt, &pt->x);

  /* Amount of jitter to apply depends on the distance of the point to the cursor,
   * as well as the strength of the brush
   */
  const float inf = gpencil_brush_influence_calc(gso, radius, co) / 2.0f;
  const float fac = BLI_rng_get_float(gso->rng) * inf;

  /* apply random to position */
  if (gso->brush->gpencil_settings->sculpt_mode_flag & GP_SCULPT_FLAGMODE_APPLY_POSITION) {
    /* Jitter is applied perpendicular to the mouse movement vector
     * - We compute all effects in screenspace (since it's easier)
     *   and then project these to get the points/distances in
     *   view-space as needed.
     */
    float mvec[2], svec[2];

    /* mouse movement in ints -> floats */
    mvec[0] = (float)(gso->mval[0] - gso->mval_prev[0]);
    mvec[1] = (float)(gso->mval[1] - gso->mval_prev[1]);

    /* rotate mvec by 90 degrees... */
    svec[0] = -mvec[1];
    svec[1] = mvec[0];

    /* scale the displacement by the random displacement, and apply */
    if (BLI_rng_get_float(gso->rng) > 0.5f) {
      mul_v2_fl(svec, -fac);
    }
    else {
      mul_v2_fl(svec, fac);
    }

    /* convert to dataspace */
    if (gps->flag & GP_STROKE_3DSPACE) {
      /* 3D: Project to 3D space */
      bool flip;
      RegionView3D *rv3d = gso->region->regiondata;
      const float zfac = ED_view3d_calc_zfac_ex(rv3d, &pt->x, &flip);
      if (flip == false) {
        float dvec[3];
        ED_view3d_win_to_delta(gso->gsc.region, svec, zfac, dvec);
        add_v3_v3(&pt->x, dvec);
        /* compute lock axis */
        gpencil_sculpt_compute_lock_axis(gso, pt, save_pt);
      }
    }
  }
  /* apply random to strength */
  if (gso->brush->gpencil_settings->sculpt_mode_flag & GP_SCULPT_FLAGMODE_APPLY_STRENGTH) {
    if (BLI_rng_get_float(gso->rng) > 0.5f) {
      pt->strength += fac;
    }
    else {
      pt->strength -= fac;
    }
    CLAMP_MIN(pt->strength, 0.0f);
    CLAMP_MAX(pt->strength, 1.0f);
  }
  /* apply random to thickness (use pressure) */
  if (gso->brush->gpencil_settings->sculpt_mode_flag & GP_SCULPT_FLAGMODE_APPLY_THICKNESS) {
    if (BLI_rng_get_float(gso->rng) > 0.5f) {
      pt->pressure += fac;
    }
    else {
      pt->pressure -= fac;
    }
    /* only limit lower value */
    CLAMP_MIN(pt->pressure, 0.0f);
  }
  /* apply random to UV (use pressure) */
  if (gso->brush->gpencil_settings->sculpt_mode_flag & GP_SCULPT_FLAGMODE_APPLY_UV) {
    if (BLI_rng_get_float(gso->rng) > 0.5f) {
      pt->uv_rot += fac;
    }
    else {
      pt->uv_rot -= fac;
    }
    CLAMP(pt->uv_rot, -M_PI_2, M_PI_2);
  }

  /* done */
  return true;
}

/* ************************************************ */
/* Non Callback-Based Brushes */
/* Clone Brush ------------------------------------- */
/* How this brush currently works:
 * - If this is start of the brush stroke, paste immediately under the cursor
 *   by placing the midpoint of the buffer strokes under the cursor now
 *
 * - Otherwise, in:
 *   "Stamp Mode" - Move the newly pasted strokes so that their center follows the cursor
 *   "Continuous" - Repeatedly just paste new copies for where the brush is now
 */

/* Custom state data for clone brush */
typedef struct tGPSB_CloneBrushData {
  /* midpoint of the strokes on the clipboard */
  float buffer_midpoint[3];

  /* number of strokes in the paste buffer (and/or to be created each time) */
  size_t totitems;

  /* for "stamp" mode, the currently pasted brushes */
  bGPDstroke **new_strokes;

  /** Mapping from colors referenced per stroke, to the new colors in the "pasted" strokes. */
  GHash *new_colors;
} tGPSB_CloneBrushData;

/* Initialize "clone" brush data. */
static void gpencil_brush_clone_init(bContext *C, tGP_BrushEditData *gso)
{
  tGPSB_CloneBrushData *data;
  bGPDstroke *gps;

  /* Initialize custom-data. */
  gso->customdata = data = MEM_callocN(sizeof(tGPSB_CloneBrushData), "CloneBrushData");

  /* compute midpoint of strokes on clipboard */
  for (gps = gpencil_strokes_copypastebuf.first; gps; gps = gps->next) {
    if (ED_gpencil_stroke_can_use(C, gps)) {
      const float dfac = 1.0f / ((float)gps->totpoints);
      float mid[3] = {0.0f};

      bGPDspoint *pt;
      int i;

      /* compute midpoint of this stroke */
      for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
        float co[3];

        mul_v3_v3fl(co, &pt->x, dfac);
        add_v3_v3(mid, co);
      }

      /* combine this stroke's data with the main data */
      add_v3_v3(data->buffer_midpoint, mid);
      data->totitems++;
    }
  }

  /* Divide the midpoint by the number of strokes, to finish averaging it */
  if (data->totitems > 1) {
    mul_v3_fl(data->buffer_midpoint, 1.0f / (float)data->totitems);
  }

  /* Create a buffer for storing the current strokes */
  if (1 /*gso->brush->mode == GP_EDITBRUSH_CLONE_MODE_STAMP*/) {
    data->new_strokes = MEM_callocN(sizeof(bGPDstroke *) * data->totitems,
                                    "cloned strokes ptr array");
  }

  /* Init colormap for mapping between the pasted stroke's source color (names)
   * and the final colors that will be used here instead.
   */
  data->new_colors = gpencil_copybuf_validate_colormap(C);
}

/* Free custom data used for "clone" brush */
static void gpencil_brush_clone_free(tGP_BrushEditData *gso)
{
  tGPSB_CloneBrushData *data = gso->customdata;

  /* free strokes array */
  MEM_SAFE_FREE(data->new_strokes);

  /* free copybuf colormap */
  if (data->new_colors) {
    BLI_ghash_free(data->new_colors, NULL, NULL);
    data->new_colors = NULL;
  }

  /* free the customdata itself */
  MEM_freeN(data);
  gso->customdata = NULL;
}

/* Create new copies of the strokes on the clipboard */
static void gpencil_brush_clone_add(bContext *C, tGP_BrushEditData *gso)
{
  tGPSB_CloneBrushData *data = gso->customdata;

  Object *ob = gso->object;
  bGPdata *gpd = (bGPdata *)ob->data;
  Scene *scene = gso->scene;
  bGPDstroke *gps;

  float delta[3];
  size_t strokes_added = 0;

  /* Compute amount to offset the points by */
  /* NOTE: This assumes that screenspace strokes are NOT used in the 3D view... */

  gpencil_brush_calc_midpoint(gso); /* this puts the cursor location into gso->dvec */
  sub_v3_v3v3(delta, gso->dvec, data->buffer_midpoint);

  /* Copy each stroke into the layer */
  for (gps = gpencil_strokes_copypastebuf.first; gps; gps = gps->next) {
    if (ED_gpencil_stroke_can_use(C, gps)) {
      bGPDstroke *new_stroke;
      bGPDspoint *pt;
      int i;

      bGPDlayer *gpl = NULL;
      /* Try to use original layer. */
      if (gps->runtime.tmp_layerinfo[0] != '\0') {
        gpl = BKE_gpencil_layer_named_get(gpd, gps->runtime.tmp_layerinfo);
      }

      /* if not available, use active layer. */
      if (gpl == NULL) {
        gpl = CTX_data_active_gpencil_layer(C);
      }
      bGPDframe *gpf = BKE_gpencil_layer_frame_get(
          gpl, CFRA, IS_AUTOKEY_ON(scene) ? GP_GETFRAME_ADD_NEW : GP_GETFRAME_USE_PREV);
      if (gpf == NULL) {
        continue;
      }

      /* Make a new stroke */
      new_stroke = BKE_gpencil_stroke_duplicate(gps, true, true);

      new_stroke->next = new_stroke->prev = NULL;
      BLI_addtail(&gpf->strokes, new_stroke);

      /* Fix color references */
      Material *ma = BLI_ghash_lookup(data->new_colors, POINTER_FROM_INT(new_stroke->mat_nr));
      new_stroke->mat_nr = BKE_gpencil_object_material_index_get(ob, ma);
      if (!ma || new_stroke->mat_nr < 0) {
        new_stroke->mat_nr = 0;
      }
      /* Adjust all the stroke's points, so that the strokes
       * get pasted relative to where the cursor is now
       */
      for (i = 0, pt = new_stroke->points; i < new_stroke->totpoints; i++, pt++) {
        /* Rotate around center new position */
        mul_mat3_m4_v3(gso->object->obmat, &pt->x); /* only rotation component */

        /* assume that the delta can just be applied, and then everything works */
        add_v3_v3(&pt->x, delta);
        mul_m4_v3(gso->object->imat, &pt->x);
      }

      /* Store ref for later */
      if ((data->new_strokes) && (strokes_added < data->totitems)) {
        data->new_strokes[strokes_added] = new_stroke;
        strokes_added++;
      }
    }
  }
}

/* Move newly-added strokes around - "Stamp" mode of the Clone brush */
static void gpencil_brush_clone_adjust(tGP_BrushEditData *gso)
{
  tGPSB_CloneBrushData *data = gso->customdata;
  size_t snum;

  /* Compute the amount of movement to apply (overwrites dvec) */
  gso->rot_eval = 0.0f;
  gpencil_brush_grab_calc_dvec(gso);

  /* For each of the stored strokes, apply the offset to each point */
  /* NOTE: Again this assumes that in the 3D view,
   * we only have 3d space and not screenspace strokes... */
  for (snum = 0; snum < data->totitems; snum++) {
    bGPDstroke *gps = data->new_strokes[snum];
    bGPDspoint *pt;
    int i;

    for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
      /* "Smudge" Effect falloff */
      float delta[3] = {0.0f};
      int sco[2] = {0};
      float influence;

      /* compute influence on point */
      gpencil_point_to_xy(&gso->gsc, gps, pt, &sco[0], &sco[1]);
      influence = gpencil_brush_influence_calc(gso, gso->brush->size, sco);

      /* adjust the amount of displacement to apply */
      mul_v3_v3fl(delta, gso->dvec, influence);

      /* apply */
      add_v3_v3(&pt->x, delta);
    }
  }
}

/* Entry-point for applying "clone" brush. */
static bool gpencil_sculpt_brush_apply_clone(bContext *C, tGP_BrushEditData *gso)
{
  /* Which "mode" are we operating in? */
  if (gso->first) {
    /* Create initial clones */
    gpencil_brush_clone_add(C, gso);
  }
  else {
    /* Stamp or Continuous Mode */
    if (1 /*gso->brush->mode == GP_EDITBRUSH_CLONE_MODE_STAMP*/) {
      /* Stamp - Proceed to translate the newly added strokes */
      gpencil_brush_clone_adjust(gso);
    }
    else {
      /* Continuous - Just keep pasting every time we move. */
      /* TODO: The spacing of repeat should be controlled using a
       * "stepsize" or similar property? */
      gpencil_brush_clone_add(C, gso);
    }
  }

  return true;
}

/* ************************************************ */
/* Header Info for GPencil Sculpt */

static void gpencil_sculpt_brush_header_set(bContext *C, tGP_BrushEditData *gso)
{
  Brush *brush = gso->brush;
  char str[UI_MAX_DRAW_STR] = "";

  BLI_snprintf(str,
               sizeof(str),
               TIP_("GPencil Sculpt: %s Stroke  | LMB to paint | RMB/Escape to Exit"
                    " | Ctrl to Invert Action | Wheel Up/Down for Size "
                    " | Shift-Wheel Up/Down for Strength"),
               brush->id.name + 2);

  ED_workspace_status_text(C, str);
}

/* ************************************************ */
/* Grease Pencil Sculpting Operator */

/* Init/Exit ----------------------------------------------- */

static bool gpencil_sculpt_brush_init(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  ToolSettings *ts = scene->toolsettings;
  Object *ob = CTX_data_active_object(C);

  /* set the brush using the tool */
  tGP_BrushEditData *gso;

  /* setup operator data */
  gso = MEM_callocN(sizeof(tGP_BrushEditData), "tGP_BrushEditData");
  op->customdata = gso;

  gso->depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  gso->bmain = CTX_data_main(C);
  /* store state */
  gso->settings = gpencil_sculpt_get_settings(scene);

  /* Random generator, only init once. */
  uint rng_seed = (uint)(PIL_check_seconds_timer_i() & UINT_MAX);
  rng_seed ^= POINTER_AS_UINT(gso);
  gso->rng = BLI_rng_new(rng_seed);

  gso->is_painting = false;
  gso->first = true;

  gso->gpd = ED_gpencil_data_get_active(C);
  gso->cfra = INT_MAX; /* NOTE: So that first stroke will get handled in init_stroke() */

  gso->scene = scene;
  gso->object = ob;
  if (ob) {
    invert_m4_m4(gso->inv_mat, ob->obmat);
    gso->vrgroup = gso->gpd->vertex_group_active_index - 1;
    if (!BLI_findlink(&gso->gpd->vertex_group_names, gso->vrgroup)) {
      gso->vrgroup = -1;
    }
    /* Check if some modifier can transform the stroke. */
    gso->is_transformed = BKE_gpencil_has_transform_modifiers(ob);
  }
  else {
    unit_m4(gso->inv_mat);
    gso->vrgroup = -1;
    gso->is_transformed = false;
  }

  gso->area = CTX_wm_area(C);
  gso->region = CTX_wm_region(C);

  Paint *paint = &ts->gp_sculptpaint->paint;
  gso->brush = paint->brush;
  BKE_curvemapping_init(gso->brush->curve);

  /* save mask */
  gso->mask = ts->gpencil_selectmode_sculpt;

  /* multiframe settings */
  gso->is_multiframe = (bool)GPENCIL_MULTIEDIT_SESSIONS_ON(gso->gpd);
  gso->use_multiframe_falloff = (ts->gp_sculpt.flag & GP_SCULPT_SETT_FLAG_FRAME_FALLOFF) != 0;

  /* Init multi-edit falloff curve data before doing anything,
   * so we won't have to do it again later. */
  if (gso->is_multiframe) {
    BKE_curvemapping_init(ts->gp_sculpt.cur_falloff);
  }

  /* Initialize custom data for brushes. */
  char tool = gso->brush->gpencil_sculpt_tool;
  switch (tool) {
    case GPSCULPT_TOOL_CLONE: {
      bGPDstroke *gps;
      bool found = false;

      /* check that there are some usable strokes in the buffer */
      for (gps = gpencil_strokes_copypastebuf.first; gps; gps = gps->next) {
        if (ED_gpencil_stroke_can_use(C, gps)) {
          found = true;
          break;
        }
      }

      if (found == false) {
        /* STOP HERE! Nothing to paste! */
        BKE_report(op->reports,
                   RPT_ERROR,
                   "Copy some strokes to the clipboard before using the Clone brush to paste "
                   "copies of them");

        MEM_freeN(gso);
        op->customdata = NULL;
        return false;
      }
      /* Initialize custom-data. */
      gpencil_brush_clone_init(C, gso);
      break;
    }

    case GPSCULPT_TOOL_GRAB: {
      /* Initialize the cache needed for this brush. */
      gso->stroke_customdata = BLI_ghash_ptr_new("GP Grab Brush - Strokes Hash");
      break;
    }

    /* Others - No customdata needed */
    default:
      break;
  }

  /* setup space conversions */
  gpencil_point_conversion_init(C, &gso->gsc);

  /* update header */
  gpencil_sculpt_brush_header_set(C, gso);

  return true;
}

static void gpencil_sculpt_brush_exit(bContext *C, wmOperator *op)
{
  tGP_BrushEditData *gso = op->customdata;
  wmWindow *win = CTX_wm_window(C);
  char tool = gso->brush->gpencil_sculpt_tool;

  /* free brush-specific data */
  switch (tool) {
    case GPSCULPT_TOOL_GRAB: {
      /* Free per-stroke customdata
       * - Keys don't need to be freed, as those are the strokes
       * - Values assigned to those keys do, as they are custom structs
       */
      BLI_ghash_free(gso->stroke_customdata, NULL, gpencil_brush_grab_stroke_free);
      break;
    }

    case GPSCULPT_TOOL_CLONE: {
      /* Free customdata */
      gpencil_brush_clone_free(gso);
      break;
    }

    default:
      break;
  }

  /* unregister timer (only used for realtime) */
  if (gso->timer) {
    WM_event_remove_timer(CTX_wm_manager(C), win, gso->timer);
  }

  if (gso->rng != NULL) {
    BLI_rng_free(gso->rng);
  }

  /* Disable headerprints. */
  ED_workspace_status_text(C, NULL);

  /* disable temp invert flag */
  gso->brush->gpencil_settings->sculpt_flag &= ~GP_SCULPT_FLAG_TMP_INVERT;

  /* Update geometry data for tagged strokes. */
  gpencil_update_geometry(gso->gpd);

  /* free operator data */
  MEM_freeN(gso);
  op->customdata = NULL;
}

/* poll callback for stroke sculpting operator(s) */
static bool gpencil_sculpt_brush_poll(bContext *C)
{
  ScrArea *area = CTX_wm_area(C);
  if (area && area->spacetype != SPACE_VIEW3D) {
    return false;
  }

  /* NOTE: this is a bit slower, but is the most accurate... */
  return CTX_DATA_COUNT(C, editable_gpencil_strokes) != 0;
}

/* Init Sculpt Stroke ---------------------------------- */

static void gpencil_sculpt_brush_init_stroke(bContext *C, tGP_BrushEditData *gso)
{
  bGPdata *gpd = gso->gpd;

  Scene *scene = gso->scene;
  int cfra = CFRA;

  /* only try to add a new frame if this is the first stroke, or the frame has changed */
  if ((gpd == NULL) || (cfra == gso->cfra)) {
    return;
  }

  /* go through each layer, and ensure that we've got a valid frame to use */
  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    if (!IS_AUTOKEY_ON(scene) && (gpl->actframe == NULL)) {
      continue;
    }

    /* only editable and visible layers are considered */
    if (BKE_gpencil_layer_is_editable(gpl) && (gpl->actframe != NULL)) {
      bGPDframe *gpf = gpl->actframe;

      /* Make a new frame to work on if the layer's frame
       * and the current scene frame don't match up:
       * - This is useful when animating as it saves that "uh-oh" moment when you realize you've
       *   spent too much time editing the wrong frame.
       */
      if (IS_AUTOKEY_ON(scene) && (gpf->framenum != cfra)) {
        BKE_gpencil_frame_addcopy(gpl, cfra);
        BKE_gpencil_tag_full_update(gpd, gpl, NULL, NULL);
        /* Need tag to recalculate evaluated data to avoid crashes. */
        DEG_id_tag_update(&gso->gpd->id, ID_RECALC_GEOMETRY);
        WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
      }
    }
  }

  /* save off new current frame, so that next update works fine */
  gso->cfra = cfra;
}

/* Apply ----------------------------------------------- */

/* Get angle of the segment relative to the original segment before any transformation
 * For strokes with one point only this is impossible to calculate because there isn't a
 * valid reference point.
 */
