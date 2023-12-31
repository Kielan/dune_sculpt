/* Array storage to minimize dup.
 *
 * This is done by splitting arrays into chunks and using copy-on-write (COW),
 * to de-dup chunks, from the users perspective this is an implementation detail.
 *
 * Overview
 *
 * Data Struct
 *
 * This diagram is an overview of the struct of a single array-store.
 *
 * The only 2 structs here which are ref externally are the.
 *
 * - ArrayStore: The whole array store.
 * - ArrayState: Represents a single state (array) of data.
 *   These can be add using a ref state,
 *   while this could be considered the prev or parent state.
 *   no relationship is kept,
 *   so the caller is free to add any state from the same ArrayStore as a ref.
 *
 * <pre>
 * <+> ArrayStore: root data-struct,
 *  |  can store many 'states', which share mem.
 *  |
 *  |  This can store many arrays, however they must share the same 'stride'.
 *  |  Arrays of different types will need to use a new ArrayStore.
 *  |
 *  +- <+> states (Collection of ArrayState's):
 *  |   |  Each represents an array added by the user of this API.
 *  |   |  and refs a chunk_list (each state is a chunk_list user).
 *  |   |  Note that the list order has no significance.
 *  |   |
 *  |   +- <+> chunk_list (aChunkList):
 *  |       |  The chunks that make up this state.
 *  |       |  Each state is a chunk_list user,
 *  |       |  avoids duplicating lists when there is no change between states.
 *  |       |
 *  |       +- chunk_refs (List of ChunkRef): Each chunk_ref links to a #BChunk.
 *  |          Each ref is a chunk user,
 *  |          avoids duplicating smaller chunks of mem found in multiple states.
 *  |
 *  +- info (ArrayInfo):
 *  |  Sizes and offsets for this array-store.
 *  |  Also caches some vars for reuse.
 *  |
 *  +- <+> mem (ArrayMem):
 *      |  Mem pools for storing ArrayStore data.
 *      |
 *      +- chunk_list (Pool of ChunkList):
 *      |  All chunk_lists, (ref counted, used by ArrayState).
 *      |
 *      +- chunk_ref (Pool of ChunkRef):
 *      |  All chunk_refs (link between ChunkList & Chunk).
 *      |
 *      +- chunks (Pool of Chunk):
 *         All chunks, (re counted, used by ChunkList).
 *         These have their headers hashed for reuse so we can quickly check for duplicates.
 * </pre>
 *
 * De-Duplication
 *
 * When creating a new state, a prev state can be given as a ref,
 * matching chunks from this state are re-used in the new state.
 *
 * First matches at either end of the array are detected.
 * For identical arrays this is all that's needed.
 *
 * De-dup is performed on any remaining chunks, by hashing the first few bytes of the chunk
 * (see: BCHUNK_HASH_TABLE_ACCUMULATE_STEPS).
 *
 * This is cached for reuse since the refd data never changes.
 *
 * An array is created to store hash vals at every 'stride',
 * then stepped over to search for matching chunks.
 *
 * Once a match is found, there is a high chance next chunks match too,
 * so this is checked to avoid performing so many hash-lookups.
 * Otherwise new chunks are created. */

#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_list.h"
#include "lib_mempool.h"

#include "lib_strict_flags.h"

#include "lib_array_store.h" /* Own include. */

/* Only for lib_array_store_is_valid. */
#include "lib_ghash.h"

/* Defines
 * Some of the logic for merging is quite involved,
 * support disabling some parts of this. */

/* Scan first chunks (happy path when beginning of the array matches).
 * When the array is a perfect match, we can re-use the entire list.
 * That disabling makes some tests fail that check for output-size. */
#define USE_FASTPATH_CHUNKS_FIRST

/* Scan last chunks (happy path when end of the array matches).
 * When the end of the array matches, we can quickly add these chunks.
 * That we will add contiguous matching chunks
 * so this isn't as useful as USE_FASTPATH_CHUNKS_FIRST,
 * however it avoids adding matching chunks into the lookup table,
 * so creating the lookup table won't be as expensive. */
#ifdef USE_FASTPATH_CHUNKS_FIRST
#  define USE_FASTPATH_CHUNKS_LAST
#endif

/* For arrays of matching length, test that *enough* of the chunks are aligned,
 * and simply step over both arrays, using matching chunks.
 * This avoids overhead of using a lookup table for cases
 * when we can assume they're mostly aligned. */
#define USE_ALIGN_CHUNKS_TEST

/* Accumulate hashes from right to left so we can create a hash for the chunk-start.
 * This serves to increase uniqueness and will help when there is many vals which are the same. */
#define USE_HASH_TABLE_ACCUMULATE

#ifdef USE_HASH_TABLE_ACCUMULATE
/* Num of times to propagate hashes back.
 * Effectively a 'triangle-number'.
 * so 3 -> 7, 4 -> 11, 5 -> 16, 6 -> 22, 7 -> 29, ... etc.
 *
 * Additional steps are expensive, so avoid high vals unless necessary
 * (with low strides, between 1-4) where a low val would cause the hashes to
 * be un-evenly distributed. */
#  define BCHUNK_HASH_TABLE_ACCUMULATE_STEPS_DEFAULT 3
#  define BCHUNK_HASH_TABLE_ACCUMULATE_STEPS_32BITS 4
#  define BCHUNK_HASH_TABLE_ACCUMULATE_STEPS_16BITS 5
/* Singe bytes (or bool) arrays need a higher num of steps
 * bc the resulting vals are not unique enough to result in evenly distributed vals.
 * Use more accumulation when the size of the structs is small, see: #105046.
 *
 * With 6 -> 22, one byte each - means an array of bools can be combine into 22 bits
 * representing 4,194,303 different combinations. */
#  define BCHUNK_HASH_TABLE_ACCUMULATE_STEPS_8BITS 6
#else
/* How many items to hash (multiplied by stride).
 * The more vals, the greater the chance this block has a unique hash. */
#  define BCHUNK_HASH_LEN 16
#endif

/* Calc the key once and reuse it. */
#define USE_HASH_TABLE_KEY_CACHE
#ifdef USE_HASH_TABLE_KEY_CACHE
#  define HASH_TABLE_KEY_UNSET ((hash_key)-1)
#  define HASH_TABLE_KEY_FALLBACK ((hash_key)-2)
#endif

/* Ensure dup entries aren't added to tmp hash table
 * needed for arrays where many vals match (an array of bools all true/false for e.g.).
 * Wo this, a huge num of dups are added a single bucket, making hash lookups slow.
 * While dedup adds some cost, it's only performed with other chunks in the same bucket
 * so cases when all chunks are unique will quickly detect and exit the `memcmp` in most cases. */
#define USE_HASH_TABLE_DEDUP

/* How much larger the table is then the total number of chunks. */
#define BCHUNK_HASH_TABLE_MUL 3

/* Merge too small/large chunks:
 * Using this means chunks below a threshold will be merged together.
 * Even though short term this uses more mem,
 * long term the overhead of maintaining many small chunks is reduced.
 * This is defined by setting the min chunk size (as a fraction of the regular chunk size).
 * Chunks may also become too large (when incrementally growing an array),
 * this also enables chunk splitting. */
#define USE_MERGE_CHUNKS

#ifdef USE_MERGE_CHUNKS
/* Merge chunks smaller then: (BArrayInfo::chunk_byte_size / #BCHUNK_SIZE_MIN_DIV). */
#  define BCHUNK_SIZE_MIN_DIV 8

/* Disallow chunks bigger than the regular chunk size scaled by this val.
 * must be at least 2!
 * however, this code runs won't run in tests unless it's ~1.1 ugh.
 * so lower only to check splitting works. */
#  define BCHUNK_SIZE_MAX_MUL 2
#endif /* USE_MERGE_CHUNKS */

/* Slow (keep disabled), but handy for debugging. */
// #define USE_VALIDATE_LIST_SIZE

// #define USE_VALIDATE_LIST_DATA_PARTIAL

// #define USE_PARANOID_CHECKS

/* Internal Structs */
typedef uint32_t hash_key;

typedef struct ArrayInfo {
  size_t chunk_stride;
  // uint chunk_count;  /* UNUSED (other values are derived from this) */

  /* Pre-calc. */
  size_t chunk_byte_size;
  /* Min/max limits (inclusive) */
  size_t chunk_byte_size_min;
  size_t chunk_byte_size_max;
  /* The read-ahead val should never exceed `chunk_byte_size`,
   * otherwise the hash would be based on values in the next chunk. */
  size_t accum_read_ahead_bytes;
#ifdef USE_HASH_TABLE_ACCUMULATE
  size_t accum_steps;
  size_t accum_read_ahead_len;
#endif
} ArrayInfo;

typedef struct ArrayMem {
  Libmempool *chunk_list; /* #BChunkList. */
  Libmempool *chunk_ref;  /* #BChunkRef. */
  Libmempool *chunk;      /* #BChunk. */
} ArrayMem;

/* Main storage for all states. */
struct ArrayStore {
  /* Static. */
  ArrayInfo info;

  /* Mem storage. */
  ArrayMemory memory;

  /* ArrayState may be in any order (logic should never depend on state order). */
  List states;
};

/* A single instance of an array.
 *
 * This is how external API's hold a ref to an in-mem state,
 * although the struct is private.
 *
 * Currently each 'state' is alloc separately.
 * While this could be moved to a mem pool,
 * it makes it easier to trace invalid usage, so leave as-is for now. */
struct ArrayState {
  /* linked list in ArrayStore.states. */
  ArrayState *next, *prev;
  /* Shared chunk list, this ref must hold a ChunkList::users. */
  struct ChunkList *chunk_list;
};

typedef struct ChunkList {
  /* List of ChunkRef's. */
  List chunk_refs;
  /* Result of `lib_list_count(chunks)`, store for reuse. */
  uint chunk_refs_len;
  /* Size of all chunks (expanded). */
  size_t total_expanded_size;

  /* Number of ArrayState using this. */
  int users;
} ChunkList;

/* A chunk of mem in an array (unit of de-dup). */
typedef struct Chunk {
  const uchar *data;
  size_t data_len;
  /* num of ChunkList using this. */
  int users;

#ifdef USE_HASH_TABLE_KEY_CACHE
  hash_key key;
#endif
} Chunk;

/* Links to store Chunk data in ChunkList.chunk_refs. */
typedef struct ChunkRef {
  struct ChunkRef *next, *prev;
  Chunk *link;
} ChunkRef;

/* Single linked list used when putting chunks into a tmp table,
 * used for lookups.
 *
 * Point to the ChunkRef, not the Chunk,
 * to allow talking down the chunks in-order until a mismatch is found,
 * this avoids having to do so many table lookups. */
typedef struct TableRef {
  struct TableRef *next;
  const ChunkRef *cref;
} TableRef;

static size_t bchunk_list_size(const ChunkList *chunk_list);

/* Internal Chunk API */
static Chunk *chunk_new(ArrayMem *bs_mem, const uchar *data, const size_t data_len)
{
  Chunk *chunk = lib_mempool_alloc(bs_mem->chunk);
  chunk->data = data;
  chunk->data_len = data_len;
  chunk->users = 0;
#ifdef USE_HASH_TABLE_KEY_CACHE
  chunk->key = HASH_TABLE_KEY_UNSET;
#endif
  return chunk;
}

static Chunk *chunk_new_copydata(ArrayMem *s_mem, const uchar *data, const size_t data_len)
{
  uchar *data_copy = mem_malloc(data_len, __func__);
  memcpy(data_copy, data, data_len);
  return chunk_new(bs_mem, data_copy, data_len);
}

static void chunk_decref(ArrayMem *bs_mem, Chunk *chunk)
{
  lib_assert(chunk->users > 0);
  if (chunk->users == 1) {
    mem_free((void *)chunk->data);
    lib_mempool_free(bs_mem->chunk, chunk);
  }
  else {
    chunk->users -= 1;
  }
}

LIB_INLINE bool chunk_data_compare_unchecked(const Chunk *chunk,
                                              const uchar *data_base,
                                              const size_t data_base_len,
                                              const size_t offset)
{
  lib_assert(offset + (size_t)chunk->data_len <= data_base_len);
  UNUSED_VARS_NDEBUG(data_base_len);
  return (memcmp(&data_base[offset], chunk->data, chunk->data_len) == 0);
}

static bool chunk_data_compare(const Chunk *chunk,
                                const uchar *data_base,
                                const size_t data_base_len,
                                const size_t offset)
{
  if (offset + (size_t)chunk->data_len <= data_base_len) {
    return chunk_data_compare_unchecked(chunk, data_base, data_base_len, offset);
  }
  return false;
}

/* Internal ChunkList API */
static ChunkList *chunk_list_new(ArrayMem *bs_mem, size_t total_expanded_size)
{
  ChunkList *chunk_list = lib_mempool_alloc(bs_mem->chunk_list);

  lib_list_clear(&chunk_list->chunk_refs);
  chunk_list->chunk_refs_len = 0;
  chunk_list->total_expanded_size = total_expanded_size;
  chunk_list->users = 0;
  return chunk_list;
}

static void chunk_list_decref(ArrayMem *bs_mem, ChunkList *chunk_list)
{
  lib_assert(chunk_list->users > 0);
  if (chunk_list->users == 1) {
    for (ChunkRef *cref = chunk_list->chunk_refs.first, *cref_next; cref; cref = cref_next) {
      cref_next = cref->next;
      chunk_decref(bs_mem, cref->link);
      lib_mempool_free(bs_mem->chunk_ref, cref);
    }

    lib_mempool_free(bs_mem->chunk_list, chunk_list);
  }
  else {
    chunk_list->users -= 1;
  }
}

#ifdef USE_VALIDATE_LIST_SIZE
#  ifndef NDEBUG
#    define ASSERT_CHUNKLIST_SIZE(chunk_list, n) lib_assert(chunk_list_size(chunk_list) == n)
#  endif
#endif
#ifndef ASSERT_CHUNKLIST_SIZE
#  define ASSERT_CHUNKLIST_SIZE(chunk_list, n) (EXPR_NOP(chunk_list), EXPR_NOP(n))
#endif

#ifdef USE_VALIDATE_LIST_DATA_PARTIAL
static size_t chunk_list_data_check(const ChunkList *chunk_list, const uchar *data)
{
  size_t offset = 0;
  LIST_FOREACH (ChunkRef *, cref, &chunk_list->chunk_refs) {
    if (memcmp(&data[offset], cref->link->data, cref->link->data_len) != 0) {
      return false;
    }
    offset += cref->link->data_len;
  }
  return true;
}
#  define ASSERT_CHUNKLIST_DATA(chunk_list, data) \
    lib_assert(chunk_list_data_check(chunk_list, data))
#else
#  define ASSERT_CHUNKLIST_DATA(chunk_list, data) (EXPR_NOP(chunk_list), EXPR_NOP(data))
#endif

#ifdef USE_MERGE_CHUNKS
static void chunk_list_ensure_min_size_last(const ArrayInfo *info,
                                             ArrayMem *bs_mem,
                                             ChunkList *chunk_list)
{
  ChunkRef *cref = chunk_list->chunk_refs.last;
  if (cref && cref->prev) {
    /* Both are decrefed after use (end of this block). */
    Chunk *chunk_curr = cref->link;
    Chunk *chunk_prev = cref->prev->link;

    if (MIN2(chunk_prev->data_len, chunk_curr->data_len) < info->chunk_byte_size_min) {
      const size_t data_merge_len = chunk_prev->data_len + chunk_curr->data_len;
      /* We could pass, but no need. */
      if (data_merge_len <= info->chunk_byte_size_max) {
        /* We have enough space to merge. */

        /* Remove last from the linked-list. */
        lib_assert(chunk_list->chunk_refs.last != chunk_list->chunk_refs.first);
        cref->prev->next = NULL;
        chunk_list->chunk_refs.last = cref->prev;
        chunk_list->chunk_refs_len -= 1;

        uchar *data_merge = mem_malloc(data_merge_len, __func__);
        memcpy(data_merge, chunk_prev->data, chunk_prev->data_len);
        memcpy(&data_merge[chunk_prev->data_len], chunk_curr->data, chunk_curr->data_len);

        cref->prev->link = bchunk_new(bs_mem, data_merge, data_merge_len);
        cref->prev->link->users += 1;

        lib_mempool_free(bs_mem->chunk_ref, cref);
      }
      else {
        /* If we always merge small slices, we should _almost_
         * never end up having very large chunks.
         * Gradual expanding on contracting will cause this.
         *
         * if we do, the code below works (test by setting 'CHUNK_SIZE_MAX_MUL = 1.2') */
        /* Keep chunk on the left hand side a regular size. */
        const size_t split = info->chunk_byte_size;

        /* Merge and split. */
        const size_t data_prev_len = split;
        const size_t data_curr_len = data_merge_len - split;
        uchar *data_prev = mem_malloc(data_prev_len, __func__);
        uchar *data_curr = mem_malloc(data_curr_len, __func__);

        if (data_prev_len <= chunk_prev->data_len) {
          const size_t data_curr_shrink_len = chunk_prev->data_len - data_prev_len;

          /* Setup 'data_prev'. */
          memcpy(data_prev, chunk_prev->data, data_prev_len);

          /* Setup 'data_curr'. */
          memcpy(data_curr, &chunk_prev->data[data_prev_len], data_curr_shrink_len);
          memcpy(&data_curr[data_curr_shrink_len], chunk_curr->data, chunk_curr->data_len);
        }
        else {
          lib_assert(data_curr_len <= chunk_curr->data_len);
          lib_assert(data_prev_len >= chunk_prev->data_len);

          const size_t data_prev_grow_len = data_prev_len - chunk_prev->data_len;

          /* Setup 'data_prev'. */
          memcpy(data_prev, chunk_prev->data, chunk_prev->data_len);
          memcpy(&data_prev[chunk_prev->data_len], chunk_curr->data, data_prev_grow_len);

          /* Setup 'data_curr'. */
          memcpy(data_curr, &chunk_curr->data[data_prev_grow_len], data_curr_len);
        }

        cref->prev->link = chunk_new(bs_mem, data_prev, data_prev_len);
        cref->prev->link->users += 1;

        cref->link = chunk_new(bs_mem, data_curr, data_curr_len);
        cref->link->users += 1;
      }

      /* Free zero users. */
      chunk_decref(bs_mem, chunk_curr);
      chunk_decref(bs_mem, chunk_prev);
    }
  }
}
#endif /* USE_MERGE_CHUNKS */

/* Split length into 2 vals
 * param r_data_trim_len: Length which is aligned to the ArrayInfo.chunk_byte_size
 * param r_data_last_chunk_len: The remaining bytes.
 *
 * This fn ensures the size of r_data_last_chunk_len
 * is larger than ArrayInfo.chunk_byte_size_min. */
static void chunk_list_calc_trim_len(const ArrayInfo *info,
                                      const size_t data_len,
                                      size_t *r_data_trim_len,
                                      size_t *r_data_last_chunk_len)
{
  size_t data_last_chunk_len = 0;
  size_t data_trim_len = data_len;

#ifdef USE_MERGE_CHUNKS
  /* Avoid creating too-small chunks more efficient than merging after. */
  if (data_len > info->chunk_byte_size) {
    data_last_chunk_len = (data_trim_len % info->chunk_byte_size);
    data_trim_len = data_trim_len - data_last_chunk_len;
    if (data_last_chunk_len) {
      if (data_last_chunk_len < info->chunk_byte_size_min) {
        /* May be zero and that's OK. */
        data_trim_len -= info->chunk_byte_size;
        data_last_chunk_len += info->chunk_byte_size;
      }
    }
  }
  else {
    data_trim_len = 0;
    data_last_chunk_len = data_len;
  }

  lib_assert((data_trim_len == 0) || (data_trim_len >= info->chunk_byte_size));
#else
  data_last_chunk_len = (data_trim_len % info->chunk_byte_size);
  data_trim_len = data_trim_len - data_last_chunk_len;
#endif

  lib_assert(data_trim_len + data_last_chunk_len == data_len);

  *r_data_trim_len = data_trim_len;
  *r_data_last_chunk_len = data_last_chunk_len;
}

/* Append and don't manage merging small chunks. */
static void chunk_list_append_only(ArrayMem *bs_mem, ChunkList *chunk_list, Chunk *chunk)
{
  ChunkRef *cref = lib_mempool_alloc(bs_mem->chunk_ref);
  lib_addtail(&chunk_list->chunk_refs, cref);
  cref->link = chunk;
  chunk_list->chunk_refs_len += 1;
  chunk->users += 1;
}

/* This is for writing single chunks,
 * use chunk_list_append_data_n when writing large blocks of mem into many chunks. */
static void chunk_list_append_data(const ArrayInfo *info,
                                    ArrayMem *bs_mem,
                                    ChunkList *chunk_list,
                                    const uchar *data,
                                    const size_t data_len)
{
  lib_assert(data_len != 0);

#ifdef USE_MERGE_CHUNKS
  lib_assert(data_len <= info->chunk_byte_size_max);

  if (!lib_list_is_empty(&chunk_list->chunk_refs)) {
    ChunkRef *cref = chunk_list->chunk_refs.last;
    Chunk *chunk_prev = cref->link;

    if (MIN2(chunk_prev->data_len, data_len) < info->chunk_byte_size_min) {
      const size_t data_merge_len = chunk_prev->data_len + data_len;
      /* Re-alloc for single user. */
      if (cref->link->users == 1) {
        uchar *data_merge = mem_realloc((void *)cref->link->data, data_merge_len);
        memcpy(&data_merge[chunk_prev->data_len], data, data_len);
        cref->link->data = data_merge;
        cref->link->data_len = data_merge_len;
      }
      else {
        uchar *data_merge = mem_malloc(data_merge_len, __func__);
        memcpy(data_merge, chunk_prev->data, chunk_prev->data_len);
        memcpy(&data_merge[chunk_prev->data_len], data, data_len);
        cref->link = chunk_new(bs_mem, data_merge, data_merge_len);
        cref->link->users += 1;
        chunk_decref(bs_mem, chunk_prev);
      }
      return;
    }
  }
#else
  UNUSED_VARS(info);
#endif /* USE_MERGE_CHUNKS */

  Chunk *chunk = chunk_new_copydata(bs_mem, data, data_len);
  chunk_list_append_only(bs_mem, chunk_list, chunk);

  /* Don't run this, instead preemptively avoid creating a chunk only to merge it (above). */
#if 0
#  ifdef USE_MERGE_CHUNKS
  chunk_list_ensure_min_size_last(info, bs_mem, chunk_list);
#  endif
#endif
}

/* Similar to chunk_list_append_data, but handle multiple chunks.
 * Use for adding arrays of arbitrary sized mem at once.
 * This fn takes care not to perform redundant chunk-merging checks,
 * so we can write successive fixed size chunks quickly. */
static void chunk_list_append_data_n(const ArrayInfo *info,
                                      ArrayMem *bs_mem,
                                      ChunkList *chunk_list,
                                      const uchar *data,
                                      size_t data_len)
{
  size_t data_trim_len, data_last_chunk_len;
  chunk_list_calc_trim_len(info, data_len, &data_trim_len, &data_last_chunk_len);

  if (data_trim_len != 0) {
    size_t i_prev;

    {
      const size_t i = info->chunk_byte_size;
      chunk_list_append_data(info, bs_mem, chunk_list, data, i);
      i_prev = i;
    }

    while (i_prev != data_trim_len) {
      const size_t i = i_prev + info->chunk_byte_size;
      Chunk *chunk = chunk_new_copydata(bs_mem, &data[i_prev], i - i_prev);
      chunk_list_append_only(bs_mem, chunk_list, chunk);
      i_prev = i;
    }

    if (data_last_chunk_len) {
      Chunk *chunk = chunk_new_copydata(bs_mem, &data[i_prev], data_last_chunk_len);
      chunk_list_append_only(bs_mem, chunk_list, chunk);
      // i_prev = data_len;  /* UNUSED */
    }
  }
  else {
    /* If we didn't write any chunks prev, we may need to merge with the last. */
    if (data_last_chunk_len) {
      chunk_list_append_data(info, bs_mem, chunk_list, data, data_last_chunk_len);
      // i_prev = data_len;  /* UNUSED */
    }
  }

#ifdef USE_MERGE_CHUNKS
  if (data_len > info->chunk_byte_size) {
    lib_assert(((ChunkRef *)chunk_list->chunk_refs.last)->link->data_len >=
               info->chunk_byte_size_min);
  }
#endif
}

static void chunk_list_append(const ArrayInfo *info,
                               ArrayMem *bs_mem,
                               ChunkList *chunk_list,
                               Chunk *chunk)
{
  chunk_list_append_only(bs_mem, chunk_list, chunk);

#ifdef USE_MERGE_CHUNKS
  chunk_list_ensure_min_size_last(info, bs_mem, chunk_list);
#else
  UNUSED_VARS(info);
#endif
}

static void bchunk_list_fill_from_array(const ArrayInfo *info,
                                        ArrayMem *bs_mem,
                                        ChunkList *chunk_list,
                                        const uchar *data,
                                        const size_t data_len)
{
  lib_assert(lib_list_is_empty(&chunk_list->chunk_refs));

  size_t data_trim_len, data_last_chunk_len;
  chunk_list_calc_trim_len(info, data_len, &data_trim_len, &data_last_chunk_len);

  size_t i_prev = 0;
  while (i_prev != data_trim_len) {
    const size_t i = i_prev + info->chunk_byte_size;
    Chunk *chunk = chunk_new_copydata(bs_mem, &data[i_prev], i - i_prev);
    chunk_list_append_only(bs_mem, chunk_list, chunk);
    i_prev = i;
  }

  if (data_last_chunk_len) {
    Chunk *chunk = chunk_new_copydata(bs_mem, &data[i_prev], data_last_chunk_len);
    chunk_list_append_only(bs_mem, chunk_list, chunk);
    // i_prev = data_len;
  }

#ifdef USE_MERGE_CHUNKS
  if (data_len > info->chunk_byte_size) {
    lib_assert(((ChunkRef *)chunk_list->chunk_refs.last)->link->data_len >=
               info->chunk_byte_size_min);
  }
#endif

  /* Works but better avoid redundant re-allocation. */
#if 0
#  ifdef USE_MERGE_CHUNKS
  chunk_list_ensure_min_size_last(info, bs_mem, chunk_list);
#  endif
#endif

  ASSERT_CHUNKLIST_SIZE(chunk_list, data_len);
  ASSERT_CHUNKLIST_DATA(chunk_list, data);
}

/* Internal Table Lookup Fns. */
/* Internal Hashing/De-Dup API
 * Only used by chunk_list_from_data_merge */

#define HASH_INIT (5381)

LIB_INLINE hash_key hash_data_single(const uchar p)
{
  return ((HASH_INIT << 5) + HASH_INIT) + (hash_key)(*((signed char *)&p));
}

/* Hash bytes, from lib_ghashutil_strhash_n. */
static hash_key hash_data(const uchar *key, size_t n)
{
  const signed char *p;
  hash_key h = HASH_INIT;

  for (p = (const signed char *)key; n--; p++) {
    h = (hash_key)((h << 5) + h) + (hash_key)*p;
  }

  return h;
}

#undef HASH_INIT

#ifdef USE_HASH_TABLE_ACCUMULATE
static void hash_array_from_data(const ArrayInfo *info,
                                 const uchar *data_slice,
                                 const size_t data_slice_len,
                                 hash_key *hash_array)
{
  if (info->chunk_stride != 1) {
    for (size_t i = 0, i_step = 0; i_step < data_slice_len; i++, i_step += info->chunk_stride) {
      hash_array[i] = hash_data(&data_slice[i_step], info->chunk_stride);
    }
  }
  else {
    /* Fast-path for bytes. */
    for (size_t i = 0; i < data_slice_len; i++) {
      hash_array[i] = hash_data_single(data_slice[i]);
    }
  }
}

/* Similar to hash_array_from_data,
 * but able to step into the next chunk if we run-out of data. */
static void hash_array_from_cref(const ArrayInfo *info,
                                 const ChunkRef *cref,
                                 const size_t data_len,
                                 hash_key *hash_array)
{
  const size_t hash_array_len = data_len / info->chunk_stride;
  size_t i = 0;
  do {
    size_t i_next = hash_array_len - i;
    size_t data_trim_len = i_next * info->chunk_stride;
    if (data_trim_len > cref->link->data_len) {
      data_trim_len = cref->link->data_len;
      i_next = data_trim_len / info->chunk_stride;
    }
    lib_assert(data_trim_len <= cref->link->data_len);
    hash_array_from_data(info, cref->link->data, data_trim_len, &hash_array[i]);
    i += i_next;
    cref = cref->next;
  } while ((i < hash_array_len) && (cref != NULL));

  /* If this isn't equal, the caller didn't properly check
   * that there was enough data left in all chunks. */
  lib_assert(i == hash_array_len);
}

LIB_INLINE void hash_accum_impl(hash_key *hash_array, const size_t i_dst, const size_t i_ahead)
{
  /* Tested to give good results when accumulating unique vals from an array of bools.
   * (least unused cells in the `TableRef **table`). */
  lib_assert(i_dst < i_ahead);
  hash_array[i_dst] += ((hash_array[i_ahead] << 3) ^ (hash_array[i_dst] >> 1));
}

static void hash_accum(hash_key *hash_array, const size_t hash_array_len, size_t iter_steps)
{
  /* _very_ unlikely, can happen if you sel a chunk-size of 1 for example. */
  if (UNLIKELY(iter_steps > hash_array_len)) {
    iter_steps = hash_array_len;
  }

  const size_t hash_array_search_len = hash_array_len - iter_steps;
  while (iter_steps != 0) {
    const size_t hash_offset = iter_steps;
    for (size_t i = 0; i < hash_array_search_len; i++) {
      hash_accum_impl(hash_array, i, i + hash_offset);
    }
    iter_steps -= 1;
  }
}

/* When we only need a single val, can use a small optimization.
 * we can avoid accumulating the tail of the array a little, each iter. */
static void hash_accum_single(hash_key *hash_array, const size_t hash_array_len, size_t iter_steps)
{
  lib_assert(iter_steps <= hash_array_len);
  if (UNLIKELY(!(iter_steps <= hash_array_len))) {
    /* While this shouldn't happen, avoid crashing. */
    iter_steps = hash_array_len;
  }
  /* We can increase this val each step to avoid accumulating quite as much
   * while getting the same results as hash_accum. */
  size_t iter_steps_sub = iter_steps;

  while (iter_steps != 0) {
    const size_t hash_array_search_len = hash_array_len - iter_steps_sub;
    const size_t hash_offset = iter_steps;
    for (uint i = 0; i < hash_array_search_len; i++) {
      hash_accum_impl(hash_array, i, i + hash_offset);
    }
    iter_steps -= 1;
    iter_steps_sub += iter_steps;
  }
}

static hash_key key_from_chunk_ref(const ArrayInfo *info,
                                   const ChunkRef *cref,
                                   /* Avoid reallocating each time. */
                                   hash_key *hash_store,
                                   const size_t hash_store_len)
{
  /* In C, will fill in a reusable array. */
  Chunk *chunk = cref->link;
  lib_assert((info->accum_read_ahead_bytes * info->chunk_stride) != 0);

  if (info->accum_read_ahead_bytes <= chunk->data_len) {
    hash_key key;

#  ifdef USE_HASH_TABLE_KEY_CACHE
    key = chunk->key;
    if (key != HASH_TABLE_KEY_UNSET) {
      /* Using key cache!
       * avoids calc every time. */
    }
    else {
      hash_array_from_cref(info, cref, info->accum_read_ahead_bytes, hash_store);
      hash_accum_single(hash_store, hash_store_len, info->accum_steps);
      key = hash_store[0];

      /* Cache the key. */
      if (UNLIKELY(key == HASH_TABLE_KEY_UNSET)) {
        key = HASH_TABLE_KEY_FALLBACK;
      }
      chunk->key = key;
    }
#  else
    hash_array_from_cref(info, cref, info->accum_read_ahead_bytes, hash_store);
    hash_accum_single(hash_store, hash_store_len, info->accum_steps);
    key = hash_store[0];
#  endif
    return key;
  }
  /* Corner case we're too small, calc the key each time. */
  hash_array_from_cref(info, cref, info->accum_read_ahead_bytes, hash_store);
  hash_accum_single(hash_store, hash_store_len, info->accum_steps);
  hash_key key = hash_store[0];

#  ifdef USE_HASH_TABLE_KEY_CACHE
  if (UNLIKELY(key == HASH_TABLE_KEY_UNSET)) {
    key = HASH_TABLE_KEY_FALLBACK;
  }
#  endif
  return key;
}

static const ChunkRef *table_lookup(const ArrayInfo *info,
                                     TableRef **table,
                                     const size_t table_len,
                                     const size_t i_table_start,
                                     const uchar *data,
                                     const size_t data_len,
                                     const size_t offset,
                                     const hash_key *table_hash_array)
{
  const hash_key key = table_hash_array[((offset - i_table_start) / info->chunk_stride)];
  const uint key_index = (uint)(key % (hash_key)table_len);
  const TableRef *tref = table[key_index];
  if (tref != NULL) {
    const size_t size_left = data_len - offset;
    do {
      const ChunkRef *cref = tref->cref;
#  ifdef USE_HASH_TABLE_KEY_CACHE
      if (cref->link->key == key)
#  endif
      {
        Chunk *chunk_test = cref->link;
        if (chunk_test->data_len <= size_left) {
          if (chunk_data_compare_unchecked(chunk_test, data, data_len, offset)) {
            /* We could remove the chunk from the table, to avoid multiple hits. */
            return cref;
          }
        }
      }
    } while ((tref = tref->next));
  }
  return NULL;
}

#else /* USE_HASH_TABLE_ACCUMULATE */

/* NON USE_HASH_TABLE_ACCUMULATE code (simply hash each chunk). */

static hash_key key_from_chunk_ref(const ArrayInfo *info, const ChunkRef *cref)
{
  hash_key key;
  Chunk *chunk = cref->link;
  const size_t data_hash_len = MIN2(chunk->data_len, CHUNK_HASH_LEN * info->chunk_stride);

#  ifdef USE_HASH_TABLE_KEY_CACHE
  key = chunk->key;
  if (key != HASH_TABLE_KEY_UNSET) {
    /* Using key cache!
     * avoids calc every time. */
  }
  else {
    /* Cache the key. */
    key = hash_data(chunk->data, data_hash_len);
    if (key == HASH_TABLE_KEY_UNSET) {
      key = HASH_TABLE_KEY_FALLBACK;
    }
    chunk->key = key;
  }
#  else
  key = hash_data(chunk->data, data_hash_len);
#  endif

  return key;
}

static const ChunkRef *table_lookup(const ArrayInfo *info,
                                     TableRef **table,
                                     const size_t table_len,
                                     const uint UNUSED(i_table_start),
                                     const uchar *data,
                                     const size_t data_len,
                                     const size_t offset,
                                     const hash_key *UNUSED(table_hash_array))
{
  const size_t data_hash_len = CHUNK_HASH_LEN * info->chunk_stride; /* TODO: cache. */

  const size_t size_left = data_len - offset;
  const hash_key key = hash_data(&data[offset], MIN2(data_hash_len, size_left));
  const uint key_index = (uint)(key % (hash_key)table_len);
  for (TableRef *tref = table[key_index]; tref; tref = tref->next) {
    const ChunkRef *cref = tref->cref;
#  ifdef USE_HASH_TABLE_KEY_CACHE
    if (cref->link->key == key)
#  endif
    {
      Chunk *chunk_test = cref->link;
      if (chunk_test->data_len <= size_left) {
        if (chunk_data_compare_unchecked(chunk_test, data, data_len, offset)) {
          /* We could remove the chunk from the table, to avoid multiple hits. */
          return cref;
        }
      }
    }
  }
  return NULL;
}

#endif /* USE_HASH_TABLE_ACCUMULATE */

/* End Table Lookup */
/* Main Data De-Dup Fn */
/* param data: Data to store in the returned val.
 * param data_len_original: Length of data in bytes.
 * param chunk_list_ref: Reuse this list or chunks within it, don't modify its content.
 * Caller is responsible for adding the user. */
static ChunkList *chunk_list_from_data_merge(const ArrayInfo *info,
                                             ArrayMem *bs_mem,
                                             const uchar *data,
                                             const size_t data_len_original,
                                             const ChunkList *chunk_list_ref)
{
  ASSERT_CHUNKLIST_SIZE(chunk_list_ref, chunk_list_ref->total_expanded_size);

  /* Fast-Path for exact match
   * Check for exact match, if so, return the current list. */
  const ChunkRef *cref_match_first = NULL;

  uint chunk_list_ref_skip_len = 0;
  size_t chunk_list_ref_skip_bytes = 0;
  size_t i_prev = 0;

#ifdef USE_FASTPATH_CHUNKS_FIRST
  {
    bool full_match = true;

    const ChunkRef *cref = chunk_list_ref->chunk_refs.first;
    while (i_prev < data_len_original) {
      if (cref != NULL && chunk_data_compare(cref->link, data, data_len_original, i_prev)) {
        cref_match_first = cref;
        chunk_list_ref_skip_len += 1;
        chunk_list_ref_skip_bytes += cref->link->data_len;
        i_prev += cref->link->data_len;
        cref = cref->next;
      }
      else {
        full_match = false;
        break;
      }
    }

    if (full_match) {
      if (chunk_list_ref->total_expanded_size == data_len_original) {
        return (ChunkList *)chunk_list_ref;
      }
    }
  }

  /* End Fast-Path (first) */

#endif /* USE_FASTPATH_CHUNKS_FIRST */

  /* Copy until we have a mismatch. */
  ChunkList *chunk_list = chunk_list_new(bs_mem, data_len_original);
  if (cref_match_first != NULL) {
    size_t chunk_size_step = 0;
    const ChunkRef *cref = chunk_list_ref->chunk_refs.first;
    while (true) {
      Chunk *chunk = cref->link;
      chunk_size_step += chunk->data_len;
      chunk_list_append_only(bs_mem, chunk_list, chunk);
      ASSERT_CHUNKLIST_SIZE(chunk_list, chunk_size_step);
      ASSERT_CHUNKLIST_DATA(chunk_list, data);
      if (cref == cref_match_first) {
        break;
      }
      cref = cref->next;
    }
    /* Happens when bytes are removed from the end of the array. */
    if (chunk_size_step == data_len_original) {
      return chunk_list;
    }

    i_prev = chunk_size_step;
  }
  else {
    i_prev = 0;
  }

  /* Fast-Path for end chunks
   * Check for trailing chunks. */
  /* In this case use 'chunk_list_ref_last' to define the last index
   * `index_match_last = -1`. */
  /* Warning, from now on don't use len(data) since we want to ignore chunks alrdy matched. */
  size_t data_len = data_len_original;
#define data_len_original invalid_usage
#ifdef data_len_original
  /* Quiet warning. */
#endif

  const ChunkRef *chunk_list_ref_last = NULL;

#ifdef USE_FASTPATH_CHUNKS_LAST
  if (!lib_list_is_empty(&chunk_list_ref->chunk_refs)) {
    const ChunkRef *cref = chunk_list_ref->chunk_refs.last;
    while ((cref->prev != NULL) && (cref != cref_match_first) &&
           (cref->link->data_len <= data_len - i_prev))
    {
      Chunk *chunk_test = cref->link;
      size_t offset = data_len - chunk_test->data_len;
      if (chunk_data_compare(chunk_test, data, data_len, offset)) {
        data_len = offset;
        chunk_list_ref_last = cref;
        chunk_list_ref_skip_len += 1;
        chunk_list_ref_skip_bytes += cref->link->data_len;
        cref = cref->prev;
      }
      else {
        break;
      }
    }
  }

  /* End Fast-Path (last) */
#endif /* USE_FASTPATH_CHUNKS_LAST */

  /* Check for aligned chunks
   * This saves a lot of searching, so use simple heuristics to detect aligned arrays.
   * (may need to tweak exact method). */
  bool use_aligned = false;

#ifdef USE_ALIGN_CHUNKS_TEST
  if (chunk_list->total_expanded_size == chunk_list_ref->total_expanded_size) {
    /* If we're alrdy a quarter aligned. */
    if (data_len - i_prev <= chunk_list->total_expanded_size / 4) {
      use_aligned = true;
    }
    else {
      /* TODO: walk over chunks and check if some arbitrary amount align. */
    }
  }
#endif /* USE_ALIGN_CHUNKS_TEST */

  /* End Aligned Chunk Case */
  if (use_aligned) {
    /* Copy matching chunks, creates using the same 'layout' as the ref. */
    const ChunkRef *cref = cref_match_first ? cref_match_first->next :
                                               chunk_list_ref->chunk_refs.first;
    while (i_prev != data_len) {
      const size_t i = i_prev + cref->link->data_len;
      lib_assert(i != i_prev);

      if ((cref != chunk_list_ref_last) &&
          chunk_data_compare(cref->link, data, data_len, i_prev)) {
        chunk_list_append(info, bs_mem, chunk_list, cref->link);
        ASSERT_CHUNKLIST_SIZE(chunk_list, i);
        ASSERT_CHUNKLIST_DATA(chunk_list, data);
      }
      else {
        chunk_list_append_data(info, bs_mem, chunk_list, &data[i_prev], i - i_prev);
        ASSERT_CHUNKLIST_SIZE(chunk_list, i);
        ASSERT_CHUNKLIST_DATA(chunk_list, data);
      }

      cref = cref->next;

      i_prev = i;
    }
  }
  else if ((data_len - i_prev >= info->chunk_byte_size) &&
           (chunk_list_ref->chunk_refs_len >= chunk_list_ref_skip_len) &&
           (chunk_list_ref->chunk_refs.first != NULL))
  {

    /* Non-Aligned Chunk De-Dup. */
    /* Only create a table if we have at least one chunk to search
     * otherwise just make a new one.
     * Support re-arranged chunks. */
#ifdef USE_HASH_TABLE_ACCUMULATE
    size_t i_table_start = i_prev;
    const size_t table_hash_array_len = (data_len - i_prev) / info->chunk_stride;
    hash_key *table_hash_array = mem_malloc(sizeof(*table_hash_array) * table_hash_array_len,
                                             __func__);
    hash_array_from_data(info, &data[i_prev], data_len - i_prev, table_hash_array);

    hash_accum(table_hash_array, table_hash_array_len, info->accum_steps);
#else
    /* Dummy vars. */
    uint i_table_start = 0;
    hash_key *table_hash_array = NULL;
#endif

    const uint chunk_list_ref_remaining_len = (chunk_list_reference->chunk_refs_len -
                                                     chunk_list_reference_skip_len) +
                                                    1;
    TableRef *table_ref_stack = mem_malloc(
        chunk_list_ref_remaining_len * sizeof(TableRef), __func__);
    uint table_ref_stack_n = 0;

    const size_t table_len = chunk_list_ref_remaining_len * BCHUNK_HASH_TABLE_MUL;
    TableRef **table = mem_calloc(table_len * sizeof(*table), __func__);

    /* Table_make inline
     * include one matching chunk, to allow for repeating vals. */
    {
#ifdef USE_HASH_TABLE_ACCUMULATE
      const size_t hash_store_len = info->accum_read_ahead_len;
      hash_key *hash_store = mem_malloc(sizeof(hash_key) * hash_store_len, __func__);
#endif

      const ChunkRef *cref;
      size_t chunk_list_ref_bytes_remaining = chunk_list_ref->total_expanded_size -
                                                    chunk_list_ref_skip_bytes;

      if (cref_match_first) {
        cref = cref_match_first;
        chunk_list_ref_bytes_remaining += cref->link->data_len;
      }
      else {
        cref = chunk_list_ref->chunk_refs.first;
      }

#ifdef USE_PARANOID_CHECKS
      {
        size_t test_bytes_len = 0;
        const ChunkRef *cr = cref;
        while (cr != chunk_list_ref_last) {
          test_bytes_len += cr->link->data_len;
          cr = cr->next;
        }
        lib_assert(test_bytes_len == chunk_list_ref_bytes_remaining);
      }
#endif

      while ((cref != chunk_list_ref_last) &&
             (chunk_list_ref_bytes_remaining >= info->accum_read_ahead_bytes))
      {
        hash_key key = key_from_chunk_ref(info,
                                          cref

#ifdef USE_HASH_TABLE_ACCUMULATE
                                          ,
                                          hash_store,
                                          hash_store_len
#endif
        );
        const uint key_index = (uint)(key % (hash_key)table_len);
        TableRef *tref_prev = table[key_index];
        lib_assert(table_ref_stack_n < chunk_list_ref_remaining_len);
#ifdef USE_HASH_TABLE_DEDUP
        bool is_dup = false;
        if (tref_prev) {
          const Chunk *chunk_a = cref->link;
          const TableRef *tref = tref_prev;
          do {
            /* Not an err, it just isn't expected the links are ever shared. */
            lib_assert(tref->cref != cref);
            const Chunk *chunk_b = tref->cref->link;
#  ifdef USE_HASH_TABLE_KEY_CACHE
            if (key == chunk_b->key)
#  endif
            {
              if (chunk_a != chunk_b) {
                if (chunk_a->data_len == chunk_b->data_len) {
                  if (memcmp(chunk_a->data, chunk_b->data, chunk_a->data_len) == 0) {
                    is_dup = true;
                    break;
                  }
                }
              }
            }
          } while ((tref = tref->next));
        }

        if (!is_dup)
#endif /* USE_HASH_TABLE_DEDUP */
        {
          TableRef *tref = &table_ref_stack[table_ref_stack_n++];
          tref->cref = cref;
          tref->next = tref_prev;
          table[key_index] = tref;
        }

        chunk_list_ref_bytes_remaining -= cref->link->data_len;
        cref = cref->next;
      }

      lib_assert(table_ref_stack_n <= chunk_list_ref_remaining_len);

#ifdef USE_HASH_TABLE_ACCUMULATE
      mem_free(hash_store);
#endif
    }
    /* Done making the table. */
    lib_assert(i_prev <= data_len);
    for (size_t i = i_prev; i < data_len;) {
      /* Assumes exiting chunk isn't a match! */

      const ChunkRef *cref_found = table_lookup(
          info, table, table_len, i_table_start, data, data_len, i, table_hash_array);
      if (cref_found != NULL) {
        lib_assert(i < data_len);
        if (i != i_prev) {
          chunk_list_append_data_n(info, bs_mem, chunk_list, &data[i_prev], i - i_prev);
          i_prev = i;
        }

        /* Now add the ref chunk. */
        {
          Chunk *chunk_found = cref_found->link;
          i += chunk_found->data_len;
          chunk_list_append(info, bs_mem, chunk_list, chunk_found);
        }
        i_prev = i;
        lib_assert(i_prev <= data_len);
        ASSERT_CHUNKLIST_SIZE(chunk_list, i_prev);
        ASSERT_CHUNKLIST_DATA(chunk_list, data);

        /* Its likely that the next chunk in the list will be a match, so check it! */
        while (!ELEM(cref_found->next, NULL, chunk_list_ref_last)) {
          cref_found = cref_found->next;
          Chunk *chunk_found = cref_found->link;

          if (chunk_data_compare(chunk_found, data, data_len, i_prev)) {
            /* May be useful to remove table data, assuming we don't have
             * repeating mem where it would be useful to re-use chunks. */
            i += chunk_found->data_len;
            chunk_list_append(info, bs_mem, chunk_list, chunk_found);
            /* Chunk_found may be freed! */
            i_prev = i;
            lib_assert(i_prev <= data_len);
            ASSERT_CHUNKLIST_SIZE(chunk_list, i_prev);
            ASSERT_CHUNKLIST_DATA(chunk_list, data);
          }
          else {
            break;
          }
        }
      }
      else {
        i = i + info->chunk_stride;
      }
    }

#ifdef USE_HASH_TABLE_ACCUMULATE
    mem_free(table_hash_array);
#endif
    mem_free(table);
    mem_free(table_ref_stack);

    /* End Table Lookup */
  }

  ASSERT_CHUNKLIST_SIZE(chunk_list, i_prev);
  ASSERT_CHUNKLIST_DATA(chunk_list, data);

  /* No Dups to copy, write new chunks
   * Trailing chunks, no matches found in table lookup above.
   * Write all new data. */
  if (i_prev != data_len) {
    chunk_list_append_data_n(info, bs_mem, chunk_list, &data[i_prev], data_len - i_prev);
    i_prev = data_len;
  }

  lib_assert(i_prev == data_len);

#ifdef USE_FASTPATH_CHUNKS_LAST
  if (chunk_list_ref_last != NULL) {
    /* Write chunk_list_ref_last since it hasn't been written yet. */
    const ChunkRef *cref = chunk_list_ref_last;
    while (cref != NULL) {
      Chunk *chunk = cref->link;
      // lib_assert(chunk_data_compare(chunk, data, data_len, i_prev));
      i_prev += chunk->data_len;
      /* Use simple since we assume the refs chunks have alrdy been sized correctly. */
      chunk_list_append_only(bs_mem, chunk_list, chunk);
      ASSERT_CHUNKLIST_DATA(chunk_list, data);
      cref = cref->next;
    }
  }
#endif

#undef data_len_original

  lib_assert(i_prev == data_len_original);

  /* Check we're the correct size and that we didn't accidentally modify the ref. */
  ASSERT_CHUNKLIST_SIZE(chunk_list, data_len_original);
  ASSERT_CHUNKLIST_SIZE(chunk_list_ref, chunk_list_ref->total_expanded_size);

  ASSERT_CHUNKLIST_DATA(chunk_list, data);

  return chunk_list;
}
/* End private API. */
/* Main Array Storage API */
ArrayStore *lib_array_store_create(uint stride, uint chunk_count)
{
  lib_assert(stride > 0 && chunk_count > 0);

  ArrayStore *bs = mem_calloc(sizeof(ArrayStore), __func__);

  bs->info.chunk_stride = stride;
  // bs->info.chunk_count = chunk_count;

  bs->info.chunk_byte_size = chunk_count * stride;
#ifdef USE_MERGE_CHUNKS
  bs->info.chunk_byte_size_min = MAX2(1u, chunk_count / CHUNK_SIZE_MIN_DIV) * stride;
  bs->info.chunk_byte_size_max = (chunk_count * CHUNK_SIZE_MAX_MUL) * stride;
#endif

#ifdef USE_HASH_TABLE_ACCUMULATE
  /* One is always subtracted from this `accum_steps`, this is intentional
   * as it results in reading ahead the expected amount. */
  if (stride <= sizeof(int8_t)) {
    bs->info.accum_steps = CHUNK_HASH_TABLE_ACCUMULATE_STEPS_8BITS + 1;
  }
  else if (stride <= sizeof(int16_t)) {
    bs->info.accum_steps = CHUNK_HASH_TABLE_ACCUMULATE_STEPS_16BITS + 1;
  }
  else if (stride <= sizeof(int32_t)) {
    bs->info.accum_steps = CHUNK_HASH_TABLE_ACCUMULATE_STEPS_32BITS + 1;
  }
  else {
    bs->info.accum_steps = CHUNK_HASH_TABLE_ACCUMULATE_STEPS_DEFAULT + 1;
  }

  do {
    bs->info.accum_steps -= 1;
    /* Triangle num, identifying now much read-ahead we need:
     * https://en.wikipedia.org/wiki/Triangular_number (+ 1) */
    bs->info.accum_read_ahead_len = ((bs->info.accum_steps * (bs->info.accum_steps + 1)) / 2) + 1;
    /* Only small chunk counts are likely to exceed the read-ahead length. */
  } while (UNLIKELY(chunk_count < bs->info.accum_read_ahead_len));

  bs->info.accum_read_ahead_bytes = bs->info.accum_read_ahead_len * stride;
#else
  bs->info.accum_read_ahead_bytes = MIN2((size_t)CHUNK_HASH_LEN, chunk_count) * stride;
#endif

  bs->mem.chunk_list = lib_mempool_create(sizeof(ChunkList), 0, 512, LIB_MEMPOOL_NOP);
  bs->mem.chunk_ref = lib_mempool_create(sizeof(ChunkRef), 0, 512, LIB_MEMPOOL_NOP);
  /* Allow iter to simplify freeing, otherwise its not needed
   * (we could loop over all states as an alt). */
  bs->mem.chunk = lib_mempool_create(sizeof(Chunk), 0, 512, LIB_MEMPOOL_ALLOW_ITER);

  lib_assert(bs->info.accum_read_ahead_bytes <= bs->info.chunk_byte_size);

  return bs;
}

static void array_store_free_data(ArrayStore *bs)
{
  /* Free chunk data. */
  {
    lib_mempool_iter iter;
    Chunk *chunk;
    lib_mempool_iternew(bs->mem.chunk, &iter);
    while ((chunk = lib_mempool_iterstep(&iter))) {
      lib_assert(chunk->users > 0);
      mem_free((void *)chunk->data);
    }
  }

  /* Free states. */
  for (ArrayState *state = bs->states.first, *state_next; state; state = state_next) {
    state_next = state->next;
    mem_free(state);
  }
}

void lib_array_store_destroy(ArrayStore *bs)
{
  array_store_free_data(bs);

  lib_mempool_destroy(bs->mem.chunk_list);
  lib_mempool_destroy(bs->mem.chunk_ref);
  lib_mempool_destroy(bs->mem.chunk);

  mem_free(bs);
}

void lib_array_store_clear(ArrayStore *bs)
{
  array_store_free_data(bs);

  lib_list_clear(&bs->states);

  lib_mempool_clear(bs->mem.chunk_list);
  lib_mempool_clear(bs->mem.chunk_ref);
  lib_mempool_clear(bs->mem.chunk);
}

/* ArrayStore Statistics */
size_t lib_array_store_calc_size_expanded_get(const ArrayStore *bs)
{
  size_t size_accum = 0;
  LIST_FOREACH (const ArrayState *, state, &bs->states) {
    size_accum += state->chunk_list->total_expanded_size;
  }
  return size_accum;
}

size_t lib_array_store_calc_size_compacted_get(const ArrayStore *bs)
{
  size_t size_total = 0;
  lib_mempool_iter iter;
  Chunk *chunk;
  lib_mempool_iternew(bs->memory.chunk, &iter);
  while ((chunk = lib_mempool_iterstep(&iter))) {
    lib_assert(chunk->users > 0);
    size_total += (size_t)chunk->data_len;
  }
  return size_total;
}

/* ArrayState Access */
ArrayState *lib_array_store_state_add(ArrayStore *bs,
                                       const void *data,
                                       const size_t data_len,
                                       const ArrayState *state_ref)
{
  /* Ensure we're aligned to the stride. */
  lib_assert((data_len % bs->info.chunk_stride) == 0);

#ifdef USE_PARANOID_CHECKS
  if (state_ref) {
    lib_assert(lib_findindex(&bs->states, state_ref) != -1);
  }
#endif

  ChunkList *chunk_list;
  if (state_ref) {
    chunk_list = chunk_list_from_data_merge(&bs->info,
                                             &bs->mem,
                                             (const uchar *)data,
                                             data_len,
                                             /* Re-use ref chunks. */
                                             state_ref->chunk_list);
  }
  else {
    chunk_list = chunk_list_new(&bs->memory, data_len);
    chunk_list_fill_from_array(&bs->info, &bs->memory, chunk_list, (const uchar *)data, data_len);
  }

  chunk_list->users += 1;

  ArrayState *state = mem_calloc(sizeof(ArrayState), __func__);
  state->chunk_list = chunk_list;

  lib_addtail(&bs->states, state);

#ifdef USE_PARANOID_CHECKS
  {
    size_t data_test_len;
    void *data_test = lib_array_store_state_data_get_alloc(state, &data_test_len);
    lib_assert(data_test_len == data_len);
    lib_assert(memcmp(data_test, data, data_len) == 0);
    mem_free(data_test);
  }
#endif

  return state;
}

void lib_array_store_state_remove(ArrayStore *bs, ArrayState *state)
{
#ifdef USE_PARANOID_CHECKS
  lib_assert(lib_findindex(&bs->states, state) != -1);
#endif

  chunk_list_decref(&bs->memory, state->chunk_list);
  lib_remlink(&bs->states, state);

  mem_free(state);
}

size_t lib_array_store_state_size_get(ArrayState *state)
{
  return state->chunk_list->total_expanded_size;
}

void lib_array_store_state_data_get(ArrayState *state, void *data)
{
#ifdef USE_PARANOID_CHECKS
  size_t data_test_len = 0;
  LIST_FOREACH (ChunkRef *, cref, &state->chunk_list->chunk_refs) {
    data_test_len += cref->link->data_len;
  }
  lib_assert(data_test_len == state->chunk_list->total_expanded_size);
#endif

  uchar *data_step = (uchar *)data;
  LIST_FOREACH (ChunkRef *, cref, &state->chunk_list->chunk_refs) {
    lib_assert(cref->link->users > 0);
    memcpy(data_step, cref->link->data, cref->link->data_len);
    data_step += cref->link->data_len;
  }
}

void *lib_array_store_state_data_get_alloc(ArrayState *state, size_t *r_data_len)
{
  void *data = mem_malloc(state->chunk_list->total_expanded_size, __func__);
  lib_array_store_state_data_get(state, data);
  *r_data_len = state->chunk_list->total_expanded_size;
  return data;
}

/* Debugging API (for testing). */
/* Only for test validation. */
static size_t chunk_list_size(const ChunkList *chunk_list)
{
  size_t total_expanded_size = 0;
  LIST_FOREACH (ChunkRef *, cref, &chunk_list->chunk_refs) {
    total_expanded_size += cref->link->data_len;
  }
  return total_expanded_size;
}

bool lib_array_store_is_valid(ArrayStore *bs)
{
  bool ok = true;

  /* Check Length */
  LIST_FOREACH (ArrayState *, state, &bs->states) {
    ChunkList *chunk_list = state->chunk_list;
    if (!(chunk_list_size(chunk_list) == chunk_list->total_expanded_size)) {
      return false;
    }

    if (lib_list_count(&chunk_list->chunk_refs) != (int)chunk_list->chunk_refs_len) {
      return false;
    }

#ifdef USE_MERGE_CHUNKS
    /* Ensure we merge all chunks that could be merged. */
    if (chunk_list->total_expanded_size > bs->info.chunk_byte_size_min) {
      LIST_FOREACH (ChunkRef *, cref, &chunk_list->chunk_refs) {
        if (cref->link->data_len < bs->info.chunk_byte_size_min) {
          return false;
        }
      }
    }
#endif
  }

  {
    lib_empool_iter iter;
    Chunk *chunk;
    lib_mempool_iternew(bs->mem.chunk, &iter);
    while ((chunk = lib_mempool_iterstep(&iter))) {
      if (!(mem_alloc_len(chunk->data) >= chunk->data_len)) {
        return false;
      }
    }
  }

  /* Check User Count & Lost Ref */
  {
    GHashIter gh_iter;

#define GHASH_PTR_ADD_USER(gh, pt) \
  { \
    void **val; \
    if (lib_ghash_ensure_p((gh), (pt), &val)) { \
      *((int *)val) += 1; \
    } \
    else { \
      *((int *)val) = 1; \
    } \
  } \
  ((void)0)

    /* Count chunk_list's. */
    GHash *chunk_list_map = lib_ghash_ptr_new(__func__);
    GHash *chunk_map = lib_ghash_ptr_new(__func__);

    int totrefs = 0;
    LIST_FOREACH (ArrayState *, state, &bs->states) {
      GHASH_PTR_ADD_USER(chunk_list_map, state->chunk_list);
    }
    GHASH_ITER (gh_iter, chunk_list_map) {
      const ChunkList *chunk_list = lib_ghashIter_getKey(&gh_iter);
      const int users = PTR_AS_INT(lib_ghashIter_getVal(&gh_iter));
      if (!(chunk_list->users == users)) {
        ok = false;
        goto user_finally;
      }
    }
    if (!(lib_mempool_len(bs->mem.chunk_list) == (int)lib_ghash_len(chunk_list_map))) {
      ok = false;
      goto user_finally;
    }

    /* Count chunk's. */
    GHASH_ITER (gh_iter, chunk_list_map) {
      const ChunkList *chunk_list = lib_ghashIter_getKey(&gh_iter);
      LIST_FOREACH (const ChunkRef *, cref, &chunk_list->chunk_refs) {
        GHASH_PTR_ADD_USER(chunk_map, cref->link);
        totrefs += 1;
      }
    }
    if (!(lib_mempool_len(bs->mem.chunk) == (int)lib_ghash_len(chunk_map))) {
      ok = false;
      goto user_finally;
    }
    if (!(lib_mempool_len(bs->mem.chunk_ref) == totrefs)) {
      ok = false;
      goto user_finally;
    }

    GHASH_ITER (gh_iter, chunk_map) {
      const Chunk *chunk = lib_ghashIter_getKey(&gh_iter);
      const int users = PTR_AS_INT(lib_ghashIter_getVal(&gh_iter));
      if (!(chunk->users == users)) {
        ok = false;
        goto user_finally;
      }
    }

#undef GHASH_PTR_ADD_USER

  user_finally:
    lib_ghash_free(chunk_list_map, NULL, NULL);
    lib_ghash_free(chunk_map, NULL, NULL);
  }

  return ok;
  /* TODO: dangling ptr checks. */
}
