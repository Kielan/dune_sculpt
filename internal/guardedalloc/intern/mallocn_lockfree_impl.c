/** Memory allocation which keeps track on allocated memory counters */

#include <stdarg.h>
#include <stdio.h> /* printf */
#include <stdlib.h>
#include <string.h> /* memcpy */
#include <sys/types.h>

#include "mem_guardedalloc.h"

/* to ensure strict conversions */
#include "../../source/dune/dunelib/lib_strict_flags.h"

#include "atomic_ops.h"
#include "mallocn_intern.h"

typedef struct MemHead {
  /* Length of allocated memory block. */
  size_t len;
} MemHead;

typedef struct MemHeadAligned {
  short alignment;
  size_t len;
} MemHeadAligned;

static unsigned int totblock = 0;
static size_t mem_in_use = 0, peak_mem = 0;
static bool malloc_debug_memset = false;

static void (*error_cb)(const char *) = NULL;

enum {
  MEMHEAD_ALIGN_FLAG = 1,
};

#define MEMHEAD_FROM_PTR(ptr) (((MemHead *)ptr) - 1)
#define PTR_FROM_MEMHEAD(memhead) (memhead + 1)
#define MEMHEAD_ALIGNED_FROM_PTR(ptr) (((MemHeadAligned *)ptr) - 1)
#define MEMHEAD_IS_ALIGNED(memhead) ((memhead)->len & (size_t)MEMHEAD_ALIGN_FLAG)

/* Uncomment this to have proper peak counter. */
#define USE_ATOMIC_MAX

MEM_INLINE void update_maximum(size_t *maximum_value, size_t value)
{
#ifdef USE_ATOMIC_MAX
  atomic_fetch_and_update_max_z(maximum_value, value);
#else
  *maximum_value = value > *maximum_value ? value : *maximum_value;
#endif
}

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
static void
print_error(const char *str, ...)
{
  char buf[512];
  va_list ap;

  va_start(ap, str);
  vsnprintf(buf, sizeof(buf), str, ap);
  va_end(ap);
  buf[sizeof(buf) - 1] = '\0';

  if (error_callback) {
    error_callback(buf);
  }
}

size_t men_lockfree_allocn_len(const void *vmemh)
{
  if (vmemh) {
    return MEMHEAD_FROM_PTR(vmemh)->len & ~((size_t)(MEMHEAD_ALIGN_FLAG));
  }

  return 0;
}

void mem_lockfree_freen(void *vmemh)
{
  if (leak_detector_has_run) {
    print_error("%s\n", free_after_leak_detection_message);
  }

  MemHead *memh = MEMHEAD_FROM_PTR(vmemh);
  size_t len = MEM_lockfree_allocN_len(vmemh);

  if (vmemh == NULL) {
    print_error("Attempt to free NULL pointer\n");
#ifdef WITH_ASSERT_ABORT
    abort();
#endif
    return;
  }

  atomic_sub_and_fetch_u(&totblock, 1);
  atomic_sub_and_fetch_z(&mem_in_use, len);

  if (UNLIKELY(malloc_debug_memset && len)) {
    memset(memh + 1, 255, len);
  }
  if (UNLIKELY(MEMHEAD_IS_ALIGNED(memh))) {
    MemHeadAligned *memh_aligned = MEMHEAD_ALIGNED_FROM_PTR(vmemh);
    aligned_free(MEMHEAD_REAL_PTR(memh_aligned));
  }
  else {
    free(memh);
  }
}

void *mem_lockfree_dupallocn(const void *vmemh)
{
  void *newp = NULL;
  if (vmemh) {
    MemHead *memh = MEMHEAD_FROM_PTR(vmemh);
    const size_t prev_size = mem_lockfree_allocN_len(vmemh);
    if (UNLIKELY(MEMHEAD_IS_ALIGNED(memh))) {
      MemHeadAligned *memh_aligned = MEMHEAD_ALIGNED_FROM_PTR(vmemh);
      newp = mem_lockfree_mallocn_aligned(
          prev_size, (size_t)memh_aligned->alignment, "dupli_malloc");
    }
    else {
      newp = mem_lockfree_mallocn(prev_size, "dupli_malloc");
    }
    memcpy(newp, vmemh, prev_size);
  }
  return newp;
}

void *mem_lockfree_reallocn_id(void *vmemh, size_t len, const char *str)
{
  void *newp = NULL;

  if (vmemh) {
    MemHead *memh = MEMHEAD_FROM_PTR(vmemh);
    size_t old_len = mem_lockfree_allocn_len(vmemh);

    if (LIKELY(!MEMHEAD_IS_ALIGNED(memh))) {
      newp = mem_lockfree_mallocn(len, "realloc");
    }
    else {
      MemHeadAligned *memh_aligned = MEMHEAD_ALIGNED_FROM_PTR(vmemh);
      newp = mem_lockfree_mallocn_aligned(len, (size_t)memh_aligned->alignment, "realloc");
    }

    if (newp) {
      if (len < old_len) {
        /* shrink */
        memcpy(newp, vmemh, len);
      }
      else {
        /* grow (or remain same size) */
        memcpy(newp, vmemh, old_len);
      }
    }

    mem_lockfree_freen(vmemh);
  }
  else {
    newp = mem_lockfree_mallocn(len, str);
  }

  return newp;
}

void *mem_lockfree_recallocn_id(void *vmemh, size_t len, const char *str)
{
  void *newp = NULL;

  if (vmemh) {
    MemHead *memh = MEMHEAD_FROM_PTR(vmemh);
    size_t old_len = MEM_lockfree_allocN_len(vmemh);

    if (LIKELY(!MEMHEAD_IS_ALIGNED(memh))) {
      newp = MEM_lockfree_mallocN(len, "recalloc");
    }
    else {
      MemHeadAligned *memh_aligned = MEMHEAD_ALIGNED_FROM_PTR(vmemh);
      newp = mem_lockfree_mallocn_aligned(len, (size_t)memh_aligned->alignment, "recalloc");
    }

    if (newp) {
      if (len < old_len) {
        /* shrink */
        memcpy(newp, vmemh, len);
      }
      else {
        memcpy(newp, vmemh, old_len);

        if (len > old_len) {
          /* grow */
          /* zero new bytes */
          memset(((char *)newp) + old_len, 0, len - old_len);
        }
      }
    }

    mem_lockfree_freen(vmemh);
  }
  else {
    newp = mem_lockfree_callocn(len, str);
  }

  return newp;
}

void *mem_lockfree_callocn(size_t len, const char *str)
{
  MemHead *memh;

  len = SIZET_ALIGN_4(len);

  memh = (MemHead *)calloc(1, len + sizeof(MemHead));

  if (LIKELY(memh)) {
    memh->len = len;
    atomic_add_and_fetch_u(&totblock, 1);
    atomic_add_and_fetch_z(&mem_in_use, len);
    update_maximum(&peak_mem, mem_in_use);

    return PTR_FROM_MEMHEAD(memh);
  }
  print_error("Calloc returns null: len=" SIZET_FORMAT " in %s, total %u\n",
              SIZET_ARG(len),
              str,
              (unsigned int)mem_in_use);
  return NULL;
}

void *mem_lockfree_calloc_arrayn(size_t len, size_t size, const char *str)
{
  size_t total_size;
  if (UNLIKELY(!MEM_size_safe_multiply(len, size, &total_size))) {
    print_error(
        "Calloc array aborted due to integer overflow: "
        "len=" SIZET_FORMAT "x" SIZET_FORMAT " in %s, total %u\n",
        SIZET_ARG(len),
        SIZET_ARG(size),
        str,
        (unsigned int)mem_in_use);
    abort();
    return NULL;
  }

  return mem_lockfree_callocn(total_size, str);
}

void *mem_lockfree_mallocn(size_t len, const char *str)
{
  MemHead *memh;

  len = SIZET_ALIGN_4(len);

  memh = (MemHead *)malloc(len + sizeof(MemHead));

  if (LIKELY(memh)) {
    if (UNLIKELY(malloc_debug_memset && len)) {
      memset(memh + 1, 255, len);
    }

    memh->len = len;
    atomic_add_and_fetch_u(&totblock, 1);
    atomic_add_and_fetch_z(&mem_in_use, len);
    update_maximum(&peak_mem, mem_in_use);

    return PTR_FROM_MEMHEAD(memh);
  }
  print_error("Malloc returns null: len=" SIZET_FORMAT " in %s, total %u\n",
              SIZET_ARG(len),
              str,
              (unsigned int)mem_in_use);
  return NULL;
}

void *mem_lockfree_malloc_arrayn(size_t len, size_t size, const char *str)
{
  size_t total_size;
  if (UNLIKELY(!MEM_size_safe_multiply(len, size, &total_size))) {
    print_error(
        "Malloc array aborted due to integer overflow: "
        "len=" SIZET_FORMAT "x" SIZET_FORMAT " in %s, total %u\n",
        SIZET_ARG(len),
        SIZET_ARG(size),
        str,
        (unsigned int)mem_in_use);
    abort();
    return NULL;
  }

  return mem_lockfree_mallocN(total_size, str);
}

void *mem_lockfree_mallocn_aligned(size_t len, size_t alignment, const char *str)
{
  /* Huge alignment values doesn't make sense and they wouldn't fit into 'short' used in the
   * MemHead. */
  assert(alignment < 1024);

  /* We only support alignments that are a power of two. */
  assert(IS_POW2(alignment));

  /* Some OS specific aligned allocators require a certain minimal alignment. */
  if (alignment < ALIGNED_MALLOC_MINIMUM_ALIGNMENT) {
    alignment = ALIGNED_MALLOC_MINIMUM_ALIGNMENT;
  }

  /* It's possible that MemHead's size is not properly aligned,
   * do extra padding to deal with this.
   *
   * We only support small alignments which fits into short in
   * order to save some bits in MemHead structure.
   */
  size_t extra_padding = MEMHEAD_ALIGN_PADDING(alignment);

  len = SIZET_ALIGN_4(len);

  MemHeadAligned *memh = (MemHeadAligned *)aligned_malloc(
      len + extra_padding + sizeof(MemHeadAligned), alignment);

  if (LIKELY(memh)) {
    /* We keep padding in the beginning of MemHead,
     * this way it's always possible to get MemHead
     * from the data pointer.
     */
    memh = (MemHeadAligned *)((char *)memh + extra_padding);

    if (UNLIKELY(malloc_debug_memset && len)) {
      memset(memh + 1, 255, len);
    }

    memh->len = len | (size_t)MEMHEAD_ALIGN_FLAG;
    memh->alignment = (short)alignment;
    atomic_add_and_fetch_u(&totblock, 1);
    atomic_add_and_fetch_z(&mem_in_use, len);
    update_maximum(&peak_mem, mem_in_use);

    return PTR_FROM_MEMHEAD(memh);
  }
  print_error("Malloc returns null: len=" SIZET_FORMAT " in %s, total %u\n",
              SIZET_ARG(len),
              str,
              (unsigned int)mem_in_use);
  return NULL;
}

void mem_lockfree_printmemlist_pydict(void)
{
}

void mem_lockfree_printmemlist(void)
{
}

/* unused */
void mem_lockfree_cbmemlist(void (*func)(void *))
{
  (void)func; /* Ignored. */
}

void mem_lockfree_printmemlist_stats(void)
{
  printf("\ntotal memory len: %.3f MB\n", (double)mem_in_use / (double)(1024 * 1024));
  printf("peak memory len: %.3f MB\n", (double)peak_mem / (double)(1024 * 1024));
  printf(
      "\nFor more detailed per-block statistics run Blender with memory debugging command line "
      "argument.\n");

#ifdef HAVE_MALLOC_STATS
  printf("System Statistics:\n");
  malloc_stats();
#endif
}

void mem_lockfree_set_error_cb(void (*fn)(const char *))
{
  error_cb = fn;
}

bool mem_lockfree_consistency_check(void)
{
  return true;
}

void mem_lockfree_set_memory_debug(void)
{
  malloc_debug_memset = true;
}

size_t mem_lockfree_get_memory_in_use(void)
{
  return mem_in_use;
}

unsigned int mem_lockfree_get_memory_blocks_in_use(void)
{
  return totblock;
}

/* dummy */
void mem_lockfree_reset_peak_memory(void)
{
  peak_mem = mem_in_use;
}

size_t mem_lockfree_get_peak_memory(void)
{
  return peak_mem;
}

#ifndef NDEBUG
const char *mem_lockfree_name_ptr(void *vmemh)
{
  if (vmemh) {
    return "unknown block name ptr";
  }

  return "mem_lockfree_name_ptr(NULL)";
}
#endif /* NDEBUG */
