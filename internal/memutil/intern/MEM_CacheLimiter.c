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
