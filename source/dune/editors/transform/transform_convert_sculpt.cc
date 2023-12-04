#include "mem_guardedalloc.h"

#include "lib_math_matrix.h"
#include "lib_math_rotation.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_paint.hh"
#include "dune_report.h"

#include "ed_sculpt.hh"

#include "transform.hh"
#include "transform_convert.hh"

/* Sculpt Transform Creation */
static void createTransSculpt(Cxt *C, TransInfo *t)
{
  TransData *td;

  Scene *scene = t->scene;
  if (!dune_id_is_editable(cxt_data_main(C), &scene->id)) {
    dune_report(t->reports, RPT_ERROR, "Linked data can't text-space transform");
    return;
  }

  dune_view_layer_synced_ensure(t->scene, t->view_layer);
  Ob *ob = dune_view_layer_active_ob_get(t->view_layer);
  SculptSession *ss = ob->sculpt;

  {
    lib_assert(t->data_container_len == 1);
    TransDataContainer *tc = t->data_container;
    tc->data_len = 1;
    tc->is_active = true;
    td = tc->data = mem_cnew<TransData>(__func__);
    td->ext = tc->data_ext = mem_cnew<TransDataExtension>(__func__);
  }

  td->flag = TD_SELECTED;
  copy_v3_v3(td->center, ss->pivot_pos);
  mul_m4_v3(ob->ob_to_world, td->center);
  td->ob = ob;

  td->loc = ss->pivot_pos;
  copy_v3_v3(td->iloc, ss->pivot_pos);

  if (is_zero_v4(ss->pivot_rot)) {
    ss->pivot_rot[3] = 1.0f;
  }

  float obmat_inv[3][3];
  copy_m3_m4(obmat_inv, ob->ob_to_world);
  invert_m3(obmat_inv);

  td->ext->rot = nullptr;
  td->ext->rotAxis = nullptr;
  td->ext->rotAngle = nullptr;
  td->ext->quat = ss->pivot_rot;
  copy_m4_m4(td->ext->obmat, ob->ob_to_world);
  copy_m3_m3(td->ext->l_smtx, obmat_inv);
  copy_m3_m4(td->ext->r_mtx, ob->ob_to_world);
  copy_m3_m3(td->ext->r_smtx, obmat_inv);

  copy_qt_qt(td->ext->iquat, ss->pivot_rot);
  td->ext->rotOrder = ROT_MODE_QUAT;

  ss->pivot_scale[0] = 1.0f;
  ss->pivot_scale[1] = 1.0f;
  ss->pivot_scale[2] = 1.0f;
  td->ext->size = ss->pivot_scale;
  copy_v3_v3(ss->init_pivot_scale, ss->pivot_scale);
  copy_v3_v3(td->ext->isize, ss->init_pivot_scale);

  copy_m3_m3(td->smtx, obmat_inv);
  copy_m3_m4(td->mtx, ob->ob_to_world);
  copy_m3_m4(td->axismtx, ob->ob_to_world);

  lib_assert(!(t->options & CXT_PAINT_CURVE));
  ed_sculpt_init_transform(C, ob, t->mval, t->undo_name);
}

/* Recalc Data ob */
static void recalcData_sculpt(TransInfo *t)
{
  dune_view_layer_synced_ensure(t->scene, t->view_layer);
  Ob *ob = dune_view_layer_active_ob_get(t->view_layer);
  ed_sculpt_update_modal_transform(t->cxt, ob);
}

static void special_aftertrans_update_sculpt(Cxt *C, TransInfo *t)
{
  Scene *scene = t->scene;
  if (!dune_id_is_editable(cxt_data_main(C), &scene->id)) {
    /* `ed_sculpt_init_transform` was not called in this case. */
    return;
  }

  dune_view_layer_synced_ensure(t->scene, t->view_layer);
  Ob *ob = dune_view_layer_active_ob_get(t->view_layer);
  lib_assert(!(t->options & CXT_PAINT_CURVE));
  ed_sculpt_end_transform(C, ob);
}

TransConvertTypeInfo TransConvertType_Sculpt = {
    /*flags*/ 0,
    /*create_trans_data*/ createTransSculpt,
    /*recalc_data*/ recalcData_sculpt,
    /*special_aftertrans_update*/ special_aftertrans_update_sculpt,
};
