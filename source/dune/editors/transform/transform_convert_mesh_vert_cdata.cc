#include "lib_math_matrix.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_meshedit.hh"

#include "graph.hh"

#include "transform.hh"
#include "transform_orientations.hh"

#include "transform_convert.hh"

/* MeshEdit Bevel Weight and Crease Transform Creation */
static float *mesh_cdata_transdata_center(const TransIslandData *island_data,
                                          const int island_index,
                                          MeshVert *eve)
{
  if (island_data->center && island_index != -1) {
    return island_data->center[island_index];
  }
  return eve->co;
}

static void mesh_cdata_transdata_create(TransDataBasic *td,
                                        MeshVert *mvert,
                                        float *weight,
                                        const TransIslandData *island_data,
                                        const int island_index)
{
  lib_assert(mesh_elem_flag_test(eve, MESH_ELEM_HIDDEN) == 0);

  td->val = weight;
  td->ival = *weight;

  if (mesh_elem_flag_test(eve, MESH_ELEM_SEL)) {
    td->flag |= TD_SELECTED;
  }

  copy_v3_v3(td->center, mesh_cdata_transdata_center(island_data, island_index, eve));
  td->extra = eve;
}

static void createTransMeshVertCData(Cxt * /*C*/, TransInfo *t)
{
  lib_assert(ELEM(t->mode, TFM_BWEIGHT, TFM_VERT_CREASE));
  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    MeshEdit *me = dune_meshedit_from_ob(tc->obedit);
    Mesh *mesh = me->mesh;
    MeshVert *eve;
    MeshIter iter;
    float mtx[3][3], smtx[3][3];
    int a;
    const int prop_mode = (t->flag & T_PROP_EDIT) ? (t->flag & T_PROP_EDIT_ALL) : 0;

    TransIslandData island_data = {nullptr};
    TransMeshDataCrazySpace crazyspace_data = {nullptr};

    /* Support other obs using proportional editing to adjust these, unless connected is
     * enabled. */
    if ((!prop_mode || (prop_mode & T_PROP_CONNECTED)) && (mesh->totvertsel == 0)) {
      continue;
    }

    int cd_offset = -1;
    if (t->mode == TFM_BWEIGHT) {
      if (!CustomData_has_layer_named(&mesh->vdata, CD_PROP_FLOAT, "bevel_weight_vert")) {
        mesh_data_layer_add_named(mesh, &mesh->vdata, CD_PROP_FLOAT, "bevel_weight_vert");
      }
      cd_offset = CustomData_get_offset_named(&bm->vdata, CD_PROP_FLOAT, "bevel_weight_vert");
    }
    else {
      if (!CustomData_has_layer_named(&mesh->vdata, CD_PROP_FLOAT, "crease_vert")) {
        mesh_data_layer_add_named(mesh, &mesh->vdata, CD_PROP_FLOAT, "crease_vert");
      }
      cd_offset = CustomData_get_offset_named(&mesh->vdata, CD_PROP_FLOAT, "crease_vert");
    }

    if (cd_offset == -1) {
      continue;
    }

    int data_len = 0;
    if (prop_mode) {
      MESH_ITER (eve, &iter, mesh, MESH_VERTS) {
        if (!mesh_elem_flag_test(eve, MESH_ELEM_HIDDEN)) {
          data_len++;
        }
      }
    }
    else {
      data_len = mesh->totvertsel;
    }

    if (data_len == 0) {
      continue;
    }

    const bool is_island_center = (t->around == V3D_AROUND_LOCAL_ORIGINS);
    if (is_island_center) {
      /* In this specific case, near-by verts will need to know
       * the island of the nearest connected vert. */
      const bool calc_single_islands = ((prop_mode & T_PROP_CONNECTED) &&
                                        (t->around == V3D_AROUND_LOCAL_ORIGINS) &&
                                        (me->selmode & SCE_SEL_VERT));

      const bool calc_island_center = false;
      const bool calc_island_axismtx = false;

      transform_convert_mesh_islands_calc(
          me, calc_single_islands, calc_island_center, calc_island_axismtx, &island_data);
    }

    copy_m3_m4(mtx, tc->obedit->ob_to_world);
    /* we use a pseudo-inverse so that when one of the axes is scaled to 0,
     * matrix inversion still works and we can still moving along the other */
    pseudoinverse_m3_m3(smtx, mtx, PSEUDOINVERSE_EPSILON);

    /* Original index of our connected vert when connected distances are calcd.
     * Optional, alloc if needed. */
    int *dists_index = nullptr;
    float *dists = nullptr;
    if (prop_mode & T_PROP_CONNECTED) {
      dists = static_cast<float *>(mem_malloc(mesh->totvert * sizeof(float), __func__));
      if (is_island_center) {
        dists_index = static_cast<int *>(mem_malloc(mesh->totvert * sizeof(int), __func__));
      }
      transform_convert_mesh_connectivity_distance(mesh->mesh, mtx, dists, dists_index);
    }

    /* Detect CrazySpace [tm]. */
    transform_convert_mesh_crazyspace_detect(t, tc, me, &crazyspace_data);

    /* Create TransData. */
    lib_assert(data_len >= 1);
    tc->data_len = data_len;
    tc->data = static_cast<TransData *>(
        mem_calloc(data_len * sizeof(TransData), "TransObData(Mesh EditMode)"));

    TransData *td = tc->data;
    MESH_ITER_INDEX (eve, &iter, mesh, MESHA_VERTS_OF_MESH, a) {
      if (mesh_elem_flag_test(eve, MESH_ELEM_HIDDEN)) {
        continue;
      }

      int island_index = -1;
      if (island_data.island_vert_map) {
        const int connected_index = (dists_index && dists_index[a] != -1) ? dists_index[a] : a;
        island_index = island_data.island_vert_map[connected_index];
      }

      float *weight = static_cast<float *>(MESH_ELEM_CD_GET_VOID_P(eve, cd_offset));
      if (prop_mode || mesh_elem_flag_test(eve, MESH_ELEM_SEL)) {
        mesh_cdata_transdata_create((TransDataBasic *)td, eve, weight, &island_data, island_index);

        if (t->around == V3D_AROUND_LOCAL_ORIGINS) {
          createSpaceNormal(td->axismtx, eve->no);
        }
        else {
          /* Setting normals */
          copy_v3_v3(td->axismtx[2], eve->no);
          td->axismtx[0][0] = td->axismtx[0][1] = td->axismtx[0][2] = td->axismtx[1][0] =
              td->axismtx[1][1] = td->axismtx[1][2] = 0.0f;
        }

        if (prop_mode) {
          if (prop_mode & T_PROP_CONNECTED) {
            td->dist = dists[a];
          }
          else {
            td->dist = FLT_MAX;
          }
        }

        /* CrazySpace */
        transform_convert_mesh_crazyspace_transdata_set(
            mtx,
            smtx,
            !crazyspace_data.defmats.is_empty() ? crazyspace_data.defmats[a].ptr() : nullptr,
            crazyspace_data.quats && mesh_elem_flag_test(eve, MESH_ELEM_TAG) ?
                crazyspace_data.quats[a] :
                nullptr,
            td);

        td++;
      }
    }

    transform_convert_mesh_islanddata_free(&island_data);
    transform_convert_mesh_crazyspace_free(&crazyspace_data);
    if (dists) {
      mem_free(dists);
    }
    if (dists_index) {
      mem_free(dists_index);
    }
  }
}

/* Recalc Mesh Data */
static void recalcData_mesh_cdata(TransInfo *t)
{
  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    graph_id_tag_update(static_cast<Id *>(tc->obedit->data), ID_RECALC_GEOMETRY);
  }
}

TransConvertTypeInfo TransConvertTypeMeshVertCData = {
    /*flags*/ (T_EDIT | T_POINTS),
    /*create_trans_data*/ createTransMeshVertCData,
    /*recalc_data*/ recalcData_mesh_cdata,
    /*special_aftertrans_update*/ nullptr,
};
