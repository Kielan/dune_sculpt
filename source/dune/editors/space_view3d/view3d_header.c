#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types_pen.h"
#include "types_obj.h"
#include "types_scene.h"

#include "lib_math_base.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_cxt.h"
#include "dune_editmesh.h"

#include "graph.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "win_api.h"
#include "win_types.h"

#include "ed_mesh.h"
#include "ed_undo.h"

#include "ui.h"
#include "ui_resources.h"

#include "view3d_intern.h"

#define B_SEL_VERT 110
#define B_SEL_EDGE 111
#define B_SEL_FACE 112

/* Toggle Matcap Flip Op */
static int toggle_matcap_flip(Cxt *C, WinOp *UNUSED(op))
{
  View3D *v3d = cxt_win_view3d(C);

  if (v3d) {
    v3d->shading.flag ^= V3D_SHADING_MATCAP_FLIP_X;
    ed_view3d_shade_update(cxt_data_main(C), v3d, cxt_win_area(C));
    win_ev_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, v3d);
  }
  else {
    Scene *scene = cxt_data_scene(C);
    scene->display.shading.flag ^= V3D_SHADING_MATCAP_FLIP_X;
    graph_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
    win_ev_add_notifier(C, NC_SCENE | ND_RENDER_OPTIONS, scene);
  }

  return OP_FINISHED;
}

void VIEW3D_OT_toggle_matcap_flip(WinOpType *ot)
{
  /* ids */
  ot->name = "Flip MatCap";
  ot->description = "Flip MatCap";
  ot->idname = "VIEW3D_OT_toggle_matcap_flip";

  /* api cbs */
  ot->ex = toggle_matcap_flip;
}

/* UI Templates */

void uiTemplateEditModeSelection(uiLayout *layout, struct Cxt *C)
{
  Object *obedit = cxt_data_edit_obj(C);
  if (!obedit || obedit->type != OB_MESH) {
    return;
  }

  MEditMesh *em = dune_editmesh_from_obj(obedit);
  uiLayout *row = uiLayoutRow(layout, true);

  ApiPtr op_ptr;
  WinOpType *ot = win_optype_find("MESH_OT_sel_mode", true);
  uiItemFullO_ptr(row,
                  ot,
                  "",
                  ICON_VERTEXSEL,
                  NULL,
                  WIN_OP_INVOKE_DEFAULT,
                  (em->selmode & SCE_SEL_VERTEX) ? UI_ITEM_O_DEPRESS : 0,
                  &op_ptr);
  api_enum_set(&op_ptr, "type", SCE_SEL_VERTEX);
  uiItemFullO_ptr(row,
                  ot,
                  "",
                  ICON_EDGESEL,
                  NULL,
                  WIN_OP_INVOKE_DEFAULT,
                  (em->selmode & SCE_SEL_EDGE) ? UI_ITEM_O_DEPRESS : 0,
                  &op_ptr);
  api_enum_set(&op_ptr, "type", SCE_SEL_EDGE);
  uiItemFullO_ptr(row,
                  ot,
                  "",
                  ICON_FACESEL,
                  NULL,
                  WIN_OP_INVOKE_DEFAULT,
                  (em->selmode & SCE_SEL_FACE) ? UI_ITEM_O_DEPRESS : 0,
                  &op_ptr);
  api_enum_set(&op_ptr, "type", SCE_SEL_FACE);
}

static void uiTemplatePaintModeSelection(uiLayout *layout, struct Cxt *C)
{
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Obj *ob = OBACT(view_layer);

  /* Gizmos aren't used in paint modes */
  if (!ELEM(ob->mode, OB_MODE_SCULPT, OB_MODE_PARTICLE_EDIT)) {
    /* masks aren't used for sculpt and particle painting */
    ApiPtr meshptr;

    api_ptr_create(ob->data, &ApiMesh, ob->data, &meshptr);
    if (ob->mode & OB_MODE_TEXTURE_PAINT) {
      uiItemR(layout, &meshptr, "use_paint_mask", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);
    }
    else {
      uiLayout *row = uiLayoutRow(layout, true);
      uiItemR(row, &meshptr, "use_paint_mask", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);
      uiItemR(row, &meshptr, "use_paint_mask_vertex", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);
    }
  }
}

void uiTemplateHeader3D_mode(uiLayout *layout, struct Cxt *C)
{
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Obj *ob = OBACT(view_layer);
  Obj *obedit = cxt_data_edit_obj(C);
  PenData *pdata = cxt_data_pen_data(C);

  bool is_paint = (ob && !(pdata && (gpd->flag & PEN_DATA_STROKE_EDITMODE)) &&
                   ELEM(ob->mode,
                        OB_MODE_SCULPT,
                        OB_MODE_VERTEX_PAINT,
                        OB_MODE_WEIGHT_PAINT,
                        OB_MODE_TEXTURE_PAINT));

  uiTemplateEditModeSelection(layout, C);
  if ((obedit == NULL) && is_paint) {
    uiTemplatePaintModeSelection(layout, C);
  }
}
