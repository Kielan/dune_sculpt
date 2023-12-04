#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gpu_state.h"

#include "lib_math_rotation.h"

#include "dune_cxt.hh"

#include "type_screen.h"
#include "types_userdef.h"

#include "ui.hh"
#include "ui_resources.hh"

#include "transform.hh"
#include "transform_drw_cursors.hh" /* Own include. */

using namespace dune;

enum eArrowDirection {
  UP,
  DOWN,
  LEFT,
  RIGHT,
};

#define ARROW_WIDTH (2.0f * U.pixelsize)
#define DASH_WIDTH (1.0f)
#define DASH_LENGTH (8.0f * DASH_WIDTH * U.pixelsize)

static void drwArrow(const uint pos_id, const enum eArrowDirection dir)
{
  int offset = 5.0f * UI_SCALE_FAC;
  int length = (6.0f * UI_SCALE_FAC) + (4.0f * U.pixelsize);
  int size = (3.0f * UI_SCALE_FAC) + (2.0f * U.pixelsize);

  /* To line up the arrow point nicely, one end has to be extended by half its width. But
   * being on a 45 degree angle, Pythagoras says a movement of `sqrt(2) / 2 * (line width / 2)`. */
  float adjust = (M_SQRT2 * ARROW_WIDTH / 4.0f);

  if (ELEM(dir, LEFT, DOWN)) {
    offset = -offset;
    length = -length;
    size = -size;
    adjust = -adjust;
  }

  immBegin(GPU_PRIM_LINES, 6);

  if (ELEM(dir, LEFT, RIGHT)) {
    immVertex2f(pos_id, offset, 0);
    immVertex2f(pos_id, offset + length, 0);
    immVertex2f(pos_id, offset + length + adjust, adjust);
    immVertex2f(pos_id, offset + length - size, -size);
    immVertex2f(pos_id, offset + length, 0);
    immVertex2f(pos_id, offset + length - size, size);
  }
  else {
    immVertex2f(pos_id, 0, offset);
    immVertex2f(pos_id, 0, offset + length);
    immVertex2f(pos_id, adjust, offset + length + adjust);
    immVertex2f(pos_id, -size, offset + length - size);
    immVertex2f(pos_id, 0, offset + length);
    immVertex2f(pos_id, size, offset + length - size);
  }

  immEnd();
}

bool transform_drw_cursor_poll(Cxt *C)
{
  ARgn *rgn = cxt_win_rgn(C);
  return (rgn && ELEM(rgn->rgntype, RGN_TYPE_WIN, RGN_TYPE_PREVIEW)) ? true : false;
}

void transform_drw_cursor_drw(Cxt * /*C*/, int x, int y, void *customdata)
{
  TransInfo *t = (TransInfo *)customdata;

  if (t->helpline == HLP_NONE) {
    return;
  }

  /* Offset the vals for the area rgn. */
  const float2 offset = {
      float(t->rgn->winrct.xmin),
      float(t->rgn->winrct.ymin),
  };

  float2 cent;
  float2 tmval = t->mval;

  projectFloatViewEx(t, t->center_global, cent, V3D_PROJ_TEST_CLIP_ZERO);

  cent += offset;
  tmval += offset;

  float viewport_size[4];
  gpu_viewport_size_get_f(viewport_size);

  gpu_line_smooth(true);
  gpu_blend(GPU_BLEND_ALPHA);
  const uint pos_id = gpu_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  /* Dashed lines first. */
  if (ELEM(t->helpline, HLP_SPRING, HLP_ANGLE)) {
    gpu_line_width(DASH_WIDTH);
    immBindBuiltinProgram(GPU_SHADER_3D_LINE_DASHED_UNIFORM_COLOR);
    immUniform2f("viewport_size", viewport_size[2], viewport_size[3]);
    immUniform1i("colors_len", 0); /* "simple" mode */
    immUniformThemeColor3(TH_VIEW_OVERLAY);
    immUniform1f("dash_width", DASH_LENGTH);
    immUniform1f("udash_factor", 0.5f);
    immBegin(GPU_PRIM_LINES, 2);
    immVertex2fv(pos_id, cent);
    immVertex2f(pos_id, tmval[0], tmval[1]);
    immEnd();
    immUnbindProgram();
  }

  /* And now, solid lines. */
  immBindBuiltinProgram(GPU_SHADER_3D_POLYLINE_UNIFORM_COLOR);
  immUniformThemeColor3(TH_VIEW_OVERLAY);
  immUniform2fv("viewportSize", &viewport_size[2]);
  immUniform1f("lineWidth", ARROW_WIDTH);

  gpu_matrix_push();
  gpu_matrix_translate_3f(float(x), float(y), 0.0f);

  switch (t->helpline) {
    case HLP_SPRING:
      gpu_matrix_rotate_axis(-RAD2DEGF(atan2f(cent[0] - tmval[0], cent[1] - tmval[1])), 'Z');
      drwArrow(pos_id, UP);
      drwArrow(pos_id, DOWN);
      break;
    case HLP_HARROW:
      drwArrow(pos_id, RIGHT);
      drwArrow(pos_id, LEFT);
      break;
    case HLP_VARROW:
      drwArrow(pos_id, UP);
      drwArrow(pos_id, DOWN);
      break;
    case HLP_CARROW: {
      /* Drw arrow based on direction defined by custom-points. */
      const int *data = static_cast<const int *>(t->mouse.data);
      const float angle = -atan2f(data[2] - data[0], data[3] - data[1]);
      gpu_matrix_rotate_axis(RAD2DEGF(angle), 'Z');
      drwArrow(pos_id, UP);
      drwArrow(pos_id, DOWN);
      break;
    }
    case HLP_ANGLE: {
      gpu_matrix_push();
      float angle = atan2f(tmval[1] - cent[1], tmval[0] - cent[0]);
      gpu_matrix_translate_3f(cosf(angle), sinf(angle), 0);
      gpu_matrix_rotate_axis(RAD2DEGF(angle), 'Z');
      drwArrow(pos_id, DOWN);
      gpu_matrix_pop();
      gpu_matrix_translate_3f(cosf(angle), sinf(angle), 0);
      gpu_matrix_rotate_axis(RAD2DEGF(angle), 'Z');
      drawArrow(pos_id, UP);
      break;
    }
    case HLP_TRACKBALL: {
      uchar col[3], col2[3];
      ui_GetThemeColor3ubv(TH_GRID, col);
      ui_make_axis_color(col, col2, 'X');
      immUniformColor3ubv(col2);
      drwArrow(pos_id, RIGHT);
      drwArrow(pos_id, LEFT);
      ui_make_axis_color(col, col2, 'Y');
      immUniformColor3ubv(col2);
      drwArrow(pos_id, UP);
      drwArrow(pos_id, DOWN);
      break;
    }
    case HLP_NONE:
      break;
  }

  gpu_matrix_pop();
  immUnbindProgram();
  gpu_line_smooth(false);
  gpu_blend(GPU_BLEND_NONE);
}
