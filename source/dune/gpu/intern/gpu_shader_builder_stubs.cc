/* Stubs to reduce linking time for shader_builder. */

#include "lib_utildefines.h"

#include "imbuf.h"
#include "imbuf_types.h"

#include "dune_customdata.h"
#include "dune_global.h"
#include "dune_material.h"
#include "dune_mesh.h"
#include "dune_node.h"
#include "dune_paint.h"
#include "dune_pbvh.h"
#include "dune_subdiv_ccg.h"

#include "types_userdef.h"

#include "NOD_shader.h"

#include "draw_engine.h"

#include "bmesh.h"

#include "ui_resources.h"

extern "C" {

Global G;
UserDef U;

/* Stubs of lib_imbuf_types.h **/
void imbuf_freeImBuf(ImBuf *UNUSED(ibuf))
{
  lib_assert_unreachable();
}

/* Stubs of ui_resources.h */
void ui_GetThemeColor4fv(int UNUSED(colorid), float UNUSED(col[4]))
{
  lib_assert_unreachable();
}

void ui_GetThemeColor3fv(int UNUSED(colorid), float UNUSED(col[3]))
{
  lib_assert_unreachable();
}

void ui_GetThemeColorShade4fv(int UNUSED(colorid), int UNUSED(offset), float UNUSED(col[4]))
{
  lib_assert_unreachable();
}

void ui_GetThemeColorShadeAlpha4fv(int UNUSED(colorid),
                                   int UNUSED(coloffset),
                                   int UNUSED(alphaoffset),
                                   float UNUSED(col[4]))
{
  lib_assert_unreachable();
}
void ui_GetThemeColorBlendShade4fv(int UNUSED(colorid1),
                                   int UNUSED(colorid2),
                                   float UNUSED(fac),
                                   int UNUSED(offset),
                                   float UNUSED(col[4]))
{
  lib_assert_unreachable();
}

void ui_GetThemeColorBlend3ubv(int UNUSED(colorid1),
                               int UNUSED(colorid2),
                               float UNUSED(fac),
                               unsigned char UNUSED(col[3]))
{
  lib_assert_unreachable();
}

void ui_GetThemeColorShadeAlpha4ubv(int UNUSED(colorid),
                                    int UNUSED(coloffset),
                                    int UNUSED(alphaoffset),
                                    unsigned char UNUSED(col[4]))
{
  gpu_assert_unreachable();
}

/* Stubs of dune_paint.h */
bool paint_is_face_hidden(const struct MLoopTri *UNUSED(lt),
                          const struct MVert *UNUSED(mvert),
                          const struct MLoop *UNUSED(mloop))
{
  lib_assert_unreachable();
  return false;
}

void dune_paint_face_set_overlay_color_get(const int UNUSED(face_set),
                                          const int UNUSED(seed),
                                          uchar UNUSED(r_color[4]))
{
  lib_assert_unreachable();
}

bool paint_is_grid_face_hidden(const unsigned int *UNUSED(grid_hidden),
                               int UNUSED(gridsize),
                               int UNUSED(x),
                               int UNUSED(y))
{
  lib_assert_unreachable();
  return false;
}

/* -------------------------------------------------------------------- */
/* Stubs of dune_mesh.h **/
void dune_mesh_calc_poly_normal(const struct MPoly *UNUSED(mpoly),
                               const struct MLoop *UNUSED(loopstart),
                               const struct MVert *UNUSED(mvarray),
                               float UNUSED(r_no[3]))
{
  lib_assert_unreachable();
}

void dune_mesh_looptri_get_real_edges(const struct Mesh *UNUSED(mesh),
                                     const struct MLoopTri *UNUSED(looptri),
                                     int UNUSED(r_edges[3]))
{
  lib_assert_unreachable();
}

/* Stubs of dune_material.h */
void dune_material_defaults_free_gpu()
{
  /* This function is reachable via gpu_exit. */
}

/* Stubs of dune_customdata.h */
int CustomData_get_offset(const struct CustomData *UNUSED(data), int UNUSED(type))
{
  lib_assert_unreachable();
  return 0;
}

/* Stubs of dune_pbvh.h **/
int dune_pbvh_count_grid_quads(lib_bitmap **UNUSED(grid_hidden),
                              const int *UNUSED(grid_indices),
                              int UNUSED(totgrid),
                              int UNUSED(gridsize))
{
  lib_assert_unreachable();
  return 0;
}

/* Stubs of dune_subdiv_ccg.h **/
int dune_subdiv_ccg_grid_to_face_index(const SubdivCCG *UNUSED(subdiv_ccg),
                                      const int UNUSED(grid_index))
{
  lib_assert_unreachable();
  return 0;
}

/* Stubs of dune_node.h **/
void ntreeGPUMaterialNodes(struct NodeTree *UNUSED(localtree),
                           struct GPUMaterial *UNUSED(mat),
                           bool *UNUSED(has_surface_output),
                           bool *UNUSED(has_volume_output))
{
  lib_assert_unreachable();
}

struct NodeTree *ntreeLocalize(struct NodeTree *UNUSED(ntree))
{
  lib_assert_unreachable();
  return nullptr;
}

void ntreeFreeLocalTree(struct NodeTree *UNUSED(ntree))
{
  lib_assert_unreachable();
}

/* Stubs of mesh.h */
void mesh_face_as_array_vert_tri(MFace *UNUSED(f), MVert *UNUSED(r_verts[3]))
{
  lib_assert_unreachable();
}

/* Stubs of draw_engine.h **/
void draw_deferred_shader_remove(struct GPUMaterial *UNUSED(mat))
{
  lib_assert_unreachable();
}
}
