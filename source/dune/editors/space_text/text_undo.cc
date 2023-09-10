
#include <cerrno>
#include <cstring>

#include "mem_guardedalloc.h"

#include "types_text.h"

#include "lib_array_store.h"
#include "lib_array_utils.h"

#include "lang.h"

#include "PIL_time.h"

#include "dune_cxt.h"
#include "dune_main.h"
#include "dune_report.h"
#include "dune_text.h"
#include "dune_undo_system.h"

#include "wm_api.hh"
#include "wm_types.hh"

#include "ed_curve.hh"
#include "ed_screen.hh"
#include "ed_text.hh"
#include "ed_undo.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "types_access.hh"
#include "types_define.hh"

#include "text_format.hh"
#include "text_intern.hh"

/* Implements ED Undo System */
#define ARRAY_CHUNK_SIZE 128

/* Only stores the state of a text buffer. */
struct TextState {
  BArrayState *buf_array_state;

  int cursor_line, cursor_line_select;
  int cursor_column, cursor_column_select;
};

static void text_state_encode(TextState *state, Text *text, BArrayStore *buffer_store)
{
  size_t buf_len = 0;
  uchar *buf = (uchar *)txt_to_buf_for_undo(text, &buf_len);
  state->buf_array_state = lib_array_store_state_add(buffer_store, buf, buf_len, nullptr);
  mem_freen(buf);

  state->cursor_line = txt_get_span(static_cast<TextLine *>(text->lines.first), text->curl);
  state->cursor_column = text->curc;

  if (txt_has_sel(text)) {
    state->cursor_line_select = (text->curl == text->sell) ?
                                    state->cursor_line :
                                    txt_get_span(static_cast<TextLine *>(text->lines.first),
                                                 text->sell);
    state->cursor_column_select = text->selc;
  }
  else {
    state->cursor_line_select = state->cursor_line;
    state->cursor_column_select = state->cursor_column;
  }
}

static void text_state_decode(TextState *state, Text *text)
{
  size_t buf_len;
  {
    const uchar *buf = static_cast<const uchar *>(
        lib_array_store_state_data_get_alloc(state->buf_array_state, &buf_len));
    txt_from_buf_for_undo(text, (const char *)buf, buf_len);
    mem_freeN((void *)buf);
  }

  const bool has_select = ((state->cursor_line != state->cursor_line_select) ||
                           (state->cursor_column != state->cursor_column_select));
  if (has_select) {
    txt_move_to(text, state->cursor_line_select, state->cursor_column_select, false);
  }
  txt_move_to(text, state->cursor_line, state->cursor_column, has_select);
}

/* Implements ED Undo System */
struct TextUndoStep {
  UndoStep step;
  UndoRefId_Text text_ref;
  /* First state is optional (initial state),
   * the second is the state after the operation is done. */
  TextState states[2];
};

static struct {
  ArrayStore *buffer_store;
  int users;
} g_text_buffers = {nullptr};

static size_t text_undosys_step_encode_to_state(TextState *state, Text *text)
{
  lib_assert(lib_array_is_zeroed(state, 1));
  if (g_text_buffers.buffer_store == nullptr) {
    g_text_buffers.buffer_store = BLI_array_store_create(1, ARRAY_CHUNK_SIZE);
  }
  g_text_buffers.users += 1;
  const size_t total_size_prev = BLI_array_store_calc_size_compacted_get(
      g_text_buffers.buffer_store);

  text_state_encode(state, text, g_text_buffers.buffer_store);

  return lib_array_store_calc_size_compacted_get(g_text_buffers.buffer_store) - total_size_prev;
}

static bool text_undosys_poll(Cxt * /*C*/)
{
  /* Only use when operators initialized. */
  UndoStack *ustack = ed_undo_stack_get();
  return (ustack->step_init && (ustack->step_init->type == DUNE_UNDOSYS_TYPE_TEXT));
}

static void text_undosys_step_encode_init(Cxt *C, UndoStep *us_p)
{
  TextUndoStep *us = (TextUndoStep *)us_p;
  lib_assert(lib_array_is_zeroed(us->states, ARRAY_SIZE(us->states)));

  Text *text = cxt_data_edit_text(C);

  /* Avoid writing the initial state where possible,
   * failing to do this won't cause bugs, it's just inefficient. */
  bool write_init = true;
  UndoStack *ustack = ed_undo_stack_get();
  if (ustack->step_active) {
    if (ustack->step_active->type == BKE_UNDOSYS_TYPE_TEXT) {
      TextUndoStep *us_active = (TextUndoStep *)ustack->step_active;
      if (STREQ(text->id.name, us_active->text_ref.name)) {
        write_init = false;
      }
    }
  }

  if (write_init) {
    us->step.data_size = text_undosys_step_encode_to_state(&us->states[0], text);
  }
  us->text_ref.ptr = text;
}

static bool text_undosys_step_encode(Cxt *C, Main * /*bmain*/, UndoStep *us_p)
{
  TextUndoStep *us = (TextUndoStep *)us_p;

  Text *text = us->text_ref.ptr;
  lib_assert(text == cxt_data_edit_text(C));
  UNUSED_VARS_NDEBUG(C);

  us->step.data_size += text_undosys_step_encode_to_state(&us->states[1], text);

  us_p->is_applied = true;

  return true;
}

static void text_undosys_step_decode(
    Cxt *C, Main * /*bmain*/, UndoStep *us_p, const eUndoStepDir dir, bool is_final)
{
  lib_assert(dir != STEP_INVALID);

  TextUndoStep *us = (TextUndoStep *)us_p;
  Text *text = us->text_ref.ptr;

  TextState *state;
  if ((us->states[0].buf_array_state != nullptr) && (dir == STEP_UNDO) && !is_final) {
    state = &us->states[0];
  }
  else {
    state = &us->states[1];
  }

  text_state_decode(state, text);

  SpaceText *st = cxt_wm_space_text(C);
  if (st) {
    /* Not essential, always show text being undo where possible. */
    st->text = text;
  }
  text_update_cursor_moved(C);
  text_drawcache_tag_update(st, true);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);
}

static void text_undosys_step_free(UndoStep *us_p)
{
  TextUndoStep *us = (TextUndoStep *)us_p;

  for (int i = 0; i < ARRAY_SIZE(us->states); i++) {
    TextState *state = &us->states[i];
    if (state->buf_array_state) {
      lib_array_store_state_remove(g_text_buffers.buffer_store, state->buf_array_state);
      g_text_buffers.users -= 1;
      if (g_text_buffers.users == 0) {
        lib_array_store_destroy(g_text_buffers.buffer_store);
        g_text_buffers.buffer_store = nullptr;
      }
    }
  }
}

static void text_undosys_foreach_id_ref(UndoStep *us_p,
                                        UndoTypeForEachIdRefFn foreach_id_ref_fn,
                                        void *user_data)
{
  TextUndoStep *us = (TextUndoStep *)us_p;
  foreach_id_ref_fn(user_data, ((UndoRefID *)&us->text_ref));
}

void ed_text_undosys_type(UndoType *ut)
{
  ut->name = "Text";
  ut->poll = text_undosys_poll;
  ut->step_encode_init = text_undosys_step_encode_init;
  ut->step_encode = text_undosys_step_encode;
  ut->step_decode = text_undosys_step_decode;
  ut->step_free = text_undosys_step_free;

  ut->step_foreach_ID_ref = text_undosys_foreach_ID_ref;

  ut->flags = UNDOTYPE_FLAG_NEED_CONTEXT_FOR_ENCODE | UNDOTYPE_FLAG_DECODE_ACTIVE_STEP;

  ut->step_size = sizeof(TextUndoStep);
}

/* Utilities */
UndoStep *ed_text_undo_push_init(Cxt *C)
{
  UndoStack *ustack = ed_undo_stack_get();
  Main *main = cxt_data_main(C);
  WM *wm = static_cast<WM *>(main->wm.first);
  if (wm->op_undo_depth <= 1) {
    UndoStep *us_p = dune_undosys_step_push_init_with_type(
        ustack, C, nullptr, DUNE_UNDOSYS_TYPE_TEXT);
    return us_p;
  }
  return nullptr;
}
