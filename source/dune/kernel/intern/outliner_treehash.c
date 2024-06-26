/** Tree hash for the outliner space. **/

#include <stdlib.h>
#include <string.h>

#include "LIB_ghash.h"
#include "LIB_mempool.h"
#include "LIB_utildefines.h"

#include "TYPES_outliner.h"

#include "DUNE_outliner_treehash.h"

#include "MEM_guardedalloc.h"

typedef struct TseGroup {
  TreeStoreElem **elems;
  int lastused;
  int size;
  int allocated;
} TseGroup;

/* Allocate structure for TreeStoreElements;
 * Most of elements in treestore have no duplicates,
 * so there is no need to preallocate memory for more than one pointer */
static TseGroup *tse_group_create(void)
{
  TseGroup *tse_group = MEM_mallocN(sizeof(TseGroup), "TseGroup");
  tse_group->elems = MEM_mallocN(sizeof(TreeStoreElem *), "TseGroupElems");
  tse_group->size = 0;
  tse_group->allocated = 1;
  tse_group->lastused = 0;
  return tse_group;
}

static void tse_group_add_element(TseGroup *tse_group, TreeStoreElem *elem)
{
  if (UNLIKELY(tse_group->size == tse_group->allocated)) {
    tse_group->allocated *= 2;
    tse_group->elems = MEM_reallocN(tse_group->elems,
                                    sizeof(TreeStoreElem *) * tse_group->allocated);
  }
  tse_group->elems[tse_group->size] = elem;
  tse_group->size++;
}

static void tse_group_remove_element(TseGroup *tse_group, TreeStoreElem *elem)
{
  int min_allocated = MAX2(1, tse_group->allocated / 2);
  LIB_assert(tse_group->allocated == 1 || (tse_group->allocated % 2) == 0);

  tse_group->size--;
  BLI_assert(tse_group->size >= 0);
  for (int i = 0; i < tse_group->size; i++) {
    if (tse_group->elems[i] == elem) {
      memcpy(tse_group->elems[i],
             tse_group->elems[i + 1],
             (tse_group->size - (i + 1)) * sizeof(TreeStoreElem *));
      break;
    }
  }

  if (UNLIKELY(tse_group->size > 0 && tse_group->size <= min_allocated)) {
    tse_group->allocated = min_allocated;
    tse_group->elems = MEM_reallocN(tse_group->elems,
                                    sizeof(TreeStoreElem *) * tse_group->allocated);
  }
}

static void tse_group_free(TseGroup *tse_group)
{
  MEM_freeN(tse_group->elems);
  MEM_freeN(tse_group);
}

static unsigned int tse_hash(const void *ptr)
{
  const TreeStoreElem *tse = ptr;
  union {
    short h_pair[2];
    unsigned int u_int;
  } hash;

  LIB_assert((tse->type != TSE_SOME_ID) || !tse->nr);

  hash.h_pair[0] = tse->type;
  hash.h_pair[1] = tse->nr;

  hash.u_int ^= LIB_ghashutil_ptrhash(tse->id);

  return hash.u_int;
}

static bool tse_cmp(const void *a, const void *b)
{
  const TreeStoreElem *tse_a = a;
  const TreeStoreElem *tse_b = b;
  return tse_a->type != tse_b->type || tse_a->nr != tse_b->nr || tse_a->id != tse_b->id;
}

static void fill_treehash(void *treehash, BLI_mempool *treestore)
{
  TreeStoreElem *tselem;
  LIB_mempool_iter iter;
  LIB_mempool_iternew(treestore, &iter);

  LIB_assert(treehash);

  while ((tselem = LIB_mempool_iterstep(&iter))) {
    DUNE_outliner_treehash_add_element(treehash, tselem);
  }
}

void *DUNE_outliner_treehash_create_from_treestore(LIB_mempool *treestore)
{
  GHash *treehash = LIB_ghash_new_ex(tse_hash, tse_cmp, "treehash", LIB_mempool_len(treestore));
  fill_treehash(treehash, treestore);
  return treehash;
}

static void free_treehash_group(void *key)
{
  tse_group_free(key);
}

void DUNE_outliner_treehash_clear_used(void *treehash)
{
  GHashIterator gh_iter;

  GHASH_ITER (gh_iter, treehash) {
    TseGroup *group = LIB_ghashIterator_getValue(&gh_iter);
    group->lastused = 0;
  }
}

void *DUNE_outliner_treehash_rebuild_from_treestore(void *treehash, LIB_mempool *treestore)
{
  LIB_assert(treehash);

  LIB_ghash_clear_ex(treehash, NULL, free_treehash_group, LIB_mempool_len(treestore));
  fill_treehash(treehash, treestore);
  return treehash;
}

void DUNE_outliner_treehash_add_element(void *treehash, TreeStoreElem *elem)
{
  TseGroup *group;
  void **val_p;

  if (!LIB_ghash_ensure_p(treehash, elem, &val_p)) {
    *val_p = tse_group_create();
  }
  group = *val_p;
  group->lastused = 0;
  tse_group_add_element(group, elem);
}

void DUNE_outliner_treehash_remove_element(void *treehash, TreeStoreElem *elem)
{
  TseGroup *group = LIB_ghash_lookup(treehash, elem);

  LIB_assert(group != NULL);
  if (group->size <= 1) {
    /* one element -> remove group completely */
    LIB_ghash_remove(treehash, elem, NULL, free_treehash_group);
  }
  else {
    tse_group_remove_element(group, elem);
  }
}

static TseGroup *DUNE_outliner_treehash_lookup_group(GHash *th, short type, short nr, struct ID *id)
{
  TreeStoreElem tse_template;
  tse_template.type = type;
  tse_template.nr = (type == TSE_SOME_ID) ? 0 : nr; /* we're picky! :) */
  tse_template.id = id;

  LIB_assert(th);

  return LIB_ghash_lookup(th, &tse_template);
}

TreeStoreElem *DUNE_outliner_treehash_lookup_unused(void *treehash,
                                                   short type,
                                                   short nr,
                                                   struct ID *id)
{
  TseGroup *group;

  LIB_assert(treehash);

  group = DUNE_outliner_treehash_lookup_group(treehash, type, nr, id);
  if (group) {
    /* Find unused element, with optimization to start from previously
     * found element assuming we do repeated lookups. */
    int size = group->size;
    int offset = group->lastused;

    for (int i = 0; i < size; i++, offset++) {
      if (offset >= size) {
        offset = 0;
      }

      if (!group->elems[offset]->used) {
        group->lastused = offset;
        return group->elems[offset];
      }
    }
  }
  return NULL;
}

TreeStoreElem *DUNE_outliner_treehash_lookup_any(void *treehash,
                                                short type,
                                                short nr,
                                                struct ID *id)
{
  TseGroup *group;

  LIB_assert(treehash);

  group = DUNE_outliner_treehash_lookup_group(treehash, type, nr, id);
  return group ? group->elems[0] : NULL;
}

void DUNE_outliner_treehash_free(void *treehash)
{
  LIB_assert(treehash);

  LIB_ghash_free(treehash, NULL, free_treehash_group);
}
