#pragma once

void mesh_op_delete_oflag_tagged(Mesh *mesh, short opflag, char htype);
void mesh_delete_hflag_tagged(Mesh *mesh, char hflag, char htype);

/**
 * warning opflag applies to different types in some contexts,
 * not just the type being removed.
 */
void mesh_op_mesh_delete_opflag_ctx(Mesh *mesh, short opflag, int type);
/**
 * warning oflag applies to different types in some contexts,
 * not just the type being removed.
 */
void mesh_delete_hflag_ctx(Mesh *mesh, char hflag, int type);
