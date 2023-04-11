#pragma once

/**
 * Geometry must be completely isolated. */
void mesh_copy_arrays(Mesh *bm_src,
                         Mesh *bm_dst,
                         MVert **verts_src,
                         uint verts_src_len,
                         MEdge **edges_src,
                         uint edges_src_len,
                         MFace **faces_src,
                         uint faces_src_len);
