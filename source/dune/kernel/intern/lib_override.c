#include <stdlib.h>
#include <string.h>

#include "CLG_log.h"

#include "MEM_guardedalloc.h"

#include "structs_ID.h"
#include "structs_collection_types.h"
#include "structs_key_types.h"
#include "structs_object_types.h"
#include "structs_scene_types.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

#include "KE_armature.h"
#include "KE_collection.h"
#include "KE_global.h"
#include "KE_idtype.h"
#include "KE_key.h"
#include "KE_layer.h"
#include "KE_lib_id.h"
#include "KE_lib_override.h"
#include "KE_lib_query.h"
#include "KE_lib_remap.h"
#include "KE_main.h"
#include "KE_node.h"
#include "KE_report.h"
#include "KE_scene.h"

#include "LOADER_readfile.h"

#include "LI_ghash.h"
#include "LI_linklist.h"
#include "LI_listbase.h"
#include "LI_memarena.h"
#include "LI_string.h"
#include "LI_task.h"
#include "LI_utildefines.h"

#include "PIL_time.h"

#include "API_access.h"
#include "API_prototypes.h"
#include "API_types.h"

#include "atomic_ops.h"

#define OVERRIDE_AUTO_CHECK_DELAY 0.2 /* 200ms between auto-override checks. */
//#define DEBUG_OVERRIDE_TIMEIT

#ifdef DEBUG_OVERRIDE_TIMEIT
#  include "PIL_time_utildefines.h"
#endif

static CLG_LogRef LOG = {"kernel.liboverride"};

static void lib_override_library_property_copy(IDOverrideLibraryProperty *op_dst,
                                               IDOverrideLibraryProperty *op_src);
static void lib_override_library_property_operation_copy(
    IDOverrideLibraryPropertyOperation *opop_dst, IDOverrideLibraryPropertyOperation *opop_src);

static void lib_override_library_property_clear(IDOverrideLibraryProperty *op);
static void lib_override_library_property_operation_clear(
    IDOverrideLibraryPropertyOperation *opop);

/** Get override data for a given ID. Needed because of our beloved shape keys snowflake. */
BLI_INLINE IDOverrideLibrary *lib_override_get(Main *bmain, ID *id, ID **r_owner_id)
{
  if (r_owner_id != NULL) {
    *r_owner_id = id;
  }
  if (id->flag & LIB_EMBEDDED_DATA_LIB_OVERRIDE) {
    const IDTypeInfo *id_type = KERNEL_idtype_get_info_from_id(id);
    if (id_type->owner_get != NULL) {
      ID *owner_id = id_type->owner_get(dunemain, id);
      if (r_owner_id != NULL) {
        *r_owner_id = owner_id;
      }
      return owner_id->override_library;
    }
    LIB_assert_msg(0, "IDTypeInfo of liboverride-embedded ID with no owner getter");
  }
  return id->override_library;
}

IDOverrideLibrary *KERNEL_lib_override_library_init(ID *local_id, ID *reference_id)
{
  /* If reference_id is NULL, we are creating an override template for purely local data.
   * Else, reference *must* be linked data. */
  LIB_assert(reference_id == NULL || ID_IS_LINKED(reference_id));
  LIB_assert(local_id->override_library == NULL);

  ID *ancestor_id;
  for (ancestor_id = reference_id; ancestor_id != NULL && ancestor_id->override_library != NULL &&
                                   ancestor_id->override_library->reference != NULL;
       ancestor_id = ancestor_id->override_library->reference) {
    /* pass */
  }

  if (ancestor_id != NULL && ancestor_id->override_library != NULL) {
    /* Original ID has a template, use it! */
    KERNEL_lib_override_library_copy(local_id, ancestor_id, true);
    if (local_id->override_library->reference != reference_id) {
      id_us_min(local_id->override_library->reference);
      local_id->override_library->reference = reference_id;
      id_us_plus(local_id->override_library->reference);
    }
    return local_id->override_library;
  }

  /* Else, generate new empty override. */
  local_id->override_library = MEM_callocN(sizeof(*local_id->override_library), __func__);
  local_id->override_library->reference = reference_id;
  id_us_plus(local_id->override_library->reference);
  local_id->tag &= ~LIB_TAG_OVERRIDE_LIBRARY_REFOK;
  /* TODO: do we want to add tag or flag to referee to mark it as such? */
  return local_id->override_library;
}

void KERNEL_lib_override_library_copy(ID *dst_id, const ID *src_id, const bool do_full_copy)
{
  LIB_assert(ID_IS_OVERRIDE_LIBRARY(src_id) || ID_IS_OVERRIDE_LIBRARY_TEMPLATE(src_id));

  if (dst_id->override_library != NULL) {
    if (src_id->override_library == NULL) {
      KERNEL_lib_override_library_free(&dst_id->override_library, true);
      return;
    }

    KERNEL_lib_override_library_clear(dst_id->override_library, true);
  }
  else if (src_id->override_library == NULL) {
    /* Virtual overrides of embedded data does not require any extra work. */
    return;
  }
  else {
    KERNEL_lib_override_library_init(dst_id, NULL);
  }

  /* If source is already overriding data, we copy it but reuse its reference for dest ID.
   * Otherwise, source is only an override template, it then becomes reference of dest ID. */
  dst_id->override_library->reference = src_id->override_library->reference ?
                                            src_id->override_library->reference :
                                            (ID *)src_id;
  id_us_plus(dst_id->override_library->reference);

  dst_id->override_library->hierarchy_root = src_id->override_library->hierarchy_root;
  dst_id->override_library->flag = src_id->override_library->flag;

  if (do_full_copy) {
    LIB_duplicatelist(&dst_id->override_library->properties,
                      &src_id->override_library->properties);
    for (IDOverrideLibraryProperty *op_dst = dst_id->override_library->properties.first,
                                   *op_src = src_id->override_library->properties.first;
         op_dst;
         op_dst = op_dst->next, op_src = op_src->next) {
      lib_override_library_property_copy(op_dst, op_src);
    }
  }

  dst_id->tag &= ~LIB_TAG_OVERRIDE_LIBRARY_REFOK;
}

void KERNEL_lib_override_library_clear(IDOverrideLibrary *override, const bool do_id_user)
{
  LIB_assert(override != NULL);

  if (!ELEM(NULL, override->runtime, override->runtime->api_path_to_override_properties)) {
    LIB_ghash_clear(override->runtime->api_path_to_override_properties, NULL, NULL);
  }

  LISTBASE_FOREACH (IDOverrideLibraryProperty *, op, &override->properties) {
    lib_override_library_property_clear(op);
  }
  LIB_freelistN(&override->properties);

  if (do_id_user) {
    id_us_min(override->reference);
    /* override->storage should never be refcounted... */
  }
}

void KERNEL_lib_override_library_free(struct IDOverrideLibrary **override, const bool do_id_user)
{
  LIB_assert(*override != NULL);

  if ((*override)->runtime != NULL) {
    if ((*override)->runtime->api_path_to_override_properties != NULL) {
      LIB_ghash_free((*override)->runtime->rna_path_to_override_properties, NULL, NULL);
    }
    MEM_SAFE_FREE((*override)->runtime);
  }

  KERNEL_lib_override_library_clear(*override, do_id_user);
  MEM_freeN(*override);
  *override = NULL;
}

static ID *lib_override_library_create_from(Main *dunemain,
                                            Library *owner_library,
                                            ID *reference_id,
                                            const int lib_id_copy_flags)
{
  /* NOTE: We do not want to copy possible override data from reference here (whether it is an
   * override template, or already an override of some other ref data). */
  ID *local_id = KERNEL_id_copy_ex(dunemain,
                                reference_id,
                                NULL,
                                LIB_ID_COPY_DEFAULT | LIB_ID_COPY_NO_LIB_OVERRIDE |
                                    lib_id_copy_flags);

  if (local_id == NULL) {
    return NULL;
  }
  id_us_min(local_id);

  /* TODO: Handle this properly in LIB_NO_MAIN case as well (i.e. resync case). Or offload to
   * generic ID copy code? */
  if ((lib_id_copy_flags & LIB_ID_CREATE_NO_MAIN) == 0) {
    local_id->lib = owner_library;
  }

  KERNEL_lib_override_library_init(local_id, reference_id);

  /* NOTE: From liboverride perspective (and API one), shape keys are considered as local embedded
   * data-blocks, just like root node trees or master collections. Therefore, we never need to
   * create overrides for them. We need a way to mark them as overrides though. */
  Key *reference_key;
  if ((reference_key = KERNEL_key_from_id(reference_id)) != NULL) {
    Key *local_key = KERNEL_key_from_id(local_id);
    LIB_assert(local_key != NULL);
    local_key->id.flag |= LIB_EMBEDDED_DATA_LIB_OVERRIDE;
  }

  return local_id;
}

/* TODO: This could be simplified by storing a flag in #IDOverrideLibrary
 * during the diffing process? */
bool KERNEL_lib_override_library_is_user_edited(struct ID *id)
{

  if (!ID_IS_OVERRIDE_LIBRARY(id)) {
    return false;
  }

  /* A bit weird, but those embedded IDs are handled by their owner ID anyway, so we can just
   * assume they are never user-edited, actual proper detection will happen from their owner check.
   */
  if (!ID_IS_OVERRIDE_LIBRARY_REAL(id)) {
    return false;
  }

  LISTBASE_FOREACH (IDOverrideLibraryProperty *, op, &id->override_library->properties) {
    LISTBASE_FOREACH (IDOverrideLibraryPropertyOperation *, opop, &op->operations) {
      if ((opop->flag & IDOVERRIDE_LIBRARY_FLAG_IDPOINTER_MATCH_REFERENCE) != 0) {
        continue;
      }
      if (opop->operation == IDOVERRIDE_LIBRARY_OP_NOOP) {
        continue;
      }
      /* If an operation does not match the filters above, it is considered as a user-editing one,
       * therefore this override is user-edited. */
      return true;
    }
  }
  return false;
}

ID *KERNEL_lib_override_library_create_from_id(Main *dunemain,
                                            ID *reference_id,
                                            const bool do_tagged_remap)
{
  LIB_assert(reference_id != NULL);
  LIB_assert(ID_IS_LINKED(reference_id));

  ID *local_id = lib_override_library_create_from(dunemain, NULL, reference_id, 0);
  /* We cannot allow automatic hierarchy resync on this ID, it is highly likely to generate a giant
   * mess in case there are a lot of hidden, non-instantiated, non-properly organized dependencies.
   * Ref T94650. */
  local_id->override_library->flag |= IDOVERRIDE_LIBRARY_FLAG_NO_HIERARCHY;
  local_id->override_library->hierarchy_root = local_id;

  if (do_tagged_remap) {
    Key *reference_key, *local_key = NULL;
    if ((reference_key = KERNEL_key_from_id(reference_id)) != NULL) {
      local_key = KERNEL_key_from_id(local_id);
      LIB_assert(local_key != NULL);
    }

    ID *other_id;
    FOREACH_MAIN_ID_BEGIN (bmain, other_id) {
      if ((other_id->tag & LIB_TAG_DOIT) != 0 && !ID_IS_LINKED(other_id)) {
        /* Note that using ID_REMAP_SKIP_INDIRECT_USAGE below is superfluous, as we only remap
         * local IDs usages anyway. */
        KERNEL_libblock_relink_ex(dunemain,
                               other_id,
                               reference_id,
                               local_id,
                               ID_REMAP_SKIP_INDIRECT_USAGE | ID_REMAP_SKIP_OVERRIDE_LIBRARY);
        if (reference_key != NULL) {
          KERNEL_libblock_relink_ex(dunemain,
                                 other_id,
                                 &reference_key->id,
                                 &local_key->id,
                                 ID_REMAP_SKIP_INDIRECT_USAGE | ID_REMAP_SKIP_OVERRIDE_LIBRARY);
        }
      }
    }
    FOREACH_MAIN_ID_END;
  }

  return local_id;
}

/* TODO: Make this static local function instead? API is becoming complex, and it's not used
 * outside of this file anyway. */
bool KERNEL_lib_override_library_create_from_tag(Main *dunemain,
                                              Library *owner_library,
                                              const ID *id_root_reference,
                                              ID *id_hierarchy_root,
                                              const ID *id_hierarchy_root_reference,
                                              const bool do_no_main)
{
  LIB_assert(id_root_reference != NULL && ID_IS_LINKED(id_root_reference));
  /* If we do not have any hierarchy root given, then the root reference must be tagged for
   * override. */
  LIB_assert(id_hierarchy_root != NULL || id_hierarchy_root_reference != NULL ||
             (id_root_reference->tag & LIB_TAG_DOIT) != 0);
  /* At least one of the hierarchy root pointers must be NULL, passing both is useless and can
   * create confusion. */
  LIB_assert(ELEM(NULL, id_hierarchy_root, id_hierarchy_root_reference));

  if (id_hierarchy_root != NULL) {
    /* If the hierarchy root is given, it must be a valid existing override (used during partial
     * resync process mainly). */
    LIB_assert((ID_IS_OVERRIDE_LIBRARY_REAL(id_hierarchy_root) &&
                id_hierarchy_root->override_library->reference->lib == id_root_reference->lib));
  }
  if (!ELEM(id_hierarchy_root_reference, NULL, id_root_reference)) {
    /* If the reference hierarchy root is given, it must be from the same library as the reference
     * root, and also tagged for override. */
    LIB_assert((id_hierarchy_root_reference->lib == id_root_reference->lib &&
                (id_hierarchy_root_reference->tag & LIB_TAG_DOIT) != 0));
  }

  const Library *reference_library = id_root_reference->lib;

  ID *reference_id;
  bool success = true;

  ListBase todo_ids = {NULL};
  LinkData *todo_id_iter;

  /* Get all IDs we want to override. */
  FOREACH_MAIN_ID_BEGIN (dunemain, reference_id) {
    if ((reference_id->tag & LIB_TAG_DOIT) != 0 && reference_id->lib == reference_library &&
        KERNEL_idtype_idcode_is_linkable(GS(reference_id->name))) {
      todo_id_iter = MEM_callocN(sizeof(*todo_id_iter), __func__);
      todo_id_iter->data = reference_id;
      LIB_addtail(&todo_ids, todo_id_iter);
    }
  }
  FOREACH_MAIN_ID_END;

  /* Override the IDs. */
  for (todo_id_iter = todo_ids.first; todo_id_iter != NULL; todo_id_iter = todo_id_iter->next) {
    reference_id = todo_id_iter->data;

    /* If `newid` is already set, assume it has been handled by calling code.
     * Only current use case: re-using proxy ID when converting to liboverride. */
    if (reference_id->newid == NULL) {
      /* NOTE: `no main` case is used during resync procedure, to support recursive resync.
       * This requires extra care further down the resync process,
       * see: KERNEL_lib_override_library_resync. */
      reference_id->newid = lib_override_library_create_from(
          dunemain, owner_library, reference_id, do_no_main ? LIB_ID_CREATE_NO_MAIN : 0);
      if (reference_id->newid == NULL) {
        success = false;
        break;
      }
    }
    /* We also tag the new IDs so that in next step we can remap their pointers too. */
    reference_id->newid->tag |= LIB_TAG_DOIT;

    Key *reference_key;
    if ((reference_key = KERNEL_key_from_id(reference_id)) != NULL) {
      reference_key->id.tag |= LIB_TAG_DOIT;

      Key *local_key = KERNEL_key_from_id(reference_id->newid);
      LIB_assert(local_key != NULL);
      reference_key->id.newid = &local_key->id;
      /* We also tag the new IDs so that in next step we can remap their pointers too. */
      local_key->id.tag |= LIB_TAG_DOIT;
    }
  }

  /* Only remap new local ID's pointers, we don't want to force our new overrides onto our whole
   * existing linked IDs usages. */
  if (success) {
    if (id_hierarchy_root_reference != NULL) {
      id_hierarchy_root = id_hierarchy_root_reference->newid;
    }
    else if (id_root_reference->newid != NULL &&
             (id_hierarchy_root == NULL ||
              id_hierarchy_root->override_library->reference == id_root_reference)) {
      id_hierarchy_root = id_root_reference->newid;
    }
    LIB_assert(id_hierarchy_root != NULL);

    LinkNode *relinked_ids = NULL;
    /* Still checking the whole Main, that way we can tag other local IDs as needing to be
     * remapped to use newly created overriding IDs, if needed. */
    ID *id;
    FOREACH_MAIN_ID_BEGIN (dunemain, id) {
      ID *other_id;
      /* In case we created new overrides as 'no main', they are not accessible directly in this
       * loop, but we can get to them through their reference's `newid` pointer. */
      if (do_no_main && id->lib == id_root_reference->lib && id->newid != NULL) {
        other_id = id->newid;
        /* Otherwise we cannot properly distinguish between IDs that are actually from the
         * linked library (and should not be remapped), and IDs that are overrides re-generated
         * from the reference from the linked library, and must therefore be remapped.
         *
         * This is reset afterwards at the end of this loop. */
        other_id->lib = NULL;
      }
      else {
        other_id = id;
      }

      /* If other ID is a linked one, but not from the same library as our reference, then we
       * consider we should also relink it, as part of recursive resync. */
      if ((other_id->tag & LIB_TAG_DOIT) != 0 && other_id->lib != id_root_reference->lib) {
        LIB_linklist_prepend(&relinked_ids, other_id);
      }
      if (other_id != id) {
        other_id->lib = id_root_reference->lib;
      }
    }
    FOREACH_MAIN_ID_END;

    struct IDRemapper *id_remapper = KERNEL_id_remapper_create();
    for (todo_id_iter = todo_ids.first; todo_id_iter != NULL; todo_id_iter = todo_id_iter->next) {
      reference_id = todo_id_iter->data;
      ID *local_id = reference_id->newid;

      if (local_id == NULL) {
        continue;
      }

      local_id->override_library->hierarchy_root = id_hierarchy_root;

      KERNEL_id_remapper_add(id_remapper, reference_id, local_id);

      Key *reference_key, *local_key = NULL;
      if ((reference_key = KERNEL_key_from_id(reference_id)) != NULL) {
        local_key = KERNEL_key_from_id(reference_id->newid);
        LIB_assert(local_key != NULL);

        KERNEL_id_remapper_add(id_remapper, &reference_key->id, &local_key->id);
      }
    }

    BKE_libblock_relink_multiple(bmain,
                                 relinked_ids,
                                 ID_REMAP_TYPE_REMAP,
                                 id_remapper,
                                 ID_REMAP_SKIP_OVERRIDE_LIBRARY | ID_REMAP_FORCE_USER_REFCOUNT);

    KERNEL_id_remapper_free(id_remapper);
    LIB_linklist_free(relinked_ids, NULL);
  }
  else {
    /* We need to cleanup potentially already created data. */
    for (todo_id_iter = todo_ids.first; todo_id_iter != NULL; todo_id_iter = todo_id_iter->next) {
      reference_id = todo_id_iter->data;
      KERNEL_id_delete(dunemain, reference_id->newid);
      reference_id->newid = NULL;
    }
  }

  LIB_freelistN(&todo_ids);

  return success;
}

typedef struct LibOverrideGroupTagData {
  Main *dunemain;
  Scene *scene;
  ID *id_root;
  ID *hierarchy_root_id;
  uint tag;
  uint missing_tag;
  /* Whether we are looping on override data, or their references (linked) one. */
  bool is_override;
  /* Whether we are creating new override, or resyncing existing one. */
  bool is_resync;

  /* Mapping linked objects to all their instantiating collections (as a linked list).
   * Avoids calling KERNEL_collection_object_find over and over, this function is very expansive. */
  GHash *linked_object_to_instantiating_collections;
  MemArena *mem_arena;
} LibOverrideGroupTagData;

static void lib_override_group_tag_data_object_to_collection_init_collection_process(
    LibOverrideGroupTagData *data, Collection *collection)
{
  LISTBASE_FOREACH (CollectionObject *, collection_object, &collection->gobject) {
    Object *ob = collection_object->ob;
    if (!ID_IS_LINKED(ob)) {
      continue;
    }

    LinkNodePair **collections_linkedlist_p;
    if (!LIB_ghash_ensure_p(data->linked_object_to_instantiating_collections,
                            ob,
                            (void ***)&collections_linkedlist_p)) {
      *collections_linkedlist_p = LIB_memarena_calloc(data->mem_arena,
                                                      sizeof(**collections_linkedlist_p));
    }
    LIB_linklist_append_arena(*collections_linkedlist_p, collection, data->mem_arena);
  }
}

/* Initialize complex data, `data` is expected to be already initialized with basic pointers and
 * other simple data.
 *
 * NOTE: Currently creates a mapping from linked object to all of their instantiating collections
 * (as returned by KERNEL_collection_object_find). */
static void lib_override_group_tag_data_object_to_collection_init(LibOverrideGroupTagData *data)
{
  data->mem_arena = LIB_memarena_new(LIB_MEMARENA_STD_BUFSIZE, __func__);

  data->linked_object_to_instantiating_collections = LIB_ghash_new(
      LIB_ghashutil_ptrhash, LIB_ghashutil_ptrcmp, __func__);
  if (data->scene != NULL) {
    lib_override_group_tag_data_object_to_collection_init_collection_process(
        data, data->scene->master_collection);
  }
  LISTBASE_FOREACH (Collection *, collection, &data->dunemain->collections) {
    lib_override_group_tag_data_object_to_collection_init_collection_process(data, collection);
  }
}

static void lib_override_group_tag_data_clear(LibOverrideGroupTagData *data)
{
  LIB_ghash_free(data->linked_object_to_instantiating_collections, NULL, NULL);
  LIB_memarena_free(data->mem_arena);
  memset(data, 0, sizeof(*data));
}

/* Tag all IDs in dependency relationships within an override hierarchy/group.
 *
 * Requires existing `Main.relations`.
 *
 * NOTE: This is typically called to complete `lib_override_linked_group_tag()`.
 */
static bool lib_override_hierarchy_dependencies_recursive_tag(LibOverrideGroupTagData *data)
{
  Main *bmain = data->dunemain;
  ID *id = data->id_root;
  const bool is_override = data->is_override;

  MainIDRelationsEntry *entry = LIB_ghash_lookup(dunemain->relations->relations_from_pointers, id);
  BLI_assert(entry != NULL);

  if (entry->tags & MAINIDRELATIONS_ENTRY_TAGS_PROCESSED) {
    /* This ID has already been processed. */
    return (*(uint *)&id->tag & data->tag) != 0;
  }
  /* This way we won't process again that ID, should we encounter it again through another
   * relationship hierarchy. */
  entry->tags |= MAINIDRELATIONS_ENTRY_TAGS_PROCESSED;

  for (MainIDRelationsEntryItem *to_id_entry = entry->to_ids; to_id_entry != NULL;
       to_id_entry = to_id_entry->next) {
    if ((to_id_entry->usage_flag & IDWALK_CB_OVERRIDE_LIBRARY_NOT_OVERRIDABLE) != 0) {
      /* Never consider non-overridable relationships ('from', 'parents', 'owner' etc. pointers) as
       * actual dependencies. */
      continue;
    }
    /* We only consider IDs from the same library. */
    ID *to_id = *to_id_entry->id_pointer.to;
    if (to_id == NULL || to_id->lib != id->lib ||
        (is_override && !ID_IS_OVERRIDE_LIBRARY(to_id))) {
      /* IDs from different libraries, or non-override IDs in case we are processing overrides, are
       * both barriers of dependency. */
      continue;
    }
    LibOverrideGroupTagData sub_data = *data;
    sub_data.id_root = to_id;
    if (lib_override_hierarchy_dependencies_recursive_tag(&sub_data)) {
      id->tag |= data->tag;
    }
  }

  return (*(uint *)&id->tag & data->tag) != 0;
}

static void lib_override_linked_group_tag_recursive(LibOverrideGroupTagData *data)
{
  Main *dunemain = data->dunemain;
  ID *id_owner = data->id_root;
  LIB_assert(ID_IS_LINKED(id_owner));
  LIB_assert(!data->is_override);
  const uint tag = data->tag;
  const uint missing_tag = data->missing_tag;

  MainIDRelationsEntry *entry = LIB_ghash_lookup(dunemain->relations->relations_from_pointers,
                                                 id_owner);
  LIB_assert(entry != NULL);

  if (entry->tags & MAINIDRELATIONS_ENTRY_TAGS_PROCESSED) {
    /* This ID has already been processed. */
    return;
  }
  /* This way we won't process again that ID, should we encounter it again through another
   * relationship hierarchy. */
  entry->tags |= MAINIDRELATIONS_ENTRY_TAGS_PROCESSED;

  for (MainIDRelationsEntryItem *to_id_entry = entry->to_ids; to_id_entry != NULL;
       to_id_entry = to_id_entry->next) {
    if ((to_id_entry->usage_flag & IDWALK_CB_OVERRIDE_LIBRARY_NOT_OVERRIDABLE) != 0) {
      /* Never consider non-overridable relationships as actual dependencies. */
      continue;
    }

    ID *to_id = *to_id_entry->id_pointer.to;
    if (ELEM(to_id, NULL, id_owner)) {
      continue;
    }
    /* We only consider IDs from the same library. */
    if (to_id->lib != id_owner->lib) {
      continue;
    }
    LIB_assert(ID_IS_LINKED(to_id));

    /* We tag all collections and objects for override. And we also tag all other data-blocks which
     * would use one of those.
     * NOTE: missing IDs (aka placeholders) are never overridden. */
    if (ELEM(GS(to_id->name), ID_OB, ID_GR)) {
      if (to_id->tag & LIB_TAG_MISSING) {
        to_id->tag |= missing_tag;
      }
      else {
        to_id->tag |= tag;
      }
    }

    /* Recursively process the dependencies. */
    LibOverrideGroupTagData sub_data = *data;
    sub_data.id_root = to_id;
    lib_override_linked_group_tag_recursive(&sub_data);
  }
}

static bool lib_override_linked_group_tag_collections_keep_tagged_check_recursive(
    LibOverrideGroupTagData *data, Collection *collection)
{
  /* NOTE: Collection's object cache (using bases, as returned by KERNEL_collection_object_cache_get)
   * is not usable here, as it may have become invalid from some previous operation and it should
   * not be updated here. So instead only use collections' reliable 'raw' data to check if some
   * object in the hierarchy of the given collection is still tagged for override. */
  for (CollectionObject *collection_object = collection->gobject.first; collection_object != NULL;
       collection_object = collection_object->next) {
    Object *object = collection_object->ob;
    if (object == NULL) {
      continue;
    }
    if ((object->id.tag & data->tag) != 0) {
      return true;
    }
  }

  for (CollectionChild *collection_child = collection->children.first; collection_child != NULL;
       collection_child = collection_child->next) {
    if (lib_override_linked_group_tag_collections_keep_tagged_check_recursive(
            data, collection_child->collection)) {
      return true;
    }
  }

  return false;
}

static void lib_override_linked_group_tag_clear_boneshapes_objects(LibOverrideGroupTagData *data)
{
  Main *dunemain = data->dunemain;

  /* Remove (untag) bone shape objects, they shall never need to be to directly/explicitly
   * overridden. */
  LISTBASE_FOREACH (Object *, ob, &bmain->objects) {
    if (ob->type == OB_ARMATURE && ob->pose != NULL && (ob->id.tag & data->tag)) {
      for (dunePoseChannel *pchan = ob->pose->chanbase.first; pchan != NULL; pchan = pchan->next) {
        if (pchan->custom != NULL) {
          pchan->custom->id.tag &= ~data->tag;
        }
      }
    }
  }

  /* Remove (untag) collections if they do not own any tagged object (either themselves, or in
   * their children collections). */
  LISTBASE_FOREACH (Collection *, collection, &dunemain->collections) {
    if ((collection->id.tag & data->tag) == 0) {
      continue;
    }

    if (!lib_override_linked_group_tag_collections_keep_tagged_check_recursive(data, collection)) {
      collection->id.tag &= ~data->tag;
    }
  }
}

/* This will tag at least all 'boundary' linked IDs for a potential override group.
 *
 * Requires existing `Main.relations`.
 *
 * Note that you will then need to call #lib_override_hierarchy_dependencies_recursive_tag to
 * complete tagging of all dependencies within the override group.
 *
 * We currently only consider Collections and Objects (that are not used as bone shapes) as valid
 * boundary IDs to define an override group.
 */
static void lib_override_linked_group_tag(LibOverrideGroupTagData *data)
{
  Main *dunemain = data->dunemain;
  ID *id_root = data->id_root;
  const bool is_resync = data->is_resync;
  LIB_assert(!data->is_override);

  if (id_root->tag & LIB_TAG_MISSING) {
    id_root->tag |= data->missing_tag;
  }
  else {
    id_root->tag |= data->tag;
  }

  /* Only objects and groups are currently considered as 'keys' in override hierarchies. */
  if (!ELEM(GS(id_root->name), ID_OB, ID_GR)) {
    return;
  }

  /* Tag all collections and objects recursively. */
  lib_override_linked_group_tag_recursive(data);

  /* Do not override objects used as bone shapes, nor their collections if possible. */
  lib_override_linked_group_tag_clear_boneshapes_objects(data);

  /* For each object tagged for override, ensure we get at least one local or liboverride
   * collection to host it. Avoids getting a bunch of random object in the scene's master
   * collection when all objects' dependencies are not properly 'packed' into a single root
   * collection.
   *
   * NOTE: In resync case, we do not handle this at all, since:
   *         - In normal, valid cases nothing would be needed anyway (resync process takes care
   *           of tagging needed 'owner' collection then).
   *         - Partial resync makes it extremely difficult to properly handle such extra
   *           collection 'tagging for override' (since one would need to know if the new object
   *           is actually going to replace an already existing override [most common case], or
   *           if it is actually a real new 'orphan' one).
   *         - While not ideal, having objects dangling around is less critical than both points
   *           above.
   *        So if users add new objects to their library override hierarchy in an invalid way, so
   *        be it. Trying to find a collection to override and host this new object would most
   *        likely make existing override very unclean anyway. */
  if (is_resync) {
    return;
  }
  LISTBASE_FOREACH (Object *, ob, &dunemain->objects) {
    if (ID_IS_LINKED(ob) && (ob->id.tag & data->tag) != 0) {
      Collection *instantiating_collection = NULL;
      Collection *instantiating_collection_override_candidate = NULL;
      /* Loop over all collections instantiating the object, if we already have a 'locale' one we
       * have nothing to do, otherwise try to find a 'linked' one that we can override too. */
      LinkNodePair *instantiating_collection_linklist = LIB_ghash_lookup(
          data->linked_object_to_instantiating_collections, ob);
      if (instantiating_collection_linklist != NULL) {
        for (LinkNode *instantiating_collection_linknode = instantiating_collection_linklist->list;
             instantiating_collection_linknode != NULL;
             instantiating_collection_linknode = instantiating_collection_linknode->next) {
          instantiating_collection = instantiating_collection_linknode->link;
          if (!ID_IS_LINKED(instantiating_collection)) {
            /* There is a local collection instantiating the linked object to override, nothing
             * else to be done here. */
            break;
          }
          if (instantiating_collection->id.tag & data->tag) {
            /* There is a linked collection instantiating the linked object to override,
             * already tagged to be overridden, nothing else to be done here. */
            break;
          }
          instantiating_collection_override_candidate = instantiating_collection;
          instantiating_collection = NULL;
        }
      }

      if (instantiating_collection == NULL &&
          instantiating_collection_override_candidate != NULL) {
        if (instantiating_collection_override_candidate->id.tag & LIB_TAG_MISSING) {
          instantiating_collection_override_candidate->id.tag |= data->missing_tag;
        }
        else {
          instantiating_collection_override_candidate->id.tag |= data->tag;
        }
      }
    }
  }
}

static void lib_override_overrides_group_tag_recursive(LibOverrideGroupTagData *data)
{
  Main *dunemain = data->dunemain;
  ID *id_owner = data->id_root;
  LIB_assert(ID_IS_OVERRIDE_LIBRARY(id_owner));
  LIB_assert(data->is_override);

  ID *id_hierarchy_root = data->hierarchy_root_id;

  if (ID_IS_OVERRIDE_LIBRARY_REAL(id_owner) &&
      (id_owner->override_library->flag & IDOVERRIDE_LIBRARY_FLAG_NO_HIERARCHY) != 0) {
    return;
  }

  const uint tag = data->tag;
  const uint missing_tag = data->missing_tag;

  MainIDRelationsEntry *entry = LIB_ghash_lookup(dunemain->relations->relations_from_pointers,
                                                 id_owner);
  LIB_assert(entry != NULL);

  if (entry->tags & MAINIDRELATIONS_ENTRY_TAGS_PROCESSED) {
    /* This ID has already been processed. */
    return;
  }
  /* This way we won't process again that ID, should we encounter it again through another
   * relationship hierarchy. */
  entry->tags |= MAINIDRELATIONS_ENTRY_TAGS_PROCESSED;

  for (MainIDRelationsEntryItem *to_id_entry = entry->to_ids; to_id_entry != NULL;
       to_id_entry = to_id_entry->next) {
    if ((to_id_entry->usage_flag & IDWALK_CB_OVERRIDE_LIBRARY_NOT_OVERRIDABLE) != 0) {
      /* Never consider non-overridable relationships as actual dependencies. */
      continue;
    }

    ID *to_id = *to_id_entry->id_pointer.to;
    if (ELEM(to_id, NULL, id_owner)) {
      continue;
    }
    /* Different libraries or different hierarchy roots are break points in override hierarchies.
     */
    if (!ID_IS_OVERRIDE_LIBRARY(to_id) || (to_id->lib != id_owner->lib)) {
      continue;
    }
    if (ID_IS_OVERRIDE_LIBRARY_REAL(to_id) &&
        to_id->override_library->hierarchy_root != id_hierarchy_root) {
      continue;
    }

    Library *reference_lib = lib_override_get(dunemain, id_owner, NULL)->reference->lib;
    ID *to_id_reference = lib_override_get(dunemain, to_id, NULL)->reference;
    if (to_id_reference->lib != reference_lib) {
      /* We do not override data-blocks from other libraries, nor do we process them. */
      continue;
    }

    if (to_id_reference->tag & LIB_TAG_MISSING) {
      to_id->tag |= missing_tag;
    }
    else {
      to_id->tag |= tag;
    }

    /* Recursively process the dependencies. */
    LibOverrideGroupTagData sub_data = *data;
    sub_data.id_root = to_id;
    lib_override_overrides_group_tag_recursive(&sub_data);
  }
}

/* This will tag all override IDs of an override group defined by the given `id_root`. */
static void lib_override_overrides_group_tag(LibOverrideGroupTagData *data)
{
  ID *id_root = data->id_root;
  LIB_assert(ID_IS_OVERRIDE_LIBRARY_REAL(id_root));
  LIB_assert(data->is_override);

  ID *id_hierarchy_root = data->hierarchy_root_id;
  LIB_assert(id_hierarchy_root != NULL);
  LIB_assert(ID_IS_OVERRIDE_LIBRARY_REAL(id_hierarchy_root));
  UNUSED_VARS_NDEBUG(id_hierarchy_root);

  if (id_root->override_library->reference->tag & LIB_TAG_MISSING) {
    id_root->tag |= data->missing_tag;
  }
  else {
    id_root->tag |= data->tag;
  }

  /* Tag all local overrides in id_root's group. */
  lib_override_overrides_group_tag_recursive(data);
}

static bool lib_override_library_create_do(Main *dunemain,
                                           Scene *scene,
                                           Library *owner_library,
                                           ID *id_root_reference,
                                           ID *id_hierarchy_root_reference)
{
  BKE_main_relations_create(bmain, 0);
  LibOverrideGroupTagData data = {.dunemain = dunemain,
                                  .scene = scene,
                                  .id_root = id_root_reference,
                                  .tag = LIB_TAG_DOIT,
                                  .missing_tag = LIB_TAG_MISSING,
                                  .is_override = false,
                                  .is_resync = false};
  lib_override_group_tag_data_object_to_collection_init(&data);
  lib_override_linked_group_tag(&data);

  KERNEL_main_relations_tag_set(dunemain, MAINIDRELATIONS_ENTRY_TAGS_PROCESSED, false);
  lib_override_hierarchy_dependencies_recursive_tag(&data);

  KERNEL_main_relations_free(dunemain);
  lib_override_group_tag_data_clear(&data);

  bool success = false;
  if (id_hierarchy_root_reference->lib != id_root_reference->lib) {
    LIB_assert(ID_IS_OVERRIDE_LIBRARY_REAL(id_hierarchy_root_reference));
    LIB_assert(id_hierarchy_root_reference->override_library->reference->lib ==
               id_root_reference->lib);
    success = KERNEL_lib_override_library_create_from_tag(
        dunemain, owner_library, id_root_reference, id_hierarchy_root_reference, NULL, false);
  }
  else {
    success = KERNEL_lib_override_library_create_from_tag(
        dunemain, owner_library, id_root_reference, NULL, id_hierarchy_root_reference, false);
  }

  return success;
}

static void lib_override_library_create_post_process(Main *dunemain,
                                                     Scene *scene,
                                                     ViewLayer *view_layer,
                                                     const Library *owner_library,
                                                     ID *id_root,
                                                     ID *id_instance_hint,
                                                     Collection *residual_storage,
                                                     const bool is_resync)
{
  /* NOTE: We only care about local IDs here, if a linked object is not instantiated in any way we
   * do not do anything about it. */

  /* We need to use the `_remap` version here as we prevented any LayerCollection resync during the
   * whole liboverride resyncing, which involves a lot of ID remapping.
   *
   * Otherwise, cached Base GHash e.g. can contain invalid stale data. */
  KERNEL_main_collection_sync_remap(dunemain);

  /* We create a set of all objects referenced into the scene by its hierarchy of collections.
   * NOTE: This is different that the list of bases, since objects in excluded collections etc.
   * won't have a base, but are still considered as instanced from our point of view. */
  GSet *all_objects_in_scene = KERNEL_scene_objects_as_gset(scene, NULL);

  /* Instantiating the root collection or object should never be needed in resync case, since the
   * old override would be remapped to the new one. */
  if (!is_resync && id_root != NULL && id_root->newid != NULL &&
      (!ID_IS_LINKED(id_root->newid) || id_root->newid->lib == owner_library)) {
    switch (GS(id_root->name)) {
      case ID_GR: {
        Object *ob_reference = id_instance_hint != NULL && GS(id_instance_hint->name) == ID_OB ?
                                   (Object *)id_instance_hint :
                                   NULL;
        Collection *collection_new = ((Collection *)id_root->newid);
        if (is_resync && KERNEL_collection_is_in_scene(collection_new)) {
          break;
        }
        if (ob_reference != NULL) {
          KERNEL_collection_add_from_object(dunemain, scene, ob_reference, collection_new);
        }
        else if (id_instance_hint != NULL) {
          LIB_assert(GS(id_instance_hint->name) == ID_GR);
          KERNEL_collection_add_from_collection(
              dunemain, scene, ((Collection *)id_instance_hint), collection_new);
        }
        else {
          KERNEL_collection_add_from_collection(
              dunemain, scene, ((Collection *)id_root), collection_new);
        }

        LIB_assert(KERNEL_collection_is_in_scene(collection_new));

        all_objects_in_scene = KERNEL_scene_objects_as_gset(scene, all_objects_in_scene);
        break;
      }
      case ID_OB: {
        Object *ob_new = (Object *)id_root->newid;
        if (LIB_gset_lookup(all_objects_in_scene, ob_new) == NULL) {
          KERNEL_collection_object_add_from(dunemain, scene, (Object *)id_root, ob_new);
          all_objects_in_scene = KERNEL_scene_objects_as_gset(scene, all_objects_in_scene);
        }
        break;
      }
      default:
        break;
    }
  }

  /* We need to ensure all new overrides of objects are properly instantiated. */
  Collection *default_instantiating_collection = residual_storage;
  LISTBASE_FOREACH (Object *, ob, &bmain->objects) {
    Object *ob_new = (Object *)ob->id.newid;
    if (ob_new == NULL || (ID_IS_LINKED(ob_new) && ob_new->id.lib != owner_library)) {
      continue;
    }

    LIB_assert(ob_new->id.override_library != NULL &&
               ob_new->id.override_library->reference == &ob->id);

    if (LIB_gset_lookup(all_objects_in_scene, ob_new) == NULL) {
      if (id_root != NULL && default_instantiating_collection == NULL) {
        ID *id_ref = id_root->newid != NULL ? id_root->newid : id_root;
        switch (GS(id_ref->name)) {
          case ID_GR: {
            /* Adding the object to a specific collection outside of the root overridden one is a
             * fairly bad idea (it breaks the override hierarchy concept). But there is no other
             * way to do this currently (we cannot add new collections to overridden root one,
             * this is not currently supported).
             * Since that will be fairly annoying and noisy, only do that in case the override
             * object is not part of any existing collection (i.e. its user count is 0). In
             * practice this should never happen I think. */
            if (ID_REAL_USERS(ob_new) != 0) {
              continue;
            }
            default_instantiating_collection = KERNEL_collection_add(
                dunemain, (Collection *)id_root, "OVERRIDE_HIDDEN");
            /* Hide the collection from viewport and render. */
            default_instantiating_collection->flag |= COLLECTION_HIDE_VIEWPORT |
                                                      COLLECTION_HIDE_RENDER;
            break;
          }
          case ID_OB: {
            /* Add the other objects to one of the collections instantiating the
             * root object, or scene's master collection if none found. */
            Object *ob_ref = (Object *)id_ref;
            LISTBASE_FOREACH (Collection *, collection, &dunemain->collections) {
              if (KERNEL_collection_has_object(collection, ob_ref) &&
                  (view_layer != NULL ?
                       KERNEL_view_layer_has_collection(view_layer, collection) :
                       KERNEL_collection_has_collection(scene->master_collection, collection)) &&
                  !ID_IS_LINKED(collection) && !ID_IS_OVERRIDE_LIBRARY(collection)) {
                default_instantiating_collection = collection;
              }
            }
            break;
          }
          default:
            break;
        }
      }
      if (default_instantiating_collection == NULL) {
        default_instantiating_collection = scene->master_collection;
      }

      KERNEL_collection_object_add(dunemain, default_instantiating_collection, ob_new);
      DEG_id_tag_update_ex(bmain, &ob_new->id, ID_RECALC_TRANSFORM | ID_RECALC_BASE_FLAGS);
    }
  }

  LIB_gset_free(all_objects_in_scene, NULL);
}

bool KERNEL_lib_override_library_create(Main *dunemain,
                                     Scene *scene,
                                     ViewLayer *view_layer,
                                     Library *owner_library,
                                     ID *id_root_reference,
                                     ID *id_hierarchy_root_reference,
                                     ID *id_instance_hint,
                                     ID **r_id_root_override)
{
  if (r_id_root_override != NULL) {
    *r_id_root_override = NULL;
  }

  if (id_hierarchy_root_reference == NULL) {
    id_hierarchy_root_reference = id_root_reference;
  }

  const bool success = lib_override_library_create_do(
      dunemain, scene, owner_library, id_root_reference, id_hierarchy_root_reference);

  if (!success) {
    return success;
  }

  if (r_id_root_override != NULL) {
    *r_id_root_override = id_root_reference->newid;
  }

  lib_override_library_create_post_process(
      dunemain, scene, view_layer, owner_library, id_root_reference, id_instance_hint, NULL, false);

  /* Cleanup. */
  KERNEL_main_id_newptr_and_tag_clear(dunemain);
  KERNEL_main_id_tag_all(dunemain, LIB_TAG_DOIT, false);

  /* We need to rebuild some of the deleted override rules (for UI feedback purpose). */
  KERNEL_lib_override_library_main_operations_create(dunemain, true);

  return success;
}

bool KERNEL_lib_override_library_template_create(struct ID *id)
{
  if (ID_IS_LINKED(id)) {
    return false;
  }
  if (ID_IS_OVERRIDE_LIBRARY(id)) {
    return false;
  }

  KERNEL_lib_override_library_init(id, NULL);
  return true;
}

static ID *lib_override_root_find(Main *dunemain, ID *id, const int curr_level, int *r_best_level)
{
  if (curr_level > 1000) {
    CLOG_ERROR(&LOG,
               "Levels of dependency relationships between library overrides IDs is way too high, "
               "skipping further processing loops (involves at least '%s')",
               id->name);
    LIB_assert(0);
    return NULL;
  }

  if (!ID_IS_OVERRIDE_LIBRARY(id)) {
    LIB_assert(0);
    return NULL;
  }

  MainIDRelationsEntry *entry = LIB_ghash_lookup(bmain->relations->relations_from_pointers, id);
  LIB_assert(entry != NULL);

  if (entry->tags & MAINIDRELATIONS_ENTRY_TAGS_PROCESSED) {
    if (ID_IS_OVERRIDE_LIBRARY_REAL(id)) {
      /* This ID has already been processed. */
      *r_best_level = curr_level;
      return id->override_library->hierarchy_root;
    }

    LIB_assert(id->flag & LIB_EMBEDDED_DATA_LIB_OVERRIDE);
    ID *id_owner;
    int best_level_placeholder = 0;
    lib_override_get(dunemain, id, &id_owner);
    return lib_override_root_find(dunemain, id_owner, curr_level + 1, &best_level_placeholder);
  }
  /* This way we won't process again that ID, should we encounter it again through another
   * relationship hierarchy. */
  entry->tags |= MAINIDRELATIONS_ENTRY_TAGS_PROCESSED;

  int best_level_candidate = curr_level;
  ID *best_root_id_candidate = id;

  for (MainIDRelationsEntryItem *from_id_entry = entry->from_ids; from_id_entry != NULL;
       from_id_entry = from_id_entry->next) {
    if ((from_id_entry->usage_flag & IDWALK_CB_OVERRIDE_LIBRARY_NOT_OVERRIDABLE) != 0) {
      /* Never consider non-overridable relationships as actual dependencies. */
      continue;
    }

    ID *from_id = from_id_entry->id_pointer.from;
    if (ELEM(from_id, NULL, id)) {
      continue;
    }
    if (!ID_IS_OVERRIDE_LIBRARY(from_id) || (from_id->lib != id->lib)) {
      continue;
    }

    int level_candidate = curr_level + 1;
    /* Recursively process the parent. */
    ID *root_id_candidate = lib_override_root_find(
        dunemain, from_id, curr_level + 1, &level_candidate);
    if (level_candidate > best_level_candidate && root_id_candidate != NULL) {
      best_root_id_candidate = root_id_candidate;
      best_level_candidate = level_candidate;
    }
  }

  if (!ID_IS_OVERRIDE_LIBRARY_REAL(best_root_id_candidate)) {
    LIB_assert(id->flag & LIB_EMBEDDED_DATA_LIB_OVERRIDE);
    ID *id_owner;
    int best_level_placeholder = 0;
    lib_override_get(dunemain, best_root_id_candidate, &id_owner);
    best_root_id_candidate = lib_override_root_find(
        dunemain, id_owner, curr_level + 1, &best_level_placeholder);
  }

  LIB_assert(best_root_id_candidate != NULL);
  LIB_assert((best_root_id_candidate->flag & LIB_EMBEDDED_DATA_LIB_OVERRIDE) == 0);

  *r_best_level = best_level_candidate;
  return best_root_id_candidate;
}

static void lib_override_root_hierarchy_set(Main *bmain, ID *id_root, ID *id, ID *id_from)
{
  if (ID_IS_OVERRIDE_LIBRARY_REAL(id)) {
    if (id->override_library->hierarchy_root == id_root) {
      /* Already set, nothing else to do here, sub-hierarchy is also assumed to be properly set
       * then. */
      return;
    }

    /* Hierarchy root already set, and not matching currently proposed one, try to find which is
     * best. */
    if (id->override_library->hierarchy_root != NULL) {
      /* Check if given `id_from` matches with the hierarchy of the linked reference ID, in which
       * case we assume that the given hierarchy root is the 'real' one.
       *
       * NOTE: This can fail if user mixed dependencies between several overrides of a same
       * reference linked hierarchy. Not much to be done in that case, it's virtually impossible to
       * fix this automatically in a reliable way. */
      if (id_from == NULL || !ID_IS_OVERRIDE_LIBRARY_REAL(id_from)) {
        /* Too complicated to deal with for now. */
        CLOG_WARN(&LOG,
                  "Inconsistency in library override hierarchy of ID '%s'.\n"
                  "\tNot enough data to verify validity of current proposed root '%s', assuming "
                  "already set one '%s' is valid.",
                  id->name,
                  id_root->name,
                  id->override_library->hierarchy_root->name);
        return;
      }

      ID *id_from_ref = id_from->override_library->reference;
      MainIDRelationsEntry *entry = BLI_ghash_lookup(bmain->relations->relations_from_pointers,
                                                     id->override_library->reference);
      LIB_assert(entry != NULL);

      bool do_replace_root = false;
      for (MainIDRelationsEntryItem *from_id_entry = entry->from_ids; from_id_entry != NULL;
           from_id_entry = from_id_entry->next) {
        if ((from_id_entry->usage_flag & IDWALK_CB_OVERRIDE_LIBRARY_NOT_OVERRIDABLE) != 0) {
          /* Never consider non-overridable relationships as actual dependencies. */
          continue;
        }

        if (id_from_ref == from_id_entry->id_pointer.from) {
          /* A matching parent was found in reference linked data, assume given hierarchy root is
           * the valid one. */
          do_replace_root = true;
          CLOG_WARN(
              &LOG,
              "Inconsistency in library override hierarchy of ID '%s'.\n"
              "\tCurrent proposed root '%s' detected as valid, will replace already set one '%s'.",
              id->name,
              id_root->name,
              id->override_library->hierarchy_root->name);
          break;
        }
      }

      if (!do_replace_root) {
        CLOG_WARN(
            &LOG,
            "Inconsistency in library override hierarchy of ID '%s'.\n"
            "\tCurrent proposed root '%s' not detected as valid, keeping already set one '%s'.",
            id->name,
            id_root->name,
            id->override_library->hierarchy_root->name);
        return;
      }
    }

    id->override_library->hierarchy_root = id_root;
  }

  MainIDRelationsEntry *entry = LIB_ghash_lookup(bmain->relations->relations_from_pointers, id);
  LIB_assert(entry != NULL);

  for (MainIDRelationsEntryItem *to_id_entry = entry->to_ids; to_id_entry != NULL;
       to_id_entry = to_id_entry->next) {
    if ((to_id_entry->usage_flag & IDWALK_CB_OVERRIDE_LIBRARY_NOT_OVERRIDABLE) != 0) {
      /* Never consider non-overridable relationships as actual dependencies. */
      continue;
    }

    ID *to_id = *to_id_entry->id_pointer.to;
    if (ELEM(to_id, NULL, id)) {
      continue;
    }
    if (!ID_IS_OVERRIDE_LIBRARY(to_id) || (to_id->lib != id->lib)) {
      continue;
    }

    /* Recursively process the sub-hierarchy. */
    lib_override_root_hierarchy_set(bmain, id_root, to_id, id);
  }
}

void KERNEL_lib_override_library_main_hierarchy_root_ensure(Main *dunemain)
{
  ID *id;

  KERNEL_main_relations_create(dunemain, 0);

  FOREACH_MAIN_ID_BEGIN (dunemain, id) {
    if (!ID_IS_OVERRIDE_LIBRARY_REAL(id)) {
      continue;
    }
    if (id->override_library->hierarchy_root != NULL) {
      continue;
    }

    KERNEL_main_relations_tag_set(dunemain, MAINIDRELATIONS_ENTRY_TAGS_PROCESSED, false);

    int best_level = 0;
    ID *id_root = lib_override_root_find(bmain, id, best_level, &best_level);

    if (!ELEM(id_root->override_library->hierarchy_root, id_root, NULL)) {
      CLOG_WARN(&LOG,
                "Potential inconsistency in library override hierarchy of ID '%s', detected as "
                "part of the hierarchy of '%s', which has a different root '%s'",
                id->name,
                id_root->name,
                id_root->override_library->hierarchy_root->name);
      continue;
    }

    lib_override_root_hierarchy_set(dunemain, id_root, id_root, NULL);

    LIB_assert(id->override_library->hierarchy_root != NULL);
  }
  FOREACH_MAIN_ID_END;

  KERNEL_main_relations_free(bmain);
}
