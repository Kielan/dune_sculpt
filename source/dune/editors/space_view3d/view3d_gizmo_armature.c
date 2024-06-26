#include "lib_dunelib.h"
#include "lib_math.h"
#include "lib_utildefines.h"

#include "dune_action.h"
#include "dune_armature.h"
#include "dune_cxt.h"
#include "dune_layer.h"
#include "dune_ob.h"

#include "types_armature.h"
#include "types_ob.h"

#include "ed_armature.h"
#include "ed_gizmo_lib.h"
#include "ed_screen.h"

#include "ui_resources.h"

#include "mem_guardedalloc.h"

#include "api_access.h"

#include "win_api.h"
#include "win_types.h"
#include "view3d_intern.h" /* own include */

/* Armature Spline Gizmo */
/* TODO: Current conversion is a approximation (usable not correct),
 * we'll need to take the next/previous bones into account to get the tangent directions.
 * First last matrices from 'dune_pchan_bbone_spline_setup' are close but also not quite accurate
 * since they're not at either end-points on the curve.
 * Likely we'll need a fn especially to get the first/last orientations. */

#define BBONE_SCALE_Y 3.0f

struct BoneSplineHandle {
  WinGizmo *gizmo;
  PoseChannel *pchan;
  /* We could remove, keep since at the moment for checking the conversion. */
  float co[3];
  int index;
};

struct BoneSplineWidgetGroup {
  struct BoneSplineHandle handles[2];
};

static void gizmo_bbone_offset_get(const WinGizmo *UNUSED(gz),
                                   WinGizmoProp *gz_prop,
                                   void *val_p)
{
  struct BoneSplineHandle *bh = gz_prop->custom_fn.user_data;
  PoseChannel *pchan = bh->pchan;

  float *val = val_p;
  lib_assert(gz_prop->type->array_length == 3);

  if (bh->index == 0) {
    bh->co[1] = pchan->bone->ease1 / BBONE_SCALE_Y;
    bh->co[0] = pchan->curve_in_x;
    bh->co[2] = pchan->curve_in_z;
  }
  else {
    bh->co[1] = -pchan->bone->ease2 / BBONE_SCALE_Y;
    bh->co[0] = pchan->curve_out_x;
    bh->co[2] = pchan->curve_out_z;
  }
  copy_v3_v3(val, bh->co);
}

static void gizmo_bbone_offset_set(const WinGizmo *UNUSED(gz),
                                   WinGizmoProp *gz_prop,
                                   const void *val_p)
{
  struct BoneSplineHandle *bh = gz_prop->custom_fn.user_data;
  PoseChannel *pchan = bh->pchan;

  const float *value = val_p;

  lib_assert(gz_prop->type->array_length == 3);
  copy_v3_v3(bh->co, val);

  if (bh->index == 0) {
    pchan->bone->ease1 = max_ff(0.0f, bh->co[1] * BBONE_SCALE_Y);
    pchan->curve_in_x = bh->co[0];
    pchan->curve_in_z = bh->co[2];
  }
  else {
    pchan->bone->ease2 = max_ff(0.0f, -bh->co[1] * BBONE_SCALE_Y);
    pchan->curve_out_x = bh->co[0];
    pchan->curve_out_z = bh->co[2];
  }
}

static bool WIDGETGROUP_armature_spline_poll(const Cxt *C, WinGizmoGroupType *UNUSED(gzgt))
{
  View3D *v3d = cxt_win_view3d(C);
  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CXT)) {
    return false;
  }

  ViewLayer *view_layer = cxt_data_view_layer(C);
  Base *base = BASACT(view_layer);
  if (base && BASE_SELECTABLE(v3d, base)) {
    Ob *ob = dune_ob_pose_armature_get(base->ob);
    if (ob) {
      const Armature *arm = ob->data;
      if (arm->drawtype == ARM_B_BONE) {
        PoseChannel *pchan = dune_pose_channel_active_if_layer_visible(ob);
        if (pchan && pchan->bone->segments > 1) {
          return true;
        }
      }
    }
  }
  return false;
}

static void WIDGETGROUP_armature_spline_setup(const Cxt *C, WinGizmoGroup *gzgroup)
{
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob = dune_ob_pose_armature_get(OBACT(view_layer));
  PoseChannel *pchan = dune_pose_channel_active_if_layer_visible(ob);

  const WinGizmoType *gzt_move = win_gizmotype_find("GIZMO_GT_move_3d", true);

  struct BoneSplineWidgetGroup *bspline_group = mem_calloc(sizeof(struct BoneSplineWidgetGroup),
                                                            __func__);
  gzgroup->customdata = bspline_group;

  /* Handles */
  for (int i = 0; i < ARRAY_SIZE(bspline_group->handles); i++) {
    WinGizmo *gz;
    gz = bspline_group->handles[i].gizmo = win_gizmo_new_ptr(gzt_move, gzgroup, NULL);
    api_enum_set(gz->ptr, "draw_style", ED_GIZMO_MOVE_STYLE_RING_2D);
    api_enum_set(gz->ptr,
                 "draw_options",
                 ED_GIZMO_MOVE_DRAW_FLAG_FILL | ED_GIZMO_MOVE_DRAW_FLAG_ALIGN_VIEW);
    win_gizmo_set_flag(gz, WIN_GIZMO_DRAW_VAL, true);

    ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
    ui_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);

    gz->scale_basis = 0.06f;

    if (i == 0) {
      copy_v3_v3(gz->matrix_basis[3], pchan->loc);
    }
  }
}

static void WIDGETGROUP_armature_spline_refresh(const Cxt *C, WinGizmoGroup *gzgroup)
{
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Ob *ob = dune_ob_pose_armature_get(OBACT(view_layer));

  if (!gzgroup->customdata) {
    return;
  }

  struct BoneSplineWidgetGroup *bspline_group = gzgroup->customdata;
  PoseChannel *pchan = dune_pose_channel_active_if_layer_visible(ob);

  /* Handles */
  for (int i = 0; i < ARRAY_SIZE(bspline_group->handles); i++) {
    WinGizmo *gz = bspline_group->handles[i].gizmo;
    bspline_group->handles[i].pchan = pchan;
    bspline_group->handles[i].index = i;

    float mat[4][4];
    mul_m4_m4m4(mat, ob->obmat, (i == 0) ? pchan->disp_mat : pchan->disp_tail_mat);
    copy_m4_m4(gz->matrix_space, mat);

    /* need to set prop here for undo. TODO: would prefer to do this in _init. */
    win_gizmo_target_prop_def_fn(gz,
                                 "offset",
                                 &(const struct WinGizmoPropFnParams){
                                    .val_get_fn = gizmo_bbone_offset_get,
                                    .val_set_fn = gizmo_bbone_offset_set,
                                    .range_get_fn = NULL,
                                    .user_data = &bspline_group->handles[i],
                                });
  }
}

void VIEW3D_GGT_armature_spline(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Armature Spline Widgets";
  gzgt->idname = "VIEW3D_GGT_armature_spline";

  gzgt->flag = (WIN_GIZMOGROUPTYPE_PERSISTENT | WIN_GIZMOGROUPTYPE_3D);

  gzgt->poll = WIDGETGROUP_armature_spline_poll;
  gzgt->setup = WIDGETGROUP_armature_spline_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = WIDGETGROUP_armature_spline_refresh;
}
