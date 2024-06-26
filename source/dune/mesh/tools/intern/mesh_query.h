#pragma once

/** Returns true if the vertex is used in a given face. */
bool mesh_vert_in_face(MeshVert *v, MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Compares the number of vertices in an array
 * that appear in a given face
 */
int mesh_verts_in_face_count(MeshVert **varr, int len, MeshFace *f) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * Return true if all verts are in the face.
 */
bool mesh_verts_in_face(MeshVert **varr, int len, MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * Returns whether or not a given edge is part of a given face.
 */
bool mesh_edge_in_face(const MeshEdge *e, const MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
LIB_INLINE bool mesh_edge_in_loop(const MeshEdge *e, const MeshLoop *l) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

LIB_INLINE bool mesh_vert_in_edge(const MeshEdge *e, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
LIB_INLINE bool mesh_verts_in_edge(const MeshVert *v1,
                                   const MeshVert *v2,
                                   const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** Returns edge length */
float mesh_edge_calc_length(const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/** Returns edge length squared (for comparisons) */
float mesh_edge_calc_length_squared(const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Utility function, since enough times we have an edge
 * and want to access 2 connected faces.
 *
 * return true when only 2 faces are found.
 */
bool mesh_edge_face_pair(MeshEdge *e, MeshFace **r_fa, MeshFace **r_fb) ATTR_NONNULL();
/**
 * Utility function, since enough times we have an edge
 * and want to access 2 connected loops.
 *
 * return true when only 2 faces are found.
 */
bool mesh_edge_loop_pair(MeshEdge *e, MeshLoop **r_la, MeshLoop **r_lb) ATTR_NONNULL();
LIB_INLINE MeshVert *mesh_edge_other_vert(MeshEdge *e, const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * Given a edge and a loop (assumes the edge is manifold). returns
 * the other faces loop, sharing the same vertex.
 *
 * <pre>
 * +-------------------+
 * |                   |
 * |                   |
 * |l_other <-- return |
 * +-------------------+ <-- A manifold edge between 2 faces
 * |l    e  <-- edge   |
 * |^ <-------- loop   |
 * |                   |
 * +-------------------+
 * </pre>
 */
MeshLoop *mesh_edge_other_loop(MeshEdge *e, MeshLoop *l) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * brief Other Loop in Face Sharing an Edge
 *
 * Finds the other loop that shares v with e loop in f.
 * <pre>
 *     +----------+
 *     |          |
 *     |    f     |
 *     |          |
 *     +----------+ <-- return the face loop of this vertex.
 *     v --> e
 *     ^     ^ <------- These vert args define direction
 *                      in the face to check.
 *                      The faces loop direction is ignored.
 * </pre>
 *
 * note caller must ensure a e is used in f
 */
MeshLoop *mesh_face_other_edge_loop(MeshFace *f, MeshEdge *e, MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * See mesh_face_other_edge_loop This is the same functionality
 * to be used when the edges loop is already known.
 */
MeshLoop *mesh_loop_other_edge_loop(MeshLoop *l, MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * brief Other Loop in Face Sharing a Vertex
 *
 * Finds the other loop in a face.
 *
 * This function returns a loop in f that shares an edge with \a v
 * The direction is defined by v_prev, where the return value is
 * the loop of what would be 'v_next'
 * <pre>
 *     +----------+ <-- return the face loop of this vertex.
 *     |          |
 *     |    f     |
 *     |          |
 *     +----------+
 *     v_prev --> v
 *     ^^^^^^     ^ <-- These vert args define direction
 *                      in the face to check.
 *                      The faces loop direction is ignored.
 * </pre>
 *
 * note v_prev and v _implicitly_ define an edge.
 */
MeshLoop *mesh_face_other_vert_loop(MeshFace *f, MeshVert *v_prev, MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * Return the other loop that uses this edge.
 *
 * In this case the loop defines the vertex,
 * the edge passed in defines the direction to step.
 *
 * <pre>
 *     +----------+ <-- Return the face-loop of this vertex.
 *     |          |
 *     |        e | <-- This edge defines the direction.
 *     |          |
 *     +----------+ <-- This loop defines the face and vertex..
 *                l
 * </pre>
 *
 */
MeshLoop *mesh_loop_other_vert_loop_by_edge(MeshLoop *l, MeshEdge *e) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * brief Other Loop in Face Sharing a Vert
 *
 * Finds the other loop that shares v with e loop in f.
 * <pre>
 *     +----------+ <-- return the face loop of this vertex.
 *     |          |
 *     |          |
 *     |          |
 *     +----------+ <-- This vertex defines the direction.
 *           l    v
 *           ^ <------- This loop defines both the face to search
 *                      and the edge, in combination with 'v'
 *                      The faces loop direction is ignored.
 * </pre>
 */
MeshLoop *mesh_loop_other_vert_loop(MeshLoop *l, MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Utility function to step around a fan of loops,
 * using an edge to mark the previous side.
 *
 * note all edges must be manifold,
 * once a non manifold edge is hit, return NULL.
 *
 * code{.unparsed}
 *                ,.,-->|
 *            _,-'      |
 *          ,'          | (notice how 'e_step'
 *         /            |  and 'l' define the
 *        /             |  direction the arrow
 *       |     return   |  points).
 *       |     loop --> |
 * ---------------------+---------------------
 *         ^      l --> |
 *         |            |
 *  assign e_step       |
 *                      |
 *   begin e_step ----> |
 *                      |
 * endcode
 */
MeshLoop *mesh_vert_step_fan_loop(MeshLoop *l, MeshEdge **e_step) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * Get the first loop of a vert. Uses the same initialization code for the first loop of the
 * iterator API
 */
MeshLoop *mesh_vert_find_first_loop(MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * A version of Mesh_vert_find_first_loop that ignores hidden loops.
 */
MeshLoop *mesh_vert_find_first_loop_visible(MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Only MeshEdge.l access us needed, however when we want the first visible loop,
 * a utility function is needed.
 */
MeshLoop *mesh_edge_find_first_loop_visible(MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** Check if verts share a face. **/
bool mesh_vert_pair_share_face_check(MeshVert *v_a, MeshVert *v_b) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
bool mesh_vert_pair_share_face_check_cb(MeshVert *v_a,
                                       MeshVert *v_b,
                                       bool (*test_fn)(MeshFace *f, void *user_data),
                                       void *user_data) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3);
MeshFace *mesh_vert_pair_shared_face_cb(MeshVert *v_a,
                                        MeshVert *v_b,
                                        bool allow_adjacent,
                                        bool (*callback)(MeshFace *, MeshLoop *, BMLoop *, void *userdata),
                                        void *user_data,
                                        MeshLoop **r_l_a,
                                        MeshLoop **r_l_b) ATTR_NONNULL(1, 2, 4, 6, 7);
/** Given 2 verts, find the smallest face they share and give back both loops. **/
MeshFace *mesh_vert_pair_share_face_by_len(
    MeshVert *v_a, MeshVert *v_b, MeshLoop **r_l_a, MeshLoop **r_l_b, bool allow_adjacent) ATTR_NONNULL();
/**
 * Given 2 verts,
 * find a face they share that has the lowest angle across these verts and give back both loops.
 *
 * This can be better than mesh_vert_pair_share_face_by_len
 * because concave splits are ranked lowest.
 */
MeshFace *mesh_vert_pair_share_face_by_angle(
    MeshVert *v_a, MeshVert *v_b, MeshLoop **r_l_a, MeshLoop **r_l_b, bool allow_adjacent) ATTR_NONNULL();

MeshFace *mesh_edge_pair_share_face_by_len(
    MeshEdge *e_a, MeshEdge *e_b, MeshLoop **r_l_a, MeshLoop **r_l_b, bool allow_adjacent) ATTR_NONNULL();

int mesh_vert_edge_count_nonwire(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
#define mesh_vert_edge_count_is_equal(v, n) (mesh_vert_edge_count_at_most(v, (n) + 1) == n)
#define mesh_vert_edge_count_is_over(v, n) (mesh_vert_edge_count_at_most(v, (n) + 1) == (n) + 1)
int mesh_vert_edge_count_at_most(const MeshVert *v, int count_max) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/** Returns the number of edges around this vertex. **/
int mesh_vert_edge_count(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
#define mesh_edge_face_count_is_equal(e, n) (mesh_edge_face_count_at_most(e, (n) + 1) == n)
#define mesh_edge_face_count_is_over(e, n) (mesh_edge_face_count_at_most(e, (n) + 1) == (n) + 1)
int mesh_edge_face_count_at_most(const MeshEdge *e, int count_max) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * Returns the number of faces around this edge */
int mesh_edge_face_count(const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
#define mesh_vert_face_count_is_equal(v, n) (mesh_vert_face_count_at_most(v, (n) + 1) == n)
#define mesh_vert_face_count_is_over(v, n) (mesh_vert_face_count_at_most(v, (n) + 1) == (n) + 1)
int mesh_vert_face_count_at_most(const MeshVert *v, int count_max) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * Returns the number of faces around this vert
 * length matches MESH_LOOPS_OF_VERT iterator
 */
int mesh_vert_face_count(const MESHVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * The function takes a vertex at the center of a fan and returns the opposite edge in the fan.
 * All edges in the fan must be manifold, otherwise return NULL.
 *
 * note This could (probably) be done more efficiently.
 */
MeshEdge *mesh_vert_other_disk_edge(MeshVert *v, MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** Fast alternative to `(mesh_vert_edge_count(v) == 2)`. */
bool mesh_vert_is_edge_pair(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Fast alternative to `(mesh_vert_edge_count(v) == 2)`
 * that checks both edges connect to the same faces.
 */
bool mesh_vert_is_edge_pair_manifold(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Access a verts 2 connected edges.
 *
 * return true when only 2 verts are found.
 */
bool mesh_vert_edge_pair(MeshVert *v, MeshEdge **r_e_a, MeshEdge **r_e_b);
/**
 * Return true if the vertex is connected to _any_ faces.
 *
 * same as `mesh_vert_face_count(v) != 0` or `mesh_vert_find_first_loop(v) == NULL`.
 */
bool mesh_vert_face_check(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Tests whether or not the vertex is part of a wire edge.
 * (ie: has no faces attached to it)
 */
bool mesh_vert_is_wire(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
LIB_INLINE bool mesh_edge_is_wire(const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * A vertex is non-manifold if it meets the following conditions:
 * 1: Loose - (has no edges/faces incident upon it).
 * 2: Joins two distinct regions - (two pyramids joined at the tip).
 * 3: Is part of an edge with more than 2 faces.
 * 4: Is part of a wire edge.
 */
bool mesh_vert_is_manifold(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * A version of mesh_vert_is_manifold
 * which only checks if we're connected to multiple isolated regions.
 */
bool mesh_vert_is_manifold_region(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
LIB_INLINE bool mesh_edge_is_manifold(const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
bool mesh_vert_is_boundary(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
LIB_INLINE bool mesh_edge_is_boundary(const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
LIB_INLINE bool mesh_edge_is_contiguous(const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Check if the edge is convex or concave
 * (depends on face winding)
 */
bool mesh_edge_is_convex(const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/** return true when loop customdata is contiguous. **/
bool mesh_edge_is_contiguous_loop_cd(const MeshEdge *e,
                                      int cd_loop_type,
                                      int cd_loop_offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** The number of loops connected to this loop (not including disconnected regions). **/
int mesh_loop_region_loops_count_at_most(MeshLoop *l, int *r_loop_total) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1);
int mesh_loop_region_loops_count(MeshLoop *l) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1);
/**
 * Check if the loop is convex or concave
 * (depends on face normal)
 */
bool mesh_loop_is_convex(const MeshLoop *l) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
LIB_INLINE bool mesh_loop_is_adjacent(const BMLoop *l_a, const BMLoop *l_b) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * Check if a point is inside the corner defined by a loop
 * (within the 2 planes defined by the loops corner & face normal).
 *
 * return signed, squared distance to the loops planes, less than 0.0 when outside.
 */
float mesh_loop_point_side_of_loop_test(const MeshLoop *l, const float co[3]) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * Check if a point is inside the edge defined by a loop
 * (within the plane defined by the loops edge & face normal).
 *
 * return signed, squared distance to the edge plane, less than 0.0 when outside.
 */
float mesh_loop_point_side_of_edge_test(const MeshLoop *l, const float co[3]) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

/** return The previous loop, over eps_sq distance from l (or NULL if l_stop is reached). */
MeshLoop *mesh_loop_find_prev_nodouble(MeshLoop *l, MeshLoop *l_stop, float eps_sq);
/** return The next loop, over eps_sq distance from l (or NULL if l_stop is reached). */
MeshLoop *mesh_loop_find_next_nodouble(MeshLoop *l, MeshLoop *l_stop, float eps_sq);

/**
 * Calculates the angle between the previous and next loops
 * (angle at this loops face corner).
 *
 * return angle in radians
 */
float mesh_loop_calc_face_angle(const MeshLoop *l) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * brief mesh_loop_calc_face_normal
 *
 * Calculate the normal at this loop corner or fallback to the face normal on straight lines.
 *
 * param l: The loop to calculate the normal at
 * param r_normal: Resulting normal
 * return The length of the cross product (double the area).
 */
float mesh_loop_calc_face_normal(const MeshLoop *l, float r_normal[3]) ATTR_NONNULL();
/**
 * mesh_loop_calc_face_normal_safe_ex with predefined sane epsilon.
 *
 * Since this doesn't scale based on triangle size, fixed value works well.
 */
float mesh_loop_calc_face_normal_safe(const BMLoop *l, float r_normal[3]) ATTR_NONNULL();
/**
 * brief mesh_loop_calc_face_normal
 *
 * Calculate the normal at this loop corner or fallback to the face normal on straight lines.
 *
 * param l: The loop to calculate the normal at.
 * param epsilon_sq: Value to avoid numeric errors (1e-5f works well).
 * param r_normal: Resulting normal.
 */
float mesh_loop_calc_face_normal_safe_ex(const MeshLoop *l, float epsilon_sq, float r_normal[3])
    ATTR_ * A version of mesh_loop_calc_face_normal_safe_ex which takes vertex coordinates.
 */
float mesh_loop_calc_face_normal_safe_vcos_ex(const MeshLoop *l,
                                              const float normal_fallback[3],
                                              float const (*vertexCos)[3],
                                              float epsilon_sq,
                                              float r_normal[3]) ATTR_NONNULL();
float mesh_loop_calc_face_normal_safe_vcos(const MeshLoop *l,
                                           const float normal_fallback[3],
                                           float const (*vertexCos)[3],
                                           float r_normal[3]) ATTR_NONNULL();

/**
 * brief mesh_loop_calc_face_direction
 *
 * Calculate the direction a loop is pointing.
 *
 * param l: The loop to calculate the direction at
 * param r_dir: Resulting direction
 */
void mesh_loop_calc_face_direction(const MeshLoop *l, float r_dir[3]);
/** mesh_loop_calc_face_tangent
 *
 * Calculate the tangent at this loop corner or fallback to the face normal on straight lines.
 * This vector always points inward into the face.
 *
 * param l: The loop to calculate the tangent at
 * param r_tangent: Resulting tangent
 */
void mesh_loop_calc_face_tangent(const BMLoop *l, float r_tangent[3]);

/**
 * MESH EDGE/FACE ANGLE
 *
 * Calculates the angle between two faces.
 * Assumes the face normals are correct.
 *
 * return angle in radians
 */
float mesh_edge_calc_face_angle_ex(const BMEdge *e, float fallback) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
float mesh_edge_calc_face_angle(const BMEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * MESH EDGE/FACE ANGLE
 *
 * Calculates the angle between two faces.
 * Assumes the face normals are correct.
 *
 * return angle in radians
 */
float mesh_edge_calc_face_angle_signed_ex(const BMEdge *e, float fallback) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(); dps
/**
 * brief MESH EDGE/FACE ANGLE
 *
 * Calculates the angle between two faces in world space.
 * Assumes the face normals are correct.
 *
 * angle in radians
 */
float mesh_edge_calc_face_angle_with_imat3_ex(const MeshEdge *e,
                                            const float imat3[3][3],
                                            float fallback) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
float mesh_edge_calc_face_angle_with_imat3(const MeshEdge *e,
                                         const float imat3[3][3]) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
float mesh_edge_calc_face_angle_signed(const MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * MESH EDGE/FACE TANGENT
 *
 * Calculate the tangent at this loop corner or fallback to the face normal on straight lines.
 * This vector always points inward into the face.
 *
 * brief mesh_edge_calc_face_tangent
 * param e:
 * param e_loop: The loop to calculate the tangent at,
 * used to get the face and winding direction.
 * param r_tangent: The loop corner tangent to set
 */
void mesh_edge_calc_face_tangent(const MeshEdge *e, const MeshLoop *e_loop, float r_tangent[3])
    ATTR_NONNULL();
float mesh_vert_calc_edge_angle(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * MESH VERT/EDGE ANGLE
 *
 * Calculates the angle a verts 2 edges.
 *
 * returns the angle in radians
 */
float mesh_vert_calc_edge_angle_ex(const MeshVert *v, float fallback) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/**
 * this isn't optimal to run on an array of verts,
 * see 'solidify_add_thickness' for a function which runs on an array.
 */
float mesh_vert_calc_shell_factor(const MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/* alternate version of mesh_vert_calc_shell_factor which only
 * uses 'hflag' faces, but falls back to all if none found. */
float mesh_vert_calc_shell_factor_ex(const MeshVert *v,
                                   const float no[3],
                                   char hflag) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * quite an obscure function.
 * used in bmesh operators that have a relative scale options,
 */
float mesh_vert_calc_median_tagged_edge_length(const MeshVert *v) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

/** Returns the loop of the shortest edge in f. **/
MeshLoop *mesh_face_find_shortest_loop(MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Returns the loop of the longest edge in f.
 */
MeshLoop *mesh_face_find_longest_loop(MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

MeshEdge *mesh_edge_exists(MeshVert *v_a, MeshVert *v_b) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Returns an edge sharing the same vertices as this one.
 * This isn't an invalid state but tools should clean up these cases before
 * returning the mesh to the user.
 */
MeshEdge *mesh_edge_find_double(MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * Given a set of vertices (varr), find out if
 * there is a face with exactly those vertices
 * (and only those vertices).
 *
 * there used to be a mesh_face_exists_overlap function that checks for partial overlap.
 */
MeshFace *mesh_face_exists(MeshVert **varr, int len) ATTR_NONNULL(1);
/**
 * Check if the face has an exact duplicate (both winding directions).
 */
MeshFace *mesh_face_find_double(MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * Given a set of vertices and edges (\a varr, \a earr), find out if
 * all those vertices are filled in by existing faces that _only_ use those vertices.
 *
 * This is for use in cases where creating a face is possible but would result in
 * many overlapping faces.
 *
 * An example of how this is used: when 2 tri's are selected that share an edge,
 * pressing F-key would make a new overlapping quad (without a check like this)
 *
 * earr and varr can be in any order, however they _must_ form a closed loop.
 */
bool mesh_face_exists_multi(MeshVert **varr, MeshEdge **earr, int len) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
/* same as 'mesh_face_exists_multi' but built vert array from edges */
bool mesh_face_exists_multi_edge(MeshEdge **earr, int len) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * Given a set of vertices (varr), find out if
 * all those vertices overlap an existing face.
 *
 * The face may contain other vert not in varr.
 *
 * Its possible there are more than one overlapping faces,
 * in this case the first one found will be returned.
 *
 * param varr: Array of unordered verts.
 * param len: varr array length.
 * return The face or NULL.
 */
MeshFace *mesh_face_exists_overlap(MeshVert **varr, int len) ATTR_WARN_UNUSED_RESULT;
/**
 * Given a set of vertices (varr), find out if
 * there is a face that uses vertices only from this list
 * (that the face is a subset or made from the vertices given).
 *
 * param varr: Array of unordered verts.
 * param len: varr array length.
 */
bool mesh_face_exists_overlap_subset(MeshVert **varr, int len) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * Returns the number of faces that are adjacent to both f1 and f2,
 * Could be sped up a bit by not using iterators and by tagging
 * faces on either side, then count the tags rather then searching.
 */
int mesh_face_share_face_count(MeshFace *f_a, MeshFace *f_b) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Counts the number of edges two faces share (if any)
 */
int mesh_face_share_edge_count(MeshFace *f_a, MeshFace *f_b) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Counts the number of verts two faces share (if any).
 */
int mesh_face_share_vert_count(MeshFace *f_a, MeshFace *f_b) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * same as mesh_face_share_face_count but returns a bool
 */
bool mesh_face_share_face_check(MeshFace *f_a, MeshFace *f_b) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/** Returns true if the faces share an edge */
bool mesh_face_share_edge_check(MeshFace *f_a, MeshFace *f_b) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/** Returns true if the faces share a vert. */
bool mesh_face_share_vert_check(MeshFace *f_a, MeshFace *f_b) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** Returns true when 2 loops share an edge (are adjacent in the face-fan */
bool mesh_loop_share_edge_check(MeshLoop *l_a, MeshLoop *l_b) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** Test if e1 shares any faces with e2 */
bool mesh_edge_share_face_check(MeshEdge *e1, MeshEdge *e2) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Test if e1 shares any quad faces with e2
 */
bool mesh_edge_share_quad_check(MeshEdge *e1, MeshEdge *e2) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/** Tests to see if e1 shares a vertex with e */
bool mesh_edge_share_vert_check(MeshEdge *e1, MeshEdge *e2) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** Return the shared vertex between the two edges or NUL */
MeshVert *mesh_edge_share_vert(MeshEdge *e1, MeshEdge *e2) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Return the Loop Shared by Edge and Vert
 *
 * Finds the loop used which uses in face loop l
 *
 * this function takes a loop rather than an edge
 * so we can select the face that the loop should be from.
 */
MeshLoop *mesh_edge_vert_share_loop(MeshLoop *l, MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/*
 * Return the Loop Shared by Face and Vertex
 *
 * Finds the loop used which uses v in face loop l
 *
 * currently this just uses simple loop in future may be sped up
 * using radial vars
 */
MeshLoop *mesh_face_vert_share_loop(MeshFace *f, MeshVert *v) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
/**
 * Return the Loop Shared by Face and Edge
 *
 * Finds the loop used which uses e in face loop l
 *
 * currently this just uses simple loop in future may be sped up
 * using radial vars
 */
MeshLoop *mesh_face_edge_share_loop(MeshFace *f, MeshEdge *e) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

void mesh_edge_ordered_verts(const MeshEdge *edge, MeshVert **r_v1, BMVert **r_v2) ATTR_NONNULL();
/**
 * Returns the verts of an edge as used in a face
 * if used in a face at all, otherwise just assign as used in the edge.
 *
 * Useful to get a deterministic winding order when calling
 * BM_face_create_ngon() on an arbitrary array of verts,
 * though be sure to pick an edge which has a face.
 *
 * \note This is in fact quite a simple check,
 * mainly include this function so the intent is more obvious.
 * We know these 2 verts will _always_ make up the loops edge
 */
void mesh_edge_ordered_verts_ex(const MeshEdge *edge,
                                MeshVert **r_v1,
                                MeshVert **r_v2,
                                const MeshLoop *edge_loop) ATTR_NONNULL();

bool mesh_vert_is_all_edge_flag_test(const MeshVert *v,
                                   char hflag,
                                   bool respect_hide) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
bool mesh_vert_is_all_face_flag_test(const MeshVert *v,
                                   char hflag,
                                   bool respect_hide) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
bool mesh_edge_is_all_face_flag_test(const MeshEdge *e,
                                   char hflag,
                                   bool respect_hide) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/* convenience functions for checking flags */
bool mesh_edge_is_any_vert_flag_test(const MeshEdge *e, char hflag) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
bool mesh_edge_is_any_face_flag_test(const MeshEdge *e, char hflag) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
bool mesh_face_is_any_vert_flag_test(const MeshFace *f, char hflag) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
bool mesh_face_is_any_edge_flag_test(const MeshFace *f, char hflag) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

bool mesh_edge_is_any_face_len_test(const MeshEdge *e, int len) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * Use within assert's to check normals are valid.
 */
bool mesh_face_is_normal_valid(const MeshFace *f) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

double mesh_calc_volume(Mesh *mesh, bool is_signed) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/**
 * Calculate isolated groups of faces with optional filtering.
 *
 * param mesh: the Mesh.
 * param r_groups_array: Array of ints to fill in, length of bm->totface
 *        (or when hflag_test is set, the number of flagged faces).
 * param r_group_index: index, length pairs into \a r_groups_array, size of return value
 *        int pairs: (array_start, array_length).
 * param filter_fn: Filter the edge-loops or vert-loops we step over (depends on \a htype_step).
 * param user_data: Optional user data for \a filter_fn, can be NULL.
 * param hflag_test: Optional flag to test faces,
 *        use to exclude faces from the calculation, 0 for all faces.
 * param htype_step: MESH_VERT to walk over face-verts, BM_EDGE to walk over faces edges
 *        (having both set is supported too).
 * return The number of groups found.
 */
int mesh_calc_face_groups(Mesh *mesh,
                             int *r_groups_array,
                             int (**r_group_index)[2],
                             MeshLoopFilterFn filter_fn,
                             MeshLoopPairFilterFn filter_pair_fn,
                             void *user_data,
                             char hflag_test,
                             char htype_step) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2, 3);
/**
 * Calculate isolated groups of edges with optional filtering.
 *
 * param mesh: the Mesh.
 * param r_groups_array: Array of ints to fill in, length of `mesh->totedge`
 *        (or when hflag_test is set, the number of flagged edges).
 * param r_group_index: index, length pairs into \a r_groups_array, size of return value
 *        int pairs: (array_start, array_length).
 * param filter_fn: Filter the edges or verts we step over (depends on \a htype_step)
 *        as to which types we deal with.
 * param user_data: Optional user data for \a filter_fn, can be NULL.
 * param hflag_test: Optional flag to test edges,
 *        use to exclude edges from the calculation, 0 for all edges.
 * return The number of groups found.
 *
 * Unlike mesh_calc_face_groups there is no 'htype_step' argument,
 *       since we always walk over verts.
 */
int mesh_calc_edge_groups(Mesh *mesh,
                          int *r_groups_array,
                          int (**r_group_index)[2],
                          MeshVertFilterFn filter_fn,
                          void *user_data,
                          char hflag_test) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2, 3);

/**
 * This is an alternative to mesh_calc_edge_groups.
 *
 * While we could call this, then create vertex & face arrays,
 * it requires looping over geometry connectivity twice,
 * this slows down edit-mesh separate by loose parts, see: T70864.
 */
int mesh_calc_edge_groups_as_arrays(Mesh *mesh,
                                    MeshVert **verts,
                                    MeshEdge **edges,
                                    MeshFace **faces,
                                    int (**r_groups)[3]) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3, 4, 5);

/* Not really any good place to put this. */
float mesh_subd_falloff_calc(int falloff, float val) ATTR_WARN_UNUSED_RESULT;

#include "mesh_query_inline.h"
