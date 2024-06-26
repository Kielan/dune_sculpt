/**
 * This file contains code for dealing
 * with polygons (normal/area calculation, tessellation, etc)
 */

#include "types_listBase.h"
#include "types_meshdata.h"
#include "types_modifier.h"

#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_heap.h"
#include "lib_linklist.h"
#include "lib_math.h"
#include "lib_memarena.h"
#include "lib_polyfill_2d.h"
#include "lib_polyfill_2d_beautify.h"

#include "mesh.h"
#include "mesh_tools.h"

#include "dune_customdata.h"

#include "intern/mesh_private.h"

/**
 * COMPUTE POLY NORMAL (MeshFace)
 *
 * Same as normal_poly_v3 but operates directly on a mesh face.
 */
static float mesh_face_calc_poly_normal(const MeshFace *f, float n[3])
{
  MeshLoop *l_first = MESH_FACE_FIRST_LOOP(f);
  MeshLoop *l_iter = l_first;
  const float *v_prev = l_first->prev->v->co;
  const float *v_curr = l_first->v->co;

  zero_v3(n);

  /* Newell's Method */
  do {
    add_newell_cross_v3_v3v3(n, v_prev, v_curr);

    l_iter = l_iter->next;
    v_prev = v_curr;
    v_curr = l_iter->v->co;

  } while (l_iter != l_first);

  return normalize_v3(n);
}

/**
 * COMPUTE POLY NORMAL MeshFace
 *
 * Same as mesh_face_calc_poly_normal
 * but takes an array of vertex locations.
 */
static float mesh_face_calc_poly_normal_vertex_cos(const MeshFace *f,
                                                 float r_no[3],
                                                 float const (*vertexCos)[3])
{
  MeshLoop *l_first = MESH_FACE_FIRST_LOOP(f);
  MeshLoop *l_iter = l_first;
  const float *v_prev = vertexCos[mesh_elem_index_get(l_first->prev->v)];
  const float *v_curr = vertexCos[mesh_elem_index_get(l_first->v)];

  zero_v3(r_no);

  /* Newell's Method */
  do {
    add_newell_cross_v3_v3v3(r_no, v_prev, v_curr);

    l_iter = l_iter->next;
    v_prev = v_curr;
    v_curr = vertexCos[mesh_elem_index_get(l_iter->v)];
  } while (l_iter != l_first);

  return normalize_v3(r_no);
}

/** COMPUTE POLY CENTER MeshFace **/
static void mesh_face_calc_poly_center_median_vertex_cos(const MeshFace *f,
                                                       float r_cent[3],
                                                       float const (*vertexCos)[3])
{
  const MeshLoop *l_first, *l_iter;

  zero_v3(r_cent);

  /* Newell's Method */
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    add_v3_v3(r_cent, vertexCos[mesh_elem_index_get(l_iter->v)]);
  } while ((l_iter = l_iter->next) != l_first);
  mul_v3_fl(r_cent, 1.0f / f->len);
}

void mesh_face_calc_tessellation(const MeshFace *f,
                               const bool use_fixed_quad,
                               MeshLoop **r_loops,
                               uint (*r_index)[3])
{
  MeshLoop *l_first = MESH_FACE_FIRST_LOOP(f);
  MeshLoop *l_iter;

  if (f->len == 3) {
    *r_loops++ = (l_iter = l_first);
    *r_loops++ = (l_iter = l_iter->next);
    *r_loops++ = (l_iter->next);

    r_index[0][0] = 0;
    r_index[0][1] = 1;
    r_index[0][2] = 2;
  }
  else if (f->len == 4 && use_fixed_quad) {
    *r_loops++ = (l_iter = l_first);
    *r_loops++ = (l_iter = l_iter->next);
    *r_loops++ = (l_iter = l_iter->next);
    *r_loops++ = (l_iter->next);

    r_index[0][0] = 0;
    r_index[0][1] = 1;
    r_index[0][2] = 2;

    r_index[1][0] = 0;
    r_index[1][1] = 2;
    r_index[1][2] = 3;
  }
  else {
    float axis_mat[3][3];
    float(*projverts)[2] = lib_array_alloca(projverts, f->len);
    int j;

    axis_dominant_v3_to_m3_negate(axis_mat, f->no);

    j = 0;
    l_iter = l_first;
    do {
      mul_v2_m3v3(projverts[j], axis_mat, l_iter->v->co);
      r_loops[j] = l_iter;
      j++;
    } while ((l_iter = l_iter->next) != l_first);

    /* complete the loop */
    lib_polyfill_calc(projverts, f->len, 1, r_index);
  }
}

void mesh_face_calc_point_in_face(const MeshFace *f, float r_co[3])
{
  const MeshLoop *l_tri[3];

  if (f->len == 3) {
    const MeshLoop *l = MESH_FACE_FIRST_LOOP(f);
    ARRAY_SET_ITEMS(l_tri, l, l->next, l->prev);
  }
  else {
    /* tessellation here seems overkill when in many cases this will be the center,
     * but without this we can't be sure the point is inside a concave face. */
    const int tottri = f->len - 2;
    MeshLoop **loops = lib_array_alloca(loops, f->len);
    uint(*index)[3] = lib_array_alloca(index, tottri);
    int j;
    int j_best = 0; /* use as fallback when unset */
    float area_best = -1.0f;

    mesh_face_calc_tessellation(f, false, loops, index);

    for (j = 0; j < tottri; j++) {
      const float *p1 = loops[index[j][0]]->v->co;
      const float *p2 = loops[index[j][1]]->v->co;
      const float *p3 = loops[index[j][2]]->v->co;
      const float area = area_squared_tri_v3(p1, p2, p3);
      if (area > area_best) {
        j_best = j;
        area_best = area;
      }
    }

    ARRAY_SET_ITEMS(
        l_tri, loops[index[j_best][0]], loops[index[j_best][1]], loops[index[j_best][2]]);
  }

  mid_v3_v3v3v3(r_co, l_tri[0]->v->co, l_tri[1]->v->co, l_tri[2]->v->co);
}

float mesh_face_calc_area(const MeshFace *f)
{
  /* inline 'area_poly_v3' logic, avoid creating a temp array */
  const MeshLoop *l_iter, *l_first;
  float n[3];

  zero_v3(n);
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    add_newell_cross_v3_v3v3(n, l_iter->v->co, l_iter->next->v->co);
  } while ((l_iter = l_iter->next) != l_first);
  return len_v3(n) * 0.5f;
}

float mesh_face_calc_area_with_mat3(const MeshFace *f, const float mat3[3][3])
{
  /* inline 'area_poly_v3' logic, avoid creating a temp array */
  const MeshLoop *l_iter, *l_first;
  float co[3];
  float n[3];

  zero_v3(n);
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  mul_v3_m3v3(co, mat3, l_iter->v->co);
  do {
    float co_next[3];
    mul_v3_m3v3(co_next, mat3, l_iter->next->v->co);
    add_newell_cross_v3_v3v3(n, co, co_next);
    copy_v3_v3(co, co_next);
  } while ((l_iter = l_iter->next) != l_first);
  return len_v3(n) * 0.5f;
}

float mesh_face_calc_area_uv(const MeshFace *f, int cd_loop_uv_offset)
{
  /* inline 'area_poly_v2' logic, avoid creating a temp array */
  const MeshLoop *l_iter, *l_first;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  /* The Trapezium Area Rule */
  float cross = 0.0f;
  do {
    const MLoopUV *luv = MESH_ELEM_CD_GET_VOID_P(l_iter, cd_loop_uv_offset);
    const MLoopUV *luv_next = MESH_ELEM_CD_GET_VOID_P(l_iter->next, cd_loop_uv_offset);
    cross += (luv_next->uv[0] - luv->uv[0]) * (luv_next->uv[1] + luv->uv[1]);
  } while ((l_iter = l_iter->next) != l_first);
  return fabsf(cross * 0.5f);
}

float mesh_face_calc_perimeter(const MeshFace *f)
{
  const MeshLoop *l_iter, *l_first;
  float perimeter = 0.0f;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    perimeter += len_v3v3(l_iter->v->co, l_iter->next->v->co);
  } while ((l_iter = l_iter->next) != l_first);

  return perimeter;
}

float mesh_face_calc_perimeter_with_mat3(const MeshFace *f, const float mat3[3][3])
{
  const MeshLoop *l_iter, *l_first;
  float co[3];
  float perimeter = 0.0f;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  mul_v3_m3v3(co, mat3, l_iter->v->co);
  do {
    float co_next[3];
    mul_v3_m3v3(co_next, mat3, l_iter->next->v->co);
    perimeter += len_v3v3(co, co_next);
    copy_v3_v3(co, co_next);
  } while ((l_iter = l_iter->next) != l_first);

  return perimeter;
}

/**
 * Utility function to calculate the edge which is most different from the other two.
 *
 * return The first edge index, where the second vertex is `(index + 1) % 3`.
 */
static int mesh_vert_tri_find_unique_edge(MeshVert *verts[3])
{
  /* find the most 'unique' loop, (greatest difference to others) */
#if 1
  /* optimized version that avoids sqrt */
  float difs[3];
  for (int i_prev = 1, i_curr = 2, i_next = 0; i_next < 3; i_prev = i_curr, i_curr = i_next++) {
    const float *co = verts[i_curr]->co;
    const float *co_other[2] = {verts[i_prev]->co, verts[i_next]->co};
    float proj_dir[3];
    mid_v3_v3v3(proj_dir, co_other[0], co_other[1]);
    sub_v3_v3(proj_dir, co);

    float proj_pair[2][3];
    project_v3_v3v3(proj_pair[0], co_other[0], proj_dir);
    project_v3_v3v3(proj_pair[1], co_other[1], proj_dir);
    difs[i_next] = len_squared_v3v3(proj_pair[0], proj_pair[1]);
  }
#else
  const float lens[3] = {
      len_v3v3(verts[0]->co, verts[1]->co),
      len_v3v3(verts[1]->co, verts[2]->co),
      len_v3v3(verts[2]->co, verts[0]->co),
  };
  const float difs[3] = {
      fabsf(lens[1] - lens[2]),
      fabsf(lens[2] - lens[0]),
      fabsf(lens[0] - lens[1]),
  };
#endif

  int order[3] = {0, 1, 2};
  axis_sort_v3(difs, order);

  return order[0];
}

void mesh_vert_tri_calc_tangent_edge(MeshVert *verts[3], float r_tangent[3])
{
  const int index = mesh_vert_tri_find_unique_edge(verts);

  sub_v3_v3v3(r_tangent, verts[index]->co, verts[(index + 1) % 3]->co);

  normalize_v3(r_tangent);
}

void mesh_vert_tri_calc_tangent_edge_pair(MeshVert *verts[3], float r_tangent[3])
{
  const int index = mesh_vert_tri_find_unique_edge(verts);

  const float *v_a = verts[index]->co;
  const float *v_b = verts[(index + 1) % 3]->co;
  const float *v_other = verts[(index + 2) % 3]->co;

  mid_v3_v3v3(r_tangent, v_a, v_b);
  sub_v3_v3v3(r_tangent, v_other, r_tangent);

  normalize_v3(r_tangent);
}

void mesh_face_calc_tangent_edge(const MeshFace *f, float r_tangent[3])
{
  const MeshLoop *l_long = mesh_face_find_longest_loop((MeshFace *)f);

  sub_v3_v3v3(r_tangent, l_long->v->co, l_long->next->v->co);

  normalize_v3(r_tangent);
}

void mesh_face_calc_tangent_edge_pair(const MeshFace *f, float r_tangent[3])
{
  if (f->len == 3) {
    MeshVert *verts[3];

    mesh_face_as_array_vert_tri((MeshFace *)f, verts);

    mesh_vert_tri_calc_tangent_edge_pair(verts, r_tangent);
  }
  else if (f->len == 4) {
    /* Use longest edge pair */
    MeshVert *verts[4];
    float vec[3], vec_a[3], vec_b[3];

    mesh_face_as_array_vert_quad((MeshFace *)f, verts);

    sub_v3_v3v3(vec_a, verts[3]->co, verts[2]->co);
    sub_v3_v3v3(vec_b, verts[0]->co, verts[1]->co);
    add_v3_v3v3(r_tangent, vec_a, vec_b);

    sub_v3_v3v3(vec_a, verts[0]->co, verts[3]->co);
    sub_v3_v3v3(vec_b, verts[1]->co, verts[2]->co);
    add_v3_v3v3(vec, vec_a, vec_b);
    /* use the longest edge length */
    if (len_squared_v3(r_tangent) < len_squared_v3(vec)) {
      copy_v3_v3(r_tangent, vec);
    }
  }
  else {
    /* For ngons use two longest disconnected edges */
    MeshLoop *l_long = mesh_face_find_longest_loop((MeshFace *)f);
    MeshLoop *l_long_other = NULL;

    float len_max_sq = 0.0f;
    float vec_a[3], vec_b[3];

    MeshLoop *l_iter = l_long->prev->prev;
    MeshLoop *l_last = l_long->next;

    do {
      const float len_sq = len_squared_v3v3(l_iter->v->co, l_iter->next->v->co);
      if (len_sq >= len_max_sq) {
        l_long_other = l_iter;
        len_max_sq = len_sq;
      }
    } while ((l_iter = l_iter->prev) != l_last);

    sub_v3_v3v3(vec_a, l_long->next->v->co, l_long->v->co);
    sub_v3_v3v3(vec_b, l_long_other->v->co, l_long_other->next->v->co);
    add_v3_v3v3(r_tangent, vec_a, vec_b);

    /* Edges may not be opposite side of the ngon,
     * this could cause problems for ngons with multiple-aligned edges of the same length.
     * Fallback to longest edge. */
    if (UNLIKELY(normalize_v3(r_tangent) == 0.0f)) {
      normalize_v3_v3(r_tangent, vec_a);
    }
  }
}

void mesh_face_calc_tangent_edge_diagonal(const MeshFace *f, float r_tangent[3])
{
  MeshLoop *l_iter, *l_first;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);

  /* In case of degenerate faces. */
  zero_v3(r_tangent);

  /* warning: O(n^2) loop here, take care! */
  float dist_max_sq = 0.0f;
  do {
    MeshLoop *l_iter_other = l_iter->next;
    MeshLoop *l_iter_last = l_iter->prev;
    do {
      lib_assert(!ELEM(l_iter->v->co, l_iter_other->v->co, l_iter_other->next->v->co));
      float co_other[3], vec[3];
      closest_to_line_segment_v3(
          co_other, l_iter->v->co, l_iter_other->v->co, l_iter_other->next->v->co);
      sub_v3_v3v3(vec, l_iter->v->co, co_other);

      const float dist_sq = len_squared_v3(vec);
      if (dist_sq > dist_max_sq) {
        dist_max_sq = dist_sq;
        copy_v3_v3(r_tangent, vec);
      }
    } while ((l_iter_other = l_iter_other->next) != l_iter_last);
  } while ((l_iter = l_iter->next) != l_first);

  normalize_v3(r_tangent);
}

void mesh_face_calc_tangent_vert_diagonal(const MeshFace *f, float r_tangent[3])
{
  MeshLoop *l_iter, *l_first;

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);

  /* In case of degenerate faces. */
  zero_v3(r_tangent);

  /* warning: O(n^2) loop here, take care! */
  float dist_max_sq = 0.0f;
  do {
    MeshLoop *l_iter_other = l_iter->next;
    do {
      float vec[3];
      sub_v3_v3v3(vec, l_iter->v->co, l_iter_other->v->co);

      const float dist_sq = len_squared_v3(vec);
      if (dist_sq > dist_max_sq) {
        dist_max_sq = dist_sq;
        copy_v3_v3(r_tangent, vec);
      }
    } while ((l_iter_other = l_iter_other->next) != l_iter);
  } while ((l_iter = l_iter->next) != l_first);

  normalize_v3(r_tangent);
}

void mesh_face_calc_tangent_auto(const MeshFace *f, float r_tangent[3])
{
  if (f->len == 3) {
    /* most 'unique' edge of a triangle */
    MeshVert *verts[3];
    mesh_face_as_array_vert_tri((MeshFace *)f, verts);
    mesh_vert_tri_calc_tangent_edge(verts, r_tangent);
  }
  else if (f->len == 4) {
    /* longest edge pair of a quad */
    mesh_face_calc_tangent_edge_pair((MeshFace *)f, r_tangent);
  }
  else {
    /* longest edge of an ngon */
    mesh_face_calc_tangent_edge((MeshFace *)f, r_tangent);
  }
}

void mesh_face_calc_bounds_expand(const MeshFace *f, float min[3], float max[3])
{
  const MeshLoop *l_iter, *l_first;
  l_iter = l_first = MEH_FACE_FIRST_LOOP(f);
  do {
    minmax_v3v3_v3(min, max, l_iter->v->co);
  } while ((l_iter = l_iter->next) != l_first);
}

void mesh_face_calc_center_bounds(const MeshFace *f, float r_cent[3])
{
  const MeshLoop *l_iter, *l_first;
  float min[3], max[3];

  INIT_MINMAX(min, max);

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    minmax_v3v3_v3(min, max, l_iter->v->co);
  } while ((l_iter = l_iter->next) != l_first);

  mid_v3_v3v3(r_cent, min, max);
}

void mesh_face_calc_center_bounds_vcos(const Mesh *mesh,
                                       const MeshFace *f,
                                       float r_cent[3],
                                       float const (*vertexCos)[3])
{
  /* must have valid index data */
  lib_assert((mesh->elem_index_dirty & MESH_VERT) == 0);
  (void)mesh;

  const MeshLoop *l_iter, *l_first;
  float min[3], max[3];

  INIT_MINMAX(min, max);

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    minmax_v3v3_v3(min, max, vertexCos[mesh_elem_index_get(l_iter->v)]);
  } while ((l_iter = l_iter->next) != l_first);

  mid_v3_v3v3(r_cent, min, max);
}
void mesh_face_calc_center_median(const MeshFace *f, float r_cent[3])
{
  const MeshLoop *l_iter, *l_first;

  zero_v3(r_cent);

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    add_v3_v3(r_cent, l_iter->v->co);
  } while ((l_iter = l_iter->next) != l_first);
  mul_v3_fl(r_cent, 1.0f / (float)f->len);
}

void mesh_face_calc_center_median_weighted(const MeshFace *f, float r_cent[3])
{
  const MeshLoop *l_iter;
  const MeshLoop *l_first;
  float totw = 0.0f;
  float w_prev;

  zero_v3(r_cent);

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  w_prev = mesh_edge_calc_length(l_iter->prev->e);
  do {
    const float w_curr = mesh_edge_calc_length(l_iter->e);
    const float w = (w_curr + w_prev);
    madd_v3_v3fl(r_cent, l_iter->v->co, w);
    totw += w;
    w_prev = w_curr;
  } while ((l_iter = l_iter->next) != l_first);

  if (totw != 0.0f) {
    mul_v3_fl(r_cent, 1.0f / (float)totw);
  }
}

void poly_rotate_plane(const float normal[3], float (*verts)[3], const uint nverts)
{
  float mat[3][3];
  float co[3];
  uint i;

  co[2] = 0.0f;

  axis_dominant_v3_to_m3(mat, normal);
  for (i = 0; i < nverts; i++) {
    mul_v2_m3v3(co, mat, verts[i]);
    copy_v3_v3(verts[i], co);
  }
}

void mesh_edge_normals_update(MeshEdge *e)
{
  MeshIter iter;
  MeshFace *f;

  MESH_ELEM_ITER (f, &iter, e, MESH_FACES_OF_EDGE) {
    mesh_face_normal_update(f);
  }

  mesh_vert_normal_update(e->v1);
  mesh_vert_normal_update(e->v2);
}

static void mesh_loop_normal_accum(const MeshLoop *l, float no[3])
{
  float vec1[3], vec2[3], fac;

  /* Same calculation used in mesh_mesh_normals_update */
  sub_v3_v3v3(vec1, l->v->co, l->prev->v->co);
  sub_v3_v3v3(vec2, l->next->v->co, l->v->co);
  normalize_v3(vec1);
  normalize_v3(vec2);

  fac = saacos(-dot_v3v3(vec1, vec2));

  madd_v3_v3fl(no, l->f->no, fac);
}

bool mesh_vert_calc_normal_ex(const MeshVert *v, const char hflag, float r_no[3])
{
  int len = 0;

  zero_v3(r_no);

  if (v->e) {
    const MeshEdge *e = v->e;
    do {
      if (e->l) {
        const MeshLoop *l = e->l;
        do {
          if (l->v == v) {
            if (mesh_elem_flag_test(l->f, hflag)) {
              mesh_loop_normal_accum(l, r_no);
              len++;
            }
          }
        } while ((l = l->radial_next) != e->l);
      }
    } while ((e = mesh_disk_edge_next(e, v)) != v->e);
  }

  if (len) {
    normalize_v3(r_no);
    return true;
  }
  return false;
}

bool mesh_vert_calc_normal(const MeshVert *v, float r_no[3])
{
  int len = 0;

  zero_v3(r_no);

  if (v->e) {
    const MeshEdge *e = v->e;
    do {
      if (e->l) {
        const MeshLoop *l = e->l;
        do {
          if (l->v == v) {
            mesh_loop_normal_accum(l, r_no);
            len++;
          }
        } while ((l = l->radial_next) != e->l);
      }
    } while ((e = mesh_disk_edge_next(e, v)) != v->e);
  }

  if (len) {
    normalize_v3(r_no);
    return true;
  }
  return false;
}

void mesh_vert_normal_update_all(MeshVert *v)
{
  int len = 0;

  zero_v3(v->no);

  if (v->e) {
    const BMEdge *e = v->e;
    do {
      if (e->l) {
        const BMLoop *l = e->l;
        do {
          if (l->v == v) {
            BM_face_normal_update(l->f);
            bm_loop_normal_accum(l, v->no);
            len++;
          }
        } while ((l = l->radial_next) != e->l);
      }
    } while ((e = bmesh_disk_edge_next(e, v)) != v->e);
  }

  if (len) {
    normalize_v3(v->no);
  }
}

void mesh_vert_normal_update(MeshVert *v)
{
  mesh_vert_calc_normal(v, v->no);
}

float mesh_face_calc_normal(const MeshFace *f, float r_no[3])
{
  MeshLoop *l;

  /* common cases first */
  switch (f->len) {
    case 4: {
      const float *co1 = (l = MESH_FACE_FIRST_LOOP(f))->v->co;
      const float *co2 = (l = l->next)->v->co;
      const float *co3 = (l = l->next)->v->co;
      const float *co4 = (l->next)->v->co;

      return normal_quad_v3(r_no, co1, co2, co3, co4);
    }
    case 3: {
      const float *co1 = (l = MESH_FACE_FIRST_LOOP(f))->v->co;
      const float *co2 = (l = l->next)->v->co;
      const float *co3 = (l->next)->v->co;

      return normal_tri_v3(r_no, co1, co2, co3);
    }
    default: {
      return mesh_face_calc_poly_normal(f, r_no);
    }
  }
}
void mesh_face_normal_update(MeshFace *f)
{
  mesh_face_calc_normal(f, f->no);
}

float mesh_face_calc_normal_vcos(const Mesh *mesh,
                                 const MeshFace *f,
                                 float r_no[3],
                                 float const (*vertexCos)[3])
{
  MeshLoop *l;

  /* must have valid index data */
  lib_assert((mesh->elem_index_dirty & MESH_VERT) == 0);
  (void)mesh;

  /* common cases first */
  switch (f->len) {
    case 4: {
      const float *co1 = vertexCos[mesh_elem_index_get((l = MESH_FACE_FIRST_LOOP(f))->v)];
      const float *co2 = vertexCos[mesh_elem_index_get((l = l->next)->v)];
      const float *co3 = vertexCos[mesh_elem_index_get((l = l->next)->v)];
      const float *co4 = vertexCos[mesh_elem_index_get((l->next)->v)];

      return normal_quad_v3(r_no, co1, co2, co3, co4);
    }
    case 3: {
      const float *co1 = vertexCos[mesh_elem_index_get((l = MESH_FACE_FIRST_LOOP(f))->v)];
      const float *co2 = vertexCos[mesh_elem_index_get((l = l->next)->v)];
      const float *co3 = vertexCos[mesh_elem_index_get((l->next)->v)];

      return normal_tri_v3(r_no, co1, co2, co3);
    }
    default: {
      return mesh_face_calc_poly_normal_vertex_cos(f, r_no, vertexCos);
    }
  }
}

void mesh_verts_calc_normal_from_cloud_ex(
    MeshVert **varr, int varr_len, float r_normal[3], float r_center[3], int *r_index_tangent)
{
  const float varr_len_inv = 1.0f / (float)varr_len;

  /* Get the center point and collect vector array since we loop over these a lot. */
  float center[3] = {0.0f, 0.0f, 0.0f};
  for (int i = 0; i < varr_len; i++) {
    madd_v3_v3fl(center, varr[i]->co, varr_len_inv);
  }

  /* Find the 'co_a' point from center. */
  int co_a_index = 0;
  const float *co_a = NULL;
  {
    float dist_sq_max = -1.0f;
    for (int i = 0; i < varr_len; i++) {
      const float dist_sq_test = len_squared_v3v3(varr[i]->co, center);
      if (!(dist_sq_test <= dist_sq_max)) {
        co_a = varr[i]->co;
        co_a_index = i;
        dist_sq_max = dist_sq_test;
      }
    }
  }

  float dir_a[3];
  sub_v3_v3v3(dir_a, co_a, center);
  normalize_v3(dir_a);

  const float *co_b = NULL;
  float dir_b[3] = {0.0f, 0.0f, 0.0f};
  {
    float dist_sq_max = -1.0f;
    for (int i = 0; i < varr_len; i++) {
      if (varr[i]->co == co_a) {
        continue;
      }
      float dir_test[3];
      sub_v3_v3v3(dir_test, varr[i]->co, center);
      project_plane_normalized_v3_v3v3(dir_test, dir_test, dir_a);
      const float dist_sq_test = len_squared_v3(dir_test);
      if (!(dist_sq_test <= dist_sq_max)) {
        co_b = varr[i]->co;
        dist_sq_max = dist_sq_test;
        copy_v3_v3(dir_b, dir_test);
      }
    }
  }

  if (varr_len <= 3) {
    normal_tri_v3(r_normal, center, co_a, co_b);
    goto finally;
  }

  normalize_v3(dir_b);

  const float *co_a_opposite = NULL;
  const float *co_b_opposite = NULL;

  {
    float dot_a_min = FLT_MAX;
    float dot_b_min = FLT_MAX;
    for (int i = 0; i < varr_len; i++) {
      const float *co_test = varr[i]->co;
      float dot_test;

      if (co_test != co_a) {
        dot_test = dot_v3v3(dir_a, co_test);
        if (dot_test < dot_a_min) {
          dot_a_min = dot_test;
          co_a_opposite = co_test;
        }
      }

      if (co_test != co_b) {
        dot_test = dot_v3v3(dir_b, co_test);
        if (dot_test < dot_b_min) {
          dot_b_min = dot_test;
          co_b_opposite = co_test;
        }
      }
    }
  }

  normal_quad_v3(r_normal, co_a, co_b, co_a_opposite, co_b_opposite);

finally:
  if (r_center != NULL) {
    copy_v3_v3(r_center, center);
  }
  if (r_index_tangent != NULL) {
    *r_index_tangent = co_a_index;
  }
}

void mesh_verts_calc_normal_from_cloud(MeshVert **varr, int varr_len, float r_normal[3])
{
  mesh_verts_calc_normal_from_cloud_ex(varr, varr_len, r_normal, NULL, NULL);
}

float mesh_face_calc_normal_subset(const MeshLoop *l_first, const MeshLoop *l_last, float r_no[3])
{
  const float *v_prev, *v_curr;

  /* Newell's Method */
  const MeshLoop *l_iter = l_first;
  const MeshLoop *l_term = l_last->next;

  zero_v3(r_no);

  v_prev = l_last->v->co;
  do {
    v_curr = l_iter->v->co;
    add_newell_cross_v3_v3v3(r_no, v_prev, v_curr);
    v_prev = v_curr;
  } while ((l_iter = l_iter->next) != l_term);

  return normalize_v3(r_no);
}

void mesh_face_calc_center_median_vcos(const Mesh *mesh,
                                       const MeshFace *f,
                                       float r_cent[3],
                                       float const (*vertexCos)[3])
{
  /* must have valid index data */
  lib_assert((mesh->elem_index_dirty & MESH_VERT) == 0);
  (void)mesh;

  mesh_face_calc_poly_center_median_vertex_cos(f, r_cent, vertexCos);
}

void mesh_face_normal_flip_ex(Mesh *mesh,
                              MeshFace *f,
                              const int cd_loop_mdisp_offset,
                              const bool use_loop_mdisp_flip)
{
  mesh_kernel_loop_reverse(mesh, f, cd_loop_mdisp_offset, use_loop_mdisp_flip);
  negate_v3(f->no);
}

void mesh_face_normal_flip(Mesh *mesh, MeshFace *f)
{
  const int cd_loop_mdisp_offset = CustomData_get_offset(&mesh->ldata, CD_MDISPS);
  mesh_face_normal_flip_ex(mesh, f, cd_loop_mdisp_offset, true);
}

bool mesh_face_point_inside_test(const MeshFace *f, const float co[3])
{
  float axis_mat[3][3];
  float(*projverts)[2] = lib_array_alloca(projverts, f->len);

  float co_2d[2];
  MeshLoop *l_iter;
  int i;

  lib_assert(mesh_face_is_normal_valid(f));

  axis_dominant_v3_to_m3(axis_mat, f->no);

  mul_v2_m3v3(co_2d, axis_mat, co);

  for (i = 0, l_iter = MESH_FACE_FIRST_LOOP(f); i < f->len; i++, l_iter = l_iter->next) {
    mul_v2_m3v3(projverts[i], axis_mat, l_iter->v->co);
  }

  return isect_point_poly_v2(co_2d, projverts, f->len, false);
}

void mesh_face_triangulate(Mesh *mesh,
                           MeshFace *f,
                           MeshFace **r_faces_new,
                           int *r_faces_new_tot,
                           MeshEdge **r_edges_new,
                           int *r_edges_new_tot,
                           LinkNode **r_faces_double,
                           const int quad_method,
                           const int ngon_method,
                           const bool use_tag,
                           /* use for ngons only! */
                           MemArena *pf_arena,

                           /* use for MOD_TRIANGULATE_NGON_BEAUTY only! */
                           struct Heap *pf_heap)
{
  const int cd_loop_mdisp_offset = CustomData_get_offset(&bm->ldata, CD_MDISPS);
  const bool use_beauty = (ngon_method == MOD_TRIANGULATE_NGON_BEAUTY);
  MeshLoop *l_first, *l_new;
  MeshFace *f_new;
  int nf_i = 0;
  int ne_i = 0;

  lib_assert(mesh_face_is_normal_valid(f));

  /* ensure both are valid or NULL */
  lib_assert((r_faces_new == NULL) == (r_faces_new_tot == NULL));

  lib_assert(f->len > 3);

  {
    MeshLoop **loops = lib_array_alloca(loops, f->len);
    uint(*tris)[3] = lib_array_alloca(tris, f->len);
    const int totfilltri = f->len - 2;
    const int last_tri = f->len - 3;
    int i;
    /* for mdisps */
    float f_center[3];

    if (f->len == 4) {
      /* even though we're not using lib_polyfill, fill in 'tris' and 'loops'
       * so we can share code to handle face creation afterwards. */
      MeshLoop *l_v1, *l_v2;

      l_first = MESH_FACE_FIRST_LOOP(f);

      switch (quad_method) {
        case MOD_TRIANGULATE_QUAD_FIXED: {
          l_v1 = l_first;
          l_v2 = l_first->next->next;
          break;
        }
        case MOD_TRIANGULATE_QUAD_ALTERNATE: {
          l_v1 = l_first->next;
          l_v2 = l_first->prev;
          break;
        }
        case MOD_TRIANGULATE_QUAD_SHORTEDGE:
        case MOD_TRIANGULATE_QUAD_LONGEDGE:
        case MOD_TRIANGULATE_QUAD_BEAUTY:
        default: {
          MeshLoop *l_v3, *l_v4;
          bool split_24;

          l_v1 = l_first->next;
          l_v2 = l_first->next->next;
          l_v3 = l_first->prev;
          l_v4 = l_first;

          if (quad_method == MOD_TRIANGULATE_QUAD_SHORTEDGE) {
            float d1, d2;
            d1 = len_squared_v3v3(l_v4->v->co, l_v2->v->co);
            d2 = len_squared_v3v3(l_v1->v->co, l_v3->v->co);
            split_24 = ((d2 - d1) > 0.0f);
          }
          else if (quad_method == MOD_TRIANGULATE_QUAD_LONGEDGE) {
            float d1, d2;
            d1 = len_squared_v3v3(l_v4->v->co, l_v2->v->co);
            d2 = len_squared_v3v3(l_v1->v->co, l_v3->v->co);
            split_24 = ((d2 - d1) < 0.0f);
          }
          else {
            /* first check if the quad is concave on either diagonal */
            const int flip_flag = is_quad_flip_v3(
                l_v1->v->co, l_v2->v->co, l_v3->v->co, l_v4->v->co);
            if (UNLIKELY(flip_flag & (1 << 0))) {
              split_24 = true;
            }
            else if (UNLIKELY(flip_flag & (1 << 1))) {
              split_24 = false;
            }
            else {
              split_24 = (mesh_verts_calc_rotate_beauty(l_v1->v, l_v2->v, l_v3->v, l_v4->v, 0, 0) >
                          0.0f);
            }
          }

          /* named confusingly, l_v1 is in fact the second vertex */
          if (split_24) {
            l_v1 = l_v4;
            // l_v2 = l_v2;
          }
          else {
            // l_v1 = l_v1;
            l_v2 = l_v3;
          }
          break;
        }
      }

      loops[0] = l_v1;
      loops[1] = l_v1->next;
      loops[2] = l_v2;
      loops[3] = l_v2->next;

      ARRAY_SET_ITEMS(tris[0], 0, 1, 2);
      ARRAY_SET_ITEMS(tris[1], 0, 2, 3);
    }
    else {
      MeshLoop *l_iter;
      float axis_mat[3][3];
      float(*projverts)[2] = lib_array_alloca(projverts, f->len);

      axis_dominant_v3_to_m3_negate(axis_mat, f->no);

      for (i = 0, l_iter = MESH_FACE_FIRST_LOOP(f); i < f->len; i++, l_iter = l_iter->next) {
        loops[i] = l_iter;
        mul_v2_m3v3(projverts[i], axis_mat, l_iter->v->co);
      }

      lib_polyfill_calc_arena(projverts, f->len, 1, tris, pf_arena);

      if (use_beauty) {
        lib_polyfill_beautify(projverts, f->len, tris, pf_arena, pf_heap);
      }

      lib_memarena_clear(pf_arena);
    }

    if (cd_loop_mdisp_offset != -1) {
      mesh_face_calc_center_median(f, f_center);
    }

    /* loop over calculated triangles and create new geometry */
    for (i = 0; i < totfilltri; i++) {
      MeshLoop *l_tri[3] = {loops[tris[i][0]], loops[tris[i][1]], loops[tris[i][2]]};

      MeshVert *v_tri[3] = {l_tri[0]->v, l_tri[1]->v, l_tri[2]->v};

      f_new = mesh_face_create_verts(mesh, v_tri, 3, f, MESH_CREATE_NOP, true);
      l_new = MESH_FACE_FIRST_LOOP(f_new);

      lib_assert(v_tri[0] == l_new->v);

      /* check for duplicate */
      if (l_new->radial_next != l_new) {
        MeshLoop *l_iter = l_new->radial_next;
        do {
          if (UNLIKELY((l_iter->f->len == 3) && (l_new->prev->v == l_iter->prev->v))) {
            /* Check the last tri because we swap last f_new with f at the end... */
            lib_linklist_prepend(r_faces_double, (i != last_tri) ? f_new : f);
            break;
          }
        } while ((l_iter = l_iter->radial_next) != l_new);
      }

      /* copy CD data */
      mesh_elem_attrs_copy(mesh, mesh, l_tri[0], l_new);
      mesh_elem_attrs_copy(mesh, mesh, l_tri[1], l_new->next);
      mesh_elem_attrs_copy(mesh, mesh, l_tri[2], l_new->prev);

      /* add all but the last face which is swapped and removed (below) */
      if (i != last_tri) {
        if (use_tag) {
          mesh_elem_flag_enable(f_new, MESH_ELEM_TAG);
        }
        if (r_faces_new) {
          r_faces_new[nf_i++] = f_new;
        }
      }

      if (use_tag || r_edges_new) {
        /* new faces loops */
        MeshLoop *l_iter;

        l_iter = l_first = l_new;
        do {
          MeshEdge *e = l_iter->e;
          /* Confusing! if its not a boundary now, we know it will be later since this will be an
           * edge of one of the new faces which we're in the middle of creating. */
          bool is_new_edge = (l_iter == l_iter->radial_next);

          if (is_new_edge) {
            if (use_tag) {
              mesh_elem_flag_enable(e, MESH_ELEM_TAG);
            }
            if (r_edges_new) {
              r_edges_new[ne_i++] = e;
            }
          }
          /* NOTE: never disable tag's. */
        } while ((l_iter = l_iter->next) != l_first);
      }

      if (cd_loop_mdisp_offset != -1) {
        float f_new_center[3];
        mesh_face_calc_center_median(f_new, f_new_center);
        mesh_face_interp_multires_ex(mesh, f_new, f, f_new_center, f_center, cd_loop_mdisp_offset);
      }
    }

    {
      /* we can't delete the real face, because some of the callers expect it to remain valid.
       * so swap data and delete the last created tri */
      mesh_face_swap_data(f, f_new);
      mesh_face_kill(mesh, f_new);
    }
  }
  mesh->elem_index_dirty |= MESH_FACE;

  if (r_faces_new_tot) {
    *r_faces_new_tot = nf_i;
  }

  if (r_edges_new_tot) {
    *r_edges_new_tot = ne_i;
  }
}

void mesh_face_splits_check_legal(Mesh *mesh, MeshFace *f, MeshLoop *(*loops)[2], int len)
{
  float out[2] = {-FLT_MAX, -FLT_MAX};
  float center[2] = {0.0f, 0.0f};
  float axis_mat[3][3];
  float(*projverts)[2] = lib_array_alloca(projverts, f->len);
  const float *(*edgeverts)[2] = lib_array_alloca(edgeverts, len);
  MeshLoop *l;
  int i, i_prev, j;

  lib_assert(mesh_face_is_normal_valid(f));

  axis_dominant_v3_to_m3(axis_mat, f->no);

  for (i = 0, l = MESH_FACE_FIRST_LOOP(f); i < f->len; i++, l = l->next) {
    mul_v2_m3v3(projverts[i], axis_mat, l->v->co);
    add_v2_v2(center, projverts[i]);
  }

  /* first test for completely convex face */
  if (is_poly_convex_v2(projverts, f->len)) {
    return;
  }

  mul_v2_fl(center, 1.0f / f->len);

  for (i = 0, l = MESH_FACE_FIRST_LOOP(f); i < f->len; i++, l = l->next) {
    mesh_elem_index_set(l, i); /* set_dirty */

    /* center the projection for maximum accuracy */
    sub_v2_v2(projverts[i], center);

    out[0] = max_ff(out[0], projverts[i][0]);
    out[1] = max_ff(out[1], projverts[i][1]);
  }
  mesh->elem_index_dirty |= MESH_LOOP;

  /* ensure we are well outside the face bounds (value is arbitrary) */
  add_v2_fl(out, 1.0f);

  for (i = 0; i < len; i++) {
    edgeverts[i][0] = projverts[mesh_elem_index_get(loops[i][0])];
    edgeverts[i][1] = projverts[mesh_elem_index_get(loops[i][1])];
  }

  /* do convexity test */
  for (i = 0; i < len; i++) {
    float mid[2];
    mid_v2_v2v2(mid, edgeverts[i][0], edgeverts[i][1]);

    int isect = 0;
    int j_prev;
    for (j = 0, j_prev = f->len - 1; j < f->len; j_prev = j++) {
      const float *f_edge[2] = {projverts[j_prev], projverts[j]};
      if (isect_seg_seg_v2(UNPACK2(f_edge), mid, out) == ISECT_LINE_LINE_CROSS) {
        isect++;
      }
    }

    if (isect % 2 == 0) {
      loops[i][0] = NULL;
    }
  }

#define EDGE_SHARE_VERT(e1, e2) \
  ((ELEM((e1)[0], (e2)[0], (e2)[1])) || (ELEM((e1)[1], (e2)[0], (e2)[1])))

  /* do line crossing tests */
  for (i = 0, i_prev = f->len - 1; i < f->len; i_prev = i++) {
    const float *f_edge[2] = {projverts[i_prev], projverts[i]};
    for (j = 0; j < len; j++) {
      if ((loops[j][0] != NULL) && !EDGE_SHARE_VERT(f_edge, edgeverts[j])) {
        if (isect_seg_seg_v2(UNPACK2(f_edge), UNPACK2(edgeverts[j])) == ISECT_LINE_LINE_CROSS) {
          loops[j][0] = NULL;
        }
      }
    }
  }

  /* self intersect tests */
  for (i = 0; i < len; i++) {
    if (loops[i][0]) {
      for (j = i + 1; j < len; j++) {
        if ((loops[j][0] != NULL) && !EDGE_SHARE_VERT(edgeverts[i], edgeverts[j])) {
          if (isect_seg_seg_v2(UNPACK2(edgeverts[i]), UNPACK2(edgeverts[j])) ==
              ISECT_LINE_LINE_CROSS) {
            loops[i][0] = NULL;
            break;
          }
        }
      }
    }
  }

#undef EDGE_SHARE_VERT
}

void mesh_face_splits_check_optimal(MeshFace *f, MeshLoop *(*loops)[2], int len)
{
  int i;

  for (i = 0; i < len; i++) {
    MeshLoop *l_a_dummy, *l_b_dummy;
    if (f != mesh_vert_pair_share_face_by_angle(
                 loops[i][0]->v, loops[i][1]->v, &l_a_dummy, &l_b_dummy, false)) {
      loops[i][0] = NULL;
    }
  }
}

void mesh_face_as_array_vert_tri(MeshFace *f, MeshVert *r_verts[3])
{
  MeshLoop *l = MESH_FACE_FIRST_LOOP(f);

  lib_assert(f->len == 3);

  r_verts[0] = l->v;
  l = l->next;
  r_verts[1] = l->v;
  l = l->next;
  r_verts[2] = l->v;
}

void mesh_face_as_array_vert_quad(MeshFace *f, MeshVert *r_verts[4])
{
  MeshLoop *l = MESH_FACE_FIRST_LOOP(f);

  BLI_assert(f->len == 4);

  r_verts[0] = l->v;
  l = l->next;
  r_verts[1] = l->v;
  l = l->next;
  r_verts[2] = l->v;
  l = l->next;
  r_verts[3] = l->v;
}

void mesh_face_as_array_loop_tri(MeshFace *f, MeshLoop *r_loops[3])
{
  MeshLoop *l = MESH_FACE_FIRST_LOOP(f);

  lib_assert(f->len == 3);

  r_loops[0] = l;
  l = l->next;
  r_loops[1] = l;
  l = l->next;
  r_loops[2] = l;
}

void mesh_face_as_array_loop_quad(MeshFace *f, MeshLoop *r_loops[4])
{
  MeshLoop *l = MESH_FACE_FIRST_LOOP(f);

  lib_assert(f->len == 4);

  r_loops[0] = l;
  l = l->next;
  r_loops[1] = l;
  l = l->next;
  r_loops[2] = l;
  l = l->next;
  r_loops[3] = l;
}
