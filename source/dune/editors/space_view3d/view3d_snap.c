#include "mem_guardedalloc.h"

#include "types_armature.h"
#include "types_ob.h"

#include "lib_array.h"
#include "lib_math.h"
#include "lib_utildefines.h"

#include "dune_action.h"
#include "dune_armature.h"
#include "dune_context.h"
#include "dune_editmesh.h"
#include "dune_layer.h"
#include "dune_main.h"
#include "dune_mball.h"
#include "dune_ob.h"
#include "dune_report.h"
#include "dune_scene.h"
#include "dune_tracking.h"

#include "graph.h"
#include "graph_query.h"

#include "win_api.h"
#include "win_types.h"

#include "api_access.h"
#include "api_define.h"

#include "ed_keyframing.h"
#include "ed_ob.h"
#include "ed_screen.h"
#include "ed_transverts.h"

#include "view3d_intern.h"

static bool snap_curs_to_sel_ex(Cxt *C, const int pivot_point, float r_cursor[3]);
static bool snap_calc_active_center(Cxt *C, const bool select_only, float r_center[3]);

/* Snap Sel to Grid Op */
/* Snaps every individual ob center to its nearest point on the grid. */
static int snap_sel_to_grid_ex(Cxt *C, WinOp *UNUSED(op))
{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  ViewLayer *view_layer_eval = graph_get_eval_view_layer(graph);
  Ob *obact = cxt_data_active_ob(C);
  Scene *scene = cxt_data_scene(C);
  ARgn *rgn = cxt_win_rgn(C);
  View3D *v3d = cxt_win_view3d(C);
  TransVertStore tvs = {NULL};
  TransVert *tv;
  float gridf, imat[3][3], bmat[3][3], vec[3];
  int a;

  gridf = ed_view3d_grid_view_scale(scene, v3d, rgn, NULL);

  if (OBEDIT_FROM_OBACT(obact)) {
    ViewLayer *view_layer = cxt_data_view_layer(C);
    uint objs_len = 0;
    Ob **objs = dune_view_layer_array_from_objs_in_edit_mode_unique_data(
        view_layer, cxt_win_view3d(C), &objs_len);
    for (uint ob_index = 0; ob_index < objs_len; ob_index++) {
      Ob *obedit = objs[ob_index];

      if (obedit->type == OB_MESH) {
        MeshEdit *em = dune_meshedit_from_ob(obedit);

        if (em->mesh->totvertsel == 0) {
          continue;
        }
      }

      if (ed_transverts_check_obedit(obedit)) {
        ed_transverts_create_from_obedit(&tvs, obedit, 0);
      }

      if (tvs.transverts_tot != 0) {
        copy_m3_m4(bmat, obedit->obmat);
        invert_m3_m3(imat, bmat);

        tv = tvs.transverts;
        for (a = 0; a < tvs.transverts_tot; a++, tv++) {
          copy_v3_v3(vec, tv->loc);
          mul_m3_v3(bmat, vec);
          add_v3_v3(vec, obedit->obmat[3]);
          vec[0] = gridf * floorf(0.5f + vec[0] / gridf);
          vec[1] = gridf * floorf(0.5f + vec[1] / gridf);
          vec[2] = gridf * floorf(0.5f + vec[2] / gridf);
          sub_v3_v3(vec, obedit->obmat[3]);

          mul_m3_v3(imat, vec);
          copy_v3_v3(tv->loc, vec);
        }
        ed_transverts_update_obedit(&tvs, obedit);
      }
      ed_transverts_free(&tvs);
    }
    mem_free(objs);
  }
  else if (OBPOSE_FROM_OBACT(obact)) {
    struct KeyingSet *ks = anim_get_keyingset_for_autokeying(scene, ANIM_KS_LOCATION_ID);
    uint objs_len = 0;
    Ob **objs_eval = dune_ob_pose_array_get(view_layer_eval, v3d, &objs_len);
    for (uint ob_index = 0; ob_index < objs_len; ob_index++) {
      Ob *ob_eval = objs_eval[ob_index];
      Ob *ob = graph_get_original_ob(ob_eval);
      PoseChannel *pchan_eval;
      Armature *arm_eval = ob_eval->data;

      invert_m4_m4(ob_eval->imat, ob_eval->obmat);

      for (pchan_eval = ob_eval->pose->chanbase.first; pchan_eval; pchan_eval = pchan_eval->next) {
        if (pchan_eval->bone->flag & BONE_SELECTED) {
          if (pchan_eval->bone->layer & arm_eval->layer) {
            if ((pchan_eval->bone->flag & BONE_CONNECTED) == 0) {
              float nLoc[3];

              /* get nearest grid point to snap to */
              copy_v3_v3(nLoc, pchan_eval->pose_mat[3]);
              /* We must operate in world space! */
              mul_m4_v3(ob_eval->obmat, nLoc);
              vec[0] = gridf * floorf(0.5f + nLoc[0] / gridf);
              vec[1] = gridf * floorf(0.5f + nLoc[1] / gridf);
              vec[2] = gridf * floorf(0.5f + nLoc[2] / gridf);
              /* Back in object space... */
              mul_m4_v3(ob_eval->imat, vec);

              /* Get location of grid point in pose space. */
              dune_armature_loc_pose_to_bone(pchan_eval, vec, vec);

              /* Adjust location on the original pchan. */
              PoseChannel *pchan = dune_pose_channel_find_name(ob->pose, pchan_eval->name);
              if ((pchan->protectflag & OB_LOCK_LOCX) == 0) {
                pchan->loc[0] = vec[0];
              }
              if ((pchan->protectflag & OB_LOCK_LOCY) == 0) {
                pchan->loc[1] = vec[1];
              }
              if ((pchan->protectflag & OB_LOCK_LOCZ) == 0) {
                pchan->loc[2] = vec[2];
              }

              /* auto-keyframing */
              ed_autokeyframe_pchan(C, scene, ob, pchan, ks);
            }
            /* if the bone has a parent and is connected to the parent,
             * don't do anything - will break chain unless we do auto-ik.
             */
          }
        }
      }
      ob->pose->flag |= (POSE_LOCKED | POSE_DO_UNLOCK);

      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    }
    mem_free(objs_eval);
  }
  else {
    /* Ob mode */
    Main *main = cxt_data_main(C);

    struct KeyingSet *ks = anim_get_keyingset_for_autokeying(scene, ANIM_KS_LOCATION_ID);

    const bool use_transform_skip_children = (scene->toolsettings->transform_flag &
                                              SCE_XFORM_SKIP_CHILDREN);
    const bool use_transform_data_origin = (scene->toolsettings->transform_flag &
                                            SCE_XFORM_DATA_ORIGIN);
    struct XFormObSkipChild_Container *xcs = NULL;
    struct XFormObData_Container *xds = NULL;

    /* Build ob array. */
    Ob **objs_eval = NULL;
    uint objs_eval_len;
    {
      lib_array_declare(objs_eval);
      FOREACH_SELECTED_EDITABLE_OB_BEGIN (view_layer_eval, v3d, ob_eval) {
        lib_array_append(objects_eval, ob_eval);
      }
      FOREACH_SELECTED_EDITABLE_OB_END;
      objects_eval_len = BLI_array_len(objects_eval);
    }

    if (use_transform_skip_children) {
      ViewLayer *view_layer = cxt_data_view_layer(C);

      Ob **objs = mem_malloc_array(objs_eval_len, sizeof(*objs), __func__);

      for (int ob_index = 0; ob_index < objs_eval_len; ob_index++) {
        Ob *ob_eval = objs_eval[ob_index];
        objs[ob_index] = grah_get_original_ob(ob_eval);
      }
      dune_scene_graph_eval_ensure(graph, main);
      xcs = ed_ob_xform_skip_child_container_create();
      ed_ob_xform_skip_child_container_item_ensure_from_array(
          xcs, view_layer, objs, objs_eval_len);
      mem_free(objs);
    }
    if (use_transform_data_origin) {
      dune_scene_graph_eval_ensure(graph, main);
      xds = ed_ob_data_xform_container_create();
    }

    for (int ob_index = 0; ob_index < objects_eval_len; ob_index++) {
      Ob *ob_eval = objs_eval[ob_index];
      Ob *ob = graph_get_original_ob(ob_eval);
      vec[0] = -ob_eval->obmat[3][0] + gridf * floorf(0.5f + ob_eval->obmat[3][0] / gridf);
      vec[1] = -ob_eval->obmat[3][1] + gridf * floorf(0.5f + ob_eval->obmat[3][1] / gridf);
      vec[2] = -ob_eval->obmat[3][2] + gridf * floorf(0.5f + ob_eval->obmat[3][2] / gridf);

      if (ob->parent) {
        float originmat[3][3];
        dune_ob_where_is_calc_ex(graph, scene, NULL, ob, originmat);

        invert_m3_m3(imat, originmat);
        mul_m3_v3(imat, vec);
      }
      if ((ob->protectflag & OB_LOCK_LOCX) == 0) {
        ob->loc[0] = ob_eval->loc[0] + vec[0];
      }
      if ((ob->protectflag & OB_LOCK_LOCY) == 0) {
        ob->loc[1] = ob_eval->loc[1] + vec[1];
      }
      if ((ob->protectflag & OB_LOCK_LOCZ) == 0) {
        ob->loc[2] = ob_eval->loc[2] + vec[2];
      }

      /* auto-keyframing */
      ed_autokeyframe_ob(C, scene, ob, ks);

      if (use_transform_data_origin) {
        ed_ob_data_xform_container_item_ensure(xds, ob);
      }

      graph_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);
    }

    if (objects_eval) {
      mem_free(objs_eval);
    }

    if (use_transform_skip_children) {
      ed_ob_xform_skip_child_container_update_all(xcs, main, graph);
      ed_ob_xform_skip_child_container_destroy(xcs);
    }
    if (use_transform_data_origin) {
      ed_ob_data_xform_container_update_all(xds, main, graph);
      ed_ob_data_xform_container_destroy(xds);
    }
  }

  win_ev_add_notifier(C, NC_OBJECT | ND_TRANSFORM, NULL);

  return OP_FINISHED;
}

void VIEW3D_OT_snap_selected_to_grid(WinOpType *ot)
{
  /* ids */
  ot->name = "Snap Selection to Grid";
  ot->description = "Snap selected item(s) to their nearest grid division";
  ot->idname = "VIEW3D_OT_snap_selected_to_grid";

  /* api cbs */
  ot->ex = snap_sel_to_grid_ex;
  ot->poll = ed_op_rgn_view3d_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Snap Selection to Location (Util) */

/* Snaps the selection as a whole (use_offset=true) or each selected object to the given location.
 *
 * param snap_target_global: a location in global space to snap to
 * (eg. 3D cursor or active ob).
 * param use_offset: if the selected objs should maintain their relative offsets
 * and be snapped by the sel pivot point (median, active),
 * or if every object origin should be snapped to the given location. */
static bool snap_selected_to_location(Cxt *C,
                                      const float snap_target_global[3],
                                      const bool use_offset,
                                      const int pivot_point,
                                      const bool use_toolsettings)
{
  Scene *scene = cxt_data_scene(C);
  Ob *obedit = cxt_data_edit_ob(C);
  Ob *obact = cxt_data_active_ob(C);
  View3D *v3d = cxt_win_view3d(C);
  TransVertStore tvs = {NULL};
  TransVert *tv;
  float imat[3][3], bmat[3][3];
  float center_global[3];
  float offset_global[3];
  int a;

  if (use_offset) {
    if ((pivot_point == V3D_AROUND_ACTIVE) && snap_calc_active_center(C, true, center_global)) {
      /* pass */
    }
    else {
      snap_curs_to_sel_ex(C, pivot_point, center_global);
    }
    sub_v3_v3v3(offset_global, snap_target_global, center_global);
  }

  if (obedit) {
    float snap_target_local[3];
    ViewLayer *view_layer = cxt_data_view_layer(C);
    uint objs_len = 0;
    Ob **objs = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
        view_layer, v3d, &objs_len);
    for (uint ob_index = 0; ob_index < objs_len; ob_index++) {
      obedit = objs[ob_index];

      if (obedit->type == OB_MESH) {
        MeshEdit *medit = dune_meshedit_from_ob(obedit);

        if (medit->mesh->totvertsel == 0) {
          continue;
        }
      }

      if (ed_transverts_check_obedit(obedit)) {
        ed_transverts_create_from_obedit(&tvs, obedit, 0);
      }

      if (tvs.transverts_tot != 0) {
        copy_m3_m4(bmat, obedit->obmat);
        invert_m3_m3(imat, bmat);

        /* get the cursor in ob space */
        sub_v3_v3v3(snap_target_local, snap_target_global, obedit->obmat[3]);
        mul_m3_v3(imat, snap_target_local);

        if (use_offset) {
          float offset_local[3];

          mul_v3_m3v3(offset_local, imat, offset_global);

          tv = tvs.transverts;
          for (a = 0; a < tvs.transverts_tot; a++, tv++) {
            add_v3_v3(tv->loc, offset_local);
          }
        }
        else {
          tv = tvs.transverts;
          for (a = 0; a < tvs.transverts_tot; a++, tv++) {
            copy_v3_v3(tv->loc, snap_target_local);
          }
        }
        ed_transverts_update_obedit(&tvs, obedit);
      }
      ed_transverts_free(&tvs);
    }
    mem_free(objs);
  }
  else if (OBPOSE_FROM_OBACT(obact)) {
    struct KeyingSet *ks = anim_get_keyingset_for_autokeying(scene, ANIM_KS_LOCATION_ID);
    ViewLayer *view_layer = cxt_data_view_layer(C);
    uint objs_len = 0;
    Obj **objs = dune_ob_pose_array_get(view_layer, v3d, &objs_len);

    for (uint ob_index = 0; ob_index < objs_len; ob_index++) {
      Ob *ob = objs[ob_index];
      PoseChannel *pchan;
      Armature *arm = ob->data;
      float snap_target_local[3];

      invert_m4_m4(ob->imat, ob->obmat);
      mul_v3_m4v3(snap_target_local, ob->imat, snap_target_global);

      for (pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
        if ((pchan->bone->flag & BONE_SELECTED) && (PBONE_VISIBLE(arm, pchan->bone)) &&
            /* if the bone has a parent and is connected to the parent,
             * don't do anything - will break chain unless we do auto-ik.
             */
            (pchan->bone->flag & BONE_CONNECTED) == 0) {
          pchan->bone->flag |= BONE_TRANSFORM;
        }
        else {
          pchan->bone->flag &= ~BONE_TRANSFORM;
        }
      }

      for (pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
        if ((pchan->bone->flag & BONE_TRANSFORM) &&
            /* check that our parents not transformed (if we have one) */
            ((pchan->bone->parent &&
              dune_armature_bone_flag_test_recursive(pchan->bone->parent, BONE_TRANSFORM)) == 0)) {
          /* Get position in pchan (pose) space. */
          float cursor_pose[3];

          if (use_offset) {
            mul_v3_m4v3(cursor_pose, ob->obmat, pchan->pose_mat[3]);
            add_v3_v3(cursor_pose, offset_global);

            mul_m4_v3(ob->imat, cursor_pose);
            dune_armature_loc_pose_to_bone(pchan, cursor_pose, cursor_pose);
          }
          else {
            dune_armature_loc_pose_to_bone(pchan, snap_target_local, cursor_pose);
          }

          /* copy new position */
          if (use_toolsettings) {
            if ((pchan->protectflag & OB_LOCK_LOCX) == 0) {
              pchan->loc[0] = cursor_pose[0];
            }
            if ((pchan->protectflag & OB_LOCK_LOCY) == 0) {
              pchan->loc[1] = cursor_pose[1];
            }
            if ((pchan->protectflag & OB_LOCK_LOCZ) == 0) {
              pchan->loc[2] = cursor_pose[2];
            }

            /* auto-keyframing */
            ed_autokeyframe_pchan(C, scene, ob, pchan, ks);
          }
          else {
            copy_v3_v3(pchan->loc, cursor_pose);
          }
        }
      }

      for (pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
        pchan->bone->flag &= ~BONE_TRANSFORM;
      }

      ob->pose->flag |= (POSE_LOCKED | POSE_DO_UNLOCK);

      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    }
    mem_free(objs);
  }
  else {
    struct KeyingSet *ks = anim_get_keyingset_for_autokeying(scene, ANIM_KS_LOCATION_ID);
    Main *main = cxt_data_main(C);
    Graph *graph = cxt_data_graph_ptr(C);

    /* Reset flags. */
    for (Ob *ob = main->objs.first; ob; ob = ob->id.next) {
      ob->flag &= ~OB_DONE;
    }

    /* Build ob array, tag objects we're transforming. */
    ViewLayer *view_layer = cxt_data_view_layer(C);
    Ob **objs = NULL;
    uint objs_len;
    {
      lib_array_declare(objects);
      FOREACH_SELECTED_EDITABLE_OBJECT_BEGIN (view_layer, v3d, ob) {
        lib_array_append(objects, ob);
        ob->flag |= OB_DONE;
      }
      FOREACH_SELECTED_EDITABLE_OB_END;
      objs_len = lib_array_len(objs);
    }

    const bool use_transform_skip_children = use_toolsettings &&
                                             (scene->toolsettings->transform_flag &
                                              SCE_XFORM_SKIP_CHILDREN);
    const bool use_transform_data_origin = use_toolsettings &&
                                           (scene->toolsettings->transform_flag &
                                            SCE_XFORM_DATA_ORIGIN);
    struct XFormObSkipChild_Container *xcs = NULL;
    struct XFormObData_Container *xds = NULL;

    if (use_transform_skip_children) {
      dune_scene_graph_eval_ensure(depsgraph, bmain);
      xcs = ed_ob_xform_skip_child_container_create();
      ed_ob_xform_skip_child_container_item_ensure_from_array(
          xcs, view_layer, objects, objects_len);
    }
    if (use_transform_data_origin) {
      dune_scene_graph_eval_ensure(graph, main);
      xds = ed_ob_data_xform_container_create();

      /* Init the transform data in a separate loop because the graph
       * may be evald while setting the locations. */
      for (int ob_index = 0; ob_index < obs_len; ob_index++) {
        Ob *ob = obs[ob_index];
        ed_ob_data_xform_container_item_ensure(xds, ob);
      }
    }

    for (int ob_index = 0; ob_index < objs_len; ob_index++) {
      Ob *ob = objs[ob_index];
      if (ob->parent && dune_ob_flag_test_recursive(ob->parent, OB_DONE)) {
        continue;
      }

      float cursor_parent[3]; /* parent-relative */

      if (use_offset) {
        add_v3_v3v3(cursor_parent, ob->obmat[3], offset_global);
      }
      else {
        copy_v3_v3(cursor_parent, snap_target_global);
      }

      sub_v3_v3(cursor_parent, ob->obmat[3]);

      if (ob->parent) {
        float originmat[3][3], parentmat[4][4];
        /* Use the evaluated object here because sometimes
         * `ob->parent->runtime.curve_cache` is required. */
        dune_scene_graph_eval_ensure(graph, main);
        Ob *ob_eval = graph_get_eval_ob(graph, ob);

        dune_ob_get_parent_matrix(ob_eval, ob_eval->parent, parentmat);
        mul_m3_m4m4(originmat, parentmat, ob->parentinv);
        invert_m3_m3(imat, originmat);
        mul_m3_v3(imat, cursor_parent);
      }
      if (use_toolsettings) {
        if ((ob->protectflag & OB_LOCK_LOCX) == 0) {
          ob->loc[0] += cursor_parent[0];
        }
        if ((ob->protectflag & OB_LOCK_LOCY) == 0) {
          ob->loc[1] += cursor_parent[1];
        }
        if ((ob->protectflag & OB_LOCK_LOCZ) == 0) {
          ob->loc[2] += cursor_parent[2];
        }

        /* auto-keyframing */
        ed_autokeyframe_object(C, scene, ob, ks);
      }
      else {
        add_v3_v3(ob->loc, cursor_parent);
      }

      graph_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);
    }

    if (objs) {
      mem_free(objs);
    }

    if (use_transform_skip_children) {
      ed_ob_xform_skip_child_container_update_all(xcs, main, graph);
      ed_ob_xform_skip_child_container_destroy(xcs);
    }
    if (use_transform_data_origin) {
      ed_ob_data_xform_container_update_all(xds, main, graph);
      ed_ob_data_xform_container_destroy(xds);
    }
  }

  win_ev_add_notifier(C, NC_OBJECT | ND_TRANSFORM, NULL);

  return true;
}

bool ed_view3d_snap_selected_to_location(Cxt *C,
                                         const float snap_target_global[3],
                                         const int pivot_point)
{
  /* These could be passed as arguments if needed. */
  /* Always use pivot point. */
  const bool use_offset = true;
  /* Disable object protected flags & auto-keyframing,
   * so this can be used as a low level fn. */
  const bool use_toolsettings = false;
  return snap_selected_to_location(
      C, snap_target_global, use_offset, pivot_point, use_toolsettings);
}

/* Snap Selection to Cursor Op */
static int snap_selected_to_cursor_ex(Cxt *C, WinOp *op)
{
  const bool use_offset = api_bool_get(op->ptr, "use_offset");

  Scene *scene = cxt_data_scene(C);

  const float *snap_target_global = scene->cursor.location;
  const int pivot_point = scene->toolsettings->transform_pivot_point;

  if (snap_sel_to_location(C, snap_target_global, use_offset, pivot_point, true)) {
    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

void VIEW3D_OT_snap_sel_to_cursor(WinOpType *ot)
{
  /* ids */
  ot->name = "Snap Selection to Cursor";
  ot->description = "Snap selected item(s) to the 3D cursor";
  ot->idname = "VIEW3D_OT_snap_selected_to_cursor";

  /* api cbs */
  ot->ex = snap_sel_to_cursor_ex;
  ot->poll = ed_op_view3d_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* api */
  api_def_bool(ot->sapi,
                  "use_offset",
                  1,
                  "Offset",
                  "If the sel should be snapped as a whole or by each ob center");
}

/* Snap Sel to Active Op */
/* Snaps each sel ob to the location of the active sel ob */
static int snap_sel_to_active_ex(Cxt *C, WinOp *op)
{
  float snap_target_global[3];

  if (snap_calc_active_center(C, false, snap_target_global) == false) {
    dune_report(op->reports, RPT_ERROR, "No active element found!");
    return OP_CANCELLED;
  }

  if (!snap_sel_to_location(C, snap_target_global, false, -1, true)) {
    return OP_CANCELLED;
  }
  return OP_FINISHED;
}

void VIEW3D_OT_snap_sel_to_active(WinOpType *ot)
{
  /* ids */
  ot->name = "Snap Sel to Active";
  ot->description = "Snap sel item(s) to the active item";
  ot->idname = "VIEW3D_OT_snap_sel_to_active";

  /* api cbs */
  ot->ex = snap_sel_to_active_ex;
  ot->poll = ed_op_view3d_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Snap Cursor to Grid Op */
/* Snaps the 3D cursor location to its nearest point on the grid. */
static int snap_curs_to_grid_ex(Cxt *C, WinOp *UNUSED(op))
{
  Scene *scene = cxt_data_scene(C);
  ARgn *rgn = cxt_win_rgn(C);
  View3D *v3d = cxt_win_view3d(C);
  float gridf, *curs;

  gridf = ed_view3d_grid_view_scale(scene, v3d, rgn, NULL);
  curs = scene->cursor.location;

  curs[0] = gridf * floorf(0.5f + curs[0] / gridf);
  curs[1] = gridf * floorf(0.5f + curs[1] / gridf);
  curs[2] = gridf * floorf(0.5f + curs[2] / gridf);

  win_ev_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, NULL); /* hrm */
  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  return OP_FINISHED;
}

void VIEW3D_OT_snap_cursor_to_grid(WinOpType *ot)
{
  /* ids */
  ot->name = "Snap Cursor to Grid";
  ot->description = "Snap 3D cursor to the nearest grid division";
  ot->idname = "VIEW3D_OT_snap_cursor_to_grid";

  /* api cbs */
  ot->ex = snap_curs_to_grid_ex;
  ot->poll = ed_op_rgn_view3d_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Snap Cursor to Sel Op */
/* Returns the center position of a tracking marker visible on the viewport
 * (useful to snap to). */
static void bundle_midpoint(Scene *scene, Ob *ob, float r_vec[3])
{
  MovieClip *clip = dune_ob_movieclip_get(scene, ob, false);
  MovieTracking *tracking;
  MovieTrackingOb *ob;
  bool ok = false;
  float min[3], max[3], mat[4][4], pos[3], cammat[4][4];

  if (!clip) {
    return;
  }

  tracking = &clip->tracking;

  copy_m4_m4(cammat, ob->obmat);

  dune_tracking_get_camera_ob_matrix(ob, mat);

  INIT_MINMAX(min, max);

  for (object = tracking->obs.first; ob; ob = ob->next) {
    List *tracksbase = dune_tracking_ob_get_tracks(tracking, ob);
    MovieTrackingTrack *track = tracksbase->first;
    float obmat[4][4];

    if (object->flag & TRACKING_OBJECT_CAMERA) {
      copy_m4_m4(obmat, mat);
    }
    else {
      float imat[4][4];

      dune_tracking_camera_get_reconstructed_interpolate(tracking, ob, scene->r.cfra, imat);
      invert_m4(imat);

      mul_m4_m4m4(obmat, cammat, imat);
    }

    while (track) {
      if ((track->flag & TRACK_HAS_BUNDLE) && TRACK_SELECTED(track)) {
        ok = 1;
        mul_v3_m4v3(pos, obmat, track->bundle_pos);
        minmax_v3v3_v3(min, max, pos);
      }

      track = track->next;
    }
  }

  if (ok) {
    mid_v3_v3v3(r_vec, min, max);
  }
}

/* Snaps the 3D cursor location to the median point of the sel */
static bool snap_curs_to_sel_ex(Cxt *C, const int pivot_point, float r_cursor[3])
{
  Graph *graph = cxt_data_ensure_eval_graph(C);
  ViewLayer *view_layer_eval = graph_get_eval_view_layer(graph);
  Ob *obedit = cxt_data_edit_ob(C);
  Scene *scene = cxt_data_scene(C);
  View3D *v3d = cxt_win_view3d(C);
  TransVertStore tvs = {NULL};
  TransVert *tv;
  float bmat[3][3], vec[3], min[3], max[3], centroid[3];
  int count = 0;

  INIT_MINMAX(min, max);
  zero_v3(centroid);

  if (obedit) {
    ViewLayer *view_layer = cxt_data_view_layer(C);
    uint objs_len = 0;
    Ob **objs = dune_view_layer_array_from_objects_in_edit_mode_unique_data(
        view_layer, cxt_win_view3d(C), &objs_len);
    for (uint ob_index = 0; ob_index < objs_len; ob_index++) {
      obedit = objs[ob_index];

      /* We can do that quick check for meshes only... */
      if (obedit->type == OB_MESH) {
        MeshEdit *medit = dune_meshedit_from_ob(obedit);

        if (medit->mesh->totvertsel == 0) {
          continue;
        }
      }

      if (ed_transverts_check_obedit(obedit)) {
        ed_transverts_create_from_obedit(&tvs, obedit, TM_ALL_JOINTS | TM_SKIP_HANDLES);
      }

      count += tvs.transverts_tot;
      if (tvs.transverts_tot != 0) {
        Ob *obedit_eval = graph_get_eval_ob(graph, obedit);
        copy_m3_m4(bmat, obedit_eval->obmat);

        tv = tvs.transverts;
        for (int i = 0; i < tvs.transverts_tot; i++, tv++) {
          copy_v3_v3(vec, tv->loc);
          mul_m3_v3(bmat, vec);
          add_v3_v3(vec, obedit_eval->obmat[3]);
          add_v3_v3(centroid, vec);
          minmax_v3v3_v3(min, max, vec);
        }
      }
      ed_transverts_free(&tvs);
    }
    mem_free(objs);
  }
  else {
    Ob *obact = cxt_data_active_ob(C);

    if (obact && (obact->mode & OB_MODE_POSE)) {
      Ob *obact_eval = graph_get_eval_ob(graph, obact);
      Armature *arm = obact_eval->data;
      PoseChannel *pchan;
      for (pchan = obact_eval->pose->chanbase.first; pchan; pchan = pchan->next) {
        if (arm->layer & pchan->bone->layer) {
          if (pchan->bone->flag & BONE_SELECTED) {
            copy_v3_v3(vec, pchan->pose_head);
            mul_m4_v3(obact_eval->obmat, vec);
            add_v3_v3(centroid, vec);
            minmax_v3v3_v3(min, max, vec);
            count++;
          }
        }
      }
    }
    else {
      FOREACH_SELECTED_OB_BEGIN (view_layer_eval, v3d, ob_eval) {
        copy_v3_v3(vec, ob_eval->obmat[3]);

        /* special case for camera -- snap to bundles */
        if (ob_eval->type == OB_CAMERA) {
          /* snap to bundles should happen only when bundles are visible */
          if (v3d->flag2 & V3D_SHOW_RECONSTRUCTION) {
            bundle_midpoint(scene, graph_get_original_ob(ob_eval), vec);
          }
        }

        add_v3_v3(centroid, vec);
        minmax_v3v3_v3(min, max, vec);
        count++;
      }
      FOREACH_SELECTED_OBJECT_END;
    }
  }

  if (count == 0) {
    return false;
  }

  if (pivot_point == V3D_AROUND_CENTER_BOUNDS) {
    mid_v3_v3v3(r_cursor, min, max);
  }
  else {
    mul_v3_fl(centroid, 1.0f / (float)count);
    copy_v3_v3(r_cursor, centroid);
  }
  return true;
}

static int snap_curs_to_sel_ex(Cxt *C, WinOp *UNUSED(op))
{
  Scene *scene = cxt_data_scene(C);
  const int pivot_point = scene->toolsettings->transform_pivot_point;
  if (snap_curs_to_sel_ex(C, pivot_point, scene->cursor.location)) {
    win_ev_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, NULL);
    graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

void VIEW3D_OT_snap_cursor_to_selected(WinOpType *ot)
{
  /* ids */
  ot->name = "Snap Cursor to Selected";
  ot->description = "Snap 3D cursor to the middle of the sel item(s)";
  ot->idname = "VIEW3D_OT_snap_cursor_to_sel";

  /* api cbs */
  ot->ex = snap_curs_to_sel_ex;
  ot->poll = ed_op_view3d_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Snap Cursor to Active Op */
/* Calcs the center position of the active ob in global space.
 * This could be exported to be a generic fn.
 * see: calcCenterActive */
static bool snap_calc_active_center(Cxt *C, const bool sel_only, float r_center[3])
{
  Ob *ob = cxt_data_active_ob(C);
  if (ob == NULL) {
    return false;
  }
  return ed_ob_calc_active_center(ob, sel_only, r_center);
}

static int snap_curs_to_active_ex(Cxt *C, WinOp *UNUSED(op))
{
  Scene *scene = cxt_data_scene(C);

  if (snap_calc_active_center(C, false, scene->cursor.location)) {
    win_ev_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, NULL);
    graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

void VIEW3D_OT_snap_cursor_to_active(WinOpType *ot)
{
  /* ids */
  ot->name = "Snap Cursor to Active";
  ot->description = "Snap 3D cursor to the active item";
  ot->idname = "VIEW3D_OT_snap_cursor_to_active";

  /* api cbs */
  ot->ex = snap_curs_to_active_ex;
  ot->poll = ed_op_view3d_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Snap Cursor to Center Op */
/* Snaps the 3D cursor location to the origin and clears cursor rotation. */
static int snap_curs_to_center_ex(Cxt *C, WinOp *UNUSED(op))
{
  Scene *scene = cxt_data_scene(C);
  float mat3[3][3];
  unit_m3(mat3);

  zero_v3(scene->cursor.location);
  dune_scene_cursor_mat3_to_rot(&scene->cursor, mat3, false);

  graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  win_ev_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, NULL);
  return OP_FINISHED;
}

void VIEW3D_OT_snap_cursor_to_center(WinOpType *ot)
{
  /* ids */
  ot->name = "Snap Cursor to World Origin";
  ot->description = "Snap 3D cursor to the world origin";
  ot->idname = "VIEW3D_OT_snap_cursor_to_center";

  /* api cbs */
  ot->ex = snap_curs_to_center_ex;
  ot->poll = ed_op_view3d_active;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Min/Max Ob Vertices Util */
bool ed_view3d_minmax_verts(Ob *obedit, float r_min[3], float r_max[3])
{
  TransVertStore tvs = {NULL};
  TransVert *tv;
  float centroid[3], vec[3], bmat[3][3];

  /* Metaballs are an exception. */
  if (obedit->type == OB_MBALL) {
    float ob_min[3], ob_max[3];
    bool changed;

    changed = dune_mball_minmax_ex(obedit->data, ob_min, ob_max, obedit->obmat, SELECT);
    if (changed) {
      minmax_v3v3_v3(r_min, r_max, ob_min);
      minmax_v3v3_v3(r_min, r_max, ob_max);
    }
    return changed;
  }

  if (ed_transverts_check_obedit(obedit)) {
    ed_transverts_create_from_obedit(&tvs, obedit, TM_ALL_JOINTS | TM_CALC_MAPLOC);
  }

  if (tvs.transverts_tot == 0) {
    return false;
  }

  copy_m3_m4(bmat, obedit->obmat);

  tv = tvs.transverts;
  for (int a = 0; a < tvs.transverts_tot; a++, tv++) {
    copy_v3_v3(vec, (tv->flag & TX_VERT_USE_MAPLOC) ? tv->maploc : tv->loc);
    mul_m3_v3(bmat, vec);
    add_v3_v3(vec, obedit->obmat[3]);
    add_v3_v3(centroid, vec);
    minmax_v3v3_v3(r_min, r_max, vec);
  }

  ed_transverts_free(&tvs);

  return true;
}
