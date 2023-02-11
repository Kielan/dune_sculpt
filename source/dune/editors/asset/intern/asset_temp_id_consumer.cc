/**
 * API for temporary loading of asset IDs.
 * Uses the `loader_lib_temp_xxx()` API internally.
 */

#include <new>

#include "types_space.h"

#include "dune_report.h"

#include "lib_utility_mixins.hh"

#include "loader_readfile.h"

#include "MEM_guardedalloc.h"

#include "ED_asset_handle.h"
#include "ED_asset_tmp_id_consumer.h"

using namespace dune;

class AssetTemporaryIDConsumer : NonCopyable, NonMovable {
  const AssetHandle &handle_;
  TmpLibCtx *tmp_lib_ctx = nullptr;

 public:
  AssetTemporaryIDConsumer(const AssetHandle &handle) : handle_(handle)
  {
  }
  ~AssetTemporaryIDConsumer()
  {
    if (tmp_lib_ctx_) {
      loader_lib_tmp_free(tmp_lib_ctx);
    }
  }

  ID *get_local_id()
  {
    return ED_asset_handle_get_local_id(&handle_);
  }

  ID *import_id(const dContext *C,
                const AssetLibRef &asset_lib_ref,
                ID_Type id_type,
                Main &dmain,
                ReportList &reports)
  {
    const char *asset_name = ED_asset_handle_get_name(&handle_);
    char dune_file_path[FILE_MAX_LIBEXTRA];
    ED_asset_handle_get_full_lib_path(C, &asset_lib_ref, &handle_, dune_file_path);

    tmp_lib_ctx_ = loader_lib_tmp_load_id(
        &bmain, dune_file_path, id_type, asset_name, &reports);

    if (tmp_lib_ctx_ == nullptr || temp_lib_ctx_->temp_id == nullptr) {
      dune_reportf(&reports, RPT_ERROR, "Unable to load %s from %s", asset_name, dune_file_path);
      return nullptr;
    }

    LIB_assert(GS(tmp_lib_ctx_->temp_id->name) == id_type);
    return tmp_lib_ctx_->temp_id;
  }
};

AssetTempIDConsumer *ED_asset_temp_id_consumer_create(const AssetHandle *handle)
{
  if (!handle) {
    return nullptr;
  }
 LIB_assert(handle->file_data->asset_data != nullptr);
  return reinterpret_cast<AssetTempIDConsumer *>(
      MEM_new<AssetTemporaryIDConsumer>(__func__, *handle));
}

void ED_asset_temp_id_consumer_free(AssetTempIDConsumer **consumer)
{
  MEM_delete(reinterpret_cast<AssetTemporaryIDConsumer *>(*consumer));
  *consumer = nullptr;
}

ID *ED_asset_tmp_id_consumer_ensure_local_id(AssetTempIDConsumer *consumer_,
                                              const duneContext *C,
                                              const AssetLibRef *asset_lib_ref,
                                              ID_Type id_type,
                                              Main *dmain,
                                              ReportList *reports)
{
  if (!(consumer_ && asset_lib_ref && dmain && reports)) {
    return nullptr;
  }
  AssetTemporaryIDConsumer *consumer = reinterpret_cast<AssetTemporaryIDConsumer *>(consumer_);

  if (ID *local_id = consumer->get_local_id()) {
    return local_id;
  }
  return consumer->import_id(C, *asset_library_ref, id_type, *bmain, *reports);
}
