/* Main Font API, public fns for font handling.
 * Wraps OpenGL and FreeType. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>

#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "mem_guardedalloc.h"

#include "lib_math.h"
#include "lib_threads.h"

#include "font_api.h"

#include "imbuf_colormanagement.h"

#include "gpu_matrix.h"
#include "gpu_shader.h"

#include "font_internal.h"
#include "font_internal_types.h"

/* Max number of font in memory.
 * Take care that now every font have a glyph cache per size/dpi,
 * so we don't need load the same font with different size, just
 * load one and call BLF_size. */
#define FONT_MAX_FONT 16

#define FONT_RESULT_CHECK_INIT(r_info) \
  if (r_info) { \
    memset(r_info, 0, sizeof(*(r_info))); \
  } \
  ((void)0)

/* Font array. */
static Font *global_font[FONT_MAX_FONT] = {NULL};

/* XXX: should these be made into global_font_'s too? */

int font_mono_font = -1;
int font_mono_font_render = -1;

static Font *font_get(int fontid)
{
  if (fontid >= 0 && fontid < FONT_MAX_FONT) {
    return global_font[fontid];
  }
  return NULL;
}

int font_init(void)
{
  for (int i = 0; i <  FONT_MAX_FONT; i++) {
    global_font[i] = NULL;
  }

  font_default_dpi(72);

  return font_init();
}

void font_exit(void)
{
  for (int i = 0; i < FONT_MAX_FONT; i++) {
    Font *font = global_font[i];
    if (font) {
      font_free(font);
      global_font[i] = NULL;
    }
  }

  font_exit();
}

void font_cache_clear(void)
{
  for (int i = 0; i < FONT_MAX_FONT; i++) {
    Font *font = global_font[i];
    if (font) {
      font_glyph_cache_clear(font);
    }
  }
}

bool font_id_is_valid(int fontid)
{
  return font_get(fontid) != NULL;
}

static int font_search(const char *name)
{
  for (int i = 0; i < BLF_MAX_FONT; i++) {
    Font *font = global_font[i];
    if (font && (STREQ(font->name, name))) {
      return i;
    }
  }

  return -1;
}

static int font_search_available(void)
{
  for (int i = 0; i < FONT_MAX_FONT; i++) {
    if (!global_font[i]) {
      return i;
    }
  }

  return -1;
}

bool font_has_glyph(int fontid, unsigned int unicode)
{
  Font *font = font_get(fontid);
  if (font) {
    return FT_Get_Char_Index(font->face, unicode) != 0;
  }
  return false;
}

int font_load(const char *name)
{
  /* check if we already load this font. */
  int i = font_search(name);
  if (i >= 0) {
    Font *font = global_font[i];
    font->ref_count++;
    return i;
  }

  return font_load_unique(name);
}

int font_load_unique(const char *name)
{
  /* Don't search in the cache!! make a new
   * object font, this is for keep fonts threads safe. */
  int i = font_search_available();
  if (i == -1) {
    printf("Too many fonts!!!\n");
    return -1;
  }

  char *filename = font_dir_search(name);
  if (!filename) {
    printf("Can't find font: %s\n", name);
    return -1;
  }

  Font *font = font_new(name, filename);
  mem_freen(filename);

  if (!font) {
    printf("Can't load font: %s\n", name);
    return -1;
  }

  font->ref_count = 1;
  global_font[i] = font;
  return i;
}

void font_metrics_attach(int fontid, unsigned char *mem, int mem_size)
{
  Font *font = font_get(fontid);

  if (font) {
    font_font_attach_from_mem(font, mem, mem_size);
  }
}

int font_load_mem(const char *name, const unsigned char *mem, int mem_size)
{
  int i = font_search(name);
  if (i >= 0) {
    // font = global_font[i]; /* UNUSED */
    return i;
  }
  return font_load_mem_unique(name, mem, mem_size);
}

int font_load_mem_unique(const char *name, const unsigned char *mem, int mem_size)
{
  /* Don't search in the cache, make a new object font!
   * this is to keep the font thread safe. */
  int i = font_search_available();
  if (i == -1) {
    printf("Too many fonts!!!\n");
    return -1;
  }

  if (!mem_size) {
    printf("Can't load font: %s from memory!!\n", name);
    return -1;
  }

  Font *font = font_new_from_mem(name, mem, mem_size);
  if (!font) {
    printf("Can't load font: %s from memory!!\n", name);
    return -1;
  }

  font->ref_count = 1;
  global_font[i] = font;
  return i;
}

void font_unload(const char *name)
{
  for (int i = 0; i < FONT_MAX_FONT; i++) {
    Font *font = global_font[i];

    if (font && (STREQ(font->name, name))) {
      lib_assert(font->ref_count > 0);
      font->ref_count--;

      if (font->ref_count == 0) {
        font_font_free(font);
        global_font[i] = NULL;
      }
    }
  }
}

void BLF_unload_id(int fontid)
{
  Font *font = font_get(fontid);
  if (font) {
    lib_assert(font->ref_count > 0);
    font->ref_count--;

    if (font->ref_count == 0) {
      font_font_free(font);
      global_font[fontid] = NULL;
    }
  }
}

void font_enable(int fontid, int option)
{
  Font *font = font_get(fontid);

  if (font) {
    font->flags |= option;
  }
}

void font_disable(int fontid, int option)
{
  Font *font = font_get(fontid);

  if (font) {
    font->flags &= ~option;
  }
}

void font_aspect(int fontid, float x, float y, float z)
{
  Font *font = font_get(fontid);

  if (font) {
    font->aspect[0] = x;
    font->aspect[1] = y;
    font->aspect[2] = z;
  }
}

void font_matrix(int fontid, const float m[16])
{
  Font *font = font_get(fontid);

  if (font) {
    memcpy(font->m, m, sizeof(font->m));
  }
}

void font_position(int fontid, float x, float y, float z)
{
  Font *font = font_get(fontid);

  if (font) {
    float xa, ya, za;
    float remainder;

    if (font->flags & FONT_ASPECT) {
      xa = font->aspect[0];
      ya = font->aspect[1];
      za = font->aspect[2];
    }
    else {
      xa = 1.0f;
      ya = 1.0f;
      za = 1.0f;
    }

    remainder = x - floorf(x);
    if (remainder > 0.4f && remainder < 0.6f) {
      if (remainder < 0.5f) {
        x -= 0.1f * xa;
      }
      else {
        x += 0.1f * xa;
      }
    }

    remainder = y - floorf(y);
    if (remainder > 0.4f && remainder < 0.6f) {
      if (remainder < 0.5f) {
        y -= 0.1f * ya;
      }
      else {
        y += 0.1f * ya;
      }
    }

    remainder = z - floorf(z);
    if (remainder > 0.4f && remainder < 0.6f) {
      if (remainder < 0.5f) {
        z -= 0.1f * za;
      }
      else {
        z += 0.1f * za;
      }
    }

    font->pos[0] = x;
    font->pos[1] = y;
    font->pos[2] = z;
  }
}

void font_size(int fontid, float size, int dpi)
{
  Font *font = font_get(fontid);

  if (font) {
    font_font_size(font, size, dpi);
  }
}

#if FONT_BLUR_ENABLE
void font_blur(int fontid, int size)
{
  Font *font = font_get(fontid);

  if (font) {
    font->blur = size;
  }
}
#endif

void font_color4ubv(int fontid, const unsigned char rgba[4])
{
  Font *font = font_get(fontid);

  if (font) {
    font->color[0] = rgba[0];
    font->color[1] = rgba[1];
    font->color[2] = rgba[2];
    font->color[3] = rgba[3];
  }
}

void font_color3ubv_alpha(int fontid, const unsigned char rgb[3], unsigned char alpha)
{
  Font *font = font_get(fontid);

  if (font) {
    font->color[0] = rgb[0];
    font->color[1] = rgb[1];
    font->color[2] = rgb[2];
    font->color[3] = alpha;
  }
}

void font_color3ubv(int fontid, const unsigned char rgb[3])
{
  font_color3ubv_alpha(fontid, rgb, 255);
}

void font_color4ub(
    int fontid, unsigned char r, unsigned char g, unsigned char b, unsigned char alpha)
{
  Font *font = font_get(fontid);

  if (font) {
    font->color[0] = r;
    font->color[1] = g;
    font->color[2] = b;
    font->color[3] = alpha;
  }
}

void font_color3ub(int fontid, unsigned char r, unsigned char g, unsigned char b)
{
  Font *font = font_get(fontid);

  if (font) {
    font->color[0] = r;
    font->color[1] = g;
    font->color[2] = b;
    font->color[3] = 255;
  }
}

void font_color4fv(int fontid, const float rgba[4])
{
  Font *font = font_get(fontid);

  if (font) {
    rgba_float_to_uchar(font->color, rgba);
  }
}

void font_color4f(int fontid, float r, float g, float b, float a)
{
  const float rgba[4] = {r, g, b, a};
  font_color4fv(fontid, rgba);
}

void font_color3fv_alpha(int fontid, const float rgb[3], float alpha)
{
  float rgba[4];
  copy_v3_v3(rgba, rgb);
  rgba[3] = alpha;
  font_color4fv(fontid, rgba);
}

void font_color3f(int fontid, float r, float g, float b)
{
  const float rgba[4] = {r, g, b, 1.0f};
  font_color4fv(fontid, rgba);
}

void font_batch_draw_begin(void)
{
  lib_assert(g_batch.enabled == false);
  g_batch.enabled = true;
}

void font_batch_draw_flush(void)
{
  if (g_batch.enabled) {
    fong_batch_draw();
  }
}

void font_batch_draw_end(void)
{
  lib_assert(g_batch.enabled == true);
  font_batch_draw(); /* Draw remaining glyphs */
  g_batch.enabled = false;
}

static void font_draw_gl__start(Font *font)
{
  /* The pixmap alignment hack is handle
   * in font_position (old ui_rasterpos_safe).  */

  if ((font->flags & (FONT_ROTATION | FONT_MATRIX | BLF_ASPECT)) == 0) {
    return; /* glyphs will be translated individually and batched. */
  }

  gpu_matrix_push();

  if (font->flags & FONT_MATRIX) {
    gpu_matrix_mul(font->m);
  }

  gpu_matrix_translate_3fv(font->pos);

  if (font->flags & FONT_ASPECT) {
    gpu_matrix_scale_3fv(font->aspect);
  }

  if (font->flags & FONT_ROTATION) {
    gpu_matrix_rotate_2d(RAD2DEG(font->angle));
  }
}

static void font_draw_gl__end(Font *font)
{
  if ((font->flags & (FONT_ROTATION | FONT_MATRIX | FONT_ASPECT)) != 0) {
    gpu_matrix_pop();
  }
}

void font_draw_ex(int fontid, const char *str, const size_t str_len, struct FontResult *r_info)
{
  Font *font = font_get(fontid);

  FONT_RESULT_CHECK_INIT(r_info);

  if (font) {
    font_draw_gl__start(font);
    if (font->flags & FONT_WORD_WRAP) {
      font_draw__wrap(font, str, str_len, r_info);
    }
    else {
      font_draw(font, str, str_len, r_info);
    }
    font_draw_gl__end(font);
  }
}
void font_draw(int fontid, const char *str, const size_t str_len)
{
  if (str_len == 0 || str[0] == '\0') {
    return;
  }

  /* Avoid bgl usage to corrupt Font drawing. */
  gpu_bgl_end();

  font_draw_ex(fontid, str, str_len, NULL);
}

int font_draw_mono(int fontid, const char *str, const size_t str_len, int cwidth)
{
  if (str_len == 0 || str[0] == '\0') {
    return 0;
  }

  Font *font = font_get(fontid);
  int columns = 0;

  if (font) {
    font_draw_gl__start(font);
    columns = font_draw_mono(font, str, str_len, cwidth);
    font_draw_gl__end(font);
  }

  return columns;
}

void font_boundbox_foreach_glyph_ex(int fontid,
                                   const char *str,
                                   size_t str_len,
                                   FontGlyphBoundsFn user_fn,
                                   void *user_data,
                                   struct FontResult *r_info)
{
  Font *font = font_get(fontid);

  FONT_RESULT_CHECK_INIT(r_info);

  if (font) {
    if (font->flags & FONT_WORD_WRAP) {
      /* TODO: word-wrap support. */
      lib_assert(0);
    }
    else {
      font_boundbox_foreach_glyph(font, str, str_len, user_fn, user_data, r_info);
    }
  }
}

void font_boundbox_foreach_glyph(
    int fontid, const char *str, const size_t str_len, FontGlyphBoundsFn user_fn, void *user_data)
{
  font_boundbox_foreach_glyph_ex(fontid, str, str_len, user_fn, user_data, NULL);
}

size_t font_width_to_strlen(
    int fontid, const char *str, const size_t str_len, float width, float *r_width)
{
  Font *font = font_get(fontid);

  if (font) {
    const float xa = (font->flags & FONT_ASPECT) ? font->aspect[0] : 1.0f;
    size_t ret;
    ret = font_width_to_strlen(font, str, str_len, width / xa, r_width);
    if (r_width) {
      *r_width *= xa;
    }
    return ret;
  }

  if (r_width) {
    *r_width = 0.0f;
  }
  return 0;
}

size_t font_width_to_rstrlen(
    int fontid, const char *str, const size_t str_len, float width, float *r_width)
{
  Font *font = font_get(fontid);

  if (font) {
    const float xa = (font->flags & FONT_ASPECT) ? font->aspect[0] : 1.0f;
    size_t ret;
    ret = font_width_to_rstrlen(font, str, str_len, width / xa, r_width);
    if (r_width) {
      *r_width *= xa;
    }
    return ret;
  }

  if (r_width) {
    *r_width = 0.0f;
  }
  return 0;
}

void font_boundbox_ex(
    int fontid, const char *str, const size_t str_len, rctf *r_box, struct FontResult *r_info)
{
  Font *font = font_get(fontid);

  FONT_RESULT_CHECK_INIT(r_info);

  if (font) {
    if (font->flags & FONT_WORD_WRAP) {
      font_boundbox__wrap(font, str, str_len, r_box, r_info);
    }
    else {
      font_boundbox(font, str, str_len, r_box, r_info);
    }
  }
}

void font_boundbox(int fontid, const char *str, const size_t str_len, rctf *r_box)
{
  font_boundbox_ex(fontid, str, str_len, r_box, NULL);
}

void font_width_and_height(
    int fontid, const char *str, const size_t str_len, float *r_width, float *r_height)
{
  Font *font = font_get(fontid);

  if (font) {
    font_width_and_height(font, str, str_len, r_width, r_height, NULL);
  }
  else {
    *r_width = *r_height = 0.0f;
  }
}

float font_width_ex(int fontid, const char *str, const size_t str_len, struct ResultBLF *r_info)
{
  Font *font = font_get(fontid);

  FONT_RESULT_CHECK_INIT(r_info);

  if (font) {
    return font_width(font, str, str_len, r_info);
  }

  return 0.0f;
}

float font_width(int fontid, const char *str, const size_t str_len)
{
  return font_width_ex(fontid, str, str_len, NULL);
}

float font_fixed_width(int fontid)
{
  Font *font = font_get(fontid);

  if (font) {
    return font_font_fixed_width(font);
  }

  return 0.0f;
}

float font_height_ex(int fontid, const char *str, const size_t str_len, struct FontResult *r_info)
{
  Font *font = font_get(fontid);

  FONT_RESULT_CHECK_INIT(r_info);

  if (font) {
    return font_height(font, str, str_len, r_info);
  }

  return 0.0f;
}

float font_height(int fontid, const char *str, const size_t str_len)
{
  return font_height_ex(fontid, str, str_len, NULL);
}

int font_height_max(int fontid)
{
  Font *font = font_get(fontid);

  if (font) {
    return font_height_max(font);
  }

  return 0;
}

float font_width_max(int fontid)
{
  Font *font = font_get(fontid);

  if (font) {
    return font_width_max(font);
  }

  return 0.0f;
}

float font_descender(int fontid)
{
  Font *font = font_get(fontid);

  if (font) {
    return font_descender(font);
  }

  return 0.0f;
}

float font_ascender(int fontid)
{
  Font *font = font_get(fontid);

  if (font) {
    return font_ascender(font);
  }

  return 0.0f;
}

void font_rotation(int fontid, float angle)
{
  Font *font = font_get(fontid);

  if (font) {
    font->angle = angle;
  }
}

void font_clipping(int fontid, float xmin, float ymin, float xmax, float ymax)
{
  Font *font = font_get(fontid);

  if (font) {
    font->clip_rec.xmin = xmin;
    font->clip_rec.ymin = ymin;
    font->clip_rec.xmax = xmax;
    font->clip_rec.ymax = ymax;
  }
}

void font_wordwrap(int fontid, int wrap_width)
{
  Font *font = font_get(fontid);

  if (font) {
    font->wrap_width = wrap_width;
  }
}

void font_shadow(int fontid, int level, const float rgba[4])
{
  Font *font = font_get(fontid);

  if (font) {
    font->shadow = level;
    rgba_float_to_uchar(font->shadow_color, rgba);
  }
}

void font_shadow_offset(int fontid, int x, int y)
{
  Font *font = font_get(fontid);

  if (font) {
    font->shadow_x = x;
    font->shadow_y = y;
  }
}

void font_buffer(int fontid,
                float *fbuf,
                unsigned char *cbuf,
                int w,
                int h,
                int nch,
                struct ColorManagedDisplay *display)
{
  Font *font = font_get(fontid);

  if (font) {
    font->buf_info.fbuf = fbuf;
    font->buf_info.cbuf = cbuf;
    font->buf_info.dims[0] = w;
    font->buf_info.dims[1] = h;
    font->buf_info.ch = nch;
    font->buf_info.display = display;
  }
}

void font_buffer_col(int fontid, const float rgba[4])
{
  Font *font = font_get(fontid);

  if (font) {
    copy_v4_v4(font->buf_info.col_init, rgba);
  }
}

void font_draw_buffer__start(Font *font)
{
  FontBufInfo *buf_info = &font->buf_info;

  rgba_float_to_uchar(buf_info->col_char, buf_info->col_init);

  if (buf_info->display) {
    copy_v4_v4(buf_info->col_float, buf_info->col_init);
    imbuf_colormanagement_display_to_scene_linear_v3(buf_info->col_float, buf_info->display);
  }
  else {
    srgb_to_linearrgb_v4(buf_info->col_float, buf_info->col_init);
  }
}
void font_draw_buffer__end(void)
{
}

void font_draw_buffer_ex(int fontid,
                        const char *str,
                        const size_t str_len,
                        struct FontResult *r_info)
{
  Font *font = font_get(fontid);

  if (font && (font->buf_info.fbuf || font->buf_info.cbuf)) {
    font_draw_buffer__start(font);
    if (font->flags & FONT_WORD_WRAP) {
      font_draw_buffer__wrap(font, str, str_len, r_info);
    }
    else {
      font_draw_buffer(font, str, str_len, r_info);
    }
    font_draw_buffer__end();
  }
}
void font_draw_buffer(int fontid, const char *str, const size_t str_len)
{
  font_draw_buffer_ex(fontid, str, str_len, NULL);
}

char *font_display_name_from_file(const char *filename)
{
  Font *font = font_font_new("font_name", filename);
  if (!font) {
    return NULL;
  }
  char *name = font_display_name(font);
  font_free(font);
  return name;
}

#ifdef DEBUG
void font_state_print(int fontid)
{
  Font *font = font_get(fontid);
  if (font) {
    printf("fontid %d %p\n", fontid, (void *)font);
    printf("  name:    '%s'\n", font->name);
    printf("  size:     %f\n", font->size);
    printf("  dpi:      %u\n", font->dpi);
    printf("  pos:      %.6f %.6f %.6f\n", UNPACK3(font->pos));
    printf("  aspect:   (%d) %.6f %.6f %.6f\n",
           (font->flags & FONT_ROTATION) != 0,
           UNPACK3(font->aspect));
    printf("  angle:    (%d) %.6f\n", (font->flags & FONT_ASPECT) != 0, font->angle);
    printf("  flag:     %d\n", font->flags);
  }
  else {
    printf("fontid %d (NULL)\n", fontid);
  }
  fflush(stdout);
}
#endif
