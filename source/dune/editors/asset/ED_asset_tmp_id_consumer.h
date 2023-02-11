/**
 * API to abstract away details for temporary loading of an ID from an asset. If the ID is stored
 * in the current file (or more precisely, in the #Main given when requesting an ID) no loading is
 * performed and the ID is returned. Otherwise it's imported for temporary access using the
 * `loader_lib_tmp` API.
 */

#pragma once

#include "types_ID_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AssetTmpIDConsumer AssetTmpIDConsumer;

struct AssetHandle;
struct AssetLibRef;
struct Main;
struct ReportList;
struct duneContext;

AssetTmpIDConsumer *ED_asset_tmp_id_consumer_create(const struct AssetHandle *handle);
void ED_asset_temp_id_consumer_free(AssetTempIDConsumer **consumer);
struct ID *ED_asset_temp_id_consumer_ensure_local_id(
    AssetTempIDConsumer *consumer,
    const struct dubeContext *C,
    const struct AssetLibRef *asset_lib_ref,
    ID_Type id_type,
    struct Main *dunemain,
    struct ReportList *reports);

#ifdef __cplusplus
}
#endif
