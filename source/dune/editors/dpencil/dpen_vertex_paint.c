/** Brush based operators for editing DPen strokes. **/

#include "MEM_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_math.h"

#include "i18n_translation.h"

#include "types_brush.h"
#include "types_dpen.h"
#include "types_material.h"

#include "dune_brush.h"
#include "dune_colortools.h"
#include "dune_context.h"
#include "dune_dpen.h"
#include "dune_material.h"
#include "dune_report.h"

#include "wm_api.h"
#include "wm_types.h"

#include "api_access.h"
#include "api_define.h"
#include "api_prototypes.h"

#include "UI_view2d.h"

#include "ed_dpen.h"
#include "ed_screen.h"
#include "ed_view3d.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "dpen_intern.h"

/* ************************************************ */
/* General Brush Editing Context */
#define DPEN_SELECT_BUFFER_CHUNK 256
#define DPEN_GRID_PIXEL_SIZE 10.0f

/* Temp Flags while Painting. */
typedef enum eDPDvertex_brush_Flag {
  /* invert the effect of the brush */
  DPEN_VERTEX_FLAG_INVERT = (1 << 0),
  /* temporary invert action */
  DPEN_VERTEX_FLAG_TMP_INVERT = (1 << 1),
} eDPDvertex_brush_Flag;

/* Grid of Colors for Smear. */
typedef struct tDPen_Grid {
  /** Lower right corner of rectangle of grid cell. */
  float bottom[2];
  /** Upper left corner of rectangle of grid cell. */
  float top[2];
  /** Average Color */
  float color[4];
  /** Total points included. */
  int totcol;

} tDPen_Grid;

/* List of points affected by brush. */
typedef struct tDPen_Selected {
  /** Referenced stroke. */
  DPenStroke *dps;
  /** Point index in points array. */
  int pt_index;
  /** Position */
  int pc[2];
  /** Color */
  float color[4];
} tDPen_Selected;

/* Context for brush operators */
typedef struct tDPen_BrushVertexpaintData {
  Scene *scene;
  Object *object;

  ARegion *region;

  /* Current DPen datablock */
  DPenData *dpd;

  Brush *brush;
  float linear_color[3];
  eDPenVertex_brush_Flag flag;
  eDPen_Vertex_SelectMaskFlag mask;

  /* Space Conversion Data */
  DPen_SpaceConversion dsc;

  /* Is the brush currently painting? */
  bool is_painting;

  /* Start of new paint */
  bool first;

  /* Is multiframe editing enabled, and are we using falloff for that? */
  bool is_multiframe;
  bool use_multiframe_falloff;

  /* Brush Runtime Data: */
  /* - position and pressure
   * - the *_prev variants are the previous values
   */
  float mval[2], mval_prev[2];
  float pressure, pressure_prev;

  /* - Effect 2D vector */
  float dvec[2];

  /* - multiframe falloff factor */
  float mf_falloff;

  /* brush geometry (bounding box) */
  rcti brush_rect;

  /* Temp data to save selected points */
  /** Stroke buffer. */
  tDPen_Selected *pbuffer;
  /** Number of elements currently used in cache. */
  int pbuffer_used;
  /** Number of total elements available in cache. */
  int pbuffer_size;

  /** Grid of average colors */
  tDPen_Grid *grid;
  /** Total number of rows/cols. */
  int grid_size;
  /** Total number of cells elements in the grid array. */
  int grid_len;
  /** Grid sample position (used to determine distance of falloff) */
  int grid_sample[2];
  /** Grid is ready to use */
  bool grid_ready;

} tDPen_BrushVertexpaintData;

/* Ensure the buffer to hold temp selected point size is enough to save all points selected. */
static tDPen_Selected *dpen_select_buffer_ensure(tDPen_Selected *buffer_array,
                                                  int *buffer_size,
                                                  int *buffer_used,
                                                  const bool clear)
{
  tDPen_Selected *p = NULL;

  /* By default a buffer is created with one block with a predefined number of free slots,
   * if the size is not enough, the cache is reallocated adding a new block of free slots.
   * This is done in order to keep cache small and improve speed. */
  if (*buffer_used + 1 > *buffer_size) {
    if ((*buffer_size == 0) || (buffer_array == NULL)) {
      p = MEM_callocN(sizeof(struct tDPen_Selected) * DPEN_SELECT_BUFFER_CHUNK, __func__);
      *buffer_size = DPEN_SELECT_BUFFER_CHUNK;
    }
    else {
      *buffer_size += DPEN_SELECT_BUFFER_CHUNK;
      p = MEM_recallocN(buffer_array, sizeof(struct tGP_Selected) * *buffer_size);
    }

    if (p == NULL) {
      *buffer_size = *buffer_used = 0;
    }

    buffer_array = p;
  }

  /* clear old data */
  if (clear) {
    *buffer_used = 0;
    if (buffer_array != NULL) {
      memset(buffer_array, 0, sizeof(tGP_Selected) * *buffer_size);
    }
  }

  return buffer_array;
}

/* Brush Operations ------------------------------- */

/* Invert behavior of brush? */
static bool brush_invert_check(tDPen_BrushVertexpaintData *dso)
{
  /* The basic setting is no inverted */
  bool invert = false;

  /* During runtime, the user can hold down the Ctrl key to invert the basic behavior */
  if (dso->flag & DPEN_VERTEX_FLAG_INVERT) {
    invert ^= true;
  }

  return invert;
}

/* Compute strength of effect. */
static float brush_influence_calc(tDPen_BrushVertexpaintData *gso, const int radius, const int co[2])
{
  Brush *brush = dso->brush;
  float influence = brush->size;

  /* use pressure? */
  if (brush->dpen_settings->flag & DPEN_BRUSH_USE_PRESSURE) {
    influence *= dso->pressure;
  }

  /* distance fading */
  int mval_i[2];
  round_v2i_v2fl(mval_i, dso->mval);
  float distance = (float)len_v2v2_int(mval_i, co);

  /* Apply Brush curve. */
  float brush_falloff = dune_brush_curve_strength(brush, distance, (float)radius);
  influence *= brush_falloff;

  /* apply multiframe falloff */
  influence *= dso->mf_falloff;

  /* return influence */
  return influence;
}

/* Compute effect vector for directional brushes. */
static void brush_calc_dvec_2d(tDPen_BrushVertexpaintData *dso)
{
  dso->dvec[0] = (float)(dso->mval[0] - dso->mval_prev[0]);
  dso->dvec[1] = (float)(dso->mval[1] - dso->mval_prev[1]);

  normalize_v2(dso->dvec);
}

/* Init a grid of cells around mouse position.
 *
 * For each Cell.
 *
 *          *--------* Top
 *          |        |
 *          |        |
 *   Bottom *--------*
 *
 * The number of cells is calculated using the brush size and a predefined
 * number of pixels (see: DPEN_GRID_PIXEL_SIZE)
 */

static void dpen_grid_cells_init(tDPen_BrushVertexpaintData *gso)
{
  tDPen_Grid *grid;
  float bottom[2];
  float top[2];
  int grid_index = 0;

  /* The grid center is (0,0). */
  bottom[0] = dso->brush_rect.xmin - dso->mval[0];
  bottom[1] = dso->brush_rect.ymax - DPEN_GRID_PIXEL_SIZE - dso->mval[1];

  /* Calc all cell of the grid from top/left. */
  for (int y = dso->grid_size - 1; y >= 0; y--) {
    top[1] = bottom[1] + DPEN_GRID_PIXEL_SIZE;

    for (int x = 0; x < dso->grid_size; x++) {
      top[0] = bottom[0] + DPEN_GRID_PIXEL_SIZE;

      grid = &dso->grid[grid_index];

      copy_v2_v2(grid->bottom, bottom);
      copy_v2_v2(grid->top, top);

      bottom[0] += DPEN_GRID_PIXEL_SIZE;

      grid_index++;
    }

    /* Reset for new row. */
    bottom[0] = dso->brush_rect.xmin - dso->mval[0];
    bottom[1] -= DPEN_GRID_PIXEL_SIZE;
  }
}

/* Get the index used in the grid base on dvec. */
static void dpen_grid_cell_average_color_idx_get(tDPen_BrushVertexpaintData *dso, int r_idx[2])
{
  /* Lower direction. */
  if (dso->dvec[1] < 0.0f) {
    if ((dso->dvec[0] >= -1.0f) && (dso->dvec[0] < -0.8f)) {
      r_idx[0] = 0;
      r_idx[1] = -1;
    }
    else if ((dso->dvec[0] >= -0.8f) && (dso->dvec[0] < -0.6f)) {
      r_idx[0] = -1;
      r_idx[1] = -1;
    }
    else if ((dso->dvec[0] >= -0.6f) && (dso->dvec[0] < 0.6f)) {
      r_idx[0] = -1;
      r_idx[1] = 0;
    }
    else if ((dso->dvec[0] >= 0.6f) && (gso->dvec[0] < 0.8f)) {
      r_idx[0] = -1;
      r_idx[1] = 1;
    }
    else if (dso->dvec[0] >= 0.8f) {
      r_idx[0] = 0;
      r_idx[1] = 1;
    }
  }
  /* Upper direction. */
  else {
    if ((dso->dvec[0] >= -1.0f) && (gso->dvec[0] < -0.8f)) {
      r_idx[0] = 0;
      r_idx[1] = -1;
    }
    else if ((dso->dvec[0] >= -0.8f) && (gso->dvec[0] < -0.6f)) {
      r_idx[0] = 1;
      r_idx[1] = -1;
    }
    else if ((dso->dvec[0] >= -0.6f) && (gso->dvec[0] < 0.6f)) {
      r_idx[0] = 1;
      r_idx[1] = 0;
    }
    else if ((dso->dvec[0] >= 0.6f) && (gso->dvec[0] < 0.8f)) {
      r_idx[0] = 1;
      r_idx[1] = 1;
    }
    else if (dso->dvec[0] >= 0.8f) {
      r_idx[0] = 0;
      r_idx[1] = 1;
    }
  }
}

static int dpen_grid_cell_index_get(tDPen_BrushVertexpaintData *dso, const int pc[2])
{
  float bottom[2], top[2];

  for (int i = 0; i < dso->grid_len; i++) {
    tDPen_Grid *grid = &dso->grid[i];
    add_v2_v2v2(bottom, grid->bottom, dso->mval);
    add_v2_v2v2(top, grid->top, dso->mval);

    if (pc[0] >= bottom[0] && pc[0] <= top[0] && pc[1] >= bottom[1] && pc[1] <= top[1]) {
      return i;
    }
  }

  return -1;
}

/* Fill the grid with the color in each cell and assign point cell index. */
static void dpen_grid_colors_calc(tDPen_BrushVertexpaintData *dso)
{
  tDPen_Selected *selected = NULL;
  DPenStroke *dps_selected = NULL;
  DPenPoint *pt = NULL;
  tDPen_Grid *grid = NULL;

  /* Don't calculate again. */
  if (dso->grid_ready) {
    return;
  }

  /* Extract colors by cell. */
  for (int i = 0; i < dso->pbuffer_used; i++) {
    selected = &dso->pbuffer[i];
    dps_selected = selected->dps;
    pt = &dps_selected->points[selected->pt_index];
    int grid_index = dpen_grid_cell_index_get(dso, selected->pc);

    if (grid_index > -1) {
      grid = &dso->grid[grid_index];
      /* Add stroke mix color (only if used). */
      if (pt->vert_color[3] > 0.0f) {
        add_v3_v3(grid->color, selected->color);
        grid->color[3] = 1.0f;
        grid->totcol++;
      }
    }
  }

  /* Average colors. */
  for (int i = 0; i < dso->grid_len; i++) {
    grid = &dso->grid[i];
    if (grid->totcol > 0) {
      mul_v3_fl(grid->color, (1.0f / (float)grid->totcol));
    }
  }

  /* Save sample position. */
  round_v2i_v2fl(dso->grid_sample, dso->mval);

  dso->grid_ready = true;
}

/* ************************************************ */
/* Brush Callbacks
 * This section defines the callbacks used by each brush to perform their magic.
 * These are called on each point within the brush's radius. */

/* Tint Brush */
static bool brush_tint_apply(tDPen_BrushVertexpaintData *dso,
                             DPenStroke *dps,
                             int pt_index,
                             const int radius,
                             const int co[2])
{
  Brush *brush = dso->brush;

  /* Attenuate factor to get a smoother tinting. */
  float inf = (brush_influence_calc(dso, radius, co) * brush->dpen_settings->draw_strength) /
              100.0f;
  float inf_fill = (dso->pressure * brush->dpen_settings->draw_strength) / 1000.0f;

  CLAMP(inf, 0.0f, 1.0f);
  CLAMP(inf_fill, 0.0f, 1.0f);

  /* Apply color to Stroke point. */
  if (DPEN_TINT_VERTEX_COLOR_STROKE(brush) && (pt_index > -1)) {
    DPenPoint *pt = &dps->points[pt_index];
    if (brush_invert_check(dso)) {
      pt->vert_color[3] -= inf;
      CLAMP_MIN(pt->vert_color[3], 0.0f);
    }
    else {
      /* Pre-multiply. */
      mul_v3_fl(pt->vert_color, pt->vert_color[3]);
      /* "Alpha over" blending. */
      interp_v3_v3v3(pt->vert_color, pt->vert_color, gso->linear_color, inf);
      pt->vert_color[3] = pt->vert_color[3] * (1.0 - inf) + inf;
      /* Un pre-multiply. */
      if (pt->vert_color[3] > 0.0f) {
        mul_v3_fl(pt->vert_color, 1.0f / pt->vert_color[3]);
      }
    }
  }

  /* Apply color to Fill area (all with same color and factor). */
  if (DPEN_TINT_VERTEX_COLOR_FILL(brush)) {
    if (brush_invert_check(gso)) {
      dps->vert_color_fill[3] -= inf_fill;
      CLAMP_MIN(dps->vert_color_fill[3], 0.0f);
    }
    else {
      /* Pre-multiply. */
      mul_v3_fl(dps->vert_color_fill, dps->vert_color_fill[3]);
      /* "Alpha over" blending. */
      interp_v3_v3v3(dps->vert_color_fill, dps->vert_color_fill, dso->linear_color, inf_fill);
      dps->vert_color_fill[3] = dps->vert_color_fill[3] * (1.0 - inf_fill) + inf_fill;
      /* Un pre-multiply. */
      if (dps->vert_color_fill[3] > 0.0f) {
        mul_v3_fl(dps->vert_color_fill, 1.0f / dps->vert_color_fill[3]);
      }
    }
  }

  return true;
}

/* Replace Brush (Don't use pressure or invert). */
static bool brush_replace_apply(tDPen_BrushVertexpaintData *dso, DPenStroke *dps, int pt_index)
{
  Brush *brush = dso->brush;
  DPenPoint *pt = &dps->points[pt_index];

  /* Apply color to Stroke point. */
  if (DPEN_TINT_VERTEX_COLOR_STROKE(brush)) {
    if (pt->vert_color[3] > 0.0f) {
      copy_v3_v3(pt->vert_color, dso->linear_color);
    }
  }

  /* Apply color to Fill area (all with same color and factor). */
  if (DPEN_TINT_VERTEX_COLOR_FILL(brush)) {
    if (dps->vert_color_fill[3] > 0.0f) {
      copy_v3_v3(dps->vert_color_fill, dso->linear_color);
    }
  }

  return true;
}

/* Get surrounding color. */
static bool get_surrounding_color(tDPen_BrushVertexpaintData *dso,
                                  DPenStroke *dps,
                                  int pt_index,
                                  float r_color[3])
{
  tDPen_Selected *selected = NULL;
  DPenStroke *dps_selected = NULL;
  DpenPoint *pt = NULL;

  int totcol = 0;
  zero_v3(r_color);

  /* Average the surrounding points except current one. */
  for (int i = 0; i < dso->pbuffer_used; i++) {
    selected = &dso->pbuffer[i];
    dps_selected = selected->dps;
    /* current point is not evaluated. */
    if ((dps_selected == dps) && (selected->pt_index == pt_index)) {
      continue;
    }

    pt = &dps_selected->points[selected->pt_index];

    /* Add stroke mix color (only if used). */
    if (pt->vert_color[3] > 0.0f) {
      add_v3_v3(r_color, selected->color);
      totcol++;
    }
  }
  if (totcol > 0) {
    mul_v3_fl(r_color, (1.0f / (float)totcol));
    return true;
  }

  return false;
}

/* Blur Brush */
static bool brush_blur_apply(tDPen_BrushVertexpaintData *gso,
                             DPenDtroke *dps,
                             int pt_index,
                             const int radius,
                             const int co[2])
{
  Brush *brush = dso->brush;

  /* Attenuate factor to get a smoother tinting. */
  float inf = (brush_influence_calc(dso, radius, co) * brush->dpen_settings->draw_strength) /
              100.0f;
  float inf_fill = (dso->pressure * brush->dpen_settings->draw_strength) / 1000.0f;

  DPenPoint *pt = &dps->points[pt_index];

  /* Get surrounding color. */
  float blur_color[3];
  if (get_surrounding_color(dso, dps, pt_index, blur_color)) {
    /* Apply color to Stroke point. */
    if (DPEN_TINT_VERTEX_COLOR_STROKE(brush)) {
      interp_v3_v3v3(pt->vert_color, pt->vert_color, blur_color, inf);
    }

    /* Apply color to Fill area (all with same color and factor). */
    if (DPEN_TINT_VERTEX_COLOR_FILL(brush)) {
      interp_v3_v3v3(dps->vert_color_fill, dps->vert_color_fill, blur_color, inf_fill);
    }
    return true;
  }

  return false;
}

/* Average Brush */
static bool brush_average_apply(tDPen_BrushVertexpaintData *dso,
                                DPenStroke *dps,
                                int pt_index,
                                const int radius,
                                const int co[2],
                                float average_color[3])
{
  Brush *brush = dso->brush;

  /* Attenuate factor to get a smoother tinting. */
  float inf = (brush_influence_calc(dso, radius, co) * brush->gpencil_settings->draw_strength) /
              100.0f;
  float inf_fill = (dso->pressure * brush->dpen_settings->draw_strength) / 1000.0f;

  DPenPoint *pt = &dps->points[pt_index];

  float alpha = pt->vert_color[3];
  float alpha_fill = dps->vert_color_fill[3];

  if (brush_invert_check(dso)) {
    alpha -= inf;
    alpha_fill -= inf_fill;
  }
  else {
    alpha += inf;
    alpha_fill += inf_fill;
  }

  /* Apply color to Stroke point. */
  if (DPEN_TINT_VERTEX_COLOR_STROKE(brush)) {
    CLAMP(alpha, 0.0f, 1.0f);
    interp_v3_v3v3(pt->vert_color, pt->vert_color, average_color, inf);
    pt->vert_color[3] = alpha;
  }

  /* Apply color to Fill area (all with same color and factor). */
  if (DPEN_TINT_VERTEX_COLOR_FILL(brush)) {
    CLAMP(alpha_fill, 0.0f, 1.0f);
    copy_v3_v3(dps->vert_color_fill, average_color);
    dps->vert_color_fill[3] = alpha_fill;
  }

  return true;
}

/* Smear Brush */
static bool brush_smear_apply(tDPen_BrushVertexpaintData *dso,
                              DPenStroke *dps,
                              int pt_index,
                              tDPen_Selected *selected)
{
  Brush *brush = dso->brush;
  tDPen_Grid *grid = NULL;
  int average_idx[2];
  ARRAY_SET_ITEMS(average_idx, 0, 0);

  bool changed = false;

  /* Need some movement, so first input is not done. */
  if (dso->first) {
    return false;
  }

  DPenPoint *pt = &dps->points[pt_index];

  /* Need get average colors in the grid. */
  if ((!dso->grid_ready) && (dso->pbuffer_used > 0)) {
    dpen_grid_colors_calc(gso);
  }

  /* The influence is equal to strength and no decay around brush radius. */
  float inf = brush->dpen_settings->draw_strength;
  if (brush->flag & DPEN_BRUSH_USE_PRESSURE) {
    inf *= dso->pressure;
  }

  /* Calc distance from initial sample location and add a falloff effect. */
  int mval_i[2];
  round_v2i_v2fl(mval_i, dso->mval);
  float distance = (float)len_v2v2_int(mval_i, dso->grid_sample);
  float fac = 1.0f - (distance / (float)(brush->size * 2));
  CLAMP(fac, 0.0f, 1.0f);
  inf *= fac;

  /* Retry row and col for average color. */
  dpen_grid_cell_average_color_idx_get(dso, average_idx);

  /* Retry average color cell. */
  int grid_index = dpen_grid_cell_index_get(dso, selected->pc);
  if (grid_index > -1) {
    int row = grid_index / dso->grid_size;
    int col = grid_index - (dso->grid_size * row);
    row += average_idx[0];
    col += average_idx[1];
    CLAMP(row, 0, dso->grid_size);
    CLAMP(col, 0, dso->grid_size);

    int new_index = (row * dso->grid_size) + col;
    CLAMP(new_index, 0, dso->grid_len - 1);
    grid = &dso->grid[new_index];
  }

  /* Apply color to Stroke point. */
  if (DPEN_TINT_VERTEX_COLOR_STROKE(brush)) {
    if (grid_index > -1) {
      if (grid->color[3] > 0.0f) {
        // copy_v3_v3(pt->vert_color, grid->color);
        interp_v3_v3v3(pt->vert_color, pt->vert_color, grid->color, inf);
        changed = true;
      }
    }
  }

  /* Apply color to Fill area (all with same color and factor). */
  if (DPEN_TINT_VERTEX_COLOR_FILL(brush)) {
    if (grid_index > -1) {
      if (grid->color[3] > 0.0f) {
        interp_v3_v3v3(dps->vert_color_fill, dps->vert_color_fill, grid->color, inf);
        changed = true;
      }
    }
  }

  return changed;
}

/* ************************************************ */
/* Header Info */
static void dpen_vertexpaint_brush_header_set(dContext *C)
{
  ed_workspace_status_text(C,
                           TIP_("DPen Vertex Paint: LMB to paint | RMB/Escape to Exit"
                                " | Ctrl to Invert Action"));
}

/* ************************************************ */
/* DPen Vertex Paint Operator */

/* Init/Exit ----------------------------------------------- */

static bool dpen_vertexpaint_brush_init(dContext *C, wmOperator *op)
{
  Scene *scene = ctx_data_scene(C);
  ToolSettings *ts = ctx_data_tool_settings(C);
  Object *ob = ctx_data_active_object(C);
  Paint *paint = ob->mode == OB_MODE_VERTEX_DPEN ? &ts->dpen_vertexpaint->paint :
                                                      &ts->dpen_paint->paint;

  /* set the brush using the tool */
  tDPen_BrushVertexpaintData *dso;

  /* setup operator data */
  dso = MEM_callocN(sizeof(tDPen_BrushVertexpaintData), "tDPen_BrushVertexpaintData");
  op->customdata = dso;

  dso->brush = paint->brush;
  srgb_to_linearrgb_v3_v3(dso->linear_color, dso->brush->rgb);
  dune_curvemapping_init(dso->brush->curve);

  dso->is_painting = false;
  fso->first = true;

  dso->pbuffer = NULL;
  dso->pbuffer_size = 0;
  dso->pbuffer_used = 0;

  /* Alloc grid array */
  dso->grid_size = (int)(((dso->brush->size * 2.0f) / DPEN_GRID_PIXEL_SIZE) + 1.0);
  /* Square value. */
  dso->grid_len = dso->grid_size * dso->grid_size;
  dso->grid = MEM_callocN(sizeof(tDPen_Grid) * dso->grid_len, "tDPen_Grid");
  dso->grid_ready = false;

  dso->dpd = ed_dpen_data_get_active(C);
  dso->scene = scene;
  dso->object = ob;

  dso->region = ctx_wm_region(C);

  /* Save mask. */
  dso->mask = ts->dpen_selectmode_vertex;

  /* Multiframe settings. */
  dso->is_multiframe = (bool)DPEN_MULTIEDIT_SESSIONS_ON(dso->dpd);
  dso->use_multiframe_falloff = (ts->dp_sculpt.flag & DPEN_SCULPT_SETT_FLAG_FRAME_FALLOFF) != 0;

  /* Init multi-edit falloff curve data before doing anything,
   * so we won't have to do it again later. */
  if (dso->is_multiframe) {
    dune_curvemapping_init(ts->dp_sculpt.cur_falloff);
  }

  /* Setup space conversions. */
  dpen_point_conversion_init(C, &dso->dsc);

  /* Update header. */
  dpen_vertexpaint_brush_header_set(C);

  return true;
}

static void dpen_vertexpaint_brush_exit(dContext *C, wmOperator *op)
{
  tDPen_BrushVertexpaintData *dso = op->customdata;

  /* Disable headerprints. */
  ed_workspace_status_text(C, NULL);

  /* Disable temp invert flag. */
  dso->brush->flag &= ~DPEN_VERTEX_FLAG_TMP_INVERT;

  /* Free operator data */
  MEM_SAFE_FREE(dso->pbuffer);
  MEM_SAFE_FREE(dso->grid);
  MEM_SAFE_FREE(dso);
  op->customdata = NULL;
}

/* Poll callback for stroke vertex paint operator. */
static bool dpen_vertexpaint_brush_poll(dContext *C)
{
  /* NOTE: this is a bit slower, but is the most accurate... */
  return CTX_DATA_COUNT(C, editable_dpen_strokes) != 0;
}

/* Helper to save the points selected by the brush. */
static void dpen_save_selected_point(tDPen_BrushVertexpaintData *dso,
                                        DPenStroke *dps,
                                        int index,
                                        int pc[2])
{
  tDPen_Selected *selected;
  DPenPoint *pt = &dps->points[index];

  /* Ensure the array to save the list of selected points is big enough. */
  dso->pbuffer = dpen_select_buffer_ensure(
      dso->pbuffer, &dso->pbuffer_size, &dso->pbuffer_used, false);

  selected = &dso->pbuffer[dso->pbuffer_used];
  selected->dps = dps;
  selected->pt_index = index;
  /* Check the index is not a special case for fill. */
  if (index > -1) {
    copy_v2_v2_int(selected->pc, pc);
    copy_v4_v4(selected->color, pt->vert_color);
  }
  dso->pbuffer_used++;
}

/* Select points in this stroke and add to an array to be used later.
 * Returns true if any point was hit and got saved */
static bool dpen_vertexpaint_select_stroke(tDPen_BrushVertexpaintData *dso,
                                              DPenStroke *dps,
                                              const char tool,
                                              const float diff_mat[4][4],
                                              const float bound_mat[4][4])
{
  DPen_SpaceConversion *dsc = &dso->dsc;
  rcti *rect = &dso->brush_rect;
  Brush *brush = dso->brush;
  const int radius = (brush->flag & DPEN_BRUSH_USE_PRESSURE) ? dso->brush->size * dso->pressure :
                                                             dso->brush->size;
  DPenStroke *dps_active = (dps->runtime.dps_orig) ? dps->runtime.dps_orig : dps;
  DPenPoint *pt_active = NULL;

  DPenPoint *pt1, *pt2;
  DPenPoint *pt = NULL;
  int pc1[2] = {0};
  int pc2[2] = {0};
  int i;
  int index;
  bool include_last = false;

  bool saved = false;

  /* Check stroke masking. */
  if (DPEN_ANY_VERTEX_MASK(dso->mask)) {
    if ((dps->flag & DPEN_STROKE_SELECT) == 0) {
      return false;
    }
  }

  /* Check if the stroke collide with brush. */
  if (!ed_dpen_stroke_check_collision(dsc, dps, dso->mval, radius, bound_mat)) {
    return false;
  }

  if (dps->totpoints == 1) {
    DPenPoint pt_temp;
    pt = &dps->points[0];
    dpen_point_to_parent_space(dps->points, diff_mat, &pt_temp);
    dpen_point_to_xy(dsc, dps, &pt_temp, &pc1[0], &pc1[1]);

    pt_active = (pt->runtime.pt_orig) ? pt->runtime.pt_orig : pt;
    /* Do bound-box check first. */
    if ((!ELEM(V2D_IS_CLIPPED, pc1[0], pc1[1])) && lib_rcti_isect_pt(rect, pc1[0], pc1[1])) {
      /* only check if point is inside */
      int mval_i[2];
      round_v2i_v2fl(mval_i, dso->mval);
      if (len_v2v2_int(mval_i, pc1) <= radius) {
        /* apply operation to this point */
        if (pt_active != NULL) {
          dpen_save_selected_point(dso, dps_active, 0, pc1);
          saved = true;
        }
      }
    }
  }
  else {
    /* Loop over the points in the stroke, checking for intersections
     * - an intersection means that we touched the stroke
     */
    bool hit = false;
    for (i = 0; (i + 1) < dps->totpoints; i++) {
      /* Get points to work with */
      pt1 = dps->points + i;
      pt2 = dps->points + i + 1;

      /* Skip if neither one is selected
       * (and we are only allowed to edit/consider selected points) */
      if (DPEN_ANY_VERTEX_MASK(dso->mask)) {
        if (!(pt1->flag & DPEN_SPOINT_SELECT) && !(pt2->flag & GP_SPOINT_SELECT)) {
          include_last = false;
          continue;
        }
      }

      DPenPoint npt;
      dpen_point_to_parent_space(pt1, diff_mat, &npt);
      dpen_point_to_xy(dsc, dps, &npt, &pc1[0], &pc1[1]);

      dpen_point_to_parent_space(pt2, diff_mat, &npt);
      dpen_point_to_xy(dsc, dps, &npt, &pc2[0], &pc2[1]);

      /* Check that point segment of the bound-box of the selection stroke. */
      if (((!ELEM(V2D_IS_CLIPPED, pc1[0], pc1[1])) && lib_rcti_isect_pt(rect, pc1[0], pc1[1])) ||
          ((!ELEM(V2D_IS_CLIPPED, pc2[0], pc2[1])) && lib_rcti_isect_pt(rect, pc2[0], pc2[1]))) {
        /* Check if point segment of stroke had anything to do with
         * brush region  (either within stroke painted, or on its lines)
         * - this assumes that line-width is irrelevant.
         */
        if (dpen_stroke_inside_circle(dso->mval, radius, pc1[0], pc1[1], pc2[0], pc2[1])) {

          /* To each point individually... */
          pt = &dps->points[i];
          pt_active = pt->runtime.pt_orig;
          if (pt_active != NULL) {
            /* If masked and the point is not selected, skip it. */
            if (DPEN_ANY_VERTEX_MASK(gso->mask) &&
                ((pt_active->flag & DPEN_SPOINT_SELECT) == 0)) {
              continue;
            }
            index = (pt->runtime.pt_orig) ? pt->runtime.idx_orig : i;
            hit = true;
            dpen_save_selected_point(dso, dps_active, index, pc1);
            saved = true;
          }

          /* Only do the second point if this is the last segment,
           * and it is unlikely that the point will get handled
           * otherwise.
           *
           * NOTE: There is a small risk here that the second point wasn't really
           *       actually in-range. In that case, it only got in because
           *       the line linking the points was!
           */
          if (i + 1 == dps->totpoints - 1) {
            pt = &dps->points[i + 1];
            pt_active = pt->runtime.pt_orig;
            if (pt_active != NULL) {
              index = (pt->runtime.pt_orig) ? pt->runtime.idx_orig : i + 1;
              hit = true;
              dpen_save_selected_point(dso, dps_active, index, pc2);
              include_last = false;
              saved = true;
            }
          }
          else {
            include_last = true;
          }
        }
        else if (include_last) {
          /* This case is for cases where for whatever reason the second vert (1st here)
           * doesn't get included because the whole edge isn't in bounds,
           * but it would've qualified since it did with the previous step
           * (but wasn't added then, to avoid double-ups).
           */
          pt = &dps->points[i];
          pt_active = pt->runtime.pt_orig;
          if (pt_active != NULL) {
            index = (pt->runtime.pt_orig) ? pt->runtime.idx_orig : i;
            hit = true;
            dpen_save_selected_point(gso, gps_active, index, pc1);
            include_last = false;
            saved = true;
          }
        }
      }
    }

    /* If nothing hit, check if the mouse is inside any filled stroke. */
    if ((!hit) && (ELEM(tool, DPENPAINT_TOOL_TINT, DPENVERTEX_TOOL_DRAW))) {
      MaterialDPenStyle *dp_style = dune_dpen_material_settings(dso->object,
                                                                     dps_active->mat_nr + 1);
      if (dpen_style->flag & DPEN_MATERIAL_FILL_SHOW) {
        int mval[2];
        round_v2i_v2fl(mval, dso->mval);
        bool hit_fill = ed_dpen_stroke_point_is_inside(dps_active, dsc, mval, diff_mat);
        if (hit_fill) {
          /* Need repeat the effect because if we don't do that the tint process
           * is very slow. */
          for (int repeat = 0; repeat < 50; repeat++) {
            dpen_save_selected_point(dso, dps_active, -1, NULL);
          }
          saved = true;
        }
      }
    }
  }

  return saved;
}

/* Apply vertex paint brushes to strokes in the given frame. */
static bool dpen_vertexpaint_brush_do_frame(dContext *C,
                                               tDPen_BrushVertexpaintData *dso,
                                               DPenLayer *dpl,
                                               DPenFrame *dpf,
                                               const float diff_mat[4][4],
                                               const float bound_mat[4][4])
{
  Object *ob = ctx_data_active_object(C);
  const char tool = ob->mode == OB_MODE_VERTEX_DPEN ? dso->brush->dpen_vertex_tool :
                                                         dso->brush->dpen_tool;
  const int radius = (dso->brush->flag & DPEN_BRUSH_USE_PRESSURE) ?
                         dso->brush->size * dso->pressure :
                         dso->brush->size;
  tDPen_Selected *selected = NULL;
  int i;

  /*---------------------------------------------------------------------
   * First step: select the points affected. This step is required to have
   * all selected points before apply the effect, because it could be
   * required to average data.
   *--------------------------------------------------------------------- */
  LISTBASE_FOREACH (DPenStroke *, dps, &dpf->strokes) {
    /* Skip strokes that are invalid for current view. */
    if (ed_dpen_stroke_can_use(C, dps) == false) {
      continue;
    }
    /* Check if the color is editable. */
    if (ed_dpen_stroke_material_editable(ob, dpl, dps) == false) {
      continue;
    }

    /* Check points below the brush. */
    bool hit = dpen_vertexpaint_select_stroke(dso, dps, tool, diff_mat, bound_mat);

    /* If stroke was hit and has an editcurve the curve needs an update. */
    DPenStroke *dps_active = (dps->runtime.dps_orig) ? dps->runtime.dps_orig : dps;
    if (dps_active->editcurve != NULL && hit) {
      dps_active->editcurve->flag |= DPEN_CURVE_NEEDS_STROKE_UPDATE;
    }
  }

  /* For Average tool, need calculate the average resulting color from all colors
   * under the brush. */
  float average_color[3] = {0};
  int totcol = 0;
  if ((tool == DPENVERTEX_TOOL_AVERAGE) && (dso->pbuffer_used > 0)) {
    for (i = 0; i < dso->pbuffer_used; i++) {
      selected = &dso->pbuffer[i];
      DPenstroke *dps = selected->dps;
      DPenPoint *pt = &dps->points[selected->pt_index];

      /* Add stroke mix color (only if used). */
      if (pt->vert_color[3] > 0.0f) {
        add_v3_v3(average_color, pt->vert_color);
        totcol++;
      }

      /* If Fill color mix, add to average. */
      if (dps->vert_color_fill[3] > 0.0f) {
        add_v3_v3(average_color, dps->vert_color_fill);
        totcol++;
      }
    }

    /* Get average. */
    if (totcol > 0) {
      mul_v3_fl(average_color, (1.0f / (float)totcol));
    }
  }

  /*---------------------------------------------------------------------
   * Second step: Apply effect.
   *--------------------------------------------------------------------- */
  bool changed = false;
  for (i = 0; i < dso->pbuffer_used; i++) {
    changed = true;
    selected = &dso->pbuffer[i];

    switch (tool) {
      case DPEN_PAINT_TOOL_TINT:
      case DPEN_VERTEX_TOOL_DRAW: {
        brush_tint_apply(dso, selected->dps, selected->pt_index, radius, selected->pc);
        changed |= true;
        break;
      }
      case DPEN_VERTEX_TOOL_BLUR: {
        brush_blur_apply(dso, selected->dps, selected->pt_index, radius, selected->pc);
        changed |= true;
        break;
      }
      case DPEN_VERTEX_TOOL_AVERAGE: {
        brush_average_apply(
            dso, selected->dps, selected->pt_index, radius, selected->pc, average_color);
        changed |= true;
        break;
      }
      case DPEN_VERTEX_TOOL_SMEAR: {
        brush_smear_apply(dso, selected->dps, selected->pt_index, selected);
        changed |= true;
        break;
      }
      case DPEN_VERTEX_TOOL_REPLACE: {
        brush_replace_apply(dso, selected->dps, selected->pt_index);
        changed |= true;
        break;
      }

      default:
        printf("ERROR: Unknown type of GPencil Vertex Paint brush\n");
        break;
    }
  }
  /* Clear the selected array, but keep the memory allocation. */
  dso->pbuffer = dpen_select_buffer_ensure(
      dso->pbuffer, &dso->pbuffer_size, &dso->pbuffer_used, true);

  return changed;
}

/* Apply brush effect to all layers. */
static bool dpen_vertexpaint_brush_apply_to_layers(dContext *C, tDPen_BrushVertexpaintData *dpbvd)
{
  ToolSettings *ts = ctx_data_tool_settings(C);
  Depsgraph *depsgraph = ctx_data_ensure_evaluated_depsgraph(C);
  Object *obact = dpbvd->object;
  bool changed = false;

  Object *ob_eval = (Object *)DEG_get_evaluated_id(depsgraph, &obact->id);
  DPenData *dpd = (DPenData *)ob_eval->data;

  /* Find visible strokes, and perform operations on those if hit */
  LISTBASE_FOREACH (DPenLayer *, dpl, &dpd->layers) {
    /* If locked or no active frame, don't do anything. */
    if ((!dune_dpen_layer_is_editable(dpl)) || (dpl->actframe == NULL)) {
      continue;
    }

    /* Calculate transform matrix. */
    float diff_mat[4][4], bound_mat[4][4];
    dune_dpen_layer_transform_matrix_get(depsgraph, obact, dpl, diff_mat);
    copy_m4_m4(bound_mat, diff_mat);
    mul_m4_m4m4(diff_mat, diff_mat, gpl->layer_invmat);

    /* Active Frame or MultiFrame? */
    if (gso->is_multiframe) {
      /* init multiframe falloff options */
      int f_init = 0;
      int f_end = 0;

      if (dso->use_multiframe_falloff) {
        dune_dpen_frame_range_selected(dpl, &f_init, &f_end);
      }

      LISTBASE_FOREACH (DPenFrame *, dpf, &dpl->frames) {
        /* Always do active frame; Otherwise, only include selected frames */
        if ((dpf == dpl->actframe) || (dpf->flag & DPEN_FRAME_SELECT)) {
          /* Compute multi-frame falloff factor. */
          if (dso->use_multiframe_falloff) {
            /* Falloff depends on distance to active frame (relative to the overall frame range) */
            dso->mf_falloff = dune_dpen_multiframe_falloff_calc(
                dpf, dpl->actframe->framenum, f_init, f_end, ts->dpen_sculpt.cur_falloff);
          }
          else {
            /* No falloff */
            dso->mf_falloff = 1.0f;
          }

          /* affect strokes in this frame */
          changed |= gpen_vertexpaint_brush_do_frame(C, gso, gpl, gpf, diff_mat, bound_mat);
        }
      }
    }
    else {
      /* Apply to active frame's strokes */
      if (gpl->actframe != NULL) {
        gso->mf_falloff = 1.0f;
        changed |= gpencil_vertexpaint_brush_do_frame(
            C, gso, gpl, gpl->actframe, diff_mat, bound_mat);
      }
    }
  }

  return changed;
}

/* Calculate settings for applying brush */
static void gpencil_vertexpaint_brush_apply(bContext *C, wmOperator *op, PointerRNA *itemptr)
{
  tGP_BrushVertexpaintData *gso = op->customdata;
  Brush *brush = gso->brush;
  const int radius = ((brush->flag & GP_BRUSH_USE_PRESSURE) ? gso->brush->size * gso->pressure :
                                                              gso->brush->size);
  float mousef[2];
  int mouse[2];
  bool changed = false;

  /* Get latest mouse coordinates */
  RNA_float_get_array(itemptr, "mouse", mousef);
  gso->mval[0] = mouse[0] = (int)(mousef[0]);
  gso->mval[1] = mouse[1] = (int)(mousef[1]);

  gso->pressure = RNA_float_get(itemptr, "pressure");

  if (RNA_boolean_get(itemptr, "pen_flip")) {
    gso->flag |= GP_VERTEX_FLAG_INVERT;
  }
  else {
    gso->flag &= ~GP_VERTEX_FLAG_INVERT;
  }

  /* Store coordinates as reference, if operator just started running */
  if (gso->first) {
    gso->mval_prev[0] = gso->mval[0];
    gso->mval_prev[1] = gso->mval[1];
    gso->pressure_prev = gso->pressure;
  }

  /* Update brush_rect, so that it represents the bounding rectangle of brush. */
  gso->brush_rect.xmin = mouse[0] - radius;
  gso->brush_rect.ymin = mouse[1] - radius;
  gso->brush_rect.xmax = mouse[0] + radius;
  gso->brush_rect.ymax = mouse[1] + radius;

  /* Calc 2D direction vector and relative angle. */
  brush_calc_dvec_2d(gso);

  /* Calc grid for smear tool. */
  gpencil_grid_cells_init(gso);

  changed = gpencil_vertexpaint_brush_apply_to_layers(C, gso);

  /* Updates */
  if (changed) {
    DEG_id_tag_update(&gso->gpd->id, ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  }

  /* Store values for next step */
  gso->mval_prev[0] = gso->mval[0];
  gso->mval_prev[1] = gso->mval[1];
  gso->pressure_prev = gso->pressure;
  gso->first = false;
}

/* Running --------------------------------------------- */

/* helper - a record stroke, and apply paint event */
static void gpencil_vertexpaint_brush_apply_event(bContext *C,
                                                  wmOperator *op,
                                                  const wmEvent *event)
{
  tGP_BrushVertexpaintData *gso = op->customdata;
  PointerRNA itemptr;
  float mouse[2];

  mouse[0] = event->mval[0] + 1;
  mouse[1] = event->mval[1] + 1;

  /* fill in stroke */
  RNA_collection_add(op->ptr, "stroke", &itemptr);

  RNA_float_set_array(&itemptr, "mouse", mouse);
  RNA_bool_set(&itemptr, "pen_flip", event->modifier & KM_CTRL);
  api_bool_set(&itemptr, "is_start", gso->first);

  /* Handle pressure sensitivity (which is supplied by tablets). */
  float pressure = event->tablet.pressure;
  CLAMP(pressure, 0.0f, 1.0f);
  api_float_set(&itemptr, "pressure", pressure);

  /* apply */
  dpen_vertexpaint_brush_apply(C, op, &itemptr);
}

/* reapply */
static int dpen_vertexpaint_brush_exec(bContext *C, wmOperator *op)
{
  if (!dpen_vertexpaint_brush_init(C, op)) {
    return OP_CANCELLED;
  }

  RNA_BEGIN (op->ptr, itemptr, "stroke") {
    dpen_vertexpaint_brush_apply(C, op, &itemptr);
  }
  RNA_END;

  dpen_vertexpaint_brush_exit(C, op);

  return OP_FINISHED;
}

/* start modal painting */
static int dpen_vertexpaint_brush_invoke(dContext *C, wmOperator *op, const wmEvent *event)
{
  tDPen_BrushVertexpaintData *gso = NULL;
  const bool is_modal = api_bool_get(op->ptr, "wait_for_input");
  const bool is_playing = ed_screen_animation_playing(ctx_wm_manager(C)) != NULL;

  /* the operator cannot work while play animation */
  if (is_playing) {
    dune_report(op->reports, RPT_ERROR, "Cannot Paint while play animation");

    return OP_CANCELLED;
  }

  /* init painting data */
  if (!gdpen_vertexpaint_brush_init(C, op)) {
    return OP_CANCELLED;
  }

  dso = op->customdata;

  /* register modal handler */
  wm_event_add_modal_handler(C, op);

  /* start drawing immediately? */
  if (is_modal == false) {
    ARegion *region = ctx_wm_region(C);

    /* apply first dab... */
    dso->is_painting = true;
    dpen_vertexpaint_brush_apply_event(C, op, event);

    /* redraw view with feedback */
    ED_region_tag_redraw(region);
  }

  return OP_RUNNING_MODAL;
}

/* painting - handle events */
static int dpen_vertexpaint_brush_modal(dContext *C, wmOperator *op, const wmEvent *event)
{
  tDPen_BrushVertexpaintData *gso = op->customdata;
  const bool is_modal = api_bool_get(op->ptr, "wait_for_input");
  bool redraw_region = false;
  bool redraw_toolsettings = false;

  /* The operator can be in 2 states: Painting and Idling */
  if (gso->is_painting) {
    /* Painting. */
    switch (event->type) {
      /* Mouse Move = Apply somewhere else */
      case MOUSEMOVE:
      case INBETWEEN_MOUSEMOVE:
        /* apply brush effect at new position */
        dpen_vertexpaint_brush_apply_event(C, op, event);

        /* force redraw, so that the cursor will at least be valid */
        redraw_region = true;
        break;

      /* Painting mbut release = Stop painting (back to idle) */
      case LEFTMOUSE:
        if (is_modal) {
          /* go back to idling... */
          gso->is_painting = false;
        }
        else {
          /* end painting, since we're not modal */
          dso->is_painting = false;

          dpen_vertexpaint_brush_exit(C, op);
          return OP_FINISHED;
        }
        break;

      /* Abort painting if any of the usual things are tried */
      case MIDDLEMOUSE:
      case RIGHTMOUSE:
      case EVT_ESCKEY:
        dpen_vertexpaint_brush_exit(C, op);
        return OP_FINISHED;
    }
  }
  else {
    /* Idling */
    lib_assert(is_modal == true);

    switch (event->type) {
      /* Painting mbut press = Start painting (switch to painting state) */
      case LEFTMOUSE:
        /* do initial "click" apply */
        gso->is_painting = true;
        gso->first = true;

        dpen_vertexpaint_brush_apply_event(C, op, event);
        break;

      /* Exit modal operator, based on the "standard" ops */
      case RIGHTMOUSE:
      case EVT_ESCKEY:
        gpencil_vertexpaint_brush_exit(C, op);
        return OP_FINISHED;

      /* MMB is often used for view manipulations */
      case MIDDLEMOUSE:
        return OP_PASS_THROUGH;

      /* Mouse movements should update the brush cursor - Just redraw the active region */
      case MOUSEMOVE:
      case INBETWEEN_MOUSEMOVE:
        redraw_region = true;
        break;

      /* Change Frame - Allowed */
      case EVT_LEFTARROWKEY:
      case EVT_RIGHTARROWKEY:
      case EVT_UPARROWKEY:
      case EVT_DOWNARROWKEY:
        return OPERATOR_PASS_THROUGH;

      /* Camera/View Gizmo's - Allowed */
      /* (See rationale in dpen_paint.c -> gpencil_draw_modal()) */
      case EVT_PAD0:
      case EVT_PAD1:
      case EVT_PAD2:
      case EVT_PAD3:
      case EVT_PAD4:
      case EVT_PAD5:
      case EVT_PAD6:
      case EVT_PAD7:
      case EVT_PAD8:
      case EVT_PAD9:
        return OP_PASS_THROUGH;

      /* Unhandled event */
      default:
        break;
    }
  }

  /* Redraw region? */
  if (redraw_region) {
    ED_region_tag_redraw(ctx_wm_region(C));
  }

  /* Redraw toolsettings (brush settings)? */
  if (redraw_toolsettings) {
    DEG_id_tag_update(&dso->dpd->id, ID_RECALC_GEOMETRY);
    wm_event_add_notifier(C, NC_SCENE | ND_TOOLSETTINGS, NULL);
  }

  return OP_RUNNING_MODAL;
}

void DPEN_OT_vertex_paint(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Stroke Vertex Paint";
  ot->idname = "DPEN_OT_vertex_paint";
  ot->description = "Paint stroke points with a color";

  /* api callbacks */
  ot->ex = dpen_vertexpaint_brush_ex;
  ot->invoke = dpen_vertexpaint_brush_invoke;
  ot->modal = dpen_vertexpaint_brush_modal;
  ot->cancel = dpen_vertexpaint_brush_exit;
  ot->poll = dpen_vertexpaint_brush_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_BLOCKING;

  /* properties */
  ApiProp *prop;
  prop = api_def_collection_runtime(ot->srna, "stroke", &api_OperatorStrokeElement, "Stroke", "");
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  prop = api_def_bool(ot->srna, "wait_for_input", true, "Wait for Input", "");
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
}
