#pragma once

#include "mesh_class.h"

struct MeshAllocTemplate;
struct MeshLoopNorEditDataArray;
struct MeshPartialUpdate;
struct MeshLoopNorSpaceArray;

void mesh_elem_toolflags_ensure(Mesh *m);
void mesh_elem_toolflags_clear(Mesh *m);

struct MeshCreateParams {
  bool use_toolflags : true;
};

/**
 * Mesh Make Allocates new Mesh structure.
 *
 * return The New mesh
 *
 * ob is needed by multires
 */
Mesh *mesh_create(const struct MeshAllocTemplate *allocsize,
                  const struct MeshCreateParams *params);

/**
 * Mesh Free Mesh
 *
 * Frees a Mesh data and its structure.
 */
void mesh_free(Mesh *mesh);
/**
 * Mesh Free Mesh Data
 *
 * Frees a Mesh structure.
 *
 * frees mesh, but not actual Mesh struct
 */
void mesh_data_free(Mesh *mesh);
/**
 * Mesh Clear Mesh
 *
 * Clear all data in mesh
 */
void mesh_clear(Mesh *mesh);

/**
 * Mesh Begin Edit
 *
 * Functions for setting up a mesh for editing and cleaning up after
 * the editing operations are done. These are called by the tools/operator
 * API for each time a tool is executed.
 */
void mesh_edit_begin(Mesh *mesh, MeshOpTypeFlag type_flag);
/**
 * Mesh End Edit
 */
void mesh_edit_end(Mesh *mesh, MeshOpTypeFlag type_flag);

void mesh_elem_index_ensure_ex(Mesh *mesh, char htype, int elem_offset[4]);
void mesh_elem_index_ensure(Mesh *mesh, char htype);
/**
 * Array checking/setting macros.
 *
 * Currently vert/edge/loop/face index data is being abused, in a few areas of the code.
 *
 * To avoid correcting them afterwards, set 'mesh->elem_index_dirty' however its possible
 * this flag is set incorrectly which could crash blender.
 *
 * Functions that calls this function may depend on dirty indices on being set.
 *
 * This is read-only, so it can be used for assertions that don't impact behavior.
 */
void mesh_elem_index_validate(
    Mesh *mesh, const char *location, const char *fn, const char *msg_a, const char *msg_b);

#ifndef NDEBUG
/** mesh_elem_index_validate the same rationale applies to this function. */
bool mesh_elem_table_check(Mesh *mesh);
#endif

/** Re-allocates mesh data with/without toolflags. */
void mesh_toolflags_set(Mesh *mesh, bool use_toolflags);

void mesh_elem_table_ensure(Mesh *mesh, char htype);
/* use mesh_elem_table_ensure where possible to avoid full rebuild */
void mesh_elem_table_init(Mesh *mesh, char htype);
void mesh_elem_table_free(Mesh *mesh, char htype);

LIB_INLINE MeshVert *mesh_vert_at_index(Mesh *mesh, const int index)
{
  lib_assert((index >= 0) && (index < mesh->totvert));
  lib_assert((bm->elem_table_dirty & MESH_VERT) == 0);
  return mesh->vtable[index];
}
LIB_INLINE MeshEdge *mesh_edge_at_index(Mesh *mesh, const int index)
{
  lib_assert((index >= 0) && (index < mesh->totedge));
  lib_assert((mesh->elem_table_dirty & MESH_EDGE) == 0);
  return mesh->etable[index];
}
LIB_INLINE MeshFace *mesh_face_at_index(Mesh *mesh, const int index)
{
  lib_assert((index >= 0) && (index < mesh->totface));
  lib_assert((mesh->elem_table_dirty & MESH_FACE) == 0);
  return mesh->ftable[index];
}

MeshVert *mesh_vert_at_index_find(Mesh *mesh, int index);
MeshEdge *mesh_edge_at_index_find(Mesh *mesh, int index);
MeshFace *mesh_face_at_index_find(Mesh *mesh, int index);
MeshLoop *mesh_loop_at_index_find(Mesh *mesh, int index);

/**
 * Use lookup table when available, else use slower find functions.
 *
 * Try to use mesh_elem_table_ensure instead.
 */
MeshVert *mesh_vert_at_index_find_or_table(Mesh *mesh, int index);
MeshEdge *mesh_edge_at_index_find_or_table(Mesh *mesh, int index);
MeshFace *mesh_face_at_index_find_or_table(Mesh *mesh, int index);

// XXX

/** Return the amount of element of type 'type' in a given mesh. */
int mesh_elem_count(Mesh *mesh, char htype);

/**
 * Remaps the vertices, edges and/or faces of the mesh as indicated by vert/edge/face_idx arrays
 * (xxx_idx[org_index] = new_index).
 *
 * A NULL array means no changes.
 *
 * note
 * - Does not mess with indices, just sets elem_index_dirty flag.
 * - For verts/edges/faces only (as loops must remain "ordered" and "aligned"
 *   on a per-face basis...).
 *
 * warning Be careful if you keep pointers to affected Mesh elements,
 * or arrays, when using this func!
 */
void mesh_remap(Mesh *mesh, const uint *vert_idx, const uint *edge_idx, const uint *face_idx);

/**
 * Use new memory pools for this mesh.
 *
 * note needed for re-sizing elements (adding/removing tool flags)
 * but could also be used for packing fragmented bmeshes.
 */
void mesh_rebuild(Mesh *mesh,
                  const struct MeshCreateParams *params,
                  struct lib_mempool *vpool,
                  struct lib_mempool *epool,
                  struct lib_mempool *lpool,
                  struct lib_mempool *fpool);

typedef struct MeshAllocTemplate {
  int totvert, totedge, totloop, totface;
} MeshAllocTemplate;

/* used as an extern, defined in mesh.h */
extern const MeshAllocTemplate mesh_allocsize_default;
extern const MeshAllocTemplate mesh_chunksize_default;

#define MESHALLOC_TEMPLATE_FROM_MESH(mesh) \
  { \
    (CHECK_TYPE_INLINE(mesh, Mesh *), (mesh)->totvert), (mesh)->totedge, (mesh)->totloop, (mesh)->totface \
  }

#define _VA_MESHALLOC_TEMPLATE_FROM_ME_1(me) \
  { \
    (CHECK_TYPE_INLINE(me, Mesh *), (me)->totvert), (me)->totedge, (me)->totloop, (me)->totpoly, \
  }
#define _VA_BMALLOC_TEMPLATE_FROM_ME_2(me_a, me_b) \
  { \
    (CHECK_TYPE_INLINE(me_a, Mesh *), \
     CHECK_TYPE_INLINE(me_b, Mesh *), \
     (me_a)->totvert + (me_b)->totvert), \
        (me_a)->totedge + (me_b)->totedge, (me_a)->totloop + (me_b)->totloop, \
        (me_a)->totpoly + (me_b)->totpoly, \
  }
#define MESHALLOC_TEMPLATE_FROM_ME(...) \
  VA_NARGS_CALL_OVERLOAD(_VA_MESHALLOC_TEMPLATE_FROM_ME_, __VA_ARGS__)

/* Vertex coords access. */
void BM_mesh_vert_coords_get(BMesh *bm, float (*vert_coords)[3]);
float (*BM_mesh_vert_coords_alloc(BMesh *bm, int *r_vert_len))[3];
void BM_mesh_vert_coords_apply(BMesh *bm, const float (*vert_coords)[3]);
void BM_mesh_vert_coords_apply_with_mat4(BMesh *bm,
                                         const float (*vert_coords)[3],
                                         const float mat[4][4]);
