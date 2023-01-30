#include <stddef.h>

#include "MEM_guardedalloc.h"

#include "TYPES_key.h"
#include "TYPES_material.h"
#include "TYPES_mesh.h"
#include "TYRS_meshdata.h"
#include "TYPES_modifier.h"
#include "TYPES_object.h"
#include "TYPES_scene.h"

#include "LIB_bitmap.h"
#include "LIB_heap_simple.h"
#include "LIB_linklist.h"
#include "LI_linklist_stack.h"
#include "LI_listbase.h"
#include "LI_math.h"
#include "LI_rand.h"
#include "LI_sort_utils.h"
#include "LI_string.h"

#include "DUNE_context.h"
#include "DUNE_customdata.h"
#include "DUNE_deform.h"
#include "DUNE_editmesh.h"
#include "DUNE_key.h"
#include "DUNE_layer.h"
#include "DUNE_lib_id.h"
#include "DUNE_main.h"
#include "DUNE_material.h"
#include "DUNE_mesh.h"
#include "DUNE_report.h"
#include "DUNE_texture.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

#include "LANG_translation.h"

#include "API_access.h"
#include "API_define.h"
#include "API_enum_types.h"
#include "API_prototypes.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_mesh.h"
#include "ED_object.h"
#include "ED_outliner.h"
#include "ED_screen.h"
#include "ED_select_utils.h"
#include "ED_transform.h"
#include "ED_uvedit.h"
#include "ED_view3d.h"

#include "RE_texture.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "mesh_intern.h" /* own include */

#include "bmesh_tools.h"

#define USE_FACE_CREATE_SEL_EXTEND

/* -------------------------------------------------------------------- */
/** Subdivide Operator */

static int edbm_subdivide_exec(bContext *C, wmOperator *op)
{
  const int cuts = RNA_int_get(op->ptr, "number_cuts");
  const float smooth = RNA_float_get(op->ptr, "smoothness");
  const float fractal = RNA_float_get(op->ptr, "fractal") / 2.5f;
  const float along_normal = RNA_float_get(op->ptr, "fractal_along_normal");
  const bool use_quad_tri = !RNA_boolean_get(op->ptr, "ngon");

  if (use_quad_tri && RNA_enum_get(op->ptr, "quadcorner") == SUBD_CORNER_STRAIGHT_CUT) {
    RNA_enum_set(op->ptr, "quadcorner", SUBD_CORNER_INNERVERT);
  }
  const int quad_corner_type = RNA_enum_get(op->ptr, "quadcorner");
  const int seed = RNA_int_get(op->ptr, "seed");

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (!(em->bm->totedgesel || em->bm->totfacesel)) {
      continue;
    }

    BM_mesh_esubdivide(em->bm,
                       BM_ELEM_SELECT,
                       smooth,
                       SUBD_FALLOFF_LIN,
                       false,
                       fractal,
                       along_normal,
                       cuts,
                       SUBDIV_SELECT_ORIG,
                       quad_corner_type,
                       use_quad_tri,
                       true,
                       false,
                       seed);

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

/* NOTE: these values must match delete_mesh() event values. */
static const EnumPropertyItem prop_mesh_cornervert_types[] = {
    {SUBD_CORNER_INNERVERT, "INNERVERT", 0, "Inner Vert", ""},
    {SUBD_CORNER_PATH, "PATH", 0, "Path", ""},
    {SUBD_CORNER_STRAIGHT_CUT, "STRAIGHT_CUT", 0, "Straight Cut", ""},
    {SUBD_CORNER_FAN, "FAN", 0, "Fan", ""},
    {0, NULL, 0, NULL, NULL},
};

void MESH_OT_subdivide(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Subdivide";
  ot->description = "Subdivide selected edges";
  ot->idname = "MESH_OT_subdivide";

  /* api callbacks */
  ot->exec = edbm_subdivide_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  prop = RNA_def_int(ot->srna, "number_cuts", 1, 1, 100, "Number of Cuts", "", 1, 10);
  /* avoid re-using last var because it can cause
   * _very_ high poly meshes and annoy users (or worse crash) */
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);

  RNA_def_float(
      ot->srna, "smoothness", 0.0f, 0.0f, 1e3f, "Smoothness", "Smoothness factor", 0.0f, 1.0f);

  WM_operatortype_props_advanced_begin(ot);

  RNA_def_boolean(ot->srna,
                  "ngon",
                  true,
                  "Create N-Gons",
                  "When disabled, newly created faces are limited to 3 and 4 sided faces");
  RNA_def_enum(
      ot->srna,
      "quadcorner",
      prop_mesh_cornervert_types,
      SUBD_CORNER_STRAIGHT_CUT,
      "Quad Corner Type",
      "How to subdivide quad corners (anything other than Straight Cut will prevent n-gons)");

  RNA_def_float(ot->srna,
                "fractal",
                0.0f,
                0.0f,
                1e6f,
                "Fractal",
                "Fractal randomness factor",
                0.0f,
                1000.0f);
  RNA_def_float(ot->srna,
                "fractal_along_normal",
                0.0f,
                0.0f,
                1.0f,
                "Along Normal",
                "Apply fractal displacement along normal only",
                0.0f,
                1.0f);
  RNA_def_int(ot->srna,
              "seed",
              0,
              0,
              INT_MAX,
              "Random Seed",
              "Seed for the random number generator",
              0,
              255);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edge Ring Subdivide Operator
 *
 * Bridge code shares props.
 *
 * \{ */

struct EdgeRingOpSubdProps {
  int interp_mode;
  int cuts;
  float smooth;

  int profile_shape;
  float profile_shape_factor;
};

static void mesh_operator_edgering_props(wmOperatorType *ot,
                                         const int cuts_min,
                                         const int cuts_default)
{
  /* NOTE: these values must match delete_mesh() event values. */
  static const EnumPropertyItem prop_subd_edgering_types[] = {
      {SUBD_RING_INTERP_LINEAR, "LINEAR", 0, "Linear", ""},
      {SUBD_RING_INTERP_PATH, "PATH", 0, "Blend Path", ""},
      {SUBD_RING_INTERP_SURF, "SURFACE", 0, "Blend Surface", ""},
      {0, NULL, 0, NULL, NULL},
  };

  PropertyRNA *prop;

  prop = RNA_def_int(
      ot->srna, "number_cuts", cuts_default, 0, 1000, "Number of Cuts", "", cuts_min, 64);
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);

  RNA_def_enum(ot->srna,
               "interpolation",
               prop_subd_edgering_types,
               SUBD_RING_INTERP_PATH,
               "Interpolation",
               "Interpolation method");

  RNA_def_float(
      ot->srna, "smoothness", 1.0f, 0.0f, 1e3f, "Smoothness", "Smoothness factor", 0.0f, 2.0f);

  /* profile-shape */
  RNA_def_float(ot->srna,
                "profile_shape_factor",
                0.0f,
                -1e3f,
                1e3f,
                "Profile Factor",
                "How much intermediary new edges are shrunk/expanded",
                -2.0f,
                2.0f);

  prop = RNA_def_property(ot->srna, "profile_shape", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_proportional_falloff_curve_only_items);
  RNA_def_property_enum_default(prop, PROP_SMOOTH);
  RNA_def_property_ui_text(prop, "Profile Shape", "Shape of the profile");
  RNA_def_property_translation_context(prop,
                                       BLT_I18NCONTEXT_ID_CURVE_LEGACY); /* Abusing id_curve :/ */
}

static void mesh_operator_edgering_props_get(wmOperator *op, struct EdgeRingOpSubdProps *op_props)
{
  op_props->interp_mode = RNA_enum_get(op->ptr, "interpolation");
  op_props->cuts = RNA_int_get(op->ptr, "number_cuts");
  op_props->smooth = RNA_float_get(op->ptr, "smoothness");

  op_props->profile_shape = RNA_enum_get(op->ptr, "profile_shape");
  op_props->profile_shape_factor = RNA_float_get(op->ptr, "profile_shape_factor");
}

static int edbm_subdivide_edge_ring_exec(bContext *C, wmOperator *op)
{

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  struct EdgeRingOpSubdProps op_props;

  mesh_operator_edgering_props_get(op, &op_props);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (em->bm->totedgesel == 0) {
      continue;
    }

    if (!EDBM_op_callf(em,
                       op,
                       "subdivide_edgering edges=%he interp_mode=%i cuts=%i smooth=%f "
                       "profile_shape=%i profile_shape_factor=%f",
                       BM_ELEM_SELECT,
                       op_props.interp_mode,
                       op_props.cuts,
                       op_props.smooth,
                       op_props.profile_shape,
                       op_props.profile_shape_factor)) {
      continue;
    }

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }

  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void MESH_OT_subdivide_edgering(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Subdivide Edge-Ring";
  ot->description = "Subdivide perpendicular edges to the selected edge-ring";
  ot->idname = "MESH_OT_subdivide_edgering";

  /* api callbacks */
  ot->exec = edbm_subdivide_edge_ring_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  mesh_operator_edgering_props(ot, 1, 10);
}


/* -------------------------------------------------------------------- */
/** \name Un-Subdivide Operator
 * \{ */

static int edbm_unsubdivide_exec(bContext *C, wmOperator *op)
{
  const int iterations = RNA_int_get(op->ptr, "iterations");
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if ((em->bm->totvertsel == 0) && (em->bm->totedgesel == 0) && (em->bm->totfacesel == 0)) {
      continue;
    }

    BMOperator bmop;
    EDBM_op_init(em, &bmop, op, "unsubdivide verts=%hv iterations=%i", BM_ELEM_SELECT, iterations);

    BMO_op_exec(em->bm, &bmop);

    if (!EDBM_op_finish(em, &bmop, op, true)) {
      continue;
    }

    if ((em->selectmode & SCE_SELECT_VERTEX) == 0) {
      EDBM_selectmode_flush_ex(em, SCE_SELECT_VERTEX); /* need to flush vert->face first */
    }
    EDBM_selectmode_flush(em);

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_unsubdivide(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Un-Subdivide";
  ot->description = "Un-subdivide selected edges and faces";
  ot->idname = "MESH_OT_unsubdivide";

  /* api callbacks */
  ot->exec = edbm_unsubdivide_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_int(
      ot->srna, "iterations", 2, 1, 1000, "Iterations", "Number of times to un-subdivide", 1, 100);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Delete Operator
 * \{ */

/* NOTE: these values must match delete_mesh() event values. */
enum {
  MESH_DELETE_VERT = 0,
  MESH_DELETE_EDGE = 1,
  MESH_DELETE_FACE = 2,
  MESH_DELETE_EDGE_FACE = 3,
  MESH_DELETE_ONLY_FACE = 4,
};

static void edbm_report_delete_info(ReportList *reports,
                                    const int totelem_old[3],
                                    const int totelem_new[3])
{
  BKE_reportf(reports,
              RPT_INFO,
              "Removed: %d vertices, %d edges, %d faces",
              totelem_old[0] - totelem_new[0],
              totelem_old[1] - totelem_new[1],
              totelem_old[2] - totelem_new[2]);
}

static int edbm_delete_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  bool changed_multi = false;

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    const int type = RNA_enum_get(op->ptr, "type");

    switch (type) {
      case MESH_DELETE_VERT: /* Erase Vertices */
        if (em->bm->totvertsel == 0) {
          continue;
        }
        BM_custom_loop_normals_to_vector_layer(em->bm);
        if (!EDBM_op_callf(em, op, "delete geom=%hv context=%i", BM_ELEM_SELECT, DEL_VERTS)) {
          continue;
        }
        break;
      case MESH_DELETE_EDGE: /* Erase Edges */
        if (em->bm->totedgesel == 0) {
          continue;
        }
        BM_custom_loop_normals_to_vector_layer(em->bm);
        if (!EDBM_op_callf(em, op, "delete geom=%he context=%i", BM_ELEM_SELECT, DEL_EDGES)) {
          continue;
        }
        break;
      case MESH_DELETE_FACE: /* Erase Faces */
        if (em->bm->totfacesel == 0) {
          continue;
        }
        BM_custom_loop_normals_to_vector_layer(em->bm);
        if (!EDBM_op_callf(em, op, "delete geom=%hf context=%i", BM_ELEM_SELECT, DEL_FACES)) {
          continue;
        }
        break;
      case MESH_DELETE_EDGE_FACE: /* Edges and Faces */
        if ((em->bm->totedgesel == 0) && (em->bm->totfacesel == 0)) {
          continue;
        }
        BM_custom_loop_normals_to_vector_layer(em->bm);
        if (!EDBM_op_callf(
                em, op, "delete geom=%hef context=%i", BM_ELEM_SELECT, DEL_EDGESFACES)) {
          continue;
        }
        break;
      case MESH_DELETE_ONLY_FACE: /* Only faces. */
        if (em->bm->totfacesel == 0) {
          continue;
        }
        BM_custom_loop_normals_to_vector_layer(em->bm);
        if (!EDBM_op_callf(em, op, "delete geom=%hf context=%i", BM_ELEM_SELECT, DEL_ONLYFACES)) {
          continue;
        }
        break;
      default:
        BLI_assert(0);
        break;
    }

    changed_multi = true;

    EDBM_flag_disable_all(em, BM_ELEM_SELECT);

    BM_custom_loop_normals_from_vector_layer(em->bm, false);

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });

    DEG_id_tag_update(obedit->data, ID_RECALC_SELECT);
    WM_event_add_notifier(C, NC_GEOM | ND_SELECT, obedit->data);
  }

  MEM_freeN(objects);

  return changed_multi ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void MESH_OT_delete(wmOperatorType *ot)
{
  static const EnumPropertyItem prop_mesh_delete_types[] = {
      {MESH_DELETE_VERT, "VERT", 0, "Vertices", ""},
      {MESH_DELETE_EDGE, "EDGE", 0, "Edges", ""},
      {MESH_DELETE_FACE, "FACE", 0, "Faces", ""},
      {MESH_DELETE_EDGE_FACE, "EDGE_FACE", 0, "Only Edges & Faces", ""},
      {MESH_DELETE_ONLY_FACE, "ONLY_FACE", 0, "Only Faces", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Delete";
  ot->description = "Delete selected vertices, edges or faces";
  ot->idname = "MESH_OT_delete";

  /* api callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = edbm_delete_exec;

  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = RNA_def_enum(ot->srna,
                          "type",
                          prop_mesh_delete_types,
                          MESH_DELETE_VERT,
                          "Type",
                          "Method used for deleting mesh data");
  RNA_def_property_flag(ot->prop, PROP_HIDDEN | PROP_SKIP_SAVE);
}

/* -------------------------------------------------------------------- */
/** \name Delete Loose Operator
 * \{ */

static bool bm_face_is_loose(BMFace *f)
{
  BMLoop *l_iter, *l_first;

  l_iter = l_first = BM_FACE_FIRST_LOOP(f);
  do {
    if (!BM_edge_is_boundary(l_iter->e)) {
      return false;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return true;
}

static int edbm_delete_loose_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  int totelem_old_sel[3];
  int totelem_old[3];

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  EDBM_mesh_stats_multi(objects, objects_len, totelem_old, totelem_old_sel);

  const bool use_verts = (RNA_boolean_get(op->ptr, "use_verts") && totelem_old_sel[0]);
  const bool use_edges = (RNA_boolean_get(op->ptr, "use_edges") && totelem_old_sel[1]);
  const bool use_faces = (RNA_boolean_get(op->ptr, "use_faces") && totelem_old_sel[2]);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];

    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;
    BMIter iter;

    BM_mesh_elem_hflag_disable_all(bm, BM_VERT | BM_EDGE | BM_FACE, BM_ELEM_TAG, false);

    if (use_faces) {
      BMFace *f;

      BM_ITER_MESH (f, &iter, bm, BM_FACES_OF_MESH) {
        if (BM_elem_flag_test(f, BM_ELEM_SELECT)) {
          BM_elem_flag_set(f, BM_ELEM_TAG, bm_face_is_loose(f));
        }
      }

      BM_mesh_delete_hflag_context(bm, BM_ELEM_TAG, DEL_FACES);
    }

    if (use_edges) {
      BMEdge *e;

      BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
        if (BM_elem_flag_test(e, BM_ELEM_SELECT)) {
          BM_elem_flag_set(e, BM_ELEM_TAG, BM_edge_is_wire(e));
        }
      }

      BM_mesh_delete_hflag_context(bm, BM_ELEM_TAG, DEL_EDGES);
    }

    if (use_verts) {
      BMVert *v;

      BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
        if (BM_elem_flag_test(v, BM_ELEM_SELECT)) {
          BM_elem_flag_set(v, BM_ELEM_TAG, (v->e == NULL));
        }
      }

      BM_mesh_delete_hflag_context(bm, BM_ELEM_TAG, DEL_VERTS);
    }

    EDBM_flag_disable_all(em, BM_ELEM_SELECT);

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }

  int totelem_new[3];
  EDBM_mesh_stats_multi(objects, objects_len, totelem_new, NULL);

  edbm_report_delete_info(op->reports, totelem_old, totelem_new);

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_delete_loose(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Delete Loose";
  ot->description = "Delete loose vertices, edges or faces";
  ot->idname = "MESH_OT_delete_loose";

  /* api callbacks */
  ot->exec = edbm_delete_loose_exec;

  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_boolean(ot->srna, "use_verts", true, "Vertices", "Remove loose vertices");
  RNA_def_boolean(ot->srna, "use_edges", true, "Edges", "Remove loose edges");
  RNA_def_boolean(ot->srna, "use_faces", false, "Faces", "Remove loose faces");
}

/* -------------------------------------------------------------------- */
/** \name Collapse Edge Operator
 * \{ */

static int edbm_collapse_edge_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (em->bm->totedgesel == 0) {
      continue;
    }

    if (!EDBM_op_callf(em, op, "collapse edges=%he uvs=%b", BM_ELEM_SELECT, true)) {
      continue;
    }

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_edge_collapse(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Collapse Edges & Faces";
  ot->description =
      "Collapse isolated edge and face regions, merging data such as UV's and vertex colors. "
      "This can collapse edge-rings as well as regions of connected faces into vertices";
  ot->idname = "MESH_OT_edge_collapse";

  /* api callbacks */
  ot->exec = edbm_collapse_edge_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** \name Create Edge/Face Operator
 * \{ */

static bool edbm_add_edge_face__smooth_get(BMesh *bm)
{
  BMEdge *e;
  BMIter iter;

  uint vote_on_smooth[2] = {0, 0};

  BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
    if (BM_elem_flag_test(e, BM_ELEM_SELECT) && e->l) {
      vote_on_smooth[BM_elem_flag_test_bool(e->l->f, BM_ELEM_SMOOTH)]++;
    }
  }

  return (vote_on_smooth[0] < vote_on_smooth[1]);
}

#ifdef USE_FACE_CREATE_SEL_EXTEND
/**
 * Function used to get a fixed number of edges linked to a vertex that passes a test function.
 * This is used so we can request all boundary edges connected to a vertex for eg.
 */
static int edbm_add_edge_face_exec__vert_edge_lookup(
    BMVert *v, BMEdge *e_used, BMEdge **e_arr, const int e_arr_len, bool (*func)(const BMEdge *))
{
  BMIter iter;
  BMEdge *e_iter;
  int i = 0;
  BM_ITER_ELEM (e_iter, &iter, v, BM_EDGES_OF_VERT) {
    if (BM_elem_flag_test(e_iter, BM_ELEM_HIDDEN) == false) {
      if ((e_used == NULL) || (e_used != e_iter)) {
        if (func(e_iter)) {
          e_arr[i++] = e_iter;
          if (i >= e_arr_len) {
            break;
          }
        }
      }
    }
  }
  return i;
}

static BMElem *edbm_add_edge_face_exec__tricky_extend_sel(BMesh *bm)
{
  BMIter iter;
  bool found = false;

  if (bm->totvertsel == 1 && bm->totedgesel == 0 && bm->totfacesel == 0) {
    /* first look for 2 boundary edges */
    BMVert *v;

    BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
      if (BM_elem_flag_test(v, BM_ELEM_SELECT)) {
        found = true;
        break;
      }
    }

    if (found) {
      BMEdge *ed_pair[3];
      if (((edbm_add_edge_face_exec__vert_edge_lookup(v, NULL, ed_pair, 3, BM_edge_is_wire) ==
            2) &&
           (BM_edge_share_face_check(ed_pair[0], ed_pair[1]) == false)) ||

          ((edbm_add_edge_face_exec__vert_edge_lookup(v, NULL, ed_pair, 3, BM_edge_is_boundary) ==
            2) &&
           (BM_edge_share_face_check(ed_pair[0], ed_pair[1]) == false))) {
        BMEdge *e_other = BM_edge_exists(BM_edge_other_vert(ed_pair[0], v),
                                         BM_edge_other_vert(ed_pair[1], v));
        BM_edge_select_set(bm, ed_pair[0], true);
        BM_edge_select_set(bm, ed_pair[1], true);
        if (e_other) {
          BM_edge_select_set(bm, e_other, true);
        }
        return (BMElem *)v;
      }
    }
  }
  else if (bm->totvertsel == 2 && bm->totedgesel == 1 && bm->totfacesel == 0) {
    /* first look for 2 boundary edges */
    BMEdge *e;

    BM_ITER_MESH (e, &iter, bm, BM_EDGES_OF_MESH) {
      if (BM_elem_flag_test(e, BM_ELEM_SELECT)) {
        found = true;
        break;
      }
    }
    if (found) {
      BMEdge *ed_pair_v1[2];
      BMEdge *ed_pair_v2[2];
      if (((edbm_add_edge_face_exec__vert_edge_lookup(e->v1, e, ed_pair_v1, 2, BM_edge_is_wire) ==
            1) &&
           (edbm_add_edge_face_exec__vert_edge_lookup(e->v2, e, ed_pair_v2, 2, BM_edge_is_wire) ==
            1) &&
           (BM_edge_share_face_check(e, ed_pair_v1[0]) == false) &&
           (BM_edge_share_face_check(e, ed_pair_v2[0]) == false)) ||

#  if 1 /* better support mixed cases T37203. */
          ((edbm_add_edge_face_exec__vert_edge_lookup(e->v1, e, ed_pair_v1, 2, BM_edge_is_wire) ==
            1) &&
           (edbm_add_edge_face_exec__vert_edge_lookup(
                e->v2, e, ed_pair_v2, 2, BM_edge_is_boundary) == 1) &&
           (BM_edge_share_face_check(e, ed_pair_v1[0]) == false) &&
           (BM_edge_share_face_check(e, ed_pair_v2[0]) == false)) ||

          ((edbm_add_edge_face_exec__vert_edge_lookup(
                e->v1, e, ed_pair_v1, 2, BM_edge_is_boundary) == 1) &&
           (edbm_add_edge_face_exec__vert_edge_lookup(e->v2, e, ed_pair_v2, 2, BM_edge_is_wire) ==
            1) &&
           (BM_edge_share_face_check(e, ed_pair_v1[0]) == false) &&
           (BM_edge_share_face_check(e, ed_pair_v2[0]) == false)) ||
#  endif

          ((edbm_add_edge_face_exec__vert_edge_lookup(
                e->v1, e, ed_pair_v1, 2, BM_edge_is_boundary) == 1) &&
           (edbm_add_edge_face_exec__vert_edge_lookup(
                e->v2, e, ed_pair_v2, 2, BM_edge_is_boundary) == 1) &&
           (BM_edge_share_face_check(e, ed_pair_v1[0]) == false) &&
           (BM_edge_share_face_check(e, ed_pair_v2[0]) == false))) {
        BMVert *v1_other = BM_edge_other_vert(ed_pair_v1[0], e->v1);
        BMVert *v2_other = BM_edge_other_vert(ed_pair_v2[0], e->v2);
        BMEdge *e_other = (v1_other != v2_other) ? BM_edge_exists(v1_other, v2_other) : NULL;
        BM_edge_select_set(bm, ed_pair_v1[0], true);
        BM_edge_select_set(bm, ed_pair_v2[0], true);
        if (e_other) {
          BM_edge_select_set(bm, e_other, true);
        }
        return (BMElem *)e;
      }
    }
  }

  return NULL;
}
static void edbm_add_edge_face_exec__tricky_finalize_sel(BMesh *bm, BMElem *ele_desel, BMFace *f)
{
  /* Now we need to find the edge that isn't connected to this element. */
  BM_select_history_clear(bm);

  /* Notes on hidden geometry:
   * - Un-hide the face since its possible hidden was copied when copying
   *   surrounding face attributes.
   * - Un-hide before adding to select history
   *   since we may extend into an existing, hidden vert/edge.
   */

  BM_elem_flag_disable(f, BM_ELEM_HIDDEN);
  BM_face_select_set(bm, f, false);

  if (ele_desel->head.htype == BM_VERT) {
    BMLoop *l = BM_face_vert_share_loop(f, (BMVert *)ele_desel);
    BLI_assert(f->len == 3);
    BM_vert_select_set(bm, (BMVert *)ele_desel, false);
    BM_edge_select_set(bm, l->next->e, true);
    BM_select_history_store(bm, l->next->e);
  }
  else {
    BMLoop *l = BM_face_edge_share_loop(f, (BMEdge *)ele_desel);
    BLI_assert(ELEM(f->len, 4, 3));

    BM_edge_select_set(bm, (BMEdge *)ele_desel, false);
    if (f->len == 4) {
      BMEdge *e_active = l->next->next->e;
      BM_elem_flag_disable(e_active, BM_ELEM_HIDDEN);
      BM_edge_select_set(bm, e_active, true);
      BM_select_history_store(bm, e_active);
    }
    else {
      BMVert *v_active = l->next->next->v;
      BM_elem_flag_disable(v_active, BM_ELEM_HIDDEN);
      BM_vert_select_set(bm, v_active, true);
      BM_select_history_store(bm, v_active);
    }
  }
}
#endif /* USE_FACE_CREATE_SEL_EXTEND */

static int edbm_add_edge_face_exec(bContext *C, wmOperator *op)
{
  /* When this is used to dissolve we could avoid this, but checking isn't too slow. */
  bool changed_multi = false;
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if ((em->bm->totvertsel == 0) && (em->bm->totedgesel == 0) && (em->bm->totvertsel == 0)) {
      continue;
    }

    bool use_smooth = edbm_add_edge_face__smooth_get(em->bm);
    int totedge_orig = em->bm->totedge;
    int totface_orig = em->bm->totface;

    BMOperator bmop;
#ifdef USE_FACE_CREATE_SEL_EXTEND
    BMElem *ele_desel;
    BMFace *ele_desel_face;

    /* be extra clever, figure out if a partial selection should be extended so we can create
     * geometry with single vert or single edge selection. */
    ele_desel = edbm_add_edge_face_exec__tricky_extend_sel(em->bm);
#endif
    if (!EDBM_op_init(em,
                      &bmop,
                      op,
                      "contextual_create geom=%hfev mat_nr=%i use_smooth=%b",
                      BM_ELEM_SELECT,
                      em->mat_nr,
                      use_smooth)) {
      continue;
    }

    BMO_op_exec(em->bm, &bmop);

    /* cancel if nothing was done */
    if ((totedge_orig == em->bm->totedge) && (totface_orig == em->bm->totface)) {
      EDBM_op_finish(em, &bmop, op, true);
      continue;
    }
#ifdef USE_FACE_CREATE_SEL_EXTEND
    /* normally we would want to leave the new geometry selected,
     * but being able to press F many times to add geometry is too useful! */
    if (ele_desel && (BMO_slot_buffer_len(bmop.slots_out, "faces.out") == 1) &&
        (ele_desel_face = BMO_slot_buffer_get_first(bmop.slots_out, "faces.out"))) {
      edbm_add_edge_face_exec__tricky_finalize_sel(em->bm, ele_desel, ele_desel_face);
    }
    else
#endif
    {
      /* Newly created faces may include existing hidden edges,
       * copying face data from surrounding, may have copied hidden face flag too.
       *
       * Important that faces use flushing since 'edges.out'
       * won't include hidden edges that already existed.
       */
      BMO_slot_buffer_hflag_disable(
          em->bm, bmop.slots_out, "faces.out", BM_FACE, BM_ELEM_HIDDEN, true);
      BMO_slot_buffer_hflag_disable(
          em->bm, bmop.slots_out, "edges.out", BM_EDGE, BM_ELEM_HIDDEN, false);

      BMO_slot_buffer_hflag_enable(
          em->bm, bmop.slots_out, "faces.out", BM_FACE, BM_ELEM_SELECT, true);
      BMO_slot_buffer_hflag_enable(
          em->bm, bmop.slots_out, "edges.out", BM_EDGE, BM_ELEM_SELECT, true);
    }

    if (!EDBM_op_finish(em, &bmop, op, true)) {
      continue;
    }

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });
    changed_multi = true;
  }
  MEM_freeN(objects);

  if (!changed_multi) {
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

void MESH_OT_edge_face_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Make Edge/Face";
  ot->description = "Add an edge or face to selected";
  ot->idname = "MESH_OT_edge_face_add";

  /* api callbacks */
  ot->exec = edbm_add_edge_face_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** \name Mark Edge (Seam) Operator
 * \{ */

static int edbm_mark_seam_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  BMEdge *eed;
  BMIter iter;
  const bool clear = RNA_boolean_get(op->ptr, "clear");

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;

    if (bm->totedgesel == 0) {
      continue;
    }

    if (clear) {
      BM_ITER_MESH (eed, &iter, bm, BM_EDGES_OF_MESH) {
        if (!BM_elem_flag_test(eed, BM_ELEM_SELECT) || BM_elem_flag_test(eed, BM_ELEM_HIDDEN)) {
          continue;
        }

        BM_elem_flag_disable(eed, BM_ELEM_SEAM);
      }
    }
    else {
      BM_ITER_MESH (eed, &iter, bm, BM_EDGES_OF_MESH) {
        if (!BM_elem_flag_test(eed, BM_ELEM_SELECT) || BM_elem_flag_test(eed, BM_ELEM_HIDDEN)) {
          continue;
        }
        BM_elem_flag_enable(eed, BM_ELEM_SEAM);
      }
    }
  }

  ED_uvedit_live_unwrap(scene, objects, objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_mark_seam(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Mark Seam";
  ot->idname = "MESH_OT_mark_seam";
  ot->description = "(Un)mark selected edges as a seam";

  /* api callbacks */
  ot->exec = edbm_mark_seam_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  prop = RNA_def_boolean(ot->srna, "clear", 0, "Clear", "");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  WM_operatortype_props_advanced_begin(ot);
}
