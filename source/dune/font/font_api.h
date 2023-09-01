#pragma once

#include "lib_compiler_attrs.h"
#include "lib_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* enable this only if needed (unused circa 2016) */
#define FONT_BLUR_ENABLE 0

struct ColorManagedDisplay;
struct FontResult;
struct rctf;
struct rcti;

int font_init(void);
void font_exit(void);

void font_cache_clear(void);

/* Optional cache flushing function, called before font_batch_draw. */
void font_cache_flush_set_fn(void (*cache_flush_fn)(void));

/* Loads a font, or returns an already loaded font and increments its ref count. */
int font_load(const char *name) ATTR_NONNULL();
int font_load_mem(const char *name, const unsigned char *mem, int mem_size) ATTR_NONNULL();

int font_load_unique(const char *name) ATTR_NONNULL();
int font_load_mem_unique(const char *name, const unsigned char *mem, int mem_size) ATTR_NONNULL();

void font_unload(const char *name) ATTR_NONNULL();
void font_unload_id(int fontid);

char *font_display_name_from_file(const char *filename);

/* Check if font supports a particular glyph. */
bool font_has_glyph(int fontid, unsigned int unicode);

/* Attach a file with metrics information from memory. */
void font_metrics_attach(int fontid, unsigned char *mem, int mem_size);

void font_aspect(int fontid, float x, float y, float z);
void font_position(int fontid, float x, float y, float z);
void font_size(int fontid, float size, int dpi);

/* Goal: small but useful color API. */
void font_color4ubv(int fontid, const unsigned char rgba[4]);
void font_color3ubv(int fontid, const unsigned char rgb[3]);
void font_color3ubv_alpha(int fontid, const unsigned char rgb[3], unsigned char alpha);
void font_color4ub(
    int fontid, unsigned char r, unsigned char g, unsigned char b, unsigned char alpha);
void font_color3ub(int fontid, unsigned char r, unsigned char g, unsigned char b);
void font_color4f(int fontid, float r, float g, float b, float a);
void font_color4fv(int fontid, const float rgba[4]);
void font_color3f(int fontid, float r, float g, float b);
void font_color3fv_alpha(int fontid, const float rgb[3], float alpha);
/* Also available: `UI_FontThemeColor(fontid, colorid)`. */

/* Set a 4x4 matrix to be multiplied before draw the text.
 * Remember that you need call font_enable(FONT_MATRIX)
 * to enable this.
 *
 * The order of the matrix is like GL:
 *  | m[0]  m[4]  m[8]  m[12] |
 *  | m[1]  m[5]  m[9]  m[13] |
 *  | m[2]  m[6]  m[10] m[14] |
 *  | m[3]  m[7]  m[11] m[15] |
 */
void font_matrix(int fontid, const float m[16]);

/* Batch draw-calls together as long as
 * the model-view matrix and the font remain unchanged. */
void font_batch_draw_begin(void);
void font_batch_draw_flush(void);
void font_batch_draw_end(void);

/* Draw the string using the current font. */
void font_draw_ex(int fontid, const char *str, size_t str_len, struct FontResult *r_info)
    ATTR_NONNULL(2);
void font_draw(int fontid, const char *str, size_t str_len) ATTR_NONNULL(2);
int font_draw_mono(int fontid, const char *str, size_t str_len, int cwidth) ATTR_NONNULL(2);

typedef bool (*FontGlyphBoundsFn)(const char *str,
                                  size_t str_step_ofs,
                                  const struct rcti *glyph_step_bounds,
                                  int glyph_advance_x,
                                  const struct rctf *glyph_bounds,
                                  const int glyph_bearing[2],
                                  void *user_data);

/* Run user_fn for each character, with the bound-box that would be used for drawing.
 *
 * param user_fn: Cb that runs on each glyph, returning false early exits.
 * param user_data: User argument passed to user_fn.
 * The font position, clipping, matrix and rotation are not applied */
void font_boundbox_foreach_glyph_ex(int fontid,
                                   const char *str,
                                   size_t str_len,
                                   FontGlyphBoundsFn user_fn,
                                   void *user_data,
                                   struct FontResult *r_info) ATTR_NONNULL(2);
void font_boundbox_foreach_glyph(int fontid,
                                const char *str,
                                size_t str_len,
                                FontGlyphBoundsFn user_fn,
                                void *user_data) ATTR_NONNULL(2);

/* Get the string byte offset that fits within a given width. */
size_t font_width_to_strlen(
    int fontid, const char *str, size_t str_len, float width, float *r_width) ATTR_NONNULL(2);
/* Same as font_width_to_strlen but search from the string end. */
size_t font_width_to_rstrlen(
    int fontid, const char *str, size_t str_len, float width, float *r_width) ATTR_NONNULL(2);

/* This fn return the bounding box of the string
 * and are not multiplied by the aspect. */
void font_boundbox_ex(int fontid,
                     const char *str,
                     size_t str_len,
                     struct rctf *box,
                     struct FontResult *r_info) ATTR_NONNULL(2);
void font_boundbox(int fontid, const char *str, size_t str_len, struct rctf *box) ATTR_NONNULL();

/* The next both fn return the width and height
 * of the string, using the current font and both value
 * are multiplied by the aspect of the font. */
float font_width_ex(int fontid, const char *str, size_t str_len, struct FontResult *r_info)
    ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(2);
float font_width(int fontid, const char *str, size_t str_len) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();
float font_height_ex(int fontid, const char *str, size_t str_len, struct FontResult *r_info)
    ATTR_WARN_UNUSED_RESULT ATTR_NONNULL(2);
float font_height(int fontid, const char *str, size_t str_len) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

/* Return dimensions of the font without any sample text. */
int font_height_max(int fontid) ATTR_WARN_UNUSED_RESULT;
float font_width_max(int fontid) ATTR_WARN_UNUSED_RESULT;
float font_descender(int fontid) ATTR_WARN_UNUSED_RESULT;
float font_ascender(int fontid) ATTR_WARN_UNUSED_RESULT;

/* The following fn return the width and height of the string, but
 * just in one call, so avoid extra freetype2 stuff. */
void font_width_and_height(
    int fontid, const char *str, size_t str_len, float *r_width, float *r_height) ATTR_NONNULL();

/* For fixed width fonts only, returns the width of a
 * character */
float font_fixed_width(int fontid) ATTR_WARN_UNUSED_RESULT;

/* By default, rotation and clipping are disable and
 * have to be enable/disable using font_enable/disable. */
void font_rotation(int fontid, float angle);
void font_clipping(int fontid, float xmin, float ymin, float xmax, float ymax);
void font_wordwrap(int fontid, int wrap_width);

#if FONT_BLUR_ENABLE
void font_blur(int fontid, int size);
#endif

void font_enable(int fontid, int option);
void font_disable(int fontid, int option);

/* Shadow options, level is the blur level, can be 3, 5 or 0 and
 * the other argument are the RGBA color.
 * Take care that shadow need to be enable using font_enable! */
void font_shadow(int fontid, int level, const float rgba[4]) ATTR_NONNULL(3);

/* Set the offset for shadow text, this is the current cursor
 * position plus this offset, don't need call font_position before
 * this fn, the current position is calculate only on
 * font_draw, so it's safe call this whenever you like. */
void font_shadow_offset(int fontid, int x, int y);

/* Set the buffer, size and number of channels to draw, one thing to take care is call
 * this fn with NULL ptr when we finish, for example:
 *
 * font_buffer(my_fbuf, my_cbuf, 100, 100, 4, true, NULL);
 *
 * ... set color, position and draw ...
 *
 * font_buffer(NULL, NULL, NULL, 0, 0, false, NULL);
 */
void font_buffer(int fontid,
                float *fbuf,
                unsigned char *cbuf,
                int w,
                int h,
                int nch,
                struct ColorManagedDisplay *display);

/* Set the color to be used for text. */
void font_buffer_col(int fontid, const float rgba[4]) ATTR_NONNULL(2);

/* Draw the string into the buffer, this function draw in both buffer,
 * float and unsigned char _BUT_ it's not necessary set both buffer, NULL is valid here. */
void font_draw_buffer_ex(int fontid, const char *str, size_t str_len, struct FontResult *r_info)
    ATTR_NONNULL(2);
void font_draw_buffer(int fontid, const char *str, size_t str_len) ATTR_NONNULL(2);

/* Add a path to the font dir paths. */
void font_dir_add(const char *path) ATTR_NONNULL();

/* Remove a path from the font dir paths */
void font_dir_rem(const char *path) ATTR_NONNULL();

/* Return an array with all the font dir (this can be used for file-selector). */
char **font_dir_get(int *ndir) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();

/* Free the data return by font_dir_get */
void font_dir_free(char **dirs, int count) ATTR_NONNULL();

/* font_thumbs.c */

/* This fn is used for generating thumbnail previews.
 * called from a thread, so it bypasses the normal font_* api (which isn't thread-safe) */
void font_thumb_preview(const char *filename,
                       const char **draw_str,
                       const char **lang_draw_str,
                       unsigned char draw_str_lines,
                       const float font_color[4],
                       int font_size,
                       unsigned char *buf,
                       int w,
                       int h,
                       int channels) ATTR_NONNULL();

/* font_default.c */

void font_default_dpi(int dpi);
void font_default_size(float size);
void font_default_set(int fontid);
/* Get default font ID so we can pass it to other fns */
int font_default(void);
/* Draw the string using the default font, size and DPI. */
void font_draw_default(float x, float y, float z, const char *str, size_t str_len) ATTR_NONNULL();
/* Set size and DPI, and return default font ID */
int font_set_default(void);

/* font_font_default.c */

int font_load_default(bool unique);
int font_load_mono_default(bool unique);

#ifdef DEBUG
void font_state_print(int fontid);
#endif

/* font->flags. */
#define FONT_ROTATION (1 << 0)
#define FONT_CLIPPING (1 << 1)
#define FONT_SHADOW (1 << 2)
// #define FONT_FLAG_UNUSED_3 (1 << 3) /* dirty */
#define FONT_MATRIX (1 << 4)
#define FONT_ASPECT (1 << 5)
#define FONT_WORD_WRAP (1 << 6)
#define FONT_MONOCHROME (1 << 7) /* no-AA */
#define FONT_HINTING_NONE (1 << 8)
#define FONT_HINTING_SLIGHT (1 << 9)
#define FONT_HINTING_FULL (1 << 10)
#define FONT_BOLD (1 << 11)
#define FONT_ITALIC (1 << 12)

#define FONT_DRAW_STR_DUMMY_MAX 1024

/* XXX, bad design */
extern int font_mono_font;
extern int font_mono_font_render; /* don't mess drawing with render threads. */

/* Result of drawing/evaluating the string */
struct FontResult {
  /**
   * Number of lines drawn when #BLF_WORD_WRAP is enabled (both wrapped and `\n` newline).
   */
  int lines;
  /**
   * The 'cursor' position on completion (ignoring character boundbox).
   */
  int width;
};

#ifdef __cplusplus
}
#endif
