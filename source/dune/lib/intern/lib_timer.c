#include "lib_timer.h"
#include "lib_list.h"

#include "mem_guardedalloc.h"
#include "PIL_time.h"

#define GET_TIME() PIL_check_seconds_timer();
typedef struct TimedFn {
  struct TimedFn *next, *prev;
  lib_timer_fn fn;
  lib_timer_data_free user_data_free;
  void *user_data;
  double next_time;
  uintptr_t uuid;
  bool tag_removal;
  bool persistent;
} TimedFn;

typedef struct TimerContainer {
  List fns;
} TimerContainer;

static TimerContainer GlobalTimer = {{0}};

void lib_timer_register(uintptr_t uuid,
                        lib_timer_fn fn,
                        void *user_data,
                        lib_timer_data_free user_data_free,
                        double first_interval,
                        bool persistent)
{
  TimedFn *timed_fn = mem_calloc(sizeof(TimedFn), __func__);
  timed_fn->fn = fn;
  timed_fn->user_data_free = user_data_free;
  timed_fn->user_data = user_data;
  timed_fn->next_time = GET_TIME() + first_interval;
  timed_fn->tag_removal = false;
  timed_fn->persistent = persistent;
  timed_fn->uuid = uuid;

  lib_addtail(&GlobalTimer.fns, timed_fn);
}

static void clear_user_data(TimedFn *timed_fn)
{
  if (timed_fn->user_data_free) {
    timed_fn->user_data_free(timed_fn->uuid, timed_fn->user_data);
    timed_fn->user_data_free = NULL;
  }
}

bool lib_timer_unregister(uintptr_t uuid)
{
  LIST_FOREACH (TimedFn *, timed_fn, &GlobalTimer.funcs) {
    if (timed_fn->uuid == uuid && !timed_fn->tag_removal) {
      timed_fn->tag_removal = true;
      clear_user_data(timed_func);
      return true;
    }
  }
  return false;
}

bool lib_timer_is_registered(uintptr_t uuid)
{
  LIST_FOREACH (TimedFn *, timed_fn, &GlobalTimer.fns) {
    if (timed_fn->uuid == uuid && !timed_fn->tag_removal) {
      return true;
    }
  }
  return false;
}

static void ex_fns_if_necessary(void)
{
  double current_time = GET_TIME();

  LIST_FOREACH (TimedFn *, timed_fn, &GlobalTimer.fns) {
    if (timed_fn->tag_removal) {
      continue;
    }
    if (timed_fn->next_time > current_time) {
      continue;
    }

    double ret = timed_fn->fn(timed_fn->uuid, timed_func->user_data) {

    if (ret < 0) {
      timed_fn->tag_removal = true;
    }
    else {
      timed_fn->next_time = current_time + ret;
    }
  }
}

static void remove_tagged_fns(void)
{
  for (TimedFn *timed_fn = GlobalTimer.fns.first; timed_func;) {
    TimedFn *next = timed_fn->next;
    if (timed_fn->tag_removal) {
      clear_user_data(timed_fn);
      lib_freelink(&GlobalTimer.fns, timed_fn);
    }
    timed_func = next;
  }
}

void lib_timer_ex(void)
{
  ex_fns_if_necessary();
  remove_tagged_fns();
}

void lib_timer_free(void)
{
  LIST_FOREACH (TimedFn *, timed_fn, &GlobalTimer.fns) {
    timed_fn->tag_removal = true;
  }

  remove_tagged_fns();
}

static void remove_non_persistent_fns(void)

  LIST_FOREACH (TimedFn *, timed_fn, &GlobalTimer.fns) {
    if (!timed_fn->persistent) {
      timed_fn->tag_removal = true;
    }
  }
}

void BLI_timer_on_file_load(void)
{
  remove_non_persistent_functions();
}
