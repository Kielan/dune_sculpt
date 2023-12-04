#include "mrm_guardedalloc.h"

#include "lib_math_matrix.h"
#include "lib_math_vector.h"

#include "dune_animsys.h"
#include "dune_cxt.hh"
#include "dune_layer.h"
#include "dune_ob.hh"
#include "dune_report.h"

#include "types_mesh.h"

#include "transform.hh"
#include "transform_snap.hh"

/* Own include. */
#include "transform_convert.hh"

/* Texture Space Transform Creatio
 * Instead of transforming the sel, move the 2D/3D cursor. */
static void createTransTexspace(Cxt * /*C*/, TransInfo *t)
{
  ViewLayer *view_layer = t->view_layer;
  TransData *td;
  Ob *ob;
  Id *id;
  char *texspace_flag;

  dune_view_layer_synced_ensure(t->scene, t->view_layer);
  ob = dune_view_layer_active_ob_get(view_layer);

  if (ob == nullptr) { /* Shouldn't logically happen, but still. */
    return;
  }

  id = static_cast<Id *>(ob->data);
  if (id == nullptr || !ELEM(GS(id->name), ID_ME, ID_CU_LEGACY, ID_MB)) {
    dune_report(t->reports, RPT_ERROR, "Unsupported ob type for texture-space transform");
    return;
  }

  if (dune_ob_obdata_is_libdata(ob)) {
    dune_report(t->reports, RPT_ERROR, "Linked data can't texture-space transform");
    return;
  }

  {
    lib_assert(t->data_container_len == 1);
    TransDataContainer *tc = t->data_container;
    tc->data_len = 1;
    td = tc->data = static_cast<TransData *>(mem_calloc(sizeof(TransData), "TransTexspace"));
    td->ext = tc->data_ext = static_cast<TransDataExtension *>(
        mem_calloc(sizeof(TransDataExtension), "TransTexspace"));
  }

  td->flag = TD_SELECTED;
  td->ob = ob;

  copy_m3_m4(td->mtx, ob->ob_to_world);
  copy_m3_m4(td->axismtx, ob->ob_to_world);
  normalize_m3(td->axismtx);
  pseudoinverse_m3_m3(td->smtx, td->mtx, PSEUDOINVERSE_EPSILON);
  
  if (dune_ob_obdata_texspace_get(ob, &texspace_flag, &td->loc, &td->ext->size)) {
    ob->dtx |= OB_TEXSPACE;
    *texspace_flag &= ~ME_TEXSPACE_FLAG_AUTO;
  }

  copy_v3_v3(td->iloc, td->loc);
  copy_v3_v3(td->center, td->loc);
  copy_v3_v3(td->ext->isize, td->ext->size);
}

/* Recalc Data ob */
static void recalcData_texspace(TransInfo *t)
{

  if (t->state != TRANS_CANCEL) {
    transform_snap_project_individual_apply(t);
  }

  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    TransData *td = tc->data;

    for (int i = 0; i < tc->data_len; i++, td++) {
      if (td->flag & TD_SKIP) {
        continue;
      }
      graph_id_tag_update(&td->ob->id, ID_RECALC_GEOMETRY);
    }
  }
}

TransConvertTypeInfo TransConvertTypeObTexSpace = {
    /*flags*/ 0,
    /*create_trans_data*/ createTransTexspace,
    /*recalc_data*/ recalcData_texspace,
    /*special_aftertrans_update*/ nullptr,
};
