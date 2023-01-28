#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_listbase.h"
#include "BLI_rect.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_object.h"
#include "BKE_scene.h"
#include "BKE_screen.h"

#include "GPU_immediate.h"
#include "GPU_matrix.h"

#include "ED_screen.h"
#include "ED_transform.h"
#include "ED_transform_snap_object_context.h"
#include "ED_view3d.h"

#include "UI_resources.h"

#include "RNA_access.h"

#include "DEG_depsgraph_query.h"

#include "WM_api.h"

#define STATE_INTERN_GET(state) \
  (SnapStateIntern *)((char *)state - offsetof(SnapStateIntern, snap_state))

typedef struct SnapStateIntern {
  struct SnapStateIntern *next, *prev;
  V3DSnapCursorState snap_state;
} SnapStateIntern;

typedef struct SnapCursorDataIntern {
  V3DSnapCursorState state_default;
  ListBase state_intern;
  V3DSnapCursorData snap_data;

  struct SnapObjectContext *snap_context_v3d;
  const Scene *scene;
  short snap_elem_hidden;

  float prevpoint_stack[3];

  /* Copy of the parameters of the last event state in order to detect updates. */
  struct {
    int x;
    int y;
#ifdef USE_SNAP_DETECT_FROM_KEYMAP_HACK
    uint8_t modifier;
#endif
  } last_eventstate;

#ifdef USE_SNAP_DETECT_FROM_KEYMAP_HACK
  struct wmKeyMap *keymap;
  int snap_on;
#endif

  struct wmPaintCursor *handle;

  bool is_initiated;
} SnapCursorDataIntern;

static SnapCursorDataIntern g_data_intern = {
    .state_default = {.flag = V3D_SNAPCURSOR_SNAP_EDIT_GEOM_FINAL,
                      .snap_elem_force = SCE_SNAP_MODE_GEOM,
                      .plane_axis = 2,
                      .color_point = {255, 255, 255, 255},
                      .color_line = {255, 255, 255, 128},
                      .color_box = {255, 255, 255, 128},
                      .box_dimensions = {1.0f, 1.0f, 1.0f},
                      .draw_point = true}};

/**
 * Dot products below this will be considered view aligned.
 * In this case we can't usefully project the mouse cursor onto the plane.
 */
static const float eps_view_align = 1e-2f;

/**
 * Calculate a 3x3 orientation matrix from the surface under the cursor.
 */
static void v3d_cursor_poject_surface_normal(const float normal[3],
                                             const float obmat[4][4],
                                             float r_mat[3][3])
{
  float mat[3][3];
  copy_m3_m4(mat, obmat);
  normalize_m3(mat);

  float dot_best = fabsf(dot_v3v3(mat[0], normal));
  int i_best = 0;
  for (int i = 1; i < 3; i++) {
    float dot_test = fabsf(dot_v3v3(mat[i], normal));
    if (dot_test > dot_best) {
      i_best = i;
      dot_best = dot_test;
    }
  }
  if (dot_v3v3(mat[i_best], normal) < 0.0f) {
    negate_v3(mat[(i_best + 1) % 3]);
    negate_v3(mat[(i_best + 2) % 3]);
  }
  copy_v3_v3(mat[i_best], normal);
  orthogonalize_m3(mat, i_best);
  normalize_m3(mat);

  copy_v3_v3(r_mat[0], mat[(i_best + 1) % 3]);
  copy_v3_v3(r_mat[1], mat[(i_best + 2) % 3]);
  copy_v3_v3(r_mat[2], mat[i_best]);
}

/**
 * Calculate 3D view incremental (grid) snapping.
 *
 * \note This could be moved to a public function.
 */
static bool v3d_cursor_snap_calc_incremental(
    Scene *scene, View3D *v3d, ARegion *region, const float co_relative[3], float co[3])
{
  const float grid_size = ED_view3d_grid_view_scale(scene, v3d, region, NULL);
  if (UNLIKELY(grid_size == 0.0f)) {
    return false;
  }

  if (scene->toolsettings->snap_flag & SCE_SNAP_ABS_GRID) {
    co_relative = NULL;
  }

  if (co_relative != NULL) {
    sub_v3_v3(co, co_relative);
  }
  mul_v3_fl(co, 1.0f / grid_size);
  co[0] = roundf(co[0]);
  co[1] = roundf(co[1]);
  co[2] = roundf(co[2]);
  mul_v3_fl(co, grid_size);
  if (co_relative != NULL) {
    add_v3_v3(co, co_relative);
  }

  return true;
}

/**
 * Re-order \a mat so \a axis_align uses its own axis which is closest to \a v.
 */
static bool mat3_align_axis_to_v3(float mat[3][3], const int axis_align, const float v[3])
{
  float dot_best = -1.0f;
  int axis_found = axis_align;
  for (int i = 0; i < 3; i++) {
    const float dot_test = fabsf(dot_v3v3(mat[i], v));
    if (dot_test > dot_best) {
      dot_best = dot_test;
      axis_found = i;
    }
  }

  if (axis_align != axis_found) {
    float tmat[3][3];
    copy_m3_m3(tmat, mat);
    const int offset = mod_i(axis_found - axis_align, 3);
    for (int i = 0; i < 3; i++) {
      copy_v3_v3(mat[i], tmat[(i + offset) % 3]);
    }
    return true;
  }
  return false;
}

/* -------------------------------------------------------------------- */
/** \name Drawings
 * \{ */

static void v3d_cursor_plane_draw_grid(const int resolution,
                                       const float scale,
                                       const float scale_fade,
                                       const float matrix[4][4],
                                       const int plane_axis,
                                       const float color[4])
{
  BLI_assert(scale_fade <= scale);
  const int resolution_min = resolution - 1;
  float color_fade[4] = {UNPACK4(color)};
  const float *center = matrix[3];

  GPU_blend(GPU_BLEND_ADDITIVE);
  GPU_line_smooth(true);
  GPU_line_width(1.0f);

  GPUVertFormat *format = immVertexFormat();
  const uint pos_id = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  const uint col_id = GPU_vertformat_attr_add(format, "color", GPU_COMP_F32, 4, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_SMOOTH_COLOR);

  const size_t coords_len = resolution * resolution;
  float(*coords)[3] = MEM_mallocN(sizeof(*coords) * coords_len, __func__);

  const int axis_x = (plane_axis + 0) % 3;
  const int axis_y = (plane_axis + 1) % 3;
  const int axis_z = (plane_axis + 2) % 3;

  int i;
  const float resolution_div = (float)1.0f / (float)resolution;
  i = 0;
  for (int x = 0; x < resolution; x++) {
    const float x_fl = (x * resolution_div) - 0.5f;
    for (int y = 0; y < resolution; y++) {
      const float y_fl = (y * resolution_div) - 0.5f;
      coords[i][axis_x] = 0.0f;
      coords[i][axis_y] = x_fl * scale;
      coords[i][axis_z] = y_fl * scale;
      mul_m4_v3(matrix, coords[i]);
      i += 1;
    }
  }
  BLI_assert(i == (int)coords_len);
  immBeginAtMost(GPU_PRIM_LINES, coords_len * 4);
  i = 0;
  for (int x = 0; x < resolution_min; x++) {
    for (int y = 0; y < resolution_min; y++) {

      /* Add #resolution_div to ensure we fade-out entirely. */
#define FADE(v) \
  max_ff(0.0f, (1.0f - square_f(((len_v3v3(v, center) / scale_fade) + resolution_div) * 2.0f)))

      const float *v0 = coords[(resolution * x) + y];
      const float *v1 = coords[(resolution * (x + 1)) + y];
      const float *v2 = coords[(resolution * x) + (y + 1)];

      const float f0 = FADE(v0);
      const float f1 = FADE(v1);
      const float f2 = FADE(v2);

      if (f0 > 0.0f || f1 > 0.0f) {
        color_fade[3] = color[3] * f0;
        immAttr4fv(col_id, color_fade);
        immVertex3fv(pos_id, v0);
        color_fade[3] = color[3] * f1;
        immAttr4fv(col_id, color_fade);
        immVertex3fv(pos_id, v1);
      }
      if (f0 > 0.0f || f2 > 0.0f) {
        color_fade[3] = color[3] * f0;
        immAttr4fv(col_id, color_fade);
        immVertex3fv(pos_id, v0);

        color_fade[3] = color[3] * f2;
        immAttr4fv(col_id, color_fade);
        immVertex3fv(pos_id, v2);
      }

#undef FADE

      i++;
    }
  }

  MEM_freeN(coords);

  immEnd();

  immUnbindProgram();

  GPU_line_smooth(false);
  GPU_blend(GPU_BLEND_NONE);
}

static void v3d_cursor_plane_draw(const RegionView3D *rv3d,
                                  const int plane_axis,
                                  const float matrix[4][4])
{
  /* Draw */
  float pixel_size;

  if (rv3d->is_persp) {
    float center[3];
    negate_v3_v3(center, rv3d->ofs);
    pixel_size = ED_view3d_pixel_size(rv3d, center);
  }
  else {
    pixel_size = ED_view3d_pixel_size(rv3d, matrix[3]);
  }

  if (pixel_size > FLT_EPSILON) {

    /* Arbitrary, 1.0 is a little too strong though. */
    float color_alpha = 0.75f;
    if (rv3d->is_persp) {
      /* Scale down the alpha when this is drawn very small,
       * since the add shader causes the small size to show too dense & bright. */
      const float relative_pixel_scale = pixel_size / ED_view3d_pixel_size(rv3d, matrix[3]);
      if (relative_pixel_scale < 1.0f) {
        color_alpha *= max_ff(square_f(relative_pixel_scale), 0.3f);
      }
    }

    {
      /* Extra adjustment when it's near view-aligned as it seems overly bright. */
      float view_vector[3];
      ED_view3d_global_to_vector(rv3d, matrix[3], view_vector);
      float view_dot = fabsf(dot_v3v3(matrix[plane_axis], view_vector));
      color_alpha *= max_ff(0.3f, 1.0f - square_f(square_f(1.0f - view_dot)));
    }

    const float scale_mod = U.gizmo_size * 2 * U.dpi_fac / U.pixelsize;

    float final_scale = (scale_mod * pixel_size);

    const int lines_subdiv = 10;
    int lines = lines_subdiv;

    float final_scale_fade = final_scale;
    final_scale = ceil_power_of_10(final_scale);

    float fac = final_scale_fade / final_scale;

    float color[4] = {1, 1, 1, color_alpha};
    color[3] *= square_f(1.0f - fac);
    if (color[3] > 0.0f) {
      v3d_cursor_plane_draw_grid(
          lines * lines_subdiv, final_scale, final_scale_fade, matrix, plane_axis, color);
    }

    color[3] = color_alpha;
    /* When the grid is large, we only need the 2x lines in the middle. */
    if (fac < 0.2f) {
      lines = 1;
      final_scale = final_scale_fade;
    }
    v3d_cursor_plane_draw_grid(lines, final_scale, final_scale_fade, matrix, plane_axis, color);
  }
}

static void cursor_box_draw(const float dimensions[3], uchar color[4])
{
  GPUVertFormat *format = immVertexFormat();
  const uint pos_id = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);

  GPU_blend(GPU_BLEND_ALPHA);
  GPU_line_smooth(true);
  GPU_line_width(1.0f);

  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformColor4ubv(color);
  imm_draw_cube_corners_3d(pos_id, (const float[3]){0.0f, 0.0f, dimensions[2]}, dimensions, 0.15f);
  immUnbindProgram();

  GPU_line_smooth(false);
  GPU_blend(GPU_BLEND_NONE);
}
