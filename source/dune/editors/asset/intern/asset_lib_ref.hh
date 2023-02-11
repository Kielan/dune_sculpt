/** Utility to extend #AssetLibRef with C++ functionality (operators, hash function, etc). **/

#pragma once

#include <cstdint>

#include "types_asset.h"

namespace dune::ed::asset {

/**
 * Wrapper to add logic to the AssetLibRef types struct.
 */
class AssetLibRefWrapper : public AssetLibRef {
 public:
  /* Intentionally not `explicit`, allow implicit conversion for convenience. Might have to be
   * NOLINT */
  AssetLibRefWrapper(const AssetLibRef &reference);
  ~AssetLibRefWrapper() = default;

  friend bool operator==(const AssetLibRefWrapper &a,
                         const AssetLibRefWrapper &b);
  uint64_t hash() const;
};

}  // namespace dune::ed::asset
