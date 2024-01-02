/* Routines for working w single linked lists of 'links' - ptrs to other data.
 * For double linked lists see 'lib_list.h'. */
#include <stdlib.h>

#include "mem_guardedalloc.h"

#include "lib_linklist.h"
#include "lib_memarena.h"
#include "lib_mempool.h"
#include "lib_utildefines.h"

#include "lib_strict_flags.h"

int lib_linklist_count(const LinkNode *list)
{
  int len;

  for (len = 0; list; list = list->next) {
    len++;
  }

  return len;
}

int lib_linklist_index(const LinkNode *list, void *ptr)
{
  int index;

  for (index = 0; list; list = list->next, index++) {
    if (list->link == ptr) {
      return index;
    }
  }

  return -1;
}

LinkNode *lib_linklist_find(LinkNode *list, int index)
{
  int i;

  for (i = 0; list; list = list->next, i++) {
    if (i == index) {
      return list;
    }
  }

  return NULL;
}

LinkNode *lib_linklist_find_last(LinkNode *list)
{
  if (list) {
    while (list->next) {
      list = list->next;
    }
  }
  return list;
}

void lib_linklist_reverse(LinkNode **listp)
{
  LinkNode *rhead = NULL, *cur = *listp;

  while (cur) {
    LinkNode *next = cur->next;

    cur->next = rhead;
    rhead = cur;

    cur = next;
  }

  *listp = rhead;
}

void lib_linklist_move_item(LinkNode **listp, int curr_index, int new_index)
{
  LinkNode *lnk, *lnk_psrc = NULL, *lnk_pdst = NULL;
  int i;

  if (new_index == curr_index) {
    return;
  }

  if (new_index < curr_index) {
    for (lnk = *listp, i = 0; lnk; lnk = lnk->next, i++) {
      if (i == new_index - 1) {
        lnk_pdst = lnk;
      }
      else if (i == curr_index - 1) {
        lnk_psrc = lnk;
        break;
      }
    }

    if (!(lnk_psrc && lnk_psrc->next && (!lnk_pdst || lnk_pdst->next))) {
      /* Invalid indices, abort. */
      return;
    }

    lnk = lnk_psrc->next;
    lnk_psrc->next = lnk->next;
    if (lnk_pdst) {
      lnk->next = lnk_pdst->next;
      lnk_pdst->next = lnk;
    }
    else {
      /* destination is 1st elem of the list... */
      lnk->next = *listp;
      *listp = lnk;
    }
  }
  else {
    for (lnk = *listp, i = 0; lnk; lnk = lnk->next, i++) {
      if (i == new_index) {
        lnk_pdst = lnk;
        break;
      }
      if (i == curr_index - 1) {
        lnk_psrc = lnk;
      }
    }

    if (!(lnk_pdst && (!lnk_psrc || lnk_psrc->next))) {
      /* Invalid indices, abort. */
      return;
    }

    if (lnk_psrc) {
      lnk = lnk_psrc->next;
      lnk_psrc->next = lnk->next;
    }
    else {
      /* src is first elem of the list... */
      lnk = *listp;
      *listp = lnk->next;
    }
    lnk->next = lnk_pdst->next;
    lnk_pdst->next = lnk;
  }
}

void lib_linklist_prepend_nlink(LinkNode **listp, void *ptr, LinkNode *nlink)
{
  nlink->link = ptr;
  nlink->next = *listp;
  *listp = nlink;
}

void lib_linklist_prepend(LinkNode **listp, void *ptr)
{
  LinkNode *nlink = mem_malloc(sizeof(*nlink), __func__);
  lib_linklist_prepend_nlink(listp, ptr, nlink);
}

void lib_linklist_prepend_arena(LinkNode **listp, void *ptr, MemArena *ma)
{
  LinkNode *nlink = lib_memarena_alloc(ma, sizeof(*nlink));
  lib_linklist_prepend_nlink(listp, ptr, nlink);
}

void lib_linklist_prepend_pool(LinkNode **listp, void *ptr, alibMempool *mempool)
{
  LinkNode *nlink = lib_mempool_alloc(mempool);
  lib_linklist_prepend_nlink(listp, ptr, nlink);
}

void lib_linklist_append_nlink(LinkNodePair *list_pair, void *ptr, LinkNode *nlink)
{
  nlink->link = ptr;
  nlink->next = NULL;

  if (list_pair->list) {
    lib_assert((list_pair->last_node != NULL) && (list_pair->last_node->next == NULL));
    list_pair->last_node->next = nlink;
  }
  else {
    lib_assert(list_pair->last_node == NULL);
    list_pair->list = nlink;
  }

  list_pair->last_node = nlink;
}

void lib_linklist_append(LinkNodePair *list_pair, void *ptr)
{
  LinkNode *nlink = mem_malloc(sizeof(*nlink), __func__);
  lib_linklist_append_nlink(list_pair, ptr, nlink);
}

void lib_linklist_append_arena(LinkNodePair *list_pair, void *ptr, MemArena *ma)
{
  LinkNode *nlink = lib_memarena_alloc(ma, sizeof(*nlink));
  lib_linklist_append_nlink(list_pair, ptr, nlink);
}

void lib_linklist_append_pool(LinkNodePair *list_pair, void *ptr, BLI_mempool *mempool)
{
  LinkNode *nlink = lib_mempool_alloc(mempool);
  lib_linklist_append_nlink(list_pair, ptr, nlink);
}

void *lib_linklist_pop(LinkNode **listp)
{
  /* intentionally no NULL check */
  void *link = (*listp)->link;
  void *next = (*listp)->next;

  mem_free(*listp);

  *listp = next;
  return link;
}

void *lib_linklist_pop_pool(LinkNode **listp, BLI_mempool *mempool)
{
  /* intentionally no NULL check */
  void *link = (*listp)->link;
  void *next = (*listp)->next;

  lib_mempool_free(mempool, (*listp));

  *listp = next;
  return link;
}

void lib_linklist_insert_after(LinkNode **listp, void *ptr)
{
  LinkNode *nlink = mem_malloc(sizeof(*nlink), __func__);
  LinkNode *node = *listp;

  nlink->link = ptr;

  if (node) {
    nlink->next = node->next;
    node->next = nlink;
  }
  else {
    nlink->next = NULL;
    *listp = nlink;
  }
}

void lib_linklist_free(LinkNode *list, LinkNodeFreeFP freef )
{
  while (list) {
    LinkNode *next = list->next;

    if (freefn) {
      freefn(list->link);
    }
    mem_free(list);

    list = next;
  }
}

void lib_linklist_free_pool(LinkNode *list, LinkNodeFreeFP freefb, LibMempool *mempool)
{
  while (list) {
    LinkNode *next = list->next;

    if (freefn) {
      freefn(list->link);
    }
    lib_mempool_free(mempool, list);

    list = next;
  }
}

void lib_linklist_free(LinkNode *list)
{
  while (list) {
    LinkNode *next = list->next;

    mem_free(list->link);
    mem_free(list);

    list = next;
  }
}

void lib_linklist_apply(LinkNode *list, LinkNodeApplyFP applyfn, void *userdata)
{
  for (; list; list = list->next) {
    applyfn(list->link, userdata);
  }
}

/* Sort */
#define SORT_IMPL_LINKTYPE LinkNode
#define SORT_IMPL_LINKTYPE_DATA link

/* regular call */
#define SORT_IMPL_FN linklist_sort_fn
#include "list_sort_impl.h"
#undef SORT_IMPL_FN

/* re-entrant call */
#define SORT_IMPL_USE_THUNK
#define SORT_IMPL_FN linklist_sort_fn_r
#include "list_sort_impl.h"
#undef SORT_IMPL_FN
#undef SORT_IMPL_USE_THUNK

#undef SORT_IMPL_LINKTYPE
#undef SORT_IMPL_LINKTYPE_DATA

LinkNode *lib_linklist_sort(LinkNode *list, int (*cmp)(const void *, const void *))
{
  if (list && list->next) {
    list = linklist_sort_fn(list, cmp);
  }
  return list;
}

LinkNode *lib_linklist_sort_r(LinkNode *list,
                              int (*cmp)(void *, const void *, const void *),
                              void *thunk)
{
  if (list && list->next) {
    list = linklist_sort_fn_r(list, cmp, thunk);
  }
  return list;
}
