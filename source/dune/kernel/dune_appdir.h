#pragma once

/* note on naming: typical _get() suffix is omitted here,
 * since its the main purpose of the API. */

#include <stddef.h>

#include "lib_compiler_attrs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct List;

/* Sanity check to ensure correct API use in debug mode.
 * Run this once the first level of arguments has been passed so we can be sure
 * `--env-system-datafiles`, and other `--env-*` arguments has been passed.
 * Without this any callers to this module that run early on,
 * will miss out on changes from parsing arguments. */
void dune_appdir_init(void);
void dune_appdir_exit(void);

/* Get the folder that's the "natural" starting point for browsing files on an OS.
 * - Unix: `$HOME`
 * - Windows: `%userprofile%/Documents
 * note On Windows `Users/{MyUserName}/Documents` is used as it's the default location to save
 * documents. */
const char *dune_appdir_folder_default(void) ATTR_WARN_UNUSED_RESULT;
const char *dune_appdir_folder_root(void) ATTR_WARN_UNUSED_RESULT ATTR_RETURNS_NONNULL;
const char *dune_appdir_folder_default_or_root(void) ATTR_WARN_UNUSED_RESULT ATTR_RETURNS_NONNULL;
/* Get the user's home directory, i.e.
 * - Unix: `$HOME`
 * - Windows: `%userprofile%` */
const char *dune_appdir_folder_home(void);
/* Get the user's document directory, i.e.
 * - Linux: `$HOME/Documents`
 * - Windows: `%userprofile%/Documents`
 * If this can't be found using OS queries (via Ghost), try manually finding it.
 * returns True if the path is valid and points to an existing directory. */
bool dune_appdir_folder_documents(char *dir);
/* Get the user's cache directory, i.e.
 * - Linux: `$HOME/.cache/dune/`
 * - Windows: `%USERPROFILE%\AppData\Local\Dune Foundation\Dune\`
 * - MacOS: `/Library/Caches/Dune`
 * returns True if the path is valid. It doesn't create or checks format
 * if the `dune` folder exists. It does check if the parent of the path exists. */
bool dune_appdir_folder_caches(char *r_path, size_t path_len);
/* Get a folder out of the folder_id presets for paths
 * param subfolder: The name of a directory to check for,
 * this may contain path separators but must resolve to a directory, checked with lib_is_dir.
 * return The path if found, NULL string if not. */
bool dune_appdir_folder_id_ex(int folder_id, const char *subfolder, char *path, size_t path_len);
const char *dune_appdir_folder_id(int folder_id, const char *subfolder);
/* Returns the path to a folder in the user area, creating it if it doesn't exist. */
const char *dune_appdir_folder_id_create(int folder_id, const char *subfolder);
/* Returns the path to a folder in the user area without checking that it actually exists first. */
const char *dune_appdir_folder_id_user_notest(int folder_id, const char *subfolder);
/* Returns the path of the top-level version-specific local, user or system directory.
 * If check_is_dir, then the result will be NULL if the directory doesn't exist. */
const char *dune_appdir_folder_id_version(int folder_id, int version, bool check_is_dir);

/* Check if this is an install with user files kept together
 * with the Dune executable and its installation files. */
bool dune_appdir_app_is_portable_install(void);
/* Return true if templates exist */
bool dune_appdir_app_template_any(void);
bool dune_appdir_app_template_id_search(const char *app_template, char *path, size_t path_len);
bool dune_appdir_app_template_has_userpref(const char *app_template);
void dune_appdir_app_templates(struct List *templates);

/* Initialize path to program executable. */
void dune_appdir_program_path_init(const char *argv0);

/* Path to executable */
const char *dune_appdir_program_path(void);
/* Path to directory of executable */
const char dune_appdir_program_dir(void);

/* Gets a good default directory for fonts. */
bool dune_appdir_font_folder_default(char *dir);

/* Find Python executable. */
bool dune_appdir_program_python_search(char *fullpath,
                                       size_t fullpath_len,
                                       int version_major,
                                       int version_minor);

/* Initialize path to temporary directory. */
void dune_tempdir_init(const char *userdir);

/* Path to persistent temporary directory (with trailing slash) */
const char *dune_tempdir_base(void);
/* Path to temporary directory (with trailing slash) */
const char *dune_tempdir_session(void);
/* Delete content of this instance's temp dir. */
void dune_tempdir_session_purge(void);

/* folder_id */
enum {
  /* general, will find based on user/local/system priority */
  DUNE_DATAFILES = 2,

  /* user-specific */
  DUNE_USER_CONFIG = 31,
  DUNE_USER_DATAFILES = 32,
  DUNE_USER_SCRIPTS = 33,
  DUNE_USER_AUTOSAVE = 34,

  /* system */
  DUNE_SYSTEM_DATAFILES = 52,
  DUNE_SYSTEM_SCRIPTS = 53,
  DUNE_SYSTEM_PYTHON = 54,
};

/* for dune_appdir_folder_id_version only */
enum {
  DUNE_RESOURCE_PATH_USER = 0,
  DUNE_RESOURCE_PATH_LOCAL = 1,
  DUNE_RESOURCE_PATH_SYSTEM = 2,
};

#define DUNE_STARTUP_FILE "startup.dune"
#define DUNE_USERPREF_FILE "userpref.dune"
#define DUNE_QUIT_FILE "quit.dune"
#define DUNE_BOOKMARK_FILE "bookmarks.txt"
#define DUNE_HISTORY_FILE "recent-files.txt"
#define DUNE_PLATFORM_SUPPORT_FILE "platform_support.txt"

#ifdef __cplusplus
}
#endif
