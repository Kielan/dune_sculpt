#pragma once

/**
 * param use_snap_center: Snap verts onto the plane.
 * param use_tag: Only bisect tagged edges and faces.
 * param opflag_center: Operator flag, enabled for geometry on the axis (existing and created)
 */
void mesh_bisect_plane(Mesh *mesh,
                       const float plane[4],
                       bool use_snap_center,
                       bool use_tag,
                       short opflag_center,
                       short opflag_new,
                       float eps);
