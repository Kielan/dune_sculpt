/** \file
 * \ingroup edasset
 *
 * Helpers to convert asset library references from and to enum values and RNA enums.
 * In some cases it's simply not possible to reference an asset library with
 * #AssetLibraryReferences. This API guarantees a safe translation to indices/enum values for as
 * long as there is no change in the order of registered custom asset libraries.
 */

#include "lib_listbase.h"

#include "dune_pref.h"

#include "typedef_user.h"

#include "UI_resources.h"

#include "api_define.h"

#include "ED_asset_lib.h"

int ED_asset_libref_to_enum_value(const AssetLibRef *lib)
{
  /* Simple case: Predefined repository, just set the value. */
  if (lib->type < ASSET_LIBRARY_CUSTOM) {
    return lib->type;
  }

  /* Note that the path isn't checked for validity here. If an invalid library path is used, the
   * Asset Browser can give a nice hint on what's wrong. */
  const dUserAssetLib *user_lib = dune_prefs_asset_lib_find_from_index(
      &U, lib->custom_lib_index);
  if (user_lib) {
    return ASSET_LIBRARY_CUSTOM + lib->custom_lib_index;
  }

  return ASSET_LIBRARY_LOCAL;
}

AssetLibRef ED_asset_libref_from_enum_value(int value)
{
  AssetLibRef lib;

  /* Simple case: Predefined repository, just set the value. */
  if (value < ASSET_LIBRARY_CUSTOM) {
    lib.type = value;
    lib.custom_lib_index = -1;
    LIB_assert(ELEM(value, ASSET_LIBRARY_LOCAL));
    return lib;
  }

  const dUserAssetLib *user_lib = dune_prefs_asset_lib_find_from_index(
      &U, value - ASSET_LIBRARY_CUSTOM);

  /* Note that there is no check if the path exists here. If an invalid library path is used, the
   * Asset Browser can give a nice hint on what's wrong. */
  if (!user_lib) {
    lib.type = ASSET_LIBRARY_LOCAL;
    lib.custom_lib_index = -1;
  }
  else {
    const bool is_valid = (user_lib->name[0] && user_lib->path[0]);
    if (is_valid) {
      lib.custom_lib_index = value - ASSET_LIBRARY_CUSTOM;
      lib.type = ASSET_LIBRARY_CUSTOM;
    }
  }
  return lib;
}

const EnumPropItem *ED_asset_libref_to_api_enum_itemf(
    const bool include_local_lib)
{
  EnumPropItem *item = nullptr;
  int totitem = 0;

  if (include_local_lib) {
    const EnumPropItem predefined_items[] = {
        /* For the future. */
        // {ASSET_REPO_BUNDLED, "BUNDLED", 0, "Bundled", "Show the default user assets"},
        {ASSET_LIBRARY_LOCAL,
         "LOCAL",
         ICON_CURRENT_FILE,
         "Current File",
         "Show the assets currently available in this Dune session"},
        {0, nullptr, 0, nullptr, nullptr},
    };

    /* Add predefined items. */
    api_enum_items_add(&item, &totitem, predefined_items);
  }

  /* Add separator if needed. */
  if (!lib_listbase_is_empty(&U.asset_libs)) {
    types_enum_item_add_separator(&item, &totitem);
  }

  int i = 0;
  for (dUserAssetLib *user_lib = (dUserAssetLib *)U.asset_lib.first;
       user_lib;
       user_lib = user_lib->next, i++) {
    /* Note that the path itself isn't checked for validity here. If an invalid library path is
     * used, the Asset Browser can give a nice hint on what's wrong. */
    const bool is_valid = (user_lib->name[0] && user_lib->path[0]);
    if (!is_valid) {
      continue;
    }

    AssetLibRef libref;
    libref.type = ASSET_LIBRARY_CUSTOM;
    libref.custom_lib_index = i;

    const int enum_value = ED_asset_libref_to_enum_value(&libref);
    /* Use library path as description, it's a nice hint for users. */
    EnumPropItem tmp = {
        enum_value, user_lib->name, ICON_NONE, user_lib->name, user_lib->path};
    api_enum_item_add(&item, &totitem, &tmp);
  }

  types_enum_item_end(&item, &totitem);
  return item;
}
