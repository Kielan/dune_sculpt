#include "lib_math.h"
#include "lib_sys_types.h"

#include "types_object.h"
#include "types_scene.h"

#include "lang.h"

#include "dune_cxt.h"
#include "dune_editmesh.h"

#include "api_access.h"
#include "api_define.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ed_mesh.h"
#include "ed_object.h"
#include "ed_screen.h"

#include "mesh_intern.h" /* own include */

#define MESH_ADD_VERTS_MAXI 10000000

/* add primitive operators */

typedef struct MakePrimitiveData {
  float mat[4][4];
  bool was_editmode;
} MakePrimitiveData;

static Object *make_prim_init(Cxt *C,
                              const char *idname,
                              const float loc[3],
                              const float rot[3],
                              const float scale[3],
                              ushort local_view_bits,
                              MakePrimitiveData *r_creation_data)
{
  struct Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  Object *obedit = cxt_data_edit_object(C);

  r_creation_data->was_editmode = false;
  if (obedit == NULL || obedit->type != OB_MESH) {
    obedit = ed_object_add_type(C, OB_MESH, idname, loc, rot, false, local_view_bits);
    ed_object_editmode_enter_ex(main, scene, obedit, 0);

    r_creation_data->was_editmode = true;
  }

  ed_object_new_primitive_matrix(C, obedit, loc, rot, scale, r_creation_data->mat);

  return obedit;
}

static void make_prim_finish(Cxt *C,
                             Object *obedit,
                             const MakePrimitiveData *creation_data,
                             int enter_editmode)
{
  DuneMeshEdit *dme = dune_editmesh_from_object(obedit);
  const bool exit_editmode = ((creation_data->was_editmode == true) && (enter_editmode == false));

  /* Primitive has all verts selected, use vert select flush
   * to push this up to edges & faces. */
  meshedit_selectmode_flush_ex(em, SCE_SELECT_VERTEX);

  /* only recalc editmode tessface if we are staying in editmode */
  meshedit_update(obedit->data,
              &(const struct EDBMUpdate_Params){
                  .calc_looptri = !exit_editmode,
                  .calc_normals = false,
                  .is_destructive = true,
              });

  /* userdef */
  if (exit_editmode) {
    ed_object_editmode_exit_ex(cxt_data_main(C), cxt_data_scene(C), obedit, EM_FREEDATA);
  }
  wm_event_add_notifier(C, NC_OBJECT | ND_DRAW, obedit);
}

static int add_primitive_plane_exec(Cxt *C, wmOp *op)
{
  MakePrimitiveData creation_data;
  Object *obedit;
  DuneMeshEdit *dme;
  float loc[3], rot[3];
  bool enter_editmode;
  ushort local_view_bits;
  const bool calc_uvs = api_bool_get(op->ptr, "calc_uvs");

  wm_op_view3d_unit_defaults(C, op);
  ed_object_add_generic_get_opts(
      C, op, 'Z', loc, rot, NULL, &enter_editmode, &local_view_bits, NULL);
  obedit = make_prim_init(C,
                          CXT_DATA_(LANG_CXT_ID_MESH, "Plane"),
                          loc,
                          rot,
                          NULL,
                          local_view_bits,
                          &creation_data);

  em = dune_editmesh_from_object(obedit);

  if (calc_uvs) {
    ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!MESH_EDIT_op_call_and_selectf(
          em,
          op,
          "verts.out",
          false,
          "create_grid x_segments=%i y_segments=%i size=%f matrix=%m4 calc_uvs=%b",
          0,
          0,
          api_float_get(op->ptr, "size") / 2.0f,
          creation_data.mat,
          calc_uvs)) {
    return OP_CANCELLED;
  }

  make_prim_finish(C, obedit, &creation_data, enter_editmode);

  return OP_FINISHED;
}

void MESH_OT_primitive_plane_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Plane";
  ot->description = "Construct a filled planar mesh with 4 vertices";
  ot->idname = "MESH_OT_primitive_plane_add";

  /* api callbacks */
  ot->ex = add_primitive_plane_ex;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ed_object_add_unit_props_size(ot);
  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);
}

static int add_primitive_cube_exec(Cxt *C, wmOp *op)
{
  MakePrimitiveData creation_data;
  Object *obedit;
  DuneMeshEdit *dme;
  float loc[3], rot[3], scale[3];
  bool enter_editmode;
  ushort local_view_bits;
  const bool calc_uvs = api_bool_get(op->ptr, "calc_uvs");

  wm_op_view3d_unit_defaults(C, op);
  ed_object_add_generic_get_opts(
      C, op, 'Z', loc, rot, scale, &enter_editmode, &local_view_bits, NULL);
  obedit = make_prim_init(C,
                          CXT_DATA_(LANG_CXT_ID_MESH, "Cube"),
                          loc,
                          rot,
                          scale,
                          local_view_bits,
                          &creation_data);

  em = dune_editmesh_from_object(obedit);

  if (calc_uvs) {
    ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!MESH_EDIT_op_call_and_selectf(em,
                                op,
                                "verts.out",
                                false,
                                "create_cube matrix=%m4 size=%f calc_uvs=%b",
                                creation_data.mat,
                                api_float_get(op->ptr, "size"),
                                calc_uvs)) {
    return OP_CANCELLED;
  }

  /* DUNEMESH_TODO make plane side this: M_SQRT2 - plane (diameter of 1.41 makes it unit size) */
  make_prim_finish(C, obedit, &creation_data, enter_editmode);

  return OP_FINISHED;
}

void MESH_OT_primitive_cube_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Cube";
  ot->description = "Construct a cube mesh";
  ot->idname = "MESH_OT_primitive_cube_add";

  /* api callbacks */
  ot->exec = add_primitive_cube_ex;
  ot->poll = ED_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ed_object_add_unit_props_size(ot);
  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);
}

static const EnumPropItem fill_type_items[] = {
    {0, "NOTHING", 0, "Nothing", "Don't fill at all"},
    {1, "NGON", 0, "N-Gon", "Use n-gons"},
    {2, "TRIFAN", 0, "Triangle Fan", "Use triangle fans"},
    {0, NULL, 0, NULL, NULL},
};

static int add_primitive_circle_ex(Cxt *C, wmOp *op)
{
  MakePrimitiveData creation_data;
  Object *obedit;
  DuneMeshEdit *dme;
  float loc[3], rot[3];
  bool enter_editmode;
  ushort local_view_bits;
  int cap_end, cap_tri;
  const bool calc_uvs = api_bool_get(op->ptr, "calc_uvs");

  cap_end = api_enum_get(op->ptr, "fill_type");
  cap_tri = (cap_end == 2);

  wm_op_view3d_unit_defaults(C, op);
  ed_object_add_generic_get_opts(
      C, op, 'Z', loc, rot, NULL, &enter_editmode, &local_view_bits, NULL);
  obedit = make_prim_init(C,
                          CXT_DATA_(LANG_CXT_ID_MESH, "Circle"),
                          loc,
                          rot,
                          NULL,
                          local_view_bits,
                          &creation_data);

  em = dune_editmesh_from_object(obedit);

  if (calc_uvs) {
    ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!MESH_EDIT_op_call_and_selectf(
          em,
          op,
          "verts.out",
          false,
          "create_circle segments=%i radius=%f cap_ends=%b cap_tris=%b matrix=%m4 calc_uvs=%b",
          api_int_get(op->ptr, "vertices"),
          api_float_get(op->ptr, "radius"),
          cap_end,
          cap_tri,
          creation_data.mat,
          calc_uvs)) {
    return OP_CANCELLED;
  }

  make_prim_finish(C, obedit, &creation_data, enter_editmode);

  return OP_FINISHED;
}

void MESH_OT_primitive_circle_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Circle";
  ot->description = "Construct a circle mesh";
  ot->idname = "MESH_OT_primitive_circle_add";

  /* api callbacks */
  ot->ex = add_primitive_circle_exec;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_int(ot->api, "vertices", 32, 3, MESH_ADD_VERTS_MAXI, "Vertices", "", 3, 500);
  ed_object_add_unit_props_radius(ot);
  api_def_enum(ot->api, "fill_type", fill_type_items, 0, "Fill Type", "");

  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);
}

static int add_primitive_cylinder_ex(Cxt *C, wmOp *op)
{
  MakePrimitiveData creation_data;
  Object *obedit;
  DuneMeshEdit *dme;
  float loc[3], rot[3], scale[3];
  bool enter_editmode;
  ushort local_view_bits;
  const int end_fill_type = api_enum_get(op->ptr, "end_fill_type");
  const bool cap_end = (end_fill_type != 0);
  const bool cap_tri = (end_fill_type == 2);
  const bool calc_uvs = api_bool_get(op->ptr, "calc_uvs");

  wm_op_view3d_unit_defaults(C, op);
  ed_object_add_generic_get_opts(
      C, op, 'Z', loc, rot, scale, &enter_editmode, &local_view_bits, NULL);
  obedit = make_prim_init(C,
                          CXT_DATA_(LANG_CXT_ID_MESH, "Cylinder"),
                          loc,
                          rot,
                          scale,
                          local_view_bits,
                          &creation_data);
  em = dune_editmesh_from_object(obedit);

  if (calc_uvs) {
    ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!EDBM_op_call_and_selectf(em,
                                op,
                                "verts.out",
                                false,
                                "create_cone segments=%i radius1=%f radius2=%f cap_ends=%b "
                                "cap_tris=%b depth=%f matrix=%m4 calc_uvs=%b",
                                api_int_get(op->ptr, "vertices"),
                                api_float_get(op->ptr, "radius"),
                                api_float_get(op->ptr, "radius"),
                                cap_end,
                                cap_tri,
                                api_float_get(op->ptr, "depth"),
                                creation_data.mat,
                                calc_uvs)) {
    return OP_CANCELLED;
  }

  make_prim_finish(C, obedit, &creation_data, enter_editmode);

  return OP_FINISHED;
}

void MESH_OT_primitive_cylinder_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Cylinder";
  ot->description = "Construct a cylinder mesh";
  ot->idname = "MESH_OT_primitive_cylinder_add";

  /* api callbacks */
  ot->ex = add_primitive_cylinder_exec;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_int(ot->api, "vertices", 32, 3, MESH_ADD_VERTS_MAXI, "Vertices", "", 3, 500);
  ed_object_add_unit_props_radius(ot);
  api_def_float_distance(
      ot->api, "depth", 2.0f, 0.0, OBJECT_ADD_SIZE_MAXF, "Depth", "", 0.001, 100.00);
  api_def_enum(ot->api, "end_fill_type", fill_type_items, 1, "Cap Fill Type", "");

  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);
}

static int add_primitive_cone_exec(Cxt *C, wmOp *op)
{
  MakePrimitiveData creation_data;
  Object *obedit;
  DuneMeshEdit *dme;
  float loc[3], rot[3], scale[3];
  bool enter_editmode;
  ushort local_view_bits;
  const int end_fill_type = api_enum_get(op->ptr, "end_fill_type");
  const bool cap_end = (end_fill_type != 0);
  const bool cap_tri = (end_fill_type == 2);
  const bool calc_uvs = api_bool_get(op->ptr, "calc_uvs");

  wm_op_view3d_unit_defaults(C, op);
  ed_object_add_generic_get_opts(
      C, op, 'Z', loc, rot, scale, &enter_editmode, &local_view_bits, NULL);
  obedit = make_prim_init(C,
                          CXT_DATA_(LANG_CXT_ID_MESH, "Cone"),
                          loc,
                          rot,
                          scale,
                          local_view_bits,
                          &creation_data);
  em = dune_editmesh_from_object(obedit);

  if (calc_uvs) {
    ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!EDBM_op_call_and_selectf(em,
                                op,
                                "verts.out",
                                false,
                                "create_cone segments=%i radius1=%f radius2=%f cap_ends=%b "
                                "cap_tris=%b depth=%f matrix=%m4 calc_uvs=%b",
                                api_int_get(op->ptr, "vertices"),
                                api_float_get(op->ptr, "radius1"),
                                api_float_get(op->ptr, "radius2"),
                                cap_end,
                                cap_tri,
                                api_float_get(op->ptr, "depth"),
                                creation_data.mat,
                                calc_uvs)) {
    return OP_CANCELLED;
  }

  make_prim_finish(C, obedit, &creation_data, enter_editmode);

  return OP_FINISHED;
}

void MESH_OT_primitive_cone_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Cone";
  ot->description = "Construct a conic mesh";
  ot->idname = "MESH_OT_primitive_cone_add";

  /* api callbacks */
  ot->ex = add_primitive_cone_ex;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_int(ot->api, "vertices", 32, 3, MESH_ADD_VERTS_MAXI, "Vertices", "", 3, 500);
  api_def_float_distance(
      ot->api, "radius1", 1.0f, 0.0, OBJECT_ADD_SIZE_MAXF, "Radius 1", "", 0.001, 100.00);
  api_def_float_distance(
      ot->api, "radius2", 0.0f, 0.0, OBJECT_ADD_SIZE_MAXF, "Radius 2", "", 0.0, 100.00);
  api_def_float_distance(
      ot->api, "depth", 2.0f, 0.0, OBJECT_ADD_SIZE_MAXF, "Depth", "", 0.001, 100.00);
  api_def_enum(ot->api, "end_fill_type", fill_type_items, 1, "Base Fill Type", "");

  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);
}

static int add_primitive_grid_exec(Cxt *C, wmOp *op)
{
  MakePrimitiveData creation_data;
  Object *obedit;
  DuneMeshEdit *em;
  float loc[3], rot[3];
  bool enter_editmode;
  ushort local_view_bits;
  const bool calc_uvs = api_bool_get(op->ptr, "calc_uvs");

  wm_op_view3d_unit_defaults(C, op);
  ed_object_add_generic_get_opts(
      C, op, 'Z', loc, rot, NULL, &enter_editmode, &local_view_bits, NULL);
  obedit = make_prim_init(C,
                          CXT_DATA_(LANG_CXT_ID_MESH, "Grid"),
                          loc,
                          rot,
                          NULL,
                          local_view_bits,
                          &creation_data);
  em = dune_editmesh_from_object(obedit);

  if (calc_uvs) {
    ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!MESH_EDIT_op_call_and_selectf(
          em,
          op,
          "verts.out",
          false,
          "create_grid x_segments=%i y_segments=%i size=%f matrix=%m4 calc_uvs=%b",
          api_int_get(op->ptr, "x_subdivisions"),
          api_int_get(op->ptr, "y_subdivisions"),
          api_float_get(op->ptr, "size") / 2.0f,
          creation_data.mat,
          calc_uvs)) {
    return OP_CANCELLED;
  }

  make_prim_finish(C, obedit, &creation_data, enter_editmode);

  return OP_FINISHED;
}

void MESH_OT_primitive_grid_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Grid";
  ot->description = "Construct a grid mesh";
  ot->idname = "MESH_OT_primitive_grid_add";

  /* api callbacks */
  ot->exec = add_primitive_grid_ex;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  /* Note that if you use MESH_ADD_VERTS_MAXI for both x and y at the same time
   * you will still reach impossible values (10^12 vertices or so...). */
  api_def_int(
      ot->api, "x_subdivisions", 10, 1, MESH_ADD_VERTS_MAXI, "X Subdivisions", "", 1, 1000);
  api_def_int(
      ot->api, "y_subdivisions", 10, 1, MESH_ADD_VERTS_MAXI, "Y Subdivisions", "", 1, 1000);

  ed_object_add_unit_props_size(ot);
  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);
}

static int add_primitive_monkey_ex(Cxt *C, wmOp *op)
{
  MakePrimitiveData creation_data;
  Object *obedit;
  DuneMeshEdit *dme;
  float loc[3], rot[3];
  float dia;
  bool enter_editmode;
  ushort local_view_bits;
  const bool calc_uvs = api_bool_get(op->ptr, "calc_uvs");

  wm_op_view3d_unit_defaults(C, op);
  ed_object_add_generic_get_opts(
      C, op, 'Y', loc, rot, NULL, &enter_editmode, &local_view_bits, NULL);

  obedit = make_prim_init(C,
                          CXT_DATA_(LANG_CXT_ID_MESH, "Suzanne"),
                          loc,
                          rot,
                          NULL,
                          local_view_bits,
                          &creation_data);
  dia = API_float_get(op->ptr, "size") / 2.0f;
  mul_mat3_m4_fl(creation_data.mat, dia);

  em = dune_editmesh_from_object(obedit);

  if (calc_uvs) {
    ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!EDBM_op_call_and_selectf(em,
                                op,
                                "verts.out",
                                false,
                                "create_monkey matrix=%m4 calc_uvs=%b",
                                creation_data.mat,
                                calc_uvs)) {
    return OP_CANCELLED;
  }

  make_prim_finish(C, obedit, &creation_data, enter_editmode);

  return OPERATOR_FINISHED;
}

void MESH_OT_primitive_monkey_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Monkey";
  ot->description = "Construct a Suzanne mesh";
  ot->idname = "MESH_OT_primitive_monkey_add";

  /* api callbacks */
  ot->ex = add_primitive_monkey_ex;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ed_object_add_unit_props_size(ot);
  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);
}

static int add_primitive_uvsphere_ex(Cxt *C, wmOp *op)
{
  MakePrimitiveData creation_data;
  Object *obedit;
  DuneMeshEdit *dme;
  float loc[3], rot[3], scale[3];
  bool enter_editmode;
  ushort local_view_bits;
  const bool calc_uvs = api_bool_get(op->ptr, "calc_uvs");

  wm_op_view3d_unit_defaults(C, op);
  ed_object_add_generic_get_opts(
      C, op, 'Z', loc, rot, scale, &enter_editmode, &local_view_bits, NULL);
  obedit = make_prim_init(C,
                          CXT_DATA_(LANG_CXT_ID_MESH, "Sphere"),
                          loc,
                          rot,
                          scale,
                          local_view_bits,
                          &creation_data);
  em = dune_editmesh_from_object(obedit);

  if (calc_uvs) {
   ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!mesh_op_call_and_selectf(
          em,
          op,
          "verts.out",
          false,
          "create_uvsphere u_segments=%i v_segments=%i radius=%f matrix=%m4 calc_uvs=%b",
          api_int_get(op->ptr, "segments"),
          api_int_get(op->ptr, "ring_count"),
          api_float_get(op->ptr, "radius"),
          creation_data.mat,
          calc_uvs)) {
    return OP_CANCELLED;
  }

  make_prim_finish(C, obedit, &creation_data, enter_editmode);

  return OP_FINISHED;
}

void MESH_OT_primitive_uv_sphere_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add UV Sphere";
  ot->description = "Construct a UV sphere mesh";
  ot->idname = "MESH_OT_primitive_uv_sphere_add";

  /* api callbacks */
  ot->ex = add_primitive_uvsphere_ex;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_int(ot->api, "segments", 32, 3, MESH_ADD_VERTS_MAXI / 100, "Segments", "", 3, 500);
  api_def_int(ot->api, "ring_count", 16, 3, MESH_ADD_VERTS_MAXI / 100, "Rings", "", 3, 500);

  ed_object_add_unit_props_radius(ot);
  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);
}

static int add_primitive_icosphere_ex(Cxt *C, wmOp *op)
{
  MakePrimitiveData creation_data;
  Object *obedit;
  DuneMeshEdit *em;
  float loc[3], rot[3], scale[3];
  bool enter_editmode;
  ushort local_view_bits;
  const bool calc_uvs = API_boolean_get(op->ptr, "calc_uvs");

  wm_op_view3d_unit_defaults(C, op);
  ed_object_add_generic_get_opts(
      C, op, 'Z', loc, rot, scale, &enter_editmode, &local_view_bits, NULL);
  obedit = make_prim_init(C,
                          CXT_DATA_(LANG_CXT_ID_MESH, "Icosphere"),
                          loc,
                          rot,
                          scale,
                          local_view_bits,
                          &creation_data);
  em = dune_editmesh_from_object(obedit);

  if (calc_uvs) {
    ed_mesh_uv_texture_ensure(obedit->data, NULL);
  }

  if (!EDBM_op_call_and_selectf(
          em,
          op,
          "verts.out",
          false,
          "create_icosphere subdivisions=%i radius=%f matrix=%m4 calc_uvs=%b",
          api_int_get(op->ptr, "subdivisions"),
          api_float_get(op->ptr, "radius"),
          creation_data.mat,
          calc_uvs)) {
    return OP_CANCELLED;
  }

  make_prim_finish(C, obedit, &creation_data, enter_editmode);

  return OP_FINISHED;
}

void MESH_OT_primitive_ico_sphere_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Ico Sphere";
  ot->description = "Construct an Icosphere mesh";
  ot->idname = "MESH_OT_primitive_ico_sphere_add";

  /* api callbacks */
  ot->ex = add_primitive_icosphere_exec;
  ot->poll = ed_op_scene_editable;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_int(ot->api, "subdivisions", 2, 1, 10, "Subdivisions", "", 1, 8);

  ed_object_add_unit_props_radius(ot);
  ed_object_add_mesh_props(ot);
  ed_object_add_generic_props(ot, true);
}
