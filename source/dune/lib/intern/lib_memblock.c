/* Dead simple, fast mem allocator for alloc many elements of the same size. */
#include <stdlib.h>
#include <string.h>
#include "atomic_ops.h"
#include "lib_utildefines.h"
#include "lib_memblock.h" /* own include */
#include "mem_guardedalloc.h"
#include "lib_strict_flags.h" /* keep last */

#define CHUNK_LIST_SIZE 16

struct LibMemblock {
  void **chunk_list;

  /* Elem size in bytes. */
  int elem_size;
  /* 1st unused elem index. */
  int elem_next;
  /* Last "touched" elem. */
  int elem_last;
  /* Offset in a chunk of the next elem. */
  int elem_next_ofs;
  /* Max offset in a chunk. */
  int chunk_max_ofs;
  /* Id of the chunk used for the next allocation. */
  int chunk_next;
  /* Chunk size in bytes. */
  int chunk_size;
  /* Num of alloc'd chunk. */
  int chunk_len;
};

LibMemblock *lib_memblock_create_ex(uint elem_size, uint chunk_size)
{
  lib_assert(elem_size < chunk_size);

  LibMemblock *mblk = mem_malloc(sizeof(LibMemblock), "lib_memblock");
  mblk->elem_size = (int)elem_size;
  mblk->elem_next = 0;
  mblk->elem_last = -1;
  mblk->chunk_size = (int)chunk_size;
  mblk->chunk_len = CHUNK_LIST_SIZE;
  mblk->chunk_list = mem_calloc(sizeof(void *) * (uint)mblk->chunk_len, "chunk list");
  mblk->chunk_list[0] = mem_malloc_aligned((uint)mblk->chunk_size, 32, "lib_memblock chunk");
  memset(mblk->chunk_list[0], 0x0, (uint)mblk->chunk_size);
  mblk->chunk_max_ofs = (mblk->chunk_size / mblk->elem_size) * mblk->elem_size;
  mblk->elem_next_ofs = 0;
  mblk->chunk_next = 0;
  return mblk;
}

void lib_memblock_destroy(LibMemblock *mblk, MemblockValFreeFP free_cb)
{
  int elem_per_chunk = mblk->chunk_size / mblk->elem_size;

  if (free_cb) {
    for (int i = 0; i <= mblk->elem_last; i++) {
      int chunk_idx = i / elem_per_chunk;
      int elem_idx = i - elem_per_chunk * chunk_idx;
      void *val = (char *)(mblk->chunk_list[chunk_idx]) + mblk->elem_size * elem_idx;
      free_cb(val);
    }
  }

  for (int i = 0; i < mblk->chunk_len; i++) {
    MEM_SAFE_FREE(mblk->chunk_list[i]);
  }
  MEM_SAFE_FREE(mblk->chunk_list);
  mem_free(mblk);
}

void lib_memblock_clear(LibMemblock *mblk, MemblockValFreeFP free_cb)
{
  int elem_per_chunk = mblk->chunk_size / mblk->elem_size;
  int last_used_chunk = mblk->elem_next / elem_per_chunk;

  if (free_cb) {
    for (int i = mblk->elem_last; i >= mblk->elem_next; i--) {
      int chunk_idx = i / elem_per_chunk;
      int elem_idx = i - elem_per_chunk * chunk_idx;
      void *val = (char *)(mblk->chunk_list[chunk_idx]) + mblk->elem_size * elem_idx;
      free_callback(val);
    }
  }

  for (int i = last_used_chunk + 1; i < mblk->chunk_len; i++) {
    MEM_SAFE_FREE(mblk->chunk_list[i]);
  }

  if (UNLIKELY(last_used_chunk + 1 < mblk->chunk_len - CHUNK_LIST_SIZE)) {
    mblk->chunk_len -= CHUNK_LIST_SIZE;
    mblk->chunk_list = mem_recalloc(mblk->chunk_list, sizeof(void *) * (uint)mblk->chunk_len);
  }

  mblk->elem_last = mblk->elem_next - 1;
  mblk->elem_next = 0;
  mblk->elem_next_ofs = 0;
  mblk->chunk_next = 0;
}

void *lib_memblock_alloc(LibMemblock *mblk)
{
  /* Bookkeeping. */
  if (mblk->elem_last < mblk->elem_next) {
    mblk->elem_last = mblk->elem_next;
  }
  mblk->elem_next++;

  void *ptr = (char *)(mblk->chunk_list[mblk->chunk_next]) + mblk->elem_next_ofs;

  mblk->elem_next_ofs += mblk->elem_size;

  if (mblk->elem_next_ofs == mblk->chunk_max_ofs) {
    mblk->elem_next_ofs = 0;
    mblk->chunk_next++;

    if (UNLIKELY(mblk->chunk_next >= mblk->chunk_len)) {
      mblk->chunk_len += CHUNK_LIST_SIZE;
      mblk->chunk_list = mem_recalloc(mblk->chunk_list, sizeof(void *) * (uint)mblk->chunk_len);
    }

    if (UNLIKELY(mblk->chunk_list[mblk->chunk_next] == NULL)) {
      mblk->chunk_list[mblk->chunk_next] = mem_malloc_aligned(
          (uint)mblk->chunk_size, 32, "lib_memblock chunk");
      memset(mblk->chunk_list[mblk->chunk_next], 0x0, (uint)mblk->chunk_size);
    }
  }
  return ptr;
}

void lib_memblock_iternew(LibMemblock *mblk, lib_memblock_iter *iter)
{
  /* Small copy of the memblock used for better cache coherence. */
  iter->chunk_list = mblk->chunk_list;
  iter->end_index = mblk->elem_next;
  iter->cur_index = 0;
  iter->chunk_idx = 0;
  iter->elem_ofs = 0;
  iter->elem_size = mblk->elem_size;
  iter->chunk_max_ofs = mblk->chunk_max_ofs;
}

void *lib_memblock_iterstep(LibMemblockIter *iter)
{
  if (iter->cur_index == iter->end_index) {
    return NULL;
  }

  iter->cur_index++;

  void *ptr = (char *)(iter->chunk_list[iter->chunk_idx]) + iter->elem_ofs;

  iter->elem_ofs += iter->elem_size;

  if (iter->elem_ofs == iter->chunk_max_ofs) {
    iter->elem_ofs = 0;
    iter->chunk_idx++;
  }
  return ptr;
}

void *lib_memblock_elem_get(LibMemblock *mblk, int chunk, int elem)
{
  lib_assert(chunk < mblk->chunk_len);
  int elem_per_chunk = mblk->chunk_size / mblk->elem_size;
  chunk += elem / elem_per_chunk;
  elem = elem % elem_per_chunk;
  return (char *)(mblk->chunk_list[chunk]) + mblk->elem_size * elem;
}
