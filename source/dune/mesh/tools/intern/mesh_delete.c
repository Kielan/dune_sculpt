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

  MESH_MUTABLE_ITER (e, e_next, &iter, mesh, MESH_EDGES_OF_MESH) {
    if (mredh_op_edge_flag_test(mesh, e, oflag)) {
      mesh_edge_kill(mesh, e);
    }
  }
}

static void mesh_remove_tagged_verts(Mesh *mesh, const short oflag)
{
  MeshVert *v, *v_next;
  MeshIter iter;

  MESH_MUTABLE_ITER (v, v_next, &iter, mesh, MESH_VERTS_OF_MESH) {
    if (mesh_vert_flag_test(mesh, v, oflag)) {
      mesh_vert_kill(mesh, v);
    }
  }
}

static void mesh_remove_tagged_verts_loose(Mesh *mesh, const short oflag)
{
  MeshVert *v, *v_next;
  MeshIter iter;

  MESH_MUTABLE_ITER (v, v_next, &iter, mesh, MESH_VERTS_OF_MESH) {
    if (mesh_vert_flag_test(mesh, v, opflag) && (v->e == NULL)) {
      mesh_vert_kill(mesh, v);
    }
  }
}

void mesh_delete_opflag_tagged(Mesh *mesh, const short oflag, const char htype)
{
  if (htype & MESH_FACE) {
    mesh_remove_tagged_faces(mesh, opflag);
  }
  if (htype & MESH_EDGE) {
    mesh_remove_tagged_edges(mesh, opflag);
  }
  if (htype & MESH_VERT) {
    mesh_remove_tagged_verts(mesh, opflag);
  }
}

void mesh_delete_opflag_ctx(Mesh *mesh, const short oflag, const int type)
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

  MESH_MUTABLE_ITER (f, f_next, &iter, mesh, MESH_FACES_OF_MESH) {
    if (mesh_elem_flag_test(f, hflag)) {
      mesh_face_kill(mesh, f);
    }
  }
}

static void mesh_remove_tagged_edges(Mesh *mesh, const char hflag)
{
  MeshEdge *e, *e_next;
  MeshIter iter;

  MESH_MUTABLE_ITER (e, e_next, &iter, bm, BM_EDGES_OF_MESH) {
    if (mesh_elem_flag_test(e, hflag)) {
      mesh_edge_kill(mesh, e);
    }
  }
}

static void mesh_remove_tagged_verts(BMesh *bm, const char hflag)
{
  MeshVert *v, *v_next;
  MeshIter iter;

  MESH_MUTABLE_ITER (v, v_next, &iter, mesh, MESH_VERTS_OF_MESH) {
    if (mesh_elem_flag_test(v, hflag)) {
      mesh_vert_kill(mesh, v);
    }
  }
}

static void mesh_remove_tagged_verts_loose(Mesh *mesh, const char hflag)
{
  MeshVert *v, *v_next;
  MeshIter iter;

  MESH_MUTABLE_ITER (v, v_next, &iter, mesh, MESH_VERTS_OF_MESH) {
    if (mesh_elem_flag_test(v, hflag) && (v->e == NULL)) {
      mesh_vert_kill(meeh, v);
    }
  }
}

void mesh_delete_hflag_tagged(Mesh *mesh, const char hflag, const char htype)
{
  if (htype & MESH_FACE) {
    mesh_remove_tagged_faces(mesh, hflag);
  }
  if (htype & MESH_EDGE) {
    mesh_remove_tagged_edges(mesh, hflag);
  }
  if (htype & MESH_VERT) {
    mesh_remove_tagged_verts(mesh, hflag);
  }
}

void mesh_delete_hflag_context(Mesh *mesh, const char hflag, const int type)
{
  MeshEdge *e;
  MeshFace *f;

  MeshIter eiter;
  MeshIter fiter;

  switch (type) {
    case DEL_VERTS: {
      mesh_remove_tagged_verts(mesh, hflag);

      break;
    }
    case DEL_EDGES: {
      /* flush down to vert */
      MESH_ITER (e, &eiter, mesh, MESH_EDGES_OF_MESH) {
        if (mesh_elem_flag_test(e, hflag)) {
          mesh_elem_flag_enable(e->v1, hflag);
          mesh_elem_flag_enable(e->v2, hflag);
        }
      }
      mesh_remove_tagged_edges(mesh, hflag);
      mesh_remove_tagged_verts_loose(bm, hflag);

      break;
    }
    case DEL_EDGESFACES: {
      mesh_remove_tagged_edges(mesh, hflag);

      break;
    }
    case DEL_ONLYFACES: {
      mesh_remove_tagged_faces(mesh, hflag);

      break;
    }
    case DEL_ONLYTAGGED: {
      mesh_delete_hflag_tagged(mesh, hflag, MESH_ALL_NOLOOP);

      break;
    }
    case DEL_FACES: {
      /* go through and mark all edges and all verts of all faces for delete */
      MESH_ITER (f, &fiter, mesh, MEEH_FACES_OF_MESH) {
        if (mesh_elem_flag_test(f, hflag)) {
          MeshLoop *l_first = MESH_FACE_FIRST_LOOP(f);
          MeshLoop *l_iter;

          l_iter = l_first;
          do {
            mesh_elem_flag_enable(l_iter->v, hflag);
            mesh_elem_flag_enable(l_iter->e, hflag);
          } while ((l_iter = l_iter->next) != l_first);
        }
      }
      /* now go through and mark all remaining faces all edges for keeping */
      MESH_ITER_MESH (f, &fiter, mesh, MESH_FACES_OF_MESH) {
        if (!mesh_elem_flag_test(f, hflag)) {
          MeshLoop *l_first = MESH_FACE_FIRST_LOOP(f);
          MeshLoop *l_iter;

          l_iter = l_first;
          do {
            mesh_elem_flag_disable(l_iter->v, hflag);
            mesh_elem_flag_disable(l_iter->e, hflag);
          } while ((l_iter = l_iter->next) != l_first);
        }
      }
      /* also mark all the vertices of remaining edges for keeping */
      MESH_ITER (e, &eiter, mesh, MESH_EDGES_OF_MESH) {
        if (!mesh_elem_flag_test(e, hflag)) {
          mesh_elem_flag_disable(e->v1, hflag);
          mesh_elem_flag_disable(e->v2, hflag);
        }
      }
      /* now delete marked face */
      mesh_remove_tagged_faces(mesh, hflag);
      /* delete marked edge */
      mesh_remove_tagged_edges(mesh, hflag);
      /* remove loose vertices */
      mesh_remove_tagged_verts(mesh, hflag);

      break;
    }
  }
}
