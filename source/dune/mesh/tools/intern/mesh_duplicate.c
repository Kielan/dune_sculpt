/** Duplicate geometry from one mesh from another. */

#include "types_object.h"

#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_math_vector.h"

#include "mesh.h"
#include "intern/mesh_private.h" /* for element checking */

static MeshVert *mesh_vert_copy(Mesh *mesh_src, Mesh *mesh_dst,MeshVert *v_src)
{
  MeshVert *v_dst = mesh_vert_create(mesh_dst, v_src->co, NULL, MESH_CREATE_SKIP_CD);
  mesh_elem_attrs_copy(mesh_src, mesh_dst, v_src, v_dst);
  return v_dst;
}

static MeshEdge *mesh_edge_copy_with_arrays(Mesh *mesh_src,
                                            Mesh *m eh_dst,
                                            MeshEdge *e_src,
                                            MeshVert **verts_dst)
{
  MeshVert *e_dst_v1 = verts_dst[mesh_elem_index_get(e_src->v1)];
  MeshVert *e_dst_v2 = verts_dst[mesh_elem_index_get(e_src->v2)];
  MeshEdge *e_dst = mesh_edge_create(mesh_dst, e_dst_v1, e_dst_v2, NULL, M_CREATE_SKIP_CD);
  mesh_elem_attrs_copy(mesh_src, mesh_dst, e_src, e_dst);
  return e_dst;
}

static MeshFace *mesh_face_copy_with_arrays(
    Mesh *mesh_src, Mesh *mesh_dst, BMFace *f_src, BMVert **verts_dst, BMEdge **edges_dst)
{
  MeshFace *f_dst;
  MeshVert **vtar = lib_array_alloca(vtar, f_src->len);
  MeshEdge **edar = lib_array_alloca(edar, f_src->len);
  MeshLoop *l_iter_src, *l_iter_dst, *l_first_src;
  int i;

  l_first_src = MESH_FACE_FIRST_LOOP(f_src);

  /* Lookup verts & edges. */
  l_iter_src = l_first_src;
  i = 0;
  do {
    vtar[i] = verts_dst[BM_elem_index_get(l_iter_src->v)];
    edar[i] = edges_dst[BM_elem_index_get(l_iter_src->e)];
    i++;
  } while ((l_iter_src = l_iter_src->next) != l_first_src);

  /* Create new face. */
  f_dst = mesh_face_create(bm_dst, vtar, edar, f_src->len, NULL, BM_CREATE_SKIP_CD);

  /* Copy attributes. */
  BM_elem_attrs_copy(bm_src, bm_dst, f_src, f_dst);

  /* Copy per-loop custom data. */
  l_iter_src = l_first_src;
  l_iter_dst = BM_FACE_FIRST_LOOP(f_dst);
  do {
    BM_elem_attrs_copy(bm_src, bm_dst, l_iter_src, l_iter_dst);
  } while ((void)(l_iter_dst = l_iter_dst->next), (l_iter_src = l_iter_src->next) != l_first_src);

  return f_dst;
}

void mesh_mesh_copy_arrays(Mesh *bm_src,
                           Mesh *bm_dst,
                           MeshVert **verts_src,
                           uint verts_src_len,
                           MeshEdge **edges_src,
                           uint edges_src_len,
                           MeshFace **faces_src,
                           uint faces_src_len)
{
  /* Vertices. */
  MeshVert **verts_dst = mem_mallocn(sizeof(*verts_dst) * verts_src_len, __func__);
  for (uint i = 0; i < verts_src_len; i++) {
    MeshVert *v_src = verts_src[i];
    mesh_elem_index_set(v_src, i); /* set_dirty! */

    MeshVert *v_dst = mesh_vert_copy(mesh_src, bm_dst, v_src);
    mesh_elem_index_set(v_dst, i); /* set_ok */
    verts_dst[i] = v_dst;
  }
  mesh_src->elem_index_dirty |= MESH_VERT;
  mesh_dst->elem_index_dirty &= ~MESH_VERT;

  /* Edges. */
  MeshEdge **edges_dst = mem_mallocn(sizeof(*edges_dst) * edges_src_len, __func__);
  for (uint i = 0; i < edges_src_len; i++) {
    MeshEdge *e_src = edges_src[i];
    mesh_elem_index_set(e_src, i); /* set_dirty! */

    BMEdge *e_dst = bm_edge_copy_with_arrays(bm_src, bm_dst, e_src, verts_dst);
    BM_elem_index_set(e_dst, i);
    edges_dst[i] = e_dst;
  }
  bm_src->elem_index_dirty |= BM_EDGE;
  bm_dst->elem_index_dirty &= ~BM_EDGE;

  /* Faces. */
  for (uint i = 0; i < faces_src_len; i++) {
    BMFace *f_src = faces_src[i];
    BMFace *f_dst = bm_face_copy_with_arrays(bm_src, bm_dst, f_src, verts_dst, edges_dst);
    BM_elem_index_set(f_dst, i);
  }
  bm_dst->elem_index_dirty &= ~BM_FACE;

  /* Cleanup. */
  MEM_freeN(verts_dst);
  MEM_freeN(edges_dst);
}
