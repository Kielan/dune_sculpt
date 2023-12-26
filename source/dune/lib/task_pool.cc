/* Task pool to run tasks in parallel. */
#include <cstdlib>
#include <memory>
#include <utility>

#include "mem_guardedalloc.h"

#include "types_list.h"

#include "lib_mempool.h"
#include "lib_task.h"
#include "lib_threads.h"

#ifdef WITH_TBB
#  include <tbb/blocked_range.h>
#  include <tbb/task_arena.h>
#  include <tbb/task_group.h>
#endif

/* Task
 * Unit of work to ex. This is a C++ class to work with TBB. */
class Task {
 public:
  TaskPool *pool;
  TaskRunFn run;
  void *taskdata;
  bool free_taskdata;
  TaskFreeFn freedata;

  Task(TaskPool *pool,
       TaskRunFn run,
       void *taskdata,
       bool free_taskdata,
       TaskFreeFn freedata)
      : pool(pool), run(run), taskdata(taskdata), free_taskdata(free_taskdata), freedata(freedata)
  {
  }

  ~Task()
  {
    if (free_taskdata) {
      if (freedata) {
        freedata(pool, taskdata);
      }
      else {
        mem_free(taskdata);
      }
    }
  }

  /* Move constructor.
   * For performance, ensure we never copy the task and only move it.
   * For TBB version 2017 and earlier we apply a workaround to make up for
   * the lack of move constructor support. */
  Task(Task &&other)
      : pool(other.pool),
        run(other.run),
        taskdata(other.taskdata),
        free_taskdata(other.free_taskdata),
        freedata(other.freedata)
  {
    other.pool = nullptr;
    other.run = nullptr;
    other.taskdata = nullptr;
    other.free_taskdata = false;
    other.freedata = nullptr;
  }

#if defined(WITH_TBB) && TBB_INTERFACE_VERSION_MAJOR < 10
  Task(const Task &other)
      : pool(other.pool),
        run(other.run),
        taskdata(other.taskdata),
        free_taskdata(other.free_taskdata),
        freedata(other.freedata)
  {
    ((Task &)other).pool = nullptr;
    ((Task &)other).run = nullptr;
    ((Task &)other).taskdata = nullptr;
    ((Task &)other).free_taskdata = false;
    ((Task &)other).freedata = nullptr;
  }
#else
  Task(const Task &other) = delete;
#endif

  Task &operator=(const Task &other) = delete;
  Task &operator=(Task &&other) = delete;

  void operator()() const;
};

/* TBB Task Group.
 * Subclass since there seems to be no other way to set priority. */
#ifdef WITH_TBB
class TBBTaskGroup : public tbb::task_group {
 public:
  TBBTaskGroup(eTaskPriority priority)
  {
#  if TBB_INTERFACE_VERSION_MAJOR >= 12
    /* TODO: support priorities in TBB 2021, where they are only available as
     * part of task arenas, no longer for task groups. Or remove support for
     * task priorities if they are no longer useful. */
    UNUSED_VARS(priority);
#  else
    switch (priority) {
      case TASK_PRIORITY_LOW:
        my_cxt.set_priority(tbb::priority_low);
        break;
      case TASK_PRIORITY_HIGH:
        my_cxt.set_priority(tbb::priority_normal);
        break;
    }
#  endif
  }
};
#endif

/* Task Pool */
enum TaskPoolType {
  TASK_POOL_TBB,
  TASK_POOL_TBB_SUSPENDED,
  TASK_POOL_NO_THREADS,
  TASK_POOL_BACKGROUND,
  TASK_POOL_BACKGROUND_SERIAL,
};

struct TaskPool {
  TaskPoolType type;
  bool use_threads;

  ThreadMutex user_mutex;
  void *userdata;

#ifdef WITH_TBB
  /* TBB task pool. */
  TBBTaskGroup tbb_group;
#endif
  volatile bool is_suspended;
  LibMempool *suspended_mempool;

  /* Background task pool. */
  List background_threads;
  ThreadQueue *background_queue;
  volatile bool background_is_canceling;
};

/* Ex task. */
void Task::operator()() const
{
  run(pool, taskdata);
}

/* TBB Task Pool.
 * Task pool using the TBB scheduler for tasks. When building wo TBB
 * support or running Dune w -t 1, this reverts to single threaded.
 * Tasks may be suspended until in all are created, to make it possible to
 * init data structs and create tasks in a single pass. */
static void tbb_task_pool_create(TaskPool *pool, eTaskPriority priority)
{
  if (pool->type == TASK_POOL_TBB_SUSPENDED) {
    pool->is_suspended = true;
    pool->suspended_mempool = lib_mempool_create(sizeof(Task), 512, 512, BLI_MEMPOOL_ALLOW_ITER);
  }

#ifdef WITH_TBB
  if (pool->use_threads) {
    new (&pool->tbb_group) TBBTaskGroup(priority);
  }
#else
  UNUSED_VARS(priority);
#endif
}

static void tbb_task_pool_run(TaskPool *pool, Task &&task)
{
  if (pool->is_suspended) {
    /* Suspended task that will be ex in work_and_wait(). */
    Task *task_mem = (Task *)lib_mempool_alloc(pool->suspended_mempool);
    new (task_mem) Task(std::move(task));
#ifdef __GNUC__
    /* Work around apparent compiler bug where task is not properly copied
     * to task_mem. This appears unrelated to the use of placement new or
     * move semantics, happens even writing to a plain C struct. Rather the
     * call into TBB seems to have some indirect effect. */
    std::atomic_thread_fence(std::memory_order_release);
#endif
  }
#ifdef WITH_TBB
  else if (pool->use_threads) {
    /* Ex in TBB task group. */
    pool->tbb_group.run(std::move(task));
  }
#endif
  else {
    /* Ex immediately. */
    task();
  }
}

static void tbb_task_pool_work_and_wait(TaskPool *pool)
{
  /* Start any suspended task now. */
  if (pool->suspended_mempool) {
    pool->is_suspended = false;

    LibMempoolIter iter;
    lib_mempool_iternew(pool->suspended_mempool, &iter);
    while (Task *task = (Task *)lib_mempool_iterstep(&iter)) {
      tbb_task_pool_run(pool, std::move(*task));
    }

    lib_mempool_clear(pool->suspended_mempool);
  }

#ifdef WITH_TBB
  if (pool->use_threads) {
    /* This is called wait(), but internally it can actually do work. This
     * matters bc we don't want recursive usage of task pools to run
     * out of threads and get stuck. */
    pool->tbb_group.wait();
  }
#endif
}

static void tbb_task_pool_cancel(TaskPool *pool)
{
#ifdef WITH_TBB
  if (pool->use_threads) {
    pool->tbb_group.cancel();
    pool->tbb_group.wait();
  }
#else
  UNUSED_VARS(pool);
#endif
}

static bool tbb_task_pool_canceled(TaskPool *pool)
{
#ifdef WITH_TBB
  if (pool->use_threads) {
    return tbb::is_current_task_group_canceling();
  }
#else
  UNUSED_VARS(pool);
#endif

  return false;
}

static void tbb_task_pool_free(TaskPool *pool)
{
#ifdef WITH_TBB
  if (pool->use_threads) {
    pool->tbb_group.~TBBTaskGroup();
  }
#endif

  if (pool->suspended_mempool) {
    lib_mempool_destroy(pool->suspended_mempool);
  }
}

/* Background Task Pool.
 * Fallback for running background tasks when building wo TBB. */
static void *background_task_run(void *userdata)
{
  TaskPool *pool = (TaskPool *)userdata;
  while (Task *task = (Task *)lib_thread_queue_pop(pool->background_queue)) {
    (*task)();
    task->~Task();
    mem_free(task);
  }
  return nullptr;
}

static void background_task_pool_create(TaskPool *pool)
{
  pool->background_queue = lib_thread_queue_init();
  lib_threadpool_init(&pool->background_threads, background_task_run, 1);
}

static void background_task_pool_run(TaskPool *pool, Task &&task)
{
  Task *task_mem = (Task *)mem_malloc(sizeof(Task), __func__);
  new (task_mem) Task(std::move(task));
  lib_thread_queue_push(pool->background_queue, task_mem);

  if lib_available_threads(&pool->background_threads)) {
    lib_threadpool_insert(&pool->background_threads, pool);
  }
}

static void background_task_pool_work_and_wait(TaskPool *pool)
{
  /* Signal background thread to stop waiting for new tasks if none are
   * left, and wait for tasks and thread to finish. */
  lib_thread_queue_nowait(pool->background_queue);
  lib_thread_queue_wait_finish(pool->background_queue);
  lib_threadpool_clear(&pool->background_threads);
}

static void background_task_pool_cancel(TaskPool *pool)
{
  pool->background_is_canceling = true;

  /* Remove tasks not yet started by background thread. */
  lib_thread_queue_nowait(pool->background_queue);
  while (Task *task = (Task *)lib_thread_queue_pop(pool->background_queue)) {
    task->~Task();
    mem_free(task);
  }

  /* Let background thread finish or cancel task it is working on. */
  lib_threadpool_remove(&pool->background_threads, pool);
  pool->background_is_canceling = false;
}

static bool background_task_pool_canceled(TaskPool *pool)
{
  return pool->background_is_canceling;
}

static void background_task_pool_free(TaskPool *pool)
{
  background_task_pool_work_and_wait(pool);

  lib_threadpool_end(&pool->background_threads);
  lib_thread_queue_free(pool->background_queue);
}

/* Task Pool */
static TaskPool *task_pool_create_ex(void *userdata, TaskPoolType type, eTaskPriority priority)
{
  const bool use_threads = lib_task_scheduler_num_threads() > 1 && type != TASK_POOL_NO_THREADS;

  /* Background task pool uses regular TBB scheduling if available. Only when
   * building without TBB or running with -t 1 do we need to ensure these tasks
   * do not block the main thread. */
  if (type == TASK_POOL_BACKGROUND && use_threads) {
    type = TASK_POOL_TBB;
  }

  /* Alloc task pool. */
  TaskPool *pool = (TaskPool *)mem_calloc(sizeof(TaskPool), "TaskPool");

  pool->type = type;
  pool->use_threads = use_threads;

  pool->userdata = userdata;
  lib_mutex_init(&pool->user_mutex);

  switch (type) {
    case TASK_POOL_TBB:
    case TASK_POOL_TBB_SUSPENDED:
    case TASK_POOL_NO_THREADS:
      tbb_task_pool_create(pool, priority);
      break;
    case TASK_POOL_BACKGROUND:
    case TASK_POOL_BACKGROUND_SERIAL:
      background_task_pool_create(pool);
      break;
  }

  return pool;
}

TaskPool *lib_task_pool_create(void *userdata, eTaskPriority priority)
{
  return task_pool_create_ex(userdata, TASK_POOL_TBB, priority);
}

TaskPool *lib_task_pool_create_background(void *userdata, eTaskPriority priority)
{
  /* In multi-threaded cxt, there is no diffs with lib_task_pool_create(),
   * but in single-threaded case it is ensured to have at least one worker thread to run on
   * (i.e. you don't have to call lib_task_pool_work_and_wait
   * on it to be sure it will be processed).
   *
   * Background pools are non-recursive
   * (that is, you should not create other background pools in tasks assigned to a background pool,
   * they could end never being ex, since the 'fallback' background thread is already
   * busy with parent task in single-threaded cxt). */
  return task_pool_create_ex(userdata, TASK_POOL_BACKGROUND, priority);
}

TaskPool *lib_task_pool_create_suspended(void *userdata, eTaskPriority priority)
{
  /* Similar to lib_task_pool_create() but does not schedule any tasks for ex
   * for until lib_task_pool_work_and_wait() is called. This helps reducing threading
   * overhead when pushing huge amount of small init tasks from the main thread. */
  return task_pool_create_ex(userdata, TASK_POOL_TBB_SUSPENDED, priority);
}

TaskPool *lib_task_pool_create_no_threads(void *userdata)
{
  return task_pool_create_ex(userdata, TASK_POOL_NO_THREADS, TASK_PRIORITY_HIGH);
}

TaskPool *lib_task_pool_create_background_serial(void *userdata, eTaskPriority priority)
{
  return task_pool_create_ex(userdata, TASK_POOL_BACKGROUND_SERIAL, priority);
}

void lib_task_pool_free(TaskPool *pool)
{
  switch (pool->type) {
    case TASK_POOL_TBB:
    case TASK_POOL_TBB_SUSPENDED:
    case TASK_POOL_NO_THREADS:
      tbb_task_pool_free(pool);
      break;
    case TASK_POOL_BACKGROUND:
    case TASK_POOL_BACKGROUND_SERIAL:
      background_task_pool_free(pool);
      break;
  }

  lib_mutex_end(&pool->user_mutex);

  mem_free(pool);
}

void lib_task_pool_push(TaskPool *pool,
                        TaskRunFn run,
                        void *taskdata,
                        bool free_taskdata,
                        TaskFreeFn freedata)
{
  Task task(pool, run, taskdata, free_taskdata, freedata);

  switch (pool->type) {
    case TASK_POOL_TBB:
    case TASK_POOL_TBB_SUSPENDED:
    case TASK_POOL_NO_THREADS:
      tbb_task_pool_run(pool, std::move(task));
      break;
    case TASK_POOL_BACKGROUND:
    case TASK_POOL_BACKGROUND_SERIAL:
      background_task_pool_run(pool, std::move(task));
      break;
  }
}

void lib_task_pool_work_and_wait(TaskPool *pool)
{
  switch (pool->type) {
    case TASK_POOL_TBB:
    case TASK_POOL_TBB_SUSPENDED:
    case TASK_POOL_NO_THREADS:
      tbb_task_pool_work_and_wait(pool);
      break;
    case TASK_POOL_BACKGROUND:
    case TASK_POOL_BACKGROUND_SERIAL:
      background_task_pool_work_and_wait(pool);
      break;
  }
}

void lib_task_pool_cancel(TaskPool *pool)
{
  switch (pool->type) {
    case TASK_POOL_TBB:
    case TASK_POOL_TBB_SUSPENDED:
    case TASK_POOL_NO_THREADS:
      tbb_task_pool_cancel(pool);
      break;
    case TASK_POOL_BACKGROUND:
    case TASK_POOL_BACKGROUND_SERIAL:
      background_task_pool_cancel(pool);
      break;
  }
}

bool lib_task_pool_current_canceled(TaskPool *pool)
{
  switch (pool->type) {
    case TASK_POOL_TBB:
    case TASK_POOL_TBB_SUSPENDED:
    case TASK_POOL_NO_THREADS:
      return tbb_task_pool_canceled(pool);
    case TASK_POOL_BACKGROUND:
    case TASK_POOL_BACKGROUND_SERIAL:
      return background_task_pool_canceled(pool);
  }
  lib_assert_msg(0, "lib_task_pool_canceled: Control flow should not come here!");
  return false;
}

void *lib_task_pool_user_data(TaskPool *pool)
{
  return pool->userdata;
}

ThreadMutex *lib_task_pool_user_mutex(TaskPool *pool)
{
  return &pool->user_mutex;
}
