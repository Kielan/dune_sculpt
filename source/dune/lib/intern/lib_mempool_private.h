#pragma once

/** \file
 * \ingroup bli
 *
 * Shared logic for #BLI_task_parallel_mempool to create a threaded iterator,
 * without exposing the these functions publicly.
 */

#include "lib_compiler_attrs.h"

#include "lib_mempool.h"
#include "lib_task.h"

typedef struct lib_mempool_threadsafe_iter {
  lib_mempool_iter iter;
  struct lib_mempool_chunk **curchunk_threaded_shared;
} lib_mempool_threadsafe_iter;

typedef struct ParallelMempoolTaskData {
  lib_mempool_threadsafe_iter ts_iter;
  TaskParallelTLS tls;
} ParallelMempoolTaskData;

/* Init an array of mempool iterators, LIN_MEMPOOL_ALLOW_ITER flag must be set.
 *
 * This is used in threaded code, to generate as much iters as needed
 * (each task should have its own),
 * such that each iter goes over its own single chunk,
 * and only getting the next chunk to iter over has to be
 * protected against concurrency (which can be done in a lock-less way).
 *
 * To be used when creating a task for each single item in the pool is totally overkill.
 *
 * See lib_task_parallel_mempool implementation for detailed usage example. */
ParallelMempoolTaskData *mempool_iter_threadsafe_create(lib_mempool *pool,
                                                        size_t iter_num) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
void mempool_iter_threadsafe_destroy(ParallelMempoolTaskData *iter_arr) ATTR_NONNULL();

/* A version of lib_mempool_iterstep that uses
 * lib_mempool_threadsafe_iter.curchunk_threaded_shared for threaded iter support.
 * (threaded section noted in comments). */
void *mempool_iter_threadsafe_step(lib_mempool_threadsafe_iter *iter);

#ifdef __cplusplus
}
#endif
