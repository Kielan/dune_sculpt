#pragma once

/** Mesh data structures, used for mesh editing operations
 ** that benefit from accessing connectivity information. **/

#include "lib_assert.h"

/* disable holes for now,
 * these are ifdef'd because they use more memory and can't be saved in DNA currently */
// #define USE_MESH_HOLES

struct MeshEdge;
struct MeshFace;
struct MeshLoop;
struct MeshVert;
struct Mesh;

struct MeshLoopNorSpaceArray;

struct lib_mempool;

/* NOTE: it is very important for BMHeader to start with two
 * pointers. this is a requirement of mempool's method of
 * iteration.
 *
 * hrm. it doesn't but still works ok, remove the comment above? - campbell.
 */

// #pragma GCC diagnostic error "-Wpadded"

/**
 * MeshHeader
 *
 * All mesh elements begin with a MeshHeader. This structure
 * hold several types of data
 *
 * 1: The type of the element (vert, edge, loop or face)
 * 2: Persistent "header" flags/markings (smooth, seam, select, hidden, etc)
 *    note that this is different from the "tool" flags.
 * 3: Unique id in the Mesh.
 * 4: some elements for internal record keeping.
 */
typedef struct MeshHeader {
  /** Customdata layers. */
  void *data;

  /**
   * - Use mesh_elem_index_get/set macros for index
   * - Uninitialized to -1 so we can easily tell its not set.
   * - Used for edge/vert/face/loop, check BMesh.elem_index_dirty for valid index values,
   *   this is abused by various tools which set it dirty.
   * - For loops this is used for sorting during tessellation.
   */
  int index;

  /** Element geometric type (verts/edges/loops/faces). */
  char htype;
  /** This would be a CD layer, see below. */
  char hflag;

  /**
   * Internal use only!
   * We are very picky about not bloating this struct
   * but in this case its padded up to 16 bytes anyway,
   * so adding a flag here gives no increase in size.
   */
  char api_flag;
  // char _pad;
} MeshHeader;

LIB_STATIC_ASSERT((sizeof(MeshHeader) <= 16), "MeshHeader size has grown!");

/* NOTE: need some way to specify custom locations for custom data layers.  so we can
 * make them point directly into structs.  and some way to make it only happen to the
 * active layer, and properly update when switching active layers. */

typedef struct MeshVert {
  MeshHeader head;

  float co[3]; /* vertex coordinates */
  float no[3]; /* vertex normal */

  /**
   * Pointer to (any) edge using this vertex (for disk cycles).
   *
   * Some higher level functions set this to different edges that use this vertex,
   * which is a bit of an abuse of internal Mesh data but also works OK for now
   * (use with care!).
   */
  struct MeshEdge *e;
} MeshVert;

typedef struct MeshVertOpFlag {
  MeshVert base;
  struct MeshFlagLayer *opflags;
} MeshVertOpFlag;

/* disk link structure, only used by edges */
typedef struct MeshDiskLink {
  struct MeshEdge *next, *prev;
} MeshDiskLink;

typedef struct MeshEdge {
  MeshHeader head;

  /**
   * Vertices (unordered),
   *
   * Although the order can be used at times,
   * when extruding a face from a wire-edge for example.
   *
   * Operations that create/subdivide edges shouldn't flip the order
   * unless there is a good reason to do so.
   */
  MeshVert *v1, *v2;

  /** The list of loops around the edge, see doc-string for MeshLoop.radial_next
   ** for an example of using this to loop over all faces used by an edge. **/
  struct MeshLoop *l;

  /**
   * Disk Cycle Pointers
   *
   * relative data: d1 indicates the next/prev
   * edge around vertex v1 and d2 does the same for v2.
   */
  MeshDiskLink v1_disk_link, v2_disk_link;
} MeshEdge;

typedef struct MeshEdgeOpFlag {
  MeshEdge base;
  struct MeshFlagLayer *oflags;
} MeshEdgeOpFlag;

typedef struct MeshLoop {
  MeshHeader head;
  /* notice no flags layer */

  /** The vertex this loop points to.
    - This vertex must be unique within the cycle. **/
  struct MeshVert *v;

  /**
   * The edge this loop uses.
   *
   * Vertices (MeshLoop.v & MeshLoop.next.v) always contain vertices from (MeshEdge.v1 &MeshEdge.v2).
   * Although no assumptions can be made about the order,
   * as this isn't meaningful for mesh topology.
   *
   * - This edge must be unique within the cycle (defined by MeshLoop.next & MeshLoop.prev links).
   */
  struct MeshEdge *e;
  /**
   * The face this loop is part of.
   *
   * - This face must be shared by all within the cycle.
   *   Used as a back-pointer so loops can know the face they define.
   */
  struct MeshFace *f;

  /**
   * Other loops connected to this edge.
   *
   * This is typically use for accessing an edges faces,
   * however this is done by stepping over it's loops.
   *
   * - This is a circular list, so there are no first/last storage of the "radial" data.
   *   Instead MeshEdge.l points to any one of the loops that use it.
   *
   * - Since the list is circular, the particular loop referenced doesn't matter,
   *   as all other loops can be accessed from it.
   *
   * - Every loop in this radial list has the same value for MeshLoop.e.
   *
   * - The value for MeshLoop.v might not match the radial next/previous
   *   as this depends on the face-winding.
   *   You can be sure MeshLoop.v will either MeshEdge.v1 or MeshEdge.v2 of MeshLoop.e,
   *
   * - Unlike face-winding (which defines if the direction the face points),
   *   next and previous are insignificant. The list could be reversed for example,
   *   without any impact on the topology.
   *
   * This is an example of looping over an edges faces using MeshLoop.radial_next.
   *
   * .c
   * MeshLoop *l_iter = edge->l;
   * do {
   *   operate_on_face(l_iter->f);
   * } while ((l_iter = l_iter->radial_next) != edge->l);
   *
   */
  struct MeshLoop *radial_next, *radial_prev;

  /**
   * Other loops that are part of this face.
   *
   * This is typically used for accessing all vertices/edges in a faces.
   *
   * - This is a circular list, so there are no first/last storage of the "cycle" data.
   *   Instead MeshFace.l_first points to any one of the loops that are part of this face.
   *
   * - Since the list is circular, the particular loop referenced doesn't matter,
   *   as all other loops can be accessed from it.
   *
   * - Every loop in this "cycle" list has the same value for #BMLoop.f.
   *
   * - The direction of this list defines the face winding.
   *   Reversing the list flips the face.
   *
   * This is an example loop over all vertices and edges of a face.
   *
   * .c
   * MeshLoop *l_first, *l_iter;
   * l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
   * do {
   *   operate_on_vert(l_iter->v);
   *   operate_on_edge(l_iter->e);
   * } while ((l_iter = l_iter->next) != l_first);
   *
   */
  struct MeshLoop *next, *prev;
} MeshLoop;

/* can cast MeshFace/MeshEdge/MeshVert, but NOT MeshLoop, since these don't have a flag layer */
typedef struct MeshElemFlag {
  MeshHeader head;
} MeshElemFlag;

/* can cast anything to this, including MeshLoop */
typedef struct MeshElem {
  MeshHeader head;
} MeshElem;

#ifdef USE_MESH_HOLES
/* eventually, this structure will be used for supporting holes in faces */
typedef struct MeshLoopList {
  struct MeshLoopList *next, *prev;
  struct MeshLoop *first, *last;
} MeshLoopList;
#endif

typedef struct MeshFace {
  MeshHeader head;

#ifdef USE_MESH_HOLES
  int totbounds; /* Total boundaries, is one plus the number of holes in the face. */
  ListBase loops;
#else
  MeshLoop *l_first;
#endif
  /**
   * Number of vertices in the face
   * (the length of MeshFace.l_first circular linked list).
   */
  int len;
  float no[3];  /* face normal */
  short mat_nr; /* material index */
  //  short _pad[3];
} MeshFace;

typedef struct MeshFaceOpFlag {
  MeshFace base;
  struct MeshFlagLayer *opflags;
} MeshFaceOpFlag;

typedef struct MeshFlagLayer {
  short f; /* flags */
} MeshFlagLayer;

// #pragma GCC diagnostic ignored "-Wpadded"

typedef struct Mesh {
  int totvert, totedge, totloop, totface;
  int totvertsel, totedgesel, totfacesel;

  /**
   * Flag index arrays as being dirty so we can check if they are clean and
   * avoid looping over the entire vert/edge/face/loop array in those cases.
   * valid flags are: `(MESH_VERT | MESH_EDGE | MESH_FACE | MESH_LOOP)`
   */
  char elem_index_dirty;

  /** Flag array table as being dirty so we know when its safe to use it, 
   ** or when it needs to be re-created. **/
  char elem_table_dirty;

  /* element pools */
  struct lib_mempool *vpool, *epool, *lpool, *fpool;

  /* mempool lookup tables (optional)
   * index tables, to map indices to elements via
   * mesh_elem_table_ensure and associated functions.  don't
   * touch this or read it directly.\
   * Use mesh_elem_table_ensure(), mesh_vert/edge/face_at_index() */
  MeshVert **vtable;
  MeshEdge **etable;
  MeshFace **ftable;

  /* size of allocated tables */
  int vtable_tot;
  int etable_tot;
  int ftable_tot;

  /* operator api stuff (must be all NULL or all alloc'd) */
  struct lib_mempool *vtoolflagpool, *etoolflagpool, *ftoolflagpool;

  uint use_toolflags : 1;

  int toolflag_index;

  CustomData vdata, edata, ldata, pdata;

#ifdef USE_MESH_HOLES
  struct lib_mempool *looplistpool;
#endif

  struct MeshLoopNorSpaceArray *lnor_spacearr;
  char spacearr_dirty;

  /* Should be copy of scene select mode. */
  /* Stored in MeshEditMesh too, this is a bit confusing,
   * make sure they're in sync!
   * Only use when the edit mesh can't be accessed - campbell */
  short selectmode;

  /* id of the shape key this mesh came from */
  int shapenr;

  int totflags;
  ListBase selected;

  /**
   * The active face.
   * This is kept even when unselected, mainly so UV editing can keep showing the
   * active faces image while the selection is being modified in the 3D viewport.
   *
   * Without this the active image in the UV editor would flicker in a distracting way
   * while changing selection in the 3D viewport.
   */
  MeshFace *act_face;

  /** List of MeshOpError, used for operator error handling. */
  ListBase errorstack;

  /**
   * Keep a single reference to the Python instance of this #BMesh (if any exists).
   *
   * This allows save invalidation of a #BMesh when it's freed,
   * so the Python object will report it as having been removed,
   * instead of crashing on invalid memory access.
   */
  void *py_handle;
} Mesh;

/** MeshHeader.htype (char) */
enum {
  MESH_VERT = 1,
  MESH_EDGE = 2,
  MESH_LOOP = 4,
  MESH_FACE = 8,
};

typedef struct MeshLoopNorEditData {
  int loop_index;
  MeshLoop *loop;
  float niloc[3];
  float nloc[3];
  float *loc;
  short *clnors_data;
} MeshLoopNorEditData;

typedef struct MeshLoopNorEditDataArray {
  MeshLoopNorEditData *lnor_editdata;
  /**
   * This one has full amount of loops,
   * used to map loop index to actual MeshLoopNorEditData struct.
   */
  MeshLoopNorEditData **lidx_to_lnor_editdata;

  int cd_custom_normal_offset;
  int totloop;
} MeshLoopNorEditDataArray;

#define MESH_ALL (MESH_VERT | MESH_EDGE | MESH_LOOP | MESH_FACE)
#define MESH_ALL_NOLOOP (MESH_VERT | MESH_EDGE | MESH_FACE)

/** Mesh.spacearr_dirty */
enum {
  MESH_SPACEARR_DIRTY = 1 << 0,
  MESH_SPACEARR_DIRTY_ALL = 1 << 1,
  MESH_SPACEARR_MESH_OP_SET = 1 << 2,
};

/* args for _Generic */
#define _MESH_GENERIC_TYPE_ELEM_NONCONST \
  void *, MeshVert *, MeshEdge *, MeshLoop *, MeshFace *, MeshVertOpFlag *, MeshEdgeOpFlag *, MeshFace_OFlag *, \
      MeshElem *, MeshElemF *, MeshHeader *

#define _MESH_GENERIC_TYPE_ELEM_CONST \
  const void *, const MeshVert *, const MeshEdge *, const MeshLoop *, const MeshFace *, \
      const MeshVertOpFlag *, const MeshEdgeOpFlag *, const MeshFaceOpFlag *, const MeshElem *, \
      const MeshElemF *, const MeshHeader *, void *const, MeshVert *const, MeshEdge *const, \
      MeshLoop *const, MeshFace *const, MeshElem *const, MeshElemF *const, MeshHeader *const

#define MESH_CHECK_TYPE_ELEM_CONST(ele) CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPES_CONST)

#define MESH_CHECK_TYPE_ELEM_NONCONST(ele) CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_ELEM_NONCONST)

#define Mesh_CHECK_TYPE_ELEM(ele) \
  CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_ELEM_NONCONST, _MESH_GENERIC_TYPE_ELEM_CONST)

/* vert */
#define _MESH_GENERIC_TYPE_VERT_NONCONST MeshVert *, MeshVertOpFlag *
#define _MESH_GENERIC_TYPE_VERT_CONST const MeshVert *, const MeshVertOpFlag *
#define MESH_CHECK_TYPE_VERT_CONST(ele) CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_VERT_CONST)
#define MESH_CHECK_TYPE_VERT_NONCONST(ele) CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_ELEM_NONCONST)
#define MESH_CHECK_TYPE_VERT(ele) \
  CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_VERT_NONCONST, _MESH_GENERIC_TYPE_VERT_CONST)
/* edge */
#define _MESH_GENERIC_TYPE_EDGE_NONCONST MeshEdge *, MeshEdgeOpFlag *
#define _MESH_GENERIC_TYPE_EDGE_CONST const MeshEdge *, const MeshEdge_OpFlag *
#define MESH_CHECK_TYPE_EDGE_CONST(ele) CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_EDGE_CONST)
#define MESH_CHECK_TYPE_EDGE_NONCONST(ele) CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_ELEM_NONCONST)
#define MESH_CHECK_TYPE_EDGE(ele) \
  CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_EDGE_NONCONST, _MESH_GENERIC_TYPE_EDGE_CONST)
/* face */
#define _MESH_GENERIC_TYPE_FACE_NONCONST MeshFace *, MeshFaceOpFlag *
#define _MESH_GENERIC_TYPE_FACE_CONST const MeshFace *, const MeshFaceOpFlag *
#define MESH_CHECK_TYPE_FACE_CONST(ele) CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_FACE_CONST)
#define MESH_CHECK_TYPE_FACE_NONCONST(ele) CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_ELEM_NONCONST)
#define MESH_CHECK_TYPE_FACE(ele) \
  CHECK_TYPE_ANY(ele, _MESH_GENERIC_TYPE_FACE_NONCONST, _MESH_GENERIC_TYPE_FACE_CONST)

/* Assignment from a void* to a typed pointer is not allowed in C++,
 * casting the LHS to void works fine though.
 */
#ifdef __cplusplus
#  define MESH_CHECK_TYPE_ELEM_ASSIGN(ele) (MESH_CHECK_TYPE_ELEM(ele)), *((void **)&ele)
#else
#  define MESH_CHECK_TYPE_ELEM_ASSIGN(ele) (MESH_CHECK_TYPE_ELEM(ele)), ele
#endif

/** MeshHeader.hflag (char) */
enum {
  MESH_ELEM_SELECT = (1 << 0),
  MESH_ELEM_HIDDEN = (1 << 1),
  MESH_ELEM_SEAM = (1 << 2),
  /**
   * used for faces and edges, note from the user POV,
   * this is a sharp edge when disabled */
  MESH_ELEM_SMOOTH = (1 << 3),
  /**
   * Internal flag, used for ensuring correct normals
   * during multi-resolution interpolation, and any other time
   * when temp tagging is handy.
   * always assume dirty & clear before use.
   */
  MESH_ELEM_TAG = (1 << 4),

  MESH_ELEM_DRAW = (1 << 5), /* edge display */

  /** Spare tag, assumed dirty, use define in each function to name based on use. */
  MESH_ELEM_TAG_ALT = (1 << 6),

  /**
   * For low level internal API tagging,
   * since tools may want to tag verts and not have functions clobber them.
   * Leave cleared!
   */
  MESH_ELEM_INTERNAL_TAG = (1 << 7),
};

struct BPyMeshGeneric;
extern void bpy_mesh_generic_invalidate(struct BPyMeshGeneric *self);

typedef bool (*MeshVertFilterFn)(const MeshElem *, void *user_data);
typedef bool (*MeshVertFilterFn)(const MeshVert *, void *user_data);
typedef bool (*MeshEdgeFilterFn)(const MeshEdge *, void *user_data);
typedef bool (*MeshFaceFilterFn)(const MeshFace *, void *user_data);
typedef bool (*MeshLoopFilterFn)(const MeshLoop *, void *user_data);
typedef bool (*MeshLoopPairFilterFn)(const MeshLoop *, const MeshLoop *, void *user_data);

/* defines */
#define MESH_ELEM_CD_SET_INT(ele, offset, f) \
  { \
    CHECK_TYPE_NONCONST(ele); \
    lib_assert(offset != -1); \
    *((int *)((char *)(ele)->head.data + (offset))) = (f); \
  } \
  (void)0

#define MESH_ELEM_CD_GET_INT(ele, offset) \
  (lib_assert(offset != -1), *((int *)((char *)(ele)->head.data + (offset))))

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#  define MESH_ELEM_CD_GET_VOID_P(ele, offset) \
    (lib_assert(offset != -1), \
     _Generic(ele, \
              GENERIC_TYPE_ANY(PTR_OFFSET((ele)->head.data, offset), \
                               _MESH_GENERIC_TYPE_ELEM_NONCONST), \
              GENERIC_TYPE_ANY((const void *)PTR_OFFSET((ele)->head.data, offset), \
                               _MESH_GENERIC_TYPE_ELEM_CONST)))
#else
#  define MESH_ELEM_CD_GET_VOID_P(ele, offset) \
    (lib_assert(offset != -1), (void *)((char *)(ele)->head.data + (offset)))
#endif

#define MESH_ELEM_CD_SET_FLOAT(ele, offset, f) \
  { \
    CHECK_TYPE_NONCONST(ele); \
    lib_assert(offset != -1); \
    *((float *)((char *)(ele)->head.data + (offset))) = (f); \
  } \
  (void)0

#define MESH_ELEM_CD_GET_FLOAT(ele, offset) \
  (lib_assert(offset != -1), *((float *)((char *)(ele)->head.data + (offset))))

#define MESH_ELEM_CD_GET_FLOAT_AS_UCHAR(ele, offset) \
  (lib_assert(offset != -1), (uchar)(MESH_ELEM_CD_GET_FLOAT(ele, offset) * 255.0f))

/* Forward declarations. */

#ifdef USE_MESH_HOLES
#  define MESH_FACE_FIRST_LOOP(p) (((MeshLoopList *)((p)->loops.first))->first)
#else
#  define MESH_FACE_FIRST_LOOP(p) ((p)->l_first)
#endif

#define MESH_DISK_EDGE_NEXT(e, v) \
  (CHECK_TYPE_INLINE(e, MeshEdge *), \
   CHECK_TYPE_INLINE(v, MeshVert *), \
   lib_assert(mesh_vert_in_edge(e, v)), \
   (((&e->v1_disk_link)[v == e->v2]).next))
#define MESH_DISK_EDGE_PREV(e, v) \
  (CHECK_TYPE_INLINE(e, MeshEdge *), \
   CHECK_TYPE_INLINE(v, MeshVert *), \
   lib_assert(mesh_vert_in_edge(e, v)), \
   (((&e->v1_disk_link)[v == e->v2]).prev))

/**
 * size to use for stack arrays when dealing with NGons,
 * alloc after this limit is reached.
 * this value is rather arbitrary */
#define MESH_DEFAULT_NGON_STACK_SIZE 32
/**
 * size to use for stack arrays dealing with connected mesh data
 * verts of faces, edges of vert - etc.
 * often used with mesh_iter_as_arrayn() */
#define MESH_DEFAULT_ITER_STACK_SIZE 16

/* avoid inf loop, this value is arbitrary
 * but should not error on valid cases */
#define MESH_LOOP_RADIAL_MAX 10000
#define MESH_NGON_MAX 100000

/* setting zero so we can catch bugs in OpenMP/Mesh */
#ifdef DEBUG
#  define MESH_OMP_LIMIT 0
#else
#  define MESH_OMP_LIMIT 10000
#endif
