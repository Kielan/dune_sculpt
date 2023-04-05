#pragma once

/** Returns whether or not a given vertex is is part of a given edge. **/
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) LIB_INLINE
    bool mesh_vert_in_edge(const MeshEdge *e, const MeshVert *v)
{
  return (ELEM(v, e->v1, e->v2));
}

/** Returns whether or not a given edge is part of a given loop. **/
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2) LIB_INLINE
    bool mesh_edge_in_loop(const MeshEdge *e, const MeshLoop *l)
{
  return (l->e == e || l->prev->e == e);
}

/** Returns whether or not two vertices are in a given edge **/
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2, 3) LIB_INLINE
    bool mesh_verts_in_edge(const MeshVert *v1, const MeshVert *v2, const MeshEdge *e)
{
  return ((e->v1 == v1 && e->v2 == v2) || (e->v1 == v2 && e->v2 == v1));
}

/** Given a edge and one of its vertices, returns the other vertex. **/
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2) LIB_INLINE MeshVert *mesh_edge_other_vert(MeshEdge *e,
                                                                                 const MeshVert *v)
{
  if (e->v1 == v) {
    return e->v2;
  }
  else if (e->v2 == v) {
    return e->v1;
  }
  return NULL;
}

/**
 * Tests whether or not the edge is part of a wire.
 * (ie: has no faces attached to it)
 */
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) BLI_INLINE bool BM_edge_is_wire(const BMEdge *e)
{
  return (e->l == NULL);
}

/**
 * Tests whether or not this edge is manifold.
 * A manifold edge has exactly 2 faces attached to it.
 */

#if 1 /* fast path for checking manifold */
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) BLI_INLINE bool BM_edge_is_manifold(const BMEdge *e)
{
  const BMLoop *l = e->l;
  return (l && (l->radial_next != l) &&        /* not 0 or 1 face users */
          (l->radial_next->radial_next == l)); /* 2 face users */
}
#else
BLI_INLINE int BM_edge_is_manifold(BMEdge *e)
{
  return (BM_edge_face_count(e) == 2);
}
#endif

/**
 * Tests that the edge is manifold and
 * that both its faces point the same way.
 */
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) BLI_INLINE bool BM_edge_is_contiguous(const BMEdge *e)
{
  const BMLoop *l = e->l;
  const BMLoop *l_other;
  return (l && ((l_other = l->radial_next) != l) && /* not 0 or 1 face users */
          (l_other->radial_next == l) &&            /* 2 face users */
          (l_other->v != l->v));
}

/**
 * Tests whether or not an edge is on the boundary
 * of a shell (has one face associated with it)
 */

#if 1 /* fast path for checking boundary */
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) BLI_INLINE bool BM_edge_is_boundary(const BMEdge *e)
{
  const BMLoop *l = e->l;
  return (l && (l->radial_next == l));
}
#else
BLI_INLINE int mesh_edge_is_boundary(MeshEdge *e)
{
  return (mesh_edge_face_count(e) == 1);
}
#endif

/**
 * Tests whether one loop is next to another within the same face.
 */
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2) LIB_INLINE
    bool BM_loop_is_adjacent(const MeshLoop *l_a, const MeshLoop *l_b)
{
  LIB_assert(l_a->f == l_b->f);
  LIB_assert(l_a != l_b);
  return (ELEM(l_b, l_a->next, l_a->prev));
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) LIB_INLINE bool mesh_loop_is_manifold(const MeshLoop *l)
{
  return ((l != l->radial_next) && (l == l->radial_next->radial_next));
}

/**
 * Check if we have a single wire edge user.
 */
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1) LIB_INLINE bool mesh_vert_is_wire_endpoint(const MeshVert *v)
{
  const MeshEdge *e = v->e;
  if (e && e->l == NULL) {
    return (MESH_DISK_EDGE_NEXT(e, v) == e);
  }
  return false;
}
