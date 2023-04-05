#pragma once

struct MeshPartialUpdate;

struct MeshCalcTessellation_Params {
  /**
   * When calculating normals as well as tessellation, calculate normals after tessellation
   * for improved performance. See MeshCalcTessellationParams
   */
  bool face_normals;
};

void mesh_calc_tessellation_ex(Mesh *mesh,
                               MeshLoop *(*looptris)[3],
                               const struct MeshCalcTessellationParams *params);
void mesh_calc_tessellation(Mesh *m, MeshLoop *(*looptris)[3]);

/** A version of mesh_calc_tessellation that avoids degenerate triangles. **/
void mesh_calc_tessellation_beauty(Mesh *m, MeshLoop *(*looptris)[3]);

void mesh_calc_tessellation_with_partial_ex(Mesh *m,
                                            MeshLoop *(*looptris)[3],
                                            const struct MeshPartialUpdate *mpinfo,
                                            const struct MeshCalcTessellationParams *params);
void mesh_calc_tessellation_with_partial(Mesh *m,
                                         MeshLoop *(*looptris)[3],
                                         const struct MeshPartialUpdate *mpinfo);
