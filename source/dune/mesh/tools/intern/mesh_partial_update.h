#pragma once

#include "lib_compiler_attrs.h"

/** Parameters used to determine which kinds of data needs to be generated. **/
typedef struct MeshPartialUpdateParams {
  bool do_normals;
  bool do_tessellate;
} MeshPartialUpdateParams;

/**
 * Cached data to speed up partial updates.
 *
 * Hints:
 *
 * - Avoid creating this data for single updates,
 *   it should be created and reused across multiple updates to gain a significant benefit
 *   (while transforming geometry for example).
 *
 * - Partial normal updates use face & loop indices,
 *   setting them to dirty values between updates will slow down normal recalculation.
 */
typedef struct MeshPartialUpdate {
  MeshVert **verts;
  MeshFace **faces;
  int verts_len, verts_len_alloc;
  int faces_len, faces_len_alloc;

  /** Store the parameters used in creation so invalid use can be asserted. */
  MeshPartialUpdateParams params;
} MeshPartialUpdate;

/**
 * All Tagged & Connected, see: mesh_partial_create_from_verts
 * Operate on everything that's tagged as well as connected geometry.
 */
MeshPartialUpdate *mesh_partial_create_from_verts(Mesh *mesh,
                                                  const MeshPartialUpdate_Params *params,
                                                  const unsigned int *verts_mask,
                                                  int verts_mask_count)
    ATTR_NONNULL(1, 2, 3) ATTR_WARN_UNUSED_RESULT;

/**
 * All Connected, operate on all faces that have both tagged and un-tagged vertices.
 *
 * Reduces computations when transforming isolated regions.
 */
MeshPartialUpdate *mesh_mesh_partial_create_from_verts_group_single(
    Mesh *mesh,
    const MeshPartialUpdateParams *params,
    const unsigned int *verts_mask,
    int verts_mask_count) ATTR_NONNULL(1, 2, 3) ATTR_WARN_UNUSED_RESULT;

/**
 * All Connected, operate on all faces that have vertices in the same group.
 *
 * Reduces computations when transforming isolated regions.
 *
 * This is a version of mesh_partial_create_from_verts_group_single
 * that handles multiple groups instead of a bitmap mask.
 *
 * This is needed for example when transform has mirror enabled,
 * since one side needs to have a different group to the other since a face that has vertices
 * attached to both won't have an affine transformation.
 *
 * param verts_group: Vertex aligned array of groups.
 * Values are used as follows:
 * - >0: Each face is grouped with other faces of the same group.
 * -  0: Not in a group (don't handle these).
 * - -1: Don't use grouping logic (include any face that contains a vertex with this group).
 * param verts_group_count: The number of non-zero values in `verts_groups`.
 */
MeshPartialUpdate *mesh_partial_create_from_verts_group_multi(
    Mesh *mesh, const MeshPartialUpdateParams *params, const int *verts_group, int verts_group_count)
    ATTR_NONNULL(1, 2, 3) ATTR_WARN_UNUSED_RESULT;

void mesh_partial_destroy(MeshPartialUpdate *meshinfo) ATTR_NONNULL(1);
