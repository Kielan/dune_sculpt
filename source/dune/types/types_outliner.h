#pragma once

#include "types_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Id;

typedef struct TreeStoreElem {
  short type, nr, flag, used;

  /* We actually also store non-ID data in this ptr for identifying
   * the TreeStoreElem for a TreeElement when rebuilding the tree. Ugly! */
  struct Id *id;
} TreeStoreElem;

/* Used only to store data in dune files. */
typedef struct TreeStore {
  /* Was previously used for memory pre-allocation. */
  int totelem TYPES_DEPRECATED;
  /* Number of elements in data array. */
  int usedelem;
  /* Elements to be packed from mempool in `writefile.c`
   * or extracted to mempool in `readfile.c`. */
  TreeStoreElem *data;
} TreeStore;

/* TreeStoreElem.flag */
enum {
  TSE_CLOSED = (1 << 0),
  TSE_SELECTED = (1 << 1),
  TSE_TEXTBUT = (1 << 2),
  TSE_CHILDSEARCH = (1 << 3),
  TSE_SEARCHMATCH = (1 << 4),
  TSE_HIGHLIGHTED = (1 << 5),
  TSE_DRAG_INTO = (1 << 6),
  TSE_DRAG_BEFORE = (1 << 7),
  TSE_DRAG_AFTER = (1 << 8),
  /* Needed because outliner-only elements can be active */
  TSE_ACTIVE = (1 << 9),
  /* TSE_ACTIVE_WALK = (1 << 10), */ /* Unused */
  TSE_HIGHLIGHTED_ICON = (1 << 11),
  TSE_DRAG_ANY = (TSE_DRAG_INTO | TSE_DRAG_BEFORE | TSE_DRAG_AFTER),
  TSE_HIGHLIGHTED_ANY = (TSE_HIGHLIGHTED | TSE_HIGHLIGHTED_ICON),
};

/* TreeStoreElem.types */
typedef enum eTreeStoreElemType {
  /* If an element is of this type, `TreeStoreElem.id` points to a valid ID and the ID-type can be
   * received through `TreeElement.idcode` (or `GS(TreeStoreElem.id->name)`). Note however that the
   * types below may also have a valid ID pointer (see #TSE_IS_REAL_ID()).
   *
   * In cases where the type is still checked against "0" (even implicitly), please replace it with
   * an explicit check against `TSE_SOME_ID`. */
  TSE_SOME_ID = 0,

  TSE_NLA = 1, /* NO ID */
  TSE_NLA_ACTION = 2,
  TSE_DEFGROUP_BASE = 3,
  TSE_DEFGROUP = 4,
  TSE_BONE = 5,
  TSE_EBONE = 6,
  TSE_CONSTRAINT_BASE = 7,
  TSE_CONSTRAINT = 8,
  TSE_MOD_BASE = 9,
  TSE_MOD = 10,
  TSE_LINKED_OB = 11,
  /* TSE_SCRIPT_BASE     = 12, */ /* UNUSED */
  TSE_POSE_BASE = 13,
  TSE_POSE_CHANNEL = 14,
  TSE_ANIM_DATA = 15,
  TSE_DRIVER_BASE = 16,           /* NO ID */
  /* TSE_DRIVER          = 17, */ /* UNUSED */

  /* TSE_PROXY = 18,           */ /* UNUSED */
  TSE_R_LAYER_BASE = 19,
  TSE_R_LAYER = 20,
  /* TSE_R_PASS          = 21, */ /* UNUSED */
  /* TSE_LINKED_MAT = 22, */
  /* NOTE: is used for light group. */
  /* TSE_LINKED_LAMP = 23, */
  TSE_POSEGRP_BASE = 24,
  TSE_POSEGRP = 25,
  TSE_SEQ = 26,     /* NO ID */
  TSE_SEQ_STRIP = 27,    /* NO ID */
  TSE_SEQ_DUP = 28, /* NO ID */
  TSE_LINKED_PSYS = 29,
  TSE_API_STRUCT = 30,        /* NO ID */
  TSE_API_PROP = 31,      /* NO ID */
  TSE_API_ARRAY_ELEM = 32,    /* NO ID */
  TSE_NLA_TRACK = 33,         /* NO ID */
  /* TSE_KEYMAP = 34, */      /* UNUSED */
  /* TSE_KEYMAP_ITEM = 35, */ /* UNUSED */
  TSE_ID_BASE = 36,           /* NO ID */
  TSE_PEN_LAYER = 37,          /* NO ID */
  TSE_LAYER_COLLECTION = 38,
  TSE_SCENE_COLLECTION_BASE = 39,
  TSE_VIEW_COLLECTION_BASE = 40,
  TSE_SCENE_OBJECTS_BASE = 41,
  TSE_PEN_EFFECT_BASE = 42,
  TSE_PEN_EFFECT = 43,
  TSE_LIB_OVERRIDE_BASE = 44,
  TSE_LIB_OVERRIDE = 45,
} eTreeStoreElemType;

/** Check whether given TreeStoreElem should have a real ID in TreeStoreElem.id member. */
#define TSE_IS_REAL_ID(_tse) \
  (!ELEM((_tse)->type, \
         TSE_NLA, \
         TSE_NLA_TRACK, \
         TSE_DRIVER_BASE, \
         TSE_SEQ, \
         TSE_SEQ_STRIP, \
         TSE_SEQ_DUP, \
         TSE_API_STRUCT, \
         TSE_API_PROP, \
         TSE_API_ARRAY_ELEM, \
         TSE_ID_BASE, \
         TSE_PEN_LAYER))

#ifdef __cplusplus
}
#endif
