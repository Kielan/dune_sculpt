#include "mem_guardedalloc.h"

#include "lib_utildefines.h"

#include "dune_cxt.h"

#include "ed_gizmo_utils.h"
#include "ed_screen.h"

#include "ui_resources.h"

#include "win_api.h"
#include "win_types.h"

#include "view3d_intern.h" /* own include */

/* Mesh Pre-Select Element Gizmo */
struct GizmoGroupPreSelElem {
  WinGizmo *gizmo;
};

static void WIDGETGROUP_mesh_preselect_elem_setup(const Cxt *UNUSED(C), WinGizmoGroup *gzgroup)
{
  const WinGizmoType *gzt_presel = win_gizmotype_find("GIZMO_GT_mesh_preselect_elem_3d", true);
  struct GizmoGroupPreSelElem *ggd = mem_calloc(sizeof(struct GizmoGroupPreSelElem), __func__);
  gzgroup->customdata = ggd;

  WinGizmo *gz = ggd->gizmo = win_gizmo_new_ptr(gzt_presel, gzgroup, NULL);
  ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
  ui_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);
}

void VIEW3D_GGT_mesh_preselect_elem(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Mesh Preselect Element";
  gzgt->idname = "VIEW3D_GGT_mesh_preselect_elem";

  gzgt->flag = WIN_GIZMOGROUPTYPE_TOOL_FALLBACK_KEYMAP | WIN_GIZMOGROUPTYPE_3D;

  gzgt->gzmap_params.spaceid = SPACE_VIEW3D;
  gzgt->gzmap_params.rgnid = RGN_TYPE_WIN;

  gzgt->poll = ed_gizmo_poll_or_unlink_delayed_from_tool;
  gzgt->setup = WIDGETGROUP_mesh_preselect_elem_setup;
}

/* Mesh Pre-Select Edge Ring Gizmo */
struct GizmoGroupPreSelEdgeRing {
  WinGizmo *gizmo;
};

static void WIDGETGROUP_mesh_preselect_edgering_setup(const Cxt *UNUSED(C),
                                                      WinGizmoGroup *gzgroup)
{
  const WinGizmoType *gzt_presel = win_gizmotype_find("GIZMO_GT_mesh_preselect_edgering_3d", true);
  struct GizmoGroupPreSelEdgeRing *ggd = mem_calloc(sizeof(struct GizmoGroupPreSelEdgeRing),
                                                     __func__);
  gzgroup->customdata = ggd;

  WinGizmo *gz = ggd->gizmo = win_gizmo_new_ptr(gzt_presel, gzgroup, NULL);
  ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
  ui_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);
}

void VIEW3D_GGT_mesh_preselect_edgering(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Mesh Preselect Edge Ring";
  gzgt->idname = "VIEW3D_GGT_mesh_preselect_edgering";

  gzgt->flag = WIN_GIZMOGROUPTYPE_TOOL_FALLBACK_KEYMAP | WIN_GIZMOGROUPTYPE_3D;

  gzgt->gzmap_params.spaceid = SPACE_VIEW3D;
  gzgt->gzmap_params.rgnid = RGN_TYPE_WIN;

  gzgt->poll = ed_gizmo_poll_or_unlink_delayed_from_tool;
  gzgt->setup = WIDGETGROUP_mesh_preselect_edgering_setup;
}
