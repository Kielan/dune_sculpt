#include "mem_guardedalloc.h"

#include "types_space.h"

#include "lib_list.h"
#include "lib_math_matrix.h"
#include "lib_math_vector.h"

#include "dune_cxt.hh"
#include "dune_report.h"

#include "seq_relations.hh"
#include "seq_retiming.hh"
#include "seq.hh"
#include "seq_time.hh"

#include "ed_keyframing.hh"

#include "ui_view2d.hh"

#include "api_access.hh"
#include "api_prototypes.h"

#include "transform.hh"
#include "transform_convert.hh"

/* Used for seq transform. */
typedef struct TransDataSeq {
  Seq *seq;
  int orig_timeline_frame;
  int key_index; /* Some actions may need to destroy original data, use index to access it. */
} TransDataSeq;

static TransData *SeqToTransData(const Scene *scene,
                                 Seq *seq,
                                 const SeqRetimingKey *key,
                                 TransData *td,
                                 TransData2D *td2d,
                                 TransDataSeq *tdseq)
{

  td2d->loc[0] = seq_retiming_key_timeline_frame_get(scene, seq, key);
  td2d->loc[1] = key->retiming_factor;
  td2d->loc2d = nullptr;
  td->loc = td2d->loc;
  copy_v3_v3(td->iloc, td->loc);
  copy_v3_v3(td->center, td->loc);
  memset(td->axismtx, 0, sizeof(td->axismtx));
  td->axismtx[2][2] = 1.0f;
  unit_m3(td->mtx);
  unit_m3(td->smtx);

  tdseq->seq = seq;
  tdseq->orig_timeline_frame = seq_retiming_key_timeline_frame_get(scene, seq, key);
  tdseq->key_index = seq_retiming_key_index_get(seq, key);

  td->extra = static_cast<void *>(tdseq);
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

static void createTransSeqRetimingData(Cxt * /*C*/, TransInfo *t)
{
  const Editing *ed = seq_editing_get(t->scene);
  if (ed == nullptr) {
    return;
  }

  const dune::Map sel = seq_retiming_sel_get(seq_editing_get(t->scene));

  if (sel.size() == 0) {
    return;
  }

  TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);
  tc->custom.type.free_cb = freeSeqData;

  tc->data_len = sel.size();
  tc->data = mem_cnew_array<TransData>(tc->data_len, "TransSeq TransData");
  tc->data_2d = mem_cnew_array<TransData2D>(tc->data_len, "TransSeq TransData2D");
  TransDataSeq *tdseq = mem_cnew_array<TransDataSeq>(tc->data_len, "TransSeq TransDataSeq");
  TransData *td = tc->data;
  TransData2D *td2d = tc->data_2d;

  for (auto item : selection.items()) {
    SeqToTransData(t->scene, item.value, item.key, td++, td2d++, tdseq++);
  }
}

static void seq_resize_speed_transition(const Scene *scene,
                                        const Seq *seq,
                                        SeqRetimingKey *key,
                                        const float loc)
{
  SeqRetimingKey *key_start = seq_retiming_transition_start_get(key);
  float offset;
  if (key == key_start) {
    offset = loc - seq_retiming_key_timeline_frame_get(scene, seq, key);
  }
  else {
    offset = seq_retiming_key_timeline_frame_get(scene, seq, key) - loc;
  }
  seq_retiming_offset_transition_key(scene, seq, key_start, offset);
}

static void recalcData_seq_retiming(TransInfo *t)
{
  const TransDataContainer *tc = TRANS_DATA_CONTAINER_FIRST_SINGLE(t);
  const TransData *td = nullptr;
  const TransData2D *td2d = nullptr;
  int i;

  for (i = 0, td = tc->data, td2d = tc->data_2d; i < tc->data_len; i++, td++, td2d++) {
    const TransDataSeq *tdseq = static_cast<TransDataSeq *>(td->extra);
    Seq *seq = tdseq->seq;

    /* Calc translation. */
    const dune::MutableSpan keys = seq_retiming_keys_get(seq);
    SeqRetimingKey *key = &keys[tdseq->key_index];

    if (seq_retiming_key_is_transition_type(key) &&
        !seq_retiming_sel_has_whole_transition(seq_editing_get(t->scene), key))
    {
      seq_resize_speed_transition(t->scene, seq, key, td2d->loc[0]);
    }
    else {
      seq_retiming_key_timeline_frame_set(t->scene, seq, key, td2d->loc[0]);
    }

    seq_relations_invalidate_cache_preprocessed(t->scene, seq);
  }
}

TransConvertTypeInfo TransConvertType_SequencerRetiming = {
    /*flags*/ (T_POINTS | T_2D_EDIT),
    /*create_trans_data*/ createTransSeqRetimingData,
    /*recalc_data*/ recalcData_sequencer_retiming,
};
