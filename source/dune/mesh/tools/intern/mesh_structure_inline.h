/** Mesh inline operator functions. **/

#pragma once

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2)
    LIB_INLINE BMDiskLink *mesh_disk_edge_link_from_vert(const MeshEdge *e, const MeshVert *v)
{
  lib_assert(mesh_vert_in_edge(e, v));
  return (BMDiskLink *)&(&e->v1_disk_link)[v == e->v2];
}

/**
 * Next Disk Edge
 *
 * Find the next edge in a disk cycle
 *
 * return Pointer to the next edge in the disk cycle for the vertex v.
 */
ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1)
    LIB_INLINE MeshEdge *mesh_disk_edge_next_safe(const MeshEdge *e, const MeshVert *v)
{
  if (v == e->v1) {
    return e->v1_disk_link.next;
  }
  if (v == e->v2) {
    return e->v2_disk_link.next;
  }
  return NULL;
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1)
    LIB_INLINE MeshEdge *mesh_disk_edge_prev_safe(const MeshEdge *e, const MeshVert *v)
{
  if (v == e->v1) {
    return e->v1_disk_link.prev;
  }
  if (v == e->v2) {
    return e->v2_disk_link.prev;
  }
  return NULL;
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2) LIB_INLINE MeshEdge *mesh_disk_edge_next(const MeshEdge *e,
                                                                                    const MeshVert *v)
{
  return MESH_DISK_EDGE_NEXT(e, v);
}

ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(1, 2) LIB_INLINE MeshEdge *mesh_disk_edge_prev(const MeshEdge *e,
                                                                                    const MeshVert *v)
{
  return MESH_DISK_EDGE_PREV(e, v);
}
