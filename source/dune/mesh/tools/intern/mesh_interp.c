/** Functions for interpolating data across the surface of a mesh. */

#include "mem_guardedalloc.h"

#include "types_meshdata.h"

#include "lib_alloca.h"
#include "lib_linklist.h"
#include "lib_math.h"
#include "lib_memarena.h"
#include "lib_task.h"

#include "dune_customdata.h"
#include "dune_multires.h"

#include "mesh.h"
#include "intern/mesh_private.h"

/* edge and vertex share, currently there's no need to have different logic */
static void mesh_data_interp_from_elem(CustomData *data_layer,
                                       const MeshElem *ele_src_1,
                                       const MeshElem *ele_src_2,
                                       MeshElem *ele_dst,
                                       const float fac)
{
  if (ele_src_1->head.data && ele_src_2->head.data) {
    /* first see if we can avoid interpolation */
    if (fac <= 0.0f) {
      if (ele_src_1 == ele_dst) {
        /* do nothing */
      }
      else {
        CustomData_mesh_free_block_data(data_layer, ele_dst->head.data);
        CustomData_mesh_copy_data(
            data_layer, data_layer, ele_src_1->head.data, &ele_dst->head.data);
      }
    }
    else if (fac >= 1.0f) {
      if (ele_src_2 == ele_dst) {
        /* do nothing */
      }
      else {
        CustomData_mesh_free_block_data(data_layer, ele_dst->head.data);
        CustomData_mesh_copy_data(
            data_layer, data_layer, ele_src_2->head.data, &ele_dst->head.data);
      }
    }
    else {
      const void *src[2];
      float w[2];

      src[0] = ele_src_1->head.data;
      src[1] = ele_src_2->head.data;
      w[0] = 1.0f - fac;
      w[1] = fac;
      CustomData_mesh_interp(data_layer, src, w, NULL, 2, ele_dst->head.data);
    }
  }
}

void mesh_data_interp_from_verts(
    Mesh *mesh, const MeshVert *v_src_1, const MeehVert *v_src_2, MeehVert *v_dst, const float fac)
{
  mesh_data_interp_from_elem(
      &mesh->vdata, (const MeshElem *)v_src_1, (const MeshElem *)v_src_2, (MeshElem *)v_dst, fac);
}

void MESH_data_interp_from_edges(
    Mesh *mesh, const MeshEdge *e_src_1, const MeshEdge *e_src_2, MeehEdge *e_dst, const float fac)
{
  meshh_data_interp_from_elem(
      &mesh->edata, (const MeshElem *)e_src_1, (const MeshElem *)e_src_2, (MeshElem *)e_dst, fac);
}

/**
 * Data Vert Average
 *
 * Sets all the customdata (e.g. vert, loop) associated with a vert
 * to the average of the face regions surrounding it.
 */
static void UNUSED_FUNCTION(MESH_Data_Vert_Average)(Mesh *UNUSED(mesh), MeshFace *UNUSED(f))
{
  // MeshIter iter;
}

void mesh_data_interp_face_vert_edge(Mesh *mesh,
                                     const MeshVert *v_src_1,
                                     const MeshVert *UNUSED(v_src_2),
                                     MeshVert *v,
                                     MeshEdge *e,
                                     const float fac)
{
  float w[2];
  MeshLoop *l_v1 = NULL, *l_v = NULL, *l_v2 = NULL;
  MeshLoop *l_iter = NULL;

  if (!e->l) {
    return;
  }

  w[1] = 1.0f - fac;
  w[0] = fac;

  l_iter = e->l;
  do {
    if (l_iter->v == v_src_1) {
      l_v1 = l_iter;
      l_v = l_v1->next;
      l_v2 = l_v->next;
    }
    else if (l_iter->v == v) {
      l_v1 = l_iter->next;
      l_v = l_iter;
      l_v2 = l_iter->prev;
    }

    if (!l_v1 || !l_v2) {
      return;
    }

    const void *src[2];
    src[0] = l_v1->head.data;
    src[1] = l_v2->head.data;

    CustomData_mesh_interp(&mesh->ldata, src, w, NULL, 2, l_v->head.data);
  } while ((l_iter = l_iter->radial_next) != e->l);
}

void mesh_face_interp_from_face_ex(Mesh *mesh,
                                   MeshFace *f_dst,
                                   const MeshFace *f_src,
                                   const bool do_vertex,
                                   const void **blocks_l,
                                   const void **blocks_v,
                                   float (*cos_2d)[2],
                                   float axis_mat[3][3])
{
  MeshLoop *l_iter;
  MeshLoop *l_first;

  float *w = lib_array_alloca(w, f_src->len);
  float co[2];
  int i;

  if (f_src != f_dst) {
    mesh_elem_attrs_copy(mesh, mesh, f_src, f_dst);
  }

  /* interpolate */
  i = 0;
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f_dst);
  do {
    mul_v2_m3v3(co, axis_mat, l_iter->v->co);
    interp_weights_poly_v2(w, cos_2d, f_src->len, co);
    CustomData_bmesh_interp(&mesh->ldata, blocks_l, w, NULL, f_src->len, l_iter->head.data);
    if (do_vertex) {
      CustomData_mesh_interp(&mesh->vdata, blocks_v, w, NULL, f_src->len, l_iter->v->head.data);
    }
  } while ((void)i++, (l_iter = l_iter->next) != l_first);
}

void mesh_face_interp_from_face(Mesh *mesh, MeshFace *f_dst, const MeshFace *f_src, const bool do_vertex)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;

  const void **blocks_l = lib_array_alloca(blocks_l, f_src->len);
  const void **blocks_v = do_vertex ? lib_array_alloca(blocks_v, f_src->len) : NULL;
  float(*cos_2d)[2] = lib_array_alloca(cos_2d, f_src->len);
  float axis_mat[3][3]; /* use normal to transform into 2d xy coords */
  int i;

  /* convert the 3d coords into 2d for projection */
  lib_assert(mesh_face_is_normal_valid(f_src));
  axis_dominant_v3_to_m3(axis_mat, f_src->no);

  i = 0;
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f_src);
  do {
    mul_v2_m3v3(cos_2d[i], axis_mat, l_iter->v->co);
    blocks_l[i] = l_iter->head.data;
    if (do_vertex) {
      blocks_v[i] = l_iter->v->head.data;
    }
  } while ((void)i++, (l_iter = l_iter->next) != l_first);

  mesh_face_interp_from_face_ex(mesh, f_dst, f_src, do_vertex, blocks_l, blocks_v, cos_2d, axis_mat);
}

/**
 * Multires Interpolation
 *
 * mdisps is a grid of displacements, ordered thus:
 * <pre>
 *      v1/center----v4/next -> x
 *          |           |
 *          |           |
 *       v2/prev------v3/cur
 *          |
 *          V
 *          y
 * </pre>
 */
static int compute_mdisp_quad(const MeshLoop *l,
                              const float l_f_center[3],
                              float v1[3],
                              float v2[3],
                              float v3[3],
                              float v4[3],
                              float e1[3],
                              float e2[3])
{
  float n[3], p[3];

#ifndef NDEBUG
  {
    float cent[3];
    /* computer center */
    mesh_face_calc_center_median(l->f, cent);
    lib_assert(equals_v3v3(cent, l_f_center));
  }
#endif

  mid_v3_v3v3(p, l->prev->v->co, l->v->co);
  mid_v3_v3v3(n, l->next->v->co, l->v->co);

  copy_v3_v3(v1, l_f_center);
  copy_v3_v3(v2, p);
  copy_v3_v3(v3, l->v->co);
  copy_v3_v3(v4, n);

  sub_v3_v3v3(e1, v2, v1);
  sub_v3_v3v3(e2, v3, v4);

  return 1;
}

static bool quad_co(const float v1[3],
                    const float v2[3],
                    const float v3[3],
                    const float v4[3],
                    const float p[3],
                    const float n[3],
                    float r_uv[2])
{
  float projverts[5][3], n2[3];
  const float origin[2] = {0.0f, 0.0f};
  int i;

  /* project points into 2d along normal */
  copy_v3_v3(projverts[0], v1);
  copy_v3_v3(projverts[1], v2);
  copy_v3_v3(projverts[2], v3);
  copy_v3_v3(projverts[3], v4);
  copy_v3_v3(projverts[4], p);

  normal_quad_v3(n2, projverts[0], projverts[1], projverts[2], projverts[3]);

  if (dot_v3v3(n, n2) < -FLT_EPSILON) {
    return false;
  }

  /* rotate */
  poly_rotate_plane(n, projverts, 5);

  /* subtract origin */
  for (i = 0; i < 4; i++) {
    sub_v2_v2(projverts[i], projverts[4]);
  }

  if (!isect_point_quad_v2(origin, projverts[0], projverts[1], projverts[2], projverts[3])) {
    return false;
  }

  resolve_quad_uv_v2(r_uv, origin, projverts[0], projverts[3], projverts[2], projverts[1]);

  return true;
}

static void mdisp_axis_from_quad(const float v1[3],
                                 const float v2[3],
                                 float UNUSED(v3[3]),
                                 const float v4[3],
                                 float r_axis_x[3],
                                 float r_axis_y[3])
{
  sub_v3_v3v3(r_axis_x, v4, v1);
  sub_v3_v3v3(r_axis_y, v2, v1);

  normalize_v3(r_axis_x);
  normalize_v3(r_axis_y);
}

/**
 * param l_src: is loop whose internal displacement.
 * param l_dst: is loop to project onto.
 * param p: The point being projected.
 * param r_axis_x, r_axis_y: The location in loop's CD_MDISPS grid of point `p`.
 */
static bool mdisp_in_mdispquad(MeshLoop *l_src,
                               MeshLoop *l_dst,
                               const float l_dst_f_center[3],
                               const float p[3],
                               int res,
                               float r_axis_x[3],
                               float r_axis_y[3],
                               float r_uv[2])
{
  float v1[3], v2[3], c[3], v3[3], v4[3], e1[3], e2[3];
  float eps = FLT_EPSILON * 4000;

  if (is_zero_v3(l_src->v->no)) {
    mesh_vert_normal_update_all(l_src->v);
  }
  if (is_zero_v3(l_dst->v->no)) {
    mesh_vert_normal_update_all(l_dst->v);
  }

  compute_mdisp_quad(l_dst, l_dst_f_center, v1, v2, v3, v4, e1, e2);

  /* expand quad a bit */
  mid_v3_v3v3v3v3(c, v1, v2, v3, v4);

  sub_v3_v3(v1, c);
  sub_v3_v3(v2, c);
  sub_v3_v3(v3, c);
  sub_v3_v3(v4, c);
  mul_v3_fl(v1, 1.0f + eps);
  mul_v3_fl(v2, 1.0f + eps);
  mul_v3_fl(v3, 1.0f + eps);
  mul_v3_fl(v4, 1.0f + eps);
  add_v3_v3(v1, c);
  add_v3_v3(v2, c);
  add_v3_v3(v3, c);
  add_v3_v3(v4, c);

  if (!quad_co(v1, v2, v3, v4, p, l_src->v->no, r_uv)) {
    return 0;
  }

  mul_v2_fl(r_uv, (float)(res - 1));

  mdisp_axis_from_quad(v1, v2, v3, v4, r_axis_x, r_axis_y);

  return 1;
}

static float mesh_loop_flip_equotion(float mat[2][2],
                                     float b[2],
                                     const float target_axis_x[3],
                                     const float target_axis_y[3],
                                     const float coord[3],
                                     int i,
                                     int j)
{
  mat[0][0] = target_axis_x[i];
  mat[0][1] = target_axis_y[i];
  mat[1][0] = target_axis_x[j];
  mat[1][1] = target_axis_y[j];
  b[0] = coord[i];
  b[1] = coord[j];

  return cross_v2v2(mat[0], mat[1]);
}

static void mesh_loop_flip_disp(const float source_axis_x[3],
                                const float source_axis_y[3],
                                const float target_axis_x[3],
                                const float target_axis_y[3],
                                float disp[3])
{
  float vx[3], vy[3], coord[3];
  float n[3], vec[3];
  float b[2], mat[2][2], d;

  mul_v3_v3fl(vx, source_axis_x, disp[0]);
  mul_v3_v3fl(vy, source_axis_y, disp[1]);
  add_v3_v3v3(coord, vx, vy);

  /* project displacement from source grid plane onto target grid plane */
  cross_v3_v3v3(n, target_axis_x, target_axis_y);
  project_v3_v3v3(vec, coord, n);
  sub_v3_v3v3(coord, coord, vec);

  d = mesh_loop_flip_equotion(mat, b, target_axis_x, target_axis_y, coord, 0, 1);

  if (fabsf(d) < 1e-4f) {
    d = mesh_loop_flip_equotion(mat, b, target_axis_x, target_axis_y, coord, 0, 2);
    if (fabsf(d) < 1e-4f) {
      d = mesh_loop_flip_equotion(mat, b, target_axis_x, target_axis_y, coord, 1, 2);
    }
  }

  disp[0] = (b[0] * mat[1][1] - mat[0][1] * b[1]) / d;
  disp[1] = (mat[0][0] * b[1] - b[0] * mat[1][0]) / d;
}

typedef struct MeshLoopInterpMultiresData {
  MeshLoop *l_dst;
  MeshLoop *l_src_first;
  int cd_loop_mdisp_offset;

  MDisps *md_dst;
  const float *f_src_center;

  float *axis_x, *axis_y;
  float *v1, *v4;
  float *e1, *e2;

  int res;
  float d;
} MeshLoopInterpMultiresData;

static void loop_interp_multires_cb(void *__restrict userdata,
                                    const int ix,
                                    const TaskParallelTLS *__restrict UNUSED(tls))
{
  MeshLoopInterpMultiresData *data = userdata;

  MeshLoop *l_first = data->l_src_first;
  MeshLoop *l_dst = data->l_dst;
  const int cd_loop_mdisp_offset = data->cd_loop_mdisp_offset;

  MDisps *md_dst = data->md_dst;
  const float *f_src_center = data->f_src_center;

  float *axis_x = data->axis_x;
  float *axis_y = data->axis_y;

  float *v1 = data->v1;
  float *v4 = data->v4;
  float *e1 = data->e1;
  float *e2 = data->e2;

  const int res = data->res;
  const float d = data->d;

  float x = d * ix, y;
  int iy;
  for (y = 0.0f, iy = 0; iy < res; y += d, iy++) {
    MeshLoop *l_iter = l_first;
    float co1[3], co2[3], co[3];

    madd_v3_v3v3fl(co1, v1, e1, y);
    madd_v3_v3v3fl(co2, v4, e2, y);
    interp_v3_v3v3(co, co1, co2, x);

    do {
      MDisps *md_src;
      float src_axis_x[3], src_axis_y[3];
      float uv[2];

      md_src = MESH_ELEM_CD_GET_VOID_P(l_iter, cd_loop_mdisp_offset);

      if (mdisp_in_mdispquad(l_dst, l_iter, f_src_center, co, res, src_axis_x, src_axis_y, uv)) {
        old_mdisps_bilinear(md_dst->disps[iy * res + ix], md_src->disps, res, uv[0], uv[1]);
        mesh_loop_flip_disp(src_axis_x, src_axis_y, axis_x, axis_y, md_dst->disps[iy * res + ix]);

        break;
      }
    } while ((l_iter = l_iter->next) != l_first);
  }
}

void mesh_loop_interp_multires_ex(Mesh *UNUSED(bm),
                                  MeshLoop *l_dst,
                                  const MFace *f_src,
                                  const float f_dst_center[3],
                                  const float f_src_center[3],
                                  const int cd_loop_mdisp_offset)
{
  MDisps *md_dst;
  float v1[3], v2[3], v3[3], v4[3] = {0.0f, 0.0f, 0.0f}, e1[3], e2[3];
  float axis_x[3], axis_y[3];

  /* ignore 2-edged faces */
  if (UNLIKELY(l_dst->f->len < 3)) {
    return;
  }

  md_dst = MESH_ELEM_CD_GET_VOID_P(l_dst, cd_loop_mdisp_offset);
  compute_mdisp_quad(l_dst, f_dst_center, v1, v2, v3, v4, e1, e2);

  /* if no disps data allocate a new grid, the size of the first grid in f_src. */
  if (!md_dst->totdisp) {
    const MDisps *md_src = MESH_ELEM_CD_GET_VOID_P(MESH_FACE_FIRST_LOOP(f_src), cd_loop_mdisp_offset);

    md_dst->totdisp = md_src->totdisp;
    md_dst->level = md_src->level;
    if (md_dst->totdisp) {
      md_dst->disps = mem_callocn(sizeof(float[3]) * md_dst->totdisp, __func__);
    }
    else {
      return;
    }
  }

  mdisp_axis_from_quad(v1, v2, v3, v4, axis_x, axis_y);

  const int res = (int)sqrt(md_dst->totdisp);
  MeshLoopInterpMultiresData data = {
      .l_dst = l_dst,
      .l_src_first = MESH_FACE_FIRST_LOOP(f_src),
      .cd_loop_mdisp_offset = cd_loop_mdisp_offset,
      .md_dst = md_dst,
      .f_src_center = f_src_center,
      .axis_x = axis_x,
      .axis_y = axis_y,
      .v1 = v1,
      .v4 = v4,
      .e1 = e1,
      .e2 = e2,
      .res = res,
      .d = 1.0f / (float)(res - 1),
  };
  TaskParallelSettings settings;
  lib_parallel_range_settings_defaults(&settings);
  settings.use_threading = (res > 5);
  lib_task_parallel_range(0, res, &data, loop_interp_multires_cb, &settings);
}

void mesh_loop_interp_multires(Mesh *mesh, MeshLoop *l_dst, const MeshFace *f_src)
{
  const int cd_loop_mdisp_offset = CustomData_get_offset(&bm->ldata, CD_MDISPS);

  if (cd_loop_mdisp_offset != -1) {
    float f_dst_center[3];
    float f_src_center[3];

    mesh_face_calc_center_median(l_dst->f, f_dst_center);
    mesh_face_calc_center_median(f_src, f_src_center);

    mesh_loop_interp_multires_ex(mesh, l_dst, f_src, f_dst_center, f_src_center, cd_loop_mdisp_offset);
  }
}

void mesh_face_interp_multires_ex(Mesh *mesh,
                                  MeshFace *f_dst,
                                  const MeshFace *f_src,
                                  const float f_dst_center[3],
                                  const float f_src_center[3],
                                  const int cd_loop_mdisp_offset)
{
  MeshLoop *l_iter, *l_first;
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f_dst);
  do {
    mesh_loop_interp_multires_ex(
      mesh, l_iter, f_src, f_dst_center, f_src_center, cd_loop_mdisp_offset);
  } while ((l_iter = l_iter->next) != l_first);
}

void mesh_face_interp_multires(Mesh *mesh, MeshFace *f_dst, const MeshFace *f_src)
{
  const int cd_loop_mdisp_offset = CustomData_get_offset(&mesh->ldata, CD_MDISPS);

  if (cd_loop_mdisp_offset != -1) {
    float f_dst_center[3];
    float f_src_center[3];

    mesh_face_calc_center_median(f_dst, f_dst_center);
    mesh_face_calc_center_median(f_src, f_src_center);

    mesh_face_interp_multires_ex(mesh, f_dst, f_src, f_dst_center, f_src_center, cd_loop_mdisp_offset);
  }
}

void mesh_face_multires_bounds_smooth(Mesh *mesh, MeshFace *f)
{
  const int cd_loop_mdisp_offset = CustomData_get_offset(&mesh->ldata, CD_MDISPS);
  MeshLoop *l;
  MeshIter liter;

  if (cd_loop_mdisp_offset == -1) {
    return;
  }

  MESH_ELEM_ITER (l, &liter, f, MESH_LOOPS_OF_FACE) {
    MDisps *mdp = MESH_ELEM_CD_GET_VOID_P(l->prev, cd_loop_mdisp_offset);
    MDisps *mdl = MESH_ELEM_CD_GET_VOID_P(l, cd_loop_mdisp_offset);
    MDisps *mdn = MESH_ELEM_CD_GET_VOID_P(l->next, cd_loop_mdisp_offset);
    float co1[3];
    int sides;
    int y;

    /**
     * mdisps is a grid of displacements, ordered thus:
     * <pre>
     *                    v4/next
     *                      |
     *  |      v1/cent-----mid2 ---> x
     *  |         |         |
     *  |         |         |
     * v2/prev---mid1-----v3/cur
     *            |
     *            V
     *            y
     * </pre>
     */

    sides = (int)sqrt(mdp->totdisp);
    for (y = 0; y < sides; y++) {
      mid_v3_v3v3(co1, mdn->disps[y * sides], mdl->disps[y]);

      copy_v3_v3(mdn->disps[y * sides], co1);
      copy_v3_v3(mdl->disps[y], co1);
    }
  }

  MESH_ITER (l, &liter, f, MESH_LOOPS_OF_FACE) {
    MDisps *mdl1 = MESH_ELEM_CD_GET_VOID_P(l, cd_loop_mdisp_offset);
    MDisps *mdl2;
    float co1[3], co2[3], co[3];
    int sides;
    int y;

    /**
     * mdisps is a grid of displacements, ordered thus:
     * <pre>
     *                    v4/next
     *                      |
     *  |      v1/cent-----mid2 ---> x
     *  |         |         |
     *  |         |         |
     * v2/prev---mid1-----v3/cur
     *            |
     *            V
     *            y
     * </pre>
     */

    if (l->radial_next == l) {
      continue;
    }

    if (l->radial_next->v == l->v) {
      mdl2 = M_ELEM_CD_GET_VOID_P(l->radial_next, cd_loop_mdisp_offset);
    }
    else {
      mdl2 = M_ELEM_CD_GET_VOID_P(l->radial_next->next, cd_loop_mdisp_offset);
    }

    sides = (int)sqrt(mdl1->totdisp);
    for (y = 0; y < sides; y++) {
      int a1, a2, o1, o2;

      if (l->v != l->radial_next->v) {
        a1 = sides * y + sides - 2;
        a2 = (sides - 2) * sides + y;

        o1 = sides * y + sides - 1;
        o2 = (sides - 1) * sides + y;
      }
      else {
        a1 = sides * y + sides - 2;
        a2 = sides * y + sides - 2;
        o1 = sides * y + sides - 1;
        o2 = sides * y + sides - 1;
      }

      /* magic blending numbers, hardcoded! */
      add_v3_v3v3(co1, mdl1->disps[a1], mdl2->disps[a2]);
      mul_v3_fl(co1, 0.18);

      add_v3_v3v3(co2, mdl1->disps[o1], mdl2->disps[o2]);
      mul_v3_fl(co2, 0.32);

      add_v3_v3v3(co, co1, co2);

      copy_v3_v3(mdl1->disps[o1], co);
      copy_v3_v3(mdl2->disps[o2], co);
    }
  }
}

void mesh_loop_interp_from_face(
    Mesh *mesh, MeshLoop *l_dst, const MeshFace *f_src, const bool do_vertex, const bool do_multires)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;
  const void **vblocks = do_vertex ? lib_array_alloca(vblocks, f_src->len) : NULL;
  const void **blocks = lib_array_alloca(blocks, f_src->len);
  float(*cos_2d)[2] = lib_array_alloca(cos_2d, f_src->len);
  float *w = lib_array_alloca(w, f_src->len);
  float axis_mat[3][3]; /* use normal to transform into 2d xy coords */
  float co[2];
  int i;

  /* Convert the 3d coords into 2d for projection. */
  float axis_dominant[3];
  if (!is_zero_v3(f_src->no)) {
    lib_assert(mesh_face_is_normal_valid(f_src));
    copy_v3_v3(axis_dominant, f_src->no);
  }
  else {
    /* Rare case in which all the vertices of the face are aligned.
     * Get a random axis that is orthogonal to the tangent. */
    float vec[3];
    mesh_face_calc_tangent_auto(f_src, vec);
    ortho_v3_v3(axis_dominant, vec);
    normalize_v3(axis_dominant);
  }
  axis_dominant_v3_to_m3(axis_mat, axis_dominant);

  i = 0;
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f_src);
  do {
    mul_v2_m3v3(cos_2d[i], axis_mat, l_iter->v->co);
    blocks[i] = l_iter->head.data;

    if (do_vertex) {
      vblocks[i] = l_iter->v->head.data;
    }
  } while ((void)i++, (l_iter = l_iter->next) != l_first);

  mul_v2_m3v3(co, axis_mat, l_dst->v->co);

  /* interpolate */
  interp_weights_poly_v2(w, cos_2d, f_src->len, co);
  CustomData_mesh_interp(&mesh->ldata, blocks, w, NULL, f_src->len, l_dst->head.data);
  if (do_vertex) {
    CustomData_mesh_interp(&mesh->vdata, vblocks, w, NULL, f_src->len, l_dst->v->head.data);
  }

  if (do_multires) {
    mesh_loop_interp_multires(mesh, l_dst, f_src);
  }
}

void mesh_vert_interp_from_face(Mesh *mesh, MeshVert *v_dst, const MeshFace *f_src)
{
  MeshLoop *l_iter;
  MeshLoop *l_first;
  const void **blocks = lib_array_alloca(blocks, f_src->len);
  float(*cos_2d)[2] = lib_array_alloca(cos_2d, f_src->len);
  float *w = lib_array_alloca(w, f_src->len);
  float axis_mat[3][3]; /* use normal to transform into 2d xy coords */
  float co[2];
  int i;

  /* convert the 3d coords into 2d for projection */
  lib_assert(mesh_face_is_normal_valid(f_src));
  axis_dominant_v3_to_m3(axis_mat, f_src->no);

  i = 0;
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f_src);
  do {
    mul_v2_m3v3(cos_2d[i], axis_mat, l_iter->v->co);
    blocks[i] = l_iter->v->head.data;
  } while ((void)i++, (l_iter = l_iter->next) != l_first);

  mul_v2_m3v3(co, axis_mat, v_dst->co);

  /* interpolate */
  interp_weights_poly_v2(w, cos_2d, f_src->len, co);
  CustomData_mesh_interp(&mesh->vdata, blocks, w, NULL, f_src->len, v_dst->head.data);
}

static void update_data_blocks(Mesh *mesh, CustomData *olddata, CustomData *data)
{
  MeshIter iter;
  lib_mempool *oldpool = olddata->pool;
  void *block;

  if (data == &mesh->vdata) {
    MeshVert *eve;

    CustomData_mesh_init_pool(data, mesh->totvert, MESH_VERT);

    MESH_ITER (eve, &iter, mesh, MESH_VERTS_OF_MESH) {
      block = NULL;
      CustomData_mesh_set_default(data, &block);
      CustomData_mesh_copy_data(olddata, data, eve->head.data, &block);
      CustomData_mesh_free_block(olddata, &eve->head.data);
      eve->head.data = block;
    }
  }
  else if (data == &mesh->edata) {
    MeshEdge *eed;

    CustomData_mesh_init_pool(data, mesh->totedge, MESH_EDGE);

    MESH_ITER (eed, &iter, mesh, MESH_EDGES_OF_MESH) {
      block = NULL;
      CustomData_mesh_set_default(data, &block);
      CustomData_mesh_copy_data(olddata, data, eed->head.data, &block);
      CustomData_mesh_free_block(olddata, &eed->head.data);
      eed->head.data = block;
    }
  }
  else if (data == &mesh->ldata) {
    MeshIter liter;
    MeshFace *efa;
    MeshLoop *l;

    CustomData_mesh_init_pool(data, mesh->totloop, MESH_LOOP);
    MESH_ITER (efa, &iter, mesh, MESH_FACES_OF_MESH) {
      MESH_ELEM_ITER (l, &liter, efa, MESH_LOOPS_OF_FACE) {
        block = NULL;
        CustomData_mesh_set_default(data, &block);
        CustomData_mesh_copy_data(olddata, data, l->head.data, &block);
        CustomData_mesh_free_block(olddata, &l->head.data);
        l->head.data = block;
      }
    }
  }
  else if (data == &mesh->pdata) {
    MeshFace *efa;

    CustomData_mesh_init_pool(data, mesh->totface, MESH_FACE);

    MESH_ITER (efa, &iter, mesh, MESH_FACES_OF_MESH) {
      block = NULL;
      CustomData_mesh_set_default(data, &block);
      CustomData_mesh_copy_data(olddata, data, efa->head.data, &block);
      CustomData_mesh_free_block(olddata, &efa->head.data);
      efa->head.data = block;
    }
  }
  else {
    /* should never reach this! */
    lib_assert(0);
  }

  if (oldpool) {
    /* this should never happen but can when dissolve fails - T28960. */
    lib_assert(data->pool != oldpool);

    lib_mempool_destroy(oldpool);
  }
}

void mesh_data_layer_add(Mesh *mesh, CustomData *data, int type)
{
  CustomData olddata;

  olddata = *data;
  olddata.layers = (olddata.layers) ? mem_dupallocn(olddata.layers) : NULL;

  /* the pool is now owned by olddata and must not be shared */
  data->pool = NULL;

  CustomData_add_layer(data, type, CD_DEFAULT, NULL, 0);

  update_data_blocks(mesh, &olddata, data);
  if (olddata.layers) {
    mem_freen(olddata.layers);
  }
}

void mesh_data_layer_add_named(Mesh *mesh, CustomData *data, int type, const char *name)
{
  CustomData olddata;

  olddata = *data;
  olddata.layers = (olddata.layers) ? mem_dupallocn(olddata.layers) : NULL;

  /* the pool is now owned by olddata and must not be shared */
  data->pool = NULL;

  CustomData_add_layer_named(data, type, CD_DEFAULT, NULL, 0, name);

  update_data_blocks(mesh, &olddata, data);
  if (olddata.layers) {
    mem_freen(olddata.layers);
  }
}

void mesh_data_layer_free(Mesh *mesh, CustomData *data, int type)
{
  CustomData olddata;
  bool has_layer;

  olddata = *data;
  olddata.layers = (olddata.layers) ? mem_dupallocn(olddata.layers) : NULL;

  /* the pool is now owned by olddata and must not be shared */
  data->pool = NULL;

  has_layer = CustomData_free_layer_active(data, type, 0);
  /* Assert because its expensive to realloc - better not do if layer isn't present. */
  lib_assert(has_layer != false);
  UNUSED_VARS_NDEBUG(has_layer);

  update_data_blocks(mesh, &olddata, data);
  if (olddata.layers) {
    mem_freen(olddata.layers);
  }
}

void mesh_data_layer_free_n(Mesh *mesh, CustomData *data, int type, int n)
{
  CustomData olddata;
  bool has_layer;

  olddata = *data;
  olddata.layers = (olddata.layers) ? mem_dupallocn(olddata.layers) : NULL;

  /* the pool is now owned by olddata and must not be shared */
  data->pool = NULL;

  has_layer = CustomData_free_layer(data, type, 0, CustomData_get_layer_index_n(data, type, n));
  /* Assert because its expensive to realloc - better not do if layer isn't present. */
  lib_assert(has_layer != false);
  UNUSED_VARS_NDEBUG(has_layer);

  update_data_blocks(mesh, &olddata, data);
  if (olddata.layers) {
    mem_freen(olddata.layers);
  }
}

void mesh_data_layer_copy(Mesh *mesh, CustomData *data, int type, int src_n, int dst_n)
{
  MeshIter iter;

  if (&mesh->vdata == data) {
    MeshVert *eve;

    MESH_ITER_MESH (eve, &iter, mesh, MESH_VERTS_OF_MESH) {
      void *ptr = CustomData_mesh_get_n(data, eve->head.data, type, src_n);
      CustomData_mesh_set_n(data, eve->head.data, type, dst_n, ptr);
    }
  }
  else if (&mesh->edata == data) {
    MeshEdge *eed;

    MESH_ITER (eed, &iter, mesh, MESH_EDGES_OF_MESH) {
      void *ptr = CustomData_mesh_get_n(data, eed->head.data, type, src_n);
      CustomData_mesh_set_n(data, eed->head.data, type, dst_n, ptr);
    }
  }
  else if (&mesh->pdata == data) {
    MeshFace *efa;

    MESH_ITER (efa, &iter, mesh, MESH_FACES_OF_MESH) {
      void *ptr = CustomData_mesh_get_n(data, efa->head.data, type, src_n);
      CustomData_mesh_set_n(data, efa->head.data, type, dst_n, ptr);
    }
  }
  else if (&mesh->ldata == data) {
    MeshIter liter;
    MeshFace *efa;
    MeshLoop *l;

    MESH_MESH_ITER (efa, &iter, mesh, MESH_FACES_OF_MESH) {
      MESH_ELEM_ITER (l, &liter, efa, MESH_LOOPS_OF_FACE) {
        void *ptr = CustomData_mesh_get_n(data, l->head.data, type, src_n);
        CustomData_mesh_set_n(data, l->head.data, type, dst_n, ptr);
      }
    }
  }
  else {
    /* should never reach this! */
    lib_assert(0);
  }
}

float mesh_elem_float_data_get(CustomData *cd, void *element, int type)
{
  const float *f = CustomData_mesh_get(cd, ((MeshHeader *)element)->data, type);
  return f ? *f : 0.0f;
}

void mesh_elem_float_data_set(CustomData *cd, void *element, int type, const float val)
{
  float *f = CustomData_mesh_get(cd, ((MeshHeader *)element)->data, type);
  if (f) {
    *f = val;
  }
}

/* -------------------------------------------------------------------- */
/* Loop interpolation functions: mesh_vert_loop_groups_data_layer_***
 *
 * Handling loop custom-data such as UV's, while keeping contiguous fans is rather tedious.
 * Especially when a verts loops can have multiple CustomData layers,
 * and each layer can have multiple (different) contiguous fans.
 * Said differently, a single vertices loops may span multiple UV islands.
 *
 * These functions snapshot vertices loops, storing each contiguous fan in its own group.
 * The caller can manipulate the loops, then re-combine the CustomData values.
 *
 * While these functions don't explicitly handle multiple layers at once,
 * the caller can simply store its own list.
 *
 * note Currently they are averaged back together (weighted by loop angle)
 * but we could copy add other methods to re-combine CustomData-Loop-Fans.
 *
  */

struct LoopWalkCtx {
  /* same for all groups */
  int type;
  int cd_layer_offset;
  const float *loop_weights;
  MemArena *arena;

  /* --- Per loop fan vars --- */

  /* reference for this contiguous fan */
  const void *data_ref;
  int data_len;

  /* accumulate 'LoopGroupCD.weight' to make unit length */
  float weight_accum;

  /* both arrays the size of the 'mesh_vert_face_count(v)'
   * each contiguous fan gets a slide of these arrays */
  void **data_array;
  int *data_index_array;
  float *weight_array;
};

/* Store vars to pass into 'CustomData_bmesh_interp' */
struct LoopGroupCD {
  /* direct customdata pointer array */
  void **data;
  /* weights (aligned with 'data') */
  float *data_weights;
  /* index-in-face */
  int *data_index;
  /* number of loops in the fan */
  int data_len;
};

static void mesh_loop_walk_add(struct LoopWalkCtx *lwc, MeshLoop *l)
{
  const int i = mesh_elem_index_get(l);
  const float w = lwc->loop_weights[i];
  mesh_elem_flag_disable(l, MESH_ELEM_INTERNAL_TAG);
  lwc->data_array[lwc->data_len] = MESH_ELEM_CD_GET_VOID_P(l, lwc->cd_layer_offset);
  lwc->data_index_array[lwc->data_len] = i;
  lwc->weight_array[lwc->data_len] = w;
  lwc->weight_accum += w;

  lwc->data_len += 1;
}

/**
 * called recursively, keep stack-usage minimal.
 *
 * note called for fan matching so we're pretty much safe not to break the stack
 */
static void mesh_loop_walk_data(struct LoopWalkCtx *lwc, MeshLoop *l_walk)
{
  int i;

  lib_assert(CustomData_data_equals(
      lwc->type, lwc->data_ref, MESH_ELEM_CD_GET_VOID_P(l_walk, lwc->cd_layer_offset)));
  lib_assert(mesh_elem_flag_test(l_walk, MESH_ELEM_INTERNAL_TAG));

  mesh_loop_walk_add(lwc, l_walk);

  /* recurse around this loop-fan (in both directions) */
  for (i = 0; i < 2; i++) {
    MeshLoop *l_other = ((i == 0) ? l_walk : l_walk->prev)->radial_next;
    if (l_other->radial_next != l_other) {
      if (l_other->v != l_walk->v) {
        l_other = l_other->next;
      }
      lib_assert(l_other->v == l_walk->v);
      if (mesh_elem_flag_test(l_other, MESH_ELEM_INTERNAL_TAG)) {
        if (CustomData_data_equals(
                lwc->type, lwc->data_ref, MESH_ELEM_CD_GET_VOID_P(l_other, lwc->cd_layer_offset))) {
          mesh_loop_walk_data(lwc, l_other);
        }
      }
    }
  }
}

LinkNode *mesh_vert_loop_groups_data_layer_create(
    Mesh *mesh, MeshVert *v, const int layer_n, const float *loop_weights, MemArena *arena)
{
  struct LoopWalkCtx lwc;
  LinkNode *groups = NULL;
  MeshLoop *l;
  MeshIter liter;
  int loop_num;

  lwc.type = mesh->ldata.layers[layer_n].type;
  lwc.cd_layer_offset = mesh->ldata.layers[layer_n].offset;
  lwc.loop_weights = loop_weights;
  lwc.arena = arena;

  /* Enable 'MESH_ELEM_INTERNAL_TAG', leaving the flag clean on completion. */
  loop_num = 0;
  MESH_ITER (l, &liter, v, NESG_LOOPS_OF_VERT) {
    mesh_elem_flag_enable(l, MESH_ELEM_INTERNAL_TAG);
    mesh_elem_index_set(l, loop_num); /* set_dirty! */
    loop_num++;
  }
  mesh->elem_index_dirty |= MESH_LOOP;

  lwc.data_len = 0;
  lwc.data_array = lib_memarena_alloc(lwc.arena, sizeof(void *) * loop_num);
  lwc.data_index_array = lib_memarena_alloc(lwc.arena, sizeof(int) * loop_num);
  lwc.weight_array = lib_memarena_alloc(lwc.arena, sizeof(float) * loop_num);

  MESH_ITER (l, &liter, v, MESH_LOOPS_OF_VERT) {
    if (mesh_elem_flag_test(l, MESH_ELEM_INTERNAL_TAG)) {
      struct LoopGroupCD *lf = lib_memarena_alloc(lwc.arena, sizeof(*lf));
      int len_prev = lwc.data_len;

      lwc.data_ref = MESH_ELEM_CD_GET_VOID_P(l, lwc.cd_layer_offset);

      /* assign len-last */
      lf->data = &lwc.data_array[lwc.data_len];
      lf->data_index = &lwc.data_index_array[lwc.data_len];
      lf->data_weights = &lwc.weight_array[lwc.data_len];
      lwc.weight_accum = 0.0f;

      /* new group */
      mesh_loop_walk_data(&lwc, l);
      lf->data_len = lwc.data_len - len_prev;

      if (LIKELY(lwc.weight_accum != 0.0f)) {
        mul_vn_fl(lf->data_weights, lf->data_len, 1.0f / lwc.weight_accum);
      }
      else {
        copy_vn_fl(lf->data_weights, lf->data_len, 1.0f / (float)lf->data_len);
      }

      lib_linklist_prepend_arena(&groups, lf, lwc.arena);
    }
  }

  lib_assert(lwc.data_len == loop_num);

  return groups;
}

static void mesh_vert_loop_groups_data_layer_merge__single(Mesh *mesh,
                                                           void *lf_p,
                                                           int layer_n,
                                                           void *data_tmp)
{
  struct LoopGroupCD *lf = lf_p;
  const int type = mesh->ldata.layers[layer_n].type;
  int i;
  const float *data_weights;

  data_weights = lf->data_weights;

  CustomData_mesh_interp_n(
      &mesh->ldata, (const void **)lf->data, data_weights, NULL, lf->data_len, data_tmp, layer_n);

  for (i = 0; i < lf->data_len; i++) {
    CustomData_copy_elements(type, data_tmp, lf->data[i], 1);
  }
}

static void mesh_vert_loop_groups_data_layer_merge_weights__single(
    Mesh *mesh, void *lf_p, const int layer_n, void *data_tmp, const float *loop_weights)
{
  struct LoopGroupCD *lf = lf_p;
  const int type = mesh->ldata.layers[layer_n].type;
  int i;
  const float *data_weights;

  /* re-weight */
  float *temp_weights = lib_array_alloca(temp_weights, lf->data_len);
  float weight_accum = 0.0f;

  for (i = 0; i < lf->data_len; i++) {
    float w = loop_weights[lf->data_index[i]] * lf->data_weights[i];
    temp_weights[i] = w;
    weight_accum += w;
  }

  if (LIKELY(weight_accum != 0.0f)) {
    mul_vn_fl(temp_weights, lf->data_len, 1.0f / weight_accum);
    data_weights = temp_weights;
  }
  else {
    data_weights = lf->data_weights;
  }

  CustomData_mesh_interp_n(
      &mesh->ldata, (const void **)lf->data, data_weights, NULL, lf->data_len, data_tmp, layer_n);

  for (i = 0; i < lf->data_len; i++) {
    CustomData_copy_elements(type, data_tmp, lf->data[i], 1);
  }
}

void mesh_vert_loop_groups_data_layer_merge(Mesh *mesh, LinkNode *groups, const int layer_n)
{
  const int type = mesh->ldata.layers[layer_n].type;
  const int size = CustomData_sizeof(type);
  void *data_tmp = alloca(size);

  do {
    mesh_vert_loop_groups_data_layer_merge__single(mesh, groups->link, layer_n, data_tmp);
  } while ((groups = groups->next));
}

void mesh_vert_loop_groups_data_layer_merge_weights(Mesh *mesh,
                                                    LinkNode *groups,
                                                    const int layer_n,
                                                    const float *loop_weights)
{
  const int type = mesh->ldata.layers[layer_n].type;
  const int size = CustomData_sizeof(type);
  void *data_tmp = alloca(size);

  do {
    mesh_vert_loop_groups_data_layer_merge_weights__single(
        mesh, groups->link, layer_n, data_tmp, loop_weights);
  } while ((groups = groups->next));
}
