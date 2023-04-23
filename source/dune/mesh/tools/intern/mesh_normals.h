#pragma once

#include "mesh_class.h"

struct MeshNormalsUpdateParams {
  /**
   * When calculating tessellation as well as normals, tessellate & calculate face normals
   * for improved performance. See MeshCalcTessellationParams
   */
  bool face_normals;
};

/**
 * Mesh Compute Normals
 *
 * Updates the normals of a mesh.
 */
void mesh_normals_update_ex(Mesh *mesh, const struct MeshNormalsUpdateParams *param);
void mesh_normals_update(Mesh *mesh);
/**
 * A version of mesh_normals_update that updates a subset of geometry,
 * used to avoid the overhead of updating everything.
 */
void mesh_normals_update_with_partial_ex(Mesh *mesh,
                                         const struct MeshPartialUpdate *meshinfo,
                                         const struct MeshNormalsUpdateParams *param);
void mesh_normals_update_with_partial(Mesh *mesh, const struct MeshPartialUpdate *meshinfo);

/**
 * Mesh Compute Normals from/to external data.
 *
 * Computes the vertex normals of a mesh into vnos,
 * using given vertex coordinates (vcos) and polygon normals (fnos).
 */
void mesh_verts_calc_normal_vcos(Mesh *mesh,
                                 const float (*fnos)[3],
                                 const float (*vcos)[3],
                                 float (*vnos)[3]);
/**
 * Mesh Compute Loop Normals from/to external data.
 *
 * Compute split normals, i.e. vertex normals associated with each poly (hence 'loop normals').
 * Useful to materialize sharp edges (or non-smooth faces) without actually modifying the geometry
 * (splitting edges).
 */
void mesh_loops_calc_normal_vcos(Mesh *mesh,
                               const float (*vcos)[3],
                               const float (*vnos)[3],
                               const float (*fnos)[3],
                               bool use_split_normals,
                               float split_angle,
                               float (*r_lnos)[3],
                               struct MeshLoopNorSpaceArray *r_lnors_spacearr,
                               short (*clnors_data)[2],
                               int cd_loop_clnors_offset,
                               bool do_rebuild);

/**
 * Check whether given loop is part of an unknown-so-far cyclic smooth fan, or not.
 * Needed because cyclic smooth fans have no obvious 'entry point',
 * and yet we need to walk them once, and only once.
 */
bool mesh_loop_check_cyclic_smooth_fan(MeshLoop *l_curr);
void mesh_lnorspacearr_store(Mesh *mesh, float (*r_lnors)[3]);
void mesh_lnorspace_invalidate(Mesh *mesh, bool do_invalidate_all);
void mesh_lnorspace_rebuild(Mesh *m, bool preserve_clnor);
/**
 * warning This function sets MESH_ELEM_TAG on loops & edges via _mesh_loops_calc_normals,
 * take care to run this before setting up tags.
 */
void mesh_lnorspace_update(Mesh *m);
void mesh_normals_loops_edges_tag(Mesh *mesh, bool do_edges);
#ifndef NDEBUG
void mesh_lnorspace_err(Mesh *mesh);
#endif

/* Loop Generics */
struct MeshLoopNorEditDataArray *mesh_loop_normal_editdata_array_init(Mesh *mesh,
                                                                      bool do_all_loops_of_vert);
void mesh_loop_normal_editdata_array_free(struct MeshLoopNorEditDataArray *lnors_ed_arr);

/**
 * warning This function sets MESH_ELEM_TAG on loops & edges via mesh_loops_calc_normals,
 * take care to run this before setting up tags.
 */
bool mesh_custom_loop_normals_to_vector_layer(struct Mesh *mesh);
void mesh_custom_loop_normals_from_vector_layer(struct Mesh *mesh, bool add_sharp_edges);

/**
 * Define sharp edges as needed to mimic 'autosmooth' from angle threshold.
 *
 * Used when defining an empty custom loop normals data layer,
 * to keep same shading as with auto-smooth!
 */
void mesh_edges_sharp_from_angle_set(Mesh *mesh, float split_angle);
