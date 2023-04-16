/**
 * mesh normal calculation functions.
 * mesh_normals.cc for the equivalent #Mesh functionality.
 */

#include "mem_guardedalloc.h"

#include "types_scene.h"

#include "lib_bitmap.h"
#include "lib_linklist_stack.h"
#include "lib_math.h"
#include "lib_stack.h"
#include "lib_task.h"
#include "lib_utildefines.h"

#include "dune_customdata.h"
#include "dune_editmesh.h"
#include "dune_global.h"
#include "dune_mesh.h"

#include "intern/mesh_private.h"

/* Smooth angle to use when tagging edges is disabled entirely. */
#define EDGE_TAG_FROM_SPLIT_ANGLE_BYPASS -FLT_MAX

static void mesh_edge_tag_from_smooth_and_set_sharp(const float (*fnos)[3],
                                                    MeshEdge *e,
                                                    const float split_angle_cos);
static void mesh_edge_tag_from_smooth(const float (*fnos)[3],
                                      MeshEdge *e,
                                      const float split_angle_cos);

/* -------------------------------------------------------------------- */
/** Update Vertex & Face Normals **/

/** Helpers for mesh_normals_update and mesh_verts_calc_normal_vcos */

/* We use that existing internal API flag,
 * assuming no other tool using it would run concurrently to clnors editing. */
#define MESH_LNORSPACE_UPDATE _FLAG_MF

typedef struct MeshVertsCalcNormalsWithCoordsData {
  /* Read-only data. */
  const float (*fnos)[3];
  const float (*vcos)[3];

  /* Write data. */
  float (*vnos)[3];
} MeshVertsCalcNormalsWithCoordsData;

LIB_INLINE void mesh_vert_calc_normals_accum_loop(const MeshLoop *l_iter,
                                                  const float e1diff[3],
                                                  const float e2diff[3],
                                                  const float f_no[3],
                                                  float v_no[3])
{
  /* Calculate the dot product of the two edges that meet at the loop's vertex. */
  /* Edge vectors are calculated from `e->v1` to `e->v2`, so adjust the dot product if one but not
   * both loops actually runs from `e->v2` to `e->v1`. */
  float dotprod = dot_v3v3(e1diff, e2diff);
  if ((l_iter->prev->e->v1 == l_iter->prev->v) ^ (l_iter->e->v1 == l_iter->v)) {
    dotprod = -dotprod;
  }
  const float fac = saacos(-dotprod);
  /* Shouldn't happen as normalizing edge-vectors cause degenerate values to be zeroed out. */
  lib_assert(!isnan(fac));
  madd_v3_v3fl(v_no, f_no, fac);
}

static void mesh_vert_calc_normals_impl(MeshVert *v)
{
  /* Note on redundant unit-length edge-vector calculation:
   *
   * This functions calculates unit-length edge-vector for every loop edge
   * in practice this means 2x `sqrt` calls per face-corner connected to each vertex.
   *
   * Previously (2.9x and older), the edge vectors were calculated and stored for reuse.
   * However the overhead of did not perform well (~16% slower - single & multi-threaded)
   * when compared with calculating the values as they are needed.
   *
   * For simple grid topologies this function calculates the edge-vectors 4x times.
   * There is some room for improved performance by storing the edge-vectors for reuse locally
   * in this function, reducing the number of redundant `sqrtf` in half (2x instead of 4x).
   * so face loops that share an edge would not calculate it multiple times.
   * From my tests the performance improvements are so small they're difficult to measure,
   * the time saved removing `sqrtf` calls is lost on storing and looking up the information,
   * even in the case of `lib_smallhash.h` & small inline lookup tables.
   *
   * Further, local data structures would need to support cases where
   * stack memory isn't sufficient - adding additional complexity for corner-cases
   * (a vertex that has thousands of connected edges for example).
   * Unless there are important use-cases that benefit from edge-vector caching,
   * keep this simple and calculate ~4x as many edge-vectors.
   *
   * In conclusion, the cost of caching & looking up edge-vectors both globally or per-vertex
   * doesn't save enough time to make it worthwhile.
   * - Campbell. */

  float *v_no = v->no;
  zero_v3(v_no);

  MeshEdge *e_first = v->e;
  if (e_first != NULL) {
    float e1diff[3], e2diff[3];
    MeshEdge *e_iter = e_first;
    do {
      MeshLoop *l_first = e_iter->l;
      if (l_first != NULL) {
        sub_v3_v3v3(e2diff, e_iter->v1->co, e_iter->v2->co);
        normalize_v3(e2diff);

        MeshLoop *l_iter = l_first;
        do {
          if (l_iter->v == v) {
            MeshEdge *e_prev = l_iter->prev->e;
            sub_v3_v3v3(e1diff, e_prev->v1->co, e_prev->v2->co);
            normalize_v3(e1diff);

            mesh_vert_calc_normals_accum_loop(l_iter, e1diff, e2diff, l_iter->f->no, v_no);
          }
        } while ((l_iter = l_iter->radial_next) != l_first);
      }
    } while ((e_iter = MESH_DISK_EDGE_NEXT(e_iter, v)) != e_first);

    if (LIKELY(normalize_v3(v_no) != 0.0f)) {
      return;
    }
  }
  /* Fallback normal. */
  normalize_v3_v3(v_no, v->co);
}

static void mesh_vert_calc_normals_cb(void *UNUSED(userdata),
                                      MempoolIterData *mp_v,
                                      const TaskParallelTLS *__restrict UNUSED(tls))
{
  MeshVert *v = (MeshVert *)mp_v;
  mesh_vert_calc_normals_impl(v);
}

static void mesh_vert_calc_normals_with_coords(MeehVert *v, MeshVertsCalcNormalsWithCoordsData *data)
{
  /* See mesh_vert_calc_normals_impl note on performance. */
  float *v_no = data->vnos[mesh_elem_index_get(v)];
  zero_v3(v_no);

  /* Loop over edges. */
  MeshEdge *e_first = v->e;
  if (e_first != NULL) {
    float e1diff[3], e2diff[3];
    MeshEdge *e_iter = e_first;
    do {
      MeshLoop *l_first = e_iter->l;
      if (l_first != NULL) {
        sub_v3_v3v3(e2diff,
                    data->vcos[mesh_elem_index_get(e_iter->v1)],
                    data->vcos[mesh_elem_index_get(e_iter->v2)]);
        normalize_v3(e2diff);

        MeshLoop *l_iter = l_first;
        do {
          if (l_iter->v == v) {
            MeshEdge *e_prev = l_iter->prev->e;
            sub_v3_v3v3(e1diff,
                        data->vcos[mesh_elem_index_get(e_prev->v1)],
                        data->vcos[mesh_elem_index_get(e_prev->v2)]);
            normalize_v3(e1diff);

            mesh_vert_calc_normals_accum_loop(
                l_iter, e1diff, e2diff, data->fnos[mesh_elem_index_get(l_iter->f)], v_no);
          }
        } while ((l_iter = l_iter->radial_next) != l_first);
      }
    } while ((e_iter = MESH_DISK_EDGE_NEXT(e_iter, v)) != e_first);

    if (LIKELY(normalize_v3(v_no) != 0.0f)) {
      return;
    }
  }
  /* Fallback normal. */
  normalize_v3_v3(v_no, data->vcos[mesh_elem_index_get(v)]);
}

static void mesh_vert_calc_normals_with_coords_cb(void *userdata,
                                                  MempoolIterData *mp_v,
                                                  const TaskParallelTLS *__restrict UNUSED(tls))
{
  MeshVertsCalcNormalsWithCoordsData *data = userdata;
  MeshVert *v = (MeshVert *)mp_v;
  mesh_vert_calc_normals_with_coords(v, data);
}

static void mesh_verts_calc_normals(Mesh *mesh,
                                       const float (*fnos)[3],
                                       const float (*vcos)[3],
                                       float (*vnos)[3])
{
  mesh_elem_index_ensure(mesh, MESH_FACE | ((vnos || vcos) ? MESH_VERT : 0));

  TaskParallelSettings settings;
  lib_parallel_mempool_settings_defaults(&settings);
  settings.use_threading = mesh->totvert >= MESH_OMP_LIMIT;

  if (vcos == NULL) {
    mesh_iter_parallel(mesh, MESH_VERTS_OF_MESH, mesh_vert_calc_normals_cb, NULL, &settings);
  }
  else {
    lib_assert(!ELEM(NULL, fnos, vnos));
    MeshVertsCalcNormalsWithCoordsData data = {
        .fnos = fnos,
        .vcos = vcos,
        .vnos = vnos,
    };
    mesh_iter_parallel(mesh, MESH_VERTS_OF_MESH, mesh_vert_calc_normals_with_coords_cb, &data, &settings);
  }
}

static void mesh_face_calc_normals_cb(void *UNUSED(userdata),
                                      MempoolIterData *mp_f,
                                      const TaskParallelTLS *__restrict UNUSED(tls))
{
  MeshFace *f = (MeshFace *)mp_f;

  mesh_face_calc_normal(f, f->no);
}

void mesh_normals_update_ex(Mesh *mesh, const struct MeshNormalsUpdateParams *params)
{
  if (params->face_normals) {
    /* Calculate all face normals. */
    TaskParallelSettings settings;
    lib_parallel_mempool_settings_defaults(&settings);
    settings.use_threading = mesh->totedge >= MESH_OMP_LIMIT;

    mesh_iter_parallel(mesh, MESH_FACES_OF_MESH, mesh_face_calc_normals_cb, NULL, &settings);
  }

  /* Add weighted face normals to vertices, and normalize vert normals. */
  mesh_verts_calc_normals(mesh, NULL, NULL, NULL);
}

void mesh_normals_update(Mesh *mesh)
{
  mesh_normals_update_ex(mesh,
                         &(const struct MeshNormalsUpdateParams){
                            .face_normals = true,
                         });
}

/* -------------------------------------------------------------------- */
/** Update Vertex & Face Normals (Partial Updates) */

static void mesh_partial_faces_parallel_range_calc_normals_cb(
    void *userdata, const int iter, const TaskParallelTLS *__restrict UNUSED(tls))
{
  MeshFace *f = ((MeshFace **)userdata)[iter];
  mesh_face_calc_normal(f, f->no);
}

static void mesh_partial_verts_parallel_range_calc_normal_cb(
    void *userdata, const int iter, const TaskParallelTLS *__restrict UNUSED(tls))
{
  MeshVert *v = ((MeshVert **)userdata)[iter];
  mesh_vert_calc_normals_impl(v);
}

void mesh_normals_update_with_partial_ex(Mesh *UNUSED(mesh),
                                         const MeshPartialUpdate *bmpinfo,
                                         const struct MeshNormalsUpdate_Params *params)
{
  lib_assert(bmpinfo->params.do_normals);
  /* While harmless, exit early if there is nothing to do. */
  if (UNLIKELY((bmpinfo->verts_len == 0) && (bmpinfo->faces_len == 0))) {
    return;
  }

  MeshVert **verts = bmpinfo->verts;
  MeshFace **faces = bmpinfo->faces;
  const int verts_len = bmpinfo->verts_len;
  const int faces_len = bmpinfo->faces_len;

  TaskParallelSettings settings;
  lib_parallel_range_settings_defaults(&settings);

  /* Faces. */
  if (params->face_normals) {
    lib_task_parallel_range(
        0, faces_len, faces, mesh_partial_faces_parallel_range_calc_normals_cb, &settings);
  }

  /* Verts. */
  lib_task_parallel_range(
      0, verts_len, verts, mesh_partial_verts_parallel_range_calc_normal_cb, &settings);
}

void mesh_normals_update_with_partial(Mesh *mesh, const BMPartialUpdate *bmpinfo)
{
  mesh_normals_update_with_partial_ex(mesh,
                                      bmpinfo,
                                      &(const struct MeshNormalsUpdateParams){
                                         .face_normals = true,
                                      });
}

/* -------------------------------------------------------------------- */
/** Update Vertex & Face Normals (Custom Coords) **/

void mesh_verts_calc_normal_vcos(Mesh *mesh,
                               const float (*fnos)[3],
                               const float (*vcos)[3],
                               float (*vnos)[3])
{
  /* Add weighted face normals to vertices, and normalize vert normals. */
  mesh_verts_calc_normals(mesh, fnos, vcos, vnos);
}

/* -------------------------------------------------------------------- */
/** Tagging Utility Functions **/

void mesh_normals_loops_edges_tag(Mesh *mesh, const bool do_edges)
{
  MeshFace *f;
  MeshEdge *e;
  MeshIter fiter, eiter;
  MeshLoop *l_curr, *l_first;

  if (do_edges) {
    int index_edge;
    MESH_INDEX_ITER (e, &eiter, mesh, MESH_EDGES_OF_MESH, index_edge) {
      MeshLoop *l_a, *l_b;

      mesh_elem_index_set(e, index_edge); /* set_inline */
      mesh_elem_flag_disable(e, MESH_ELEM_TAG);
      if (mesh_edge_loop_pair(e, &l_a, &l_b)) {
        if (mesh_elem_flag_test(e, MESH_ELEM_SMOOTH) && l_a->v != l_b->v) {
          mesh_elem_flag_enable(e, MESH_ELEM_TAG);
        }
      }
    }
    mesh->elem_index_dirty &= ~MESH_EDGE;
  }

  int index_face, index_loop = 0;
  MESH_INDEX_ITER (f, &fiter, mesh, MESH_FACES_OF_MESH, index_face) {
    mesh_elem_index_set(f, index_face); /* set_inline */
    l_curr = l_first = MESH_FACE_FIRST_LOOP(f);
    do {
      mesh_elem_index_set(l_curr, index_loop++); /* set_inline */
      mesh_elem_flag_disable(l_curr, MESH_ELEM_TAG);
    } while ((l_curr = l_curr->next) != l_first);
  }
  mesh->elem_index_dirty &= ~(MESH_FACE | MESH_LOOP);
}

/** Helpers for mesh_loop_normals_update and mesh_loops_calc_normal_vcos */
static void mesh_edges_sharp_tag(Mesh *mesh,
                                 const float (*fnos)[3],
                                 float split_angle_cos,
                                 const bool do_sharp_edges_tag)
{
  MeshIter eiter;
  MeshEdge *e;
  int i;

  if (fnos) {
    mesh_elem_index_ensure(mesh, MESH_FACE);
  }

  if (do_sharp_edges_tag) {
    MESH_INDEX_ITER (e, &eiter, mesh, MESH_EDGES_OF_MESH, i) {
      MESH_elem_index_set(e, i); /* set_inline */
      if (e->l != NULL) {
        mesh_edge_tag_from_smooth_and_set_sharp(fnos, e, split_angle_cos);
      }
    }
  }
  else {
    MESH_INDEX_ITER (e, &eiter, mesh, MESH_EDGES_OF_MESH, i) {
      mesh_elem_index_set(e, i); /* set_inline */
      if (e->l != NULL) {
        mesh_edge_tag_from_smooth(fnos, e, split_angle_cos);
      }
    }
  }

  mesh->elem_index_dirty &= ~MESH_EDGE;
}

void mesh_edges_sharp_from_angle_set(Mesh *mesh, const float split_angle)
{
  if (split_angle >= (float)M_PI) {
    /* Nothing to do! */
    return;
  }

  mesh_edges_sharp_tag(mesh, NULL, cosf(split_angle), true);
}

/* -------------------------------------------------------------------- */
/** Loop Normals Calculation API **/

bool mesh_loop_check_cyclic_smooth_fan(MeshLoop *l_curr)
{
  MeshLoop *lfan_pivot_next = l_curr;
  MeshEdge *e_next = l_curr->e;

  lib_assert(!mesh_elem_flag_test(lfan_pivot_next, MESH_ELEM_TAG));
  mesh_elem_flag_enable(lfan_pivot_next, MESH_ELEM_TAG);

  while (true) {
    /* Much simpler than in sibling code with basic Mesh data! */
    lfan_pivot_next = mesh_vert_step_fan_loop(lfan_pivot_next, &e_next);

    if (!lfan_pivot_next || !mesh_elem_flag_test(e_next, BM_ELEM_TAG)) {
      /* Sharp loop/edge, so not a cyclic smooth fan... */
      return false;
    }
    /* Smooth loop/edge... */
    if (mesh_elem_flag_test(lfan_pivot_next, BM_ELEM_TAG)) {
      if (lfan_pivot_next == l_curr) {
        /* We walked around a whole cyclic smooth fan
         * without finding any already-processed loop,
         * means we can use initial l_curr/l_prev edge as start for this smooth fan. */
        return true;
      }
      /* ... already checked in some previous looping, we can abort. */
      return false;
    }
    /* ... we can skip it in future, and keep checking the smooth fan. */
    mesh_elem_flag_enable(lfan_pivot_next, BM_ELEM_TAG);
  }
}

/**
 * Called for all faces loops.
 *
 * - All loops must have MESH_ELEM_TAG cleared.
 * - Loop indices must be valid.
 *
 * note When custom normals are present, the order of loops can be important.
 * Loops with lower indices must be passed before loops with higher indices (for each vertex).
 * This is needed since the first loop sets the reference point for the custom normal offsets.
 *
 * return The number of loops that were handled (for early exit when all have been handled).
 */
static int mesh_loops_calc_normals_for_loop(Mesh *mesh,
                                            const float (*vcos)[3],
                                            const float (*fnos)[3],
                                            const short (*clnors_data)[2],
                                            const int cd_loop_clnors_offset,
                                            const bool has_clnors,
                                            /* Cache. */
                                            lib_Stack *edge_vectors,
                                            /* Iterate. */
                                            MeshLoop *l_curr,
                                            /* Result. */
                                            float (*r_lnos)[3],
                                            MeshLoopNorSpaceArray *r_lnors_spacearr)
{
  lib_assert((mesh->elem_index_dirty & MESH_LOOP) == 0);
  lib_assert((fnos == NULL) || ((mesh->elem_index_dirty & BM_FACE) == 0));
  lib_assert((vcos == NULL) || ((mesh->elem_index_dirty & BM_VERT) == 0));
  UNUSED_VARS_NDEBUG(mesh);

  int handled = 0;

  /* Temp normal stack. */
  LIB_SMALLSTACK_DECLARE(normal, float *);
  /* Temp clnors stack. */
  LIB_SMALLSTACK_DECLARE(clnors, short *);
  /* Temp edge vectors stack, only used when computing lnor spacearr. */

  /* A smooth edge, we have to check for cyclic smooth fan case.
   * If we find a new, never-processed cyclic smooth fan, we can do it now using that loop/edge
   * as 'entry point', otherwise we can skip it. */

  /* NOTE: In theory, we could make mesh_loop_check_cyclic_smooth_fan() store
   * mlfan_pivot's in a stack, to avoid having to fan again around
   * the vert during actual computation of clnor & clnorspace. However, this would complicate
   * the code, add more memory usage, and
   * mesh_vert_step_fan_loop() is quite cheap in term of CPU cycles,
   * so really think it's not worth it. */
  if (mesh_elem_flag_test(l_curr->e, MESH_ELEM_TAG) &&
      (mesh_elem_flag_test(l_curr, MESH_ELEM_TAG) || !mesh_loop_check_cyclic_smooth_fan(l_curr))) {
  }
  else if (!mesh_elem_flag_test(l_curr->e, MESH_ELEM_TAG) &&
           !mesh_elem_flag_test(l_curr->prev->e, MESH_ELEM_TAG)) {
    /* Simple case (both edges around that vertex are sharp in related polygon),
     * this vertex just takes its poly normal.
     */
    const int l_curr_index = mesh_elem_index_get(l_curr);
    const float *no = fnos ? fnos[mesh_elem_index_get(l_curr->f)] : l_curr->f->no;
    copy_v3_v3(r_lnos[l_curr_index], no);

    /* If needed, generate this (simple!) lnor space. */
    if (r_lnors_spacearr) {
      float vec_curr[3], vec_prev[3];
      MeshLoopNorSpace *lnor_space = dune_lnor_space_create(r_lnors_spacearr);

      {
        const MeshVert *v_pivot = l_curr->v;
        const float *co_pivot = vcos ? vcos[mesh_elem_index_get(v_pivot)] : v_pivot->co;
        const MeshVert *v_1 = l_curr->next->v;
        const float *co_1 = vcos ? vcos[mesh_elem_index_get(v_1)] : v_1->co;
        const MeshVert *v_2 = l_curr->prev->v;
        const float *co_2 = vcos ? vcos[mesh_elem_index_get(v_2)] : v_2->co;

        lib_assert(v_1 == mesh_edge_other_vert(l_curr->e, v_pivot));
        lib_assert(v_2 == mesh_edge_other_vert(l_curr->prev->e, v_pivot));

        sub_v3_v3v3(vec_curr, co_1, co_pivot);
        normalize_v3(vec_curr);
        sub_v3_v3v3(vec_prev, co_2, co_pivot);
        normalize_v3(vec_prev);
      }

      mesh_lnor_space_define(lnor_space, r_lnos[l_curr_index], vec_curr, vec_prev, NULL);
      /* We know there is only one loop in this space,
       * no need to create a linklist in this case... */
      dune_lnor_space_add_loop(r_lnors_spacearr, lnor_space, l_curr_index, l_curr, true);

      if (has_clnors) {
        const short(*clnor)[2] = clnors_data ? &clnors_data[l_curr_index] :
                                               (const void *)MESH_ELEM_CD_GET_VOID_P(
                                                   l_curr, cd_loop_clnors_offset);
        dune_lnor_space_custom_data_to_normal(lnor_space, *clnor, r_lnos[l_curr_index]);
      }
    }
    handled = 1;
  }
  /* We *do not need* to check/tag loops as already computed!
   * Due to the fact a loop only links to one of its two edges,
   * a same fan *will never be walked more than once!*
   * Since we consider edges having neighbor faces with inverted (flipped) normals as sharp,
   * we are sure that no fan will be skipped, even only considering the case
   * (sharp curr_edge, smooth prev_edge), and not the alternative
   * (smooth curr_edge, sharp prev_edge).
   * All this due/thanks to link between normals and loop ordering.
   */
  else {
    /* We have to fan around current vertex, until we find the other non-smooth edge,
     * and accumulate face normals into the vertex!
     * Note in case this vertex has only one sharp edge,
     * this is a waste because the normal is the same as the vertex normal,
     * but I do not see any easy way to detect that (would need to count number of sharp edges
     * per vertex, I doubt the additional memory usage would be worth it, especially as it
     * should not be a common case in real-life meshes anyway).
     */
    MeshVert *v_pivot = l_curr->v;
    MeshEdge *e_next;
    const MeshEdge *e_org = l_curr->e;
    MeshLoop *lfan_pivot, *lfan_pivot_next;
    int lfan_pivot_index;
    float lnor[3] = {0.0f, 0.0f, 0.0f};
    float vec_curr[3], vec_next[3], vec_org[3];

    /* We validate clnors data on the fly - cheapest way to do! */
    int clnors_avg[2] = {0, 0};
    const short(*clnor_ref)[2] = NULL;
    int clnors_nbr = 0;
    bool clnors_invalid = false;

    const float *co_pivot = vcos ? vcos[mesh_elem_index_get(v_pivot)] : v_pivot->co;

    MeshLoopNorSpace *lnor_space = r_lnors_spacearr ? dune_lnor_space_create(r_lnors_spacearr) : NULL;

    lib_assert((edge_vectors == NULL) || lib_stack_is_empty(edge_vectors));

    lfan_pivot = l_curr;
    lfan_pivot_index = mesh_elem_index_get(lfan_pivot);
    e_next = lfan_pivot->e; /* Current edge here, actually! */

    /* Only need to compute previous edge's vector once,
     * then we can just reuse old current one! */
    {
      const MeshVert *v_2 = lfan_pivot->next->v;
      const float *co_2 = vcos ? vcos[mesh_elem_index_get(v_2)] : v_2->co;

      lib_assert(v_2 == mesh_edge_other_vert(e_next, v_pivot));

      sub_v3_v3v3(vec_org, co_2, co_pivot);
      normalize_v3(vec_org);
      copy_v3_v3(vec_curr, vec_org);

      if (r_lnors_spacearr) {
        lib_stack_push(edge_vectors, vec_org);
      }
    }

    while (true) {
      /* Much simpler than in sibling code with basic Mesh data! */
      lfan_pivot_next = mesh_vert_step_fan_loop(lfan_pivot, &e_next);
      if (lfan_pivot_next) {
        lib_assert(lfan_pivot_next->v == v_pivot);
      }
      else {
        /* next edge is non-manifold, we have to find it ourselves! */
        e_next = (lfan_pivot->e == e_next) ? lfan_pivot->prev->e : lfan_pivot->e;
      }

      /* Compute edge vector.
       * NOTE: We could pre-compute those into an array, in the first iteration,
       * instead of computing them twice (or more) here.
       * However, time gained is not worth memory and time lost,
       * given the fact that this code should not be called that much in real-life meshes.
       */
      {
        const MeshVert *v_2 = mesh_edge_other_vert(e_next, v_pivot);
        const float *co_2 = vcos ? vcos[mesh_elem_index_get(v_2)] : v_2->co;

        sub_v3_v3v3(vec_next, co_2, co_pivot);
        normalize_v3(vec_next);
      }

      {
        /* Code similar to accumulate_vertex_normals_poly_v3. */
        /* Calculate angle between the two poly edges incident on this vertex. */
        const MeShFace *f = lfan_pivot->f;
        const float fac = saacos(dot_v3v3(vec_next, vec_curr));
        const float *no = fnos ? fnos[mesh_elem_index_get(f)] : f->no;
        /* Accumulate */
        madd_v3_v3fl(lnor, no, fac);

        if (has_clnors) {
          /* Accumulate all clnors, if they are not all equal we have to fix that! */
          const short(*clnor)[2] = clnors_data ? &clnors_data[lfan_pivot_index] :
                                                 (const void *)BM_ELEM_CD_GET_VOID_P(
                                                     lfan_pivot, cd_loop_clnors_offset);
          if (clnors_nbr) {
            clnors_invalid |= ((*clnor_ref)[0] != (*clnor)[0] || (*clnor_ref)[1] != (*clnor)[1]);
          }
          else {
            clnor_ref = clnor;
          }
          clnors_avg[0] += (*clnor)[0];
          clnors_avg[1] += (*clnor)[1];
          clnors_nbr++;
          /* We store here a pointer to all custom lnors processed. */
          LIB_SMALLSTACK_PUSH(clnors, (short *)*clnor);
        }
      }

      /* We store here a pointer to all loop-normals processed. */
      LIB_SMALLSTACK_PUSH(normal, (float *)r_lnos[lfan_pivot_index]);

      if (r_lnors_spacearr) {
        /* Assign current lnor space to current 'vertex' loop. */
        dune_lnor_space_add_loop(r_lnors_spacearr, lnor_space, lfan_pivot_index, lfan_pivot, false);
        if (e_next != e_org) {
          /* We store here all edges-normalized vectors processed. */
          lib_stack_push(edge_vectors, vec_next);
        }
      }

      handled += 1;

      if (!mesh_elem_flag_test(e_next, MESH_ELEM_TAG) || (e_next == e_org)) {
        /* Next edge is sharp, we have finished with this fan of faces around this vert! */
        break;
      }

      /* Copy next edge vector to current one. */
      copy_v3_v3(vec_curr, vec_next);
      /* Next pivot loop to current one. */
      lfan_pivot = lfan_pivot_next;
      lfan_pivot_index = mesh_elem_index_get(lfan_pivot);
    }

    {
      float lnor_len = normalize_v3(lnor);

      /* If we are generating lnor spacearr, we can now define the one for this fan. */
      if (r_lnors_spacearr) {
        if (UNLIKELY(lnor_len == 0.0f)) {
          /* Use vertex normal as fallback! */
          copy_v3_v3(lnor, r_lnos[lfan_pivot_index]);
          lnor_len = 1.0f;
        }

        dune_lnor_space_define(lnor_space, lnor, vec_org, vec_next, edge_vectors);

        if (has_clnors) {
          if (clnors_invalid) {
            short *clnor;

            clnors_avg[0] /= clnors_nbr;
            clnors_avg[1] /= clnors_nbr;
            /* Fix/update all clnors of this fan with computed average value. */

            /* Prints continuously when merge custom normals, so commenting. */
            // printf("Invalid clnors in this fan!\n");

            while ((clnor = LIB_SMALLSTACK_POP(clnors))) {
              // print_v2("org clnor", clnor);
              clnor[0] = (short)clnors_avg[0];
              clnor[1] = (short)clnors_avg[1];
            }
            // print_v2("new clnors", clnors_avg);
          }
          else {
            /* We still have to consume the stack! */
            while (LIB_SMALLSTACK_POP(clnors)) {
              /* pass */
            }
          }
          dune_lnor_space_custom_data_to_normal(lnor_space, *clnor_ref, lnor);
        }
      }

      /* In case we get a zero normal here, just use vertex normal already set! */
      if (LIKELY(lnor_len != 0.0f)) {
        /* Copy back the final computed normal into all related loop-normals. */
        float *nor;

        while ((nor = LIB_SMALLSTACK_POP(normal))) {
          copy_v3_v3(nor, lnor);
        }
      }
      else {
        /* We still have to consume the stack! */
        while (LIB_SMALLSTACK_POP(normal)) {
          /* pass */
        }
      }
    }

    /* Tag related vertex as sharp, to avoid fanning around it again
     * (in case it was a smooth one). */
    if (r_lnors_spacearr) {
      mesh_elem_flag_enable(l_curr->v, MESH_ELEM_TAG);
    }
  }
  return handled;
}

static int mesh_loop_index_cmp(const void *a, const void *b)
{
  lib_assert(mesh_elem_index_get((MeshLoop *)a) != mesh_elem_index_get((MeshLoop *)b));
  if (lib_elem_index_get((MeshLoop *)a) < mesh_elem_index_get((MeshLoop *)b)) {
    return -1;
  }
  return 1;
}

/**
 * We only tag edges that are *really* smooth when the following conditions are met:
 * - The angle between both its polygons normals is below split_angle value.
 * - The edge is tagged as smooth.
 * - The faces of the edge are tagged as smooth.
 * - The faces of the edge have compatible (non-flipped) topological normal (winding),
 *   i.e. both loops on the same edge do not share the same vertex.
 */
LIB_INLINE bool mesh_edge_is_smooth_no_angle_test(const MeshEdge *e,
                                                const MeshLoop *l_a,
                                                const MeshLoop *l_b)
{
  lib_assert(l_a->radial_next == l_b);
  return (
      /* The face is manifold. */
      (l_b->radial_next == l_a) &&
      /* Faces have winding that faces the same way. */
      (l_a->v != l_b->v) &&
      /* The edge is smooth. */
      mesh_elem_flag_test(e, MESH_ELEM_SMOOTH) &&
      /* Both faces are smooth. */
      mesh_elem_flag_test(l_a->f, MESH_ELEM_SMOOTH) && mesh_elem_flag_test(l_b->f, MESH_ELEM_SMOOTH));
}

static void mesh_edge_tag_from_smooth(const float (*fnos)[3], MeshEdge *e, const float split_angle_cos)
{
  lib_assert(e->l != NULL);
  MeshLoop *l_a = e->l, *l_b = l_a->radial_next;
  bool is_smooth = false;
  if (mesh_edge_is_smooth_no_angle_test(e, l_a, l_b)) {
    if (split_angle_cos != -1.0f) {
      const float dot = (fnos == NULL) ? dot_v3v3(l_a->f->no, l_b->f->no) :
                                         dot_v3v3(fnos[mesh_elem_index_get(l_a->f)],
                                                  fnos[mesh_elem_index_get(l_b->f)]);
      if (dot >= split_angle_cos) {
        is_smooth = true;
      }
    }
    else {
      is_smooth = true;
    }
  }

  /* Perform `mesh_elem_flag_set(e, MESH_ELEM_TAG, is_smooth)`
   * NOTE: This will be set by multiple threads however it will be set to the same value. */

  /* No need for atomics here as this is a single byte. */
  char *hflag_p = &e->head.hflag;
  if (is_smooth) {
    *hflag_p = *hflag_p | MESH_ELEM_TAG;
  }
  else {
    *hflag_p = *hflag_p & ~MESH_ELEM_TAG;
  }
}

/**
 * A version of mesh_edge_tag_from_smooth that sets sharp edges
 * when they would be considered smooth but exceed the split angle .
 *
 * This doesn't have the same atomic requirement as #bm_edge_tag_from_smooth
 * since it isn't run from multiple threads at once.
 */
static void mesh_edge_tag_from_smooth_and_set_sharp(const float (*fnos)[3],
                                                  BMEdge *e,
                                                  const float split_angle_cos)
{
  lib_assert(e->l != NULL);
  MeshLoop *l_a = e->l, *l_b = l_a->radial_next;
  bool is_smooth = false;
  if (mesh_edge_is_smooth_no_angle_test(e, l_a, l_b)) {
    if (split_angle_cos != -1.0f) {
      const float dot = (fnos == NULL) ? dot_v3v3(l_a->f->no, l_b->f->no) :
                                         dot_v3v3(fnos[BM_elem_index_get(l_a->f)],
                                                  fnos[BM_elem_index_get(l_b->f)]);
      if (dot >= split_angle_cos) {
        is_smooth = true;
      }
      else {
        /* Note that we do not care about the other sharp-edge cases
         * (sharp poly, non-manifold edge, etc.),
         * only tag edge as sharp when it is due to angle threshold. */
        mesh_elem_flag_disable(e, MESH_ELEM_SMOOTH);
      }
    }
    else {
      is_smooth = true;
    }
  }

  mesh_elem_flag_set(e, MESH_ELEM_TAG, is_smooth);
}

/**
 * Operate on all vertices loops.
 * operating on vertices this is needed for multi-threading
 * so there is a guarantee that each thread has isolated loops.
 */
static void mesh_loops_calc_normals_for_vert_with_clnors(Mesh *mesh,
                                                         const float (*vcos)[3],
                                                         const float (*fnos)[3],
                                                         float (*r_lnos)[3],
                                                         const short (*clnors_data)[2],
                                                         const int cd_loop_clnors_offset,
                                                         const bool do_rebuild,
                                                         const float split_angle_cos,
                                                         /* TLS */
                                                         MeshLoopNorSpaceArray *r_lnors_spacearr,
                                                         lib_Stack *edge_vectors,
                                                         /* Iterate over. */
                                                         MeshVert *v)
{
  /* Respecting face order is necessary so the initial starting loop is consistent
   * with looping over loops of all faces.
   *
   * Logically we could sort the loops by their index & loop over them
   * however it's faster to use the lowest index of an un-ordered list
   * since it's common that smooth vertices only ever need to pick one loop
   * which then handles all the others.
   *
   * Sorting is only performed when multiple fans are found. */
  const bool has_clnors = true;
  LinkNode *loops_of_vert = NULL;
  int loops_of_vert_count = 0;
  /* When false the caller must have already tagged the edges. */
  const bool do_edge_tag = (split_angle_cos != EDGE_TAG_FROM_SPLIT_ANGLE_BYPASS);

  /* The loop with the lowest index. */
  {
    LinkNode *link_best;
    uint index_best = UINT_MAX;
    MeshEdge *e_curr_iter = v->e;
    do { /* Edges of vertex. */
      MeshLoop *l_curr = e_curr_iter->l;
      if (l_curr == NULL) {
        continue;
      }

      if (do_edge_tag) {
        mesh_edge_tag_from_smooth(fnos, e_curr_iter, split_angle_cos);
      }

      do { /* Radial loops. */
        if (l_curr->v != v) {
          continue;
        }
        if (do_rebuild && !MESH_ELEM_API_FLAG_TEST(l_curr, MESH_LNORSPACE_UPDATE) &&
            !(mesh->spacearr_dirty & MESH_SPACEARR_DIRTY_ALL)) {
          continue;
        }
        mesh_elem_flag_disable(l_curr, MESH_ELEM_TAG);
        lib_linklist_prepend_alloca(&loops_of_vert, l_curr);
        loops_of_vert_count += 1;

        const uint index_test = (uint)mesh_elem_index_get(l_curr);
        if (index_best > index_test) {
          index_best = index_test;
          link_best = loops_of_vert;
        }
      } while ((l_curr = l_curr->radial_next) != e_curr_iter->l);
    } while ((e_curr_iter = MESH_DISK_EDGE_NEXT(e_curr_iter, v)) != v->e);

    if (UNLIKELY(loops_of_vert == NULL)) {
      return;
    }

    /* Immediately pop the best element.
     * The order doesn't matter, so swap the links as it's simpler than tracking
     * reference to `link_best`. */
    if (link_best != loops_of_vert) {
      SWAP(void *, link_best->link, loops_of_vert->link);
    }
  }

  bool loops_of_vert_is_sorted = false;

  /* Keep track of the number of loops that have been assigned. */
  int loops_of_vert_handled = 0;

  while (loops_of_vert != NULL) {
    MeshLoop *l_best = loops_of_vert->link;
    loops_of_vert = loops_of_vert->next;

    lib_assert(l_best->v == v);
    loops_of_vert_handled += mesh_loops_calc_normals_for_loop(mesh,
                                                                 vcos,
                                                                 fnos,
                                                                 clnors_data,
                                                                 cd_loop_clnors_offset,
                                                                 has_clnors,
                                                                 edge_vectors,
                                                                 l_best,
                                                                 r_lnos,
                                                                 r_lnors_spacearr);

    /* Check if an early exit is possible without  an exhaustive inspection of every loop
     * where 1 loop's fan extends out to all remaining loops.
     * This is a common case for smooth vertices. */
    lib_assert(loops_of_vert_handled <= loops_of_vert_count);
    if (loops_of_vert_handled == loops_of_vert_count) {
      break;
    }

    /* Note on sorting, in some cases it will be faster to scan for the lowest index each time.
     * However in the worst case this is `O(N^2)`, so use a single sort call instead. */
    if (!loops_of_vert_is_sorted) {
      if (loops_of_vert && loops_of_vert->next) {
        loops_of_vert = lib_linklist_sort(loops_of_vert, bm_loop_index_cmp);
        loops_of_vert_is_sorted = true;
      }
    }
  }
}

/**
 * A simplified version of #bm_mesh_loops_calc_normals_for_vert_with_clnors
 * that can operate on loops in any order.
 */
static void mesh_loops_calc_normals_for_vert_without_clnors(
    Mesh *mesh,
    const float (*vcos)[3],
    const float (*fnos)[3],
    float (*r_lnos)[3],
    const bool do_rebuild,
    const float split_angle_cos,
    /* TLS */
    MeshLoopNorSpaceArray *r_lnors_spacearr,
    LibStack *edge_vectors,
    /* Iterate over. */
    MeshVert *v)
{
  const bool has_clnors = false;
  const short(*clnors_data)[2] = NULL;
  /* When false the caller must have already tagged the edges. */
  const bool do_edge_tag = (split_angle_cos != EDGE_TAG_FROM_SPLIT_ANGLE_BYPASS);
  const int cd_loop_clnors_offset = -1;

  MeshEdge *e_curr_iter;

  /* Unfortunately a loop is needed just to clear loop-tags. */
  e_curr_iter = v->e;
  do { /* Edges of vertex. */
    MeshLoop *l_curr = e_curr_iter->l;
    if (l_curr == NULL) {
      continue;
    }

    if (do_edge_tag) {
      mesh_edge_tag_from_smooth(fnos, e_curr_iter, split_angle_cos);
    }

    do { /* Radial loops. */
      if (l_curr->v != v) {
        continue;
      }
      mesh_elem_flag_disable(l_curr, MESH_ELEM_TAG);
    } while ((l_curr = l_curr->radial_next) != e_curr_iter->l);
  } while ((e_curr_iter = MESH_DISK_EDGE_NEXT(e_curr_iter, v)) != v->e);

  e_curr_iter = v->e;
  do { /* Edges of vertex. */
    MeshLoop *l_curr = e_curr_iter->l;
    if (l_curr == NULL) {
      continue;
    }
    do { /* Radial loops. */
      if (l_curr->v != v) {
        continue;
      }
      if (do_rebuild && !MESH_ELEM_API_FLAG_TEST(l_curr, MESH_LNORSPACE_UPDATE) &&
          !(mesh->spacearr_dirty & MESH_SPACEARR_DIRTY_ALL)) {
        continue;
      }
      mesh_loops_calc_normals_for_loop(mesh,
                                       vcos,
                                       fnos,
                                       clnors_data,
                                       cd_loop_clnors_offset,
                                       has_clnors,
                                       edge_vectors,
                                       l_curr,
                                       r_lnos,
                                       r_lnors_spacearr);
    } while ((l_curr = l_curr->radial_next) != e_curr_iter->l);
  } while ((e_curr_iter = MESH_DISK_EDGE_NEXT(e_curr_iter, v)) != v->e);
}

/**
 * Mesh version of dune_mesh_normals_loop_split() in `mesh_evaluate.cc`
 * Will use first clnors_data array, and fallback to cd_loop_clnors_offset
 * (use NULL and -1 to not use clnors).
 *
 * This sets MESH_ELEM_TAG which is used in tool code (e.g. T84426).
 * we could add a low-level API flag for this, see MESH_ELEM_API_FLAG_ENABLE and friends.
 */
static void mesh_loops_calc_normals__single_threaded(Mesh *mesh,
                                                     const float (*vcos)[3],
                                                     const float (*fnos)[3],
                                                     float (*r_lnos)[3],
                                                     MESHLoopNorSpaceArray *r_lnors_spacearr,
                                                     const short (*clnors_data)[2],
                                                     const int cd_loop_clnors_offset,
                                                     const bool do_rebuild,
                                                     const float split_angle_cos)
{
  MeshIter fiter;
  MeshFace *f_curr;
  const bool has_clnors = clnors_data || (cd_loop_clnors_offset != -1);
  /* When false the caller must have already tagged the edges. */
  const bool do_edge_tag = (split_angle_cos != EDGE_TAG_FROM_SPLIT_ANGLE_BYPASS);

  MeshLoopNorSpaceArray _lnors_spacearr = {NULL};

  lib_Stack *edge_vectors = NULL;

  {
    char htype = 0;
    if (vcos) {
      htype |= MESH_VERT;
    }
    /* Face/Loop indices are set inline below. */
    nmesh_elem_index_ensure(mesh, htype);
  }

  if (!r_lnors_spacearr && has_clnors) {
    /* We need to compute lnor spacearr if some custom lnor data are given to us! */
    r_lnors_spacearr = &_lnors_spacearr;
  }
  if (r_lnors_spacearr) {
    dune_lnor_spacearr_init(r_lnors_spacearr, bm->totloop, MLNOR_SPACEARR_BMLOOP_PTR);
    edge_vectors = lib_stack_new(sizeof(float[3]), __func__);
  }

  /* Clear all loops' tags (means none are to be skipped for now). */
  int index_face, index_loop = 0;
  MESH_INDEX_ITER (f_curr, &fiter, mesh, MESH_FACES_OF_MESH, index_face) {
    MeshLoop *l_curr, *l_first;

    mesh_elem_index_set(f_curr, index_face); /* set_inline */

    l_curr = l_first = MESH_FACE_FIRST_LOOP(f_curr);
    do {
      mesh_elem_index_set(l_curr, index_loop++); /* set_inline */
      mesh_elem_flag_disable(l_curr, MESH_ELEM_TAG);
    } while ((l_curr = l_curr->next) != l_first);
  }
  mesh->elem_index_dirty &= ~(MESH_FACE | MESH_LOOP);

  /* Always tag edges based on winding & sharp edge flag
   * (even when the auto-smooth angle doesn't need to be calculated). */
  if (do_edge_tag) {
    mesh_edges_sharp_tag(bm, fnos, has_clnors ? -1.0f : split_angle_cos, false);
  }

  /* We now know edges that can be smoothed (they are tagged),
   * and edges that will be hard (they aren't).
   * Now, time to generate the normals.
   */
  MESH_ITER (f_curr, &fiter, mesh, MESH_FACES_OF_MESH) {
    MeshLoop *l_curr, *l_first;

    l_curr = l_first = MESH_FACE_FIRST_LOOP(f_curr);
    do {
      if (do_rebuild && !MESH_ELEM_API_FLAG_TEST(l_curr, MESH_LNORSPACE_UPDATE) &&
          !(mesh->spacearr_dirty & MESH_SPACEARR_DIRTY_ALL)) {
        continue;
      }
      mesh_loops_calc_normals_for_loop(mesh,
                                       vcos,
                                       fnos,
                                       clnors_data,
                                       cd_loop_clnors_offset,
                                       has_clnors,
                                       edge_vectors,
                                       l_curr,
                                       r_lnos,
                                       r_lnors_spacearr);
    } while ((l_curr = l_curr->next) != l_first);
  }

  if (r_lnors_spacearr) {
    lib_stack_free(edge_vectors);
    if (r_lnors_spacearr == &_lnors_spacearr) {
      dune_lnor_spacearr_free(r_lnors_spacearr);
    }
  }
}

typedef struct MeshLoopsCalcNormalsWithCoordsData {
  /* Read-only data. */
  const float (*fnos)[3];
  const float (*vcos)[3];
  Mesh *mesh;
  const short (*clnors_data)[2];
  const int cd_loop_clnors_offset;
  const bool do_rebuild;
  const float split_angle_cos;

  /* Output. */
  float (*r_lnos)[3];
  MeshLoopNorSpaceArray *r_lnors_spacearr;
} MeshLoopsCalcNormalsWithCoordsData;

typedef struct MeshLoopsCalcNormalsWithCoords_TLS {
  LibStack *edge_vectors;

  /** Copied from MeshLoopsCalcNormalsWithCoordsData.r_lnors_spacearr when it's not NULL. */
  MeshLoopNorSpaceArray *lnors_spacearr;
  MeshLoopNorSpaceArray lnors_spacearr_buf;
} MeshLoopsCalcNormalsWithCoords_TLS;

static void mesh_loops_calc_normals_for_vert_init_fn(const void *__restrict userdata,
                                                        void *__restrict chunk)
{
  const MeshLoopsCalcNormalsWithCoordsData *data = userdata;
  MeshLoopsCalcNormalsWithCoords_TLS *tls_data = chunk;
  if (data->r_lnors_spacearr) {
    tls_data->edge_vectors = lib_stack_new(sizeof(float[3]), __func__);
    dune_lnor_spacearr_tls_init(data->r_lnors_spacearr, &tls_data->lnors_spacearr_buf);
    tls_data->lnors_spacearr = &tls_data->lnors_spacearr_buf;
  }
  else {
    tls_data->lnors_spacearr = NULL;
  }
}

static void mesh_loops_calc_normals_for_vert_reduce_fn(const void *__restrict userdata,
                                                       void *__restrict UNUSED(chunk_join),
                                                       void *__restrict chunk)
{
  const MeshLoopsCalcNormalsWithCoordsData *data = userdata;
  MeshLoopsCalcNormalsWithCoords_TLS *tls_data = chunk;

  if (data->r_lnors_spacearr) {
    dune_lnor_spacearr_tls_join(data->r_lnors_spacearr, tls_data->lnors_spacearr);
  }
}

static void dune_mesh_loops_calc_normals_for_vert_free_fn(const void *__restrict userdata,
                                                        void *__restrict chunk)
{
  const MeshLoopsCalcNormalsWithCoordsData *data = userdata;
  MeshLoopsCalcNormalsWithCoords_TLS *tls_data = chunk;

  if (data->r_lnors_spacearr) {
    lib_stack_free(tls_data->edge_vectors);
  }
}

static void mesh_loops_calc_normals_for_vert_with_clnors_fn(
    void *userdata, MempoolIterData *mp_v, const TaskParallelTLS *__restrict tls)
{
  MeshVert *v = (MeshVert *)mp_v;
  if (v->e == NULL) {
    return;
  }
  MeshLoopsCalcNormalsWithCoordsData *data = userdata;
  MeshLoopsCalcNormalsWithCoords_TLS *tls_data = tls->userdata_chunk;
  mesh_loops_calc_normals_for_vert_with_clnors(data->bm,
                                                  data->vcos,
                                                  data->fnos,
                                                  data->r_lnos,

                                                  data->clnors_data,
                                                  data->cd_loop_clnors_offset,
                                                  data->do_rebuild,
                                                  data->split_angle_cos,
                                                  /* Thread local. */
                                                  tls_data->lnors_spacearr,
                                                  tls_data->edge_vectors,
                                                  /* Iterate over. */
                                                  v);
}

static void mesh_loops_calc_normals_for_vert_without_clnors_fn(
    void *userdata, MempoolIterData *mp_v, const TaskParallelTLS *__restrict tls)
{
  MeshVert *v = (MeshVert *)mp_v;
  if (v->e == NULL) {
    return;
  }
  MeshLoopsCalcNormalsWithCoordsData *data = userdata;
  MeshLoopsCalcNormalsWithCoords_TLS *tls_data = tls->userdata_chunk;
  mesh_loops_calc_normals_for_vert_without_clnors(data->mesh,
                                                  data->vcos,
                                                  data->fnos,
                                                  data->r_lnos,

                                                  data->do_rebuild,
                                                  data->split_angle_cos,
                                                  /* Thread local. */
                                                  tls_data->lnors_spacearr,
                                                  tls_data->edge_vectors,
                                                  /* Iterate over. */
                                                  v);
}

static void mesh_loops_calc_normals__multi_threaded(Mesh *mesh,
                                                    const float (*vcos)[3],
                                                    const float (*fnos)[3],
                                                    float (*r_lnos)[3],
                                                    MeshLoopNorSpaceArray *r_lnors_spacearr,
                                                    const short (*clnors_data)[2],
                                                    Mesh const int cd_loop_clnors_offset,
                                                    const bool do_rebuild,
                                                    const float split_angle_cos)
{
  const bool has_clnors = clnors_data || (cd_loop_clnors_offset != -1);
  MeshLoopNorSpaceArray _lnors_spacearr = {NULL};

  {
    char htype = MESH_LOOP;
    if (vcos) {
      htype |= MESH_VERT;
    }
    if (fnos) {
      htype |= MESH_FACE;
    }
    /* Face/Loop indices are set inline below. */
    mesh_elem_index_ensure(mesh, htype);
  }

  if (!r_lnors_spacearr && has_clnors) {
    /* We need to compute lnor spacearr if some custom lnor data are given to us! */
    r_lnors_spacearr = &_lnors_spacearr;
  }
  if (r_lnors_spacearr) {
    dune_lnor_spacearr_init(r_lnors_spacearr, bm->totloop, MLNOR_SPACEARR_BMLOOP_PTR);
  }

  /* We now know edges that can be smoothed (they are tagged),
   * and edges that will be hard (they aren't).
   * Now, time to generate the normals.
   */

  TaskParallelSettings settings;
  lib_parallel_mempool_settings_defaults(&settings);

  MeshLoopsCalcNormalsWithCoords_TLS tls = {NULL};

  settings.userdata_chunk = &tls;
  settings.userdata_chunk_size = sizeof(tls);

  settings.func_init = mesh_loops_calc_normals_for_vert_init_fn;
  settings.func_reduce = mesh_loops_calc_normals_for_vert_reduce_fn;
  settings.func_free = mesh_loops_calc_normals_for_vert_free_fn;

  MeshLoopsCalcNormalsWithCoordsData data = {
      .mesh = mesh,
      .vcos = vcos,
      .fnos = fnos,
      .r_lnos = r_lnos,
      .r_lnors_spacearr = r_lnors_spacearr,
      .clnors_data = clnors_data,
      .cd_loop_clnors_offset = cd_loop_clnors_offset,
      .do_rebuild = do_rebuild,
      .split_angle_cos = split_angle_cos,
  };

  mesh_iter_parallel(mesh,
                     MESH_VERTS_OF_MESH,
                     has_clnors ? mesh_loops_calc_normals_for_vert_with_clnors_fn :
                                mesh_loops_calc_normals_for_vert_without_clnors_fn,
                   &data,
                   &settings);

  if (r_lnors_spacearr) {
    if (r_lnors_spacearr == &_lnors_spacearr) {
      dune_lnor_spacearr_free(r_lnors_spacearr);
    }
  }
}

static void mesh_loops_calc_normals(Mesh *mesh,
                                    const float (*vcos)[3],
                                    const float (*fnos)[3],
                                    float (*r_lnos)[3],
                                    MeshLoopNorSpaceArray *r_lnors_spacearr,
                                    const short (*clnors_data)[2],
                                    const int cd_loop_clnors_offset,
                                    const bool do_rebuild,
                                    const float split_angle_cos)
{
  if (mesh->totloop < MESH_OMP_LIMIT) {
    mesh_loops_calc_normals__single_threaded(mesh,
                                             vcos,
                                             fnos,
                                             r_lnos,
                                             r_lnors_spacearr,
                                             clnors_data,
                                             cd_loop_clnors_offset,
                                             do_rebuild,
                                             split_angle_cos);
  }
  else {
    mesh_loops_calc_normals__multi_threaded(mesh,
                                            vcos,
                                            fnos,
                                            r_lnos,
                                            r_lnors_spacearr,
                                            clnors_data,
                                            cd_loop_clnors_offset,
                                            do_rebuild,
                                            split_angle_cos);
  }
}

/* This threshold is a bit touchy (usual float precision issue), this value seems OK. */
#define LNOR_SPACE_TRIGO_THRESHOLD (1.0f - 1e-4f)

/**
 * Check each current smooth fan (one lnor space per smooth fan!), and if all its
 * matching custom lnors are not (enough) equal, add sharp edges as needed.
 */
static bool mesh_loops_split_lnor_fans(Mesh *mesh,
                                       MeshLoopNorSpaceArray *lnors_spacearr,
                                       const float (*new_lnors)[3])
{
  lib_bitmap *done_loops = LIB_BITMAP_NEW((size_t)mesh->totloop, __func__);
  bool changed = false;

  lib_assert(lnors_spacearr->data_type == MLNOR_SPACEARR_BMLOOP_PTR);

  for (int i = 0; i < mesh->totloop; i++) {
    if (!lnors_spacearr->lspacearr[i]) {
      /* This should not happen in theory, but in some rare case (probably ugly geometry)
       * we can get some NULL loopspacearr at this point. :/
       * Maybe we should set those loops' edges as sharp?
       */
      LIB_BITMAP_ENABLE(done_loops, i);
      if (G.debug & G_DEBUG) {
        printf("WARNING! Getting invalid NULL loop space for loop %d!\n", i);
      }
      continue;
    }

    if (!LIB_BITMAP_TEST(done_loops, i)) {
      /* Notes:
       * * In case of mono-loop smooth fan, we have nothing to do.
       * * Loops in this linklist are ordered (in reversed order compared to how they were
       *   discovered by dune_mesh_normals_loop_split(), but this is not a problem).
       *   Which means if we find a mismatching clnor,
       *   we know all remaining loops will have to be in a new, different smooth fan/lnor space.
       * * In smooth fan case, we compare each clnor against a ref one,
       *   to avoid small differences adding up into a real big one in the end!
       */
      if (lnors_spacearr->lspacearr[i]->flags & MLNOR_SPACE_IS_SINGLE) {
        LIB_BITMAP_ENABLE(done_loops, i);
        continue;
      }

      LinkNode *loops = lnors_spacearr->lspacearr[i]->loops;
      MeshLoop *prev_ml = NULL;
      const float *org_nor = NULL;

      while (loops) {
        MeshLoop *ml = loops->link;
        const int lidx = mesh_elem_index_get(ml);
        const float *nor = new_lnors[lidx];

        if (!org_nor) {
          org_nor = nor;
        }
        else if (dot_v3v3(org_nor, nor) < LNOR_SPACE_TRIGO_THRESHOLD) {
          /* Current normal differs too much from org one, we have to tag the edge between
           * previous loop's face and current's one as sharp.
           * We know those two loops do not point to the same edge,
           * since we do not allow reversed winding in a same smooth fan.
           */
          MeshEdge *e = (prev_ml->e == ml->prev->e) ? prev_ml->e : ml->e;

          mesh_elem_flag_disable(e, BM_ELEM_TAG | BM_ELEM_SMOOTH);
          changed = true;

          org_nor = nor;
        }

        prev_ml = ml;
        loops = loops->next;
        LIB_BITMAP_ENABLE(done_loops, lidx);
      }

      /* We also have to check between last and first loops,
       * otherwise we may miss some sharp edges here!
       * This is just a simplified version of above while loop.
       * See T45984. */
      loops = lnors_spacearr->lspacearr[i]->loops;
      if (loops && org_nor) {
        MeshLoop *ml = loops->link;
        const int lidx = mesh_elem_index_get(ml);
        const float *nor = new_lnors[lidx];

        if (dot_v3v3(org_nor, nor) < LNOR_SPACE_TRIGO_THRESHOLD) {
          MeshEdge *e = (prev_ml->e == ml->prev->e) ? prev_ml->e : ml->e;

          mesh_elem_flag_disable(e, MESH_ELEM_TAG | MESH_ELEM_SMOOTH);
          changed = true;
        }
      }
    }
  }

  mem_freen(done_loops);
  return changed;
}

/**
 * Assign custom normal data from given normal vectors, averaging normals
 * from one smooth fan as necessary.
 */
static void mesh_loops_assign_normal_data(Mesh *mesh,
                                          MeshLoopNorSpaceArray *lnors_spacearr,
                                          short (*r_clnors_data)[2],
                                          const int cd_loop_clnors_offset,
                                          const float (*new_lnors)[3])
{
  lib_bitmap *done_loops = LIB_BITMAP_NEW((size_t)bm->totloop, __func__);

  LIB_SMALLSTACK_DECLARE(clnors_data, short *);

  LIB_assert(lnors_spacearr->data_type == MLNOR_SPACEARR_BMLOOP_PTR);

  for (int i = 0; i < mesh->totloop; i++) {
    if (!lnors_spacearr->lspacearr[i]) {
      LIB_BITMAP_ENABLE(done_loops, i);
      if (G.debug & G_DEBUG) {
        printf("WARNING! Still getting invalid NULL loop space in second loop for loop %d!\n", i);
      }
      continue;
    }

    if (!LIB_BITMAP_TEST(done_loops, i)) {
      /* Note we accumulate and average all custom normals in current smooth fan,
       * to avoid getting different clnors data (tiny differences in plain custom normals can
       * give rather huge differences in computed 2D factors).
       */
      LinkNode *loops = lnors_spacearr->lspacearr[i]->loops;

      if (lnors_spacearr->lspacearr[i]->flags & MLNOR_SPACE_IS_SINGLE) {
        MeshLoop *ml = (MeshLoop *)loops;
        const int lidx = mesh_elem_index_get(ml);

        lib_assert(lidx == i);

        const float *nor = new_lnors[lidx];
        short *clnor = r_clnors_data ? &r_clnors_data[lidx] :
                                       MESH_ELEM_CD_GET_VOID_P(ml, cd_loop_clnors_offset);

        dune_lnor_space_custom_normal_to_data(lnors_spacearr->lspacearr[i], nor, clnor);
        LIB_BITMAP_ENABLE(done_loops, i);
      }
      else {
        int nbr_nors = 0;
        float avg_nor[3];
        short clnor_data_tmp[2], *clnor_data;

        zero_v3(avg_nor);

        while (loops) {
          MeshLoop *ml = loops->link;
          const int lidx = mesh m_elem_index_get(ml);
          const float *nor = new_lnors[lidx];
          short *clnor = r_clnors_data ? &r_clnors_data[lidx] :
                                         MESH_ELEM_CD_GET_VOID_P(ml, cd_loop_clnors_offset);

          nbr_nors++;
          add_v3_v3(avg_nor, nor);
          LIB_SMALLSTACK_PUSH(clnors_data, clnor);

          loops = loops->next;
          LIB_BITMAP_ENABLE(done_loops, lidx);
        }

        mul_v3_fl(avg_nor, 1.0f / (float)nbr_nors);
        dune_lnor_space_custom_normal_to_data(
            lnors_spacearr->lspacearr[i], avg_nor, clnor_data_tmp);

        while ((clnor_data = LIB_SMALLSTACK_POP(clnors_data))) {
          clnor_data[0] = clnor_data_tmp[0];
          clnor_data[1] = clnor_data_tmp[1];
        }
      }
    }
  }

  mem_freen(done_loops);
}

/**
 * Compute internal representation of given custom normals (as an array of float[2] or data layer).
 *
 * It also makes sure the mesh matches those custom normals, by marking new sharp edges to split
 * the smooth fans when loop normals for the same vertex are different, or averaging the normals
 * instead, depending on the do_split_fans parameter.
 */
static void mesh_loops_custom_normals_set(Mesh *mesh,
                                          const float (*vcos)[3],
                                          const float (*fnos)[3],
                                          MeshLoopNorSpaceArray *r_lnors_spacearr,
                                          short (*r_clnors_data)[2],
                                          const int cd_loop_clnors_offset,
                                          float (*new_lnors)[3],
                                          const int cd_new_lnors_offset,
                                          bool do_split_fans)
{
  MeshFace *f;
  MeshLoop *l;
  MeshIter liter, fiter;
  float(*cur_lnors)[3] = mem_mallocn(sizeof(*cur_lnors) * mesh->totloop, __func__);

  dune_lnor_spacearr_clear(r_lnors_spacearr);

  /* Tag smooth edges and set lnos from vnos when they might be completely smooth...
   * When using custom loop normals, disable the angle feature! */
  mesh_edges_sharp_tag(bm, fnos, -1.0f, false);

  /* Finish computing lnos by accumulating face normals
   * in each fan of faces defined by sharp edges. */
  mesh_loops_calc_normals(mesh,
                          vcos,
                          fnos,
                          cur_lnors,
                          r_lnors_spacearr,
                          r_clnors_data,
                          cd_loop_clnors_offset,
                          false,
                          EDGE_TAG_FROM_SPLIT_ANGLE_BYPASS);

  /* Extract new normals from the data layer if necessary. */
  float(*custom_lnors)[3] = new_lnors;

  if (new_lnors == NULL) {
    custom_lnors = mem_mallocn(sizeof(*new_lnors) * mesh->totloop, __func__);

    MESH_ITER (f, &fiter, mesh, MESH_FACES_OF_MESH) {
      MESH_ELEM_ITER (l, &liter, f, MESH_LOOPS_OF_FACE) {
        const float *normal = MESH_ELEM_CD_GET_VOID_P(l, cd_new_lnors_offset);
        copy_v3_v3(custom_lnors[mesh_elem_index_get(l)], normal);
      }
    }
  }

  /* Validate the new normals. */
  for (int i = 0; i < bm->totloop; i++) {
    if (is_zero_v3(custom_lnors[i])) {
      copy_v3_v3(custom_lnors[i], cur_lnors[i]);
    }
    else {
      normalize_v3(custom_lnors[i]);
    }
  }

  /* Now, check each current smooth fan (one lnor space per smooth fan!),
   * and if all its matching custom lnors are not equal, add sharp edges as needed. */
  if (do_split_fans && mesh_loops_split_lnor_fans(mesh, r_lnors_spacearr, custom_lnors)) {
    /* If any sharp edges were added, run mesh_loops_calc_normals() again to get lnor
     * spacearr/smooth fans matching the given custom lnors. */
    dune_lnor_spacearr_clear(r_lnors_spacearr);

    mesh_loops_calc_normals(mesh,
                            vcos,
                            fnos,
                            cur_lnors,
                            r_lnors_spacearr,
                            r_clnors_data,
                            cd_loop_clnors_offset,
                            false,
                            EDGE_TAG_FROM_SPLIT_ANGLE_BYPASS);
  }

  /* And we just have to convert plain object-space custom normals to our
   * lnor space-encoded ones. */
  mesh_loops_assign_normal_data(
      mesh, r_lnors_spacearr, r_clnors_data, cd_loop_clnors_offset, custom_lnors);

  mem_freen(cur_lnors);

  if (custom_lnors != new_lnors) {
    mem_freen(custom_lnors);
  }
}

static void mesh_loops_calc_normals_no_autosmooth(BMesh *bm,
                                                     const float (*vnos)[3],
                                                     const float (*fnos)[3],
                                                     float (*r_lnos)[3])
{
  MeshIter fiter;
  MeshFace *f_curr;

  {
    char htype = MEEH_LOOP;
    if (vnos) {
      htype |= MESH_VERT;
    }
    if (fnos) {
      htype |= MESH_FACE;
    }
    mesh_elem_index_ensure(mesh, htype);
  }

  MESH_ITER (f_curr, &fiter, mesh, MESH_FACES_OF_MESH) {
    MeshLoop *l_curr, *l_first;
    const bool is_face_flat = !mesh_elem_flag_test(f_curr, BM_ELEM_SMOOTH);

    l_curr = l_first = MESH_FACE_FIRST_LOOP(f_curr);
    do {
      const float *no = is_face_flat ? (fnos ? fnos[BM_elem_index_get(f_curr)] : f_curr->no) :
                                       (vnos ? vnos[BM_elem_index_get(l_curr->v)] : l_curr->v->no);
      copy_v3_v3(r_lnos[mesh_elem_index_get(l_curr)], no);

    } while ((l_curr = l_curr->next) != l_first);
  }
}

void mesh_loops_calc_normal_vcos(Mesh *mesh,
                                 const float (*vcos)[3],
                                 const float (*vnos)[3],
                                 const float (*fnos)[3],
                                 const bool use_split_normals,
                                 const float split_angle,
                                 float (*r_lnos)[3],
                                 MeshLoopNorSpaceArray *r_lnors_spacearr,
                                 short (*clnors_data)[2],
                                 const int cd_loop_clnors_offset,
                                 const bool do_rebuild)
{
  const bool has_clnors = clnors_data || (cd_loop_clnors_offset != -1);

  if (use_split_normals) {
    mesh_loops_calc_normals(mesh,
                            vcos,
                            fnos,
                            r_lnos,
                            r_lnors_spacearr,
                            clnors_data,
                            cd_loop_clnors_offset,
                            do_rebuild,
                            has_clnors ? -1.0f : cosf(split_angle));
  }
  else {
    lib_assert(!r_lnors_spacearr);
    mesh_loops_calc_normals_no_autosmooth(mesh, vnos, fnos, r_lnos);
  }
}

/* -------------------------------------------------------------------- */
/** Loop Normal Space API **/

void mesh_lnorspacearr_store(Mesh *mesh, float (*r_lnors)[3])
{
  lib_assert(mesh->lnor_spacearr != NULL);

  if (!CustomData_has_layer(&mesh->ldata, CD_CUSTOMLOOPNORMAL)) {
    mesh_data_layer_add(mesh, &mesh->ldata, CD_CUSTOMLOOPNORMAL);
  }

  int cd_loop_clnors_offset = CustomData_get_offset(&mesh->ldata, CD_CUSTOMLOOPNORMAL);

  mesh_loops_calc_normal_vcos(mesh,
                            NULL,
                            NULL,
                            NULL,
                            true,
                            M_PI,
                            r_lnors,
                            mesh->lnor_spacearr,
                            NULL,
                            cd_loop_clnors_offset,
                            false);
  mesh->spacearr_dirty &= ~(MESH_SPACEARR_DIRTY | MESH_SPACEARR_DIRTY_ALL);
}

#define CLEAR_SPACEARRAY_THRESHOLD(x) ((x) / 2)

void mesh_lnorspace_invalidate(Mesh *mesh, const bool do_invalidate_all)
{
  if (mesh->spacearr_dirty & MESH_SPACEARR_DIRTY_ALL) {
    return;
  }
  if (do_invalidate_all || mesh->totvertsel > CLEAR_SPACEARRAY_THRESHOLD(mesh->totvert)) {
    mesh->spacearr_dirty |= MESH_SPACEARR_DIRTY_ALL;
    return;
  }
  if (mesh->lnor_spacearr == NULL) {
    mesh->spacearr_dirty |= MESH_SPACEARR_DIRTY_ALL;
    return;
  }

  MeshVert *v;
  MeshLoop *l;
  MeshIter viter, liter;
  /* NOTE: we could use temp tag of MeshItem for that,
   * but probably better not use it in such a low-level func?
   * --mont29 */
  lib_bitmap *done_verts = LIB_BITMAP_NEW(mesh->totvert, __func__);

  mesh_elem_index_ensure(mesh, MESH_VERT);

  /* When we affect a given vertex, we may affect following smooth fans:
   * - all smooth fans of said vertex;
   * - all smooth fans of all immediate loop-neighbors vertices;
   * This can be simplified as 'all loops of selected vertices and their immediate neighbors'
   * need to be tagged for update.
   */
  MESH_ITER (v, &viter, mesh, MESH_VERTS_OF_MESH) {
    if (mesh_elem_flag_test(v, MESH_ELEM_SELECT)) {
      MESH_ELEM_ITER (l, &liter, v, MESH_LOOPS_OF_VERT) {
        MESH_ELEM_API_FLAG_ENABLE(l, MESH_LNORSPACE_UPDATE);

        /* Note that we only handle unselected neighbor vertices here, main loop will take care of
         * selected ones. */
        if ((!mesh_elem_flag_test(l->prev->v, MESH_ELEM_SELECT)) &&
            !LIB_BITMAP_TEST(done_verts, mesh_elem_index_get(l->prev->v))) {

          MeshLoop *l_prev;
          MeshIter liter_prev;
          MESH_ELEM_ITER (l_prev, &liter_prev, l->prev->v, BM_LOOPS_OF_VERT) {
            MESH_ELEM_API_FLAG_ENABLE(l_prev, BM_LNORSPACE_UPDATE);
          }
          LIB_BITMAP_ENABLE(done_verts, BM_elem_index_get(l_prev->v));
        }

        if ((!mesh_elem_flag_test(l->next->v, BM_ELEM_SELECT)) &&
            !LIB_BITMAP_TEST(done_verts, BM_elem_index_get(l->next->v))) {

          MeshLoop *l_next;
          MeshIter liter_next;
          MESH_ELEM_ITER (l_next, &liter_next, l->next->v, BM_LOOPS_OF_VERT) {
            MESH_ELEM_API_FLAG_ENABLE(l_next, BM_LNORSPACE_UPDATE);
          }
          BLI_BITMAP_ENABLE(done_verts, BM_elem_index_get(l_next->v));
        }
      }

      BLI_BITMAP_ENABLE(done_verts, BM_elem_index_get(v));
    }
  }

  MEM_freeN(done_verts);
  bm->spacearr_dirty |= BM_SPACEARR_DIRTY;
}

void BM_lnorspace_rebuild(BMesh *bm, bool preserve_clnor)
{
  BLI_assert(bm->lnor_spacearr != NULL);

  if (!(bm->spacearr_dirty & (BM_SPACEARR_DIRTY | BM_SPACEARR_DIRTY_ALL))) {
    return;
  }
  BMFace *f;
  BMLoop *l;
  BMIter fiter, liter;

  float(*r_lnors)[3] = MEM_callocN(sizeof(*r_lnors) * bm->totloop, __func__);
  float(*oldnors)[3] = preserve_clnor ? MEM_mallocN(sizeof(*oldnors) * bm->totloop, __func__) :
                                        NULL;

  int cd_loop_clnors_offset = CustomData_get_offset(&bm->ldata, CD_CUSTOMLOOPNORMAL);

  BM_mesh_elem_index_ensure(bm, BM_LOOP);

  if (preserve_clnor) {
    BLI_assert(bm->lnor_spacearr->lspacearr != NULL);

    BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
      BM_ITER_ELEM (l, &liter, f, BM_LOOPS_OF_FACE) {
        if (BM_ELEM_API_FLAG_TEST(l, BM_LNORSPACE_UPDATE) ||
            bm->spacearr_dirty & BM_SPACEARR_DIRTY_ALL) {
          short(*clnor)[2] = BM_ELEM_CD_GET_VOID_P(l, cd_loop_clnors_offset);
          int l_index = BM_elem_index_get(l);

          BKE_lnor_space_custom_data_to_normal(
              bm->lnor_spacearr->lspacearr[l_index], *clnor, oldnors[l_index]);
        }
      }
    }
  }

  if (bm->spacearr_dirty & BM_SPACEARR_DIRTY_ALL) {
    BKE_lnor_spacearr_clear(bm->lnor_spacearr);
  }
  BM_loops_calc_normal_vcos(bm,
                            NULL,
                            NULL,
                            NULL,
                            true,
                            M_PI,
                            r_lnors,
                            bm->lnor_spacearr,
                            NULL,
                            cd_loop_clnors_offset,
                            true);
  MEM_freeN(r_lnors);

  BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
    BM_ITER_ELEM (l, &liter, f, BM_LOOPS_OF_FACE) {
      if (BM_ELEM_API_FLAG_TEST(l, BM_LNORSPACE_UPDATE) ||
          bm->spacearr_dirty & BM_SPACEARR_DIRTY_ALL) {
        if (preserve_clnor) {
          short(*clnor)[2] = BM_ELEM_CD_GET_VOID_P(l, cd_loop_clnors_offset);
          int l_index = BM_elem_index_get(l);
          BKE_lnor_space_custom_normal_to_data(
              bm->lnor_spacearr->lspacearr[l_index], oldnors[l_index], *clnor);
        }
        BM_ELEM_API_FLAG_DISABLE(l, BM_LNORSPACE_UPDATE);
      }
    }
  }

  MEM_SAFE_FREE(oldnors);
  bm->spacearr_dirty &= ~(BM_SPACEARR_DIRTY | BM_SPACEARR_DIRTY_ALL);

#ifndef NDEBUG
  BM_lnorspace_err(bm);
#endif
}

void BM_lnorspace_update(BMesh *bm)
{
  if (bm->lnor_spacearr == NULL) {
    bm->lnor_spacearr = MEM_callocN(sizeof(*bm->lnor_spacearr), __func__);
  }
  if (bm->lnor_spacearr->lspacearr == NULL) {
    float(*lnors)[3] = MEM_callocN(sizeof(*lnors) * bm->totloop, __func__);

    BM_lnorspacearr_store(bm, lnors);

    MEM_freeN(lnors);
  }
  else if (bm->spacearr_dirty & (BM_SPACEARR_DIRTY | BM_SPACEARR_DIRTY_ALL)) {
    BM_lnorspace_rebuild(bm, false);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Loop Normal Edit Data Array API
 *
 * Utilities for creating/freeing #BMLoopNorEditDataArray.
 * \{ */

/**
 * Auxiliary function only used by rebuild to detect if any spaces were not marked as invalid.
 * Reports error if any of the lnor spaces change after rebuilding, meaning that all the possible
 * lnor spaces to be rebuilt were not correctly marked.
 */
#ifndef NDEBUG
void BM_lnorspace_err(BMesh *bm)
{
  bm->spacearr_dirty |= BM_SPACEARR_DIRTY_ALL;
  bool clear = true;

  MLoopNorSpaceArray *temp = MEM_callocN(sizeof(*temp), __func__);
  temp->lspacearr = NULL;

  BKE_lnor_spacearr_init(temp, bm->totloop, MLNOR_SPACEARR_BMLOOP_PTR);

  int cd_loop_clnors_offset = CustomData_get_offset(&bm->ldata, CD_CUSTOMLOOPNORMAL);
  float(*lnors)[3] = MEM_callocN(sizeof(*lnors) * bm->totloop, __func__);
  BM_loops_calc_normal_vcos(
      bm, NULL, NULL, NULL, true, M_PI, lnors, temp, NULL, cd_loop_clnors_offset, true);

  for (int i = 0; i < bm->totloop; i++) {
    int j = 0;
    j += compare_ff(
        temp->lspacearr[i]->ref_alpha, bm->lnor_spacearr->lspacearr[i]->ref_alpha, 1e-4f);
    j += compare_ff(
        temp->lspacearr[i]->ref_beta, bm->lnor_spacearr->lspacearr[i]->ref_beta, 1e-4f);
    j += compare_v3v3(
        temp->lspacearr[i]->vec_lnor, bm->lnor_spacearr->lspacearr[i]->vec_lnor, 1e-4f);
    j += compare_v3v3(
        temp->lspacearr[i]->vec_ortho, bm->lnor_spacearr->lspacearr[i]->vec_ortho, 1e-4f);
    j += compare_v3v3(
        temp->lspacearr[i]->vec_ref, bm->lnor_spacearr->lspacearr[i]->vec_ref, 1e-4f);

    if (j != 5) {
      clear = false;
      break;
    }
  }
  BKE_lnor_spacearr_free(temp);
  MEM_freeN(temp);
  MEM_freeN(lnors);
  BLI_assert(clear);

  bm->spacearr_dirty &= ~BM_SPACEARR_DIRTY_ALL;
}
#endif

static void bm_loop_normal_mark_indiv_do_loop(BMLoop *l,
                                              BLI_bitmap *loops,
                                              MLoopNorSpaceArray *lnor_spacearr,
                                              int *totloopsel,
                                              const bool do_all_loops_of_vert)
{
  if (l != NULL) {
    const int l_idx = BM_elem_index_get(l);

    if (!BLI_BITMAP_TEST(loops, l_idx)) {
      /* If vert and face selected share a loop, mark it for editing. */
      BLI_BITMAP_ENABLE(loops, l_idx);
      (*totloopsel)++;

      if (do_all_loops_of_vert) {
        /* If required, also mark all loops shared by that vertex.
         * This is needed when loop spaces may change
         * (i.e. when some faces or edges might change of smooth/sharp status). */
        BMIter liter;
        BMLoop *lfan;
        BM_ITER_ELEM (lfan, &liter, l->v, BM_LOOPS_OF_VERT) {
          const int lfan_idx = BM_elem_index_get(lfan);
          if (!BLI_BITMAP_TEST(loops, lfan_idx)) {
            BLI_BITMAP_ENABLE(loops, lfan_idx);
            (*totloopsel)++;
          }
        }
      }
      else {
        /* Mark all loops in same loop normal space (aka smooth fan). */
        if ((lnor_spacearr->lspacearr[l_idx]->flags & MLNOR_SPACE_IS_SINGLE) == 0) {
          for (LinkNode *node = lnor_spacearr->lspacearr[l_idx]->loops; node; node = node->next) {
            const int lfan_idx = BM_elem_index_get((BMLoop *)node->link);
            if (!BLI_BITMAP_TEST(loops, lfan_idx)) {
              BLI_BITMAP_ENABLE(loops, lfan_idx);
              (*totloopsel)++;
            }
          }
        }
      }
    }
  }
}

/* Mark the individual clnors to be edited, if multiple selection methods are used. */
static int bm_loop_normal_mark_indiv(BMesh *bm, BLI_bitmap *loops, const bool do_all_loops_of_vert)
{
  BMEditSelection *ese, *ese_prev;
  int totloopsel = 0;

  const bool sel_verts = (bm->selectmode & SCE_SELECT_VERTEX) != 0;
  const bool sel_edges = (bm->selectmode & SCE_SELECT_EDGE) != 0;
  const bool sel_faces = (bm->selectmode & SCE_SELECT_FACE) != 0;
  const bool use_sel_face_history = sel_faces && (sel_edges || sel_verts);

  BM_mesh_elem_index_ensure(bm, BM_LOOP);

  BLI_assert(bm->lnor_spacearr != NULL);
  BLI_assert(bm->lnor_spacearr->data_type == MLNOR_SPACEARR_BMLOOP_PTR);

  if (use_sel_face_history) {
    /* Using face history allows to select a single loop from a single face...
     * Note that this is O(n^2) piece of code,
     * but it is not designed to be used with huge selection sets,
     * rather with only a few items selected at most. */
    /* Goes from last selected to the first selected element. */
    for (ese = bm->selected.last; ese; ese = ese->prev) {
      if (ese->htype == BM_FACE) {
        /* If current face is selected,
         * then any verts to be edited must have been selected before it. */
        for (ese_prev = ese->prev; ese_prev; ese_prev = ese_prev->prev) {
          if (ese_prev->htype == BM_VERT) {
            bm_loop_normal_mark_indiv_do_loop(
                BM_face_vert_share_loop((BMFace *)ese->ele, (BMVert *)ese_prev->ele),
                loops,
                bm->lnor_spacearr,
                &totloopsel,
                do_all_loops_of_vert);
          }
          else if (ese_prev->htype == BM_EDGE) {
            BMEdge *e = (BMEdge *)ese_prev->ele;
            bm_loop_normal_mark_indiv_do_loop(BM_face_vert_share_loop((BMFace *)ese->ele, e->v1),
                                              loops,
                                              bm->lnor_spacearr,
                                              &totloopsel,
                                              do_all_loops_of_vert);

            bm_loop_normal_mark_indiv_do_loop(BM_face_vert_share_loop((BMFace *)ese->ele, e->v2),
                                              loops,
                                              bm->lnor_spacearr,
                                              &totloopsel,
                                              do_all_loops_of_vert);
          }
        }
      }
    }
  }
  else {
    if (sel_faces) {
      /* Only select all loops of selected faces. */
      BMLoop *l;
      BMFace *f;
      BMIter liter, fiter;
      BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
        if (BM_elem_flag_test(f, BM_ELEM_SELECT)) {
          BM_ITER_ELEM (l, &liter, f, BM_LOOPS_OF_FACE) {
            bm_loop_normal_mark_indiv_do_loop(
                l, loops, bm->lnor_spacearr, &totloopsel, do_all_loops_of_vert);
          }
        }
      }
    }
    if (sel_edges) {
      /* Only select all loops of selected edges. */
      BMLoop *l;
      BMEdge *e;
      BMIter liter, eiter;
      BM_ITER_MESH (e, &eiter, bm, BM_EDGES_OF_MESH) {
        if (BM_elem_flag_test(e, BM_ELEM_SELECT)) {
          BM_ITER_ELEM (l, &liter, e, BM_LOOPS_OF_EDGE) {
            bm_loop_normal_mark_indiv_do_loop(
                l, loops, bm->lnor_spacearr, &totloopsel, do_all_loops_of_vert);
            /* Loops actually 'have' two edges, or said otherwise, a selected edge actually selects
             * *two* loops in each of its faces. We have to find the other one too. */
            if (BM_vert_in_edge(e, l->next->v)) {
              bm_loop_normal_mark_indiv_do_loop(
                  l->next, loops, bm->lnor_spacearr, &totloopsel, do_all_loops_of_vert);
            }
            else {
              BLI_assert(BM_vert_in_edge(e, l->prev->v));
              bm_loop_normal_mark_indiv_do_loop(
                  l->prev, loops, bm->lnor_spacearr, &totloopsel, do_all_loops_of_vert);
            }
          }
        }
      }
    }
    if (sel_verts) {
      /* Select all loops of selected verts. */
      BMLoop *l;
      BMVert *v;
      BMIter liter, viter;
      BM_ITER_MESH (v, &viter, bm, BM_VERTS_OF_MESH) {
        if (BM_elem_flag_test(v, BM_ELEM_SELECT)) {
          BM_ITER_ELEM (l, &liter, v, BM_LOOPS_OF_VERT) {
            bm_loop_normal_mark_indiv_do_loop(
                l, loops, bm->lnor_spacearr, &totloopsel, do_all_loops_of_vert);
          }
        }
      }
    }
  }

  return totloopsel;
}

static void loop_normal_editdata_init(
    BMesh *bm, BMLoopNorEditData *lnor_ed, BMVert *v, BMLoop *l, const int offset)
{
  BLI_assert(bm->lnor_spacearr != NULL);
  BLI_assert(bm->lnor_spacearr->lspacearr != NULL);

  const int l_index = BM_elem_index_get(l);
  short *clnors_data = BM_ELEM_CD_GET_VOID_P(l, offset);

  lnor_ed->loop_index = l_index;
  lnor_ed->loop = l;

  float custom_normal[3];
  BKE_lnor_space_custom_data_to_normal(
      bm->lnor_spacearr->lspacearr[l_index], clnors_data, custom_normal);

  lnor_ed->clnors_data = clnors_data;
  copy_v3_v3(lnor_ed->nloc, custom_normal);
  copy_v3_v3(lnor_ed->niloc, custom_normal);

  lnor_ed->loc = v->co;
}

BMLoopNorEditDataArray *BM_loop_normal_editdata_array_init(BMesh *bm,
                                                           const bool do_all_loops_of_vert)
{
  BMLoop *l;
  BMVert *v;
  BMIter liter, viter;

  int totloopsel = 0;

  BLI_assert(bm->spacearr_dirty == 0);

  BMLoopNorEditDataArray *lnors_ed_arr = MEM_callocN(sizeof(*lnors_ed_arr), __func__);
  lnors_ed_arr->lidx_to_lnor_editdata = MEM_callocN(
      sizeof(*lnors_ed_arr->lidx_to_lnor_editdata) * bm->totloop, __func__);

  if (!CustomData_has_layer(&bm->ldata, CD_CUSTOMLOOPNORMAL)) {
    BM_data_layer_add(bm, &bm->ldata, CD_CUSTOMLOOPNORMAL);
  }
  const int cd_custom_normal_offset = CustomData_get_offset(&bm->ldata, CD_CUSTOMLOOPNORMAL);

  BM_mesh_elem_index_ensure(bm, BM_LOOP);

  BLI_bitmap *loops = BLI_BITMAP_NEW(bm->totloop, __func__);

  /* This function define loop normals to edit, based on selection modes and history. */
  totloopsel = bm_loop_normal_mark_indiv(bm, loops, do_all_loops_of_vert);

  if (totloopsel) {
    BMLoopNorEditData *lnor_ed = lnors_ed_arr->lnor_editdata = MEM_mallocN(
        sizeof(*lnor_ed) * totloopsel, __func__);

    BM_ITER_MESH (v, &viter, bm, BM_VERTS_OF_MESH) {
      BM_ITER_ELEM (l, &liter, v, BM_LOOPS_OF_VERT) {
        if (BLI_BITMAP_TEST(loops, BM_elem_index_get(l))) {
          loop_normal_editdata_init(bm, lnor_ed, v, l, cd_custom_normal_offset);
          lnors_ed_arr->lidx_to_lnor_editdata[BM_elem_index_get(l)] = lnor_ed;
          lnor_ed++;
        }
      }
    }
    lnors_ed_arr->totloop = totloopsel;
  }

  MEM_freeN(loops);
  lnors_ed_arr->cd_custom_normal_offset = cd_custom_normal_offset;
  return lnors_ed_arr;
}

void BM_loop_normal_editdata_array_free(BMLoopNorEditDataArray *lnors_ed_arr)
{
  MEM_SAFE_FREE(lnors_ed_arr->lnor_editdata);
  MEM_SAFE_FREE(lnors_ed_arr->lidx_to_lnor_editdata);
  MEM_freeN(lnors_ed_arr);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Custom Normals / Vector Layer Conversion
 * \{ */

bool BM_custom_loop_normals_to_vector_layer(BMesh *bm)
{
  BMFace *f;
  BMLoop *l;
  BMIter liter, fiter;

  if (!CustomData_has_layer(&bm->ldata, CD_CUSTOMLOOPNORMAL)) {
    return false;
  }

  BM_lnorspace_update(bm);

  /* Create a loop normal layer. */
  if (!CustomData_has_layer(&bm->ldata, CD_NORMAL)) {
    BM_data_layer_add(bm, &bm->ldata, CD_NORMAL);

    CustomData_set_layer_flag(&bm->ldata, CD_NORMAL, CD_FLAG_TEMPORARY);
  }

  const int cd_custom_normal_offset = CustomData_get_offset(&bm->ldata, CD_CUSTOMLOOPNORMAL);
  const int cd_normal_offset = CustomData_get_offset(&bm->ldata, CD_NORMAL);

  int l_index = 0;
  BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
    BM_ITER_ELEM (l, &liter, f, BM_LOOPS_OF_FACE) {
      const short *clnors_data = BM_ELEM_CD_GET_VOID_P(l, cd_custom_normal_offset);
      float *normal = BM_ELEM_CD_GET_VOID_P(l, cd_normal_offset);

      BKE_lnor_space_custom_data_to_normal(
          bm->lnor_spacearr->lspacearr[l_index], clnors_data, normal);
      l_index += 1;
    }
  }

  return true;
}

void BM_custom_loop_normals_from_vector_layer(BMesh *bm, bool add_sharp_edges)
{
  if (!CustomData_has_layer(&bm->ldata, CD_CUSTOMLOOPNORMAL) ||
      !CustomData_has_layer(&bm->ldata, CD_NORMAL)) {
    return;
  }

  const int cd_custom_normal_offset = CustomData_get_offset(&bm->ldata, CD_CUSTOMLOOPNORMAL);
  const int cd_normal_offset = CustomData_get_offset(&bm->ldata, CD_NORMAL);

  if (bm->lnor_spacearr == NULL) {
    bm->lnor_spacearr = MEM_callocN(sizeof(*bm->lnor_spacearr), __func__);
  }

  bm_mesh_loops_custom_normals_set(bm,
                                   NULL,
                                   NULL,
                                   bm->lnor_spacearr,
                                   NULL,
                                   cd_custom_normal_offset,
                                   NULL,
                                   cd_normal_offset,
                                   add_sharp_edges);

  bm->spacearr_dirty &= ~(BM_SPACEARR_DIRTY | BM_SPACEARR_DIRTY_ALL);
}

