#pragma once

/**
 * Dissolve Vert
 *
 * Turns the face region surrounding a manifold vertex into a single polygon.
 *
 * par Example:
 * <pre>
 *              +---------+             +---------+
 *              |  \   /  |             |         |
 *     Before:  |    v    |      After: |         |
 *              |  /   \  |             |         |
 *              +---------+             +---------+
 * </pre>
 *
 * This function can also collapse edges too
 * in cases when it can't merge into faces.
 *
 * par Example:
 * <pre>
 *     Before:  +----v----+      After: +---------+
 * </pre>
 *
 * note dissolves vert, in more situations than mesh_disk_dissolve
 * (e.g. if the vert is part of a wire edge, etc).
 */
bool mesh_vert_dissolve(Mesh *mesh, MeshVert *v);

/** dissolves all faces around a vert, and removes it. **/
bool mesh_disk_dissolve(Mesh *mesh, MeshVert *v);

/**
 * Faces Join Pair
 *
 * Joins two adjacent faces together.
 *
 * This method calls to mesh_faces_join to do its work.
 * This means connected edges which also share the two faces will be joined.
 *
 * If the windings do not match the winding of the new face will follow
 * a l_a's winding (i.e. a l_b will be reversed before the join).
 *
 * return The combined face or NULL on failure.
 */
MeshFace *mesh_faces_join_pair(Mesh *mesh, MeshLoop *l_a, BMLoop *l_b, bool do_del);

/** see: mesh_polygon_edgenet.h for mesh_face_split_edgenet */

/**
 * Face Split
 *
 * Split a face along two vertices. returns the newly made face, and sets
 * the a r_l member to a loop in the newly created edge.
 *
 * param mesh: The mesh
 * param f: the original face
 * param l_a, l_b: Loops of this face, their vertices define
 * the split edge to be created (must be differ and not can't be adjacent in the face).
 * param r_l: pointer which will receive the BMLoop for the split edge in the new face
 * param example: Edge used for attributes of splitting edge, if non-NULL
 * param no_double: Use an existing edge if found
 *
 * return Pointer to the newly created face representing one side of the split
 * if the split is successful (and the original face will be the other side).
 * NULL if the split fails.
 */
MeshFace *mesh_face_split(
    Mesh *mesh, MeshFace *f, MeshLoop *l_a, MeshLoop *l_b, MeshLoop **r_l, MeshEdge *example, bool no_double);

/**
 * Face Split with intermediate points
 *
 * Like mesh_face_split, but with an edge split by n intermediate points with given coordinates.
 *
 * param mesh: The mesh.
 * param f: the original face.
 * param l_a, l_b: Vertices which define the split edge, must be different.
 * param cos: Array of coordinates for intermediate points.
 * param n: Length of cos (must be > 0).
 * param r_l: pointer which will receive the MeshLoop.
 * for the first split edge (from \a l_a) in the new face.
 * param example: Edge used for attributes of splitting edge, if non-NULL.
 *
 * return Pointer to the newly created face representing one side of the split
 * if the split is successful (and the original face will be the other side).
 * NULL if the split fails.
 */
MeshFace *mesh_face_split_n(Mesh *mesh,
                            MeshFace *f,
                            MeshLoop *l_a,
                            MeshLoop *l_b,
                            float cos[][3],
                            int n,
                            MeshLoop **r_l,
                            MeshEdge *example);

/**
 * Vert Collapse Faces
 *
 * Collapses vertex v_kill that has only two manifold edges
 * onto a vertex it shares an edge with.
 * fac defines the amount of interpolation for Custom Data.
 *
 * note that this is not a general edge collapse function.
 *
 * note this function is very close to mesh_vert_collapse_edge,
 * both collapse a vertex and return a new edge.
 * Except this takes a factor and merges custom data.
 *
 * param mesh: The mesh
 * param e_kill: The edge to collapse
 * param v_kill: The vertex  to collapse into the edge
 * param fac: The factor along the edge
 * param join_faces: When true the faces around the vertex will be joined
 * otherwise collapse the vertex by merging the 2 edges this vert touches into one.
 * param kill_degenerate_faces: Removes faces with less than 3 verts after collapsing.
 *
 * returns The New Edge
 */
MeshEdge *mesh_vert_collapse_faces(Mesh *mesh,
                                   MeshEdge *e_kill,
                                   MeshVert *v_kill,
                                   float fac,
                                   bool do_del,
                                   bool join_faces,
                                   bool kill_degenerate_faces,
                                   bool kill_duplicate_faces);
/**
 * Vert Collapse Faces
 *
 * Collapses a vertex onto another vertex it shares an edge with.
 *
 * return The New Edge
 */
MeshEdge *mesh_vert_collapse_edge(Mesh *mesh,
                                  MeshEdge *e_kill,
                                  MeshVert *v_kill,
                                  bool do_del,
                                  bool kill_degenerate_faces,
                                  bool kill_duplicate_faces);

/** Collapse and edge into a single vertex. */
MeshVert *mesh_edge_collapse(
    Mesh *mesh, MeshEdge *e_kill, MeshVert *v_kill, bool do_del, bool kill_degenerate_faces);

/**
 * Edge Split
 *
 * <pre>
 * Before: v
 *         +-----------------------------------+
 *                           e
 *
 * After:  v                 v_new (returned)
 *         +-----------------+-----------------+
 *                 r_e                e
 * </pre>
 *
 * param e: The edge to split.
 * param v: One of the vertices in e and defines the "from" end of the splitting operation,
 * the new vertex will be fac of the way from v to the other end.
 * param r_e: The newly created edge.
 * return  The new vertex.
 */
MeshVert *mesh_edge_split(Mesh *mesh, MeshEdge *e, MeshVert *v, MeshEdge **r_e, float fac);

/**
 * brief Split an edge multiple times evenly
 *
 * param r_varr: Optional array, verts in between (v1 -> v2)
 */
MeshVert *mesh_edge_split_n(Mesh *mesh, MeshEdge *e, int numcuts, MeshVert **r_varr);

/**
 * Swap v1 & v2
 *
 * Typically we shouldn't care about this, however it's used when extruding wire edges.
 */
void mesh_edge_verts_swap(MeshEdge *e);

bool mesh_face_validate(MeshFace *face, FILE *err);

/**
 * Calculate the 2 loops which _would_ make up the newly rotated Edge
 * but don't actually change anything.
 *
 * Use this to further inspect if the loops to be connected have issues:
 *
 * Examples:
 * - the newly formed edge already exists
 * - the new face would be degenerate (zero area / concave /  bow-tie)
 * - may want to measure if the new edge gives improved results topology.
 *   over the old one, as with beauty fill.
 *
 * mesh_edge_rotate_check must have already run.
 */
void mesh_edge_calc_rotate(MeshEdge *e, bool ccw, MeshLoop **r_l1, MeshLoop **r_l2);
/**
 * Check if Rotate Edge is OK
 *
 * Quick check to see if we could rotate the edge,
 * use this to avoid calling exceptions on common cases.
 */
bool mesh_edge_rotate_check(MeshEdge *e);
/**
 * Check if Edge Rotate Gives Degenerate Faces
 *
 * Check 2 cases
 * 1) does the newly forms edge form a flipped face (compare with previous cross product)
 * 2) does the newly formed edge cause a zero area corner (or close enough to be almost zero)
 *
 * param e: The edge to test rotation.
 * param l1, l2: are the loops of the proposed verts to rotate too and should
 * be the result of calling mesh_edge_calc_rotate
 */
bool mesh_edge_rotate_check_degenerate(MeshEdge *e, MeshLoop *l1, MeshLoop *l2);
bool mesh_edge_rotate_check_beauty(MeshEdge *e, MeshLoop *l1, MeshLoop *l2);
/**
 * Rotate Edge
 *
 * Spins an edge topologically,
 * either counter-clockwise or clockwise depending on ccw.
 *
 * return The spun edge, NULL on error
 * (e.g., if the edge isn't surrounded by exactly two faces).
 *
 * This works by dissolving the edge then re-creating it,
 * so the returned edge won't have the same pointer address as the original one.
 *
 * see header definition for check_flag enum.
 */
MeshEdge *mesh_edge_rotate(Mesh *mesh, MeshEdge *e, bool ccw, short check_flag);

/** Flags for mesh_edge_rotate */
enum {
  /** Disallow rotating when the new edge matches an existing one. */
  MESH_EDGEROT_CHECK_EXISTS = (1 << 0),
  /** Overrides existing check, if the edge already, rotate and merge them. */
  MESH_EDGEROT_CHECK_SPLICE = (1 << 1),
  /** Disallow creating bow-tie, concave or zero area faces */
  MESH_EDGEROT_CHECK_DEGENERATE = (1 << 2),
  /** Disallow rotating into ugly topology. */
  MESH_EDGEROT_CHECK_BEAUTY = (1 << 3),
};

/** Rip a single face from a vertex fan */
MeshVert *mesh_face_loop_separate(Mesh *mesh, MeshLoop *l_sep);
MeshVert *mesh_face_loop_separate_multi_isolated(Mesh *mesh, MeshLoop *l_sep);
MeshVert *mesh_face_loop_separate_multi(Mesh *mesh, MeshLoop **larr, int larr_len);
