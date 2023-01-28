#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "DNA_action_types.h"
#include "DNA_armature_types.h"
#include "DNA_curve_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_meta_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_tracking_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_array.h"
#include "BLI_bitmap.h"
#include "BLI_lasso_2d.h"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_rect.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#ifdef __BIG_ENDIAN__
#  include "BLI_endian_switch.h"
#endif

/* vertex box select */
#include "BKE_global.h"
#include "BKE_main.h"
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "BKE_action.h"
#include "BKE_armature.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_editmesh.h"
#include "BKE_layer.h"
#include "BKE_mball.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_paint.h"
#include "BKE_scene.h"
#include "BKE_tracking.h"
#include "BKE_workspace.h"

#include "DEG_depsgraph.h"

#include "WM_api.h"
#include "WM_toolsystem.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "ED_armature.h"
#include "ED_curve.h"
#include "ED_gpencil.h"
#include "ED_lattice.h"
#include "ED_mball.h"
#include "ED_mesh.h"
#include "ED_object.h"
#include "ED_outliner.h"
#include "ED_particle.h"
#include "ED_screen.h"
#include "ED_sculpt.h"
#include "ED_select_utils.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "GPU_matrix.h"
#include "GPU_select.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "DRW_engine.h"
#include "DRW_select_buffer.h"

#include "view3d_intern.h" /* own include */

// #include "PIL_time_utildefines.h"

/* -------------------------------------------------------------------- */
/** \name Public Utilities
 * \{ */

float ED_view3d_select_dist_px(void)
{
  return 75.0f * U.pixelsize;
}

void ED_view3d_viewcontext_init(bContext *C, ViewContext *vc, Depsgraph *depsgraph)
{
  /* TODO: should return whether there is valid context to continue. */

  memset(vc, 0, sizeof(ViewContext));
  vc->C = C;
  vc->region = CTX_wm_region(C);
  vc->bmain = CTX_data_main(C);
  vc->depsgraph = depsgraph;
  vc->scene = CTX_data_scene(C);
  vc->view_layer = CTX_data_view_layer(C);
  vc->v3d = CTX_wm_view3d(C);
  vc->win = CTX_wm_window(C);
  vc->rv3d = CTX_wm_region_view3d(C);
  vc->obact = CTX_data_active_object(C);
  vc->obedit = CTX_data_edit_object(C);
}

void ED_view3d_viewcontext_init_object(ViewContext *vc, Object *obact)
{
  vc->obact = obact;
  if (vc->obedit) {
    BLI_assert(BKE_object_is_in_editmode(obact));
    vc->obedit = obact;
    if (vc->em) {
      vc->em = BKE_editmesh_from_object(vc->obedit);
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Internal Object Utilities
 * \{ */

static bool object_deselect_all_visible(ViewLayer *view_layer, View3D *v3d)
{
  bool changed = false;
  LISTBASE_FOREACH (Base *, base, &view_layer->object_bases) {
    if (base->flag & BASE_SELECTED) {
      if (BASE_SELECTABLE(v3d, base)) {
        ED_object_base_select(base, BA_DESELECT);
        changed = true;
      }
    }
  }
  return changed;
}

/* deselect all except b */
static bool object_deselect_all_except(ViewLayer *view_layer, Base *b)
{
  bool changed = false;
  LISTBASE_FOREACH (Base *, base, &view_layer->object_bases) {
    if (base->flag & BASE_SELECTED) {
      if (b != base) {
        ED_object_base_select(base, BA_DESELECT);
        changed = true;
      }
    }
  }
  return changed;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Internal Edit-Mesh Select Buffer Wrapper
 *
 * Avoid duplicate code when using edit-mode selection,
 * actual logic is handled outside of this function.
 *
 * \note Currently this #EDBMSelectID_Context which is mesh specific
 * however the logic could also be used for non-meshes too.
 *
 * \{ */

struct EditSelectBuf_Cache {
  BLI_bitmap *select_bitmap;
};

static void editselect_buf_cache_init(ViewContext *vc, short select_mode)
{
  if (vc->obedit) {
    uint bases_len = 0;
    Base **bases = BKE_view_layer_array_from_bases_in_edit_mode(
        vc->view_layer, vc->v3d, &bases_len);

    DRW_select_buffer_context_create(bases, bases_len, select_mode);
    MEM_freeN(bases);
  }
  else {
    /* Use for paint modes, currently only a single object at a time. */
    if (vc->obact) {
      Base *base = BKE_view_layer_base_find(vc->view_layer, vc->obact);
      DRW_select_buffer_context_create(&base, 1, select_mode);
    }
  }
}

static void editselect_buf_cache_free(struct EditSelectBuf_Cache *esel)
{
  MEM_SAFE_FREE(esel->select_bitmap);
}

static void editselect_buf_cache_free_voidp(void *esel_voidp)
{
  editselect_buf_cache_free(esel_voidp);
  MEM_freeN(esel_voidp);
}

static void editselect_buf_cache_init_with_generic_userdata(wmGenericUserData *wm_userdata,
                                                            ViewContext *vc,
                                                            short select_mode)
{
  struct EditSelectBuf_Cache *esel = MEM_callocN(sizeof(*esel), __func__);
  wm_userdata->data = esel;
  wm_userdata->free_fn = editselect_buf_cache_free_voidp;
  wm_userdata->use_free = true;
  editselect_buf_cache_init(vc, select_mode);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Internal Edit-Mesh Utilities
 * \{ */

static bool edbm_backbuf_check_and_select_verts(struct EditSelectBuf_Cache *esel,
                                                Depsgraph *depsgraph,
                                                Object *ob,
                                                BMEditMesh *em,
                                                const eSelectOp sel_op)
{
  BMVert *eve;
  BMIter iter;
  bool changed = false;

  const BLI_bitmap *select_bitmap = esel->select_bitmap;
  uint index = DRW_select_buffer_context_offset_for_object_elem(depsgraph, ob, SCE_SELECT_VERTEX);
  if (index == 0) {
    return false;
  }

  index -= 1;
  BM_ITER_MESH (eve, &iter, em->bm, BM_VERTS_OF_MESH) {
    if (!BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
      const bool is_select = BM_elem_flag_test(eve, BM_ELEM_SELECT);
      const bool is_inside = BLI_BITMAP_TEST_BOOL(select_bitmap, index);
      const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
      if (sel_op_result != -1) {
        BM_vert_select_set(em->bm, eve, sel_op_result);
        changed = true;
      }
    }
    index++;
  }
  return changed;
}

static bool edbm_backbuf_check_and_select_edges(struct EditSelectBuf_Cache *esel,
                                                Depsgraph *depsgraph,
                                                Object *ob,
                                                BMEditMesh *em,
                                                const eSelectOp sel_op)
{
  BMEdge *eed;
  BMIter iter;
  bool changed = false;

  const BLI_bitmap *select_bitmap = esel->select_bitmap;
  uint index = DRW_select_buffer_context_offset_for_object_elem(depsgraph, ob, SCE_SELECT_EDGE);
  if (index == 0) {
    return false;
  }

  index -= 1;
  BM_ITER_MESH (eed, &iter, em->bm, BM_EDGES_OF_MESH) {
    if (!BM_elem_flag_test(eed, BM_ELEM_HIDDEN)) {
      const bool is_select = BM_elem_flag_test(eed, BM_ELEM_SELECT);
      const bool is_inside = BLI_BITMAP_TEST_BOOL(select_bitmap, index);
      const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
      if (sel_op_result != -1) {
        BM_edge_select_set(em->bm, eed, sel_op_result);
        changed = true;
      }
    }
    index++;
  }
  return changed;
}

static bool edbm_backbuf_check_and_select_faces(struct EditSelectBuf_Cache *esel,
                                                Depsgraph *depsgraph,
                                                Object *ob,
                                                BMEditMesh *em,
                                                const eSelectOp sel_op)
{
  BMFace *efa;
  BMIter iter;
  bool changed = false;

  const BLI_bitmap *select_bitmap = esel->select_bitmap;
  uint index = DRW_select_buffer_context_offset_for_object_elem(depsgraph, ob, SCE_SELECT_FACE);
  if (index == 0) {
    return false;
  }

  index -= 1;
  BM_ITER_MESH (efa, &iter, em->bm, BM_FACES_OF_MESH) {
    if (!BM_elem_flag_test(efa, BM_ELEM_HIDDEN)) {
      const bool is_select = BM_elem_flag_test(efa, BM_ELEM_SELECT);
      const bool is_inside = BLI_BITMAP_TEST_BOOL(select_bitmap, index);
      const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
      if (sel_op_result != -1) {
        BM_face_select_set(em->bm, efa, sel_op_result);
        changed = true;
      }
    }
    index++;
  }
  return changed;
}

/* object mode, edbm_ prefix is confusing here, rename? */
static bool edbm_backbuf_check_and_select_verts_obmode(Mesh *me,
                                                       struct EditSelectBuf_Cache *esel,
                                                       const eSelectOp sel_op)
{
  MVert *mv = me->mvert;
  uint index;
  bool changed = false;

  const BLI_bitmap *select_bitmap = esel->select_bitmap;

  if (mv) {
    for (index = 0; index < me->totvert; index++, mv++) {
      if (!(mv->flag & ME_HIDE)) {
        const bool is_select = mv->flag & SELECT;
        const bool is_inside = BLI_BITMAP_TEST_BOOL(select_bitmap, index);
        const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
        if (sel_op_result != -1) {
          SET_FLAG_FROM_TEST(mv->flag, sel_op_result, SELECT);
          changed = true;
        }
      }
    }
  }
  return changed;
}

/* object mode, edbm_ prefix is confusing here, rename? */
static bool edbm_backbuf_check_and_select_faces_obmode(Mesh *me,
                                                       struct EditSelectBuf_Cache *esel,
                                                       const eSelectOp sel_op)
{
  MPoly *mpoly = me->mpoly;
  uint index;
  bool changed = false;

  const BLI_bitmap *select_bitmap = esel->select_bitmap;

  if (mpoly) {
    for (index = 0; index < me->totpoly; index++, mpoly++) {
      if (!(mpoly->flag & ME_HIDE)) {
        const bool is_select = mpoly->flag & ME_FACE_SEL;
        const bool is_inside = BLI_BITMAP_TEST_BOOL(select_bitmap, index);
        const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
        if (sel_op_result != -1) {
          SET_FLAG_FROM_TEST(mpoly->flag, sel_op_result, ME_FACE_SEL);
          changed = true;
        }
      }
    }
  }
  return changed;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Lasso Select
 * \{ */

typedef struct LassoSelectUserData {
  ViewContext *vc;
  const rcti *rect;
  const rctf *rect_fl;
  rctf _rect_fl;
  const int (*mcoords)[2];
  int mcoords_len;
  eSelectOp sel_op;
  eBezTriple_Flag select_flag;

  /* runtime */
  int pass;
  bool is_done;
  bool is_changed;
} LassoSelectUserData;

static void view3d_userdata_lassoselect_init(LassoSelectUserData *r_data,
                                             ViewContext *vc,
                                             const rcti *rect,
                                             const int (*mcoords)[2],
                                             const int mcoords_len,
                                             const eSelectOp sel_op)
{
  r_data->vc = vc;

  r_data->rect = rect;
  r_data->rect_fl = &r_data->_rect_fl;
  BLI_rctf_rcti_copy(&r_data->_rect_fl, rect);

  r_data->mcoords = mcoords;
  r_data->mcoords_len = mcoords_len;
  r_data->sel_op = sel_op;
  /* SELECT by default, but can be changed if needed (only few cases use and respect this). */
  r_data->select_flag = SELECT;

  /* runtime */
  r_data->pass = 0;
  r_data->is_done = false;
  r_data->is_changed = false;
}

static bool view3d_selectable_data(bContext *C)
{
  Object *ob = CTX_data_active_object(C);

  if (!ED_operator_region_view3d_active(C)) {
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
          !BKE_paint_select_elem_test(ob)) {
        return 0;
      }
    }
  }

  return 1;
}

/* helper also for box_select */
static bool edge_fully_inside_rect(const rctf *rect, const float v1[2], const float v2[2])
{
  return BLI_rctf_isect_pt_v(rect, v1) && BLI_rctf_isect_pt_v(rect, v2);
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

static void do_lasso_select_pose__do_tag(void *userData,
                                         struct bPoseChannel *pchan,
                                         const float screen_co_a[2],
                                         const float screen_co_b[2])
{
  LassoSelectUserData *data = userData;
  const bArmature *arm = data->vc->obact->data;
  if (!PBONE_SELECTABLE(arm, pchan->bone)) {
    return;
  }

  if (BLI_rctf_isect_segment(data->rect_fl, screen_co_a, screen_co_b) &&
      BLI_lasso_is_edge_inside(
          data->mcoords, data->mcoords_len, UNPACK2(screen_co_a), UNPACK2(screen_co_b), INT_MAX)) {
    pchan->bone->flag |= BONE_DONE;
    data->is_changed = true;
  }
}
static void do_lasso_tag_pose(ViewContext *vc,
                              Object *ob,
                              const int mcoords[][2],
                              const int mcoords_len)
{
  ViewContext vc_tmp;
  LassoSelectUserData data;
  rcti rect;

  if ((ob->type != OB_ARMATURE) || (ob->pose == NULL)) {
    return;
  }

  vc_tmp = *vc;
  vc_tmp.obact = ob;

  BLI_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassoselect_init(&data, vc, &rect, mcoords, mcoords_len, 0);

  ED_view3d_init_mats_rv3d(vc_tmp.obact, vc->rv3d);

  /* Treat bones as clipped segments (no joints). */
  pose_foreachScreenBone(&vc_tmp,
                         do_lasso_select_pose__do_tag,
                         &data,
                         V3D_PROJ_TEST_CLIP_DEFAULT | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);
}

static bool do_lasso_select_objects(ViewContext *vc,
                                    const int mcoords[][2],
                                    const int mcoords_len,
                                    const eSelectOp sel_op)
{
  View3D *v3d = vc->v3d;
  Base *base;

  bool changed = false;
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    changed |= object_deselect_all_visible(vc->view_layer, vc->v3d);
  }

  for (base = vc->view_layer->object_bases.first; base; base = base->next) {
    if (BASE_SELECTABLE(v3d, base)) { /* Use this to avoid unnecessary lasso look-ups. */
      const bool is_select = base->flag & BASE_SELECTED;
      const bool is_inside = ((ED_view3d_project_base(vc->region, base) == V3D_PROJ_RET_OK) &&
                              BLI_lasso_is_point_inside(
                                  mcoords, mcoords_len, base->sx, base->sy, IS_CLIPPED));
      const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
      if (sel_op_result != -1) {
        ED_object_base_select(base, sel_op_result ? BA_SELECT : BA_DESELECT);
        changed = true;
      }
    }
  }

  if (changed) {
    DEG_id_tag_update(&vc->scene->id, ID_RECALC_SELECT);
    WM_main_add_notifier(NC_SCENE | ND_OB_SELECT, vc->scene);
  }
  return changed;
}

/**
 * Use for lasso & box select.
 */
static Base **do_pose_tag_select_op_prepare(ViewContext *vc, uint *r_bases_len)
{
  Base **bases = NULL;
  BLI_array_declare(bases);
  FOREACH_BASE_IN_MODE_BEGIN (vc->view_layer, vc->v3d, OB_ARMATURE, OB_MODE_POSE, base_iter) {
    Object *ob_iter = base_iter->object;
    bArmature *arm = ob_iter->data;
    LISTBASE_FOREACH (bPoseChannel *, pchan, &ob_iter->pose->chanbase) {
      Bone *bone = pchan->bone;
      bone->flag &= ~BONE_DONE;
    }
    arm->id.tag |= LIB_TAG_DOIT;
    ob_iter->id.tag &= ~LIB_TAG_DOIT;
    BLI_array_append(bases, base_iter);
  }
  FOREACH_BASE_IN_MODE_END;
  *r_bases_len = BLI_array_len(bases);
  return bases;
}

static bool do_pose_tag_select_op_exec(Base **bases, const uint bases_len, const eSelectOp sel_op)
{
  bool changed_multi = false;

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    for (int i = 0; i < bases_len; i++) {
      Base *base_iter = bases[i];
      Object *ob_iter = base_iter->object;
      if (ED_pose_deselect_all(ob_iter, SEL_DESELECT, false)) {
        ED_pose_bone_select_tag_update(ob_iter);
        changed_multi = true;
      }
    }
  }

  for (int i = 0; i < bases_len; i++) {
    Base *base_iter = bases[i];
    Object *ob_iter = base_iter->object;
    bArmature *arm = ob_iter->data;

    /* Don't handle twice. */
    if (arm->id.tag & LIB_TAG_DOIT) {
      arm->id.tag &= ~LIB_TAG_DOIT;
    }
    else {
      continue;
    }

    bool changed = true;
    LISTBASE_FOREACH (bPoseChannel *, pchan, &ob_iter->pose->chanbase) {
      Bone *bone = pchan->bone;
      if ((bone->flag & BONE_UNSELECTABLE) == 0) {
        const bool is_select = bone->flag & BONE_SELECTED;
        const bool is_inside = bone->flag & BONE_DONE;
        const int sel_op_result = ED_select_op_action_deselected(sel_op, is_select, is_inside);
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
      ED_pose_bone_select_tag_update(ob_iter);
      changed_multi = true;
    }
  }
  return changed_multi;
}

static bool do_lasso_select_pose(ViewContext *vc,
                                 const int mcoords[][2],
                                 const int mcoords_len,
                                 const eSelectOp sel_op)
{
  uint bases_len;
  Base **bases = do_pose_tag_select_op_prepare(vc, &bases_len);

  for (int i = 0; i < bases_len; i++) {
    Base *base_iter = bases[i];
    Object *ob_iter = base_iter->object;
    do_lasso_tag_pose(vc, ob_iter, mcoords, mcoords_len);
  }

  const bool changed_multi = do_pose_tag_select_op_exec(bases, bases_len, sel_op);
  if (changed_multi) {
    DEG_id_tag_update(&vc->scene->id, ID_RECALC_SELECT);
    WM_main_add_notifier(NC_SCENE | ND_OB_SELECT, vc->scene);
  }

  MEM_freeN(bases);
  return changed_multi;
}

static void do_lasso_select_mesh__doSelectVert(void *userData,
                                               BMVert *eve,
                                               const float screen_co[2],
                                               int UNUSED(index))
{
  LassoSelectUserData *data = userData;
  const bool is_select = BM_elem_flag_test(eve, BM_ELEM_SELECT);
  const bool is_inside =
      (BLI_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       BLI_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED));
  const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
  if (sel_op_result != -1) {
    BM_vert_select_set(data->vc->em->bm, eve, sel_op_result);
    data->is_changed = true;
  }
}
struct LassoSelectUserData_ForMeshEdge {
  LassoSelectUserData *data;
  struct EditSelectBuf_Cache *esel;
  uint backbuf_offset;
};
static void do_lasso_select_mesh__doSelectEdge_pass0(void *user_data,
                                                     BMEdge *eed,
                                                     const float screen_co_a[2],
                                                     const float screen_co_b[2],
                                                     int index)
{
  struct LassoSelectUserData_ForMeshEdge *data_for_edge = user_data;
  LassoSelectUserData *data = data_for_edge->data;
  bool is_visible = true;
  if (data_for_edge->backbuf_offset) {
    uint bitmap_inedx = data_for_edge->backbuf_offset + index - 1;
    is_visible = BLI_BITMAP_TEST_BOOL(data_for_edge->esel->select_bitmap, bitmap_inedx);
  }

  const bool is_select = BM_elem_flag_test(eed, BM_ELEM_SELECT);
  const bool is_inside =
      (is_visible && edge_fully_inside_rect(data->rect_fl, screen_co_a, screen_co_b) &&
       BLI_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, UNPACK2(screen_co_a), IS_CLIPPED) &&
       BLI_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, UNPACK2(screen_co_b), IS_CLIPPED));
  const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
  if (sel_op_result != -1) {
    BM_edge_select_set(data->vc->em->bm, eed, sel_op_result);
    data->is_done = true;
    data->is_changed = true;
  }
}
static void do_lasso_select_mesh__doSelectEdge_pass1(void *user_data,
                                                     BMEdge *eed,
                                                     const float screen_co_a[2],
                                                     const float screen_co_b[2],
                                                     int index)
{
  struct LassoSelectUserData_ForMeshEdge *data_for_edge = user_data;
  LassoSelectUserData *data = data_for_edge->data;
  bool is_visible = true;
  if (data_for_edge->backbuf_offset) {
    uint bitmap_inedx = data_for_edge->backbuf_offset + index - 1;
    is_visible = BLI_BITMAP_TEST_BOOL(data_for_edge->esel->select_bitmap, bitmap_inedx);
  }

  const bool is_select = BM_elem_flag_test(eed, BM_ELEM_SELECT);
  const bool is_inside = (is_visible && BLI_lasso_is_edge_inside(data->mcoords,
                                                                 data->mcoords_len,
                                                                 UNPACK2(screen_co_a),
                                                                 UNPACK2(screen_co_b),
                                                                 IS_CLIPPED));
  const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
  if (sel_op_result != -1) {
    BM_edge_select_set(data->vc->em->bm, eed, sel_op_result);
    data->is_changed = true;
  }
}

static void do_lasso_select_mesh__doSelectFace(void *userData,
                                               BMFace *efa,
                                               const float screen_co[2],
                                               int UNUSED(index))
{
  LassoSelectUserData *data = userData;
  const bool is_select = BM_elem_flag_test(efa, BM_ELEM_SELECT);
  const bool is_inside =
      (BLI_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       BLI_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED));
  const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
  if (sel_op_result != -1) {
    BM_face_select_set(data->vc->em->bm, efa, sel_op_result);
    data->is_changed = true;
  }
}

static bool do_lasso_select_mesh(ViewContext *vc,
                                 wmGenericUserData *wm_userdata,
                                 const int mcoords[][2],
                                 const int mcoords_len,
                                 const eSelectOp sel_op)
{
  LassoSelectUserData data;
  ToolSettings *ts = vc->scene->toolsettings;
  rcti rect;

  /* set editmesh */
  vc->em = BKE_editmesh_from_object(vc->obedit);

  BLI_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassoselect_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    if (vc->em->bm->totvertsel) {
      EDBM_flag_disable_all(vc->em, BM_ELEM_SELECT);
      data.is_changed = true;
    }
  }

  /* for non zbuf projections, don't change the GL state */
  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

  GPU_matrix_set(vc->rv3d->viewmat);

  const bool use_zbuf = !XRAY_FLAG_ENABLED(vc->v3d);

  struct EditSelectBuf_Cache *esel = wm_userdata->data;
  if (use_zbuf) {
    if (wm_userdata->data == NULL) {
      editselect_buf_cache_init_with_generic_userdata(wm_userdata, vc, ts->selectmode);
      esel = wm_userdata->data;
      esel->select_bitmap = DRW_select_buffer_bitmap_from_poly(
          vc->depsgraph, vc->region, vc->v3d, mcoords, mcoords_len, &rect, NULL);
    }
  }

  if (ts->selectmode & SCE_SELECT_VERTEX) {
    if (use_zbuf) {
      data.is_changed |= edbm_backbuf_check_and_select_verts(
          esel, vc->depsgraph, vc->obedit, vc->em, sel_op);
    }
    else {
      mesh_foreachScreenVert(
          vc, do_lasso_select_mesh__doSelectVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    }
  }
  if (ts->selectmode & SCE_SELECT_EDGE) {
    /* Does both use_zbuf and non-use_zbuf versions (need screen cos for both) */
    struct LassoSelectUserData_ForMeshEdge data_for_edge = {
        .data = &data,
        .esel = use_zbuf ? esel : NULL,
        .backbuf_offset = use_zbuf ? DRW_select_buffer_context_offset_for_object_elem(
                                         vc->depsgraph, vc->obedit, SCE_SELECT_EDGE) :
                                     0,
    };

    const eV3DProjTest clip_flag = V3D_PROJ_TEST_CLIP_NEAR |
                                   (use_zbuf ? 0 : V3D_PROJ_TEST_CLIP_BB);
    /* Fully inside. */
    mesh_foreachScreenEdge_clip_bb_segment(
        vc, do_lasso_select_mesh__doSelectEdge_pass0, &data_for_edge, clip_flag);
    if (data.is_done == false) {
      /* Fall back to partially inside.
       * Clip content to account for edges partially behind the view. */
      mesh_foreachScreenEdge_clip_bb_segment(vc,
                                             do_lasso_select_mesh__doSelectEdge_pass1,
                                             &data_for_edge,
                                             clip_flag | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);
    }
  }

  if (ts->selectmode & SCE_SELECT_FACE) {
    if (use_zbuf) {
      data.is_changed |= edbm_backbuf_check_and_select_faces(
          esel, vc->depsgraph, vc->obedit, vc->em, sel_op);
    }
    else {
      mesh_foreachScreenFace(
          vc, do_lasso_select_mesh__doSelectFace, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
    }
  }

  if (data.is_changed) {
    EDBM_selectmode_flush(vc->em);
  }
  return data.is_changed;
}

static void do_lasso_select_curve__doSelect(void *userData,
                                            Nurb *UNUSED(nu),
                                            BPoint *bp,
                                            BezTriple *bezt,
                                            int beztindex,
                                            bool handles_visible,
                                            const float screen_co[2])
{
  LassoSelectUserData *data = userData;

  const bool is_inside = BLI_lasso_is_point_inside(
      data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED);
  if (bp) {
    const bool is_select = bp->f1 & SELECT;
    const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
    if (sel_op_result != -1) {
      SET_FLAG_FROM_TEST(bp->f1, sel_op_result, data->select_flag);
      data->is_changed = true;
    }
  }
  else {
    if (!handles_visible) {
      /* can only be (beztindex == 1) here since handles are hidden */
      const bool is_select = bezt->f2 & SELECT;
      const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
      if (sel_op_result != -1) {
        SET_FLAG_FROM_TEST(bezt->f2, sel_op_result, data->select_flag);
      }
      bezt->f1 = bezt->f3 = bezt->f2;
      data->is_changed = true;
    }
    else {
      uint8_t *flag_p = (&bezt->f1) + beztindex;
      const bool is_select = *flag_p & SELECT;
      const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
      if (sel_op_result != -1) {
        SET_FLAG_FROM_TEST(*flag_p, sel_op_result, data->select_flag);
        data->is_changed = true;
      }
    }
  }
}

static bool do_lasso_select_curve(ViewContext *vc,
                                  const int mcoords[][2],
                                  const int mcoords_len,
                                  const eSelectOp sel_op)
{
  const bool deselect_all = (sel_op == SEL_OP_SET);
  LassoSelectUserData data;
  rcti rect;

  BLI_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassoselect_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  Curve *curve = (Curve *)vc->obedit->data;
  ListBase *nurbs = BKE_curve_editNurbs_get(curve);

  /* For deselect all, items to be selected are tagged with temp flag. Clear that first. */
  if (deselect_all) {
    BKE_nurbList_flag_set(nurbs, BEZT_FLAG_TEMP_TAG, false);
    data.select_flag = BEZT_FLAG_TEMP_TAG;
  }

  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
  nurbs_foreachScreenVert(vc, do_lasso_select_curve__doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  /* Deselect items that were not added to selection (indicated by temp flag). */
  if (deselect_all) {
    data.is_changed |= BKE_nurbList_flag_set_from_flag(nurbs, BEZT_FLAG_TEMP_TAG, SELECT);
  }

  if (data.is_changed) {
    BKE_curve_nurb_vert_active_validate(vc->obedit->data);
  }
  return data.is_changed;
}

static void do_lasso_select_lattice__doSelect(void *userData, BPoint *bp, const float screen_co[2])
{
  LassoSelectUserData *data = userData;
  const bool is_select = bp->f1 & SELECT;
  const bool is_inside =
      (BLI_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       BLI_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED));
  const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
  if (sel_op_result != -1) {
    SET_FLAG_FROM_TEST(bp->f1, sel_op_result, SELECT);
    data->is_changed = true;
  }
}
static bool do_lasso_select_lattice(ViewContext *vc,
                                    const int mcoords[][2],
                                    const int mcoords_len,
                                    const eSelectOp sel_op)
{
  LassoSelectUserData data;
  rcti rect;

  BLI_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassoselect_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data.is_changed |= ED_lattice_flags_set(vc->obedit, 0);
  }

  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d); /* for foreach's screen/vert projection */
  lattice_foreachScreenVert(
      vc, do_lasso_select_lattice__doSelect, &data, V3D_PROJ_TEST_CLIP_DEFAULT);
  return data.is_changed;
}

static void do_lasso_select_armature__doSelectBone(void *userData,
                                                   EditBone *ebone,
                                                   const float screen_co_a[2],
                                                   const float screen_co_b[2])
{
  LassoSelectUserData *data = userData;
  const bArmature *arm = data->vc->obedit->data;
  if (!EBONE_VISIBLE(arm, ebone)) {
    return;
  }

  int is_ignore_flag = 0;
  int is_inside_flag = 0;

  if (screen_co_a[0] != IS_CLIPPED) {
    if (BLI_rcti_isect_pt(data->rect, UNPACK2(screen_co_a)) &&
        BLI_lasso_is_point_inside(
            data->mcoords, data->mcoords_len, UNPACK2(screen_co_a), INT_MAX)) {
      is_inside_flag |= BONESEL_ROOT;
    }
  }
  else {
    is_ignore_flag |= BONESEL_ROOT;
  }

  if (screen_co_b[0] != IS_CLIPPED) {
    if (BLI_rcti_isect_pt(data->rect, UNPACK2(screen_co_b)) &&
        BLI_lasso_is_point_inside(
            data->mcoords, data->mcoords_len, UNPACK2(screen_co_b), INT_MAX)) {
      is_inside_flag |= BONESEL_TIP;
    }
  }
  else {
    is_ignore_flag |= BONESEL_TIP;
  }

  if (is_ignore_flag == 0) {
    if (is_inside_flag == (BONE_ROOTSEL | BONE_TIPSEL) ||
        BLI_lasso_is_edge_inside(data->mcoords,
                                 data->mcoords_len,
                                 UNPACK2(screen_co_a),
                                 UNPACK2(screen_co_b),
                                 INT_MAX)) {
      is_inside_flag |= BONESEL_BONE;
    }
  }

  ebone->temp.i = is_inside_flag | (is_ignore_flag >> 16);
}
static void do_lasso_select_armature__doSelectBone_clip_content(void *userData,
                                                                EditBone *ebone,
                                                                const float screen_co_a[2],
                                                                const float screen_co_b[2])
{
  LassoSelectUserData *data = userData;
  bArmature *arm = data->vc->obedit->data;
  if (!EBONE_VISIBLE(arm, ebone)) {
    return;
  }

  const int is_ignore_flag = ebone->temp.i << 16;
  int is_inside_flag = ebone->temp.i & ~0xFFFF;

  /* - When #BONESEL_BONE is set, there is nothing to do.
   * - When #BONE_ROOTSEL or #BONE_TIPSEL have been set - they take priority over bone selection.
   */
  if (is_inside_flag & (BONESEL_BONE | BONE_ROOTSEL | BONE_TIPSEL)) {
    return;
  }

  if (BLI_lasso_is_edge_inside(
          data->mcoords, data->mcoords_len, UNPACK2(screen_co_a), UNPACK2(screen_co_b), INT_MAX)) {
    is_inside_flag |= BONESEL_BONE;
  }

  ebone->temp.i = is_inside_flag | (is_ignore_flag >> 16);
}

static bool do_lasso_select_armature(ViewContext *vc,
                                     const int mcoords[][2],
                                     const int mcoords_len,
                                     const eSelectOp sel_op)
{
  LassoSelectUserData data;
  rcti rect;

  BLI_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassoselect_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data.is_changed |= ED_armature_edit_deselect_all_visible(vc->obedit);
  }

  bArmature *arm = vc->obedit->data;

  ED_armature_ebone_listbase_temp_clear(arm->edbo);

  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

  /* Operate on fully visible (non-clipped) points. */
  armature_foreachScreenBone(
      vc, do_lasso_select_armature__doSelectBone, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  /* Operate on bones as segments clipped to the viewport bounds
   * (needed to handle bones with both points outside the view).
   * A separate pass is needed since clipped coordinates can't be used for selecting joints. */
  armature_foreachScreenBone(vc,
                             do_lasso_select_armature__doSelectBone_clip_content,
                             &data,
                             V3D_PROJ_TEST_CLIP_DEFAULT | V3D_PROJ_TEST_CLIP_CONTENT_DEFAULT);

  data.is_changed |= ED_armature_edit_select_op_from_tagged(vc->obedit->data, sel_op);

  if (data.is_changed) {
    WM_main_add_notifier(NC_OBJECT | ND_BONE_SELECT, vc->obedit);
  }
  return data.is_changed;
}

static void do_lasso_select_mball__doSelectElem(void *userData,
                                                struct MetaElem *ml,
                                                const float screen_co[2])
{
  LassoSelectUserData *data = userData;
  const bool is_select = ml->flag & SELECT;
  const bool is_inside =
      (BLI_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       BLI_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], INT_MAX));
  const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
  if (sel_op_result != -1) {
    SET_FLAG_FROM_TEST(ml->flag, sel_op_result, SELECT);
    data->is_changed = true;
  }
}
static bool do_lasso_select_meta(ViewContext *vc,
                                 const int mcoords[][2],
                                 const int mcoords_len,
                                 const eSelectOp sel_op)
{
  LassoSelectUserData data;
  rcti rect;

  MetaBall *mb = (MetaBall *)vc->obedit->data;

  BLI_lasso_boundbox(&rect, mcoords, mcoords_len);

  view3d_userdata_lassoselect_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    data.is_changed |= BKE_mball_deselect_all(mb);
  }

  ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);

  mball_foreachScreenElem(
      vc, do_lasso_select_mball__doSelectElem, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

  return data.is_changed;
}

static void do_lasso_select_meshobject__doSelectVert(void *userData,
                                                     MVert *mv,
                                                     const float screen_co[2],
                                                     int UNUSED(index))
{
  LassoSelectUserData *data = userData;
  const bool is_select = mv->flag & SELECT;
  const bool is_inside =
      (BLI_rctf_isect_pt_v(data->rect_fl, screen_co) &&
       BLI_lasso_is_point_inside(
           data->mcoords, data->mcoords_len, screen_co[0], screen_co[1], IS_CLIPPED));
  const int sel_op_result = ED_select_op_action_deselected(data->sel_op, is_select, is_inside);
  if (sel_op_result != -1) {
    SET_FLAG_FROM_TEST(mv->flag, sel_op_result, SELECT);
    data->is_changed = true;
  }
}
static bool do_lasso_select_paintvert(ViewContext *vc,
                                      wmGenericUserData *wm_userdata,
                                      const int mcoords[][2],
                                      const int mcoords_len,
                                      const eSelectOp sel_op)
{
  const bool use_zbuf = !XRAY_ENABLED(vc->v3d);
  Object *ob = vc->obact;
  Mesh *me = ob->data;
  rcti rect;

  if (me == NULL || me->totvert == 0) {
    return false;
  }

  bool changed = false;
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    /* flush selection at the end */
    changed |= paintvert_deselect_all_visible(ob, SEL_DESELECT, false);
  }

  BLI_lasso_boundbox(&rect, mcoords, mcoords_len);

  struct EditSelectBuf_Cache *esel = wm_userdata->data;
  if (use_zbuf) {
    if (wm_userdata->data == NULL) {
      editselect_buf_cache_init_with_generic_userdata(wm_userdata, vc, SCE_SELECT_VERTEX);
      esel = wm_userdata->data;
      esel->select_bitmap = DRW_select_buffer_bitmap_from_poly(
          vc->depsgraph, vc->region, vc->v3d, mcoords, mcoords_len, &rect, NULL);
    }
  }

  if (use_zbuf) {
    if (esel->select_bitmap != NULL) {
      changed |= edbm_backbuf_check_and_select_verts_obmode(me, esel, sel_op);
    }
  }
  else {
    LassoSelectUserData data;

    view3d_userdata_lassoselect_init(&data, vc, &rect, mcoords, mcoords_len, sel_op);

    ED_view3d_init_mats_rv3d(vc->obact, vc->rv3d);

    meshobject_foreachScreenVert(
        vc, do_lasso_select_meshobject__doSelectVert, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

    changed |= data.is_changed;
  }

  if (changed) {
    if (SEL_OP_CAN_DESELECT(sel_op)) {
      BKE_mesh_mselect_validate(me);
    }
    paintvert_flush_flags(ob);
    paintvert_tag_select_update(vc->C, ob);
  }

  return changed;
}
static bool do_lasso_select_paintface(ViewContext *vc,
                                      wmGenericUserData *wm_userdata,
                                      const int mcoords[][2],
                                      const int mcoords_len,
                                      const eSelectOp sel_op)
{
  Object *ob = vc->obact;
  Mesh *me = ob->data;
  rcti rect;

  if (me == NULL || me->totpoly == 0) {
    return false;
  }

  bool changed = false;
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    /* flush selection at the end */
    changed |= paintface_deselect_all_visible(vc->C, ob, SEL_DESELECT, false);
  }

  BLI_lasso_boundbox(&rect, mcoords, mcoords_len);

  struct EditSelectBuf_Cache *esel = wm_userdata->data;
  if (esel == NULL) {
    editselect_buf_cache_init_with_generic_userdata(wm_userdata, vc, SCE_SELECT_FACE);
    esel = wm_userdata->data;
    esel->select_bitmap = DRW_select_buffer_bitmap_from_poly(
        vc->depsgraph, vc->region, vc->v3d, mcoords, mcoords_len, &rect, NULL);
  }

  if (esel->select_bitmap) {
    changed |= edbm_backbuf_check_and_select_faces_obmode(me, esel, sel_op);
  }

  if (changed) {
    paintface_flush_flags(vc->C, ob, SELECT);
  }
  return changed;
}

static bool view3d_lasso_select(bContext *C,
                                ViewContext *vc,
                                const int mcoords[][2],
                                const int mcoords_len,
                                const eSelectOp sel_op)
{
  Object *ob = CTX_data_active_object(C);
  bool changed_multi = false;

  wmGenericUserData wm_userdata_buf = {0};
  wmGenericUserData *wm_userdata = &wm_userdata_buf;

  if (vc->obedit == NULL) { /* Object Mode */
    if (BKE_paint_select_face_test(ob)) {
      changed_multi |= do_lasso_select_paintface(vc, wm_userdata, mcoords, mcoords_len, sel_op);
    }
    else if (BKE_paint_select_vert_test(ob)) {
      changed_multi |= do_lasso_select_paintvert(vc, wm_userdata, mcoords, mcoords_len, sel_op);
    }
    else if (ob &&
             (ob->mode & (OB_MODE_VERTEX_PAINT | OB_MODE_WEIGHT_PAINT | OB_MODE_TEXTURE_PAINT))) {
      /* pass */
    }
    else if (ob && (ob->mode & OB_MODE_PARTICLE_EDIT)) {
      changed_multi |= PE_lasso_select(C, mcoords, mcoords_len, sel_op);
    }
    else if (ob && (ob->mode & OB_MODE_POSE)) {
      changed_multi |= do_lasso_select_pose(vc, mcoords, mcoords_len, sel_op);
      if (changed_multi) {
        ED_outliner_select_sync_from_pose_bone_tag(C);
      }
    }
    else {
      changed_multi |= do_lasso_select_objects(vc, mcoords, mcoords_len, sel_op);
      if (changed_multi) {
        ED_outliner_select_sync_from_object_tag(C);
      }
    }
  }
  else { /* Edit Mode */
    FOREACH_OBJECT_IN_MODE_BEGIN (vc->view_layer, vc->v3d, ob->type, ob->mode, ob_iter) {
      ED_view3d_viewcontext_init_object(vc, ob_iter);
      bool changed = false;

      switch (vc->obedit->type) {
        case OB_MESH:
          changed = do_lasso_select_mesh(vc, wm_userdata, mcoords, mcoords_len, sel_op);
          break;
        case OB_CURVES_LEGACY:
        case OB_SURF:
          changed = do_lasso_select_curve(vc, mcoords, mcoords_len, sel_op);
          break;
        case OB_LATTICE:
          changed = do_lasso_select_lattice(vc, mcoords, mcoords_len, sel_op);
          break;
        case OB_ARMATURE:
          changed = do_lasso_select_armature(vc, mcoords, mcoords_len, sel_op);
          if (changed) {
            ED_outliner_select_sync_from_edit_bone_tag(C);
          }
          break;
        case OB_MBALL:
          changed = do_lasso_select_meta(vc, mcoords, mcoords_len, sel_op);
          break;
        default:
          BLI_assert_msg(0, "lasso select on incorrect object type");
          break;
      }

      if (changed) {
        DEG_id_tag_update(vc->obedit->data, ID_RECALC_SELECT);
        WM_event_add_notifier(C, NC_GEOM | ND_SELECT, vc->obedit->data);
        changed_multi = true;
      }
    }
    FOREACH_OBJECT_IN_MODE_END;
  }

  WM_generic_user_data_free(wm_userdata);

  return changed_multi;
}

/* lasso operator gives properties, but since old code works
 * with short array we convert */
static int view3d_lasso_select_exec(bContext *C, wmOperator *op)
{
  ViewContext vc;
  int mcoords_len;
  const int(*mcoords)[2] = WM_gesture_lasso_path_to_array(C, op, &mcoords_len);

  if (mcoords) {
    Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
    view3d_operator_needs_opengl(C);
    BKE_object_update_select_id(CTX_data_main(C));

    /* setup view context for argument to callbacks */
    ED_view3d_viewcontext_init(C, &vc, depsgraph);

    eSelectOp sel_op = RNA_enum_get(op->ptr, "mode");
    bool changed_multi = view3d_lasso_select(C, &vc, mcoords, mcoords_len, sel_op);

    MEM_freeN((void *)mcoords);

    if (changed_multi) {
      return OPERATOR_FINISHED;
    }
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_PASS_THROUGH;
}

void VIEW3D_OT_select_lasso(wmOperatorType *ot)
{
  ot->name = "Lasso Select";
  ot->description = "Select items using lasso selection";
  ot->idname = "VIEW3D_OT_select_lasso";

  ot->invoke = WM_gesture_lasso_invoke;
  ot->modal = WM_gesture_lasso_modal;
  ot->exec = view3d_lasso_select_exec;
  ot->poll = view3d_selectable_data;
  ot->cancel = WM_gesture_lasso_cancel;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_DEPENDS_ON_CURSOR;

  /* properties */
  WM_operator_properties_gesture_lasso(ot);
  WM_operator_properties_select_operation(ot);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Cursor Picking
 * \{ */

/* The max number of menu items in an object select menu */
typedef struct SelMenuItemF {
  char idname[MAX_ID_NAME - 2];
  int icon;
  Base *base_ptr;
  void *item_ptr;
} SelMenuItemF;

#define SEL_MENU_SIZE 22
static SelMenuItemF object_mouse_select_menu_data[SEL_MENU_SIZE];

/* special (crappy) operator only for menu select */
static const EnumPropertyItem *object_select_menu_enum_itemf(bContext *C,
                                                             PointerRNA *UNUSED(ptr),
                                                             PropertyRNA *UNUSED(prop),
                                                             bool *r_free)
{
  EnumPropertyItem *item = NULL, item_tmp = {0};
  int totitem = 0;
  int i = 0;

  /* Don't need context but avoid API doc-generation using this. */
  if (C == NULL || object_mouse_select_menu_data[i].idname[0] == '\0') {
    return DummyRNA_NULL_items;
  }

  for (; i < SEL_MENU_SIZE && object_mouse_select_menu_data[i].idname[0] != '\0'; i++) {
    item_tmp.name = object_mouse_select_menu_data[i].idname;
    item_tmp.identifier = object_mouse_select_menu_data[i].idname;
    item_tmp.value = i;
    item_tmp.icon = object_mouse_select_menu_data[i].icon;
    RNA_enum_item_add(&item, &totitem, &item_tmp);
  }

  RNA_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static int object_select_menu_exec(bContext *C, wmOperator *op)
{
  const int name_index = RNA_enum_get(op->ptr, "name");
  const bool extend = RNA_boolean_get(op->ptr, "extend");
  const bool deselect = RNA_boolean_get(op->ptr, "deselect");
  const bool toggle = RNA_boolean_get(op->ptr, "toggle");
  bool changed = false;
  const char *name = object_mouse_select_menu_data[name_index].idname;

  View3D *v3d = CTX_wm_view3d(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const Base *oldbasact = BASACT(view_layer);

  Base *basact = NULL;
  CTX_DATA_BEGIN (C, Base *, base, selectable_bases) {
    /* This is a bit dodgy, there should only be ONE object with this name,
     * but library objects can mess this up. */
    if (STREQ(name, base->object->id.name + 2)) {
      basact = base;
      break;
    }
  }
  CTX_DATA_END;

  if (basact == NULL) {
    return OPERATOR_CANCELLED;
  }
  UNUSED_VARS_NDEBUG(v3d);
  BLI_assert(BASE_SELECTABLE(v3d, basact));

  if (extend) {
    ED_object_base_select(basact, BA_SELECT);
    changed = true;
  }
  else if (deselect) {
    ED_object_base_select(basact, BA_DESELECT);
    changed = true;
  }
  else if (toggle) {
    if (basact->flag & BASE_SELECTED) {
      if (basact == oldbasact) {
        ED_object_base_select(basact, BA_DESELECT);
        changed = true;
      }
    }
    else {
      ED_object_base_select(basact, BA_SELECT);
      changed = true;
    }
  }
  else {
    object_deselect_all_except(view_layer, basact);
    ED_object_base_select(basact, BA_SELECT);
    changed = true;
  }

  if ((oldbasact != basact)) {
    ED_object_base_activate(C, basact);
  }

  /* weak but ensures we activate menu again before using the enum */
  memset(object_mouse_select_menu_data, 0, sizeof(object_mouse_select_menu_data));

  /* undo? */
  if (changed) {
    Scene *scene = CTX_data_scene(C);
    DEG_id_tag_update(&scene->id, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);

    ED_outliner_select_sync_from_object_tag(C);

    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void VIEW3D_OT_select_menu(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Select Menu";
  ot->description = "Menu object selection";
  ot->idname = "VIEW3D_OT_select_menu";

  /* api callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = object_select_menu_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* #Object.id.name to select (dynamic enum). */
  prop = RNA_def_enum(ot->srna, "name", DummyRNA_NULL_items, 0, "Object Name", "");
  RNA_def_enum_funcs(prop, object_select_menu_enum_itemf);
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_ENUM_NO_TRANSLATE);
  ot->prop = prop;

  prop = RNA_def_boolean(ot->srna, "extend", 0, "Extend", "");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "deselect", 0, "Deselect", "");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "toggle", 0, "Toggle", "");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/**
 * \return True when a menu was activated.
 */
static bool object_mouse_select_menu(bContext *C,
                                     ViewContext *vc,
                                     const GPUSelectResult *buffer,
                                     const int hits,
                                     const int mval[2],
                                     const struct SelectPick_Params *params,
                                     Base **r_basact)
{
  int base_count = 0;
  bool ok;
  LinkNodePair linklist = {NULL, NULL};

  /* handle base->object->select_id */
  CTX_DATA_BEGIN (C, Base *, base, selectable_bases) {
    ok = false;

    /* two selection methods, the CTRL select uses max dist of 15 */
    if (buffer) {
      for (int a = 0; a < hits; a++) {
        /* index was converted */
        if (base->object->runtime.select_id == (buffer[a].id & ~0xFFFF0000)) {
          ok = true;
          break;
        }
      }
    }
    else {
      const int dist = 15 * U.pixelsize;
      if (ED_view3d_project_base(vc->region, base) == V3D_PROJ_RET_OK) {
        const int delta_px[2] = {base->sx - mval[0], base->sy - mval[1]};
        if (len_manhattan_v2_int(delta_px) < dist) {
          ok = true;
        }
      }
    }

    if (ok) {
      base_count++;
      BLI_linklist_append(&linklist, base);

      if (base_count == SEL_MENU_SIZE) {
        break;
      }
    }
  }
  CTX_DATA_END;

  *r_basact = NULL;

  if (base_count == 0) {
    return false;
  }
  if (base_count == 1) {
    Base *base = (Base *)linklist.list->link;
    BLI_linklist_free(linklist.list, NULL);
    *r_basact = base;
    return false;
  }

  /* UI, full in static array values that we later use in an enum function */
  LinkNode *node;
  int i;

  memset(object_mouse_select_menu_data, 0, sizeof(object_mouse_select_menu_data));

  for (node = linklist.list, i = 0; node; node = node->next, i++) {
    Base *base = node->link;
    Object *ob = base->object;
    const char *name = ob->id.name + 2;

    BLI_strncpy(object_mouse_select_menu_data[i].idname, name, MAX_ID_NAME - 2);
    object_mouse_select_menu_data[i].icon = UI_icon_from_id(&ob->id);
  }

  wmOperatorType *ot = WM_operatortype_find("VIEW3D_OT_select_menu", false);
  PointerRNA ptr;

  WM_operator_properties_create_ptr(&ptr, ot);
  RNA_boolean_set(&ptr, "extend", params->sel_op == SEL_OP_ADD);
  RNA_boolean_set(&ptr, "deselect", params->sel_op == SEL_OP_SUB);
  RNA_boolean_set(&ptr, "toggle", params->sel_op == SEL_OP_XOR);
  WM_operator_name_call_ptr(C, ot, WM_OP_INVOKE_DEFAULT, &ptr, NULL);
  WM_operator_properties_free(&ptr);

  BLI_linklist_free(linklist.list, NULL);
  return true;
}

static int bone_select_menu_exec(bContext *C, wmOperator *op)
{
  const int name_index = RNA_enum_get(op->ptr, "name");

  const struct SelectPick_Params params = {
      .sel_op = ED_select_op_from_booleans(RNA_boolean_get(op->ptr, "extend"),
                                           RNA_boolean_get(op->ptr, "deselect"),
                                           RNA_boolean_get(op->ptr, "toggle")),
  };

  View3D *v3d = CTX_wm_view3d(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const Base *oldbasact = BASACT(view_layer);

  Base *basact = object_mouse_select_menu_data[name_index].base_ptr;

  if (basact == NULL) {
    return OPERATOR_CANCELLED;
  }

  BLI_assert(BASE_SELECTABLE(v3d, basact));

  if (basact->object->mode & OB_MODE_EDIT) {
    EditBone *ebone = (EditBone *)object_mouse_select_menu_data[name_index].item_ptr;
    ED_armature_edit_select_pick_bone(C, basact, ebone, BONE_SELECTED, &params);
  }
  else {
    bPoseChannel *pchan = (bPoseChannel *)object_mouse_select_menu_data[name_index].item_ptr;
    ED_armature_pose_select_pick_bone(view_layer, v3d, basact->object, pchan->bone, &params);
  }

  /* Weak but ensures we activate the menu again before using the enum. */
  memset(object_mouse_select_menu_data, 0, sizeof(object_mouse_select_menu_data));

  /* We make the armature selected:
   * Not-selected active object in pose-mode won't work well for tools. */
  ED_object_base_select(basact, BA_SELECT);

  WM_event_add_notifier(C, NC_OBJECT | ND_BONE_SELECT, basact->object);
  WM_event_add_notifier(C, NC_OBJECT | ND_BONE_ACTIVE, basact->object);

  /* In weight-paint, we use selected bone to select vertex-group,
   * so don't switch to new active object. */
  if (oldbasact) {
    if (basact->object->mode & OB_MODE_EDIT) {
      /* Pass. */
    }
    else if (oldbasact->object->mode & OB_MODE_ALL_WEIGHT_PAINT) {
      /* Prevent activating.
       * Selection causes this to be considered the 'active' pose in weight-paint mode.
       * Eventually this limitation may be removed.
       * For now, de-select all other pose objects deforming this mesh. */
      ED_armature_pose_select_in_wpaint_mode(view_layer, basact);
    }
    else {
      if (oldbasact != basact) {
        ED_object_base_activate(C, basact);
      }
    }
  }

  /* Undo? */
  Scene *scene = CTX_data_scene(C);
  DEG_id_tag_update(&scene->id, ID_RECALC_SELECT);
  DEG_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);
  WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);

  ED_outliner_select_sync_from_object_tag(C);

  return OPERATOR_FINISHED;
}

void VIEW3D_OT_bone_select_menu(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Select Menu";
  ot->description = "Menu bone selection";
  ot->idname = "VIEW3D_OT_bone_select_menu";

  /* api callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = bone_select_menu_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* #Object.id.name to select (dynamic enum). */
  prop = RNA_def_enum(ot->srna, "name", DummyRNA_NULL_items, 0, "Bone Name", "");
  RNA_def_enum_funcs(prop, object_select_menu_enum_itemf);
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_ENUM_NO_TRANSLATE);
  ot->prop = prop;

  prop = RNA_def_boolean(ot->srna, "extend", 0, "Extend", "");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "deselect", 0, "Deselect", "");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna, "toggle", 0, "Toggle", "");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/**
 * \return True when a menu was activated.
 */
static bool bone_mouse_select_menu(bContext *C,
                                   const GPUSelectResult *buffer,
                                   const int hits,
                                   const bool is_editmode,
                                   const struct SelectPick_Params *params)
{
  BLI_assert(buffer);

  int bone_count = 0;
  LinkNodePair base_list = {NULL, NULL};
  LinkNodePair bone_list = {NULL, NULL};
  GSet *added_bones = BLI_gset_ptr_new("Bone mouse select menu");

  /* Select logic taken from ed_armature_pick_bone_from_selectbuffer_impl in armature_select.c */
  for (int a = 0; a < hits; a++) {
    void *bone_ptr = NULL;
    Base *bone_base = NULL;
    uint hitresult = buffer[a].id;

    if (!(hitresult & BONESEL_ANY)) {
      /* To avoid including objects in selection. */
      continue;
    }

    hitresult &= ~BONESEL_ANY;
    const uint hit_object = hitresult & 0xFFFF;

    /* Find the hit bone base (armature object). */
    CTX_DATA_BEGIN (C, Base *, base, selectable_bases) {
      if (base->object->runtime.select_id == hit_object) {
        bone_base = base;
        break;
      }
    }
    CTX_DATA_END;

    if (!bone_base) {
      continue;
    }

    /* Determine what the current bone is */
    if (is_editmode) {
      EditBone *ebone;
      const uint hit_bone = (hitresult & ~BONESEL_ANY) >> 16;
      bArmature *arm = bone_base->object->data;
      ebone = BLI_findlink(arm->edbo, hit_bone);
      if (ebone && !(ebone->flag & BONE_UNSELECTABLE)) {
        bone_ptr = ebone;
      }
    }
    else {
      bPoseChannel *pchan;
      const uint hit_bone = (hitresult & ~BONESEL_ANY) >> 16;
      pchan = BLI_findlink(&bone_base->object->pose->chanbase, hit_bone);
      if (pchan && !(pchan->bone->flag & BONE_UNSELECTABLE)) {
        bone_ptr = pchan;
      }
    }

    if (!bone_ptr) {
      continue;
    }
    /* We can hit a bone multiple times, so make sure we are not adding an already included bone
     * to the list. */
    const bool is_duplicate_bone = BLI_gset_haskey(added_bones, bone_ptr);

    if (!is_duplicate_bone) {
      bone_count++;
      BLI_linklist_append(&base_list, bone_base);
      BLI_linklist_append(&bone_list, bone_ptr);
      BLI_gset_insert(added_bones, bone_ptr);

      if (bone_count == SEL_MENU_SIZE) {
        break;
      }
    }
  }

  BLI_gset_free(added_bones, NULL);

  if (bone_count == 0) {
    return false;
  }
  if (bone_count == 1) {
    BLI_linklist_free(base_list.list, NULL);
    BLI_linklist_free(bone_list.list, NULL);
    return false;
  }

  /* UI, full in static array values that we later use in an enum function */
  LinkNode *bone_node, *base_node;
  int i;

  memset(object_mouse_select_menu_data, 0, sizeof(object_mouse_select_menu_data));

  for (base_node = base_list.list, bone_node = bone_list.list, i = 0; bone_node;
       base_node = base_node->next, bone_node = bone_node->next, i++) {
    char *name;

    object_mouse_select_menu_data[i].base_ptr = base_node->link;

    if (is_editmode) {
      EditBone *ebone = bone_node->link;
      object_mouse_select_menu_data[i].item_ptr = ebone;
      name = ebone->name;
    }
    else {
      bPoseChannel *pchan = bone_node->link;
      object_mouse_select_menu_data[i].item_ptr = pchan;
      name = pchan->name;
    }

    BLI_strncpy(object_mouse_select_menu_data[i].idname, name, MAX_ID_NAME - 2);
    object_mouse_select_menu_data[i].icon = ICON_BONE_DATA;
  }

  wmOperatorType *ot = WM_operatortype_find("VIEW3D_OT_bone_select_menu", false);
  PointerRNA ptr;

  WM_operator_properties_create_ptr(&ptr, ot);
  RNA_boolean_set(&ptr, "extend", params->sel_op == SEL_OP_ADD);
  RNA_boolean_set(&ptr, "deselect", params->sel_op == SEL_OP_SUB);
  RNA_boolean_set(&ptr, "toggle", params->sel_op == SEL_OP_XOR);
  WM_operator_name_call_ptr(C, ot, WM_OP_INVOKE_DEFAULT, &ptr, NULL);
  WM_operator_properties_free(&ptr);

  BLI_linklist_free(base_list.list, NULL);
  BLI_linklist_free(bone_list.list, NULL);
  return true;
}

static bool selectbuffer_has_bones(const GPUSelectResult *buffer, const uint hits)
{
  for (uint i = 0; i < hits; i++) {
    if (buffer[i].id & 0xFFFF0000) {
      return true;
    }
  }
  return false;
}

/* utility function for mixed_bones_object_selectbuffer */
static int selectbuffer_ret_hits_15(GPUSelectResult *UNUSED(buffer), const int hits15)
{
  return hits15;
}

static int selectbuffer_ret_hits_9(GPUSelectResult *buffer, const int hits15, const int hits9)
{
  const int ofs = hits15;
  memcpy(buffer, buffer + ofs, hits9 * sizeof(GPUSelectResult));
  return hits9;
}

static int selectbuffer_ret_hits_5(GPUSelectResult *buffer,
                                   const int hits15,
                                   const int hits9,
                                   const int hits5)
{
  const int ofs = hits15 + hits9;
  memcpy(buffer, buffer + ofs, hits5 * sizeof(GPUSelectResult));
  return hits5;
}

/**
 * Populate a select buffer with objects and bones, if there are any.
 * Checks three selection levels and compare.
 *
 * \param do_nearest_xray_if_supported: When set, read in hits that don't stop
 * at the nearest surface. The hits must still be ordered by depth.
 * Needed so we can step to the next, non-active object when it's already selected, see: T76445.
 */
static int mixed_bones_object_selectbuffer(ViewContext *vc,
                                           GPUSelectResult *buffer,
                                           const int buffer_len,
                                           const int mval[2],
                                           eV3DSelectObjectFilter select_filter,
                                           bool do_nearest,
                                           bool do_nearest_xray_if_supported,
                                           const bool do_material_slot_selection)
{
  rcti rect;
  int hits15, hits9 = 0, hits5 = 0;
  bool has_bones15 = false, has_bones9 = false, has_bones5 = false;

  int select_mode = (do_nearest ? VIEW3D_SELECT_PICK_NEAREST : VIEW3D_SELECT_PICK_ALL);
  int hits = 0;

  if (do_nearest_xray_if_supported) {
    if ((U.gpu_flag & USER_GPU_FLAG_NO_DEPT_PICK) == 0) {
      select_mode = VIEW3D_SELECT_PICK_ALL;
    }
  }

  /* we _must_ end cache before return, use 'goto finally' */
  view3d_opengl_select_cache_begin();

  BLI_rcti_init_pt_radius(&rect, mval, 14);
  hits15 = view3d_opengl_select_ex(
      vc, buffer, buffer_len, &rect, select_mode, select_filter, do_material_slot_selection);
  if (hits15 == 1) {
    hits = selectbuffer_ret_hits_15(buffer, hits15);
    goto finally;
  }
  else if (hits15 > 0) {
    int ofs;
    has_bones15 = selectbuffer_has_bones(buffer, hits15);

    ofs = hits15;
    BLI_rcti_init_pt_radius(&rect, mval, 9);
    hits9 = view3d_opengl_select(
        vc, buffer + ofs, buffer_len - ofs, &rect, select_mode, select_filter);
    if (hits9 == 1) {
      hits = selectbuffer_ret_hits_9(buffer, hits15, hits9);
      goto finally;
    }
    else if (hits9 > 0) {
      has_bones9 = selectbuffer_has_bones(buffer + ofs, hits9);

      ofs += hits9;
      BLI_rcti_init_pt_radius(&rect, mval, 5);
      hits5 = view3d_opengl_select(
          vc, buffer + ofs, buffer_len - ofs, &rect, select_mode, select_filter);
      if (hits5 == 1) {
        hits = selectbuffer_ret_hits_5(buffer, hits15, hits9, hits5);
        goto finally;
      }
      else if (hits5 > 0) {
        has_bones5 = selectbuffer_has_bones(buffer + ofs, hits5);
      }
    }

    if (has_bones5) {
      hits = selectbuffer_ret_hits_5(buffer, hits15, hits9, hits5);
      goto finally;
    }
    else if (has_bones9) {
      hits = selectbuffer_ret_hits_9(buffer, hits15, hits9);
      goto finally;
    }
    else if (has_bones15) {
      hits = selectbuffer_ret_hits_15(buffer, hits15);
      goto finally;
    }

    if (hits5 > 0) {
      hits = selectbuffer_ret_hits_5(buffer, hits15, hits9, hits5);
      goto finally;
    }
    else if (hits9 > 0) {
      hits = selectbuffer_ret_hits_9(buffer, hits15, hits9);
      goto finally;
    }
    else {
      hits = selectbuffer_ret_hits_15(buffer, hits15);
      goto finally;
    }
  }

finally:
  view3d_opengl_select_cache_end();
  return hits;
}

static int mixed_bones_object_selectbuffer_extended(ViewContext *vc,
                                                    GPUSelectResult *buffer,
                                                    const int buffer_len,
                                                    const int mval[2],
                                                    eV3DSelectObjectFilter select_filter,
                                                    bool use_cycle,
                                                    bool enumerate,
                                                    bool *r_do_nearest)
{
  bool do_nearest = false;
  View3D *v3d = vc->v3d;

  /* define if we use solid nearest select or not */
  if (use_cycle) {
    /* Update the coordinates (even if the return value isn't used). */
    const bool has_motion = WM_cursor_test_motion_and_update(mval);
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

  int hits = mixed_bones_object_selectbuffer(
      vc, buffer, buffer_len, mval, select_filter, do_nearest, true, false);

  return hits;
}

/**
 * \param has_bones: When true, skip non-bone hits, also allow bases to be used
 * that are visible but not select-able,
 * since you may be in pose mode with an un-selectable object.
 *
 * \return the active base or NULL.
 */
static Base *mouse_select_eval_buffer(ViewContext *vc,
                                      const GPUSelectResult *buffer,
                                      int hits,
                                      Base *startbase,
                                      bool has_bones,
                                      bool do_nearest,
                                      int *r_sub_selection)
{
  ViewLayer *view_layer = vc->view_layer;
  View3D *v3d = vc->v3d;
  Base *base, *basact = NULL;
  int a;
  int sub_selection_id = 0;

  if (do_nearest) {
    uint min = 0xFFFFFFFF;
    int selcol = 0;

    if (has_bones) {
      /* we skip non-bone hits */
      for (a = 0; a < hits; a++) {
        if (min > buffer[a].depth && (buffer[a].id & 0xFFFF0000)) {
          min = buffer[a].depth;
          selcol = buffer[a].id & 0xFFFF;
          sub_selection_id = (buffer[a].id & 0xFFFF0000) >> 16;
        }
      }
    }
    else {
      int select_id_exclude = 0;
      /* Only exclude active object when it is selected. */
      if (BASACT(view_layer) && (BASACT(view_layer)->flag & BASE_SELECTED) && hits > 1) {
        select_id_exclude = BASACT(view_layer)->object->runtime.select_id;
      }

      /* Find the best active & non-active hits.
       * NOTE(@campbellbarton): Checking if `hits > 1` isn't a reliable way to know
       * if there are multiple objects selected since it's possible the same object
       * generates multiple hits, either from:
       * - Multiple sub-components (bones & camera tracks).
       * - Multiple selectable elements such as the object center and the geometry.
       *
       * For this reason, keep track of the best hit as well as the best hit that
       * excludes the selected & active object, using this value when it's valid. */

      uint min_not_active = min;
      int hit_index = -1, hit_index_not_active = -1;

      for (a = 0; a < hits; a++) {
        /* Any object. */
        if (min > buffer[a].depth) {
          min = buffer[a].depth;
          hit_index = a;
        }
        /* Any object other than the active-selected. */
        if (select_id_exclude != 0) {
          if (min_not_active > buffer[a].depth && select_id_exclude != (buffer[a].id & 0xFFFF)) {
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
        if (base->object->runtime.select_id == selcol) {
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
            /* skip non-bone objects */
            if (buffer[a].id & 0xFFFF0000) {
              if (base->object->runtime.select_id == (buffer[a].id & 0xFFFF)) {
                basact = base;
              }
            }
          }
          else {
            if (base->object->runtime.select_id == (buffer[a].id & 0xFFFF)) {
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

static Base *mouse_select_object_center(ViewContext *vc, Base *startbase, const int mval[2])
{
  ARegion *region = vc->region;
  ViewLayer *view_layer = vc->view_layer;
  View3D *v3d = vc->v3d;

  Base *oldbasact = BASACT(view_layer);

  const float mval_fl[2] = {(float)mval[0], (float)mval[1]};
  float dist = ED_view3d_select_dist_px() * 1.3333f;
  Base *basact = NULL;

  /* Put the active object at a disadvantage to cycle through other objects. */
  const float penalty_dist = 10.0f * UI_DPI_FAC;
  Base *base = startbase;
  while (base) {
    if (BASE_SELECTABLE(v3d, base)) {
      float screen_co[2];
      if (ED_view3d_project_float_global(
              region, base->object->obmat[3], screen_co, V3D_PROJ_TEST_CLIP_DEFAULT) ==
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

static Base *ed_view3d_give_base_under_cursor_ex(bContext *C,
                                                 const int mval[2],
                                                 int *r_material_slot)
{
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  ViewContext vc;
  Base *basact = NULL;
  GPUSelectResult buffer[MAXPICKELEMS];

  /* setup view context for argument to callbacks */
  view3d_operator_needs_opengl(C);
  BKE_object_update_select_id(CTX_data_main(C));

  ED_view3d_viewcontext_init(C, &vc, depsgraph);

  const bool do_nearest = !XRAY_ACTIVE(vc.v3d);
  const bool do_material_slot_selection = r_material_slot != NULL;
  const int hits = mixed_bones_object_selectbuffer(&vc,
                                                   buffer,
                                                   ARRAY_SIZE(buffer),
                                                   mval,
                                                   VIEW3D_SELECT_FILTER_NOP,
                                                   do_nearest,
                                                   false,
                                                   do_material_slot_selection);

  if (hits > 0) {
    const bool has_bones = (r_material_slot == NULL) && selectbuffer_has_bones(buffer, hits);
    basact = mouse_select_eval_buffer(&vc,
                                      buffer,
                                      hits,
                                      vc.view_layer->object_bases.first,
                                      has_bones,
                                      do_nearest,
                                      r_material_slot);
  }

  return basact;
}
