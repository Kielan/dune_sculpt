/* mesh construction fns. */
#include "mem_guardedalloc.h"
#include "lib_alloca.h"
#include "lib_math.h"
#include "lib_sort_utils.h"
#include "dune_customdata.h"
#include "structs_mesh_types.h"
#include "structs_meshdata_types.h"
#include "mesh.h"
#include "intern/bmesh_private.h"

#define SELECT 1

bool mesh_verts_from_edges(MVert **vert_arr, MEdge **edge_arr, const int len)
{
  int i, i_prev = len - 1;
  for (i = 0; i < len; i++) {
    vert_arr[i] = mesh_edge_share_vert(edge_arr[i_prev], edge_arr[i]);
    if (vert_arr[i] == NULL) {
      return false;
    }
    i_prev = i;
  }
  return true;
}

bool mesh_edges_from_verts(MEdge **edge_arr, MVert **vert_arr, const int len)
{
  int i, i_prev = len - 1;
  for (i = 0; i < len; i++) {
    edge_arr[i_prev] = mesh_edge_exists(vert_arr[i_prev], vert_arr[i]);
    if (edge_arr[i_prev] == NULL) {
      return false;
    }
    i_prev = i;
  }
  return true;
}

void mesh_edges_from_verts_ensure(Mesh *m, MEdge **edge_arr, MVert **vert_arr, const int len)
{
  int i, i_prev = len - 1;
  for (i = 0; i < len; i++) {
    edge_arr[i_prev] = mesh_edge_create(
        m, vert_arr[i_prev], vert_arr[i], NULL, MESH_CREATE_NO_DOUBLE);
    i_prev = i;
  }
}

/* prototypes */
static void mesh_loop_attrs_copy(
    Mesh *m_src, Mesh *m_dst, const MLoop *l_src, MLoop *l_dst, CustomDataMask mask_exclude);

MFace *mesh_face_create_quad_tri(Mesh *m,
                                MVert *v1,
                                MVert *v2,
                                MVert *v3,
                                MVert *v4,
                                const MFace *f_example,
                                const eMCreateFlag create_flag)
{
  MVert *vtar[4] = {v1, v2, v3, v4};
  return m_face_create_verts(m, vtar, v4 ? 4 : 3, f_example, create_flag, true);
}

void m_face_copy_shared(Mesh *m, MFace *f, MLoopFilterFn filter_fn, void *user_data)
{
  MLoop *l_first;
  MLoop *l_iter;

#ifdef DEBUG
  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    lib_assert(MESH_ELEM_API_FLAG_TEST(l_iter, _FLAG_OVERLAP) == 0);
  } while ((l_iter = l_iter->next) != l_first);
#endif

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    MLoop *l_other = l_iter->radial_next;

    if (l_other && l_other != l_iter) {
      MLoop *l_src[2];
      MLoop *l_dst[2] = {l_iter, l_iter->next};
      uint j;

      if (l_other->v == l_iter->v) {
        l_src[0] = l_other;
        l_src[1] = l_other->next;
      }
      else {
        l_src[0] = l_other->next;
        l_src[1] = l_other;
      }

      for (j = 0; j < 2; j++) {
        lib_assert(l_dst[j]->v == l_src[j]->v);
        if (MESH_ELEM_API_FLAG_TEST(l_dst[j], _FLAG_OVERLAP) == 0) {
          if ((filter_fn == NULL) || filter_fn(l_src[j], user_data)) {
            mesh_loop_attrs_copy(m, m, l_src[j], l_dst[j], 0x0);
            MESH_ELEM_API_FLAG_ENABLE(l_dst[j], _FLAG_OVERLAP);
          }
        }
      }
    }
  } while ((l_iter = l_iter->next) != l_first);

  l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
  do {
    MESH_ELEM_API_FLAG_DISABLE(l_iter, _FLAG_OVERLAP);
  } while ((l_iter = l_iter->next) != l_first);
}

/* Given an array of edges,
 * order them using the winding defined by v1 & v2
 * into edges_sort & verts_sort.
 * All arrays must be len long. */
static bool mesh_edges_sort_winding(MVert *v1,
                                  MVert *v2,
                                  MEdge **edges,
                                  const int len,
                                  MEdge **edges_sort,
                                  MVert **verts_sort)
{
  MEdge *e_iter, *e_first;
  MVert *v_iter;
  int i;

  /* all flags _must_ be cleared on exit! */
  for (i = 0; i < len; i++) {
    MESH_ELEM_API_FLAG_ENABLE(edges[i], _FLAG_MF);
    MESH_ELEM_API_FLAG_ENABLE(edges[i]->v1, _FLAG_MV);
    MESH_ELEM_API_FLAG_ENABLE(edges[i]->v2, _FLAG_MV);
  }

  /* find first edge */
  i = 0;
  v_iter = v1;
  e_iter = e_first = v1->e;
  do {
    if (MESH_ELEM_API_FLAG_TEST(e_iter, _FLAG_MF) && (mesh_edge_other_vert(e_iter, v_iter) == v2)) {
      i = 1;
      break;
    }
  } while ((e_iter = mesh_disk_edge_next(e_iter, v_iter)) != e_first);
  if (i == 0) {
    goto error;
  }

  i = 0;
  do {
    /* entering loop will always succeed */
    if (MESH_ELEM_API_FLAG_TEST(e_iter, _FLAG_MF)) {
      if (UNLIKELY(MESH_ELEM_API_FLAG_TEST(v_iter, _FLAG_MV) == false)) {
        /* vert is in loop multiple times */
        goto error;
      }

      MESH_ELEM_API_FLAG_DISABLE(e_iter, _FLAG_MF);
      edges_sort[i] = e_iter;

      MESH_ELEM_API_FLAG_DISABLE(v_iter, _FLAG_MV);
      verts_sort[i] = v_iter;

      i += 1;

      /* walk onto the next vertex */
      v_iter = mesh_edge_other_vert(e_iter, v_iter);
      if (i == len) {
        if (UNLIKELY(v_iter != verts_sort[0])) {
          goto error;
        }
        break;
      }

      e_first = e_iter;
    }
  } while ((e_iter = mesh_disk_edge_next(e_iter, v_iter)) != e_first);

  if (i == len) {
    return true;
  }

error:
  for (i = 0; i < len; i++) {
    MESH_ELEM_API_FLAG_DISABLE(edges[i], _FLAG_MF);
    MESH_ELEM_API_FLAG_DISABLE(edges[i]->v1, _FLAG_MV);
    MESH_ELEM_API_FLAG_DISABLE(edges[i]->v2, _FLAG_MV);
  }

  return false;
}

MFace *mesh_face_create_ngon(Mesh *m,
                            MVert *v1,
                            MVert *v2,
                            MEdge **edges,
                            const int len,
                            const MFace *f_example,
                            const eMCreateFlag create_flag)
{
  MEdge **edges_sort = lib_arr_alloca(edges_sort, len);
  MVert **verts_sort = lib_arr_alloca(verts_sort, len);

  lib_assert(len && v1 && v2 && edges && m);

  if (m_edges_sort_winding(v1, v2, edges, len, edges_sort, verts_sort)) {
    return mesh_face_create(m, verts_sort, edges_sort, len, f_example, create_flag);
  }

  return NULL;
}

MFace *mesh_face_create_ngon_verts(Mesh *m,
                                  MVert **vert_arr,
                                  const int len,
                                  const MFace *f_example,
                                  const eMCreateFlag create_flag,
                                  const bool calc_winding,
                                  const bool create_edges)
{
  MEdge **edge_arr = lib_arr_alloca(edge_arr, len);
  uint winding[2] = {0, 0};
  int i, i_prev = len - 1;
  MVert *v_winding[2] = {vert_arr[i_prev], vert_arr[0]};

  lib_assert(len > 2);

  for (i = 0; i < len; i++) {
    if (create_edges) {
      edge_arr[i] = mesh_edge_create(m, vert_arr[i_prev], vert_arr[i], NULL, MESH_CREATE_NO_DOUBLE);
    }
    else {
      edge_arr[i] = mesh_edge_exists(vert_arr[i_prev], vert_arr[i]);
      if (edge_arr[i] == NULL) {
        return NULL;
      }
    }

    if (calc_winding) {
      /* the edge may exist alrdy and be attached to a face
       * in this case we can find the best winding to use for the new face */
      if (edge_arr[i]->l) {
        MVert *test_v1, *test_v2;
        /* we want to use the reverse winding to the existing order */
        mesh_edge_ordered_verts(edge_arr[i], &test_v2, &test_v1);
        winding[(vert_arr[i_prev] == test_v2)]++;
        lib_assert(elem(vert_arr[i_prev], test_v2, test_v1));
      }
    }

    i_prev = i;
  }

  if (calc_winding) {
    if (winding[0] < winding[1]) {
      winding[0] = 1;
      winding[1] = 0;
    }
    else {
      winding[0] = 0;
      winding[1] = 1;
    }
  }
  else {
    winding[0] = 0;
    winding[1] = 1;
  }

  /* create the face */
  return mesh_face_create_ngon(
      m, v_winding[winding[0]], v_winding[winding[1]], edge_arr, len, f_example, create_flag);
}

void mesh_verts_sort_radial_plane(MVert **vert_arr, int len)
{
  struct SortIntByFloat *vang = lib_arr_alloca(vang, len);
  MVert **vert_arr_map = lib_arr_alloca(vert_arr_map, len);

  float nor[3], cent[3];
  int index_tangent = 0;
  mesh_verts_calc_normal_from_cloud_ex(vert_arr, len, nor, cent, &index_tangent);
  const float *far = vert_arr[index_tangent]->co;

  /* Now calc every points angle around the normal (signed). */
  for (int i = 0; i < len; i++) {
    vang[i].sort_val = angle_signed_on_axis_v3v3v3_v3(far, cent, vert_arr[i]->co, nor);
    vang[i].data = i;
    vert_arr_map[i] = vert_arr[i];
  }

  /* sort by angle and magic! - we have our ngon */
  qsort(vang, len, sizeof(*vang), lib_sortutil_cmp_float);

  for (int i = 0; i < len; i++) {
    vert_arr[i] = vert_arr_map[vang[i].data];
  }
}

static void bm_vert_attrs_copy(
    BMesh *bm_src, BMesh *bm_dst, const BMVert *v_src, BMVert *v_dst, CustomDataMask mask_exclude)
{
  if ((m_src == m_dst) && (v_src == v_dst)) {
    lib_assert_msg(0, "MVert: source and target match");
    return;
  }
  if ((mask_exclude & CD_MASK_NORMAL) == 0) {
    copy_v3_v3(v_dst->no, v_src->no);
  }
  CustomData_mesh_free_block_data_exclude_by_type(&m_dst->vdata, v_dst->head.data, mask_exclude);
  CustomData_mesh_copy_data_exclude_by_type(
      &m_src->vdata, &m_dst->vdata, v_src->head.data, &v_dst->head.data, mask_exclude);
}

static void mesh_edge_attrs_copy(
    Mesh *m_src, Mesh *m_dst, const MEdge *e_src, MEdge *e_dst, CustomDataMask mask_exclude)
{
  if ((m_src == m_dst) && (e_src == e_dst)) {
    lib_assert_msg(0, "MEdge: source and target match");
    return;
  }
  CustomData_mesh_free_block_data_exclude_by_type(&m_dst->edata, e_dst->head.data, mask_exclude);
  CustomData_mesh_copy_data_exclude_by_type(
      &m_src->edata, &m_dst->edata, e_src->head.data, &e_dst->head.data, mask_exclude);
}

static void mesh_loop_attrs_copy(
    Mesh *bm_src, Mesh *m_dst, const MLoop *l_src, MLoop *l_dst, CustomDataMask mask_exclude)
{
  if ((bm_src == bm_dst) && (l_src == l_dst)) {
    lib_assert_msg(0, "MLoop: source and target match");
    return;
  }
  CustomData_mesh_free_block_data_exclude_by_type(&m_dst->ldata, l_dst->head.data, mask_exclude);
  CustomData_mesh_copy_data_exclude_by_type(
      &m_src->ldata, &m_dst->ldata, l_src->head.data, &l_dst->head.data, mask_exclude);
}

static void m_face_attrs_copy(
    Mesh *m_src, Mesh *m_dst, const MFace *f_src, MFace *f_dst, CustomDataMask mask_exclude)
{
  if ((m_src == bm_dst) && (f_src == f_dst)) {
    lib_assert_msg(0, "Face: source and target match");
    return;
  }
  if ((mask_exclude & CD_MASK_NORMAL) == 0) {
    copy_v3_v3(f_dst->no, f_src->no);
  }
  CustomData_mesh_free_block_data_exclude_by_type(&m_dst->pdata, f_dst->head.data, mask_exclude);
  CustomData_mesh_copy_data_exclude_by_type(
      &m_src->pdata, &m_dst->pdata, f_src->head.data, &f_dst->head.data, mask_exclude);
  f_dst->mat_nr = f_src->mat_nr;
}

void mesh_elem_attrs_copy_ex(Mesh *m_src,
                           Mesh *m_dst,
                           const void *ele_src_v,
                           void *ele_dst_v,
                           const char hflag_mask,
                           const uint64_t cd_mask_exclude)
{
  /* TODO: Special handling for hide flags? */
  /* TODO: swap src/dst args, everywhere else in mesh does other way round. */
  const MHeader *ele_src = ele_src_v;
  MHeader *ele_dst = ele_dst_v;

  lib_assert(ele_src->htype == ele_dst->htype);
  lib_assert(ele_src != ele_dst);

  if ((hflag_mask & MESH_ELEM_SELECT) == 0) {
    /* First we copy select */
    if (mesh_elem_flag_test((MElem *)ele_src, M_ELEM_SELECT)) {
      mesh_elem_select_set(m_dst, (MElem *)ele_dst, true);
    }
  }

  /* Now we copy flags */
  if (hflag_mask == 0) {
    ele_dst->hflag = ele_src->hflag;
  }
  else if (hflag_mask == 0xff) {
    /* pass */
  }
  else {
    ele_dst->hflag = ((ele_dst->hflag & hflag_mask) | (ele_src->hflag & ~hflag_mask));
  }

  /* Copy specific attrs */
  switch (ele_dst->htype) {
    case MESH_VERT:
      mesh_vert_attrs_copy(
          m_src, bm_dst, (const MVert *)ele_src, (MVert *)ele_dst, cd_mask_exclude);
      break;
    case MESH_EDGE:
      mesh_edge_attrs_copy(
          m_src, m_dst, (const MEdge *)ele_src, (MEdge *)ele_dst, cd_mask_exclude);
      break;
    case MESH_LOOP:
      mesh_loop_attrs_copy(
          m_src, bm_dst, (const MLoop *)ele_src, (MLoop *)ele_dst, cd_mask_exclude);
      break;
    case MESH_FACE:
      mesh_face_attrs_copy(
          m_src, m_dst, (const MFace *)ele_src, (MFace *)ele_dst, cd_mask_exclude);
      break;
    default:
      lib_assert(0);
      break;
  }
}

void mesh_elem_attrs_copy(Mesh *m_src, Mesh *m_dst, const void *ele_src, void *ele_dst)
{
  /* MESH_TODO, default 'use_flags' to false */
  mesh_elem_attrs_copy_ex(m_src, m_dst, ele_src, ele_dst, MESH_ELEM_SELECT, 0x0);
}

void mesh_elem_select_copy(BMesh *bm_dst, void *ele_dst_v, const void *ele_src_v)
{
  BMHeader *ele_dst = ele_dst_v;
  const BMHeader *ele_src = ele_src_v;

  BLI_assert(ele_src->htype == ele_dst->htype);

  if ((ele_src->hflag & BM_ELEM_SELECT) != (ele_dst->hflag & BM_ELEM_SELECT)) {
    BM_elem_select_set(bm_dst, (BMElem *)ele_dst, (ele_src->hflag & BM_ELEM_SELECT) != 0);
  }
}

/* helper function for 'BM_mesh_copy' */
static BMFace *bm_mesh_copy_new_face(
    BMesh *bm_new, BMesh *bm_old, BMVert **vtable, BMEdge **etable, BMFace *f)
{
  BMLoop **loops = BLI_array_alloca(loops, f->len);
  BMVert **verts = BLI_array_alloca(verts, f->len);
  BMEdge **edges = BLI_array_alloca(edges, f->len);

  BMFace *f_new;
  BMLoop *l_iter, *l_first;
  int j;

  j = 0;
  l_iter = l_first = BM_FACE_FIRST_LOOP(f);
  do {
    loops[j] = l_iter;
    verts[j] = vtable[BM_elem_index_get(l_iter->v)];
    edges[j] = etable[BM_elem_index_get(l_iter->e)];
    j++;
  } while ((l_iter = l_iter->next) != l_first);

  f_new = BM_face_create(bm_new, verts, edges, f->len, NULL, BM_CREATE_SKIP_CD);

  if (UNLIKELY(f_new == NULL)) {
    return NULL;
  }

  /* use totface in case adding some faces fails */
  BM_elem_index_set(f_new, (bm_new->totface - 1)); /* set_inline */

  BM_elem_attrs_copy_ex(bm_old, bm_new, f, f_new, 0xff, 0x0);
  f_new->head.hflag = f->head.hflag; /* low level! don't do this for normal api use */

  j = 0;
  l_iter = l_first = BM_FACE_FIRST_LOOP(f_new);
  do {
    BM_elem_attrs_copy(bm_old, bm_new, loops[j], l_iter);
    j++;
  } while ((l_iter = l_iter->next) != l_first);

  return f_new;
}

void BM_mesh_copy_init_customdata_from_mesh_array(BMesh *bm_dst,
                                                  const Mesh *me_src_array[],
                                                  const int me_src_array_len,
                                                  const BMAllocTemplate *allocsize)

{
  if (allocsize == NULL) {
    allocsize = &bm_mesh_allocsize_default;
  }

  char cd_flag = 0;

  for (int i = 0; i < me_src_array_len; i++) {
    const Mesh *me_src = me_src_array[i];
    if (i == 0) {
      CustomData_copy(&me_src->vdata, &bm_dst->vdata, CD_MASK_BMESH.vmask, CD_CALLOC, 0);
      CustomData_copy(&me_src->edata, &bm_dst->edata, CD_MASK_BMESH.emask, CD_CALLOC, 0);
      CustomData_copy(&me_src->ldata, &bm_dst->ldata, CD_MASK_BMESH.lmask, CD_CALLOC, 0);
      CustomData_copy(&me_src->pdata, &bm_dst->pdata, CD_MASK_BMESH.pmask, CD_CALLOC, 0);
    }
    else {
      CustomData_merge(&me_src->vdata, &bm_dst->vdata, CD_MASK_BMESH.vmask, CD_CALLOC, 0);
      CustomData_merge(&me_src->edata, &bm_dst->edata, CD_MASK_BMESH.emask, CD_CALLOC, 0);
      CustomData_merge(&me_src->ldata, &bm_dst->ldata, CD_MASK_BMESH.lmask, CD_CALLOC, 0);
      CustomData_merge(&me_src->pdata, &bm_dst->pdata, CD_MASK_BMESH.pmask, CD_CALLOC, 0);
    }

    cd_flag |= me_src->cd_flag;
  }

  cd_flag |= BM_mesh_cd_flag_from_bmesh(bm_dst);

  CustomData_bmesh_init_pool(&bm_dst->vdata, allocsize->totvert, BM_VERT);
  CustomData_bmesh_init_pool(&bm_dst->edata, allocsize->totedge, BM_EDGE);
  CustomData_bmesh_init_pool(&bm_dst->ldata, allocsize->totloop, BM_LOOP);
  CustomData_bmesh_init_pool(&bm_dst->pdata, allocsize->totface, BM_FACE);

  BM_mesh_cd_flag_apply(bm_dst, cd_flag);
}

void BM_mesh_copy_init_customdata_from_mesh(BMesh *bm_dst,
                                            const Mesh *me_src,
                                            const BMAllocTemplate *allocsize)
{
  BM_mesh_copy_init_customdata_from_mesh_array(bm_dst, &me_src, 1, allocsize);
}

void BM_mesh_copy_init_customdata(BMesh *bm_dst, BMesh *bm_src, const BMAllocTemplate *allocsize)
{
  if (allocsize == NULL) {
    allocsize = &bm_mesh_allocsize_default;
  }

  CustomData_copy(&bm_src->vdata, &bm_dst->vdata, CD_MASK_BMESH.vmask, CD_CALLOC, 0);
  CustomData_copy(&bm_src->edata, &bm_dst->edata, CD_MASK_BMESH.emask, CD_CALLOC, 0);
  CustomData_copy(&bm_src->ldata, &bm_dst->ldata, CD_MASK_BMESH.lmask, CD_CALLOC, 0);
  CustomData_copy(&bm_src->pdata, &bm_dst->pdata, CD_MASK_BMESH.pmask, CD_CALLOC, 0);

  CustomData_bmesh_init_pool(&bm_dst->vdata, allocsize->totvert, BM_VERT);
  CustomData_bmesh_init_pool(&bm_dst->edata, allocsize->totedge, BM_EDGE);
  CustomData_bmesh_init_pool(&bm_dst->ldata, allocsize->totloop, BM_LOOP);
  CustomData_bmesh_init_pool(&bm_dst->pdata, allocsize->totface, BM_FACE);
}

void BM_mesh_copy_init_customdata_all_layers(BMesh *bm_dst,
                                             BMesh *bm_src,
                                             const char htype,
                                             const BMAllocTemplate *allocsize)
{
  if (allocsize == NULL) {
    allocsize = &bm_mesh_allocsize_default;
  }

  const char htypes[4] = {BM_VERT, BM_EDGE, BM_LOOP, BM_FACE};
  BLI_assert(((&bm_dst->vdata + 1) == &bm_dst->edata) &&
             ((&bm_dst->vdata + 2) == &bm_dst->ldata) && ((&bm_dst->vdata + 3) == &bm_dst->pdata));

  BLI_assert(((&allocsize->totvert + 1) == &allocsize->totedge) &&
             ((&allocsize->totvert + 2) == &allocsize->totloop) &&
             ((&allocsize->totvert + 3) == &allocsize->totface));

  for (int i = 0; i < 4; i++) {
    if (!(htypes[i] & htype)) {
      continue;
    }
    CustomData *dst = &bm_dst->vdata + i;
    CustomData *src = &bm_src->vdata + i;
    const int size = *(&allocsize->totvert + i);

    for (int l = 0; l < src->totlayer; l++) {
      CustomData_add_layer_named(
          dst, src->layers[l].type, CD_CALLOC, NULL, 0, src->layers[l].name);
    }
    CustomData_bmesh_init_pool(dst, size, htypes[i]);
  }
}

BMesh *BM_mesh_copy(BMesh *bm_old)
{
  BMesh *bm_new;
  BMVert *v, *v_new, **vtable = NULL;
  BMEdge *e, *e_new, **etable = NULL;
  BMFace *f, *f_new, **ftable = NULL;
  BMElem **eletable;
  BMEditSelection *ese;
  BMIter iter;
  int i;
  const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_BM(bm_old);

  /* allocate a bmesh */
  bm_new = BM_mesh_create(&allocsize,
                          &((struct BMeshCreateParams){
                              .use_toolflags = bm_old->use_toolflags,
                          }));

  BM_mesh_copy_init_customdata(bm_new, bm_old, &allocsize);

  vtable = MEM_mallocN(sizeof(BMVert *) * bm_old->totvert, "BM_mesh_copy vtable");
  etable = MEM_mallocN(sizeof(BMEdge *) * bm_old->totedge, "BM_mesh_copy etable");
  ftable = MEM_mallocN(sizeof(BMFace *) * bm_old->totface, "BM_mesh_copy ftable");

  BM_ITER_MESH_INDEX (v, &iter, bm_old, BM_VERTS_OF_MESH, i) {
    /* copy between meshes so can't use 'example' argument */
    v_new = BM_vert_create(bm_new, v->co, NULL, BM_CREATE_SKIP_CD);
    BM_elem_attrs_copy_ex(bm_old, bm_new, v, v_new, 0xff, 0x0);
    v_new->head.hflag = v->head.hflag; /* low level! don't do this for normal api use */
    vtable[i] = v_new;
    BM_elem_index_set(v, i);     /* set_inline */
    BM_elem_index_set(v_new, i); /* set_inline */
  }
  bm_old->elem_index_dirty &= ~BM_VERT;
  bm_new->elem_index_dirty &= ~BM_VERT;

  /* safety check */
  BLI_assert(i == bm_old->totvert);

  BM_ITER_MESH_INDEX (e, &iter, bm_old, BM_EDGES_OF_MESH, i) {
    e_new = BM_edge_create(bm_new,
                           vtable[BM_elem_index_get(e->v1)],
                           vtable[BM_elem_index_get(e->v2)],
                           e,
                           BM_CREATE_SKIP_CD);

    BM_elem_attrs_copy_ex(bm_old, bm_new, e, e_new, 0xff, 0x0);
    e_new->head.hflag = e->head.hflag; /* low level! don't do this for normal api use */
    etable[i] = e_new;
    BM_elem_index_set(e, i);     /* set_inline */
    BM_elem_index_set(e_new, i); /* set_inline */
  }
  bm_old->elem_index_dirty &= ~BM_EDGE;
  bm_new->elem_index_dirty &= ~BM_EDGE;

  /* safety check */
  BLI_assert(i == bm_old->totedge);

  BM_ITER_MESH_INDEX (f, &iter, bm_old, BM_FACES_OF_MESH, i) {
    BM_elem_index_set(f, i); /* set_inline */

    f_new = bm_mesh_copy_new_face(bm_new, bm_old, vtable, etable, f);

    ftable[i] = f_new;

    if (f == bm_old->act_face) {
      bm_new->act_face = f_new;
    }
  }
  m_old->elem_index_dirty &= ~M_FACE;
  m_new->elem_index_dirty &= ~M_FACE;

  /* low level! don't do this for normal api use */
  m_new->totvertsel = m_old->totvertsel;
  m_new->totedgesel = m_old->totedgesel;
  m_new->totfacesel = m_old->totfacesel;

  /* safety check */
  lib_assert(i == m_old->totface);

  /* copy over edit selection history */
  for (ese = bm_old->selected.first; ese; ese = ese->next) {
    BMElem *ele = NULL;

    switch (ese->htype) {
      case M_VERT:
        eletable = (MElem **)vtable;
        break;
      case M_EDGE:
        eletable = (MElem **)etable;
        break;
      case M_FACE:
        eletable = (MElem **)ftable;
        break;
      default:
        eletable = NULL;
        break;
    }

    if (eletable) {
      ele = eletable[BM_elem_index_get(ese->ele)];
      if (ele) {
        BM_select_history_store(bm_new, ele);
      }
    }
  }

  MEM_freeN(etable);
  MEM_freeN(vtable);
  MEM_freeN(ftable);

  /* Copy various settings. */
  bm_new->shapenr = bm_old->shapenr;
  bm_new->selectmode = bm_old->selectmode;

  return bm_new;
}

char BM_vert_flag_from_mflag(const char mflag)
{
  return (((mflag & SELECT) ? BM_ELEM_SELECT : 0) | ((mflag & ME_HIDE) ? BM_ELEM_HIDDEN : 0));
}
char BM_edge_flag_from_mflag(const short mflag)
{
  return (((mflag & SELECT) ? BM_ELEM_SELECT : 0) | ((mflag & ME_SEAM) ? BM_ELEM_SEAM : 0) |
          ((mflag & ME_EDGEDRAW) ? BM_ELEM_DRAW : 0) |
          ((mflag & ME_SHARP) == 0 ? BM_ELEM_SMOOTH : 0) | /* invert */
          ((mflag & ME_HIDE) ? BM_ELEM_HIDDEN : 0));
}
char BM_face_flag_from_mflag(const char mflag)
{
  return (((mflag & ME_FACE_SEL) ? BM_ELEM_SELECT : 0) |
          ((mflag & ME_SMOOTH) ? BM_ELEM_SMOOTH : 0) | ((mflag & ME_HIDE) ? BM_ELEM_HIDDEN : 0));
}

char BM_vert_flag_to_mflag(BMVert *v)
{
  const char hflag = v->head.hflag;

  return (((hflag & BM_ELEM_SELECT) ? SELECT : 0) | ((hflag & BM_ELEM_HIDDEN) ? ME_HIDE : 0));
}

short BM_edge_flag_to_mflag(BMEdge *e)
{
  const char hflag = e->head.hflag;

  return (((hflag & BM_ELEM_SELECT) ? SELECT : 0) | ((hflag & BM_ELEM_SEAM) ? ME_SEAM : 0) |
          ((hflag & BM_ELEM_DRAW) ? ME_EDGEDRAW : 0) |
          ((hflag & BM_ELEM_SMOOTH) == 0 ? ME_SHARP : 0) |
          ((hflag & BM_ELEM_HIDDEN) ? ME_HIDE : 0) |
          (BM_edge_is_wire(e) ? ME_LOOSEEDGE : 0) | /* not typical */
          ME_EDGERENDER);
}
char BM_face_flag_to_mflag(BMFace *f)
{
  const char hflag = f->head.hflag;

  return (((hflag & BM_ELEM_SELECT) ? ME_FACE_SEL : 0) |
          ((hflag & BM_ELEM_SMOOTH) ? ME_SMOOTH : 0) | ((hflag & BM_ELEM_HIDDEN) ? ME_HIDE : 0));
}
