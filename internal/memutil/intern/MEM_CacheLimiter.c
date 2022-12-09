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
