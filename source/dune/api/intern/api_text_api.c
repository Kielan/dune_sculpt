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

  fn = api_def_fn(sapi, "clear", "rna_Text_clear");
  api_def_fn_ui_description(fn, "clear the text block");

  fn = api_def_fn(sapi, "write", "rna_Text_write");
  api_def_fn_ui_description(
      fn, "write text at the cursor location and advance to the end of the text block");
  parm = api_def_string(fn, "text", "Text", 0, "", "New text for this data-block");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "from_string", "rna_Text_from_string");
  RNA_def_function_ui_description(func, "Replace text with this string.");
  parm = RNA_def_string(func, "text", "Text", 0, "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "as_string", "rna_Text_as_string");
  RNA_def_function_ui_description(func, "Return the text as a string");
  parm = RNA_def_string(func, "text", "Text", 0, "", "");
  RNA_def_parameter_flags(parm, PROP_DYNAMIC, PARM_OUTPUT);

  func = RNA_def_function(
      srna, "is_syntax_highlight_supported", "ED_text_is_syntax_highlight_supported");
  RNA_def_function_return(func,
                          RNA_def_boolean(func, "is_syntax_highlight_supported", false, "", ""));
  RNA_def_function_ui_description(func,
                                  "Returns True if the editor supports syntax highlighting "
                                  "for the current text datablock");

  func = RNA_def_function(srna, "select_set", "rna_Text_select_set");
  RNA_def_function_ui_description(func, "Set selection range by line and character index");
  parm = RNA_def_int(func, "line_start", 0, INT_MIN, INT_MAX, "Start Line", "", INT_MIN, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(
      func, "char_start", 0, INT_MIN, INT_MAX, "Start Character", "", INT_MIN, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "line_end", 0, INT_MIN, INT_MAX, "End Line", "", INT_MIN, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "char_end", 0, INT_MIN, INT_MAX, "End Character", "", INT_MIN, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "cursor_set", "rna_Text_cursor_set");
  RNA_def_function_ui_description(func, "Set cursor by line and (optionally) character index");
  parm = RNA_def_int(func, "line", 0, 0, INT_MAX, "Line", "", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "character", 0, 0, INT_MAX, "Character", "", 0, INT_MAX);
  RNA_def_boolean(func, "select", false, "", "Select when moving the cursor");
}

#endif
