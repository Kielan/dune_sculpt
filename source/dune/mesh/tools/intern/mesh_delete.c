#include "lib_utildefines.h"

#include "mesh.h"
#include "intern/mesh_private.h"

/* mesh functions */

/* -------------------------------------------------------------------- */
/** Mesh Operator Delete Functions **/

/**
 * Called by operators to remove elements that they have marked for
 * removal.
 */
static void mesh_remove_tagged_faces(Mesh *mesh, const short oflag)
{
  MeshFace *f, *f_next;
  MeshIter iter;

  MESH_ITER_MUTABLE (f, f_next, &iter, mesh, MESH_FACES_OF_MESH) {
    if (mesh_face_flag_test(mesh, f, oflag)) {
      mesh_face_kill(mesh, f);
    }
  }
}

static void mesh_remove_tagged_edges(Mesh *mesh, const short oflag)
{
  MeshEdge *e, *e_next;
  MeshIter iter;

  MESH_ITER_MUTABLE (e, e_next, &iter, mesh, MESH_EDGES_OF_MESH) {
    if (BMO_edge_flag_test(bm, e, oflag)) {
      BM_edge_kill(bm, e);
    }
  }
}

static void mesh_remove_tagged_verts(Mesh *mesh, const short oflag)
{
  MeshVert *v, *v_next;
  MeshIter iter;

  MESH_ITER_MUTABLE (v, v_next, &iter, mesh, MESH_VERTS_OF_MESH) {
    if (mesh_vert_flag_test(mesh, v, oflag)) {
      mesh_vert_kill(mesh, v);
    }
  }
}

static void mesh_remove_tagged_verts_loose(Mesh *mesh, const short oflag)
{
  MeshVert *v, *v_next;
  MeshIter iter;

  MESH_ITER_MUTABLE (v, v_next, &iter, mesh, MESH_VERTS_OF_MESH) {
    if (mesh_vert_flag_test(mesh, v, oflag) && (v->e == NULL)) {
      mesh_vert_kill(mesh, v);
    }
  }
}

void mesh_delete_oflag_tagged(Mesh *mesh, const short oflag, const char htype)
{
  if (htype & MESH_FACE) {
    mesh_remove_tagged_faces(mesh, oflag);
  }
  if (htype & MESH_EDGE) {
    mesh_remove_tagged_edges(mesh, oflag);
  }
  if (htype & MESH_VERT) {
    mesh_remove_tagged_verts(mesh, oflag);
  }
}

void mesh_delete_oflag_context(Mesh *mesh, const short oflag, const int type)
{
  MeshEdge *e;
  MeshFace *f;

  MeshIter eiter;
  MeshIter fiter;

  switch (type) {
    case DEL_VERTS: {
      mesh_remove_tagged_verts(mesh, oflag);

      break;
    }
    case DEL_EDGES: {
      /* flush down to vert */
      MESH_ITER (e, &eiter, mesh, MESH_EDGES_OF_MESH) {
        if (mesh_edge_flag_test(mesh, e, oflag)) {
          mesh_vert_flag_enable(mesh, e->v1, oflag);
          mesh_vert_flag_enable(mesh, e->v2, oflag);
        }
      }
      mesh_remove_tagged_edges(mesh, oflag);
      mesh_remove_tagged_verts_loose(mesh, oflag);

      break;
    }
    case DEL_EDGESFACES: {
      mesh_remove_tagged_edges(mesh, oflag);

      break;
    }
    case DEL_ONLYFACES: {
      mesh_remove_tagged_faces(mesh, oflag);

      break;
    }
    case DEL_ONLYTAGGED: {
      mesh_delete_oflag_tagged(mesh, oflag, MESH_ALL_NOLOOP);

      break;
    }
    case DEL_FACES:
    case DEL_FACES_KEEP_BOUNDARY: {
      /* go through and mark all edges and all verts of all faces for delete */
      MESH_ITER (f, &fiter, mesh, MESH_FACES_OF_MESH) {
        if (mesh_face_flag_test(mesh, f, oflag)) {
          MeshLoop *l_first = MESH_FACE_FIRST_LOOP(f);
          MeshLoop *l_iter;

          l_iter = l_first;
          do {
            mesh_vert_flag_enable(mesh, l_iter->v, oflag);
            mesh_edge_flag_enable(mesh, l_iter->e, oflag);
          } while ((l_iter = l_iter->next) != l_first);
        }
      }
      /* now go through and mark all remaining faces all edges for keeping */
      MESH_ITER (f, &fiter, mesh, MESH_FACES_OF_MESH) {
        if (!mesh_face_flag_test(mesh, f, oflag)) {
          MeshLoop *l_first = MESH_FACE_FIRST_LOOP(f);
          MeshLoop *l_iter;

          l_iter = l_first;
          do {
            mesh_vert_flag_disable(mesh, l_iter->v, oflag);
            mesh_edge_flag_disable(mesh, l_iter->e, oflag);
          } while ((l_iter = l_iter->next) != l_first);
        }
      }
      /* also mark all the vertices of remaining edges for keeping */
      MESH_ITER (e, &eiter, mesh, MESH_EDGES_OF_MESH) {

        /* Only exception to normal 'DEL_FACES' logic. */
        if (type == DEL_FACES_KEEP_BOUNDARY) {
          if (mesh_edge_is_boundary(e)) {
            mesh_edge_flag_disable(mesh, e, oflag);
          }
        }

        if (!mesh_edge_flag_test(mesh, e, oflag)) {
          mesh_vert_flag_disable(mesh, e->v1, oflag);
          mesh_vert_flag_disable(mesh, e->v2, oflag);
        }
      }

      /* now delete marked face */
      mesh_op_remove_tagged_faces(bm, oflag);
      /* delete marked edge */
      mesh_op_remove_tagged_edges(bm, oflag);
      /* remove loose vertices */
      mesh_op_remove_tagged_verts(bm, oflag);

      break;
    }
  }
}

/* Mesh functions
 *
 * NOTE: this is just a duplicate of the code above (bad!)
 * but for now keep in sync, its less hassle than having to create bmesh operator flags,
 * each time we need to remove some geometry.
 */

/* -------------------------------------------------------------------- */
/** Mesh Delete Functions (no oflags) **/

static void mesh_remove_tagged_faces(Mesh *mesh, const char hflag)
{
  MeshFace *f, *f_next;
  MeshIter iter;

  MESH_ITER_MUTABLE (f, f_next, &iter, bm, BM_FACES_OF_MESH) {
    if (mesh_elem_flag_test(f, hflag)) {
      mesh_face_kill(mesh, f);
    }
  }
}

static void bm_remove_tagged_edges(BMesh *bm, const char hflag)
{
  BMEdge *e, *e_next;
  BMIter iter;

  BM_ITER_MESH_MUTABLE (e, e_next, &iter, bm, BM_EDGES_OF_MESH) {
    if (BM_elem_flag_test(e, hflag)) {
      BM_edge_kill(bm, e);
    }
  }
}

static void bm_remove_tagged_verts(BMesh *bm, const char hflag)
{
  BMVert *v, *v_next;
  BMIter iter;

  BM_ITER_MESH_MUTABLE (v, v_next, &iter, bm, BM_VERTS_OF_MESH) {
    if (BM_elem_flag_test(v, hflag)) {
      BM_vert_kill(bm, v);
    }
  }
}

static void bm_remove_tagged_verts_loose(BMesh *bm, const char hflag)
{
  BMVert *v, *v_next;
  BMIter iter;

  BM_ITER_MESH_MUTABLE (v, v_next, &iter, bm, BM_VERTS_OF_MESH) {
    if (BM_elem_flag_test(v, hflag) && (v->e == NULL)) {
      BM_vert_kill(bm, v);
    }
  }
}

void BM_mesh_delete_hflag_tagged(BMesh *bm, const char hflag, const char htype)
{
  if (htype & BM_FACE) {
    bm_remove_tagged_faces(bm, hflag);
  }
  if (htype & BM_EDGE) {
    bm_remove_tagged_edges(bm, hflag);
  }
  if (htype & BM_VERT) {
    bm_remove_tagged_verts(bm, hflag);
  }
}

void BM_mesh_delete_hflag_context(BMesh *bm, const char hflag, const int type)
{
  BMEdge *e;
  BMFace *f;

  BMIter eiter;
  BMIter fiter;

  switch (type) {
    case DEL_VERTS: {
      bm_remove_tagged_verts(bm, hflag);

      break;
    }
    case DEL_EDGES: {
      /* flush down to vert */
      BM_ITER_MESH (e, &eiter, bm, BM_EDGES_OF_MESH) {
        if (BM_elem_flag_test(e, hflag)) {
          BM_elem_flag_enable(e->v1, hflag);
          BM_elem_flag_enable(e->v2, hflag);
        }
      }
      bm_remove_tagged_edges(bm, hflag);
      bm_remove_tagged_verts_loose(bm, hflag);

      break;
    }
    case DEL_EDGESFACES: {
      bm_remove_tagged_edges(bm, hflag);

      break;
    }
    case DEL_ONLYFACES: {
      bm_remove_tagged_faces(bm, hflag);

      break;
    }
    case DEL_ONLYTAGGED: {
      BM_mesh_delete_hflag_tagged(bm, hflag, BM_ALL_NOLOOP);

      break;
    }
    case DEL_FACES: {
      /* go through and mark all edges and all verts of all faces for delete */
      BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
        if (BM_elem_flag_test(f, hflag)) {
          BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
          BMLoop *l_iter;

          l_iter = l_first;
          do {
            BM_elem_flag_enable(l_iter->v, hflag);
            BM_elem_flag_enable(l_iter->e, hflag);
          } while ((l_iter = l_iter->next) != l_first);
        }
      }
      /* now go through and mark all remaining faces all edges for keeping */
      BM_ITER_MESH (f, &fiter, bm, BM_FACES_OF_MESH) {
        if (!BM_elem_flag_test(f, hflag)) {
          BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
          BMLoop *l_iter;

          l_iter = l_first;
          do {
            BM_elem_flag_disable(l_iter->v, hflag);
            BM_elem_flag_disable(l_iter->e, hflag);
          } while ((l_iter = l_iter->next) != l_first);
        }
      }
      /* also mark all the vertices of remaining edges for keeping */
      BM_ITER_MESH (e, &eiter, bm, BM_EDGES_OF_MESH) {
        if (!BM_elem_flag_test(e, hflag)) {
          BM_elem_flag_disable(e->v1, hflag);
          BM_elem_flag_disable(e->v2, hflag);
        }
      }
      /* now delete marked face */
      bm_remove_tagged_faces(bm, hflag);
      /* delete marked edge */
      bm_remove_tagged_edges(bm, hflag);
      /* remove loose vertices */
      bm_remove_tagged_verts(bm, hflag);

      break;
    }
  }
}
