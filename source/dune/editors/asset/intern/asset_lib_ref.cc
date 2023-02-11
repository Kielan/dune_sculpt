#include "lib_hash.hh"

#include "asset_lib_ref.hh"

namespace dune::ed::asset {

AssetLibRefWrapper::AssetLibRefWrapper(const AssetLibRef &reference)
    : AssetLibRef(reference)
{
}

bool operator==(const AssetLibRefWrapper &a, const AssetLibRefWrapper &b)
{
  return (a.type == b.type) &&
         ((a.type == ASSET_LIBRARY_CUSTOM) ? (a.custom_lib_index == b.custom_lib_index) :
                                             true);
}

uint64_t AssetLibRefWrapper::hash() const
{
  uint64_t hash1 = DefaultHash<decltype(type)>{}(type);
  if (type != ASSET_LIBRARY_CUSTOM) {
    return hash1;
  }

  uint64_t hash2 = DefaultHash<decltype(custom_lib_index)>{}(custom_lib_index);
  return hash1 ^ (hash2 * 33); /* Copied from DefaultHash for std::pair. */
}

}  // namespace dune::ed::asset
