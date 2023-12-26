/* Task scheduler init. */
#include "mem_guardedalloc.h"

#include "lib_lazy_threading.hh"
#include "lib_task.h"
#include "lib_threads.h"

#ifdef WITH_TBB
/* Need to include at least one header to get the version define. */
#  include <tbb/blocked_range.h>
#  include <tbb/task_arena.h>
#  if TBB_INTERFACE_VERSION_MAJOR >= 10
#    include <tbb/global_control.h>
#    define WITH_TBB_GLOBAL_CONTROL
#  endif
#endif

/* Task Scheduler */
static int task_scheduler_num_threads = 1;
#ifdef WITH_TBB_GLOBAL_CONTROL
static tbb::global_control *task_scheduler_global_control = nullptr;
#endif

void lib_task_scheduler_init()
{
#ifdef WITH_TBB_GLOBAL_CONTROL
  const int threads_override_num = lib_sys_num_threads_override_get();

  if (threads_override_num > 0) {
    /* Override num of threads. This settings is used within the lifetime
     * of tbb::global_control, so we alloc it on the heap. */
    task_scheduler_global_control = mem_new<tbb::global_control>(
        __func__, tbb::global_control::max_allowed_parallelism, threads_override_num);
    task_scheduler_num_threads = threads_override_num;
  }
  else {
    /* Let TBB choose the num of threads. For (legacy) code that calls
     * lib_task_scheduler_num_threads() we provide the sys thread count.
     * Ideally such code should be rewritten not to use the num of threads
     * at all. */
    task_scheduler_num_threads = lib_sys_thread_count();
  }
#else
  task_scheduler_num_threads = lib_sys_thread_count();
#endif
}

void lib_task_scheduler_exit()
{
#ifdef WITH_TBB_GLOBAL_CONTROL
  mem_del(task_scheduler_global_control);
#endif
}

int lib_task_scheduler_num_threads()
{
  return task_scheduler_num_threads;
}

void lib_task_isolate(void (*fn)(void *userdata), void *userdata)
{
#ifdef WITH_TBB
  dune::lazy_threading::ReceiverIsolation isolation;
  tbb::this_task_arena::isolate([&] { fn(userdata); });
#else
  fn(userdata);
#endif
}
