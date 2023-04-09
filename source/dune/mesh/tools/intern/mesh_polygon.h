#pragma once

struct MeshPartialUpdate;
struct Heap;

#include "lib_compiler_attrs.h"

/**
 * For tools that insist on using triangles, ideally we would cache this data.
 *
 * param use_fixed_quad: When true,
 * always split quad along (0 -> 2) regardless of concave corners,
 * (as done in mesh_calc_tessellation).
 * param r_loops: Store face loop pointers, (f->len)
 * param r_index: Store triangle triples, indices into \a r_loops,  `((f->len - 2) * 3)`
 */
void mesh_face_calc_tessellation(const MeshFace *f,
                               bool use_fixed_quad,
                               MeshLoop **r_loops,
                               uint (*r_index)[3]);
/** Return a point inside the face. **/
void mesh_face_calc_point_in_face(const MeshFace *f, float r_co[3]);

/**
 * MESH UPDATE FACE NORMAL
 *
 * Updates the stored normal for the
 * given face. Requires that a buffer
 * of sufficient length to store projected
 * coordinates for all of the face's vertices
 * is passed in as well.
 */
float mesh_face_calc_normal(const MeshFace *f, float r_no[3]) ATTR_NONNULL();
/* exact same as 'mesh_face_calc_normal' but accepts vertex coords */
float mesh_face_calc_normal_vcos(const Mesh *mesh,
                               const MeshFace *f,
                               float r_no[3],
                               float const (*vertexCos)[3]) ATTR_NONNULL();

/**
 * Calculate a normal from a vertex cloud.
 *
 * We could make a higher quality version that takes all vertices into account.
 * Currently it finds 4 outer most points returning its normal.
 */
void mesh_verts_calc_normal_from_cloud_ex(
    MeshVert **varr, int varr_len, float r_normal[3], float r_center[3], int *r_index_tangent);
void mesh_verts_calc_normal_from_cloud(MeshVert **varr, int varr_len, float r_normal[3]);

/** Calculates the face subset normal. **/
float mesh_face_calc_normal_subset(const MeshLoop *l_first, const MeshLoop *l_last, float r_no[3])
    ATTR_NONNULL();
/** get the area of the face **/
float mesh_face_calc_area(const MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/** Get the area of the face in world space. **/
float mesh_face_calc_area_with_mat3(const MeshFace *f, const float mat3[3][3]) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/** get the area of UV face */
float mesh_face_calc_area_uv(const MeshFace *f, int cd_loop_uv_offset) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/** compute the perimeter of an ngon */
float mesh_face_calc_perimeter(const MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/** Calculate the perimeter of a ngon in world space. **/
float mesh_face_calc_perimeter_with_mat3(const MeshFace *f,
                                       const float mat3[3][3]) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/** Compute the tangent of the face, using the longest edge. **/
void mesh_face_calc_tangent_edge(const MeshFace *f, float r_tangent[3]) ATTR_NONNULL();
/**
 * Compute the tangent of the face, using the two longest disconnected edges.
 *
 * param r_tangent: Calculated unit length tangent (return value).
 */
void mesh_face_calc_tangent_edge_pair(const BMFace *f, float r_tangent[3]) ATTR_NONNULL();
/**
 * Compute the tangent of the face, using the edge farthest away from any vertex in the face.
 *
 * param r_tangent: Calculated unit length tangent (return value).
 */
void mesh_face_calc_tangent_edge_diagonal(const MeshFace *f, float r_tangent[3]) ATTR_NONNULL();
/**
 * Compute the tangent of the face, using longest distance between vertices on the face.
 *
 * note The logic is almost identical to mesh_face_calc_tangent_edge_diagonal
 */
void mesh_face_calc_tangent_vert_diagonal(const MeshFace *f, float r_tangent[3]) ATTR_NONNULL();
/**
 * Compute a meaningful direction along the face (use for gizmo axis).
 *
 * note Callers shouldn't depend on the *exact* method used here.
 */
void mesh_face_calc_tangent_auto(const MeshFace *f, float r_tangent[3]) ATTR_NONNULL();
/** computes center of face in 3d.  uses center of bounding box. **/
void mesh_face_calc_center_bounds(const MeshFace *f, float r_cent[3]) ATTR_NONNULL();
/** computes center of face in 3d.  uses center of bounding box. */
void mesh_face_calc_center_bounds_vcos(const Mesh *mesh,
                                     const MeshFace *f,
                                     float r_center[3],
                                     float const (*vertexCos)[3]) ATTR_NONNULL();
/** computes the center of a face, using the mean average **/
void mesh_face_calc_center_median(const MeshFace *f, float r_center[3]) ATTR_NONNULL();
/* exact same as 'mesh_face_calc_normal' but accepts vertex coords */
void mesh_face_calc_center_median_vcos(const Mesh *mesh,
                                     const MeshFace *f,
                                     float r_center[3],
                                     float const (*vertexCos)[3]) ATTR_NONNULL();
/** computes the center of a face,
 * using the mean average
 * weighted by edge length **/
void mesh_face_calc_center_median_weighted(const MeshFace *f, float r_cent[3]) ATTR_NONNULL();

/** expands bounds (min/max must be initialized). **/
void mesh_face_calc_bounds_expand(const MeshFace *f, float min[3], float max[3]);

void mesh_face_normal_update(MeshFace *f) ATTR_NONNULL();

/** updates face and vertex normals incident on an edge **/
void mesh_edge_normals_update(MeshEdge *e) ATTR_NONNULL();

bool mesh_vert_calc_normal_ex(const MeshVert *v, char hflag, float r_no[3]);
bool mesh_vert_calc_normal(const MeshVert *v, float r_no[3]);
/** update a vert normal (but not the faces incident on it) **/
void mesh_vert_normal_update(MeshVert *v) ATTR_NONNULL();
void mesh_vert_normal_update_all(MeshVert *v) ATTR_NONNULL();

/**
 * Face Flip Normal
 *
 * Reverses the winding of a face.
 * This updates the calculated normal.
 */
void mesh_face_normal_flip_ex(Mesh *mesh,
                              MeshFace *f,
                              int cd_loop_mdisp_offset,
                              bool use_loop_mdisp_flip) ATTR_NONNULL();
void mesh_face_normal_flip(Mesh *mesh, MeshFace *f) ATTR_NONNULL();
/**
 * MESH POINT IN FACE
 *
 * Projects co onto face f, and returns true if it is inside
 * the face bounds.
 *
 * note this uses a best-axis projection test,
 * instead of projecting co directly into f's orientation space,
 * so there might be accuracy issues.
 */
bool mesh_face_point_inside_test(const BMFace *f, const float co[3]) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

/**
 * MESH TRIANGULATE FACE
 *
 * Breaks all quads and ngons down to triangles.
 * It uses poly-fill for the ngons splitting, and
 * the beautify operator when use_beauty is true.
 *
 * param r_faces_new: if non-null, must be an array of MeshFace pointers,
 * with a length equal to (f->len - 3). It will be filled with the new
 * triangles (not including the original triangle).
 *
 * param r_faces_double: When newly created faces are duplicates of existing faces,
 * they're added to this list. Caller must handle de-duplication.
 * This is done because its possible _all_ faces exist already,
 * and in that case we would have to remove all faces including the one passed,
 * which causes complications adding/removing faces while looking over them.
 *
 * note The number of faces is _almost_ always (f->len - 3),
 *       However there may be faces that already occupying the
 *       triangles we would make, so the caller must check \a r_faces_new_tot.
 *
 * note use_tag tags new flags and edges.
 */
void mesh_face_triangulate(Mesh *mesh,
                           MeshFace *f,
                           MeshFace **r_faces_new,
                           int *r_faces_new_tot,
                           MeshEdge **r_edges_new,
                           int *r_edges_new_tot,
                           struct LinkNode **r_faces_double,
                           int quad_method,
                           int ngon_method,
                           bool use_tag,
                           struct MemArena *pf_arena,
                           struct Heap *pf_heap) ATTR_NONNULL(1, 2);

/**
 * each pair of loops defines a new edge, a split.  this function goes
 * through and sets pairs that are geometrically invalid to null.  a
 * split is invalid, if it forms a concave angle or it intersects other
 * edges in the face, or it intersects another split.  in the case of
 * intersecting splits, only the first of the set of intersecting
 * splits survives
 */
void BM_face_splits_check_legal(BMesh *bm, BMFace *f, BMLoop *(*loops)[2], int len) ATTR_NONNULL();
/**
 * This simply checks that the verts don't connect faces which would have more optimal splits.
 * but _not_ check for correctness.
 */
void BM_face_splits_check_optimal(BMFace *f, BMLoop *(*loops)[2], int len) ATTR_NONNULL();

/**
 * Small utility functions for fast access
 *
 * faster alternative to:
 * BM_iter_as_array(bm, BM_VERTS_OF_FACE, f, (void **)v, 3);
 */
void BM_face_as_array_vert_tri(BMFace *f, BMVert *r_verts[3]) ATTR_NONNULL();
/**
 * faster alternative to:
 * BM_iter_as_array(bm, BM_VERTS_OF_FACE, f, (void **)v, 4);
 */
void BM_face_as_array_vert_quad(BMFace *f, BMVert *r_verts[4]) ATTR_NONNULL();

/**
 * Small utility functions for fast access
 *
 * faster alternative to:
 * BM_iter_as_array(bm, BM_LOOPS_OF_FACE, f, (void **)l, 3);
 */
void BM_face_as_array_loop_tri(BMFace *f, BMLoop *r_loops[3]) ATTR_NONNULL();
/**
 * faster alternative to:
 * BM_iter_as_array(bm, BM_LOOPS_OF_FACE, f, (void **)l, 4);
 */
void BM_face_as_array_loop_quad(BMFace *f, BMLoop *r_loops[4]) ATTR_NONNULL();

/**
 * Calculate a tangent from any 3 vertices.
 *
 * The tangent aligns to the most *unique* edge
 * (the edge most unlike the other two).
 *
 * \param r_tangent: Calculated unit length tangent (return value).
 */
void BM_vert_tri_calc_tangent_edge(BMVert *verts[3], float r_tangent[3]);
/**
 * Calculate a tangent from any 3 vertices,
 *
 * The tangent follows the center-line formed by the most unique edges center
 * and the opposite vertex.
 *
 * \param r_tangent: Calculated unit length tangent (return value).
 */
void BM_vert_tri_calc_tangent_edge_pair(BMVert *verts[3], float r_tangent[3]);
