#include "LIB_ghash.h"
#include "LIB_listbase.h"

void KERNEL_addon_pref_type_init(void)
{
  LIB_assert(global_addonpreftype_hash == NULL);
  global_addonpreftype_hash = LIB_ghash_str_new(__func__);
}

/* kernel/keyconfig.c */

void KERNEL_keyconfig_pref_type_init(void)
{
  LIB_assert(global_keyconfigpreftype_hash == NULL);
  global_keyconfigpreftype_hash = LIB_ghash_str_new(__func__);
}
