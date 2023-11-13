#include "lib_dunelib.h"
#include "lib_math.h"
#include "lib_utildefines.h"

#include "dune_camera.h"
#include "dune_cxt.h"
#include "dune_layer.h"

#include "types_camera.h"
#include "types_obj.h"

#include "ed_armature.h"
#include "ed_gizmo_lib.h"
#include "ed_screen.h"

#include "ui_resources.h"

#include "mem_guardedalloc.h"

#include "api_access.h"

#include "win_api.h"
#include "win_msg.h"
#include "win_types.h"

#include "graph.h"

#include "view3d_intern.h" /* own include */

/* Camera Gizmos */

struct CameraWidgetGroup {
  WinGizmo *dop_dist;
  WinGizmo *focal_len;
  WinGizmo *ortho_scale;
};

static bool WIDGETGROUP_camera_poll(const Cxt *C, WinGizmoGroupType *UNUSED(gzgt))
{
  View3D *v3d = cxt_win_view3d(C);
  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CXT)) {
    return false;
  }
  if ((v3d->gizmo_show_camera & V3D_GIZMO_SHOW_CAMERA_LENS) == 0 &&
      (v3d->gizmo_show_camera & V3D_GIZMO_SHOW_CAMERA_DOF_DIST) == 0) {
    return false;
  }

  ViewLayer *view_layer = cxt_data_view_layer(C);
  Base *base = BASACT(view_layer);
  if (base && BASE_SELECTABLE(v3d, base)) {
    Obj *ob = base->object;
    if (ob->type == OB_CAMERA) {
      Camera *camera = ob->data;
      /* TODO: support overrides. */
      if (!ID_IS_LINKED(camera)) {
        return true;
      }
    }
  }
  return false;
}

static void WIDGETGROUP_camera_setup(const Cxt *C, WinGizmoGroup *gzgroup)
{
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Object *ob = OBACT(view_layer);
  float dir[3];

  const WinGizmoType *gzt_arrow = win_gizmotype_find("GIZMO_GT_arrow_3d", true);

  struct CameraWidgetGroup *cagzgroup = mem_calloc(sizeof(struct CameraWidgetGroup), __func__);
  gzgroup->customdata = cagzgroup;

  negate_v3_v3(dir, ob->obmat[2]);

  /* dof distance */
  {
    WinGizmo *gz;
    gz = cagzgroup->dop_dist = win_gizmo_new_ptr(gzt_arrow, gzgroup, NULL);
    api_enum_set(gz->ptr, "draw_style", ED_GIZMO_ARROW_STYLE_CROSS);
    win_gizmo_set_flag(gz, WIN_GIZMO_DRAW_HOVER | WIN_GIZMO_DRAW_NO_SCALE, true);

    ui_GetThemeColor3fv(TH_GIZMO_A, gz->color);
    ii_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);
  }

  /* focal length
   * - logic/calcs are similar to dune_camera_view_frame_ex, better keep in sync */
  {
    WinGizmo *gz;
    gz = cagzgroup->focal_len = win_gizmo_new_ptr(gzt_arrow, gzgroup, NULL);
    gz->flag |= WIN_GIZMO_DRAW_NO_SCALE;
    api_enum_set(gz->ptr, "draw_style", ED_GIZMO_ARROW_STYLE_CONE);
    api_enum_set(gz->ptr, "transform", ED_GIZMO_ARROW_XFORM_FLAG_CONSTRAINED);

    ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
    ui_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);

    gz = cagzgroup->ortho_scale = win_gizmo_new_ptr(gzt_arrow, gzgroup, NULL);
    gz->flag |= WIN_GIZMO_DRAW_NO_SCALE;
    api_enum_set(gz->ptr, "draw_style", ED_GIZMO_ARROW_STYLE_CONE);
    api_enum_set(gz->ptr, "transform", ED_GIZMO_ARROW_XFORM_FLAG_CONSTRAINED);

    ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
    ui_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);
  }
}

static void WIDGETGROUP_camera_refresh(const Cxt *C, WinGizmoGroup *gzgroup)
{
  if (!gzgroup->customdata) {
    return;
  }

  struct CameraWidgetGroup *cagzgroup = gzgroup->customdata;
  View3D *v3d = cxt_win_view3d(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Object *ob = OBACT(view_layer);
  Camera *ca = ob->data;
  ApiPtr camera_ptr;
  float dir[3];

  api_ptr_create(&ca->id, &ApiCamera, ca, &camera_ptr);

  negate_v3_v3(dir, ob->obmat[2]);

  if ((ca->flag & CAM_SHOWLIMITS) && (v3d->gizmo_show_camera & V3D_GIZMO_SHOW_CAMERA_DOF_DIST)) {
    win_gizmo_set_matrix_location(cagzgroup->dop_dist, ob->obmat[3]);
    win_gizmo_set_matrix_rotation_from_yz_axis(cagzgroup->dop_dist, ob->obmat[1], dir);
    win_gizmo_set_scale(cagzgroup->dop_dist, ca->drawsize);
    win_gizmo_set_flag(cagzgroup->dop_dist, WM_GIZMO_HIDDEN, false);

    /* Need to set prop here for undo. TODO: would prefer to do this in _init. */
    ApiPtr camera_dof_ptr;
    api_ptr_create(&ca->id, &ApiCameraDOFSettings, &ca->dof, &camera_dof_ptr);
    win_gizmo_target_prop_def_api(
        cagzgroup->dop_dist, "offset", &camera_dof_ptr, "focus_distance", -1);
  }
  else {
    win_gizmo_set_flag(cagzgroup->dop_dist, WIN_GIZMO_HIDDEN, true);
  }

  /* TODO: make focal length/ortho ob_scale_inv widget optional. */
  const Scene *scene = cxt_data_scene(C);
  const float aspx = (float)scene->r.xsch * scene->r.xasp;
  const float aspy = (float)scene->r.ysch * scene->r.yasp;
  const bool is_ortho = (ca->type == CAM_ORTHO);
  const int sensor_fit = dune_camera_sensor_fit(ca->sensor_fit, aspx, aspy);
  /* Important to use camera value, not calculated fit since 'AUTO' uses width always. */
  const float sensor_size = dune_camera_sensor_size(ca->sensor_fit, ca->sensor_x, ca->sensor_y);
  WinGizmo *widget = is_ortho ? cagzgroup->ortho_scale : cagzgroup->focal_len;
  float scale_matrix;
  if (true) {
    float offset[3];
    float aspect[2];

    win_gizmo_set_flag(widget, WIN_GIZMO_HIDDEN, false);
    win_gizmo_set_flag(
        is_ortho ? cagzgroup->focal_len : cagzgroup->ortho_scale, WIN_GIZMO_HIDDEN, true);

    /* account for lens shifting */
    offset[0] = ((ob->scale[0] > 0.0f) ? -2.0f : 2.0f) * ca->shiftx;
    offset[1] = 2.0f * ca->shifty;
    offset[2] = 0.0f;

    /* get aspect */
    aspect[0] = (sensor_fit == CAMERA_SENSOR_FIT_HOR) ? 1.0f : aspx / aspy;
    aspect[1] = (sensor_fit == CAMERA_SENSOR_FIT_HOR) ? aspy / aspx : 1.0f;

    unit_m4(widget->matrix_basis);
    win_gizmo_set_matrix_location(widget, ob->obmat[3]);
    win_gizmo_set_matrix_rotation_from_yz_axis(widget, ob->obmat[1], dir);

    if (is_ortho) {
      scale_matrix = ca->ortho_scale * 0.5f;
    }
    else {
      const float ob_scale_inv[3] = {
          1.0f / len_v3(ob->obmat[0]),
          1.0f / len_v3(ob->obmat[1]),
          1.0f / len_v3(ob->obmat[2]),
      };
      const float ob_scale_uniform_inv = (ob_scale_inv[0] + ob_scale_inv[1] + ob_scale_inv[2]) /
                                         3.0f;
      scale_matrix = (ca->drawsize * 0.5f) / ob_scale_uniform_inv;
    }
    mul_v3_fl(widget->matrix_basis[0], scale_matrix);
    mul_v3_fl(widget->matrix_basis[1], scale_matrix);

    api_float_set_array(widget->ptr, "aspect", aspect);

    win_gizmo_set_matrix_offset_location(widget, offset);
  }

  /* define & update props */
  {
    const char *propname = is_ortho ? "ortho_scale" : "lens";
    ApiProp *prop = api_struct_find_prop(&camera_ptr, propname);
    const WinGizmoPropType *gz_prop_type = win_gizmotype_target_prop_find(widget->type,
                                                                          "offset");

    win_gizmo_target_prop_clear_api_ptr(widget, gz_prop_type);

    float min, max, range;
    float step, precision;

    /* get prop range */
    api_prop_float_ui_range(&camera_ptr, prop, &min, &max, &step, &precision);
    range = max - min;

    ed_gizmo_arrow3d_set_range_fac(
        widget,
        is_ortho ?
            ((range / ca->ortho_scale) * ca->drawsize) :
            (scale_matrix * range /
             /* Half sensor, intentionally use sensor from camera and not calc'd above. */
             (0.5f * sensor_size)));

    win_gizmo_target_prop_def_api_ptr(widget, gz_prop_type, &camera_ptr, prop, -1);
  }

  /* This could be handled more elegantly (split into two gizmo groups). */
  if ((v3d->gizmo_show_camera & V3D_GIZMO_SHOW_CAMERA_LENS) == 0) {
    win_gizmo_set_flag(cagzgroup->focal_len, WIN_GIZMO_HIDDEN, true);
    win_gizmo_set_flag(cagzgroup->ortho_scale, WIN_GIZMO_HIDDEN, true);
  }
}

static void WIDGETGROUP_camera_msg_sub(const Cxt *C,
                                       WinGizmoGroup *gzgroup,
                                       struct WinMsgBus *mbus)
{
  ARgn *rgn = cxt_win_rgn(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Obj *ob = OBACT(view_layer);
  Camera *ca = ob->data;

  WinMsgSubVal msg_sub_val_gz_tag_refresh = {
      .owner = region,
      .user_data = gzgroup->parent_gzmap,
      .notify = win_gizmo_do_msg_notify_tag_refresh,
  };

  {
    const ApiProp *props[] = {
        &api_CameraDOFSettings_focus_distance,
        &api_Camera_display_size,
        &api_Camera_ortho_scale,
        &api_Camera_sensor_fit,
        &api_Camera_sensor_width,
        &api_Camera_sensor_height,
        &api_Camera_shift_x,
        &api_Camera_shift_y,
        &api_Camera_type,
        &api_Camera_lens,
    };

    ApiPtr idptr;
    api_id_ptr_create(&ca->id, &idptr);

    for (int i = 0; i < ARRAY_SIZE(props); i++) {
      win_msg_sub_api(mbus, &idptr, props[i], &msg_sub_val_gz_tag_refresh, __func__);
    }
  }

  /* Sub to render settings */
  {
    win_msg_sub_api_anon_prop(
        mbus, RenderSettings, resolution_x, &msg_sub_val_gz_tag_refresh);
    win_msg_sub_api_anon_prop(
        mbus, RenderSettings, resolution_y, &msg_sub_val_gz_tag_refresh);
    win_msg_sub_api_anon_prop(
        mbus, RenderSettings, pixel_aspect_x, &msg_sub_val_gz_tag_refresh);
    win_msg_sub_api_anon_prop(
        mbus, RenderSettings, pixel_aspect_y, &msg_sub_val_gz_tag_refresh);
  }
}

void VIEW3D_GGT_camera(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Camera Widgets";
  gzgt->idname = "VIEW3D_GGT_camera"
  gzgt->flag = (WIN_GIZMOGROUPTYPE_PERSISTENT | WIN_GIZMOGROUPTYPE_3D | WIN_GIZMOGROUPTYPE_DEPTH_3D);

  gzgt->poll = WIDGETGROUP_camera_poll;
  gzgt->setup = WIDGETGROUP_camera_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = WIDGETGROUP_camera_refresh;
  gzgt->msg_sub = WIDGETGROUP_camera_msg_sub;
}

/* CameraView Gizmos */
struct CameraViewWidgetGroup {
  Scene *scene;
  bool is_camera;

  WinGizmo *border;

  struct {
    rctf *edit_border;
    rctf view_border;
  } state;
};

/* scale cbs */
static void gizmo_render_border_prop_matrix_get(const WinGizmo *UNUSED(gz),
                                                WinGizmoProp *gz_prop,
                                                void *value_p)
{
  float(*matrix)[4] = value_p;
  lib_assert(gz_prop->type->array_length == 16);
  struct CameraViewWidgetGroup *viewgroup = gz_prop->custom_fn.user_data;
  const rctf *border = viewgroup->state.edit_border;

  unit_m4(matrix);
  matrix[0][0] = lib_rctf_size_x(border);
  matrix[1][1] = lib_rctf_size_y(border);
  matrix[3][0] = lib_rctf_cent_x(border);
  matrix[3][1] = lib_rctf_cent_y(border);
}

static void gizmo_render_border_prop_matrix_set(const WinGizmo *UNUSED(gz),
                                                WinGizmoProp *gz_prop,
                                                const void *value_p)
{
  const float(*matrix)[4] = value_p;
  struct CameraViewWidgetGroup *viewgroup = gz_prop->custom_fn.user_data;
  rctf *border = viewgroup->state.edit_border;
  lib_assert(gz_prop->type->array_length == 16);

  lib_rctf_resize(border, len_v3(matrix[0]), len_v3(matrix[1]));
  lib_rctf_recenter(border, matrix[3][0], matrix[3][1]);
  lib_rctf_isect(
      &(rctf){
          .xmin = 0,
          .ymin = 0,
          .xmax = 1,
          .ymax = 1,
      },
      border,
      border);

  if (viewgroup->is_camera) {
    graph_id_tag_update(&viewgroup->scene->id, ID_RECALC_COPY_ON_WRITE);
  }
}

static bool WIDGETGROUP_camera_view_poll(const Cxt *C, WinGizmoGroupType *UNUSED(gzgt))
{
  Scene *scene = cxt_data_scene(C);

  /* This is just so the border isn't always in the way,
   * stealing mouse clicks from regular usage.
   * We could change the rules for when to show. */
  {
    ViewLayer *view_layer = cxt_data_view_layer(C);
    if (scene->camera != OBACT(view_layer)) {
      return false;
    }
  }

  View3D *v3d = cxt_win_view3d(C);
  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CXT)) {
    return false;
  }

  ARgn *rgn = cxt_win_rgn(C);
  RgnView3D *rv3d = rgn->rgndata;
  if (rv3d->persp == RV3D_CAMOB) {
    if (scene->r.mode & R_BORDER) {
      /* TODO: support overrides. */
      if (!ID_IS_LINKED(scene)) {
        return true;
      }
    }
  }
  else if (v3d->flag2 & V3D_RENDER_BORDER) {
    return true;
  }
  return false;
}

static void WIDGETGROUP_camera_view_setup(const Cxt *UNUSED(C), WinGizmoGroup *gzgroup)
{
  struct CameraViewWidgetGroup *viewgroup = mem_malloc(sizeof(struct CameraViewWidgetGroup),
                                                        __func__);

  viewgroup->border = win_gizmo_new("GIZMO_GT_cage_2d", gzgroup, NULL);

  api_enum_set(viewgroup->border->ptr,
               "transform",
               ED_GIZMO_CAGE2D_XFORM_FLAG_TRANSLATE | ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE);
  /* Box style is more subtle in this case. */
  api_enum_set(viewgroup->border->ptr, "draw_style", ED_GIZMO_CAGE2D_STYLE_BOX);

  win_gizmo_set_scale(viewgroup->border, 10.0f / 0.15f);

  gzgroup->customdata = viewgroup;
}

static void WIDGETGROUP_camera_view_draw_prepare(const Cxt *C, WinGizmoGroup *gzgroup)
{
  struct CameraViewWidgetGroup *viewgroup = gzgroup->customdata;

  ARgn *rgn = cxt_win_rgn(C);
  /* Drawing code should happen with fully eval graph. */
  struct Graph *graph = cxt_data_expect_eval_graph(C);
  RgnView3D *rv3d = rgn->rgndata;
  if (rv3d->persp == RV3D_CAMOB) {
    Scene *scene = cxt_data_scene(C);
    View3D *v3d = cxt_win_view3d(C);
    ed_view3d_calc_camera_border(
        scene, graph, rgn, v3d, rv3d, &viewgroup->state.view_border, false);
  }
  else {
    viewgroup->state.view_border = (rctf){
        .xmin = 0,
        .ymin = 0,
        .xmax = rgn->winx,
        .ymax = rgn->winy,
    };
  }

  WinGizmo *gz = viewgroup->border;
  unit_m4(gz->matrix_space);
  mul_v3_fl(gz->matrix_space[0], lib_rctf_size_x(&viewgroup->state.view_border));
  mul_v3_fl(gz->matrix_space[1], lib_rctf_size_y(&viewgroup->state.view_border));
  gz->matrix_space[3][0] = viewgroup->state.view_border.xmin;
  gz->matrix_space[3][1] = viewgroup->state.view_border.ymin;
}

static void WIDGETGROUP_camera_view_refresh(const Cxt *C, WinGizmoGroup *gzgroup)
{
  struct CameraViewWidgetGroup *viewgroup = gzgroup->customdata;

  View3D *v3d = cxt_in_view3d(C);
  ARgn *rgn = cxt_win_rgn(C);
  RgnView3D *rv3d = rgn->rgndata;
  Scene *scene = cxt_data_scene(C);

  viewgroup->scene = scene;

  {
    WinGizmo *gz = viewgroup->border;
    win_gizmo_set_flag(gz, WIN_GIZMO_HIDDEN, false);

    api_enum_set(viewgroup->border->ptr,
                 "transform",
                 ED_GIZMO_CAGE2D_XFORM_FLAG_TRANSLATE | ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE);

    if (rv3d->persp == RV3D_CAMOB) {
      viewgroup->state.edit_border = &scene->r.border;
      viewgroup->is_camera = true;
    }
    else {
      viewgroup->state.edit_border = &v3d->render_border;
      viewgroup->is_camera = false;
    }
    
    win_gizmo_target_prop_def_fn(gz,
                                "matrix",
                                &(const struct WinGizmoPropFnParams){
                                    .val_get_fn = gizmo_render_border_prop_matrix_get,
                                    .val_set_fn = gizmo_render_border_prop_matrix_set,
                                          .range_get_fn = NULL,
                                          .user_data = viewgroup,
                                      });
  }
}

void VIEW3D_GGT_camera_view(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Camera View Widgets";
  gzgt->idname = "VIEW3D_GGT_camera_view";

  gzgt->flag = (WIN_GIZMOGROUPTYPE_PERSISTENT | WIN_GIZMOGROUPTYPE_SCALE);

  gzgt->poll = WIDGETGROUP_camera_view_poll;
  gzgt->setup = WIDGETGROUP_camera_view_setup;
  gzgt->draw_prepare = WIDGETGROUP_camera_view_draw_prepare;
  gzgt->refresh = WIDGETGROUP_camera_view_refresh;
}
