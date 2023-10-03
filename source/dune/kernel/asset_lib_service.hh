#pragma once

#ifndef __cplusplus
#  error This is a C++-only header file.
#endif

#include "dune_asset_lib.hh"

#include "lib_map.hh"

#include <memory>

namespace dune {

/* Global singleton-ish that provides access to individual AssetLib instances.
 *
 * Whenever a dune file is loaded, the existing instance of AssetLibService is destructed, and
 * a new one is created -- hence the "singleton-ish". This ensures only information about relevant
 * asset libs is loaded.
 *
 * How Asset libs are identified may change in the future.
 *  For now they are assumed to be:
 * - on disk (identified by the absolute directory), or
 * - the "current file" lib (which is in memory but could have catalogs
 *   loaded from a file on disk) */
class AssetLibService {
 public:
  using AssetLibPtr = std::unique_ptr<AssetLib>;

  AssetLibService() = default;
  ~AssetLibService() = default;

  /* Return the AssetLibService singleton, allocating it if necessary. */
  static AssetLibService *get();

  /* Destroy the AssetLibService singleton. It will be reallocated by get() if necessary. */
  static void destroy();

  /* Get the given asset lib. Opens it (i.e. creates a new AssetLib instance) if necessary. */
  AssetLib *get_asset_lib_on_disk(StringRefNull top_level_directory);

  /* Get the "Current File" asset lib. */
  AssetLib *get_asset_lib_current_file();

  /* Returns whether there are any known asset libs with unsaved catalog edits. */
  bool has_any_unsaved_catalogs() const;

 protected:
  static std::unique_ptr<AssetLibService> instance_;

  /* Mapping absolute path of the lib's top-level directory to the AssetLibrary instance. */
  Map<std::string, AssetLibPtr> on_disk_libs_;
  AssetLibPtr current_file_lib_;

  /* Handlers for managing the life cycle of the AssetLibService instance. */
  CbFnStore on_load_cb_store_;
  static bool atexit_handler_registered_;

  /* Allocate a new instance of the service and assign it to `instance_`. */
  static void allocate_service_instance();

  /* Ensure the AssetLibService instance is destroyed before a new dune file is loaded.
   * This makes memory management simple, and ensures a fresh start for every blend file. */
  void app_handler_register();
  void app_handler_unregister();
};
