/**
 * This file contains code for polygon tessellation
 * (creating triangles from polygons).
 *
 * see mesh_tessellate.c for the Mesh equivalent of this file.
 */

#include "types_meshdata.h"

#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_heap.h"
#include "lib_linklist.h"
#include "lib_math.h"
#include "lib_memarena.h"
#include "lib_polyfill_2d.h"
#include "lib_polyfill_2d_beautify.h"
#include "lib_task.h"

#include "mesh.h"
#include "mesh_tools.h"

/**
 * On systems with 32+ cores,
 * only a very small number of faces has any advantage single threading (in the 100's).
 * Note that between 500-2000 quads, the difference isn't so much
 * (tessellation isn't a bottleneck in this case anyway).
 * Avoid the slight overhead of using threads in this case.
 */
#define MESH_FACE_TESSELLATE_THREADED_LIMIT 1024

/* -------------------------------------------------------------------- */
/** Default Mesh Tessellation **/

/**
 * param face_normal: This will be optimized out as a constant.
 */
LIB_INLINE void mesh_calc_tessellation_for_face_impl(MeshLoop *(*looptris)[3],
                                                     MeshFace *efa,
                                                     MemArena **pf_arena_p,
                                                     const bool face_normal)
{
#ifdef DEBUG
  /* The face normal is used for projecting faces into 2D space for tessellation.
   * Invalid normals may result in invalid tessellation.
   * Either `face_normal` should be true or normals should be updated first. */
  LIB_assert(face_normal || mesh_face_is_normal_valid(efa));
#endif

  switch (efa->len) {
    case 3: {
      /* `0 1 2` -> `0 1 2` */
      MeshLoop *l;
      MeshLoop **l_ptr = looptris[0];
      l_ptr[0] = l = MESH_FACE_FIRST_LOOP(efa);
      l_ptr[1] = l = l->next;
      l_ptr[2] = l->next;
      if (face_normal) {
        normal_tri_v3(efa->no, l_ptr[0]->v->co, l_ptr[1]->v->co, l_ptr[2]->v->co);
      }
      break;
    }
    case 4: {
      /* `0 1 2 3` -> (`0 1 2`, `0 2 3`) */
      MeshLoop *l;
      MeshLoop **l_ptr_a = looptris[0];
      MeshLoop **l_ptr_b = looptris[1];
      (l_ptr_a[0] = l_ptr_b[0] = l = MESH_FACE_FIRST_LOOP(efa));
      (l_ptr_a[1] = l = l->next);
      (l_ptr_a[2] = l_ptr_b[1] = l = l->next);
      (l_ptr_b[2] = l->next);

      if (face_normal) {
        normal_quad_v3(
            efa->no, l_ptr_a[0]->v->co, l_ptr_a[1]->v->co, l_ptr_a[2]->v->co, l_ptr_b[2]->v->co);
      }

      if (UNLIKELY(is_quad_flip_v3_first_third_fast_with_normal(l_ptr_a[0]->v->co,
                                                                l_ptr_a[1]->v->co,
                                                                l_ptr_a[2]->v->co,
                                                                l_ptr_b[2]->v->co,
                                                                efa->no))) {
        /* Flip out of degenerate 0-2 state. */
        l_ptr_a[2] = l_ptr_b[2];
        l_ptr_b[0] = l_ptr_a[1];
      }
      break;
    }
    default: {
      if (face_normal) {
        mesh_face_calc_normal(efa, efa->no);
      }

      MeshLoop *l_iter, *l_first;
      MeshLoop **l_arr;

      float axis_mat[3][3];
      float(*projverts)[2];
      uint(*tris)[3];

      const int tris_len = efa->len - 2;

      MemArena *pf_arena = *pf_arena_p;
      if (UNLIKELY(pf_arena == NULL)) {
        pf_arena = *pf_arena_p = lib_memarena_new(LIB_MEMARENA_STD_BUFSIZE, __func__);
      }

      tris = lib_memarena_alloc(pf_arena, sizeof(*tris) * tris_len);
      l_arr = lib_memarena_alloc(pf_arena, sizeof(*l_arr) * efa->len);
      projverts = lib_memarena_alloc(pf_arena, sizeof(*projverts) * efa->len);

      axis_dominant_v3_to_m3_negate(axis_mat, efa->no);

      int i = 0;
      l_iter = l_first = MESH_FACE_FIRST_LOOP(efa);
      do {
        l_arr[i] = l_iter;
        mul_v2_m3v3(projverts[i], axis_mat, l_iter->v->co);
        i++;
      } while ((l_iter = l_iter->next) != l_first);

      lib_polyfill_calc_arena(projverts, efa->len, 1, tris, pf_arena);

      for (i = 0; i < tris_len; i++) {
        MeshLoop **l_ptr = looptris[i];
        uint *tri = tris[i];

        l_ptr[0] = l_arr[tri[0]];
        l_ptr[1] = l_arr[tri[1]];
        l_ptr[2] = l_arr[tri[2]];
      }

      lib_memarena_clear(pf_arena);
      break;
    }
  }
}

static void mesh_calc_tessellation_for_face(MeshLoop *(*looptris)[3],
                                            MeshFace *efa,
                                            MemArena **pf_arena_p)
{
  mesh_calc_tessellation_for_face_impl(looptris, efa, pf_arena_p, false);
}

static void mesh_calc_tessellation_for_face_with_normal(MeshLoop *(*looptris)[3],
                                                         MeshFace *efa,
                                                         MemArena **pf_arena_p)
{
  mesh_calc_tessellation_for_face_impl(looptris, efa, pf_arena_p, true);
}

/**
 * brief mesh_calc_tessellation get the looptris and its number from a certain bmesh
 * param looptris:
 *
 * looptris Must be pre-allocated to at least the size of given by: poly_to_tri_count
 */
static void mesh_calc_tessellation__single_threaded(Mesh *bm,
                                                    MeshLoop *(*looptris)[3],
                                                    const char face_normals)
{
#ifndef NDEBUG
  const int looptris_tot = poly_to_tri_count(mesh->totface, mesh->totloop);
#endif

  MeshIter iter;
  MeshFace *efa;
  int i = 0;

  MemArena *pf_arena = NULL;

  if (face_normals) {
    MESH_ITER (efa, &iter, mesh, MESH_FACES_OF_MESH) {
      lib_assert(efa->len >= 3);
      mesh_face_calc_normal(efa, efa->no);
      mesh_calc_tessellation_for_face_with_normal(looptris + i, efa, &pf_arena);
      i += efa->len - 2;
    }
  }
  else {
    MESH_ITER (efa, &iter, bm, BM_FACES_OF_MESH) {
      lib_assert(efa->len >= 3);
      mesh_calc_tessellation_for_face(looptris + i, efa, &pf_arena);
      i += efa->len - 2;
    }
  }

  if (pf_arena) {
    lib_memarena_free(pf_arena);
    pf_arena = NULL;
  }

  lib_assert(i <= looptris_tot);
}

struct TessellationUserTLS {
  MemArena *pf_arena;
};

static void mesh_calc_tessellation_for_face_fn(void *__restrict userdata,
                                               MempoolIterData *mp_f,
                                               const TaskParallelTLS *__restrict tls)
{
  struct TessellationUserTLS *tls_data = tls->userdata_chunk;
  MeshLoop *(*looptris)[3] = userdata;
  MeshFace *f = (MeshFace *)mp_f;
  MeshLoop *l = MESH_FACE_FIRST_LOOP(f);
  const int offset = mesh_elem_index_get(l) - (mesh_elem_index_get(f) * 2);
  mesh_calc_tessellation_for_face(looptris + offset, f, &tls_data->pf_arena);
}

static void mesh_calc_tessellation_for_face_with_normals_fn(void *__restrict userdata,
                                                            MempoolIterData *mp_f,
                                                            const TaskParallelTLS *__restrict tls)
{
  struct TessellationUserTLS *tls_data = tls->userdata_chunk;
  MeshLoop *(*looptris)[3] = userdata;
  MeshFace *f = (MeshFace *)mp_f;
  MeshLoop *l = MESH_FACE_FIRST_LOOP(f);
  const int offset = mesh_elem_index_get(l) - (mesh_elem_index_get(f) * 2);
  mesh_calc_tessellation_for_face_with_normal(looptris + offset, f, &tls_data->pf_arena);
}

static void mesh_calc_tessellation_for_face_free_fn(const void *__restrict UNUSED(userdata),
                                                     void *__restrict tls_v)
{
  struct TessellationUserTLS *tls_data = tls_v;
  if (tls_data->pf_arena) {
    lib_memarena_free(tls_data->pf_arena);
  }
}

static void mesh_calc_tessellation__multi_threaded(Mesh *mesh,
                                                   MeshLoop *(*looptris)[3],
                                                   const char face_normals)
{
  mesh_elem_index_ensure(mesh, MESH_LOOP | MESH_FACE);

  TaskParallelSettings settings;
  struct TessellationUserTLS tls_dummy = {NULL};
  lib_parallel_mempool_settings_defaults(&settings);
  settings.userdata_chunk = &tls_dummy;
  settings.userdata_chunk_size = sizeof(tls_dummy);
  settings.func_free = mesh_calc_tessellation_for_face_free_fn;
  mesh_iter_parallel(mesh,
                   MESH_FACES_OF_MESH,
                   face_normals ? mesh_calc_tessellation_for_face_with_normals_fn :
                                  mesh_calc_tessellation_for_face_fn,
                   looptris,
                   &settings);
}

void mesh_calc_tessellation_ex(Mesh *mesh,
                               MeshLoop *(*looptris)[3],
                               const struct MeshCalcTessellationParams *params)
{
  if (mesh->totface < MESH_FACE_TESSELLATE_THREADED_LIMIT) {
    mesh_calc_tessellation__single_threaded(mesh, looptris, params->face_normals);
  }
  else {
    mesh_calc_tessellation__multi_threaded(mesh, looptris, params->face_normals);
  }
}

void mesh_calc_tessellation(Mesh *mesh, MeshLoop *(*looptris)[3])
{
  mesh_calc_tessellation_ex(mesh,
                            looptris,
                            &(const struct MeshCalcTessellationParams){
                                .face_normals = false,
                            });
}

/* -------------------------------------------------------------------- */
/** Default Tessellation (Partial Updates) **/

struct PartialTessellationUserData {
  MeshFace **faces;
  MeshLoop *(*looptris)[3];
};

struct PartialTessellationUserTLS {
  MemArena *pf_arena;
};

static void mesh_calc_tessellation_for_face_partial_fn(void *__restrict userdata,
                                                       const int index,
                                                       const TaskParallelTLS *__restrict tls)
{
  struct PartialTessellationUserTLS *tls_data = tls->userdata_chunk;
  struct PartialTessellationUserData *data = userdata;
  MeshFace *f = data->faces[index];
  MeshLoop *l = MESH_FACE_FIRST_LOOP(f);
  const int offset = mesh_elem_index_get(l) - (mesh_elem_index_get(f) * 2);
  mesh_calc_tessellation_for_face(data->looptris + offset, f, &tls_data->pf_arena);
}

static void mesh_calc_tessellation_for_face_partial_with_normals_fn(
    void *__restrict userdata, const int index, const TaskParallelTLS *__restrict tls)
{
  struct PartialTessellationUserTLS *tls_data = tls->userdata_chunk;
  struct PartialTessellationUserData *data = userdata;
  MeshFace *f = data->faces[index];
  MeshLoop *l = MESH_FACE_FIRST_LOOP(f);
  const int offset = mesh_elem_index_get(l) - (mesh_elem_index_get(f) * 2);
  mesh_calc_tessellation_for_face_with_normal(data->looptris + offset, f, &tls_data->pf_arena);
}

static void bmesh_calc_tessellation_for_face_partial_free_fn(
    const void *__restrict UNUSED(userdata), void *__restrict tls_v)
{
  struct PartialTessellationUserTLS *tls_data = tls_v;
  if (tls_data->pf_arena) {
    lib_memarena_free(tls_data->pf_arena);
  }
}

static void mesh_calc_tessellation_with_partial__multi_threaded(
    MeshLoop *(*looptris)[3],
    const MeshPartialUpdate *bmpinfo,
    const struct MeshCalcTessellationParams *params)
{
  const int faces_len = bmpinfo->faces_len;
  MeshFace **faces = bmpinfo->faces;

  struct PartialTessellationUserData data = {
      .faces = faces,
      .looptris = looptris,
  };
  struct PartialTessellationUserTLS tls_dummy = {NULL};
  TaskParallelSettings settings;
  lib_parallel_range_settings_defaults(&settings);
  settings.use_threading = true;
  settings.userdata_chunk = &tls_dummy;
  settings.userdata_chunk_size = sizeof(tls_dummy);
  settings.func_free = mesh_calc_tessellation_for_face_partial_free_fn;

  lib_task_parallel_range(0,
                          faces_len,
                          &data,
                          params->face_normals ?
                          mesh_calc_tessellation_for_face_partial_with_normals_fn :
                          mesh_calc_tessellation_for_face_partial_fn,
                          &settings);
}

static void mesh_calc_tessellation_with_partial__single_threaded(
    MeshLoop *(*looptris)[3],
    const MeshPartialUpdate *bmpinfo,
    const struct MeshCalcTessellation_Params *params)
{
  const int faces_len = bmpinfo->faces_len;
  MeshFace **faces = bmpinfo->faces;

  MemArena *pf_arena = NULL;

  if (params->face_normals) {
    for (int index = 0; index < faces_len; index++) {
      MeshFace *f = faces[index];
      MeshLoop *l = MESH_FACE_FIRST_LOOP(f);
      const int offset = mesh_elem_index_get(l) - (mesh_elem_index_get(f) * 2);
      mesh_calc_tessellation_for_face_with_normal(looptris + offset, f, &pf_arena);
    }
  }
  else {
    for (int index = 0; index < faces_len; index++) {
      MeshFace *f = faces[index];
      MeshLoop *l = MESH_FACE_FIRST_LOOP(f);
      const int offset = mesh_elem_index_get(l) - (mesh_elem_index_get(f) * 2);
      mesh_calc_tessellation_for_face(looptris + offset, f, &pf_arena);
    }
  }

  if (pf_arena) {
    lib_memarena_free(pf_arena);
  }
}

void mesh_calc_tessellation_with_partial_ex(Mesh *mesh,
                                            MeshLoop *(*looptris)[3],
                                            const MeshPartialUpdate *bmpinfo,
                                            const struct MeshCalcTessellationParams *params)
{
  lib_assert(bmpinfo->params.do_tessellate);
  /* While harmless, exit early if there is nothing to do (avoids ensuring the index). */
  if (UNLIKELY(bmpinfo->faces_len == 0)) {
    return;
  }

  mesh_elem_index_ensure(mesh, MESH_LOOP | MESH_FACE);

  if (bmpinfo->faces_len < MESH_FACE_TESSELLATE_THREADED_LIMIT) {
    mesh_calc_tessellation_with_partial__single_threaded(looptris, bmpinfo, params);
  }
  else {
    mesh_calc_tessellation_with_partial__multi_threaded(looptris, bmpinfo, params);
  }
}

void mesh_calc_tessellation_with_partial(Mesh *mesh,
                                         MeshLoop *(*looptris)[3],
                                         const MeshPartialUpdate *mpinfo)
{
  mesh_calc_tessellation_with_partial_ex(mesh, looptris, bmpinfo, false);
}

/* -------------------------------------------------------------------- */
/** Beauty Mesh Tessellation. Avoid degenerate triangles. **/

static int mesh_calc_tessellation_for_face_beauty(BMLoop *(*looptris)[3],
                                                   BMFace *efa,
                                                   MemArena **pf_arena_p,
                                                   Heap **pf_heap_p)
{
  switch (efa->len) {
    case 3: {
      MeshLoop *l;
      MeshLoop **l_ptr = looptris[0];
      l_ptr[0] = l = MESH_FACE_FIRST_LOOP(efa);
      l_ptr[1] = l = l->next;
      l_ptr[2] = l->next;
      return 1;
    }
    case 4: {
      MeshLoop *l_v1 = MESH_FACE_FIRST_LOOP(efa);
      MeshLoop *l_v2 = l_v1->next;
      MeshLoop *l_v3 = l_v2->next;
      MeshLoop *l_v4 = l_v1->prev;

      /* mesh_verts_calc_rotate_beauty performs excessive checks we don't need!
       * It's meant for rotating edges, it also calculates a new normal.
       *
       * Use lib_polyfill_beautify_quad_rotate_calc since we have the normal.
       */
#if 0
      const bool split_13 = (mesh_verts_calc_rotate_beauty(
                                 l_v1->v, l_v2->v, l_v3->v, l_v4->v, 0, 0) < 0.0f);
#else
      float axis_mat[3][3], v_quad[4][2];
      axis_dominant_v3_to_m3(axis_mat, efa->no);
      mul_v2_m3v3(v_quad[0], axis_mat, l_v1->v->co);
      mul_v2_m3v3(v_quad[1], axis_mat, l_v2->v->co);
      mul_v2_m3v3(v_quad[2], axis_mat, l_v3->v->co);
      mul_v2_m3v3(v_quad[3], axis_mat, l_v4->v->co);

      const bool split_13 = lib_polyfill_beautify_quad_rotate_calc(
                                v_quad[0], v_quad[1], v_quad[2], v_quad[3]) < 0.0f;
#endif

      MeshLoop **l_ptr_a = looptris[0];
      MeshLoop **l_ptr_b = looptris[1];
      if (split_13) {
        l_ptr_a[0] = l_v1;
        l_ptr_a[1] = l_v2;
        l_ptr_a[2] = l_v3;

        l_ptr_b[0] = l_v1;
        l_ptr_b[1] = l_v3;
        l_ptr_b[2] = l_v4;
      }
      else {
        l_ptr_a[0] = l_v1;
        l_ptr_a[1] = l_v2;
        l_ptr_a[2] = l_v4;

        l_ptr_b[0] = l_v2;
        l_ptr_b[1] = l_v3;
        l_ptr_b[2] = l_v4;
      }
      return 2;
    }
    default: {
      MemArena *pf_arena = *pf_arena_p;
      Heap *pf_heap = *pf_heap_p;
      if (UNLIKELY(pf_arena == NULL)) {
        pf_arena = *pf_arena_p = lib_memarena_new(LIB_MEMARENA_STD_BUFSIZE, __func__);
        pf_heap = *pf_heap_p = lib_heap_new_ex(LIB_POLYFILL_ALLOC_NGON_RESERVE);
      }

      MeshLoop *l_iter, *l_first;
      MeshLoop **l_arr;

      float axis_mat[3][3];
      float(*projverts)[2];
      uint(*tris)[3];

      const int tris_len = efa->len - 2;

      tris = lib_memarena_alloc(pf_arena, sizeof(*tris) * tris_len);
      l_arr = lib_memarena_alloc(pf_arena, sizeof(*l_arr) * efa->len);
      projverts = lib_memarena_alloc(pf_arena, sizeof(*projverts) * efa->len);

      axis_dominant_v3_to_m3_negate(axis_mat, efa->no);

      int i = 0;
      l_iter = l_first = BM_FACE_FIRST_LOOP(efa);
      do {
        l_arr[i] = l_iter;
        mul_v2_m3v3(projverts[i], axis_mat, l_iter->v->co);
        i++;
      } while ((l_iter = l_iter->next) != l_first);

      lib_polyfill_calc_arena(projverts, efa->len, 1, tris, pf_arena);

      lib_polyfill_beautify(projverts, efa->len, tris, pf_arena, pf_heap);

      for (i = 0; i < tris_len; i++) {
        MeshLoop **l_ptr = looptris[i];
        uint *tri = tris[i];

        l_ptr[0] = l_arr[tri[0]];
        l_ptr[1] = l_arr[tri[1]];
        l_ptr[2] = l_arr[tri[2]];
      }

      lib_memarena_clear(pf_arena);

      return tris_len;
    }
  }
}

void mesh_calc_tessellation_beauty(Mesh *mesh, MeshLoop *(*looptris)[3])
{
#ifndef NDEBUG
  const int looptris_tot = poly_to_tri_count(mesh->totface, mesh->totloop);
#endif

  MeshIter iter;
  MeshFace *efa;
  int i = 0;

  MemArena *pf_arena = NULL;

  /* use_beauty */
  Heap *pf_heap = NULL;

  MESH_ITER (efa, &iter, mesh, MESH_FACES) {
    lib_assert(efa->len >= 3);
    i += mesh_calc_tessellation_for_face_beauty(looptris + i, efa, &pf_arena, &pf_heap);
  }

  if (pf_arena) {
    lib_memarena_free(pf_arena);

    lib_heap_free(pf_heap, NULL);
  }

  lib_assert(i <= looptris_tot);
}
