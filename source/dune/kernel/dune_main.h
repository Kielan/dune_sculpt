#pragma once

/* About Main struct
 * Main is the root of the 'data-base' of a Blender context. All data is put into lists, and all
 * these lists are stored here.
 *
 * note A Dune file is not much more than a binary dump of these lists. This list of lists is
 * not serialized itself.
 *
 * note `dune_main` files are for operations over the Main database itself, or generating extra
 * temp data to help working with it. Those should typically not affect the data-blocks themselves.
 *
 * section Fn Names
 *
 * - `dune_main_` should be used for functions in that file. */

#include "types_list.h"

#include "lib_compiler_attrs.h"
#include "lib_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct lib_mempool;
struct Thumbnail;
struct GHash;
struct GSet;
struct IdNameLib_Map;
struct ImBuf;
struct Lib;
struct MainLock;
struct UniqueName_Map;

/* Dune thumbnail, as written to the `.dune` file (width, height, and data as char RGBA). */
typedef struct Thumbnail {
  int width, height;
  /** Pixel data, RGBA (repeated): `sizeof(char[4]) * width * height`. */
  char rect[0];
} Thumbnail;

/** Structs caching relations between data-blocks in a given Main. */
typedef struct MainIdRelationsEntryItem {
  struct MainIdRelationsEntryItem *next;

  union {
    /* For `from_ids` list, a user of the hashed Id */
    struct Id *from;
    /* For `to_ids` list, an Id used by the hashed Id */
    struct Id **to;
  } id_ptr;
  /* Session uuid of the `id_pointer`. */
  uint session_uuid;

  int usage_flag; /* Using IDWALK_ enums, defined in dune_lib_query.h */
} MainIdRelationsEntryItem;

typedef struct MainIdRelationsEntry {
  /* Linked list of Ids using that Id. */
  struct MainIdRelationsEntryItem *from_ids;
  /* Linked list of Ids used by that Id. */
  struct MainIdRelationsEntryItem *to_ids;

  /* Session uuid of the Id matching that entry. */
  uint session_uuid;

  /* Runtime tags, users should ensure those are reset after usage. */
  uint tags;
} MainIdRelationsEntry;

/* MainIdRelationsEntry.tags */
typedef enum eMainIdRelationsEntryTags {
  /* Generic tag marking the entry as to be processed. */
  MAINIDRELATIONS_ENTRY_TAGS_DOIT = 1 << 0,

  /* Generic tag marking the entry as processed in the `to` direction (i.e. the IDs used by this
   * item have been processed). */
  MAINIDRELATIONS_ENTRY_TAGS_PROCESSED_TO = 1 << 4,
  /* Generic tag marking the entry as processed in the `from` direction (i.e. the IDs using this
   * item have been processed). */
  MAINIDRELATIONS_ENTRY_TAGS_PROCESSED_FROM = 1 << 5,
  /* Generic tag marking the entry as processed. */
  MAINIDRELATIONS_ENTRY_TAGS_PROCESSED = MAINIDRELATIONS_ENTRY_TAGS_PROCESSED_TO |
                                         MAINIDRELATIONS_ENTRY_TAGS_PROCESSED_FROM,

  /* Generic tag marking the entry as being processed in the `to` direction (i.e. the IDs used by
   * this item are being processed). Useful for dependency loops detection and handling. */
  MAINIDRELATIONS_ENTRY_TAGS_INPROGRESS_TO = 1 << 8,
  /* Generic tag marking the entry as being processed in the `from` direction (i.e. the IDs using
   * this item are being processed). Useful for dependency loops detection and handling. */
  MAINIDRELATIONS_ENTRY_TAGS_INPROGRESS_FROM = 1 << 9,
  /* Generic tag marking the entry as being processed. Useful for dependency loops detection and
   * handling. */
  MAINIDRELATIONS_ENTRY_TAGS_INPROGRESS = MAINIDRELATIONS_ENTRY_TAGS_INPROGRESS_TO |
                                          MAINIDRELATIONS_ENTRY_TAGS_INPROGRESS_FROM,
} eMainIDRelationsEntryTags;

typedef struct MainIdRelations {
  /* Mapping from an ID pointer to all of its parents (Ids using it) and children (Ids it uses).
   * Values are `MainIdRelationsEntry` ptrs. */
  struct GHash *relations_from_pointers;
  /* NOTE: we could add more mappings when needed (e.g. from session uuid?). */

  short flag;

  /* Private... */
  struct lib_mempool *entry_items_pool;
} MainIdRelations;

enum {
  /* Those bmain relations include pointers/usages from editors. */
  MAINIDRELATIONS_INCLUDE_UI = 1 << 0,
};

typedef struct Main {
  struct Main *next, *prev;
  /* The file-path of this dune file, an empty string indicates an unsaved file.
   *
   * For the current loaded dune file this path must be absolute & normalized.
   * This prevents redundant leading slashes or current-working-directory relative paths
   * from causing problems with absolute/relative conversion which relies on this `filepath`
   * being absolute. See lib_path_canonicalize_native.
   *
   * This rule is not strictly enforced as in some cases loading a Main is performed
   * to read data temporarily (prefs & startup) for e.g.
   * where the `filepath` is not persistent or used as a basis for other paths. */
  char filepath[1024];               /* 1024 = FILE_MAX */
  short versionfile, subversionfile; /* see DUNE_FILE_VERSION, DUNE_FILE_SUBVERSION */
  short minversionfile, minsubversionfile;
  /* The currently opened .dune file was written from a newer version of Dune, and has forward
   * compatibility issues (data loss).
   *
   * note: In practice currently this is only based on the version numbers, in the future it
   * could try to use more refined detection on load. */
  bool has_forward_compatibility_issues;

  /* Commit timestamp from `buildinfo`. */
  uint64_t build_commit_timestamp;
  /* Commit Hash from `buildinfo`. */
  char build_hash[16];
  /* Indicate the Main.filepath (file) is the recovered one. */
  bool recovered;
  /* All current Id's exist in the last memfile undo step. */
  bool is_memfile_undo_written;
  /* An Id needs its data to be flushed back.
   * use "needs_flush_to_id" in edit data to flag data which needs updating. */
  bool is_memfile_undo_flush_needed;
  /* Indicates that next memfile undo step should not allow reusing old main when re-read, but
   * instead do a complete full re-read/update from stored memfile. */
  bool use_memfile_full_barrier;

  /* When linking, disallow creation of new data-blocks.
   * Make sure we don't do this by accident, see #76738. */
  bool is_locked_for_linking;

  /* When set, indicates that an unrecoverable error/data corruption was detected.
   * Should only be set by readfile code, and used by upper-level code (typically setup_app_data)
   * to cancel a file reading operation. */
  bool is_read_invalid;

  /* True if this main is the 'GMAIN' of current Dune.
   *
   * There should always be only one global main, all others generated temporarily for
   * various data management process must have this property set to false.. */
  bool is_global_main;

  Thumbnail *dune_thumb;

  struct Lib *curlib;
  List scenes;
  List libs;
  List objects;
  List meshes;
  List curves;
  List metaballs;
  List materials;
  List textures;
  List images;
  List lattices;
  List lights;
  List cameras;
  List ipo; /* Deprecated (only for versioning). */
  List shapekeys;
  List worlds;
  List screens;
  List fonts;
  List texts;
  List speakers;
  List lightprobes;
  List sounds;
  List collections;
  List armatures;
  List actions;
  List nodetrees;
  List brushes;
  List particles;
  List palettes;
  List paintcurves;
  List wm; /* Singleton (exception). */
  List pens;
  List movieclips;
  List masks;
  List linestyles;
  List cachefiles;
  List workspaces;
  /**
   * note The name `hair_curves` is chosen to be different than `curves`,
   * but they are generic curve data-blocks, not just for hair.
   */
  List hair_curves;
  List pointclouds;
  List volumes;

  /* Must be generated, used and freed by same code - never assume this is valid data unless you
   * know when, who and how it was created.
   * Used by code doing a lot of remapping etc. at once to speed things up. */
  struct MainIdRelations *relations;

  /** IdMap of Ids. Currently used when reading (expanding) libraries. */
  struct IdNameLib_Map *id_map;

  /** Used for efficient calculations of unique names. */
  struct UniqueName_Map *name_map;

  /* Used for efficient calculations of unique names. Covers all names in current Main, including
   * linked data ones. */
  struct UniqueName_Map *name_map_global;

  struct MainLock *lock;
} Main;

/* Create a new Main data-base.
 * Always generate a non-global Main, use dune_globals_main_replace to put a newly
 * created one in `G_MAIN` */
struct Main *dune_main_new(void);
void dune_main_free(struct Main *mainvar);

/* Check whether given `main` is empty or contains some Ids */
bool dune_main_is_empty(struct Main *main);

void dune_main_lock(struct Main *main);
void dune_main_unlock(struct Main *main);

/* Generate the mappings between used Ids and their users, and vice-versa. */
void dune_main_relations_create(struct Main *main, short flag);
void dune_main_relations_free(struct Main *main);
/* Set or clear given `tag` in all relation entries of given `main` */
void dune_main_relations_tag_set(struct Main *main, eMainIdRelationsEntryTags tag, bool value);

/* Create a Set storing all Ids present in given main, by their ptrs.
 *
 * param gset: If not NULL, given GSet will be extended with Ids from given main,
 * instead of creating a new one. */
struct GSet *dune_main_gset_create(struct Main *main, struct GSet *gset);

/* Temporary runtime API to allow re-using local (already appended)
 * Ids instead of appending a new copy again. */
/* Generate a mapping between 'lib path' of an Id
 * (as a pair (relative dune file path, id name)), and a current local Id, if any.
 * This uses the infor stored in `Id.lib_weak_ref` */
struct GHash *dune_main_lib_weak_ref_create(struct Main *main) ATTR_NONNULL();
/* Destroy the data generated by dune_main_lib_weak_ref_create. */
void dune_main_lib_weak_ref_destroy(struct GHash *lib_weak_ref_mapping)
    ATTR_NONNULL();
/* Search for a local Id matching the given linked Id ref.
 * param lib_weak_ref_mapping: the mapping data generated by
 * dune_main_lib_weak_ref_create.
 * param lib_filepath: the path of a dune file lib (relative to current working one)
 * param lib_id_name: the full Id name, including the leading two chars encoding the Id
 * type */
struct Id *dune_main_lib_weak_ref_search_item(
            struct GHash *lib_weak_ref_mapping,
            const char *lib_filepath,
            const char *lib_id_name) ATTR_NONNULL();
/* Add the given Id weak lib ref to given local Id and the runtime mapping.
 * param lib_weak_ref_mapping: the mapping data generated by
 * dune_main_lib_weak_ref_create.
 * param lib_filepath: the path of a dune file lib (relative to current working one).
 * param lib_id_name: the full Id name, including the leading two chars encoding the Id type.
 * param new_id: New local Id matching given weak ref. */
void dune_main_lib_weak_ref_add_item(struct GHash *lib_weak_ref_mapping,
                                     const char *lib_filepath,
                                     const char *lib_id_name,
                                     struct Id *new_id) ATTR_NONNULL();
/* Update the status of the given Id weak lib ref in current local Ids and the runtime
 * mapping.
 * Effectively transfers the 'ownership' of the given weak ref from `old_id` to
 * `new_id`.
 *
 * param lib_weak_ref_mapping: the mapping data generated by
 * dune_main_lib_weak_ref_create.
 * param lib_filepath: the path of a dune file lib (relative to current working one).
 * param lib_id_name: the full Id name, including the leading two chars encoding the Id type.
 * param old_id: Existing local Id matching given weak ref.
 * param new_id: New local Id matching given weak ref. */
void dune_main_lib_weak_ref_update_item(struct GHash *lib_weak_ref_mapping,
                                        const char *lib_filepath,
                                        const char *lib_id_name,
                                        struct Id *old_id,
                                        struct Id *new_id) ATTR_NONNULL();
/* Remove the given Id weak lib ref from the given local Id and the runtime mapping.
 *
 * param library_weak_ref_mapping: the mapping data generated by
 * dune_main_lib_weak_ref_create.
 * param lib_filepath: the path of a dune file lib (relative to current working one).
 * param lib_id_name: the full Id name, including the leading two chars encoding the Id type.
 * param old_id: Existing local Id matching given weak ref. */
void dune_main_lib_weak_ref_remove_item(struct GHash *lib_weak_ref_mapping,
                                        const char *lib_filepath,
                                        const char *lib_id_name,
                                        struct Id *old_id) ATTR_NONNULL();

/* Generic utils to loop over whole Main database */
#define FOREACH_MAIN_LIST_ID_BEGIN(_list, _id) \
  { \
    Id *_id_next = (Id *)(_list)->first; \
    for ((_id) = _id_next; (_id) != NULL; (_id) = _id_next) { \
      _id_next = (Id *)(_id)->next;

#define FOREACH_MAIN_LIST_ID_END \
  } \
  } \
  ((void)0)

#define FOREACH_MAIN_LIST_BEGIN(_main, _list) \
  { \
    List *_listarray[INDEX_ID_MAX]; \
    int _i = set_listptrs((_main), _listarray); \
    while (_i--) { \
      (_lb) = _lbarray[_i];

#define FOREACH_MAIN_LIST_END \
  } \
  } \
  ((void)0)

/* Top level `foreach`-like macro allowing to loop over all Ids in a given Main data-base.
 *
 * NOTE: Order tries to go from 'user Ids' to 'used Ids' (e.g. collections will be processed
 * before objects, which will be processed before obdata types, etc.).
 *
 * WARNING: DO NOT use break statement with that macro, use FOREACH_MAIN_LIST and
 * FOREACH_MAIN_LIST_ID instead if you need that kind of control flow. */
#define FOREACH_MAIN_ID_BEGIN(_main, _id) \
  { \
    List *_list; \
    FOREACH_MAIN_LIST_BEGIN ((_main), _list) { \
      FOREACH_MAIN_LIST_ID_BEGIN (_list, (_id))

#define FOREACH_MAIN_ID_END \
  FOREACH_MAIN_LIST_ID_END; \
  } \
  FOREACH_MAIN_LIST_END; \
  } \
  ((void)0)

/* Generates a raw .dune file thumbnail data from given image.
 * param bmain: If not NULL, also store generated data in this Main.
 * param img: ImBuf image to generate thumbnail data from.
 * return The generated .dune file raw thumbnail data. */
struct Thumbnail *dune_main_thumbnail_from_imbuf(struct Main *main, struct ImBuf *img);
/* Generates an image from raw .dune file thumbnail data.
 * param main: Use this main->dune_thumb data if given data is NULL.
 * param data: Raw .dune file thumbnail data.
 * return An ImBuf from given data, or NULL if invalid. */
struct ImBuf *dune_main_thumbnail_to_imbuf(struct Main *main, struct Thumbnail *data);
/* Generates an empty (black) thumbnail for given Main. */
void dune_main_thumbnail_create(struct Main *main);

/* Return file-path of given main. */
const char *dune_main_file_path(const struct Main *main) ATTR_NONNULL();
/* Return file-path of global main #G_MAIN.
 *
 * warning Usage is not recommended,
 * you should always try to get a valid Main ptr from cxt. */
const char *dune_main_file_path_from_global(void);

/* return A ptr to the List of given main for requested type Id type. */
struct List *which_lib(struct Main *main, short type);

//#define INDEX_ID_MAX 41
/* Put the ptrs to all the List structs in given `main` into the `*list[INDEX_ID_MAX]`
 * array, and return the number of those for convenience.
 *
 * This is useful for generic traversal of all the blocks in a Main (by traversing all the lists
 * in turn), without worrying about block types.
 *
 * param list: Array of lists INDEX_ID_MAX in length.
 *
 * The order of each ID type List in the array is determined by the `INDEX_ID_<IDTYPE>`
 * enum definitions in `types_id.h`. See also the FOREACH_MAIN_ID_BEGIN macro in `dune_main.h` */
int set_listptrs(struct Main *main, struct List *list[]);

#define MAIN_VERSION_FILE_ATLEAST(main, ver, subver) \
  ((main)->versionfile > (ver) || \
   ((main)->versionfile == (ver) && (main)->subversionfile >= (subver)))

#define MAIN_VERSION_FILE_OLDER(main, ver, subver) \
  ((main)->versionfile < (ver) || \
   ((main)->versionfile == (ver) && (main)->subversionfile < (subver)))

#define MAIN_VERSION_FILE_OLDER_OR_EQUAL(main, ver, subver) \
  ((main)->versionfile < (ver) || \
   ((main)->versionfile == (ver) && (main)->subversionfile <= (subver)))

/**
 * The size of thumbnails (optionally) stored in the `.blend` files header.
 *
 * NOTE(@ideasman42): This is kept small as it's stored uncompressed in the `.blend` file,
 * where a larger size would increase the size of every `.blend` file unreasonably.
 * If we wanted to increase the size, we'd want to use compression (JPEG or similar).
 */
#define THUMB_SIZE 128

#define THUMB_MEMSIZE(_x, _y) \
  (sizeof(Thumbnail) + ((size_t)(_x) * (size_t)(_y)) * sizeof(int))
/** Protect against buffer overflow vulnerability & negative sizes. */
#define THUMB_MEMSIZE_IS_VALID(_x, _y) \
  (((_x) > 0 && (_y) > 0) && ((uint64_t)(_x) * (uint64_t)(_y) < (SIZE_MAX / (sizeof(int) * 4))))

#ifdef __cplusplus
}
#endif
