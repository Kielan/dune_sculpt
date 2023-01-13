#include <fstream>
#include <set>

#include "BKE_asset_catalog.hh"
#include "BKE_asset_library.h"

#include "BLI_fileops.hh"
#include "BLI_path_util.h"

/* For S_ISREG() and S_ISDIR() on Windows. */
#ifdef WIN32
#  include "BLI_winstuff.h"
#endif

#include "CLG_log.h"

static CLG_LogRef LOG = {"bke.asset_service"};

namespace blender::bke {

const CatalogFilePath AssetCatalogService::DEFAULT_CATALOG_FILENAME = "blender_assets.cats.txt";

const int AssetCatalogDefinitionFile::SUPPORTED_VERSION = 1;
const std::string AssetCatalogDefinitionFile::VERSION_MARKER = "VERSION ";

const std::string AssetCatalogDefinitionFile::HEADER =
    "# This is an Asset Catalog Definition file for Blender.\n"
    "#\n"
    "# Empty lines and lines starting with `#` will be ignored.\n"
    "# The first non-ignored line should be the version indicator.\n"
    "# Other lines are of the format \"UUID:catalog/path/for/assets:simple catalog name\"\n";

AssetCatalogService::AssetCatalogService()
    : catalog_collection_(std::make_unique<AssetCatalogCollection>())
{
}

AssetCatalogService::AssetCatalogService(const CatalogFilePath &asset_library_root)
    : catalog_collection_(std::make_unique<AssetCatalogCollection>()),
      asset_library_root_(asset_library_root)
{
}

void AssetCatalogService::tag_has_unsaved_changes(AssetCatalog *edited_catalog)
{
  if (edited_catalog) {
    edited_catalog->flags.has_unsaved_changes = true;
  }
  BLI_assert(catalog_collection_);
  catalog_collection_->has_unsaved_changes_ = true;
}

void AssetCatalogService::untag_has_unsaved_changes()
{
  BLI_assert(catalog_collection_);
  catalog_collection_->has_unsaved_changes_ = false;

  /* TODO(Sybren): refactor; this is more like "post-write cleanup" than "remove a tag" code. */

  /* Forget about any deleted catalogs. */
  if (catalog_collection_->catalog_definition_file_) {
    for (CatalogID catalog_id : catalog_collection_->deleted_catalogs_.keys()) {
      catalog_collection_->catalog_definition_file_->forget(catalog_id);
    }
  }
  catalog_collection_->deleted_catalogs_.clear();

  /* Mark all remaining catalogs as "without unsaved changes". */
  for (auto &catalog_uptr : catalog_collection_->catalogs_.values()) {
    catalog_uptr->flags.has_unsaved_changes = false;
  }
}

bool AssetCatalogService::has_unsaved_changes() const
{
  BLI_assert(catalog_collection_);
  return catalog_collection_->has_unsaved_changes_;
}

void AssetCatalogService::tag_all_catalogs_as_unsaved_changes()
{
  for (auto &catalog : catalog_collection_->catalogs_.values()) {
    catalog->flags.has_unsaved_changes = true;
  }
  catalog_collection_->has_unsaved_changes_ = true;
}

bool AssetCatalogService::is_empty() const
{
  BLI_assert(catalog_collection_);
  return catalog_collection_->catalogs_.is_empty();
}

OwningAssetCatalogMap &AssetCatalogService::get_catalogs()
{
  return catalog_collection_->catalogs_;
}
OwningAssetCatalogMap &AssetCatalogService::get_deleted_catalogs()
{
  return catalog_collection_->deleted_catalogs_;
}

AssetCatalogDefinitionFile *AssetCatalogService::get_catalog_definition_file()
{
  return catalog_collection_->catalog_definition_file_.get();
}

AssetCatalog *AssetCatalogService::find_catalog(CatalogID catalog_id) const
{
  const std::unique_ptr<AssetCatalog> *catalog_uptr_ptr =
      catalog_collection_->catalogs_.lookup_ptr(catalog_id);
  if (catalog_uptr_ptr == nullptr) {
    return nullptr;
  }
  return catalog_uptr_ptr->get();
}

AssetCatalog *AssetCatalogService::find_catalog_by_path(const AssetCatalogPath &path) const
{
  /* Use an AssetCatalogOrderedSet to find the 'best' catalog for this path. This will be the first
   * one loaded from disk, or if that does not exist the one with the lowest UUID. This ensures
   * stable, predictable results. */
  MutableAssetCatalogOrderedSet ordered_catalogs;

  for (const auto &catalog : catalog_collection_->catalogs_.values()) {
    if (catalog->path == path) {
      ordered_catalogs.insert(catalog.get());
    }
  }

  if (ordered_catalogs.empty()) {
    return nullptr;
  }

  MutableAssetCatalogOrderedSet::iterator best_choice_it = ordered_catalogs.begin();
  return *best_choice_it;
}

bool AssetCatalogService::is_catalog_known(CatalogID catalog_id) const
{
  BLI_assert(catalog_collection_);
  return catalog_collection_->catalogs_.contains(catalog_id);
}

AssetCatalogFilter AssetCatalogService::create_catalog_filter(
    const CatalogID active_catalog_id) const
{
  Set<CatalogID> matching_catalog_ids;
  Set<CatalogID> known_catalog_ids;
  matching_catalog_ids.add(active_catalog_id);

  const AssetCatalog *active_catalog = find_catalog(active_catalog_id);

  /* This cannot just iterate over tree items to get all the required data, because tree items only
   * represent single UUIDs. It could be used to get the main UUIDs of the children, though, and
   * then only do an exact match on the path (instead of the more complex `is_contained_in()`
   * call). Without an extra indexed-by-path acceleration structure, this is still going to require
   * a linear search, though. */
  for (const auto &catalog_uptr : catalog_collection_->catalogs_.values()) {
    if (active_catalog && catalog_uptr->path.is_contained_in(active_catalog->path)) {
      matching_catalog_ids.add(catalog_uptr->catalog_id);
    }
    known_catalog_ids.add(catalog_uptr->catalog_id);
  }

  return AssetCatalogFilter(std::move(matching_catalog_ids), std::move(known_catalog_ids));
}

void AssetCatalogService::delete_catalog_by_id_soft(const CatalogID catalog_id)
{
  std::unique_ptr<AssetCatalog> *catalog_uptr_ptr = catalog_collection_->catalogs_.lookup_ptr(
      catalog_id);
  if (catalog_uptr_ptr == nullptr) {
    /* Catalog cannot be found, which is fine. */
    return;
  }

  /* Mark the catalog as deleted. */
  AssetCatalog *catalog = catalog_uptr_ptr->get();
  catalog->flags.is_deleted = true;

  /* Move ownership from catalog_collection_->catalogs_ to catalog_collection_->deleted_catalogs_.
   */
  catalog_collection_->deleted_catalogs_.add(catalog_id, std::move(*catalog_uptr_ptr));

  /* The catalog can now be removed from the map without freeing the actual AssetCatalog. */
  catalog_collection_->catalogs_.remove(catalog_id);
}

void AssetCatalogService::delete_catalog_by_id_hard(CatalogID catalog_id)
{
  catalog_collection_->catalogs_.remove(catalog_id);
  catalog_collection_->deleted_catalogs_.remove(catalog_id);

  /* TODO(@sybren): adjust this when supporting multiple CDFs. */
  catalog_collection_->catalog_definition_file_->forget(catalog_id);
}

void AssetCatalogService::prune_catalogs_by_path(const AssetCatalogPath &path)
{
  /* Build a collection of catalog IDs to delete. */
  Set<CatalogID> catalogs_to_delete;
  for (const auto &catalog_uptr : catalog_collection_->catalogs_.values()) {
    const AssetCatalog *cat = catalog_uptr.get();
    if (cat->path.is_contained_in(path)) {
      catalogs_to_delete.add(cat->catalog_id);
    }
  }

  /* Delete the catalogs. */
  for (const CatalogID cat_id : catalogs_to_delete) {
    this->delete_catalog_by_id_soft(cat_id);
  }

  this->rebuild_tree();
}

void AssetCatalogService::prune_catalogs_by_id(const CatalogID catalog_id)
{
  const AssetCatalog *catalog = find_catalog(catalog_id);
  BLI_assert_msg(catalog, "trying to prune asset catalogs by the path of a non-existent catalog");
  if (!catalog) {
    return;
  }
  this->prune_catalogs_by_path(catalog->path);
}

void AssetCatalogService::update_catalog_path(const CatalogID catalog_id,
                                              const AssetCatalogPath &new_catalog_path)
{
  AssetCatalog *renamed_cat = this->find_catalog(catalog_id);
  const AssetCatalogPath old_cat_path = renamed_cat->path;

  for (auto &catalog_uptr : catalog_collection_->catalogs_.values()) {
    AssetCatalog *cat = catalog_uptr.get();

    const AssetCatalogPath new_path = cat->path.rebase(old_cat_path, new_catalog_path);
    if (!new_path) {
      continue;
    }
    cat->path = new_path;
    cat->simple_name_refresh();

    /* TODO(Sybren): go over all assets that are assigned to this catalog, defined in the current
     * blend file, and update the catalog simple name stored there. */
  }

  this->rebuild_tree();
}

AssetCatalog *AssetCatalogService::create_catalog(const AssetCatalogPath &catalog_path)
{
  std::unique_ptr<AssetCatalog> catalog = AssetCatalog::from_path(catalog_path);
  catalog->flags.has_unsaved_changes = true;

  /* So we can std::move(catalog) and still use the non-owning pointer: */
  AssetCatalog *const catalog_ptr = catalog.get();

  /* TODO(@sybren): move the `AssetCatalog::from_path()` function to another place, that can reuse
   * catalogs when a catalog with the given path is already known, and avoid duplicate catalog IDs.
   */
  BLI_assert_msg(!catalog_collection_->catalogs_.contains(catalog->catalog_id),
                 "duplicate catalog ID not supported");
  catalog_collection_->catalogs_.add_new(catalog->catalog_id, std::move(catalog));

  if (catalog_collection_->catalog_definition_file_) {
    /* Ensure the new catalog gets written to disk at some point. If there is no CDF in memory yet,
     * it's enough to have the catalog known to the service as it'll be saved to a new file. */
    catalog_collection_->catalog_definition_file_->add_new(catalog_ptr);
  }

  BLI_assert_msg(catalog_tree_, "An Asset Catalog tree should always exist.");
  catalog_tree_->insert_item(*catalog_ptr);

  return catalog_ptr;
}

static std::string asset_definition_default_file_path_from_dir(StringRef asset_library_root)
{
  char file_path[PATH_MAX];
  BLI_join_dirfile(file_path,
                   sizeof(file_path),
                   asset_library_root.data(),
                   AssetCatalogService::DEFAULT_CATALOG_FILENAME.data());
  return file_path;
}

void AssetCatalogService::load_from_disk()
{
  load_from_disk(asset_library_root_);
}

void AssetCatalogService::load_from_disk(const CatalogFilePath &file_or_directory_path)
{
  BLI_stat_t status;
  if (BLI_stat(file_or_directory_path.data(), &status) == -1) {
    /* TODO(@sybren): throw an appropriate exception. */
    CLOG_WARN(&LOG, "path not found: %s", file_or_directory_path.data());
    return;
  }

  if (S_ISREG(status.st_mode)) {
    load_single_file(file_or_directory_path);
  }
  else if (S_ISDIR(status.st_mode)) {
    load_directory_recursive(file_or_directory_path);
  }
  else {
    /* TODO(@sybren): throw an appropriate exception. */
  }

  /* TODO: Should there be a sanitize step? E.g. to remove catalogs with identical paths? */

  rebuild_tree();
}

void AssetCatalogService::load_directory_recursive(const CatalogFilePath &directory_path)
{
  /* TODO(@sybren): implement proper multi-file support. For now, just load
   * the default file if it is there. */
  CatalogFilePath file_path = asset_definition_default_file_path_from_dir(directory_path);

  if (!BLI_exists(file_path.data())) {
    /* No file to be loaded is perfectly fine. */
    CLOG_INFO(&LOG, 2, "path not found: %s", file_path.data());
    return;
  }

  this->load_single_file(file_path);
}

void AssetCatalogService::load_single_file(const CatalogFilePath &catalog_definition_file_path)
{
  /* TODO(@sybren): check that #catalog_definition_file_path is contained in #asset_library_root_,
   * otherwise some assumptions may fail. */
  std::unique_ptr<AssetCatalogDefinitionFile> cdf = parse_catalog_file(
      catalog_definition_file_path);

  BLI_assert_msg(!catalog_collection_->catalog_definition_file_,
                 "Only loading of a single catalog definition file is supported.");
  catalog_collection_->catalog_definition_file_ = std::move(cdf);
}

std::unique_ptr<AssetCatalogDefinitionFile> AssetCatalogService::parse_catalog_file(
    const CatalogFilePath &catalog_definition_file_path)
{
  auto cdf = std::make_unique<AssetCatalogDefinitionFile>();
  cdf->file_path = catalog_definition_file_path;

  /* TODO(Sybren): this might have to move to a higher level when supporting multiple CDFs. */
  Set<AssetCatalogPath> seen_paths;

  auto catalog_parsed_callback = [this, catalog_definition_file_path, &seen_paths](
                                     std::unique_ptr<AssetCatalog> catalog) {
    if (catalog_collection_->catalogs_.contains(catalog->catalog_id)) {
      /* TODO(@sybren): apparently another CDF was already loaded. This is not supported yet. */
      std::cerr << catalog_definition_file_path << ": multiple definitions of catalog "
                << catalog->catalog_id << " in multiple files, ignoring this one." << std::endl;
      /* Don't store 'catalog'; unique_ptr will free its memory. */
      return false;
    }

    catalog->flags.is_first_loaded = seen_paths.add(catalog->path);

    /* The AssetCatalog pointer is now owned by the AssetCatalogService. */
    catalog_collection_->catalogs_.add_new(catalog->catalog_id, std::move(catalog));
    return true;
  };

  cdf->parse_catalog_file(cdf->file_path, catalog_parsed_callback);

  return cdf;
}

void AssetCatalogService::reload_catalogs()
{
  /* TODO(Sybren): expand to support multiple CDFs. */
  AssetCatalogDefinitionFile *const cdf = catalog_collection_->catalog_definition_file_.get();
  if (!cdf || cdf->file_path.empty() || !BLI_is_file(cdf->file_path.c_str())) {
    return;
  }

  /* Keeps track of the catalog IDs that are seen in the CDF, so that we also know what was deleted
   * from the file on disk. */
  Set<CatalogID> cats_in_file;

  auto catalog_parsed_callback = [this, &cats_in_file](std::unique_ptr<AssetCatalog> catalog) {
    const CatalogID catalog_id = catalog->catalog_id;
    cats_in_file.add(catalog_id);

    const bool should_skip = is_catalog_known_with_unsaved_changes(catalog_id);
    if (should_skip) {
      /* Do not overwrite unsaved local changes. */
      return false;
    }

    /* This is either a new catalog, or we can just replace the in-memory one with the newly loaded
     * one. */
    catalog_collection_->catalogs_.add_overwrite(catalog_id, std::move(catalog));
    return true;
  };

  cdf->parse_catalog_file(cdf->file_path, catalog_parsed_callback);
  this->purge_catalogs_not_listed(cats_in_file);
  this->rebuild_tree();
}

void AssetCatalogService::purge_catalogs_not_listed(const Set<CatalogID> &catalogs_to_keep)
{
  Set<CatalogID> cats_to_remove;
  for (CatalogID cat_id : this->catalog_collection_->catalogs_.keys()) {
    if (catalogs_to_keep.contains(cat_id)) {
      continue;
    }
    if (is_catalog_known_with_unsaved_changes(cat_id)) {
      continue;
    }
    /* This catalog is not on disk, but also not modified, so get rid of it. */
    cats_to_remove.add(cat_id);
  }

  for (CatalogID cat_id : cats_to_remove) {
    delete_catalog_by_id_hard(cat_id);
  }
}
