#include "types_ob.h"
#include "types_scene.h"
#include "types_view3d.h"

#include "dune_cxt.hh"

#include "lib_math_matrix.h"
#include "lib_math_rotation.h"
#include "lib_math_vector.h"

#include "api_access.hh"
#include "api_define.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ed_transverts.hh"

#include "ob_intern.h"

static void ob_warp_calc_view_matrix(float r_mat_view[4][4],
                                     float r_center_view[3],
                                     Ob *obedit,
                                     const float viewmat[4][4],
                                     const float center[3],
                                     const float offset_angle)
{
  float mat_offset[4][4];
  float viewmat_roll[4][4];

  /* apply the rotation offset by rolling the view */
  axis_angle_to_mat4_single(mat_offset, 'Z', offset_angle);
  mul_m4_m4m4(viewmat_roll, mat_offset, viewmat);

  /* apply the view and the object matrix */
  mul_m4_m4m4(r_mat_view, viewmat_roll, obedit->object_to_world);

  /* get the view-space cursor */
  mul_v3_m4v3(r_center_view, viewmat_roll, center);
}

static void ob_warp_transverts_minmax_x(TransVertStore *tvs,
                                            const float mat_view[4][4],
                                            const float center_view[3],
                                            float *r_min,
                                            float *r_max)
{
  /* no need to apply translation and cursor offset for every vertex, delay this */
  const float x_ofs = (mat_view[3][0] - center_view[0]);
  float min = FLT_MAX, max = -FLT_MAX;

  TransVert *tv = tvs->transverts;
  for (int i = 0; i < tvs->transverts_tot; i++, tv++) {
    float val;

    /* Convert ob-space to view-space. */
    val = dot_m4_v3_row_x(mat_view, tv->loc);

    min = min_ff(min, val);
    max = max_ff(max, val);
  }

  *r_min = min + x_ofs;
  *r_max = max + x_ofs;
}

static void ob_warp_transverts(TransVertStore *tvs,
                                   const float mat_view[4][4],
                                   const float center_view[3],
                                   const float angle_,
                                   const float min,
                                   const float max)
{
  TransVert *tv;
  const float angle = -angle_;
/* cache vars for tiny speedup */
#if 1
  const float range = max - min;
  const float range_inv = 1.0f / range;
  const float min_ofs = min + (0.5f * range);
#endif

  float dir_min[2], dir_max[2];
  float imat_view[4][4];

  invert_m4_m4(imat_view, mat_view);

  /* calc the direction vectors outside min/max range */
  {
    const float phi = angle * 0.5f;

    dir_max[0] = cosf(phi);
    dir_max[1] = sinf(phi);

    dir_min[0] = -dir_max[0];
    dir_min[1] = dir_max[1];
  }

  tv = tvs->transverts;
  for (int i = 0; i < tvs->transverts_tot; i++, tv++) {
    float co[3], co_add[2];
    float val, phi;

    /* Convert ob-space to view-space. */
    mul_v3_m4v3(co, mat_view, tv->loc);
    sub_v2_v2(co, center_view);

    val = co[0];
    /* is overwritten later anyway */
    // co[0] = 0.0f;

    if (val < min) {
      mul_v2_v2fl(co_add, dir_min, min - val);
      val = min;
    }
    else if (val > max) {
      mul_v2_v2fl(co_add, dir_max, val - max);
      val = max;
    }
    else {
      zero_v2(co_add);
    }

/* map from x axis to (-0.5 - 0.5) */
#if 0
    val = ((val - min) / (max - min)) - 0.5f;
#else
    val = (val - min_ofs) * range_inv;
#endif

    /* convert the x axis into a rotation */
    phi = val * angle;

    co[0] = -sinf(phi) * co[1];
    co[1] = cosf(phi) * co[1];

    add_v2_v2(co, co_add);

    /* Convert view-space to ob-space. */
    add_v2_v2(co, center_view);
    mul_v3_m4v3(tv->loc, imat_view, co);
  }
}

static int ob_warp_verts_ex(Cxt *C, WinOp *op)
{
  const float warp_angle = api_float_get(op->ptr, "warp_angle");
  const float offset_angle = api_float_get(op->ptr, "offset_angle");

  TransVertStore tvs = {nullptr};
  Ob *obedit = cxt_data_edit_ob(C);

  /* typically from 'rv3d' and 3d cursor */
  float viewmat[4][4];
  float center[3];

  /* 'viewmat' relative vars */
  float mat_view[4][4];
  float center_view[3];

  float min, max;

  ed_transverts_create_from_obedit(&tvs, obedit, TM_ALL_JOINTS | TM_SKIP_HANDLES);
  if (tvs.transverts == nullptr) {
    return OP_CANCELLED;
  }

  /* Get view-matrix. */
  {
    ApiProp *prop_viewmat = api_struct_find_prop(op->ptr, "viewmat");
    if (api_prop_is_set(op->ptr, prop_viewmat)) {
      api_prop_float_get_array(op->ptr, prop_viewmat, (float *)viewmat);
    }
    else {
      RgnView3D *rv3d = cxt_win_rgn_view3d(C);

      if (rv3d) {
        copy_m4_m4(viewmat, rv3d->viewmat);
      }
      else {
        unit_m4(viewmat);
      }

      api_prop_float_set_array(op->ptr, prop_viewmat, (float *)viewmat);
    }
  }

  /* get center */
  {
    ApiProp *prop_center = api_struct_find_prop(op->ptr, "center");
    if (api_prop_is_set(op->ptr, prop_center)) {
      api_prop_float_get_array(op->ptr, prop_center, center);
    }
    else {
      const Scene *scene = cxt_data_scene(C);
      copy_v3_v3(center, scene->cursor.location);

      api_prop_float_set_array(op->ptr, prop_center, center);
    }
  }

  ob_warp_calc_view_matrix(mat_view, center_view, obedit, viewmat, center, offset_angle);

  /* get minmax */
  {
    ApiProp *prop_min = api_struct_find_prop(op->ptr, "min");
    ApiProp *prop_max = api_struct_find_prop(op->ptr, "max");

    if (api_prop_is_set(op->ptr, prop_min) || api_prop_is_set(op->ptr, prop_max)) {
      min = api_prop_float_get(op->ptr, prop_min);
      max = api_prop_float_get(op->ptr, prop_max);
    }
    else {
      /* handy to set the bounds of the mesh */
      ob_warp_transverts_minmax_x(&tvs, mat_view, center_view, &min, &max);

      api_prop_float_set(op->ptr, prop_min, min);
      api_prop_float_set(op->ptr, prop_max, max);
    }

    if (min > max) {
      SWAP(float, min, max);
    }
  }

  if (min != max) {
    ob_warp_transverts(&tvs, mat_view, center_view, warp_angle, min, max);
  }

  ed_transverts_update_obedit(&tvs, obedit);
  ed_transverts_free(&tvs);
    
  win_ev_add_notifier(C, NC_OB | ND_DRW, obedit);

  return OP_FINISHED;
}

void TRANSFORM_OT_vertex_warp(WinOpType *ot)
{
  ApiProp *prop;

  /* ids */
  ot->name = "Warp";
  ot->description = "Warp vertices around the cursor";
  ot->idname = "TRANSFORM_OT_vertex_warp";

  /* api cbs */
  ot->ex = ob_warp_verts_ex;
  ot->poll = ed_transverts_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  prop = api_def_float(ot->sapi,
                       "warp_angle",
                       DEG2RADF(360.0f),
                       -FLT_MAX,
                       FLT_MAX,
                       "Warp Angle",
                       "Amount to warp about the cursor",
                       DEG2RADF(-360.0f),
                       DEG2RADF(360.0f));
  api_def_prop_subtype(prop, PROP_ANGLE);

  prop = api_def_float(ot->sapi,
                       "offset_angle",
                       DEG2RADF(0.0f),
                       -FLT_MAX,
                       FLT_MAX,
                       "Offset Angle",
                       "Angle to use as the basis for warping",
                       DEG2RADF(-360.0f),
                       DEG2RADF(360.0f));
  api_def_prop_subtype(prop, PROP_ANGLE);

  prop = api_def_float(ot->sapi, "min", -1.0f, -FLT_MAX, FLT_MAX, "Min", "", -100.0, 100.0);
  prop = api_def_float(ot->sapi, "max", 1.0f, -FLT_MAX, FLT_MAX, "Max", "", -100.0, 100.0);

  /* hidden props */
  prop = api_def_float_matrix(
      ot->sapi, "viewmat", 4, 4, nullptr, 0.0f, 0.0f, "Matrix", "", 0.0f, 0.0f);
  api_def_prop_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  prop = api_def_float_vector_xyz(
      ot->srna, "center", 3, nullptr, -FLT_MAX, FLT_MAX, "Center", "", -FLT_MAX, FLT_MAX);
  wpi_def_prop_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
}
