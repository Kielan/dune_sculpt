/* ed ui
 * A special set of icons to represent input devices,
 * this is a mix of text (via fonts) and a handful of custom glyphs for special keys.
 *
 * Ev codes are used as ids*/

#include "gpu_batch.h"
#include "gpu_state.h"

#include "lib_string.h"

#include "font_api.h"

#include "ui.hh"

#include "ui_intern.hh"

static void icon_draw_rect_input_txt(
    const rctf *rect, const float color[4], const char *str, float font_size, float v_offset)
{
  font_batch_draw_flush();
  const int font_id = font_default();
  font_color4fv(font_id, color);
  font_size(font_id, font_size * UI_SCALE_FAC);
  float width, height;
  font_width_and_height(font_id, str, FONT_DRAW_STR_DUMMY_MAX, &width, &height);
  const float x = trunc(rect->xmin + (((rect->xmax - rect->xmin) - width) / 2.0f));
  const float y = rect->ymin + (((rect->ymax - rect->ymin) - height) / 2.0f) +
                  (v_offset * UI_SCALE_FAC);
  font_pos(font_id, x, y, 0.0f);
  font_draw(font_id, str, BLF_DRAW_STR_DUMMY_MAX);
  font_batch_draw_flush();
}

void icon_draw_rect_input(
    float x, float y, int w, int h, float /*alpha*/, short ev_type, short /*ev_value*/)
{
  rctf rect{};
  rect.xmin = int(x) - U.pixelsize;
  rect.xmax = int(x + w + U.pixelsize);
  rect.ymin = int(y);
  rect.ymax = int(y + h);

  float color[4];
  gpu_line_width(1.0f);
  ui_GetThemeColor4fv(TH_TXT, color);
  ui_draw_roundbox_corner_set(UI_CNR_ALL);
  ui_draw_roundbox_aa(&rect, false, 3.0f * U.pixelsize, color);

  const enum {
    UNIX,
    MACOS,
    MSWIN,
  } platform =

#if defined(__APPLE__)
      MACOS
#elif defined(_WIN32)
      MSWIN
#else
      UNIX
#endif
      ;

  if ((ev_type >= EV_AKEY) && (ev_type <= EV_ZKEY)) {
    const char str[2] = {char('A' + (ev_type - EV_AKEY)), '\0'};
    icon_draw_rect_input_txt(&rect, color, str, 13.0f, 0.0f);
  }
  else if ((ev_type >= EV_F1KEY) && (event_type <= EV_F24KEY)) {
    char str[4];
    SNPRINTF(str, "F%d", 1 + (ev_type - EV_F1KEY));
    icon_draw_rect_input_txt(&rect, color, str, ev_type > EV_F9KEY ? 8.5f : 11.5f, 0.0f);
  }
  else if (ev_type == EV_LEFTSHIFTKEY) { /* Right Shift has already been converted to left. */
    const char str[] = LIB_STR_UTF8_UPWARDS_WHITE_ARROW;
    icon_draw_rect_input_text(&rect, color, str, 16.0f, 0.0f);
  }
  else if (ev_type == EV_LEFTCTRLKEY) { /* Right Shift has already been converted to left. */
    if (platform == MACOS) {
      const char str[] = LIB_STR_UTF8_UP_ARROWHEAD;
      icon_draw_rect_input_txt(&rect, color, str, 21.0f, -8.0f);
    }
    else {
      icon_draw_rect_input_txt(&rect, color, "Ctrl", 9.0f, 0.0f);
    }
  }
  else if (ev_type == EV_LEFTALTKEY) { /* Right Alt has already been converted to left. */
    if (platform == MACOS) {
      const char str[] = LIB_STR_UTF8_OPTION_KEY;
      icon_draw_rect_input_txt(&rect, color, str, 13.0f, 0.0f);
    }
    else {
      icon_draw_rect_input_txt(&rect, color, "Alt", 10.0f, 0.0f);
    }
  }
  else if (ev_type == EV_OSKEY) {
    if (platform == MACOS) {
      const char str[] = LIB_STR_UTF8_PLACE_OF_INTEREST_SIGN;
      icon_draw_rect_input_txt(&rect, color, str, 16.0f, 0.0f);
    }
    else if (platform == MSWIN) {
      const char str[] = LIB_STR_UTF8_BLACK_DIAMOND_MINUS_WHITE_X;
      icon_draw_rect_input_txt(&rect, color, str, 16.0f, 0.0f);
    }
    else {
      icon_draw_rect_input_txt(&rect, color, "OS", 10.0f, 0.0f);
    }
  }
  else if (ev_type == EV_DELKEY) {
    icon_draw_rect_input_txt(&rect, color, "Del", 9.0f, 0.0f);
  }
  else if (ev_type == EV_TABKEY) {
    const char str[] = LIB_STR_UTF8_HORIZONTAL_TAB_KEY;
    icon_draw_rect_input_txt(&rect, color, str, 18.0f, -1.5f);
  }
  else if (ev_type == EV_HOMEKEY) {
    icon_draw_rect_input_txt(&rect, color, "Home", 6.0f, 0.0f);
  }
  else if (ev_type == EV_ENDKEY) {
    icon_draw_rect_input_txt(&rect, color, "End", 8.0f, 0.0f);
  }
  else if (ev_type == EV_RETKEY) {
    const char str[] = LIB_STR_UTF8_RETURN_SYMBOL;
    icon_draw_rect_input_txt(&rect, color, str, 17.0f, -1.0f);
  }
  else if (ev_type == EV_ESCKEY) {
    if (platform == MACOS) {
      const char str[] = LIB_STR_UTF8_BROKEN_CIRCLE_WITH_NORTHWEST_ARROW;
      icon_draw_rect_input_txt(&rect, color, str, 21.0f, -1.0f);
    }
    else {
      icon_draw_rect_input_txt(&rect, color, "Esc", 8.5f, 0.0f);
    }
  }
  else if (ev_type == EV_PAGEUPKEY) {
    const char str[] = "P" LIB_STR_UTF8_UPWARDS_ARROW;
    icon_draw_rect_input_txt(&rect, color, str, 12.0f, 0.0f);
  }
  else if (ev_type == EV_PAGEDOWNKEY) {
    const char str[] = "P" LIB_STR_UTF8_DOWNWARDS_ARROW;
    icon_draw_rect_input_txt(&rect, color, str, 12.0f, 0.0f);
  }
  else if (ev_type == EV_LEFTARROWKEY) {
    const char str[] = LIB_STR_UTF8_LEFTWARDS_ARROW;
    icon_draw_rect_input_txt(&rect, color, str, 18.0f, -1.5f);
  }
  else if (ev_type == EV_UPARROWKEY) {
    const char str[] = LIB_STR_UTF8_UPWARDS_ARROW;
    icon_draw_rect_input_txt(&rect, color, str, 16.0f, 0.0f);
  }
  else if (ev_type == EV_RIGHTARROWKEY) {
    const char str[] = LIB_STR_UTF8_RIGHTWARDS_ARROW;
    icon_draw_rect_input_txt(&rect, color, str, 18.0f, -1.5f);
  }
  else if (ev_type == EV_DOWNARROWKEY) {
    const char str[] = LIB_STR_UTF8_DOWNWARDS_ARROW;
    icon_draw_rect_input_txt(&rect, color, str, 16.0f, 0.0f);
  }
  else if (ev_type == EV_SPACEKEY) {
    const char str[] = LIB_STR_UTF8_OPEN_BOX;
    icon_draw_rect_input_txt(&rect, color, str, 20.0f, 2.0f);
  }
}
