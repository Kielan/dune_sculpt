/**
 * edmesh
 *
 * Utility functions for merging geometry once transform has finished:
 *
 * - #EDBM_automerge
 * - #EDBM_automerge_and_split
 */

#include "KERNEL_editmesh.h"

#include "TYPES_object.h"

#include "ED_mesh.h"

#include "tools/bmesh_intersect_edges.h"

//#define DEBUG_TIME
#ifdef DEBUG_TIME
#  include "PIL_time.h"
#endif

/* use bmesh operator flags for a few operators */
#define BMO_ELE_TAG 1

/* -------------------------------------------------------------------- */
/** Auto-Merge Selection
 *
 * Used after transform operations.
 **/

void EDBM_automerge(Object *obedit, bool update, const char hflag, const float dist)
{
  BMEditMesh *em = KERNEL_editmesh_from_object(obedit);
  BMesh *dm = em->dm;
  int totvert_prev = dm->totvert;

  BMOperator findop, weldop;

  /* Search for doubles among all vertices, but only merge non-VERT_KEEP
   * vertices into VERT_KEEP vertices. */
  BMO_op_initf(bm,
               &findop,
               BMO_FLAG_DEFAULTS,
               "find_doubles verts=%av keep_verts=%Hv dist=%f",
               hflag,
               dist);

  BMO_op_exec(bm, &findop);

  /* weld the vertices */
  BMO_op_init(bm, &weldop, BMO_FLAG_DEFAULTS, "weld_verts");
  BMO_slot_copy(&findop, slots_out, "targetmap.out", &weldop, slots_in, "targetmap");
  BMO_op_exec(bm, &weldop);

  BMO_op_finish(bm, &findop);
  BMO_op_finish(bm, &weldop);

  if ((totvert_prev != bm->totvert) && update) {
    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }
}

/* -------------------------------------------------------------------- */
/** Auto-Merge & Split Selection
 *
 * Used after transform operations.
 **/

void EDBM_automerge_and_split(Object *obedit,
                              const bool UNUSED(split_edges),
                              const bool split_faces,
                              const bool update,
                              const char hflag,
                              const float dist)
{
  bool ok = false;

  DuneMeshEdit *dme = KERNEL_editmesh_from_object(obedit);
  DuneMesh *dm = em->dm;

#ifdef DEBUG_TIME
  em->bm = DuneMesh_mesh_copy(dm);

  double t1 = PIL_check_seconds_timer();
  EDBM_automerge(obedit, false, hflag, dist);
  t1 = PIL_check_seconds_timer() - t1;

  DuneMesh_mesh_free(em->bm);
  em->dm = dm;
  double t2 = PIL_check_seconds_timer();
#endif

  DuneMeshOperator weldop;
  DuneMeshOpSlot *slot_targetmap;

  DuneMeshOperator_op_init(dm, &weldop, BMO_FLAG_DEFAULTS, "weld_verts");
  slot_targetmap = DuneMeshOperator_slot_get(weldop.slots_in, "targetmap");

  GHash *ghash_targetmap = BMO_SLOT_AS_GHASH(slot_targetmap);

  ok = DuneMesh_mesh_intersect_edges(bm, hflag, dist, split_faces, ghash_targetmap);

  if (ok) {
    DuneMeshOperator_op_exec(dm, &weldop);
  }

  DuneMeshOperator_op_finish(dm, &weldop);

#ifdef DEBUG_TIME
  t2 = PIL_check_seconds_timer() - t2;
  printf("t1: %lf; t2: %lf; fac: %lf\n", t1, t2, t1 / t2);
#endif

  if (LIKELY(ok) && update) {
    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }
}
