#pragma once

#include "mesh_core.h"

struct MeshAllocTemplate;
struct Mesh;

/**
 * Fill in a vertex array from an edge array.
 *
 * returns false if any verts aren't found.
 */
bool mesh_verts_from_edges(MeshVert **vert_arr, MeshEdge **edge_arr, int len);

/**
 * Fill in an edge array from a vertex array (connected polygon loop).
 *
 * returns false if any edges aren't found.
 */
bool mesh_edges_from_verts(MeshEdge **edge_arr, MeshVert **vert_arr, int len);
/**
 * Fill in an edge array from a vertex array (connected polygon loop).
 * Creating edges as-needed.
 */
void mesh_edges_from_verts_ensure(Mesh *mesh, MeshEdge **edge_arr, MeshVert **vert_arr, int len);

/**
 * Makes an NGon from an un-ordered set of verts.
 *
 * Assumes:
 * - that verts are only once in the list.
 * - that the verts have roughly planer bounds
 * - that the verts are roughly circular
 *
 * There can be concave areas but overlapping folds from the center point will fail.
 *
 * A brief explanation of the method used
 * - find the center point
 * - find the normal of the vertex-cloud
 * - order the verts around the face based on their angle to the normal vector at the center point.
 *
 * \note Since this is a vertex-cloud there is no direction.
 */
void mesh_verts_sort_radial_plane(MeshVert **vert_arr, int len);

/**
 * \brief Make Quad/Triangle
 *
 * Creates a new quad or triangle from a list of 3 or 4 vertices.
 * If \a no_double is true, then a check is done to see if a face
 * with these vertices already exists and returns it instead.
 *
 * If a pointer to an example face is provided, its custom data
 * and properties will be copied to the new face.
 *
 * \note The winding of the face is determined by the order
 * of the vertices in the vertex array.
 */
MeshFace *mesh_face_create_quad_tri(Mesh *mesh,
                                MVert *v1,
                                MVert *v2,
                                MVert *v3,
                                MVert *v4,
                                const MFace *f_example,
                                eMCreateFlag create_flag);

/**
 * \brief copies face loop data from shared adjacent faces.
 *
 * \param filter_fn: A function that filters the source loops before copying
 * (don't always want to copy all).
 *
 * \note when a matching edge is found, both loops of that edge are copied
 * this is done since the face may not be completely surrounded by faces,
 * this way: a quad with 2 connected quads on either side will still get all 4 loops updated
 */
void BM_face_copy_shared(BMesh *bm, BMFace *f, BMLoopFilterFunc filter_fn, void *user_data);

/**
 * \brief Make NGon
 *
 * Makes an ngon from an unordered list of edges.
 * Verts \a v1 and \a v2 define the winding of the new face.
 *
 * \a edges are not required to be ordered, simply to form
 * a single closed loop as a whole.
 *
 * \note While this function will work fine when the edges
 * are already sorted, if the edges are always going to be sorted,
 * #BM_face_create should be considered over this function as it
 * avoids some unnecessary work.
 */
MeshFace *mesh_face_create_ngon(Mesh *bm,
                            MVert *v1,
                            MVert *v2,
                            MEdge **edges,
                            int len,
                            const MFace *f_example,
                            eMCreateFlag create_flag);
/**
 * Create an ngon from an array of sorted verts
 *
 * Special features this has over other functions.
 * - Optionally calculate winding based on surrounding edges.
 * - Optionally create edges between vertices.
 * - Uses verts so no need to find edges (handy when you only have verts)
 */
MeshFace *mesh_face_create_ngon_verts(Mesh *mesh,
                                      MeshVert **vert_arr,
                                      int len,
                                      const MeshFace *f_example,
                                      eMeshCreateFlag create_flag,
                                      bool calc_winding,
                                      bool create_edges);

/**
 * Copies attributes, e.g. customdata, header flags, etc, from one element
 * to another of the same type.
 */
void mesh_elem_attrs_copy_ex(Mesh *mesh_src,
                             Mesh *mesh_dst,
                             const void *ele_src_v,
                             void *ele_dst_v,
                             char hflag_mask,
                             uint64_t cd_mask_exclude);
void mesh_elem_attrs_copy(Mesh *mesh_src, Mesh *mesh_dst, const void *ele_src_v, void *ele_dst_v);
void mesh_elem_select_copy(Mesh *mesh_dst, void *ele_dst_v, const void *ele_src_v);

/**
 * Initialize the `bm_dst` layers in preparation for populating it's contents with multiple meshes.
 * Typically done using multiple calls to mesh_from_me with the same `bm` argument).
 *
 * \note While the custom-data layers of all meshes are created, the active layers are set
 * by the first instance mesh containing that layer type.
 * This means the first mesh should always be the main mesh (from the user perspective),
 * as this is the mesh they have control over (active UV layer for rendering for example).
 */
void mesh_copy_init_customdata_from_mesh_array(Mesh *mesh_dst,
                                               const struct Mesh *me_src_array[],
                                               int me_src_array_len,
                                               const struct MeshAllocTemplate *allocsize);
void mesh_copy_init_customdata_from_mesh(Mesh *mesh_dst,
                                         const struct Mesh *me_src,
                                         const struct MeshAllocTemplate *allocsize);
void mesh_copy_init_customdata(Mesh *mesh_dst,
                               Mesh *mesh_src,
                               const struct MeshAllocTemplate *allocsize);
/**
 * Similar to mesh_copy_init_customdata but copies all layers ignoring
 * flags like CD_FLAG_NOCOPY.
 *
 * param mesh_dst: Mesh whose custom-data layers will be added.
 * param mesh_src: Mesh whose custom-data layers will be copied.
 * param htype: Specifies which custom-data layers will be initiated.
 * param allocsize: Initialize the memory-pool before use (may be an estimate).
 */
void mesh_copy_init_customdata_all_layers(Mesh *mesh_dst,
                                          Mesh *mesh_src,
                                          char htype,
                                          const struct MeshAllocTemplate *allocsize);
Mesh *mesh_copy(BMesh *bm_old);

char mesh_face_flag_from_mflag(char mflag);
char mesh_edge_flag_from_mflag(short mflag);
/* ME -> BM */
char mesh_vert_flag_from_mflag(char mflag);
char mesh_face_flag_to_mflag(MeshFace *f);
short mesh_edge_flag_to_mflag(MeshEdge *e);
/* BM -> ME */
char mesh_vert_flag_to_mflag(MeshVert *v);
