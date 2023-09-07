/** Creation gizmos. **/

#include "mem_guardedalloc.h"

#include "lib_math.h"

#include "types_object.h"
#include "types_scene.h"

#include "dune_cxt.h"
#include "dune_editmesh.h"
#include "dune_scene.h"

#include "ed_gizmo_lib.h"
#include "ed_gizmo_utils.h"
#include "ed_mesh.h"
#include "ed_object.h"
#include "ed_screen.h"
#include "ed_undo.h"
#include "ed_view3d.h"

#include "api_access.h"
#include "api_define.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ui_resources.h"

#include "lang.h"

#include "mesh_intern.h" /* own include */

/* Helper Functions **/

/* When we place a shape, pick a plane.
 *
 * We may base this choice on context,
 * for now pick the "ground" based on the 3D cursor's dominant plane
 * pointing down relative to the view. */
static void calc_initial_placement_point_from_view(Cxt *C,
                                                   const float mval[2],
                                                   float r_location[3],
                                                   float r_rotation[3][3])
{

  Scene *scene = cxt_data_scene(C);
  ARegion *region = cxt_wm_region(C);
  RegionView3D *rv3d = region->regiondata;

  bool use_mouse_project = true; /* TODO: make optional */

  float cursor_matrix[4][4];
  float orient_matrix[3][3];
  dune_scene_cursor_to_mat4(&scene->cursor, cursor_matrix);

  const float dots[3] = {
      dot_v3v3(rv3d->viewinv[2], cursor_matrix[0]),
      dot_v3v3(rv3d->viewinv[2], cursor_matrix[1]),
      dot_v3v3(rv3d->viewinv[2], cursor_matrix[2]),
  };
  const int axis = axis_dominant_v3_single(dots);

  copy_v3_v3(orient_matrix[0], cursor_matrix[(axis + 1) % 3]);
  copy_v3_v3(orient_matrix[1], cursor_matrix[(axis + 2) % 3]);
  copy_v3_v3(orient_matrix[2], cursor_matrix[axis]);

  if (dot_v3v3(rv3d->viewinv[2], orient_matrix[2]) < 0.0f) {
    negate_v3(orient_matrix[2]);
  }
  if (is_negative_m3(orient_matrix)) {
    swap_v3_v3(orient_matrix[0], orient_matrix[1]);
  }

  if (use_mouse_project) {
    float plane[4];
    plane_from_point_normal_v3(plane, cursor_matrix[3], orient_matrix[2]);
    if (ed_view3d_win_to_3d_on_plane(region, plane, mval, true, r_location)) {
      copy_m3_m3(r_rotation, orient_matrix);
      return;
    }
  }

  /* fallback */
  copy_v3_v3(r_location, cursor_matrix[3]);
  copy_m3_m3(r_rotation, orient_matrix);
}

/** Placement Gizmo **/
typedef struct GizmoPlacementGroup {
  struct wmGizmo *cage;
  struct {
    DuneContext *context;
    wmOperator *op;
    PropAPI *prop_matrix;
  } data;
} GizmoPlacementGroup;

/* warning Calling redo from property updates is not great.
 * This is needed because changing the API doesn't cause a redo
 * and we're not using operator UI which does just this. */
static void gizmo_placement_exec(GizmoPlacementGroup *ggd)
{
  wmOp *op = ggd->data.op;
  if (op == wm_op_last_redo((Cxt *)ggd->data.cxt)) {
    ed_undo_op_repeat((Cxt *)ggd->data.cxt, op);
  }
}

static void gizmo_mesh_placement_update_from_op(GizmoPlacementGroup *ggd)
{
  wmOp *op = ggd->data.op;
  UNUSED_VARS(op);
  /* For now don't read back from the op. */
#if 0
  api_prop_float_get_array(op->ptr, ggd->data.prop_matrix, &ggd->cage->matrix_offset[0][0]);
#endif
}

/* translate callbacks */
static void gizmo_placement_prop_matrix_get(const wmGizmo *gz,
                                            wmGizmoProp *gz_prop,
                                            void *value_p)
{
  GizmoPlacementGroup *ggd = gz->parent_gzgroup->customdata;
  wmOp *op = ggd->data.op;
  float *value = value_p;
  LI!_assert(gz_prop->type->array_length == 16);
  UNUSED_VARS_NDEBUG(gz_prop);

  if (value_p != ggd->cage->matrix_offset) {
    mul_m4_m4m4(value_p, ggd->cage->matrix_basis, ggd->cage->matrix_offset);
    API_prop_float_get_array(op->ptr, ggd->data.prop_matrix, value);
  }
}

static void gizmo_placement_prop_matrix_set(const wmGizmo *gz,
                                            wmGizmoProp *gz_prop,
                                            const void *value)
{
  GizmoPlacementGroup *ggd = gz->parent_gzgroup->customdata;
  wmOp *op = ggd->data.op;

  lib_assert(gz_prop->type->array_length == 16);
  UNUSED_VARS_NDEBUG(gz_prop);

  float mat[4][4];
  mul_m4_m4m4(mat, ggd->cage->matrix_basis, value);

  if (is_negative_m4(mat)) {
    negate_mat3_m4(mat);
  }

  api_prop_float_set_array(op->ptr, ggd->data.prop_matrix, &mat[0][0]);

  gizmo_placement_exec(ggd);
}

static bool gizmo_mesh_placement_poll(const Cxt *C, wmGizmoGroupType *gzgt)
{
  return ed_gizmo_poll_or_unlink_delayed_from_op(
      C, gzgt, "MESH_OT_primitive_cube_add_gizmo");
}

static void gizmo_mesh_placement_modal_from_setup(const Cxt *C, wmGizmoGroup *gzgroup)
{
  GizmoPlacementGroup *ggd = gzgroup->customdata;

  /* Initial size. */
  {
    wmGizmo *gz = ggd->cage;
    zero_m4(gz->matrix_offset);

    /* TODO: support zero scaled matrix in 'GIZMO_GT_cage_3d'. */
    gz->matrix_offset[0][0] = 0.01;
    gz->matrix_offset[1][1] = 0.01;
    gz->matrix_offset[2][2] = 0.01;
    gz->matrix_offset[3][3] = 1.0f;
  }

  /* Start off dragging. */
  {
    wmWindow *win = cxt_wm_window(C);
    ARegion *region = cxt_wm_region(C);
    wmGizmo *gz = ggd->cage;

    {
      float mat3[3][3];
      float location[3];
      calc_initial_placement_point_from_view((DuneContext *)C,
                                             (float[2]){
                                                 win->eventstate->xy[0] - region->winrct.xmin,
                                                 win->eventstate->xy[1] - region->winrct.ymin,
                                             },
                                             location,
                                             mat3);
      copy_m4_m3(gz->matrix_basis, mat3);
      copy_v3_v3(gz->matrix_basis[3], location);
    }

    if (1) {
      wmGizmoMap *gzmap = gzgroup->parent_gzmap;
      wm_gizmo_modal_set_from_setup(gzmap,
                                    (Cxt *)C,
                                    ggd->cage,
                                    ED_GIZMO_CAGE3D_PART_SCALE_MAX_X_MAX_Y_MAX_Z,
                                    win->eventstate);
    }
  }
}

static void gizmo_mesh_placement_setup(const Cxt *C, wmGizmoGroup *gzgroup)
{
  wmOp *op = wm_op_last_redo(C);

  if (op == NULL || !STREQ(op->type->idname, "MESH_OT_primitive_cube_add_gizmo")) {
    return;
  }

  struct GizmoPlacementGroup *ggd = mem_callocn(sizeof(GizmoPlacementGroup), __func__);
  gzgroup->customdata = ggd;

  const wmGizmoType *gzt_cage = WM_gizmotype_find("GIZMO_GT_cage_3d", true);

  ggd->cage = wm_gizmo_new_ptr(gzt_cage, gzgroup, NULL);

  ui_GetThemeColor3fv(TH_GIZMO_PRIMARY, ggd->cage->color);

  api_enum_set(ggd->cage->ptr,
               "transform",
               ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE | ED_GIZMO_CAGE2D_XFORM_FLAG_TRANSLATE |
                   ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE_SIGNED);

  wm_gizmo_set_flag(ggd->cage, WM_GIZMO_DRAW_VALUE, true);

  ggd->data.context = (Cxt *)C;
  ggd->data.op = op;
  ggd->data.prop_matrix = api_struct_find_prop(op->ptr, "matrix");

  gizmo_mesh_placement_update_from_op(ggd);

  /* Setup property callbacks */
  {
    wm_gizmo_target_prop_def_func(ggd->cage,
                                      "matrix",
                                      &(const struct wmGizmoPropFnParams){
                                          .value_get_fn = gizmo_placement_prop_matrix_get,
                                          .value_set_fn = gizmo_placement_prop_matrix_set,
                                          .range_get_fn = NULL,
                                          .user_data = NULL,
                                      });
  }

  gizmo_mesh_placement_modal_from_setup(C, gzgroup);
}

static void gizmo_mesh_placement_draw_prepare(const Cxt *UNUSED(C), wmGizmoGroup *gzgroup)
{
  GizmoPlacementGroup *ggd = gzgroup->customdata;
  if (ggd->data.op->next) {
    ggd->data.op = WM_op_last_redo((Cxt *)ggd->data.cxt);
  }
  gizmo_mesh_placement_update_from_op(ggd);
}

static void mesh_ggt_add_bounds(struct wmGizmoGroupType *gzgt)
{
  gzgt->name = "Mesh Add Bounds";
  gzgt->idname = "MESH_GGT_add_bounds";

  gzgt->flag = WM_GIZMOGROUPTYPE_3D;

  gzgt->gzmap_params.spaceid = SPACE_VIEW3D;
  gzgt->gzmap_params.regionid = RGN_TYPE_WINDOW;

  gzgt->poll = gizmo_mesh_placement_poll;
  gzgt->setup = gizmo_mesh_placement_setup;
  gzgt->draw_prepare = gizmo_mesh_placement_draw_prepare;
}

/* Add Cube Gizmo-Operator
 * For now we use a separate op to add a cube,
 * we can try to merge then however they are invoked differently
 * and share the same Mesh creation code. */

static int add_primitive_cube_gizmo_ex(Cxt *C, wmOp *op)
{
  Object *obedit = cxt_data_edit_object(C);
  DuneMeshEdit *dme = dune_editmesh_from_object(obedit);
  float matrix[4][4];

  /* Get the matrix that defines the cube bounds (as set by the gizmo cage). */
  {
    PropAPI *prop_matrix = api_struct_find_prop(op->ptr, "matrix");
    if (api_prop_is_set(op->ptr, prop_matrix)) {
      api_prop_float_get_array(op->ptr, prop_matrix, &matrix[0][0]);
      invert_m4_m4(obedit->imat, obedit->obmat);
      mul_m4_m4m4(matrix, obedit->imat, matrix);
    }
    else {
      /* For the first update the widget may not set the matrix. */
      return OP_FINISHED;
    }
  }

  const bool calc_uvs = api_bool_get(op->ptr, "calc_uvs");

  if (calc_uvs) {
    ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!mesh_op_call_and_selectf(em,
                                op,
                                "verts.out",
                                false,
                                "create_cube matrix=%m4 size=%f calc_uvs=%b",
                                matrix,
                                1.0f,
                                calc_uvs)) {
    return OP_CANCELLED;
  }

  mesh_selectmode_flush_ex(em, SCE_SELECT_VERTEX);
  mesh_update(obedit->data,
              &(const struct EDBMUpdate_Params){
                  .calc_looptri = true,
                  .calc_normals = false,
                  .is_destructive = true,
              });

  return OP_FINISHED;
}

static int add_primitive_cube_gizmo_invoke(Cxt *C,
                                           wmOp *op,
                                           const wmEvent *UNUSED(event))
{
  View3D *v3d = cxt_wm_view3d(C);

  int ret = add_primitive_cube_gizmo_ex(C, op);
  if (ret & OP_FINISHED) {
    /* Setup gizmos */
    if (v3d && ((v3d->gizmo_flag & V3D_GIZMO_HIDE) == 0)) {
      wmGizmoGroupType *gzgt = wm_gizmogrouptype_find("MESH_GGT_add_bounds", false);
      if (!wm_gizmo_group_type_ensure_ptr(gzgt)) {
        struct Main *duneMain = cxt_data_main(C);
        wm_gizmo_group_type_reinit_ptr(duneMain, gzgt);
      }
    }
  }

  return ret;
}

void MESH_OT_primitive_cube_add_gizmo(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Cube";
  ot->description = "Construct a cube mesh";
  ot->idname = "MESH_OT_primitive_cube_add_gizmo";

  /* api callbacks */
  ot->invoke = add_primitive_cube_gizmo_invoke;
  ot->ex = add_primitive_cube_gizmo_ex;
  ot->poll = ed_op_editmesh_view3d;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);

  /* hidden props */
  ApiProp *prop = api_def_float_matrix(
      ot->api, "matrix", 4, 4, NULL, 0.0f, 0.0f, "Matrix", "", 0.0f, 0.0f);
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  wm_gizmogrouptype_append(MESH_GGT_add_bounds);
}
