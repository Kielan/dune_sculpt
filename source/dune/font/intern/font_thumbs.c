/* Util fn to generate font preview images.
 * Isolate since this needs to be called by ImBuf code (bad level call). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>

#include FT_FREETYPE_H

#include "lib_list.h"
#include "lib_rect.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "font_internal.h"
#include "font_internal_types.h"

#include "font_api.h"
#include "lang.h"

#include "lib_strict_flags.h"

void font_thumb_preview(const char *filename,
                       const char **draw_str,
                       const char **lang_draw_str,
                       const unsigned char draw_str_lines,
                       const float font_color[4],
                       const int font_size,
                       unsigned char *buf,
                       int w,
                       int h,
                       int channels)
{
  const unsigned int dpi = 72;
  const int font_size_min = 6;
  int font_size_curr;
  /* shrink 1/th each line */
  int font_shrink = 4;

  Font *font;

  /* Create a new blender font obj and fill it with default values */
  font = font_font_new("thumb_font", filename);
  if (!font) {
    printf("Info: Can't load font '%s', no preview possible\n", filename);
    return;
  }

  /* Would be done via the Font API, but we're not using a fontid here */
  font->buf_info.cbuf = buf;
  font->buf_info.ch = channels;
  font->buf_info.dims[0] = w;
  font->buf_info.dims[1] = h;

  /* Always create the image with a white font,
   * the caller can theme how it likes */
  memcpy(font->buf_info.col_init, font_color, sizeof(font->buf_info.col_init));
  font->pos[1] = (float)h;

  font_size_curr = font_size;

  font_draw_buffer__start(font);

  for (int i = 0; i < draw_str_lines; i++) {
    const char *draw_str_lang = lang_draw_str[i] != NULL ? lang_draw_str[i] : draw_str[i];
    const size_t draw_str_lang_len = strlen(draw_str_lang);
    int draw_str_lang_nbr = 0;

    CLAMP_MIN(font_size_curr, font_size_min);
    if (!font_font_size(font, (float)font_size_curr, dpi)) {
      break;
    }

    /* decrease font size each time */
    font_size_curr -= (font_size_curr / font_shrink);
    font_shrink += 1;

    font->pos[1] -= font_font_ascender(font) * 1.1f;

    /* We fallback to default english strings in case not enough chars are available in current
     * font for given translated string (useful in non-latin lang cxt, like Chinese,
     * since many fonts will then show nothing but ugly 'missing char' in their preview).
     * Does not handle all cases, but much better than nothing. */
    if (font_font_count_missing_chars(font, draw_str_lang, draw_str_lang_len, &draw_str_lang_nbr) >
        (draw_str_lang_nbr / 2)) {
      font_font_draw_buffer(font, draw_str[i], strlen(draw_str[i]), NULL);
    }
    else {
      font_font_draw_buffer(font, draw_str_i18n, draw_str_i18n_len, NULL);
    }
  }

  font_draw_buffer__end();
  font_font_free(font);
}
