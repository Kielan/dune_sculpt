/* edui
 * Pie Menu Rgn */
#include <cstdarg>
#include <cstdlib>
#include <cstring>

#include "mem_guardedalloc.h"

#include "types_userdef.h"

#include "lib_blenlib.h"
#include "lib_utildefines.h"

#include "PIL_time.h"

#include "dune_cxt.h"
#include "dune_screen.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "api_access.hh"
#include "api_path.hh"
#include "api_prototypes.h"

#include "ui.hh"

#include "lang.h"

#include "ed_screen.hh"

#include "ui_intern.hh"
#include "ui_rgns_intern.hh"

/* Pie Menu */
struct uiPieMenu {
  uiBlock *block_radial; /* radial block of the pie menu (more could be added later) */
  uiLayout *layout;
  int mx, my;
};

static uiBlock *ui_block_fn_PIE(Cxt * /*C*/, uiPopupBlockHandle *handle, void *arg_pie)
{
  uiBlock *block;
  uiPieMenu *pie = static_cast<uiPieMenu *>(arg_pie);
  int minwidth, width, height;

  minwidth = UI_MENU_WIDTH_MIN;
  block = pie->block_radial;

  /* in some cases we create the block before the rgn,
   * so we set it delayed here if necessary */
  if (lib_findindex(&handle->rgn->uiblocks, block) == -1) {
    ui_block_rgn_set(block, handle->rgn);
  }

  ui_block_layout_resolve(block, &width, &height);

  ui_block_flag_enable(block, UI_BLOCK_LOOP | UI_BLOCK_NUMSELECT);
  ui_block_theme_style_set(block, UI_BLOCK_THEME_STYLE_POPUP);

  block->minbounds = minwidth;
  block->bounds = 1;
  block->bounds_offset[0] = 0;
  block->bounds_offset[1] = 0;
  block->bounds_type = UI_BLOCK_BOUNDS_PIE_CENTER;

  block->pie_data.pie_center_spawned[0] = pie->mx;
  block->pie_data.pie_center_spawned[1] = pie->my;

  return pie->block_radial;
}

static float ui_pie_menu_title_width(const char *name, int icon)
{
  const uiFontStyle *fstyle = UI_FSTYLE_WIDGET;
  return (ui_fontstyle_string_width(fstyle, name) + (UNIT_X * (1.50f + (icon ? 0.25f : 0.0f))));
}

uiPieMenu *ui_pie_menu_begin(Cxt *C, const char *title, int icon, const WinEv *ev)
{
  const uiStyle *style = ui_style_get_dpi();
  short ev_type;

  Win *win = cxt_win(C);

  uiPieMenu *pie = mem_cnew<uiPieMenu>(__func__);

  pie->block_radial = ui_block_begin(C, nullptr, __func__, UI_EMBOSS);
  /* may be useful later to allow spawning pies
   * from old positions */
  // pie->block_radial->flag |= UI_BLOCK_POPUP_MEMORY;
  pie->block_radial->puphash = ui_popup_menu_hash(title);
  pie->block_radial->flag |= UI_BLOCK_RADIAL;

  /* if pie is spawned by a left click, release or click ev,
   * it is always assumed to be click style */
  if (ev->type == LEFTMOUSE || ELEM(ev->val, KM_RELEASE, KM_CLICK)) {
    pie->block_radial->pie_data.flags |= UI_PIE_CLICK_STYLE;
    pie->block_radial->pie_data.ev_type = EV_NONE;
    win->pie_ev_type_lock = EV_NONE;
  }
  else {
    if (win->pie_ev_type_last != EV_NONE) {
      /* original pie key has been released, so don't propagate the ev */
      if (win->pie_ev_type_lock == EV_NONE) {
        ev_type = EV_NONE;
        pie->block_radial->pie_data.flags |= UI_PIE_CLICK_STYLE;
      }
      else {
        ev_type = win->pie_ev_type_last;
      }
    }
    else {
      ev_type = ev->type;
    }

    pie->block_radial->pie_data.ev_type = ev_type;
    win->pie_ev_type_lock = ev_type;
  }

  pie->layout = ui_block_layout(
      pie->block_radial, UI_LAYOUT_VERTICAL, UI_LAYOUT_PIEMENU, 0, 0, 200, 0, 0, style);

  /* WinEv.xy is where we started dragging in case of KM_CLICK_DRAG. */
  pie->mx = ev->xy[0];
  pie->my = ev->xy[1];

  /* create title btn */
  if (title[0]) {
    Btn *btn;
    char titlestr[256];
    int w;
    if (icon) {
      SNPRINTF(titlestr, " %s", title);
      w = ui_pie_menu_title_width(titlestr, icon);
      btn = BtnDefIconTxt(pie->block_radial,
                          BTYPE_LABEL,
                          0,
                          icon,
                          titlestr,
                          0,
                          0,
                          w,
                          UNIT_Y,
                          nullptr,
                          0.0,
                          0.0,
                          0,
                          0,
                          "");
    }
    else {
      w = ui_pie_menu_title_width(title, 0);
      btn = Btn(pie->block_radial,
                BTYPE_LABEL,
                0,
                title,
                0,
                0,
                w,
                UNIT_Y,
                nullptr,
                0.0,
                0.0,
                0,
                0,
                "");
    }
    /* do not align left */
    btn->drawflag &= ~BTN_TXT_LEFT;
    pie->block_radial->pie_data.title = btn->str;
    pie->block_radial->pie_data.icon = icon;
  }

  return pie;
}

void ui_pie_menu_end(Cxt *C, uiPieMenu *pie)
{
  Win *win = cxt_win(C);
  uiPopupBlockHandle *menu;

  menu = ui_popup_block_create(C, nullptr, nullptr, nullptr, ui_block_fn_PIE, pie, nullptr);
  menu->popup = true;
  menu->towardstime = PIL_check_seconds_timer();

  ui_popup_handlers_add(C, &win->modalhandlers, menu, WIN_HANDLER_ACCEPT_DBL_CLICK);
  win_ev_add_mousemove(win);

  mem_freen(pie);
}

uiLayout *ui_pie_menu_layout(uiPieMenu *pie)
{
  return pie->layout;
}

int ui_pie_menu_invoke(Cxt *C, const char *idname, const WinEv *ev)
{
  uiPieMenu *pie;
  uiLayout *layout;
  MenuType *mt = win_menutype_find(idname, true);

  if (mt == nullptr) {
    printf("%s: named menu \"%s\" not found\n", __func__, idname);
    return OP_CANCELLED;
  }

  if (win_menutype_poll(C, mt) == false) {
    /* cancel but allow ev to pass through, just like ops do */
    return (OP_CANCELLED | OP_PASS_THROUGH);
  }

  pie = ui_pie_menu_begin(C, CXT_IFACE_(mt->translation_cxt, mt->label), ICON_NONE, ev)
  layout = ui_pie_menu_layout(pie);

  ui_menutype_draw(C, mt, layout);

  ui_pie_menu_end(C, pie);

  return OP_INTERFACE;
}

int ui_pie_menu_invoke_from_op_enum(
    Cxt *C, const char *title, const char *opname, const char *propname, const WinEv *ev)
{
  uiPieMenu *pie;
  uiLayout *layout;

  pie = ui_pie_menu_begin(C, IFACE_(title), ICON_NONE, ev);
  layout = ui_pie_menu_layout(pie);

  layout = uiLayoutRadial(layout);
  uiItemsEnumO(layout, opname, propname);

  ui_pie_menu_end(C, pie);

  return OP_INTERFACE;
}

int ui_pie_menu_invoke_from_api_enum(Cxt *C,
                                     const char *title,
                                     const char *path,
                                     const WinEv *ev)
{
  ApiPtr r_ptr;
  ApiProp *r_prop;
  uiPieMenu *pie;
  uiLayout *layout;

  ApiPtr cxt_ptr = api_ptr_create(nullptr, &ApiCxt, C);

  if (!api_path_resolve(&cxt_ptr, path, &r_ptr, &r_prop)) {
    return OP_CANCELLED;
  }

  /* invalid prop, only accept enums */
  if (api_prop_type(r_prop) != PROP_ENUM) {
    lib_assert(0);
    return OP_CANCELLED;
  }

  pie = ui_pie_menu_begin(C, IFACE_(title), ICON_NONE, ev);

  layout = ui_pie_menu_layout(pie);

  layout = uiLayoutRadial(layout);
  uiItemFullR(layout, &r_ptr, r_prop, API_NO_INDEX, 0, UI_ITEM_R_EXPAND, nullptr, ICON_NONE);

  ui_pie_menu_end(C, pie);

  return OP_INTERFACE;
}

/* Pie Menu Level
 * Pie menus can't contain more than 8 items (yet).
 * When using uiItemsFullEnumO, a "More" btn is created that calls
 * a new pie menu if the enum has too many items. We call this a new "level".
 * Indirect recursion is used, so that a theoretically unlimited number of items is supported.
 *
 * This is a implementation specifically for op enums,
 * needed since the object mode pie now has more than 8 items.
 * Ideally we'd have some way of handling this for all kinds of pie items, but that's tricky.
 * - Julian */

struct PieMenuLevelData {
  char title[UI_MAX_NAME_STR]; /* parent pie title, copied for level */
  int icon;                    /* parent pie icon, copied for level */
  int totitem;                 /* total count of *remaining* items */

  /* needed for calling uiItemsFullEnumO_array again for new level */
  WinOpType *ot;
  const char *propname;
  IdProp *props;
  WinOpCallCxt cxt;
  eUI_Item_Flag flag;
};

/* Invokes a new pie menu for a new level. */
static void ui_pie_menu_level_invoke(Cxt *C, void *argN, void *arg2)
{
  EnumPropItem *item_array = (EnumPropItem *)argN;
  PieMenuLevelData *lvl = (PieMenuLevelData *)arg2;
  Win *win = cxt_win(C);

  uiPieMenu *pie = ui_pie_menu_begin(C, IFACE_(lvl->title), lvl->icon, win->evstate);
  uiLayout *layout = ui_pie_menu_layout(pie);

  layout = uiLayoutRadial(layout);

  ApiPtr ptr;

  win_op_props_create_ptr(&ptr, lvl->ot);
  /* So the cxt is passed to `itemf` fns (some need it). */
  win_op_props_sanitize(&ptr, false);
  ApiProp *prop = api_struct_find_prop(&ptr, lvl->propname);

  if (prop) {
    uiItemsFullEnumO_items(layout,
                           lvl->ot,
                           ptr,
                           prop,
                           lvl->props,
                           lvl->cxt,
                           lvl->flag,
                           item_array,
                           lvl->totitem);
  }
  else {
    api_warning("%s.%s not found", api_struct_id(ptr.type), lvl->propname);
  }

  ui_pie_menu_end(C, pie);
}

void ui_pie_menu_level_create(uiBlock *block,
                              WinOpType *ot,
                              const char *propname,
                              IdProp *props,
                              const EnumPropItem *items,
                              int totitem,
                              const WinOpCallCxt cxt,
                              const eUI_Item_Flag flag)
{
  const int totitem_parent = PIE_MAX_ITEMS - 1;
  const int totitem_remain = totitem - totitem_parent;
  const size_t array_size = sizeof(EnumPropItem) * totitem_remain;

  /* used as btn->fn_arg so freeing is handled elsewhere */
  EnumPropItem *remaining = static_cast<EnumPropItem *>(
      mem_mallocn(array_size + sizeof(EnumPropItem), "pie_level_item_array"));
  memcpy(remaining, items + totitem_parent, array_size);
  /* A null terminating sentinel element is required. */
  memset(&remaining[totitem_remain], 0, sizeof(EnumPropItem));

  /* yuk, static... issue is we can't reliably free this without doing dangerous changes */
  static PieMenuLevelData lvl;
  STRNCPY(lvl.title, block->pie_data.title);
  lvl.totitem = totitem_remain;
  lvl.ot = ot;
  lvl.propname = propname;
  lvl.props = props;
  lvl.cxt = cxt;
  lvl.flag = flag;

  /* add a 'more' menu entry */
  Btn *btn = BtnDefIconTxt(block,
                           BTYPE_BTN,
                           0,
                           ICON_PLUS,
                           "More",
                           0,
                           0,
                           UNIT_X * 3,
                           UNIT_Y,
                           nullptr,
                           0.0f,
                           0.0f,
                           0.0f,
                           0.0f,
                           "Show more items of this menu");
  btn_fn_set(btn, ui_pie_menu_level_invoke, remaining, &lvl);
}
