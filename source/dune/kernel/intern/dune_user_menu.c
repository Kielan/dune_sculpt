/* User defined menu API. */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "LIB_listbase.h"
#include "LIB_string.h"

#include "STRUCTS_userdef_types.h"

#include "KERNEL_dune_user_menu.h"
#include "KERNEL_idprop.h"

/* -------------------------------------------------------------------- */
/* Menu Type */

bUserMenu *KERNEL_dune_user_menu_find(ListBase *lb, char space_type, const char *context)
{
  LISTBASE_FOREACH (duneUserMenu *, um, lb) {
    if ((space_type == um->space_type) && (STREQ(context, um->context))) {
      return um;
    }
  }
  return NULL;
}

bUserMenu *KERNEL_dune_user_menu_ensure(ListBase *lb, char space_type, const char *context)
{
  bUserMenu *um = KERNEL_dune_user_menu_find(lb, space_type, context);
  if (um == NULL) {
    um = MEM_callocN(sizeof(duneUserMenu), __func__);
    um->space_type = space_type;
    STRNCPY(um->context, context);
    LIB_addhead(lb, um);
  }
  return um;
}

/* -------------------------------------------------------------------- */
/** Menu Item **/

bUserMenuItem *KERNEL_dune_user_menu_item_add(ListBase *lb, int type)
{
  uint size;

  if (type == USER_MENU_TYPE_SEP) {
    size = sizeof(duneUserMenuItem);
  }
  else if (type == USER_MENU_TYPE_OPERATOR) {
    size = sizeof(duneUserMenuItem_Op);
  }
  else if (type == USER_MENU_TYPE_MENU) {
    size = sizeof(duneUserMenuItem_Menu);
  }
  else if (type == USER_MENU_TYPE_PROP) {
    size = sizeof(duneUserMenuItem_Prop);
  }
  else {
    size = sizeof(duneUserMenuItem);
    LIB_assert(0);
  }

  duneUserMenuItem *umi = MEM_callocN(size, __func__);
  umi->type = type;
  LIB_addtail(lb, umi);
  return umi;
}

void KERNEL_dune_user_menu_item_free(duneUserMenuItem *umi)
{
  if (umi->type == USER_MENU_TYPE_OPERATOR) {
    duneUserMenuItem_Op *umi_op = (duneUserMenuItem_Op *)umi;
    if (umi_op->prop) {
      IDP_FreeProperty(umi_op->prop);
    }
  }
  MEM_freeN(umi);
}

void KERNEL_dune_user_menu_item_free_list(ListBase *lb)
{
  for (duneUserMenuItem *umi = lb->first, *umi_next; umi; umi = umi_next) {
    umi_next = umi->next;
    KERNEL_dune_user_menu_item_free(umi);
  }
  LIB_listbase_clear(lb);
}
