#include <limits.h>
#include <stdlib.h>

#include "mem_guardedalloc.h"

#include "lang_translation.h"

#include "dune_text.h"

#include "ed_text.h"

#include "api_define.h"

#include "api_internal.h"

#include "api_text_types.h"

#include "wm_types.h"

#ifdef API_RUNTIME

static void api_Text_filepath_get(ApiPtr *ptr, char *value)
{
  Text *text = (Text *)ptr->data;

  if (text->filepath) {
    strcpy(value, text->filepath);
  } else {
    value[0] = '\0';
  }
}

static int api_Text_filepath_length(ApiPtr *ptr)
{
  Text *text = (Text *)ptr->data;
  return (text->filepath) ? strlen(text->filepath) : 0;
}

static void api_Text_filepath_set(ApiPtr *ptr, const char *value)
{
  Text *text = (Text *)ptr->data;

  if (text->filepath) {
    mem_freen(text->filepath);
  }

  if (value[0]) {
    text->filepath = lib_strdup(value);
  } else {
    text->filepath = NULL;
  }
}

static bool api_Text_modified_get(ApiPtr *ptr)
{
  Text *text = (Text *)ptr->data;
  return dune_text_file_modified_check(text) != 0;
}

static int api_Text_current_line_index_get(ApiPtr *ptr)
{
  Text *text = (Text *)ptr->data;
  return lib_findindex(&text->lines, text->curl);
}

static void api_Text_current_line_index_set(ApiPtr *ptr, int value)
{
  Text *text = ptr->data;
  TextLine *line = lib_findlink(&text->lines, value);
  if (line == NULL) {
    line = text->lines.last;
  }
  text->curl = line;
  text->curc = 0;
}

static int api_Text_select_end_line_index_get(ApiPtr *ptr)
{
  Text *text = ptr->data;
  return lib_findindex(&text->lines, text->sell);
}

static void api_Text_select_end_line_index_set(ApiPtr *ptr, int value)
{
  Text *text = ptr->data;
  TextLine *line = lib_findlink(&text->lines, value);
  if (line == NULL) {
    line = text->lines.last;
  }
  text->sell = line;
  text->selc = 0;
}

static int api_Text_current_character_get(ApiPtr *ptr)
{
  Text *text = ptr->data;
  return lib_str_utf8_offset_to_index(text->curl->line, text->curc);
}

static void api_Text_current_character_set(ApiPtr *ptr, int index)
{
  Text *text = ptr->data;
  TextLine *line = text->curl;
  const int len_utf8 = lib_strlen_utf8(line->line);
  CLAMP_MAX(index, len_utf8);
  text->curc = lib_str_utf8_offset_from_index(line->line, index);
}

static int api_Text_select_end_character_get(ApiPtr *ptr)
{
  Text *text = ptr->data;
  return lib_str_utf8_offset_to_index(text->sell->line, text->selc);
}

static void api_Text_select_end_character_set(Apitr *ptr, int index)
{
  Text *text = ptr->data;
  TextLine *line = text->sell;
  const int len_utf8 = lib_strlen_utf8(line->line);
  CLAMP_MAX(index, len_utf8);
  text->selc = lib_str_utf8_offset_from_index(line->line, index);
}

static void api_TextLine_body_get(ApiPtr *ptr, char *value)
{
  TextLine *line = (TextLine *)ptr->data;

  if (line->line) {
    strcpy(value, line->line);
  } else {
    value[0] = '\0';
  }
}

static int api_TextLine_body_length(ApiPtr *ptr)
{
  TextLine *line = (TextLine *)ptr->data;
  return line->len;
}

static void api_TextLine_body_set(ApiPtr *ptr, const char *value)
{
  TextLine *line = (TextLine *)ptr->data;
  int len = strlen(value);

  if (line->line) {
    mem_freen(line->line);
  }

  line->line = mem_mallocn((len + 1) * sizeof(char), "api_text_body");
  line->len = len;
  memcpy(line->line, value, len + 1);

  if (line->format) {
    mem_freen(line->format);
    line->format = NULL;
  }
}

#else

static void api_def_text_line(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "TextLine", NULL);
  api_def_struct_ui_text(sapi, "Text Line", "Line of text in a Text data-block");

  prop = api_def_prop(sapi, "body", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(
      prop, "api_TextLine_body_get", "api_TextLine_body_length", "api_TextLine_body_set");
  api_def_prop_ui_text(prop, "Line", "Text in the line");
  api_def_prop_update(prop, NC_TEXT | NA_EDITED, NULL);
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_TEXT);
}

static void api_def_text(DuneApi *dapi)
{

  static const EnumPropItem indentation_items[] = {
      {0, "TABS", 0, "Tabs", "Indent using tabs"},
      {TXT_TABSTOSPACES, "SPACES", 0, "Spaces", "Indent using spaces"},
      {0, NULL, 0, NULL, NULL},
  };

  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Text", "ID");
  api_def_struct_ui_text(
      sapi, "Text", "Text data-block referencing an external or packed text file");
  api_def_struct_ui_icon(sapi, ICON_TEXT);
  api_def_struct_clear_flag(sapi, STRUCT_ID_REFCOUNT);

  prop = api_def_prop(sapi, "filepath", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(
      prop, "api_Text_filepath_get", "api_Text_filepath_length", "api_Text_filepath_set");
  api_def_prop_ui_text(prop, "File Path", "Filename of the text file");

  prop = api_def_prop(sapi, "is_dirty", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", TXT_ISDIRTY);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Dirty", "Text file has been edited since last save");

  prop = api_def_prop(sapi, "is_modified", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_Text_modified_get", NULL);
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_TEXT);
  api_def_prop_ui_text(
      prop, "Modified", "Text file on disk is different than the one in memory");

  prop = api_def_prop(sapi, "is_in_memory", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", TXT_ISMEM);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Memory", "Text file is in memory, without a corresponding file on disk");
  
  prop = api_def_prop(sapi, "use_module", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flags", TXT_ISSCRIPT);
  api_def_prop_ui_text(prop, "Register", "Run this text as a Python script on loading");

  prop = api_def_prop(sapi, "indentation", PROP_ENUM, PROP_NONE); /* as an enum */
  api_def_prop_enum_bitflag_stype(prop, NULL, "flags");
  api_def_prop_enum_items(prop, indentation_items);
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_TEXT);
  api_def_prop_ui_text(prop, "Indentation", "Use tabs or spaces for indentation");

  prop = api_def_prop(sapi, "lines", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "TextLine");
  api_def_prop_ui_text(prop, "Lines", "Lines of text");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_TEXT);

  prop = api_def_prop(sapi, "current_line", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "curl");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "TextLine");
  api_def_prop_ui_text(
      prop, "Current Line", "Current line, and start line of selection if one exists");

  prop = api_def_prop(sapi, "current_character", PROP_INT, PROP_UNSIGNED);
  api_def_prop_range(prop, 0, INT_MAX);
  api_def_prop_ui_text(prop,
                       "Current Character",
                       "Index of current character in current line, and also start index of "
                       "character in selection if one exists");
  api_def_prop_int_fns(
      prop, "api_Text_current_character_get", "api_Text_current_character_set", NULL);
  api_def_prop_update(prop, NC_TEXT | ND_CURSOR, NULL);

  prop = api_def_prop(sapi, "current_line_index", PROP_INT, PROP_NONE);
  api_def_prop_int_fns(
      prop, "api_Text_current_line_index_get", "api_Text_current_line_index_set", NULL);
  api_def_prop_ui_text(
      prop, "Current Line Index", "Index of current TextLine in TextLine collection");
  api_def_prop_update(prop, NC_TEXT | ND_CURSOR, NULL);

  prop = api_def_prop(sapi, "select_end_line", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "sell");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "TextLine");
  api_def_prop_ui_text(prop, "Selection End Line", "End line of selection");

  prop = api_def_prop(sapi, "select_end_line_index", PROP_INT, PROP_NONE);
  api_def_prop_int_fns(
      prop, "api_Text_select_end_line_index_get", "api_Text_select_end_line_index_set", NULL);
  api_def_prop_ui_text(prop, "Select End Line Index", "Index of last TextLine in selection");
  api_def_prop_update(prop, NC_TEXT | ND_CURSOR, NULL);

  prop = api_def_prop(sapi, "select_end_character", PROP_INT, PROP_UNSIGNED);
  api_def_prop_range(prop, 0, INT_MAX);
  api_def_prop_ui_text(prop,
                       "Selection End Character",
                       "Index of character after end of selection in the selection end line");
  api_def_prop_int_fns(
      prop, "api_Text_select_end_character_get", "api_Text_select_end_character_set", NULL);
  api_def_prop_update(prop, NC_TEXT | ND_CURSOR, NULL);

  api_api_text(sapi);
}

void api_def_text(DuneApi *dapi)
{
  api_def_text_line(dapi);
  api_def_text(dapi);
}

#endif
