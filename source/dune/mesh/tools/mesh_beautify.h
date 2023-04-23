#pragma once

enum {
  /** Vertices tags must match (special case). */
  VERT_RESTRICT_TAG = (1 << 0),
  /** Don't rotate out of degenerate state (needed for iterative rotation). */
  EDGE_RESTRICT_DEGENERATE = (1 << 1),
};

/** This function sets the edge indices to invalid values. **/
void mesh_beautify_fill(Mesh *mesh,
                        MeshEdge **edge_array,
                        int edge_array_len,
                        short flag,
                        short method,
                        short oflag_edge,
                        short oflag_face);

/**
 * Assuming we have 2 triangles sharing an edge (2 - 4),
 * check if the edge running from (1 - 3) gives better results.
 *
 * return (negative number means the edge can be rotated, lager == better).
 */
float mesh_verts_calc_rotate_beauty(const MeshVert *v1,
                                    const MeshVert *v2,
                                    const MeshVert *v3,
                                    const MeshVert *v4,
                                    short flag,
                                    short method);
