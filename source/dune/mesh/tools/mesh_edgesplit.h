#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * param use_verts: Use flagged verts instead of edges.
 * param tag_only: Only split tagged edges.
 * param copy_select: Copy selection history.
 */
void mesh_edgesplit(Mesh *m, bool use_verts, bool tag_only, bool copy_select);

#ifdef __cplusplus
}
#endif
