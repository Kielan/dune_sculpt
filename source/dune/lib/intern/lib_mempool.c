/* Simple, fast mem allocator for alloc many elems of the same size.
 * Supports:
 * - Freeing chunks.
 * - Iter over alloc chunks
 *   (optionally when using the BLIB_MEMPOOL_ALLOW_ITER flag). */
#include <stdlib.h>
#include <string.h>

#include "atomic_ops.h"

#include "lib_utildefines.h"

#include "lib_asan.h"
#include "lib_mempool.h"         /* own include */
#include "lib_mempool_private.h" /* own include */

#ifdef WITH_ASAN
#  include "lib_threads.h"
#endif

#include "mem_guardedalloc.h"

#include "lib_strict_flags.h" /* keep last */

#ifdef WITH_MEM_VALGRIND
#  include "valgrind/memcheck.h"
#endif

#ifdef WITH_ASAN
#  define POISON_REDZONE_SIZE 32
#else
#  define POISON_REDZONE_SIZE 0
#endif

/* Copied from loader_dune_defs.hh, don't use here bc we're in lib. */
#ifdef __BIG_ENDIAN__
/* Big Endian */
#  define MAKE_ID(a, b, c, d) ((int)(a) << 24 | (int)(b) << 16 | (c) << 8 | (d))
#  define MAKE_ID_8(a, b, c, d, e, f, g, h) \
    ((int64_t)(a) << 56 | (int64_t)(b) << 48 | (int64_t)(c) << 40 | (int64_t)(d) << 32 | \
     (int64_t)(e) << 24 | (int64_t)(f) << 16 | (int64_t)(g) << 8 | (h))
#else
/* Little Endian */
#  define MAKE_ID(a, b, c, d) ((int)(d) << 24 | (int)(c) << 16 | (b) << 8 | (a))
#  define MAKE_ID_8(a, b, c, d, e, f, g, h) \
    ((int64_t)(h) << 56 | (int64_t)(g) << 48 | (int64_t)(f) << 40 | (int64_t)(e) << 32 | \
     (int64_t)(d) << 24 | (int64_t)(c) << 16 | (int64_t)(b) << 8 | (a))
#endif

/* Important that this val is an is _not_  aligned with `sizeof(void *)`.
 * Having a ptr to 2/4/8... aligned mem is enough to ensure
 * the `freeword` will never be used.
 * To be safe, use a word that's the same in both directions. */
#define FREEWORD \
  ((sizeof(void *) > sizeof(int32_t)) ? MAKE_ID_8('e', 'e', 'r', 'f', 'f', 'r', 'e', 'e') : \
                                        MAKE_ID('e', 'f', 'f', 'e'))

/* The 'used' word just needs to be set to something besides FREEWORD. */
#define USEDWORD MAKE_ID('u', 's', 'e', 'd')

/* Currently totalloc isn't used. */
// #define USE_TOTALLOC

/* optimize pool size */
#define USE_CHUNK_POW2

#ifndef NDEBUG
static bool mempool_debug_memset = false;
#endif

/* A free elem from lib_mempool_chunk. Data is cast to this type and stored in
 * lib_mempool.free as a single linked list, each item lib_mempool.esize large.
 * Each element represents a block which lib_mempool_alloc may return. */
typedef struct LibFreeNode {
  struct LibFreeNode *next;
  /* Used to id this as a freed node. */
  intptr_t freeword;
} LibFreeNode;

/* A chunk of mem in the mempool stored in
 * lib_mempool.chunks as a double linked list. */
typedef struct LibMempoolChunk {
  struct LibMempoolChunk *next;
} LibMempoolChunk;

/* The mempool, stores and tracks mem chunks and elements within those chunks free. */
struct LibMempool {
#ifdef WITH_ASAN
  /* Serialize access to mem-pools when debugging with ASAN. */
  ThreadMutex mutex;
#endif
  /* Single linked list of alloc chunks. */
  LibMempoolChunk *chunks;
  /* Keep a ptr to the last, so we can append new chunks there
   * this is needed for iteration so we can loop over chunks in the order added. */
  LibMempoolChunk *chunk_tail;

  /* Ele size in bytes. */
  uint esize;
  /* Chunk size in bytes. */
  uint csize;
  /* Num of elems per chunk. */
  uint pchunk;
  uint flag;

  /* Free elem list. Interleaved into chunk data. */
  LibFreenode *free;
  /* Use to know how many chunks to keep for #BLI_mempool_clear. */
  uint maxchunks;
  /* Num of elems currently in use. */
  uint totused;
#ifdef USE_TOTALLOC
  /* Num of elems alloc in total. */
  uint totalloc;
#endif
};

#define MEMPOOL_ELEM_SIZE_MIN (sizeof(void *) * 2)

#define CHUNK_DATA(chunk) (CHECK_TYPE_INLINE(chunk, BLI_mempool_chunk *), (void *)((chunk) + 1))

#define NODE_STEP_NEXT(node) ((void *)((char *)(node) + esize))
#define NODE_STEP_PREV(node) ((void *)((char *)(node)-esize))

/* Extra bytes implicitly used for every chunk alloc. */
#define CHUNK_OVERHEAD (uint)(MEM_SIZE_OVERHEAD + sizeof(LibMempoolChunk))

static void mempool_asan_unlock(LibMempool *pool)
{
#ifdef WITH_ASAN
  lib_mutex_unlock(&pool->mutex);
#else
  UNUSED_VARS(pool);
#endif
}

static void mempool_asan_lock(LibMempool *pool)
{
#ifdef WITH_ASAN
  lib_mutex_lock(&pool->mutex);
#else
  UNUSED_VARS(pool);
#endif
}

#ifdef USE_CHUNK_POW2
static uint power_of_2_max_u(uint x)
{
  x -= 1;
  x = x | (x >> 1);
  x = x | (x >> 2);
  x = x | (x >> 4);
  x = x | (x >> 8);
  x = x | (x >> 16);
  return x + 1;
}
#endif

LIB_INLINE LibMempoolChunk *mempool_chunk_find(LibMempoolChunk *head, uint index)
{
  while (index-- && head) {
    head = head->next;
  }
  return head;
}

/* return the num of chunks to alloc based on how many elems are needed.
 * For small pools 1 is a good default, the elems need to be init,
 * adding overhead on creation which is redundant if they aren't used. */
LIB_INLINE uint mempool_maxchunks(const uint elem_num, const uint pchunk)
{
  return (elem_num <= pchunk) ? 1 : ((elem_num / pchunk) + 1);
}

static LibMempoolChunk *mempool_chunk_alloc(LibMempool *pool)
{
  return mem_malloc(sizeof(LibMempoolChunk) + (size_t)pool->csize, "mempool chunk");
}

/* Init a chunk and add into pool->chunks
 *
 * param pool: The pool to add the chunk into.
 * param mpchunk: The new uninit chunk (can be malloc'd)
 * param last_tail: The last elem of the prev chunk
 * (used when building free chunks initially)
 * return The last chunk, */
static LibFreenode *mempool_chunk_add(LibMempool *pool,
                                      LibMempoolChunk *mpchunk,
                                      LibFreenode *last_tail)
{
  const uint esize = pool->esize;
  LibFreenode *curnode = CHUNK_DATA(mpchunk);
  uint j;

  /* append */
  if (pool->chunk_tail) {
    pool->chunk_tail->next = mpchunk;
  }
  else {
    lib_assert(pool->chunks == NULL);
    pool->chunks = mpchunk;
  }

  mpchunk->next = NULL;
  pool->chunk_tail = mpchunk;

  if (UNLIKELY(pool->free == NULL)) {
    pool->free = curnode;
  }

  /* loop through alloc'd data, building the ptr structs */
  j = pool->pchunk;
  if (pool->flag & LIB_MEMPOOL_ALLOW_ITER) {
    while (j--) {
      LibFreenode *next;

      lib_asan_unpoison(curnode, pool->esize - POISON_REDZONE_SIZE);
#ifdef WITH_MEM_VALGRIND
      VALGRIND_MAKE_MEM_DEFINED(curnode, pool->esize - POISON_REDZONE_SIZE);
#endif
      curnode->next = next = NODE_STEP_NEXT(curnode);
      curnode->freeword = FREEWORD;

      lib_asan_poison(curnode, pool->esize);
#ifdef WITH_MEM_VALGRIND
      VALGRIND_MAKE_MEM_UNDEFINED(curnode, pool->esize);
#endif
      curnode = next;
    }
  }
  else {
    while (j--) {
      LibFreenode *next;

      lib_asan_unpoison(curnode, pool->esize - POISON_REDZONE_SIZE);
#ifdef WITH_MEM_VALGRIND
      VALGRIND_MAKE_MEM_DEFINED(curnode, pool->esize - POISON_REDZONE_SIZE);
#endif
      curnode->next = next = NODE_STEP_NEXT(curnode);
      lib_asan_poison(curnode, pool->esize);
#ifdef WITH_MEM_VALGRIND
      VALGRIND_MAKE_MEM_UNDEFINED(curnode, pool->esize);
#endif

      curnode = next;
    }
  }

  /* terminate the list (rewind one)
   * will be overwritten if 'curnode' gets passed in again as 'last_tail' */
  if (POISON_REDZONE_SIZE > 0) {
    lib_asan_unpoison(curnode, pool->esize - POISON_REDZONE_SIZE);
    lib_asan_poison(curnode, pool->esize);
#ifdef WITH_MEM_VALGRIND
    VALGRIND_MAKE_MEM_DEFINED(curnode, pool->esize - POISON_REDZONE_SIZE);
    VALGRIND_MAKE_MEM_UNDEFINED(curnode, pool->esize);
#endif
  }

  curnode = NODE_STEP_PREV(curnode);

  lib_asan_unpoison(curnode, pool->esize - POISON_REDZONE_SIZE);
#ifdef WITH_MEM_VALGRIND
  VALGRIND_MAKE_MEM_DEFINED(curnode, pool->esize - POISON_REDZONE_SIZE);
#endif

  curnode->next = NULL;
  lib_asan_poison(curnode, pool->esize);
#ifdef WITH_MEM_VALGRIND
  VALGRIND_MAKE_MEM_UNDEFINED(curnode, pool->esize);
#endif

#ifdef USE_TOTALLOC
  pool->totalloc += pool->pchunk;
#endif

  /* final ptr in the prev alloc chunk is wrong */
  if (last_tail) {
    lib_asan_unpoison(last_tail, pool->esize - POISON_REDZONE_SIZE);
#ifdef WITH_MEM_VALGRIND
    VALGRIND_MAKE_MEM_DEFINED(last_tail, pool->esize - POISON_REDZONE_SIZE);
#endif
    last_tail->next = CHUNK_DATA(mpchunk);
    lib_asan_poison(last_tail, pool->esize);
#ifdef WITH_MEM_VALGRIND
    VALGRIND_MAKE_MEM_UNDEFINED(last_tail, pool->esize);
#endif
  }

  return curnode;
}

static void mempool_chunk_free(LibMempoolChunk *mpchunk, LibMempool *pool)
{
#ifdef WITH_ASAN
  lib_asan_unpoison(mpchunk, sizeof(LibMempoolChunk) + pool->esize * pool->csize);
#else
  UNUSED_VARS(pool);
#endif
#ifdef WITH_MEM_VALGRIND
  VALGRIND_MAKE_MEM_DEFINED(mpchunk, sizeof(BLI_mempool_chunk) + pool->esize * pool->csize);
#endif
  mem_freeNmpchunk);
}

static void mempool_chunk_free_all(LibMempoolChunk *mpchunk, LibMempool *pool)
{
  LibMempoolChunk *mpchunk_next;

  for (; mpchunk; mpchunk = mpchunk_next) {
    mpchunk_next = mpchunk->next;
    mempool_chunk_free(mpchunk, pool);
  }
}

LibMempool *lib_mempool_create(uint esize, uint elem_num, uint pchunk, uint flag)
{
  LibMempool *pool;
  LibFreenode *last_tail = NULL;
  uint i, maxchunks;

  /* alloc the pool struct */
  pool = mem_malloc(sizeof(LibMempool), "memory pool");

#ifdef WITH_ASAN
  lib_mutex_init(&pool->mutex);
#endif

  /* set the elem size */
  if (esize < (int)MEMPOOL_ELEM_SIZE_MIN) {
    esize = (int)MEMPOOL_ELEM_SIZE_MIN;
  }

  if (flag & LIB_MEMPOOL_ALLOW_ITER) {
    esize = MAX2(esize, (uint)sizeof(LibFreenode));
  }

  esize += POISON_REDZONE_SIZE;

  maxchunks = mempool_maxchunks(elem_num, pchunk);

  pool->chunks = NULL;
  pool->chunk_tail = NULL;
  pool->esize = esize;

  /* Optimize chunk size to powers of 2, accounting for slop-space. */
#ifdef USE_CHUNK_POW2
  {
    lib_assert(power_of_2_max_u(pchunk * esize) > CHUNK_OVERHEAD);
    pchunk = (power_of_2_max_u(pchunk * esize) - CHUNK_OVERHEAD) / esize;
  }
#endif

  pool->csize = esize * pchunk;

  /* Ensure this is a power of 2, minus the rounding by element size. */
#if defined(USE_CHUNK_POW2) && !defined(NDEBUG)
  {
    uint final_size = (uint)MEM_SIZE_OVERHEAD + (uint)sizeof(lib_mempool_chunk) + pool->csize;
    lib_assert(((uint)power_of_2_max_u(final_size) - final_size) < pool->esize);
  }
#endif

  pool->pchunk = pchunk;
  pool->flag = flag;
  pool->free = NULL; /* mempool_chunk_add assigns */
  pool->maxchunks = maxchunks;
#ifdef USE_TOTALLOC
  pool->totalloc = 0;
#endif
  pool->totused = 0;

  if (elem_num) {
    /* Alloc the actual chunks. */
    for (i = 0; i < maxchunks; i++) {
      LibMempoolChunk *mpchunk = mempool_chunk_alloc(pool);
      last_tail = mempool_chunk_add(pool, mpchunk, last_tail);
    }
  }

#ifdef WITH_MEM_VALGRIND
  VALGRIND_CREATE_MEMPOOL(pool, 0, false);
#endif

  return pool;
}

void *lib_mempool_alloc(LibMempool *pool)
{
  LibFreenode *free_pop;

  if (UNLIKELY(pool->free == NULL)) {
    /* Need to alloc a new chunk. */
    LibMempoolChunk *mpchunk = mempool_chunk_alloc(pool);
    mempool_chunk_add(pool, mpchunk, NULL);
  }

  free_pop = pool->free;

  lib_asan_unpoison(free_pop, pool->esize - POISON_REDZONE_SIZE);
#ifdef WITH_MEM_VALGRIND
  VALGRIND_MEMPOOL_ALLOC(pool, free_pop, pool->esize - POISON_REDZONE_SIZE);
  /* Mark as define, then undefine immediately before returning so:
   * - `free_pop->next` can be read without reading "undefined" mem.
   * - `freeword` can be set wo causing the mem to be considered "defined".
   *
   * These could be handled on a more granular level - dealing with defining & underlining these
   * members explicitly but that requires more involved calls,
   * adding overhead for no real benefit. */
  VALGRIND_MAKE_MEM_DEFINED(free_pop, pool->esize - POISON_REDZONE_SIZE);
#endif

  lib_assert(pool->chunk_tail->next == NULL);

  if (pool->flag & LIB_MEMPOOL_ALLOW_ITER) {
    free_pop->freeword = USEDWORD;
  }

  pool->free = free_pop->next;
  pool->totused++;

#ifdef WITH_MEM_VALGRIND
  VALGRIND_MAKE_MEM_UNDEFINED(free_pop, pool->esize - POISON_REDZONE_SIZE);
#endif

  return (void *)free_pop;
}

void *lib_mempool_calloc(LibMempool *pool)
{
  void *retval = lib_mempool_alloc(pool);

  memset(retval, 0, (size_t)pool->esize - POISON_REDZONE_SIZE);

  return retval;
}

/* Free an elem from the mempool.
 * doesn't protect against double frees, take care! */
void lib_mempool_free(LibMempool *pool, void *addr)
{
  LibFreenode *newhead = addr;

#ifndef NDEBUG
  {
    LibMempool_chunk *chunk;
    bool found = false;
    for (chunk = pool->chunks; chunk; chunk = chunk->next) {
      if (ARRAY_HAS_ITEM((char *)addr, (char *)CHUNK_DATA(chunk), pool->csize)) {
        found = true;
        break;
      }
    }
    if (!found) {
      lib_assert_msg(0, "Attempt to free data which is not in pool.\n");
    }
  }

  /* Enable for debugging. */
  if (UNLIKELY(mempool_debug_memset)) {
    memset(addr, 255, pool->esize - POISON_REDZONE_SIZE);
  }
#endif

  if (pool->flag & LIB_MEMPOOL_ALLOW_ITER) {
#ifndef NDEBUG
    /* This will detect double free's. */
    lib_assert(newhead->freeword != FREEWORD);
#endif
    newhead->freeword = FREEWORD;
  }

  newhead->next = pool->free;
  pool->free = newhead;

  lib_asan_poison(newhead, pool->esize);

  pool->totused--;

#ifdef WITH_MEM_VALGRIND
  VALGRIND_MEMPOOL_FREE(pool, addr);
#endif

  /* Nothing is in use; free all the chunks except the first. */
  if (UNLIKELY(pool->totused == 0) && (pool->chunks->next)) {
    const uint esize = pool->esize;
    LibFreenode *curnode;
    uint j;
    LibMempoolChunk *first;

    first = pool->chunks;
    mempool_chunk_free_all(first->next, pool);
    first->next = NULL;
    pool->chunk_tail = first;

#ifdef USE_TOTALLOC
    pool->totalloc = pool->pchunk;
#endif

    /* Tmp alloc so VALGRIND doesn't complain when setting freed blocks 'next'. */
#ifdef WITH_MEM_VALGRIND
    VALGRIND_MEMPOOL_ALLOC(pool, CHUNK_DATA(first), pool->csize);
#endif

    curnode = CHUNK_DATA(first);
    pool->free = curnode;

    j = pool->pchunk;
    while (j--) {
      lib_asan_unpoison(curnode, pool->esize - POISON_REDZONE_SIZE);
      LibFreenode *next = curnode->next = NODE_STEP_NEXT(curnode);
      lib_asan_poison(curnode, pool->esize);
      curnode = next;
    }

    lib_asan_unpoison(curnode, pool->esize - POISON_REDZONE_SIZE);
    LibFreenode *prev = NODE_STEP_PREV(curnode);
    lib_asan_poison(curnode, pool->esize);

    curnode = prev;

    lib_asan_unpoison(curnode, pool->esize - POISON_REDZONE_SIZE);
    curnode->next = NULL; /* terminate the list */
    lib_asan_poison(curnode, pool->esize);

#ifdef WITH_MEM_VALGRIND
    VALGRIND_MEMPOOL_FREE(pool, CHUNK_DATA(first));
#endif
  }
}

int lib_mempool_len(const LibMempool *pool)
{
  int ret = (int)pool->totused;

  return ret;
}

void *lib_mempool_findelem(LibMempool *pool, uint index)
{
  mempool_asan_lock(pool);

  lib_assert(pool->flag & LIB_MEMPOOL_ALLOW_ITER);

  if (index < (uint)pool->totused) {
    /* We could have some faster mem chunk stepping code inline. */
    LibMempoolIter iter;
    void *elem;
    lib_mempool_iternew(pool, &iter);
    for (elem = lib_mempool_iterstep(&iter); index-- != 0; elem = lib_mempool_iterstep(&iter)) {
      /* pass */
    }

    mempool_asan_unlock(pool);
    return elem;
  }

  mempool_asan_unlock(pool);
  return NULL;
}

void lib_mempool_as_table(LibMempool *pool, void **data)
{
  LibMempool_iter iter;
  void *elem;
  void **p = data;

  lib_assert(pool->flag & LIB_MEMPOOL_ALLOW_ITER);

  lib_mempool_iternew(pool, &iter);

  while ((elem = lib_mempool_iterstep(&iter))) {
    *p++ = elem;
  }

  lib_assert((ptrdiff_t)(p - data) == (ptrdiff_t)pool->totused);
}

void **lib_mempool_as_tableN(LibMempool *pool, const char *allocstr)
{
  void **data = mem_malloc((size_t)pool->totused * sizeof(void *), allocstr);
  lib_mempool_as_table(pool, data);
  return data;
}

void lib_mempool_as_array(LibMempool *pool, void *data)
{
  const uint esize = pool->esize - (uint)POISON_REDZONE_SIZE;
  LibMempoolIter iter;
  char *elem, *p = data;

  BLI_assert(pool->flag & BLI_MEMPOOL_ALLOW_ITER);

  mempool_asan_lock(pool);
  BLI_mempool_iternew(pool, &iter);
  while ((elem = BLI_mempool_iterstep(&iter))) {
    memcpy(p, elem, (size_t)esize);
    p = NODE_STEP_NEXT(p);
  }
  mempool_asan_unlock(pool);
}

void *BLI_mempool_as_arrayN(BLI_mempool *pool, const char *allocstr)
{
  char *data = MEM_malloc_arrayN((size_t)pool->totused, pool->esize, allocstr);
  BLI_mempool_as_array(pool, data);
  return data;
}

void BLI_mempool_iternew(BLI_mempool *pool, BLI_mempool_iter *iter)
{
  BLI_assert(pool->flag & BLI_MEMPOOL_ALLOW_ITER);

  iter->pool = pool;
  iter->curchunk = pool->chunks;
  iter->curindex = 0;
}

static void mempool_threadsafe_iternew(BLI_mempool *pool, BLI_mempool_threadsafe_iter *ts_iter)
{
  BLI_mempool_iternew(pool, &ts_iter->iter);
  ts_iter->curchunk_threaded_shared = NULL;
}

ParallelMempoolTaskData *mempool_iter_threadsafe_create(BLI_mempool *pool, const size_t iter_num)
{
  BLI_assert(pool->flag & BLI_MEMPOOL_ALLOW_ITER);

  ParallelMempoolTaskData *iter_arr = MEM_mallocN(sizeof(*iter_arr) * iter_num, __func__);
  BLI_mempool_chunk **curchunk_threaded_shared = MEM_mallocN(sizeof(void *), __func__);

  mempool_threadsafe_iternew(pool, &iter_arr->ts_iter);

  *curchunk_threaded_shared = iter_arr->ts_iter.iter.curchunk;
  iter_arr->ts_iter.curchunk_threaded_shared = curchunk_threaded_shared;
  for (size_t i = 1; i < iter_num; i++) {
    iter_arr[i].ts_iter = iter_arr[0].ts_iter;
    *curchunk_threaded_shared = iter_arr[i].ts_iter.iter.curchunk =
        ((*curchunk_threaded_shared) ? (*curchunk_threaded_shared)->next : NULL);
  }

  return iter_arr;
}

void mempool_iter_threadsafe_destroy(ParallelMempoolTaskData *iter_arr)
{
  BLI_assert(iter_arr->ts_iter.curchunk_threaded_shared != NULL);

  MEM_freeN(iter_arr->ts_iter.curchunk_threaded_shared);
  MEM_freeN(iter_arr);
}

#if 0
/* unoptimized, more readable */

static void *bli_mempool_iternext(BLI_mempool_iter *iter)
{
  void *ret = NULL;

  if (iter->curchunk == NULL || !iter->pool->totused) {
    return ret;
  }

  ret = ((char *)CHUNK_DATA(iter->curchunk)) + (iter->pool->esize * iter->curindex);

  iter->curindex++;

  if (iter->curindex == iter->pool->pchunk) {
    iter->curindex = 0;
    iter->curchunk = iter->curchunk->next;
  }

  return ret;
}

void *BLI_mempool_iterstep(BLI_mempool_iter *iter)
{
  BLI_freenode *ret;

  do {
    ret = bli_mempool_iternext(iter);
  } while (ret && ret->freeword == FREEWORD);

  return ret;
}

#else /* Optimized version of code above. */

void *BLI_mempool_iterstep(BLI_mempool_iter *iter)
{
  if (UNLIKELY(iter->curchunk == NULL)) {
    return NULL;
  }

  const uint esize = iter->pool->esize;
  BLI_freenode *curnode = POINTER_OFFSET(CHUNK_DATA(iter->curchunk), (esize * iter->curindex));
  BLI_freenode *ret;
  do {
    ret = curnode;

    BLI_asan_unpoison(ret, iter->pool->esize - POISON_REDZONE_SIZE);
#  ifdef WITH_MEM_VALGRIND
    VALGRIND_MAKE_MEM_DEFINED(ret, iter->pool->esize - POISON_REDZONE_SIZE);
#  endif

    if (++iter->curindex != iter->pool->pchunk) {
      curnode = POINTER_OFFSET(curnode, esize);
    }
    else {
      iter->curindex = 0;
      iter->curchunk = iter->curchunk->next;
      if (UNLIKELY(iter->curchunk == NULL)) {
        BLI_asan_unpoison(ret, iter->pool->esize - POISON_REDZONE_SIZE);
#  ifdef WITH_MEM_VALGRIND
        VALGRIND_MAKE_MEM_DEFINED(ret, iter->pool->esize - POISON_REDZONE_SIZE);
#  endif
        void *ret2 = (ret->freeword == FREEWORD) ? NULL : ret;

        if (ret->freeword == FREEWORD) {
          BLI_asan_poison(ret, iter->pool->esize);
#  ifdef WITH_MEM_VALGRIND
          VALGRIND_MAKE_MEM_UNDEFINED(ret, iter->pool->esize);
#  endif
        }

        return ret2;
      }
      curnode = CHUNK_DATA(iter->curchunk);
    }
  } while (ret->freeword == FREEWORD);

  return ret;
}

void *mempool_iter_threadsafe_step(BLI_mempool_threadsafe_iter *ts_iter)
{
  BLI_mempool_iter *iter = &ts_iter->iter;
  if (UNLIKELY(iter->curchunk == NULL)) {
    return NULL;
  }

  mempool_asan_lock(iter->pool);

  const uint esize = iter->pool->esize;
  BLI_freenode *curnode = POINTER_OFFSET(CHUNK_DATA(iter->curchunk), (esize * iter->curindex));
  BLI_freenode *ret;
  do {
    ret = curnode;

    BLI_asan_unpoison(ret, esize - POISON_REDZONE_SIZE);
#  ifdef WITH_MEM_VALGRIND
    VALGRIND_MAKE_MEM_DEFINED(ret, iter->pool->esize);
#  endif

    if (++iter->curindex != iter->pool->pchunk) {
      curnode = POINTER_OFFSET(curnode, esize);
    }
    else {
      iter->curindex = 0;

      /* Begin unique to the `threadsafe` version of this function. */
      for (iter->curchunk = *ts_iter->curchunk_threaded_shared;
           (iter->curchunk != NULL) && (atomic_cas_ptr((void **)ts_iter->curchunk_threaded_shared,
                                                       iter->curchunk,
                                                       iter->curchunk->next) != iter->curchunk);
           iter->curchunk = *ts_iter->curchunk_threaded_shared)
      {
        /* pass. */
      }
      if (UNLIKELY(iter->curchunk == NULL)) {
        if (ret->freeword == FREEWORD) {
          BLI_asan_poison(ret, esize);
#  ifdef WITH_MEM_VALGRIND
          VALGRIND_MAKE_MEM_UNDEFINED(ret, iter->pool->esize);
#  endif
          mempool_asan_unlock(iter->pool);
          return NULL;
        }
        else {
          mempool_asan_unlock(iter->pool);
          return ret;
        }
      }
      /* End `threadsafe` exception. */

      iter->curchunk = iter->curchunk->next;
      if (UNLIKELY(iter->curchunk == NULL)) {
        if (ret->freeword == FREEWORD) {
          BLI_asan_poison(ret, iter->pool->esize);
#  ifdef WITH_MEM_VALGRIND
          VALGRIND_MAKE_MEM_UNDEFINED(ret, iter->pool->esize);
#  endif
          mempool_asan_unlock(iter->pool);
          return NULL;
        }
        else {
          mempool_asan_unlock(iter->pool);
          return ret;
        }
      }

      curnode = CHUNK_DATA(iter->curchunk);
    }

    if (ret->freeword == FREEWORD) {
      BLI_asan_poison(ret, iter->pool->esize);
#  ifdef WITH_MEM_VALGRIND
      VALGRIND_MAKE_MEM_UNDEFINED(ret, iter->pool->esize);
#  endif
    }
    else {
      break;
    }
  } while (true);

  mempool_asan_unlock(iter->pool);
  return ret;
}

#endif

void lib_mempool_clear_ex(LibMempool *pool, const int elem_num_reserve)
{
  BLI_mempool_chunk *mpchunk;
  BLI_mempool_chunk *mpchunk_next;
  uint maxchunks;

  LibMempool_chunk *chunks_tmp;
  LibFreenode *last_tail = NULL;

#ifdef WITH_MEM_VALGRIND
  VALGRIND_DESTROY_MEMPOOL(pool);
  VALGRIND_CREATE_MEMPOOL(pool, 0, false);
#endif

  if (elem_num_reserve == -1) {
    maxchunks = pool->maxchunks;
  }
  else {
    maxchunks = mempool_maxchunks((uint)elem_num_reserve, pool->pchunk);
  }

  /* Free all after 'pool->maxchunks'. */
  mpchunk = mempool_chunk_find(pool->chunks, maxchunks - 1);
  if (mpchunk && mpchunk->next) {
    /* terminate */
    mpchunk_next = mpchunk->next;
    mpchunk->next = NULL;
    mpchunk = mpchunk_next;

    do {
      mpchunk_next = mpchunk->next;
      mempool_chunk_free(mpchunk, pool);
    } while ((mpchunk = mpchunk_next));
  }

  /* re-init */
  pool->free = NULL;
  pool->totused = 0;
#ifdef USE_TOTALLOC
  pool->totalloc = 0;
#endif

  chunks_tmp = pool->chunks;
  pool->chunks = NULL;
  pool->chunk_tail = NULL;

  while ((mpchunk = chunks_tmp)) {
    chunks_tmp = mpchunk->next;
    last_tail = mempool_chunk_add(pool, mpchunk, last_tail);
  }
}

void lib_mempool_clear(LibMempool *pool)
{
  lib_mempool_clear_ex(pool, -1);
}

void lib_mempool_destroy(LibMempool *pool)
{
  mempool_chunk_free_all(pool->chunks, pool);

#ifdef WITH_MEM_VALGRIND
  VALGRIND_DESTROY_MEMPOOL(pool);
#endif

  mem_free(pool);
}

#ifndef NDEBUG
void lib_mempool_set_mem_debug(void)
{
  mempool_debug_memset = true;
}
#endif
