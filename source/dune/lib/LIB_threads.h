#pragma once

#include <pthread.h>

#include "lib_sys_types.h"

/** For tables, button in UI, etc. */
#define DUNE_MAX_THREADS 1024

struct ListBase;

/* Threading API */

/* This is run once at startup. */
void lib_threadapi_init(void);
void lib_threadapi_exit(void);

/**
 * param tot: When 0 only initializes malloc mutex in a safe way (see sequence.c)
 * problem otherwise: scene render will kill of the mutex!
 */
void lib_threadpool_init(struct ListBase *threadbase, void *(*do_thread)(void *), int tot);
/** Amount of available threads. */
int lib_available_threads(struct ListBase *threadbase);
/** Returns thread number, for sample patterns or threadsafe tables. */
int LIB_threadpool_available_thread_index(struct ListBase *threadbase);
void LIB_threadpool_insert(struct ListBase *threadbase, void *callerdata);
void LIB_threadpool_remove(struct ListBase *threadbase, void *callerdata);
void LIB_threadpool_remove_index(struct ListBase *threadbase, int index);
void LIB_threadpool_clear(struct ListBase *threadbase);
void LIB_threadpool_end(struct ListBase *threadbase);
int LIB_thread_is_main(void);

/* System Information */

/**
 * return the number of threads the system can make use of.
 */
int LIB_system_thread_count(void);
void LIB_system_num_threads_override_set(int num);
int LIB_system_num_threads_override_get(void);

/**
 * Global Mutex Locks
 *
 * One custom lock available now. can be extended.
 */
enum {
  LOCK_IMAGE = 0,
  LOCK_DRAW_IMAGE,
  LOCK_VIEWER,
  LOCK_CUSTOM1,
  LOCK_NODES,
  LOCK_MOVIECLIP,
  LOCK_COLORMANAGE,
  LOCK_FFTW,
  LOCK_VIEW3D,
};

void LIB_thread_lock(int type);
void LIB_thread_unlock(int type);

/* Mutex Lock */

typedef pthread_mutex_t ThreadMutex;
#define LIB_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

void LIB_mutex_init(ThreadMutex *mutex);
void LIB_mutex_end(ThreadMutex *mutex);

ThreadMutex *LIB_mutex_alloc(void);
void LIB_mutex_free(ThreadMutex *mutex);

void LIB_mutex_lock(ThreadMutex *mutex);
bool LIB_mutex_trylock(ThreadMutex *mutex);
void LIB_mutex_unlock(ThreadMutex *mutex);

/* Spin Lock */
typedef pthread_spinlock_t SpinLock;

void LIB_spin_init(SpinLock *spin);
void LIB_spin_lock(SpinLock *spin);
void LIB_spin_unlock(SpinLock *spin);
void LIB_spin_end(SpinLock *spin);

/* Read/Write Mutex Lock */

#define THREAD_LOCK_READ 1
#define THREAD_LOCK_WRITE 2

#define LIB_RWLOCK_INITIALIZER PTHREAD_RWLOCK_INITIALIZER

typedef pthread_rwlock_t ThreadRWMutex;

void LIB_rw_mutex_init(ThreadRWMutex *mutex);
void LIB_rw_mutex_end(ThreadRWMutex *mutex);

ThreadRWMutex *LIB_rw_mutex_alloc(void);
void LIB_rw_mutex_free(ThreadRWMutex *mutex);

void LIB_rw_mutex_lock(ThreadRWMutex *mutex, int mode);
void LIB_rw_mutex_unlock(ThreadRWMutex *mutex);

/* Ticket Mutex Lock
 *
 * This is a 'fair' mutex in that it will grant the lock to the first thread
 * that requests it. */

typedef struct TicketMutex TicketMutex;

TicketMutex *LIB_ticket_mutex_alloc(void);
void LIB_ticket_mutex_free(TicketMutex *ticket);
void LIB_ticket_mutex_lock(TicketMutex *ticket);
void LIB_ticket_mutex_unlock(TicketMutex *ticket);

/* Condition */

typedef pthread_cond_t ThreadCondition;

void LIB_condition_init(ThreadCondition *cond);
void LIB_condition_wait(ThreadCondition *cond, ThreadMutex *mutex);
void LIB_condition_wait_global_mutex(ThreadCondition *cond, int type);
void LIB_condition_notify_one(ThreadCondition *cond);
void LIB_condition_notify_all(ThreadCondition *cond);
void LIB_condition_end(ThreadCondition *cond);

/* ThreadWorkQueue
 *
 * Thread-safe work queue to push work/pointers between threads. */

typedef struct ThreadQueue ThreadQueue;

ThreadQueue *LIB_thread_queue_init(void);
void LIB_thread_queue_free(ThreadQueue *queue);

void LIB_thread_queue_push(ThreadQueue *queue, void *work);
void *LIB_thread_queue_pop(ThreadQueue *queue);
void *LIB_thread_queue_pop_timeout(ThreadQueue *queue, int ms);
int LIB_thread_queue_len(ThreadQueue *queue);
bool LIB_thread_queue_is_empty(ThreadQueue *queue);

void LIB_thread_queue_wait_finish(ThreadQueue *queue);
void LIB_thread_queue_nowait(ThreadQueue *queue);

/* Thread local storage */

#if defined(__APPLE__)
#  define ThreadLocal(type) pthread_key_t
#  define LIB_thread_local_create(name) pthread_key_create(&name, NULL)
#  define LIB_thread_local_delete(name) pthread_key_delete(name)
#  define LIB_thread_local_get(name) pthread_getspecific(name)
#  define LIB_thread_local_set(name, value) pthread_setspecific(name, value)
#else /* defined(__APPLE__) */
#  ifdef _MSC_VER
#    define ThreadLocal(type) __declspec(thread) type
#  else
#    define ThreadLocal(type) __thread type
#  endif
#  define LIB_thread_local_create(name)
#  define LIB_thread_local_delete(name)
#  define LIB_thread_local_get(name) name
#  define LIB_thread_local_set(name, value) name = value
#endif /* defined(__APPLE__) */
