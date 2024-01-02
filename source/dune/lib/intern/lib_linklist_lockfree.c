#include "mem_guardedalloc.h"

#include "lib_linklist_lockfree.h"
#include "lib_strict_flags.h"

#include "atomic_ops.h"

void lib_linklist_lockfree_init(LockfreeLinkList *list)
{
  list->dummy_node.next = NULL;
  list->head = list->tail = &list->dummy_node;
}

void lib_linklist_lockfree_free(LockfreeLinkList *list, LockfreeeLinkNodeFreeFP free_fn)
{
  if (free_fn != NULL) {
    /* We start from a first user-added node. */
    LockfreeLinkNode *node = list->head->next;
    while (node != NULL) {
      LockfreeLinkNode *node_next = node->next;
      free_fn(node);
      node = node_next;
    }
  }
}

void lib_linklist_lockfree_clear(LockfreeLinkList *list, LockfreeeLinkNodeFreeFP free_fn)
{
  lib_linklist_lockfree_free(list, free_fn);
  lib_linklist_lockfree_init(list);
}

void lib_linklist_lockfree_insert(LockfreeLinkList *list, LockfreeLinkNode *node)
{
  /* Based on:
   * John D. Valois
   * Implementing Lock-Free Queues
   * http://people.csail.mit.edu/bushl2/rpi/portfolio/lockfree-grape/documents/lock-free-linked-lists.pdf  */
  bool keep_working;
  LockfreeLinkNode *tail_node;
  node->next = NULL;
  do {
    tail_node = list->tail;
    keep_working = (atomic_cas_ptr((void **)&tail_node->next, NULL, node) != NULL);
    if (keep_working) {
      atomic_cas_ptr((void **)&list->tail, tail_node, tail_node->next);
    }
  } while (keep_working);
  atomic_cas_ptr((void **)&list->tail, tail_node, node);
}

LockfreeLinkNode *lib_linklist_lockfree_begin(LockfreeLinkList *list)
{
  return list->head->next;
}
