/**
 * Cut the geometry in half using a plane.
 *
 * par Implementation
 * This simply works by splitting tagged edges who's verts span either side of
 * the plane, then splitting faces along their dividing verts.
 * The only complex case is when a ngon spans the axis multiple times,
 * in this case we need to do some extra checks to correctly bisect the ngon.
 * see: mesh_face_bisect_verts
 */

#include <limits.h>

#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_linklist.h"
#include "lib_linklist_stack.h"
#include "lib_math.h"
#include "lib_utildefines.h"
#include "lib_utildefines_stack.h"

#include "mesh.h"
#include "mesh_bisect_plane.h" /* Own include. */

#include "lib_strict_flags.h" /* Keep last. */

/* -------------------------------------------------------------------- */
/** Math Functions **/

static short plane_point_test_v3(const float plane[4],
                                 const float co[3],
                                 const float eps,
                                 float *r_depth)
{
  const float f = plane_point_side_v3(plane, co);
  *r_depth = f;

  if (f <= -eps) {
    return -1;
  }
  if (f >= eps) {
    return 1;
  }
  return 0;
}

/* -------------------------------------------------------------------- */
/** Mesh Element Accessors
 *
 * Wrappers to hide internal data-structure abuse,
 * later we may want to move this into some hash lookup
 * to a separate struct, but for now we can store in Mesh data.
 **/

#define MESH_VERT_DIR(v) ((short *)(&(v)->head.index))[0]  /* Direction -1/0/1 */
#define MESH_VERT_SKIP(v) ((short *)(&(v)->head.index))[1] /* Skip Vert 0/1 */
#define MESH_VERT_DIST(v) ((v)->no[0])                     /* Distance from the plane. */
#define MESH_VERT_SORTVAL(v) ((v)->no[1])                  /* Temp value for sorting. */
#define MESH_VERT_LOOPINDEX(v) /* The verts index within a face (temp var) */ \
  (*((uint *)(&(v)->no[2])))

/* -------------------------------------------------------------------- */
/** Mesh Flag Accessors
 *
 * Hide flag access
 * (for more readable code since same flag is used differently for vert/edge-face).
 **/

/** Enable when vertex is in the center and its faces have been added to the stack. */
LIB_INLINE void vert_is_center_enable(MeshVert *v)
{
  mesh_elem_flag_enable(v, MESH_ELEM_TAG);
}
LIB_INLINE void vert_is_center_disable(MeshVert *v)
{
  mesh_elem_flag_disable(v, MESH_ELEM_TAG);
}
LIB_INLINE bool vert_is_center_test(MeshVert *v)
{
  return (mesh_elem_flag_test(v, MESH_ELEM_TAG) != 0);
}

LIB_INLINE bool vert_pair_adjacent_in_orig_face(MeshVert *v_a, MeshVert *v_b, const uint f_len_orig)
{
  const uint delta = (uint)abs((int)MESH_VERT_LOOPINDEX(v_a) - (int)MESH_VERT_LOOPINDEX(v_b));
  return ELEM(delta, 1, (uint)(f_len_orig - 1));
}

/** Enable when the edge can be cut. */
LIB_INLINE void edge_is_cut_enable(MeshEdge *e)
{
  mesh_elem_flag_enable(e, MESH_ELEM_TAG);
}
LIB_INLINE void edge_is_cut_disable(MeshEdge *e)
{
  mesh_elem_flag_disable(e, MESH_ELEM_TAG);
}
LIB_INLINE bool edge_is_cut_test(MeshEdge *e)
{
  return (mesh_elem_flag_test(e, MESH_ELEM_TAG) != 0);
}

/** Enable when the faces are added to the stack. */
LIB_INLINE void face_in_stack_enable(MeshFace *f)
{
  mesh_elem_flag_disable(f, MESH_ELEM_TAG);
}
LIB_INLINE void face_in_stack_disable(MeshFace *f)
{
  mesh_elem_flag_enable(f, MESH_ELEM_TAG);
}
LIB_INLINE bool face_in_stack_test(MeshFace *f)
{
  return (mesh_elem_flag_test(f, MESH_ELEM_TAG) == 0);
}

/* -------------------------------------------------------------------- */
/** Mesh Face Bisect **/

static int mesh_vert_sortval_cb(const void *v_a_v, const void *v_b_v)
{
  const float val_a = MESH_VERT_SORTVAL(*((MeshVert **)v_a_v));
  const float val_b = MESH_VERT_SORTVAL(*((MeshVert **)v_b_v));

  if (val_a > val_b) {
    return 1;
  }
  if (val_a < val_b) {
    return -1;
  }
  return 0;
}

static void mesh_face_bisect_verts(
    Mesh *mesh, MeshFace *f, const float plane[4], const short opflag_center, const short oflag_new)
{
  /* Unlikely more than 2 verts are needed. */
  const uint f_len_orig = (uint)f->len;
  MeshVert **vert_split_arr = lib_array_alloca(vert_split_arr, f_len_orig);
  STACK_DECLARE(vert_split_arr);
  MeshLoop *l_iter, *l_first;
  bool use_dirs[3] = {false, false, false};
  bool is_inside = false;
  /* True when the face contains one or more edges with both it's vertices on the plane.
   * When set, centered loops are walked over to check if they need to be skipped. */
  bool face_has_center_edge = false;

  STACK_INIT(vert_split_arr, f_len_orig);

  l_first = MESH_FACE_FIRST_LOOP(f);

  /* Add plane-aligned verts to the stack and check we have verts from both sides in this face
   * (that the face doesn't only have boundary verts on the plane for eg). */
  l_iter = l_first;
  do {
    if (vert_is_center_test(l_iter->v)) {
      lib_assert(MESH_VERT_DIR(l_iter->v) == 0);

      /* If both are -1 or 1, or both are zero: don't flip 'inside' var while walking. */
      MESH_VERT_SKIP(l_iter->v) = (((MESH_VERT_DIR(l_iter->prev->v) ^ MESH_VERT_DIR(l_iter->next->v))) ==
                                 0);

      STACK_PUSH(vert_split_arr, l_iter->v);

      if (face_has_center_edge == false) {
        if (vert_is_center_test(l_iter->prev->v)) {
          face_has_center_edge = true;
        }
      }
    }
    use_dirs[MESH_VERT_DIR(l_iter->v) + 1] = true;
  } while ((l_iter = l_iter->next) != l_first);

  if ((STACK_SIZE(vert_split_arr) > 1) && (use_dirs[0] && use_dirs[2])) {
    if (LIKELY(STACK_SIZE(vert_split_arr) == 2)) {
      MeshLoop *l_new;
      MeshLoop *l_a, *l_b;

      l_a = mesh_face_vert_share_loop(f, vert_split_arr[0]);
      l_b = mesh_face_vert_share_loop(f, vert_split_arr[1]);

      /* Common case, just cut the face once. */
      mesh_face_split(mesh, f, l_a, l_b, &l_new, NULL, true);
      if (l_new) {
        if (oflag_center | oflag_new) {
          mesh_op_edge_flag_enable(mesh, l_new->e, opflag_center | opflag_new);
        }
        if (oflag_new) {
          mesh_op_face_flag_enable(mesh, l_new->f, oflag_new);
        }
      }
    }
    else {
      /* Less common case, _complicated_ we need to calculate how to do multiple cuts. */

      uint i = 0;

      /* ---- */
      /* Check contiguous spans of centered vertices (skipping when necessary). */
      if (face_has_center_edge) {

        /* Loop indices need to be set for adjacency checks. */
        l_iter = l_first;
        do {
          MESH_VERT_LOOPINDEX(l_iter->v) = i++;
        } while ((l_iter = l_iter->next) != l_first);

        /* Start out on a non-centered vertex so a span of centered vertices can be looped over
         * without having to scan backwards as well as forwards. */
        MeshLoop *l_first_non_center = l_first;
        while (vert_is_center_test(l_first_non_center->v)) {
          l_first_non_center = l_first_non_center->next;
        }

        l_iter = l_first_non_center;
        do {
          if (MESH_VERT_SKIP(l_iter->v)) {
            continue;
          }
          /* No need to check the previous as the iteration starts on a non-centered vertex. */
          if (!(vert_is_center_test(l_iter->v) && vert_is_center_test(l_iter->next->v))) {
            continue;
          }

          /* Walk over the next loops as long as they are centered. */
          MeshLoop *l_prev = l_iter->prev;
          MeshLoop *l_next = l_iter->next->next;
          /* No need to scan the previous vertices,
           * these will have been dealt with in previous steps. */
          lib_assert(!vert_is_center_test(l_prev->v));
          while (vert_is_center_test(l_next->v)) {
            l_next = l_next->next;
          }

          /* Skip all vertices when the edges connected to the beginning/end
           * of the range are on a different side of the bisecting plane. */
          if (!(MESH_VERT_DIR(l_prev->v) ^ MESH_VERT_DIR(l_next->v))) {
            lib_assert(!vert_is_center_test(l_prev->v));
            l_iter = l_prev->next;
            while (l_iter != l_next) {
              lib_assert(vert_is_center_test(l_iter->v));
              MESH_VERT_SKIP(l_iter->v) = true;
              l_iter = l_iter->next;
            }
          }
          /* Step over the span already handled, even if skip wasn't set. */
          l_iter = l_next->prev;
        } while ((l_iter = l_iter->next) != l_first_non_center);
      }

      float(*face_verts_proj_2d)[2] = lib_array_alloca(face_verts_proj_2d, f_len_orig);
      float axis_mat[3][3];

      MeshFace **face_split_arr = lib_array_alloca(face_split_arr, STACK_SIZE(vert_split_arr));
      STACK_DECLARE(face_split_arr);

      float sort_dir[3];

      /* ---- */
      /* Calculate the direction to sort verts in the face intersecting the plane */

      /* The exact direction isn't important, vertices just need to be sorted across the face.
       * (`sort_dir` could be flipped either way). */
      cross_v3_v3v3(sort_dir, f->no, plane);
      if (UNLIKELY(normalize_v3(sort_dir) == 0.0f)) {
        /* find any 2 verts and get their direction */
        for (i = 0; i < STACK_SIZE(vert_split_arr); i++) {
          if (!equals_v3v3(vert_split_arr[0]->co, vert_split_arr[i]->co)) {
            sub_v3_v3v3(sort_dir, vert_split_arr[0]->co, vert_split_arr[i]->co);
            normalize_v3(sort_dir);
          }
        }
        if (UNLIKELY(i == STACK_SIZE(vert_split_arr))) {
          /* Ok, we can't do anything useful here,
           * face has no area or so, bail out, this is highly unlikely but not impossible. */
          goto finally;
        }
      }

      /* ---- */
      /* Calculate 2d coords to use for intersection checks */

      /* Get the faces 2d coords. */
      lib_assert(mesh_face_is_normal_valid(f));
      axis_dominant_v3_to_m3(axis_mat, f->no);

      l_iter = l_first;
      i = 0;
      do {
        mul_v2_m3v3(face_verts_proj_2d[i], axis_mat, l_iter->v->co);
        i++;
      } while ((l_iter = l_iter->next) != l_first);

      /* ---- */
      /* Sort the verts across the face from one side to another. */
      for (i = 0; i < STACK_SIZE(vert_split_arr); i++) {
        MeshVert *v = vert_split_arr[i];
        MESH_VERT_SORTVAL(v) = dot_v3v3(sort_dir, v->co);
      }

      qsort(
          vert_split_arr, STACK_SIZE(vert_split_arr), sizeof(*vert_split_arr), bm_vert_sortval_cb);

      /* ---- */
      /* Split the face across sorted splits. */

      /* NOTE: we don't know which face gets which splits,
       * so at the moment we have to search all faces for the vert pair,
       * while not all that nice, typically there are < 5 resulting faces,
       * so its not _that_ bad. */

      STACK_INIT(face_split_arr, STACK_SIZE(vert_split_arr));
      STACK_PUSH(face_split_arr, f);

      for (i = 0; i < STACK_SIZE(vert_split_arr) - 1; i++) {
        MeshVert *v_a = vert_split_arr[i];
        MeshVert *v_b = vert_split_arr[i + 1];

        if (face_has_center_edge) {
          if (vert_pair_adjacent_in_orig_face(v_a, v_b, f_len_orig)) {
            continue;
          }
        }

        if (!MESH_VERT_SKIP(v_a)) {
          is_inside = !is_inside;
        }

        if (is_inside) {
          MeshLoop *l_a, *l_b;
          bool found = false;
          uint j;

          for (j = 0; j < STACK_SIZE(face_split_arr); j++) {
            /* It would be nice to avoid loop lookup here,
             * but we need to know which face the verts are in. */
            if ((l_a = mesh_face_vert_share_loop(face_split_arr[j], v_a)) &&
                (l_b = mesh_face_vert_share_loop(face_split_arr[j], v_b))) {
              found = true;
              break;
            }
          }

          /* Ideally won't happen, but it can for self-intersecting faces. */
          // lib_assert(found == true);

          /* In fact this simple test is good enough, test if the loops are adjacent. */
          if (found && !mesh_loop_is_adjacent(l_a, l_b)) {
            MeshLoop *l_new;
            MeshFace *f_tmp;
            f_tmp = mesh_face_split(mesh, face_split_arr[j], l_a, l_b, &l_new, NULL, true);

            if (l_new) {
              if (opflag_center | opflag_new) {
                mesh_op_edge_flag_enable(mesh, l_new->e, opflag_center | opflag_new);
              }
              if (oflag_new) {
                mesh_op_face_flag_enable(mesh, l_new->f, opflag_new);
              }
            }

            if (f_tmp) {
              if (f_tmp != face_split_arr[j]) {
                STACK_PUSH(face_split_arr, f_tmp);
                lib_assert(STACK_SIZE(face_split_arr) <= STACK_SIZE(vert_split_arr));
              }
            }
          }
        }
        else {
          // printf("no intersect\n");
        }
      }
    }
  }

finally:
  (void)vert_split_arr;
}

/* -------------------------------------------------------------------- */
/** Public Mesh Bisect Function **/

void mesh_bisect_plane(Mesh *mesh,
                       const float plane[4],
                       const bool use_snap_center,
                       const bool use_tag,
                       const short oflag_center,
                       const short oflag_new,
                       const float eps)
{
  uint einput_len;
  uint i;
  MeshEdge **edges_arr = mesh_mallocn(sizeof(*edges_arr) * (size_t)bm->totedge, __func__);

  LIB_LINKSTACK_DECLARE(face_stack, MeshFace *);

  MeshVert *v;
  MeshFace *f;

  MeshIter iter;

  if (use_tag) {
    /* Build tagged edge array. */
    MeshEdge *e;
    einput_len = 0;

    /* Flush edge tags to verts. */
    mesh_elem_hflag_disable_all(mesh, MESH_VERT, MESH_ELEM_TAG, false);

    /* Keep face tags as is. */
    MESH_INDEX_ITER (e, &iter, mesh, MESH_EDGES_OF_MESH, i) {
      if (edge_is_cut_test(e)) {
        edges_arr[einput_len++] = e;

        /* Flush edge tags to verts. */
        mesh_elem_flag_enable(e->v1, MESH_ELEM_TAG);
        mesh_elem_flag_enable(e->v2, MESH_ELEM_TAG);
      }
    }

    /* Face tags are set by caller. */
  }
  else {
    MeshEdge *e;
    einput_len = (uint)mesh->totedge;
    MESH_INDEX_ITER (e, &iter, mesh, MESH_EDGES_OF_MESH, i) {
      edge_is_cut_enable(e);
      edges_arr[i] = e;
    }

    MESH_ITER (f, &iter, mesh, MESH_FACES_OF_MESH) {
      face_in_stack_disable(f);
    }
  }

  MESH_ITER (v, &iter, mesh, MESH_VERTS_OF_MESH) {

    if (use_tag && !mesh_elem_flag_test(v, MESH_ELEM_TAG)) {
      vert_is_center_disable(v);

      /* These should never be accessed. */
      MESH_VERT_DIR(v) = 0;
      MESH_VERT_DIST(v) = 0.0f;

      continue;
    }

    vert_is_center_disable(v);
    MESH_VERT_DIR(v) = plane_point_test_v3(plane, v->co, eps, &(BM_VERT_DIST(v)));

    if (MESH_VERT_DIR(v) == 0) {
      if (opflag_center) {
        mesh_op_vert_flag_enable(mesh, v, opflag_center);
      }
      if (use_snap_center) {
        closest_to_plane_v3(v->co, plane, v->co);
      }
    }
  }

  /* Store a stack of faces to be evaluated for splitting. */
  LIB_LINKSTACK_INIT(face_stack);

  for (i = 0; i < einput_len; i++) {
    /* We could check `edge_is_cut_test(e)` but there is no point. */
    MeshEdge *e = edges_arr[i];
    const int side[2] = {MEEH_VERT_DIR(e->v1), MESH_VERT_DIR(e->v2)};
    const float dist[2] = {MESH_VERT_DIST(e->v1), MESH_VERT_DIST(e->v2)};

    if (side[0] && side[1] && (side[0] != side[1])) {
      const float e_fac = dist[0] / (dist[0] - dist[1]);
      MeshVert *v_new;

      if (e->l) {
        MeshLoop *l_iter, *l_first;
        l_iter = l_first = e->l;
        do {
          if (!face_in_stack_test(l_iter->f)) {
            face_in_stack_enable(l_iter->f);
            LIB_LINKSTACK_PUSH(face_stack, l_iter->f);
          }
        } while ((l_iter = l_iter->radial_next) != l_first);
      }

      {
        MESHEdge *e_new;
        v_new = mesh_edge_split(mesh, e, e->v1, &e_new, e_fac);
        if (oflag_new) {
          mesh_op_edge_flag_enable(mesh, e_new, opflag_new);
        }
      }

      vert_is_center_enable(v_new);
      if (opflag_new | opflag_center) {
        mesh_op_vert_flag_enable(mesh, v_new, opflag_new | opflag_center);
      }

      MESH_VERT_DIR(v_new) = 0;
      MESH_VERT_DIST(v_new) = 0.0f;
    }
    else if (side[0] == 0 || side[1] == 0) {
      /* Check if either edge verts are aligned,
       * if so - tag and push all faces that use it into the stack. */
      uint j;
      MESH_ELEM_INDEX_ITER (v, &iter, e, MESH_VERTS_OF_EDGE, j) {
        if (side[j] == 0) {
          if (vert_is_center_test(v) == 0) {
            MeshIter itersub;
            MeshLoop *l_iter;

            vert_is_center_enable(v);

            MESH_ELEM_ITER (l_iter, &itersub, v, MESH_LOOPS_OF_VERT) {
              if (!face_in_stack_test(l_iter->f)) {
                face_in_stack_enable(l_iter->f);
                LIB_LINKSTACK_PUSH(face_stack, l_iter->f);
              }
            }
          }
        }
      }

      /* If both verts are on the center - tag it. */
      if (oflag_center) {
        if (side[0] == 0 && side[1] == 0) {
          mesh_op_edge_flag_enable(mesh, e, opflag_center);
        }
      }
    }
  }

  mem_freen(edges_arr);

  while ((f = LIB_LINKSTACK_POP(face_stack))) {
    mesh_face_bisect_verts(mesh, f, plane, opflag_center, opflag_new);
  }

  /* Caused by access macros: MESH_VERT_DIR, MESH_VERT_SKIP. */
  mesh->elem_index_dirty |= MESH_VERT;

  /* Now we have all faces to split in the stack. */
  LIB_LINKSTACK_FREE(face_stack);
}
