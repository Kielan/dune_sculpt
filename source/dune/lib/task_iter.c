/* Parallel tasks over all elements in a container. */

#include <stdlib.h>

#include "mem_guardedalloc.h"

#include "types_list.h"

#include "lib_list.h"
#include "lib_math_base.h"
#include "lib_mempool.h"
#include "lib_mempool_private.h"
#include "lib_task.h"
#include "lib_threads.h"

#include "atomic_ops.h"

/* Macros */
/* Allows to avoid using malloc for userdata_chunk in tasks, when small enough. */
#define MALLOCA(_size) ((_size) <= 8192) ? alloca(_size) : mem_malloc((_size), __func__)
#define MALLOCA_FREE(_mem, _size) \
  if (((_mem) != NULL) && ((_size) > 8192)) { \
    mem_free(_mem); \
  } \
  ((void)0)

/* MemPool Iter */
typedef struct ParallelMempoolState {
  void *userdata;
  TaskParallelMempoolFn fn;
} ParallelMempoolState;

static void parallel_mempool_fn(TaskPool *__restrict pool, void *taskdata)
{
  ParallelMempoolState *__restrict state = lib_task_pool_user_data(pool);
  lib_mempool_threadsafe_iter *iter = &((ParallelMempoolTaskData *)taskdata)->ts_iter;
  TaskParallelTLS *tls = &((ParallelMempoolTaskData *)taskdata)->tls;

  MempoolIterData *item;
  while ((item = mempool_iter_threadsafe_step(iter)) != NULL) {
    state->fn(state->userdata, item, tls);
  }
}

void lib_task_parallel_mempool(LibMempool *mempool,
                               void *userdata,
                               TaskParallelMempoolFn fn,
                               const TaskParallelSettings *settings)
{
  if (UNLIKELY(lib_mempool_len(mempool) == 0)) {
    return;
  }

  void *userdata_chunk = settings->userdata_chunk;
  const size_t userdata_chunk_size = settings->userdata_chunk_size;
  void *userdata_chunk_array = NULL;
  const bool use_userdata_chunk = (userdata_chunk_size != 0) && (userdata_chunk != NULL);

  if (!settings->use_threading) {
    TaskParallelTLS tls = {NULL};
    if (use_userdata_chunk) {
      if (settings->fn_init != NULL) {
        settings->fn_init(userdata, userdata_chunk);
      }
      tls.userdata_chunk = userdata_chunk;
    }

    lib_mempool_iter iter;
    lib_mempool_iternew(mempool, &iter);

    void *item;
    while ((item = lib_mempool_iterstep(&iter))) {
      fn(userdata, item, &tls);
    }

    if (use_userdata_chunk) {
      if (settings->fn_free != NULL) {
        /* `fn_free` should only free data that was created during ex of `fn`. */
        settings->fn_free(userdata, userdata_chunk);
      }
    }

    return;
  }

  ParallelMempoolState state;
  TaskPool *task_pool = lib_task_pool_create(&state, TASK_PRIORITY_HIGH);
  const int threads_num = lib_task_scheduler_num_threads();

  /* The idea here is to prevent creating task for each of the loop iters
   * and instead have tasks which are evenly distributed across CPU cores and
   * pull next item to be crunched using the threaded-aware lib_mempool_iter. */
  const int tasks_num = threads_num + 2;

  state.userdata = userdata;
  state.fn = fn;

  if (use_userdata_chunk) {
    userdata_chunk_array = MALLOCA(userdata_chunk_size * tasks_num);
  }

  ParallelMempoolTaskData *mempool_iter_data = mempool_iter_threadsafe_create(
      mempool, (size_t)tasks_num);

  for (int i = 0; i < tasks_num; i++) {
    void *userdata_chunk_local = NULL;
    if (use_userdata_chunk) {
      userdata_chunk_local = (char *)userdata_chunk_array + (userdata_chunk_size * i);
      memcpy(userdata_chunk_local, userdata_chunk, userdata_chunk_size);
      if (settings->fn_init != NULL) {
        settings->fn_init(userdata, userdata_chunk_local);
      }
    }
    mempool_iter_data[i].tls.userdata_chunk = userdata_chunk_local;

    /* Use this pool's pre-alloc tasks. */
    lib_task_pool_push(task_pool, parallel_mempool_fn, &mempool_iter_data[i], false, NULL);
  }

  lib_task_pool_work_and_wait(task_pool);
  lib_task_pool_free(task_pool);

  if (use_userdata_chunk) {
    if ((settings->fn_free != NULL) || (settings->fn_reduce != NULL)) {
      for (int i = 0; i < tasks_num; i++) {
        if (settings->fn_reduce) {
          settings->fn_reduce(
              userdata, userdata_chunk, mempool_iter_data[i].tls.userdata_chunk);
        }
        if (settings->fn_free) {
          settings->fn_free(userdata, mempool_iter_data[i].tls.userdata_chunk);
        }
      }
    }
    MALLOCA_FREE(userdata_chunk_array, userdata_chunk_size * tasks_num);
  }

  mempool_iter_threadsafe_destroy(mempool_iter_data);
}

#undef MALLOCA
#undef MALLOCA_FREE
