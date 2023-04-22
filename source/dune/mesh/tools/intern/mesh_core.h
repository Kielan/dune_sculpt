#pragma once

MeshFace *mesh_face_copy(Mesh *mesh_dst, Mesh *mesh_src, MeshFace *f, bool copy_verts, bool copy_edges);

typedef enum eMeshCreateFlag {
  MESH_CREATE_NOP = 0,
  /** Faces and edges only. */
  MESH_CREATE_NO_DOUBLE = (1 << 1),
  /**
   * Skip custom-data - for all element types data,
   * use if we immediately write custom-data into the element so this skips copying from 'example'
   * arguments or setting defaults, speeds up conversion when data is converted all at once.
   */
  MESH_CREATE_SKIP_CD = (1 << 2),
} eMeshCreateFlag;

/**
 * Main function for creating a new vertex. */
MeshVert *mesh_vert_create(Mesh *mesh,
                           const float co[3],
                           const MeshVert *v_example,
                           eMeshCreateFlag create_flag);
/**
 * Main function for creating a new edge.
 *
 * Duplicate edges are supported by the API however users should _never_ see them.
 * so unless you need a unique edge or know the edge won't exist,
 * you should call with no_double = true.
 */
MeshEdge *mesh_edge_create(
    Mesh *mesh, MeshVert *v1, MeshVert *v2, const MeshEdge *e_example, eMeshCreateFlag create_flag);
/**
 * Main face creation function
 *
 * param mesh: The mesh
 * param verts: A sorted array of verts size of len
 * param edges: A sorted array of edges size of len
 * param len: Length of the face
 * param create_flag: Options for creating the face
 */
MeshFace *mesh_face_create(Mesh *mesh,
                           MeshVert **verts,
                           MeshEdge **edges,
                           int len,
                           const MeshFace *f_example,
                           eMeshCreateFlag create_flag);
/** Wrapper for mesh_face_create when you don't have an edge array **/
MeshFace *mesh_face_create_verts(Mesh *mesh,
                                 MeshVert **vert_arr,
                                 int len,
                                 const MeshFace *f_example,
                                 eMeshCreateFlag create_flag,
                                 bool create_edges);

/** Kills all edges associated with f, along with any other faces containing those edges. **/
void mesh_face_edges_kill(Mesh *mesh, MeshFace *f);
/** kills all verts associated with f, along with any other faces containing
  * those vertices */
void mesh_face_verts_kill(Mesh *mesh, MeshFace *f);

/**
 * A version of Mesh_face_kill which removes edges and verts
 * which have no remaining connected geometry.
 */
void mesh_face_kill_loose(Mesh *mesh, MeshFace *f);

/** Kills f and its loops. */
void mesh_face_kill(Mesh *mesh, MeshFace *f);
/** Kills e and all faces that use it. */
void mesh_edge_kill(Mesh *mesh, MeshEdge *e);
/** Kills v and all edges that use it. */
void mesh_vert_kill(Mesh *mesh, MeshVert *v);

/**
 * Splice Edge
 *
 * Splice two unique edges which share the same two vertices into one edge.
 *  (e_src into e_dst, removing e_src).
 *
 * Success
 *
 * Edges must already have the same vertices.
 */
bool mesh_edge_splice(Mesh *mesh, MeshEdge *e_dst, MeshEdge *e_src);
/**
 * Splice Vert
 *
 * Merges two verts into one
 * (v_src into v_dst, removing v_src).
 *
 * Success
 *
 * This doesn't work for collapsing edges,
 * where v and vtarget are connected by an edge
 * (assert checks for this case).
 */
bool mesh_vert_splice(Mesh *mesh, MeshVert *v_dst, MeshVert *v_src);
/**
 * Check if splicing vertices would create any double edges.
 *
 * assume caller will handle case where verts share an edge.
 */
bool mesh_vert_splice_check_double(MeshVert *v_a, MeshVert *v_b);

/**
 * Loop Reverse
 *
 * Changes the winding order of a face from CW to CCW or vice versa.
 *
 * param cd_loop_mdisp_offset: Cached result of `CustomData_get_offset(&bm->ldata, CD_MDISPS)`.
 * param use_loop_mdisp_flip: When set, flip the Z-depth of the mdisp,
 * (use when flipping normals, disable when mirroring, eg: symmetrize).
 */
void mesh_kernel_loop_reverse(Mesh *mesh,
                              MeshFace *f,
                              int cd_loop_mdisp_offset,
                              bool use_loop_mdisp_flip);

/**
 * Avoid calling this where possible,
 * low level function so both face pointers remain intact but point to swapped data.
 * must be from the same mesh.
 */
void mesh_face_swap_data(MeshFace *f_a, MeshFace *f_b);

/**
 * Join Connected Faces
 *
 * Joins a collected group of faces into one. Only restriction on
 * the input data is that the faces must be connected to each other.
 *
 * The newly created combine MeshFace.
 *
 * If a pair of faces share multiple edges,
 * the pair of faces will be joined at every edge.
 *
 * this is a generic, flexible join faces function,
 * almost everything uses this, including mesh_faces_join_pair
 */
MeshFace *mesh_faces_join(Mesh *mesh, MeshFace **faces, int totface, bool do_del);
/** High level function which wraps both mesh_kernel_vert_separate and #bmesh_kernel_edge_separate */
void mesh_vert_separate(Mesh *mesh,
                        MeshVert *v,
                        MeshEdge **e_in,
                        int e_in_len,
                        bool copy_select,
                        MeshVert ***r_vout,
                        int *r_vout_len);
/** A version of mesh_vert_separate which takes a flag. */
void mesh_vert_separate_hflag(
    Mesh *mesh, MeshVert *v, char hflag, bool copy_select, MeshVert ***r_vout, int *r_vout_len);
void mesh_vert_separate_tested_edges(
    Mesh *mesh, MeshVert *v_dst, MeshVert *v_src, bool (*testfn)(MeshEdge *, void *arg), void *arg);

/** Mesh Kernel: For modifying structure
 * Names are on the verbose side but these are only for low-level access. **/
/**
 * Separate Vert
 *
 * Separates all disjoint fans that meet at a vertex, making a unique
 * vertex for each region. returns an array of all resulting vertices.
 *
 * note this is a low level function, bm_edge_separate needs to run on edges first
 * or, the faces sharing verts must not be sharing edges for them to split at least.
 *
 * return Success
 */
void mesh_kernel_vert_separate(
    Mesh *mesh, MeshVert *v, MeshVert ***r_vout, int *r_vout_len, bool copy_select);
/**
 * Separate Edge
 *
 * Separates a single edge into two edge: the original edge and
 * a new edge that has only l_sep in its radial.
 *
 * return Success
 *
 * note Does nothing if l_sep is already the only loop in the
 * edge radial.
 */
void mesh_kernel_edge_separate(Mesh *mesh, MeshEdge *e, MeshLoop *l_sep, bool copy_select);

/**
 * Split Face Make Edge (SFME)
 *
 * warning this is a low level function, most likely you want to use #BM_face_split()
 *
 * Takes as input two vertices in a single face.
 * An edge is created which divides the original face into two distinct regions.
 * One of the regions is assigned to the original face and it is closed off.
 * The second region has a new face assigned to it.
 *
 * \par Examples:
 * <pre>
 *     Before:               After:
 *      +--------+           +--------+
 *      |        |           |        |
 *      |        |           |   f1   |
 *     v1   f1   v2          v1======v2
 *      |        |           |   f2   |
 *      |        |           |        |
 *      +--------+           +--------+
 * </pre>
 *
 * the input vertices can be part of the same edge. This will
 * result in a two edged face. This is desirable for advanced construction
 * tools and particularly essential for edge bevel. Because of this it is
 * up to the caller to decide what to do with the extra edge.
 *
 * note If holes is NULL, then both faces will lose
 * all holes from the original face.  Also, you cannot split between
 * a hole vert and a boundary vert; that case is handled by higher-
 * level wrapping functions (when holes are fully implemented, anyway).
 *
 * note that holes represents which holes goes to the new face, and of
 * course this requires removing them from the existing face first, since
 * you cannot have linked list links inside multiple lists.
 *
 * return A MeshFace pointer
 */
MeshFace *mesh_kernel_split_face_make_edge(Mesh *mesh,
                                           MeshFace *f,
                                           MeshLoop *l_v1,
                                           MeshLoop *l_v2,
                                           MeshLoop **r_l,
#ifdef USE_MESH_HOLES
                                           ListBase *holes,
#endif
                                           MeshEdge *example,
                                           bool no_double);

/**
 * Split Edge Make Vert (SEMV)
 *
 * Takes e edge and splits it into two, creating a new vert.
 * tv should be one end of e : the newly created edge
 * will be attached to that end and is returned in r_e.
 *
 * \par Examples:
 *
 * <pre>
 *                     E
 *     Before: OV-------------TV
 *                 E       RE
 *     After:  OV------NV-----TV
 * </pre>
 *
 * return The newly created MeshVert pointer.
 */
MeshVert *mesh_kernel_split_edge_make_vert(Mesh *mesh, MeshVert *tv, MeshEdge *e, MeshEdge **r_e);
/**
 * Join Edge Kill Vert (JEKV)
 *
 * Takes an edge e_kill and pointer to one of its vertices v_kill
 * and collapses the edge on that vertex.
 *
 * \par Examples:
 *
 * <pre>
 *     Before:    e_old  e_kill
 *              +-------+-------+
 *              |       |       |
 *              v_old   v_kill  v_target
 *
 *     After:           e_old
 *              +---------------+
 *              |               |
 *              v_old           v_target
 * </pre>
 *
 * par Restrictions:
 * KV is a vertex that must have a valance of exactly two. Furthermore
 * both edges in KV's disk cycle (OE and KE) must be unique (no double edges).
 *
 * return The resulting edge, NULL for failure.
 *
 * note This euler has the possibility of creating
 * faces with just 2 edges. It is up to the caller to decide what to do with
 * these faces.
 */
MeshEdge *mesh_kernel_join_edge_kill_vert(Mesh *mesh,
                                          MeshEdge *e_kill,
                                          MeshVert *v_kill,
                                          bool do_del,
                                          bool check_edge_exists,
                                          bool kill_degenerate_faces,
                                          bool kill_duplicate_faces);
/**
 * Join Vert Kill Edge (JVKE)
 *
 * Collapse an edge, merging surrounding data.
 *
 * Unlike mesh_vert_collapse_edge & mesh_kernel_join_edge_kill_vert
 * which only handle 2 valence verts,
 * this can handle any number of connected edges/faces.
 *
 * <pre>
 * Before: -> After:
 * +-+-+-+    +-+-+-+
 * | | | |    | \ / |
 * +-+-+-+    +--+--+
 * | | | |    | / \ |
 * +-+-+-+    +-+-+-+
 * </pre>
 */
MeshVert *mesh_kernel_join_vert_kill_edge(Mesh *mesh,
                                          MeshEdge *e_kill,
                                          MeshVert *v_kill,
                                          bool do_del,
                                          bool check_edge_exists,
                                          bool kill_degenerate_faces);
/**
 * Join Face Kill Edge (JFKE)
 *
 * Takes two faces joined by a single 2-manifold edge and fuses them together.
 * The edge shared by the faces must not be connected to any other edges which have
 * Both faces in its radial cycle
 *
 * \par Examples:
 * <pre>
 *           A                   B
 *      +--------+           +--------+
 *      |        |           |        |
 *      |   f1   |           |   f1   |
 *     v1========v2 = Ok!    v1==V2==v3 == Wrong!
 *      |   f2   |           |   f2   |
 *      |        |           |        |
 *      +--------+           +--------+
 * </pre>
 *
 * In the example A, faces f1 and f2 are joined by a single edge,
 * and the euler can safely be used.
 * In example B however, f1 and f2 are joined by multiple edges and will produce an error.
 * The caller in this case should call mesh_kernel_join_edge_kill_vert on the extra edges
 * before attempting to fuse f1 and f2.
 *
 * The order of arguments decides whether or not certain per-face attributes are present
 * in the resultant face. For instance vertex winding, material index, smooth flags,
 * etc are inherited from f1, not f2.
 *
 * return A MeshFace pointer
 */
MeshFace *mesh_kernel_join_face_kill_edge(Mesh *mesh, MeshFace *f1, MeshFace *f2, MeshEdge *e);

/**
 * brief Un-glue Region Make Vert (URMV)
 *
 * Disconnects a face from its vertex fan at loop l_sep
 *
 * return The newly created MeshVert
 *
 * note Will be a no-op and return original vertex if only two edges at that vertex.
 */
MeshVert *mesh_kernel_unglue_region_make_vert(Mesh *mesh, MeshLoop *l_sep);
/**
 * A version of mesh_kernel_unglue_region_make_vert that disconnects multiple loops at once.
 * The loops must all share the same vertex, can be in any order
 * and are all moved to use a single new vertex - which is returned.
 *
 * This function handles the details of finding fans boundaries.
 */
MeshVert *mesh_kernel_unglue_region_make_vert_multi(Mesh *mesh, MeshLoop **larr, int larr_len);
/**
 * This function assumes l_sep is a part of a larger fan which has already been
 * isolated by calling mesh_kernel_edge_separate to segregate it radially.
 */
MeshVert *mesh_kernel_unglue_region_make_vert_multi_isolated(Mesh *mesh, MeshLoop *l_sep);
