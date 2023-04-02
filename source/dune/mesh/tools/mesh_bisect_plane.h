#pragma once

/**
 * param use_snap_center: Snap verts onto the plane.
 * param use_tag: Only bisect tagged edges and faces.
 * param oflag_center: Operator flag, enabled for geometry on the axis (existing and created)
 */
void mesh_bisect_plane(Mesh *bm,
                       const float plane[4],
                       bool use_snap_center,
                       bool use_tag,
                       short oflag_center,
                       short oflag_new,
                       float eps);
