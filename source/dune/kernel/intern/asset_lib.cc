#include <memory>

#include "dune_asset_lib.hh"
#include "dune_main.h"
#include "dune_preferences.h"

#include "lib_path_util.h"

#include "types_asset.h"
#include "types_userdef.h"

#include "asset_lib_service.hh"

bool dune::AssetLib::save_catalogs_when_file_is_saved = true;

/* Loading an asset lib at this point only means loading the catalogs. Later on this should
 * invoke reading of asset representations too. */
struct AssetLib *dune_asset_lib_load(const char *lib_path)
{
  dune::AssetLibService *service = dune::AssetLibService::get();
  dune::AssetLib *lib;
  if (lib_path == nullptr || lib_path[0] == '\0') {
    lib = service->get_asset_lib_current_file();
  }
  else {
    lib = service->get_asset_lib_on_disk(lib_path);
  }
  return reinterpret_cast<struct AssetLib *>(lib);
}

bool dune_asset_lib_has_any_unsaved_catalogs()
{
  dune::AssetLibService *service = dune::AssetLibService::get();
  return service->has_any_unsaved_catalogs();
}

bool dune_asset_lib_find_suitable_root_path_from_path(const char *input_path,
                                                         char *r_lib_path)
{
  if (UserAssetLib *prefs_lib = dune_prefs_asset_lib_containing_path(
          &U, input_path)) {
    lib_strncpy(r_lib_path, prefs_lib->path, FILE_MAXDIR);
    return true;
  }

  lib_split_dir_part(input_path, r_lib_path, FILE_MAXDIR);
  return r_lib_path[0] != '\0';
}

bool dune_asset_lib_find_suitable_root_path_from_main(const Main *main, char *r_lib_path)
{
  return dune_asset_lib_find_suitable_root_path_from_path(main->filepath, r_lib_path);
}

dune::AssetCatalogService *dune_asset_lib_get_catalog_service(
    const ::AssetLib *lib_c)
{
  if (lib_c == nullptr) {
    return nullptr;
  }

  const dune::AssetLib &lib = reinterpret_cast<const dune::AssetLib &>(
      *lib_c);
  return lib.catalog_service.get();
}

dune::AssetCatalogTree *dune_asset_lib_get_catalog_tree(const ::AssetLib *lib)
{
  dune::AssetCatalogService *catalog_service = dune_asset_lib_get_catalog_service(
      lib);
  if (catalog_service == nullptr) {
    return nullptr;
  }

  return catalog_service->get_catalog_tree();
}

void dune_asset_lib_refresh_catalog_simplename(struct AssetLib *asset_lib,
                                               struct AssetMetaData *asset_data)
{
  dune::AssetLib *lib = reinterpret_cast<dune::AssetLib *>(asset_lib);
  lib->refresh_catalog_simplename(asset_data);
}

namespace dune {

AssetLib::AssetLib() : catalog_service(std::make_unique<AssetCatalogService>())
{
}

AssetLib::~AssetLib()
{
  if (on_save_cb_store_.fn) {
    on_dune_save_handler_unregister();
  }
}

void AssetLib::load(StringRefNull lib_root_directory)
{
  auto catalog_service = std::make_unique<AssetCatalogService>(lib_root_directory);
  catalog_service->load_from_disk();
  this->catalog_service = std::move(catalog_service);
}

void AssetLib::refresh()
{
  this->catalog_service->reload_catalogs();
}

namespace {
void asset_lib_on_save_post(struct Main *main,
                            struct ApiPtr **ptrs,
                            const int num_ptrs,
                            void *arg)
{
  AssetLib *asset_lib = static_cast<AssetLib *>(arg);
  asset_lib->on_dune_save_post(main, ptrs, num_ptrs);
}

}  // namespace

void AssetLib::on_dune_save_handler_register()
{
  /* The callback system doesn't own `on_save_callback_store_`. */
  on_save_cb_store_.alloc = false;

  on_save_cb_store_.fn = asset_lib_on_save_post;
  on_save_cb_store_.arg = this;

  dune_cb_add(&on_save_cb_store_, DUNE_CB_EVT_SAVE_POST);
}

void AssetLib::on_dune_save_handler_unregister()
{
  dune_cb_remove(&on_save_cb_store_, DUNE_CB_EVT_SAVE_POST);
  on_save_cb_store_.fn = nullptr;
  on_save_cb_store_.arg = nullptr;
}

void AssetLib::on_dune_save_post(struct Main *main,
                                 struct ApiPtr ** /*ptrs*/,
                                 const int /*num_ptrs*/)
{
  if (this->catalog_service == nullptr) {
    return;
  }

  if (save_catalogs_when_file_is_saved) {
    this->catalog_service->write_to_disk(main->filepath);
  }
}

void AssetLib::refresh_catalog_simplename(struct AssetMetaData *asset_data)
{
  if (lib_uuid_is_nil(asset_data->catalog_id)) {
    asset_data->catalog_simple_name[0] = '\0';
    return;
  }
  const AssetCatalog *catalog = this->catalog_service->find_catalog(asset_data->catalog_id);
  if (catalog == nullptr) {
    /* No-op if the catalog cannot be found. This could be the kind of "the catalog definition file
     * is corrupt/lost" scenario that the simple name is meant to help recover from. */
    return;
  }
  STRNCPY(asset_data->catalog_simple_name, catalog->simple_name.c_str());
}
