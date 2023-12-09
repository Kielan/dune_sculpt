/* BIF_meshlaplacian.h: Algorithms using the mesh laplacian. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//#define RIGID_DEFORM

struct Mesh;
struct Ob;
struct DeformGroup;

#ifdef RIGID_DEFORM
struct EditMesh;
#endif

/* Laplacian Sys */
struct LaplacianSys;
typedef struct LaplacianSys LaplacianSys;

void laplacian_add_vertex(LaplacianSys *sys, float *co, int pinned);
void laplacian_add_triangle(LaplacianSys *sys, int v1, int v2, int v3);

void laplacian_begin_solve(LaplacianSys *sys, int index);
void laplacian_add_right_hand_side(LaplacianSys *sys, int v, float value);
int laplacian_sys_solve(LaplacianSys *sys);
float laplacian_sys_get_solution(LaplacianSys *sys, int v);

/* Heat Weighting */
void heat_bone_weighting(struct Ob *ob,
                         struct Mesh *mesh,
                         float (*verts)[3],
                         int numbones,
                         struct DeformGroup **dgrouplist,
                         struct DeformGroup **dgroupflip,
                         float (*root)[3],
                         float (*tip)[3],
                         const int *sel,
                         const char **error_str);

#ifdef RIGID_DEFORM
/* As-Rigid-As-Possible Deformation */

void rigid_deform_begin(struct EditMesh *em);
void rigid_deform_iteration(void);
void rigid_deform_end(int cancel);
#endif

#ifdef __cplusplus
}
#endif

/* Harmonic Coordinates */

/* ED_mesh_deform_bind_callback(...) defined in ED_armature.hh */
