#include "asset_lib_service.hh"

#include "dune.h"

#include "lib_fileops.h" /* For PATH_MAX (at least on Windows). */
#include "lib_path_util.h"
#include "lib_string_ref.hh"

#include "CLG_log.h"

static CLG_LogRef LOG = {"dune.asset_service"};

namespace dune {

std::unique_ptr<AssetLibService> AssetLibService::instance_;
bool AssetLibService::atexit_handler_registered_ = false;

AssetLibService *AssetLibService::get()
{
  if (!instance_) {
    allocate_service_instance();
  }
  return instance_.get();
}

void AssetLibService::destroy()
{
  if (!instance_) {
    return;
  }
  instance_->app_handler_unregister();
  instance_.reset();
}

namespace {
std::string normalize_directory_path(StringRefNull directory)
{

  char dir_normalized[PATH_MAX];
  STRNCPY(dir_normalized, directory.c_str());
  lib_path_normalize_dir(nullptr, dir_normalized);
  return std::string(dir_normalized);
}
}  // namespace

AssetLib *AssetLibService::get_asset_lib_on_disk(StringRefNull top_level_directory)
{
  lib_assert_msg(!top_level_directory.is_empty(),
                 "top level directory must be given for on-disk asset lib");

  std::string top_dir_trailing_slash = normalize_directory_path(top_level_dir);

  AssetLibPtr *lib_uptr_ptr = on_disk_libs_.lookup_ptr(top_dir_trailing_slash);

  if (lib_uptr_ptr != nullptr) {
    CLOG_INFO(&LOG, 2, "get \"%s\" (cached)", top_dir_trailing_slash.c_str());
    AssetLib *lib = lib_uptr_ptr->get();
    lib->refres();
    return lib;
  }

  AssetLibPtr lib_uptr = std::make_unique<AssetLib>();
  AssetLib *lib = lib_uptr.get();

  lib->on_dune_save_handler_register();
  lib->load(top_dir_trailing_slash);

  on_disk_libs_.add_new(top_dir_trailing_slash, std::move(lib_uptr));
  CLOG_INFO(&LOG, 2, "get \"%s\" (loaded)", top_dir_trailing_slash.c_str());
  return lib;
}

AssetLib *AssetLibService::get_asset_lib_current_file()
{
  if (current_file_lib_) {
    CLOG_INFO(&LOG, 2, "get current file lib (cached)");
  }
  else {
    CLOG_INFO(&LOG, 2, "get current file lib (loaded)");
    current_file_lib_ = std::make_unique<AssetLib>();
    current_file_lib_->on_dune_save_handler_register();
  }

  AssetLib *lib = current_file_lib_.get();
  return lib;
}

void AssetLibService::allocate_service_instance()
{
  instance_ = std::make_unique<AssetLibService>();
  instance_->app_handler_register();

  if (!atexit_handler_registered_) {
    /* Ensure the instance gets freed before Blender's memory leak detector runs. */
    dune_atexit_register([](void * /*user_data*/) { AssetLibService::destroy(); },
                            nullptr);
    atexit_handler_registered_ = true;
  }
}

static void on_dunefile_load(struct Main * /*Main*/,
                              struct ApiPtr ** /*ptrs*/,
                              const int /*num_ptrs*/,
                              void * /*arg*/)
{
  AssetLibService::destroy();
}

void AssetLibService::app_handler_register()
{
  /* The callback system doesn't own `on_load_cb_store_`. */
  on_load_cb_store_.alloc = false;

  on_load_cb_store_.fn = &on_dunefile_load;
  on_load_cb_store_.arg = this;

  dune_cb_add(&on_load_cb_store_, DUNE_CB_EVT_LOAD_PRE);
}

void AssetLibService::app_handler_unregister()
{
  dune_cb_remove(&on_load_cb_store_, DUNE_CB_EVT_LOAD_PRE);
  on_load_cb_store_.fn = nullptr;
  on_load_cb_store_.arg = nullptr;
}

bool AssetLibService::has_any_unsaved_catalogs() const
{
  if (current_file_lib_ && current_file_lib_->catalog_service->has_unsaved_changes()) {
    return true;
  }

  for (const auto &asset_lib_uptr : on_disk_libs_.values()) {
    if (asset_lib_uptr->catalog_service->has_unsaved_changes()) {
      return true;
    }
  }

  return false;
}
