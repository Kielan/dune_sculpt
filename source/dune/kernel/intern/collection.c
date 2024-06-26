/* Allow using deprecated functionality for .blend file I/O. */
#define TYPES_DEPRECATED_ALLOW

#include <string.h>

#include "lib_dune.h"
#include "lib_iter.h"
#include "lib_list.h"
#include "lib_math_base.h"
#include "lib_threads.h"
#include "lib.h"

#include "dune_anim_data.h"
#include "dune_collection.h"
#include "dune_icons.h"
#include "dune_idprop.h"
#include "dune_idtype.h"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_lib_query.h"
#include "dune_lib_remap.h"
#include "dune_main.h"
#include "dune_object.h"
#include "dune_rigidbody.h"
#include "dune_scene.h"

#include "types_defaults.h"

#include "types_id.h"
#include "types_collection.h"
#include "types_layer.h"
#include "types_object.h"
#include "types_rigidbody.h"
#include "types_scene.h"

#include "graph.h"
#include "graph_query.h"

#include "mem_guardedalloc.h"

#include "loader_read_write.h"

/* Prototypes */
static bool collection_child_add(Collection *parent,
                                 Collection *collection,
                                 const int flag,
                                 const bool add_us);
static bool collection_child_remove(Collection *parent, Collection *collection);
static bool collection_object_add(
    Main *main, Collection *collection, Object *ob, int flag, const bool add_us);
static bool collection_object_remove(Main *main,
                                     Collection *collection,
                                     Object *ob,
                                     const bool free_us);

static CollectionChild *collection_find_child(Collection *parent, Collection *collection);
static CollectionParent *collection_find_parent(Collection *child, Collection *collection);

static bool collection_find_child_recursive(const Collection *parent,
                                            const Collection *collection);

/* Collection Data-Block */

static void collection_init_data(Id *id)
{
  Collection *collection = (Collection *)id;
  lib_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(collection, id));

  MEMCPY_STRUCT_AFTER(collection, types_struct_default_get(Collection), id);
}

/* Only copy internal data of Collection Id from source
 * to already allocated/initialized destination.
 * You probably never want to use that directly,
 * use dune_id_copy or dune_id_copy_ex for typical needs.
 *
 * WARNING! This fn will not handle Id user count!
 *
 * param flag: Copying options (see dune_lib_id.h's LIB_ID_COPY_... flags for more) */
static void collection_copy_data(Main *main, Id *id_dst, const Id *id_src, const int flag)
{
  Collection *collection_dst = (Collection *)id_dst;
  const Collection *collection_src = (const Collection *)id_src;

  lib_assert(((collection_src->flag & COLLECTION_IS_MASTER) != 0) ==
             ((collection_src->id.flag & LIB_EMBEDDED_DATA) != 0));

  /* Do not copy collection's preview (same behavior as for objects). */
  if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0 && false) { /* TODO: temp hack. */
    dune_previewimg_id_copy(&collection_dst->id, &collection_src->id);
  }
  else {
    collection_dst->preview = NULL;
  }

  collection_dst->flag &= ~COLLECTION_HAS_OBJECT_CACHE;
  collection_dst->flag &= ~COLLECTION_HAS_OBJECT_CACHE_INSTANCED;
  lib_list_clear(&collection_dst->object_cache);
  lib_list_clear(&collection_dst->object_cache_instanced);

  lib_list_clear(&collection_dst->gobject);
  lib_list_clear(&collection_dst->children);
  lib_list_clear(&collection_dst->parents);

  LIST_FOREACH (CollectionChild *, child, &collection_src->children) {
    collection_child_add(collection_dst, child->collection, flag, false);
  }
  LIST_FOREACH (CollectionObject *, cob, &collection_src->gobject) {
    collection_object_add(main, collection_dst, cob->ob, flag, false);
  }
}

static void collection_free_data(Id *id)
{
  Collection *collection = (Collection *)id;

  /* No animation-data here. */
  dube_previewimg_free(&collection->preview);

  lib_freelistn(&collection->gobject);
  lib_freelistn(&collection->children);
  lib_freelistn(&collection->parents);

  dune_collection_object_cache_free(collection);
}

static void collection_foreach_id(Id *id, LibForeachIdData *data)
{
  Collection *collection = (Collection *)id;

  LIST_FOREACH (CollectionObject *, cob, &collection->gobject) {
    DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, cob->ob, IDWALK_CB_USER);
  }
  LIST_FOREACH (CollectionChild *, child, &collection->children) {
    DUNE_LIB_FOREACHID_PROCESS_IDSUPER(
        data, child->collection, IDWALK_CB_NEVER_SELF | IDWALK_CB_USER);
  }
  LIST_FOREACH (CollectionParent *, parent, &collection->parents) {
    /* This is very weak. The whole idea of keeping pointers to private IDs is very bad
     * anyway... */
    const int cb_flag = ((parent->collection != NULL &&
                          (parent->collection->id.flag & LIB_EMBEDDED_DATA) != 0) ?
                             IDWALK_CB_EMBEDDED :
                             IDWALK_CB_NOP);
    DUNE_LIB_FOREACHID_PROCESS_IDSUPER(
        data, parent->collection, IDWALK_CB_NEVER_SELF | IDWALK_CB_LOOPBACK | cb_flag);
  }
}

static Id *collection_owner_get(Main *main, Id *id)
{
  if ((id->flag & LIB_EMBEDDED_DATA) == 0) {
    return id;
  }
  lib_assert((id->tag & LIB_TAG_NO_MAIN) == 0);

  Collection *master_collection = (Collection *)id;
  lib_assert((master_collection->flag & COLLECTION_IS_MASTER) != 0);

  LIST_FOREACH (Scene *, scene, &main->scenes) {
    if (scene->master_collection == master_collection) {
      return &scene->id;
    }
  }

  lib_assert_msg(0, "Embedded collection with no owner. Critical Main inconsistency.");
  return NULL;
}

void dune_collection_blend_write_nolib(Writer *writer, Collection *collection)
{
  dune_id_write(writer, &collection->id);

  /* Shared fn for collection data-blocks and scene master collection. */
  dune_previewimg_write(writer, collection->preview);

  LIST_FOREACH (CollectionObject *, cob, &collection->gobject) {
    loader_write_struct(writer, CollectionObject, cob);
  }

  LIST_FOREACH (CollectionChild *, child, &collection->children) {
    loader_write_struct(writer, CollectionChild, child);
  }
}

static void dune_collection_write(Writer *writer, Id *id, const void *id_address)
{
  Collection *collection = (Collection *)id;

  /* Clean up, important in undo case to reduce false detection of changed data-blocks. */
  collection->flag &= ~COLLECTION_HAS_OBJECT_CACHE;
  collection->flag &= ~COLLECTION_HAS_OBJECT_CACHE_INSTANCED;
  collection->tag = 0;
  lib_list_clear(&collection->object_cache);
  lib_list_clear(&collection->object_cache_instanced);
  lib_list_clear(&collection->parents);

  /* write LibData */
  loader_write_id_struct(writer, Collection, id_address, &collection->id);

  BKE_collection_blend_write_nolib(writer, collection);
}

#ifdef USE_COLLECTION_COMPAT_28
void dube_collection_compat_read_data(DataReader *reader, SceneCollection *sc)
{
  loader_read_list(reader, &sc->objects);
  loader_read_list(reader, &sc->scene_collections);

  LIST_FOREACH (SceneCollection *, nsc, &sc->scene_collections) {
    dune_collection_compat_read_data(reader, nsc);
  }
}
#endif

void dune_collection_read_data(DataReader *reader, Collection *collection)
{
  loader_read_list(reader, &collection->gobject);
  loader_read_list(reader, &collection->children);

  loader_read_data_address(reader, &collection->preview);
  dune_previewimg_read(reader, collection->preview);

  collection->flag &= ~COLLECTION_HAS_OBJECT_CACHE;
  collection->flag &= ~COLLECTION_HAS_OBJECT_CACHE_INSTANCED;
  collection->tag = 0;
  lib_list_clear(&collection->object_cache);
  lib_list_clear(&collection->object_cache_instanced);
  lib_list_clear(&collection->parents);

#ifdef USE_COLLECTION_COMPAT_28
  /* This runs before the very first doversion. */
  loader_read_data_address(reader, &collection->collection);
  if (collection->collection != NULL) {
    dune_collection_compat_read_data(reader, collection->collection);
  }

  loader_read_data_address(reader, &collection->view_layer);
  if (collection->view_layer != NULL) {
    dune_view_layer_read_data(reader, collection->view_layer);
  }
#endif
}

static void dune_collection_read_data(DataReader *reader, Id *id)
{
  Collection *collection = (Collection *)id;
  dune_collection_read_data(reader, collection);
}

static void lib_link_collection_data(LibReader *reader, Lib *lib, Collection *collection)
{
  LIST_FOREACH_MUTABLE (CollectionObject *, cob, &collection->gobject) {
    loader_read_id_address(reader, lib, &cob->ob);

    if (cob->ob == NULL) {
      lib_freelinkn(&collection->gobject, cob);
    }
  }

  LIST_FOREACH (CollectionChild *, child, &collection->children) {
    loader_read_id_address(reader, lib, &child->collection);
  }
}

#ifdef USE_COLLECTION_COMPAT_28
void dune_collection_compat_read_lib(LibReader *reader,
                                     Lib *lib,
                                     SceneCollection *sc)
{
  LIST_FOREACH (LinkData *, link, &sc->objects) {
    loader_read_id_address(reader, lib, &link->data);
    lib_assert(link->data);
  }

  LIST_FOREACH (SceneCollection *, nsc, &sc->scene_collections) {
    dune_collection_compat_read_lib(reader, lib, nsc);
  }
}
#endif

void dune_collection_read_lib(LibReader *reader, Collection *collection)
{
#ifdef USE_COLLECTION_COMPAT_28
  if (collection->collection) {
    dune_collection_compat_read_lib(reader, collection->id.lib, collection->collection);
  }

  if (collection->view_layer) {
    dune_view_layer_read_lib(reader, collection->id.lib, collection->view_layer);
  }
#endif

  lib_link_collection_data(reader, collection->id.lib, collection);
}

static void dune_collection_read_lib(LibReader *reader, Id *id)
{
  Collection *collection = (Collection *)id;
  dune_collection_read_lib(reader, collection);
}

#ifdef USE_COLLECTION_COMPAT_28
void dune_collection_compat_dune_read_expand(struct Expander *expander,
                                             struct SceneCollection *sc)
{
  LIST_FOREACH (LinkData *, link, &sc->objects) {
    loader_expand(expander, link->data);
  }

  LIST_FOREACH (SceneCollection *, nsc, &sc->scene_collections) {
    dune_collection_compat_read_expand(expander, nsc);
  }
}
#endif

void dune_collection_read_expand(Expander *expander, Collection *collection)
{
  LIST_FOREACH (CollectionObject *, cob, &collection->gobject) {
    loader_expand(expander, cob->ob);
  }

  LIST_FOREACH (CollectionChild *, child, &collection->children) {
    loader_expand(expander, child->collection);
  }

#ifdef USE_COLLECTION_COMPAT_28
  if (collection->collection != NULL) {
    dune_collection_compat_read_expand(expander, collection->collection);
  }
#endif
}

static void dune_collection_read_expand(Expander *expander, Id *id)
{
  Collection *collection = (Collection *)id;
  dune_collection_read_expand(expander, collection);
}

IdTypeInfo IdType_ID_GR = {
    .id_code = ID_GR,
    .id_filter = FILTER_ID_GR,
    .main_list_index = INDEX_ID_GR,
    .struct_size = sizeof(Collection),
    .name = "Collection",
    .name_plural = "collections",
    .lang_cxt = LANG_CXT_ID_COLLECTION,
    .flags = IDTYPE_FLAGS_NO_ANIMDATA,
    .asset_type_info = NULL,

    .init_data = collection_init_data,
    .copy_data = collection_copy_data,
    .free_data = collection_free_data,
    .make_local = NULL,
    .foreach_id = collection_foreach_id,
    .foreach_cache = NULL,
    .foreach_path = NULL,
    .owner_get = collection_owner_get,

    .dune_write = collectio_write,
    .dune_read_data = collection_read_data,
    .dune_read_lib = collection_read_lib,
    .dune_read_expand = collection_read_expand,

    .dune_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

/* Add Collection */

/* Add new collection, without view layer syncing. */
static Collection *collection_add(Main *main,
                                  Collection *collection_parent,
                                  const char *name_custom)
{
  /* Determine new collection name. */
  char name[MAX_NAME];

  if (name_custom) {
    STRNCPY(name, name_custom);
  }
  else {
    dune_collection_new_name_get(collection_parent, name);
  }

  /* Create new collection. */
  Collection *collection = dune_id_new(main, ID_GR, name);

  /* We increase collection user count when linking to Collections. */
  id_us_min(&collection->id);

  /* Optionally add to parent collection. */
  if (collection_parent) {
    collection_child_add(collection_parent, collection, 0, true);
  }

  return collection;
}

Collection *dune_collection_add(Main *main, Collection *collection_parent, const char *name_custom)
{
  Collection *collection = collection_add(main, collection_parent, name_custom);
  dune_main_collection_sync(main);
  return collection;
}

void dune_collection_add_from_object(Main *main,
                                     Scene *scene,
                                     const Object *ob_src,
                                     Collection *collection_dst)
{
  bool is_instantiated = false;

  FOREACH_SCENE_COLLECTION_BEGIN (scene, collection) {
    if (!ID_IS_LINKED(collection) && dune_collection_has_object(collection, ob_src)) {
      collection_child_add(collection, collection_dst, 0, true);
      is_instantiated = true;
    }
  }
  FOREACH_SCENE_COLLECTION_END;

  if (!is_instantiated) {
    collection_child_add(scene->master_collection, collection_dst, 0, true);
  }

  dune_main_collection_sync(main);
}

void dune_collection_add_from_collection(Main *main,
                                        Scene *scene,
                                        Collection *collection_src,
                                        Collection *collection_dst)
{
  bool is_instantiated = false;

  FOREACH_SCENE_COLLECTION_BEGIN (scene, collection) {
    if (!ID_IS_LINKED(collection) && collection_find_child(collection, collection_src)) {
      collection_child_add(collection, collection_dst, 0, true);
      is_instantiated = true;
    }
    else if (!is_instantiated && collection_find_child(collection, collection_dst)) {
      /* If given collection_dst is already instantiated in scene, even if its 'model' src one is
       * not, do not add it to master scene collection. */
      is_instantiated = true;
    }
  }
  FOREACH_SCENE_COLLECTION_END;

  if (!is_instantiated) {
    collection_child_add(scene->master_collection, collection_dst, 0, true);
  }

  dune_main_collection_sync(main);
}

/* Free and Delete Collection */
void dune_collection_free_data(Collection *collection)
{
  dune_libblock_free_data(&collection->id, false);
  collection_free_data(&collection->id);
}

bool dune_collection_delete(Main *main, Collection *collection, bool hierarchy)
{
  /* Master collection is not real datablock, can't be removed. */
  if (collection->flag & COLLECTION_IS_MASTER) {
    lib_assert_msg(0, "Scene master collection can't be deleted");
    return false;
  }

  if (hierarchy) {
    /* Remove child objects. */
    CollectionObject *cob = collection->gobject.first;
    while (cob != NULL) {
      collection_object_remove(main, collection, cob->ob, true);
      cob = collection->gobject.first;
    }

    /* Delete all child collections recursively. */
    CollectionChild *child = collection->children.first;
    while (child != NULL) {
      dune_collection_delete(main, child->collection, hierarchy);
      child = collection->children.first;
    }
  }
  else {
    /* Link child collections into parent collection. */
    LIST_FOREACH (CollectionChild *, child, &collection->children) {
      LIST_FOREACH (CollectionParent *, cparent, &collection->parents) {
        Collection *parent = cparent->collection;
        collection_child_add(parent, child->collection, 0, true);
      }
    }

    CollectionObject *cob = collection->gobject.first;
    while (cob != NULL) {
      /* Link child object into parent collections. */
      LIST_FOREACH (CollectionParent *, cparent, &collection->parents) {
        Collection *parent = cparent->collection;
        collection_object_add(main, parent, cob->ob, 0, true);
      }

      /* Remove child object. */
      collection_object_remove(main, collection, cob->ob, true);
      cob = collection->gobject.first;
    }
  }

  dune_id_delete(main, collection);

  dune_main_collection_sync(main);

  return true;
}

/* Collection Copy */
static Collection *collection_duplicate_recursive(Main *main,
                                                  Collection *parent,
                                                  Collection *collection_old,
                                                  const eIdFlagsDup duplicate_flags,
                                                  const eLibIdFlagsDup duplicate_options)
{
  Collection *collection_new;
  bool do_full_process = false;
  const bool is_collection_master = (collection_old->flag & COLLECTION_IS_MASTER) != 0;

  const bool do_objects = (duplicate_flags & USER_DUP_OBJECT) != 0;

  if (is_collection_master) {
    /* We never duplicate master collections here, but we can still deep-copy their objects and
     * collections. */
    lib_assert(parent == NULL);
    collection_new = collection_old;
    do_full_process = true;
  }
  else if (collection_old->id.newid == NULL) {
    collection_new = (Collection *)dune_id_copy_for_duplicate(
        main, (Id *)collection_old, duplicate_flags, LIB_ID_COPY_DEFAULT);

    if (collection_new == collection_old) {
      return collection_new;
    }

    do_full_process = true;
  }
  else {
    collection_new = (Collection *)collection_old->id.newid;
  }

  /* Optionally add to parent (we always want to do that,
   * even if collection_old had already been duplicated). */
  if (parent != NULL) {
    if (collection_child_add(parent, collection_new, 0, true)) {
      /* Put collection right after existing one. */
      CollectionChild *child = collection_find_child(parent, collection_old);
      CollectionChild *child_new = collection_find_child(parent, collection_new);

      if (child && child_new) {
        lib_remlink(&parent->children, child_new);
        lib_insertlinkafter(&parent->children, child, child_new);
      }
    }
  }

  /* If we are not doing any kind of deep-copy, we can return immediately.
   * False do_full_process means collection_old had already been duplicated,
   * no need to redo some deep-copy on it. */
  if (!do_full_process) {
    return collection_new;
  }

  if (do_objects) {
    /* We need to first duplicate the objects in a separate loop, to support the master collection
     * case, where both old and new collections are the same.
     * Otherwise, depending on naming scheme and sorting, we may end up duplicating the new objects
     * we just added, in some infinite loop. */
    LIST_FOREACH (CollectionObject *, cob, &collection_old->gobject) {
      Object *ob_old = cob->ob;

      if (ob_old->id.newid == NULL) {
        dune_object_duplicate(
            main, ob_old, duplicate_flags, duplicate_options | LIB_ID_DUPLICATE_IS_SUBPROCESS);
      }
    }

    /* We can loop on collection_old's objects, but have to consider it mutable because with master
     * collections collection_old and collection_new are the same data here. */
    LIST_FOREACH_MUTABLE (CollectionObject *, cob, &collection_old->gobject) {
      Object *ob_old = cob->ob;
      Object *ob_new = (Object *)ob_old->id.newid;

      /* New object can be NULL in master collection case, since new and old objects are in same
       * collection. */
      if (ELEM(ob_new, ob_old, NULL)) {
        continue;
      }

      collection_object_add(main, collection_new, ob_new, 0, true);
      collection_object_remove(main, collection_new, ob_old, false);
    }
  }

  /* We can loop on collection_old's children,
   * that list is currently identical the collection_new' children, and won't be changed here. */
  LIST_FOREACH_MUTABLE (CollectionChild *, child, &collection_old->children) {
    Collection *child_collection_old = child->collection;

    Collection *child_collection_new = collection_duplicate_recursive(
        main, collection_new, child_collection_old, duplicate_flags, duplicate_options);
    if (child_collection_new != child_collection_old) {
      collection_child_remove(collection_new, child_collection_old);
    }
  }

  return collection_new;
}

Collection *dune_collection_duplicate(Main *main,
                                     Collection *parent,
                                     Collection *collection,
                                     eIdFlagsDup duplicate_flags,
                                     eLibIdFlagsDup duplicate_options)
{
  const bool is_subprocess = (duplicate_options & LIB_ID_DUPLICATE_IS_SUBPROCESS) != 0;
  const bool is_root_id = (duplicate_options & LIB_ID_DUPLICATE_IS_ROOT_ID) != 0;

  if (!is_subprocess) {
    dune_main_id_newptr_and_tag_clear(main);
  }
  if (is_root_id) {
    /* In case root duplicated ID is linked, assume we want to get a local copy of it and duplicate
     * all expected linked data. */
    if (ID_IS_LINKED(collection)) {
      duplicate_flags |= USER_DUP_LINKED_ID;
    }
    duplicate_options &= ~LIB_ID_DUPLICATE_IS_ROOT_ID;
  }

  Collection *collection_new = collection_duplicate_recursive(
      main, parent, collection, duplicate_flags, duplicate_options);

  if (!is_subprocess) {
    /* `collection_duplicate_recursive` will also tag our 'root' collection, which is not required
     * unless its duplication is a sub-process of another one. */
    collection_new->id.tag &= ~LIB_TAG_NEW;

    /* This code will follow into all Id links using an ID tagged with LIB_TAG_NEW. */
    dune_libblock_relink_to_newid(main, &collection_new->id, 0);

#ifndef NDEBUG
    /* Call to `dune_libblock_relink_to_newid` above is supposed to have cleared all those flags. */
    Id *id_iter;
    FOREACH_MAIN_ID_BEGIN (main, id_iter) {
      if (id_iter->tag & LIB_TAG_NEW) {
        lib_assert((id_iter->tag & LIB_TAG_NEW) == 0);
      }
    }
    FOREACH_MAIN_ID_END;
#endif

    /* Cleanup */
    dune_main_id_newptr_and_tag_clear(main);

    dune_main_collection_sync(main);
  }

  return collection_new;
}

/* Collection Naming */
void dune_collection_new_name_get(Collection *collection_parent, char *rname)
{
  char *name;

  if (!collection_parent) {
    name = lib_strdup("Collection");
  }
  else if (collection_parent->flag & COLLECTION_IS_MASTER) {
    name = lib_sprintfn("Collection %d", lib_list_count(&collection_parent->children) + 1);
  }
  else {
    const int number = lib_list_count(&collection_parent->children) + 1;
    const int digits = integer_digits_i(number);
    const int max_len = sizeof(collection_parent->id.name) - 1 /* NULL terminator */ -
                        (1 + digits) /* " %d" */ - 2 /* ID */;
    name = lib_sprintfn("%.*s %d", max_len, collection_parent->id.name + 2, number);
  }

  lib_strncpy(rname, name, MAX_NAME);
  mem_freen(name);
}

const char *dune_collection_ui_name_get(struct Collection *collection)
{
  if (collection->flag & COLLECTION_IS_MASTER) {
    return IFACE_("Scene Collection");
  }

  return collection->id.name + 2;
}

/* Object List Cache */
static void collection_object_cache_fill(List *lb,
                                         Collection *collection,
                                         int parent_restrict,
                                         bool with_instances)
{
  int child_restrict = collection->flag | parent_restrict;

  LIST_FOREACH (CollectionObject *, cob, &collection->gobject) {
    Base *base = lib_findptr(lb, cob->ob, offsetof(Base, object));

    if (base == NULL) {
      base = mem_callocn(sizeof(Base), "Object Base");
      base->object = cob->ob;
      lib_addtail(lb, base);
      if (with_instances && cob->ob->instance_collection) {
        collection_object_cache_fill(
            lb, cob->ob->instance_collection, child_restrict, with_instances);
      }
    }

    /* Only collection flags are checked here currently, object restrict flag is checked
     * in FOREACH_COLLECTION_VISIBLE_OBJECT_RECURSIVE_BEGIN since it can be animated
     * without updating the cache. */
    if (((child_restrict & COLLECTION_HIDE_VIEWPORT) == 0)) {
      base->flag |= BASE_ENABLED_VIEWPORT;
    }
    if (((child_restrict & COLLECTION_HIDE_RENDER) == 0)) {
      base->flag |= BASE_ENABLED_RENDER;
    }
  }

  LIST_FOREACH (CollectionChild *, child, &collection->children) {
    collection_object_cache_fill(lb, child->collection, child_restrict, with_instances);
  }
}

List dune_collection_object_cache_get(Collection *collection)
{
  if (!(collection->flag & COLLECTION_HAS_OBJECT_CACHE)) {
    static ThreadMutex cache_lock = LIB_MUTEX_INITIALIZER;

    lib_mutex_lock(&cache_lock);
    if (!(collection->flag & COLLECTION_HAS_OBJECT_CACHE)) {
      collection_object_cache_fill(&collection->object_cache, collection, 0, false);
      collection->flag |= COLLECTION_HAS_OBJECT_CACHE;
    }
    lib_mutex_unlock(&cache_lock);
  }

  return collection->object_cache;
}

List dune_collection_object_cache_instanced_get(Collection *collection)
{
  if (!(collection->flag & COLLECTION_HAS_OBJECT_CACHE_INSTANCED)) {
    static ThreadMutex cache_lock = LIB_MUTEX_INITIALIZER;

    lib_mutex_lock(&cache_lock);
    if (!(collection->flag & COLLECTION_HAS_OBJECT_CACHE_INSTANCED)) {
      collection_object_cache_fill(&collection->object_cache_instanced, collection, 0, true);
      collection->flag |= COLLECTION_HAS_OBJECT_CACHE_INSTANCED;
    }
    lib_mutex_unlock(&cache_lock);
  }

  return collection->object_cache_instanced;
}

static void collection_object_cache_free(Collection *collection)
{
  /* Clear own cache an for all parents, since those are affected by changes as well. */
  collection->flag &= ~COLLECTION_HAS_OBJECT_CACHE;
  collection->flag &= ~COLLECTION_HAS_OBJECT_CACHE_INSTANCED;
  lib_freelistn(&collection->object_cache);
  lib_freelistn(&collection->object_cache_instanced);

  LIST_FOREACH (CollectionParent *, parent, &collection->parents) {
    collection_object_cache_free(parent->collection);
  }
}

void dune_collection_object_cache_free(Collection *collection)
{
  collection_object_cache_free(collection);
}

Base *dune_collection_or_layer_objects(const ViewLayer *view_layer, Collection *collection)
{
  if (collection) {
    return dune_collection_object_cache_get(collection).first;
  }

  return FIRSTBASE(view_layer);
}

/* Scene Master Collection */
Collection *dune_collection_master_add()
{
  /* Not an actual datablock, but owned by scene. */
  Collection *master_collection = BKE_libblock_alloc(
      NULL, ID_GR, BKE_SCENE_COLLECTION_NAME, LIB_ID_CREATE_NO_MAIN);
  master_collection->id.flag |= LIB_EMBEDDED_DATA;
  master_collection->flag |= COLLECTION_IS_MASTER;
  master_collection->color_tag = COLLECTION_COLOR_NONE;
  return master_collection;
}

/* Cyclic Checks */
static bool collection_object_cyclic_check_internal(Object *object, Collection *collection)
{
  if (object->instance_collection) {
    Collection *dup_collection = object->instance_collection;
    if ((dup_collection->id.tag & LIB_TAG_DOIT) == 0) {
      /* Cycle already exists in collections, let's prevent further crappyness */
      return true;
    }
    /* flag the object to identify cyclic dependencies in further dupli collections */
    dup_collection->id.tag &= ~LIB_TAG_DOIT;

    if (dup_collection == collection) {
      return true;
    }

    FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (dup_collection, collection_object) {
      if (collection_object_cyclic_check_internal(collection_object, dup_collection)) {
        return true;
      }
    }
    FOREACH_COLLECTION_OBJECT_RECURSIVE_END;

    /* un-flag the object, it's allowed to have the same collection multiple times in parallel */
    dup_collection->id.tag |= LIB_TAG_DOIT;
  }

  return false;
}

bool dune_collection_object_cyclic_check(Main *main, Object *object, Collection *collection)
{
  /* first flag all collections */
  dune_main_id_tag_list(&main->collections, LIB_TAG_DOIT, true);

  return collection_object_cyclic_check_internal(object, collection);
}

/* Collection Object Membership */
bool dune_collection_has_object(Collection *collection, const Object *ob)
{
  if (ELEM(NULL, collection, ob)) {
    return false;
  }

  return (lib_findptr(&collection->gobject, ob, offsetof(CollectionObject, ob)));
}

bool dune_collection_has_object_recursive(Collection *collection, Object *ob)
{
  if (ELEM(NULL, collection, ob)) {
    return false;
  }

  const List objects = dune_collection_object_cache_get(collection);
  return (lib_findptr(&objects, ob, offsetof(Base, object)));
}

bool dune_collection_has_object_recursive_instanced(Collection *collection, Object *ob)
{
  if (ELEM(NULL, collection, ob)) {
    return false;
  }

  const List objects = dune_collection_object_cache_instanced_get(collection);
  return (lib_findptr(&objects, ob, offsetof(Base, object)));
}

static Collection *collection_next_find(Main *bmain, Scene *scene, Collection *collection)
{
  if (scene && collection == scene->master_collection) {
    return main->collections.first;
  }

  return collection->id.next;
}

Collection *dune_collection_object_find(Main *main,
                                       Scene *scene,
                                       Collection *collection,
                                       Object *ob)
{
  if (collection) {
    collection = collection_next_find(main, scene, collection);
  }
  else if (scene) {
    collection = scene->master_collection;
  }
  else {
    collection = main->collections.first;
  }

  while (collection) {
    if (dune_collection_has_object(collection, ob)) {
      return collection;
    }
    collection = collection_next_find(main, scene, collection);
  }
  return NULL;
}

bool dune_collection_is_empty(const Collection *collection)
{
  return lib_list_is_empty(&collection->gobject) &&
         lib_list_is_empty(&collection->children);
}

/* Collection Objects */
static void collection_tag_update_parent_recursive(Main *main,
                                                   Collection *collection,
                                                   const int flag)
{
  if (collection->flag & COLLECTION_IS_MASTER) {
    return;
  }

  graph_id_tag_update_ex(main, &collection->id, flag);

  LIST_FOREACH (CollectionParent *, collection_parent, &collection->parents) {
    if (collection_parent->collection->flag & COLLECTION_IS_MASTER) {
      /* We don't care about scene/master collection here. */
      continue;
    }
    collection_tag_update_parent_recursive(main, collection_parent->collection, flag);
  }
}

static Collection *collection_parent_editable_find_recursive(Collection *collection)
{
  if (!ID_IS_LINKED(collection) && !ID_IS_OVERRIDE_LIB(collection)) {
    return collection;
  }

  if (collection->flag & COLLECTION_IS_MASTER) {
    return NULL;
  }

  LIST_FOREACH (CollectionParent *, collection_parent, &collection->parents) {
    if (!ID_IS_LINKED(collection_parent->collection) &&
        !ID_IS_OVERRIDE_LIB(collection_parent->collection)) {
      return collection_parent->collection;
    }
    Collection *editable_collection = collection_parent_editable_find_recursive(
        collection_parent->collection);
    if (editable_collection != NULL) {
      return editable_collection;
    }
  }

  return NULL;
}

static bool collection_object_add(
    Main *main, Collection *collection, Object *ob, int flag, const bool add_us)
{
  if (ob->instance_collection) {
    /* Cyclic dependency check. */
    if (collection_find_child_recursive(ob->instance_collection, collection) ||
        ob->instance_collection == collection) {
      return false;
    }
  }

  CollectionObject *cob = lib_findptr(&collection->gobject, ob, offsetof(CollectionObject, ob));
  if (cob) {
    return false;
  }

  cob = mem_callocn(sizeof(CollectionObject), __func__);
  cob->ob = ob;
  lib_addtail(&collection->gobject, cob);
  dune_collection_object_cache_free(collection);

  if (add_us && (flag & LIB_ID_CREATE_NO_USER_REFCOUNT) == 0) {
    id_us_plus(&ob->id);
  }

  if ((flag & LIB_ID_CREATE_NO_MAIN) == 0) {
    collection_tag_update_parent_recursive(main, collection, ID_RECALC_COPY_ON_WRITE);
  }

  if ((flag & LIB_ID_CREATE_NO_MAIN) == 0) {
    dune_rigidbody_main_collection_object_add(main, collection, ob);
  }

  return true;
}

static bool collection_object_remove(Main *main,
                                     Collection *collection,
                                     Object *ob,
                                     const bool free_us)
{
  CollectionObject *cob = lib_findptr(&collection->gobject, ob, offsetof(CollectionObject, ob));
  if (cob == NULL) {
    return false;
  }

  lib_freelinkn(&collection->gobject, cob);
  dune_collection_object_cache_free(collection);

  if (free_us) {
    dune_id_free_us(main, ob);
  }
  else {
    id_us_min(&ob->id);
  }

  collection_tag_update_parent_recursive(main, collection, ID_RECALC_COPY_ON_WRITE);

  return true;
}

bool dune_collection_object_add_notest(Main *main, Collection *collection, Object *ob)
{
  if (ob == NULL) {
    return false;
  }

  /* Only case where this ptr can be NULL is when scene itself is linked, this case should
   * never be reached. */
  lib_assert(collection != NULL);
  if (collection == NULL) {
    return false;
  }

  if (!collection_object_add(main, collection, ob, 0, true)) {
    return false;
  }

  if (dune_collection_is_in_scene(collection)) {
    dune_main_collection_sync(main);
  }

  grqph_id_tag_update(&collection->id, ID_RECALC_GEOMETRY);

  return true;
}

bool dune_collection_object_add(Main *main, Collection *collection, Object *ob)
{
  if (collection == NULL) {
    return false;
  }

  collection = collection_parent_editable_find_recursive(collection);

  return dune_collection_object_add_notest(main, collection, ob);
}

void dune_collection_object_add_from(Main *main, Scene *scene, Object *ob_src, Object *ob_dst)
{
  bool is_instantiated = false;

  FOREACH_SCENE_COLLECTION_BEGIN (scene, collection) {
    if (!ID_IS_LINKED(collection) && !ID_IS_OVERRIDE_LIB(collection) &&
        dune_collection_has_object(collection, ob_src)) {
      collection_object_add(main, collection, ob_dst, 0, true);
      is_instantiated = true;
    }
  }
  FOREACH_SCENE_COLLECTION_END;

  if (!is_instantiated) {
    /* In case we could not find any non-linked collections in which instantiate our ob_dst,
     * fallback to scene's master collection... */
    collection_object_add(main, scene->master_collection, ob_dst, 0, true);
  }

  dune_main_collection_sync(main);
}

bool dune_collection_object_remove(Main *main,
                                   Collection *collection,
                                   Object *ob,
                                   const bool free_us)
{
  if (ELEM(NULL, collection, ob)) {
    return false;
  }

  if (!collection_object_remove(main, collection, ob, free_us)) {
    return false;
  }

  if (dune_collection_is_in_scene(collection)) {
    dune_main_collection_sync(main);
  }

  graph_id_tag_update(&collection->id, ID_RECALC_GEOMETRY);

  return true;
}

/* Remove object from all collections of scene
 * param collection_skip: Don't remove base from this collection. */
static bool scene_collections_object_remove(
    Main *main, Scene *scene, Object *ob, const bool free_us, Collection *collection_skip)
{
  bool removed = false;

  if (collection_skip == NULL) {
    dune_scene_remove_rigidbody_object(main, scene, ob, free_us);
  }

  FOREACH_SCENE_COLLECTION_BEGIN (scene, collection) {
    if (collection != collection_skip) {
      removed |= collection_object_remove(main, collection, ob, free_us);
    }
  }
  FOREACH_SCENE_COLLECTION_END;

  dune_main_collection_sync(main);

  return removed;
}

bool dune_scene_collections_object_remove(Main *main, Scene *scene, Object *ob, const bool free_us)
{
  return scene_collections_object_remove(main, scene, ob, free_us, NULL);
}

/* Remove all NULL objects from collections.
 * This is used for lib remapping, where these ptrs have been set to NULL.
 * Otherwise this should never happen. */
static void collection_object_remove_nulls(Collection *collection)
{
  bool changed = false;

  LIST_FOREACH_MUTABLE (CollectionObject *, cob, &collection->gobject) {
    if (cob->ob == NULL) {
      lib_freelinkn(&collection->gobject, cob);
      changed = true;
    }
  }

  if (changed) {
    dune_collection_object_cache_free(collection);
  }
}

void dune_collections_object_remove_nulls(Main *main)
{
  LIST_FOREACH (Scene *, scene, &main->scenes) {
    collection_object_remove_nulls(scene->master_collection);
  }

  LIST_FOREACH (Collection *, collection, &main->collections) {
    collection_object_remove_nulls(collection);
  }
}

/* Remove all duplicate objects from collections.
 * This is used for lib remapping, happens when remapping an object to another one already
 * present in the collection. Otherwise this should never happen. */
static void collection_object_remove_duplicates(Collection *collection)
{
  bool changed = false;

  LIST_FOREACH_MUTABLE (CollectionObject *, cob, &collection->gobject) {
    if (cob->ob->runtime.collection_management) {
      lib_freelinkn(&collection->gobject, cob);
      changed = true;
      continue;
    }
    cob->ob->runtime.collection_management = true;
  }

  /* Cleanup. */
  LIST_FOREACH (CollectionObject *, cob, &collection->gobject) {
    cob->ob->runtime.collection_management = false;
  }

  if (changed) {
    dune_collection_object_cache_free(collection);
  }
}

void dune_collections_object_remove_duplicates(struct Main *main)
{
  LIST_FOREACH (Object *, ob, &main->objects) {
    ob->runtime.collection_management = false;
  }

  LIST_FOREACH (Scene *, scene, &main->scenes) {
    collection_object_remove_duplicates(scene->master_collection);
  }

  LIST_FOREACH (Collection *, collection, &main->collections) {
    collection_object_remove_duplicates(collection);
  }
}

static void collection_null_children_remove(Collection *collection)
{
  LIST_FOREACH_MUTABLE (CollectionChild *, child, &collection->children) {
    if (child->collection == NULL) {
      lib_freelinkn(&collection->children, child);
    }
  }
}

static void collection_missing_parents_remove(Collection *collection)
{
  LIST_FOREACH_MUTABLE (CollectionParent *, parent, &collection->parents) {
    if ((parent->collection == NULL) || !collection_find_child(parent->collection, collection)) {
      lib_freelinkn(&collection->parents, parent);
    }
  }
}

void dune_collections_child_remove_nulls(Main *main,
                                        Collection *parent_collection,
                                        Collection *child_collection)
{
  if (child_collection == NULL) {
    if (parent_collection != NULL) {
      collection_null_children_remove(parent_collection);
    }
    else {
      /* We need to do the checks in two steps when more than one collection may be involved,
       * otherwise we can miss some cases...
       * Also, master collections are not in bmain, so we also need to loop over scenes */
      LIST_FOREACH (Collection *, collection, &main->collections) {
        collection_null_children_remove(collection);
      }
      LIST_FOREACH (Scene *, scene, &bmain->scenes) {
        collection_null_children_remove(scene->master_collection);
      }
    }

    LIST_FOREACH (Collection *, collection, &main->collections) {
      collection_missing_parents_remove(collection);
    }
    LIST_FOREACH (Scene *, scene, &main->scenes) {
      collection_missing_parents_remove(scene->master_collection);
    }
  }
  else {
    LIST_FOREACH_MUTABLE (CollectionParent *, parent, &child_collection->parents) {
      collection_null_children_remove(parent->collection);

      if (!collection_find_child(parent->collection, child_collection)) {
        lib_freelinkn(&child_collection->parents, parent);
      }
    }
  }
}

void dune_collection_object_move(
    Main *main, Scene *scene, Collection *collection_dst, Collection *collection_src, Object *ob)
{
  /* In both cases we first add the object, then remove it from the other collections.
   * Otherwise we lose the original base and whether it was active and selected. */
  if (collection_src != NULL) {
    if (dune_collection_object_add(main, collection_dst, ob)) {
      dune_collection_object_remove(main, collection_src, ob, false);
    }
  }
  else {
    /* Adding will fail if object is already in collection.
     * However we still need to remove it from the other collections. */
    dune_collection_object_add(main, collection_dst, ob);
    scene_collections_object_remove(main, scene, ob, false, collection_dst);
  }
}

/* Collection Scene Membership */
bool dune_collection_is_in_scene(Collection *collection)
{
  if (collection->flag & COLLECTION_IS_MASTER) {
    return true;
  }

  LIST_FOREACH (CollectionParent *, cparent, &collection->parents) {
    if (dune_collection_is_in_scene(cparent->collection)) {
      return true;
    }
  }

  return false;
}

void dune_collections_after_lib_link(Main *main)
{
  /* Need to update layer collections because objects might have changed
   * in linked files, and because undo push does not include updated base
   * flags since those are refreshed after the operator completes. */
  dune_main_collection_sync(main);
}

/* Collection Children */
static bool collection_instance_find_recursive(Collection *collection,
                                               Collection *instance_collection)
{
  LIST_FOREACH (CollectionObject *, collection_object, &collection->gobject) {
    if (collection_object->ob != NULL &&
        /* Object from a given collection should never instantiate that collection either. */
        ELEM(collection_object->ob->instance_collection, instance_collection, collection)) {
      return true;
    }
  }

  LIST_FOREACH (CollectionChild *, collection_child, &collection->children) {
    if (collection_child->collection != NULL &&
        collection_instance_find_recursive(collection_child->collection, instance_collection)) {
      return true;
    }
  }

  return false;
}

bool dune_collection_cycle_find(Collection *new_ancestor, Collection *collection)
{
  if (collection == new_ancestor) {
    return true;
  }

  if (collection == NULL) {
    collection = new_ancestor;
  }

  LIST_FOREACH (CollectionParent *, parent, &new_ancestor->parents) {
    if (dune_collection_cycle_find(parent->collection, collection)) {
      return true;
    }
  }

  /* Find possible objects in collection or its children, that would instantiate the given ancestor
   * collection (that would also make a fully invalid cycle of dependencies). */
  return collection_instance_find_recursive(collection, new_ancestor);
}

static bool collection_instance_fix_recursive(Collection *parent_collection,
                                              Collection *collection)
{
  bool cycles_found = false;

  LIST_FOREACH (CollectionObject *, collection_object, &parent_collection->gobject) {
    if (collection_object->ob != NULL &&
        collection_object->ob->instance_collection == collection) {
      id_us_min(&collection->id);
      collection_object->ob->instance_collection = NULL;
      cycles_found = true;
    }
  }

  LIST_FOREACH (CollectionChild *, collection_child, &parent_collection->children) {
    if (collection_instance_fix_recursive(collection_child->collection, collection)) {
      cycles_found = true;
    }
  }

  return cycles_found;
}

static bool collection_cycle_fix_recursive(Main main,
                                           Collection *parent_collection,
                                           Collection *collection)
{
  bool cycles_found = false;

  LIST_FOREACH_MUTABLE (CollectionParent *, parent, &parent_collection->parents) {
    if (dune_collection_cycle_find(parent->collection, collection)) {
      dune_collection_child_remove(bmain, parent->collection, parent_collection);
      cycles_found = true;
    }
    else if (collection_cycle_fix_recursive(main, parent->collection, collection)) {
      cycles_found = true;
    }
  }

  return cycles_found;
}

bool dune_collection_cycles_fix(Main *main, Collection *collection)
{
  return collection_cycle_fix_recursive(main, collection, collection) ||
         collection_instance_fix_recursive(collection, collection);
}

static CollectionChild *collection_find_child(Collection *parent, Collection *collection)
{
  return lib_findptr(&parent->children, collection, offsetof(CollectionChild, collection));
}

static bool collection_find_child_recursive(const Collection *parent, const Collection *collection)
{
  LIST_FOREACH (const CollectionChild *, child, &parent->children) {
    if (child->collection == collection) {
      return true;
    }

    if (collection_find_child_recursive(child->collection, collection)) {
      return true;
    }
  }

  return false;
}

bool dune_collection_has_collection(const Collection *parent, const Collection *collection)
{
  return collection_find_child_recursive(parent, collection);
}

static CollectionParent *collection_find_parent(Collection *child, Collection *collection)
{
  return lib_findptr(&child->parents, collection, offsetof(CollectionParent, collection));
}

static bool collection_child_add(Collection *parent,
                                 Collection *collection,
                                 const int flag,
                                 const bool add_us)
{
  CollectionChild *child = collection_find_child(parent, collection);
  if (child) {
    return false;
  }
  if (dune_collection_cycle_find(parent, collection)) {
    return false;
  }

  child = mem_callocn(sizeof(CollectionChild), "CollectionChild");
  child->collection = collection;
  lib_addtail(&parent->children, child);

  /* Don't add parent links for depsgraph datablocks, these are not kept in sync. */
  if ((flag & LIB_ID_CREATE_NO_MAIN) == 0) {
    CollectionParent *cparent = MEM_callocN(sizeof(CollectionParent), "CollectionParent");
    cparent->collection = parent;
    lib_addtail(&collection->parents, cparent);
  }

  if (add_us) {
    id_us_plus(&collection->id);
  }

  dune_collection_object_cache_free(parent);

  return true;
}

static bool collection_child_remove(Collection *parent, Collection *collection)
{
  CollectionChild *child = collection_find_child(parent, collection);
  if (child == NULL) {
    return false;
  }

  CollectionParent *cparent = collection_find_parent(collection, parent);
  lib_freelinkn(&collection->parents, cparent);
  lib_freelinkn(&parent->children, child);

  id_us_min(&collection->id);

  dune_collection_object_cache_free(parent);

  return true;
}

bool dune_collection_child_add(Main *main, Collection *parent, Collection *child)
{
  if (!collection_child_add(parent, child, 0, true)) {
    return false;
  }

  dune_main_collection_sync(bmain);
  return true;
}

bool dune_collection_child_add_no_sync(Collection *parent, Collection *child)
{
  return collection_child_add(parent, child, 0, true);
}

bool dune_collection_child_remove(Main *main, Collection *parent, Collection *child)
{
  if (!collection_child_remove(parent, child)) {
    return false;
  }

  dune_main_collection_sync(main);
  return true;
}

void dune_collection_parent_relations_rebuild(Collection *collection)
{
  LIST_FOREACH_MUTABLE (CollectionChild *, child, &collection->children) {
    /* Check for duplicated children (can happen with remapping e.g.). */
    CollectionChild *other_child = collection_find_child(collection, child->collection);
    if (other_child != child) {
      lib_freelinkn(&collection->children, child);
      continue;
    }

    /* Invalid child, either without a collection, or because it creates a dependency cycle. */
    if (child->collection == NULL || dune_collection_cycle_find(collection, child->collection)) {
      lib_freelinkn(&collection->children, child);
      continue;
    }

    /* Can happen when remapping data partially out-of-Main (during advanced ID management
     * ops like lib-override resync e.g.). */
    if ((child->collection->id.tag & (LIB_TAG_NO_MAIN | LIB_TAG_COPIED_ON_WRITE)) != 0) {
      continue;
    }

    lib_assert(collection_find_parent(child->collection, collection) == NULL);
    CollectionParent *cparent = mem_callocn(sizeof(CollectionParent), __func__);
    cparent->collection = collection;
    lib_addtail(&child->collection->parents, cparent);
  }
}

static void collection_parents_rebuild_recursive(Collection *collection)
{
  /* A same collection may be child of several others, no need to process it more than once. */
  if ((collection->tag & COLLECTION_TAG_RELATION_REBUILD) == 0) {
    return;
  }

  dune_collection_parent_relations_rebuild(collection);
  collection->tag &= ~COLLECTION_TAG_RELATION_REBUILD;

  LIST_FOREACH (CollectionChild *, child, &collection->children) {
    /* See comment above in `BKE_collection_parent_relations_rebuild`. */
    if ((child->collection->id.tag & (LIB_TAG_NO_MAIN | LIB_TAG_COPIED_ON_WRITE)) != 0) {
      continue;
    }
    collection_parents_rebuild_recursive(child->collection);
  }
}

void dune_main_collections_parent_relations_rebuild(Main *main)
{
  /* Only collections not in main (master ones in scenes) have no parent... */
  LIST_FOREACH (Collection *, collection, &main->collections) {
    lib_freelistn(&collection->parents);

    collection->tag |= COLLECTION_TAG_RELATION_REBUILD;
  }

  /* Scene's master collections will be 'root' parent of most of our collections, so start with
   * them. */
  LIST_FOREACH (Scene *, scene, &bmain->scenes) {
    /* This fn can be called from readfile.c, when this pointer is not guaranteed to be NULL. */
    if (scene->master_collection != NULL) {
      lib_assert(lib_lis_is_empty(&scene->master_collection->parents));
      scene->master_collection->tag |= COLLECTION_TAG_RELATION_REBUILD;
      collection_parents_rebuild_recursive(scene->master_collection);
    }
  }

  /* We may have parent chains outside of scene's master_collection context? At least, readfile's
   * lib_link_collection_data() seems to assume that, so do the same here. */
  LIST_FOREACH (Collection *, collection, &main->collections) {
    if (collection->tag & COLLECTION_TAG_RELATION_REBUILD) {
      /* NOTE: we do not have easy access to 'which collections is root' info in that case, which
       * means test for cycles in collection relationships may fail here. I don't think that is an
       * issue in practice here, but worth keeping in mind... */
      collection_parents_rebuild_recursive(collection);
    }
  }
}

/* Collection Index */
static Collection *collection_from_index_recursive(Collection *collection,
                                                   const int index,
                                                   int *index_current)
{
  if (index == (*index_current)) {
    return collection;
  }

  (*index_current)++;

  LIST_FOREACH (CollectionChild *, child, &collection->children) {
    Collection *nested = collection_from_index_recursive(child->collection, index, index_current);
    if (nested != NULL) {
      return nested;
    }
  }
  return NULL;
}

Collection *dune_collection_from_index(Scene *scene, const int index)
{
  int index_current = 0;
  Collection *master_collection = scene->master_collection;
  return collection_from_index_recursive(master_collection, index, &index_current);
}

static bool collection_objects_select(ViewLayer *view_layer, Collection *collection, bool deselect)
{
  bool changed = false;

  if (collection->flag & COLLECTION_HIDE_SELECT) {
    return false;
  }

  LIST_FOREACH (CollectionObject *, cob, &collection->gobject) {
    Base *base = dune_view_layer_base_find(view_layer, cob->ob);

    if (base) {
      if (deselect) {
        if (base->flag & BASE_SELECTED) {
          base->flag &= ~BASE_SELECTED;
          changed = true;
        }
      }
      else {
        if ((base->flag & BASE_SELECTABLE) && !(base->flag & BASE_SELECTED)) {
          base->flag |= BASE_SELECTED;
          changed = true;
        }
      }
    }
  }

  LIST_FOREACH (CollectionChild *, child, &collection->children) {
    if (collection_objects_select(view_layer, collection, deselect)) {
      changed = true;
    }
  }

  return changed;
}

bool dune_collection_objects_select(ViewLayer *view_layer, Collection *collection, bool deselect)
{
  LayerCollection *layer_collection = BKE_layer_collection_first_from_scene_collection(view_layer,
                                                                                       collection);

  if (layer_collection != NULL) {
    return dune_layer_collection_objects_select(view_layer, layer_collection, deselect);
  }

  return collection_objects_select(view_layer, collection, deselect);
}

/* Collection move (outliner drag & drop) */
bool dune_collection_move(Main *main,
                         Collection *to_parent,
                         Collection *from_parent,
                         Collection *relative,
                         bool relative_after,
                         Collection *collection)
{
  if (collection->flag & COLLECTION_IS_MASTER) {
    return false;
  }
  if (dune_collection_cycle_find(to_parent, collection)) {
    return false;
  }

  /* Move to new parent collection */
  if (from_parent) {
    collection_child_remove(from_parent, collection);
  }

  collection_child_add(to_parent, collection, 0, true);

  /* Move to specified location under parent. */
  if (relative) {
    CollectionChild *child = collection_find_child(to_parent, collection);
    CollectionChild *relative_child = collection_find_child(to_parent, relative);

    if (relative_child) {
      lib_remlink(&to_parent->children, child);

      if (relative_after) {
        li _insertlinkafter(&to_parent->children, relative_child, child);
      }
      else {
        lib_insertlinkbefore(&to_parent->children, relative_child, child);
      }

      dune_collection_object_cache_free(to_parent);
    }
  }

  /* Update layer collections. */
  dund_main_collection_sync(main);

  return true;
}

/* Iters */
/* Scene collection iter. */
typedef struct CollectionsIteratorData {
  Scene *scene;
  void **array;
  int tot, cur;
} CollectionsIteratorData;

static void scene_collection_cb(Collection *collection,
                                dune_scene_collections_Cb cb,
                                void *data)
{
  cb(collection, data);

  LIST_FOREACH (CollectionChild *, child, &collection->children) {
    scene_collection_cb(child->collection, cb, data);
  }
}

static void scene_collections_count(Collection *UNUSED(collection), void *data)
{
  int *tot = data;
  (*tot)++;
}

static void scene_collections_build_array(Collection *collection, void *data)
{
  Collection ***array = data;
  **array = collection;
  (*array)++;
}

static void scene_collections_array(Scene *scene,
                                    Collection ***r_collections_array,
                                    int *r_collections_array_len)
{
  *r_collections_array = NULL;
  *r_collections_array_len = 0;

  if (scene == NULL) {
    return;
  }

  Collection *collection = scene->master_collection;
  lib_assert(collection != NULL);
  scene_collection_cb(collection, scene_collections_count, r_collections_array_len);

  lib_assert(*r_collections_array_len > 0);

  Collection **array = mem_malloc_arrayn(
      *r_collections_array_len, sizeof(Collection *), "CollectionArray");
  *r_collections_array = array;
  scene_collection_cb(collection, scene_collections_build_array, &array);
}

void dune_scene_collections_iter_begin(LibIter *iter, void *data_in)
{
  Scene *scene = data_in;
  CollectionsIterData *data = mem_callocn(sizeof(CollectionsIterData), __func__);

  data->scene = scene;

  LIB_ITER_INIT(iter);
  iter->data = data;

  scene_collections_array(scene, (Collection ***)&data->array, &data->tot);
  lib_assert(data->tot != 0);

  data->cur = 0;
  iter->current = data->array[data->cur];
}

void dune_scene_collections_iter_next(struct LibIter *iter)
{
  CollectionsIterData *data = iter->data;

  if (++data->cur < data->tot) {
    iter->current = data->array[data->cur];
  }
  else {
    iter->valid = false;
  }
}

void dune_scene_collections_iter_end(struct LibIter *iter)
{
  CollectionsIterData *data = iter->data;

  if (data) {
    if (data->array) {
      mem_freen(data->array);
    }
    mem_freen(data);
  }
  iter->valid = false;
}

/* scene objects iterator */
typedef struct SceneObjectsIterData {
  GSet *visited;
  CollectionObject *cob_next;
  LibIter scene_collection_iter;
} SceneObjectsIterData;

static void scene_objects_iter_begin(LibIter *iter, Scene *scene, GSet *visited_objects)
{
  SceneObjectsIterData *data = mem_callocn(sizeof(SceneObjectsIterData), __func__);

  LIB_ITER_INIT(iter);
  iter->data = data;

  /* Lookup list to make sure that each object is only processed once. */
  if (visited_objects != NULL) {
    data->visited = visited_objects;
  }
  else {
    data->visited = lib_gset_ptr_new(__func__);
  }

  /* We wrap the scenecollection iterator here to go over the scene collections. */
  dune_scene_collections_iterator_begin(&data->scene_collection_iter, scene);

  Collection *collection = data->scene_collection_iter.current;
  data->cob_next = collection->gobject.first;

  dune_scene_objects_iter_next(iter);
}

void dune_scene_objects_iter_begin(LibIter *iter, void *data_in)
{
  Scene *scene = data_in;

  scene_objects_iter_begin(iter, scene, NULL);
}

/* Ensures we only get each object once, even when included in several collections */
static CollectionObject *object_base_unique(GSet *gs, CollectionObject *cob)
{
  for (; cob != NULL; cob = cob->next) {
    Object *ob = cob->ob;
    void **ob_key_p;
    if (!lib_gset_ensure_p_ex(gs, ob, &ob_key_p)) {
      *ob_key_p = ob;
      return cob;
    }
  }
  return NULL;
}

void dune_scene_objects_iter_next(LibIter *iter)
{
  SceneObjectsIterData *data = iter->data;
  CollectionObject *cob = data->cob_next ? object_base_unique(data->visited, data->cob_next) :
                                           NULL;

  if (cob) {
    data->cob_next = cob->next;
    iter->current = cob->ob;
  }
  else {
    /* if this is the last object of this List look at the next Collection */
    Collection *collection;
    dune_scene_collections_iter_next(&data->scene_collection_iter);
    do {
      collection = data->scene_collection_iter.current;
      /* get the first unique object of this collection */
      CollectionObject *new_cob = object_base_unique(data->visited, collection->gobject.first);
      if (new_cob) {
        data->cob_next = new_cob->next;
        iter->current = new_cob->ob;
        return;
      }
      dune_scene_collections_iter_next(&data->scene_collection_iter);
    } while (data->scene_collection_iter.valid);

    if (!data->scene_collection_iter.valid) {
      iter->valid = false;
    }
  }
}

void dune_scene_objects_iter_end(LibIter *iter)
{
  SceneObjectsIterData *data = iter->data;
  if (data) {
    dune_scene_collections_iter_end(&data->scene_collection_iter);
    if (data->visited != NULL) {
      lib_gset_free(data->visited, NULL);
    }
    mem_freen(data);
  }
}

GSet *dune_scene_objects_as_gset(Scene *scene, GSet *objects_gset)
{
  LibIter iter;
  scene_objects_iter_begin(&iter, scene, objects_gset);
  while (iter.valid) {
    dune_scene_objects_iter_next(&iter);
  }

  /* `return_gset` is either given `objects_gset` (if non-NULL), or the GSet allocated by the
   * iterator. Either way, we want to get it back, and prevent `dune_scene_objects_iter_end`
   * from freeing it. */
  GSet *return_gset = ((SceneObjectsIterData *)iter.data)->visited;
  ((SceneObjectsIterData *)iter.data)->visited = NULL;
  dune_scene_objects_iter_end(&iter);

  return return_gset;
}
