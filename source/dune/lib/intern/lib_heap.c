/* A min-heap / priority queue ADT. */
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_heap.h"
#include "lib_strict_flags.h"
#include "lib_utildefines.h"

struct HeapNode {
  float val;
  uint index;
  void *ptr;
};

struct HeapNodeChunk {
  struct HeapNodeChunk *prev;
  uint size;
  uint bufsize;
  HeapNode buf[0];
};

/* Num of nodes to include per HeapNodeChunk when no reserved size is passed,
 * or we alloc past the reserved num.
 * Optimize num for 64kb allocs.
 * keep type in sync w nodes_num in heap_node_alloc_chunk. */
#define HEAP_CHUNK_DEFAULT_NUM \
  (uint)(MEM_SIZE_OPTIMAL((1 << 16) - sizeof(struct HeapNode_Chunk)) / sizeof(HeapNode))

struct Heap {
  uint size;
  uint bufsize;
  HeapNode **tree;

  struct {
    /* Always keep min 1 chunk (never NULL) */
    struct HeapNodeChunk *chunk;
    /* when NULL alloc a new chunk */
    HeapNode *free;
  } nodes;
};

/* Intern Fns */
#define HEAP_PARENT(i) (((i)-1) >> 1)
#define HEAP_LEFT(i) (((i) << 1) + 1)
#define HEAP_RIGHT(i) (((i) << 1) + 2)
#define HEAP_COMPARE(a, b) ((a)->value < (b)->value)

#if 0 /* UNUSED */
#  define HEAP_EQUALS(a, b) ((a)->value == (b)->value)
#endif

LIB_INLINE void heap_swap(Heap *heap, const uint i, const uint j)
{
#if 1
  HeapNode **tree = heap->tree;
  HeapNode *pi = tree[i], *pj = tree[j];
  pi->index = j;
  tree[j] = pi;
  pj->index = i;
  tree[i] = pj;
#elif 0
  SWAP(uint, heap->tree[i]->index, heap->tree[j]->index);
  SWAP(HeapNode *, heap->tree[i], heap->tree[j]);
#else
  HeapNode **tree = heap->tree;
  union {
    uint index;
    HeapNode *node;
  } tmp;
  SWAP_TVAL(tmp.index, tree[i]->index, tree[j]->index);
  SWAP_TVAL(tmp.node, tree[i], tree[j]);
#endif
}

static void heap_down(Heap *heap, uint i)
{
  /* size won't change in the loop */
  HeapNode **const tree = heap->tree;
  const uint size = heap->size;

  while (1) {
    const uint l = HEAP_LEFT(i);
    const uint r = HEAP_RIGHT(i);
    uint smallest = i;

    if (LIKELY(l < size) && HEAP_COMPARE(tree[l], tree[smallest])) {
      smallest = l;
    }
    if (LIKELY(r < size) && HEAP_COMPARE(tree[r], tree[smallest])) {
      smallest = r;
    }

    if (UNLIKELY(smallest == i)) {
      break;
    }

    heap_swap(heap, i, smallest);
    i = smallest;
  }
}

static void heap_up(Heap *heap, uint i)
{
  HeapNode **const tree = heap->tree;

  while (LIKELY(i > 0)) {
    const uint p = HEAP_PARENT(i);

    if (HEAP_COMPARE(tree[p], tree[i])) {
      break;
    }
    heap_swap(heap, p, i);
    i = p;
  }
}

/* Internal Mem Mngmt */
static struct HeapNodeChunk *heap_node_alloc_chunk(uint nodes_num,
                                                   struct HeapNodeChunk *chunk_prev)
{
  struct HeapNodeChunk *chunk = mem_malloc(
      sizeof(struct HeapNodeChunk) + (sizeof(HeapNode) * nodes_num), __func__);
  chunk->prev = chunk_prev;
  chunk->bufsize = nodes_num;
  chunk->size = 0;
  return chunk;
}

static HeapNode *heap_node_alloc(Heap *heap)
{
  HeapNode *node;

  if (heap->nodes.free) {
    node = heap->nodes.free;
    heap->nodes.free = heap->nodes.free->ptr;
  }
  else {
    struct HeapNodeChunk *chunk = heap->nodes.chunk;
    if (UNLIKELY(chunk->size == chunk->bufsize)) {
      chunk = heap->nodes.chunk = heap_node_alloc_chunk(HEAP_CHUNK_DEFAULT_NUM, chunk);
    }
    node = &chunk->buf[chunk->size++];
  }

  return node;
}

static void heap_node_free(Heap *heap, HeapNode *node)
{
  node->ptr = heap->nodes.free;
  heap->nodes.free = node;
}

/* Public Heap API */
Heap *lib_heap_new_ex(uint reserve_num)
{
  Heap *heap = mem_malloc(sizeof(Heap), __func__);
  /* ensure have min 1 to keep doubling it */
  heap->size = 0;
  heap->bufsize = MAX2(1u, reserve_num);
  heap->tree = mem_malloc(heap->bufsize * sizeof(HeapNode *), "BLIHeapTree");

  heap->nodes.chunk = heap_node_alloc_chunk(
      (reserve_num > 1) ? reserve_num : HEAP_CHUNK_DEFAULT_NUM, NULL);
  heap->nodes.free = NULL;

  return heap;
}

Heap *lib_heap_new(void)
{
  return lib_heap_new_ex(1);
}

void lib_heap_free(Heap *heap, HeapFreeFP ptrfreefp)
{
  if (ptrfreefp) {
    uint i;

    for (i = 0; i < heap->size; i++) {
      ptrfreefp(heap->tree[i]->ptr);
    }
  }

  struct HeapNode_Chunk *chunk = heap->nodes.chunk;
  do {
    struct HeapNode_Chunk *chunk_prev;
    chunk_prev = chunk->prev;
    mem_free(chunk);
    chunk = chunk_prev;
  } while (chunk);

  mem_free(heap->tree);
  mem_free(heap);
}

void lib_heap_clear(Heap *heap, HeapFreeFP ptrfreefp)
{
  if (ptrfreefp) {
    uint i;

    for (i = 0; i < heap->size; i++) {
      ptrfreefp(heap->tree[i]->ptr);
    }
  }
  heap->size = 0;

  /* Remove all except the last chunk */
  while (heap->nodes.chunk->prev) {
    struct HeapNode_Chunk *chunk_prev = heap->nodes.chunk->prev;
    mem_free(heap->nodes.chunk);
    heap->nodes.chunk = chunk_prev;
  }
  heap->nodes.chunk->size = 0;
  heap->nodes.free = NULL;
}

HeapNode *lib_heap_insert(Heap *heap, float value, void *ptr)
{
  HeapNode *node;

  if (UNLIKELY(heap->size >= heap->bufsize)) {
    heap->bufsize *= 2;
    heap->tree = mem_realloc(heap->tree, heap->bufsize * sizeof(*heap->tree));
  }

  node = heap_node_alloc(heap);

  node->ptr = ptr;
  node->val = val;
  node->index = heap->size;

  heap->tree[node->index] = node;

  heap->size++;

  heap_up(heap, node->index);

  return node;
}

void lib_heap_insert_or_update(Heap *heap, HeapNode **node_p, float val, void *ptr)
{
  if (*node_p == NULL) {
    *node_p = lib_heap_insert(heap, val, ptr);
  }
  else {
    lib_heap_node_val_update_ptr(heap, *node_p, val, ptr);
  }
}

bool lib_heap_is_empty(const Heap *heap)
{
  return (heap->size == 0);
}

uint lib_heap_len(const Heap *heap)
{
  return heap->size;
}

HeapNode *lib_heap_top(const Heap *heap)
{
  return heap->tree[0];
}

float lib_heap_top_val(const Heap *heap)
{
  lib_assert(heap->size != 0);

  return heap->tree[0]->val;
}

void *lib_heap_pop_min(Heap *heap)
{
  lib_assert(heap->size != 0);

  void *ptr = heap->tree[0]->ptr;

  heap_node_free(heap, heap->tree[0]);

  if (--heap->size) {
    heap_swap(heap, 0, heap->size);
    heap_down(heap, 0);
  }

  return ptr;
}

void lib_heap_remove(Heap *heap, HeapNode *node)
{
  lib_assert(heap->size != 0);

  uint i = node->index;

  while (i > 0) {
    uint p = HEAP_PARENT(i);
    heap_swap(heap, p, i);
    i = p;
  }

  lib_heap_pop_min(heap);
}

void lib_heap_node_val_update(Heap *heap, HeapNode *node, float value)
{
  if (value < node->val) {
    node->val = value;
    heap_up(heap, node->index);
  }
  else if (val > node->val) {
    node->val = val;
    heap_down(heap, node->index);
  }
}

void pob_heap_node_val_update_ptr(Heap *heap, HeapNode *node, float val, void *ptr)
{
  node->ptr = ptr; /* only diff */
  if (val < node->val) {
    node->val = value;
    heap_up(heap, node->index);
  }
  else if (val > node->val) {
    node->val = val;
    heap_down(heap, node->index);
  }
}

float lib_heap_node_val(const HeapNode *heap)
{
  return heap->val;
}

void *lib_heap_node_ptr(const HeapNode *heap)
{
  return heap->ptr;
}

static bool heap_is_minheap(const Heap *heap, uint root)
{
  if (root < heap->size) {
    if (heap->tree[root]->index != root) {
      return false;
    }
    const uint l = HEAP_LEFT(root);
    if (l < heap->size) {
      if (HEAP_COMPARE(heap->tree[l], heap->tree[root]) || !heap_is_minheap(heap, l)) {
        return false;
      }
    }
    const uint r = HEAP_RIGHT(root);
    if (r < heap->size) {
      if (HEAP_COMPARE(heap->tree[r], heap->tree[root]) || !heap_is_minheap(heap, r)) {
        return false;
      }
    }
  }
  return true;
}
bool lib_heap_is_valid(const Heap *heap)
{
  return heap_is_minheap(heap, 0);
}
