/* General Interface Region Code
 * Most logic is now in 'ui_rgn_*.c' */

#include "lib_list.h"
#include "lib_utildefines.h"
#include "mem_guardedalloc.h"

#include "dune_cxt.h"
#include "dune_screen.hh"

#include "win_api.hh"
#include "win_draw.hh"

#include "ed_screen.hh"

#include "ui_rgns_intern.hh"

ARgn *ui_rgn_tmp_add(Screen *screen)
{
  ARgn *rgn = mem_cnew<ARgn>(__func__);
  lib_addtail(&screen->rgnbase, rgn);

  rgn->rgntype = RGN_TYPE_TEMP;
  rgn->alignment = RGN_ALIGN_FLOAT;

  return rgn;
}

void ui_rgn_tmp_remove(Cxt *C, Screen *screen, Rgn *rgn)
{
  Win *win = cxt_win(C);

  lib_assert(rgn->rgntype == RGN_TYPE_TEMP);
  lib_assert(lib_findindex(&screen->rgnbase, rgn) != -1);
  if (win) {
    win_draw_rgn_clear(win, rgn);
  }

  ed_rgn_exit(C, rgn);
  dune_area_rgn_free(nullptr, rgn); /* nullptr: no space-type. */
  lib_freelink(&screen->rgnbase, rgn);

  if (cxt_win_rgn(C) == rgn) {
    cxt_win_rgn_set(C, nullptr);
  }
  if (cxt_win_menu(C) == rgn) {
    cxt_win_menu_set(C, nullptr);
  }
}
