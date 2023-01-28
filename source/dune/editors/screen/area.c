#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_userdef_types.h"

#include "BLI_blenlib.h"
#include "BLI_linklist_stack.h"
#include "BLI_math.h"
#include "BLI_rand.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_screen.h"
#include "BKE_workspace.h"

#include "RNA_access.h"
#include "RNA_types.h"

#include "WM_api.h"
#include "WM_message.h"
#include "WM_toolsystem.h"
#include "WM_types.h"

#include "ED_buttons.h"
#include "ED_screen.h"
#include "ED_screen_types.h"
#include "ED_space_api.h"
#include "ED_time_scrub_ui.h"

#include "GPU_framebuffer.h"
#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_matrix.h"
#include "GPU_state.h"

#include "BLF_api.h"

#include "IMB_imbuf_types.h"
#include "IMB_metadata.h"

#include "UI_interface.h"
#include "UI_interface_icons.h"
#include "UI_resources.h"
#include "UI_view2d.h"

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
  GPU_blend(GPU_BLEND_ALPHA);

  float color[4] = {0.0f, 0.0f, 0.0f, 0.25f};
  UI_GetThemeColor3fv(TH_EDITOR_OUTLINE, color);

  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
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

  GPU_blend(GPU_BLEND_NONE);
}

void ED_region_pixelspace(const ARegion *region)
{
  wmOrtho2_region_pixelspace(region);
  GPU_matrix_identity_set();
}

void ED_region_do_listen(wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  wmNotifier *notifier = params->notifier;

  /* generic notes first */
  switch (notifier->category) {
    case NC_WM:
      if (notifier->data == ND_FILEREAD) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_WINDOW:
      ED_region_tag_redraw(region);
      break;
  }

  if (region->type && region->type->listener) {
    region->type->listener(params);
  }

  LISTBASE_FOREACH (uiList *, list, &region->ui_lists) {
    if (list->type && list->type->listener) {
      list->type->listener(list, params);
    }
  }
}

void ED_area_do_listen(wmSpaceTypeListenerParams *params)
{
  /* no generic notes? */
  if (params->area->type && params->area->type->listener) {
    params->area->type->listener(params);
  }
}

void ED_area_do_refresh(bContext *C, ScrArea *area)
{
  /* no generic notes? */
  if (area->type && area->type->refresh) {
    area->type->refresh(C, area);
  }
  area->do_refresh = false;
}

/**
 * \brief Corner widget use for quitting fullscreen.
 */
static void area_draw_azone_fullscreen(
    short UNUSED(x1), short UNUSED(y1), short x2, short y2, float alpha)
{
  UI_icon_draw_ex(x2 - U.widget_unit,
                  y2 - U.widget_unit,
                  ICON_FULLSCREEN_EXIT,
                  U.inv_dpi_fac,
                  min_ff(alpha, 0.75f),
                  0.0f,
                  NULL,
                  false);
}

/**
 * \brief Corner widgets use for dragging and splitting the view.
 */
static void area_draw_azone(short UNUSED(x1), short UNUSED(y1), short UNUSED(x2), short UNUSED(y2))
{
  /* No drawing needed since all corners are action zone, and visually distinguishable. */
}

/**
 * \brief Edge widgets to show hidden panels such as the toolbar and headers.
 */
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
      BLI_assert(0);
      return;
  }

  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  GPU_blend(GPU_BLEND_ALPHA);
  /* NOTE(fclem): There is something strange going on with Mesa and GPU_SHADER_2D_UNIFORM_COLOR
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
  GPU_blend(GPU_BLEND_NONE);
}

static void region_draw_azone_tab_arrow(ScrArea *area, ARegion *region, AZone *az)
{
  GPU_blend(GPU_BLEND_ALPHA);

  /* add code to draw region hidden as 'too small' */
  switch (az->edge) {
    case AE_TOP_TO_BOTTOMRIGHT:
      UI_draw_roundbox_corner_set(UI_CNR_TOP_LEFT | UI_CNR_TOP_RIGHT);
      break;
    case AE_BOTTOM_TO_TOPLEFT:
      UI_draw_roundbox_corner_set(UI_CNR_BOTTOM_RIGHT | UI_CNR_BOTTOM_LEFT);
      break;
    case AE_LEFT_TO_TOPRIGHT:
      UI_draw_roundbox_corner_set(UI_CNR_TOP_LEFT | UI_CNR_BOTTOM_LEFT);
      break;
    case AE_RIGHT_TO_TOPLEFT:
      UI_draw_roundbox_corner_set(UI_CNR_TOP_RIGHT | UI_CNR_BOTTOM_RIGHT);
      break;
  }

  /* Workaround for different color spaces between normal areas and the ones using GPUViewports. */
  float alpha = WM_region_use_viewport(area, region) ? 0.6f : 0.4f;
  const float color[4] = {0.05f, 0.05f, 0.05f, alpha};
  UI_draw_roundbox_aa(
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

  GPU_line_width(1.0f);
  GPU_blend(GPU_BLEND_ALPHA);

  GPU_matrix_push();
  GPU_matrix_translate_2f(-region->winrct.xmin, -region->winrct.ymin);

  LISTBASE_FOREACH (AZone *, az, &area->actionzones) {
    /* test if action zone is over this region */
    rcti azrct;
    BLI_rcti_init(&azrct, az->x1, az->x2, az->y1, az->y2);

    if (BLI_rcti_isect(&region->drawrct, &azrct, NULL)) {
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

  GPU_matrix_pop();

  GPU_blend(GPU_BLEND_NONE);
}

static void region_draw_status_text(ScrArea *area, ARegion *region)
{
  bool overlap = ED_region_is_overlap(area->spacetype, region->regiontype);

  if (overlap) {
    GPU_clear_color(0.0f, 0.0f, 0.0f, 0.0f);
  }
  else {
    UI_ThemeClearColor(TH_HEADER);
  }

  int fontid = BLF_set_default();

  const float width = BLF_width(fontid, region->headerstr, BLF_DRAW_STR_DUMMY_MAX);
  const float x = UI_UNIT_X;
  const float y = 0.4f * UI_UNIT_Y;

  if (overlap) {
    const float pad = 2.0f * UI_DPI_FAC;
    const float x1 = x - (UI_UNIT_X - pad);
    const float x2 = x + width + (UI_UNIT_X - pad);
    const float y1 = pad;
    const float y2 = region->winy - pad;

    GPU_blend(GPU_BLEND_ALPHA);

    float color[4] = {0.0f, 0.0f, 0.0f, 0.5f};
    UI_GetThemeColor3fv(TH_BACK, color);
    UI_draw_roundbox_corner_set(UI_CNR_ALL);
    UI_draw_roundbox_aa(
        &(const rctf){
            .xmin = x1,
            .xmax = x2,
            .ymin = y1,
            .ymax = y2,
        },
        true,
        4.0f,
        color);

    UI_FontThemeColor(fontid, TH_TEXT);
  }
  else {
    UI_FontThemeColor(fontid, TH_TEXT);
  }

  BLF_position(fontid, x, y, 0.0f);
  BLF_draw(fontid, region->headerstr, BLF_DRAW_STR_DUMMY_MAX);
}

void ED_region_do_msg_notify_tag_redraw(
    /* Follow wmMsgNotifyFn spec */
    bContext *UNUSED(C),
    wmMsgSubscribeKey *UNUSED(msg_key),
    wmMsgSubscribeValue *msg_val)
{
  ARegion *region = msg_val->owner;
  ED_region_tag_redraw(region);

  /* This avoids _many_ situations where header/properties control display settings.
   * the common case is space properties in the header */
  if (ELEM(region->regiontype, RGN_TYPE_HEADER, RGN_TYPE_TOOL_HEADER, RGN_TYPE_UI)) {
    while (region && region->prev) {
      region = region->prev;
    }
    for (; region; region = region->next) {
      if (ELEM(region->regiontype, RGN_TYPE_WINDOW, RGN_TYPE_CHANNELS)) {
        ED_region_tag_redraw(region);
      }
    }
  }
}

void ED_area_do_msg_notify_tag_refresh(
    /* Follow wmMsgNotifyFn spec */
    bContext *UNUSED(C),
    wmMsgSubscribeKey *UNUSED(msg_key),
    wmMsgSubscribeValue *msg_val)
{
  ScrArea *area = msg_val->user_data;
  ED_area_tag_refresh(area);
}

void ED_area_do_mgs_subscribe_for_tool_header(const wmRegionMessageSubscribeParams *params)
{
  struct wmMsgBus *mbus = params->message_bus;
  WorkSpace *workspace = params->workspace;
  ARegion *region = params->region;

  BLI_assert(region->regiontype == RGN_TYPE_TOOL_HEADER);
  wmMsgSubscribeValue msg_sub_value_region_tag_redraw = {
      .owner = region,
      .user_data = region,
      .notify = ED_region_do_msg_notify_tag_redraw,
  };
  WM_msg_subscribe_rna_prop(
      mbus, &workspace->id, workspace, WorkSpace, tools, &msg_sub_value_region_tag_redraw);
}

void ED_area_do_mgs_subscribe_for_tool_ui(const wmRegionMessageSubscribeParams *params)
{
  struct wmMsgBus *mbus = params->message_bus;
  WorkSpace *workspace = params->workspace;
  ARegion *region = params->region;

  BLI_assert(region->regiontype == RGN_TYPE_UI);
  const char *panel_category_tool = "Tool";
  const char *category = UI_panel_category_active_get(region, false);

  bool update_region = false;
  if (category && STREQ(category, panel_category_tool)) {
    update_region = true;
  }
  else {
    /* Check if a tool category panel is pinned and visible in another category. */
    LISTBASE_FOREACH (Panel *, panel, &region->panels) {
      if (UI_panel_is_active(panel) && panel->flag & PNL_PIN &&
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
        .notify = ED_region_do_msg_notify_tag_redraw,
    };
    WM_msg_subscribe_rna_prop(
        mbus, &workspace->id, workspace, WorkSpace, tools, &msg_sub_value_region_tag_redraw);
  }
}

/**
 * Although there's no general support for minimizing areas, the status-bar can
 * be snapped to be only a few pixels high. A few pixels rather than 0 so it
 * can be un-minimized again. We consider it pseudo-minimized and don't draw
 * it then.
 */
static bool area_is_pseudo_minimized(const ScrArea *area)
{
  return (area->winx < 3) || (area->winy < 3);
}

void ED_region_do_layout(bContext *C, ARegion *region)
{
  /* This is optional, only needed for dynamically sized regions. */
  ScrArea *area = CTX_wm_area(C);
  ARegionType *at = region->type;

  if (!at->layout) {
    return;
  }

  if (at->do_lock || (area && area_is_pseudo_minimized(area))) {
    return;
  }

  region->do_draw |= RGN_DRAWING;

  UI_SetTheme(area ? area->spacetype : 0, at->regionid);
  at->layout(C, region);

  /* Clear temporary update flag. */
  region->flag &= ~RGN_FLAG_SEARCH_FILTER_UPDATE;
}

void ED_region_do_draw(bContext *C, ARegion *region)
{
  wmWindow *win = CTX_wm_window(C);
  ScrArea *area = CTX_wm_area(C);
  ARegionType *at = region->type;

  /* see BKE_spacedata_draw_locks() */
  if (at->do_lock) {
    return;
  }

  region->do_draw |= RGN_DRAWING;

  /* Set viewport, scissor, ortho and region->drawrct. */
  wmPartialViewport(&region->drawrct, &region->winrct, &region->drawrct);

  wmOrtho2_region_pixelspace(region);

  UI_SetTheme(area ? area->spacetype : 0, at->regionid);

  if (area && area_is_pseudo_minimized(area)) {
    UI_ThemeClearColor(TH_EDITOR_OUTLINE);
    return;
  }
  /* optional header info instead? */
  if (region->headerstr) {
    region_draw_status_text(area, region);
  }
  else if (at->draw) {
    at->draw(C, region);
  }

  /* XXX test: add convention to end regions always in pixel space,
   * for drawing of borders/gestures etc */
  ED_region_pixelspace(region);

  /* Remove sRGB override by rebinding the framebuffer. */
  GPUFrameBuffer *fb = GPU_framebuffer_active_get();
  GPU_framebuffer_bind(fb);

  ED_region_draw_cb_draw(C, region, REGION_DRAW_POST_PIXEL);

  region_draw_azones(area, region);

  /* for debugging unneeded area redraws and partial redraw */
  if (G.debug_value == 888) {
    GPU_blend(GPU_BLEND_ALPHA);
    GPUVertFormat *format = immVertexFormat();
    uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
    immUniformColor4f(BLI_thread_frand(0), BLI_thread_frand(0), BLI_thread_frand(0), 0.1f);
    immRectf(pos,
             region->drawrct.xmin - region->winrct.xmin,
             region->drawrct.ymin - region->winrct.ymin,
             region->drawrct.xmax - region->winrct.xmin,
             region->drawrct.ymax - region->winrct.ymin);
    immUnbindProgram();
    GPU_blend(GPU_BLEND_NONE);
  }

  memset(&region->drawrct, 0, sizeof(region->drawrct));

  UI_blocklist_free_inactive(C, region);

  if (area) {
    const bScreen *screen = WM_window_get_active_screen(win);

    /* Only region emboss for top-bar */
    if ((screen->state != SCREENFULL) && ED_area_is_global(area)) {
      region_draw_emboss(region, &region->winrct, (REGION_EMBOSS_LEFT | REGION_EMBOSS_RIGHT));
    }
    else if ((region->regiontype == RGN_TYPE_WINDOW) && (region->alignment == RGN_ALIGN_QSPLIT)) {

      /* draw separating lines between the quad views */

      float color[4] = {0.0f, 0.0f, 0.0f, 0.8f};
      UI_GetThemeColor3fv(TH_EDITOR_OUTLINE, color);
      GPUVertFormat *format = immVertexFormat();
      uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
      immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
      immUniformColor4fv(color);
      GPU_line_width(1.0f);
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
    WorkSpace *workspace = CTX_wm_workspace(C);
    wmWindowManager *wm = CTX_wm_manager(C);
    bScreen *screen = WM_window_get_active_screen(win);
    Scene *scene = CTX_data_scene(C);
    struct wmMsgBus *mbus = wm->message_bus;
    WM_msgbus_clear_by_owner(mbus, region);

    /* Cheat, always subscribe to this space type properties.
     *
     * This covers most cases and avoids copy-paste similar code for each space type.
     */
    if (ELEM(
            region->regiontype, RGN_TYPE_WINDOW, RGN_TYPE_CHANNELS, RGN_TYPE_UI, RGN_TYPE_TOOLS)) {
      SpaceLink *sl = area->spacedata.first;

      PointerRNA ptr;
      RNA_pointer_create(&screen->id, &RNA_Space, sl, &ptr);

      wmMsgSubscribeValue msg_sub_value_region_tag_redraw = {
          .owner = region,
          .user_data = region,
          .notify = ED_region_do_msg_notify_tag_redraw,
      };
      /* All properties for this space type. */
      WM_msg_subscribe_rna(mbus, &ptr, NULL, &msg_sub_value_region_tag_redraw, __func__);
    }

    wmRegionMessageSubscribeParams message_subscribe_params = {
        .context = C,
        .message_bus = mbus,
        .workspace = workspace,
        .scene = scene,
        .screen = screen,
        .area = area,
        .region = region,
    };
    ED_region_message_subscribe(&message_subscribe_params);
  }
}

/* **********************************
 * maybe silly, but let's try for now
 * to keep these tags protected
 * ********************************** */

void ED_region_tag_redraw(ARegion *region)
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

void ED_region_tag_redraw_cursor(ARegion *region)
{
  if (region) {
    region->do_draw_paintcursor = RGN_DRAW;
  }
}

void ED_region_tag_redraw_no_rebuild(ARegion *region)
{
  if (region && !(region->do_draw & (RGN_DRAWING | RGN_DRAW))) {
    region->do_draw &= ~(RGN_DRAW_PARTIAL | RGN_DRAW_EDITOR_OVERLAYS);
    region->do_draw |= RGN_DRAW_NO_REBUILD;
    memset(&region->drawrct, 0, sizeof(region->drawrct));
  }
}

void ED_region_tag_refresh_ui(ARegion *region)
{
  if (region) {
    region->do_draw |= RGN_REFRESH_UI;
  }
}

void ED_region_tag_redraw_editor_overlays(struct ARegion *region)
{
  if (region && !(region->do_draw & (RGN_DRAWING | RGN_DRAW))) {
    if (region->do_draw & RGN_DRAW_PARTIAL) {
      ED_region_tag_redraw(region);
    }
    else {
      region->do_draw |= RGN_DRAW_EDITOR_OVERLAYS;
    }
  }
}

void ED_region_tag_redraw_partial(ARegion *region, const rcti *rct, bool rebuild)
{
  if (region && !(region->do_draw & RGN_DRAWING)) {
    if (region->do_draw & RGN_DRAW_PARTIAL) {
      /* Partial redraw already set, expand region. */
      BLI_rcti_union(&region->drawrct, rct);
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

void ED_area_tag_redraw(ScrArea *area)
{
  if (area) {
    LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
      ED_region_tag_redraw(region);
    }
  }
}

void ED_area_tag_redraw_no_rebuild(ScrArea *area)
{
  if (area) {
    LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
      ED_region_tag_redraw_no_rebuild(region);
    }
  }
}

void ED_area_tag_redraw_regiontype(ScrArea *area, int regiontype)
{
  if (area) {
    LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
      if (region->regiontype == regiontype) {
        ED_region_tag_redraw(region);
      }
    }
  }
}

void ED_area_tag_refresh(ScrArea *area)
{
  if (area) {
    area->do_refresh = true;
  }
}

/* *************************************************************** */

const char *ED_area_region_search_filter_get(const ScrArea *area, const ARegion *region)
{
  /* Only the properties editor has a search string for now. */
  if (area->spacetype == SPACE_PROPERTIES) {
    SpaceProperties *sbuts = area->spacedata.first;
    if (region->regiontype == RGN_TYPE_WINDOW) {
      return ED_buttons_search_string_get(sbuts);
    }
  }

  return NULL;
}

void ED_region_search_filter_update(const ScrArea *area, ARegion *region)
{
  region->flag |= RGN_FLAG_SEARCH_FILTER_UPDATE;

  const char *search_filter = ED_area_region_search_filter_get(area, region);
  SET_FLAG_FROM_TEST(region->flag,
                     region->regiontype == RGN_TYPE_WINDOW && search_filter[0] != '\0',
                     RGN_FLAG_SEARCH_FILTER_ACTIVE);
}

/* *************************************************************** */

void ED_area_status_text(ScrArea *area, const char *str)
{
  /* happens when running transform operators in background mode */
  if (area == NULL) {
    return;
  }

  LISTBASE_FOREACH (ARegion *, region, &area->regionbase) {
    if (region->regiontype == RGN_TYPE_HEADER) {
      if (str) {
        if (region->headerstr == NULL) {
          region->headerstr = MEM_mallocN(UI_MAX_DRAW_STR, "headerprint");
        }
        BLI_strncpy(region->headerstr, str, UI_MAX_DRAW_STR);
        BLI_str_rstrip(region->headerstr);
      }
      else {
        MEM_SAFE_FREE(region->headerstr);
      }
      ED_region_tag_redraw(region);
    }
  }
}

void ED_workspace_status_text(bContext *C, const char *str)
{
  wmWindow *win = CTX_wm_window(C);
  WorkSpace *workspace = CTX_wm_workspace(C);

  /* Can be NULL when running operators in background mode. */
  if (workspace == NULL) {
    return;
  }

  if (str) {
    if (workspace->status_text == NULL) {
      workspace->status_text = MEM_mallocN(UI_MAX_DRAW_STR, "headerprint");
    }
    BLI_strncpy(workspace->status_text, str, UI_MAX_DRAW_STR);
  }
  else {
    MEM_SAFE_FREE(workspace->status_text);
  }

  /* Redraw status bar. */
  LISTBASE_FOREACH (ScrArea *, area, &win->global_areas.areabase) {
    if (area->spacetype == SPACE_STATUSBAR) {
      ED_area_tag_redraw(area);
      break;
    }
  }
}

/* ************************************************************ */

static void area_azone_init(wmWindow *win, const bScreen *screen, ScrArea *area)
{
  /* reinitialize entirely, regions and fullscreen add azones too */
  BLI_freelistN(&area->actionzones);

  if (screen->state != SCREENNORMAL) {
    return;
  }

  if (U.app_flag & USER_APP_LOCK_CORNER_SPLIT) {
    return;
  }

  if (ED_area_is_global(area)) {
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
    if (!WM_window_is_fullscreen(win) &&
        ((coords[i][0] == 0 && coords[i][1] == 0) ||
         (coords[i][0] == WM_window_pixels_x(win) && coords[i][1] == 0))) {
      continue;
    }
#else
    (void)win;
#endif

    /* set area action zones */
    AZone *az = (AZone *)MEM_callocN(sizeof(AZone), "actionzone");
    BLI_addtail(&(area->actionzones), az);
    az->type = AZONE_AREA;
    az->x1 = coords[i][0];
    az->y1 = coords[i][1];
    az->x2 = coords[i][2];
    az->y2 = coords[i][3];
    BLI_rcti_init(&az->rect, az->x1, az->x2, az->y1, az->y2);
  }
}

static void fullscreen_azone_init(ScrArea *area, ARegion *region)
{
  if (ED_area_is_global(area) || (region->regiontype != RGN_TYPE_WINDOW)) {
    return;
  }

  AZone *az = (AZone *)MEM_callocN(sizeof(AZone), "fullscreen action zone");
  BLI_addtail(&(area->actionzones), az);
  az->type = AZONE_FULLSCREEN;
  az->region = region;
  az->alpha = 0.0f;

  if (U.uiflag2 & USER_REGION_OVERLAP) {
    const rcti *rect_visible = ED_region_visible_rect(region);
    az->x2 = region->winrct.xmin + rect_visible->xmax;
    az->y2 = region->winrct.ymin + rect_visible->ymax;
  }
  else {
    az->x2 = region->winrct.xmax;
    az->y2 = region->winrct.ymax;
  }
  az->x1 = az->x2 - AZONEFADEOUT;
  az->y1 = az->y2 - AZONEFADEOUT;

  BLI_rcti_init(&az->rect, az->x1, az->x2, az->y1, az->y2);
}

#define AZONEPAD_EDGE (0.1f * U.widget_unit)
#define AZONEPAD_ICON (0.45f * U.widget_unit)
static void region_azone_edge(AZone *az, ARegion *region)
{
  /* If region is overlapped (transparent background), move #AZone to content.
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
  BLI_rcti_init(&az->rect, az->x1, az->x2, az->y1, az->y2);
}

/* region already made zero sized, in shape of edge */
static void region_azone_tab_plus(ScrArea *area, AZone *az, ARegion *region)
{
  float edge_offset = 1.0f;
  const float tab_size_x = 0.7f * U.widget_unit;
  const float tab_size_y = 0.4f * U.widget_unit;

  int tot = 0;
  LISTBASE_FOREACH (AZone *, azt, &area->actionzones) {
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
  BLI_rcti_init(&az->rect, az->x1, az->x2, az->y1, az->y2);
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
  BLI_addtail(&(area->actionzones), az);
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
