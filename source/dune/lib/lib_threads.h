#pragma once

#include <pthread.h>

#include "lib_sys_types.h"

/* For tables, btn in UI, etc. */
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
void lib_threadpool_init(struct List *threadbase, void *(*do_thread)(void *), int tot);
/** Amount of available threads. */
int lib_available_threads(struct List *threadbase);
/** Returns thread number, for sample patterns or threadsafe tables. */
int lib_threadpool_available_thread_index(struct List *threadbase);
void lib_threadpool_insert(struct List *threadbase, void *callerdata);
void lib_threadpool_remove(struct List *threadbase, void *callerdata);
void lib_threadpool_remove_index(struct List *threadbase, int index);
void lib_threadpool_clear(struct List *threadbase);
void lib_threadpool_end(struct List *threadbase);
int lin_thread_is_main(void);

/* System Information */

/* return the nmbr of threads the sys can make use of. */
int lib_sys_thread_count(void);
void lib_sys_num_threads_override_set(int num);
int lib_sys_num_threads_override_get(void);

/* Global Mutex Locks
 * One custom lock available now. can be extended. */
enum {
  LOCK_IMG = 0,
  LOCK_DRW_IMG,
  LOCK_VIEWER,
  LOCK_CUSTOM1,
  LOCK_NODES,
  LOCK_MOVIECLIP,
  LOCK_COLORMANAGE,
  LOCK_FFTW,
  LOCK_VIEW3D,
};

void lib_thread_lock(int type);
void lib_thread_unlock(int type);

/* Mutex Lock */

typedef pthread_mutex_t ThreadMutex;
#define LIB_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

void lib_mutex_init(ThreadMutex *mutex);
void lib_mutex_end(ThreadMutex *mutex);

ThreadMutex *lib_mutex_alloc(void);
void lib_mutex_free(ThreadMutex *mutex);

void lib_mutex_lock(ThreadMutex *mutex);
bool lib_mutex_trylock(ThreadMutex *mutex);
void lib_mutex_unlock(ThreadMutex *mutex);

/* Spin Lock */
typedef pthread_spinlock_t SpinLock;

void lib_spin_init(SpinLock *spin);
void lib_spin_lock(SpinLock *spin);
void lib_spin_unlock(SpinLock *spin);
void lib_spin_end(SpinLock *spin);

/* Read/Write Mutex Lock */
#define THREAD_LOCK_READ 1
#define THREAD_LOCK_WRITE 2

#define LIB_RWLOCK_INITIALIZER PTHREAD_RWLOCK_INITIALIZER

typedef pthread_rwlock_t ThreadRWMutex;

void lib_rw_mutex_init(ThreadRWMutex *mutex);
void lib_rw_mutex_end(ThreadRWMutex *mutex);

ThreadRWMutex *lib_rw_mutex_alloc(void);
void lib_rw_mutex_free(ThreadRWMutex *mutex);

void lib_rw_mutex_lock(ThreadRWMutex *mutex, int mode);
void lib_rw_mutex_unlock(ThreadRWMutex *mutex);

/* Ticket Mutex Lock
 * This is a 'fair' mutex in that it will grant the lock to the first thread
 * that requests it. */
typedef struct TicketMutex TicketMutex;

TicketMutex *lib_ticket_mutex_alloc(void);
void lib_ticket_mutex_free(TicketMutex *ticket);
void lib_ticket_mutex_lock(TicketMutex *ticket);
void lib_ticket_mutex_unlock(TicketMutex *ticket);

/* Condition */
typedef pthread_cond_t ThreadCondition;

void lib_condition_init(ThreadCondition *cond);
void lib_condition_wait(ThreadCondition *cond, ThreadMutex *mutex);
void lib_condition_wait_global_mutex(ThreadCondition *cond, int type);
void lib_condition_notify_one(ThreadCondition *cond);
void lib_condition_notify_all(ThreadCondition *cond);
void lib_condition_end(ThreadCondition *cond);

/* ThreadWorkQueue
 * Thread-safe work queue to push work/ptrs between threads. */
typedef struct ThreadQueue ThreadQueue;

ThreadQueue *lib_thread_queue_init(void);
void lib_thread_queue_free(ThreadQueue *queue);

void lib_thread_queue_push(ThreadQueue *queue, void *work);
void *lib_thread_queue_pop(ThreadQueue *queue);
void *lib_thread_queue_pop_timeout(ThreadQueue *queue, int ms);
int lib_thread_queue_len(ThreadQueue *queue);
bool lib_thread_queue_is_empty(ThreadQueue *queue);

void lib_thread_queue_wait_finish(ThreadQueue *queue);
void lib_thread_queue_nowait(ThreadQueue *queue);

/* Thread local storage */
#if defined(__APPLE__)
#  define ThreadLocal(type) pthread_key_t
#  define lib_thread_local_create(name) pthread_key_create(&name, NULL)
#  define lib_thread_local_delete(name) pthread_key_delete(name)
#  define lib_thread_local_get(name) pthread_getspecific(name)
#  define lib_thread_local_set(name, val) pthread_setspecific(name, val)
#else /* defined(__APPLE__) */
#  ifdef _MSC_VER
#    define ThreadLocal(type) __declspec(thread) type
#  else
#    define ThreadLocal(type) __thread type
#  endif
#  define lib_thread_local_create(name)
#  define lib_thread_local_delete(name)
#  define lib_thread_local_get(name) name
#  define lib_thread_local_set(name, val) name = val
#endif /* defined(__APPLE__) */
