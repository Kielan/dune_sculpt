#pragma once

#include "mem_guardedalloc.h"

#include "intern/graph_type.h"

#include "api_access.h"

struct Id;
struct ApiPtr;
struct ApiProp;

namespace dune {
namespace graph {

class GraphBuilderCache;

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

  MEM_CXX_CLASS_ALLOC_FNS("AnimatedPropId");
};

class AnimatedPropStorage {
 public:
  AnimatedPropStorage();

  void initializeFromId(GraphBuilderCache *builder_cache, Id *id);

  void tagPropAsAnimated(const AnimatedPropId &prop_id);
  void tagPropAsAnimated(const ApiPtr *ptr_api, const ApiProp *prop_api);

  bool isPropAnimated(const AnimatedPropId &prop_id);
  bool isPropAnimated(const ApiPtr *ptr_api, const PropApi *prop_api);

  bool isAnyPropAnimated(const ApiPtr *ptr_api);

  /* The storage is fully initialized from all F-Curves from corresponding ID. */
  bool is_fully_initialized;

  /* indexed by ApiPtr.data. */
  Set<void *> animated_objects_set;
  Set<AnimatedPropId> animated_prop_set;

  MEM_CXX_CLASS_ALLOC_FNS("AnimatedPropStorage");
};

/* Cached data which can be re-used by multiple builders. */
class GraphBuilderCache {
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

  Map<Id *, AnimatedPropStorage *> animated_prop_storage_map_;

  MEM_CXX_CLASS_ALLOC_FUNCS("GraphBuilderCache");
};

}  // namespace graph
}  // namespace dune
