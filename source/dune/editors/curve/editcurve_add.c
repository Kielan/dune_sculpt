
#include "DNA_anim_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"

#include "BLT_translation.h"

#include "BKE_context.h"
#include "BKE_curve.h"

#include "DEG_depsgraph.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_curve.h"
#include "ED_object.h"
#include "ED_screen.h"
#include "ED_view3d.h"

#include "curve_intern.h"

static const float nurbcircle[8][2] = {
    {0.0, -1.0},
    {-1.0, -1.0},
    {-1.0, 0.0},
    {-1.0, 1.0},
    {0.0, 1.0},
    {1.0, 1.0},
    {1.0, 0.0},
    {1.0, -1.0},
};

/************ add primitive, used by object/ module ****************/

static const char *get_curve_defname(int type)
{
  int stype = type & CU_PRIMITIVE;

  if ((type & CU_TYPE) == CU_BEZIER) {
    switch (stype) {
      case CU_PRIM_CURVE:
        return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "BezierCurve");
      case CU_PRIM_CIRCLE:
        return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "BezierCircle");
      case CU_PRIM_PATH:
        return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "CurvePath");
      default:
        return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "Curve");
    }
  }
  else {
    switch (stype) {
      case CU_PRIM_CURVE:
        return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "NurbsCurve");
      case CU_PRIM_CIRCLE:
        return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "NurbsCircle");
      case CU_PRIM_PATH:
        return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "NurbsPath");
      default:
        return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "Curve");
    }
  }
}

static const char *get_surf_defname(int type)
{
  int stype = type & CU_PRIMITIVE;

  switch (stype) {
    case CU_PRIM_CURVE:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "SurfCurve");
    case CU_PRIM_CIRCLE:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "SurfCircle");
    case CU_PRIM_PATCH:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "SurfPatch");
    case CU_PRIM_SPHERE:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "SurfSphere");
    case CU_PRIM_DONUT:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "SurfTorus");
    default:
      return CTX_DATA_(BLT_I18NCONTEXT_ID_CURVE_LEGACY, "Surface");
  }
}

Nurb *ED_curve_add_nurbs_primitive(
    bContext *C, Object *obedit, float mat[4][4], int type, int newob)
{
  static int xzproj = 0; /* this function calls itself... */
  ListBase *editnurb = object_editcurve_get(obedit);
  RegionView3D *rv3d = ED_view3d_context_rv3d(C);
  Nurb *nu = NULL;
  BezTriple *bezt;
  BPoint *bp;
  Curve *cu = (Curve *)obedit->data;
  float vec[3], zvec[3] = {0.0f, 0.0f, 1.0f};
  float umat[4][4], viewmat[4][4];
  float fac;
  int a, b;
  const float grid = 1.0f;
  const int cutype = (type & CU_TYPE); /* poly, bezier, nurbs, etc */
  const int stype = (type & CU_PRIMITIVE);

  unit_m4(umat);
  unit_m4(viewmat);

  if (rv3d) {
    copy_m4_m4(viewmat, rv3d->viewmat);
    copy_v3_v3(zvec, rv3d->viewinv[2]);
  }

  BKE_nurbList_flag_set(editnurb, SELECT, false);

  /* these types call this function to return a Nurb */
  if (!ELEM(stype, CU_PRIM_TUBE, CU_PRIM_DONUT)) {
    nu = (Nurb *)MEM_callocN(sizeof(Nurb), "addNurbprim");
    nu->type = cutype;
    nu->resolu = cu->resolu;
    nu->resolv = cu->resolv;
  }

  switch (stype) {
    case CU_PRIM_CURVE: /* curve */
      nu->resolu = cu->resolu;
      if (cutype == CU_BEZIER) {
        nu->pntsu = 2;
        nu->bezt = (BezTriple *)MEM_callocN(sizeof(BezTriple) * nu->pntsu, "addNurbprim1");
        bezt = nu->bezt;
        bezt->h1 = bezt->h2 = HD_ALIGN;
        bezt->f1 = bezt->f2 = bezt->f3 = SELECT;
        bezt->radius = 1.0;

        bezt->vec[1][0] += -grid;
        bezt->vec[0][0] += -1.5f * grid;
        bezt->vec[0][1] += -0.5f * grid;
        bezt->vec[2][0] += -0.5f * grid;
        bezt->vec[2][1] += 0.5f * grid;
        for (a = 0; a < 3; a++) {
          mul_m4_v3(mat, bezt->vec[a]);
        }

        bezt++;
        bezt->h1 = bezt->h2 = HD_ALIGN;
        bezt->f1 = bezt->f2 = bezt->f3 = SELECT;
        bezt->radius = bezt->weight = 1.0;

        bezt->vec[0][0] = 0;
        bezt->vec[0][1] = 0;
        bezt->vec[1][0] = grid;
        bezt->vec[1][1] = 0;
        bezt->vec[2][0] = grid * 2;
        bezt->vec[2][1] = 0;
        for (a = 0; a < 3; a++) {
          mul_m4_v3(mat, bezt->vec[a]);
        }

        BKE_nurb_handles_calc(nu);
      }
      else {

        nu->pntsu = 4;
        nu->pntsv = 1;
        nu->orderu = 4;
        nu->bp = (BPoint *)MEM_callocN(sizeof(BPoint) * nu->pntsu, "addNurbprim3");

        bp = nu->bp;
        for (a = 0; a < 4; a++, bp++) {
          bp->vec[3] = 1.0;
          bp->f1 = SELECT;
          bp->radius = bp->weight = 1.0;
        }

        bp = nu->bp;
        bp->vec[0] += -1.5f * grid;
        bp++;
        bp->vec[0] += -grid;
        bp->vec[1] += grid;
        bp++;
        bp->vec[0] += grid;
        bp->vec[1] += grid;
        bp++;
        bp->vec[0] += 1.5f * grid;

        bp = nu->bp;
        for (a = 0; a < 4; a++, bp++) {
          mul_m4_v3(mat, bp->vec);
        }

        if (cutype == CU_NURBS) {
          nu->knotsu = NULL; /* nurbs_knot_calc_u allocates */
          BKE_nurb_knot_calc_u(nu);
        }
      }
      break;
    case CU_PRIM_PATH: /* 5 point path */
      nu->pntsu = 5;
      nu->pntsv = 1;
      nu->orderu = 5;
      nu->flagu = CU_NURB_ENDPOINT; /* endpoint */
      nu->resolu = cu->resolu;
      nu->bp = (BPoint *)MEM_callocN(sizeof(BPoint) * nu->pntsu, "addNurbprim3");

      bp = nu->bp;
      for (a = 0; a < 5; a++, bp++) {
        bp->vec[3] = 1.0;
        bp->f1 = SELECT;
        bp->radius = bp->weight = 1.0;
      }

      bp = nu->bp;
      bp->vec[0] += -2.0f * grid;
      bp++;
      bp->vec[0] += -grid;
      bp++;
      bp++;
      bp->vec[0] += grid;
      bp++;
      bp->vec[0] += 2.0f * grid;

      bp = nu->bp;
      for (a = 0; a < 5; a++, bp++) {
        mul_m4_v3(mat, bp->vec);
      }

      if (cutype == CU_NURBS) {
        nu->knotsu = NULL; /* nurbs_knot_calc_u allocates */
        BKE_nurb_knot_calc_u(nu);
      }

      break;
    case CU_PRIM_CIRCLE: /* circle */
      nu->resolu = cu->resolu;

      if (cutype == CU_BEZIER) {
        nu->pntsu = 4;
        nu->bezt = (BezTriple *)MEM_callocN(sizeof(BezTriple) * nu->pntsu, "addNurbprim1");
        nu->flagu = CU_NURB_CYCLIC;
        bezt = nu->bezt;

        bezt->h1 = bezt->h2 = HD_AUTO;
        bezt->f1 = bezt->f2 = bezt->f3 = SELECT;
        bezt->vec[1][0] += -grid;
        for (a = 0; a < 3; a++) {
          mul_m4_v3(mat, bezt->vec[a]);
        }
        bezt->radius = bezt->weight = 1.0;

        bezt++;
        bezt->h1 = bezt->h2 = HD_AUTO;
        bezt->f1 = bezt->f2 = bezt->f3 = SELECT;
        bezt->vec[1][1] += grid;
        for (a = 0; a < 3; a++) {
          mul_m4_v3(mat, bezt->vec[a]);
        }
        bezt->radius = bezt->weight = 1.0;

        bezt++;
        bezt->h1 = bezt->h2 = HD_AUTO;
        bezt->f1 = bezt->f2 = bezt->f3 = SELECT;
        bezt->vec[1][0] += grid;
        for (a = 0; a < 3; a++) {
          mul_m4_v3(mat, bezt->vec[a]);
        }
        bezt->radius = bezt->weight = 1.0;

        bezt++;
        bezt->h1 = bezt->h2 = HD_AUTO;
        bezt->f1 = bezt->f2 = bezt->f3 = SELECT;
        bezt->vec[1][1] += -grid;
        for (a = 0; a < 3; a++) {
          mul_m4_v3(mat, bezt->vec[a]);
        }
        bezt->radius = bezt->weight = 1.0;

        BKE_nurb_handles_calc(nu);
      }
      else if (cutype == CU_NURBS) { /* nurb */
        nu->pntsu = 8;
        nu->pntsv = 1;
        nu->orderu = 3;
        nu->bp = (BPoint *)MEM_callocN(sizeof(BPoint) * nu->pntsu, "addNurbprim6");
        nu->flagu = CU_NURB_CYCLIC | CU_NURB_BEZIER | CU_NURB_ENDPOINT;
        bp = nu->bp;

        for (a = 0; a < 8; a++) {
          bp->f1 = SELECT;
          if (xzproj == 0) {
            bp->vec[0] += nurbcircle[a][0] * grid;
            bp->vec[1] += nurbcircle[a][1] * grid;
          }
          else {
            bp->vec[0] += 0.25f * nurbcircle[a][0] * grid - 0.75f * grid;
            bp->vec[2] += 0.25f * nurbcircle[a][1] * grid;
          }
          if (a & 1) {
            bp->vec[3] = 0.5 * M_SQRT2;
          }
          else {
            bp->vec[3] = 1.0;
          }
          mul_m4_v3(mat, bp->vec);
          bp->radius = bp->weight = 1.0;

          bp++;
        }

        BKE_nurb_knot_calc_u(nu);
      }
      break;
    case CU_PRIM_PATCH:         /* 4x4 patch */
      if (cutype == CU_NURBS) { /* nurb */

        nu->pntsu = 4;
        nu->pntsv = 4;
        nu->orderu = 4;
        nu->orderv = 4;
        nu->flag = CU_SMOOTH;
        nu->bp = (BPoint *)MEM_callocN(sizeof(BPoint) * (4 * 4), "addNurbprim6");
        nu->flagu = 0;
        nu->flagv = 0;
        bp = nu->bp;

        for (a = 0; a < 4; a++) {
          for (b = 0; b < 4; b++) {
            bp->f1 = SELECT;
            fac = (float)a - 1.5f;
            bp->vec[0] += fac * grid;
            fac = (float)b - 1.5f;
            bp->vec[1] += fac * grid;
            if ((ELEM(a, 1, 2)) && (ELEM(b, 1, 2))) {
              bp->vec[2] += grid;
            }
            mul_m4_v3(mat, bp->vec);
            bp->vec[3] = 1.0;
            bp++;
          }
        }

        BKE_nurb_knot_calc_u(nu);
        BKE_nurb_knot_calc_v(nu);
      }
      break;
    case CU_PRIM_TUBE: /* Cylinder */
      if (cutype == CU_NURBS) {
        nu = ED_curve_add_nurbs_primitive(C, obedit, mat, CU_NURBS | CU_PRIM_CIRCLE, 0);
        nu->resolu = cu->resolu;
        nu->flag = CU_SMOOTH;
        BLI_addtail(editnurb, nu); /* temporal for extrude and translate */
        vec[0] = vec[1] = 0.0;
        vec[2] = -grid;

        mul_mat3_m4_v3(mat, vec);

        ed_editnurb_translate_flag(editnurb, SELECT, vec, CU_IS_2D(cu));
        ed_editnurb_extrude_flag(cu->editnurb, SELECT);
        mul_v3_fl(vec, -2.0f);
        ed_editnurb_translate_flag(editnurb, SELECT, vec, CU_IS_2D(cu));

        BLI_remlink(editnurb, nu);

        a = nu->pntsu * nu->pntsv;
        bp = nu->bp;
        while (a-- > 0) {
          bp->f1 |= SELECT;
          bp++;
        }
      }
      break;
    case CU_PRIM_SPHERE: /* sphere */
      if (cutype == CU_NURBS) {
        const float tmp_cent[3] = {0.0f, 0.0f, 0.0f};
        const float tmp_vec[3] = {0.0f, 0.0f, 1.0f};

        nu->pntsu = 5;
        nu->pntsv = 1;
        nu->orderu = 3;
        nu->resolu = cu->resolu;
        nu->resolv = cu->resolv;
        nu->flag = CU_SMOOTH;
        nu->bp = (BPoint *)MEM_callocN(sizeof(BPoint) * nu->pntsu, "addNurbprim6");
        nu->flagu = 0;
        bp = nu->bp;

        for (a = 0; a < 5; a++) {
          bp->f1 = SELECT;
          bp->vec[0] += nurbcircle[a][0] * grid;
          bp->vec[2] += nurbcircle[a][1] * grid;
          if (a & 1) {
            bp->vec[3] = 0.5 * M_SQRT2;
          }
          else {
            bp->vec[3] = 1.0;
          }
          mul_m4_v3(mat, bp->vec);
          bp++;
        }
        nu->flagu = CU_NURB_BEZIER | CU_NURB_ENDPOINT;
        BKE_nurb_knot_calc_u(nu);

        BLI_addtail(editnurb, nu); /* temporal for spin */

        if (newob && (U.flag & USER_ADD_VIEWALIGNED) == 0) {
          ed_editnurb_spin(umat, NULL, obedit, tmp_vec, tmp_cent);
        }
        else if (U.flag & USER_ADD_VIEWALIGNED) {
          ed_editnurb_spin(viewmat, NULL, obedit, zvec, mat[3]);
        }
        else {
          ed_editnurb_spin(umat, NULL, obedit, tmp_vec, mat[3]);
        }

        BKE_nurb_knot_calc_v(nu);

        a = nu->pntsu * nu->pntsv;
        bp = nu->bp;
        while (a-- > 0) {
          bp->f1 |= SELECT;
          bp++;
        }
        BLI_remlink(editnurb, nu);
      }
      break;
    case CU_PRIM_DONUT: /* torus */
      if (cutype == CU_NURBS) {
        const float tmp_cent[3] = {0.0f, 0.0f, 0.0f};
        const float tmp_vec[3] = {0.0f, 0.0f, 1.0f};

        xzproj = 1;
        nu = ED_curve_add_nurbs_primitive(C, obedit, mat, CU_NURBS | CU_PRIM_CIRCLE, 0);
        xzproj = 0;
        nu->resolu = cu->resolu;
        nu->resolv = cu->resolv;
        nu->flag = CU_SMOOTH;
        BLI_addtail(editnurb, nu); /* temporal for spin */

        /* same as above */
        if (newob && (U.flag & USER_ADD_VIEWALIGNED) == 0) {
          ed_editnurb_spin(umat, NULL, obedit, tmp_vec, tmp_cent);
        }
        else if (U.flag & USER_ADD_VIEWALIGNED) {
          ed_editnurb_spin(viewmat, NULL, obedit, zvec, mat[3]);
        }
        else {
          ed_editnurb_spin(umat, NULL, obedit, tmp_vec, mat[3]);
        }

        BLI_remlink(editnurb, nu);

        a = nu->pntsu * nu->pntsv;
        bp = nu->bp;
        while (a-- > 0) {
          bp->f1 |= SELECT;
          bp++;
        }
      }
      break;

    default: /* should never happen */
      BLI_assert_msg(0, "invalid nurbs type");
      return NULL;
  }

  BLI_assert(nu != NULL);

  if (nu) { /* should always be set */
    nu->flag |= CU_SMOOTH;
    cu->actnu = BLI_listbase_count(editnurb);
    cu->actvert = CU_ACT_NONE;

    if (CU_IS_2D(cu)) {
      BKE_nurb_project_2d(nu);
    }
  }

  return nu;
}

static int curvesurf_prim_add(bContext *C, wmOperator *op, int type, int isSurf)
{
  struct Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Object *obedit = OBEDIT_FROM_VIEW_LAYER(view_layer);
  ListBase *editnurb;
  Nurb
