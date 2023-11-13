#include "lib_math.h"
#include "lib_utildefines.h"

#include "dune_cxt.h"
#include "dune_image.h"
#include "dune_layer.h"
#include "dune_obj.h"

#include "graph.h"

#include "types_light.h"
#include "types_obj.h"

#include "ed_gizmo_lib.h"
#include "ed_screen.h"

#include "ui_resources.h"

#include "mem_guardedalloc.h"

#include "api_access.h"

#include "win_api.h"
#include "win_types.h"

#include "view3d_intern.h" /* own include */

/* Empty Image Gizmos */
struct EmptyImgWidgetGroup {
  WinGizmo *gizmo;
  struct {
    Obj *ob;
    float dims[2];
  } state;
};

/* translate cbs */
static void gizmo_empty_image_prop_matrix_get(const WinGizmo *gz,
                                              WinGizmoProp *gz_prop,
                                              void *value_p)
{
  float(*matrix)[4] = value_p;
  lib_assert(gz_prop->type->array_length == 16);
  struct EmptyImageWidgetGroup *igzgroup = gz_prop->custom_fn.user_data;
  const Object *ob = igzgroup->state.ob;

  unit_m4(matrix);
  matrix[0][0] = ob->empty_drawsize;
  matrix[1][1] = ob->empty_drawsize;

  float dims[2] = {0.0f, 0.0f};
  api_float_get_array(gz->ptr, "dimensions", dims);
  dims[0] *= ob->empty_drawsize;
  dims[1] *= ob->empty_drawsize;

  matrix[3][0] = (ob->ima_ofs[0] * dims[0]) + (0.5f * dims[0]);
  matrix[3][1] = (ob->ima_ofs[1] * dims[1]) + (0.5f * dims[1]);
}

static void gizmo_empty_img_prop_matrix_set(const WinGizmo *gz,
                                            WinGizmoProp *gz_prop,
                                            const void *val_p)
{
  const float(*matrix)[4] = val_p;
  lib_assert(gz_prop->type->array_length == 16);
  struct EmptyImgWidgetGroup *igzgroup = gz_prop->custom_fn.user_data;
  Obj *ob = igzgroup->state.ob;

  ob->empty_drawsize = matrix[0][0];
  graph_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);

  float dims[2];
  api_float_get_array(gz->ptr, "dimensions", dims);
  dims[0] *= ob->empty_drawsize;
  dims[1] *= ob->empty_drawsize;

  ob->ima_ofs[0] = (matrix[3][0] - (0.5f * dims[0])) / dims[0];
  ob->ima_ofs[1] = (matrix[3][1] - (0.5f * dims[1])) / dims[1];
}

static bool WIDGETGROUP_empty_img_poll(const Cxt *C, WinGizmoGroupType *UNUSED(gzgt))
{
  View3D *v3d = cxt_win_view3d(C);
  RgnView3D *rv3d = cxt_win_rgn_view3d(C);

  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CXT)) {
    return false;
  }
  if ((v3d->gizmo_show_empty & V3D_GIZMO_SHOW_EMPTY_IMAGE) == 0) {
    return false;
  }

  ViewLayer *view_layer = cxt_data_view_layer(C);
  Base *base = BASACT(view_layer);
  if (base && BASE_SELECTABLE(v3d, base)) {
    Obj *ob = base->obj;
    if (ob->type == OB_EMPTY) {
      if (ob->empty_drawtype == OB_EMPTY_IMG) {
        return dune_obj_empty_img_frame_is_visible_in_view3d(ob, rv3d);
      }
    }
  }
  return false;
}

static void WIDGETGROUP_empty_img_setup(const Cxt *UNUSED(C), WinGizmoGroup *gzgroup)
{
  struct EmptyImgWidgetGroup *igzgroup = mem_malloc(sizeof(struct EmptyImgWidgetGroup),
                                                       __func__);
  igzgroup->gizmo = win_gizmo_new("GIZMO_GT_cage_2d", gzgroup, NULL);
  WinGizmo *gz = igzgroup->gizmo;
  api_enum_set(gz->ptr, "transform", ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE);

  gzgroup->customdata = igzgroup;

  win_gizmo_set_flag(gz, WIN_GIZMO_DRAW_HOVER, true);

  ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
  ui_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);
}

static void WIDGETGROUP_empty_img_refresh(const Cxt *C, WinGizmoGroup *gzgroup)
{
  struct EmptyImgWidgetGroup *igzgroup = gzgroup->customdata;
  WinGizmo *gz = igzgroup->gizmo;
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Obj *ob = OBACT(view_layer);

  copy_m4_m4(gz->matrix_basis, ob->obmat);

  api_enum_set(gz->ptr,
               "transform",
               ED_GIZMO_CAGE2D_XFORM_FLAG_TRANSLATE | ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE |
               ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE_UNIFORM);

  igzgroup->state.ob = ob;

  /* Use dimensions for aspect */
  if (ob->data != NULL) {
    const Img *img = ob->data;
    ImgUser iuser = *ob->iuser;
    float size[2];
    dune_img_get_size_fl(ob->data, &iuser, size);

    /* Get the image aspect even if the buffer is invalid */
    if (img->aspx > img->aspy) {
      size[1] *= img->aspy / img->aspx;
    }
    else if (img->aspx < img->aspy) {
      size[0] *= img->aspx / img->aspy;
    }

    const float dims_max = max_ff(size[0], size[1]);
    igzgroup->state.dims[0] = size[0] / dims_max;
    igzgroup->state.dims[1] = size[1] / dims_max;
  }
  else {
    copy_v2_fl(igzgroup->state.dims, 1.0f);
  }
  api_float_set_array(gz->ptr, "dimensions", igzgroup->state.dims);

  win_gizmo_target_prop_def_fn(gz,
                               "matrix",
                               &(const struct WinGizmoPropFnParams){
                                 .val_get_fn = gizmo_empty_img_prop_matrix_get,
                                 .val_set_fn = gizmo_empty_img_prop_matrix_set,
                                 .range_get_fn = NULL,
                                 .user_data = igzgroup,
                              });
}

void VIEW3D_GGT_empty_img(WinGizmoGroupType *gzgt)
{
  gzgt->name = "Area Light Widgets";
  gzgt->idname = "VIEW3D_GGT_empty_img";

  gzgt->flag |= (WIN_GIZMOGROUPTYPE_PERSISTENT | WIN_GIZMOGROUPTYPE_3D | WIN_GIZMOGROUPTYPE_DEPTH_3D);

  gzgt->poll = WIDGETGROUP_empty_img_poll;
  gzgt->setup = WIDGETGROUP_empty_img_setup;
  gzgt->setup_keymap = win_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = WIDGETGROUP_empty_img_refresh;
}
