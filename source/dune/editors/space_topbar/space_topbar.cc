#include <cstdio>
#include <cstring>

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_dunefile.h"
#include "dune_cxt.h"
#include "dune_global.h"
#include "dune_screen.h"
#include "dune_undo_system.h"

#include "ed_screen.hh"
#include "ed_space_api.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"
#include "ui_view2d.hh"

#include "loader_read_write.hh"

#include "api_access.hh"

#include "wm_api.hh"
#include "wm_message.hh"
#include "wm_types.hh"

/* default cbs for topbar space */
static SpaceLink *topbar_create(const ScrArea * /*area*/, const Scene * /*scene*/)
{
  ARegion *region;
  SpaceTopBar *stopbar;

  stopbar = static_cast<SpaceTopBar *>(mem_callocn(sizeof(*stopbar), "init topbar"));
  stopbar->spacetype = SPACE_TOPBAR;

  /* header */
  region = static_cast<ARegion *>(mem_callocn(sizeof(ARegion), "left aligned header for topbar"));
  lib_addtail(&stopbar->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = RGN_ALIGN_TOP;
  region = static_cast<ARegion *>(MEM_callocN(sizeof(ARegion), "right aligned header for topbar"));
  lib_addtail(&stopbar->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = RGN_ALIGN_RIGHT | RGN_SPLIT_PREV;

  /* main regions */
  region = static_cast<ARegion *>(MEM_callocN(sizeof(ARegion), "main region of topbar"));
  lib_addtail(&stopbar->regionbase, region);
  region->regiontype = RGN_TYPE_WINDOW;

  return (SpaceLink *)stopbar;
}

/* Doesn't free the space-link itself. */
static void topbar_free(SpaceLink * /*sl*/) {}

/* spacetype; init cb */
static void topbar_init(WM * /*wm*/, ScrArea * /*area*/) {}

static SpaceLink *topbar_duplicate(SpaceLink *sl)
{
  SpaceTopBar *stopbarn = static_cast<SpaceTopBar *>(MEM_dupallocN(sl));

  /* clear or remove stuff from old */
  return (SpaceLink *)stopbarn;
}

/* add handlers, stuff you only do once or on area/region changes */
static void topbar_main_region_init(WM *wm, ARegion *region)
{
  wmKeyMap *keymap;

  /* force delayed UI_view2d_region_reinit call */
  if (ELEM(RGN_ALIGN_ENUM_FROM_MASK(region->alignment), RGN_ALIGN_RIGHT)) {
    region->flag |= RGN_FLAG_DYNAMIC_SIZE;
  }
  ui_view2d_region_reinit(&region->v2d, V2D_COMMONVIEW_HEADER, region->winx, region->winy);

  keymap = wm_keymap_ensure(wm->defaultconf, "View2D Buttons List", 0, 0);
  wm_event_add_keymap_handler(&region->handlers, keymap);
}

static void topbar_optypes() {}

static void topbar_keymap(wmKeyConfig * /*keyconf*/) {}

/* add handlers, stuff you only do once or on area/region changes */
static void topbar_header_region_init(WM * /*wm*/, ARegion *region)
{
  if (RGN_ALIGN_ENUM_FROM_MASK(region->alignment) == RGN_ALIGN_RIGHT) {
    region->flag |= RGN_FLAG_DYNAMIC_SIZE;
  }
  ed_region_header_init(region);
}

static void topbar_main_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_WM:
      if (wmn->data == ND_HISTORY) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_SCENE:
      if (wmn->data == ND_MODE) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_VIEW3D) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_PEN:
      if (wmn->data == ND_DATA) {
        ed_region_tag_redraw(region);
      }
      break;
  }
}

static void topbar_header_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_WM:
      if (wmn->data == ND_JOB) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_WORKSPACE:
      ed_region_tag_redraw(region);
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_INFO) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_SCREEN:
      if (wmn->data == ND_LAYER) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_SCENE:
      if (wmn->data == ND_SCENEBROWSE) {
        ed_region_tag_redraw(region);
      }
      break;
  }
}

static void topbar_header_region_message_subscribe(const wmRegionMessageSubscribeParams *params)
{
  wmMsgBus *mbus = params->message_bus;
  WorkSpace *workspace = params->workspace;
  ARegion *region = params->region;

  wmMsgSubscribeValue msg_sub_value_region_tag_redraw{};
  msg_sub_value_region_tag_redraw.owner = region;
  msg_sub_value_region_tag_redraw.user_data = region;
  msg_sub_value_region_tag_redraw.notify = ed_region_do_msg_notify_tag_redraw;

  wm_msg_subscribe_api_prop(
      mbus, &workspace->id, workspace, WorkSpace, tools, &msg_sub_value_region_tag_redraw);
}

static void recent_files_menu_draw(const Cxt * /*C*/, Menu *menu)
{
  uiLayout *layout = menu->layout;
  uiLayoutSetOpCxt(layout, WM_OP_INVOKE_DEFAULT);
  if (!lib_list_is_empty(&G.recent_files)) {
    LIST_FOREACH (RecentFile *, recent, &G.recent_files) {
      const char *file = lib_path_basename(recent->filepath);
      const int icon = dune_file_extension_check(file) ? ICON_FILE_BLEND : ICON_FILE_BACKUP;
      ApiPtr ptr;
      uiItemFullO(layout,
                  "WM_OT_open_mainfile",
                  file,
                  icon,
                  nullptr,
                  WM_OP_INVOKE_DEFAULT,
                  UI_ITEM_NONE,
                  &ptr);
      api_string_set(&ptr, "filepath", recent->filepath);
      api_bool_set(&ptr, "display_file_selector", false);
    }
  }
  else {
    uiItemL(layout, IFACE_("No Recent Files"), ICON_NONE);
  }
}

static void recent_files_menu_register()
{
  MenuType *mt;

  mt = static_cast<MenuType *>(MEM_callocN(sizeof(MenuType), "spacetype info menu recent files"));
  STRNCPY(mt->idname, "TOPBAR_MT_file_open_recent");
  STRNCPY(mt->label, N_("Open Recent"));
  STRNCPY(mt->translation_context, LANG_CXT_DEFAULT_BPYRNA);
  mt->draw = recent_files_menu_draw;
  wm_menutype_add(mt);
}

static void undo_history_draw_menu(const Cxt *C, Menu *menu)
{
  WM *wm = cxt_wm_manager(C);
  if (wm->undo_stack == nullptr) {
    return;
  }

  int undo_step_count = 0;
  int undo_step_count_all = 0;
  LIST_FOREACH_BACKWARD (UndoStep *, us, &wm->undo_stack->steps) {
    undo_step_count_all += 1;
    if (us->skip) {
      continue;
    }
    undo_step_count += 1;
  }

  uiLayout *split = uiLayoutSplit(menu->layout, 0.0f, false);
  uiLayout *column = nullptr;

  const int col_size = 20 + (undo_step_count / 12);

  undo_step_count = 0;

  /* Reverse the order so the most recent state is first in the menu. */
  int i = undo_step_count_all - 1;
  for (UndoStep *us = static_cast<UndoStep *>(wm->undo_stack->steps.last); us; us = us->prev, i--)
  {
    if (us->skip) {
      continue;
    }
    if (!(undo_step_count % col_size)) {
      column = uiLayoutColumn(split, false);
    }
    const bool is_active = (us == wm->undo_stack->step_active);
    uiLayout *row = uiLayoutRow(column, false);
    uiLayoutSetEnabled(row, !is_active);
    uiItemIntO(row,
               CXT_IFACE_(LANG_CXT_OP_DEFAULT, us->name),
               is_active ? ICON_LAYER_ACTIVE : ICON_NONE,
               "ED_OT_undo_history",
               "item",
               i);
    undo_step_count += 1;
  }
}

static void undo_history_menu_register()
{
  MenuType *mt;

  mt = static_cast<MenuType *>(mem_callocn(sizeof(MenuType), __func__));
  STRNCPY(mt->idname, "TOPBAR_MT_undo_history");
  STRNCPY(mt->label, N_("Undo History"));
  STRNCPY(mt->translation_cxt, LANG_CXT_DEFAULT_BPYRNA);
  mt->draw = undo_history_draw_menu;
  wm_menutype_add(mt);
}

static void topbar_space_dune_write(DuneWriter *writer, SpaceLink *sl)
{
  loader_write_struct(writer, SpaceTopBar, sl);
}

void ed_spacetype_topbar()
{
  SpaceType *st = static_cast<SpaceType *>(MEM_callocN(sizeof(SpaceType), "spacetype topbar"));
  ARegionType *art;

  st->spaceid = SPACE_TOPBAR;
  STRNCPY(st->name, "Top Bar");

  st->create = topbar_create;
  st->free = topbar_free;
  st->init = topbar_init;
  st->duplicate = topbar_duplicate;
  st->optypes = topbar_optypes;
  st->keymap = topbar_keymap;
  st->dune_write = topbar_space_dune_write;

  /* regions: main window */
  art = static_cast<ARegionType *>(
      mem_callocn(sizeof(ARegionType), "spacetype topbar main region"));
  art->regionid = RGN_TYPE_WINDOW;
  art->init = topbar_main_region_init;
  art->layout = ed_region_header_layout;
  art->draw = ed_region_header_draw;
  art->listener = topbar_main_region_listener;
  art->prefsizex = UI_UNIT_X * 5; /* Mainly to avoid glitches */
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;

  lib_addhead(&st->regiontypes, art);

  /* regions: header */
  art = static_cast<ARegionType *>(
      mem_callocn(sizeof(ARegionType), "spacetype topbar header region"));
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->prefsizex = UI_UNIT_X * 5; /* Mainly to avoid glitches */
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;
  art->listener = topbar_header_listener;
  art->message_subscribe = topbar_header_region_message_subscribe;
  art->init = topbar_header_region_init;
  art->layout = ed_region_header_layout;
  art->draw = ed_region_header_draw;

  lib_addhead(&st->regiontypes, art);

  recent_files_menu_register();
  undo_history_menu_register();

  dune_spacetype_register(st);
}
