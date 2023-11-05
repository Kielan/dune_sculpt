#include "types_screen.h"
#include "types_space.h"

#include "lib_math_color.h"
#include "lib_math_vector.h"

#include "dune_cxt.h"
#include "dune_screen.h"

#include "ui.h"

#include "win_api.h"
#include "win_types.h"

#include "ui_intern.h"

#include "ui_eyedropper_intern.h" /* own include */

/* Keymap */
/* Modal Keymap */
WinKeyMap *eyedropper_modal_keymap(WinKeyConfig *keyconf)
{
  static const EnumPropItem modal_items[] = {
      {EYE_MODAL_CANCEL, "CANCEL", 0, "Cancel", ""},
      {EYE_MODAL_SAMPLE_CONFIRM, "SAMPLE_CONFIRM", 0, "Confirm Sampling", ""},
      {EYE_MODAL_SAMPLE_BEGIN, "SAMPLE_BEGIN", 0, "Start Sampling", ""},
      {EYE_MODAL_SAMPLE_RESET, "SAMPLE_RESET", 0, "Reset Sampling", ""},
      {0, NULL, 0, NULL, NULL},
  };

  WinKeyMap *keymap = win_modalkeymap_find(keyconf, "Eyedropper Modal Map");

  /* this fn is called for each spacetype, only needs to add map once */
  if (keymap && keymap->modal_items) {
    return NULL;
  }

  keymap = win_modalkeymap_ensure(keyconf, "Eyedropper Modal Map", modal_items);

  /* assign to ops */
  win_modalkeymap_assign(keymap, "UI_OT_eyedropper_colorramp");
  win_modalkeymap_assign(keymap, "UI_OT_eyedropper_color");
  win_modalkeymap_assign(keymap, "UI_OT_eyedropper_id");
  win_modalkeymap_assign(keymap, "UI_OT_eyedropper_depth");
  win_modalkeymap_assign(keymap, "UI_OT_eyedropper_driver");
  win_modalkeymap_assign(keymap, "UI_OT_eyedropper_pen_color");

  return keymap;
}

WinKeyMap *eyedropper_colorband_modal_keymap(WinKeyConfig *keyconf)
{
  static const EnumPropItem modal_items_point[] = {
      {EYE_MODAL_POINT_CANCEL, "CANCEL", 0, "Cancel", ""},
      {EYE_MODAL_POINT_SAMPLE, "SAMPLE_SAMPLE", 0, "Sample a Point", ""},
      {EYE_MODAL_POINT_CONFIRM, "SAMPLE_CONFIRM", 0, "Confirm Sampling", ""},
      {EYE_MODAL_POINT_RESET, "SAMPLE_RESET", 0, "Reset Sampling", ""},
      {0, NULL, 0, NULL, NULL},
  };

  WinKeyMap *keymap = win_modalkeymap_find(keyconf, "Eyedropper ColorRamp PointSampling Map");
  if (keymap && keymap->modal_items) {
    return keymap;
  }

  keymap = win_modalkeymap_ensure(
      keyconf, "Eyedropper ColorRamp PointSampling Map", modal_items_point);

  /* assign to ops */
  win_modalkeymap_assign(keymap, "UI_OT_eyedropper_colorramp_point");

  return keymap;
}

/* Util Fns */
/* Generic Shared Fns */
static void eyedropper_draw_cursor_text_ex(const int xy[2], const char *name)
{
  const uiFontStyle *fstyle = UI_FSTYLE_WIDGET;

  /* Use the theme settings from tooltips. */
  const Theme *theme = ui_GetTheme();
  const uiWidgetColors *wcol = &theme->tui.wcol_tooltip;

  float col_fg[4], col_bg[4];
  rgba_uchar_to_float(col_fg, wcol->text);
  rgba_uchar_to_float(col_bg, wcol->inner);

  ui_fontstyle_draw_simple_backdrop(fstyle, xy[0], xy[1] + U.widget_unit, name, col_fg, col_bg);
}

void eyedropper_draw_cursor_text_win(const struct Win *win, const char *name)
{
  if (name[0] == '\0') {
    return;
  }

  eyedropper_draw_cursor_text_ex(win->eventstate->xy, name);
}

void eyedropper_draw_cursor_text_region(const int xy[2], const char *name)
{
  if (name[0] == '\0') {
    return;
  }

  eyedropper_draw_cursor_text_ex(xy, name);
}

Btn *eyedropper_get_prop_btn_under_mouse(Cxt *C, const WinEvent *event)
{
  Screen *screen = cxt_win_screen(C);
  ScrArea *area = dune_screen_find_area_xy(screen, SPACE_TYPE_ANY, event->xy);
  const ARegion *region = dune_area_find_region_xy(area, RGN_TYPE_ANY, event->xy);

  Btn *btn = btn_find_mouse_over(region, event);

  if (ELEM(NULL, btn, btn->apipoint.data, btn->apiprop)) {
    return NULL;
  }
  return but;
}

void datadropper_win_area_find(
    const Cxt *C, const int mval[2], int r_mval[2], Win **r_win, ScrArea **r_area)
{
  Screen *screen = cxt_win_screen(C);

  *r_win = cxt_win(C);
  *r_area = dune_screen_find_area_xy(screen, SPACE_TYPE_ANY, mval);
  if (*r_area == NULL) {
    *r_win = win_find_under_cursor(*r_win, mval, r_mval);
    if (*r_win) {
      screen = win_get_active_screen(*r_win);
      *r_area = dune_screen_find_area_xy(screen, SPACE_TYPE_ANY, r_mval);
    }
  }
  else if (mval != r_mval) {
    copy_v2_v2_int(r_mval, mval);
  }
}
