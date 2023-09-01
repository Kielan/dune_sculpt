/* Default API, that uses Dune's user prefs for the default size. */

#include "types_userdef.h"

#include "lib_assert.h"

#include "font_api.h"

#include "font_internal.h"

/* call font_default_set first! */
#define ASSERT_DEFAULT_SET lib_assert(global_font_default != -1)

/* Default size and dpi, for fong_draw_default. */
static int global_font_default = -1;
static int global_font_dpi = 72;
/* Keep in sync with `UI_DEFAULT_TEXT_POINTS` */
static float global_font_size = 11.0f;

void font_default_dpi(int dpi)
{
  global_font_dpi = dpi;
}

void font_default_size(float size)
{
  global_font_size = size;
}

void font_default_set(int fontid)
{
  if ((fontid == -1) || font_id_is_valid(fontid)) {
    global_font_default = fontid;
  }
}

int font_default(void)
{
  ASSERT_DEFAULT_SET;
  return global_font_default;
}

int font_set_default(void)
{
  ASSERT_DEFAULT_SET;

  font_size(global_font_default, global_font_size, global_font_dpi);

  return global_font_default;
}

void font_draw_default(float x, float y, float z, const char *str, const size_t str_len)
{
  ASSERT_DEFAULT_SET;
  font_size(global_font_default, global_font_size, global_font_dpi);
  font_position(global_font_default, x, y, z);
  font_draw(global_font_default, str, str_len);
}
