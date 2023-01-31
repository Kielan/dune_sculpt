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

/* -------------------------------------------------------------------- */
/** \name Mark Edge (Sharp) Operator
 * \{ */

static int edbm_mark_sharp_exec(bContext *C, wmOperator *op)
{
  BMEdge *eed;
  BMIter iter;
  const bool clear = RNA_boolean_get(op->ptr, "clear");
  const bool use_verts = RNA_boolean_get(op->ptr, "use_verts");
  ViewLayer *view_layer = CTX_data_view_layer(C);

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;

    if ((use_verts && bm->totvertsel == 0) || (!use_verts && bm->totedgesel == 0)) {
      continue;
    }

    BM_ITER_MESH (eed, &iter, bm, BM_EDGES_OF_MESH) {
      if (use_verts) {
        if (!(BM_elem_flag_test(eed->v1, BM_ELEM_SELECT) ||
              BM_elem_flag_test(eed->v2, BM_ELEM_SELECT))) {
          continue;
        }
      }
      else if (!BM_elem_flag_test(eed, BM_ELEM_SELECT)) {
        continue;
      }

      BM_elem_flag_set(eed, BM_ELEM_SMOOTH, clear);
    }

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

void MESH_OT_mark_sharp(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Mark Sharp";
  ot->idname = "MESH_OT_mark_sharp";
  ot->description = "(Un)mark selected edges as sharp";

  /* api callbacks */
  ot->exec = edbm_mark_sharp_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  prop = RNA_def_boolean(ot->srna, "clear", false, "Clear", "");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
  prop = RNA_def_boolean(
      ot->srna,
      "use_verts",
      false,
      "Vertices",
      "Consider vertices instead of edges to select which edges to (un)tag as sharp");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/* -------------------------------------------------------------------- */
/** \name Connect Vertex Path Operator
 * \{ */

static bool edbm_connect_vert_pair(BMEditMesh *em, struct Mesh *me, wmOperator *op)
{
  BMesh *bm = em->bm;
  BMOperator bmop;
  const int verts_len = bm->totvertsel;
  bool is_pair = (verts_len == 2);
  int len = 0;
  bool check_degenerate = true;

  bool checks_succeded = true;

  /* sanity check */
  if (verts_len < 2) {
    return false;
  }

  BMVert **verts = MEM_mallocN(sizeof(*verts) * verts_len, __func__);
  {
    BMIter iter;
    BMVert *v;
    int i = 0;

    BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
      if (BM_elem_flag_test(v, BM_ELEM_SELECT)) {
        verts[i++] = v;
      }
    }

    if (BM_vert_pair_share_face_check_cb(
            verts[0],
            verts[1],
            BM_elem_cb_check_hflag_disabled_simple(BMFace *, BM_ELEM_HIDDEN))) {
      check_degenerate = false;
      is_pair = false;
    }
  }

  if (is_pair) {
    if (!EDBM_op_init(em,
                      &bmop,
                      op,
                      "connect_vert_pair verts=%eb verts_exclude=%hv faces_exclude=%hf",
                      verts,
                      verts_len,
                      BM_ELEM_HIDDEN,
                      BM_ELEM_HIDDEN)) {
      checks_succeded = false;
    }
  }
  else {
    if (!EDBM_op_init(em,
                      &bmop,
                      op,
                      "connect_verts verts=%eb faces_exclude=%hf check_degenerate=%b",
                      verts,
                      verts_len,
                      BM_ELEM_HIDDEN,
                      check_degenerate)) {
      checks_succeded = false;
    }
  }
  if (checks_succeded) {
    BMBackup em_backup = EDBM_redo_state_store(em);

    BM_custom_loop_normals_to_vector_layer(bm);

    BMO_op_exec(bm, &bmop);
    const bool failure = BMO_error_occurred_at_level(bm, BMO_ERROR_FATAL);
    len = BMO_slot_get(bmop.slots_out, "edges.out")->len;

    if (len && is_pair) {
      /* new verts have been added, we have to select the edges, not just flush */
      BMO_slot_buffer_hflag_enable(
          em->bm, bmop.slots_out, "edges.out", BM_EDGE, BM_ELEM_SELECT, true);
    }

    bool em_backup_free = true;
    if (!EDBM_op_finish(em, &bmop, op, false)) {
      len = 0;
    }
    else if (failure) {
      len = 0;
      EDBM_redo_state_restore_and_free(&em_backup, em, true);
      em_backup_free = false;
    }
    else {
      /* so newly created edges get the selection state from the vertex */
      EDBM_selectmode_flush(em);

      BM_custom_loop_normals_from_vector_layer(bm, false);

      EDBM_update(me,
                  &(const struct EDBMUpdate_Params){
                      .calc_looptri = true,
                      .calc_normals = false,
                      .is_destructive = true,
                  });
    }

    if (em_backup_free) {
      EDBM_redo_state_free(&em_backup);
    }
  }
  MEM_freeN(verts);

  return len;
}

static int edbm_vert_connect_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  uint failed_objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (!edbm_connect_vert_pair(em, obedit->data, op)) {
      failed_objects_len++;
    }
  }
  MEM_freeN(objects);
  return failed_objects_len == objects_len ? OPERATOR_CANCELLED : OPERATOR_FINISHED;
}

void MESH_OT_vert_connect(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Vertex Connect";
  ot->idname = "MESH_OT_vert_connect";
  ot->description = "Connect selected vertices of faces, splitting the face";

  /* api callbacks */
  ot->exec = edbm_vert_connect_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** \name Connect Vertex Path Operator
 * \{ */

/**
 * check that endpoints are verts and only have a single selected edge connected.
 */
static bool bm_vert_is_select_history_open(BMesh *bm)
{
  BMEditSelection *ele_a = bm->selected.first;
  BMEditSelection *ele_b = bm->selected.last;
  if ((ele_a->htype == BM_VERT) && (ele_b->htype == BM_VERT)) {
    if ((BM_iter_elem_count_flag(BM_EDGES_OF_VERT, (BMVert *)ele_a->ele, BM_ELEM_SELECT, true) ==
         1) &&
        (BM_iter_elem_count_flag(BM_EDGES_OF_VERT, (BMVert *)ele_b->ele, BM_ELEM_SELECT, true) ==
         1)) {
      return true;
    }
  }

  return false;
}

static bool bm_vert_connect_pair(BMesh *bm, BMVert *v_a, BMVert *v_b)
{
  BMOperator bmop;
  BMVert **verts;
  const int totedge_orig = bm->totedge;

  BMO_op_init(bm, &bmop, BMO_FLAG_DEFAULTS, "connect_vert_pair");

  verts = BMO_slot_buffer_alloc(&bmop, bmop.slots_in, "verts", 2);
  verts[0] = v_a;
  verts[1] = v_b;

  BM_vert_normal_update(verts[0]);
  BM_vert_normal_update(verts[1]);

  BMO_op_exec(bm, &bmop);
  BMO_slot_buffer_hflag_enable(bm, bmop.slots_out, "edges.out", BM_EDGE, BM_ELEM_SELECT, true);
  BMO_op_finish(bm, &bmop);
  return (bm->totedge != totedge_orig);
}

static bool bm_vert_connect_select_history(BMesh *bm)
{
  /* Logic is as follows:
   *
   * - If there are any isolated/wire verts - connect as edges.
   * - Otherwise connect faces.
   * - If all edges have been created already, closed the loop.
   */
  if (BLI_listbase_count_at_most(&bm->selected, 2) == 2 && (bm->totvertsel > 2)) {
    BMEditSelection *ese;
    int tot = 0;
    bool changed = false;
    bool has_wire = false;
    // bool all_verts;

    /* ensure all verts have history */
    for (ese = bm->selected.first; ese; ese = ese->next, tot++) {
      BMVert *v;
      if (ese->htype != BM_VERT) {
        break;
      }
      v = (BMVert *)ese->ele;
      if ((has_wire == false) && ((v->e == NULL) || BM_vert_is_wire(v))) {
        has_wire = true;
      }
    }
    // all_verts = (ese == NULL);

    if (has_wire == false) {
      /* all verts have faces , connect verts via faces! */
      if (tot == bm->totvertsel) {
        BMEditSelection *ese_last;
        ese_last = bm->selected.first;
        ese = ese_last->next;

        do {

          if (BM_edge_exists((BMVert *)ese_last->ele, (BMVert *)ese->ele)) {
            /* pass, edge exists (and will be selected) */
          }
          else {
            changed |= bm_vert_connect_pair(bm, (BMVert *)ese_last->ele, (BMVert *)ese->ele);
          }
        } while ((void)(ese_last = ese), (ese = ese->next));

        if (changed) {
          return true;
        }
      }

      if (changed == false) {
        /* existing loops: close the selection */
        if (bm_vert_is_select_history_open(bm)) {
          changed |= bm_vert_connect_pair(bm,
                                          (BMVert *)((BMEditSelection *)bm->selected.first)->ele,
                                          (BMVert *)((BMEditSelection *)bm->selected.last)->ele);

          if (changed) {
            return true;
          }
        }
      }
    }

    else {
      /* no faces, simply connect the verts by edges */
      BMEditSelection *ese_prev;
      ese_prev = bm->selected.first;
      ese = ese_prev->next;

      do {
        if (BM_edge_exists((BMVert *)ese_prev->ele, (BMVert *)ese->ele)) {
          /* pass, edge exists (and will be selected) */
        }
        else {
          BMEdge *e;
          e = BM_edge_create(bm, (BMVert *)ese_prev->ele, (BMVert *)ese->ele, NULL, 0);
          BM_edge_select_set(bm, e, true);
          changed = true;
        }
      } while ((void)(ese_prev = ese), (ese = ese->next));

      if (changed == false) {
        /* existing loops: close the selection */
        if (bm_vert_is_select_history_open(bm)) {
          BMEdge *e;
          ese_prev = bm->selected.first;
          ese = bm->selected.last;
          e = BM_edge_create(bm, (BMVert *)ese_prev->ele, (BMVert *)ese->ele, NULL, 0);
          BM_edge_select_set(bm, e, true);
        }
      }

      return true;
    }
  }

  return false;
}

/**
 * Convert an edge selection to a temp vertex selection
 * (which must be cleared after use as a path to connect).
 */
static bool bm_vert_connect_select_history_edge_to_vert_path(BMesh *bm, ListBase *r_selected)
{
  ListBase selected_orig = {NULL, NULL};
  BMEditSelection *ese;
  int edges_len = 0;
  bool side = false;

  /* first check all edges are OK */
  for (ese = bm->selected.first; ese; ese = ese->next) {
    if (ese->htype == BM_EDGE) {
      edges_len += 1;
    }
    else {
      return false;
    }
  }
  /* if this is a mixed selection, bail out! */
  if (bm->totedgesel != edges_len) {
    return false;
  }

  SWAP(ListBase, bm->selected, selected_orig);

  /* convert edge selection into 2 ordered loops (where the first edge ends up in the middle) */
  for (ese = selected_orig.first; ese; ese = ese->next) {
    BMEdge *e_curr = (BMEdge *)ese->ele;
    BMEdge *e_prev = ese->prev ? (BMEdge *)ese->prev->ele : NULL;
    BMLoop *l_curr;
    BMLoop *l_prev;
    BMVert *v;

    if (e_prev) {
      BMFace *f = BM_edge_pair_share_face_by_len(e_curr, e_prev, &l_curr, &l_prev, true);
      if (f) {
        if ((e_curr->v1 != l_curr->v) == (e_prev->v1 != l_prev->v)) {
          side = !side;
        }
      }
      else if (is_quad_flip_v3(e_curr->v1->co, e_curr->v2->co, e_prev->v2->co, e_prev->v1->co)) {
        side = !side;
      }
    }

    v = (&e_curr->v1)[side];
    if (!bm->selected.last || (BMVert *)((BMEditSelection *)bm->selected.last)->ele != v) {
      BM_select_history_store_notest(bm, v);
    }

    v = (&e_curr->v1)[!side];
    if (!bm->selected.first || (BMVert *)((BMEditSelection *)bm->selected.first)->ele != v) {
      BM_select_history_store_head_notest(bm, v);
    }

    e_prev = e_curr;
  }

  *r_selected = bm->selected;
  bm->selected = selected_orig;

  return true;
}

static int edbm_vert_connect_path_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  uint failed_selection_order_len = 0;
  uint failed_connect_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;
    const bool is_pair = (em->bm->totvertsel == 2);
    ListBase selected_orig = {NULL, NULL};

    if (bm->totvertsel == 0) {
      continue;
    }

    /* when there is only 2 vertices, we can ignore selection order */
    if (is_pair) {
      if (!edbm_connect_vert_pair(em, obedit->data, op)) {
        failed_connect_len++;
      }
      continue;
    }

    if (bm->selected.first) {
      BMEditSelection *ese = bm->selected.first;
      if (ese->htype == BM_EDGE) {
        if (bm_vert_connect_select_history_edge_to_vert_path(bm, &selected_orig)) {
          SWAP(ListBase, bm->selected, selected_orig);
        }
      }
    }

    BM_custom_loop_normals_to_vector_layer(bm);

    if (bm_vert_connect_select_history(bm)) {
      EDBM_selectmode_flush(em);

      BM_custom_loop_normals_from_vector_layer(bm, false);

      EDBM_update(obedit->data,
                  &(const struct EDBMUpdate_Params){
                      .calc_looptri = true,
                      .calc_normals = false,
                      .is_destructive = true,
                  });
    }
    else {
      failed_selection_order_len++;
    }

    if (!BLI_listbase_is_empty(&selected_orig)) {
      BM_select_history_clear(bm);
      bm->selected = selected_orig;
    }
  }

  MEM_freeN(objects);

  if (failed_selection_order_len == objects_len) {
    BKE_report(op->reports, RPT_ERROR, "Invalid selection order");
    return OPERATOR_CANCELLED;
  }
  if (failed_connect_len == objects_len) {
    BKE_report(op->reports, RPT_ERROR, "Could not connect vertices");
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

void MESH_OT_vert_connect_path(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Vertex Connect Path";
  ot->idname = "MESH_OT_vert_connect_path";
  ot->description = "Connect vertices by their selection order, creating edges, splitting faces";

  /* api callbacks */
  ot->exec = edbm_vert_connect_path_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** \name Connect Concave Operator
 * \{ */

static int edbm_vert_connect_concave_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (em->bm->totfacesel == 0) {
      continue;
    }

    if (!EDBM_op_call_and_selectf(
            em, op, "faces.out", true, "connect_verts_concave faces=%hf", BM_ELEM_SELECT)) {
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

void MESH_OT_vert_connect_concave(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Split Concave Faces";
  ot->idname = "MESH_OT_vert_connect_concave";
  ot->description = "Make all faces convex";

  /* api callbacks */
  ot->exec = edbm_vert_connect_concave_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** \name Split Non-Planar Faces Operator
 * \{ */

static int edbm_vert_connect_nonplaner_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const float angle_limit = RNA_float_get(op->ptr, "angle_limit");
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (em->bm->totfacesel == 0) {
      continue;
    }

    if (!EDBM_op_call_and_selectf(em,
                                  op,
                                  "faces.out",
                                  true,
                                  "connect_verts_nonplanar faces=%hf angle_limit=%f",
                                  BM_ELEM_SELECT,
                                  angle_limit)) {
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

void MESH_OT_vert_connect_nonplanar(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Split Non-Planar Faces";
  ot->idname = "MESH_OT_vert_connect_nonplanar";
  ot->description = "Split non-planar faces that exceed the angle threshold";

  /* api callbacks */
  ot->exec = edbm_vert_connect_nonplaner_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  prop = RNA_def_float_rotation(ot->srna,
                                "angle_limit",
                                0,
                                NULL,
                                0.0f,
                                DEG2RADF(180.0f),
                                "Max Angle",
                                "Angle limit",
                                0.0f,
                                DEG2RADF(180.0f));
  RNA_def_property_float_default(prop, DEG2RADF(5.0f));
}

/* -------------------------------------------------------------------- */
/** \name Make Planar Faces Operator
 * \{ */

static int edbm_face_make_planar_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  const int repeat = RNA_int_get(op->ptr, "repeat");
  const float fac = RNA_float_get(op->ptr, "factor");

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    if (em->bm->totfacesel == 0) {
      continue;
    }

    if (!EDBM_op_callf(em,
                       op,
                       "planar_faces faces=%hf iterations=%i factor=%f",
                       BM_ELEM_SELECT,
                       repeat,
                       fac)) {
      continue;
    }

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = true,
                    .is_destructive = true,
                });
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_face_make_planar(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Make Planar Faces";
  ot->idname = "MESH_OT_face_make_planar";
  ot->description = "Flatten selected faces";

  /* api callbacks */
  ot->exec = edbm_face_make_planar_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_float(ot->srna, "factor", 1.0f, -10.0f, 10.0f, "Factor", "", 0.0f, 1.0f);
  RNA_def_int(ot->srna, "repeat", 1, 1, 10000, "Iterations", "", 1, 200);
}

/* -------------------------------------------------------------------- */
/** \name Split Edge Operator
 * \{ */

static bool edbm_edge_split_selected_edges(wmOperator *op, Object *obedit, BMEditMesh *em)
{
  BMesh *bm = em->bm;
  if (bm->totedgesel == 0) {
    return false;
  }

  BM_custom_loop_normals_to_vector_layer(em->bm);

  if (!EDBM_op_call_and_selectf(
          em, op, "edges.out", false, "split_edges edges=%he", BM_ELEM_SELECT)) {
    return false;
  }

  BM_custom_loop_normals_from_vector_layer(em->bm, false);

  EDBM_select_flush(em);
  EDBM_update(obedit->data,
              &(const struct EDBMUpdate_Params){
                  .calc_looptri = true,
                  .calc_normals = false,
                  .is_destructive = true,
              });

  return true;
}

static bool edbm_edge_split_selected_verts(wmOperator *op, Object *obedit, BMEditMesh *em)
{
  BMesh *bm = em->bm;

  /* Note that tracking vertices through the 'split_edges' operator is complicated.
   * Instead, tag loops for selection. */
  if (bm->totvertsel == 0) {
    return false;
  }

  BM_custom_loop_normals_to_vector_layer(em->bm);

  /* Flush from vertices to edges. */
  BMIter iter;
  BMEdge *eed;
  BM_ITER_MESH (eed, &iter, bm, BM_EDGES_OF_MESH) {
    BM_elem_flag_disable(eed, BM_ELEM_TAG);
    if (eed->l != NULL) {
      if (!BM_elem_flag_test(eed, BM_ELEM_HIDDEN) &&
          (BM_elem_flag_test(eed->v1, BM_ELEM_SELECT) ||
           BM_elem_flag_test(eed->v2, BM_ELEM_SELECT))) {
        BM_elem_flag_enable(eed, BM_ELEM_TAG);
      }
      /* Store selection in loop tags. */
      BMLoop *l_iter = eed->l;
      do {
        BM_elem_flag_set(l_iter, BM_ELEM_TAG, BM_elem_flag_test(l_iter->v, BM_ELEM_SELECT));
      } while ((l_iter = l_iter->radial_next) != eed->l);
    }
  }

  if (!EDBM_op_callf(em,
                     op,
                     "split_edges edges=%he verts=%hv use_verts=%b",
                     BM_ELEM_TAG,
                     BM_ELEM_SELECT,
                     true)) {
    return false;
  }

  BM_ITER_MESH (eed, &iter, em->bm, BM_EDGES_OF_MESH) {
    if (eed->l != NULL) {
      BMLoop *l_iter = eed->l;
      do {
        if (BM_elem_flag_test(l_iter, BM_ELEM_TAG)) {
          BM_vert_select_set(em->bm, l_iter->v, true);
        }
      } while ((l_iter = l_iter->radial_next) != eed->l);
    }
    else {
      /* Split out wire. */
      for (int i = 0; i < 2; i++) {
        BMVert *v = *(&eed->v1 + i);
        if (BM_elem_flag_test(v, BM_ELEM_SELECT)) {
          if (eed != BM_DISK_EDGE_NEXT(eed, v)) {
            BM_vert_separate(bm, v, &eed, 1, true, NULL, NULL);
          }
        }
      }
    }
  }

  BM_custom_loop_normals_from_vector_layer(em->bm, false);

  EDBM_select_flush(em);
  EDBM_update(obedit->data,
              &(const struct EDBMUpdate_Params){
                  .calc_looptri = true,
                  .calc_normals = false,
                  .is_destructive = true,
              });

  return true;
}

static int edbm_edge_split_exec(bContext *C, wmOperator *op)
{
  const int type = RNA_enum_get(op->ptr, "type");

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    switch (type) {
      case BM_VERT:
        if (!edbm_edge_split_selected_verts(op, obedit, em)) {
          continue;
        }
        break;
      case BM_EDGE:
        if (!edbm_edge_split_selected_edges(op, obedit, em)) {
          continue;
        }
        break;
      default:
        BLI_assert(0);
    }
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_edge_split(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Edge Split";
  ot->idname = "MESH_OT_edge_split";
  ot->description = "Split selected edges so that each neighbor face gets its own copy";

  /* api callbacks */
  ot->exec = edbm_edge_split_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  static const EnumPropertyItem merge_type_items[] = {
      {BM_EDGE, "EDGE", 0, "Faces by Edges", "Split faces along selected edges"},
      {BM_VERT,
       "VERT",
       0,
       "Faces & Edges by Vertices",
       "Split faces and edges connected to selected vertices"},
      {0, NULL, 0, NULL, NULL},
  };

  ot->prop = RNA_def_enum(
      ot->srna, "type", merge_type_items, BM_EDGE, "Type", "Method to use for splitting");
}

/* -------------------------------------------------------------------- */
/** \name Duplicate Operator
 * \{ */

static int edbm_duplicate_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    if (em->bm->totvertsel == 0) {
      continue;
    }

    BMOperator bmop;
    BMesh *bm = em->bm;

    EDBM_op_init(em,
                 &bmop,
                 op,
                 "duplicate geom=%hvef use_select_history=%b use_edge_flip_from_face=%b",
                 BM_ELEM_SELECT,
                 true,
                 true);

    BMO_op_exec(bm, &bmop);

    /* de-select all would clear otherwise */
    BM_SELECT_HISTORY_BACKUP(bm);

    EDBM_flag_disable_all(em, BM_ELEM_SELECT);

    BMO_slot_buffer_hflag_enable(
        bm, bmop.slots_out, "geom.out", BM_ALL_NOLOOP, BM_ELEM_SELECT, true);

    /* rebuild editselection */
    BM_SELECT_HISTORY_RESTORE(bm);

    if (!EDBM_op_finish(em, &bmop, op, true)) {
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

static int edbm_duplicate_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  WM_cursor_wait(true);
  edbm_duplicate_exec(C, op);
  WM_cursor_wait(false);

  return OPERATOR_FINISHED;
}

void MESH_OT_duplicate(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Duplicate";
  ot->description = "Duplicate selected vertices, edges or faces";
  ot->idname = "MESH_OT_duplicate";

  /* api callbacks */
  ot->invoke = edbm_duplicate_invoke;
  ot->exec = edbm_duplicate_exec;

  ot->poll = ED_operator_editmesh;

  /* to give to transform */
  RNA_def_int(ot->srna, "mode", TFM_TRANSLATION, 0, INT_MAX, "Mode", "", 0, INT_MAX);
}

static BMLoopNorEditDataArray *flip_custom_normals_init_data(BMesh *bm)
{
  BMLoopNorEditDataArray *lnors_ed_arr = NULL;
  if (CustomData_has_layer(&bm->ldata, CD_CUSTOMLOOPNORMAL)) {
    /* The mesh has custom normal data, update these too.
     * Otherwise they will be left in a mangled state.
     */
    BM_lnorspace_update(bm);
    lnors_ed_arr = BM_loop_normal_editdata_array_init(bm, true);
  }

  return lnors_ed_arr;
}

static bool flip_custom_normals(BMesh *bm, BMLoopNorEditDataArray *lnors_ed_arr)
{
  if (!lnors_ed_arr) {
    return false;
  }

  if (lnors_ed_arr->totloop == 0) {
    /* No loops normals to flip, exit early! */
    return false;
  }

  bm->spacearr_dirty |= BM_SPACEARR_DIRTY_ALL;
  BM_lnorspace_update(bm);

  /* We need to recreate the custom normal array because the clnors_data will
   * be mangled because we swapped the loops around when we flipped the faces. */
  BMLoopNorEditDataArray *lnors_ed_arr_new_full = BM_loop_normal_editdata_array_init(bm, true);

  {
    /* We need to recalculate all loop normals in the affected area. Even the ones that are not
     * going to be flipped because the clnors data is mangled. */

    BMLoopNorEditData *lnor_ed_new_full = lnors_ed_arr_new_full->lnor_editdata;
    for (int i = 0; i < lnors_ed_arr_new_full->totloop; i++, lnor_ed_new_full++) {

      BMLoopNorEditData *lnor_ed =
          lnors_ed_arr->lidx_to_lnor_editdata[lnor_ed_new_full->loop_index];

      BLI_assert(lnor_ed != NULL);

      BKE_lnor_space_custom_normal_to_data(
          bm->lnor_spacearr->lspacearr[lnor_ed_new_full->loop_index],
          lnor_ed->nloc,
          lnor_ed_new_full->clnors_data);
    }
  }

  BMFace *f;
  BMLoop *l, *l_start;
  BMIter iter_f;
  BM_ITER_MESH (f, &iter_f, bm, BM_FACES_OF_MESH) {
    /* Flip all the custom loop normals on the selected faces. */
    if (!BM_elem_flag_test(f, BM_ELEM_SELECT)) {
      continue;
    }

    /* Because the winding has changed, we need to go the reverse way around the face to get the
     * correct placement of the normals. However we need to derive the old loop index to get the
     * correct data. Note that the first loop index is the same though. So the loop starts and ends
     * in the same place as before the flip.
     */

    l_start = l = BM_FACE_FIRST_LOOP(f);
    int old_index = BM_elem_index_get(l);
    do {
      int loop_index = BM_elem_index_get(l);

      BMLoopNorEditData *lnor_ed = lnors_ed_arr->lidx_to_lnor_editdata[old_index];
      BMLoopNorEditData *lnor_ed_new = lnors_ed_arr_new_full->lidx_to_lnor_editdata[loop_index];
      BLI_assert(lnor_ed != NULL && lnor_ed_new != NULL);

      negate_v3(lnor_ed->nloc);

      BKE_lnor_space_custom_normal_to_data(
          bm->lnor_spacearr->lspacearr[loop_index], lnor_ed->nloc, lnor_ed_new->clnors_data);

      old_index++;
      l = l->prev;
    } while (l != l_start);
  }
  BM_loop_normal_editdata_array_free(lnors_ed_arr_new_full);
  return true;
}

/* -------------------------------------------------------------------- */
/** \name Flip Normals Operator
 * \{ */

static void edbm_flip_normals_custom_loop_normals(Object *obedit, BMEditMesh *em)
{
  if (!CustomData_has_layer(&em->bm->ldata, CD_CUSTOMLOOPNORMAL)) {
    return;
  }

  /* The mesh has custom normal data, flip them. */
  BMesh *bm = em->bm;

  BM_lnorspace_update(bm);
  BMLoopNorEditDataArray *lnors_ed_arr = BM_loop_normal_editdata_array_init(bm, false);
  BMLoopNorEditData *lnor_ed = lnors_ed_arr->lnor_editdata;

  for (int i = 0; i < lnors_ed_arr->totloop; i++, lnor_ed++) {
    negate_v3(lnor_ed->nloc);

    BKE_lnor_space_custom_normal_to_data(
        bm->lnor_spacearr->lspacearr[lnor_ed->loop_index], lnor_ed->nloc, lnor_ed->clnors_data);
  }
  BM_loop_normal_editdata_array_free(lnors_ed_arr);
  EDBM_update(obedit->data,
              &(const struct EDBMUpdate_Params){
                  .calc_looptri = true,
                  .calc_normals = false,
                  .is_destructive = false,
              });
}

static void edbm_flip_normals_face_winding(wmOperator *op, Object *obedit, BMEditMesh *em)
{

  bool has_flipped_faces = false;

  /* See if we have any custom normals to flip. */
  BMLoopNorEditDataArray *lnors_ed_arr = flip_custom_normals_init_data(em->bm);

  if (EDBM_op_callf(em, op, "reverse_faces faces=%hf flip_multires=%b", BM_ELEM_SELECT, true)) {
    has_flipped_faces = true;
  }

  if (flip_custom_normals(em->bm, lnors_ed_arr) || has_flipped_faces) {
    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }

  if (lnors_ed_arr != NULL) {
    BM_loop_normal_editdata_array_free(lnors_ed_arr);
  }
}

static int edbm_flip_normals_exec(bContext *C, wmOperator *op)
{
  const bool only_clnors = RNA_boolean_get(op->ptr, "only_clnors");

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (only_clnors) {
      if ((em->bm->totvertsel == 0) && (em->bm->totedgesel == 0) && (em->bm->totfacesel == 0)) {
        continue;
      }
      edbm_flip_normals_custom_loop_normals(obedit, em);
    }
    else {
      if (em->bm->totfacesel == 0) {
        continue;
      }
      edbm_flip_normals_face_winding(op, obedit, em);
    }
  }

  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

void MESH_OT_flip_normals(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Flip Normals";
  ot->description = "Flip the direction of selected faces' normals (and of their vertices)";
  ot->idname = "MESH_OT_flip_normals";

  /* api callbacks */
  ot->exec = edbm_flip_normals_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_boolean(ot->srna,
                  "only_clnors",
                  false,
                  "Custom Normals Only",
                  "Only flip the custom loop normals of the selected elements");
}



