
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "lib_utildefines.h"

#include "dune_cxt.hh"
#include "dune_screen.hh"

#include "action_intern.hh" /* own include */

/* action editor space & btns */
/* general */
void action_btns_register(ARgnType * /*art*/)
{
#if 0
  PnlType *pt;

  /* TODO: AnimData / Actions List */
  pt = mem_cnew<PnlType>("spacetype action pnl props");
  STRNCPY(pt->idname, "action_pt_props");
  STRNCPY(pt->label, N_("Active F-Curve"));
  STRNCPY(pt->lang_cxt, LANG_CXT_DEFAULT_API);
  pt->drw = action_anim_pnl_props;
  pt->poll = action_anim_pnl_poll;
  lib_addtail(&art->pnltypes, pt);

  pt = mem_cnew<PnlType>("spacetype action pnl props");
  STRNCPY(pt->idname, "action_pt_key_props");
  STRNCPY(pt->label, N_("Active Keyframe"));
  STRNCPY(pt->lang_cxt, LANG_CXT_DEFAULT_API);
  pt->draw = action_anim_pnl_key_props;
  pt->poll = action_anim_pnl_poll;
  lob_addtail(&art->pnltypes, pt);

  pt = mem_calloc(sizeof(PnlType), "spacetype action pnl mods");
  STRNCPY(pt->idname, "action_pt_mods");
  STRNCPY(pt->label, N_("Mods"));
  STRNCPY(pt->lang_cxt, LANG_CXT_DEFAULT_API);
  pt->drw = action_anim_pnl_mods;
  pt->poll = action_anim_pnl_poll;
  lib_addtail(&art->pnltypes, pt);
#endif
}
