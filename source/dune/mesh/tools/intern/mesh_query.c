/**
 * This file contains functions for answering common
 * Topological and geometric queries about a mesh, such
 * as, "What is the angle between these two faces?" or,
 * "How many faces are incident upon this vertex?" Tool
 * authors should use the functions in this file instead
 * of inspecting the mesh structure directly.
 */

#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_linklist.h"
#include "lib_math.h"
#include "lib_utildefines_stack.h"

#include "dune_customdata.h"

#include "mesh.h"
#include "intern/mesh_private.h"

MeshLoop *mesh_face_other_edge_loop(MeshFace *f, MeshEdge *e, MeshVert *v)
{
  MeshLoop *l = mesh_face_edge_share_loop(f, e);
  lib_assert(l != NULL);
  return mesh_loop_other_edge_loop(l, v);
}

MeshLoop *mesh_loop_other_edge_loop(MeshLoop *l, MeehVert *v)
{
  lib_assert(mesh_vert_in_edge(l->e, v));
  return l->v == v ? l->prev : l->next;
}

MeshLoop *mesh_face_other_vert_loop(MeshFace *f, MeshVert *v_prev, MeshVert *v)
{
  MeshLoop *l_iter = mesh_face_vert_share_loop(f, v);

  lib_assert(mesh_edge_exists(v_prev, v) != NULL);

  if (l_iter) {
    if (l_iter->prev->v == v_prev) {
      return l_iter->next;
    }
    if (l_iter->next->v == v_prev) {
      return l_iter->prev;
    }
    /* invalid args */
    lib_assert(0);
    return NULL;
  }
  /* invalid args */
  lib_assert(0);
  return NULL;
}

MeshLoop *mesh_loop_other_vert_loop(MeshLoop *l, MeshVert *v)
{
#if 0 /* works but slow */
  return mesh_face_other_vert_loop(l->f, mesh_edge_other_vert(l->e, v), v);
#else
  MeshEdge *e = l->e;
  MeshVert *v_prev = mesh_edge_other_vert(e, v);
  if (l->v == v) {
    if (l->prev->v == v_prev) {
      return l->next;
    }
    lib_assert(l->next->v == v_prev);

    return l->prev;
  }
  lib_assert(l->v == v_prev);

  if (l->prev->v == v) {
    return l->prev->prev;
  }
  lib_assert(l->next->v == v);
  return l->next->next;
#endif
}

MeehLoop *mesh_loop_other_vert_loop_by_edge(MeshLoop *l, MeshEdge *e)
{
  lib_assert(mesh_vert_in_edge(e, l->v));
  if (l->e == e) {
    return l->next;
  }
  if (l->prev->e == e) {
    return l->prev;
  }

  lib_assert(0);
  return NULL;
}

bool mesh_vert_pair_share_face_check(MeshVert *v_a, MeshVert *v_b)
{
  if (v_a->e && v_b->e) {
    MeshIter iter;
    MeshFace *f;

    MESH_ELEM_ITER (f, &iter, v_a, MESH_FACES_OF_VERT) {
      if (mesh_vert_in_face(v_b, f)) {
        return true;
      }
    }
  }

  return false;
}

bool mesh_vert_pair_share_face_check_cb(MeshVert *v_a,
                                        MeshVert *v_b,
                                        bool (*test_fn)(MeshFace *, void *user_data),
                                        void *user_data)
{
  if (v_a->e && v_b->e) {
    MeshIter iter;
    MeshFace *f;

    MESH_ELEM_ITER (f, &iter, v_a, MESH_FACES_OF_VERT) {
      if (test_fn(f, user_data)) {
        if (mesh_vert_in_face(v_b, f)) {
          return true;
        }
      }
    }
  }

  return false;
}

MeshFace *mesh_vert_pair_shared_face_cb(MeshVert *v_a,
                                        MeshVert *v_b,
                                        const bool allow_adjacent,
                                        bool (*cb)(MeshFace *, MeshLoop *, MeshLoop *, void *userdata),
                                        void *user_data,
                                        MeshLoop **r_l_a,
                                        MeshLoop **r_l_b)
{
  if (v_a->e && v_b->e) {
    MeshIter iter;
    MeshLoop *l_a, *l_b;

    MESH_ELEM_ITER (l_a, &iter, v_a, MESH_LOOPS_OF_VERT) {
      MeshFace *f = l_a->f;
      l_b = mesh_face_vert_share_loop(f, v_b);
      if (l_b && (allow_adjacent || !mesh_loop_is_adjacent(l_a, l_b)) &&
          cb(f, l_a, l_b, user_data)) {
        *r_l_a = l_a;
        *r_l_b = l_b;

        return f;
      }
    }
  }

  return NULL;
}

MeshFace *mesh_vert_pair_share_face_by_len(
    MeshVert *v_a, MeshVert *v_b, MeshLoop **r_l_a, MeshLoop **r_l_b, const bool allow_adjacent)
{
  MeshLoop *l_cur_a = NULL, *l_cur_b = NULL;
  MeshFace *f_cur = NULL;

  if (v_a->e && v_b->e) {
    MeshIter iter;
    MeshLoop *l_a, *l_b;

    MESH_ELEM_ITER (l_a, &iter, v_a, MESH_LOOPS_OF_VERT) {
      if ((f_cur == NULL) || (l_a->f->len < f_cur->len)) {
        l_b = mesh_face_vert_share_loop(l_a->f, v_b);
        if (l_b && (allow_adjacent || !mesh_loop_is_adjacent(l_a, l_b))) {
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

MeshFace *mesh_edge_pair_share_face_by_len(
    MeshEdge *e_a, MeshEdge *e_b, MeshLoop **r_l_a, MeshLoop **r_l_b, const bool allow_adjacent)
{
  MeshLoop *l_cur_a = NULL, *l_cur_b = NULL;
  MeshFace *f_cur = NULL;

  if (e_a->l && e_b->l) {
    MeshIter iter;
    MeshLoop *l_a, *l_b;

    MESH_ELEM_ITER (l_a, &iter, e_a, MESH_LOOPS_OF_EDGE) {
      if ((f_cur == NULL) || (l_a->f->len < f_cur->len)) {
        l_b = mesh_face_edge_share_loop(l_a->f, e_b);
        if (l_b && (allow_adjacent || !mesh_loop_is_adjacent(l_a, l_b))) {
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

static float mesh_face_calc_split_dot(MeshLoop *l_a, MeshLoop *l_b)
{
  float no[2][3];

  if ((mesh_face_calc_normal_subset(l_a, l_b, no[0]) != 0.0f) &&
      (mesh_face_calc_normal_subset(l_b, l_a, no[1]) != 0.0f)) {
    return dot_v3v3(no[0], no[1]);
  }
  return -1.0f;
}

float mesh_loop_point_side_of_loop_test(const MeshLoop *l, const float co[3])
{
  const float *axis = l->f->no;
  return dist_signed_squared_to_corner_v3v3v3(co, l->prev->v->co, l->v->co, l->next->v->co, axis);
}

float mesh_loop_point_side_of_edge_test(const MeshLoop *l, const float co[3])
{
  const float *axis = l->f->no;
  float dir[3];
  float plane[4];

  sub_v3_v3v3(dir, l->next->v->co, l->v->co);
  cross_v3_v3v3(plane, axis, dir);

  plane[3] = -dot_v3v3(plane, l->v->co);
  return dist_signed_squared_to_plane_v3(co, plane);
}

MeshFace *mesh_vert_pair_share_face_by_angle(
    MeshVert *v_a, MeshVert *v_b, MeshLoop **r_l_a, MeshLoop **r_l_b, const bool allow_adjacent)
{
  MeshLoop *l_cur_a = NULL, *l_cur_b = NULL;
  MeshFace *f_cur = NULL;

  if (v_a->e && v_b->e) {
    MeshIter iter;
    MeshLoop *l_a, *l_b;
    float dot_best = -1.0f;

    MESH_ELEM_ITER (l_a, &iter, v_a, MESH_LOOPS_OF_VERT) {
      l_b = mesh_face_vert_share_loop(l_a->f, v_b);
      if (l_b && (allow_adjacent || !mesh_loop_is_adjacent(l_a, l_b))) {

        if (f_cur == NULL) {
          f_cur = l_a->f;
          l_cur_a = l_a;
          l_cur_b = l_b;
        }
        else {
          /* avoid expensive calculations if we only ever find one face */
          float dot;
          if (dot_best == -1.0f) {
            dot_best = mesh_face_calc_split_dot(l_cur_a, l_cur_b);
          }

          dot = mesh_face_calc_split_dot(l_a, l_b);
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

MeshLoop *mesh_vert_find_first_loop(MeshVert *v)
{
  return v->e ? mesh_disk_faceloop_find_first(v->e, v) : NULL;
}
MeshLoop *mesh_vert_find_first_loop_visible(MeshVert *v)
{
  return v->e ? mesh_disk_faceloop_find_first_visible(v->e, v) : NULL;
}

bool mesh_vert_in_face(MeshVert *v, MeshFace *f)
{
  MeshLoop *l_iter, *l_first;

#ifdef USE_MESH_HOLES
  MeshLoopList *lst;
  for (lst = f->loops.first; lst; lst = lst->next)
#endif
  {
#ifdef USE_MESH_HOLES
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

int mesh_verts_in_face_count(MeshVert **varr, int len, MeshFace *f)
{
  MeshLoop *l_iter, *l_first;

#ifdef USE_MESH_HOLES
  MeshLoopList *lst;
#endif

  int i, count = 0;

  for (i = 0; i < len; i++) {
    MESH_ELEM_API_FLAG_ENABLE(varr[i], _FLAG_OVERLAP);
  }

#ifdef USE_MESH_HOLES
  for (lst = f->loops.first; lst; lst = lst->next)
#endif
  {

#ifdef USE_MESH_HOLES
    l_iter = l_first = lst->first;
#else
    l_iter = l_first = f->l_first;
#endif

    do {
      if (MESH_ELEM_API_FLAG_TEST(l_iter->v, _FLAG_OVERLAP)) {
        count++;
      }

    } while ((l_iter = l_iter->next) != l_first);
  }

  for (i = 0; i < len; i++) {
    MESH_ELEM_API_FLAG_DISABLE(varr[i], _FLAG_OVERLAP);
  }

  return count;
}

bool mesh_verts_in_face(MeshVert **varr, int len, BMFace *f)
{
  MeshLoop *l_iter, *l_first;

#ifdef USE_MESH_HOLES
  MeshLoopList *lst;
#endif

  int i;
  bool ok = true;

  /* simple check, we know can't succeed */
  if (f->len < len) {
    return false;
  }

  for (i = 0; i < len; i++) {
    MESH_ELEM_API_FLAG_ENABLE(varr[i], _FLAG_OVERLAP);
  }

#ifdef USE_MESH_HOLES
  for (lst = f->loops.first; lst; lst = lst->next)
#endif
  {

#ifdef USE_MESH_HOLES
    l_iter = l_first = lst->first;
#else
    l_iter = l_first = f->l_first;
#endif

    do {
      if (MESH_ELEM_API_FLAG_TEST(l_iter->v, _FLAG_OVERLAP)) {
        /* pass */
      }
      else {
        ok = false;
        break;
      }

    } while ((l_iter = l_iter->next) != l_first);
  }

  for (i = 0; i < len; i++) {
    MESH_ELEM_API_FLAG_DISABLE(varr[i], _FLAG_OVERLAP);
  }

  return ok;
}

bool mesh_edge_in_face(const MeshEdge *e, const MeshFace *f)
{
  if (e->l) {
    const MeshLoop *l_iter, *l_first;

    l_iter = l_first = e->l;
    do {
      if (l_iter->f == f) {
        return true;
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  return false;
}

MeshLoop *mesh_edge_other_loop(MeshEdge *e, MeshLoop *l)
{
  MeshLoop *l_other;

  // lib_assert(mesh_edge_is_manifold(e));  // TOO strict, just check if we have another radial face
  lib_assert(e->l && e->l->radial_next != e->l);
  lib_assert(mesh_vert_in_edge(e, l->v));

  l_other = (l->e == e) ? l : l->prev;
  l_other = l_other->radial_next;
  lib_assert(l_other->e == e);

  if (l_other->v == l->v) {
    /* pass */
  }
  else if (l_other->next->v == l->v) {
    l_other = l_other->next;
  }
  else {
    lib_assert(0);
  }

  return l_other;
}

MeshLoop *mesh_vert_step_fan_loop(MeshLoop *l, MeshEdge **e_step)
{
  MeshEdge *e_prev = *e_step;
  MeshEdge *e_next;
  if (l->e == e_prev) {
    e_next = l->prev->e;
  }
  else if (l->prev->e == e_prev) {
    e_next = l->e;
  }
  else {
    lib_assert(0);
    return NULL;
  }

  if (mesh_edge_is_manifold(e_next)) {
    return mesh_edge_other_loop((*e_step = e_next), l);
  }
  return NULL;
}

MeshEdge *mesh_vert_other_disk_edge(MeshVert *v, MeshEdge *e_first)
{
  MeshLoop *l_a;
  int tot = 0;
  int i;

  lib_assert(mesh_vert_in_edge(e_first, v));

  l_a = e_first->l;
  do {
    l_a = mesh_loop_other_vert_loop(l_a, v);
    l_a = mesh_vert_in_edge(l_a->e, v) ? l_a : l_a->prev;
    if (mesh_edge_is_manifold(l_a->e)) {
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
      l_a = mesh_vert_in_edge(l_a->e, v) ? l_a : l_a->prev;
      return l_a->e;
    }

    l_a = mesh_loop_other_vert_loop(l_a, v);
    l_a = mesh_vert_in_edge(l_a->e, v) ? l_a : l_a->prev;
    if (mesh_edge_is_manifold(l_a->e)) {
      l_a = l_a->radial_next;
    }
    /* this won't have changed from the previous loop */

    i++;
  } while (l_a != e_first->l);

  return NULL;
}

float mesh_edge_calc_length(const MeshEdge *e)
{
  return len_v3v3(e->v1->co, e->v2->co);
}

float mesh_edge_calc_length_squared(const MeshEdge *e)
{
  return len_squared_v3v3(e->v1->co, e->v2->co);
}

bool mesh_edge_face_pair(MeshEdge *e, MeshFace **r_fa, MeshFace **r_fb)
{
  MeshLoop *la, *lb;

  if ((la = e->l) && (lb = la->radial_next) && (la != lb) && (lb->radial_next == la)) {
    *r_fa = la->f;
    *r_fb = lb->f;
    return true;
  }

  *r_fa = NULL;
  *r_fb = NULL;
  return false;
}

bool mesh_edge_loop_pair(MeshEdge *e, MeshLoop **r_la, MeshLoop **r_lb)
{
  MeshLoop *la, *lb;

  if ((la = e->l) && (lb = la->radial_next) && (la != lb) && (lb->radial_next == la)) {
    *r_la = la;
    *r_lb = lb;
    return true;
  }

  *r_la = NULL;
  *r_lb = NULL;
  return false;
}

bool mesh_vert_is_edge_pair(const MeshVert *v)
{
  const MeshEdge *e = v->e;
  if (e) {
    MeshEdge *e_other = MESH_DISK_EDGE_NEXT(e, v);
    return ((e_other != e) && (MESH_DISK_EDGE_NEXT(e_other, v) == e));
  }
  return false;
}

bool mesh_vert_is_edge_pair_manifold(const MeshVert *v)
{
  const MeshEdge *e = v->e;
  if (e) {
    MeshEdge *e_other = MESH_DISK_EDGE_NEXT(e, v);
    if (((e_other != e) && (MESH_DISK_EDGE_NEXT(e_other, v) == e))) {
      return mesh_edge_is_manifold(e) && mesh_edge_is_manifold(e_other);
    }
  }
  return false;
}

bool mesh_vert_edge_pair(MeshVert *v, MeshEdge **r_e_a, MeshEdge **r_e_b)
{
  MeshEdge *e_a = v->e;
  if (e_a) {
    MeshEdge *e_b = MESH_DISK_EDGE_NEXT(e_a, v);
    if ((e_b != e_a) && (MESH_DISK_EDGE_NEXT(e_b, v) == e_a)) {
      *r_e_a = e_a;
      *r_e_b = e_b;
      return true;
    }
  }

  *r_e_a = NULL;
  *r_e_b = NULL;
  return false;
}

int mesh_vert_edge_count(const MeshVert *v)
{
  return mesh_disk_count(v);
}

int mesh_vert_edge_count_at_most(const MeshVert *v, const int count_max)
{
  return mesh_disk_count_at_most(v, count_max);
}

int mesh_vert_edge_count_nonwire(const MeshVert *v)
{
  int count = 0;
  MeshIter eiter;
  MeshEdge *edge;
  MESH_ELEM_ITER (edge, &eiter, (MeshVert *)v, MESH_EDGES_OF_VERT) {
    if (edge->l) {
      count++;
    }
  }
  return count;
}
int mesh_edge_face_count(const MeshEdge *e)
{
  int count = 0;

  if (e->l) {
    MeshLoop *l_iter, *l_first;

    l_iter = l_first = e->l;
    do {
      count++;
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  return count;
}

int mesh_edge_face_count_at_most(const MeshEdge *e, const int count_max)
{
  int count = 0;

  if (e->l) {
    MeshLoop *l_iter, *l_first;

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

int mesh_vert_face_count(const MeshVert *v)
{
  return mesh_disk_facevert_count(v);
}

int mesh_vert_face_count_at_most(const MeshVert *v, int count_max)
{
  return mesh_disk_facevert_count_at_most(v, count_max);
}

bool mesh_vert_face_check(const MeshVert *v)
{
  if (v->e != NULL) {
    const MeshEdge *e_iter, *e_first;
    e_first = e_iter = v->e;
    do {
      if (e_iter->l != NULL) {
        return true;
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);
  }
  return false;
}

bool mesh_vert_is_wire(const MeshVert *v)
{
  if (v->e) {
    MeshEdge *e_first, *e_iter;

    e_first = e_iter = v->e;
    do {
      if (e_iter->l) {
        return false;
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);

    return true;
  }
  return false;
}

bool mesh_vert_is_manifold(const MeshVert *v)
{
  MeshEdge *e_iter, *e_first, *e_prev;
  MeshLoop *l_iter, *l_first;
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
     * use mesh_edge_is_manifold() here */
    if (e_iter->l == NULL || (e_iter->l != e_iter->l->radial_next->radial_next)) {
      return false;
    }

    /* count radial loops */
    if (e_iter->l->v == v) {
      loop_num += 1;
    }

    if (!mesh_edge_is_boundary(e_iter)) {
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
  } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);

  e_first = l_first->e;
  l_first = (l_first->v == v) ? l_first : l_first->next;
  lib_assert(l_first->v == v);

  l_iter = l_first;
  e_prev = e_first;

  do {
    loop_num_region += 1;
  } while (((l_iter = mesh_vert_step_fan_loop(l_iter, &e_prev)) != l_first) && (l_iter != NULL));

  return (loop_num == loop_num_region);
}

#define LOOP_VISIT _FLAG_WALK
#define EDGE_VISIT _FLAG_WALK

static int mesh_loop_region_count__recursive(MeshEdge *e, MeshVert *v)
{
  MeshLoop *l_iter, *l_first;
  int count = 0;

  lib_assert(!MESH_ELEM_API_FLAG_TEST(e, EDGE_VISIT));
  MESH_ELEM_API_FLAG_ENABLE(e, EDGE_VISIT);

  l_iter = l_first = e->l;
  do {
    if (l_iter->v == v) {
      MeshEdge *e_other = l_iter->prev->e;
      if (!MESH_ELEM_API_FLAG_TEST(l_iter, LOOP_VISIT)) {
        MESH_ELEM_API_FLAG_ENABLE(l_iter, LOOP_VISIT);
        count += 1;
      }
      if (!MESH_ELEM_API_FLAG_TEST(e_other, EDGE_VISIT)) {
        count += mesh_loop_region_count__recursive(e_other, v);
      }
    }
    else if (l_iter->next->v == v) {
      MeshEdge *e_other = l_iter->next->e;
      if (!MESH_ELEM_API_FLAG_TEST(l_iter->next, LOOP_VISIT)) {
        MESH_ELEM_API_FLAG_ENABLE(l_iter->next, LOOP_VISIT);
        count += 1;
      }
      if (!MESH_ELEM_API_FLAG_TEST(e_other, EDGE_VISIT)) {
        count += mesh_loop_region_count__recursive(e_other, v);
      }
    }
    else {
      lib_assert(0);
    }
  } while ((l_iter = l_iter->radial_next) != l_first);

  return count;
}

static int mesh_loop_region_count__clear(MeshLoop *l)
{
  int count = 0;
  MeshEdge *e_iter, *e_first;

  /* clear flags */
  e_iter = e_first = l->e;
  do {
    MESH_ELEM_API_FLAG_DISABLE(e_iter, EDGE_VISIT);
    if (e_iter->l) {
      MeshLoop *l_iter, *l_first;
      l_iter = l_first = e_iter->l;
      do {
        if (l_iter->v == l->v) {
          MESH_ELEM_API_FLAG_DISABLE(l_iter, LOOP_VISIT);
          count += 1;
        }
      } while ((l_iter = l_iter->radial_next) != l_first);
    }
  } while ((e_iter = MESH_DISK_EDGE_NEXT(e_iter, l->v)) != e_first);

  return count;
}

int mesh_loop_region_loops_count_at_most(BMLoop *l, int *r_loop_total)
{
  const int count = mesh_loop_region_count__recursive(l->e, l->v);
  const int count_total = bm_loop_region_count__clear(l);
  if (r_loop_total) {
    *r_loop_total = count_total;
  }
  return count;
}

#undef LOOP_VISIT
#undef EDGE_VISIT

int mesh_loop_region_loops_count(MeshLoop *l)
{
  return mesh_loop_region_loops_count_at_most(l, NULL);
}

bool mesh_vert_is_manifold_region(const MeshVert *v)
{
  MeshLoop *l_first = mesh_vert_find_first_loop((MeshVert *)v);
  if (l_first) {
    int count, count_total;
    count = mesh_loop_region_loops_count_at_most(l_first, &count_total);
    return (count == count_total);
  }
  return true;
}

bool mesh_edge_is_convex(const MeshEdge *e)
{
  if (mesh_edge_is_manifold(e)) {
    MeshLoop *l1 = e->l;
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

bool mesh_edge_is_contiguous_loop_cd(const MeshEdge *e,
                                     const int cd_loop_type,
                                     const int cd_loop_offset)
{
  lib_assert(cd_loop_offset != -1);

  if (e->l && e->l->radial_next != e->l) {
    const MeshLoop *l_base_v1 = e->l;
    const MeshLoop *l_base_v2 = e->l->next;
    const void *l_base_cd_v1 = MESH_ELEM_CD_GET_VOID_P(l_base_v1, cd_loop_offset);
    const void *l_base_cd_v2 = MESH_ELEM_CD_GET_VOID_P(l_base_v2, cd_loop_offset);
    const MeshLoop *l_iter = e->l->radial_next;
    do {
      const MeshLoop *l_iter_v1;
      const MeshLoop *l_iter_v2;
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
      lib_assert((l_iter_v1->v == l_base_v1->v) && (l_iter_v2->v == l_base_v2->v));

      l_iter_cd_v1 = MESH_ELEM_CD_GET_VOID_P(l_iter_v1, cd_loop_offset);
      l_iter_cd_v2 = MESH_ELEM_CD_GET_VOID_P(l_iter_v2, cd_loop_offset);

      if ((CustomData_data_equals(cd_loop_type, l_base_cd_v1, l_iter_cd_v1) == 0) ||
          (CustomData_data_equals(cd_loop_type, l_base_cd_v2, l_iter_cd_v2) == 0)) {
        return false;
      }

    } while ((l_iter = l_iter->radial_next) != e->l);
  }
  return true;
}

bool mesh_vert_is_boundary(const MeshVert *v)
{
  if (v->e) {
    MeshEdge *e_first, *e_iter;

    e_first = e_iter = v->e;
    do {
      if (mesh_edge_is_boundary(e_iter)) {
        return true;
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);

    return false;
  }
  return false;
}

int mesh_face_share_face_count(MeshFace *f_a, MeshFace *f_b)
{
  MeshIter iter1, iter2;
  MeshEdge *e;
  MeshFace *f;
  int count = 0;

  MESH_ELEM_ITER (e, &iter1, f_a, MESH_EDGES_OF_FACE) {
    MESH_ELEM_ITER (f, &iter2, e, MESH_FACES_OF_EDGE) {
      if (f != f_a && f != f_b && mesh_face_share_edge_check(f, f_b)) {
        count++;
      }
    }
  }

  return count;
}

bool mesh_face_share_face_check(MeshFace *f_a, MeshFace *f_b)
{
  MeshIter iter1, iter2;
  MeshEdge *e;
  MeshFace *f;

  MESH_ELEM_ITER (e, &iter1, f_a, MESH_EDGES_OF_FACE) {
    MESH_ELEM_ITER (f, &iter2, e, MESH_FACES_OF_EDGE) {
      if (f != f_a && f != f_b && mesh_face_share_edge_check(f, f_b)) {
        return true;
      }
    }
  }

  return false;
}

int mesh_face_share_edge_count(MeshFace *f_a, MeshFace *f_b)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;
  int count = 0;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f_a);
  do {
    if (mesh_edge_in_face(l_iter->e, f_b)) {
      count++;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return count;
}

bool mesh_face_share_edge_check(MeshFace *f1, MeshFace *f2)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f1);
  do {
    if (mesh_edge_in_face(l_iter->e, f2)) {
      return true;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return false;
}

int mesh_face_share_vert_count(MeshFace *f_a, MeshFace *f_b)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;
  int count = 0;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f_a);
  do {
    if (mesh_vert_in_face(l_iter->v, f_b)) {
      count++;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return count;
}

bool mesh_face_share_vert_check(MeshFace *f_a, MeshFace *f_b)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f_a);
  do {
    if (mesh_vert_in_face(l_iter->v, f_b)) {
      return true;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return false;
}

bool mesh_loop_share_edge_check(MeshLoop *l_a, MeshLoop *l_b)
{
  lib_assert(l_a->v == l_b->v);
  return (ELEM(l_a->e, l_b->e, l_b->prev->e) || ELEM(l_b->e, l_a->e, l_a->prev->e));
}

bool mesh_edge_share_face_check(MeshEdge *e1, MeshEdge *e2)
{
  MeshLoop *l;
  MeshFace *f;

  if (e1->l && e2->l) {
    l = e1->l;
    do {
      f = l->f;
      if (mesh_edge_in_face(e2, f)) {
        return true;
      }
      l = l->radial_next;
    } while (l != e1->l);
  }
  return false;
}

bool mesh_edge_share_quad_check(MeshEdge *e1, MeshEdge *e2)
{
  MeshLoop *l;
  MeshFace *f;

  if (e1->l && e2->l) {
    l = e1->l;
    do {
      f = l->f;
      if (f->len == 4) {
        if (mesh_edge_in_face(e2, f)) {
          return true;
        }
      }
      l = l->radial_next;
    } while (l != e1->l);
  }
  return false;
}

bool mesh_edge_share_vert_check(MeshEdge *e1, MeshEdge *e2)
{
  return (e1->v1 == e2->v1 || e1->v1 == e2->v2 || e1->v2 == e2->v1 || e1->v2 == e2->v2);
}

MeshVert *mesh_edge_share_vert(MeshEdge *e1, MeshEdge *e2)
{
  lib_assert(e1 != e2);
  if (mesh_vert_in_edge(e2, e1->v1)) {
    return e1->v1;
  }
  if (mesh_vert_in_edge(e2, e1->v2)) {
    return e1->v2;
  }
  return NULL;
}

MeshLoop *mesh_edge_vert_share_loop(M shLoop *l, MeshVert *v)
{
  lib_assert(mesh_vert_in_edge(l->e, v));
  if (l->v == v) {
    return l;
  }
  return l->next;
}

MeshLoop *mesh_face_vert_share_loop(MeshFace *f, MeshVert *v)
{
  MeshLoop *l_first;
  MeshLoop *l_iter;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    if (l_iter->v == v) {
      return l_iter;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return NULL;
}

MeshLoop *mesh_face_edge_share_loop(MeshFace *f, MeshEdge *e)
{
  MeshLoop *l_first;
  MeshLoop *l_iter;

  l_iter = l_first = e->l;
  do {
    if (l_iter->f == f) {
      return l_iter;
    }
  } while ((l_iter = l_iter->radial_next) != l_first);

  return NULL;
}

void mesh_edge_ordered_verts_ex(const MeshEdge *edge,
                                MeshVert **r_v1,
                                MeshVert **r_v2,
                                const MeshLoop *edge_loop)
{
  lib_assert(edge_loop->e == edge);
  (void)edge; /* quiet warning in release build */
  *r_v1 = edge_loop->v;
  *r_v2 = edge_loop->next->v;
}

void mesh_edge_ordered_verts(const MeshEdge *edge, MeshVert **r_v1, MeshVert **r_v2)
{
  mesh_edge_ordered_verts_ex(edge, r_v1, r_v2, edge->l);
}

MeshLoop *mesh_loop_find_prev_nodouble(MeshLoop *l, MeshLoop *l_stop, const float eps_sq)
{
  MeshLoop *l_step = l->prev;

  lib_assert(!ELEM(l_stop, NULL, l));

  while (UNLIKELY(len_squared_v3v3(l->v->co, l_step->v->co) < eps_sq)) {
    l_step = l_step->prev;
    lib_assert(l_step != l);
    if (UNLIKELY(l_step == l_stop)) {
      return NULL;
    }
  }

  return l_step;
}

MeshLoop *mesh_loop_find_next_nodouble(MeshLoop *l, MeshLoop *l_stop, const float eps_sq)
{
  MeshLoop *l_step = l->next;

  lib_assert(!ELEM(l_stop, NULL, l));

  while (UNLIKELY(len_squared_v3v3(l->v->co, l_step->v->co) < eps_sq)) {
    l_step = l_step->next;
    lib_assert(l_step != l);
    if (UNLIKELY(l_step == l_stop)) {
      return NULL;
    }
  }

  return l_step;
}

bool mesh_loop_is_convex(const MeshLoop *l)
{
  float e_dir_prev[3];
  float e_dir_next[3];
  float l_no[3];

  sub_v3_v3v3(e_dir_prev, l->prev->v->co, l->v->co);
  sub_v3_v3v3(e_dir_next, l->next->v->co, l->v->co);
  cross_v3_v3v3(l_no, e_dir_next, e_dir_prev);
  return dot_v3v3(l_no, l->f->no) > 0.0f;
}

float mesh_loop_calc_face_angle(const MeshLoop *l)
{
  return angle_v3v3v3(l->prev->v->co, l->v->co, l->next->v->co);
}

float mesh_loop_calc_face_normal_safe_ex(const MeshLoop *l, const float epsilon_sq, float r_normal[3])
{
  /* NOTE: we cannot use result of normal_tri_v3 here to detect colinear vectors
   * (vertex on a straight line) from zero value,
   * because it does not normalize both vectors before making cross-product.
   * Instead of adding two costly normalize computations,
   * just check ourselves for colinear case. */
  /* NOTE: FEPSILON might need some finer tweaking at some point?
   * Seems to be working OK for now though. */
  float v1[3], v2[3], v_tmp[3];
  sub_v3_v3v3(v1, l->prev->v->co, l->v->co);
  sub_v3_v3v3(v2, l->next->v->co, l->v->co);

  const float fac = ((v2[0] == 0.0f) ?
                         ((v2[1] == 0.0f) ? ((v2[2] == 0.0f) ? 0.0f : v1[2] / v2[2]) :
                                            v1[1] / v2[1]) :
                         v1[0] / v2[0]);

  mul_v3_v3fl(v_tmp, v2, fac);
  sub_v3_v3(v_tmp, v1);
  if (fac != 0.0f && !is_zero_v3(v1) && len_squared_v3(v_tmp) > epsilon_sq) {
    /* Not co-linear, we can compute cross-product and normalize it into normal. */
    cross_v3_v3v3(r_normal, v1, v2);
    return normalize_v3(r_normal);
  }
  copy_v3_v3(r_normal, l->f->no);
  return 0.0f;
}

float mesh_loop_calc_face_normal_safe_vcos_ex(const MeshLoop *l,
                                              const float normal_fallback[3],
                                              float const (*vertexCos)[3],
                                              const float epsilon_sq,
                                              float r_normal[3])
{
  const int i_prev = mesh_elem_index_get(l->prev->v);
  const int i_next = mesh_elem_index_get(l->next->v);
  const int i = mesh_elem_index_get(l->v);

  float v1[3], v2[3], v_tmp[3];
  sub_v3_v3v3(v1, vertexCos[i_prev], vertexCos[i]);
  sub_v3_v3v3(v2, vertexCos[i_next], vertexCos[i]);

  const float fac = ((v2[0] == 0.0f) ?
                         ((v2[1] == 0.0f) ? ((v2[2] == 0.0f) ? 0.0f : v1[2] / v2[2]) :
                                            v1[1] / v2[1]) :
                         v1[0] / v2[0]);

  mul_v3_v3fl(v_tmp, v2, fac);
  sub_v3_v3(v_tmp, v1);
  if (fac != 0.0f && !is_zero_v3(v1) && len_squared_v3(v_tmp) > epsilon_sq) {
    /* Not co-linear, we can compute cross-product and normalize it into normal. */
    cross_v3_v3v3(r_normal, v1, v2);
    return normalize_v3(r_normal);
  }
  copy_v3_v3(r_normal, normal_fallback);
  return 0.0f;
}

float mesh_loop_calc_face_normal_safe(const MeshLoop *l, float r_normal[3])
{
  return mesh_loop_calc_face_normal_safe_ex(l, 1e-5f, r_normal);
}

float mesh_loop_calc_face_normal_safe_vcos(const MeshLoop *l,
                                         const float normal_fallback[3],
                                         float const (*vertexCos)[3],
                                         float r_normal[3])

{
  return mesh_loop_calc_face_normal_safe_vcos_ex(l, normal_fallback, vertexCos, 1e-5f, r_normal);
}

float mesh_loop_calc_face_normal(const MeshLoop *l, float r_normal[3])
{
  float v1[3], v2[3];
  sub_v3_v3v3(v1, l->prev->v->co, l->v->co);
  sub_v3_v3v3(v2, l->next->v->co, l->v->co);

  cross_v3_v3v3(r_normal, v1, v2);
  const float len = normalize_v3(r_normal);
  if (UNLIKELY(len == 0.0f)) {
    copy_v3_v3(r_normal, l->f->no);
  }
  return len;
}

void mesh_loop_calc_face_direction(const MeshLoop *l, float r_dir[3])
{
  float v_prev[3];
  float v_next[3];

  sub_v3_v3v3(v_prev, l->v->co, l->prev->v->co);
  sub_v3_v3v3(v_next, l->next->v->co, l->v->co);

  normalize_v3(v_prev);
  normalize_v3(v_next);

  add_v3_v3v3(r_dir, v_prev, v_next);
  normalize_v3(r_dir);
}

void mesh_loop_calc_face_tangent(const MeshLoop *l, float r_tangent[3])
{
  float v_prev[3];
  float v_next[3];
  float dir[3];

  sub_v3_v3v3(v_prev, l->prev->v->co, l->v->co);
  sub_v3_v3v3(v_next, l->v->co, l->next->v->co);

  normalize_v3(v_prev);
  normalize_v3(v_next);
  add_v3_v3v3(dir, v_prev, v_next);

  if (compare_v3v3(v_prev, v_next, FLT_EPSILON * 10.0f) == false) {
    float nor[3]; /* for this purpose doesn't need to be normalized */
    cross_v3_v3v3(nor, v_prev, v_next);
    /* concave face check */
    if (UNLIKELY(dot_v3v3(nor, l->f->no) < 0.0f)) {
      negate_v3(nor);
    }
    cross_v3_v3v3(r_tangent, dir, nor);
  }
  else {
    /* prev/next are the same - compare with face normal since we don't have one */
    cross_v3_v3v3(r_tangent, dir, l->f->no);
  }

  normalize_v3(r_tangent);
}

float mesh_edge_calc_face_angle_ex(const MeshEdge *e, const float fallback)
{
  if (mesh_edge_is_manifold(e)) {
    const MeshLoop *l1 = e->l;
    const MeshLoop *l2 = e->l->radial_next;
    return angle_normalized_v3v3(l1->f->no, l2->f->no);
  }
  return fallback;
}
float mesh_edge_calc_face_angle(const MeshEdge *e)
{
  return mesh_edge_calc_face_angle_ex(e, DEG2RADF(90.0f));
}

float mesh_edge_calc_face_angle_with_imat3_ex(const MeshEdge *e,
                                              const float imat3[3][3],
                                              const float fallback)
{
  if (mesh_edge_is_manifold(e)) {
    const MeshLoop *l1 = e->l;
    const MeshLoop *l2 = e->l->radial_next;
    float no1[3], no2[3];
    copy_v3_v3(no1, l1->f->no);
    copy_v3_v3(no2, l2->f->no);

    mul_transposed_m3_v3(imat3, no1);
    mul_transposed_m3_v3(imat3, no2);

    normalize_v3(no1);
    normalize_v3(no2);

    return angle_normalized_v3v3(no1, no2);
  }
  return fallback;
}
float mesh_edge_calc_face_angle_with_imat3(const MeshEdge *e, const float imat3[3][3])
{
  return mesh_edge_calc_face_angle_with_imat3_ex(e, imat3, DEG2RADF(90.0f));
}

float mesh_edge_calc_face_angle_signed_ex(const MeshEdge *e, const float fallback)
{
  if (mesh_edge_is_manifold(e)) {
    MeshLoop *l1 = e->l;
    MeshLoop *l2 = e->l->radial_next;
    const float angle = angle_normalized_v3v3(l1->f->no, l2->f->no);
    return mesh_edge_is_convex(e) ? angle : -angle;
  }
  return fallback;
}
float mesh_edge_calc_face_angle_signed(const MeshEdge *e)
{
  return mesh_edge_calc_face_angle_signed_ex(e, DEG2RADF(90.0f));
}

void mesh_edge_calc_face_tangent(const MeshEdge *e, const MeshLoop *e_loop, float r_tangent[3])
{
  float tvec[3];
  MeshVert *v1, *v2;
  mesh_edge_ordered_verts_ex(e, &v1, &v2, e_loop);

  sub_v3_v3v3(tvec, v1->co, v2->co); /* use for temp storage */
  /* NOTE: we could average the tangents of both loops,
   * for non flat ngons it will give a better direction */
  cross_v3_v3v3(r_tangent, tvec, e_loop->f->no);
  normalize_v3(r_tangent);
}

float mesh_vert_calc_edge_angle_ex(const MeshVert *v, const float fallback)
{
  MeshEdge *e1, *e2;

  /* Saves `mesh_vert_edge_count(v)` and edge iterator,
   * get the edges and count them both at once. */

  if ((e1 = v->e) && (e2 = mesh_disk_edge_next(e1, v)) && (e1 != e2) &&
      /* make sure we come full circle and only have 2 connected edges */
      (e1 == mesh_disk_edge_next(e2, v))) {
    MeshVert *v1 = mesh_edge_other_vert(e1, v);
    MeshVert *v2 = mesh_edge_other_vert(e2, v);

    return (float)M_PI - angle_v3v3v3(v1->co, v->co, v2->co);
  }
  return fallback;
}

float mesh_vert_calc_edge_angle(const MeshVert *v)
{
  return mesh_vert_calc_edge_angle_ex(v, DEG2RADF(90.0f));
}

float mesh_vert_calc_shell_factor(const MeshVert *v)
{
  MeshIter iter;
  MeshLoop *l;
  float accum_shell = 0.0f;
  float accum_angle = 0.0f;

  MESH_ITER_ELEM (l, &iter, (MeshVert *)v, MESH_LOOPS_OF_VERT) {
    const float face_angle = mesh_loop_calc_face_angle(l);
    accum_shell += shell_v3v3_normalized_to_dist(v->no, l->f->no) * face_angle;
    accum_angle += face_angle;
  }

  if (accum_angle != 0.0f) {
    return accum_shell / accum_angle;
  }
  return 1.0f;
}
float mesh_vert_calc_shell_factor_ex(const MeshVert *v, const float no[3], const char hflag)
{
  MeshIter iter;
  const MeshLoop *l;
  float accum_shell = 0.0f;
  float accum_angle = 0.0f;
  int tot_sel = 0, tot = 0;

  MESH_ELEM_ITER (l, &iter, (MeshVert *)v, MESH_LOOPS_OF_VERT) {
    if (mesh_elem_flag_test(l->f, hflag)) { /* <-- main difference to mesh_vert_calc_shell_factor! */
      const float face_angle = mesh_loop_calc_face_angle(l);
      accum_shell += shell_v3v3_normalized_to_dist(no, l->f->no) * face_angle;
      accum_angle += face_angle;
      tot_sel++;
    }
    tot++;
  }

  if (accum_angle != 0.0f) {
    return accum_shell / accum_angle;
  }
  /* other main difference from mesh_vert_calc_shell_factor! */
  if (tot != 0 && tot_sel == 0) {
    /* none selected, so use all */
    return mesh_vert_calc_shell_factor(v);
  }
  return 1.0f;
}

float mesh_vert_calc_median_tagged_edge_length(const MeshVert *v)
{
  MeshIter iter;
  MeshEdge *e;
  int tot;
  float length = 0.0f;

  MESH_ITER_ELEM_INDEX (e, &iter, (MeshVert *)v, MESH_EDGES_OF_VERT, tot) {
    const MeshVert *v_other = mesh_edge_other_vert(e, v);
    if (mesh_elem_flag_test(v_other, MESH_ELEM_TAG)) {
      length += mesh_edge_calc_length(e);
    }
  }

  if (tot) {
    return length / (float)tot;
  }
  return 0.0f;
}

MeshLoop *mesh_face_find_shortest_loop(MeshFace *f)
{
  MeshLoop *shortest_loop = NULL;
  float shortest_len = FLT_MAX;

  MeshLoop *l_iter;
  MeshLoop *l_first;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);

  do {
    const float len_sq = len_squared_v3v3(l_iter->v->co, l_iter->next->v->co);
    if (len_sq <= shortest_len) {
      shortest_loop = l_iter;
      shortest_len = len_sq;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return shortest_loop;
}

MeshLoop *mesh_face_find_longest_loop(MeshFace *f)
{
  MeshLoop *longest_loop = NULL;
  float len_max_sq = 0.0f;

  MeshLoop *l_iter;
  MeshLoop *l_first;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);

  do {
    const float len_sq = len_squared_v3v3(l_iter->v->co, l_iter->next->v->co);
    if (len_sq >= len_max_sq) {
      longest_loop = l_iter;
      len_max_sq = len_sq;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return longest_loop;
}

/**
 * Returns the edge existing between v_a and v_b, or NULL if there isn't one.
 *
 * multiple edges may exist between any two vertices, and therefore
 * this function only returns the first one found.
 */
#if 0
MeshEdge *mesh_edge_exists(MeshVert *v_a, MeshVert *v_b)
{
  MeshIter iter;
  MeshEdge *e;

  lib_assert(v_a != v_b);
  lib_assert(v_a->head.htype == MESH_VERT && v_b->head.htype == MESH_VERT);

  MESH_ELEM_ITER (e, &iter, v_a, MESH_EDGES_OF_VERT) {
    if (e->v1 == v_b || e->v2 == v_b) {
      return e;
    }
  }

  return NULL;
}
#else
MeshEdge *mesh_edge_exists(MeshVert *v_a, MeshVert *v_b)
{
  /* speedup by looping over both edges verts
   * where one vert may connect to many edges but not the other. */

  MeshEdge *e_a, *e_b;

  lib_assert(v_a != v_b);
  lib_assert(v_a->head.htype == MESH_VERT && v_b->head.htype == MESH_VERT);

  if ((e_a = v_a->e) && (e_b = v_b->e)) {
    MeshEdge *e_a_iter = e_a, *e_b_iter = e_b;

    do {
      if (mesh_vert_in_edge(e_a_iter, v_b)) {
        return e_a_iter;
      }
      if (mesh_vert_in_edge(e_b_iter, v_a)) {
        return e_b_iter;
      }
    } while (((e_a_iter = mesh_disk_edge_next(e_a_iter, v_a)) != e_a) &&
             ((e_b_iter = mesh_disk_edge_next(e_b_iter, v_b)) != e_b));
  }

  return NULL;
}
#endif

MeshEdge *mesh_edge_find_double(MeshEdge *e)
{
  MeshVert *v = e->v1;
  MeshVert *v_other = e->v2;

  MeshEdge *e_iter;

  e_iter = e;
  while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e) {
    if (UNLIKELY(mesh_vert_in_edge(e_iter, v_other))) {
      return e_iter;
    }
  }

  return NULL;
}

MeshLoop *mesh_edge_find_first_loop_visible(MeshEdge *e)
{
  if (e->l != NULL) {
    MeshLoop *l_iter, *l_first;
    l_iter = l_first = e->l;
    do {
      if (!mesh_elem_flag_test(l_iter->f, MESH_ELEM_HIDDEN)) {
        return l_iter;
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }
  return NULL;
}

MeshFace *mesh_face_exists(MeshVert **varr, int len)
{
  if (varr[0]->e) {
    MeshEdge *e_iter, *e_first;
    e_iter = e_first = varr[0]->e;

    /* would normally use MESH_LOOPS_OF_VERT, but this runs so often,
     * its faster to iterate on the data directly */
    do {
      if (e_iter->l) {
        MeshLoop *l_iter_radial, *l_first_radial;
        l_iter_radial = l_first_radial = e_iter->l;

        do {
          if ((l_iter_radial->v == varr[0]) && (l_iter_radial->f->len == len)) {
            /* the fist 2 verts match, now check the remaining (len - 2) faces do too
             * winding isn't known, so check in both directions */
            int i_walk = 2;

            if (l_iter_radial->next->v == varr[1]) {
              MeshLoop *l_walk = l_iter_radial->next->next;
              do {
                if (l_walk->v != varr[i_walk]) {
                  break;
                }
              } while ((void)(l_walk = l_walk->next), ++i_walk != len);
            }
            else if (l_iter_radial->prev->v == varr[1]) {
              MeshLoop *l_walk = l_iter_radial->prev->prev;
              do {
                if (l_walk->v != varr[i_walk]) {
                  break;
                }
              } while ((void)(l_walk = l_walk->prev), ++i_walk != len);
            }

            if (i_walk == len) {
              return l_iter_radial->f;
            }
          }
        } while ((l_iter_radial = l_iter_radial->radial_next) != l_first_radial);
      }
    } while ((e_iter = MESH_DISK_EDGE_NEXT(e_iter, varr[0])) != e_first);
  }

  return NULL;
}

MeshFace *mesh_face_find_double(MeshFace *f)
{
  MeshLoop *l_first = MESH_FACE_FIRST_LOOP(f);
  for (MeshLoop *l_iter = l_first->radial_next; l_first != l_iter; l_iter = l_iter->radial_next) {
    if (l_iter->f->len == l_first->f->len) {
      if (l_iter->v == l_first->v) {
        MeshLoop *l_a = l_first, *l_b = l_iter, *l_b_init = l_iter;
        do {
          if (l_a->e != l_b->e) {
            break;
          }
        } while (((void)(l_a = l_a->next), (l_b = l_b->next)) != l_b_init);
        if (l_b == l_b_init) {
          return l_iter->f;
        }
      }
      else {
        MeshLoop *l_a = l_first, *l_b = l_iter, *l_b_init = l_iter;
        do {
          if (l_a->e != l_b->e) {
            break;
          }
        } while (((void)(l_a = l_a->prev), (l_b = l_b->next)) != l_b_init);
        if (l_b == l_b_init) {
          return l_iter->f;
        }
      }
    }
  }
  return NULL;
}

bool mesh_face_exists_multi(MeshVert **varr, MeshEdge **earr, int len)
{
  MeshFace *f;
  MeshEdge *e;
  MeshVert *v;
  bool ok;
  int tot_tag;

  MeshIter fiter;
  MeshIter viter;

  int i;

  for (i = 0; i < len; i++) {
    /* save some time by looping over edge faces rather than vert faces
     * will still loop over some faces twice but not as many */
    MESH_ITER_ELEM (f, &fiter, earr[i], MESH_FACES_OF_EDGE) {
      mesh_elem_flag_disable(f, MESH_ELEM_INTERNAL_TAG);
      MESH_ITER_ELEM (v, &viter, f, MEAH_VERTS_OF_FACE) {
        mesh_elem_flag_disable(v, MESH_ELEM_INTERNAL_TAG);
      }
    }

    /* clear all edge tags */
    MESH_ITER_ELEM (e, &fiter, varr[i], MESH_EDGES_OF_VERT) {
      MESH_elem_flag_disable(e, MESH_ELEM_INTERNAL_TAG);
    }
  }

  /* now tag all verts and edges in the boundary array as true so
   * we can know if a face-vert is from our array */
  for (i = 0; i < len; i++) {
    mesh_elem_flag_enable(varr[i], MESH_ELEM_INTERNAL_TAG);
    mesh_elem_flag_enable(earr[i], MESH_ELEM_INTERNAL_TAG);
  }

  /* so! boundary is tagged, everything else cleared */

  /* 1) tag all faces connected to edges - if all their verts are boundary */
  tot_tag = 0;
  for (i = 0; i < len; i++) {
    MESH_ITER_ELEM (f, &fiter, earr[i], MESH_FACES_OF_EDGE) {
      if (!mesh_elem_flag_test(f, MESH_ELEM_INTERNAL_TAG)) {
        ok = true;
        MESH_ITER_ELEM (v, &viter, f, MESH_VERTS_OF_FACE) {
          if (!mesh_elem_flag_test(v, MESH_ELEM_INTERNAL_TAG)) {
            ok = false;
            break;
          }
        }

        if (ok) {
          /* we only use boundary verts */
          mesh_elem_flag_enable(f, MESH_ELEM_INTERNAL_TAG);
          tot_tag++;
        }
      }
      else {
        /* we already found! */
      }
    }
  }

  if (tot_tag == 0) {
    /* no faces use only boundary verts, quit early */
    ok = false;
    goto finally;
  }

  /* 2) loop over non-boundary edges that use boundary verts,
   *    check each have 2 tagged faces connected (faces that only use 'varr' verts) */
  ok = true;
  for (i = 0; i < len; i++) {
    MESH_ELEM_ITER (e, &fiter, varr[i], MESH_EDGES_OF_VERT) {

      if (/* non-boundary edge */
          mesh_elem_flag_test(e, MESH_ELEM_INTERNAL_TAG) == false &&
          /* ...using boundary verts */
          mesh_elem_flag_test(e->v1, MESH_ELEM_INTERNAL_TAG) &&
          mesh_elem_flag_test(e->v2, MESH_ELEM_INTERNAL_TAG)) {
        int tot_face_tag = 0;
        MESH_ELEM_ITER (f, &fiter, e, MESH_FACES_OF_EDGE) {
          if (mesh_elem_flag_test(f, MESH_ELEM_INTERNAL_TAG)) {
            tot_face_tag++;
          }
        }

        if (tot_face_tag != 2) {
          ok = false;
          break;
        }
      }
    }

    if (ok == false) {
      break;
    }
  }

finally:
  /* Cleanup */
  for (i = 0; i < len; i++) {
    mesh_elem_flag_disable(varr[i], MESH_ELEM_INTERNAL_TAG);
    mesh_elem_flag_disable(earr[i], MESH_ELEM_INTERNAL_TAG);
  }
  return ok;
}

bool mesh_face_exists_multi_edge(MeshEdge **earr, int len)
{
  MeshVert **varr = lib_array_alloca(varr, len);

  /* first check if verts have edges, if not we can bail out early */
  if (!mesh_verts_from_edges(varr, earr, len)) {
    BMESH_ASSERT(0);
    return false;
  }

  return mesh_face_exists_multi(varr, earr, len);
}

MeshFace *mesh_face_exists_overlap(MeshVert **varr, const int len)
{
  MeshIter viter;
  MeshFace *f;
  int i;
  MeshFace *f_overlap = NULL;
  LinkNode *f_lnk = NULL;

#ifdef DEBUG
  /* check flag isn't already set */
  for (i = 0; i < len; i++) {
    MESH_ITER_ELEM (f, &viter, varr[i], MESH_FACES_OF_VERT) {
      lib_assert(MESH_ELEM_API_FLAG_TEST(f, _FLAG_OVERLAP) == 0);
    }
  }
#endif

  for (i = 0; i < len; i++) {
    MESH_ITER_ELEM (f, &viter, varr[i], MESH_FACES_OF_VERT) {
      if (MESH_ELEM_API_FLAG_TEST(f, _FLAG_OVERLAP) == 0) {
        if (len <= mesh_verts_in_face_count(varr, len, f)) {
          f_overlap = f;
          break;
        }

        MESH_ELEM_API_FLAG_ENABLE(f, _FLAG_OVERLAP);
        lib_linklist_prepend_alloca(&f_lnk, f);
      }
    }
  }

  for (; f_lnk; f_lnk = f_lnk->next) {
    MESH_ELEM_API_FLAG_DISABLE((MeshFace *)f_lnk->link, _FLAG_OVERLAP);
  }

  return f_overlap;
}

bool mesh_face_exists_overlap_subset(MeshVert **varr, const int len)
{
  MeshIter viter;
  MeshFace *f;
  bool is_init = false;
  bool is_overlap = false;
  LinkNode *f_lnk = NULL;

#ifdef DEBUG
  /* check flag isn't already set */
  for (int i = 0; i < len; i++) {
    lib_assert(MESH_ELEM_API_FLAG_TEST(varr[i], _FLAG_OVERLAP) == 0);
    MESH_ELEM_ITER (f, &viter, varr[i], MESH_FACES_OF_VERT) {
      lib_assert(MESH_ELEM_API_FLAG_TEST(f, _FLAG_OVERLAP) == 0);
    }
  }
#endif

  for (int i = 0; i < len; i++) {
    MESH_ELEM_ITER (f, &viter, varr[i], MESH_FACES_OF_VERT) {
      if ((f->len <= len) && (MESH_ELEM_API_FLAG_TEST(f, _FLAG_OVERLAP) == 0)) {
        /* Check if all vers in this face are flagged. */
        MeshLoop *l_iter, *l_first;

        if (is_init == false) {
          is_init = true;
          for (int j = 0; j < len; j++) {
            MESH_ELEM_API_FLAG_ENABLE(varr[j], _FLAG_OVERLAP);
          }
        }

        l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
        is_overlap = true;
        do {
          if (MESH_ELEM_API_FLAG_TEST(l_iter->v, _FLAG_OVERLAP) == 0) {
            is_overlap = false;
            break;
          }
        } while ((l_iter = l_iter->next) != l_first);

        if (is_overlap) {
          break;
        }

        MESH_ELEM_API_FLAG_ENABLE(f, _FLAG_OVERLAP);
        lib_linklist_prepend_alloca(&f_lnk, f);
      }
    }
  }

  if (is_init == true) {
    for (int i = 0; i < len; i++) {
      MESH_ELEM_API_FLAG_DISABLE(varr[i], _FLAG_OVERLAP);
    }
  }

  for (; f_lnk; f_lnk = f_lnk->next) {
    MESH_ELEM_API_FLAG_DISABLE((MeshFace *)f_lnk->link, _FLAG_OVERLAP);
  }

  return is_overlap;
}

bool mesh_vert_is_all_edge_flag_test(const MeshVert *v, const char hflag, const bool respect_hide)
{
  if (v->e) {
    MeshEdge *e_other;
    MeshIter eiter;

    MESH_ELEM_ITER (e_other, &eiter, (MeshVert *)v, MESH_EDGES_OF_VERT) {
      if (!respect_hide || !mesh_elem_flag_test(e_other, MESH_ELEM_HIDDEN)) {
        if (!mesh_elem_flag_test(e_other, hflag)) {
          return false;
        }
      }
    }
  }

  return true;
}

bool mesh_vert_is_all_face_flag_test(const MeshVert *v, const char hflag, const bool respect_hide)
{
  if (v->e) {
    MeshEdge *f_other;
    MeshIter fiter;

    MESH_ELEM_ITER (f_other, &fiter, (MeshVert *)v, MESH_FACES_OF_VERT) {
      if (!respect_hide || !mesh_elem_flag_test(f_other, MESH_ELEM_HIDDEN)) {
        if (!mesh_elem_flag_test(f_other, hflag)) {
          return false;
        }
      }
    }
  }

  return true;
}

bool mesh_edge_is_all_face_flag_test(const MeshEdge *e, const char hflag, const bool respect_hide)
{
  if (e->l) {
    MeshLoop *l_iter, *l_first;

    l_iter = l_first = e->l;
    do {
      if (!respect_hide || !mesh_elem_flag_test(l_iter->f, MESH_ELEM_HIDDEN)) {
        if (!mesh_elem_flag_test(l_iter->f, hflag)) {
          return false;
        }
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  return true;
}

bool mesh_edge_is_any_face_flag_test(const MeshEdge *e, const char hflag)
{
  if (e->l) {
    MeshLoop *l_iter, *l_first;

    l_iter = l_first = e->l;
    do {
      if (mesh_elem_flag_test(l_iter->f, hflag)) {
        return true;
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  return false;
}

bool mesh_edge_is_any_vert_flag_test(const MeshEdge *e, const char hflag)
{
  return (mesh_elem_flag_test(e->v1, hflag) || mesh_elem_flag_test(e->v2, hflag));
}

bool mesh_face_is_any_vert_flag_test(const MeshFace *f, const char hflag)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    if (mesh_elem_flag_test(l_iter->v, hflag)) {
      return true;
    }
  } while ((l_iter = l_iter->next) != l_first);
  return false;
}

bool mesh_face_is_any_edge_flag_test(const MeshFace *f, const char hflag)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    if (mesh_elem_flag_test(l_iter->e, hflag)) {
      return true;
    }
  } while ((l_iter = l_iter->next) != l_first);
  return false;
}

bool mesh_edge_is_any_face_len_test(const MeshEdge *e, const int len)
{
  if (e->l) {
    MeshLoop *l_iter, *l_first;

    l_iter = l_first = e->l;
    do {
      if (l_iter->f->len == len) {
        return true;
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  return false;
}

bool mesh_face_is_normal_valid(const MeshFace *f)
{
  const float eps = 0.0001f;
  float no[3];

  mesh_face_calc_normal(f, no);
  return len_squared_v3v3(no, f->no) < (eps * eps);
}

/**
 * Use to accumulate volume calculation for faces with consistent winding.
 *
 * Use double precision since this is prone to float precision error, see T73295.
 */
static double mesh_mesh_calc_volume_face(const BMFace *f)
{
  const int tottri = f->len - 2;
  MeshLoop **loops = lib_array_alloca(loops, f->len);
  uint(*index)[3] = lib_array_alloca(index, tottri);
  double vol = 0.0;

  mesh_face_calc_tessellation(f, false, loops, index);

  for (int j = 0; j < tottri; j++) {
    const float *p1 = loops[index[j][0]]->v->co;
    const float *p2 = loops[index[j][1]]->v->co;
    const float *p3 = loops[index[j][2]]->v->co;

    double p1_db[3];
    double p2_db[3];
    double p3_db[3];

    copy_v3db_v3fl(p1_db, p1);
    copy_v3db_v3fl(p2_db, p2);
    copy_v3db_v3fl(p3_db, p3);

    /* co1.dot(co2.cross(co3)) / 6.0 */
    double cross[3];
    cross_v3_v3v3_db(cross, p2_db, p3_db);
    vol += dot_v3v3_db(p1_db, cross);
  }
  return (1.0 / 6.0) * vol;
}
double mesh_calc_volume(Mesh *mesh, bool is_signed)
{
  /* warning, calls own tessellation function, may be slow */
  double vol = 0.0;
  MeshFace *f;
  MeshIter fiter;

  MESH_ELEM_ITER (f, &fiter, mesh, MESH_FACES_OF_MESH) {
    vol += mesh_calc_volume_face(f);
  }

  if (is_signed == false) {
    vol = fabs(vol);
  }

  return vol;
}

int mesh_calc_face_groups(Mesh *mesh,
                          int *r_groups_array,
                          int (**r_group_index)[2],
                          MeshLoopFilterFn filter_fn,
                          MeshLoopPairFilterFn filter_pair_fn,
                          void *user_data,
                          const char hflag_test,
                          const char htype_step)
{
  /* NOTE: almost duplicate of mesh_calc_edge_groups, keep in sync. */

#ifdef DEBUG
  int group_index_len = 1;
#else
  int group_index_len = 32;
#endif

  int(*group_index)[2] = mem_mallocn(sizeof(*group_index) * group_index_len, __func__);

  int *group_array = r_groups_array;
  STACK_DECLARE(group_array);

  int group_curr = 0;

  uint tot_faces = 0;
  uint tot_touch = 0;

  MeshFace **stack;
  STACK_DECLARE(stack);

  MeshIter iter;
  MeshFace *f, *f_next;
  int i;

  STACK_INIT(group_array, mesh->totface);

  lib_assert(((htype_step & ~(MESH_VERT | MESH_EDGE)) == 0) && (htype_step != 0));

  /* init the array */
  MESH_ITER_MESH_INDEX (f, &iter, mesh, MESH_FACES_OF_MESH, i) {
    if ((hflag_test == 0) || mesh_elem_flag_test(f, hflag_test)) {
      tot_faces++;
      mesh_elem_flag_disable(f, MESH_ELEM_TAG);
    }
    else {
      /* never walk over tagged */
      mesh_elem_flag_enable(f, MESH_ELEM_TAG);
    }

    mesh_elem_index_set(f, i); /* set_inline */
  }
  mesh->elem_index_dirty &= ~MESH_FACE;

  /* detect groups */
  stack = mem_mallocn(sizeof(*stack) * tot_faces, __func__);

  f_next = mesh_iter_new(&iter, mesh, MESH_FACES_OF_MESH, NULL);

  while (tot_touch != tot_faces) {
    int *group_item;
    bool ok = false;

    lib_assert(tot_touch < tot_faces);

    STACK_INIT(stack, tot_faces);

    for (; f_next; f_next = mesh_iter_step(&iter)) {
      if (mesh_elem_flag_test(f_next, MESH_ELEM_TAG) == false) {
        mesh_elem_flag_enable(f_next, MEEH_ELEM_TAG);
        STACK_PUSH(stack, f_next);
        ok = true;
        break;
      }
    }

    lib_assert(ok == true);
    UNUSED_VARS_NDEBUG(ok);

    /* manage arrays */
    if (group_index_len == group_curr) {
      group_index_len *= 2;
      group_index = mem_reallocn(group_index, sizeof(*group_index) * group_index_len);
    }

    group_item = group_index[group_curr];
    group_item[0] = STACK_SIZE(group_array);
    group_item[1] = 0;

    while ((f = STACK_POP(stack))) {
      MeshLoop *l_iter, *l_first;

      /* add face */
      STACK_PUSH(group_array, mesh_elem_index_get(f));
      tot_touch++;
      group_item[1]++;
      /* done */

      if (htype_step & MESH_EDGE) {
        /* search for other faces */
        l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
        do {
          MeshLoop *l_radial_iter = l_iter->radial_next;
          if ((l_radial_iter != l_iter) && ((filter_fn == NULL) || filter_fn(l_iter, user_data))) {
            do {
              if ((filter_pair_fn == NULL) || filter_pair_fn(l_iter, l_radial_iter, user_data)) {
                MeshFace *f_other = l_radial_iter->f;
                if (mesh_elem_flag_test(f_other, MESH_ELEM_TAG) == false) {
                  mesh_elem_flag_enable(f_other, MESH_ELEM_TAG);
                  STACK_PUSH(stack, f_other);
                }
              }
            } while ((l_radial_iter = l_radial_iter->radial_next) != l_iter);
          }
        } while ((l_iter = l_iter->next) != l_first);
      }

      if (htype_step & MESH_VERT) {
        MeshIter liter;
        /* search for other faces */
        l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
        do {
          if ((filter_fn == NULL) || filter_fn(l_iter, user_data)) {
            MeshLoop *l_other;
            MESH_ELEM_ITER (l_other, &liter, l_iter, MESH_LOOPS_OF_LOOP) {
              if ((filter_pair_fn == NULL) || filter_pair_fn(l_iter, l_other, user_data)) {
                MeshFace *f_other = l_other->f;
                if (mesh_elem_flag_test(f_other, MESH_ELEM_TAG) == false) {
                  mesh_elem_flag_enable(f_other, MESH_ELEM_TAG);
                  STACK_PUSH(stack, f_other);
                }
              }
            }
          }
        } while ((l_iter = l_iter->next) != l_first);
      }
    }

    group_curr++;
  }

  mem_freen(stack);

  /* reduce alloc to required size */
  if (group_index_len != group_curr) {
    group_index = mem_reallocn(group_index, sizeof(*group_index) * group_curr);
  }
  *r_group_index = group_index;

  return group_curr;
}

int mesh_calc_edge_groups(Mesh *mesh,
                          int *r_groups_array,
                          int (**r_group_index)[2],
                          MeshVertFilterFn filter_fn,
                          void *user_data,
                          const char hflag_test)
{
  /* NOTE: almost duplicate of mesh_calc_face_groups, keep in sync. */

#ifdef DEBUG
  int group_index_len = 1;
#else
  int group_index_len = 32;
#endif

  int(*group_index)[2] = mem_mallocn(sizeof(*group_index) * group_index_len, __func__);

  int *group_array = r_groups_array;
  STACK_DECLARE(group_array);

  int group_curr = 0;

  uint tot_edges = 0;
  uint tot_touch = 0;

  MeshEdge **stack;
  STACK_DECLARE(stack);

  MeshIter iter;
  MeshEdge *e, *e_next;
  int i;
  STACK_INIT(group_array, mesh->totedge);

  /* init the array */
  MESH_ITER_MESH_INDEX (e, &iter, mesh, MESH_EDGES_OF_MESH, i) {
    if ((hflag_test == 0) || mesh_elem_flag_test(e, hflag_test)) {
      tot_edges++;
      mesh_elem_flag_disable(e, MESH_ELEM_TAG);
    }
    else {
      /* never walk over tagged */
      mesh_elem_flag_enable(e, MESH_ELEM_TAG);
    }

    mesh_elem_index_set(e, i); /* set_inline */
  }
  mesh->elem_index_dirty &= ~MESH_EDGE;

  /* detect groups */
  stack = mem_mallocn(sizeof(*stack) * tot_edges, __func__);

  e_next = mesh_iter_new(&iter, mesh, MESH_EDGES_OF_MESH, NULL);

  while (tot_touch != tot_edges) {
    int *group_item;
    bool ok = false;

    lib_assert(tot_touch < tot_edges);

    STACK_INIT(stack, tot_edges);

    for (; e_next; e_next = mesh_iter_step(&iter)) {
      if (mesh_elem_flag_test(e_next, MESH_ELEM_TAG) == false) {
        mesh_elem_flag_enable(e_next, MESH_ELEM_TAG);
        STACK_PUSH(stack, e_next);
        ok = true;
        break;
      }
    }

    lib_assert(ok == true);
    UNUSED_VARS_NDEBUG(ok);

    /* manage arrays */
    if (group_index_len == group_curr) {
      group_index_len *= 2;
      group_index = mem_reallocn(group_index, sizeof(*group_index) * group_index_len);
    }

    group_item = group_index[group_curr];
    group_item[0] = STACK_SIZE(group_array);
    group_item[1] = 0;

    while ((e = STACK_POP(stack))) {
      MeshIter viter;
      MeshIter eiter;
      MeshVert *v;

      /* add edge */
      STACK_PUSH(group_array, mem_elem_index_get(e));
      tot_touch++;
      group_item[1]++;
      /* done */

      /* search for other edges */
      MESH_ELEM_ITER (v, &viter, e, MESH_VERTS_OF_EDGE) {
        if ((filter_fn == NULL) || filter_fn(v, user_data)) {
          MeshEdge *e_other;
          MESH_ELEM_ITER (e_other, &eiter, v, MESH_EDGES_OF_VERT) {
            if (mem_elem_flag_test(e_other, MESH_ELEM_TAG) == false) {
              mem_elem_flag_enable(e_other, MESH_ELEM_TAG);
              STACK_PUSH(stack, e_other);
            }
          }
        }
      }
    }

    group_curr++;
  }

  mem_freen(stack);

  /* reduce alloc to required size */
  if (group_index_len != group_curr) {
    group_index = mem_reallocn(group_index, sizeof(*group_index) * group_curr);
  }
  *r_group_index = group_index;

  return group_curr;
}

int mesh_calc_edge_groups_as_arrays(
    Mesh *mesh, MeshVert **verts, MeshEdge **edges, MeshFace **faces, int (**r_groups)[3])
{
  int(*groups)[3] = mem_mallocn(sizeof(*groups) * mesh->totvert, __func__);
  STACK_DECLARE(groups);
  STACK_INIT(groups, mesh->totvert);

  /* Clear all selected vertices */
  mesh_elem_hflag_disable_all(mesh, MESH_VERT | MESH_EDGE | MESH_FACE, MESH_ELEM_TAG, false);

  MeshVert **stack = mem_mallocn(sizeof(*stack) * mesh->totvert, __func__);
  STACK_DECLARE(stack);
  STACK_INIT(stack, mesh->totvert);

  STACK_DECLARE(verts);
  STACK_INIT(verts, mesh->totvert);

  STACK_DECLARE(edges);
  STACK_INIT(edges, mesh->totedge);

  STACK_DECLARE(faces);
  STACK_INIT(faces, mesh->totface);

  MeshIter iter;
  MeehVert *v_stack_init;
  MESH_ITER_MESH (v_stack_init, &iter, mesh, MESH_VERTS_OF_MESH) {
    if (mesh_elem_flag_test(v_stack_init, MESH_ELEM_TAG)) {
      continue;
    }

    const uint verts_init = STACK_SIZE(verts);
    const uint edges_init = STACK_SIZE(edges);
    const uint faces_init = STACK_SIZE(faces);

    /* Initialize stack. */
    mesh_elem_flag_enable(v_stack_init, MESH_ELEM_TAG);
    STACK_PUSH(verts, v_stack_init);

    if (v_stack_init->e != NULL) {
      MeshVert *v_iter = v_stack_init;
      do {
        MeshEdge *e_iter, *e_first;
        e_iter = e_first = v_iter->e;
        do {
          if (!mesh_elem_flag_test(e_iter, MESH_ELEM_TAG)) {
            mesh_elem_flag_enable(e_iter, MESH_ELEM_TAG);
            STACK_PUSH(edges, e_iter);

            if (e_iter->l != NULL) {
              MeshLoop *l_iter, *l_first;
              l_iter = l_first = e_iter->l;
              do {
                if (!mesh_elem_flag_test(l_iter->f, MESH_ELEM_TAG)) {
                  mesh_elem_flag_enable(l_iter->f, MESH_ELEM_TAG);
                  STACK_PUSH(faces, l_iter->f);
                }
              } while ((l_iter = l_iter->radial_next) != l_first);
            }

            MeshVert *v_other = mesh_edge_other_vert(e_iter, v_iter);
            if (!mesh_elem_flag_test(v_other, MESH_ELEM_TAG)) {
              mesh_elem_flag_enable(v_other, MESH_ELEM_TAG);
              STACK_PUSH(verts, v_other);

              STACK_PUSH(stack, v_other);
            }
          }
        } while ((e_iter = MESH_DISK_EDGE_NEXT(e_iter, v_iter)) != e_first);
      } while ((v_iter = STACK_POP(stack)));
    }

    int *g = STACK_PUSH_RET(groups);
    g[0] = STACK_SIZE(verts) - verts_init;
    g[1] = STACK_SIZE(edges) - edges_init;
    g[2] = STACK_SIZE(faces) - faces_init;
  }

  mem_freen(stack);

  /* Reduce alloc to required size. */
  groups = mem_reallocn(groups, sizeof(*groups) * STACK_SIZE(groups));
  *r_groups = groups;
  return STACK_SIZE(groups);
}

float mesh_subd_falloff_calc(const int falloff, float val)
{
  switch (falloff) {
    case SUBD_FALLOFF_SMOOTH:
      val = 3.0f * val * val - 2.0f * val * val * val;
      break;
    case SUBD_FALLOFF_SPHERE:
      val = sqrtf(2.0f * val - val * val);
      break;
    case SUBD_FALLOFF_ROOT:
      val = sqrtf(val);
      break;
    case SUBD_FALLOFF_SHARP:
      val = val * val;
      break;
    case SUBD_FALLOFF_LIN:
      break;
    case SUBD_FALLOFF_INVSQUARE:
      val = val * (2.0f - val);
      break;
    default:
      lib_assert(0);
      break;
  }

  return val;
}
