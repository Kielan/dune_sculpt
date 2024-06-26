#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Intersect tessellated faces
 * leaving the resulting edges tagged.
 *
 * param test_fn: Return value: -1: skip, 0: tree_a, 1: tree_b (use_self == false)
 * param boolean_mode: -1: no-boolean, 0: intersection... see #BMESH_ISECT_BOOLEAN_ISECT.
 * return true if the mesh is changed (intersections cut or faces removed from boolean).
 */
bool mesh_intersect(Mesh *mesh,
                    struct MeshLoop *(*looptris)[3],
                    int looptris_tot,
                    int (*test_fn)(MeshFace *f, void *user_data),
                    void *user_data,
                    bool use_self,
                    bool use_separate,
                    bool use_dissolve,
                    bool use_island_connect,
                    bool use_partial_connect,
                    bool use_edge_tag,
                    int boolean_mode,
                    float eps);

enum {
  MESH_ISECT_BOOLEAN_NONE = -1,
  /* aligned with BooleanModifierOp */
  MESH_ISECT_BOOLEAN_ISECT = 0,
  MESH_ISECT_BOOLEAN_UNION = 1,
  MESH_ISECT_BOOLEAN_DIFFERENCE = 2,
};

#ifdef __cplusplus
}
#endif
