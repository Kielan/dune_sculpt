#include "MEM_guardedalloc.h"

#include "BLI_array.h"
#include "BLI_bitmap.h"
#include "BLI_heap.h"
#include "BLI_linklist.h"
#include "BLI_linklist_stack.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_math_bits.h"
#include "BLI_rand.h"
#include "BLI_string.h"
#include "BLI_utildefines_stack.h"

#include "BKE_context.h"
#include "BKE_customdata.h"
#include "BKE_deform.h"
#include "BKE_editmesh.h"
#include "BKE_layer.h"
#include "BKE_report.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "ED_mesh.h"
#include "ED_object.h"
#include "ED_screen.h"
#include "ED_select_utils.h"
#include "ED_transform.h"
#include "ED_view3d.h"

#include "BLT_translation.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"

#include "UI_resources.h"

#include "bmesh_tools.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "DRW_select_buffer.h"

#include "mesh_intern.h" /* own include */

/* use bmesh operator flags for a few operators */
#define BMO_ELE_TAG 1

/* -------------------------------------------------------------------- */
/** \name Select Mirror
 * \{ */

void EDBM_select_mirrored(BMEditMesh *em,
                          const Mesh *me,
                          const int axis,
                          const bool extend,
                          int *r_totmirr,
                          int *r_totfail)
{
  BMesh *bm = em->bm;
  BMIter iter;
  int totmirr = 0;
  int totfail = 0;
  bool use_topology = me->editflag & ME_EDIT_MIRROR_TOPO;

  *r_totmirr = *r_totfail = 0;

  /* select -> tag */
  if (bm->selectmode & SCE_SELECT_VERTEX) {
    BMVert *v;
    BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
      BM_elem_flag_set(v, BM_ELEM_TAG, BM_elem_flag_test(v, BM_ELEM_SELECT));
    }
  }
  else if (em->selectmode & SCE_SELECT_EDGE) {
    BMEdge *e;
    BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
      BM_elem_flag_set(e, BM_ELEM_TAG, BM_elem_flag_test(e, BM_ELEM_SELECT));
    }
  }
  else {
    BMFace *f;
    BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
      BM_elem_flag_set(f, BM_ELEM_TAG, BM_elem_flag_test(f, BM_ELEM_SELECT));
    }
  }

  EDBM_verts_mirror_cache_begin(em, axis, true, true, false, use_topology);

  if (!extend) {
    EDBM_flag_disable_all(em, BM_ELEM_SELECT);
  }

  if (bm->selectmode & SCE_SELECT_VERTEX) {
    BMVert *v;
    BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
      if (!BM_elem_flag_test(v, BM_ELEM_HIDDEN) && BM_elem_flag_test(v, BM_ELEM_TAG)) {
        BMVert *v_mirr = EDBM_verts_mirror_get(em, v);
        if (v_mirr && !BM_elem_flag_test(v_mirr, BM_ELEM_HIDDEN)) {
          BM_vert_select_set(bm, v_mirr, true);
          totmirr++;
        }
        else {
          totfail++;
        }
      }
    }
  }
  else if (em->selectmode & SCE_SELECT_EDGE) {
    BMEdge *e;
    BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
      if (!BM_elem_flag_test(e, BM_ELEM_HIDDEN) && BM_elem_flag_test(e, BM_ELEM_TAG)) {
        BMEdge *e_mirr = EDBM_verts_mirror_get_edge(em, e);
        if (e_mirr && !BM_elem_flag_test(e_mirr, BM_ELEM_HIDDEN)) {
          BM_edge_select_set(bm, e_mirr, true);
          totmirr++;
        }
        else {
          totfail++;
        }
      }
    }
  }
  else {
    BMFace *f;
    BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
      if (!BM_elem_flag_test(f, BM_ELEM_HIDDEN) && BM_elem_flag_test(f, BM_ELEM_TAG)) {
        BMFace *f_mirr = EDBM_verts_mirror_get_face(em, f);
        if (f_mirr && !BM_elem_flag_test(f_mirr, BM_ELEM_HIDDEN)) {
          BM_face_select_set(bm, f_mirr, true);
          totmirr++;
        }
        else {
          totfail++;
        }
      }
    }
  }

  EDBM_verts_mirror_cache_end(em);

  *r_totmirr = totmirr;
  *r_totfail = totfail;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Back-Buffer OpenGL Selection
 * \{ */

static BMElem *edbm_select_id_bm_elem_get(Base **bases, const uint sel_id, uint *r_base_index)
{
  uint elem_id;
  char elem_type = 0;
  bool success = DRW_select_buffer_elem_get(sel_id, &elem_id, r_base_index, &elem_type);

  if (success) {
    Object *obedit = bases[*r_base_index]->object;
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    switch (elem_type) {
      case SCE_SELECT_FACE:
        return (BMElem *)BM_face_at_index_find_or_table(em->bm, elem_id);
      case SCE_SELECT_EDGE:
        return (BMElem *)BM_edge_at_index_find_or_table(em->bm, elem_id);
      case SCE_SELECT_VERTEX:
        return (BMElem *)BM_vert_at_index_find_or_table(em->bm, elem_id);
      default:
        BLI_assert(0);
        return NULL;
    }
  }

  return NULL;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Find Nearest Vert/Edge/Face
 *
 * \note Screen-space manhattan distances are used here,
 * since its faster and good enough for the purpose of selection.
 *
 * \note \a dist_bias is used so we can bias against selected items.
 * when choosing between elements of a single type, but return the real distance
 * to avoid the bias interfering with distance comparisons when mixing types.
 * \{ */

#define FIND_NEAR_SELECT_BIAS 5
#define FIND_NEAR_CYCLE_THRESHOLD_MIN 3

struct NearestVertUserData_Hit {
  float dist;
  float dist_bias;
  int index;
  BMVert *vert;
};

struct NearestVertUserData {
  float mval_fl[2];
  bool use_select_bias;
  bool use_cycle;
  int cycle_index_prev;

  struct NearestVertUserData_Hit hit;
  struct NearestVertUserData_Hit hit_cycle;
};

static void findnearestvert__doClosest(void *userData,
                                       BMVert *eve,
                                       const float screen_co[2],
                                       int index)
{
  struct NearestVertUserData *data = userData;
  float dist_test, dist_test_bias;

  dist_test = dist_test_bias = len_manhattan_v2v2(data->mval_fl, screen_co);

  if (data->use_select_bias && BM_elem_flag_test(eve, BM_ELEM_SELECT)) {
    dist_test_bias += FIND_NEAR_SELECT_BIAS;
  }

  if (dist_test_bias < data->hit.dist_bias) {
    data->hit.dist_bias = dist_test_bias;
    data->hit.dist = dist_test;
    data->hit.index = index;
    data->hit.vert = eve;
  }

  if (data->use_cycle) {
    if ((data->hit_cycle.vert == NULL) && (index > data->cycle_index_prev) &&
        (dist_test_bias < FIND_NEAR_CYCLE_THRESHOLD_MIN)) {
      data->hit_cycle.dist_bias = dist_test_bias;
      data->hit_cycle.dist = dist_test;
      data->hit_cycle.index = index;
      data->hit_cycle.vert = eve;
    }
  }
}

BMVert *EDBM_vert_find_nearest_ex(ViewContext *vc,
                                  float *dist_px_manhattan_p,
                                  const bool use_select_bias,
                                  bool use_cycle,
                                  Base **bases,
                                  uint bases_len,
                                  uint *r_base_index)
{
  uint base_index = 0;

  if (!XRAY_FLAG_ENABLED(vc->v3d)) {
    uint dist_px_manhattan_test = (uint)ED_view3d_backbuf_sample_size_clamp(vc->region,
                                                                            *dist_px_manhattan_p);
    uint index;
    BMVert *eve;

    /* No afterqueue (yet), so we check it now, otherwise the bm_xxxofs indices are bad. */
    {
      DRW_select_buffer_context_create(bases, bases_len, SCE_SELECT_VERTEX);

      index = DRW_select_buffer_find_nearest_to_point(
          vc->depsgraph, vc->region, vc->v3d, vc->mval, 1, UINT_MAX, &dist_px_manhattan_test);

      if (index) {
        eve = (BMVert *)edbm_select_id_bm_elem_get(bases, index, &base_index);
      }
      else {
        eve = NULL;
      }
    }

    if (eve) {
      if (dist_px_manhattan_test < *dist_px_manhattan_p) {
        if (r_base_index) {
          *r_base_index = base_index;
        }
        *dist_px_manhattan_p = dist_px_manhattan_test;
        return eve;
      }
    }
    return NULL;
  }

  struct NearestVertUserData data = {{0}};
  const struct NearestVertUserData_Hit *hit = NULL;
  const eV3DProjTest clip_flag = RV3D_CLIPPING_ENABLED(vc->v3d, vc->rv3d) ?
                                     V3D_PROJ_TEST_CLIP_DEFAULT :
                                     V3D_PROJ_TEST_CLIP_DEFAULT & ~V3D_PROJ_TEST_CLIP_BB;
  BMesh *prev_select_bm = NULL;

  static struct {
    int index;
    const BMVert *elem;
    const BMesh *bm;
  } prev_select = {0};

  data.mval_fl[0] = vc->mval[0];
  data.mval_fl[1] = vc->mval[1];
  data.use_select_bias = use_select_bias;
  data.use_cycle = use_cycle;

  for (; base_index < bases_len; base_index++) {
    Base *base_iter = bases[base_index];
    ED_view3d_viewcontext_init_object(vc, base_iter->object);
    if (use_cycle && prev_select.bm == vc->em->bm &&
        prev_select.elem == BM_vert_at_index_find_or_table(vc->em->bm, prev_select.index)) {
      data.cycle_index_prev = prev_select.index;
      /* No need to compare in the rest of the loop. */
      use_cycle = false;
    }
    else {
      data.cycle_index_prev = 0;
    }

    data.hit.dist = data.hit_cycle.dist = data.hit.dist_bias = data.hit_cycle.dist_bias =
        *dist_px_manhattan_p;

    ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);
    mesh_foreachScreenVert(vc, findnearestvert__doClosest, &data, clip_flag);

    hit = (data.use_cycle && data.hit_cycle.vert) ? &data.hit_cycle : &data.hit;

    if (hit->dist < *dist_px_manhattan_p) {
      if (r_base_index) {
        *r_base_index = base_index;
      }
      *dist_px_manhattan_p = hit->dist;
      prev_select_bm = vc->em->bm;
    }
  }

  if (hit == NULL) {
    return NULL;
  }

  prev_select.index = hit->index;
  prev_select.elem = hit->vert;
  prev_select.bm = prev_select_bm;

  return hit->vert;
}

BMVert *EDBM_vert_find_nearest(ViewContext *vc, float *dist_px_manhattan_p)
{
  Base *base = BKE_view_layer_base_find(vc->view_layer, vc->obact);
  return EDBM_vert_find_nearest_ex(vc, dist_px_manhattan_p, false, false, &base, 1, NULL);
}

/* find the distance to the edge we already have */
struct NearestEdgeUserData_ZBuf {
  float mval_fl[2];
  float dist;
  const BMEdge *edge_test;
};

static void find_nearest_edge_center__doZBuf(void *userData,
                                             BMEdge *eed,
                                             const float screen_co_a[2],
                                             const float screen_co_b[2],
                                             int UNUSED(index))
{
  struct NearestEdgeUserData_ZBuf *data = userData;

  if (eed == data->edge_test) {
    float dist_test;
    float screen_co_mid[2];

    mid_v2_v2v2(screen_co_mid, screen_co_a, screen_co_b);
    dist_test = len_manhattan_v2v2(data->mval_fl, screen_co_mid);

    if (dist_test < data->dist) {
      data->dist = dist_test;
    }
  }
}

struct NearestEdgeUserData_Hit {
  float dist;
  float dist_bias;
  int index;
  BMEdge *edge;

  /* edges only, un-biased manhattan distance to which ever edge we pick
   * (not used for choosing) */
  float dist_center_px_manhattan;
};

struct NearestEdgeUserData {
  ViewContext vc;
  float mval_fl[2];
  bool use_select_bias;
  bool use_cycle;
  int cycle_index_prev;

  struct NearestEdgeUserData_Hit hit;
  struct NearestEdgeUserData_Hit hit_cycle;
};

/* NOTE: uses v3d, so needs active 3d window. */
static void find_nearest_edge__doClosest(
    void *userData, BMEdge *eed, const float screen_co_a[2], const float screen_co_b[2], int index)
{
  struct NearestEdgeUserData *data = userData;
  float dist_test, dist_test_bias;

  float fac = line_point_factor_v2(data->mval_fl, screen_co_a, screen_co_b);
  float screen_co[2];

  if (fac <= 0.0f) {
    fac = 0.0f;
    copy_v2_v2(screen_co, screen_co_a);
  }
  else if (fac >= 1.0f) {
    fac = 1.0f;
    copy_v2_v2(screen_co, screen_co_b);
  }
  else {
    interp_v2_v2v2(screen_co, screen_co_a, screen_co_b, fac);
  }

  dist_test = dist_test_bias = len_manhattan_v2v2(data->mval_fl, screen_co);

  if (data->use_select_bias && BM_elem_flag_test(eed, BM_ELEM_SELECT)) {
    dist_test_bias += FIND_NEAR_SELECT_BIAS;
  }

  if (data->vc.rv3d->rflag & RV3D_CLIPPING) {
    float vec[3];

    interp_v3_v3v3(vec, eed->v1->co, eed->v2->co, fac);
    if (ED_view3d_clipping_test(data->vc.rv3d, vec, true)) {
      return;
    }
  }

  if (dist_test_bias < data->hit.dist_bias) {
    float screen_co_mid[2];

    data->hit.dist_bias = dist_test_bias;
    data->hit.dist = dist_test;
    data->hit.index = index;
    data->hit.edge = eed;

    mid_v2_v2v2(screen_co_mid, screen_co_a, screen_co_b);
    data->hit.dist_center_px_manhattan = len_manhattan_v2v2(data->mval_fl, screen_co_mid);
  }

  if (data->use_cycle) {
    if ((data->hit_cycle.edge == NULL) && (index > data->cycle_index_prev) &&
        (dist_test_bias < FIND_NEAR_CYCLE_THRESHOLD_MIN)) {
      float screen_co_mid[2];

      data->hit_cycle.dist_bias = dist_test_bias;
      data->hit_cycle.dist = dist_test;
      data->hit_cycle.index = index;
      data->hit_cycle.edge = eed;

      mid_v2_v2v2(screen_co_mid, screen_co_a, screen_co_b);
      data->hit_cycle.dist_center_px_manhattan = len_manhattan_v2v2(data->mval_fl, screen_co_mid);
    }
  }
}

BMEdge *EDBM_edge_find_nearest_ex(ViewContext *vc,
                                  float *dist_px_manhattan_p,
                                  float *r_dist_center_px_manhattan,
                                  const bool use_select_bias,
                                  bool use_cycle,
                                  BMEdge **r_eed_zbuf,
                                  Base **bases,
                                  uint bases_len,
                                  uint *r_base_index)
{
  uint base_index = 0;

  if (!XRAY_FLAG_ENABLED(vc->v3d)) {
    uint dist_px_manhattan_test = (uint)ED_view3d_backbuf_sample_size_clamp(vc->region,
                                                                            *dist_px_manhattan_p);
    uint index;
    BMEdge *eed;

    /* No afterqueue (yet), so we check it now, otherwise the bm_xxxofs indices are bad. */
    {
      DRW_select_buffer_context_create(bases, bases_len, SCE_SELECT_EDGE);

      index = DRW_select_buffer_find_nearest_to_point(
          vc->depsgraph, vc->region, vc->v3d, vc->mval, 1, UINT_MAX, &dist_px_manhattan_test);

      if (index) {
        eed = (BMEdge *)edbm_select_id_bm_elem_get(bases, index, &base_index);
      }
      else {
        eed = NULL;
      }
    }

    if (r_eed_zbuf) {
      *r_eed_zbuf = eed;
    }

    /* exception for faces (verts don't need this) */
    if (r_dist_center_px_manhattan && eed) {
      struct NearestEdgeUserData_ZBuf data;

      data.mval_fl[0] = vc->mval[0];
      data.mval_fl[1] = vc->mval[1];
      data.dist = FLT_MAX;
      data.edge_test = eed;

      ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

      mesh_foreachScreenEdge(vc,
                             find_nearest_edge_center__doZBuf,
                             &data,
                             V3D_PROJ_TEST_CLIP_DEFAULT | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);

      *r_dist_center_px_manhattan = data.dist;
    }
    /* end exception */

    if (eed) {
      if (dist_px_manhattan_test < *dist_px_manhattan_p) {
        if (r_base_index) {
          *r_base_index = base_index;
        }
        *dist_px_manhattan_p = dist_px_manhattan_test;
        return eed;
      }
    }
    return NULL;
  }

  struct NearestEdgeUserData data = {{0}};
  const struct NearestEdgeUserData_Hit *hit = NULL;
  /* interpolate along the edge before doing a clipping plane test */
  const eV3DProjTest clip_flag = V3D_PROJ_TEST_CLIP_DEFAULT & ~V3D_PROJ_TEST_CLIP_BB;
  BMesh *prev_select_bm = NULL;

  static struct {
    int index;
    const BMEdge *elem;
    const BMesh *bm;
  } prev_select = {0};

  data.vc = *vc;
  data.mval_fl[0] = vc->mval[0];
  data.mval_fl[1] = vc->mval[1];
  data.use_select_bias = use_select_bias;
  data.use_cycle = use_cycle;

  for (; base_index < bases_len; base_index++) {
    Base *base_iter = bases[base_index];
    ED_view3d_viewcontext_init_object(vc, base_iter->object);
    if (use_cycle && prev_select.bm == vc->em->bm &&
        prev_select.elem == BM_edge_at_index_find_or_table(vc->em->bm, prev_select.index)) {
      data.cycle_index_prev = prev_select.index;
      /* No need to compare in the rest of the loop. */
      use_cycle = false;
    }
    else {
      data.cycle_index_prev = 0;
    }

    data.hit.dist = data.hit_cycle.dist = data.hit.dist_bias = data.hit_cycle.dist_bias =
        *dist_px_manhattan_p;

    ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);
    mesh_foreachScreenEdge(
        vc, find_nearest_edge__doClosest, &data, clip_flag | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);

    hit = (data.use_cycle && data.hit_cycle.edge) ? &data.hit_cycle : &data.hit;

    if (hit->dist < *dist_px_manhattan_p) {
      if (r_base_index) {
        *r_base_index = base_index;
      }
      *dist_px_manhattan_p = hit->dist;
      prev_select_bm = vc->em->bm;
    }
  }

  if (hit == NULL) {
    return NULL;
  }

  if (r_dist_center_px_manhattan) {
    *r_dist_center_px_manhattan = hit->dist_center_px_manhattan;
  }

  prev_select.index = hit->index;
  prev_select.elem = hit->edge;
  prev_select.bm = prev_select_bm;

  return hit->edge;
}

BMEdge *EDBM_edge_find_nearest(ViewContext *vc, float *dist_px_manhattan_p)
{
  Base *base = BKE_view_layer_base_find(vc->view_layer, vc->obact);
  return EDBM_edge_find_nearest_ex(
      vc, dist_px_manhattan_p, NULL, false, false, NULL, &base, 1, NULL);
}

/* find the distance to the face we already have */
struct NearestFaceUserData_ZBuf {
  float mval_fl[2];
  float dist_px_manhattan;
  const BMFace *face_test;
};

static void find_nearest_face_center__doZBuf(void *userData,
                                             BMFace *efa,
                                             const float screen_co[2],
                                             int UNUSED(index))
{
  struct NearestFaceUserData_ZBuf *data = userData;

  if (efa == data->face_test) {
    const float dist_test = len_manhattan_v2v2(data->mval_fl, screen_co);

    if (dist_test < data->dist_px_manhattan) {
      data->dist_px_manhattan = dist_test;
    }
  }
}

struct NearestFaceUserData_Hit {
  float dist;
  float dist_bias;
  int index;
  BMFace *face;
};

struct NearestFaceUserData {
  float mval_fl[2];
  bool use_select_bias;
  bool use_cycle;
  int cycle_index_prev;

  struct NearestFaceUserData_Hit hit;
  struct NearestFaceUserData_Hit hit_cycle;
};

static void findnearestface__doClosest(void *userData,
                                       BMFace *efa,
                                       const float screen_co[2],
                                       int index)
{
  struct NearestFaceUserData *data = userData;
  float dist_test, dist_test_bias;

  dist_test = dist_test_bias = len_manhattan_v2v2(data->mval_fl, screen_co);

  if (data->use_select_bias && BM_elem_flag_test(efa, BM_ELEM_SELECT)) {
    dist_test_bias += FIND_NEAR_SELECT_BIAS;
  }

  if (dist_test_bias < data->hit.dist_bias) {
    data->hit.dist_bias = dist_test_bias;
    data->hit.dist = dist_test;
    data->hit.index = index;
    data->hit.face = efa;
  }

  if (data->use_cycle) {
    if ((data->hit_cycle.face == NULL) && (index > data->cycle_index_prev) &&
        (dist_test_bias < FIND_NEAR_CYCLE_THRESHOLD_MIN)) {
      data->hit_cycle.dist_bias = dist_test_bias;
      data->hit_cycle.dist = dist_test;
      data->hit_cycle.index = index;
      data->hit_cycle.face = efa;
    }
  }
}

BMFace *EDBM_face_find_nearest_ex(ViewContext *vc,
                                  float *dist_px_manhattan_p,
                                  float *r_dist_center,
                                  const bool use_zbuf_single_px,
                                  const bool use_select_bias,
                                  bool use_cycle,
                                  BMFace **r_efa_zbuf,
                                  Base **bases,
                                  uint bases_len,
                                  uint *r_base_index)
{
  uint base_index = 0;

  if (!XRAY_FLAG_ENABLED(vc->v3d)) {
    float dist_test;
    uint index;
    BMFace *efa;

    {
      uint dist_px_manhattan_test = 0;
      if (*dist_px_manhattan_p != 0.0f && (use_zbuf_single_px == false)) {
        dist_px_manhattan_test = (uint)ED_view3d_backbuf_sample_size_clamp(vc->region,
                                                                           *dist_px_manhattan_p);
      }

      DRW_select_buffer_context_create(bases, bases_len, SCE_SELECT_FACE);

      if (dist_px_manhattan_test == 0) {
        index = DRW_select_buffer_sample_point(vc->depsgraph, vc->region, vc->v3d, vc->mval);
        dist_test = 0.0f;
      }
      else {
        index = DRW_select_buffer_find_nearest_to_point(
            vc->depsgraph, vc->region, vc->v3d, vc->mval, 1, UINT_MAX, &dist_px_manhattan_test);
        dist_test = dist_px_manhattan_test;
      }

      if (index) {
        efa = (BMFace *)edbm_select_id_bm_elem_get(bases, index, &base_index);
      }
      else {
        efa = NULL;
      }
    }

    if (r_efa_zbuf) {
      *r_efa_zbuf = efa;
    }

    /* exception for faces (verts don't need this) */
    if (r_dist_center && efa) {
      struct NearestFaceUserData_ZBuf data;

      data.mval_fl[0] = vc->mval[0];
      data.mval_fl[1] = vc->mval[1];
      data.dist_px_manhattan = FLT_MAX;
      data.face_test = efa;

      ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

      mesh_foreachScreenFace(
          vc, find_nearest_face_center__doZBuf, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

      *r_dist_center = data.dist_px_manhattan;
    }
    /* end exception */

    if (efa) {
      if (dist_test < *dist_px_manhattan_p) {
        if (r_base_index) {
          *r_base_index = base_index;
        }
        *dist_px_manhattan_p = dist_test;
        return efa;
      }
    }
    return NULL;
  }

  struct NearestFaceUserData data = {{0}};
  const struct NearestFaceUserData_Hit *hit = NULL;
  const eV3DProjTest clip_flag = V3D_PROJ_TEST_CLIP_DEFAULT;
  BMesh *prev_select_bm = NULL;

  static struct {
    int index;
    const BMFace *elem;
    const BMesh *bm;
  } prev_select = {0};

  data.mval_fl[0] = vc->mval[0];
  data.mval_fl[1] = vc->mval[1];
  data.use_select_bias = use_select_bias;
  data.use_cycle = use_cycle;

  for (; base_index < bases_len; base_index++) {
    Base *base_iter = bases[base_index];
    ED_view3d_viewcontext_init_object(vc, base_iter->object);
    if (use_cycle && prev_select.bm == vc->em->bm &&
        prev_select.elem == BM_face_at_index_find_or_table(vc->em->bm, prev_select.index)) {
      data.cycle_index_prev = prev_select.index;
      /* No need to compare in the rest of the loop. */
      use_cycle = false;
    }
    else {
      data.cycle_index_prev = 0;
    }

    data.hit.dist = data.hit_cycle.dist = data.hit.dist_bias = data.hit_cycle.dist_bias =
        *dist_px_manhattan_p;

    ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);
    mesh_foreachScreenFace(vc, findnearestface__doClosest, &data, clip_flag);

    hit = (data.use_cycle && data.hit_cycle.face) ? &data.hit_cycle : &data.hit;

    if (hit->dist < *dist_px_manhattan_p) {
      if (r_base_index) {
        *r_base_index = base_index;
      }
      *dist_px_manhattan_p = hit->dist;
      prev_select_bm = vc->em->bm;
    }
  }

  if (hit == NULL) {
    return NULL;
  }

  if (r_dist_center) {
    *r_dist_center = hit->dist;
  }

  prev_select.index = hit->index;
  prev_select.elem = hit->face;
  prev_select.bm = prev_select_bm;

  return hit->face;
}

BMFace *EDBM_face_find_nearest(ViewContext *vc, float *dist_px_manhattan_p)
{
  Base *base = BKE_view_layer_base_find(vc->view_layer, vc->obact);
  return EDBM_face_find_nearest_ex(
      vc, dist_px_manhattan_p, NULL, false, false, false, NULL, &base, 1, NULL);
}

#undef FIND_NEAR_SELECT_BIAS
#undef FIND_NEAR_CYCLE_THRESHOLD_MIN

/* best distance based on screen coords.
 * use em->selectmode to define how to use
 * selected vertices and edges get disadvantage
 * return 1 if found one
 */
static bool unified_findnearest(ViewContext *vc,
                                Base **bases,
                                const uint bases_len,
                                int *r_base_index,
                                BMVert **r_eve,
                                BMEdge **r_eed,
                                BMFace **r_efa)
{
  BMEditMesh *em = vc->em;

  const bool use_cycle = !WM_cursor_test_motion_and_update(vc->mval);
  const float dist_init = ED_view3d_select_dist_px();
  /* since edges select lines, we give dots advantage of ~20 pix */
  const float dist_margin = (dist_init / 2);
  float dist = dist_init;

  struct {
    struct {
      BMVert *ele;
      int base_index;
    } v;
    struct {
      BMEdge *ele;
      int base_index;
    } e, e_zbuf;
    struct {
      BMFace *ele;
      int base_index;
    } f, f_zbuf;
  } hit = {{NULL}};

  /* no afterqueue (yet), so we check it now, otherwise the em_xxxofs indices are bad */

  if ((dist > 0.0f) && (em->selectmode & SCE_SELECT_FACE)) {
    float dist_center = 0.0f;
    float *dist_center_p = (em->selectmode & (SCE_SELECT_EDGE | SCE_SELECT_VERTEX)) ?
                               &dist_center :
                               NULL;

    uint base_index = 0;
    BMFace *efa_zbuf = NULL;
    BMFace *efa_test = EDBM_face_find_nearest_ex(
        vc, &dist, dist_center_p, true, true, use_cycle, &efa_zbuf, bases, bases_len, &base_index);

    if (efa_test && dist_center_p) {
      dist = min_ff(dist_margin, dist_center);
    }
    if (efa_test) {
      hit.f.base_index = base_index;
      hit.f.ele = efa_test;
    }
    if (efa_zbuf) {
      hit.f_zbuf.base_index = base_index;
      hit.f_zbuf.ele = efa_zbuf;
    }
  }

  if ((dist > 0.0f) && (em->selectmode & SCE_SELECT_EDGE)) {
    float dist_center = 0.0f;
    float *dist_center_p = (em->selectmode & SCE_SELECT_VERTEX) ? &dist_center : NULL;

    uint base_index = 0;
    BMEdge *eed_zbuf = NULL;
    BMEdge *eed_test = EDBM_edge_find_nearest_ex(
        vc, &dist, dist_center_p, true, use_cycle, &eed_zbuf, bases, bases_len, &base_index);

    if (eed_test && dist_center_p) {
      dist = min_ff(dist_margin, dist_center);
    }
    if (eed_test) {
      hit.e.base_index = base_index;
      hit.e.ele = eed_test;
    }
    if (eed_zbuf) {
      hit.e_zbuf.base_index = base_index;
      hit.e_zbuf.ele = eed_zbuf;
    }
  }

  if ((dist > 0.0f) && (em->selectmode & SCE_SELECT_VERTEX)) {
    uint base_index = 0;
    BMVert *eve_test = EDBM_vert_find_nearest_ex(
        vc, &dist, true, use_cycle, bases, bases_len, &base_index);

    if (eve_test) {
      hit.v.base_index = base_index;
      hit.v.ele = eve_test;
    }
  }

  /* Return only one of 3 pointers, for front-buffer redraws. */
  if (hit.v.ele) {
    hit.f.ele = NULL;
    hit.e.ele = NULL;
  }
  else if (hit.e.ele) {
    hit.f.ele = NULL;
  }

  /* there may be a face under the cursor, who's center if too far away
   * use this if all else fails, it makes sense to select this */
  if ((hit.v.ele || hit.e.ele || hit.f.ele) == 0) {
    if (hit.e_zbuf.ele) {
      hit.e.base_index = hit.e_zbuf.base_index;
      hit.e.ele = hit.e_zbuf.ele;
    }
    else if (hit.f_zbuf.ele) {
      hit.f.base_index = hit.f_zbuf.base_index;
      hit.f.ele = hit.f_zbuf.ele;
    }
  }

  /* Only one element type will be non-null. */
  BLI_assert(((hit.v.ele != NULL) + (hit.e.ele != NULL) + (hit.f.ele != NULL)) <= 1);

  if (hit.v.ele) {
    *r_base_index = hit.v.base_index;
  }
  if (hit.e.ele) {
    *r_base_index = hit.e.base_index;
  }
  if (hit.f.ele) {
    *r_base_index = hit.f.base_index;
  }

  *r_eve = hit.v.ele;
  *r_eed = hit.e.ele;
  *r_efa = hit.f.ele;

  return (hit.v.ele || hit.e.ele || hit.f.ele);
}

#undef FAKE_SELECT_MODE_BEGIN
#undef FAKE_SELECT_MODE_END

bool EDBM_unified_findnearest(ViewContext *vc,
                              Base **bases,
                              const uint bases_len,
                              int *r_base_index,
                              BMVert **r_eve,
                              BMEdge **r_eed,
                              BMFace **r_efa)
{
  return unified_findnearest(vc, bases, bases_len, r_base_index, r_eve, r_eed, r_efa);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Alternate Find Nearest Vert/Edge (optional boundary)
 *
 * \note This uses ray-cast method instead of back-buffer,
 * currently used for poly-build.
 * \{ */

bool EDBM_unified_findnearest_from_raycast(ViewContext *vc,
                                           Base **bases,
                                           const uint bases_len,
                                           bool use_boundary_vertices,
                                           bool use_boundary_edges,
                                           int *r_base_index_vert,
                                           int *r_base_index_edge,
                                           int *r_base_index_face,
                                           struct BMVert **r_eve,
                                           struct BMEdge **r_eed,
                                           struct BMFace **r_efa)
{

  const float mval_fl[2] = {UNPACK2(vc->mval)};
  float ray_origin[3], ray_direction[3];

  struct {
    uint base_index;
    BMElem *ele;
  } best = {0, NULL};
  /* Currently unused, keep since we may want to pick the best. */
  UNUSED_VARS(best);

  struct {
    uint base_index;
    BMElem *ele;
  } best_vert = {0, NULL};

  struct {
    uint base_index;
    BMElem *ele;
  } best_edge = {0, NULL};

  struct {
    uint base_index;
    BMElem *ele;
  } best_face = {0, NULL};

  if (ED_view3d_win_to_ray_clipped(
          vc->depsgraph, vc->region, vc->v3d, mval_fl, ray_origin, ray_direction, true)) {
    float dist_sq_best = FLT_MAX;
    float dist_sq_best_vert = FLT_MAX;
    float dist_sq_best_edge = FLT_MAX;
    float dist_sq_best_face = FLT_MAX;

    const bool use_vert = (r_eve != NULL);
    const bool use_edge = (r_eed != NULL);
    const bool use_face = (r_efa != NULL);

    for (uint base_index = 0; base_index < bases_len; base_index++) {
      Base *base_iter = bases[base_index];
      Object *obedit = base_iter->object;

      BMEditMesh *em = BKE_editmesh_from_object(obedit);
      BMesh *bm = em->bm;
      float imat3[3][3];

      ED_view3d_viewcontext_init_object(vc, obedit);
      copy_m3_m4(imat3, obedit->obmat);
      invert_m3(imat3);

      const float(*coords)[3] = NULL;
      {
        Mesh *me_eval = (Mesh *)DEG_get_evaluated_id(vc->depsgraph, obedit->data);
        if (me_eval->runtime.edit_data) {
          coords = me_eval->runtime.edit_data->vertexCos;
        }
      }

      if (coords != NULL) {
        BM_mesh_elem_index_ensure(bm, BM_VERT);
      }

      if ((use_boundary_vertices || use_boundary_edges) && (use_vert || use_edge)) {
        BMEdge *e;
        BMIter eiter;
        BM_ITER_MESH (e, &eiter, bm, BM_EDGES_OF_MESH) {
          if ((BM_elem_flag_test(e, BM_ELEM_HIDDEN) == false) && (BM_edge_is_boundary(e))) {
            if (use_vert && use_boundary_vertices) {
              for (uint j = 0; j < 2; j++) {
                BMVert *v = *((&e->v1) + j);
                float point[3];
                mul_v3_m4v3(point, obedit->obmat, coords ? coords[BM_elem_index_get(v)] : v->co);
                const float dist_sq_test = dist_squared_to_ray_v3_normalized(
                    ray_origin, ray_direction, point);
                if (dist_sq_test < dist_sq_best_vert) {
                  dist_sq_best_vert = dist_sq_test;
                  best_vert.base_index = base_index;
                  best_vert.ele = (BMElem *)v;
                }
                if (dist_sq_test < dist_sq_best) {
                  dist_sq_best = dist_sq_test;
                  best.base_index = base_index;
                  best.ele = (BMElem *)v;
                }
              }
            }

            if (use_edge && use_boundary_edges) {
              float point[3];
#if 0
              const float dist_sq_test = dist_squared_ray_to_seg_v3(
                  ray_origin, ray_direction, e->v1->co, e->v2->co, point, &depth);
#else
              if (coords) {
                mid_v3_v3v3(
                    point, coords[BM_elem_index_get(e->v1)], coords[BM_elem_index_get(e->v2)]);
              }
              else {
                mid_v3_v3v3(point, e->v1->co, e->v2->co);
              }
              mul_m4_v3(obedit->obmat, point);
              const float dist_sq_test = dist_squared_to_ray_v3_normalized(
                  ray_origin, ray_direction, point);
              if (dist_sq_test < dist_sq_best_edge) {
                dist_sq_best_edge = dist_sq_test;
                best_edge.base_index = base_index;
                best_edge.ele = (BMElem *)e;
              }
              if (dist_sq_test < dist_sq_best) {
                dist_sq_best = dist_sq_test;
                best.base_index = base_index;
                best.ele = (BMElem *)e;
              }
#endif
            }
          }
        }
      }
      /* Non boundary case. */
      if (use_vert && !use_boundary_vertices) {
        BMVert *v;
        BMIter viter;
        BM_ITER_MESH (v, &viter, bm, BM_VERTS_OF_MESH) {
          if (BM_elem_flag_test(v, BM_ELEM_HIDDEN) == false) {
            float point[3];
            mul_v3_m4v3(point, obedit->obmat, coords ? coords[BM_elem_index_get(v)] : v->co);
            const float dist_sq_test = dist_squared_to_ray_v3_normalized(
                ray_origin, ray_direction, point);
            if (dist_sq_test < dist_sq_best_vert) {
              dist_sq_best_vert = dist_sq_test;
              best_vert.base_index = base_index;
              best_vert.ele = (BMElem *)v;
            }
            if (dist_sq_test < dist_sq_best) {
              dist_sq_best = dist_sq_test;
              best.base_index = base_index;
              best.ele = (BMElem *)v;
            }
          }
        }
      }

      if (use_edge && !use_boundary_edges) {
        BMEdge *e;
        BMIter eiter;
        BM_ITER_MESH (e, &eiter, bm, BM_EDGES_OF_MESH) {
          if (BM_elem_flag_test(e, BM_ELEM_HIDDEN) == false) {
            float point[3];
            if (coords) {
              mid_v3_v3v3(
                  point, coords[BM_elem_index_get(e->v1)], coords[BM_elem_index_get(e->v2)]);
            }
            else {
              mid_v3_v3v3(point, e->v1->co, e->v2->co);
            }
            mul_m4_v3(obedit->obmat, point);
            const float dist_sq_test = dist_squared_to_ray_v3_normalized(
                ray_origin, ray_direction, point);
            if (dist_sq_test < dist_sq_best_edge) {
              dist_sq_best_edge = dist_sq_test;
              best_edge.base_index = base_index;
              best_edge.ele = (BMElem *)e;
            }
            if (dist_sq_test < dist_sq_best) {
              dist_sq_best = dist_sq_test;
              best.base_index = base_index;
              best.ele = (BMElem *)e;
            }
          }
        }
      }

      if (use_face) {
        BMFace *f;
        BMIter fiter;
        BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
          if (BM_elem_flag_test(f, BM_ELEM_HIDDEN) == false) {
            float point[3];
            if (coords) {
              BM_face_calc_center_median_vcos(bm, f, point, coords);
            }
            else {
              BM_face_calc_center_median(f, point);
            }
            mul_m4_v3(obedit->obmat, point);
            const float dist_sq_test = dist_squared_to_ray_v3_normalized(
                ray_origin, ray_direction, point);
            if (dist_sq_test < dist_sq_best_face) {
              dist_sq_best_face = dist_sq_test;
              best_face.base_index = base_index;
              best_face.ele = (BMElem *)f;
            }
            if (dist_sq_test < dist_sq_best) {
              dist_sq_best = dist_sq_test;
              best.base_index = base_index;
              best.ele = (BMElem *)f;
            }
          }
        }
      }
    }
  }

  *r_base_index_vert = best_vert.base_index;
  *r_base_index_edge = best_edge.base_index;
  *r_base_index_face = best_face.base_index;

  if (r_eve) {
    *r_eve = NULL;
  }
  if (r_eed) {
    *r_eed = NULL;
  }
  if (r_efa) {
    *r_efa = NULL;
  }

  if (best_vert.ele) {
    *r_eve = (BMVert *)best_vert.ele;
  }
  if (best_edge.ele) {
    *r_eed = (BMEdge *)best_edge.ele;
  }
  if (best_face.ele) {
    *r_efa = (BMFace *)best_face.ele;
  }

  return (best_vert.ele != NULL || best_edge.ele != NULL || best_face.ele != NULL);
}

/* -------------------------------------------------------------------- */
/** \name Select Similar Region Operator
 * \{ */

static int edbm_select_similar_region_exec(bContext *C, wmOperator *op)
{
  Object *obedit = CTX_data_edit_object(C);
  BMEditMesh *em = BKE_editmesh_from_object(obedit);
  BMesh *bm = em->bm;
  bool changed = false;

  /* group vars */
  int *groups_array;
  int(*group_index)[2];
  int group_tot;
  int i;

  if (bm->totfacesel < 2) {
    BKE_report(op->reports, RPT_ERROR, "No face regions selected");
    return OPERATOR_CANCELLED;
  }

  groups_array = MEM_mallocN(sizeof(*groups_array) * bm->totfacesel, __func__);
  group_tot = BM_mesh_calc_face_groups(
      bm, groups_array, &group_index, NULL, NULL, NULL, BM_ELEM_SELECT, BM_VERT);

  BM_mesh_elem_table_ensure(bm, BM_FACE);

  for (i = 0; i < group_tot; i++) {
    ListBase faces_regions;
    int tot;

    const int fg_sta = group_index[i][0];
    const int fg_len = group_index[i][1];
    int j;
    BMFace **fg = MEM_mallocN(sizeof(*fg) * fg_len, __func__);

    for (j = 0; j < fg_len; j++) {
      fg[j] = BM_face_at_index(bm, groups_array[fg_sta + j]);
    }

    tot = BM_mesh_region_match(bm, fg, fg_len, &faces_regions);

    MEM_freeN(fg);

    if (tot) {
      LinkData *link;
      while ((link = BLI_pophead(&faces_regions))) {
        BMFace *f, **faces = link->data;
        while ((f = *(faces++))) {
          BM_face_select_set(bm, f, true);
        }
        MEM_freeN(link->data);
        MEM_freeN(link);

        changed = true;
      }
    }
  }

  MEM_freeN(groups_array);
  MEM_freeN(group_index);

  if (changed) {
    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }
  else {
    BKE_report(op->reports, RPT_WARNING, "No matching face regions found");
  }

  return OPERATOR_FINISHED;
}

void MESH_OT_select_similar_region(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Similar Regions";
  ot->idname = "MESH_OT_select_similar_region";
  ot->description = "Select similar face regions to the current selection";

  /* api callbacks */
  ot->exec = edbm_select_similar_region_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Mode Vert/Edge/Face Operator
 * \{ */

static int edbm_select_mode_exec(bContext *C, wmOperator *op)
{
  const int type = RNA_enum_get(op->ptr, "type");
  const int action = RNA_enum_get(op->ptr, "action");
  const bool use_extend = RNA_boolean_get(op->ptr, "use_extend");
  const bool use_expand = RNA_boolean_get(op->ptr, "use_expand");

  if (EDBM_selectmode_toggle_multi(C, type, action, use_extend, use_expand)) {
    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

static int edbm_select_mode_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  /* Bypass when in UV non sync-select mode, fall through to keymap that edits. */
  if (CTX_wm_space_image(C)) {
    ToolSettings *ts = CTX_data_tool_settings(C);
    if ((ts->uv_flag & UV_SYNC_SELECTION) == 0) {
      return OPERATOR_PASS_THROUGH;
    }
    /* Bypass when no action is needed. */
    if (!RNA_struct_property_is_set(op->ptr, "type")) {
      return OPERATOR_CANCELLED;
    }
  }

  /* detecting these options based on shift/ctrl here is weak, but it's done
   * to make this work when clicking buttons or menus */
  if (!RNA_struct_property_is_set(op->ptr, "use_extend")) {
    RNA_boolean_set(op->ptr, "use_extend", event->modifier & KM_SHIFT);
  }
  if (!RNA_struct_property_is_set(op->ptr, "use_expand")) {
    RNA_boolean_set(op->ptr, "use_expand", event->modifier & KM_CTRL);
  }

  return edbm_select_mode_exec(C, op);
}

static char *edbm_select_mode_get_description(struct bContext *UNUSED(C),
                                              struct wmOperatorType *UNUSED(op),
                                              struct PointerRNA *values)
{
  const int type = RNA_enum_get(values, "type");

  /* Because the special behavior for shift and ctrl click depend on user input, they may be
   * incorrect if the operator is used from a script or from a special button. So only return the
   * specialized descriptions if only the "type" is set, which conveys that the operator is meant
   * to be used with the logic in the `invoke` method. */
  if (RNA_struct_property_is_set(values, "type") &&
      !RNA_struct_property_is_set(values, "use_extend") &&
      !RNA_struct_property_is_set(values, "use_expand") &&
      !RNA_struct_property_is_set(values, "action")) {
    switch (type) {
      case SCE_SELECT_VERTEX:
        return BLI_strdup(TIP_(
            "Vertex select - Shift-Click for multiple modes, Ctrl-Click contracts selection"));
      case SCE_SELECT_EDGE:
        return BLI_strdup(
            TIP_("Edge select - Shift-Click for multiple modes, "
                 "Ctrl-Click expands/contracts selection depending on the current mode"));
      case SCE_SELECT_FACE:
        return BLI_strdup(
            TIP_("Face select - Shift-Click for multiple modes, Ctrl-Click expands selection"));
    }
  }

  return NULL;
}

void MESH_OT_select_mode(wmOperatorType *ot)
{
  PropertyRNA *prop;

  static const EnumPropertyItem actions_items[] = {
      {0, "DISABLE", 0, "Disable", "Disable selected markers"},
      {1, "ENABLE", 0, "Enable", "Enable selected markers"},
      {2, "TOGGLE", 0, "Toggle", "Toggle disabled flag for selected markers"},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Select Mode";
  ot->idname = "MESH_OT_select_mode";
  ot->description = "Change selection mode";

  /* api callbacks */
  ot->invoke = edbm_select_mode_invoke;
  ot->exec = edbm_select_mode_exec;
  ot->poll = ED_operator_editmesh;
  ot->get_description = edbm_select_mode_get_description;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  /* Hide all, not to show redo panel. */
  prop = RNA_def_boolean(ot->srna, "use_extend", false, "Extend", "");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "use_expand", false, "Expand", "");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
  ot->prop = prop = RNA_def_enum(ot->srna, "type", rna_enum_mesh_select_mode_items, 0, "Type", "");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  prop = RNA_def_enum(
      ot->srna, "action", actions_items, 2, "Action", "Selection action to execute");
  RNA_def_property_flag(prop, PROP_HIDDEN);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Loop (Non Modal) Operator
 * \{ */

static void walker_select_count(BMEditMesh *em,
                                int walkercode,
                                void *start,
                                int r_count_by_select[2])
{
  BMesh *bm = em->bm;
  BMElem *ele;
  BMWalker walker;

  r_count_by_select[0] = r_count_by_select[1] = 0;

  BMW_init(&walker,
           bm,
           walkercode,
           BMW_MASK_NOP,
           BMW_MASK_NOP,
           BMW_MASK_NOP,
           BMW_FLAG_TEST_HIDDEN,
           BMW_NIL_LAY);

  for (ele = BMW_begin(&walker, start); ele; ele = BMW_step(&walker)) {
    r_count_by_select[BM_elem_flag_test(ele, BM_ELEM_SELECT) ? 1 : 0] += 1;

    /* Early exit when mixed (could be optional if needed. */
    if (r_count_by_select[0] && r_count_by_select[1]) {
      r_count_by_select[0] = r_count_by_select[1] = -1;
      break;
    }
  }

  BMW_end(&walker);
}

static void walker_select(BMEditMesh *em, int walkercode, void *start, const bool select)
{
  BMesh *bm = em->bm;
  BMElem *ele;
  BMWalker walker;

  BMW_init(&walker,
           bm,
           walkercode,
           BMW_MASK_NOP,
           BMW_MASK_NOP,
           BMW_MASK_NOP,
           BMW_FLAG_TEST_HIDDEN,
           BMW_NIL_LAY);

  for (ele = BMW_begin(&walker, start); ele; ele = BMW_step(&walker)) {
    if (!select) {
      BM_select_history_remove(bm, ele);
    }
    BM_elem_select_set(bm, ele, select);
  }
  BMW_end(&walker);
}

static int edbm_loop_multiselect_exec(bContext *C, wmOperator *op)
{
  const bool is_ring = RNA_boolean_get(op->ptr, "ring");
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (em->bm->totedgesel == 0) {
      continue;
    }

    BMEdge *eed;
    BMEdge **edarray;
    int edindex;
    BMIter iter;
    int totedgesel = 0;

    BM_ITER_MESH (eed, &iter, em->bm, BM_EDGES_OF_MESH) {
      if (BM_elem_flag_test(eed, BM_ELEM_SELECT)) {
        totedgesel++;
      }
    }

    edarray = MEM_mallocN(sizeof(BMEdge *) * totedgesel, "edge array");
    edindex = 0;

    BM_ITER_MESH (eed, &iter, em->bm, BM_EDGES_OF_MESH) {
      if (BM_elem_flag_test(eed, BM_ELEM_SELECT)) {
        edarray[edindex] = eed;
        edindex++;
      }
    }

    if (is_ring) {
      for (edindex = 0; edindex < totedgesel; edindex += 1) {
        eed = edarray[edindex];
        walker_select(em, BMW_EDGERING, eed, true);
      }
      EDBM_selectmode_flush(em);
    }
    else {
      for (edindex = 0; edindex < totedgesel; edindex += 1) {
        eed = edarray[edindex];
        bool non_manifold = BM_edge_face_count_is_over(eed, 2);
        if (non_manifold) {
          walker_select(em, BMW_EDGELOOP_NONMANIFOLD, eed, true);
        }
        else {
          walker_select(em, BMW_EDGELOOP, eed, true);
        }
      }
      EDBM_selectmode_flush(em);
    }
    MEM_freeN(edarray);
    //  if (EM_texFaceCheck())

    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_loop_multi_select(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Multi Select Loops";
  ot->idname = "MESH_OT_loop_multi_select";
  ot->description = "Select a loop of connected edges by connection type";

  /* api callbacks */
  ot->exec = edbm_loop_multiselect_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna, "ring", 0, "Ring", "");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Loop (Cursor Pick) Operator
 * \{ */

static void mouse_mesh_loop_face(BMEditMesh *em, BMEdge *eed, bool select, bool select_clear)
{
  if (select_clear) {
    EDBM_flag_disable_all(em, BM_ELEM_SELECT);
  }

  walker_select(em, BMW_FACELOOP, eed, select);
}

static void mouse_mesh_loop_edge_ring(BMEditMesh *em, BMEdge *eed, bool select, bool select_clear)
{
  if (select_clear) {
    EDBM_flag_disable_all(em, BM_ELEM_SELECT);
  }

  walker_select(em, BMW_EDGERING, eed, select);
}

static void mouse_mesh_loop_edge(
    BMEditMesh *em, BMEdge *eed, bool select, bool select_clear, bool select_cycle)
{
  bool edge_boundary = false;
  bool non_manifold = BM_edge_face_count_is_over(eed, 2);

  /* Cycle between BMW_EDGELOOP / BMW_EDGEBOUNDARY. */
  if (select_cycle && BM_edge_is_boundary(eed)) {
    int count_by_select[2];

    /* If the loops selected toggle the boundaries. */
    walker_select_count(em, BMW_EDGELOOP, eed, count_by_select);
    if (count_by_select[!select] == 0) {
      edge_boundary = true;

      /* If the boundaries selected, toggle back to the loop. */
      walker_select_count(em, BMW_EDGEBOUNDARY, eed, count_by_select);
      if (count_by_select[!select] == 0) {
        edge_boundary = false;
      }
    }
  }

  if (select_clear) {
    EDBM_flag_disable_all(em, BM_ELEM_SELECT);
  }

  if (edge_boundary) {
    walker_select(em, BMW_EDGEBOUNDARY, eed, select);
  }
  else if (non_manifold) {
    walker_select(em, BMW_EDGELOOP_NONMANIFOLD, eed, select);
  }
  else {
    walker_select(em, BMW_EDGELOOP, eed, select);
  }
}

static bool mouse_mesh_loop(
    bContext *C, const int mval[2], bool extend, bool deselect, bool toggle, bool ring)
{
  Base *basact = NULL;
  BMVert *eve = NULL;
  BMEdge *eed = NULL;
  BMFace *efa = NULL;

  ViewContext vc;
  BMEditMesh *em;
  bool select = true;
  bool select_clear = false;
  bool select_cycle = true;
  float mvalf[2];

  em_setup_viewcontext(C, &vc);
  mvalf[0] = (float)(vc.mval[0] = mval[0]);
  mvalf[1] = (float)(vc.mval[1] = mval[1]);

  BMEditMesh *em_original = vc.em;
  const short selectmode = em_original->selectmode;
  em_original->selectmode = SCE_SELECT_EDGE;

  uint bases_len;
  Base **bases = BKE_view_layer_array_from_bases_in_edit_mode(vc.view_layer, vc.v3d, &bases_len);

  {
    int base_index = -1;
    if (EDBM_unified_findnearest(&vc, bases, bases_len, &base_index, &eve, &eed, &efa)) {
      basact = bases[base_index];
      ED_view3d_viewcontext_init_object(&vc, basact->object);
      em = vc.em;
    }
    else {
      em = NULL;
    }
  }

  em_original->selectmode = selectmode;

  if (em == NULL || eed == NULL) {
    MEM_freeN(bases);
    return false;
  }

  if (extend == false && deselect == false && toggle == false) {
    select_clear = true;
  }

  if (extend) {
    select = true;
  }
  else if (deselect) {
    select = false;
  }
  else if (select_clear || (BM_elem_flag_test(eed, BM_ELEM_SELECT) == 0)) {
    select = true;
  }
  else if (toggle) {
    select = false;
    select_cycle = false;
  }

  if (select_clear) {
    for (uint base_index = 0; base_index < bases_len; base_index++) {
      Base *base_iter = bases[base_index];
      Object *ob_iter = base_iter->object;
      BMEditMesh *em_iter = BKE_editmesh_from_object(ob_iter);

      if (em_iter->bm->totvertsel == 0) {
        continue;
      }

      if (em_iter == em) {
        continue;
      }

      EDBM_flag_disable_all(em_iter, BM_ELEM_SELECT);
      DEG_id_tag_update(ob_iter->data, ID_RECALC_SELECT);
    }
  }

  if (em->selectmode & SCE_SELECT_FACE) {
    mouse_mesh_loop_face(em, eed, select, select_clear);
  }
  else {
    if (ring) {
      mouse_mesh_loop_edge_ring(em, eed, select, select_clear);
    }
    else {
      mouse_mesh_loop_edge(em, eed, select, select_clear, select_cycle);
    }
  }

  EDBM_selectmode_flush(em);

  /* sets as active, useful for other tools */
  if (select) {
    if (em->selectmode & SCE_SELECT_VERTEX) {
      /* Find nearest vert from mouse
       * (initialize to large values in case only one vertex can be projected) */
      float v1_co[2], v2_co[2];
      float length_1 = FLT_MAX;
      float length_2 = FLT_MAX;

      /* We can't be sure this has already been set... */
      ED_view3d_init_mats_rv3d(vc.obedit, vc.rv3d);

      if (ED_view3d_project_float_object(vc.region, eed->v1->co, v1_co, V3D_PROJ_TEST_CLIP_NEAR) ==
          V3D_PROJ_RET_OK) {
        length_1 = len_squared_v2v2(mvalf, v1_co);
      }

      if (ED_view3d_project_float_object(vc.region, eed->v2->co, v2_co, V3D_PROJ_TEST_CLIP_NEAR) ==
          V3D_PROJ_RET_OK) {
        length_2 = len_squared_v2v2(mvalf, v2_co);
      }
#if 0
      printf("mouse to v1: %f\nmouse to v2: %f\n",
             len_squared_v2v2(mvalf, v1_co),
             len_squared_v2v2(mvalf, v2_co));
#endif
      BM_select_history_store(em->bm, (length_1 < length_2) ? eed->v1 : eed->v2);
    }
    else if (em->selectmode & SCE_SELECT_EDGE) {
      BM_select_history_store(em->bm, eed);
    }
    else if (em->selectmode & SCE_SELECT_FACE) {
      /* Select the face of eed which is the nearest of mouse. */
      BMFace *f;
      BMIter iterf;
      float best_dist = FLT_MAX;
      efa = NULL;

      /* We can't be sure this has already been set... */
      ED_view3d_init_mats_rv3d(vc.obedit, vc.rv3d);

      BM_ITER_ELEM (f, &iterf, eed, BM_FACES_OF_EDGE) {
        if (BM_elem_flag_test(f, BM_ELEM_SELECT)) {
          float cent[3];
          float co[2], tdist;

          BM_face_calc_center_median(f, cent);
          if (ED_view3d_project_float_object(vc.region, cent, co, V3D_PROJ_TEST_CLIP_NEAR) ==
              V3D_PROJ_RET_OK) {
            tdist = len_squared_v2v2(mvalf, co);
            if (tdist < best_dist) {
              // printf("Best face: %p (%f)\n", f, tdist);
              best_dist = tdist;
              efa = f;
            }
          }
        }
      }
      if (efa) {
        BM_mesh_active_face_set(em->bm, efa);
        BM_select_history_store(em->bm, efa);
      }
    }
  }

  MEM_freeN(bases);

  DEG_id_tag_update(vc.obedit->data, ID_RECALC_SELECT);
  WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc.obedit->data);

  return true;
}

static int edbm_select_loop_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{

  view3d_operator_needs_opengl(C);

  if (mouse_mesh_loop(C,
                      event->mval,
                      RNA_boolean_get(op->ptr, "extend"),
                      RNA_boolean_get(op->ptr, "deselect"),
                      RNA_boolean_get(op->ptr, "toggle"),
                      RNA_boolean_get(op->ptr, "ring"))) {
    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void MESH_OT_loop_select(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Loop Select";
  ot->idname = "MESH_OT_loop_select";
  ot->description = "Select a loop of connected edges";

  /* api callbacks */
  ot->invoke = edbm_select_loop_invoke;
  ot->poll = ED_operator_editmesh_region_view3d;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  PropertyRNA *prop;

  prop = RNA_def_boolean(ot->srna, "extend", 0, "Extend Select", "Extend the selection");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "deselect", 0, "Deselect", "Remove from the selection");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "toggle", 0, "Toggle Select", "Toggle the selection");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "ring", 0, "Select Ring", "Select ring");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

void MESH_OT_edgering_select(wmOperatorType *ot)
{
  /* description */
  ot->name = "Edge Ring Select";
  ot->idname = "MESH_OT_edgering_select";
  ot->description = "Select an edge ring";

  /* callbacks */
  ot->invoke = edbm_select_loop_invoke;
  ot->poll = ED_operator_editmesh_region_view3d;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* Properties. */
  PropertyRNA *prop;
  prop = RNA_def_boolean(ot->srna, "extend", 0, "Extend", "Extend the selection");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "deselect", 0, "Deselect", "Remove from the selection");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "toggle", 0, "Toggle Select", "Toggle the selection");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "ring", 1, "Select Ring", "Select ring");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name (De)Select All Operator
 * \{ */

static int edbm_select_all_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  int action = RNA_enum_get(op->ptr, "action");

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  if (action == SEL_TOGGLE) {
    action = SEL_SELECT;
    for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
      Object *obedit = objects[ob_index];
      BMEditMesh *em = BKE_editmesh_from_object(obedit);
      if (em->bm->totvertsel || em->bm->totedgesel || em->bm->totfacesel) {
        action = SEL_DESELECT;
        break;
      }
    }
  }

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    switch (action) {
      case SEL_SELECT:
        EDBM_flag_enable_all(em, BM_ELEM_SELECT);
        break;
      case SEL_DESELECT:
        EDBM_flag_disable_all(em, BM_ELEM_SELECT);
        break;
      case SEL_INVERT:
        EDBM_select_swap(em);
        EDBM_selectmode_flush(em);
        break;
    }
    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_select_all(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "(De)select All";
  ot->idname = "MESH_OT_select_all";
  ot->description = "(De)select all vertices, edges or faces";

  /* api callbacks */
  ot->exec = edbm_select_all_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  WM_operator_properties_select_all(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Interior Faces Operator
 * \{ */

static int edbm_faces_select_interior_exec(bContext *C, wmOperator *UNUSED(op))
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (!EDBM_select_interior_faces(em)) {
      continue;
    }

    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_select_interior_faces(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Interior Faces";
  ot->idname = "MESH_OT_select_interior_faces";
  ot->description = "Select faces where all edges have more than 2 face users";

  /* api callbacks */
  ot->exec = edbm_faces_select_interior_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Select Picking API
 *
 * Here actual select happens,
 * Gets called via generic mouse select operator.
 * \{ */

bool EDBM_select_pick(bContext *C, const int mval[2], const struct SelectPick_Params *params)
{
  ViewContext vc;

  int base_index_active = -1;
  BMVert *eve = NULL;
  BMEdge *eed = NULL;
  BMFace *efa = NULL;

  /* setup view context for argument to callbacks */
  em_setup_viewcontext(C, &vc);
  vc.mval[0] = mval[0];
  vc.mval[1] = mval[1];

  uint bases_len = 0;
  Base **bases = BKE_view_layer_array_from_bases_in_edit_mode(vc.view_layer, vc.v3d, &bases_len);

  bool changed = false;
  bool found = unified_findnearest(&vc, bases, bases_len, &base_index_active, &eve, &eed, &efa);

  if (params->sel_op == SEL_OP_SET) {
    BMElem *ele = efa ? (BMElem *)efa : (eed ? (BMElem *)eed : (BMElem *)eve);
    if ((found && params->select_passthrough) && BM_elem_flag_test(ele, BM_ELEM_SELECT)) {
      found = false;
    }
    else if (found || params->deselect_all) {
      /* Deselect everything. */
      for (uint base_index = 0; base_index < bases_len; base_index++) {
        Base *base_iter = bases[base_index];
        Object *ob_iter = base_iter->object;
        EDBM_flag_disable_all(BKE_editmesh_from_object(ob_iter), BM_ELEM_SELECT);
        DEG_id_tag_update(ob_iter->data, ID_RECALC_SELECT);
        WM_event_add_notifier(C, NC_GEOM | ND_SELECT, ob_iter->data);
      }
      changed = true;
    }
  }

  if (found) {
    Base *basact = bases[base_index_active];
    ED_view3d_viewcontext_init_object(&vc, basact->object);

    if (efa) {
      switch (params->sel_op) {
        case SEL_OP_ADD: {
          BM_mesh_active_face_set(vc.em->bm, efa);

          /* Work-around: deselect first, so we can guarantee it will
           * be active even if it was already selected. */
          BM_select_history_remove(vc.em->bm, efa);
          BM_face_select_set(vc.em->bm, efa, false);
          BM_select_history_store(vc.em->bm, efa);
          BM_face_select_set(vc.em->bm, efa, true);
          break;
        }
        case SEL_OP_SUB: {
          BM_select_history_remove(vc.em->bm, efa);
          BM_face_select_set(vc.em->bm, efa, false);
          break;
        }
        case SEL_OP_XOR: {
          BM_mesh_active_face_set(vc.em->bm, efa);
          if (!BM_elem_flag_test(efa, BM_ELEM_SELECT)) {
            BM_select_history_store(vc.em->bm, efa);
            BM_face_select_set(vc.em->bm, efa, true);
          }
          else {
            BM_select_history_remove(vc.em->bm, efa);
            BM_face_select_set(vc.em->bm, efa, false);
          }
          break;
        }
        case SEL_OP_SET: {
          BM_mesh_active_face_set(vc.em->bm, efa);
          if (!BM_elem_flag_test(efa, BM_ELEM_SELECT)) {
            BM_select_history_store(vc.em->bm, efa);
            BM_face_select_set(vc.em->bm, efa, true);
          }
          break;
        }
        case SEL_OP_AND: {
          BLI_assert_unreachable(); /* Doesn't make sense for picking. */
          break;
        }
      }
    }
    else if (eed) {

      switch (params->sel_op) {
        case SEL_OP_ADD: {
          /* Work-around: deselect first, so we can guarantee it will
           * be active even if it was already selected. */
          BM_select_history_remove(vc.em->bm, eed);
          BM_edge_select_set(vc.em->bm, eed, false);
          BM_select_history_store(vc.em->bm, eed);
          BM_edge_select_set(vc.em->bm, eed, true);
          break;
        }
        case SEL_OP_SUB: {
          BM_select_history_remove(vc.em->bm, eed);
          BM_edge_select_set(vc.em->bm, eed, false);
          break;
        }
        case SEL_OP_XOR: {
          if (!BM_elem_flag_test(eed, BM_ELEM_SELECT)) {
            BM_select_history_store(vc.em->bm, eed);
            BM_edge_select_set(vc.em->bm, eed, true);
          }
          else {
            BM_select_history_remove(vc.em->bm, eed);
            BM_edge_select_set(vc.em->bm, eed, false);
          }
          break;
        }
        case SEL_OP_SET: {
          if (!BM_elem_flag_test(eed, BM_ELEM_SELECT)) {
            BM_select_history_store(vc.em->bm, eed);
            BM_edge_select_set(vc.em->bm, eed, true);
          }
          break;
        }
        case SEL_OP_AND: {
          BLI_assert_unreachable(); /* Doesn't make sense for picking. */
          break;
        }
      }
    }
    else if (eve) {
      switch (params->sel_op) {
        case SEL_OP_ADD: {
          /* Work-around: deselect first, so we can guarantee it will
           * be active even if it was already selected. */
          BM_select_history_remove(vc.em->bm, eve);
          BM_vert_select_set(vc.em->bm, eve, false);
          BM_select_history_store(vc.em->bm, eve);
          BM_vert_select_set(vc.em->bm, eve, true);
          break;
        }
        case SEL_OP_SUB: {
          BM_select_history_remove(vc.em->bm, eve);
          BM_vert_select_set(vc.em->bm, eve, false);
          break;
        }
        case SEL_OP_XOR: {
          if (!BM_elem_flag_test(eve, BM_ELEM_SELECT)) {
            BM_select_history_store(vc.em->bm, eve);
            BM_vert_select_set(vc.em->bm, eve, true);
          }
          else {
            BM_select_history_remove(vc.em->bm, eve);
            BM_vert_select_set(vc.em->bm, eve, false);
          }
          break;
        }
        case SEL_OP_SET: {
          if (!BM_elem_flag_test(eve, BM_ELEM_SELECT)) {
            BM_select_history_store(vc.em->bm, eve);
            BM_vert_select_set(vc.em->bm, eve, true);
          }
          break;
        }
        case SEL_OP_AND: {
          BLI_assert_unreachable(); /* Doesn't make sense for picking. */
          break;
        }
      }
    }

    EDBM_selectmode_flush(vc.em);

    if (efa) {
      /* Change active material on object. */
      if (efa->mat_nr != vc.obedit->actcol - 1) {
        vc.obedit->actcol = efa->mat_nr + 1;
        vc.em->mat_nr = efa->mat_nr;
        WM_event_add_notifier(C, NC_MATERIAL | ND_SHADING_LINKS, NULL);
      }

      /* Change active face-map on object. */
      if (!BLI_listbase_is_empty(&vc.obedit->fmaps)) {
        const int cd_fmap_offset = CustomData_get_offset(&vc.em->bm->pdata, CD_FACEMAP);
        if (cd_fmap_offset != -1) {
          int map = *((int *)BM_ELEM_CD_GET_VOID_P(efa, cd_fmap_offset));
          if ((map < -1) || (map > BLI_listbase_count_at_most(&vc.obedit->fmaps, map))) {
            map = -1;
          }
          map += 1;
          if (map != vc.obedit->actfmap) {
            /* We may want to add notifiers later,
             * currently select update handles redraw. */
            vc.obedit->actfmap = map;
          }
        }
      }
    }

    /* Changing active object is handy since it allows us to
     * switch UV layers, vgroups for eg. */
    if (vc.view_layer->basact != basact) {
      ED_object_base_activate(C, basact);
    }

    DEG_id_tag_update(vc.obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc.obedit->data);

    changed = true;
  }

  MEM_freeN(bases);

  return changed;
}

