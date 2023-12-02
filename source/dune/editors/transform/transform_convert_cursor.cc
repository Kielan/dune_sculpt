/* Instead of transforming the sel, move the 2D/3D cursor. */
#include "types_space.h"

#include "mem_guardedalloc.h"

#include "lib_math_matrix.h"
#include "lib_math_rotation.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_report.h"
#include "dune_scene.h"

#include "transform.hh"
#include "transform_convert.hh"

/* Shared 2D Cursor Utils */
static void createTransCursor_2D_impl(TransInfo *t, float cursor_location[2])
{
  TransData *td;
  TransData2D *td2d;
  {
    lib_assert(t->data_container_len == 1);
    TransDataContainer *tc = t->data_container;
    tc->data_len = 1;
    td = tc->data = static_cast<TransData *>(MEM_callocN(sizeof(TransData), "TransTexspace"));
    td2d = tc->data_2d = static_cast<TransData2D *>(
        mem_calloc(tc->data_len * sizeof(TransData2D), "TransObData2D(Cursor)"));
    td->ext = tc->data_ext = static_cast<TransDataExtension *>(
        mem_calloc(sizeof(TransDataExtension), "TransCursorExt"));
  }

  td->flag = TD_SELECTED;

  td2d->loc2d = cursor_location;

  /* UV coords are scaled by aspects (see UVsToTransData). This also applies for the Cursor in the
   * UV Editor which also means that for display and when the cursor coords are flushed
   * (recalcData_cursor_img), these are converted each time. */
  td2d->loc[0] = cursor_location[0] * t->aspect[0];
  td2d->loc[1] = cursor_location[1] * t->aspect[1];
  td2d->loc[2] = 0.0f;

  copy_v3_v3(td->center, td2d->loc);

  td->ob = nullptr;

  unit_m3(td->mtx);
  unit_m3(td->axismtx);
  pseudoinverse_m3_m3(td->smtx, td->mtx, PSEUDOINVERSE_EPSILON);

  td->loc = td2d->loc;
  copy_v3_v3(td->iloc, td2d->loc);
}

static void recalcData_cursor_2D_impl(TransInfo *t)
{
  TransDataContainer *tc = t->data_container;
  TransData *td = tc->data;
  TransData2D *td2d = tc->data_2d;
  float aspect_inv[2];

  aspect_inv[0] = 1.0f / t->aspect[0];
  aspect_inv[1] = 1.0f / t->aspect[1];

  td2d->loc2d[0] = td->loc[0] * aspect_inv[0];
  td2d->loc2d[1] = td->loc[1] * aspect_inv[1];

  graph_id_tag_update(&t->scene->id, ID_RECALC_COPY_ON_WRITE);
}

/* Img Cursor */
static void createTransCursor_img(Cxt * /*C*/, TransInfo *t)
{
  SpaceImg *simg = static_cast<SpaceImg *>(t->area->spacedata.first);
  createTransCursor_2D_impl(t, simg->cursor);
}

static void recalcData_cursor_img(TransInfo *t)
{
  recalcData_cursor_2D_impl(t);
}

/* Seq Cursor */
static void createTransCursor_seq(Cxt * /*C*/, TransInfo *t)
{
  SpaceSeq *sseq = static_cast<SpaceSeq *>(t->area->spacedata.first);
  if (sseq->mainb != SEQ_DRW_IMG_IMBUF) {
    return;
  }
  createTransCursor_2D_impl(t, sseq->cursor);
}

static void recalcData_cursor_seq(TransInfo *t)
{
  recalcData_cursor_2D_impl(t);
}

/* View 3D Cursor */
static void createTransCursor_view3d(bContext * /*C*/, TransInfo *t)
{
  TransData *td;

  Scene *scene = t->scene;
  if (ID_IS_LINKED(scene)) {
    dune_report(t->reports, RPT_ERROR, "Linked data can't text-space transform");
    return;
  }

  View3DCursor *cursor = &scene->cursor;
  {
    lib_assert(t->data_container_len == 1);
    TransDataContainer *tc = t->data_container;
    tc->data_len = 1;
    td = tc->data = static_cast<TransData *>(mem_calloc(sizeof(TransData), "TransTexspace"));
    td->ext = tc->data_ext = static_cast<TransDataExtension *>(
        mem_calloc(sizeof(TransDataExtension), "TransTexspace"));
  }

  td->flag = TD_SELECTED;
  copy_v3_v3(td->center, cursor->location);
  td->ob = nullptr;

  unit_m3(td->mtx);
  dune_scene_cursor_rot_to_mat3(cursor, td->axismtx);
  normalize_m3(td->axismtx);
  pseudoinverse_m3_m3(td->smtx, td->mtx, PSEUDOINVERSE_EPSILON);

  td->loc = cursor->location;
  copy_v3_v3(td->iloc, cursor->location);

  if (cursor->rotation_mode > 0) {
    td->ext->rot = cursor->rotation_euler;
    td->ext->rotAxis = nullptr;
    td->ext->rotAngle = nullptr;
    td->ext->quat = nullptr;

    copy_v3_v3(td->ext->irot, cursor->rotation_euler);
  }
  else if (cursor->rotation_mode == ROT_MODE_AXISANGLE) {
    td->ext->rot = nullptr;
    td->ext->rotAxis = cursor->rotation_axis;
    td->ext->rotAngle = &cursor->rotation_angle;
    td->ext->quat = nullptr;

    td->ext->irotAngle = cursor->rotation_angle;
    copy_v3_v3(td->ext->irotAxis, cursor->rotation_axis);
  }
  else {
    td->ext->rot = nullptr;
    td->ext->rotAxis = nullptr;
    td->ext->rotAngle = nullptr;
    td->ext->quat = cursor->rotation_quaternion;

    copy_qt_qt(td->ext->iquat, cursor->rotation_quaternion);
  }
  td->ext->rotOrder = cursor->rotation_mode;
}

static void recalcData_cursor_view3d(TransInfo *t)
{
  graph_id_tag_update(&t->scene->id, ID_RECALC_COPY_ON_WRITE);
}

TransConvertTypeInfo TransConvertType_CursorImage = {
    /*flags*/ T_2D_EDIT,
    /*create_trans_data*/ createTransCursor_image,
    /*recalc_data*/ recalcData_cursor_image,
    /*special_aftertrans_update*/ nullptr,
};

TransConvertTypeInfo TransConvertType_CursorSeq = {
    /*flags*/ T_2D_EDIT,
    /*create_trans_data*/ createTransCursor_sequ,
    /*recalc_data*/ recalcData_cursor_sequ,
    /*special_aftertrans_update*/ nullptr,
};

TransConvertTypeInfo TransConvertType_Cursor3D = {
    /*flags*/ 0,
    /*create_trans_data*/ createTransCursor_view3d,
    /*recalc_data*/ recalcData_cursor_view3d,
    /*special_aftertrans_update*/ nullptr,
};
