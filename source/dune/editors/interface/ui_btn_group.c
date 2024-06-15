#include "lib_list.h"

#include "mem_guardedalloc.h"

#include "ui_intern.h"

/* Btn Groups */
void ui_block_new_btn_group(uiBlock *block, BtnGroupFlag flag)
{
  /* Don't create a new group if there is a "lock" on new groups. */
  if (!lib_list_is_empty(&block->btn_groups)) {
    BtnGroup *last_btn_group = block->btn_groups.last;
    if (last_btn_group->flag & UI_BTN_GROUP_LOCK) {
      return;
    }
  }

  BtnGroup *new_group = mem_malloc(sizeof(BtnGroup), __func__);
  lib_list_clear(&new_group->btns);
  new_group->flag = flag;
  lib_addtail(&block->btn_groups, new_group);
}

void btn_group_add_btn(uiBlock *block, Btn *btn)
{
  if (lib_list_is_empty(&block->btn_groups)) {
    ui_block_new_btn_group(block, 0);
  }

  BtnGroup *current_btn_group = block->btn_groups.last;

  /* We can't use the btn directly bc adding it to
   * this list would mess with its `prev` and `next` ptrs. */
  LinkData *btn_link = lib_genericNode(btn);
  lib_addtail(&current_btn_group->btns, btn_link);
}

static void btn_group_free(BtnGroup *btn_group)
{
  lib_freelist(&btn_group->btns);
  mem_free(btn_group);
}

void ui_block_free_btn_groups(uiBlock *block)
{
  LIST_FOREACH_MUTABLE (BtnGroup *, btn_group, &block->btn_groups) {
    btn_group_free(btn_group);
  }
}

void btn_group_replace_btn_ptr(uiBlock *block, const void *old_btn_ptr, Btn *new_btn)
{
  LIST_FOREACH (BtnGroup *, btn_group, &block->btn_groups) {
    LIST_FOREACH (LinkData *, link, &btn_group->btns) {
      if (link->data == old_btn_ptr) {
        link->data = new_btn;
        return;
      }
    }
  }
}
