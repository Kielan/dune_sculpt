#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "types_anim.h"
#include "types_screen.h"
#include "types_space.h"

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"

#include "dune_cxt.hh"
#include "dune_fcurve.h"
#include "dune_screen.hh"

#include "ed_anim_api.hh"
#include "ed_screen.hh"
#include "ui.hh"

#include "api_access.hh"
#include "api_prototypes.h"

#include "graph_intern.h" /* own include */

/* Set Up Drivers Editor */
void ed_drivers_editor_init(Cxt *C, ScrArea *area)
{
  SpaceGraph *sipo = (SpaceGraph *)area->spacedata.first;

  /* Set mode */
  sipo->mode = SIPO_MODE_DRIVERS;

  /* Show Props Rgn (or else the settings can't be edited) */
  ARgn *rgn_props = dune_area_find_rgn_type(area, RGN_TYPE_UI);
  if (rgn_props) {
    ui_pnl_category_active_set(rgn_props, "Drivers");

    rgn_props->flag &= ~RGN_FLAG_HIDDEN;
    /* Adjust width of this too? */

    ed_rgn_visibility_change_update(C, area, rgn_props);
  }
  else {
    printf("%s: Couldn't find props rgn for Drivers Editor - %p\n", __func__, area);
  }

  /* Adjust framing in graph rgn */
  /* TODO: Have a way of not resetting this every time?
   * (e.g. So that switching back and forth between editors doesn't keep jumping?) */
  ARgn *rgn_main = dune_area_find_rgn_type(area, RGN_TYPE_WIN);
  if (rgn_main) {
    /* Ideally we recenter based on the range instead... */
    rgn_main->v2d.tot.xmin = -2.0f;
    rgn_main->v2d.tot.ymin = -2.0f;
    rgn_main->v2d.tot.xmax = 2.0f;
    rgn_main->v2d.tot.ymax = 2.0f;

    rgn_main->v2d.cur = rgn_main->v2d.tot;
  }
}

/* Active F-Curve */
AnimListElem *get_active_fcurve_channel(AnimCxt *ac)
{
  List anim_data = {nullptr, nullptr};
  int filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_FOREDIT | ANIMFILTER_ACTIVE |
                ANIMFILTER_FCURVESONLY);
  size_t items = anim_animdata_filter(
      ac, &anim_data, eAnimFilterFlags(filter), ac->data, eAnimContTypes(ac->datatype));

  /* We take the first F-Curve only, since some other ones may have had 'active' flag set
   * if they were from linked data. */
  if (items) {
    AnimListElem *ale = (AnimListElem *)anim_data.first;

    /* remove first item from list, then free the rest of the list and return the stored one */
    lib_remlink(&anim_data, ale);
    anim_animdata_freelist(&anim_data);

    return ale;
  }

  /* no active F-Curve */
  return nullptr;
}

/* Op Polling Cbs */
bool graphop_visible_keyframes_poll(Cxt *C)
{
  AnimCxt ac;
  List anim_data = {nullptr, nullptr};
  ScrArea *area = cxt_win_area(C);
  size_t items;
  int filter;
  bool found = false;

  /* firstly, check if in Graph Editor */
  /* TODO: also check for rgn? */
  if ((area == nullptr) || (area->spacetype != SPACE_GRAPH)) {
    return found;
  }

  /* try to init Anim-Cxt stuff ourselves and check */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return found;
  }

  /* loop over the visible (sel doesn't matter) F-Curves, and see if they're suitable
   * stopping on the first successful match */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FCURVESONLY);
  items = anim_animdata_filter(
      &ac, &anim_data, eAnimFilterFlags(filter), ac.data, eAnimContTypes(ac.datatype));
  if (items == 0) {
    return found;
  }

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    FCurve *fcu = (FCurve *)ale->data;

    /* visible curves for sel must fulfill the following criteria:
     *-It has bezier keyframes
     *-F-Curve mods do not interfere with the result too much
     * (i.e. the mod-ctrl drwing check returns false) */
    if (fcu->bezt == nullptr) {
      continue;
    }
    if (dune_fcurve_are_keyframes_usable(fcu)) {
      found = true;
      break;
    }
  }

  /* cleanup and return findings */
  amim_animdata_freelist(&anim_data);
  return found;
}

bool graphop_editable_keyframes_poll(Cxt *C)
{
  AnimCxt ac;
  List anim_data = {nullptr, nullptr};
  ScrArea *area = cxt_win_area(C);
  size_t items;
  int filter;
  bool found = false;

  /* first check if in Graph Editor or Dopesheet */
  /* TODO: also check for rgn? */
  if (area == nullptr || !ELEM(area->spacetype, SPACE_GRAPH, SPACE_ACTION)) {
    return found;
  }

  /* try to init Anim-Cxt stuff ourselves and check */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return found;
  }

  /* loop over the editable F-Curves, and see if they're suitable
   * stopping on the first successful match */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_FOREDIT | ANIMFILTER_CURVE_VISIBLE |
            ANIMFILTER_FCURVESONLY);
  items = anim_animdata_filter(
      &ac, &anim_data, eAnimFilterFlags(filter), ac.data, eAnimContTypes(ac.datatype));
  if (items == 0) {
    cxt_win_op_poll_msg_set(C, "There is no anim data to operate on");
    return found;
  }

  LIST_FOREACH (AnimListElem *, ale, &anim_data) {
    FCurve *fcu = (FCurve *)ale->data;

    /* editable curves must fulfill the following criteria:
     * - it has bezier keyframes
     * - it must not be protected from editing (this is alrdy checked for with the edit flag
     * - F-Curve mods do not interfere with the result too much
     *   (i.e. the mod-ctrl drwing check returns false) */
    if (fcu->bezt == nullptr && fcu->fpt != nullptr) {
      /* This is a baked curve, it is never editable. */
      continue;
    }
    if (dune_fcurve_is_keyframable(fcu)) {
      found = true;
      break;
    }
  }

  /* cleanup and return findings */
  anim_animdata_freelist(&anim_data);
  return found;
}

bool graphop_active_fcurve_poll(Cxt *C)
{
  AnimCxt ac;
  AnimListElem *ale;
  ScrArea *area = cxt_win_area(C);
  bool has_fcurve = false;

  /* firstly, check if in Graph Editor */
  /* TODO: also check for rgn? */
  if ((area == nullptr) || (area->spacetype != SPACE_GRAPH)) {
    return has_fcurve;
  }

  /* try init Anim-Cxt stuff ourselves and check */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return has_fcurve;
  }

  /* try to get the Active F-Curve */
  ale = get_active_fcurve_channel(&ac);
  if (ale == nullptr) {
    return has_fcurve;
  }

  /* Do we have a suitable F-Curves?
   * - For most cases, NLA Ctrl Curves are sufficiently similar to NLA
   *   curves to serve this role too. Under the hood, they are F-Curves too.
   *   The only problems which will arise here are if these need to be
   *   in an Action too (but drivers would then also be affected! */
  has_fcurve = ((ale->data) && ELEM(ale->type, ANIMTYPE_FCURVE, ANIMTYPE_NLACURVE));
  if (has_fcurve) {
    FCurve *fcu = (FCurve *)ale->data;
    has_fcurve = (fcu->flag & FCURVE_VISIBLE) != 0;
  }

  /* free tmp data... */
  mem_free(ale);

  /* return success */
  return has_fcurve;
}

bool graphop_active_editable_fcurve_cxt_poll(Cxt *C)
{
  ApiPtr ptr = cxt_data_ptr_get_type(C, "active_editable_fcurve", &ApiFCurve);

  return ptr.data != nullptr;
}

bool graphop_sel_fcurve_poll(Cxt *C)
{
  AnimCxt ac;
  List anim_data = {nullptr, nullptr};
  ScrArea *area = cxt_win_area(C);
  size_t items;
  int filter;

  /* firstly, check if in Graph Editor */
  /* TODO: also check for rgn? */
  if ((area == nullptr) || (area->spacetype != SPACE_GRAPH)) {
    return false;
  }

  /* try to init Anim-Cxt stuff ourselves and check */
  if (anim_animdata_get_cxt(C, &ac) == 0) {
    return false;
  }

  /* Get the editable + selected F-Curves, and as long as we got some, we can return.
   * NOTE: curve-visible flag isn't included,
   * otherwise sel a curve via list to edit is too cumbersome. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_SEL | ANIMFILTER_FOREDIT |
            ANIMFILTER_FCURVESONLY);
  items = anim_animdata_filter(
      &ac, &anim_data, eAnimFilterFlags(filter), ac.data, eAnimContTypes(ac.datatype));
  if (items == 0) {
    return false;
  }

  /* cleanup and return findings */
  anim_animdata_freelist(&anim_data);
  return true;
}
