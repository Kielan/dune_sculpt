#include <stdio.h>
#include <stdlib.h>

#include "lib_utildefines.h"

#include "ed_text.h"

#include "api_define.h"

#include "api_internal.h" /* own include */

#ifdef API_RUNTIME

#  include "wm_api.h"
#  include "wm_types.h"

static void api_Text_clear(Text *text)
{
  dune_text_clear(text);
  api_main_add_notifier(NC_TEXT | NA_EDITED, text);
}

static void api_Text_write(Text *text, const char *str)
{
  dune_text_write(text, str, strlen(str));
  wm_main_add_notifier(NC_TEXT | NA_EDITED, text);
}

static void api_Text_from_string(Text *text, const char *str)
{
  dune_text_clear(text);
  dune_text_write(text, str, strlen(str));
}

static void api_Text_as_string(Text *text, int *r_result_len, const char **result)
{
  size_t result_len;
  *result = txt_to_buf(text, &result_len);
  *r_result_len = result_len;
}

static void api_Text_select_set(Text *text, int startl, int startc, int endl, int endc)
{
  txt_sel_set(text, startl, startc, endl, endc);
  wm_main_add_notifier(NC_TEXT | NA_EDITED, text);
}

static void api_Text_cursor_set(Text *text, int line, int ch, bool select)
{
  txt_move_to(text, line, ch, select);
  wm_main_add_notifier(NC_TEXT | NA_EDITED, text);
}

#else

void api_text(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "clear", "api_Text_clear");
  api_def_fn_ui_description(fn, "clear the text block");

  fn = api_def_fn(sapi, "write", "api_Text_write");
  api_def_fn_ui_description(
      fn, "write text at the cursor location and advance to the end of the text block");
  parm = api_def_string(fn, "text", "Text", 0, "", "New text for this data-block");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "from_string", "api_Text_from_string");
  api_def_fn_ui_description(fn, "Replace text with this string.");
  parm = api_def_string(fn, "text", "Text", 0, "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "as_string", "api_Text_as_string");
  api_def_fn_ui_description(fn, "Return the text as a string");
  parm = api_def_string(fn, "text", "Text", 0, "", "");
  api_def_param_flags(parm, PROP_DYNAMIC, PARM_OUTPUT);

  fn = api_def_fn(
      sapi, "is_syntax_highlight_supported", "ed_text_is_syntax_highlight_supported");
  api_def_fn_return(fn, api_def_bool(fn, "is_syntax_highlight_supported", false, "", ""));
  api_def_fn_ui_description(fn,
                            "Returns True if the editor supports syntax highlighting "
                            "for the current text datablock");

  fn = api_def_fn(sapi, "select_set", api_Text_select_set");
  api_def_fn_ui_description(fn, "Set selection range by line and character index");
  parm = api_def_int(fn, "line_start", 0, INT_MIN, INT_MAX, "Start Line", "", INT_MIN, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(
      fn, "char_start", 0, INT_MIN, INT_MAX, "Start Character", "", INT_MIN, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "line_end", 0, INT_MIN, INT_MAX, "End Line", "", INT_MIN, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "char_end", 0, INT_MIN, INT_MAX, "End Character", "", INT_MIN, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "cursor_set", "api_Text_cursor_set");
  api_def_fn_ui_description(fn, "Set cursor by line and (optionally) character index");
  parm = api_def_int(fn, "line", 0, 0, INT_MAX, "Line", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "character", 0, 0, INT_MAX, "Character", "", 0, INT_MAX);
  api_def_bool(fn, "select", false, "", "Select when moving the cursor");
}

#endif
