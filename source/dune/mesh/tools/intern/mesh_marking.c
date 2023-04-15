/**
 * Selection routines for mesh structures.
 * This is actually all old code ripped from
 * editmesh_lib.c and slightly modified to work
 * for mesh's. This also means that it has some
 * of the same problems.... something that
 * that should be addressed eventually.
 */

#include <stddef.h>

#include "mem_guardedalloc.h"

#include "types_scene.h"

#include "lib_listbase.h"
#include "lib_math.h"
#include "lib_task.h"

#include "mesh.h"
#include "mesh_structure.h"

/* For '_FLAG_OVERLAP'. */
#include "mesh_private.h"

/* -------------------------------------------------------------------- */
/**Recounting total selection. **/

typedef struct SelectionCountChunkData {
  int selection_len;
} SelectionCountChunkData;

static void recount_totsels_range_vert_func(void *UNUSED(userdata),
                                            MempoolIterData *iter,
                                            const TaskParallelTLS *__restrict tls)
{
  SelectionCountChunkData *count = tls->userdata_chunk;
  const MeshVert *eve = (const MeshVert *)iter;
  if (mesh_elem_flag_test(eve, MESH_ELEM_SELECT)) {
    count->selection_len += 1;
  }
}

static void recount_totsels_range_edge_func(void *UNUSED(userdata),
                                            MempoolIterData *iter,
                                            const TaskParallelTLS *__restrict tls)
{
  SelectionCountChunkData *count = tls->userdata_chunk;
  const MeshEdge *eed = (const MeshEdge *)iter;
  if (mesh_elem_flag_test(eed, MESH_ELEM_SELECT)) {
    count->selection_len += 1;
  }
}

static void recount_totsels_range_face_fn(void *UNUSED(userdata),
                                            MempoolIterData *iter,
                                            const TaskParallelTLS *__restrict tls)
{
  SelectionCountChunkData *count = tls->userdata_chunk;
  const MeshFace *efa = (const MeshFace *)iter;
  if (mesh_elem_flag_test(efa, MESH_ELEM_SELECT)) {
    count->selection_len += 1;
  }
}

static void recount_totsels_reduce(const void *__restrict UNUSED(userdata),
                                   void *__restrict chunk_join,
                                   void *__restrict chunk)
{
  SelectionCountChunkData *dst = chunk_join;
  const SelectionCountChunkData *src = chunk;
  dst->selection_len += src->selection_len;
}

static TaskParallelMempoolFn recount_totsels_get_range_fn(MeshIterType iter_type)
{
  lib_assert(ELEM(iter_type, MESH_VERTS_OF_MESH, MESH_EDGES_OF_MESH, MESH_FACES_OF_MESH));

  TaskParallelMempoolFn range_fn = NULL;
  if (iter_type == MESH_VERTS_OF_MESH) {
    range_fn = recount_totsels_range_vert_fn;
  }
  else if (iter_type == MESH_EDGES_OF_MESH) {
    range_fn = recount_totsels_range_edge_func;
  }
  else if (iter_type == MESH_FACES_OF_MESH) {
    range_fn = recount_totsels_range_face_func;
  }
  return range_fn;
}

static int recount_totsel(Mesh *mesh, MeshIterType iter_type)
{
  const int MIN_ITER_SIZE = 1024;

  TaskParallelSettings settings;
  lib_parallel_range_settings_defaults(&settings);
  settings.fn_reduce = recount_totsels_reduce;
  settings.min_iter_per_thread = MIN_ITER_SIZE;

  SelectionCountChunkData count = {0};
  settings.userdata_chunk = &count;
  settings.userdata_chunk_size = sizeof(count);

  TaskParallelMempoolFn range_fn = recount_totsels_get_range_fn(iter_type);
  mesh_iter_parallel(mesh, iter_type, range_fn, NULL, &settings);
  return count.selection_len;
}

static void recount_totvertsel(Mesh *meeh)
{
  mesh->totvertsel = recount_totsel(mesh, MESH_VERTS_OF_MESH);
}

static void recount_totedgesel(Mesh *mesh)
{
  mesh->totedgesel = recount_totsel(mesh, MESH_EDGES_OF_MESH);
}

static void recount_totfacesel(Mesh *mesh)
{
  mesh->totfacesel = recount_totsel(mesh, MESH_FACES_OF_MESH);
}

static void recount_totsels(Mesh *mesh)
{
  recount_totvertsel(mesh);
  recount_totedgesel(mesh);
  recount_totfacesel(mesh);
}

#ifndef NDEBUG
static bool recount_totsels_are_ok(Mesh *mesh)
{
  return mesh->totvertsel == recount_totsel(mesh, MESH_VERTS_OF_MESH) &&
         mesh->totedgesel == recount_totsel(mesh, MESH_EDGES_OF_MESH) &&
         mesh->totfacesel == recount_totsel(mesh, MESH_FACES_OF_MESH);
}
#endif

/* -------------------------------------------------------------------- */
/** Mesh helper functions for selection & hide flushing. **/

static bool mesh_vert_is_edge_select_any_other(const MeshVert *v, const MeshEdge *e_first)
{
  const MeshEdge *e_iter = e_first;

  /* start by stepping over the current edge */
  while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first) {
    if (mesh_elem_flag_test(e_iter, MESH_ELEM_SELECT)) {
      return true;
    }
  }
  return false;
}

#if 0
static bool mesh_vert_is_edge_select_any(const MeshVert *v)
{
  if (v->e) {
    const MeshEdge *e_iter, *e_first;
    e_iter = e_first = v->e;
    do {
      if (mesh_elem_flag_test(e_iter, MESH_ELEM_SELECT)) {
        return true;
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);
  }
  return false;
}
#endif

static bool mesh_vert_is_edge_visible_any(const MeshVert *v)
{
  if (v->e) {
    const MeshEdge *e_iter, *e_first;
    e_iter = e_first = v->e;
    do {
      if (!mesh_elem_flag_test(e_iter, MESH_ELEM_HIDDEN)) {
        return true;
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);
  }
  return false;
}

static bool mesh_edge_is_face_select_any_other(MeshLoop *l_first)
{
  const MeshLoop *l_iter = l_first;

  /* start by stepping over the current face */
  while ((l_iter = l_iter->radial_next) != l_first) {
    if (mesh_elem_flag_test(l_iter->f, MESH_ELEM_SELECT)) {
      return true;
    }
  }
  return false;
}

#if 0
static bool mesh_edge_is_face_select_any(const MeshEdge *e)
{
  if (e->l) {
    const MeshLoop *l_iter, *l_first;
    l_iter = l_first = e->l;
    do {
      if (mesh_elem_flag_test(l_iter->f, MESH_ELEM_SELECT)) {
        return true;
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }
  return false;
}
#endif

static bool mesh_edge_is_face_visible_any(const MeshEdge *e)
{
  if (e->l) {
    const MeshLoop *l_iter, *l_first;
    l_iter = l_first = e->l;
    do {
      if (!mesh_elem_flag_test(l_iter->f, MESH_ELEM_HIDDEN)) {
        return true;
      }
    } while ((l_iter = l_iter->radial_next) != l_first);
  }
  return false;
}

void mesh_select_mode_clean_ex(Mesh *mesh, const short selectmode)
{
  if (selectmode & SCE_SELECT_VERTEX) {
    /* pass */
  }
  else if (selectmode & SCE_SELECT_EDGE) {
   MeshIter iter;

    if (mesh->totvertsel) {
      MeshVert *v;
      MESH_ITER (v, &iter, mesh, MESH_VERTS_OF_MESH) {
        mesh_elem_flag_disable(MESH_ELEM_SELECT);
      }
     mesh->totvertsel = 0;
    }

    if (mesh->totedgesel) {
      MeshEdge *e;
      MESH_ITER (e, &iter, mesh, MESH_EDGES_OF_MESH) {
        if (mesh_elem_flag_test(e, MESH_ELEM_SELECT)) {
          mesh_vert_select_set(mesh, e->v1, true);
          mesh_vert_select_set(mesh, e->v2, true);
        }
      }
    }
  }
  else if (selectmode & SCE_SELECT_FACE) {
    MeshIter iter;

    if (mesh->totvertsel) {
      MeshVert *v;
      MESH_ITER (v, &iter, mesh, MESH_VERTS_OF_MESH) {
        mesh_elem_flag_disable(v, MESH_ELEM_SELECT);
      }
      mesh->totvertsel = 0;
    }

    if (mesh->totedgesel) {
      MeshEdge *e;
      MESH_ITER (e, &iter, mesh, MESH_EDGES_OF_MESH) {
        mesh_elem_flag_disable(e, MESH_ELEM_SELECT);
      }
      mesh->totedgesel = 0;
    }

    if (mesh->totfacesel) {
      MeshFace *f;
      MESH_ITER (f, &iter, mesh, MESH_FACES_OF_MESH) {
        if (mesh_elem_flag_test(f, MESH_ELEM_SELECT)) {
          MeshLoop *l_iter, *l_first;
          l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
          do {
            mesh_edge_select_set(mesh, l_iter->e, true);
          } while ((l_iter = l_iter->next) != l_first);
        }
      }
    }
  }
}

void mesh_select_mode_clean(Mesh *mesh)
{
  mesh_select_mode_clean_ex(mesh, mesh->selectmode);
}

/* -------------------------------------------------------------------- */
/** Select mode flush selection **/

typedef struct SelectionFlushChunkData {
  int delta_selection_len;
} SelectionFlushChunkData;

static void mesh_select_mode_flush_vert_to_edge_iter_fn(void *UNUSED(userdata),
                                                       MempoolIterData *iter,
                                                       const TaskParallelTLS *__restrict tls)
{
  SelectionFlushChunkData *chunk_data = tls->userdata_chunk;
  MeshEdge *e = (MeshEdge *)iter;
  const bool is_selected = mesh_elem_flag_test(e, MESH_ELEM_SELECT);
  const bool is_hidden = mesh_elem_flag_test(e, MESH_ELEM_HIDDEN);
  if (!is_hidden &&
      (mesh_elem_flag_test(e->v1, MESH_ELEM_SELECT) && mesh_elem_flag_test(e->v2, BM_ELEM_SELECT))) {
    mesh_elem_flag_enable(e, MESH_ELEM_SELECT);
    chunk_data->delta_selection_len += is_selected ? 0 : 1;
  }
  else {
    mesh_elem_flag_disable(e, MESH_ELEM_SELECT);
    chunk_data->delta_selection_len += is_selected ? -1 : 0;
  }
}

static void mesh_select_mode_flush_edge_to_face_iter_fn(void *UNUSED(userdata),
                                                           MempoolIterData *iter,
                                                           const TaskParallelTLS *__restrict tls)
{
  SelectionFlushChunkData *chunk_data = tls->userdata_chunk;
  MeshFace *f = (MeshFace *)iter;
  MeshLoop *l_iter;
  MeshLoop *l_first;
  const bool is_selected = mesh_elem_flag_test(f, MESH_ELEM_SELECT);
  bool ok = true;
  if (!mesh_elem_flag_test(f, MESH_ELEM_HIDDEN)) {
    l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
    do {
      if (!mesh_elem_flag_test(l_iter->e, BM_ELEM_SELECT)) {
        ok = false;
        break;
      }
    } while ((l_iter = l_iter->next) != l_first);
  }
  else {
    ok = false;
  }

  mesh_elem_flag_set(f, MESH_ELEM_SELECT, ok);
  if (is_selected && !ok) {
    chunk_data->delta_selection_len -= 1;
  }
  else if (ok && !is_selected) {
    chunk_data->delta_selection_len += 1;
  }
}

static void mesh_select_mode_flush_reduce_fn(const void *__restrict UNUSED(userdata),
                                             void *__restrict chunk_join,
                                             void *__restrict chunk)
{
  SelectionFlushChunkData *dst = chunk_join;
  const SelectionFlushChunkData *src = chunk;
  dst->delta_selection_len += src->delta_selection_len;
}

static void mesh_select_mode_flush_vert_to_edge(Mesh *mesh)
{
  SelectionFlushChunkData chunk_data = {0};

  TaskParallelSettings settings;
  lib_parallel_range_settings_defaults(&settings);
  settings.use_threading = mesh->totedge >= MESH_OMP_LIMIT;
  settings.userdata_chunk = &chunk_data;
  settings.userdata_chunk_size = sizeof(chunk_data);
  settings.func_reduce = mesh_mesh_select_mode_flush_reduce_fn;

  mesh_iter_parallel(
      mesh, MESH_EDGES_OF_MESH, mesh_select_mode_flush_vert_to_edge_iter_fn, NULL, &settings);
  mesh->totedgesel += chunk_data.delta_selection_len;
}

static void mesh_select_mode_flush_edge_to_face(Mesh *mesh)
{
  SelectionFlushChunkData chunk_data = {0};

  TaskParallelSettings settings;
  lib_parallel_range_settings_defaults(&settings);
  settings.use_threading = mesh->totface >= MESH_OMP_LIMIT;
  settings.userdata_chunk = &chunk_data;
  settings.userdata_chunk_size = sizeof(chunk_data);
  settings.func_reduce = mesh_mesh_select_mode_flush_reduce_fn;

  mesh_iter_parallel(
      mesh, MESH_FACES_OF_MESH, mesh_select_mode_flush_edge_to_face_iter_fn, NULL, &settings);
  mesh->totfacesel += chunk_data.delta_selection_len;
}

void mesh_select_mode_flush_ex(Mesh *mesh, const short selectmode, eBMSelectionFlushFLags flags)
{
  if (selectmode & SCE_SELECT_VERTEX) {
    mesh_select_mode_flush_vert_to_edge(mesh);
  }

  if (selectmode & (SCE_SELECT_VERTEX | SCE_SELECT_EDGE)) {
    mesh_select_mode_flush_edge_to_face(mesh);
  }

  /* Remove any deselected elements from the MeshEditSelection */
  mesh_select_history_validate(mesh);

  if (flags & MESH_SELECT_LEN_FLUSH_RECALC_VERT) {
    recount_totvertsel(mesh);
  }
  if (flags & MESH_SELECT_LEN_FLUSH_RECALC_EDGE) {
    recount_totedgesel(mesh);
  }
  if (flags & MESH_SELECT_LEN_FLUSH_RECALC_FACE) {
    recount_totfacesel(mesh);
  }
  lib_assert(recount_totsels_are_ok(mesh));
}

void mesh_select_mode_flush(Mesh *mesh)
{
  mesh_select_mode_flush_ex(mesh, mesh->selectmode, BM_SELECT_LEN_FLUSH_RECALC_ALL);
}

void mesh_deselect_flush(Mesh *mesh)
{
  MeshIter eiter;
  MeshEdge *e;

  MESH_ITER (e, &eiter, mesh, MESH_EDGES_OF_MESH) {
    if (!mesh_elem_flag_test(e, MESH_ELEM_HIDDEN)) {
      if (mesh_elem_flag_test(e, MESH_ELEM_SELECT)) {
        if (!mesh_elem_flag_test(e->v1, MESH_ELEM_SELECT) ||
            !mesh_elem_flag_test(e->v2, MESH_ELEM_SELECT)) {
          mesh_elem_flag_disable(e, MESH_ELEM_SELECT);
        }
      }

      if (e->l && !mesh_elem_flag_test(e, MESH_ELEM_SELECT)) {
        MeshLoop *l_iter;
        MeshLoop *l_first;

        l_iter = l_first = e->l;
        do {
          mesh_elem_flag_disable(l_iter->f, BM_ELEM_SELECT);
        } while ((l_iter = l_iter->radial_next) != l_first);
      }
    }
  }

  /* Remove any deselected elements from the BMEditSelection */
  mesh_select_history_validate(mesh);

  recount_totsels(mesh);
}

void mesh_select_flush(Mesh *mesh)
{
  MeshEdge *e;
  MeshLoop *l_iter;
  MeshLoop *l_first;
  MeshFace *f;

  Mesh eiter;
  MeshIter fiter;

  bool ok;

  MESH_ITER_MESH (e, &eiter, mesh, MESH_EDGES_OF_MESH) {
    if (mesh_elem_flag_test(e->v1, MESH_ELEM_SELECT) && mesh_elem_flag_test(e->v2, BM_ELEM_SELECT) &&
        !mesh_elem_flag_test(e, MESH_ELEM_HIDDEN)) {
      mesh_elem_flag_enable(e, MESH_ELEM_SELECT);
    }
  }
  MESH_ITER (f, &fiter, mesh, MESH_FACES_OF_MESH) {
    ok = true;
    if (!mesh_elem_flag_test(f, MESH_ELEM_HIDDEN)) {
      l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
      do {
        if (!mesh_elem_flag_test(l_iter->v, MESH_ELEM_SELECT)) {
          ok = false;
          break;
        }
      } while ((l_iter = l_iter->next) != l_first);
    }
    else {
      ok = false;
    }

    if (ok) {
      mesh_elem_flag_enable(f, MESH_ELEM_SELECT);
    }
  }

  recount_totsels(bm);
}

void mesh_vert_select_set(Mesh *mesh, MeshVert *v, const bool select)
{
  lib_assert(v->head.htype == MESH_VERT);

  if (mesh_elem_flag_test(v, MESH_ELEM_HIDDEN)) {
    return;
  }

  if (select) {
    if (!mesh_elem_flag_test(v, MESH_ELEM_SELECT)) {
      mesh_elem_flag_enable(v, MESH_ELEM_SELECT);
      mesh->totvertsel += 1;
    }
  }
  else {
    if (mesh_elem_flag_test(v, MESH_ELEM_SELECT)) {
      mesh->totvertsel -= 1;
      mesh_elem_flag_disable(v, MESH_ELEM_SELECT);
    }
  }
}

void mesh_edge_select_set(Mesh *mesh, MeshEdge *e, const bool select)
{
  lib_assert(e->head.htype == MESH_EDGE);

  if (mesh_elem_flag_test(e, MESH_ELEM_HIDDEN)) {
    return;
  }

  if (select) {
    if (!mesh_elem_flag_test(e, MESH_ELEM_SELECT)) {
      mesh_elem_flag_enable(e, MESH_ELEM_SELECT);
      mesh->totedgesel += 1;
    }
    mesh_vert_select_set(mesh, e->v1, true);
    mesh_vert_select_set(mesh, e->v2, true);
  }
  else {
    if (mesh_elem_flag_test(e, MESH_ELEM_SELECT)) {
      mesh_elem_flag_disable(e, MESH_ELEM_SELECT);
      mesh->totedgesel -= 1;
    }

    if ((mesh->selectmode & SCE_SELECT_VERTEX) == 0) {
      int i;

      /* check if the vert is used by a selected edge */
      for (i = 0; i < 2; i++) {
        MeshVert *v = *((&e->v1) + i);
        if (mesh_vert_is_edge_select_any_other(v, e) == false) {
          mesh_vert_select_set(mesh, v, false);
        }
      }
    }
    else {
      mesh_vert_select_set(mesh, e->v1, false);
      mesh_vert_select_set(mesh, e->v2, false);
    }
  }
}

void mesh_face_select_set(Mesh *mesh, MeshFace *f, const bool select)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;

  lib_assert(f->head.htype == MESH_FACE);

  if (mesh_elem_flag_test(f, MESH_ELEM_HIDDEN)) {
    return;
  }

  if (select) {
    if (!mesh_elem_flag_test(f, MESH_ELEM_SELECT)) {
      mesh_elem_flag_enable(f, MESH_ELEM_SELECT);
      mesh->totfacesel += 1;
    }

    l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
    do {
      mesh_vert_select_set(mesh, l_iter->v, true);
      mesh_edge_select_set(mesh, l_iter->e, true);
    } while ((l_iter = l_iter->next) != l_first);
  }
  else {

    if (mesh_elem_flag_test(f, MESH_ELEM_SELECT)) {
      mesh_elem_flag_disable(f, MESH_ELEM_SELECT);
      mesh->totfacesel -= 1;
    }
    /**
     * This allows a temporarily invalid state - where for eg
     * an edge bay be de-selected, but an adjacent face remains selected.
     *
     * Rely on mesh_select_mode_flush to correct these cases.
     *
     * flushing based on mode, see T46494
     */
    if (mesh->selectmode & SCE_SELECT_VERTEX) {
      l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
      do {
        mesh_vert_select_set(mesh, l_iter->v, false);
        mesh_edge_select_set_noflush(mesh, l_iter->e, false);
      } while ((l_iter = l_iter->next) != l_first);
    }
    else {
      /**
       * use mesh_edge_select_set_noflush,
       * vertex flushing is handled last.
       */
      if (mesh->selectmode & SCE_SELECT_EDGE) {
        l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
        do {
          mesh_edge_select_set_noflush(mesh, l_iter->e, false);
        } while ((l_iter = l_iter->next) != l_first);
      }
      else {
        l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
        do {
          if (mesh_edge_is_face_select_any_other(l_iter) == false) {
            mesh_edge_select_set_noflush(bm, l_iter->e, false);
          }
        } while ((l_iter = l_iter->next) != l_first);
      }

      /* flush down to verts */
      l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
      do {
        if (mesh_vert_is_edge_select_any_other(l_iter->v, l_iter->e) == false) {
          mesh_vert_select_set(mesh, l_iter->v, false);
        }
      } while ((l_iter = l_iter->next) != l_first);
    }
  }
}

/* -------------------------------------------------------------------- */
/** Non Flushing Versions Element Selection **/

void mesh_edge_select_set_noflush(Mesh *mesh, MeshEdge *e, const bool select)
{
  lib_assert(e->head.htype == MESH_EDGE);

  if (mesh_elem_flag_test(e, MESH_ELEM_HIDDEN)) {
    return;
  }

  if (select) {
    if (!mesh_elem_flag_test(e, MESH_ELEM_SELECT)) {
      mesh_elem_flag_enable(e, MESH_ELEM_SELECT);
      mesh->totedgesel += 1;
    }
  }
  else {
    if (mesh_elem_flag_test(e, MESH_ELEM_SELECT)) {
      mesh_elem_flag_disable(e, MESH_ELEM_SELECT);
      mesh->totedgesel -= 1;
    }
  }
}

void mesh_face_select_set_noflush(Mesh *mesh, MeshFace *f, const bool select)
{
  lib_assert(f->head.htype == MESH_FACE);

  if (mesh_elem_flag_test(f, MESH_ELEM_HIDDEN)) {
    return;
  }

  if (select) {
    if (!mesh_elem_flag_test(f, M_ELEM_SELECT)) {
      mesh_elem_flag_enable(f, M_ELEM_SELECT);
      meh->totfacesel += 1;
    }
  }
  else {
    if (mesh_elem_flag_test(f, MESH_ELEM_SELECT)) {
      mesh_elem_flag_disable(f, MESH_ELEM_SELECT);
      mesh->totfacesel -= 1;
    }
  }
}

void mesh_select_mode_set(Mesh *mesh, int selectmode)
{
  MeshIter iter;
  MehElem *ele;

  mesh->selectmode = selectmode;

  if (mesh->selectmode & SCE_SELECT_VERTEX) {
    /* disabled because selection flushing handles these */
#if 0
    MESH_ITER (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
      mesh_elem_flag_disable(ele, MESH_ELEM_SELECT);
    }
    MESH_ITER (ele, &iter, mesh, MESH_FACES_OF_MESH) {
      mesh_elem_flag_disable(ele, MESH_ELEM_SELECT);
    }
#endif
    mesh_select_mode_flush(mesh);
  }
  else if (mesh->selectmode & SCE_SELECT_EDGE) {
    /* disabled because selection flushing handles these */
#if 0
    MESH_ITER (ele, &iter, mesh, MESH_VERTS_OF_MESH) {
      mesh_elem_flag_disable(ele, MESH_ELEM_SELECT);
    }
#endif

    MESH_ITER (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
      if (mesh_elem_flag_test(ele, MESH_ELEM_SELECT)) {
        mesh_edge_select_set(mesh, (MEdge *)ele, true);
      }
    }
    mesh_select_mode_flush(mesh);
  }
  else if (mesh->selectmode & SCE_SELECT_FACE) {
    /* disabled because selection flushing handles these */
#if 0
    MESH_ITER (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
      mesh_elem_flag_disable(ele, MESH_ELEM_SELECT);
    }
#endif
    MESH_ITER (ele, &iter, mesh, MESH_FACES_OF_MESH) {
      if (mesh_elem_flag_test(ele, MESH_ELEM_SELECT)) {
        mesh_face_select_set(mesh, (MeshFace *)ele, true);
      }
    }
    mesh_select_mode_flush(mesh);
  }
}

/** counts number of elements with flag enabled/disabled **/
static int mesh_flag_count(Mesh *mesh,
                           const char htype,
                           const char hflag,
                           const bool respecthide,
                           const bool test_for_enabled)
{
  MeshElem *ele;
  MeshIter iter;
  int tot = 0;

  lib_assert((htype & ~MESH_ALL_NOLOOP) == 0);

  if (htype & MESH_VERT) {
    MESH_ITER (ele, &iter, mesh, MESH_VERTS_OF_MESH) {
      if (respecthide && mesh_elem_flag_test(ele, MESH_ELEM_HIDDEN)) {
        continue;
      }
      if (mesh_elem_flag_test_bool(ele, hflag) == test_for_enabled) {
        tot++;
      }
    }
  }
  if (htype & MESH_EDGE) {
    MESH_ITER (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
      if (respecthide && mesh_elem_flag_test(ele, MESH_ELEM_HIDDEN)) {
        continue;
      }
      if (elem_flag_test_bool(ele, hflag) == test_for_enabled) {
        tot++;
      }
    }
  }
  if (htype & MESH_FACE) {
    MESH_ITER (ele, &iter, mesh, MESH_FACES_OF_MESH) {
      if (respecthide && mesh_elem_flag_test(ele, MESH_ELEM_HIDDEN)) {
        continue;
      }
      if (mesh_elem_flag_test_bool(ele, hflag) == test_for_enabled) {
        tot++;
      }
    }
  }

  return tot;
}

int mesh_elem_hflag_count_enabled(Mesh *mesh,
                                  const char htype,
                                  const char hflag,
                                  const bool respecthide)
{
  return mesh_flag_count(mesh, htype, hflag, respecthide, true);
}

int mesh_elem_hflag_count_disabled(Mesh *mesh,
                                   const char htype,
                                   const char hflag,
                                   const bool respecthide)
{
  return mesh_flag_count(mesh, htype, hflag, respecthide, false);
}

void mesh_elem_select_set(Mesh *mesh, MeshElem *ele, const bool select)
{
  switch (ele->head.htype) {
    case MESH_VERT:
      mesh_vert_select_set(mesh, (MeshVert *)ele, select);
      break;
    case MESH_EDGE:
      mesh_edge_select_set(mesh, (MeshEdge *)ele, select);
      break;
    case MESH_FACE:
      mesh_face_select_set(mesh, (MeshFace *)ele, select);
      break;
    default:
      lib_assert(0);
      break;
  }
}

void mesh_active_face_set(Mesh *mesh, MeshFace *f)
{
  mesh->act_face = f;
}

MeshFace *mesh_active_face_get(Mesh *mesh, const bool is_sloppy, const bool is_selected)
{
  if (mesh->act_face && (!is_selected || mesh_elem_flag_test(mesh->act_face, MESH_ELEM_SELECT))) {
    return mesh->act_face;
  }
  if (is_sloppy) {
    MeshIter iter;
    MeshFace *f = NULL;
    MeshEditSelection *ese;

    /* Find the latest non-hidden face from the MeshEditSelection */
    ese = mesh->selected.last;
    for (; ese; ese = ese->prev) {
      if (ese->htype == MESH_FACE) {
        f = (MeshFace *)ese->ele;

        if (mesh_elem_flag_test(f, MESH_ELEM_HIDDEN)) {
          f = NULL;
        }
        else if (is_selected && !mesh_elem_flag_test(f, MESH_ELEM_SELECT)) {
          f = NULL;
        }
        else {
          break;
        }
      }
    }
    /* Last attempt: try to find any selected face */
    if (f == NULL) {
       MESH_ITER (f, &iter, mesh, MESH_FACES_OF_MESH) {
        if (mesh_elem_flag_test(f, MESH_ELEM_SELECT)) {
          break;
        }
      }
    }
    return f; /* can still be null */
  }
  return NULL;
}

MeshEdge *mesh_active_edge_get(Mesh *mesh)
{
  if (mesh->selected.last) {
    MeshEditSelection *ese = mesh->selected.last;

    if (ese && ese->htype == MESH_EDGE) {
      return (MeshEdge *)ese->ele;
    }
  }

  return NULL;
}

MeshVert *mesh_active_vert_get(Mesh *mesh)
{
  if (mesh->selected.last) {
    MeshEditSelection *ese = mesh->selected.last;

    if (ese && ese->htype == MESH_VERT) {
      return (MeshVert *)ese->ele;
    }
  }

  return NULL;
}

MeshElem *mesh_active_elem_get(Mesh *mesh)
{
  if (mesh->selected.last) {
    MeshEditSelection *ese = mesh->selected.last;

    if (ese) {
      return ese->ele;
    }
  }

  return NULL;
}

void mesh_editselection_center(MeshEditSelection *ese, float r_center[3])
{
  if (ese->htype == MESH_VERT) {
    MeshVert *eve = (MeshVert *)ese->ele;
    copy_v3_v3(r_center, eve->co);
  }
  else if (ese->htype == MESH_EDGE) {
    MedhEdge *eed = (MeshEdge *)ese->ele;
    mid_v3_v3v3(r_center, eed->v1->co, eed->v2->co);
  }
  else if (ese->htype == MESH_FACE) {
    MeshFace *efa = (MeshFace *)ese->ele;
    mesh_face_calc_center_median(efa, r_center);
  }
}

void mesh_editselection_normal(MeshEditSelection *ese, float r_normal[3])
{
  if (ese->htype == MESH_VERT) {
    MeshVert *eve = (MeshVert *)ese->ele;
    copy_v3_v3(r_normal, eve->no);
  }
  else if (ese->htype == MESH_EDGE) {
    MeshEdge *eed = (MeshEdge *)ese->ele;
    float plane[3]; /* need a plane to correct the normal */
    float vec[3];   /* temp vec storage */

    add_v3_v3v3(r_normal, eed->v1->no, eed->v2->no);
    sub_v3_v3v3(plane, eed->v2->co, eed->v1->co);

    /* the 2 vertex normals will be close but not at right angles to the edge
     * for rotate about edge we want them to be at right angles, so we need to
     * do some extra calculation to correct the vert normals,
     * we need the plane for this */
    cross_v3_v3v3(vec, r_normal, plane);
    cross_v3_v3v3(r_normal, plane, vec);
    normalize_v3(r_normal);
  }
  else if (ese->htype == MESH_FACE) {
    MeshFace *efa = (MeshFace *)ese->ele;
    copy_v3_v3(r_normal, efa->no);
  }
}

void mesh_editselection_plane(MeshEditSelection *ese, float r_plane[3])
{
  if (ese->htype == MESH_VERT) {
    MeshVert *eve = (MeshVert *)ese->ele;
    float vec[3] = {0.0f, 0.0f, 0.0f};

    if (ese->prev) { /* use previously selected data to make a useful vertex plane */
      mesh_editselection_center(ese->prev, vec);
      sub_v3_v3v3(r_plane, vec, eve->co);
    }
    else {
      /* make a fake plane that's at right-angles to the normal
       * we can't make a crossvec from a vec that's the same as the vec
       * unlikely but possible, so make sure if the normal is (0, 0, 1)
       * that vec isn't the same or in the same direction even. */
      if (eve->no[0] < 0.5f) {
        vec[0] = 1.0f;
      }
      else if (eve->no[1] < 0.5f) {
        vec[1] = 1.0f;
      }
      else {
        vec[2] = 1.0f;
      }
      cross_v3_v3v3(r_plane, eve->no, vec);
    }
    normalize_v3(r_plane);
  }
  else if (ese->htype == MESH_EDGE) {
    MeshEdge *eed = (MeshEdge *)ese->ele;

    if (mesh_edge_is_boundary(eed)) {
      sub_v3_v3v3(r_plane, eed->l->v->co, eed->l->next->v->co);
    }
    else {
      /* the plane is simple, it runs along the edge
       * however selecting different edges can swap the direction of the y axis.
       * this makes it less likely for the y axis of the gizmo
       * (running along the edge).. to flip less often.
       * at least its more predictable */
      if (eed->v2->co[1] > eed->v1->co[1]) { /* check which to do first */
        sub_v3_v3v3(r_plane, eed->v2->co, eed->v1->co);
      }
      else {
        sub_v3_v3v3(r_plane, eed->v1->co, eed->v2->co);
      }
    }

    normalize_v3(r_plane);
  }
  else if (ese->htype == MESH_FACE) {
    MeshFace *efa = (MeshFace *)ese->ele;
    mesh_face_calc_tangent_auto(efa, r_plane);
  }
}

static MeshEditSelection *mesh_select_history_create(MeshHeader *ele)
{
  MeshEditSelection *ese = (MeshEditSelection *)mem_callocn(sizeof(MeshEditSelection),
                                                        "MeshEdit Selection");
  ese->htype = ele->htype;
  ese->ele = (MeshElem *)ele;
  return ese;
}

/* --- macro wrapped funcs --- */

bool _mesh_select_history_check(Mesh *mesh, const MeshHeader *ele)
{
  return (lib_findptr(&mesh->selected, ele, offsetof(MeshEditSelection, ele)) != NULL);
}

bool _mesh_select_history_remove(Mesh *mesh, MeshHeader *ele)
{
  MeshEditSelection *ese = lib_findptr(&mesh->selected, ele, offsetof(MeshEditSelection, ele));
  if (ese) {
    lib_freelinkn(&mesh->selected, ese);
    return true;
  }
  return false;
}

void _mesh_select_history_store_notest(Mesh *mesh, MeshHeader *ele)
{
  MeshEditSelection *ese = mesh_select_history_create(ele);
  lib_addtail(&(mesh->selected), ese);
}

void _mesh_select_history_store_head_notest(Mesh *mesh, MeshHeader *ele)
{
  MeshEditSelection *ese = mesh_select_history_create(ele);
  lib_addhead(&(mesh->selected), ese);
}

void _mesh_select_history_store(Mesh *mesh, MeshHeader *ele)
{
  if (!mesh_select_history_check(mesh, (MeshElem *)ele)) {
    mesh_select_history_store_notest(mesh, (MeshElem *)ele);
  }
}

void _mesh_select_history_store_head(Mesh *mesh, MeshHeader *ele)
{
  if (!mesh_select_history_check(mesh, (MeshElem *)ele)) {
    mesh_select_history_store_head_notest(mesh, (MeshElem *)ele);
  }
}

void _mesh_select_history_store_after_notest(Mesh *mesh, MeshEditSelection *ese_ref, MeshHeader *ele)
{
  MeshEditSelection *ese = mesh_select_history_create(ele);
  lib_insertlinkafter(&(mesh->selected), ese_ref, ese);
}

void _mesh_select_history_store_after(BMesh *bm, BMEditSelection *ese_ref, BMHeader *ele)
{
  if (!mesh_select_history_check(bm, (BMElem *)ele)) {
    mesh_select_history_store_after_notest(bm, ese_ref, (BMElem *)ele);
  }
}
/* --- end macro wrapped funcs --- */

void mesh_select_history_clear(Mesh *mesh)
{
  lib_freelistn(&mesh->selected);
}

void mesh_select_history_validate(Mesh *mesh)
{
  MeshEditSelection *ese, *ese_next;

  for (ese = mesh->selected.first; ese; ese = ese_next) {
    ese_next = ese->next;
    if (!mesh_elem_flag_test(ese->ele, MESH_ELEM_SELECT)) {
      lib_freelinkn(&(mesh->selected), ese);
    }
  }
}

bool mesh_select_history_active_get(Mesh *mesh, MeshEditSelection *ese)
{
  MeshEditSelection *ese_last = mesh->selected.last;
  MeshFace *efa = mesh_active_face_get(mesh, false, true);

  ese->next = ese->prev = NULL;

  if (ese_last) {
    /* If there is an active face, use it over the last selected face. */
    if (ese_last->htype == MESH_FACE) {
      if (efa) {
        ese->ele = (MeshElem *)efa;
      }
      else {
        ese->ele = ese_last->ele;
      }
      ese->htype = MESH_FACE;
    }
    else {
      ese->ele = ese_last->ele;
      ese->htype = ese_last->htype;
    }
  }
  else if (efa) {
    /* no edit-selection, fallback to active face */
    ese->ele = (MeshElem *)efa;
    ese->htype = MESH_FACE;
  }
  else {
    ese->ele = NULL;
    return false;
  }

  return true;
}

GHash *mesh_select_history_map_create(Mesh *mesh)
{
  MeshEditSelection *ese;
  GHash *map;

  if (lib_listbase_is_empty(&mesh->selected)) {
    return NULL;
  }

  map = lib_ghash_ptr_new(__func__);

  for (ese = mesh->selected.first; ese; ese = ese->next) {
    lib_ghash_insert(map, ese->ele, ese);
  }

  return map;
}

void mesh_select_history_merge_from_targetmap(
    Mesh *mesh, GHash *vert_map, GHash *edge_map, GHash *face_map, const bool use_chain)
{

#ifdef DEBUG
  LISTBASE_FOREACH (MeshEditSelection *, ese, &mesh->selected) {
    LIB_assert(MESH_ELEM_API_FLAG_TEST(ese->ele, _FLAG_OVERLAP) == 0);
  }
#endif

  LISTBASE_FOREACH (MeshEditSelection *, ese, &mesh->selected) {
    MESH_ELEM_API_FLAG_ENABLE(ese->ele, _FLAG_OVERLAP);

    /* Only loop when (use_chain == true). */
    GHash *map = NULL;
    switch (ese->ele->head.htype) {
      case MESH_VERT:
        map = vert_map;
        break;
      case MESH_EDGE:
        map = edge_map;
        break;
      case MESH_FACE:
        map = face_map;
        break;
      default:
        MESH_ASSERT(0);
        break;
    }
    if (map != NULL) {
      MeshElem *ele_dst = ese->ele;
      while (true) {
        MeshElem *ele_dst_next = lib_ghash_lookup(map, ele_dst);
        lib_assert(ele_dst != ele_dst_next);
        if (ele_dst_next == NULL) {
          break;
        }
        ele_dst = ele_dst_next;
        /* Break loop on circular reference (should never happen). */
        if (UNLIKELY(ele_dst == ese->ele)) {
          lib_assert(0);
          break;
        }
        if (use_chain == false) {
          break;
        }
      }
      ese->ele = ele_dst;
    }
  }

  /* Remove overlapping duplicates. */
  for (MeshEditSelection *ese = mesh->selected.first, *ese_next; ese; ese = ese_next) {
    ese_next = ese->next;
    if (MESH_ELEM_API_FLAG_TEST(ese->ele, _FLAG_OVERLAP)) {
      MESH_ELEM_API_FLAG_DISABLE(ese->ele, _FLAG_OVERLAP);
    }
    else {
      lib_freelinkn(&mesh->selected, ese);
    }
  }
}

void mesh_elem_hflag_disable_test(Mesh *mesh,
                                  const char htype,
                                  const char hflag,
                                  const bool respecthide,
                                  const bool overwrite,
                                  const char hflag_test)
{
  const char iter_types[3] = {MESH_VERTS_OF_MESH, MESH_EDGES_OF_MESH, BM_FACES_OF_MESH};

  const char flag_types[3] = {MESH_VERT, MESH_EDGE, MESH_FACE};

  const char hflag_nosel = hflag & ~MESH_ELEM_SELECT;

  int i;

  lib_assert((htype & ~BM_ALL_NOLOOP) == 0);

  if (hflag & MESH_ELEM_SELECT) {
    mesh_select_history_clear(bm);
  }

  if ((htype == (MESH_VERT | MESH_EDGE | MESH_FACE)) && (hflag == MESH_ELEM_SELECT) &&
      (respecthide == false) && (hflag_test == 0)) {
    /* fast path for deselect all, avoid topology loops
     * since we know all will be de-selected anyway. */
    for (i = 0; i < 3; i++) {
      MeshIter iter;
      MeshElem *ele;

      ele = mesh_iter_new(&iter, mesh, iter_types[i], NULL);
      for (; ele; ele = mesh_iter_step(&iter)) {
        mesh_elem_flag_disable(ele, MESH_ELEM_SELECT);
      }
    }

    mesh->totvertsel = mesh->totedgesel = mesh->totfacesel = 0;
  }
  else {
    for (i = 0; i < 3; i++) {
      MeshIter iter;
      MeshElem *ele;

      if (htype & flag_types[i]) {
        ele = mesh_iter_new(&iter, mesh, iter_types[i], NULL);
        for (; ele; ele = mesh_iter_step(&iter)) {

          if (UNLIKELY(respecthide && mesh_elem_flag_test(ele, MESH_ELEM_HIDDEN))) {
            /* pass */
          }
          else if (!hflag_test || BM_elem_flag_test(ele, hflag_test)) {
            if (hflag & MESH_ELEM_SELECT) {
              mesh_elem_select_set(mesh, ele, false);
            }
            mesh_elem_flag_disable(ele, hflag);
          }
          else if (overwrite) {
            /* no match! */
            if (hflag & MESH_ELEM_SELECT) {
              mesh_elem_select_set(mesh, ele, true);
            }
            mesh_elem_flag_enable(ele, hflag_nosel);
          }
        }
      }
    }
  }
}

void mesh_elem_hflag_enable_test(Mesh *mesh,
                                    const char htype,
                                    const char hflag,
                                    const bool respecthide,
                                    const bool overwrite,
                                    const char hflag_test)
{
  const char iter_types[3] = {MESH_VERTS_OF_MESH, MESH_EDGES_OF_MESH, MESH_FACES_OF_MESH};

  const char flag_types[3] = {MESH_VERT, MESH_EDGE, MESH_FACE};

  /* use the nosel version when setting so under no
   * condition may a hidden face become selected.
   * Applying other flags to hidden faces is OK. */
  const char hflag_nosel = hflag & ~MESH_ELEM_SELECT;

  MeshIter iter;
  MeshElem *ele;
  int i;

  lib_assert((htype & ~MESH_ALL_NOLOOP) == 0);

  /* NOTE: better not attempt a fast path for selection as done with de-select
   * because hidden geometry and different selection modes can give different results,
   * we could of course check for no hidden faces and then use
   * quicker method but its not worth it. */

  for (i = 0; i < 3; i++) {
    if (htype & flag_types[i]) {
      ele = mesh_iter_new(&iter, mesh, iter_types[i], NULL);
      for (; ele; ele = mesh_iter_step(&iter)) {

        if (UNLIKELY(respecthide && mesh_elem_flag_test(ele, MESH_ELEM_HIDDEN))) {
          /* pass */
        }
        else if (!hflag_test || mesh_elem_flag_test(ele, hflag_test)) {
          /* match! */
          if (hflag & MESH_ELEM_SELECT) {
            mesh_elem_select_set(mesh, ele, true);
          }
          mesh_elem_flag_enable(ele, hflag_nosel);
        }
        else if (overwrite) {
          /* no match! */
          if (hflag & MESH_ELEM_SELECT) {
            mesh_elem_select_set(mesh, ele, false);
          }
          mesh_elem_flag_disable(ele, hflag);
        }
      }
    }
  }
}

void mesh_elem_hflag_disable_all(Mesh *mesh,
                                const char htype,
                                const char hflag,
                                const bool respecthide)
{
  /* call with 0 hflag_test */
  mesh_elem_hflag_disable_test(mesh, htype, hflag, respecthide, false, 0);
}

void mesh_elem_hflag_enable_all(Mesh *mesh,
                                const char htype,
                                const char hflag,
                                const bool respecthide)
{
  /* call with 0 hflag_test */
  mesh_elem_hflag_enable_test(mesh, htype, hflag, respecthide, false, 0);
}

/***************** Mesh Hiding stuff *********** */

/**
 * Hide unless any connected elements are visible.
 * Run this after hiding a connected edge or face.
 */
static void vert_flush_hide_set(MeshVert *v)
{
  mesh_elem_flag_set(v, MESH_ELEM_HIDDEN, !mesh_vert_is_edge_visible_any(v));
}

/**
 * Hide unless any connected elements are visible.
 * Run this after hiding a connected face.
 */
static void edge_flush_hide_set(MeshEdge *e)
{
  mesh_elem_flag_set(e, MESH_ELEM_HIDDEN, !mesh_edge_is_face_visible_any(e));
}

void mesh_vert_hide_set(MeshVert *v, const bool hide)
{
  /* vert hiding: vert + surrounding edges and faces */
  lib_assert(v->head.htype == MESH_VERT);
  if (hide) {
    lib_assert(!mesh_elem_flag_test(v, MESH_ELEM_SELECT));
  }

  mesh_elem_flag_set(v, MESH_ELEM_HIDDEN, hide);

  if (v->e) {
    MeshEdge *e_iter, *e_first;
    e_iter = e_first = v->e;
    do {
      mesh_elem_flag_set(e_iter, MESH_ELEM_HIDDEN, hide);
      if (e_iter->l) {
        const MeshLoop *l_radial_iter, *l_radial_first;
        l_radial_iter = l_radial_first = e_iter->l;
        do {
          mesh_elem_flag_set(l_radial_iter->f, MESH_ELEM_HIDDEN, hide);
        } while ((l_radial_iter = l_radial_iter->radial_next) != l_radial_first);
      }
    } while ((e_iter = mesh_disk_edge_next(e_iter, v)) != e_first);
  }
}

void mesh_edge_hide_set(MeshEdge *e, const bool hide)
{
  lib_assert(e->head.htype == MESH_EDGE);
  if (hide) {
    lib_assert(!mesh_elem_flag_test(e, MESH_ELEM_SELECT));
  }

  /* edge hiding: faces around the edge */
  if (e->l) {
    const MeshLoop *l_iter, *l_first;
    l_iter = l_first = e->l;
    do {
      mesh_elem_flag_set(l_iter->f, MESH_ELEM_HIDDEN, hide);
    } while ((l_iter = l_iter->radial_next) != l_first);
  }

  mesh_elem_flag_set(e, MESH_ELEM_HIDDEN, hide);

  /* hide vertices if necessary */
  if (hide) {
    vert_flush_hide_set(e->v1);
    vert_flush_hide_set(e->v2);
  }
  else {
    mesh_elem_flag_disable(e->v1, MESH_ELEM_HIDDEN);
    mesh_elem_flag_disable(e->v2, MESH_ELEM_HIDDEN);
  }
}

void mesh_face_hide_set(MeshFace *f, const bool hide)
{
  lib_assert(f->head.htype == MESH_FACE);
  if (hide) {
    lib_assert(!mesh_elem_flag_test(f, MESH_ELEM_SELECT));
  }

  BM_elem_flag_set(f, BM_ELEM_HIDDEN, hide);

  if (hide) {
    BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
    BMLoop *l_iter;

    l_iter = l_first;
    do {
      edge_flush_hide_set(l_iter->e);
    } while ((l_iter = l_iter->next) != l_first);

    l_iter = l_first;
    do {
      vert_flush_hide_set(l_iter->v);
    } while ((l_iter = l_iter->next) != l_first);
  }
  else {
    BMLoop *l_first = BM_FACE_FIRST_LOOP(f);
    BMLoop *l_iter;

    l_iter = l_first;
    do {
      BM_elem_flag_disable(l_iter->e, BM_ELEM_HIDDEN);
      BM_elem_flag_disable(l_iter->v, BM_ELEM_HIDDEN);
    } while ((l_iter = l_iter->next) != l_first);
  }
}

void _bm_elem_hide_set(BMesh *bm, BMHeader *head, const bool hide)
{
  /* Follow convention of always deselecting before
   * hiding an element */
  switch (head->htype) {
    case BM_VERT:
      if (hide) {
        BM_vert_select_set(bm, (BMVert *)head, false);
      }
      BM_vert_hide_set((BMVert *)head, hide);
      break;
    case BM_EDGE:
      if (hide) {
        BM_edge_select_set(bm, (BMEdge *)head, false);
      }
      BM_edge_hide_set((BMEdge *)head, hide);
      break;
    case BM_FACE:
      if (hide) {
        BM_face_select_set(bm, (BMFace *)head, false);
      }
      BM_face_hide_set((BMFace *)head, hide);
      break;
    default:
      BMESH_ASSERT(0);
      break;
  }
}
