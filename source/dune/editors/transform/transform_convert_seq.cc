#include "types_space.h"

#include "mem_guardedalloc.h"

#include "lib_list.h"
#include "lib_math_matrix.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_main.hh"
#include "dune_report.h"

#include "ed_markers.hh"
#include "ed_time_scrub_ui.hh"

#include "seq_anim.hh"
#include "seq_channels.hh"
#include "seq_edit.hh"
#include "seq_effects.hh"
#include "seq_iter.hh"
#include "seq_relations.hh"
#include "seq.hh"
#include "seq_time.hh"
#include "seq_transform.hh"
#include "seq_utils.hh"

#include "ui_view2d.hh"

#include "transform.hh"
#include "transform_convert.hh"

#define SEQ_EDGE_PAN_INSIDE_PAD 3.5
#define SEQ_EDGE_PAN_OUTSIDE_PAD 0 /* Disable clamping for panning, use whole screen. */
#define SEQ_EDGE_PAN_SPEED_RAMP 1
#define SEQ_EDGE_PAN_MAX_SPEED 4 /* In UI units per second, slower than default. */
#define SEQ_EDGE_PAN_DELAY 1.0f
#define SEQ_EDGE_PAN_ZOOM_INFLUENCE 0.5f

/* Used for seq transform. */
struct TransDataSeq {
  Seq *seq;
  /* A copy of Seq.flag that may be modded for nested strips. */
  int flag;
  /* Provides transform data at the strips start,
   * but apply correctly to the start frame. */
  int start_offset;
  /* opts: SEL SEQ_LEFTSEL and SEQ_RIGHTSEL */
  short sel_flag;
};

/* Seq transform customdata (stored in TransCustomDataContainer). */
struct TransSeq {
  TransDataSeq *tdseq;
  int sel_channel_range_min;
  int sel_channel_range_max;

  /* Init rect of the view2d, used for computing offset during edge panning */
  rctf init_v2d_cur;
  View2DEdgePanData edge_pan;

  /* Strips that aren't sel, but their position entirely depends on transformed strips. */
  dune::VectorSet<Seq *> time_dependent_strips;
};

/* Seq Transform Creation */
/* Fn applies rules for transform a strip so dup
 * checks don't need to be added in multiple places.
 * count and flag MUST be set. */
static void SeqTransInfo(TransInfo *t, Seq *seq, int *r_count, int *r_flag)
{
  Scene *scene = t->scene;
  Editing *ed = seq_editing_get(t->scene);
  List *channels = seq_channels_displayed_get(ed);

  /* for extend we need to do some tricks */
  if (t->mode == TFM_TIME_EXTEND) {

    /* Extend Transform */
    int cfra = scene->r.cfra;
    int left = seq_time_left_handle_frame_get(scene, seq);
    int right = seq_time_right_handle_frame_get(scene, seq);

    if ((seq->flag & SEL) == 0 || seq_transform_is_locked(channels, seq)) {
      *r_count = 0;
      *r_flag = 0;
    }
    else {
      *r_count = 1; /* unless its set to 0, extend will never set 2 handles at once */
      *r_flag = (seq->flag | SEL) & ~(SEQ_LEFTSEL | SEQ_RIGHTSEL);

      if (t->frame_side == 'R') {
        if (right <= cfra) {
          *r_count = *r_flag = 0;
        } /* ignore */
        else if (left > cfra) {
        } /* keep the selection */
        else {
          *r_flag |= SEQ_RIGHTSEL;
        }
      }
      else {
        if (left >= cfra) {
          *r_count = *r_flag = 0;
        } /* ignore */
        else if (right < cfra) {
        } /* keep the selection */
        else {
          *r_flag |= SEQ_LEFTSEL;
        }
      }
    }
  }
  else {

    t->frame_side = 'B';

    /* Normal Transform */
    /* Count */
    /* Non nested strips (reset sel and handles). */
    if ((seq->flag & SEL) == 0 || seq_transform_is_locked(channels, seq)) {
      *r_count = 0;
      *r_flag = 0;
    }
    else {
      if ((seq->flag & (SEQ_LEFTSEL | SEQ_RIGHTSEL)) == (SEQ_LEFTSEL | SEQ_RIGHTSEL)) {
        *r_flag = seq->flag;
        *r_count = 2; /* we need 2 transdata's */
      }
      else {
        *r_flag = seq->flag;
        *r_count = 1; /* sel or with a handle sel */
      }
    }
  }
}

static int SeqTransCount(TransInfo *t, List *seqbase)
{
  int tot = 0, count, flag;

  LIST_FOREACH (Seq *, seq, seqbase) {
    SeqTransInfo(t, seq, &count, &flag); /* ignore the flag */
    tot += count;
  }

  return tot;
}

static TransData *SeqToTransData(Scene *scene,
                                 TransData *td,
                                 TransData2D *td2d,
                                 TransDataSeq *tdsq,
                                 Seq *seq,
                                 int flag,
                                 int sel_flag)
{
  int start_left;

  switch (sel_flag) {
    case SEL:
      /* Use seq_tx_get_final_left() and an offset here
       * so transform has the left hand location of the strip.
       * tdsq->start_offset is used when flushing the tx data back */
      start_left = seq_time_left_handle_frame_get(scene, seq);
      td2d->loc[0] = start_left;
      tdsq->start_offset = start_left - seq->start; /* use to apply the original location */
      break;
    case SEQ_LEFTSEL:
      start_left = seq_time_left_handle_frame_get(scene, seq);
      td2d->loc[0] = start_left;
      break;
    case SEQ_RIGHTSEL:
      td2d->loc[0] = seq_time_right_handle_frame_get(scene, seq);
      break;
  }

  td2d->loc[1] = seq->machine; /* channel - Y location */
  td2d->loc[2] = 0.0f;
  td2d->loc2d = nullptr;

  tdsq->seq = seq;

  /* Use instead of seq->flag for nested strips and other
   * cases where the sel may need to be mod */
  tdsq->flag = flag;
  tdsq->sel_flag = sel_flag;

  td->extra = (void *)tdsq; /* allow us to update the strip from here */

  td->flag = 0;
  td->loc = td2d->loc;
  copy_v3_v3(td->center, td->loc);
  copy_v3_v3(td->iloc, td->loc);

  memset(td->axismtx, 0, sizeof(td->axismtx));
  td->axismtx[2][2] = 1.0f;

  td->ext = nullptr;
  td->val = nullptr;

  td->flag |= TD_SELECTED;
  td->dist = 0.0;

  unit_m3(td->mtx);
  unit_m3(td->smtx);

  /* Time Transform (extend) */
  td->val = td2d->loc;
  td->ival = td2d->loc[0];

  return td;
}

static int SeqToTransData_build(
    TransInfo *t, List *seqbase, TransData *td, TransData2D *td2d, TransDataSeq *tdsq)
{
  Scene *scene = t->scene;
  int count, flag;
  int tot = 0;

  LISTBASE_FOREACH (Sequence *, seq, seqbase) {

    SeqTransInfo(t, seq, &count, &flag);

    /* use 'flag' which is derived from seq->flag but mod for special cases */
    if (flag & SEL) {
      if (flag & (SEQ_LEFTSEL | SEQ_RIGHTSEL)) {
        if (flag & SEQ_LEFTSEL) {
          SeqToTransData(scene, td++, td2d++, tdsq++, seq, flag, SEQ_LEFTSEL);
          tot++;
        }
        if (flag & SEQ_RIGHTSEL) {
          SeqToTransData(scene, td++, td2d++, tdsq++, seq, flag, SEQ_RIGHTSEL);
          tot++;
        }
      }
      else {
        SeqToTransData(scene, td++, td2d++, tdsq++, seq, flag, SEL);
        tot++;
      }
    }
  }
  return tot;
}

static void free_transform_custom_data(TransCustomData *custom_data)
{
  if ((custom_data->data != nullptr) && custom_data->use_free) {
    TransSeq *ts = static_cast<TransSeq *>(custom_data->data);
    mem_free(ts->tdseq);
    mem_delete(ts);
    custom_data->data = nullptr;
  }
}

/* Canceled, need to update the strips display. */
static void seq_transform_cancel(TransInfo *t, dune::Span<Seq *> transformed_strips)
{
  List *seqlist = seq_active_seqlist_get(seq_editing_get(t->scene));

  for (Seq *seq : transformed_strips) {
    /* Handle pre-existing overlapping strips even when op is canceled.
     * Necessary for seq_ot_dup_move macro for example. */
    if (seq_transform_test_overlap(t->scene, seqbase, seq)) {
      seq_transform_seqbase_shuffle(seqbase, seq, t->scene);
    }
  }
}

static List *seqbase_active_get(const TransInfo *t)
{
  Editing *ed = seq_editing_get(t->scene);
  return seq_active_seqbase_get(ed);
}

static bool seq_transform_check_overlap(dune::Span<Seq *> transformed_strips)
{
  for (Seq *seq : transformed_strips) {
    if (seq->flag & SEQ_OVERLAP) {
      return true;
    }
  }
  return false;
}

static dune::VectorSet<Seq *> seq_transform_collection_from_transdata(
    TransDataContainer *tc)
{
  dune::VectorSet<Seq *> strips;
  TransData *td = tc->data;
  for (int a = 0; a < tc->data_len; a++, td++) {
    Seq *seq = ((TransDataSeq *)td->extra)->seq;
    strips.add(seq);
  }
  return strips;
}

static void freeSeqData(TransInfo *t, TransDataContainer *tc, TransCustomData *custom_data)
{
  Editing *ed = seq_editing_get(t->scene);
  if (ed == nullptr) {
    free_transform_custom_data(custom_data);
    return;
  }

  dune::VectorSet transformed_strips = seq_transform_collection_from_transdata(tc);
  seq_iter_set_expand(
      t->scene, seqbase_active_get(t), transformed_strips, seq_query_strip_effect_chain);

  for (Seq *seq : transformed_strips) {
    seq->flag &= ~SEQ_IGNORE_CHANNEL_LOCK;
  }

  if (t->state == TRANS_CANCEL) {
    seq_transform_cancel(t, transformed_strips);
    free_transform_custom_data(custom_data);
    return;
  }

  TransSeq *ts = static_cast<TransSeq *>(tc->custom.type.data);
  List *seqlistp = seqlist_active_get(t);
  Scene *scene = t->scene;
  const bool use_sync_markers = (((SpaceSeq *)t->area->spacedata.first)->flag &
                                 SEQ_MARKER_TRANS) != 0;
  if (seq_transform_check_overlap(transformed_strips)) {
    seq_transform_handle_overlap(
        scene, seqlistp, transformed_strips, ts->time_dep_strips, use_sync_markers);
  }

  graph_id_tag_update(&t->scene->id, ID_RECALC_SEQ_STRIPS);
  free_transform_custom_data(custom_data);
}

static dune::VectorSet<Seq *> query_sel_strips_no_handles(List *seqlist)
{
  dune::VectorSet<Seq *> strips;
  LIST_FOREACH (Seq *, seq, seqbase) {
    if ((seq->flag & SEL) != 0 && ((seq->flag & (SEQ_LEFTSEL | SEQ_RIGHTSEL)) == 0)) {
      strips.add(seq);
    }
  }
  return strips;
}

enum SeqInputSide {
  SEQ_INPUT_LEFT = -1,
  SEQ_INPUT_RIGHT = 1,
};

static Seq *effect_input_get(const Scene *scene, Seq *effect, SeqInputSide side)
{
  Seq *input = effect->seq1;
  if (effect->seq2 && (seq_time_left_handle_frame_get(scene, effect->seq2) -
                       seq_time_left_handle_frame_get(scene, effect->seq1)) *
                              side >
                          0)
  {
    input = effect->seq2;
  }
  return input;
}

static Seq *effect_base_input_get(const Scene *scene, Seq *effect, SeqInputSide side)
{
  Seq *input = effect, *seq_iter = effect;
  while (seq_iter != nullptr) {
    input = seq_iter;
    seq_iter = effect_input_get(scene, seq_iter, side);
  }
  return input;
}

/* Strips that aren't stime_dep_stripsel, but their position entirely depends on
 * transformed strips. This collection is used to offset anim */
static void query_time_dep_strips_strips(
    TransInfo *t, dune::VectorSet<Seq *> &time_dep_strips)
{
  List *seqlist = seqlist_active_get(t);

  /* Query dep strips where used strips do not have handles sel.
   * If all inputs of any effect even indirectly(through another effect) points to sel strip,
   * its position will change. */

  dune::VectorSet<Sequl *> strips_no_handles = query_sel_strips_no_handles(seqlist);
  time_dependent_strips.add_multiple(strips_no_handles);

  seq_iter_set_expand(t->scene, seqbase, strips_no_handles, SEQ_query_strip_effect_chain);
  bool strip_added = true;

  while (strip_added) {
    strip_added = false;

    for (Seq *seq : strips_no_handles) {
      if (time_dep_strips.contains(seq)) {
        continue; /* Strip alrdy in collection, skip it. */
      }

      /* If both seq1 and seq2 exist, both must be sel. */
      if (seq->seq1 && time_dep_strips.contains(seq->seq1)) {
        if (seq->seq2 && !time_dep_strips.contains(seq->seq2)) {
          continue;
        }
        strip_added = true;
        time_dep_strips.add(seq);
      }
    }
  }

  /* Query dep strips where used strips do have handles sel.
   * If any 2-input effect changes position bc handles were moved, anim should be offset.
   * W single input effect, it is less likely desirable to move anim. */
  dune::VectorSet sel_strips = seq_query_sel_strips(seqbase);
  seq_iter_set_expand(t->scene, seqbase, sel_strips, seq_query_strip_effect_chain);
  for (Seq *seq : sel_strips) {
    /* Check only 2 input effects. */
    if (seq->seq1 == nullptr || seq->seq2 == nullptr) {
      continue;
    }

    /* Find immediate base inputs(left and right side). */
    Seq *input_left = effect_base_input_get(t->scene, seq, SEQ_INPUT_LEFT);
    Seq *input_right = effect_base_input_get(t->scene, seq, SEQ_INPUT_RIGHT);

    if ((input_left->flag & SEQ_RIGHTSEL) != 0 && (input_right->flag & SEQ_LEFTSEL) != 0) {
      time_dep_strips.add(seq);
    }
  }

  /* Remove all non-effects. */
  time_dep_strips.remove_if(
      [&](Seq *seq) { return seq_transform_can_be_translated(seq); });
}

static void createTransSeqData(Cxt * /*C*/, TransInfo *t)
{
  Scene *scene = t->scene;
  Editing *ed = seq_editing_get(t->scene);
  TransData *td = nullptr;
  TransData2D *td2d = nullptr;
  TransDataSeq *tdsq = nullptr;
  TransSeq *ts = nullptr;

  int count = 0;

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  if (ed == nullptr) {
    tc->data_len = 0;
    return;
  }

  /* Disable cursor wrapping for edge pan. */
  if (t->mode == TFM_TRANSLATION) {
    t->flag |= T_NO_CURSOR_WRAP;
  }

  tc->custom.type.free_cb = freeSeqData;
  t->frame_side = transform_convert_frame_side_dir_get(t, float(scene->r.cfra));

  count = SeqTransCount(t, ed->seqbasep);

  /* alloc mem for data */
  tc->data_len = count;

  /* stop if trying to build list if nothing selected */
  if (count == 0) {
    return;
  }

  tc->custom.type.data = ts = mem_new<TransSeq>(__func__);
  tc->custom.type.use_free = true;
  td = tc->data = static_cast<TransData *>(
      mem_calloc(tc->data_len * sizeof(TransData), "TransSeq TransData"));
  td2d = tc->data_2d = static_cast<TransData2D *>(
      mem_calloc(tc->data_len * sizeof(TransData2D), "TransSeq TransData2D"));
  ts->tdseq = tdsq = static_cast<TransDataSeq *>(
      mem_calloc(tc->data_len * sizeof(TransDataSeq), "TransSeq TransDataSeq"));

  /* Custom data to enable edge panning during transformation. */
  ui_view2d_edge_pan_init(t->cxt,
                          &ts->edge_pan,
                          SEQ_EDGE_PAN_INSIDE_PAD,
                          SEQ_EDGE_PAN_OUTSIDE_PAD,
                          SEQ_EDGE_PAN_SPEED_RAMP,
                          SEQ_EDGE_PAN_MAX_SPEED,
                          SEQ_EDGE_PAN_DELAY,
                          SEQ_EDGE_PAN_ZOOM_INFLUENCE);
  ui_view2d_edge_pan_set_limits(&ts->edge_pan, -FLT_MAX, FLT_MAX, 1, MAXSEQ + 1);
  ts->init_v2d_cur = t->rgn->v2d.cur;

  /* loop 2: build transdata array */
  SeqToTransData_build(t, ed->seqbasep, td, td2d, tdsq);

  ts->sel_channel_range_min = MAXSEQ + 1;
  LIST_FOREACH (Seq *, seq, seq_active_get(ed)) {
    if ((seq->flag & SEL) != 0) {
      ts->sel_channel_range_min = min_ii(ts->sel_channel_range_min, seq->machine);
      ts->sel_channel_range_max = max_ii(ts->sel_channel_range_max, seq->machine);
    }
  }

  query_time_dep_strips_strips(t, ts->time_dep_strips);
}

/* UVs Transform Flush */
static void view2d_edge_pan_loc_compensate(TransInfo *t, float loc_in[2], float r_loc[2])
{
  TransSeq *ts = (TransSeq *)TRANS_DATA_CONTAINER_FIRST_SINGLE(t)->custom.type.data;

  /* Init and current view2D rects for additional transform due to view panning and zooming */
  const rctf *rect_src = &ts->init_v2d_cur;
  const rctf *rect_dst = &t->rgn->v2d.cur;

  if (t->options & CXT_VIEW2D_EDGE_PAN) {
    if (t->state == TRANS_CANCEL) {
      ui_view2d_edge_pan_cancel(t->cxt, &ts->edge_pan);
    }
    else {
      /* Edge panning fns expect win coords, mval is relative to rgn */
      const int xy[2] = {
          t->rgn->winrct.xmin + int(t->mval[0]),
          t->rgn->winrct.ymin + int(t->mval[1]),
      };
      ui_view2d_edge_pan_apply(t->cxt, &ts->edge_pan, xy);
    }
  }

  copy_v2_v2(r_loc, loc_in);
  /* Additional offset due to change in view2D rect. */
  lib_rctf_transform_pt_v(rect_dst, rect_src, r_loc, r_loc);
}

static void flushTransSeq(TransInfo *t)
{
  /* Editing null check alrdy done */
  List *seqlistp = seqbase_active_get(t);

  int a, new_frame, offset;

  TransData *td = nullptr;
  TransData2D *td2d = nullptr;
  TransDataSeq *tdsq = nullptr;
  Seq *seq;

  Scene *scene = t->scene;

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  /* Calc for offset anim of effects that change position w inputs.
   * Max(positive or negative) val is used, bc individual strips can be clamped.
   * Works in most scenarios but some edge cases.
   *
   * Solution: store effect position and calc real offset.
   * W (>5) effects in chain there is visible lag in strip position update
   * bc during recalc hierarchy is not taken into account. */
  int max_offset = 0;

  /* Flush to 2D vector from internally used 3D vector. */
  for (a = 0, td = tc->data, td2d = tc->data_2d; a < tc->data_len; a++, td++, td2d++) {
    tdsq = (TransDataSeq *)td->extra;
    seq = tdsq->seq;
    float loc[2];
    view2d_edge_pan_loc_compensate(t, td->loc, loc);
    new_frame = round_fl_to_int(loc[0]);

    switch (tdsq->sel_flag) {
      case SEL: {
        if (seq_transform_can_be_translated(seq)) {
          offset = new_frame - tdsq->start_offset - seq->start;
          seq_transform_translate(scene, seq, offset);
          if (abs(offset) > abs(max_offset)) {
            max_offset = offset;
          }
        }
        seq->machine = round_fl_to_int(loc[1]);
        CLAMP(seq->machine, 1, MAXSEQ);
        break;
      }
      case SEQ_LEFTSEL: { /* No vert transform. */
        int old_startdisp = seq_time_left_handle_frame_get(scene, seq);
        sea_time_left_handle_frame_set(t->scene, seq, new_frame);

        if (abs(seq_time_left_handle_frame_get(scene, seq) - old_startdisp) > abs(max_offset)) {
          max_offset = seq_time_left_handle_frame_get(scene, seq) - old_startdisp;
        }
        break;
      }
      case SEQ_RIGHTSEL: { /* No vertical transform. */
        int old_enddisp = seq_time_right_handle_frame_get(scene, seq);
        seq_time_right_handle_frame_set(t->scene, seq, new_frame);

        if (abs(seq_time_right_handle_frame_get(scene, seq) - old_enddisp) > abs(max_offset)) {
          max_offset = seq_time_right_handle_frame_get(scene, seq) - old_enddisp;
        }
        break;
      }
    }
  }

  TransSeq *ts = (TransSeq *)TRANS_DATA_CONTAINER_FIRST_SINGLE(t)->custom.type.data;

  /* Update anim for effects. */
  for (Seq *seq : ts->time_dep_strips) {
    seq_offset_animdata(t->scene, seq, max_offset);
  }

  /* need to do overlap check in a new loop or adjacent strips
   * will not be updated and we'll get false positives */
  dune::VectorSet transformed_strips = seq_transform_collection_from_transdata(tc);
  seq_iter_set_expand(
      t->scene, seqbase_active_get(t), transformed_strips, seq_query_strip_effect_chain);

  for (Seq *seq : transformed_strips) {
    /* test overlap, displays red outline */
    seq->flag &= ~SEQ_OVERLAP;
    if (seq_transform_test_overlap(scene, seqbasep, seq)) {
      seq->flag |= SEQ_OVERLAP;
    }
  }
}

static void recalcData_seq(TransInfo *t)
{
  TransData *td;
  int a;
  Seq *seq_prev = nullptr;

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);

  for (a = 0, td = tc->data; a < tc->data_len; a++, td++) {
    TransDataSeq *tdsq = (TransDataSeq *)td->extra;
    Seq *seq = tdsq->seq;

    if (seq != seq_prev) {
      seq_relations_invalidate_cache_composite(t->scene, seq);
    }

    seq_prev = seq;
  }

  graph_id_tag_update(&t->scene->id, ID_RECALC_SEQ_STRIPS);

  flushTransSeq(t);
}

/* Special After Transform Seq */
static void special_aftertrans_update_seq(Cxt * /*C*/, TransInfo *t)
{
  if (t->state == TRANS_CANCEL) {
    return;
  }
  /* freeSeqData in transform_conversions.c does this
   * keep here so the else at the end won't run... */
  SpaceSeq *sseq = (SpaceSeq *)t->area->spacedata.first;

  /* Marker transform, not especially nice but we may want to move markers
   * at the same time as strips in the Video Seq. */
  if (sseq->flag & SEQ_MARKER_TRANS) {
    /* can't use TFM_TIME_EXTEND
     * for some reason EXTEND is changed into TRANSLATE, so use frame_side instead */

    if (t->mode == TFM_SEQ_SLIDE) {
      if (t->frame_side == 'B') {
        ed_markers_post_apply_transform(
            &t->scene->markers, t->scene, TFM_TIME_TRANSLATE, t->vals_final[0], t->frame_side);
      }
    }
    else if (ELEM(t->frame_side, 'L', 'R')) {
      ed_markers_post_apply_transform(
          &t->scene->markers, t->scene, TFM_TIME_EXTEND, t->vals_final[0], t->frame_side);
    }
  }
}

void transform_convert_seq_channel_clamp(TransInfo *t, float r_val[2])
{
  const TransSeq *ts = (TransSeq *)TRANS_DATA_CONTAINER_FIRST_SINGLE(t)->custom.type.data;
  const int channel_offset = round_fl_to_int(r_val[1]);
  const int min_channel_after_transform = ts->sel_channel_range_min + channel_offset;
  const int max_channel_after_transform = ts->sel_channel_range_max + channel_offset;

  if (max_channel_after_transform > MAXSEQ) {
    r_val[1] -= max_channel_after_transform - MAXSEQ;
  }
  if (min_channel_after_transform < 1) {
    r_val[1] -= min_channel_after_transform - 1;
  }
}

TransConvertTypeInfo TransConvertType_Seq = {
    /*flags*/ (T_POINTS | T_2D_EDIT),
    /*create_trans_data*/ createTransSeqData,
    /*recalc_data*/ recalcData_seq,
    /*special_aftertrans_update*/ special_aftertrans_update_seq,
};
