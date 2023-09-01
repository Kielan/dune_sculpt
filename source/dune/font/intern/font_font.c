/** \file
 * \ingroup blf
 *
 * Deals with drawing text to OpenGL or bitmap buffers.
 *
 * Also low level functions for managing \a FontBLF.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>

#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "MEM_guardedalloc.h"

#include "DNA_vec_types.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_math_color_blend.h"
#include "BLI_rect.h"
#include "BLI_string.h"
#include "BLI_string_utf8.h"
#include "BLI_threads.h"

#include "BLF_api.h"

#include "GPU_batch.h"
#include "GPU_matrix.h"

#include "blf_internal.h"
#include "blf_internal_types.h"

#include "BLI_strict_flags.h"

#ifdef WIN32
#  define FT_New_Face FT_New_Face__win32_compat
#endif

/* Batching buffer for drawing. */

BatchBLF g_batch;

/* freetype2 handle ONLY for this file! */
static FT_Library ft_lib;
static SpinLock ft_lib_mutex;
static SpinLock blf_glyph_cache_mutex;

/* May be set to #UI_widgetbase_draw_cache_flush. */
static void (*blf_draw_cache_flush)(void) = NULL;

/* -------------------------------------------------------------------- */
/** \name FreeType Utilities (Internal)
 * \{ */

/**
 * Convert a FreeType 26.6 value representing an unscaled design size to pixels.
 * This is an exact copy of the scaling done inside FT_Get_Kerning when called
 * with #FT_KERNING_DEFAULT, including arbitrary resizing for small fonts.
 */
static int blf_unscaled_F26Dot6_to_pixels(FontBLF *font, FT_Pos value)
{
  /* Scale value by font size using integer-optimized multiplication. */
  FT_Long scaled = FT_MulFix(value, font->face->size->metrics.x_scale);

  /* FreeType states that this '25' has been determined heuristically. */
  if (font->face->size->metrics.x_ppem < 25) {
    scaled = FT_MulDiv(scaled, font->face->size->metrics.x_ppem, 25);
  }

  /* Copies of internal FreeType macros needed here. */
#define FT_PIX_FLOOR(x) ((x) & ~63)
#define FT_PIX_ROUND(x) FT_PIX_FLOOR((x) + 32)

  /* Round to even 64ths, then divide by 64. */
  return (int)FT_PIX_ROUND(scaled) >> 6;

#undef FT_PIX_FLOOR
#undef FT_PIX_ROUND
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Glyph Batching
 * \{ */

/**
 * Draw-calls are precious! make them count!
 * Since most of the Text elements are not covered by other UI elements, we can
 * group some strings together and render them in one draw-call. This behavior
 * is on demand only, between #BLF_batch_draw_begin() and #BLF_batch_draw_end().
 */
static void blf_batch_draw_init(void)
{
  GPUVertFormat format = {0};
  g_batch.pos_loc = GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 4, GPU_FETCH_FLOAT);
  g_batch.col_loc = GPU_vertformat_attr_add(
      &format, "col", GPU_COMP_U8, 4, GPU_FETCH_INT_TO_FLOAT_UNIT);
  g_batch.offset_loc = GPU_vertformat_attr_add(&format, "offset", GPU_COMP_I32, 1, GPU_FETCH_INT);
  g_batch.glyph_size_loc = GPU_vertformat_attr_add(
      &format, "glyph_size", GPU_COMP_I32, 2, GPU_FETCH_INT);

  g_batch.verts = GPU_vertbuf_create_with_format_ex(&format, GPU_USAGE_STREAM);
  GPU_vertbuf_data_alloc(g_batch.verts, BLF_BATCH_DRAW_LEN_MAX);

  GPU_vertbuf_attr_get_raw_data(g_batch.verts, g_batch.pos_loc, &g_batch.pos_step);
  GPU_vertbuf_attr_get_raw_data(g_batch.verts, g_batch.col_loc, &g_batch.col_step);
  GPU_vertbuf_attr_get_raw_data(g_batch.verts, g_batch.offset_loc, &g_batch.offset_step);
  GPU_vertbuf_attr_get_raw_data(g_batch.verts, g_batch.glyph_size_loc, &g_batch.glyph_size_step);
  g_batch.glyph_len = 0;

  /* A dummy VBO containing 4 points, attributes are not used. */
  GPUVertBuf *vbo = GPU_vertbuf_create_with_format(&format);
  GPU_vertbuf_data_alloc(vbo, 4);

  /* We render a quad as a triangle strip and instance it for each glyph. */
  g_batch.batch = GPU_batch_create_ex(GPU_PRIM_TRI_STRIP, vbo, NULL, GPU_BATCH_OWNS_VBO);
  GPU_batch_instbuf_set(g_batch.batch, g_batch.verts, true);
}

static void blf_batch_draw_exit(void)
{
  GPU_BATCH_DISCARD_SAFE(g_batch.batch);
}

void blf_batch_draw_begin(FontBLF *font)
{
  if (g_batch.batch == NULL) {
    blf_batch_draw_init();
  }

  const bool font_changed = (g_batch.font != font);
  const bool simple_shader = ((font->flags & (BLF_ROTATION | BLF_MATRIX | BLF_ASPECT)) == 0);
  const bool shader_changed = (simple_shader != g_batch.simple_shader);

  g_batch.active = g_batch.enabled && simple_shader;

  if (simple_shader) {
    /* Offset is applied to each glyph. */
    g_batch.ofs[0] = floorf(font->pos[0]);
    g_batch.ofs[1] = floorf(font->pos[1]);
  }
  else {
    /* Offset is baked in modelview mat. */
    zero_v2(g_batch.ofs);
  }

  if (g_batch.active) {
    float gpumat[4][4];
    GPU_matrix_model_view_get(gpumat);

    bool mat_changed = (memcmp(gpumat, g_batch.mat, sizeof(g_batch.mat)) != 0);

    if (mat_changed) {
      /* Modelviewmat is no longer the same.
       * Flush cache but with the previous mat. */
      GPU_matrix_push();
      GPU_matrix_set(g_batch.mat);
    }

    /* flush cache if config is not the same. */
    if (mat_changed || font_changed || shader_changed) {
      blf_batch_draw();
      g_batch.simple_shader = simple_shader;
      g_batch.font = font;
    }
    else {
      /* Nothing changed continue batching. */
      return;
    }

    if (mat_changed) {
      GPU_matrix_pop();
      /* Save for next memcmp. */
      memcpy(g_batch.mat, gpumat, sizeof(g_batch.mat));
    }
  }
  else {
    /* flush cache */
    blf_batch_draw();
    g_batch.font = font;
    g_batch.simple_shader = simple_shader;
  }
}

static GPUTexture *blf_batch_cache_texture_load(void)
{
  GlyphCacheBLF *gc = g_batch.glyph_cache;
  BLI_assert(gc);
  BLI_assert(gc->bitmap_len > 0);

  if (gc->bitmap_len > gc->bitmap_len_landed) {
    const int tex_width = GPU_texture_width(gc->texture);

    int bitmap_len_landed = gc->bitmap_len_landed;
    int remain = gc->bitmap_len - bitmap_len_landed;
    int offset_x = bitmap_len_landed % tex_width;
    int offset_y = bitmap_len_landed / tex_width;

    /* TODO(germano): Update more than one row in a single call. */
    while (remain) {
      int remain_row = tex_width - offset_x;
      int width = remain > remain_row ? remain_row : remain;
      GPU_texture_update_sub(gc->texture,
                             GPU_DATA_UBYTE,
                             &gc->bitmap_result[bitmap_len_landed],
                             offset_x,
                             offset_y,
                             0,
                             width,
                             1,
                             0);

      bitmap_len_landed += width;
      remain -= width;
      offset_x = 0;
      offset_y += 1;
    }

    gc->bitmap_len_landed = bitmap_len_landed;
  }

  return gc->texture;
}

void blf_batch_draw(void)
{
  if (g_batch.glyph_len == 0) {
    return;
  }

  GPU_blend(GPU_BLEND_ALPHA);

  /* We need to flush widget base first to ensure correct ordering. */
  if (blf_draw_cache_flush != NULL) {
    blf_draw_cache_flush();
  }

  GPUTexture *texture = blf_batch_cache_texture_load();
  GPU_vertbuf_data_len_set(g_batch.verts, g_batch.glyph_len);
  GPU_vertbuf_use(g_batch.verts); /* send data */

  GPU_batch_program_set_builtin(g_batch.batch, GPU_SHADER_TEXT);
  GPU_batch_texture_bind(g_batch.batch, "glyph", texture);
  GPU_batch_draw(g_batch.batch);

  GPU_blend(GPU_BLEND_NONE);

  GPU_texture_unbind(texture);

  /* restart to 1st vertex data pointers */
  GPU_vertbuf_attr_get_raw_data(g_batch.verts, g_batch.pos_loc, &g_batch.pos_step);
  GPU_vertbuf_attr_get_raw_data(g_batch.verts, g_batch.col_loc, &g_batch.col_step);
  GPU_vertbuf_attr_get_raw_data(g_batch.verts, g_batch.offset_loc, &g_batch.offset_step);
  GPU_vertbuf_attr_get_raw_data(g_batch.verts, g_batch.glyph_size_loc, &g_batch.glyph_size_step);
  g_batch.glyph_len = 0;
}

static void blf_batch_draw_end(void)
{
  if (!g_batch.active) {
    blf_batch_draw();
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Glyph Stepping Utilities (Internal)
 * \{ */

/* Fast path for runs of ASCII characters. Given that common UTF-8
 * input will consist of an overwhelming majority of ASCII
 * characters.
 */

BLI_INLINE GlyphBLF *blf_glyph_from_utf8_and_step(
    FontBLF *font, GlyphCacheBLF *gc, const char *str, size_t str_len, size_t *i_p)
{
  uint charcode = BLI_str_utf8_as_unicode_step(str, str_len, i_p);
  /* Invalid unicode sequences return the byte value, stepping forward one.
   * This allows `latin1` to display (which is sometimes used for file-paths). */
  BLI_assert(charcode != BLI_UTF8_ERR);
  return blf_glyph_ensure(font, gc, charcode);
}

BLI_INLINE int blf_kerning(FontBLF *font, const GlyphBLF *g_prev, const GlyphBLF *g)
{
  if (!FT_HAS_KERNING(font->face) || g_prev == NULL) {
    return 0;
  }

  FT_Vector delta = {KERNING_ENTRY_UNSET};

  /* Get unscaled kerning value from our cache if ASCII. */
  if ((g_prev->c < KERNING_CACHE_TABLE_SIZE) && (g->c < GLYPH_ASCII_TABLE_SIZE)) {
    delta.x = font->kerning_cache->ascii_table[g->c][g_prev->c];
  }

  /* If not ASCII or not found in cache, ask FreeType for kerning. */
  if (UNLIKELY(delta.x == KERNING_ENTRY_UNSET)) {
    /* Note that this function sets delta values to zero on any error. */
    FT_Get_Kerning(font->face, g_prev->idx, g->idx, FT_KERNING_UNSCALED, &delta);
  }

  /* If ASCII we save this value to our cache for quicker access next time. */
  if ((g_prev->c < KERNING_CACHE_TABLE_SIZE) && (g->c < GLYPH_ASCII_TABLE_SIZE)) {
    font->kerning_cache->ascii_table[g->c][g_prev->c] = (int)delta.x;
  }

  if (delta.x != 0) {
    /* Convert unscaled design units to pixels and move pen. */
    return blf_unscaled_F26Dot6_to_pixels(font, delta.x);
  }

  return 0;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Text Drawing: GPU
 * \{ */

static void blf_font_draw_ex(FontBLF *font,
                             GlyphCacheBLF *gc,
                             const char *str,
                             const size_t str_len,
                             struct ResultBLF *r_info,
                             int pen_y)
{
  GlyphBLF *g, *g_prev = NULL;
  int pen_x = 0;
  size_t i = 0;

  if (str_len == 0) {
    /* early output, don't do any IMM OpenGL. */
    return;
  }

  blf_batch_draw_begin(font);

  while ((i < str_len) && str[i]) {
    g = blf_glyph_from_utf8_and_step(font, gc, str, str_len, &i);

    if (UNLIKELY(g == NULL)) {
      continue;
    }
    pen_x += blf_kerning(font, g_prev, g);

    /* do not return this loop if clipped, we want every character tested */
    blf_glyph_draw(font, gc, g, (float)pen_x, (float)pen_y);

    pen_x += g->advance_i;
    g_prev = g;
  }

  blf_batch_draw_end();

  if (r_info) {
    r_info->lines = 1;
    r_info->width = pen_x;
  }
}
void blf_font_draw(FontBLF *font, const char *str, const size_t str_len, struct ResultBLF *r_info)
{
  GlyphCacheBLF *gc = blf_glyph_cache_acquire(font);
  blf_font_draw_ex(font, gc, str, str_len, r_info, 0);
  blf_glyph_cache_release(font);
}

int blf_font_draw_mono(FontBLF *font, const char *str, const size_t str_len, int cwidth)
{
  GlyphBLF *g;
  int col, columns = 0;
  int pen_x = 0, pen_y = 0;
  size_t i = 0;

  GlyphCacheBLF *gc = blf_glyph_cache_acquire(font);

  blf_batch_draw_begin(font);

  while ((i < str_len) && str[i]) {
    g = blf_glyph_from_utf8_and_step(font, gc, str, str_len, &i);

    if (UNLIKELY(g == NULL)) {
      continue;
    }
    /* do not return this loop if clipped, we want every character tested */
    blf_glyph_draw(font, gc, g, (float)pen_x, (float)pen_y);

    col = BLI_wcwidth((char32_t)g->c);
    if (col < 0) {
      col = 1;
    }

    columns += col;
    pen_x += cwidth * col;
  }

  blf_batch_draw_end();

  blf_glyph_cache_release(font);
  return columns;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Text Drawing: Buffer
 * \{ */

/* Sanity checks are done by BLF_draw_buffer() */
static void blf_font_draw_buffer_ex(FontBLF *font,
                                    GlyphCacheBLF *gc,
                                    const char *str,
                                    const size_t str_len,
                                    struct ResultBLF *r_info,
                                    int pen_y)
{
  GlyphBLF *g, *g_prev = NULL;
  int pen_x = (int)font->pos[0];
  int pen_y_basis = (int)font->pos[1] + pen_y;
  size_t i = 0;

  /* buffer specific vars */
  FontBufInfoBLF *buf_info = &font->buf_info;
  const float *b_col_float = buf_info->col_float;
  const unsigned char *b_col_char = buf_info->col_char;
  int chx, chy;
  int y, x;

  /* another buffer specific call for color conversion */

  while ((i < str_len) && str[i]) {
    g = blf_glyph_from_utf8_and_step(font, gc, str, str_len, &i);

    if (UNLIKELY(g == NULL)) {
      continue;
    }
    pen_x += blf_kerning(font, g_prev, g);

    chx = pen_x + ((int)g->pos[0]);
    chy = pen_y_basis + g->dims[1];

    if (g->pitch < 0) {
      pen_y = pen_y_basis + (g->dims[1] - g->pos[1]);
    }
    else {
      pen_y = pen_y_basis - (g->dims[1] - g->pos[1]);
    }

    if ((chx + g->dims[0]) >= 0 && chx < buf_info->dims[0] && (pen_y + g->dims[1]) >= 0 &&
        pen_y < buf_info->dims[1]) {
      /* don't draw beyond the buffer bounds */
      int width_clip = g->dims[0];
      int height_clip = g->dims[1];
      int yb_start = g->pitch < 0 ? 0 : g->dims[1] - 1;

      if (width_clip + chx > buf_info->dims[0]) {
        width_clip -= chx + width_clip - buf_info->dims[0];
      }
      if (height_clip + pen_y > buf_info->dims[1]) {
        height_clip -= pen_y + height_clip - buf_info->dims[1];
      }

      /* drawing below the image? */
      if (pen_y < 0) {
        yb_start += (g->pitch < 0) ? -pen_y : pen_y;
        height_clip += pen_y;
        pen_y = 0;
      }

      if (buf_info->fbuf) {
        int yb = yb_start;
        for (y = ((chy >= 0) ? 0 : -chy); y < height_clip; y++) {
          for (x = ((chx >= 0) ? 0 : -chx); x < width_clip; x++) {
            const char a_byte = *(g->bitmap + x + (yb * g->pitch));
            if (a_byte) {
              const float a = (a_byte / 255.0f) * b_col_float[3];
              const size_t buf_ofs = (((size_t)(chx + x) +
                                       ((size_t)(pen_y + y) * (size_t)buf_info->dims[0])) *
                                      (size_t)buf_info->ch);
              float *fbuf = buf_info->fbuf + buf_ofs;

              float font_pixel[4];
              font_pixel[0] = b_col_float[0] * a;
              font_pixel[1] = b_col_float[1] * a;
              font_pixel[2] = b_col_float[2] * a;
              font_pixel[3] = a;
              blend_color_mix_float(fbuf, fbuf, font_pixel);
            }
          }

          if (g->pitch < 0) {
            yb++;
          }
          else {
            yb--;
          }
        }
      }

      if (buf_info->cbuf) {
        int yb = yb_start;
        for (y = ((chy >= 0) ? 0 : -chy); y < height_clip; y++) {
          for (x = ((chx >= 0) ? 0 : -chx); x < width_clip; x++) {
            const char a_byte = *(g->bitmap + x + (yb * g->pitch));

            if (a_byte) {
              const float a = (a_byte / 255.0f) * b_col_float[3];
              const size_t buf_ofs = (((size_t)(chx + x) +
                                       ((size_t)(pen_y + y) * (size_t)buf_info->dims[0])) *
                                      (size_t)buf_info->ch);
              unsigned char *cbuf = buf_info->cbuf + buf_ofs;

              uchar font_pixel[4];
              font_pixel[0] = b_col_char[0];
              font_pixel[1] = b_col_char[1];
              font_pixel[2] = b_col_char[2];
              font_pixel[3] = unit_float_to_uchar_clamp(a);
              blend_color_mix_byte(cbuf, cbuf, font_pixel);
            }
          }

          if (g->pitch < 0) {
            yb++;
          }
          else {
            yb--;
          }
        }
      }
    }

    pen_x += g->advance_i;
    g_prev = g;
  }

  if (r_info) {
    r_info->lines = 1;
    r_info->width = pen_x;
  }
}

void blf_font_draw_buffer(FontBLF *font,
                          const char *str,
                          const size_t str_len,
                          struct ResultBLF *r_info)
{
  GlyphCacheBLF *gc = blf_glyph_cache_acquire(font);
  blf_font_draw_buffer_ex(font, gc, str, str_len, r_info, 0);
  blf_glyph_cache_release(font);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Text Evaluation: Width to String Length
 *
 * Use to implement exported functions:
 * - #BLF_width_to_strlen
 * - #BLF_width_to_rstrlen
 * \{ */

static bool blf_font_width_to_strlen_glyph_process(
    FontBLF *font, GlyphBLF *g_prev, GlyphBLF *g, int *pen_x, const int width_i)
{
  if (UNLIKELY(g == NULL)) {
    return false; /* continue the calling loop. */
  }
  *pen_x += blf_kerning(font, g_prev, g);
  *pen_x += g->advance_i;

  /* When true, break the calling loop. */
  return (*pen_x >= width_i);
}

size_t blf_font_width_to_strlen(
    FontBLF *font, const char *str, const size_t str_len, float width, float *r_width)
{
  GlyphBLF *g, *g_prev;
  int pen_x, width_new;
  size_t i, i_prev;

  GlyphCacheBLF *gc = blf_glyph_cache_acquire(font);
  const int width_i = (int)width;

  for (i_prev = i = 0, width_new = pen_x = 0, g_prev = NULL; (i < str_len) && str[i];
       i_prev = i, width_new = pen_x, g_prev = g) {
    g = blf_glyph_from_utf8_and_step(font, gc, str, str_len, &i);

    if (blf_font_width_to_strlen_glyph_process(font, g_prev, g, &pen_x, width_i)) {
      break;
    }
  }

  if (r_width) {
    *r_width = (float)width_new;
  }

  blf_glyph_cache_release(font);
  return i_prev;
}

size_t blf_font_width_to_rstrlen(
    FontBLF *font, const char *str, const size_t str_len, float width, float *r_width)
{
  GlyphBLF *g, *g_prev;
  int pen_x, width_new;
  size_t i, i_prev, i_tmp;
  const char *s, *s_prev;

  GlyphCacheBLF *gc = blf_glyph_cache_acquire(font);
  const int width_i = (int)width;

  i = BLI_strnlen(str, str_len);
  s = BLI_str_find_prev_char_utf8(&str[i], str);
  i = (size_t)(s - str);
  s_prev = BLI_str_find_prev_char_utf8(s, str);
  i_prev = (size_t)(s_prev - str);

  i_tmp = i;
  g = blf_glyph_from_utf8_and_step(font, gc, str, str_len, &i_tmp);
  for (width_new = pen_x = 0; (s != NULL);
       i = i_prev, s = s_prev, g = g_prev, g_prev = NULL, width_new = pen_x) {
    s_prev = BLI_str_find_prev_char_utf8(s, str);
    i_prev = (size_t)(s_prev - str);

    if (s_prev != NULL) {
      i_tmp = i_prev;
      g_prev = blf_glyph_from_utf8_and_step(font, gc, str, str_len, &i_tmp);
      BLI_assert(i_tmp == i);
    }

    if (blf_font_width_to_strlen_glyph_process(font, g_prev, g, &pen_x, width_i)) {
      break;
    }
  }

  if (r_width) {
    *r_width = (float)width_new;
  }

  blf_glyph_cache_release(font);
  return i;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Text Evaluation: Glyph Bound Box with Callback
 * \{ */

static void blf_font_boundbox_ex(FontBLF *font,
                                 GlyphCacheBLF *gc,
                                 const char *str,
                                 const size_t str_len,
                                 rctf *box,
                                 struct ResultBLF *r_info,
                                 int pen_y)
{
  GlyphBLF *g, *g_prev = NULL;
  int pen_x = 0;
  size_t i = 0;
  rctf gbox;

  box->xmin = 32000.0f;
  box->xmax = -32000.0f;
  box->ymin = 32000.0f;
  box->ymax = -32000.0f;

  while ((i < str_len) && str[i]) {
    g = blf_glyph_from_utf8_and_step(font, gc, str, str_len, &i);

    if (UNLIKELY(g == NULL)) {
      continue;
    }
    pen_x += blf_kerning(font, g_prev, g);

    gbox.xmin = (float)pen_x;
    gbox.xmax = (float)pen_x + g->advance;
    gbox.ymin = g->box.ymin + (float)pen_y;
    gbox.ymax = g->box.ymax + (float)pen_y;

    if (gbox.xmin < box->xmin) {
      box->xmin = gbox.xmin;
    }
    if (gbox.ymin < box->ymin) {
      box->ymin = gbox.ymin;
    }

    if (gbox.xmax > box->xmax) {
      box->xmax = gbox.xmax;
    }
    if (gbox.ymax > box->ymax) {
      box->ymax = gbox.ymax;
    }

    pen_x += g->advance_i;
    g_prev = g;
  }

  if (box->xmin > box->xmax) {
    box->xmin = 0.0f;
    box->ymin = 0.0f;
    box->xmax = 0.0f;
    box->ymax = 0.0f;
  }

  if (r_info) {
    r_info->lines = 1;
    r_info->width = pen_x;
  }
}
void blf_font_boundbox(
    FontBLF *font, const char *str, const size_t str_len, rctf *r_box, struct ResultBLF *r_info)
{
  GlyphCacheBLF *gc = blf_glyph_cache_acquire(font);
  blf_font_boundbox_ex(font, gc, str, str_len, r_box, r_info, 0);
  blf_glyph_cache_release(font);
}

void blf_font_width_and_height(FontBLF *font,
                               const char *str,
                               const size_t str_len,
                               float *r_width,
                               float *r_height,
                               struct ResultBLF *r_info)
{
  float xa, ya;
  rctf box;

  if (font->flags & BLF_ASPECT) {
    xa = font->aspect[0];
    ya = font->aspect[1];
  }
  else {
    xa = 1.0f;
    ya = 1.0f;
  }

  if (font->flags & BLF_WORD_WRAP) {
    blf_font_boundbox__wrap(font, str, str_len, &box, r_info);
  }
  else {
    blf_font_boundbox(font, str, str_len, &box, r_info);
  }
  *r_width = (BLI_rctf_size_x(&box) * xa);
  *r_height = (BLI_rctf_size_y(&box) * ya);
}

float blf_font_width(FontBLF *font,
                     const char *str,
                     const size_t str_len,
                     struct ResultBLF *r_info)
{
  float xa;
  rctf box;

  if (font->flags & BLF_ASPECT) {
    xa = font->aspect[0];
  }
  else {
    xa = 1.0f;
  }

  if (font->flags & BLF_WORD_WRAP) {
    blf_font_boundbox__wrap(font, str, str_len, &box, r_info);
  }
  else {
    blf_font_boundbox(font, str, str_len, &box, r_info);
  }
  return BLI_rctf_size_x(&box) * xa;
}

float blf_font_height(FontBLF *font,
                      const char *str,
                      const size_t str_len,
                      struct ResultBLF *r_info)
{
  float ya;
  rctf box;

  if (font->flags & BLF_ASPECT) {
    ya = font->aspect[1];
  }
  else {
    ya = 1.0f;
  }

  if (font->flags & BLF_WORD_WRAP) {
    font_font_boundbox__wrap(font, str, str_len, &box, r_info);
  }
  else {
    font_font_boundbox(font, str, str_len, &box, r_info);
  }
  return lib_rctf_size_y(&box) * ya;
}

float font_font_fixed_width(Font *font)
{
  FontGlyphCache *gc = font_glyph_cache_acquire(font);
  float width = (gc) ? (float)gc->fixed_width : font->size / 2.0f;
  font_glyph_cache_release(font);
  return width;
}

static void font_font_boundbox_foreach_glyph_ex(Font *font,
                                               FontGlyphCache *gc,
                                               const char *str,
                                               const size_t str_len,
                                               FontGlyphBoundsFn user_fn,
                                               void *user_data,
                                               struct FontResult *r_info,
                                               int pen_y)
{
  FontGlyph *g, *g_prev = NULL;
  int pen_x = 0;
  size_t i = 0, i_curr;
  rcti gbox;

  if (str_len == 0) {
    /* early output. */
    return;
  }

  while ((i < str_len) && str[i]) {
    i_curr = i;
    g = font_glyph_from_utf8_and_step(font, gc, str, str_len, &i);

    if (UNLIKELY(g == NULL)) {
      continue;
    }
    pen_x += font_kerning(font, g_prev, g);

    gbox.xmin = pen_x;
    gbox.xmax = gbox.xmin + MIN2(g->advance_i, g->dims[0]);
    gbox.ymin = pen_y;
    gbox.ymax = gbox.ymin - g->dims[1];

    pen_x += g->advance_i;

    if (user_fn(str, i_curr, &gbox, g->advance_i, &g->box, g->pos, user_data) == false) {
      break;
    }

    g_prev = g;
  }

  if (r_info) {
    r_info->lines = 1;
    r_info->width = pen_x;
  }
}
void font_font_boundbox_foreach_glyph(FontBLF *font,
                                     const char *str,
                                     const size_t str_len,
                                     BLF_GlyphBoundsFn user_fn,
                                     void *user_data,
                                     struct ResultBLF *r_info)
{
  GlyphCacheBLF *gc = blf_glyph_cache_acquire(font);
  blf_font_boundbox_foreach_glyph_ex(font, gc, str, str_len, user_fn, user_data, r_info, 0);
  blf_glyph_cache_release(font);
}
