#include "lib_utildefines.h"

#include "dune_cxt.h"
#include "dune_layer.h"

#include "types_obj_force.h"
#include "types_obj.h"

#include "ed_gizmo_lib.h"
#include "ed_screen.h"

#include "ui_resources.h"

#include "mem_guardedalloc.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "win_api.h"
#include "win_types.h"

#include "view3d_intern.h" /* own include */

/* Force Field Gizmos */
static bool WIDGETGROUP_forcefield_poll(const Cxt *C, WinGizmoGroupType *UNUSED(gzgt))
{
  View3D *v3d = cxt_win_view3d(C);

  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CXT)) {
    return false;
  }
  if ((v3d->gizmo_show_empty & V3D_GIZMO_SHOW_EMPTY_FORCE_FIELD) == 0) {
    return false;
  }

  ViewLayer *view_layer = cxt_data_view_layer(C);
  Base *base = BASACT(view_layer);
  if (base && BASE_SELECTABLE(v3d, base)) {
    Obj *ob = base->obj;
    if (ob->pd && ob->pd->forcefield) {
      return true;
    }
  }
  return false;
}

static void WIDGETGROUP_forcefield_setup(const Cxt *UNUSED(C), WinGizmoGroup *gzgroup)
{
  /* only wind effector for now */
  WinGizmoWrapper *wwrapper = mem_malloc(sizeof(WinGizmoWrapper), __func__);
  gzgroup->customdata = wwrapper;

  wwrapper->gizmo = win_gizmo_new("GIZMO_GT_arrow_3d", gzgroup, NULL);
  WinGizmo *gz = wwrapper->gizmo;
  api_enum_set(gz->ptr, "transform", ED_GIZMO_ARROW_XFORM_FLAG_CONSTRAINED);
  ed_gizmo_arrow3d_set_ui_range(gz, -200.0f, 200.0f);
  ed_gizmo_arrow3d_set_range_fac(gz, 6.0f);

  ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
  ui_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);
}

static void WIDGETGROUP_forcefield_refresh(const Cxt *C, WinGizmoGroup *gzgroup)
{
  WinGizmoWrapper *wwrapper = gzgroup->customdata;
  WinGizmo *gz = wwrapper->gizmo;
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Obj *ob = OBACT(view_layer);
  PartDeflect *pd = ob->pd;

  if (pd->forcefield == PFIELD_WIND) {
    const float size = (ob->type == OB_EMPTY) ? ob->empty_drawsize : 1.0f;
    const float ofs[3] = {0.0f, -size, 0.0f};
    ApiPtr field_ptr;

    api_ptr_create(&ob->id, &ApiFieldSettings, pd, &field_ptr);
    win_gizmo_set_matrix_location(gz, ob->obmat[3]);
    win_gizmo_set_matrix_rotation_from_z_axis(gz, ob->obmat[2]);
    win_gizmo_set_matrix_offset_location(gz, ofs);
    win_gizmo_set_flag(gz, WIN_GIZMO_HIDDEN, false);
    win_gizmo_target_prop_def_api(gz, "offset", &field_ptr, "strength", -1);
  }
  else {
    win_gizmo_set_flag(gz, WIN_GIZMO_HIDDEN, true);
  }
}

void VIEW3D_GGT_force_field(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Force Field Widgets";
  gzgt->idname = "VIEW3D_GGT_force_field";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_PERSISTENT | WIN_GIZMOGROUPTYPE_3D | WIN_GIZMOGROUPTYPE_SCALE |
                 WIN_GIZMOGROUPTYPE_DEPTH_3D);

  gzgt->poll = WIDGETGROUP_forcefield_poll;
  gzgt->setup = WIDGETGROUP_forcefield_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = WIDGETGROUP_forcefield_refresh;
}
