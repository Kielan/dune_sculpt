#pragma once

bool mesh_intersect_edges(
    Mesh *bm, char hflag, float dist, bool split_faces, GHash *r_targetmap);
