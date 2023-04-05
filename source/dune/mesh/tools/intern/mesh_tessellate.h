#pragma once

struct MeshPartialUpdate;

struct BeshCalcTessellation_Params {
  /**
   * When calculating normals as well as tessellation, calculate normals after tessellation
   * for improved performance. See MeshCalcTessellation_Params
   */
  bool face_normals;
};

void mesh_calc_tessellation_ex(Mesh *mesh,
                               MeshLoop *(*looptris)[3],
                               const struct MeshCalcTessellationParams *params);
void mesh_calc_tessellation(Mesh *m, MeshLoop *(*looptris)[3]);

/**
 * A version of #BM_mesh_calc_tessellation that avoids degenerate triangles.
 */
void BM_mesh_calc_tessellation_beauty(BMesh *bm, BMLoop *(*looptris)[3]);

void BM_mesh_calc_tessellation_with_partial_ex(BMesh *bm,
                                               BMLoop *(*looptris)[3],
                                               const struct BMPartialUpdate *bmpinfo,
                                               const struct BMeshCalcTessellation_Params *params);
void BM_mesh_calc_tessellation_with_partial(BMesh *bm,
                                            BMLoop *(*looptris)[3],
                                            const struct BMPartialUpdate *bmpinfo);
