#pragma once

/**
 * Check of this Mesh is valid,
 * this function can be slow bc its intended to help with debugging.
 *
 * return true when the mesh is valid.
 */
bool mesh_validate(Mesh *mesh);
