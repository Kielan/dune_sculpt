/**
 * UI/Editor level API for catalog operations, creating richer functionality than the dune catalog
 * API provides (which this uses internally).
 *
 * Note that `ED_asset_catalog.h` is part of this API.
 */

#pragma once

#include <optional>

#include "dune_asset_catalog.hh"

#include "lib_string_ref.hh"

struct AssetLibrary;
namespace dune::bke {
class AssetCatalog;
}  // namespace dune::

dune::AssetCatalog *ED_asset_catalog_add(AssetLibrary *library,
                                                 dune::StringRefNull name,
                                                 dune::StringRef parent_path = nullptr);
void ED_asset_catalog_remove(AssetLibrary *library, const dune::CatalogID &catalog_id);

void ED_asset_catalog_rename(AssetLibrary *library,
                             dune::CatalogID catalog_id,
                             dune::StringRefNull new_name);
/**
 * Reinsert catalog identified by a src_catalog_id as child to catalog identified by a
 * dst_parent_catalog_id. If a dst_parent_catalog_id is not set, the catalog is moved to the root
 * level of the tree.
 * The name of the reinserted catalog is made unique within the parent. Note that moving a catalog
 * to the same level it was before will also change its name, since the name uniqueness check isn't
 * smart enough to ignore the item to be reinserted. So the caller is expected to handle this case
 * to avoid unwanted renames.
 *
 * Nothing is done (debug builds run into an assert) if the given catalog IDs can't be identified.
 */
void ED_asset_catalog_move(
    AssetLibrary *library,
    dune::CatalogID src_catalog_id,
    std::optional<dune::CatalogID> dst_parent_catalog_id = std::nullopt);
