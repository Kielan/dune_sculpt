/* Simple, fast mem allocator for alloc many small elements of diff sizes
 * in fixed size mem chunks,
 * although allocs bigger than the chunk size are supported.
 * They will reduce the efficiency of this data-struct.
 * Elements are ptr aligned.
 *
 * Supports:
 * - Alloc of mixed sizes.
 * - Iter over allocs in-order.
 * - Clearing for re-use.
 *
 * Unsupported:
 * - Freeing individual elements.
 *
 * We could inline iter stepping,
 * tests show this doesn't give noticeable speedup. */

#include <stdlib.h>
#include <string.h>

#include "lib_asan.h"
#include "lib_utildefines.h"

#include "lib_memiter.h" /* own include */

#include "mem_guardedalloc.h"

#include "lib_strict_flags.h" /* keep last */

/* TODO: Valgrind. */
typedef uintptr_t data_t;
typedef intptr_t offset_t;

/* Write the chunk terminator on adding each element.
 * typically we rely on the 'count' to avoid iter'ing past the end. */
// #define USE_TERMINATE_PARANOID

/* Currently totalloc isn't used. */
// #define USE_TOTALLOC

/* pad must be power of two */
#define PADUP(num, pad) (((num) + ((pad)-1)) & ~((pad)-1))

typedef struct LibMemIterElem {
  offset_t size;
  data_t data[0];
} LibMemIterElem;

typedef struct LibMemIterChunk {
  struct LibMemIterChunk *next;
  /* internal format is:
   * `[next_ptr, size:data, size:data, ..., negative_offset]`
   * Where negative offset rewinds to the start */
  data_t data[0];
} LibMemIterChunk;

typedef struct LibMemIter {
  /* A ptr to 'head' is needed so we can iterate in the order allocated. */
  lib_memiter_chunk *head, *tail;
  data_t *data_curr;
  data_t *data_last;
  /* Used unless a large elem is requested.
   * (which should be very rare!). */
  uint chunk_size_in_bytes_min;
  uint count;
#ifdef USE_TOTALLOC
  uint totalloc;
#endif
} LibMemIter;

LIB_INLINE uint data_offset_from_size(uint size)
{
  return PADUP(size, (uint)sizeof(data_t)) / (uint)sizeof(data_t);
}

static void memiter_set_rewind_offset(LibMemIter *mi)
{
  LibMemIterElem *elem = (LibMemIterElem *)mi->data_curr;

  lib_asan_unpoison(elem, sizeof(LibMemIterElem));

  elem->size = (offset_t)(((data_t *)mi->tail) - mi->data_curr);
  lib_assert(elem->size < 0);
}

static void memiter_init(LibMemIter *mi)
{
  mi->head = NULL;
  mi->tail = NULL;
  mi->data_curr = NULL;
  mi->data_last = NULL;
  mi->count = 0;
#ifdef USE_TOTALLOC
  mi->totalloc = 0;
#endif
}

/* Public API's */
LibMemIter *lib_memiter_create(uint chunk_size_min)
{
  LibMemIter *mi = mem_malloc(sizeof(LibMemIter), "lib_memiter");
  memiter_init(mi);

  /* Small vals are used for tests to check for correctness,
   * but otherwise not that useful. */
  const uint slop_space = (sizeof(BLI_memiter_chunk) + MEM_SIZE_OVERHEAD);
  if (chunk_size_min >= 1024) {
    /* As long as the input is a power of 2, this will give efficient sizes. */
    chunk_size_min -= slop_space;
  }

  mi->chunk_size_in_bytes_min = chunk_size_min;
  return mi;
}

void *lib_memiter_alloc(LibMemIter *mi, uint elem_size)
{
  const uint data_offset = data_offset_from_size(elem_size);
  data_t *data_curr_next = LIKELY(mi->data_curr) ? mi->data_curr + (1 + data_offset) : NULL;

  if (UNLIKELY(mi->data_curr == NULL) || (data_curr_next > mi->data_last)) {

#ifndef USE_TERMINATE_PARANOID
    if (mi->data_curr != NULL) {
      memiter_set_rewind_offset(mi);
    }
#endif

    uint chunk_size_in_bytes = mi->chunk_size_in_bytes_min;
    if (UNLIKELY(chunk_size_in_bytes < elem_size + (uint)sizeof(data_t[2]))) {
      chunk_size_in_bytes = elem_size + (uint)sizeof(data_t[2]);
    }
    uint chunk_size = data_offset_from_size(chunk_size_in_bytes);
    LibMemIterChunk *chunk = mem_malloc(
        sizeof(LibMemIterChunk) + (chunk_size * sizeof(data_t)), "lib_memiter_chunk");

    if (mi->head == NULL) {
      lib_assert(mi->tail == NULL);
      mi->head = chunk;
    }
    else {
      mi->tail->next = chunk;
    }
    mi->tail = chunk;
    chunk->next = NULL;

    mi->data_curr = chunk->data;
    mi->data_last = chunk->data + (chunk_size - 1);
    data_curr_next = mi->data_curr + (1 + data_offset);

    lib_asan_poison(chunk->data, chunk_size * sizeof(data_t));
  }

  lib_assert(data_curr_next <= mi->data_last);

  LibMemIterElem *elem = (LinMemIterElem *)mi->data_curr;

  lib_asan_unpoison(elem, sizeof(lib_memiter_elem) + elem_size);

  elem->size = (offset_t)elem_size;
  mi->data_curr = data_curr_next;

#ifdef USE_TERMINATE_PARANOID
  memiter_set_rewind_offset(mi);
#endif

  mi->count += 1;

#ifdef USE_TOTALLOC
  mi->totalloc += elem_size;
#endif

  return elem->data;
}

void *lib_memiter_calloc(LibMemIter *mi, uint elem_size)
{
  void *data = lib_memiter_alloc(mi, elem_size);
  memset(data, 0, elem_size);
  return data;
}

void lib_memiter_alloc_from(LibMemIter *mi, uint elem_size, const void *data_from)
{
  void *data = lib_memiter_alloc(mi, elem_size);
  memcpy(data, data_from, elem_size);
}

static void memiter_free_data(LibMemIter *mi)
{
  LibMemIterChunk *chunk = mi->head;
  while (chunk) {
    LibMemIterChunk *chunk_next = chunk->next;

    /* Unpoison mem bc mem_free might overwrite it. */
    lib_asan_unpoison(chunk, mem_alloc_len(chunk));

    mem_free(chunk);
    chunk = chunk_next;
  }
}

void lib_memiter_destroy(LibMemIter *mi)
{
  memiter_free_data(mi);
  mem_free(mi);
}

void lib_memiter_clear(LibMemIter *mi)
{
  memiter_free_data(mi);
  memiter_init(mi);
}

uint lib_memiter_count(const LibMemIter *mi)
{
  return mi->count;
}

/* Helper API's */
void *lib_memiter_elem_first(LibMemIter *mi)
{
  if (mi->head != NULL) {
    LinMemIterChunk *chunk = mi->head;
    LibMemIterElem *elem = (LibMemIterElem *)chunk->data;
    return elem->data;
  }
  return NULL;
}

void *lib_memiter_elem_first_size(LibMemIter *mi, uint *r_size)
{
  if (mi->head != NULL) {
    LibMemIterChunk *chunk = mi->head;
    LibMemIterElem *elem = (LibMemIterElem *)chunk->data;
    *r_size = (uint)elem->size;
    return elem->data;
  }
  return NULL;
}

/* Iter API's
 * We could loop over elems until a NULL chunk is found,
 * however this means every alloc needs to preemptively run
 * memiter_set_rewind_offset (see USE_TERMINATE_PARANOID).
 * Unless we have a call to finalize alloc (which complicates usage).
 * So use a counter instead. */

void lib_memiter_iter_init(LibMemIter *mi, LibMemIterHandle *iter)
{
  iter->elem = mi->head ? (LibMemIterElem *)mi->head->data : NULL;
  iter->elem_left = mi->count;
}

bool lib_memiter_iter_done(const LibMemIterHandle *iter)
{
  return iter->elem_left != 0;
}

LIB_INLINE void memiter_chunk_step(LibMemIterHandle *iter)
{
  lib_assert(iter->elem->size < 0);
  LibMemIterChunk *chunk = (LibMemIterChunk *)(((data_t *)iter->elem) + iter->elem->size);
  chunk = chunk->next;
  iter->elem = chunk ? (LibMemIterElem *)chunk->data : NULL;
  lib_assert(iter->elem == NULL || iter->elem->size >= 0);
}

void *lib_memiter_iter_step_size(LibMemIterHandle *iter, uint *r_size)
{
  if (iter->elem_left != 0) {
    iter->elem_left -= 1;
    if (UNLIKELY(iter->elem->size < 0)) {
      memiter_chunk_step(iter);
    }
    lib_assert(iter->elem->size >= 0);
    uint size = (uint)iter->elem->size;
    *r_size = size; /* <-- only diff */
    data_t *data = iter->elem->data;
    iter->elem = (LibMemIterElem *)&data[data_offset_from_size(size)];
    return (void *)data;
  }
  return NULL;
}

void *lib_memiter_iter_step(LibMemIterHandle *iter)
{
  if (iter->elem_left != 0) {
    iter->elem_left -= 1;
    if (UNLIKELY(iter->elem->size < 0)) {
      memiter_chunk_step(iter);
    }
    lib_assert(iter->elem->size >= 0);
    uint size = (uint)iter->elem->size;
    data_t *data = iter->elem->data;
    iter->elem = (LibMemIterElem *)&data[data_offset_from_size(size)];
    return (void *)data;
  }
  return NULL;
}

/** \} */
