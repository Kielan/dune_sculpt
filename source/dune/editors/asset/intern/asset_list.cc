#include <optional>
#include <string>

#include "BKE_context.h"

#include "BLI_map.hh"
#include "BLI_path_util.h"
#include "BLI_utility_mixins.hh"

#include "types_space.h"

#include "BKE_preferences.h"

#include "ED_fileselect.h"

#include "WM_api.h"

/* XXX uses private header of file-space. */
#include "../space_file/file_indexer.h"
#include "../space_file/filelist.h"

#include "ED_asset_handle.h"
#include "ED_asset_indexer.h"
#include "ED_asset_list.h"
#include "ED_asset_list.hh"
#include "asset_library_reference.hh"

namespace blender::ed::asset {

/* -------------------------------------------------------------------- */
/** Asset list API
 *
 *  Internally re-uses #FileList from the File Browser. It does all the heavy lifting already.
 **/

/**
 * RAII wrapper for `FileList`
 */
class FileListWrapper {
  static void filelist_free_fn(FileList *list)
  {
    filelist_free(list);
    MEM_freeN(list);
  }

  std::unique_ptr<FileList, decltype(&filelist_free_fn)> file_list_;

 public:
  explicit FileListWrapper(eFileSelectType filesel_type)
      : file_list_(filelist_new(filesel_type), filelist_free_fn)
  {
  }
  FileListWrapper(FileListWrapper &&other) = default;
  FileListWrapper &operator=(FileListWrapper &&other) = default;
  ~FileListWrapper()
  {
    /* Destructs the owned pointer. */
    file_list_ = nullptr;
  }

  operator FileList *() const
  {
    return file_list_.get();
  }
};

class PreviewTimer {
  /* Non-owning! The Window-Manager registers and owns this. */
  wmTimer *timer_ = nullptr;

 public:
  void ensureRunning(const duneContext *C)
  {
    if (!timer_) {
      timer_ = wm_event_add_timer_notifier(
          ctx_wm_manager(C), ctx_wm_window(C), NC_ASSET | ND_ASSET_LIST_PREVIEW, 0.01);
    }
  }

  void stop(const duneContext *C)
  {
    if (timer_) {
      WM_event_remove_timer_notifier(CTX_wm_manager(C), CTX_wm_window(C), timer_);
      timer_ = nullptr;
    }
  }
};

class AssetList : NonCopyable {
  FileListWrapper filelist_;
  AssetLibraryReference lib_ref_;
  PreviewTimer previews_timer_;

 public:
  AssetList() = delete;
  AssetList(eFileSelectType filesel_type, const AssetLibRef &asset_lib_ref);
  AssetList(AssetList &&other) = default;
  ~AssetList() = default;

  void setup();
  void fetch(const duneContext &C);
  void ensurePreviewsJob(duneContext *C);
  void clear(duneContext *C);

  bool needsRefetch() const;
  void iterate(AssetListIterFn fn) const;
  bool listen(const wmNotifier &notifier) const;
  int size() const;
  void tagMainDataDirty() const;
  void remapID(ID *id_old, ID *id_new) const;
  StringRef filepath() const;
};

AssetList::AssetList(eFileSelectType filesel_type, const AssetLibRef &asset_lib_ref)
    : filelist_(filesel_type), lib_ref_(asset_lib_ref)
{
}

void AssetList::setup()
{
  FileList *files = filelist_;

  dUserAssetLib *user_lib = nullptr;

  /* Ensure valid repository, or fall-back to local one. */
  if (library_ref_.type == ASSET_LIBRARY_CUSTOM) {
    LIB_assert(lib_ref_.custom_lib_index >= 0);

    user_lib = dune_prefs_asset_lib_find_from_index(
        &U, lib_ref_.custom_lib_index);
  }

  /* Relevant bits from file_refresh(). */
  /* TODO pass options properly. */
  filelist_setrecursion(files, FILE_SELECT_MAX_RECURSIONS);
  filelist_setsorting(files, FILE_SORT_ALPHA, false);
  filelist_setlib(files, &lib_ref_);
  filelist_setfilteroptions(
      files,
      false,
      true,
      true, /* Just always hide parent, prefer to not add an extra user option for this. */
      FILE_TYPE_DUNELIB,
      FILTER_ID_ALL,
      true,
      "",
      "");

  const bool use_asset_indexer = !USER_EXPERIMENTAL_TEST(&U, no_asset_indexing);
  filelist_setindexer(files, use_asset_indexer ? &file_indexer_asset : &file_indexer_noop);

  char path[FILE_MAXDIR] = "";
  if (user_library) {
    LIB_strncpy(path, user_lib->path, sizeof(path));
    filelist_setdir(files, path);
  }
  else {
    filelist_setdir(files, path);
  }
}

void AssetList::fetch(const bContext &C)
{
  FileList *files = filelist_;

  if (filelist_needs_force_reset(files)) {
    filelist_readjob_stop(files, CTX_wm_manager(&C));
    filelist_clear_from_reset_tag(files);
  }

  if (filelist_needs_reading(files)) {
    if (!filelist_pending(files)) {
      filelist_readjob_start(files, NC_ASSET | ND_ASSET_LIST_READING, &C);
    }
  }
  filelist_sort(files);
  filelist_filter(files);
}

bool AssetList::needsRefetch() const
{
  return filelist_needs_force_reset(filelist_) || filelist_needs_reading(filelist_);
}

void AssetList::iterate(AssetListIterFn fn) const
{
  FileList *files = filelist_;
  int numfiles = filelist_files_ensure(files);

  for (int i = 0; i < numfiles; i++) {
    FileDirEntry *file = filelist_file(files, i);
    if ((file->typeflag & FILE_TYPE_ASSET) == 0) {
      continue;
    }

    AssetHandle asset_handle = {file};
    if (!fn(asset_handle)) {
      /* If the callback returns false, we stop iterating. */
      break;
    }
  }
}

void AssetList::ensurePreviewsJob(duneContext *C)
{
  FileList *files = filelist_;
  int numfiles = filelist_files_ensure(files);

  filelist_cache_previews_set(files, true);
  filelist_file_cache_slidingwindow_set(files, 256);
  /* TODO fetch all previews for now. */
  filelist_file_cache_block(files, numfiles / 2);
  filelist_cache_previews_update(files);

  {
    const bool previews_running = filelist_cache_previews_running(files) &&
                                  !filelist_cache_previews_done(files);
    if (previews_running) {
      previews_timer_.ensureRunning(C);
    }
    else {
      /* Preview is not running, no need to keep generating update events! */
      previews_timer_.stop(C);
    }
  }
}

void AssetList::clear(duneContext *C)
{
  /* Based on #ED_fileselect_clear() */

  FileList *files = filelist_;
  filelist_readjob_stop(files, ctx_wm_manager(C));
  filelist_freelib(files);
  filelist_clear(files);

  WM_main_add_notifier(NC_ASSET | ND_ASSET_LIST, nullptr);
}

/**
 * return True if the asset-list needs a UI redraw.
 */
bool AssetList::listen(const wmNotifier &notifier) const
{
  switch (notifier.category) {
    case NC_ID: {
      if (ELEM(notifier.action, NA_RENAME)) {
        return true;
      }
      break;
    }
    case NC_ASSET:
      if (ELEM(notifier.data, ND_ASSET_LIST, ND_ASSET_LIST_READING, ND_ASSET_LIST_PREVIEW)) {
        return true;
      }
      if (ELEM(notifier.action, NA_ADDED, NA_REMOVED, NA_EDITED)) {
        return true;
      }
      break;
  }

  return false;
}

/**
 * return The number of assets in the list.
 */
int AssetList::size() const
{
  return filelist_files_ensure(filelist_);
}

void AssetList::tagMainDataDirty() const
{
  if (filelist_needs_reset_on_main_changes(filelist_)) {
    filelist_tag_force_reset_mainfiles(filelist_);
  }
}

void AssetList::remapID(ID * /*id_old*/, ID * /*id_new*/) const
{
  /* Trigger full re-fetch of the file list if main data was changed, don't even attempt remap
   * pointers. We could give file list types a id-remap callback, but it's probably not worth it.
   * Refreshing local file lists is relatively cheap. */
  tagMainDataDirty();
}

StringRef AssetList::filepath() const
{
  return filelist_dir(filelist_);
}

/* -------------------------------------------------------------------- */
/** Runtime asset list cache */

/**
 * Class managing a global asset list map, each entry being a list for a specific asset library.
 */
class AssetListStorage {
  using AssetListMap = Map<AssetLibRefWrapper, AssetList>;

 public:
  /* Purely static class, can't instantiate this. */
  AssetListStorage() = delete;

  static void fetch_lib(const AssetLibRef &lib_ref, const duneContext &C);
  static void destruct();
  static AssetList *lookup_list(const AssetLibRef &lib_ref);
  static void tagMainDataDirty();
  static void remapID(ID *id_new, ID *id_old);

 private:
  static std::optional<eFileSelectType> asset_lib_ref_to_fileselect_type(
      const AssetLibRef &lib_ref);

  using is_new_t = bool;
  static std::tuple<AssetList &, is_new_t> ensure_list_storage(
      const AssetLibRef &lib_ref, eFileSelectType filesel_type);

  static AssetListMap &global_storage();
};

void AssetListStorage::fetch_lib(const AssetLibRef &lib_ref,
                                     const dContext &C)
{
  std::optional filesel_type = asset_lib_ref_to_fileselect_type(library_reference);
  if (!filesel_type) {
    return;
  }

  auto [list, is_new] = ensure_list_storage(library_reference, *filesel_type);
  if (is_new || list.needsRefetch()) {
    list.setup();
    list.fetch(C);
  }
}

void AssetListStorage::destruct()
{
  global_storage().~AssetListMap();
}

AssetList *AssetListStorage::lookup_list(const AssetLibRef &lib_ref)
{
  return global_storage().lookup_ptr(lib_ref);
}

void AssetListStorage::tagMainDataDirty()
{
  for (AssetList &list : global_storage().values()) {
    list.tagMainDataDirty();
  }
}

void AssetListStorage::remapID(ID *id_new, ID *id_old)
{
  for (AssetList &list : global_storage().values()) {
    list.remapID(id_new, id_old);
  }
}

std::optional<eFileSelectType> AssetListStorage::asset_library_reference_to_fileselect_type(
    const AssetLibRef &lib_ref)
{
  switch (library_reference.type) {
    case ASSET_LIBRARY_CUSTOM:
      return FILE_ASSET_LIBRARY;
    case ASSET_LIBRARY_LOCAL:
      return FILE_MAIN_ASSET;
  }

  return std::nullopt;
}

std::tuple<AssetList &, AssetListStorage::is_new_t> AssetListStorage::ensure_list_storage(
    const AssetLibRef &lib_ref, eFileSelectType filesel_type)
{
  AssetListMap &storage = global_storage();

  if (AssetList *list = storage.lookup_ptr(library_reference)) {
    return {*list, false};
  }
  storage.add(lib_ref, AssetList(filesel_type, lib_ref));
  return {storage.lookup(lib_ref), true};
}

/**
 * Wrapper for Construct on First Use idiom, to avoid the Static Initialization Fiasco.
 */
AssetListStorage::AssetListMap &AssetListStorage::global_storage()
{
  static AssetListMap global_storage_;
  return global_storage_;
}

}  // namespace dune::ed::asset

/* -------------------------------------------------------------------- */
/** C-API */

using namespace dune::ed::asset;

void ED_assetlist_storage_fetch(const AssetLibRef *lib_ref, const duneContext *C)
{
  AssetListStorage::fetch_lib(*lib_ref, *C);
}

void ED_assetlist_ensure_previews_job(const AssetLibRef *lib_ref, duneContext *C)
{

  AssetList *list = AssetListStorage::lookup_list(*lib_ref);
  if (list) {
    list->ensurePreviewsJob(C);
  }
}

void ED_assetlist_clear(const AssetLibRef *libref, duneContext *C)
{
  AssetList *list = AssetListStorage::lookup_list(*libref);
  if (list) {
    list->clear(C);
  }
}

bool ED_assetlist_storage_has_list_for_lib(const AssetLibraryRef *lib_ref)
{
  return AssetListStorage::lookup_list(*lib_ref) != nullptr;
}

void ED_assetlist_iterate(const AssetLibRef &lib_ref, AssetListIterFn fn)
{
  AssetList *list = AssetListStorage::lookup_list(lib_ref);
  if (list) {
    list->iterate(fn);
  }
}

/* TODO hack to use the File Browser path, so we can keep all the import logic handled by the asset
 * API. Get rid of this once the File Browser is integrated better with the asset list. */
static const char *assetlist_lib_path_from_sfile_get_hack(const duneContext *C)
{
  SpaceFile *sfile = ctx_wm_space_file(C);
  if (!sfile || !ED_fileselect_is_asset_browser(sfile)) {
    return nullptr;
  }

  FileAssetSelectParams *asset_select_params = ED_fileselect_get_asset_params(sfile);
  if (!asset_select_params) {
    return nullptr;
  }

  return filelist_dir(sfile->files);
}

std::string ED_assetlist_asset_filepath_get(const bContext *C,
                                            const AssetLibraryReference &library_reference,
                                            const AssetHandle &asset_handle)
{
  if (ED_asset_handle_get_local_id(&asset_handle) ||
      !ED_asset_handle_get_metadata(&asset_handle)) {
    return {};
  }
  const char *lib_path = ED_assetlist_lib_path(&libref);
  if (!lib_path && C) {
    lib_path = assetlist_lib_path_from_sfile_get_hack(C);
  }
  if (!lib_path) {
    return {};
  }
  const char *asset_relpath = asset_handle.file_data->relpath;

  char path[FILE_MAX_LIBEXTRA];
  LIB_join_dirfile(path, sizeof(path), library_path, asset_relpath);

  return path;
}

ImBuf *ED_assetlist_asset_image_get(const AssetHandle *asset_handle)
{
  ImBuf *imbuf = filelist_file_getimage(asset_handle->file_data);
  if (imbuf) {
    return imbuf;
  }

  return filelist_geticon_image_ex(asset_handle->file_data);
}

const char *ED_assetlist_lib_path(const AssetLibRef *library_reference)
{
  AssetList *list = AssetListStorage::lookup_list(*library_reference);
  if (list) {
    return list->filepath().data();
  }
  return nullptr;
}

bool ED_assetlist_listen(const AssetLibRef *libref,
                         const wmNotifier *notifier)
{
  AssetList *list = AssetListStorage::lookup_list(*libref);
  if (list) {
    return list->listen(*notifier);
  }
  return false;
}

int ED_assetlist_size(const AssetLibRef *libref)
{
  AssetList *list = AssetListStorage::lookup_list(*libref);
  if (list) {
    return list->size();
  }
  return -1;
}

void ED_assetlist_storage_tag_main_data_dirty()
{
  AssetListStorage::tagMainDataDirty();
}

void ED_assetlist_storage_id_remap(ID *id_old, ID *id_new)
{
  AssetListStorage::remapID(id_old, id_new);
}

void ED_assetlist_storage_exit()
{
  AssetListStorage::destruct();
}
