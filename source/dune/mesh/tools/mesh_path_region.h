#pragma once

struct LinkNode *mesh_calc_path_region_vert(BMesh *bm,
                                               BMElem *ele_src,
                                               BMElem *ele_dst,
                                               bool (*filter_fn)(BMVert *, void *user_data),
                                               void *user_data) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3);

struct LinkNode *mesh_calc_path_region_edge(Mesh *bm,
                                            MElem *ele_src,
                                            MElem *ele_dst,
                                            bool (*filter_fn)(BMEdge *, void *user_data),
                                            void *user_data) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3);

struct LinkNode *mesh_calc_path_region_face(BMesh *bm,
                                               BMElem *ele_src,
                                               BMElem *ele_dst,
                                               bool (*filter_fn)(BMFace *, void *user_data),
                                               void *user_data) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3);
