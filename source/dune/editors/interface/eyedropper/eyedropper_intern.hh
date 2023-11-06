#pragma once

/* `ui_eyedropper.cc` */
void eyedropper_draw_cursor_text_win(const Win *win, const char *name);
void eyedropper_draw_cursor_text_rgn(const int xy[2], const char *name);
/* Util to retrieve a btn representing a api prop that is currently under the cursor.
 *
 * This is to be used by any eyedroppers which fetch props (e.g. UI_OT_eyedropper_driver).
 * Especially during modal ops (e.g. as with the eyedroppers), cxt cannot be relied
 * upon to provide this info, as it is not updated until the op finishes.
 *
 * return A btn under the mouse which relates to some api Prop, or NULL */
Btn *eyedropper_get_prop_btn_under_mouse(Cxt *C, const WinEv *ev);
void datadropper_win_area_find(const Cxt *C,
                               const int event_xy[2],
                               int r_event_xy[2],
                               Win **r_win,
                               ScrArea **r_area);

/* ui_eyedropper_color.c (expose for color-band picker) */

/* get the color from the screen.
 * Special check for image or nodes where we MAY have HDR pixels which don't display.
 * Exposed by 'eyedropper_intern.hh' for use with color band picking. */
void eyedropper_color_sample_fl(Cxt *C, const int m_xy[2], float r_col[3]);

/* Used for most eye-dropper ops. */
enum {
  EYE_MODAL_CANCEL = 1,
  EYE_MODAL_SAMPLE_CONFIRM,
  EYE_MODAL_SAMPLE_BEGIN,
  EYE_MODAL_SAMPLE_RESET,
};

/* Color-band point sample. */
enum {
  EYE_MODAL_POINT_CANCEL = 1,
  EYE_MODAL_POINT_SAMPLE,
  EYE_MODAL_POINT_CONFIRM,
  EYE_MODAL_POINT_RESET,
  EYE_MODAL_POINT_REMOVE_LAST,
};
