/** Core Mesh functions for adding, removing BMesh elements. **/

#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_array.h"
#include "lib_linklist_stack.h"
#include "lib_math_vector.h"
#include "lib_utildefines_stack.h"

#include "i18n.h"

#include "types_meshdata.h"

#include "dune_customdata.h"
#include "dune_mesh.h"

#include "mesh.h"
#include "intern/mesh_private.h"

/* use so valgrinds memcheck alerts us when undefined index is used.
 * TESTING ONLY! */
// #define USE_DEBUG_INDEX_MEMCHECK

#ifdef USE_DEBUG_INDEX_MEMCHECK
#  define DEBUG_MEMCHECK_INDEX_INVALIDATE(ele) \
    { \
      int undef_idx; \
      mesh_elem_index_set(ele, undef_idx); /* set_ok_invalid */ \
    } \
    (void)0

#endif

MeshVert *mesh_vert_create(Mesh *mesh,
                       const float co[3],
                       const MeshVert *v_example,
                       const eMeshCreateFlag create_flag)
{
  MVert *v = lib_mempool_alloc(bm->vpool);

  lib_assert((v_example == NULL) || (v_example->head.htype == BM_VERT));
  lib_assert(!(create_flag & 1));

  /* --- assign all members --- */
  v->head.data = NULL;

#ifdef USE_DEBUG_INDEX_MEMCHECK
  DEBUG_MEMCHECK_INDEX_INVALIDATE(v);
#else
  mesh_elem_index_set(v, -1); /* set_ok_invalid */
#endif

  v->head.htype = MESH_VERT;
  v->head.hflag = 0;
  v->head.api_flag = 0;

  /* allocate flags */
  if (mesh->use_toolflags) {
    ((MeshVert_OFlag *)v)->oflags = mesh->vtoolflagpool ? BLI_mempool_calloc(bm->vtoolflagpool) : NULL;
  }

  /* 'v->no' is handled by mesh_elem_attrs_copy */
  if (co) {
    copy_v3_v3(v->co, co);
  }
  else {
    zero_v3(v->co);
  }
  /* 'v->no' set below */

  v->e = NULL;
  /* --- done --- */

  /* disallow this flag for verts - its meaningless */
  lib_assert((create_flag & MESH_CREATE_NO_DOUBLE) == 0);

  /* may add to middle of the pool */
  mesh->elem_index_dirty |= MESH_VERT;
  mesh->elem_table_dirty |= MESH_VERT;
  mesh->spacearr_dirty |= MESH_SPACEARR_DIRTY_ALL;

  mesh->totvert++;

  if (!(create_flag & MESH_CREATE_SKIP_CD)) {
    if (v_example) {
      int *keyi;

      /* handles 'v->no' too */
      mesh_elem_attrs_copy(mesh, mesh, v_example, v);

      /* exception: don't copy the original shapekey index */
      keyi = CustomData_mesh_get(&mesh->vdata, v->head.data, CD_SHAPE_KEYINDEX);
      if (keyi) {
        *keyi = ORIGINDEX_NONE;
      }
    }
    else {
      CustomData_mesh_set_default(&mesh->vdata, &v->head.data);
      zero_v3(v->no);
    }
  }
  else {
    if (v_example) {
      copy_v3_v3(v->no, v_example->no);
    }
    else {
      zero_v3(v->no);
    }
  }

  MESH_CHECK_ELEMENT(v);

  return v;
}

MeshEdge *mesh_edge_create(
    Meshesh *mesh, MeshVert *v1, MeshVert *v2, const MeshEdge *e_example, const eMeshCreateFlag create_flag)
{
  MeshEdge *e;

  lib_assert(v1 != v2);
  lib_assert(v1->head.htype == MESH_VERT && v2->head.htype == MESH_VERT);
  lib_assert((e_example == NULL) || (e_example->head.htype == MESH_EDGE));
  lib_assert(!(create_flag & 1));

  if ((create_flag & MESH_CREATE_NO_DOUBLE) && (e = mesh_edge_exists(v1, v2))) {
    return e;
  }

  e = lib_mempool_alloc(mesh->epool);

  /* --- assign all members --- */
  e->head.data = NULL;

#ifdef USE_DEBUG_INDEX_MEMCHECK
  DEBUG_MEMCHECK_INDEX_INVALIDATE(e);
#else
  mesh_elem_index_set(e, -1); /* set_ok_invalid */
#endif

  e->head.htype = MESH_EDGE;
  e->head.hflag = MESH_ELEM_SMOOTH | MESH_ELEM_DRAW;
  e->head.api_flag = 0;

  /* allocate flags */
  if (mesh->use_toolflags) {
    ((MeshEdge_OFlag *)e)->oflags = mesh->etoolflagpool ? lib_mempool_calloc(mesh->etoolflagpool) : NULL;
  }

  e->v1 = v1;
  e->v2 = v2;
  e->l = NULL;

  memset(&e->v1_disk_link, 0, sizeof(MeshDiskLink[2]));
  /* --- done --- */

  mesh_disk_edge_append(e, e->v1);
  mesh_disk_edge_append(e, e->v2);

  /* may add to middle of the pool */
  mesh->elem_index_dirty |= MESH_EDGE;
  mesh->elem_table_dirty |= MESH_EDGE;
  mesh->spacearr_dirty |= MESH_SPACEARR_DIRTY_ALL;

  mesh->totedge++;

  if (!(create_flag & MESH_CREATE_SKIP_CD)) {
    if (e_example) {
      mesh_elem_attrs_copy(mesh, mesh, e_example, e);
    }
    else {
      CustomData_mesh_set_default(&mesh->edata, &e->head.data);
    }
  }

  MESH_CHECK_ELEMENT(e);

  return e;
}

/**
 * note In most cases a \a l_example should be NULL,
 * since this is a low level API and we shouldn't attempt to be clever and guess what's intended.
 * In cases where copying adjacent loop-data is useful, see mesh_face_copy_shared.
 */
static MeshLoop *mesh_loop_create(Mesh *mesh,
                                  MeshVert *v,
                                  MeshEdge *e,
                                  MeshFace *f,
                                  const MeshLoop *l_example,
                                  const eMeshCreateFlag create_flag)
{
  MeshLoop *l = NULL;

  l = Aliv_mempool_alloc(mesh->lpool);

  lib_assert((l_example == NULL) || (l_example->head.htype == MESH_LOOP));
  lib_assert(!(create_flag & 1));

#ifndef NDEBUG
  if (l_example) {
    /* ensure passing a loop is either sharing the same vertex, or entirely disconnected
     * use to catch mistake passing in loop offset-by-one. */
    lib_assert((v == l_example->v) || !ELEM(v, l_example->prev->v, l_example->next->v));
  }
#endif

  /* --- assign all members --- */
  l->head.data = NULL;

#ifdef USE_DEBUG_INDEX_MEMCHECK
  DEBUG_MEMCHECK_INDEX_INVALIDATE(l);
#else
  mesh_elem_index_set(l, -1); /* set_ok_invalid */
#endif

  l->head.htype = MESH_LOOP;
  l->head.hflag = 0;
  l->head.api_flag = 0;

  l->v = v;
  l->e = e;
  l->f = f;

  l->radial_next = NULL;
  l->radial_prev = NULL;
  l->next = NULL;
  l->prev = NULL;
  /* --- done --- */

  /* may add to middle of the pool */
  mesh->elem_index_dirty |= MESH_LOOP;
  mesh->spacearr_dirty |= MESH_SPACEARR_DIRTY_ALL;

  mesh->totloop++;

  if (!(create_flag & MESH_CREATE_SKIP_CD)) {
    if (l_example) {
      /* no need to copy attrs, just handle customdata */
      // mesh_elem_attrs_copy(mesh, mesh, l_example, l);
      CustomData_mesh_free_block_data(&mesh->ldata, l->head.data);
      CustomData_mesh_copy_data(&mesh->ldata, &mesh->ldata, l_example->head.data, &l->head.data);
    }
    else {
      CustomData_mesh_set_default(&mesh->ldata, &l->head.data);
    }
  }

  return l;
}

static MeshLoop *mesh_face_boundary_add(
    Mesh *mesh, MeshFace *f, MeshVert *startv, MeshEdge *starte, const eBMCreateFlag create_flag)
{
#ifdef USE_MESH_HOLES
  MeshLoopList *lst = lib_mempool_calloc(mesh->looplistpool);
#endif
  MeshLoop *l = mesh_loop_create(mesh, startv, starte, f, NULL /* starte->l */, create_flag);

  mesh_radial_loop_append(starte, l);

#ifdef USE_MESH_HOLES
  lst->first = lst->last = l;
  lib_addtail(&f->loops, lst);
#else
  f->l_first = l;
#endif

  return l;
}

MeshFace *mesh_face_copy(
    Mesh *mesh_dst, Mesh *mesh_src, MeshFace *f, const bool copy_verts, const bool copy_edges)
{
  MeshVert **verts = lib_array_alloca(verts, f->len);
  MeshEdge **edges = lib_array_alloca(edges, f->len);
  MeshLoop *l_iter;
  MeshLoop *l_first;
  MeshLoop *l_copy;
  MeshFace *f_copy;
  int i;

  lib_assert((mesh_dst == mesh_src) || (copy_verts && copy_edges));

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  i = 0;
  do {
    if (copy_verts) {
      verts[i] = mesh_vert_create(mesh_dst, l_iter->v->co, l_iter->v, BM_CREATE_NOP);
    }
    else {
      verts[i] = l_iter->v;
    }
    i++;
  } while ((l_iter = l_iter->next) != l_first);

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  i = 0;
  do {
    if (copy_edges) {
      MeshVert *v1, *v2;

      if (l_iter->e->v1 == verts[i]) {
        v1 = verts[i];
        v2 = verts[(i + 1) % f->len];
      }
      else {
        v2 = verts[i];
        v1 = verts[(i + 1) % f->len];
      }

      edges[i] = mesh_edge_create(mesh_dst, v1, v2, l_iter->e, MESH_CREATE_NOP);
    }
    else {
      edges[i] = l_iter->e;
    }
    i++;
  } while ((l_iter = l_iter->next) != l_first);

  f_copy = mesh_face_create(mesh_dst, verts, edges, f->len, NULL, MESH_CREATE_SKIP_CD);

  mesh_elem_attrs_copy(mesh_src, mesh_dst, f, f_copy);

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  l_copy = MESH_FACE_FIRST_LOOP(f_copy);
  do {
    mesh_elem_attrs_copy(mesh_src, mesh_dst, l_iter, l_copy);
    l_copy = l_copy->next;
  } while ((l_iter = l_iter->next) != l_first);

  return f_copy;
}

/**
 * only create the face, since this calloc's the length is initialized to 0,
 * leave adding loops to the caller.
 *
 * note Caller needs to handle customdata.
 */
LIB_INLINE MeshFace *mesh_face_create__internal(Mesh *mesh)
{
  MeshFace *f;

  f = lib_mempool_alloc(mesh->fpool);

  /* --- assign all members --- */
  f->head.data = NULL;
#ifdef USE_DEBUG_INDEX_MEMCHECK
  DEBUG_MEMCHECK_INDEX_INVALIDATE(f);
#else
  mesh_elem_index_set(f, -1); /* set_ok_invalid */
#endif

  f->head.htype = MESH_FACE;
  f->head.hflag = 0;
  f->head.api_flag = 0;

  /* allocate flags */
  if (mesh->use_toolflags) {
    ((MeshFace_OFlag *)f)->oflags = bm->ftoolflagpool ? BLI_mempool_calloc(bm->ftoolflagpool) : NULL;
  }

#ifdef USE_MESH_HOLES
  lib_listbase_clear(&f->loops);
#else
  f->l_first = NULL;
#endif
  f->len = 0;
  /* caller must initialize */
  // zero_v3(f->no);
  f->mat_nr = 0;
  /* --- done --- */

  /* may add to middle of the pool */
  mesh->elem_index_dirty |= MESH_FACE;
  mesh->elem_table_dirty |= MESH_FACE;
  mesh->spacearr_dirty |= MESH_SPACEARR_DIRTY_ALL;

  mesh->totface++;

#ifdef USE_MESH_HOLES
  f->totbounds = 0;
#endif

  return f;
}

MeshFace *mesh_face_create(Mesh *mesh,
                       MeshVert **verts,
                       MeshEdge **edges,
                       const int len,
                       const MeshFace *f_example,
                       const eMeshCreateFlag create_flag)
{
  MeshFace *f = NULL;
  MeshLoop *l, *startl, *lastl;
  int i;

  lib_assert((f_example == NULL) || (f_example->head.htype == BM_FACE));
  lib_assert(!(create_flag & 1));

  if (len == 0) {
    /* just return NULL for now */
    return NULL;
  }

  if (create_flag & MESH_CREATE_NO_DOUBLE) {
    /* Check if face already exists */
    f = mesh_face_exists(verts, len);
    if (f != NULL) {
      return f;
    }
  }

  f = mesh_face_create__internal(mesh);

  startl = lastl = mesh_face_boundary_add(bm, f, verts[0], edges[0], create_flag);

  for (i = 1; i < len; i++) {
    l = mesh_loop_create(mesh, verts[i], edges[i], f, NULL /* edges[i]->l */, create_flag);

    mesh_radial_loop_append(edges[i], l);

    l->prev = lastl;
    lastl->next = l;
    lastl = l;
  }

  startl->prev = lastl;
  lastl->next = startl;

  f->len = len;

  if (!(create_flag & MESH_CREATE_SKIP_CD)) {
    if (f_example) {
      mesh_elem_attrs_copy(mesh, mesh, f_example, f);
    }
    else {
      CustomData_mesh_set_default(&mesh->pdata, &f->head.data);
      zero_v3(f->no);
    }
  }
  else {
    if (f_example) {
      copy_v3_v3(f->no, f_example->no);
    }
    else {
      zero_v3(f->no);
    }
  }

  MESH_CHECK_ELEMENT(f);

  return f;
}

MeshFace *mesh_face_create_verts(Mesh *mesh,
                             MeshVert **vert_arr,
                             const int len,
                             const MeshFace *f_example,
                             const eMeshCreateFlag create_flag,
                             const bool create_edges)
{
  MeshEdge **edge_arr = lib_array_alloca(edge_arr, len);

  if (create_edges) {
    mesh_edges_from_verts_ensure(mesh, edge_arr, vert_arr, len);
  }
  else {
    if (mesh_edges_from_verts(edge_arr, vert_arr, len) == false) {
      return NULL;
    }
  }

  return mesh_face_create(mesh, vert_arr, edge_arr, len, f_example, create_flag);
}

#ifndef NDEBUG

int mesh_elem_check(void *element, const char htype)
{
  MeshHeader *head = element;
  enum {
    IS_NULL = (1 << 0),
    IS_WRONG_TYPE = (1 << 1),

    IS_VERT_WRONG_EDGE_TYPE = (1 << 2),

    IS_EDGE_NULL_DISK_LINK = (1 << 3),
    IS_EDGE_WRONG_LOOP_TYPE = (1 << 4),
    IS_EDGE_WRONG_FACE_TYPE = (1 << 5),
    IS_EDGE_NULL_RADIAL_LINK = (1 << 6),
    IS_EDGE_ZERO_FACE_LENGTH = (1 << 7),

    IS_LOOP_WRONG_FACE_TYPE = (1 << 8),
    IS_LOOP_WRONG_EDGE_TYPE = (1 << 9),
    IS_LOOP_WRONG_VERT_TYPE = (1 << 10),
    IS_LOOP_VERT_NOT_IN_EDGE = (1 << 11),
    IS_LOOP_NULL_CYCLE_LINK = (1 << 12),
    IS_LOOP_ZERO_FACE_LENGTH = (1 << 13),
    IS_LOOP_WRONG_FACE_LENGTH = (1 << 14),
    IS_LOOP_WRONG_RADIAL_LENGTH = (1 << 15),

    IS_FACE_NULL_LOOP = (1 << 16),
    IS_FACE_WRONG_LOOP_FACE = (1 << 17),
    IS_FACE_NULL_EDGE = (1 << 18),
    IS_FACE_NULL_VERT = (1 << 19),
    IS_FACE_LOOP_VERT_NOT_IN_EDGE = (1 << 20),
    IS_FACE_LOOP_WRONG_RADIAL_LENGTH = (1 << 21),
    IS_FACE_LOOP_WRONG_DISK_LENGTH = (1 << 22),
    IS_FACE_LOOP_DUPE_LOOP = (1 << 23),
    IS_FACE_LOOP_DUPE_VERT = (1 << 24),
    IS_FACE_LOOP_DUPE_EDGE = (1 << 25),
    IS_FACE_WRONG_LENGTH = (1 << 26),
  } err = 0;

  if (!element) {
    return IS_NULL;
  }

  if (head->htype != htype) {
    return IS_WRONG_TYPE;
  }

  switch (htype) {
    case MESH_VERT: {
      MeshVert *v = element;
      if (v->e && v->e->head.htype != BM_EDGE) {
        err |= IS_VERT_WRONG_EDGE_TYPE;
      }
      break;
    }
    case MESH_EDGE: {
      MeshEdge *e = element;
      if (e->v1_disk_link.prev == NULL || e->v2_disk_link.prev == NULL ||
          e->v1_disk_link.next == NULL || e->v2_disk_link.next == NULL) {
        err |= IS_EDGE_NULL_DISK_LINK;
      }

      if (e->l && e->l->head.htype != BM_LOOP) {
        err |= IS_EDGE_WRONG_LOOP_TYPE;
      }
      if (e->l && e->l->f->head.htype != BM_FACE) {
        err |= IS_EDGE_WRONG_FACE_TYPE;
      }
      if (e->l && (e->l->radial_next == NULL || e->l->radial_prev == NULL)) {
        err |= IS_EDGE_NULL_RADIAL_LINK;
      }
      if (e->l && e->l->f->len <= 0) {
        err |= IS_EDGE_ZERO_FACE_LENGTH;
      }
      break;
    }
    case MESH_LOOP: {
      MeshLoop *l = element, *l2;
      int i;

      if (l->f->head.htype != BM_FACE) {
        err |= IS_LOOP_WRONG_FACE_TYPE;
      }
      if (l->e->head.htype != BM_EDGE) {
        err |= IS_LOOP_WRONG_EDGE_TYPE;
      }
      if (l->v->head.htype != BM_VERT) {
        err |= IS_LOOP_WRONG_VERT_TYPE;
      }
      if (!mesh_vert_in_edge(l->e, l->v)) {
        fprintf(stderr,
                "%s: fatal bmesh error (vert not in edge)! (bmesh internal error)\n",
                __func__);
        err |= IS_LOOP_VERT_NOT_IN_EDGE;
      }

      if (l->radial_next == NULL || l->radial_prev == NULL) {
        err |= IS_LOOP_NULL_CYCLE_LINK;
      }
      if (l->f->len <= 0) {
        err |= IS_LOOP_ZERO_FACE_LENGTH;
      }

      /* validate boundary loop -- invalid for hole loops, of course,
       * but we won't be allowing those for a while yet */
      l2 = l;
      i = 0;
      do {
        if (i >= BM_NGON_MAX) {
          break;
        }

        i++;
      } while ((l2 = l2->next) != l);

      if (i != l->f->len || l2 != l) {
        err |= IS_LOOP_WRONG_FACE_LENGTH;
      }

      if (!bmesh_radial_validate(bmesh_radial_length(l), l)) {
        err |= IS_LOOP_WRONG_RADIAL_LENGTH;
      }

      break;
    }
    case BM_FACE: {
      BMFace *f = element;
      BMLoop *l_iter;
      BMLoop *l_first;
      int len = 0;

#  ifdef USE_BMESH_HOLES
      if (!f->loops.first)
#  else
      if (!f->l_first)
#  endif
      {
        err |= IS_FACE_NULL_LOOP;
      }
      l_iter = l_first = BM_FACE_FIRST_LOOP(f);
      do {
        if (l_iter->f != f) {
          fprintf(stderr,
                  "%s: loop inside one face points to another! (bmesh internal error)\n",
                  __func__);
          err |= IS_FACE_WRONG_LOOP_FACE;
        }

        if (!l_iter->e) {
          err |= IS_FACE_NULL_EDGE;
        }
        if (!l_iter->v) {
          err |= IS_FACE_NULL_VERT;
        }
        if (l_iter->e && l_iter->v) {
          if (!BM_vert_in_edge(l_iter->e, l_iter->v) ||
              !BM_vert_in_edge(l_iter->e, l_iter->next->v)) {
            err |= IS_FACE_LOOP_VERT_NOT_IN_EDGE;
          }

          if (!bmesh_radial_validate(bmesh_radial_length(l_iter), l_iter)) {
            err |= IS_FACE_LOOP_WRONG_RADIAL_LENGTH;
          }

          if (bmesh_disk_count_at_most(l_iter->v, 2) < 2) {
            err |= IS_FACE_LOOP_WRONG_DISK_LENGTH;
          }
        }

        /* check for duplicates */
        if (BM_ELEM_API_FLAG_TEST(l_iter, _FLAG_ELEM_CHECK)) {
          err |= IS_FACE_LOOP_DUPE_LOOP;
        }
        BM_ELEM_API_FLAG_ENABLE(l_iter, _FLAG_ELEM_CHECK);
        if (l_iter->v) {
          if (BM_ELEM_API_FLAG_TEST(l_iter->v, _FLAG_ELEM_CHECK)) {
            err |= IS_FACE_LOOP_DUPE_VERT;
          }
          BM_ELEM_API_FLAG_ENABLE(l_iter->v, _FLAG_ELEM_CHECK);
        }
        if (l_iter->e) {
          if (BM_ELEM_API_FLAG_TEST(l_iter->e, _FLAG_ELEM_CHECK)) {
            err |= IS_FACE_LOOP_DUPE_EDGE;
          }
          BM_ELEM_API_FLAG_ENABLE(l_iter->e, _FLAG_ELEM_CHECK);
        }

        len++;
      } while ((l_iter = l_iter->next) != l_first);

      /* cleanup duplicates flag */
      l_iter = l_first = BM_FACE_FIRST_LOOP(f);
      do {
        BM_ELEM_API_FLAG_DISABLE(l_iter, _FLAG_ELEM_CHECK);
        if (l_iter->v) {
          BM_ELEM_API_FLAG_DISABLE(l_iter->v, _FLAG_ELEM_CHECK);
        }
        if (l_iter->e) {
          BM_ELEM_API_FLAG_DISABLE(l_iter->e, _FLAG_ELEM_CHECK);
        }
      } while ((l_iter = l_iter->next) != l_first);

      if (len != f->len) {
        err |= IS_FACE_WRONG_LENGTH;
      }
      break;
    }
    default:
      BLI_assert(0);
      break;
  }

  BMESH_ASSERT(err == 0);

  return err;
}

#endif /* NDEBUG */

/**
 * low level function, only frees the vert,
 * doesn't change or adjust surrounding geometry
 */
static void bm_kill_only_vert(BMesh *bm, BMVert *v)
{
  bm->totvert--;
  bm->elem_index_dirty |= BM_VERT;
  bm->elem_table_dirty |= BM_VERT;
  bm->spacearr_dirty |= BM_SPACEARR_DIRTY_ALL;

  BM_select_history_remove(bm, v);

  if (v->head.data) {
    CustomData_bmesh_free_block(&bm->vdata, &v->head.data);
  }

  if (bm->vtoolflagpool) {
    BLI_mempool_free(bm->vtoolflagpool, ((BMVert_OFlag *)v)->oflags);
  }
  BLI_mempool_free(bm->vpool, v);
}

/**
 * low level function, only frees the edge,
 * doesn't change or adjust surrounding geometry
 */
static void bm_kill_only_edge(BMesh *bm, BMEdge *e)
{
  bm->totedge--;
  bm->elem_index_dirty |= BM_EDGE;
  bm->elem_table_dirty |= BM_EDGE;
  bm->spacearr_dirty |= BM_SPACEARR_DIRTY_ALL;

  BM_select_history_remove(bm, (BMElem *)e);

  if (e->head.data) {
    CustomData_bmesh_free_block(&bm->edata, &e->head.data);
  }

  if (bm->etoolflagpool) {
    BLI_mempool_free(bm->etoolflagpool, ((BMEdge_OFlag *)e)->oflags);
  }
  BLI_mempool_free(bm->epool, e);
}

/**
 * low level function, only frees the face,
 * doesn't change or adjust surrounding geometry
 */
static void bm_kill_only_face(BMesh *bm, BMFace *f)
{
  if (bm->act_face == f) {
    bm->act_face = NULL;
  }

  bm->totface--;
  bm->elem_index_dirty |= BM_FACE;
  bm->elem_table_dirty |= BM_FACE;
  bm->spacearr_dirty |= BM_SPACEARR_DIRTY_ALL;

  BM_select_history_remove(bm, (BMElem *)f);

  if (f->head.data) {
    CustomData_bmesh_free_block(&bm->pdata, &f->head.data);
  }

  if (bm->ftoolflagpool) {
    BLI_mempool_free(bm->ftoolflagpool, ((BMFace_OFlag *)f)->oflags);
  }
  BLI_mempool_free(bm->fpool, f);
}

/**
 * low level function, only frees the loop,
 * doesn't change or adjust surrounding geometry
 */
static void bm_kill_only_loop(BMesh *bm, BMLoop *l)
{
  bm->totloop--;
  bm->elem_index_dirty |= BM_LOOP;
  bm->spacearr_dirty |= BM_SPACEARR_DIRTY_ALL;

  if (l->head.data) {
    CustomData_bmesh_free_block(&bm->ldata, &l->head.data);
  }

  BLI_mempool_free(bm->lpool, l);
}

void BM_face_edges_kill(BMesh *bm, BMFace *f)
{
  BMEdge **edges = BLI_array_alloca(edges, f->len);
  BMLoop *l_iter;
  BMLoop *l_first;
  int i = 0;

  l_iter = l_first = BM_FACE_FIRST_LOOP(f);
  do {
    edges[i++] = l_iter->e;
  } while ((l_iter = l_iter->next) != l_first);

  for (i = 0; i < f->len; i++) {
    BM_edge_kill(bm, edges[i]);
  }
}

void BM_face_verts_kill(BMesh *bm, BMFace *f)
{
  BMVert **verts = BLI_array_alloca(verts, f->len);
  BMLoop *l_iter;
  BMLoop *l_first;
  int i = 0;

  l_iter = l_first = BM_FACE_FIRST_LOOP(f);
  do {
    verts[i++] = l_iter->v;
  } while ((l_iter = l_iter->next) != l_first);

  for (i = 0; i < f->len; i++) {
    BM_vert_kill(bm, verts[i]);
  }
}

void BM_face_kill(BMesh *bm, BMFace *f)
{
#ifdef USE_BMESH_HOLES
  BMLoopList *ls, *ls_next;
#endif

#ifdef NDEBUG
  /* check length since we may be removing degenerate faces */
  if (f->len >= 3) {
    BM_CHECK_ELEMENT(f);
  }
#endif

#ifdef USE_BMESH_HOLES
  for (ls = f->loops.first; ls; ls = ls_next)
#else
  if (f->l_first)
#endif
  {
    BMLoop *l_iter, *l_next, *l_first;

#ifdef USE_BMESH_HOLES
    ls_next = ls->next;
    l_iter = l_first = ls->first;
#else
    l_iter = l_first = f->l_first;
#endif

    do {
      l_next = l_iter->next;

      bmesh_radial_loop_remove(l_iter->e, l_iter);
      bm_kill_only_loop(bm, l_iter);

    } while ((l_iter = l_next) != l_first);

#ifdef USE_BMESH_HOLES
    BLI_mempool_free(bm->looplistpool, ls);
#endif
  }

  bm_kill_only_face(bm, f);
}

void BM_face_kill_loose(BMesh *bm, BMFace *f)
{
#ifdef USE_BMESH_HOLES
  BMLoopList *ls, *ls_next;
#endif

  BM_CHECK_ELEMENT(f);

#ifdef USE_BMESH_HOLES
  for (ls = f->loops.first; ls; ls = ls_next)
#else
  if (f->l_first)
#endif
  {
    BMLoop *l_iter, *l_next, *l_first;

#ifdef USE_BMESH_HOLES
    ls_next = ls->next;
    l_iter = l_first = ls->first;
#else
    l_iter = l_first = f->l_first;
#endif

    do {
      BMEdge *e;
      l_next = l_iter->next;

      e = l_iter->e;
      bmesh_radial_loop_remove(e, l_iter);
      bm_kill_only_loop(bm, l_iter);

      if (e->l == NULL) {
        BMVert *v1 = e->v1, *v2 = e->v2;

        bmesh_disk_edge_remove(e, e->v1);
        bmesh_disk_edge_remove(e, e->v2);
        bm_kill_only_edge(bm, e);

        if (v1->e == NULL) {
          bm_kill_only_vert(bm, v1);
        }
        if (v2->e == NULL) {
          bm_kill_only_vert(bm, v2);
        }
      }
    } while ((l_iter = l_next) != l_first);

#ifdef USE_BMESH_HOLES
    BLI_mempool_free(bm->looplistpool, ls);
#endif
  }

  bm_kill_only_face(bm, f);
}

void BM_edge_kill(BMesh *bm, BMEdge *e)
{
  while (e->l) {
    BM_face_kill(bm, e->l->f);
  }

  bmesh_disk_edge_remove(e, e->v1);
  bmesh_disk_edge_remove(e, e->v2);

  bm_kill_only_edge(bm, e);
}
