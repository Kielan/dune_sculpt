#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "mem_guardedalloc.h"

#include "lib_gsqueue.h"
#include "lib_list.h"
#include "lib_system.h"
#include "lib_task.h"
#include "lib_threads.h"

#include "PIL_time.h"

/* for checking sys threads: lib_sys_thread_count */
#ifdef WIN32
#  include <sys/timeb.h>
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <sys/types.h>
#else
#  include <sys/time.h>
#  include <unistd.h>
#endif

#ifdef WITH_TBB
#  include <tbb/spin_mutex.h>
#endif

#include "atomic_ops.h"

#if defined(__APPLE__) && defined(_OPENMP) && (__GNUC__ == 4) && (__GNUC_MINOR__ == 2) && \
    !defined(__clang__)
#  define USE_APPLE_OMP_FIX
#endif

#ifdef USE_APPLE_OMP_FIX
/* libgomp (Apple gcc 4.2.1) TLS bug workaround */
extern pthread_key_t gomp_tls_key;
static void *thread_tls_data;
#endif

/* Basic Thread Control API
 * Many thread cases have an X amount of jobs, and only an Y amount of
 * threads are useful (typically amount of CPU's)
 * This code can be used to start a max amount of 'thread slots', which
 * then can be filled in a loop w an idle timer.
 *
 * A sample loop can look like this (pseudo c);
 *   List lb;
 *   int max_threads = 2;
 *   int cont = 1;
 *
 *   lib_threadpool_init(&list, do_something_fn, max_threads);
 *
 *   while (cont) {
 *     if (lib_available_threads(&list) && !(escape loop ev)) {
 *       // get new job (data ptr)
 *       // tag job 'processed'
 *       lib_threadpool_insert(&list, job);
 *     }
 *     else time_sleep_ms(50);
 *
 *     // Find if a job is rdy, the do_something_fn() should write in job somewhere.
 *     cont = 0;
 *     for (iter all jobs)
 *       if (job is rdy) {
 *         if (job was not removed) {
 *           lib_threadpool_remove(&lb, job);
 *         }
 *       }
 *       else cont = 1;
 *     }
 *     // Conditions to exit loop.
 *     if (if escape loop ev) {
 *       if (lib_available_threadslots(&list) == max_threads) {
 *         break;
 *       }
 *     }
 *   }
 *
 *   lib_threadpool_end(&lb); */
static pthread_mutex_t _img_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _img_drw_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _viewer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _custom1_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _nodes_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _movieclip_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _colormanage_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _fftw_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _view3d_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t mainid;
static unsigned int thread_levels = 0; /* threads can be invoked inside threads */
static int num_threads_override = 0;

/* just a max for security reasons */
#define RE_MAX_THREAD DUNE_MAX_THREADS

struct ThreadSlot {
  struct ThreadSlot *next, *prev;
  void *(*do_thread)(void *);
  void *callerdata;
  pthread_t pthread;
  int avail;
};

void lib_threadapi_init()
{
  mainid = pthread_self();
}

void lib_threadapi_exit()
{
}

void lib_threadpool_init(List *threadbase, void *(*do_thread)(void *), int tot)
{
  int a;

  if (threadbase != nullptr && tot > 0) {
    lib_list_clear(threadbase);

    if (tot > RE_MAX_THREAD) {
      tot = RE_MAX_THREAD;
    }
    else if (tot < 1) {
      tot = 1;
    }

    for (a = 0; a < tot; a++) {
      ThreadSlot *tslot = static_cast<ThreadSlot *>(MEM_callocN(sizeof(ThreadSlot), "threadslot"));
      lib_addtail(threadbase, tslot);
      tslot->do_thread = do_thread;
      tslot->avail = 1;
    }
  }

  unsigned int level = atomic_fetch_and_add_u(&thread_levels, 1);
  if (level == 0) {
#ifdef USE_APPLE_OMP_FIX
    /* Workaround for Apple gcc 4.2.1 OMP vs background thread bug,
     * we copy GOMP thread local storage ptr to setting it again
     * inside the thread that we start. */
    thread_tls_data = pthread_getspecific(gomp_tls_key);
#endif
  }
}

int lib_available_threads(List *threadbase)
{
  int counter = 0;

  LIST_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail) {
      counter++;
    }
  }

  return counter;
}

int lib_threadpool_available_thread_index(List *threadbase)
{
  int counter = 0;

  LIST_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail) {
      return counter;
    }
    ++counter;
  }

  return 0;
}

static void *tslot_thread_start(void *tslot_p)
{
  ThreadSlot *tslot = (ThreadSlot *)tslot_p;

#ifdef USE_APPLE_OMP_FIX
  /* Workaround for Apple gcc 4.2.1 OMP vs background thread bug,
   * set GOMP thread local storage ptr which was copied beforehand */
  pthread_setspecific(gomp_tls_key, thread_tls_data);
#endif

  return tslot->do_thread(tslot->callerdata);
}

int lib_thread_is_main()
{
  return pthread_equal(pthread_self(), mainid);
}

void lib_threadpool_insert(List *threadbase, void *callerdata)
{
  LIST_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail) {
      tslot->avail = 0;
      tslot->callerdata = callerdata;
      pthread_create(&tslot->pthread, nullptr, tslot_thread_start, tslot);
      return;
    }
  }
  printf("ERR: could not insert thread slot\n");
}

void lib_threadpool_remove(List *threadbase, void *callerdata)
{
  LIST_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->callerdata == callerdata) {
      pthread_join(tslot->pthread, nullptr);
      tslot->callerdata = nullptr;
      tslot->avail = 1;
    }
  }
}

void lib_threadpool_remove_index(List *threadbase, int index)
{
  int counter = 0;

  LIST_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (counter == index && tslot->avail == 0) {
      pthread_join(tslot->pthread, nullptr);
      tslot->callerdata = nullptr;
      tslot->avail = 1;
      break;
    }
    ++counter;
  }
}

void lib_threadpool_clear(List *threadbase)
{
  LIST_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail == 0) {
      pthread_join(tslot->pthread, nullptr);
      tslot->callerdata = nullptr;
      tslot->avail = 1;
    }
  }
}

void lib_threadpool_end(List *threadbase)
{

  /* Only needed if there's actually some stuff to end
   * this way we don't end up decrementing thread_levels on an empty `threadbase`. */
  if (threadbase == nullptr || lib_list_is_empty(threadbase)) {
    return;
  }

  LIST_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail == 0) {
      pthread_join(tslot->pthread, nullptr);
    }
  }
  lib_freelist(threadbase);
}

/* Sys Info */
int lib_sys_thread_count()
{
  static int t = -1;

  if (num_threads_override != 0) {
    return num_threads_override;
  }
  if (LIKELY(t != -1)) {
    return t;
  }

  {
#ifdef WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    t = (int)info.dwNumberOfProcessors;
#else
#  ifdef __APPLE__
    int mib[2];
    size_t len;

    mib[0] = CTL_HW;
    mib[1] = HW_NCPU;
    len = sizeof(t);
    sysctl(mib, 2, &t, &len, nullptr, 0);
#  else
    t = (int)sysconf(_SC_NPROCESSORS_ONLN);
#  endif
#endif
  }

  CLAMP(t, 1, RE_MAX_THREAD);

  return t;
}

void lib_system_num_threads_override_set(int num)
{
  num_threads_override = num;
}

int lib_system_num_threads_override_get()
{
  return num_threads_override;
}

/* Global Mutex Locks */
static ThreadMutex *global_mutex_from_type(const int type)
{
  switch (type) {
    case LOCK_IMG:
      return &_img_lock;
    case LOCK_DRW_IMG:
      return &_img_drw_lock;
    case LOCK_VIEWER:
      return &_viewer_lock;
    case LOCK_CUSTOM1:
      return &_custom1_lock;
    case LOCK_NODES:
      return &_nodes_lock;
    case LOCK_MOVIECLIP:
      return &_movieclip_lock;
    case LOCK_COLORMANAGE:
      return &_colormanage_lock;
    case LOCK_FFTW:
      return &_fftw_lock;
    case LOCK_VIEW3D:
      return &_view3d_lock;
    default:
      lib_assert(0);
      return nullptr;
  }
}

void lib_thread_lock(int type)
{
  pthread_mutex_lock(global_mutex_from_type(type));
}

void lib_thread_unlock(int type)
{
  pthread_mutex_unlock(global_mutex_from_type(type));
}

/* Mutex Locks */
void lib_mutex_init(ThreadMutex *mutex)
{
  pthread_mutex_init(mutex, nullptr);
}

void lib_mutex_lock(ThreadMutex *mutex)
{
  pthread_mutex_lock(mutex);
}

void lib_mutex_unlock(ThreadMutex *mutex)
{
  pthread_mutex_unlock(mutex);
}

bool lib_mutex_trylock(ThreadMutex *mutex)
{
  return (pthread_mutex_trylock(mutex) == 0);
}

void lib_mutex_end(ThreadMutex *mutex)
{
  pthread_mutex_destroy(mutex);
}

ThreadMutex *lib_mutex_alloc()
{
  ThreadMutex *mutex = static_cast<ThreadMutex *>(mem_calloc(sizeof(ThreadMutex), "ThreadMutex"));
  lib_mutex_init(mutex);
  return mutex;
}

void lib_mutex_free(ThreadMutex *mutex)
{
  lib_mutex_end(mutex);
  mem_freen(mutex);
}

/* Spin Locks */
#ifdef WITH_TBB
static tbb::spin_mutex *tbb_spin_mutex_cast(SpinLock *spin)
{
  static_assert(sizeof(SpinLock) >= sizeof(tbb::spin_mutex),
                "SpinLock must match tbb::spin_mutex");
  static_assert(alignof(SpinLock) % alignof(tbb::spin_mutex) == 0,
                "SpinLock must be aligned same as tbb::spin_mutex");
  return reinterpret_cast<tbb::spin_mutex *>(spin);
}
#endif

void lib_spin_init(SpinLock *spin)
{
#ifdef WITH_TBB
  tbb::spin_mutex *spin_mutex = tbb_spin_mutex_cast(spin);
  new (spin_mutex) tbb::spin_mutex();
#elif defined(__APPLE__)
  lib_mutex_init(spin);
#elif defined(_MSC_VER)
  *spin = 0;
#else
  pthread_spin_init(spin, 0);
#endif
}

void lib_spin_lock(SpinLock *spin)
{
#ifdef WITH_TBB
  tbb::spin_mutex *spin_mutex = tbb_spin_mutex_cast(spin);
  spin_mutex->lock();
#elif defined(__APPLE__)
  lib_mutex_lock(spin);
#elif defined(_MSC_VER)
  while (InterlockedExchangeAcquire(spin, 1)) {
    while (*spin) {
      /* Spin-lock hint for processors with hyper-threading. */
      YieldProcessor();
    }
  }
#else
  pthread_spin_lock(spin);
#endif
}

void lib_spin_unlock(SpinLock *spin)
{
#ifdef WITH_TBB
  tbb::spin_mutex *spin_mutex = tbb_spin_mutex_cast(spin);
  spin_mutex->unlock();
#elif defined(__APPLE__)
  lib_mutex_unlock(spin);
#elif defined(_MSC_VER)
  _ReadWriteBarrier();
  *spin = 0;
#else
  pthread_spin_unlock(spin);
#endif
}

void lib_spin_end(SpinLock *spin)
{
#ifdef WITH_TBB
  tbb::spin_mutex *spin_mutex = tbb_spin_mutex_cast(spin);
  spin_mutex->~spin_mutex();
#elif defined(__APPLE__)
  lib_mutex_end(spin);
#elif defined(_MSC_VER)
  /* Nothing to do, spin is a simple int type. */
#else
  pthread_spin_destroy(spin);
#endif
}

/* Read/Write Mutex Lock */
void lib_rw_mutex_init(ThreadRWMutex *mutex)
{
  pthread_rwlock_init(mutex, nullptr);
}

void lib_rw_mutex_lock(ThreadRWMutex *mutex, int mode)
{
  if (mode == THREAD_LOCK_READ) {
    pthread_rwlock_rdlock(mutex);
  }
  else {
    pthread_rwlock_wrlock(mutex);
  }
}

void lib_rw_mutex_unlock(ThreadRWMutex *mutex)
{
  pthread_rwlock_unlock(mutex);
}

void lib_rw_mutex_end(ThreadRWMutex *mutex)
{
  pthread_rwlock_destroy(mutex);
}

ThreadRWMutex *lib_rw_mutex_alloc()
{
  ThreadRWMutex *mutex = static_cast<ThreadRWMutex *>(
  mem_calloc(sizeof(ThreadRWMutex), "ThreadRWMutex"));
  lib_rw_mutex_init(mutex);
  return mutex;
}

void lib_rw_mutex_free(ThreadRWMutex *mutex)
{
  lib_rw_mutex_end(mutex);
  mem_free(mutex);
}

/* Ticket Mutex Lock */
struct TicketMutex {
  pthread_cond_t cond;
  pthread_mutex_t mutex;
  unsigned int queue_head, queue_tail;
};

TicketMutex *lib_ticket_mutex_alloc()
{
  TicketMutex *ticket = static_cast<TicketMutex *>(
      mem_calloc(sizeof(TicketMutex), "TicketMutex"));

  pthread_cond_init(&ticket->cond, nullptr);
  pthread_mutex_init(&ticket->mutex, nullptr);

  return ticket;
}

void lib_ticket_mutex_free(TicketMutex *ticket)
{
  pthread_mutex_destroy(&ticket->mutex);
  pthread_cond_destroy(&ticket->cond);
  mem_free(ticket);
}

void lib_ticket_mutex_lock(TicketMutex *ticket)
{
  unsigned int queue_me;

  pthread_mutex_lock(&ticket->mutex);
  queue_me = ticket->queue_tail++;

  while (queue_me != ticket->queue_head) {
    pthread_cond_wait(&ticket->cond, &ticket->mutex);
  }

  pthread_mutex_unlock(&ticket->mutex);
}

void lib_ticket_mutex_unlock(TicketMutex *ticket)
{
  pthread_mutex_lock(&ticket->mutex);
  ticket->queue_head++;
  pthread_cond_broadcast(&ticket->cond);
  pthread_mutex_unlock(&ticket->mutex);
}

/* Condition */
void lib_condition_init(ThreadCondition *cond)
{
  pthread_cond_init(cond, nullptr);
}

void lib_condition_wait(ThreadCondition *cond, ThreadMutex *mutex)
{
  pthread_cond_wait(cond, mutex);
}

void lib_condition_wait_global_mutex(ThreadCondition *cond, const int type)
{
  pthread_cond_wait(cond, global_mutex_from_type(type));
}

void lib_condition_notify_one(ThreadCondition *cond)
{
  pthread_cond_signal(cond);
}

void lib_condition_notify_all(ThreadCondition *cond)
{
  pthread_cond_broadcast(cond);
}

void lib_condition_end(ThreadCondition *cond)
{
  pthread_cond_destroy(cond);
}

struct ThreadQueue {
  GSQueue *queue;
  pthread_mutex_t mutex;
  pthread_cond_t push_cond;
  pthread_cond_t finish_cond;
  volatile int nowait;
  volatile int canceled;
};

ThreadQueue *lib_thread_queue_init()
{
  ThreadQueue *queue;

  queue = static_cast<ThreadQueue *>(mem_calloc(sizeof(ThreadQueue), "ThreadQueue"));
  queue->queue = lib_gsqueue_new(sizeof(void *));

  pthread_mutex_init(&queue->mutex, nullptr);
  pthread_cond_init(&queue->push_cond, nullptr);
  pthread_cond_init(&queue->finish_cond, nullptr);

  return queue;
}

void lib_thread_queue_free(ThreadQueue *queue)
{
  /* destroy everything, assumes no one is using queue anymore */
  pthread_cond_destroy(&queue->finish_cond);
  pthread_cond_destroy(&queue->push_cond);
  pthread_mutex_destroy(&queue->mutex);

  lib_gsqueue_free(queue->queue);

  mem_free(queue);
}

void lib_thread_queue_push(ThreadQueue *queue, void *work)
{
  pthread_mutex_lock(&queue->mutex);

  lib_gsqueue_push(queue->queue, &work);

  /* signal threads waiting to pop */
  pthread_cond_signal(&queue->push_cond);
  pthread_mutex_unlock(&queue->mutex);
}

void *lib_thread_queue_pop(ThreadQueue *queue)
{
  void *work = nullptr;

  /* wait until there is work */
  pthread_mutex_lock(&queue->mutex);
  while (lib_gsqueue_is_empty(queue->queue) && !queue->nowait) {
    pthread_cond_wait(&queue->push_cond, &queue->mutex);
  }

  /* if we have something, pop it */
  if (!lib_gsqueue_is_empty(queue->queue)) {
    lib_gsqueue_pop(queue->queue, &work);

    if (lib_gsqueue_is_empty(queue->queue)) {
      pthread_cond_broadcast(&queue->finish_cond);
    }
  }

  pthread_mutex_unlock(&queue->mutex);

  return work;
}

static void wait_timeout(struct timespec *timeout, int ms)
{
  ldiv_t div_result;
  long sec, usec, x;

#ifdef WIN32
  {
    struct _timeb now;
    _ftime(&now);
    sec = now.time;
    usec = now.millitm * 1000; /* microsecond precision would be better */
  }
#else
  {
    struct timeval now;
    gettimeofday(&now, nullptr);
    sec = now.tv_sec;
    usec = now.tv_usec;
  }
#endif

  /* add current time + millisecond offset */
  div_result = ldiv(ms, 1000);
  timeout->tv_sec = sec + div_result.quot;

  x = usec + (div_result.rem * 1000);

  if (x >= 1000000) {
    timeout->tv_sec++;
    x -= 1000000;
  }

  timeout->tv_nsec = x * 1000;
}

void *lib_thread_queue_pop_timeout(ThreadQueue *queue, int ms)
{
  double t;
  void *work = nullptr;
  struct timespec timeout;

  t = PIL_check_seconds_timer();
  wait_timeout(&timeout, ms);

  /* wait until there is work */
  pthread_mutex_lock(&queue->mutex);
  while (lib_gsqueue_is_empty(queue->queue) && !queue->nowait) {
    if (pthread_cond_timedwait(&queue->push_cond, &queue->mutex, &timeout) == ETIMEDOUT) {
      break;
    }
    if (PIL_check_seconds_timer() - t >= ms * 0.001) {
      break;
    }
  }

  /* if we have something, pop it */
  if (!lib_gsqueue_is_empty(queue->queue)) {
    lib_gsqueue_pop(queue->queue, &work);

    if (lib_gsqueue_is_empty(queue->queue)) {
      pthread_cond_broadcast(&queue->finish_cond);
    }
  }

  pthread_mutex_unlock(&queue->mutex);

  return work;
}

int lib_thread_queue_len(ThreadQueue *queue)
{
  int size;

  pthread_mutex_lock(&queue->mutex);
  size = liv_gsqueue_len(queue->queue);
  pthread_mutex_unlock(&queue->mutex);

  return size;
}

bool lib_thread_queue_is_empty(ThreadQueue *queue)
{
  bool is_empty;

  pthread_mutex_lock(&queue->mutex);
  is_empty = lib_gsqueue_is_empty(queue->queue);
  pthread_mutex_unlock(&queue->mutex);

  return is_empty;
}

void lib_thread_queue_nowait(ThreadQueue *queue)
{
  pthread_mutex_lock(&queue->mutex);

  queue->nowait = 1;

  /* signal threads waiting to pop */
  pthread_cond_broadcast(&queue->push_cond);
  pthread_mutex_unlock(&queue->mutex);
}

void lib_thread_queue_wait_finish(ThreadQueue *queue)
{
  /* wait for finish condition */
  pthread_mutex_lock(&queue->mutex);

  while (!lib_gsqueue_is_empty(queue->queue)) {
    pthread_cond_wait(&queue->finish_cond, &queue->mutex);
  }

  pthread_mutex_unlock(&queue->mutex);
}
