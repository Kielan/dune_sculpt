/**
 * This file contains functions for locally modifying
 * the topology of existing mesh data. (split, join, flip etc).
 */

#include "mem_guardedalloc.h"

#include "lib_array.h"
#include "lib_math.h"

#include "dune_customdata.h"

#include "mesh.h"
#include "intern/mesh_private.h"

bool mesh_vert_dissolve(Mesh *mesh, MeshVert *v)
{
  /* logic for 3 or more is identical */
  const int len = mesh_vert_edge_count_at_most(v, 3);

  if (len == 1) {
    mesh_vert_kill(mesh, v); /* will kill edges too */
    return true;
  }
  if (!mesh_vert_is_manifold(v)) {
    if (!v->e) {
      mesh_vert_kill(mesh, v);
      return true;
    }
    if (!v->e->l) {
      if (len == 2) {
        return (mesh_vert_collapse_edge(mesh, v->e, v, true, true, true) != NULL);
      }
      /* used to kill the vertex here, but it may be connected to faces.
       * so better do nothing */
      return false;
    }
    return false;
  }
  if (len == 2 && mesh_vert_face_count_is_equal(v, 1)) {
    /* boundary vertex on a face */
    return (mesh_vert_collapse_edge(mesh, v->e, v, true, true, true) != NULL);
  }
  return mesh_disk_dissolve(mesh, v);
}

bool mesh_disk_dissolve(Mesh *mesh, MeshVert *v)
{
  MeshEdge *e, *keepedge = NULL, *baseedge = NULL;
  int len = 0;

  if (!mesh_vert_is_manifold(v)) {
    return false;
  }

  if (v->e) {
    /* v->e we keep, what else */
    e = v->e;
    do {
      e = mesh_disk_edge_next(e, v);
      if (!(mesh_edge_share_face_check(e, v->e))) {
        keepedge = e;
        baseedge = v->e;
        break;
      }
      len++;
    } while (e != v->e);
  }

  /* this code for handling 2 and 3-valence verts
   * may be totally bad */
  if (keepedge == NULL && len == 3) {
#if 0
    /* handle specific case for three-valence.  solve it by
     * increasing valence to four.  this may be hackish. */
    MeshLoop *l_a = mesh_face_vert_share_loop(e->l->f, v);
    MeshLoop *l_b = (e->l->v == v) ? e->l->next : e->l;

    if (!mesh_face_split(mesh, e->l->f, l_a, l_b, NULL, NULL, false)) {
      return false;
    }

    if (!mesh_disk_dissolve(mesh, v)) {
      return false;
    }
#else
    if (UNLIKELY(!mesh_faces_join_pair(mesh, e->l, e->l->radial_next, true))) {
      return false;
    }
    if (UNLIKELY(!mesh_vert_collapse_faces(mesh, v->e, v, 1.0, true, false, true, true))) {
      return false;
    }
#endif
    return true;
  }
  if (keepedge == NULL && len == 2) {
    /* collapse the vertex */
    e = mesh_vert_collapse_faces(mesh, v->e, v, 1.0, true, true, true, true);

    if (!e) {
      return false;
    }

    /* handle two-valence */
    if (e->l != e->l->radial_next) {
      if (!mesh_faces_join_pair(mesh, e->l, e->l->radial_next, true)) {
        return false;
      }
    }

    return true;
  }

  if (keepedge) {
    bool done = false;

    while (!done) {
      done = true;
      e = v->e;
      do {
        MeshFace *f = NULL;
        if (mesh_edge_is_manifold(e) && (e != baseedge) && (e != keepedge)) {
          f = mesh_faces_join_pair(mesh, e->l, e->l->radial_next, true);
          /* return if couldn't join faces in manifold
           * conditions */
          /* !disabled for testing why bad things happen */
          if (!f) {
            return false;
          }
        }

        if (f) {
          done = false;
          break;
        }
      } while ((e = mesh_disk_edge_next(e, v)) != v->e);
    }

    /* collapse the vertex */
    /* NOTE: the baseedge can be a boundary of manifold, use this as join_faces arg. */
    e = mesh_vert_collapse_faces(
        mesh, baseedge, v, 1.0, true, !mesh_edge_is_boundary(baseedge), true, true);

    if (!e) {
      return false;
    }

    if (e->l) {
      /* get remaining two faces */
      if (e->l != e->l->radial_next) {
        /* join two remaining faces */
        if (!mesh_faces_join_pair(mesh, e->l, e->l->radial_next, true)) {
          return false;
        }
      }
    }
  }

  return true;
}

MeshFace *mesh_faces_join_pair(Mesh *mesh, MeshLoop *l_a, MeshLoop *l_b, const bool do_del)
{
  lib_assert((l_a != l_b) && (l_a->e == l_b->e));

  if (l_a->v == l_b->v) {
    const int cd_loop_mdisp_offset = CustomData_get_offset(&mesh->ldata, CD_MDISPS);
    mesh_kernel_loop_reverse(mesh, l_b->f, cd_loop_mdisp_offset, true);
  }

  MeshFace *faces[2] = {l_a->f, l_b->f};
  return mesh_faces_join(mesh, faces, 2, do_del);
}

MeshFace *mesh_face_split(Mesh *mesh,
                      MeshFace *f,
                      MeshLoop *l_a,
                      MeshLoop *l_b,
                      MeshLoop **r_l,
                      MeshEdge *example,
                      const bool no_double)
{
  const int cd_loop_mdisp_offset = CustomData_get_offset(&bm->ldata, CD_MDISPS);
  MeshFace *f_new, *f_tmp;

  lib_assert(l_a != l_b);
  lib_assert(f == l_a->f && f == l_b->f);
  lib_assert(!mesh_loop_is_adjacent(l_a, l_b));

  /* could be an assert */
  if (UNLIKELY(mesh_loop_is_adjacent(l_a, l_b)) || UNLIKELY((f != l_a->f || f != l_b->f))) {
    if (r_l) {
      *r_l = NULL;
    }
    return NULL;
  }

  /* do we have a multires layer? */
  if (cd_loop_mdisp_offset != -1) {
    f_tmp = mesh_face_copy(mesh, mesh, f, false, false);
  }

#ifdef USE_MESH_HOLES
  f_new = mesh_kernel_split_face_make_edge(bm, f, l_a, l_b, r_l, NULL, example, no_double);
#else
  f_new = mesh_kernel_split_face_make_edge(bm, f, l_a, l_b, r_l, example, no_double);
#endif

  if (f_new) {
    /* handle multires update */
    if (cd_loop_mdisp_offset != -1) {
      float f_dst_center[3];
      float f_src_center[3];

      mesh_face_calc_center_median(f_tmp, f_src_center);

      mesh_face_calc_center_median(f, f_dst_center);
      mesh_face_interp_multires_ex(mesh, f, f_tmp, f_dst_center, f_src_center, cd_loop_mdisp_offset);

      mesh_face_calc_center_median(f_new, f_dst_center);
      mesh_face_interp_multires_ex(
          mesh, f_new, f_tmp, f_dst_center, f_src_center, cd_loop_mdisp_offset);

#if 0
      /* mesh_face_multires_bounds_smooth doesn't flip displacement correct */
      mesh_face_multires_bounds_smooth(mesh, f);
      mesh_face_multires_bounds_smooth(mesh, f_new);
#endif
    }
  }

  if (cd_loop_mdisp_offset != -1) {
    mesh_face_kill(mesh, f_tmp);
  }

  return f_new;
}

MeshFace *mesh_face_split_n(Mesh *mesh,
                            MeshFace *f,
                            MeshLoop *l_a,
                            MeshLoop *l_b,
                            float cos[][3],
                            int n,
                             MeshLoop **r_l,
                            MeshEdge *example)
{
  MeshFace *f_new, *f_tmp;
  MeshLoop *l_new;
  MeshEdge *e, *e_new;
  MeshVert *v_new;
  // MeshVert *v_a = l_a->v; /* UNUSED */
  MeshVert *v_b = l_b->v;
  int i, j;

  lib_assert(l_a != l_b);
  lib_assert(f == l_a->f && f == l_b->f);
  lib_assert(!((n == 0) && mesh_loop_is_adjacent(l_a, l_b)));

  /* could be an assert */
  if (UNLIKELY((n == 0) && mesh_loop_is_adjacent(l_a, l_b)) || UNLIKELY(l_a->f != l_b->f)) {
    if (r_l) {
      *r_l = NULL;
    }
    return NULL;
  }

  f_tmp = mesh_face_copy(mesh, mesh, f, true, true);

#ifdef USE_MESH_HOLES
  f_new = mesh_kernel_split_face_make_edge(mesh, f, l_a, l_b, &l_new, NULL, example, false);
#else
  f_new = mesh_kernel_split_face_make_edge(mesh, f, l_a, l_b, &l_new, example, false);
#endif
  /* mesh_kernel_split_face_make_edge returns in 'l_new'
   * a Loop for f_new going from 'v_a' to 'v_b'.
   * The radial_next is for 'f' and goes from 'v_b' to 'v_a'. */

  if (f_new) {
    e = l_new->e;
    for (i = 0; i < n; i++) {
      v_new = mesh_kernel_split_edge_make_vert(mesh, v_b, e, &e_new);
      lib_assert(v_new != NULL);
      /* mesh_kernel_split_edge_make_vert returns in 'e_new'
       * the edge going from 'v_new' to 'v_b'. */
      copy_v3_v3(v_new->co, cos[i]);

      /* interpolate the loop data for the loops with (v == v_new), using orig face */
      for (j = 0; j < 2; j++) {
        MeshEdge *e_iter = (j == 0) ? e : e_new;
        MeshLoop *l_iter = e_iter->l;
        do {
          if (l_iter->v == v_new) {
            /* this interpolates both loop and vertex data */
            mesh_loop_interp_from_face(bm, l_iter, f_tmp, true, true);
          }
        } while ((l_iter = l_iter->radial_next) != e_iter->l);
      }
      e = e_new;
    }
  }

  mesh_face_verts_kill(mesh, f_tmp);

  if (r_l) {
    *r_l = l_new;
  }

  return f_new;
}

MeshEdge *mesh_vert_collapse_faces(Mesh *mesh,
                                   MeshEdge *e_kill,
                                   MeshVert *v_kill,
                                   float fac,
                                   const bool do_del,
                                   const bool join_faces,
                                   const bool kill_degenerate_faces,
                                   const bool kill_duplicate_faces)
{
  MeshEdge *e_new = NULL;
  MeshVert *tv = mesh_edge_other_vert(e_kill, v_kill);

  MeshEdge *e2;
  MeshVert *tv2;

  /* Only intended to be called for 2-valence vertices */
  lib_assert(mesh_disk_count(v_kill) <= 2);

  /* first modify the face loop data */

  if (e_kill->l) {
    MeshLoop *l_iter;
    const float w[2] = {1.0f - fac, fac};

    l_iter = e_kill->l;
    do {
      if (l_iter->v == tv && l_iter->next->v == v_kill) {
        const void *src[2];
        MeshLoop *tvloop = l_iter;
        MeshLoop *kvloop = l_iter->next;

        src[0] = kvloop->head.data;
        src[1] = tvloop->head.data;
        CustomData_mesh_interp(&mesh->ldata, src, w, NULL, 2, kvloop->head.data);
      }
    } while ((l_iter = l_iter->radial_next) != e_kill->l);
  }

  /* now interpolate the vertex data */
  mesh_data_interp_from_verts(mesh, v_kill, tv, v_kill, fac);

  e2 = mesh_disk_edge_next(e_kill, v_kill);
  tv2 = mesh_edge_other_vert(e2, v_kill);

  if (join_faces) {
    MeshIter fiter;
    MeshFace **faces = NULL;
    MeshFace *f;
    lib_array_staticdeclare(faces, MESH_DEFAULT_ITER_STACK_SIZE);

    MESH_ITER (f, &fiter, v_kill, MESH_FACES_OF_VERT) {
      lib_array_append(faces, f);
    }

    if (lib_array_len(faces) >= 2) {
      MeshFace *f2 = mesh_faces_join(mesh, faces, lib_array_len(faces), true);
      if (f2) {
        MeshLoop *l_a, *l_b;

        if ((l_a = mesh_face_vert_share_loop(f2, tv)) && (l_b = mesh_face_vert_share_loop(f2, tv2))) {
          MeshLoop *l_new;

          if (mesh_face_split(mesh, f2, l_a, l_b, &l_new, NULL, false)) {
            e_new = l_new->e;
          }
        }
      }
    }

    lib_assert(lib_array_len(faces) < 8);

    lib_array_free(faces);
  }
  else {
    /* single face or no faces */
    /* same as mesh_vert_collapse_edge() however we already
     * have vars to perform this operation so don't call. */
    e_new = mesh_kernel_join_edge_kill_vert(
        mesh, e_kill, v_kill, do_del, true, kill_degenerate_faces, kill_duplicate_faces);

    /* e_new = mesh_edge_exists(tv, tv2); */ /* same as return above */
  }

  return e_new;
}

MeshEdge *mesh_vert_collapse_edge(Mesh *mesh,
                                  MeshEdge *e_kill,
                                  MeshVert *v_kill,
                                  const bool do_del,
                                  const bool kill_degenerate_faces,
                                  const bool kill_duplicate_faces)
{
  /* nice example implementation but we want loops to have their customdata
   * accounted for */
#if 0
  MeshEdge *e_new = NULL;

  /* Collapse between 2 edges */

  /* in this case we want to keep all faces and not join them,
   * rather just get rid of the vertex - see bug T28645. */
  MeshVert *tv = mesh_edge_other_vert(e_kill, v_kill);
  if (tv) {
    MeshEdge *e2 = mesh_disk_edge_next(e_kill, v_kill);
    if (e2) {
      MeshVert *tv2 = mesh_edge_other_vert(e2, v_kill);
      if (tv2) {
        /* only action, other calls here only get the edge to return */
        e_new = mesh_kernel_join_edge_kill_vert(
            mesh, e_kill, v_kill, do_del, true, kill_degenerate_faces);
      }
    }
  }

  return e_new;
#else
  /* with these args faces are never joined, same as above
   * but account for loop customdata */
  return mesh_vert_collapse_faces(
      mesh, e_kill, v_kill, 1.0f, do_del, false, kill_degenerate_faces, kill_duplicate_faces);
#endif
}

#undef DO_V_INTERP

MeshVert *mesh_edge_collapse(
    Mesh *meeh, MeshEdge *e_kill, MeshVert *v_kill, const bool do_del, const bool kill_degenerate_faces)
{
  return mesh_kernel_join_vert_kill_edge(mesh, e_kill, v_kill, do_del, true, kill_degenerate_faces);
}

MeshVert *mesh_edge_split(Mesh *mesh, MeshEdge *e, MeshVert *v, MeshEdge **r_e, float fac)
{
  MeshVert *v_new, *v_other;
  MeshEdge *e_new;
  MeshFace **oldfaces = NULL;
  lib_array_staticdeclare(oldfaces, 32);
  const int cd_loop_mdisp_offset = mesh_edge_is_wire(e) ?
                                       -1 :
                                       CustomData_get_offset(&bm->ldata, CD_MDISPS);

  lib_assert(mesh_vert_in_edge(e, v) == true);

  /* do we have a multi-res layer? */
  if (cd_loop_mdisp_offset != -1) {
    MeshLoop *l;
    int i;

    l = e->l;
    do {
      lib_array_append(oldfaces, l->f);
      l = l->radial_next;
    } while (l != e->l);

    /* flag existing faces so we can differentiate oldfaces from new faces */
    for (i = 0; i < lib_array_len(oldfaces); i++) {
      MESH_ELEM_API_FLAG_ENABLE(oldfaces[i], _FLAG_OVERLAP);
      oldfaces[i] = mesh_face_copy(mesh, mesh, oldfaces[i], true, true);
      MESH_ELEM_API_FLAG_DISABLE(oldfaces[i], _FLAG_OVERLAP);
    }
  }

  v_other = mesh_edge_other_vert(e, v);
  v_new = mesh_kernel_split_edge_make_vert(mesh, v, e, &e_new);
  if (r_e != NULL) {
    *r_e = e_new;
  }

  lib_assert(v_new != NULL);
  lib_assert(mesh_vert_in_edge(e_new, v) && mesh_vert_in_edge(e_new, v_new));
  lib_assert(mesh_vert_in_edge(e, v_new) && mesh_vert_in_edge(e, v_other));

  sub_v3_v3v3(v_new->co, v_other->co, v->co);
  madd_v3_v3v3fl(v_new->co, v->co, v_new->co, fac);

  e_new->head.hflag = e->head.hflag;
  mesh_elem_attrs_copy(mesh, mesh, e, e_new);

  /* v->v_new->v2 */
  mesh_data_interp_face_vert_edge(mesh, v_other, v, v_new, e, fac);
  mesh_data_interp_from_verts(mesh, v, v_other, v_new, fac);

  if (cd_loop_mdisp_offset != -1) {
    int i, j;

    /* interpolate new/changed loop data from copied old faces */
    for (i = 0; i < lib_array_len(oldfaces); i++) {
      float f_center_old[3];

      mesh_face_calc_center_median(oldfaces[i], f_center_old);

      for (j = 0; j < 2; j++) {
        MeshEdge *e1 = j ? e_new : e;
        MeshLoop *l;

        l = e1->l;

        if (UNLIKELY(!l)) {
          MESH_ASSERT(0);
          break;
        }

        do {
          /* check this is an old face */
          if (MESH_ELEM_API_FLAG_TEST(l->f, _FLAG_OVERLAP)) {
            float f_center[3];

            mesh_face_calc_center_median(l->f, f_center);
            mesh_face_interp_multires_ex(
                mesh, l->f, oldfaces[i], f_center, f_center_old, cd_loop_mdisp_offset);
          }
          l = l->radial_next;
        } while (l != e1->l);
      }
    }

    /* destroy the old faces */
    for (i = 0; i < lib_array_len(oldfaces); i++) {
      mesh_face_verts_kill(mesh, oldfaces[i]);
    }

    /* fix boundaries a bit, doesn't work too well quite yet */
#if 0
    for (j = 0; j < 2; j++) {
      MeshEdge *e1 = j ? e_new : e;
      MeshLoop *l, *l2;

      l = e1->l;
      if (UNLIKELY(!l)) {
        MESH_ASSERT(0);
        break;
      }

      do {
        mesh_face_multires_bounds_smooth(bm, l->f);
        l = l->radial_next;
      } while (l != e1->l);
    }
#endif

    lib_array_free(oldfaces);
  }

  return v_new;
}

MeshVert *mesh_edge_split_n(Mesh *mesh, BMEdge *e, int numcuts, BMVert **r_varr)
{
  int i;
  float percent;
  MeshVert *v_new = NULL;

  for (i = 0; i < numcuts; i++) {
    percent = 1.0f / (float)(numcuts + 1 - i);
    v_new = mesh_edge_split(mesh, e, e->v2, NULL, percent);
    if (r_varr) {
      /* fill in reverse order (v1 -> v2) */
      r_varr[numcuts - i - 1] = v_new;
    }
  }
  return v_new;
}

void mesh_edge_verts_swap(MeshEdge *e)
{
  SWAP(MeshVert *, e->v1, e->v2);
  SWAP(MeshDiskLink, e->v1_disk_link, e->v2_disk_link);
}

#if 0
/** Checks if a face is valid in the data structure */
bool mesh_face_validate(MeshFace *face, FILE *err)
{
  MeshIter iter;
  lib_array_declare(verts);
  MeshVert **verts = NULL;
  MeshLoop *l;
  int i, j;
  bool ret = true;

  if (face->len == 2) {
    fprintf(err, "warning: found two-edged face. face ptr: %p\n", face);
    fflush(err);
  }

  lib_array_grow_items(verts, face->len);
  MES_ELEM_INDEX_ITER (l, &iter, face, MESH_LOOPS_OF_FACE, i) {
    verts[i] = l->v;
    if (l->e->v1 == l->e->v2) {
      fprintf(err, "Found mesh edge with identical verts!\n");
      fprintf(err, "  edge ptr: %p, vert: %p\n", l->e, l->e->v1);
      fflush(err);
      ret = false;
    }
  }

  for (i = 0; i < face->len; i++) {
    for (j = 0; j < face->len; j++) {
      if (j == i) {
        continue;
      }

      if (verts[i] == verts[j]) {
        fprintf(err, "Found duplicate verts in bmesh face!\n");
        fprintf(err, "  face ptr: %p, vert: %p\n", face, verts[i]);
        fflush(err);
        ret = false;
      }
    }
  }

  lib_array_free(verts);
  return ret;
}
#endif

void mesh_edge_calc_rotate(MeshEdge *e, const bool ccw, MeshLoop **r_l1, MeshLoop **r_l2)
{
  MeshVert *v1, *v2;
  MeshFace *fa, *fb;

  /* this should have already run */
  lib_assert(mesh_edge_rotate_check(e) == true);

  /* we know this will work */
  mesh_edge_face_pair(e, &fa, &fb);

  /* so we can use `ccw` variable correctly,
   * otherwise we could use the edges verts direct */
  mesh_edge_ordered_verts(e, &v1, &v2);

  /* we could swap the verts _or_ the faces, swapping faces
   * gives more predictable results since that way the next vert
   * just stitches from face fa / fb */
  if (!ccw) {
    SWAP(MeshFace *, fa, fb);
  }

  *r_l1 = mesh_face_other_vert_loop(fb, v2, v1);
  *r_l2 = mesh_face_other_vert_loop(fa, v1, v2);
}

bool mesh_edge_rotate_check(MeshEdge *e)
{
  MeshFace *fa, *fb;
  if (mesh_edge_face_pair(e, &fa, &fb)) {
    MeshLoop *la, *lb;

    la = mesh_face_other_vert_loop(fa, e->v2, e->v1);
    lb = mesh_face_other_vert_loop(fb, e->v2, e->v1);

    /* check that the next vert in both faces isn't the same
     * (ie - the next edge doesn't share the same faces).
     * since we can't rotate usefully in this case. */
    if (la->v == lb->v) {
      return false;
    }

    /* mirror of the check above but in the opposite direction */
    la = mesh_face_other_vert_loop(fa, e->v1, e->v2);
    lb = mesh_face_other_vert_loop(fb, e->v1, e->v2);

    if (la->v == lb->v) {
      return false;
    }

    return true;
  }
  return false;
}

bool mesh_edge_rotate_check_degenerate(MeshEdge *e, MeshLoop *l1, MeshLoop *l2)
{
  /* NOTE: for these vars 'old' just means initial edge state. */

  float ed_dir_old[3];      /* edge vector */
  float ed_dir_new[3];      /* edge vector */
  float ed_dir_new_flip[3]; /* edge vector */

  float ed_dir_v1_old[3];
  float ed_dir_v2_old[3];

  float ed_dir_v1_new[3];
  float ed_dir_v2_new[3];

  float cross_old[3];
  float cross_new[3];

  /* original verts - these will be in the edge 'e' */
  MeshVert *v1_old, *v2_old;

  /* verts from the loops passed */

  MeshVert *v1, *v2;
  /* These are the opposite verts - the verts that _would_ be used if `ccw` was inverted. */
  MeshVert *v1_alt, *v2_alt;

  /* this should have already run */
  lib_assert(mesh_edge_rotate_check(e) == true);

  mesh_edge_ordered_verts(e, &v1_old, &v2_old);

  v1 = l1->v;
  v2 = l2->v;

  /* get the next vert along */
  v1_alt = mesh_face_other_vert_loop(l1->f, v1_old, v1)->v;
  v2_alt = mesh_face_other_vert_loop(l2->f, v2_old, v2)->v;

  /* normalize all so comparisons are scale independent */

  lib_assert(mesh_edge_exists(v1_old, v1));
  lib_assert(mesh_edge_exists(v1, v1_alt));

  lib_assert(mesh_edge_exists(v2_old, v2));
  lib_assert(mesh_edge_exists(v2, v2_alt));

  /* old and new edge vecs */
  sub_v3_v3v3(ed_dir_old, v1_old->co, v2_old->co);
  sub_v3_v3v3(ed_dir_new, v1->co, v2->co);
  normalize_v3(ed_dir_old);
  normalize_v3(ed_dir_new);

  /* old edge corner vecs */
  sub_v3_v3v3(ed_dir_v1_old, v1_old->co, v1->co);
  sub_v3_v3v3(ed_dir_v2_old, v2_old->co, v2->co);
  normalize_v3(ed_dir_v1_old);
  normalize_v3(ed_dir_v2_old);

  /* old edge corner vecs */
  sub_v3_v3v3(ed_dir_v1_new, v1->co, v1_alt->co);
  sub_v3_v3v3(ed_dir_v2_new, v2->co, v2_alt->co);
  normalize_v3(ed_dir_v1_new);
  normalize_v3(ed_dir_v2_new);

  /* compare */
  cross_v3_v3v3(cross_old, ed_dir_old, ed_dir_v1_old);
  cross_v3_v3v3(cross_new, ed_dir_new, ed_dir_v1_new);
  if (dot_v3v3(cross_old, cross_new) < 0.0f) { /* does this flip? */
    return false;
  }
  cross_v3_v3v3(cross_old, ed_dir_old, ed_dir_v2_old);
  cross_v3_v3v3(cross_new, ed_dir_new, ed_dir_v2_new);
  if (dot_v3v3(cross_old, cross_new) < 0.0f) { /* does this flip? */
    return false;
  }

  negate_v3_v3(ed_dir_new_flip, ed_dir_new);

  /* result is zero area corner */
  if ((dot_v3v3(ed_dir_new, ed_dir_v1_new) > 0.999f) ||
      (dot_v3v3(ed_dir_new_flip, ed_dir_v2_new) > 0.999f)) {
    return false;
  }

  return true;
}

bool mesh_edge_rotate_check_beauty(MeshEdge *e, MeshLoop *l1, BMLoop *l2)
{
  /* Stupid check for now:
   * Could compare angles of surrounding edges
   * before & after, but this is OK. */
  return (len_squared_v3v3(e->v1->co, e->v2->co) > len_squared_v3v3(l1->v->co, l2->v->co));
}

MeshEdge *mesh_edge_rotate(Mesh *mesh, MeshEdge *e, const bool ccw, const short check_)
{
  MeshVert *v1, *v2;
  MeshLoop *l1, *l2;
  MeshFace *f;
  MeshEdge *e_new = NULL;
  char f_active_prev = 0;
  char f_hflag_prev_1;
  char f_hflag_prev_2;

  if (!mesh_edge_rotate_check(e)) {
    return NULL;
  }

  mesh_edge_calc_rotate(e, ccw, &l1, &l2);

  /* the loops will be freed so assign verts */
  v1 = l1->v;
  v2 = l2->v;

  /* --------------------------------------- */
  /* Checking Code - make sure we can rotate */

  if (check_flag & MESH_EDGEROT_CHECK_BEAUTY) {
    if (!mesh_edge_rotate_check_beauty(e, l1, l2)) {
      return NULL;
    }
  }

  /* check before applying */
  if (check_flag & MESH_EDGEROT_CHECK_EXISTS) {
    if (mesh_edge_exists(v1, v2)) {
      return NULL;
    }
  }

  /* slowest, check last */
  if (check_flag & MESH_EDGEROT_CHECK_DEGENERATE) {
    if (!mesh_edge_rotate_check_degenerate(e, l1, l2)) {
      return NULL;
    }
  }
  /* Done Checking */
  /* ------------- */

  /* --------------- */
  /* Rotate The Edge */

  /* first create the new edge, this is so we can copy the customdata from the old one
   * if splice if disabled, always add in a new edge even if there's one there. */
  e_new = mesh_edge_create(
      mesh, v1, v2, e, (check_flag & MESH_EDGEROT_CHECK_SPLICE) ? BM_CREATE_NO_DOUBLE : BM_CREATE_NOP);

  f_hflag_prev_1 = l1->f->head.hflag;
  f_hflag_prev_2 = l2->f->head.hflag;

  /* maintain active face */
  if (mesh->act_face == l1->f) {
    f_active_prev = 1;
  }
  else if (mesh->act_face == l2->f) {
    f_active_prev = 2;
  }

  const bool is_flipped = !mesh_edge_is_contiguous(e);

  /* don't delete the edge, manually remove the edge after so we can copy its attributes */
  f = mesh_faces_join_pair(
      mesh, mesh_face_edge_share_loop(l1->f, e), BM_face_edge_share_loop(l2->f, e), true);

  if (f == NULL) {
    return NULL;
  }

  /* NOTE: this assumes joining the faces _didnt_ also remove the verts.
   * the mesh_edge_rotate_check will ensure this, but its possibly corrupt state or future edits
   * break this */
  if ((l1 = mesh_face_vert_share_loop(f, v1)) && (l2 = mesh_face_vert_share_loop(f, v2)) &&
      mesh_face_split(mesh, f, l1, l2, NULL, NULL, true)) {
    /* we should really be able to know the faces some other way,
     * rather than fetching them back from the edge, but this is predictable
     * where using the return values from face split isn't. - campbell */
    MeshFace *fa, *fb;
    if (mesh_edge_face_pair(e_new, &fa, &fb)) {
      fa->head.hflag = f_hflag_prev_1;
      fb->head.hflag = f_hflag_prev_2;

      if (f_active_prev == 1) {
        mesh->act_face = fa;
      }
      else if (f_active_prev == 2) {
        mesh->act_face = fb;
      }

      if (is_flipped) {
        mesh_face_normal_flip(bm, fb);

        if (ccw) {
          /* Needed otherwise `ccw` toggles direction */
          e_new->l = e_new->l->radial_next;
        }
      }
    }
  }
  else {
    return NULL;
  }

  return e_new;
}

MeshVert *mesh_face_loop_separate(Mesh *mesh, MeshLoop *l_sep)
{
  return mesh_kernel_unglue_region_make_vert(mesh, l_sep);
}

MeshVert *mesh_face_loop_separate_multi_isolated(Mesh *mesh, MeshLoop *l_sep)
{
  return mesh_kernel_unglue_region_make_vert_multi_isolated(mesh, l_sep);
}

MeshVert *mesh_face_loop_separate_multi(Mesh *mesh, MeshLoop **larr, int larr_len)
{
  return mesh_kernel_unglue_region_make_vert_multi(mesh, larr, larr_len);
}
