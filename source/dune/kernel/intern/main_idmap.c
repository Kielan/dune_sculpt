#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "LIB_ghash.h"
#include "LIB_listbase.h"
#include "LIB_mempool.h"
#include "LIB_utildefines.h"

#include "types_id.h"

#include "dune_idtype.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_main_idmap.h" /* own include */

/* Util fns for faster Id lookups. */

/** dune_main_idmap API
 *
 * Cache Id (name, lib lookups).
 * This doesn't account for adding/removing data-blocks,
 * and should only be used when performing many lookups.
 *
 * GHash's are initialized on demand,
 * since its likely some types will never have lookups run on them,
 * so its a waste to create and never use. */

struct IdNameLib_Key {
  /* `Id.name + 2`: without the Id type prefix, since each id type gets its own 'map'. */
  const char *name;
  /* `Id.lib`: */
  const Lib *lib;
};

struct IDNameLib_TypeMap {
  GHash *map;
  short id_type;
};

/* Opaque structure, external API users only see this. */
struct IdNameLib_Map {
  struct IdNameLib_TypeMap type_maps[INDEX_ID_MAX];
  struct GHash *uuid_map;
  struct Main *bmain;
  struct GSet *valid_id_ptrs;
  int idmap_types;

  /* For storage of keys for the TypeMap ghash, avoids many single allocs. */
  lib_mempool *type_maps_keys_pool;
};

static struct IdNameLib_TypeMap *main_idmap_from_idcode(struct IdNameLib_Map *id_map,
                                                        short id_type)
{
  if (id_map->idmap_types & MAIN_IDMAP_TYPE_NAME) {
    for (int i = 0; i < INDEX_ID_MAX; i++) {
      if (id_map->type_maps[i].id_type == id_type) {
        return &id_map->type_maps[i];
      }
    }
  }
  return NULL;
}

struct IdNameLibMap *dune_main_idmap_create(struct Main *main,
                                            const bool create_valid_ids_set,
                                            struct Main *old_main,
                                            const int idmap_types)
{
  struct IdNameLibMap *id_map = mem_mallocn(sizeof(*id_map), __func__);
  id_map->main = main;
  id_map->idmap_types = idmap_types;

  int index = 0;
  while (index < INDEX_ID_MAX) {
    struct IdNameLib_TypeMap *type_map = &id_map->type_maps[index];
    type_map->map = NULL;
    type_map->id_type = dune_idtype_idcode_iter_step(&index);
    lib_assert(type_map->id_type != 0);
  }
  lib_assert(index == INDEX_ID_MAX);
  id_map->type_maps_keys_pool = NULL;

  if (idmap_types & MAIN_IDMAP_TYPE_UUID) {
    ID *id;
    id_map->uuid_map = lib_ghash_int_new(__func__);
    FOREACH_MAIN_ID_BEGIN (main, id) {
      lib_assert(id->session_uuid != MAIN_ID_SESSION_UUID_UNSET);
      void **id_ptr_v;
      const bool existing_key = lib_ghash_ensure_p(
          id_map->uuid_map, PTR_FROM_UINT(id->session_uuid), &id_ptr_v);
      lib_assert(existing_key == false);
      UNUSED_VARS_NDEBUG(existing_key);

      *id_ptr_v = id;
    }
    FOREACH_MAIN_ID_END;
  }
  else {
    id_map->uuid_map = NULL;
  }

  if (create_valid_ids_set) {
    id_map->valid_id_ptrs = dune_main_gset_create(main, NULL);
    if (old_main != NULL) {
      id_map->valid_id_ptrs = dune_main_gset_create(old_main, id_map->valid_id_ptrs);
    }
  }
  else {
    id_map->valid_id_ptrs = NULL;
  }

  return id_map;
}

void dune_main_idmap_insert_id(struct IDNameLib_Map *id_map, ID *id)
{
  if (id_map->idmap_types & MAIN_IDMAP_TYPE_NAME) {
    const short id_type = GS(id->name);
    struct IDNameLib_TypeMap *type_map = main_idmap_from_idcode(id_map, id_type);

    /* No need to do anything if map has not been lazily created yet. */
    if (LIKELY(type_map != NULL) && type_map->map != NULL) {
      LIB_assert(id_map->type_maps_keys_pool != NULL);

      struct IDNameLib_Key *key = lib_mempool_alloc(id_map->type_maps_keys_pool);
      key->name = id->name + 2;
      key->lib = id->lib;
      lib_ghash_insert(type_map->map, key, id);
    }
  }

  if (id_map->idmap_types & MAIN_IDMAP_TYPE_UUID) {
    lib_assert(id_map->uuid_map != NULL);
    lib_assert(id->session_uuid != MAIN_ID_SESSION_UUID_UNSET);
    void **id_ptr_v;
    const bool existing_key = lib_ghash_ensure_p(
        id_map->uuid_map, PTR_FROM_UINT(id->session_uuid), &id_ptr_v);
    lib_assert(existing_key == false);
    UNUSED_VARS_NDEBUG(existing_key);

    *id_ptr_v = id;
  }
}

void dune_main_idmap_remove_id(struct IdNameLib_Map *id_map, Id *id)
{
  if (id_map->idmap_types & MAIN_IDMAP_TYPE_NAME) {
    const short id_type = GS(id->name);
    struct IDNameLib_TypeMap *type_map = main_idmap_from_idcode(id_map, id_type);

    /* No need to do anything if map has not been lazily created yet. */
    if (LIKELY(type_map != NULL) && type_map->map != NULL) {
      lib_assert(id_map->type_maps_keys_pool != NULL);

      /* NOTE: We cannot free the key from the MemPool here, would need new API from GHash to also
       * retrieve key pointer. Not a big deal for now */
      lib_ghash_remove(type_map->map, &(struct IdNameLibKey){id->name + 2, id->lib}, NULL, NULL);
    }
  }

  if (id_map->idmap_types & MAIN_IDMAP_TYPE_UUID) {
    lib_assert(id_map->uuid_map != NULL);
    lib_assert(id->session_uuid != MAIN_ID_SESSION_UUID_UNSET);

    lib_ghash_remove(id_map->uuid_map, PTR_FROM_UINT(id->session_uuid), NULL, NULL);
  }
}

struct Main *dune_main_idmap_main_get(struct IdNameLib_Map *id_map)
{
  return id_map->bmain;
}

static unsigned int idkey_hash(const void *ptr)
{
  const struct IdNameLib_Key *idkey = ptr;
  unsigned int key = lib_ghashutil_strhash(idkey->name);
  if (idkey->lib) {
    key ^= lib_ghashutil_ptrhash(idkey->lib);
  }
  return key;
}

static bool idkey_cmp(const void *a, const void *b)
{
  const struct IdNameLib_Key *idkey_a = a;
  const struct IdNameLib_Key *idkey_b = b;
  return !STREQ(idkey_a->name, idkey_b->name) || (idkey_a->lib != idkey_b->lib);
}

Id *dune_main_idmap_lookup_name(struct IdNameLib_Map *id_map,
                               short id_type,
                               const char *name,
                               const Library *lib)
{
  struct IdNameLibTypeMap *type_map = main_idmap_from_idcode(id_map, id_type);

  if (UNLIKELY(type_map == NULL)) {
    return NULL;
  }

  /* Lazy init. */
  if (type_map->map == NULL) {
    if (id_map->type_maps_keys_pool == NULL) {
      id_map->type_maps_keys_pool = lib_mempool_create(
          sizeof(struct IdNameLibKey), 1024, 1024, LIB_MEMPOOL_NOP);
    }

    GHash *map = type_map->map = lib_ghash_new(idkey_hash, idkey_cmp, __func__);
    List *lb = which_lib(id_map->main, id_type);
    for (Id *id = lb->first; id; id = id->next) {
      struct IdNameLib_Key *key = lib_mempool_alloc(id_map->type_maps_keys_pool);
      key->name = id->name + 2;
      key->lib = id->lib;
      lib_ghash_insert(map, key, id);
    }
  }

  const struct IdNameLibKey key_lookup = {name, lib};
  return lib_ghash_lookup(type_map->map, &key_lookup);
}

Id *dune_main_idmap_lookup_id(struct IdNameLibMap *id_map, const Id *id)
{
  /* When used during undo/redo, this function cannot assume that given id points to valid memory
   * (i.e. has not been freed),
   * so it has to check that it does exist in 'old' (aka current) Main database.
   * Otherwise, we cannot provide new Id ptr that way (would crash accessing freed memory
   * when trying to get Id name) */
  if (id_map->valid_id_ptrs == NULL || lib_gset_haskey(id_map->valid_id_ptrs, id)) {
    return dune_main_idmap_lookup_name(id_map, GS(id->name), id->name + 2, id->lib);
  }
  return NULL;
}

Id dune_main_idmap_lookup_uuid(struct IdNameLibMap *id_map, const uint session_uuid)
{
  if (id_map->idmap_types & MAIN_IDMAP_TYPE_UUID) {
    return lib_ghash_lookup(id_map->uuid_map, PTR_FROM_UINT(session_uuid));
  }
  return NULL;
}

void dune_main_idmap_destroy(struct IdNameLibMap *id_map)
{
  if (id_map->idmap_types & MAIN_IDMAP_TYPE_NAME) {
    struct IdNameLibTypeMap *type_map = id_map->type_maps;
    for (int i = 0; i < INDEX_ID_MAX; i++, type_map++) {
      if (type_map->map) {
        lib_ghash_free(type_map->map, NULL, NULL);
        type_map->map = NULL;
      }
    }
    if (id_map->type_maps_keys_pool != NULL) {
      lib_mempool_destroy(id_map->type_maps_keys_pool);
      id_map->type_maps_keys_pool = NULL;
    }
  }
  if (id_map->idmap_types & MAIN_IDMAP_TYPE_UUID) {
    lib_ghash_free(id_map->uuid_map, NULL, NULL);
  }

  lib_assert(id_map->type_maps_keys_pool == NULL);

  if (id_map->valid_id_ptrs != NULL) {
    lib_gset_free(id_map->valid_id_ptrs, NULL);
  }

  mem_freen(id_map);
}
