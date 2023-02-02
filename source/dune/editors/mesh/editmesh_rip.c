#include "MEM_guardedalloc.h"

#include "DNA_object_types.h"

#include "BLI_array.h"
#include "BLI_math.h"

#include "BKE_context.h"
#include "BKE_editmesh.h"
#include "BKE_layer.h"
#include "BKE_report.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_types.h"

#include "ED_mesh.h"
#include "ED_screen.h"
#include "ED_transform.h"
#include "ED_view3d.h"

#include "bmesh.h"
#include "bmesh_tools.h"

#include "mesh_intern.h" /* own include */

/**
 * helper to find edge for edge_rip,
 *
 * \param inset: is used so we get some useful distance
 * when comparing multiple edges that meet at the same
 * point and would result in the same distance.
 */
#define INSET_DEFAULT 0.00001f
static float edbm_rip_edgedist_squared(ARegion *region,
                                       float mat[4][4],
                                       const float co1[3],
                                       const float co2[3],
                                       const float mvalf[2],
                                       const float inset)
{
  float vec1[2], vec2[2], dist_sq;

  ED_view3d_project_float_v2_m4(region, co1, vec1, mat);
  ED_view3d_project_float_v2_m4(region, co2, vec2, mat);

  if (inset != 0.0f) {
    const float dist_2d = len_v2v2(vec1, vec2);
    if (dist_2d > FLT_EPSILON) {
      const float dist = inset / dist_2d;
      BLI_assert(isfinite(dist));
      interp_v2_v2v2(vec1, vec1, vec2, dist);
      interp_v2_v2v2(vec2, vec2, vec1, dist);
    }
  }

  dist_sq = dist_squared_to_line_segment_v2(mvalf, vec1, vec2);
  BLI_assert(isfinite(dist_sq));

  return dist_sq;
}

#if 0
static float edbm_rip_linedist(
    ARegion *region, float mat[4][4], const float co1[3], const float co2[3], const float mvalf[2])
{
  float vec1[2], vec2[2];

  ED_view3d_project_float_v2_m4(region, co1, vec1, mat);
  ED_view3d_project_float_v2_m4(region, co2, vec2, mat);

  return dist_to_line_v2(mvalf, vec1, vec2);
}
#endif

/**
 * Calculates a point along the loop tangent which can be used to measure against edges.
 */
static void edbm_calc_loop_co(BMLoop *l, float l_mid_co[3])
{
  BM_loop_calc_face_tangent(l, l_mid_co);

  /* scale to average of surrounding edge size, only needs to be approx, but should
   * be roughly equivalent to the check below which uses the middle of the edge. */
  mul_v3_fl(l_mid_co, (BM_edge_calc_length(l->e) + BM_edge_calc_length(l->prev->e)) / 2.0f);

  add_v3_v3(l_mid_co, l->v->co);
}

static float edbm_rip_edge_side_measure(
    BMEdge *e, BMLoop *e_l, ARegion *region, float projectMat[4][4], const float fmval[2])
{
  float cent[3] = {0, 0, 0}, mid[3];

  float vec[2];
  float fmval_tweak[2];
  float e_v1_co[2], e_v2_co[2];
  float score;

  BMVert *v1_other;
  BMVert *v2_other;

  BLI_assert(BM_vert_in_edge(e, e_l->v));

  /* method for calculating distance:
   *
   * for each edge: calculate face center, then made a vector
   * from edge midpoint to face center.  offset edge midpoint
   * by a small amount along this vector. */

  /* rather than the face center, get the middle of
   * both edge verts connected to this one */
  v1_other = BM_face_other_vert_loop(e_l->f, e->v2, e->v1)->v;
  v2_other = BM_face_other_vert_loop(e_l->f, e->v1, e->v2)->v;
  mid_v3_v3v3(cent, v1_other->co, v2_other->co);
  mid_v3_v3v3(mid, e->v1->co, e->v2->co);

  ED_view3d_project_float_v2_m4(region, cent, cent, projectMat);
  ED_view3d_project_float_v2_m4(region, mid, mid, projectMat);

  ED_view3d_project_float_v2_m4(region, e->v1->co, e_v1_co, projectMat);
  ED_view3d_project_float_v2_m4(region, e->v2->co, e_v2_co, projectMat);

  sub_v2_v2v2(vec, cent, mid);
  normalize_v2_length(vec, 0.01f);

  /* rather than adding to both verts, subtract from the mouse */
  sub_v2_v2v2(fmval_tweak, fmval, vec);

  score = len_v2v2(e_v1_co, e_v2_co);

  if (dist_squared_to_line_segment_v2(fmval_tweak, e_v1_co, e_v2_co) >
      dist_squared_to_line_segment_v2(fmval, e_v1_co, e_v2_co)) {
    return score;
  }
  return -score;
}

/* - Advanced selection handling 'ripsel' functions ----- */

/**
 * How rip selection works
 *
 * Firstly - rip is basically edge split with side-selection & grab.
 * Things would be much more simple if we didn't have to worry about side selection
 *
 * The method used for checking the side of selection is as follows...
 * - First tag all rip-able edges.
 * - Build a contiguous edge list by looping over tagged edges and following each one's tagged
 *   siblings in both directions.
 *   - The loops are not stored in an array. Instead both loops on either side of each edge has
 *     its index values set to count down from the last edge. This way once we have the 'last'
 *     edge it's very easy to walk down the connected edge loops.
 *     The reason for using loops like this is because when the edges are split we don't know
 *     which face user gets the newly created edge
 *     (it's as good as random so we can't assume new edges will be on one side).
 *     After splitting, it's very simple to walk along boundary loops since each only has one edge
 *     from a single side.
 * - The end loop pairs are stored in an array however to support multiple edge-selection-islands,
 *   so you can rip multiple selections at once.
 * - * Execute the split *
 * - For each #EdgeLoopPair walk down both sides of the split using the loops and measure
 *   which is facing the mouse.
 * - Deselect the edge loop facing away.
 *
 * Limitation!
 * This currently works very poorly with intersecting edge islands
 * (verts with more than 2 tagged edges). This is nice to do but for now not essential.
 *
 * - campbell.
 */

#define IS_VISIT_POSSIBLE(e) (BM_edge_is_manifold(e) && BM_elem_flag_test(e, BM_ELEM_TAG))
#define IS_VISIT_DONE(e) ((e)->l && (BM_elem_index_get((e)->l) != INVALID_UID))
#define INVALID_UID INT_MIN
