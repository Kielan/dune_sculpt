/* Util fns for merging geometry once transform has finished:
 * - edm_automerge
 * - edm_automerge_and_split */

#include "dune_editmesh.h"

#include "types_object.h"

#include "ed_mesh.h"

#include "tools/mesh_intersect_edges.h"

//#define DEBUG_TIME
#ifdef DEBUG_TIME
#  include "PIL_time.h"
#endif

/* use mesh op flags for a few ops */
#define MO_ELE_TAG 1

/* Auto-Merge Selection
 *
 * Used after transform ops. */

void mesh_automerge(Object *obedit, bool update, const char hflag, const float dist)
{
  MeshEdit *me = dune_meshedit_from_object(obedit);
  Mesh *mesh = em->mesh;
  int totvert_prev = mesh->totvert;

  MOp findop, weldop;

  /* Search for doubles among all vertices, but only merge non-VERT_KEEP
   * vertices into VERT_KEEP vertices. */
  mo_op_initf(mesh,
              &findop,
              MO_FLAG_DEFAULTS,
              "find_doubles verts=%av keep_verts=%Hv dist=%f",
              hflag,
              dist);

  mo_op_ex(mesh, &findop);

  /* weld the vertices */
  meshop_op_init(mesh, &weldop, MO_FLAG_DEFAULTS, "weld_verts");
  meshop_slot_copy(&findop, slots_out, "targetmap.out", &weldop, slots_in, "targetmap");
  meshop_op_ex(mesh, &weldop);

  meshop_op_finish(mesh, &findop);
  meshop_op_finish(mesh, &weldop);

  if ((totvert_prev != mesh->totvert) && update) {
    editmesh_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }
}

/* Auto-Merge & Split Selection
 * Used after transform ops. */
void mesh_automerge_and_split(Object *obedit,
                              const bool UNUSED(split_edges),
                              const bool split_faces,
                              const bool update,
                              const char hflag,
                              const float dist)
{
  bool ok = false;

  MeshEdit *mesh = dune_meshedit_from_object(obedit);
  Mesh *mesh = em->mesh;

#ifdef DEBUG_TIME
  em->mesh = mesh_copy(mesh);

  double t1 = PIL_check_seconds_timer();
  EDBM_automerge(obedit, false, hflag, dist);
  t1 = PIL_check_seconds_timer() - t1;

  Mesh_mesh_free(em->mesh);
  em->mesh = mesh;
  double t2 = PIL_check_seconds_timer();
#endif

  MeshOp weldop;
  MeshOpSlot *slot_targetmap;

  MeshOp_op_init(mesh, &weldop, MO_FLAG_DEFAULTS, "weld_verts");
  slot_targetmap = MeshOp_slot_get(weldop.slots_in, "targetmap");

  GHash *ghash_targetmap = MO_SLOT_AS_GHASH(slot_targetmap);

  ok = mesh_intersect_edges(bm, hflag, dist, split_faces, ghash_targetmap);

  if (ok) {
    MeshOp_op_ex(mesh, &weldop);
  }

  MeshOp_op_finish(dm, &weldop);

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
