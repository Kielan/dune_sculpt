#pragma once

#include <string>

#include "lib_fn_ref.hh"

struct AssetHandle;
struct AssetLibRef;
struct FileDirEntry;
struct duneContext;

std::string ED_assetlist_asset_filepath_get(const duneContext *C,
                                            const AssetLibRef &lib_ref,
                                            const AssetHandle &asset_handle);

/* Can return false to stop iterating. */
using AssetListIterFn = dune::FnRef<bool(AssetHandle)>;
void ED_assetlist_iterate(const AssetLibRef &lib_ref, AssetListIterFn fn);
