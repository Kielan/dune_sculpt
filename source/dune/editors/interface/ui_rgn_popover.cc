/** \file
 * \ingroup edinterface
 *
 * Pop-Over Region
 *
 * \note This is very close to `interface_region_menu_popup.cc`.
 *
 * We could even merge them, however menu logic is already over-loaded.
 * PopOver's have the following differences.
 *
 * - UI is not constrained to a list.
 * - Pressing a button won't close the pop-over.
 * - Different draw style (to show this is has different behavior from a menu).
 * - #PanelType are used instead of #MenuType.
 * - No menu flipping support.
 * - No moving the menu to fit the mouse cursor.
 * - No key accelerators to access menu items
 *   (if we add support they would work differently).
 * - No arrow key navigation.
 * - No menu memory.
 * - No title.
 */

#include "MEM_guardedalloc.h"

#include "DNA_userdef_types.h"

#include "BLI_listbase.h"

#include "BLI_math_vector.h"
#include "BLI_rect.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"
#include "BKE_report.h"
#include "BKE_screen.hh"

#include "ED_screen.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "UI_interface.hh"

#include "interface_intern.hh"
#include "interface_regions_intern.hh"

/* -------------------------------------------------------------------- */
/** \name Popup Menu with Callback or String
 * \{ */

struct uiPopover {
  uiBlock *block;
  uiLayout *layout;
  uiBut *but;
  ARegion *butregion;

  /* Needed for keymap removal. */
  wmWindow *window;
  wmKeyMap *keymap;
  wmEventHandler_Keymap *keymap_handler;

  uiMenuCreateFunc menu_func;
  void *menu_arg;

  /* Size in pixels (ui scale applied). */
  int ui_size_x;

#ifdef USE_UI_POPOVER_ONCE
  bool is_once;
#endif
};

/**
 * \param region: Optional, the region the block will be placed in. Must be set if the popover is
 *                supposed to support refreshing.
 */
static void ui_popover_create_block(bContext *C,
                                    ARegion *region,
                                    uiPopover *pup,
                                    wmOperatorCallContext opcontext)
{
  BLI_assert(pup->ui_size_x != 0);

  const uiStyle *style = UI_style_get_dpi();

  pup->block = UI_block_begin(C, region, __func__, UI_EMBOSS);

  UI_block_flag_enable(pup->block, UI_BLOCK_KEEP_OPEN | UI_BLOCK_POPOVER);
#ifdef USE_UI_POPOVER_ONCE
  if (pup->is_once) {
    UI_block_flag_enable(pup->block, UI_BLOCK_POPOVER_ONCE);
  }
#endif

  pup->layout = UI_block_layout(
      pup->block, UI_LAYOUT_VERTICAL, UI_LAYOUT_PANEL, 0, 0, pup->ui_size_x, 0, 0, style);

  uiLayoutSetOperatorCxt(pup->layout, opcxt);

  if (pup->btn) {
    if (pup->btn->cxt) {
      uiLayoutCxtCopy(pup->layout, pup->btn->cxt);
    }
  }
}

static uiBlock *ui_block_fn_POPOVER(Cxt *C, uiPopupBlockHandle *handle, void *arg_pup)
{
  uiPopover *pup = static_cast<uiPopover *>(arg_pup);

  /* Create UI block and layout now if it wasn't done between begin/end. */
  if (!pup->layout) {
    ui_popover_create_block(C, handle->rgn, pup, WIN_OP_INVOKE_RGN_WIN);

    if (pup->menu_fn) {
      pup->block->handle = handle;
      pup->menu_fn(C, pup->layout, pup->menu_arg);
      pup->block->handle = nullptr;
    }

    pup->layout = nullptr;
  }

  /* Setup and resolve UI layout for block. */
  uiBlock *block = pup->block;
  int width, height;

  /* in some cases we create the block before the rgn,
   * so we set it delayed here if necessary */
  if (lib_findindex(&handle->region->uiblocks, block) == -1) {
    ui_block_rgn_set(block, handle->rgn);
  }

  ui_block_layout_resolve(block, &width, &height);
  ui_block_direction_set(block, UI_DIR_DOWN | UI_DIR_CENTER_X);

  const int block_margin = U.widget_unit / 2;

  if (pup->but) {
    /* For a header menu we set the direction automatic. */
    block->minbounds = lib_rctf_size_x(&pup->but->rect);
    ui_block_bounds_set_normal(block, block_margin);

    /* If menu slides out of other menu, override direction. */
    const bool slideout = ui_block_is_menu(pup->btn->block);
    if (slideout) {
      ui_block_direction_set(block, UI_DIR_RIGHT);
    }

    /* Store the btn location for positioning the popover arrow hint. */
    if (!handle->refresh) {
      float center[2] = {lib_rctf_cent_x(&pup->btn->rect), lib_rctf_cent_y(&pup->btn->rect)};
      ui_block_to_win_fl(handle->cxt_rgn, pup->btn->block, &center[0], &center[1]);
      /* These variables aren't used for popovers,
       * we could add new variables if there is a conflict. */
      block->bounds_offset[0] = int(center[0]);
      block->bounds_offset[1] = int(center[1]);
      copy_v2_v2_int(handle->prev_bounds_offset, block->bounds_offset);
    }
    else {
      copy_v2_v2_int(block->bounds_offset, handle->prev_bounds_offset);
    }

    if (!slideout) {
      ARgn *rgn = cxt_win_rgn(C);

      if (rgn && rgn->pnls.first) {
        /* For rgns with pnls, prefer to open to top so we can
         * see the values of the buttons below changing. */
        ui_block_direction_set(block, UI_DIR_UP | UI_DIR_CENTER_X);
      }
      /* Prefer popover from header to be positioned into the editor. */
      else if (rgn) {
        if (RGN_TYPE_IS_HEADER_ANY(rgn->rgntype)) {
          if (RGN_ALIGN_ENUM_FROM_MASK(rgn->alignment) == RGN_ALIGN_BOTTOM) {
            ii_block_direction_set(block, UI_DIR_UP | UI_DIR_CENTER_X);
          }
        }
      }
    }

    /* Estimated a maximum size so we don't go off-screen for low height
     * areas near the bottom of the win on refreshes. */
    handle->max_size_y = UNIT_Y * 16.0f;
  }
  else {
    /* Not attached to a button. */
    int bounds_offset[2] = {0, 0};
    ui_block_flag_enable(block, UI_BLOCK_LOOP);
    ui_block_theme_style_set(block, UI_BLOCK_THEME_STYLE_POPUP);
    ui_block_direction_set(block, block->direction);
    block->minbounds = UI_MENU_WIDTH_MIN;

    if (!handle->refresh) {
      Btn *btn = nullptr;
      Btn *btn_first = nullptr;
      LIST_FOREACH (Btn *, btn_iter, &block->btns) {
        if ((btn_first == nullptr) && btn_is_editable(btn_iter)) {
          btn_first = btn_iter;
        }
        if (btn_iter->flag & (UI_SEL | UI_SEL_DRAW)) {
          btn = btn_iter;
          break;
        }
      }

      if (but) {
        bounds_offset[0] = -(btn->rect.xmin + 0.8f * lib_rctf_size_x(&but->rect));
        bounds_offset[1] = -lib_rctf_cent_y(&btn->rect);
      }
      else {
        bounds_offset[0] = -(pup->ui_size_x / 2);
        bounds_offset[1] = btn_first ? -lib_rctf_cent_y(&btn_first->rect) : (UI_UNIT_Y / 2);
      }
      copy_v2_v2_int(handle->prev_bounds_offset, bounds_offset);
    }
    else {
      copy_v2_v2_int(bounds_offset, handle->prev_bounds_offset);
    }

    ui_block_bounds_set_popup(block, block_margin, bounds_offset);
  }

  return block;
}

static void ui_block_free_fn_POPOVER(void *arg_pup)
{
  uiPopover *pup = static_cast<uiPopover *>(arg_pup);
  if (pup->keymap != nullptr) {
    Win *win = pup->win;
    win_ev_remove_keymap_handler(&win->modalhandlers, pup->keymap);
  }
  mem_freen(pup);
}

uiPopupBlockHandle *ui_popover_pnl_create(
    Cxt *C, Rgn *btnrgn, Btn *btn, uiMenuCreateFn menu_fn, void *arg)
{
  Win *win = cxt_win(C);
  const uiStyle *style = ui_style_get_dpi();
  const PnlType *pnl_type = (PnlType *)arg;

  /* Create popover, buttons are created from cb */
  uiPopover *pup = mem_cnew<uiPopover>(__func__);
  pup->btn = btn;

  /* FIXME: maybe one day we want non pnl popovers? */
  {
    const int ui_units_x = (pnl_type->ui_units_x == 0) ? UI_POPOVER_WIDTH_UNITS :
                                                           pnl_type->ui_units_x;
    /* Scale width by changes to Text Style point size. */
    const int text_points_max = std::max(style->widget.points, style->widgetlabel.points);
    pup->ui_size_x = ui_units_x * U.widget_unit *
                     (text_points_max / float(UI_DEFAULT_TXT_POINTS));
  }

  pup->menu_fn = menu_fn;
  pup->menu_arg = arg;

#ifdef USE_UI_POPOVER_ONCE
  {
    /* Ideally this would be passed in */
    const WinEv *ev = win->evstate;
    pup->is_once = (ev->type == LEFTMOUSE) && (ev->val == KM_PRESS);
  }
#endif

  /* Create popup block. */
  uiPopupBlockHandle *handle;
  handle = ui_popup_block_create(
      C, btnrgn, btn, nullptr, ui_block_fn_POPOVER, pup, ui_block_free_fn_POPOVER);
  handle->can_refresh = true;

  /* Add handlers. If attached to a btn, the btn will already
   * add a modal handler and pass on events. */
  if (!btn) {
    ui_popup_handlers_add(C, &win->modalhandlers, handle, 0);
    win_ev_add_mousemove(win);
    handle->popup = true;
  }

  return handle;
}

/* Standard Popover Pnls */
int ui_popover_pnl_invoke(Cxt *C, const char *idname, bool keep_open, ReportList *reports)
{
  uiLayout *layout;
  PnlType *pt = win_pnltype_find(idname, true);
  if (pt == nullptr) {
    dune_reportf(reports, RPT_ERROR, "Pnl \"%s\" not found", idname);
    return OP_CANCELLED;
  }

  if (pt->poll && (pt->poll(C, pt) == false)) {
    /* cancel btn allow ev to pass through, just like ops do */
    return (OP_CANCELLED | OP_PASS_THROUGH);
  }

  uiBlock *block = nullptr;
  if (keep_open) {
    uiPopupBlockHandle *handle = ui_popover_pnl_create(
        C, nullptr, nullptr, ui_item_pnltype_fn, pt);
    uiPopover *pup = static_cast<uiPopover *>(handle->popup_create_vars.arg);
    block = pup->block;
  }
  else {
    uiPopover *pup = ui_popover_begin(C, U.widget_unit * pt->ui_units_x, false);
    layout = ui_popover_layout(pup);
    ui_pnltype_draw(C, pt, layout);
    ui_popover_end(C, pup, nullptr);
    block = pup->block;
  }

  if (block) {
    uiPopupBlockHandle *handle = static_cast<uiPopupBlockHandle *>(block->handle);
    ui_block_active_only_flagged_btns(C, handle->rgn, block);
  }
  return OP_INTERFACE;
}


/* Popup Menu API with begin & end */
uiPopover *ui_popover_begin(Cxt *C, int ui_menu_width, bool from_active_btn)
{
  uiPopover *pup = mem_cnew<uiPopover>(__func__);
  if (ui_menu_width == 0) {
    ui_menu_width = U.widget_unit * UI_POPOVER_WIDTH_UNITS;
  }
  pup->ui_size_x = ui_menu_width;

  ARgn *btnrgn = nullptr;
  Btn *btn = nullptr;

  if (from_active_btn) {
    btnrgn = cxt_win_rn(C);
    btn = ui_rgn_active_btn_get(btnrgn);
    if (btn == nullptr) {
      btnrgn = nullptr;
    }
  }

  pup->btn = btn;
  pup->btnrgn = btnrgn;

  /* Op cxt default same as menus, change if needed. */
  ui_popover_create_block(C, nullptr, pup, WIN_OP_EX_RGN_WIN);

  /* create in advance so we can let btns point to retval already */
  pup->block->handle = mem_cnew<uiPopupBlockHandle>(__func__);

  return pup;
}

static void popover_keymap_fn(WinKeyMap * /*keymap*/, WinKeyMapItem * /*kmi*/, void *user_data)
{
  uiPopover *pup = static_cast<uiPopover *>(user_data);
  pup->block->handle->menuretval = UI_RETURN_OK;
}

void ui_popover_end(Cxt *C, uiPopover *pup, WinKeyMap *keymap)
{
  Win *win = cxt_win(C);
  /* Create popup block. No refresh support since the buttons were created
   * between begin/end and we have no callback to recreate them. */
  uiPopupBlockHandle *handle;

  if (keymap) {
    /* Add so we get keymaps shown in the btns. */
    ui_block_flag_enable(pup->block, UI_BLOCK_SHOW_SHORTCUT_ALWAYS);
    pup->keymap = keymap;
    pup->keymap_handler = win_ev_add_keymap_handler_priority(&win->modalhandlers, keymap, 0);
    win_ev_set_keymap_handler_post_cb(pup->keymap_handler, popover_keymap_fn, pup);
  }

  handle = ui_popup_block_create(C,
                                 pup->btnrgn,
                                 pup->btn,
                                 nullptr,
                                 ui_block_fn_POPOVER,
                                 pup,
                                 ui_block_free_fn_POPOVER);

  /* Add handlers. */
  ui_popup_handlers_add(C, &win->modalhandlers, handle, 0);
  win_ev_add_mousemove(win);
  handle->popup = true;

  /* Re-add so it gets priority. */
  if (keymap) {
    lib_remlink(&win->modalhandlers, pup->keymap_handler);
    lib_addhead(&win->modalhandlers, pup->keymap_handler);
  }

  pup->win = win;

  /* TODO: we may want to make this configurable.
   * The begin/end stype of calling popups doesn't allow 'can_refresh' to be set.
   * For now close this style of popovers when accessed. */
  ui_block_flag_disable(pup->block, UI_BLOCK_KEEP_OPEN);
}

uiLayout *UI_popover_layout(uiPopover *pup)
{
  return pup->layout;
}

#ifdef USE_UI_POPOVER_ONCE
void UI_popover_once_clear(uiPopover *pup)
{
  pup->is_once = false;
}
#endif
