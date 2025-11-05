#include "mem_guardedalloc.h"

#include "lib_bitmap.h"
#include "lib_dunelib.h"
#include "lib_math.h"

#include "imbuf.h"
#include "imbuf_types.h"

#include "types_mesh.h"
#include "types_meshdata.h"
#include "types_object.h"

#include "dune_cx.h"
#include "dune_customdata.h"
#include "dune_global.h"
#include "dune_mesh.h"
#include "dune_object.h"

#include "ed_mesh.h"
#include "ed_screen.h"
#include "ed_select_utils.h"
#include "ed_view3d.h"

#include "wm_api.h"
#include "wm_types.h"

#include "graph.h"
#include "graph_query.h"

/* own include */
void paintface_flush_flags(struct Cx *C, Object *ob, short flag)
{
  Mesh *me = dune_mesh_from_obj(ob);
  MPoly *polys, *mp_orig;
  const int *index_arr = NULL;
  int totpoly;

  lib_assert((flag & ~(SELECT | ME_HIDE)) == 0);

  if (me == NULL) {
    return;
  }

  /* NOTE: call dune_mesh_flush_hidden_from_verts_ex first when changing hidden flags. */

  /* we could call this directly in all areas that change selection,
   * since this could become slow for realtime updates (circle-select for eg) */
  if (flag & SELECT) {
    dune_mesh_flush_select_from_polys(me);
  }

  Graph *graph = cx_data_ensure_eval_graph(C);
  Object *ob_eval = graph_get_eval_object(graph, ob);

  if (ob_eval == NULL) {
    return;
  }

  Mesh *me_orig = (Mesh *)ob_eval->runtime.data_orig;
  Mesh *me_eval = (Mesh *)ob_eval->runtime.data_eval;
  bool updated = false;

  if (me_orig != NULL && me_eval != NULL && me_orig->totpoly == me->totpoly) {
    /* Update the COW copy of the mesh. */
    for (int i = 0; i < me->totpoly; i++) {
      me_orig->mpoly[i].flag = me->mpoly[i].flag;
    }

    /* If the mesh has only deform modifiers, the evaluated mesh shares arrays. */
    if (me_eval->mpoly == me_orig->mpoly) {
      updated = true;
    }
    /* Mesh polys => Final derived polys */
    else if ((idx_arr = CustomData_get_layer(&me_eval->pdata, CD_ORIGIDX))) {
      polys = me_eval->mpoly;
      totpoly = me_eval->totpoly;

      /* loop over final derived polys */
      for (int i = 0; i < totpoly; i++) {
        if (idx_arr[i] != ORIGIDX_NONE) {
          /* Copy flags onto the final derived poly from the original mesh poly */
          mp_orig = me->mpoly + idx_arr[i];
          polys[i].flag = mp_orig->flag;
        }
      }

      updated = true;
    }
  }

  if (updated) {
    if (flag & ME_HIDE) {
      dune_mesh_batch_cache_dirty_tag(me_eval, DUNE_MESH_BATCH_DIRTY_ALL);
    }
    else {
      dune_mesh_batch_cache_dirty_tag(me_eval, DUNE_MESH_BATCH_DIRTY_SELECT_PAINT);
    }

    graph_id_tag_update(ob->data, ID_RECALC_SELECT);
  }
  else {
    graph_id_tag_update(ob->data, ID_RECALC_COPY_ON_WRITE | ID_RECALC_SELECT);
  }

  wm_event_add_notifier(C, NC_GEOM | ND_SELECT, ob->data);
}

void paintface_hide(Cx *C, Object *ob, const bool unselected)
{
  Mesh *me;
  MPoly *mpoly;
  int a;

  me = dune_mesh_from_obj(ob);
  if (me == NULL || me->totpoly == 0) {
    return;
  }

  mpoly = me->mpoly;
  a = me->totpoly;
  while (a--) {
    if ((mpoly->flag & ME_HIDE) == 0) {
      if (((mpoly->flag & ME_FACE_SEL) == 0) == unselected) {
        mpoly->flag |= ME_HIDE;
      }
    }

    if (mpoly->flag & ME_HIDE) {
      mpoly->flag &= ~ME_FACE_SEL;
    }

    mpoly++;
  }

  dune_mesh_flush_hidden_from_polys(me);

  paintface_flush_flags(C, ob, SELECT | ME_HIDE);
}

void paintface_reveal(Cx *C, Object *ob, const bool select)
{
  Mesh *me;
  MPoly *mpoly;
  int a;

  me = dune_mesh_from_object(ob);
  if (me == NULL || me->totpoly == 0) {
    return;
  }

  mpoly = me->mpoly;
  a = me->totpoly;
  while (a--) {
    if (mpoly->flag & ME_HIDE) {
      SET_FLAG_FROM_TEST(mpoly->flag, select, ME_FACE_SEL);
      mpoly->flag &= ~ME_HIDE;
    }
    mpoly++;
  }

  dune_mesh_flush_hidden_from_polys(me);

  paintface_flush_flags(C, ob, SELECT | ME_HIDE);
}

/* Set tface seams based on edge data, uses hash table to find seam edges. */
static void select_linked_tfaces_w_seams(Mesh *me, const uint index, const bool select)
{
  MPoly *mp;
  MLoop *ml;
  int a, b;
  bool do_it = true;
  bool mark = false;

  lib_bitmap *edge_tag = LIB_BITMAP_NEW(me->totedge, __func__);
  lib_bitmap *poly_tag = LIB_BITMAP_NEW(me->totpoly, __func__);

  if (index != (uint)-1) {
    /* only put face under cursor in array */
    mp = &me->mpoly[index];
    dune_mesh_poly_edgebitmap_insert(edge_tag, mp, me->mloop + mp->loopstart);
    LIB_BITMAP_ENABLE(poly_tag, index);
  }
  else {
    /* fill array by selection */
    mp = me->mpoly;
    for (a = 0; a < me->totpoly; a++, mp++) {
      if (mp->flag & ME_HIDE) {
        /* pass */
      }
      else if (mp->flag & ME_FACE_SEL) {
        dune_mesh_poly_edgebitmap_insert(edge_tag, mp, me->mloop + mp->loopstart);
        LIB_BITMAP_ENABLE(poly_tag, a);
      }
    }
  }

  while (do_it) {
    do_it = false;

    /* expand selection */
    mp = me->mpoly;
    for (a = 0; a < me->totpoly; a++, mp++) {
      if (mp->flag & ME_HIDE) {
        continue;
      }

      if (!LIB_BITMAP_TEST(poly_tag, a)) {
        mark = false;

        ml = me->mloop + mp->loopstart;
        for (b = 0; b < mp->totloop; b++, ml++) {
          if ((me->medge[ml->e].flag & ME_SEAM) == 0) {
            if (LIB_BITMAP_TEST(edge_tag, ml->e)) {
              mark = true;
              break;
            }
          }
        }

        if (mark) {
          LIB_BITMAP_ENABLE(poly_tag, a);
          dune_mesh_poly_edgebitmap_insert(edge_tag, mp, me->mloop + mp->loopstart);
          do_it = true;
        }
      }
    }
  }

  mem_freen(edge_tag);

  for (a = 0, mp = me->mpoly; a < me->totpoly; a++, mp++) {
    if (LIB_BITMAP_TEST(poly_tag, a)) {
      SET_FLAG_FROM_TEST(mp->flag, select, ME_FACE_SEL);
    }
  }

  mem_freen(poly_tag);
}

void paintface_select_linked(Cx *C, Object *ob, const int mval[2], const bool select)
{
  Mesh *me;
  uint index = (uint)-1;

  me = dune_mesh_from_object(ob);
  if (me == NULL || me->totpoly == 0) {
    return;
  }

  if (mval) {
    if (!ed_mesh_pick_face(C, ob, mval, ED_MESH_PICK_DEFAULT_FACE_DIST, &index)) {
      return;
    }
  }

  select_linked_tfaces_with_seams(me, index, select);

  paintface_flush_flags(C, ob, SELECT);
}

bool paintface_deselect_all_visible(Cx *C, Object *ob, int action, bool flush_flags)
{
  Mesh *me;
  MPoly *mpoly;
  int a;

  me = dune_mesh_from_object(ob);
  if (me == NULL) {
    return false;
  }

  if (action == SEL_TOGGLE) {
    action = SEL_SELECT;

    mpoly = me->mpoly;
    a = me->totpoly;
    while (a--) {
      if ((mpoly->flag & ME_HIDE) == 0 && mpoly->flag & ME_FACE_SEL) {
        action = SEL_DESELECT;
        break;
      }
      mpoly++;
    }
  }

  bool changed = false;

  mpoly = me->mpoly;
  a = me->totpoly;
  while (a--) {
    if ((mpoly->flag & ME_HIDE) == 0) {
      switch (action) {
        case SEL_SELECT:
          if ((mpoly->flag & ME_FACE_SEL) == 0) {
            mpoly->flag |= ME_FACE_SEL;
            changed = true;
          }
          break;
        case SEL_DESELECT:
          if ((mpoly->flag & ME_FACE_SEL) != 0) {
            mpoly->flag &= ~ME_FACE_SEL;
            changed = true;
          }
          break;
        case SEL_INVERT:
          mpoly->flag ^= ME_FACE_SEL;
          changed = true;
          break;
      }
    }
    mpoly++;
  }

  if (changed) {
    if (flush_flags) {
      paintface_flush_flags(C, ob, SELECT);
    }
  }
  return changed;
}

bool paintface_minmax(Object *ob, float r_min[3], float r_max[3])
{
  const Mesh *me;
  const MPoly *mp;
  const MLoop *ml;
  const MVert *mvert;
  int a, b;
  bool ok = false;
  float vec[3], bmat[3][3];

  me = dune_mesh_from_object(ob);
  if (!me || !me->mloopuv) {
    return ok;
  }

  copy_m3_m4(bmat, ob->obmat);

  mvert = me->mvert;
  mp = me->mpoly;
  for (a = me->totpoly; a > 0; a--, mp++) {
    if (mp->flag & ME_HIDE || !(mp->flag & ME_FACE_SEL)) {
      continue;
    }

    ml = me->mloop + mp->loopstart;
    for (b = 0; b < mp->totloop; b++, ml++) {
      mul_v3_m3v3(vec, bmat, mvert[ml->v].co);
      add_v3_v3v3(vec, vec, ob->obmat[3]);
      minmax_v3v3_v3(r_min, r_max, vec);
    }

    ok = true;
  }

  return ok;
}

bool paintface_mouse_select(struct Cx *C,
                            const int mval[2],
                            const struct SelectPick_Params *params,
                            Object *ob)
{
  Mesh *me;
  MPoly *mpoly_sel = NULL;
  uint idx;
  bool changed = false;
  bool found = false;

  /* Get the face under the cursor */
  me = dune_mesh_from_object(ob);

  if (ed_mesh_pick_face(C, ob, mval, ED_MESH_PICK_DEFAULT_FACE_DIST, &idx)) {
    if (idx < me->totpoly) {
      mpoly_sel = me->mpoly + idx;
      if ((mpoly_sel->flag & ME_HIDE) == 0) {
        found = true;
      }
    }
  }

  if (params->sel_op == SEL_OP_SET) {
    if ((found && params->select_passthrough) && (mpoly_sel->flag & ME_FACE_SEL)) {
      found = false;
    }
    else if (found || params->deselect_all) {
      /* Deselect everything. */
      changed |= paintface_deselect_all_visible(C, ob, SEL_DESELECT, false);
    }
  }

  if (found) {
    me->act_face = (int)index;

    switch (params->sel_op) {
      case SEL_OP_ADD: {
        mpoly_sel->flag |= ME_FACE_SEL;
        break;
      }
      case SEL_OP_SUB: {
        mpoly_sel->flag &= ~ME_FACE_SEL;
        break;
      }
      case SEL_OP_XOR: {
        if (mpoly_sel->flag & ME_FACE_SEL) {
          mpoly_sel->flag &= ~ME_FACE_SEL;
        }
        else {
          mpoly_sel->flag |= ME_FACE_SEL;
        }
        break;
      }
      case SEL_OP_SET: {
        mpoly_sel->flag |= ME_FACE_SEL;
        break;
      }
      case SEL_OP_AND: {
        lib_assert_unreachable(); /* Doesn't make sense for picking. */
        break;
      }
    }

    /* image window redraw */
    paintface_flush_flags(C, ob, SELECT);
    ed_rgn_tag_redraw(cx_wm_rgn(C)); /* XXX: should redraw all 3D views. */
    changed = true;
  }
  return changed || found;
}

void paintvert_flush_flags(Object *ob)
{
  Mesh *me = dune_mesh_from_obj(ob);
  Mesh *me_eval = dune_obj_get_evaluated_mesh(ob);
  MVert *mvert_eval, *mv;
  const int *idx_arr = NULL;
  int totvert;
  int i;

  if (me == NULL) {
    return;
  }

  /* we could call this directly in all areas that change selection,
   * since this could become slow for realtime updates (circle-select for eg) */
  dune_mesh_flush_select_from_verts(me);

  if (me_eval == NULL) {
    return;
  }

  idx_arr = CustomData_get_layer(&me_eval->vdata, CD_ORIGIDX);

  mvert_eval = me_eval->mvert;
  totvert = me_eval->totvert;

  mv = mvert_eval;

  if (idx_arr) {
    int orig_idx;
    for (i = 0; i < totvert; i++, mv++) {
      orig_idx = idx_arr[i];
      if (orig_idx != ORIGIDX_NONE) {
        mv->flag = me->mvert[idx_arr[i]].flag;
      }
    }
  }
  else {
    for (i = 0; i < totvert; i++, mv++) {
      mv->flag = me->mvert[i].flag;
    }
  }

  dune_mesh_batch_cache_dirty_tag(me, DUNE_MESH_BATCH_DIRTY_ALL);
}

void paintvert_tag_select_update(struct Cx *C, struct Object *ob)
{
  graph_id_tag_update(ob->data, ID_RECALC_COPY_ON_WRITE | ID_RECALC_SELECT);
  wm_event_add_notifier(C, NC_GEOM | ND_SELECT, ob->data);
}

bool paintvert_deselect_all_visible(Object *ob, int action, bool flush_flags)
{
  Mesh *me;
  MVert *mvert;
  int a;

  me = dune_mesh_from_obj(ob);
  if (me == NULL) {
    return false;
  }

  if (action == SEL_TOGGLE) {
    action = SEL_SELECT;

    mvert = me->mvert;
    a = me->totvert;
    while (a--) {
      if ((mvert->flag & ME_HIDE) == 0 && mvert->flag & SELECT) {
        action = SEL_DESELECT;
        break;
      }
      mvert++;
    }
  }

  bool changed = false;
  mvert = me->mvert;
  a = me->totvert;
  while (a--) {
    if ((mvert->flag & ME_HIDE) == 0) {
      switch (action) {
        case SEL_SELECT:
          if ((mvert->flag & SELECT) == 0) {
            mvert->flag |= SELECT;
            changed = true;
          }
          break;
        case SEL_DESELECT:
          if ((mvert->flag & SELECT) != 0) {
            mvert->flag &= ~SELECT;
            changed = true;
          }
          break;
        case SEL_INVERT:
          mvert->flag ^= SELECT;
          changed = true;
          break;
      }
    }
    mvert++;
  }

  if (changed) {
    /* handle mselect */
    if (action == SEL_SELECT) {
      /* pass */
    }
    else if (ELEM(action, SEL_DESELECT, SEL_INVERT)) {
      dune_mesh_mselect_clear(me);
    }
    else {
      dune_mesh_mselect_validate(me);
    }

    if (flush_flags) {
      paintvert_flush_flags(ob);
    }
  }
  return changed;
}

void paintvert_sel_ungrouped(Object *ob, bool extend, bool flush_flags)
{
  Mesh *me = dune_mesh_from_object(ob);
  MVert *mv;
  MDeformVert *dv;
  int a, tot;

  if (me == NULL || me->dvert == NULL) {
    return;
  }

  if (!extend) {
    paintvert_deselect_all_visible(ob, SEL_DESELECT, false);
  }

  dv = me->dvert;
  tot = me->totvert;

  for (a = 0, mv = me->mvert; a < tot; a++, mv++, dv++) {
    if ((mv->flag & ME_HIDE) == 0) {
      if (dv->dw == NULL) {
        /* if null weight then not grouped */
        mv->flag |= SELECT;
      }
    }
  }

  if (flush_flags) {
    paintvert_flush_flags(ob);
  }
}
