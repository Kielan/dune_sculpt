#include <stdlib.h> /* abort */
#include <string.h> /* strstr */
#include <sys/stat.h>
#include <sys/types.h>
#include <wctype.h>

#include "MEM_guardedalloc.h"

#include "BLI_fileops.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_string_cursor_utf8.h"
#include "BLI_string_utf8.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_constraint_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_text_types.h"
#include "DNA_userdef_types.h"

#include "BKE_bpath.h"
#include "BKE_idtype.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_text.h"

#include "BLO_read_write.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

/* -------------------------------------------------------------------- */
/** \name Prototypes
 * \{ */

static void txt_pop_first(Text *text);
static void txt_pop_last(Text *text);
static void txt_delete_line(Text *text, TextLine *line);
static void txt_delete_sel(Text *text);
static void txt_make_dirty(Text *text);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Text Data-Block
 * \{ */

static void text_init_data(ID *id)
{
  Text *text = (Text *)id;
  TextLine *tmp;

  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(text, id));

  text->filepath = NULL;

  text->flags = TXT_ISDIRTY | TXT_ISMEM;
  if ((U.flag & USER_TXT_TABSTOSPACES_DISABLE) == 0) {
    text->flags |= TXT_TABSTOSPACES;
  }

  BLI_listbase_clear(&text->lines);

  tmp = (TextLine *)MEM_mallocN(sizeof(TextLine), "textline");
  tmp->line = (char *)MEM_mallocN(1, "textline_string");
  tmp->format = NULL;

  tmp->line[0] = 0;
  tmp->len = 0;

  tmp->next = NULL;
  tmp->prev = NULL;

  BLI_addhead(&text->lines, tmp);

  text->curl = text->lines.first;
  text->curc = 0;
  text->sell = text->lines.first;
  text->selc = 0;
}

/**
 * Only copy internal data of Text ID from source
 * to already allocated/initialized destination.
 * You probably never want to use that directly,
 * use #BKE_id_copy or #BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag: Copying options (see BKE_lib_id.h's LIB_ID_COPY_... flags for more).
 */
static void text_copy_data(Main *UNUSED(bmain),
                           ID *id_dst,
                           const ID *id_src,
                           const int UNUSED(flag))
{
  Text *text_dst = (Text *)id_dst;
  const Text *text_src = (Text *)id_src;

  /* File name can be NULL. */
  if (text_src->filepath) {
    text_dst->filepath = BLI_strdup(text_src->filepath);
  }

  text_dst->flags |= TXT_ISDIRTY;

  BLI_listbase_clear(&text_dst->lines);
  text_dst->curl = text_dst->sell = NULL;
  text_dst->compiled = NULL;

  /* Walk down, reconstructing. */
  LISTBASE_FOREACH (TextLine *, line_src, &text_src->lines) {
    TextLine *line_dst = MEM_mallocN(sizeof(*line_dst), __func__);

    line_dst->line = BLI_strdup(line_src->line);
    line_dst->format = NULL;
    line_dst->len = line_src->len;

    BLI_addtail(&text_dst->lines, line_dst);
  }

  text_dst->curl = text_dst->sell = text_dst->lines.first;
  text_dst->curc = text_dst->selc = 0;
}

/** Free (or release) any data used by this text (does not free the text itself). */
static void text_free_data(ID *id)
{
  /* No animation-data here. */
  Text *text = (Text *)id;

  BKE_text_free_lines(text);

  MEM_SAFE_FREE(text->filepath);
#ifdef WITH_PYTHON
  BPY_text_free_code(text);
#endif
}

static void text_foreach_path(ID *id, BPathForeachPathData *bpath_data)
{
  Text *text = (Text *)id;

  if (text->filepath != NULL) {
    BKE_bpath_foreach_path_allocated_process(bpath_data, &text->filepath);
  }
}

static void text_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  Text *text = (Text *)id;

  /* NOTE: we are clearing local temp data here, *not* the flag in the actual 'real' ID. */
  if ((text->flags & TXT_ISMEM) && (text->flags & TXT_ISEXT)) {
    text->flags &= ~TXT_ISEXT;
  }

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  text->compiled = NULL;

  /* write LibData */
  BLO_write_id_struct(writer, Text, id_address, &text->id);
  BKE_id_blend_write(writer, &text->id);

  if (text->filepath) {
    BLO_write_string(writer, text->filepath);
  }

  if (!(text->flags & TXT_ISEXT)) {
    /* now write the text data, in two steps for optimization in the readfunction */
    LISTBASE_FOREACH (TextLine *, tmp, &text->lines) {
      BLO_write_struct(writer, TextLine, tmp);
    }

    LISTBASE_FOREACH (TextLine *, tmp, &text->lines) {
      BLO_write_raw(writer, tmp->len + 1, tmp->line);
    }
  }
}

static void text_blend_read_data(BlendDataReader *reader, ID *id)
{
  Text *text = (Text *)id;
  BLO_read_data_address(reader, &text->filepath);

  text->compiled = NULL;

#if 0
  if (text->flags & TXT_ISEXT) {
    BKE_text_reload(text);
  }
  /* else { */
#endif

  BLO_read_list(reader, &text->lines);

  BLO_read_data_address(reader, &text->curl);
  BLO_read_data_address(reader, &text->sell);

  LISTBASE_FOREACH (TextLine *, ln, &text->lines) {
    BLO_read_data_address(reader, &ln->line);
    ln->format = NULL;

    if (ln->len != (int)strlen(ln->line)) {
      printf("Error loading text, line lengths differ\n");
      ln->len = strlen(ln->line);
    }
  }

  text->flags = (text->flags) & ~TXT_ISEXT;
}

IDTypeInfo IDType_ID_TXT = {
    .id_code = ID_TXT,
    .id_filter = FILTER_ID_TXT,
    .main_listbase_index = INDEX_ID_TXT,
    .struct_size = sizeof(Text),
    .name = "Text",
    .name_plural = "texts",
    .translation_context = BLT_I18NCONTEXT_ID_TEXT,
    .flags = IDTYPE_FLAGS_NO_ANIMDATA | IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    .asset_type_info = NULL,

    .init_data = text_init_data,
    .copy_data = text_copy_data,
    .free_data = text_free_data,
    .make_local = NULL,
    .foreach_id = NULL,
    .foreach_cache = NULL,
    .foreach_path = text_foreach_path,
    .owner_get = NULL,

    .blend_write = text_blend_write,
    .blend_read_data = text_blend_read_data,
    .blend_read_lib = NULL,
    .blend_read_expand = NULL,

    .blend_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Text Add, Free, Validation
 * \{ */

void BKE_text_free_lines(Text *text)
{
  for (TextLine *tmp = text->lines.first, *tmp_next; tmp; tmp = tmp_next) {
    tmp_next = tmp->next;
    MEM_freeN(tmp->line);
    if (tmp->format) {
      MEM_freeN(tmp->format);
    }
    MEM_freeN(tmp);
  }

  BLI_listbase_clear(&text->lines);

  text->curl = text->sell = NULL;
}

Text *BKE_text_add(Main *bmain, const char *name)
{
  Text *ta;

  ta = BKE_id_new(bmain, ID_TXT, name);
  /* Texts have no users by default... Set the fake user flag to ensure that this text block
   * doesn't get deleted by default when cleaning up data blocks. */
  id_us_min(&ta->id);
  id_fake_user_set(&ta->id);

  return ta;
}

int txt_extended_ascii_as_utf8(char **str)
{
  ptrdiff_t bad_char, i = 0;
  const ptrdiff_t length = (ptrdiff_t)strlen(*str);
  int added = 0;

  while ((*str)[i]) {
    if ((bad_char = BLI_str_utf8_invalid_byte(*str + i, length - i)) == -1) {
      break;
    }

    added++;
    i += bad_char + 1;
  }

  if (added != 0) {
    char *newstr = MEM_mallocN(length + added + 1, "text_line");
    ptrdiff_t mi = 0;
    i = 0;

    while ((*str)[i]) {
      if ((bad_char = BLI_str_utf8_invalid_byte((*str) + i, length - i)) == -1) {
        memcpy(newstr + mi, (*str) + i, length - i + 1);
        break;
      }

      memcpy(newstr + mi, (*str) + i, bad_char);

      const int mofs = mi + bad_char;
      BLI_str_utf8_from_unicode((*str)[i + bad_char], newstr + mofs, (length + added) - mofs);
      i += bad_char + 1;
      mi += bad_char + 2;
    }
    newstr[length + added] = '\0';
    MEM_freeN(*str);
    *str = newstr;
  }

  return added;
}

/**
 * Removes any control characters from a text-line and fixes invalid UTF8 sequences.
 */
static void cleanup_textline(TextLine *tl)
{
  int i;

  for (i = 0; i < tl->len; i++) {
    if (tl->line[i] < ' ' && tl->line[i] != '\t') {
      memmove(tl->line + i, tl->line + i + 1, tl->len - i);
      tl->len--;
      i--;
    }
  }
  tl->len += txt_extended_ascii_as_utf8(&tl->line);
}

/**
 * used for load and reload (unlike txt_insert_buf)
 * assumes all fields are empty
 */
static void text_from_buf(Text *text, const unsigned char *buffer, const int len)
{
  int i, llen, lines_count;

  BLI_assert(BLI_listbase_is_empty(&text->lines));

  llen = 0;
  lines_count = 0;
  for (i = 0; i < len; i++) {
    if (buffer[i] == '\n') {
      TextLine *tmp;

      tmp = (TextLine *)MEM_mallocN(sizeof(TextLine), "textline");
      tmp->line = (char *)MEM_mallocN(llen + 1, "textline_string");
      tmp->format = NULL;

      if (llen) {
        memcpy(tmp->line, &buffer[i - llen], llen);
      }
      tmp->line[llen] = 0;
      tmp->len = llen;

      cleanup_textline(tmp);

      BLI_addtail(&text->lines, tmp);
      lines_count += 1;

      llen = 0;
      continue;
    }
    llen++;
  }

  /* create new line in cases:
   * - rest of line (if last line in file hasn't got \n terminator).
   *   in this case content of such line would be used to fill text line buffer
   * - file is empty. in this case new line is needed to start editing from.
   * - last character in buffer is \n. in this case new line is needed to
   *   deal with newline at end of file. (see T28087) (sergey) */
  if (llen != 0 || lines_count == 0 || buffer[len - 1] == '\n') {
    TextLine *tmp;

    tmp = (TextLine *)MEM_mallocN(sizeof(TextLine), "textline");
    tmp->line = (char *)MEM_mallocN(llen + 1, "textline_string");
    tmp->format = NULL;

    if (llen) {
      memcpy(tmp->line, &buffer[i - llen], llen);
    }

    tmp->line[llen] = 0;
    tmp->len = llen;

    cleanup_textline(tmp);

    BLI_addtail(&text->lines, tmp);
    /* lines_count += 1; */ /* UNUSED */
  }

  text->curl = text->sell = text->lines.first;
  text->curc = text->selc = 0;
}

bool BKE_text_reload(Text *text)
{
  unsigned char *buffer;
  size_t buffer_len;
  char filepath_abs[FILE_MAX];
  BLI_stat_t st;

  if (!text->filepath) {
    return false;
  }

  BLI_strncpy(filepath_abs, text->filepath, FILE_MAX);
  BLI_path_abs(filepath_abs, ID_BLEND_PATH_FROM_GLOBAL(&text->id));

  buffer = BLI_file_read_text_as_mem(filepath_abs, 0, &buffer_len);
  if (buffer == NULL) {
    return false;
  }

  /* free memory: */
  BKE_text_free_lines(text);
  txt_make_dirty(text);

  /* clear undo buffer */
  if (BLI_stat(filepath_abs, &st) != -1) {
    text->mtime = st.st_mtime;
  }
  else {
    text->mtime = 0;
  }

  text_from_buf(text, buffer, buffer_len);

  MEM_freeN(buffer);
  return true;
}

Text *BKE_text_load_ex(Main *bmain, const char *file, const char *relpath, const bool is_internal)
{
  unsigned char *buffer;
  size_t buffer_len;
  Text *ta;
  char filepath_abs[FILE_MAX];
  BLI_stat_t st;

  BLI_strncpy(filepath_abs, file, FILE_MAX);
  if (relpath) { /* Can be NULL (background mode). */
    BLI_path_abs(filepath_abs, relpath);
  }

  buffer = BLI_file_read_text_as_mem(filepath_abs, 0, &buffer_len);
  if (buffer == NULL) {
    return NULL;
  }

  ta = BKE_libblock_alloc(bmain, ID_TXT, BLI_path_basename(filepath_abs), 0);
  id_us_min(&ta->id);
  id_fake_user_set(&ta->id);

  BLI_listbase_clear(&ta->lines);
  ta->curl = ta->sell = NULL;

  if ((U.flag & USER_TXT_TABSTOSPACES_DISABLE) == 0) {
    ta->flags = TXT_TABSTOSPACES;
  }

  if (is_internal == false) {
    ta->filepath = MEM_mallocN(strlen(file) + 1, "text_name");
    strcpy(ta->filepath, file);
  }
  else {
    ta->flags |= TXT_ISMEM | TXT_ISDIRTY;
  }

  /* clear undo buffer */
  if (BLI_stat(filepath_abs, &st) != -1) {
    ta->mtime = st.st_mtime;
  }
  else {
    ta->mtime = 0;
  }

  text_from_buf(ta, buffer, buffer_len);

  MEM_freeN(buffer);

  return ta;
}

Text *BKE_text_load(Main *bmain, const char *file, const char *relpath)
{
  return BKE_text_load_ex(bmain, file, relpath, false);
}

void BKE_text_clear(Text *text) /* called directly from rna */
{
  txt_sel_all(text);
  txt_delete_sel(text);
  txt_make_dirty(text);
}

void BKE_text_write(Text *text, const char *str) /* called directly from rna */
{
  txt_insert_buf(text, str);
  txt_move_eof(text, 0);
  txt_make_dirty(text);
}

int BKE_text_file_modified_check(Text *text)
{
  BLI_stat_t st;
  int result;
  char file[FILE_MAX];

  if (!text->filepath) {
    return 0;
  }

  BLI_strncpy(file, text->filepath, FILE_MAX);
  BLI_path_abs(file, ID_BLEND_PATH_FROM_GLOBAL(&text->id));

  if (!BLI_exists(file)) {
    return 2;
  }

  result = BLI_stat(file, &st);

  if (result == -1) {
    return -1;
  }

  if ((st.st_mode & S_IFMT) != S_IFREG) {
    return -1;
  }

  if (st.st_mtime > text->mtime) {
    return 1;
  }

  return 0;
}

void BKE_text_file_modified_ignore(Text *text)
{
  BLI_stat_t st;
  int result;
  char file[FILE_MAX];

  if (!text->filepath) {
    return;
  }

  BLI_strncpy(file, text->filepath, FILE_MAX);
  BLI_path_abs(file, ID_BLEND_PATH_FROM_GLOBAL(&text->id));

  if (!BLI_exists(file)) {
    return;
  }

  result = BLI_stat(file, &st);

  if (result == -1 || (st.st_mode & S_IFMT) != S_IFREG) {
    return;
  }

  text->mtime = st.st_mtime;
}

/* -------------------------------------------------------------------- */
/** Editing Utility Functions */
