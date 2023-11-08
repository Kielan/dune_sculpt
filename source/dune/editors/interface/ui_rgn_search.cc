/* Search Box Rgn & Interaction */

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "types_id.h"
#include "mem_guardedalloc.h"

#include "types_userdef.h"

#include "lib_list.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "dune_cxt.h"
#include "dune_screen.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "api_access.hh"

#include "ui.hh"
#include "ui_icons.hh"
#include "ui_view2d.hh"

#include "lang.h"

#include "ed_screen.hh"

#include "gpu_state.h"
#include "ui_intern.hh"
#include "ui_rgns_intern.hh"

#define MENU_BORDER int(0.3f * U.widget_unit)

/* Search Box Creation */
struct uiSearchItems {
  int maxitem, totitem, maxstrlen;

  int offset, offset_i; /* offset for inserting in array */
  int more;             /* flag indicating there are more items */

  char **names;
  void **ptrs;
  int *icons;
  int *btn_flags;
  uint8_t *name_prefix_offsets;

  /* Is there any item with an icon? */
  bool has_icon;

  AutoComplete *autocpl;
  void *active;
};

struct uiSearchboxData {
  rcti bbox;
  uiFontStyle fstyle;
  uiSearchItems items;
  bool size_set;
  ARgn *btnrgn;
  BtnSearch *search_btn;
  /* index in items array */
  int active;
  /* when menu opened with enough space for this */
  bool noback;
  /* draw thumbnail previews, rather than list */
  bool preview;
  /* Use the UI_SEP_CHAR char for splitting shortcuts (good for ops, bad for data). */
  bool use_shortcut_sep;
  int prv_rows, prv_cols;
  /* Show the active icon and text after the last instance of this string.
   * Used so we can show leading txt to menu items less prominently (not related to 'use_sep'). */
  const char *sep_string;

  /* Owned by BtnSearch */
  void *search_arg;
  BtnSearchListenFn search_listener;
};

#define SEARCH_ITEMS 10

bool ui_search_item_add(uiSearchItems *items,
                        const char *name,
                        void *ptr,
                        int iconid,
                        const int btn_flag,
                        const uint8_t name_prefix_offset)
{
  /* hijack for autocomplete */
  if (items->autocpl) {
    ui_autocomplete_update_name(items->autocpl, name + name_prefix_offset);
    return true;
  }

  if (iconid) {
    items->has_icon = true;
  }

  /* hijack for finding active item */
  if (items->active) {
    if (poin == items->active) {
      items->offset_i = items->totitem;
    }
    items->totitem++;
    return true;
  }

  if (items->totitem >= items->maxitem) {
    items->more = 1;
    return false;
  }

  /* skip first items in list */
  if (items->offset_i > 0) {
    items->offset_i--;
    return true;
  }

  if (items->names) {
    lib_strncpy(items->names[items->totitem], name, items->maxstrlen);
  }
  if (items->ptrs) {
    items->ptrs[items->totitem] = ptr;
  }
  if (items->icons) {
    items->icons[items->totitem] = iconid;
  }

  if (name_prefix_offset != 0) {
    /* Lazy init, as this isn't used often. */
    if (items->name_prefix_offsets == nullptr) {
      items->name_prefix_offsets = (uint8_t *)mem_calloc(
          items->maxitem * sizeof(*items->name_prefix_offsets), __func__);
    }
    items->name_prefix_offsets[items->totitem] = name_prefix_offset;
  }

  /* Limit flags that can be set so flags such as 'UI_SEL' aren't accidentally set
   * which will cause problems, add others as needed. */
  lib_assert((btn_flag &
              ~(BTN_DISABLED | BTN_INACTIVE | BTN_REDALERT | BTN_HAS_SEP_CHAR)) == 0);
  if (items->btn_flags) {
    items->btn_flags[items->totitem] = btn_flag;
  }

  items->totitem++;

  return true;
}

int ui_searchbox_size_y()
{
  return SEARCH_ITEMS * UNIT_Y + 2 * UI_POPUP_MENU_TOP;
}

int ui_searchbox_size_x()
{
  return 12 * UI_UNIT_X;
}

int ui_search_items_find_index(uiSearchItems *items, const char *name)
{
  if (items->name_prefix_offsets != nullptr) {
    for (int i = 0; i < items->totitem; i++) {
      if (STREQ(name, items->names[i] + items->name_prefix_offsets[i])) {
        return i;
      }
    }
  }
  else {
    for (int i = 0; i < items->totitem; i++) {
      if (STREQ(name, items->names[i])) {
        return i;
      }
    }
  }
  return -1;
}

/* rgn is the search box itself */
static void ui_searchbox_sel(Cxt *C, ARgn *rgn, Btn *btn, int step)
{
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);

  /* apply step */
  data->active += step;

  if (data->items.totitem == 0) {
    data->active = -1;
  }
  else if (data->active >= data->items.totitem) {
    if (data->items.more) {
      data->items.offset++;
      data->active = data->items.totitem - 1;
      ui_searchbox_update(C, rgn, btn, false);
    }
    else {
      data->active = data->items.totitem - 1;
    }
  }
  else if (data->active < 0) {
    if (data->items.offset) {
      data->items.offset--;
      data->active = 0;
      ui_searchbox_update(C, rgn, btn, false);
    }
    else {
      /* only let users step into an 'unset' state for unlink btns */
      data->active = (btn->flag & BTN_VALUE_CLEAR) ? -1 : 0;
    }
  }

  ed_rgn_tag_redraw(rgn);
}

static void ui_searchbox_butrect(rcti *r_rect, uiSearchboxData *data, int itemnr)
{
  /* thumbnail preview */
  if (data->preview) {
    const int btnw = (lib_rcti_size_x(&data->bbox) - 2 * MENU_BORDER) / data->prv_cols;
    const int btnh = (lib_rcti_size_y(&data->bbox) - 2 * MENU_BORDER) / data->prv_rows;
    int row, col;

    *r_rect = data->bbox;

    col = itemnr % data->prv_cols;
    row = itemnr / data->prv_cols;

    r_rect->xmin += MENU_BORDER + (col * btnw);
    r_rect->xmax = r_rect->xmin + btnw;

    r_rect->ymax -= MENU_BORDER + (row * btnh);
    r_rect->ymin = r_rect->ymax - btnh;
  }
  /* list view */
  else {
    const int btnh = (lib_rcti_size_y(&data->bbox) - 2 * UI_POPUP_MENU_TOP) / SEARCH_ITEMS;

    *r_rect = data->bbox;
    r_rect->xmin = data->bbox.xmin + 3.0f;
    r_rect->xmax = data->bbox.xmax - 3.0f;

    r_rect->ymax = data->bbox.ymax - UI_POPUP_MENU_TOP - itemnr * btnh;
    r_rect->ymin = r_rect->ymax - buth;
  }
}

int ui_searchbox_find_index(ARgn *rgn, const char *name)
{
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);
  return ui_search_items_find_index(&data->items, name);
}

bool ui_searchbox_inside(ARgn *rgn, const int xy[2])
{
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);

  return lib_rcti_isect_pt(&data->bbox, xy[0] - rgn->winrct.xmin, xy[1] - rgn->winrct.ymin);
}

bool ui_searchbox_apply(Btn *btn, ARgn *rgn)
{
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);
  BtnSearch *search_btn = (BtnSearch *)btn;

  lib_assert(btn->type == BTYPE_SEARCH_MENU);

  search_btn->item_active = nullptr;

  if (data->active != -1) {
    const char *name = data->items.names[data->active] +
                       /* Never include the prefix in the btn. */
                       (data->items.name_prefix_offsets ?
                            data->items.name_prefix_offsets[data->active] :
                            0);

    const char *name_sep = data->use_shortcut_sep ? strrchr(name, UI_SEP_CHAR) : nullptr;

    /* Search btn with dynamic string props may have their own method of applying
     * the search results, so only copy the result if there is a proper space for it. */
    if (btn->hardmax != 0) {
      lib_strncpy(btn->editstr, name, name_sep ? (name_sep - name) + 1 : data->items.maxstrlen);
    }

    search_btn->item_active = data->items.ptrs[data->active];
    MEM_SAFE_FREE(search_btn->item_active_str);
    search_btn->item_active_str = lib_strdup(data->items.names[data->active]);

    return true;
  }
  return false;
}

static ARgn *win_searchbox_tooltip_init(
    Cxt *C, ARgn *rgn, int * /*r_pass*/, double * /*pass_delay*/, bool *r_exit_on_ev)
{
  *r_exit_on_ev = true;

  LIST_FOREACH (uiBlock *, block, &rgn->uiblocks) {
    LIST_FOREACH (Btn *, btn, &block->btns) {
      if (btn->type != BTYPE_SEARCH_MENU) {
        continue;
      }

      BtnSearch *search_btn = (BtnSearch *)btn;
      if (!search_btn->item_tooltip_fn) {
        continue;
      }

      ARgn *searchbox_rgn = ui_rgn_searchbox_rgn_get(rgn);
      uiSearchboxData *data = static_cast<uiSearchboxData *>(searchbox_rgn->rgndata);

      lib_assert(data->items.ptrs[data->active] == search_btn->item_active);

      rcti rect;
      ui_searchbox_btnrect(&rect, data, data->active);

      return search_btn->item_tooltip_fn(
          C, rgn, &rect, search_btn->arg, search_btn->item_active);
    }
  }
  return nullptr;
}

bool ui_searchbox_ev(
    Cxt *C, ARgn *rgn, Btn *btn, ARgn *btnrgn, const WinEv *ev)
{
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);
  BtnSearch *search_but = (BtnSearch *)btn;
  int type = ev->type, val = ev->val;
  bool handled = false;
  bool tooltip_timer_started = false;

  lib_assert(btn->type == BTYPE_SEARCH_MENU);

  if (type == MOUSEPAN) {
    ui_pan_to_scroll(ev, &type, &val);
  }

  switch (type) {
    case WHEELUPMOUSE:
    case EV_UPARROWKEY:
      ui_searchbox_sel(C, rgn, btn, -1);
      handled = true;
      break;
    case WHEELDOWNMOUSE:
    case EV_DOWNARROWKEY:
      ui_searchbox_sel(C, rgn, btn, 1);
      handled = true;
      break;
    case RIGHTMOUSE:
      if (val) {
        if (search_btn->item_cxt_menu_fn) {
          if (data->active != -1) {
            /* Check the cursor is over the active element
             * (a little confusing if this isn't the case, although it does work). */
            rcti rect;
            ui_searchbox_btnrect(&rect, data, data->active);
            if (lib_rcti_isect_pt(
                    &rect, ev->xy[0] - rgn->winrct.xmin, ev->xy[1] - rgn->winrct.ymin))
            {

              void *active = data->items.ptrs[data->active];
              if (search_btn->item_cxt_menu_fn(C, search_btn->arg, active, ev)) {
                handled = true;
              }
            }
          }
        }
      }
      break;
    case MOUSEMOVE: {
      bool is_inside = false;

      if (lib_rcti_isect_pt(&rgn->winrct, ev->xy[0], ev->xy[1])) {
        rcti rect;
        int a;

        for (a = 0; a < data->items.totitem; a++) {
          ui_searchbox_btnrect(&rect, data, a);
          if (lib_rcti_isect_pt(
                  &rect, ev->xy[0] - rgn->winrct.xmin, ev->xy[1] - rgn->winrct.ymin))
          {
            is_inside = true;
            if (data->active != a) {
              data->active = a;
              ui_searchbox_sel(C, rgn, btn, 0);
              handled = true;
              break;
            }
          }
        }
      }

      if (U.flag & USER_TOOLTIPS) {
        if (is_inside) {
          if (data->active != -1) {
            ScrArea *area = cxt_win_area(C);
            search_btn->item_active = data->items.ptrs[data->active];
            win_tooltip_timer_init(C, cxt_win(C), area, btnrgn, win_searchbox_tooltip_init);
            tooltip_timer_started = true;
          }
        }
      }

      break;
    }
  }

  if (handled && (tooltip_timer_started == false)) {
    Win *win = cxt_win(C);
    win_tooltip_clear(C, win);
  }

  return handled;
}

/* Wrap BtnSearchUpdateFn cb */
static void ui_searchbox_update_fn(Cxt *C,
                                   BtnSearch *btn,
                                   const char *str,
                                   uiSearchItems *items)
{
  /* While the btn is in txt editing mode (searchbox open), remove tooltips on every update. */
  if (btn->editstr) {
    Win *win = cxt_win(C);
    win_tooltip_clear(C, win);
  }
  const bool is_first_search = !btn->changed;
  btn->items_update_fn(C, btn->arg, str, items, is_first_search);
}

void ui_searchbox_update(Cxt *C, ARgn *rgn, Btn *btn, const bool reset)
{
  BtnSearch *search_btn = (BtnSearch *)btn;
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);

  lib_assert(btn->type == BTYPE_SEARCH_MENU);

  /* reset vars */
  data->items.totitem = 0;
  data->items.more = 0;
  if (!reset) {
    data->items.offset_i = data->items.offset;
  }
  else {
    data->items.offset_i = data->items.offset = 0;
    data->active = -1;

    /* On init, find and center active item. */
    const bool is_first_search = !btn->changed;
    if (is_first_search && search_btn->items_update_fn && search_btn->item_active) {
      data->items.active = search_btn->item_active;
      ui_searchbox_update_fn(C, search_btn, btn->editstr, &data->items);
      data->items.active = nullptr;

      /* found active item, calculate real offset by centering it */
      if (data->items.totitem) {
        /* first case, begin of list */
        if (data->items.offset_i < data->items.maxitem) {
          data->active = data->items.offset_i;
          data->items.offset_i = 0;
        }
        else {
          /* second case, end of list */
          if (data->items.totitem - data->items.offset_i <= data->items.maxitem) {
            data->active = data->items.offset_i - data->items.totitem + data->items.maxitem;
            data->items.offset_i = data->items.totitem - data->items.maxitem;
          }
          else {
            /* center active item */
            data->items.offset_i -= data->items.maxitem / 2;
            data->active = data->items.maxitem / 2;
          }
        }
      }
      data->items.offset = data->items.offset_i;
      data->items.totitem = 0;
    }
  }

  /* cb */
  if (search_btn->items_update_fn) {
    ui_searchbox_update_fn(C, search_btn, btn->editstr, &data->items);
  }

  /* handle case where editstr is equal to one of items */
  if (reset && data->active == -1) {
    for (int a = 0; a < data->items.totitem; a++) {
      const char *name = data->items.names[a] +
                         /* Never include the prefix in the btn. */
                         (data->items.name_prefix_offsets ? data->items.name_prefix_offsets[a] :
                                                            0);
      const char *name_sep = data->use_shortcut_sep ? strrchr(name, UI_SEP_CHAR) : nullptr;
      if (STREQLEN(but->editstr, name, name_sep ? (name_sep - name) : data->items.maxstrlen)) {
        data->active = a;
        break;
      }
    }
    if (data->items.totitem == 1 && btn->editstr[0]) {
      data->active = 0;
    }
  }

  /* validate sel'd item */
  ui_searchbox_sel(C, rgn, btn, 0);

  ed_rgn_tag_redraw(rgn);
}

int ui_searchbox_autocomplete(Cxt *C, ARgn *rgn, Btn *btn, char *str)
{
  BtnSearch *search_btn = (BtnSearch *)btn;
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);
  int match = AUTOCOMPLETE_NO_MATCH;

  lib_assert(btn->type == BTYPE_SEARCH_MENU);

  if (str[0]) {
    data->items.autocpl = ui_autocomplete_begin(str, btn_string_get_maxncpy(but));

    ui_searchbox_update_fn(C, search_btn, btn->editstr, &data->items);

    match = ui_autocomplete_end(data->items.autocpl, str);
    data->items.autocpl = nullptr;
  }

  return match;
}

static void ui_searchbox_rgn_draw_fn(const Cxt *C, ARgn *rgn)
{
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);

  /* pixel space */
  winOrtho2_rgn_pixelspace(rgn);

  if (data->noback == false) {
    ui_draw_widget_menu_back(&data->bbox, true);
  }

  /* draw txt */
  if (data->items.totitem) {
    rcti rect;

    if (data->preview) {
      /* draw items */
      for (int a = 0; a < data->items.totitem; a++) {
        const int btn_flag = ((a == data->active) ? UI_HOVER : 0) | data->items.btn_flags[a];

        /* ensure icon is up-to-date */
        ui_icon_ensure_deferred(C, data->items.icons[a], data->preview);

        ui_searchbox_btnrect(&rect, data, a);

        /* widget itself */
        ui_draw_preview_item(&data->fstyle,
                             &rect,
                             data->items.names[a],
                             data->items.icons[a],
                             btn_flag,
                             UI_STYLE_TXT_LEFT);
      }

      /* indicate more */
      if (data->items.more) {
        ui_searchbox_btnrect(&rect, data, data->items.maxitem - 1);
        gpu_blend(GPU_BLEND_ALPHA);
        ui_icon_draw(rect.xmax - 18, rect.ymin - 7, ICON_TRIA_DOWN);
        gpu_blend(GPU_BLEND_NONE);
      }
      if (data->items.offset) {
        ui_searchbox_btnrect(&rect, data, 0);
        gpu_blend(GPU_BLEND_ALPHA);
        ui_icon_draw(rect.xmin, rect.ymax - 9, ICON_TRIA_UP);
        gpu_blend(GPU_BLEND_NONE);
      }
    }
    else {
      const int search_sep_len = data->sep_string ? strlen(data->sep_string) : 0;
      /* draw items */
      for (int a = 0; a < data->items.totitem; a++) {
        const int btn_flag = ((a == data->active) ? UI_HOVER : 0) | data->items.btn_flags[a];
        char *name = data->items.names[a];
        int icon = data->items.icons[a];
        char *name_sep_test = nullptr;

        uiMenuItemSeparatorType separator_type = UI_MENU_ITEM_SEPARATOR_NONE;
        if (data->use_shortcut_sep) {
          separator_type = UI_MENU_ITEM_SEPARATOR_SHORTCUT;
        }
        /* Only set for displaying additional hint (e.g. lib name of a linked data-block). */
        else if (btn_flag & BTN_HAS_SEP_CHAR) {
          separator_type = UI_MENU_ITEM_SEPARATOR_HINT;
        }

        ui_searchbox_btnrect(&rect, data, a);

        /* widget itself */
        if ((search_sep_len == 0) ||
            !(name_sep_test = strstr(data->items.names[a], data->sep_string))) {
          if (!icon && data->items.has_icon) {
            /* If there is any icon item, make sure all items line up. */
            icon = ICON_BLANK1;
          }

          /* Simple menu item. */
          ui_draw_menu_item(&data->fstyle, &rect, name, icon, btn_flag, separator_type, nullptr);
        }
        else {
          /* Split menu item, faded txt before the separator. */
          char *name_sep = nullptr;
          do {
            name_sep = name_sep_test;
            name_sep_test = strstr(name_sep + search_sep_len, data->sep_string);
          } while (name_sep_test != nullptr);

          name_sep += search_sep_len;
          const char name_sep_prev = *name_sep;
          *name_sep = '\0';
          int name_width = 0;
          ui_draw_menu_item(&data->fstyle,
                            &rect,
                            name,
                            0,
                            btn_flag | BTN_INACTIVE,
                            UI_MENU_ITEM_SEPARATOR_NONE,
                            &name_width);
          *name_sep = name_sep_prev;
          rect.xmin += name_width;
          rect.xmin += UI_UNIT_X / 4;

          if (icon == ICON_BLANK1) {
            icon = ICON_NONE;
            rect.xmin -= UI_ICON_SIZE / 4;
          }

          /* The previous menu item draws the active sel */
          ui_draw_menu_item(
              &data->fstyle, &rect, name_sep, icon, btn_flag, separator_type, nullptr);
        }
      }
      /* indicate more */
      if (data->items.more) {
        ui_searchbox_btnrect(&rect, data, data->items.maxitem - 1);
        gpu_blend(GPU_BLEND_ALPHA);
        ui_icon_draw(lib_rcti_size_x(&rect) / 2, rect.ymin - 9, ICON_TRIA_DOWN);
        gpu_blend(GPU_BLEND_NONE);
      }
      if (data->items.offset) {
        ui_searchbox_butrect(&rect, data, 0);
        gpu_blend(GPU_BLEND_ALPHA);
        ui_icon_draw(BLI_rcti_size_x(&rect) / 2, rect.ymax - 7, ICON_TRIA_UP);
        gpu_blend(GPU_BLEND_NONE);
      }
    }
  }
}

static void ui_searchbox_rgn_free_fn(ARgn *rgn)
{
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);

  /* free search data */
  for (int a = 0; a < data->items.maxitem; a++) {
    mem_free(data->items.names[a]);
  }
  mem_free(data->items.names);
  mem_free(data->items.ptrs);
  mem_free(data->items.icons);
  mem_free(data->items.btn_flags);

  if (data->items.name_prefix_offsets != nullptr) {
    mem_freeN(data->items.name_prefix_offsets);
  }

  mem_free(data);
  rgn->rgndata = nullptr;
}

static void ui_searchbox_rgn_listen_fn(const WinRgnListenerParams *params)
{
  uiSearchboxData *data = static_cast<uiSearchboxData *>(params->rgn->rgndata);
  if (data->search_listener) {
    data->search_listener(params, data->search_arg);
  }
}

static uiMenuItemSeparatorType ui_searchbox_item_separator(uiSearchboxData *data)
{
  uiMenuItemSeparatorType separator_type = data->use_shortcut_sep ?
                                               UI_MENU_ITEM_SEPARATOR_SHORTCUT :
                                               UI_MENU_ITEM_SEPARATOR_NONE;
  if (separator_type == UI_MENU_ITEM_SEPARATOR_NONE && !data->preview) {
    for (int a = 0; a < data->items.totitem; a++) {
      if (data->items.btn_flags[a] & BTN_HAS_SEP_CHAR) {
        separator_type = UI_MENU_ITEM_SEPARATOR_HINT;
        break;
      }
    }
  }
  return separator_type;
}

static void ui_searchbox_rgn_layout_fn(const Cxt *C, ARgn *rgn)
{
  uiSearchboxData *data = (uiSearchboxData *)rgn->rgndata;

  if (data->size_set) {
    /* Already set. */
    return;
  }

  BtnSearch *btn = data->search_btn;
  ARgn *btnrgn = data->btnrgn;
  const int margin = UI_POPUP_MARGIN;
  Win *win = cxt_win(C);

  /* compute pos */
  if (btn->block->flag & UI_BLOCK_SEARCH_MENU) {
    const int search_btn_h = lib_rctf_size_y(&btn->rect) + 10;
    /* this case is search menu inside other menu */
    /* we copy rgn size */

    rgn->winrct = btnrgn->winrct;

    /* widget rect, in rgn coords */
    data->bbox.xmin = margin;
    data->bbox.xmax = lib_rcti_size_x(&rgn->winrct) - margin;
    data->bbox.ymin = margin;
    data->bbox.ymax = lib_rcti_size_y(&rgn->winrct) - margin;

    /* check if btn is lower half */
    if (btn->rect.ymax < lib_rctf_cent_y(&btn->block->rect)) {
      data->bbox.ymin += search_btn_h;
    }
    else {
      data->bbox.ymax -= search_btn_h;
    }
  }
  else {
    int searchbox_width = ui_searchbox_size_x();

    /* We should make this wider if there is a path or hint on the right. */
    if (ui_searchbox_item_separator(data) != UI_MENU_ITEM_SEPARATOR_NONE) {
      searchbox_width += 12 * data->fstyle.points * UI_SCALE_FAC;
    }

    rctf rect_fl;
    rect_fl.xmin = btn->rect.xmin - 5; /* align txt with btn */
    rect_fl.xmax = btn->rect.xmax + 5; /* symmetrical */
    rect_fl.ymax = btn->rect.ymin;
    rect_fl.ymin = rect_fl.ymax - ui_searchbox_size_y();

    const int ofsx = (btn->block->panel) ? btn->block->panel->ofsx : 0;
    const int ofsy = (btn->block->panel) ? btn->block->panel->ofsy : 0;

    lib_rctf_translate(&rect_fl, ofsx, ofsy);

    /* minimal width */
    if (lib_rctf_size_x(&rect_fl) < searchbox_width) {
      rect_fl.xmax = rect_fl.xmin + searchbox_width;
    }

    /* copy to int, gets projected if possible too */
    rcti rect_i;
    lib_rcti_rctf_copy(&rect_i, &rect_fl);

    if (btnrgn->v2d.cur.xmin != btrgn->v2d.cur.xmax) {
      ui_view2d_view_to_rgn_rcti(&btrgn->v2d, &rect_fl, &rect_i);
    }

    lib_rcti_translate(&rect_i, btnrgn->winrct.xmin, btrgn->winrct.ymin);

    int winx = win_pixels_x(win);
    // winy = win_pixels_y(win);  /* UNUSED */
    // win_get_size(win, &winx, &winy);

    if (rect_i.xmax > winx) {
      /* super size */
      if (rect_i.xmax > winx + rect_i.xmin) {
        rect_i.xmax = winx;
        rect_i.xmin = 0;
      }
      else {
        rect_i.xmin -= rect_i.xmax - winx;
        rect_i.xmax = winx;
      }
    }

    if (rect_i.ymin < 0) {
      int newy1 = btn->rect.ymax + ofsy;

      if (btnrgn->v2d.cur.xmin != btnrgn->v2d.cur.xmax) {
        newy1 = ui_view2d_view_to_rgn_y(&btnrgn->v2d, newy1);
      }

      newy1 += btnrgn->winrct.ymin;

      rect_i.ymax = lib_rcti_size_y(&rect_i) + newy1;
      rect_i.ymin = newy1;
    }

    /* widget rect, in rgn coords */
    data->bbox.xmin = margin;
    data->bbox.xmax = lib_rcti_size_x(&rect_i) + margin;
    data->bbox.ymin = margin;
    data->bbox.ymax = lib_rcti_size_y(&rect_i) + margin;

    /* rgn bigger for shadow */
    rgn->winrct.xmin = rect_i.xmin - margin;
    rgn->winrct.xmax = rect_i.xmax + margin;
    rgn->winrct.ymin = rect_i.ymin - margin;
    rgn->winrct.ymax = rect_i.ymax;
  }

  rgn->winx = rgn->winrct.xmax - rgn->winrct.xmin + 1;
  rgn->winy = rgn->winrct.ymax - rgn->winrct.ymin + 1;

  data->size_set = true;
}

static ARgn *ui_searchbox_create_generic_ex(Cxt *C,
                                            ARgn *btnrgn,
                                            BtnSearch *btn,
                                            const bool use_shortcut_sep)
{
  const uiStyle *style = ui_style_get();
  const float aspect = btn->block->aspect;

  /* create area rgn */
  ARgn *rgn = ui_rgn_temp_add(cxt_win_screen(C));

  static ARgnType type;
  memset(&type, 0, sizeof(ARgnType));
  type.layout = ui_searchbox_rgn_layout_fn;
  type.draw = ui_searchbox_rgn_draw_fn;
  type.free = ui_searchbox_rgn_free_fn;
  type.listener = ui_searchbox_rgn_listen_fn;
  type.rgnid = RGN_TYPE_TEMPORARY;
  rgn->type = &type;

  /* Create search-box data. */
  uiSearchboxData *data = mem_cnew<uiSearchboxData>(__func__);
  data->search_arg = btn->arg;
  data->search_btn = btn;
  data->btnrgn = btnrgn;
  data->size_set = false;
  data->search_listener = btn->listen_fn;

  /* Set font, get the bounding-box. */
  data->fstyle = style->widget; /* copy struct */
  ui_fontscale(&data->fstyle.points, aspect);
  ui_fontstyle_set(&data->fstyle);

  rgn->rgndata = data;

  /* Special case, hard-coded feature, not draw backdrop when called from menus,
   * assume for design that popup already added it. */
  if (btn->block->flag & UI_BLOCK_SEARCH_MENU) {
    data->noback = true;
  }

  if (btn->a1 > 0 && btn->a2 > 0) {
    data->preview = true;
    data->prv_rows = btn->a1;
    data->prv_cols = btn->a2;
  }

  if (btn->optype != nullptr || use_shortcut_sep) {
    data->use_shortcut_sep = true;
  }
  data->sep_string = btn->item_sep_string;

  /* Adds sub-win. */
  ed_rgn_floating_init(rgn);

  /* notify change and redraw */
  ed_rgn_tag_redraw(rgn);

  /* prepare search data */
  if (data->preview) {
    data->items.maxitem = data->prv_rows * data->prv_cols;
  }
  else {
    data->items.maxitem = SEARCH_ITEMS;
  }
  /* In case the btn's string is dynamic, make sure there are buffers available. */
  data->items.maxstrlen = btn->hardmax == 0 ? UI_MAX_NAME_STR : btn->hardmax;
  data->items.totitem = 0;
  data->items.names = (char **)mem_calloc(data->items.maxitem * sizeof(void *), __func__);
  data->items.ptrs = (void **)mem_calloc(data->items.maxitem * sizeof(void *), __func__);
  data->items.icons = (int *)mem_calloc(data->items.maxitem * sizeof(int), __func__);
  data->items.btn_flags = (int *)mem_calloc(data->items.maxitem * sizeof(int), __func__);
  data->items.name_prefix_offsets = nullptr; /* Lazy initialized as needed. */
  for (int i = 0; i < data->items.maxitem; i++) {
    data->items.names[i] = (char *)mem_calloc(data->items.maxstrlen + 1, __func__);
  }

  return rgn;
}

ARgn *ui_searchbox_create_generic(Ctxt *C, ARgn *btnrgn, BtnSearch *search_btn)
{
  return ui_searchbox_create_generic_ex(C, btnrgn, search_btn, false);
}

/* Similar to Python's `str.title` except...
 * - we know words are upper case and ascii only.
 * - '_' are replaces by spaces */
static void str_tolower_titlecaps_ascii(char *str, const size_t len)
{
  bool prev_delim = true;

  for (size_t i = 0; (i < len) && str[i]; i++) {
    if (str[i] >= 'A' && str[i] <= 'Z') {
      if (prev_delim == false) {
        str[i] += 'a' - 'A';
      }
    }
    else if (str[i] == '_') {
      str[i] = ' ';
    }

    prev_delim = ELEM(str[i], ' ') || (str[i] >= '0' && str[i] <= '9');
  }
}

static void ui_searchbox_rgn_draw_cb__op(const Cxt * /*C*/, ARgn *rgn)
{
  uiSearchboxData *data = static_cast<uiSearchboxData *>(rgn->rgndata);

  /* pixel space */
  WinOrtho2_rgn_pixelspace(rgn);

  if (data->noback == false) {
    ui_draw_widget_menu_back(&data->bbox, true);
  }

  /* draw txt */
  if (data->items.totitem) {
    rcti rect;

    /* draw items */
    for (int a = 0; a < data->items.totitem; a++) {
      rcti rect_pre, rect_post;
      ui_searchbox_btnrect(&rect, data, a);

      rect_pre = rect;
      rect_post = rect;

      rect_pre.xmax = rect_post.xmin = rect.xmin + ((rect.xmax - rect.xmin) / 4);

      /* widget itself */
      /* i18n msgs extracting tool does the same, please keep it in sync. */
      {
        const int btn_flag = ((a == data->active) ? UI_HOVER : 0) | data->items.btn_flags[a];

        WinOpType *ot = static_cast<WinOpType *>(data->items.ptrs[a]);
        char txt_pre[128];
        const char *txt_pre_p = strstr(ot->idname, "_OT_");
        if (txt_pre_p == nullptr) {
          txt_pre[0] = '\0';
        }
        else {
          int txt_pre_len;
          txt_pre_p += 1;
          txt_pre_len = lib_strncpy_rlen(
              txt_pre, ot->idname, min_ii(sizeof(txt_pre), txt_pre_p - ot->idname));
          txt_pre[txt_pre_len] = ':';
          txt_pre[txt_pre_len + 1] = '\0';
          str_tolower_titlecaps_ascii(txt_pre, sizeof(txt_pre));
        }

        rect_pre.xmax += 4; /* sneaky, avoid showing ugly margin */
        ui_draw_menu_item(&data->fstyle,
                          &rect_pre,
                          CXT_IFACE_(LANG_CXT_OP_DEFAULT, txt_pre),
                          data->items.icons[a],
                          btn_flag,
                          UI_MENU_ITEM_SEPARATOR_NONE,
                          nullptr);
        ui_draw_menu_item(&data->fstyle,
                          &rect_post,
                          data->items.names[a],
                          0,
                          btn_flag,
                          data->use_shortcut_sep ? UI_MENU_ITEM_SEPARATOR_SHORTCUT :
                                                   UI_MENU_ITEM_SEPARATOR_NONE,
                          nullptr);
      }
    }
    /* indicate more */
    if (data->items.more) {
      ui_searchbox_btnrect(&rect, data, data->items.maxitem - 1);
      gpu_blend(GPU_BLEND_ALPHA);
      ui_icon_draw(lib_rcti_size_x(&rect) / 2, rect.ymin - 9, ICON_TRIA_DOWN);
      gpu_blend(GPU_BLEND_NONE);
    }
    if (data->items.offset) {
      ui_searchbox_btnrect(&rect, data, 0);
      gpu_blend(GPU_BLEND_ALPHA);
      ui_icon_draw(lib_rcti_size_x(&rect) / 2, rect.ymax - 7, ICON_TRIA_UP);
      gpu_blend(GPU_BLEND_NONE);
    }
  }
}

ARgn *ui_searchbox_create_op(Cxt *C, ARgn *btnrgn, BtnSearch *search_btn)
{
  ARgn *rgn = ui_searchbox_create_generic_ex(C, btnrgn, search_btn, true);

  rgn->type->draw = ui_searchbox_rgn_draw_cb_op;

  return rgn;
}

void ui_searchbox_free(Cxt *C, ARgn *rgn)
{
  ui_rgn_temp_remove(C, cxt_win_screen(C), rgn);
}

static void ui_searchbox_rgn_draw_cb_menu(const Cxt * /*C*/, ARgn * /*rgn*/)
{
  /* Currently unused. */
}

ARgn *ui_searchbox_create_menu(Cxt *C, ARgn *btnrgn, BtnSearch *search_btn)
{
  ARgn *rgn = ui_searchbox_create_generic_ex(C, btnrgn, search_btn, true);

  if (false) {
    rgn->type->draw = ui_searchbox_rgn_draw_cb_menu;
  }

  return rgn;
}

void btn_search_refresh(BtnSearch *btn)
{
  /* possibly very large lists (such as Id datablocks) only
   * only validate string api btns (not ptrs) */
  if (btn->apiprop && api_prop_type(btn->apiprop) != PROP_STRING) {
    return;
  }

  uiSearchItems *items = mem_cnew<uiSearchItems>(__func__);

  /* setup search struct */
  items->maxitem = 10;
  items->maxstrlen = 256;
  items->names = (char **)mem_calloc(items->maxitem * sizeof(void *), __func__);
  for (int i = 0; i < items->maxitem; i++) {
    items->names[i] = (char *)mem_calloc(btn->hardmax + 1, __func__);
  }

  ui_searchbox_update_fn((Cxt *)btn->block->evil_C, btn, btn->drawstr, items);

  if (!btn->results_are_suggestions) {
    /* Only red-alert when we are sure of it, this can miss cases when >10 matches. */
    if (items->totitem == 0) {
      btn_flag_enable(btn, BTN_REDALERT);
    }
    else if (items->more == 0) {
      if (ui_search_items_find_index(items, btn->drawstr) == -1) {
        btn_flag_enable(btn, BTN_REDALERT);
      }
    }
  }

  for (int i = 0; i < items->maxitem; i++) {
    mem_free(items->names[i]);
  }
  mem_free(items->names);
  mem_freeN(items);
}
