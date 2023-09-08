#include "types_window.h"

#include "mem_guardedalloc.h"

#include "lib_dune.h"

#include "dune_cxt.h"
#include "dune_screen.h"

#include "ed_screen.hh"

#include "wm_types.hh"

#include "ui_interface.hh"

#include "text_intern.hh"

/* header area region */
/* props */
static ARegion *text_has_props_region(ScrArea *area)
{
  ARegion *region, *arnew;

  region = dune_area_find_region_type(area, RGN_TYPE_UI);
  if (region) {
    return region;
  }

  /* add subdiv level; after header */
  region = dune_area_find_region_type(area, RGN_TYPE_HEADER);

  /* is error! */
  if (region == nullptr) {
    return nullptr;
  }

  arnew = static_cast<ARegion *>(mem_callocn(sizeof(ARegion), "properties region"));

  lib_insertlinkafter(&area->regionbase, region, arnew);
  arnew->regiontype = RGN_TYPE_UI;
  arnew->alignment = RGN_ALIGN_LEFT;

  arnew->flag = RGN_FLAG_HIDDEN;

  return arnew;
}

static bool text_props_poll(Cxt *C)
{
  return (cxt_wm_space_text(C) != nullptr);
}

static int text_text_search_ex(Cxt *C, wmOp * /*op*/)
{
  ScrArea *area = cxt_wm_area(C);
  ARegion *region = text_has_props_region(area);
  SpaceText *st = cxt_wm_space_text(C);

  if (region) {
    if (region->flag & RGN_FLAG_HIDDEN) {
      ed_region_toggle_hidden(C, region);
    }

    ui_panel_category_active_set(region, "Text");

    /* cannot send a button activate yet for case when region wasn't visible yet */
    /* flag gets checked and cleared in main draw callback */
    st->flags |= ST_FIND_ACTIVATE;

    ed_region_tag_redraw(region);
  }
  return OPERATOR_FINISHED;
}

void TEXT_OT_start_find(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Find";
  ot->description = "Start searching text";
  ot->idname = "TEXT_OT_start_find";

  /* api callbacks */
  ot->exec = text_text_search_exec;
  ot->poll = text_properties_poll;
}
