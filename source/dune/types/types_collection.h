/* Ob groups, one ob can be in many groups at once. */
#pragma once

#include "types_id.h"
#include "types_defs.h"
#include "types_list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Collection;
struct Ob;

typedef struct CollectionOb {
  struct CollectionOb *next, *prev;
  struct Ob *ob;
} CollectionOb;

typedef struct CollectionChild {
  struct CollectionChild *next, *prev;
  struct Collection *collection;
} CollectionChild;

enum eCollectionLineArtUsage {
  COLLECTION_LRT_INCLUDE = 0,
  COLLECTION_LRT_OCCLUSION_ONLY = (1 << 0),
  COLLECTION_LRT_EXCLUDE = (1 << 1),
  COLLECTION_LRT_INTERSECTION_ONLY = (1 << 2),
  COLLECTION_LRT_NO_INTERSECTION = (1 << 3),
};

enum eCollectionLineArtFlags {
  COLLECTION_LRT_USE_INTERSECTION_MASK = (1 << 0),
};

typedef struct Collection {
  Id id;

  /* CollectionOb. */
  List gob;
  /* CollectionChild. */
  List children;

  struct PreviewImg *preview;

  unsigned int layer TYPES_DEPRECATED;
  float instance_offset[3];

  short flag;
  /* Runtime-only, always cleared on file load. */
  short tag;

  short lineart_usage;         /* eCollectionLineArt_Usage */
  unsigned char lineart_flags; /* eCollectionLineArt_Flags */
  unsigned char lineart_intersection_mask;
  char _pad[6];

  int16_t color_tag;

  /* Runtime. Cache of obs in this collection and all its
   * children. This is created on demand when e.g. some phys
   * sim needs it, we don't want to have it for every
   * collections due to mem usage reasons. */
  List ob_cache;

  /* Need this for line art sub-collection selections. */
  List ob_cache_instanced;

  /* Runtime. List of collections that are a parent of this
   * datablock. */
  List parents;

  /* Deprecated */
  struct SceneCollection *collection TYPES_DEPRECATED;
  struct ViewLayer *view_layer TYPES_DEPRECATED;
} Collection;

/* Collection->flag */
enum {
  COLLECTION_HIDE_VIEWPORT = (1 << 0),             /* Disable in viewports. */
  COLLECTION_HIDE_SEL = (1 << 1),               /* Not sel in viewport. */
  /* COLLECTION_DISABLED_DEPRECATED = (1 << 2), */ /* Not used anymore */
  COLLECTION_HIDE_RENDER = (1 << 3),               /* Disable in renders. */
  COLLECTION_HAS_OB_CACHE = (1 << 4),          /* Runtime: ob_cache is populated. */
  COLLECTION_IS_MASTER = (1 << 5), /* Is master collection embedded in the scene. */
  COLLECTION_HAS_OB_CACHE_INSTANCED = (1 << 6), /* for ob_cache_instanced. */
};

/* Collection->tag */
enum {
  /* That code (dune_main_collections_parent_relations_rebuild and the like)
   * is called from very low-level places, like e.g Id remapping...
   * Using a generic tag like LIB_TAG_DOIT for this is just impossible, we need our very own. */
  COLLECTION_TAG_RELATION_REBUILD = (1 << 0),
};

/* Collection->color_tag. */
typedef enum CollectionColorTag {
  COLLECTION_COLOR_NONE = -1,
  COLLECTION_COLOR_01,
  COLLECTION_COLOR_02,
  COLLECTION_COLOR_03,
  COLLECTION_COLOR_04,
  COLLECTION_COLOR_05,
  COLLECTION_COLOR_06,
  COLLECTION_COLOR_07,
  COLLECTION_COLOR_08,

  COLLECTION_COLOR_TOT,
} CollectionColorTag;

#ifdef __cplusplus
}
#endif
