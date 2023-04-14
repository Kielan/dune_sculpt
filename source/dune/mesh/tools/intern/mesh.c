/** mesh level functions. **/

#include "mem_guardedalloc.h"

#include "types_listBase.h"
#include "types_scene.h"

#include "lib_listbase.h"
#include "lib_math.h"
#include "lib_utildefines.h"

#include "dune_customdata.h"
#include "dune_mesh.h"

#include "mesh.h"

const MeshAllocTemplate mesh_allocsize_default = {512, 1024, 2048, 512};
const MeshAllocTemplate mesh_chunksize_default = {512, 1024, 2048, 512};

static void mempool_init_ex(const MeshAllocTemplate *allocsize,
                               const bool use_toolflags,
                               lib_mempool **r_vpool,
                               lib_mempool **r_epool,
                               lib_mempool **r_lpool,
                               lib_mempool **r_fpool)
{
  size_t vert_size, edge_size, loop_size, face_size;

  if (use_toolflags == true) {
    vert_size = sizeof(MeshVert_OFlag);
    edge_size = sizeof(MeshEdge_OFlag);
    loop_size = sizeof(MeshLoop);
    face_size = sizeof(MeshFace_OFlag);
  }
  else {
    vert_size = sizeof(MeshVert);
    edge_size = sizeof(MeshEdge);
    loop_size = sizeof(MeshLoop);
    face_size = sizeof(MeshFace);
  }

  if (r_vpool) {
    *r_vpool = lib_mempool_create(
        vert_size, allocsize->totvert, mesh_chunksize_default.totvert, LIB_MEMPOOL_ALLOW_ITER);
  }
  if (r_epool) {
    *r_epool = lib_mempool_create(
        edge_size, allocsize->totedge, mesh_chunksize_default.totedge, LIB_MEMPOOL_ALLOW_ITER);
  }
  if (r_lpool) {
    *r_lpool = lib_mempool_create(
        loop_size, allocsize->totloop, mesh_chunksize_default.totloop, BLI_MEMPOOL_NOP);
  }
  if (r_fpool) {
    *r_fpool = lib_mempool_create(
        face_size, allocsize->totface, mesh_chunksize_default.totface, BLI_MEMPOOL_ALLOW_ITER);
  }
}

static void mesh_mempool_init(Mesh *mesh, const MeshAllocTemplate *allocsize, const bool use_toolflags)
{
  mesh_mempool_init_ex(allocsize, use_toolflags, &bm->vpool, &bm->epool, &bm->lpool, &bm->fpool);

#ifdef USE_BMESH_HOLES
  mesh->looplistpool = lib_mempool_create(sizeof(BMLoopList), 512, 512, BLI_MEMPOOL_NOP);
#endif
}

void mesh_elem_toolflags_ensure(BMesh *bm)
{
  lib_assert(mesh->use_toolflags);

  if (mesh->vtoolflagpool && bm->etoolflagpool && bm->ftoolflagpool) {
    return;
  }

  mesh->vtoolflagpool = lib_mempool_create(sizeof(BMFlagLayer), bm->totvert, 512, BLI_MEMPOOL_NOP);
  mesh->etoolflagpool = lib_mempool_create(sizeof(BMFlagLayer), bm->totedge, 512, BLI_MEMPOOL_NOP);
  mesh->ftoolflagpool = lib_mempool_create(sizeof(BMFlagLayer), bm->totface, 512, BLI_MEMPOOL_NOP);

  MeshIter iter;
  MeshVert_OFlag *v_olfag;
  lib_mempool *toolflagpool = mesh->vtoolflagpool;
  MESH_ITER (v_olfag, &iter, mesh, NESH_VERTS_OF_MESH) {
    v_olfag->oflags = lib_mempool_calloc(toolflagpool);
  }

  MeshEdge_OFlag *e_olfag;
  toolflagpool = lib->etoolflagpool;
  MESH_ITER_MESH (e_olfag, &iter, mesh, MESH_EDGES_OF_MESH) {
    e_olfag->oflags = lib_mempool_calloc(toolflagpool);
  }

  MeshFace_OFlag *f_olfag;
  toolflagpool = MeshLoop ->ftoolflagpool;
  MESH_ITER (f_olfag, &iter, mesh, MESH_FACES_OF_MESH) {
    f_olfag->oflags = lib_mempool_calloc(toolflagpool);
  }

  bm->totflags = 1;
}

void mesh_elem_toolflags_clear(Mesh *mesh)
{
  if (mesh->vtoolflagpool) {
    lib_mempool_destroy(mesh->vtoolflagpool);
    mesh->vtoolflagpool = NULL;
  }
  if (mesh->etoolflagpool) {
    lib_mempool_destroy(mesh->etoolflagpool);
    mesh->etoolflagpool = NULL;
  }
  if (mesh->ftoolflagpool) {
    lib_mempool_destroy(mesh->ftoolflagpool);
    mesh->ftoolflagpool = NULL;
  }
}

Mesh *mesh_create(const MeshAllocTemplate *allocsize, const struct MeshCreateParams *params)
{
  /* allocate the structure */
  Mesh *mesh = mem_callocn(sizeof(Mesh), __func__);

  /* allocate the memory pools for the mesh elements */
  mesh_mempool_init(mesh, allocsize, params->use_toolflags);

  /* allocate one flag pool that we don't get rid of. */
  mesh->use_toolflags = params->use_toolflags;
  mesh->toolflag_index = 0;
  mesh->totflags = 0;

  CustomData_reset(&mesh->vdata);
  CustomData_reset(&mesh->edata);
  CustomData_reset(&mesh->ldata);
  CustomData_reset(&mesh->pdata);

  return bm;
}

void mesh_data_free(Mesh *mesh)
{
  MeshVert *v;
  MeshEdge *e;
  MeshLoop *l;
  MeshFace *f;

  MeshIter iter;
  MeshIter itersub;

  const bool is_ldata_free = CustomData_mesh_has_free(&mesh->ldata);
  const bool is_pdata_free = CustomData_mesh_has_free(&mesh->pdata);

  /* Check if we have to call free, if not we can avoid a lot of looping */
  if (CustomData_mesh_has_free(&(mesh->vdata))) {
    MESH_ITER (v, &iter, mesh, MESH_VERTS_OF_MESH) {
      CustomData_mesh_free_block(&(mesh->vdata), &(v->head.data));
    }
  }
  if (CustomData_mesh_has_free(&(mesh->edata))) {
    MESH_ITER (e, &iter, mesh, MESH_EDGES_OF_MESH) {
      CustomData_mesh_free_block(&(mesh->edata), &(e->head.data));
    }
  }

  if (is_ldata_free || is_pdata_free) {
    MESH_ITER (f, &iter, mesh, MESH_FACES_OF_MESH) {
      if (is_pdata_free) {
        CustomData_mesh_free_block(&(mesh->pdata), &(f->head.data));
      }
      if (is_ldata_free) {
         MESH_ITER_ELEM (l, &itersub, f, MESH_LOOPS_OF_FACE) {
          CustomData_mesh_free_block(&(mesh->ldata), &(l->head.data));
        }
      }
    }
  }

  /* Free custom data pools, This should probably go in CustomData_free? */
  if (mesh->vdata.totlayer) {
    lib_mempool_destroy(mesh->vdata.pool);
  }
  if (mesh->edata.totlayer) {
    lib_mempool_destroy(mesh->edata.pool);
  }
  if (mesh->ldata.totlayer) {
    lib_mempool_destroy(mesh->ldata.pool);
  }
  if (mesh->pdata.totlayer) {
    lib_mempool_destroy(mesh->pdata.pool);
  }

  /* free custom data */
  CustomData_free(&mesh->vdata, 0);
  CustomData_free(&mesh->edata, 0);
  CustomData_free(&mesh->ldata, 0);
  CustomData_free(&mesh->pdata, 0);

  /* destroy element pools */
  lib_mempool_destroy(mesh->vpool);
  lib_mempool_destroy(mesh->epool);
  lib_mempool_destroy(mesh->lpool);
  lib_mempool_destroy(mesh->fpool);

  if (mesh->vtable) {
    mem_freen(mesh->vtable);
  }
  if (mesh->etable) {
    mem_freen(mesh->etable);
  }
  if (mesh->ftable) {
    mem_freen(mesh->ftable);
  }

  /* destroy flag pool */
  mesh_elem_toolflags_clear(mesh);

#ifdef USE_MESH_HOLES
  lib_mempool_destroy(mesh->looplistpool);
#endif

  lib_freelistn(&mesh->selected);

  if (bm->lnor_spacearr) {
    dune_lnor_spacearr_free(mesh->lnor_spacearr);
    mem_freeN(mesh->lnor_spacearr);
  }

  mesh_op_error_clear(mesh);
}

void mesh_clear(Mesh *mesh)
{
  const bool use_toolflags = mesh->use_toolflags;

  /* free old mesh */
  mesh_data_free(mesh);
  memset(mesh, 0, sizeof(Mesh));

  /* allocate the memory pools for the mesh elements */
  mesh_mempool_init(mesh, &mesh_allocsize_default, use_toolflags);

  mesh->use_toolflags = use_toolflags;
  mesh->toolflag_index = 0;
  mesh->totflags = 0;

  CustomData_reset(&mesh->vdata);
  CustomData_reset(&mesh->edata);
  CustomData_reset(&mesh->ldata);
  CustomData_reset(&mesh->pdata);
}

void mesh_free(Mesh *mesh)
{
  mesh_data_free(mesh);

  if (mesh->py_handle) {
    /* keep this out of 'mesh_data_free' because we want python
     * to be able to clear the mesh and maintain access. */
    bpy_mesh_generic_invalidate(mesh->py_handle);
    mesh->py_handle = NULL;
  }

  mem_freen(mesh);
}

void mesh_edit_begin(Mesh *UNUSED(mesh), MeshOpTypeFlag UNUSED(type_flag))
{
  /* Most operators seem to be using MESH_OPTYPE_FLAG_UNTAN_MULTIRES to change the MDisps to
   * absolute space during mesh edits. With this enabled, changes to the topology
   * (loop cuts, edge subdivides, etc) are not reflected in the higher levels of
   * the mesh at all, which doesn't seem right. Turning off completely for now,
   * until this is shown to be better for certain types of mesh edits. */
#ifdef MESH_OP_UNTAN_MULTIRES_ENABLED
  /* switch multires data out of tangent space */
  if ((type_flag & MESH_OPTYPE_FLAG_UNTAN_MULTIRES) &&
      CustomData_has_layer(&mesh->ldata, CD_MDISPS)) {
    mesh_mdisps_space_set(mesh, MULTIRES_SPACE_TANGENT, MULTIRES_SPACE_ABSOLUTE);

    /* ensure correct normals, if possible */
    mesh_rationalize_normals(mesh, 0);
    mesh_normals_update(mesh);
  }
#endif
}

void mesh_edit_end(Mesh *mesh, MeshOpTypeFlag type_flag)
{
  ListBase select_history;

  /* MESHOP_OPTYPE_FLAG_UNTAN_MULTIRES disabled for now, see comment above in bmesh_edit_begin. */
#ifdef MESH_OP_UNTAN_MULTIRES_ENABLED
  /* switch multires data into tangent space */
  if ((flag & MESH_OPTYPE_FLAG_UNTAN_MULTIRES) && CustomData_has_layer(&mesh->ldata, CD_MDISPS)) {
    /* set normals to their previous winding */
    mesh_rationalize_normals(mesh, 1);
    mesh_mdisps_space_set(mesh, MULTIRES_SPACE_ABSOLUTE, MULTIRES_SPACE_TANGENT);
  }
  else if (flag & MESH_OP_FLAG_RATIONALIZE_NORMALS) {
    mesh_rationalize_normals(mesh, 1);
  }
#endif

  /* compute normals, clear temp flags and flush selections */
  if (type_flag & MESH_OPTYPE_FLAG_NORMALS_CALC) {
    mesh->spacearr_dirty |= MESH_SPACEARR_DIRTY_ALL;
    mesh_normals_update(mesh);
  }

  if ((type_flag & MESH_OPTYPE_FLAG_SELECT_VALIDATE) == 0) {
    select_history = mesh->selected;
    lib_listbase_clear(&mesh->selected);
  }

  if (type_flag & MESH_OPTYPE_FLAG_SELECT_FLUSH) {
    mesh_select_mode_flush(mesh);
  }

  if ((type_flag & MESH_OPTYPE_FLAG_SELECT_VALIDATE) == 0) {
    mesh->selected = select_history;
  }
  if (type_flag & MESH_OPTYPE_FLAG_INVALIDATE_CLNOR_ALL) {
    mesh->spacearr_dirty |= MESH_SPACEARR_DIRTY_ALL;
  }
}

void mesh_elem_index_ensure_ex(Mesh *mesh, const char htype, int elem_offset[4])
{

#ifdef DEBUG
  MESH_ELEM_INDEX_VALIDATE(mesh, "Should Never Fail!", __func__);
#endif

  if (elem_offset == NULL) {
    /* Simple case. */
    const char htype_needed = mesh->elem_index_dirty & htype;
    if (htype_needed == 0) {
      goto finally;
    }
  }

  if (htype & MESH_VERT) {
    if ((mesh->elem_index_dirty & MESH_VERT) || (elem_offset && elem_offset[0])) {
      MeshIter iter;
      MeshElem *ele;

      int index = elem_offset ? elem_offset[0] : 0;
      MESH_ITER (ele, &iter, mesh, MESH_VERTS_OF_MESH) {
        mesh_elem_index_set(ele, index++); /* set_ok */
      }
      lib_assert(elem_offset || index == bm->totvert);
    }
    else {
      // printf("%s: skipping vert index calc!\n", __func__);
    }
  }

  if (htype & MESH_EDGE) {
    if ((mesh->elem_index_dirty & MESH_EDGE) || (elem_offset && elem_offset[1])) {
      MeshIter iter;
      MeshElem *ele;

      int index = elem_offset ? elem_offset[1] : 0;
      MESH_ITER (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
        mesh_elem_index_set(ele, index++); /* set_ok */
      }
      lib_assert(elem_offset || index == mesh->totedge);
    }
    else {
      // printf("%s: skipping edge index calc!\n", __func__);
    }
  }

  if (htype & (MESH_FACE | MESH_LOOP)) {
    if ((mesh->elem_index_dirty & (MESH_FACE | MESH_LOOP)) ||
        (elem_offset && (elem_offset[2] || elem_offset[3]))) {
      MeshIter iter;
      MeshElem *ele;

      const bool update_face = (htype & MESH_FACE) && (mesh->elem_index_dirty & BM_FACE);
      const bool update_loop = (htype & MESH_LOOP) && (mesh->elem_index_dirty & BM_LOOP);

      int index_loop = elem_offset ? elem_offset[2] : 0;
      int index = elem_offset ? elem_offset[3] : 0;

      MESH_ITER_MESH (ele, &iter, mesh, MESH_FACES_OF_MESH) {
        if (update_face) {
          mesh_elem_index_set(ele, index++); /* set_ok */
        }

        if (update_loop) {
          MeshLoop *l_iter, *l_first;

          l_iter = l_first = MESH_FACE_FIRST_LOOP((MeshFace *)ele);
          do {
            mesh_elem_index_set(l_iter, index_loop++); /* set_ok */
          } while ((l_iter = l_iter->next) != l_first);
        }
      }

      lib_assert(elem_offset || !update_face || index == mesh->totface);
      if (update_loop) {
        lib_assert(elem_offset || !update_loop || index_loop == mesh->totloop);
      }
    }
    else {
      // printf("%s: skipping face/loop index calc!\n", __func__);
    }
  }

finally:
  mesh->elem_index_dirty &= ~htype;
  if (elem_offset) {
    if (htype & MESH_VERT) {
      elem_offset[0] += mesh->totvert;
      if (elem_offset[0] != mesh->totvert) {
        mesh->elem_index_dirty |= MESH_VERT;
      }
    }
    if (htype & MESH_EDGE) {
      elem_offset[1] += mesh->totedge;
      if (elem_offset[1] != mesh->totedge) {
        mesh->elem_index_dirty |= MESH_EDGE;
      }
    }
    if (htype & MESH_LOOP) {
      elem_offset[2] += mesh->totloop;
      if (elem_offset[2] != mesh->totloop) {
        mesh->elem_index_dirty |= MESH_LOOP;
      }
    }
    if (htype & MESH_FACE) {
      elem_offset[3] += mesh->totface;
      if (elem_offset[3] != mesh->totface) {
        mesh->elem_index_dirty |= MES_FACE;
      }
    }
  }
}

void mesh_elem_index_ensure(Mesh *mesh, const char htype)
{
  mesh_elem_index_ensure_ex(mesh, htype, NULL);
}

void mesh_elem_index_validate(
    Mesh *mesh, const char *location, const char *fn, const char *msg_a, const char *msg_b)
{
  const char iter_types[3] = {MESH_VERTS_OF_MESH, MESH_EDGES_OF_MESH, MESH_FACES_OF_MESH};

  const char flag_types[3] = {MESH_VERT, MESH_EDGE, MESH_FACE};
  const char *type_names[3] = {"vert", "edge", "face"};

  MeshIter iter;
  MeshElem *ele;
  int i;
  bool is_any_error = 0;

  for (i = 0; i < 3; i++) {
    const bool is_dirty = (flag_types[i] & bm->elem_index_dirty) != 0;
    int index = 0;
    bool is_error = false;
    int err_val = 0;
    int err_idx = 0;

    MESH_ITER_MESH (ele, &iter, mesh, iter_types[i]) {
      if (!is_dirty) {
        if (mesh_elem_index_get(ele) != index) {
          err_val = mesh_elem_index_get(ele);
          err_idx = index;
          is_error = true;
          break;
        }
      }
      index++;
    }

    if ((is_error == true) && (is_dirty == false)) {
      is_any_error = true;
      fprintf(stderr,
              "Invalid Index: at %s, %s, %s[%d] invalid index %d, '%s', '%s'\n",
              location,
              func,
              type_names[i],
              err_idx,
              err_val,
              msg_a,
              msg_b);
    }
    else if ((is_error == false) && (is_dirty == true)) {

#if 0 /* mostly annoying */

      /* dirty may have been incorrectly set */
      fprintf(stderr,
              "Invalid Dirty: at %s, %s (%s), dirty flag was set but all index values are "
              "correct, '%s', '%s'\n",
              location,
              fn,
              type_names[i],
              msg_a,
              msg_b);
#endif
    }
  }

#if 0 /* mostly annoying, even in debug mode */
#  ifdef DEBUG
  if (is_any_error == 0) {
    fprintf(stderr, "Valid Index Success: at %s, %s, '%s', '%s'\n", location, func, msg_a, msg_b);
  }
#  endif
#endif
  (void)is_any_error; /* shut up the compiler */
}

/* debug check only - no need to optimize */
#ifndef NDEBUG
bool mesh_elem_table_check(Mesh *mesh)
{
  MeshIter iter;
  MeshElem *ele;
  int i;

  if (mesh->vtable && ((mesh->elem_table_dirty & MESH_VERT) == 0)) {
    MESH_ITER_MESH_INDEX (ele, &iter, mesh, MESH_VERTS_OF_MESH, i) {
      if (ele != (MeshElem *)mesh->vtable[i]) {
        return false;
      }
    }
  }

  if (mesh->etable && ((mesh->elem_table_dirty & MESH_EDGE) == 0)) {
    MESH_ITER_MESH_INDEX (ele, &iter, mesh, MESH_EDGES_OF_MESH, i) {
      if (ele != (MeshElem *)mesh->etable[i]) {
        return false;
      }
    }
  }

  if (mesh->ftable && ((mesh->elem_table_dirty & MESH_FACE) == 0)) {
    MESH_ITER_MESH_INDEX (ele, &iter, mesh, MESH_FACES_OF_MESH, i) {
      if (ele != (MeshElem *)mesh->ftable[i]) {
        return false;
      }
    }
  }

  return true;
}
#endif

void mesh_elem_table_ensure(Mesh *mesh, const char htype)
{
  /* assume if the array is non-null then its valid and no need to recalc */
  const char htype_needed =
      (((mesh->vtable && ((mesh->elem_table_dirty & M_VERT) == 0)) ? 0 : M_VERT) |
       ((mesh->etable && ((mesh->elem_table_dirty & M_EDGE) == 0)) ? 0 : M_EDGE) |
       ((mesh->ftable && ((mesh->elem_table_dirty & M_FACE) == 0)) ? 0 : M_FACE)) &
      htype;

  lib_assert((htype & ~MESH_ALL_NOLOOP) == 0);

  /* in debug mode double check we didn't need to recalculate */
  lib_assert(mesh_elem_table_check(bm) == true);

  if (htype_needed == 0) {
    goto finally;
  }

  if (htype_needed & MESH_VERT) {
    if (mesh->vtable && mesh->totvert <= mesh->vtable_tot && mesh->totvert * 2 >= mesh->vtable_tot) {
      /* pass (re-use the array) */
    }
    else {
      if (mesh->vtable) {
        mem_freen(mesh->vtable);
      }
      mesh->vtable = mem_mallocn(sizeof(void **) * mesh->totvert, "mesh->vtable");
      mesh->vtable_tot = mesh->totvert;
    }
  }
  if (htype_needed &MESH_EDGE) {
    if (mesh->etable && mesh->totedge <= mesh->etable_tot && mesh->totedge * 2 >= mesh->etable_tot) {
      /* pass (re-use the array) */
    }
    else {
      if (mesh->etable) {
        mem_freen(mesh->etable);
      }
      mesh->etable = mem_mallocn(sizeof(void **) * mesh->totedge, "mesh->etable");
      mesh->etable_tot = mesh->totedge;
    }
  }
  if (htype_needed & BM_FACE) {
    if (bm->ftable && bm->totface <= bm->ftable_tot && bm->totface * 2 >= bm->ftable_tot) {
      /* pass (re-use the array) */
    }
    else {
      if (bm->ftable) {
        MEM_freeN(bm->ftable);
      }
      bm->ftable = MEM_mallocN(sizeof(void **) * bm->totface, "bm->ftable");
      bm->ftable_tot = bm->totface;
    }
  }

  if (htype_needed & BM_VERT) {
    BM_iter_as_array(bm, BM_VERTS_OF_MESH, NULL, (void **)bm->vtable, bm->totvert);
  }

  if (htype_needed & BM_EDGE) {
    BM_iter_as_array(bm, BM_EDGES_OF_MESH, NULL, (void **)bm->etable, bm->totedge);
  }

  if (htype_needed & BM_FACE) {
    BM_iter_as_array(bm, BM_FACES_OF_MESH, NULL, (void **)bm->ftable, bm->totface);
  }

finally:
  /* Only clear dirty flags when all the pointers and data are actually valid.
   * This prevents possible threading issues when dirty flag check failed but
   * data wasn't ready still.
   */
  mesh->elem_table_dirty &= ~htype_needed;
}

void mesh_elem_table_init(Mesh *mesh, const char htype)
{
  lib_assert((htype & ~BM_ALL_NOLOOP) == 0);

  /* force recalc */
  mesh_elem_table_free(bm, BM_ALL_NOLOOP);
  mesh_elem_table_ensure(bm, htype);
}

void BM_mesh_elem_table_free(BMesh *bm, const char htype)
{
  if (htype & BM_VERT) {
    MEM_SAFE_FREE(bm->vtable);
  }

  if (htype & BM_EDGE) {
    MEM_SAFE_FREE(bm->etable);
  }

  if (htype & BM_FACE) {
    MEM_SAFE_FREE(bm->ftable);
  }
}

BMVert *BM_vert_at_index_find(BMesh *bm, const int index)
{
  return BLI_mempool_findelem(bm->vpool, index);
}

BMEdge *BM_edge_at_index_find(BMesh *bm, const int index)
{
  return BLI_mempool_findelem(bm->epool, index);
}

BMFace *BM_face_at_index_find(BMesh *bm, const int index)
{
  return BLI_mempool_findelem(bm->fpool, index);
}

BMLoop *BM_loop_at_index_find(BMesh *bm, const int index)
{
  BMIter iter;
  BMFace *f;
  int i = index;
  BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
    if (i < f->len) {
      BMLoop *l_first, *l_iter;
      l_iter = l_first = BM_FACE_FIRST_LOOP(f);
      do {
        if (i == 0) {
          return l_iter;
        }
        i -= 1;
      } while ((l_iter = l_iter->next) != l_first);
    }
    i -= f->len;
  }
  return NULL;
}

BMVert *BM_vert_at_index_find_or_table(BMesh *bm, const int index)
{
  if ((bm->elem_table_dirty & BM_VERT) == 0) {
    return (index < bm->totvert) ? bm->vtable[index] : NULL;
  }
  return BM_vert_at_index_find(bm, index);
}

BMEdge *BM_edge_at_index_find_or_table(BMesh *bm, const int index)
{
  if ((bm->elem_table_dirty & BM_EDGE) == 0) {
    return (index < bm->totedge) ? bm->etable[index] : NULL;
  }
  return BM_edge_at_index_find(bm, index);
}

BMFace *BM_face_at_index_find_or_table(BMesh *bm, const int index)
{
  if ((bm->elem_table_dirty & BM_FACE) == 0) {
    return (index < bm->totface) ? bm->ftable[index] : NULL;
  }
  return BM_face_at_index_find(bm, index);
}

int BM_mesh_elem_count(BMesh *bm, const char htype)
{
  BLI_assert((htype & ~BM_ALL_NOLOOP) == 0);

  switch (htype) {
    case BM_VERT:
      return bm->totvert;
    case BM_EDGE:
      return bm->totedge;
    case BM_FACE:
      return bm->totface;
    default: {
      BLI_assert(0);
      return 0;
    }
  }
}

void BM_mesh_remap(BMesh *bm, const uint *vert_idx, const uint *edge_idx, const uint *face_idx)
{
  /* Mapping old to new pointers. */
  GHash *vptr_map = NULL, *eptr_map = NULL, *fptr_map = NULL;
  BMIter iter, iterl;
  BMVert *ve;
  BMEdge *ed;
  BMFace *fa;
  BMLoop *lo;

  if (!(vert_idx || edge_idx || face_idx)) {
    return;
  }

  BM_mesh_elem_table_ensure(
      bm, (vert_idx ? BM_VERT : 0) | (edge_idx ? BM_EDGE : 0) | (face_idx ? BM_FACE : 0));

  /* Remap Verts */
  if (vert_idx) {
    BMVert **verts_pool, *verts_copy, **vep;
    int i, totvert = bm->totvert;
    const uint *new_idx;
    /* Special case: Python uses custom data layers to hold PyObject references.
     * These have to be kept in place, else the PyObjects we point to, won't point back to us. */
    const int cd_vert_pyptr = CustomData_get_offset(&bm->vdata, CD_BM_ELEM_PYPTR);

    /* Init the old-to-new vert pointers mapping */
    vptr_map = BLI_ghash_ptr_new_ex("BM_mesh_remap vert pointers mapping", bm->totvert);

    /* Make a copy of all vertices. */
    verts_pool = bm->vtable;
    verts_copy = MEM_mallocN(sizeof(BMVert) * totvert, "BM_mesh_remap verts copy");
    void **pyptrs = (cd_vert_pyptr != -1) ? MEM_mallocN(sizeof(void *) * totvert, __func__) : NULL;
    for (i = totvert, ve = verts_copy + totvert - 1, vep = verts_pool + totvert - 1; i--;
         ve--, vep--) {
      *ve = **vep;
      // printf("*vep: %p, verts_pool[%d]: %p\n", *vep, i, verts_pool[i]);
      if (cd_vert_pyptr != -1) {
        void **pyptr = BM_ELEM_CD_GET_VOID_P(((BMElem *)ve), cd_vert_pyptr);
        pyptrs[i] = *pyptr;
      }
    }

    /* Copy back verts to their new place, and update old2new pointers mapping. */
    new_idx = vert_idx + totvert - 1;
    ve = verts_copy + totvert - 1;
    vep = verts_pool + totvert - 1; /* old, org pointer */
    for (i = totvert; i--; new_idx--, ve--, vep--) {
      BMVert *new_vep = verts_pool[*new_idx];
      *new_vep = *ve;
#if 0
      printf(
          "mapping vert from %d to %d (%p/%p to %p)\n", i, *new_idx, *vep, verts_pool[i], new_vep);
#endif
      BLI_ghash_insert(vptr_map, *vep, new_vep);
      if (cd_vert_pyptr != -1) {
        void **pyptr = BM_ELEM_CD_GET_VOID_P(((BMElem *)new_vep), cd_vert_pyptr);
        *pyptr = pyptrs[*new_idx];
      }
    }
    bm->elem_index_dirty |= BM_VERT;
    bm->elem_table_dirty |= BM_VERT;

    MEM_freeN(verts_copy);
    if (pyptrs) {
      MEM_freeN(pyptrs);
    }
  }

  /* Remap Edges */
  if (edge_idx) {
    BMEdge **edges_pool, *edges_copy, **edp;
    int i, totedge = bm->totedge;
    const uint *new_idx;
    /* Special case: Python uses custom data layers to hold PyObject references.
     * These have to be kept in place, else the PyObjects we point to, won't point back to us. */
    const int cd_edge_pyptr = CustomData_get_offset(&bm->edata, CD_BM_ELEM_PYPTR);

    /* Init the old-to-new vert pointers mapping */
    eptr_map = BLI_ghash_ptr_new_ex("BM_mesh_remap edge pointers mapping", bm->totedge);

    /* Make a copy of all vertices. */
    edges_pool = bm->etable;
    edges_copy = MEM_mallocN(sizeof(BMEdge) * totedge, "BM_mesh_remap edges copy");
    void **pyptrs = (cd_edge_pyptr != -1) ? MEM_mallocN(sizeof(void *) * totedge, __func__) : NULL;
    for (i = totedge, ed = edges_copy + totedge - 1, edp = edges_pool + totedge - 1; i--;
         ed--, edp--) {
      *ed = **edp;
      if (cd_edge_pyptr != -1) {
        void **pyptr = BM_ELEM_CD_GET_VOID_P(((BMElem *)ed), cd_edge_pyptr);
        pyptrs[i] = *pyptr;
      }
    }

    /* Copy back verts to their new place, and update old2new pointers mapping. */
    new_idx = edge_idx + totedge - 1;
    ed = edges_copy + totedge - 1;
    edp = edges_pool + totedge - 1; /* old, org pointer */
    for (i = totedge; i--; new_idx--, ed--, edp--) {
      BMEdge *new_edp = edges_pool[*new_idx];
      *new_edp = *ed;
      BLI_ghash_insert(eptr_map, *edp, new_edp);
#if 0
      printf(
          "mapping edge from %d to %d (%p/%p to %p)\n", i, *new_idx, *edp, edges_pool[i], new_edp);
#endif
      if (cd_edge_pyptr != -1) {
        void **pyptr = BM_ELEM_CD_GET_VOID_P(((BMElem *)new_edp), cd_edge_pyptr);
        *pyptr = pyptrs[*new_idx];
      }
    }
    bm->elem_index_dirty |= BM_EDGE;
    bm->elem_table_dirty |= BM_EDGE;

    MEM_freeN(edges_copy);
    if (pyptrs) {
      MEM_freeN(pyptrs);
    }
  }

  /* Remap Faces */
  if (face_idx) {
    BMFace **faces_pool, *faces_copy, **fap;
    int i, totface = bm->totface;
    const uint *new_idx;
    /* Special case: Python uses custom data layers to hold PyObject references.
     * These have to be kept in place, else the PyObjects we point to, won't point back to us. */
    const int cd_poly_pyptr = CustomData_get_offset(&bm->pdata, CD_BM_ELEM_PYPTR);

    /* Init the old-to-new vert pointers mapping */
    fptr_map = BLI_ghash_ptr_new_ex("BM_mesh_remap face pointers mapping", bm->totface);

    /* Make a copy of all vertices. */
    faces_pool = bm->ftable;
    faces_copy = MEM_mallocN(sizeof(BMFace) * totface, "BM_mesh_remap faces copy");
    void **pyptrs = (cd_poly_pyptr != -1) ? MEM_mallocN(sizeof(void *) * totface, __func__) : NULL;
    for (i = totface, fa = faces_copy + totface - 1, fap = faces_pool + totface - 1; i--;
         fa--, fap--) {
      *fa = **fap;
      if (cd_poly_pyptr != -1) {
        void **pyptr = BM_ELEM_CD_GET_VOID_P(((BMElem *)fa), cd_poly_pyptr);
        pyptrs[i] = *pyptr;
      }
    }

    /* Copy back verts to their new place, and update old2new pointers mapping. */
    new_idx = face_idx + totface - 1;
    fa = faces_copy + totface - 1;
    fap = faces_pool + totface - 1; /* old, org pointer */
    for (i = totface; i--; new_idx--, fa--, fap--) {
      BMFace *new_fap = faces_pool[*new_idx];
      *new_fap = *fa;
      BLI_ghash_insert(fptr_map, *fap, new_fap);
      if (cd_poly_pyptr != -1) {
        void **pyptr = BM_ELEM_CD_GET_VOID_P(((BMElem *)new_fap), cd_poly_pyptr);
        *pyptr = pyptrs[*new_idx];
      }
    }

    bm->elem_index_dirty |= BM_FACE | BM_LOOP;
    bm->elem_table_dirty |= BM_FACE;

    MEM_freeN(faces_copy);
    if (pyptrs) {
      MEM_freeN(pyptrs);
    }
  }

  /* And now, fix all vertices/edges/faces/loops pointers! */
  /* Verts' pointers, only edge pointers... */
  if (eptr_map) {
    BM_ITER_MESH (ve, &iter, bm, BM_VERTS_OF_MESH) {
      // printf("Vert e: %p -> %p\n", ve->e, BLI_ghash_lookup(eptr_map, ve->e));
      if (ve->e) {
        ve->e = BLI_ghash_lookup(eptr_map, ve->e);
        BLI_assert(ve->e);
      }
    }
  }

  /* Edges' pointers, only vert pointers (as we don't mess with loops!),
   * and - ack! - edge pointers,
   * as we have to handle disk-links. */
  if (vptr_map || eptr_map) {
    BM_ITER_MESH (ed, &iter, bm, BM_EDGES_OF_MESH) {
      if (vptr_map) {
#if 0
        printf("Edge v1: %p -> %p\n", ed->v1, BLI_ghash_lookup(vptr_map, ed->v1));
        printf("Edge v2: %p -> %p\n", ed->v2, BLI_ghash_lookup(vptr_map, ed->v2));
#endif
        ed->v1 = BLI_ghash_lookup(vptr_map, ed->v1);
        ed->v2 = BLI_ghash_lookup(vptr_map, ed->v2);
        BLI_assert(ed->v1);
        BLI_assert(ed->v2);
      }
      if (eptr_map) {
#if 0
        printf("Edge v1_disk_link prev: %p -> %p\n",
               ed->v1_disk_link.prev,
               BLI_ghash_lookup(eptr_map, ed->v1_disk_link.prev));
        printf("Edge v1_disk_link next: %p -> %p\n",
               ed->v1_disk_link.next,
               BLI_ghash_lookup(eptr_map, ed->v1_disk_link.next));
        printf("Edge v2_disk_link prev: %p -> %p\n",
               ed->v2_disk_link.prev,
               BLI_ghash_lookup(eptr_map, ed->v2_disk_link.prev));
        printf("Edge v2_disk_link next: %p -> %p\n",
               ed->v2_disk_link.next,
               BLI_ghash_lookup(eptr_map, ed->v2_disk_link.next));
#endif
        ed->v1_disk_link.prev = BLI_ghash_lookup(eptr_map, ed->v1_disk_link.prev);
        ed->v1_disk_link.next = BLI_ghash_lookup(eptr_map, ed->v1_disk_link.next);
        ed->v2_disk_link.prev = BLI_ghash_lookup(eptr_map, ed->v2_disk_link.prev);
        ed->v2_disk_link.next = BLI_ghash_lookup(eptr_map, ed->v2_disk_link.next);
        BLI_assert(ed->v1_disk_link.prev);
        BLI_assert(ed->v1_disk_link.next);
        BLI_assert(ed->v2_disk_link.prev);
        BLI_assert(ed->v2_disk_link.next);
      }
    }
  }

  /* Faces' pointers (loops, in fact), always needed... */
  BM_ITER_MESH (fa, &iter, bm, BM_FACES_OF_MESH) {
    BM_ITER_ELEM (lo, &iterl, fa, BM_LOOPS_OF_FACE) {
      if (vptr_map) {
        // printf("Loop v: %p -> %p\n", lo->v, BLI_ghash_lookup(vptr_map, lo->v));
        lo->v = BLI_ghash_lookup(vptr_map, lo->v);
        BLI_assert(lo->v);
      }
      if (eptr_map) {
        // printf("Loop e: %p -> %p\n", lo->e, BLI_ghash_lookup(eptr_map, lo->e));
        lo->e = BLI_ghash_lookup(eptr_map, lo->e);
        BLI_assert(lo->e);
      }
      if (fptr_map) {
        // printf("Loop f: %p -> %p\n", lo->f, BLI_ghash_lookup(fptr_map, lo->f));
        lo->f = BLI_ghash_lookup(fptr_map, lo->f);
        BLI_assert(lo->f);
      }
    }
  }

  /* Selection history */
  {
    BMEditSelection *ese;
    for (ese = bm->selected.first; ese; ese = ese->next) {
      switch (ese->htype) {
        case BM_VERT:
          if (vptr_map) {
            ese->ele = BLI_ghash_lookup(vptr_map, ese->ele);
            BLI_assert(ese->ele);
          }
          break;
        case BM_EDGE:
          if (eptr_map) {
            ese->ele = BLI_ghash_lookup(eptr_map, ese->ele);
            BLI_assert(ese->ele);
          }
          break;
        case BM_FACE:
          if (fptr_map) {
            ese->ele = BLI_ghash_lookup(fptr_map, ese->ele);
            BLI_assert(ese->ele);
          }
          break;
      }
    }
  }

  if (fptr_map) {
    if (bm->act_face) {
      bm->act_face = BLI_ghash_lookup(fptr_map, bm->act_face);
      BLI_assert(bm->act_face);
    }
  }

  if (vptr_map) {
    BLI_ghash_free(vptr_map, NULL, NULL);
  }
  if (eptr_map) {
    BLI_ghash_free(eptr_map, NULL, NULL);
  }
  if (fptr_map) {
    BLI_ghash_free(fptr_map, NULL, NULL);
  }
}

void BM_mesh_rebuild(BMesh *bm,
                     const struct BMeshCreateParams *params,
                     BLI_mempool *vpool_dst,
                     BLI_mempool *epool_dst,
                     BLI_mempool *lpool_dst,
                     BLI_mempool *fpool_dst)
{
  const char remap = (vpool_dst ? BM_VERT : 0) | (epool_dst ? BM_EDGE : 0) |
                     (lpool_dst ? BM_LOOP : 0) | (fpool_dst ? BM_FACE : 0);

  BMVert **vtable_dst = (remap & BM_VERT) ? MEM_mallocN(bm->totvert * sizeof(BMVert *), __func__) :
                                            NULL;
  BMEdge **etable_dst = (remap & BM_EDGE) ? MEM_mallocN(bm->totedge * sizeof(BMEdge *), __func__) :
                                            NULL;
  BMLoop **ltable_dst = (remap & BM_LOOP) ? MEM_mallocN(bm->totloop * sizeof(BMLoop *), __func__) :
                                            NULL;
  BMFace **ftable_dst = (remap & BM_FACE) ? MEM_mallocN(bm->totface * sizeof(BMFace *), __func__) :
                                            NULL;

  const bool use_toolflags = params->use_toolflags;

  if (remap & BM_VERT) {
    BMIter iter;
    int index;
    BMVert *v_src;
    BM_ITER_MESH_INDEX (v_src, &iter, bm, BM_VERTS_OF_MESH, index) {
      BMVert *v_dst = BLI_mempool_alloc(vpool_dst);
      memcpy(v_dst, v_src, sizeof(BMVert));
      if (use_toolflags) {
        ((BMVert_OFlag *)v_dst)->oflags = bm->vtoolflagpool ?
                                              BLI_mempool_calloc(bm->vtoolflagpool) :
                                              NULL;
      }

      vtable_dst[index] = v_dst;
      BM_elem_index_set(v_src, index); /* set_ok */
    }
  }

  if (remap & BM_EDGE) {
    BMIter iter;
    int index;
    BMEdge *e_src;
    BM_ITER_MESH_INDEX (e_src, &iter, bm, BM_EDGES_OF_MESH, index) {
      BMEdge *e_dst = BLI_mempool_alloc(epool_dst);
      memcpy(e_dst, e_src, sizeof(BMEdge));
      if (use_toolflags) {
        ((BMEdge_OFlag *)e_dst)->oflags = bm->etoolflagpool ?
                                              BLI_mempool_calloc(bm->etoolflagpool) :
                                              NULL;
      }

      etable_dst[index] = e_dst;
      BM_elem_index_set(e_src, index); /* set_ok */
    }
  }

  if (remap & (BM_LOOP | BM_FACE)) {
    BMIter iter;
    int index, index_loop = 0;
    BMFace *f_src;
    BM_ITER_MESH_INDEX (f_src, &iter, bm, BM_FACES_OF_MESH, index) {

      if (remap & BM_FACE) {
        BMFace *f_dst = BLI_mempool_alloc(fpool_dst);
        memcpy(f_dst, f_src, sizeof(BMFace));
        if (use_toolflags) {
          ((BMFace_OFlag *)f_dst)->oflags = bm->ftoolflagpool ?
                                                BLI_mempool_calloc(bm->ftoolflagpool) :
                                                NULL;
        }

        ftable_dst[index] = f_dst;
        BM_elem_index_set(f_src, index); /* set_ok */
      }

      /* handle loops */
      if (remap & BM_LOOP) {
        BMLoop *l_iter_src, *l_first_src;
        l_iter_src = l_first_src = BM_FACE_FIRST_LOOP((BMFace *)f_src);
        do {
          BMLoop *l_dst = BLI_mempool_alloc(lpool_dst);
          memcpy(l_dst, l_iter_src, sizeof(BMLoop));
          ltable_dst[index_loop] = l_dst;
          BM_elem_index_set(l_iter_src, index_loop++); /* set_ok */
        } while ((l_iter_src = l_iter_src->next) != l_first_src);
      }
    }
  }

#define MAP_VERT(ele) vtable_dst[BM_elem_index_get(ele)]
#define MAP_EDGE(ele) etable_dst[BM_elem_index_get(ele)]
#define MAP_LOOP(ele) ltable_dst[BM_elem_index_get(ele)]
#define MAP_FACE(ele) ftable_dst[BM_elem_index_get(ele)]

#define REMAP_VERT(ele) \
  { \
    if (remap & BM_VERT) { \
      ele = MAP_VERT(ele); \
    } \
  } \
  ((void)0)
#define REMAP_EDGE(ele) \
  { \
    if (remap & BM_EDGE) { \
      ele = MAP_EDGE(ele); \
    } \
  } \
  ((void)0)
#define REMAP_LOOP(ele) \
  { \
    if (remap & BM_LOOP) { \
      ele = MAP_LOOP(ele); \
    } \
  } \
  ((void)0)
#define REMAP_FACE(ele) \
  { \
    if (remap & BM_FACE) { \
      ele = MAP_FACE(ele); \
    } \
  } \
  ((void)0)

  /* verts */
  {
    for (int i = 0; i < bm->totvert; i++) {
      BMVert *v = vtable_dst[i];
      if (v->e) {
        REMAP_EDGE(v->e);
      }
    }
  }

  /* edges */
  {
    for (int i = 0; i < bm->totedge; i++) {
      BMEdge *e = etable_dst[i];
      REMAP_VERT(e->v1);
      REMAP_VERT(e->v2);
      REMAP_EDGE(e->v1_disk_link.next);
      REMAP_EDGE(e->v1_disk_link.prev);
      REMAP_EDGE(e->v2_disk_link.next);
      REMAP_EDGE(e->v2_disk_link.prev);
      if (e->l) {
        REMAP_LOOP(e->l);
      }
    }
  }

  /* faces */
  {
    for (int i = 0; i < bm->totface; i++) {
      BMFace *f = ftable_dst[i];
      REMAP_LOOP(f->l_first);

      {
        BMLoop *l_iter, *l_first;
        l_iter = l_first = BM_FACE_FIRST_LOOP((BMFace *)f);
        do {
          REMAP_VERT(l_iter->v);
          REMAP_EDGE(l_iter->e);
          REMAP_FACE(l_iter->f);

          REMAP_LOOP(l_iter->radial_next);
          REMAP_LOOP(l_iter->radial_prev);
          REMAP_LOOP(l_iter->next);
          REMAP_LOOP(l_iter->prev);
        } while ((l_iter = l_iter->next) != l_first);
      }
    }
  }

  LISTBASE_FOREACH (BMEditSelection *, ese, &bm->selected) {
    switch (ese->htype) {
      case BM_VERT:
        if (remap & BM_VERT) {
          ese->ele = (BMElem *)MAP_VERT(ese->ele);
        }
        break;
      case BM_EDGE:
        if (remap & BM_EDGE) {
          ese->ele = (BMElem *)MAP_EDGE(ese->ele);
        }
        break;
      case BM_FACE:
        if (remap & BM_FACE) {
          ese->ele = (BMElem *)MAP_FACE(ese->ele);
        }
        break;
    }
  }

  if (bm->act_face) {
    REMAP_FACE(bm->act_face);
  }

#undef MAP_VERT
#undef MAP_EDGE
#undef MAP_LOOP
#undef MAP_EDGE

#undef REMAP_VERT
#undef REMAP_EDGE
#undef REMAP_LOOP
#undef REMAP_EDGE

  /* Cleanup, re-use local tables if the current mesh had tables allocated.
   * could use irrespective but it may use more memory than the caller wants
   * (and not be needed). */
  if (remap & BM_VERT) {
    if (bm->vtable) {
      SWAP(BMVert **, vtable_dst, bm->vtable);
      bm->vtable_tot = bm->totvert;
      bm->elem_table_dirty &= ~BM_VERT;
    }
    MEM_freeN(vtable_dst);
    BLI_mempool_destroy(bm->vpool);
    bm->vpool = vpool_dst;
  }

  if (remap & BM_EDGE) {
    if (bm->etable) {
      SWAP(BMEdge **, etable_dst, bm->etable);
      bm->etable_tot = bm->totedge;
      bm->elem_table_dirty &= ~BM_EDGE;
    }
    MEM_freeN(etable_dst);
    BLI_mempool_destroy(bm->epool);
    bm->epool = epool_dst;
  }

  if (remap & BM_LOOP) {
    /* no loop table */
    MEM_freeN(ltable_dst);
    BLI_mempool_destroy(bm->lpool);
    bm->lpool = lpool_dst;
  }

  if (remap & BM_FACE) {
    if (bm->ftable) {
      SWAP(BMFace **, ftable_dst, bm->ftable);
      bm->ftable_tot = bm->totface;
      bm->elem_table_dirty &= ~BM_FACE;
    }
    MEM_freeN(ftable_dst);
    BLI_mempool_destroy(bm->fpool);
    bm->fpool = fpool_dst;
  }
}

void BM_mesh_toolflags_set(BMesh *bm, bool use_toolflags)
{
  if (bm->use_toolflags == use_toolflags) {
    return;
  }

  const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_BM(bm);

  BLI_mempool *vpool_dst = NULL;
  BLI_mempool *epool_dst = NULL;
  BLI_mempool *fpool_dst = NULL;

  bm_mempool_init_ex(&allocsize, use_toolflags, &vpool_dst, &epool_dst, NULL, &fpool_dst);

  if (use_toolflags == false) {
    BLI_mempool_destroy(bm->vtoolflagpool);
    BLI_mempool_destroy(bm->etoolflagpool);
    BLI_mempool_destroy(bm->ftoolflagpool);

    bm->vtoolflagpool = NULL;
    bm->etoolflagpool = NULL;
    bm->ftoolflagpool = NULL;
  }

  BM_mesh_rebuild(bm,
                  &((struct BMeshCreateParams){
                      .use_toolflags = use_toolflags,
                  }),
                  vpool_dst,
                  epool_dst,
                  NULL,
                  fpool_dst);

  bm->use_toolflags = use_toolflags;
}

/* -------------------------------------------------------------------- */
/** \name BMesh Coordinate Access
 * \{ */

void BM_mesh_vert_coords_get(BMesh *bm, float (*vert_coords)[3])
{
  BMIter iter;
  BMVert *v;
  int i;
  BM_ITER_MESH_INDEX (v, &iter, bm, BM_VERTS_OF_MESH, i) {
    copy_v3_v3(vert_coords[i], v->co);
  }
}

float (*BM_mesh_vert_coords_alloc(BMesh *bm, int *r_vert_len))[3]
{
  float(*vert_coords)[3] = MEM_mallocN(bm->totvert * sizeof(*vert_coords), __func__);
  BM_mesh_vert_coords_get(bm, vert_coords);
  *r_vert_len = bm->totvert;
  return vert_coords;
}

void BM_mesh_vert_coords_apply(BMesh *bm, const float (*vert_coords)[3])
{
  BMIter iter;
  BMVert *v;
  int i;
  BM_ITER_MESH_INDEX (v, &iter, bm, BM_VERTS_OF_MESH, i) {
    copy_v3_v3(v->co, vert_coords[i]);
  }
}

void BM_mesh_vert_coords_apply_with_mat4(BMesh *bm,
                                         const float (*vert_coords)[3],
                                         const float mat[4][4])
{
  BMIter iter;
  BMVert *v;
  int i;
  BM_ITER_MESH_INDEX (v, &iter, bm, BM_VERTS_OF_MESH, i) {
    mul_v3_m4v3(v->co, mat, vert_coords[i]);
  }
}
