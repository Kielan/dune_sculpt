#pragma once

/**
 * Splits a face into many smaller faces defined by an edge-net.
 * handle customdata and degenerate cases.
 *
 * - Isolated holes or unsupported face configurations, will be ignored.
 * - Customdata calculations aren't efficient
 *   (need to calculate weights for each vert).
 */
bool mesh_face_split_edgenet(Mesh *mesh,
                             MeshFace *f,
                             MeshEdge **edge_net,
                             int edge_net_len,
                             MeshFace ***r_face_arr,
                             int *r_face_arr_len);

/**
 * For when the edge-net has holes in it-this connects them.
 *
 * param use_partial_connect: Support for handling islands connected by only a single edge,
 * note: is quite slow so avoid using where possible.
 * param mem_arena: Avoids many small allocs & should be cleared after each use.
 * take care since edge_net_new is stored in \a r_edge_net_new.
 */
bool mesh_face_split_edgenet_connect_islands(Mesh *mesh,
                                             MeshFace *f,
                                             MeshEdge **edge_net_init,
                                             uint edge_net_init_len,
                                             bool use_partial_connect,
                                             struct MemArena *mem_arena,
                                             MeshEdge ***r_edge_net_new,
                                             uint *r_edge_net_new_len) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL(1, 2, 3, 6, 7, 8);
