#pragma once

float mesh_loop_uv_calc_edge_length_squared(const MeshLoop *l,
                                          int cd_loop_uv_offset) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
float mesh_loop_uv_calc_edge_length(const MeshLoop *l, int cd_loop_uv_offset) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

/**
 * Computes the UV center of a face, using the mean average weighted by edge length.
 *
 * See mesh_face_calc_center_median_weighted for matching spatial functionality.
 *
 * param aspect: Calculate the center scaling by these values, and finally dividing.
 * Since correct weighting depends on having the correct aspect.
 */
void mesh_uv_calc_center_median_weighted(const MeshFace *f,
                                         const float aspect[2],
                                         int cd_loop_uv_offset,
                                         float r_cent[2]) ATTR_NONNULL();
void mesh_face_uv_calc_center_median(const MeshFace *f, int cd_loop_uv_offset, float r_cent[2])
    ATTR_NONNULL();

/** Calculate the UV cross product (use the sign to check the winding). **/
float mesh_face_uv_calc_cross(const MeshFace *f, int cd_loop_uv_offset) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

void mesh_face_uv_minmax(const MeshFace *f, float min[2], float max[2], int cd_loop_uv_offset);
void mesh_face_uv_transform(MeshFace *f, const float matrix[2][2], int cd_loop_uv_offset);

bool mesh_loop_uv_share_edge_check_with_limit(MeshLoop *l_a,
                                              MeshLoop *l_b,
                                              const float limit[2],
                                              int cd_loop_uv_offset) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

/** Check if two loops that share an edge also have the same UV coordinates. **/
bool mesh_loop_uv_share_edge_check(MeshLoop *l_a,
                                 MeshLoop *l_b,
                                 int cd_loop_uv_offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** Check if two loops that share a vertex also have the same UV coordinates. **/
bool mesh_edge_uv_share_vert_check(MeshEdge *e, MeshLoop *l_a, MeshLoop *l_b, int cd_loop_uv_offset)
    ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** Check if two loops that share a vertex also have the same UV coordinates. **/
bool mesh_loop_uv_share_vert_check(MeshLoop *l_a,
                                   MeshLoop *l_b,
                                   int cd_loop_uv_offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/** Check if the point is inside the UV face. **/
bool mesh_face_uv_point_inside_test(const MeshFace *f,
                                    const float co[2],
                                    int cd_loop_uv_offset) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
