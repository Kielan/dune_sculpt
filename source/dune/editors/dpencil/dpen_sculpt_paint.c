/** \file
 * \ingroup edgpencil
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
