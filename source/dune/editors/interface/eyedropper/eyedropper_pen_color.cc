/* Eyedropper (RGB Color)
 * Defines:
 * - UI_OT_eyedropper_pen_color */

#include "mem_guardedalloc.h"

#include "lib_list.h"
#include "lib_string.h"

#include "lang.h"

#include "types_pen_legacy.h"
#include "types_material.h"
#include "typew_space.h"

#include "dune_cxt.h"
#include "dune_pen_legacy.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_material.h"
#include "dune_paint.hh"
#include "dune_report.h"

#include "UI.hh"

#include "imbuf_colormanagement.h"

#include "win_api.hh"
#include "win_types.hh"

#include "api_access.hh"
#include "api_define.hh"

#include "ed_pen_legacy.hh"
#include "ed_screen.hh"
#include "ed_undo.hh"

#include "graph.hh"
#include "graph_build.hh"

#include "eyedropper_intern.hh"
#include "ui_intern.hh"

enum ePenEyeMode {
  PEN_EYE_MATERIAL = 0,
  PEN_EYE_PALETTE = 1,
};

struct EyedropperPen {
  ColorManagedDisplay *display;
  /* color under cursor RGB */
  float color[3];
  /* Mode */
  ePenEyeMode mode;
};

/* Helper: Draw status message while the user is running the operator */
static void eyedropper_pen_status_indicators(Cxt *C)
{
  char msg_str[UI_MAX_DRAW_STR];
  STRNCPY(msg_str, TIP_("LMB: Stroke - Shift: Fill - Shift+Ctrl: Stroke + Fill"));

  ed_workspace_status_text(C, msg_str);
}

/* Initialize. */
static bool eyedropper_pen_init(Cxt *C, WinOp *op)
{
  EyedropperPen *eye = mem_cnew<EyedroppenPen>(__func__);

  op->customdata = eye;
  Scene *scene = cxt_data_scene(C);

  const char *display_device;
  display_device = scene->display_settings.display_device;
  eye->display = imbuf_colormanagement_display_get_named(display_device);

  eye->mode = (ePenEyeMode)api_enum_get(op->ptr, "mode");
  return true;
}

/* Exit and free memory. */
static void eyedropper_pen_exit(Cxt *C, WinOp *op)
{
  /* Clear status message area. */
  ed_workspace_status_text(C, nullptr);

  MEM_SAFE_FREE(op->customdata);
}

static void eyedropper_add_material(Cxt *C,
                                    const float col_conv[4],
                                    const bool only_stroke,
                                    const bool only_fill,
                                    const bool both)
{
  Main *main = cxt_data_main(C);
  Object *ob = cxt_data_active_object(C);
  Material *ma = nullptr;

  bool found = false;

  /* Look for a similar material in grease pencil slots. */
  short *totcol = dune_object_material_len_p(ob);
  for (short i = 0; i < *totcol; i++) {
    ma = dune_object_material_get(ob, i + 1);
    if (ma == nullptr) {
      continue;
    }

    MaterialPenStyle *pen_style = ma->pen_style;
    if (pen_style != nullptr) {
      /* Check stroke color. */
      bool found_stroke = compare_v3v3(pen_style->stroke_rgba, col_conv, 0.01f) &&
                          (pen_style->flag & PEN_MATERIAL_STROKE_SHOW);
      /* Check fill color. */
      bool found_fill = compare_v3v3(pen_style->fill_rgba, col_conv, 0.01f) &&
                        (pen_style->flag & PEN_MATERIAL_FILL_SHOW);

      if ((only_stroke) && (found_stroke) && ((pen_style->flag & PEN_MATERIAL_FILL_SHOW) == 0)) {
        found = true;
      }
      else if ((only_fill) && (found_fill) && ((pen_style->flag & PEN_MATERIAL_STROKE_SHOW) == 0)) {
        found = true;
      }
      else if ((both) && (found_stroke) && (found_fill)) {
        found = true;
      }

      /* Found existing material. */
      if (found) {
        ob->actcol = i + 1;
        win_main_add_notifier(NC_MATERIAL | ND_SHADING_LINKS, nullptr);
        win_main_add_notifier(NC_SPACE | ND_SPACE_VIEW3D, nullptr);
        return;
      }
    }
  }

  /* If material was not found add a new material with stroke and/or fill color
   * depending of the secondary key (LMB: Stroke, Shift: Fill, Shift+Ctrl: Stroke/Fill) */
  int idx;
  Material *ma_new = dune_pen_object_material_new(main, ob, "Material", &idx);
  win_main_add_notifier(NC_OBJECT | ND_OB_SHADING, &ob->id);
  win_main_add_notifier(NC_MATERIAL | ND_SHADING_LINKS, nullptr);
  graph_relations_tag_update(bmain);

  lib_assert(ma_new != nullptr);

  MaterialPenStyle *pen_style_new = ma_new->pen_style;
  lib_assert(pen_style_new != nullptr);

  /* Only create Stroke (default option). */
  if (only_stroke) {
    /* Stroke color. */
    pen_style_new->flag |= PEN_MATERIAL_STROKE_SHOW;
    pen_style_new->flag &= ~PEN_MATERIAL_FILL_SHOW;
    copy_v3_v3(pen_style_new->stroke_rgba, col_conv);
    zero_v4(pen_style_new->fill_rgba);
  }
  /* Fill Only. */
  else if (only_fill) {
    /* Fill color. */
    pen_style_new->flag &= ~PEN_MATERIAL_STROKE_SHOW;
    pen_style_new->flag |= PEN_MATERIAL_FILL_SHOW;
    zero_v4(pen_style_new->stroke_rgba);
    copy_v3_v3(pen_style_new->fill_rgba, col_conv);
  }
  /* Stroke and Fill. */
  else if (both) {
    pen_style_new->flag |= PEN_MATERIAL_STROKE_SHOW | PEN_MATERIAL_FILL_SHOW;
    copy_v3_v3(pen_style_new->stroke_rgba, col_conv);
    copy_v3_v3(pen_style_new->fill_rgba, col_conv);
  }
  /* Push undo for new created material. */
  ed_undo_push(C, "Add Pen Material");
}

/* Create a new palette color and palette if needed. */
static void eyedropper_add_palette_color(Cxt *C, const float col_conv[4])
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  ToolSettings *ts = scene->toolsettings;
  PenPaint *pen_paint = ts->pen_paint;
  PenVertexPaint *pen_vertexpaint = ts->pen_vertexpaint;
  Paint *paint = &pen_paint->paint;
  Paint *vertexpaint = &pen_vertexpaint->paint;

  /* Check for Palette in Draw and Vertex Paint Mode. */
  if (paint->palette == nullptr) {
    Palette *palette = dune_palette_add(main, "Pen");
    id_us_min(&palette->id);

    dune_paint_palette_set(paint, palette);

    if (vertexpaint->palette == nullptr) {
      dune_paint_palette_set(vertexpaint, palette);
    }
  }
  /* Check if the color exist already. */
  Palette *palette = paint->palette;
  LIST_FOREACH (PaletteColor *, palcolor, &palette->colors) {
    if (compare_v3v3(palcolor->rgb, col_conv, 0.01f)) {
      return;
    }
  }

  /* Create Colors. */
  PaletteColor *palcol = dune_palette_color_add(palette);
  if (palcol) {
    copy_v3_v3(palcol->rgb, col_conv);
  }
}

/* Set the material or the palette color. */
static void eyedropper_pen_color_set(Cxt *C, const WinEv *ev, EyedropperPen *eye)
{

  const bool only_stroke = (ev->mod & (KM_CTRL | KM_SHIFT)) == 0;
  const bool only_fill = ((ev->mod & KM_CTRL) == 0 && (ev->mod & KM_SHIFT));
  const bool both = ((ev->mod & KM_CTRL) && (ev->mod & KM_SHIFT));

  float col_conv[4];

  /* Convert from linear rgb space to display space because palette colors are in display
   *  space, and this conversion is needed to undo the conversion to linear performed by
   *  eyedropper_color_sample_fl. */
  if ((eye->display) && (eye->mode == PEN_EYE_PALETTE)) {
    copy_v3_v3(col_conv, eye->color);
    imbuf_colormanagement_scene_linear_to_display_v3(col_conv, eye->display);
  }
  else {
    copy_v3_v3(col_conv, eye->color);
  }

  /* Add material or Palette color. */
  if (eye->mode == PEN_EYE_MATERIAL) {
    eyedropper_add_material(C, col_conv, only_stroke, only_fill, both);
  }
  else {
    eyedropper_add_palette_color(C, col_conv);
  }
}

/* Sample the color below cursor. */
static void eyedropper_pen_color_sample(Cxt *C, EyedropperPen *eye, const int m_xy[2])
{
  eyedropper_color_sample_fl(C, m_xy, eye->color);
}

/* Cancel op */
static void eyedropper_pen_cancel(Cxt *C, WinOp *op)
{
  eyedropper_pen_exit(C, op);
}

/* Main modal status check. */
static int eyedropper_pen_modal(Cxt *C, WinOp *op, const WinEv *ev)
{
  EyedropperPen *eye = (EyedropperPen *)op->customdata;
  /* Handle modal keymap */
  switch (event->type) {
    case EV_MODAL_MAP: {
      switch (ev->val) {
        case EYE_MODAL_SAMPLE_BEGIN: {
          return OP_RUNNING_MODAL;
        }
        case EYE_MODAL_CANCEL: {
          eyedropper_pen_cancel(C, op);
          return OP_CANCELLED;
        }
        case EYE_MODAL_SAMPLE_CONFIRM: {
          eyedropper_pen_color_sample(C, eye, ev->xy);

          /* Create material. */
          eyedropper_pen_color_set(C, event, eye);
          win_main_add_notifier(NC_PEN | ND_DATA | NA_EDITED, nullptr);

          eyedropper_pen_exit(C, op);
          return OP_FINISHED;
        }
        default: {
          break;
        }
      }
      break;
    }
    case MOUSEMOVE:
    case INBETWEEN_MOUSEMOVE: {
      eyedropper_pen_color_sample(C, eye, ev->xy);
      break;
    }
    default: {
      break;
    }
  }

  return OP_RUNNING_MODAL;
}

/* Modal Op init */
static int eyedropper_pen_invoke(Cxt *C, WinOp *op, const WinEv * /*event*/)
{
  /* Init. */
  if (eyedropper_pen_init(C, op)) {
    /* Add modal temp handler. */
    win_event_add_modal_handler(C, op);
    /* Status message. */
    eyedropper_pen_status_indicators(C);

    return OP_RUNNING_MODAL;
  }
  return OP_PASS_THROUGH;
}

/* Repeat operator */
static int eyedropper_pen_ex(Cxt *C, WinOp *op)
{
  /* init */
  if (eyedropper_pen_init(C, op)) {

    /* cleanup */
    eyedropper_pen_exit(C, op);

    return OP_FINISHED;
  }
  return OP_PASS_THROUGH;
}

static bool eyedropper_pen_poll(Cxt *C)
{
  /* Only valid if the current active object is grease pencil. */
  Object *obact = cxt_data_active_object(C);
  if ((obact == nullptr) || (obact->type != OB_PEN_LEGACY)) {
    return false;
  }

  /* Test we have a win below. */
  return (cxt_win(C) != nullptr);
}

void UI_OT_eyedropper_pen_color(WinOpType *ot)
{
  static const EnumPropItem items_mode[] = {
      {PEN_EYE_MATERIAL, "MATERIAL", 0, "Material", ""},
      {PEN_EYE_PALETTE, "PALETTE", 0, "Palette", ""},
      {0, nullptr, 0, nullptr, nullptr},
  };

  /* ids */
  ot->name = "Pen Eyedropper";
  ot->idname = "UI_OT_eyedropper_pen_color";
  ot->description = "Sample a color from the Dune Win and create Pen material";

  /* api cbs */
  ot->invoke = eyedropper_pen_invoke;
  ot->modal = eyedropper_pen_modal;
  ot->cancel = eyedropper_pen_cancel;
  ot->ex = eyedropper_pen_ex;
  ot->poll = eyedropper_pen_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING;

  /* props */
  ot->prop = api_def_enum(ot->sapi, "mode", items_mode, PEN_EYE_MATERIAL, "Mode", "");
}
