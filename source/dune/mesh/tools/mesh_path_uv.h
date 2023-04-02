#pragma once

struct MeshCalcPathUVParams {
  uint use_topology_distance : 1;
  uint use_step_face : 1;
  uint cd_loop_uv_offset;
  float aspect_y;
};

struct LinkNode *mesh_calc_path_uv_vert(BMesh *bm,
                                        BMLoop *l_src,
                                        BMLoop *l_dst,
                                        const struct BMCalcPathUVParams *params,
                                        bool (*filter_fn)(BMLoop *, void *),
                                        void *user_data) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3, 5);

struct LinkNode *mesh_calc_path_uv_face(BMesh *bm,
                                        BMFace *f_src,
                                        BMFace *f_dst,
                                        const struct BMCalcPathUVParams *params,
                                        bool (*filter_fn)(BMFace *, void *),
                                        void *user_data) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3, 5);
