#include "dune_cxt.h"

#include "ed_fileselect.hh"
#include "ed_screen.hh"

#include "api_prototypes.h"

#include "file_intern.hh"
#include "filelist.hh"

const char *file_cxt_dir[] = {
    "active_file",
    "selected_files",
    "asset_lib_ref",
    "selected_asset_files",
    "id",
    "selected_ids",
    nullptr,
};

int file_cxt(const Cxt *C,
            const char *member,
            CxtDataResult *result)
{
  Screen *screen = cxt_wm_screen(C);
  SpaceFile *sfile = cxt_wm_space_file(C);
  FileSelectParams *params = er_fileselect_get_active_params(sfile);

  lib_assert(!ed_area_is_global(cxt_wm_area(C)));

  if (cxt_data_dir(member)) {
    cxt_data_dir_set(result, file_cxt_dir);
    return CXT_RESULT_OK;
  }

  /* The following member checks return file-list data, check if that needs refreshing first. */
  if (file_main_region_needs_refresh_before_draw(sfile)) {
    return CXT_RESULT_NO_DATA;
  }

  if (cxt_data_equals(member, "active_file")) {
    FileDirEntry *file = filelist_file(sfile->files, params->active_file);
    if (file == nullptr) {
      return CXT_RESULT_NO_DATA;
    }

    cxt_data_ptr_set(result, &screen->id, &ApiFileSelectEntry, file);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "selected_files")) {
    const int num_files_filtered = filelist_files_ensure(sfile->files);

    for (int file_index = 0; file_index < num_files_filtered; file_index++) {
      if (filelist_entry_is_selected(sfile->files, file_index)) {
        FileDirEntry *entry = filelist_file(sfile->files, file_index);
        cxt_data_list_add(result, &screen->id, &ApiFileSelectEntry, entry);
      }
    }

    cxt_data_type_set(result, CXT_DATA_TYPE_COLLECTION);
    return CXT_RESULT_OK;
  }

  if (cxt_data_equals(member, "asset_lib_ref")) {
    FileAssetSelectParams *asset_params = ed_fileselect_get_asset_params(sfile);
    if (!asset_params) {
      return CXT_RESULT_NO_DATA;
    }

    cxt_data_ptr_set(
        result, &screen->id, &ApiAssetLibRef, &asset_params->asset_lib_ref);
    return CXT_RESULT_OK;
  }
  /* TODO temporary AssetHandle design: For now this returns the file entry. Would be better if it
   * was `"selected_assets"` and returned the assets (e.g. as `AssetHandle`) directly. See comment
   * for AssetHandle for more info. */
  if (cxt_data_equals(member, "selected_asset_files")) {
    const int num_files_filtered = filelist_files_ensure(sfile->files);

    for (int file_index = 0; file_index < num_files_filtered; file_index++) {
      if (filelist_entry_is_selected(sfile->files, file_index)) {
        FileDirEntry *entry = filelist_file(sfile->files, file_index);
        if (entry->asset) {
          cxt_data_list_add(result, &screen->id, &ApiFileSelectEntry, entry);
        }
      }
    }

    cxt_data_type_set(result, CXT_DATA_TYPE_COLLECTION);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "id")) {
    const FileDirEntry *file = filelist_file(sfile->files, params->active_file);
    if (file == nullptr) {
      return CXT_RESULT_NO_DATA;
    }

    Id *id = filelist_file_get_id(file);
    if (id == nullptr) {
      return CXT_RESULT_NO_DATA;
    }

    cxt_data_id_ptr_set(result, id);
    return CXT_RESULT_OK;
  }
  if (cxt_data_equals(member, "selected_ids")) {
    const int num_files_filtered = filelist_files_ensure(sfile->files);

    for (int file_index = 0; file_index < num_files_filtered; file_index++) {
      if (!filelist_entry_is_selected(sfile->files, file_index)) {
        continue;
      }
      Id *id = filelist_entry_get_id(sfile->files, file_index);
      if (!id) {
        continue;
      }

      cxt_data_id_list_add(result, id);
    }

    cxt_data_type_set(result, CXT_DATA_TYPE_COLLECTION);
    return CXT_RESULT_OK;
  }

  return CXT_RESULT_MEMBER_NOT_FOUND;
}
