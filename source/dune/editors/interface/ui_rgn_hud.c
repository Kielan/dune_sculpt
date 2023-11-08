#include <string.h>

#include "mem_guardedalloc.h"

#include "types_screen.h"
#include "types_userdef.h"

#include "lib_list.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "dune_cxt.h"
#include "dune_screen.h"

#include "win_api.h"
#include "win_types.h"

#include "api_access.h"

#include "ui.h"
#include "view2d.h"

#include "lang.h"

#include "ent_screen.h"
#include "ent_undo.h"

#include "gpu_framebuffer.h"
#include "ui_intern.h"

/* Utils */
struct HudRgnData {
  short rgnid;
};

static bool last_redo_poll(const Cxt *C, short rgn_type)
{
  WinOp *op = win_op_last_redo(C);
  if (op == NULL) {
    return false;
  }

  bool success = false;
  {
    /* Make sure that we are using the same rgn type as the original
     * op call. Otherwise we would be polling the op with the
     * wrong cxt. */
    ScrArea *area = cxt_win_area(C);
    ARgn *rgn_op = (rgn_type != -1) ? dune_area_find_rgn_type(area, rgn_type) : NULL;
    ARgn *rgn_prev = cxt_win_rgn(C);
    cxt_win_rgn_set((Cxt *)C, rgn_op);

    if (win_op_repeat_check(C, op) && win_op_check_ui_empty(op->type) == false) {
      success = win_op_poll((Cxt *)C, op->type);
    }
    cxt_win_rgn_set((Cxt *)C, rgn_prev);
  }
  return success;
}

static void hud_rgn_hide(ARgn *rgn)
{
  rgn->flag |= RGN_FLAG_HIDDEN;
  /* Avoids setting 'AREA_FLAG_RGN_SIZE_UPDATE'
   * since other rgns don't depend on this. */
  lib_rcti_init(&rgn->winrct, 0, 0, 0, 0);
}

/* Redo Pnl **/
static bool hud_pnl_op_redo_poll(const Cxt *C, PnlType *UNUSED(pt))
{
  ScrArea *area = cxt_win_area(C);
  ARgn *rgn = dune_area_find_rgn_type(area, RGN_TYPE_HUD);
  if (rgn != NULL) {
    struct HudRgnData *hrd = rgn->rgndata;
    if (hrd != NULL) {
      return last_redo_poll(C, hrd->rgnid);
    }
  }
  return false;
}

static void hud_panellist_op_redo_draw_header(const Cxt *C, PanelList *panellist)
{
  wmOp *op = wm_op_last_redo(C);
  lib_strncpy(panellist->drawname, wm_op_name(op->type, op->ptr), sizeof(panellist->drawname));
}

static void hud_panellist_op_redo_draw(const Cxt *C, Panel *panel)
{
  wmOp *op = wm_op_last_redo(C);
  if (op == NULL) {
    return;
  }
  if (!wm_op_check_ui_enabled(C, op->type->name)) {
    uiLayoutSetEnabled(panel->layout, false);
  }
  uiLayout *col = uiLayoutColumn(panellist->layout, false);
  uiTemplateOpRedoProps(col, C);
}

static void hud_panellist_register(ARegionType *art, int space_type, int region_type)
{
  PanelType *pt;

  pt = mem_callocn(sizeof(PanelType), __func__);
  strcpy(pt->idname, "OP_PT_redo");
  strcpy(pt->label, N_("Redo"));
  strcpy(pt->translation_cxt, I18N_CTX_DEFAULT);
  pt->draw_header = hud_panellist_op_redo_draw_header;
  pt->draw = hud_panellist_op_redo_draw;
  pt->poll = hud_panellist_op_redo_poll;
  pt->space_type = space_type;
  pt->region_type = region_type;
  pt->flag |= PANEL_TYPE_DEFAULT_CLOSED;
  lib_addtail(&art->paneltypes, pt);
}

/** Callbacks for Floating Region **/
static void hud_region_init(WindowManager *wm, ARegion *region)
{
  entity_region_panellist_init(wm, region);
  ui_region_handlers_add(&region->handlers);
  region->flag |= RGN_FLAG_TEMP_REGIONDATA;
}

static void hud_region_free(ARegion *region)
{
  MEM_SAFE_FREE(region->regiondata);
}

static void hud_region_layout(const Cxt *C, ARegion *region)
{
  struct HudRegionData *hrd = region->regiondata;
  if (hrd == NULL || !last_redo_poll(C, hrd->regionid)) {
    entity_region_tag_redraw(region);
    hud_region_hide(region);
    return;
  }

  ScrArea *area = cxt_wm_area(C);
  const int size_y = region->sizey;

  entity_region_panellist_layout(C, region);

  if (region->panels.first &&
      ((area->flag & AREA_FLAG_REGION_SIZE_UPDATE) || (region->sizey != size_y))) {
    int winx_new = UI_DPI_FAC * (region->sizex + 0.5f);
    int winy_new = UI_DPI_FAC * (region->sizey + 0.5f);
    View2D *v2d = &region->v2d;

    if (region->flag & RGN_FLAG_SIZE_CLAMP_X) {
      CLAMP_MAX(winx_new, region->winx);
    }
    if (region->flag & RGN_FLAG_SIZE_CLAMP_Y) {
      CLAMP_MAX(winy_new, region->winy);
    }

    region->winx = winx_new;
    region->winy = winy_new;

    region->winrct.xmax = (region->winrct.xmin + region->winx) - 1;
    region->winrct.ymax = (region->winrct.ymin + region->winy) - 1;

    view2d_region_reinit(v2d, V2D_COMMONVIEW_LIST, region->winx, region->winy);

    /* Weak, but needed to avoid glitches, especially with hi-dpi
     * (where resizing the view glitches often).
     * Fortunately this only happens occasionally. */
    entity_region_panellist_layout(C, region);
  }

  /* restore view matrix */
  view2d_view_restore(C);
}

static void hud_region_draw(const Cxt *C, ARegion *region)
{
  view2d_view_ortho(&region->v2d);
  wmOrtho2_region_pixelspace(region);
  gpu_clear_color(0.0f, 0.0f, 0.0f, 0.0f);

  if ((region->flag & RGN_FLAG_HIDDEN) == 0) {
    ui_draw_menu_back(NULL,
                      NULL,
                      &(rcti){
                          .xmax = region->winx,
                          .ymax = region->winy,
                      });
    entity_region_panellist_draw(C, region);
  }
}

ARegionType *entity_area_type_hud(int space_type)
{
  ARegionType *art = mem_callocn(sizeof(ARegionType), __func__);
  art->regionid = RGN_TYPE_HUD;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D;
  art->layout = hud_region_layout;
  art->draw = hud_region_draw;
  art->init = hud_region_init;
  art->free = hud_region_free;

  /* We need to indicate a preferred size to avoid false `RGN_FLAG_TOO_SMALL`
   * the first time the region is created. */
  art->prefsizex = AREAMINX;
  art->prefsizey = HEADERY;

  hud_panellist_register(art, space_type, art->regionid);

  art->lock = 1; /* can become flag, see dune_spacedata_draw_locks */
  return art;
}

static ARegion *hud_region_add(ScrArea *area)
{
  ARegion *region = mem_callocn(sizeof(ARegion), "area region");
  ARegion *region_win = dune_area_find_region_type(area, RGN_TYPE_WINDOW);
  if (region_win) {
    lib_insertlinkbefore(&area->regionbase, region_win, region);
  }
  else {
    lib_addtail(&area->regionbase, region);
  }
  region->regiontype = RGN_TYPE_HUD;
  region->alignment = RGN_ALIGN_FLOAT;
  region->overlap = true;
  region->flag |= RGN_FLAG_DYNAMIC_SIZE;

  if (region_win) {
    float x, y;

    view2d_scroller_size_get(&region_win->v2d, &x, &y);
    region->runtime.offset_x = x;
    region->runtime.offset_y = y;
  }

  return region;
}

void entity_area_type_hud_clear(WindowManager *wm, ScrArea *area_keep)
{
  LIST_FOREACH (Window *, win, &wm->windows) {
    Screen *screen = wm_window_get_active_screen(win);
    LIST_FOREACH (ScrArea *, area, &screen->areabase) {
      if (area != area_keep) {
        LIST_FOREACH (ARegion *, region, &area->regionbase) {
          if (region->regiontype == RGN_TYPE_HUD) {
            if ((region->flag & RGN_FLAG_HIDDEN) == 0) {
              hud_region_hide(region);
              entity_region_tag_redraw(region);
              entity_area_tag_redraw(area);
            }
          }
        }
      }
    }
  }
}

void entity_area_type_hud_ensure(Cxt *C, ScrArea *area)
{
  WindowManager *wm = cxt_wm_manager(C);
  entity_area_type_hud_clear(wm, area);

  ARegionType *art = dune_regiontype_from_id(area->type, RGN_TYPE_HUD);
  if (art == NULL) {
    return;
  }

  ARegion *region = dune_area_find_region_type(area, RGN_TYPE_HUD);

  if (region && (region->flag & RGN_FLAG_HIDDEN_BY_USER)) {
    /* The region is intentionally hidden by the user, don't show it. */
    hud_region_hide(region);
    return;
  }

  bool init = false;
  const bool was_hidden = region == NULL || region->visible == false;
  ARegion *region_op = ctx_wm_region(C);
  lib_assert((region_op == NULL) || (region_op->regiontype != RGN_TYPE_HUD));
  if (!last_redo_poll(C, region_op ? region_op->regiontype : -1)) {
    if (region) {
      entity_region_tag_redraw(region);
      hud_region_hide(region);
    }
    return;
  }

  if (region == NULL) {
    init = true;
    region = hud_region_add(area);
    region->type = art;
  }

  /* Let 'ED_area_update_region_sizes' do the work of placing the region.
   * Otherwise we could set the 'region->winrct' & 'region->winx/winy' here. */
  if (init) {
    area->flag |= AREA_FLAG_REGION_SIZE_UPDATE;
  }
  else {
    if (region->flag & RGN_FLAG_HIDDEN) {
      /* Also forces recalculating HUD size in hud_region_layout(). */
      area->flag |= AREA_FLAG_REGION_SIZE_UPDATE;
    }
    region->flag &= ~RGN_FLAG_HIDDEN;
  }

  {
    struct HudRegionData *hrd = region->regiondata;
    if (hrd == NULL) {
      hrd = mem_callocn(sizeof(*hrd), __func__);
      region->regiondata = hrd;
    }
    if (region_op) {
      hrd->regionid = region_op->regiontype;
    }
    else {
      hrd->regionid = -1;
    }
  }

  if (init) {
    /* This is needed or 'winrct' will be invalid. */
    Window *win = cxt_wm_window(C);
    entity_area_update_region_sizes(wm, win, area);
  }

  entity_region_floating_init(region);
  entity_region_tag_redraw(region);

  /* Reset zoom level (not well supported). */
  region->v2d.cur = region->v2d.tot = (rctf){
      .xmax = region->winx,
      .ymax = region->winy,
  };
  region->v2d.minzoom = 1.0f;
  region->v2d.maxzoom = 1.0f;

  region->visible = !(region->flag & RGN_FLAG_HIDDEN);

  /* We shouldn't need to do this every time :S */
  /* this is evil! - Also makes the menu show on first draw. :( */
  if (region->visible) {
    ARegion *region_prev = cxt_wm_region(C);
    cxt_wm_region_set((Cxt *)C, region);
    hud_region_layout(C, region);
    if (was_hidden) {
      region->winx = region->v2d.winx;
      region->winy = region->v2d.winy;
      region->v2d.cur = region->v2d.tot = (rctf){
          .xmax = region->winx,
          .ymax = region->winy,
      };
    }
    cxt_wm_region_set((Cxt *)C, region_prev);
  }

  region->visible = !((region->flag & RGN_FLAG_HIDDEN) || (region->flag & RGN_FLAG_TOO_SMALL));
}
