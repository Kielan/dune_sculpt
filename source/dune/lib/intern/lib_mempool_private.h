#pragma once
/* Shared logic for lib_task_parallel_mempool:
 * creates a threaded iter wo
 * exposing these fns publicly. */
#include "lib_compiler_attrs.h"
#include "lib_mempool.h"
#include "lib_task.h"

typedef struct MempoolThreadsafeIter {
  MempoolIter iter;
  struct MempoolChunk **curchunk_threaded_shared;
} MempoolThreadsafeIter;

typedef struct ParallelMempoolTaskData {
  MempoolThreadsafeIter ts_iter;
  TaskParallelTLS tls;
} ParallelMempoolTaskData;

/* Init an arr of mempool iters, LIB_MEMPOOL_ALLOW_ITER flag must be set.
 * This is used in threaded code, to generate as much iters as needed
 * (each task should have its own),
 * each iter goes over its own single chunk,
 * only getting the next chunk to iter over has to be
 * protected against concurrency (can be done in a lock-less way).
 * To be used when creating a task for each single item in the pool is totally overkill.
 * See lib_task_parallel_mempool impl for detailed usage example. */
ParallelMempoolTaskData *mempool_iter_threadsafe_create(lib_mempool *pool,
                                                        size_t iter_num) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
void mempool_iter_threadsafe_destroy(ParallelMempoolTaskData *iter_arr) ATTR_NONNULL();

/* A v of lib_mempool_iterstep that uses
 * lib_mempool_threadsafe_iter.curchunk_threaded_shared for threaded iter support.
 * (threaded section noted in comments). */
void *mempool_iter_threadsafe_step(MempoolThreadsafeIter *iter);

#ifdef __cplusplus
}
#endif
