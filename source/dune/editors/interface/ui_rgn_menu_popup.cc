/** \file
 * \ingroup edinterface
 *
 * PopUp Menu Region
 */

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "MEM_guardedalloc.h"

#include "DNA_userdef_types.h"

#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_math_vector.h"
#include "BLI_rect.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"
#include "BKE_report.h"
#include "BKE_screen.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "RNA_access.hh"

#include "UI_interface.hh"

#include "BLT_translation.h"

#include "ED_screen.hh"

#include "interface_intern.hh"
#include "interface_regions_intern.hh"

/* -------------------------------------------------------------------- */
/** \name Utility Functions
 * \{ */

bool ui_but_menu_step_poll(const uiBut *but)
{
  BLI_assert(but->type == UI_BTYPE_MENU);

  /* currently only RNA buttons */
  return ((but->menu_step_func != nullptr) ||
          (but->rnaprop && RNA_property_type(but->rnaprop) == PROP_ENUM));
}

int ui_but_menu_step(uiBut *but, int direction)
{
  if (ui_but_menu_step_poll(but)) {
    if (but->menu_step_func) {
      return but->menu_step_func(
          static_cast<bContext *>(but->block->evil_C), direction, but->poin);
    }

    const int curval = RNA_property_enum_get(&but->rnapoin, but->rnaprop);
    return RNA_property_enum_step(static_cast<bContext *>(but->block->evil_C),
                                  &but->rnapoin,
                                  but->rnaprop,
                                  curval,
                                  direction);
  }

  printf("%s: cannot cycle button '%s'\n", __func__, but->str);
  return 0;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Popup Menu Memory
 *
 * Support menu-memory, a feature that positions the cursor
 * over the previously used menu item.
 *
 * \note This is stored for each unique menu title.
 * \{ */

static uint ui_popup_string_hash(const char *str, const bool use_sep)
{
  /* sometimes button contains hotkey, sometimes not, strip for proper compare */
  int hash;
  const char *delimit = use_sep ? strrchr(str, UI_SEP_CHAR) : nullptr;

  if (delimit) {
    hash = BLI_ghashutil_strhash_n(str, delimit - str);
  }
  else {
    hash = BLI_ghashutil_strhash(str);
  }

  return hash;
}

uint ui_popup_menu_hash(const char *str)
{
  return BLI_ghashutil_strhash(str);
}

/* but == nullptr read, otherwise set */
static uiBut *ui_popup_menu_memory__internal(uiBlock *block, uiBut *but)
{
  static uint mem[256];
  static bool first = true;

  const uint hash = block->puphash;
  const uint hash_mod = hash & 255;

  if (first) {
    /* init */
    memset(mem, -1, sizeof(mem));
    first = false;
  }

  if (but) {
    /* set */
    mem[hash_mod] = ui_popup_string_hash(but->str, but->flag & UI_BUT_HAS_SEP_CHAR);
    return nullptr;
  }

  /* get */
  LISTBASE_FOREACH (uiBut *, but_iter, &block->buttons) {
    /* Prevent labels (typically headings), from being returned in the case the text
     * happens to matches one of the menu items.
     * Skip separators too as checking them is redundant. */
    if (ELEM(but_iter->type, UI_BTYPE_LABEL, UI_BTYPE_SEPR, UI_BTYPE_SEPR_LINE)) {
      continue;
    }
    if (mem[hash_mod] == ui_popup_string_hash(but_iter->str, but_iter->flag & UI_BUT_HAS_SEP_CHAR))
    {
      return but_iter;
    }
  }

  return nullptr;
}

uiBut *ui_popup_menu_memory_get(uiBlock *block)
{
  return ui_popup_menu_memory__internal(block, nullptr);
}

void ui_popup_menu_memory_set(uiBlock *block, uiBut *but)
{
  ui_popup_menu_memory__internal(block, but);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Popup Menu with Callback or String
 * \{ */

struct uiPopupMenu {
  uiBlock *block;
  uiLayout *layout;
  uiBut *but;
  ARegion *butregion;

  /* Menu hash is created from this, to keep a memory of recently opened menus. */
  const char *title;

  int mx, my;
  bool popup, slideout;

  std::function<void(bContext *C, uiLayout *layout)> menu_func;
};

/**
 * \param title: Optional. If set, it will be used to store recently opened menus so they can be
 *               opened with the mouse over the last chosen entry again.
 */
static void ui_popup_menu_create_block(bContext *C,
                                       uiPopupMenu *pup,
                                       const char *title,
                                       const char *block_name)
{
  const uiStyle *style = UI_style_get_dpi();

  pup->block = UI_block_begin(C, nullptr, block_name, UI_EMBOSS_PULLDOWN);

  /* A title is only provided when a Menu has a label, this is not always the case, see e.g.
   * `VIEW3D_MT_edit_mesh_context_menu` -- this specifies its own label inside the draw function
   * depending on vertex/edge/face mode. We still want to flag the uiBlock (but only insert into
   * the `puphash` if we have a title provided). Choosing an entry in a menu will still handle
   * `puphash` later (see `button_activate_exit`) though multiple menus without a label might fight
   * for the same storage of the menu memory. Using idname instead (or in combination with the
   * label) for the hash could be looked at to solve this. */
  pup->block->flag |= UI_BLOCK_POPUP_MEMORY;
  if (title && title[0]) {
    pup->block->puphash = ui_popup_menu_hash(title);
  }
  pup->layout = UI_block_layout(
      pup->block, UI_LAYOUT_VERTICAL, UI_LAYOUT_MENU, 0, 0, 200, 0, UI_MENU_PADDING, style);

  /* NOTE: this intentionally differs from the menu & sub-menu default because many operators
   * use popups like this to select one of their options -
   * where having invoke doesn't make sense.
   * When the menu was opened from a button, use invoke still for compatibility. This used to be
   * the default and changing now could cause issues. */
  const wmOperatorCallContext opcontext = pup->but ? WM_OP_INVOKE_REGION_WIN :
                                                     WM_OP_EXEC_REGION_WIN;

  uiLayoutSetOperatorContext(pup->layout, opcontext);

  if (pup->but) {
    if (pup->but->context) {
      uiLayoutContextCopy(pup->layout, pup->but->context);
    }
  }
}

static uiBlock *ui_block_func_POPUP(bContext *C, uiPopupBlockHandle *handle, void *arg_pup)
{
  uiPopupMenu *pup = static_cast<uiPopupMenu *>(arg_pup);

  int minwidth = 0;

  if (!pup->layout) {
    ui_popup_menu_create_block(C, pup, pup->title, __func__);

    if (pup->menu_func) {
      pup->block->handle = handle;
      pup->menu_func(C, pup->layout);
      pup->block->handle = nullptr;
    }

    if (uiLayoutGetUnitsX(pup->layout) != 0.0f) {
      /* Use the minimum width from the layout if it's set. */
      minwidth = uiLayoutGetUnitsX(pup->layout) * UI_UNIT_X;
    }

    pup->layout = nullptr;
  }

  /* Find block minimum width. */
  if (minwidth) {
    /* Skip. */
  }
  else if (pup->but) {
    /* Minimum width to enforce. */
    if (pup->but->drawstr[0]) {
      minwidth = BLI_rctf_size_x(&pup->but->rect);
    }
    else {
      /* For buttons with no text, use the minimum (typically icon only). */
      minwidth = UI_MENU_WIDTH_MIN;
    }
  }
  else {
    minwidth = UI_MENU_WIDTH_MIN;
  }

  /* Find block direction. */
  char direction;
  if (pup->but) {
    if (pup->block->direction != 0) {
      /* allow overriding the direction from menu_func */
      direction = pup->block->direction;
    }
    else {
      direction = UI_DIR_DOWN;
    }
  }
  else {
    direction = UI_DIR_DOWN;
  }

  bool flip = (direction == UI_DIR_DOWN);

  uiBlock *block = pup->block;

  /* in some cases we create the block before the region,
   * so we set it delayed here if necessary */
  if (BLI_findindex(&handle->region->uiblocks, block) == -1) {
    UI_block_region_set(block, handle->region);
  }

  block->direction = direction;

  int width, height;
  UI_block_layout_resolve(block, &width, &height);

  UI_block_flag_enable(block, UI_BLOCK_MOVEMOUSE_QUIT | UI_BLOCK_NUMSELECT);

  if (pup->popup) {
    int offset[2] = {0, 0};

    uiBut *but_activate = nullptr;
    UI_block_flag_enable(block, UI_BLOCK_LOOP);
    UI_block_theme_style_set(block, UI_BLOCK_THEME_STYLE_POPUP);
    UI_block_direction_set(block, direction);

    /* offset the mouse position, possibly based on earlier selection */
    if (!handle->refresh) {
      uiBut *bt;
      if ((block->flag & UI_BLOCK_POPUP_MEMORY) && (bt = ui_popup_menu_memory_get(block))) {
        /* position mouse on last clicked item, at 0.8*width of the
         * button, so it doesn't overlap the text too much, also note
         * the offset is negative because we are inverse moving the
         * block to be under the mouse */
        offset[0] = -(bt->rect.xmin + 0.8f * BLI_rctf_size_x(&bt->rect));
        offset[1] = -(bt->rect.ymin + 0.5f * UI_UNIT_Y);

        if (ui_but_is_editable(bt)) {
          but_activate = bt;
        }
      }
      else {
        /* position mouse at 0.8*width of the button and below the tile
         * on the first item */
        offset[0] = 0;
        LISTBASE_FOREACH (uiBut *, but_iter, &block->buttons) {
          offset[0] = min_ii(offset[0],
                             -(but_iter->rect.xmin + 0.8f * BLI_rctf_size_x(&but_iter->rect)));
        }

        offset[1] = 2.1 * UI_UNIT_Y;

        LISTBASE_FOREACH (uiBut *, but_iter, &block->buttons) {
          if (ui_but_is_editable(but_iter)) {
            but_activate = but_iter;
            break;
          }
        }
      }
      copy_v2_v2_int(handle->prev_bounds_offset, offset);
    }
    else {
      copy_v2_v2_int(offset, handle->prev_bounds_offset);
    }

    /* in rare cases this is needed since moving the popup
     * to be within the window bounds may move it away from the mouse,
     * This ensures we set an item to be active. */
    if (but_activate) {
      ARegion *region = CTX_wm_region(C);
      if (region && region->regiontype == RGN_TYPE_TOOLS && but_activate->block &&
          (but_activate->block->flag & UI_BLOCK_POPUP_HOLD))
      {
        /* In Toolbars, highlight the button with select color. */
        but_activate->flag |= UI_SELECT_DRAW;
      }
      ui_but_activate_over(C, handle->region, but_activate);
    }

    block->minbounds = minwidth;
    UI_block_bounds_set_menu(block, 1, offset);
  }
  else {
    /* for a header menu we set the direction automatic */
    if (!pup->slideout && flip) {
      ARegion *region = CTX_wm_region(C);
      if (region) {
        if (RGN_TYPE_IS_HEADER_ANY(region->regiontype)) {
          if (RGN_ALIGN_ENUM_FROM_MASK(region->alignment) == RGN_ALIGN_BOTTOM) {
            UI_block_direction_set(block, UI_DIR_UP);
          }
        }
      }
    }

    block->minbounds = minwidth;
    UI_block_bounds_set_text(block, 3.0f * UI_UNIT_X);
  }

  /* if menu slides out of other menu, override direction */
  if (pup->slideout) {
    UI_block_direction_set(block, UI_DIR_RIGHT);
  }

  return pup->block;
}

static void ui_block_free_func_POPUP(void *arg_pup)
{
  uiPopupMenu *pup = static_cast<uiPopupMenu *>(arg_pup);
  MEM_delete(pup);
}

static uiPopupBlockHandle *ui_popup_menu_create(
    bContext *C,
    ARgn *btnrgn,
    uiBtn *btn,
    const char *title,
    std::function<void(Cxt *, uiLayout *)> menu_fn)
{
  Win *win = cxt_win(C);

  uiPopupMenu *pup = mem_new<uiPopupMenu>(__func__);
  pup->title = title;
  /* menu created from a cb */
  pup->menu_fn = menu_fn;
  if (btn) {
    pup->slideout = ui_block_is_menu(btn->block);
    pup->btn = btn;

    if (btn->type == BTYPE_PULLDOWN) {
      ed_workspace_status_txt(C, TIP_("Press spacebar to search..."));
    }
  }

  if (!btn) {
    /* no btn to start from, means we are a popup */
    pup->mx = win->evstate->xy[0];
    pup->my = win->evstate->xy[1];
    pup->popup = true;
  }
  uiPopupBlockHandle *handle = ui_popup_block_create(
      C, btnrgn, btn, nullptr, ui_block_fn_POPUP, pup, ui_block_free_fn_POPUP);

  if (!btn) {
    handle->popup = true;

    ui_popup_handlers_add(C, &win->modalhandlers, handle, 0);
    win_ev_add_mousemove(win);
  }

  return handle;
}

uiPopupBlockHandle *ui_popup_menu_create(
    Cxt *C, ARgn *btnrgn, Btn *btn, uiMenuCreateFn menu_fn, void *arg)
{
  return ui_popup_menu_create(
      C, btnrgn, btn, nullptr, [menu_fn, arg](Cxt *C, uiLayout *layout) {
        menu_fn(C, layout, arg);
      });
}

/* Popup Menu API with begin & end */
static void create_title_btn(uiLayout *layout, const char *title, int icon)
{
  uiBlock *block = uiLayoutGetBlock(layout);
  char titlestr[256];

  if (icon) {
    SNPRINTF(titlestr, " %s", title);
    BtnIconTxt(block,
               BTYPE_LABEL,
               0,
               icon,
               titlestr,
               0,
               0,
               200,
               UNIT_Y,
               nullptr,
               0.0,
               0.0,
               0,
               0,
               "");
  }
  else {
    Btn *btm = Btn(
        block, BTYPE_LABEL, 0, title, 0, 0, 200, UNIT_Y, nullptr, 0.0, 0.0, 0, 0, "");
    btn->drawflag = BTN_TEXT_LEFT;
  }

  uiItemS(layout);
}

uiPopupMenu *ui_popup_menu_begin_ex(Cxt *C,
                                    const char *title,
                                    const char *block_name,
                                    int icon)
{
  uiPopupMenu *pup = mem_new<uiPopupMenu>(__func__);

  pup->title = title;

  ui_popup_menu_create_block(C, pup, title, block_name);

  /* create in advance so we can let buttons point to retval already */
  pup->block->handle = mem_cnew<uiPopupBlockHandle>(__func__);

  if (title[0]) {
    create_title_button(pup->layout, title, icon);
  }

  return pup;
}

uiPopupMenu *ui_popup_menu_begin(Cxt *C, const char *title, int icon)
{
  return ui_popup_menu_begin_ex(C, title, __func__, icon);
}

void ui_popup_menu_btn_set(uiPopupMenu *pup, ARgn *btnrgn, Btn *btn)
{
  pup->btn = btn;
  pup->btnrgn = btnrgn;
}

void ui_popup_menu_end(Cxt *C, uiPopupMenu *pup)
{
  Win *win = cxt_win(C);

  pup->popup = true;
  pup->mx = win->evstate->xy[0];
  pup->my = win->evtstate->xy[1];

  Btn *btn = nullptr;
  ARgn *btnrgn = nullptr;
  if (pup->btn) {
    btn = pup->btn;
    btnrgn = pup->btnrgn;
  }

  uiPopupBlockHandle *menu = ui_popup_block_create(
      C, btnrgn, btn, nullptr, ui_block_fn_POPUP, pup, nullptr);
  menu->popup = true;

  ui_popup_handlers_add(C, &win->modalhandlers, menu, 0);
  win_ev_add_mousemove(win);

  mem_delete(pup);
}

bool ui_popup_menu_end_or_cancel(Cxt *C, uiPopupMenu *pup)
{
  if (!ui_block_is_empty_ex(pup->block, true)) {
    ui_popup_menu_end(C, pup);
    return true;
  }
  ui_block_layout_resolve(pup->block, nullptr, nullptr);
  mem_free(pup->block->handle);
  ui_block_free(C, pup->block);
  mem_delete(pup);
  return false;
}

uiLayout *ui_popup_menu_layout(uiPopupMenu *pup)
{
  return pup->layout;
}

/* Standard Popup Menus */
void ui_popup_menu_reports(Cxt *C, ReportList *reports)
{
  uiPopupMenu *pup = nullptr;
  uiLayout *layout;

  if (!cxt_win(C)) {
    return;
  }

  dune_reports_lock(reports);

  LIST_FOREACH (Report *, report, &reports->list) {
    int icon;
    const char *msg, *msg_next;

    if (report->type < reports->printlevel) {
      continue;
    }

    if (pup == nullptr) {
      char title[UI_MAX_DRAW_STR];
      SNPRINTF(title, "%s: %s", IFACE_("Report"), report->typestr);
      /* popup_menu stuff does just what we need (but pass meaningful block name) */
      pup = ui_popup_menu_begin_ex(C, title, __func__, ICON_NONE);
      layout = ui_popup_menu_layout(pup);
    }
    else {
      uiItemS(layout);
    }

    /* split each newline into a label */
    msg = report->message;
    icon = ui_icon_from_report_type(report->type);
    do {
      char buf[UI_MAX_DRAW_STR];
      msg_next = strchr(msg, '\n');
      if (msg_next) {
        msg_next++;
        lib_strncpy(buf, msg, MIN2(sizeof(buf), msg_next - msg));
        msg = buf;
      }
      uiItemL(layout, msg, icon);
      icon = ICON_NONE;
    } while ((msg = msg_next) && *msg);
  }

  dune_reports_unlock(reports);

  if (pup) {
    ui_popup_menu_end(C, pup);
  }
}

static void ui_popup_menu_create_from_menutype(Cxt *C,
                                               MenuType *mt,
                                               const char *title,
                                               const int icon)
{
  uiPopupBlockHandle *handle = ui_popup_menu_create(
      C, nullptr, nullptr, title, [mt, title, icon](Cxt *C, uiLayout *layout) -> void {
        if (title && title[0]) {
          create_title_button(layout, title, icon);
        }
        ui_item_menutype_fn(C, layout, mt);
      });

  STRNCPY(handle->menu_idname, mt->idname);
  handle->can_refresh = true;

  if (bool(mt->flag & MenuTypeFlag::SearchOnKeyPress)) {
    ed_workspace_status_txt(C, TIP_("Type to search..."));
  }
  else if (mt->idname[0]) {
    ed_workspace_status_txt(C, TIP_("Press spacebar to search..."));
  }
}

int ui_popup_menu_invoke(Cxt *C, const char *idname, ReportList *reports)
{
  MenuType *mt = win_menutype_find(idname, true);

  if (mt == nullptr) {
    dune_reportf(reports, RPT_ERROR, "Menu \"%s\" not found", idname);
    return OP_CANCELLED;
  }

  if (win_menutype_poll(C, mt) == false) {
    /* cancel btn allow event to pass through, just like ops do */
    return (OP_CANCELLED | OP_PASS_THROUGH);
  }
  /* For now always recreate menus on redraw that were invoked with this fn. Maybe we want to
   * make that optional somehow. */
  const bool allow_refresh = true;

  const char *title = CXT_IFACE_(mt->lang_cxt, mt->label);
  if (allow_refresh) {
    ui_popup_menu_create_from_menutype(C, mt, title, ICON_NONE);
  }
  else {
    /* If no refresh is needed, create the block directly. */
    uiPopupMenu *pup = ui_popup_menu_begin(C, title, ICON_NONE);
    uiLayout *layout = ui_popup_menu_layout(pup);
    ui_menutype_draw(C, mt, layout);
    ui_popup_menu_end(C, pup);
  }

  return OP_INTERFACE;
}

/* Popup Block API */
void ui_popup_block_invoke_ex(
    Cxt *C, uiBlockCreateFn fn, void *arg, uiFreeArgFn arg_free, bool can_refresh)
{
  Win *win = cxt_win(C);

  uiPopupBlockHandle *handle = ui_popup_block_create(
      C, nullptr, nullptr, fn, nullptr, arg, arg_free);
  handle->popup = true;

  /* It can be useful to disable refresh (even though it will work)
   * as this exists text fields which can be disruptive if refresh isn't needed. */
  handle->can_refresh = can_refresh;

  ui_popup_handlers_add(C, &win->modalhandlers, handle, 0);
  ui_block_active_only_flagged_btns(
      C, handle->rgn, static_cast<uiBlock *>(handle->rgn->uiblocks.first));
  win_ev_add_mousemove(win);
}

void ui_popup_block_invoke(Cxt *C, uiBlockCreateFn fn, void *arg, uiFreeArgFn arg_free)
{
  ui_popup_block_invoke_ex(C, fn, arg, arg_free, true);
}

void ui_popup_block_ex(Cxt *C,
                       uiBlockCreateFn fn,
                       uiBlockHandleFn popup_fn,
                       uiBlockCancelFn cancel_fn,
                       void *arg,
                       WinOp *op)
{
  Win *win = cxt_win(C);

  uiPopupBlockHandle *handle = ui_popup_block_create(
      C, nullptr, nullptr, fn, nullptr, arg, nullptr);
  handle->popup = true;
  handle->retvalue = 1;
  handle->can_refresh = true;

  handle->popup_op = op;
  handle->popup_arg = arg;
  handle->popup_fn = popup_fn;
  handle->cancel_fn = cancel_fn;
  // handle->opcxt = opcxt;

  ui_popup_handlers_add(C, &win->modalhandlers, handle, 0);
  ui_block_active_only_flagged_btns(
      C, handle->rgn, static_cast<uiBlock *>(handle->rgn->uiblocks.first));
  win_ev_add_mousemove(win);
}

#if 0 /* UNUSED */
void uiPupBlockOp(Cxt *C,
                        uiBlockCreateFn fn,
                        WinOp *op,
                        WinOpCallCxt opcxt)
{
  Win *win = cxt_win(C);
  uiPopupBlockHandle *handle;

  handle = ui_popup_block_create(C, nullptr, nullptr, fn, nullptr, op, nullptr);
  handle->popup = 1;
  handle->retvalue = 1;
  handle->can_refresh = true;

  handle->popup_arg = op;
  handle->popup_fn = op_cb;
  handle->cancel_fn = confirm_cancel_op;
  handle->opcxt = opcxt;

  UI_popup_handlers_add(C, &win->modalhandlers, handle, 0);
  win_ev_add_mousemove(C);
}
#endif

void ui_popup_block_close(Cxt *C, Win *win, uiBlock *block)
{
  /* if loading new .dune while popup is open, win will be nullptr */
  if (block->handle) {
    if (win) {
      const Screen *screen = win_get_active_screen(win);

      ui_popup_handlers_remove(&win->modalhandlers, block->handle);
      ui_popup_block_free(C, block->handle);

      /* In the case we have nested popups,
       * closing one may need to redraw another, see: #48874 */
      LIST_FOREACH (ARgn *, rgn, &screen->rgnbase) {
        ed_rgn_tag_refresh_ui(rgn);
      }
    }
  }
}

bool ui_popup_block_name_exists(const Screen *screen, const char *name)
{
  LIST_FOREACH (const ARgn *, rhn, &screen->rgnbase) {
    LIST_FOREACH (const uiBlock *, block, &rgn->uiblocks) {
      if (STREQ(block->name, name)) {
        return true;
      }
    }
  }
  return false;
}
