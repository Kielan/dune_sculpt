#include <stdio.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_userdef.h"

#include "lib_dunelib.h"
#include "lib_linklist_stack.h"
#include "lib_math.h"
#include "lib_rand.h"
#include "lib_utildefines.h"

#include "dune_cxt.h"
#include "dune_global.h"
#include "dune_image.h"
#include "dune_screen.h"
#include "dune_workspace.h"

#include "api_access.h"
#include "api_types.h"

#include "wm_api.h"
#include "wm_message.h"
#include "wm_toolsystem.h"
#include "wm_types.h"

#include "ed_btns.h"
#include "ed_screen.h"
#include "ed_screen_types.h"
#include "ed_space_api.h"
#include "ed_time_scrub_ui.h"

#include "gpu_framebuffer.h"
#include "gpu_immediate.h"
#include "gpu_immediate_util.h"
#include "gpu_matrix.h"
#include "gpu_state.h"

#include "BLF_api.h"

#include "IMB_imbuf_types.h"
#include "IMB_metadata.h"

#include "ui_interface.h"
#include "ui_interface_icons.h"
#include "ui_resources.h"
#include "ui_view2d.h"

#include "screen_intern.h"

enum RegionEmbossSide {
  REGION_EMBOSS_LEFT = (1 << 0),
  REGION_EMBOSS_TOP = (1 << 1),
  REGION_EMBOSS_BOTTOM = (1 << 2),
  REGION_EMBOSS_RIGHT = (1 << 3),
  REGION_EMBOSS_ALL = REGION_EMBOSS_LEFT | REGION_EMBOSS_TOP | REGION_EMBOSS_RIGHT |
                      REGION_EMBOSS_BOTTOM,
};

/* general area and region code */
static void region_draw_emboss(const ARegion *region, const rcti *scirct, int sides)
{
  /* translate scissor rect to region space */
  const rcti rect = {.xmin = scirct->xmin - region->winrct.xmin,
                     .xmax = scirct->xmax - region->winrct.xmin,
                     .ymin = scirct->ymin - region->winrct.ymin,
                     .ymax = scirct->ymax - region->winrct.ymin};

  /* set transp line */
  gpu_blend(GPU_BLEND_ALPHA);

  float color[4] = {0.0f, 0.0f, 0.0f, 0.25f};
  ui_GetThemeColor3fv(TH_EDITOR_OUTLINE, color);

  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
  immUniformColor4fv(color);

  immBeginAtMost(GPU_PRIM_LINES, 8);

  /* right */
  if (sides & REGION_EMBOSS_RIGHT) {
    immVertex2f(pos, rect.xmax, rect.ymax);
    immVertex2f(pos, rect.xmax, rect.ymin);
  }

  /* bottom */
  if (sides & REGION_EMBOSS_BOTTOM) {
    immVertex2f(pos, rect.xmax, rect.ymin);
    immVertex2f(pos, rect.xmin, rect.ymin);
  }

  /* left */
  if (sides & REGION_EMBOSS_LEFT) {
    immVertex2f(pos, rect.xmin, rect.ymin);
    immVertex2f(pos, rect.xmin, rect.ymax);
  }

  /* top */
  if (sides & REGION_EMBOSS_TOP) {
    immVertex2f(pos, rect.xmin, rect.ymax);
    immVertex2f(pos, rect.xmax, rect.ymax);
  }

  immEnd();
  immUnbindProgram();

  gpu_blend(GPU_BLEND_NONE);
}

void ed_region_pixelspace(const ARegion *region)
{
  wmOrtho2_region_pixelspace(region);
  gpu_matrix_identity_set();
}

void ed_region_do_listen(wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  wmNotifier *notifier = params->notifier;

  /* generic notes first */
  switch (notifier->category) {
    case NC_WM:
      if (notifier->data == ND_FILEREAD) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_WINDOW:
      ed_region_tag_redraw(region);
      break;
  }

  if (region->type && region->type->listener) {
    region->type->listener(params);
  }

  LIST_FOREACH (uiList *, list, &region->ui_lists) {
    if (list->type && list->type->listener) {
      list->type->listener(list, params);
    }
  }
}

void ed_area_do_listen(wmSpaceTypeListenerParams *params)
{
  /* no generic notes? */
  if (params->area->type && params->area->type->listener) {
    params->area->type->listener(params);
  }
}

void ed_area_do_refresh(Cxt *C, ScrArea *area)
{
  /* no generic notes? */
  if (area->type && area->type->refresh) {
    area->type->refresh(C, area);
  }
  area->do_refresh = false;
}

/** Corner widget use for quitting fullscreen. **/
static void area_draw_azone_fullscreen(
    short UNUSED(x1), short UNUSED(y1), short x2, short y2, float alpha)
{
  ui_icon_draw_ex(x2 - U.widget_unit,
                  y2 - U.widget_unit,
                  ICON_FULLSCREEN_EXIT,
                  U.inv_dpi_fac,
                  min_ff(alpha, 0.75f),
                  0.0f,
                  NULL,
                  false);
}

/** Corner widgets use for dragging and splitting the view */
static void area_draw_azone(short UNUSED(x1), short UNUSED(y1), short UNUSED(x2), short UNUSED(y2))
{
  /* No drawing needed since all corners are action zone, and visually distinguishable. */
}

/** Edge widgets to show hidden panels such as the toolbar and headers **/
static void draw_azone_arrow(float x1, float y1, float x2, float y2, AZEdge edge)
{
  const float size = 0.2f * U.widget_unit;
  const float l = 1.0f;  /* arrow length */
  const float s = 0.25f; /* arrow thickness */
  const float hl = l / 2.0f;
  const float points[6][2] = {
      {0, -hl}, {l, hl}, {l - s, hl + s}, {0, s + s - hl}, {s - l, hl + s}, {-l, hl}};
  const float center[2] = {(x1 + x2) / 2, (y1 + y2) / 2};

  int axis;
  int sign;
  switch (edge) {
    case AE_BOTTOM_TO_TOPLEFT:
      axis = 0;
      sign = 1;
      break;
    case AE_TOP_TO_BOTTOMRIGHT:
      axis = 0;
      sign = -1;
      break;
    case AE_LEFT_TO_TOPRIGHT:
      axis = 1;
      sign = 1;
      break;
    case AE_RIGHT_TO_TOPLEFT:
      axis = 1;
      sign = -1;
      break;
    default:
    lib_assert(0);
      return;
  }

  GPUVertFormat *format = immVertexFormat();
  uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  gpu_blend(GPU_BLEND_ALPHA);
  /* NOTE: There is something strange going on with Mesa and GPU_SHADER_2D_UNIFORM_COLOR
   * that causes a crash on some GPUs (see T76113). Using 3D variant avoid the issue. */
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformColor4f(0.8f, 0.8f, 0.8f, 0.4f);

  immBegin(GPU_PRIM_TRI_FAN, 6);
  for (int i = 0; i < 6; i++) {
    if (axis == 0) {
      immVertex2f(pos, center[0] + points[i][0] * size, center[1] + points[i][1] * sign * size);
    }
    else {
      immVertex2f(pos, center[0] + points[i][1] * sign * size, center[1] + points[i][0] * size);
    }
  }
  immEnd();

  immUnbindProgram();
  gpu_blend(GPU_BLEND_NONE);
}

static void region_draw_azone_tab_arrow(ScrArea *area, ARegion *region, AZone *az)
{
  gpu_blend(GPU_BLEND_ALPHA);

  /* add code to draw region hidden as 'too small' */
  switch (az->edge) {
    case AE_TOP_TO_BOTTOMRIGHT:
      ui_draw_roundbox_corner_set(UI_CNR_TOP_LEFT | UI_CNR_TOP_RIGHT);
      break;
    case AE_BOTTOM_TO_TOPLEFT:
      ui_draw_roundbox_corner_set(UI_CNR_BOTTOM_RIGHT | UI_CNR_BOTTOM_LEFT);
      break;
    case AE_LEFT_TO_TOPRIGHT:
      ui_draw_roundbox_corner_set(UI_CNR_TOP_LEFT | UI_CNR_BOTTOM_LEFT);
      break;
    case AE_RIGHT_TO_TOPLEFT:
      ui_draw_roundbox_corner_set(UI_CNR_TOP_RIGHT | UI_CNR_BOTTOM_RIGHT);
      break;
  }

  /* Workaround for different color spaces between normal areas and the ones using GPUViewports. */
  float alpha = wm_region_use_viewport(area, region) ? 0.6f : 0.4f;
  const float color[4] = {0.05f, 0.05f, 0.05f, alpha};
  ui_draw_roundbox_aa(
      &(const rctf){
          .xmin = (float)az->x1,
          .xmax = (float)az->x2,
          .ymin = (float)az->y1,
          .ymax = (float)az->y2,
      },
      true,
      4.0f,
      color);

  draw_azone_arrow((float)az->x1, (float)az->y1, (float)az->x2, (float)az->y2, az->edge);
}

static void area_azone_tag_update(ScrArea *area)
{
  area->flag |= AREA_FLAG_ACTIONZONES_UPDATE;
}

static void region_draw_azones(ScrArea *area, ARegion *region)
{
  if (!area) {
    return;
  }

  gpu_line_width(1.0f);
  gpu_blend(GPU_BLEND_ALPHA);

  gpu_matrix_push();
  gpu_matrix_translate_2f(-region->winrct.xmin, -region->winrct.ymin);

  LIST_FOREACH (AZone *, az, &area->actionzones) {
    /* test if action zone is over this region */
    rcti azrct;
    lib_rcti_init(&azrct, az->x1, az->x2, az->y1, az->y2);

    if (lib_rcti_isect(&region->drawrct, &azrct, NULL)) {
      if (az->type == AZONE_AREA) {
        area_draw_azone(az->x1, az->y1, az->x2, az->y2);
      }
      else if (az->type == AZONE_REGION) {
        if (az->region) {
          /* only display tab or icons when the region is hidden */
          if (az->region->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL)) {
            region_draw_azone_tab_arrow(area, region, az);
          }
        }
      }
      else if (az->type == AZONE_FULLSCREEN) {
        if (az->alpha > 0.0f) {
          area_draw_azone_fullscreen(az->x1, az->y1, az->x2, az->y2, az->alpha);
        }
      }
    }
    if (!IS_EQF(az->alpha, 0.0f) && ELEM(az->type, AZONE_FULLSCREEN, AZONE_REGION_SCROLL)) {
      area_azone_tag_update(area);
    }
  }

  gpu_matrix_pop();

  gpu_blend(GPU_BLEND_NONE);
}

static void region_draw_status_text(ScrArea *area, ARegion *region)
{
  bool overlap = ed_region_is_overlap(area->spacetype, region->regiontype);

  if (overlap) {
    gpu_clear_color(0.0f, 0.0f, 0.0f, 0.0f);
  }
  else {
    ui_ThemeClearColor(TH_HEADER);
  }

  int fontid = font_set_default();

  const float width = font_width(fontid, region->headerstr, BLF_DRAW_STR_DUMMY_MAX);
  const float x = UI_UNIT_X;
  const float y = 0.4f * UI_UNIT_Y;

  if (overlap) {
    const float pad = 2.0f * UI_DPI_FAC;
    const float x1 = x - (UI_UNIT_X - pad);
    const float x2 = x + width + (UI_UNIT_X - pad);
    const float y1 = pad;
    const float y2 = region->winy - pad;

    gpu_blend(GPU_BLEND_ALPHA);

    float color[4] = {0.0f, 0.0f, 0.0f, 0.5f};
    ui_GetThemeColor3fv(TH_BACK, color);
    ui_draw_roundbox_corner_set(UI_CNR_ALL);
    ui_draw_roundbox_aa(
        &(const rctf){
            .xmin = x1,
            .xmax = x2,
            .ymin = y1,
            .ymax = y2,
        },
        true,
        4.0f,
        color);

    ui_FontThemeColor(fontid, TH_TEXT);
  }
  else {
    ui_FontThemeColor(fontid, TH_TEXT);
  }

  font_position(fontid, x, y, 0.0f);
  font_draw(fontid, region->headerstr, BLF_DRAW_STR_DUMMY_MAX);
}

void ed_region_do_msg_notify_tag_redraw(
    /* Follow wmMsgNotifyFn spec */
    Cxt *UNUSED(C),
    wmMsgSubscribeKey *UNUSED(msg_key),
    wmMsgSubscribeValue *msg_val)
{
  ARegion *region = msg_val->owner;
  ed_region_tag_redraw(region);

  /* This avoids _many_ situations where header/properties control display settings.
   * the common case is space properties in the header */
  if (ELEM(region->regiontype, RGN_TYPE_HEADER, RGN_TYPE_TOOL_HEADER, RGN_TYPE_UI)) {
    while (region && region->prev) {
      region = region->prev;
    }
    for (; region; region = region->next) {
      if (ELEM(region->regiontype, RGN_TYPE_WINDOW, RGN_TYPE_CHANNELS)) {
        ed_region_tag_redraw(region);
      }
    }
  }
}

void ed_area_do_msg_notify_tag_refresh(
    /* Follow wmMsgNotifyFn spec */
    duneCxt *UNUSED(C),
    wmMsgSubscribeKey *UNUSED(msg_key),
    wmMsgSubscribeValue *msg_val)
{
  ScrArea *area = msg_val->user_data;
  ed_area_tag_refresh(area);
}

void ed_area_do_mgs_subscribe_for_tool_header(const wmRegionMessageSubscribeParams *params)
{
  struct wmMsgBus *mbus = params->message_bus;
  WorkSpace *workspace = params->workspace;
  ARegion *region = params->region;

  lib_assert(region->regiontype == RGN_TYPE_TOOL_HEADER);
  wmMsgSubscribeValue msg_sub_value_region_tag_redraw = {
      .owner = region,
      .user_data = region,
      .notify = ed_region_do_msg_notify_tag_redraw,
  };
  wm_msg_subscribe_rna_prop(
      mbus, &workspace->id, workspace, WorkSpace, tools, &msg_sub_value_region_tag_redraw);
}

void ed_area_do_mgs_subscribe_for_tool_ui(const wmRegionMessageSubscribeParams *params)
{
  struct wmMsgBus *mbus = params->message_bus;
  WorkSpace *workspace = params->workspace;
  ARegion *region = params->region;

  lib_assert(region->regiontype == RGN_TYPE_UI);
  const char *panel_category_tool = "Tool";
  const char *category = ui_panel_category_active_get(region, false);

  bool update_region = false;
  if (category && STREQ(category, panel_category_tool)) {
    update_region = true;
  }
  else {
    /* Check if a tool category panel is pinned and visible in another category. */
    LIST_FOREACH (Panel *, panel, &region->panels) {
      if (ui_panel_is_active(panel) && panel->flag & PNL_PIN &&
          STREQ(panel->type->category, panel_category_tool)) {
        update_region = true;
        break;
      }
    }
  }

  if (update_region) {
    wmMsgSubscribeValue msg_sub_value_region_tag_redraw = {
        .owner = region,
        .user_data = region,
        .notify = ed_region_do_msg_notify_tag_redraw,
    };
    wm_msg_subscribe_api_prop(
        mbus, &workspace->id, workspace, WorkSpace, tools, &msg_sub_value_region_tag_redraw);
  }
}

/* Although there's no general support for minimizing areas, the status-bar can
 * be snapped to be only a few pixels high. A few pixels rather than 0 so it
 * can be un-minimized again. We consider it pseudo-minimized and don't draw
 * it then. */
static bool area_is_pseudo_minimized(const ScrArea *area)
{
  return (area->winx < 3) || (area->winy < 3);
}

void ed_region_do_layout(Cxt *C, ARegion *region)
{
  /* This is optional, only needed for dynamically sized regions. */
  ScrArea *area = cxt_wm_area(C);
  ARegionType *at = region->type;

  if (!at->layout) {
    return;
  }

  if (at->do_lock || (area && area_is_pseudo_minimized(area))) {
    return;
  }

  region->do_draw |= RGN_DRAWING;

  ui_SetTheme(area ? area->spacetype : 0, at->regionid);
  at->layout(C, region);

  /* Clear temporary update flag. */
  region->flag &= ~RGN_FLAG_SEARCH_FILTER_UPDATE;
}

void ed_region_do_draw(Cxt *C, ARegion *region)
{
  Window *win = cxt_wm_window(C);
  ScrArea *area = cxt_wm_area(C);
  ARegionType *at = region->type;

  /* see dune_spacedata_draw_locks() */
  if (at->do_lock) {
    return;
  }

  region->do_draw |= RGN_DRAWING;

  /* Set viewport, scissor, ortho and region->drawrct. */
  wmPartialViewport(&region->drawrct, &region->winrct, &region->drawrct);

  wmOrtho2_region_pixelspace(region);

  ui_SetTheme(area ? area->spacetype : 0, at->regionid);

  if (area && area_is_pseudo_minimized(area)) {
    ui_ThemeClearColor(TH_EDITOR_OUTLINE);
    return;
  }
  /* optional header info instead? */
  if (region->headerstr) {
    region_draw_status_text(area, region);
  }
  else if (at->draw) {
    at->draw(C, region);
  }

  /* test: add convention to end regions always in pixel space,
   * for drawing of borders/gestures etc */
  ed_region_pixelspace(region);

  /* Remove sRGB override by rebinding the framebuffer. */
  GPUFrameBuffer *fb = gpu_framebuffer_active_get();
  gpu_framebuffer_bind(fb);

  ed_region_draw_cb_draw(C, region, REGION_DRAW_POST_PIXEL);

  region_draw_azones(area, region);

  /* for debugging unneeded area redraws and partial redraw */
  if (G.debug_value == 888) {
    gpu_blend(GPU_BLEND_ALPHA);
    GPUVertFormat *format = immVertexFormat();
    uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
    immUniformColor4f(BLI_thread_frand(0), BLI_thread_frand(0), BLI_thread_frand(0), 0.1f);
    immRectf(pos,
             region->drawrct.xmin - region->winrct.xmin,
             region->drawrct.ymin - region->winrct.ymin,
             region->drawrct.xmax - region->winrct.xmin,
             region->drawrct.ymax - region->winrct.ymin);
    immUnbindProgram();
    gpu_blend(GPU_BLEND_NONE);
  }

  memset(&region->drawrct, 0, sizeof(region->drawrct));

  ui_blocklist_free_inactive(C, region);

  if (area) {
    const Screen *screen = wm_window_get_active_screen(win);

    /* Only region emboss for top-bar */
    if ((screen->state != SCREENFULL) && ed_area_is_global(area)) {
      region_draw_emboss(region, &region->winrct, (REGION_EMBOSS_LEFT | REGION_EMBOSS_RIGHT));
    }
    else if ((region->regiontype == RGN_TYPE_WINDOW) && (region->alignment == RGN_ALIGN_QSPLIT)) {

      /* draw separating lines between the quad views */

      float color[4] = {0.0f, 0.0f, 0.0f, 0.8f};
      ui_GetThemeColor3fv(TH_EDITOR_OUTLINE, color);
      GPUVertFormat *format = immVertexFormat();
      uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
      immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
      immUniformColor4fv(color);
      gpu_line_width(1.0f);
      imm_draw_box_wire_2d(pos,
                           0,
                           0,
                           region->winrct.xmax - region->winrct.xmin + 1,
                           region->winrct.ymax - region->winrct.ymin + 1);
      immUnbindProgram();
    }
  }

  /* We may want to detach message-subscriptions from drawing. */
  {
    WorkSpace *workspace = cxt_wm_workspace(C);
    WindowManager *wm = cxt_wm_manager(C);
    Screen *screen = cxt_window_get_active_screen(win);
    Scene *scene = cxt_data_scene(C);
    struct wmMsgBus *mbus = wm->message_bus;
    wm_msgbus_clear_by_owner(mbus, region);

    /* Cheat, always subscribe to this space type props.
     * This covers most cases and avoids copy-paste similar code for each space type. */
    if (ELEM(
            region->regiontype, RGN_TYPE_WINDOW, RGN_TYPE_CHANNELS, RGN_TYPE_UI, RGN_TYPE_TOOLS)) {
      SpaceLink *sl = area->spacedata.first;

      ApiPtr ptr;
      api_ptr_create(&screen->id, &API_Space, sl, &ptr);

      wmMsgSubscribeValue msg_sub_value_region_tag_redraw = {
          .owner = region,
          .user_data = region,
          .notify = ed_region_do_msg_notify_tag_redraw,
      };
      /* All props for this space type. */
      wm_msg_subscribe_api(mbus, &ptr, NULL, &msg_sub_value_region_tag_redraw, __func__);
    }

    wmRegionMessageSubscribeParams message_subscribe_params = {
        .cxt = C,
        .message_bus = mbus,
        .workspace = workspace,
        .scene = scene,
        .screen = screen,
        .area = area,
        .region = region,
    };
    ed_region_message_subscribe(&message_subscribe_params);
  }
}

/* **********************************
 * maybe silly, but let's try for now
 * to keep these tags protected
 * ********************************** */

void ed_region_tag_redraw(ARegion *region)
{
  /* don't tag redraw while drawing, it shouldn't happen normally
   * but python scripts can cause this to happen indirectly */
  if (region && !(region->do_draw & RGN_DRAWING)) {
    /* zero region means full region redraw */
    region->do_draw &= ~(RGN_DRAW_PARTIAL | RGN_DRAW_NO_REBUILD | RGN_DRAW_EDITOR_OVERLAYS);
    region->do_draw |= RGN_DRAW;
    memset(&region->drawrct, 0, sizeof(region->drawrct));
  }
}

void ed_region_tag_redraw_cursor(ARegion *region)
{
  if (region) {
    region->do_draw_paintcursor = RGN_DRAW;
  }
}

void ed_region_tag_redraw_no_rebuild(ARegion *region)
{
  if (region && !(region->do_draw & (RGN_DRAWING | RGN_DRAW))) {
    region->do_draw &= ~(RGN_DRAW_PARTIAL | RGN_DRAW_EDITOR_OVERLAYS);
    region->do_draw |= RGN_DRAW_NO_REBUILD;
    memset(&region->drawrct, 0, sizeof(region->drawrct));
  }
}

void ed_region_tag_refresh_ui(ARegion *region)
{
  if (region) {
    region->do_draw |= RGN_REFRESH_UI;
  }
}

void ed_region_tag_redraw_editor_overlays(struct ARegion *region)
{
  if (region && !(region->do_draw & (RGN_DRAWING | RGN_DRAW))) {
    if (region->do_draw & RGN_DRAW_PARTIAL) {
      ed_region_tag_redraw(region);
    }
    else {
      region->do_draw |= RGN_DRAW_EDITOR_OVERLAYS;
    }
  }
}

void ed_region_tag_redraw_partial(ARegion *region, const rcti *rct, bool rebuild)
{
  if (region && !(region->do_draw & RGN_DRAWING)) {
    if (region->do_draw & RGN_DRAW_PARTIAL) {
      /* Partial redraw already set, expand region. */
      lib_rcti_union(&region->drawrct, rct);
      if (rebuild) {
        region->do_draw &= ~RGN_DRAW_NO_REBUILD;
      }
    }
    else if (region->do_draw & (RGN_DRAW | RGN_DRAW_NO_REBUILD)) {
      /* Full redraw already requested. */
      if (rebuild) {
        region->do_draw &= ~RGN_DRAW_NO_REBUILD;
      }
    }
    else {
      /* No redraw set yet, set partial region. */
      region->drawrct = *rct;
      region->do_draw |= RGN_DRAW_PARTIAL;
      if (!rebuild) {
        region->do_draw |= RGN_DRAW_NO_REBUILD;
      }
    }
  }
}

void ed_area_tag_redraw(ScrArea *area)
{
  if (area) {
    LIST_FOREACH (ARegion *, region, &area->regionbase) {
      ed_region_tag_redraw(region);
    }
  }
}

void ed_area_tag_redraw_no_rebuild(ScrArea *area)
{
  if (area) {
    LIST_FOREACH (ARegion *, region, &area->regionbase) {
      ed_region_tag_redraw_no_rebuild(region);
    }
  }
}

void ed_area_tag_redraw_regiontype(ScrArea *area, int regiontype)
{
  if (area) {
    LIST_FOREACH (ARegion *, region, &area->regionbase) {
      if (region->regiontype == regiontype) {
        ed_region_tag_redraw(region);
      }
    }
  }
}

void ed_area_tag_refresh(ScrArea *area)
{
  if (area) {
    area->do_refresh = true;
  }
}

/* *************************************************************** */

const char *ed_area_region_search_filter_get(const ScrArea *area, const ARegion *region)
{
  /* Only the properties editor has a search string for now. */
  if (area->spacetype == SPACE_PROPS) {
    SpaceProps *sbtns = area->spacedata.first;
    if (region->regiontype == RGN_TYPE_WINDOW) {
      return ed_btns_search_string_get(sbuts);
    }
  }

  return NULL;
}

void ed_region_search_filter_update(const ScrArea *area, ARegion *region)
{
  region->flag |= RGN_FLAG_SEARCH_FILTER_UPDATE;

  const char *search_filter = ed_area_region_search_filter_get(area, region);
  SET_FLAG_FROM_TEST(region->flag,
                     region->regiontype == RGN_TYPE_WINDOW && search_filter[0] != '\0',
                     RGN_FLAG_SEARCH_FILTER_ACTIVE);
}

/* *************************************************************** */

void ed_area_status_text(ScrArea *area, const char *str)
{
  /* happens when running transform operators in background mode */
  if (area == NULL) {
    return;
  }

  LIST_FOREACH (ARegion *, region, &area->regionbase) {
    if (region->regiontype == RGN_TYPE_HEADER) {
      if (str) {
        if (region->headerstr == NULL) {
          region->headerstr = mem_mallocn(UI_MAX_DRAW_STR, "headerprint");
        }
        lib_strncpy(region->headerstr, str, UI_MAX_DRAW_STR);
        lib_str_rstrip(region->headerstr);
      }
      else {
        MEM_SAFE_FREE(region->headerstr);
      }
      ed_region_tag_redraw(region);
    }
  }
}

void ed_workspace_status_text(Cxt *C, const char *str)
{
  Window *win = cxt_wm_window(C);
  WorkSpace *workspace = cxt_wm_workspace(C);

  /* Can be NULL when running operators in background mode. */
  if (workspace == NULL) {
    return;
  }

  if (str) {
    if (workspace->status_text == NULL) {
      workspace->status_text = mem_mallocn(UI_MAX_DRAW_STR, "headerprint");
    }
    lib_strncpy(workspace->status_text, str, UI_MAX_DRAW_STR);
  }
  else {
    MEM_SAFE_FREE(workspace->status_text);
  }

  /* Redraw status bar. */
  LIST_FOREACH (ScrArea *, area, &win->global_areas.areabase) {
    if (area->spacetype == SPACE_STATUSBAR) {
      ed_area_tag_redraw(area);
      break;
    }
  }
}

/* ************************************************************ */

static void area_azone_init(wmWindow *win, const Screen *screen, ScrArea *area)
{
  /* reinitialize entirely, regions and fullscreen add azones too */
  lib_freelistn(&area->actionzones);

  if (screen->state != SCREENNORMAL) {
    return;
  }

  if (U.app_flag & USER_APP_LOCK_CORNER_SPLIT) {
    return;
  }

  if (ed_area_is_global(area)) {
    return;
  }

  if (screen->temp) {
    return;
  }

  const float coords[4][4] = {
      /* Bottom-left. */
      {area->totrct.xmin - U.pixelsize,
       area->totrct.ymin - U.pixelsize,
       area->totrct.xmin + AZONESPOTW,
       area->totrct.ymin + AZONESPOTH},
      /* Bottom-right. */
      {area->totrct.xmax - AZONESPOTW,
       area->totrct.ymin - U.pixelsize,
       area->totrct.xmax + U.pixelsize,
       area->totrct.ymin + AZONESPOTH},
      /* Top-left. */
      {area->totrct.xmin - U.pixelsize,
       area->totrct.ymax - AZONESPOTH,
       area->totrct.xmin + AZONESPOTW,
       area->totrct.ymax + U.pixelsize},
      /* Top-right. */
      {area->totrct.xmax - AZONESPOTW,
       area->totrct.ymax - AZONESPOTH,
       area->totrct.xmax + U.pixelsize,
       area->totrct.ymax + U.pixelsize},
  };

  for (int i = 0; i < 4; i++) {
    /* can't click on bottom corners on OS X, already used for resizing */
#ifdef __APPLE__
    if (!wm_window_is_fullscreen(win) &&
        ((coords[i][0] == 0 && coords[i][1] == 0) ||
         (coords[i][0] == WM_window_pixels_x(win) && coords[i][1] == 0))) {
      continue;
    }
#else
    (void)win;
#endif

    /* set area action zones */
    AZone *az = (AZone *)mem_callocn(sizeof(AZone), "actionzone");
    lib_addtail(&(area->actionzones), az);
    az->type = AZONE_AREA;
    az->x1 = coords[i][0];
    az->y1 = coords[i][1];
    az->x2 = coords[i][2];
    az->y2 = coords[i][3];
    LIB_rcti_init(&az->rect, az->x1, az->x2, az->y1, az->y2);
  }
}

static void fullscreen_azone_init(ScrArea *area, ARegion *region)
{
  if (ed_area_is_global(area) || (region->regiontype != RGN_TYPE_WINDOW)) {
    return;
  }

  AZone *az = (AZone *)mem_callocn(sizeof(AZone), "fullscreen action zone");
  lib_addtail(&(area->actionzones), az);
  az->type = AZONE_FULLSCREEN;
  az->region = region;
  az->alpha = 0.0f;

  if (U.uiflag2 & USER_REGION_OVERLAP) {
    const rcti *rect_visible = ed_region_visible_rect(region);
    az->x2 = region->winrct.xmin + rect_visible->xmax;
    az->y2 = region->winrct.ymin + rect_visible->ymax;
  }
  else {
    az->x2 = region->winrct.xmax;
    az->y2 = region->winrct.ymax;
  }
  az->x1 = az->x2 - AZONEFADEOUT;
  az->y1 = az->y2 - AZONEFADEOUT;

  lib_rcti_init(&az->rect, az->x1, az->x2, az->y1, az->y2);
}

#define AZONEPAD_EDGE (0.1f * U.widget_unit)
#define AZONEPAD_ICON (0.45f * U.widget_unit)
static void region_azone_edge(AZone *az, ARegion *region)
{
  /* If region is overlapped (transparent background), move AZone to content.
   * Note this is an arbitrary amount that matches nicely with numbers elsewhere. */
  int overlap_padding = (region->overlap) ? (int)(0.4f * U.widget_unit) : 0;

  switch (az->edge) {
    case AE_TOP_TO_BOTTOMRIGHT:
      az->x1 = region->winrct.xmin;
      az->y1 = region->winrct.ymax - AZONEPAD_EDGE - overlap_padding;
      az->x2 = region->winrct.xmax;
      az->y2 = region->winrct.ymax + AZONEPAD_EDGE - overlap_padding;
      break;
    case AE_BOTTOM_TO_TOPLEFT:
      az->x1 = region->winrct.xmin;
      az->y1 = region->winrct.ymin + AZONEPAD_EDGE + overlap_padding;
      az->x2 = region->winrct.xmax;
      az->y2 = region->winrct.ymin - AZONEPAD_EDGE + overlap_padding;
      break;
    case AE_LEFT_TO_TOPRIGHT:
      az->x1 = region->winrct.xmin - AZONEPAD_EDGE + overlap_padding;
      az->y1 = region->winrct.ymin;
      az->x2 = region->winrct.xmin + AZONEPAD_EDGE + overlap_padding;
      az->y2 = region->winrct.ymax;
      break;
    case AE_RIGHT_TO_TOPLEFT:
      az->x1 = region->winrct.xmax + AZONEPAD_EDGE - overlap_padding;
      az->y1 = region->winrct.ymin;
      az->x2 = region->winrct.xmax - AZONEPAD_EDGE - overlap_padding;
      az->y2 = region->winrct.ymax;
      break;
  }
  lib_rcti_init(&az->rect, az->x1, az->x2, az->y1, az->y2);
}

/* region already made zero sized, in shape of edge */
static void region_azone_tab_plus(ScrArea *area, AZone *az, ARegion *region)
{
  float edge_offset = 1.0f;
  const float tab_size_x = 0.7f * U.widget_unit;
  const float tab_size_y = 0.4f * U.widget_unit;

  int tot = 0;
  LIST_FOREACH (AZone *, azt, &area->actionzones) {
    if (azt->edge == az->edge) {
      tot++;
    }
  }

  switch (az->edge) {
    case AE_TOP_TO_BOTTOMRIGHT: {
      int add = (region->winrct.ymax == area->totrct.ymin) ? 1 : 0;
      az->x1 = region->winrct.xmax - ((edge_offset + 1.0f) * tab_size_x);
      az->y1 = region->winrct.ymax - add;
      az->x2 = region->winrct.xmax - (edge_offset * tab_size_x);
      az->y2 = region->winrct.ymax - add + tab_size_y;
      break;
    }
    case AE_BOTTOM_TO_TOPLEFT:
      az->x1 = region->winrct.xmax - ((edge_offset + 1.0f) * tab_size_x);
      az->y1 = region->winrct.ymin - tab_size_y;
      az->x2 = region->winrct.xmax - (edge_offset * tab_size_x);
      az->y2 = region->winrct.ymin;
      break;
    case AE_LEFT_TO_TOPRIGHT:
      az->x1 = region->winrct.xmin - tab_size_y;
      az->y1 = region->winrct.ymax - ((edge_offset + 1.0f) * tab_size_x);
      az->x2 = region->winrct.xmin;
      az->y2 = region->winrct.ymax - (edge_offset * tab_size_x);
      break;
    case AE_RIGHT_TO_TOPLEFT:
      az->x1 = region->winrct.xmax;
      az->y1 = region->winrct.ymax - ((edge_offset + 1.0f) * tab_size_x);
      az->x2 = region->winrct.xmax + tab_size_y;
      az->y2 = region->winrct.ymax - (edge_offset * tab_size_x);
      break;
  }
  /* rect needed for mouse pointer test */
  lib_rcti_init(&az->rect, az->x1, az->x2, az->y1, az->y2);
}

static bool region_azone_edge_poll(const ARegion *region, const bool is_fullscreen)
{
  const bool is_hidden = (region->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL));

  if (is_hidden && is_fullscreen) {
    return false;
  }
  if (!is_hidden && ELEM(region->regiontype, RGN_TYPE_HEADER, RGN_TYPE_TOOL_HEADER)) {
    return false;
  }

  if (is_hidden && (U.app_flag & USER_APP_HIDE_REGION_TOGGLE)) {
    return false;
  }

  if (!is_hidden && (U.app_flag & USER_APP_LOCK_EDGE_RESIZE)) {
    return false;
  }

  return true;
}

static void region_azone_edge_init(ScrArea *area,
                                   ARegion *region,
                                   AZEdge edge,
                                   const bool is_fullscreen)
{
  const bool is_hidden = (region->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL));

  if (!region_azone_edge_poll(region, is_fullscreen)) {
    return;
  }

  AZone *az = (AZone *)MEM_callocN(sizeof(AZone), "actionzone");
  LIB_addtail(&(area->actionzones), az);
  az->type = AZONE_REGION;
  az->region = region;
  az->edge = edge;

  if (is_hidden) {
    region_azone_tab_plus(area, az, region);
  }
  else {
    region_azone_edge(az, region);
  }
}


static void region_azone_scrollbar_init(ScrArea *area,
                                        ARegion *region,
                                        AZScrollDirection direction)
{
  rcti scroller_vert = (direction == AZ_SCROLL_VERT) ? region->v2d.vert : region->v2d.hor;
  AZone *az = MEM_callocN(sizeof(*az), __func__);

  LIB_addtail(&area->actionzones, az);
  az->type = AZONE_REGION_SCROLL;
  az->region = region;
  az->direction = direction;

  if (direction == AZ_SCROLL_VERT) {
    az->region->v2d.alpha_vert = 0;
  }
  else if (direction == AZ_SCROLL_HOR) {
    az->region->v2d.alpha_hor = 0;
  }

  LIB_rcti_translate(&scroller_vert, region->winrct.xmin, region->winrct.ymin);
  az->x1 = scroller_vert.xmin - AZONEFADEIN;
  az->y1 = scroller_vert.ymin - AZONEFADEIN;
  az->x2 = scroller_vert.xmax + AZONEFADEIN;
  az->y2 = scroller_vert.ymax + AZONEFADEIN;

  LIB_rcti_init(&az->rect, az->x1, az->x2, az->y1, az->y2);
}

static void region_azones_scrollbars_init(ScrArea *area, ARegion *region)
{
  const View2D *v2d = &region->v2d;

  if ((v2d->scroll & V2D_SCROLL_VERTICAL) && ((v2d->scroll & V2D_SCROLL_VERTICAL_HANDLES) == 0)) {
    region_azone_scrollbar_init(area, region, AZ_SCROLL_VERT);
  }
  if ((v2d->scroll & V2D_SCROLL_HORIZONTAL) &&
      ((v2d->scroll & V2D_SCROLL_HORIZONTAL_HANDLES) == 0)) {
    region_azone_scrollbar_init(area, region, AZ_SCROLL_HOR);
  }
}

static void region_azones_add_edge(ScrArea *area,
                                   ARegion *region,
                                   const int alignment,
                                   const bool is_fullscreen)
{

  /* edge code (t b l r) is along which area edge azone will be drawn */
  if (alignment == RGN_ALIGN_TOP) {
    region_azone_edge_init(area, region, AE_BOTTOM_TO_TOPLEFT, is_fullscreen);
  }
  else if (alignment == RGN_ALIGN_BOTTOM) {
    region_azone_edge_init(area, region, AE_TOP_TO_BOTTOMRIGHT, is_fullscreen);
  }
  else if (alignment == RGN_ALIGN_RIGHT) {
    region_azone_edge_init(area, region, AE_LEFT_TO_TOPRIGHT, is_fullscreen);
  }
  else if (alignment == RGN_ALIGN_LEFT) {
    region_azone_edge_init(area, region, AE_RIGHT_TO_TOPLEFT, is_fullscreen);
  }
}

static void region_azones_add(const Screen *screen, ScrArea *area, ARegion *region)
{
  const bool is_fullscreen = screen->state == SCREENFULL;

  /* Only display tab or icons when the header region is hidden
   * (not the tool header - they overlap). */
  if (region->regiontype == RGN_TYPE_TOOL_HEADER) {
    return;
  }

  region_azones_add_edge(area, region, RGN_ALIGN_ENUM_FROM_MASK(region->alignment), is_fullscreen);

  /* For a split region also continue the azone edge from the next region if this region is aligned
   * with the next */
  if ((region->alignment & RGN_SPLIT_PREV) && region->prev) {
    region_azones_add_edge(
        area, region, RGN_ALIGN_ENUM_FROM_MASK(region->prev->alignment), is_fullscreen);
  }

  if (is_fullscreen) {
    fullscreen_azone_init(area, region);
  }

  region_azones_scrollbars_init(area, region);
}

/* dir is direction to check, not the splitting edge direction! */
static int rct_fits(const rcti *rect, const eScreenAxis dir_axis, int size)
{
  if (dir_axis == SCREEN_AXIS_H) {
    return lib_rcti_size_x(rect) + 1 - size;
  }
  /* Vertical. */
  return lib_rcti_size_y(rect) + 1 - size;
}

/* *************************************************************** */

/* region should be overlapping */
/* fn checks if some overlapping region was defined before - on same place */
static void region_overlap_fix(ScrArea *area, ARegion *region)
{
  /* find overlapping previous region on same place */
  ARegion *region_iter;
  int align1 = 0;
  const int align = RGN_ALIGN_ENUM_FROM_MASK(region->alignment);
  for (region_iter = region->prev; region_iter; region_iter = region_iter->prev) {
    if (region_iter->flag & RGN_FLAG_HIDDEN) {
      continue;
    }

    if (region_iter->overlap && ((region_iter->alignment & RGN_SPLIT_PREV) == 0)) {
      if (ELEM(region_iter->alignment, RGN_ALIGN_FLOAT)) {
        continue;
      }
      align1 = region_iter->alignment;
      if (lib_rcti_isect(&region_iter->winrct, &region->winrct, NULL)) {
        if (align1 != align) {
          /* Left overlapping right or vice-versa, forbid this! */
          region->flag |= RGN_FLAG_TOO_SMALL;
          return;
        }
        /* Else, we have our previous region on same side. */
        break;
      }
    }
  }

  /* Guard against flags slipping through that would have to be masked out in usages below. */
  lib_assert(align1 == RGN_ALIGN_ENUM_FROM_MASK(align1));

  /* translate or close */
  if (region_iter) {
    if (align1 == RGN_ALIGN_LEFT) {
      if (region->winrct.xmax + region_iter->winx > area->winx - U.widget_unit) {
        region->flag |= RGN_FLAG_TOO_SMALL;
        return;
      }
      lib_rcti_translate(&region->winrct, region_iter->winx, 0);
    }
    else if (align1 == RGN_ALIGN_RIGHT) {
      if (region->winrct.xmin - region_iter->winx < U.widget_unit) {
        region->flag |= RGN_FLAG_TOO_SMALL;
        return;
      }
      lib_rcti_translate(&region->winrct, -region_iter->winx, 0);
    }
  }

  /* At this point, 'region' is in its final position and still open.
   * Make a final check it does not overlap any previous 'other side' region. */
  for (region_iter = region->prev; region_iter; region_iter = region_iter->prev) {
    if (region_iter->flag & RGN_FLAG_HIDDEN) {
      continue;
    }
    if (ELEM(region_iter->alignment, RGN_ALIGN_FLOAT)) {
      continue;
    }

    if (region_iter->overlap && (region_iter->alignment & RGN_SPLIT_PREV) == 0) {
      if ((region_iter->alignment != align) &&
          lib_rcti_isect(&region_iter->winrct, &region->winrct, NULL)) {
        /* Left overlapping right or vice-versa, forbid this! */
        region->flag |= RGN_FLAG_TOO_SMALL;
        return;
      }
    }
  }
}

bool ed_region_is_overlap(int spacetype, int regiontype)
{
  if (regiontype == RGN_TYPE_HUD) {
    return true;
  }
  if (U.uiflag2 & USER_REGION_OVERLAP) {
    if (spacetype == SPACE_NODE) {
      if (regiontype == RGN_TYPE_TOOLS) {
        return true;
      }
    }
    else if (ELEM(spacetype, SPACE_VIEW3D, SPACE_IMAGE)) {
      if (ELEM(regiontype,
               RGN_TYPE_TOOLS,
               RGN_TYPE_UI,
               RGN_TYPE_TOOL_PROPS,
               RGN_TYPE_FOOTER,
               RGN_TYPE_TOOL_HEADER)) {
        return true;
      }
    }
  }

  return false;
}

static void region_rect_recursive(
    ScrArea *area, ARegion *region, rcti *remainder, rcti *overlap_remainder, int quad)
{
  rcti *remainder_prev = remainder;

  if (region == NULL) {
    return;
  }

  int prev_winx = region->winx;
  int prev_winy = region->winy;

  /* no returns in function, winrct gets set in the end again */
  lib_rcti_init(&region->winrct, 0, 0, 0, 0);

  /* for test; allow split of previously defined region */
  if (region->alignment & RGN_SPLIT_PREV) {
    if (region->prev) {
      remainder = &region->prev->winrct;
    }
  }

  int alignment = RGN_ALIGN_ENUM_FROM_MASK(region->alignment);

  /* set here, assuming userpref switching forces to call this again */
  region->overlap = ED_region_is_overlap(area->spacetype, region->regiontype);

  /* clear state flags first */
  region->flag &= ~(RGN_FLAG_TOO_SMALL | RGN_FLAG_SIZE_CLAMP_X | RGN_FLAG_SIZE_CLAMP_Y);
  /* user errors */
  if ((region->next == NULL) && !ELEM(alignment, RGN_ALIGN_QSPLIT, RGN_ALIGN_FLOAT)) {
    alignment = RGN_ALIGN_NONE;
  }

  /* If both the ARegion.sizex/y and the prefsize are 0, the region is tagged as too small, even
   * before the layout for dynamic regions is created. #wm_draw_window_offscreen() allows the
   * layout to be created despite the RGN_FLAG_TOO_SMALL flag being set. But there may still be
   * regions that don't have a separate ARegionType.layout callback. For those, set a default
   * prefsize so they can become visible. */
  if ((region->flag & RGN_FLAG_DYNAMIC_SIZE) && !(region->type->layout)) {
    if ((region->sizex == 0) && (region->type->prefsizex == 0)) {
      region->type->prefsizex = AREAMINX;
    }
    if ((region->sizey == 0) && (region->type->prefsizey == 0)) {
      region->type->prefsizey = HEADERY;
    }
  }

  /* prefsize, taking into account DPI */
  int prefsizex = UI_DPI_FAC *
                  ((region->sizex > 1) ? region->sizex + 0.5f : region->type->prefsizex);
  int prefsizey;

  if (region->flag & RGN_FLAG_PREFSIZE_OR_HIDDEN) {
    prefsizex = UI_DPI_FAC * region->type->prefsizex;
    prefsizey = UI_DPI_FAC * region->type->prefsizey;
  }
  else if (region->regiontype == RGN_TYPE_HEADER) {
    prefsizey = ed_area_headersize();
  }
  else if (region->regiontype == RGN_TYPE_TOOL_HEADER) {
    prefsizey = ed_area_headersize();
  }
  else if (region->regiontype == RGN_TYPE_FOOTER) {
    prefsizey = ed_area_footersize();
  }
  else if (ed_area_is_global(area)) {
    prefsizey = ed_region_global_size_y();
  }
  else {
    prefsizey = UI_DPI_FAC * (region->sizey > 1 ? region->sizey + 0.5f : region->type->prefsizey);
  }

  if (region->flag & RGN_FLAG_HIDDEN) {
    /* hidden is user flag */
  }
  else if (alignment == RGN_ALIGN_FLOAT) {
    /* Currently this window type is only used for RGN_TYPE_HUD,
     * We expect the panel to resize itself to be larger.
     *
     * This aligns to the lower left of the area. */
    const int size_min[2] = {UI_UNIT_X, UI_UNIT_Y};
    rcti overlap_remainder_margin = *overlap_remainder;

    lib_rcti_resize(&overlap_remainder_margin,
                    max_ii(0, lib_rcti_size_x(overlap_remainder) - UI_UNIT_X / 2),
                    max_ii(0, lib_rcti_size_y(overlap_remainder) - UI_UNIT_Y / 2));
    region->winrct.xmin = overlap_remainder_margin.xmin + region->runtime.offset_x;
    region->winrct.ymin = overlap_remainder_margin.ymin + region->runtime.offset_y;
    region->winrct.xmax = region->winrct.xmin + prefsizex - 1;
    region->winrct.ymax = region->winrct.ymin + prefsizey - 1;

    lib_rcti_isect(&region->winrct, &overlap_remainder_margin, &region->winrct);

    if (lib_rcti_size_x(&region->winrct) != prefsizex - 1) {
      region->flag |= RGN_FLAG_SIZE_CLAMP_X;
    }
    if (lib_rcti_size_y(&region->winrct) != prefsizey - 1) {
      region->flag |= RGN_FLAG_SIZE_CLAMP_Y;
    }

    /* We need to use a test that won't have been previously clamped. */
    rcti winrct_test = {
        .xmin = region->winrct.xmin,
        .ymin = region->winrct.ymin,
        .xmax = region->winrct.xmin + size_min[0],
        .ymax = region->winrct.ymin + size_min[1],
    };
    lib_rcti_isect(&winrct_test, &overlap_remainder_margin, &winrct_test);
    if (lib_rcti_size_x(&winrct_test) < size_min[0] ||
        lib_rcti_size_y(&winrct_test) < size_min[1]) {
      region->flag |= RGN_FLAG_TOO_SMALL;
    }
  }
  else if (rct_fits(remainder, SCREEN_AXIS_V, 1) < 0 ||
           rct_fits(remainder, SCREEN_AXIS_H, 1) < 0) {
    /* remainder is too small for any usage */
    region->flag |= RGN_FLAG_TOO_SMALL;
  }
  else if (alignment == RGN_ALIGN_NONE) {
    /* typically last region */
    region->winrct = *remainder;
    lib_rcti_init(remainder, 0, 0, 0, 0);
  }
  else if (ELEM(alignment, RGN_ALIGN_TOP, RGN_ALIGN_BOTTOM)) {
    rcti *winrct = (region->overlap) ? overlap_remainder : remainder;

    if ((prefsizey == 0) || (rct_fits(winrct, SCREEN_AXIS_V, prefsizey) < 0)) {
      region->flag |= RGN_FLAG_TOO_SMALL;
    }
    else {
      int fac = rct_fits(winrct, SCREEN_AXIS_V, prefsizey);

      if (fac < 0) {
        prefsizey += fac;
      }

      region->winrct = *winrct;

      if (alignment == RGN_ALIGN_TOP) {
        region->winrct.ymin = region->winrct.ymax - prefsizey + 1;
        winrct->ymax = region->winrct.ymin - 1;
      }
      else {
        region->winrct.ymax = region->winrct.ymin + prefsizey - 1;
        winrct->ymin = region->winrct.ymax + 1;
      }
      lib_rcti_sanitize(winrct);
    }
  }
  else if (ELEM(alignment, RGN_ALIGN_LEFT, RGN_ALIGN_RIGHT)) {
    rcti *winrct = (region->overlap) ? overlap_remainder : remainder;

    if ((prefsizex == 0) || (rct_fits(winrct, SCREEN_AXIS_H, prefsizex) < 0)) {
      region->flag |= RGN_FLAG_TOO_SMALL;
    }
    else {
      int fac = rct_fits(winrct, SCREEN_AXIS_H, prefsizex);

      if (fac < 0) {
        prefsizex += fac;
      }

      region->winrct = *winrct;

      if (alignment == RGN_ALIGN_RIGHT) {
        region->winrct.xmin = region->winrct.xmax - prefsizex + 1;
        winrct->xmax = region->winrct.xmin - 1;
      }
      else {
        region->winrct.xmax = region->winrct.xmin + prefsizex - 1;
        winrct->xmin = region->winrct.xmax + 1;
      }
      lib_rcti_sanitize(winrct);
    }
  }
  else if (ELEM(alignment, RGN_ALIGN_VSPLIT, RGN_ALIGN_HSPLIT)) {
    /* Percentage subdiv. */
    region->winrct = *remainder;

    if (alignment == RGN_ALIGN_HSPLIT) {
      if (rct_fits(remainder, SCREEN_AXIS_H, prefsizex) > 4) {
        region->winrct.xmax = lib_rcti_cent_x(remainder);
        remainder->xmin = region->winrct.xmax + 1;
      }
      else {
        lib_rcti_init(remainder, 0, 0, 0, 0);
      }
    }
    else {
      if (rct_fits(remainder, SCREEN_AXIS_V, prefsizey) > 4) {
        region->winrct.ymax = LIB_rcti_cent_y(remainder);
        remainder->ymin = region->winrct.ymax + 1;
      }
      else {
        lib_rcti_init(remainder, 0, 0, 0, 0);
      }
    }
  }
  else if (alignment == RGN_ALIGN_QSPLIT) {
    region->winrct = *remainder;

    /* test if there's still 4 regions left */
    if (quad == 0) {
      ARegion *region_test = region->next;
      int count = 1;

      while (region_test) {
        region_test->alignment = RGN_ALIGN_QSPLIT;
        region_test = region_test->next;
        count++;
      }

      if (count != 4) {
        /* let's stop adding regions */
        lib_rcti_init(remainder, 0, 0, 0, 0);
        if (G.debug & G_DEBUG) {
          printf("region quadsplit failed\n");
        }
      }
      else {
        quad = 1;
      }
    }
    if (quad) {
      if (quad == 1) { /* left bottom */
        region->winrct.xmax = lib_rcti_cent_x(remainder);
        region->winrct.ymax = lib_rcti_cent_y(remainder);
      }
      else if (quad == 2) { /* left top */
        region->winrct.xmax = lib_rcti_cent_x(remainder);
        region->winrct.ymin = lib_rcti_cent_y(remainder) + 1;
      }
      else if (quad == 3) { /* right bottom */
        region->winrct.xmin = lib_rcti_cent_x(remainder) + 1;
        region->winrct.ymax = lib_rcti_cent_y(remainder);
      }
      else { /* right top */
        region->winrct.xmin = lib_rcti_cent_x(remainder) + 1;
        region->winrct.ymin = lib_rcti_cent_y(remainder) + 1;
        lib_rcti_init(remainder, 0, 0, 0, 0);
      }

      /* Fix any negative dimensions. This can happen when a quad split 3d view gets too small.
       * (see T72200). */
      lib_rcti_sanitize(&region->winrct);

      quad++;
    }
  }

  /* for speedup */
  region->winx = lib_rcti_size_x(&region->winrct) + 1;
  region->winy = lib_rcti_size_y(&region->winrct) + 1;

  /* If region opened normally, we store this for hide/reveal usage. */
  /* Prevent rounding errors for UI_DPI_FAC multiply and divide. */
  if (region->winx > 1) {
    region->sizex = (region->winx + 0.5f) / UI_DPI_FAC;
  }
  if (region->winy > 1) {
    region->sizey = (region->winy + 0.5f) / UI_DPI_FAC;
  }

  /* exception for multiple overlapping regions on same spot */
  if (region->overlap && (alignment != RGN_ALIGN_FLOAT)) {
    region_overlap_fix(area, region);
  }

  /* set winrect for azones */
  if (region->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL)) {
    region->winrct = (region->overlap) ? *overlap_remainder : *remainder;

    switch (alignment) {
      case RGN_ALIGN_TOP:
        region->winrct.ymin = region->winrct.ymax;
        break;
      case RGN_ALIGN_BOTTOM:
        region->winrct.ymax = region->winrct.ymin;
        break;
      case RGN_ALIGN_RIGHT:
        region->winrct.xmin = region->winrct.xmax;
        break;
      case RGN_ALIGN_LEFT:
        region->winrct.xmax = region->winrct.xmin;
        break;
      default:
        /* prevent winrct to be valid */
        region->winrct.xmax = region->winrct.xmin;
        break;
    }

    /* Size on one axis is now 0, the other axis may still be invalid (negative) though. */
    lib_rcti_sanitize(&region->winrct);
  }

  /* restore prev-split exception */
  if (region->alignment & RGN_SPLIT_PREV) {
    if (region->prev) {
      remainder = remainder_prev;
      region->prev->winx = lib_rcti_size_x(&region->prev->winrct) + 1;
      region->prev->winy = lib_rcti_size_y(&region->prev->winrct) + 1;
    }
  }

  /* After non-overlapping region, all following overlapping regions
   * fit within the remaining space again. */
  if (!region->overlap) {
    *overlap_remainder = *remainder;
  }

  lib_assert(lib_rcti_is_valid(&region->winrct));

  region_rect_recursive(area, region->next, remainder, overlap_remainder, quad);

  /* Tag for redraw if size changes. */
  if (region->winx != prev_winx || region->winy != prev_winy) {
    /* 3D View needs a full rebuild in case a progressive render runs. Rest can live with
     * no-rebuild (e.g. Outliner) */
    if (area->spacetype == SPACE_VIEW3D) {
      lib_region_tag_redraw(region);
    }
    else {
      lib_region_tag_redraw_no_rebuild(region);
    }
  }

  /* Clear, initialize on demand. */
  memset(&region->runtime.visible_rect, 0, sizeof(region->runtime.visible_rect));
}

static void area_calc_totrct(ScrArea *area, const rcti *window_rect)
{
  short px = (short)U.pixelsize;

  area->totrct.xmin = area->v1->vec.x;
  area->totrct.xmax = area->v4->vec.x;
  area->totrct.ymin = area->v1->vec.y;
  area->totrct.ymax = area->v2->vec.y;

  /* scale down totrct by 1 pixel on all sides not matching window borders */
  if (area->totrct.xmin > window_rect->xmin) {
    area->totrct.xmin += px;
  }
  if (area->totrct.xmax < (window_rect->xmax - 1)) {
    area->totrct.xmax -= px;
  }
  if (area->totrct.ymin > window_rect->ymin) {
    area->totrct.ymin += px;
  }
  if (area->totrct.ymax < (window_rect->ymax - 1)) {
    area->totrct.ymax -= px;
  }
  /* Although the following asserts are correct they lead to a very unstable Dune.
   * And the asserts would fail even in 2.7x
   * (they were added in 2.8x as part of the top-bar commit).
   * For more details see T54864. */
#if 0
  lib_assert(area->totrct.xmin >= 0);
  lib_assert(area->totrct.xmax >= 0);
  lib_assert(area->totrct.ymin >= 0);
  lib_assert(area->totrct.ymax >= 0);
#endif

  /* for speedup */
  area->winx = lib_rcti_size_x(&area->totrct) + 1;
  area->winy = lib_rcti_size_y(&area->totrct) + 1;
}

/* used for area initialize below */
static void region_subwindow(ARegion *region)
{
  bool hidden = (region->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL)) != 0;

  if ((region->alignment & RGN_SPLIT_PREV) && region->prev) {
    hidden = hidden || (region->prev->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL));
  }

  region->visible = !hidden;
}

static bool event_in_markers_region(const ARegion *region, const wmEvent *event)
{
  rcti rect = region->winrct;
  rect.ymax = rect.ymin + UI_MARKER_MARGIN_Y;
  return lib_rcti_isect_pt_v(&rect, event->xy);
}

/** param region: Region, may be NULL when adding handlers for a area. **/
static void ed_default_handlers(
    WindowManager *wm, ScrArea *area, ARegion *region, List *handlers, int flag)
{
  lib_assert(region ? (&region->handlers == handlers) : (&area->handlers == handlers));

  /* NOTE: add-handler checks if it already exists. */

  /* it would be good to have bound-box checks for some of these. */
  if (flag & ED_KEYMAP_UI) {
    wmKeyMap *keymap = wm_keymap_ensure(wm->defaultconf, "User Interface", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap);

    List *dropboxes = wm_dropboxmap_find("User Interface", 0, 0);
    wm_event_add_dropbox_handler(handlers, dropboxes);

    /* user interface widgets */
    ui_region_handlers_add(handlers);
  }
  if (flag & ED_KEYMAP_GIZMO) {
    lib_assert(region && ELEM(region->type->regionid, RGN_TYPE_WINDOW, RGN_TYPE_PREVIEW));
    if (region) {
      /* Anything else is confusing, only allow this. */
      lib_assert(&region->handlers == handlers);
      if (region->gizmo_map == NULL) {
        region->gizmo_map = WM_gizmomap_new_from_type(
            &(const struct wmGizmoMapType_Params){area->spacetype, region->type->regionid});
      }
      wm_gizmomap_add_handlers(region, region->gizmo_map);
    }
  }
  if (flag & ED_KEYMAP_VIEW2D) {
    /* 2d-viewport handling+manipulation */
    wmKeyMap *keymap = wm_keymap_ensure(wm->defaultconf, "View2D", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap);
  }
  if (flag & ED_KEYMAP_ANIMATION) {
    wmKeyMap *keymap;

    /* time-markers */
    keymap = wm_keymap_ensure(wm->defaultconf, "Markers", 0, 0);
    wm_event_add_keymap_handler_poll(handlers, keymap, event_in_markers_region);

    /* time-scrub */
    keymap = wm_keymap_ensure(wm->defaultconf, "Time Scrub", 0, 0);
    wm_event_add_keymap_handler_poll(handlers, keymap, ed_time_scrub_event_in_region);

    /* frame changing and timeline operators (for time spaces) */
    keymap = wm_keymap_ensure(wm->defaultconf, "Animation", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap);
  }
  if (flag & ED_KEYMAP_TOOL) {
    if (flag & ED_KEYMAP_GIZMO) {
      wm_event_add_keymap_handler_dynamic(
          &region->handlers, wm_event_get_keymap_from_toolsystem_with_gizmos, area);
    }
    else {
      wm_event_add_keymap_handler_dynamic(
          &region->handlers, wm_event_get_keymap_from_toolsystem, area);
    }
  }
  if (flag & ED_KEYMAP_FRAMES) {
    /* frame changing/jumping (for all spaces) */
    wmKeyMap *keymap = wm_keymap_ensure(wm->defaultconf, "Frames", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap);
  }
  if (flag & ED_KEYMAP_HEADER) {
    /* standard keymap for headers regions */
    wmKeyMap *keymap = wm_keymap_ensure(wm->defaultconf, "Region Context Menu", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap);
  }
  if (flag & ED_KEYMAP_FOOTER) {
    /* standard keymap for footer regions */
    wmKeyMap *keymap = wm_keymap_ensure(wm->defaultconf, "Region Context Menu", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap);
  }
  if (flag & ED_KEYMAP_NAVBAR) {
    /* standard keymap for Nav bar regions */
    wmKeyMap *keymap = wm_keymap_ensure(wm->defaultconf, "Region Context Menu", 0, 0);
    wm_event_add_keymap_handler(&region->handlers, keymap);
  }

  /* Keep last because of LMB/RMB handling, see: T57527. */
  if (flag & ED_KEYMAP_GPENCIL) {
    /* pen */
    /* This is now 4 keymaps - One for basic functionality,
     * and others for special stroke modes (edit, paint and sculpt).
     *
     * For now, it's easier to just include all,
     * since you hardly want one without the others. */
    wmKeyMap *keymap_general = wm_keymap_ensure(wm->defaultconf, "Pen", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_general);

    wmKeyMap *keymap_curve_edit = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Curve Edit Mode", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_curve_edit);

    wmKeyMap *keymap_edit = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Edit Mode", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_edit);

    wmKeyMap *keymap_paint = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Paint Mode", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_paint);

    wmKeyMap *keymap_paint_draw = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Paint (Draw brush)", 0, 0);
    WM_event_add_keymap_handler(handlers, keymap_paint_draw);

    wmKeyMap *keymap_paint_erase = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Paint (Erase)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_paint_erase);

    wmKeyMap *keymap_paint_fill = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Paint (Fill)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_paint_fill);

    wmKeyMap *keymap_paint_tint = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Paint (Tint)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_paint_tint);

    wmKeyMap *keymap_sculpt = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Sculpt Mode", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_sculpt);

    wmKeyMap *keymap_vertex = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Vertex Mode", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_vertex);

    wmKeyMap *keymap_vertex_draw = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Vertex (Draw)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_vertex_draw);

    wmKeyMap *keymap_vertex_blur = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Vertex (Blur)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_vertex_blur);

    wmKeyMap *keymap_vertex_average = wm_keymap_ensure(
        wm->defaultconf, "Pen Stroke Vertex (Average)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_vertex_average);

    wmKeyMap *keymap_vertex_smear = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Vertex (Smear)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_vertex_smear);

    wmKeyMap *keymap_vertex_replace = WM_keymap_ensure(
        wm->defaultconf, "Pen Stroke Vertex (Replace)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_vertex_replace);

    wmKeyMap *keymap_sculpt_smooth = WM_keymap_ensure(
        wm->defaultconf, "Pen Stroke Sculpt (Smooth)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_sculpt_smooth);

    wmKeyMap *keymap_sculpt_thickness = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Sculpt (Thickness)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_sculpt_thickness);

    wmKeyMap *keymap_sculpt_strength = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Sculpt (Strength)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_sculpt_strength);

    wmKeyMap *keymap_sculpt_grab = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Sculpt (Grab)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_sculpt_grab);

    wmKeyMap *keymap_sculpt_push = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Sculpt (Push)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_sculpt_push);

    wmKeyMap *keymap_sculpt_twist = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Sculpt (Twist)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_sculpt_twist);

    wmKeyMap *keymap_sculpt_pinch = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Sculpt (Pinch)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_sculpt_pinch);

    wmKeyMap *keymap_sculpt_randomize = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Sculpt (Randomize)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_sculpt_randomize);

    wmKeyMap *keymap_sculpt_clone = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Sculpt (Clone)", 0, 0);
    WM_event_add_keymap_handler(handlers, keymap_sculpt_clone);

    wmKeyMap *keymap_weight = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Weight Mode", 0, 0);
    WM_event_add_keymap_handler(handlers, keymap_weight);

    wmKeyMap *keymap_weight_draw = WM_keymap_ensure(
        wm->defaultconf, "Pencil Stroke Weight (Draw)", 0, 0);
    wm_event_add_keymap_handler(handlers, keymap_weight_draw);
  }
}

void ed_area_update_region_sizes(WindowManager *wm, Window *win, ScrArea *area)
{
  if (!(area->flag & AREA_FLAG_REGION_SIZE_UPDATE)) {
    return;
  }
  const Screen *screen = wm_window_get_active_screen(win);

  rcti window_rect;
  WM_window_rect_calc(win, &window_rect);
  area_calc_totrct(area, &window_rect);

  /* region rect sizes */
  rcti rect = area->totrct;
  rcti overlap_rect = rect;
  region_rect_recursive(area, area->regionbase.first, &rect, &overlap_rect, 0);

  /* Dynamically sized regions may have changed region sizes, so we have to force azone update. */
  area_azone_init(win, screen, area);

  LIST_FOREACH (ARegion *, region, &area->regionbase) {
    region_subwindow(region);

    /* region size may have changed, init does necessary adjustments */
    if (region->type->init) {
      region->type->init(wm, region);
    }

    /* Some AZones use View2D data which is only updated in region init, so call that first! */
    region_azones_add(screen, area, region);
  }
  ed_area_azones_update(area, win->eventstate->xy);

  area->flag &= ~AREA_FLAG_REGION_SIZE_UPDATE;
}

bool ed_area_has_shared_border(struct ScrArea *a, struct ScrArea *b)
{
  return area_getorientation(a, b) != -1;
}

void ed_area_init(WindowManager *wm, wmWindow *win, ScrArea *area)
{
  WorkSpace *workspace = ed_window_get_active_workspace(win);
  const Screen *screen = DUNE_workspace_active_screen_get(win->workspace_hook);
  ViewLayer *view_layer = wm_window_get_active_view_layer(win);

  if (ed_area_is_global(area) && (area->global->flag & GLOBAL_AREA_IS_HIDDEN)) {
    return;
  }

  rcti window_rect;
  wm_window_rect_calc(win, &window_rect);

  /* Set type-definitions. */
  area->type = dune_spacetype_from_id(area->spacetype);

  if (area->type == NULL) {
    area->spacetype = SPACE_VIEW3D;
    area->type = dune_spacetype_from_id(area->spacetype);
  }

  LIST_FOREACH (ARegion *, region, &area->regionbase) {
    region->type = dune_regiontype_from_id_or_first(area->type, region->regiontype);
  }

  /* area sizes */
  area_calc_totrct(area, &window_rect);

  /* region rect sizes */
  rcti rect = area->totrct;
  rcti overlap_rect = rect;
  region_rect_recursive(area, area->regionbase.first, &rect, &overlap_rect, 0);
  area->flag &= ~AREA_FLAG_REGION_SIZE_UPDATE;

  /* default area handlers */
  ed_default_handlers(wm, area, NULL, &area->handlers, area->type->keymapflag);
  /* checks spacedata, adds own handlers */
  if (area->type->init) {
    area->type->init(wm, area);
  }

  /* clear all azones, add the area triangle widgets */
  area_azone_init(win, screen, area);

  /* region windows, default and own handlers */
  LIST_FOREACH (ARegion *, region, &area->regionbase) {
    region_subwindow(region);

    if (region->visible) {
      /* default region handlers */
      ed_default_handlers(wm, area, region, &region->handlers, region->type->keymapflag);
      /* own handlers */
      if (region->type->init) {
        region->type->init(wm, region);
      }
    }
    else {
      /* prevent uiblocks to run */
      ui_blocklist_free(NULL, region);
    }

    /* Some AZones use View2D data which is only updated in region init, so call that first! */
    region_azones_add(screen, area, region);
  }

  /* Avoid re-initializing tools while resizing the window. */
  if ((G.moving & G_TRANSFORM_WM) == 0) {
    if ((1 << area->spacetype) & WM_TOOLSYSTEM_SPACE_MASK) {
      wm_toolsystem_refresh_screen_area(workspace, view_layer, area);
      area->flag |= AREA_FLAG_ACTIVE_TOOL_UPDATE;
    }
    else {
      area->runtime.tool = NULL;
      area->runtime.is_tool_set = true;
    }
  }
}

static void area_offscreen_init(ScrArea *area)
{
  area->type = dune_spacetype_from_id(area->spacetype);

  if (area->type == NULL) {
    area->spacetype = SPACE_VIEW3D;
    area->type = dune_spacetype_from_id(area->spacetype);
  }

  LIST_FOREACH (ARegion *, region, &area->regionbase) {
    region->type = dune_regiontype_from_id_or_first(area->type, region->regiontype);
  }
}

ScrArea *ed_area_offscreen_create(Window *win, eSpace_Type space_type)
{
  ScrArea *area = MEM_callocN(sizeof(*area), __func__);
  area->spacetype = space_type;

  screen_area_spacelink_add(wm_window_get_active_scene(win), area, space_type);
  area_offscreen_init(area);

  return area;
}

static void area_offscreen_exit(WindowManager *wm, Window *win, ScrArea *area)
{
  if (area->type && area->type->exit) {
    area->type->exit(wm, area);
  }

  LIST_FOREACH (ARegion *, region, &area->regionbase) {
    if (region->type && region->type->exit) {
      region->type->exit(wm, region);
    }

    wm_event_modal_handler_region_replace(win, region, NULL);
    wm_draw_region_free(region, true);

    MEM_SAFE_FREE(region->headerstr);

    if (region->regiontimer) {
      wm_event_remove_timer(wm, win, region->regiontimer);
      region->regiontimer = NULL;
    }

    if (wm->message_bus) {
      wm_msgbus_clear_by_owner(wm->message_bus, region);
    }
  }

  wm_event_modal_handler_area_replace(win, area, NULL);
}

void ed_area_offscreen_free(WindowManager *wm, Window *win, ScrArea *area)
{
  area_offscreen_exit(wm, win, area);

  dune_screen_area_free(area);
  mem_freen(area);
}

static void region_update_rect(ARegion *region)
{
  region->winx = lib_rcti_size_x(&region->winrct) + 1;
  region->winy = lib_rcti_size_y(&region->winrct) + 1;

  /* v2d mask is used to subtract scrollbars from a 2d view. Needs initialize here. */
  lib_rcti_init(&region->v2d.mask, 0, region->winx - 1, 0, region->winy - 1);
}

void ed_region_update_rect(ARegion *region)
{
  region_update_rect(region);
}

void ed_region_floating_init(ARegion *region)
{
  lib_assert(region->alignment == RGN_ALIGN_FLOAT);

  /* refresh can be called before window opened */
  region_subwindow(region);

  region_update_rect(region);
}

void ed_region_cursor_set(Window *win, ScrArea *area, ARegion *region)
{
  if (region != NULL) {
    if ((region->gizmo_map != NULL) && wm_gizmomap_cursor_set(region->gizmo_map, win)) {
      return;
    }
    if (area && region->type && region->type->cursor) {
      region->type->cursor(win, area, region);
      return;
    }
  }

  if (wm_cursor_set_from_tool(win, area, region)) {
    return;
  }

  wm_cursor_set(win, WM_CURSOR_DEFAULT);
}

void ed_region_visibility_change_update(Cxt *C, ScrArea *area, ARegion *region)
{
  if (region->flag & RGN_FLAG_HIDDEN) {
    wm_event_remove_handlers(C, &region->handlers);
    /* Needed to close any open pop-overs which would otherwise remain open,
     * crashing on attempting to refresh. See: T93410.
     *
     * When ED_area_init frees buttons via UI_blocklist_free a NULL context
     * is passed, causing the free not to remove menus or their handlers. */
    ui_region_free_active_but_all(C, region);
  }

  ed_area_init(cxt_wm_manager(C), cxt_wm_window(C), area);
  ed_area_tag_redraw(area);
}

void region_toggle_hidden(Cxt *C, ARegion *region, const bool do_fade)
{
  ScrArea *area = cxt_wm_area(C);

  region->flag ^= RGN_FLAG_HIDDEN;

  if (do_fade && region->overlap) {
    /* starts a timer, and in end calls the stuff below itself (region_sblend_invoke()) */
    ed_region_visibility_change_update_animated(C, area, region);
  }
  else {
    ed_region_visibility_change_update(C, area, region);
  }
}

void ed_region_toggle_hidden(Cxt *C, ARegion *region)
{
  region_toggle_hidden(C, region, true);
}

void ed_area_data_copy(ScrArea *area_dst, ScrArea *area_src, const bool do_free)
{
  const char spacetype = area_dst->spacetype;
  const short flag_copy = HEADER_NO_PULLDOWN;

  area_dst->spacetype = area_src->spacetype;
  area_dst->type = area_src->type;

  area_dst->flag = (area_dst->flag & ~flag_copy) | (area_src->flag & flag_copy);

  /* area */
  if (do_free) {
    dune_spacedata_freelist(&area_dst->spacedata);
  }
  dune_spacedata_copylist(&area_dst->spacedata, &area_src->spacedata);

  /* NOTE: SPACE_EMPTY is possible on new screens. */

  /* regions */
  if (do_free) {
    SpaceType *st = DUNE_spacetype_from_id(spacetype);
    LIST_FOREACH (ARegion *, region, &area_dst->regionbase) {
      dune_area_region_free(st, region);
    }
    lib_freelistn(&area_dst->regionbase);
  }
  SpaceType *st = dune_spacetype_from_id(area_src->spacetype);
  LIST_FOREACH (ARegion *, region, &area_src->regionbase) {
    ARegion *newar = dune_area_region_copy(st, region);
    lib_addtail(&area_dst->regionbase, newar);
  }
}

void ed_area_data_swap(ScrArea *area_dst, ScrArea *area_src)
{
  SWAP(char, area_dst->spacetype, area_src->spacetype);
  SWAP(SpaceType *, area_dst->type, area_src->type);

  SWAP(ListBase, area_dst->spacedata, area_src->spacedata);
  SWAP(ListBase, area_dst->regionbase, area_src->regionbase);
}

/* -------------------------------------------------------------------- */
/* Region Alignment Syncing for Space Switching **/

/* Store the alignment & other info per region type
 * (use as a region-type aligned array).
 *
 * note Currently this is only done for headers,
 * we might want to do this with the tool-bar in the future too. */
struct RegionTypeAlignInfo {
  struct {
    /* Values match ARegion.alignment without flags (see #RGN_ALIGN_ENUM_FROM_MASK).
     * store all so we can sync alignment without adding extra checks. */
    short alignment;
    /* Needed for detecting which header displays the space-type switcher. */
    bool hidden;
  } by_type[RGN_TYPE_LEN];
};

static void region_align_info_from_area(ScrArea *area, struct RegionTypeAlignInfo *r_align_info)
{
  for (int index = 0; index < RGN_TYPE_LEN; index++) {
    r_align_info->by_type[index].alignment = -1;
    /* Default to true, when it doesn't exist - it's effectively hidden. */
    r_align_info->by_type[index].hidden = true;
  }

  LIST_FOREACH (ARegion *, region, &area->regionbase) {
    const int index = region->regiontype;
    if ((uint)index < RGN_TYPE_LEN) {
      r_align_info->by_type[index].alignment = RGN_ALIGN_ENUM_FROM_MASK(region->alignment);
      r_align_info->by_type[index].hidden = (region->flag & RGN_FLAG_HIDDEN) != 0;
    }
  }
}

/* Keeping alignment between headers keep the space-type selector button in the same place.
 * This is complicated by the editor-type selector being placed on the header
 * closest to the screen edge which changes based on hidden state.
 *
 * The tool-header is used when visible, otherwise the header is used */
static short region_alignment_from_header_and_tool_header_state(
    const struct RegionTypeAlignInfo *region_align_info, const short fallback)
{
  const short header_alignment = region_align_info->by_type[RGN_TYPE_HEADER].alignment;
  const short tool_header_alignment = region_align_info->by_type[RGN_TYPE_TOOL_HEADER].alignment;

  const bool header_hidden = region_align_info->by_type[RGN_TYPE_HEADER].hidden;
  const bool tool_header_hidden = region_align_info->by_type[RGN_TYPE_TOOL_HEADER].hidden;

  if ((tool_header_alignment != -1) &&
      /* If tool-header is hidden, use header alignment. */
      ((tool_header_hidden == false) ||
       /* Don't prioritize the tool-header if both are hidden (behave as if both are visible).
        * Without this, switching to a space with headers hidden will flip the alignment
        * upon switching to a space with visible headers. */
       (header_hidden && tool_header_hidden))) {
    return tool_header_alignment;
  }
  if (header_alignment != -1) {
    return header_alignment;
  }
  return fallback;
}

/* Notes on header alignment syncing.
 *
 * This is as involved as it is because:
 *
 * - There are currently 3 kinds of headers.
 * - All headers can independently visible & flipped to another side
 *   (except for the tool-header that depends on the header visibility).
 * - We don't want the space-switching button to flip when switching spaces.
 *   From the user perspective it feels like a bug to move the button you click on
 *   to the opposite side of the area.
 * - The space-switcher may be on either the header or the tool-header
 *   depending on the tool-header visibility.
 *
 * How this works:
 *
 * - When headers match on both spaces, we copy the alignment
 *   from the previous regions to the next regions when syncing.
 * - Otherwise detect the _primary_ header (the one that shows the space type)
 *   and use this to set alignment for the headers in the destination area.
 * - Header & tool-header/footer may be on opposite sides, this is preserved when syncing. */
static void region_align_info_to_area_for_headers(
    const struct RegionTypeAlignInfo *region_align_info_src,
    const struct RegionTypeAlignInfo *region_align_info_dst,
    ARegion *region_by_type[RGN_TYPE_LEN])
{
  /* Abbreviate access. */
  const short header_alignment_src = region_align_info_src->by_type[RGN_TYPE_HEADER].alignment;
  const short tool_header_alignment_src =
      region_align_info_src->by_type[RGN_TYPE_TOOL_HEADER].alignment;

  const bool tool_header_hidden_src = region_align_info_src->by_type[RGN_TYPE_TOOL_HEADER].hidden;

  const short primary_header_alignment_src = region_alignment_from_header_and_tool_header_state(
      region_align_info_src, -1);

  /* Neither alignments are usable, don't sync. */
  if (primary_header_alignment_src == -1) {
    return;
  }

  const short header_alignment_dst = region_align_info_dst->by_type[RGN_TYPE_HEADER].alignment;
  const short tool_header_alignment_dst =
      region_align_info_dst->by_type[RGN_TYPE_TOOL_HEADER].alignment;
  const short footer_alignment_dst = region_align_info_dst->by_type[RGN_TYPE_FOOTER].alignment;

  const bool tool_header_hidden_dst = region_align_info_dst->by_type[RGN_TYPE_TOOL_HEADER].hidden;

  /* New synchronized alignments to set (or ignore when left as -1). */
  short header_alignment_sync = -1;
  short tool_header_alignment_sync = -1;
  short footer_alignment_sync = -1;

  /* Both source/destination areas have same region configurations regarding headers.
   * Simply copy the values. */
  if (((header_alignment_src != -1) == (header_alignment_dst != -1)) &&
      ((tool_header_alignment_src != -1) == (tool_header_alignment_dst != -1)) &&
      (tool_header_hidden_src == tool_header_hidden_dst)) {
    if (header_alignment_dst != -1) {
      header_alignment_sync = header_alignment_src;
    }
    if (tool_header_alignment_dst != -1) {
      tool_header_alignment_sync = tool_header_alignment_src;
    }
  }
  else {
    /* Not an exact match, check the space selector isn't moving. */
    const short primary_header_alignment_dst = region_alignment_from_header_and_tool_header_state(
        region_align_info_dst, -1);

    if (primary_header_alignment_src != primary_header_alignment_dst) {
      if ((header_alignment_dst != -1) && (tool_header_alignment_dst != -1)) {
        if (header_alignment_dst == tool_header_alignment_dst) {
          /* Apply to both. */
          tool_header_alignment_sync = primary_header_alignment_src;
          header_alignment_sync = primary_header_alignment_src;
        }
        else {
          /* Keep on opposite sides. */
          tool_header_alignment_sync = primary_header_alignment_src;
          header_alignment_sync = (tool_header_alignment_sync == RGN_ALIGN_BOTTOM) ?
                                      RGN_ALIGN_TOP :
                                      RGN_ALIGN_BOTTOM;
        }
      }
      else {
        /* Apply what we can to regions that exist. */
        if (header_alignment_dst != -1) {
          header_alignment_sync = primary_header_alignment_src;
        }
        if (tool_header_alignment_dst != -1) {
          tool_header_alignment_sync = primary_header_alignment_src;
        }
      }
    }
  }

  if (footer_alignment_dst != -1) {
    if ((header_alignment_dst != -1) && (header_alignment_dst == footer_alignment_dst)) {
      /* Apply to both. */
      footer_alignment_sync = primary_header_alignment_src;
    }
    else {
      /* Keep on opposite sides. */
      footer_alignment_sync = (primary_header_alignment_src == RGN_ALIGN_BOTTOM) ?
                                  RGN_ALIGN_TOP :
                                  RGN_ALIGN_BOTTOM;
    }
  }

  /* Finally apply synchronized flags. */
  if (header_alignment_sync != -1) {
    ARegion *region = region_by_type[RGN_TYPE_HEADER];
    if (region != NULL) {
      region->alignment = RGN_ALIGN_ENUM_FROM_MASK(header_alignment_sync) |
                          RGN_ALIGN_FLAG_FROM_MASK(region->alignment);
    }
  }

  if (tool_header_alignment_sync != -1) {
    ARegion *region = region_by_type[RGN_TYPE_TOOL_HEADER];
    if (region != NULL) {
      region->alignment = RGN_ALIGN_ENUM_FROM_MASK(tool_header_alignment_sync) |
                          RGN_ALIGN_FLAG_FROM_MASK(region->alignment);
    }
  }

  if (footer_alignment_sync != -1) {
    ARegion *region = region_by_type[RGN_TYPE_FOOTER];
    if (region != NULL) {
      region->alignment = RGN_ALIGN_ENUM_FROM_MASK(footer_alignment_sync) |
                          RGN_ALIGN_FLAG_FROM_MASK(region->alignment);
    }
  }
}

static void region_align_info_to_area(
    ScrArea *area, const struct RegionTypeAlignInfo region_align_info_src[RGN_TYPE_LEN])
{
  ARegion *region_by_type[RGN_TYPE_LEN] = {NULL};
  LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
    const int index = region->regiontype;
    if ((uint)index < RGN_TYPE_LEN) {
      region_by_type[index] = region;
    }
  }

  struct RegionTypeAlignInfo region_align_info_dst;
  region_align_info_from_area(area, &region_align_info_dst);

  if ((region_by_type[RGN_TYPE_HEADER] != NULL) ||
      (region_by_type[RGN_TYPE_TOOL_HEADER] != NULL)) {
    region_align_info_to_area_for_headers(
        region_align_info_src, &region_align_info_dst, region_by_type);
  }

  /* Note that we could support other region types. */
}

/* *********** Space switching code *********** */

void ed_area_swapspace(Cxt *C, ScrArea *sa1, ScrArea *sa2)
{
  ScrArea *tmp = mem_callocn(sizeof(ScrArea), "addscrarea");
  Window *win = cxt_wm_window(C);

  ed_area_exit(C, sa1);
  ed_area_exit(C, sa2);

  ed_area_data_copy(tmp, sa1, false);
  ed_area_data_copy(sa1, sa2, true);
  ed_area_data_copy(sa2, tmp, true);
  ed_area_init(cxt_wm_manager(C), win, sa1);
  ed_area_init(cxt_wm_manager(C), win, sa2);

  dune_screen_area_free(tmp);
  mem_freen(tmp);

  /* tell WM to refresh, cursor types etc */
  wm_event_add_mousemove(win);

  ed_area_tag_redraw(sa1);
  ed_area_tag_refresh(sa1);
  ed_area_tag_redraw(sa2);
  ed_area_tag_refresh(sa2);
}

void ed_area_newspace(Cxt *C, ScrArea *area, int type, const bool skip_region_exit)
{
  Window *win = cxt_wm_window(C);

  if (area->spacetype != type) {
    SpaceLink *slold = area->spacedata.first;
    /* store area->type->exit callback */
    void *area_exit = area->type ? area->type->exit : NULL;
    /* When the user switches between space-types from the type-selector,
     * changing the header-type is jarring (especially when using Ctrl-MouseWheel).
     *
     * However, add-on install for example, forces the header to the top which shouldn't
     * be applied back to the previous space type when closing - see: T57724
     *
     * Newly-created windows won't have any space data, use the alignment
     * the space type defaults to in this case instead
     * (needed for preferences to have space-type on bottom).
     */

    bool sync_header_alignment = false;
    struct RegionTypeAlignInfo region_align_info[RGN_TYPE_LEN];
    if ((slold != NULL) && (slold->link_flag & SPACE_FLAG_TYPE_TEMPORARY) == 0) {
      region_align_info_from_area(area, region_align_info);
      sync_header_alignment = true;
    }

    /* in some cases (opening temp space) we don't want to
     * call area exit callback, so we temporarily unset it */
    if (skip_region_exit && area->type) {
      area->type->exit = NULL;
    }

    ed_area_exit(C, area);

    /* restore old area exit callback */
    if (skip_region_exit && area->type) {
      area->type->exit = area_exit;
    }

    SpaceType *st = dune_spacetype_from_id(type);

    area->spacetype = type;
    area->type = st;

    /* If st->create may be called, don't use cxt until then. The
     * area->type->cxt() cb has changed but data may be invalid
     * (e.g. with props editor) until space-data is properly created */

    /* check previously stored space */
    SpaceLink *sl = NULL;
    LIST_FOREACH (SpaceLink *, sl_iter, &area->spacedata) {
      if (sl_iter->spacetype == type) {
        sl = sl_iter;
        break;
      }
    }

    /* old spacedata... happened during work on 2.50, remove */
    if (sl && lib_list_is_empty(&sl->regionbase)) {
      st->free(sl);
      lib_freelinkn(&area->spacedata, sl);
      if (slold == sl) {
        slold = NULL;
      }
      sl = NULL;
    }

    if (sl) {
      /* swap regions */
      slold->regionbase = area->regionbase;
      area->regionbase = sl->regionbase;
      lib_list_clear(&sl->regionbase);
      /* SPACE_FLAG_TYPE_WAS_ACTIVE is only used to go back to a previously active space that is
       * overlapped by temporary ones. It's now properly activated, so the flag should be cleared
       * at this point. */
      sl->link_flag &= ~SPACE_FLAG_TYPE_WAS_ACTIVE;

      /* put in front of list */
      lib_remlink(&area->spacedata, sl);
      lib_addhead(&area->spacedata, sl);
    }
    else {
      /* new space */
      if (st) {
        /* Don't get scene from context here which may depend on space-data. */
        Scene *scene = wm_window_get_active_scene(win);
        sl = st->create(area, scene);
        lib_addhead(&area->spacedata, sl);

        /* swap regions */
        if (slold) {
          slold->regionbase = area->regionbase;
        }
        area->regionbase = sl->regionbase;
        lib_list_clear(&sl->regionbase);
      }
    }

    /* Sync header alignment. */
    if (sync_header_alignment) {
      region_align_info_to_area(area, region_align_info);
    }

    ed_area_init(cxt_wm_manager(C), win, area);

    /* tell wm to refresh, cursor types etc */
    wm_event_add_mousemove(win);

    /* send space change notifier */
    wm_event_add_notifier(C, NC_SPACE | ND_SPACE_CHANGED, area);

    ed_area_tag_refresh(area);
  }

  /* also redraw when re-used */
  ed_area_tag_redraw(area);
}

static SpaceLink *area_get_prevspace(ScrArea *area)
{
  SpaceLink *sl = area->spacedata.first;

  /* First toggle to the next temporary space in the list. */
  for (SpaceLink *sl_iter = sl->next; sl_iter; sl_iter = sl_iter->next) {
    if (sl_iter->link_flag & SPACE_FLAG_TYPE_TEMPORARY) {
      return sl_iter;
    }
  }

  /* No temporary space, find the item marked as last active. */
  for (SpaceLink *sl_iter = sl->next; sl_iter; sl_iter = sl_iter->next) {
    if (sl_iter->link_flag & SPACE_FLAG_TYPE_WAS_ACTIVE) {
      return sl_iter;
    }
  }

  /* If neither is found, we can just return to the regular previous one. */
  return sl->next;
}

void ed_area_prevspace(Cxt *C, ScrArea *area)
{
  SpaceLink *sl = area->spacedata.first;
  SpaceLink *prevspace = sl ? area_get_prevspace(area) : NULL;

  if (prevspace) {
    ed_area_newspace(C, area, prevspace->spacetype, false);
    /* We've exited the space, so it can't be considered temporary anymore. */
    sl->link_flag &= ~SPACE_FLAG_TYPE_TEMPORARY;
  }
  else {
    /* no change */
    return;
  }
  /* If this is a stacked fullscreen, changing to previous area exits it (meaning we're still in a
   * fullscreen, but not in a stacked one). */
  area->flag &= ~AREA_FLAG_STACKED_FULLSCREEN;

  ed_area_tag_redraw(area);

  /* send space change notifier */
  wm_event_add_notifier(C, NC_SPACE | ND_SPACE_CHANGED, area);
}

int ed_area_header_switchbutton(const bContext *C, uiBlock *block, int yco)
{
  ScrArea *area = cxt_wm_area(C);
  Screen *screen = cxt_wm_screen(C);
  ApiPtr areaptr;
  int xco = 0.4 * U.widget_unit;

  api_ptr_create(&(screen->id), &RNA_Area, area, &areaptr);

  uiDefButR(block,
            UI_BTYPE_MENU,
            0,
            "",
            xco,
            yco,
            1.6 * U.widget_unit,
            U.widget_unit,
            &areaptr,
            "ui_type",
            0,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            "");

  return xco + 1.7 * U.widget_unit;
}

/************************ standard UI regions ************************/

static ThemeColorId region_background_color_id(const Cxt *C, const ARegion *region)
{
  ScrArea *area = cxt_wm_area(C);

  switch (region->regiontype) {
    case RGN_TYPE_HEADER:
    case RGN_TYPE_TOOL_HEADER:
      if (ED_screen_area_active(C) || ED_area_is_global(area)) {
        return TH_HEADER_ACTIVE;
      }
      else {
        return TH_HEADER;
      }
    case RGN_TYPE_PREVIEW:
      return TH_PREVIEW_BACK;
    default:
      return TH_BACK;
  }
}

static void region_clear_color(const Cxt *C, const ARegion *region, ThemeColorId colorid)
{
  if (region->alignment == RGN_ALIGN_FLOAT) {
    /* handle our own drawing. */
  }
  else if (region->overlap) {
    /* view should be in pixelspace */
    ui_view2d_view_restore(C);

    float back[4];
    ui_GetThemeColor4fv(colorid, back);
    gpu_clear_color(back[3] * back[0], back[3] * back[1], back[3] * back[2], back[3]);
  }
  else {
    ui_ThemeClearColor(colorid);
  }
}

LIB_INLINE bool streq_array_any(const char *s, const char *arr[])
{
  for (uint i = 0; arr[i]; i++) {
    if (STREQ(arr[i], s)) {
      return true;
    }
  }
  return false;
}

/* Builds the panel layout for the input panel or type pt.
 *
 * param panel: The panel to draw. Can be null,
 * in which case a panel with the type of a pt will be created.
 * param unique_panel_str: A unique identifier for the name of the a uiBlock associated with the
 * panel. Used when the panel is an instanced panel so a unique identifier is needed to find the
 * correct old a uiBlock, and NULL otherwise. */
static void ed_panel_draw(const Cxt *C,
                          ARegion *region,
                          List *lb,
                          PanelType *pt,
                          Panel *panel,
                          int w,
                          int em,
                          char *unique_panel_str,
                          const char *search_filter)
{
  const uiStyle *style = ui_style_get_dpi();

  /* Draw panel. */
  char block_name[DUNE_ST_MAXNAME + INSTANCED_PANEL_UNIQUE_STR_LEN];
  strncpy(block_name, pt->idname, DUNE_ST_MAXNAME);
  if (unique_panel_str != NULL) {
    /* Instanced panels should have already been added at this point. */
    strncat(block_name, unique_panel_str, INSTANCED_PANEL_UNIQUE_STR_LEN);
  }
  uiBlock *block = UI_block_begin(C, region, block_name, UI_EMBOSS);

  bool open;
  panel = UI_panel_begin(region, lb, block, pt, panel, &open);

  const bool search_filter_active = search_filter != NULL && search_filter[0] != '\0';

  /* bad fixed values */
  int xco, yco, h = 0;
  int headerend = w - UI_UNIT_X;

  UI_panel_header_buttons_begin(panel);
  if (pt->draw_header_preset && !(pt->flag & PANEL_TYPE_NO_HEADER)) {
    /* for preset menu */
    panel->layout = UI_block_layout(block,
                                    UI_LAYOUT_HORIZONTAL,
                                    UI_LAYOUT_HEADER,
                                    0,
                                    (UI_UNIT_Y * 1.1f) + style->panelspace,
                                    UI_UNIT_Y,
                                    1,
                                    0,
                                    style);

    pt->draw_header_preset(C, panel);

    ui_block_apply_search_filter(block, search_filter);
    ui_block_layout_resolve(block, &xco, &yco);
    ui_block_translate(block, headerend - xco, 0);
    panel->layout = NULL;
  }

  if (pt->draw_header && !(pt->flag & PANEL_TYPE_NO_HEADER)) {
    int labelx, labely;
    ui_panel_label_offset(block, &labelx, &labely);

    /* Unusual case: Use expanding layout (buttons stretch to available width). */
    if (pt->flag & PANEL_TYPE_HEADER_EXPAND) {
      uiLayout *layout = UI_block_layout(block,
                                         UI_LAYOUT_VERTICAL,
                                         UI_LAYOUT_PANEL,
                                         labelx,
                                         labely,
                                         headerend - 2 * style->panelspace,
                                         1,
                                         0,
                                         style);
      panel->layout = uiLayoutRow(layout, false);
    }
    /* Regular case: Normal panel with fixed size btns. */
    else {
      panel->layout = UI_block_layout(
          block, UI_LAYOUT_HORIZONTAL, UI_LAYOUT_HEADER, labelx, labely, UI_UNIT_Y, 1, 0, style);
    }

    pt->draw_header(C, panel);

    ui_block_apply_search_filter(block, search_filter);
    ui_block_layout_resolve(block, &xco, &yco);
    panel->labelofs = xco - labelx;
    panel->layout = NULL;
  }
  else {
    panel->labelofs = 0;
  }
  ui_panel_header_btns_end(panel);

  if (open || search_filter_active) {
    short panelCxt;

    /* panel cxt can either be toolbar region or normal panels region */
    if (pt->flag & PANEL_TYPE_LAYOUT_VERT_BAR) {
      panelCxt = UI_LAYOUT_VERT_BAR;
    }
    else if (region->regiontype == RGN_TYPE_TOOLS) {
      panelCxt = UI_LAYOUT_TOOLBAR;
    }
    else {
      panelCxt = UI_LAYOUT_PANEL;
    }

    panel->layout = ui_block_layout(
        block,
        UI_LAYOUT_VERTICAL,
        panelCxt,
        (pt->flag & PANEL_TYPE_LAYOUT_VERT_BAR) ? 0 : style->panelspace,
        0,
        (pt->flag & PANEL_TYPE_LAYOUT_VERT_BAR) ? 0 : w - 2 * style->panelspace,
        em,
        0,
        style);

    pt->draw(C, panel);

    UI_block_apply_search_filter(block, search_filter);
    UI_block_layout_resolve(block, &xco, &yco);
    panel->layout = NULL;

    if (yco != 0) {
      h = -yco + 2 * style->panelspace;
    }
  }

  UI_block_end(C, block);

  /* Draw child panels. */
  if (open || search_filter_active) {
    LIST_FOREACH (LinkData *, link, &pt->children) {
      PanelType *child_pt = link->data;
      Panel *child_panel = ui_panel_find_by_type(&panel->children, child_pt);

      if (child_pt->draw && (!child_pt->poll || child_pt->poll(C, child_pt))) {
        ed_panel_draw(C,
                      region,
                      &panel->children,
                      child_pt,
                      child_panel,
                      w,
                      em,
                      unique_panel_str,
                      search_filter);
      }
    }
  }

  ui_panel_end(panel, w, h);
}

/** Check whether a panel should be added to the region's panel layout. **/
static bool panel_add_check(const duneContext *C,
                            const WorkSpace *workspace,
                            const char *contexts[],
                            const char *category_override,
                            PanelType *panel_type)
{
  /* Only add top level panels. */
  if (panel_type->parent) {
    return false;
  }
  /* Check the category override first. */
  if (category_override) {
    if (!STREQ(panel_type->category, category_override)) {
      return false;
    }
  }

  /* Verify context. */
  if (contexts != NULL && panel_type->context[0]) {
    if (!streq_array_any(panel_type->context, contexts)) {
      return false;
    }
  }

  /* If we're tagged, only use compatible. */
  if (panel_type->owner_id[0]) {
    if (!dune_workspace_owner_id_check(workspace, panel_type->owner_id)) {
      return false;
    }
  }

  if (LIKELY(panel_type->draw)) {
    if (panel_type->poll && !panel_type->poll(C, panel_type)) {
      return false;
    }
  }

  return true;
}
static bool region_uses_category_tabs(const ScrArea *area, const ARegion *region)
{
  /* Should use some better check? */
  /* For now also has hardcoded check for clip editor until it supports actual toolbar. */
  return ((1 << region->regiontype) & RGN_TYPE_HAS_CATEGORY_MASK) ||
         (region->regiontype == RGN_TYPE_TOOLS && area->spacetype == SPACE_CLIP);
}

static const char *region_panels_collect_categories(ARegion *region,
                                                    LinkNode *panel_types_stack,
                                                    bool *use_category_tabs)
{
  ui_panel_category_clear_all(region);

  /* gather unique categories */
  for (LinkNode *pt_link = panel_types_stack; pt_link; pt_link = pt_link->next) {
    PanelType *pt = pt_link->link;
    if (pt->category[0]) {
      if (!ui_panel_category_find(region, pt->category)) {
        ui_panel_category_add(region, pt->category);
      }
    }
  }

  if (ui_panel_category_is_visible(region)) {
    return ui_panel_category_active_get(region, true);
  }

  *use_category_tabs = false;
  return NULL;
}

static int panel_draw_width_from_max_width_get(const ARegion *region,
                                               const PanelType *panel_type,
                                               const int max_width)
{
  /* With a background, we want some extra padding. */
  return ui_panel_should_show_background(region, panel_type) ?
             max_width - UI_PANEL_MARGIN_X * 2.0f :
             max_width;
}

void ED_region_panels_layout_ex(const Cxt *C,
                                ARegion *region,
                                List *paneltypes,
                                const char *cxts[],
                                const char *category_override)
{
  /* collect panels to draw */
  WorkSpace *workspace = cxt_wm_workspace(C);
  LinkNode *panel_types_stack = NULL;
  LIST_FOREACH_BACKWARD (PanelType *, pt, paneltypes) {
    if (panel_add_check(C, workspace, cxts, category_override, pt)) {
      lib_linklist_prepend_alloca(&panel_types_stack, pt);
    }
  }

  region->runtime.category = NULL;

  ScrArea *area = cxt_wm_area(C);
  View2D *v2d = &region->v2d;

  bool use_category_tabs = (category_override == NULL) && region_uses_category_tabs(area, region);
  /* offset panels for small vertical tab area */
  const char *category = NULL;
  const int category_tabs_width = UI_PANEL_CATEGORY_MARGIN_WIDTH;
  int margin_x = 0;
  const bool region_layout_based = region->flag & RGN_FLAG_DYNAMIC_SIZE;
  bool update_tot_size = true;

  /* only allow scrolling in vertical direction */
  v2d->keepofs |= V2D_LOCKOFS_X | V2D_KEEPOFS_Y;
  v2d->keepofs &= ~(V2D_LOCKOFS_Y | V2D_KEEPOFS_X);
  v2d->scroll &= ~V2D_SCROLL_BOTTOM;
  v2d->scroll |= V2D_SCROLL_RIGHT;

  /* collect categories */
  if (use_category_tabs) {
    category = region_panels_collect_categories(region, panel_types_stack, &use_category_tabs);
  }
  if (use_category_tabs) {
    margin_x = category_tabs_width;
  }

  const int width_no_header = lib_rctf_size_x(&v2d->cur) - margin_x;
  /* Works out to 10 * UI_UNIT_X or 20 * UI_UNIT_X. */
  const int em = (region->type->prefsizex) ? 10 : 20;

  /* create panels */
  ui_panels_begin(C, region);

  /* Get search string for property search. */
  const char *search_filter = ED_area_region_search_filter_get(area, region);

  /* set view2d view matrix  - UI_block_begin() stores it */
  ui_view2d_view_ortho(v2d);

  bool has_instanced_panel = false;
  for (LinkNode *pt_link = panel_types_stack; pt_link; pt_link = pt_link->next) {
    PanelType *pt = pt_link->link;

    if (pt->flag & PANEL_TYPE_INSTANCED) {
      has_instanced_panel = true;
      continue;
    }
    Panel *panel = ui_panel_find_by_type(&region->panels, pt);

    if (use_category_tabs && pt->category[0] && !STREQ(category, pt->category)) {
      if ((panel == NULL) || ((panel->flag & PNL_PIN) == 0)) {
        continue;
      }
    }
    const int width = panel_draw_width_from_max_width_get(region, pt, width_no_header);

    if (panel && ui_panel_is_dragging(panel)) {
      /* Prevent View2d.tot rectangle size changes while dragging panels. */
      update_tot_size = false;
    }

    ed_panel_draw(C,
                  region,
                  &region->panels,
                  pt,
                  panel,
                  (pt->flag & PANEL_TYPE_NO_HEADER) ? width_no_header : width,
                  em,
                  NULL,
                  search_filter);
  }

  /* Draw "poly-instantiated" panels that don't have a 1 to 1 correspondence with their types. */
  if (has_instanced_panel) {
    LIST_FOREACH (Panel *, panel, &region->panels) {
      if (panel->type == NULL) {
        continue; /* Some panels don't have a type. */
      }
      if (!(panel->type->flag & PANEL_TYPE_INSTANCED)) {
        continue;
      }
      if (use_category_tabs && panel->type->category[0] &&
          !STREQ(category, panel->type->category)) {
        continue;
      }
      const int width = panel_draw_width_from_max_width_get(region, panel->type, width_no_header);

      if (panel && UI_panel_is_dragging(panel)) {
        /* Prevent View2d.tot rectangle size changes while dragging panels. */
        update_tot_size = false;
      }

      /* Use a unique identifier for instanced panels, otherwise an old block for a different
       * panel of the same type might be found. */
      char unique_panel_str[INSTANCED_PANEL_UNIQUE_STR_LEN];
      UI_list_panel_unique_str(panel, unique_panel_str);
      ed_panel_draw(C,
                    region,
                    &region->panels,
                    panel->type,
                    panel,
                    (panel->type->flag & PANEL_TYPE_NO_HEADER) ? width_no_header : width,
                    em,
                    unique_panel_str,
                    search_filter);
    }
  }

  /* align panels and return size */
  int x, y;
  UI_panels_end(C, region, &x, &y);

  /* before setting the view */
  if (region_layout_based) {
    /* only single panel support at the moment.
     * Can't use x/y values calculated above because they're not using the real height of panels,
     * instead they calculate offsets for the next panel to start drawing. */
    Panel *panel = region->panels.last;
    if (panel != NULL) {
      const int size_dyn[2] = {
          UI_UNIT_X * (UI_panel_is_closed(panel) ? 8 : 14) / UI_DPI_FAC,
          UI_panel_size_y(panel) / UI_DPI_FAC,
      };
      /* region size is layout based and needs to be updated */
      if ((region->sizex != size_dyn[0]) || (region->sizey != size_dyn[1])) {
        region->sizex = size_dyn[0];
        region->sizey = size_dyn[1];
        area->flag |= AREA_FLAG_REGION_SIZE_UPDATE;
      }
      y = fabsf(region->sizey * UI_DPI_FAC - 1);
    }
  }
  else {
    /* We always keep the scroll offset -
     * so the total view gets increased with the scrolled away part. */
    if (v2d->cur.ymax < -FLT_EPSILON) {
      /* Clamp to lower view boundary */
      if (v2d->tot.ymin < -v2d->winy) {
        y = min_ii(y, 0);
      }
      else {
        y = min_ii(y, v2d->cur.ymin);
      }
    }

    y = -y;
  }

  UI_blocklist_update_view_for_buttons(C, &region->uiblocks);

  if (update_tot_size) {
    /* this also changes the 'cur' */
    UI_view2d_totRect_set(v2d, x, y);
  }

  if (use_category_tabs) {
    region->runtime.category = category;
  }
}

void ED_region_panels_layout(const bContext *C, ARegion *region)
{
  ED_region_panels_layout_ex(C, region, &region->type->paneltypes, NULL, NULL);
}

void ED_region_panels_draw(const bContext *C, ARegion *region)
{
  View2D *v2d = &region->v2d;

  if (region->alignment != RGN_ALIGN_FLOAT) {
    region_clear_color(
        C, region, (region->type->regionid == RGN_TYPE_PREVIEW) ? TH_PREVIEW_BACK : TH_BACK);
  }

  /* reset line width for drawing tabs */
  gpu_line_width(1.0f);

  /* set the view */
  UI_view2d_view_ortho(v2d);

  /* View2D matrix might have changed due to dynamic sized regions. */
  UI_blocklist_update_window_matrix(C, &region->uiblocks);

  /* draw panels */
  UI_panels_draw(C, region);

  /* restore view matrix */
  UI_view2d_view_restore(C);

  /* Set in layout. */
  if (region->runtime.category) {
    UI_panel_category_draw_all(region, region->runtime.category);
  }

  /* scrollers */
  bool use_mask = false;
  rcti mask;
  if (region->runtime.category &&
      (RGN_ALIGN_ENUM_FROM_MASK(region->alignment) == RGN_ALIGN_RIGHT)) {
    use_mask = true;
    UI_view2d_mask_from_win(v2d, &mask);
    mask.xmax -= UI_PANEL_CATEGORY_MARGIN_WIDTH;
  }
  ui_view2d_scrollers_draw(v2d, use_mask ? &mask : NULL);
}

void ed_region_panels_ex(const Cxt *C, ARegion *region, const char *contexts[])
{
  /* TODO: remove? */
  ED_region_panels_layout_ex(C, region, &region->type->paneltypes, contexts, NULL);
  ED_region_panels_draw(C, region);
}

void ED_region_panels(const Cxt *C, ARegion *region)
{
  /* TODO: remove? */
  ed_region_panels_layout(C, region);
  ed_region_panels_draw(C, region);
}

void ed_region_panels_init(WindowManager *wm, ARegion *region)
{
  ui_view2d_region_reinit(&region->v2d, V2D_COMMONVIEW_PANELS_UI, region->winx, region->winy);

  wmKeyMap *keymap = wm_keymap_ensure(wm->defaultconf, "View2D Buttons List", 0, 0);
  wm_event_add_keymap_handler(&region->handlers, keymap);
}

/* Check whether any of the buttons generated by the a panel_type's
 * layout cbs match the a search_filter.
 *
 * param panel: If non-NULL, use this instead of adding a new panel for the a panel_type */
static bool panel_prop_search(const Cxt *C,
                                  ARegion *region,
                                  const uiStyle *style,
                                  Panel *panel,
                                  PanelType *panel_type,
                                  const char *search_filter)
{
  uiBlock *block = ui_block_begin(C, region, panel_type->idname, UI_EMBOSS);
  ui_block_set_search_only(block, true);

  /* Skip panels that give meaningless search results. */
  if (panel_type->flag & PANEL_TYPE_NO_SEARCH) {
    return false;
  }

  if (panel == NULL) {
    bool open; /* Dummy variable. */
    panel = ui_panel_begin(region, &region->panels, block, panel_type, panel, &open);
  }

  /* Build the layouts. Because they are only used for search,
   * they don't need any of the proper style or layout information. */
  if (panel->type->draw_header_preset != NULL) {
    panel->layout = UI_block_layout(
        block, UI_LAYOUT_HORIZONTAL, UI_LAYOUT_HEADER, 0, 0, 0, 0, 0, style);
    panel_type->draw_header_preset(C, panel);
  }
  if (panel->type->draw_header != NULL) {
    panel->layout = UI_block_layout(
        block, UI_LAYOUT_HORIZONTAL, UI_LAYOUT_HEADER, 0, 0, 0, 0, 0, style);
    panel_type->draw_header(C, panel);
  }
  if (LIKELY(panel->type->draw != NULL)) {
    panel->layout = ui_block_layout(
        block, UI_LAYOUT_VERTICAL, UI_LAYOUT_PANEL, 0, 0, 0, 0, 0, style);
    panel_type->draw(C, panel);
  }

  ui_block_layout_free(block);

  /* We could check after each layout to increase the likelihood of returning early,
   * but that probably wouldn't make much of a difference anyway. */
  if (ui_block_apply_search_filter(block, search_filter)) {
    return true;
  }

  LIST_FOREACH (LinkData *, link, &panel_type->children) {
    PanelType *panel_type_child = link->data;
    if (!panel_type_child->poll || panel_type_child->poll(C, panel_type_child)) {
      /* Search for the existing child panel here because it might be an instanced
       * child panel with a custom data field that will be needed to build the layout. */
      Panel *child_panel = ui_panel_find_by_type(&panel->children, panel_type_child);
      if (panel_prop_search(C, region, style, child_panel, panel_type_child, search_filter)) {
        return true;
      }
    }
  }

  return false;
}

bool ED_region_prop_search(const duneContext *C,
                               ARegion *region,
                               ListBase *paneltypes,
                               const char *contexts[],
                               const char *category_override)
{
  ScrArea *area = cxt_wm_area(C);
  WorkSpace *workspace = cxt_wm_workspace(C);
  const uiStyle *style = ui_style_get_dpi();
  const char *search_filter = ed_area_region_search_filter_get(area, region);

  LinkNode *panel_types_stack = NULL;
  LIST_FOREACH_BACKWARD (PanelType *, pt, paneltypes) {
    if (panel_add_check(C, workspace, cxts, category_override, pt)) {
      lib_linklist_prepend_alloca(&panel_types_stack, pt);
    }
  }

  const char *category = NULL;
  bool use_category_tabs = (category_override == NULL) && region_uses_category_tabs(area, region);
  if (use_category_tabs) {
    category = region_panels_collect_categories(region, panel_types_stack, &use_category_tabs);
  }

  /* Run prop search for each panel, stopping if a result is found. */
  bool has_result = true;
  bool has_instanced_panel = false;
  for (LinkNode *pt_link = panel_types_stack; pt_link; pt_link = pt_link->next) {
    PanelType *panel_type = pt_link->link;
    /* Note that these checks are duplicated from #ED_region_panels_layout_ex. */
    if (panel_type->flag & PANEL_TYPE_INSTANCED) {
      has_instanced_panel = true;
      continue;
    }

    if (use_category_tabs) {
      if (panel_type->category[0] && !STREQ(category, panel_type->category)) {
        continue;
      }
    }

    /* We start prop search with an empty panel list, so there's
     * no point in trying to find an existing panel with this type. */
    has_result = panel_prop_search(C, region, style, NULL, panel_type, search_filter);
    if (has_result) {
      break;
    }
  }

  /* Run prop search for instanced panels (created in the layout calls of previous panels). */
  if (!has_result && has_instanced_panel) {
    LIST_FOREACH (Panel *, panel, &region->panels) {
      /* Note that these checks are duplicated from ED_region_panels_layout_ex. */
      if (panel->type == NULL || !(panel->type->flag & PANEL_TYPE_INSTANCED)) {
        continue;
      }
      if (use_category_tabs) {
        if (panel->type->category[0] && !STREQ(category, panel->type->category)) {
          continue;
        }
      }

      has_result = panel_prop_search(C, region, style, panel, panel->type, search_filter);
      if (has_result) {
        break;
      }
    }
  }

  /* Free the panels and blocks, as they are only used for search. */
  UI_blocklist_free(C, region);
  UI_panels_free_instanced(C, region);
  dune_area_region_panels_free(&region->panels);

  return has_result;
}

void ed_region_header_layout(const Cxt *C, ARegion *region)
{
  const uiStyle *style = ui_style_get_dpi();
  bool region_layout_based = region->flag & RGN_FLAG_DYNAMIC_SIZE;

  /* Height of buttons and scaling needed to achieve it. */
  const int btny = min_ii(UI_UNIT_Y, region->winy - 2 * UI_DPI_FAC);
  const float btny_scale = btny / (float)UI_UNIT_Y;

  /* Vertically center buttons. */
  int xco = UI_HEADER_OFFSET;
  int yco = btny + (region->winy - btny) / 2;
  int maxco = xco;

  /* workaround for 1 px alignment issue. Not sure what causes it...
   * Would prefer a proper fix - Julian */
  if (!ELEM(cxt_wm_area(C)->spacetype, SPACE_TOPBAR, SPACE_STATUSBAR)) {
    yco -= 1;
  }

  /* set view2d view matrix for scrolling (without scrollers) */
  UI_view2d_view_ortho(&region->v2d);

  /* draw all headers types */
  LIST_FOREACH (HeaderType *, ht, &region->type->headertypes) {
    if (ht->poll && !ht->poll(C, ht)) {
      continue;
    }

    uiBlock *block = UI_block_begin(C, region, ht->idname, UI_EMBOSS);
    uiLayout *layout = UI_block_layout(
        block, UI_LAYOUT_HORIZONTAL, UI_LAYOUT_HEADER, xco, yco, buttony, 1, 0, style);

    if (buttony_scale != 1.0f) {
      uiLayoutSetScaleY(layout, buttony_scale);
    }

    Header header = {NULL};
    if (ht->draw) {
      header.type = ht;
      header.layout = layout;
      ht->draw(C, &header);
      if (ht->next) {
        uiItemS(layout);
      }

      /* for view2d */
      xco = uiLayoutGetWidth(layout);
      if (xco > maxco) {
        maxco = xco;
      }
    }

    UI_block_layout_resolve(block, &xco, &yco);

    /* for view2d */
    if (xco > maxco) {
      maxco = xco;
    }

    int new_sizex = (maxco + UI_HEADER_OFFSET) / UI_DPI_FAC;

    if (region_layout_based && (region->sizex != new_sizex)) {
      /* region size is layout based and needs to be updated */
      ScrArea *area = cxt_wm_area(C);

      region->sizex = new_sizex;
      area->flag |= AREA_FLAG_REGION_SIZE_UPDATE;
    }

    ui_block_end(C, block);

    /* In most cases there is only ever one header, it never makes sense to draw more than one
     * header in the same region, this results in overlapping buttons, see: T60195. */
    break;
  }

  if (!region_layout_based) {
    maxco += UI_HEADER_OFFSET;
  }

  /* Always as last. */
  ui_view2d_totRect_set(&region->v2d, maxco, region->winy);

  /* Restore view matrix. */
  ui_view2d_view_restore(C);
}

void ed_region_header_draw(const Cxt *C, ARegion *region)
{
  /* clear */
  region_clear_color(C, region, region_background_color_id(C, region));

  UI_view2d_view_ortho(&region->v2d);

  /* View2D matrix might have changed due to dynamic sized regions. */
  UI_blocklist_update_window_matrix(C, &region->uiblocks);

  /* draw blocks */
  UI_blocklist_draw(C, &region->uiblocks);

  /* restore view matrix */
  UI_view2d_view_restore(C);
}

void ED_region_header(const bContext *C, ARegion *region)
{
  /* TODO: remove? */
  ED_region_header_layout(C, region);
  ED_region_header_draw(C, region);
}

void ED_region_header_init(ARegion *region)
{
  UI_view2d_region_reinit(&region->v2d, V2D_COMMONVIEW_HEADER, region->winx, region->winy);
}

int ED_area_headersize(void)
{
  /* Accommodate widget and padding. */
  return U.widget_unit + (int)(UI_DPI_FAC * HEADER_PADDING_Y);
}

int ED_area_footersize(void)
{
  return ED_area_headersize();
}

int ED_area_global_size_y(const ScrArea *area)
{
  lib_assert(ED_area_is_global(area));
  return round_fl_to_int(area->global->cur_fixed_height * UI_DPI_FAC);
}
int ED_area_global_min_size_y(const ScrArea *area)
{
  LIB_assert(ED_area_is_global(area));
  return round_fl_to_int(area->global->size_min * UI_DPI_FAC);
}
int ED_area_global_max_size_y(const ScrArea *area)
{
  LIB_assert(ED_area_is_global(area));
  return round_fl_to_int(area->global->size_max * UI_DPI_FAC);
}

bool ED_area_is_global(const ScrArea *area)
{
  return area->global != NULL;
}

ScrArea *ED_area_find_under_cursor(const duneContext *C, int spacetype, const int xy[2])
{
  duneScreen *screen = CTX_wm_screen(C);
  wmWindow *win = CTX_wm_window(C);

  ScrArea *area = NULL;

  if (win->parent) {
    /* If active window is a child, check itself first. */
    area = DUNE_screen_find_area_xy(screen, spacetype, xy);
  }

  if (!area) {
    /* Check all windows except the active one. */
    int scr_pos[2];
    wmWindow *r_win = WM_window_find_under_cursor(win, xy, scr_pos);
    if (r_win && r_win != win) {
      win = r_win;
      screen = WM_window_get_active_screen(win);
      area = DUNE_screen_find_area_xy(screen, spacetype, scr_pos);
    }
  }

  if (!area && !win->parent) {
    /* If active window is a parent window, check itself last. */
    area = DUNE_screen_find_area_xy(screen, spacetype, xy);
  }

  return area;
}

ScrArea *ED_screen_areas_iter_first(const wmWindow *win, const bScreen *screen)
{
  ScrArea *global_area = win->global_areas.areabase.first;

  if (!global_area) {
    return screen->areabase.first;
  }
  if ((global_area->global->flag & GLOBAL_AREA_IS_HIDDEN) == 0) {
    return global_area;
  }
  /* Find next visible area. */
  return ED_screen_areas_iter_next(screen, global_area);
}
ScrArea *ED_screen_areas_iter_next(const bScreen *screen, const ScrArea *area)
{
  if (area->global == NULL) {
    return area->next;
  }

  for (ScrArea *area_iter = area->next; area_iter; area_iter = area_iter->next) {
    if ((area_iter->global->flag & GLOBAL_AREA_IS_HIDDEN) == 0) {
      return area_iter;
    }
  }
  /* No visible next global area found, start iterating over layout areas. */
  return screen->areabase.first;
}

int ED_region_global_size_y(void)
{
  return ED_area_headersize(); /* same size as header */
}

void ED_region_info_draw_multiline(ARegion *region,
                                   const char *text_array[],
                                   float fill_color[4],
                                   const bool full_redraw)
{
  const int header_height = UI_UNIT_Y;
  const uiStyle *style = UI_style_get_dpi();
  int fontid = style->widget.uifont_id;
  int scissor[4];
  int num_lines = 0;

  /* background box */
  rcti rect = *ED_region_visible_rect(region);

  /* Box fill entire width or just around text. */
  if (!full_redraw) {
    const char **text = &text_array[0];
    while (*text) {
      rect.xmax = min_ii(rect.xmax,
                         rect.xmin + BLF_width(fontid, *text, BLF_DRAW_STR_DUMMY_MAX) +
                             1.2f * U.widget_unit);
      text++;
      num_lines++;
    }
  }
  /* Just count the line number. */
  else {
    const char **text = &text_array[0];
    while (*text) {
      text++;
      num_lines++;
    }
  }

  rect.ymin = rect.ymax - header_height * num_lines;

  /* setup scissor */
  GPU_scissor_get(scissor);
  GPU_scissor(rect.xmin, rect.ymin, BLI_rcti_size_x(&rect) + 1, BLI_rcti_size_y(&rect) + 1);

  GPU_blend(GPU_BLEND_ALPHA);
  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_I32, 2, GPU_FETCH_INT_TO_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
  immUniformColor4fv(fill_color);
  immRecti(pos, rect.xmin, rect.ymin, rect.xmax + 1, rect.ymax + 1);
  immUnbindProgram();
  GPU_blend(GPU_BLEND_NONE);

  /* text */
  UI_FontThemeColor(fontid, TH_TEXT_HI);
  BLF_clipping(fontid, rect.xmin, rect.ymin, rect.xmax, rect.ymax);
  BLF_enable(fontid, BLF_CLIPPING);
  int offset = num_lines - 1;
  {
    const char **text = &text_array[0];
    while (*text) {
      BLF_position(fontid,
                   rect.xmin + 0.6f * U.widget_unit,
                   rect.ymin + 0.3f * U.widget_unit + offset * header_height,
                   0.0f);
      BLF_draw(fontid, *text, BLF_DRAW_STR_DUMMY_MAX);
      text++;
      offset--;
    }
  }

  BLF_disable(fontid, BLF_CLIPPING);

  /* restore scissor as it was before */
  GPU_scissor(scissor[0], scissor[1], scissor[2], scissor[3]);
}

void ED_region_info_draw(ARegion *region,
                         const char *text,
                         float fill_color[4],
                         const bool full_redraw)
{
  const char *text_array[2] = {text, NULL};
  ED_region_info_draw_multiline(region, text_array, fill_color, full_redraw);
}

typedef struct MetadataPanelDrawContext {
  uiLayout *layout;
} MetadataPanelDrawContext;

static void metadata_panel_draw_field(const char *field, const char *value, void *ctx_v)
{
  MetadataPanelDrawContext *ctx = (MetadataPanelDrawContext *)ctx_v;
  uiLayout *row = uiLayoutRow(ctx->layout, false);
  uiItemL(row, field, ICON_NONE);
  uiItemL(row, value, ICON_NONE);
}

void ED_region_image_metadata_panel_draw(ImBuf *ibuf, uiLayout *layout)
{
  MetadataPanelDrawContext ctx;
  ctx.layout = layout;
  IMB_metadata_foreach(ibuf, metadata_panel_draw_field, &ctx);
}

void ED_region_grid_draw(ARegion *region, float zoomx, float zoomy, float x0, float y0)
{
  /* the image is located inside (x0, y0), (x0+1, y0+1) as set by view2d */
  int x1, y1, x2, y2;
  UI_view2d_view_to_region(&region->v2d, x0, y0, &x1, &y1);
  UI_view2d_view_to_region(&region->v2d, x0 + 1.0f, y0 + 1.0f, &x2, &y2);

  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  float gridcolor[4];
  UI_GetThemeColor4fv(TH_GRID, gridcolor);

  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
  /* To fake alpha-blending, color shading is reduced when alpha is nearing 0. */
  immUniformThemeColorBlendShade(TH_BACK, TH_GRID, gridcolor[3], 20 * gridcolor[3]);
  immRectf(pos, x1, y1, x2, y2);
  immUnbindProgram();

  /* gridsize adapted to zoom level */
  float gridsize = 0.5f * (zoomx + zoomy);
  float gridstep = 1.0f / 32.0f;
  if (gridsize <= 0.0f) {
    return;
  }

  if (gridsize < 1.0f) {
    while (gridsize < 1.0f) {
      gridsize *= 4.0f;
      gridstep *= 4.0f;
    }
  }
  else {
    while (gridsize >= 4.0f) {
      gridsize /= 4.0f;
      gridstep /= 4.0f;
    }
  }

  float blendfac = 0.25f * gridsize - floorf(0.25f * gridsize);
  CLAMP(blendfac, 0.0f, 1.0f);

  int count_fine = 1.0f / gridstep;
  int count_large = 1.0f / (4.0f * gridstep);

  if (count_fine > 0) {
    GPU_vertformat_clear(format);
    pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    uint color = GPU_vertformat_attr_add(format, "color", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);

    immBindBuiltinProgram(GPU_SHADER_2D_FLAT_COLOR);
    immBegin(GPU_PRIM_LINES, 4 * count_fine + 4 * count_large);

    float theme_color[3];
    UI_GetThemeColorShade3fv(TH_GRID, (int)(20.0f * (1.0f - blendfac)), theme_color);
    float fac = 0.0f;

    /* the fine resolution level */
    for (int i = 0; i < count_fine; i++) {
      immAttr3fv(color, theme_color);
      immVertex2f(pos, x1, y1 * (1.0f - fac) + y2 * fac);
      immAttr3fv(color, theme_color);
      immVertex2f(pos, x2, y1 * (1.0f - fac) + y2 * fac);
      immAttr3fv(color, theme_color);
      immVertex2f(pos, x1 * (1.0f - fac) + x2 * fac, y1);
      immAttr3fv(color, theme_color);
      immVertex2f(pos, x1 * (1.0f - fac) + x2 * fac, y2);
      fac += gridstep;
    }

    if (count_large > 0) {
      UI_GetThemeColor3fv(TH_GRID, theme_color);
      fac = 0.0f;

      /* the large resolution level */
      for (int i = 0; i < count_large; i++) {
        immAttr3fv(color, theme_color);
        immVertex2f(pos, x1, y1 * (1.0f - fac) + y2 * fac);
        immAttr3fv(color, theme_color);
        immVertex2f(pos, x2, y1 * (1.0f - fac) + y2 * fac);
        immAttr3fv(color, theme_color);
        immVertex2f(pos, x1 * (1.0f - fac) + x2 * fac, y1);
        immAttr3fv(color, theme_color);
        immVertex2f(pos, x1 * (1.0f - fac) + x2 * fac, y2);
        fac += 4.0f * gridstep;
      }
    }

    immEnd();
    immUnbindProgram();
  }
}

/* If the area has overlapping regions, it returns visible rect for Region *region */
/* rect gets returned in local region coordinates */
static void region_visible_rect_calc(ARegion *region, rcti *rect)
{
  ARegion *region_iter = region;

  /* allow function to be called without area */
  while (region_iter->prev) {
    region_iter = region_iter->prev;
  }

  *rect = region->winrct;

  /* check if a region overlaps with the current one */
  for (; region_iter; region_iter = region_iter->next) {
    if (region != region_iter && region_iter->overlap) {
      if (LIB_rcti_isect(rect, &region_iter->winrct, NULL)) {
        int alignment = RGN_ALIGN_ENUM_FROM_MASK(region_iter->alignment);

        if (ELEM(alignment, RGN_ALIGN_LEFT, RGN_ALIGN_RIGHT)) {
          /* Overlap left, also check 1 pixel offset (2 regions on one side). */
          if (abs(rect->xmin - region_iter->winrct.xmin) < 2) {
            rect->xmin = region_iter->winrct.xmax;
          }

          /* Overlap right. */
          if (abs(rect->xmax - region_iter->winrct.xmax) < 2) {
            rect->xmax = region_iter->winrct.xmin;
          }
        }
        else if (ELEM(alignment, RGN_ALIGN_TOP, RGN_ALIGN_BOTTOM)) {
          /* Same logic as above for vertical regions. */
          if (abs(rect->ymin - region_iter->winrct.ymin) < 2) {
            rect->ymin = region_iter->winrct.ymax;
          }
          if (abs(rect->ymax - region_iter->winrct.ymax) < 2) {
            rect->ymax = region_iter->winrct.ymin;
          }
        }
        else if (alignment == RGN_ALIGN_FLOAT) {
          /* Skip floating. */
        }
        else {
          LIB_assert_msg(0, "Region overlap with unknown alignment");
        }
      }
    }
  }
  LIB_rcti_translate(rect, -region->winrct.xmin, -region->winrct.ymin);
}

const rcti *ED_region_visible_rect(ARegion *region)
{
  rcti *rect = &region->runtime.visible_rect;
  if (rect->xmin == 0 && rect->ymin == 0 && rect->xmax == 0 && rect->ymax == 0) {
    region_visible_rect_calc(region, rect);
  }
  return rect;
}

/* Cache display helpers */

void ED_region_cache_draw_background(ARegion *region)
{
  /* Local coordinate visible rect inside region, to accommodate overlapping ui. */
  const rcti *rect_visible = ED_region_visible_rect(region);
  const int region_bottom = rect_visible->ymin;

  uint pos = GPU_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_I32, 2, GPU_FETCH_INT_TO_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
  immUniformColor4ub(128, 128, 255, 64);
  immRecti(pos, 0, region_bottom, region->winx, region_bottom + 8 * UI_DPI_FAC);
  immUnbindProgram();
}

void ED_region_cache_draw_curfra_label(const int framenr, const float x, const float y)
{
  const uiStyle *style = UI_style_get();
  int fontid = style->widget.uifont_id;
  char numstr[32];
  float font_dims[2] = {0.0f, 0.0f};

  /* frame number */
  BLF_size(fontid, 11.0f * U.pixelsize, U.dpi);
  LIB_snprintf(numstr, sizeof(numstr), "%d", framenr);

  BLF_width_and_height(fontid, numstr, sizeof(numstr), &font_dims[0], &font_dims[1]);

  uint pos = GPU_vertformat_attr_add(
      immVertexFormat(), "pos", GPU_COMP_I32, 2, GPU_FETCH_INT_TO_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
  immUniformThemeColor(TH_CFRAME);
  immRecti(pos, x, y, x + font_dims[0] + 6.0f, y + font_dims[1] + 4.0f);
  immUnbindProgram();

  UI_FontThemeColor(fontid, TH_TEXT);
  BLF_position(fontid, x + 2.0f, y + 2.0f, 0.0f);
  BLF_draw(fontid, numstr, sizeof(numstr));
}

void ED_region_cache_draw_cached_segments(
    ARegion *region, const int num_segments, const int *points, const int sfra, const int efra)
{
  if (num_segments) {
    /* Local coordinate visible rect inside region, to accommodate overlapping ui. */
    const rcti *rect_visible = ED_region_visible_rect(region);
    const int region_bottom = rect_visible->ymin;

    uint pos = GPU_vertformat_attr_add(
        immVertexFormat(), "pos", GPU_COMP_I32, 2, GPU_FETCH_INT_TO_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
    immUniformColor4ub(128, 128, 255, 128);

    for (int a = 0; a < num_segments; a++) {
      float x1 = (float)(points[a * 2] - sfra) / (efra - sfra + 1) * region->winx;
      float x2 = (float)(points[a * 2 + 1] - sfra + 1) / (efra - sfra + 1) * region->winx;

      immRecti(pos, x1, region_bottom, x2, region_bottom + 8 * UI_DPI_FAC);
      /* TODO: use primitive restart to draw multiple rects more efficiently */
    }

    immUnbindProgram();
  }
}

void ED_region_message_subscribe(wmRegionMessageSubscribeParams *params)
{
  ARegion *region = params->region;
  const dineContext *C = params->context;
  struct wmMsgBus *mbus = params->message_bus;

  if (region->gizmo_map != NULL) {
    WM_gizmomap_message_subscribe(C, region->gizmo_map, region, mbus);
  }

  if (!LIB_listbase_is_empty(&region->uiblocks)) {
    UI_region_message_subscribe(region, mbus);
  }

  if (region->type->message_subscribe != NULL) {
    region->type->message_subscribe(params);
  }
}

int ED_region_snap_size_test(const ARegion *region)
{
  /* Use a larger value because toggling scrollbars can jump in size. */
  const int snap_match_threshold = 16;
  if (region->type->snap_size != NULL) {
    return ((((region->sizex - region->type->snap_size(region, region->sizex, 0)) <=
              snap_match_threshold)
             << 0) |
            (((region->sizey - region->type->snap_size(region, region->sizey, 1)) <=
              snap_match_threshold)
             << 1));
  }
  return 0;
}

bool ED_region_snap_size_apply(ARegion *region, int snap_flag)
{
  bool changed = false;
  if (region->type->snap_size != NULL) {
    if (snap_flag & (1 << 0)) {
      short snap_size = region->type->snap_size(region, region->sizex, 0);
      if (snap_size != region->sizex) {
        region->sizex = snap_size;
        changed = true;
      }
    }
    if (snap_flag & (1 << 1)) {
      short snap_size = region->type->snap_size(region, region->sizey, 1);
      if (snap_size != region->sizey) {
        region->sizey = snap_size;
        changed = true;
      }
    }
  }
  return changed;
}
