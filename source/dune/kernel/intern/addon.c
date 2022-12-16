#include <stddef.h>
#include <stdlib.h>

#include "API_types.h"

#include "LIB_ghash.h"
#include "LIB_listbase.h"
#include "LIB_string.h"
#include "LIB_utildefines.h"

#include "KERNEL_addon.h" /* own include */
#include "KERNEL_idprop.h"

#include "_listBase.h"
#include "_userdef_types.h"

#include "MEM_guardedalloc.h"

#include "CLG_log.h"

static CLG_LogRef LOG = {"bke.addon"};

/* -------------------------------------------------------------------- */
/* Add-on New/Free */
bAddon *KERNEL_addon_new(void)
{
  bAddon *addon = MEM_callocN(sizeof(bAddon), "bAddon");
  return addon;
}

bAddon *KERNEL_addon_find(ListBase *addon_list, const char *module)
{
  return LIB_findstring(addon_list, module, offsetof(bAddon, module));
}

bAddon *KERNEL_addon_ensure(ListBase *addon_list, const char *module)
{
  bAddon *addon = KERNEL_addon_find(addon_list, module);
  if (addon == NULL) {
    addon = KERNEL_addon_new();
    LIB_strncpy(addon->module, module, sizeof(addon->module));
    LIB_addtail(addon_list, addon);
  }
  return addon;
}

bool KERNEL_addon_remove_safe(ListBase *addon_list, const char *module)
{
  bAddon *addon = LIB_findstring(addon_list, module, offsetof(bAddon, module));
  if (addon) {
    LIB_remlink(addon_list, addon);
    KERNEL_addon_free(addon);
    return true;
  }
  return false;
}

void KERNEL_addon_free(bAddon *addon)
{
  if (addon->prop) {
    IDP_FreeProperty(addon->prop);
  }
  MEM_freeN(addon);
}

/* -------------------------------------------------------------------- */
/* Add-on Preference API */

static GHash *global_addonpreftype_hash = NULL;

bAddonPrefType *KERNEL_addon_pref_type_find(const char *idname, bool quiet)
{
  if (idname[0]) {
    bAddonPrefType *apt;

    apt = LIB_ghash_lookup(global_addonpreftype_hash, idname);
    if (apt) {
      return apt;
    }

    if (!quiet) {
      CLOG_WARN(&LOG, "search for unknown addon-pref '%s'", idname);
    }
  }
  else {
    if (!quiet) {
      CLOG_WARN(&LOG, "search for empty addon-pref");
    }
  }

  return NULL;
}

void KERNEL_addon_pref_type_add(bAddonPrefType *apt)
{
  LIB_ghash_insert(global_addonpreftype_hash, apt->idname, apt);
}

void KERNEL_addon_pref_type_remove(const bAddonPrefType *apt)
{
  LIB_ghash_remove(global_addonpreftype_hash, apt->idname, NULL, MEM_freeN);
}

void KERNEL_addon_pref_type_init(void)
{
  LIB_assert(global_addonpreftype_hash == NULL);
  global_addonpreftype_hash = LIB_ghash_str_new(__func__);
}

void KERNEL_addon_pref_type_free(void)
{
  LIB_ghash_free(global_addonpreftype_hash, NULL, MEM_freeN);
  global_addonpreftype_hash = NULL;
}
