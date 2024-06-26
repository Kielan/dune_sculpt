/* A generic struct queue
 * (a queue for fixed length generally small) structs */
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_gsqueue.h"
#include "lib_strict_flags.h"
#include "lib_utildefines.h"

/* target chunk size: 64kb */
#define CHUNK_SIZE_DEFAULT (1 << 16)
/* ensure we get at least this many elems per chunk */
#define CHUNK_ELEM_MIN 32

struct QueueChunk {
  struct QueueChunk *next;
  char data[0];
};

struct _GSQueue {
  struct QueueChunk *chunk_first; /* first active chunk to pop from */
  struct QueueChunk *chunk_last;  /* last active chunk to push onto */
  struct QueueChunk *chunk_free;  /* free chunks to reuse */
  size_t chunk_first_index;       /* index into 'chunk_first' */
  size_t chunk_last_index;        /* index into 'chunk_last' */
  size_t chunk_elem_max;          /* num of elems per chunk */
  size_t elem_size;               /* mem size of elems */
  size_t elem_num;                /* total num of elems */
};

static void *queue_get_first_elem(GSQueue *queue)
{
  return ((char *)(queue)->chunk_first->data) + ((queue)->elem_size * (queue)->chunk_first_index);
}

static void *queue_get_last_elem(GSQueue *queue)
{
  return ((char *)(queue)->chunk_last->data) + ((queue)->elem_size * (queue)->chunk_last_index);
}

/* num of elements per chunk, optimized for slop-space. */
static size_t queue_chunk_elem_max_calc(const size_t elem_size, size_t chunk_size)
{
  /* get at least this num of elems per chunk */
  const size_t elem_size_min = elem_size * CHUNK_ELEM_MIN;

  lib_assert((elem_size != 0) && (chunk_size != 0));

  while (UNLIKELY(chunk_size <= elem_size_min)) {
    chunk_size <<= 1;
  }

  /* account for slop-space */
  chunk_size -= (sizeof(struct QueueChunk) + MEM_SIZE_OVERHEAD);

  return chunk_size / elem_size;
}

GSQueue *lib_gsqueue_new(const size_t elem_size)
{
  GSQueue *queue = mem_calloc(sizeof(*queue), "lib_gsqueue_new");

  queue->chunk_elem_max = queue_chunk_elem_max_calc(elem_size, CHUNK_SIZE_DEFAULT);
  queue->elem_size = elem_size;
  /* force init */
  queue->chunk_last_index = queue->chunk_elem_max - 1;

  return queue;
}

static void queue_free_chunk(struct QueueChunk *data)
{
  while (data) {
    struct QueueChunk *data_next = data->next;
    mem_free(data);
    data = data_next;
  }
}

void lib_gsqueue_free(GSQueue *queue)
{
  queue_free_chunk(queue->chunk_first);
  queue_free_chunk(queue->chunk_free);
  mem_free(queue);
}

void lib_gsqueue_push(GSQueue *queue, const void *item)
{
  queue->chunk_last_index++;
  queue->elem_num++;

  if (UNLIKELY(queue->chunk_last_index == queue->chunk_elem_max)) {
    struct QueueChunk *chunk;
    if (queue->chunk_free) {
      chunk = queue->chunk_free;
      queue->chunk_free = chunk->next;
    }
    else {
      chunk = mem_malloc(sizeof(*chunk) + (queue->elem_size * queue->chunk_elem_max), __func__);
    }

    chunk->next = NULL;

    if (queue->chunk_last == NULL) {
      queue->chunk_first = chunk;
    }
    else {
      queue->chunk_last->next = chunk;
    }

    queue->chunk_last = chunk;
    queue->chunk_last_index = 0;
  }

  lib_assert(queue->chunk_last_index < queue->chunk_elem_max);

  /* Return last of queue */
  memcpy(queue_get_last_elem(queue), item, queue->elem_size);
}

void lib_gsqueue_pop(GSQueue *queue, void *r_item)
{
  lib_assert(lib_gsqueue_is_empty(queue) == false);

  memcpy(r_item, queue_get_first_elem(queue), queue->elem_size);
  queue->chunk_first_index++;
  queue->elem_num--;

  if (UNLIKELY(queue->chunk_first_index == queue->chunk_elem_max || queue->elem_num == 0)) {
    struct QueueChunk *chunk_free = queue->chunk_first;

    queue->chunk_first = queue->chunk_first->next;
    queue->chunk_first_index = 0;
    if (queue->chunk_first == NULL) {
      queue->chunk_last = NULL;
      queue->chunk_last_index = queue->chunk_elem_max - 1;
    }

    chunk_free->next = queue->chunk_free;
    queue->chunk_free = chunk_free;
  }
}

size_t lib_gsqueue_len(const GSQueue *queue)
{
  return queue->elem_num;
}

bool lib_gsqueue_is_empty(const GSQueue *queue)
{
  return (queue->chunk_first == NULL);
}
