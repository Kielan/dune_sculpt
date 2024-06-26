/**
 * Beautify the mesh by rotating edges between triangles
 * to more attractive positions until no more rotations can be made.
 *
 * In principle this is very simple however there is the possibility of
 * going into an eternal loop where edges keep rotating.
 * To avoid this - each edge stores a set of it previous
 * states so as not to rotate back.
 *
 * TODO
 * - Take face normals into account.
 */

#include "lib_heap.h"
#include "lib_math.h"
#include "lib_polyfill_2d_beautify.h"

#include "mem_guardedalloc.h"

#include "mesh.h"
#include "mesh_beautify.h" /* own include */

// #define DEBUG_TIME

#ifdef DEBUG_TIME
#  include "PIL_time.h"
#  include "PIL_time_utildefines.h"
#endif

/* -------------------------------------------------------------------- */
/* GSet for edge rotation */

typedef struct EdRotState {
  /**
   * Edge vert indices (ordered small -> large).
   */
  int v_pair[2];
  /**
   * Face vert indices (small -> large).
   *
   * Each face-vertex points to a connected triangles vertex
   * that's isn't part of the edge defined by `v_pair`.
   */
  int f_pair[2];
} EdRotState;

#if 0
/* use lib_ghashutil_inthash_v4 direct */
static uint erot_gsetutil_hash(const void *ptr)
{
  const EdRotState *e_state = (const EdRotState *)ptr;
  return lib_ghashutil_inthash_v4(&e_state->v_pair[0]);
}
#endif
#if 0
static int erot_gsetutil_cmp(const void *a, const void *b)
{
  const EdRotState *e_state_a = (const EdRotState *)a;
  const EdRotState *e_state_b = (const EdRotState *)b;
  if (e_state_a->v_pair[0] < e_state_b->v_pair[0]) {
    return -1;
  }
  if (e_state_a->v_pair[0] > e_state_b->v_pair[0]) {
    return 1;
  }
  if (e_state_a->v_pair[1] < e_state_b->v_pair[1]) {
    return -1;
  }
  if (e_state_a->v_pair[1] > e_state_b->v_pair[1]) {
    return 1;
  }
  if (e_state_a->f_pair[0] < e_state_b->f_pair[0]) {
    return -1;
  }
  if (e_state_a->f_pair[0] > e_state_b->f_pair[0]) {
    return 1;
  }
  if (e_state_a->f_pair[1] < e_state_b->f_pair[1]) {
    return -1;
  }
  if (e_state_a->f_pair[1] > e_state_b->f_pair[1]) {
    return 1;
  }
  return 0;
}
#endif
static GSet *erot_gset_new(void)
{
  return lib_gset_new(lib_ghashutil_inthash_v4_p, lib_ghashutil_inthash_v4_cmp, __func__);
}

/* ensure v0 is smaller */
#define EDGE_ORD(v0, v1) \
  if (v0 > v1) { \
    SWAP(int, v0, v1); \
  } \
  (void)0

static void erot_state_ex(const MeshEdge *e, int v_index[2], int f_index[2])
{
  lib_assert(mesh_edge_is_manifold(e));
  lib_assert(mesh_vert_in_edge(e, e->l->prev->v) == false);
  lib_assert(mesh_vert_in_edge(e, e->l->radial_next->prev->v) == false);

  /* verts of the edge */
  v_index[0] = mesh_elem_index_get(e->v1);
  v_index[1] = mesh_elem_index_get(e->v2);
  EDGE_ORD(v_index[0], v_index[1]);

  /* verts of each of the 2 faces attached to this edge
   * (that are not a part of this edge) */
  f_index[0] = mesh_elem_index_get(e->l->prev->v);
  f_index[1] = mesh_elem_index_get(e->l->radial_next->prev->v);
  EDGE_ORD(f_index[0], f_index[1]);
}

static void erot_state_current(const MeshEdge *e, EdRotState *e_state)
{
  erot_state_ex(e, e_state->v_pair, e_state->f_pair);
}

static void erot_state_alternate(const MeshEdge *e, EdRotState *e_state)
{
  erot_state_ex(e, e_state->f_pair, e_state->v_pair);
}

/* -------------------------------------------------------------------- */
/* Calculate the improvement of rotating the edge */

static float mesh_edge_calc_rotate_beauty__area(const float v1[3],
                                                const float v2[3],
                                                const float v3[3],
                                                const float v4[3],
                                                const bool lock_degenerate)
{
  /* not a loop (only to be able to break out) */
  do {
    float v1_xy[2], v2_xy[2], v3_xy[2], v4_xy[2];

    /* first get the 2d values */
    {
      const float eps = 1e-5;
      float no_a[3], no_b[3];
      float no[3];
      float axis_mat[3][3];
      float no_scale;
      cross_tri_v3(no_a, v2, v3, v4);
      cross_tri_v3(no_b, v2, v4, v1);

      // printf("%p %p %p %p - %p %p\n", v1, v2, v3, v4, e->l->f, e->l->radial_next->f);
      lib_assert((ELEM(v1, v2, v3, v4) == false) && (ELEM(v2, v1, v3, v4) == false) &&
                 (ELEM(v3, v1, v2, v4) == false) && (ELEM(v4, v1, v2, v3) == false));

      add_v3_v3v3(no, no_a, no_b);
      if (UNLIKELY((no_scale = normalize_v3(no)) == 0.0f)) {
        break;
      }

      axis_dominant_v3_to_m3(axis_mat, no);
      mul_v2_m3v3(v1_xy, axis_mat, v1);
      mul_v2_m3v3(v2_xy, axis_mat, v2);
      mul_v2_m3v3(v3_xy, axis_mat, v3);
      mul_v2_m3v3(v4_xy, axis_mat, v4);

      /**
       * Check if input faces are already flipped.
       * Logic for 'signum_i' addition is:
       *
       * Accept:
       * - (1, 1) or (-1, -1): same side (common case).
       * - (-1/1, 0): one degenerate, OK since we may rotate into a valid state.
       *
       * Ignore:
       * - (-1, 1): opposite winding, ignore.
       * - ( 0, 0): both degenerate, ignore.
       *
       * \note The cross product is divided by 'no_scale'
       * so the rotation calculation is scale independent.
       */
      if (!(signum_i_ex(cross_tri_v2(v2_xy, v3_xy, v4_xy) / no_scale, eps) +
            signum_i_ex(cross_tri_v2(v2_xy, v4_xy, v1_xy) / no_scale, eps))) {
        break;
      }
    }

    /**
     * Important to lock degenerate here,
     * since the triangle pars will be projected into different 2D spaces.
     * Allowing to rotate out of a degenerate state can flip the faces
     * (when performed iteratively).
     */
    return lib_polyfill_beautify_quad_rotate_calc_ex(
        v1_xy, v2_xy, v3_xy, v4_xy, lock_degenerate, NULL);
  } while (false);

  return FLT_MAX;
}

static float mesh_edge_calc_rotate_beauty__angle(const float v1[3],
                                               const float v2[3],
                                               const float v3[3],
                                               const float v4[3])
{
  /* not a loop (only to be able to break out) */
  do {
    float no_a[3], no_b[3];
    float angle_24, angle_13;

    /* edge (2-4), current state */
    normal_tri_v3(no_a, v2, v3, v4);
    normal_tri_v3(no_b, v2, v4, v1);
    angle_24 = angle_normalized_v3v3(no_a, no_b);

    /* edge (1-3), new state */
    /* only check new state for degenerate outcome */
    if ((normal_tri_v3(no_a, v1, v2, v3) == 0.0f) || (normal_tri_v3(no_b, v1, v3, v4) == 0.0f)) {
      break;
    }
    angle_13 = angle_normalized_v3v3(no_a, no_b);

    return angle_13 - angle_24;
  } while (false);

  return FLT_MAX;
}

float mesh_verts_calc_rotate_beauty(const MeshVert *v1,
                                    const MeshVert *v2,
                                    const MeshVert *v3,
                                    const MeshVert *v4,
                                    const short flag,
                                    const short method)
{
  /* not a loop (only to be able to break out) */
  do {
    if (flag & VERT_RESTRICT_TAG) {
      const MeshVert *v_a = v1, *v_b = v3;
      if (mesh_elem_flag_test(v_a, MESH_ELEM_TAG) == mesh_elem_flag_test(v_b, MESH_ELEM_TAG)) {
        break;
      }
    }

    if (UNLIKELY(v1 == v3)) {
      // printf("This should never happen, but does sometimes!\n");
      break;
    }

    switch (method) {
      case 0:
        return mesh_edge_calc_rotate_beauty__area(
            v1->co, v2->co, v3->co, v4->co, flag & EDGE_RESTRICT_DEGENERATE);
      default:
        return mesh_edge_calc_rotate_beauty__angle(v1->co, v2->co, v3->co, v4->co);
    }
  } while (false);

  return FLT_MAX;
}

static float mesh_edge_calc_rotate_beauty(const MeshEdge *e, const short flag, const short method)
{
  const MeshVert *v1, *v2, *v3, *v4;
  v1 = e->l->prev->v;              /* First vert co */
  v2 = e->l->v;                    /* `e->v1` or `e->v2`. */
  v3 = e->l->radial_next->prev->v; /* Second vert co */
  v4 = e->l->next->v;              /* `e->v1` or `e->v2`. */

  return mesh_verts_calc_rotate_beauty(v1, v2, v3, v4, flag, method);
}

/* -------------------------------------------------------------------- */
/* Update the edge cost of rotation in the heap */

LIB_INLINE bool edge_in_array(const MeshEdge *e, const MeshEdge **edge_array, const int edge_array_len)
{
  const int index = mesh_elem_index_get(e);
  return ((index >= 0) && (index < edge_array_len) && (e == edge_array[index]));
}

/* recalc an edge in the heap (surrounding geometry has changed) */
static void mesh_edge_update_beauty_cost_single(MeshEdge *e,
                                                Heap *eheap,
                                                HeapNode **eheap_table,
                                                GSet **edge_state_arr,
                                                /* only for testing the edge is in the array */
                                                const MeshEdge **edge_array,
                                                const int edge_array_len,
                                                const short flag,
                                                const short method)
{
  if (edge_in_array(e, edge_array, edge_array_len)) {
    const int i = mesh_elem_index_get(e);
    GSet *e_state_set = edge_state_arr[i];

    if (eheap_table[i]) {
      lib_heap_remove(eheap, eheap_table[i]);
      eheap_table[i] = NULL;
    }

    /* check if we can add it back */
    lib_assert(mesh_edge_is_manifold(e) == true);

    /* check we're not moving back into a state we have been in before */
    if (e_state_set != NULL) {
      EdRotState e_state_alt;
      erot_state_alternate(e, &e_state_alt);
      if (lib_gset_haskey(e_state_set, (void *)&e_state_alt)) {
        // printf("  skipping, we already have this state\n");
        return;
      }
    }

    {
      /* recalculate edge */
      const float cost = mesh_edge_calc_rotate_beauty(e, flag, method);
      if (cost < 0.0f) {
        eheap_table[i] = lib_heap_insert(eheap, cost, e);
      }
      else {
        eheap_table[i] = NULL;
      }
    }
  }
}

/* we have rotated an edge, tag other edges and clear this one */
static void mesh_edge_update_beauty_cost(MeshEdge *e,
                                         Heap *eheap,
                                         HeapNode **eheap_table,
                                         GSet **edge_state_arr,
                                         const MeshEdge **edge_array,
                                         const int edge_array_len,
                                         /* only for testing the edge is in the array */
                                         const short flag,
                                         const short method)
{
  int i;

  MeshEdge *e_arr[4] = {
      e->l->next->e,
      e->l->prev->e,
      e->l->radial_next->next->e,
      e->l->radial_next->prev->e,
  };

  lib_assert(e->l->f->len == 3 && e->l->radial_next->f->len == 3);

  lib_assert(mesh_edge_face_count_is_equal(e, 2));

  for (i = 0; i < 4; i++) {
    mesh_edge_update_beauty_cost_single(
        e_arr[i], eheap, eheap_table, edge_state_arr, edge_array, edge_array_len, flag, method);
  }
}

/* -------------------------------------------------------------------- */
/* Beautify Fill */

void mesh_beautify_fill(Mesh *mesh,
                        MeshEdge **edge_array,
                        const int edge_array_len,
                        const short flag,
                        const short method,
                        const short opflag_edge,
                        const short opflag_face)
{
  Heap *eheap;            /* edge heap */
  HeapNode **eheap_table; /* edge index aligned table pointing to the eheap */

  GSet **edge_state_arr = mem_callocn((size_t)edge_array_len * sizeof(GSet *), __func__);
  lib_mempool *edge_state_pool = lib_mempool_create(sizeof(EdRotState), 0, 512, LIB_MEMPOOL_NOP);
  int i;

#ifdef DEBUG_TIME
  TIMEIT_START(beautify_fill);
#endif

  eheap = lib_heap_new_ex((uint)edge_array_len);
  eheap_table = mem_mallocn(sizeof(HeapNode *) * (size_t)edge_array_len, __func__);

  /* build heap */
  for (i = 0; i < edge_array_len; i++) {
    MeshEdge *e = edge_array[i];
    const float cost = mesh_edge_calc_rotate_beauty(e, flag, method);
    if (cost < 0.0f) {
      eheap_table[i] = lib_heap_insert(eheap, cost, e);
    }
    else {
      eheap_table[i] = NULL;
    }

    mesh_elem_index_set(e, i); /* set_dirty */
  }
  mesh->elem_index_dirty |= MESH_EDGE;

  while (lib_heap_is_empty(eheap) == false) {
    MeshEdge *e = lib_heap_pop_min(eheap);
    i = mesh_elem_index_get(e);
    eheap_table[i] = NULL;

    lib_assert(mesh_edge_face_count_is_equal(e, 2));

    e = mesh_edge_rotate(mesh, e, false, MESH_EDGEROT_CHECK_EXISTS);

    lib_assert(e == NULL || mesh_edge_face_count_is_equal(e, 2));

    if (LIKELY(e)) {
      GSet *e_state_set = edge_state_arr[i];

      /* add the new state into the set so we don't move into this state again
       * NOTE: we could add the previous state too but this isn't essential)
       *       for avoiding eternal loops */
      EdRotState *e_state = lib_mempool_alloc(edge_state_pool);
      erot_state_current(e, e_state);
      if (UNLIKELY(e_state_set == NULL)) {
        edge_state_arr[i] = e_state_set = erot_gset_new(); /* store previous state */
      }
      lib_assert(lib_gset_haskey(e_state_set, (void *)e_state) == false);
      lib_gset_insert(e_state_set, e_state);

      // printf("  %d -> %d, %d\n", i, mesh_elem_index_get(e->v1), mesh_elem_index_get(e->v2));

      /* maintain the index array */
      edge_array[i] = e;
      mesh_elem_index_set(e, i);

      /* recalculate faces connected on the heap */
      mesh_edge_update_beauty_cost(e,
                                 eheap,
                                 eheap_table,
                                 edge_state_arr,
                                 (const MeshEdge **)edge_array,
                                 edge_array_len,
                                 flag,
                                 method);

      /* update flags */
      if (oflag_edge) {
        mesh_op_edge_flag_enable(mesh, e, oflag_edge);
      }

      if (oflag_face) {
        mesh__face_flag_enable(mesh, e->l->f, oflag_face);
        mesh_op_face_flag_enable(mesh, e->l->radial_next->f, oflag_face);
      }
    }
  }

  lib_heap_free(eheap, NULL);
  mem_freen(eheap_table);

  for (i = 0; i < edge_array_len; i++) {
    if (edge_state_arr[i]) {
      lib_gset_free(edge_state_arr[i], NULL);
    }
  }

  mem_freen(edge_state_arr);
  lib_mempool_destroy(edge_state_pool);

#ifdef DEBUG_TIME
  TIMEIT_END(beautify_fill);
#endif
}
