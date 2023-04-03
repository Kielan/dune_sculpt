/** \file
 * \ingroup bmesh
 *
 * This file contains functions for answering common
 * Topological and geometric queries about a mesh, such
 * as, "What is the angle between these two faces?" or,
 * "How many faces are incident upon this vertex?" Tool
 * authors should use the functions in this file instead
 * of inspecting the mesh structure directly.
 */

#include "MEM_guardedalloc.h"

#include "BLI_alloca.h"
#include "BLI_linklist.h"
#include "BLI_math.h"
#include "BLI_utildefines_stack.h"

#include "BKE_customdata.h"

#include "bmesh.h"
#include "intern/bmesh_private.h"

BMLoop *BM_face_other_edge_loop(BMFace *f, BMEdge *e, BMVert *v)
{
  BMLoop *l = BM_face_edge_share_loop(f, e);
  BLI_assert(l != NULL);
  return BM_loop_other_edge_loop(l, v);
}

BMLoop *BM_loop_other_edge_loop(BMLoop *l, BMVert *v)
{
  BLI_assert(BM_vert_in_edge(l->e, v));
  return l->v == v ? l->prev : l->next;
}

BMLoop *BM_face_other_vert_loop(BMFace *f, BMVert *v_prev, BMVert *v)
{
  BMLoop *l_iter = BM_face_vert_share_loop(f, v);

  BLI_assert(BM_edge_exists(v_prev, v) != NULL);

  if (l_iter) {
    if (l_iter->prev->v == v_prev) {
      return l_iter->next;
    }
    if (l_iter->next->v == v_prev) {
      return l_iter->prev;
    }
    /* invalid args */
    BLI_assert(0);
    return NULL;
  }
  /* invalid args */
  BLI_assert(0);
  return NULL;
}

BMLoop *BM_loop_other_vert_loop(BMLoop *l, BMVert *v)
{
#if 0 /* works but slow */
  return BM_face_other_vert_loop(l->f, BM_edge_other_vert(l->e, v), v);
#else
  BMEdge *e = l->e;
  BMVert *v_prev = BM_edge_other_vert(e, v);
  if (l->v == v) {
    if (l->prev->v == v_prev) {
      return l->next;
    }
    BLI_assert(l->next->v == v_prev);

    return l->prev;
  }
  BLI_assert(l->v == v_prev);

  if (l->prev->v == v) {
    return l->prev->prev;
  }
  BLI_assert(l->next->v == v);
  return l->next->next;
#endif
}

BMLoop *BM_loop_other_vert_loop_by_edge(BMLoop *l, BMEdge *e)
{
  BLI_assert(BM_vert_in_edge(e, l->v));
  if (l->e == e) {
    return l->next;
  }
  if (l->prev->e == e) {
    return l->prev;
  }

  BLI_assert(0);
  return NULL;
}

bool BM_vert_pair_share_face_check(BMVert *v_a, BMVert *v_b)
{
  if (v_a->e && v_b->e) {
    BMIter iter;
    BMFace *f;

    BM_ITER_ELEM (f, &iter, v_a, BM_FACES_OF_VERT) {
      if (BM_vert_in_face(v_b, f)) {
        return true;
      }
    }
  }

  return false;
}

bool BM_vert_pair_share_face_check_cb(BMVert *v_a,
                                      BMVert *v_b,
                                      bool (*test_fn)(BMFace *, void *user_data),
                                      void *user_data)
{
  if (v_a->e && v_b->e) {
    BMIter iter;
    BMFace *f;

    BM_ITER_ELEM (f, &iter, v_a, BM_FACES_OF_VERT) {
      if (test_fn(f, user_data)) {
        if (BM_vert_in_face(v_b, f)) {
          return true;
        }
      }
    }
  }

  return false;
}

BMFace *BM_vert_pair_shared_face_cb(BMVert *v_a,
                                    BMVert *v_b,
                                    const bool allow_adjacent,
                                    bool (*callback)(BMFace *, BMLoop *, BMLoop *, void *userdata),
                                    void *user_data,
                                    BMLoop **r_l_a,
                                    BMLoop **r_l_b)
{
  if (v_a->e && v_b->e) {
    BMIter iter;
    BMLoop *l_a, *l_b;

    BM_ITER_ELEM (l_a, &iter, v_a, BM_LOOPS_OF_VERT) {
      BMFace *f = l_a->f;
      l_b = BM_face_vert_share_loop(f, v_b);
      if (l_b && (allow_adjacent || !BM_loop_is_adjacent(l_a, l_b)) &&
          callback(f, l_a, l_b, user_data)) {
        *r_l_a = l_a;
        *r_l_b = l_b;

        return f;
      }
    }
  }

  return NULL;
}

BMFace *BM_vert_pair_share_face_by_len(
    BMVert *v_a, BMVert *v_b, BMLoop **r_l_a, BMLoop **r_l_b, const bool allow_adjacent)
{
  BMLoop *l_cur_a = NULL, *l_cur_b = NULL;
  BMFace *f_cur = NULL;

  if (v_a->e && v_b->e) {
    BMIter iter;
    BMLoop *l_a, *l_b;

    BM_ITER_ELEM (l_a, &iter, v_a, BM_LOOPS_OF_VERT) {
      if ((f_cur == NULL) || (l_a->f->len < f_cur->len)) {
        l_b = BM_face_vert_share_loop(l_a->f, v_b);
        if (l_b && (allow_adjacent || !BM_loop_is_adjacent(l_a, l_b))) {
          f_cur = l_a->f;
          l_cur_a = l_a;
          l_cur_b = l_b;
        }
      }
    }
  }

  *r_l_a = l_cur_a;
  *r_l_b = l_cur_b;

  return f_cur;
}

BMFace *BM_edge_pair_share_face_by_len(
    BMEdge *e_a, BMEdge *e_b, BMLoop **r_l_a, BMLoop **r_l_b, const bool allow_adjacent)
{
  BMLoop *l_cur_a = NULL, *l_cur_b = NULL;
  BMFace *f_cur = NULL;

  if (e_a->l && e_b->l) {
    BMIter iter;
    BMLoop *l_a, *l_b;

    BM_ITER_ELEM (l_a, &iter, e_a, BM_LOOPS_OF_EDGE) {
      if ((f_cur == NULL) || (l_a->f->len < f_cur->len)) {
        l_b = BM_face_edge_share_loop(l_a->f, e_b);
        if (l_b && (allow_adjacent || !BM_loop_is_adjacent(l_a, l_b))) {
          f_cur = l_a->f;
          l_cur_a = l_a;
          l_cur_b = l_b;
        }
      }
    }
  }

  *r_l_a = l_cur_a;
  *r_l_b = l_cur_b;

  return f_cur;
}

static float bm_face_calc_split_dot(BMLoop *l_a, BMLoop *l_b)
{
  float no[2][3];

  if ((BM_face_calc_normal_subset(l_a, l_b, no[0]) != 0.0f) &&
      (BM_face_calc_normal_subset(l_b, l_a, no[1]) != 0.0f)) {
    return dot_v3v3(no[0], no[1]);
  }
  return -1.0f;
}

float BM_loop_point_side_of_loop_test(const BMLoop *l, const float co[3])
{
  const float *axis = l->f->no;
  return dist_signed_squared_to_corner_v3v3v3(co, l->prev->v->co, l->v->co, l->next->v->co, axis);
}

float BM_loop_point_side_of_edge_test(const BMLoop *l, const float co[3])
{
  const float *axis = l->f->no;
  float dir[3];
  float plane[4];

  sub_v3_v3v3(dir, l->next->v->co, l->v->co);
  cross_v3_v3v3(plane, axis, dir);

  plane[3] = -dot_v3v3(plane, l->v->co);
  return dist_signed_squared_to_plane_v3(co, plane);
}

BMFace *BM_vert_pair_share_face_by_angle(
    BMVert *v_a, BMVert *v_b, BMLoop **r_l_a, BMLoop **r_l_b, const bool allow_adjacent)
{
  BMLoop *l_cur_a = NULL, *l_cur_b = NULL;
  BMFace *f_cur = NULL;

  if (v_a->e && v_b->e) {
    BMIter iter;
    BMLoop *l_a, *l_b;
    float dot_best = -1.0f;

    BM_ITER_ELEM (l_a, &iter, v_a, BM_LOOPS_OF_VERT) {
      l_b = BM_face_vert_share_loop(l_a->f, v_b);
      if (l_b && (allow_adjacent || !BM_loop_is_adjacent(l_a, l_b))) {

        if (f_cur == NULL) {
          f_cur = l_a->f;
          l_cur_a = l_a;
          l_cur_b = l_b;
        }
        else {
          /* avoid expensive calculations if we only ever find one face */
          float dot;
          if (dot_best == -1.0f) {
            dot_best = bm_face_calc_split_dot(l_cur_a, l_cur_b);
          }

          dot = bm_face_calc_split_dot(l_a, l_b);
          if (dot > dot_best) {
            dot_best = dot;

            f_cur = l_a->f;
            l_cur_a = l_a;
            l_cur_b = l_b;
          }
        }
      }
    }
  }

  *r_l_a = l_cur_a;
  *r_l_b = l_cur_b;

  return f_cur;
}

BMLoop *BM_vert_find_first_loop(BMVert *v)
{
  return v->e ? bmesh_disk_faceloop_find_first(v->e, v) : NULL;
}
BMLoop *BM_vert_find_first_loop_visible(BMVert *v)
{
  return v->e ? bmesh_disk_faceloop_find_first_visible(v->e, v) : NULL;
}

bool BM_vert_in_face(BMVert *v, BMFace *f)
{
  BMLoop *l_iter, *l_first;

#ifdef USE_BMESH_HOLES
  BMLoopList *lst;
  for (lst = f->loops.first; lst; lst = lst->next)
#endif
  {
#ifdef USE_BMESH_HOLES
    l_iter = l_first = lst->first;
#else
    l_iter = l_first = f->l_first;
#endif
    do {
      if (l_iter->v == v) {
        return true;
      }
    } while ((l_iter = l_iter->next) != l_first);
  }

  return false;
}

int BM_verts_in_face_count(BMVert **varr, int len, BMFace *f)
{
  BMLoop *l_iter, *l_first;

#ifdef USE_BMESH_HOLES
  BMLoopList *lst;
#endif

  int i, count = 0;

  for (i = 0; i < len; i++) {
    BM_ELEM_API_FLAG_ENABLE(varr[i], _FLAG_OVERLAP);
  }

#ifdef USE_BMESH_HOLES
  for (lst = f->loops.first; lst; lst = lst->next)
#endif
  {

#ifdef USE_BMESH_HOLES
    l_iter = l_first = lst->first;
#else
    l_iter = l_first = f->l_first;
#endif

    do {
      if (BM_ELEM_API_FLAG_TEST(l_iter->v, _FLAG_OVERLAP)) {
        count++;
      }

    } while ((l_iter = l_iter->next) != l_first);
  }

  for (i = 0; i < len; i++) {
    BM_ELEM_API_FLAG_DISABLE(varr[i], _FLAG_OVERLAP);
  }

  return count;
}

bool BM_verts_in_face(BMVert **varr, int len, BMFace *f)
{
  BMLoop *l_iter, *l_first;

#ifdef USE_BMESH_HOLES
  BMLoopList *lst;
#endif

  int i;
  bool ok = true;

  /* simple check, we know can't succeed */
  if (f->len < len) {
    return false;
  }

  for (i = 0; i < len; i++) {
    BM_ELEM_API_FLAG_ENABLE(varr[i], _FLAG_OVERLAP);
  }

#ifdef USE_BMESH_HOLES
  for (lst = f->loops.first; lst; lst = lst->next)
#endif
  {

#ifdef USE_BMESH_HOLES
    l_iter = l_first = lst->first;
#else
    l_iter = l_first = f->l_first;
#endif

    do {
      if (BM_ELEM_API_FLAG_TEST(l_iter->v, _FLAG_OVERLAP)) {
        /* pass */
      }
      else {
        ok = false;
        break;
      }

    } while ((l_iter = l_iter->next) != l_first);
  }

  for (i = 0; i < len; i++) {
    BM_ELEM_API_FLAG_DISABLE(varr[i], _FLAG_OVERLAP);
  }

  return ok;
}

bool BM_edge_in_face(const BMEdge *e, const BMFace *f)
{
  if (e->l) {
    const BMLoop *l_iter, *l_first;

    l_iter = l_first = e->l;
    do {
      if (l_iter->f == f) {
        return true;
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  return false;
}

BMLoop *BM_edge_other_loop(BMEdge *e, BMLoop *l)
{
  BMLoop *l_other;

  // BLI_assert(BM_edge_is_manifold(e));  // TOO strict, just check if we have another radial face
  BLI_assert(e->l && e->l->radial_next != e->l);
  BLI_assert(BM_vert_in_edge(e, l->v));

  l_other = (l->e == e) ? l : l->prev;
  l_other = l_other->radial_next;
  BLI_assert(l_other->e == e);

  if (l_other->v == l->v) {
    /* pass */
  }
  else if (l_other->next->v == l->v) {
    l_other = l_other->next;
  }
  else {
    BLI_assert(0);
  }

  return l_other;
}

BMLoop *BM_vert_step_fan_loop(BMLoop *l, BMEdge **e_step)
{
  BMEdge *e_prev = *e_step;
  BMEdge *e_next;
  if (l->e == e_prev) {
    e_next = l->prev->e;
  }
  else if (l->prev->e == e_prev) {
    e_next = l->e;
  }
  else {
    BLI_assert(0);
    return NULL;
  }

  if (BM_edge_is_manifold(e_next)) {
    return BM_edge_other_loop((*e_step = e_next), l);
  }
  return NULL;
}

BMEdge *BM_vert_other_disk_edge(BMVert *v, BMEdge *e_first)
{
  BMLoop *l_a;
  int tot = 0;
  int i;

  BLI_assert(BM_vert_in_edge(e_first, v));

  l_a = e_first->l;
  do {
    l_a = BM_loop_other_vert_loop(l_a, v);
    l_a = BM_vert_in_edge(l_a->e, v) ? l_a : l_a->prev;
    if (BM_edge_is_manifold(l_a->e)) {
      l_a = l_a->radial_next;
    }
    else {
      return NULL;
    }

    tot++;
  } while (l_a != e_first->l);

  /* we know the total, now loop half way */
  tot /= 2;
  i = 0;

  l_a = e_first->l;
  do {
    if (i == tot) {
      l_a = BM_vert_in_edge(l_a->e, v) ? l_a : l_a->prev;
      return l_a->e;
    }

    l_a = BM_loop_other_vert_loop(l_a, v);
    l_a = BM_vert_in_edge(l_a->e, v) ? l_a : l_a->prev;
    if (BM_edge_is_manifold(l_a->e)) {
      l_a = l_a->radial_next;
    }
    /* this won't have changed from the previous loop */

    i++;
  } while (l_a != e_first->l);

  return NULL;
}

float BM_edge_calc_length(const BMEdge *e)
{
  return len_v3v3(e->v1->co, e->v2->co);
}

float BM_edge_calc_length_squared(const BMEdge *e)
{
  return len_squared_v3v3(e->v1->co, e->v2->co);
}

bool BM_edge_face_pair(BMEdge *e, BMFace **r_fa, BMFace **r_fb)
{
  BMLoop *la, *lb;

  if ((la = e->l) && (lb = la->radial_next) && (la != lb) && (lb->radial_next == la)) {
    *r_fa = la->f;
    *r_fb = lb->f;
    return true;
  }

  *r_fa = NULL;
  *r_fb = NULL;
  return false;
}

bool BM_edge_loop_pair(BMEdge *e, BMLoop **r_la, BMLoop **r_lb)
{
  BMLoop *la, *lb;

  if ((la = e->l) && (lb = la->radial_next) && (la != lb) && (lb->radial_next == la)) {
    *r_la = la;
    *r_lb = lb;
    return true;
  }

  *r_la = NULL;
  *r_lb = NULL;
  return false;
}

bool BM_vert_is_edge_pair(const BMVert *v)
{
  const BMEdge *e = v->e;
  if (e) {
    BMEdge *e_other = BM_DISK_EDGE_NEXT(e, v);
    return ((e_other != e) && (BM_DISK_EDGE_NEXT(e_other, v) == e));
  }
  return false;
}

bool BM_vert_is_edge_pair_manifold(const BMVert *v)
{
  const BMEdge *e = v->e;
  if (e) {
    BMEdge *e_other = BM_DISK_EDGE_NEXT(e, v);
    if (((e_other != e) && (BM_DISK_EDGE_NEXT(e_other, v) == e))) {
      return BM_edge_is_manifold(e) && BM_edge_is_manifold(e_other);
    }
  }
  return false;
}

bool BM_vert_edge_pair(BMVert *v, BMEdge **r_e_a, BMEdge **r_e_b)
{
  BMEdge *e_a = v->e;
  if (e_a) {
    BMEdge *e_b = BM_DISK_EDGE_NEXT(e_a, v);
    if ((e_b != e_a) && (BM_DISK_EDGE_NEXT(e_b, v) == e_a)) {
      *r_e_a = e_a;
      *r_e_b = e_b;
      return true;
    }
  }

  *r_e_a = NULL;
  *r_e_b = NULL;
  return false;
}

int BM_vert_edge_count(const BMVert *v)
{
  return bmesh_disk_count(v);
}

int BM_vert_edge_count_at_most(const BMVert *v, const int count_max)
{
  return bmesh_disk_count_at_most(v, count_max);
}

int BM_vert_edge_count_nonwire(const BMVert *v)
{
  int count = 0;
  BMIter eiter;
  BMEdge *edge;
  BM_ITER_ELEM (edge, &eiter, (BMVert *)v, BM_EDGES_OF_VERT) {
    if (edge->l) {
      count++;
    }
  }
  return count;
}
int BM_edge_face_count(const BMEdge *e)
{
  int count = 0;

  if (e->l) {
    BMLoop *l_iter, *l_first;

    l_iter = l_first = e->l;
    do {
      count++;
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  return count;
}

int BM_edge_face_count_at_most(const BMEdge *e, const int count_max)
{
  int count = 0;

  if (e->l) {
    BMLoop *l_iter, *l_first;

    l_iter = l_first = e->l;
    do {
      count++;
      if (count == count_max) {
        break;
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  return count;
}

int BM_vert_face_count(const BMVert *v)
{
  return bmesh_disk_facevert_count(v);
}

int BM_vert_face_count_at_most(const BMVert *v, int count_max)
{
  return bmesh_disk_facevert_count_at_most(v, count_max);
}

bool BM_vert_face_check(const BMVert *v)
{
  if (v->e != NULL) {
    const BMEdge *e_iter, *e_first;
    e_first = e_iter = v->e;
    do {
      if (e_iter->l != NULL) {
        return true;
      }
    } while ((e_iter = bmesh_disk_edge_next(e_iter, v)) != e_first);
  }
  return false;
}

bool BM_vert_is_wire(const BMVert *v)
{
  if (v->e) {
    BMEdge *e_first, *e_iter;

    e_first = e_iter = v->e;
    do {
      if (e_iter->l) {
        return false;
      }
    } while ((e_iter = bmesh_disk_edge_next(e_iter, v)) != e_first);

    return true;
  }
  return false;
}

bool BM_vert_is_manifold(const BMVert *v)
{
  BMEdge *e_iter, *e_first, *e_prev;
  BMLoop *l_iter, *l_first;
  int loop_num = 0, loop_num_region = 0, boundary_num = 0;

  if (v->e == NULL) {
    /* loose vert */
    return false;
  }

  /* count edges while looking for non-manifold edges */
  e_first = e_iter = v->e;
  /* may be null */
  l_first = e_iter->l;
  do {
    /* loose edge or edge shared by more than two faces,
     * edges with 1 face user are OK, otherwise we could
     * use BM_edge_is_manifold() here */
    if (e_iter->l == NULL || (e_iter->l != e_iter->l->radial_next->radial_next)) {
      return false;
    }

    /* count radial loops */
    if (e_iter->l->v == v) {
      loop_num += 1;
    }

    if (!BM_edge_is_boundary(e_iter)) {
      /* non boundary check opposite loop */
      if (e_iter->l->radial_next->v == v) {
        loop_num += 1;
      }
    }
    else {
      /* start at the boundary */
      l_first = e_iter->l;
      boundary_num += 1;
      /* >2 boundaries can't be manifold */
      if (boundary_num == 3) {
        return false;
      }
    }
  } while ((e_iter = bmesh_disk_edge_next(e_iter, v)) != e_first);

  e_first = l_first->e;
  l_first = (l_first->v == v) ? l_first : l_first->next;
  BLI_assert(l_first->v == v);

  l_iter = l_first;
  e_prev = e_first;

  do {
    loop_num_region += 1;
  } while (((l_iter = BM_vert_step_fan_loop(l_iter, &e_prev)) != l_first) && (l_iter != NULL));

  return (loop_num == loop_num_region);
}

#define LOOP_VISIT _FLAG_WALK
#define EDGE_VISIT _FLAG_WALK

static int bm_loop_region_count__recursive(BMEdge *e, BMVert *v)
{
  BMLoop *l_iter, *l_first;
  int count = 0;

  BLI_assert(!BM_ELEM_API_FLAG_TEST(e, EDGE_VISIT));
  BM_ELEM_API_FLAG_ENABLE(e, EDGE_VISIT);

  l_iter = l_first = e->l;
  do {
    if (l_iter->v == v) {
      BMEdge *e_other = l_iter->prev->e;
      if (!BM_ELEM_API_FLAG_TEST(l_iter, LOOP_VISIT)) {
        BM_ELEM_API_FLAG_ENABLE(l_iter, LOOP_VISIT);
        count += 1;
      }
      if (!BM_ELEM_API_FLAG_TEST(e_other, EDGE_VISIT)) {
        count += bm_loop_region_count__recursive(e_other, v);
      }
    }
    else if (l_iter->next->v == v) {
      BMEdge *e_other = l_iter->next->e;
      if (!BM_ELEM_API_FLAG_TEST(l_iter->next, LOOP_VISIT)) {
        BM_ELEM_API_FLAG_ENABLE(l_iter->next, LOOP_VISIT);
        count += 1;
      }
      if (!BM_ELEM_API_FLAG_TEST(e_other, EDGE_VISIT)) {
        count += bm_loop_region_count__recursive(e_other, v);
      }
    }
    else {
      BLI_assert(0);
    }
  } while ((l_iter = l_iter->radial_next) != l_first);

  return count;
}

static int bm_loop_region_count__clear(BMLoop *l)
{
  int count = 0;
  BMEdge *e_iter, *e_first;

  /* clear flags */
  e_iter = e_first = l->e;
  do {
    BM_ELEM_API_FLAG_DISABLE(e_iter, EDGE_VISIT);
    if (e_iter->l) {
      BMLoop *l_iter, *l_first;
      l_iter = l_first = e_iter->l;
      do {
        if (l_iter->v == l->v) {
          BM_ELEM_API_FLAG_DISABLE(l_iter, LOOP_VISIT);
          count += 1;
        }
      } while ((l_iter = l_iter->radial_next) != l_first);
    }
  } while ((e_iter = BM_DISK_EDGE_NEXT(e_iter, l->v)) != e_first);

  return count;
}

int BM_loop_region_loops_count_at_most(BMLoop *l, int *r_loop_total)
{
  const int count = bm_loop_region_count__recursive(l->e, l->v);
  const int count_total = bm_loop_region_count__clear(l);
  if (r_loop_total) {
    *r_loop_total = count_total;
  }
  return count;
}

#undef LOOP_VISIT
#undef EDGE_VISIT

int BM_loop_region_loops_count(BMLoop *l)
{
  return BM_loop_region_loops_count_at_most(l, NULL);
}

bool BM_vert_is_manifold_region(const BMVert *v)
{
  BMLoop *l_first = BM_vert_find_first_loop((BMVert *)v);
  if (l_first) {
    int count, count_total;
    count = BM_loop_region_loops_count_at_most(l_first, &count_total);
    return (count == count_total);
  }
  return true;
}

bool BM_edge_is_convex(const BMEdge *e)
{
  if (BM_edge_is_manifold(e)) {
    BMLoop *l1 = e->l;
    BMLoop *l2 = e->l->radial_next;
    if (!equals_v3v3(l1->f->no, l2->f->no)) {
      float cross[3];
      float l_dir[3];
      cross_v3_v3v3(cross, l1->f->no, l2->f->no);
      /* we assume contiguous normals, otherwise the result isn't meaningful */
      sub_v3_v3v3(l_dir, l1->next->v->co, l1->v->co);
      return (dot_v3v3(l_dir, cross) > 0.0f);
    }
  }
  return true;
}

bool BM_edge_is_contiguous_loop_cd(const BMEdge *e,
                                   const int cd_loop_type,
                                   const int cd_loop_offset)
{
  BLI_assert(cd_loop_offset != -1);

  if (e->l && e->l->radial_next != e->l) {
    const BMLoop *l_base_v1 = e->l;
    const BMLoop *l_base_v2 = e->l->next;
    const void *l_base_cd_v1 = BM_ELEM_CD_GET_VOID_P(l_base_v1, cd_loop_offset);
    const void *l_base_cd_v2 = BM_ELEM_CD_GET_VOID_P(l_base_v2, cd_loop_offset);
    const BMLoop *l_iter = e->l->radial_next;
    do {
      const BMLoop *l_iter_v1;
      const BMLoop *l_iter_v2;
      const void *l_iter_cd_v1;
      const void *l_iter_cd_v2;

      if (l_iter->v == l_base_v1->v) {
        l_iter_v1 = l_iter;
        l_iter_v2 = l_iter->next;
      }
      else {
        l_iter_v1 = l_iter->next;
        l_iter_v2 = l_iter;
      }
      BLI_assert((l_iter_v1->v == l_base_v1->v) && (l_iter_v2->v == l_base_v2->v));

      l_iter_cd_v1 = BM_ELEM_CD_GET_VOID_P(l_iter_v1, cd_loop_offset);
      l_iter_cd_v2 = BM_ELEM_CD_GET_VOID_P(l_iter_v2, cd_loop_offset);

      if ((CustomData_data_equals(cd_loop_type, l_base_cd_v1, l_iter_cd_v1) == 0) ||
          (CustomData_data_equals(cd_loop_type, l_base_cd_v2, l_iter_cd_v2) == 0)) {
        return false;
      }

    } while ((l_iter = l_iter->radial_next) != e->l);
  }
  return true;
}

bool BM_vert_is_boundary(const BMVert *v)
{
  if (v->e) {
    BMEdge *e_first, *e_iter;

    e_first = e_iter = v->e;
    do {
      if (BM_edge_is_boundary(e_iter)) {
        return true;
      }
    } while ((e_iter = bmesh_disk_edge_next(e_iter, v)) != e_first);

    return false;
  }
  return false;
}

int BM_face_share_face_count(BMFace *f_a, BMFace *f_b)
{
  BMIter iter1, iter2;
  BMEdge *e;
  BMFace *f;
  int count = 0;

  BM_ITER_ELEM (e, &iter1, f_a, BM_EDGES_OF_FACE) {
    BM_ITER_ELEM (f, &iter2, e, BM_FACES_OF_EDGE) {
      if (f != f_a && f != f_b && BM_face_share_edge_check(f, f_b)) {
        count++;
      }
    }
  }

  return count;
}

bool BM_face_share_face_check(BMFace *f_a, BMFace *f_b)
{
  BMIter iter1, iter2;
  BMEdge *e;
  BMFace *f;

  BM_ITER_ELEM (e, &iter1, f_a, BM_EDGES_OF_FACE) {
    BM_ITER_ELEM (f, &iter2, e, BM_FACES_OF_EDGE) {
      if (f != f_a && f != f_b && BM_face_share_edge_check(f, f_b)) {
        return true;
      }
    }
  }

  return false;
}

int BM_face_share_edge_count(BMFace *f_a, BMFace *f_b)
{
  BMLoop *l_iter;
  BMLoop *l_first;
  int count = 0;

  l_iter = l_first = BM_FACE_FIRST_LOOP(f_a);
  do {
    if (BM_edge_in_face(l_iter->e, f_b)) {
      count++;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return count;
}

bool BM_face_share_edge_check(BMFace *f1, BMFace *f2)
{
  BMLoop *l_iter;
  BMLoop *l_first;

  l_iter = l_first = BM_FACE_FIRST_LOOP(f1);
  do {
    if (BM_edge_in_face(l_iter->e, f2)) {
      return true;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return false;
}

int BM_face_share_vert_count(BMFace *f_a, BMFace *f_b)
{
  BMLoop *l_iter;
  BMLoop *l_first;
  int count = 0;

  l_iter = l_first = BM_FACE_FIRST_LOOP(f_a);
  do {
    if (BM_vert_in_face(l_iter->v, f_b)) {
      count++;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return count;
}

bool BM_face_share_vert_check(BMFace *f_a, BMFace *f_b)
{
  BMLoop *l_iter;
  BMLoop *l_first;

  l_iter = l_first = BM_FACE_FIRST_LOOP(f_a);
  do {
    if (BM_vert_in_face(l_iter->v, f_b)) {
      return true;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return false;
}

bool BM_loop_share_edge_check(BMLoop *l_a, BMLoop *l_b)
{
  BLI_assert(l_a->v == l_b->v);
  return (ELEM(l_a->e, l_b->e, l_b->prev->e) || ELEM(l_b->e, l_a->e, l_a->prev->e));
}

bool BM_edge_share_face_check(BMEdge *e1, BMEdge *e2)
{
  BMLoop *l;
  BMFace *f;

  if (e1->l && e2->l) {
    l = e1->l;
    do {
      f = l->f;
      if (BM_edge_in_face(e2, f)) {
        return true;
      }
      l = l->radial_next;
    } while (l != e1->l);
  }
  return false;
}

bool BM_edge_share_quad_check(BMEdge *e1, BMEdge *e2)
{
  BMLoop *l;
  BMFace *f;

  if (e1->l && e2->l) {
    l = e1->l;
    do {
      f = l->f;
      if (f->len == 4) {
        if (BM_edge_in_face(e2, f)) {
          return true;
        }
      }
      l = l->radial_next;
    } while (l != e1->l);
  }
  return false;
}

bool BM_edge_share_vert_check(BMEdge *e1, BMEdge *e2)
{
  return (e1->v1 == e2->v1 || e1->v1 == e2->v2 || e1->v2 == e2->v1 || e1->v2 == e2->v2);
}

BMVert *BM_edge_share_vert(BMEdge *e1, BMEdge *e2)
{
  BLI_assert(e1 != e2);
  if (BM_vert_in_edge(e2, e1->v1)) {
    return e1->v1;
  }
  if (BM_vert_in_edge(e2, e1->v2)) {
    return e1->v2;
  }
  return NULL;
}

BMLoop *BM_edge_vert_share_loop(BMLoop *l, BMVert *v)
{
  BLI_assert(BM_vert_in_edge(l->e, v));
  if (l->v == v) {
    return l;
  }
  return l->next;
}

BMLoop *BM_face_vert_share_loop(BMFace *f, BMVert *v)
{
  BMLoop *l_first;
  BMLoop *l_iter;

  l_iter = l_first = BM_FACE_FIRST_LOOP(f);
  do {
    if (l_iter->v == v) {
      return l_iter;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return NULL;
}

BMLoop *BM_face_edge_share_loop(BMFace *f, BMEdge *e)
{
  BMLoop *l_first;
  BMLoop *l_iter;

  l_iter = l_first = e->l;
  do {
    if (l_iter->f == f) {
      return l_iter;
    }
  } while ((l_iter = l_iter->radial_next) != l_first);

  return NULL;
}

void BM_edge_ordered_verts_ex(const BMEdge *edge,
                              BMVert **r_v1,
                              BMVert **r_v2,
                              const BMLoop *edge_loop)
{
  BLI_assert(edge_loop->e == edge);
  (void)edge; /* quiet warning in release build */
  *r_v1 = edge_loop->v;
  *r_v2 = edge_loop->next->v;
}

void BM_edge_ordered_verts(const BMEdge *edge, BMVert **r_v1, BMVert **r_v2)
{
  BM_edge_ordered_verts_ex(edge, r_v1, r_v2, edge->l);
}

BMLoop *BM_loop_find_prev_nodouble(BMLoop *l, BMLoop *l_stop, const float eps_sq)
{
  BMLoop *l_step = l->prev;

  BLI_assert(!ELEM(l_stop, NULL, l));

  while (UNLIKELY(len_squared_v3v3(l->v->co, l_step->v->co) < eps_sq)) {
    l_step = l_step->prev;
    BLI_assert(l_step != l);
    if (UNLIKELY(l_step == l_stop)) {
      return NULL;
    }
  }

  return l_step;
}
