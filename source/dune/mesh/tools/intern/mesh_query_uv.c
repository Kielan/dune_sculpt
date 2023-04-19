#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_linklist.h"
#include "lib_math.h"
#include "lib_utildefines_stack.h"

#include "dune_customdata.h"

#include "types_meshdata.h"

#include "mesh.h"
#include "intern/mesh_private.h"

static void uv_aspect(const MeshLoop *l,
                      const float aspect[2],
                      const int cd_loop_uv_offset,
                      float r_uv[2])
{
  const float *uv = ((const MeshLoopUV *)MESH_ELEM_CD_GET_VOID_P(l, cd_loop_uv_offset))->uv;
  r_uv[0] = uv[0] * aspect[0];
  r_uv[1] = uv[1] * aspect[1];
}

/**
 * Typically we avoid hiding arguments,
 * make this an exception since it reads poorly with so many repeated arguments.
 */
#define UV_ASPECT(l, r_uv) uv_aspect(l, aspect, cd_loop_uv_offset, r_uv)

void mesh_face_uv_calc_center_median_weighted(const MeshFace *f,
                                              const float aspect[2],
                                              const int cd_loop_uv_offset,
                                              float r_cent[2])
{
  const MeshLoop *l_iter;
  const MeshLoop *l_first;
  float totw = 0.0f;
  float w_prev;

  zero_v2(r_cent);

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);

  float uv_prev[2], uv_curr[2];
  UV_ASPECT(l_iter->prev, uv_prev);
  UV_ASPECT(l_iter, uv_curr);
  w_prev = len_v2v2(uv_prev, uv_curr);
  do {
    float uv_next[2];
    UV_ASPECT(l_iter->next, uv_next);
    const float w_curr = len_v2v2(uv_curr, uv_next);
    const float w = (w_curr + w_prev);
    madd_v2_v2fl(r_cent, uv_curr, w);
    totw += w;
    w_prev = w_curr;
    copy_v2_v2(uv_curr, uv_next);
  } while ((l_iter = l_iter->next) != l_first);

  if (totw != 0.0f) {
    mul_v2_fl(r_cent, 1.0f / (float)totw);
  }
  /* Reverse aspect. */
  r_cent[0] /= aspect[0];
  r_cent[1] /= aspect[1];
}

#undef UV_ASPECT

void mesh_face_uv_calc_center_median(const MeshFace *f, const int cd_loop_uv_offset, float r_cent[2])
{
  const MeshLoop *l_iter;
  const MeshLoop *l_first;
  zero_v2(r_cent);
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    const MLoopUV *luv = MESH_ELEM_CD_GET_VOID_P(l_iter, cd_loop_uv_offset);
    add_v2_v2(r_cent, luv->uv);
  } while ((l_iter = l_iter->next) != l_first);

  mul_v2_fl(r_cent, 1.0f / (float)f->len);
}

float mesh_face_uv_calc_cross(const MeshFace *f, const int cd_loop_uv_offset)
{
  float(*uvs)[2] = lib_array_alloca(uvs, f->len);
  const MeshLoop *l_iter;
  const MeshLoop *l_first;
  int i = 0;
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    const MLoopUV *luv = MESH_ELEM_CD_GET_VOID_P(l_iter, cd_loop_uv_offset);
    copy_v2_v2(uvs[i++], luv->uv);
  } while ((l_iter = l_iter->next) != l_first);
  return cross_poly_v2(uvs, f->len);
}

void mesh_face_uv_minmax(const MeshFace *f, float min[2], float max[2], const int cd_loop_uv_offset)
{
  const MeshLoop *l_iter;
  const MeshLoop *l_first;
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    const MLoopUV *luv = MESH_ELEM_CD_GET_VOID_P(l_iter, cd_loop_uv_offset);
    minmax_v2v2_v2(min, max, luv->uv);
  } while ((l_iter = l_iter->next) != l_first);
}

void mesh_face_uv_transform(MeshFace *f, const float matrix[2][2], const int cd_loop_uv_offset)
{
  BMLoop *l_iter;
  BMLoop *l_first;
  l_iter = l_first = BM_FACE_FIRST_LOOP(f);
  do {
    MLoopUV *luv = BM_ELEM_CD_GET_VOID_P(l_iter, cd_loop_uv_offset);
    mul_m2_v2(matrix, luv->uv);
  } while ((l_iter = l_iter->next) != l_first);
}

bool BM_loop_uv_share_edge_check(BMLoop *l_a, BMLoop *l_b, const int cd_loop_uv_offset)
{
  BLI_assert(l_a->e == l_b->e);
  MLoopUV *luv_a_curr = BM_ELEM_CD_GET_VOID_P(l_a, cd_loop_uv_offset);
  MLoopUV *luv_a_next = BM_ELEM_CD_GET_VOID_P(l_a->next, cd_loop_uv_offset);
  MLoopUV *luv_b_curr = BM_ELEM_CD_GET_VOID_P(l_b, cd_loop_uv_offset);
  MLoopUV *luv_b_next = BM_ELEM_CD_GET_VOID_P(l_b->next, cd_loop_uv_offset);
  if (l_a->v != l_b->v) {
    SWAP(MLoopUV *, luv_b_curr, luv_b_next);
  }
  return (equals_v2v2(luv_a_curr->uv, luv_b_curr->uv) &&
          equals_v2v2(luv_a_next->uv, luv_b_next->uv));
}

bool BM_loop_uv_share_vert_check(BMLoop *l_a, BMLoop *l_b, const int cd_loop_uv_offset)
{
  BLI_assert(l_a->v == l_b->v);
  const MLoopUV *luv_a = BM_ELEM_CD_GET_VOID_P(l_a, cd_loop_uv_offset);
  const MLoopUV *luv_b = BM_ELEM_CD_GET_VOID_P(l_b, cd_loop_uv_offset);
  if (!equals_v2v2(luv_a->uv, luv_b->uv)) {
    return false;
  }
  return true;
}

bool mesh_edge_uv_share_vert_check(MeshEdge *e, MeshLoop *l_a, MeshLoop *l_b, const int cd_loop_uv_offset)
{
  lib_assert(l_a->v == l_b->v);
  if (!mesh_loop_uv_share_vert_check(l_a, l_b, cd_loop_uv_offset)) {
    return false;
  }

  /* No need for NULL checks, these will always succeed. */
  const MeshLoop *l_other_a = mesh_loop_other_vert_loop_by_edge(l_a, e);
  const MeshLoop *l_other_b = mesh_loop_other_vert_loop_by_edge(l_b, e);

  {
    const MeshLoopUV *luv_other_a = MESH_ELEM_CD_GET_VOID_P(l_other_a, cd_loop_uv_offset);
    const MeshLoopUV *luv_other_b = MESH_ELEM_CD_GET_VOID_P(l_other_b, cd_loop_uv_offset);
    if (!equals_v2v2(luv_other_a->uv, luv_other_b->uv)) {
      return false;
    }
  }

  return true;
}

bool mesh_face_uv_point_inside_test(const MeshFace *f, const float co[2], const int cd_loop_uv_offset)
{
  float(*projverts)[2] = lib_array_alloca(projverts, f->len);

  MeshLoop *l_iter;
  int i;

  lib_assert(mesh_face_is_normal_valid(f));

  for (i = 0, l_iter = MESH_FACE_FIRST_LOOP(f); i < f->len; i++, l_iter = l_iter->next) {
    copy_v2_v2(projverts[i], MESH_ELEM_CD_GET_VOID_P(l_iter, cd_loop_uv_offset));
  }

  return isect_point_poly_v2(co, projverts, f->len, false);
}
