/**
 * Generate data needed for partially updating mesh information.
 * Currently this is used for normals and tessellation.
 *
 * Transform is the obvious use case where there is no need to update normals or tessellation
 * for geometry which has not been modified.
 *
 * In the future this could be integrated into GPU updates too.
 *
 * Kinds of Partial Geometry
 * =========================
 *
 * All Tagged
 * ----------
 * Operate on everything that's tagged as well as connected geometry.
 * see: mesh_partial_create_from_verts
 *
 * Grouped
 * -------
 * Operate on everything that is connected to both tagged and un-tagged.
 * see: mesh_partial_create_from_verts_group_single
 *
 * Reduces computations when transforming isolated regions.
 *
 * Optionally support multiple groups since axis-mirror (for example)
 * will transform vertices in different directions, as well as keeping centered vertices.
 * see: mesh_partial_create_from_verts_group_multi
 *
 * note Others can be added as needed.
 */

#include "types_object.h"

#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_bitmap.h"
#include "lib_math_vector.h"

#include "mesh.h"

/**
 * Grow by 1.5x (rounding up).
 *
 * note Use conservative reallocation since the initial sizes reserved
 * may be close to (or exactly) the number of elements needed.
 */
#define GROW(len_alloc) ((len_alloc) + ((len_alloc) - ((len_alloc) / 2)))
#define GROW_ARRAY(mem, len_alloc) \
  { \
    mem = mem_reallocn(mem, (sizeof(*mem)) * ((len_alloc) = GROW(len_alloc))); \
  } \
  ((void)0)

#define GROW_ARRAY_AS_NEEDED(mem, len_alloc, index) \
  if (UNLIKELY(len_alloc == index)) { \
    GROW_ARRAY(mem, len_alloc); \
  }

LIB_INLINE bool partial_elem_vert_ensure(BMPartialUpdate *bmpinfo,
                                         BLI_bitmap *verts_tag,
                                         BMVert *v)
{
  const int i = mesh_elem_index_get(v);
  if (!LIB_BITMAP_TEST(verts_tag, i)) {
    LIB_BITMAP_ENABLE(verts_tag, i);
    GROW_ARRAY_AS_NEEDED(meshinfo->verts, meshinfo->verts_len_alloc, meshinfo->verts_len);
    meshinfo->verts[meshinfo->verts_len++] = v;
    return true;
  }
  return false;
}

LIB_INLINE bool partial_elem_face_ensure(MeshPartialUpdate *meshinfo,
                                         lib_bitmap *faces_tag,
                                         MeshFace *f)
{
  const int i = mesh_elem_index_get(f);
  if (!LIB_BITMAP_TEST(faces_tag, i)) {
    LIB_BITMAP_ENABLE(faces_tag, i);
    GROW_ARRAY_AS_NEEDED(meshinfo->faces, meshinfo->faces_len_alloc, meshinfo->faces_len);
    meshinfo->faces[meshinfo->faces_len++] = f;
    return true;
  }
  return false;
}

MeshPartialUpdate *mesh_mesh_partial_create_from_verts(Mesh *mesh,
                                                       const MeshPartialUpdateParams *params,
                                                       const lib_bitmap *verts_mask,
                                                       const int verts_mask_count)
{
  /* The caller is doing something wrong if this isn't the case. */
  lib_assert(verts_mask_count <= mesh->totvert);

  MeshPartialUpdate *meshinfo = mem_callocn(sizeof(*bmpinfo), __func__);

  /* Reserve more edges than vertices since it's common for a grid topology
   * to use around twice as many edges as vertices. */
  const int default_verts_len_alloc = verts_mask_count;
  const int default_faces_len_alloc = min_ii(bm->totface, verts_mask_count);

  /* Allocate tags instead of using MESH_ELEM_TAG because the caller may already be using tags.
   * Further, walking over all geometry to clear the tags isn't so efficient. */
  lib_bitmap *verts_tag = NULL;
  lib_bitmap *faces_tag = NULL;

  /* Set vert inline. */
  mesh_elem_index_ensure(mesh, MESH_FACE);

  if (params->do_normals || params->do_tessellate) {
    /* - Extend to all vertices connected faces:
     *   In the case of tessellation this is enough.
     *
     *   In the case of vertex normal calculation,
     *   All the relevant connectivity data can be accessed from the faces
     *   (there is no advantage in storing connected edges or vertices in this pass).
     *
     * NOTE: In the future it may be useful to differentiate between vertices
     * that are directly marked (by the filter function when looping over all vertices).
     * And vertices marked from indirect connections.
     * This would require an extra tag array, so avoid this unless it's needed.
     */

    /* Faces. */
    if (meshinfo->faces == NULL) {
      meshinfo->faces_len_alloc = default_faces_len_alloc;
      meshinfo->faces = mem_mallocn((sizeof(MeshFace *) * meshinfo->faces_len_alloc), __func__);
      faces_tag = LIB_BITMAP_NEW((size_t)mesh->totface, __func__);
    }

    MeshVert *v;
    MeshIter iter;
    int i;
    MESH_ITER_MESH_INDEX (v, &iter, mesh, MESH_VERTS_OF_MESH, i) {
      MESH_elem_index_set(v, i); /* set_inline */
      if (!LIB_BITMAP_TEST(verts_mask, i)) {
        continue;
      }
      MeshEdge *e_iter = v->e;
      if (e_iter != NULL) {
        /* Loop over edges. */
        MeshEdge *e_first = v->e;
        do {
          MeshLoop *l_iter = e_iter->l;
          if (e_iter->l != NULL) {
            MeshLoop *l_first = e_iter->l;
            /* Loop over radial loops. */
            do {
              if (l_iter->v == v) {
                partial_elem_face_ensure(meshinfo, faces_tag, l_iter->f);
              }
            } while ((l_iter = l_iter->radial_next) != l_first);
          }
        } while ((e_iter = MESH_DISK_EDGE_NEXT(e_iter, v)) != e_first);
      }
    }
  }

  if (params->do_normals) {
    /* - Extend to all faces vertices:
     *   Any changes to the faces normal needs to update all surrounding vertices.
     *
     * - Extend to all these vertices connected edges:
     *   These and needed to access those vertices edge vectors in normal calculation logic.
     */

    /* Vertices. */
    if (meshinfo->verts == NULL) {
      meshinfo->verts_len_alloc = default_verts_len_alloc;
      meshinfo->verts = mem_mallocn((sizeof(MeshVert *) * meshinfo->verts_len_alloc), __func__);
      verts_tag = LIB_BITMAP_NEW((size_t)mesh->totvert, __func__);
    }

    for (int i = 0; i < meshinfo->faces_len; i++) {
      MeshFace *f = meshinfo->faces[i];
      MeshLoop *l_iter, *l_first;
      l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
      do {
        partial_elem_vert_ensure(meshinfo, verts_tag, l_iter->v);
      } while ((l_iter = l_iter->next) != l_first);
    }
  }

  if (verts_tag) {
    mem_freen(verts_tag);
  }
  if (faces_tag) {
    mem_freen(faces_tag);
  }

  meshinfo->params = *params;

  return bmpinfo;
}

MeshPartialUpdate *mesh_partial_create_from_verts_group_single(
    Mesh *mesh,
    const MeshPartialUpdateParams *params,
    const lib_bitmap *verts_mask,
    const int verts_mask_count)
{
  MeshPartialUpdate *meshinfo = mem_callocn(sizeof(*meshinfo), __func__);

  lib_bitmap *verts_tag = NULL;
  lib_bitmap *faces_tag = NULL;

  /* It's not worth guessing a large number as isolated regions will allocate zero faces. */
  const int default_faces_len_alloc = 1;

  int face_tag_loop_len = 0;

  if (params->do_normals || params->do_tessellate) {

    /* Faces. */
    if (meshinfo->faces == NULL) {
      meshinfo->faces_len_alloc = default_faces_len_alloc;
      mesinfo->faces = mem_mallocn((sizeof(MeshFace *) * meshinfo->faces_len_alloc), __func__);
      faces_tag = LIB_BITMAP_NEW((size_t)bm->totface, __func__);
    }

    MeshFace *f;
    MeshIter iter;
    int i;
    MESH_ITER_MESH_INDEX (f, &iter, mesh, MESH_FACES_OF_MESH, i) {
      enum { SIDE_A = (1 << 0), SIDE_B = (1 << 1) } side_flag = 0;
      mesh_elem_index_set(f, i); /* set_inline */
      MeshLoop *l_iter, *l_first;
      l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
      do {
        const int j = mesh_elem_index_get(l_iter->v);
        side_flag |= LIB_BITMAP_TEST(verts_mask, j) ? SIDE_A : SIDE_B;
        if (UNLIKELY(side_flag == (SIDE_A | SIDE_B))) {
          partial_elem_face_ensure(meshinfo, faces_tag, f);
          face_tag_loop_len += f->len;
          break;
        }
      } while ((l_iter = l_iter->next) != l_first);
    }
  }

  if (params->do_normals) {
    /* Extend to all faces vertices:
     * Any changes to the faces normal needs to update all surrounding vertices. */

    /* Over allocate using the total number of face loops. */
    const int default_verts_len_alloc = min_ii(mesh->totvert, max_ii(1, face_tag_loop_len));

    /* Vertices. */
    if (meshinfo->verts == NULL) {
      meshinfo->verts_len_alloc = default_verts_len_alloc;
      meshinfo->verts = mem_mallocn((sizeof(MeshVert *) * meshinfo->verts_len_alloc), __func__);
      verts_tag = LIB_BITMAP_NEW((size_t)bm->totvert, __func__);
    }

    for (int i = 0; i < bmpinfo->faces_len; i++) {
      BMFace *f = bmpinfo->faces[i];
      BMLoop *l_iter, *l_first;
      l_iter = l_first = BM_FACE_FIRST_LOOP(f);
      do {
        partial_elem_vert_ensure(bmpinfo, verts_tag, l_iter->v);
      } while ((l_iter = l_iter->next) != l_first);
    }

    /* Loose vertex support, these need special handling as loose normals depend on location. */
    if (bmpinfo->verts_len < verts_mask_count) {
      BMVert *v;
      BMIter iter;
      int i;
      BM_ITER_MESH_INDEX (v, &iter, bm, BM_VERTS_OF_MESH, i) {
        if (BLI_BITMAP_TEST(verts_mask, i) && (BM_vert_find_first_loop(v) == NULL)) {
          partial_elem_vert_ensure(bmpinfo, verts_tag, v);
        }
      }
    }
  }

  if (verts_tag) {
    mem_freen(verts_tag);
  }
  if (faces_tag) {
    mem_freen(faces_tag);
  }

  meshinfo->params = *params;

  return meshinfo;
}

MeshPartialUpdate *mesh_partial_create_from_verts_group_multi(
    Mesh *mesh,
    const MeshPartialUpdatewParams *params,
    const int *verts_group,
    const int verts_group_count)
{
  /* Provide a quick way of visualizing which faces are being manipulated. */
  // #define DEBUG_MATERIAL

  MeshPartialUpdate *bmpinfo = mem_callocn(sizeof(*meshinfo), __func__);

  lib_bitmap *verts_tag = NULL;
  lib_bitmap *faces_tag = NULL;

  /* It's not worth guessing a large number as isolated regions will allocate zero faces. */
  const int default_faces_len_alloc = 1;

  int face_tag_loop_len = 0;

  if (params->do_normals || params->do_tessellate) {

    /* Faces. */
    if (meshinfo->faces == NULL) {
      meshinfo->faces_len_alloc = default_faces_len_alloc;
      meshinfo->faces = mem_mallocn((sizeof(MeshFace *) * meshinfo->faces_len_alloc), __func__);
      faces_tag = LIB_BITMAP_NEW((size_t)bm->totface, __func__);
    }

    MeshFace *f;
    MeshIter iter;
    int i;
    MESH_ITER_MESH_INDEX (f, &iter, mesh, MESH_FACES_OF_MESH, i) {
      mesh_elem_index_set(f, i); /* set_inline */
      MeshLoop *l_iter, *l_first;
      l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
      const int group_test = verts_group[mesh_elem_index_get(l_iter->prev->v)];
#ifdef DEBUG_MATERIAL
      f->mat_nr = 0;
#endif
      do {
        const int group_iter = verts_group[BM_elem_index_get(l_iter->v)];
        if (UNLIKELY((group_iter != group_test) || (group_iter == -1))) {
          partial_elem_face_ensure(meshinfo, faces_tag, f);
          face_tag_loop_len += f->len;
#ifdef DEBUG_MATERIAL
          f->mat_nr = 1;
#endif
          break;
        }
      } while ((l_iter = l_iter->next) != l_first);
    }
  }

  if (params->do_normals) {
    /* Extend to all faces vertices:
     * Any changes to the faces normal needs to update all surrounding vertices. */

    /* Over allocate using the total number of face loops. */
    const int default_verts_len_alloc = min_ii(bm->totvert, max_ii(1, face_tag_loop_len));

    /* Vertices. */
    if (meshinfo->verts == NULL) {
      meshinfo->verts_len_alloc = default_verts_len_alloc;
      meshinfo->verts = mem_mallocn((sizeof(MeshVert *) * meshinfo->verts_len_alloc), __func__);
      verts_tag = LIB_BITMAP_NEW((size_t)mesh->totvert, __func__);
    }

    for (int i = 0; i < meshinfo->faces_len; i++) {
      MeshFace *f = meshinfo->faces[i];
      MeshLoop *l_iter, *l_first;
      l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
      do {
        partial_elem_vert_ensure(meshinfo, verts_tag, l_iter->v);
      } while ((l_iter = l_iter->next) != l_first);
    }

    /* Loose vertex support, these need special handling as loose normals depend on location. */
    if (meshinfo->verts_len < verts_group_count) {
      MeshVert *v;
      MeshIter iter;
      int i;
      MESH_ITER_MESH_INDEX (v, &iter, mesh, MESH_VERTS_OF_MESH, i) {
        if ((verts_group[i] != 0) && (mesh_vert_find_first_loop(v) == NULL)) {
          partial_elem_vert_ensure(meshinfo, verts_tag, v);
        }
      }
    }
  }

  if (verts_tag) {
    mem_freen(verts_tag);
  }
  if (faces_tag) {
    mem_freen(faces_tag);
  }

  meshinfo->params = *params;

  return meshinfo;
}

void mesh_partial_destroy(MeshPartialUpdate *meshinfo)
{
  if (meshinfo->verts) {
    mem_freen(mesh->verts);
  }
  if (meshinfo->faces) {
    mem_freen(meshinfo->faces);
  }
  mem_freen(meshinfo);
}
