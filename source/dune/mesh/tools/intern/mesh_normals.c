/** \file
 * \ingroup bmesh
 *
 * BM mesh normal calculation functions.
 *
 * \see mesh_normals.cc for the equivalent #Mesh functionality.
 */

#include "MEM_guardedalloc.h"

#include "DNA_scene_types.h"

#include "BLI_bitmap.h"
#include "BLI_linklist_stack.h"
#include "BLI_math.h"
#include "BLI_stack.h"
#include "BLI_task.h"
#include "BLI_utildefines.h"

#include "BKE_customdata.h"
#include "BKE_editmesh.h"
#include "BKE_global.h"
#include "BKE_mesh.h"

#include "intern/bmesh_private.h"

/* Smooth angle to use when tagging edges is disabled entirely. */
#define EDGE_TAG_FROM_SPLIT_ANGLE_BYPASS -FLT_MAX

static void bm_edge_tag_from_smooth_and_set_sharp(const float (*fnos)[3],
                                                  BMEdge *e,
                                                  const float split_angle_cos);
static void bm_edge_tag_from_smooth(const float (*fnos)[3],
                                    BMEdge *e,
                                    const float split_angle_cos);

/* -------------------------------------------------------------------- */
/** \name Update Vertex & Face Normals
 * \{ */

/**
 * Helpers for #BM_mesh_normals_update and #BM_verts_calc_normal_vcos
 */

/* We use that existing internal API flag,
 * assuming no other tool using it would run concurrently to clnors editing. */
#define BM_LNORSPACE_UPDATE _FLAG_MF

typedef struct BMVertsCalcNormalsWithCoordsData {
  /* Read-only data. */
  const float (*fnos)[3];
  const float (*vcos)[3];

  /* Write data. */
  float (*vnos)[3];
} BMVertsCalcNormalsWithCoordsData;

BLI_INLINE void bm_vert_calc_normals_accum_loop(const BMLoop *l_iter,
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
  BLI_assert(!isnan(fac));
  madd_v3_v3fl(v_no, f_no, fac);
}

static void bm_vert_calc_normals_impl(BMVert *v)
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
   * even in the case of `BLI_smallhash.h` & small inline lookup tables.
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

  BMEdge *e_first = v->e;
  if (e_first != NULL) {
    float e1diff[3], e2diff[3];
    BMEdge *e_iter = e_first;
    do {
      BMLoop *l_first = e_iter->l;
      if (l_first != NULL) {
        sub_v3_v3v3(e2diff, e_iter->v1->co, e_iter->v2->co);
        normalize_v3(e2diff);

        BMLoop *l_iter = l_first;
        do {
          if (l_iter->v == v) {
            BMEdge *e_prev = l_iter->prev->e;
            sub_v3_v3v3(e1diff, e_prev->v1->co, e_prev->v2->co);
            normalize_v3(e1diff);

            bm_vert_calc_normals_accum_loop(l_iter, e1diff, e2diff, l_iter->f->no, v_no);
          }
        } while ((l_iter = l_iter->radial_next) != l_first);
      }
    } while ((e_iter = BM_DISK_EDGE_NEXT(e_iter, v)) != e_first);

    if (LIKELY(normalize_v3(v_no) != 0.0f)) {
      return;
    }
  }
  /* Fallback normal. */
  normalize_v3_v3(v_no, v->co);
}

static void bm_vert_calc_normals_cb(void *UNUSED(userdata),
                                    MempoolIterData *mp_v,
                                    const TaskParallelTLS *__restrict UNUSED(tls))
{
  BMVert *v = (BMVert *)mp_v;
  bm_vert_calc_normals_impl(v);
}

static void bm_vert_calc_normals_with_coords(BMVert *v, BMVertsCalcNormalsWithCoordsData *data)
{
  /* See #bm_vert_calc_normals_impl note on performance. */
  float *v_no = data->vnos[BM_elem_index_get(v)];
  zero_v3(v_no);

  /* Loop over edges. */
  BMEdge *e_first = v->e;
  if (e_first != NULL) {
    float e1diff[3], e2diff[3];
    BMEdge *e_iter = e_first;
    do {
      BMLoop *l_first = e_iter->l;
      if (l_first != NULL) {
        sub_v3_v3v3(e2diff,
                    data->vcos[BM_elem_index_get(e_iter->v1)],
                    data->vcos[BM_elem_index_get(e_iter->v2)]);
        normalize_v3(e2diff);

        BMLoop *l_iter = l_first;
        do {
          if (l_iter->v == v) {
            BMEdge *e_prev = l_iter->prev->e;
            sub_v3_v3v3(e1diff,
                        data->vcos[BM_elem_index_get(e_prev->v1)],
                        data->vcos[BM_elem_index_get(e_prev->v2)]);
            normalize_v3(e1diff);

            bm_vert_calc_normals_accum_loop(
                l_iter, e1diff, e2diff, data->fnos[BM_elem_index_get(l_iter->f)], v_no);
          }
        } while ((l_iter = l_iter->radial_next) != l_first);
      }
    } while ((e_iter = BM_DISK_EDGE_NEXT(e_iter, v)) != e_first);

    if (LIKELY(normalize_v3(v_no) != 0.0f)) {
      return;
    }
  }
  /* Fallback normal. */
  normalize_v3_v3(v_no, data->vcos[BM_elem_index_get(v)]);
}

static void bm_vert_calc_normals_with_coords_cb(void *userdata,
                                                MempoolIterData *mp_v,
                                                const TaskParallelTLS *__restrict UNUSED(tls))
{
  BMVertsCalcNormalsWithCoordsData *data = userdata;
  BMVert *v = (BMVert *)mp_v;
  bm_vert_calc_normals_with_coords(v, data);
}

static void bm_mesh_verts_calc_normals(BMesh *bm,
                                       const float (*fnos)[3],
                                       const float (*vcos)[3],
                                       float (*vnos)[3])
{
  BM_mesh_elem_index_ensure(bm, BM_FACE | ((vnos || vcos) ? BM_VERT : 0));

  TaskParallelSettings settings;
  BLI_parallel_mempool_settings_defaults(&settings);
  settings.use_threading = bm->totvert >= BM_OMP_LIMIT;

  if (vcos == NULL) {
    BM_iter_parallel(bm, BM_VERTS_OF_MESH, bm_vert_calc_normals_cb, NULL, &settings);
  }
  else {
    BLI_assert(!ELEM(NULL, fnos, vnos));
    BMVertsCalcNormalsWithCoordsData data = {
        .fnos = fnos,
        .vcos = vcos,
        .vnos = vnos,
    };
    BM_iter_parallel(bm, BM_VERTS_OF_MESH, bm_vert_calc_normals_with_coords_cb, &data, &settings);
  }
}

static void bm_face_calc_normals_cb(void *UNUSED(userdata),
                                    MempoolIterData *mp_f,
                                    const TaskParallelTLS *__restrict UNUSED(tls))
{
  BMFace *f = (BMFace *)mp_f;

  BM_face_calc_normal(f, f->no);
}

void BM_mesh_normals_update_ex(BMesh *bm, const struct BMeshNormalsUpdate_Params *params)
{
  if (params->face_normals) {
    /* Calculate all face normals. */
    TaskParallelSettings settings;
    BLI_parallel_mempool_settings_defaults(&settings);
    settings.use_threading = bm->totedge >= BM_OMP_LIMIT;

    BM_iter_parallel(bm, BM_FACES_OF_MESH, bm_face_calc_normals_cb, NULL, &settings);
  }

  /* Add weighted face normals to vertices, and normalize vert normals. */
  bm_mesh_verts_calc_normals(bm, NULL, NULL, NULL);
}

void BM_mesh_normals_update(BMesh *bm)
{
  BM_mesh_normals_update_ex(bm,
                            &(const struct BMeshNormalsUpdate_Params){
                                .face_normals = true,
                            });
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Update Vertex & Face Normals (Partial Updates)
 * \{ */

static void bm_partial_faces_parallel_range_calc_normals_cb(
    void *userdata, const int iter, const TaskParallelTLS *__restrict UNUSED(tls))
{
  BMFace *f = ((BMFace **)userdata)[iter];
  BM_face_calc_normal(f, f->no);
}

static void bm_partial_verts_parallel_range_calc_normal_cb(
    void *userdata, const int iter, const TaskParallelTLS *__restrict UNUSED(tls))
{
  BMVert *v = ((BMVert **)userdata)[iter];
  bm_vert_calc_normals_impl(v);
}

void BM_mesh_normals_update_with_partial_ex(BMesh *UNUSED(bm),
                                            const BMPartialUpdate *bmpinfo,
                                            const struct BMeshNormalsUpdate_Params *params)
{
  BLI_assert(bmpinfo->params.do_normals);
  /* While harmless, exit early if there is nothing to do. */
  if (UNLIKELY((bmpinfo->verts_len == 0) && (bmpinfo->faces_len == 0))) {
    return;
  }

  BMVert **verts = bmpinfo->verts;
  BMFace **faces = bmpinfo->faces;
  const int verts_len = bmpinfo->verts_len;
  const int faces_len = bmpinfo->faces_len;

  TaskParallelSettings settings;
  BLI_parallel_range_settings_defaults(&settings);

  /* Faces. */
  if (params->face_normals) {
    BLI_task_parallel_range(
        0, faces_len, faces, bm_partial_faces_parallel_range_calc_normals_cb, &settings);
  }

  /* Verts. */
  BLI_task_parallel_range(
      0, verts_len, verts, bm_partial_verts_parallel_range_calc_normal_cb, &settings);
}

void BM_mesh_normals_update_with_partial(BMesh *bm, const BMPartialUpdate *bmpinfo)
{
  BM_mesh_normals_update_with_partial_ex(bm,
                                         bmpinfo,
                                         &(const struct BMeshNormalsUpdate_Params){
                                             .face_normals = true,
                                         });
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Update Vertex & Face Normals (Custom Coords)
 * \{ */

void BM_verts_calc_normal_vcos(BMesh *bm,
                               const float (*fnos)[3],
                               const float (*vcos)[3],
                               float (*vnos)[3])
{
  /* Add weighted face normals to vertices, and normalize vert normals. */
  bm_mesh_verts_calc_normals(bm, fnos, vcos, vnos);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Tagging Utility Functions
 * \{ */

void BM_normals_loops_edges_tag(BMesh *bm, const bool do_edges)
{
  BMFace *f;
  BMEdge *e;
  BMIter fiter, eiter;
  BMLoop *l_curr, *l_first;

  if (do_edges) {
    int index_edge;
    BM_ITER_MESH_INDEX (e, &eiter, bm, BM_EDGES_OF_MESH, index_edge) {
      BMLoop *l_a, *l_b;

      BM_elem_index_set(e, index_edge); /* set_inline */
      BM_elem_flag_disable(e, BM_ELEM_TAG);
      if (BM_edge_loop_pair(e, &l_a, &l_b)) {
        if (BM_elem_flag_test(e, BM_ELEM_SMOOTH) && l_a->v != l_b->v) {
          BM_elem_flag_enable(e, BM_ELEM_TAG);
        }
      }
    }
    bm->elem_index_dirty &= ~BM_EDGE;
  }

  int index_face, index_loop = 0;
  BM_ITER_MESH_INDEX (f, &fiter, bm, BM_FACES_OF_MESH, index_face) {
    BM_elem_index_set(f, index_face); /* set_inline */
    l_curr = l_first = BM_FACE_FIRST_LOOP(f);
    do {
      BM_elem_index_set(l_curr, index_loop++); /* set_inline */
      BM_elem_flag_disable(l_curr, BM_ELEM_TAG);
    } while ((l_curr = l_curr->next) != l_first);
  }
  bm->elem_index_dirty &= ~(BM_FACE | BM_LOOP);
}

/**
 * Helpers for #BM_mesh_loop_normals_update and #BM_loops_calc_normal_vcos
 */
static void bm_mesh_edges_sharp_tag(BMesh *bm,
                                    const float (*fnos)[3],
                                    float split_angle_cos,
                                    const bool do_sharp_edges_tag)
{
  BMIter eiter;
  BMEdge *e;
  int i;

  if (fnos) {
    BM_mesh_elem_index_ensure(bm, BM_FACE);
  }

  if (do_sharp_edges_tag) {
    BM_ITER_MESH_INDEX (e, &eiter, bm, BM_EDGES_OF_MESH, i) {
      BM_elem_index_set(e, i); /* set_inline */
      if (e->l != NULL) {
        bm_edge_tag_from_smooth_and_set_sharp(fnos, e, split_angle_cos);
      }
    }
  }
  else {
    BM_ITER_MESH_INDEX (e, &eiter, bm, BM_EDGES_OF_MESH, i) {
      BM_elem_index_set(e, i); /* set_inline */
      if (e->l != NULL) {
        bm_edge_tag_from_smooth(fnos, e, split_angle_cos);
      }
    }
  }

  bm->elem_index_dirty &= ~BM_EDGE;
}

void BM_edges_sharp_from_angle_set(BMesh *bm, const float split_angle)
{
  if (split_angle >= (float)M_PI) {
    /* Nothing to do! */
    return;
  }

  bm_mesh_edges_sharp_tag(bm, NULL, cosf(split_angle), true);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Loop Normals Calculation API
 * \{ */

bool BM_loop_check_cyclic_smooth_fan(BMLoop *l_curr)
{
  BMLoop *lfan_pivot_next = l_curr;
  BMEdge *e_next = l_curr->e;

  BLI_assert(!BM_elem_flag_test(lfan_pivot_next, BM_ELEM_TAG));
  BM_elem_flag_enable(lfan_pivot_next, BM_ELEM_TAG);

  while (true) {
    /* Much simpler than in sibling code with basic Mesh data! */
    lfan_pivot_next = BM_vert_step_fan_loop(lfan_pivot_next, &e_next);

    if (!lfan_pivot_next || !BM_elem_flag_test(e_next, BM_ELEM_TAG)) {
      /* Sharp loop/edge, so not a cyclic smooth fan... */
      return false;
    }
    /* Smooth loop/edge... */
    if (BM_elem_flag_test(lfan_pivot_next, BM_ELEM_TAG)) {
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
    BM_elem_flag_enable(lfan_pivot_next, BM_ELEM_TAG);
  }
}

/**
 * Called for all faces loops.
 *
 * - All loops must have #BM_ELEM_TAG cleared.
 * - Loop indices must be valid.
 *
 * \note When custom normals are present, the order of loops can be important.
 * Loops with lower indices must be passed before loops with higher indices (for each vertex).
 * This is needed since the first loop sets the reference point for the custom normal offsets.
 *
 * \return The number of loops that were handled (for early exit when all have been handled).
 */
static int bm_mesh_loops_calc_normals_for_loop(BMesh *bm,
                                               const float (*vcos)[3],
                                               const float (*fnos)[3],
                                               const short (*clnors_data)[2],
                                               const int cd_loop_clnors_offset,
                                               const bool has_clnors,
                                               /* Cache. */
                                               BLI_Stack *edge_vectors,
                                               /* Iterate. */
                                               BMLoop *l_curr,
                                               /* Result. */
                                               float (*r_lnos)[3],
                                               MLoopNorSpaceArray *r_lnors_spacearr)
{
  BLI_assert((bm->elem_index_dirty & BM_LOOP) == 0);
  BLI_assert((fnos == NULL) || ((bm->elem_index_dirty & BM_FACE) == 0));
  BLI_assert((vcos == NULL) || ((bm->elem_index_dirty & BM_VERT) == 0));
  UNUSED_VARS_NDEBUG(bm);

  int handled = 0;

  /* Temp normal stack. */
  BLI_SMALLSTACK_DECLARE(normal, float *);
  /* Temp clnors stack. */
  BLI_SMALLSTACK_DECLARE(clnors, short *);
  /* Temp edge vectors stack, only used when computing lnor spacearr. */

  /* A smooth edge, we have to check for cyclic smooth fan case.
   * If we find a new, never-processed cyclic smooth fan, we can do it now using that loop/edge
   * as 'entry point', otherwise we can skip it. */

  /* NOTE: In theory, we could make bm_mesh_loop_check_cyclic_smooth_fan() store
   * mlfan_pivot's in a stack, to avoid having to fan again around
   * the vert during actual computation of clnor & clnorspace. However, this would complicate
   * the code, add more memory usage, and
   * BM_vert_step_fan_loop() is quite cheap in term of CPU cycles,
   * so really think it's not worth it. */
  if (BM_elem_flag_test(l_curr->e, BM_ELEM_TAG) &&
