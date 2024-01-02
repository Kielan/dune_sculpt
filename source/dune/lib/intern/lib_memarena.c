/* Efficient mem alloc for many small chunks.
 * About Mem Arena
 * Mem arena's are commonly used when the program
 * needs to quickly alloc lots of little bits of data,
 * which are all freed at the same moment.
 * - Mem can't be freed during the arena's lifetime. */
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_asan.h"
#include "lib_memarena.h"
#include "lib_strict_flags.h"
#include "lib_utildefines.h"

#ifdef WITH_MEM_VALGRIND
#  include "valgrind/memcheck.h"
#else
#  define VALGRIND_CREATE_MEMPOOL(pool, rzB, is_zeroed) UNUSED_VARS(pool, rzB, is_zeroed)
#  define VALGRIND_DESTROY_MEMPOOL(pool) UNUSED_VARS(pool)
#  define VALGRIND_MEMPOOL_ALLOC(pool, addr, size) UNUSED_VARS(pool, addr, size)
#  define VALGRIND_MOVE_MEMPOOL(pool_a, pool_b) UNUSED_VARS(pool_a, pool_b)
#endif

struct MemBuf {
  struct MemBuf *next;
  uchar data[0];
};

struct MemArena {
  uchar *curbuf;
  const char *name;
  struct MemBuf *bufs;

  size_t bufsize, cursize;
  size_t align;

  bool use_calloc;
};

static void memarena_buf_free_all(struct MemBuf *mb)
{
  while (mb != NULL) {
    struct MemBuf *mb_next = mb->next;

    /* Unpoison mem bc mem_free might overwrite it. */
    lib_asan_unpoison(mb, (uint)mem_alloc_len(mb));

    mem_free(mb);
    mb = mb_next;
  }
}

MemArena *lib_memarena_new(const size_t bufsize, const char *name)
{
  MemArena *ma = mem_calloc(sizeof(*ma), "memarena");
  ma->bufsize = bufsize;
  ma->align = 8;
  ma->name = name;

  VALGRIND_CREATE_MEMPOOL(ma, 0, false);

  return ma;
}

void lib_memarena_use_calloc(MemArena *ma)
{
  ma->use_calloc = 1;
}

void lib_memarena_use_malloc(MemArena *ma)
{
  ma->use_calloc = 0;
}

void lib_memarena_use_align(MemArena *ma, const size_t align)
{
  /* Align must be a power of two. */
  lib_assert((align & (align - 1)) == 0);

  ma->align = align;
}

void lib_memarena_free(MemArena *ma)
{
  memarena_buf_free_all(ma->bufs);

  VALGRIND_DESTROY_MEMPOOL(ma);

  mem_free(ma);
}

/* Pad num up by amt (must be power of two). */
#define PADUP(num, amt) (((num) + ((amt)-1)) & ~((amt)-1))

/* Align alloc'd mem (needed if `align > 8`). */
static void memarena_curbuf_align(MemArena *ma)
{
  uchar *tmp;

  tmp = (uchar *)PADUP((intptr_t)ma->curbuf, (int)ma->align);
  ma->cursize -= (size_t)(tmp - ma->curbuf);
  ma->curbuf = tmp;
}

void *lib_memarena_alloc(MemArena *ma, size_t size)
{
  void *ptr;

  /* Ensure proper alignment by rounding size up to multiple of 8. */
  size = PADUP(size, ma->align);

  if (UNLIKELY(size > ma->cursize)) {
    if (size > ma->bufsize - (ma->align - 1)) {
      ma->cursize = PADUP(size + 1, ma->align);
    }
    else {
      ma->cursize = ma->bufsize;
    }

    struct MemBuf *mb = (ma->use_calloc ? MEM_callocN : MEM_mallocN)(sizeof(*mb) + ma->cursize,
                                                                     ma->name);
    ma->curbuf = mb->data;
    mb->next = ma->bufs;
    ma->bufs = mb;

    lib_asan_poison(ma->curbuf, ma->cursize);

    memarena_curbuf_align(ma);
  }

  ptr = ma->curbuf;
  ma->curbuf += size;
  ma->cursize -= size;

  VALGRIND_MEMPOOL_ALLOC(ma, ptr, size);

  lib_asan_unpoison(ptr, size);

  return ptr;
}

void *lib_memarena_calloc(MemArena *ma, size_t size)
{
  void *ptr;

  /* No need to use this fn call if we're calloc'ing by default. */
  lib_assert(ma->use_calloc == false);

  ptr = lib_memarena_alloc(ma, size);
  lib_assert(ptr != NULL);
  memset(ptr, 0, size);

  return ptr;
}

void lib_memarena_merge(MemArena *ma_dst, MemArena *ma_src)
{
  /* Mem arenas must be compatible. */
  lib_assert(ma_dst != ma_src);
  lib_assert(ma_dst->align == ma_src->align);
  lib_assert(ma_dst->use_calloc == ma_src->use_calloc);
  lib_assert(ma_dst->bufsize == ma_src->bufsize);

  if (ma_src->bufs == NULL) {
    return;
  }

  if (UNLIKELY(ma_dst->bufs == NULL)) {
    lib_assert(ma_dst->curbuf == NULL);
    ma_dst->bufs = ma_src->bufs;
    ma_dst->curbuf = ma_src->curbuf;
    ma_dst->cursize = ma_src->cursize;
  }
  else {
    /* Keep the 'ma_dst->curbuf' for simplicity.
     * Insert bufs after the first. */
    if (ma_dst->bufs->next != NULL) {
      /* Loop over `ma_src` instead of `ma_dst` since it's likely the destination is larger
       * when used for accumulating from multiple sources. */
      struct MemBuf *mb_src = ma_src->bufs;
      mb_src = ma_src->bufs;
      while (mb_src && mb_src->next) {
        mb_src = mb_src->next;
      }
      mb_src->next = ma_dst->bufs->next;
    }
    ma_dst->bufs->next = ma_src->bufs;
  }

  ma_src->bufs = NULL;
  ma_src->curbuf = NULL;
  ma_src->cursize = 0;

  VALGRIND_MOVE_MEMPOOL(ma_src, ma_dst);
  VALGRIND_CREATE_MEMPOOL(ma_src, 0, false);
}

void lib_memarena_clear(MemArena *ma)
{
  if (ma->bufs) {
    uchar *curbuf_prev;
    size_t curbuf_used;

    if (ma->bufs->next) {
      memarena_buf_free_all(ma->bufs->next);
      ma->bufs->next = NULL;
    }

    curbuf_prev = ma->curbuf;
    ma->curbuf = ma->bufs->data;
    memarena_curbuf_align(ma);

    /* restore to original size */
    curbuf_used = (size_t)(curbuf_prev - ma->curbuf);
    ma->cursize += curbuf_used;

    if (ma->use_calloc) {
      memset(ma->curbuf, 0, curbuf_used);
    }
    lib_asan_poison(ma->curbuf, ma->cursize);
  }

  VALGRIND_DESTROY_MEMPOOL(ma);
  VALGRIND_CREATE_MEMPOOL(ma, 0, false);
}
