/** Mesh validation function. **/

/* debug builds only */
#ifdef DEBUG

#  include "lib_edgehash.h"
#  include "lib_utildefines.h"

#  include "mesh.h"

#  include "mesh_validate.h"

/* macro which inserts the function name */
#  if defined __GNUC__
#    define ERRMSG(format, args...) \
      { \
        fprintf(stderr, "%s: " format ", " AT "\n", __func__, ##args); \
        errtot++; \
      } \
      (void)0
#  else
#    define ERRMSG(format, ...) \
      { \
        fprintf(stderr, "%s: " format ", " AT "\n", __func__, __VA_ARGS__); \
        errtot++; \
      } \
      (void)0
#  endif

bool mesh_validate(Mesh *mesh)
{
  EdgeHash *edge_hash = lib_edgehash_new_ex(__func__, mesh->totedge);
  int errtot;

  MeshIter iter;
  MeshVert *v;
  MeshEdge *e;
  MeshFace *f;

  int i, j;

  errtot = -1; /* 'ERRMSG' next line will set at zero */
  fprintf(stderr, "\n");
  ERRMSG("This is a debugging function and not intended for general use, running slow test!");

  /* force recalc, even if tagged as valid, since this mesh is suspect! */
  mesh->elem_index_dirty |= MESH_ALL;
  mesh_elem_index_ensure(mesh, MESH_ALL);

  MESH_INDEX_ITER (v, &iter, mesh, MESH_VERTS_OF_MESH, i) {
    if (mesh_elem_flag_test(v, MESH_ELEM_SELECT | MESH_ELEM_HIDDEN) ==
        (MESH_ELEM_SELECT | MESH_ELEM_HIDDEN)) {
      ERRMSG("vert %d: is hidden and selected", i);
    }

    if (v->e) {
      if (!mesh_vert_in_edge(v->e, v)) {
        ERRMSG("vert %d: is not in its referenced edge: %d", i, mesh_elem_index_get(v->e));
      }
    }
  }

  /* check edges */
  MESH_INDEX_ITER (e, &iter, mesh, MESH_EDGES_OF_MESH, i) {
    void **val_p;

    if (e->v1 == e->v2) {
      ERRMSG("edge %d: duplicate index: %d", i, mesh_elem_index_get(e->v1));
    }

    /* build edgehash at the same time */
    if (lib_edgehash_ensure_p(
            edge_hash, mesh_elem_index_get(e->v1), BM_elem_index_get(e->v2), &val_p)) {
      MeshEdge *e_other = *val_p;
      ERRMSG("edge %d, %d: are duplicates", i, BM_elem_index_get(e_other));
    }
    else {
      *val_p = e;
    }
  }

  /* edge radial structure */
  MESH_INDEX_ITER (e, &iter, mesh, MESH_EDGES_OF_MESH, i) {
    if (mesh_elem_flag_test(e, MESH_ELEM_SELECT | BM_ELEM_HIDDEN) ==
        (MESH_ELEM_SELECT | MESH_ELEM_HIDDEN)) {
      ERRMSG("edge %d: is hidden and selected", i);
    }

    if (e->l) {
      MeshLoop *l_iter;
      MeshLoop *l_first;

      j = 0;

      l_iter = l_first = e->l;
      /* we could do more checks here, but save for face checks */
      do {
        if (l_iter->e != e) {
          ERRMSG("edge %d: has invalid loop, loop is of face %d", i, mesh_elem_index_get(l_iter->f));
        }
        else if (mesh_vert_in_edge(e, l_iter->v) == false) {
          ERRMSG("edge %d: has invalid loop with vert not in edge, loop is of face %d",
                 i,
                 mesh_elem_index_get(l_iter->f));
        }
        else if (mesh_vert_in_edge(e, l_iter->next->v) == false) {
          ERRMSG("edge %d: has invalid loop with next vert not in edge, loop is of face %d",
                 i,
                 mesh_elem_index_get(l_iter->f));
        }
      } while ((l_iter = l_iter->radial_next) != l_first);
    }
  }

  /* face structure */
  MESH_ITER_MESH_INDEX (f, &iter, mesh, MESH_FACES_OF_MESH, i) {
    MeshLoop *l_iter;
    MeshLoop *l_first;

    if (mesh_elem_flag_test(f, MESH_ELEM_SELECT | MESH_ELEM_HIDDEN) ==
        (MESH_ELEM_SELECT | MESH_ELEM_HIDDEN)) {
      ERRMSG("face %d: is hidden and selected", i);
    }

    l_iter = l_first = MESH_FACE_FIRST_LOOP(f);

    do {
      mesh_elem_flag_disable(l_iter, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_disable(l_iter->v, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_disable(l_iter->e, MESH_ELEM_INTERNAL_TAG);
    } while ((l_iter = l_iter->next) != l_first);

    j = 0;

    l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
    do {
      if (mesh_elem_flag_test(l_iter, MESH_ELEM_INTERNAL_TAG)) {
        ERRMSG("face %d: has duplicate loop at corner: %d", i, j);
      }
      if (mesh_elem_flag_test(l_iter->v, MESH_ELEM_INTERNAL_TAG)) {
        ERRMSG(
            "face %d: has duplicate vert: %d, at corner: %d", i, BM_elem_index_get(l_iter->v), j);
      }
      if (mesh_elem_flag_test(l_iter->e, MESH_ELEM_INTERNAL_TAG)) {
        ERRMSG(
            "face %d: has duplicate edge: %d, at corner: %d", i, BM_elem_index_get(l_iter->e), j);
      }

      /* adjacent data checks */
      if (l_iter->f != f) {
        ERRMSG("face %d: has loop that points to face: %d at corner: %d",
               i,
               mesh_elem_index_get(l_iter->f),
               j);
      }
      if (l_iter != l_iter->prev->next) {
        ERRMSG("face %d: has invalid 'prev/next' at corner: %d", i, j);
      }
      if (l_iter != l_iter->next->prev) {
        ERRMSG("face %d: has invalid 'next/prev' at corner: %d", i, j);
      }
      if (l_iter != l_iter->radial_prev->radial_next) {
        ERRMSG("face %d: has invalid 'radial_prev/radial_next' at corner: %d", i, j);
      }
      if (l_iter != l_iter->radial_next->radial_prev) {
        ERRMSG("face %d: has invalid 'radial_next/radial_prev' at corner: %d", i, j);
      }

      mesh_elem_flag_enable(l_iter, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_enable(l_iter->v, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_enable(l_iter->e, MESH_ELEM_INTERNAL_TAG);
      j++;
    } while ((l_iter = l_iter->next) != l_first);

    if (j != f->len) {
      ERRMSG("face %d: has length of %d but should be %d", i, f->len, j);
    }

    /* leave elements un-tagged, not essential but nice to avoid unintended dirty tag use later. */
    do {
      mesh_elem_flag_disable(l_iter, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_disable(l_iter->v, MESH_ELEM_INTERNAL_TAG);
      mesh_elem_flag_disable(l_iter->e, MESH_ELEM_INTERNAL_TAG);
    } while ((l_iter = l_iter->next) != l_first);
  }

  lib_edgehash_free(edge_hash, NULL);

  const bool is_valid = (errtot == 0);
  ERRMSG("Finished - errors %d", errtot);
  return is_valid;
}

#endif
