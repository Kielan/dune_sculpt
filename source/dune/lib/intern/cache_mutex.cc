#include "lib_cache_mutex.hh"
#include "lib_task.hh"

namespace dune {

void CacheMutex::ensure(const FnRef<void()> compute_cache)
{
  if (cache_valid_.load(std::memory_order_acquire)) {
    return;
  }
  std::scoped_lock lock{mutex_};
  /* Double checked lock. */
  if (cache_valid_.load(std::memory_order_relaxed)) {
    return;
  }
  /* Use task isolation bc a mutex is locked and the cache computation might use
   * multi-threading. */
  threading::isolate_task(compute_cache);

  cache_valid_.store(true, std::memory_order_release);
}

}  // namespace dune
