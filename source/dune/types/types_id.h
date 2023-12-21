/* brief id and lib types, which are fundamental for api. **/
#pragma once

#include "types_id_enums.h"
#include "types_defs.h"
#include "types_list.h"

struct FileData;
struct GHash;
struct GPUTexture;
struct Id;
struct Lib;
struct PackedFile;

/* Runtime display data */
struct DrwData;
typedef void (*DrwDataInitCb)(struct DrwData *engine_data);
typedef void (*DrwDataFreeCb)(struct DrwData *engine_data);

typedef struct DrwData {
  struct DrwData *next, *prev;
  struct DrwEngineType *engine_type;
  /* Only nested data, NOT the engine data itself. */
  DrwDataFreeCb free;
  /* Accumulated recalc flags, which corresponds to ID->recalc flags. */
  int recalc;
} DrwData;

typedef struct DrwDataList {
  struct DrwData *first, *last;
} DrwDataList;

typedef struct IdPropUIData {
  /* Tooltip/prop description ptr. Owned by the IdProp. */
  char *description;
  /* API subtype, used for every type except string props (PropSubType). */
  int api_subtype;
  char _pad[4];
} IdPropUIData;

/* IDP_UI_DATA_TYPE_INT */
typedef struct IdPropUIDataInt {
  IdPropUIData base;
  int *default_array; /* Only for array properties. */
  int default_array_len;
  char _pad[4];
  int min;
  int max;
  int soft_min;
  int soft_max;
  int step;
  int default_val;
} IdPropUIDataInt;

/* IDP_UI_DATA_TYPE_FLOAT */
typedef struct IdPropUIDataFloat {
  IdPropUIData base;
  double *default_array; /* Only for array properties. */
  int default_array_len;
  char _pad[4];
  float step;
  int precision;
  double min;
  double max;
  double soft_min;
  double soft_max;
  double default_val;
} IdPropUIDataFloat;

/* IDP_UI_DATA_TYPE_STRING */
typedef struct IdPropUIDataString {
  IdPropUIData base;
  char *default_val;
} IdPropUIDataString;

/* IDP_UI_DATA_TYPE_ID */
typedef struct IdPropUIDataId {
  IdPropUIData base;
} IdPropUIDataId;

typedef struct IdPropData {
  void *ptr;
  List group;
  /* Fits a double into these two 32bit integers. */
  int val, val2;
} IdPropData;

typedef struct IdProp {
  struct IdProp *next, *prev;
  char type, subtype;
  short flag;
  /* MAX_IDPROP_NAME. */
  char name[64];
  /* saved is used to indicate if this struct has been saved yet.
   * seemed like a good idea as a '_pad' var was needed anyway :) */
  int saved;
  /* alignment for 64 bits. */
  IdPropData data;
  /* Array length, also (this is important!) string length + 1.
   * the idea is to be able to reuse array realloc functions on strings. */
  int len;
  /* Strings and arrays are both buffered, though the buffer isn't saved. */
  /* totallen is total length of allocated array/string, including a buffer.
   * Note that the buffering is mild; the code comes from python's list implementation. */
  int totallen;
  IdPropUIData *ui_data;
} IdProp;

#define MAX_IDPROP_NAME 64
#define DEFAULT_ALLOC_FOR_NULL_STRINGS 64

/*->type*/
typedef enum eIdPropType {
  IDP_STRING = 0,
  IDP_INT = 1,
  IDP_FLOAT = 2,
  /* Array containing int, floats, doubles or groups. */
  IDP_ARRAY = 5,
  IDP_GROUP = 6,
  IDP_ID = 7,
  IDP_DOUBLE = 8,
  IDP_IDPARRAY = 9,
} eIdPropType;
#define IDP_NUMTYPES 10

/* Used by some IDP utils, keep values in sync with type enum above. */
enum {
  IDP_TYPE_FILTER_STRING = 1 << 0,
  IDP_TYPE_FILTER_INT = 1 << 1,
  IDP_TYPE_FILTER_FLOAT = 1 << 2,
  IDP_TYPE_FILTER_ARRAY = 1 << 5,
  IDP_TYPE_FILTER_GROUP = 1 << 6,
  IDP_TYPE_FILTER_ID = 1 << 7,
  IDP_TYPE_FILTER_DOUBLE = 1 << 8,
  IDP_TYPE_FILTER_IDPARRAY = 1 << 9,
};

/*->subtype */
/* IDP_STRING */
enum {
  IDP_STRING_SUB_UTF8 = 0, /* default */
  IDP_STRING_SUB_BYTE = 1, /* arbitrary byte array, _not_ null terminated */
};

/*->flag*/
enum {
  /* This IDProp may be statically overridden.
   * Should only be used/be relevant for custom props. */
  IDP_FLAG_OVERRIDABLE_LIB = 1 << 0,

  /* This collection item IdProp has been inserted in a local override.
   * This is used by internal code to distinguish between lib-originated items and
   * local-inserted ones, as many operations are not allowed on the former. */
  IDP_FLAG_OVERRIDELIB_LOCAL = 1 << 1,

  /* This means the prop is set but api will return false when checking
   * 'api_prop_is_set', currently this is a runtime flag */
  IDP_FLAG_GHOST = 1 << 7,
};

/* add any future new id prop types here. */
/* Static id override structs. */
typedef struct IdOverrideLibPropOp {
  struct IdOverrideLibPropOp *next, *prev;
  /* Type of override. */
  short op;
  short flag;
  /** Runtime, tags are common to both IDOverrideProperty and IDOverridePropertyOperation. */
  short tag;
  char _pad0[2];

  /* Sub-item refs, if needed (for arrays or collections only).
   * We need both ref and local values to allow e.g. insertion into RNA collections
   * (constraints, mods...).
   * In API collection case, if names are defined, they are used in priority.
   * Names are ptrs (instead of char[64]) to save some space, NULL or empty string when unset.
   * Indices are -1 when unset.
   *
   * For insertion ops in api collections, ref may not actually exist in the
   * linked ref data. It is used to identify the anchor of the insertion op (i.e. the
   * item after or before which the new local item should be inserted), in the local override. */
  char *subitem_ref_name;
  char *subitem_local_name;
  int subitem_ref_index;
  int subitem_local_index;
} IdOverrideLibPropOp;

/* IdOverrideLibPropOp->op. */
enum {
  /* Basic ops. */
  IDOVERRIDE_LIB_OP_NOOP = 0, /* Special value, forbids any overriding. */

  IDOVERRIDE_LIB_OP_REPLACE = 1, /* Fully replace local value by reference one. */

  /* Numeric-only operations. */
  IDOVERRIDE_LIB_OP_ADD = 101, /* Add local value to reference one. */
  /* Subtract local value from reference one (needed due to unsigned values etc.). */
  IDOVERRIDE_LIB_OP_SUBTRACT = 102,
  /* Multiply reference value by local one (more useful than diff for scales and the like). */
  IDOVERRIDE_LIB_OP_MULTIPLY = 103,

  /* Collection-only operations. */
  IDOVERRIDE_LIB_OP_INSERT_AFTER = 201,  /* Insert after given reference's subitem. */
  IDOVERRIDE_LIB_OP_INSERT_BEFORE = 202, /* Insert before given reference's subitem. */
  /* We can add more if needed (move, delete, ...). */
};

/* IdOverrideLibPropOp->flag. */
enum {
  /** User cannot remove that override operation. */
  IDOVERRIDE_LIB_FLAG_MANDATORY = 1 << 0,
  /* User cannot change that override operation. */
  IDOVERRIDE_LIB_FLAG_LOCKED = 1 << 1,

  /* For overrides of Id ptrs: this override still matches (follows) the hierarchy of the
   *  ref linked data. */
  IDOVERRIDE_LIB_FLAG_IDPTR_MATCH_REFERENCE = 1 << 8,
};

/* A single overridden prop, contain all ops on this one. */
typedef struct IdOverrideLibProp {
  struct IdOverrideLibProp *next, *prev;

  /* Path from id to overridden prop.
   * *Does not* include indices/names for final arrays/collections items. */
  char *api_path;

  /* List of overriding ops (IdOverrideLibraryPropOp) applied to this property. */
  List ops;

  /* Runtime, tags are common to both IdOverrideLibProp and
   * IdOverrideLibPropOp. */
  short tag;
  char _pad[2];

  /* The prop type matching the rna_path. */
  unsigned int api_prop_type;
} IdOverrideLibProp;

/* IdOverrideLibProp->tag and IdOverrideLibPropOp->tag. */
enum {
  /* This override prop (operation) is unused and should be removed by cleanup process. */
  IDOVERRIDE_LIB_TAG_UNUSED = 1 << 0,
};

typedef struct IdOverrideLibRuntime {
  struct GHash *api_path_to_override_props;
  uint tag;
} IdOverrideLibRuntime;

/* IdOverrideLibRuntime->tag. */
enum {
  /* This override needs to be reloaded. */
  IDOVERRIDE_LIB_RUNTIME_TAG_NEEDS_RELOAD = 1 << 0,
};

/* Main container for all overriding data info of a data-block. */
typedef struct IdOverrideLib {
  /* Ref linked id which this one overrides. */
  struct Id *ref;
  /* List of IdOverrideLibProp structs. */
  List props;
  /* Override hierarchy root Id. Usually the actual root of the hierarchy, but not always
   * in degenerated cases.
   * All liboverrides of a same hierarchy (e.g. a character collection) share the same root. */
  struct Id *hierarchy_root;
  /* Read/write data. */
  /* Tmp Id storing extra override data (used for differential ops only currently).
   * Always NULL outside of read/write cxt. */
  struct Id *storage;
  IdOverrideLibRuntime *runtime;
  unsigned int flag;
  char _pad_1[4];
} IdOverrideLib;

/* IdOverrideLib->flag */
enum {
  /* The override data-block should not be considered as part of an override hierarchy (generally
   * bc it was created as an single override, outside of any hierarchy consideration). */
  IDOVERRIDE_LIB_FLAG_NO_HIERARCHY = 1 << 0,
};

/* watch it: Seq has identical beginning. */
/* Id is the first thing included in all serializable types. It
 * provides a common handle to place all data in double-linked lists */

/* 2 chars for Id code and 64 for actual name */
#define MAX_ID_NAME 66

/* id_Runtime_Remap.status */
enum {
  /* new_id is directly linked in current .blend. */
  ID_REMAP_IS_LINKED_DIRECT = 1 << 0,
  /* There was some skipped 'user_one' usages of old_id. */
  ID_REMAP_IS_USER_ONE_SKIPPED = 1 << 1,
};

/* Status used and counters created during id-remapping. */
typedef struct IdRuntimeRemap {
  /* Status during id remapping. */
  int status;
  /* During id remapping the number of skipped use cases that refcount the data-block. */
  int skipped_refcounted;
  /* During id remapping the number of direct use cases that could be remapped (e.g. obdata when
in edit mode). */
  int skipped_direct;
  /* During id remapping, the number of indirect use cases that could not be remapped. */
  int skipped_indirect;
} IdRuntimeRemap;

typedef struct IdRuntime {
  IdRuntimeRemap remap;
} IdRuntime;

/* There's a nasty circular dependency here.... 'void *' to the rescue! I
 * really wonder why this is needed. */
typedef struct Id {
  void *next, *prev;
  struct Id *newid;

  struct Lib *lib;

  /* If the Id is an asset, this pointer is set. Owning pointer. */
  struct AssetMetaData *asset_data;

  /* MAX_ID_NAME. */
  char name[66];
  /* lib_... flags report on status of the data-block this Id belongs to
   * (persistent, saved to and read from .dune). */
  short flag;
  /* LIB_TAG_... tags (runtime only, cleared at read time) */
  int tag;
  int us;
  int icon_id;
  int recalc;
  /* Used by undo code. recalc_after_undo_push contains the changes between the
   * last undo push and the current state. This is accumulated as IDs are tagged
   * for update in the graph, and only cleared on undo push.
   *
   * recalc_up_to_undo_push is saved to undo mem, and is the val of
   * recalc_after_undo_push at the time of the undo push. This means it can be
   * used to find the changes between undo states */
  int recalc_up_to_undo_push;
  int recalc_after_undo_push;

  /* Session-wide unique id for a given Id, that remain the same across potential
   * re-allocations (e.g. due to undo/redo steps).  */
  unsigned int sess_uuid;

  IdProp *props;

  /* Ref linked id which this one overrides. */
  IdOverrideLib *override_lib;

  /* Only set for data-blocks which are coming from copy-on-write, points to
   * the original version of it.
   * Also used tmp during memfile undo to keep a reference to old Id when found. */
  struct Id *orig_id;

  /* Holds the PyOb ref to the Id (initialized on demand).
   *
   * This isn't essential, it could be removed however it gives some advantages:
   *
   * - Every time the Id is accessed a BPy_StructAPI doesn't have to be created & destroyed
   *   (consider all the polling and drawing functions that access ID's).
   *
   * - When this Id is del, the BPy_StructAPI can be invalidated
   *   so accessing it from Python raises an exception instead of crashing.
   *
   *   This is of limited benefit though, as it doesn't apply to non Id data
   *   that refs this Id (the bones of an armature or the mods of an ob for e.g.). */
  void *py_instance;

  /* Weak ref to an id in a given lib file, used to allow re-using already appended data
   * in some cases, instead of appending it again.
   *
   * May be NULL. */
  struct LibWeakRef *lib_weak_ref;

  struct IdRuntime runtime;
} Id;

/* For each lib file used, a Lib struct is added to Main
 * WARNING: readfile.c, expand_doit() reads this struct wo types check! */
typedef struct Lib {
  Id id;
  struct FileData *filedata;
  /* Path name used for reading, can be relative and edited in the outliner. */
  char filepath[1024];

  /* Run-time only, absolute file-path (set on read).
   * This is only for convenience, `filepath` is the real path
   * used on file read but in some cases its useful to access the absolute one.
   *
   * Use dune_lib_filepath_set() rather than setting `filepath`
   * directly and it will be kept in sync */
  char filepath_abs[1024];

  /* Set for indirectly linked libs, used in the outliner and while reading. */
  struct Lib *parent;

  struct PackedFile *packedfile;

  ushort tag;
  char _pad_0[6];

  /* Tmp data needed by read/write code, and liboverride recursive resync. */
  int tmp_index;
  /* See DUNE_FILE_VERSION, DUNE_FILE_SUBVERSION, needed for do_versions. */
  short versionfile, subversionfile;
} Lib;

/* Lib.tag */
enum eLibTag {
  /* Automatic recursive resync was needed when linking/loading data from that library. */
  LIB_TAG_RESYNC_REQUIRED = 1 << 0,
};

/* A weak lib/Id ref for local data that has been appended, to allow re-using that local
 * data instead of creating a new copy of it in future appends.
 *
 * This is by design a weak ref, in other words code should be totally fine and perform
 * a regular append if it cannot find a valid matching local Id.
 *
 * There should always be only one single ID in current Main matching a given linked
 * ref. */
typedef struct LibWeakRef {
  /**  Expected to match a `Library.filepath`. */
  char lib_filepath[1024];

  /** MAX_ID_NAME. May be different from the current local ID name. */
  char lib_id_name[66];

  char _pad[2];
} LibWeakRef;

/* for PreviewImage->flag */
enum ePreviewImgFlag {
  PRV_CHANGED = (1 << 0),
  PRV_USER_EDITED = (1 << 1), /* if user-edited, do not auto-update this anymore! */
  PRV_RENDERING = (1 << 2),   /* Rendering was invoked. Cleared on file read. */
};

/* for PreviewImg->tag */
enum {
  PRV_TAG_DEFFERED = (1 << 0),           /* Actual loading of preview is deferred. */
  PRV_TAG_DEFFERED_RENDERING = (1 << 1), /* Deferred preview is being loaded. */
  PRV_TAG_DEFFERED_DEL = (1 << 2),    /* Deferred preview should be deleted asap. */
};

typedef struct PreviewImg {
  /* All vals of 2 are really NUM_ICON_SIZES */
  unsigned int w[2];
  unsigned int h[2];
  short flag[2];
  short changed_timestamp[2];
  unsigned int *rect[2];

  /* Runtime-only data. */
  struct GPUTexture *gputexture[2];
  /* Used by previews outside of ID context. */
  int icon_id;

  /* Runtime data. */
  short tag;
  char _pad[2];
} PreviewImg;

#define PRV_DEFERRED_DATA(prv) \
  (CHECK_TYPE_INLINE(prv, PreviewImg *), \
   lib_assert((prv)->tag & PRV_TAG_DEFFERED), \
   (void *)((prv) + 1))

#define ID_FAKE_USERS(id) ((((const Id *)id)->flag & LIB_FAKEUSER) ? 1 : 0)
#define ID_REAL_USERS(id) (((const Id *)id)->us - ID_FAKE_USERS(id))
#define ID_EXTRA_USERS(id) (((const Id *)id)->tag & LIB_TAG_EXTRAUSER ? 1 : 0)

#define ID_CHECK_UNDO(id) \
  ((GS((id)->name) != ID_SCR) && (GS((id)->name) != ID_WM) && (GS((id)->name) != ID_WS))

#define ID_DUNE_PATH(_main, _id) \
  ((_id)->lib ? (_id)->lib->filepath_abs : DUNE_main_dunefile_path((_dmain)))
#define ID_DUNE_PATH_FROM_GLOBAL(_id) \
  ((_id)->lib ? (_id)->lib->filepath_abs : DUNE_main_dunefile_path_from_global())

#define ID_MISSING(_id) ((((const Id *)(_id))->tag & LIB_TAG_MISSING) != 0)

#define ID_IS_LINKED(_id) (((const Id *)(_id))->lib != NULL)

/* That these are fairly high-level checks, should be used at user interaction level, not in
 * dune_lib_override typically (especially due to the check on LIB_TAG_EXTERN). */
#define ID_IS_OVERRIDABLE_LIB_HIERARCHY(_id) \
  (ID_IS_LINKED(_id) && !ID_MISSING(_id) && \
   (DUNE_idtype_get_info_from_id((const Id *)(_id))->flags & IDTYPE_FLAGS_NO_LIBLINKING) == 0 && \
   !ELEM(GS(((Id *)(_id))->name), ID_SCE))
#define ID_IS_OVERRIDABLE_LIB(_id) \
  (ID_IS_OVERRIDABLE_LIB_HIERARCHY((_id)) && (((const Id *)(_id))->tag & LIB_TAG_EXTERN) != 0)

/* The 3 checks below dont take into account whether given Id is linked or not (when
 * chaining overrides over several libs). User must ensure the Id is not linked itself
 * currently. */
/* TODO: add `_EDITABLE` versions of those macros (that would check if Id is linked or not)? */
#define ID_IS_OVERRIDE_LIB_REAL(_id) \
  (((const Id *)(_id))->override_lib != NULL && \
   ((const Id *)(_id))->override_lib->ref != NULL)

#define ID_IS_OVERRIDE_LIB_VIRTUAL(_id) \
  ((((const Id *)(_id))->flag & LIB_EMBEDDED_DATA_LIB_OVERRIDE) != 0)

#define ID_IS_OVERRIDE_LIB(_id) \
  (ID_IS_OVERRIDE_LIB_REAL(_id) || ID_IS_OVERRIDE_LIBRARY_VIRTUAL(_id))

#define ID_IS_OVERRIDE_LIB_HIERARCHY_ROOT(_id) \
  (!ID_IS_OVERRIDE_LIB_REAL(_id) || \
   ((Id *)(_id))->override_lib->hierarchy_root == ((ID *)(_id)))

#define ID_IS_OVERRIDE_LIBRARY_TEMPLATE(_id) \
  (((Id *)(_id))->override_lib != NULL && ((ID *)(_id))->override_library->reference == NULL)

#define ID_IS_ASSET(_id) (((const Id *)(_id))->asset_data != NULL)

/* Check whether datablock type is covered by copy-on-write. */
#define ID_TYPE_IS_COW(_id_type) \
  (!ELEM(_id_type, ID_LI, ID_IP, ID_SCR, ID_VF, ID_BR, ID_WM, ID_PAL, ID_PC, ID_WS, ID_IM))

/* Check whether data-block type requires copy-on-write from ID_RECALC_PARAMETERS.
 * Keep in sync with dune_id_eval_properties_copy. */
#define ID_TYPE_SUPPORTS_PARAMS_WITHOUT_COW(id_type) ELEM(id_type, ID_ME)

#ifdef GS
#  undef GS
#endif
#define GS(a) \
  (CHECK_TYPE_ANY(a, char *, const char *, char[66], const char[66]), \
   (IdType)(*((const short *)(a))))

#define ID_NEW_SET(_id, _idn) \
  (((Id *)(_id))->newid = (Id *)(_idn), \
   ((Id *)(_id))->newid->tag |= LIB_TAG_NEW, \
   (void *)((Id *)(_id))->newid)
#define ID_NEW_REMAP(a) \
  if ((a) && (a)->id.newid) { \
    (a) = (void *)(a)->id.newid; \
  } \
  ((void)0)

/* id->flag (persistent). */
enum {
  /* Don't del the data-block even if unused. */
  LIB_FAKEUSER = 1 << 9,
  /* The data-block is a sub-data of another one.
   * Direct persistent references are not allowed */
  LIB_EMBEDDED_DATA = 1 << 10,
  /* Data-block is from a lib and linked indirectly, with LIB_TAG_INDIRECT
   * tag set. But the current .dune file also has a weak pointer to it that
   * we want to restore if possible, and silently drop if it's missing.  */
  LIB_INDIRECT_WEAK_LINK = 1 << 11,
  /* The data-block is a sub-data of another 1, which is an override.
   * Also applies to shape-keys, though they are not 100% embedded data. */
  LIB_EMBEDDED_DATA_LIB_OVERRIDE = 1 << 12,
  /* The override data-block appears to not be needed anymore after resync with linked data, but it
   * was kept around (because e.g. detected as user-edited). */
  LIB_LIB_OVERRIDE_RESYNC_LEFTOVER = 1 << 13,
};

/* id->tag (runtime-only).
 * Those flags belong to three different categories,
 * which have different expected handling in code:
 *
 * - RESET_BEFORE_USE: piece of code that wants to use such flag
 *   has to ensure they are properly 'reset' first.
 * - RESET_AFTER_USE: piece of code that wants to use such flag has to ensure they are properly
 *   'reset' after usage
 *   (though 'lifetime' of those flags is a bit fuzzy, e.g. _RECALC ones are reset on depsgraph
 *   eval...).
 * - RESET_NEVER: those flags are 'status' one, and never actually need any reset
 *   (except on init during .blend file reading). */
enum {
  /* RESET_NEVER Datablock is from current .blend file. */
  LIB_TAG_LOCAL = 0,
  /* RESET_NEVER Datablock is from a library,
   * but is used (linked) directly by current .blend file. */
  LIB_TAG_EXTERN = 1 << 0,
  /* RESET_NEVER Datablock is from a library,
   * and is only used (linked) indirectly through other libraries. */
  LIB_TAG_INDIRECT = 1 << 1,

  /* RESET_AFTER_USE Flag used internally in readfile.c,
   * to mark IDs needing to be expanded (only done once). */
  LIB_TAG_NEED_EXPAND = 1 << 3,
  /* RESET_AFTER_USE Flag used internally in readfile.c to mark ID
   * placeholders for linked data-blocks needing to be read. */
  LIB_TAG_ID_LINK_PLACEHOLDER = 1 << 4,
  /* RESET_AFTER_USE */
  LIB_TAG_NEED_LINK = 1 << 5,

  /* RESET_NEVER tag data-block as a place-holder
   * (bc the real one could not be linked from its library e.g.). */
  LIB_TAG_MISSING = 1 << 6,

  /* RESET_NEVER tag data-block as being up-to-date regarding its reference. */
  LIB_TAG_OVERRIDE_LIB_REFOK = 1 << 9,
  /* RESET_NEVER tag data-block as needing an auto-override execution, if enabled. */
  LIB_TAG_OVERRIDE_LIB_AUTOREFRESH = 1 << 17,

  /* tag data-block as having an extra user. */
  LIB_TAG_EXTRAUSER = 1 << 2,
  /* tag data-block as having actually increased user-count for the extra virtual user. */
  LIB_TAG_EXTRAUSER_SET = 1 << 7,

  /* RESET_AFTER_USE tag newly duplicated/copied IDs (see #ID_NEW_SET macro above).
   * Also used internally in readfile.c to mark data-blocks needing do_versions. */
  LIB_TAG_NEW = 1 << 8,
  /* RESET_BEFORE_USE free test flag.
   * TODO: make it a RESET_AFTER_USE too. */
  LIB_TAG_DOIT = 1 << 10,
  /* RESET_AFTER_USE tag existing data before linking so we know what is new. */
  LIB_TAG_PRE_EXISTING = 1 << 11,

  /* The data-block is a copy-on-write/localized version.
   * RESET_NEVER
   *
   * warning This should not be cleared on existing data.
   * If support for this is needed, see T88026 as this flag controls memory ownership
   * of physics *shared* ptrs. */
  LIB_TAG_COPIED_ON_WRITE = 1 << 12,
  /* The data-block is not the original COW Id created by the depsgraph, but has be re-allocated
   * during the evaluation process of another Id.
   *
   * RESET_NEVER
   *
   * Typical example is object data, when evaluating the object's modifier stack the final obdata
   * can be different than the COW initial obdata Id.  */
  LIB_TAG_COPIED_ON_WRITE_EVAL_RESULT = 1 << 13,

  /* The data-block is fully outside of any Id management area, and should be considered as a
   * purely independent data.
   *
   * RESET_NEVER
   *
   * Only used by node-groups currently. */
  LIB_TAG_LOCALIZED = 1 << 14,

  /* RESET_NEVER tag data-block for freeing etc. behavior
   * (usually set when copying real one into temp/runtime one). */
  LIB_TAG_NO_MAIN = 1 << 15,          /* Datablock is not listed in Main database. */
  LIB_TAG_NO_USER_REFCOUNT = 1 << 16, /* Datablock does not refcount usages of other IDs. */
  /* Datablock was not allocated by standard system (BKE_libblock_alloc), do not free its memory
   * (usual type-specific freeing is called though). */
  LIB_TAG_NOT_ALLOCATED = 1 << 18,

  /* RESET_AFTER_USE Used by undo system to tag unchanged IDs re-used from old Main (instead of
   * read from memfile). */
  LIB_TAG_UNDO_OLD_ID_REUSED = 1 << 19,

  /* This Id is part of a tmp Main which is expected to be freed in a short time-frame.
   * Don't allow assigning this to non-tmp members (since it's likely to cause errors).
   * When set Id.sess_uuid isn't initialized, since the data isn't part of the session. */
  LIB_TAG_TEMP_MAIN = 1 << 20,

  /* The data-block is a lib override that needs re-sync to its linked ref */
  LIB_TAG_LIB_OVERRIDE_NEED_RESYNC = 1 << 21,
};

/* Tag given Id for an update in all the dep graphs. */
typedef enum IDRecalcFlag {
  /************************
   * Individual update tags, what Id gets tagged for update with. */

  /* Ob transformation changed. ** */
  ID_RECALC_TRANSFORM = (1 << 0),

  /* Geometry changed. **
   *
   * When ob of armature type gets tagged with this flag, its pose is
   * re-eval.
   *
   * When ob of other type is tagged with this flag it makes the modifier
   * stack to be re-eval.
   *
   * When ob data type (mesh, curve, ...) gets tagged with this flag it
   * makes all obs which shares this data-block to be updated.
   *
   * Note that the evaluation depends on the object-mode.
   * So edit-mesh data for example only reevaluate with the updated edit-mesh.
   * When geometry in the original ID has been modified #ID_RECALC_GEOMETRY_ALL_MODES
   * must be used instead.
   *
   * When a collection gets tagged with this flag, all objects depending on the geometry and
   * transforms on any of the objects in the collection are updated. */
  ID_RECALC_GEOMETRY = (1 << 1),

  /* ** Animation or time changed and animation is to be re-evaluated. ** */
  ID_RECALC_ANIMATION = (1 << 2),

  /* Particle sys changed. ** */
  /* Only do pathcache etc. */
  ID_RECALC_PSYS_REDO = (1 << 3),
  /* Reset everything including pointcache. */
  ID_RECALC_PSYS_RESET = (1 << 4),
  /* Only child settings changed. */
  ID_RECALC_PSYS_CHILD = (1 << 5),
  /* Phys type changed. */
  ID_RECALC_PSYS_PHYS = (1 << 6),

  /* Material and shading ** */

  /* For materials and node trees this means that topology of the shader tree
   * changed, and the shader is to be recompiled.
   * For obs it means that the draw batch cache is to be redone. */
  ID_RECALC_SHADING = (1 << 7),
  /* TODO: Consider adding an explicit ID_RECALC_SHADING_PARAMATERS
   * which can be used for cases when only socket value changed, to speed up
   * redraw update in that case. */

  /* Sel of the ID itself or its components (for example, vertices) did
   * change, and all the drawing data is to be updated. */
  ID_RECALC_SEL = (1 << 9),
  /* Flags on the base did change, and is to be copied onto all the copies of
   * corresponding obs. */
  ID_RECALC_BASE_FLAGS = (1 << 10),
  ID_RECALC_POINT_CACHE = (1 << 11),
  /* Only inform editors about the change. Is used to force update of editors
   * when data-block which is not a part of dependency graph did change.
   *
   * For example, brush texture did change and the preview is to be
   * re-rendered. */
  ID_RECALC_EDITORS = (1 << 12),

  /* Update copy on write component. **
   * This is most generic tag which should only be used when nothing else
   * matches */
  ID_RECALC_COPY_ON_WRITE = (1 << 13),

  /* Seqs in the seq did change.
   * Use this tag with a scene Id which owns the seqs. */
  ID_RECALC_SEQUENCER_STRIPS = (1 << 14),

  /* Runs on frame-change (used for seeking audio too). */
  ID_RECALC_FRAME_CHANGE = (1 << 15),

  ID_RECALC_AUDIO_FPS = (1 << 16),
  ID_RECALC_AUDIO_VOLUME = (1 << 17),
  ID_RECALC_AUDIO_MUTE = (1 << 18),
  ID_RECALC_AUDIO_LISTENER = (1 << 19),

  ID_RECALC_AUDIO = (1 << 20),

  /* This triggers copy on write for types that require it.
   * Exceptions to this can be added using #ID_TYPE_SUPPORTS_PARAMS_WITHOUT_COW,
   * this has the advantage that large arrays stored in the idea data don't
   * have to be copied on every update. */
  ID_RECALC_PARAMS = (1 << 21),

  /* Input has changed and datablock is to be reload from disk.
   * Applies to movie clips to inform that copy-on-written version is to be refreshed for the new
   * input file or for color space changes. */
  ID_RECALC_SRC = (1 << 23),

  /* Virtual recalc tag/marker required for undo in some cases, where actual data does not change
   * and hence do not require an update, but conceptually we are dealing with something new.
   *
   * Current known case: linked IDs made local without requiring any copy. While their users do not
   * require any update, they have actually been 'virtually' remapped from the linked ID to the
   * local one.
   */
  ID_RECALC_TAG_FOR_UNDO = (1 << 24),

  /* The node tree has changed in a way that affects its output nodes. */
  ID_RECALC_NTREE_OUTPUT = (1 << 25),

  /***************************************************************************
   * Pseudonyms, to have more semantic meaning in the actual code without
   * using too much low-level and implementation specific tags. */

  /* Update animation data-block itself, without doing full re-evaluation of
   * all dependent objects. */
  ID_RECALC_ANIMATION_NO_FLUSH = ID_RECALC_COPY_ON_WRITE,

  /* Ensure geometry of object and edit modes are both up-to-date in the evaluated data-block.
   * Example usage is when mesh validation modifies the non-edit-mode data,
   * which we want to be copied over to the evaluated data-block. */
  ID_RECALC_GEOMETRY_ALL_MODES = ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE,

  /***************************************************************************
   * Aggregate flags, use only for checks on runtime.
   * Do NOT use those for tagging. */

  /* Identifies that SOMETHING has been changed in this ID. */
  ID_RECALC_ALL = ~(0),
  /* Identifies that something in particle system did change. */
  ID_RECALC_PSYS_ALL = (ID_RECALC_PSYS_REDO | ID_RECALC_PSYS_RESET | ID_RECALC_PSYS_CHILD |
                        ID_RECALC_PSYS_PHYS),

} IDRecalcFlag;

/* To filter ID types (filter_id). 64 bit to fit all types. */
#define FILTER_ID_AC (1ULL << 0)
#define FILTER_ID_AR (1ULL << 1)
#define FILTER_ID_BR (1ULL << 2)
#define FILTER_ID_CA (1ULL << 3)
#define FILTER_ID_CU_LEGACY (1ULL << 4)
#define FILTER_ID_GD (1ULL << 5)
#define FILTER_ID_GR (1ULL << 6)
#define FILTER_ID_IM (1ULL << 7)
#define FILTER_ID_LA (1ULL << 8)
#define FILTER_ID_LS (1ULL << 9)
#define FILTER_ID_LT (1ULL << 10)
#define FILTER_ID_MA (1ULL << 11)
#define FILTER_ID_MB (1ULL << 12)
#define FILTER_ID_MC (1ULL << 13)
#define FILTER_ID_ME (1ULL << 14)
#define FILTER_ID_MSK (1ULL << 15)
#define FILTER_ID_NT (1ULL << 16)
#define FILTER_ID_OB (1ULL << 17)
#define FILTER_ID_PAL (1ULL << 18)
#define FILTER_ID_PC (1ULL << 19)
#define FILTER_ID_SCE (1ULL << 20)
#define FILTER_ID_SPK (1ULL << 21)
#define FILTER_ID_SO (1ULL << 22)
#define FILTER_ID_TE (1ULL << 23)
#define FILTER_ID_TXT (1ULL << 24)
#define FILTER_ID_VF (1ULL << 25)
#define FILTER_ID_WO (1ULL << 26)
#define FILTER_ID_PA (1ULL << 27)
#define FILTER_ID_CF (1ULL << 28)
#define FILTER_ID_WS (1ULL << 29)
#define FILTER_ID_LP (1ULL << 31)
#define FILTER_ID_CV (1ULL << 32)
#define FILTER_ID_PT (1ULL << 33)
#define FILTER_ID_VO (1ULL << 34)
#define FILTER_ID_SIM (1ULL << 35)

#define FILTER_ID_ALL \
  (FILTER_ID_AC | FILTER_ID_AR | FILTER_ID_BR | FILTER_ID_CA | FILTER_ID_CU_LEGACY | \
   FILTER_ID_GD | FILTER_ID_GR | FILTER_ID_IM | FILTER_ID_LA | FILTER_ID_LS | FILTER_ID_LT | \
   FILTER_ID_MA | FILTER_ID_MB | FILTER_ID_MC | FILTER_ID_ME | FILTER_ID_MSK | FILTER_ID_NT | \
   FILTER_ID_OB | FILTER_ID_PA | FILTER_ID_PAL | FILTER_ID_PC | FILTER_ID_SCE | FILTER_ID_SPK | \
   FILTER_ID_SO | FILTER_ID_TE | FILTER_ID_TXT | FILTER_ID_VF | FILTER_ID_WO | FILTER_ID_CF | \
   FILTER_ID_WS | FILTER_ID_LP | FILTER_ID_CV | FILTER_ID_PT | FILTER_ID_VO | FILTER_ID_SIM)

/**
 * This enum defines the index assigned to each type of IDs in the array returned by
 * #set_listbasepointers, and by extension, controls the default order in which each ID type is
 * processed during standard 'foreach' looping over all IDs of a #Main data-base.
 *
 * About Order:
 * ------------
 *
 * This is (loosely) defined with a relationship order in mind, from lowest level (ID types using,
 * referencing almost no other ID types) to highest level (ID types potentially using many other ID
 * types).
 *
 * So e.g. it ensures that this dependency chain is respected:
 *   Material <- Mesh <- Object <- Collection <- Scene
 *
 * Default order of processing of IDs in 'foreach' macros (#FOREACH_MAIN_ID_BEGIN and the like),
 * built on top of set_listbasepointers, is actually reversed compared to the order defined here,
 * since processing usually needs to happen on users before it happens on used IDs (when freeing
 * e.g.).
 *
 * DO NOT rely on this order as being full-proofed dependency order, there are many cases were it
 * can be violated (most obvious cases being custom properties and drivers, which can reference any
 * other ID types).
 *
 * However, this order can be considered as an optimization heuristic, especially when processing
 * relationships in a non-recursive pattern: in typical cases, a vast majority of those
 * relationships can be processed fine in the first pass, and only few additional passes are
 * required to address all remaining relationship cases.
 * See e.g. how dune_lib_unused_linked_data_set_tag is doing this.
 */
enum {
  /* Special case: Library, should never ever depend on any other type. */
  INDEX_ID_LI = 0,

  /* Animation types, might be used by almost all other types. */
  INDEX_ID_IP, /* Deprecated. */
  INDEX_ID_AC,

  /* Grease Pencil, special case, should be with the other obdata, but it can also be used by many
   * other ID types, including node trees e.g.
   * So there is no proper place for those, for now keep close to the lower end of the processing
   * hierarchy, but we may want to re-evaluate that at some point. */
  INDEX_ID_GD,

  /* Node trees, abstraction for procedural data, potentially used by many other ID types.
   *
   * NOTE: While node trees can also use many other ID types, they should not /own/ any of those,
   * while they are being owned by many other ID types. This is why they are placed here. */
  INDEX_ID_NT,

  /* File-wrapper types, those usually 'embed' external files in Blender, with no dependencies to
   * other ID types. */
  INDEX_ID_VF,
  INDEX_ID_TXT,
  INDEX_ID_SO,

  /* Image/movie types, can be used by shading ID types, but also directly by Objects, Scenes, etc.
   */
  INDEX_ID_MSK,
  INDEX_ID_IM,
  INDEX_ID_MC,

  /* Shading types. */
  INDEX_ID_TE,
  INDEX_ID_MA,
  INDEX_ID_LS,
  INDEX_ID_WO,

  /* Simulation-related types. */
  INDEX_ID_CF,
  INDEX_ID_SIM,
  INDEX_ID_PA,

  /* Shape Keys snow-flake, can be used by several obdata types. */
  INDEX_ID_KE,

  /* Object data types. */
  INDEX_ID_AR,
  INDEX_ID_ME,
  INDEX_ID_CU_LEGACY,
  INDEX_ID_MB,
  INDEX_ID_CV,
  INDEX_ID_PT,
  INDEX_ID_VO,
  INDEX_ID_LT,
  INDEX_ID_LA,
  INDEX_ID_CA,
  INDEX_ID_SPK,
  INDEX_ID_LP,

  /* Collection and object types. */
  INDEX_ID_OB,
  INDEX_ID_GR,

  /* Preset-like, not-really-data types, can use many other ID types but should never be used by
   * any actual data type (besides Scene, due to tool settings). */
  INDEX_ID_PAL,
  INDEX_ID_PC,
  INDEX_ID_BR,

  /* Scene, after preset-like ID types because of tool settings. */
  INDEX_ID_SCE,

  /* UI-related types, should never be used by any other data type. */
  INDEX_ID_SCR,
  INDEX_ID_WS,
  INDEX_ID_WM,

  /* Special values. */
  INDEX_ID_NULL,
  INDEX_ID_MAX,
};
