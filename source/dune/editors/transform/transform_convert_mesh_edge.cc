#include "types_mesh.h"

#include "mem_guardedalloc.h"

#include "lib_math_matrix.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_customdata.hh"
#include "dune_meshedit.hh"
#include "dune_mesh.hh"

#include "transform.hh"
#include "transform_convert.hh"

/* Edge (for crease) Transform Creation */
static void createTransEdge(Cxt * /*C*/, TransInfo *t)
{
  FOREACH_TRANS_DATA_CONTAINER (t, tc) {

    MeshEdit *em = dune_meshedit_from_ob(tc->obedit);
    TransData *td = nullptr;
    MeshEdge *eed;
    MeshIter iter;
    float mtx[3][3], smtx[3][3];
    int count = 0, countsel = 0;
    const bool is_prop_edit = (t->flag & T_PROP_EDIT) != 0;
    const bool is_prop_connected = (t->flag & T_PROP_CONNECTED) != 0;
    int cd_edge_float_offset;

    MESH_ITER (eed, &iter, me->mesh, MESH_EDGES_OF_MESH) {
      if (!mesh_elem_flag_test(eed, MESH_ELEM_HIDDEN)) {
        if (mesh_elem_flag_test(eed, MESH_ELEM_SEL)) {
          countsel++;
        }
        if (is_prop_edit) {
          count++;
        }
      }
    }

    if (((is_prop_edit && !is_prop_connected) ? count : countsel) == 0) {
      tc->data_len = 0;
      continue;
    }

    if (is_prop_edit) {
      tc->data_len = count;
    }
    else {
      tc->data_len = countsel;
    }

    td = tc->data = static_cast<TransData *>(
        mem_calloc(tc->data_len * sizeof(TransData), "TransCrease"));

    copy_m3_m4(mtx, tc->obedit->ob_to_world);
    pseudoinverse_m3_m3(smtx, mtx, PSEUDOINVERSE_EPSILON);

    /* create data we need */
    if (t->mode == TFM_BWEIGHT) {
      if (!CustomData_has_layer_named(&me->mesh->edata, CD_PROP_FLOAT, "bevel_weight_edge")) {
        mesh_data_layer_add_named(me->mesh, &me->mesh->edata, CD_PROP_FLOAT, "bevel_weight_edge");
      }
      cd_edge_float_offset = CustomData_get_offset_named(
          &me->mesh->edata, CD_PROP_FLOAT, "bevel_weight_edge");
    }
    else { /* if (t->mode == TFM_EDGE_CREASE) { */
      lib_assert(t->mode == TFM_EDGE_CREASE);
      if (!CustomData_has_layer_named(&em->mesh->edata, CD_PROP_FLOAT, "crease_edge")) {
        mesh_data_layer_add_named(em-mesh, &em->mesh->edata, CD_PROP_FLOAT, "crease_edge");
      }
      cd_edge_float_offset = CustomData_get_offset_named(
          &me->mesh->edata, CD_PROP_FLOAT, "crease_edge");
    }

    lib_assert(cd_edge_float_offset != -1);

    MESH_ITER (eed, &iter, em->mesh, MESH_EDGES_OF_MESH) {
      if (!mesh_elem_flag_test(eed, MESH_ELEM_HIDDEN) &&
          (mesh_elem_flag_test(eed, MESH_ELEM_SEL) || is_prop_edit))
      {
        float *fl_ptr;
        /* need to set center for center calculations */
        mid_v3_v3v3(td->center, eed->v1->co, eed->v2->co);

        td->loc = nullptr;
        if (mesh_elem_flag_test(eed, MESH_ELEM_SEL)) {
          td->flag = TD_SELECTED;
        }
        else {
          td->flag = 0;
        }

        copy_m3_m3(td->smtx, smtx);
        copy_m3_m3(td->mtx, mtx);

        td->ext = nullptr;

        fl_ptr = static_cast<float *>(BM_ELEM_CD_GET_VOID_P(eed, cd_edge_float_offset));
        td->val = fl_ptr;
        td->ival = *fl_ptr;

        td++;
      }
    }
  }
}

static void recalcData_mesh_edge(TransInfo *t)
{
  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    graph_id_tag_update(static_cast<Id *>(tc->obedit->data), ID_RECALC_GEOMETRY);
  }
}

TransConvertTypeInfo TransConvertType_MeshEdge = {
    /*flags*/ T_EDIT,
    /*create_trans_data*/ createTransEdge,
    /*recalc_data*/ recalcData_mesh_edge,
    /*special_aftertrans_update*/ nullptr,
};
