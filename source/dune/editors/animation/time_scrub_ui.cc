#include "dune_cxt.hh"
#include "dune_scene.h"

#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gpu_state.h"

#include "ed_time_scrub_ui.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ui.hh"
#include "ui_icons.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "types_scene.h"

#include "lib_rect.h"
#include "lib_string.h"
#include "lib_timecode.h"

#include "api_access.hh"
#include "api_prototypes.h"

void ed_time_scrub_rgn_rect_get(const ARgn *rgn, rcti *rect)
{
  rect->xmin = 0;
  rect->xmax = rgn->winx;
  rect->ymax = rgn->winy;
  rect->ymin = rect->ymax - UI_TIME_SCRUB_MARGIN_Y;
}

static int get_centered_txt_y(const rcti *rect)
{
  return lib_rcti_cent_y(rect) - UI_SCALE_FAC * 4;
}

static void drw_background(const rcti *rect)
{
  uint pos = gpu_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

  immUniformThemeColor(TH_TIME_SCRUB_BACKGROUND);

  gpu_blend(GPU_BLEND_ALPHA);

  immRectf(pos, rect->xmin, rect->ymin, rect->xmax, rect->ymax);

  gpu_blend(GPU_BLEND_NONE);

  immUnbindProgram();
}

static void get_current_time_str(
    const Scene *scene, bool display_seconds, int frame, char *r_str, uint str_maxncpy)
{
  if (display_seconds) {
    lib_timecode_string_from_time(r_str, str_maxncpy, 0, FRA2TIME(frame), FPS, U.timecode_style);
  }
  else {
    lib_snprintf(r_str, str_maxncpy, "%d", frame);
  }
}

static void drw_current_frame(const Scene *scene,
                               bool display_seconds,
                               const View2D *v2d,
                               const rcti *scrub_rgn_rect,
                               int current_frame)
{
  const uiFontStyle *fstyle = UI_FSTYLE_WIDGET;
  int frame_x = ui_view2d_view_to_rgn_x(v2d, current_frame);

  char frame_str[64];
  get_current_time_str(scene, display_seconds, current_frame, frame_str, sizeof(frame_str));
  float txt_width = ui_fontstyle_string_width(fstyle, frame_str);
  float box_width = std::max(text_width + 8 * UI_SCALE_FAC, 24 * UI_SCALE_FAC);
  float box_padding = 3 * UI_SCALE_FAC;
  const int line_outline = max_ii(1, round_fl_to_int(1 * UI_SCALE_FAC));

  float bg_color[4];
  ui_GetThemeColorShade4fv(TH_CFRAME, -5, bg_color);

  /* Drw vert line from the bottom of the current frame box to the bottom of the screen. */
  const float subframe_x = ui_view2d_view_to_rgn_x(v2d, dune_scene_ctime_get(scene));
  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  hou_blend(GPU_BLEND_ALPHA);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

  /* Outline. */
  immUniformThemeColorShadeAlpha(TH_BACK, -25, -100);
  immRectf(pos,
           subframe_x - (line_outline + U.pixelsize),
           scrub_rgn_rect->ymax - box_padding,
           subframe_x + (line_outline + U.pixelsize),
           0.0f);

  /* Line. */
  immUniformThemeColor(TH_CFRAME);
  immRectf(pos,
           subframe_x - U.pixelsize,
           scrub_rgn_rect->ymax - box_padding,
           subframe_x + U.pixelsize,
           0.0f);
  immUnbindProgram();
  gpu_blend(GPU_BLEND_NONE);

  ui_drw_roundbox_corner_set(UI_CNR_ALL);

  float outline_color[4];
  ui_GetThemeColorShade4fv(TH_CFRAME, 5, outline_color);

  rctf rect{};
  rect.xmin = frame_x - box_width / 2 + U.pixelsize / 2;
  rect.xmax = frame_x + box_width / 2 + U.pixelsize / 2;
  rect.ymin = scrub_rgn_rect->ymin + box_padding;
  rect.ymax = scrub_rgn_rect->ymax - box_padding;
  ui_drw_roundbox_4fv_ex(
      &rect, bg_color, nullptr, 1.0f, outline_color, U.pixelsize, 4 * UI_SCALE_FAC);

  uchar txt_color[4];
  ui_GetThemeColor4ubv(TH_HEADER_TXT_HI, txt_color);
  ui_fontstyle_drw_simple(fstyle,
                          frame_x - txt_width / 2 + U.pixelsize / 2,
                          get_centered_txt_y(scrub_rgn_rect),
                          frame_str,
                          txt_color);
}

void ed_time_scrub_drw_current_frame(const ARgn *rgn,
                                      const Scene *scene,
                                      bool display_seconds)
{
  const View2D *v2d = &rgn->v2d;
  gpu_matrix_push_projection();
  winOrtho2_rgn_pixelspace(rgn);

  rcti scrub_rgn_rect;
  ed_time_scrub_rgn_rect_get(rgn, &scrub_rgn_rect);

  drw_current_frame(scene, display_seconds, v2d, &scrub_rgn_rect, scene->r.cfra);
  gpu_matrix_pop_projection();
}

void ed_time_scrub_dre(const ARgn *gn,
                        const Scene *scene,
                        bool display_seconds,
                        bool discrete_frames)
{
  const View2D *v2d = &rgn->v2d;

  gpu_matrix_push_projection();
  winOrtho2_rgn_pixelspace(rgn);

  rcti scrub_rgn_rect;
  ed_time_scrub_rgn_rect_get(rgn, &scrub_rgn_rect);

  drw_background(&scrub_rgn_rect);

  rcti numbers_rect = scrub_rgn_rect;
  numbers_rect.ymin = get_centered_txt_y(&scrub_rgn_rect) - 4 * UI_SCALE_FAC;
  if (discrete_frames) {
    ui_view2d_drw_scale_x_discrete_frames_or_seconds(
        rgn, v2d, &numbers_rect, scene, display_seconds, TH_TXT);
  }
  else {
    ui_view2d_drw_scale_x_frames_or_seconds(
        rgn, v2d, &numbers_rect, scene, display_seconds, TH_TXT);
  }

  gpu_matrix_pop_projection();
}

bool ed_time_scrub_ev_in_rgn(const ARgn *rgn, const WinEv *ev)
{
  rcti rect = rgn->winrct;
  rect.ymin = rect.ymax - UI_TIME_SCRUB_MARGIN_Y;
  return lib_rcti_isect_pt_v(&rect, ev->xy);
}

void ed_time_scrub_channel_search_drw(const Cxt *C, ARgn *rgn, DopeSheet *dopesheet)
{
  gpu_matrix_push_projection();
  winOrtho2_rgn_pixelspace(rgn);

  rcti rect;
  rect.xmin = 0;
  rect.xmax = rgn->winx;
  rect.ymin = rgn->winy - UI_TIME_SCRUB_MARGIN_Y;
  rect.ymax = rgn->winy;

  uint pos = gpu_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformThemeColor(TH_BACK);
  immRectf(pos, rect.xmin, rect.ymin, rect.xmax, rect.ymax);
  immUnbindProgram();

  ApiPtr ptr = api_ptr_create(&cxt_win_screen(C)->id, &ApiDopeSheet, dopesheet);

  const uiStyle *style = ui_style_get_dpi();
  const float padding_x = 2 * UI_SCALE_FAC;
  const float padding_y = UI_SCALE_FAC;

  uiBlock *block = ui_block_begin(C, rgn, __func__, UI_EMBOSS);
  uiLayout *layout = ui_block_layout(block,
                                     UI_LAYOUT_VERT,
                                     UI_LAYOUT_HEADER,
                                     rect.xmin + padding_x,
                                     rect.ymin + UI_UNIT_Y + padding_y,
                                     lib_rcti_size_x(&rect) - 2 * padding_x,
                                     1,
                                     0,
                                     style);
  uiLayoutSetScaleY(layout, (UI_UNIT_Y - padding_y) / UI_UNIT_Y);
  ui_block_layout_set_current(block, layout);
  ui_block_align_begin(block);
  uiItemR(layout, &ptr, "filter_text", UI_ITEM_NONE, "", ICON_NONE);
  uiItemR(layout, &ptr, "use_filter_invert", UI_ITEM_NONE, "", ICON_ARROW_LEFTRIGHT);
  ui_block_align_end(block);
  ui_block_layout_resolve(block, nullptr, nullptr);

  /* Make sure the evs are consumed from the search and don't reach other UI blocks since this
   * is drwn on top of animation-channels. */
  ui_block_flag_enable(block, UI_BLOCK_CLIP_EVENTS);
  ui_block_bounds_set_normal(block, 0);
  ui_block_end(C, block);
  ui_block_drw(C, block);

  gpu_matrix_pop_projection();
}
