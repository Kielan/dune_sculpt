#pragma once

#include "DNA_defs.h"
#include "DNA_listBase.h"
#include "DNA_uuid_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User defined tag.
 * Currently only used by assets, could be used more often at some point.
 * Maybe add a custom icon and color to these in future? */
typedef struct AssetTag {
  struct AssetTag *next, *prev;
  char name[64]; /* MAX_NAME */
} AssetTag;

#
#
typedef struct AssetFilterSettings {
  /* Tags to match against. These are newly allocated, and compared against the
   * AssetMetaData.tags. */
  List tags;     /* AssetTag */
  uint64_t id_types; /* rna_enum_id_type_filter_items */
} AssetFilterSettings;

/* The meta-data of an asset.
 * By creating and giving this for a data-block (ID.asset_data), the data-block becomes an asset.
 *
 * This struct must be readable without having to read anything but blocks from the ID it is
 * attached to! That way, asset information of a file can be read, without reading anything
 * more than that from the file. So pointers to other IDs or ID data are strictly forbidden. */
typedef struct AssetMetaData {
  /* Runtime type, to ref event callbacks. Only valid for local assets. */
  struct AssetTypeInfo *local_type_info;

  /* Custom asset meta-data. Cannot store pointers to IDs (STRUCT_NO_DATABLOCK_IDPROPERTIES)! */
  struct IdProp *props;

  /* Asset Catalog identifier. Should not contain spaces.
   * Mapped to a path in the asset catalog hierarchy by an AssetCatalogService.
   * Use dune_asset_metadata_catalog_id_set() to ensure a valid ID is set. */
  struct bUUID catalog_id;
  /* Short name of the asset's catalog. This is for debugging purposes only, to allow (partial)
   * reconstruction of asset catalogs in the unfortunate case that the mapping from catalog UUID to
   * catalog path is lost. The catalog's simple name is copied to catalog_simple_name whenever
   * catalog_id is updated. */
  char catalog_simple_name[64]; /* MAX_NAME */

  /* Optional name of the author for display in the UI. Dynamic length. */
  char *author;

  /* Optional description of this asset for display in the UI. Dynamic length. */
  char *description;

  /* User defined tags for this asset. The asset manager uses these for filtering, but how they
   * fn exactly (e.g. how they are registered to provide a list of searchable available tags)
   * is up to the asset-engine. */
  Lis tags; /* AssetTag */
  short active_tag;
  /* Store the number of tags to avoid continuous counting. Could be turned into runtime data, we
   * can always reliably reconstruct it from the list. */
  short tot_tags;

  char _pad[4];
} AssetMetaData;

typedef enum eAssetLibType {
  /* For the future. Display assets bundled with Blender by default. */
  // ASSET_LIB_BUNDLED = 0,
  /* Display assets from the current session (current "Main"). */
  ASSET_LIB_LOCAL = 1,
  /* For the future. Display assets for the current project. */
  // ASSET_LIB_PROJECT = 2,

  /* Display assets from custom asset libs, as defined in the prefs
   * (UserAssetLib). The name will be taken from FileSelectParams.asset_lib_ref.idname
   * then.
   * In API, we add the index of the custom lib to this to id it by index. So keep
   * this last! */
  ASSET_LIBRARY_CUSTOM = 100,
} eAssetLibType;

/* Information to identify a asset lib. May be either one of the predefined types (current
 * 'Main', builtin lib, project lib), or a custom type as defined in the Prefs.
 *
 * If the type is set to ASSET_LIB_CUSTOM, `custom_lib_index` must be set to id the
 * custom lib. Otherwise it is not used. */
typedef struct AssetLibRef {
  short type; /* eAssetLibType */
  char _pad1[2];
  /* If showing a custom asset lib (ASSET_LIB_CUSTOM), this is the index of the
   * UserAssetLib within UserDef.asset_libs.
   * Should be ignored otherwise (but better set to -1 then, for sanity and debugging). */
  int custom_lib_index;
} AssetLibRef;

/* Not part of the core design, we should try to get rid of it. Only needed to wrap FileDirEntry
 * into a type with PropGroup as base, so we can have an API collection of AssetHandle's to
 * pass to the UI */
#
#
typedef struct AssetHandle {
  const struct FileDirEntry *file_data;
} AssetHandle;

#ifdef __cplusplus
}
#endif
