#pragma once

#include "types_asset.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return an index that can be used to uniquely identify a library, assuming
 * that all relevant indices were created with this function.
 */
int ED_asset_lib_ref_to_enum_value(const AssetLibRef *lib);
/**
 * Return an asset library reference matching the index returned by
 * #ED_asset_lib_ref_to_enum_value().
 */
AssetLibRef ED_asset_lib_ref_from_enum_value(int value);
/**
 * Translate all available asset libraries to an api enum, whereby the enum values match the result
 * of #ED_asset_lib_ref_to_enum_value() for any given library.
 *
 * Since this is meant for UI display, skips non-displayable libraries, that is, libraries with an
 * empty name or path.
 *
 * param include_local_lib: Whether to include the "Current File" library or not.
 */
const struct EnumPropItem *ED_asset_lib_ref_to_api_enum_itemf(
    bool include_local_lib);

#ifdef __cplusplus
}
#endif
