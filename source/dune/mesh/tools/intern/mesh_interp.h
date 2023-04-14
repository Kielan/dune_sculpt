#pragma once

struct LinkNode;
struct MemArena;

void mesh_loop_interp_multires_ex(Mesh *mesh,
                                  MeshLoop *l_dst,
                                  const MeshFace *f_src,
                                  const float f_dst_center[3],
                                  const float f_src_center[3],
                                  int cd_loop_mdisp_offset);
/** Project the multi-resolution grid in target onto f_src's set of multi-resolution grids. */
void mesh_loop_interp_multires(Mesh *mesh, MeshLoop *l_dst, const MeshFace *f_src);

void mesh_face_interp_multires_ex(Mesh *mesh,
                                  MeshFace *f_dst,
                                  const MeshFace *f_src,
                                  const float f_dst_center[3],
                                  const float f_src_center[3],
                                  int cd_loop_mdisp_offset);
void mesh_face_interp_multires(Mesh *mesh, MeshFace *f_dst, const MeshFace *f_src);

void mesh_vert_interp_from_face(Mesh *mesh, MeshVert *v_dst, const MeshFace *f_src);

/**
 * Data, Interpolate From Verts
 *
 * Interpolates per-vertex data from two sources to v_dst
 *
 * note This is an exact match to mesh_data_interp_from_edges.
 */
void mesh_data_interp_from_verts(
    Mesh *mesh, const MeshVert *v_src_1, const MeshVert *v_src_2, MeshVert *v_dst, float fac);
/**
 * Data, Interpolate From Edges
 *
 * Interpolates per-edge data from two sources to \a e_dst.
 *
 * This is an exact match to mesh_data_interp_from_verts.
 */
void mesh_data_interp_from_edges(
    Mesh *mesh, const MeshEdge *e_src_1, const MeshEdge *e_src_2, BMEdge *e_dst, float fac);
/**
 * Data Face-Vert Edge Interpolate
 *
 * Walks around the faces of e and interpolates
 * the loop data between two sources.
 */
void mesh_data_interp_face_vert_edge(
    Mesh *mesh, const MeshVert *v_src_1, const MeshVert *v_src_2, MeshVert *v, MeshEdge *e, float fac);
void mesh_data_layer_add(Mesh *mesh, CustomData *data, int type);
void mesh_data_layer_add_named(Mesh *mesh, CustomData *data, int type, const char *name);
void mesh_data_layer_free(Mesh *mesh, CustomData *data, int type);
void mesh_data_layer_free_n(Mesh *mesh, CustomData *data, int type, int n);
void mesh_data_layer_copy(Mesh *mesh, CustomData *data, int type, int src_n, int dst_n);

float mesh_elem_float_data_get(CustomData *cd, void *element, int type);
void mesh_elem_float_data_set(CustomData *cd, void *element, int type, float val);

/**
 * Data Interpolate From Face
 *
 * Projects target onto source, and pulls interpolated custom-data from source.
 *
 * Only handles loop custom-data. multi-res is handled.
 */
void mesh_face_interp_from_face_ex(Mesh *mesh,
                                 MeshFace *f_dst,
                                 const MeshFace *f_src,
                                 bool do_vertex,
                                 const void **blocks,
                                 const void **blocks_v,
                                 float (*cos_2d)[2],
                                 float axis_mat[3][3]);
void mesh_face_interp_from_face(Mesh *mesh, MeshFace *f_dst, const MeshFace *f_src, bool do_vertex);
/**
 * Projects a single loop, target, onto f_src for custom-data interpolation.
 * multi-resolution is handled.
 * param do_vertex: When true the target's vert data will also get interpolated.
 */
void mesh_loop_interp_from_face(
    Mesh *mesh, MeshLoop *l_dst, const MeshFace *f_src, bool do_vertex, bool do_multires);

/**
 * Smooths boundaries between multi-res grids,
 * including some borders in adjacent faces.
 */
void mesh_face_multires_bounds_smooth(Mesh *mesh, MeshFace *f);

struct LinkNode *mesh_vert_loop_groups_data_layer_create(
    Mesh *mesh, MeshVert *v, int layer_n, const float *loop_weights, struct MemArena *arena);
/**
 * Take existing custom data and merge each fan's data.
 */
void mesh_vert_loop_groups_data_layer_merge(Mesh *mesh, struct LinkNode *groups, int layer_n);
/**
 * A version of mesh_vert_loop_groups_data_layer_merge
 * that takes an array of loop-weights (aligned with MESH_LOOPS_OF_VERT iterator).
 */
void mesh_vert_loop_groups_data_layer_merge_weights(Mesh *mesh,
                                                    struct LinkNode *groups,
                                                    int layer_n,
                                                    const float *loop_weights);
