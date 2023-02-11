
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct AssetFilterSettings;
struct AssetHandle;
struct AssetLibRef;
struct ID;
struct duneContext;
struct wmNotifier;

/**
 * Invoke asset list reading, potentially in a parallel job. Won't wait until the job is done,
 * and may return earlier.
 */
void ED_assetlist_storage_fetch(const struct AssetLibRef *lib_ref,
                                const struct bContext *C);
void ED_assetlist_ensure_previews_job(const struct AssetLibRef *lib_ref,
                                      struct duneContext *C);
void ED_assetlist_clear(const struct AssetLibRef *lib_ref, struct duneContext *C);
bool ED_assetlist_storage_has_list_for_lib(const AssetLibRef *lib_ref);
/**
 * Tag all asset lists in the storage that show main data as needing an update (re-fetch).
 *
 * This only tags the data. If the asset list is visible on screen, the space is still responsible
 * for ensuring the necessary redraw. It can use #ED_assetlist_listen() to check if the asset-list
 * needs a redraw for a given notifier.
 */
void ED_assetlist_storage_tag_main_data_dirty(void);
/**
 * Remapping of ID pointers within the asset lists. Typically called when an ID is deleted to clear
 * all references to it (a id_new is null then).
 */
void ED_assetlist_storage_id_remap(struct ID *id_old, struct ID *id_new);
/**
 * Can't wait for static deallocation to run. There's nested data allocated with our guarded
 * allocator, it will complain about unfreed memory on exit.
 */
void ED_assetlist_storage_exit(void);

struct ImBuf *ED_assetlist_asset_image_get(const AssetHandle *asset_handle);
const char *ED_assetlist_lib_path(const struct AssetLibRef *lib_ref);

/**
 * return True if the region needs a UI redraw.
 */
bool ED_assetlist_listen(const struct AssetLibRef *lib_ref,
                         const struct wmNotifier *notifier);
/**
 * return The number of assets stored in the asset list for a lib_ref, or -1 if there
 *         is no list fetched for it.
 */
int ED_assetlist_size(const struct AssetLibRef *lib_ref);

#ifdef __cplusplus
}
#endif
