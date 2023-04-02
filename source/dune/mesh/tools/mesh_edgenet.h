#pragma once

/**
 * Fill in faces from an edgenet made up of boundary and wire edges.
 *
 * New faces currently don't have their normals calculated and are flipped randomly.
 *       The caller needs to flip faces correctly.
 *
 * param mesh: The mesh to operate on.
 * param use_edge_tag: Only fill tagged edges.
 */
void mesh_edgenet(Mesh *mesh, bool use_edge_tag, bool use_new_face_tag);
