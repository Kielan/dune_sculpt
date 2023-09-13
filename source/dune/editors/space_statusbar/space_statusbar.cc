#include <cstdio>
#include <cstring>

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"

#include "dune_cxt.h"
#include "dune_screen.h"

#include "ed_screen.hh"
#include "ed_space_api.hh"

#include "api_access.hh"

#include "ui_interface.hh"

#include "loader_read_write.hh"

#include "wm_api.hh"
#include "wm_message.hh"
#include "wm_types.hh"

/* default cbs for statusbar space **/
static SpaceLink *statusbar_create(const ScrArea * /*area*/, const Scene * /*scene*/)
{
  ARegion *region;
  SpaceStatusBar *sstatusbar;

  sstatusbar = static_cast<SpaceStatusBar *>(MEM_callocN(sizeof(*sstatusbar), "init statusbar"));
  sstatusbar->spacetype = SPACE_STATUSBAR;

  /* header region */
  region = static_cast<ARegion *>(MEM_callocN(sizeof(*region), "header for statusbar"));
  lib_addtail(&sstatusbar->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = RGN_ALIGN_NONE;

  return (SpaceLink *)sstatusbar;
}

/* Doesn't free the space-link itself. */
static void statusbar_free(SpaceLink * /*sl*/) {}

/* spacetype; init callback */
static void statusbar_init(WM * /*wm*/, ScrArea * /*area*/) {}

static SpaceLink *statusbar_duplicate(SpaceLink *sl)
{
  SpaceStatusBar *sstatusbarn = static_cast<SpaceStatusBar *>(mem_dupallocn(sl));

  /* clear or remove stuff from old */

  return (SpaceLink *)sstatusbarn;
}

/* add handlers, stuff you only do once or on area/region changes */
static void statusbar_header_region_init(WM * /*wm*/, ARegion *region)
{
  if (ELEM(RGN_ALIGN_ENUM_FROM_MASK(region->alignment), RGN_ALIGN_RIGHT)) {
    region->flag |= RGN_FLAG_DYNAMIC_SIZE;
  }
  ed_region_header_init(region);
}

static void statusbar_optypes() {}

static void statusbar_keymap(wmKeyConfig * /*keyconf*/) {}

static void statusbar_header_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_SCREEN:
      if (ELEM(wmn->data, ND_LAYER, ND_ANIMPLAY)) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_WM:
      if (wmn->data == ND_JOB) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_SCENE:
      if (wmn->data == ND_RENDER_RESULT) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_INFO) {
        ed_region_tag_redraw(region);
      }
      break;
    case NC_ID:
      if (wmn->action == NA_RENAME) {
        ed_region_tag_redraw(region);
      }
      break;
  }
}

static void statusbar_header_region_message_subscribe(const wmRegionMsgSubParams *params)
{
  wmMsgBus *mbus = params->message_bus;
  ARegion *region = params->region;

  wmMsgSubValue msg_sub_value_region_tag_redraw{};
  msg_sub_value_region_tag_redraw.owner = region;
  msg_sub_value_region_tag_redraw.user_data = region;
  msg_sub_value_region_tag_redraw.notify = ed_region_do_msg_notify_tag_redraw;

  wm_msg_subscribe_api_anon_prop(mbus, Window, view_layer, &msg_sub_value_region_tag_redraw);
  wm_msg_subscribe_api_anon_prop(mbus, ViewLayer, name, &msg_sub_value_region_tag_redraw);
}

static void statusbar_space_blend_write(DuneWriter *writer, SpaceLink *sl)
{
  loader_write_struct(writer, SpaceStatusBar, sl);
}

void ed_spacetype_statusbar()
{
  SpaceType *st = static_cast<SpaceType *>(mem_callocn(sizeof(*st), "spacetype statusbar"));
  ARegionType *art;

  st->spaceid = SPACE_STATUSBAR;
  STRNCPY(st->name, "Status Bar");

  st->create = statusbar_create;
  st->free = statusbar_free;
  st->init = statusbar_init;
  st->duplicate = statusbar_duplicate;
  st->optypes = statusbar_optypes;
  st->keymap = statusbar_keymap;
  st->dune_write = statusbar_space_dune_write;

  /* regions: header window */
  art = static_cast<ARegionType *>(mem_callocn(sizeof(*art), "spacetype statusbar header region"));
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = 0.8f * HEADERY;
  art->prefsizex = UI_UNIT_X * 5; /* Mainly to avoid glitches */
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;
  art->init = statusbar_header_region_init;
  art->layout = ed_region_header_layout;
  art->draw = ed_region_header_draw;
  art->listener = statusbar_header_region_listener;
  art->message_subscribe = statusbar_header_region_message_subscribe;
  lib_addhead(&st->regiontypes, art);

  dune_spacetype_register(st);
}
