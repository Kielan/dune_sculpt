/** \file
 * \ingroup eduv
 *
 * \note The logic in this file closely follows `editmesh_path.cc`.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "BLI_linklist.h"
#include "DNA_windowmanager_types.h"
#include "MEM_guardedalloc.h"

#include "BLI_ghash.h"
#include "BLI_linklist_stack.h"
#include "BLI_math_vector.h"
#include "BLI_utildefines.h"

#include "DNA_image_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_space_types.h"

#include "BKE_context.hh"
#include "BKE_customdata.hh"
#include "BKE_editmesh.hh"
#include "BKE_layer.h"
#include "BKE_mesh.hh"
#include "BKE_report.h"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_query.hh"

#include "ED_object.hh"
#include "ED_screen.hh"
#include "ED_transform.hh"
#include "ED_uvedit.hh"

#include "RNA_access.hh"
#include "RNA_define.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "UI_view2d.hh"

#include "intern/bmesh_marking.hh"
#include "uvedit_intern.h"

#include "bmesh_tools.hh"

/* -------------------------------------------------------------------- */
/** \name Path Select Struct & Properties
 * \{ */

struct PathSelectParams {
  /** ensure the active element is the last selected item (handy for picking) */
  bool track_active;
  bool use_topology_distance;
  bool use_face_step;
  bool use_fill;
  CheckerIntervalParams interval_params;
};

struct UserData_UV {
  Scene *scene;
  BMEditMesh *em;
  BMUVOffsets offsets;
};

static void path_select_properties(wmOperatorType *ot)
{
  RNA_def_boolean(ot->srna,
                  "use_face_step",
                  false,
                  "Face Stepping",
                  "Traverse connected faces (includes diagonals and edge-rings)");
  RNA_def_boolean(ot->srna,
                  "use_topology_distance",
                  false,
                  "Topology Distance",
                  "Find the minimum number of steps, ignoring spatial distance");
  RNA_def_boolean(ot->srna,
                  "use_fill",
                  false,
                  "Fill Region",
                  "Select all paths between the source/destination elements");

  WM_operator_properties_checker_interval(ot, true);
}

static void path_select_params_from_op(wmOperator *op, PathSelectParams *op_params)
{
  op_params->track_active = false;
  op_params->use_face_step = RNA_boolean_get(op->ptr, "use_face_step");
  op_params->use_fill = RNA_boolean_get(op->ptr, "use_fill");
  op_params->use_topology_distance = RNA_boolean_get(op->ptr, "use_topology_distance");
  WM_operator_properties_checker_interval_from_op(op, &op_params->interval_params);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name UV Vert Path
 * \{ */

/* callbacks */
static bool verttag_filter_cb(BMLoop *l, void *user_data_v)
{
  UserData_UV *user_data = static_cast<UserData_UV *>(user_data_v);
  return uvedit_face_visible_test(user_data->scene, l->f);
}
static bool verttag_test_cb(BMLoop *l, void *user_data_v)
{
  /* All connected loops are selected or we return false. */
  UserData_UV *user_data = static_cast<UserData_UV *>(user_data_v);
  const Scene *scene = user_data->scene;
  const int cd_loop_uv_offset = user_data->offsets.uv;
  const float *luv = BM_ELEM_CD_GET_FLOAT_P(l, cd_loop_uv_offset);
  BMIter iter;
  BMLoop *l_iter;
  BM_ITER_ELEM (l_iter, &iter, l->v, BM_LOOPS_OF_VERT) {
    if (verttag_filter_cb(l_iter, user_data)) {
      const float *luv_iter = BM_ELEM_CD_GET_FLOAT_P(l_iter, cd_loop_uv_offset);
      if (equals_v2v2(luv, luv_iter)) {
        if (!uvedit_uv_select_test(scene, l_iter, user_data->offsets)) {
          return false;
        }
      }
    }
  }
  return true;
}
static void verttag_set_cb(BMLoop *l, bool val, void *user_data_v)
{
  UserData_UV *user_data = static_cast<UserData_UV *>(user_data_v);
  const Scene *scene = user_data->scene;
  BMEditMesh *em = user_data->em;
  const uint cd_loop_uv_offset = user_data->offsets.uv;
  const float *luv = BM_ELEM_CD_GET_FLOAT_P(l, cd_loop_uv_offset);
  BMIter iter;
  BMLoop *l_iter;
  BM_ITER_ELEM (l_iter, &iter, l->v, BM_LOOPS_OF_VERT) {
    if (verttag_filter_cb(l_iter, user_data)) {
      const float *luv_iter = BM_ELEM_CD_GET_FLOAT_P(l_iter, cd_loop_uv_offset);
      if (equals_v2v2(luv, luv_iter)) {
        uvedit_uv_select_set(scene, em->bm, l_iter, val, false, user_data->offsets);
      }
    }
  }
}

static int mouse_mesh_uv_shortest_path_vert(Scene *scene,
                                            Object *obedit,
                                            const PathSelectParams *op_params,
                                            BMLoop *l_src,
                                            BMLoop *l_dst,
                                            const float aspect_y,
                                            const BMUVOffsets offsets)
{
  BMEditMesh *em = BKE_editmesh_from_object(obedit);
  BMesh *bm = em->bm;
  int flush = 0;

  UserData_UV user_data = {};
  user_data.scene = scene;
  user_data.em = em;
  user_data.offsets = offsets;

  BMCalcPathUVParams params{};
  params.use_topology_distance = op_params->use_topology_distance;
  params.use_step_face = op_params->use_face_step;
  params.aspect_y = aspect_y;
  params.cd_loop_uv_offset = offsets.uv;

  LinkNode *path = nullptr;
  bool is_path_ordered = false;

  if (l_src != l_dst) {
    if (op_params->use_fill) {
      path = BM_mesh_calc_path_uv_region_vert(bm,
                                              (BMElem *)l_src,
                                              (BMElem *)l_dst,
                                              params.cd_loop_uv_offset,
                                              verttag_filter_cb,
                                              &user_data);
    }
    else {
      is_path_ordered = true;
      path = BM_mesh_calc_path_uv_vert(bm, l_src, l_dst, &params, verttag_filter_cb, &user_data);
    }
  }

  BMLoop *l_dst_last = l_dst;

  if (path) {
    /* toggle the flag */
    bool all_set = true;
    LinkNode *node = path;
    do {
      if (!verttag_test_cb((BMLoop *)node->link, &user_data)) {
        all_set = false;
        break;
      }
    } while ((node = node->next));

    int depth = -1;
    node = path;
    do {
      if ((is_path_ordered == false) ||
          WM_operator_properties_checker_interval_test(&op_params->interval_params, depth))
      {
        verttag_set_cb((BMLoop *)node->link, !all_set, &user_data);
        if (is_path_ordered) {
          l_dst_last = static_cast<BMLoop *>(node->link);
        }
      }
    } while ((void)depth++, (node = node->next));

    BLI_linklist_free(path, nullptr);
    flush = all_set ? -1 : 1;
  }
  else {
    const bool is_act = !verttag_test_cb(l_dst, &user_data);
    verttag_set_cb(l_dst, is_act, &user_data); /* switch the face option */
  }

  if (op_params->track_active) {
    ED_uvedit_active_vert_loop_set(bm, l_dst_last);
  }
  return flush;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name UV Edge Path
 * \{ */

/* callbacks */
static bool edgetag_filter_cb(BMLoop *l, void *user_data_v)
{
  UserData_UV *user_data = static_cast<UserData_UV *>(user_data_v);
  return uvedit_face_visible_test(user_data->scene, l->f);
}
static bool edgetag_test_cb(BMLoop *l, void *user_data_v)
{
  /* All connected loops (UV) are selected or we return false. */
  UserData_UV *user_data = static_cast<UserData_UV *>(user_data_v);
  const Scene *scene = user_data->scene;
  BMIter iter;
  BMLoop *l_iter;
  BM_ITER_ELEM (l_iter, &iter, l->e, BM_LOOPS_OF_EDGE) {
    if (edgetag_filter_cb(l_iter, user_data)) {
      if (BM_loop_uv_share_edge_check(l, l_iter, user_data->offsets.uv)) {
        if (!uvedit_edge_select_test(scene, l_iter, user_data->offsets)) {
          return false;
        }
      }
    }
  }
  return true;
}
static void edgetag_set_cb(BMLoop *l, bool val, void *user_data_v)
{
  UserData_UV *user_data = static_cast<UserData_UV *>(user_data_v);
  const Scene *scene = user_data->scene;
  BMEditMesh *em = user_data->em;
  uvedit_edge_select_set_with_sticky(scene, em, l, val, false, user_data->offsets);
}

static int mouse_mesh_uv_shortest_path_edge(Scene *scene,
                                            Object *obedit,
                                            const PathSelectParams *op_params,
                                            BMLoop *l_src,
                                            BMLoop *l_dst,
                                            const float aspect_y,
                                            const BMUVOffsets offsets)
{
  BMEditMesh *em = BKE_editmesh_from_object(obedit);
  BMesh *bm = em->bm;
  int flush = 0;

  UserData_UV user_data = {};
  user_data.scene = scene;
  user_data.em = em;
  user_data.offsets = offsets;

  BMCalcPathUVParams params = {};
  params.use_topology_distance = op_params->use_topology_distance;
  params.use_step_face = op_params->use_face_step;
  params.aspect_y = aspect_y;
  params.cd_loop_uv_offset = offsets.uv;

  LinkNode *path = nullptr;
  bool is_path_ordered = false;

  if (l_src != l_dst) {
    if (op_params->use_fill) {
      path = BM_mesh_calc_path_uv_region_edge(bm,
                                              (BMElem *)l_src,
                                              (BMElem *)l_dst,
                                              params.cd_loop_uv_offset,
                                              edgetag_filter_cb,
                                              &user_data);
    }
    else {
      is_path_ordered = true;
      path = BM_mesh_calc_path_uv_edge(bm, l_src, l_dst, &params, edgetag_filter_cb, &user_data);
    }
  }

  BMLoop *l_dst_last = l_dst;

  if (path) {
    /* toggle the flag */
    bool all_set = true;
    LinkNode *node = path;
    do {
      if (!edgetag_test_cb((BMLoop *)node->link, &user_data)) {
        all_set = false;
        break;
      }
    } while ((node = node->next));

    int depth = -1;
    node = path;
    do {
      if ((is_path_ordered == false) ||
          WM_operator_properties_checker_interval_test(&op_params->interval_params, depth))
      {
        edgetag_set_cb((BMLoop *)node->link, !all_set, &user_data);
        if (is_path_ordered) {
          l_dst_last = static_cast<BMLoop *>(node->link);
        }
      }
    } while ((void)depth++, (node = node->next));

    BLI_linklist_free(path, nullptr);
    flush = all_set ? -1 : 1;
  }
  else {
    const bool is_act = !edgetag_test_cb(l_dst, &user_data);
    edgetag_set_cb(l_dst, is_act, &user_data); /* switch the face option */
  }

  if (op_params->track_active) {
    ED_uvedit_active_edge_loop_set(bm, l_dst_last);
  }
  return flush;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name UV Face Path
 * \{ */

/* callbacks */
static bool facetag_filter_cb(BMFace *f, void *user_data_v)
{
  UserData_UV *user_data = static_cast<UserData_UV *>(user_data_v);
  return uvedit_face_visible_test(user_data->scene, f);
}
static bool facetag_test_cb(BMFace *f, void *user_data_v)
{
  /* All connected loops are selected or we return false. */
  UserData_UV *user_data = static_cast<UserData_UV *>(user_data_v);
  const Scene *scene = user_data->scene;
  BMIter iter;
  BMLoop *l_iter;
  BM_ITER_ELEM (l_iter, &iter, f, BM_LOOPS_OF_FACE) {
    if (!uvedit_edge_select_test(scene, l_iter, user_data->offsets)) {
      return false;
    }
  }
  return true;
}
static void facetag_set_cb(BMFace *f, bool val, void *user_data_v)
{
  UserData_UV *user_data = static_cast<UserData_UV *>(user_data_v);
  const Scene *scene = user_data->scene;
  BMEditMesh *em = user_data->em;
  uvedit_face_select_set_with_sticky(scene, em, f, val, false, user_data->offsets);
}

static int mouse_mesh_uv_shortest_path_face(Scene *scene,
                                            Object *obedit,
                                            const PathSelectParams *op_params,
                                            BMFace *f_src,
                                            BMFace *f_dst,
                                            const float aspect_y,
                                            const BMUVOffsets offsets)
{
  BMEditMesh *em = BKE_editmesh_from_object(obedit);
  BMesh *bm = em->bm;
  int flush = 0;

  UserData_UV user_data = {};
  user_data.scene = scene;
  user_data.em = em;
  user_data.offsets = offsets;

  BMCalcPathUVParams params = {};
  params.use_topology_distance = op_params->use_topology_distance;
  params.use_step_face = op_params->use_face_step;
  params.aspect_y = aspect_y;
  params.cd_loop_uv_offset = offsets.uv;

  LinkNode *path = nullptr;
  bool is_path_ordered = false;

  if (f_src != f_dst) {
    if (op_params->use_fill) {
      path = BM_mesh_calc_path_uv_region_face(bm,
                                              (BMElem *)f_src,
                                              (BMElem *)f_dst,
                                              params.cd_loop_uv_offset,
                                              facetag_filter_cb,
                                              &user_data);
    }
    else {
      is_path_ordered = true;
      path = BM_mesh_calc_path_uv_face(bm, f_src, f_dst, &params, facetag_filter_cb, &user_data);
    }
  }

  BMFace *f_dst_last = f_dst;

  if (path) {
    /* toggle the flag */
    bool all_set = true;
    LinkNode *node = path;
    do {
      if (!facetag_test_cb((BMFace *)node->link, &user_data)) {
        all_set = false;
        break;
      }
    } while ((node = node->next));

    int depth = -1;
    node = path;
    do {
      if ((is_path_ordered == false) ||
          WM_operator_properties_checker_interval_test(&op_params->interval_params, depth))
      {
        facetag_set_cb((BMFace *)node->link, !all_set, &user_data);
        if (is_path_ordered) {
          f_dst_last = static_cast<BMFace *>(node->link);
        }
      }
    } while ((void)depth++, (node = node->next));

    BLI_linklist_free(path, nullptr);
    flush = all_set ? -1 : 1;
  }
  else {
    const bool is_act = !facetag_test_cb(f_dst, &user_data);
    facetag_set_cb(f_dst, is_act, &user_data); /* switch the face option */
  }

  if (op_params->track_active) {
    /* Unlike other types, we can track active without it being selected. */
    BM_mesh_active_face_set(bm, f_dst_last);
  }
  return flush;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Main Operator for vert/edge/face tag
 * \{ */

static int uv_shortest_path_pick_exec(bContext *C, wmOperator *op);

static bool uv_shortest_path_pick_ex(Scene *scene,
                                     Depsgraph *depsgraph,
                                     Object *obedit,
                                     const PathSelectParams *op_params,
                                     BMElem *ele_src,
                                     BMElem *ele_dst,
                                     const float aspect_y,
                                     const BMUVOffsets offsets)
{
  const ToolSettings *ts = scene->toolsettings;
  const char uv_selectmode = ED_uvedit_select_mode_get(scene);
  bool ok = false;
  int flush = 0;

  if (ELEM(nullptr, ele_src, ele_dst) || (ele_src->head.htype != ele_dst->head.htype)) {
    /* pass */
  }
  else if (ele_src->head.htype == BM_FACE) {
    flush = mouse_mesh_uv_shortest_path_face(
        scene, obedit, op_params, (BMFace *)ele_src, (BMFace *)ele_dst, aspect_y, offsets);
    ok = true;
  }
  else if (ele_src->head.htype == BM_LOOP) {
    if (uv_selectmode & UV_SELECT_EDGE) {
      flush = mouse_mesh_uv_shortest_path_edge(
          scene, obedit, op_params, (BMLoop *)ele_src, (BMLoop *)ele_dst, aspect_y, offsets);
    }
    else {
      flush = mouse_mesh_uv_shortest_path_vert(
          scene, obedit, op_params, (BMLoop *)ele_src, (BMLoop *)ele_dst, aspect_y, offsets);
    }
    ok = true;
  }

  if (ok) {
    if (flush != 0) {
      const bool select = (flush == 1);
      BMEditMesh *em = BKE_editmesh_from_object(obedit);
      if (ts->uv_flag & UV_SYNC_SELECTION) {
        ED_uvedit_select_sync_flush(scene->toolsettings, em, select);
      }
      else {
        ED_uvedit_selectmode_flush(scene, em);
      }
    }

    if (ts->uv_flag & UV_SYNC_SELECTION) {
      DEG_id_tag_update(static_cast<ID *>(obedit->data), ID_RECALC_SELECT);
    }
    else {
      Object *obedit_eval = DEG_get_evaluated_object(depsgraph, obedit);
      BKE_mesh_batch_cache_dirty_tag(static_cast<Mesh *>(obedit_eval->data),
                                     BKE_MESH_BATCH_DIRTY_UVEDIT_SELECT);
    }
    /* Only for region redraw. */
    WM_main_add_notifier(NC_GEOM | ND_SELECT, obedit->data);
  }

  return ok;
}

static int uv_shortest_path_pick_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  Scene *scene = CTX_data_scene(C);
  const ToolSettings *ts = scene->toolsettings;
  const char uv_selectmode = ED_uvedit_select_mode_get(scene);

  /* We could support this, it needs further testing. */
  if (RNA_struct_property_is_set(op->ptr, "index")) {
    return uv_shortest_path_pick_exec(C, op);
  }

  PathSelectParams op_params;
  path_select_params_from_op(op, &op_params);

  /* Set false if we support edge tagging. */
  op_params.track_active = true;

  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data_with_uvs(
      scene, view_layer, nullptr, &objects_len);

  float co[2];

  const ARegion *region = CTX_wm_region(C);

  UI_view2d_region_to_view(&region->v2d, event->mval[0], event->mval[1], &co[0], &co[1]);

  BMElem *ele_src = nullptr, *ele_dst = nullptr;

  /* Detect the hit. */
  UvNearestHit hit = uv_nearest_hit_init_max(&region->v2d);
  bool hit_found = false;
  if (uv_selectmode == UV_SELECT_FACE) {
    if (uv_find_nearest_face_multi(scene, objects, objects_len, co, &hit)) {
      hit_found = true;
    }
  }
  else if (uv_selectmode & UV_SELECT_EDGE) {
    if (uv_find_nearest_edge_multi(scene, objects, objects_len, co, 0.0f, &hit)) {
      hit_found = true;
    }
  }
  else {
    if (uv_find_nearest_vert_multi(scene, objects, objects_len, co, 0.0f, &hit)) {
      hit_found = true;
    }
  }

  bool changed = false;
  if (hit_found) {
    /* This may not be the active object. */
    Ob *obedit = hit.ob;
    MeshEdit *em = BKE_editmesh_from_object(obedit);
    Mesh *mesh = em->bm;
    const MeshUVOffsets offsets = BM_uv_map_get_offsets(bm);

    /* Respond to the hit. */
    if (uv_selectmode == UV_SEL_FACE) {
      /* Face selection. */
      MeshFace *f_src = mesh_active_face_get(mesh, false, false);
      /* Check sel? */
      ele_src = (MeshElem *)f_src;
      ele_dst = (MeshElem *)hit.efa;
    }
    else if (uv_selmode & UV_SEL_EDGE) {
      /* Edge selection. */
      MesgLoop *l_src = nullptr;
      if (ts->uv_flag & UV_SYNC_SEL) {
        MeshEdge *e_src = mesh_active_edge_get(bm);
        if (e_src != nullptr) {
          l_src = uv_find_nearest_loop_from_edge(scene, obedit, e_src, co);
        }
      }
      else {
        l_src = ed_uvedit_active_edge_loop_get(mesh);
        if (l_src != nullptr) {
          if (!uvedit_uv_sel_test(scene, l_src, offsets) &&
              !uvedit_uv_sel_test(scene, l_src->next, offsets))
          {
            l_src = nullptr;
          }
          ele_src = (MeshElem *)l_src;
        }
      }
      ele_src = (MeshElem *)l_src;
      ele_dst = (MeshElem *)hit.l;
    }
    else {
      /* Vertex sel. */
      MeshLoop *l_src = nullptr;
      if (ts->uv_flag & UV_SYNC_SELECTION) {
        MeshVert *v_src = mesh_active_vert_get(mesh);
        if (v_src != nullptr) {
          l_src = uv_find_nearest_loop_from_vert(scene, obedit, v_src, co);
        }
      }
      else {
        l_src = ed_uvedit_active_vert_loop_get(mesh);
        if (l_src != nullptr) {
          if (!uvedit_uv_sel_test(scene, l_src, offsets)) {
            l_src = nullptr;
          }
        }
      }
      ele_src = (MeshElem *)l_src;
      ele_dst = (MeshElem *)hit.l;
    }

    if (ele_src && ele_dst) {
      /* Always use the active ob, not `obedit` as the active defines the UV display. */
      const float aspect_y = ed_uvedit_get_aspect_y(cxt_data_edit_ob(C));
      uv_shortest_path_pick_ex(
          scene, graph, obedit, &op_params, ele_src, ele_dst, aspect_y, offsets);

      /* Store the ob and it's index so redo is possible. */
      int index;
      if (uv_selmode & UV_SEL_FACE) {
        mesh_elem_index_ensure(mesh, MESH_FACE);
        index = mesh_elem_index_get(ele_dst);
      }
      else if (uv_selmode & UV_SEL_EDGE) {
        mesh_elem_index_ensure(mesh, mesh_LOOP);
        index = mesh_elem_index_get(ele_dst);
      }
      else {
        mesh_elem_index_ensure(mesh, MESH_LOOP);
        index = mesh_elem_index_get(ele_dst);
      }

      const int ob_index = ed_ob_in_mode_to_index(scene, view_layer, OB_MODE_EDIT, obedit);
      lib_assert(ob_index != -1);
      api_int_set(op->ptr, "ob_index", ob_index);
      api_int_set(op->ptr, "index", index);
      changed = true;
    }
  }

mem_free(obs);

  return changed ? OP_FINISHED : OP_CANCELLED;
}

static int uv_shortest_path_pick_ex(Cxt *C, WinOp *op)
{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  const char uv_selmode = ed_uvedit_sel_mode_get(scene);

  const int object_index = api_int_get(op->ptr, "object_index");
  const int index = api_int_get(op->ptr, "index");
  if (object_index == -1) {
    return OP_CANCELLED;
  }

  Ob *obedit = ed_ob_in_mode_from_index(scene, view_layer, OB_MODE_EDIT, object_index);
  if (obedit == nullptr) {
    return OP_CANCELLED;
  }

  MeshEdit *em = dune_meshedit_from_ob(obedit);
  Mesh *mesh = mesh->mesh;
  const MeshUVOffsets offsets = mesh_uv_map_get_offsets(bm);

  MeshElem *ele_src, *ele_dst;

  /* NOLINTBEGIN: bugprone-assignment-in-if-condition */
  if (uv_selmode & UV_SEL_FACE) {
    if (index < 0 || index >= mesh->totface) {
      return OP_CANCELLED;
    }
    if (!(ele_src = (MeshElem *)mesh_active_face_get(mesh, false, false)) ||
        !(ele_dst = (MeshElem *)mesh_face_at_index_find_or_table(mesh, index)))
    {
      return OP_CANCELLED;
    }
  }
  else if (uv_selmode & UV_SEL_EDGE) {
    if (index < 0 || index >= mesh->totloop) {
      return OP_CANCELLED;
    }
    if (!(ele_src = (MeshElem *)ed_uvedit_active_edge_loop_get(mesh)) ||
        !(ele_dst = (MeshElem *)mesh_loop_at_index_find(mewh, index)))
    {
      return OP_CANCELLED;
    }
  }
  else {
    if (index < 0 || index >= mesh->totloop) {
      return OP_CANCELLED;
    }
    if (!(ele_src = (MeshElem *)ed_uvedit_active_vert_loop_get(mesh)) ||
        !(ele_dst = (MeshElem *)mesh_loop_at_index_find(mesh, index)))
    {
      return OP_CANCELLED;
    }
  }
  /* NOLINTEND: bugprone-assignment-in-if-condition */
  /* Always use the active ob, not `obedit` as the active defines the UV display. */
  const float aspect_y = ed_uvedit_get_aspect_y(cxt_data_edit_ob(C));

  PathSelParams op_params;
  path_sel_params_from_op(op, &op_params);
  op_params.track_active = true;

  if (!uv_shortest_path_pick_ex(
          scene, graph, obedit, &op_params, ele_src, ele_dst, aspect_y, offsets))
  {
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

void UV_OT_shortest_path_pick(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Pick Shortest Path";
  ot->idname = "UV_OT_shortest_path_pick";
  ot->description = "Sel shortest path between two sels";

  /* api cbs */
  ot->invoke = uv_shortest_path_pick_invoke;
  ot->ex = uv_shortest_path_pick_ex;
  ot->poll = ed_op_uvedit_space_img;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  path_sel_props(ot);

  /* use for redo */
  prop = api_def_int(ot->sapi, "ob_index", -1, -1, INT_MAX, "", "", 0, INT_MAX);
  api_def_prop_flag(prop, PropFlag(PROP_HIDDEN | PROP_SKIP_SAVE));
  prop = api_def_int(ot->sapi, "index", -1, -1, INT_MAX, "", "", 0, INT_MAX);
  api_def_prop_flag(prop, PropFlag(PROP_HIDDEN | PROP_SKIP_SAVE));
}

/* Sel Path Between Existing Sel */
static int uv_shortest_path_sel_ex(Cxt *C, WinOp *op)
{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  Scene *scene = cxt_data_scene(C);
  const char uv_selmode = ed_uvedit_sel_mode_get(scene);
  bool found_valid_elements = false;

  const float aspect_y = ed_uvedit_get_aspect_y(ed_data_edit_ob(C));

  ViewLayer *view_layer = cxt_data_view_layer(C);
  uint obs_len = 0;
  Ob **obs = dune_view_layer_arr_from_obs_in_edit_mode_unique_data_w_uvs(
      scene, view_layer, nullptr, &obs_len);
  for (uint ob_index = 0; ob_index < obs_len; ob_index++) {
    Ob *obedit = obs[ob_index];
    MeshEdit *em = dune_meshedit_from_ob(obedit);
    Mesh *mesh = mesh->mesh;

    const MeshUVOffsets offsets = mesh_uv_map_get_offsets(bm);

    MeshElem *ele_src = nullptr, *ele_dst = nullptr;

    /* Find 2x elements. */
    {
      MeshElem **ele_arr = nullptr;
      int ele_array_len = 0;
      if (uv_selmode & UV_SEL_FACE) {
        ele_arr = MeshMElem **)ed_uvedit_sel_faces(scene, mesh, 3, &ele_arr_len);
      }
      else if (uv_selectmode & UV_SELECT_EDGE) {
        ele_array = (MeshElem **)ed_uvedit_sel_edges(scene, mesh, 3, &ele_arr_len);
      }
      else {
        ele_array = (MeshElem **)ed_uvedit_sel_verts(scene, mesh, 3, &ele_arr_len);
      }

      if (ele_array_len == 2) {
        ele_src = ele_array[0];
        ele_dst = ele_array[1];
      }
      mem_free(ele_array);
    }

    if (ele_src && ele_dst) {
      PathSelParams op_params;
      path_sel_params_from_op(op, &op_params);

      uv_shortest_path_pick_ex(
          scene, depsgraph, obedit, &op_params, ele_src, ele_dst, aspect_y, offsets);

      found_valid_elements = true;
    }
  }
  mem_free(obs);

  if (!found_valid_elements) {
    dune_report(
        op->reports, RPT_WARNING, "Path sel requires two matching elements to be selected");
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

void UV_OT_shortest_path_sel(WinOpType *ot)
{
  /* ids */
  ot->name = "Sel Shortest Path";
  ot->idname = "UV_OT_shortest_path_sel";
  ot->description = "Sel shortest path between two vertices/edges/faces";

  /* api cbs */
  ot->ex = uv_shortest_path_sel_ex;
  ot->poll = ed_op_uvedit_space_img;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  path_select_properties(ot);
}

/** \} */
