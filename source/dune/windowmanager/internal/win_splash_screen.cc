/* This file contains the splash screen logic (the `WM_OT_splash` operator).
 *
 * - Loads the splash img.
 * - Displaying version info.
 * - Lists New Files (application templates).
 * - Lists Recent files.
 * - Links to web sites. */

#include <algorithm>
#include <cstring>

#include "types_id.h"
#include "types_scene_types.h"
#include "types_screen.h"
#include "types_userdef.h"
#include "types_win.h"

#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "dune_appdir.hh"
#include "dune_version.h"
#include "dune_cxt.hh"

#include "lang.hh"

#include "imbuf.hh"
#include "imbuf_types.hh"

#include "ed_datafiles.h"
#include "ed_screen.hh"

#include "ui.hh"
#include "ui_icons.hh"
#include "ui_resources.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "win.hh"

/* -------------------------------------------------------------------- */
/* Splash Screen */

static void wm_block_splash_close(Cxt *C, void *arg_block, void * /*arg*/)
{
  Win *win = cxt_win(C);
  ui_popup_block_close(C, win, static_cast<uiBlock *>(arg_block));
}

static void wm_block_splash_add_label(uiBlock *block, const char *label, int x, int y)
{
  if (!(label && label[0])) {
    return;
  }

  ui_block_emboss_set(block, UI_EMBOSS_NONE);

  uiBtn *btn = uiDefBtn(
      block, UI_BTYPE_LABEL, 0, label, 0, y, x, UI_UNIT_Y, nullptr, 0, 0, nullptr);
  ui_btn_drwflag_disable(btn, UI_BUT_TEXT_LEFT);
  ui_btn_drwflag_enable(btn, UI_BUT_TEXT_RIGHT);

  /* 1 = UI_SELECT, internal flag to draw in white. */
  UI_but_flag_enable(but, 1);
  UI_block_emboss_set(block, UI_EMBOSS);
}

#ifndef WITH_HEADLESS
static void wm_block_splash_image_roundcorners_add(ImBuf *ibuf)
{
  uchar *rct = ibuf->byte_buffer.data;
  if (!rct) {
    return;
  }

  bTheme *btheme = UI_GetTheme();
  const float roundness = btheme->tui.wcol_menu_back.roundness * UI_SCALE_FAC;
  const int size = roundness * 20;

  if (size < ibuf->x && size < ibuf->y) {
    /* Y-axis initial offset. */
    rct += 4 * (ibuf->y - size) * ibuf->x;

    for (int y = 0; y < size; y++) {
      for (int x = 0; x < size; x++, rct += 4) {
        const float pixel = 1.0 / size;
        const float u = pixel * x;
        const float v = pixel * y;
        const float distance = sqrt(u * u + v * v);

        /* Pointer offset to the alpha value of pixel. */
        /* NOTE: the left corner is flipped in the X-axis. */
        const int offset_l = 4 * (size - x - x - 1) + 3;
        const int offset_r = 4 * (ibuf->x - size) + 3;

        if (distance > 1.0) {
          rct[offset_l] = 0;
          rct[offset_r] = 0;
        }
        else {
          /* Create a single pixel wide transition for anti-aliasing.
           * Invert the distance and map its range [0, 1] to [0, pixel]. */
          const float fac = (1.0 - distance) * size;

          if (fac > 1.0) {
            continue;
          }

          const uchar alpha = unit_float_to_uchar_clamp(fac);
          rct[offset_l] = alpha;
          rct[offset_r] = alpha;
        }
      }

      /* X-axis offset to the next row. */
      rct += 4 * (ibuf->x - size);
    }
  }
}
#endif /* !WITH_HEADLESS */

static ImBuf *wm_block_splash_image(int width, int *r_height)
{
  ImBuf *ibuf = nullptr;
  int height = 0;
#ifndef WITH_HEADLESS
  if (U.app_template[0] != '\0') {
    char splash_filepath[FILE_MAX];
    char template_directory[FILE_MAX];
    if (BKE_appdir_app_template_id_search(
            U.app_template, template_directory, sizeof(template_directory)))
    {
      BLI_path_join(splash_filepath, sizeof(splash_filepath), template_directory, "splash.png");
      ibuf = IMB_loadiffname(splash_filepath, IB_rect, nullptr);
    }
  }

  if (ibuf == nullptr) {
    const uchar *splash_data = (const uchar *)datatoc_splash_png;
    size_t splash_data_size = datatoc_splash_png_size;
    ibuf = IMB_ibImageFromMemory(
        splash_data, splash_data_size, IB_rect, nullptr, "<splash screen>");
  }

  if (ibuf) {
    ibuf->planes = 32; /* The image might not have an alpha channel. */
    height = (width * ibuf->y) / ibuf->x;
    if (width != ibuf->x || height != ibuf->y) {
      IMB_scaleImBuf(ibuf, width, height);
    }

    wm_block_splash_image_roundcorners_add(ibuf);
    IMB_premultiply_alpha(ibuf);
  }

#else
  UNUSED_VARS(width);
#endif
  *r_height = height;
  return ibuf;
}

/* Close the splash when opening a file-sel. */
static void win_block_splash_close_on_filesel(Cxt *C, void *arg1, void * /*arg2*/)
{
  Win *win = cxt_win(C);
  if (!win) {
    return;
  }

  /* Check for the event as this will run before the new window/area has been created. */
  bool has_filesel = false;
  LIST_FOREACH (const WinEv *, ev, &win->ev_queue) {
    if (ev->type == EV_FILESEL) {
      has_filesel = true;
      break;
    }
  }

  if (has_fileselect) {
    win_block_splash_close(C, arg1, nullptr);
  }
}

static uiBlock *win_block_splash_create(Cxt *C, ARgn *rgn, void * /*arg*/)
{
  const uiStyle *style = ui_style_get_dpi();

  uiBlock *block = ui_block_begin(C, region, "splash", UI_EMBOSS);

  /* Note on UI_BLOCK_NO_WIN_CLIP, the win size is not always sync'd
   * with the OS when the splash shows, win clipping in this case gives
   * ugly results and clipping the splash isn't useful anyway, just disable it #32938. */
  ui_block_flag_enable(block, UI_BLOCK_LOOP | UI_BLOCK_KEEP_OPEN | UI_BLOCK_NO_WIN_CLIP);
  ui_block_theme_style_set(block, UI_BLOCK_THEME_STYLE_POPUP);

  const int text_points_max = std::max(style->widget.points, style->widgetlabel.points);
  int splash_width = text_points_max * 45 * UI_SCALE_FAC;
  CLAMP_MAX(splash_width, cxt_win_window(C)->sizex * 0.7f);
  int splash_height;

  /* Would be nice to support caching this, so it only has to be re-read (and likely resized) on
   * first draw or if the image changed. */
  ImBuf *ibuf = win_block_splash_img(splash_width, &splash_height);
  /* This should never happen, if it does - don't crash. */
  if (LIKELY(ibuf)) {
    uiBtn *btn = uiDefBtnImg(
        block, ibuf, 0, 0.5f * U.widget_unit, splash_width, splash_height, nullptr);

    ui_btn_fn_set(but, wm_block_splash_close, block, nullptr);

    wm_block_splash_add_label(block,
                              dune_version_string(),
                              splash_width - 8.0 * UI_SCALE_FAC,
                              splash_height - 13.0 * UI_SCALE_FAC);
  }

  const int layout_margin_x = UI_SCALE_FAC * 26;
  uiLayout *layout = ui_block_layout(block,
                                     UI_LAYOUT_VERTICAL,
                                     UI_LAYOUT_PNL,
                                     layout_margin_x,
                                     0,
                                     splash_width - (layout_margin_x * 2),
                                     UI_SCALE_FAC * 110,
                                     0,
                                     style);

  MenuType *mt;
  char userpref[FILE_MAX];
  const std::optional<std::string> cfgdir = dune_appdir_folder_id(BLENDER_USER_CONFIG, nullptr);

  if (cfgdir.has_val()) {
    lib_path_join(userpref, sizeof(userpref), cfgdir->c_str(), BLENDER_USERPREF_FILE);
  }
  else {
    userpref[0] = '\0';
  }

  /* Draw setup screen if no preferences have been saved yet. */
  if (!(userpref[0] && lib_exists(userpref))) {
    mt = win_menutype_find("win_mt_splash_quick_setup", true);

    /* The #UI_BLOCK_QUICK_SETUP flag prevents the button text from being left-aligned,
     * as it is for all menus due to the #UI_BLOCK_LOOP flag, see in #ui_def_but. */
    ui_block_flag_enable(block, UI_BLOCK_QUICK_SETUP);
  }
  else {
    mt = win_menutype_find("win_mt_splash", true);
  }

  ui_block_fn_set(block, wm_block_splash_close_on_fileselect, block, nullptr);

  if (mt) {
    ui_menutype_drw(C, mt, layout);
  }

  ui_block_bounds_set_centered(block, 0);

  return block;
}

static int win_splash_invoke(Cxt *C, WinOp * /*op*/, const WinEv * /*event*/)
{
  ui_popup_block_invoke(C, win_block_splash_create, nullptr, nullptr);

  return OPERATOR_FINISHED;
}

void win_ot_splash(WinOpType *ot)
{
  ot->name = "Splash Screen";
  ot->idname = "win_ot_splash";
  ot->description = "Open the splash screen with release info";

  ot->invoke = win_splash_invoke;
  ot->poll = win_op_winactive;
}


/* Splash Screen: About */
static uiBlock *win_block_about_create(Cxt *C, ARgn *rgn, void * /*arg*/)
{
  const uiStyle *style = ui_style_get_dpi();
  const int text_points_max = std::max(style->widget.points, style->widgetlabel.points);
  const int dialog_width = text_points_max * 42 * UI_SCALE_FAC;

  uiBlock *block = ui_block_begin(C, rgn, "about", UI_EMBOSS);

  ui_block_flag_enable(block, UI_BLOCK_KEEP_OPEN | UI_BLOCK_LOOP | UI_BLOCK_NO_WIN_CLIP);
  ui_block_theme_style_set(block, UI_BLOCK_THEME_STYLE_POPUP);

  uiLayout *layout = ui_block_layout(
      block, UI_LAYOUT_VERTICAL, UI_LAYOUT_PNL, 0, 0, dialog_width, 0, 0, style);

/* Dune logo. */
#ifndef WITH_HEADLESS

  const uchar *dune_logo_data = (const uchar *)datatoc_dune_logo_png;
  size_t dune_logo_data_size = datatoc_dune_logo_png_size;
  ImBuf *ibuf = imbuf_ibImgFromMem(
      blender_logo_data, blender_logo_data_size, IB_rect, nullptr, "blender_logo");

  if (ibuf) {
    int width = 0.5 * dialog_width;
    int height = (width * ibuf->y) / ibuf->x;

    imbuf_premultiply_alpha(ibuf);
    imbuf_scaleImBuf(ibuf, width, height);

    bTheme *btheme = UI_GetTheme();
    const uchar *color = btheme->tui.wcol_menu_back.text_sel;

    /* The top margin. */
    uiLayout *row = uiLayoutRow(layout, false);
    uiItemS_ex(row, 0.2f);

    /* The logo img. */
    row = uiLayoutRow(layout, false);
    uiLayoutSetAlignment(row, UI_LAYOUT_ALIGN_LEFT);
    uiDefButImage(block, ibuf, 0, U.widget_unit, width, height, color);

    /* Padding below the logo. */
    row = uiLayoutRow(layout, false);
    uiItemS_ex(row, 2.7f);
  }
#endif /* !WITH_HEADLESS */

  uiLayout *col = uiLayoutColumn(layout, true);

  uiItemL_ex(col, IFACE_("Blender"), ICON_NONE, true, false);

  MenuType *mt = WM_menutype_find("WM_MT_splash_about", true);
  if (mt) {
    ui_menutype_draw(C, mt, col);
  }

  ui_block_bounds_set_centered(block, 22 * UI_SCALE_FAC);

  return block;
}

static int win_splash_about_invoke(bContext *C, wmOperator * /*op*/, const wmEvent * /*event*/)
{
  ui_popup_block_invoke(C, wm_block_about_create, nullptr, nullptr);

  return OPERATOR_FINISHED;
}

void WM_OT_splash_about(wmOperatorType *ot)
{
  ot->name = "About Blender";
  ot->idname = "WM_OT_splash_about";
  ot->description = "Open a window with information about Blender";

  ot->invoke = wm_splash_about_invoke;
  ot->poll = WM_operator_winactive;
}
