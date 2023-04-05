#pragma once

/**
 * The lowest level of functionality for manipulating mesh structures.
 * None of these functions should ever be exported to the rest of Dune.
 *
 * in the vast majority of cases there shouldn't be used directly.
 * if absolutely necessary, see function definitions in code for
 * descriptive comments. Don't import.
 */

/* LOOP CYCLE MANAGEMENT */
/** loop cycle functions, loops surrounding a face **/
bool mesh_loop_validate(MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/* DISK CYCLE MANAGEMENT */
void mesh_disk_edge_append(MeshEdge *e, MeshVert *v) ATTR_NONNULL();
void mesh_disk_edge_remove(MeshEdge *e, MeshVert *v) ATTR_NONNULL();
LIB_INLINE MeshEdge *mesh_disk_edge_next_safe(const MeshEdge *e,
                                              const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
LIB_INLINE MeshEdge *mesh_disk_edge_prev_safe(const MeshEdge *e,
                                             const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
LIB_INLINE MeshEdge *mesh_disk_edge_next(const MeshEdge *e, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
LIB_INLINE MeshEdge *mesh_disk_edge_prev(const MeshEdge *e, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
int mesh_disk_facevert_count_at_most(const MeshVert *v, int count_max) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * brief DISK COUNT FACE VERT
 *
 * Counts the number of loop users
 * for this vertex. Note that this is
 * equivalent to counting the number of
 * faces incident upon this vertex
 */
int mesh_disk_facevert_count(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * brief FIND FIRST FACE EDGE
 *
 * Finds the first edge in a vertices
 * Disk cycle that has one of this
 * vert's loops attached
 * to it.
 */
MeshEdge *mesh_disk_faceedge_find_first(const MeshEdge *e, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * Special case for MESH_LOOPS_OF_VERT & MESH_FACES_OF_VERT, avoids 2x calls.
 *
 * The returned MeshLoop.e matches the result of mesh_disk_faceedge_find_first
 */
MeshLoop *mesh_disk_faceloop_find_first(const MeshEdge *e, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * A version of mesh_disk_faceloop_find_first that ignores hidden faces.
 */
MeshLoop *mesh_disk_faceloop_find_first_visible(const MeshEdge *e,
                                               const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
MeshEdge *mesh_disk_faceedge_find_next(const MeshEdge *e, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

/* RADIAL CYCLE MANAGEMENT */
void mesh_radial_loop_append(MeshEdge *e, MeshLoop *l) ATTR_NONNULL();
/**
 * brief MESH RADIAL REMOVE LOOP
 *
 * Removes a loop from an radial cycle. If edge e is non-NULL
 * it should contain the radial cycle, and it will also get
 * updated (in the case that the edge's link into the radial
 * cycle was the loop which is being removed from the cycle).
 */
void mesh_radial_loop_remove(MeshEdge *e, MeshLoop *l) ATTR_NONNULL();
/**
 * A version of mesh_radial_loop_remove which only performs the radial unlink,
 * leaving the edge untouched.
 */
void mesh_radial_loop_unlink(MeshLoop *l) ATTR_NONNULL();
/* NOTE:
 *      mesh_radial_loop_next(MeshLoop *l) / prev.
 * just use member access l->radial_next, l->radial_prev now */

int bmesh_radial_facevert_count_at_most(const BMLoop *l,
                                        const BMVert *v,
                                        int count_max) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * \brief RADIAL COUNT FACE VERT
 *
 * Returns the number of times a vertex appears
 * in a radial cycle
 */
int bmesh_radial_facevert_count(const BMLoop *l, const BMVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * \brief RADIAL CHECK FACE VERT
 *
 * Quicker check for `bmesh_radial_facevert_count(...) != 0`.
 */
bool bmesh_radial_facevert_check(const BMLoop *l, const BMVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * \brief BME RADIAL FIND FIRST FACE VERT
 *
 * Finds the first loop of v around radial
 * cycle
 */
MeshLoop *bmesh_radial_faceloop_find_first(const MeshLoop *l, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
MeshLoop *bmesh_radial_faceloop_find_next(const MeshLoop *l, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
MeshLoop *bmesh_radial_faceloop_find_vert(const MeshFace *f, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/*****radial cycle functions, e.g. loops surrounding edges**** */
bool mesh_radial_validate(int radlen, MeshLoop *l) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/* EDGE UTILITIES */
void mesh_disk_vert_swap(MeshEdge *e, MeshVert *v_dst, BMVert *v_src) ATTR_NONNULL();
/**
 * Handles all connected data, use with care.
 *
 * Assumes caller has setup correct state before the swap is done.
 */
void mesh_edge_vert_swap(MeshEdge *e, MeshVert *v_dst, MeshVert *v_src) ATTR_NONNULL();
void mesh_disk_vert_replace(MeshEdge *e, MeshVert *v_dst, MeshVert *v_src) ATTR_NONNULL();
MeshEdge *mesh_disk_edge_exists(const MeshVert *v1, const MeshVert *v2) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
bool mesh_disk_validate(int len, MeshEdge *e, MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

#include "intern/mesh_structure_inline.h"
