#include "lib_math.h"
#include "lib_utildefines.h"

#include "dune_cxt.h"
#include "dune_layer.h"
#include "dune_ob.h"

#include "graph.h"

#include "types_light.h"
#include "types_ob.h"

#include "ed_gizmo_lib.h"
#include "ed_screen.h"

#include "ui_resources.h"

#include "mem_guardedalloc.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "win_api.h"
#include "win_types.h"

#include "view3d_intern.h" /* own include */

/* Spot Light Gizmos */
static bool WIDGETGROUP_light_spot_poll(const Cxt *C, WinGizmoGroupType *UNUSED(gzgt))
{
  View3D *v3d = cxt_wm_view3d(C);
  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CXT)) {
    return false;
  }
  if ((v3d->gizmo_show_light & V3D_GIZMO_SHOW_LIGHT_SIZE) == 0) {
    return false;
  }

  ViewLayer *view_layer = cxt_data_view_layer(C);
  Base *base = BASACT(view_layer);
  if (base && BASE_SELECTABLE(v3d, base)) {
    Obj *ob = base->ob;
    if (ob->type == OB_LAMP) {
      Light *la = ob->data;
      return (la->type == LA_SPOT);
    }
  }
  return false;
}

static void WIDGETGROUP_light_spot_setup(const Cxt *UNUSED(C), WinGizmoGroup *gzgroup)
{
  WinGizmoWrapper *wwrapper = mem_malloc(sizeof(WinGizmoWrapper), __func__);

  wwrapper->gizmo = win_gizmo_new("GIZMO_GT_arrow_3d", gzgroup, NULL);
  WinGizmo *gz = wwrapper->gizmo;
  api_enum_set(gz->ptr, "transform", ED_GIZMO_ARROW_XFORM_FLAG_INVERTED);

  gzgroup->customdata = wwrapper;

  ed_gizmo_arrow3d_set_range_fac(gz, 4.0f);

  ui_GetThemeColor3fv(TH_GIZMO_SECONDARY, gz->color);
}

static void WIDGETGROUP_light_spot_refresh(const duneContext *C, wmGizmoGroup *gzgroup)
{
  WinGizmoWrapper *wwrapper = gzgroup->customdata;
  WinGizmo *gz = wwrapper->gizmo;
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob = OBACT(view_layer);
  Light *la = ob->data;
  float dir[3];

  negate_v3_v3(dir, ob->obmat[2]);

  win_gizmo_set_matrix_rotation_from_z_axis(gz, dir);
  win_gizmo_set_matrix_location(gz, ob->obmat[3]);

  /* need to set prop here for undo. TODO: would prefer to do this in _init. */
  ApiPtr lamp_ptr;
  const char *propname = "spot_size";
  api_ptr_create(&la->id, &ApiLight, la, &lamp_ptr);
  win_gizmo_target_prop_def_api(gz, "offset", &lamp_ptr, propname, -1);
}

void VIEW3D_GGT_light_spot(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Spot Light Widgets";
  gzgt->idname = "VIEW3D_GGT_light_spot";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_PERSISTENT | WIN_GIZMOGROUPTYPE_3D | WIN_GIZMOGROUPTYPE_DEPTH_3D);

  gzgt->poll = WIDGETGROUP_light_spot_poll;
  gzgt->setup = WIDGETGROUP_light_spot_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = WIDGETGROUP_light_spot_refresh;
}

/* Area Light Gizmos */
/* scale cbs */
static void gizmo_area_light_prop_matrix_get(const WinGizmo *UNUSED(gz),
                                             WinGizmoProp *gz_prop,
                                             void *value_p)
{
  lib_assert(gz_prop->type->array_length == 16);
  float(*matrix)[4] = value_p;
  const Light *la = gz_prop->custom_fn.user_data;

  matrix[0][0] = la->area_size;
  matrix[1][1] = ELEM(la->area_shape, LA_AREA_RECT, LA_AREA_ELLIPSE) ? la->area_sizey :
                                                                       la->area_size;
}

static void gizmo_area_light_prop_matrix_set(const WinGizmo *UNUSED(gz),
                                             WinGizmoProp *gz_prop,
                                             const void *value_p)
{
  const float(*matrix)[4] = value_p;
  lib_assert(gz_prop->type->array_length == 16);
  Light *la = gz_prop->custom_fn.user_data;

  if (ELEM(la->area_shape, LA_AREA_RECT, LA_AREA_ELLIPSE)) {
    la->area_size = len_v3(matrix[0]);
    la->area_sizey = len_v3(matrix[1]);
  }
  else {
    la->area_size = len_v3(matrix[0]);
  }

  graph_id_tag_update(&la->id, ID_RECALC_COPY_ON_WRITE);
  win_main_add_notifier(NC_LAMP | ND_LIGHTING_DRAW, la);
}

static bool WIDGETGROUP_light_area_poll(const Cxt *C, WinGizmoGroupType *UNUSED(gzgt))
{
  View3D *v3d = cxt_win_view3d(C);
  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CXT)) {
    return false;
  }
  if ((v3d->gizmo_show_light & V3D_GIZMO_SHOW_LIGHT_SIZE) == 0) {
    return false;
  }

  ViewLayer *view_layer = CTX_data_view_layer(C);
  Base *base = BASACT(view_layer);
  if (base && BASE_SELECTABLE(v3d, base)) {
    Object *ob = base->object;
    if (ob->type == OB_LAMP) {
      Light *la = ob->data;
      return (la->type == LA_AREA);
    }
  }
  return false;
}

static void WIDGETGROUP_light_area_setup(const Cxt *UNUSED(C), WinGizmoGroup *gzgroup)
{
  WinGizmoWrapper *wwrapper = mem_malloc(sizeof(WinGizmoWrapper), __func__);
  wwrapper->gizmo = win_gizmo_new("GIZMO_GT_cage_2d", gzgroup, NULL);
  WinGizmo *gz = wwrapper->gizmo;
  api_enum_set(gz->ptr, "transform", ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE);

  gzgroup->customdata = wwrapper;

  win_gizmo_set_flag(gz, WIN_GIZMO_DRAW_HOVER, true);

  ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
  ui_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);
}

static void WIDGETGROUP_light_area_refresh(const Cxt *C, WinGizmoGroup *gzgroup)
{
  WinGizmoWrapper *wwrapper = gzgroup->customdata;
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob = OBACT(view_layer);
  Light *la = ob->data;
  WinGizmo *gz = wwrapper->gizmo;

  copy_m4_m4(gz->matrix_basis, ob->obmat);

  int flag = ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE;
  if (ELEM(la->area_shape, LA_AREA_SQUARE, LA_AREA_DISK)) {
    flag |= ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE_UNIFORM;
  }
  api_enum_set(gz->ptr, "transform", flag);

  /* need to set property here for undo. TODO: would prefer to do this in _init. */
  win_gizmo_target_prop_def_fn(gz,
                               "matrix",
                               &(const struct WinGizmoPropFnParams){
                                  .val_get_fn = gizmo_area_light_prop_matrix_get,
                                  .val_set_fn = gizmo_area_light_prop_matrix_set,
                                  .range_get_fn = NULL,
                                  .user_data = la,
                               });
}

void VIEW3D_GGT_light_area(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Area Light Widgets";
  gzgt->idname = "VIEW3D_GGT_light_area";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_PERSISTENT | WIN_GIZMOGROUPTYPE_3D | WIN_GIZMOGROUPTYPE_DEPTH_3D);

  gzgt->poll = WIDGETGROUP_light_area_poll;
  gzgt->setup = WIDGETGROUP_light_area_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = WIDGETGROUP_light_area_refresh;
}

/* Light Target Gizmo */
static bool WIDGETGROUP_light_target_poll(const Cxt *C, WinGizmoGroupType *UNUSED(gzgt))
{
  View3D *v3d = cxt_win_view3d(C);
  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CONTEXT)) {
    return false;
  }
  if ((v3d->gizmo_show_light & V3D_GIZMO_SHOW_LIGHT_LOOK_AT) == 0) {
    return false;
  }

  ViewLayer *view_layer = cxt_data_view_layer(C);
  Base *base = BASACT(view_layer);
  if (base && BASE_SELECTABLE(v3d, base)) {
    Ob *ob = base->object;
    if (ob->type == OB_LAMP) {
      Light *la = ob->data;
      return (ELEM(la->type, LA_SUN, LA_SPOT, LA_AREA));
    }
#if 0
    else if (ob->type == OB_CAMERA) {
      return true;
    }
#endif
  }
  return false;
}

static void WIDGETGROUP_light_target_setup(const Cxt *UNUSED(C), WinGizmoGroup *gzgroup)
{
  WinGizmoWrapper *wwrapper = mem_malloc(sizeof(WinGizmoWrapper), __func__);
  wwrapper->gizmo = win_gizmo_new("GIZMO_GT_move_3d", gzgroup, NULL);
  WinGizmo *gz = wwrapper->gizmo;

  gzgroup->customdata = wwrapper;

  ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
  ui_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);

  gz->scale_basis = 0.06f;

  WinOpType *ot = win_optype_find("OB_OT_transform_axis_target", true);

  api_enum_set(
      gz->ptr, "draw_options", ED_GIZMO_MOVE_DRAW_FLAG_FILL | ED_GIZMO_MOVE_DRAW_FLAG_ALIGN_VIEW);

  win_gizmo_op_set(gz, 0, ot, NULL);
}

static void WIDGETGROUP_light_target_draw_prepare(const Cxt *C, WinGizmoGroup *gzgroup)
{
  WinGizmoWrapper *wwrapper = gzgroup->customdata;
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob = OBACT(view_layer);
  WinGizmo *gz = wwrapper->gizmo;

  normalize_m4_m4(gz->matrix_basis, ob->obmat);
  unit_m4(gz->matrix_offset);

  if (ob->type == OB_LAMP) {
    Light *la = ob->data;
    if (la->type == LA_SPOT) {
      /* Draw just past the light size angle gizmo. */
      madd_v3_v3fl(gz->matrix_basis[3], gz->matrix_basis[2], -la->spotsize);
    }
  }
  gz->matrix_offset[3][2] -= 23.0;
  win_gizmo_set_flag(gz, WIN_GIZMO_DRAW_OFFSET_SCALE, true);
}

void VIEW3D_GGT_light_target(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Target Light Widgets";
  gzgt->idname = "VIEW3D_GGT_light_target";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_PERSISTENT | WIN_GIZMOGROUPTYPE_3D);

  gzgt->poll = WIDGETGROUP_light_target_poll;
  gzgt->setup = WIDGETGROUP_light_target_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->draw_prepare = WIDGETGROUP_light_target_draw_prepare;
}
