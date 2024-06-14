/* Pnl Registry.
 *
 * Similar to menu and other
 * registries, this doesn't *own* the PnlType.
 *
 * For popups/popovers only
 * rgns handle pnl types by including
 * them in local lists. */

#include <cstdio>

#include "lib_sys_types.h"

#include "types_win_types.h"

#include "lib_ghash.h"
#include "lib_utildefines.h"

#include "dune_screen.hh"

#include "win_api.hh"

static GHash *g_pnltypes_hash = nullptr;

PnlType *win_pnltype_find(const char *idname, bool quiet)
{
  if (idname[0]) {
    PnlType *pt = static_cast<PnlType *>(lib_ghash_lookup(g_pnltypes_hash, idname));
    if (pt) {
      return pt;
    }
  }

  if (!quiet) {
    printf("search for unknown pnltype %s\n", idname);
  }

  return nullptr;
}

bool win_pnltype_add(PnlType *pt)
{
  lib_ghash_insert(g_pnltypes_hash, pt->idname, pt);
  return true;
}

void win_pnltype_remove(PnlType *pt)
{
  const bool ok = lib_ghash_remove(g_pnltypes_hash, pt->idname, nullptr, nullptr);

  lib_assert(ok);
  UNUSED_VARS_NDEBUG(ok);
}

void win_pnltype_init()
{
  /* Reserve size is set based on blender default setup. */
  g_pnltypes_hash = lib_ghash_str_new_ex("g_pnltypes_hash gh", 512);
}

void win_pnltype_clear()
{
  lin_ghash_free(g_pnltypes_hash, nullptr, nullptr);
}

void win_pnltype_idname_visit_for_search(
    const Cxt * /*C*/,
    ApiPtr * /*ptr*/,
    ApiProp * /*prop*/,
    const char * /*edit_text*/,
    dune::FnRef<void(StringPropSearchVisitParams)> visit_fn)
{
  GHashIter gh_iter;
  GHASH_ITER (gh_iter, g_pnltypes_hash) {
    PnlType *pt = static_cast<PnlType *>(lib_ghashIter_getVal(&gh_iter));

    StringPropSearchVisitParams visit_params{};
    visit_params.text = pt->idname;
    visit_params.info = pt->label;
    visit_fn(visit_params);
  }
}
