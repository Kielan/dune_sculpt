#include <cerrno>
#include <cstring>

#include "mem_guardedalloc.h"

#include "types_text.h"

#include "lib_dune.h"
#include "lib_math_base.h"
#include "lib_math_vector.h"
#include "lib_string_cursor_utf8.h"

#include "lang.h"

#include "PIL_time.h"

#include "dune_cxt.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_report.h"
#include "dune_text.h"

#include "wm_api.hh"
#include "wm_types.hh"

#include "ed_curve.hh"
#include "ed_screen.hh"
#include "ed_text.hh"
#include "ui_interface.hh"
#include "ui_resources.hh"

#include "api_access.hh"
#include "api_define.hh"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#  include "BPY_extern_run.h"
#endif

#include "text_format.hh"
#include "text_intern.hh"

static void txt_screen_clamp(SpaceText *st, ARegion *region);

/* Utils */

/* Tests if the given char represents a start of a new line or the
 * indentation part of a line.
 * param c: The current character.
 * param r_last_state: A pointer to a flag representing the last state. The
 * flag may be modified. */
static void test_line_start(char c, bool *r_last_state)
{
  if (c == '\n') {
    *r_last_state = true;
  }
  else if (!ELEM(c, '\t', ' ')) {
    *r_last_state = false;
  }
}

/* This fn receives a character and returns its closing pair if it exists.
 * param character: Character to find the closing pair.
 * return The closing pair of the character if it exists */
static char text_closing_character_pair_get(const char character)
{

  switch (character) {
    case '(':
      return ')';
    case '[':
      return ']';
    case '{':
      return '}';
    case '"':
      return '"';
    case '\'':
      return '\'';
    default:
      return 0;
  }
}

/* This fn converts the indentation tabs from a buffer to spaces.
 * param in_buf: A pointer to a cstring.
 * param tab_size: The size, in spaces, of the tab character.
 * param r_out_buf_len: The strlen of the returned buffer */
static char *buf_tabs_to_spaces(const char *in_buf, const int tab_size, int *r_out_buf_len)
{
  /* Get the number of tab characters in buffer. */
  bool line_start = true;
  int num_tabs = 0;

  for (int in_offset = 0; in_buf[in_offset]; in_offset++) {
    /* Verify if is an indentation whitespace character. */
    test_line_start(in_buf[in_offset], &line_start);

    if (in_buf[in_offset] == '\t' && line_start) {
      num_tabs++;
    }
  }

  /* Allocate output before with extra space for expanded tabs. */
  const int out_size = strlen(in_buf) + num_tabs * (tab_size - 1) + 1;
  char *out_buf = static_cast<char *>(MEM_mallocN(out_size * sizeof(char), __func__));

  /* Fill output buffer. */
  int spaces_until_tab = 0;
  int out_offset = 0;
  line_start = true;

  for (int in_offset = 0; in_buf[in_offset]; in_offset++) {
    /* Verify if is an indentation whitespace character. */
    test_line_start(in_buf[in_offset], &line_start);

    if (in_buf[in_offset] == '\t' && line_start) {
      /* Calculate tab size so it fills until next indentation. */
      int num_spaces = tab_size - (spaces_until_tab % tab_size);
      spaces_until_tab = 0;

      /* Write to buffer. */
      memset(&out_buf[out_offset], ' ', num_spaces);
      out_offset += num_spaces;
    }
    else {
      if (in_buf[in_offset] == ' ') {
        spaces_until_tab++;
      }
      else if (in_buf[in_offset] == '\n') {
        spaces_until_tab = 0;
      }

      out_buf[out_offset++] = in_buf[in_offset];
    }
  }

  out_buf[out_offset] = '\0';
  *r_out_buf_len = out_offset;
  return out_buf;
}

LIB_INLINE int text_pixel_x_to_column(SpaceText *st, const int x)
{
  /* Add half the char width so mouse cursor selection is in between letters. */
  return (x + (st->runtime.cwidth_px / 2)) / st->runtime.cwidth_px;
}

static void text_select_update_primary_clipboard(const Text *text)
{
  if ((wm_capabilities_flag() & WM_CAPABILITY_PRIMARY_CLIPBOARD) == 0) {
    return;
  }
  if (!txt_has_sel(text)) {
    return;
  }
  char *buf = txt_sel_to_buf(text, nullptr);
  if (buf == nullptr) {
    return;
  }
  wm_clipboard_text_set(buf, true);
  mem_freen(buf);
}

/* Operator Poll */
static bool text_new_poll(Cxt * /*C*/)
{
  return true;
}

static bool text_data_poll(Cxt *C)
{
  Text *text = cxt_data_edit_text(C);
  if (!text) {
    return false;
  }
  return true;
}

static bool text_edit_poll(Cxt *C)
{
  Text *text = cxt_data_edit_text(C);

  if (!text) {
    return false;
  }

  if (!dune_id_is_editable(cxt_data_main(C), &text->id)) {
    // dune_report(op->reports, RPT_ERROR, "Cannot edit external library data");
    return false;
  }

  return true;
}

bool text_space_edit_poll(Cxt *C)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);

  if (!st || !text) {
    return false;
  }

  if (!dune_id_is_editable(cxt_data_main(C), &text->id)) {
    // dune_report(op->reports, RPT_ERROR, "Cannot edit external library data");
    return false;
  }

  return true;
}

static bool text_region_edit_poll(Cxt *C)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  ARegion *region = cxt_wm_region(C);

  if (!st || !text) {
    return false;
  }

  if (!region || region->regiontype != RGN_TYPE_WINDOW) {
    return false;
  }

  if (!dune_id_is_editable(cxt_data_main(C), &text->id)) {
    // dune_report(op->reports, RPT_ERROR, "Cannot edit external library data");
    return false;
  }

  return true;
}

/* Updates */
void text_update_line_edited(TextLine *line)
{
  if (!line) {
    return;
  }

  /* we just free format here, and let it rebuild during draw */
  MEM_SAFE_FREE(line->format);
}

void text_update_edited(Text *text)
{
  LIST_FOREACH (TextLine *, line, &text->lines) {
    text_update_line_edited(line);
  }
}

/* New Operator */
static int text_new_ex(Cxt *C, wmOp * /*op*/)
{
  SpaceText *st = cxt_wm_space_text(C);
  Main *main = cxt_data_main(C);
  Text *text;
  ApiPtr ptr;
  ApiProp *prop;

  text = dune_text_add(main, DATA_("Text"));

  /* hook into UI */
  ui_cxt_active_btn_prop_get_templateId(C, &ptr, &prop);

  if (prop) {
    ApiPtr idptr = api_id_ptr_create(&text->id);
    api_prop_ptr_set(&ptr, prop, idptr, nullptr);
    api_prop_update(C, &ptr, prop);
  }
  else if (st) {
    st->text = text;
    st->left = 0;
    st->top = 0;
    st->runtime.scroll_ofs_px[0] = 0;
    st->runtime.scroll_ofs_px[1] = 0;
    text_drawcache_tag_update(st, true);
  }

  wm_event_add_notifier(C, NC_TEXT | NA_ADDED, text);

  return OP_FINISHED;
}

void TEXT_OT_new(wmOpType *ot)
{
  /* identifiers */
  ot->name = "New Text";
  ot->idname = "TEXT_OT_new";
  ot->description = "Create a new text data-block";

  /* api callbacks */
  ot->ex = text_new_ex;
  ot->poll = text_new_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* Open Op */
static void text_open_init(Cxt *C, wmOp *op)
{
  ApiPropPtr *pprop = static_cast<ApiPropPtr *>(
      mem_callocn(sizeof(ApiPropPtr), "ApiOpenPropPtr"));

  op->customdata = pprop;
  ui_cxt_active_btn_prop_get_templateId(C, &pprop->ptr, &pprop->prop);
}

static void text_open_cancel(Cxt * /*C*/, wmOp *op)
{
  mem_freen(op->customdata);
}

static int text_open_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  Main *main = cxt_data_main(C);
  Text *text;
  ApiPropPtr *pprop;
  char filepath[FILE_MAX];
  const bool internal = api_bool_get(op->ptr, "internal");

  api_string_get(op->ptr, "filepath", filepath);

  text = dune_text_load_ex(main, filepath, dune_main_dunefile_path(bmain), internal);

  if (!text) {
    if (op->customdata) {
      mem_freen(op->customdata);
    }
    return OP_CANCELLED;
  }

  if (!op->customdata) {
    text_open_init(C, op);
  }

  /* hook into UI */
  pprop = static_cast<ApiPropPtr *>(op->customdata);

  if (pprop->prop) {
    ApiPtr idptr = api_id_ptr_create(&text->id);
    api_prop_ptr_set(&pprop->ptr, pprop->prop, idptr, nullptr);
    api_prop_update(C, &pprop->ptr, pprop->prop);
  }
  else if (st) {
    st->text = text;
    st->left = 0;
    st->top = 0;
    st->runtime.scroll_ofs_px[0] = 0;
    st->runtime.scroll_ofs_px[1] = 0;
  }

  text_drawcache_tag_update(st, true);
  wm_event_add_notifier(C, NC_TEXT | NA_ADDED, text);

  MEM_freeN(op->customdata);

  return OP_FINISHED;
}

static int text_open_invoke(Cxt *C, wmOp *op, const wmEvent * /*event*/)
{
  Main *bmain = cxt_data_main(C);
  Text *text = cxt_data_edit_text(C);
  const char *path = (text && text->filepath) ? text->filepath : dube_main_dunefile_path(main);

  if (api_struct_prop_is_set(op->ptr, "filepath")) {
    return text_open_ex(C, op);
  }

  text_open_init(C, op);
  api_string_set(op->ptr, "filepath", path);
  wm_event_add_fileselect(C, op);

  return OP_RUNNING_MODAL;
}

void TEXT_OT_open(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Open Text";
  ot->idname = "TEXT_OT_open";
  ot->description = "Open a new text data-block";

  /* api callbacks */
  ot->ex = text_open_ex;
  ot->invoke = text_open_invoke;
  ot->cancel = text_open_cancel;
  ot->poll = text_new_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  wm_op_props_filesel(ot,
                      FILE_TYPE_FOLDER | FILE_TYPE_TEXT | FILE_TYPE_PYSCRIPT,
                      FILE_SPECIAL,
                      FILE_OPENFILE,
                      WM_FILESEL_FILEPATH,
                      FILE_DEFAULTDISPLAY,
                      FILE_SORT_DEFAULT); /* TODO: relative_path. */
  api_def_bool(
      ot->sapi, "internal", false, "Make Internal", "Make text file internal after loading");
}

/* Reload Op */
static int text_reload_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  ARegion *region = cxt_wm_region(C);

  /* store view & cursor state */
  const int orig_top = st->top;
  const int orig_curl = lib_findindex(&text->lines, text->curl);
  const int orig_curc = text->curc;

  /* Don't make this part of 'poll', since 'Alt-R' will type 'R',
   * if poll checks for the filename. */
  if (text->filepath == nullptr) {
    dune_report(op->reports, RPT_ERROR, "This text has not been saved");
    return OP_CANCELLED;
  }

  if (!dune_text_reload(text)) {
    dune_report(op->reports, RPT_ERROR, "Could not reopen file");
    return OP_CANCELLED;
  }

#ifdef WITH_PYTHON
  if (text->compiled) {
    BPY_text_free_code(text);
  }
#endif

  text_update_edited(text);
  text_update_cursor_moved(C);
  text_drawcache_tag_update(st, true);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  text->flags &= ~TXT_ISDIRTY;

  /* return to scroll position */
  st->top = orig_top;
  txt_screen_clamp(st, region);
  /* return cursor */
  txt_move_to(text, orig_curl, orig_curc, false);

  return OP_FINISHED;
}

void TEXT_OT_reload(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Reload";
  ot->idname = "TEXT_OT_reload";
  ot->description = "Reload active text data-block from its file";

  /* api callbacks */
  ot->ex = text_reload_ex;
  ot->invoke = wm_op_confirm;
  ot->poll = text_edit_poll;
}

/* Delete Operator */
static bool text_unlink_poll(Cxt *C)
{
  /* it should be possible to unlink texts if they're lib-linked in... */
  return cxt_data_edit_text(C) != nullptr;
}

static int text_unlink_ex(Cxt *C, wmOp * /*op*/)
{
  Main *main = cxt_data_main(C);
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);

  /* make the previous text active, if its not there make the next text active */
  if (st) {
    if (text->id.prev) {
      st->text = static_cast<Text *>(text->id.prev);
      text_update_cursor_moved(C);
    }
    else if (text->id.next) {
      st->text = static_cast<Text *>(text->id.next);
      text_update_cursor_moved(C);
    }
  }

  dune_id_delete(main, text);

  text_drawcache_tag_update(st, true);
  wm_event_add_notifier(C, NC_TEXT | NA_REMOVED, nullptr);

  return OP_FINISHED;
}

void TEXT_OT_unlink(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Unlink";
  ot->idname = "TEXT_OT_unlink";
  ot->description = "Unlink active text data-block";

  /* api cbs */
  ot->ex = text_unlink_ex;
  ot->invoke = wm_op_confirm;
  ot->poll = text_unlink_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* Make Internal Operator */
static int text_make_internal_ex(Cxt *C, wmOp * /*op*/)
{
  Text *text = cxt_data_edit_text(C);

  text->flags |= TXT_ISMEM | TXT_ISDIRTY;

  MEM_SAFE_FREE(text->filepath);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

void TEXT_OT_make_internal(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Make Internal";
  ot->idname = "TEXT_OT_make_internal";
  ot->description = "Make active text file internal";

  /* api callbacks */
  ot->ex = text_make_internal_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* Save Operator */
static void txt_write_file(Main *main, Text *text, ReportList *reports)
{
  FILE *fp;
  lib_stat_t st;
  char filepath[FILE_MAX];

  if (text->filepath == nullptr) {
    dune_reportf(reports, RPT_ERROR, "No file path for \"%s\"", text->id.name + 2);
    return;
  }

  STRNCPY(filepath, text->filepath);
  lib_path_abs(filepath, dune_main_dunefile_path(main));

  /* Check if file write permission is ok. */
  if (lib_exists(filepath) && !lib_file_is_writable(filepath)) {
    dune_reportf(
        reports, RPT_ERROR, "Cannot save text file, path \"%s\" is not writable", filepath);
    return;
  }

  fp = lib_fopen(filepath, "w");
  if (fp == nullptr) {
    dune_reportf(reports,
                RPT_ERROR,
                "Unable to save '%s': %s",
                filepath,
                errno ? strerror(errno) : TIP_("unknown error writing file"));
    return;
  }

  LIST_FOREACH (TextLine *, tmp, &text->lines) {
    fputs(tmp->line, fp);
    if (tmp->next) {
      fputc('\n', fp);
    }
  }

  fclose(fp);

  if (lib_stat(filepath, &st) == 0) {
    text->mtime = st.st_mtime;

    /* Report since this can be called from key shortcuts. */
    dune_reportf(reports, RPT_INFO, "Saved text \"%s\"", filepath);
  }
  else {
    text->mtime = 0;
    dune_reportf(reports,
                RPT_WARNING,
                "Unable to stat '%s': %s",
                filepath,
                errno ? strerror(errno) : TIP_("unknown error statting file"));
  }

  text->flags &= ~TXT_ISDIRTY;
}

static int text_save_ex(Cxt *C, wmOp *op)
{
  Main *main = cxt_data_main(C);
  Text *text = cxt_data_edit_text(C);

  txt_write_file(main, text, op->reports);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

static int text_save_invoke(Cxt *C, wmOp *op, const wmEvent *event)
{
  Text *text = cxt_data_edit_text(C);

  /* Internal and texts without a filepath will go to "Save As". */
  if (text->filepath == nullptr || (text->flags & TXT_ISMEM)) {
    wm_op_name_call(C, "TEXT_OT_save_as", WM_OP_INVOKE_DEFAULT, nullptr, event);
    return OP_CANCELLED;
  }
  return text_save_ex(C, op);
}

void TEXT_OT_save(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Save";
  ot->idname = "TEXT_OT_save";
  ot->description = "Save active text data-block";

  /* api callbacks */
  ot->ex = text_save_ex;
  ot->invoke = text_save_invoke;
  ot->poll = text_edit_poll;
}

/* Save As Operator */
static int text_save_as_ex(Cxt *C, wmOp *op)
{
  Main *main = cxt_data_main(C);
  Text *text = cxt_data_edit_text(C);
  char filepath[FILE_MAX];

  if (!text) {
    return OP_CANCELLED;
  }

  api_string_get(op->ptr, "filepath", filepath);

  if (text->filepath) {
  men_freen(text->filepath);
  }
  text->filepath = lib_strdup(filepath);
  text->flags &= ~TXT_ISMEM;

  txt_write_file(main, text, op->reports);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

static int text_save_as_invoke(Cxt *C, wmOp *op, const wmEvent * /*event*/)
{
  Main *main = cxt_data_main(C);
  Text *text = cxt_data_edit_text(C);

  if (api_struct_prop_is_set(op->ptr, "filepath")) {
    return text_save_as_ex(C, op);
  }

  const char *filepath;
  if (text->filepath) {
    filepath = text->filepath;
  }
  else if (text->flags & TXT_ISMEM) {
    filepath = text->id.name + 2;
  }
  else {
    filepath = dune_main_dunefile_path(main);
  }

  api_string_set(op->ptr, "filepath", filepath);
  wm_event_add_fileselect(C, op);

  return OP_RUNNING_MODAL;
}

void TEXT_OT_save_as(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Save As";
  ot->idname = "TEXT_OT_save_as";
  ot->description = "Save active text file with options";

  /* api callbacks */
  ot->ex = text_save_as_ex;
  ot->invoke = text_save_as_invoke;
  ot->poll = text_edit_poll;

  /* props */
  wm_op_props_filesel(ot,
                      FILE_TYPE_FOLDER | FILE_TYPE_TEXT | FILE_TYPE_PYSCRIPT,
                      FILE_SPECIAL,
                      FILE_SAVE,
                      WM_FILESEL_FILEPATH,
                      FILE_DEFAULTDISPLAY,
                      FILE_SORT_DEFAULT); /* XXX TODO: relative_path. */
}

/* Run Script Operator */
static int text_run_script(Cxt *C, ReportList *reports)
{
#ifdef WITH_PYTHON
  Text *text = cxt_data_edit_text(C);
  const bool is_live = (reports == nullptr);

  /* only for comparison */
  void *curl_prev = text->curl;
  int curc_prev = text->curc;

  if (BPY_run_text(C, text, reports, !is_live)) {
    if (is_live) {
      /* for nice live updates */
      wm_event_add_notifier(C, NC_WINDOW | NA_EDITED, nullptr);
    }
    return OP_FINISHED;
  }

  /* Don't report error messages while live editing */
  if (!is_live) {
    /* text may have freed itself */
    if (cxt_data_edit_text(C) == text) {
      if (text->curl != curl_prev || curc_prev != text->curc) {
        text_update_cursor_moved(C);
        wn_event_add_notifier(C, NC_TEXT | NA_EDITED, text);
      }
    }

    /* No need to report the error, this has already been handled by #BPY_run_text. */
    return OP_FINISHED;
  }
#else
  (void)C;
  (void)reports;
#endif /* !WITH_PYTHON */
  return OP_CANCELLED;
}

static int text_run_script_ex(Cxt *C, wmOp *op)
{
#ifndef WITH_PYTHON
  (void)C; /* unused */

  dune_report(op->reports, RPT_ERROR, "Python disabled in this build");

  return OP_CANCELLED;
#else
  return text_run_script(C, op->reports);
#endif
}

void TEXT_OT_run_script(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Run Script";
  ot->idname = "TEXT_OT_run_script";
  ot->description = "Run active script";

  /* api callbacks */
  ot->poll = text_data_poll;
  ot->ex = text_run_script_ex;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Refresh Pyconstraints Operator */
static int text_refresh_pyconstraints_exec(Cxt * /*C*/, wmOp * /*op*/)
{
#ifdef WITH_PYTHON
#  if 0
  Main *main = cxt_data_main(C);
  Text *text = cxt_data_edit_text(C);
  Object *ob;
  Constraint *con;
  short update;

  /* check all pyconstraints */
  for (ob = main->objects.first; ob; ob = ob->id.next) {
    update = 0;
    if (ob->type == OB_ARMATURE && ob->pose) {
      PoseChannel *pchan;
      for (pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
        for (con = pchan->constraints.first; con; con = con->next) {
          if (con->type == CONSTRAINT_TYPE_PYTHON) {
            bPythonConstraint *data = con->data;
            if (data->text == text) {
              BPY_pyconstraint_update(ob, con);
            }
            update = 1;
          }
        }
      }
    }
    for (con = ob->constraints.first; con; con = con->next) {
      if (con->type == CONSTRAINT_TYPE_PYTHON) {
        bPythonConstraint *data = con->data;
        if (data->text == text) {
          BPY_pyconstraint_update(ob, con);
        }
        update = 1;
      }
    }

    if (update) {
      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    }
  }
#  endif
#endif

  return OP_FINISHED;
}

void TEXT_OT_refresh_pyconstraints(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Refresh PyConstraints";
  ot->idname = "TEXT_OT_refresh_pyconstraints";
  ot->description = "Refresh all pyconstraints";

  /* api callbacks */
  ot->ex = text_refresh_pyconstraints_ex;
  ot->poll = text_edit_poll;
}

/* Paste Operator */
static int text_paste_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);

  const bool selection = api_bool_get(op->ptr, "selection");

  char *buf;
  int buf_len;

  /* No need for UTF8 validation as the conversion handles invalid sequences gracefully. */
  buf = wm_clipboard_text_get(selection, false, &buf_len);

  if (!buf) {
    return OP_CANCELLED;
  }

  text_drawcache_tag_update(st, false);

  ed_text_undo_push_init(C);

  /* Convert clipboard content indentation to spaces if specified */
  if (text->flags & TXT_TABSTOSPACES) {
    char *new_buf = buf_tabs_to_spaces(buf, TXT_TABSIZE, &buf_len);
    mem_freen(buf);
    buf = new_buf;
  }

  txt_insert_buf(text, buf, buf_len);
  text_update_edited(text);

  mem_freen(buf);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  /* run the script while editing, evil but useful */
  if (st->live_edit) {
    text_run_script(C, nullptr);
  }

  return OP_FINISHED;
}

void TEXT_OT_paste(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Paste";
  ot->idname = "TEXT_OT_paste";
  ot->description = "Paste text from clipboard";

  /* api cbs */
  ot->ex = text_paste_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  ApiProp *prop;
  prop = api_def_bool(ot->sapi,
                      "selection",
                      false,
                      "Selection",
                      "Paste text selected elsewhere rather than copied (X11/Wayland only)");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* Duplicate Operator */
static int text_duplicate_line_ex(Cxt *C, wmOp * /*op*/)
{
  Text *text = cxt_data_edit_text(C);

  ed_text_undo_push_init(C);

  txt_duplicate_line(text);

  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  /* run the script while editing, evil but useful */
  if (cxt_wm_space_text(C)->live_edit) {
    text_run_script(C, nullptr);
  }

  return OP_FINISHED;
}

void TEXT_OT_duplicate_line(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Duplicate Line";
  ot->idname = "TEXT_OT_duplicate_line";
  ot->description = "Duplicate the current line";

  /* api callbacks */
  ot->ex = text_duplicate_line_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* Copy Operator */
static void txt_copy_clipboard(Text *text)
{
  char *buf;

  if (!txt_has_sel(text)) {
    return;
  }

  buf = txt_sel_to_buf(text, nullptr);

  if (buf) {
    wm_clipboard_text_set(buf, false);
    mem_freen(buf);
  }
}

static int text_copy_ex(Cxt *C, wmOp * /*op*/)
{
  Text *text = cxt_data_edit_text(C);

  txt_copy_clipboard(text);

  return OP_FINISHED;
}

void TEXT_OT_copy(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Copy";
  ot->idname = "TEXT_OT_copy";
  ot->description = "Copy selected text to clipboard";

  /* api callbacks */
  ot->ex = text_copy_ex;
  ot->poll = text_edit_poll;
}

/* Cut Operator */
static int text_cut_ex(Cxt *C, wmOp * /*op*/)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);

  text_drawcache_tag_update(st, false);

  txt_copy_clipboard(text);

  ed_text_undo_push_init(C);
  txt_delete_selected(text);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  /* run the script while editing, evil but useful */
  if (st->live_edit) {
    text_run_script(C, nullptr);
  }

  return OP_FINISHED;
}

void TEXT_OT_cut(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Cut";
  ot->idname = "TEXT_OT_cut";
  ot->description = "Cut selected text to clipboard";

  /* api cbs */
  ot->ex = text_cut_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}



/* Indent or Autocomplete Operator */
static int text_indent_or_autocomplete_ex(Cxt *C, wmOp * /*op*/)
{
  Text *text = cxt_data_edit_text(C);
  TextLine *line = text->curl;
  bool text_before_cursor = text->curc != 0 && !ELEM(line->line[text->curc - 1], ' ', '\t');
  if (text_before_cursor && (txt_has_sel(text) == false)) {
    wm_op_name_call(C, "TEXT_OT_autocomplete", WM_OP_INVOKE_DEFAULT, nullptr, nullptr);
  }
  else {
    wm_op_name_call(C, "TEXT_OT_indent", WM_OP_EX_DEFAULT, nullptr, nullptr);
  }
  return OP_FINISHED;
}

void TEXT_OT_indent_or_autocomplete(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Indent or Autocomplete";
  ot->idname = "TEXT_OT_indent_or_autocomplete";
  ot->description = "Indent selected text or autocomplete";

  /* api callbacks */
  ot->ex = text_indent_or_autocomplete_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = 0;
}

/* Indent Operator */
static int text_indent_ex(Cxt *C, wmOp * /*op*/)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);

  text_drawcache_tag_update(st, false);

  ed_text_undo_push_init(C);

  if (txt_has_sel(text)) {
    txt_order_cursors(text, false);
    txt_indent(text);
  }
  else {
    txt_add_char(text, '\t');
  }

  text_update_edited(text);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

void TEXT_OT_indent(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Indent";
  ot->idname = "TEXT_OT_indent";
  ot->description = "Indent selected text";

  /* api callbacks */
  ot->ex = text_indent_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* Unindent Operator */
static int text_unindent_ex(Cxt *C, wmOp * /*op*/)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);

  text_drawcache_tag_update(st, false);

  ed_text_undo_push_init(C);

  txt_order_cursors(text, false);
  txt_unindent(text);

  text_update_edited(text);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

void TEXT_OT_unindent(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Unindent";
  ot->idname = "TEXT_OT_unindent";
  ot->description = "Unindent selected text";

  /* api cbs */
  ot->ex = text_unindent_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* Line Break Operator */
static int text_line_break_ex(Cxt *C, wmOp * /*op*/)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  int a, curts;
  int space = (text->flags & TXT_TABSTOSPACES) ? st->tabnumber : 1;

  text_drawcache_tag_update(st, false);

  /* Double check tabs/spaces before splitting the line. */
  curts = txt_setcurr_tab_spaces(text, space);
  ed_text_undo_push_init(C);
  txt_split_curline(text);

  for (a = 0; a < curts; a++) {
    if (text->flags & TXT_TABSTOSPACES) {
      txt_add_char(text, ' ');
    }
    else {
      txt_add_char(text, '\t');
    }
  }

  if (text->curl) {
    if (text->curl->prev) {
      text_update_line_edited(text->curl->prev);
    }
    text_update_line_edited(text->curl);
  }

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

void TEXT_OT_line_break(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Line Break";
  ot->idname = "TEXT_OT_line_break";
  ot->description = "Insert line break at cursor position";

  /* api callbacks */
  ot->ex = text_line_break_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* Toggle-Comment Operator */
static int text_comment_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  int type = api_enum_get(op->ptr, "type");
  const char *prefix = ed_text_format_comment_line_prefix(text);

  text_drawcache_tag_update(st, false);

  ed_text_undo_push_init(C);

  if (txt_has_sel(text)) {
    txt_order_cursors(text, false);
  }

  switch (type) {
    case 1:
      txt_comment(text, prefix);
      break;
    case -1:
      txt_uncomment(text, prefix);
      break;
    default:
      if (txt_uncomment(text, prefix) == false) {
        txt_comment(text, prefix);
      }
      break;
  }

  text_update_edited(text);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

void TEXT_OT_comment_toggle(wmOpType *ot)
{
  static const EnumPropItem comment_items[] = {
      {0, "TOGGLE", 0, "Toggle Comments", nullptr},
      {1, "COMMENT", 0, "Comment", nullptr},
      {-1, "UNCOMMENT", 0, "Un-Comment", nullptr},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* identifiers */
  ot->name = "Toggle Comments";
  ot->idname = "TEXT_OT_comment_toggle";

  /* api callbacks */
  ot->ex = text_comment_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  ApiProp *prop;
  prop = api_def_enum(ot->srna, "type", comment_items, 0, "Type", "Add or remove comments");
  api_def_prop_flag(prop, PropertyFlag(PROP_HIDDEN | PROP_SKIP_SAVE));
}

/* Convert Whitespace Operator */
enum { TO_SPACES, TO_TABS };
static const EnumPropItem whitespace_type_items[] = {
    {TO_SPACES, "SPACES", 0, "To Spaces", nullptr},
    {TO_TABS, "TABS", 0, "To Tabs", nullptr},
    {0, nullptr, 0, nullptr, nullptr},
};

static int text_convert_whitespace_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  FlattenString fs;
  size_t a, j, max_len = 0;
  int type = api_enum_get(op->ptr, "type");

  /* first convert to all space, this make it a lot easier to convert to tabs
   * because there is no mixtures of ' ' && '\t' */
  LIST_FOREACH (TextLine *, tmp, &text->lines) {
    char *new_line;

    lib_assert(tmp->line);

    flatten_string(st, &fs, tmp->line);
    new_line = lib_strdup(fs.buf);
    flatten_string_free(&fs);

    mem_freen(tmp->line);
    if (tmp->format) {
      mem_freen(tmp->format);
    }

    /* Put new_line in the tmp->line spot still need to try and set the curc correctly. */
    tmp->line = new_line;
    tmp->len = strlen(new_line);
    tmp->format = nullptr;
    if (tmp->len > max_len) {
      max_len = tmp->len;
    }
  }

  if (type == TO_TABS) {
    char *tmp_line = static_cast<char *>(mem_mallocn(sizeof(*tmp_line) * (max_len + 1), __func__));

    LIST_FOREACH (TextLine *, tmp, &text->lines) {
      const char *text_check_line = tmp->line;
      const int text_check_line_len = tmp->len;
      char *tmp_line_cur = tmp_line;
      const size_t tab_len = st->tabnumber;

      lib_assert(text_check_line);

      for (a = 0; a < text_check_line_len;) {
        /* A tab can only start at a position multiple of tab_len... */
        if (!(a % tab_len) && (text_check_line[a] == ' ')) {
          /* a + 0 we already know to be ' ' char... */
          for (j = 1;
               (j < tab_len) && (a + j < text_check_line_len) && (text_check_line[a + j] == ' ');
               j++) {
            /* pass */
          }

          if (j == tab_len) {
            /* We found a set of spaces that can be replaced by a tab... */
            if ((tmp_line_cur == tmp_line) && a != 0) {
              /* Copy all 'valid' string already 'parsed'... */
              memcpy(tmp_line_cur, text_check_line, a);
              tmp_line_cur += a;
            }
            *tmp_line_cur = '\t';
            tmp_line_cur++;
            a += j;
          }
          else {
            if (tmp_line_cur != tmp_line) {
              memcpy(tmp_line_cur, &text_check_line[a], j);
              tmp_line_cur += j;
            }
            a += j;
          }
        }
        else {
          size_t len = lib_str_utf8_size_safe(&text_check_line[a]);
          if (tmp_line_cur != tmp_line) {
            memcpy(tmp_line_cur, &text_check_line[a], len);
            tmp_line_cur += len;
          }
          a += len;
        }
      }

      if (tmp_line_cur != tmp_line) {
        *tmp_line_cur = '\0';

#ifndef NDEBUG
        lib_assert(tmp_line_cur - tmp_line <= max_len);

        flatten_string(st, &fs, tmp_line);
        lib_assert(STREQ(fs.buf, tmp->line));
        flatten_string_free(&fs);
#endif

        mem_freen(tmp->line);
        if (tmp->format) {
          mem_freen(tmp->format);
        }

        /* Put new_line in the tmp->line spot
         * still need to try and set the curc correctly. */
        tmp->line = lib_strdup(tmp_line);
        tmp->len = strlen(tmp_line);
        tmp->format = nullptr;
      }
    }

    mem_freen(tmp_line);
  }

  text_update_edited(text);
  text_update_cursor_moved(C);
  text_drawcache_tag_update(st, true);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

void TEXT_OT_convert_whitespace(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Convert Whitespace";
  ot->idname = "TEXT_OT_convert_whitespace";
  ot->description = "Convert whitespaces by type";

  /* api callbacks */
  ot->ex = text_convert_whitespace_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  api_def_enum(ot->sapi,
               "type",
               whitespace_type_items,
               TO_SPACES,
               "Type",
               "Type of whitespace to convert to");
}

/* Select All Operator */
static int text_select_all_ex(Cxt *C, wmOp * /*op*/)
{
  Text *text = cxt_data_edit_text(C);

  txt_sel_all(text);

  text_update_cursor_moved(C);
  text_select_update_primary_clipboard(text);

  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

void TEXT_OT_select_all(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Select All";
  ot->idname = "TEXT_OT_select_all";
  ot->description = "Select all text";

  /* api callbacks */
  ot->ex = text_select_all_ex;
  ot->poll = text_edit_poll;
}

/* Select Line Operator */
static int text_select_line_ex(Cxt *C, wmOp * /*op*/)
{
  Text *text = cxt_data_edit_text(C);

  txt_sel_line(text);

  text_update_cursor_moved(C);
  text_select_update_primary_clipboard(text);

  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

void TEXT_OT_select_line(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Select Line";
  ot->idname = "TEXT_OT_select_line";
  ot->description = "Select text by line";

  /* api callbacks */
  ot->ex = text_select_line_ex;
  ot->poll = text_edit_poll;
}

/* Select Word Operator */
static int text_select_word_ex(Cxt *C, wmOp * /*op*/
{
  Text *text = cxt_data_edit_text(C);

  lib_str_cursor_step_bounds_utf8(
      text->curl->line, text->curl->len, text->selc, &text->curc, &text->selc);

  text_update_cursor_moved(C);
  text_select_update_primary_clipboard(text);

  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

void TEXT_OT_select_word(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Select Word";
  ot->idname = "TEXT_OT_select_word";
  ot->description = "Select word under cursor";

  /* api callbacks */
  ot->ex = text_select_word_ex;
  ot->poll = text_edit_poll;
}

/* Move Lines Operators */
static int move_lines_ex(Cxt *C, wmOp *op)
{
  Text *text = cxt_data_edit_text(C);
  const int direction = api_enum_get(op->ptr, "direction");

  ed_text_undo_push_init(C);

  txt_move_lines(text, direction);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  /* run the script while editing, evil but useful */
  if (cxt_wm_space_text(C)->live_edit) {
    text_run_script(C, nullptr);
  }

  return OP_FINISHED;
}

void TEXT_OT_move_lines(wmOpType *ot)
{
  static const EnumPropItem direction_items[] = {
      {TXT_MOVE_LINE_UP, "UP", 0, "Up", ""},
      {TXT_MOVE_LINE_DOWN, "DOWN", 0, "Down", ""},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* identifiers */
  ot->name = "Move Lines";
  ot->idname = "TEXT_OT_move_lines";
  ot->description = "Move the currently selected line(s) up/down";

  /* api callbacks */
  ot->ex = move_lines_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* props */
  api_def_enum(ot->sapi, "direction", direction_items, 1, "Direction", "");
}

/* Move Operator */
static const EnumPropItem move_type_items[] = {
    {LINE_BEGIN, "LINE_BEGIN", 0, "Line Begin", ""},
    {LINE_END, "LINE_END", 0, "Line End", ""},
    {FILE_TOP, "FILE_TOP", 0, "File Top", ""},
    {FILE_BOTTOM, "FILE_BOTTOM", 0, "File Bottom", ""},
    {PREV_CHAR, "PREVIOUS_CHARACTER", 0, "Previous Character", ""},
    {NEXT_CHAR, "NEXT_CHARACTER", 0, "Next Character", ""},
    {PREV_WORD, "PREVIOUS_WORD", 0, "Previous Word", ""},
    {NEXT_WORD, "NEXT_WORD", 0, "Next Word", ""},
    {PREV_LINE, "PREVIOUS_LINE", 0, "Previous Line", ""},
    {NEXT_LINE, "NEXT_LINE", 0, "Next Line", ""},
    {PREV_PAGE, "PREVIOUS_PAGE", 0, "Previous Page", ""},
    {NEXT_PAGE, "NEXT_PAGE", 0, "Next Page", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

/* get cursor position in line by relative wrapped line and column positions */
static int text_get_cursor_rel(
    SpaceText *st, ARegion *region, TextLine *linein, int rell, int relc)
{
  int i, j, start, end, max, chop, curs, loop, endj, found, selc;
  char ch;

  max = wrap_width(st, region);

  selc = start = endj = curs = found = 0;
  end = max;
  chop = loop = 1;

  for (i = 0, j = 0; loop; j += BLI_str_utf8_size_safe(linein->line + j)) {
    int chars;
    int columns = lib_str_utf8_char_width_safe(linein->line + j); /* = 1 for tab */

    /* Mimic replacement of tabs */
    ch = linein->line[j];
    if (ch == '\t') {
      chars = st->tabnumber - i % st->tabnumber;
      ch = ' ';
    }
    else {
      chars = 1;
    }

    while (chars--) {
      if (rell == 0 && i - start <= relc && i + columns - start > relc) {
        /* current position could be wrapped to next line */
        /* this should be checked when end of current line would be reached */
        selc = j;
        found = 1;
      }
      else if (i - end <= relc && i + columns - end > relc) {
        curs = j;
      }
      if (i + columns - start > max) {
        end = MIN2(end, i);

        if (found) {
          /* exact cursor position was found, check if it's */
          /* still on needed line (hasn't been wrapped) */
          if (selc > endj && !chop) {
            selc = endj;
          }
          loop = 0;
          break;
        }

        if (chop) {
          endj = j;
        }

        start = end;
        end += max;
        chop = 1;
        rell--;

        if (rell == 0 && i + columns - start > relc) {
          selc = curs;
          loop = 0;
          break;
        }
      }
      else if (ch == '\0') {
        if (!found) {
          selc = linein->len;
        }
        loop = 0;
        break;
      }
      else if (ELEM(ch, ' ', '-')) {
        if (found) {
          loop = 0;
          break;
        }

        if (rell == 0 && i + columns - start > relc) {
          selc = curs;
          loop = 0;
          break;
        }
        end = i + 1;
        endj = j;
        chop = 0;
      }
      i += columns;
    }
  }

  return selc;
}

static int cursor_skip_find_line(
    SpaceText *st, ARegion *region, int lines, TextLine **linep, int *charp, int *rell, int *relc)
{
  int offl, offc, visible_lines;

  wrap_offset_in_line(st, region, *linep, *charp, &offl, &offc);
  *relc = text_get_char_pos(st, (*linep)->line, *charp) + offc;
  *rell = lines;

  /* handle current line */
  if (lines > 0) {
    visible_lines = text_get_visible_lines(st, region, (*linep)->line);

    if (*rell - visible_lines + offl >= 0) {
      if (!(*linep)->next) {
        if (offl < visible_lines - 1) {
          *rell = visible_lines - 1;
          return 1;
        }

        *charp = (*linep)->len;
        return 0;
      }

      *rell -= visible_lines - offl;
      *linep = (*linep)->next;
    }
    else {
      *rell += offl;
      return 1;
    }
  }
  else {
    if (*rell + offl <= 0) {
      if (!(*linep)->prev) {
        if (offl) {
          *rell = 0;
          return 1;
        }

        *charp = 0;
        return 0;
      }

      *rell += offl;
      *linep = (*linep)->prev;
    }
    else {
      *rell += offl;
      return 1;
    }
  }

  /* skip lines and find destination line and offsets */
  while (*linep) {
    visible_lines = text_get_visible_lines(st, region, (*linep)->line);

    if (lines < 0) { /* moving top */
      if (*rell + visible_lines >= 0) {
        *rell += visible_lines;
        break;
      }

      if (!(*linep)->prev) {
        *rell = 0;
        break;
      }

      *rell += visible_lines;
      *linep = (*linep)->prev;
    }
    else { /* moving bottom */
      if (*rell - visible_lines < 0) {
        break;
      }

      if (!(*linep)->next) {
        *rell = visible_lines - 1;
        break;
      }

      *rell -= visible_lines;
      *linep = (*linep)->next;
    }
  }

  return 1;
}

static void txt_wrap_move_bol(SpaceText *st, ARegion *region, const bool sel)
{
  Text *text = st->text;
  TextLine **linep;
  int *charp;
  int oldc, i, j, max, start, end, endj, chop, loop;
  char ch;

  text_update_character_width(st);

  if (sel) {
    linep = &text->sell;
    charp = &text->selc;
  }
  else {
    linep = &text->curl;
    charp = &text->curc;
  }

  oldc = *charp;

  max = wrap_width(st, region);

  start = endj = 0;
  end = max;
  chop = loop = 1;
  *charp = 0;

  for (i = 0, j = 0; loop; j += lib_str_utf8_size_safe((*linep)->line + j)) {
    int chars;
    int columns = lib_str_utf8_char_width_safe((*linep)->line + j); /* = 1 for tab */

    /* Mimic replacement of tabs */
    ch = (*linep)->line[j];
    if (ch == '\t') {
      chars = st->tabnumber - i % st->tabnumber;
      ch = ' ';
    }
    else {
      chars = 1;
    }

    while (chars--) {
      if (i + columns - start > max) {
        end = MIN2(end, i);

        *charp = endj;

        if (j >= oldc) {
          if (ch == '\0') {
            *charp = lib_str_utf8_offset_from_column((*linep)->line, start);
          }
          loop = 0;
          break;
        }

        if (chop) {
          endj = j;
        }

        start = end;
        end += max;
        chop = 1;
      }
      else if (ELEM(ch, ' ', '-', '\0')) {
        if (j >= oldc) {
          *charp = lib_str_utf8_offset_from_column((*linep)->line, start);
          loop = 0;
          break;
        }

        end = i + 1;
        endj = j + 1;
        chop = 0;
      }
      i += columns;
    }
  }

  if (!sel) {
    txt_pop_sel(text);
  }
}

static void txt_wrap_move_eol(SpaceText *st, ARegion *region, const bool sel)
{
  Text *text = st->text;
  TextLine **linep;
  int *charp;
  int oldc, i, j, max, start, end, endj, chop, loop;
  char ch;

  text_update_character_width(st);

  if (sel) {
    linep = &text->sell;
    charp = &text->selc;
  }
  else {
    linep = &text->curl;
    charp = &text->curc;
  }

  oldc = *charp;

  max = wrap_width(st, region);

  start = endj = 0;
  end = max;
  chop = loop = 1;
  *charp = 0;

  for (i = 0, j = 0; loop; j += lib_str_utf8_size_safe((*linep)->line + j)) {
    int chars;
    int columns = lib_str_utf8_char_width_safe((*linep)->line + j); /* = 1 for tab */

    /* Mimic replacement of tabs */
    ch = (*linep)->line[j];
    if (ch == '\t') {
      chars = st->tabnumber - i % st->tabnumber;
      ch = ' ';
    }
    else {
      chars = 1;
    }

    while (chars--) {
      if (i + columns - start > max) {
        end = MIN2(end, i);

        if (chop) {
          endj = lib_str_find_prev_char_utf8((*linep)->line + j, (*linep)->line) - (*linep)->line;
        }

        if (endj >= oldc) {
          if (ch == '\0') {
            *charp = (*linep)->len;
          }
          else {
            *charp = endj;
          }
          loop = 0;
          break;
        }

        start = end;
        end += max;
        chop = 1;
      }
      else if (ch == '\0') {
        *charp = (*linep)->len;
        loop = 0;
        break;
      }
      else if (ELEM(ch, ' ', '-')) {
        end = i + 1;
        endj = j;
        chop = 0;
      }
      i += columns;
    }
  }

  if (!sel) {
    txt_pop_sel(text);
  }
}
static void txt_wrap_move_up(SpaceText *st, ARegion *region, const bool sel)
{
  Text *text = st->text;
  TextLine **linep;
  int *charp;
  int offl, offc, col;

  text_update_character_width(st);

  if (sel) {
    linep = &text->sell;
    charp = &text->selc;
  }
  else {
    linep = &text->curl;
    charp = &text->curc;
  }

  wrap_offset_in_line(st, region, *linep, *charp, &offl, &offc);
  col = text_get_char_pos(st, (*linep)->line, *charp) + offc;
  if (offl) {
    *charp = text_get_cursor_rel(st, region, *linep, offl - 1, col);
  }
  else {
    if ((*linep)->prev) {
      int visible_lines;

      *linep = (*linep)->prev;
      visible_lines = text_get_visible_lines(st, region, (*linep)->line);
      *charp = text_get_cursor_rel(st, region, *linep, visible_lines - 1, col);
    }
    else {
      *charp = 0;
    }
  }

  if (!sel) {
    txt_pop_sel(text);
  }
}

static void txt_wrap_move_down(SpaceText *st, ARegion *region, const bool sel)
{
  Text *text = st->text;
  TextLine **linep;
  int *charp;
  int offl, offc, col, visible_lines;

  text_update_character_width(st);

  if (sel) {
    linep = &text->sell;
    charp = &text->selc;
  }
  else {
    linep = &text->curl;
    charp = &text->curc;
  }

  wrap_offset_in_line(st, region, *linep, *charp, &offl, &offc);
  col = text_get_char_pos(st, (*linep)->line, *charp) + offc;
  visible_lines = text_get_visible_lines(st, region, (*linep)->line);
  if (offl < visible_lines - 1) {
    *charp = text_get_cursor_rel(st, region, *linep, offl + 1, col);
  }
  else {
    if ((*linep)->next) {
      *linep = (*linep)->next;
      *charp = text_get_cursor_rel(st, region, *linep, 0, col);
    }
    else {
      *charp = (*linep)->len;
    }
  }

  if (!sel) {
    txt_pop_sel(text);
  }
}

/* Moves the cursor vertically by the specified number of lines.
 * If the destination line is shorter than the current cursor position, the
 * cursor will be positioned at the end of this line.
 *
 * This is to replace screen_skip for PageUp/Down operations. */
static void cursor_skip(SpaceText *st, ARegion *region, Text *text, int lines, const bool sel)
{
  TextLine **linep;
  int *charp;

  if (sel) {
    linep = &text->sell;
    charp = &text->selc;
  }
  else {
    linep = &text->curl;
    charp = &text->curc;
  }

  if (st && region && st->wordwrap) {
    int rell, relc;

    /* find line and offsets inside it needed to set cursor position */
    if (cursor_skip_find_line(st, region, lines, linep, charp, &rell, &relc)) {
      *charp = text_get_cursor_rel(st, region, *linep, rell, relc);
    }
  }
  else {
    while (lines > 0 && (*linep)->next) {
      *linep = (*linep)->next;
      lines--;
    }
    while (lines < 0 && (*linep)->prev) {
      *linep = (*linep)->prev;
      lines++;
    }
  }

  if (*charp > (*linep)->len) {
    *charp = (*linep)->len;
  }

  if (!sel) {
    txt_pop_sel(text);
  }
}

static int text_move_cursor(Cxt *C, int type, bool select)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  ARegion *region = cxt_wm_region(C);

  /* ensure we have the right region, it's optional */
  if (region && region->regiontype != RGN_TYPE_WINDOW) {
    region = nullptr;
  }

  switch (type) {
    case LINE_BEGIN:
      if (!select) {
        txt_sel_clear(text);
      }
      if (st && st->wordwrap && region) {
        txt_wrap_move_bol(st, region, select);
      }
      else {
        txt_move_bol(text, select);
      }
      break;

    case LINE_END:
      if (!select) {
        txt_sel_clear(text);
      }
      if (st && st->wordwrap && region) {
        txt_wrap_move_eol(st, region, select);
      }
      else {
        txt_move_eol(text, select);
      }
      break;

    case FILE_TOP:
      txt_move_bof(text, select);
      break;

    case FILE_BOTTOM:
      txt_move_eof(text, select);
      break;

    case PREV_WORD:
      if (txt_cursor_is_line_start(text)) {
        txt_move_left(text, select);
      }
      txt_jump_left(text, select, true);
      break;

    case NEXT_WORD:
      if (txt_cursor_is_line_end(text)) {
        txt_move_right(text, select);
      }
      txt_jump_right(text, select, true);
      break;

    case PREV_CHAR:
      if (txt_has_sel(text) && !select) {
        txt_order_cursors(text, false);
        txt_pop_sel(text);
      }
      else {
        txt_move_left(text, select);
      }
      break;

    case NEXT_CHAR:
      if (txt_has_sel(text) && !select) {
        txt_order_cursors(text, true);
        txt_pop_sel(text);
      }
      else {
        txt_move_right(text, select);
      }
      break;

    case PREV_LINE:
      if (st && st->wordwrap && region) {
        txt_wrap_move_up(st, region, select);
      }
      else {
        txt_move_up(text, select);
      }
      break;

    case NEXT_LINE:
      if (st && st->wordwrap && region) {
        txt_wrap_move_down(st, region, select);
      }
      else {
        txt_move_down(text, select);
      }
      break;

    case PREV_PAGE:
      if (st) {
        cursor_skip(st, region, st->text, -st->runtime.viewlines, select);
      }
      else {
        cursor_skip(nullptr, nullptr, text, -10, select);
      }
      break;

    case NEXT_PAGE:
      if (st) {
        cursor_skip(st, region, st->text, st->runtime.viewlines, select);
      }
      else {
        cursor_skip(nullptr, nullptr, text, 10, select);
      }
      break;
  }

  text_update_cursor_moved(C);
  if (select) {
    text_select_update_primary_clipboard(st->text);
  }

  wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, text);

  return OP_FINISHED;
}

static int text_move_ex(Cxt *C, wmOp *op)
{
  int type = api_enum_get(op->ptr, "type");

  return text_move_cursor(C, type, false);
}

void TEXT_OT_move(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Move Cursor";
  ot->idname = "TEXT_OT_move";
  ot->description = "Move cursor to position type";

  /* api callbacks */
  ot->ex = text_move_ex;
  ot->poll = text_edit_poll;

  /* props */
  api_def_enum(ot->sapi, "type", move_type_items, LINE_BEGIN, "Type", "Where to move cursor to");
}

/* Move Select Op */
static int text_move_select_ex(Cxt *C, wmOp *op)
{
  int type = api_enum_get(op->ptr, "type");

  return text_move_cursor(C, type, true);
}

void TEXT_OT_move_select(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Move Select";
  ot->idname = "TEXT_OT_move_select";
  ot->description = "Move the cursor while selecting";

  /* api callbacks */
  ot->ex = text_move_select_ex;
  ot->poll = text_space_edit_poll;

  /* properties */
  api_def_enum(ot->sapi,
               "type",
               move_type_items,
               LINE_BEGIN,
               "Type",
               "Where to move cursor to, to make a selection");
}

/* Jump Operator */
static int text_jump_ex(Cxt *C, wmOp *op)
{
  Text *text = cxt_data_edit_text(C);
  int line = api_int_get(op->ptr, "line");
  short nlines = txt_get_span(static_cast<TextLine *>(text->lines.first),
                              static_cast<TextLine *>(text->lines.last)) +
                 1;

  if (line < 1) {
    txt_move_toline(text, 1, false);
  }
  else if (line > nlines) {
    txt_move_toline(text, nlines - 1, false);
  }
  else {
    txt_move_toline(text, line - 1, false);
  }

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, text);

  return OP_FINISHED;
}

static int text_jump_invoke(Cxt *C, wmOp *op, const wmEvent * /*event*/)
{
  return wm_op_props_dialog_popup(C, op, 200);
}

void TEXT_OT_jump(wmOpType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Jump";
  ot->idname = "TEXT_OT_jump";
  ot->description = "Jump cursor to line";

  /* api callbacks */
  ot->invoke = text_jump_invoke;
  ot->ex = text_jump_ex;
  ot->poll = text_edit_poll;

  /* props */
  prop = api_def_int(ot->sapi, "line", 1, 1, INT_MAX, "Line", "Line number to jump to", 1, 10000);
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_TEXT);
}

/* Delete Operator */
static const EnumPropItem delete_type_items[] = {
    {DEL_NEXT_CHAR, "NEXT_CHARACTER", 0, "Next Character", ""},
    {DEL_PREV_CHAR, "PREVIOUS_CHARACTER", 0, "Previous Character", ""},
    {DEL_NEXT_WORD, "NEXT_WORD", 0, "Next Word", ""},
    {DEL_PREV_WORD, "PREVIOUS_WORD", 0, "Previous Word", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static int text_delete_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  int type = api_enum_get(op->ptr, "type");

  text_drawcache_tag_update(st, true);

  /* behavior could be changed here,
   * but for now just don't jump words when we have a selection */
  if (txt_has_sel(text)) {
    if (type == DEL_PREV_WORD) {
      type = DEL_PREV_CHAR;
    }
    else if (type == DEL_NEXT_WORD) {
      type = DEL_NEXT_CHAR;
    }
  }

  ed_text_undo_push_init(C);

  if (type == DEL_PREV_WORD) {
    if (txt_cursor_is_line_start(text)) {
      txt_backspace_char(text);
    }
    txt_backspace_word(text);
  }
  else if (type == DEL_PREV_CHAR) {

    if (text->flags & TXT_TABSTOSPACES) {
      if (!txt_has_sel(text) && !txt_cursor_is_line_start(text)) {
        int tabsize = 0;
        tabsize = txt_calc_tab_left(text->curl, text->curc);
        if (tabsize) {
          text->sell = text->curl;
          text->selc = text->curc - tabsize;
          txt_order_cursors(text, false);
        }
      }
    }
    if (U.text_flag & USER_TEXT_EDIT_AUTO_CLOSE) {
      const char *curr = text->curl->line + text->curc;
      if (*curr != '\0') {
        const char *prev = lib_str_find_prev_char_utf8(curr, text->curl->line);
        if ((curr != prev) && /* When back-spacing from the start of the line. */
            (*curr == text_closing_character_pair_get(*prev)))
        {
          txt_move_right(text, false);
          txt_backspace_char(text);
        }
      }
    }
    txt_backspace_char(text);
  }
  else if (type == DEL_NEXT_WORD) {
    if (txt_cursor_is_line_end(text)) {
      txt_delete_char(text);
    }
    txt_delete_word(text);
  }
  else if (type == DEL_NEXT_CHAR) {

    if (text->flags & TXT_TABSTOSPACES) {
      if (!txt_has_sel(text) && !txt_cursor_is_line_end(text)) {
        int tabsize = 0;
        tabsize = txt_calc_tab_right(text->curl, text->curc);
        if (tabsize) {
          text->sell = text->curl;
          text->selc = text->curc + tabsize;
          txt_order_cursors(text, true);
        }
      }
    }

    txt_delete_char(text);
  }

  text_update_line_edited(text->curl);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  /* run the script while editing, evil but useful */
  if (st->live_edit) {
    text_run_script(C, nullptr);
  }

  return OP_FINISHED;
}

void TEXT_OT_delete(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Delete";
  ot->idname = "TEXT_OT_delete";
  ot->description = "Delete text by cursor position";

  /* api callbacks */
  ot->ex = text_delete_ex;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  AphProp *prop;
  prop = api_def_enum(ot->sapu,
                      "type",
                      delete_type_items,
                      DEL_NEXT_CHAR,
                      "Type",
                      "Which part of the text to delete");
  api_def_prop_flag(prop, PropertyFlag(PROP_HIDDEN | PROP_SKIP_SAVE));
}

/* Toggle Overwrite Op */
static int text_toggle_overwrite_ex(Cxt *C, wmOp * /*op*/)
{
  SpaceText *st = cxt_wm_space_text(C);

  st->overwrite = !st->overwrite;

  wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, st->text);

  return OP_FINISHED;
}

void TEXT_OT_overwrite_toggle(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Toggle Overwrite";
  ot->idname = "TEXT_OT_overwrite_toggle";
  ot->description = "Toggle overwrite while typing";

  /* api callbacks */
  ot->ex = text_toggle_overwrite_ex;
  ot->poll = text_space_edit_poll;
}

/* Scroll Op*/
static void txt_screen_clamp(SpaceText *st, ARegion *region)
{
  if (st->top <= 0) {
    st->top = 0;
  }
  else {
    int last;
    last = text_get_total_lines(st, region);
    last = last - (st->runtime.viewlines / 2);
    if (last > 0 && st->top > last) {
      st->top = last;
    }
  }
}

/* Moves the view vertically by the specified number of lines */
static void txt_screen_skip(SpaceText *st, ARegion *region, int lines)
{
  st->top += lines;
  txt_screen_clamp(st, region);
}

/* quick enum for tsc->zone (scroller handles) */
enum eScrollZone {
  SCROLLHANDLE_INVALID_OUTSIDE = -1,
  SCROLLHANDLE_BAR,
  SCROLLHANDLE_MIN_OUTSIDE,
  SCROLLHANDLE_MAX_OUTSIDE,
};

struct TextScroll {
  int mval_prev[2];
  int mval_delta[2];

  bool is_first;
  bool is_scrollbar;

  enum eScrollZone zone;

  /* Store the state of the display, cache some constant vars. */
  struct {
    int ofs_init[2];
    int ofs_max[2];
    int size_px[2];
  } state;
  int ofs_delta[2];
  int ofs_delta_px[2];
};

static void text_scroll_state_init(TextScroll *tsc, SpaceText *st, ARegion *region)
{
  tsc->state.ofs_init[0] = st->left;
  tsc->state.ofs_init[1] = st->top;

  tsc->state.ofs_max[0] = INT_MAX;
  tsc->state.ofs_max[1] = max_ii(0,
                                 text_get_total_lines(st, region) - (st->runtime.viewlines / 2));

  tsc->state.size_px[0] = st->runtime.cwidth_px;
  tsc->state.size_px[1] = TXT_LINE_HEIGHT(st);
}

static bool text_scroll_poll(Cxt *C)
{
  /* it should be possible to still scroll linked texts to read them,
   * even if they can't be edited... */
  return cxt_data_edit_text(C) != nullptr;
}

static int text_scroll_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  ARegion *region = cxt_wm_region(C);

  int lines = api_int_get(op->ptr, "lines");

  if (lines == 0) {
    return OP_CANCELLED;
  }

  txt_screen_skip(st, region, lines * 3);

  ed_area_tag_redraw(cxt_wm_area(C));

  return OP_FINISHED;
}

static void text_scroll_apply(Cxt *C, wmOp *op, const wmEvent *event)
{
  SpaceText *st = cxt_wm_space_text(C);
  TextScroll *tsc = static_cast<TextScroll *>(op->customdata);
  const int mval[2] = {event->xy[0], event->xy[1]};

  text_update_character_width(st);

  /* compute mouse move distance */
  if (tsc->is_first) {
    copy_v2_v2_int(tsc->mval_prev, mval);
    tsc->is_first = false;
  }

  if (event->type != MOUSEPAN) {
    sub_v2_v2v2_int(tsc->mval_delta, mval, tsc->mval_prev);
  }

  /* accumulate scroll, in float values for events that give less than one
   * line offset but taken together should still scroll */
  if (!tsc->is_scrollbar) {
    tsc->ofs_delta_px[0] -= tsc->mval_delta[0];
    tsc->ofs_delta_px[1] += tsc->mval_delta[1];
  }
  else {
    tsc->ofs_delta_px[1] -= (tsc->mval_delta[1] * st->runtime.scroll_px_per_line) *
                            tsc->state.size_px[1];
  }

  for (int i = 0; i < 2; i += 1) {
    int lines_from_pixels = tsc->ofs_delta_px[i] / tsc->state.size_px[i];
    tsc->ofs_delta[i] += lines_from_pixels;
    tsc->ofs_delta_px[i] -= lines_from_pixels * tsc->state.size_px[i];
  }

  /* The final values need to be calculated from the inputs,
   * so clamping and ensuring an unsigned pixel offset doesn't conflict with
   * updating the cursor mval_delta. */
  int scroll_ofs_new[2] = {
      tsc->state.ofs_init[0] + tsc->ofs_delta[0],
      tsc->state.ofs_init[1] + tsc->ofs_delta[1],
  };
  int scroll_ofs_px_new[2] = {
      tsc->ofs_delta_px[0],
      tsc->ofs_delta_px[1],
  };

  for (int i = 0; i < 2; i += 1) {
    /* Ensure always unsigned (adjusting line/column accordingly). */
    while (scroll_ofs_px_new[i] < 0) {
      scroll_ofs_px_new[i] += tsc->state.size_px[i];
      scroll_ofs_new[i] -= 1;
    }

    /* Clamp within usable region. */
    if (scroll_ofs_new[i] < 0) {
      scroll_ofs_new[i] = 0;
      scroll_ofs_px_new[i] = 0;
    }
    else if (scroll_ofs_new[i] >= tsc->state.ofs_max[i]) {
      scroll_ofs_new[i] = tsc->state.ofs_max[i];
      scroll_ofs_px_new[i] = 0;
    }
  }

  /* Override for word-wrap. */
  if (st->wordwrap) {
    scroll_ofs_new[0] = 0;
    scroll_ofs_px_new[0] = 0;
  }

  /* Apply to the screen. */
  if (scroll_ofs_new[0] != st->left || scroll_ofs_new[1] != st->top ||
      /* Horizontal sub-pixel offset currently isn't used. */
      /* scroll_ofs_px_new[0] != st->scroll_ofs_px[0] || */
      scroll_ofs_px_new[1] != st->runtime.scroll_ofs_px[1])
  {

    st->left = scroll_ofs_new[0];
    st->top = scroll_ofs_new[1];
    st->runtime.scroll_ofs_px[0] = scroll_ofs_px_new[0];
    st->runtime.scroll_ofs_px[1] = scroll_ofs_px_new[1];
    ed_area_tag_redraw(cxt_wm_area(C));
  }

  tsc->mval_prev[0] = mval[0];
  tsc->mval_prev[1] = mval[1];
}

static void scroll_exit(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  TextScroll *tsc = static_cast<TextScroll *>(op->customdata);

  st->flags &= ~ST_SCROLL_SELECT;

  if (st->runtime.scroll_ofs_px[1] > tsc->state.size_px[1] / 2) {
    st->top += 1;
  }

  st->runtime.scroll_ofs_px[0] = 0;
  st->runtime.scroll_ofs_px[1] = 0;
  ed_area_tag_redraw(cxt_wm_area(C));

  mem_freen(op->customdata);
}

static int text_scroll_modal(Cxt *C, wmOp *op, const wmEvent *event)
{
  TextScroll *tsc = static_cast<TextScroll *>(op->customdata);
  SpaceText *st = cxt_wm_space_text(C);
  ARegion *region = cxt_wm_region(C);

  switch (event->type) {
    case MOUSEMOVE:
      if (tsc->zone == SCROLLHANDLE_BAR) {
        text_scroll_apply(C, op, event);
      }
      break;
    case LEFTMOUSE:
    case RIGHTMOUSE:
    case MIDDLEMOUSE:
      if (event->val == KM_RELEASE) {
        if (ELEM(tsc->zone, SCROLLHANDLE_MIN_OUTSIDE, SCROLLHANDLE_MAX_OUTSIDE)) {
          txt_screen_skip(st,
                          region,
                          st->runtime.viewlines *
                              (tsc->zone == SCROLLHANDLE_MIN_OUTSIDE ? 1 : -1));

          ed_area_tag_redraw(cxt_wm_area(C));
        }
        scroll_exit(C, op);
        return OP_FINISHED;
      }
  }

  return OP_RUNNING_MODAL;
}

static void text_scroll_cancel(Cxt *C, wmOp *op)
{
  scroll_exit(C, op);
}

static int text_scroll_invoke(Cxt *C, wmOp *op, const wmEvent *event)
{
  SpaceText *st = cxt_wm_space_text(C);
  ARegion *region = cxt_wm_region(C);

  TextScroll *tsc;

  if (api_struct_prop_is_set(op->ptr, "lines")) {
    return text_scroll_ex(C, op);
  }

  tsc = static_cast<TextScroll *>(mem_callocn(sizeof(TextScroll), "TextScroll"));
  tsc->is_first = true;
  tsc->zone = SCROLLHANDLE_BAR;

  text_scroll_state_init(tsc, st, region);

  op->customdata = tsc;

  st->flags |= ST_SCROLL_SELECT;

  if (event->type == MOUSEPAN) {
    text_update_character_width(st);

    copy_v2_v2_int(tsc->mval_prev, event->xy);
    /* Sensitivity of scroll set to 4pix per line/char */
    tsc->mval_delta[0] = (event->xy[0] - event->prev_xy[0]) * st->runtime.cwidth_px / 4;
    tsc->mval_delta[1] = (event->xy[1] - event->prev_xy[1]) * st->runtime.lheight_px / 4;
    tsc->is_first = false;
    tsc->is_scrollbar = false;
    text_scroll_apply(C, op, event);
    scroll_exit(C, op);
    return OP_FINISHED;
  }

  wm_event_add_modal_handler(C, op);

  return OP_RUNNING_MODAL;
}

void TEXT_OT_scroll(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Scroll";
  /* don't really see the difference between this and
   * scroll_bar. Both do basically the same thing (aside from key-maps). */
  ot->idname = "TEXT_OT_scroll";

  /* api callbacks */
  ot->ex = text_scroll_ex;
  ot->invoke = text_scroll_invoke;
  ot->modal = text_scroll_modal;
  ot->cancel = text_scroll_cancel;
  ot->poll = text_scroll_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_GRAB_CURSOR_XY | OPTYPE_INTERNAL;

  /* properties */
  ApiProp *prop;
  prop = api_def_int(
      ot->sapi, "lines", 1, INT_MIN, INT_MAX, "Lines", "Number of lines to scroll", -100, 100);
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_TEXT);
}

/* Scroll Bar Operator */
static bool text_region_scroll_poll(Cxt *C)
{
  /* same as text_region_edit_poll except it works on libdata too */
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  ARegion *region = cxt_wm_region(C);

  if (!st || !text) {
    return false;
  }

  if (!region || region->regiontype != RGN_TYPE_WINDOW) {
    return false;
  }

  return true;
}

static int text_scroll_bar_invoke(Cxt *C, wmOp *op, const wmEvent *event)
{
  SpaceText *st = cxt_wm_space_text(C);
  ARegion *region = cxt_wm_region(C);
  TextScroll *tsc;
  const int *mval = event->mval;
  enum eScrollZone zone = SCROLLHANDLE_INVALID_OUTSIDE;

  if (api_struct_prop_is_set(op->ptr, "lines")) {
    return text_scroll_ex(C, op);
  }

  /* verify we are in the right zone */
  if (mval[0] > st->runtime.scroll_region_handle.xmin &&
      mval[0] < st->runtime.scroll_region_handle.xmax)
  {
    if (mval[1] >= st->runtime.scroll_region_handle.ymin &&
        mval[1] <= st->runtime.scroll_region_handle.ymax)
    {
      /* mouse inside scroll handle */
      zone = SCROLLHANDLE_BAR;
    }
    else if (mval[1] > TXT_SCROLL_SPACE && mval[1] < region->winy - TXT_SCROLL_SPACE) {
      if (mval[1] < st->runtime.scroll_region_handle.ymin) {
        zone = SCROLLHANDLE_MIN_OUTSIDE;
      }
      else {
        zone = SCROLLHANDLE_MAX_OUTSIDE;
      }
    }
  }

  if (zone == SCROLLHANDLE_INVALID_OUTSIDE) {
    /* we are outside slider - nothing to do */
    return OP_PASS_THROUGH;
  }

  tsc = static_cast<TextScroll *>(MEM_callocN(sizeof(TextScroll), "TextScroll"));
  tsc->is_first = true;
  tsc->is_scrollbar = true;
  tsc->zone = zone;
  op->customdata = tsc;
  st->flags |= ST_SCROLL_SELECT;

  text_scroll_state_init(tsc, st, region);

  /* jump scroll, works in v2d but needs to be added here too :S */
  if (event->type == MIDDLEMOUSE) {
    tsc->mval_prev[0] = region->winrct.xmin + BLI_rcti_cent_x(&st->runtime.scroll_region_handle);
    tsc->mval_prev[1] = region->winrct.ymin + BLI_rcti_cent_y(&st->runtime.scroll_region_handle);

    tsc->is_first = false;
    tsc->zone = SCROLLHANDLE_BAR;
    text_scroll_apply(C, op, event);
  }

  wm_event_add_modal_handler(C, op);

  return OP_RUNNING_MODAL;
}

void TEXT_OT_scroll_bar(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Scrollbar";
  /* don't really see the difference between this and
   * scroll. Both do basically the same thing (aside from key-maps). */
  ot->idname = "TEXT_OT_scroll_bar";

  /* api callbacks */
  ot->invoke = text_scroll_bar_invoke;
  ot->modal = text_scroll_modal;
  ot->cancel = text_scroll_cancel;
  ot->poll = text_region_scroll_poll;

  /* flags */
  ot->flag = OPTYPE_BLOCKING | OPTYPE_INTERNAL;

  /* properties */
  ApiProp *prop;
  prop = api_def_int(
      ot->sapi, "lines", 1, INT_MIN, INT_MAX, "Lines", "Number of lines to scroll", -100, 100);
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_TEXT);
}

/* Set Selection Op */
struct SetSelection {
  int selc, sell;
  short mval_prev[2];
  wmTimer *timer; /* needed for scrolling when mouse at region bounds */
};

static int flatten_width(SpaceText *st, const char *str)
{
  int total = 0;

  for (int i = 0; str[i]; i += lib_str_utf8_size_safe(str + i)) {
    if (str[i] == '\t') {
      total += st->tabnumber - total % st->tabnumber;
    }
    else {
      total += lib_str_utf8_char_width_safe(str + i);
    }
  }

  return total;
}

static int flatten_column_to_offset(SpaceText *st, const char *str, int index)
{
  int i = 0, j = 0, col;

  while (*(str + j)) {
    if (str[j] == '\t') {
      col = st->tabnumber - i % st->tabnumber;
    }
    else {
      col = lib_str_utf8_char_width_safe(str + j);
    }

    if (i + col > index) {
      break;
    }

    i += col;
    j += lib_str_utf8_size_safe(str + j);
  }

  return j;
}

static TextLine *get_line_pos_wrapped(SpaceText *st, ARegion *region, int *y)
{
  TextLine *linep = static_cast<TextLine *>(st->text->lines.first);
  int i, lines;

  if (*y < -st->top) {
    return nullptr; /* We are beyond the first line... */
  }

  for (i = -st->top; i <= *y && linep; linep = linep->next, i += lines) {
    lines = text_get_visible_lines(st, region, linep->line);

    if (i + lines > *y) {
      /* We found the line matching given vertical 'coordinate',
       * now set y relative to this line's start. */
      *y -= i;
      break;
    }
  }
  return linep;
}

static void text_cursor_set_to_pos_wrapped(
    SpaceText *st, ARegion *region, int x, int y, const bool sel)
{
  Text *text = st->text;
  int max = wrap_width(st, region); /* column */
  int charp = -1;                   /* mem */
  bool found = false;               /* flags */

  /* Point to line matching given y position, if any. */
  TextLine *linep = get_line_pos_wrapped(st, region, &y);

  if (linep) {
    int i = 0, start = 0, end = max; /* column */
    int j, curs = 0, endj = 0;       /* mem */
    bool chop = true;                /* flags */
    char ch;

    for (j = 0; !found && ((ch = linep->line[j]) != '\0');
         j += lib_str_utf8_size_safe(linep->line + j))
    {
      int chars;
      int columns = lib_str_utf8_char_width_safe(linep->line + j); /* = 1 for tab */

      /* Mimic replacement of tabs */
      if (ch == '\t') {
        chars = st->tabnumber - i % st->tabnumber;
        ch = ' ';
      }
      else {
        chars = 1;
      }

      while (chars--) {
        /* Gone too far, go back to last wrap point */
        if (y < 0) {
          charp = endj;
          y = 0;
          found = true;
          break;
          /* Exactly at the cursor */
        }
        if (y == 0 && i - start <= x && i + columns - start > x) {
          /* current position could be wrapped to next line */
          /* this should be checked when end of current line would be reached */
          charp = curs = j;
          found = true;
          /* Prepare curs for next wrap */
        }
        else if (i - end <= x && i + columns - end > x) {
          curs = j;
        }
        if (i + columns - start > max) {
          end = MIN2(end, i);

          if (found) {
            /* exact cursor position was found, check if it's still on needed line
             * (hasn't been wrapped) */
            if (charp > endj && !chop && ch != '\0') {
              charp = endj;
            }
            break;
          }

          if (chop) {
            endj = j;
          }
          start = end;
          end += max;

          if (j < linep->len) {
            y--;
          }

          chop = true;
          if (y == 0 && i + columns - start > x) {
            charp = curs;
            found = true;
            break;
          }
        }
        else if (ELEM(ch, ' ', '-', '\0')) {
          if (found) {
            break;
          }

          if (y == 0 && i + columns - start > x) {
            charp = curs;
            found = true;
            break;
          }
          end = i + 1;
          endj = j;
          chop = false;
        }
        i += columns;
      }
    }

    lib_assert(y == 0);

    if (!found) {
      /* On correct line but didn't meet cursor, must be at end */
      charp = linep->len;
    }
  }
  else if (y < 0) { /* Before start of text. */
    linep = static_cast<TextLine *>(st->text->lines.first);
    charp = 0;
  }
  else { /* Beyond end of text */
    linep = static_cast<TextLine *>(st->text->lines.last);
    charp = linep->len;
  }

  lib_assert(linep && charp != -1);

  if (sel) {
    text->sell = linep;
    text->selc = charp;
  }
  else {
    text->curl = linep;
    text->curc = charp;
  }
}

static void text_cursor_set_to_pos(SpaceText *st, ARegion *region, int x, int y, const bool sel)
{
  Text *text = st->text;
  text_update_character_width(st);
  y = (region->winy - 2 - y) / TXT_LINE_HEIGHT(st);

  x -= TXT_BODY_LEFT(st);
  if (x < 0) {
    x = 0;
  }
  x = text_pixel_x_to_column(st, x) + st->left;

  if (st->wordwrap) {
    text_cursor_set_to_pos_wrapped(st, region, x, y, sel);
  }
  else {
    TextLine **linep;
    int *charp;
    int w;

    if (sel) {
      linep = &text->sell;
      charp = &text->selc;
    }
    else {
      linep = &text->curl;
      charp = &text->curc;
    }

    y -= txt_get_span(static_cast<TextLine *>(text->lines.first), *linep) - st->top;

    if (y > 0) {
      while (y-- != 0) {
        if ((*linep)->next) {
          *linep = (*linep)->next;
        }
      }
    }
    else if (y < 0) {
      while (y++ != 0) {
        if ((*linep)->prev) {
          *linep = (*linep)->prev;
        }
      }
    }

    w = flatten_width(st, (*linep)->line);
    if (x < w) {
      *charp = flatten_column_to_offset(st, (*linep)->line, x);
    }
    else {
      *charp = (*linep)->len;
    }
  }
  if (!sel) {
    txt_pop_sel(text);
  }
}

static void text_cursor_timer_ensure(Cxt *C, SetSelection *ssel)
{
  if (ssel->timer == nullptr) {
    WM *wm = cxt_wm_manager(C);
    Window *win = cxt_wm_window(C);

    ssel->timer = wm_event_timer_add(wm, win, TIMER, 0.02f);
  }
}

static void text_cursor_timer_remove(Cxt *C, SetSelection *ssel)
{
  if (ssel->timer) {
    WM *wm = cxt_wm_manager(C);
    Window *win = cxt_wm_window(C);

    wm_event_timer_remove(wm, win, ssel->timer);
  }
  ssel->timer = nullptr;
}

static void text_cursor_set_apply(Cxt *C, wmOp *op, const wmEvent *event)
{
  SpaceText *st = cxt_wm_space_text(C);
  ARegion *region = cxt_wm_region(C);
  SetSelection *ssel = static_cast<SetSelection *>(op->customdata);

  if (event->mval[1] < 0 || event->mval[1] > region->winy) {
    text_cursor_timer_ensure(C, ssel);

    if (event->type == TIMER) {
      text_cursor_set_to_pos(st, region, event->mval[0], event->mval[1], true);
      ed_text_scroll_to_cursor(st, region, false);
      wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, st->text);
    }
  }
  else if (!st->wordwrap && (event->mval[0] < 0 || event->mval[0] > region->winx)) {
    text_cursor_timer_ensure(C, ssel);

    if (event->type == TIMER) {
      text_cursor_set_to_pos(
          st, region, CLAMPIS(event->mval[0], 0, region->winx), event->mval[1], true);
      ed_text_scroll_to_cursor(st, region, false);
      wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, st->text);
    }
  }
  else {
    text_cursor_timer_remove(C, ssel);

    if (event->type != TIMER) {
      text_cursor_set_to_pos(st, region, event->mval[0], event->mval[1], true);
      ed_text_scroll_to_cursor(st, region, false);
      wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, st->text);

      ssel->mval_prev[0] = event->mval[0];
      ssel->mval_prev[1] = event->mval[1];
    }
  }
}

static void text_cursor_set_exit(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  SetSelection *ssel = static_cast<SetSelection *>(op->customdata);

  text_update_cursor_moved(C);
  text_select_update_primary_clipboard(st->text);

  wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, st->text);

  text_cursor_timer_remove(C, ssel);
  mem_freen(ssel);
}

static int text_selection_set_invoke(Cxt *C, wmOp *op, const wmEvent *event)
{
  SpaceText *st = cxt_wm_space_text(C);
  SetSelection *ssel;

  if (event->mval[0] >= st->runtime.scroll_region_handle.xmin) {
    return OP_PASS_THROUGH;
  }

  op->customdata = mem_callocn(sizeof(SetSelection), "SetCursor");
  ssel = static_cast<SetSelection *>(op->customdata);

  ssel->mval_prev[0] = event->mval[0];
  ssel->mval_prev[1] = event->mval[1];

  ssel->sell = txt_get_span(static_cast<TextLine *>(st->text->lines.first), st->text->sell);
  ssel->selc = st->text->selc;

  wm_event_add_modal_handler(C, op);

  text_cursor_set_apply(C, op, event);

  return OP_RUNNING_MODAL;
}

static int text_selection_set_modal(Cxt *C, wmOp *op, const wmEvent *event)
{
  switch (event->type) {
    case LEFTMOUSE:
    case MIDDLEMOUSE:
    case RIGHTMOUSE:
      text_cursor_set_exit(C, op);
      return OP_FINISHED;
    case TIMER:
    case MOUSEMOVE:
      text_cursor_set_apply(C, op, event);
      break;
  }

  return OP_RUNNING_MODAL;
}

static void text_selection_set_cancel(Cxt *C, wmOp *op)
{
  text_cursor_set_exit(C, op);
}

void TEXT_OT_selection_set(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Set Selection";
  ot->idname = "TEXT_OT_selection_set";
  ot->description = "Set text selection";

  /* api callbacks */
  ot->invoke = text_selection_set_invoke;
  ot->modal = text_selection_set_modal;
  ot->cancel = text_selection_set_cancel;
  ot->poll = text_region_edit_poll;
}

/* Set Cursor Operator */
static int text_cursor_set_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  ARegion *region = cxt_wm_region(C);
  int x = api_int_get(op->ptr, "x");
  int y = api_int_get(op->ptr, "y");

  text_cursor_set_to_pos(st, region, x, y, false);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, st->text);

  return OP_PASS_THROUGH;
}

static int text_cursor_set_invoke(Cxt *C, wmOp *op, const wmEvent *event)
{
  SpaceText *st = cxt_wm_space_text(C);

  if (event->mval[0] >= st->runtime.scroll_region_handle.xmin) {
    return OP_PASS_THROUGH;
  }

  api_int_set(op->ptr, "x", event->mval[0]);
  api_int_set(op->ptr, "y", event->mval[1]);

  return text_cursor_set_ex(C, op);
}

void TEXT_OT_cursor_set(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Set Cursor";
  ot->idname = "TEXT_OT_cursor_set";
  ot->description = "Set cursor position";

  /* api callbacks */
  ot->invoke = text_cursor_set_invoke;
  ot->ex = text_cursor_set_ex;
  ot->poll = text_region_edit_poll;

  /* properties */
  api_def_int(ot->sapi, "x", 0, INT_MIN, INT_MAX, "X", "", INT_MIN, INT_MAX);
  api_def_int(ot->sapi, "y", 0, INT_MIN, INT_MAX, "Y", "", INT_MIN, INT_MAX);
}

/* Line Number Operator */
static int text_line_number_invoke(Cxt *C, wmOp * /*op*/, const wmEvent *event)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  ARegion *region = cxt_wm_region(C);
  const int *mval = event->mval;
  double time;
  static int jump_to = 0;
  static double last_jump = 0;

  text_update_character_width(st);

  if (!st->showlinenrs) {
    return OP_PASS_THROUGH;
  }

  if (!(mval[0] > 2 &&
        mval[0] < (TXT_NUMCOL_WIDTH(st) + (TXT_BODY_LPAD * st->runtime.cwidth_px)) &&
        mval[1] > 2 && mval[1] < region->winy - 2))
  {
    return OP_PASS_THROUGH;
  }

  const char event_ascii = wm_event_utf8_to_ascii(event);
  if (!(event_ascii >= '0' && event_ascii <= '9')) {
    return OP_PASS_THROUGH;
  }

  time = PIL_check_seconds_timer();
  if (last_jump < time - 1) {
    jump_to = 0;
  }

  jump_to *= 10;
  jump_to += int(event_ascii - '0');

  txt_move_toline(text, jump_to - 1, false);
  last_jump = time;

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, text);

  return OP_FINISHED;
}

void TEXT_OT_line_number(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Line Number";
  ot->idname = "TEXT_OT_line_number";
  ot->description = "The current line number";

  /* api callbacks */
  ot->invoke = text_line_number_invoke;
  ot->poll = text_region_edit_poll;
}

/* Insert Operator */
static int text_insert_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  char *str;
  int str_len;
  bool done = false;
  size_t i = 0;
  uint code;

  text_drawcache_tag_update(st, false);

  str = api_string_get_alloc(op->ptr, "text", nullptr, 0, &str_len);

  ed_text_undo_push_op_init(C);

  if (st && st->overwrite) {
    while (str[i]) {
      code = lib_str_utf8_as_unicode_step(str, str_len, &i);
      done |= txt_replace_char(text, code);
    }
  }
  else {
    while (str[i]) {
      code = lib_str_utf8_as_unicode_step(str, str_len, &i);
      done |= txt_add_char(text, code);
    }
  }

  mem_freen(str);

  if (!done) {
    return OP_CANCELLED;
  }

  text_update_line_edited(text->curl);

  text_update_cursor_moved(C);
  wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);

  return OP_FINISHED;
}

static int text_insert_invoke(Cxt *C, wmOp *op, const wmEvent *event)
{
  SpaceText *st = cxt_wm_space_text(C);
  uint auto_close_char = 0;
  int ret;

  /* NOTE: the "text" prop is always set from key-map,
   * so we can't use api_struct_prop_is_set, check the length instead. */
  if (!api_string_length(op->ptr, "text")) {
    /* If Alt/Control/Super are pressed pass through except for utf8 character event
     * (when input method are used for utf8 inputs, the user may assign key event
     * including Alt/Control/Super like Control-M to commit utf8 string.
     * In such case, the modifiers in the utf8 character event make no sense). */
    if ((event->mod & (KM_CTRL | KM_OSKEY)) && !event->utf8_buf[0]) {
      return OP_PASS_THROUGH;
    }

    char str[LIB_UTF8_MAX + 1];
    const size_t len = lib_str_utf8_size_safe(event->utf8_buf);
    memcpy(str, event->utf8_buf, len);
    str[len] = '\0';
    api_string_set(op->ptr, "text", str);

    if (U.text_flag & USER_TEXT_EDIT_AUTO_CLOSE) {
      auto_close_char = lib_str_utf8_as_unicode(str);
    }
  }

  ret = text_insert_ex(C, op);

  if ((ret == OP_FINISHED) && (auto_close_char != 0)) {
    const uint auto_close_match = text_closing_character_pair_get(auto_close_char);
    if (auto_close_match != 0) {
      Text *text = cxt_data_edit_text(C);
      txt_add_char(text, auto_close_match);
      txt_move_left(text, false);
    }
  }

  /* run the script while editing, evil but useful */
  if (ret == OP_FINISHED && st->live_edit) {lol
    text_run_script(C, nullptr);
  }

  return ret;
}

void TEXT_OT_insert(wmOpType *ot)
{
  ApiProp *prop;

  /* identifiers */
  ot->name = "Insert";
  ot->idname = "TEXT_OT_insert";
  ot->description = "Insert text at cursor position";

  /* api callbacks */
  ot->ex = text_insert_ex;
  ot->invoke = text_insert_invoke;
  ot->poll = text_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  prop = api_def_string(
      ot->sapi, "text", nullptr, 0, "Text", "Text to insert at the cursor position");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* Find Operator */
/* mode */
enum {
  TEXT_FIND = 0,
  TEXT_REPLACE = 1,
};

static int text_find_and_replace(Cxt *C, wmOp *op, short mode)
{
  Main *main = cxt_data_main(C);
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = st->text;
  int flags;
  int found = 0;
  char *tmp;

  if (!st->findstr[0]) {
    return OP_CANCELLED;
  }

  flags = st->flags;
  if (flags & ST_FIND_ALL) {
    flags &= ~ST_FIND_WRAP;
  }

  /* Replace current */
  if (mode != TEXT_FIND && txt_has_sel(text)) {
    tmp = txt_sel_to_buf(text, nullptr);

    if (flags & ST_MATCH_CASE) {
      found = STREQ(st->findstr, tmp);
    }
    else {
      found = lib_strcasecmp(st->findstr, tmp) == 0;
    }

    if (found) {
      if (mode == TEXT_REPLACE) {
        ed_text_undo_push_init(C);
        txt_insert_buf(text, st->replacestr, strlen(st->replacestr));
        if (text->curl && text->curl->format) {
          mem_freen(text->curl->format);
          text->curl->format = nullptr;
        }
        text_update_cursor_moved(C);
        wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);
        text_drawcache_tag_update(st, true);
      }
    }
    mem_freen(tmp);
    tmp = nullptr;
  }

  /* Find next */
  if (txt_find_string(text, st->findstr, flags & ST_FIND_WRAP, flags & ST_MATCH_CASE)) {
    text_update_cursor_moved(C);
    wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, text);
  }
  else if (flags & ST_FIND_ALL) {
    if (text->id.next) {
      text = st->text = static_cast<Text *>(text->id.next);
    }
    else {
      text = st->text = static_cast<Text *>(bmain->texts.first);
    }
    txt_move_toline(text, 0, false);
    text_update_cursor_moved(C);
    wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, text);
  }
  else {
    if (!found) {
      dune_reportf(op->reports, RPT_WARNING, "Text not found: %s", st->findstr);
    }
  }

  return OP_FINISHED;
}

static int text_find_ex(Cxt *C, wmOp *op)
{
  return text_find_and_replace(C, op, TEXT_FIND);
}

void TEXT_OT_find(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Find Next";
  ot->idname = "TEXT_OT_find";
  ot->description = "Find specified text";

  /* api callbacks */
  ot->ex = text_find_ex;
  ot->poll = text_space_edit_poll;
}

/* Replace Operator */
static int text_replace_all(Cxt *C)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = st->text;
  const int flags = st->flags;
  int found = 0;

  if (!st->findstr[0]) {
    return OP_CANCELLED;
  }

  const int orig_curl = lib_findindex(&text->lines, text->curl);
  const int orig_curc = text->curc;
  bool has_sel = txt_has_sel(text);

  txt_move_toline(text, 0, false);

  found = txt_find_string(text, st->findstr, 0, flags & ST_MATCH_CASE);
  if (found) {
    ed_text_undo_push_init(C);

    do {
      txt_insert_buf(text, st->replacestr, strlen(st->replacestr));
      if (text->curl && text->curl->format) {
        mem_freen(text->curl->format);
        text->curl->format = nullptr;
      }
      found = txt_find_string(text, st->findstr, 0, flags & ST_MATCH_CASE);
    } while (found);

    wm_event_add_notifier(C, NC_TEXT | NA_EDITED, text);
    text_drawcache_tag_update(st, true);
  }
  else {
    /* Restore position */
    txt_move_to(text, orig_curl, orig_curc, has_sel);
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

static int text_replace_ex(Cxt *C, wmOp *op)
{
  bool replace_all = api_bool_get(op->ptr, "all");
  if (replace_all) {
    return text_replace_all(C);
  }
  return text_find_and_replace(C, op, TEXT_REPLACE);
}

void TEXT_OT_replace(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Replace";
  ot->idname = "TEXT_OT_replace";
  ot->description = "Replace text with the specified text";

  /* api callbacks */
  ot->ex = text_replace_ex;
  ot->poll = text_space_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  ApiProp *prop;
  prop = api_def_bool(ot->sapi, "all", false, "Replace All", "Replace all occurrences");
  api_def_prop_flag(prop, PropertyFlag(PROP_HIDDEN | PROP_SKIP_SAVE));
}

/* Find Set Selected */
static int text_find_set_selected_ex(Cxt *C, wmOp *op)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  char *tmp;

  tmp = txt_sel_to_buf(text, nullptr);
  STRNCPY(st->findstr, tmp);
  mem_freen(tmp);

  if (!st->findstr[0]) {
    return OP_FINISHED;
  }

  return text_find_and_replace(C, op, TEXT_FIND);
}

void TEXT_OT_find_set_selected(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Find & Set Selection";
  ot->idname = "TEXT_OT_find_set_selected";
  ot->description = "Find specified text and set as selected";

  /* api callbacks */
  ot->ex = text_find_set_selected_ex;
  ot->poll = text_space_edit_poll;
}

/* Replace Set Selected */
static int text_replace_set_selected_exec(Cxt *C, wmOp * /*op*/)
{
  SpaceText *st = cxt_wm_space_text(C);
  Text *text = cxt_data_edit_text(C);
  char *tmp;

  tmp = txt_sel_to_buf(text, nullptr);
  STRNCPY(st->replacestr, tmp);
  MEM_freeN(tmp);

  return OP_FINISHED;
}

void TEXT_OT_replace_set_selected(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Replace & Set Selection";
  ot->idname = "TEXT_OT_replace_set_selected";
  ot->description = "Replace text with specified text and set as selected";

  /* api callbacks */
  ot->ex = text_replace_set_selected_ex;
  ot->poll = text_space_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* Jump to File at Point (Internal)
 * This is the internal implementation, typically `TEXT_OT_jump_to_file_at_point`
 * should be used because it respects the "External Editor" preference */

static int text_jump_to_file_at_point_internal_exec(Cxt *C, wmOp *op)
{
  char filepath[FILE_MAX];
  api_string_get(op->ptr, "filepath", filepath);
  const int line = api_int_get(op->ptr, "line");
  const int column = api_int_get(op->ptr, "column");

  Main *main = cxt_data_main(C);
  Text *text = nullptr;

  LIST_FOREACH (Text *, text_iter, &bmain->texts) {
    if (text_iter->filepath && lib_path_cmp(text_iter->filepath, filepath) == 0) {
      text = text_iter;
      break;
    }
  }

  if (text == nullptr) {
    text = dune_text_load(main, filepath, dune_main_dunefile_path(main));
  }

  if (text == nullptr) {
    dune_reportf(op->reports, RPT_WARNING, "File '%s' cannot be opened", filepath);
    return OP_CANCELLED;
  }

  txt_move_to(text, line, column, false);

  /* naughty!, find text area to set, not good behavior
   * but since this is a developer tool lets allow it - campbell */
  if (!ed_text_activate_in_screen(C, text)) {
    dune_reportf(op->reports, RPT_INFO, "See '%s' in the text editor", text->id.name + 2);
  }

  wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, text);

  return OP_FINISHED;
}

void TEXT_OT_jump_to_file_at_point_internal(wmOpType *ot)
{
  ApiProp *prop;

  /* identifiers */
  ot->name = "Jump to File at Point (Internal)";
  ot->idname = "TEXT_OT_jump_to_file_at_point_internal";
  ot->description = "Jump to a file for the internal text editor";

  /* api callbacks */
  ot->exec = text_jump_to_file_at_point_internal_exec;

  /* flags */
  ot->flag = 0;

  prop = api_def_string(ot->sapi, "filepath", nullptr, FILE_MAX, "Filepath", "");
  apio_def_prop_flag(prop, PropertyFlag(PROP_HIDDEN | PROP_SKIP_SAVE));
  prop = api_def_int(ot->sapi, "line", 0, 0, INT_MAX, "Line", "Line to jump to", 1, 10000);
  api_def_prop_flag(prop, PropertyFlag(PROP_HIDDEN | PROP_SKIP_SAVE));
  prop = api_def_int(ot->sapi, "column", 0, 0, INT_MAX, "Column", "Column to jump to", 1, 10000);
  api_def_prop_flag(prop, PropertyFlag(PROP_HIDDEN | PROP_SKIP_SAVE));
}

/* Resolve Conflict Operator */
enum { RESOLVE_IGNORE, RESOLVE_RELOAD, RESOLVE_SAVE, RESOLVE_MAKE_INTERNAL };
static const EnumPropItem resolution_items[] = {
    {RESOLVE_IGNORE, "IGNORE", 0, "Ignore", ""},
    {RESOLVE_RELOAD, "RELOAD", 0, "Reload", ""},
    {RESOLVE_SAVE, "SAVE", 0, "Save", ""},
    {RESOLVE_MAKE_INTERNAL, "MAKE_INTERNAL", 0, "Make Internal", ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static bool text_resolve_conflict_poll(Cxt *C)
{
  Text *text = cxt_data_edit_text(C);

  if (!text_edit_poll(C)) {
    return false;
  }

  return ((text->filepath != nullptr) && !(text->flags & TXT_ISMEM));
}

static int text_resolve_conflict_ex(Cxt *C, wmOp *op)
{
  Text *text = cxt_data_edit_text(C);
  int resolution = api_enum_get(op->ptr, "resolution");

  switch (resolution) {
    case RESOLVE_RELOAD:
      return text_reload_exec(C, op);
    case RESOLVE_SAVE:
      return text_save_exec(C, op);
    case RESOLVE_MAKE_INTERNAL:
      return text_make_internal_exec(C, op);
    case RESOLVE_IGNORE:
      dune_text_file_modified_ignore(text);
      return OP_FINISHED;
  }

  return OPERATOR_CANCELLED;
}

static int text_resolve_conflict_invoke(Cxt *C, wmOp *op, const wmEvent * /*event*/)
{
  Text *text = cxt_data_edit_text(C);
  uiPopupMenu *pup;
  uiLayout *layout;

  switch (dune_text_file_modified_check(text)) {
    case 1:
      if (text->flags & TXT_ISDIRTY) {
        /* Modified locally and externally, ah. offer more possibilities. */
        pup = UI_popup_menu_begin(
            C, IFACE_("File Modified Outside and Inside Blender"), ICON_NONE);
        layout = UI_popup_menu_layout(pup);
        uiItemEnumO_ptr(layout,
                        op->type,
                        IFACE_("Reload from disk (ignore local changes)"),
                        ICON_NONE,
                        "resolution",
                        RESOLVE_RELOAD);
        uiItemEnumO_ptr(layout,
                        op->type,
                        IFACE_("Save to disk (ignore outside changes)"),
                        ICON_NONE,
                        "resolution",
                        RESOLVE_SAVE);
        uiItemEnumO_ptr(layout,
                        op->type,
                        IFACE_("Make text internal (separate copy)"),
                        ICON_NONE,
                        "resolution",
                        RESOLVE_MAKE_INTERNAL);
        UI_popup_menu_end(C, pup);
      }
      else {
        pup = UI_popup_menu_begin(C, IFACE_("File Modified Outside Blender"), ICON_NONE);
        layout = UI_popup_menu_layout(pup);
        uiItemEnumO_ptr(
            layout, op->type, IFACE_("Reload from disk"), ICON_NONE, "resolution", RESOLVE_RELOAD);
        uiItemEnumO_ptr(layout,
                        op->type,
                        IFACE_("Make text internal (separate copy)"),
                        ICON_NONE,
                        "resolution",
                        RESOLVE_MAKE_INTERNAL);
        uiItemEnumO_ptr(
            layout, op->type, IFACE_("Ignore"), ICON_NONE, "resolution", RESOLVE_IGNORE);
        UI_popup_menu_end(C, pup);
      }
      break;
    case 2:
      pup = UI_popup_menu_begin(C, IFACE_("File Deleted Outside Blender"), ICON_NONE);
      layout = UI_popup_menu_layout(pup);
      uiItemEnumO_ptr(layout,
                      op->type,
                      IFACE_("Make text internal"),
                      ICON_NONE,
                      "resolution",
                      RESOLVE_MAKE_INTERNAL);
      uiItemEnumO_ptr(
          layout, op->type, IFACE_("Recreate file"), ICON_NONE, "resolution", RESOLVE_SAVE);
      UI_popup_menu_end(C, pup);
      break;
  }

  return OP_INTERFACE;
}

void TEXT_OT_resolve_conflict(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Resolve Conflict";
  ot->idname = "TEXT_OT_resolve_conflict";
  ot->description = "When external text is out of sync, resolve the conflict";

  /* api callbacks */
  ot->ex = text_resolve_conflict_ex;
  ot->invoke = text_resolve_conflict_invoke;
  ot->poll = text_resolve_conflict_poll;

  /* props */
  api_def_enum(ot->sapi,
               "resolution",
               resolution_items,
               RESOLVE_IGNORE,
               "Resolution",
               "How to solve conflict due to differences in internal and external text");
}

/* To 3D Object Operator */
static int text_to_3d_object_ex(Cxt *C, wmOp *op)
{
  const Text *text = cxt_data_edit_text(C);
  const bool split_lines = api_bool_get(op->ptr, "split_lines");

  ed_text_to_object(C, text, split_lines);

  return OP_FINISHED;
}

void TEXT_OT_to_3d_object(wmOpType *ot)
{
  /* identifiers */
  ot->name = "To 3D Object";
  ot->idname = "TEXT_OT_to_3d_object";
  ot->description = "Create 3D text object from active text data-block";

  /* api cbs */
  ot->ex = text_to_3d_object_ex;
  ot->poll = text_data_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_bool(
      ot->sapi, "split_lines", false, "Split Lines", "Create one object per line in the text");
}
