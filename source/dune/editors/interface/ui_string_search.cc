#include <sstream>

#include "dune_appdir.h"

#include "api_userdef_types.h"

#include "ui_string_search.hh"

#include "lib_fileops.hh"
#include "lib_map.hh"
#include "lib_path_util.h"

namespace dune::ui::string_search {

using dune::string_search::RecentCache;

struct RecentCacheStorage {
  /* Is incremented every time a search item has been selected. This is used to keep track of the
   * order of recent searches. */
  int logical_clock = 0;
  RecentCache cache;
};

static RecentCacheStorage &get_recent_cache_storage()
{
  lib_assert((U.flag & USER_FLAG_RECENT_SEARCHES_DISABLE) == 0);
  static RecentCacheStorage storage;
  return storage;
}

void add_recent_search(const StringRef chosen_str)
{
  RecentCacheStorage &storage = get_recent_cache_storage();
  storage.cache.logical_time_by_str.add_overwrite(chosen_str, storage.logical_clock);
  storage.logical_clock++;
}

const RecentCache *get_recent_cache_or_null()
{
  if (U.flag & USER_FLAG_RECENT_SEARCHES_DISABLE) {
    return nullptr;
  }
  RecentCacheStorage &storage = get_recent_cache_storage();
  return &storage.cache;
}

static std::optional<std::string> get_recent_searches_file_path()
{
  const char *user_config_dir = dune_appdir_folder_id_create(DUNE_USER_CONFIG, nullptr);
  if (!user_config_dir) {
    return std::nullopt;
  }
  char filepath[FILE_MAX];
  lib_path_join(filepath, sizeof(filepath), user_config_dir, DUNE_RECENT_SEARCHES_FILE);
  return std::string(filepath);
}

void write_recent_searches_file()
{
  if (U.flag & USER_FLAG_RECENT_SEARCHES_DISABLE) {
    return;
  }

  const std::optional<std::string> path = get_recent_searches_file_path();
  if (!path) {
    return;
  }

  const RecentCacheStorage &storage = get_recent_cache_storage();
  Vector<std::pair<int, std::string>> values;
  for (const auto item : storage.cache.logical_time_by_str.items()) {
    values.append({item.value, item.key});
  }
  std::sort(values.begin(), values.end());

  fstream file(*path, std::ios::out);
  for (const auto &item : values) {
    file.write(item.second.data(), item.second.size());
    file.write("\n", 1);
  }
}

void read_recent_searches_file()
{
  if (U.flag & USER_FLAG_RECENT_SEARCHES_DISABLE) {
    return;
  }

  const std::optional<std::string> path = get_recent_searches_file_path();
  if (!path) {
    return;
  }

  RecentCacheStorage &storage = get_recent_cache_storage();
  storage.logical_clock = 0;
  storage.cache.logical_time_by_str.clear();

  fstream file(*path);
  std::string line;
  while (std::getline(file, line)) {
    storage.cache.logical_time_by_str.add_overwrite(line, storage.logical_clock);
    storage.logical_clock++;
  }
}

}  // namespace dune::ui::string_search
