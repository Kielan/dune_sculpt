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
  M_FACES_OF_VERT = 5,
  M_LOOPS_OF_VERT = 6,
  M_VERTS_OF_EDGE = 7, /* just v1, v2: added so py can use generalized sequencer wrapper */
  M_FACES_OF_EDGE = 8,
  M_VERTS_OF_FACE = 9,
  M_EDGES_OF_FACE = 10,
  M_LOOPS_OF_FACE = 11,
  /* returns elements from all boundaries, and returns
   * the first element at the end to flag that we're entering
   * a different face hole boundary. */
  // BM_ALL_LOOPS_OF_FACE = 12,
  /* iterate through loops around this loop, which are fetched
   * from the other faces in the radial cycle surrounding the
   * input loop's edge. */
  M_LOOPS_OF_LOOP = 12,
  M_LOOPS_OF_EDGE = 13,
} MeshIterType;

#define MESH_ITYPE_MAX 14

/* the iterator htype for each iterator */
extern const char mesh_iter_itype_htype_map[MESH_ITYPE_MAX];

/* -------------------------------------------------------------------- */
/** Defines for passing to mesh_iter_new.
 *
 * "OF" can be substituted for "around" so MESH_VERTS_OF_FACE means "vertices* around a face."
 **/

#define MESH_ITER_MESH(ele, iter, mesh, itype) \
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
struct MIter__loop_of_edge {
  MEdge *edata;
  MLoop *l_first, *l_next;
};
struct BMIter__loop_of_loop {
  MLoop *ldata;
  MLoop *l_first, *l_next;
};
struct MIter__face_of_edge {
  MEdge *edata;
  MLoop *l_first, *l_next;
};
struct MIter__vert_of_edge {
  MEdge *edata;
};
struct MIter__vert_of_face {
  MFace *pdata;
  MLoop *l_first, *l_next;
};
struct MIter__edge_of_face {
  MFace *pdata;
  MLoop *l_first, *l_next;
};
struct MIter__loop_of_face {
  MFace *pdata;
  MLoop *l_first, *l_next;
};

typedef void (*BMIter__begin_cb)(void *);
typedef void *(*BMIter__step_cb)(void *);

/* Iterator Structure */
/* NOTE: some of these vars are not used,
 * so they have been commented to save stack space since this struct is used all over */
typedef struct BMIter {
  /* keep union first */
  union {
    struct BMIter__elem_of_mesh elem_of_mesh;

    struct BMIter__edge_of_vert edge_of_vert;
    struct BMIter__face_of_vert face_of_vert;
    struct BMIter__loop_of_vert loop_of_vert;
    struct BMIter__loop_of_edge loop_of_edge;
    struct BMIter__loop_of_loop loop_of_loop;
    struct BMIter__face_of_edge face_of_edge;
    struct BMIter__vert_of_edge vert_of_edge;
    struct BMIter__vert_of_face vert_of_face;
    struct BMIter__edge_of_face edge_of_face;
    struct BMIter__loop_of_face loop_of_face;
  } data;

  BMIter__begin_cb begin;
  BMIter__step_cb step;

  int count; /* NOTE: only some iterators set this, don't rely on it. */
  char itype;
} BMIter;

/**
 * \note Use #BM_vert_at_index / #BM_edge_at_index / #BM_face_at_index for mesh arrays.
 */
void *BM_iter_at_index(BMesh *bm, char itype, void *data, int index) ATTR_WARN_UNUSED_RESULT;
/**
 * \brief Iterator as Array
 *
 * Sometimes its convenient to get the iterator as an array
 * to avoid multiple calls to #BM_iter_at_index.
 */
int BM_iter_as_array(BMesh *bm, char itype, void *data, void **array, int len);
/**
 * \brief Iterator as Array
 *
 * Allocates a new array, has the advantage that you don't need to know the size ahead of time.
 *
 * Takes advantage of less common iterator usage to avoid counting twice,
 * which you might end up doing when #BM_iter_as_array is used.
 *
 * Caller needs to free the array.
 */
void *BM_iter_as_arrayN(BMesh *bm,
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
int mesh_iter_mesh_bitmap_from_filter_tessface(BMesh *bm,
                                             uint *bitmap,
                                             bool (*test_fn)(BMFace *, void *user_data),
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
int mesh_iter_elem_count_flag(BMesh *bm, char itype, void *data, short oflag, bool value);
/**
 * Utility function.
 */
int mesh_iter_mesh_count(char itype, BMesh *bm);
/**
 * Mesh Iter Flag Count
 *
 * Counts how many flagged / unflagged items are found in this mesh.
 */
int mesh_iter_mesh_count_flag(char itype, BMesh *bm, char hflag, bool value);

/* private for bmesh_iterators_inline.c */

#define BMITER_CB_DEF(name) \
  struct BMIter__##name; \
  void bmiter__##name##_begin(struct BMIter__##name *iter); \
  void *bmiter__##name##_step(struct BMIter__##name *iter)

BMITER_CB_DEF(elem_of_mesh);
BMITER_CB_DEF(edge_of_vert);
BMITER_CB_DEF(face_of_vert);
BMITER_CB_DEF(loop_of_vert);
BMITER_CB_DEF(loop_of_edge);
BMITER_CB_DEF(loop_of_loop);
BMITER_CB_DEF(face_of_edge);
BMITER_CB_DEF(vert_of_edge);
BMITER_CB_DEF(vert_of_face);
BMITER_CB_DEF(edge_of_face);
BMITER_CB_DEF(loop_of_face);

#undef BMITER_CB_DEF

#include "intern/bmesh_iterators_inline.h"

#define BM_ITER_CHECK_TYPE_DATA(data) \
  CHECK_TYPE_ANY(data, void *, BMFace *, BMEdge *, BMVert *, BMLoop *, BMElem *)

#define BM_iter_new(iter, bm, itype, data) \
  (BM_ITER_CHECK_TYPE_DATA(data), BM_iter_new(iter, bm, itype, data))
#define BM_iter_init(iter, bm, itype, data) \
  (BM_ITER_CHECK_TYPE_DATA(data), BM_iter_init(iter, bm, itype, data))
