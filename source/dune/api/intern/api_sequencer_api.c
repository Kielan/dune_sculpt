#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types_scene.h"
#include "types_sequence.h"

#include "lib_utildefines.h"

#include "api_access.h"
#include "api_define.h"

#include "seq_edit.h"

#include "api_internal.h"

#ifdef API_RUNTIME

// #include "types_anim_types.h"
#  include "types_image.h"
#  include "types_mask.h"
#  include "types_sound.h"

#  include "lib_path_util.h" /* #BLI_path_split_dir_file */

#  include "dune_image.h"
#  include "dune_mask.h"
#  include "dune_movieclip.h"

#  include "dune_report.h"
#  include "dune_sound.h"

#  include "imbuf.h"
#  include "imbuf_types.h"

#  include "seq_add.h"
#  include "seq_edit.h"
#  include "seq_effects.h"
#  include "seq_relations.h"
#  include "seq_render.h"
#  include "seq_retiming.h"
#  include "seq_sequencer.h"
#  include "seq_time.h"

#  include "wm_api.h"

static StripElem *api_seq_strip_elem_from_frame(Id *id, Seq *self, int timeline_frame)
{
  Scene *scene = (Scene *)id;
  return seq_render_give_stripelem(scene, self, timeline_frame);
}

static void api_seq_swap_internal(Id *id,
                                  Seq *seq_self,
                                  ReportList *reports,
                                  Seq *seq_other)
{
  const char *error_msg;
  Scene *scene = (Scene *)id;

  if (seq_edit_seq_swap(scene, seq_self, seq_other, &error_msg) == false) {
    dune_report(reports, RPT_ERROR, error_msg);
  }
}

static void api_seq_move_strip_to_meta(
    Id *id, Seq *seq_self, Main *main, ReportList *reports, Seq *meta_dst)
{
  Scene *scene = (Scene *)id;
  const char *error_msg;

  /* Move strip to meta. */
  if (!seq_edit_move_strip_to_meta(scene, seq_self, meta_dst, &error_msg)) {
    dune_report(reports, RPT_ERROR, error_msg);
  }

  /* Update depsgraph. */
  grap_relations_tag_update(main);
  graph_id_tag_update(&scene->id, ID_RECALC_SEQ_STRIPS);

  seq_lookup_tag(scene, SEQ_LOOKUP_TAG_INVALID);

  wm_main_add_notifier(NC_SCENE | ND_SEQ, scene);
}

static Seq *api_seq_split(
    Id *id, Seq *seq, Main *main, ReportList *reports, int frame, int split_method)
{
  Scene *scene = (Scene *)id;
  List *seqbase = seq_get_seqbase_by_seq(scene, seq);

  const char *error_msg = NULL;
  Seq *r_seq = seq_edit_strip_split(
      main, scene, seqbase, seq, frame, split_method, &error_msg);
  if (error_msg != NULL) {
    dune_report(reports, RPT_ERROR, error_msg);
  }

  /* Update depsgraph. */
  graph_relations_tag_update(main);
  graph_id_tag_update(&scene->id, ID_RECALC_SEQ_STRIPS);

  wm_main_add_notifier(NC_SCENE | ND_SEQ, scene);

  return r_seq;
}

static Seq *api_seq_parent_meta(Id *id, Seq *seq_self)
{
  Scene *scene = (Scene *)id;
  Editing *ed = seq_editing_get(scene);

  return seq_find_metastrip_by_sequence(&ed->seqbase, NULL, seq_self);
}

static Seq *api_seq_new_clip(Id *id,
                                        ListBase *seqbase,
                                        Main *bmain,
                                        const char *name,
                                        MovieClip *clip,
                                        int channel,
                                        int frame_start)
{
  Scene *scene = (Scene *)id;
  SeqLoadData load_data;
  seq_add_load_data_init(&load_data, name, NULL, frame_start, channel);
  load_data.clip = clip;
  Seq *seq = seq_add_movieclip_strip(scene, seqbase, &load_data);

  graph_relations_tag_update(bmain);
  graph_id_tag_update(&scene->id, ID_RECALC_SEQ_STRIPS);
  wm_main_add_notifier(NC_SCENE | ND_SEQ, scene);

  return seq;
}

static Seq *api_seq_editing_new_clip(Id *id,
                                     Editing *ed,
                                     Main *main,
                                     const char *name,
                                     MovieClip *clip,
                                     int channel,
                                     int frame_start)
{
  return api_seq_new_clip(id, &ed->seqbase, main, name, clip, channel, frame_start);
}

static Seq *api_seq_meta_new_clip(Id *id,
                                  Seq *seq,
                                  Main *main,
                                  const char *name,
                                  MovieClip *clip,
                                  int channel,
                                  int frame_start)
{
  return api_seq_new_clip(id, &seq->seqbase, main, name, clip, channel, frame_start);
}

static Seq *api_seq_new_mask(Id *id,
                                   List *seqbase,
                                   Main *main,
                                   const char *name,
                                   Mask *mask,
                                   int channel,
                                   int frame_start)
{
  Scene *scene = (Scene *)id;
  SeqLoadData load_data;
  seq_add_load_data_init(&load_data, name, NULL, frame_start, channel);
  load_data.mask = mask;
  Seq *seq = seq_add_mask_strip(scene, seqbase, &load_data);

  graph_relations_tag_update(main);
  graph_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  wm_main_add_notifier(NC_SCENE | ND_SEQUENCER, scene);

  return seq;
}
static Seq *api_seq_editing_new_mask(
    Id *id, Editing *ed, Main *main, const char *name, Mask *mask, int channel, int frame_start)
{
  return api_seq_new_mask(id, &ed->seqbase, main, name, mask, channel, frame_start);
}

static Seq *api_seq_meta_new_mask(
    Id *id, Seq *seq, Main *main, const char *name, Mask *mask, int channel, int frame_start)
{
  return api_seq_new_mask(id, &seq->seqbase, main, name, mask, channel, frame_start);
}

static Seq *api_seq_new_scene(Id *id,
                              List *seqbase,
                              Main *main,
                              const char *name,
                              Scene *sce_seq,
                              int channel,
                              int frame_start)
{
  Scene *scene = (Scene *)id;
  SeqLoadData load_data;
  seq_add_load_data_init(&load_data, name, NULL, frame_start, channel);
  load_data.scene = sce_seq;
  Seq *seq = seq_add_scene_strip(scene, seqbase, &load_data);

  graph_relations_tag_update(main);
  graph_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  wm_main_add_notifier(NC_SCENE | ND_SEQUENCER, scene);

  return seq;
}

static Seq *api_seq_editing_new_scene(Id *id,
                                      Editing *ed,
                                      Main *main,
                                      const char *name,
                                      Scene *sce_seq,
                                      int channel,
                                      int frame_start)
{
  return api_seq_new_scene(id, &ed->seqbase, main, name, sce_seq, channel, frame_start);
}

static Seq *api_seq_meta_new_scene(Id *id,
                                   Seq *seq,
                                   Main *main,
                                   const char *name,
                                   Scene *sce_seq,
                                   int channel,
                                   int frame_start)
{
  return api_seq_new_scene(id, &seq->seqbase, main, name, sce_seq, channel, frame_start);
}

static Seq *api_seq_new_image(Id *id,
                              List *seqbase,
                              Main *main,
                              ReportList *UNUSED(reports)
                              const char *name,
                              const char *file,
                              int channel,
                              int frame_start,
                              int fit_method)
{
  Scene *scene = (Scene *)id;

  SeqLoadData load_data;
  seq_add_load_data_init(&load_data, name, file, frame_start, channel);
  load_data.image.len = 1;
  load_data.fit_method = fit_method;
  Seq *seq = seq_add_image_strip(main, scene, seqbase, &load_data);

  char dir[FILE_MAX], filename[FILE_MAX];
  lib_path_split_dir_file(file, dir, sizeof(dir), filename, sizeof(filename));
  seq_add_image_set_directory(seq, dir);
  seq_add_image_load_file(scene, seq, 0, filename);
  seq_add_image_init_alpha_mode(seq);

  graph_relations_tag_update(main);
  graph_id_tag_update(&scene->id, ID_RECALC_SEQ_STRIPS);
  wm_main_add_notifier(NC_SCENE | ND_SEQ, scene);

  return seq;
}

static Seq *api_seq_editing_new_image(Id *id,
                                      Editing *ed,
                                      Main *main,
                                      ReportList *reports,
                                      const char *name,
                                      const char *file,
                                      int channel,
                                      int frame_start,
                                      int fit_method)
{
  return api_seq_new_image(
      id, &ed->seqbase, main, reports, name, file, channel, frame_start, fit_method);
}

static Seq *api_seq_meta_new_image(Id *id,
                                   Seq *seq,
                                   Main *main,
                                   ReportList *reports,
                                   const char *name,
                                   const char *file,
                                   int channel,
                                   int frame_start,
                                   int fit_method)
{
  return api_seq_new_image(
      id, &seq->seqbase, main, reports, name, file, channel, frame_start, fit_method);
}

static Seq *api_seq_new_movie(Id *id,
                              List *seqbase,
                              Main *main,
                              const char *name
                              const char *file,
                              int channel,
                              int frame_start,
                              int fit_method)
{
  Scene *scene = (Scene *)id;
  SeqLoadData load_data;
  seq_add_load_data_init(&load_data, name, file, frame_start, channel);
  load_data.fit_method = fit_method;
  load_data.allow_invalid_file = true;
  Seq *seq = seq_add_movie_strip(main, scene, seqbase, &load_data);

  graph_relations_tag_update(main);
  graph_id_tag_update(&scene->id, ID_RECALC_SEQ_STRIPS);
  wm_main_add_notifier(NC_SCENE | ND_SEQU, scene);

  return seq;
}

static Seq *api_seq_editing_new_movie(Id *id,
                                      Editing *ed,
                                      Main *main,
                                      const char *name,
                                      const char *file,
                                      int channel,
                                      int frame_start,
                                      int fit_method)
{
  return api_seq_new_movie(
      id, &ed->seqbase, main, name, file, channel, frame_start, fit_method);
}

static Seq *api_seq_meta_new_movie(Id *id,
                                   Seq *seq,
                                   Main *main,
                                   const char *name,
                                   const char *file,
                                   int channel,
                                   int frame_start,
                                   int fit_method)
{
  return api_seq_new_movie(
      id, &seq->seqbase, main, name, file, channel, frame_start, fit_method);
}

#  ifdef WITH_AUDASPACE
static Seq *api_seq_new_sound(Id *id,
                              List *seqbase,
                              Main *main,
                              ReportList *reports,
                              const char *name,
                              const char *file,
                              int channel,
                              int frame_start)
{
  Scene *scene = (Scene *)id;
  SeqLoadData load_data;
  seq_add_load_data_init(&load_data, name, file, frame_start, channel);
  load_data.allow_invalid_file = true;
  Seq *seq = seq_add_sound_strip(main, scene, seqbase, &load_data);

  if (seq == NULL) {
    dune_report(reports, RPT_ERROR, "Sequences.new_sound: unable to open sound file");
    return NULL;
  }

  graph_relations_tag_update(main);
  graph_id_tag_update(&scene->id, ID_RECALC_SEQ_STRIPS);
  wm_main_add_notifier(NC_SCENE | ND_SEQ, scene);

  return seq;
}
#  else  /* WITH_AUDASPACE */
static Seq *api_seq_new_sound(Id *UNUSED(id),
                              List *UNUSED(seqbase),
                              Main *UNUSED(main),
                              ReportList *reports,
                              const char *UNUSED(name),
                              const char *UNUSED(file),
                              int UNUSED(channel),
                              int UNUSED(frame_start))
{
  dune_report(reports, RPT_ERROR, "Dune compiled without Audaspace support");
  return NULL;
}
#  endif /* WITH_AUDASPACE */

static Seq *api_seq_editing_new_sound(Id *id,
                                      Editing *ed,
                                      Main *main,
                                      ReportList *reports,
                                      const char *name,
                                      const char *file,
                                      int channel,
                                      int frame_start)
{
  return api_seq_new_sound(
      id, &ed->seqbase, main, reports, name, file, channel, frame_start);
}

static Seq *api_seq_meta_new_sound(Id *id,
                                   Seq *seq,
                                   Main *main,
                                   ReportList *reports,
                                   const char *name,
                                   const char *file,
                                   int channel,
                                   int frame_start)
{
  return api_seq_new_sound(
      id, &seq->seqbase, main, reports, name, file, channel, frame_start);
}

/* Meta sequence
 * Possibility to create an empty meta to avoid plenty of meta toggling
 * Created meta have a length equal to 1, must be set through the API. */
static Seq *api_seq_new_m(
    Id *id, List *seqbase, const char *name, int channel, int frame_start)
{
  Scene *scene = (Scene *)id;
  SeqLoadData load_data;
  seq_add_load_data_init(&load_data, name, NULL, frame_start, channel);
  Seq *seqm = seq_add_meta_strip(scene, seqbase, &load_data);

  return seqm;
}

static Seq *api_seq_editing_new_meta(
    Id *id, Editing *ed, const char *name, int channel, int frame_start)
{
  return api_seq_new_meta(id, &ed->seqbase, name, channel, frame_start);
}

static Seq *api_seq_meta_new_meta(
    Id *id, Seq *seq, const char *name, int channel, int frame_start)
{
  return api_seq_new_meta(id, &seq->seqbase, name, channel, frame_start);
}

static Seq *api_seq_new_effect(Id *id,
                               List *seqbase,
                               ReportList *reports,
                               const char *name,
                               int type,
                               int channel,
                               int frame_start,
                               int frame_end,
                               Seq *seq1,
                               Seq *seq2,
                               Seq *seq3)
{
  Scene *scene = (Scene *)id;
  Seq *seq;
  const int num_inputs = seq_effect_get_num_inputs(type);

  switch (num_inputs) {
    case 0:
      if (frame_end <= frame_start) {
        dune_report(reports, RPT_ERROR, "Seq.new_effect: end frame not set");
        return NULL;
      }
      break;
    case 1:
      if (seq1 == NULL) {
        dune_report(reports, RPT_ERROR, "Seq.new_effect: effect takes 1 input sequence");
        return NULL;
      }
      break;
    case 2:
      if (seq1 == NULL || seq2 == NULL) {
        dune_report(reports, RPT_ERROR, "Seq.new_effect: effect takes 2 input sequences");
        return NULL;
      }
      break;
    case 3:
      if (seq1 == NULL || seq2 == NULL || seq3 == NULL) {
        dune_report(reports, RPT_ERROR, "Seq.new_effect: effect takes 3 input sequences");
        return NULL;
      }
      break;
    default:
      dune_reportf(
          reports,
          RPT_ERROR,
          "Seq.new_effect: effect expects more than 3 inputs (%d, should never happen!)",
          num_inputs);
      return NULL;
  }

  SeqLoadData load_data;
  seq_add_load_data_init(&load_data, name, NULL, frame_start, channel);
  load_data.effect.end_frame = frame_end;
  load_data.effect.type = type;
  load_data.effect.seq1 = seq1;
  load_data.effect.seq2 = seq2;
  load_data.effect.seq3 = seq3;
  seq = seq_add_effect_strip(scene, seqbase, &load_data);

  graph_id_tag_update(&scene->id, ID_RECALC_SEQ_STRIPS);
  wm_main_add_notifier(NC_SCENE | ND_SEQUENCER, scene);

  return seq;
};
static Seq *api_seq_editing_new_effect(Id *id,
                                       Editing *ed,
                                       ReportList *reports,
                                       const char *name,
                                       int type,
                                       int channel,
                                       int frame_start,
                                       int frame_end,
                                       Seq *seq1,
                                       Seq *seq2,
                                       Seq *seq3)
{
  return api_seq_new_effect(
      id, &ed->seqbase, reports, name, type, channel, frame_start, frame_end, seq1, seq2, seq3);
}

static Seq *api_seq_meta_new_effect(Id *id,
                                    Seq *seq,
                                               ReportList *reports,
                                               const char *name,
                                               int type,
                                               int channel,
                                               int frame_start,
                                               int frame_end,
                                               Sequence *seq1,
                                               Sequence *seq2,
                                               Sequence *seq3)
{
  return api_seq_new_effect(
      id, &seq->seqbase, reports, name, type, channel, frame_start, frame_end, seq1, seq2, seq3);
}

static void api_seq_remove(
    Id *id, List *seqbase, Main *main, ReportList *reports, ApiPtr *seq_ptr)
{
  Seq *seq = seq_ptr->data;
  Scene *scene = (Scene *)id;

  if (lib_findindex(seqbase, seq) == -1) {
    dune_reportf(
        reports, RPT_ERROR, "Sequence '%s' not in scene '%s'", seq->name + 2, scene->id.name + 2);
    return;
  }

  seq_edit_flag_for_removal(scene, seqbase, seq);
  seq_edit_remove_flagged_seq(scene, seqbase);
  API_PTR_INVALIDATE(seq_ptr);

  graph_relations_tag_update(main);
  graph_id_tag_update(&scene->id, ID_RECALC_SEQ_STRIPS);
  wm_main_add_notifier(NC_SCENE | ND_SEQU, scene);
}

static void api_seq_editing_remove(
    Id *id, Editing *ed, Main *main, ReportList *reports, ApiPtr *seq_ptr)
{
  api_seq_remove(id, &ed->seqbase, main, reports, seq_ptr);
}

static void api_seq_meta_remove(
    Id *id, Seq *seq, Main *main, ReportList *reports, ApiPtr *seq_ptr)
{
  api_seq_remove(id, &seq->seqbase, main, reports, seq_ptr);
}

static StripElem *api_SequenceElements_append(Id *id, Sequence *seq, const char *filename)
{
  Scene *scene = (Scene *)id;
  StripElem *se;

  seq->strip->stripdata = se = mem_reallocn(seq->strip->stripdata,
                                            sizeof(StripElem) * (seq->len + 1));
  se += seq->len;
  STRNCPY(se->name, filename);
  seq->len++;

  seq->flag &= ~SEQ_SINGLE_FRAME_CONTENT;

  wm_main_add_notifier(NC_SCENE | ND_SEQUENCER, scene);

  return se;
}

static void api_SequenceElements_pop(ID *id, Sequence *seq, ReportList *reports, int index)
{
  Scene *scene = (Scene *)id;
  StripElem *new_seq, *se;

  if (seq->len == 1) {
    dune_report(reports, RPT_ERROR, "SequenceElements.pop: cannot pop the last element");
    return;
  }

  /* python style negative indexing */
  if (index < 0) {
    index += seq->len;
  }

  if (seq->len <= index || index < 0) {
    dune_report(reports, RPT_ERROR, "SequenceElements.pop: index out of range");
    return;
  }

  new_seq = mem_callocn(sizeof(StripElem) * (seq->len - 1), "SequenceElements_pop");
  seq->len--;

  if (seq->len == 1) {
    seq->flag |= SEQ_SINGLE_FRAME_CONTENT;
  }

  se = seq->strip->stripdata;
  if (index > 0) {
    memcpy(new_seq, se, sizeof(StripElem) * index);
  }

  if (index < seq->len) {
    memcpy(&new_seq[index], &se[index + 1], sizeof(StripElem) * (seq->len - index));
  }

  mem_freen(seq->strip->stripdata);
  seq->strip->stripdata = new_seq;

  wm_main_add_notifier(NC_SCENE | ND_SEQ, scene);
}

static void api_seq_invalidate_cache_apifn(Id *id, Seq *self, int type)
{
  switch (type) {
    case SEQ_CACHE_STORE_RAW:
      seq_relations_invalidate_cache_raw((Scene *)id, self);
      break;
    case SEQ_CACHE_STORE_PREPROCESSED:
      seq_relations_invalidate_cache_preprocessed((Scene *)id, self);
      break;
    case SEQ_CACHE_STORE_COMPOSITE:
      seq_relations_invalidate_cache_composite((Scene *)id, self);
      break;
  }
}

static SeqRetimingHandle *api_seq_retiming_handles_add(Id *id,
                                                       Seq *seq,
                                                       int timeline_frame)
{
  Scene *scene = (Scene *)id;

  SeqRetimingHandle *handle = seq_retiming_add_handle(scene, seq, timeline_frame);

  seq_relations_invalidate_cache_raw(scene, seq);
  wm_main_add_notifier(NC_SCENE | ND_SEQ, NULL);
  return handle;
}

static void api_seq_retiming_handles_reset(Id *id, Seq *seq)
{
  Scene *scene = (Scene *)id;

  seq_retiming_data_clear(seq);

  seq_relations_invalidate_cache_raw(scene, seq);
  wm_main_add_notifier(NC_SCENE | ND_SEQ, NULL);
}

#else

void api_seq_strip(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  static const EnumPropItem seq_cahce_type_items[] = {
      {SEQ_CACHE_STORE_RAW, "RAW", 0, "Raw", ""},
      {SEQ_CACHE_STORE_PREPROCESSED, "PREPROCESSED", 0, "Preprocessed", ""},
      {SEQ_CACHE_STORE_COMPOSITE, "COMPOSITE", 0, "Composite", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem seq_split_method_items[] = {
      {SEQ_SPLIT_SOFT, "SOFT", 0, "Soft", ""},
      {SEQ_SPLIT_HARD, "HARD", 0, "Hard", ""},
      {0, NULL, 0, NULL, NULL},
  };

  fn = api_def_fn(sapi, "strip_elem_from_frame", "api_seq_strip_elem_from_frame");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Return the strip element from a given frame or None");
  parm = api_def_int(fn,
                     "frame",
                     0,
                     -MAXFRAME,
                     MAXFRAME,
                     "Frame",
                     "The frame to get the strip element from",
                     -MAXFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_fn_return(
      fn,
      api_def_ptr(fn, "elem", "SequenceElement", "", "strip element of the current frame"));

  fn = api_def_fn(sapi, "swap", "api_seq_swap_internal");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_SELF_ID);
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "other", "Sequence", "Other", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  fn = api_def_fn(sapi, "move_to_meta", "api_seq_move_strip_to_meta");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_SELF_ID | FN_USE_MAIN);
  parm = api_def_ptr(fn,
                     "meta_sequence",
                     "Sequence",
                     "Destination Meta Sequence",
                     "Meta to move the strip into");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  fn = api_def_fn(sapi, "parent_meta", "api_seq_parent_meta");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_function_ui_description(func, "Parent meta");
  /* return type */
  parm = api_def_ptr(fn, "sequence", "Sequence", "", "Parent Meta");
  api_def_fn_return(fn, parm);

  func = api_def_fn(sapi, "invalidate_cache", "api_Sequence_invalidate_cache_rnafunc");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn,
                                  "Invalidate cached images for strip and all dependent strips");
  parm = api_def_enum(fn, "type", seq_cahce_type_items, 0, "Type", "Cache Type");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  fn = api_def_fn(sapi, "split", "api_seq_split");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_SELF_ID | FN_USE_MAIN);
  api_def_fn_ui_description(fn, "Split Sequence");
  parm = api_def_int(
      fn, "frame", 0, INT_MIN, INT_MAX, "", "Frame where to split the strip", INT_MIN, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn, "split_method", seq_split_method_items, 0, "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  /* Return type. */
  parm = api_def_ptr(fn, "sequence", "Sequence", "", "Right side Sequence");
  api_def_fn_return(fn, parm);
}

void api_seq_elements(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *parm;
  ApiFn *fn;

  api_def_prop_sapi(cprop, "SequenceElements");
  sapi = api_def_struct(dapi, "SequenceElements", NULL);
  api_def_struct_stype(sapi, "Sequence");
  api_def_struct_ui_text(sapi, "SequenceElements", "Collection of SequenceElement");

  fn = api_def_fn(sapi, "append", "rna_SequenceElements_append");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Push an image from ImageSequence.directory");
  parm = api_def_string(fn, "filename", "File", 0, "", "Filepath to image");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "elem", "SequenceElement", "", "New SequenceElement");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "pop", "api_SequenceElements_pop");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Pop an image off the collection");
  parm = api_def_int(
      fn, "index", -1, INT_MIN, INT_MAX, "", "Index of image to remove", INT_MIN, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_seq_retiming_handles(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  api_def_prop_sapi(cprop, "RetimingHandles");
  sapi = api_def_struct(dapi, "RetimingHandles", NULL);
  api_def_struct_stype(sapi, "Sequence");
  api_def_struct_ui_text(sapi, "RetimingHandles", "Collection of RetimingHandle");

  ApiFn *fn = api_def_fn(sapi, "add", "api_seq_retiming_handles_add");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_int(
      fn, "timeline_frame", 0, -MAXFRAME, MAXFRAME, "Timeline Frame", "", -MAXFRAME, MAXFRAME);
  api_def_fn_ui_description(fn, "Add retiming handle");
  /* return type */
  ApiProp *parm = api_def_ptr(
      fn, "retiming_handle", "RetimingHandle", "", "New RetimingHandle");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "reset", "api_seq_retiming_handles_reset");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  api_def_fn_ui_description(fn, "Remove all retiming handles");
}

void api_seq(DuneApi *dapi, ApiProp *cprop, const bool metastrip)
{
  ApiStruct *sapi;
  ApiProp *parm;
  ApiFn *fn;

  static const EnumPropItem seq_effect_items[] = {
      {SEQ_TYPE_CROSS, "CROSS", 0, "Cross", ""},
      {SEQ_TYPE_ADD, "ADD", 0, "Add", ""},
      {SEQ_TYPE_SUB, "SUBTRACT", 0, "Subtract", ""},
      {SEQ_TYPE_ALPHAOVER, "ALPHA_OVER", 0, "Alpha Over", ""},
      {SEQ_TYPE_ALPHAUNDER, "ALPHA_UNDER", 0, "Alpha Under", ""},
      {SEQ_TYPE_GAMCROSS, "GAMMA_CROSS", 0, "Gamma Cross", ""},
      {SEQ_TYPE_MUL, "MULTIPLY", 0, "Multiply", ""},
      {SEQ_TYPE_OVERDROP, "OVER_DROP", 0, "Over Drop", ""},
      {SEQ_TYPE_WIPE, "WIPE", 0, "Wipe", ""},
      {SEQ_TYPE_GLOW, "GLOW", 0, "Glow", ""},
      {SEQ_TYPE_TRANSFORM, "TRANSFORM", 0, "Transform", ""},
      {SEQ_TYPE_COLOR, "COLOR", 0, "Color", ""},
      {SEQ_TYPE_SPEED, "SPEED", 0, "Speed", ""},
      {SEQ_TYPE_MULTICAM, "MULTICAM", 0, "Multicam Selector", ""},
      {SEQ_TYPE_ADJUSTMENT, "ADJUSTMENT", 0, "Adjustment Layer", ""},
      {SEQ_TYPE_GAUSSIAN_BLUR, "GAUSSIAN_BLUR", 0, "Gaussian Blur", ""},
      {SEQ_TYPE_TEXT, "TEXT", 0, "Text", ""},
      {SEQ_TYPE_COLORMIX, "COLORMIX", 0, "Color Mix", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem scale_fit_methods[] = {
      {SEQ_SCALE_TO_FIT, "FIT", 0, "Scale to Fit", "Scale image so fits in preview"},
      {SEQ_SCALE_TO_FILL,
       "FILL",
       0,
       "Scale to Fill",
       "Scale image so it fills preview completely"},
      {SEQ_STRETCH_TO_FILL, "STRETCH", 0, "Stretch to Fill", "Stretch image so it fills preview"},
      {SEQ_USE_ORIGINAL_SIZE, "ORIGINAL", 0, "Use Original Size", "Don't scale the image"},
      {0, NULL, 0, NULL, NULL},
  };

  const char *new_clip_fn_name = "api_seq_editing_new_clip";
  const char *new_mask_fn_name = "api_seq_editing_new_mask";
  const char *new_scene_fn_name = "api_seq_editing_new_scene";
  const char *new_image_fn_name = "api_seq_editing_new_image";
  const char *new_movie_fn_name = "api_seq_editing_new_movie";
  const char *new_sound_fn_name = "api_seq_editing_new_sound";
  const char *new_meta_fn_name = "api_seq_editing_new_meta";
  const char *new_effect_fn_name = "api_seq_editing_new_effect";
  const char *remove_fn_name = "api_seq_editing_remove";

  if (metastrip) {
    api_def_prop_sapi(cprop, "SeqMeta");
    sapi = api_def_struct(dapi, "SeqMeta", NULL);
    api_def_struct_stype(sapi, "Seq");

    new_clip_fn_name = "api_seq_meta_new_clip";
    new_mask_fn_name = "api_Seq_meta_new_mask";
    new_scene_fn_name = "api_Seq_meta_new_scene";
    new_image_fn_name = "api_Seq_meta_new_image";
    new_movie_fn_name = "api_Seq_meta_new_movie";
    new_sound_fn_name = "api_Seq_meta_new_sound";
    new_meta_fn_name = "api_Sequ_meta_new_meta";
    new_effect_fn_name = "api_Sequ_meta_new_effect";
    remove_fn_name = "api_Seq_meta_remove";
  }
  else {
    api_def_prop_sapi(cprop, "SeqTopLevel");
    sapi = api_def_struct(dapi, "SeqTopLevel", NULL);
    api_def_struct_stype(sapi, "Editing");
  }

  api_def_struct_ui_text(sapi, "Seq", "Collection of Sequences");

  fn = api_def_fn(sapi, "new_clip", new_clip_fn_name);
  api_def_fn_flag(fn, FN_USE_SELF_ID | FUNC_USE_MAIN);
  api_def_fn_ui_description(fn, "Add a new movie clip sequence");
  parm = api_def_string(fn, "name", "Name", 0, "", "Name for the new sequence");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "clip", "MovieClip", "", "Movie clip to add");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_int(
      fn, "channel", 0, 1, MAXSEQ, "Channel", "The channel for the new sequence", 1, MAXSEQ);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn,
                     "frame_start",
                     0,
                     -MAXFRAME,
                     MAXFRAME,
                     "",
                     "The start frame for the new sequence",
                     -MAXFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "sequence", "Sequence", "", "New Sequence");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_mask", new_mask_fn_name);
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_MAIN);
  api_def_fn_ui_description(fn, "Add a new mask sequence");
  parm = api_def_string(fn, "name", "Name", 0, "", "Name for the new sequence");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "mask", "Mask", "", "Mask to add");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_int(
      fn, "channel", 0, 1, MAXSEQ, "Channel", "The channel for the new sequence", 1, MAXSEQ);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn,
                     "frame_start",
                     0,
                     -MAXFRAME,
                     MAXFRAME,
                     "",
                     "The start frame for the new sequence",
                     -MAXFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "sequence", "Sequence", "", "New Sequence");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_scene", new_scene_fn_name);
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_MAIN);
  api_def_fn_ui_description(fn, "Add a new scene sequence");
  parm = api_def_string(fn, "name", "Name", 0, "", "Name for the new sequence");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "scene", "Scene", "", "Scene to add
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_int(
      fn, "channel", 0, 1, MAXSEQ, "Channel", "The channel for the new sequence", 1, MAXSEQ);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn,
                     "frame_start",
                     0,
                     -MAXFRAME,
                     MAXFRAME,
                     "",
                     "The start frame for the new sequence",
                     -MAXFRAME,
                     MAXFRAME);
  api_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "sequence", "Sequence", "", "New Sequence");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_image", new_image_fn_name);
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_SELF_ID | FUNC_USE_MAIN);
  api_def_fn_ui_description(fn, "Add a new image sequence");
  parm = api_def_string(fn, "name", "Name", 0, "", "Name for the new sequence");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "filepath", "File", 0, "", "Filepath to image");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(
      fn, "channel", 0, 1, MAXSEQ, "Channel", "The channel for the new sequence", 1, MAXSEQ);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn,
                     "frame_start",
                     0,
                     -MAXFRAME,
                     MAXFRAME,
                     "",
                     "The start frame for the new sequence",
                     -MAXFRAME,
                     MAXFRAME);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(
      func, "fit_method", scale_fit_methods, SEQ_USE_ORIGINAL_SIZE, "Image Fit Method", NULL);
  api_def_param_flags(parm, 0, PARM_PYFUNC_OPTIONAL);
  /* return type */
  parm = api_def_ptr(fn, "sequence", "Sequence", "", "New Sequence");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_movie", new_movie_fn_name
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_MAIN);
  api_def_fn_ui_description(fn, "Add a new movie sequence");
  parm = api_def_string(fn, "name", "Name", 0, "", "Name for the new sequence");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "filepath", "File", 0, "", "Filepath to movie");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(
      fn, "channel", 0, 1, MAXSEQ, "Channel", "The channel for the new sequence", 1, MAXSEQ);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(func,
                     "frame_start",
                     0,
                     -MAXFRAME,
                     MAXFRAME,
                     "",
                     "The start frame for the new sequence",
                     -MAXFRAME,
                     MAXFRAME);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(
      func, "fit_method", scale_fit_methods, SEQ_USE_ORIGINAL_SIZE, "Image Fit Method", NULL);
  RNA_def_parameter_flags(parm, 0, PARM_PYFUNC_OPTIONAL);
  /* return type */
  parm = RNA_def_pointer(func, "sequence", "Sequence", "", "New Sequence");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "new_sound", new_sound_func_name);
  RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_SELF_ID | FUNC_USE_MAIN);
  RNA_def_function_ui_description(func, "Add a new sound sequence");
  parm = RNA_def_string(func, "name", "Name", 0, "", "Name for the new sequence");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(func, "filepath", "File", 0, "", "Filepath to movie");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(
      func, "channel", 0, 1, MAXSEQ, "Channel", "The channel for the new sequence", 1, MAXSEQ);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func,
                     "frame_start",
                     0,
                     -MAXFRAME,
                     MAXFRAME,
                     "",
                     "The start frame for the new sequence",
                     -MAXFRAME,
                     MAXFRAME);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "sequence", "Sequence", "", "New Sequence");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "new_meta", new_meta_func_name);
  RNA_def_function_flag(func, FUNC_USE_SELF_ID);
  RNA_def_function_ui_description(func, "Add a new meta sequence");
  parm = RNA_def_string(func, "name", "Name", 0, "", "Name for the new sequence");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(
      func, "channel", 0, 1, MAXSEQ, "Channel", "The channel for the new sequence", 1, MAXSEQ);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func,
                     "frame_start",
                     0,
                     -MAXFRAME,
                     MAXFRAME,
                     "",
                     "The start frame for the new sequence",
                     -MAXFRAME,
                     MAXFRAME);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = RNA_def_pointer(func, "sequence", "Sequence", "", "New Sequence");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "new_effect", new_effect_func_name);
  RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_SELF_ID);
  RNA_def_function_ui_description(func, "Add a new effect sequence");
  parm = RNA_def_string(func, "name", "Name", 0, "", "Name for the new sequence");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(func, "type", seq_effect_items, 0, "Type", "type for the new sequence");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(
      func, "channel", 0, 1, MAXSEQ, "Channel", "The channel for the new sequence", 1, MAXSEQ);
  /* don't use MAXFRAME since it makes importer scripts fail */
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func,
                     "frame_start",
                     0,
                     INT_MIN,
                     INT_MAX,
                     "",
                     "The start frame for the new sequence",
                     INT_MIN,
                     INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_int(func,
              "frame_end",
              0,
              INT_MIN,
              INT_MAX,
              "",
              "The end frame for the new sequence",
              INT_MIN,
              INT_MAX);
  RNA_def_pointer(func, "seq1", "Sequence", "", "Sequence 1 for effect");
  RNA_def_pointer(func, "seq2", "Sequence", "", "Sequence 2 for effect");
  RNA_def_pointer(func, "seq3", "Sequence", "", "Sequence 3 for effect");
  /* return type */
  parm = RNA_def_pointer(func, "sequence", "Sequence", "", "New Sequence");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", remove_func_name);
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_USE_REPORTS | FUNC_USE_MAIN);
  RNA_def_function_ui_description(func, "Remove a Sequence");
  parm = RNA_def_pointer(func, "sequence", "Sequence", "", "Sequence to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

#endif
