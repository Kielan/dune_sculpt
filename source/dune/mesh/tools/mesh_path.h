#pragma once

struct MCalcPathParams {
  uint use_topology_distance : 1;
  uint use_step_face : 1;
};

struct LinkNode *mesh_calc_path_vert(Mesh *bm,
                                     MeshVert *v_src,
                                     MeshVert *v_dst,
                                     const struct MeshCalcPathParams *params,
                                     bool (*filter_fn)(MeshVert *, void *),
                                     void *user_data) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3, 5);

struct LinkNode *mesh_calc_path_edge(Mesh *bm,
                                     MEdge *e_src,
                                     MEdge *e_dst,
                                     const struct BMCalcPathParams *params,
                                     bool (*filter_fn)(BMEdge *, void *),
                                     void *user_data) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3, 5);

struct LinkNode *mesh_calc_path_face(Mesh *bm,
                                     MFace *f_src,
                                     MFace *f_dst,
                                     const struct BMCalcPathParams *params,
                                     bool (*filter_fn)(BMFace *, void *),
                                     void *user_data) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3, 5);
