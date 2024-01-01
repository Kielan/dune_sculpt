/* A min-heap/priority queue ADT.
 * Simplified v of the heap, only supports insert + removal from top.
 * See lib_heap.c for a more full featured heap implementation */
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_heap_simple.h"
#include "lib_strict_flags.h"
#include "lib_utildefines.h"

#define HEAP_PARENT(i) (((i)-1) >> 1)

/* HeapSimple Intern Structs */
typedef struct HeapSimpleNode {
  float val;
  void *ptr;
} HeapSimpleNode;

struct HeapSimple {
  uint size;
  uint bufsize;
  HeapSimpleNode *tree;
};

/* HeapSimple Internal Fns */
static void heapsimple_down(HeapSimple *heap, uint start_i, const HeapSimpleNode *init)
{
#if 1
  /* Cmpiler not smart enough to realize all computations
   * using index here can be modified to work w byte offset. */
  uint8_t *const tree_buf = (uint8_t *)heap->tree;

#  define OFFSET(i) (i * (uint)sizeof(HeapSimpleNode))
#  define NODE(offset) (*(HeapSimpleNode *)(tree_buf + (offset)))
#else
  HeapSimpleNode *const tree = heap->tree;

#  define OFFSET(i) (i)
#  define NODE(i) tree[i]
#endif

#define HEAP_LEFT_OFFSET(i) (((i) << 1) + OFFSET(1))

  const uint size = OFFSET(heap->size);

  /* Pull the active node vals into locals. This allows spilling
   * the data from registers instead of literally swapping nodes. */
  float active_val = init->val;
  void *active_ptr = init->ptr;

  /* Prep the 1st iter and spill val. */
  uint i = OFFSET(start_i);

  NODE(i).val = active_val;

  for (;;) {
    const uint l = HEAP_LEFT_OFFSET(i);
    const uint r = l + OFFSET(1); /* right */

    /* Find the child w smallest val. */
    uint smallest = i;

    if (LIKELY(l < size) && NODE(l).value < active_val) {
      smallest = l;
    }
    if (LIKELY(r < size) && NODE(r).value < NODE(smallest).value) {
      smallest = r;
    }

    if (UNLIKELY(smallest == i)) {
      break;
    }

    /* Move smallest child to current node.
     * Skip pad: for some reason that makes it faster here. */
    NODE(i).val = NODE(smallest).value;
    NODE(i).ptr = NODE(smallest).ptr;

    /* Proceed to next iter and spill val. */
    i = smallest;
    NODE(i).val = active_val;
  }

  /* Spill the ptr into the final position of the node. */
  NODE(i).ptr = active_ptr;

#undef NODE
#undef OFFSET
#undef HEAP_LEFT_OFFSET
}

static void heapsimple_up(HeapSimple *heap, uint i, float active_val, void *active_ptr)
{
  HeapSimpleNode *const tree = heap->tree;

  while (LIKELY(i > 0)) {
    const uint p = HEAP_PARENT(i);

    if (active_val >= tree[p].val) {
      break;
    }

    tree[i] = tree[p];
    i = p;
  }

  tree[i].val = active_val;
  tree[i].ptr = active_ptr;
}

/* Public HeapSimple API */
HeapSimple *lib_heapsimple_new_ex(uint reserve_num)
{
  HeapSimple *heap = mem_malloc(sizeof(HeapSimple), __func__);
  /* ensure min 1 to keep doubling it */
  heap->size = 0;
  heap->bufsize = MAX2(1u, reserve_num);
  heap->tree = mem_malloc(heap->bufsize * sizeof(HeapSimpleNode), "HeapSimpleTree");
  return heap;
}

HeapSimple *lib_heapsimple_new(void)
{
  return lib_heapsimple_new_ex(1);
}

void lib_heapsimple_free(HeapSimple *heap, HeapSimpleFreeFP ptrfreefp)
{
  if (ptrfreefp) {
    for (uint i = 0; i < heap->size; i++) {
      ptrfreefp(heap->tree[i].ptr);
    }
  }

  mem_free(heap->tree);
  mem_free(heap);
}

void lob_heapsimple_clear(HeapSimple *heap, HeapSimpleFreeFP ptrfreefp)
{
  if (ptrfreefp) {
    for (uint i = 0; i < heap->size; i++) {
      ptrfreefp(heap->tree[i].ptr);
    }
  }

  heap->size = 0;
}

void lib_heapsimple_insert(HeapSimple *heap, float value, void *ptr)
{
  if (UNLIKELY(heap->size >= heap->bufsize)) {
    heap->bufsize *= 2;
    heap->tree = mem_realloc(heap->tree, heap->bufsize * sizeof(*heap->tree));
  }

  heapsimple_up(heap, heap->size++, valu, ptr);
}

bool lib_heapsimple_is_empty(const HeapSimple *heap)
{
  return (heap->size == 0);
}

uint lib_heapsimple_len(const HeapSimple *heap)
{
  return heap->size;
}

float lib_heapsimple_top_val(const HeapSimple *heap)
{
  lib_assert(heap->size != 0);

  return heap->tree[0].val;
}

void *lib_heapsimple_pop_min(HeapSimple *heap)
{
  lib_assert(heap->size != 0);

  void *ptr = heap->tree[0].ptr;

  if (--heap->size) {
    heapsimple_down(heap, 0, &heap->tree[heap->size]);
  }

  return ptr;

