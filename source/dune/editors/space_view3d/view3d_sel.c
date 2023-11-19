#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "types_action.h"
#include "types_armature.h"
#include "types_curve.h"
#include "types_pen.h"
#include "types_mesh.h"
#include "types_meshdata.h"
#include "types_meta.h"
#include "types_ob.h"
#include "types_scene.h"
#include "types_tracking.h"

#include "mem_guardedalloc.h"

#include "lib_array.h"
#include "lib_bitmap.h"
#include "lib_lasso_2d.h"
#include "lib_linklist.h"
#include "lib_list.h"
#include "lib_math.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#ifdef __BIG_ENDIAN__
#  include "lib_endian_switch.h"
#endif

/* vertex box sel */
#include "dune_global.h"
#include "dune_main.h"
#include "imbuf.h"
#include "imbuf_types.h"

#include "dune_action.h"
#include "dune_armature.h"
#include "dune_cxt.h"
#include "dune_curve.h"
#include "dune_meshedit.h"
#include "dune_layer.h"
#include "dune_mball.h"
#include "dune_mesh.h"
#include "dune_ob.h"
#include "dune_paint.h"
#include "dune_scene.h"
#include "dune_tracking.h"
#include "dune_workspace.h"

#include "graph.h"

#include "win_api.h"
#include "win_toolsystem.h"
#include "win_types.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "ed_armature.h"
#include "ed_curve.h"
#include "ed_pen.h"
#include "ed_lattice.h"
#include "ed_mball.h"
#include "ed_mesh.h"
#include "ed_ob.h"
#include "ed_outliner.h"
#include "ed_particle.h"
#include "ed_screen.h"
#include "ed_sculpt.h"
#include "ed_sel_utils.h"

#include "ui.h"
#include "ui_resources.h"

#include "gpu_matrix.h"
#include "gpu_sel.h"

#include "graph.h"
#include "graph_query.h"

#include "drw_engine.h"
#include "drw_sel_buffer.h"

#include "view3d_intern.h" /* own include */

// #include "PIL_time_utildefines.h"

/* Pub Utils */
float ed_view3d_sel_dist_px(void)
{
  return 75.0f * U.pixelsize;
}

void ed_view3d_viewcxt_init(bCxt *C, ViewCxt *vc, Graph *graph)
{
  /* TODO: should return whether there is valid cxt to continue. */

  memset(vc, 0, sizeof(ViewCxt));
  vc->C = C;
  vc->rgn = cxt_win_rgn(C);
  vc->main = cxt_data_main(C);
  vc->graph = graph;
  vc->scene = cxt_data_scene(C);
  vc->view_layer = cxt_data_view_layer(C);
  vc->v3d = cxt_win_view3d(C);
  vc->win = cxt_win(C);
  vc->rv3d = cxt_win_rgn_view3d(C);
  vc->obact = cxt_data_active_ob(C);
  vc->obedit = cxt_data_edit_ob(C);
}

void ed_view3d_viewcxt_init_ob(ViewCxt *vc, Ob *obact)
{
  vc->obact = obact;
  if (vc->obedit) {
    lib_assert(dune_ob_is_in_editmode(obact));
    vc->obedit = obact;
    if (vc->em) {
      vc->em = dune_meshedit_from_ob(vc->obedit);
    }
  }
}

/* Internal Ob Utils */
static bool ob_desel_all_visible(ViewLayer *view_layer, View3D *v3d)
{
  bool changed = false;
  LIST_FOREACH (Base *, base, &view_layer->ob_bases) {
    if (base->flag & BASE_SELECTED) {
      if (BASE_SELECTABLE(v3d, base)) {
        ed_ob_base_sel(base, BA_DESEL);
        changed = true;
      }
    }
  }
  return changed;
}

/* desel all except b */
static bool ob_desel_all_except(ViewLayer *view_layer, Base *b)
{
  bool changed = false;
  LIST_FOREACH (Base *, base, &view_layer->ob_bases) {
    if (base->flag & BASE_SELECTED) {
      if (b != base) {
        ed_ob_base_sel(base, BA_DESEL);
        changed = true;
      }
    }
  }
  return changed;
}

/* Internal Edit-Mesh Sel Buffer Wrapper
 * Avoid duplicate code when using edit-mode sel,
 * actual logic is handled outside of this fn.
 *
 * Currently this EDBMSelIdCxt which is mesh specific
 * however the logic could also be used for non-meshes too */

struct EditSelBufCache {
  lib_bitmap *sel_bitmap;
};

static void editsel_buf_cache_init(ViewCzt *vc, short sel_mode)
{
  if (vc->obedit) {
    uint bases_len = 0;
    Base **bases = dune_view_layer_array_from_bases_in_edit_mode(
        vc->view_layer, vc->v3d, &bases_len);

    drw_sel_buffer_cxt_create(bases, bases_len, sel_mode);
    mem_free(bases);
  }
  else {
    /* Use for paint modes, currently only a single ob at a time. */
    if (vc->obact) {
      Base *base = dune_view_layer_base_find(vc->view_layer, vc->obact);
      drw_sel_buffer_cxt_create(&base, 1, sel_mode);
    }
  }
}

static void editsel_buf_cache_free(struct EditSelBufCache *esel)
{
  MEM_SAFE_FREE(esel->sel_bitmap);
}

static void editsel_buf_cache_free_voidp(void *esel_voidp)
{
  editsel_buf_cache_free(esel_voidp);
  mem_free(esel_voidp);
}

static void editsel_buf_cache_init_with_generic_userdata(WinGenericUserData *win_userdata,
                                                         ViewCxt *vc,
                                                         short sel_mode)
{
  struct EditSelBuf_Cache *esel = mem_calloc(sizeof(*esel), __func__);
  win_userdata->data = esel;
  win_userdata->free_fn = editsel_buf_cache_free_voidp;
  win_userdata->use_free = true;
  editsel_buf_cache_init(vc, sel_mode);
}

/* Internal Edit-Mesh Utils */
static bool edbm_backbuf_check_and_sel_verts(struct EditSelBufCache *esel,
                                                Graph *graph,
                                                Ob *ob,
                                                MeshEdit *em,
                                                const eSelOp sel_op)
{
  MVert *eve;
  MIter iter;
  bool changed = false;

  const lib_bitmap *sel_bitmap = esel->sel_bitmap;
  uint index = drw_sel_buffer_cxt_offset_for_ob_elem(graph, ob, SCE_SEL_VERTEX);
  if (index == 0) {
    return false;
  }

  index -= 1;
  MESH_ITER_MESH (eve, &iter, em->mesh, MESH_VERTS_OF_MESH) {
    if (!mesh_elem_flag_test(eve, MESH_ELEM_HIDDEN)) {
      const bool is_sel = mesh_elem_flag_test(eve, MESH_ELEM_SEL);
      const bool is_inside = LIB_BITMAP_TEST_BOOL(sel_bitmap, index);
      const int sel_op_result = ed_sel_op_action_deselected(sel_op, is_sel, is_inside);
      if (sel_op_result != -1) {
        mesh_vert_sel_set(em->bm, eve, sel_op_result);
        changed = true;
      }
    }
    index++;
  }
  return changed;
}

static bool edbm_backbuf_check_and_sel_edges(struct EditSelBufCache *esel,
                                             Graph *graph,
                                             Ob *ob,
                                             MeshEdit *em,
                                             const eSelOp sel_op)
{
  MEdge *eed;
  MIter iter;
  bool changed = false;

  const lib_bitmap *sel_bitmap = esel->sel_bitmap;
  uint index = draw_sel_buffer_cxt_offset_for_ob_elem(graph, ob, SCE_SEL_EDGE);
  if (index == 0) {
    return false;
  }

  index -= 1;
  MESH_ITER_MESH (eed, &iter, em->mesh, MESH_EDGES_OF_MESH) {
    if (!mesh_elem_flag_test(eed, MESH_ELEM_HIDDEN)) {
      const bool is_sel = mesh_elem_flag_test(eed, MESH_ELEM_SEL);
      const bool is_inside = lib_BITMAP_TEST_BOOL(sel_bitmap, index);
      const int sel_op_result = ed_sel_op_action_deselected(sel_op, is_sel, is_inside);
      if (sel_op_result != -1) {
        mesh_edge_sel_set(em->mesh, eed, sel_op_result);
        changed = true;
      }
    }
    index++;
  }
  return changed;
}

static bool edbm_backbuf_check_and_sel_faces(struct EditSelBufCache *esel,
                                             Graph *graph,
                                             Ob *ob,
                                             MeshEdit *em,
                                             const eSelOp sel_op)
{
  MFace *efa;
  MIter iter;
  bool changed = false;

  const lib_bitmap *sel_bitmap = esel->sel_bitmap;
  uint index = drw_sel_buffer_cxt_offset_for_ob_elem(graph, ob, SCE_SELECT_FACE);
  if (index == 0) {
    return false;
  }

  index -= 1;
  MESH_ITER (efa, &iter, em->mesh, MESH_FACES_OF_MESH) {
    if (!mesh_elem_flag_test(efa, MESH_ELEM_HIDDEN)) {
      const bool is_sel = mesh_elem_flag_test(efa, MESH_ELEM_SEL);
      const bool is_inside = lib_BITMAP_TEST_BOOL(sel_bitmap, index);
      const int sel_op_result = ed_sel_op_action_deselected(sel_op, is_sel, is_inside);
      if (sel_op_result != -1) {
        mesh_face_sel_set(em->mesh, efa, sel_op_result);
        changed = true;
      }
    }
    index++;
  }
  return changed;
}

/* object mode, edbm_ prefix is confusing here, rename? */
static bool edbm_backbuf_check_and_sel_verts_obmode(Mesh *me,
                                                    struct EditSelBufCache *esel,
                                                    const eSelOp sel_op)
{
  MVert *mv = me->mvert;
  uint index;
  bool changed = false;

  const lib_bitmap *sel_bitmap = esel->sel_bitmap;

  if (mv) {
    for (index = 0; index < me->totvert; index++, mv++) {
      if (!(mv->flag & ME_HIDE)) {
        const bool is_sel = mv->flag & SEL;
        const bool is_inside = LIB_BITMAP_TEST_BOOL(sel_bitmap, index);
        const int sel_op_result = ed_sel_op_action_deselected(sel_op, is_sel, is_inside);
        if (sel_op_result != -1) {
          SET_FLAG_FROM_TEST(mv->flag, sel_op_result, SELECT);
          changed = true;
        }
      }
    }
  }
  return changed;
}

/* ob mode, edbm_ prefix is confusing here, rename? */
static bool edbm_backbuf_check_and_sel_faces_obmode(Mesh *me,
                                                    struct EditSelBufCache *esel,
                                                    const eSelOp sel_op)
{
  MPoly *mpoly = me->mpoly;
  uint index;
  bool changed = false;

  const lib_bitmap *sel_bitmap = esel->select_bitmap;

  if (mpoly) {
    for (index = 0; index < me->totpoly; index++, mpoly++) {
      if (!(mpoly->flag & ME_HIDE)) {
        const bool is_sel = mpoly->flag & ME_FACE_SEL;
        const bool is_inside = LIB_BITMAP_TEST_BOOL(select_bitmap, index);
        const int sel_op_result = ed_sel_op_action_deselected(sel_op, is_sel, is_inside);
        if (sel_op_result != -1) {
          SET_FLAG_FROM_TEST(mpoly->flag, sel_op_result, ME_FACE_SEL);
          changed = true;
        }
      }
    }
  }
  return changed;
}

/* Lasso Sel */
typedef struct LassoSelUserData {
  ViewCxt *vc;
  const rcti *rect;
  const rctf *rect_fl;
  rctf _rect_fl;
  const int (*mcoords)[2];
  int mcoords_len;
  eSelOp sel_op;
  eBezTripleFlag sel_flag;

  /* runtime */
  int pass;
  bool is_done;
  bool is_changed;
} LassoSelUserData;

static void view3d_userdata_lassosel_init(LassoSelUserData *r_data,
                                          ViewCxt *vc,
                                          const rcti *rect,
                                          const int (*mcoords)[2],
                                          const int mcoords_len,
                                          const eSelOp sel_op)
{
  r_data->vc = vc;

  r_data->rect = rect;
  r_data->rect_fl = &r_data->_rect_fl;
  lib_rctf_rcti_copy(&r_data->_rect_fl, rect);

  r_data->mcoords = mcoords;
  r_data->mcoords_len = mcoords_len;
  r_data->sel_op = sel_op;
  /* SEL by default, but can be changed if needed (only few cases use and respect this). */
  r_data->sel_flag = SEL;

  /* runtime */
  r_data->pass = 0;
  r_data->is_done = false;
  r_data->is_changed = false;
}

static bool view3d_sel_data(Cxt *C)
{
  Ob *ob = cxt_data_active_ob(C);

  if (!ed_op_rgn_view3d_active(C)) {
    return 0;
  }

  if (ob) {
    if (ob->mode & OB_MODE_EDIT) {
      if (ob->type == OB_FONT) {
        return 0;
      }
    }
    else {
      if ((ob->mode & (OB_MODE_VERTEX_PAINT | OB_MODE_WEIGHT_PAINT | OB_MODE_TEXTURE_PAINT)) &&
          !dune_paint_sel_elem_test(ob)) {
        return 0;
      }
    }
  }

  return 1;
}

/* helper also for box_sel */
static bool edge_fully_inside_rect(const rctf *rect, const float v1[2], const float v2[2])
{
  return lib_rctf_isect_pt_v(rect, v1) && lib_rctf_isect_pt_v(rect, v2);
}

static bool edge_inside_rect(const rctf *rect, const float v1[2], const float v2[2])
{
  int d1, d2, d3, d4;

  /* check points in rect */
  if (edge_fully_inside_rect(rect, v1, v2)) {
    return 1;
  }

  /* check points completely out rect */
  if (v1[0] < rect->xmin && v2[0] < rect->xmin) {
    return 0;
  }
  if (v1[0] > rect->xmax && v2[0] > rect->xmax) {
    return 0;
  }
  if (v1[1] < rect->ymin && v2[1] < rect->ymin) {
    return 0;
  }
  if (v1[1] > rect->ymax && v2[1] > rect->ymax) {
    return 0;
  }

  /* simple check lines intersecting. */
  d1 = (v1[1] - v2[1]) * (v1[0] - rect->xmin) + (v2[0] - v1[0]) * (v1[1] - rect->ymin);
  d2 = (v1[1] - v2[1]) * (v1[0] - rect->xmin) + (v2[0] - v1[0]) * (v1[1] - rect->ymax);
  d3 = (v1[1] - v2[1]) * (v1[0] - rect->xmax) + (v2[0] - v1[0]) * (v1[1] - rect->ymax);
  d4 = (v1[1] - v2[1]) * (v1[0] - rect->xmax) + (v2[0] - v1[0]) * (v1[1] - rect->ymin);

  if (d1 < 0 && d2 < 0 && d3 < 0 && d4 < 0) {
    return 0;
  }
  if (d1 > 0 && d2 > 0 && d3 > 0 && d4 > 0) {
    return 0;
  }

  return 1;
}

static void do_lasso_sel_pose__do_tag(void *userData,
                                      struct PoseChannel *pchan,
                                      const float screen_co_a[2],
                                      const float screen_co_b[2])
{
  LassoSelUserData *data = userData;
  const Armature *arm = data->vc->obact->data;
  if (!PBONE_SELECTABLE(arm, pchan->bone)) {
    return;
  }

  if (lib_rctf_isect_segment(data->rect_fl, screen_co_a, screen_co_b) &&
      lib_lasso_is_edge_inside(
          data->mcoords, data->mcoords_len, UNPACK2(screen_co_a), UNPACK2(screen_co_b), INT_MAX)) {
    pchan->bone->flag |= BONE_DONE;
    data->is_changed = true;
  }
}
static void do_lasso_tag_pose(ViewCxt *vc,
                              Ob *ob,
                              const int mcoords[][2],
                              const int mcoords_len)
{
  ViewCxt vc_tmp;
  LassoSelUserData data;
  rcti rect;

  if ((ob->type != OB_ARMATURE) || (ob->pose == NULL)) {
    return;
  }

  vc_tmp = *vc;
  vc_tmp.obact = ob;

  lib_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassosel_init(&data, vc, &rect, mcoords, mcoords_len, 0);

  ed_view3d_init_mats_rv3d(vc_tmp.obact, vc->rv3d);

  /* Treat bones as clipped segments (no joints). */
  pose_foreachScreenBone(&vc_tmp,
                         do_lasso_sel_pose__do_tag,
                         &data,
                         V3D_PROJ_TEST_CLIP_DEFAULT | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);
}

static bool do_lasso_sel_objs(ViewCxt *vc,
                              const int mcoords[][2],
                              const int mcoords_len,
                             const eSelectOp sel_op)
{
  View3D *v3d = vc->v3d;
  Base *base;

  bool changed = false;
  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    changed |= ob_desel_all_visible(vc->view_layer, vc->v3d);
  }

  for (base = vc->view_layer->ob_bases.first; base; base = base->next) {
    if (BASE_SELECTABLE(v3d, base)) { /* Use this to avoid unnecessary lasso look-ups. */
      const bool is_select = base->flag & BASE_SELECTED;
      const bool is_inside = ((ed_view3d_project_base(vc->rgn, base) == V3D_PROJ_RET_OK) &&
                              lib_lasso_is_point_inside(
                                  mcoords, mcoords_len, base->sx, base->sy, IS_CLIPPED));
      const int sel_op_result = ed_sel_op_action_deselected(sel_op, is_sel, is_inside);
      if (sel_op_result != -1) {
        ed_ob_base_sel(base, sel_op_result ? BA_SEL : BA_DESEL);
        changed = true;
      }
    }
  }

  if (changed) {
    graph_id_tag_update(&vc->scene->id, ID_RECALC_SEL);
    win_main_add_notifier(NC_SCENE | ND_OB_SEL, vc->scene);
  }
  return changed;
}

/* Use for lasso & box sel */
static Base **do_pose_tag_sel_op_prepare(ViewCxt *vc, uint *r_bases_len)
{
  Base **bases = NULL;
  lib_array_declare(bases);
  FOREACH_BASE_IN_MODE_BEGIN (vc->view_layer, vc->v3d, OB_ARMATURE, OB_MODE_POSE, base_iter) {
    Ob *ob_iter = base_iter->ob;
    Armature *arm = ob_iter->data;
    LIST_FOREACH (PoseChannel *, pchan, &ob_iter->pose->chanbase) {
      Bone *bone = pchan->bone;
      bone->flag &= ~BONE_DONE;
    }
    arm->id.tag |= LIB_TAG_DOIT;
    ob_iter->id.tag &= ~LIB_TAG_DOIT;
    lib_array_append(bases, base_iter);
  }
  FOREACH_BASE_IN_MODE_END;
  *r_bases_len = lib_array_len(bases);
  return bases;
}

static bool do_pose_tag_sel_op_ex(Base **bases, const uint bases_len, const eSelOp sel_op)
{
  bool changed_multi = false;

  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    for (int i = 0; i < bases_len; i++) {
      Base *base_iter = bases[i];
      Ob *ob_iter = base_iter->object;
      if (ed_pose_desel_all(ob_iter, SEL_DESEL, false)) {
        ed_pose_bone_sel_tag_update(ob_iter);
        changed_multi = true;
      }
    }
  }

  for (int i = 0; i < bases_len; i++) {
    Base *base_iter = bases[i];
    Ob *ob_iter = base_iter->ob
    Armature *arm = ob_iter->data;

    /* Don't handle twice. */
    if (arm->id.tag & LIB_TAG_DOIT) {
      arm->id.tag &= ~LIB_TAG_DOIT;
    }
    else {
      continue;
    }

    bool changed = true;
    LIST_FOREACH (PoseChannel *, pchan, &ob_iter->pose->chanbase) {
      Bone *bone = pchan->bone;
      if ((bone->flag & BONE_UNSELECTABLE) == 0) {
        const bool is_sel = bone->flag & BONE_SEl;
        const bool is_inside = bone->flag & BONE_DONE;
        const int sel_op_result = ed_sel_op_action_desel(sel_op, is_sel, is_inside);
        if (sel_op_result != -1) {
          SET_FLAG_FROM_TEST(bone->flag, sel_op_result, BONE_SELECTED);
          if (sel_op_result == 0) {
            if (arm->act_bone == bone) {
              arm->act_bone = NULL;
            }
          }
          changed = true;
        }
      }
    }
    if (changed) {
      ed_pose_bone_sel_tag_update(ob_iter);
      changed_multi = true;
    }
  }
  return changed_multi;
}

static bool do_lasso_sel_pose(ViewCxt *vc,
                              const int mcoords[][2],
                              const int mcoords_len,
                              const eSelOp sel_op)
{
  uint bases_len;
  Base **bases = do_pose_tag_sel_op_prepare(vc, &bases_len);

  for (int i = 0; i < bases_len; i++) {
    Base *base_iter = bases[i];
    Ob *ob_iter = base_iter->ob;
    do_lasso_tag_pose(vc, ob_iter, mcoords, mcoords_len);
  }

  const bool changed_multi = do_pose_tag_sel_op_exe(bases, bases_len, sel_op);
  if (changed_multi) {
    graph_id_tag_update(&vc->scene->id, ID_RECALC_SEL);
    win_main_add_notifier(NC_SCENE | ND_OB_SEL, vc->scene);
  }

  mem_free(bases);
  return changed_multi;
}

static void do_lasso_sel_mesh__doSelVert(void *userData,
                                         MVert *eve,
                                         const float screen_co[2],
                                         int UNUSED(index))
{
  LassoSelUserData *data = userData;
  const bool is_sel = mesh_elem_flag_test(eve, MESH_ELEM_SEL);
  const bool is_inside =
      (lib_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       lib_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED));
  const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    mesh_vert_sel_set(data->vc->em->mesh, eve, sel_op_result);
    data->is_changed = true;
  }
}
struct LassoSelUserDataForMeshEdge {
  LassoSelUserData *data;
  struct EditSelBuf_Cache *esel;
  uint backbuf_offset;
};
static void do_lasso_sel_mesh_doSelEdge_pass0(void *user_data,
                                              MEdge *eed,
                                              const float screen_co_a[2],
                                              const float screen_co_b[2],
                                              int index)
{
  struct LassoStUserDataForMeshEdge *data_for_edge = user_data;
  LassoSelUserData *data = data_for_edge->data;
  bool is_visible = true;
  if (data_for_edge->backbuf_offset) {
    uint bitmap_inedx = data_for_edge->backbuf_offset + index - 1;
    is_visible = LIB_BITMAP_TEST_BOOL(data_for_edge->esel->sel_bitmap, bitmap_inedx);
  }

  const bool is_sel = mesh_elem_flag_test(eed, MESH_ELEM_SEL);
  const bool is_inside =
      (is_visible && edge_fully_inside_rect(data->rect_fl, screen_co_a, screen_co_b) &&
       lib_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, UNPACK2(screen_co_a), IS_CLIPPED) &&
       lib_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, UNPACK2(screen_co_b), IS_CLIPPED));
  const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    mesh_edge_sel_set(data->vc->em->bm, eed, sel_op_result);
    data->is_done = true;
    data->is_changed = true;
  }
}
static void do_lasso_sel_mesh_doSelEdge_pass1(void *user_data,
                                              MEdge *eed,
                                              const float screen_co_a[2],
                                              const float screen_co_b[2],
                                              int index)
{
  struct LassoSelUserDataForMeshEdge *data_for_edge = user_data;
  LassoSelUserData *data = data_for_edge->data;
  bool is_visible = true;
  if (data_for_edge->backbuf_offset) {
    uint bitmap_inedx = data_for_edge->backbuf_offset + index - 1;
    is_visible = LIB_BITMAP_TEST_BOOL(data_for_edge->esel->sel_bitmap, bitmap_inedx);
  }

  const bool is_sel = mesh_elem_flag_test(eed, MESH_ELEM_SEL);
  const bool is_inside = (is_visible && lib_lasso_is_edge_inside(data->mcoords,
                                                                 data->mcoords_len,
                                                                 UNPACK2(screen_co_a),
                                                                 UNPACK2(screen_co_b),
                                                                 IS_CLIPPED));
  const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    mesh_edge_sel_set(data->vc->em->mesh, eed, sel_op_result);
    data->is_changed = true;
  }
}

static void do_lasso_sel_mesh_doSelFace(void *userData,
                                        MFace *efa,
                                        const float screen_co[2],
                                        int UNUSED(index))
{
  LassoSelUserData *data = userData;
  const bool is_sel = mesh_elem_flag_test(efa, MESH_ELEM_SEL);
  const bool is_inside =
      (lib_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       lib_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED));
  const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    mesh_face_sel_set(data->vc->em->mesh, efa, sel_op_result);
    data->is_changed = true;
  }
}

static bool do_lasso_sel_mesh(ViewCxt *vc,
                              WinGenericUserData *win_userdata,
                              const int mcoords[][2],
                              const int mcoords_len,
                              const eSelOp sel_op)
{
  LassoSelUserData data;
  ToolSettings *ts = vc->scene->toolsettings;
  rcti rect;

  /* set meshedit */
  vc->em = dune_meshedit_from_ob(vc->obedit);

  lib_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassosel_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    if (vc->em-mesh>totvertsel) {
      ed_flag_disable_all(vc->em, MESH_ELEM_SEL);
      data.is_changed = true;
    }
  }

  /* for non zbuf projections, don't change the GL state */
  ed_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

  gpu_matrix_set(vc->rv3d->viewmat);

  const bool use_zbuf = !XRAY_FLAG_ENABLED(vc->v3d);

  struct EditSelBufCache *esel = wk_userdata->data;
  if (use_zbuf) {
    if (win_userdata->data == NULL) {
      editsel_buf_cache_init_with_generic_userdata(win_userdata, vc, ts->selmode);
      esel = win_userdata->data;
      esel->sel_bitmap = drw_sel_buffer_bitmap_from_poly(
          vc->graph, vc->rgn, vc->v3d, mcoords, mcoords_len, &rect, NULL);
    }
  }

  if (ts->selmode & SCE_SEL_VERTEX) {
    if (use_zbuf) {
      data.is_changed |= edbm_backbuf_check_and_sel_verts(
          esel, vc->graph, vc->obedit, vc->medit, sel_op);
    }
    else {
      mesh_foreachScreenVert(
          vc, do_lasso_sel_mesh_doSelVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    }
  }
  if (ts->selmode & SCE_SEL_EDGE) {
    /* Does both use_zbuf and non-use_zbuf versions (need screen cos for both) */
    struct LassoSelUserData_ForMeshEdge data_for_edge = {
        .data = &data,
        .esel = use_zbuf ? esel : NULL,
        .backbuf_offset = use_zbuf ? drw_sel_buffer_cxt_offset_for_ob_elem(
                                         vc->graph, vc->obedit, SCE_SEL_EDGE) :
                                     0,
    };

    const eV3DProjTest clip_flag = V3D_PROJ_TEST_CLIP_NEAR |
                                   (use_zbuf ? 0 : V3D_PROJ_TEST_CLIP_BB);
    /* Fully inside. */
    mesh_foreachScreenEdge_clip_bb_segment(
        vc, do_lasso_sel_mesh__doSelEdge_pass0, &data_for_edge, clip_flag);
    if (data.is_done == false) {
      /* Fall back to partially inside.
       * Clip content to account for edges partially behind the view. */
      mesh_foreachScreenEdge_clip_bb_segment(vc,
                                             do_lasso_sel_mesh_doSelEdge_pass1,
                                             &data_for_edge,
                                             clip_flag | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);
    }
  }

  if (ts->selmode & SCE_SEL_FACE) {
    if (use_zbuf) {
      data.is_changed |= edbm_backbuf_check_and_sel_faces(
          esel, vc->graph, vc->obedit, vc->meshedit, sel_op);
    }
    else {
      mesh_foreachScreenFace(
          vc, do_lasso_sel_mesh_doSelFace, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    }
  }

  if (data.is_changed) {
    edbm_selmode_flush(vc->meshedit);
  }
  return data.is_changed;
}

static void do_lasso_sel_curve_doSel(void *userData,
                                    Nurb *UNUSED(nu),
                                    Point *point,
                                    BezTriple *bezt,
                                    int beztindex,
                                    bool handles_visible,
                                    const float screen_co[2])
{
  LassoSelUserData *data = userData;

  const bool is_inside = lib_lasso_is_point_inside(
      data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED);
  if (bp) {
    const bool is_sel = point->f1 & SEL;
    const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
    if (sel_op_result != -1) {
      SET_FLAG_FROM_TEST(bp->f1, sel_op_result, data->select_flag);
      data->is_changed = true;
    }
  }
  else {
    if (!handles_visible) {
      /* can only be (beztindex == 1) here since handles are hidden */
      const bool is_sel = bezt->f2 & SEL;
      const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
      if (sel_op_result != -1) {
        SET_FLAG_FROM_TEST(bezt->f2, sel_op_result, data->sel_flag);
      }
      bezt->f1 = bezt->f3 = bezt->f2;
      data->is_changed = true;
    }
    else {
      uint8_t *flag_p = (&bezt->f1) + beztindex;
      const bool is_sel = *flag_p & SEL;
      const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_select, is_inside);
      if (sel_op_result != -1) {
        SET_FLAG_FROM_TEST(*flag_p, sel_op_result, data->sel_flag);
        data->is_changed = true;
      }
    }
  }
}

static bool do_lasso_sel_curve(ViewCxt *vc,
                               const int mcoords[][2],
                               const int mcoords_len,
                               const eSelOp sel_op)
{
  const bool desel_all = (sel_op == SEL_OP_SET);
  LassoSelUserData data;
  rcti rect;

  lib_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassosel_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  Curve *curve = (Curve *)vc->obedit->data;
  List *nurbs = dune_curve_editNurbs_get(curve);

  /* For desel all, items to be selected are tagged with tmp flag. Clear that first. */
  if (desel_all) {
    dune_nurbList_flag_set(nurbs, BEZT_FLAG_TMP_TAG, false);
    data.sel_flag = BEZT_FLAG_TMP_TAG;
  }

  ed_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
  nurbs_foreachScreenVert(vc, do_lasso_sel_curve_doSel, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  /* Desel items that were not added to sel (indicated by temp flag). */
  if (desel_all) {
    data.is_changed |= dune_nurbList_flag_set_from_flag(nurbs, BEZT_FLAG_TMP_TAG, SEL);
  }

  if (data.is_changed) {
    dune_curve_nurb_vert_active_validate(vc->obedit->data);
  }
  return data.is_changed;
}

static void do_lasso_sel_lattice_doSel(void *userData, Point *point, const float screen_co[2])
{
  LassoSelUserData *data = userData;
  const bool is_sel = point->f1 & SEL;
  const bool is_inside =
      (lib_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       lib_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED));
  const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    SET_FLAG_FROM_TEST(point->f1, sel_op_result, SEL);
    data->is_changed = true;
  }
}
static bool do_lasso_sel_lattice(ViewCxt *vc,
                                 const int mcoords[][2],
                                 const int mcoords_len,
                                 const eSelOp sel_op)
{
  LassoSelUserData data;
  rcti rect;

  lib_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassosel_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    data.is_changed |= ed_lattice_flags_set(vc->obedit, 0);
  }

  ed_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
  lattice_foreachScreenVert(
      vc, do_lasso_sel_lattice_doSel, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
  return data.is_changed;
}

static void do_lasso_sel_armature_doSelBone(void *userData,
                                            EditBone *ebone,
                                            const float screen_co_a[2],
                                            const float screen_co_b[2])
{
  LassoSelUserData *data = userData;
  const Armature *arm = data->vc->obedit->data;
  if (!EBONE_VISIBLE(arm, ebone)) {
    return;
  }

  int is_ignore_flag = 0;
  int is_inside_flag = 0;

  if (screen_co_a[0] != IS_CLIPPED) {
    if (lib_rcti_isect_pt(data->rect, UNPACK2(screen_co_a)) &&
        lib_lasso_is_point_inside(
            data->mcoords, data->mcoords_len, UNPACK2(screen_co_a), INT_MAX)) {
      is_inside_flag |= BONESEL_ROOT;
    }
  }
  else {
    is_ignore_flag |= BONESEL_ROOT;
  }

  if (screen_co_b[0] != IS_CLIPPED) {
    if (lib_rcti_isect_pt(data->rect, UNPACK2(screen_co_b)) &&
        lib_lasso_is_point_inside(
            data->mcoords, data->mcoords_len, UNPACK2(screen_co_b), INT_MAX)) {
      is_inside_flag |= BONESEL_TIP;
    }
  }
  else {
    is_ignore_flag |= BONESEL_TIP;
  }

  if (is_ignore_flag == 0) {
    if (is_inside_flag == (BONE_ROOTSEL | BONE_TIPSEL) ||
        lib_lasso_is_edge_inside(data->mcoords,
                                 data->mcoords_len,
                                 UNPACK2(screen_co_a),
                                 UNPACK2(screen_co_b),
                                 INT_MAX)) {
      is_inside_flag |= BONESEL_BONE;
    }
  }

  ebone->tmp.i = is_inside_flag | (is_ignore_flag >> 16);
}
static void do_lasso_sel_armature_doSelBone_clip_content(void *userData,
                                                         EditBone *ebone,
                                                         const float screen_co_a[2],
                                                         const float screen_co_b[2])
{
  LassoSelUserData *data = userData;
  Armature *arm = data->vc->obedit->data;
  if (!EBONE_VISIBLE(arm, ebone)) {
    return;
  }

  const int is_ignore_flag = ebone->tmp.i << 16;
  int is_inside_flag = ebone->tmp.i & ~0xFFFF;

  /* When BONESEL_BONE is set, there is nothing to do.
   * When BONE_ROOTSEL or BONE_TIPSEL have been set - they take priority over bone sel */
  if (is_inside_flag & (BONESEL_BONE | BONE_ROOTSEL | BONE_TIPSEL)) {
    return;
  }

  if (lib_lasso_is_edge_inside(
          data->mcoords, data->mcoords_len, UNPACK2(screen_co_a), UNPACK2(screen_co_b), INT_MAX)) {
    is_inside_flag |= BONESEL_BONE;
  }

  ebone->tmp.i = is_inside_flag | (is_ignore_flag >> 16);
}

static bool do_lasso_sel_armature(ViewCxt *vc,
                                  const int mcoords[][2],
                                  const int mcoords_len,
                                  const eSelOp sel_op)
{
  LassoSelUserData data;
  rcti rect;

  lib_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassosel_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    data.is_changed |= ed_armature_edit_desel_all_visible(vc->obedit);
  }

  Armature *arm = vc->obedit->data;

  ed_armature_ebone_list_tmp_clear(arm->edbo);

  ed_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

  /* Op on fully visible (non-clipped) points. */
  armature_foreachScreenBone(
      vc, do_lasso_sel_armature_doSelBone, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  /* Op on bones as segments clipped to the viewport bounds
   * (needed to handle bones with both points outside the view).
   * A separate pass is needed since clipped coordinates can't be used for selecting joints. */
  armature_foreachScreenBone(vc,
                             do_lasso_sel_armature_doSelBone_clip_content,
                             &data,
                             V3D_PROJ_TEST_CLIP_DEFAULT | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);

  data.is_changed |= ed_armature_edit_sel_op_from_tagged(vc->obedit->data, sel_op);

  if (data.is_changed) {
    win_main_add_notifier(NC_OB | ND_BONE_SEL, vc->obedit);
  }
  return data.is_changed;
}

static void do_lasso_sel_mball_doSelElem(void *userData,
                                          struct MetaElem *ml,
                                          const float screen_co[2])
{
  LassoSelUserData *data = userData;
  const bool is_sel = ml->flag & SEL;
  const bool is_inside =
      (lib_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       lib_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], INT_MAX));
  const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    SET_FLAG_FROM_TEST(ml->flag, sel_op_result, SEL);
    data->is_changed = true;
  }
}
static bool do_lasso_sel_meta(ViewCxt *vc,
                              const int mcoords[][2],
                              const int mcoords_len,
                              const eSelOp sel_op)
{
  LassoSelUserData data;
  rcti rect;

  MetaBall *mb = (MetaBall *)vc->obedit->data;

  lib_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassosel_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    data.is_changed |= dune_mball_desel_all(mb);
  }

  ed_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

  mball_foreachScreenElem(
      vc, do_lasso_sel_mball__doSelElem, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  return data.is_changed;
}

static void do_lasso_sel_meshob__doSelVert(void *userData,
                                           MVert *mv,
                                           const float screen_co[2],
                                           int UNUSED(index))
{
  LassoSelUserData *data = userData;
  const bool is_sel = mv->flag & SEL;
  const bool is_inside =
      (lib_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       lib_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED));
  const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    SET_FLAG_FROM_TEST(mv->flag, sel_op_result, SELE);
    data->is_changed = true;
  }
}
static bool do_lasso_sel_paintvert(ViewCxt *vc,
                                   WinGenericUserData *win_userdata,
                                   const int mcoords[][2],
                                   const int mcoords_len,
                                   const eSelOp sel_op)
{
  const bool use_zbuf = !XRAY_ENABLED(vc->v3d);
  Ob *ob = vc->obact;
  Mesh *me = ob->data;
  rcti rect;

  if (me == NULL || me->totvert == 0) {
    return false;
  }

  bool changed = false;
  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    /* flush sel at the end */
    changed |= paintvert_desel_all_visible(ob, SEL_DESEL, false);
  }

  lib_lasso_boundbox(&rect, mcoords, mcoords_len);

  struct EditSelBuf_Cache *esel = win_userdata->data;
  if (use_zbuf) {
    if (win_userdata->data == NULL) {
      editsel_buf_cache_init_with_generic_userdata(win_userdata, vc, SCE_SEL_VERTEX);
      esel = win_userdata->data;
      esel->sel_bitmap = drw_sel_buffer_bitmap_from_poly(
          vc->graph, vc->rgn, vc->v3d, mcoords, mcoords_len, &rect, NULL);
    }
  }

  if (use_zbuf) {
    if (esel->sel_bitmap != NULL) {
      changed |= edbm_backbuf_check_and_sel_verts_obmode(me, esel, sel_op);
    }
  }
  else {
    LassoSelUserData data;

    view3d_userdata_lassosel_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

    ed_view3d_init_mats_rv3d(vc->obact, vc->rv3d);

    meshob_foreachScreenVert(
        vc, do_lasso_sel_meshob__doSelVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

    changed |= data.is_changed;
  }

  if (changed) {
    if (SEL_OP_CAN_DESEL(sel_op)) {
      dune_mesh_msel_validate(me);
    }
    paintvert_flush_flags(ob);
    paintvert_tag_select_update(vc->C, ob);
  }

  return changed;
}
static bool do_lasso_sel_paintface(ViewCxt *vc,
                                   WinGenericUserData *win_userdata,
                                   const int mcoords[][2],
                                   const int mcoords_len,
                                   const eSelOp sel_op)
{
  Ob *ob = vc->obact;
  Mesh *me = ob->data;
  rcti rect;

  if (me == NULL || me->totpoly == 0) {
    return false;
  }

  bool changed = false;
  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    /* flush sel at the end */
    changed |= paintface_desel_all_visible(vc->C, ob, SEL_DESELECT, false);
  }

  lib_lasso_boundbox(&rect, mcoords, mcoords_len);

  struct EditSelBuf_Cache *esel = win_userdata->data;
  if (esel == NULL) {
    editsel_buf_cache_init_with_generic_userdata(win_userdata, vc, SCE_SEL_FACE);
    esel = win_userdata->data;
    esel->sel_bitmap = drw_sel_buffer_bitmap_from_poly(
        vc->graph, vc->rgn, vc->v3d, mcoords, mcoords_len, &rect, NULL);
  }

  if (esel->sel_bitmap) {
    changed |= edbm_backbuf_check_and_sel_faces_obmode(me, esel, sel_op);
  }

  if (changed) {
    paintface_flush_flags(vc->C, ob, SEL);
  }
  return changed;
}

static bool view3d_lasso_sel(Cxt *C,
                             ViewCxt *vc,
                             const int mcoords[][2],
                             const int mcoords_len,
                             const eSelOp sel_op)
{
  Ob *ob = cxt_data_active_ob(C);
  bool changed_multi = false;

  WinGenericUserData win_userdata_buf = {0};
  WinGenericUserData *win_userdata = &win_userdata_buf;

  if (vc->obedit == NULL) { /* Ob Mode */
    if win_paint_sel_face_test(ob)) {
      changed_multi |= do_lasso_sel_paintface(vc, win_userdata, mcoords, mcoords_len, sel_op);
    }
    else if (dune_paint_sel_vert_test(ob)) {
      changed_multi |= do_lasso_sel_paintvert(vc, win_userdata, mcoords, mcoords_len, sel_op);
    }
    else if (ob &&
             (ob->mode & (OB_MODE_VERTEX_PAINT | OB_MODE_WEIGHT_PAINT | OB_MODE_TEXTURE_PAINT))) {
      /* pass */
    }
    else if (ob && (ob->mode & OB_MODE_PARTICLE_EDIT)) {
      changed_multi |= PE_lasso_sel(C, mcoords, mcoords_len, sel_op);
    }
    else if (ob && (ob->mode & OB_MODE_POSE)) {
      changed_multi |= do_lasso_sel_pose(vc, mcoords, mcoords_len, sel_op);
      if (changed_multi) {
        ed_outliner_sel_sync_from_pose_bone_tag(C);
      }
    }
    else {
      changed_multi |= do_lasso_sel_objs(vc, mcoords, mcoords_len, sel_op);
      if (changed_multi) {
        ed_outliner_sel_sync_from_ob_tag(C);
      }
    }
  }
  else { /* Edit Mode */
    FOREACH_OB_IN_MODE_BEGIN (vc->view_layer, vc->v3d, ob->type, ob->mode, ob_iter) {
      ed_view3d_viewcxt_init_ob(vc, ob_iter);
      bool changed = false;

      switch (vc->obedit->type) {
        case OB_MESH:
          changed = do_lasso_sel_mesh(vc, wm_userdata, mcoords, mcoords_len, sel_op);
          break;
        case OB_CURVES_LEGACY:
        case OB_SURF:
          changed = do_lasso_sel_curve(vc, mcoords, mcoords_len, sel_op);
          break;
        case OB_LATTICE:
          changed = do_lasso_sel_lattice(vc, mcoords, mcoords_len, sel_op);
          break;
        case OB_ARMATURE:
          changed = do_lasso_sel_armature(vc, mcoords, mcoords_len, sel_op);
          if (changed) {
            ed_outliner_sel_sync_from_edit_bone_tag(C);
          }
          break;
        case OB_MBALL:
          changed = do_lasso_sel_meta(vc, mcoords, mcoords_len, sel_op);
          break;
        default:
          lib_assert_msg(0, "lasso sel on incorrect ob type");
          break;
      }

      if (changed) {
        graph_id_tag_update(vc->obedit->data, ID_RECALC_SEL);
        win_ev_add_notifier(C, NC_GEOM | ND_SEL, vc->obedit->data);
        changed_multi = true;
      }
    }
    FOREACH_OB_IN_MODE_END;
  }

  win_generic_user_data_free(win_userdata);

  return changed_multi;
}

/* lasso op gives props, but since old code works
 * with short array we convert */
static int view3d_lasso_sel_ex(Cxt *C, WinOp *op)
{
  ViewCxt vc;
  int mcoords_len;
  const int(*mcoords)[2] = win_gesture_lasso_path_to_array(C, op, &mcoords_len);

  if (mcoords) {
    Graph *graph = cxt_data_ensure_eval_graph(C);
    view3d_op_needs_opengl(C);
    dune_ob_update_sel_id(cxt_data_main(C));

    /* setup view cxt for arg to cbs */
    ed_view3d_viewcxt_init(C, &vc, graph);

    eSelOp sel_op = api_enum_get(op->ptr, "mode");
    bool changed_multi = view3d_lasso_sel(C, &vc, mcoords, mcoords_len, sel_op);

    mem_free((void *)mcoords);

    if (changed_multi) {
      return OP_FINISHED;
    }
    return OP_CANCELLED;
  }
  return OP_PASS_THROUGH;
}

void VIEW3D_OT_sel_lasso(WinOpType *ot)
{
  ot->name = "Lasso Sel";
  ot->description = "Sel items using lasso sel";
  ot->idname = "VIEW3D_OT_sel_lasso";

  ot->invoke = win_gesture_lasso_invoke;
  ot->modal = win_gesture_lasso_modal;
  ot->ex = view3d_lasso_sel_ex;
  ot->poll = view3d_sel_data;
  ot->cancel = win_gesture_lasso_cancel;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_DEPENDS_ON_CURSOR;

  /* props */
  win_op_proos_gesture_lasso(ot);
  win_op_props_sel_op(ot);
}

/* Cursor Picking */
/* The max num of menu items in an ob sel menu */
typedef struct SelMenuItemF {
  char idname[MAX_ID_NAME - 2];
  int icon;
  Base *base_ptr;
  void *item_ptr;
} SelMenuItemF;

#define SEL_MENU_SIZE 22
static SelMenuItemF ob_mouse_sel_menu_data[SEL_MENU_SIZE];

/* special (crappy) op only for menu sel */
static const EnumPropItem *ob_sel_menu_enum_itemf(Cxt *C,
                                                  ApiPtr *UNUSED(ptr),
                                                  ApiProp *UNUSED(prop),
                                                  bool *r_free)
{
  EnumPropItem *item = NULL, item_tmp = {0};
  int totitem = 0;
  int i = 0;

  /* Don't need cxt but avoid API doc-generation using this. */
  if (C == NULL || ob_mouse_sel_menu_data[i].idname[0] == '\0') {
    return ApiDummy_NULL_items;
  }

  for (; i < SEL_MENU_SIZE && ob_mouse_sel_menu_data[i].idname[0] != '\0'; i++) {
    item_tmp.name = ob_mouse_sel_menu_data[i].idname;
    item_tmp.id = ob_mouse_sel_menu_data[i].idname;
    item_tmp.val = i;
    item_tmp.icon = ob_mouse_sel_menu_data[i].icon;
    api_enum_item_add(&item, &totitem, &item_tmp);
  }

  api_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static int ob_sel_menu_ex(Cxt *C, WinOp *op)
{
  const int name_index = api_enum_get(op->ptr, "name");
  const bool extend = api_bool_get(op->ptr, "extend");
  const bool desel = api_bool_get(op->ptr, "desel");
  const bool toggle = api_bool_get(op->ptr, "toggle");
  bool changed = false;
  const char *name = ob_mouse_sel_menu_data[name_index].idname;

  View3D *v3d = cxt_win_view3d(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  const Base *oldbasact = BASACT(view_layer);

  Base *basact = NULL;
  CXT_DATA_BEGIN (C, Base *, base, selectable_bases) {
    /* This is a bit dodgy, there should only be ONE object with this name,
     * but lib objs can mess this up. */
    if (STREQ(name, base->ob->id.name + 2)) {
      basact = base;
      break;
    }
  }
  CXT_DATA_END;

  if (basact == NULL) {
    return OP_CANCELLED;
  }
  UNUSED_VARS_NDEBUG(v3d);
  lib_assert(BASE_SELECTABLE(v3d, basact));

  if (extend) {
    ed_ob_base_sel(basact, BA_SEL);
    changed = true;
  }
  else if (desel) {
    ed_ob_base_sel(basact, BA_DESEL);
    changed = true;
  }
  else if (toggle) {
    if (basact->flag & BASE_SELECTED) {
      if (basact == oldbasact) {
        ed_ob_base_sel(basact, BA_DESEL);
        changed = true;
      }
    }
    else {
      ed_ob_base_sel(basact, BA_SEL);
      changed = true;
    }
  }
  else {
    ob_desel_all_except(view_layer, basact);
    ed_ob_base_sel(basact, BA_SEL);
    changed = true;
  }

  if ((oldbasact != basact)) {
    ed_ob_base_activate(C, basact);
  }

  /* weak but ensures we activate menu again before using the enum */
  memset(ob_mouse_sel_menu_data, 0, sizeof(ob_mouse_sel_menu_data));

  /* undo? */
  if (changed) {
    Scene *scene = cxt_data_scene(C);
    graph_id_tag_update(&scene->id, ID_RECALC_SEL);
    win_ev_add_notifier(C, NC_SCENE | ND_OB_SEL, scene);

    ed_outliner_sel_sync_from_ob_tag(C);

    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

void VIEW3D_OT_sel_menu(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Sel Menu";
  ot->description = "Menu ob sel";
  ot->idname = "VIEW3D_OT_sel_menu";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = ob_sel_menu_ex;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Ob.id.name to sel (dynamic enum). */
  prop = api_def_enum(ot->sapi, "name", DummyApi_NULL_items, 0, "Ob Name", "");
  api_def_enum_fns(prop, ob_sel_menu_enum_itemf);
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_ENUM_NO_TRANSLATE);
  ot->prop = prop;

  prop = api_def_bool(ot->sapi, "extend", 0, "Extend", "");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_bool(ot->sapi, "desel", 0, "Desel", "");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_bool(ot->srna, "toggle", 0, "Toggle", "");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* return True when a menu was activate */
static bool ob_mouse_sel_menu(Cxt *C,
                              ViewCxt *vc,
                              const GPUSelResult *buffer,
                              const int hits,
                              const int mval[2],
                              const struct SelPick_Params *params,
                              Base **r_basact)
{
  int base_count = 0;
  bool ok;
  LinkNodePair linklist = {NULL, NULL};

  /* handle base->ob->sel_id */
  CXT_DATA_BEGIN (C, Base *, base, selectable_bases) {
    ok = false;

    /* two sel methods, the CTRL sel uses max dist of 15 */
    if (buffer) {
      for (int a = 0; a < hits; a++) {
        /* index was converted */
        if (base->ob->runtime.sel_id == (buffer[a].id & ~0xFFFF0000)) {
          ok = true;
          break;
        }
      }
    }
    else {
      const int dist = 15 * U.pixelsize;
      if (ed_view3d_project_base(vc->rgn, base) == V3D_PROJ_RET_OK) {
        const int delta_px[2] = {base->sx - mval[0], base->sy - mval[1]};
        if (len_manhattan_v2_int(delta_px) < dist) {
          ok = true;
        }
      }
    }

    if (ok) {
      base_count++;
      lib_linklist_append(&linklist, base);

      if (base_count == SEL_MENU_SIZE) {
        break;
      }
    }
  }
  CXT_DATA_END;

  *r_basact = NULL;

  if (base_count == 0) {
    return false;
  }
  if (base_count == 1) {
    Base *base = (Base *)linklist.list->link;
    lib_linklist_free(linklist.list, NULL);
    *r_basact = base;
    return false;
  }

  /* UI, full in static array vals that we later use in an enum fn */
  LinkNode *node;
  int i;
l
  memset(ob_mouse_sel_menu_data, 0, sizeof(ob_mouse_sel_menu_data));

  for (node = linklist.list, i = 0; node; node = node->next, i++) {
    Base *base = node->link;
    Ob *ob = base->object;
    const char *name = ob->id.name + 2;

    lib_strncpy(ob_mouse_sel_menu_data[i].idname, name, MAX_ID_NAME - 2);
    ob_mouse_sel_menu_data[i].icon = ui_icon_from_id(&ob->id);
  }

  WinOpType *ot = win_optype_find("VIEW3D_OT_sel_menu", false);
  ApiPtr ptr;

  win_op_props_create_ptr(&ptr, ot);
  api_bool_set(&ptr, "extend", params->sel_op == SEL_OP_ADD);
  api_bool_set(&ptr, "desel", params->sel_op == SEL_OP_SUB);
  api_bool_set(&ptr, "toggle", params->sel_op == SEL_OP_XOR);
  win_op_name_call_ptr(C, ot, WIN_OP_INVOKE_DEFAULT, &ptr, NULL);
  win_op_props_free(&ptr);

  lib_linklist_free(linklist.list, NULL);
  return true;
}

static int bone_sel_menu_ex(Cxt *C, WinOp *op)
{
  const int name_index = api_enum_get(op->ptr, "name");

  const struct SelPickParams params = {
      .sel_op = ed_sel_op_from_bools(api_bool_get(op->ptr, "extend"),
                                     api_bool_get(op->ptr, "desel"),
                                     api_bool_get(op->ptr, "toggle")),
  };

  View3D *v3d = cxt_win_view3d(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  const Base *oldbasact = BASACT(view_layer);

  Base *basact = ob_mouse_sel_menu_data[name_index].base_ptr;

  if (basact == NULL) {
    return OP_CANCELLED;
  }

  lib_assert(BASE_SELECTABLE(v3d, basact));

  if (basact->ob->mode & OB_MODE_EDIT) {
    EditBone *ebone = (EditBone *)ob_mouse_sel_menu_data[name_index].item_ptr;
    ed_armature_edit_sel_pick_bone(C, basact, ebone, BONE_SELECTED, &params);
  }
  else {
    PoseChannel *pchan = (PoseChannel *)ob_mouse_sel_menu_data[name_index].item_ptr;
    ed_armature_pose_sel_pick_bone(view_layer, v3d, basact->ob, pchan->bone, &params);
  }

  /* Weak but ensures we activate the menu again before using the enum. */
  memset(ob_mouse_sel_menu_data, 0, sizeof(ob_mouse_sel_menu_data));

  /* We make the armature selected:
   * Not-sel active ob in pose-mode won't work well for tools. */
  ed_ob_base_sel(basact, BA_SEL);

  win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, basact->ob);
  em_ev_add_notifier(C, NC_OB | ND_BONE_ACTIVE, basact->ob);

  /* In weight-paint, we use selected bone to sel vertex-group,
   * so don't switch to new active object. */
  if (oldbasact) {
    if (basact->obt->mode & OB_MODE_EDIT) {
      /* Pass. */
    }
    else if (oldbasact->ob->mode & OB_MODE_ALL_WEIGHT_PAINT) {
      /* Prevent activating.
       * Sel causes this to be considered the 'active' pose in weight-paint mode.
       * Eventually this limitation may be removed.
       * For now, de-sel all other pose objs deforming this mesh. */
      ed_armature_pose_sel_in_wpaint_mode(view_layer, basact);
    }
    else {
      if (oldbasact != basact) {
        ed_ob_base_activate(C, basact);
      }
    }
  }

  /* Undo? */
  Scene *scene = cxt_data_scene(C);
  graph_id_tag_update(&scene->id, ID_RECALC_SEL);
  graph_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);
  win_ev_add_notifier(C, NC_SCENE | ND_OB_SEL, scene);

  ed_outliner_sel_sync_from_ob_tag(C);

  return OP_FINISHED;
}

void VIEW3D_OT_bone_sel_menu(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Sel Menu";
  ot->description = "Menu bone sel";
  ot->idname = "VIEW3D_OT_bone_sel_menu";

  /* api cbs */
  ot->invoke = win_menu_invoke;
  ot->ex = bone_sel_menu_ex;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Ob.id.name to sel (dynamic enum). */
  prop = api_def_enum(ot->sapi, "name", DummyApi_NULL_items, 0, "Bone Name", "");
  api_def_enum_fns(prop, ob_sel_menu_enum_itemf);
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_ENUM_NO_TRANSLATE);
  ot->prop = prop;

  prop = api_def_bool(ot->sapi, "extend", 0, "Extend", "");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_bool(ot->sapi, "desel", 0, "Deselect", "");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_bool(ot->sapi, "toggle", 0, "Toggle", "");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* return True when a menu was activated. */
static bool bone_mouse_sel_menu(Cxt *C,
                                const GPUSelResult *buffer,
                                const int hits,
                                const bool is_editmode,
                                const struct SelPick_Params *params)
{
  lib_assert(buffer);

  int bone_count = 0;
  LinkNodePair base_list = {NULL, NULL};
  LinkNodePair bone_list = {NULL, NULL};
  GSet *added_bones = lib_gset_ptr_new("Bone mouse select menu");

  /* Sel logic taken from ed_armature_pick_bone_from_selbuffer_impl in armature_selc */
  for (int a = 0; a < hits; a++) {
    void *bone_ptr = NULL;
    Base *bone_base = NULL;
    uint hitresult = buffer[a].id;

    if (!(hitresult & BONESEL_ANY)) {
      /* To avoid including objs in sel. */
      continue;
    }

    hitresult &= ~BONESEL_ANY;
    const uint hit_ob = hitresult & 0xFFFF;

    /* Find the hit bone base (armature ob). */
    CXT_DATA_BEGIN (C, Base *, base, selectable_bases) {
      if (base->ob->runtime.sel_id == hit_ob) {
        bone_base = base;
        break;
      }
    }
    CXT_DATA_END;

    if (!bone_base) {
      continue;
    }

    /* Determine what the current bone is */
    if (is_editmode) {
      EditBone *ebone;
      const uint hit_bone = (hitresult & ~BONESEL_ANY) >> 16;
      Armature *arm = bone_base->object->data;
      ebone = lib_findlink(arm->edbo, hit_bone);
      if (ebone && !(ebone->flag & BONE_UNSELECTABLE)) {
        bone_ptr = ebone;
      }
    }
    else {
      PoseChannel *pchan;
      const uint hit_bone = (hitresult & ~BONESEL_ANY) >> 16;
      pchan = lib_findlink(&bone_base->ob->pose->chanbase, hit_bone);
      if (pchan && !(pchan->bone->flag & BONE_UNSELECTABLE)) {
        bone_ptr = pchan;
      }
    }

    if (!bone_ptr) {
      continue;
    }
    /* We can hit a bone multiple times, so make sure we are not adding an alrdy included bone
     * to the list. */
    const bool is_duplicate_bone = lib_gset_haskey(added_bones, bone_ptr);

    if (!is_duplicate_bone) {
      bone_count++;
      lib_linklist_append(&base_list, bone_base);
      lib_linklist_append(&bone_list, bone_ptr);
      lib_gset_insert(added_bones, bone_ptr);

      if (bone_count == SEL_MENU_SIZE) {
        break;
      }
    }
  }

  lib_gset_free(added_bones, NULL);

  if (bone_count == 0) {
    return false;
  }
  if (bone_count == 1) {
    lib_linklist_free(base_list.list, NULL);
    lib_linklist_free(bone_list.list, NULL);
    return false;
  }

  /* UI, full in static array vals that we later use in an enum fn */
  LinkNode *bone_node, *base_node;
  int i;

  memset(ob_mouse_sel_menu_data, 0, sizeof(ob_mouse_sel_menu_data));

  for (base_node = base_list.list, bone_node = bone_list.list, i = 0; bone_node;
       base_node = base_node->next, bone_node = bone_node->next, i++) {
    char *name;

    ob_mouse_sel_menu_data[i].base_ptr = base_node->link;

    if (is_editmode) {
      EditBone *ebone = bone_node->link;
      ob_mouse_sel_menu_data[i].item_ptr = ebone;
      name = ebone->name;
    }
    else {
      PoseChannel *pchan = bone_node->link;
      ob_mouse_sel_menu_data[i].item_ptr = pchan;
      name = pchan->name;
    }

    lib_strncpy(ob_mouse_sel_menu_data[i].idname, name, MAX_ID_NAME - 2);
    ob_mouse_sel_menu_data[i].icon = ICON_BONE_DATA;
  }

  WinOpType *ot = win_optype_find("VIEW3D_OT_bone_sel_menu", false);
  ApiPtr ptr;

  win_op_props_create_ptr(&ptr, ot);
  api_bool_set(&ptr, "extend", params->sel_op == SEL_OP_ADD);
  woi_bool_set(&ptr, "desel", params->sel_op == SEL_OP_SUB);
  api_bool_set(&ptr, "toggle", params->sel_op == SEL_OP_XOR);
  win_op_name_call_ptr(C, ot, WIN_OP_INVOKE_DEFAULT, &ptr, NULL);
  win_op_props_free(&ptr);

  lib_linklist_free(base_list.list, NULL);
  lib_linklist_free(bone_list.list, NULL);
  return true;
}

static bool selbuffer_has_bones(const GPUSelResult *buffer, const uint hits)
{
  for (uint i = 0; i < hits; i++) {
    if (buffer[i].id & 0xFFFF0000) {
      return true;
    }
  }
  return false;
}

/* util fn for mixed_bones_ob_selbuffer */
static int selbuffer_ret_hits_15(GPUSelResult *UNUSED(buffer), const int hits15)
{
  return hits15;
}

static int selbuffer_ret_hits_9(GPUSelResult *buffer, const int hits15, const int hits9)
{
  const int ofs = hits15;
  memcpy(buffer, buffer + ofs, hits9 * sizeof(GPUSelResult));
  return hits9;
}

static int selbuffer_ret_hits_5(GPUSelResult *buffer,
                                const int hits15,
                                const int hits9,
                                const int hits5)
{
  const int ofs = hits15 + hits9;
  memcpy(buffer, buffer + ofs, hits5 * sizeof(GPUSelResult));
  return hits5;
}

/* Populate a sel buffer with objs and bones, if there are any.
 * Checks three sel levels and compare.
 * param do_nearest_xray_if_supported: When set, read in hits that don't stop
 * at the nearest surface. The hits must still be ordered by depth.
 * Needed so we can step to the next, non-active ob when it's alrdy sel, see: T76445. */
static int mixed_bones_ob_selbuffer(ViewCxt *vc,
                                    GPUSelResult *buffer,
                                    const int buffer_len,
                                    const int mval[2],
                                    eV3DSelObFilter select_filter,
                                    bool do_nearest,
                                    bool do_nearest_xray_if_supported,
                                    const bool do_material_slot_sel)
{
  rcti rect;
  int hits15, hits9 = 0, hits5 = 0;
  bool has_bones15 = false, has_bones9 = false, has_bones5 = false;

  int sel_mode = (do_nearest ? VIEW3D_SEL_PICK_NEAREST : VIEW3D_SEL_PICK_ALL);
  int hits = 0;

  if (do_nearest_xray_if_supported) {
    if ((U.gpu_flag & USER_GPU_FLAG_NO_DEPT_PICK) == 0) {
      sel_mode = VIEW3D_SEL_PICK_ALL;
    }
  }

  /* we _must_ end cache before return, use 'goto finally' */
  view3d_opengl_sel_cache_begin();

  lib_rcti_init_pt_radius(&rect, mval, 14);
  hits15 = view3d_opengl_sel_ex(
      vc, buffer, buffer_len, &rect, sel_mode, sel_filter, do_material_slot_selection);
  if (hits15 == 1) {
    hits = selbuffer_ret_hits_15(buffer, hits15);
    goto finally;
  }
  else if (hits15 > 0) {
    int ofs;
    has_bones15 = selbuffer_has_bones(buffer, hits15);

    ofs = hits15;
    lib_rcti_init_pt_radius(&rect, mval, 9);
    hits9 = view3d_opengl_sel(
        vc, buffer + ofs, buffer_len - ofs, &rect, sel_mode, select_filter);
    if (hits9 == 1) {
      hits = selbuffer_ret_hits_9(buffer, hits15, hits9);
      goto finally;
    }
    else if (hits9 > 0) {
      has_bones9 = selbuffer_has_bones(buffer + ofs, hits9);

      ofs += hits9;
      lib_rcti_init_pt_radius(&rect, mval, 5);
      hits5 = view3d_opengl_sel(
          vc, buffer + ofs, buffer_len - ofs, &rect, sel_mode, sel_filter);
      if (hits5 == 1) {
        hits = selbuffer_ret_hits_5(buffer, hits15, hits9, hits5);
        goto finally;
      }
      else if (hits5 > 0) {
        has_bones5 = selbuffer_has_bones(buffer + ofs, hits5);
      }
    }

    if (has_bones5) {
      hits = selbuffer_ret_hits_5(buffer, hits15, hits9, hits5);
      goto finally;
    }
    else if (has_bones9) {
      hits = selbuffer_ret_hits_9(buffer, hits15, hits9);
      goto finally;
    }
    else if (has_bones15) {
      hits = selbuffer_ret_hits_15(buffer, hits15);
      goto finally;
    }

    if (hits5 > 0) {
      hits = selbuffer_ret_hits_5(buffer, hits15, hits9, hits5);
      goto finally;
    }
    else if (hits9 > 0) {
      hits = selbuffer_ret_hits_9(buffer, hits15, hits9);
      goto finally;
    }
    else {
      hits = selbuffer_ret_hits_15(buffer, hits15);
      goto finally;
    }
  }

finally:
  view3d_opengl_sel_cache_end();
  return hits;
}

static int mixed_bones_ob_selbuffer_extended(ViewCxt *vc,
                                             GPUSelResult *buffer,
                                             const int buffer_len,
                                             const int mval[2],
                                             eV3DSelObFilter sel_filter,
                                             bool use_cycle,
                                             bool enumerate,
                                             bool *r_do_nearest)
{
  bool do_nearest = false;
  View3D *v3d = vc->v3d;

  /* define if we use solid nearest sel or not */
  if (use_cycle) {
    /* Update the coordinates (even if the return val isn't used). */
    const bool has_motion = win_cursor_test_motion_and_update(mval);
    if (!XRAY_ACTIVE(v3d)) {
      do_nearest = has_motion;
    }
  }
  else {
    if (!XRAY_ACTIVE(v3d)) {
      do_nearest = true;
    }
  }

  if (r_do_nearest) {
    *r_do_nearest = do_nearest;
  }

  do_nearest = do_nearest && !enumerate;

  int hits = mixed_bones_ob_selbuffer(
      vc, buffer, buffer_len, mval, sel_filter, do_nearest, true, false);

  return hits;
}

/* param has_bones: When true, skip non-bone hits, also allow bases to be used
 * that are visible but not select-able,
 * since you may be in pose mode with an un-selectable ob.
 *
 * return the active base or NULL */
static Base *mouse_sel_eval_buffer(ViewCxt *vc,
                                   const GPUSelResult *buffer,
                                   int hits,
                                   Base *startbase,
                                   bool has_bones,
                                   bool do_nearest,
                                   int *r_sub_sel)
{
  ViewLayer *view_layer = vc->view_layer;
  View3D *v3d = vc->v3d;
  Base *base, *basact = NULL;
  int a;
  int sub_sel_id = 0;

  if (do_nearest) {
    uint min = 0xFFFFFFFF;
    int selcol = 0;

    if (has_bones) {
      /* we skip non-bone hits */
      for (a = 0; a < hits; a++) {
        if (min > buffer[a].depth && (buffer[a].id & 0xFFFF0000)) {
          min = buffer[a].depth;
          selcol = buffer[a].id & 0xFFFF;
          sub_sel_id = (buffer[a].id & 0xFFFF0000) >> 16;
        }
      }
    }
    else {
      int sel_id_exclude = 0;
      /* Only exclude active ob when it is selected. */
      if (BASACT(view_layer) && (BASACT(view_layer)->flag & BASE_SELECTED) && hits > 1) {
        sel_id_exclude = BASACT(view_layer)->ob->runtime.sel_id;
      }

      /* Find the best active & non-active hits.
       * Checking if `hits > 1` isn't a reliable way to know
       * if there are multiple objs selected since it's possible the same ob
       * generates multiple hits, either from:
       * Multiple sub-components (bones & camera tracks).
       * Multiple selectable elements such as the ob center and the geometry.
       * For this reason, keep track of the best hit as well as the best hit that
       * excludes the selected & active ob, using this val when it's valid. */

      uint min_not_active = min;
      int hit_index = -1, hit_index_not_active = -1;

      for (a = 0; a < hits; a++) {
        /* Any object. */
        if (min > buffer[a].depth) {
          min = buffer[a].depth;
          hit_index = a;
        }
        /* Any ob other than the active-selected. */
        if (sel_id_exclude != 0) {
          if (min_not_active > buffer[a].depth && sel_id_exclude != (buffer[a].id & 0xFFFF)) {
            min_not_active = buffer[a].depth;
            hit_index_not_active = a;
          }
        }
      }

      /* When the active was selected, first try to use the index
       * for the best non-active hit that was found. */
      if (hit_index_not_active != -1) {
        hit_index = hit_index_not_active;
      }

      if (hit_index != -1) {
        selcol = buffer[hit_index].id & 0xFFFF;
        sub_selection_id = (buffer[hit_index].id & 0xFFFF0000) >> 16;
        /* No need to set `min` to `buffer[hit_index].depth`, it's not used from now on. */
      }
    }

    base = FIRSTBASE(view_layer);
    while (base) {
      if (has_bones ? BASE_VISIBLE(v3d, base) : BASE_SELECTABLE(v3d, base)) {
        if (base->ob->runtime.id == selcol) {
          break;
        }
      }
      base = base->next;
    }
    if (base) {
      basact = base;
    }
  }
  else {

    base = startbase;
    while (base) {
      /* skip objects with select restriction, to prevent prematurely ending this loop
       * with an un-selectable choice */
      if (has_bones ? (base->flag & BASE_VISIBLE_VIEWLAYER) == 0 :
                      (base->flag & BASE_SELECTABLE) == 0) {
        base = base->next;
        if (base == NULL) {
          base = FIRSTBASE(view_layer);
        }
        if (base == startbase) {
          break;
        }
      }

      if (has_bones ? BASE_VISIBLE(v3d, base) : BASE_SELECTABLE(v3d, base)) {
        for (a = 0; a < hits; a++) {
          if (has_bones) {
            /* skip non-bone objs */
            if (buffer[a].id & 0xFFFF0000) {
              if (base->ob->runtime.seli_id == (buffer[a].id & 0xFFFF)) {
                basact = base;
              }
            }
          }
          else {
            if (base->ob->runtime.select_id == (buffer[a].id & 0xFFFF)) {
              basact = base;
            }
          }
        }
      }

      if (basact) {
        break;
      }

      base = base->next;
      if (base == NULL) {
        base = FIRSTBASE(view_layer);
      }
      if (base == startbase) {
        break;
      }
    }
  }

  if (basact && r_sub_selection) {
    *r_sub_selection = sub_selection_id;
  }

  return basact;
}

static Base *mouse_sel_ob_center(ViewCxt *vc, Base *startbase, const int mval[2])
{
  ARgn *rgn = vc->rgn;
  ViewLayer *view_layer = vc->view_layer;
  View3D *v3d = vc->v3d;

  Base *oldbasact = BASACT(view_layer);

  const float mval_fl[2] = {(float)mval[0], (float)mval[1]};
  float dist = ed_view3d_sel_dist_px() * 1.3333f;
  Base *basact = NULL;

  /* Put the active object at a disadvantage to cycle through other objects. */
  const float penalty_dist = 10.0f * UI_DPI_FAC;
  Base *base = startbase;
  while (base) {
    if (BASE_SELECTABLE(v3d, base)) {
      float screen_co[2];
      if (ed_view3d_project_float_global(
              rgn, base->ob->obmat[3], screen_co, V3D_PROJ_TEST_CLIP_DEFAULT) ==
          V3D_PROJ_RET_OK) {
        float dist_test = len_manhattan_v2v2(mval_fl, screen_co);
        if (base == oldbasact) {
          dist_test += penalty_dist;
        }
        if (dist_test < dist) {
          dist = dist_test;
          basact = base;
        }
      }
    }
    base = base->next;

    if (base == NULL) {
      base = FIRSTBASE(view_layer);
    }
    if (base == startbase) {
      break;
    }
  }
  return basact;
}

static Base *ed_view3d_give_base_under_cursor_ex(Cxt *C,
                                                 const int mval[2],
                                                 int *r_material_slot)
{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  ViewCxt vc;
  Base *basact = NULL;
  GPUSelResult buffer[MAXPICKELEMS];

  /* setup view cxt for arg to cbs */
  view3d_op_needs_opengl(C);
  dune_ob_update_sel_id(cxt_data_main(C));

  ed_view3d_viewcxt_init(C, &vc, graph);

  const bool do_nearest = !XRAY_ACTIVE(vc.v3d);
  const bool do_material_slot_sel = r_material_slot != NULL;
  const int hits = mixed_bones_ob_selbuffer(&vc,
                                            buffer,
                                            ARRAY_SIZE(buffer),
                                            mval,
                                            VIEW3D_SEL_FILTER_NOP,
                                            do_nearest,
                                            false,
                                            do_material_slot_selection);

  if (hits > 0) {
    const bool has_bones = (r_material_slot == NULL) && selectbuffer_has_bones(buffer, hits);
    basact = mouse_sel_eval_buffer(&vc,
                                   buffer,
                                   hits,
                                   vc.view_layer->ob_bases.first,
                                   has_bones,
                                   do_nearest,
                                   r_material_slot);
  }

  return basact;
}


Base *ed_view3d_give_base_under_cursor(Cxt *C, const int mval[2])
{
  return ed_view3d_give_base_under_cursor_ex(C, mval, NULL);
}

Object *ed_view3d_give_object_under_cursor(Cxt *C, const int mval[2])
{
  Base *base = ed_view3d_give_base_under_cursor(C, mval);
  if (base) {
    return base->ob;
  }
  return NULL;
}

struct Obj *es_view3d_give_material_slot_under_cursor(struct Cxt *C,
                                                      const int mval[2],
                                                      int *r_material_slot)
{
  Base *base = ed_view3d_give_base_under_cursor_ex(C, mval, r_material_slot);
  if (base) {
    return base->ob;
  }
  return NULL;
}

bool ed_view3d_is_ob_under_cursor(Cxt *C, const int mval[2])
{
  return ed_view3d_give_ob_under_cursor(C, mval) != NULL;
}

static void desel_all_tracks(MovieTracking *tracking)
{
  MovieTrackingOb *ob;

  ob = tracking->objs.first;
  while (ob) {
    List *tracksbase = dune_tracking_ob_get_tracks(tracking, ob);
    MovieTrackingTrack *track = tracksbase->first;

    while (track) {
      dune_tracking_track_desel(track, TRACK_AREA_ALL);

      track = track->next;
    }

    ob = ob->next;
  }
}

static bool ed_ob_sel_pick_camera_track(Cxt *C,
                                        Scene *scene,
                                        Base *basact,
                                        MovieClip *clip,
                                        const struct GPUSelResult *buffer,
                                        const short hits,
                                        const struct SelPickParams *params)
{
  bool changed = false;
  bool found = false;

  MovieTracking *tracking = &clip->tracking;
  List *tracksbase = NULL;
  MovieTrackingTrack *track = NULL;

  for (int i = 0; i < hits; i++) {
    const int hitresult = buffer[i].id;

    /* If there's bundles in buffer sel bundles first,
     * so non-camera elements should be ignored in buffer. */
    if (basact->ob->runtime.sel_id != (hitresult & 0xFFFF)) {
      continue;
    }
    /* Index of bundle is 1<<16-based. if there's no "bone" index
     * in height word, this buffer val belongs to camera. not to bundle. */
    if ((hitresult & 0xFFFF0000) == 0) {
      continue;
    }

    track = dune_tracking_track_get_indexed(&clip->tracking, hitresult >> 16, &tracksbase);
    found = true;
    break;
  }

  /* Note `params->desel_all` is ignored for tracks as in this case
   * all objs will be de-selected (not tracks). */
  if (params->sel_op == SEL_OP_SET) {
    if ((found && params->sel_passthrough) && TRACK_SELECTED(track)) {
      found = false;
    }
    else if (found /* `|| params->desel_all` */) {
      /* Desel everything. */
      desel_all_tracks(tracking);
      changed = true;
    }
  }

  if (found) {
    switch (params->sel_op) {
      case SEL_OP_ADD: {
        dune_tracking_track_sel(tracksbase, track, TRACK_AREA_ALL, true);
        break;
      }
      case SEL_OP_SUB: {
        dune_tracking_track_desel(track, TRACK_AREA_ALL);
        break;
      }
      case SEL_OP_XOR: {
        if (TRACK_SEL(track)) {
          dune_tracking_track_desel(track, TRACK_AREA_ALL);
        }
        else {
          dune_tracking_track_sel(tracksbase, track, TRACK_AREA_ALL, true);
        }
        break;
      }
      case SEL_OP_SET: {
        dune_tracking_track_sel(tracksbase, track, TRACK_AREA_ALL, false);
        break;
      }
      case SEL_OP_AND: {
        lib_assert_unreachable(); /* Doesn't make sense for picking. */
        break;
      }
    }

    graoh_id_tag_update(&scene->id, ID_RECALC_SEL);
    graph_id_tag_update(&clip->id, ID_RECALC_SEL);
    win_ev_add_notifier(C, NC_MOVIECLIP | ND_SEL, track);
    win_ev_add_notifier(C, NC_SCENE | ND_OB_SEL, scene);

    changed = true;
  }

  return changed || found;
}

static bool ed_ob_sel_pick(Cxt *C,
                           const int mval[2],
                           const struct SelPickParams *params,
                           bool obcenter,
                           bool enumerate,
                           bool ob)
{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  ViewCxt vc;
  /* Setup view cxt for arg to cbs */
  ed_view3d_viewcxt_init(C, &vc, graph);

  Scene *scene = ed_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  View3D *v3d = cxt_win_view3d(C);
  /* Don't set when the cxt has no active object (hidden), see: T60807. */
  const Base *oldbasact = vc.obact ? BASACT(view_layer) : NULL;
  Base *startbase = NULL, *basact = NULL, *basact_override = NULL;
  const eObMode ob_mode = oldbasact ? oldbasact->ob->mode : OB_MODE_OB;
  const bool is_obedit = (vc.obedit != NULL);

  /* Handle setting the new base active */
  bool use_activate_selected_base = false;

  /* When enabled, don't attempt any further sel */
  bool handled = false;
  bool changed = false;

  if (ob) {
    /* Signal for view3d_opengl_sel to skip edit-mode objs */
    vc.obedit = NULL;
  }

  /* Always start list from `basact` when cycling the sel */
  startbase = FIRSTBASE(view_layer);
  if (oldbasact && oldbasact->next) {
    startbase = oldbasact->next;
  }

  GPUSelResult buffer[MAXPICKELEMS];
  int hits = 0;
  bool do_nearest = false;
  bool has_bones = false;

  if (obcenter == false) {
    /* If objects have pose-mode set, the bones are in the same selection buffer. */
    const eV3DSelObFilter sel_filter = ((ob == false) ?
                                                      ed_view3d_sel_filter_from_mode(scene,
                                                                                     vc.obact) :
                                                      VIEW3D_SEL_FILTER_NOP);
    hits = mixed_bones_ob_selbuffer_extended(
        &vc, buffer, ARRAY_SIZE(buffer), mval, sel_filter, true, enumerate, &do_nearest);
    has_bones = (ob && hits > 0) ? false : selbuffer_has_bones(buffer, hits);
  }

  /* First handle menu sel, early exit when a menu was opened.
   * Otherwise fall through to regular sel. */
  if (enumerate) {
    bool has_menu = false;
    if (obcenter) {
      if (ob_mouse_sel_menu(C, &vc, NULL, 0, mval, params, &basact_override)) {
        has_menu = true;
      }
    }
    else {
      if (hits != 0) {
        if (has_bones && bone_mouse_sel_menu(C, buffer, hits, false, params)) {
          has_menu = true;
        }
        else if (ob_mouse_sel_menu(C, &vc, buffer, hits, mval, params, &basact_override)) {
          has_menu = true;
        }
      }
    }

    /* Let the menu handle any further actions. */
    if (has_menu) {
      return false;
    }
  }
  
  /* This block uses the ctrl key to make the ob sel
   * by its center point rather than its contents */
  /* In edit-mode do not activate. */
  if (obcenter) {
    if (basact_override) {
      basact = basact_override;
    }
    else {
      basact = mouse_sel_ob_center(&vc, startbase, mval);
    }
  }
  else {
    if (basact_override) {
      basact = basact_override;
    }
    else {
      basact = (hits > 0) ? mouse_sel_eval_buffer(
                                &vc, buffer, hits, startbase, has_bones, do_nearest, NULL) :
                            NULL;
    }

    if (((hits > 0) && has_bones) ||
        /* Special case, even when there are no hits, pose logic may desel all bones. */
        ((hits == 0) && (ob_mode & OB_MODE_POSE))) {

      if (basact && (has_bones && (basact->ob->type == OB_CAMERA))) {
        MovieClip *clip = dune_ob_movieclip_get(scene, basact->ob, false);
        if (clip != NULL) {
          if (ed_ob_sel_pick_camera_track(C, scene, basact, clip, buffer, hits, params)) {
            ed_ob_base_sel(basact, BA_SEL);

            /* Don't set `handled` here as the ob activation may be necessary. */
            changed = true;
          }
          else {
            /* Fallback to regular ob sel if no new bundles were selected,
             * allows to sel ob parented to reconstruction ob. */
            basact = mouse_sel_eval_buffer(
                &vc, buffer, hits, startbase, false, do_nearest, NULL);
          }
        }
      }
      else if (ed_armature_pose_sel_pick_with_buffer(view_layer,
                                                     v3d,
                                                     basact ? basact : (Base *)oldbasact,
                                                     buffer,
                                                     hits,
                                                     params,
                                                     do_nearest)) {
        /* When there is no `baseact` this will have operated on `oldbasact`,
         * no ob ops are needed. */
        if (basact != NULL) {
          /* then bone is found */
          /* we make the armature sel:
           * not-sel active ob in pose-mode won't work well for tools */
          ed_ob_base_sel(basact, BA_SEL);

          win_ev_add_notifier(C, NC_OB | ND_BONE_SEL, basact->ob);
          win_ev_add_notifier(C, NC_OB | ND_BONE_ACTIVE, basact->ob);
          graph_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);

          /* In weight-paint, we use sel bone to sel vertex-group,
           * so don't switch to new active ob */
          if (oldbasact) {
            if (oldbasact->ob->mode & OB_MODE_ALL_WEIGHT_PAINT) {
              /* Prevent activating.
               * Sel causes this to be considered the 'active' pose in weight-paint mode.
               * Eventually this limitation may be removed.
               * For now, desel all other pose objs deforming this mesh. */
              ed_armature_pose_sel_in_wpaint_mode(view_layer, basact);

              handled = true;
            }
            else if ((ob_mode & OB_MODE_POSE) && (basact->ob->mode & OB_MODE_POSE)) {
              /* W/in pose-mode, keep the current sel when switching pose bones,
               * this is noticeable when in pose mode with multiple objs at once.
               * Where sel the bone of a different ob would desel this one.
               * After that, exiting pose-mode would only have the active armature selected.
               * This matches multi-ob edit-mode behavior. */
              handled = true;

              if (oldbasact != basact) {
                use_activate_sel_base = true;
              }
            }
            else {
              /* Don't set `handled` here as the ob sel may be necessary
               * when starting out in obmode and moving into pose-mode,
               * when moving from pose to obmode using ob sel also makes sense. */
            }
          }
        }
      }
      /* Prevent bone/track se to pass on to ob sel. */
      if (basact == oldbasact) {
        handled = true;
      }
    }
  }

  if ((scene->toolsettings->ob_flag & SCE_OB_MODE_LOCK) &&
      /* No further sel should take place. */
      (handled == false) &&
      /* No special logic in edit-mode. */
      (is_obedit == false)) {

    if (basact && !dune_ob_is_mode_compat(basact->ob, ob_mode)) {
      if (ob_mode == OB_MODE_OB) {
        struct Main *main = cxt_data_main(C);
        ed_ob_mode_generic_exit(main, vc.graph, scene, basact->ob);
      }
      if (!dune_ob_is_mode_compat(basact->ob, ob_mode)) {
        basact = NULL;
      }
    }

    /* Disallow switching modes,
     * special exception for edit-mode - vertex-parent op. */
    if (basact && oldbasact) {
      if ((oldbasact->ob->mode != basact->ob->mode) &&
          (oldbasact->ob->mode & basact->ob->mode) == 0) {
        basact = NULL;
      }
    }
  }

  /* Ensure code above doesn't change the active base. */
  lib_assert(oldbasact == (vc.obact ? BASACT(view_layer) : NULL));

  bool found = (basact != NULL);
  if ((handled == false) && (vc.obedit == NULL)) {
    /* Obmode (pose mode will have been handled already). */
    if (params->sel_op == SEL_OP_SET) {
      if ((found && params->sel_passthrough) && (basact->flag & BASE_SELECTED)) {
        found = false;
      }
      else if (found || params->desel_all) {
        /* Desel everything. */
        /* `basact` may be NULL. */
        changed |= object_desel_all_except(view_layer, basact);
        graph_id_tag_update(&scene->id, ID_RECALC_SEL);
      }
    }
  }

  /* so, do we have something sel? */
  if ((handled == false) && found) {
    changed = true;

    if (vc.obedit) {
      /* Only do the select (use for setting vertex parents & hooks). */
      ob_desel_all_except(view_layer, basact);
      ed_ob_base_sel(basact, BA_SEL);
    }
    /* Also prevent making it active on mouse sel. */
    else if (BASE_SELECTABLE(v3d, basact)) {
      use_activate_selected_base |= (oldbasact != basact) && (is_obedit == false);

      switch (params->sel_op) {
        case SEL_OP_ADD: {
          ed_ob_base_sel(basact, BA_SELECT);
          break;
        }
        case SEL_OP_SUB: {
          ed_ob_base_sel(basact, BA_DESEL);
          break;
        }
        case SEL_OP_XOR: {
          if (basact->flag & BASE_SEL) {
            /* Keep sel if the base is to be activated. */
            if (use_activate_sel_base == false) {
              ed_ob_base_sel(basact, BA_DESEL);
            }
          }
          else {
            ed_ob_base_sel(basact, BA_SEL);
          }
          break;
        }
        case SEL_OP_SET: {
          ob_desel_all_except(view_layer, basact);
          ed_ob_base_sel(basact, BA_SEL);
          break;
        }
        case SEL_OP_AND: {
          lib_assert_unreachable(); /* Doesn't make sense for picking. */
          break;
        }
      }
    }

    graph_id_tag_update(&scene->id, ID_RECALC_SEL);
    win_ev_add_notifier(C, NC_SCENE | ND_OB_SEL, scene);
  }

  if (use_activate_sel_base && (basact != NULL)) {
    changed = true;
    ed_ob_base_activate(C, basact); /* adds notifier */
    if ((scene->toolsettings->ob_flag & SCE_OB_MODE_LOCK) == 0) {
      win_toolsystem_update_from_cxt_view3d(C);
    }
  }

  if (changed) {
    if (vc.obact && vc.obact->mode & OB_MODE_POSE) {
      ed_outliner_sel_sync_from_pose_bone_tag(C);
    }
    else {
      ed_outliner_sel_sync_from_ob_tag(C);
    }
  }

  return changed;
}

/* Mouse sel in weight paint.
 * Called via generic mouse sel op.
 * return True when pick finds an element or the sel changed. */
static bool ed_paint_vertex_sel_pick(Cxt *C,
                                     const int mval[2],
                                     const struct SelPickParams *params,
                                     Ob *obact)
{
  View3D *v3d = cxt_win_view3d(C);
  const bool use_zbuf = !XRAY_ENABLED(v3d);

  Mesh *me = obact->data; /* alrdy checked for NULL */
  uint index = 0;
  MVert *mv;
  bool changed = false;

  bool found = ed_mesh_pick_vert(C, obact, mval, ED_MESH_PICK_DEFAULT_VERT_DIST, use_zbuf, &index);

  if (params->sel_op == SEL_OP_SET) {
    if ((found && params->sel_passthrough) && (me->mvert[index].flag & SEL)) {
      found = false;
    }
    else if (found || params->desel_all) {
      /* Desel everything. */
      changed |= paintface_desel_all_visible(C, obact, SEL_DESELECT, false);
    }
  }

  if (found) {
    mv = &me->mvert[index];
    switch (params->sel_op) {
      case SEL_OP_ADD: {
        mv->flag |= SEL;
        break;
      }
      case SEL_OP_SUB: {
        mv->flag &= ~SEL;
        break;
      }
      case SEL_OP_XOR: {
        mv->flag ^= SEL;
        break;
      }
      case SEL_OP_SET: {
        paintvert_desel_all_visible(obact, SEL_DESEL, false);
        mv->flag |= SEL;
        break;
      }
      case SEL_OP_AND: {
        lib_assert_unreachable(); /* Doesn't make sense for picking. */
        break;
      }
    }

    /* update msel */
    if (mv->flag & SEL) {
      dune_mesh_msel_active_set(me, index, ME_VSEL);
    }
    else {
      dune_mesh_msel_validate(me);
    }

    paintvert_flush_flags(obact);

    changed = true;
  }

  if (changed) {
    paintvert_tag_sel_update(C, obact);
  }

  return changed || found;
}

static int view3d_sel_ex(Cxt *C, WinOp *op)
{
  Scene *scene = cxt_data_scene(C);
  Ob *obedit = cxt_data_edit_ob(C);
  Ob *obact = cxt_data_active_ob(C);
  const struct SelPick_Params params = {
      .sel_op = ed_sel_op_from_bools(api_bool_get(op->ptr, "extend"),
                                     api_bool_get(op->ptr, "desel"),
                                     api_bool_get(op->ptr, "toggle")),
      .desel_all = api_bool_get(op->ptr, "desel_all"),
      .sel_passthrough = api_bool_get(op->ptr, "sel_passthrough"),

  };
  bool center = api_bool_get(op->ptr, "center");
  bool enumerate = api_bool_get(op->ptr, "enumerate");
  /* Only force ob sel for edit-mode to support vertex parenting,
   * or paint-sel to allow pose bone sel with vert/face sel. */
  bool ob = (api_bool_get(op->ptr, "ob") &&
                 (obedit || dune_paint_sel_elem_test(obact) ||
                  /* so its possible to sel bones in weight-paint mode (LMB sel) */
                  (obact && (obact->mode & OB_MODE_ALL_WEIGHT_PAINT) &&
                   dune_ob_pose_armature_get(obact))));

  /* This could be called "changed_or_found" since this is true when there is an element
   * under the cursor to sel, even if it happens that the sel & active state doesn't
   * actually change. This is important so undo pushes are predictable. */
  bool changed = false;
  int mval[2];

  api_int_get_array(op->ptr, "location", mval);

  view3d_op_needs_opengl(C);
  dune_ob_update_sel_id(cxt_data_main(C));

  if (ob) {
    obedit = NULL;
    obact = NULL;

    /* ack, this is incorrect but to do this correctly we would need an
     * alternative editmode/obmode keymap, this copies the fnality
     * from 2.4x where Ctrl+Sel in editmode does ob sel only. */
    center = false;
  }

  if (obedit && ob == false) {
    if (obedit->type == OB_MESH) {
      changed = edbm_sel_pick(C, mval, &params);
    }
    else if (obedit->type == OB_ARMATURE) {
      if (enumerate) {
        Graph *graph = cxt_data_ensure_eval_graph(C);
        ViewCxt vc;
        ed_view3d_viewcxt_init(C, &vc, graph);

        GPUSelResult buffer[MAXPICKELEMS];
        const int hits = mixed_bones_ob_selbuffer(
            &vc, buffer, ARRAY_SIZE(buffer), mval, VIEW3D_SEL_FILTER_NOP, false, true, false);
        changed = bone_mouse_sel_menu(C, buffer, hits, true, &params);
      }
      if (!changed) {
        changed = ed_armature_edit_sel_pick(C, mval, &params);
      }
    }
    else if (obedit->type == OB_LATTICE) {
      changed = ed_lattice_sel_pick(C, mval, &params);
    }
    else if (ELEM(obedit->type, OB_CURVES_LEGACY, OB_SURF)) {
      changed = ed_curve_editnurb_sel_pick(C, mval, &params);
    }
    else if (obedit->type == OB_MBALL) {
      changed = ed_mball_sel_pick(C, mval, &params);
    }
    else if (obedit->type == OB_FONT) {
      changed = ed_curve_editfont_sel_pick(C, mval, &params);
    }
  }
  else if (obact && obact->mode & OB_MODE_PARTICLE_EDIT) {
    changed = pe_mouse_particles(C, mval, &params);
  }
  else if (obact && dune_paint_sel_face_test(obact)) {
    changed = paintface_mouse_sel(C, mval, &params, obact);
  }
  else if (dune_paint_sel_vert_test(obact)) {
    changed = ed_wpaint_vertex_sel_pick(C, mval, &params, obact);
  }
  else {
    changed = ed_ob_sel_pick(C, mval, &params, center, enumerate, ob);
  }

  /* Pass-through flag may be cleared, see wm_op_flag_only_pass_through_on_press. */
  /* Pass-through allows tweaks
   * FINISHED to signal one operator worked */
  if (changed) {
    win_ev_add_notifier(C, NC_SCENE | ND_OB_SEL, scene);
    return OP_PASS_THROUGH | OP_FINISHED;
  }
  /* Nothing sel, just passthrough. */
  return OP_PASS_THROUGH | OP_CANCELLED;
}

static int view3d_sel_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  api_int_set_array(op->ptr, "location", ev->mval);

  const int retval = view3d_sel_ex(C, op);

  return win_op_flag_only_pass_through_on_press(retval, eve);
}

void VIEW3D_OT_sel(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Sel";
  ot->description = "Sel and activate item(s)";
  ot->idname = "VIEW3D_OT_sel";

  /* api cbs */
  ot->invoke = view3d_sel_invoke;
  ot->ex = view3d_sel_ex;
  ot->poll = ed_op_view3d_active;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* props */
  win_op_props_mouse_sel(ot);

  prop = api_def_bool(
      ot->sapi,
      "center",
      0,
      "Center",
      "Use the ob center when sel, in edit mode used to extend ob sel");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_bool(
      ot->sapi, "enumerate", 0, "Enumerate", "List objs under the mouse (ob mode only)");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  prop = api_def_bool(ot->sapi, "ob", 0, "Ob", "Use ob sel (edit mode only)");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  prop = api_def_int_vector(ot->sapi,
                            "location",
                            2,
                            NULL,
                            INT_MIN,
                            INT_MAX,
                            "Location",
                            "Mouse location",
                            INT_MIN,
                            INT_MAX);
  api_def_prop_flag(prop, PROP_HIDDEN);
}

/* Box Sel */
typedef struct BoxSelUserData {
  ViewCxt *vc;
  const rcti *rect;
  const rctf *rect_fl;
  rctf _rect_fl;
  eSelOp sel_op;
  eBezTriple_Flag sel_flag;

  /* runtime */
  bool is_done;
  bool is_changed;
} BoxSelUserData;

static void view3d_userdata_boxsel_init(BoxSelUserData *r_data,
                                        ViewCxt *vc,
                                        const rcti *rect,
                                        const eSelOp sel_op)
{
  r_data->vc = vc;

  r_data->rect = rect;
  r_data->rect_fl = &r_data->_rect_fl;
  lib_rctf_rcti_copy(&r_data->_rect_fl, rect);

  r_data->sel_op = sel_op;
  /* SEL by default, but can be changed if needed (only few cases use and respect this). */
  r_data->sel_flag = SEL;

  /* runtime */
  r_data->is_done = false;
  r_data->is_changed = false;
}

bool edge_inside_circle(const float cent[2],
                        float radius,
                        const float screen_co_a[2],
                        const float screen_co_b[2])
{
  const float radius_squared = radius * radius;
  return (dist_squared_to_line_segment_v2(cent, screen_co_a, screen_co_b) < radius_squared);
}

static void do_paintvert_box_sel_doSelVert(void *userData,
                                           MVert *mv,
                                           const float screen_co[2],
                                           int UNUSED(index))
{
  BoxSelUserData *data = userData;
  const bool is_sel = mv->flag & SEL;
  const bool is_inside = lib_rctf_isect_pt_v(data->rect_fl, screen_co);
  const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    SET_FLAG_FROM_TEST(mv->flag, sel_op_result, SEL);
    data->is_changed = true;
  }
}
static bool do_paintvert_box_sel(ViewCxt *vc,
                                 WinGenericUserData *win_userdata,
                                 const rcti *rect,
                                 const eSelOp sel_op)
{
  const bool use_zbuf = !XRAY_ENABLED(vc->v3d);

  Mesh *me;

  me = vc->obact->data;
  if ((me == NULL) || (me->totvert == 0)) {
    return OP_CANCELLED;
  }

  bool changed = false;
  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    changed |= paintvert_desel_all_visible(vc->obact, SEL_DESEL, false);
  }

  if (lib_rcti_is_empty(rect)) {
    /* pass */
  }
  else if (use_zbuf) {
    struct EditSelBuf_Cache *esel = wm_userdata->data;
    if (win_userdata->data == NULL) {
      editsel_buf_cache_init_with_generic_userdata(win_userdata, vc, SCE_SEL_VERTEX);
      esel = win_userdata->data;
      esel->sel_bitmap = drw_sel_buffer_bitmap_from_rect(
          vc->graph, vc->rgn, vc->v3d, rect, NULL);
    }
    if (esel->sel_bitmap != NULL) {
      changed |= edbm_backbuf_check_and_sel_verts_obmode(me, esel, sel_op);
    }
  }
  else {
    BoxSelUserData data;

    view3d_userdata_boxsel_init(&data, vc, rect, sel_op);

    ed_view3d_init_mats_rv3d(vc->obact, vc->rv3d);

    meshob_foreachScreenVert(
        vc, do_paintvert_box_sel_doSelVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    changed |= data.is_changed;
  }

  if (changed) {
    if (SEL_OP_CAN_DESEL(sel_op)) {
      dune_mesh_msel_validate(me);
    }
    paintvert_flush_flags(vc->obact);
    paintvert_tag_sel_update(vc->C, vc->obact);
  }
  return changed;
}

static bool do_paintface_box_sel(ViewCxt *vc,
                                 WinGenericUserData *win_userdata,
                                 const rcti *rect,
                                 int sel_op)
{
  Ob *ob = vc->obact;
  Mesh *me;

  me = dune_mesh_from_ob(ob);
  if ((me == NULL) || (me->totpoly == 0)) {
    return false;
  }

  bool changed = false;
  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    changed |= paintface_desel_all_visible(vc->C, vc->obact, SEL_DESEL, false);
  }

  if (lib_rcti_is_empty(rect)) {
    /* pass */
  }
  else {
    struct EditSelBuf_Cache *esel = win_userdata->data;
    if (win_userdata->data == NULL) {
      editsel_buf_cache_init_with_generic_userdata(win_userdata, vc, SCE_SEL_FACE);
      esel = win_userdata->data;
      esel->sel_bitmap = drw_sel_buffer_bitmap_from_rect(
          vc->graph, vc->rgn, vc->v3d, rect, NULL);
    }
    if (esel->sel_bitmap != NULL) {
      changed |= edbm_backbuf_check_and_select_faces_obmode(me, esel, sel_op);
    }
  }

  if (changed) {
    paintface_flush_flags(vc->C, vc->obact, SEL);
  }
  return changed;
}

static void do_nurbs_box_sel_doSel(void *userData,
                                   Nurb *UNUSED(nu),
                                   Point *point,
                                   BezTriple *bezt,
                                   int beztindex,
                                   bool handles_visible,
                                   const float screen_co[2])
{
  BoxSelUserData *data = userData;

  const bool is_inside = lib_rctf_isect_pt_v(data->rect_fl, screen_co);
  if (bp) {
    const bool is_sel = point->f1 & SEL;
    const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_select, is_inside);
    if (sel_op_result != -1) {
      SET_FLAG_FROM_TEST(bp->f1, sel_op_result, data->select_flag);
      data->is_changed = true;
    }
  }
  else {
    if (!handles_visible) {
      /* can only be (beztindex == 1) here since handles are hidden */
      const bool is_sel = bezt->f2 & SEL;
      const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_sel, is_inside);
      if (sel_op_result != -1) {
        SET_FLAG_FROM_TEST(bezt->f2, sel_op_result, data->sel_flag);
        data->is_changed = true;
      }
      bezt->f1 = bezt->f3 = bezt->f2;
    }
    else {
      uint8_t *flag_p = (&bezt->f1) + beztindex;
      const bool is_sel = *flag_p & SEL;
      const int sel_op_result = ed_sel_op_action_deselected(data->sel_op, is_select, is_inside);
      if (sel_op_result != -1) {
        SET_FLAG_FROM_TEST(*flag_p, sel_op_result, data->sel_flag);
        data->is_changed = true;
      }
    }
  }
}

static bool do_nurbs_box_select(ViewCxt *vc, rcti *rect, const eSelOp sel_op)
{
  const bool desel_all = (sel_op == SEL_OP_SET);
  BoxSelUserData data;

  view3d_userdata_boxsel_init(&data, vc, rect, sel_op);

  Curve *curve = (Curve *)vc->obedit->data;
  List *nurbs = dune_curve_editNurbs_get(curve);

  /* For deselect all, items to be selected are tagged with temp flag. Clear that first. */
  if (desel_all) {
    dune_nurbList_flag_set(nurbs, BEZT_FLAG_TMP_TAG, false);
    data.sel_flag = BEZT_FLAG_TMP_TAG;
  }

  ed_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
  nurbs_foreachScreenVert(vc, do_nurbs_box_sel_doSel, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  /* Desel items that were not added to selection (indicated by tmp flag). */
  if (desel_all) {
    data.is_changed |= dune_nurbList_flag_set_from_flag(nurbs, BEZT_FLAG_TEMP_TAG, SELECT);
  }

  dune_curve_nurb_vert_active_validate(vc->obedit->data);

  return data.is_changed;
}

static void do_lattice_box_sel_doSel(void *userData, Point *point, const float screen_co[2])
{
  BoxSelUserData *data = userData;
  const bool is_sel = point->f1 & SEL;
  const bool is_inside = lib_rctf_isect_pt_v(data->rect_fl, screen_co);
  const int sel_op_result = ed_sel_op_action_desel(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    SET_FLAG_FROM_TEST(bp->f1, sel_op_result, SEL);
    data->is_changed = true;
  }
}
static bool do_lattice_box_sel(ViewCxt *vc, rcti *rect, const eSelOp sel_op)
{
  BoxSelUserData data;

  view3d_userdata_boxsel_init(&data, vc, rect, sel_op);

  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    data.is_changed |= ed_lattice_flags_set(vc->obedit, 0);
  }

  ed_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
  lattice_foreachScreenVert(
      vc, do_lattice_box_sel_doSel, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  return data.is_changed;
}

static void do_mesh_box_sel_doSelVert(void *userData,
                                      MVert *eve,
                                      const float screen_co[2],
                                      int UNUSED(index))
{
  BoxSelUserData *data = userData;
  const bool is_sel = dune_mesh_elem_flag_test(eve, MESH_ELEM_SEL);
  const bool is_inside = lib_rctf_isect_pt_v(data->rect_fl, screen_co);
  const int sel_op_result = ed_sel_op_action_desel(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    dune_mesh_vert_sel_set(data->vc->em->bm, eve, sel_op_result);
    data->is_changed = true;
  }
}
struct BoxSelUserDataForMeshEdge {
  BoxSelUserData *data;
  struct EditSelBufCache *esel;
  uint backbuf_offset;
};
/* Pass 0 operates on edges when fully inside */
static void do_mesh_box_sel_doSelEdge_pass0(
    void *userData, MeshEdge *eed, const float screen_co_a[2], const float screen_co_b[2], int index)
{
  struct BoxSelUserDataForMeshEdge *data_for_edge = userData;
  BoxSelUserData *data = data_for_edge->data;
  bool is_visible = true;
  if (data_for_edge->backbuf_offset) {
    uint bitmap_inedx = data_for_edge->backbuf_offset + index - 1;
    is_visible = LIB_BITMAP_TEST_BOOL(data_for_edge->esel->sel_bitmap, bitmap_inedx);
  }

  const bool is_sel = dune_mesh_elem_flag_test(eed, MESH_ELEM_SEL);
  const bool is_inside = (is_visible &&
                          edge_fully_inside_rect(data->rect_fl, screen_co_a, screen_co_b));
  const int sel_op_result = ed_sel_op_action_desel(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    dune_mesh_edge_sel_set(data->vc->em-mesh, eed, sel_op_result);
    data->is_done = true;
    data->is_changed = true;
  }
}
/* Pass 1 operates on edges when partially inside */
static void do_mesh_box_sel_doSelEdge_pass1(
    void *userData, MeshEdge *eed, const float screen_co_a[2], const float screen_co_b[2], int index)
{
  struct BoxSelUserDataForMeshEdge data_for_edge = userData;
  BoxSelUserData *data = data_for_edge->data;
  bool is_visible = true;
  if (data_for_edge->backbuf_offset) {
    uint bitmap_inedx = data_for_edge->backbuf_offset + index - 1;
    is_visible = LIB_BITMAP_TEST_BOOL(data_for_edge->esel->sel_bitmap, bitmap_inedx);
  }

  const bool is_sel = dune_mesh_elem_flag_test(eed, MESH_ELEM_SEL);
  const bool is_inside = (is_visible && edge_inside_rect(data->rect_fl, screen_co_a, screen_co_b));
  const int sel_op_result = ed_sel_op_action_desel(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    DuneMesh_edge_sel_set(data->vc->me->mesh, eed, sel_op_result);
    data->is_changed = true;
  }
}
static void do_mesh_box_sel_doSelFace(void *userData,
                                      MFace *efa,
                                      const float screen_co[2],
                                      int UNUSED(index))
{
  BoxSelUserData *data = userData;
  const bool is_sel = mesh_elem_flag_test(efa, MESH_ELEM_SEL);
  const bool is_inside = lib_rctf_isect_pt_v(data->rect_fl, screen_co);
  const int sel_op_result = ed_sel_op_action_desel(data->sel_op, is_sel, is_inside);
  if (sel_op_result != -1) {
    DuneMesh_face_sel_set(data->vc->em->bm, efa, sel_op_result);
    data->is_changed = true;
  }
}
static bool do_mesh_box_sel(ViewCxt *vc,
                            WinGenericUserData *win_userdata,
                            const rcti *rect,
                            const eSelOp sel_op)
{
  BoxSelUserData data;
  ToolSettings *ts = vc->scene->toolsettings;

  view3d_userdata_boxsel_init(&data, vc, rect, sel_op);

  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    if (vc->em->mesh->totvertsel) {
      ed_flag_disable_all(vc->em, MESH_ELEM_SEL);
      data.is_changed = true;
    }
  }

  /* for non zbuf projections, don't change the GL state */
  ed_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

  gpu_matrix_set(vc->rv3d->viewmat);

  const bool use_zbuf = !XRAY_FLAG_ENABLED(vc->v3d);

  struct EditSelBufCache *esel = win_userdata->data;
  if (use_zbuf) {
    if (win_userdata->data == NULL) {
      editsel_buf_cache_init_with_generic_userdata(win_userdata, vc, ts->selectmode);
      esel = win_userdata->data;
      esel->sel_bitmap = drw_sel_buffer_bitmap_from_rect(
          vc->graph, vc->rgn, vc->v3d, rect, NULL);
    }
  }

  if (ts->selmode & SCE_SEL_VERTEX) {
    if (use_zbuf) {
      data.is_changed |= edbm_backbuf_check_and_sel_verts(
          esel, vc->graph, vc->obedit, vc->em, sel_op);
    }
    else {
      mesh_foreachScreenVert(
          vc, do_mesh_box_sel_doSelVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    }
  }
  if (ts->selmode & SCE_SEL_EDGE) {
    /* Does both use_zbuf and non-use_zbuf versions (need screen cos for both) */
    struct BoxSelUserData_ForMeshEdge cb_data = {
        .data = &data,
        .esel = use_zbuf ? esel : NULL;
        .backbuf_offset = use_zbuf ? drw_sel_buffer_cxt_offset_for_ob_elem(
                                         vc->graph, vc->obedit, SCE_SEL_EDGE) :
                                     0,
    };

    const eV3DProjTest clip_flag = V3D_PROJ_TEST_CLIP_NEAR |
                                   (use_zbuf ? 0 : V3D_PROJ_TEST_CLIP_BB);
    /* Fully inside. */
    mesh_foreachScreenEdge_clip_bb_segment(
        vc, do_mesh_box_select__doSelEdge_pass0, &cb_data, clip_flag);
    if (data.is_done == false) {
      /* Fall back to partially inside.
       * Clip content to account for edges partially behind the view. */
      mesh_foreachScreenEdge_clip_bb_segment(vc,
                                             do_mesh_box_sel_doSelEdge_pass1,
                                             &cb_data,
                                             clip_flag | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);
    }
  }

  if (ts->selmode & SCE_SEL_FACE) {
    if (use_zbuf) {
      data.is_changed |= edbm_backbuf_check_and_sel_faces(
          esel, vc->graph, vc->obedit, vc->em, sel_op);
    }
    else {
      mesh_foreachScreenFace(
          vc, do_mesh_box_sel_doSelFace, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    }
  }

  if (data.is_changed) {
    edbm_selmode_flush(vc->em);
  }
  return data.is_changed;
}

static bool do_meta_box_sel(ViewCxt *vc, const rcti *rect, const eSelOp sel_op)
{
  Ob *ob = vc->obedit;
  MetaBall *mb = (MetaBall *)ob->data;
  MetaElem *ml;
  int a;
  bool changed = false;

  GPUSelResult buffer[MAXPICKELEMS];
  int hits;

  hits = view3d_opengl_sel(
      vc, buffer, MAXPICKELEMS, rect, VIEW3D_SEL_ALL, VIEW3D_SEL_FILTER_NOP);

  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    changed |= dune_mball_desel_all(mb);
  }

  int metaelem_id = 0;
  for (ml = mb->editelems->first; ml; ml = ml->next, metaelem_id += 0x10000) {
    bool is_inside_radius = false;
    bool is_inside_stiff = false;

    for (a = 0; a < hits; a++) {
      const int hitresult = buffer[a].id;

      if (hitresult == -1) {
        continue;
      }

      const uint hit_ob = hitresult & 0xFFFF;
      if (vc->obedit->runtime.sel_id != hit_ob) {
        continue;
      }

      if (metaelem_id != (hitresult & 0xFFFF0000 & ~MBALLSEL_ANY)) {
        continue;
      }

      if (hitresult & MBALLSEL_RADIUS) {
        is_inside_radius = true;
        break;
      }

      if (hitresult & MBALLSEL_STIFF) {
        is_inside_stiff = true;
        break;
      }
    }
    const int flag_prev = ml->flag;
    if (is_inside_radius) {
      ml->flag |= MB_SCALE_RAD;
    }
    if (is_inside_stiff) {
      ml->flag &= ~MB_SCALE_RAD;
    }

    const bool is_sel = (ml->flag & SEL);
    const bool is_inside = is_inside_radius || is_inside_stiff;

    const int sel_op_result = ed_sel_op_action_deselected(sel_op, is_sel, is_inside);
    if (sel_op_result != -1) {
      SET_FLAG_FROM_TEST(ml->flag, sel_op_result, SEL);
    }
    changed |= (flag_prev != ml->flag);
  }

  return changed;
}

static bool do_armature_box_sel(ViewCxt *vc, const rcti *rect, const eSelOp sel_op)
{
  bool changed = false;
  int a;

  GPUSelResult buffer[MAXPICKELEMS];
  int hits;

  hits = view3d_opengl_sel(
      vc, buffer, MAXPICKELEMS, rect, VIEW3D_SEL_ALL, VIEW3D_SEL_FILTER_NOP);

  uint bases_len = 0;
  Base **bases = dune_view_layer_array_from_bases_in_edit_mode_unique_data(
      vc->view_layer, vc->v3d, &bases_len);

  if (SEL_OP_USE_PRE_DESEL(sel_op)) {
    changed |= ed_armature_edit_desel_all_visible_multi_ex(bases, bases_len);
  }

  for (uint base_index = 0; base_index < bases_len; base_index++) {
    Ob *obedit = bases[base_index]->ob;
    obedit->id.tag &= ~LIB_TAG_DOIT;

    Armature *arm = obedit->data;
    ed_armature_ebone_list_tmp_clear(arm->edbo);
  }

  /* first we only check points inside the border */
  for (a = 0; a < hits; a++) {
    const int sel_id = buffer[a].id;
    if (sel_id != -1) {
      if ((sel_id & 0xFFFF0000) == 0) {
        continue;
      }

      EditBone *ebone;
      Base *base_edit = ed_armature_base_and_ebone_from_sel_buffer(
          bases, bases_len, sel_id, &ebone);
      ebone->tmp.i |= sel_id & BONESEL_ANY;
      base_edit->ob->id.tag |= LIB_TAG_DOIT;
    }
  }

  for (uint base_index = 0; base_index < bases_len; base_index++) {
    Ob *obedit = bases[base_index]->ob;
    if (obedit->id.tag & LIB_TAG_DOIT) {
      obedit->id.tag &= ~LIB_TAG_DOIT;
      changed |= ed_armature_edit_sel_op_from_tagged(obedit->data, sel_op);
    }
  }

  mem_free(bases);

  return changed;
}

/* Compare result of 'gpu_sel': 'GPUSelResult',
 * needed for when we need to align with ob draw-order */
static int opengl_bone_sel_buf_cmp(const void *sel_a_p, const void *sel_b_p)
{
  uint sel_a = ((GPUSelResult *)sel_a_p)->id;
  uint sel_b = ((GPUSelResult *)sel_b_p)->id;

#ifdef __BIG_ENDIAN__
  lib_endian_switch_uint32(&sel_a);
  lib_endian_switch_uint32(&sel_b);
#endif

  if (sel_a < sel_b) {
    return -1;
  }
  if (sel_a > sel_b) {
    return 1;
  }
  return 0;
}

static bool do_ob_box_sel(Cxt *C, ViewCxt *vc, rcti *rect, const eSelectOp sel_op)
{
  View3D *v3d = vc->v3d;
  int totob = MAXPICKELEMS; /* XXX solve later */
  
  /* Sel buffer has bones potentially too, so we add MAXPICKELEMS. */
  GPUSelResult *buffer = mem_malloc((totob + MAXPICKELEMS) * sizeof(GPUSelResult),
                                        "sel buffer");
  const eV3DSelObFilter select_filter = ed_view3d_sel_filter_from_mode(vc->scene,
                                                                       vc->obact);
  const int hits = view3d_opengl_select(
      vc, buf, (totob + MAXPICKELEMS), rect, VIEW3D_SEL_ALL, sel_filter);

  LISTBASE_FOREACH (Base *, base, &vc->view_layer->object_bases) {
    base->object->id.tag &= ~LIB_TAG_DOIT;
  }

  Base **bases = NULL;
  LIB_array_declare(bases);

  bool changed = false;
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    changed |= object_deselect_all_visible(vc->view_layer, vc->v3d);
  }

  if ((hits == -1) && !SEL_OP_USE_OUTSIDE(sel_op)) {
    goto finally;
  }

  LISTBASE_FOREACH (Base *, base, &vc->view_layer->object_bases) {
    if (BASE_SELECTABLE(v3d, base)) {
      if ((base->object->runtime.select_id & 0x0000FFFF) != 0) {
        LIB_array_append(bases, base);
      }
    }
  }

  /* The draw order doesn't always match the order we populate the engine, see: T51695. */
  qsort(buffer, hits, sizeof(GPUSelectResult), opengl_bone_select_buffer_cmp);

  for (const GPUSelectResult *buf_iter = buffer, *buf_end = buf_iter + hits; buf_iter < buf_end;
       buf_iter++) {
    dunePoseChannel *pchan_dummy;
    Base *base = ED_armature_base_and_pchan_from_select_buffer(
        bases, LIB_array_len(bases), buf_iter->id, &pchan_dummy);
    if (base != NULL) {
      base->object->id.tag |= LIB_TAG_DOIT;
    }
  }

  for (Base *base = vc->view_layer->object_bases.first; base && hits; base = base->next) {
    if (BASE_SELECTABLE(v3d, base)) {
      const bool is_select = base->flag & BASE_SELECTED;
      const bool is_inside = base->object->id.tag & LIB_TAG_DOIT;
      const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
      if (sel_op_result != -1) {
        ED_object_base_select(base, sel_op_result ? BA_SELECT : BA_DESELECT);
        changed = true;
      }
    }
  }

finally:
  if (bases != NULL) {
    MEM_freeN(bases);
  }

  MEM_freeN(buffer);

  if (changed) {
    DEG_id_tag_update(&vc->scene->id, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, vc->scene);
  }
  return changed;
}

static bool do_pose_box_select(duneContext *C, ViewContext *vc, rcti *rect, const eSelectOp sel_op)
{
  uint bases_len;
  Base **bases = do_pose_tag_select_op_prepare(vc, &bases_len);

  int totobj = MAXPICKELEMS; /* XXX solve later */

  /* Selection buffer has bones potentially too, so add MAXPICKELEMS. */
  GPUSelectResult *buffer = MEM_mallocN((totobj + MAXPICKELEMS) * sizeof(GPUSelectResult),
                                        "selection buffer");
  const eV3DSelectObjectFilter select_filter = ED_view3d_select_filter_from_mode(vc->scene,
                                                                                 vc->obact);
  const int hits = view3d_opengl_select(
      vc, buffer, (totobj + MAXPICKELEMS), rect, VIEW3D_SELECT_ALL, select_filter);
  /*
   * LOGIC NOTES:
   * The buffer and ListBase have the same relative order, which makes the selection
   * very simple. Loop through both data sets at the same time, if the color
   * is the same as the object, we have a hit and can move to the next color
   * and object pair, if not, just move to the next object,
   * keeping the same color until we have a hit.
   */

  if (hits > 0) {
    /* no need to loop if there's no hit */

    /* The draw order doesn't always match the order we populate the engine, see: T51695. */
    qsort(buffer, hits, sizeof(GPUSelectResult), opengl_bone_select_buffer_cmp);

    for (const GPUSelectResult *buf_iter = buffer, *buf_end = buf_iter + hits; buf_iter < buf_end;
         buf_iter++) {
      Bone *bone;
      Base *base = ED_armature_base_and_bone_from_select_buffer(
          bases, bases_len, buf_iter->id, &bone);

      if (base == NULL) {
        continue;
      }

      /* Loop over contiguous bone hits for 'base'. */
      for (; buf_iter != buf_end; buf_iter++) {
        /* should never fail */
        if (bone != NULL) {
          base->object->id.tag |= LIB_TAG_DOIT;
          bone->flag |= BONE_DONE;
        }

        /* Select the next bone if we're not switching bases. */
        if (buf_iter + 1 != buf_end) {
          const GPUSelectResult *col_next = buf_iter + 1;
          if ((base->object->runtime.select_id & 0x0000FFFF) != (col_next->id & 0x0000FFFF)) {
            break;
          }
          if (base->object->pose != NULL) {
            const uint hit_bone = (col_next->id & ~BONESEL_ANY) >> 16;
            dunePoseChannel *pchan = LIB_findlink(&base->object->pose->chanbase, hit_bone);
            bone = pchan ? pchan->bone : NULL;
          }
          else {
            bone = NULL;
          }
        }
      }
    }
  }

  const bool changed_multi = do_pose_tag_select_op_exec(bases, bases_len, sel_op);
  if (changed_multi) {
    DEG_id_tag_update(&vc->scene->id, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, vc->scene);
  }

  if (bases != NULL) {
    MEM_freeN(bases);
  }
  MEM_freeN(buffer);

  return changed_multi;
}

static int view3d_box_select_exec(duneContext *C, wmOperator *op)
{
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  ViewContext vc;
  rcti rect;
  bool changed_multi = false;

  wmGenericUserData wm_userdata_buf = {0};
  wmGenericUserData *wm_userdata = &wm_userdata_buf;

  view3d_operator_needs_opengl(C);
  DUNE_object_update_select_id(CTX_data_main(C));

  /* setup view context for argument to callbacks */
  ED_view3d_viewcontext_init(C, &vc, depsgraph);

  eSelectOp sel_op = API_enum_get(op->ptr, "mode");
  WM_operator_properties_border_to_rcti(op, &rect);

  if (vc.obedit) {
    FOREACH_OBJECT_IN_MODE_BEGIN (
        vc.view_layer, vc.v3d, vc.obedit->type, vc.obedit->mode, ob_iter) {
      ED_view3d_viewcontext_init_object(&vc, ob_iter);
      bool changed = false;

      switch (vc.obedit->type) {
        case OB_MESH:
          vc.em = DUNE_editmesh_from_object(vc.obedit);
          changed = do_mesh_box_select(&vc, wm_userdata, &rect, sel_op);
          if (changed) {
            DEG_id_tag_update(vc.obedit->data, ID_RECALC_SELECT);
            WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc.obedit->data);
          }
          break;
        case OB_CURVES_LEGACY:
        case OB_SURF:
          changed = do_nurbs_box_select(&vc, &rect, sel_op);
          if (changed) {
            DEG_id_tag_update(vc.obedit->data, ID_RECALC_SELECT);
            WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc.obedit->data);
          }
          break;
        case OB_MBALL:
          changed = do_meta_box_select(&vc, &rect, sel_op);
          if (changed) {
            DEG_id_tag_update(vc.obedit->data, ID_RECALC_SELECT);
            WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc.obedit->data);
          }
          break;
        case OB_ARMATURE:
          changed = do_armature_box_select(&vc, &rect, sel_op);
          if (changed) {
            DEG_id_tag_update(&vc.obedit->id, ID_RECALC_SELECT);
            WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, vc.obedit);
            ED_outliner_select_sync_from_edit_bone_tag(C);
          }
          break;
        case OB_LATTICE:
          changed = do_lattice_box_select(&vc, &rect, sel_op);
          if (changed) {
            DEG_id_tag_update(vc.obedit->data, ID_RECALC_SELECT);
            WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc.obedit->data);
          }
          break;
        default:
          LIB_assert_msg(0, "box select on incorrect object type");
          break;
      }
      changed_multi |= changed;
    }
    FOREACH_OBJECT_IN_MODE_END;
  }
  else { /* No edit-mode, unified for bones and objects. */
    if (vc.obact && DUNE_paint_select_face_test(vc.obact)) {
      changed_multi = do_paintface_box_select(&vc, wm_userdata, &rect, sel_op);
    }
    else if (vc.obact && DUNE_paint_select_vert_test(vc.obact)) {
      changed_multi = do_paintvert_box_select(&vc, wm_userdata, &rect, sel_op);
    }
    else if (vc.obact && vc.obact->mode & OB_MODE_PARTICLE_EDIT) {
      changed_multi = PE_box_select(C, &rect, sel_op);
    }
    else if (vc.obact && vc.obact->mode & OB_MODE_POSE) {
      changed_multi = do_pose_box_select(C, &vc, &rect, sel_op);
      if (changed_multi) {
        ED_outliner_select_sync_from_pose_bone_tag(C);
      }
    }
    else { /* object mode with none active */
      changed_multi = do_object_box_select(C, &vc, &rect, sel_op);
      if (changed_multi) {
        ED_outliner_select_sync_from_object_tag(C);
      }
    }
  }

  WM_generic_user_data_free(wm_userdata);

  if (changed_multi) {
    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void VIEW3D_OT_select_box(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Box Select";
  ot->description = "Select items using box selection";
  ot->idname = "VIEW3D_OT_select_box";

  /* api callbacks */
  ot->invoke = WM_gesture_box_invoke;
  ot->exec = view3d_box_select_exec;
  ot->modal = WM_gesture_box_modal;
  ot->poll = view3d_selectable_data;
  ot->cancel = WM_gesture_box_cancel;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* api */
  WM_operator_properties_gesture_box(ot);
  WM_operator_properties_select_operation(ot);
}

/* -------------------------------------------------------------------- */
/** Circle Select **/

typedef struct CircleSelectUserData {
  ViewContext *vc;
  bool select;
  int mval[2];
  float mval_fl[2];
  float radius;
  float radius_squared;
  eBezTriple_Flag select_flag;

  /* runtime */
  bool is_changed;
} CircleSelectUserData;

static void view3d_userdata_circleselect_init(CircleSelectUserData *r_data,
                                              ViewContext *vc,
                                              const bool select,
                                              const int mval[2],
                                              const float rad)
{
  r_data->vc = vc;
  r_data->select = select;
  copy_v2_v2_int(r_data->mval, mval);
  r_data->mval_fl[0] = mval[0];
  r_data->mval_fl[1] = mval[1];

  r_data->radius = rad;
  r_data->radius_squared = rad * rad;

  /* SELECT by default, but can be changed if needed (only few cases use and respect this). */
  r_data->select_flag = SELECT;

  /* runtime */
  r_data->is_changed = false;
}

static void mesh_circle_doSelectVert(void *userData,
                                     BMVert *eve,
                                     const float screen_co[2],
                                     int UNUSED(index))
{
  CircleSelectUserData *data = userData;

  if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
    DuneMesh_vert_select_set(data->vc->em->bm, eve, data->select);
    data->is_changed = true;
  }
}
static void mesh_circle_doSelectEdge(void *userData,
                                     BMEdge *eed,
                                     const float screen_co_a[2],
                                     const float screen_co_b[2],
                                     int UNUSED(index))
{
  CircleSelectUserData *data = userData;

  if (edge_inside_circle(data->mval_fl, data->radius, screen_co_a, screen_co_b)) {
    BM_edge_select_set(data->vc->em->bm, eed, data->select);
    data->is_changed = true;
  }
}
static void mesh_circle_doSelectFace(void *userData,
                                     BMFace *efa,
                                     const float screen_co[2],
                                     int UNUSED(index))
{
  CircleSelectUserData *data = userData;

  if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
    DUNEMESH_face_select_set(data->vc->em->bm, efa, data->select);
    data->is_changed = true;
  }
}

static bool mesh_circle_select(ViewContext *vc,
                               wmGenericUserData *wm_userdata,
                               eSelectOp sel_op,
                               const int mval[2],
                               float rad)
{
  ToolSettings *ts = vc->scene->toolsettings;
  CircleSelectUserData data;
  vc->em = DUNE_editmesh_from_object(vc->obedit);

  bool changed = false;
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    if (vc->em->bm->totvertsel) {
      EDBM_flag_disable_all(vc->em, BM_ELEM_SELECT);
      vc->em->bm->totvertsel = 0;
      vc->em->bm->totedgesel = 0;
      vc->em->bm->totfacesel = 0;
      changed = true;
    }
  }
  const bool select = (sel_op != SEL_OP_SUB);

  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */

  view3d_userdata_circleselect_init(&data, vc, select, mval, rad);

  const bool use_zbuf = !XRAY_FLAG_ENABLED(vc->v3d);

  if (use_zbuf) {
    if (wm_userdata->data == NULL) {
      editselect_buf_cache_init_with_generic_userdata(wm_userdata, vc, ts->selectmode);
    }
  }
  struct EditSelectBuf_Cache *esel = wm_userdata->data;

  if (use_zbuf) {
    if (esel->select_bitmap == NULL) {
      esel->select_bitmap = DRW_select_buffer_bitmap_from_circle(
          vc->depsgraph, vc->region, vc->v3d, mval, (int)(rad + 1.0f), NULL);
    }
  }

  if (ts->selectmode & SCE_SELECT_VERTEX) {
    if (use_zbuf) {
      if (esel->select_bitmap != NULL) {
        changed |= edbm_backbuf_check_and_select_verts(
            esel, vc->depsgraph, vc->obedit, vc->em, select ? SEL_OP_ADD : SEL_OP_SUB);
      }
    }
    else {
      mesh_foreachScreenVert(vc, mesh_circle_doSelectVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    }
  }

  if (ts->selectmode & SCE_SELECT_EDGE) {
    if (use_zbuf) {
      if (esel->select_bitmap != NULL) {
        changed |= edbm_backbuf_check_and_select_edges(
            esel, vc->depsgraph, vc->obedit, vc->em, select ? SEL_OP_ADD : SEL_OP_SUB);
      }
    }
    else {
      mesh_foreachScreenEdge_clip_bb_segment(
          vc,
          mesh_circle_doSelectEdge,
          &data,
          (V3D_PROJ_TEST_CLIP_NEAR | V3D_PROJ_TEST_CLIP_BB | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT));
    }
  }

  if (ts->selectmode & SCE_SELECT_FACE) {
    if (use_zbuf) {
      if (esel->select_bitmap != NULL) {
        changed |= edbm_backbuf_check_and_select_faces(
            esel, vc->depsgraph, vc->obedit, vc->em, select ? SEL_OP_ADD : SEL_OP_SUB);
      }
    }
    else {
      mesh_foreachScreenFace(vc, mesh_circle_doSelectFace, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    }
  }

  changed |= data.is_changed;

  if (changed) {
    DUNEMESH_mesh_select_mode_flush_ex(
        vc->em->bm, vc->em->selectmode, DUNEMESH_SELECT_LEN_FLUSH_RECALC_NOTHING);
  }
  return changed;
}

static bool paint_facesel_circle_select(ViewContext *vc,
                                        wmGenericUserData *wm_userdata,
                                        const eSelectOp sel_op,
                                        const int mval[2],
                                        float rad)
{
  LIB_assert(ELEM(sel_op, SEL_OP_SET, SEL_OP_ADD, SEL_OP_SUB));
  Object *ob = vc->obact;
  Mesh *me = ob->data;

  bool changed = false;
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    /* flush selection at the end */
    changed |= paintface_deselect_all_visible(vc->C, ob, SEL_DESELECT, false);
  }

  if (wm_userdata->data == NULL) {
    editselect_buf_cache_init_with_generic_userdata(wm_userdata, vc, SCE_SELECT_FACE);
  }

  {
    struct EditSelectBuf_Cache *esel = wm_userdata->data;
    esel->select_bitmap = DRW_select_buffer_bitmap_from_circle(
        vc->depsgraph, vc->region, vc->v3d, mval, (int)(rad + 1.0f), NULL);
    if (esel->select_bitmap != NULL) {
      changed |= edbm_backbuf_check_and_select_faces_obmode(me, esel, sel_op);
      MEM_freeN(esel->select_bitmap);
      esel->select_bitmap = NULL;
    }
  }

  if (changed) {
    paintface_flush_flags(vc->C, ob, SELECT);
  }
  return changed;
}

static void paint_vertsel_circle_select_doSelectVert(void *userData,
                                                     MVert *mv,
                                                     const float screen_co[2],
                                                     int UNUSED(index))
{
  CircleSelectUserData *data = userData;

  if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
    SET_FLAG_FROM_TEST(mv->flag, data->select, SELECT);
    data->is_changed = true;
  }
}
static bool paint_vertsel_circle_select(ViewContext *vc,
                                        wmGenericUserData *wm_userdata,
                                        const eSelectOp sel_op,
                                        const int mval[2],
                                        float rad)
{
  LIB_assert(ELEM(sel_op, SEL_OP_SET, SEL_OP_ADD, SEL_OP_SUB));
  const bool use_zbuf = !XRAY_ENABLED(vc->v3d);
  Object *ob = vc->obact;
  Mesh *me = ob->data;
  /* CircleSelectUserData data = {NULL}; */ /* UNUSED */

  bool changed = false;
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    /* Flush selection at the end. */
    changed |= paintvert_deselect_all_visible(ob, SEL_DESELECT, false);
  }

  const bool select = (sel_op != SEL_OP_SUB);

  if (use_zbuf) {
    if (wm_userdata->data == NULL) {
      editselect_buf_cache_init_with_generic_userdata(wm_userdata, vc, SCE_SELECT_VERTEX);
    }
  }

  if (use_zbuf) {
    struct EditSelectBuf_Cache *esel = wm_userdata->data;
    esel->select_bitmap = DRW_select_buffer_bitmap_from_circle(
        vc->depsgraph, vc->region, vc->v3d, mval, (int)(rad + 1.0f), NULL);
    if (esel->select_bitmap != NULL) {
      changed |= edbm_backbuf_check_and_select_verts_obmode(me, esel, sel_op);
      MEM_freeN(esel->select_bitmap);
      esel->select_bitmap = NULL;
    }
  }
  else {
    CircleSelectUserData data;

    ED_view3d_init_mats_rv3d(vc->obact, vc->rv3d); /* for foreach's screen/vert projection */

    view3d_userdata_circleselect_init(&data, vc, select, mval, rad);
    meshobject_foreachScreenVert(
        vc, paint_vertsel_circle_select_doSelectVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    changed |= data.is_changed;
  }

  if (changed) {
    if (sel_op == SEL_OP_SUB) {
      DUNE_mesh_mselect_validate(me);
    }
    paintvert_flush_flags(ob);
    paintvert_tag_select_update(vc->C, ob);
  }
  return changed;
}

static void nurbscurve_circle_doSelect(void *userData,
                                       Nurb *UNUSED(nu),
                                       DunePoint *dp,
                                       BezTriple *bezt,
                                       int beztindex,
                                       bool UNUSED(handles_visible),
                                       const float screen_co[2])
{
  CircleSelectUserData *data = userData;

  if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
    if (bp) {
      SET_FLAG_FROM_TEST(bp->f1, data->select, data->select_flag);
    }
    else {
      if (beztindex == 0) {
        SET_FLAG_FROM_TEST(bezt->f1, data->select, data->select_flag);
      }
      else if (beztindex == 1) {
        SET_FLAG_FROM_TEST(bezt->f2, data->select, data->select_flag);
      }
      else {
        SET_FLAG_FROM_TEST(bezt->f3, data->select, data->select_flag);
      }
    }
    data->is_changed = true;
  }
}
static bool nurbscurve_circle_select(ViewContext *vc,
                                     const eSelectOp sel_op,
                                     const int mval[2],
                                     float rad)
{
  const bool select = (sel_op != SEL_OP_SUB);
  const bool deselect_all = (sel_op == SEL_OP_SET);
  CircleSelectUserData data;

  view3d_userdata_circleselect_init(&data, vc, select, mval, rad);

  Curve *curve = (Curve *)vc->obedit->data;
  ListBase *nurbs = DUNE_curve_editNurbs_get(curve);

  /* For deselect all, items to be selected are tagged with temp flag. Clear that first. */
  if (deselect_all) {
    DUNE_nurbList_flag_set(nurbs, BEZT_FLAG_TEMP_TAG, false);
    data.select_flag = BEZT_FLAG_TEMP_TAG;
  }

  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
  nurbs_foreachScreenVert(vc, nurbscurve_circle_doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  /* Deselect items that were not added to selection (indicated by temp flag). */
  if (deselect_all) {
    data.is_changed |= DUNE_nurbList_flag_set_from_flag(nurbs, BEZT_FLAG_TEMP_TAG, SELECT);
  }

  DUNE_curve_nurb_vert_active_validate(vc->obedit->data);

  return data.is_changed;
}

static void latticecurve_circle_doSelect(void *userData, BPoint *bp, const float screen_co[2])
{
  CircleSelectUserData *data = userData;

  if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
    bp->f1 = data->select ? (bp->f1 | SELECT) : (bp->f1 & ~SELECT);
    data->is_changed = true;
  }
}
static bool lattice_circle_select(ViewContext *vc,
                                  const eSelectOp sel_op,
                                  const int mval[2],
                                  float rad)
{
  CircleSelectUserData data;
  const bool select = (sel_op != SEL_OP_SUB);

  view3d_userdata_circleselect_init(&data, vc, select, mval, rad);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data.is_changed |= ED_lattice_flags_set(vc->obedit, 0);
  }
  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */

  lattice_foreachScreenVert(vc, latticecurve_circle_doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  return data.is_changed;
}

/**
 * logic is shared with the edit-bone case, see #armature_circle_doSelectJoint.
 */
static bool pchan_circle_doSelectJoint(void *userData,
                                       bPoseChannel *pchan,
                                       const float screen_co[2])
{
  CircleSelectUserData *data = userData;

  if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
    if (data->select) {
      pchan->bone->flag |= BONE_SELECTED;
    }
    else {
      pchan->bone->flag &= ~BONE_SELECTED;
    }
    return 1;
  }
  return 0;
}
static void do_circle_select_pose__doSelectBone(void *userData,
                                                struct dunePoseChannel *pchan,
                                                const float screen_co_a[2],
                                                const float screen_co_b[2])
{
  CircleSelectUserData *data = userData;
  duneArmature *arm = data->vc->obact->data;
  if (!PBONE_SELECTABLE(arm, pchan->bone)) {
    return;
  }

  bool is_point_done = false;
  int points_proj_tot = 0;

  /* project head location to screenspace */
  if (screen_co_a[0] != IS_CLIPPED) {
    points_proj_tot++;
    if (pchan_circle_doSelectJoint(data, pchan, screen_co_a)) {
      is_point_done = true;
    }
  }

  /* project tail location to screenspace */
  if (screen_co_b[0] != IS_CLIPPED) {
    points_proj_tot++;
    if (pchan_circle_doSelectJoint(data, pchan, screen_co_b)) {
      is_point_done = true;
    }
  }

  /* check if the head and/or tail is in the circle
   * - the call to check also does the selection already
   */

  /* only if the endpoints didn't get selected, deal with the middle of the bone too
   * It works nicer to only do this if the head or tail are not in the circle,
   * otherwise there is no way to circle select joints alone */
  if ((is_point_done == false) && (points_proj_tot == 2) &&
      edge_inside_circle(data->mval_fl, data->radius, screen_co_a, screen_co_b)) {
    if (data->select) {
      pchan->bone->flag |= BONE_SELECTED;
    }
    else {
      pchan->bone->flag &= ~BONE_SELECTED;
    }
    data->is_changed = true;
  }

  data->is_changed |= is_point_done;
}
static bool pose_circle_select(ViewContext *vc,
                               const eSelectOp sel_op,
                               const int mval[2],
                               float rad)
{
  LIB_assert(ELEM(sel_op, SEL_OP_SET, SEL_OP_ADD, SEL_OP_SUB));
  CircleSelectUserData data;
  const bool select = (sel_op != SEL_OP_SUB);

  view3d_userdata_circleselect_init(&data, vc, select, mval, rad);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data.is_changed |= ED_pose_deselect_all(vc->obact, SEL_DESELECT, false);
  }

  ED_view3d_init_mats_rv3d(vc->obact, vc->rv3d); /* for foreach's screen/vert projection */

  /* Treat bones as clipped segments (no joints). */
  pose_foreachScreenBone(vc,
                         do_circle_select_pose__doSelectBone,
                         &data,
                         V3D_PROJ_TEST_CLIP_DEFAULT | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);

  if (data.is_changed) {
    ED_pose_bone_select_tag_update(vc->obact);
  }
  return data.is_changed;
}

/**
 * logic is shared with the pose-bone case, see pchan_circle_doSelectJoint.
 */
static bool armature_circle_doSelectJoint(void *userData,
                                          EditBone *ebone,
                                          const float screen_co[2],
                                          bool head)
{
  CircleSelectUserData *data = userData;

  if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
    if (head) {
      if (data->select) {
        ebone->flag |= BONE_ROOTSEL;
      }
      else {
        ebone->flag &= ~BONE_ROOTSEL;
      }
    }
    else {
      if (data->select) {
        ebone->flag |= BONE_TIPSEL;
      }
      else {
        ebone->flag &= ~BONE_TIPSEL;
      }
    }
    return 1;
  }
  return 0;
}
static void do_circle_select_armature__doSelectBone(void *userData,
                                                    struct EditBone *ebone,
                                                    const float screen_co_a[2],
                                                    const float screen_co_b[2])
{
  CircleSelectUserData *data = userData;
  const duneArmature *arm = data->vc->obedit->data;
  if (!(data->select ? EBONE_SELECTABLE(arm, ebone) : EBONE_VISIBLE(arm, ebone))) {
    return;
  }

  /* When true, ignore in the next pass. */
  ebone->temp.i = false;

  bool is_point_done = false;
  bool is_edge_done = false;
  int points_proj_tot = 0;

  /* project head location to screenspace */
  if (screen_co_a[0] != IS_CLIPPED) {
    points_proj_tot++;
    if (armature_circle_doSelectJoint(data, ebone, screen_co_a, true)) {
      is_point_done = true;
    }
  }

  /* project tail location to screenspace */
  if (screen_co_b[0] != IS_CLIPPED) {
    points_proj_tot++;
    if (armature_circle_doSelectJoint(data, ebone, screen_co_b, false)) {
      is_point_done = true;
    }
  }

  /* check if the head and/or tail is in the circle
   * - the call to check also does the selection already
   */

  /* only if the endpoints didn't get selected, deal with the middle of the bone too
   * It works nicer to only do this if the head or tail are not in the circle,
   * otherwise there is no way to circle select joints alone */
  if ((is_point_done == false) && (points_proj_tot == 2) &&
      edge_inside_circle(data->mval_fl, data->radius, screen_co_a, screen_co_b)) {
    SET_FLAG_FROM_TEST(ebone->flag, data->select, BONE_SELECTED | BONE_TIPSEL | BONE_ROOTSEL);
    is_edge_done = true;
    data->is_changed = true;
  }

  if (is_point_done || is_edge_done) {
    ebone->temp.i = true;
  }

  data->is_changed |= is_point_done;
}
static void do_circle_select_armature__doSelectBone_clip_content(void *userData,
                                                                 struct EditBone *ebone,
                                                                 const float screen_co_a[2],
                                                                 const float screen_co_b[2])
{
  CircleSelectUserData *data = userData;
  duneArmature *arm = data->vc->obedit->data;

  if (!(data->select ? EBONE_SELECTABLE(arm, ebone) : EBONE_VISIBLE(arm, ebone))) {
    return;
  }

  /* Set in the first pass, needed so circle select prioritizes joints. */
  if (ebone->temp.i == true) {
    return;
  }

  if (edge_inside_circle(data->mval_fl, data->radius, screen_co_a, screen_co_b)) {
    SET_FLAG_FROM_TEST(ebone->flag, data->select, BONE_SELECTED | BONE_TIPSEL | BONE_ROOTSEL);
    data->is_changed = true;
  }
}
static bool armature_circle_select(ViewContext *vc,
                                   const eSelectOp sel_op,
                                   const int mval[2],
                                   float rad)
{
  CircleSelectUserData data;
  duneArmature *arm = vc->obedit->data;

  const bool select = (sel_op != SEL_OP_SUB);

  view3d_userdata_circleselect_init(&data, vc, select, mval, rad);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data.is_changed |= ED_armature_edit_deselect_all_visible(vc->obedit);
  }

  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

  /* Operate on fully visible (non-clipped) points. */
  armature_foreachScreenBone(
      vc, do_circle_select_armature__doSelectBone, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  /* Operate on bones as segments clipped to the viewport bounds
   * (needed to handle bones with both points outside the view).
   * A separate pass is needed since clipped coordinates can't be used for selecting joints. */
  armature_foreachScreenBone(vc,
                             do_circle_select_armature__doSelectBone_clip_content,
                             &data,
                             V3D_PROJ_TEST_CLIP_DEFAULT | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);

  if (data.is_changed) {
    ED_armature_edit_sync_selection(arm->edbo);
    ED_armature_edit_validate_active(arm);
    WM_main_add_notifier(NC_OBJECT | ND_BONE_SELECT, vc->obedit);
  }
  return data.is_changed;
}

static void do_circle_select_mball__doSelectElem(void *userData,
                                                 struct MetaElem *ml,
                                                 const float screen_co[2])
{
  CircleSelectUserData *data = userData;

  if (len_squared_v2v2(data->mval_fl, screen_co) <= data->radius_squared) {
    if (data->select) {
      ml->flag |= SELECT;
    }
    else {
      ml->flag &= ~SELECT;
    }
    data->is_changed = true;
  }
}
static bool mball_circle_select(ViewContext *vc,
                                const eSelectOp sel_op,
                                const int mval[2],
                                float rad)
{
  CircleSelectUserData data;

  const bool select = (sel_op != SEL_OP_SUB);

  view3d_userdata_circleselect_init(&data, vc, select, mval, rad);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data.is_changed |= BKE_mball_deselect_all(vc->obedit->data);
  }

  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

  mball_foreachScreenElem(
      vc, do_circle_select_mball__doSelectElem, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
  return data.is_changed;
}

/** Callbacks for circle selection in Editmode **/
static bool obedit_circle_select(duneContext *C,
                                 ViewContext *vc,
                                 wmGenericUserData *wm_userdata,
                                 const eSelectOp sel_op,
                                 const int mval[2],
                                 float rad)
{
  bool changed = false;
  LIB_assert(ELEM(sel_op, SEL_OP_SET, SEL_OP_ADD, SEL_OP_SUB));
  switch (vc->obedit->type) {
    case OB_MESH:
      changed = mesh_circle_select(vc, wm_userdata, sel_op, mval, rad);
      break;
    case OB_CURVES_LEGACY:
    case OB_SURF:
      changed = nurbscurve_circle_select(vc, sel_op, mval, rad);
      break;
    case OB_LATTICE:
      changed = lattice_circle_select(vc, sel_op, mval, rad);
      break;
    case OB_ARMATURE:
      changed = armature_circle_select(vc, sel_op, mval, rad);
      if (changed) {
        ED_outliner_select_sync_from_edit_bone_tag(C);
      }
      break;
    case OB_MBALL:
      changed = mball_circle_select(vc, sel_op, mval, rad);
      break;
    default:
      BLI_assert(0);
      break;
  }

  if (changed) {
    DEG_id_tag_update(vc->obact->data, ID_RECALC_SELECT);
    WM_main_add_notifier(NC_GEOM | ND_SELECT, vc->obact->data);
  }
  return changed;
}

static bool object_circle_select(ViewContext *vc,
                                 const eSelectOp sel_op,
                                 const int mval[2],
                                 float rad)
{
  LIB_assert(ELEM(sel_op, SEL_OP_SET, SEL_OP_ADD, SEL_OP_SUB));
  ViewLayer *view_layer = vc->view_layer;
  View3D *v3d = vc->v3d;

  const float radius_squared = rad * rad;
  const float mval_fl[2] = {mval[0], mval[1]};

  bool changed = false;
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    changed |= object_deselect_all_visible(vc->view_layer, vc->v3d);
  }
  const bool select = (sel_op != SEL_OP_SUB);
  const int select_flag = select ? BASE_SELECTED : 0;

  Base *base;
  for (base = FIRSTBASE(view_layer); base; base = base->next) {
    if (BASE_SELECTABLE(v3d, base) && ((base->flag & BASE_SELECTED) != select_flag)) {
      float screen_co[2];
      if (ED_view3d_project_float_global(
              vc->region, base->object->obmat[3], screen_co, V3D_PROJ_TEST_CLIP_DEFAULT) ==
          V3D_PROJ_RET_OK) {
        if (len_squared_v2v2(mval_fl, screen_co) <= radius_squared) {
          ED_object_base_select(base, select ? BA_SELECT : BA_DESELECT);
          changed = true;
        }
      }
    }
  }

  return changed;
}

/* not a real operator, only for circle test */
static int view3d_circle_select_exec(bContext *C, wmOperator *op)
{
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  ViewContext vc;
  const int radius = API_int_get(op->ptr, "radius");
  const int mval[2] = {API_int_get(op->ptr, "x"), API_int_get(op->ptr, "y")};

  /* Allow each selection type to allocate their own data that's used between executions. */
  wmGesture *gesture = op->customdata; /* NULL when non-modal. */
  wmGenericUserData wm_userdata_buf = {0};
  wmGenericUserData *wm_userdata = gesture ? &gesture->user_data : &wm_userdata_buf;

  const eSelectOp sel_op = ED_select_op_modal(RNA_enum_get(op->ptr, "mode"),
                                              WM_gesture_is_modal_first(gesture));

  ED_view3d_viewcontext_init(C, &vc, depsgraph);

  Object *obact = vc.obact;
  Object *obedit = vc.obedit;

  if (obedit || DUNE_paint_select_elem_test(obact) || (obact && (obact->mode & OB_MODE_POSE))) {
    view3d_operator_needs_opengl(C);
    if (obedit == NULL) {
      DUNE_object_update_select_id(CTX_data_main(C));
    }

    FOREACH_OBJECT_IN_MODE_BEGIN (vc.view_layer, vc.v3d, obact->type, obact->mode, ob_iter) {
      ED_view3d_viewcontext_init_object(&vc, ob_iter);

      obact = vc.obact;
      obedit = vc.obedit;

      if (obedit) {
        obedit_circle_select(C, &vc, wm_userdata, sel_op, mval, (float)radius);
      }
      else if (DUNE_paint_select_face_test(obact)) {
        paint_facesel_circle_select(&vc, wm_userdata, sel_op, mval, (float)radius);
      }
      else if (DUNE_paint_select_vert_test(obact)) {
        paint_vertsel_circle_select(&vc, wm_userdata, sel_op, mval, (float)radius);
      }
      else if (obact->mode & OB_MODE_POSE) {
        pose_circle_select(&vc, sel_op, mval, (float)radius);
        ED_outliner_select_sync_from_pose_bone_tag(C);
      }
      else {
        LIB_assert(0);
      }
    }
    FOREACH_OBJECT_IN_MODE_END;
  }
  else if (obact && (obact->mode & OB_MODE_PARTICLE_EDIT)) {
    if (PE_circle_select(C, wm_userdata, sel_op, mval, (float)radius)) {
      return OPERATOR_FINISHED;
    }
    return OPERATOR_CANCELLED;
  }
  else if (obact && obact->mode & OB_MODE_SCULPT) {
    return OPERATOR_CANCELLED;
  }
  else {
    if (object_circle_select(&vc, sel_op, mval, (float)radius)) {
      DEG_id_tag_update(&vc.scene->id, ID_RECALC_SELECT);
      WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, vc.scene);

      ED_outliner_select_sync_from_object_tag(C);
    }
  }

  /* Otherwise this is freed by the gesture. */
  if (wm_userdata == &wm_userdata_buf) {
    WM_generic_user_data_free(wm_userdata);
  }
  else {
    struct EditSelectBuf_Cache *esel = wm_userdata->data;
    if (esel && esel->select_bitmap) {
      MEM_freeN(esel->select_bitmap);
      esel->select_bitmap = NULL;
    }
  }

  return OPERATOR_FINISHED;
}

void VIEW3D_OT_select_circle(wmOperatorType *ot)
{
  ot->name = "Circle Select";
  ot->description = "Select items using circle selection";
  ot->idname = "VIEW3D_OT_select_circle";

  ot->invoke = WM_gesture_circle_invoke;
  ot->modal = WM_gesture_circle_modal;
  ot->exec = view3d_circle_select_exec;
  ot->poll = view3d_selectable_data;
  ot->cancel = WM_gesture_circle_cancel;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  WM_operator_properties_gesture_circle(ot);
  WM_operator_properties_select_operation_simple(ot);
}
