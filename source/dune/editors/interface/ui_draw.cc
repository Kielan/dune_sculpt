#include <cmath>
#include <cstring>

#include "types_color.h"
#include "types_curve.h"
#include "types_curveprofile.h"
#include "types_movieclip.h"
#include "types_screen.h"

#include "lib_math_rotation.h"
#include "lib_polyfill_2d.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "mem_guardedalloc.h"

#include "dune_colorband.h"
#include "dune_colortools.h"
#include "dune_curveprofile.h"
#include "dune_node.hh"
#include "dune_tracking.h"

#include "imbuf_colormanagement.h"
#include "imbuf.h"
#include "imbuf_types.h"

#include "BIF_glutil.hh"

#include "font_api.h"

#include "gpu_batch.h"
#include "gpu_batch_presets.h"
#include "gpu_cxt.h"
#include "gpu_immediate.h"
#include "gpu_immediate_util.h"
#include "gpu_matrix.h"
#include "gpu_shader_shared.h"
#include "gpu_state.h"

#include "ui.hh"

/* own include */
#include "ui_intern.hh"

static int roundboxtype = UI_CNR_ALL;

void ui_drw_roundbox_corner_set(int type)
{
  /* Not sure the roundbox fn is the best place to change this
   * if this is undone, it's not that big a deal, only makes curves edges square. */
  roundboxtype = type;
}

#if 0 /* unused */
int ui_drw_roundbox_corner_get()
{
  return roundboxtype;
}
#endif

void ui_drw_roundbox_4fv_ex(const rctf *rect,
                             const float inner1[4],
                             const float inner2[4],
                             float shade_dir,
                             const float outline[4],
                             float outline_width,
                             float rad)
{
  /* WATCH: This is assuming the ModelViewProjectionMatrix is area pixel space.
   * If it has been scaled, then it's no longer valid. */
  uiWidgetBaseParams widget_params{};
  widget_params.recti.xmin = rect->xmin + outline_width;
  widget_params.recti.ymin = rect->ymin + outline_width;
  widget_params.recti.xmax = rect->xmax - outline_width;
  widget_params.recti.ymax = rect->ymax - outline_width;
  widget_params.rect = *rect;
  widget_params.radi = rad;
  widget_params.rad = rad;
  widget_params.round_corners[0] = (roundboxtype & UI_CNR_BOTTOM_LEFT) ? 1.0f : 0.0f;
  widget_params.round_corners[1] = (roundboxtype & UI_CNR_BOTTOM_RIGHT) ? 1.0f : 0.0f;
  widget_params.round_corners[2] = (roundboxtype & UI_CNR_TOP_RIGHT) ? 1.0f : 0.0f;
  widget_params.round_corners[3] = (roundboxtype & UI_CNR_TOP_LEFT) ? 1.0f : 0.0f;
  widget_params.color_inner1[0] = inner1 ? inner1[0] : 0.0f;
  widget_params.color_inner1[1] = inner1 ? inner1[1] : 0.0f;
  widget_params.color_inner1[2] = inner1 ? inner1[2] : 0.0f;
  widget_params.color_inner1[3] = inner1 ? inner1[3] : 0.0f;
  widget_params.color_inner2[0] = inner2 ? inner2[0] : inner1 ? inner1[0] : 0.0f;
  widget_params.color_inner2[1] = inner2 ? inner2[1] : inner1 ? inner1[1] : 0.0f;
  widget_params.color_inner2[2] = inner2 ? inner2[2] : inner1 ? inner1[2] : 0.0f;
  widget_params.color_inner2[3] = inner2 ? inner2[3] : inner1 ? inner1[3] : 0.0f;
  widget_params.color_outline[0] = outline ? outline[0] : inner1 ? inner1[0] : 0.0f;
  widget_params.color_outline[1] = outline ? outline[1] : inner1 ? inner1[1] : 0.0f;
  widget_params.color_outline[2] = outline ? outline[2] : inner1 ? inner1[2] : 0.0f;
  widget_params.color_outline[3] = outline ? outline[3] : inner1 ? inner1[3] : 0.0f;
  widget_params.shade_dir = shade_dir;
  widget_params.alpha_discard = 1.0f;

  GPUBatch *batch = ui_batch_roundbox_widget_get();
  gpu_batch_program_set_builtin(batch, GPU_SHADER_2D_WIDGET_BASE);
  gpu_batch_uniform_4fv_array(batch, "parameters", 11, (const float(*)[4]) & widget_params);
  gpu_blend(GPU_BLEND_ALPHA);
  gpu_batch_drw(batch);
  gpi_blend(GPU_BLEND_NONE);
}

void ui_drw_roundbox_3ub_alpha(
    const rctf *rect, bool filled, float rad, const uchar col[3], uchar alpha)
{
  const float colv[4] = {
      float(col[0]) / 255.0f,
      float(col[1]) / 255.0f,
      float(col[2]) / 255.0f,
      float(alpha) / 255.0f,
  };
  ui_drw_roundbox_4fv_ex(rect, (filled) ? colv : nullptr, nullptr, 1.0f, colv, U.pixelsize, rad);
}

void ui_drw_roundbox_3fv_alpha(
    const rctf *rect, bool filled, float rad, const float col[3], float alpha)
{
  const float colv[4] = {col[0], col[1], col[2], alpha};
  ui_drw_roundbox_4fv_ex(rect, (filled) ? colv : nullptr, nullptr, 1.0f, colv, U.pixelsize, rad);
}

void ui_drw_roundbox_aa(const rctf *rect, bool filled, float rad, const float color[4])
{
  /* this is to emulate prev behavior of semitransparent fills but that's was a side effect
   * of the previous AA method. Better fix the callers. */
  float colv[4] = {color[0], color[1], color[2], color[3]};
  if (filled) {
    colv[3] *= 0.65f;
  }

  ui_drw_roundbox_4fv_ex(rect, (filled) ? colv : nullptr, nullptr, 1.0f, colv, U.pixelsize, rad);
}

void ui_drw_roundbox_4fv(const rctf *rect, bool filled, float rad, const float col[4])
{
  /* Exactly the same as ui_drw_roundbox_aa but does not do the legacy transparency. */
  ui_drw_roundbox_4fv_ex(rect, (filled) ? col : nullptr, nullptr, 1.0f, col, U.pixelsize, rad);
}

void ui_drw_rounded_corners_inverted(const rcti &rect,
                                      const float rad,
                                      const dune::float4 color)
{
  GPUVertFormat *format = immVertexFormat();
  const uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  float vec[4][2] = {
      {0.195, 0.02},
      {0.55, 0.169},
      {0.831, 0.45},
      {0.98, 0.805},
  };
  for (int a = 0; a < 4; a++) {
    mul_v2_fl(vec[a], rad);
  }

  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformColor4fv(color);

  if (roundboxtype & UI_CNR_TOP_LEFT) {
    immBegin(GPU_PRIM_TRI_FAN, 7);
    immVertex2f(pos, rect.xmin, rect.ymax);
    immVertex2f(pos, rect.xmin, rect.ymax - rad);
    for (int a = 0; a < 4; a++) {
      immVertex2f(pos, rect.xmin + vec[a][1], rect.ymax - rad + vec[a][0]);
    }
    immVertex2f(pos, rect.xmin + rad, rect.ymax);
    immEnd();
  }

  if (roundboxtype & UI_CNR_TOP_RIGHT) {
    immBegin(GPU_PRIM_TRI_FAN, 7);
    immVertex2f(pos, rect.xmax, rect.ymax);
    immVertex2f(pos, rect.xmax - rad, rect.ymax);
    for (int a = 0; a < 4; a++) {
      immVertex2f(pos, rect.xmax - rad + vec[a][0], rect.ymax - vec[a][1]);
    }
    immVertex2f(pos, rect.xmax, rect.ymax - rad);
    immEnd();
  }

  if (roundboxtype & UI_CNR_BOTTOM_RIGHT) {
    immBegin(GPU_PRIM_TRI_FAN, 7);
    immVertex2f(pos, rect.xmax, rect.ymin);
    immVertex2f(pos, rect.xmax, rect.ymin + rad);
    for (int a = 0; a < 4; a++) {
      immVertex2f(pos, rect.xmax - vec[a][1], rect.ymin + rad - vec[a][0]);
    }
    immVertex2f(pos, rect.xmax - rad, rect.ymin);
    immEnd();
  }

  if (roundboxtype & UI_CNR_BOTTOM_LEFT) {
    immBegin(GPU_PRIM_TRI_FAN, 7);
    immVertex2f(pos, rect.xmin, rect.ymin);
    immVertex2f(pos, rect.xmin + rad, rect.ymin);
    for (int a = 0; a < 4; a++) {
      immVertex2f(pos, rect.xmin + rad - vec[a][0], rect.ymin + vec[a][1]);
    }
    immVertex2f(pos, rect.xmin, rect.ymin + rad);
    immEnd();
  }

  immUnbindProgram();
}

void ui_drw_txt_underline(int pos_x, int pos_y, int len, int height, const float color[4])
{
  const int ofs_y = 4 * U.pixelsize;

  GPUVertFormat *format = immVertexFormat();
  const uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_I32, 2, GPU_FETCH_INT_TO_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformColor4fv(color);

  immRecti(pos, pos_x, pos_y - ofs_y, pos_x + len, pos_y - ofs_y + (height * U.pixelsize));
  immUnbindProgram();
}
