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

/* -------------------------------------------------------------------- */
/** \name Rotate Edge Operator
 * \{ */

/**
 * Rotate the edges between selected faces, otherwise rotate the selected edges.
 */
static int edbm_edge_rotate_selected_exec(bContext *C, wmOperator *op)
{
  BMEdge *eed;
  BMIter iter;
  const bool use_ccw = RNA_boolean_get(op->ptr, "use_ccw");

  int tot_failed_all = 0;
  bool no_selected_edges = true, invalid_selected_edges = true;

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    int tot = 0;

    if (em->bm->totedgesel == 0) {
      continue;
    }
    no_selected_edges = false;

    /* first see if we have two adjacent faces */
    BM_ITER_MESH (eed, &iter, em->bm, BM_EDGES_OF_MESH) {
      BM_elem_flag_disable(eed, BM_ELEM_TAG);
      if (BM_elem_flag_test(eed, BM_ELEM_SELECT)) {
        BMFace *fa, *fb;
        if (BM_edge_face_pair(eed, &fa, &fb)) {
          /* if both faces are selected we rotate between them,
           * otherwise - rotate between 2 unselected - but not mixed */
          if (BM_elem_flag_test(fa, BM_ELEM_SELECT) == BM_elem_flag_test(fb, BM_ELEM_SELECT)) {
            BM_elem_flag_enable(eed, BM_ELEM_TAG);
            tot++;
          }
        }
      }
    }

    /* OK, we don't have two adjacent faces, but we do have two selected ones.
     * that's an error condition. */
    if (tot == 0) {
      continue;
    }
    invalid_selected_edges = false;

    BMOperator bmop;
    EDBM_op_init(em, &bmop, op, "rotate_edges edges=%he use_ccw=%b", BM_ELEM_TAG, use_ccw);

    /* avoids leaving old verts selected which can be a problem running multiple times,
     * since this means the edges become selected around the face
     * which then attempt to rotate */
    BMO_slot_buffer_hflag_disable(em->bm, bmop.slots_in, "edges", BM_EDGE, BM_ELEM_SELECT, true);

    BMO_op_exec(em->bm, &bmop);
    /* edges may rotate into hidden vertices, if this does _not_ run we get an illogical state */
    BMO_slot_buffer_hflag_disable(
        em->bm, bmop.slots_out, "edges.out", BM_EDGE, BM_ELEM_HIDDEN, true);
    BMO_slot_buffer_hflag_enable(
        em->bm, bmop.slots_out, "edges.out", BM_EDGE, BM_ELEM_SELECT, true);

    const int tot_rotate = BMO_slot_buffer_len(bmop.slots_out, "edges.out");
    const int tot_failed = tot - tot_rotate;

    tot_failed_all += tot_failed;

    if (tot_failed != 0) {
      /* If some edges fail to rotate, we need to re-select them,
       * otherwise we can end up with invalid selection
       * (unselected edge between 2 selected faces). */
      BM_mesh_elem_hflag_enable_test(em->bm, BM_EDGE, BM_ELEM_SELECT, true, false, BM_ELEM_TAG);
    }

    EDBM_selectmode_flush(em);

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

  if (no_selected_edges) {
    BKE_report(
        op->reports, RPT_ERROR, "Select edges or face pairs for edge loops to rotate about");
    return OPERATOR_CANCELLED;
  }

  /* Ok, we don't have two adjacent faces, but we do have two selected ones.
   * that's an error condition. */
  if (invalid_selected_edges) {
    BKE_report(op->reports, RPT_ERROR, "Could not find any selected edges that can be rotated");
    return OPERATOR_CANCELLED;
  }

  if (tot_failed_all != 0) {
    BKE_reportf(op->reports, RPT_WARNING, "Unable to rotate %d edge(s)", tot_failed_all);
  }

  return OPERATOR_FINISHED;
}

void MESH_OT_edge_rotate(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Rotate Selected Edge";
  ot->description = "Rotate selected edge or adjoining faces";
  ot->idname = "MESH_OT_edge_rotate";

  /* api callbacks */
  ot->exec = edbm_edge_rotate_selected_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_boolean(ot->srna, "use_ccw", false, "Counter Clockwise", "");
}

/* -------------------------------------------------------------------- */
/** \name Hide Operator
 * \{ */

static int edbm_hide_exec(bContext *C, wmOperator *op)
{
  const bool unselected = RNA_boolean_get(op->ptr, "unselected");
  ViewLayer *view_layer = CTX_data_view_layer(C);
  bool changed = false;

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;

    if (unselected) {
      if (em->selectmode & SCE_SELECT_VERTEX) {
        if (bm->totvertsel == bm->totvert) {
          continue;
        }
      }
      else if (em->selectmode & SCE_SELECT_EDGE) {
        if (bm->totedgesel == bm->totedge) {
          continue;
        }
      }
      else if (em->selectmode & SCE_SELECT_FACE) {
        if (bm->totfacesel == bm->totface) {
          continue;
        }
      }
    }
    else {
      if (bm->totvertsel == 0) {
        continue;
      }
    }

    if (EDBM_mesh_hide(em, unselected)) {
      EDBM_update(obedit->data,
                  &(const struct EDBMUpdate_Params){
                      .calc_looptri = true,
                      .calc_normals = false,
                      .is_destructive = false,
                  });
      changed = true;
    }
  }
  MEM_freeN(objects);

  if (!changed) {
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

void MESH_OT_hide(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Hide Selected";
  ot->idname = "MESH_OT_hide";
  ot->description = "Hide (un)selected vertices, edges or faces";

  /* api callbacks */
  ot->exec = edbm_hide_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_boolean(
      ot->srna, "unselected", false, "Unselected", "Hide unselected rather than selected");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Reveal Operator
 * \{ */

static int edbm_reveal_exec(bContext *C, wmOperator *op)
{
  const bool select = RNA_boolean_get(op->ptr, "select");
  ViewLayer *view_layer = CTX_data_view_layer(C);

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (EDBM_mesh_reveal(em, select)) {
      EDBM_update(obedit->data,
                  &(const struct EDBMUpdate_Params){
                      .calc_looptri = true,
                      .calc_normals = false,
                      .is_destructive = false,
                  });
    }
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_reveal(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reveal Hidden";
  ot->idname = "MESH_OT_reveal";
  ot->description = "Reveal all hidden vertices, edges and faces";

  /* api callbacks */
  ot->exec = edbm_reveal_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_boolean(ot->srna, "select", true, "Select", "");
}

/* -------------------------------------------------------------------- */
/** \name Recalculate Normals Operator
 * \{ */

static int edbm_normals_make_consistent_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const bool inside = RNA_boolean_get(op->ptr, "inside");

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (em->bm->totfacesel == 0) {
      continue;
    }

    BMLoopNorEditDataArray *lnors_ed_arr = NULL;

    if (inside) {
      /* Save custom normal data for later so we can flip them correctly. */
      lnors_ed_arr = flip_custom_normals_init_data(em->bm);
    }

    if (!EDBM_op_callf(em, op, "recalc_face_normals faces=%hf", BM_ELEM_SELECT)) {
      continue;
    }

    if (inside) {
      EDBM_op_callf(em, op, "reverse_faces faces=%hf flip_multires=%b", BM_ELEM_SELECT, true);
      flip_custom_normals(em->bm, lnors_ed_arr);
      if (lnors_ed_arr != NULL) {
        BM_loop_normal_editdata_array_free(lnors_ed_arr);
      }
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

void MESH_OT_normals_make_consistent(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Recalculate Normals";
  ot->description = "Make face and vertex normals point either outside or inside the mesh";
  ot->idname = "MESH_OT_normals_make_consistent";

  /* api callbacks */
  ot->exec = edbm_normals_make_consistent_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_boolean(ot->srna, "inside", false, "Inside", "");
}

/* -------------------------------------------------------------------- */
/** \name Smooth Vertices Operator
 * \{ */

static int edbm_do_smooth_vertex_exec(bContext *C, wmOperator *op)
{
  const float fac = RNA_float_get(op->ptr, "factor");

  const bool xaxis = RNA_boolean_get(op->ptr, "xaxis");
  const bool yaxis = RNA_boolean_get(op->ptr, "yaxis");
  const bool zaxis = RNA_boolean_get(op->ptr, "zaxis");
  int repeat = RNA_int_get(op->ptr, "repeat");

  if (!repeat) {
    repeat = 1;
  }

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Mesh *me = obedit->data;
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    bool mirrx = false, mirry = false, mirrz = false;
    float clip_dist = 0.0f;
    const bool use_topology = (me->editflag & ME_EDIT_MIRROR_TOPO) != 0;

    if (em->bm->totvertsel == 0) {
      continue;
    }

    /* mirror before smooth */
    if (((Mesh *)obedit->data)->symmetry & ME_SYMMETRY_X) {
      EDBM_verts_mirror_cache_begin(em, 0, false, true, false, use_topology);
    }

    /* if there is a mirror modifier with clipping, flag the verts that
     * are within tolerance of the plane(s) of reflection
     */
    LISTBASE_FOREACH (ModifierData *, md, &obedit->modifiers) {
      if (md->type == eModifierType_Mirror && (md->mode & eModifierMode_Realtime)) {
        MirrorModifierData *mmd = (MirrorModifierData *)md;

        if (mmd->flag & MOD_MIR_CLIPPING) {
          if (mmd->flag & MOD_MIR_AXIS_X) {
            mirrx = true;
          }
          if (mmd->flag & MOD_MIR_AXIS_Y) {
            mirry = true;
          }
          if (mmd->flag & MOD_MIR_AXIS_Z) {
            mirrz = true;
          }

          clip_dist = mmd->tolerance;
        }
      }
    }

    for (int i = 0; i < repeat; i++) {
      if (!EDBM_op_callf(
              em,
              op,
              "smooth_vert verts=%hv factor=%f mirror_clip_x=%b mirror_clip_y=%b mirror_clip_z=%b "
              "clip_dist=%f use_axis_x=%b use_axis_y=%b use_axis_z=%b",
              BM_ELEM_SELECT,
              fac,
              mirrx,
              mirry,
              mirrz,
              clip_dist,
              xaxis,
              yaxis,
              zaxis)) {
        continue;
      }
    }

    /* apply mirror */
    if (((Mesh *)obedit->data)->symmetry & ME_SYMMETRY_X) {
      EDBM_verts_mirror_apply(em, BM_ELEM_SELECT, 0);
      EDBM_verts_mirror_cache_end(em);
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

void MESH_OT_vertices_smooth(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Smooth Vertices";
  ot->description = "Flatten angles of selected vertices";
  ot->idname = "MESH_OT_vertices_smooth";

  /* api callbacks */
  ot->exec = edbm_do_smooth_vertex_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = RNA_def_float_factor(
      ot->srna, "factor", 0.0f, -10.0f, 10.0f, "Smoothing", "Smoothing factor", 0.0f, 1.0f);
  RNA_def_int(
      ot->srna, "repeat", 1, 1, 1000, "Repeat", "Number of times to smooth the mesh", 1, 100);

  WM_operatortype_props_advanced_begin(ot);

  RNA_def_boolean(ot->srna, "xaxis", true, "X-Axis", "Smooth along the X axis");
  RNA_def_boolean(ot->srna, "yaxis", true, "Y-Axis", "Smooth along the Y axis");
  RNA_def_boolean(ot->srna, "zaxis", true, "Z-Axis", "Smooth along the Z axis");

  /* Set generic modal callbacks. */
  WM_operator_type_modal_from_exec_for_object_edit_coords(ot);
}

/* -------------------------------------------------------------------- */
/** \name Laplacian Smooth Vertices Operator
 * \{ */

static int edbm_do_smooth_laplacian_vertex_exec(bContext *C, wmOperator *op)
{
  int tot_unselected = 0;
  ViewLayer *view_layer = CTX_data_view_layer(C);

  const float lambda_factor = RNA_float_get(op->ptr, "lambda_factor");
  const float lambda_border = RNA_float_get(op->ptr, "lambda_border");
  const bool usex = RNA_boolean_get(op->ptr, "use_x");
  const bool usey = RNA_boolean_get(op->ptr, "use_y");
  const bool usez = RNA_boolean_get(op->ptr, "use_z");
  const bool preserve_volume = RNA_boolean_get(op->ptr, "preserve_volume");
  int repeat = RNA_int_get(op->ptr, "repeat");

  if (!repeat) {
    repeat = 1;
  }

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    Mesh *me = obedit->data;
    bool use_topology = (me->editflag & ME_EDIT_MIRROR_TOPO) != 0;

    if (em->bm->totvertsel == 0) {
      tot_unselected++;
      continue;
    }

    /* Mirror before smooth. */
    if (((Mesh *)obedit->data)->symmetry & ME_SYMMETRY_X) {
      EDBM_verts_mirror_cache_begin(em, 0, false, true, false, use_topology);
    }

    bool failed_repeat_loop = false;
    for (int i = 0; i < repeat; i++) {
      if (!EDBM_op_callf(em,
                         op,
                         "smooth_laplacian_vert verts=%hv lambda_factor=%f lambda_border=%f "
                         "use_x=%b use_y=%b use_z=%b preserve_volume=%b",
                         BM_ELEM_SELECT,
                         lambda_factor,
                         lambda_border,
                         usex,
                         usey,
                         usez,
                         preserve_volume)) {
        failed_repeat_loop = true;
        break;
      }
    }
    if (failed_repeat_loop) {
      continue;
    }

    /* Apply mirror. */
    if (((Mesh *)obedit->data)->symmetry & ME_SYMMETRY_X) {
      EDBM_verts_mirror_apply(em, BM_ELEM_SELECT, 0);
      EDBM_verts_mirror_cache_end(em);
    }

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }
  MEM_freeN(objects);

  if (tot_unselected == objects_len) {
    BKE_report(op->reports, RPT_WARNING, "No selected vertex");
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

void MESH_OT_vertices_smooth_laplacian(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Laplacian Smooth Vertices";
  ot->description = "Laplacian smooth of selected vertices";
  ot->idname = "MESH_OT_vertices_smooth_laplacian";

  /* api callbacks */
  ot->exec = edbm_do_smooth_laplacian_vertex_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_int(
      ot->srna, "repeat", 1, 1, 1000, "Number of iterations to smooth the mesh", "", 1, 200);
  RNA_def_float(
      ot->srna, "lambda_factor", 1.0f, 1e-7f, 1000.0f, "Lambda factor", "", 1e-7f, 1000.0f);
  RNA_def_float(ot->srna,
                "lambda_border",
                5e-5f,
                1e-7f,
                1000.0f,
                "Lambda factor in border",
                "",
                1e-7f,
                1000.0f);

  WM_operatortype_props_advanced_begin(ot);

  RNA_def_boolean(ot->srna, "use_x", true, "Smooth X Axis", "Smooth object along X axis");
  RNA_def_boolean(ot->srna, "use_y", true, "Smooth Y Axis", "Smooth object along Y axis");
  RNA_def_boolean(ot->srna, "use_z", true, "Smooth Z Axis", "Smooth object along Z axis");
  RNA_def_boolean(ot->srna,
                  "preserve_volume",
                  true,
                  "Preserve Volume",
                  "Apply volume preservation after smooth");
}

/* -------------------------------------------------------------------- */
/** \name Set Faces Smooth Shading Operator
 * \{ */

static void mesh_set_smooth_faces(BMEditMesh *em, short smooth)
{
  BMIter iter;
  BMFace *efa;

  if (em == NULL) {
    return;
  }

  BM_ITER_MESH (efa, &iter, em->bm, BM_FACES_OF_MESH) {
    if (BM_elem_flag_test(efa, BM_ELEM_SELECT)) {
      BM_elem_flag_set(efa, BM_ELEM_SMOOTH, smooth);
    }
  }
}

static int edbm_faces_shade_smooth_exec(bContext *C, wmOperator *UNUSED(op))
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

    mesh_set_smooth_faces(em, 1);
    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = false,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_faces_shade_smooth(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Shade Smooth";
  ot->description = "Display faces smooth (using vertex normals)";
  ot->idname = "MESH_OT_faces_shade_smooth";

  /* api callbacks */
  ot->exec = edbm_faces_shade_smooth_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** \name Set Faces Flat Shading Operator
 * \{ */

static int edbm_faces_shade_flat_exec(bContext *C, wmOperator *UNUSED(op))
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

    mesh_set_smooth_faces(em, 0);
    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = false,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_faces_shade_flat(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Shade Flat";
  ot->description = "Display faces flat";
  ot->idname = "MESH_OT_faces_shade_flat";

  /* api callbacks */
  ot->exec = edbm_faces_shade_flat_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** \name UV/Color Rotate/Reverse Operator
 * \{ */

static int edbm_rotate_uvs_exec(bContext *C, wmOperator *op)
{
  /* get the direction from RNA */
  const bool use_ccw = RNA_boolean_get(op->ptr, "use_ccw");

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

    BMOperator bmop;

    EDBM_op_init(em, &bmop, op, "rotate_uvs faces=%hf use_ccw=%b", BM_ELEM_SELECT, use_ccw);

    BMO_op_exec(em->bm, &bmop);

    if (!EDBM_op_finish(em, &bmop, op, true)) {
      continue;
    }

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = false,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }

  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

static int edbm_reverse_uvs_exec(bContext *C, wmOperator *op)
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

    BMOperator bmop;

    EDBM_op_init(em, &bmop, op, "reverse_uvs faces=%hf", BM_ELEM_SELECT);

    BMO_op_exec(em->bm, &bmop);

    if (!EDBM_op_finish(em, &bmop, op, true)) {
      continue;
    }
    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = false,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }

  MEM_freeN(objects);
  return OPERATOR_FINISHED;
}

static int edbm_rotate_colors_exec(bContext *C, wmOperator *op)
{
  /* get the direction from RNA */
  const bool use_ccw = RNA_boolean_get(op->ptr, "use_ccw");

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *ob = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(ob);
    if (em->bm->totfacesel == 0) {
      continue;
    }

    BMOperator bmop;

    EDBM_op_init(em, &bmop, op, "rotate_colors faces=%hf use_ccw=%b", BM_ELEM_SELECT, use_ccw);

    BMO_op_exec(em->bm, &bmop);

    if (!EDBM_op_finish(em, &bmop, op, true)) {
      continue;
    }

    /* dependencies graph and notification stuff */
    EDBM_update(ob->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = false,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

static int edbm_reverse_colors_exec(bContext *C, wmOperator *op)
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

    BMOperator bmop;

    EDBM_op_init(em, &bmop, op, "reverse_colors faces=%hf", BM_ELEM_SELECT);

    BMO_op_exec(em->bm, &bmop);

    if (!EDBM_op_finish(em, &bmop, op, true)) {
      continue;
    }

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = false,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }
  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

void MESH_OT_uvs_rotate(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Rotate UVs";
  ot->idname = "MESH_OT_uvs_rotate";
  ot->description = "Rotate UV coordinates inside faces";

  /* api callbacks */
  ot->exec = edbm_rotate_uvs_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_boolean(ot->srna, "use_ccw", false, "Counter Clockwise", "");
}

void MESH_OT_uvs_reverse(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reverse UVs";
  ot->idname = "MESH_OT_uvs_reverse";
  ot->description = "Flip direction of UV coordinates inside faces";

  /* api callbacks */
  ot->exec = edbm_reverse_uvs_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  // RNA_def_enum(ot->srna, "axis", axis_items, DIRECTION_CW, "Axis", "Axis to mirror UVs around");
}

void MESH_OT_colors_rotate(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Rotate Colors";
  ot->idname = "MESH_OT_colors_rotate";
  ot->description = "Rotate vertex colors inside faces";

  /* api callbacks */
  ot->exec = edbm_rotate_colors_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  RNA_def_boolean(ot->srna, "use_ccw", false, "Counter Clockwise", "");
}

void MESH_OT_colors_reverse(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reverse Colors";
  ot->idname = "MESH_OT_colors_reverse";
  ot->description = "Flip direction of vertex colors inside faces";

  /* api callbacks */
  ot->exec = edbm_reverse_colors_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
#if 0
  RNA_def_enum(ot->srna, "axis", axis_items, DIRECTION_CW, "Axis", "Axis to mirror colors around");
#endif
}

/* -------------------------------------------------------------------- */
/** \name Merge Vertices Operator
 * \{ */

enum {
  MESH_MERGE_LAST = 1,
  MESH_MERGE_CENTER = 3,
  MESH_MERGE_CURSOR = 4,
  MESH_MERGE_COLLAPSE = 5,
  MESH_MERGE_FIRST = 6,
};

static bool merge_firstlast(BMEditMesh *em,
                            const bool use_first,
                            const bool use_uvmerge,
                            wmOperator *wmop)
{
  BMVert *mergevert;
  BMEditSelection *ese;

  /* operator could be called directly from shortcut or python,
   * so do extra check for data here
   */

  /* While #merge_type_itemf does a sanity check, this operation runs on all edit-mode objects.
   * Some of them may not have the expected selection state. */
  if (use_first == false) {
    if (!em->bm->selected.last || ((BMEditSelection *)em->bm->selected.last)->htype != BM_VERT) {
      return false;
    }

    ese = em->bm->selected.last;
    mergevert = (BMVert *)ese->ele;
  }
  else {
    if (!em->bm->selected.first || ((BMEditSelection *)em->bm->selected.first)->htype != BM_VERT) {
      return false;
    }

    ese = em->bm->selected.first;
    mergevert = (BMVert *)ese->ele;
  }

  if (!BM_elem_flag_test(mergevert, BM_ELEM_SELECT)) {
    return false;
  }

  if (use_uvmerge) {
    if (!EDBM_op_callf(
            em, wmop, "pointmerge_facedata verts=%hv vert_snap=%e", BM_ELEM_SELECT, mergevert)) {
      return false;
    }
  }

  if (!EDBM_op_callf(
          em, wmop, "pointmerge verts=%hv merge_co=%v", BM_ELEM_SELECT, mergevert->co)) {
    return false;
  }

  return true;
}

static bool merge_target(BMEditMesh *em,
                         Scene *scene,
                         Object *ob,
                         const bool use_cursor,
                         const bool use_uvmerge,
                         wmOperator *wmop)
{
  BMIter iter;
  BMVert *v;
  float co[3], cent[3] = {0.0f, 0.0f, 0.0f};
  const float *vco = NULL;

  if (use_cursor) {
    vco = scene->cursor.location;
    copy_v3_v3(co, vco);
    invert_m4_m4(ob->imat, ob->obmat);
    mul_m4_v3(ob->imat, co);
  }
  else {
    float fac;
    int i = 0;
    BM_ITER_MESH (v, &iter, em->bm, BM_VERTS_OF_MESH) {
      if (!BM_elem_flag_test(v, BM_ELEM_SELECT)) {
        continue;
      }
      add_v3_v3(cent, v->co);
      i++;
    }

    if (!i) {
      return false;
    }

    fac = 1.0f / (float)i;
    mul_v3_fl(cent, fac);
    copy_v3_v3(co, cent);
    vco = co;
  }

  if (!vco) {
    return false;
  }

  if (use_uvmerge) {
    if (!EDBM_op_callf(em, wmop, "average_vert_facedata verts=%hv", BM_ELEM_SELECT)) {
      return false;
    }
  }

  if (!EDBM_op_callf(em, wmop, "pointmerge verts=%hv merge_co=%v", BM_ELEM_SELECT, co)) {
    return false;
  }

  return true;
}

static int edbm_merge_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  const int type = RNA_enum_get(op->ptr, "type");
  const bool uvs = RNA_boolean_get(op->ptr, "uvs");

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    if (em->bm->totvertsel == 0) {
      continue;
    }

    BM_custom_loop_normals_to_vector_layer(em->bm);

    bool ok = false;
    switch (type) {
      case MESH_MERGE_CENTER:
        ok = merge_target(em, scene, obedit, false, uvs, op);
        break;
      case MESH_MERGE_CURSOR:
        ok = merge_target(em, scene, obedit, true, uvs, op);
        break;
      case MESH_MERGE_LAST:
        ok = merge_firstlast(em, false, uvs, op);
        break;
      case MESH_MERGE_FIRST:
        ok = merge_firstlast(em, true, uvs, op);
        break;
      case MESH_MERGE_COLLAPSE:
        ok = EDBM_op_callf(em, op, "collapse edges=%he uvs=%b", BM_ELEM_SELECT, uvs);
        break;
      default:
        BLI_assert(0);
        break;
    }

    if (!ok) {
      continue;
    }

    BM_custom_loop_normals_from_vector_layer(em->bm, false);

    EDBM_update(obedit->data,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = true,
                    .calc_normals = false,
                    .is_destructive = true,
                });

    /* once collapsed, we can't have edge/face selection */
    if ((em->selectmode & SCE_SELECT_VERTEX) == 0) {
      EDBM_flag_disable_all(em, BM_ELEM_SELECT);
    }
    /* Only active object supported, see comment below. */
    if (ELEM(type, MESH_MERGE_FIRST, MESH_MERGE_LAST)) {
      break;
    }
  }

  MEM_freeN(objects);

  return OPERATOR_FINISHED;
}

static const EnumPropertyItem merge_type_items[] = {
    {MESH_MERGE_CENTER, "CENTER", 0, "At Center", ""},
    {MESH_MERGE_CURSOR, "CURSOR", 0, "At Cursor", ""},
    {MESH_MERGE_COLLAPSE, "COLLAPSE", 0, "Collapse", ""},
    {MESH_MERGE_FIRST, "FIRST", 0, "At First", ""},
    {MESH_MERGE_LAST, "LAST", 0, "At Last", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem *merge_type_itemf(bContext *C,
                                                PointerRNA *UNUSED(ptr),
                                                PropertyRNA *UNUSED(prop),
                                                bool *r_free)
{
  if (!C) { /* needed for docs */
    return merge_type_items;
  }

  Object *obedit = CTX_data_edit_object(C);
  if (obedit && obedit->type == OB_MESH) {
    EnumPropertyItem *item = NULL;
    int totitem = 0;
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    /* Keep these first so that their automatic shortcuts don't change. */
    RNA_enum_items_add_value(&item, &totitem, merge_type_items, MESH_MERGE_CENTER);
    RNA_enum_items_add_value(&item, &totitem, merge_type_items, MESH_MERGE_CURSOR);
    RNA_enum_items_add_value(&item, &totitem, merge_type_items, MESH_MERGE_COLLAPSE);

    /* Only active object supported:
     * In practice it doesn't make sense to run this operation on non-active meshes
     * since selecting will activate - we could have own code-path for these but it's a hassle
     * for now just apply to the active (first) object. */
    if (em->selectmode & SCE_SELECT_VERTEX) {
      if (em->bm->selected.first && em->bm->selected.last &&
          ((BMEditSelection *)em->bm->selected.first)->htype == BM_VERT &&
          ((BMEditSelection *)em->bm->selected.last)->htype == BM_VERT) {
        RNA_enum_items_add_value(&item, &totitem, merge_type_items, MESH_MERGE_FIRST);
        RNA_enum_items_add_value(&item, &totitem, merge_type_items, MESH_MERGE_LAST);
      }
      else if (em->bm->selected.first &&
               ((BMEditSelection *)em->bm->selected.first)->htype == BM_VERT) {
        RNA_enum_items_add_value(&item, &totitem, merge_type_items, MESH_MERGE_FIRST);
      }
      else if (em->bm->selected.last &&
               ((BMEditSelection *)em->bm->selected.last)->htype == BM_VERT) {
        RNA_enum_items_add_value(&item, &totitem, merge_type_items, MESH_MERGE_LAST);
      }
    }

    RNA_enum_item_end(&item, &totitem);

    *r_free = true;

    return item;
  }

  /* Get all items e.g. when creating keymap item. */
  return merge_type_items;
}

void MESH_OT_merge(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Merge";
  ot->description = "Merge selected vertices";
  ot->idname = "MESH_OT_merge";

  /* api callbacks */
  ot->exec = edbm_merge_exec;
  ot->invoke = WM_menu_invoke;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = RNA_def_enum(
      ot->srna, "type", merge_type_items, MESH_MERGE_CENTER, "Type", "Merge method to use");
  RNA_def_enum_funcs(ot->prop, merge_type_itemf);

  WM_operatortype_props_advanced_begin(ot);

  RNA_def_boolean(ot->srna, "uvs", false, "UVs", "Move UVs according to merge");
}

/* -------------------------------------------------------------------- */
/** \name Merge By Distance Operator
 * \{ */

static int edbm_remove_doubles_exec(bContext *C, wmOperator *op)
{
  const float threshold = RNA_float_get(op->ptr, "threshold");
  const bool use_unselected = RNA_boolean_get(op->ptr, "use_unselected");
  const bool use_sharp_edge_from_normals = RNA_boolean_get(op->ptr, "use_sharp_edge_from_normals");

  int count_multi = 0;

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);

  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);

    /* Selection used as target with 'use_unselected'. */
    if (em->bm->totvertsel == 0) {
      continue;
    }

    BMOperator bmop;
    const int totvert_orig = em->bm->totvert;

    /* avoid losing selection state (select -> tags) */
    char htype_select;
    if (em->selectmode & SCE_SELECT_VERTEX) {
      htype_select = BM_VERT;
    }
    else if (em->selectmode & SCE_SELECT_EDGE) {
      htype_select = BM_EDGE;
    }
    else {
      htype_select = BM_FACE;
    }

    BM_custom_loop_normals_to_vector_layer(em->bm);

    /* store selection as tags */
    BM_mesh_elem_hflag_enable_test(em->bm, htype_select, BM_ELEM_TAG, true, true, BM_ELEM_SELECT);

    if (use_unselected) {
      EDBM_automerge(obedit, false, BM_ELEM_SELECT, threshold);
    }
    else {
      EDBM_op_init(em, &bmop, op, "find_doubles verts=%hv dist=%f", BM_ELEM_SELECT, threshold);

      BMO_op_exec(em->bm, &bmop);

      if (!EDBM_op_callf(em, op, "weld_verts targetmap=%S", &bmop, "targetmap.out")) {
        BMO_op_finish(em->bm, &bmop);
        continue;
      }

      if (!EDBM_op_finish(em, &bmop, op, true)) {
        continue;
      }
    }

    const int count = (totvert_orig - em->bm->totvert);

    /* restore selection from tags */
    BM_mesh_elem_hflag_enable_test(em->bm, htype_select, BM_ELEM_SELECT, true, true, BM_ELEM_TAG);
    EDBM_selectmode_flush(em);

    BM_custom_loop_normals_from_vector_layer(em->bm, use_sharp_edge_from_normals);

    if (count) {
      count_multi += count;
      EDBM_update(obedit->data,
                  &(const struct EDBMUpdate_Params){
                      .calc_looptri = true,
                      .calc_normals = false,
                      .is_destructive = true,
                  });
    }
  }
  MEM_freeN(objects);

  BKE_reportf(op->reports, RPT_INFO, "Removed %d vertice(s)", count_multi);

  return OPERATOR_FINISHED;
}

void MESH_OT_remove_doubles(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Merge by Distance";
  ot->description = "Merge vertices based on their proximity";
  ot->idname = "MESH_OT_remove_doubles";

  /* api callbacks */
  ot->exec = edbm_remove_doubles_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_float_distance(ot->srna,
                         "threshold",
                         1e-4f,
                         1e-6f,
                         50.0f,
                         "Merge Distance",
                         "Maximum distance between elements to merge",
                         1e-5f,
                         10.0f);
  RNA_def_boolean(ot->srna,
                  "use_unselected",
                  false,
                  "Unselected",
                  "Merge selected to other unselected vertices");

  RNA_def_boolean(ot->srna,
                  "use_sharp_edge_from_normals",
                  false,
                  "Sharp Edges",
                  "Calculate sharp edges using custom normal data (when available)");
}

/* -------------------------------------------------------------------- */
/** \name Shape Key Propagate Operator
 * \{ */

/* BMESH_TODO this should be properly encapsulated in a bmop.  but later. */
static bool shape_propagate(BMEditMesh *em)
{
  BMIter iter;
  BMVert *eve = NULL;
  float *co;
  int totshape = CustomData_number_of_layers(&em->bm->vdata, CD_SHAPEKEY);

  if (!CustomData_has_layer(&em->bm->vdata, CD_SHAPEKEY)) {
    return false;
  }

  BM_ITER_MESH (eve, &iter, em->bm, BM_VERTS_OF_MESH) {
    if (!BM_elem_flag_test(eve, BM_ELEM_SELECT) || BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
      continue;
    }

    for (int i = 0; i < totshape; i++) {
      co = CustomData_bmesh_get_n(&em->bm->vdata, eve->head.data, CD_SHAPEKEY, i);
      copy_v3_v3(co, eve->co);
    }
  }
  return true;
}

static int edbm_shape_propagate_to_all_exec(bContext *C, wmOperator *op)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  int tot_shapekeys = 0;
  int tot_selected_verts_objects = 0;

  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Mesh *me = obedit->data;
    BMEditMesh *em = me->edit_mesh;

    if (em->bm->totvertsel == 0) {
      continue;
    }
    tot_selected_verts_objects++;

    if (shape_propagate(em)) {
      tot_shapekeys++;
    }

    EDBM_update(me,
                &(const struct EDBMUpdate_Params){
                    .calc_looptri = false,
                    .calc_normals = false,
                    .is_destructive = false,
                });
  }
  MEM_freeN(objects);

  if (tot_selected_verts_objects == 0) {
    BKE_report(op->reports, RPT_ERROR, "No selected vertex");
    return OPERATOR_CANCELLED;
  }
  if (tot_shapekeys == 0) {
    BKE_report(op->reports, RPT_ERROR, "Mesh(es) do not have shape keys");
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

void MESH_OT_shape_propagate_to_all(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Shape Propagate";
  ot->description = "Apply selected vertex locations to all other shape keys";
  ot->idname = "MESH_OT_shape_propagate_to_all";

  /* api callbacks */
  ot->exec = edbm_shape_propagate_to_all_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}


/* -------------------------------------------------------------------- */
/** \name Blend from Shape Operator
 * \{ */

/* BMESH_TODO this should be properly encapsulated in a bmop.  but later. */
static int edbm_blend_from_shape_exec(bContext *C, wmOperator *op)
{
  Object *obedit_ref = CTX_data_edit_object(C);
  Mesh *me_ref = obedit_ref->data;
  Key *key_ref = me_ref->key;
  KeyBlock *kb_ref = NULL;
  BMEditMesh *em_ref = me_ref->edit_mesh;
  BMVert *eve;
  BMIter iter;
  ViewLayer *view_layer = CTX_data_view_layer(C);
  float co[3], *sco;
  int totshape_ref = 0;

  const float blend = RNA_float_get(op->ptr, "blend");
  int shape_ref = RNA_enum_get(op->ptr, "shape");
  const bool use_add = RNA_boolean_get(op->ptr, "add");

  /* Sanity check. */
  totshape_ref = CustomData_number_of_layers(&em_ref->bm->vdata, CD_SHAPEKEY);

  if (totshape_ref == 0 || shape_ref < 0) {
    BKE_report(op->reports, RPT_ERROR, "Active mesh does not have shape keys");
    return OPERATOR_CANCELLED;
  }
  if (shape_ref >= totshape_ref) {
    /* This case occurs if operator was used before on object with more keys than current one. */
    shape_ref = 0; /* default to basis */
  }

  /* Get shape key - needed for finding reference shape (for add mode only). */
  if (key_ref) {
    kb_ref = BLI_findlink(&key_ref->block, shape_ref);
  }

  int tot_selected_verts_objects = 0;
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    Mesh *me = obedit->data;
    Key *key = me->key;
    KeyBlock *kb = NULL;
    BMEditMesh *em = me->edit_mesh;
    int shape;

    if (em->bm->totvertsel == 0) {
      continue;
    }
    tot_selected_verts_objects++;

    if (!key) {
      continue;
    }
    kb = BKE_keyblock_find_name(key, kb_ref->name);
    shape = BLI_findindex(&key->block, kb);

    if (kb) {
      /* Perform blending on selected vertices. */
      BM_ITER_MESH (eve, &iter, em->bm, BM_VERTS_OF_MESH) {
        if (!BM_elem_flag_test(eve, BM_ELEM_SELECT) || BM_elem_flag_test(eve, BM_ELEM_HIDDEN)) {
          continue;
        }

        /* Get coordinates of shapekey we're blending from. */
        sco = CustomData_bmesh_get_n(&em->bm->vdata, eve->head.data, CD_SHAPEKEY, shape);
        copy_v3_v3(co, sco);

        if (use_add) {
          /* In add mode, we add relative shape key offset. */
          const float *rco = CustomData_bmesh_get_n(
              &em->bm->vdata, eve->head.data, CD_SHAPEKEY, kb->relative);
          sub_v3_v3v3(co, co, rco);

          madd_v3_v3fl(eve->co, co, blend);
        }
        else {
          /* In blend mode, we interpolate to the shape key. */
          interp_v3_v3v3(eve->co, eve->co, co, blend);
        }
      }
      EDBM_update(me,
                  &(const struct EDBMUpdate_Params){
                      .calc_looptri = true,
                      .calc_normals = true,
                      .is_destructive = false,
                  });
    }
  }
  MEM_freeN(objects);

  if (tot_selected_verts_objects == 0) {
    BKE_report(op->reports, RPT_ERROR, "No selected vertex");
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

static const EnumPropertyItem *shape_itemf(bContext *C,
                                           PointerRNA *UNUSED(ptr),
                                           PropertyRNA *UNUSED(prop),
                                           bool *r_free)
{
  Object *obedit = CTX_data_edit_object(C);
  BMEditMesh *em;
  EnumPropertyItem *item = NULL;
  int totitem = 0;

  if ((obedit && obedit->type == OB_MESH) && (em = BKE_editmesh_from_object(obedit)) &&
      CustomData_has_layer(&em->bm->vdata, CD_SHAPEKEY)) {
    EnumPropertyItem tmp = {0, "", 0, "", ""};
    int a;

    for (a = 0; a < em->bm->vdata.totlayer; a++) {
      if (em->bm->vdata.layers[a].type != CD_SHAPEKEY) {
        continue;
      }

      tmp.value = totitem;
      tmp.identifier = em->bm->vdata.layers[a].name;
      tmp.name = em->bm->vdata.layers[a].name;
      /* RNA_enum_item_add sets totitem itself! */
      RNA_enum_item_add(&item, &totitem, &tmp);
    }
  }

  RNA_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static void edbm_blend_from_shape_ui(bContext *C, wmOperator *op)
{
  uiLayout *layout = op->layout;
  Object *obedit = CTX_data_edit_object(C);
  Mesh *me = obedit->data;
  PointerRNA ptr_key;

  RNA_id_pointer_create((ID *)me->key, &ptr_key);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  uiItemPointerR(layout, op->ptr, "shape", &ptr_key, "key_blocks", NULL, ICON_SHAPEKEY_DATA);
  uiItemR(layout, op->ptr, "blend", 0, NULL, ICON_NONE);
  uiItemR(layout, op->ptr, "add", 0, NULL, ICON_NONE);
}

void MESH_OT_blend_from_shape(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Blend from Shape";
  ot->description = "Blend in shape from a shape key";
  ot->idname = "MESH_OT_blend_from_shape";

  /* api callbacks */
  ot->exec = edbm_blend_from_shape_exec;
  /* disable because search popup closes too easily */
  //  ot->invoke = WM_operator_props_popup_call;
  ot->ui = edbm_blend_from_shape_ui;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  prop = RNA_def_enum(
      ot->srna, "shape", DummyRNA_NULL_items, 0, "Shape", "Shape key to use for blending");
  RNA_def_enum_funcs(prop, shape_itemf);
  RNA_def_property_flag(prop, PROP_ENUM_NO_TRANSLATE | PROP_NEVER_UNLINK);
  RNA_def_float(ot->srna, "blend", 1.0f, -1e3f, 1e3f, "Blend", "Blending factor", -2.0f, 2.0f);
  RNA_def_boolean(ot->srna, "add", true, "Add", "Add rather than blend between shapes");
}

/* -------------------------------------------------------------------- */
/** \name Solidify Mesh Operator
 * \{ */

static int edbm_solidify_exec(bContext *C, wmOperator *op)
{
  const float thickness = RNA_float_get(op->ptr, "thickness");

  ViewLayer *view_layer = CTX_data_view_layer(C);
  uint objects_len = 0;
  Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data(
      view_layer, CTX_wm_view3d(C), &objects_len);
  for (uint ob_index = 0; ob_index < objects_len; ob_index++) {
    Object *obedit = objects[ob_index];
    BMEditMesh *em = BKE_editmesh_from_object(obedit);
    BMesh *bm = em->bm;

    if (em->bm->totfacesel == 0) {
      continue;
    }

    BMOperator bmop;

    if (!EDBM_op_init(
            em, &bmop, op, "solidify geom=%hf thickness=%f", BM_ELEM_SELECT, thickness)) {
      continue;
    }

    /* deselect only the faces in the region to be solidified (leave wire
     * edges and loose verts selected, as there will be no corresponding
     * geometry selected below) */
    BMO_slot_buffer_hflag_disable(bm, bmop.slots_in, "geom", BM_FACE, BM_ELEM_SELECT, true);

    /* run the solidify operator */
    BMO_op_exec(bm, &bmop);

    /* select the newly generated faces */
    BMO_slot_buffer_hflag_enable(bm, bmop.slots_out, "geom.out", BM_FACE, BM_ELEM_SELECT, true);

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

void MESH_OT_solidify(wmOperatorType *ot)
{
  PropertyRNA *prop;
  /* identifiers */
  ot->name = "Solidify";
  ot->description = "Create a solid skin by extruding, compensating for sharp angles";
  ot->idname = "MESH_OT_solidify";

  /* api callbacks */
  ot->exec = edbm_solidify_exec;
  ot->poll = ED_operator_editmesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  prop = RNA_def_float_distance(
      ot->srna, "thickness", 0.01f, -1e4f, 1e4f, "Thickness", "", -10.0f, 10.0f);
  RNA_def_property_ui_range(prop, -10.0, 10.0, 0.1, 4);
}

/* -------------------------------------------------------------------- */
/** \name Knife Subdivide Operator
 * \{ */

#define KNIFE_EXACT 1
#define KNIFE_MIDPOINT 2
#define KNIFE_MULTICUT 3

static const EnumPropertyItem knife_items[] = {
    {KNIFE_EXACT, "EXACT", 0, "Exact", ""},
    {KNIFE_MIDPOINT, "MIDPOINTS", 0, "Midpoints", ""},
    {KNIFE_MULTICUT, "MULTICUT", 0, "Multicut", ""},
    {0, NULL, 0, NULL, NULL},
};

/* bm_edge_seg_isect() Determines if and where a mouse trail intersects an BMEdge */

static float bm_edge_seg_isect(const float sco_a[2],
                               const float sco_b[2],
                               float (*mouse_path)[2],
                               int len,
                               char mode,
                               int *isected)
{
#define MAXSLOPE 100000
  float x11, y11, x12 = 0, y12 = 0, x2max, x2min, y2max;
  float y2min, dist, lastdist = 0, xdiff2, xdiff1;
  float m1, b1, m2, b2, x21, x22, y21, y22, xi;
  float yi, x1min, x1max, y1max, y1min, perc = 0;
  float threshold = 0.0;
  int i;

  // threshold = 0.000001; /* tolerance for vertex intersection */
  // XXX threshold = scene->toolsettings->select_thresh / 100;

  /* Get screen coords of verts */
  x21 = sco_a[0];
  y21 = sco_a[1];

  x22 = sco_b[0];
  y22 = sco_b[1];

  xdiff2 = (x22 - x21);
  if (xdiff2) {
    m2 = (y22 - y21) / xdiff2;
    b2 = ((x22 * y21) - (x21 * y22)) / xdiff2;
  }
  else {
    m2 = MAXSLOPE; /* Vertical slope. */
    b2 = x22;
  }

  *isected = 0;

  /* check for _exact_ vertex intersection first */
  if (mode != KNIFE_MULTICUT) {
    for (i = 0; i < len; i++) {
      if (i > 0) {
        x11 = x12;
        y11 = y12;
      }
      else {
        x11 = mouse_path[i][0];
        y11 = mouse_path[i][1];
      }
      x12 = mouse_path[i][0];
      y12 = mouse_path[i][1];

      /* test e->v1 */
      if ((x11 == x21 && y11 == y21) || (x12 == x21 && y12 == y21)) {
        perc = 0;
        *isected = 1;
        return perc;
      }
      /* test e->v2 */
      if ((x11 == x22 && y11 == y22) || (x12 == x22 && y12 == y22)) {
        perc = 0;
        *isected = 2;
        return perc;
      }
    }
  }

  /* now check for edge intersect (may produce vertex intersection as well) */
  for (i = 0; i < len; i++) {
    if (i > 0) {
      x11 = x12;
      y11 = y12;
    }
    else {
      x11 = mouse_path[i][0];
      y11 = mouse_path[i][1];
    }
    x12 = mouse_path[i][0];
    y12 = mouse_path[i][1];

    /* Calculate the distance from point to line. */
    if (m2 != MAXSLOPE) {
      /* `sqrt(m2 * m2 + 1);` Only looking for change in sign.  Skip extra math. */
      dist = (y12 - m2 * x12 - b2);
    }
    else {
      dist = x22 - x12;
    }

    if (i == 0) {
      lastdist = dist;
    }

    /* if dist changes sign, and intersect point in edge's Bound Box */
    if ((lastdist * dist) <= 0) {
      xdiff1 = (x12 - x11); /* Equation of line between last 2 points */
      if (xdiff1) {
        m1 = (y12 - y11) / xdiff1;
        b1 = ((x12 * y11) - (x11 * y12)) / xdiff1;
      }
      else {
        m1 = MAXSLOPE;
        b1 = x12;
      }
      x2max = max_ff(x21, x22) + 0.001f; /* Prevent missed edges. */
      x2min = min_ff(x21, x22) - 0.001f; /* Due to round off error. */
      y2max = max_ff(y21, y22) + 0.001f;
      y2min = min_ff(y21, y22) - 0.001f;

      /* Found an intersect,  calc intersect point */
      if (m1 == m2) { /* co-incident lines */
        /* cut at 50% of overlap area */
        x1max = max_ff(x11, x12);
        x1min = min_ff(x11, x12);
        xi = (min_ff(x2max, x1max) + max_ff(x2min, x1min)) / 2.0f;

        y1max = max_ff(y11, y12);
        y1min = min_ff(y11, y12);
        yi = (min_ff(y2max, y1max) + max_ff(y2min, y1min)) / 2.0f;
      }
      else if (m2 == MAXSLOPE) {
        xi = x22;
        yi = m1 * x22 + b1;
      }
      else if (m1 == MAXSLOPE) {
        xi = x12;
        yi = m2 * x12 + b2;
      }
      else {
        xi = (b1 - b2) / (m2 - m1);
        yi = (b1 * m2 - m1 * b2) / (m2 - m1);
      }

      /* Intersect inside bounding box of edge? */
      if ((xi >= x2min) && (xi <= x2max) && (yi <= y2max) && (yi >= y2min)) {
        /* Test for vertex intersect that may be 'close enough'. */
        if (mode != KNIFE_MULTICUT) {
          if (xi <= (x21 + threshold) && xi >= (x21 - threshold)) {
            if (yi <= (y21 + threshold) && yi >= (y21 - threshold)) {
              *isected = 1;
              perc = 0;
              break;
            }
          }
          if (xi <= (x22 + threshold) && xi >= (x22 - threshold)) {
            if (yi <= (y22 + threshold) && yi >= (y22 - threshold)) {
              *isected = 2;
              perc = 0;
              break;
            }
          }
        }
        if ((m2 <= 1.0f) && (m2 >= -1.0f)) {
          perc = (xi - x21) / (x22 - x21);
        }
        else {
          perc = (yi - y21) / (y22 - y21); /* lower slope more accurate */
        }
        // isect = 32768.0 * (perc + 0.0000153); /* Percentage in 1 / 32768ths */

        break;
      }
    }
    lastdist = dist;
  }
  return perc;
}

#define ELE_EDGE_CUT 1

static int edbm_knife_cut_exec(bContext *C, wmOperator *op)
{
  Object *obedit = CTX_data_edit_object(C);
  BMEditMesh *em = BKE_editmesh_from_object(obedit);
  BMesh *bm = em->bm;
  ARegion *region = CTX_wm_region(C);
  BMVert *bv;
  BMIter iter;
  BMEdge *be;
  BMOperator bmop;
  float isect = 0.0f;
  int isected, i;
  short numcuts = 1;
  const short mode = RNA_int_get(op->ptr, "type");

  /* Allocated variables. */
  float(*screen_vert_coords)[2], (*sco)[2], (*mouse_path)[2];

  /* edit-object needed for matrix, and region->regiondata for projections to work */
  if (ELEM(NULL, obedit, region, region->regiondata)) {
    return OPERATOR_CANCELLED;
  }

  if (bm->totvertsel < 2) {
    BKE_report(op->reports, RPT_ERROR, "No edges are selected to operate on");
    return OPERATOR_CANCELLED;
  }

  const int len = RNA_collection_length(op->ptr, "path");

  if (len < 2) {
    BKE_report(op->reports, RPT_ERROR, "Mouse path too short");
    return OPERATOR_CANCELLED;
  }

  mouse_path = MEM_mallocN(len * sizeof(*mouse_path), __func__);

  /* get the cut curve */
  RNA_BEGIN (op->ptr, itemptr, "path") {
    RNA_float_get_array(&itemptr, "loc", (float *)&mouse_path[len]);
  }
  RNA_END;

  /* for ED_view3d_project_float_object */
  ED_view3d_init_mats_rv3d(obedit, region->regiondata);

  /* TODO: investigate using index lookup for #screen_vert_coords() rather than a hash table. */

  /* the floating point coordinates of verts in screen space will be
   * stored in a hash table according to the vertices pointer */
  screen_vert_coords = sco = MEM_mallocN(sizeof(float[2]) * bm->totvert, __func__);

  BM_ITER_MESH_INDEX (bv, &iter, bm, BM_VERTS_OF_MESH, i) {
    if (ED_view3d_project_float_object(region, bv->co, *sco, V3D_PROJ_TEST_CLIP_NEAR) !=
        V3D_PROJ_RET_OK) {
      copy_v2_fl(*sco, FLT_MAX); /* set error value */
    }
    BM_elem_index_set(bv, i); /* set_inline */
    sco++;
  }
  bm->elem_index_dirty &= ~BM_VERT; /* clear dirty flag */

  if (!EDBM_op_init(em, &bmop, op, "subdivide_edges")) {
    MEM_freeN(mouse_path);
    MEM_freeN(screen_vert_coords);
    return OPERATOR_CANCELLED;
  }

  /* Store percentage of edge cut for KNIFE_EXACT here. */
  BMOpSlot *slot_edge_percents = BMO_slot_get(bmop.slots_in, "edge_percents");
  BM_ITER_MESH (be, &iter, bm, BM_EDGES_OF_MESH) {
    bool is_cut = false;
    if (BM_elem_flag_test(be, BM_ELEM_SELECT)) {
      const float *sco_a = screen_vert_coords[BM_elem_index_get(be->v1)];
      const float *sco_b = screen_vert_coords[BM_elem_index_get(be->v2)];

      /* check for error value (vert can't be projected) */
      if ((sco_a[0] != FLT_MAX) && (sco_b[0] != FLT_MAX)) {
        isect = bm_edge_seg_isect(sco_a, sco_b, mouse_path, len, mode, &isected);

        if (isect != 0.0f) {
          if (!ELEM(mode, KNIFE_MULTICUT, KNIFE_MIDPOINT)) {
            BMO_slot_map_float_insert(&bmop, slot_edge_percents, be, isect);
          }
        }
      }
    }

    BMO_edge_flag_set(bm, be, ELE_EDGE_CUT, is_cut);
  }

  /* free all allocs */
  MEM_freeN(screen_vert_coords);
  MEM_freeN(mouse_path);

  BM_custom_loop_normals_to_vector_layer(bm);

  BMO_slot_buffer_from_enabled_flag(bm, &bmop, bmop.slots_in, "edges", BM_EDGE, ELE_EDGE_CUT);

  if (mode == KNIFE_MIDPOINT) {
    numcuts = 1;
  }
  BMO_slot_int_set(bmop.slots_in, "cuts", numcuts);

  BMO_slot_int_set(bmop.slots_in, "quad_corner_type", SUBD_CORNER_STRAIGHT_CUT);
  BMO_slot_bool_set(bmop.slots_in, "use_single_edge", false);
  BMO_slot_bool_set(bmop.slots_in, "use_grid_fill", false);

  BMO_slot_float_set(bmop.slots_in, "radius", 0);

  BMO_op_exec(bm, &bmop);
  if (!EDBM_op_finish(em, &bmop, op, true)) {
    return OPERATOR_CANCELLED;
  }

  BM_custom_loop_normals_from_vector_layer(bm, false);

  EDBM_update(obedit->data,
              &(const struct EDBMUpdate_Params){
                  .calc_looptri = true,
                  .calc_normals = false,
                  .is_destructive = true,
              });

  return OPERATOR_FINISHED;
}

#undef ELE_EDGE_CUT

void MESH_OT_knife_cut(wmOperatorType *ot)
{
  ot->name = "Knife Cut";
  ot->description = "Cut selected edges and faces into parts";
  ot->idname = "MESH_OT_knife_cut";

  ot->invoke = WM_gesture_lines_invoke;
  ot->modal = WM_gesture_lines_modal;
  ot->exec = edbm_knife_cut_exec;

  ot->poll = EDBM_view3d_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_DEPENDS_ON_CURSOR;

  /* properties */
  PropertyRNA *prop;
  prop = RNA_def_collection_runtime(ot->srna, "path", &RNA_OperatorMousePath, "Path", "");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  RNA_def_enum(ot->srna, "type", knife_items, KNIFE_EXACT, "Type", "");

  /* internal */
  RNA_def_int(
      ot->srna, "cursor", WM_CURSOR_KNIFE, 0, WM_CURSOR_NUM, "Cursor", "", 0, WM_CURSOR_NUM);
}

/* -------------------------------------------------------------------- */
/** \name Separate Parts Operator
 * \{ */

enum {
  MESH_SEPARATE_SELECTED = 0,
  MESH_SEPARATE_MATERIAL = 1,
  MESH_SEPARATE_LOOSE = 2,
};

/** TODO: Use #mesh_separate_arrays since it's more efficient. */
static Base *mesh_separate_tagged(
    Main *bmain, Scene *scene, ViewLayer *view_layer, Base *base_old, BMesh *bm_old)
{
  Object *obedit = base_old->object;
  BMesh *bm_new = BM_mesh_create(&bm_mesh_allocsize_default,
                                 &((struct BMeshCreateParams){
                                     .use_toolflags = true,
                                 }));
  BM_mesh_elem_toolflags_ensure(bm_new); /* needed for 'duplicate' bmo */

  BM_mesh_copy_init_customdata(bm_new, bm_old, &bm_mesh_allocsize_default);

  /* Take into account user preferences for duplicating actions. */
  const eDupli_ID_Flags dupflag = USER_DUP_MESH | (U.dupflag & USER_DUP_ACT);
  Base *base_new = ED_object_add_duplicate(bmain, scene, view_layer, base_old, dupflag);

  /* normally would call directly after but in this case delay recalc */
  // DAG_relations_tag_update(bmain);

  /* new in 2.5 */
  BKE_object_material_array_assign(bmain,
                                   base_new->object,
                                   BKE_object_material_array_p(obedit),
                                   *BKE_object_material_len_p(obedit),
                                   false);

  ED_object_base_select(base_new, BA_SELECT);

  BMO_op_callf(bm_old,
               (BMO_FLAG_DEFAULTS & ~BMO_FLAG_RESPECT_HIDE),
               "duplicate geom=%hvef dest=%p",
               BM_ELEM_TAG,
               bm_new);
  BMO_op_callf(bm_old,
               (BMO_FLAG_DEFAULTS & ~BMO_FLAG_RESPECT_HIDE),
               "delete geom=%hvef context=%i",
               BM_ELEM_TAG,
               DEL_FACES);

  /* deselect loose data - this used to get deleted,
   * we could de-select edges and verts only, but this turns out to be less complicated
   * since de-selecting all skips selection flushing logic */
  BM_mesh_elem_hflag_disable_all(bm_old, BM_VERT | BM_EDGE | BM_FACE, BM_ELEM_SELECT, false);

  BM_mesh_normals_update(bm_new);

  BM_mesh_bm_to_me(bmain, bm_new, base_new->object->data, (&(struct BMeshToMeshParams){0}));

  BM_mesh_free(bm_new);
  ((Mesh *)base_new->object->data)->edit_mesh = NULL;

  return base_new;
}

static Base *mesh_separate_arrays(Main *bmain,
                                  Scene *scene,
                                  ViewLayer *view_layer,
                                  Base *base_old,
                                  BMesh *bm_old,
                                  BMVert **verts,
                                  uint verts_len,
                                  BMEdge **edges,
                                  uint edges_len,
                                  BMFace **faces,
                                  uint faces_len)
{
  const BMAllocTemplate bm_new_allocsize = {
      .totvert = verts_len,
      .totedge = edges_len,
      .totloop = faces_len * 3,
      .totface = faces_len,
  };
  const bool use_custom_normals = (bm_old->lnor_spacearr != NULL);

  Object *obedit = base_old->object;

  BMesh *bm_new = BM_mesh_create(&bm_new_allocsize, &((struct BMeshCreateParams){0}));

  if (use_custom_normals) {
    /* Needed so the temporary normal layer is copied too. */
    BM_mesh_copy_init_customdata_all_layers(bm_new, bm_old, BM_ALL, &bm_new_allocsize);
  }
  else {
    BM_mesh_copy_init_customdata(bm_new, bm_old, &bm_new_allocsize);
  }

  /* Take into account user preferences for duplicating actions. */
  const eDupli_ID_Flags dupflag = USER_DUP_MESH | (U.dupflag & USER_DUP_ACT);
  Base *base_new = ED_object_add_duplicate(bmain, scene, view_layer, base_old, dupflag);

  /* normally would call directly after but in this case delay recalc */
  // DAG_relations_tag_update(bmain);

  /* new in 2.5 */
  BKE_object_material_array_assign(bmain,
                                   base_new->object,
                                   BKE_object_material_array_p(obedit),
                                   *BKE_object_material_len_p(obedit),
                                   false);

  ED_object_base_select(base_new, BA_SELECT);

  BM_mesh_copy_arrays(bm_old, bm_new, verts, verts_len, edges, edges_len, faces, faces_len);

  if (use_custom_normals) {
    BM_custom_loop_normals_from_vector_layer(bm_new, false);
  }

  for (uint i = 0; i < verts_len; i++) {
    BM_vert_kill(bm_old, verts[i]);
  }

  BM_mesh_bm_to_me(bmain, bm_new, base_new->object->data, (&(struct BMeshToMeshParams){0}));

  BM_mesh_free(bm_new);
  ((Mesh *)base_new->object->data)->edit_mesh = NULL;

  return base_new;
}

static bool mesh_separate_selected(
    Main *bmain, Scene *scene, ViewLayer *view_layer, Base *base_old, BMesh *bm_old)
{
  /* we may have tags from previous operators */
  BM_mesh_elem_hflag_disable_all(bm_old, BM_FACE | BM_EDGE | BM_VERT, BM_ELEM_TAG, false);

  /* sel -> tag */
  BM_mesh_elem_hflag_enable_test(
      bm_old, BM_FACE | BM_EDGE | BM_VERT, BM_ELEM_TAG, true, false, BM_ELEM_SELECT);

  return (mesh_separate_tagged(bmain, scene, view_layer, base_old, bm_old) != NULL);
}

/**
 * Sets an object to a single material. from one of its slots.
 *
 * \note This could be used for split-by-material for non mesh types.
 * \note This could take material data from another object or args.
 */
static void mesh_separate_material_assign_mat_nr(Main *bmain, Object *ob, const short mat_nr)
{
  ID *obdata = ob->data;

  const short *totcolp = BKE_id_material_len_p(obdata);
  Material ***matarar = BKE_id_material_array_p(obdata);

  if ((totcolp && matarar) == 0) {
    BLI_assert(0);
    return;
  }

  if (*totcolp) {
    Material *ma_ob;
    Material *ma_obdata;
    char matbit;

    if (mat_nr < ob->totcol) {
      ma_ob = ob->mat[mat_nr];
      matbit = ob->matbits[mat_nr];
    }
    else {
      ma_ob = NULL;
      matbit = 0;
    }

    if (mat_nr < *totcolp) {
      ma_obdata = (*matarar)[mat_nr];
    }
    else {
      ma_obdata = NULL;
    }

    BKE_id_material_clear(bmain, obdata);
    BKE_object_material_resize(bmain, ob, 1, true);
    BKE_id_material_resize(bmain, obdata, 1, true);

    ob->mat[0] = ma_ob;
    id_us_plus((ID *)ma_ob);
    ob->matbits[0] = matbit;
    (*matarar)[0] = ma_obdata;
    id_us_plus((ID *)ma_obdata);
  }
  else {
    BKE_id_material_clear(bmain, obdata);
    BKE_object_material_resize(bmain, ob, 0, true);
    BKE_id_material_resize(bmain, obdata, 0, true);
  }
}

static bool mesh_separate_material(
    Main *bmain, Scene *scene, ViewLayer *view_layer, Base *base_old, BMesh *bm_old)
{
  BMFace *f_cmp, *f;
  BMIter iter;
  bool result = false;

  while ((f_cmp = BM_iter_at_index(bm_old, BM_FACES_OF_MESH, NULL, 0))) {
    Base *base_new;
    const short mat_nr = f_cmp->mat_nr;
    int tot = 0;

    BM_mesh_elem_hflag_disable_all(bm_old, BM_VERT | BM_EDGE | BM_FACE, BM_ELEM_TAG, false);

    BM_ITER_MESH (f, &iter, bm_old, BM_FACES_OF_MESH) {
      if (f->mat_nr == mat_nr) {
        BMLoop *l_iter;
        BMLoop *l_first;

        BM_elem_flag_enable(f, BM_ELEM_TAG);
        l_iter = l_first = BM_FACE_FIRST_LOOP(f);
        do {
          BM_elem_flag_enable(l_iter->v, BM_ELEM_TAG);
          BM_elem_flag_enable(l_iter->e, BM_ELEM_TAG);
        } while ((l_iter = l_iter->next) != l_first);

        tot++;
      }
    }

    /* leave the current object with some materials */
    if (tot == bm_old->totface) {
      mesh_separate_material_assign_mat_nr(bmain, base_old->object, mat_nr);

      /* since we're in editmode, must set faces here */
      BM_ITER_MESH (f, &iter, bm_old, BM_FACES_OF_MESH) {
        f->mat_nr = 0;
      }
      break;
    }

    /* Move selection into a separate object */
    base_new = mesh_separate_tagged(bmain, scene, view_layer, base_old, bm_old);
    if (base_new) {
      mesh_separate_material_assign_mat_nr(bmain, base_new->object, mat_nr);
    }

    result |= (base_new != NULL);
  }

  return result;
}

static bool mesh_separate_loose(
    Main *bmain, Scene *scene, ViewLayer *view_layer, Base *base_old, BMesh *bm_old)
{
  /* Without this, we duplicate the object mode mesh for each loose part.
   * This can get very slow especially for large meshes with many parts
   * which would duplicate the mesh on entering edit-mode. */
  const bool clear_object_data = true;

  bool result = false;

  BMVert **vert_groups = MEM_mallocN(sizeof(*vert_groups) * bm_old->totvert, __func__);
  BMEdge **edge_groups = MEM_mallocN(sizeof(*edge_groups) * bm_old->totedge, __func__);
  BMFace **face_groups = MEM_mallocN(sizeof(*face_groups) * bm_old->totface, __func__);

  int(*groups)[3] = NULL;
  int groups_len = BM_mesh_calc_edge_groups_as_arrays(
      bm_old, vert_groups, edge_groups, face_groups, &groups);
  if (groups_len <= 1) {
    goto finally;
  }

  if (clear_object_data) {
    ED_mesh_geometry_clear(base_old->object->data);
  }

  BM_custom_loop_normals_to_vector_layer(bm_old);

  /* Separate out all groups except the first. */
  uint group_ofs[3] = {UNPACK3(groups[0])};
  for (int i = 1; i < groups_len; i++) {
    Base *base_new = mesh_separate_arrays(bmain,
                                          scene,
                                          view_layer,
                                          base_old,
                                          bm_old,
                                          vert_groups + group_ofs[0],
                                          groups[i][0],
                                          edge_groups + group_ofs[1],
                                          groups[i][1],
                                          face_groups + group_ofs[2],
                                          groups[i][2]);
    result |= (base_new != NULL);

    group_ofs[0] += groups[i][0];
    group_ofs[1] += groups[i][1];
    group_ofs[2] += groups[i][2];
  }

  Mesh *me_old = base_old->object->data;
  BM_mesh_elem_hflag_disable_all(bm_old, BM_VERT | BM_EDGE | BM_FACE, BM_ELEM_SELECT, false);

  if (clear_object_data) {
    BM_mesh_bm_to_me(NULL,
                     bm_old,
                     me_old,
                     (&(struct BMeshToMeshParams){
                         .update_shapekey_indices = true,
                     }));
  }

finally:
  MEM_freeN(vert_groups);
  MEM_freeN(edge_groups);
  MEM_freeN(face_groups);

  MEM_freeN(groups);

  return result;
}

static int edbm_separate_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  const int type = RNA_enum_get(op->ptr, "type");
  bool changed_multi = false;

  if (ED_operator_editmesh(C)) {
    uint bases_len = 0;
    uint empty_selection_len = 0;
    Base **bases = BKE_view_layer_array_from_bases_in_edit_mode_unique_data(
        view_layer, CTX_wm_view3d(C), &bases_len);
    for (uint bs_index = 0; bs_index < bases_len; bs_index++) {
      Base *base = bases[bs_index];
      BMEditMesh *em = BKE_editmesh_from_object(base->object);
      bool changed = false;

      if (type == 0) {
        if ((em->bm->totvertsel == 0) && (em->bm->totedgesel == 0) && (em->bm->totfacesel == 0)) {
          /* when all objects has no selection */
          if (++empty_selection_len == bases_len) {
            BKE_report(op->reports, RPT_ERROR, "Nothing selected");
          }
          continue;
        }
      }

      /* editmode separate */
      switch (type) {
        case MESH_SEPARATE_SELECTED:
          changed = mesh_separate_selected(bmain, scene, view_layer, base, em->bm);
          break;
        case MESH_SEPARATE_MATERIAL:
          changed = mesh_separate_material(bmain, scene, view_layer, base, em->bm);
          break;
        case MESH_SEPARATE_LOOSE:
          changed = mesh_separate_loose(bmain, scene, view_layer, base, em->bm);
          break;
        default:
          BLI_assert(0);
          break;
      }

      if (changed) {
        EDBM_update(base->object->data,
                    &(const struct EDBMUpdate_Params){
                        .calc_looptri = true,
                        .calc_normals = false,
                        .is_destructive = true,
                    });
      }
      changed_multi |= changed;
    }
    MEM_freeN(bases);
  }
  else {
    if (type == MESH_SEPARATE_SELECTED) {
      BKE_report(op->reports, RPT_ERROR, "Selection not supported in object mode");
      return OPERATOR_CANCELLED;
    }

    /* object mode separate */
    CTX_DATA_BEGIN (C, Base *, base_iter, selected_editable_bases) {
      Object *ob = base_iter->object;
      if (ob->type == OB_MESH) {
        Mesh *me = ob->data;
        if (!ID_IS_LINKED(me)) {
          BMesh *bm_old = NULL;
          bool changed = false;

          bm_old = BM_mesh_create(&bm_mesh_allocsize_default,
                                  &((struct BMeshCreateParams){
                                      .use_toolflags = true,
                                  }));

          BM_mesh_bm_from_me(bm_old, me, (&(struct BMeshFromMeshParams){0}));

          switch (type) {
            case MESH_SEPARATE_MATERIAL:
              changed = mesh_separate_material(bmain, scene, view_layer, base_iter, bm_old);
              break;
            case MESH_SEPARATE_LOOSE:
              changed = mesh_separate_loose(bmain, scene, view_layer, base_iter, bm_old);
              break;
            default:
              BLI_assert(0);
              break;
          }

          if (changed) {
            BM_mesh_bm_to_me(bmain,
                             bm_old,
                             me,
                             (&(struct BMeshToMeshParams){
                                 .calc_object_remap = true,
                             }));

            DEG_id_tag_update(&me->id, ID_RECALC_GEOMETRY_ALL_MODES);
            WM_event_add_notifier(C, NC_GEOM | ND_DATA, me);
          }

          BM_mesh_free(bm_old);

          changed_multi |= changed;
        }
      }
    }
    CTX_DATA_END;
  }

  if (changed_multi) {
    /* delay depsgraph recalc until all objects are duplicated */
    DEG_relations_tag_update(bmain);
    WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, NULL);
    ED_outliner_select_sync_from_object_tag(C);

    return OPERATOR_FINISHED;
  }

  return OPERATOR_CANCELLED;
}

void MESH_OT_separate(wmOperatorType *ot)
{
  static const EnumPropertyItem prop_separate_types[] = {
      {MESH_SEPARATE_SELECTED, "SELECTED", 0, "Selection", ""},
      {MESH_SEPARATE_MATERIAL, "MATERIAL", 0, "By Material", ""},
      {MESH_SEPARATE_LOOSE, "LOOSE", 0, "By Loose Parts", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Separate";
  ot->description = "Separate selected geometry into a new mesh";
  ot->idname = "MESH_OT_separate";

  /* api callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = edbm_separate_exec;
  ot->poll = ED_operator_scene_editable; /* object and editmode */

  /* flags */
  ot->flag = OPTYPE_UNDO;

  ot->prop = RNA_def_enum(
      ot->srna, "type", prop_separate_types, MESH_SEPARATE_SELECTED, "Type", "");
}


