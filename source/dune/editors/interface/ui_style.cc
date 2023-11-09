#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mem_guardedalloc.h"

#include "types_userdef.h"

#include "lib_list.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "dune_global.h"

#include "font_api.h"

#include "lang.h"

#include "ui.hh"

#include "ed_datafiles.h"

#include "ui_intern.hh"

#ifdef WIN32
#  include "lib_math_base.h" /* M_PI */
#endif

static void fontstyle_set_ex(const uiFontStyle *fs, const float dpi_fac);

/* style + theme + layout-engine = UI */

/* This is a complete set of layout rules, the 'state' of the Layout
 * Engine. Multiple styles are possible, defined via C. Styles
 * get a name, and will typically get activated per rgn type, like
 * `Header`, or `Listview` or `Toolbar`. Props of Style definitions
 * are:
 *
 * - default column props, internal spacing, aligning, min/max width
 * - btn alignment rules (for groups)
 * - label placement rules
 * - internal labeling or external labeling default
 * - default minimum widths for btns/labels (in amount of chars)
 * - font types, styles and relative sizes for Pnl titles, labels, etc. */

static uiStyle *ui_style_new(List *styles, const char *name, short uifont_id)
{
  uiStyle *style = mem_cnew<uiStyle>(__func__);

  lib_addtail(styles, style);
  STRNCPY(style->name, name);

  style->pnlzoom = 1.0; /* unused */

  style->pnltitle.uifont_id = uifont_id;
  style->pnltitle.points = UI_DEFAULT_TITLE_POINTS;
  style->pnltitle.character_weight = 400;
  style->pnltitle.shadow = 3;
  style->pnltitle.shadx = 0;
  style->pnltitle.shady = -1;
  style->pnltitle.shadowalpha = 0.5f;
  style->pnltitle.shadowcolor = 0.0f;

  style->grouplabel.uifont_id = uifont_id;
  style->grouplabel.points = UI_DEFAULT_TITLE_POINTS;
  style->grouplabel.char_weight = 400;
  style->grouplabel.shadow = 3;
  style->grouplabel.shadx = 0;
  style->grouplabel.shady = -1;
  style->grouplabel.shadowalpha = 0.5f;
  style->grouplabel.shadowcolor = 0.0f;

  style->widgetlabel.uifont_id = uifont_id;
  style->widgetlabel.points = UI_DEFAULT_TXT_POINTS;
  style->widgetlabel.char_weight = 400;
  style->widgetlabel.shadow = 3;
  style->widgetlabel.shadx = 0;
  style->widgetlabel.shady = -1;
  style->widgetlabel.shadowalpha = 0.5f;
  style->widgetlabel.shadowcolor = 0.0f;

  style->widget.uifont_id = uifont_id;
  style->widget.points = UI_DEFAULT_TXT_POINTS;
  style->widget.char_weight = 400;
  style->widget.shadow = 1;
  style->widget.shady = -1;
  style->widget.shadowalpha = 0.5f;
  style->widget.shadowcolor = 0.0f;

  style->columnspace = 8;
  style->templatespace = 5;
  style->boxspace = 5;
  style->btnspacex = 8;
  style->btnspacey = 2;
  style->pnlspace = 8;
  style->pnlouter = 4;

  return style;
}

static uiFont *uifont_to_dunefont(int id)
{
  uiFont *font = static_cast<uiFont *>(U.uifonts.first);

  for (; font; font = font->next) {
    if (font->uifont_id == id) {
      return font;
    }
  }
  return static_cast<uiFont *>(U.uifonts.first);
}

/* draw */
void ui_fontstyle_draw_ex(const uiFontStyle *fs,
                          const rcti *rect,
                          const char *str,
                          const size_t str_len,
                          const uchar col[4],
                          const uiFontStyleDraw_Params *fs_params,
                          int *r_xofs,
                          int *r_yofs,
                          ResultFont *r_info)
{
  int xofs = 0, yofs;
  int font_flag = FONT_CLIPPING;

  ui_fontstyle_set(fs);

  /* set the flag */
  if (fs->shadow) {
    font_flag |= FONT_SHADOW;
    const float shadow_color[4] = {
        fs->shadowcolor, fs->shadowcolor, fs->shadowcolor, fs->shadowalpha};
    font_shadow(fs->uifont_id, fs->shadow, shadow_color);
    font_shadow_offset(fs->uifont_id, fs->shadx, fs->shady);
  }
  if (fs_params->word_wrap == 1) {
    font_flag |= FONT_WORD_WRAP;
  }
  if (fs->bold) {
    font_flag |= FONT_BOLD;
  }
  if (fs->italic) {
    font_flag |= FONT_ITALIC;
  }

  font_enable(fs->uifont_id, font_flag);

  if (fs_params->word_wrap == 1) {
    /* Draw from bound-box top. */
    yofs = lib_rcti_size_y(rect) - font_height_max(fs->uifont_id);
  }
  else {
    /* Draw from bound-box center. */
    const int height = font_ascender(fs->uifont_id) + font_descender(fs->uifont_id);
    yofs = ceil(0.5f * (lib_rcti_size_y(rect) - height));
  }

  if (fs_params->align == UI_STYLE_TXT_CENTER) {
    xofs = floor(0.5f * (lib_rcti_size_x(rect) - font_width(fs->uifont_id, str, str_len)));
  }
  else if (fs_params->align == UI_STYLE_TXT_RIGHT) {
    xofs = lib_rcti_size_x(rect) - font_width(fs->uifont_id, str, str_len);
  }

  yofs = std::max(0, yofs);
  xofs = std::max(0, xofs);

  font_clipping(fs->uifont_id, rect->xmin, rect->ymin, rect->xmax, rect->ymax);
  font_pos(fs->uifont_id, rect->xmin + xofs, rect->ymin + yofs, 0.0f);
  font_color4ubv(fs->uifont_id, col);

  font_draw_ex(fs->uifont_id, str, str_len, r_info);

  font_disable(fs->uifont_id, font_flag);

  if (r_xofs) {
    *r_xofs = xofs;
  }
  if (r_yofs) {
    *r_yofs = yofs;
  }
}

void ui_fontstyle_draw(const uiFontStyle *fs,
                       const rcti *rect,
                       const char *str,
                       const size_t str_len,
                       const uchar col[4],
                       const uiFontStyleDraw_Params *fs_params)
{
  ui_fontstyle_draw_ex(fs, rect, str, str_len, col, fs_params, nullptr, nullptr, nullptr);
}

void ui_fontstyle_draw_rotated(const uiFontStyle *fs,
                               const rcti *rect,
                               const char *str,
                               const uchar col[4])
{
  float height;
  int xofs, yofs;
  float angle;
  rcti txtrect;

  ui_fontstyle_set(fs);

  height = font_ascender(fs->uifont_id) + font_descender(fs->uifont_id);
  /* becomes x-offset when rotated */
  xofs = ceil(0.5f * (lib_rcti_size_y(rect) - height));

  /* ignore UI_STYLE, always aligned to top */
  /* Rotate counter-clockwise for now (assumes left-to-right language). */
  xofs += height;
  yofs = font_width(fs->uifont_id, str, FONT_DRAW_STR_DUMMY_MAX) + 5;
  angle = M_PI_2;

  /* translate rect to vertical */
  txtrect.xmin = rect->xmin - lib_rcti_size_y(rect);
  txtrect.ymin = rect->ymin - lib_rcti_size_x(rect);
  txtrect.xmax = rect->xmin;
  txtrect.ymax = rect->ymin;

  /* clip is very strict, so we give it some space */
  /* clipping is done without rotation, so make rect big enough to contain both positions */
  font_clipping(fs->uifont_id,
               txtrect.xmin - 1,
               txtrect.ymin - yofs - xofs - 4,
               rect->xmax + 1,
               rect->ymax + 4);
  font_enable(fs->uifont_id, FONT_CLIPPING);
  font_pos(fs->uifont_id, txtrect.xmin + xofs, txtrect.ymax - yofs, 0.0f);

  font_enable(fs->uifont_id, FONT_ROTATION);
  font_rotation(fs->uifont_id, angle);
  font_color4ubv(fs->uifont_id, col);

  if (fs->shadow) {
    font_enable(fs->uifont_id, FONT_SHADOW);
    const float shadow_color[4] = {
        fs->shadowcolor, fs->shadowcolor, fs->shadowcolor, fs->shadowalpha};
    font_shadow(fs->uifont_id, fs->shadow, shadow_color);
    font_shadow_offset(fs->uifont_id, fs->shadx, fs->shady);
  }

  font_draw(fs->uifont_id, str, FONT_DRAW_STR_DUMMY_MAX);
  font_disable(fs->uifont_id, FONT_ROTATION);
  font_disable(fs->uifont_id, FONT_CLIPPING);
  if (fs->shadow) {
    font_disable(fs->uifont_id, FONT_SHADOW);
  }
}

void ui_fontstyle_draw_simple(
    const uiFontStyle *fs, float x, float y, const char *str, const uchar col[4])
{
  ui_fontstyle_set(fs);
  font_pos(fs->uifont_id, x, y, 0.0f);
  font_color4ubv(fs->uifont_id, col);
  font_draw(fs->uifont_id, str, FONT_DRAW_STR_DUMMY_MAX);
}

void ui_fontstyle_draw_simple_backdrop(const uiFontStyle *fs,
                                       float x,
                                       float y,
                                       const char *str,
                                       const float col_fg[4],
                                       const float col_bg[4])
{
  ui_fontstyle_set(fs);

  {
    const int width = font_width(fs->uifont_id, str, FONT_DRAW_STR_DUMMY_MAX);
    const int height = font_height_max(fs->uifont_id);
    const int decent = font_descender(fs->uifont_id);
    const float margin = height / 4.0f;

    rctf rect;
    rect.xmin = x - margin;
    rect.xmax = x + width + margin;
    rect.ymin = (y + decent) - margin;
    rect.ymax = (y + decent) + height + margin;
    ui_draw_roundbox_corner_set(UI_CNR_ALL);
    ui_draw_roundbox_4fv(&rect, true, margin, col_bg);
  }

  font_pos(fs->uifont_id, x, y, 0.0f);
  font_color4fv(fs->uifont_id, col_fg);
  font_draw(fs->uifont_id, str, FONT_DRAW_STR_DUMMY_MAX);
}

/* helpers */
const uiStyle *ui_style_get()
{
#if 0
  uiStyle *style = nullptr;
  /* offset is two struct uiStyle ptrs */
  style = lib_findstring(&U.uistyles, "Unifont Style", sizeof(style) * 2);
  return (style != nullptr) ? style : U.uistyles.first;
#else
  return static_cast<const uiStyle *>(U.uistyles.first);
#endif
}

const uiStyle *ui_style_get_dpi()
{
  const uiStyle *style = ui_style_get();
  static uiStyle _style;

  _style = *style;

  _style.pnltitle.shadx = short(UI_SCALE_FAC * _style.pnltitle.shadx);
  _style.pnltitle.shady = short(UI_SCALE_FAC * _style.pnltitle.shady);
  _style.grouplabel.shadx = short(UI_SCALE_FAC * _style.grouplabel.shadx);
  _style.grouplabel.shady = short(UI_SCALE_FAC * _style.grouplabel.shady);
  _style.widgetlabel.shadx = short(UI_SCALE_FAC * _style.widgetlabel.shadx);
  _style.widgetlabel.shady = short(UI_SCALE_FAC * _style.widgetlabel.shady);

  _style.columnspace = short(UI_SCALE_FAC * _style.columnspace);
  _style.templatespace = short(UI_SCALE_FAC * _style.templatespace);
  _style.boxspace = short(UI_SCALE_FAC * _style.boxspace);
  _style.btnspacex = short(UI_SCALE_FAC * _style.btnspacex);
  _style.btnspacey = short(UI_SCALE_FAC * _style.btnspacey);
  _style.pnlspace = short(UI_SCALE_FAC * _style.pnlspace);
  _style.pnlouter = short(UI_SCALE_FAC * _style.pnlouter);

  return &_style;
}

int ui_fontstyle_string_width(const uiFontStyle *fs, const char *str)
{
  ui_fontstyle_set(fs);
  return int(font_width(fs->uifont_id, str, FONT_DRAW_STR_DUMMY_MAX));
}

int ui_fontstyle_string_width_with_block_aspect(const uiFontStyle *fs,
                                                const char *str,
                                                const float aspect)
{
  /* FIXME: the final scale of the font is rounded which should be accounted for.
   * Failing to do so causes bad alignment when zoomed out very far in the node-editor. */
  fontstyle_set_ex(fs, UI_SCALE_FAC / aspect);
  return int(font_width(fs->uifont_id, str, FONT_DRAW_STR_DUMMY_MAX) * aspect);
}

int ui_fontstyle_height_max(const uiFontStyle *fs)
{
  ui_fontstyle_set(fs);
  return font_height_max(fs->uifont_id);
}

/* init exit */
void uiStyleInit()
{
  const uiStyle *style = static_cast<uiStyle *>(U.uistyles.first);

  /* Recover from uninitialized DPI. */
  if (U.dpi == 0) {
    U.dpi = 72;
  }
  CLAMP(U.dpi, 48, 144);

  /* Needed so that custom fonts are always first. */
  font_unload_all();

  uiFont *font_first = static_cast<uiFont *>(U.uifonts.first);

  /* default builtin */
  if (font_first == nullptr) {
    font_first = mem_cnew<uiFont>(__func__);
    lib_addtail(&U.uifonts, font_first);
  }

  if (U.font_path_ui[0]) {
    STRNCPY(font_first->filepath, U.font_path_ui);
    font_first->uifont_id = UIFONT_CUSTOM1;
  }
  else {
    STRNCPY(font_first->filepath, "default");
    font_first->uifont_id = UIFONT_DEFAULT;
  }

  LIST_FOREACH (uiFont *, font, &U.uifonts) {
    const bool unique = false;

    if (font->uifont_id == UIFONT_DEFAULT) {
      font->dunef_id = font_load_default(unique);
    }
    else {
      font->dunef_id = font_load(font->filepath);
      if (font->dunef_id == -1) {
        font->dunef_id = font_load_default(unique);
      }
    }

    font_default_set(font->dunef_id);

    if (font->dunef_id == -1) {
      if (G.debug & G_DEBUG) {
        printf("%s: error, no fonts available\n", __func__);
      }
    }
  }

  if (style == nullptr) {
    style = ui_style_new(&U.uistyles, "Default Style", UIFONT_DEFAULT);
  }

  font_cache_flush_set_fn(ui_widgetbase_draw_cache_flush);

  font_default_size(style->widgetlabel.points);

  /* This should be moved into a style,
   * but for now best only load the monospaced font once. */
  lib_assert(font_mono_font == -1);
  /* Use unique font loading to avoid thread safety issues with mono font
   * used for render metadata stamp in threads. */
  if (U.font_path_ui_mono[0]) {
    font_mono_font = font_load_unique(U.font_path_ui_mono);
  }
  if (font_mono_font == -1) {
    const bool unique = true;
    font_mono_font = font_load_mono_default(unique);
  }

  /* Set default flags based on UI prefs (not render fonts) */
  {
    const int flag_disable = (FONT_MONOCHROME | FONT_HINTING_NONE | FONT_HINTING_SLIGHT |
                              FONT_HINTING_FULL | FONT_RENDER_SUBPIXELAA);
    int flag_enable = 0;

    if (U.txt_render & USER_TXT_HINTING_NONE) {
      flag_enable |= FONT_HINTING_NONE;
    }
    else if (U.txt_render & USER_TXT_HINTING_SLIGHT) {
      flag_enable |= FONT_HINTING_SLIGHT;
    }
    else if (U.txt_render & USER_TXT_HINTING_FULL) {
      flag_enable |= FONT_HINTING_FULL;
    }

    if (U.txt_render & USER_TXT_DISABLE_AA) {
      flag_enable |= FONT_MONOCHROME;
    }
    if (U.txt_render & USER_TXT_RENDER_SUBPIXELAA) {
      flag_enable |= FONT_RENDER_SUBPIXELAA;
    }

    LIST_FOREACH (uiFont *, font, &U.uifonts) {
      if (font->dunef_id != -1) {
        font_disable(font->font_id, flag_disable);
        font_enable(font->font_id, flag_enable);
      }
    }
    if (font_mono_font != -1) {
      font_disable(dunef_mono_font, flag_disable);
      font_enable(dunef_mono_font, flag_enable);
    }
  }

  /* Second for rendering else we get threading problems,
   * This isn't good that the render font depends on the prefs,
   * keep for now though, since without this there is no way to display many unicode chars. */
  if (font_mono_font_render == -1) {
    const bool unique = true;
    font_mono_font_render = font_load_mono_default(unique);
  }

  /* Load the fallback fonts last. */
  font_load_font_stack();
}

static void fontstyle_set_ex(const uiFontStyle *fs, const float dpi_fac)
{
  uiFont *font = uifont_to_dunefont(fs->uifont_id);

  font_size(font->dunef_id, fs->points * dpi_fac);
  font_char_weight(fs->uifont_id, fs->char_weight);
}

void ui_fontstyle_set(const uiFontStyle *fs)
{
  fontstyle_set_ex(fs, UI_SCALE_FAC);
}
