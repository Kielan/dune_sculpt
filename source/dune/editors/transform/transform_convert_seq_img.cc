#include "mem_guardedalloc.h"

#include "types_space.h"

#include "lib_list.h"
#include "lib_math_matrix.h"
#include "lib_math_rotation.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_report.h"

#include "seq_channels.hh"
#include "seq_iter.hh"
#include "seq_relations.hh"
#include "seq.hh"
#include "seq_time.hh"
#include "seq_transform.hh"
#include "seq_utils.hh"

#include "ed_keyframing.hh"

#include "anim_keyframing.hh"

#include "ui_view2d.hh"

#include "api_access.hh"
#include "api_prototypes.h"

#include "transform.hh"
#include "transform_convert.hh"

/* Used for seq transform. */
struct TransDataSeq {
  Seq *seq;
  float orig_origin_position[2];
  float orig_translation[2];
  float orig_scale[2];
  float orig_rotation;
};

static TransData *SeqToTransData(const Scene *scene,
                                 Seq *seq,
                                 TransData *td,
                                 TransData2D *td2d,
                                 TransDataSeq *tdseq,
                                 int vert_index)
{
  const StripTransform *transform = seq->strip->transform;
  float origin[2];
  seq_img_transform_origin_offset_pixelspace_get(scene, seq, origin);
  float vertex[2] = {origin[0], origin[1]};

  /* Add ctrl vertex, so rotation and scale can be calc.
   * All 3 verts will form a "L" shape aligned to the local strip axis. */
  if (vert_index == 1) {
    vertex[0] += cosf(transform->rotation);
    vertex[1] += sinf(transform->rotation);
  }
  else if (vert_index == 2) {
    vertex[0] -= sinf(transform->rotation);
    vertex[1] += cosf(transform->rotation);
  }

  td2d->loc[0] = vertex[0];
  td2d->loc[1] = vertex[1];
  td2d->loc2d = nullptr;
  td->loc = td2d->loc;
  copy_v3_v3(td->iloc, td->loc);

  td->center[0] = origin[0];
  td->center[1] = origin[1];

  unit_m3(td->mtx);
  unit_m3(td->smtx);

  axis_angle_to_mat3_single(td->axismtx, 'Z', transform->rotation);
  normalize_m3(td->axismtx);

  tdseq->seq = seq;
  copy_v2_v2(tdseq->orig_origin_position, origin);
  tdseq->orig_translation[0] = transform->xofs;
  tdseq->orig_translation[1] = transform->yofs;
  tdseq->orig_scale[0] = transform->scale_x;
  tdseq->orig_scale[1] = transform->scale_y;
  tdseq->orig_rotation = transform->rotation;

  td->extra = (void *)tdseq;
  td->ext = nullptr;
  td->flag |= TD_SELECTED;
  td->dist = 0.0;

  return td;
}

static void freeSeqData(TransInfo * /*t*/,
                        TransDataContainer *tc,
                        TransCustomData * /*custom_data*/)
{
  TransData *td = (TransData *)tc->data;
  mem_free(td->extra);
}

static void createTransSeqImgData(Cxt * /*C*/, TransInfo *t)
{
  Editing *ed = seq_editing_get(t->scene);
  const SpaceSeq *sseq = static_cast<const SpaceSeq *>(t->area->spacedata.first);
  const ARgn *rgn = t->rgn;

  if (ed == nullptr) {
    return;
  }
  if (sseq->mainb != SEQ_DRW_IMG_IMBUF) {
    return;
  }
  if (rgn->rgntype == RGN_TYPE_PREVIEW && sseq->view == SEQ_VIEW_PREVIEW) {
    return;
  }

  List *seqlist = seq_active_seqbase_get(ed);
  List *channels = seq_channels_displayed_get(ed);
  dune::VectorSet strips = seq_query_rendered_strips(
      t->scene, channels, seqlist, t->scene->r.cfra, 0);
  strips.remove_if([&](Seq *seq) { return (seq->flag & SEL) == 0; });

  if (strips.is_empty()) {
    return;
  }

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);
  tc->custom.type.free_cb = freeSeqData;

  tc->data_len = strips.size() * 3; /* 3 vertices per sequence are needed. */
  TransData *td = tc->data = static_cast<TransData *>(
      mem_calloc(tc->data_len * sizeof(TransData), "TransSeq TransData"));
  TransData2D *td2d = tc->data_2d = static_cast<TransData2D *>(
      mem_calloc(tc->data_len * sizeof(TransData2D), "TransSeq TransData2D"));
  TransDataSeq *tdseq = static_cast<TransDataSeq *>(
      mem_calloc(tc->data_len * sizeof(TransDataSeq), "TransSeq TransDataSeq"));

  for (Seq *seq : strips) {
    /* 1 Seq needs 3 TransData entries: center point placed in img origin
     * then 2 points offset by 1 in X and Y direction respectively,
     * so rotation and scale can be calc from these points. */
    SeqToTransData(t->scene, seq, td++, td2d++, tdseq++, 0);
    SeqToTransData(t->scene, seq, td++, td2d++, tdseq++, 1);
    SeqToTransData(t->scene, seq, td++, td2d++, tdseq++, 2);
  }
}

static bool autokeyframe_seq_img(Cxt *C,
                                 Scene *scene,
                                 StripTransform *transform,
                                 const int tmode)
{
  ApiProp *prop;
  ApiPtr ptr = api_ptr_create(&scene->id, &ApiSeqTransform, transform);

  const bool around_cursor = scene->toolsettings->seq_tool_settings->pivot_point ==
                             V3D_AROUND_CURSOR;
  const bool do_loc = tmode == TFM_TRANSLATION || around_cursor;
  const bool do_rot = tmode == TFM_ROTATION;
  const bool do_scale = tmode == TFM_RESIZE;

  bool changed = false;
  if (do_rot) {
    prop = api_struct_find_prop(&ptr, "rotation");
    changed |= dune::animrig::autokeyframe_prop(
        C, scene, &ptr, prop, -1, scene->r.cfra, false);
  }
  if (do_loc) {
    prop = api_struct_find_prop(&ptr, "offset_x");
    changed |= dune::animrig::autokeyframe_prop(
        C, scene, &ptr, prop, -1, scene->r.cfra, false);
    prop = api_struct_find_prop(&ptr, "offset_y");
    changed |= dune::animrig::autokeyframe_prop(
        C, scene, &ptr, prop, -1, scene->r.cfra, false);
  }
  if (do_scale) {
    prop = api_struct_find_prop(&ptr, "scale_x");
    changed |= dune::animrig::autokeyframe_prop(
        C, scene, &ptr, prop, -1, scene->r.cfra, false);
    prop = api_struct_find_prop(&ptr, "scale_y");
    changed |= dune::animrig::autokeyframe_prop(
        C, scene, &ptr, prop, -1, scene->r.cfra, false);
  }

  return changed;
}

static void recalcData_seq_img(TransInfo *t)
{
  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);
  TransData *td = nullptr;
  TransData2D *td2d = nullptr;
  int i;

  for (i = 0, td = tc->data, td2d = tc->data_2d; i < tc->data_len; i++, td++, td2d++) {
    /* Origin. */
    float origin[2];
    copy_v2_v2(origin, td2d->loc);
    i++;
    td++;
    td2d++;

    /* X and Y ctrl points used to read scale and rotation. */
    float handle_x[2];
    copy_v2_v2(handle_x, td2d->loc);
    sub_v2_v2(handle_x, origin);
    i++;
    td++;
    td2d++;

    float handle_y[2];
    copy_v2_v2(handle_y, td2d->loc);
    sub_v2_v2(handle_y, origin);

    TransDataSeq *tdseq = static_cast<TransDataSeq *>(td->extra);
    Seq *seq = tdseq->seq;
    StripTransform *transform = seq->strip->transform;
    float mirror[2];
    seq_img_transform_mirror_factor_get(seq, mirror);

    /* Calc translation. */
    float translation[2];
    copy_v2_v2(translation, tdseq->orig_origin_position);
    sub_v2_v2(translation, origin);
    mul_v2_v2(translation, mirror);
    translation[0] *= t->scene->r.yasp / t->scene->r.xasp;

    transform->xofs = tdseq->orig_translation[0] - translation[0];
    transform->yofs = tdseq->orig_translation[1] - translation[1];

    /* Scale. */
    transform->scale_x = tdseq->orig_scale[0] * fabs(len_v2(handle_x));
    transform->scale_y = tdseq->orig_scale[1] * fabs(len_v2(handle_y));

    /* Rotation. Scaling can cause negative rotation. */
    if (t->mode == TFM_ROTATION) {
      transform->rotation = tdseq->orig_rotation - t->vals_final[0];
    }

    if ((t->animtimer) && dune::animrig::is_autokey_on(t->scene)) {
      animrecord_check_state(t, &t->scene->id);
      autokeyframe_seq_img(t->context, t->scene, transform, t->mode);
    }

    seq_relations_invalidate_cache_preprocessed(t->scene, seq);
  }
}

static void special_aftertrans_update_seq_image(Cxt * /*C*/, TransInfo *t)
{

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);
  TransData *td = nullptr;
  TransData2D *td2d = nullptr;
  int i;

  for (i = 0, td = tc->data, td2d = tc->data_2d; i < tc->data_len; i++, td++, td2d++) {
    TransDataSeq *tdseq = static_cast<TransDataSeq *>(td->extra);
    Seq *seq = tdseq->seq;
    StripTransform *transform = seq->strip->transform;
    if (t->state == TRANS_CANCEL) {
      if (t->mode == TFM_ROTATION) {
        transform->rotation = tdseq->orig_rotation;
      }
      continue;
    }

    if (dune::animrig::is_autokey_on(t->scene)) {
      autokeyframe_seq_img(t->cxt, t->scene, transform, t->mode);
    }
  }
}

TransConvertTypeInfo TransConvertType_SequencerImage = {
    /*flags*/ (T_POINTS | T_2D_EDIT),
    /*create_trans_data*/ createTransSeqImageData,
    /*recalc_data*/ recalcData_sequencer_image,
    /*special_aftertrans_update*/ special_aftertrans_update__sequencer_image,
};
