#pragma once

/* API to manage data-blocks inside of Dune's Main data-base, or as independent runtime-only
 * data.
 *
 * `dune_lib_` files are for ops over data-blocks themselves, although they might
 * alter Main as well (when creating/renaming/deleting an ID e.g.).
 *
 * Fn Names
 *
 * warning Descriptions below is ideal goal, current status of naming does not yet fully follow it
 * (this is WIP).
 *
 * - `dune_lib_id_` should be used for rather high-level ops, that involve Main database and
 *   relations with other IDs, and can be considered as 'safe' (as in, in themselves, they leave
 *   affected IDs/Main in a consistent status).
 * - `dune_lib_libblock_` should be used for lower level ops, that perform some parts of
 *   `dune_lib_id_` ones, but will generally not ensure caller that affected data is in a consistent
 *   state by their own ex alone.
 * - `dune_lib_main_` should be used for operations performed over all Ids of a given Main
 *   data-base.
 *
 * External code should typically not use `dune_lib_libblock_` fns, except in some
 * specific cases requiring advanced (and potentially dangerous) handling. */

#include "lib_compiler_attrs.h"
#include "lib_utildefines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct DuneWriter;
struct GHash;
struct Id;
struct Lib;
struct List;
struct Main;
struct ApiPtr;
struct ApiProp;
struct Cxt;

/* Get alloc size of a given data-block type and optionally alloc name. */
size_t dune_libblock_get_alloc_info(short type, const char **name);
/* Allocs and returns mem of the right size for the specd block type,
 * init to zero. */
void *dune_libblock_alloc_notest(short type) ATTR_WARN_UNUSED_RESULT;
/* Allocs and returns a block of the specified type, with the specified name
 * (adjusted as necessary to ensure uniqueness), and appended to the specified list.
 * The user count is set to 1, all other content (apart from name and links) being
 * initialized to zero. */
void *dune_libblock_alloc(struct Main *main, short type, const char *name, int flag)
    ATTR_WARN_UNUSED_RESULT;
/* Init an Id of given type, such that it has valid 'empty' data.
 * Id is assumed to be just calloc'ed. */
void dune_libblock_init_empty(struct Id *id) ATTR_NONNULL(1);

/* Reset the runtime counters used by Id remapping. */
void dune_libblock_runtime_reset_remapping_status(struct Id *id) ATTR_NONNULL(1);

/* Id's sess_uuid management. */

/* When an ID's uuid is of that value, it is unset/invalid (e.g. for runtime Ids, etc.). */
#define MAIN_ID_SESSION_UUID_UNSET 0

/* Generate a sess-wise uuid for the given id.
 * "sess-wise" here means while editing a given .dune file. Once a new .dune file is
 * loaded or created, undo history is cleared/reset, and so is the uuid counter. */
void dune_lib_libblock_session_uuid_ensure(struct Id *id);
/* Re-gen a new sess-wise uuid for the given id.
 * This has a few very specific use-cases, no other usage is expected currently:
 *   - To handle UI-related data-blocks that are kept across new file reading, when we do keep
 * existing UI.
 *   - For Ids that are made local wo needing any copying. */
void dune_lib_libblock_session_uuid_renew(struct Id *id);

/* Generic helper to create a new empty data-block of given type in given main database.
 * param name: can be NULL, in which case we get default name for this Id type. */
void *dune_id_new(struct Main *main, short type, const char *name);
/* Generic helper to create a new tmp empty data-block of given type,
 * *outside* of any Main database.
 * param name: can be NULL, in which case we get default name for this Id type. */
void *dune_id_new_nomain(short type, const char *name);

/* New Id creation/copying options. */
enum {
  /* Generic options (should be handled by all Id types copying, Id creation, etc.). *** */
  /* Create datablock outside of any main database -
   * similar to 'localize' fns of materials etc. */
  LIB_ID_CREATE_NO_MAIN = 1 << 0,
  /* Do not affect user refcount of datablocks used by new one
   * (which also gets zero usercount then).
   * Implies LIB_ID_CREATE_NO_MAIN. */
  LIB_ID_CREATE_NO_USER_REFCOUNT = 1 << 1,
  /* Assume given 'newid' alrdy points to allocd mem for whole datablock
   * (ID + data) - USE WITH CAUTION!
   * Implies LIB_ID_CREATE_NO_MAIN. */
  LIB_ID_CREATE_NO_ALLOCATE = 1 << 2,

  /* Do not tag new Id for update in graph. */
  LIB_ID_CREATE_NO_DEG_TAG = 1 << 8,

  /* Very similar to LIB_ID_CREATE_NO_MAIN, and should never be used with it (typically combined
   * with LIB_ID_CREATE_LOCALIZE or LIB_ID_COPY_LOCALIZE in fact).
   * It ensures that Ids created with it will get the LIB_TAG_LOCALIZED tag, and uses some
   * specific code in some copy cases (mostly for node trees). */
  LIB_ID_CREATE_LOCAL = 1 << 9,

  /* Create for the graph, when set LIB_TAG_COPIED_ON_WRITE must be set.
   * Internally this is used to share some ptrs instead of duplicating them. */
  LIB_ID_COPY_SET_COPIED_ON_WRITE = 1 << 10,

  /* Specific options to some Id types or usages. */
  /* May be ignored by unrelated Id copying fns. */
  /* Ob only, needed by make_local code. */
  /* LIB_ID_COPY_NO_PROXY_CLEAR = 1 << 16, */ /* UNUSED */
  /* Do not copy preview data, when supported. */
  LIB_ID_COPY_NO_PREVIEW = 1 << 17,
  /* Copy runtime data caches. */
  LIB_ID_COPY_CACHES = 1 << 18,
  /* Don't copy id->adt, used by ID data-block localization routines. */
  LIB_ID_COPY_NO_ANIMDATA = 1 << 19,
  /* Mesh: Reference CD data layers instead of doing real copy - USE WITH CAUTION! */
  LIB_ID_COPY_CD_REFERENCE = 1 << 20,
  /* Do not copy id->override_library, used by ID data-block override routines. */
  LIB_ID_COPY_NO_LIB_OVERRIDE = 1 << 21,
  /* When copying local sub-data (like constraints or modifiers), do not set their "library
   * override local data" flag. */
  LIB_ID_COPY_NO_LIB_OVERRIDE_LOCAL_DATA_FLAG = 1 << 22,

  /* XXX Hackish/not-so-nice specific behaviors needed for some corner cases. *** */
  /* Ideally we should not have those, but we need them for now... *** */
  /* EXCEPTION! Deep-copy actions used by animation-data of copied ID. */
  LIB_ID_COPY_ACTIONS = 1 << 24,
  /* Keep the library pointer when copying data-block outside of bmain. */
  LIB_ID_COPY_KEEP_LIB = 1 << 25,
  /* EXCEPTION! Deep-copy shape-keys used by copied obdata ID. */
  LIB_ID_COPY_SHAPEKEY = 1 << 26,
  /** EXCEPTION! Specific deep-copy of node trees used e.g. for rendering purposes. */
  LIB_ID_COPY_NODETREE_LOCALIZE = 1 << 27,
  /**
   * EXCEPTION! Specific handling of RB objects regarding collections differs depending whether we
   * duplicate scene/collections, or objects.
   */
  LIB_ID_COPY_RIGID_BODY_NO_COLLECTION_HANDLING = 1 << 28,

  /* Helper 'defines' gathering most common flag sets. *** */
  /* Shape-keys are not real ID's, more like local data to geometry IDs. */
  LIB_ID_COPY_DEFAULT = LIB_ID_COPY_SHAPEKEY,

  /* Create a local, outside of bmain, data-block to work on. */
  LIB_ID_CREATE_LOCALIZE = LIB_ID_CREATE_NO_MAIN | LIB_ID_CREATE_NO_USER_REFCOUNT |
                           LIB_ID_CREATE_NO_DEG_TAG,
  /* Generate a local copy, outside of bmain, to work on (used by COW e.g.). */
  LIB_ID_COPY_LOCALIZE = LIB_ID_CREATE_LOCALIZE | LIB_ID_COPY_NO_PREVIEW | LIB_ID_COPY_CACHES |
                         LIB_ID_COPY_NO_LIB_OVERRIDE,
};

void dune_libblock_copy_ex(struct Main *main,
                          const struct Id *id,
                          struct Id **r_newid,
                          int orig_flag);
/* Used everywhere in kernel. */
void *dune_libblock_copy(struct Main *main, const struct Id *id) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

/* Sets the name of a block to name, suitably adjusted for uniqueness. */
void dune_libblock_rename(struct Main *main, struct Id *id, const char *name) ATTR_NONNULL();
/* Use after setting the Id's name
 * When name exists: call 'new_id' */
void lib_libblock_ensure_unique_name(struct Main *main, const char *name) ATTR_NONNULL();

struct Id *dune_libblock_find_name(struct Main *main,
                                  short type,
                                  const char *name) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
struct Id *dune_libblock_find_sess_uuid(struct Main *main, short type, uint32_t sess_uuid);
/* Dupl (a.k.a. deep copy) common processing options.
 * See also eDupli_ID_Flags for options controlling what kind of Ids to duplicate. */
typedef enum eLibIdDuplicateFlags {
  /* This call to a duplicate fn is part of another call for some parent Id.
   * Therefore, this sub-process should not clear `newid` ptrs, nor handle remapping itself.
   * In some cases (like Ob one), the duplicate fn may be called on the root Id
   * with this flag set, as remapping and/or other similar tasks need to be handled by the caller. */
  LIB_ID_DUPLICATE_IS_SUBPROCESS = 1 << 0,
  /* This call is performed on a 'root' Id, and should therefore perform some decisions regarding
   * sub-IDs (dependencies), check for linked vs. locale data, etc. */
  LIB_ID_DUPLICATE_IS_ROOT_ID = 1 << 1,
} eLibIdDuplicateFlags;

ENUM_OPS(eLibIdDuplicateFlags, LIB_ID_DUPLICATE_IS_ROOT_ID)

/* lib_remap.c (keep here since they're general fns) */
/* New freeing logic options. */
enum {
  /* Generic options (should be handled by all Id types freeing). */
  /* Do not try to remove freed Id from given Main (passed Main may be NULL). */
  LIB_ID_FREE_NO_MAIN = 1 << 0,
  /* Do not affect user refcount of datablocks used by freed one.
   * Implies LIB_ID_FREE_NO_MAIN. */
  LIB_ID_FREE_NO_USER_REFCOUNT = 1 << 1,
  /* Assume freed Id datablock memory is managed elsewhere, do not free it
   * (still calls relevant ID type's freeing function though) - USE WITH CAUTION!
   * Implies LIB_ID_FREE_NO_MAIN. */
  LIB_ID_FREE_NOT_ALLOCATED = 1 << 2,

  /** Do not tag freed ID for update in depsgraph. */
  LIB_ID_FREE_NO_DEG_TAG = 1 << 8,
  /** Do not attempt to remove freed ID from UI data/notifiers/... */
  LIB_ID_FREE_NO_UI_USER = 1 << 9,
};

void dune_libblock_free_datablock(struct Id *id, int flag) ATTR_NONNULL();
void dune_libblock_free_data(struct Id *id, bool do_id_user) ATTR_NONNULL();

/* In most cases dune_id_free_ex handles this, when lower level fns are called directly
 * this fn will need to be called too, if Python has access to the data.
 *
 * Id data-blocks such as Material.nodetree are not stored in #Main. */
void dune_libblock_free_data_py(struct Id *id);

/* Complete Id freeing, extended version for corner cases.
 * Can override default (and safe!) freeing process, to gain some speed up.
 *
 * At that point, given id is assumed to not be used by any other data-block already
 * (might not be actually true, in case e.g. several inter-related IDs get freed together...).
 * However, they might still be using (referencing) other IDs, this code takes care of it if
 * LIB_TAG_NO_USER_REFCOUNT is not defined.
 *
 * param main: Main database containing the freed Id,
 * can be NULL in case it's a tmp Id outside of any Main struct.
 * param idv: Ptr to Id to be freed.
 * param flag: Set of LIB_ID_FREE_... flags controlling/overriding usual freeing process,
 * 0 to get default safe behavior.
 * param use_flag_from_idtag: Still use freeing info flags from given Id datablock,
 * even if some overriding ones are passed in flag param. */
void dune_id_free_ex(struct Main *main, void *idv, int flag, bool use_flag_from_idtag);
/* Complete Id freeing, should be usable in most cases (even for out-of-Main Ids).
 *
 * See dune_id_free_ex description for full details.
 *
 * param main: Main database containing the freed Id,
 * can be NULL in case it's a tmp Id outside of any Main.
 * param idv: Ptr to Id to be freed. */
void dune_id_free(struct Main *main, void *idv);

/* Not rly a freeing fn by itself,
 * it decrements usercount of given id, and only frees it if it reaches 0. */
void dune_id_free_us(struct Main *main, void *idv) ATTR_NONNULL();

/* Properly delete a single Id from given main database. */
void dune_id_delete(struct Main *main, void *idv) ATTR_NONNULL();
/* Properly delete all Ids tagged w LIB_TAG_DOIT, in given main db.
 *
 * This is more efficient than calling dune_id_delete repetitively on a large set of IDs
 * (several times faster when deleting most of the Ids at once).
 *
 * warning Considered experimental for now, seems to be working OK but this is
 * risky code in a complicated area.
 * return Number of deleted datablocks. */
size_t dune_id_multi_tagged_delete(struct Main *main) ATTR_NONNULL();

/* Add a 'NO_MAIN' data-block to given main (also sets usercounts of its IDs if needed). */
void dune_libblock_management_main_add(struct Main *main, void *idv);
/* Remove a data-block from given main (set it to 'NO_MAIN' status). */
void dune_libblock_management_main_remove(struct Main *main, void *idv);

void dune_libblock_management_usercounts_set(struct Main *main, void *idv);
void dune_libblock_management_usercounts_clear(struct Main *main, void *idv);

void id_lib_extern(struct Id *id);
void id_lib_indirect_weak_link(struct Id *id);
/* Ensure we have a real user
 * Now that we have flags, we could get rid of the 'fake_user' special case,
 * flags are enough to ensure we always have a real user.
 * However, ID_REAL_USERS is used in several places outside of core lib.c,
 * so think we can wait later to make this change. */
void id_us_ensure_real(struct Id *id);
void id_us_clear_real(struct Id *id);
/* Same as id_us_plus, but does not handle lib indirect -> extern.
 * Only used by readfile.c so far, but simpler/safer to keep it here nonetheless. */
void id_us_plus_no_lib(struct Id *id);
void id_us_plus(struct Id *id);
/* decrements the user count for *id. */
void id_us_min(struct Id *id);
void id_fake_user_set(struct Id *id);
void id_fake_user_clear(struct Id *id);
void dune_id_newptr_and_tag_clear(struct Id *id);

/* Flags to control make local code behavior. */
enum {
  /* Making that ID local is part of making local a whole library. */
  LIB_ID_MAKELOCAL_FULL_LIBRARY = 1 << 0,

  /* In case caller code already knows this ID should be made local without copying. */
  LIB_ID_MAKELOCAL_FORCE_LOCAL = 1 << 1,
  /* In case caller code already knows this ID should be made local using copying. */
  LIB_ID_MAKELOCAL_FORCE_COPY = 1 << 2,

  /* Clear asset data (in case the ID can actually be made local, in copy case asset data is never
   * copied over). */
  LIB_ID_MAKELOCAL_ASSET_DATA_CLEAR = 1 << 3,
};

/* Helper to decide whether given `id` can be directly made local, or needs to be copied.
 * `r_force_local` and `r_force_copy` cannot be true together. But both can be false, in case no
 * action should be performed.
 *
 * low-level helper to de-duplicate logic between `dune_lib_id_make_local_generic` and the
 * specific corner-cases implementations needed for objects and brushes. */
void dune_lib_id_make_local_generic_action_define(
    struct Main *main, struct Id *id, int flags, bool *r_force_local, bool *r_force_copy);
/* Generic 'make local' function, works for most of data-block types. */
void dune_lib_id_make_local_generic(struct Main *main, struct Id *id, int flags);
/* Calls the appropriate make_local method for the block, unless test is set.
 *
 * Always set Id.newid ptr in case it gets duplicated.
 *
 * param flags: Special flag used when making a whole lib's content local,
 * it needs specific handling.
 * return true is the ID has successfully been made local. */
bool dune_lib_id_make_local(struct Main *main, struct Id *id, int flags);
/* note Does *not* set #ID.newid ptr. */
bool id_single_user(struct Cxt *C,
                    struct Id *id,
                    struct ApiPtr *ptr,
                    struct ApiProp *prop);
bool dune_id_copy_is_allowed(const struct Id *id);
/* Invokes the appropriate copy method for the block and returns the result in
 * Id.newid, unless test. Returns true if the block can be copied. */
struct Id *dune_id_copy(struct Main *main, const struct Id *id);
/* Generic entry point for copying a data-block (new API).
 * note Copy is generally only affecting the given data-block
 * (no Id used by copied one will be affected, besides user-count).
 *
 * There are exceptions though:
 * - Embedded Ids (root node trees and master collections) are always copied with their owner.
 * - If LIB_ID_COPY_ACTIONS is defined, actions used by anim-data will be duplicated.
 * - If LIB_ID_COPY_SHAPEKEY is defined, shape-keys will be duplicated.
 * - If LIB_ID_CREATE_LOCAL is defined, root node trees will be deep-duplicated recursively.
 *
 * note User-count of new copy is always set to 1.
 *
 * param main: Main database, may be NULL only if LIB_ID_CREATE_NO_MAIN is specified.
 * param id: Source data-block.
 * param r_newid: Ptr to new (copied) Id ptr, may be NULL.
 * Used to allow copying into already allocd mem.
 * param flag: Set of copy opts, see `types_id.h` enum for details
 * (leave to zero for default, full copy).
 * return NULL when copying that ID type is not supported, the new copy otherwise. */
struct Id *dune_id_copy_ex(struct Main *main, const struct Id *id, struct Id **r_newid, int flag);
/* Invokes the appropriate copy method for the block and returns the result in
 * newid, unless test. Returns true if the block can be copied. */
struct ID *dune_id_copy_for_duplicate(struct Main *main,
                                     struct Id *id,
                                     uint duplicate_flags,
                                     int copy_flags);

/* Does a mere mem swap over the whole Ids data (including type-specific mem).
 * Most internal Id data itself is not swapped (only IdProps are).
 *
 * param main: May be NULL, in which case there will be no remapping of internal ptrs to
 * itself. */
void dune_lib_id_swap(struct Main *main, struct Id *id_a, struct Id *id_b);
/* Does a mere mem swap over the whole Ids data (including type-specific mem).
 * All internal Id data itself is also swapped.
 *
 * param main: May be NULL, in which case there will be no remapping of internal ptrs to
 * itself. */
void dune_lib_id_swap_full(struct Main *main, struct Id *id_a, struct Id *id_b);

/* Sort given id into given lb list, using case-insensitive comparison of the id names.
 *
 * All other IDs beside given one are assumed already properly sorted in the list.
 *
 * param id_sorting_hint: Ignored if NULL. Otherwise, used to check if we can insert id
 * immediately before or after that ptr. It must always be into given lb list. */
void id_sort_by_name(struct List *lb, struct Id *id, struct Id *id_sorting_hint);
/**
 * Expand ID usages of given id as 'extern' (and no more indirect) linked data.
 * Used by ID copy/make_local functions.
 */
void dune_lib_id_expand_local(struct Main *main, struct Id *id, int flags);

/* Ensures given ID has a unique name in given list.
 *
 * Only for local IDs (linked ones alrdy have a unique Id in their lib).
 *
 * param do_linked_data: if true, also ensure a unique name in case the given \a id is linked
 * (otherwise, just ensure that it is properly sorted).
 *
 * return true if a new name had to be created. */
bool dune_id_new_name_validate(struct List *lb,
                              struct Id *id,
                              const char *name,
                              bool do_linked_data) ATTR_NONNULL(1, 2);
/**
 * Pull an ID out of a library (make it local). Only call this for IDs that
 * don't have other library users.
 *
 * \param flags: Same set of `LIB_ID_MAKELOCAL_` flags as passed to #BKE_lib_id_make_local.
 */
void dune_lib_id_clear_lib_data(struct Main *main, struct Id *id, int flags);

/* Clear or set given tags for all ids of given type in `bmain` (runtime tags).
 * Affect whole Main database. */
void dune_main_id_tag_idcode(struct Main *mainvar, short type, int tag, bool val);
/* Clear or set given tags for all ids in list (runtime tags). */
void dune_main_id_tag_list(struct List *lb, int tag, bool val);
/* Clear or set given tags for all ids in bmain (runtime tags). */
void dune_main_id_tag_all(struct Main *mainvar, int tag, bool val);

/* Clear or set given flags for all ids in listb (persistent flags). */
void dune_main_id_flag_list(struct List *lb, int flag, bool val);
/* Clear or set given flags for all ids in bmain (persistent flags). */
void dune_main_id_flag_all(struct Main *main, int flag, bool val);

/* Next to indirect usage in `readfile.c/writefile.c` also in `editob.c`, `scene.c`. */
void dune_main_id_newptr_and_tag_clear(struct Main *main);

void dune_main_id_refcount_recompute(struct Main *main, bool do_linked_only);

void dune_main_lib_objects_recalc_all(struct Main *bmain);

/* Only for repairing files via versioning, avoid for general use. */
void dune_main_id_repair_duplicate_names_list(struct List *lb);

#define MAX_ID_FULL_NAME (64 + 64 + 3 + 1)         /* 64 is MAX_ID_NAME - 2 */
#define MAX_ID_FULL_NAME_UI (MAX_ID_FULL_NAME + 3) /* Adds 'keycode' two letters at beginning. */
/* Generate full name of the data-block (without ID code, but with library if any).
 *
 * note Result is unique to a given ID type in a given Main database.
 *
 * param name: An allocated string of minimal length #MAX_ID_FULL_NAME,
 * will be filled with generated string.
 * param separator_char: Character to use for separating name and library name.
 * Can be 0 to use default (' '). */
void dune_id_full_name_get(char name[MAX_ID_FULL_NAME], const struct Id *id, char separator_char);
/**
 * Generate full name of the data-block (without ID code, but with library if any),
 * with a 2 to 3 character prefix prepended indicating whether it comes from a library,
 * is overriding, has a fake or no user, etc.
 *
 * \note Result is unique to a given ID type in a given Main database.
 *
 * \param name: An allocated string of minimal length #MAX_ID_FULL_NAME_UI,
 * will be filled with generated string.
 * \param separator_char: Character to use for separating name and library name.
 * Can be 0 to use default (' ').
 * \param r_prefix_len: The length of the prefix added.
 */
void dune_id_full_name_ui_prefix_get(char name[MAX_ID_FULL_NAME_UI],
                                    const struct ID *id,
                                    bool add_lib_hint,
                                    char separator_char,
                                    int *r_prefix_len);

/* Generate a concatenation of ID name (including two-chars type code) and its lib name, if any.
 *
 * \return A unique allocated string key for any ID in the whole Main database. */
