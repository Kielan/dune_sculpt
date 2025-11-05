#include "types_modifier_types.h"
#include "types_object_types.h"

#include "lib_list.h"
#include "lib_math.h"

#include "dune_cx.h"
#include "dune_editmesh.h"
#include "dune_layer.h"
#include "dune_report.h"

#include "api_access.h"
#include "api_define.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ed_mesh.h"
#include "ed_screen.h"
#include "ed_transform.h"
#include "ed_view3d.h"

#include "mem_guardedalloc.h"

#include "mesh_intern.h" /* own include */

/* Extrude Internal Utils */
static void edm_extrude_edge_exclude_mirror(
    Object *obedit, MEditMesh *em, const char hflag, MOp *op, MOpSlot *slot_edges_exclude)
{
  Mesh *m = em->mesh;
  ModifierData *md;

  /* If a mirror modifier with clipping is on, we need to adjust some
   * of the cases above to handle edges on the line of symmetry. */
  for (md = obedit->modifiers.first; md; md = md->next) {
    if ((md->type == eModifierType_Mirror) && (md->mode & eModifierMode_Realtime)) {
      MirrorModifierData *mmd = (MirrorModifierData *)md;

      if (mmd->flag & MOD_MIR_CLIPPING) {
        MIter iter;
        MEdge *edge;

        float mtx[4][4];
        if (mmd->mirror_ob) {
          float imtx[4][4];
          invert_m4_m4(imtx, mmd->mirror_ob->obmat);
          mul_m4_m4m4(mtx, imtx, obedit->obmat);
        }

        M_ITER_MESH (edge, &iter, m, M_EDGES_OF_MESH) {
          if (m_elem_flag_test(edge, hflag) && m_edge_is_boundary(edge) &&
              m_elem_flag_test(edge->l->f, hflag)) {
            float co1[3], co2[3];

            copy_v3_v3(co1, edge->v1->co);
            copy_v3_v3(co2, edge->v2->co);

            if (mmd->mirror_ob) {
              mul_v3_m4v3(co1, mtx, co1);
              mul_v3_m4v3(co2, mtx, co2);
            }

            if (mmd->flag & MOD_MIR_AXIS_X) {
              if ((fabsf(co1[0]) < mmd->tolerance) && (fabsf(co2[0]) < mmd->tolerance)) {
                MOp_slot_map_empty_insert(op, slot_edges_exclude, edge);
              }
            }
            if (mmd->flag & MOD_MIR_AXIS_Y) {
              if ((fabsf(co1[1]) < mmd->tolerance) && (fabsf(co2[1]) < mmd->tolerance)) {
                MOp_slot_map_empty_insert(op, slot_edges_exclude, edge);
              }
            }
            if (mmd->flag & MOD_MIR_AXIS_Z) {
              if ((fabsf(co1[2]) < mmd->tolerance) && (fabsf(co2[2]) < mmd->tolerance)) {
                MOp_slot_map_empty_insert(op, slot_edges_exclude, edge);
              }
            }
          }
        }
      }
    }
  }
}

/* individual face extrude */
/* will use vertex normals for extrusion directions, so *nor is unaffected */
static bool edm_extrude_discrete_faces(MEditMesh *em, wmOp *op, const char hflag)
{
  MOIter siter;
  MIter liter;
  MFace *f;
  MLoop *l;
  MOp mop;

  edm_op_init(
      em, &mop, op, "extrude_discrete_faces faces=%hf use_select_history=%b", hflag, true);

  /* deselect original verts */
  edbm_flag_disable_all(em, MESH_ELEM_SEL);

  mesh_op_exec(em->mesh, &mop);

  MO_ITER (f, &siter, mop.slots_out, "faces.out", M_FACE) {
    mesh_face_sel_set(em->mesh, f, true);

    /* set face vertex normals to face normal */
    M_ITER_ELEM (l, &liter, f, M_LOOPS_OF_FACE) {
      copy_v3_v3(l->v->no, f->no);
    }
  }

  if (!edm_op_finish(em, &mop, op, true)) {
    return false;
  }

  return true;
}

bool edm_extrude_edges_indiv(EditMesh *em,
                             wmOp *op,
                             const char hflag,
                             const bool use_normal_flip)
{
  Mesh *m = em->mesh;
  MOp mop;

  edm_op_init(em,
              &bmop,
              op,
              "extrude_edge_only edges=%he use_normal_flip=%b use_select_history=%b",
              hflag,
              use_normal_flip,
              true);

  /* deselect original verts */
  M_SEL_HISTORY_BACKUP(bm);
  edm_flag_disable_all(em, M_ELEM_SEL);
  M_SEL_HISTORY_RESTORE(m);

  m_op_exec(em->mesh, &mop);
  m_slot_buffer_hflag_enable(
      em->mesh, mop.slots_out, "geom.out", M_VERT | M_EDGE, M_ELEM_SEL, true);

  if (!edm_op_finish(em, &mop, op, true)) {
    return false;
  }

  return true;
}

/* extrudes individual verts */
static bool edm_extrude_verts_indiv(MEditMesh *em, wmOp *op, const char hflag)
{
  MOp mop;

  edm_op_init(em, &mop, op, "extrude_vert_indiv verts=%hv use_select_history=%b", hflag, true);

  /* deselect original verts */
  mo_slot_buffer_hflag_disable(em->mesh, mop.slots_in, "verts", M_VERT, M_ELEM_SEL, true);

  m_op_exec(em->mesh, &mop);
  mo_slot_buf_hflag_enable(em->mesh, mop.slots_out, "verts.out", M_VERT, M_ELEM_SEL, true);

  if (!E_op_finish(em, &mop, op, true)) {
    return false;
  }

  return true;
}

static char edm_extrude_htype_from_em_sel(MEditMesh *em)
{
  char htype = M_ALL_NOLOOP;

  if (em->selmode & SCE_SEL_VERT) {
    /* pass */
  }
  else if (em->selmode & SCE_SEL_EDGE) {
    htype &= ~M_VERT;
  }
  else {
    htype &= ~(M_VERT | M_EDGE);
  }

  if (em->mesh->totedgesel == 0) {
    htype &= ~(M_EDGE | M_FACE);
  }
  else if (em->mesh->totfacesel == 0) {
    htype &= ~M_FACE;
  }

  return htype;
}

static bool edm_extrude_ex(Object *obedit,
                            MEditMesh *em,
                            char htype,
                            const char hflag,
                            const bool use_normal_flip,
                            const bool use_dissolve_ortho_edges,
                            const bool use_mirror,
                            const bool use_sel_history)
{
  Mesh *m = em->mesh;
  MOIter siter;
  MOp extop;
  MElem *ele;

  /* needed to remove the faces left behind */
  if (htype & M_FACE) {
    htype |= M_EDGE;
  }

  mo_op_init(m, &extop, MO_FLAG_DEFAULTS, "extrude_face_rgn");
  mo_slot_bool_set(extop.slots_in, "use_normal_flip", use_normal_flip);
  mo_slot_bool_set(extop.slots_in, "use_dissolve_ortho_edges", use_dissolve_ortho_edges);
  mo_slot_bool_set(extop.slots_in, "use_select_history", use_select_history);
  mo_slot_buf_from_enabled_hflag(bm, &extop, extop.slots_in, "geom", htype, hflag);

  if (use_mirror) {
    MOpSlot *slot_edges_exclude;
    slot_edges_exclude = mo_slot_get(extop.slots_in, "edges_exclude");

    edm_extrude_edge_exclude_mirror(obedit, em, hflag, &extop, slot_edges_exclude);
  }

  M_SEL_HISTORY_BACKUP(m);
  EDM_flag_disable_all(em, M_ELEM_SEL);
  M_SEL_HISTORY_RESTORE(m);

  mo_op_exec(m, &extop);

  MO_ITER (ele, &siter, extop.slots_out, "geom.out", M_ALL_NOLOOP) {
    m_elem_sel_set(m, ele, true);
  }

  mo_op_finish(m, &extop);

  return true;
}

/* Extrude Repeat Op */
static int edm_extrude_repeat_exec(Cx *C, wmOp *op)
{

  ApiProp *prop = api_struct_find_prop(op->ptr, "offset");
  const int steps = api_int_get(op->ptr, "steps");
  const float scale_offset = api_float_get(op->ptr, "scale_offset");
  float offset[3];

  if (!api_prop_is_set(op->ptr, prop)) {
    RgnView3D *rv3d = cx_wm_rgn_view3d(C);
    if (rv3d != NULL) {
      normalize_v3_v3(offset, rv3d->persinv[2]);
    }
    else {
      copy_v3_v3(offset, (const float[3]){0, 0, 1});
    }
    api_prop_float_set_arr(op->ptr, prop, offset);
  }
  else {
    api_prop_float_get_arr(op->ptr, prop, offset);
  }

  mul_v3_fl(offset, scale_offset);

  ViewLayer *view_layer = cx_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = dune_view_layer_arr_from_objects_in_edit_mode_unique_data(
      view_layer, cx_wm_view3d(C), &objects_len);

  for (uint ob_idx = 0; ob_idx < objects_len; ob_idx++) {
    float offset_local[3], tmat[3][3];

    Object *obedit = objects[ob_idx];
    MEditMesh *em = dune_editmesh_from_object(obedit);

    copy_m3_m4(tmat, obedit->obmat);
    invert_m3(tmat);
    mul_v3_m3v3(offset_local, tmat, offset);

    for (int a = 0; a < steps; a++) {
      edm_extrude_ex(obedit, em, M_ALL_NOLOOP, M_ELEM_SEL, false, false, false, true);
      mo_op_callf(
          em->mesh, MO_FLAG_DEFAULTS, "translate vec=%v verts=%hv", offset_local, M_ELEM_SEL);
    }

    edm_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = true,
                    .is_destructive = true,
                });
  }

  MEM_freeN(objects);

  return OP_FINISHED;
}

void mesh_ot_extrude_repeat(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Extrude Repeat";
  ot->description = "Extrude selected vertices, edges or faces repeatedly";
  ot->idname = "mesh_ot_extrude_repeat";

  /* api cbs */
  ot->exec = edm_extrude_repeat_exec;
  ot->poll = ed_op_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_int(ot->srna, "steps", 10, 0, 1000000, "Steps", "", 0, 180);
  ApiProp *prop = api_def_float_vector_xyz(
      ot->srna, "offset", 3, NULL, -100000, 100000, "Offset", "Offset vector", -1000.0f, 1000.0f);
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  api_def_float(ot->srna, "scale_offset", 1.0f, 0.0f, 1e12f, "Scale Offset", "", 0.0f, 100.0f);
}

/* Extrude Op */
/* generic extern called extruder */
static bool edm_extrude_mesh(Object *obedit, MEditMesh *em, wmOp *op)
{
  const bool use_normal_flip = api_bool_get(op->ptr, "use_normal_flip");
  const bool use_dissolve_ortho_edges = api_bool_get(op->ptr, "use_dissolve_ortho_edges");
  const char htype = edm_extrude_htype_from_em_sel(em);
  enum { NONE = 0, ELEM_FLAG, VERT_ONLY, EDGE_ONLY } nr;
  bool changed = false;

  if (em->selmode & SCE_SEL_VERT) {
    if (em->mesh->totvertsel == 0) {
      nr = NONE;
    }
    else if (em->mesh->totvertsel == 1) {
      nr = VERT_ONLY;
    }
    else if (em->mesh->totedgesel == 0) {
      nr = VERT_ONLY;
    }
    else {
      nr = ELEM_FLAG;
    }
  }
  else if (em->selectmode & SCE_SEL_EDGE) {
    if (em->mesh->totedgesel == 0) {
      nr = NONE;
    }
    else if (em->mesh->totfacesel == 0) {
      nr = EDGE_ONLY;
    }
    else {
      nr = ELEM_FLAG;
    }
  }
  else {
    if (em->mesh->totfacesel == 0) {
      nr = NONE;
    }
    else {
      nr = ELEM_FLAG;
    }
  }

  switch (nr) {
    case NONE:
      return false;
    case ELEM_FLAG:
      changed = edm_extrude_ex(obedit,
                                em,
                                htype,
                                M_ELEM_SEL,
                                use_normal_flip,
                                use_dissolve_ortho_edges,
                                true,
                                true);
      break;
    case VERT_ONLY:
      changed = edm_extrude_verts_indiv(em, op, M_ELEM_SEL);
      break;
    case EDGE_ONLY:
      changed = edm_extrude_edges_indiv(em, op, M_ELEM_SEL, use_normal_flip);
      break;
  }

  if (changed) {
    return true;
  }

  dune_report(op->reports, RPT_ERR, "Not a valid selection for extrude");
  return false;
}

/* extrude without transform */
static int edm_extrude_rgn_exec(Cx *C, wmOp *op)
{
  ViewLayer *view_layer = cx_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = dune_view_layer_arr_from_objects_in_edit_mode_unique_data(
      view_layer, cx_wm_view3d(C), &objects_len);

  for (uint ob_idx = 0; ob_idx < objects_len; ob_idx++) {
    Object *obedit = objects[ob_index];
    MEditMesh *em = dune_editmesh_from_object(obedit);
    if (em->mesh->totvertsel == 0) {
      continue;
    }

    if (!edm_extrude_mesh(obedit, em, op)) {
      continue;
    }
    /* This normally happens when pushing undo but modal operators
     * like this one don't push undo data until after modal mode is done. */
    edm_update(obedit->data,
                &(const struct EdMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = true,
                    .is_destructive = true,
                });
  }
  MEM_freeN(objects);
  return OP_FINISHED;
}

void mesh_ot_extrude_rgn(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Extrude Region";
  ot->idname = "mesh_ot_extrude_region";
  ot->description = "Extrude region of faces";

  /* api cbs */
  // ot->invoke = mesh_extrude_rgn_invoke;
  ot->exec = edm_extrude_rgn_exec;
  ot->poll = ed_op_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->sapi, "use_normal_flip", false, "Flip Normals", "");
  api_def_bool(ot->sapi, "use_dissolve_ortho_edges", false, "Dissolve Orthogonal Edges", "");
  Transform_Props(ot, P_NO_DEFAULTS | P_MIRROR_DUMMY);
}

/* Extrude Cx Op
 * Guess what to do based on selection. */

/* extrude wo transform */
static int edm_extrude_cx_exec(Cx *C, wmOp *op)
{
  ViewLayer *view_layer = cx_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = dune_view_layer_arr_from_objects_in_edit_mode_unique_data(
      view_layer, cx_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_idx];
    MEditMesh *em = dune_editmesh_from_object(obedit);
    if (em->mesh->totvertsel == 0) {
      continue;
    }

    edbm_extrude_mesh(obedit, em, op);

    /* This normally happens when pushing undo but modal operators
     * like this one don't push undo data until after modal mode is done. */
    edm_update(obedit->data,
                &(const struct EdMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = true,
                    .is_destructive = true,
                });
  }
  MEM_freeN(objects);
  return OP_FINISHED;
}

void mesh_ot_extrude_cx(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Extrude Cx";
  ot->idname = "mesh_ot_extrude_cx";
  ot->description = "Extrude selection";

  /* api cbs */
  ot->exec = edbm_extrude_cx_exec;
  ot->poll = ed_op_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  api_def_bool(ot->sapi, "use_normal_flip", false, "Flip Normals", "");
  api_def_bool(ot->sapi, "use_dissolve_ortho_edges", false, "Dissolve Orthogonal Edges", "");
  Transform_Props(ot, P_NO_DEFAULTS | P_MIRROR_DUMMY);
}

/* Extrude Verts Op */
static int edm_extrude_verts_exec(Cx *C, wmOp *op)
{
  ViewLayer *view_layer = cx_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = dune_view_layer_arr_from_objects_in_edit_mode_unique_data(
      view_layer, cx_wm_view3d(C), &objects_len);

  for (uint ob_idx = 0; ob_idx < objects_len; ob_idx++) {
    Object *obedit = objects[ob_idx];
    MEditMesh *em = dune_editmesh_from_object(obedit);
    if (em->mesh->totvertsel == 0) {
      continue;
    }

    edm_extrude_verts_indiv(em, op, M_ELEM_SEL);

    edm_update(obedit->data,
                &(const struct EdMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }
  MEM_freeN(objects);

  return OP_FINISHED;
}

void mesh_ot_extrude_verts_indiv(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Extrude Only Verts";
  ot->idname = "mesh_ot_extrude_verts_indiv";
  ot->description = "Extrude individual vertices only";

  /* api cbs */
  ot->exec = edm_extrude_verts_exec;
  ot->poll = ed_op_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* to give to transform */
  Transform_Properties(ot, P_NO_DEFAULTS | P_MIRROR_DUMMY);
}

/* Extrude Edges Op */
static int edm_extrude_edges_exec(Cx *C, wmOp *op)
{
  const bool use_normal_flip = api_bool_get(op->ptr, "use_normal_flip");
  ViewLayer *view_layer = cx_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = dune_view_layer_arr_from_objects_in_edit_mode_unique_data(
      view_layer, cx_wm_view3d(C), &objects_len);

  for (uint ob_idx = 0; ob_idx < objects_len; ob_idx++) {
    Object *obedit = objects[ob_idx];
    MEditMesh *em = dune_editmesh_from_object(obedit);
    if (em->mesh->totedgesel == 0) {
      continue;
    }

    edm_extrude_edges_indiv(em, op, M_ELEM_SEL, use_normal_flip);

    edm_update(obedit->data,
                &(const struct EdMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }
  MEM_freeN(objects);

  return OP_FINISHED;
}

void mesh_ot_extrude_edges_indiv(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Extrude Only Edges";
  ot->idname = "mesh_ot_extrude_edges_indiv";
  ot->description = "Extrude individual edges only";

  /* api callbacks */
  ot->exec = edbm_extrude_edges_exec;
  ot->poll = ed_op_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* to give to transform */
  api_def_bool(ot->sapi, "use_normal_flip", false, "Flip Normals", "");
  Transform_Props(ot, P_NO_DEFAULTS | P_MIRROR_DUMMY);
}

/* Extrude Faces Op */
static int edm_extrude_faces_exec(Cx *C, wmOp *op)
{
  ViewLayer *view_layer = cx_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = dune_view_layer_arr_from_objects_in_edit_mode_unique_data(
      view_layer, cx_wm_view3d(C), &objects_len);

  for (uint ob_idx = 0; ob_idx < objects_len; ob_idx++) {
    Object *obedit = objects[ob_idx];
    MEditMesh *em = dune_editmesh_from_object(obedit);
    if (em->mesh->totfacesel == 0) {
      continue;
    }

    edm_extrude_discrete_faces(em, op, M_ELEM_SEL);

    edm_update(obedit->data,
                &(const struct EDMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }
  MEM_freeN(objects);

  return OP_FINISHED;
}

void mesh_ot_extrude_faces_indiv(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Extrude Individual Faces";
  ot->idname = "mesh_ot_extrude_faces_indiv";
  ot->description = "Extrude individual faces only";

  /* api cbs */
  ot->exec = edm_extrude_faces_exec;
  ot->poll = ed_op_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  Transform_Props(ot, P_NO_DEFAULTS | P_MIRROR_DUMMY);
}

/** Dupli-Extrude Op
 *
 * Add-click-mesh (extrude) op. */

static int edm_dupli_extrude_cursor_invoke(Cx *C, wmOp *op, const wmEvent *event)
{
  struct Graph *depsgraph = cx_data_ensure_evaluated_graph(C);
  ViewCx vc;
  MeshVert *v1;
  MeshIter iter;
  float center[3];
  uint verts_len;

  em_setup_viewcx(C, &vc);
  const Object *object_active = vc.obact;

  const bool rot_src = api_bool_get(op->ptr, "rotate_source");
  const bool use_proj = ((vc.scene->toolsettings->snap_flag & SCE_SNAP) &&
                         (vc.scene->toolsettings->snap_mode == SCE_SNAP_MODE_FACE));

  /* First calculate the center of transformation. */
  zero_v3(center);
  verts_len = 0;

  uint objects_len = 0;
  Object **objects = dune_view_layer_arr_from_objects_in_edit_mode_unique_data(
      vc.view_layer, vc.v3d, &objects_len);
  for (uint ob_idx = 0; ob_idx < objects_len; ob_idx++) {
    Object *obedit = objects[ob_idx];
    ed_view3d_viewcx_init_object(&vc, obedit);
    const int local_verts_len = vc.em->mesh->totvertsel;

    if (vc.em->mesh->totvertsel == 0) {
      continue;
    }

    float local_center[3];
    zero_v3(local_center);

    M_ITER_MESH (v1, &iter, vc.em->mesh, M_VERTS_OF_MESH) {
      if (m_elem_flag_test(v1, M_ELEM_SEL)) {
        add_v3_v3(local_center, v1->co);
      }
    }

    mul_v3_fl(local_center, 1.0f / (float)local_verts_len);
    mul_m4_v3(vc.obedit->obmat, local_center);
    mul_v3_fl(local_center, (float)local_verts_len);

    add_v3_v3(center, local_center);
    verts_len += local_verts_len;
  }

  if (verts_len != 0) {
    mul_v3_fl(center, 1.0f / (float)verts_len);
  }

  /* Then we process the meshes. */
  for (uint ob_idx = 0; ob_idx < objects_len; ob_idx++) {
    Object *obedit = objects[ob_index];
    ed_view3d_viewcx_init_object(&vc, obedit);

    if (verts_len != 0) {
      if (vc.em->mesh->totvertsel == 0) {
        continue;
      }
    }
    else if (obedit != object_active) {
      continue;
    }

    invert_m4_m4(vc.obedit->imat, vc.obedit->obmat);
    ed_view3d_init_mats_rv3d(vc.obedit, vc.rv3d);

    float local_center[3];
    mul_v3_m4v3(local_center, vc.obedit->imat, center);

    /* call extrude? */
    if (verts_len != 0) {
      const char extrude_htype = edm_extrude_htype_from_em_sel(vc.em);
      MeshEdge *eed;
      float mat[3][3];
      float vec[3], ofs[3];
      float nor[3] = {0.0, 0.0, 0.0};

      /* 2D normal calc */
      const float mval_f[2] = {(float)event->mval[0], (float)event->mval[1]};

      /* check for edges that are half selected, use for rotation */
      bool done = false;
      MESH_ITER_MESH (eed, &iter, vc.em->mesh, MESH_EDGES_OF_MESH) {
        if (mesh_elem_flag_test(eed, MESH_ELEM_SEL)) {
          float co1[2], co2[2];

          if ((ed_view3d_project_float_object(vc.ron, eed->v1->co, co1, V3D_PROJ_TEST_NOP) ==
               V3D_PROJ_RET_OK) &&
              (ed_view3d_project_float_object(vc.rgn, eed->v2->co, co2, V3D_PROJ_TEST_NOP) ==
               V3D_PROJ_RET_OK)) {
            /* 2D rotate by 90d while adding.
             *  (x, y) = (y, -x)
             *
             * accumulate the screenspace normal in 2D,
             * with screenspace edge length weighting the result. */
            if (line_point_side_v2(co1, co2, mval_f) >= 0.0f) {
              nor[0] += (co1[1] - co2[1]);
              nor[1] += -(co1[0] - co2[0]);
            }
            else {
              nor[0] += (co2[1] - co1[1]);
              nor[1] += -(co2[0] - co1[0]);
            }
            done = true;
          }
        }
      }

      if (done) {
        float view_vec[3], cross[3];

        /* convert the 2D normal into 3D */
        mul_mat3_m4_v3(vc.rv3d->viewinv, nor); /* World-space. */
        mul_mat3_m4_v3(vc.obedit->imat, nor);  /* Local-space. */

        /* correct the normal to be aligned on the view plane */
        mul_v3_mat3_m4v3(view_vec, vc.obedit->imat, vc.rv3d->viewinv[2]);
        cross_v3_v3v3(cross, nor, view_vec);
        cross_v3_v3v3(nor, view_vec, cross);
        normalize_v3(nor);
      }

      /* center */
      copy_v3_v3(ofs, local_center);

      mul_m4_v3(vc.obedit->obmat, ofs); /* view space */
      ED_view3d_win_to_3d_int(vc.v3d, vc.region, ofs, event->mval, ofs);
      mul_m4_v3(vc.obedit->imat, ofs); /* back in object space */

      sub_v3_v3(ofs, local_center);

      /* calculate rotation */
      unit_m3(mat);
      if (done) {
        float angle;

        normalize_v3_v3(vec, ofs);

        angle = angle_normalized_v3v3(vec, nor);

        if (angle != 0.0f) {
          float axis[3];

          cross_v3_v3v3(axis, nor, vec);

          /* halve the rotation if its applied twice */
          if (rot_src) {
            angle *= 0.5f;
          }

          axis_angle_to_mat3(mat, axis, angle);
        }
      }

      if (rot_src) {
        edm_op_callf(
            vc.em, op, "rotate verts=%hv cent=%v matrix=%m3", M_ELEM_SEL, local_center, mat);

        /* also project the source, for retopo workflow */
        if (use_proj) {
          edm_project_snap_verts(C, depsgraph, vc.rgn, vc.obedit, vc.em);
        }
      }

      edm_extrude_ex(vc.obedit, vc.em, extrude_htype, M_ELEM_SEL, false, false, true, true);
      edm_op_callf(
          vc.em, op, "rotate verts=%hv cent=%v matrix=%m3", M_ELEM_SEL, local_center, mat);
      edm_op_callf(vc.em, op, "translate verts=%hv vec=%v", M_ELEM_SEL, ofs);
    }
    else {
      /* This only runs for the active object. */
      const float *cursor = vc.scene->cursor.location;
      MeshOp dmop;
      MOIter oiter;

      copy_v3_v3(local_center, cursor);
      ed_view3d_win_to_3d_int(vc.v3d, vc.rgn, local_center, event->mval, local_center);

      mul_m4_v3(vc.obedit->imat, local_center); /* back in object space */

      edbm_op_init(vc.em, &dmop, op, "create_vert co=%v", local_center);
      meshobj_op_exec(vc.em->dm, &dmop);

      MO_ITER (v1, &oiter, dmop.slots_out, "vert.out", MESH_VERT) {
        mesh_vert_sel_set(vc.em->dm, v1, true);
      }

      if (!edm_op_finish(vc.em, &dmop, op, true)) {
        continue;
      }
    }

    if (use_proj) {
      edm_project_snap_verts(C, depsgraph, vc.rgn, vc.obedit, vc.em);
    }

    /* This normally happens when pushing undo but modal operators
     * like this one don't push undo data until after modal mode is done. */
    edbm_update(vc.obedit->data,
                &(const struct EdMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = true,
                    .is_destructive = true,
                });

    wm_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
    wm_event_add_notifier(C, NC_GEOM | ND_SEL, obedit->data);
  }
  mem_freeN(objects);

  return OP_FINISHED;
}

void mesh_ot_dupli_extrude_cursor(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Extrude to Cursor or Add";
  ot->idname = "mesh_ot_dupli_extrude_cursor";
  ot->description =
      "Duplicate and extrude selected verts, edges or faces towards the mouse cursor";

  /* api cbs */
  ot->invoke = edm_dupli_extrude_cursor_invoke;
  ot->poll = ed_op_editmesh_rgn_view3d;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_DEPENDS_ON_CURSOR;

  api_def_bool(ot->sapi,
                "rotate_source",
                true,
                "Rotate Source",
                "Rotate initial selection giving better shape");
}
