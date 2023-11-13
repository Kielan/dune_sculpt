#include "lib_math.h"
#include "lib_utildefines.h"

#include "dune_cxt.h"

#include "types_obj.h"

#include "ed_gizmo_lib.h"
#include "ed_screen.h"

#include "ui.h"
#include "ui_resource.h"

#include "mem_guardedalloc.h"

#include "api_access.h"

#include "win_api.h"
#include "win_types.h"

#include "view3d_intern.h" /* own include */

/* View3D Nav Gizmo Group */

/* Size of main icon. */
#define GIZMO_SIZE U.gizmo_size_nav_v3d

/* Main gizmo offset from screen edges in unscaled pixels. */
#define GIZMO_OFFSET 10.0f

/* Width of smaller btns in unscaled pixels. */
#define GIZMO_MINI_SIZE 28.0f

/* Margin around the smaller btns. */
#define GIZMO_MINI_OFFSET 2.0f

enum {
  GZ_INDEX_MOVE = 0,
  GZ_INDEX_ROTATE = 1,
  GZ_INDEX_ZOOM = 2,

  /* just btns */
  /* overlaps GZ_INDEX_ORTHO (switch between) */
  GZ_INDEX_PERSP = 3,
  GZ_INDEX_ORTHO = 4,
  GZ_INDEX_CAMERA = 5,

  GZ_INDEX_TOTAL = 6,
};

struct NavGizmoInfo {
  const char *opname;
  const char *gizmo;
  uint icon;
};

static struct NavGizmoInfo g_nav_params[GZ_INDEX_TOTAL] = {
    {
        .opname = "VIEW3D_OT_move",
        .gizmo = "GIZMO_GT_brn_2d",
        .icon = ICON_VIEW_PAN,
    },
    {
        .opname = "VIEW3D_OT_rotate",
        .gizmo = "VIEW3D_GT_nav_rotate",
        .icon = ICON_NONE,
    },
    {
        .opname = "VIEW3D_OT_zoom",
        .gizmo = "GIZMO_GT_btn_2d",
        .icon = ICON_VIEW_ZOOM,
    },
    {
        .opname = "VIEW3D_OT_view_persportho",
        .gizmo = "GIZMO_GT_btn_2d",
        .icon = ICON_VIEW_PERSPECTIVE,
    },
    {
        .opname = "VIEW3D_OT_view_persportho",
        .gizmo = "GIZMO_GT_btn_2d",
        .icon = ICON_VIEW_ORTHO,
    },
    {
        .opname = "VIEW3D_OT_view_camera",
        .gizmo = "GIZMO_GT_btn_2d",
        .icon = ICON_VIEW_CAMERA,
    },
};

struct NavWidgetGroup {
  WinGizmo *gz_array[GZ_INDEX_TOTAL];
  /* Store the view state to check for changes. */
  struct {
    rcti rect_visible;
    struct {
      char is_persp;
      bool is_camera;
      char viewlock;
    } rv3d;
  } state;
  int rgn_size[2];
};

static bool WIDGETGROUP_nav_poll(const Cxt *C, WinGizmoGroupType *UNUSED(gzgt))
{
  View3D *v3d = cxt_win_view3d(C);
  if ((((U.uiflag & USER_SHOW_GIZMO_NAV) == 0) &&
       (U.mini_axis_type != USER_MINI_AXIS_TYPE_GIZMO)) ||
      (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_NAV))) {
    return false;
  }
  return true;
}

static void WIDGETGROUP_nav_setup(const Cxt *C, WinGizmoGroup *gzgroup)
{
  struct NavWidgetGroup *navgroup = mem_calloc(sizeof(struct NavWidgetGroup), __func__);

  navgroup->rgn_size[0] = -1;
  navgroup->rgn_size[1] = -1;

  WinOpType *ot_view_axis = win_optype_find("VIEW3D_OT_view_axis", true);
  WinOpType *ot_view_camera = win_optype_find("VIEW3D_OT_view_camera", true);

  for (int i = 0; i < GZ_INDEX_TOTAL; i++) {
    const struct NavGizmoInfo *info = &g_nav_params[i];
    navgroup->gz_array[i] = win_gizmo_new(info->gizmo, gzgroup, NULL);
    WinGizmo *gz = navgroup->gz_array[i];
    gz->flag |= WIN_GIZMO_MOVE_CURSOR | WIN_GIZMO_DRAW_MODAL;

    if (i == GZ_INDEX_ROTATE) {
      gz->color[3] = 0.0f;
      copy_v3_fl(gz->color_hi, 0.5f);
      gz->color_hi[3] = 0.5f;
    }
    else {
      uchar icon_color[3];
      ui_GetThemeColor3ubv(TH_TXT, icon_color);
      int color_tint, color_tint_hi;
      if (icon_color[0] > 128) {
        color_tint = -40;
        color_tint_hi = 60;
        gz->color[3] = 0.5f;
        gz->color_hi[3] = 0.5f;
      }
      else {
        color_tint = 60;
        color_tint_hi = 60;
        gz->color[3] = 0.5f;
        gz->color_hi[3] = 0.75f;
      }
      ui_GetThemeColorShade3fv(TH_HEADER, color_tint, gz->color);
      ui_GetThemeColorShade3fv(TH_HEADER, color_tint_hi, gz->color_hi);
    }

    /* may be overwritten later */
    gz->scale_basis = GIZMO_MINI_SIZE / 2.0f;
    if (info->icon != ICON_NONE) {
      ApiProp *prop = api_struct_find_prop(gz->ptr, "icon");
      api_prop_enum_set(gz->ptr, prop, info->icon);
      api_enum_set(
          gz->ptr, "draw_options", ED_GIZMO_BTN_SHOW_OUTLINE | ED_GIZMO_BTN_SHOW_BACKDROP);
    }

    WinOpType *ot = win_optype_find(info->opname, true);
    win_gizmo_op_set(gz, 0, ot, NULL);
  }

  {
    WinGizmo *gz = navgroup->gz_array[GZ_INDEX_CAMERA];
    win_gizmo_op_set(gz, 0, ot_view_camera, NULL);
  }

  /* Click only btns (not modal). */
  {
    int gz_ids[] = {GZ_INDEX_PERSP, GZ_INDEX_ORTHO, GZ_INDEX_CAMERA};
    for (int i = 0; i < ARRAY_SIZE(gz_ids); i++) {
      WinGizmo *gz = navgroup->gz_array[gz_ids[i]];
      api_bool_set(gz->ptr, "show_drag", false);
    }
  }

  /* Modal ops, don't use initial mouse location since we're clicking on a btn. */
  {
    int gz_ids[] = {GZ_INDEX_MOVE, GZ_INDEX_ROTATE, GZ_INDEX_ZOOM};
    for (int i = 0; i < ARRAY_SIZE(gz_ids); i++) {
      WinGizmo *gz = navgroup->gz_array[gz_ids[i]];
      WinGizmoOpElem *gzop = win_gizmo_op_get(gz, 0);
      api_bool_set(&gzop->ptr, "use_cursor_init", false);
    }
  }

  {
    WinGizmo *gz = navgroup->gz_array[GZ_INDEX_ROTATE];
    gz->scale_basis = GIZMO_SIZE / 2.0f;
    const char mapping[6] = {
        RV3D_VIEW_LEFT,
        RV3D_VIEW_RIGHT,
        RV3D_VIEW_FRONT,
        RV3D_VIEW_BACK,
        RV3D_VIEW_BOTTOM,
        RV3D_VIEW_TOP,
    };

    for (int part_index = 0; part_index < 6; part_index += 1) {
      ApiPtr *ptr = win_gizmo_op_set(gz, part_index + 1, ot_view_axis, NULL);
      api_enum_set(ptr, "type", mapping[part_index]);
    }

    /* When dragging an axis, use this instead. */
    WinMngr *wm = cxt_wm(C);
    gz->keymap = win_gizmo_keymap_generic_click_drag(wm);
    gz->drag_part = 0;
  }

  gzgroup->customdata = navgroup;
}

static void WIDGETGROUP_nav_draw_prepare(const Cxt *C, WinGizmoGroup *gzgroup)
{
  struct NavWidgetGroup *navgroup = gzgroup->customdata;
  ARn *rgn = cxt_win_rgn(C);
  const RgnView3D *rv3d = rgn->rgndata;

  for (int i = 0; i < 3; i++) {
    copy_v3_v3(navgroup->gz_array[GZ_INDEX_ROTATE]->matrix_offset[i], rv3d->viewmat[i]);
  }

  const rcti *rect_visible = ed_rgn_visible_rect(rgn);

  /* Ensure types match so bits are never lost on assignment. */
  CHECK_TYPE_PAIR(navgroup->state.rv3d.viewlock, rv3d->viewlock);

  if ((navgroup->state.rect_visible.xmax == rect_visible->xmax) &&
      (navgroup->state.rect_visible.ymax == rect_visible->ymax) &&
      (navgroup->state.rv3d.is_persp == rv3d->is_persp) &&
      (navgroup->state.rv3d.is_camera == (rv3d->persp == RV3D_CAMOB)) &&
      (navgroup->state.rv3d.viewlock == RV3D_LOCK_FLAGS(rv3d))) {
    return;
  }

  navgroup->state.rect_visible = *rect_visible;
  navgroup->state.rv3d.is_persp = rv3d->is_persp;
  navgroup->state.rv3d.is_camera = (rv3d->persp == RV3D_CAMOB);
  navgroup->state.rv3d.viewlock = RV3D_LOCK_FLAGS(rv3d);

  const bool show_nav = (U.uiflag & USER_SHOW_GIZMO_NAV) != 0;
  const bool show_rotate_gizmo = (U.mini_axis_type == USER_MINI_AXIS_TYPE_GIZMO);
  const float icon_offset = ((GIZMO_SIZE / 2.0f) + GIZMO_OFFSET) * UI_DPI_FAC;
  const float icon_offset_mini = (GIZMO_MINI_SIZE + GIZMO_MINI_OFFSET) * UI_DPI_FAC;
  const float co_rotate[2] = {
      rect_visible->xmax - icon_offset,
      rect_visible->ymax - icon_offset,
  };

  float icon_offset_from_axis = 0.0f;
  switch ((eUserpref_MiniAxisType)U.mini_axis_type) {
    case USER_MINI_AXIS_TYPE_GIZMO:
      icon_offset_from_axis = icon_offset * 2.1f;
      break;
    case USER_MINI_AXIS_TYPE_MINIMAL:
      icon_offset_from_axis = (UI_UNIT_X * 2.5) + ((U.rvisize * U.pixelsize * 2.0f));
      break;
    case USER_MINI_AXIS_TYPE_NONE:
      icon_offset_from_axis = icon_offset_mini * 0.75f;
      break;
  }

  const float co[2] = {
      roundf(rect_visible->xmax - icon_offset_mini * 0.75f),
      roundf(rect_visible->ymax - icon_offset_from_axis),
  };

  WinGizmo *gz;

  for (uint i = 0; i < ARRAY_SIZE(navgroup->gz_array); i++) {
    gz = navgroup->gz_array[i];
    win_gizmo_set_flag(gz, WIN_GIZMO_HIDDEN, true);
  }

  if (show_rotate_gizmo) {
    gz = navgroup->gz_array[GZ_INDEX_ROTATE];
    gz->matrix_basis[3][0] = roundf(co_rotate[0]);
    gz->matrix_basis[3][1] = roundf(co_rotate[1]);
    win_gizmo_set_flag(gz, WIN_GIZMO_HIDDEN, false);
  }

  if (show_nav) {
    int icon_mini_slot = 0;
    if ((RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ZOOM_AND_DOLLY) == 0) {
      gz = navgroup->gz_array[GZ_INDEX_ZOOM];
      gz->matrix_basis[3][0] = roundf(co[0]);
      gz->matrix_basis[3][1] = roundf(co[1] - (icon_offset_mini * icon_mini_slot++));
      win_gizmo_set_flag(gz, WIN_GIZMO_HIDDEN, false);
    }

    if ((RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_LOCATION) == 0) {
      gz = navgroup->gz_array[GZ_INDEX_MOVE];
      gz->matrix_basis[3][0] = roundf(co[0]);
      gz->matrix_basis[3][1] = roundf(co[1] - (icon_offset_mini * icon_mini_slot++));
      win_gizmo_set_flag(gz, WIN_GIZMO_HIDDEN, false);
    }

    if ((RV3D_LOCK_FLAGS(rv3d) & RV3D_LOCK_ROTATION) == 0) {
      gz = navgroup->gz_array[GZ_INDEX_CAMERA];
      gz->matrix_basis[3][0] = roundf(co[0]);
      gz->matrix_basis[3][1] = roundf(co[1] - (icon_offset_mini * icon_mini_slot++));
      win_gizmo_set_flag(gz, WIN_GIZMO_HIDDEN, false);

      if (navgroup->state.rv3d.is_camera == false) {
        gz = navgroup->gz_array[rv3d->is_persp ? GZ_INDEX_PERSP : GZ_INDEX_ORTHO];
        gz->matrix_basis[3][0] = roundf(co[0]);
        gz->matrix_basis[3][1] = roundf(co[1] - (icon_offset_mini * icon_mini_slot++));
        win_gizmo_set_flag(gz, WIN_GIZMO_HIDDEN, false);
      }
    }
  }
}

void VIEW3D_GGT_nav(WinGizmoGroupType *gzgt)
{
  gzgt->name = "View3D Nav";
  gzgt->idname = "VIEW3D_GGT_nav";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_PERSISTENT | WIN_GIZMOGROUPTYPE_SCALE |
                 WIN_GIZMOGROUPTYPE_DRAW_MODAL_ALL);

  gzgt->poll = WIDGETGROUP_nav_poll;
  gzgt->setup = WIDGETGROUP_nav_setup;
  gzgt->draw_prepare = WIDGETGROUP_nav_draw_prepare;
}
