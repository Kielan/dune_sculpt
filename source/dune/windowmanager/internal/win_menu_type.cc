/* Menu Registry. */
#include <cstdio>

#include "lib_sys_types.h"

#include "types_win.h"

#include "mem_guardedalloc.h"

#include "lib_ghash.h"
#include "lib_utildefines.h"

#include "dune_cxt.hh"
#include "dune_screen.hh"
#include "dune_workspace.hh"

#include "win_api.hh"
#include "win_types.hh"

static GHash *menutypes_hash = nullptr;

MenuType *win_menutype_find(const char *idname, bool quiet)
{
  if (idname[0]) {
    MenuType *mt = static_cast<MenuType *>(lib_ghash_lookup(menutypes_hash, idname));
    if (mt) {
      return mt;
    }
  }

  if (!quiet) {
    printf("search for unknown menutype %s\n", idname);
  }

  return nullptr;
}

void win_menutype_iter(GHashIter *ghi)
{
  lib_ghashIter_init(ghi, menutypes_hash);
}

bool win_menutype_add(MenuType *mt)
{
  lib_assert((mt->description == nullptr) || (mt->description[0]));
  lib_ghash_insert(menutypes_hash, mt->idname, mt);
  return true;
}

void win_menutype_freelink(MenuType *mt)
{
  bool ok = lib_ghash_remove(menutypes_hash, mt->idname, nullptr, MEM_freeN);

  lib_assert(ok);
  UNUSED_VARS_NDEBUG(ok);
}

void win_menutype_init()
{
  /* Reserve size is set based on blender default setup. */
  menutypes_hash = lib_ghash_str_new_ex("menutypes_hash gh", 512);
}

void win_menutype_free()
{
  GHashIter gh_iter;

  GHASH_ITER (gh_iter, menutypes_hash) {
    MenuType *mt = static_cast<MenuType *>(BLI_ghashIterator_getValue(&gh_iter));
    if (mt->api_ext.free) {
      mt->api_ext.free(mt->rna_ext.data);
    }
  }

  lib_ghash_free(menutypes_hash, nullptr, MEM_freeN);
  menutypes_hash = nullptr;
}

bool win_menutype_poll(Cxt *C, MenuType *mt)
{
  /* If we're tagged, only use compatible. */
  if (mt->owner_id[0] != '\0') {
    const WorkSpace *workspace = cxt_win_workspace(C);
    if (dune_workspace_owner_id_check(workspace, mt->owner_id) == false) {
      return false;
    }
  }

  if (mt->poll != nullptr) {
    return mt->poll(C, mt);
  }
  return true;
}

void win_menutype_idname_visit_for_search(
    const Cxt * /*C*/,
    ApiPtr * /*ptr*/,
    ApiProp * /*prop*/,
    const char * /*edit_text*/,
    dune::FnRef<void(StringPropSearchVisitParams)> visit_fn)
{
  GHashIter gh_iter;
  GHASH_ITER (gh_iter, menutypes_hash) {
    MenuType *mt = static_cast<MenuType *>(lib_ghashIter_getVal(&gh_iter));

    StringPropSearchVisitParams visit_params{};
    visit_params.text = mt->idname;
    visit_params.info = mt->label;
    visit_fn(visit_params);
  }
}
