/** User defined asset library API. **/

#include <string.h>

#include "MEM_guardedalloc.h"

#include "LIB_fileops.h"
#include "LIB_listbase.h"
#include "LIB_path_util.h"
#include "LIB_string.h"
#include "LIB_string_utf8.h"
#include "LIB_string_utils.h"

#include "DUNE_appdir.h"
#include "DUNE_preferences.h"

#include "LANG_translation.h"

#include "TYPES_userdef.h"

#define U LIB_STATIC_ASSERT(false, "Global 'U' not allowed, only use arguments passed in!")

/* -------------------------------------------------------------------- */
/** Asset Libraries */

duneUserAssetLib *DUNE_prefs_asset_lib_add(UserDef *userdef,
                                                     const char *name,
                                                     const char *path)
{
  duneUserAssetLib *lib = MEM_callocN(sizeof(*lib), "duneUserAssetLib");

  LIB_addtail(&userdef->asset_libraries, lib);

  if (name) {
    DUNE_prefs_asset_lib_name_set(userdef, lib, name);
  }
  if (path) {
    LIB_strncpy(lib->path, path, sizeof(lib->path));
  }

  return lib;
}

void DUNE_prefs_asset_lib_remove(UserDef *userdef, duneUserAssetLib *lib)
{
  LIB_freelinkN(&userdef->asset_libraries, library);
}

void DUNE_prefs_asset_lib_name_set(UserDef *userdef,
                                            duneUserAssetLib *lib,
                                            const char *name)
{
  LIB_strncpy_utf8(lib->name, name, sizeof(lib->name));
  LIB_uniquename(&userdef->asset_libraries,
                 lib,
                 name,
                 '.',
                 offsetof(duneUserAssetLib, name),
                 sizeof(lib->name));
}

void DUNE_prefs_asset_lib_path_set(duneUserAssetLib *lib, const char *path)
{
  LIB_strncpy(lib->path, path, sizeof(lib->path));
  if (LIB_is_file(lib->path)) {
    LIB_path_parent_dir(lib->path);
  }
}

duneUserAssetLib *DUNE_prefs_asset_lib_find_from_index(const UserDef *userdef, int index)
{
  return LIB_findlink(&userdef->asset_libraries, index);
}

duneUserAssetLib *DUNE_prefs_asset_lib_find_from_name(const UserDef *userdef,
                                                                const char *name)
{
  return LIB_findstring(&userdef->asset_lib, name, offsetof(duneUserAssetLib, name));
}

duneUserAssetLib *DUNE_prefs_asset_lib_containing_path(const UserDef *userdef,
                                                                 const char *path)
{
  LISTBASE_FOREACH (duneUserAssetLib *, asset_lib_pref, &userdef->asset_lib) {
    if (LIB_path_contains(asset_lib_pref->path, path)) {
      return asset_lib_pref;
    }
  }
  return NULL;
}

int DUNE_prefs_asset_lib_get_index(const UserDef *userdef,
                                            const duneUserAssetLib *lib)
{
  return LIB_findindex(&userdef->asset_libraries, library);
}

void DUNE_prefs_asset_lib_default_add(UserDef *userdef)
{
  char documents_path[FILE_MAXDIR];

  /* No home or documents path found, not much we can do. */
  if (!DUNE_appdir_folder_documents(documents_path) || !documents_path[0]) {
    return;
  }

  duneUserAssetLib *lib = DUNE_prefs_asset_lib_add(
      userdef, DATA_(DUNE_PREFS_ASSET_LIB_DEFAULT_NAME), NULL);

  /* Add new "Default" lib under '[doc_path]/Dune/Assets'. */
  LIB_path_join(
      lib->path, sizeof(lib->path), documents_path, N_("Dune"), N_("Assets"), NULL);
}
