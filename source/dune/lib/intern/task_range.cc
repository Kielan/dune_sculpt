/* Task parallel range fns */
#include <cstdlib>

#include "mem_guardedalloc.h"

#include "types_list.h"

#include "lib_lazy_threading.hh"
#include "lib_task.h"
#include "lib_task.hh"
#include "lib_threads.h"

#include "atomic_ops.h"

#ifdef WITH_TBB
#  include <tbb/blocked_range.h>
#  include <tbb/enumerable_thread_specific.h>
#  include <tbb/parallel_for.h>
#  include <tbb/parallel_reduce.h>
#endif

#ifdef WITH_TBB

/* Fn for running TBB parallel_for and parallel_reduce. */
struct RangeTask {
  TaskParallelRangeFn fn;
  void *userdata;
  const TaskParallelSettings *settings;

  void *userdata_chunk;

  /* Root constructor. */
  RangeTask(TaskParallelRangeFn fn, void *userdata, const TaskParallelSettings *settings)
      : func(fn), userdata(userdata), settings(settings)
  {
    init_chunk(settings->userdata_chunk);
  }

  /* Copy constructor. */
  RangeTask(const RangeTask &other)
      : func(other.fn), userdata(other.userdata), settings(other.settings)
  {
    init_chunk(settings->userdata_chunk);
  }

  /* Splitting constructor for parallel reduce. */
  RangeTask(RangeTask &other, tbb::split /*unused*/)
      : fn(other.fn), userdata(other.userdata), settings(other.settings)
  {
    init_chunk(settings->userdata_chunk);
  }

  ~RangeTask()
  {
    if (settings->fn_free != nullptr) {
      settings->fn_free(userdata, userdata_chunk);
    }
    MEM_SAFE_FREE(userdata_chunk);
  }

  void init_chunk(void *from_chunk)
  {
    if (from_chunk) {
      userdata_chunk = mem_malloc(settings->userdata_chunk_size, "RangeTask");
      memcpy(userdata_chunk, from_chunk, settings->userdata_chunk_size);
    }
    else {
      userdata_chunk = nullptr;
    }
  }

  void operator()(const tbb::blocked_range<int> &r) const
  {
    TaskParallelTLS tls;
    tls.userdata_chunk = userdata_chunk;
    for (int i = r.begin(); i != r.end(); ++i) {
      fn(userdata, i, &tls);
    }
  }

  void join(const RangeTask &other)
  {
    settings->fn_reduce(userdata, userdata_chunk, other.userdata_chunk);
  }
};

#endif

void lib_task_parallel_range(const int start,
                             const int stop,
                             void *userdata,
                             TaskParallelRangeFn fn,
                             const TaskParallelSettings *settings)
{
#ifdef WITH_TBB
  /* Multithreading. */
  if (settings->use_threading && lib_task_scheduler_num_threads() > 1) {
    RangeTask task(fn, userdata, settings);
    const size_t grainsize = std::max(settings->min_iter_per_thread, 1);
    const tbb::blocked_range<int> range(start, stop, grainsize);

    dune::lazy_threading::send_hint();

    if (settings->fn_reduce) {
      parallel_reduce(range, task);
      if (settings->userdata_chunk) {
        memcpy(settings->userdata_chunk, task.userdata_chunk, settings->userdata_chunk_size);
      }
    }
    else {
      parallel_for(range, task);
    }
    return;
  }
#endif

  /* Single threaded. Nothing to reduce as everything is accumulated into the
   * main userdata chunk directly. */
  TaskParallelTLS tls;
  tls.userdata_chunk = settings->userdata_chunk;
  for (int i = start; i < stop; i++) {
    fn(userdata, i, &tls);
  }
  if (settings->fn_free != nullptr) {
    settings->fn_free(userdata, settings->userdata_chunk);
  }
}

int lib_task_parallel_thread_id(const TaskParallelTLS * /*tls*/)
{
#ifdef WITH_TBB
  /* Get a unique thread Id for texture nodes. In the future we should get rid
   * of the thread Id and change texture eval to not require per-thread
   * storage that can't be efficiently alloc on the stack. */
  static tbb::enumerable_thread_specific<int> tbb_thread_id(-1);
  static int tbb_thread_id_counter = 0;

  int &thread_id = tbb_thread_id.local();
  if (thread_id == -1) {
    thread_id = atomic_fetch_and_add_int32(&tbb_thread_id_counter, 1);
    if (thread_id >= DUNE_MAX_THREADS) {
      lib_assert_msg(0, "Max num of threads exceeded for sculpting");
      thread_id = thread_id % DUNE_MAX_THREADS;
    }
  }
  return thread_id;
#else
  return 0;
#endif
}

namespace dune::threading::detail {

void parallel_for_impl(const IndexRange range,
                       const int64_t grain_size,
                       const FnRef<void(IndexRange)> fn)
{
#ifdef WITH_TBB
  /* Invoking tbb for small workloads has a large overhead. */
  if (range.size() >= grain_size) {
    lazy_threading::send_hint();
    tbb::parallel_for(
        tbb::blocked_range<int64_t>(range.first(), range.one_after_last(), grain_size),
        [fn](const tbb::blocked_range<int64_t> &subrange) {
          fn(IndexRange(subrange.begin(), subrange.size()));
        });
    return;
  }
#else
  UNUSED_VARS(grain_size);
#endif
  fn(range);
}

}  // namespace blender::threading::detail
