#pragma once

#include "lib_compiler_attrs.h"
#include "lib_utildefines.h"

#include "types_asset.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AssetLibRef;
struct AssetMetaData;
struct DataReader;
struct Writer;
struct Id;
struct IdProp;
struct PreviewImage;

typedef void (*PreSaveFn)(void *asset_ptr, struct AssetMetaData *asset_data);

typedef struct AssetTypeInfo {
  /* For local assets (assets in the current .blend file), a callback to execute before the file is
   * saved.  */
  PreSaveFn pre_save_fn;
} AssetTypeInfo;

struct AssetMetaData *asset_metadata_create(void);
void asset_metadata_free(struct AssetMetaData **asset_data);

struct AssetTagEnsureResult {
  struct AssetTag *tag;
  /* Set to false if a tag of this name was already present. */
  bool is_new;
};

struct AssetTag *asset_metadata_tag_add(struct AssetMetaData *asset_data, const char *name);
/* Make sure there is a tag with name name, create one if needed. */
struct AssetTagEnsureResult asset_metadata_tag_ensure(struct AssetMetaData *asset_data,
                                                      const char *name);
void asset_metadata_tag_remove(struct AssetMetaData *asset_data, struct AssetTag *tag);

/* Clean up the catalog Id (white-spaces removed, length reduced, etc.) and assign it. */
void asset_metadata_catalog_id_clear(struct AssetMetaData *asset_data);
void asset_metadata_catalog_id_set(struct AssetMetaData *asset_data,
                                   UUID catalog_id,
                                   const char *catalog_simple_name);

void asset_lib_ref_init_default(struct AssetLibRef *lib_ref);

void asset_metadata_idprop_ensure(struct AssetMetaData *asset_data, struct IdProp *prop);
struct IdProp *asset_metadata_idprop_find(const struct AssetMetaData *asset_data,
                                          const char *name) ATTR_WARN_UNUSED_RESULT;

struct PreviewImage *asset_metadata_preview_get_from_id(const struct AssetMetaData *asset_data,
                                                        const struct Id *owner_id);

void asset_metadata_write(struct Writer *writer, struct AssetMetaData *asset_data);
void asset_metadata_read(struct DataReader *reader, struct AssetMetaData *asset_data);

#ifdef __cplusplus
}
#endif
