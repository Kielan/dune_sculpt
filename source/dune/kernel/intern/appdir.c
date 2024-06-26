#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LIB_fileops.h"
#include "LIB_fileops_types.h"
#include "LIB_listbase.h"
#include "LIB_path_util.h"
#include "LIB_string.h"
#include "LIB_string_utf8.h"
#include "LIB_string_utils.h"
#include "LIB_utildefines.h"

#include "KERNEL_appdir.h" /* own include */
#include "KERNEL_dune_version.h"

#include "TRANSLATION_translation.h"

#include "GHOST_Path-api.h"

#include "MEM_guardedalloc.h"

#include "CLG_log.h"

#ifdef WIN32
#  include "utf_winfunc.h"
#  include "utfconv.h"
#  include <io.h>
#  ifdef _WIN32_IE
#    undef _WIN32_IE
#  endif
#  define _WIN32_IE 0x0501
#  include "LIB_winstuff.h"
#  include <shlobj.h>
#  include <windows.h>
#else /* non windows */
#  ifdef WITH_BINRELOC
#    include "binreloc.h"
#  endif
/* #mkdtemp on OSX (and probably all *BSD?), not worth making specific check for this OS. */
#  include <unistd.h>
#endif /* WIN32 */

static const char _str_null[] = "(null)";
#define STR_OR_FALLBACK(a) ((a) ? (a) : _str_null)

/* -------------------------------------------------------------------- */
/** Local Variables **/

/* local */
static CLG_LogRef LOG = {"bke.appdir"};

static struct {
  /** Full path to program executable. */
  char program_filename[FILE_MAX];
  /** Full path to directory in which executable is located. */
  char program_dirname[FILE_MAX];
  /** Persistent temporary directory (defined by the preferences or OS). */
  char temp_dirname_base[FILE_MAX];
  /** Volatile temporary directory (owned by Blender, removed on exit). */
  char temp_dirname_session[FILE_MAX];
} g_app = {
    .temp_dirname_session = "",
};

/* -------------------------------------------------------------------- */
/** Initialization **/

#ifndef NDEBUG
static bool is_appdir_init = false;
#  define ASSERT_IS_INIT() LIB_assert(is_appdir_init)
#else
#  define ASSERT_IS_INIT() ((void)0)
#endif

void KERNEL_appdir_init(void)
{
#ifndef NDEBUG
  LIB_assert(is_appdir_init == false);
  is_appdir_init = true;
#endif
}

void KERNEL_appdir_exit(void)
{
#ifndef NDEBUG
  LIB_assert(is_appdir_init == true);
  is_appdir_init = false;
#endif
}

/* -------------------------------------------------------------------- */
/** Internal Utilities **/

/**
 * returns a formatted representation of the specified version number. Non-re-entrant!
 */
static char *dune_version_decimal(const int version)
{
  static char version_str[5];
  LIB_assert(version < 1000);
  LIB_snprintf(version_str, sizeof(version_str), "%d.%d", version / 100, version % 100);
  return version_str;
}

/* -------------------------------------------------------------------- */
/** Default Directories **/

const char *KERNEL_appdir_folder_default(void)
{
#ifndef WIN32
  return LIB_getenv("HOME");
#else  /* Windows */
  static char documentfolder[MAXPATHLEN];

  if (KERNEL_appdir_folder_documents(documentfolder)) {
    return documentfolder;
  }

  return NULL;
#endif /* WIN32 */
}

const char *KERNEL_appdir_folder_root(void)
{
#ifndef WIN32
  return "/";
#else
  static char root[4];
  LIB_windows_get_default_root_dir(root);
  return root;
#endif
}

const char *KERNEL_appdir_folder_default_or_root(void)
{
  const char *path = KERNEL_appdir_folder_default();
  if (path == NULL) {
    path = KERNEL_appdir_folder_root();
  }
  return path;
}

const char *KERNEL_appdir_folder_home(void)
{
#ifdef WIN32
  return LIB_getenv("userprofile");
#elif defined(__APPLE__)
  return LIB_expand_tilde("~/");
#else
  return LIB_getenv("HOME");
#endif
}

bool KERNEL_appdir_folder_documents(char *dir)
{
  dir[0] = '\0';

  const char *documents_path = GHOST_getUserSpecialDir(GHOST_kUserSpecialDirDocuments);

  /* Usual case: Ghost gave us the documents path. We're done here. */
  if (documents_path && LIB_is_dir(documents_path)) {
    LIB_strncpy(dir, documents_path, FILE_MAXDIR);
    return true;
  }

  /* Ghost couldn't give us a documents path, let's try if we can find it ourselves. */

  const char *home_path = KERNEL_appdir_folder_home();
  if (!home_path || !LIB_is_dir(home_path)) {
    return false;
  }

  char try_documents_path[FILE_MAXDIR];
  /* Own attempt at getting a valid Documents path. */
  LIB_path_join(try_documents_path, sizeof(try_documents_path), home_path, N_("Documents"), NULL);
  if (!LIB_is_dir(try_documents_path)) {
    return false;
  }

  LIB_strncpy(dir, try_documents_path, FILE_MAXDIR);
  return true;
}

bool KERNEL_appdir_folder_caches(char *r_path, const size_t path_len)
{
  r_path[0] = '\0';

  const char *caches_root_path = GHOST_getUserSpecialDir(GHOST_kUserSpecialDirCaches);
  if (caches_root_path == NULL || !BLI_is_dir(caches_root_path)) {
    caches_root_path = KERNEL_tempdir_base();
  }
  if (caches_root_path == NULL || !BLI_is_dir(caches_root_path)) {
    return false;
  }

#ifdef WIN32
  LIB_path_join(
      r_path, path_len, caches_root_path, "Dune Foundation", "Dune", "Cache", SEP_STR, NULL);
#elif defined(__APPLE__)
  LIB_path_join(r_path, path_len, caches_root_path, "Dune", SEP_STR, NULL);
#else /* __linux__ */
  LIB_path_join(r_path, path_len, caches_root_path, "dune", SEP_STR, NULL);
#endif

  return true;
}

bool KERNEL_appdir_font_folder_default(char *dir)
{
  char test_dir[FILE_MAXDIR];
  test_dir[0] = '\0';

#ifdef WIN32
  wchar_t wpath[FILE_MAXDIR];
  if (SHGetSpecialFolderPathW(0, wpath, CSIDL_FONTS, 0)) {
    wcscat(wpath, L"\\");
    LIB_strncpy_wchar_as_utf8(test_dir, wpath, sizeof(test_dir));
  }
#elif defined(__APPLE__)
  STRNCPY(test_dir, LIB_expand_tilde("~/Library/Fonts/"));
  LIB_path_slash_ensure(test_dir);
#else
  STRNCPY(test_dir, "/usr/share/fonts");
#endif

  if (test_dir[0] && LIB_exists(test_dir)) {
    LIB_strncpy(dir, test_dir, FILE_MAXDIR);
    return true;
  }
  return false;
}

/* -------------------------------------------------------------------- */
/** Path Presets (Internal Helpers) **/

/**
 * Concatenates paths into targetpath,
 * returning true if result points to a directory.
 *
 * param path_base: Path base, never NULL.
 * param folder_name: First sub-directory (optional).
 * param subfolder_name: Second sub-directory (optional).
 * param check_is_dir: When false, return true even if the path doesn't exist.
 *
 * note The names for optional paths only follow other usage in this file,
 * the names don't matter for this function.
 *
 * note If it's useful we could take an arbitrary number of paths.
 * For now usage is limited and we don't need this.
 */
static bool test_path(char *targetpath,
                      size_t targetpath_len,
                      const bool check_is_dir,
                      const char *path_base,
                      const char *folder_name,
                      const char *subfolder_name)
{
  ASSERT_IS_INIT();

  /* Only the last argument should be NULL. */
  LIB_assert(!(folder_name == NULL && (subfolder_name != NULL)));
  LIB_path_join(targetpath, targetpath_len, path_base, folder_name, subfolder_name, NULL);
  if (check_is_dir == false) {
    CLOG_INFO(&LOG, 3, "using without test: '%s'", targetpath);
    return true;
  }

  if (LIB_is_dir(targetpath)) {
    CLOG_INFO(&LOG, 3, "found '%s'", targetpath);
    return true;
  }

  CLOG_INFO(&LOG, 3, "missing '%s'", targetpath);

  /* Path not found, don't accidentally use it,
   * otherwise call this function with `check_is_dir` set to false. */
  targetpath[0] = '\0';
  return false;
}

/**
 * Puts the value of the specified environment variable into \a path if it exists.
 *
 * param check_is_dir: When true, checks if it points at a directory.
 *
 * returns true when the value of the environment variable is stored
 * at the address path points to.
 */
static bool test_env_path(char *path, const char *envvar, const bool check_is_dir)
{
  ASSERT_IS_INIT();

  const char *env_path = envvar ? LIB_getenv(envvar) : NULL;
  if (!env_path) {
    return false;
  }

  LIB_strncpy(path, env_path, FILE_MAX);

  if (check_is_dir == false) {
    CLOG_INFO(&LOG, 3, "using env '%s' without test: '%s'", envvar, env_path);
    return true;
  }

  if (LIB_is_dir(env_path)) {
    CLOG_INFO(&LOG, 3, "env '%s' found: %s", envvar, env_path);
    return true;
  }

  CLOG_INFO(&LOG, 3, "env '%s' missing: %s", envvar, env_path);

  /* Path not found, don't accidentally use it,
   * otherwise call this function with `check_is_dir` set to false. */
  path[0] = '\0';
  return false;
}

/**
 * Constructs in a targetpath the name of a directory relative to a version-specific
 * sub-directory in the parent directory of the Dune executable.
 *
 * param targetpath: String to return path.
 * param folder_name: Optional folder name within version-specific directory.
 * param subfolder_name: Optional sub-folder name within folder_name.
 *
 * param version: To construct name of version-specific directory within #g_app.program_dirname.
 * param check_is_dir: When false, return true even if the path doesn't exist.
 * return true if such a directory exists.
 */
static bool get_path_local_ex(char *targetpath,
                              size_t targetpath_len,
                              const char *folder_name,
                              const char *subfolder_name,
                              const int version,
                              const bool check_is_dir)
{
  char relfolder[FILE_MAX];

  CLOG_INFO(&LOG,
            3,
            "folder='%s', subfolder='%s'",
            STR_OR_FALLBACK(folder_name),
            STR_OR_FALLBACK(subfolder_name));

  if (folder_name) { /* `subfolder_name` may be NULL. */
    LIB_path_join(relfolder, sizeof(relfolder), folder_name, subfolder_name, NULL);
  }
  else {
    relfolder[0] = '\0';
  }

  /* Try `{g_app.program_dirname}/2.xx/{folder_name}` the default directory
   * for a portable distribution. See `WITH_INSTALL_PORTABLE` build-option. */
  const char *path_base = g_app.program_dirname;
#ifdef __APPLE__
  /* Due new code-sign situation in OSX > 10.9.5
   * we must move the dune_version dir with contents to Resources. */
  char osx_resourses[FILE_MAX];
  LIB_snprintf(osx_resourses, sizeof(osx_resourses), "%s../Resources", g_app.program_dirname);
  /* Remove the '/../' added above. */
  LIB_path_normalize(NULL, osx_resourses);
  path_base = osx_resourses;
#endif
  return test_path(targetpath,
                   targetpath_len,
                   check_is_dir,
                   path_base,
                   dune_version_decimal(version),
                   relfolder);
}
static bool get_path_local(char *targetpath,
                           size_t targetpath_len,
                           const char *folder_name,
                           const char *subfolder_name)
{
  const int version = DUNE_VERSION;
  const bool check_is_dir = true;
  return get_path_local_ex(
      targetpath, targetpath_len, folder_name, subfolder_name, version, check_is_dir);
}

bool KERNEL_appdir_app_is_portable_install(void)
{
  /* Detect portable install by the existence of `config` folder. */
  char path[FILE_MAX];
  return get_path_local(path, sizeof(path), "config", NULL);
}

/**
 * Returns the path of a folder from environment variables.
 *
 * param targetpath: String to return path.
 * param subfolder_name: optional name of sub-folder within folder.
 * param envvar: name of environment variable to check folder_name.
 * param check_is_dir: When false, return true even if the path doesn't exist.
 * return true if it was able to construct such a path and the path exists.
 */
static bool get_path_environment_ex(char *targetpath,
                                    size_t targetpath_len,
                                    const char *subfolder_name,
                                    const char *envvar,
                                    const bool check_is_dir)
{
  char user_path[FILE_MAX];

  if (test_env_path(user_path, envvar, check_is_dir)) {
    /* Note that `subfolder_name` may be NULL, in this case we use `user_path` as-is. */
    return test_path(targetpath, targetpath_len, check_is_dir, user_path, subfolder_name, NULL);
  }
  return false;
}
static bool get_path_environment(char *targetpath,
                                 size_t targetpath_len,
                                 const char *subfolder_name,
                                 const char *envvar)
{
  const bool check_is_dir = true;
  return get_path_environment_ex(targetpath, targetpath_len, subfolder_name, envvar, check_is_dir);
}

/**
 * Returns the path of a folder within the user-files area.
 *
 * param targetpath: String to return path.
 * param folder_name: default name of folder within user area.
 * param subfolder_name: optional name of sub-folder within folder.
 * param version: Dune version, used to construct a sub-directory name.
 * param check_is_dir: When false, return true even if the path doesn't exist.
 * return true if it was able to construct such a path.
 */
static bool get_path_user_ex(char *targetpath,
                             size_t targetpath_len,
                             const char *folder_name,
                             const char *subfolder_name,
                             const int version,
                             const bool check_is_dir)
{
  char user_path[FILE_MAX];
  const char *user_base_path;

  /* for portable install, user path is always local */
  if (KERNEL_appdir_app_is_portable_install()) {
    return get_path_local_ex(
        targetpath, targetpath_len, folder_name, subfolder_name, version, check_is_dir);
  }
  user_path[0] = '\0';

  user_base_path = GHOST_getUserDir(version, dune_version_decimal(version));
  if (user_base_path) {
    LIB_strncpy(user_path, user_base_path, FILE_MAX);
  }

  if (!user_path[0]) {
    return false;
  }

  CLOG_INFO(&LOG,
            3,
            "'%s', folder='%s', subfolder='%s'",
            user_path,
            STR_OR_FALLBACK(folder_name),
            STR_OR_FALLBACK(subfolder_name));

  /* `subfolder_name` may be NULL. */
  return test_path(
      targetpath, targetpath_len, check_is_dir, user_path, folder_name, subfolder_name);
}
static bool get_path_user(char *targetpath,
                          size_t targetpath_len,
                          const char *folder_name,
                          const char *subfolder_name)
{
  const int version = DUNE_VERSION;
  const bool check_is_dir = true;
  return get_path_user_ex(
      targetpath, targetpath_len, folder_name, subfolder_name, version, check_is_dir);
}

/**
 * Returns the path of a folder within the Duneinstallation directory.
 *
 * param targetpath: String to return path.
 * param folder_name: default name of folder within installation area.
 * param subfolder_name: optional name of sub-folder within folder.
 * param version: Dune version, used to construct a sub-directory name.
 * param check_is_dir: When false, return true even if the path doesn't exist.
 * return true if it was able to construct such a path.
 */
static bool get_path_system_ex(char *targetpath,
                               size_t targetpath_len,
                               const char *folder_name,
                               const char *subfolder_name,
                               const int version,
                               const bool check_is_dir)
{
  char system_path[FILE_MAX];
  const char *system_base_path;
  char relfolder[FILE_MAX];

  if (folder_name) { /* `subfolder_name` may be NULL. */
    LIB_path_join(relfolder, sizeof(relfolder), folder_name, subfolder_name, NULL);
  }
  else {
    relfolder[0] = '\0';
  }

  system_path[0] = '\0';
  system_base_path = GHOST_getSystemDir(version, dune_version_decimal(version));
  if (system_base_path) {
    LIB_strncpy(system_path, system_base_path, FILE_MAX);
  }

  if (!system_path[0]) {
    return false;
  }

  CLOG_INFO(&LOG,
            3,
            "'%s', folder='%s', subfolder='%s'",
            system_path,
            STR_OR_FALLBACK(folder_name),
            STR_OR_FALLBACK(subfolder_name));

  /* Try `$DUNEPATH/folder_name/subfolder_name`, `subfolder_name` may be NULL. */
  return test_path(
      targetpath, targetpath_len, check_is_dir, system_path, folder_name, subfolder_name);
}

static bool get_path_system(char *targetpath,
                            size_t targetpath_len,
                            const char *folder_name,
                            const char *subfolder_name)
{
  const int version = DUNE_VERSION;
  const bool check_is_dir = true;
  return get_path_system_ex(
      targetpath, targetpath_len, folder_name, subfolder_name, version, check_is_dir);
}

/* -------------------------------------------------------------------- */
/** Path Presets API **/

bool KERNEL_appdir_folder_id_ex(const int folder_id,
                             const char *subfolder,
                             char *path,
                             size_t path_len)
{
  switch (folder_id) {
    case DUNE_DATAFILES: /* general case */
      if (get_path_environment(path, path_len, subfolder, "DUNE_USER_DATAFILES")) {
        break;
      }
      if (get_path_user(path, path_len, "datafiles", subfolder)) {
        break;
      }
      if (get_path_environment(path, path_len, subfolder, "DUNE_SYSTEM_DATAFILES")) {
        break;
      }
      if (get_path_local(path, path_len, "datafiles", subfolder)) {
        break;
      }
      if (get_path_system(path, path_len, "datafiles", subfolder)) {
        break;
      }
      return false;

    case DUNE_USER_DATAFILES:
      if (get_path_environment(path, path_len, subfolder, "DUNE_USER_DATAFILES")) {
        break;
      }
      if (get_path_user(path, path_len, "datafiles", subfolder)) {
        break;
      }
      return false;

    case DUNE_SYSTEM_DATAFILES:
      if (get_path_environment(path, path_len, subfolder, "DUNE_SYSTEM_DATAFILES")) {
        break;
      }
      if (get_path_system(path, path_len, "datafiles", subfolder)) {
        break;
      }
      if (get_path_local(path, path_len, "datafiles", subfolder)) {
        break;
      }
      return false;

    case DUNE_USER_AUTOSAVE:
      if (get_path_environment(path, path_len, subfolder, "DUNE_USER_DATAFILES")) {
        break;
      }
      if (get_path_user(path, path_len, "autosave", subfolder)) {
        break;
      }
      return false;

    case DUNE_USER_CONFIG:
      if (get_path_environment(path, path_len, subfolder, "DUNE_USER_CONFIG")) {
        break;
      }
      if (get_path_user(path, path_len, "config", subfolder)) {
        break;
      }
      return false;

    case DUNE_USER_SCRIPTS:
      if (get_path_environment(path, path_len, subfolder, "BLENDER_USER_SCRIPTS")) {
        break;
      }
      if (get_path_user(path, path_len, "scripts", subfolder)) {
        break;
      }
      return false;

    case DUNE_SYSTEM_SCRIPTS:
      if (get_path_environment(path, path_len, subfolder, "DUNE_SYSTEM_SCRIPTS")) {
        break;
      }
      if (get_path_system(path, path_len, "scripts", subfolder)) {
        break;
      }
      if (get_path_local(path, path_len, "scripts", subfolder)) {
        break;
      }
      return false;

    case DUNE_SYSTEM_PYTHON:
      if (get_path_environment(path, path_len, subfolder, "DUNE_SYSTEM_PYTHON")) {
        break;
      }
      if (get_path_system(path, path_len, "python", subfolder)) {
        break;
      }
      if (get_path_local(path, path_len, "python", subfolder)) {
        break;
      }
      return false;

    default:
      LIB_assert_unreachable();
      break;
  }

  return true;
}

const char *KERNEL_appdir_folder_id(const int folder_id, const char *subfolder)
{
  static char path[FILE_MAX] = "";
  if (KERNEL_appdir_folder_id_ex(folder_id, subfolder, path, sizeof(path))) {
    return path;
  }
  return NULL;
}

const char *KERNEL_appdir_folder_id_user_notest(const int folder_id, const char *subfolder)
{
  const int version = DUNE_VERSION;
  static char path[FILE_MAX] = "";
  const bool check_is_dir = false;

  switch (folder_id) {
    case DUNE_USER_DATAFILES:
      if (get_path_environment_ex(
              path, sizeof(path), subfolder, "BLENDER_USER_DATAFILES", check_is_dir)) {
        break;
      }
      get_path_user_ex(path, sizeof(path), "datafiles", subfolder, version, check_is_dir);
      break;
    case DUNE_USER_CONFIG:
      if (get_path_environment_ex(
              path, sizeof(path), subfolder, "BLENDER_USER_CONFIG", check_is_dir)) {
        break;
      }
      get_path_user_ex(path, sizeof(path), "config", subfolder, version, check_is_dir);
      break;
    case DUNE_USER_AUTOSAVE:
      if (get_path_environment_ex(
              path, sizeof(path), subfolder, "DUNE_USER_AUTOSAVE", check_is_dir)) {
        break;
      }
      get_path_user_ex(path, sizeof(path), "autosave", subfolder, version, check_is_dir);
      break;
    case DUNE_USER_SCRIPTS:
      if (get_path_environment_ex(
              path, sizeof(path), subfolder, "DUNE_USER_SCRIPTS", check_is_dir)) {
        break;
      }
      get_path_user_ex(path, sizeof(path), "scripts", subfolder, version, check_is_dir);
      break;
    default:
      LIB_assert_unreachable();
      break;
  }

  if ('\0' == path[0]) {
    return NULL;
  }
  return path;
}

const char *KERNEL_appdir_folder_id_create(const int folder_id, const char *subfolder)
{
  const char *path;

  /* Only for user folders. */
  if (!ELEM(folder_id,
            DUNE_USER_DATAFILES,
            DUNE_USER_CONFIG,
            DUNE_USER_SCRIPTS,
            DUNE_USER_AUTOSAVE)) {
    return NULL;
  }

  path = KERNEL_appdir_folder_id(folder_id, subfolder);

  if (!path) {
    path = KERNEL_appdir_folder_id_user_notest(folder_id, subfolder);
    if (path) {
      LIB_dir_create_recursive(path);
    }
  }

  return path;
}

const char *KERNEL_appdir_folder_id_version(const int folder_id,
                                         const int version,
                                         const bool check_is_dir)
{
  static char path[FILE_MAX] = "";
  bool ok;
  switch (folder_id) {
    case DUNE_RESOURCE_PATH_USER:
      ok = get_path_user_ex(path, sizeof(path), NULL, NULL, version, check_is_dir);
      break;
    case DUNE_RESOURCE_PATH_LOCAL:
      ok = get_path_local_ex(path, sizeof(path), NULL, NULL, version, check_is_dir);
      break;
    case DUNE_RESOURCE_PATH_SYSTEM:
      ok = get_path_system_ex(path, sizeof(path), NULL, NULL, version, check_is_dir);
      break;
    default:
      path[0] = '\0'; /* in case check_is_dir is false */
      ok = false;
      LIB_assert_msg(0, "incorrect ID");
      break;
  }
  return ok ? path : NULL;
}

/* -------------------------------------------------------------------- */
/** Program Path Queries
 *
 * Access locations of Dune & Python.
 **/

/**
 * Checks if name is a fully qualified filename to an executable.
 * If not it searches `$PATH` for the file. On Windows it also
 * adds the correct extension (`.com` `.exe` etc) from
 * `$PATHEXT` if necessary. Also on Windows it translates
 * the name to its 8.3 version to prevent problems with
 * spaces and stuff. Final result is returned in fullname.
 *
 * param fullname: The full path and full name of the executable
 * (must be #FILE_MAX minimum)
 * param name: The name of the executable (usually `argv[0]`) to be checked
 */
static void where_am_i(char *fullname, const size_t maxlen, const char *name)
{
#ifdef WITH_BINRELOC
  /* Linux uses `binreloc` since `argv[0]` is not reliable, call `br_init(NULL)` first. */
  {
    const char *path = NULL;
    path = br_find_exe(NULL);
    if (path) {
      LIB_strncpy(fullname, path, maxlen);
      free((void *)path);
      return;
    }
  }
#endif

#ifdef _WIN32
  {
    wchar_t *fullname_16 = MEM_mallocN(maxlen * sizeof(wchar_t), "ProgramPath");
    if (GetModuleFileNameW(0, fullname_16, maxlen)) {
      conv_utf_16_to_8(fullname_16, fullname, maxlen);
      if (!LIB_exists(fullname)) {
        CLOG_ERROR(&LOG, "path can't be found: \"%.*s\"", (int)maxlen, fullname);
        MessageBox(
            NULL, "path contains invalid characters or is too long (see console)", "Error", MB_OK);
      }
      MEM_freeN(fullname_16);
      return;
    }

    MEM_freeN(fullname_16);
  }
#endif

  /* Unix and non Linux. */
  if (name && name[0]) {

    LIB_strncpy(fullname, name, maxlen);
    if (name[0] == '.') {
      LIB_path_abs_from_cwd(fullname, maxlen);
#ifdef _WIN32
      LIB_path_program_extensions_add_win32(fullname, maxlen);
#endif
    }
    else if (LIB_path_slash_rfind(name)) {
      /* Full path. */
      LIB_strncpy(fullname, name, maxlen);
#ifdef _WIN32
      LIB_path_program_extensions_add_win32(fullname, maxlen);
#endif
    }
    else {
      LIB_path_program_search(fullname, maxlen, name);
    }
    /* Remove "/./" and "/../" so string comparisons can be used on the path. */
    LIB_path_normalize(NULL, fullname);

#if defined(DEBUG)
    if (!STREQ(name, fullname)) {
      CLOG_INFO(&LOG, 2, "guessing '%s' == '%s'", name, fullname);
    }
#endif
  }
}

void KERNEL_appdir_program_path_init(const char *argv0)
{
  where_am_i(g_app.program_filename, sizeof(g_app.program_filename), argv0);
  LIB_split_dir_part(g_app.program_filename, g_app.program_dirname, sizeof(g_app.program_dirname));
}

const char *KERNEL_appdir_program_path(void)
{
  LIB_assert(g_app.program_filename[0]);
  return g_app.program_filename;
}

const char *KERNEL_appdir_program_dir(void)
{
  LIB_assert(g_app.program_dirname[0]);
  return g_app.program_dirname;
}

bool KERNEL_appdir_program_python_search(char *fullpath,
                                      const size_t fullpath_len,
                                      const int version_major,
                                      const int version_minor)
{
  ASSERT_IS_INIT();

#ifdef PYTHON_EXECUTABLE_NAME
  /* Passed in from the build-systems 'PYTHON_EXECUTABLE'. */
  const char *python_build_def = STRINGIFY(PYTHON_EXECUTABLE_NAME);
#endif
  const char *basename = "python";
#if defined(WIN32) && !defined(NDEBUG)
  const char *basename_debug = "python_d";
#endif
  char python_version[16];
  /* Check both possible names. */
  const char *python_names[] = {
#ifdef PYTHON_EXECUTABLE_NAME
    python_build_def,
#endif
#if defined(WIN32) && !defined(NDEBUG)
    basename_debug,
#endif
    python_version,
    basename,
  };
  bool is_found = false;

  SNPRINTF(python_version, "%s%d.%d", basename, version_major, version_minor);

  {
    const char *python_bin_dir = KERNEL_appdir_folder_id(DUNE_SYSTEM_PYTHON, "bin");
    if (python_bin_dir) {

      for (int i = 0; i < ARRAY_SIZE(python_names); i++) {
        LIB_join_dirfile(fullpath, fullpath_len, python_bin_dir, python_names[i]);

        if (
#ifdef _WIN32
            LIB_path_program_extensions_add_win32(fullpath, fullpath_len)
#else
            LIB_exists(fullpath)
#endif
        ) {
          is_found = true;
          break;
        }
      }
    }
  }

  if (is_found == false) {
    for (int i = 0; i < ARRAY_SIZE(python_names); i++) {
      if (LIB_path_program_search(fullpath, fullpath_len, python_names[i])) {
        is_found = true;
        break;
      }
    }
  }

  if (is_found == false) {
    *fullpath = '\0';
  }

  return is_found;
}

/* -------------------------------------------------------------------- */
/** Application Templates **/

/** Keep in sync with `bpy.utils.app_template_paths()` */
static const char *app_template_directory_search[2] = {
    "startup" SEP_STR "dune_app_templates_user",
    "startup" SEP_STR "dune_app_templates_system",
};

static const int app_template_directory_id[2] = {
    /* Only 'USER' */
    DUNE_USER_SCRIPTS,
    /* Covers 'LOCAL' & 'SYSTEM'. */
    DUNE_SYSTEM_SCRIPTS,
};

bool KERNEL_appdir_app_template_any(void)
{
  char temp_dir[FILE_MAX];
  for (int i = 0; i < ARRAY_SIZE(app_template_directory_id); i++) {
    if (KERNEL_appdir_folder_id_ex(app_template_directory_id[i],
                                app_template_directory_search[i],
                                temp_dir,
                                sizeof(temp_dir))) {
      return true;
    }
  }
  return false;
}

bool KERNEL_appdir_app_template_id_search(const char *app_template, char *path, size_t path_len)
{
  for (int i = 0; i < ARRAY_SIZE(app_template_directory_id); i++) {
    char subdir[FILE_MAX];
    LIB_join_dirfile(subdir, sizeof(subdir), app_template_directory_search[i], app_template);
    if (KERNEL_appdir_folder_id_ex(app_template_directory_id[i], subdir, path, path_len)) {
      return true;
    }
  }
  return false;
}

bool KERNEL_appdir_app_template_has_userpref(const char *app_template)
{
  /* Test if app template provides a `userpref.dune`.
   * If not, we will share user preferences with the rest of Dune. */
  if (app_template[0] == '\0') {
    return false;
  }

  char app_template_path[FILE_MAX];
  if (!KERNEL_appdir_app_template_id_search(
          app_template, app_template_path, sizeof(app_template_path))) {
    return false;
  }

  char userpref_path[FILE_MAX];
  LIB_path_join(
      userpref_path, sizeof(userpref_path), app_template_path, BLENDER_USERPREF_FILE, NULL);
  return LIB_exists(userpref_path);
}

void KERNEL_appdir_app_templates(ListBase *templates)
{
  LIB_listbase_clear(templates);

  for (int i = 0; i < ARRAY_SIZE(app_template_directory_id); i++) {
    char subdir[FILE_MAX];
    if (!KERNEL_appdir_folder_id_ex(app_template_directory_id[i],
                                 app_template_directory_search[i],
                                 subdir,
                                 sizeof(subdir))) {
      continue;
    }

    struct direntry *dir;
    uint totfile = LIB_filelist_dir_contents(subdir, &dir);
    for (int f = 0; f < totfile; f++) {
      if (!FILENAME_IS_CURRPAR(dir[f].relname) && S_ISDIR(dir[f].type)) {
        char *template = LIB_strdup(dir[f].relname);
        LIB_addtail(templates, LIB_genericNodeN(template));
      }
    }

    LIB_filelist_free(dir, totfile);
  }
}

/* -------------------------------------------------------------------- */
/** Temporary Directories **/

/**
 * Gets the temp directory when dune first runs.
 * If the default path is not found, use try $TEMP
 *
 * Also make sure the temp dir has a trailing slash
 *
 * param tempdir: The full path to the temporary temp directory.
 * param tempdir_len: The size of the tempdir buffer.
 * param userdir: Directory specified in user preferences (may be NULL).
 * note that by default this is an empty string, only use when non-empty.
 */
static void where_is_temp(char *tempdir, const size_t tempdir_len, const char *userdir)
{

  tempdir[0] = '\0';

  if (userdir && LIB_is_dir(userdir)) {
    LIB_strncpy(tempdir, userdir, tempdir_len);
  }

  if (tempdir[0] == '\0') {
    const char *env_vars[] = {
#ifdef WIN32
        "TEMP",
#else
        /* Non standard (could be removed). */
        "TMP",
        /* Posix standard. */
        "TMPDIR",
#endif
    };
    for (int i = 0; i < ARRAY_SIZE(env_vars); i++) {
      const char *tmp = LIB_getenv(env_vars[i]);
      if (tmp && (tmp[0] != '\0') && LIB_is_dir(tmp)) {
        LIB_strncpy(tempdir, tmp, tempdir_len);
        break;
      }
    }
  }

  if (tempdir[0] == '\0') {
    LIB_strncpy(tempdir, "/tmp/", tempdir_len);
  }
  else {
    /* add a trailing slash if needed */
    LIB_path_slash_ensure(tempdir);
  }
}

static void tempdir_session_create(char *tempdir_session,
                                   const size_t tempdir_session_len,
                                   const char *tempdir)
{
  tempdir_session[0] = '\0';

  const int tempdir_len = strlen(tempdir);
  /* 'XXXXXX' is kind of tag to be replaced by `mktemp-family` by an UUID. */
  const char *session_name = "blender_XXXXXX";
  const int session_name_len = strlen(session_name);

  /* +1 as a slash is added,
   * #_mktemp_s also requires the last null character is included. */
  const int tempdir_session_len_required = tempdir_len + session_name_len + 1;

  if (tempdir_session_len_required <= tempdir_session_len) {
    /* No need to use path joining utility as we know the last character of #tempdir is a slash. */
    LIB_string_join(tempdir_session, tempdir_session_len, tempdir, session_name);
#ifdef WIN32
    const bool needs_create = (_mktemp_s(tempdir_session, tempdir_session_len_required) == 0);
#else
    const bool needs_create = (mkdtemp(tempdir_session) == NULL);
#endif
    if (needs_create) {
      LIB_dir_create_recursive(tempdir_session);
    }
    if (LIB_is_dir(tempdir_session)) {
      LIB_path_slash_ensure(tempdir_session);
      /* Success. */
      return;
    }
  }

  CLOG_WARN(&LOG,
            "Could not generate a temp file name for '%s', falling back to '%s'",
            tempdir_session,
            tempdir);
  LIB_strncpy(tempdir_session, tempdir, tempdir_session_len);
}

void KERNEL_tempdir_init(const char *userdir)
{
  /* Sets #g_app.temp_dirname_base to \a userdir if specified and is a valid directory,
   * otherwise chooses a suitable OS-specific temporary directory.
   * Sets #g_app.temp_dirname_session to a #mkdtemp
   * generated sub-dir of #g_app.temp_dirname_base. */

  where_is_temp(g_app.temp_dirname_base, sizeof(g_app.temp_dirname_base), userdir);

  /* Clear existing temp dir, if needed. */
  KERNEL_tempdir_session_purge();
  /* Now that we have a valid temp dir, add system-generated unique sub-dir. */
  tempdir_session_create(
      g_app.temp_dirname_session, sizeof(g_app.temp_dirname_session), g_app.temp_dirname_base);
}

const char *KERNEL_tempdir_session(void)
{
  return g_app.temp_dirname_session[0] ? g_app.temp_dirname_session : BKE_tempdir_base();
}

const char *KERNEL_tempdir_base(void)
{
  return g_app.temp_dirname_base;
}

void KERNEL_tempdir_session_purge(void)
{
  if (g_app.temp_dirname_session[0] && LIB_is_dir(g_app.temp_dirname_session)) {
    LIB_delete(g_app.temp_dirname_session, true, true);
  }
}
