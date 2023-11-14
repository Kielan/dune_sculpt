#include "mem_guardedalloc.h"

#include "lib_linklist.h"
#include "lib_list.h"
#include "lib_math.h"
#include "lib_rect.h"

#include "dune_action.h"
#include "dune_cxt.h"
#include "dune_global.h"
#include "dune_pen_mod.h"
#include "dune_idprop.h"
#include "dune_layer.h"
#include "dune_main.h"
#include "dune_mod.h"
#include "dune_obj.h"
#include "dune_report.h"
#include "dune_scene.h"

#include "graph_query.h"

#include "ui_resources.h"

#include "gpu_matrix.h"
#include "gpu_select.h"
#include "gpu_state.h"

#include "win_api.h"

#include "ed_obj.h"
#include "ed_screen.h"

#include "draw_engine.h"

#include "api_access.h"
#include "api_define.h"

#include "view3d_intern.h" /* own include */
#include "view3d_nav.h"

/* Camera to View Op */

static int view3d_camera_to_view_ex(Cxt *C, WinOp *UNUSED(op))
{
  const Graph *graph = cxt_data_ensure_eval_graph(C);
  View3D *v3d;
  ARgn *rgn;
  RgnView3D *rv3d;

  ObjTfmProtectedChannels obtfm;

  ed_view3d_cxt_user_rgn(C, &v3d, &rgn);
  rv3d = region->regiondata;

  ed_view3d_lastview_store(rv3d);

  dune_obj_tfm_protected_backup(v3d->camera, &obtfm);

  ed_view3d_to_obj(graph, v3d->camera, rv3d->ofs, rv3d->viewquat, rv3d->dist);

  dune_obj_tfm_protected_restore(v3d->camera, &obtfm, v3d->camera->protectflag);

  graph_id_tag_update(&v3d->camera->id, ID_RECALC_TRANSFORM);
  rv3d->persp = RV3D_CAMOB;

  win_ev_add_notifier(C, NC_OBJECT | ND_TRANSFORM, v3d->camera);

  return OP_FINISHED;
}

static bool view3d_camera_to_view_poll(Cxt *C)
{
  View3D *v3d;
  ARgn *rgn;

  if (ed_view3d_cxt_user_rgn(C, &v3d, &rgn)) {
    RgnView3D *rv3d = rgn->rgndata;
    if (v3d && v3d->camera && !ID_IS_LINKED(v3d->camera)) {
      if (rv3d && (RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ANY_TRANSFORM) == 0) {
        if (rv3d->persp != RV3D_CAMOB) {
          return true;
        }
      }
    }
  }

  return false;
}

void VIEW3D_OT_camera_to_view(WinOpType *ot)
{
  /* ids */
  ot->name = "Align Camera to View";
  ot->description = "Set camera view to active view";
  ot->idname = "VIEW3D_OT_camera_to_view";

  /* api cbs */
  ot->ex = view3d_camera_to_view_ex;
  ot->poll = view3d_camera_to_view_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Camera Fit Frame to Selected Op */

/* unlike VIEW3D_OT_view_selected this is for framing a render and not
 * meant to take into account vertex/bone selection for eg. */
static int view3d_camera_to_view_selected_exec(Cxt *C, WinOp *op)
{
  Main *main = cxt_data_main(C);
  Graph *graph = cxt_data_ensure_eval_graph(C);
  Scene *scene = cxt_data_scene(C);
  View3D *v3d = cxt_win_view3d(C); /* can be NULL */
  Obj *camera_ob = v3d ? v3d->camera : scene->camera;

  if (camera_ob == NULL) {
    dune_report(op->reports, RPT_ERROR, "No active camera");
    return OP_CANCELLED;
  }

  if (ed_view3d_camera_to_view_selected(main, graph, scene, camera_ob)) {
    win_ev_add_notifier(C, NC_OBJ | ND_TRANSFORM, camera_ob);
    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

void VIEW3D_OT_camera_to_view_selected(WinOpType *ot)
{
  /* ids */
  ot->name = "Camera Fit Frame to Selected";
  ot->description = "Move the camera so selected objs are framed";
  ot->idname = "VIEW3D_OT_camera_to_view_selected";

  /* api cbs */
  ot->ex = view3d_camera_to_view_selected_ex;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Obj as Camera Op */
static void sync_viewport_camera_smoothview(bCxt *C,
                                            View3D *v3d,
                                            Obj *ob,
                                            const int smooth_viewtx)
{
  Main *main = cxt_data_main(C);
  for (Screen *screen = main->screens.first; screen != NULL; screen = screen->id.next) {
    for (ScrArea *area = screen->areabase.first; area != NULL; area = area->next) {
      for (SpaceLink *space_link = area->spacedata.first; space_link != NULL;
           space_link = space_link->next) {
        if (space_link->spacetype == SPACE_VIEW3D) {
          View3D *other_v3d = (View3D *)space_link;
          if (other_v3d == v3d) {
            continue;
          }
          if (other_v3d->camera == ob) {
            continue;
          }
          if (v3d->scenelock) {
            List *list = (space_link == area->spacedata.first) ? &area->rgnbase :
                                                                   &space_link->rgnbase;
            for (ARgn *other_rgn = list->first; other_rgn != NULL;
                 other_rgn = other_rgn->next) {
              if (other_rgn->rgntype == RGN_TYPE_WIN) {
                if (other_rgn->rgndata) {
                  RgnView3D *other_rv3d = other_region->regiondata;
                  if (other_rv3d->persp == RV3D_CAMOB) {
                    Object *other_camera_old = other_v3d->camera;
                    other_v3d->camera = ob;
                    ed_view3d_lastview_store(other_rv3d);
                    ed_view3d_smooth_view(C,
                                          other_v3d,
                                          other_rgn,
                                          smooth_viewtx,
                                          &(const V3D_SmoothParams){
                                              .camera_old = other_camera_old,
                                              .camera = other_v3d->camera,
                                              .ofs = other_rv3d->ofs,
                                              .quat = other_rv3d->viewquat,
                                              .dist = &other_rv3d->dist,
                                              .lens = &other_v3d->lens,
                                          });
                  }
                  else {
                    other_v3d->camera = ob;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

static int view3d_setobjascamera_ex(Cxt *C, WinOp *op)
{
  View3D *v3d;
  ARgn *rgn;
  RgnView3D *rv3d;

  Scene *scene = cxt_data_scene(C);
  Obj *ob = cxt_data_active_obj(C);

  const int smooth_viewtx = win_op_smooth_viewtx_get(op);

  /* no NULL check is needed, poll checks */
  ed_view3d_cxt_user_rgn(C, &v3d, &rgn);
  rv3d = rgn->rgndata;

  if (ob) {
    Obj *camera_old = (rv3d->persp == RV3D_CAMOB) ? V3D_CAMERA_SCENE(scene, v3d) : NULL;
    rv3d->persp = RV3D_CAMOB;
    v3d->camera = ob;
    if (v3d->scenelock && scene->camera != ob) {
      scene->camera = ob;
      graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
    }

    /* unlikely but looks like a glitch when set to the same */
    if (camera_old != ob) {
      ed_view3d_lastview_store(rv3d);

      ed_view3d_smooth_view(C,
                            v3d,
                            rgn,
                            smooth_viewtx,
                            &(const V3D_SmoothParams){
                                .camera_old = camera_old,
                                .camera = v3d->camera,
                                .ofs = rv3d->ofs,
                                .quat = rv3d->viewquat,
                                .dist = &rv3d->dist,
                                .lens = &v3d->lens,
                            });
    }

    if (v3d->scenelock) {
      sync_viewport_camera_smoothview(C, v3d, ob, smooth_viewtx);
      win_ev_add_notifier(C, NC_SCENE, scene);
    }
    win_ev_add_notifier(C, NC_OBJ | ND_DRAW, scene);
  }

  return OP_FINISHED;
}

bool ed_op_rv3d_user_rgn_poll(Cxt *C)
{
  View3D *v3d_dummy;
  ARgn *rgn_dummy;

  return ed_view3d_cxt_user_rgn(C, &v3d_dummy, &rgn_dummy);
}

void VIEW3D_OT_obj_as_camera(WinOpType *ot)
{
  /* ids */
  ot->name = "Set Active Obj as Camera";
  ot->description = "Set the active obj as the active camera for this view or scene";
  ot->idname = "VIEW3D_OT_obj_as_camera";

  /* api cbs */
  ot->ex = view3d_setobjascamera_ex;
  ot->poll = es_op_rv3d_user_rgn_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Win and View Matrix Calc */
void view3d_winmatrix_set(Graph *graph,
                          ARgn *rgn,
                          const View3D *v3d,
                          const rcti *rect)
{
  RgnView3D *rv3d = rgn->rgndata;
  rctf full_viewplane;
  float clipsta, clipend;
  bool is_ortho;

  is_ortho = ed_view3d_viewplane_get(
      graph, v3d, rv3d, rgn->winx, rgn->winy, &full_viewplane, &clipsta, &clipend, NULL);
  rv3d->is_persp = !is_ortho;

#if 0
  printf("%s: %d %d %f %f %f %f %f %f\n",
         __func__,
         winx,
         winy,
         full_viewplane.xmin,
         full_viewplane.ymin,
         full_viewplane.xmax,
         full_viewplane.ymax,
         clipsta,
         clipend);
#endif

  /* Note the code here was tweaked to avoid an apparent compiler bug in clang 13 (see T91680). */
  rctf viewplane;
  if (rect) {
    /* Smaller viewplane subset for selection picking. */
    viewplane.xmin = full_viewplane.xmin +
                     (lib_rctf_size_x(&full_viewplane) * (rect->xmin / (float)rgn->winx));
    viewplane.ymin = full_viewplane.ymin +
                     (lib_rctf_size_y(&full_viewplane) * (rect->ymin / (float)rgn->winy));
    viewplane.xmax = full_viewplane.xmin +
                     (lib_rctf_size_x(&full_viewplane) * (rect->xmax / (float)rgn->winx));
    viewplane.ymax = full_viewplane.ymin +
                     (lib_rctf_size_y(&full_viewplane) * (rect->ymax / (float)rgn->winy));
  }
  else {
    viewplane = full_viewplane;
  }

  if (is_ortho) {
    gpu_matrix_ortho_set(
        viewplane.xmin, viewplane.xmax, viewplane.ymin, viewplane.ymax, clipsta, clipend);
  }
  else {
    gpu_matrix_frustum_set(
        viewplane.xmin, viewplane.xmax, viewplane.ymin, viewplane.ymax, clipsta, clipend);
  }

  /* update matrix in 3d view rgn */
  gpu_matrix_projection_get(rv3d->winmat);
}

static void obmat_to_viewmat(RgnView3D *rv3d, Obj *ob)
{
  float bmat[4][4];

  rv3d->view = RV3D_VIEW_USER; /* don't show the grid */

  normalize_m4_m4(bmat, ob->obmat);
  invert_m4_m4(rv3d->viewmat, bmat);

  /* view quat calc, needed for add obj */
  mat4_normalized_to_quat(rv3d->viewquat, rv3d->viewmat);
}

void view3d_viewmatrix_set(Graph *graph,
                           const Scene *scene,
                           const View3D *v3d,
                           RgnView3D *rv3d,
                           const float rect_scale[2])
{
  if (rv3d->persp == RV3D_CAMOB) { /* obs/camera */
    if (v3d->camera) {
      Obj *ob_camera_eval = graph_get_eval_obj(graph, v3d->camera);
      obmat_to_viewmat(rv3d, ob_camera_eval);
    }
    else {
      quat_to_mat4(rv3d->viewmat, rv3d->viewquat);
      rv3d->viewmat[3][2] -= rv3d->dist;
    }
  }
  else {
    bool use_lock_ofs = false;

    /* should be moved to better initialize later on XXX */
    if (RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ROTATION) {
      ed_view3d_lock(rv3d);
    }

    quat_to_mat4(rv3d->viewmat, rv3d->viewquat);
    if (rv3d->persp == RV3D_PERSP) {
      rv3d->viewmat[3][2] -= rv3d->dist;
    }
    if (v3d->ob_center) {
      Obj *ob_eval = graph_get_eval_obj(graph, v3d->ob_center);
      float vec[3];

      copy_v3_v3(vec, ob_eval->obmat[3]);
      if (ob_eval->type == OB_ARMATURE && v3d->ob_center_bone[0]) {
        PoseChannel *pchan = dune_pose_channel_find_name(ob_eval->pose, v3d->ob_center_bone);
        if (pchan) {
          copy_v3_v3(vec, pchan->pose_mat[3]);
          mul_m4_v3(ob_eval->obmat, vec);
        }
      }
      translate_m4(rv3d->viewmat, -vec[0], -vec[1], -vec[2]);
      use_lock_ofs = true;
    }
    else if (v3d->ob_center_cursor) {
      float vec[3];
      copy_v3_v3(vec, scene->cursor.location);
      translate_m4(rv3d->viewmat, -vec[0], -vec[1], -vec[2]);
      use_lock_ofs = true;
    }
    else {
      translate_m4(rv3d->viewmat, rv3d->ofs[0], rv3d->ofs[1], rv3d->ofs[2]);
    }

    /* lock offset */
    if (use_lock_ofs) {
      float persmat[4][4], persinv[4][4];
      float vec[3];

      /* we could calculate the real persmat/persinv here
       * but it would be unreliable so better to later */
      mul_m4_m4m4(persmat, rv3d->winmat, rv3d->viewmat);
      invert_m4_m4(persinv, persmat);

      mul_v2_v2fl(vec, rv3d->ofs_lock, rv3d->is_persp ? rv3d->dist : 1.0f);
      vec[2] = 0.0f;

      if (rect_scale) {
        /* Since 'RgnView3D.winmat' has been calc and this fn doesn't take the
         * 'ARgn' we don't know about the rgn size.
         * Use 'rect_scale' when drawing a sub-rgn to apply 2D offset,
         * scaled by the diff between the sub-rgn and the rgn size. */
        vec[0] /= rect_scale[0];
        vec[1] /= rect_scale[1];
      }

      mul_mat3_m4_v3(persinv, vec);
      translate_m4(rv3d->viewmat, vec[0], vec[1], vec[2]);
    }
    /* end lock offset */
  }
}

/* OpenGL Sel Utils */
void view3d_opengl_sel_cache_begin(void)
{
  gpu_sel_cache_begin();
}

void view3d_opengl_sel_cache_end(void)
{
  gpu_sel_cache_end();
}

struct DrawSelLoopUserData {
  uint pass;
  uint hits;
  GPUSelResult *buffer;
  uint buffer_len;
  const rcti *rect;
  eGPUSelMode gpu_sel_mode;
};

static bool drw_sel_loop_pass(eDRWSelStage stage, void *user_data)
{
  bool continue_pass = false;
  struct DrawSelLoopUserData *data = user_data;
  if (stage == DRW_SEL_PASS_PRE) {
    gpu_sel_begin(
        data->buffer, data->buffer_len, data->rect, data->gpu_sel_mode, data->hits);
    /* always run POST after PRE. */
    continue_pass = true;
  }
  else if (stage == DRW_SEL_PASS_POST) {
    int hits = gpu_sel_end();
    if (data->pass == 0) {
      /* quirk of gpu_sel_end, only take hits value from first call. */
      data->hits = hits;
    }
    if (data->gpu_sel_mode == GPU_SEL_NEAREST_FIRST_PASS) {
      data->gpu_sel_mode = GPU_SEL_NEAREST_SECOND_PASS;
      continue_pass = (hits > 0);
    }
    data->pass += 1;
  }
  else {
    lib_assert(0);
  }
  return continue_pass;
}

eV3DSelObjFilter ed_view3d_sel_filter_from_mode(const Scene *scene, const Object *obact)
{
  if (scene->toolsettings->object_flag & SCE_OB_MODE_LOCK) {
    if (obact && (obact->mode & OB_MODE_ALL_WEIGHT_PAINT) &&
        dune_obj_pose_armature_get((Obj *)obact)) {
      return VIEW3D_SEL_FILTER_WPAINT_POSE_MODE_LOCK;
    }
    return VIEW3D_SEL_FILTER_OBJECT_MODE_LOCK;
  }
  return VIEW3D_SEL_FILTER_NOP;
}

/* Implement VIEW3D_SEL_FILTER_OB_MODE_LOCK. */
static bool drw_sel_filter_obj_mode_lock(Obj *ob, void *user_data)
{
  const Obj *obact = user_data;
  return dune_obj_is_mode_compat(ob, obact->mode);
}

/* Implement VIEW3D_SEL_FILTER_WPAINT_POSE_MODE_LOCK for special case when
 * we want to sel pose bones (this doesn't switch modes) */
static bool drw_sel_filter_object_mode_lock_for_weight_paint(Obj *ob, void *user_data)
{
  LinkNode *ob_pose_list = user_data;
  return ob_pose_list && (lib_linklist_index(ob_pose_list, graph_get_original_obj(ob)) != -1);
}

int view3d_opengl_sel_ex(ViewCxt *vc,
                         GPUSelResult *buffer,
                         uint buffer_len,
                         const rcti *input,
                         eV3DSelMode sel_mode,
                         eV3DSelObjFilter sel_filter,
                         const bool do_material_slot_selection)
{
  struct ThemeState theme_state;
  const WinMngr *wm = cxt_wm(vc->C);
  Graph *graph = vc->graph;
  Scene *scene = vc->scene;
  View3D *v3d = vc->v3d;
  ARgn *rgn = vc->rgn;
  rcti rect;
  int hits = 0;
  const bool use_obedit_skip = (OBEDIT_FROM_VIEW_LAYER(vc->view_layer) != NULL) &&
                               (vc->obedit == NULL);
  const bool is_pick_sel = (U.gpu_flag & USER_GPU_FLAG_NO_DEPT_PICK) == 0;
  const bool do_passes = ((is_pick_select == false) &&
                          (select_mode == VIEW3D_SEL_PICK_NEAREST));
  const bool use_nearest = (is_pick_sel && sel_mode == VIEW3D_SELECT_PICK_NEAREST);
  bool draw_surface = true;

  eGPUSelMode gpu_sel_mode;

  /* case not a box sel */
  if (input->xmin == input->xmax) {
    /* seems to be default value for bones only now */
    lib_rcti_init_pt_radius(&rect, (const int[2]){input->xmin, input->ymin}, 12);
  }
  else {
    rect = *input;
  }

  if (is_pick_sel) {
    if (sel_mode == VIEW3D_SEL_PICK_NEAREST) {
      gpu_sel_mode = GPU_SEL_PICK_NEAREST;
    }
    else if (sel_mode == VIEW3D_SEL_PICK_ALL) {
      gpu_sel_mode = GPU_SEL_PICK_ALL;
    }
    else {
      gpu_sel_mode = GPU_SEL_ALL;
    }
  }
  else {
    if (do_passes) {
      gpu_sel_mode = GPU_SEL_NEAREST_FIRST_PASS;
    }
    else {
      gpu_sel_mode = GPU_SEL_ALL;
    }
  }

  /* Re-use cache (rect must be smaller than the cached)
   * other cxt is assumed to be unchanged */
  if (gpu_sel_is_cached()) {
    gpu_sel_begin(buffer, buffer_len, &rect, gpu_sel_mode, 0);
    gpu_sel_cache_load_id();
    hits = gpu_sel_end();
    goto finally;
  }

  /* Important to use 'vc->obact', not 'OBACT(vc->view_layer)' below,
   * so it will be NULL when hidden. */
  struct {
    Drw_ObjFilterFn fn;
    void *user_data;
  } obj_filter = {NULL, NULL};
  switch (sel_filter) {
    case VIEW3D_SEL_FILTER_OB_MODE_LOCK: {
      Obj *obact = vc->obact;
      if (obact && obact->mode != OB_MODE_OB) {
        obj_filter.fn = drw_sel_filter_obj_mode_lock;
        obj_filter.user_data = obact;
      }
      break;
    }
    case VIEW3D_SEL_FILTER_WPAINT_POSE_MODE_LOCK: {
      Obj *obact = vc->obact;
      lib_assert(obact && (obact->mode & OB_MODE_ALL_WEIGHT_PAINT));
      /* While this uses 'alloca' in a loop (which we typically avoid),
       * the number of items is nearly always 1, maybe 2..3 in rare cases. */
      LinkNode *ob_pose_list = NULL;
      if (obact->type == OB_PEN) {
        PenVirtualModData virtualModData;
        const PenModData *md = dune_pen_mod_get_virtual_modlist(
            obact, &virtualModData);
        for (; md; md = md->next) {
          if (md->type == ePenModType_Armature) {
            ArmaturePenModData *agmd = (ArmaturePenModData *)md;
            if (agmd->obj && (agmd->obj->mode & OB_MODE_POSE)) {
              lib_linklist_prepend_alloca(&ob_pose_list, agmd->obj);
            }
          }
        }
      }
      else {
        VirtualModData virtualModData;
        const ModData *md = dune_mods_get_virtual_modlist(obact,
                                                               &virtualModData);
        for (; md; md = md->next) {
          if (md->type == eModType_Armature) {
            ArmatureModData *amd = (ArmatureModData *)md;
            if (amd->ob && (amd->ob->mode & OB_MODE_POSE)) {
              lib_linklist_prepend_alloca(&ob_pose_list, amd->ob);
            }
          }
        }
      }
      ob_filter.fn = drw_sel_filter_ob_mode_lock_for_weight_paint;
      ob_filter.user_data = ob_pose_list;
      break;
    }
    case VIEW3D_SEL_FILTER_NOP:
      break;
  }

  /* Tools may request depth outside of regular drawing code. */
  ui_Theme_Store(&theme_state);
  ui_SetTheme(SPACE_VIEW3D, RGN_TYPE_WIN);

  /* All of the queries need to be perform on the drawing cxt */
  drw_opengl_cxt_enable();

  G.f |= G_FLAG_PICKSEL;

  /* Important we use the 'viewmat' and don't re-calculate since
   * the ob & bone view locking takes 'rect' into account, see: T51629. */
  ed_view3d_draw_setup_view(
      wm, vc->win, graph, scene, rgn, v3d, vc->rv3d->viewmat, NULL, &rect);

  if (!XRAY_ACTIVE(v3d)) {
    gpu_depth_test(GPU_DEPTH_LESS_EQUAL);
  }

  /* If in xray mode, we sel the wires in priority. */
  if (XRAY_ACTIVE(v3d) && use_nearest) {
    /* We need to call "gpu_sel_*" API's inside drw_sel_loop
     * bc the OpenGL cxt created & destroyed inside this fn */
    struct DrawSelLoopUserData drw_sel_loop_user_data = {
        .pass = 0,
        .hits = 0,
        .buffer = buffer,
        .buffer_len = buffer_len,
        .rect = &rect,
        .gpu_sel_mode = gpu_sel_mode,
    };
    draw_surface = false;
    drw_sel_loop(graph,
                 rgn,
                 v3d,
                 use_obedit_skip,
                 draw_surface,
                 use_nearest,
                 do_material_slot_selection,
                 &rect,
                 drw_sel_loop_pass,
                 &drw_sel_loop_user_data,
                 ob_filter.fn,
                 ob_filter.user_data);
    hits = drw_sel_loop_user_data.hits;
    /* FIX: This cleanup the state before doing another selection pass.
     * (see T56695) */
    gpu_sel_cache_end();
  }

  if (hits == 0) {
    /* We need to call "gpu_sel_*" API's inside drw_sel_loop
     * because the OpenGL cxt created & destroyed inside this fn. */
    struct DrawSelLoopUserData drw_sel_loop_user_data = {
        .pass = 0,
        .hits = 0,
        .buffer = buffer,
        .buffer_len = buffer_len,
        .rect = &rect,
        .gpu_sel_mode = gpu_sel_mode,
    };
    /* If are not in wireframe mode, we need to use the mesh surfaces to check for hits */
    draw_surface = (v3d->shading.type > OB_WIRE) || !XRAY_ENABLED(v3d);
    drw_sel_loop(graph,
                 rgn,
                 v3d,
                 use_obedit_skip,
                 draw_surface,
                 use_nearest,
                 do_material_slot_selection,
                 &rect,
                 drw_sel_loop_pass,
                 &drw_sel_loop_user_data,
                 ob_filter.fn,
                 ob_filter.user_data);
    hits = drw_sel_loop_user_data.hits;
  }

  G.f &= ~G_FLAG_PICKSEL;
  ed_view3d_draw_setup_view(
      wm, vc->win, graph, scene, rgn, v3d, vc->rv3d->viewmat, NULL, NULL);

  if (!XRAY_ACTIVE(v3d)) {
    gpu_depth_test(GPU_DEPTH_NONE);
  }

  drw_opengl_cxt_disable();

  ui_Theme_Restore(&theme_state);

finally:

  if (hits < 0) {
    printf("Too many obs in sel buffer\n"); /* XXX make error message */
  }

  return hits;
}

int view3d_opengl_sel(ViewCxt *vc,
                      GPUSelResult *buffer,
                      uint buffer_len,
                      const rcti *input,
                      eV3DSelMode sel_mode,
                      eV3DSelObFilter sel_filter)
{
  return view3d_opengl_sel_ex(vc, buffer, buffer_len, input, sel_mode, sel_filter, false);
}

int view3d_opengl_sel_with_id_filter(ViewCxt *vc,
                                     GPUSelResult *buffer,
                                     const uint buffer_len,
                                     const rcti *input,
                                     eV3DSelMode sel_mode,
                                     eV3DSelObFilter sel_filter,
                                     uint sel_id)
{
  int hits = view3d_opengl_sel(vc, buffer, buffer_len, input, sel_mode, sel_filter);

  /* Selection sometimes uses -1 for an invalid selection Id, remove these as they
   * interfere with detection of actual number of hits in the sel. */
  if (hits > 0) {
    hits = gpu_sel_buffer_remove_by_id(buffer, hits, sel_id);
  }
  return hits;
}

/* Local View Ops */
static uint free_localview_bit(Main *main)
{
  ScrArea *area;
  Screen *screen;

  ushort local_view_bits = 0;

  /* Sometimes we lose a local-view: when an area is closed.
   * Check all areas: which local-views are in use? */
  for (screen = main->screens.first; screen; screen = screen->id.next) {
    for (area = screen->areabase.first; area; area = area->next) {
      SpaceLink *sl = area->spacedata.first;
      for (; sl; sl = sl->next) {
        if (sl->spacetype == SPACE_VIEW3D) {
          View3D *v3d = (View3D *)sl;
          if (v3d->localvd) {
            local_view_bits |= v3d->local_view_uuid;
          }
        }
      }
    }
  }

  for (int i = 0; i < 16; i++) {
    if ((local_view_bits & (1 << i)) == 0) {
      return (1 << i);
    }
  }

  return 0;
}

static bool view3d_localview_init(const Graph *graph,
                                  WinMngr *wm,
                                  Win *win,
                                  Main *main,
                                  ViewLayer *view_layer,
                                  ScrArea *area,
                                  const bool frame_selected,
                                  const int smooth_viewtx,
                                  ReportList *reports)
{
  View3D *v3d = area->spacedata.first;
  Base *base;
  float min[3], max[3], box[3];
  float size = 0.0f;
  uint local_view_bit;
  bool ok = false;

  if (v3d->localvd) {
    return ok;
  }

  INIT_MINMAX(min, max);

  local_view_bit = free_localview_bit(main);

  if (local_view_bit == 0) {
    /* TODO: We can kick one of the other 3D views out of local view
     * specially if it is not being used. */
    dune_report(reports, RPT_ERROR, "No more than 16 local views");
    ok = false;
  }
  else {
    Ob *obedit = OBEDIT_FROM_VIEW_LAYER(view_layer);
    if (obedit) {
      for (base = FIRSTBASE(view_layer); base; base = base->next) {
        base->local_view_bits &= ~local_view_bit;
      }
      FOREACH_BASE_IN_EDIT_MODE_BEGIN (view_layer, v3d, base_iter) {
        dune_ob_minmax(base_iter->object, min, max, false);
        base_iter->local_view_bits |= local_view_bit;
        ok = true;
      }
      FOREACH_BASE_IN_EDIT_MODE_END;
    }
    else {
      for (base = FIRSTBASE(view_layer); base; base = base->next) {
        if (BASE_SELECTED(v3d, base)) {
          dune_ob_minmax(base->ob, min, max, false);
          base->local_view_bits |= local_view_bit;
          ok = true;
        }
        else {
          base->local_view_bits &= ~local_view_bit;
        }
      }
    }

    sub_v3_v3v3(box, max, min);
    size = max_fff(box[0], box[1], box[2]);
  }

  if (ok == false) {
    return false;
  }

  ARgn *rgn;

  v3d->localvd = mem_malloc(sizeof(View3D), "localview");

  memcpy(v3d->localvd, v3d, sizeof(View3D));
  v3d->local_view_uuid = local_view_bit;

  for (rgn = area->rgnbase.first; rgn; rgn = rgn->next) {
    if (region->rgntype == RGN_TYPE_WIN) {
      RgnView3D *rv3d = rgn->rgndata;
      bool ok_dist = true;

      /* New view vals. */
      Ob *camera_old = NULL;
      float dist_new, ofs_new[3];

      rv3d->localvd = mem_malloc(sizeof(RgnView3D), "localview rgn");
      memcpy(rv3d->localvd, rv3d, sizeof(RgnView3D));

      if (frame_selected) {
        float mid[3];
        mid_v3_v3v3(mid, min, max);
        negate_v3_v3(ofs_new, mid);

        if (rv3d->persp == RV3D_CAMOB) {
          rv3d->persp = RV3D_PERSP;
          camera_old = v3d->camera;
        }

        if (rv3d->persp == RV3D_ORTHO) {
          if (size < 0.0001f) {
            ok_dist = false;
          }
        }

        if (ok_dist) {
          dist_new = ed_view3d_radius_to_dist(
              v3d, rgn, graph, rv3d->persp, true, (size / 2) * VIEW3D_MARGIN);

          if (rv3d->persp == RV3D_PERSP) {
            /* Don't zoom closer than the near clipping plane. */
            dist_new = max_ff(dist_new, v3d->clip_start * 1.5f);
          }
        }

        ed_view3d_smooth_view_ex(graph,
                                 wm,
                                 win,
                                 area,
                                 v3d,
                                 rgn,
                                 smooth_viewtx,
                                 &(const V3D_SmoothParams){
                                     .camera_old = camera_old,
                                     .ofs = ofs_new,
                                     .quat = rv3d->viewquat,
                                     .dist = ok_dist ? &dist_new : NULL,
                                     .lens = &v3d->lens,
                                 });
      }
    }
  }

  return ok;
}

static void view3d_localview_exit(const Graph *graph,
                                  WinMngr *wm,
                                  Win *win,
                                  ViewLayer *view_layer,
                                  ScrArea *area,
                                  const bool frame_selected,
                                  const int smooth_viewtx)
{
  View3D *v3d = area->spacedata.first;

  if (v3d->localvd == NULL) {
    return;
  }

  for (Base *base = FIRSTBASE(view_layer); base; base = base->next) {
    if (base->local_view_bits & v3d->local_view_uuid) {
      base->local_view_bits &= ~v3d->local_view_uuid;
    }
  }

  Ob *camera_old = v3d->camera;
  Ob *camera_new = v3d->localvd->camera;

  v3d->local_view_uuid = 0;
  v3d->camera = v3d->localvd->camera;

  mem_free(v3d->localvd);
  v3d->localvd = NULL;
  MEM_SAFE_FREE(v3d->runtime.local_stats);

  LIST_FOREACH (ARgn *, rgn, &area->rgnbase) {
    if (rgn->rgntype == RGN_TYPE_WIN) {
      RgnView3D *rv3d = rgn->rgndata;

      if (rv3d->localvd == NULL) {
        continue;
      }

      if (frame_selected) {
        Ob *camera_old_rv3d, *camera_new_rv3d;

        camera_old_rv3d = (rv3d->persp == RV3D_CAMOB) ? camera_old : NULL;
        camera_new_rv3d = (rv3d->localvd->persp == RV3D_CAMOB) ? camera_new : NULL;

        rv3d->view = rv3d->localvd->view;
        rv3d->persp = rv3d->localvd->persp;
        rv3d->camzoom = rv3d->localvd->camzoom;

        ed_view3d_smooth_view_ex(graph,
                                 wm,
                                 win,
                                 area,
                                 v3d,
                                 rgn,
                                 smooth_viewtx,
                                 &(const V3D_SmoothParams){
                                     .camera_old = camera_old_rv3d,
                                     .camera = camera_new_rv3d,
                                     .ofs = rv3d->localvd->ofs,
                                     .quat = rv3d->localvd->viewquat,
                                     .dist = &rv3d->localvd->dist,
                                 });
      }

      mem_free(rv3d->localvd);
      rv3d->localvd = NULL;
    }
  }
}

static int localview_ex(Cxt *C, WinOp *op)
{
  const Graph *graph = cxt_data_ensure_eval_graph(C);
  const int smooth_viewtx = win_op_smooth_viewtx_get(op);
  WinMngr *wm = cxt_wm(C);
  Win *win = cxt_win(C);
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  ScrArea *area = cxt_win_area(C);
  View3D *v3d = cxt_win_view3d(C);
  bool frame_selected = api_bool_get(op->ptr, "frame_selected");
  bool changed;

  if (v3d->localvd) {
    view3d_localview_exit(graph, wm, win, view_layer, area, frame_selected, smooth_viewtx);
    changed = true;
  }
  else {
    changed = view3d_localview_init(
        graph, wm, win, main, view_layer, area, frame_selected, smooth_viewtx, op->reports);
  }

  if (changed) {
    graph_id_type_tag(main, ID_OB);
    ed_area_tag_redraw(area);

    /* Unselected objects become selected when exiting. */
    if (v3d->localvd == NULL) {
      graph_id_tag_update(&scene->id, ID_RECALC_SEL);
      win_ev_add_notifier(C, NC_SCENE | ND_OB_SEL, scene);
    }
    else {
      graph_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);
    }

    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

void VIEW3D_OT_localview(WinOpType *ot)
{
  /* ids */
  ot->name = "Local View";
  ot->description = "Toggle display of selected ob(s) separately and centered in view";
  ot->idname = "VIEW3D_OT_localview";

  /* api cbs */
  ot->ex = localview_ex;
  ot->flag = OPTYPE_UNDO; /* localview changes ob layer bitflags */

  ot->poll = ed_op_view3d_active;

  api_def_bool(ot->srna,
                  "frame_selected",
                  true,
                  "Frame Selected",
                  "Move the view to frame the selected obs");
}

static int localview_remove_from_ex(Cxt *C, WinOp *op)
{
  View3D *v3d = CTX_wm_view3d(C);
  Main *main = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  bool changed = false;

  for (Base *base = FIRSTBASE(view_layer); base; base = base->next) {
    if (BASE_SELECTED(v3d, base)) {
      base->local_view_bits &= ~v3d->local_view_uuid;
      ED_object_base_select(base, BA_DESELECT);

      if (base == BASACT(view_layer)) {
        view_layer->basact = NULL;
      }
      changed = true;
    }
  }

  if (changed) {
    DEG_tag_on_visible_update(bmain, false);
    DEG_id_tag_update(&scene->id, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
    WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);
    return OPERATOR_FINISHED;
  }

  BKE_report(op->reports, RPT_ERROR, "No object selected");
  return OPERATOR_CANCELLED;
}

static bool localview_remove_from_poll(bContext *C)
{
  if (CTX_data_edit_object(C) != NULL) {
    return false;
  }

  View3D *v3d = CTX_wm_view3d(C);
  return v3d && v3d->localvd;
}

void VIEW3D_OT_localview_remove_from(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove from Local View";
  ot->description = "Move selected objects out of local view";
  ot->idname = "VIEW3D_OT_localview_remove_from";

  /* api callbacks */
  ot->exec = localview_remove_from_exec;
  ot->invoke = WM_operator_confirm;
  ot->poll = localview_remove_from_poll;
  ot->flag = OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Local Collections
 * \{ */

static uint free_localcollection_bit(Main *bmain, ushort local_collections_uuid, bool *r_reset)
{
  ScrArea *area;
  bScreen *screen;

  ushort local_view_bits = 0;

  /* Check all areas: which local-views are in use? */
  for (screen = bmain->screens.first; screen; screen = screen->id.next) {
    for (area = screen->areabase.first; area; area = area->next) {
      SpaceLink *sl = area->spacedata.first;
      for (; sl; sl = sl->next) {
        if (sl->spacetype == SPACE_VIEW3D) {
          View3D *v3d = (View3D *)sl;
          if (v3d->flag & V3D_LOCAL_COLLECTIONS) {
            local_view_bits |= v3d->local_collections_uuid;
          }
        }
      }
    }
  }

  /* First try to keep the old uuid. */
  if (local_collections_uuid && ((local_collections_uuid & local_view_bits) == 0)) {
    return local_collections_uuid;
  }

  /* Otherwise get the first free available. */
  for (int i = 0; i < 16; i++) {
    if ((local_view_bits & (1 << i)) == 0) {
      *r_reset = true;
      return (1 << i);
    }
  }

  return 0;
}

static void local_collections_reset_uuid(LayerCollection *layer_collection,
                                         const ushort local_view_bit)
{
  if (layer_collection->flag & LAYER_COLLECTION_HIDE) {
    layer_collection->local_collections_bits &= ~local_view_bit;
  }
  else {
    layer_collection->local_collections_bits |= local_view_bit;
  }

  LISTBASE_FOREACH (LayerCollection *, child, &layer_collection->layer_collections) {
    local_collections_reset_uuid(child, local_view_bit);
  }
}

static void view3d_local_collections_reset(Main *bmain, const uint local_view_bit)
{
  LISTBASE_FOREACH (Scene *, scene, &bmain->scenes) {
    LISTBASE_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
      LISTBASE_FOREACH (LayerCollection *, layer_collection, &view_layer->layer_collections) {
        local_collections_reset_uuid(layer_collection, local_view_bit);
      }
    }
  }
}

bool ED_view3d_local_collections_set(Main *bmain, struct View3D *v3d)
{
  if ((v3d->flag & V3D_LOCAL_COLLECTIONS) == 0) {
    return true;
  }

  bool reset = false;
  v3d->flag &= ~V3D_LOCAL_COLLECTIONS;
  uint local_view_bit = free_localcollection_bit(bmain, v3d->local_collections_uuid, &reset);

  if (local_view_bit == 0) {
    return false;
  }

  v3d->local_collections_uuid = local_view_bit;
  v3d->flag |= V3D_LOCAL_COLLECTIONS;

  if (reset) {
    view3d_local_collections_reset(bmain, local_view_bit);
  }

  return true;
}

void ED_view3d_local_collections_reset(struct bContext *C, const bool reset_all)
{
  Main *bmain = CTX_data_main(C);
  uint local_view_bit = ~(0);
  bool do_reset = false;

  /* Reset only the ones that are not in use. */
  LISTBASE_FOREACH (bScreen *, screen, &bmain->screens) {
    LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
      LISTBASE_FOREACH (SpaceLink *, sl, &area->spacedata) {
        if (sl->spacetype == SPACE_VIEW3D) {
          View3D *v3d = (View3D *)sl;
          if (v3d->local_collections_uuid) {
            if (v3d->flag & V3D_LOCAL_COLLECTIONS) {
              local_view_bit &= ~v3d->local_collections_uuid;
            }
            else {
              do_reset = true;
            }
          }
        }
      }
    }
  }

  if (do_reset) {
    view3d_local_collections_reset(bmain, local_view_bit);
  }
  else if (reset_all && (do_reset || (local_view_bit != ~(0)))) {
    view3d_local_collections_reset(bmain, ~(0));
    View3D v3d = {.local_collections_uuid = ~(0)};
    BKE_layer_collection_local_sync(CTX_data_view_layer(C), &v3d);
    DEG_id_tag_update(&CTX_data_scene(C)->id, ID_RECALC_BASE_FLAGS);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name XR Functionality
 * \{ */

#ifdef WITH_XR_OPENXR

static void view3d_xr_mirror_begin(RegionView3D *rv3d)
{
  /* If there is no session yet, changes below should not be applied! */
  BLI_assert(WM_xr_session_exists(&((wmWindowManager *)G_MAIN->wm.first)->xr));

  rv3d->runtime_viewlock |= RV3D_LOCK_ANY_TRANSFORM;
  /* Force perspective view. This isn't reset but that's not really an issue. */
  rv3d->persp = RV3D_PERSP;
}

static void view3d_xr_mirror_end(RegionView3D *rv3d)
{
  rv3d->runtime_viewlock &= ~RV3D_LOCK_ANY_TRANSFORM;
}

void ED_view3d_xr_mirror_update(const ScrArea *area, const View3D *v3d, const bool enable)
{
  ARegion *region_rv3d;

  BLI_assert(v3d->spacetype == SPACE_VIEW3D);

  if (ED_view3d_area_user_region(area, v3d, &region_rv3d)) {
    if (enable) {
      view3d_xr_mirror_begin(region_rv3d->regiondata);
    }
    else {
      view3d_xr_mirror_end(region_rv3d->regiondata);
    }
  }
}

void ED_view3d_xr_shading_update(wmWindowManager *wm, const View3D *v3d, const Scene *scene)
{
  if (v3d->runtime.flag & V3D_RUNTIME_XR_SESSION_ROOT) {
    View3DShading *xr_shading = &wm->xr.session_settings.shading;
    /* Flags that shouldn't be overridden by the 3D View shading. */
    int flag_copy = 0;
    if (v3d->shading.type != OB_SOLID) {
      /* Don't set V3D_SHADING_WORLD_ORIENTATION for solid shading since it results in distorted
       * lighting when the view matrix has a scale factor. */
      flag_copy |= V3D_SHADING_WORLD_ORIENTATION;
    }

    BLI_assert(WM_xr_session_exists(&wm->xr));

    if (v3d->shading.type == OB_RENDER) {
      if (!(BKE_scene_uses_blender_workbench(scene) || BKE_scene_uses_blender_eevee(scene))) {
        /* Keep old shading while using Cycles or another engine, they are typically not usable in
         * VR. */
        return;
      }
    }

    if (xr_shading->prop) {
      IDP_FreeProperty(xr_shading->prop);
      xr_shading->prop = NULL;
    }

    /* Copy shading from View3D to VR view. */
    const int old_xr_shading_flag = xr_shading->flag;
    *xr_shading = v3d->shading;
    xr_shading->flag = (xr_shading->flag & ~flag_copy) | (old_xr_shading_flag & flag_copy);
    if (v3d->shading.prop) {
      xr_shading->prop = IDP_CopyProperty(xr_shading->prop);
    }
  }
}

bool ED_view3d_is_region_xr_mirror_active(const wmWindowManager *wm,
                                          const View3D *v3d,
                                          const ARegion *region)
{
  return (v3d->flag & V3D_XR_SESSION_MIRROR) &&
         /* The free region (e.g. the camera region in quad-view) is always
          * the last in the list base. We don't want any other to be affected. */
         !region->next &&  //
         WM_xr_session_is_ready(&wm->xr);
}

#endif
