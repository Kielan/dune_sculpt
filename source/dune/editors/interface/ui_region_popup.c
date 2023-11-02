/* PopUp Region (Generic) */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_userdef.h"

#include "lib_list.h"
#include "lib_math.h"
#include "lib_rect.h"
#include "lib_utildefines.h"

#include "dune_cxt.h"
#include "dune_screen.h"

#include "win_api.h"
#include "win_types.h"

#include "ui.h"

#include "ed_screen.h"

#include "ui_intern.h"
#include "ui_regions_intern.h"

/* Util Fns */

void ui_popup_translate(ARegion *region, const int mdiff[2])
{
  lib_rcti_translate(&region->winrct, UNPACK2(mdiff));

  ed_region_update_rect(region);

  ed_region_tag_redraw(region);

  /* update blocks */
  LIST_FOREACH (uiBlock *, block, &region->uiblocks) {
    uiPopupBlockHandle *handle = block->handle;
    /* Make empty, will be initialized on next use, see T60608. */
    lib_rctf_init(&handle->prev_block_rect, 0, 0, 0, 0);

    LIST_FOREACH (uiSafetyRct *, saferct, &block->saferct) {
      lib_rctf_translate(&saferct->parent, UNPACK2(mdiff));
      lib_rctf_translate(&saferct->safety, UNPACK2(mdiff));
    }
  }
}

/* position block relative to but, result is in window space */
static void ui_popup_block_position(Win *win,
                                    ARegion *btnregion,
                                    Btn *btn,
                                    uiBlock *block)
{
  uiPopupBlockHandle *handle = block->handle;

  /* Compute btn position in win coordinates using the source
   * btn region/block, to position the popup attached to it. */
  rctf butrct;

  if (!handle->refresh) {
    ui_block_to_win_rctf(btnregion, btn->block, &btnrct, &btn->rect);

    /* widget_roundbox_set has this correction too, keep in sync */
    if (btn->type != BTYPE_PULLDOWN) {
      if (btn->drawflag & BTN_ALIGN_TOP) {
        btnrct.ymax += U.pixelsize;
      }
      if (btn->drawflag & BTN_ALIGN_LEFT) {
        btnrct.xmin -= U.pixelsize;
      }
    }

    handle->prev_btnrct = btnrct;
  }
  else {
    /* For refreshes, keep same btn position so popup doesn't move. */
    btnrct = handle->prev_btnrct;
  }

  /* Compute block size in window space, based on btns contained in it. */
  if (block->rect.xmin == 0.0f && block->rect.xmax == 0.0f) {
    if (block->btns.first) {
      lib_rctf_init_minmax(&block->rect);

      LIST_FOREACH (Btn *, bt, &block->btns) {
        if (block->content_hints & UI_BLOCK_CONTAINS_SUBMENU_BTN) {
          bt->rect.xmax += UI_MENU_SUBMENU_PADDING;
        }
        lib_rctf_union(&block->rect, &bt->rect);
      }
    }
    else {
      /* we're nice and allow empty blocks too */
      block->rect.xmin = block->rect.ymin = 0;
      block->rect.xmax = block->rect.ymax = 20;
    }
  }

  ui_block_to_win_rctf(btnregion, btn->block, &block->rect, &block->rect);

  /* Compute direction relative to button, based on available space. */
  const int size_x = lib_rctf_size_x(&block->rect) + 0.2f * UI_UNIT_X; /* 4 for shadow */
  const int size_y = lib_rctf_size_y(&block->rect) + 0.2f * UI_UNIT_Y;
  const int center_x = (block->direction & UI_DIR_CENTER_X) ? size_x / 2 : 0;
  const int center_y = (block->direction & UI_DIR_CENTER_Y) ? size_y / 2 : 0;

  short dir1 = 0, dir2 = 0;

  if (!handle->refresh) {
    bool left = 0, right = 0, top = 0, down = 0;

    const int win_x = win_pixels_x(win);
    const int win_y = win_pixels_y(win);

    /* Take into account maximum size so we don't have to flip on refresh. */
    const float max_size_x = max_ff(size_x, handle->max_size_x);
    const float max_size_y = max_ff(size_y, handle->max_size_y);

    /* check if there's space at all */
    if (btnrct.xmin - max_size_x + center_x > 0.0f) {
      left = 1;
    }
    if (btnrct.xmax + max_size_x - center_x < win_x) {
      right = 1;
    }
    if (btnrct.ymin - max_size_y + center_y > 0.0f) {
      down = 1;
    }
    if (btnrct.ymax + max_size_y - center_y < win_y) {
      top = 1;
    }

    if (top == 0 && down == 0) {
      if (btnrct.ymin - max_size_y < win_y - btnrct.ymax - max_size_y) {
        top = 1;
      }
      else {
        down = 1;
      }
    }

    dir1 = (block->direction & UI_DIR_ALL);

    /* Secondary directions. */
    if (dir1 & (UI_DIR_UP | UI_DIR_DOWN)) {
      if (dir1 & UI_DIR_LEFT) {
        dir2 = UI_DIR_LEFT;
      }
      else if (dir1 & UI_DIR_RIGHT) {
        dir2 = UI_DIR_RIGHT;
      }
      dir1 &= (UI_DIR_UP | UI_DIR_DOWN);
    }

    if ((dir2 == 0) && (ELEM(dir1, UI_DIR_LEFT, UI_DIR_RIGHT))) {
      dir2 = UI_DIR_DOWN;
    }
    if ((dir2 == 0) && (ELEM(dir1, UI_DIR_UP, UI_DIR_DOWN))) {
      dir2 = UI_DIR_LEFT;
    }

    /* no space at all? don't change */
    if (left || right) {
      if (dir1 == UI_DIR_LEFT && left == 0) {
        dir1 = UI_DIR_RIGHT;
      }
      if (dir1 == UI_DIR_RIGHT && right == 0) {
        dir1 = UI_DIR_LEFT;
      }
      /* this is aligning, not append! */
      if (dir2 == UI_DIR_LEFT && right == 0) {
        dir2 = UI_DIR_RIGHT;
      }
      if (dir2 == UI_DIR_RIGHT && left == 0) {
        dir2 = UI_DIR_LEFT;
      }
    }
    if (down || top) {
      if (dir1 == UI_DIR_UP && top == 0) {
        dir1 = UI_DIR_DOWN;
      }
      if (dir1 == UI_DIR_DOWN && down == 0) {
        dir1 = UI_DIR_UP;
      }
      lib_assert(dir2 != UI_DIR_UP);
      //          if (dir2 == UI_DIR_UP   && top == 0)  { dir2 = UI_DIR_DOWN; }
      if (dir2 == UI_DIR_DOWN && down == 0) {
        dir2 = UI_DIR_UP;
      }
    }

    handle->prev_dir1 = dir1;
    handle->prev_dir2 = dir2;
  }
  else {
    /* For refreshes, keep same popup direct so popup doesn't move
     * to a totally different position while editing in it. */
    dir1 = handle->prev_dir1;
    dir2 = handle->prev_dir2;
  }

  /* Compute offset based on direction. */
  float offset_x = 0, offset_y = 0;

  /* Ensure btns don't come between the parent button and the popup, see: T63566. */
  const float offset_overlap = max_ff(U.pixelsize, 1.0f);

  if (dir1 == UI_DIR_LEFT) {
    offset_x = (btnrct.xmin - block->rect.xmax) + offset_overlap;
    if (dir2 == UI_DIR_UP) {
      offset_y = btnrct.ymin - block->rect.ymin - center_y - UI_MENU_PADDING;
    }
    else {
      offset_y = btnrct.ymax - block->rect.ymax + center_y + UI_MENU_PADDING;
    }
  }
  else if (dir1 == UI_DIR_RIGHT) {
    offset_x = (btnrct.xmax - block->rect.xmin) - offset_overlap;
    if (dir2 == UI_DIR_UP) {
      offset_y = btnrct.ymin - block->rect.ymin - center_y - UI_MENU_PADDING;
    }
    else {
      offset_y = btnrct.ymax - block->rect.ymax + center_y + UI_MENU_PADDING;
    }
  }
  else if (dir1 == UI_DIR_UP) {
    offset_y = (btnrct.ymax - block->rect.ymin) - offset_overlap;
    if (dir2 == UI_DIR_RIGHT) {
      offset_x = btnrct.xmax - block->rect.xmax + center_x;
    }
    else {
      offset_x = btnrct.xmin - block->rect.xmin - center_x;
    }
    /* changed direction? */
    if ((dir1 & block->direction) == 0) {
      /* TODO: still do */
      ui_block_order_flip(block);
    }
  }
  else if (dir1 == UI_DIR_DOWN) {
    offset_y = (btnrct.ymin - block->rect.ymax) + offset_overlap;
    if (dir2 == UI_DIR_RIGHT) {
      offset_x = btnrct.xmax - block->rect.xmax + center_x;
    }
    else {
      offset_x = btnrct.xmin - block->rect.xmin - center_x;
    }
    /* changed direction? */
    if ((dir1 & block->direction) == 0) {
      /* TODO: still do */
      ui_block_order_flip(block);
    }
  }

  /* Center over popovers for eg. */
  if (block->direction & UI_DIR_CENTER_X) {
    offset_x += lib_rctf_size_x(&btnrct) / ((dir2 == UI_DIR_LEFT) ? 2 : -2);
  }

  /* Apply offset, btns in window coords. */
  LIST_FOREACH (Btn *, bt, &block->btns) {
    ui_block_to_win_rctf(btnregion, btn->block, &bt->rect, &bt->rect);

    lib_rctf_translate(&bt->rect, offset_x, offset_y);

    /* ui_but_update recalculates drawstring size in pixels */
    ui_btn_update(bt);
  }

  lib_rctf_translate(&block->rect, offset_x, offset_y);

  /* Safety calculus. */
  {
    const float midx = lib_rctf_cent_x(&btnrct);
    const float midy = lib_rctf_cent_y(&btnrct);

    /* when you are outside parent btn, safety there should be smaller */
    const int s1 = 40 * U.dpi_fac;
    const int s2 = 3 * U.dpi_fac;

    /* parent btn to left */
    if (midx < block->rect.xmin) {
      block->safety.xmin = block->rect.xmin - s2;
    }
    else {
      block->safety.xmin = block->rect.xmin - s1;
    }
    /* parent btn to right */
    if (midx > block->rect.xmax) {
      block->safety.xmax = block->rect.xmax + s2;
    }
    else {
      block->safety.xmax = block->rect.xmax + s1;
    }

    /* parent btn on bottom */
    if (midy < block->rect.ymin) {
      block->safety.ymin = block->rect.ymin - s2;
    }
    else {
      block->safety.ymin = block->rect.ymin - s1;
    }
    /* parent button on top */
    if (midy > block->rect.ymax) {
      block->safety.ymax = block->rect.ymax + s2;
    }
    else {
      block->safety.ymax = block->rect.ymax + s1;
    }

    /* Exception for switched pull-downs. */
    if (dir1 && (dir1 & block->direction) == 0) {
      if (dir2 == UI_DIR_RIGHT) {
        block->safety.xmax = block->rect.xmax + s2;
      }
      if (dir2 == UI_DIR_LEFT) {
        block->safety.xmin = block->rect.xmin - s2;
      }
    }
    block->direction = dir1;
  }

  /* Keep a list of these, needed for pull-down menus. */
  uiSafetyRct *saferct = mem_calloc(sizeof(uiSafetyRct), "uiSafetyRct");
  saferct->parent = btnrct;
  saferct->safety = block->safety;
  lib_freelist(&block->saferct);
  lib_duplicatelist(&block->saferct, &btn->block->saferct);
  lib_addhead(&block->saferct, saferct);
}

/* Menu Block Creation */
static void ui_block_region_refresh(const Cxt *C, ARegion *region)
{
  ScrArea *cxt_area = cxt_win_area(C);
  ARegion *cxt_region = cxt_win_region(C);

  if (region->do_draw & RGN_REFRESH_UI) {
    ScrArea *handle_cxt_area;
    ARegion *handle_cxt_region;

    region->do_draw &= ~RGN_REFRESH_UI;
    LIST_FOREACH_MUTABLE (uiBlock *, block, &region->uiblocks) {
      uiPopupBlockHandle *handle = block->handle;

      if (handle->can_refresh) {
        handle_cxt_area = handle->cxt_area;
        handle_cxt_region = handle->cxt_region;

        if (handle_cxt_area) {
          cxt_win_area_set((Cxt *)C, handle_cxt_area);
        }
        if (handle_cxt_region) {
          cxt_win_region_set((Cxt *)C, handle_cxt_region);
        }

        Btn *btn = handle->popup_create_vars.btn;
        ARegion *btnregion = handle->popup_create_vars.btnregion;
        ui_popup_block_refresh((Cxt *)C, handle, btnregion, btn);
      }
    }
  }

  cxt_win_area_set((Cxt *)C, cxt_area);
  cxt_win_region_set((Cxt *)C, cxt_region);
}

static void ui_block_region_draw(const Cxt *C, ARegion *region)
{
  LIST_FOREACH (uiBlock *, block, &region->uiblocks) {
    ui_block_draw(C, block);
  }
}

/* Use to refresh centered popups on screen resizing (for splash). */
static void ui_block_region_popup_win_listener(const WinRegionListenerParams *params)
{
  ARegion *region = params->region;
  WinNotifier *winnotifier = params->notifier;

  switch (winnotifier->category) {
    case NC_WIN: {
      switch (winnotifier->action) {
        case NA_EDITED: {
          /* win resize */
          ed_region_tag_refresh_ui(region);
          break;
        }
      }
      break;
    }
  }
}

static void ui_popup_block_clip(Win *win, uiBlock *block)
{
  const float xmin_orig = block->rect.xmin;
  const int margin = UI_SCREEN_MARGIN;
  int winx, winy;

  if (block->flag & UI_BLOCK_NO_WIN_CLIP) {
    return;
  }

  winx = win_pixels_x(win);
  winy = win_pixels_y(win);

  /* shift to left if outside of view */
  if (block->rect.xmax > winx - margin) {
    const float xofs = winx - margin - block->rect.xmax;
    block->rect.xmin += xofs;
    block->rect.xmax += xofs;
  }
  /* shift menus to right if outside of view */
  if (block->rect.xmin < margin) {
    const float xofs = (margin - block->rect.xmin);
    block->rect.xmin += xofs;
    block->rect.xmax += xofs;
  }

  if (block->rect.ymin < margin) {
    block->rect.ymin = margin;
  }
  if (block->rect.ymax > winy - UI_POPUP_MENU_TOP) {
    block->rect.ymax = winy - UI_POPUP_MENU_TOP;
  }

  /* ensure menu items draw inside left/right boundary */
  const float xofs = block->rect.xmin - xmin_orig;
  LIST_FOREACH (Btn *, btn, &block->btns) {
    btn->rect.xmin += xofs;
    btn->rect.xmax += xofs;
  }
}

void ui_popup_block_scrolltest(uiBlock *block)
{
  block->flag &= ~(UI_BLOCK_CLIPBOTTOM | UI_BLOCK_CLIPTOP);

  LIST_FOREACH (Btn *, bt, &block->btns) {
    btn->flag &= ~UI_SCROLLED;
  }

  if (block->btns.first == block->btns.last) {
    return;
  }

  /* mark btns that are outside boundary */
  LIST_FOREACH (Btn *, btn, &block->btns) {
    if (btn->rect.ymin < block->rect.ymin) {
      btn->flag |= UI_SCROLLED;
      block->flag |= UI_BLOCK_CLIPBOTTOM;
    }
    if (btn->rect.ymax > block->rect.ymax) {
      btn->flag |= UI_SCROLLED;
      block->flag |= UI_BLOCK_CLIPTOP;
    }
  }

  /* mark btns overlapping arrows, if we have them */
  LIST_FOREACH (Btn *, btn, &block->btns) {
    if (block->flag & UI_BLOCK_CLIPBOTTOM) {
      if (btn->rect.ymin < block->rect.ymin + UI_MENU_SCROLL_ARROW) {
        btn->flag |= UI_SCROLLED;
      }
    }
    if (block->flag & UI_BLOCK_CLIPTOP) {
      if (btn->rect.ymax > block->rect.ymax - UI_MENU_SCROLL_ARROW) {
        btn->flag |= UI_SCROLLED;
      }
    }
  }
}

static void ui_popup_block_remove(Cxt *C, uiPopupBlockHandle *handle)
{
  Win *cxt_win = cxt_win(C);
  ScrArea *cxt_area = cxt_win_area(C);
  ARegion *cxt_region = cxt_win_region(C);

  WinManager *wm = cxt_win_manager(C);
  Win *win = cxt_win;
  Screen *screen = cxt_wm_screen(C);

  /* There may actually be a different window active than the one showing the popup, so lookup real
   * one. */
  if (lib_findindex(&screen->regionbase, handle->region) == -1) {
    LIST_FOREACH (Win *, win_iter, &wm->windows) {
      screen = win_get_active_screen(win_iter);
      if (lib_findindex(&screen->regionbase, handle->region) != -1) {
        win = win_iter;
        break;
      }
    }
  }

  lib_assert(win && screen);

  cxt_win_set(C, win);
  ui_region_temp_remove(C, screen, handle->region);

  /* Reset context (area and region were NULL'ed when changing context window). */
  cxt_win_set(C, cxt_win);
  cxt_win_area_set(C, cxt_area);
  cxt_win_region_set(C, cxt_region);

  /* reset to region cursor (only if there's not another menu open) */
  if (lib_list_is_empty(&screen->regionbase)) {
    win->tag_cursor_refresh = true;
  }

  if (handle->scrolltimer) {
    win_event_remove_timer(wm, win, handle->scrolltimer);
  }
}

uiBlock *ui_popup_block_refresh(Cxt *C,
                                uiPopupBlockHandle *handle,
                                ARegion *btnregion,
                                uiBtn *btn)
{
  const int margin = UI_POPUP_MARGIN;
  wmWindow *window = CTX_wm_window(C);
  ARegion *region = handle->region;

  const uiBlockCreateFn create_func = handle->popup_create_vars.create_func;
  const uiBlockHandleCreateFunc handle_create_func = handle->popup_create_vars.handle_create_func;
  void *arg = handle->popup_create_vars.arg;

  uiBlock *block_old = region->uiblocks.first;
  uiBlock *block;

  handle->refresh = (block_old != NULL);

  lib_assert(!handle->refresh || handle->can_refresh);

#ifdef DEBUG
  WinEvent *event_back = win->eventstate;
  WinEvent *event_last_back = win->event_last_handled;
#endif

  /* create ui block */
  if (create_func) {
    block = create_fn(C, region, arg);
  }
  else {
    block = handle_create_fn(C, handle, arg);
  }

  /* callbacks _must_ leave this for us, otherwise we can't call UI_block_update_from_old */
  BLI_assert(!block->endblock);

  /* ensure we don't use mouse coords here! */
#ifdef DEBUG
  window->eventstate = NULL;
#endif

  if (block->handle) {
    memcpy(block->handle, handle, sizeof(uiPopupBlockHandle));
    MEM_freeN(handle);
    handle = block->handle;
  }
  else {
    block->handle = handle;
  }

  region->regiondata = handle;

  /* set UI_BLOCK_NUMSELECT before UI_block_end() so we get alphanumeric keys assigned */
  if (but == NULL) {
    block->flag |= UI_BLOCK_POPUP;
  }

  block->flag |= UI_BLOCK_LOOP;
  UI_block_theme_style_set(block, UI_BLOCK_THEME_STYLE_POPUP);

  /* defer this until blocks are translated (below) */
  block->oldblock = NULL;

  if (!block->endblock) {
    UI_block_end_ex(
        C, block, handle->popup_create_vars.event_xy, handle->popup_create_vars.event_xy);
  }

  /* if this is being created from a button */
  if (but) {
    block->aspect = but->block->aspect;
    ui_popup_block_position(window, butregion, but, block);
    handle->direction = block->direction;
  }
  else {
    uiSafetyRct *saferct;
    /* Keep a list of these, needed for pull-down menus. */
    saferct = MEM_callocN(sizeof(uiSafetyRct), "uiSafetyRct");
    saferct->safety = block->safety;
    BLI_addhead(&block->saferct, saferct);
  }

  if (block->flag & UI_BLOCK_RADIAL) {
    const int win_width = UI_SCREEN_MARGIN;
    int winx, winy;

    int x_offset = 0, y_offset = 0;

    winx = WM_window_pixels_x(window);
    winy = WM_window_pixels_y(window);

    copy_v2_v2(block->pie_data.pie_center_init, block->pie_data.pie_center_spawned);

    /* only try translation if area is large enough */
    if (BLI_rctf_size_x(&block->rect) < winx - (2.0f * win_width)) {
      if (block->rect.xmin < win_width) {
        x_offset += win_width - block->rect.xmin;
      }
      if (block->rect.xmax > winx - win_width) {
        x_offset += winx - win_width - block->rect.xmax;
      }
    }

    if (BLI_rctf_size_y(&block->rect) < winy - (2.0f * win_width)) {
      if (block->rect.ymin < win_width) {
        y_offset += win_width - block->rect.ymin;
      }
      if (block->rect.ymax > winy - win_width) {
        y_offset += winy - win_width - block->rect.ymax;
      }
    }
    /* if we are offsetting set up initial data for timeout functionality */

    if ((x_offset != 0) || (y_offset != 0)) {
      block->pie_data.pie_center_spawned[0] += x_offset;
      block->pie_data.pie_center_spawned[1] += y_offset;

      UI_block_translate(block, x_offset, y_offset);

      if (U.pie_initial_timeout > 0) {
        block->pie_data.flags |= UI_PIE_INITIAL_DIRECTION;
      }
    }

    region->winrct.xmin = 0;
    region->winrct.xmax = winx;
    region->winrct.ymin = 0;
    region->winrct.ymax = winy;

    ui_block_calc_pie_segment(block, block->pie_data.pie_center_init);

    /* lastly set the buttons at the center of the pie menu, ready for animation */
    if (U.pie_animation_timeout > 0) {
      LISTBASE_FOREACH (uiBut *, but_iter, &block->buttons) {
        if (but_iter->pie_dir != UI_RADIAL_NONE) {
          BLI_rctf_recenter(&but_iter->rect, UNPACK2(block->pie_data.pie_center_spawned));
        }
      }
    }
  }
  else {
    /* Add an offset to draw the popover arrow. */
    if ((block->flag & UI_BLOCK_POPOVER) && ELEM(block->direction, UI_DIR_UP, UI_DIR_DOWN)) {
      /* Keep sync with 'ui_draw_popover_back_impl'. */
      const float unit_size = U.widget_unit / block->aspect;
      const float unit_half = unit_size * (block->direction == UI_DIR_DOWN ? 0.5 : -0.5);

      UI_block_translate(block, 0, -unit_half);
    }

    /* clip block with window boundary */
    ui_popup_block_clip(window, block);

    /* Avoid menu moving down and losing cursor focus by keeping it at
     * the same height. */
    if (handle->refresh && handle->prev_block_rect.ymax > block->rect.ymax) {
      if (block->bounds_type != UI_BLOCK_BOUNDS_POPUP_CENTER) {
        const float offset = handle->prev_block_rect.ymax - block->rect.ymax;
        UI_block_translate(block, 0, offset);
        block->rect.ymin = handle->prev_block_rect.ymin;
      }
    }

    handle->prev_block_rect = block->rect;

    /* the block and buttons were positioned in window space as in 2.4x, now
     * these menu blocks are regions so we bring it back to region space.
     * additionally we add some padding for the menu shadow or rounded menus */
    region->winrct.xmin = block->rect.xmin - margin;
    region->winrct.xmax = block->rect.xmax + margin;
    region->winrct.ymin = block->rect.ymin - margin;
    region->winrct.ymax = block->rect.ymax + UI_POPUP_MENU_TOP;

    UI_block_translate(block, -region->winrct.xmin, -region->winrct.ymin);

    /* apply scroll offset */
    if (handle->scrolloffset != 0.0f) {
      LISTBASE_FOREACH (uiBut *, bt, &block->buttons) {
        bt->rect.ymin += handle->scrolloffset;
        bt->rect.ymax += handle->scrolloffset;
      }
    }
  }

  if (block_old) {
    block->oldblock = block_old;
    UI_block_update_from_old(C, block);
    UI_blocklist_free_inactive(C, region);
  }

  /* checks which buttons are visible, sets flags to prevent draw (do after region init) */
  ui_popup_block_scrolltest(block);

  /* adds subwindow */
  ED_region_floating_init(region);

  /* get winmat now that we actually have the subwindow */
  wmGetProjectionMatrix(block->winmat, &region->winrct);

  /* notify change and redraw */
  ED_region_tag_redraw(region);

  ED_region_update_rect(region);

#ifdef DEBUG
  window->eventstate = event_back;
  window->event_last_handled = event_last_back;
#endif

  return block;
}

uiPopupBlockHandle *ui_popup_block_create(bContext *C,
                                          ARegion *butregion,
                                          uiBut *but,
                                          uiBlockCreateFunc create_func,
                                          uiBlockHandleCreateFunc handle_create_func,
                                          void *arg,
                                          uiFreeArgFunc arg_free)
{
  wmWindow *window = CTX_wm_window(C);
  uiBut *activebut = UI_context_active_but_get(C);
  static ARegionType type;
  ARegion *region;
  uiBlock *block;
  uiPopupBlockHandle *handle;

  /* disable tooltips from buttons below */
  if (activebut) {
    UI_but_tooltip_timer_remove(C, activebut);
  }
  /* standard cursor by default */
  WM_cursor_set(window, WM_CURSOR_DEFAULT);

  /* create handle */
  handle = MEM_callocN(sizeof(uiPopupBlockHandle), "uiPopupBlockHandle");

  /* store context for operator */
  handle->ctx_area = CTX_wm_area(C);
  handle->ctx_region = CTX_wm_region(C);

  /* store vars to refresh popup (RGN_REFRESH_UI) */
  handle->popup_create_vars.create_func = create_func;
  handle->popup_create_vars.handle_create_func = handle_create_func;
  handle->popup_create_vars.arg = arg;
  handle->popup_create_vars.arg_free = arg_free;
  handle->popup_create_vars.but = but;
  handle->popup_create_vars.butregion = but ? butregion : NULL;
  copy_v2_v2_int(handle->popup_create_vars.event_xy, window->eventstate->xy);

  /* don't allow by default, only if popup type explicitly supports it */
  handle->can_refresh = false;

  /* create area region */
  region = ui_region_temp_add(CTX_wm_screen(C));
  handle->region = region;

  memset(&type, 0, sizeof(ARegionType));
  type.draw = ui_block_region_draw;
  type.layout = ui_block_region_refresh;
  type.regionid = RGN_TYPE_TEMPORARY;
  region->type = &type;

  UI_region_handlers_add(&region->handlers);

  block = ui_popup_block_refresh(C, handle, butregion, but);
  handle = block->handle;

  /* keep centered on window resizing */
  if (block->bounds_type == UI_BLOCK_BOUNDS_POPUP_CENTER) {
    type.listener = ui_block_region_popup_window_listener;
  }

  return handle;
}

void ui_popup_block_free(bContext *C, uiPopupBlockHandle *handle)
{
  /* If this popup is created from a popover which does NOT have keep-open flag set,
   * then close the popover too. We could extend this to other popup types too. */
  ARegion *region = handle->popup_create_vars.butregion;
  if (region != NULL) {
    LISTBASE_FOREACH (uiBlock *, block, &region->uiblocks) {
      if (block->handle && (block->flag & UI_BLOCK_POPOVER) &&
          (block->flag & UI_BLOCK_KEEP_OPEN) == 0) {
        uiPopupBlockHandle *menu = block->handle;
        menu->menuretval = UI_RETURN_OK;
      }
    }
  }

  if (handle->popup_create_vars.arg_free) {
    handle->popup_create_vars.arg_free(handle->popup_create_vars.arg);
  }

  ui_popup_block_remove(C, handle);

  MEM_freeN(handle);
}
