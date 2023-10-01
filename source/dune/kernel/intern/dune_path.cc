/* TODO:
 * currently there are some cases we don't support.
 * - passing output paths to the visitor?, like render out.
 * - passing sequence strips with many images.
 * - passing directory paths - visitors don't know which path is a dir or a file.
 */

#include <sys/stat.h>

#include <cstring>

/* path/file handling stuff */
#ifndef WIN32
#  include <dirent.h>
#  include <unistd.h>
#else
#  include "lib_winstuff.h"
#  include <io.h>
#endif

#include "mem_guardedalloc.h"

#include "types_brush.h"
#include "types_cachefile.h"
#include "types_fluid.h"
#include "types_freestyle.h"
#include "types_image.h"
#include "types_material.h"
#include "types_mesh.h"
#include "types_mod.h"
#include "types_movieclip.h"
#include "types_node.h"
#include "types_object_fluidsim.h"
#include "types_object_force.h"
#include "types_object.h"
#include "types_particle.h"
#include "types_pointcache.h"
#include "types_scene.h"
#include "types_seq.h"
#include "types_sound.h"
#include "types_text.h"
#include "types_texture.h"
#include "types_vfont.h"
#include "types_volume.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "graph.hh"

#include "dune_idtype.h"
#include "dune_image.h"
#include "dune_lib_id.h"
#include "dune_lib.h"
#include "dune_main.h"
#include "dune_node.h"
#include "dune_report.h"
#include "dune_vfont.h"

#include "dune_path.h" /* own include */

#include "CLG_log.h"

#include "seq_iter.h"

#ifndef _MSC_VER
#  include "lib_strict_flags.h"
#endif

static CLG_LogRef LOG = {"dune.path"};

/* Generic File Path Traversal API */
void dune_path_foreach_path_id(PathForeachPathData *path_data, Id *id)
{
  const ePForIdeachFlag flag = path_data->flag;
  const char *absbase = (flag & PATH_FOREACH_PATH_ABSOLUTE) ?
                            ID_DUNE_PATH(path_data->main, id) :
                            nullptr;
  path_data->absolute_base_path = absbase;
  path_data->owner_id = id;
  path_data->is_path_mod = false;

  if ((flag & PATH_FOREACH_PATH_SKIP_LINKED) && ID_IS_LINKED(id)) {
    return;
  }

  if (id->lib_weak_ref != nullptr &&
      (flag & PATH_TRAVERSE_SKIP_WEAK_REF) == 0) {
    path_foreach_path_fixed_process(path_data,
                                    id->lib_weak_ref->lib_filepath,
                                    sizeof(id->lib_weak_ref->lib_filepath));
  }

  NodeTree *embedded_node_tree = ntreeFromId(id);
  if (embedded_node_tree != nullptr) {
    path_foreach_path_id(bpath_data, &embedded_node_tree->id);
  }

  const IdTypeInfo *id_type = dune_idtype_get_info_from_id(id);

  lib_assert(id_type != nullptr);
  if (id_type == nullptr || id_type->foreach_path == nullptr) {
    return;
  }

  id_type->foreach_path(id, path_data);

  if (path_data->is_path_mod) {
    dune_graph_id_tag_update(id, ID_RECALC_SOURCE | ID_RECALC_COPY_ON_WRITE);
  }
}

void dune_path_foreach_path_main(PathForeachPathData *path_data)
{
  Id *id;
  FOREACH_MAIN_ID_BEGIN (path_data->main, id) {
    dune_path_foreach_path_id(path_data, id);
  }
  FOREACH_MAIN_ID_END;
}

bool dune_path_foreach_path_fixed_process(PathForeachPathData *path_data,
                                          char *path,
                                          size_t path_maxncpy)
{
  const char *absolute_base_path = path_data->absolute_base_path;

  char path_src_buf[FILE_MAX];
  const char *path_src;
  char path_dst[FILE_MAX];

  if (absolute_base_path) {
    STRNCPY(path_src_buf, path);
    lib_path_abs(path_src_buf, absolute_base_path);
    path_src = path_src_buf;
  }
  else {
    path_src = path;
  }

  /* so functions can check old value */
  STRNCPY(path_dst, path);

  if (path_data->cb_fn(path_data, path_dst, sizeof(path_dst), path_src)) {
    lib_strncpy(path, path_dst, path_maxncpy);
    bpath_data->is_path_mod = true;
    return true;
  }

  return false;
}

bool dune_path_foreach_path_dirfile_fixed_process(PathForeachPathData *path_data,
                                                  char *path_dir,
                                                  size_t path_dir_maxncpy,
                                                  char *path_file,
                                                  size_t path_file_maxncpy)
{
  const char *absolute_base_path = path_data->absolute_base_path;

  char path_src[FILE_MAX];
  char path_dst[FILE_MAX];

  lib_path_join(path_src, sizeof(path_src), path_dir, path_file);

  /* So that functions can access the old value. */
  STRNCPY(path_dst, path_src);

  if (absolute_base_path) {
    lib_path_abs(path_src, absolute_base_path);
  }

  if (apath_data->cb_fn(
        path_data, path_dst, sizeof(path_dst), (const char *)path_src)) {
    lib_path_split_dir_file(path_dst, path_dir, path_dir_maxncpy, path_file, path_file_maxncpy);
    path_data->is_path_mod = true;
    return true;
  }

  return false;
}

bool dune_path_foreach_path_allocated_process(PathForeachPathData *path_data, char **path)
{
  const char *absolute_base_path = path_data->absolute_base_path;

  char path_src_buf[FILE_MAX];
  const char *path_src;
  char path_dst[FILE_MAX];

  if (absolute_base_path) {
    STRNCPY(path_src_buf, *path);
    lib_path_abs(path_src_buf, absolute_base_path);
    path_src = path_src_buf;
  }
  else {
    path_src = *path;
  }

  if (path_data->cb_fn(path_data, path_dst, sizeof(path_dst), path_src)) {
    mem_freen(*path);
    (*path) = lib_strdup(path_dst);
    path_data->is_path_mod = true;
    return true;
  }

  return false;
}

/* name Check Missing Files */
static bool check_missing_files_foreach_path_cb(PathForeachPathData *path_data,
                                                char * /*path_dst*/,
                                                size_t /*path_dst_maxncpy*/,

                                                const char *path_src)
{
  ReportList *reports = (ReportList *)path_data->user_data;

  if (!lib_exists(path_src)) {
    dune_reportf(reports, RPT_WARNING, "Path '%s' not found", path_src);
  }

  return false;
}

void dune_path_missing_files_check(Main *main, ReportList *reports)
{
  PathForeachPathData path_data{};
  path_data.main = main;
  path_data.cb_fn = check_missing_files_foreach_path_cb;
  path_data.flag = PATH_FOREACH_PATH_ABSOLUTE | DUNE_BPATH_FOREACH_PATH_SKIP_PACKED |
                   PATH_FOREACH_PATH_RESOLVE_TOKEN | DUNE_BPATH_TRAVERSE_SKIP_WEAK_REFERENCES;
  path_data.user_data = reports;
  dune_path_foreach_path_main(&path_data);

  if (lib_list_is_empty(&reports->list)) {
    dune_reportf(reports, RPT_INFO, "No missing files");
  }
}

/* Find Missing Files */
#define MAX_DIR_RECURSE 16
#define FILESIZE_INVALID_DIRECTORY -1

/* Find the given filename recursively in the given search directory and its sub-directories.
 *
 * note Use the biggest matching file found, so that thumbnails don't get used by mistake.
 *
 * param search_directory: Directory to search in.
 * param filename_src: Search for this filename.
 * param r_filepath_new: The path of the new found file will be copied here, caller must
 *                        initialize as empty string.
 * param r_filesize: Size of the file, `FILESIZE_INVALID_DIRECTORY` if search directory could not
 *                    be opened.
 * param r_recurse_depth: Current recursion depth.
 *
 * return true if found, false otherwise. */
static bool missing_files_find__recursive(const char *search_directory,
                                          const char *filename_src,
                                          char r_filepath_new[FILE_MAX],
                                          int64_t *r_filesize,
                                          int *r_recurse_depth)
{
  /* TODO: Move this function to BLI_path_utils? The 'biggest size' behavior is quite specific
   * though... */
  DIR *dir;
  lib_stat_t status;
  char path[FILE_MAX];
  int64_t size;
  bool found = false;

  dir = opendir(search_directory);

  if (dir == nullptr) {
    return found;
  }

  if (*r_filesize == FILESIZE_INVALID_DIRECTORY) {
    *r_filesize = 0; /* The directory opened fine. */
  }

  for (dirent *de = readdir(dir); de != nullptr; de = readdir(dir)) {
    if (FILENAME_IS_CURRPAR(de->d_name)) {
      continue;
    }

    lib_path_join(path, sizeof(path), search_directory, de->d_name);

    if (lib_stat(path, &status) == -1) {
      CLOG_WARN(&LOG, "Cannot get file status (`stat()`) of '%s'", path);
      continue;
    }

    if (S_ISREG(status.st_mode)) {                                  /* It is a file. */
      if (lib_path_ncmp(filename_src, de->d_name, FILE_MAX) == 0) { /* Names match. */
        size = status.st_size;
        if ((size > 0) && (size > *r_filesize)) { /* Find the biggest matching file. */
          *r_filesize = size;
          lib_strncpy(r_filepath_new, path, FILE_MAX);
          found = true;
        }
      }
    }
    else if (S_ISDIR(status.st_mode)) { /* It is a sub-directory. */
      if (*r_recurse_depth <= MAX_DIR_RECURSE) {
        (*r_recurse_depth)++;
        found |= missing_files_find__recursive(
            path, filename_src, r_filepath_new, r_filesize, r_recurse_depth);
        (*r_recurse_depth)--;
      }
    }
  }

  closedir(dir);
  return found;
}

struct PathFind_Data {
  const char *basedir;
  const char *searchdir;
  ReportList *reports;
  bool find_all; /* Also search for files which current path is still valid. */
};

static bool missing_files_find_foreach_path_cb(PathForeachPathData *path_data,
                                               char *path_dst,
                                               size_t path_dst_maxncpy,
                                               const char *path_src)
{
  PathFind_Data *data = (PathFind_Data *)path_data->user_data;
  char filepath_new[FILE_MAX];

  int64_t filesize = FILESIZE_INVALID_DIRECTORY;
  int recurse_depth = 0;
  bool is_found;

  if (!data->find_all && lib_exists(path_src)) {
    return false;
  }

  filepath_new[0] = '\0';

  is_found = missing_files_find__recursive(
      data->searchdir, lib_path_basename(path_src), filepath_new, &filesize, &recurse_depth);

  if (filesize == FILESIZE_INVALID_DIRECTORY) {
    dune_reportf(data->reports,
                RPT_WARNING,
                "Could not open the directory '%s'",
                lib_path_basename(data->searchdir));
    return false;
  }
  if (is_found == false) {
    dune_reportf(data->reports,
                RPT_WARNING,
                "Could not find '%s' in '%s'",
                lib_path_basename(path_src),
                data->searchdir);
    return false;
  }

  /* Keep the path relative if the previous one was relative. */
  if (lib_path_is_rel(path_dst)) {
    lib_path_rel(filepath_new, data->basedir);
  }
  lib_strncpy(path_dst, filepath_new, path_dst_maxncpy);
  return true;
}

void dune_path_missing_files_find(Main *main,
                                  const char *searchpath,
                                  ReportList *reports,
                                  const bool find_all)
{
  PathFind_Data data = {nullptr};
  const int flag = PATH_FOREACH_PATH_ABSOLUTE | PATH_FOREACH_PATH_RELOAD_EDITED |
                   PATH_FOREACH_PATH_RESOLVE_TOKEN | PATH_TRAVERSE_SKIP_WEAK_REFERENCES;

  data.basedir = dune_main_file_path(main);
  data.reports = reports;
  data.searchdir = searchpath;
  data.find_all = find_all;

  PathForeachPathData path_data{};
  path_data.main = main;
  path_data.cb_fn = missing_files_find_foreach_path_cb;
  path_data.flag = ePathForeachFlag(flag);
  path_data.user_data = &data;
  dune_path_foreach_path_main(&path_data);
}

#undef MAX_DIR_RECURSE
#undef FILESIZE_INVALID_DIRECTORY

/* Rebase Relative Paths */
struct PathRebase_Data {
  const char *basedir_src;
  const char *basedir_dst;
  ReportList *reports;

  int count_tot;
  int count_changed;
  int count_failed;
};

static bool relative_rebase_foreach_path_cb(PathForeachPathData *path_data,
                                            char *path_dst,
                                            size_t path_dst_maxncpy,
                                            const char *path_src)
{
  PathRebase_Data *data = (PathRebase_Data *)path_data->user_data;

  data->count_tot++;

  if (!lib_path_is_rel(path_src)) {
    /* Absolute, leave this as-is. */
    return false;
  }

  char filepath[(FILE_MAXDIR * 2) + FILE_MAXFILE];
  lib_strncpy(filepath, path_src, FILE_MAX);
  if (!lib_path_abs(filepath, data->basedir_src)) {
    dune_reportf(data->reports, RPT_WARNING, "Path '%s' cannot be made absolute", path_src);
    data->count_failed++;
    return false;
  }

  lib_path_normalize(filepath);

  /* This may fail, if so it's fine to leave absolute since the path is still valid. */
  lib_path_rel(filepath, data->basedir_dst);

  lib_strncpy(path_dst, filepath, path_dst_maxncpy);
  data->count_changed++;
  return true;
}

void dune_path_relative_rebase(Main *main,
                               const char *basedir_src,
                               const char *basedir_dst,
                               ReportList *reports)
{
  PathRebase_Data data = {nullptr};
  const int flag = (PATH_FOREACH_PATH_SKIP_LINKED | PATH_FOREACH_PATH_SKIP_MULTIFILE);

  lib_assert(basedir_src[0] != '\0');
  lib_assert(basedir_dst[0] != '\0');

  data.basedir_src = basedir_src;
  data.basedir_dst = basedir_dst;
  data.reports = reports;

  PathForeachPathData path_data{};
  path_data.main = main;
  path_data.cb_fn = relative_rebase_foreach_path_cb;
  path_data.flag = ePathForeachFlag(flag);
  path_data.user_data = &data;
  dune_path_foreach_path_main(&path_data);

  dune_reportf(reports,
              data.count_failed ? RPT_WARNING : RPT_INFO,
              "Total files %d | Changed %d | Failed %d",
              data.count_tot,
              data.count_changed,
              data.count_failed);
}

/* Make Paths Relative Or Absolute */
struct PathRemap_Data {
  const char *basedir;
  ReportList *reports;

  int count_tot;
  int count_changed;
  int count_failed;
};

static bool relative_convert_foreach_path_cb(PathForeachPathData *path_data,
                                             char *path_dst,
                                             size_t path_dst_maxncpy,
                                             const char *path_src)
{
  PathRemap_Data *data = (PathRemap_Data *)path_data->user_data;

  data->count_tot++;

  if (lib_path_is_rel(path_src)) {
    return false; /* Already relative. */
  }

  char path_test[FILE_MAX];
  STRNCPY(path_test, path_src);

  lib_path_rel(path_test, data->basedir);
  if (!lib_path_is_rel(path_test)) {
    const char *type_name = dune_idtype_get_info_from_id(path_data->owner_id)->name;
    const char *id_name = path_data->owner_id->name + 2;
    dune_reportf(data->reports,
                RPT_WARNING,
                "Path '%s' cannot be made relative for %s '%s'",
                path_src,
                type_name,
                id_name);
    data->count_failed++;
    return false;
  }

  lib_strncpy(path_dst, path_test, path_dst_maxncpy);
  data->count_changed++;
  return true;
}

static bool absolute_convert_foreach_path_cb(PathForeachPathData *path_data,
                                             char *path_dst,
                                             size_t path_dst_maxncpy,
                                             const char *path_src)
{
  PathRemap_Data *data = (PathRemap_Data *)path_data->user_data;

  data->count_tot++;

  if (!lib_path_is_rel(path_src)) {
    return false; /* Already absolute. */
  }

  char path_test[FILE_MAX];
  STRNCPY(path_test, path_src);
  lib_path_abs(path_test, data->basedir);
  if (lib_path_is_rel(path_test)) {
    const char *type_name = dune_idtype_get_info_from_id(path_data->owner_id)->name;
    const char *id_name = path_data->owner_id->name + 2;
    dune_reportf(data->reports,
                RPT_WARNING,
                "Path '%s' cannot be made absolute for %s '%s'",
                path_src,
                type_name,
                id_name);
    data->count_failed++;
    return false;
  }

  lib_strncpy(path_dst, path_test, path_dst_maxncpy);
  data->count_changed++;
  return true;
}

static void path_absolute_relative_convert(Main *main,
                                           const char *basedir,
                                           ReportList *reports,
                                           PathForeachPathFnCb cb_fn)
{
  PathRemap_Data data = {nullptr};
  const int flag = DUNE_PATH_FOREACH_PATH_SKIP_LINKED;

  lib_assert(basedir[0] != '\0');
  if (basedir[0] == '\0') {
    CLOG_ERROR(&LOG, "basedir='', this is a bug");
    return;
  }

  data.basedir = basedir;
  data.reports = reports;

  PathForeachPathData path_data{};
  path_data.main = main;
  path_data.cb_fn = cb_fn;
  path_data.flag = ePathForeachFlag(flag);
  path_data.user_data = &data;
  dune_path_foreach_path_main(&path_data);

  dune_reportf(reports,
              data.count_failed ? RPT_WARNING : RPT_INFO,
              "Total files %d | Changed %d | Failed %d",
              data.count_tot,
              data.count_changed,
              data.count_failed);
}

void dune_path_relative_convert(Main *main, const char *basedir, ReportList *reports)
{
  path_absolute_relative_convert(main, basedir, reports, relative_convert_foreach_path_cb);
}

void dune_path_absolute_convert(Main *main, const char *basedir, ReportList *reports)
{
  path_absolute_relative_convert(main, basedir, reports, absolute_convert_foreach_path_cb);
}

/* Backup/Restore/Free paths list fns. */
struct PathStore {
  PathStore *next, *prev;
  /** Over allocate. */
  char filepath[0];
};

static bool path_list_append(PathForeachPathData *path_data,
                              char * /*path_dst*/,
                              size_t /*path_dst_maxncpy*/,
                              const char *path_src)
{
  List *path_list = static_cast<List *>(path_data->user_data);
  size_t path_size = strlen(path_src) + 1;

  /* NOTE: the PathStore and its string are allocated together in a single alloc. */
  PathStore *path_store = static_cast<PathStore *>(
      mem_mallocn(sizeof(PathStore) + path_size, __func__));

  char *filepath = path_store->filepath;

  memcpy(filepath, path_src, path_size);
  lib_addtail(path_list, path_store);
  return false;
}

static bool path_list_restore(PathForeachPathData *path_data,
                              char *path_dst,
                              size_t path_dst_maxncpy,
                              const char *path_src)
{
  List *path_list = static_cast<List *>(path_data->user_data);

  /* `ls->first` should never be nullptr, because the number of paths should not change.
   * If this happens, there is a bug in caller code. */
  lib_assert(!lib_list_is_empty(path_list));

  PathStore *path_store = static_cast<PathStore *>(path_list->first);
  const char *filepath = path_store->filepath;
  bool is_path_changed = false;

  if (!STREQ(path_src, filepath)) {
    lib_strncpy(path_dst, filepath, path_dst_maxncpy);
    is_path_changed = true;
  }

  lib_freelinkn(path_list, path_store);
  return is_path_changed;
}

void *dune_path_list_backup(Main *main, const ePathForeachFlag flag)
{
  List *path_list = static_cast<List *>(mem_callocn(sizeof(ListBase), __func__));

  PathForeachPathData path_data{};
  path_data.main = main;
  path_data.cb_fn = path_list_append;
  path_data.flag = flag;
  path_data.user_data = path_list;
  dune_path_foreach_path_main(&path_data);

  return path_list;
}

void dune_path_list_restore(Main *main, const ePathForeachFlag flag, void *path_list_handle)
{
  List *path_list = static_cast<List *>(path_list_handle);

  PathForeachPathData path_data{};
  path_data.main = main;
  path_data.cb_fn = path_list_restore;
  path_data.flag = flag;
  path_data.user_data = path_list;
  dune_path_foreach_path_main(&path_data);
}

void dune_path_list_free(void *path_list_handle)
{
  List *path_list = static_cast<List *>(path_list_handle);
  /* The whole list should have been consumed by dune_path_list_restore, see also comment in
   * path_list_restore. */
  lib_assert(lib_list_is_empty(path_list));

  lib_freelistn(path_list);
  mem_freen(path_list);
}
