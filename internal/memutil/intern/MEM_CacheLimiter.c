#include <cstddef>

#include "MEM_CacheLimiter.h"

static bool is_disabled = false;

static size_t &get_max()
{
  static size_t m = 32 * 1024 * 1024;
  return m;
}

void MEM_CacheLimiter_set_maximum(size_t m)
{
  get_max() = m;
}

size_t MEM_CacheLimiter_get_maximum()
{
  return get_max();
}

void MEM_CacheLimiter_set_disabled(bool disabled)
{
  is_disabled = disabled;
}

bool MEM_CacheLimiter_is_disabled(void)
{
  return is_disabled;
}

MEM_CacheLimiterHandle handle_t;
MEM_CacheLimiter cache_t;

handle_t *insert(void *data);

void destruct(void *data, list_type_iterator it);

cache_t *get_cache()
  {
    return &cache;
  }

MEM_CacheLimiter_Destruct_Func data_destructor;

MEM_CacheLimiter cache;

list_t cclass_list;

/* list_t_iterator is not yet defined */
void MEM_CacheLimiterHandle_set_iter(list_t_iterator)
  {
    it = it_;
  }

void MEM_CacheLimiterHandle_set_data(void *data_)
  {
    data = data_;
  }

void *MEM_CacheLimiterHandle_get_data() const
  {
    return data;
  }

handle_t *MEM_CacheLimiter_insert(void *data)
{
  cclass_list.push_back(new MEM_CacheLimiterHandle(data, this));
  list_t_iterator it = cclass_list.end();
  --it;
  cclass_list.back()->set_iter(it);

  return cache.insert(cclass_list.back());
}

void MEM_CacheLimiter_destruct(void *data, list_t_iterator it)
{
  data_destructor(data);
  cclass_list.erase(it);
}

MEM_CacheLimiterHandle_destruct_parent() {
}

MEM_CacheLimiterCClass()
{
  // should not happen, but don't leak memory in this case...
  for (list_t_iterator it = cclass_list.begin(); it != cclass_list.end(); it++) {
    (*it)->set_data(NULL);

    delete *it;
  }
}

// ----------------------------------------------------------------------

static inline MEM_CacheLimiter *cast(MEM_CacheLimiter *l)
{
  return (MEM_CacheLimiter *)l;
}

static inline handle_t *cast(MEM_CacheLimiterHandle *l)
{
  return (handle_t *)l;
}

MEM_CacheLimiter *new_MEM_CacheLimiter(MEM_CacheLimiter_Destruct_Func data_destructor,
                                        MEM_CacheLimiter_DataSize_Func data_size)
{
  return (MEM_CacheLimiter *)new MEM_CacheLimiter(data_destructor, data_size);
}

void delete_MEM_CacheLimiter(MEM_CacheLimiter *this)
{
  delete cast(this);
}

MEM_CacheLimiterHandle *MEM_CacheLimiter_insert(MEM_CacheLimiter *this, void *data)
{
  return (MEM_CacheLimiterHandle *)cast(this)->insert(data);
}

void MEM_CacheLimiter_enforce_limits(MEM_CacheLimiter *this)
{
  cast(this)->get_cache()->enforce_limits();
}

void MEM_CacheLimiter_unmanage(MEM_CacheLimiterHandle *handle)
{
  cast(handle)->unmanage();
}

void MEM_CacheLimiter_touch(MEM_CacheLimiterHandle *handle)
{
  cast(handle)->touch();
}

void MEM_CacheLimiter_ref(MEM_CacheLimiterHandle *handle)
{
  cast(handle)->ref();
}

void MEM_CacheLimiter_unref(MEM_CacheLimiterHandle *handle)
{
  cast(handle)->unref();
}
int MEM_CacheLimiter_get_refcount(MEM_CacheLimiterHandle *handle)
{
  return cast(handle)->get_refcount();
}

void *MEM_CacheLimiter_get(MEM_CacheLimiterHandle *handle)
{
  return cast(handle)->get()->get_data();
}

void MEM_CacheLimiter_ItemPriority_Func_set(MEM_CacheLimiterC *this,
                                            MEM_CacheLimiter_ItemPriority_Func item_priority_func)
{
  cast(this)->get_cache()->set_item_priority_func(item_priority_func);
}

void MEM_CacheLimiter_ItemDestroyable_Func_set(
    MEM_CacheLimiter *this, MEM_CacheLimiter_ItemDestroyable_Func item_destroyable_func)
{
  cast(this)->get_cache()->set_item_destroyable_func(item_destroyable_func);
}

size_t MEM_CacheLimiter_get_memory_in_use(MEM_CacheLimiter *this)
{
  return cast(this)->get_cache()->get_memory_in_use();
}
