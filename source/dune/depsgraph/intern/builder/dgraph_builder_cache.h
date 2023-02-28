#pragma once

#include "MEM_guardedalloc.h"

#include "intern/depsgraph_type.h"

#include "api_access.h"

struct Id;
struct ApiPtr;
struct ApiProp;

namespace dune {
namespace deg {

class DGraphBuilderCache;

/* Identifier for animated property. */
class AnimatedPropId {
 public:
  AnimatedPropId();
  AnimatedPropId(const ApiPtr *ptr_api, const ApiProp *prop_api);
  AnimatedPropId(const ApiPi &ptr_api, const ApiProp *prop_api);
  AnimatedPropId(Id *id, ApiStruct *type, const char *prop_name);
  AnimatedPropId(Id *id, ApiStruct *type, void *data, const char *prop_name);

  uint64_t hash() const;
  friend bool op==(const AnimatedPropId &a, const AnimatedPropId &b);

  /* Corresponds to ApiPtr.data. */
  void *data;
  const ApiProp *prop_api;

  MEM_CXX_CLASS_ALLOC_FUNCS("AnimatedPropertyID");
};

class AnimatedPropStorage {
 public:
  AnimatedPropStorage();

  void initializeFromID(DepsgraphBuilderCache *builder_cache, Id *id);

  void tagPropAsAnimated(const AnimatedPropId &prop_id);
  void tagPropAsAnimated(const ApiPtr *ptr_api, const ApiProp *prop_api);

  bool isPropertyAnimated(const AnimatedPropId &prop_id);
  bool isPropertyAnimated(const ApiPtr *ptr_api, const PropAPi *prop_api);

  bool isAnyPropAnimated(const ApiPtr *ptr_api);

  /* The storage is fully initialized from all F-Curves from corresponding ID. */
  bool is_fully_initialized;

  /* indexed by ApiPtr.data. */
  Set<void *> animated_objects_set;
  Set<AnimatedPropID> animated_prop_set;

  MEM_CXX_CLASS_ALLOC_FUNCS("AnimatedPropStorage");
};

/* Cached data which can be re-used by multiple builders. */
class DGraphBuilderCache {
 public:
  ~DGraphBuilderCache();

  /* Makes sure storage for animated properties exists and initialized for the given ID. */
  AnimatedPropStorage *ensureAnimatedPropStorage(Id *id);
  AnimatedPropStorage *ensureInitializedAnimatedPropStorage(Id *id);

  /* Shortcuts to go through ensureInitializedAnimatedPropStorage and its
   * isPropAnimated.
   *
   * NOTE: Avoid using for multiple subsequent lookups, query for the storage once, and then query
   * the storage.
   *
   * TODO: Technically, this makes this class something else than just a cache, but what is
   * the better name? */
  template<typename... Args> bool isPropAnimated(Id *id, Args... args)
  {
    AnimatedPropStorage *animated_prop_storage = ensureInitializedAnimatedPropStorage(
        id);
    return animated_prop_storage->isPropAnimated(args...);
  }

  bool isAnyPropAnimated(const ApiPtr *ptr)
  {
    AnimatedPropStorage *animated_prop_storage = ensureInitializedAnimatedPropStorage(
        ptr->owner_id);
    return animated_prop_storage->isAnyPropAnimated(ptr);
  }

  Map<ID *, AnimatedPropStorage *> animated_prop_storage_map_;

  MEM_CXX_CLASS_ALLOC_FUNCS("DGraphBuilderCache");
};

}  // namespace deg
}  // namespace dune
