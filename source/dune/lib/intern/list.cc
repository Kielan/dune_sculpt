/* Manipulations on double-linked list (List structs).
 * For single linked lists see 'lib_linklist.h' */
#include <cstdlib>
#include <cstring>

#include "mem_guardedalloc.h"
#include "types_list.h"
#include "lib_list.h"
#include "lib_strict_flags.h"

void lib_movelisttolist(List *dst, List *src)
{
  if (src->first == nullptr) {
    return;
  }

  if (dst->first == nullptr) {
    dst->first = src->first;
    dst->last = src->last;
  }
  else {
    ((Link *)dst->last)->next = static_cast<Link *>(src->first);
    ((Link *)src->first)->prev = static_cast<Link *>(dst->last);
    dst->last = src->last;
  }
  src->first = src->last = nullptr;
}

void lib_movelisttolist_reverse(List *dst, List *src)
{
  if (src->first == nullptr) {
    return;
  }

  if (dst->first == nullptr) {
    dst->first = src->first;
    dst->last = src->last;
  }
  else {
    ((Link *)src->last)->next = static_cast<Link *>(dst->first);
    ((Link *)dst->first)->prev = static_cast<Link *>(src->last);
    dst->first = src->first;
  }

  src->first = src->last = nullptr;
}

void lib_list_split_after(List *original_list, List *split_list, void *vlink)
{
  lib_assert(lib_list_is_empty(split_list));
  lib_assert(vlink == nullptr || lib_findindex(original_list, vlink) >= 0);

  if (vlink == original_list->last) {
    /* Nothing to split, and `split_listbase` is assumed alrdy empty (see assert above). */
    return;
  }

  if (vlink == nullptr) {
    /* Move everything into `split_list`. */
    SWAP(List, *original_list, *split_list);
    return;
  }

  Link *link = static_cast<Link *>(vlink);
  Link *next_link = link->next;
  lib_assert(next_link != nullptr);
  Link *last_link = static_cast<Link *>(original_list->last);

  original_list->last = link;
  split_list->first = next_link;
  split_list->last = last_link;

  link->next = nullptr;
  next_link->prev = nullptr;
}

void lib_addhead(List *list, void *vlink)
{
  Link *link = static_cast<Link *>(vlink);

  if (link == nullptr) {
    return;
  }

  link->next = static_cast<Link *>(list->first);
  link->prev = nullptr;

  if (list->first) {
    ((Link *)list->first)->prev = link;
  }
  if (list->last == nullptr) {
    list->last = link;
  }
  list->first = link;
}

void lib_addtail(List *list, void *vlink)
{
  Link *link = static_cast<Link *>(vlink);

  if (link == nullptr) {
    return;
  }

  link->next = nullptr;
  link->prev = static_cast<Link *>(listbase->last);

  if (list->last) {
    ((Link *)list->last)->next = link;
  }
  if (list->first == nullptr) {
    list->first = link;
  }
  list->last = link;
}

void lib_remlink(List *list, void *vlink)
{
  Link *link = static_cast<Link *>(vlink);

  if (link == nullptr) {
    return;
  }

  if (link->next) {
    link->next->prev = link->prev;
  }
  if (link->prev) {
    link->prev->next = link->next;
  }

  if (list->last == link) {
    list->last = link->prev;
  }
  if (list->first == link) {
    list->first = link->next;
  }
}

bool lib_remlink_safe(List *list, void *vlink)
{
  if (lib_findindex(list, vlink) != -1) {
    lib_remlink(listbase, vlink);
    return true;
  }

  return false;
}

void lib_list_swaplinks(List *list, void *vlinka, void *vlinkb)
{
  Link *linka = static_cast<Link *>(vlinka);
  Link *linkb = static_cast<Link *>(vlinkb);

  if (!linka || !linkb) {
    return;
  }

  if (linkb->next == linka) {
    std::swap(linka, linkb);
  }

  if (linka->next == linkb) {
    linka->next = linkb->next;
    linkb->prev = linka->prev;
    linka->prev = linkb;
    linkb->next = linka;
  }
  else { /* Non-contiguous items, we can safely swap. */
    std::swap(linka->prev, linkb->prev);
    std::swap(linka->next, linkb->next);
  }

  /* Update neighbors of linka and linkb. */
  if (linka->prev) {
    linka->prev->next = linka;
  }
  if (linka->next) {
    linka->next->prev = linka;
  }
  if (linkb->prev) {
    linkb->prev->next = linkb;
  }
  if (linkb->next) {
    linkb->next->prev = linkb;
  }

  if (list->last == linka) {
    list->last = linkb;
  }
  else if (list->last == linkb) {
    list->last = linka;
  }

  if (list->first == linka) {
    list->first = linkb;
  }
  else if (list->first == linkb) {
    list->first = linka;
  }
}

void lib_list_swaplinks(List *lista, List *listb, void *vlinka, void *vlinkb)
{
  Link *linka = static_cast<Link *>(vlinka);
  Link *linkb = static_cast<Link *>(vlinkb);
  Link linkc = {nullptr};

  if (!linka || !linkb) {
    return;
  }

  /* The ref to `linkc` assigns nullptr, not a dangling ptr so it can be ignored. */
#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 1201 /* gcc12.1+ only */
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif

  /* Tmp link to use as placeholder of the links positions */
  lib_insertlinkafter(lista, linka, &linkc);

#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 1201 /* gcc12.1+ only */
#  pragma GCC diagnostic pop
#endif

  /* Bring linka into linkb position */
  lib_remlink(lista, linka);
  lib_insertlinkafter(listb, linkb, linka);

  /* Bring linkb into linka position */
  lib_remlink(listb, linkb);
  lib_insertlinkafter(lista, &linkc, linkb);

  /* Remove tmp link */
  lib_remlink(lista, &linkc);
}

void *lib_pophead(List *list)
{
  Link *link;
  if ((link = static_cast<Link *>(list->first))) {
    lib_remlink(list, link);
  }
  return link;
}

void *lib_poptail(List *list)
{
  Link *link;
  if ((link = static_cast<Link *>(listbase->last))) {
    BLI_remlink(listbase, link);
  }
  return link;
}

void lib_freelink(List *list, void *vlink)
{
  Link *link = static_cast<Link *>(vlink);

  if (link == nullptr) {
    return;
  }

  lib_remlink(list, link);
  mem_free(link);
}

/* Assigns all Link.prev ptrs from Link.next */
static void list_double_from_single(Link *iter, List *list)
{
  Link *prev = nullptr;
  list->first = iter;
  do {
    iter->prev = prev;
    prev = iter;
  } while ((iter = iter->next));
  list->last = prev;
}

#define SORT_IMPL_LINKTYPE Link

/* regular call */
#define SORT_IMPL_FUNC list_sort_fn
#include "list_sort_impl.h"
#undef SORT_IMPL_FN

/* re-entrant call */
#define SORT_IMPL_USE_THUNK
#define SORT_IMPL_FN list_sort_fn_r
#include "list_sort_impl.h"
#undef SORT_IMPL_FN
#undef SORT_IMPL_USE_THUNK

#undef SORT_IMPL_LINKTYPE

void lib_list_sort(List *list, int (*cmp)(const void *, const void *))
{
  if (list->first != list->last) {
    Link *head = static_cast<Link *>(list->first);
    head = list_sort_fn(head, cmp);
    list_double_from_single(head, list);
  }
}

void lib_list_sort_r(List *list,
                     int (*cmp)(void *, const void *, const void *),
                     void *thunk)
{
  if (list->first != list->last) {
    Link *head = static_cast<Link *>(list->first);
    head = list_sort_fn_r(head, cmp, thunk);
    list_double_from_single(head, list);
  }
}

void lib_insertlinkafter(List *list, void *vprevlink, void *vnewlink)
{
  Link *prevlink = static_cast<Link *>(vprevlink);
  Link *newlink = static_cast<Link *>(vnewlink);

  /* newlink before nextlink */
  if (newlink == nullptr) {
    return;
  }

  /* empty list */
  if (list->first == nullptr) {
    list->first = newlink;
    list->last = newlink;
    return;
  }

  /* insert at head of list */
  if (prevlink == nullptr) {
    newlink->prev = nullptr;
    newlink->next = static_cast<Link *>(listbase->first);
    newlink->next->prev = newlink;
    list->first = newlink;
    return;
  }

  /* at end of list */
  if (list->last == prevlink) {
    list->last = newlink;
  }

  newlink->next = prevlink->next;
  newlink->prev = prevlink;
  prevlink->next = newlink;
  if (newlink->next) {
    newlink->next->prev = newlink;
  }
}

void lib_insertlinkbefore(List *list, void *vnextlink, void *vnewlink)
{
  Link *nextlink = static_cast<Link *>(vnextlink);
  Link *newlink = static_cast<Link *>(vnewlink);

  /* newlink before nextlink */
  if (newlink == nullptr) {
    return;
  }

  /* empty list */
  if (list->first == nullptr) {
    list->first = newlink;
    list->last = newlink;
    return;
  }

  /* insert at end of list */
  if (nextlink == nullptr) {
    newlink->prev = static_cast<Link *>(list->last);
    newlink->next = nullptr;
    ((Link *)list->last)->next = newlink;
    list->last = newlink;
    return;
  }

  /* at beginning of list */
  if (list->first == nextlink) {
    list->first = newlink;
  }

  newlink->next = nextlink;
  newlink->prev = nextlink->prev;
  nextlink->prev = newlink;
  if (newlink->prev) {
    newlink->prev->next = newlink;
  }
}

void lib_insertlinkreplace(ListBase *listbase, void *vreplacelink, void *vnewlink)
{
  Link *l_old = static_cast<Link *>(vreplacelink);
  Link *l_new = static_cast<Link *>(vnewlink);

  /* update adjacent links */
  if (l_old->next != nullptr) {
    l_old->next->prev = l_new;
  }
  if (l_old->prev != nullptr) {
    l_old->prev->next = l_new;
  }

  /* set direct links */
  l_new->next = l_old->next;
  l_new->prev = l_old->prev;

  /* update list */
  if (list->first == l_old) {
    list->first = l_new;
  }
  if (list->last == l_old) {
    list->last = l_new;
  }
}

bool lib_list_link_move(List *list, void *vlink, int step)
{
  Link *link = static_cast<Link *>(vlink);
  Link *hook = link;
  const bool is_up = step < 0;

  if (step == 0) {
    return false;
  }
  lib_assert_lib_findindex(listbase, link) != -1);

  /* find link to insert before/after */
  const int abs_step = abs(step);
  for (int i = 0; i < abs_step; i++) {
    hook = is_up ? hook->prev : hook->next;
    if (!hook) {
      return false;
    }
  }

  /* reinsert link */
  BLI_remlink(listbase, vlink);
  if (is_up) {
    BLI_insertlinkbefore(listbase, hook, vlink);
  }
  else {
    BLI_insertlinkafter(listbase, hook, vlink);
  }
  return true;
}

bool BLI_listbase_move_index(ListBase *listbase, int from, int to)
{
  if (from == to) {
    return false;
  }

  /* Find the link to move. */
  void *link = BLI_findlink(listbase, from);

  if (!link) {
    return false;
  }

  return BLI_listbase_link_move(listbase, link, to - from);
}

void BLI_freelist(ListBase *listbase)
{
  Link *link, *next;

  link = static_cast<Link *>(listbase->first);
  while (link) {
    next = link->next;
    free(link);
    link = next;
  }

  BLI_listbase_clear(listbase);
}

void BLI_freelistN(ListBase *listbase)
{
  Link *link, *next;

  link = static_cast<Link *>(listbase->first);
  while (link) {
    next = link->next;
    MEM_freeN(link);
    link = next;
  }

  BLI_listbase_clear(listbase);
}

int BLI_listbase_count_at_most(const ListBase *listbase, const int count_max)
{
  Link *link;
  int count = 0;

  for (link = static_cast<Link *>(listbase->first); link && count != count_max; link = link->next)
  {
    count++;
  }

  return count;
}

int BLI_listbase_count(const ListBase *listbase)
{
  int count = 0;
  LISTBASE_FOREACH (Link *, link, listbase) {
    count++;
  }

  return count;
}

void *lib_findlink(const ListBase *listbase, int number)
{
  Link *link = nullptr;

  if (number >= 0) {
    link = static_cast<Link *>(listbase->first);
    while (link != nullptr && number != 0) {
      number--;
      link = link->next;
    }
  }

  return link;
}

void *lib_rfindlink(const List *list, int num)
{
  Link *link = nullptr;

  if (num >= 0) {
    link = static_cast<Link *>(list->last);
    while (link != nullptr && num != 0) {
      num--;
      link = link->prev;
    }
  }

  return link;
}

void *lib_findlinkfrom(Link *start, int steps)
{
  Link *link = nullptr;

  if (steps >= 0) {
    link = start;
    while (link != nullptr && steps != 0) {
      steps--;
      link = link->next;
    }
  }
  else {
    link = start;
    while (link != nullptr && steps != 0) {
      steps++;
      link = link->prev;
    }
  }

  return link;
}

int lib_findindex(const List *list, const void *vlink)
{
  Link *link = nullptr;
  int num = 0;

  if (vlink == nullptr) {
    return -1;
  }

  link = static_cast<Link *>(list->first);
  while (link) {
    if (link == vlink) {
      return num;
    }

    num++;
    link = link->next;
  }

  return -1;
}

void *lib_findstring(const List *list, const char *id, const int offset)
{
  const char *id_iter;

  if (id == nullptr) {
    return nullptr;
  }

  LIST_FOREACH (Link *, link, listbase) {
    id_iter = ((const char *)link) + offset;

    if (id[0] == id_iter[0] && STREQ(id, id_iter)) {
      return link;
    }
  }

  return nullptr;
}
void *lib_rfindstring(const List *list, const char *id, const int offset)
{
  /* Same as lib_findstring but find reverse. */
  LIST_FOREACH_BACKWARD (Link *, link, list) {
    const char *id_iter = ((const char *)link) + offset;
    if (id[0] == id_iter[0] && STREQ(id, id_iter)) {
      return link;
    }
  }

  return nullptr;
}

void *lib_findstring_ptr(const List *list, const char *id, const int offset)
{
  const char *id_iter;

  LIST_FOREACH (Link *, link, list) {
    /* Exact copy of lib_findstring(), except for this line, and the check for potential nullptr
     * below. */
    id_iter = *((const char **)(((const char *)link) + offset));
    if (id_iter && id[0] == id_iter[0] && STREQ(id, id_iter)) {
      return link;
    }
  }

  return nullptr;
}
void *lib_rfindstring_ptr(const List *list, const char *id, const int offset)
{
  /* Same as lib_findstring_ptr but find reverse. */

  const char *id_iter;

  LIST_FOREACH_BACKWARD (Link *, link, listbase) {
    /* Exact copy of lib_rfindstring(), except for this line, and the check for potential nullptr
     * below. */
    id_iter = *((const char **)(((const char *)link) + offset));
    if (id_iter && id[0] == id_iter[0] && STREQ(id, id_iter)) {
      return link;
    }
  }

  return nullptr;
}

void *lib_list_findafter_string_ptr(Link *link, const char *id, const int offset)
{
  const char *id_iter;

  for (link = link->next; link; link = link->next) {
    /* Exact copy of lib_findstring(), except for this line, and the check for potential nullptr
     * below. */
    id_iter = *((const char **)(((const char *)link) + offset));
    if (id_iter && id[0] == id_iter[0] && STREQ(id, id_iter)) {
      return link;
    }
  }

  return nullptr;
}

void *lib_findptr(const List *list, const void *ptr, const int offset)
{
  LIST_FOREACH (Link *, link, list) {
    /* Exact copy of lib_findstring(), except for this line. */
    const void *ptr_iter = *((const void **)(((const char *)link) + offset));
    if (ptr == ptr_iter) {
      return link;
    }
  }
  return nullptr;
}
void *lib_rfindptr(const List *list, const void *ptr, const int offset)
{
  /* Same as lib_findptr but find reverse. */
  LIST_FOREACH_BACKWARD (Link *, link, list) {
    /* Exact copy of lib_rfindstring(), except for this line. */
    const void *ptr_iter = *((const void **)(((const char *)link) + offset));

    if (ptr == ptr_iter) {
      return link;
    }
  }
  return nullptr;
}

void *lib_list_bytes_find(const List *list,
                              const void *bytes,
                              const size_t bytes_size,
                              const int offset)
{
  LIST_FOREACH (Link *, link, list) {
    const void *ptr_iter = (const void *)(((const char *)link) + offset);
    if (memcmp(bytes, ptr_iter, bytes_size) == 0) {
      return link;
    }
  }

  return nullptr;
}
void *lib_list_bytes_rfind(const List *list,
                               const void *bytes,
                               const size_t bytes_size,
                               const int offset)
{
  /* Same as lib_list_bytes_find but find reverse. */
  LIST_FOREACH_BACKWARD (Link *, link, list) {
    const void *ptr_iter = (const void *)(((const char *)link) + offset);
    if (memcmp(bytes, ptr_iter, bytes_size) == 0) {
      return link;
    }
  }
  return nullptr;
}

void *lib_list_string_or_index_find(const List *list,
                                    const char *string,
                                    const size_t string_offset,
                                    const int index)
{
  Link *link = nullptr;
  Link *link_at_index = nullptr;

  int index_iter;
  for (link = static_cast<Link *>(listbase->first), index_iter = 0; link;
       link = link->next, index_iter++)
  {
    if (string != nullptr && string[0] != '\0') {
      const char *string_iter = ((const char *)link) + string_offset;

      if (string[0] == string_iter[0] && STREQ(string, string_iter)) {
        return link;
      }
    }
    if (index_iter == index) {
      link_at_index = link;
    }
  }
  return link_at_index;
}

int lib_findstringindex(const List *list, const char *id, const int offset)
{
  Link *link = nullptr;
  const char *id_iter;
  int i = 0;

  link = static_cast<Link *>(list->first);
  while (link) {
    id_iter = ((const char *)link) + offset;

    if (id[0] == id_iter[0] && STREQ(id, id_iter)) {
      return i;
    }
    i++;
    link = link->next;
  }

  return -1;
}

List lib_list_from_link(Link *some_link)
{
  List list = {some_link, some_link};
  if (some_link == nullptr) {
    return list;
  }

  /* Find the first element. */
  while (((Link *)list.first)->prev != nullptr) {
    list.first = ((Link *)list.first)->prev;
  }

  /* Find the last element. */
  while (((Link *)list.last)->next != nullptr) {
    list.last = ((Link *)list.last)->next;
  }

  return list;
}

void lib_duplist(List *dst, const List *src)
{
  Link *dst_link, *src_link;

  /* in this order, to ensure it works if dst == src */
  src_link = static_cast<Link *>(src->first);
  dst->first = dst->last = nullptr;

  while (src_link) {
    dst_link = static_cast<Link *>(mem_dupalloc(src_link));
    lib_addtail(dst, dst_link);

    src_link = src_link->next;
  }
}

void lib_list_reverse(List *lb)
{
  Link *curr = static_cast<Link *>(lb->first);
  Link *prev = nullptr;
  Link *next = nullptr;
  while (curr) {
    next = curr->next;
    curr->next = prev;
    curr->prev = next;
    prev = curr;
    curr = next;
  }

  /* swap first/last */
  curr = static_cast<Link *>(lb->first);
  lb->first = lb->last;
  lb->last = curr;
}

void lib_list_rotate_first(List *lb, void *vlink)
{
  /* make circular */
  ((Link *)lb->first)->prev = static_cast<Link *>(lb->last);
  ((Link *)lb->last)->next = static_cast<Link *>(lb->first);

  lb->first = vlink;
  lb->last = ((Link *)vlink)->prev;

  ((Link *)lb->first)->prev = nullptr;
  ((Link *)lb->last)->next = nullptr;
}

void lib_list_rotate_last(ListBase *lb, void *vlink)
{
  /* make circular */
  ((Link *)lb->first)->prev = static_cast<Link *>(lb->last);
  ((Link *)lb->last)->next = static_cast<Link *>(lb->first);

  lb->first = ((Link *)vlink)->next;
  lb->last = vlink;

  ((Link *)lb->first)->prev = nullptr;
  ((Link *)lb->last)->next = nullptr;
}

bool lib_list_validate(ListBase *lb)
{
  if (lb->first == nullptr && lb->last == nullptr) {
    /* Empty list. */
    return true;
  }
  if (ELEM(nullptr, lb->first, lb->last)) {
    /* If one of the pointer is null, but not this other, this is a corrupted listbase. */
    return false;
  }

  /* Walk the list in bot directions to ensure all next & prev pointers are valid and consistent.
   */
  LIST_FOREACH (Link *, lb_link, lb) {
    if (lb_link == lb->first) {
      if (lb_link->prev != nullptr) {
        return false;
      }
    }
    if (lb_link == lb->last) {
      if (lb_link->next != nullptr) {
        return false;
      }
    }
  }
  LIST_FOREACH_BACKWARD (Link *, lb_link, lb) {
    if (lb_link == lb->last) {
      if (lb_link->next != nullptr) {
        return false;
      }
    }
    if (lb_link == lb->first) {
      if (lb_link->prev != nullptr) {
        return false;
      }
    }
  }

  return true;
}

LinkData *lib_genericNode(void *data)
{
  LinkData *ld;

  if (data == nullptr) {
    return nullptr;
  }

  /* create new link, and make it hold the given data */
  ld = MEM_cnew<LinkData>(__func__);
  ld->data = data;

  return ld;
}
