#pragma once

#include "lib_compiler_attrs.h"
#include "lib_utildefines.h"
#include "types_list.h"
// struct List;
// struct LinkData;

/* Returns the position of a vlink within a listbase, numbering from 0, or -1 if not found. */
int lib_findindex(const struct List *list, const void *vlink) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1);
/* Returns the 0-based index of the first element of listbase which contains the specified
 * null-terminated string at the specified offset, or -1 if not found. */
int lib_findstringindex(const struct List *list,
                        const char *id,
                        int offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);

/* Return a List representing the entire list the given Link is in. */
List lib_list_from_link(struct Link *some_link);

/* Find forwards. */

/* Returns the nth element of a list, numbering from 0. */
void *lib_findlink(const struct List *list, int number) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1);

/* Returns the nth element after a link, numbering from 0. */
void *lib_findlinkfrom(struct Link *start, int number) ATTR_WARN_UNUSED_RESULT;

/* Finds the first element of a listbase which contains the null-terminated
 * string a id at the specified offset, returning NULL if not found. */
void *lib_findstring(const struct List *list,
                     const char *id,
                     int offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);
/* Finds the first element of a listbase which contains a pointer to the
 * null-terminated string a id at the specified offset, returning NULL if not found. */
void *lib_findstring_ptr(const struct List *list,
                         const char *id,
                         int offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);
/* Finds the first element of listbase which contains the specified pointer value
 * at the specified offset, returning NULL if not found. */
void *lib_findptr(const struct List *list,
                  const void *ptr,
                  int offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);
/* Finds the first element of listbase which contains the specified bytes
 * at the specified offset, returning NULL if not found */
void *lib_list_bytes_find(const List *list,
                          const void *bytes,
                          size_t bytes_size,
                          int offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2);
/* Find the first item in the list that matches the given string, or the given index as fallback.
 * note The string is only used is non-NULL and non-empty.
 * return The found item, or NULL */
void *lib_list_string_or_index_find(const struct List *list,
                                    const char *string,
                                    size_t string_offset,
                                    int index) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);

/* Find backwards. */
/* Returns the nth-last element of list, numbering from 0. */
void *lib_rfindlink(const struct List *list, int number) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1);
/* Finds the last element of listbase which contains the
 * null-terminated string id at the specified offset, returning NULL if not found */
void *lib_rfindstring(const struct List *list,
                      const char *id,
                      int offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);
/* Finds the last element of listbase which contains a pointer to the
 * null-terminated string id at the specified offset, returning NULL if not found */
void *lib_rfindstring_ptr(const struct List *list,
                          const char *id,
                          int offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);
/* Finds the last element of list which contains the specified ptr value
 * at the specified offset, returning NULL if not found. */
void *lib_rfindptr(const struct List *list,
                   const void *ptr,
                   int offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);
/* Finds the last element of list which contains the specified bytes
 * at the specified offset, returning NULL if not found. */
void *lib_list_bytes_rfind(const List *list,
                           const void *bytes,
                           size_t bytes_size,
                           int offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2);

/* Removes and disposes of the entire contents of a listbase using guardedalloc. */
void lib_freelistn(struct List *list) ATTR_NONNULL(1);
/* Appends a vlink (assumed to begin with a Link) onto list. */
void lib_addtail(struct List *list, void *vlink) ATTR_NONNULL(1);
/* Removes a vlink from a list. Assumes it is linked into there! */
void lib_remlink(struct List *list, void *vlink) ATTR_NONNULL(1);
/* Checks that a vlink is linked into listbase, removing it from there if so. */
bool lib_remlink_safe(struct List *list, void *vlink) ATTR_NONNULL(1);
/* Removes the head from a list and returns it */
void *lib_pophead(List *list) ATTR_NONNULL(1);
/* Removes the tail from a list and returns it. */
void *lib_poptail(List *list) ATTR_NONNULL(1);

/* Prepends a vlink (assumed to begin with a Link) onto list. */
void lib_addhead(struct List *list, void *vlink) ATTR_NONNULL(1);
/* Inserts a vnewlink immediately preceding a vnextlink in list.
 * Or, if a vnextlink is NULL, puts a vnewlink at the end of the list. */
void lib_insertlinkbefore(struct List *list, void *vnextlink, void *vnewlink)
    ATTR_NONNULL(1);
/* Inserts a vnewlink immediately following a vprevlink in a listbase.
 * Or, if a vprevlink is NULL, puts a vnewlink at the front of the list. */
void lib_insertlinkafter(struct List *list, void *vprevlink, void *vnewlink)
    ATTR_NONNULL(1);
/* Insert a link in place of another, without changing its position in the list.
 * Puts `vnewlink` in the position of `vreplacelink`, removing `vreplacelink`.
 * - `vreplacelink` *must* be in the list.
 * - `vnewlink` *must not* be in the list. */
void lib_insertlinkreplace(List *list, void *vreplacelink, void *vnewlink)
    ATTR_NONNULL(1, 2, 3);
/* Sorts the elements of listbase into the order defined by cmp
 * (which should return 1 if its first arg should come after its second arg).
 * This uses insertion sort, so NOT ok for large list. */
void lib_list_sort(struct List *list, int (*cmp)(const void *, const void *))
    ATTR_NONNULL(1, 2);
void lib_list_sort_r(List *list,
                     int (*cmp)(void *, const void *, const void *),
                     void *thunk) ATTR_NONNULL(1, 2);
/* Reinsert a vlink relative to its current position but offset by a step. Doesn't move
 * item if new position would exceed list (could optionally move to head/tail).
 *
 * param step: Absolute value defines step size, sign defines direction. E.g pass -1
 *              to move a vlink before previous, or 1 to move behind next.
 * return If position of a vlink has changed. */
bool lib_list_link_move(List *list, void *vlink, int step) ATTR_NONNULL();
/* Move the link at the index a from to the position at index a to.
 * return If the move was successful. */
bool lib_list_move_index(List *list, int from, int to) ATTR_NONNULL();
/* Removes and disposes of the entire contents of list using direct free(3). */
void lib_freelist(struct List *list) ATTR_NONNULL(1);
/* Returns the number of elements in a list, up until (and including count_max)
 * note Use to avoid redundant looping. */
int lib_list_count_at_most(const struct List *list,
                               int count_max) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);
/* Returns the number of elements in list. */
int lib_list_count(const struct List *list) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);
/* Removes a vlink from list and disposes of it. Assumes it is linked into there! */
void lib_freelinkn(struct List *list, void *vlink) ATTR_NONNULL(1);

/* Swaps a vlinka and a vlinkb in the list. Assumes they are both already in the list! */
void lib_list_swaplinks(struct List *list, void *vlinka, void *vlinkb)
    ATTR_NONNULL(1, 2);
/* Swaps a vlinka and a vlinkb from their respective lists.
 * Assumes they are both already in their a lista! */
void lib_list_swaplinks(struct List *lista,
                        struct List *listb,
                        void *vlinka,
                        void *vlinkb) ATTR_NONNULL(2, 3);

/* Moves the entire contents of a src onto the end of a dst */
void lib_movelisttolist(struct List *dst, struct List *src) ATTR_NONNULL(1, 2);
/* Moves the entire contents of a src at the beginning of a dst. */
void lib_movelisttolist_reverse(struct List *dst, struct List *src) ATTR_NONNULL(1, 2);
/* Sets dst to a duplicate of the entire contents of src. dst may be the same as src. */
void lib_duplicatelist(struct List *dst, const struct List *src) ATTR_NONNULL(1, 2);
void lib_list_reverse(struct List *lb) ATTR_NONNULL(1);
/* param vlink: Link to make first */
void lib_list_rotate_first(struct List *lb, void *vlink) ATTR_NONNULL(1, 2);
/* param vlink: Link to make last. */
void lib_list_rotate_last(struct List *lb, void *vlink) ATTR_NONNULL(1, 2);

/* Utility functions to avoid first/last references inline all over. */
LIB_INLINE bool lib_list_is_single(const struct List *lb)
{
  return (lb->first && lb->first == lb->last);
}
LIB_INLINE bool lib_list_is_empty(const struct List *lb)
{
  return (lb->first == (void *)0);
}
LIB_INLINE void lib_list_clear(struct List *lb)
{
  lb->first = lb->last = (void *)0;
}

/* Equality check for List
 * This only shallowly compares the ListBase itself (so the first/last
 * ptrs), and does not do any equality checks on the list items. */
LIB_INLINE bool lib_list_equal(const struct List *a, const struct List *b)
{
  if (a == NULL) {
    return b == NULL;
  }
  if (b == NULL) {
    return false;
  }
  return a->first == b->first && a->last == b->last;
}

/* Create a generic list node containing link to provided data. */
struct LinkData *lib_genericNodeN(void *data);

/* Does a full loop on the list, with any value acting as first
 * (handy for cycling items)
 *
 * code{.c}
 *
 * LIST_CIRCULAR_FORWARD_BEGIN(list, item, item_init)
 * {
 *     ...operate on marker...
 * }
 * LIST_CIRCULAR_FORWARD_END (list, item, item_init);
 *
 * endcode */
#define LIST_CIRCULAR_FORWARD_BEGIN(lb, lb_iter, lb_init) \
  if ((lb)->first && (lb_init || (lb_init = (lb)->first))) { \
    lb_iter = lb_init; \
    do {
#define LIST_CIRCULAR_FORWARD_END(lb, lb_iter, lb_init) \
  } \
  while ((lb_iter = (lb_iter)->next ? (lb_iter)->next : (lb)->first), (lb_iter != lb_init)) \
    ; \
  } \
  ((void)0)

#define LIST_CIRCULAR_BACKWARD_BEGIN(lb, lb_iter, lb_init) \
  if ((lb)->last && (lb_init || (lb_init = (lb)->last))) { \
    lb_iter = lb_init; \
    do {
#define LIST_CIRCULAR_BACKWARD_END(lb, lb_iter, lb_init) \
  } \
  while ((lb_iter = (lb_iter)->prev ? (lb_iter)->prev : (lb)->last), (lb_iter != lb_init)) \
    ; \
  } \
  ((void)0)

#define LIST_FOREACH(type, var, list) \
  for (type var = (type)((list)->first); var != NULL; var = (type)(((Link *)(var))->next))

/* A version of LIST_FOREACH that supports incrementing an index variable at every step.
 * Including this in the macro helps prevent mistakes where "continue" mistakenly skips the
 * incrementation */
#define LIST_FOREACH_INDEX(type, var, list, index_var) \
  for (type var = (((void)(index_var = 0)), (type)((list)->first)); var != NULL; \
       var = (type)(((Link *)(var))->next), index_var++)

#define LIST_FOREACH_BACKWARD(type, var, list) \
  for (type var = (type)((list)->last); var != NULL; var = (type)(((Link *)(var))->prev))

/* A version of LIST_FOREACH that supports removing the item we're looping over */
#define LIST_FOREACH_MUTABLE(type, var, list) \
  for (type var = (type)((list)->first), *var##_iter_next; \
       ((var != NULL) ? ((void)(var##_iter_next = (type)(((Link *)(var))->next)), 1) : 0); \
       var = var##_iter_next)

/* A version of LISTBASE_FOREACH_BACKWARD that supports removing the item we're looping over. */
#define LISTBASE_FOREACH_BACKWARD_MUTABLE(type, var, list) \
  for (type var = (type)((list)->last), *var##_iter_prev; \
       ((var != NULL) ? ((void)(var##_iter_prev = (type)(((Link *)(var))->prev)), 1) : 0); \
       var = var##_iter_prev)
