#include "mem_guardedalloc.h"

#include "lib_listbase.h"
#include "lib_math_vector.h"
#include "lib_mempool.h"
#include "lib_stack.h"
#include "lib_utildefines_iter.h"

#include "mesh.h"

#include "mesh_edgeloop.h" /* own include */

typedef struct MeshEdgeLoopStore {
  struct MeshEdgeLoopStore *next, *prev;
  ListBase verts;
  int flag;
  int len;
  /* optional values  to calc */
  float co[3], no[3];
} MeshEdgeLoopStore;

#define MESH_EDGELOOP_IS_CLOSED (1 << 0)

/* Use a small value since we need normals even for very small loops. */
#define EDGELOOP_EPS 1e-10f

/* -------------------------------------------------------------------- */
/* mesh_edgeloops_find & Util Functions. */

static int mesh_vert_other_tag(MeshVert *v, MeshVert *v_prev, BMEdge **r_e)
{
  MeshIter iter;
  MeshEdge *e, *e_next = NULL;
  uint count = 0;

  MESH_ITER_ELEM (e, &iter, v, MESH_EDGES_OF_VERT) {
    if (mesh_elem_flag_test(e, MESH_ELEM_INTERNAL_TAG)) {
      MeshVert *v_other = mesh_edge_other_vert(e, v);
      if (v_other != v_prev) {
        e_next = e;
        count++;
      }
    }
  }

  *r_e = e_next;
  return count;
}

/** return success **/
static bool mesh_loop_build(MeshEdgeLoopStore *el_store, MeshVert *v_prev, MeshVert *v, int dir)
{
  void (*add_fn)(ListBase *, void *) = dir == 1 ? lib_addhead : lib_addtail;
  MeshEdge *e_next;
  MeshVert *v_next;
  MeshVert *v_first = v;

  lib_assert(abs(dir) == 1);

  if (!mesh_elem_flag_test(v, MESH_ELEM_INTERNAL_TAG)) {
    return true;
  }

  while (v) {
    LinkData *node = mem_callocn(sizeof(*node), __func__);
    int count;
    node->data = v;
    add_fn(&el_store->verts, node);
    el_store->len++;
    mesh_elem_flag_disable(v, MESH_ELEM_INTERNAL_TAG);

    count = mesh_vert_other_tag(v, v_prev, &e_next);
    if (count == 1) {
      v_next = mesh_edge_other_vert(e_next, v);
      mesh_elem_flag_disable(e_next, MESH_ELEM_INTERNAL_TAG);
      if (UNLIKELY(v_next == v_first)) {
        el_store->flag |= MESH_EDGELOOP_IS_CLOSED;
        v_next = NULL;
      }
    }
    else if (count == 0) {
      /* pass */
      v_next = NULL;
    }
    else {
      v_next = NULL;
      return false;
    }

    v_prev = v;
    v = v_next;
  }

  return true;
}

int mesh_edgeloops_find(Mesh *mesh,
                       ListBase *r_eloops,
                       bool (*test_fn)(MeshEdge *, void *user_data),
                       void *user_data)
{
  MeshIter iter;
  MeshEdge *e;
  MeshVert *v;
  int count = 0;

  MESH_ITER_MESH (v, &iter, mesh, BM_VERTS_OF_MESH) {
    mesh_elem_flag_disable(v, BM_ELEM_INTERNAL_TAG);
  }

  /* first flush edges to tags, and tag verts */
  lib_Stack *edge_stack = lib_stack_new(sizeof(MeshEdge *), __func__);
  MESH_ITER_MESH (e, &iter, mesh, MESH_EDGES_OF_MESH) {
    lib_assert(!mesh_elem_flag_test(e, MESH_ELEM_INTERNAL_TAG));
    if (test_fn(e, user_data)) {
      mesh_elem_flag_enable(e, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_enable(e->v1, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_enable(e->v2, MESH_ELEM_INTERNAL_TAG);
      lib_stack_push(edge_stack, (void *)&e);
    }
    else {
      mesh_elem_flag_disable(e, MESH_ELEM_INTERNAL_TAG);
    }
  }

  const uint edges_len = lib_stack_count(edge_stack);
  MeshEdge **edges = mem_mallocn(sizeof(*edges) * edges_len, __func__);
  lib_stack_pop_n_reverse(edge_stack, edges, lib_stack_count(edge_stack));
  lib_stack_free(edge_stack);

  for (uint i = 0; i < edges_len; i += 1) {
    e = edges[i];
    if (mesh_elem_flag_test(e, MESH_ELEM_INTERNAL_TAG)) {
      MeshEdgeLoopStore *el_store = mem_callocn(sizeof(MeshEdgeLoopStore), __func__);

      /* add both directions */
      if (mesh_loop_build(el_store, e->v1, e->v2, 1) && mesh_loop_build(el_store, e->v2, e->v1, -1) &&
          el_store->len > 1) {
        lib_addtail(r_eloops, el_store);
        count++;
      }
      else {
        mesh_edgeloop_free(el_store);
      }
    }
  }

  for (uint i = 0; i < edges_len; i += 1) {
    e = edges[i];
    mesh_elem_flag_disable(e, MESH_ELEM_INTERNAL_TAG);
    mesh_elem_flag_disable(e->v1, MESH_ELEM_INTERNAL_TAG);
    mesh_elem_flag_disable(e->v2, MESH_ELEM_INTERNAL_TAG);
  }

  mem_freen(edges);
  return count;
}

/* -------------------------------------------------------------------- */
/* mesh_edgeloops_find_path & Util Functions. */

/**
 * Find s single, open edge loop - given 2 vertices.
 * Add to
 */
struct VertStep {
  struct VertStep *next, *prev;
  MeshVert *v;
};

static void vs_add(
    lib_mempool *vs_pool, ListBase *lb, BMVert *v, BMEdge *e_prev, const int iter_tot)
{
  struct VertStep *vs_new = lib_mempool_alloc(vs_pool);
  vs_new->v = v;

  mesh_elem_index_set(v, iter_tot); /* set_dirty */

  /* This edge stores a direct path back to the original vertex so we can
   * backtrack without having to store an array of previous verts. */

  /* WARNING: Setting the edge is not common practice but currently harmless, take care. */
  lib_assert(mesh_vert_in_edge(e_prev, v));
  v->e = e_prev;

  lib_addtail(lb, vs_new);
}

static bool mesh_loop_path_build_step(lib_mempool *vs_pool,
                                    ListBase *lb,
                                    const int dir,
                                    MeshVert *v_match[2])
{
  ListBase lb_tmp = {NULL, NULL};
  struct VertStep *vs, *vs_next;
  lib_assert(abs(dir) == 1);

  for (vs = lb->first; vs; vs = vs_next) {
    MeshIter iter;
    MeshEdge *e;
    /* these values will be the same every iteration */
    const int vs_iter_tot = mesh_elem_index_get(vs->v);
    const int vs_iter_next = vs_iter_tot + dir;

    vs_next = vs->next;

    MESH_ITER_ELEM (e, &iter, vs->v, MESH_EDGES_OF_VERT) {
      if (M_elem_flag_test(e, MESH_ELEM_INTERNAL_TAG)) {
        MeshVert *v_next = mesh_edge_other_vert(e, vs->v);
        const int v_next_index = mesh_elem_index_get(v_next);
        /* not essential to clear flag but prevents more checking next time round */
        mesh_elem_flag_disable(e, MESH_ELEM_INTERNAL_TAG);
        if (v_next_index == 0) {
          vs_add(vs_pool, &lb_tmp, v_next, e, vs_iter_next);
        }
        else if ((dir < 0) == (v_next_index < 0)) {
          /* on the same side - do nothing */
        }
        else {
          /* we have met out match! (vertices from different sides meet) */
          if (dir == 1) {
            v_match[0] = vs->v;
            v_match[1] = v_next;
          }
          else {
            v_match[0] = v_next;
            v_match[1] = vs->v;
          }
          /* normally we would manage memory of remaining items in (lb, lb_tmp),
           * but search is done, vs_pool will get destroyed immediately */
          return true;
        }
      }
    }

    lib_mempool_free(vs_pool, vs);
  }

  /* Commented because used in a loop, and this flag has already been set. */
  /* mesh->elem_index_dirty |= MESH_VERT; */

  /* lb is now full of free'd items, overwrite */
  *lb = lb_tmp;

  return (lib_listbase_is_empty(lb) == false);
}

bool mesh_edgeloops_find_path(Mesh *mesh,
                              ListBase *r_eloops,
                              bool (*test_fn)(MeshEdge *, void *user_data),
                              void *user_data,
                              MeshVert *v_src,
                              MeshVert *v_dst)
{
  MeshIter iter;
  MeshEdge *e;
  bool found = false;

  lib_assert(v_src != v_dst);

  {
    MeshVert *v;
    MESH_ITER_MESH (v, &iter, mesh, MESH_VERTS_OF_MESH) {
      mesh_elem_index_set(v, 0);
      mesh_elem_flag_disable(v, MESH_ELEM_INTERNAL_TAG);
    }
  }
  mesh->elem_index_dirty |= MESH_VERT;

  /* first flush edges to tags, and tag verts */
  int edges_len;
  MeshEdge **edges;

  if (test_fn) {
    LibStack *edge_stack = lib_stack_new(sizeof(MeshEdge *), __func__);
    MESH_ITER_MESH (e, &iter, mesh, MESH_EDGES_OF_MESH) {
      if (test_fn(e, user_data)) {
        M_elem_flag_enable(e, MESH_ELEM_INTERNAL_TAG);
        M_elem_flag_enable(e->v1, MESH_ELEM_INTERNAL_TAG);
        M_elem_flag_enable(e->v2, MESH_ELEM_INTERNAL_TAG);
        lib_stack_push(edge_stack, (void *)&e);
      }
      else {
        mesh_elem_flag_disable(e, MESH_ELEM_INTERNAL_TAG);
      }
    }
    edges_len = lib_stack_count(edge_stack);
    edges = mem_mallocn(sizeof(*edges) * edges_len, __func__);
    lib_stack_pop_n_reverse(edge_stack, edges, lib_stack_count(edge_stack));
    lib_stack_free(edge_stack);
  }
  else {
    int i = 0;
    edges_len = mesh->totedge;
    edges = mem_mallocn(sizeof(*edges) * edges_len, __func__);

    MESH_ITER_MESH_INDEX (e, &iter, mesh, MESH_EDGES_OF_MESH, i) {
      mesh_elem_flag_enable(e, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_enable(e->v1, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_enable(e->v2, MESH_ELEM_INTERNAL_TAG);
      edges[i] = e;
    }
  }

  /* prime the lists and begin search */
  {
    MeehVert *v_match[2] = {NULL, NULL};
    ListBase lb_src = {NULL, NULL};
    ListBase lb_dst = {NULL, NULL};
    lib_mempool *vs_pool = lib_mempool_create(sizeof(struct VertStep), 0, 512, BLI_MEMPOOL_NOP);

    /* edge args are dummy */
    vs_add(vs_pool, &lb_src, v_src, v_src->e, 1);
    vs_add(vs_pool, &lb_dst, v_dst, v_dst->e, -1);
    mesh->elem_index_dirty |= MESH_VERT;

    do {
      if ((mesh_loop_path_build_step(vs_pool, &lb_src, 1, v_match) == false) || v_match[0]) {
        break;
      }
      if ((mesh_loop_path_build_step(vs_pool, &lb_dst, -1, v_match) == false) || v_match[0]) {
        break;
      }
    } while (true);

    lib_mempool_destroy(vs_pool);

    if (v_match[0]) {
      MeshEdgeLoopStore *el_store = mem_callocn(sizeof(MeshEdgeLoopStore), __func__);
      MeshVert *v;

      /* build loop from edge pointers */
      v = v_match[0];
      while (true) {
        LinkData *node = mem_callocn(sizeof(*node), __func__);
        node->data = v;
        lib_addhead(&el_store->verts, node);
        el_store->len++;
        if (v == v_src) {
          break;
        }
        v = mesh_edge_other_vert(v->e, v);
      }

      v = v_match[1];
      while (true) {
        LinkData *node = mem_callocn(sizeof(*node), __func__);
        node->data = v;
        lib_addtail(&el_store->verts, node);
        el_store->len++;
        if (v == v_dst) {
          break;
        }
        v = BM_edge_other_vert(v->e, v);
      }

      BLI_addtail(r_eloops, el_store);

      found = true;
    }
  }

  for (uint i = 0; i < edges_len; i += 1) {
    e = edges[i];
    mesh_elem_flag_disable(e, M_ELEM_INTERNAL_TAG);
    mesh_elem_flag_disable(e->v1, M_ELEM_INTERNAL_TAG);
    mesh_elem_flag_disable(e->v2, M_ELEM_INTERNAL_TAG);
  }
  mem_freen(edges);

  return found;
}

/* -------------------------------------------------------------------- */
/* mesh_edgeloops_xxx utility function */

void mesh_edgeloops_free(ListBase *eloops)
{
  MeshEdgeLoopStore *el_store;
  while ((el_store = lib_pophead(eloops))) {
    mesh_edgeloop_free(el_store);
  }
}

void mesh_edgeloops_calc_center(Mesh *mesh, ListBase *eloops)
{
  MeshEdgeLoopStore *el_store;
  for (el_store = eloops->first; el_store; el_store = el_store->next) {
    mesh_edgeloop_calc_center(mesh, el_store);
  }
}

void BM_mesh_edgeloops_calc_normal(BMesh *bm, ListBase *eloops)
{
  BMEdgeLoopStore *el_store;
  for (el_store = eloops->first; el_store; el_store = el_store->next) {
    BM_edgeloop_calc_normal(bm, el_store);
  }
}

void BM_mesh_edgeloops_calc_normal_aligned(BMesh *bm, ListBase *eloops, const float no_align[3])
{
  BMEdgeLoopStore *el_store;
  for (el_store = eloops->first; el_store; el_store = el_store->next) {
    BM_edgeloop_calc_normal_aligned(bm, el_store, no_align);
  }
}

void BM_mesh_edgeloops_calc_order(BMesh *UNUSED(bm), ListBase *eloops, const bool use_normals)
{
  ListBase eloops_ordered = {NULL};
  BMEdgeLoopStore *el_store;
  float cent[3];
  int tot = 0;
  zero_v3(cent);
  /* assumes we calculated centers already */
  for (el_store = eloops->first; el_store; el_store = el_store->next, tot++) {
    add_v3_v3(cent, el_store->co);
  }
  mul_v3_fl(cent, 1.0f / (float)tot);

  /* Find the furthest out loop. */
  {
    BMEdgeLoopStore *el_store_best = NULL;
    float len_best_sq = -1.0f;
    for (el_store = eloops->first; el_store; el_store = el_store->next) {
      const float len_sq = len_squared_v3v3(cent, el_store->co);
      if (len_sq > len_best_sq) {
        len_best_sq = len_sq;
        el_store_best = el_store;
      }
    }

    BLI_remlink(eloops, el_store_best);
    BLI_addtail(&eloops_ordered, el_store_best);
  }

  /* not so efficient re-ordering */
  while (eloops->first) {
    BMEdgeLoopStore *el_store_best = NULL;
    const float *co = ((BMEdgeLoopStore *)eloops_ordered.last)->co;
    const float *no = ((BMEdgeLoopStore *)eloops_ordered.last)->no;
    float len_best_sq = FLT_MAX;

    if (use_normals) {
      BLI_ASSERT_UNIT_V3(no);
    }

    for (el_store = eloops->first; el_store; el_store = el_store->next) {
      float len_sq;
      if (use_normals) {
        /* scale the length by how close the loops are to pointing at eachother */
        float dir[3];
        sub_v3_v3v3(dir, co, el_store->co);
        len_sq = normalize_v3(dir);
        len_sq = len_sq *
                 ((1.0f - fabsf(dot_v3v3(dir, no))) + (1.0f - fabsf(dot_v3v3(dir, el_store->no))));
      }
      else {
        len_sq = len_squared_v3v3(co, el_store->co);
      }

      if (len_sq < len_best_sq) {
        len_best_sq = len_sq;
        el_store_best = el_store;
      }
    }

    BLI_remlink(eloops, el_store_best);
    BLI_addtail(&eloops_ordered, el_store_best);
  }

  *eloops = eloops_ordered;
}

/* -------------------------------------------------------------------- */
/* BM_edgeloop_*** functions */

BMEdgeLoopStore *BM_edgeloop_copy(BMEdgeLoopStore *el_store)
{
  BMEdgeLoopStore *el_store_copy = MEM_mallocN(sizeof(*el_store), __func__);
  *el_store_copy = *el_store;
  BLI_duplicatelist(&el_store_copy->verts, &el_store->verts);
  return el_store_copy;
}

BMEdgeLoopStore *BM_edgeloop_from_verts(BMVert **v_arr, const int v_arr_tot, bool is_closed)
{
  BMEdgeLoopStore *el_store = MEM_callocN(sizeof(*el_store), __func__);
  int i;
  for (i = 0; i < v_arr_tot; i++) {
    LinkData *node = MEM_callocN(sizeof(*node), __func__);
    node->data = v_arr[i];
    BLI_addtail(&el_store->verts, node);
  }
  el_store->len = v_arr_tot;
  if (is_closed) {
    el_store->flag |= BM_EDGELOOP_IS_CLOSED;
  }
  return el_store;
}
void BM_edgeloop_free(BMEdgeLoopStore *el_store)
{
  BLI_freelistN(&el_store->verts);
  MEM_freeN(el_store);
}

bool BM_edgeloop_is_closed(BMEdgeLoopStore *el_store)
{
  return (el_store->flag & BM_EDGELOOP_IS_CLOSED) != 0;
}

ListBase *BM_edgeloop_verts_get(BMEdgeLoopStore *el_store)
{
  return &el_store->verts;
}

int BM_edgeloop_length_get(BMEdgeLoopStore *el_store)
{
  return el_store->len;
}

const float *BM_edgeloop_normal_get(struct BMEdgeLoopStore *el_store)
{
  return el_store->no;
}

const float *BM_edgeloop_center_get(struct BMEdgeLoopStore *el_store)
{
  return el_store->co;
}

#define NODE_AS_V(n) ((BMVert *)((LinkData *)n)->data)
#define NODE_AS_CO(n) ((BMVert *)((LinkData *)n)->data)->co

void BM_edgeloop_edges_get(struct BMEdgeLoopStore *el_store, BMEdge **e_arr)
{
  LinkData *node;
  int i = 0;
  for (node = el_store->verts.first; node && node->next; node = node->next) {
    e_arr[i++] = BM_edge_exists(NODE_AS_V(node), NODE_AS_V(node->next));
    BLI_assert(e_arr[i - 1] != NULL);
  }

  if (el_store->flag & BM_EDGELOOP_IS_CLOSED) {
    e_arr[i] = BM_edge_exists(NODE_AS_V(el_store->verts.first), NODE_AS_V(el_store->verts.last));
    BLI_assert(e_arr[i] != NULL);
  }
  BLI_assert(el_store->len == i + 1);
}

void BM_edgeloop_calc_center(BMesh *UNUSED(bm), BMEdgeLoopStore *el_store)
{
  LinkData *node_curr = el_store->verts.last;
  LinkData *node_prev = ((LinkData *)el_store->verts.last)->prev;
  LinkData *node_first = el_store->verts.first;
  LinkData *node_next = node_first;

  const float *v_prev = NODE_AS_CO(node_prev);
  const float *v_curr = NODE_AS_CO(node_curr);
  const float *v_next = NODE_AS_CO(node_next);

  float totw = 0.0f;
  float w_prev;

  zero_v3(el_store->co);

  w_prev = len_v3v3(v_prev, v_curr);
  do {
    const float w_curr = len_v3v3(v_curr, v_next);
    const float w = (w_curr + w_prev);
    madd_v3_v3fl(el_store->co, v_curr, w);
    totw += w;
    w_prev = w_curr;

    node_prev = node_curr;
    node_curr = node_next;
    node_next = node_next->next;

    if (node_next == NULL) {
      break;
    }
    v_prev = v_curr;
    v_curr = v_next;
    v_next = NODE_AS_CO(node_next);
  } while (1);

  if (totw != 0.0f) {
    mul_v3_fl(el_store->co, 1.0f / (float)totw);
  }
}

bool BM_edgeloop_calc_normal(BMesh *UNUSED(bm), BMEdgeLoopStore *el_store)
{
  LinkData *node_curr = el_store->verts.first;
  const float *v_prev = NODE_AS_CO(el_store->verts.last);
  const float *v_curr = NODE_AS_CO(node_curr);

  zero_v3(el_store->no);

  /* Newell's Method */
  do {
    add_newell_cross_v3_v3v3(el_store->no, v_prev, v_curr);

    if ((node_curr = node_curr->next)) {
      v_prev = v_curr;
      v_curr = NODE_AS_CO(node_curr);
    }
    else {
      break;
    }
  } while (true);

  if (UNLIKELY(normalize_v3(el_store->no) < EDGELOOP_EPS)) {
    el_store->no[2] = 1.0f; /* other axis set to 0.0 */
    return false;
  }
  return true;
}

bool BM_edgeloop_calc_normal_aligned(BMesh *UNUSED(bm),
                                     BMEdgeLoopStore *el_store,
                                     const float no_align[3])
{
  LinkData *node_curr = el_store->verts.first;
  const float *v_prev = NODE_AS_CO(el_store->verts.last);
  const float *v_curr = NODE_AS_CO(node_curr);

  zero_v3(el_store->no);

  /* Own Method */
  do {
    float cross[3], no[3], dir[3];
    sub_v3_v3v3(dir, v_curr, v_prev);
    cross_v3_v3v3(cross, no_align, dir);
    cross_v3_v3v3(no, dir, cross);
    add_v3_v3(el_store->no, no);

    if ((node_curr = node_curr->next)) {
      v_prev = v_curr;
      v_curr = NODE_AS_CO(node_curr);
    }
    else {
      break;
    }
  } while (true);

  if (UNLIKELY(normalize_v3(el_store->no) < EDGELOOP_EPS)) {
    el_store->no[2] = 1.0f; /* other axis set to 0.0 */
    return false;
  }
  return true;
}

void BM_edgeloop_flip(BMesh *UNUSED(bm), BMEdgeLoopStore *el_store)
{
  negate_v3(el_store->no);
  BLI_listbase_reverse(&el_store->verts);
}

void BM_edgeloop_expand(
    BMesh *bm, BMEdgeLoopStore *el_store, int el_store_len, bool split, GSet *split_edges)
{
  bool split_swap = true;

#define EDGE_SPLIT(node_copy, node_other) \
  { \
    BMVert *v_split, *v_other = (node_other)->data; \
    BMEdge *e_split, *e_other = BM_edge_exists((node_copy)->data, v_other); \
    v_split = BM_edge_split( \
        bm, e_other, split_swap ? (node_copy)->data : v_other, &e_split, 0.0f); \
    v_split->e = e_split; \
    BLI_assert(v_split == e_split->v2); \
    BLI_gset_insert(split_edges, e_split); \
    (node_copy)->data = v_split; \
  } \
  ((void)0)

  /* first double until we are more than half as big */
  while ((el_store->len * 2) < el_store_len) {
    LinkData *node_curr = el_store->verts.first;
    while (node_curr) {
      LinkData *node_curr_copy = MEM_dupallocN(node_curr);
      if (split == false) {
        BLI_insertlinkafter(&el_store->verts, node_curr, node_curr_copy);
        node_curr = node_curr_copy->next;
      }
      else {
        if (node_curr->next || (el_store->flag & BM_EDGELOOP_IS_CLOSED)) {
          EDGE_SPLIT(node_curr_copy,
                     node_curr->next ? node_curr->next : (LinkData *)el_store->verts.first);
          BLI_insertlinkafter(&el_store->verts, node_curr, node_curr_copy);
          node_curr = node_curr_copy->next;
        }
        else {
          EDGE_SPLIT(node_curr_copy, node_curr->prev);
          BLI_insertlinkbefore(&el_store->verts, node_curr, node_curr_copy);
          node_curr = node_curr->next;
        }
        split_swap = !split_swap;
      }
      el_store->len++;
    }
    split_swap = !split_swap;
  }

  if (el_store->len < el_store_len) {
    LinkData *node_curr = el_store->verts.first;

    int iter_prev = 0;
    BLI_FOREACH_SPARSE_RANGE (el_store->len, (el_store_len - el_store->len), iter) {
      while (iter_prev < iter) {
        node_curr = node_curr->next;
        iter_prev += 1;
      }

      LinkData *node_curr_copy;
      node_curr_copy = MEM_dupallocN(node_curr);
      if (split == false) {
        BLI_insertlinkafter(&el_store->verts, node_curr, node_curr_copy);
        node_curr = node_curr_copy->next;
      }
      else {
        if (node_curr->next || (el_store->flag & BM_EDGELOOP_IS_CLOSED)) {
          EDGE_SPLIT(node_curr_copy,
                     node_curr->next ? node_curr->next : (LinkData *)el_store->verts.first);
          BLI_insertlinkafter(&el_store->verts, node_curr, node_curr_copy);
          node_curr = node_curr_copy->next;
        }
        else {
          EDGE_SPLIT(node_curr_copy, node_curr->prev);
          BLI_insertlinkbefore(&el_store->verts, node_curr, node_curr_copy);
          node_curr = node_curr->next;
        }
        split_swap = !split_swap;
      }
      el_store->len++;
      iter_prev += 1;
    }
  }

#undef BKE_FOREACH_SUBSET_OF_RANGE
#undef EDGE_SPLIT

  BLI_assert(el_store->len == el_store_len);
}

bool BM_edgeloop_overlap_check(struct BMEdgeLoopStore *el_store_a,
                               struct BMEdgeLoopStore *el_store_b)
{
  LinkData *node;

  /* A little more efficient if 'a' as smaller. */
  if (el_store_a->len > el_store_b->len) {
    SWAP(BMEdgeLoopStore *, el_store_a, el_store_b);
  }

  /* init */
  for (node = el_store_a->verts.first; node; node = node->next) {
    BM_elem_flag_enable((BMVert *)node->data, BM_ELEM_INTERNAL_TAG);
  }
  for (node = el_store_b->verts.first; node; node = node->next) {
    BM_elem_flag_disable((BMVert *)node->data, BM_ELEM_INTERNAL_TAG);
  }

  /* Check 'a' (clear as we go). */
  for (node = el_store_a->verts.first; node; node = node->next) {
    if (!BM_elem_flag_test((BMVert *)node->data, BM_ELEM_INTERNAL_TAG)) {
      /* Finish clearing 'a', leave tag clean. */
      while ((node = node->next)) {
        BM_elem_flag_disable((BMVert *)node->data, BM_ELEM_INTERNAL_TAG);
      }
      return true;
    }
    mesh_elem_flag_disable((BMVert *)node->data, BM_ELEM_INTERNAL_TAG);
  }
  return false;
}
