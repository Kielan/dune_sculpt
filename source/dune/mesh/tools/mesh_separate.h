#pragma once

/** Split all faces that match `filter_fn`. **/
void mesh_separate_faces(Mesh *mesh, MeshFaceFilterFn filter_fn, void *user_data);
