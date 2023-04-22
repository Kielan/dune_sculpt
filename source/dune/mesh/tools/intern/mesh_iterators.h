#pragma once

/**
 * Mesh Iterators
 *
 * The functions and structures in this file
 * provide a unified method for iterating over
 * the elements of a mesh and answering simple
 * adjacency queries. Tool authors should use
 * the iterators provided in this file instead
 * of inspecting the structure directly.
 */

#include "lib_compiler_attrs.h"
#include "lib_mempool.h"

/* these iterator over all elements of a specific
 * type in the mesh.
 *
 * be sure to keep 'mesh_iter_itype_htype_map' in sync with any changes
 */
typedef enum MeshIterType {
  MESH_VERTS_OF_MESH = 1,
  MESH_EDGES_OF_MESH = 2,
  MESH_FACES_OF_MESH = 3,
  /* these are topological iterators. */
  MESH_EDGES_OF_VERT = 4,
  MESH_FACES_OF_VERT = 5,
  MESH_LOOPS_OF_VERT = 6,
  MESH_VERTS_OF_EDGE = 7, /* just v1, v2: added so py can use generalized sequencer wrapper */
  MESH_FACES_OF_EDGE = 8,
  MESH_VERTS_OF_FACE = 9,
  MESH_EDGES_OF_FACE = 10,
  MESH_LOOPS_OF_FACE = 11,
  /* returns elements from all boundaries, and returns
   * the first element at the end to flag that we're entering
   * a different face hole boundary. */
  // MESH_ALL_LOOPS_OF_FACE = 12,
  /* iterate through loops around this loop, which are fetched
   * from the other faces in the radial cycle surrounding the
   * input loop's edge. */
  MESH_LOOPS_OF_LOOP = 12,
  MESH_LOOPS_OF_EDGE = 13,
} MeshIterType;

#define MESH_ITYPE_MAX 14

/* the iterator htype for each iterator */
extern const char mesh_iter_itype_htype_map[MESH_ITYPE_MAX];

/* -------------------------------------------------------------------- */
/** Defines for passing to mesh_iter_new.
 *
 * "OF" can be substituted for "around" so MESH_VERTS_OF_FACE means "vertices* around a face."
 **/

#define MESH_ITER(ele, iter, mesh, itype) \
  for (MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_new(iter, mesh, itype, NULL); ele; \
       MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_step(iter))

#define MESH_ITER_MESH_INDEX(ele, iter, mesh, itype, indexvar) \
  for (MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_new(iter, mesh, itype, NULL), indexvar = 0; ele; \
       MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_step(iter), (indexvar)++)

/* a version of MESH_ITER which keeps the next item in storage
 * so we can delete the current item, see bug T36923. */
#ifdef DEBUG
#  define MESH_ITER_MUTABLE(ele, ele_next, iter, bm, itype) \
    for (MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_new(iter, mesh, itype, NULL); \
         ele ? ((void)((iter)->count = mesh_iter_mesh_count(itype, mesh)), \
                (void)(MESH_CHECK_TYPE_ELEM_ASSIGN(ele_next) = mesh_iter_step(iter)), \
                1) : \
               0; \
         MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = ele_next)
#else
#  define MESH_ITER_MUTABLE(ele, ele_next, iter, mesh, itype) \
    for (MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_new(iter, mesh, itype, NULL); \
         ele ? ((MESH_CHECK_TYPE_ELEM_ASSIGN(ele_next) = mesh_iter_step(iter)), 1) : 0; \
         ele = ele_next)
#endif

#define MESH_ITER_ELEM(ele, iter, data, itype) \
  for (MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_new(iter, NULL, itype, data); ele; \
       MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_step(iter))

#define MESH SH_ITER_ELEM_INDEX(ele, iter, data, itype, indexvar) \
  for (MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_new(iter, NULL, itype, data), indexvar = 0; ele; \
       MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = mesh_iter_step(iter), (indexvar)++)

/* iterator type structs */
struct MeshIter__elem_of_mesh {
  lib_mempool_iter pooliter;
};
struct MeshIter__edge_of_vert {
  MeshVert *vdata;
  MeshEdge *e_first, *e_next;
};
struct MeshIter__face_of_vert {
  MeshVert *vdata;
  MeshLoop *l_first, *l_next;
  MeshEdge *e_first, *e_next;
};
struct MeshIter__loop_of_vert {
  MVert *vdata;
  MLoop *l_first, *l_next;
  MEdge *e_first, *e_next;
};
struct MeshIter__loop_of_edge {
  MeshEdge *edata;
  MeshLoop *l_first, *l_next;
};
struct MeshIter__loop_of_loop {
  MeshLoop *ldata;
  MeshLoop *l_first, *l_next;
};
struct MeshIter__face_of_edge {
  MeshEdge *edata;
  MeshLoop *l_first, *l_next;
};
struct MeshIter__vert_of_edge {
  MeshEdge *edata;
};
struct MeshIter__vert_of_face {
  MeshFace *pdata;
  MeshLoop *l_first, *l_next;
};
struct MeshIter__edge_of_face {
  MeshFace *pdata;
  MeshLoop *l_first, *l_next;
};
struct MeshIter__loop_of_face {
  MeshFace *pdata;
  MeshLoop *l_first, *l_next;
};

typedef void (*MeshIter__begin_cb)(void *);
typedef void *(*Meshter__step_cb)(void *);

/* Iterator Structure */
/* NOTE: some of these vars are not used,
 * so they have been commented to save stack space since this struct is used all over */
typedef struct MeshIter {
  /* keep union first */
  union {
    struct MeshIter__elem_of_mesh elem_of_mesh;

    struct MeshIter__edge_of_vert edge_of_vert;
    struct MeshIter__face_of_vert face_of_vert;
    struct MeshIter__loop_of_vert loop_of_vert;
    struct MeshIter__loop_of_edge loop_of_edge;
    struct MeshIter__loop_of_loop loop_of_loop;
    struct MeshIter__face_of_edge face_of_edge;
    struct MeshIter__vert_of_edge vert_of_edge;
    struct MeshIter__vert_of_face vert_of_face;
    struct MeshIter__edge_of_face edge_of_face;
    struct MeshIter__loop_of_face loop_of_face;
  } data;

  MeshIter__begin_cb begin;
  MeshIter__step_cb step;

  int count; /* NOTE: only some iterators set this, don't rely on it. */
  char itype;
} MeshIter;

/** Use mesh_vert_at_index / mesh_edge_at_index / mesh_face_at_index for mesh arrays. **/
void *mesh_iter_at_index(Mesh *mesh, char itype, void *data, int index) ATTR_WARN_UNUSED_RESULT;
/**
 * Iterator as Array
 *
 * Sometimes its convenient to get the iterator as an array
 * to avoid multiple calls to mesh_iter_at_index.
 */
int mesh_iter_as_array(Mesh *mesh, char itype, void *data, void **array, int len);
/**
 * Iterator as Array
 *
 * Allocates a new array, has the advantage that you don't need to know the size ahead of time.
 *
 * Takes advantage of less common iterator usage to avoid counting twice,
 * which you might end up doing when mesh_iter_as_array is used.
 *
 * Caller needs to free the array.
 */
void *mesh_iter_as_arrayn(Mesh *mesh,
                          char itype,
                          void *data,
                          int *r_len,
                          void **stack_array,
                          int stack_array_size) ATTR_WARN_UNUSED_RESULT;
/**
 * Operator Iterator as Array
 *
 * Sometimes its convenient to get the iterator as an array.
 */
int mesh_op_iter_as_array(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                         const char *slot_name,
                         char restrictmask,
                         void **array,
                         int len);
void *mesh_op_iter_as_arrayN(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                         const char *slot_name,
                         char restrictmask,
                         int *r_len,
                         /* optional args to avoid an alloc (normally stack array) */
                         void **stack_array,
                         int stack_array_size);

int mesh_iter_mesh_bitmap_from_filter(char itype,
                                      Mesh *mesh,
                                      uint *bitmap,
                                      bool (*test_fn)(MeshElem *, void *user_data),
                                      void *user_data);
/** Needed when we want to check faces, but return a loop aligned array. */
int mesh_iter_mesh_bitmap_from_filter_tessface(Mesh *mesh,
                                               uint *bitmap,
                                               bool (*test_fn)(MeshFace *, void *user_data),
                                               void *user_data);

/**
 * Elem Iter Flag Count
 *
 * Counts how many flagged / unflagged items are found in this element.
 */
int mesh_iter_elem_count_flag(char itype, void *data, char hflag, bool value);
/**
 * Elem Iter Tool Flag Count
 *
 * Counts how many flagged / unflagged items are found in this element.
 */
int mesh_iter_elem_count_flag(Mesh *mesh, char itype, void *data, short opflag, bool value);
/** Utility function. */
int mesh_iter_mesh_count(char itype, Mesh *mesh);
/**
 * Mesh Iter Flag Count
 *
 * Counts how many flagged / unflagged items are found in this mesh.
 */
int mesh_iter_mesh_count_flag(char itype, Mesh *mesh, char hflag, bool value);

/* private for bmesh_iterators_inline.c */

#define MESH_ITER_CB_DEF(name) \
  struct MeshIter__##name; \
  void bmiter__##name##_begin(struct MeshIter__##name *iter); \
  void *bmiter__##name##_step(struct MeshIter__##name *iter)

MESH_ITER_CB_DEF(elem_of_mesh);
MESH_ITER_CB_DEF(edge_of_vert);
MESH_ITER_CB_DEF(face_of_vert);
MESH_ITER_CB_DEF(loop_of_vert);
MESH_ITER_CB_DEF(loop_of_edge);
MESH_ITER_CB_DEF(loop_of_loop);
MESH_ITER_CB_DEF(face_of_edge);
MESH_ITER_CB_DEF(vert_of_edge);
MESH_ITER_CB_DEF(vert_of_face);
MESH_ITER_CB_DEF(edge_of_face);
MESH_ITER_CB_DEF(loop_of_face);

#undef MESH_ITER_CB_DEF

#include "intern/mesh_iterators_inline.h"

#define MESH_ITER_CHECK_TYPE_DATA(data) \
  CHECK_TYPE_ANY(data, void *, MeshFace *, MeshEdge *, MeshVert *, MeshLoop *, MeshElem *)

#define mesh_iter_new(iter, mesh, itype, data) \
  (MESH_ITER_CHECK_TYPE_DATA(data), mesh_iter_new(iter, mesh, itype, data))
#define mesh_iter_init(iter, mesh, itype, data) \
  (MESH_ITER_CHECK_TYPE_DATA(data), mesh_iter_init(iter, mesh, itype, data))
