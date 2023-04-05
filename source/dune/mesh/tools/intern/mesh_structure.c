/** Low level routines for manipulating the Mesh structure. */

#include "lib_utildefines.h"

#include "mesh.h"
#include "intern/mesh_private.h"

/** MISC utility functions. */

void mesh_disk_vert_swap(MeshEdge *e, MeshVert *v_dst, MeshVert *v_src)
{
  if (e->v1 == v_src) {
    e->v1 = v_dst;
    e->v1_disk_link.next = e->v1_disk_link.prev = NULL;
  }
  else if (e->v2 == v_src) {
    e->v2 = v_dst;
    e->v2_disk_link.next = e->v2_disk_link.prev = NULL;
  }
  else {
    lib_assert(0);
  }
}

void mesh_edge_vert_swap(MeshEdge *e, MeshVert *v_dst, MeshVert *v_src)
{
  /* swap out loops */
  if (e->l) {
    BMLoop *l_iter, *l_first;
    l_iter = l_first = e->l;
    do {
      if (l_iter->v == v_src) {
        l_iter->v = v_dst;
      }
      else if (l_iter->next->v == v_src) {
        l_iter->next->v = v_dst;
      }
      else {
        lib_assert(l_iter->prev->v != v_src);
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  /* swap out edges */
  mesh_disk_vert_replace(e, v_dst, v_src);
}

void mesh_disk_vert_replace(MeshEdge *e, MeshVert *v_dst, MeshVert *v_src)
{
  lib_assert(e->v1 == v_src || e->v2 == v_src);
  mesh_disk_edge_remove(e, v_src);      /* remove e from tv's disk cycle */
  mesh_disk_vert_swap(e, v_dst, v_src); /* swap out tv for v_new in e */
  mesh_disk_edge_append(e, v_dst);      /* add e to v_dst's disk cycle */
  lib_assert(e->v1 != e->v2);
}

/**
 * section mesh_cycles Mesh Cycles
 *
 * NOTE: this is somewhat outdated, though bits of its API are still used.
 *
 * Cycles are circular doubly linked lists that form the basis of adjacency
 * information in the Mesh modeler. Full adjacency relations can be derived
 * from examining these cycles very quickly. Although each cycle is a double
 * circular linked list, each one is considered to have a 'base' or 'head',
 * and care must be taken by Euler code when modifying the contents of a cycle.
 *
 * The contents of this file are split into two parts. First there are the
 * mesh_cycle family of functions which are generic circular double linked list
 * procedures. The second part contains higher level procedures for supporting
 * modification of specific cycle types.
 *
 * The three cycles explicitly stored in the Mesh data structure are as follows:
 * 1: The Disk Cycle - A circle of edges around a vertex
 * Base: vertex->edge pointer.
 *
 * This cycle is the most complicated in terms of its structure. Each MeshEdge contains
 * two MeshCycleNode structures to keep track of that edges membership in the disk cycle
 * of each of its vertices. However for any given vertex it may be the first in some edges
 * in its disk cycle and the second for others. The mesh_disk_XXX family of functions contain
 * some nice utilities for navigating disk cycles in a way that hides this detail from the
 * tool writer.
 *
 * Note that the disk cycle is completely independent from face data. One advantage of this
 * is that wire edges are fully integrated into the topology database. Another is that the
 * the disk cycle has no problems dealing with non-manifold conditions involving faces.
 *
 * Functions relating to this cycle:
 * - mesh_disk_vert_replace
 * - mesh_disk_edge_append
 * - mesh_disk_edge_remove
 * - mesh_disk_edge_next
 * - mesh_disk_edge_prev
 * - mesh_disk_facevert_count
 * - mesh_disk_faceedge_find_first
 * - mesh_disk_faceedge_find_next
 * 2: The Radial Cycle - A circle of face edges (mesh_Loop) around an edge
 * Base: edge->l->radial structure.
 *
 * The radial cycle is similar to the radial cycle in the radial edge data structure.*
 * Unlike the radial edge however, the radial cycle does not require a large amount of memory
 * to store non-manifold conditions since Mesh does not keep track of region/shell information.
 *
 * Functions relating to this cycle:
 * - mesh_radial_loop_append
 * - mesh_radial_loop_remove
 * - mesh_radial_facevert_count
 * - mesh_radial_facevert_check
 * - mesh_radial_faceloop_find_first
 * - mesh_radial_faceloop_find_next
 * - mesh_radial_validate
 * 3: The Loop Cycle - A circle of face edges around a polygon.
 * Base: polygon->lbase.
 *
 * The loop cycle keeps track of a faces vertices and edges. It should be noted that the
 * direction of a loop cycle is either CW or CCW depending on the face normal, and is
 * not oriented to the faces edit-edges.
 *
 * Functions relating to this cycle:
 * - mesh_cycle_XXX family of functions.
 * note the order of elements in all cycles except the loop cycle is undefined. This
 * leads to slightly increased seek time for deriving some adjacency relations, however the
 * advantage is that no intrinsic properties of the data structures are dependent upon the
 * cycle order and all non-manifold conditions are represented trivially.
 */

void mesh_disk_edge_append(MeshEdge *e, MeshVert *v)
{
  if (!v->e) {
    MeshDiskLink *dl1 = mesh_disk_edge_link_from_vert(e, v);

    v->e = e;
    dl1->next = dl1->prev = e;
  }
  else {
    MeshDiskLink *dl1, *dl2, *dl3;

    dl1 = mesh_disk_edge_link_from_vert(e, v);
    dl2 = mesh_disk_edge_link_from_vert(v->e, v);
    dl3 = dl2->prev ? mesh_disk_edge_link_from_vert(dl2->prev, v) : NULL;

    dl1->next = v->e;
    dl1->prev = dl2->prev;

    dl2->prev = e;
    if (dl3) {
      dl3->next = e;
    }
  }
}

void mesh_disk_edge_remove(MeshEdge *e, MeshVert *v)
{
  MeshDiskLink *dl1, *dl2;

  dl1 = mesh_disk_edge_link_from_vert(e, v);
  if (dl1->prev) {
    dl2 = mesh_disk_edge_link_from_vert(dl1->prev, v);
    dl2->next = dl1->next;
  }

  if (dl1->next) {
    dl2 = mesh_disk_edge_link_from_vert(dl1->next, v);
    dl2->prev = dl1->prev;
  }

  if (v->e == e) {
    v->e = (e != dl1->next) ? dl1->next : NULL;
  }

  dl1->next = dl1->prev = NULL;
}

MeshEdge *mesh_disk_edge_exists(const MeshVert *v1, const MeshVert *v2)
{
  MeshEdge *e_iter, *e_first;

  if (v1->e) {
    e_first = e_iter = v1->e;

    do {
      if (mesh_verts_in_edge(v1, v2, e_iter)) {
        return e_iter;
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v1)) != e_first);
  }

  return NULL;
}

int mesh_disk_count(const MeshVert *v)
{
  int count = 0;
  if (v->e) {
    MeshEdge *e_first, *e_iter;
    e_iter = e_first = v->e;
    do {
      count++;
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);
  }
  return count;
}

int mesh_disk_count_at_most(const MeshVert *v, const int count_max)
{
  int count = 0;
  if (v->e) {
    MeshEdge *e_first, *e_iter;
    e_iter = e_first = v->e;
    do {
      count++;
      if (count == count_max) {
        break;
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);
  }
  return count;
}

bool mesh_disk_validate(int len, MeshEdge *e, MeshVert *v)
{
  MeshEdge *e_iter;

  if (!mesh_vert_in_edge(e, v)) {
    return false;
  }
  if (len == 0 || mesh_disk_count_at_most(v, len + 1) != len) {
    return false;
  }

  e_iter = e;
  do {
    if (len != 1 && mesh_disk_edge_prev(e_iter, v) == e_iter) {
      return false;
    }
  } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e);

  return true;
}

int mesh_disk_facevert_count(const MeshVert *v)
{
  /* is there an edge on this vert at all */
  int count = 0;
  if (v->e) {
    MeshEdge *e_first, *e_iter;

    /* first, loop around edge */
    e_first = e_iter = v->e;
    do {
      if (e_iter->l) {
        count += mesh_radial_facevert_count(e_iter->l, v);
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);
  }
  return count;
}

int mesh_disk_facevert_count_at_most(const MeshVert *v, const int count_max)
{
  /* is there an edge on this vert at all */
  int count = 0;
  if (v->e) {
    MeshEdge *e_first, *e_iter;

    /* first, loop around edge */
    e_first = e_iter = v->e;
    do {
      if (e_iter->l) {
        count += mesh_radial_facevert_count_at_most(e_iter->l, v, count_max - count);
        if (count == count_max) {
          break;
        }
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);
  }
  return count;
}

MeshEdge *mesh_disk_faceedge_find_first(const MeshEdge *e, const MeshVert *v)
{
  const MeshEdge *e_iter = e;
  do {
    if (e_iter->l != NULL) {
      return (MeshEdge *)((e_iter->l->v == v) ? e_iter : e_iter->l->next->e);
    }
  } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e);
  return NULL;
}

MeshLoop *mesh_disk_faceloop_find_first(const MeshEdge *e, const MeshVert *v)
{
  const MeshEdge *e_iter = e;
  do {
    if (e_iter->l != NULL) {
      return (e_iter->l->v == v) ? e_iter->l : e_iter->l->next;
    }
  } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e);
  return NULL;
}

MeshLoop *mesh_disk_faceloop_find_first_visible(const MeshEdge *e, const MeshVert *v)
{
  const MeshEdge *e_iter = e;
  do {
    if (!mesh_elem_flag_test(e_iter, MESH_ELEM_HIDDEN)) {
      if (e_iter->l != NULL) {
        MeshLoop *l_iter, *l_first;
        l_iter = l_first = e_iter->l;
        do {
          if (!mesh_elem_flag_test(l_iter->f, MESH_ELEM_HIDDEN)) {
            return (l_iter->v == v) ? l_iter : l_iter->next;
          }
        } while ((l_iter = l_iter->radial_next) != l_first);
      }
    }
  } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e);
  return NULL;
}

MeshEdge *mesh_disk_faceedge_find_next(const MeshEdge *e, const MeshVert *v)
{
  MeshEdge *e_find;
  e_find = mesh_disk_edge_next(e, v);
  do {
    if (e_find->l && mesh_radial_facevert_check(e_find->l, v)) {
      return e_find;
    }
  } while ((e_find = mesh_disk_edge_next(e_find, v)) != e);
  return (MeshEdge *)e;
}

bool mesh_radial_validate(int radlen, MeshLoop *l)
{
  MeshLoop *l_iter = l;
  int i = 0;

  if (mesh_radial_length(l) != radlen) {
    return false;
  }

  do {
    if (UNLIKELY(!l_iter)) {
      MESH_ASSERT(0);
      return false;
    }

    if (l_iter->e != l->e) {
      return false;
    }
    if (!ELEM(l_iter->v, l->e->v1, l->e->v2)) {
      return false;
    }

    if (UNLIKELY(i > MESH_LOOP_RADIAL_MAX)) {
      MESH_ASSERT(0);
      return false;
    }

    i++;
  } while ((l_iter = l_iter->radial_next) != l);

  return true;
}

void mesh_radial_loop_append(MeshEdge *e, MeshLoop *l)
{
  if (e->l == NULL) {
    e->l = l;
    l->radial_next = l->radial_prev = l;
  }
  else {
    l->radial_prev = e->l;
    l->radial_next = e->l->radial_next;

    e->l->radial_next->radial_prev = l;
    e->l->radial_next = l;

    e->l = l;
  }

  if (UNLIKELY(l->e && l->e != e)) {
    /* l is already in a radial cycle for a different edge */
    BMESH_ASSERT(0);
  }

  l->e = e;
}

void bmesh_radial_loop_remove(MeshEdge *e, MeshLoop *l)
{
  /* if e is non-NULL, l must be in the radial cycle of e */
  if (UNLIKELY(e != l->e)) {
    MESH_ASSERT(0);
  }

  if (l->radial_next != l) {
    if (l == e->l) {
      e->l = l->radial_next;
    }

    l->radial_next->radial_prev = l->radial_prev;
    l->radial_prev->radial_next = l->radial_next;
  }
  else {
    if (l == e->l) {
      e->l = NULL;
    }
    else {
      MESH_ASSERT(0);
    }
  }

  /* l is no longer in a radial cycle; empty the links
   * to the cycle and the link back to an edge */
  l->radial_next = l->radial_prev = NULL;
  l->e = NULL;
}

void mesh_radial_loop_unlink(MeshLoop *l)
{
  if (l->radial_next != l) {
    l->radial_next->radial_prev = l->radial_prev;
    l->radial_prev->radial_next = l->radial_next;
  }

  /* l is no longer in a radial cycle; empty the links
   * to the cycle and the link back to an edge */
  l->radial_next = l->radial_prev = NULL;
  l->e = NULL;
}

MeshLoop *mesh_radial_faceloop_find_first(const MeshLoop *l, const MeshVert *v)
{
  const MeshLoop *l_iter;
  l_iter = l;
  do {
    if (l_iter->v == v) {
      return (MeshLoop *)l_iter;
    }
  } while ((l_iter = l_iter->radial_next) != l);
  return NULL;
}

MeshLoop *mesh_radial_faceloop_find_next(const MeshLoop *l, const MeshVert *v)
{
  MeshLoop *l_iter;
  l_iter = l->radial_next;
  do {
    if (l_iter->v == v) {
      return l_iter;
    }
  } while ((l_iter = l_iter->radial_next) != l);
  return (MeshLoop *)l;
}

int mesh_radial_length(const MeshLoop *l)
{
  const MeshLoop *l_iter = l;
  int i = 0;

  if (!l) {
    return 0;
  }

  do {
    if (UNLIKELY(!l_iter)) {
      /* Radial cycle is broken (not a circular loop). */
      MESH_ASSERT(0);
      return 0;
    }

    i++;
    if (UNLIKELY(i >= MESH_LOOP_RADIAL_MAX)) {
      MESH_ASSERT(0);
      return -1;
    }
  } while ((l_iter = l_iter->radial_next) != l);

  return i;
}

int mesh_radial_facevert_count(const MeshLoop *l, const MeshVert *v)
{
  const MeshLoop *l_iter;
  int count = 0;
  l_iter = l;
  do {
    if (l_iter->v == v) {
      count++;
    }
  } while ((l_iter = l_iter->radial_next) != l);

  return count;
}

int mesh_radial_facevert_count_at_most(const MeshLoop *l, const MeshVert *v, const int count_max)
{
  const MeshLoop *l_iter;
  int count = 0;
  l_iter = l;
  do {
    if (l_iter->v == v) {
      count++;
      if (count == count_max) {
        break;
      }
    }
  } while ((l_iter = l_iter->radial_next) != l);

  return count;
}

bool mesh_radial_facevert_check(const MeshLoop *l, const MeshVert *v)
{
  const MeshLoop *l_iter;
  l_iter = l;
  do {
    if (l_iter->v == v) {
      return true;
    }
  } while ((l_iter = l_iter->radial_next) != l);

  return false;
}

bool mesh_loop_validate(MeshFace *f)
{
  int i;
  int len = f->len;
  MeshLoop *l_iter, *l_first;

  l_first = MESH_FACE_FIRST_LOOP(f);

  if (l_first == NULL) {
    return false;
  }

  /* Validate that the face loop cycle is the length specified by f->len */
  for (i = 1, l_iter = l_first->next; i < len; i++, l_iter = l_iter->next) {
    if ((l_iter->f != f) || (l_iter == l_first)) {
      return false;
    }
  }
  if (l_iter != l_first) {
    return false;
  }

  /* Validate the loop->prev links also form a cycle of length f->len */
  for (i = 1, l_iter = l_first->prev; i < len; i++, l_iter = l_iter->prev) {
    if (l_iter == l_first) {
      return false;
    }
  }
  if (l_iter != l_first) {
    return false;
  }

  return true;
}
