#pragma once

/**
 * Geometry must be completely isolated. */
void mesh_copy_arrays(Mesh *mesh_src,
                      Mesh *mesh_dst,
                      MeshVert **verts_src,
                      uint verts_src_len,
                      MeshEdge **edges_src,
                      uint edges_src_len,
                      MeshFace **faces_src,
                      uint faces_src_len);
