#include "MEM_guardedalloc.h"

#include "LIB_utildefines.h"

#include "LIB_bitmap.h"
#include "LIB_ghash.h"
#include "LIB_math.h"
#include "LIB_rand.h"
#include "LIB_task.h"

#include "TYPES_mesh.h"
#include "TYPES_meshdata.h"

#include "DUNE_ccg.h"
#include "DUNE_mesh.h"
#include "DUNE_paint.h"
#include "DUNE_pbvh.h"
#include "DUNE_subdiv_ccg.h"

#include "PIL_time.h"

#include "GPU_buffers.h"

#include "bmesh.h"

#include "atomic_ops.h"

#include "pbvh_intern.h"

#include <limits.h>

#define LEAF_LIMIT 10000

//#define PERFCNTRS

#define STACK_FIXED_DEPTH 100

typedef struct PBVHStack {
  PBVHNode *node;
  bool revisiting;
} PBVHStack;

typedef struct PBVHIter {
  PBVH *pbvh;
  BKE_pbvh_SearchCallback scb;
  void *search_data;

  PBVHStack *stack;
  int stacksize;

  PBVHStack stackfixed[STACK_FIXED_DEPTH];
  int stackspace;
} PBVHIter;

void BB_reset(BB *bb)
{
  bb->bmin[0] = bb->bmin[1] = bb->bmin[2] = FLT_MAX;
  bb->bmax[0] = bb->bmax[1] = bb->bmax[2] = -FLT_MAX;
}

void BB_expand(BB *bb, const float co[3])
{
  for (int i = 0; i < 3; i++) {
    bb->bmin[i] = min_ff(bb->bmin[i], co[i]);
    bb->bmax[i] = max_ff(bb->bmax[i], co[i]);
  }
}

void BB_expand_with_bb(BB *bb, BB *bb2)
{
  for (int i = 0; i < 3; i++) {
    bb->bmin[i] = min_ff(bb->bmin[i], bb2->bmin[i]);
    bb->bmax[i] = max_ff(bb->bmax[i], bb2->bmax[i]);
  }
}

int BB_widest_axis(const BB *bb)
{
  float dim[3];

  for (int i = 0; i < 3; i++) {
    dim[i] = bb->bmax[i] - bb->bmin[i];
  }

  if (dim[0] > dim[1]) {
    if (dim[0] > dim[2]) {
      return 0;
    }

    return 2;
  }

  if (dim[1] > dim[2]) {
    return 1;
  }

  return 2;
}

void BBC_update_centroid(BBC *bbc)
{
  for (int i = 0; i < 3; i++) {
    bbc->bcentroid[i] = (bbc->bmin[i] + bbc->bmax[i]) * 0.5f;
  }
}

/* Not recursive */
static void update_node_vb(PBVH *pbvh, PBVHNode *node)
{
  BB vb;

  BB_reset(&vb);

  if (node->flag & PBVH_Leaf) {
    PBVHVertexIter vd;

    BKE_pbvh_vertex_iter_begin (pbvh, node, vd, PBVH_ITER_ALL) {
      BB_expand(&vb, vd.co);
    }
    BKE_pbvh_vertex_iter_end;
  }
  else {
    BB_expand_with_bb(&vb, &pbvh->nodes[node->children_offset].vb);
    BB_expand_with_bb(&vb, &pbvh->nodes[node->children_offset + 1].vb);
  }

  node->vb = vb;
}

// void BKE_pbvh_node_BB_reset(PBVHNode *node)
//{
//  BB_reset(&node->vb);
//}
//
// void BKE_pbvh_node_BB_expand(PBVHNode *node, float co[3])
//{
//  BB_expand(&node->vb, co);
//}

static bool face_materials_match(const MPoly *f1, const MPoly *f2)
{
  return ((f1->flag & ME_SMOOTH) == (f2->flag & ME_SMOOTH) && (f1->mat_nr == f2->mat_nr));
}

static bool grid_materials_match(const DMFlagMat *f1, const DMFlagMat *f2)
{
  return ((f1->flag & ME_SMOOTH) == (f2->flag & ME_SMOOTH) && (f1->mat_nr == f2->mat_nr));
}

/* Adapted from BLI_kdopbvh.c */
/* Returns the index of the first element on the right of the partition */
static int partition_indices(int *prim_indices, int lo, int hi, int axis, float mid, BBC *prim_bbc)
{
  int i = lo, j = hi;
  for (;;) {
    for (; prim_bbc[prim_indices[i]].bcentroid[axis] < mid; i++) {
      /* pass */
    }
    for (; mid < prim_bbc[prim_indices[j]].bcentroid[axis]; j--) {
      /* pass */
    }

    if (!(i < j)) {
      return i;
    }

    SWAP(int, prim_indices[i], prim_indices[j]);
    i++;
  }
}

/* Returns the index of the first element on the right of the partition */
static int partition_indices_material(PBVH *pbvh, int lo, int hi)
{
  const MPoly *mpoly = pbvh->mpoly;
  const MLoopTri *looptri = pbvh->looptri;
  const DMFlagMat *flagmats = pbvh->grid_flag_mats;
  const int *indices = pbvh->prim_indices;
  const void *first;
  int i = lo, j = hi;

  if (pbvh->looptri) {
    first = &mpoly[looptri[pbvh->prim_indices[lo]].poly];
  }
  else {
    first = &flagmats[pbvh->prim_indices[lo]];
  }

  for (;;) {
    if (pbvh->looptri) {
      for (; face_materials_match(first, &mpoly[looptri[indices[i]].poly]); i++) {
        /* pass */
      }
      for (; !face_materials_match(first, &mpoly[looptri[indices[j]].poly]); j--) {
        /* pass */
      }
    }
    else {
      for (; grid_materials_match(first, &flagmats[indices[i]]); i++) {
        /* pass */
      }
      for (; !grid_materials_match(first, &flagmats[indices[j]]); j--) {
        /* pass */
      }
    }

    if (!(i < j)) {
      return i;
    }

    SWAP(int, pbvh->prim_indices[i], pbvh->prim_indices[j]);
    i++;
  }
}

void pbvh_grow_nodes(PBVH *pbvh, int totnode)
{
  if (UNLIKELY(totnode > pbvh->node_mem_count)) {
    pbvh->node_mem_count = pbvh->node_mem_count + (pbvh->node_mem_count / 3);
    if (pbvh->node_mem_count < totnode) {
      pbvh->node_mem_count = totnode;
    }
    pbvh->nodes = MEM_recallocN(pbvh->nodes, sizeof(PBVHNode) * pbvh->node_mem_count);
  }

  pbvh->totnode = totnode;
}

/* Add a vertex to the map, with a positive value for unique vertices and
 * a negative value for additional vertices */
static int map_insert_vert(
    PBVH *pbvh, GHash *map, unsigned int *face_verts, unsigned int *uniq_verts, int vertex)
{
  void *key, **value_p;

  key = POINTER_FROM_INT(vertex);
  if (!BLI_ghash_ensure_p(map, key, &value_p)) {
    int value_i;
    if (BLI_BITMAP_TEST(pbvh->vert_bitmap, vertex) == 0) {
      BLI_BITMAP_ENABLE(pbvh->vert_bitmap, vertex);
      value_i = *uniq_verts;
      (*uniq_verts)++;
    }
    else {
      value_i = ~(*face_verts);
      (*face_verts)++;
    }
    *value_p = POINTER_FROM_INT(value_i);
    return value_i;
  }

  return POINTER_AS_INT(*value_p);
}

/* Find vertices used by the faces in this node and update the draw buffers */
static void build_mesh_leaf_node(PBVH *pbvh, PBVHNode *node)
{
  bool has_visible = false;

  node->uniq_verts = node->face_verts = 0;
  const int totface = node->totprim;

  /* reserve size is rough guess */
  GHash *map = BLI_ghash_int_new_ex("build_mesh_leaf_node gh", 2 * totface);

  int(*face_vert_indices)[3] = MEM_mallocN(sizeof(int[3]) * totface, "bvh node face vert indices");

  node->face_vert_indices = (const int(*)[3])face_vert_indices;

  if (pbvh->respect_hide == false) {
    has_visible = true;
  }

  for (int i = 0; i < totface; i++) {
    const MLoopTri *lt = &pbvh->looptri[node->prim_indices[i]];
    for (int j = 0; j < 3; j++) {
      face_vert_indices[i][j] = map_insert_vert(
          pbvh, map, &node->face_verts, &node->uniq_verts, pbvh->mloop[lt->tri[j]].v);
    }

    if (has_visible == false) {
      if (!paint_is_face_hidden(lt, pbvh->verts, pbvh->mloop)) {
        has_visible = true;
      }
    }
  }

  int *vert_indices = MEM_callocN(sizeof(int) * (node->uniq_verts + node->face_verts),
                                  "bvh node vert indices");
  node->vert_indices = vert_indices;

  /* Build the vertex list, unique verts first */
  GHashIterator gh_iter;
  GHASH_ITER (gh_iter, map) {
    void *value = BLI_ghashIterator_getValue(&gh_iter);
    int ndx = POINTER_AS_INT(value);

    if (ndx < 0) {
      ndx = -ndx + node->uniq_verts - 1;
    }

    vert_indices[ndx] = POINTER_AS_INT(BLI_ghashIterator_getKey(&gh_iter));
  }

  for (int i = 0; i < totface; i++) {
    const int sides = 3;

    for (int j = 0; j < sides; j++) {
      if (face_vert_indices[i][j] < 0) {
        face_vert_indices[i][j] = -face_vert_indices[i][j] + node->uniq_verts - 1;
      }
    }
  }

  BKE_pbvh_node_mark_rebuild_draw(node);

  BKE_pbvh_node_fully_hidden_set(node, !has_visible);

  BLI_ghash_free(map, NULL, NULL);
}

static void update_vb(PBVH *pbvh, PBVHNode *node, BBC *prim_bbc, int offset, int count)
{
  BB_reset(&node->vb);
  for (int i = offset + count - 1; i >= offset; i--) {
    BB_expand_with_bb(&node->vb, (BB *)(&prim_bbc[pbvh->prim_indices[i]]));
  }
  node->orig_vb = node->vb;
}

int BKE_pbvh_count_grid_quads(BLI_bitmap **grid_hidden,
                              const int *grid_indices,
                              int totgrid,
                              int gridsize)
{
  const int gridarea = (gridsize - 1) * (gridsize - 1);
  int totquad = 0;

  /* grid hidden layer is present, so have to check each grid for
   * visibility */

  for (int i = 0; i < totgrid; i++) {
    const BLI_bitmap *gh = grid_hidden[grid_indices[i]];

    if (gh) {
      /* grid hidden are present, have to check each element */
      for (int y = 0; y < gridsize - 1; y++) {
        for (int x = 0; x < gridsize - 1; x++) {
          if (!paint_is_grid_face_hidden(gh, gridsize, x, y)) {
            totquad++;
          }
        }
      }
    }
    else {
      totquad += gridarea;
    }
  }

  return totquad;
}

void BKE_pbvh_sync_face_sets_to_grids(PBVH *pbvh)
{
  const int gridsize = pbvh->gridkey.grid_size;
  for (int i = 0; i < pbvh->totgrid; i++) {
    BLI_bitmap *gh = pbvh->grid_hidden[i];
    const int face_index = BKE_subdiv_ccg_grid_to_face_index(pbvh->subdiv_ccg, i);
    if (!gh && pbvh->face_sets[face_index] < 0) {
      gh = pbvh->grid_hidden[i] = BLI_BITMAP_NEW(pbvh->gridkey.grid_area,
                                                 "partialvis_update_grids");
    }
    if (gh) {
      for (int y = 0; y < gridsize; y++) {
        for (int x = 0; x < gridsize; x++) {
          BLI_BITMAP_SET(gh, y * gridsize + x, pbvh->face_sets[face_index] < 0);
        }
      }
    }
  }
}

static void build_grid_leaf_node(PBVH *pbvh, PBVHNode *node)
{
  int totquads = BKE_pbvh_count_grid_quads(
      pbvh->grid_hidden, node->prim_indices, node->totprim, pbvh->gridkey.grid_size);
  BKE_pbvh_node_fully_hidden_set(node, (totquads == 0));
  BKE_pbvh_node_mark_rebuild_draw(node);
}

static void build_leaf(PBVH *pbvh, int node_index, BBC *prim_bbc, int offset, int count)
{
  pbvh->nodes[node_index].flag |= PBVH_Leaf;

  pbvh->nodes[node_index].prim_indices = pbvh->prim_indices + offset;
  pbvh->nodes[node_index].totprim = count;

  /* Still need vb for searches */
  update_vb(pbvh, &pbvh->nodes[node_index], prim_bbc, offset, count);

  if (pbvh->looptri) {
    build_mesh_leaf_node(pbvh, pbvh->nodes + node_index);
  }
  else {
    build_grid_leaf_node(pbvh, pbvh->nodes + node_index);
  }
}

/* Return zero if all primitives in the node can be drawn with the
 * same material (including flat/smooth shading), non-zero otherwise */
static bool leaf_needs_material_split(PBVH *pbvh, int offset, int count)
{
  if (count <= 1) {
    return false;
  }

  if (pbvh->looptri) {
    const MLoopTri *first = &pbvh->looptri[pbvh->prim_indices[offset]];
    const MPoly *mp = &pbvh->mpoly[first->poly];

    for (int i = offset + count - 1; i > offset; i--) {
      int prim = pbvh->prim_indices[i];
      const MPoly *mp_other = &pbvh->mpoly[pbvh->looptri[prim].poly];
      if (!face_materials_match(mp, mp_other)) {
        return true;
      }
    }
  }
  else {
    const DMFlagMat *first = &pbvh->grid_flag_mats[pbvh->prim_indices[offset]];

    for (int i = offset + count - 1; i > offset; i--) {
      int prim = pbvh->prim_indices[i];
      if (!grid_materials_match(first, &pbvh->grid_flag_mats[prim])) {
        return true;
      }
    }
  }

  return false;
}

/* Recursively build a node in the tree
 *
 * vb is the voxel box around all of the primitives contained in
 * this node.
 *
 * cb is the bounding box around all the centroids of the primitives
 * contained in this node
 *
 * offset and start indicate a range in the array of primitive indices
 */

static void build_sub(PBVH *pbvh, int node_index, BB *cb, BBC *prim_bbc, int offset, int count)
{
  int end;
  BB cb_backing;

  /* Decide whether this is a leaf or not */
  const bool below_leaf_limit = count <= pbvh->leaf_limit;
  if (below_leaf_limit) {
    if (!leaf_needs_material_split(pbvh, offset, count)) {
      build_leaf(pbvh, node_index, prim_bbc, offset, count);
      return;
    }
  }

  /* Add two child nodes */
  pbvh->nodes[node_index].children_offset = pbvh->totnode;
  pbvh_grow_nodes(pbvh, pbvh->totnode + 2);

  /* Update parent node bounding box */
  update_vb(pbvh, &pbvh->nodes[node_index], prim_bbc, offset, count);

  if (!below_leaf_limit) {
    /* Find axis with widest range of primitive centroids */
    if (!cb) {
      cb = &cb_backing;
      BB_reset(cb);
      for (int i = offset + count - 1; i >= offset; i--) {
        BB_expand(cb, prim_bbc[pbvh->prim_indices[i]].bcentroid);
      }
    }
    const int axis = BB_widest_axis(cb);

    /* Partition primitives along that axis */
    end = partition_indices(pbvh->prim_indices,
                            offset,
                            offset + count - 1,
                            axis,
                            (cb->bmax[axis] + cb->bmin[axis]) * 0.5f,
                            prim_bbc);
  }
  else {
    /* Partition primitives by material */
    end = partition_indices_material(pbvh, offset, offset + count - 1);
  }

  /* Build children */
  build_sub(pbvh, pbvh->nodes[node_index].children_offset, NULL, prim_bbc, offset, end - offset);
  build_sub(pbvh,
            pbvh->nodes[node_index].children_offset + 1,
            NULL,
            prim_bbc,
            end,
            offset + count - end);
}

static void pbvh_build(PBVH *pbvh, BB *cb, BBC *prim_bbc, int totprim)
{
  if (totprim != pbvh->totprim) {
    pbvh->totprim = totprim;
    if (pbvh->nodes) {
      MEM_freeN(pbvh->nodes);
    }
    if (pbvh->prim_indices) {
      MEM_freeN(pbvh->prim_indices);
    }
    pbvh->prim_indices = MEM_mallocN(sizeof(int) * totprim, "bvh prim indices");
    for (int i = 0; i < totprim; i++) {
      pbvh->prim_indices[i] = i;
    }
    pbvh->totnode = 0;
    if (pbvh->node_mem_count < 100) {
      pbvh->node_mem_count = 100;
      pbvh->nodes = MEM_callocN(sizeof(PBVHNode) * pbvh->node_mem_count, "bvh initial nodes");
    }
  }

  pbvh->totnode = 1;
  build_sub(pbvh, 0, cb, prim_bbc, 0, totprim);
}

void BKE_pbvh_build_mesh(PBVH *pbvh,
                         Mesh *mesh,
                         const MPoly *mpoly,
                         const MLoop *mloop,
                         MVert *verts,
                         int totvert,
                         struct CustomData *vdata,
                         struct CustomData *ldata,
                         struct CustomData *pdata,
                         const MLoopTri *looptri,
                         int looptri_num)
{
  BBC *prim_bbc = NULL;
  BB cb;

  pbvh->mesh = mesh;
  pbvh->type = PBVH_FACES;
  pbvh->mpoly = mpoly;
  pbvh->mloop = mloop;
  pbvh->looptri = looptri;
  pbvh->verts = verts;
  BKE_mesh_vertex_normals_ensure(mesh);
  pbvh->vert_normals = BKE_mesh_vertex_normals_for_write(mesh);
  pbvh->vert_bitmap = BLI_BITMAP_NEW(totvert, "bvh->vert_bitmap");
  pbvh->totvert = totvert;
  pbvh->leaf_limit = LEAF_LIMIT;
  pbvh->vdata = vdata;
  pbvh->ldata = ldata;
  pbvh->pdata = pdata;

  pbvh->face_sets_color_seed = mesh->face_sets_color_seed;
  pbvh->face_sets_color_default = mesh->face_sets_color_default;

  BB_reset(&cb);

  /* For each face, store the AABB and the AABB centroid */
  prim_bbc = MEM_mallocN(sizeof(BBC) * looptri_num, "prim_bbc");

  for (int i = 0; i < looptri_num; i++) {
    const MLoopTri *lt = &looptri[i];
    const int sides = 3;
    BBC *bbc = prim_bbc + i;

    BB_reset((BB *)bbc);

    for (int j = 0; j < sides; j++) {
      BB_expand((BB *)bbc, verts[pbvh->mloop[lt->tri[j]].v].co);
    }

    BBC_update_centroid(bbc);

    BB_expand(&cb, bbc->bcentroid);
  }

  if (looptri_num) {
    pbvh_build(pbvh, &cb, prim_bbc, looptri_num);
  }

  MEM_freeN(prim_bbc);

  /* Clear the bitmap so it can be used as an update tag later on. */
  BLI_bitmap_set_all(pbvh->vert_bitmap, false, totvert);
}

void BKE_pbvh_build_grids(PBVH *pbvh,
                          CCGElem **grids,
                          int totgrid,
                          CCGKey *key,
                          void **gridfaces,
                          DMFlagMat *flagmats,
                          BLI_bitmap **grid_hidden)
{
  const int gridsize = key->grid_size;

  pbvh->type = PBVH_GRIDS;
  pbvh->grids = grids;
  pbvh->gridfaces = gridfaces;
  pbvh->grid_flag_mats = flagmats;
  pbvh->totgrid = totgrid;
  pbvh->gridkey = *key;
  pbvh->grid_hidden = grid_hidden;
  pbvh->leaf_limit = max_ii(LEAF_LIMIT / (gridsize * gridsize), 1);

  BB cb;
  BB_reset(&cb);

  /* For each grid, store the AABB and the AABB centroid */
  BBC *prim_bbc = MEM_mallocN(sizeof(BBC) * totgrid, "prim_bbc");

  for (int i = 0; i < totgrid; i++) {
    CCGElem *grid = grids[i];
    BBC *bbc = prim_bbc + i;

    BB_reset((BB *)bbc);

    for (int j = 0; j < gridsize * gridsize; j++) {
      BB_expand((BB *)bbc, CCG_elem_offset_co(key, grid, j));
    }

    BBC_update_centroid(bbc);

    BB_expand(&cb, bbc->bcentroid);
  }

  if (totgrid) {
    pbvh_build(pbvh, &cb, prim_bbc, totgrid);
  }

  MEM_freeN(prim_bbc);
}

PBVH *BKE_pbvh_new(void)
{
  PBVH *pbvh = MEM_callocN(sizeof(PBVH), "pbvh");
  pbvh->respect_hide = true;
  return pbvh;
}

void BKE_pbvh_free(PBVH *pbvh)
{
  for (int i = 0; i < pbvh->totnode; i++) {
    PBVHNode *node = &pbvh->nodes[i];

    if (node->flag & PBVH_Leaf) {
      if (node->draw_buffers) {
        GPU_pbvh_buffers_free(node->draw_buffers);
      }
      if (node->vert_indices) {
        MEM_freeN((void *)node->vert_indices);
      }
      if (node->face_vert_indices) {
        MEM_freeN((void *)node->face_vert_indices);
      }
      if (node->bm_faces) {
        BLI_gset_free(node->bm_faces, NULL);
      }
      if (node->bm_unique_verts) {
        BLI_gset_free(node->bm_unique_verts, NULL);
      }
      if (node->bm_other_verts) {
        BLI_gset_free(node->bm_other_verts, NULL);
      }
    }
  }

  if (pbvh->deformed) {
    if (pbvh->verts) {
      /* if pbvh was deformed, new memory was allocated for verts/faces -- free it */

      MEM_freeN((void *)pbvh->verts);
    }
  }

  if (pbvh->looptri) {
    MEM_freeN((void *)pbvh->looptri);
  }

  if (pbvh->nodes) {
    MEM_freeN(pbvh->nodes);
  }

  if (pbvh->prim_indices) {
    MEM_freeN(pbvh->prim_indices);
  }

  MEM_SAFE_FREE(pbvh->vert_bitmap);

  MEM_freeN(pbvh);
}

static void pbvh_iter_begin(PBVHIter *iter,
                            PBVH *pbvh,
                            BKE_pbvh_SearchCallback scb,
                            void *search_data)
{
  iter->pbvh = pbvh;
  iter->scb = scb;
  iter->search_data = search_data;

  iter->stack = iter->stackfixed;
  iter->stackspace = STACK_FIXED_DEPTH;

  iter->stack[0].node = pbvh->nodes;
  iter->stack[0].revisiting = false;
  iter->stacksize = 1;
}

static void pbvh_iter_end(PBVHIter *iter)
{
  if (iter->stackspace > STACK_FIXED_DEPTH) {
    MEM_freeN(iter->stack);
  }
}

static void pbvh_stack_push(PBVHIter *iter, PBVHNode *node, bool revisiting)
{
  if (UNLIKELY(iter->stacksize == iter->stackspace)) {
    iter->stackspace *= 2;
    if (iter->stackspace != (STACK_FIXED_DEPTH * 2)) {
      iter->stack = MEM_reallocN(iter->stack, sizeof(PBVHStack) * iter->stackspace);
    }
    else {
      iter->stack = MEM_mallocN(sizeof(PBVHStack) * iter->stackspace, "PBVHStack");
      memcpy(iter->stack, iter->stackfixed, sizeof(PBVHStack) * iter->stacksize);
    }
  }

  iter->stack[iter->stacksize].node = node;
  iter->stack[iter->stacksize].revisiting = revisiting;
  iter->stacksize++;
}

static PBVHNode *pbvh_iter_next(PBVHIter *iter)
{
  /* purpose here is to traverse tree, visiting child nodes before their
   * parents, this order is necessary for e.g. computing bounding boxes */

  while (iter->stacksize) {
    /* pop node */
    iter->stacksize--;
    PBVHNode *node = iter->stack[iter->stacksize].node;

    /* on a mesh with no faces this can happen
     * can remove this check if we know meshes have at least 1 face */
    if (node == NULL) {
      return NULL;
    }

    bool revisiting = iter->stack[iter->stacksize].revisiting;

    /* revisiting node already checked */
    if (revisiting) {
      return node;
    }

    if (iter->scb && !iter->scb(node, iter->search_data)) {
      continue; /* don't traverse, outside of search zone */
    }

    if (node->flag & PBVH_Leaf) {
      /* immediately hit leaf node */
      return node;
    }

    /* come back later when children are done */
    pbvh_stack_push(iter, node, true);

    /* push two child nodes on the stack */
    pbvh_stack_push(iter, iter->pbvh->nodes + node->children_offset + 1, false);
    pbvh_stack_push(iter, iter->pbvh->nodes + node->children_offset, false);
  }

  return NULL;
}

static PBVHNode *pbvh_iter_next_occluded(PBVHIter *iter)
{
  while (iter->stacksize) {
    /* pop node */
    iter->stacksize--;
    PBVHNode *node = iter->stack[iter->stacksize].node;

    /* on a mesh with no faces this can happen
     * can remove this check if we know meshes have at least 1 face */
    if (node == NULL) {
      return NULL;
    }

    if (iter->scb && !iter->scb(node, iter->search_data)) {
      continue; /* don't traverse, outside of search zone */
    }

    if (node->flag & PBVH_Leaf) {
      /* immediately hit leaf node */
      return node;
    }

    pbvh_stack_push(iter, iter->pbvh->nodes + node->children_offset + 1, false);
    pbvh_stack_push(iter, iter->pbvh->nodes + node->children_offset, false);
  }

  return NULL;
}

void BKE_pbvh_search_gather(
    PBVH *pbvh, BKE_pbvh_SearchCallback scb, void *search_data, PBVHNode ***r_array, int *r_tot)
{
  PBVHIter iter;
  PBVHNode **array = NULL, *node;
  int tot = 0, space = 0;

  pbvh_iter_begin(&iter, pbvh, scb, search_data);

  while ((node = pbvh_iter_next(&iter))) {
    if (node->flag & PBVH_Leaf) {
      if (UNLIKELY(tot == space)) {
        /* resize array if needed */
        space = (tot == 0) ? 32 : space * 2;
        array = MEM_recallocN_id(array, sizeof(PBVHNode *) * space, __func__);
      }

      array[tot] = node;
      tot++;
    }
  }

  pbvh_iter_end(&iter);

  if (tot == 0 && array) {
    MEM_freeN(array);
    array = NULL;
  }

  *r_array = array;
  *r_tot = tot;
}

void BKE_pbvh_search_callback(PBVH *pbvh,
                              BKE_pbvh_SearchCallback scb,
                              void *search_data,
                              BKE_pbvh_HitCallback hcb,
                              void *hit_data)
{
  PBVHIter iter;
  PBVHNode *node;

  pbvh_iter_begin(&iter, pbvh, scb, search_data);

  while ((node = pbvh_iter_next(&iter))) {
    if (node->flag & PBVH_Leaf) {
      hcb(node, hit_data);
    }
  }

  pbvh_iter_end(&iter);
}

typedef struct node_tree {
  PBVHNode *data;

  struct node_tree *left;
  struct node_tree *right;
} node_tree;

static void node_tree_insert(node_tree *tree, node_tree *new_node)
{
  if (new_node->data->tmin < tree->data->tmin) {
    if (tree->left) {
      node_tree_insert(tree->left, new_node);
    }
    else {
      tree->left = new_node;
    }
  }
  else {
    if (tree->right) {
      node_tree_insert(tree->right, new_node);
    }
    else {
      tree->right = new_node;
    }
  }
}

static void traverse_tree(node_tree *tree,
                          BKE_pbvh_HitOccludedCallback hcb,
                          void *hit_data,
                          float *tmin)
{
  if (tree->left) {
    traverse_tree(tree->left, hcb, hit_data, tmin);
  }

  hcb(tree->data, hit_data, tmin);

  if (tree->right) {
    traverse_tree(tree->right, hcb, hit_data, tmin);
  }
}

static void free_tree(node_tree *tree)
{
  if (tree->left) {
    free_tree(tree->left);
    tree->left = NULL;
  }

  if (tree->right) {
    free_tree(tree->right);
    tree->right = NULL;
  }

  free(tree);
}

float BKE_pbvh_node_get_tmin(PBVHNode *node)
{
  return node->tmin;
}

static void BKE_pbvh_search_callback_occluded(PBVH *pbvh,
                                              BKE_pbvh_SearchCallback scb,
                                              void *search_data,
                                              BKE_pbvh_HitOccludedCallback hcb,
                                              void *hit_data)
{
  PBVHIter iter;
  PBVHNode *node;
  node_tree *tree = NULL;

  pbvh_iter_begin(&iter, pbvh, scb, search_data);

  while ((node = pbvh_iter_next_occluded(&iter))) {
    if (node->flag & PBVH_Leaf) {
      node_tree *new_node = malloc(sizeof(node_tree));

      new_node->data = node;

      new_node->left = NULL;
      new_node->right = NULL;

      if (tree) {
        node_tree_insert(tree, new_node);
      }
      else {
        tree = new_node;
      }
    }
  }

  pbvh_iter_end(&iter);

  if (tree) {
    float tmin = FLT_MAX;
    traverse_tree(tree, hcb, hit_data, &tmin);
    free_tree(tree);
  }
}

static bool update_search_cb(PBVHNode *node, void *data_v)
{
  int flag = POINTER_AS_INT(data_v);

  if (node->flag & PBVH_Leaf) {
    return (node->flag & flag) != 0;
  }

  return true;
}

typedef struct PBVHUpdateData {
  PBVH *pbvh;
  PBVHNode **nodes;
  int totnode;

  float (*vnors)[3];
  int flag;
  bool show_sculpt_face_sets;
} PBVHUpdateData;

static void pbvh_update_normals_clear_task_cb(void *__restrict userdata,
                                              const int n,
                                              const TaskParallelTLS *__restrict UNUSED(tls))
{
  PBVHUpdateData *data = userdata;
  PBVH *pbvh = data->pbvh;
  PBVHNode *node = data->nodes[n];
  float(*vnors)[3] = data->vnors;

  if (node->flag & PBVH_UpdateNormals) {
    const int *verts = node->vert_indices;
    const int totvert = node->uniq_verts;
    for (int i = 0; i < totvert; i++) {
      const int v = verts[i];
      if (BLI_BITMAP_TEST(pbvh->vert_bitmap, v)) {
        zero_v3(vnors[v]);
      }
    }
  }
}

static void pbvh_update_normals_accum_task_cb(void *__restrict userdata,
                                              const int n,
                                              const TaskParallelTLS *__restrict UNUSED(tls))
{
  PBVHUpdateData *data = userdata;

  PBVH *pbvh = data->pbvh;
  PBVHNode *node = data->nodes[n];
  float(*vnors)[3] = data->vnors;

  if (node->flag & PBVH_UpdateNormals) {
    unsigned int mpoly_prev = UINT_MAX;
    float fn[3];

    const int *faces = node->prim_indices;
    const int totface = node->totprim;

    for (int i = 0; i < totface; i++) {
      const MLoopTri *lt = &pbvh->looptri[faces[i]];
      const unsigned int vtri[3] = {
          pbvh->mloop[lt->tri[0]].v,
          pbvh->mloop[lt->tri[1]].v,
          pbvh->mloop[lt->tri[2]].v,
      };
      const int sides = 3;

      /* Face normal and mask */
      if (lt->poly != mpoly_prev) {
        const MPoly *mp = &pbvh->mpoly[lt->poly];
        BKE_mesh_calc_poly_normal(mp, &pbvh->mloop[mp->loopstart], pbvh->verts, fn);
        mpoly_prev = lt->poly;
      }

      for (int j = sides; j--;) {
        const int v = vtri[j];

        if (BLI_BITMAP_TEST(pbvh->vert_bitmap, v)) {
          /* NOTE: This avoids `lock, add_v3_v3, unlock`
           * and is five to ten times quicker than a spin-lock.
           * Not exact equivalent though, since atomicity is only ensured for one component
           * of the vector at a time, but here it shall not make any sensible difference. */
          for (int k = 3; k--;) {
            atomic_add_and_fetch_fl(&vnors[v][k], fn[k]);
          }
        }
      }
    }
  }
}

static void pbvh_update_normals_store_task_cb(void *__restrict userdata,
                                              const int n,
                                              const TaskParallelTLS *__restrict UNUSED(tls))
{
  PBVHUpdateData *data = userdata;
  PBVH *pbvh = data->pbvh;
  PBVHNode *node = data->nodes[n];
  float(*vnors)[3] = data->vnors;

  if (node->flag & PBVH_UpdateNormals) {
    const int *verts = node->vert_indices;
    const int totvert = node->uniq_verts;

    for (int i = 0; i < totvert; i++) {
      const int v = verts[i];

      /* No atomics necessary because we are iterating over uniq_verts only,
       * so we know only this thread will handle this vertex. */
      if (BLI_BITMAP_TEST(pbvh->vert_bitmap, v)) {
        normalize_v3(vnors[v]);
        BLI_BITMAP_DISABLE(pbvh->vert_bitmap, v);
      }
    }

    node->flag &= ~PBVH_UpdateNormals;
  }
}

static void pbvh_faces_update_normals(PBVH *pbvh, PBVHNode **nodes, int totnode)
{
  /* subtle assumptions:
   * - We know that for all edited vertices, the nodes with faces
   *   adjacent to these vertices have been marked with PBVH_UpdateNormals.
   *   This is true because if the vertex is inside the brush radius, the
   *   bounding box of its adjacent faces will be as well.
   * - However this is only true for the vertices that have actually been
   *   edited, not for all vertices in the nodes marked for update, so we
   *   can only update vertices marked in the `vert_bitmap`.
   */

  PBVHUpdateData data = {
      .pbvh = pbvh,
      .nodes = nodes,
      .vnors = pbvh->vert_normals,
  };

  TaskParallelSettings settings;
  BKE_pbvh_parallel_range_settings(&settings, true, totnode);

  /* Zero normals before accumulation. */
  BLI_task_parallel_range(0, totnode, &data, pbvh_update_normals_clear_task_cb, &settings);
  BLI_task_parallel_range(0, totnode, &data, pbvh_update_normals_accum_task_cb, &settings);
  BLI_task_parallel_range(0, totnode, &data, pbvh_update_normals_store_task_cb, &settings);
}

static void pbvh_update_mask_redraw_task_cb(void *__restrict userdata,
                                            const int n,
                                            const TaskParallelTLS *__restrict UNUSED(tls))
{

  PBVHUpdateData *data = userdata;
  PBVH *pbvh = data->pbvh;
  PBVHNode *node = data->nodes[n];
  if (node->flag & PBVH_UpdateMask) {

    bool has_unmasked = false;
    bool has_masked = true;
    if (node->flag & PBVH_Leaf) {
      PBVHVertexIter vd;

      BKE_pbvh_vertex_iter_begin (pbvh, node, vd, PBVH_ITER_ALL) {
        if (vd.mask && *vd.mask < 1.0f) {
          has_unmasked = true;
        }
        if (vd.mask && *vd.mask > 0.0f) {
          has_masked = false;
        }
      }
      BKE_pbvh_vertex_iter_end;
    }
    else {
      has_unmasked = true;
      has_masked = true;
    }
    BKE_pbvh_node_fully_masked_set(node, !has_unmasked);
    BKE_pbvh_node_fully_unmasked_set(node, has_masked);

    node->flag &= ~PBVH_UpdateMask;
  }
}

static void pbvh_update_mask_redraw(PBVH *pbvh, PBVHNode **nodes, int totnode, int flag)
{
  PBVHUpdateData data = {
      .pbvh = pbvh,
      .nodes = nodes,
      .flag = flag,
  };

  TaskParallelSettings settings;
  BKE_pbvh_parallel_range_settings(&settings, true, totnode);
  BLI_task_parallel_range(0, totnode, &data, pbvh_update_mask_redraw_task_cb, &settings);
}

static void pbvh_update_visibility_redraw_task_cb(void *__restrict userdata,
                                                  const int n,
                                                  const TaskParallelTLS *__restrict UNUSED(tls))
{

  PBVHUpdateData *data = userdata;
  PBVH *pbvh = data->pbvh;
  PBVHNode *node = data->nodes[n];
  if (node->flag & PBVH_UpdateVisibility) {
    node->flag &= ~PBVH_UpdateVisibility;
    BKE_pbvh_node_fully_hidden_set(node, true);
    if (node->flag & PBVH_Leaf) {
      PBVHVertexIter vd;
      BKE_pbvh_vertex_iter_begin (pbvh, node, vd, PBVH_ITER_ALL) {
        if (vd.visible) {
          BKE_pbvh_node_fully_hidden_set(node, false);
          return;
        }
      }
      BKE_pbvh_vertex_iter_end;
    }
  }
}

static void pbvh_update_visibility_redraw(PBVH *pbvh, PBVHNode **nodes, int totnode, int flag)
{
  PBVHUpdateData data = {
      .pbvh = pbvh,
      .nodes = nodes,
      .flag = flag,
  };

  TaskParallelSettings settings;
  BKE_pbvh_parallel_range_settings(&settings, true, totnode);
  BLI_task_parallel_range(0, totnode, &data, pbvh_update_visibility_redraw_task_cb, &settings);
}

static void pbvh_update_BB_redraw_task_cb(void *__restrict userdata,
                                          const int n,
                                          const TaskParallelTLS *__restrict UNUSED(tls))
{
  PBVHUpdateData *data = userdata;
  PBVH *pbvh = data->pbvh;
  PBVHNode *node = data->nodes[n];
  const int flag = data->flag;

  if ((flag & PBVH_UpdateBB) && (node->flag & PBVH_UpdateBB)) {
    /* don't clear flag yet, leave it for flushing later */
    /* Note that bvh usage is read-only here, so no need to thread-protect it. */
    update_node_vb(pbvh, node);
  }

  if ((flag & PBVH_UpdateOriginalBB) && (node->flag & PBVH_UpdateOriginalBB)) {
    node->orig_vb = node->vb;
  }

  if ((flag & PBVH_UpdateRedraw) && (node->flag & PBVH_UpdateRedraw)) {
    node->flag &= ~PBVH_UpdateRedraw;
  }
}

void pbvh_update_BB_redraw(PBVH *pbvh, PBVHNode **nodes, int totnode, int flag)
{
  /* update BB, redraw flag */
  PBVHUpdateData data = {
      .pbvh = pbvh,
      .nodes = nodes,
      .flag = flag,
  };

  TaskParallelSettings settings;
  BKE_pbvh_parallel_range_settings(&settings, true, totnode);
  BLI_task_parallel_range(0, totnode, &data, pbvh_update_BB_redraw_task_cb, &settings);
}

static int pbvh_get_buffers_update_flags(PBVH *UNUSED(pbvh))
{
  int update_flags = GPU_PBVH_BUFFERS_SHOW_VCOL | GPU_PBVH_BUFFERS_SHOW_MASK |
                     GPU_PBVH_BUFFERS_SHOW_SCULPT_FACE_SETS;
  return update_flags;
}

static void pbvh_update_draw_buffer_cb(void *__restrict userdata,
                                       const int n,
                                       const TaskParallelTLS *__restrict UNUSED(tls))
{
  /* Create and update draw buffers. The functions called here must not
   * do any OpenGL calls. Flags are not cleared immediately, that happens
   * after GPU_pbvh_buffer_flush() which does the final OpenGL calls. */
  PBVHUpdateData *data = userdata;
  PBVH *pbvh = data->pbvh;
  PBVHNode *node = data->nodes[n];

  if (node->flag & PBVH_RebuildDrawBuffers) {
    switch (pbvh->type) {
      case PBVH_GRIDS:
        node->draw_buffers = GPU_pbvh_grid_buffers_build(node->totprim, pbvh->grid_hidden);
        break;
      case PBVH_FACES:
        node->draw_buffers = GPU_pbvh_mesh_buffers_build(
            pbvh->mpoly,
            pbvh->mloop,
            pbvh->looptri,
            pbvh->verts,
            node->prim_indices,
            CustomData_get_layer(pbvh->pdata, CD_SCULPT_FACE_SETS),
            node->totprim,
            pbvh->mesh);
        break;
      case PBVH_BMESH:
        node->draw_buffers = GPU_pbvh_bmesh_buffers_build(pbvh->flags &
                                                          PBVH_DYNTOPO_SMOOTH_SHADING);
        break;
    }
  }

  if (node->flag & PBVH_UpdateDrawBuffers) {
    const int update_flags = pbvh_get_buffers_update_flags(pbvh);
    switch (pbvh->type) {
      case PBVH_GRIDS:
        GPU_pbvh_grid_buffers_update(node->draw_buffers,
                                     pbvh->subdiv_ccg,
                                     pbvh->grids,
                                     pbvh->grid_flag_mats,
                                     node->prim_indices,
                                     node->totprim,
                                     pbvh->face_sets,
                                     pbvh->face_sets_color_seed,
                                     pbvh->face_sets_color_default,
                                     &pbvh->gridkey,
                                     update_flags);
        break;
      case PBVH_FACES:
        GPU_pbvh_mesh_buffers_update(node->draw_buffers,
                                     pbvh->verts,
                                     pbvh->vert_normals,
                                     CustomData_get_layer(pbvh->vdata, CD_PAINT_MASK),
                                     CustomData_get_layer(pbvh->ldata, CD_MLOOPCOL),
                                     CustomData_get_layer(pbvh->pdata, CD_SCULPT_FACE_SETS),
                                     pbvh->face_sets_color_seed,
                                     pbvh->face_sets_color_default,
                                     CustomData_get_layer(pbvh->vdata, CD_PROP_COLOR),
                                     update_flags);
        break;
      case PBVH_BMESH:
        GPU_pbvh_bmesh_buffers_update(node->draw_buffers,
                                      pbvh->bm,
                                      node->bm_faces,
                                      node->bm_unique_verts,
                                      node->bm_other_verts,
                                      update_flags);
        break;
    }
  }
}

static void pbvh_update_draw_buffers(PBVH *pbvh, PBVHNode **nodes, int totnode, int update_flag)
{
  if ((update_flag & PBVH_RebuildDrawBuffers) || ELEM(pbvh->type, PBVH_GRIDS, PBVH_BMESH)) {
    /* Free buffers uses OpenGL, so not in parallel. */
    for (int n = 0; n < totnode; n++) {
      PBVHNode *node = nodes[n];
      if (node->flag & PBVH_RebuildDrawBuffers) {
        GPU_pbvh_buffers_free(node->draw_buffers);
        node->draw_buffers = NULL;
      }
      else if ((node->flag & PBVH_UpdateDrawBuffers) && node->draw_buffers) {
        if (pbvh->type == PBVH_GRIDS) {
          GPU_pbvh_grid_buffers_update_free(
              node->draw_buffers, pbvh->grid_flag_mats, node->prim_indices);
        }
        else if (pbvh->type == PBVH_BMESH) {
          GPU_pbvh_bmesh_buffers_update_free(node->draw_buffers);
        }
      }
    }
  }

  /* Parallel creation and update of draw buffers. */
  PBVHUpdateData data = {
      .pbvh = pbvh,
      .nodes = nodes,
  };

  TaskParallelSettings settings;
  BKE_pbvh_parallel_range_settings(&settings, true, totnode);
  BLI_task_parallel_range(0, totnode, &data, pbvh_update_draw_buffer_cb, &settings);

  for (int i = 0; i < totnode; i++) {
    PBVHNode *node = nodes[i];

    if (node->flag & PBVH_UpdateDrawBuffers) {
      /* Flush buffers uses OpenGL, so not in parallel. */
      GPU_pbvh_buffers_update_flush(node->draw_buffers);
    }

    node->flag &= ~(PBVH_RebuildDrawBuffers | PBVH_UpdateDrawBuffers);
  }
}

static int pbvh_flush_bb(PBVH *pbvh, PBVHNode *node, int flag)
{
  int update = 0;

  /* Difficult to multi-thread well, we just do single threaded recursive. */
  if (node->flag & PBVH_Leaf) {
    if (flag & PBVH_UpdateBB) {
      update |= (node->flag & PBVH_UpdateBB);
      node->flag &= ~PBVH_UpdateBB;
    }

    if (flag & PBVH_UpdateOriginalBB) {
      update |= (node->flag & PBVH_UpdateOriginalBB);
      node->flag &= ~PBVH_UpdateOriginalBB;
    }

    return update;
  }

  update |= pbvh_flush_bb(pbvh, pbvh->nodes + node->children_offset, flag);
  update |= pbvh_flush_bb(pbvh, pbvh->nodes + node->children_offset + 1, flag);

  if (update & PBVH_UpdateBB) {
    update_node_vb(pbvh, node);
  }
  if (update & PBVH_UpdateOriginalBB) {
    node->orig_vb = node->vb;
  }

  return update;
}

void BKE_pbvh_update_bounds(PBVH *pbvh, int flag)
{
  if (!pbvh->nodes) {
    return;
  }

  PBVHNode **nodes;
  int totnode;

  BKE_pbvh_search_gather(pbvh, update_search_cb, POINTER_FROM_INT(flag), &nodes, &totnode);

  if (flag & (PBVH_UpdateBB | PBVH_UpdateOriginalBB | PBVH_UpdateRedraw)) {
    pbvh_update_BB_redraw(pbvh, nodes, totnode, flag);
  }

  if (flag & (PBVH_UpdateBB | PBVH_UpdateOriginalBB)) {
    pbvh_flush_bb(pbvh, pbvh->nodes, flag);
  }

  MEM_SAFE_FREE(nodes);
}

void BKE_pbvh_update_vertex_data(PBVH *pbvh, int flag)
{
  if (!pbvh->nodes) {
    return;
  }

  PBVHNode **nodes;
  int totnode;

  BKE_pbvh_search_gather(pbvh, update_search_cb, POINTER_FROM_INT(flag), &nodes, &totnode);

  if (flag & (PBVH_UpdateMask)) {
    pbvh_update_mask_redraw(pbvh, nodes, totnode, flag);
  }

  if (flag & (PBVH_UpdateColor)) {
    /* Do nothing */
  }

  if (flag & (PBVH_UpdateVisibility)) {
    pbvh_update_visibility_redraw(pbvh, nodes, totnode, flag);
  }

  if (nodes) {
    MEM_freeN(nodes);
  }
}

static void pbvh_faces_node_visibility_update(PBVH *pbvh, PBVHNode *node)
{
  MVert *mvert;
  const int *vert_indices;
  int totvert, i;
  BKE_pbvh_node_num_verts(pbvh, node, NULL, &totvert);
  BKE_pbvh_node_get_verts(pbvh, node, &vert_indices, &mvert);

  for (i = 0; i < totvert; i++) {
    MVert *v = &mvert[vert_indices[i]];
    if (!(v->flag & ME_HIDE)) {
      BKE_pbvh_node_fully_hidden_set(node, false);
      return;
    }
  }

  BKE_pbvh_node_fully_hidden_set(node, true);
}

static void pbvh_grids_node_visibility_update(PBVH *pbvh, PBVHNode *node)
{
  CCGElem **grids;
  BLI_bitmap **grid_hidden;
  int *grid_indices, totgrid, i;

  BKE_pbvh_node_get_grids(pbvh, node, &grid_indices, &totgrid, NULL, NULL, &grids);
  grid_hidden = BKE_pbvh_grid_hidden(pbvh);
  CCGKey key = *BKE_pbvh_get_grid_key(pbvh);

  for (i = 0; i < totgrid; i++) {
    int g = grid_indices[i], x, y;
    BLI_bitmap *gh = grid_hidden[g];

    if (!gh) {
      BKE_pbvh_node_fully_hidden_set(node, false);
      return;
    }

    for (y = 0; y < key.grid_size; y++) {
      for (x = 0; x < key.grid_size; x++) {
        if (!BLI_BITMAP_TEST(gh, y * key.grid_size + x)) {
          BKE_pbvh_node_fully_hidden_set(node, false);
          return;
        }
      }
    }
  }
  BKE_pbvh_node_fully_hidden_set(node, true);
}

static void pbvh_bmesh_node_visibility_update(PBVHNode *node)
{
  GSet *unique, *other;

  unique = BKE_pbvh_bmesh_node_unique_verts(node);
  other = BKE_pbvh_bmesh_node_other_verts(node);

  GSetIterator gs_iter;

  GSET_ITER (gs_iter, unique) {
    BMVert *v = BLI_gsetIterator_getKey(&gs_iter);
    if (!BM_elem_flag_test(v, BM_ELEM_HIDDEN)) {
      BKE_pbvh_node_fully_hidden_set(node, false);
      return;
    }
  }

  GSET_ITER (gs_iter, other) {
    BMVert *v = BLI_gsetIterator_getKey(&gs_iter);
    if (!BM_elem_flag_test(v, BM_ELEM_HIDDEN)) {
      BKE_pbvh_node_fully_hidden_set(node, false);
      return;
    }
  }

  BKE_pbvh_node_fully_hidden_set(node, true);
}

static void pbvh_update_visibility_task_cb(void *__restrict userdata,
                                           const int n,
                                           const TaskParallelTLS *__restrict UNUSED(tls))
{

  PBVHUpdateData *data = userdata;
  PBVH *pbvh = data->pbvh;
  PBVHNode *node = data->nodes[n];
  if (node->flag & PBVH_UpdateVisibility) {
    switch (BKE_pbvh_type(pbvh)) {
      case PBVH_FACES:
        pbvh_faces_node_visibility_update(pbvh, node);
        break;
      case PBVH_GRIDS:
        pbvh_grids_node_visibility_update(pbvh, node);
        break;
      case PBVH_BMESH:
        pbvh_bmesh_node_visibility_update(node);
        break;
    }
    node->flag &= ~PBVH_UpdateVisibility;
  }
}

static void pbvh_update_visibility(PBVH *pbvh, PBVHNode **nodes, int totnode)
{
  PBVHUpdateData data = {
      .pbvh = pbvh,
      .nodes = nodes,
  };

  TaskParallelSettings settings;
  BKE_pbvh_parallel_range_settings(&settings, true, totnode);
  BLI_task_parallel_range(0, totnode, &data, pbvh_update_visibility_task_cb, &settings);
}

void BKE_pbvh_update_visibility(PBVH *pbvh)
{
  if (!pbvh->nodes) {
    return;
  }

  PBVHNode **nodes;
  int totnode;

  BKE_pbvh_search_gather(
      pbvh, update_search_cb, POINTER_FROM_INT(PBVH_UpdateVisibility), &nodes, &totnode);
  pbvh_update_visibility(pbvh, nodes, totnode);

  if (nodes) {
    MEM_freeN(nodes);
  }
}

void BKE_pbvh_redraw_BB(PBVH *pbvh, float bb_min[3], float bb_max[3])
{
  PBVHIter iter;
  PBVHNode *node;
  BB bb;

  BB_reset(&bb);

  pbvh_iter_begin(&iter, pbvh, NULL, NULL);

  while ((node = pbvh_iter_next(&iter))) {
    if (node->flag & PBVH_UpdateRedraw) {
      BB_expand_with_bb(&bb, &node->vb);
    }
  }

  pbvh_iter_end(&iter);

  copy_v3_v3(bb_min, bb.bmin);
  copy_v3_v3(bb_max, bb.bmax);
}

void BKE_pbvh_get_grid_updates(PBVH *pbvh, bool clear, void ***r_gridfaces, int *r_totface)
{
  GSet *face_set = BLI_gset_ptr_new(__func__);
  PBVHNode *node;
  PBVHIter iter;

  pbvh_iter_begin(&iter, pbvh, NULL, NULL);

  while ((node = pbvh_iter_next(&iter))) {
    if (node->flag & PBVH_UpdateNormals) {
      for (uint i = 0; i < node->totprim; i++) {
        void *face = pbvh->gridfaces[node->prim_indices[i]];
        BLI_gset_add(face_set, face);
      }

      if (clear) {
        node->flag &= ~PBVH_UpdateNormals;
      }
    }
  }

  pbvh_iter_end(&iter);

  const int tot = BLI_gset_len(face_set);
  if (tot == 0) {
    *r_totface = 0;
    *r_gridfaces = NULL;
    BLI_gset_free(face_set, NULL);
    return;
  }

  void **faces = MEM_mallocN(sizeof(*faces) * tot, "PBVH Grid Faces");

  GSetIterator gs_iter;
  int i;
  GSET_ITER_INDEX (gs_iter, face_set, i) {
    faces[i] = BLI_gsetIterator_getKey(&gs_iter);
  }

  BLI_gset_free(face_set, NULL);

  *r_totface = tot;
  *r_gridfaces = faces;
}

/***************************** PBVH Access ***********************************/

PBVHType BKE_pbvh_type(const PBVH *pbvh)
{
  return pbvh->type;
}

bool BKE_pbvh_has_faces(const PBVH *pbvh)
{
  if (pbvh->type == PBVH_BMESH) {
    return (pbvh->bm->totface != 0);
  }

  return (pbvh->totprim != 0);
}

void BKE_pbvh_bounding_box(const PBVH *pbvh, float min[3], float max[3])
{
  if (pbvh->totnode) {
    const BB *bb = &pbvh->nodes[0].vb;
    copy_v3_v3(min, bb->bmin);
    copy_v3_v3(max, bb->bmax);
  }
  else {
    zero_v3(min);
    zero_v3(max);
  }
}

BLI_bitmap **BKE_pbvh_grid_hidden(const PBVH *pbvh)
{
  BLI_assert(pbvh->type == PBVH_GRIDS);
  return pbvh->grid_hidden;
}

const CCGKey *BKE_pbvh_get_grid_key(const PBVH *pbvh)
{
  BLI_assert(pbvh->type == PBVH_GRIDS);
  return &pbvh->gridkey;
}

struct CCGElem **BKE_pbvh_get_grids(const PBVH *pbvh)
{
  BLI_assert(pbvh->type == PBVH_GRIDS);
  return pbvh->grids;
}

BLI_bitmap **BKE_pbvh_get_grid_visibility(const PBVH *pbvh)
{
  BLI_assert(pbvh->type == PBVH_GRIDS);
  return pbvh->grid_hidden;
}

int BKE_pbvh_get_grid_num_vertices(const PBVH *pbvh)
{
  BLI_assert(pbvh->type == PBVH_GRIDS);
  return pbvh->totgrid * pbvh->gridkey.grid_area;
}

int BKE_pbvh_get_grid_num_faces(const PBVH *pbvh)
{
  BLI_assert(pbvh->type == PBVH_GRIDS);
  return pbvh->totgrid * (pbvh->gridkey.grid_size - 1) * (pbvh->gridkey.grid_size - 1);
}

BMesh *BKE_pbvh_get_bmesh(PBVH *pbvh)
{
  BLI_assert(pbvh->type == PBVH_BMESH);
  return pbvh->bm;
}

/***************************** Node Access ***********************************/

void BKE_pbvh_node_mark_update(PBVHNode *node)
{
  node->flag |= PBVH_UpdateNormals | PBVH_UpdateBB | PBVH_UpdateOriginalBB |
                PBVH_UpdateDrawBuffers | PBVH_UpdateRedraw;
}

void BKE_pbvh_node_mark_update_mask(PBVHNode *node)
{
  node->flag |= PBVH_UpdateMask | PBVH_UpdateDrawBuffers | PBVH_UpdateRedraw;
}

void BKE_pbvh_node_mark_update_color(PBVHNode *node)
{
  node->flag |= PBVH_UpdateColor | PBVH_UpdateDrawBuffers | PBVH_UpdateRedraw;
}

void BKE_pbvh_node_mark_update_visibility(PBVHNode *node)
{
  node->flag |= PBVH_UpdateVisibility | PBVH_RebuildDrawBuffers | PBVH_UpdateDrawBuffers |
                PBVH_UpdateRedraw;
}

void BKE_pbvh_node_mark_rebuild_draw(PBVHNode *node)
{
  node->flag |= PBVH_RebuildDrawBuffers | PBVH_UpdateDrawBuffers | PBVH_UpdateRedraw;
}

void BKE_pbvh_node_mark_redraw(PBVHNode *node)
{
  node->flag |= PBVH_UpdateDrawBuffers | PBVH_UpdateRedraw;
}

void BKE_pbvh_node_mark_normals_update(PBVHNode *node)
{
  node->flag |= PBVH_UpdateNormals;
}

void BKE_pbvh_node_fully_hidden_set(PBVHNode *node, int fully_hidden)
{
  BLI_assert(node->flag & PBVH_Leaf);

  if (fully_hidden) {
    node->flag |= PBVH_FullyHidden;
  }
  else {
    node->flag &= ~PBVH_FullyHidden;
  }
}

bool BKE_pbvh_node_fully_hidden_get(PBVHNode *node)
{
  return (node->flag & PBVH_Leaf) && (node->flag & PBVH_FullyHidden);
}

void BKE_pbvh_node_fully_masked_set(PBVHNode *node, int fully_masked)
{
  BLI_assert(node->flag & PBVH_Leaf);

  if (fully_masked) {
    node->flag |= PBVH_FullyMasked;
  }
  else {
    node->flag &= ~PBVH_FullyMasked;
  }
}

bool BKE_pbvh_node_fully_masked_get(PBVHNode *node)
{
  return (node->flag & PBVH_Leaf) && (node->flag & PBVH_FullyMasked);
}

void BKE_pbvh_node_fully_unmasked_set(PBVHNode *node, int fully_masked)
{
  BLI_assert(node->flag & PBVH_Leaf);

  if (fully_masked) {
    node->flag |= PBVH_FullyUnmasked;
  }
  else {
    node->flag &= ~PBVH_FullyUnmasked;
  }
}

bool BKE_pbvh_node_fully_unmasked_get(PBVHNode *node)
{
  return (node->flag & PBVH_Leaf) && (node->flag & PBVH_FullyUnmasked);
}

void BKE_pbvh_vert_mark_update(PBVH *pbvh, int index)
{
  BLI_assert(pbvh->type == PBVH_FACES);
  BLI_BITMAP_ENABLE(pbvh->vert_bitmap, index);
}

void BKE_pbvh_node_get_verts(PBVH *pbvh,
                             PBVHNode *node,
                             const int **r_vert_indices,
                             MVert **r_verts)
{
  if (r_vert_indices) {
    *r_vert_indices = node->vert_indices;
  }

  if (r_verts) {
    *r_verts = pbvh->verts;
  }
}

void BKE_pbvh_node_num_verts(PBVH *pbvh, PBVHNode *node, int *r_uniquevert, int *r_totvert)
{
  int tot;

  switch (pbvh->type) {
    case PBVH_GRIDS:
      tot = node->totprim * pbvh->gridkey.grid_area;
      if (r_totvert) {
        *r_totvert = tot;
      }
      if (r_uniquevert) {
        *r_uniquevert = tot;
      }
      break;
    case PBVH_FACES:
      if (r_totvert) {
        *r_totvert = node->uniq_verts + node->face_verts;
      }
      if (r_uniquevert) {
        *r_uniquevert = node->uniq_verts;
      }
      break;
    case PBVH_BMESH:
      tot = BLI_gset_len(node->bm_unique_verts);
      if (r_totvert) {
        *r_totvert = tot + BLI_gset_len(node->bm_other_verts);
      }
      if (r_uniquevert) {
        *r_uniquevert = tot;
      }
      break;
  }
}

void BKE_pbvh_node_get_grids(PBVH *pbvh,
                             PBVHNode *node,
                             int **r_grid_indices,
                             int *r_totgrid,
                             int *r_maxgrid,
                             int *r_gridsize,
                             CCGElem ***r_griddata)
{
  switch (pbvh->type) {
    case PBVH_GRIDS:
      if (r_grid_indices) {
        *r_grid_indices = node->prim_indices;
      }
      if (r_totgrid) {
        *r_totgrid = node->totprim;
      }
      if (r_maxgrid) {
        *r_maxgrid = pbvh->totgrid;
      }
      if (r_gridsize) {
        *r_gridsize = pbvh->gridkey.grid_size;
      }
      if (r_griddata) {
        *r_griddata = pbvh->grids;
      }
      break;
    case PBVH_FACES:
    case PBVH_BMESH:
      if (r_grid_indices) {
        *r_grid_indices = NULL;
      }
      if (r_totgrid) {
        *r_totgrid = 0;
      }
      if (r_maxgrid) {
        *r_maxgrid = 0;
      }
      if (r_gridsize) {
        *r_gridsize = 0;
      }
      if (r_griddata) {
        *r_griddata = NULL;
      }
      break;
  }
}

