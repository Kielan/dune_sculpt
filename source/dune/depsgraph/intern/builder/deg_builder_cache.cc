#include "intern/builder/deg_builder_cache.h"

#include "MEM_guardedalloc.h"

#include "types_anim.h"

#include "lib_utildefines.h"

#include "dune_animsys.h"

namespace dune::deg {

/* Animated property storage. */

AnimatedPropID::AnimatedPropID() : data(nullptr), prop_api(nullptr)
{
}

AnimatedPropId::AnimatedPropId(const ApiPtr *ptr_api,
                               const ApiProp *prop_api)
    : AnimatedPropId(*ptr_api, prop_api)
{
}

AnimatedPropId::AnimatedPropId(const ApiPtr &ptr_api,
                               const ApiProp *prop_api)
    : data(ptr_api.data), prop_api(prop_api)
{
}

AnimatedPropId::AnimatedPropId(Id *id, ApiStruct *type, const char *prop_name)
    : data(id)
{
  prop_api = api_struct_type_find_prop(type, prop_name);
}

AnimatedPropId::AnimatedPropId(Id * /*id*/,
                                       ApiStruct *type,
                                       void *data,
                                       const char *prop_name)
    : data(data)
{
  property_api = api_struct_type_find_prop(type, prop_name);
}

bool op==(const AnimatedPropId &a, const AnimatedPropId &b)
{
  return a.data == b.data && a.prop_api == b.prop_api;
}

uint64_t AnimatedPropId::hash() const
{
  uintptr_t ptr1 = (uintptr_t)data;
  uintptr_t ptr2 = (uintptr_t)prop_api;
  return static_cast<uint64_t>(((ptr1 >> 4) * 33) ^ (ptr2 >> 4));
}

namespace {

struct AnimatedPropCbData {
  ApiPtr ptr_api;
  AnimatedPropStorage *animated_prop_storage;
  DGraphBuilderCache *builder_cache;
};

void animated_prop_cb(Id * /*id*/, FCurve *fcurve, void *data_v)
{
  if (fcurve->api_path == nullptr || fcurve->api_path[0] == '\0') {
    return;
  }
  AnimatedPropCbData *data = static_cast<AnimatedPropCbData *>(data_v);
  /* Resolve property. */
  ApiPtr ptr_api;
  ApiProp *prop_api = nullptr;
  if (!api_path_resolve_prop(
          &data->ptr_api, fcurve->api_path, &ptr_api, &prop_api)) {
    return;
  }
  /* Get storage for the ID.
   * This is needed to deal with cases when nested datablock is animated by its parent. */
  AnimatedPropStorage *animated_property_storage = data->animated_prop_storage;
  if (ptr_api.owner_id != data->ptr_api.owner_id) {
    animated_prop_storage = data->builder_cache->ensureAnimatedPropStorage(
        ptr_api.owner_id);
  }
  /* Set the property as animated. */
  animated_prop_storage->tagPropAsAnimated(&ptr_api, prop_api);
}

}  // namespace

AnimatedPropStorage::AnimatedPropStorage() : is_fully_initialized(false)
{
}

void AnimatedPropStorage::initializeFromId(DGraphBuilderCache *builder_cache, Id *id)
{
  AnimatedPropCbData data;
  api_id_ptr_create(id, &data.ptr_api);
  data.animated_prop_storage = this;
  data.builder_cache = builder_cache;
  dune_fcurves_id_cb(id, animated_prop_cb, &data);
}

void AnimatedPropStorage::tagPropAsAnimated(const AnimatedPropId &prop_id)
{
  animated_objects_set.add(prop_id.data);
  animated_props_set.add(prop_id);
}

void AnimatedPropStorage::tagPropAsAnimated(const ApiPtr *ptr_api,
                                                    const ApiProp *prop_api)
{
  tagPropAsAnimated(AnimatedPropId(ptr_api, prop_api));
}

bool AnimatedPropStorage::isPropAnimated(const AnimatedPropId &prop_id)
{
  return animated_props_set.contains(prop_id);
}

bool AnimatedPropStorage::isPropAnimated(const ApiPtr *ptr_api,
                                         const ApiProp *prop_api)
{
  return isPropAnimated(AnimatedPropId(ptr_api, prop_api));
}

bool AnimatedPropStorage::isAnyPropAnimated(const ApiPtr *ptr_api)
{
  return animated_objects_set.contains(ptr_api->data);
}

/* Builder cache itself. */

DGraphBuilderCache::~DGraphBuilderCache()
{
  for (AnimatedPropStorage *animated_prop_storage :
       animated_prop_storage_map_.values()) {
    delete animated_prop_storage;
  }
}

AnimatedPropStorage *DGraphBuilderCache::ensureAnimatedPropStorage(Id *id)
{
  return animated_prop_storage_map_.lookup_or_add_cb(
      id, []() { return new AnimatedPropStorage(); });
}

AnimatedPropStorage *DGraphBuilderCache::ensureInitializedAnimatedPropStorage(Id *id)
{
  AnimatedPropStorage *animated_prop_storage = ensureAnimatedPropStorage(id);
  if (!animated_prop_storage->is_fully_initialized) {
    animated_prop_storage->initializeFromId(this, id);
    animated_prop_storage->is_fully_initialized = true;
  }
  return animated_prop_storage;
}

}  // namespace dune::deg
