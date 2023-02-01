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
