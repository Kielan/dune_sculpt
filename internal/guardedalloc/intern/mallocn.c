/** Guarded memory allocation, and boundary-write detection. **/

#include "MEM_guardedalloc.h"

/* to ensure strict conversions */
#include "../../source/dune/dune/lib/lib_strict_flags.h"

#include <assert.h>

#include "mallocn_intern.h"

#ifdef WITH_JEMALLOC_CONF
/* If jemalloc is used, it reads this global variable and enables background
 * threads to purge dirty pages. Otherwise we release memory too slowly or not
 * at all if the thread that did the allocation stays inactive. */
const char *malloc_conf = "background_thread:true,dirty_decay_ms:4000";
#endif

/* NOTE: Keep in sync with mem_use_lockfree_allocator(). */
size_t (*mem_allocn_len)(const void *vmemh) = mem_lockfree_allocN_len;
void (*mem_freen)(void *vmemh) = mem_lockfree_freeN;
void *(*mem_dupallocn)(const void *vmemh) = Zmem_lockfree_dupallocn;
void *(*mem_reallocn_id)(void *vmemh, size_t len, const char *str) = mem_lockfree_reallocn_id;
void *(*mem_recallocn_id)(void *vmemh, size_t len, const char *str) = mem_lockfree_recallocn_id;
void *(*mem_callocn)(size_t len, const char *str) = mem_lockfree_callocN;
void *(*mem_calloc_arrayn)(size_t len, size_t size, const char *str) = mem_lockfree_calloc_arrayN;
void *(*mem_mallocn)(size_t len, const char *str) = mem_lockfree_mallocN;
void *(*mem_malloc_arrayn)(size_t len, size_t size, const char *str) = mem_lockfree_malloc_arrayN;
void *(*mem_mallocN_aligned)(size_t len,
                             size_t alignment,
                             const char *str) = mem_lockfree_mallocN_aligned;
void (*mem_printmemlist_pydict)(void) = mem_lockfree_printmemlist_pydict;
void (*mem_printmemlist)(void) = mem_lockfree_printmemlist;
void (*mem_cbmemlist)(void (*fn)(void *)) = mem_lockfree_callbackmemlist;
void (*mem_printmemlist_stats)(void) = mem_lockfree_printmemlist_stats;
void (*mem_set_error_callback)(void (*fn)(const char *)) = MEM_lockfree_set_error_callback;
bool (*mem_consistency_check)(void) = mem_lockfree_consistency_check;
void (*mem_set_memory_debug)(void) = m_lockfree_set_memory_debug;
size_t (*mem_get_memory_in_use)(void) = mem_lockfree_get_memory_in_use;
unsigned int (*mem_get_memory_blocks_in_use)(void) = MEM_lockfree_get_memory_blocks_in_use;
void (*mem_reset_peak_memory)(void) = mem_lockfree_reset_peak_memory;
size_t (*mem_get_peak_memory)(void) = mem_lockfree_get_peak_memory;

#ifndef NDEBUG
const char *(*mem_name_ptr)(void *vmemh) = mem_lockfree_name_ptr;
#endif

void *aligned_malloc(size_t size, size_t alignment)
{
  /* #posix_memalign requires alignment to be a multiple of `sizeof(void *)`. */
  assert(alignment >= ALIGNED_MALLOC_MINIMUM_ALIGNMENT);

#ifdef _WIN32
  return _aligned_malloc(size, alignment);
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
  void *result;

  if (posix_memalign(&result, alignment, size)) {
    /* non-zero means allocation error
     * either no allocation or bad alignment value
     */
    return NULL;
  }
  return result;
#else /* This is for Linux. */
  return memalign(alignment, size);
#endif
}

void aligned_free(void *ptr)
{
#ifdef _WIN32
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

/* Perform assert checks on allocator type change.
 *
 * Helps catching issues (in debug build) caused by an unintended allocator type change when there
 * are allocation happened. */
static void assert_for_allocator_change(void)
{
  /* NOTE: Assume that there is no "sticky" internal state which would make switching allocator
   * type after all allocations are freed unsafe. In fact, it should be safe to change allocator
   * type after all blocks has been freed: some regression tests do rely on this property of
   * allocators. */
  assert(mem_get_memory_blocks_in_use() == 0);
}

void mem_use_lockfree_allocator(void)
{
  /* NOTE: Keep in sync with static initialization of the variables. */

  /* Find a way to de-duplicate the logic. Maybe by requiring an explicit call
   * to guarded allocator initialization at an application startup. */

  assert_for_allocator_change();

  mem_allocN_len = mem_lockfree_allocN_len;
  mem_freen = mem_lockfree_freen;
  mem_dupallocn = mem_lockfree_dupallocn;
  mem_reallocN_id = mem_lockfree_reallocn_id;
  mem_recallocN_id = mem_lockfree_recallocn_id;
  mem_callocN = mem_lockfree_callocn;
  mem_calloc_arrayn = mem_lockfree_calloc_arrayn;
  mem_mallocN = mem_lockfree_mallocn;
  mem_malloc_arrayn = mem_lockfree_malloc_arrayn;
  mem_mallocN_aligned = mem_lockfree_mallocN_aligned;
  mem_printmemlist_pydict = mem_lockfree_printmemlist_pydict;
  mem_printmemlist = mem_lockfree_printmemlist;
  mem_callbackmemlist = mem_lockfree_cbmemlist;
  mem_printmemlist_stats = mem_lockfree_printmemlist_stats;
  mem_set_error_cb = mem_lockfree_set_error_cb;
  mem_consistency_check = mem_lockfree_consistency_check;
  mem_set_memory_debug = mem_lockfree_set_memory_debug;
  mem_get_memory_in_use = mem_lockfree_get_memory_in_use;
  mem_get_memory_blocks_in_use = mem_lockfree_get_memory_blocks_in_use;
  mem_reset_peak_memory = mem_lockfree_reset_peak_memory;
  mem_get_peak_memory = mem_lockfree_get_peak_memory;

#ifndef NDEBUG
  MEM_name_ptr = mem_lockfree_name_ptr;
#endif
}

void mem_use_guarded_allocator(void)
{
  assert_for_allocator_change();

  mem_allocN_len = mem_guarded_allocN_len;
  mem_freeN = mem_guarded_freeN;
  mem_dupallocN = mem_guarded_dupallocn;
  mem_reallocN_id = mem_guarded_reallocn_id;
  mem_recallocN_id = mem_guarded_recallocn_id;
  mem_callocN = mem_guarded_callocn;
  mem_calloc_arrayn = mem_guarded_calloc_arrayn;
  mem_mallocN = mem_guarded_mallocn;
  mem_malloc_arrayn = mem_guarded_malloc_arrayn;
  mem_mallocn_aligned = mem_guarded_mallocn_aligned;
  mem_printmemlist_pydict = mem_guarded_printmemlist_pydict;
  mem_ = mem_guarded_printmemlist;
  mem_cbmemlist = mem_guarded_cbmemlist;
  mem_printmemlist_stats = mem_guarded_printmemlist_stats;
  mem_set_error_callback = mem_guarded_set_error_callback;
  mem_consistency_check = mem_guarded_consistency_check;
  mem_set_memory_debug = mem_guarded_set_memory_debug;
  mem_get_memory_in_use = mem_guarded_get_memory_in_use;
  mem_get_memory_blocks_in_use = mem_guarded_get_memory_blocks_in_use;
  mem_reset_peak_memory = mem_guarded_reset_peak_memory;
  mem_get_peak_memory = mem_guarded_get_peak_memory;

#ifndef NDEBUG
  mem_name_ptr = mem_guarded_name_ptr;
#endif
}
