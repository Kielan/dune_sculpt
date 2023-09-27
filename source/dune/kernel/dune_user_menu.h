#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct List;
struct UserMenu;
struct UserMenuItem;

struct UserMenu *dune_user_menu_find(struct List *list,
                                     char space_type,
                                     const char *cxt);
struct UserMenu *dune_user_menu_ensure(struct List *list,
                                       char space_type,
                                       const char *cxt);

struct UserMenuItem *dune_user_menu_item_add(struct List *list, int type);
void dune_user_menu_item_free(struct UserMenuItem *umi);
void dune_user_menu_item_free_list(struct List *list);

#ifdef __cplusplus
}
#endif
