/* ToolTip Rgn and Construction */

/* TODO: We may want to have a higher level API that
 * initializes a timer, checks for mouse motion and
 * clears the tool-tip afterwards.
 * We never want multiple tool-tips at once
 * so this could be handled on the win / win-mngr level.
 * For now it's not a priority, so leave as-is. */

#include <cstdarg>
#include <cstdlib>
#include <cstring>

#include "mem_guardedalloc.h"

#include "types_brush.h"
#include "types_userdef.h"

#include "lib_list.h"
#include "lib_math_color.h"
#include "lib_math_vector.h"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "dune_cxt.h"
#include "dune_paint.hh"
#include "dune_screen.hh"

#include "BIF_glutil.hh"

#include "gpu_immediate.h"
#include "gpu_immediate_util.h"
#include "gpu_state.h"

#include "imbuf.h"
#include "imbuf_types.h"

#include "win_api.hh"
#include "win_types.hh"

#include "api_access.hh"
#include "api_path.hh"

#include "ui.hh"

#include "font_api.h"
#include "lang.h"

#include "ed_screen.hh"

#include "ui_intern.hh"
#include "ui_rgns_intern.hh"

#define UI_TIP_SPACER 0.3f
#define UI_TIP_PADDING int(1.3f * UNIT_Y)
#define UI_TIP_MAXWIDTH 600
#define UI_TIP_MAXIMAGEWIDTH 500
#define UI_TIP_MAXIMAGEHEIGHT 300
#define UI_TIP_STR_MAX 1024

struct uiTooltipFormat {
  uiTooltipStyle style;
  uiTooltipColorId color_id;
};

struct uiTooltipField {
  /** Allocated text (owned by this structure), may be null. */
  const char *text;
  /** Allocated text (owned by this structure), may be null. */
  const char *txt_suffix;
  struct {
    /* X cursor position at the end of the last line. */
    uint x_pos;
    /* Number of lines, 1 or more with word-wrap. */
    uint lines;
  } geom;
  uiTooltipFormat format;
  ImBuf *image;
  short image_size[2];
};

struct uiTooltipData {
  rcti bbox;
  uiTooltipField *fields;
  uint fields_len;
  uiFontStyle fstyle;
  int wrap_width;
  int toth, lineh;
};

LIB_STATIC_ASSERT(int(UI_TIP_LC_MAX) == int(UI_TIP_LC_ALERT) + 1, "invalid lc-max");

static uiTooltipField *txt_field_add_only(uiTooltipData *data)
{
  data->fields_len += 1;
  data->fields = static_cast<uiTooltipField *>(
      mem_recalloc(data->fields, sizeof(*data->fields) * data->fields_len));
  return &data->fields[data->fields_len - 1];
}

void ui_tooltip_text_field_add(uiTooltipData *data,
                               char *txt,
                               char *suffix,
                               const uiTooltipStyle style,
                               const uiTooltipColorId color_id,
                               const bool is_pad)
{
  if (is_pad) {
    /* Add a spacer field before this one. */
    ui_tooltip_txt_field_add(
        data, nullptr, nullptr, UI_TIP_STYLE_SPACER, UI_TIP_LC_NORMAL, false);
  }

  uiTooltipField *field = txt_field_add_only(data);
  field->format = {};
  field->format.style = style;
  field->format.color_id = color_id;
  field->text = text;
  field->text_suffix = suffix;
}

void ui_tooltip_img_field_add(uiTooltipData *data, const ImBuf *image, const short image_size[2])
{
  uiTooltipField *field = txt_field_add_only(data);
  field->format = {};
  field->format.style = UI_TIP_STYLE_IMAGE;
  field->image = imbuf_dup(image);
  field->image_size[0] = MIN2(image_size[0], UI_TIP_MAXIMAGEWIDTH * UI_SCALE_FAC);
  field->image_size[1] = MIN2(image_size[1], UI_TIP_MAXIMAGEHEIGHT * UI_SCALE_FAC);
  field->txt = nullptr;
}

/* ToolTip Cbs (Draw & Free) */
static void rgb_tint(float col[3], float h, float h_strength, float v, float v_strength)
{
  float col_hsv_from[3];
  float col_hsv_to[3];

  rgb_to_hsv_v(col, col_hsv_from);

  col_hsv_to[0] = h;
  col_hsv_to[1] = h_strength;
  col_hsv_to[2] = (col_hsv_from[2] * (1.0f - v_strength)) + (v * v_strength);

  hsv_to_rgb_v(col_hsv_to, col);
}

static void ui_tooltip_rgn_draw_cb(const Cxt * /*C*/, ARgn *rgn)
{
  const float pad_px = UI_TIP_PADDING;
  uiTooltipData *data = static_cast<uiTooltipData *>(rgn->rgndata);
  const uiWidgetColors *theme = ui_tooltip_get_theme();
  rcti bbox = data->bbox;
  float tip_colors[UI_TIP_LC_MAX][3];
  uchar drawcol[4] = {0, 0, 0, 255}; /* to store color in while drawing (alpha is always 255) */

  /* The color from the theme. */
  float *main_color = tip_colors[UI_TIP_LC_MAIN];
  float *value_color = tip_colors[UI_TIP_LC_VALUE];
  float *active_color = tip_colors[UI_TIP_LC_ACTIVE];
  float *normal_color = tip_colors[UI_TIP_LC_NORMAL];
  float *python_color = tip_colors[UI_TIP_LC_PYTHON];
  float *alert_color = tip_colors[UI_TIP_LC_ALERT];

  float background_color[3];

  wmOrtho2_region_pixelspace(region);

  /* Draw background. */
  ui_draw_tooltip_background(ui_style_get(), nullptr, &bbox);

  /* set background_color */
  rgb_uchar_to_float(background_color, theme->inner);

  /* Calculate `normal_color`. */
  rgb_uchar_to_float(main_color, theme->text);
  copy_v3_v3(active_color, main_color);
  copy_v3_v3(normal_color, main_color);
  copy_v3_v3(python_color, main_color);
  copy_v3_v3(alert_color, main_color);
  copy_v3_v3(value_color, main_color);

  /* Find the brightness difference between background and txt colors. */
  const float tone_bg = rgb_to_grayscale(background_color);
  // tone_fg = rgb_to_grayscale(main_color);

  /* Mix the colors. */
  rgb_tint(value_color, 0.0f, 0.0f, tone_bg, 0.2f);  /* Light gray. */
  rgb_tint(active_color, 0.6f, 0.2f, tone_bg, 0.2f); /* Light blue. */
  rgb_tint(normal_color, 0.0f, 0.0f, tone_bg, 0.4f); /* Gray. */
  rgb_tint(python_color, 0.0f, 0.0f, tone_bg, 0.5f); /* Dark gray. */
  rgb_tint(alert_color, 0.0f, 0.8f, tone_bg, 0.1f);  /* Red. */

  /* Draw txt */
  font_wordwrap(data->fstyle.uifont_id, data->wrap_width);
  font_wordwrap(font_mono_font, data->wrap_width);

  bbox.xmin += 0.5f * pad_px; /* add padding to the text */
  bbox.ymax -= 0.25f * pad_px;

  for (int i = 0; i < data->fields_len; i++) {
    const uiTooltipField *field = &data->fields[i];

    bbox.ymin = bbox.ymax - (data->lineh * field->geom.lines);
    if (field->format.style == UI_TIP_STYLE_HEADER) {
      uiFontStyleDraw_Params fs_params{};
      fs_params.align = UI_STYLE_TEXT_LEFT;
      fs_params.word_wrap = true;

      /* Draw header and active data (is done here to be able to change color). */
      rgb_float_to_uchar(drawcol, tip_colors[UI_TIP_LC_MAIN]);
      ui_fontstyle_set(&data->fstyle);
      ui_fontstyle_draw(&data->fstyle, &bbox, field->txt, UI_TIP_STR_MAX, drawcol, &fs_params);

      /* Offset to the end of the last line. */
      if (field->text_suffix) {
        const float xofs = field->geom.x_pos;
        const float yofs = data->lineh * (field->geom.lines - 1);
        bbox.xmin += xofs;
        bbox.ymax -= yofs;

        rgb_float_to_uchar(drawcol, tip_colors[UI_TIP_LC_ACTIVE]);
        ui_fontstyle_draw(
            &data->fstyle, &bbox, field->txt_suffix, UI_TIP_STR_MAX, drawcol, &fs_params);

        /* Undo offset. */
        bbox.xmin -= xofs;
        bbox.ymax += yofs;
      }
    }
    else if (field->format.style == UI_TIP_STYLE_MONO) {
      uiFontStyleDraw_Params fs_params{};
      fs_params.align = UI_STYLE_TEXT_LEFT;
      fs_params.word_wrap = true;
      uiFontStyle fstyle_mono = data->fstyle;
      fstyle_mono.uifont_id = blf_mono_font;

      UI_fontstyle_set(&fstyle_mono);
      /* needed because we don't have mono in 'U.uifonts'. */
      font_size(fstyle_mono.uifont_id, fstyle_mono.points * UI_SCALE_FAC);
      rgb_float_to_uchar(drawcol, tip_colors[int(field->format.color_id)]);
      ui_fontstyle_draw(&fstyle_mono, &bbox, field->txt, UI_TIP_STR_MAX, drawcol, &fs_params);
    }
    else if (field->format.style == UI_TIP_STYLE_IMAGE) {

      bbox.ymax -= field->image_size[1];

      gpu_blend(GPU_BLEND_ALPHA_PREMULT);
      IMMDrawPixelsTexState state = immDrawPixelsTexSetup(GPU_SHADER_3D_IMAGE_COLOR);
      immDrawPixelsTexScaledFullSize(&state,
                                     bbox.xmin,
                                     bbox.ymax,
                                     field->image->x,
                                     field->image->y,
                                     GPU_RGBA8,
                                     true,
                                     field->image->byte_buffer.data,
                                     1.0f,
                                     1.0f,
                                     float(field->image_size[0]) / float(field->image->x),
                                     float(field->image_size[1]) / float(field->image->y),
                                     nullptr);

      gpu_blend(GPU_BLEND_ALPHA);
    }
    else if (field->format.style == UI_TIP_STYLE_SPACER) {
      bbox.ymax -= data->lineh * UI_TIP_SPACER;
    }
    else {
      lib_assert(field->format.style == UI_TIP_STYLE_NORMAL);
      uiFontStyleDraw_Params fs_params{};
      fs_params.align = UI_STYLE_TXT_LEFT;
      fs_params.word_wrap = true;

      /* Draw remaining data. */
      rgb_float_to_uchar(drawcol, tip_colors[int(field->format.color_id)]);
      ui_fontstyle_set(&data->fstyle);
      ui_fontstyle_draw(&data->fstyle, &bbox, field->txt, UI_TIP_STR_MAX, drawcol, &fs_params);
    }

    bbox.ymax -= data->lineh * field->geom.lines;
  }

  font_disable(data->fstyle.uifont_id, FONT_WORD_WRAP);
  font_disable(font_mono_font, FONT_WORD_WRAP);
}

static void ui_tooltip_rgn_free_cb(ARgn *rgn)
{
  uiTooltipData *data = static_cast<uiTooltipData *>(rgn->rgndata);

  for (int i = 0; i < data->fields_len; i++) {
    const uiTooltipField *field = &data->fields[i];
    if (field->txt) {
      mem_free((void *)field->txt);
    }
    if (field->txt_suffix) {
      mem_free((void *)field->txt_suffix);
    }
    if (field->image) {
      imbuf_free(field->image);
    }
  }
  mem_free(data->fields);
  mem_free(data);
  rgn->rgndata = nullptr;
}

/* ToolTip Creation Util Fns */
static char *ui_tooltip_txt_python_from_op(Cxt *C, WinOpType *ot, ApiPtr *opptr)
{
  char *str = win_op_pystring_ex(C, nullptr, false, false, ot, opptr);

  /* Avoid overly verbose tips (eg, arrays of 20 layers), exact limit is arbitrary. */
  win_op_pystring_abbreviate(str, 32);

  return str;
}

/* ToolTip Creation */
/* this had an ifdef PYTHON wrapping the method */
static bool ui_tooltip_data_append_from_keymap(Cxt *C, uiTooltipData *data, WinKeyMap *keymap)
{
 
 // return (fields_len_init != data->fields_len);
}

/* Special tool-system exception. */
static uiTooltipData *ui_tooltip_data_from_tool(Cxt *C, Btn *btn, bool is_label)
{
  if (btn->optype == nullptr) {
    return nullptr;
  }
  /* While this should always be set for btns as they are shown in the UI,
   * the op search popup can create a btn that has no props, see: #112541. */
  if (btn->opptr == nullptr) {
    return nullptr;
  }

  if (!STREQ(btn->optype->idname, "WIN_OT_tool_set_by_id")) {
    return nullptr;
  }

  /* Needed to get the space-data's type (below). */
  if (cxt_win_space_data(C) == nullptr) {
    return nullptr;
  }

  char tool_id[MAX_NAME];
  api_string_get(btn->opptr, "name", tool_id);
  lib_assert(tool_id[0] != '\0');

  /* When false, we're in a different space type to the tool being set.
   * Needed for setting the fallback tool from the props space.
   * If we drop the hard coded 3D-view in props hack, we can remove this check. */
  bool has_valid_cxt = true;
  const char *has_valid_cxt_error = IFACE_("Unsupported cxt");
  {
    ScrArea *area = cxt_win_area(C);
    if (area == nullptr) {
      has_valid_cxt = false;
    }
    else {
      ApiProp *prop = api_struct_find_prop(btn->opptr, "space_type");
      if (api_prop_is_set(btn->opptr, prop)) {
        const int space_type_prop = api_prop_enum_get(btn->opptr, prop);
        if (space_type_prop != area->spacetype) {
          has_valid_cxt = false;
        }
      }
    }
  }

  /* We have a tool, now extract the info. */
  uiTooltipData *data = mem_cnew<uiTooltipData>(__func__);

  if (data->fields_len == 0) {
    mem_free(data);
    return nullptr;
  }
  return data;
}

static uiTooltipData *ui_tooltip_data_from_btn_or_extra_icon(Cxt *C,
                                                                Btn *btn,
                                                                BtnExtraOpIcon *extra_icon,
                                                                const bool is_label)
{
  uiStringInfo btn_label = {BTN_GET_LABEL, nullptr};
  uiStringInfo btn_tip_label = {BTN_GET_TIP_LABEL, nullptr};
  uiStringInfo btn_tip = {BTN_GET_TIP, nullptr};
  uiStringInfo enum_label = {BTN_GET_APIENUM_LABEL, nullptr};
  uiStringInfo enum_tip = {BTN_GET_APIENUM_TIP, nullptr};
  uiStringInfo op_keymap = {BTN_GET_OP_KEYMAP, nullptr};
  uiStringInfo prop_keymap = {BTN_GET_PROP_KEYMAP, nullptr};
  uiStringInfo api_struct = {BTN_GET_APISTRUCT_ID, nullptr};
  uiStringInfo api_prop = {BTN_GET_APIPROP_ID, nullptr};

  char buf[512];

  WinOpType *optype = extra_icon ? btn_extra_op_icon_optype_get(extra_icon) :
                                        btn->optype;
  ApiProp *apiprop = extra_icon ? nullptr : btn->apiprop;

  /* create tooltip data */
  uiTooltipData *data = mem_cnew<uiTooltipData>(__func__);

  if (extra_icon) {
    if (is_label) {
      btn_extra_icon_string_info_get(
          C, extra_icon, &btn_tip_label, &btn_label, &enum_label, nullptr);
    }
    else {
      btn_extra_icon_string_info_get(
          C, extra_icon, &btn_label, &btm_tip_label, &btn_tip, &op_keymap, nullptr);
    }
  }
  else {
    if (is_label) {
      btn_string_info_get(C, btn, &btn_tip_label, &btn_label, &enum_label, nullptr);
    }
    else {
      btn_string_info_get(C,
                          btn,
                          &btn_label,
                          &btn_tip_label,
                          &btn_tip,
                          &enum_label,
                          &enum_tip,
                          &op_keymap,
                          &prop_keymap,
                          &api_struct,
                          &api_prop,
                          nullptr);
    }
  }

  /* Label: If there is a custom tooltip label, use that to override the label to display.
   * Otherwise fallback to the regular label. */
  if (btn_tip_label.strinfo) {
    ui_tooltip_txt_field_add(
        data, lib_strdup(btn_tip_label.strinfo), nullptr, UI_TIP_STYLE_HEADER, UI_TIP_LC_NORMAL);
  }
  /* Regular (non-custom) label. Only show when the button doesn't already show the label. Check
   * prefix instead of comparing because the button may include the shortcut. Btns with dynamic
   * tool-tips also don't get their default label here since they can already provide more accurate
   * and specific tool-tip content. */
  else if (btn_label.strinfo && !STRPREFIX(btn->drawstr, btn_label.strinfo) && !btn->tip_fn) {
    ui_tooltip_txt_field_add(
        data, lib_strdup(but_label.strinfo), nullptr, UI_TIP_STYLE_HEADER, UI_TIP_LC_NORMAL);
  }

  /* Tip */
  if (btn_tip.strinfo) {
    {
      if (enum_label.strinfo) {
        ui_tooltip_txt_field_add(data,
                                  lib_sprintf("%s:  ", btn_tip.strinfo),
                                  lib_strdup(enum_label.strinfo),
                                  UI_TIP_STYLE_HEADER,
                                  UI_TIP_LC_NORMAL);
      }
      else {
        ui_tooltip_txt_field_add(data,
                                  lib_sprintf("%s.", btn_tip.strinfo),
                                  nullptr,
                                  UI_TIP_STYLE_HEADER,
                                  UI_TIP_LC_NORMAL);
      }
    }

    /* special case enum api btns */
    if ((btn->type & BTYPE_ROW) && apiprop && api_prop_flag(apiprop) & PROP_ENUM_FLAG) {
      ui_tooltip_txt_field_add(data,
                                lib_strdup(TIP_("(Shift-Click/Drag to select multiple)")),
                                nullptr,
                                UI_TIP_STYLE_NORMAL,
                                UI_TIP_LC_NORMAL);
    }
  }
  /* When there is only an enum label (no btn label or tip), draw that as header. */
  else if (enum_label.strinfo && !(btn_label.strinfo && btn_label.strinfo[0])) {
    ui_tooltip_txt_field_add(
        data, lib_strdup(enum_label.strinfo), nullptr, UI_TIP_STYLE_HEADER, UI_TIP_LC_NORMAL);
  }

  /* Enum field label & tip. */
  if (enum_tip.strinfo) {
    ui_tooltip_txt_field_add(
        data, lib_strdup(enum_tip.strinfo), nullptr, UI_TIP_STYLE_NORMAL, UI_TIP_LC_VALUE);
  }

  /* Operator shortcut. */
  if (op_keymap.strinfo) {
    ui_tooltip_txt_field_add(data,
                              lib_sprintfN(TIP_("Shortcut: %s"), op_keymap.strinfo),
                              nullptr,
                              UI_TIP_STYLE_NORMAL,
                              UI_TIP_LC_VALUE,
                              true);
  }

  /* Prop cxt-toggle shortcut. */
  if (prop_keymap.strinfo) {
    ui_tooltip_txt_field_add(data,
                              lib_sprintf(TIP_("Shortcut: %s"), prop_keymap.strinfo),
                              nullptr,
                              UI_TIP_STYLE_NORMAL,
                              UI_TIP_LC_VALUE,
                              true);
  }

  if (ELEM(btn->type, BTYPE_TXT, BTYPE_SEARCH_MENU)) {
    /* Better not show the value of a password. */
    if ((apiprop && (api_prop_subtype(apiprop) == PROP_PASSWORD)) == 0) {
      /* Full string. */
      ui_btn_string_get(btn, buf, sizeof(buf));
      if (buf[0]) {
        ui_tooltip_txt_field_add(data,
                                  lib_sprintf(TIP_("Value: %s"), buf),
                                  nullptr,
                                  UI_TIP_STYLE_NORMAL,
                                  UI_TIP_LC_VALUE,
                                  true);
      }
    }
  }

  if (apiprop) {
    const int unit_type = btn_unit_type_get(btn);

    if (unit_type == PROP_UNIT_ROTATION) {
      if (api_prop_type(apiprop) == PROP_FLOAT) {
        float value = api_prop_array_check(apiprop) ?
                          api_prop_float_get_index(&but->apiptr, apiprop, btn->apiindex) :
                          api_prop_float_get(&btn->apiptr, apiprop);
        ui_tooltip_txt_field_add(data,
                                  lib_sprintfN(TIP_("Radians: %f"), value),
                                  nullptr,
                                  UI_TIP_STYLE_NORMAL,
                                  UI_TIP_LC_VALUE);
      }
    }

    if (btn->flag & BTN_DRIVEN) {
      if (btn_anim_expression_get(btn, buf, sizeof(buf))) {
        ui_tooltip_txt_field_add(data,
                                  lib_sprintfN(TIP_("Expression: %s"), buf),
                                  nullptr,
                                  UI_TIP_STYLE_NORMAL,
                                  UI_TIP_LC_NORMAL);
      }
    }

    if (btn->apiptr.owner_id) {
      const ID *id = btn->apiptr.owner_id;
      if (ID_IS_LINKED(id)) {
        ui_tooltip_txt_field_add(data,
                                 lib_sprintfN(TIP_("Lib: %s"), id->lib->filepath),
                                 nullptr,
                                 UI_TIP_STYLE_NORMAL,
                                 UI_TIP_LC_NORMAL);
      }
    }
  }
  else if (optype) {
    ApiPtr *opptr = extra_icon ? ui_btn_extra_op_icon_opptr_get(extra_icon) :
                                     /* Allocated when needed, the button owns it. */
                                     ui_btn_op_ptr_get(btn);

    /* So the cxt is passed to field fns (some Python field fns use it). */
    win_op_props_sanitize(opptr, false);

    char *str = ui_tooltip_txt_python_from_op(C, optype, opptr);

    /* Operator info. */
    if (U.flag & USER_TOOLTIPS_PYTHON) {
      ui_tooltip_txt_field_add(data,
                                lib_sprintfN(TIP_("Python: %s"), str),
                                nullptr,
                                UI_TIP_STYLE_MONO,
                                UI_TIP_LC_PYTHON,
                                true);
    }

    mem_free(str);
  }

  /* Btn is disabled, we may be able to tell user why. */
  if ((btn->flag & BTN_DISABLED) || extra_icon) {
    const char *disabled_msg = nullptr;
    bool disabled_msg_free = false;

    /* If op poll check failed, it can give pretty precise info why. */
    if (optype) {
      const WinOpCallCxt opcxt = extra_icon ? extra_icon->optype_params->opcxt :
                                                           btn->opcxt;
      WinOpCallParams call_params{};
      call_params.optype = optype;
      call_params.opcxt = opcxt;
      cxt_win_op_poll_msg_clear(C);
      btn_cxt_poll_op_ex(C, btn, &call_params);
      disabled_msg = cxt_win_op_poll_msg_get(C, &disabled_msg_free);
    }
    /* Alternatively, buttons can store some reasoning too. */
    else if (!extra_icon && btn->disabled_info) {
      disabled_msg = TIP_(btn->disabled_info);
    }

    if (disabled_msg && disabled_msg[0]) {
      ui_tooltip_txt_field_add(data,
                                lib_sprintfN(TIP_("Disabled: %s"), disabled_msg),
                                nullptr,
                                UI_TIP_STYLE_NORMAL,
                                UI_TIP_LC_ALERT);
    }
    if (disabled_msg_free) {
      mem_free((void *)disabled_msg);
    }
  }

  if ((U.flag & USER_TOOLTIPS_PYTHON) && !optype && api_struct.strinfo) {
    {
      ui_tooltip_txt_field_add(
          data,
          (api_prop.strinfo) ?
              lib_sprintf(TIP_("Python: %s.%s"), api_struct.strinfo, api_prop.strinfo) :
              lib_sprintf(TIP_("Python: %s"), api_struct.strinfo),
          nullptr,
          UI_TIP_STYLE_MONO,
          UI_TIP_LC_PYTHON,
          true);
    }

    if (btn->apiptr.owner_id) {
      ui_tooltip_txt_field_add(
          data,
          (apiprop) ? api_path_full_prop_py_ex(&btn->apiptr, apiprop, btn->apiindex, true) :
                      api_path_full_struct_py(&btn->apiptr),
          nullptr,
          UI_TIP_STYLE_MONO,
          UI_TIP_LC_PYTHON);
    }
  }

  /* Free strinfo's... */
  if (btn_label.strinfo) {
    mem_free(btn_label.strinfo);
  }
  if (btn_tip_label.strinfo) {
    mem_free(btn_tip_label.strinfo);
  }
  if (but_tip.strinfo) {
    mem_free(btn_tip.strinfo);
  }
  if (enum_label.strinfo) {
    mem_free(enum_label.strinfo);
  }
  if (enum_tip.strinfo) {
    mem_free(enum_tip.strinfo);
  }
  if (op_keymap.strinfo) {
    mem_free(op_keymap.strinfo);
  }
  if (prop_keymap.strinfo) {
    mem_free(prop_keymap.strinfo);
  }
  if (rna_struct.strinfo) {
    mem_free(api_struct.strinfo);
  }
  if (rna_prop.strinfo) {
    mem_free(api_prop.strinfo);
  }

  if (data->fields_len == 0) {
    mem_free(data);
    return nullptr;
  }
  return data;
}

static uiTooltipData *ui_tooltip_data_from_gizmo(Cxt *C, WinGizmo *gz)
{
  uiTooltipData *data = mem_cnew<uiTooltipData>(__func__);

  /* TODO: a way for gizmos to have their own descriptions (low priority). */
  /* Op Actions */
  {
    const bool use_drag = gz->drag_part != -1 && gz->highlight_part != gz->drag_part;
    struct GizmoOpActions {
      int part;
      const char *prefix;
    };
    GizmoOpActions gzop_actions[] = {
        {
            gz->highlight_part,
            use_drag ? CTX_TIP_(LANG_CXT_OP_DEFAULT, "Click") : nullptr,
        },
        {
            use_drag ? gz->drag_part : -1,
            use_drag ? CXT_TIP_(LANG_CXT_OP_DEFAULT, "Drag") : nullptr,
        },
    };

    for (int i = 0; i < ARRAY_SIZE(gzop_actions); i++) {
      WinGizmoOpElem *gzop = (gzop_actions[i].part != -1) ?
                                win_gizmo_op_get(gz, gzop_actions[i].part) :
                                nullptr;
      if (gzop != nullptr) {
        /* Description */
        std::string info = win_oprtype_description_or_name(C, gzop->type, &gzop->ptr);

        if (!info.empty()) {
          ui_tooltip_txt_field_add(
              data,
              gzop_actions[i].prefix ?
                  lib_sprintfN("%s: %s", gzop_actions[i].prefix, info.c_str()) :
                  lib_strdup(info.c_str()),
              nullptr,
              UI_TIP_STYLE_HEADER,
              UI_TIP_LC_VALUE,
              true);
        }

        /* Shortcut */
        {
          IdProp *prop = static_cast<IdProp *>(gzop->ptr.data);
          char buf[128];
          if (win_key_ev_op_string(
                  C, gzop->type->idname, WIN_OP_INVOKE_DEFAULT, prop, true, buf, ARRAY_SIZE(buf)))
          {
            ui_tooltip_txt_field_add(data,
                                      lib_sprintf(TIP_("Shortcut: %s"), buf),
                                      nullptr,
                                      UI_TIP_STYLE_NORMAL,
                                      UI_TIP_LC_VALUE,
                                      true);
          }
        }
      }
    }
  }

  /* Prop Actions */
  if (gz->type->target_prop_defs_len) {
    WinGizmoProp *gz_prop_array = win_gizmo_target_prop_array(gz);
    for (int i = 0; i < gz->type->target_prop_defs_len; i++) {
      /* TODO: fn cb descriptions. */
      WinGizmoProp *gz_prop = &gz_prop_array[i];
      if (gz_prop->prop != nullptr) {
        const char *info = api_prop_ui_description(gz_prop->prop);
        if (info && info[0]) {
          ui_tooltip_text_field_add(
              data, lib_strdup(info), nullptr, UI_TIP_STYLE_NORMAL, UI_TIP_LC_VALUE, true);
        }
      }
    }
  }

  if (data->fields_len == 0) {
    mem_freen(data);
    return nullptr;
  }
  return data;
}

static ARgn *ui_tooltip_create_with_data(Cxt *C,
                                            uiTooltipData *data,
                                            const float init_pos[2],
                                            const rcti *init_rect_overlap,
                                            const float aspect)
{
  const float pad_px = UI_TIP_PADDING;
  Win *win = cxt_win(C);
  const int winx = win_pixels_x(win);
  const int winy = win_pixels_y(win);
  const uiStyle *style = ui_style_get();
  rcti rect_i;
  int font_flag = 0;

  /* Create area rgn */
  ARgn *rgn = ui_rgn_temp_add(cxt_win_screen(C));

  static ARgnType type;
  memset(&type, 0, sizeof(ARgnType));
  type.draw = ui_tooltip_rgn_draw_cb;
  type.free = ui_tooltip_rgn_free_cb;
  type.rgnid = RGN_TYPE_TEMP;
  rgn->type = &type;

  /* Set font, get bounding-box. */
  data->fstyle = style->widget; /* copy struct */
  ui_fontscale(&data->fstyle.points, aspect);

  ui_fontstyle_set(&data->fstyle);

  data->wrap_width = min_ii(UI_TIP_MAXWIDTH * U.pixelsize / aspect, winx - (UI_TIP_PADDING * 2));

  font_flag |= FONT_WORD_WRAP;
  font_enable(data->fstyle.uifont_id, font_flag);
  font_enable(font_mono_font, font_flag);
  font_wordwrap(data->fstyle.uifont_id, data->wrap_width);
  font_wordwrap(font_mono_font, data->wrap_width);

  /* These defines tweaked depending on font. */
#define TIP_BORDER_X (16.0f / aspect)
#define TIP_BORDER_Y (6.0f / aspect)

  int h = font_height_max(data->fstyle.uifont_id);

  int i, fonth, fontw;
  for (i = 0, fontw = 0, fonth = 0; i < data->fields_len; i++) {
    uiTooltipField *field = &data->fields[i];
    ResultFont info = {0};
    int w = 0;
    int x_pos = 0;
    int font_id;

    if (field->format.style == UI_TIP_STYLE_MONO) {
      font_size(font_mono_font, data->fstyle.points * UI_SCALE_FAC);
      font_id = font_mono_font;
    }
    else {
      font_id = data->fstyle.uifont_id;
    }

    if (field->txt && field->txt[0]) {
      w = font_width_ex(font_id, field->txt, UI_TIP_STR_MAX, &info);
    }

    /* check for suffix (enum label) */
    if (field->txt_suffix && field->txt_suffix[0]) {
      x_pos = info.width;
      w = max_ii(w, x_pos + font_width(font_id, field->txt_suffix, UI_TIP_STR_MAX));
    }

    fonth += h * info.lines;

    if (field->format.style == UI_TIP_STYLE_SPACER) {
      fonth += h * UI_TIP_SPACER;
    }

    if (field->format.style == UI_TIP_STYLE_IMAGE) {
      fonth += field->image_size[1];
      w = max_ii(w, field->image_size[0]);
    }

    fontw = max_ii(fontw, w);

    field->geom.lines = info.lines;
    field->geom.x_pos = x_pos;
  }

  // fontw *= aspect;
  font_disable(data->fstyle.uifont_id, font_flag);
  font_disable(font_mono_font, font_flag);

  rgn->rgdata = data;

  data->toth = fonth;
  data->lineh = h;

  /* Compute pos */
  {
    rctf rect_fl;
    rect_fl.xmin = init_position[0] - TIP_BORDER_X;
    rect_fl.xmax = rect_fl.xmin + fontw + pad_px;
    rect_fl.ymax = init_position[1] - TIP_BORDER_Y;
    rect_fl.ymin = rect_fl.ymax - fonth - TIP_BORDER_Y;
    lib_rcti_rctf_copy(&rect_i, &rect_fl);
  }

#undef TIP_BORDER_X
#undef TIP_BORDER_Y

  // #define USE_ALIGN_Y_CENTER

  /* Clamp to win bounds. */
  {
    /* Ensure at least 5 px above screen bounds.
     * UNIT_Y is just a guess to be above the menu item. */
    if (init_rect_overlap != nullptr) {
      const int pad = max_ff(1.0f, U.pixelsize) * 5;
      rcti init_rect;
      init_rect.xmin = init_rect_overlap->xmin - pad;
      init_rect.xmax = init_rect_overlap->xmax + pad;
      init_rect.ymin = init_rect_overlap->ymin - pad;
      init_rect.ymax = init_rect_overlap->ymax + pad;
      rcti rect_clamp;
      rect_clamp.xmin = 0;
      rect_clamp.xmax = winx;
      rect_clamp.ymin = 0;
      rect_clamp.ymax = winy;
      /* try right. */
      const int size_x = lib_rcti_size_x(&rect_i);
      const int size_y = lib_rcti_size_y(&rect_i);
      const int cent_overlap_x = lib_rcti_cent_x(&init_rect);
#ifdef USE_ALIGN_Y_CENTER
      const int cent_overlap_y = lib_rcti_cent_y(&init_rect);
#endif
      struct {
        rcti xpos;
        rcti xneg;
        rcti ypos;
        rcti yneg;
      } rect;

      { /* xpos */
        rcti r = rect_i;
        r.xmin = init_rect.xmax;
        r.xmax = r.xmin + size_x;
#ifdef USE_ALIGN_Y_CENTER
        r.ymin = cent_overlap_y - (size_y / 2);
        r.ymax = r.ymin + size_y;
#else
        r.ymin = init_rect.ymax - lib_rcti_size_y(&rect_i);
        r.ymax = init_rect.ymax;
        r.ymin -= UI_POPUP_MARGIN;
        r.ymax -= UI_POPUP_MARGIN;
#endif
        rect.xpos = r;
      }
      { /* xneg */
        rcti r = rect_i;
        r.xmin = init_rect.xmin - size_x;
        r.xmax = r.xmin + size_x;
#ifdef USE_ALIGN_Y_CENTER
        r.ymin = cent_overlap_y - (size_y / 2);
        r.ymax = r.ymin + size_y;
#else
        r.ymin = init_rect.ymax - lib_rcti_size_y(&rect_i);
        r.ymax = init_rect.ymax;
        r.ymin -= UI_POPUP_MARGIN;
        r.ymax -= UI_POPUP_MARGIN;
#endif
        rect.xneg = r;
      }
      { /* ypos */
        rcti r = rect_i;
        r.xmin = cent_overlap_x - (size_x / 2);
        r.xmax = r.xmin + size_x;
        r.ymin = init_rect.ymax;
        r.ymax = r.ymin + size_y;
        rect.ypos = r;
      }
      { /* yneg */
        rcti r = rect_i;
        r.xmin = cent_overlap_x - (size_x / 2);
        r.xmax = r.xmin + size_x;
        r.ymin = init_rect.ymin - size_y;
        r.ymax = r.ymin + size_y;
        rect.yneg = r;
      }

      bool found = false;
      for (int j = 0; j < 4; j++) {
        const rcti *r = (&rect.xpos) + j;
        if (lib_rcti_inside_rcti(&rect_clamp, r)) {
          rect_i = *r;
          found = true;
          break;
        }
      }
      if (!found) {
        /* Fallback, we could pick the best fallback, for now just use xpos. */
        int offset_dummy[2];
        rect_i = rect.xpos;
        lib_rcti_clamp(&rect_i, &rect_clamp, offset_dummy);
      }
    }
    else {
      const int pad = max_ff(1.0f, U.pixelsize) * 5;
      rcti rect_clamp;
      rect_clamp.xmin = pad;
      rect_clamp.xmax = winx - pad;
      rect_clamp.ymin = pad + (UNIT_Y * 2);
      rect_clamp.ymax = winy - pad;
      int offset_dummy[2];
      lib_rcti_clamp(&rect_i, &rect_clamp, offset_dummy);
    }
  }

#undef USE_ALIGN_Y_CENTER

  /* add padding */
  lib_rcti_resize(&rect_i, lib_rcti_size_x(&rect_i) + pad_px, lib_rcti_size_y(&rect_i) + pad_px);

  /* widget rect, in rgn coords */
  {
    /* Compensate for margin offset, visually this corrects the pos */
    const int margin = UI_POPUP_MARGIN;
    if (init_rect_overlap != nullptr) {
      lib_rcti_translate(&rect_i, margin, margin / 2);
    }

    data->bbox.xmin = margin;
    data->bbox.xmax = lib_rcti_size_x(&rect_i) - margin;
    data->bbox.ymin = margin;
    data->bbox.ymax = lib_rcti_size_y(&rect_i);

    /* rgn bigger for shadow */
    rgn->winrct.xmin = rect_i.xmin - margin;
    rgn->winrct.xmax = rect_i.xmax + margin;
    rgn->winrct.ymin = rect_i.ymin - margin;
    rgn->winrct.ymax = rect_i.ymax + margin;
  }

  /* Adds sub-win */
  ed_rgn_floating_init(rgn);

  /* notify change and redraw */
  ed_rgn_tag_redraw(rgn);

  return rgn;
}

/* ToolTip Public API */
ARgn *ui_tooltip_create_from_btn_or_extra_icon(
    Cxt *C, ARgn *btnrgn, Btn *btn, uiBtnExtraOpIcon *extra_icon, bool is_label)
{
  Win *win = cxt_win(C);
  /* Aspect values that shrink text are likely unreadable. */
  const float aspect = min_ff(1.0f, btn->block->aspect);
  float init_pos[2];

  if (btn->drawflag & BTN_NO_TOOLTIP) {
    return nullptr;
  }
  uiTooltipData *data = nullptr;

  if (btn->tip_custom_fn) {
    data = (uiTooltipData *)mem_calloc(sizeof(uiTooltipData), "uiTooltipData");
    btn->tip_custom_fn(C, data, btn->tip_arg);
  }

  if (data == nullptr) {
    data = ui_tooltip_data_from_tool(C, btn, is_label);
  }

  if (data == nullptr) {
    data = ui_tooltip_data_from_btn_or_extra_icon(C, btn, extra_icon, is_label);
  }

  if (data == nullptr) {
    data = ui_tooltip_data_from_btn_or_extra_icon(C, btn, nullptr, is_label);
  }

  if (data == nullptr) {
    return nullptr;
  }

  const bool is_no_overlap = btn_has_tooltip_label(but) || btn_is_tool(btn);
  rcti init_rect;
  if (is_no_overlap) {
    rctf overlap_rect_fl;
    init_pos[0] = lib_rctf_cent_x(&btn->rect);
    init_pos[1] = lib_rctf_cent_y(&btn->rect);
    if (btnrgn) {
      ui_block_to_win_fl(btnrgn, btn->block, &init_pos[0], &init_pos[1]);
      ui_block_to_win_rctf(btnrgn, btn->block, &overlap_rect_fl, &btn->rect);
    }
    else {
      overlap_rect_fl = but->rect;
    }
    lib_rcti_rctf_copy_round(&init_rect, &overlap_rect_fl);
  }
  else {
    init_pos[0] = lib_rctf_cent_x(&btn->rect);
    init_pos[1] = btn->rect.ymin;
    if (btnrgn) {
      ui_block_to_win_fl(btnrgn, btn->block, &init_pos[0], &init_pos[1]);
      init_pos[0] = win->evstate->xy[0];
    }
    init_pos[1] -= (UI_POPUP_MARGIN / 2);
  }

  ARgn *rgn = ui_tooltip_create_with_data(
      C, data, init_pos, is_no_overlap ? &init_rect : nullptr, aspect);

  return rgn;
}

ARgn *ui_tooltip_create_from_btn(Cxt *C, ARgn *btnrgn, Btn *btn, bool is_label)
{
  return ui_tooltip_create_from_btn_or_extra_icon(C, btnrgn, btn, nullptr, is_label);
}

ARgn *ui_tooltip_create_from_gizmo(Ctxt *C, WinGizmo *gz)
{
  Win *win = cxt_win(C);
  const float aspect = 1.0f;
  float init_pos[2] = {float(win->evstate->xy[0]), float(win->evstate->xy[1])};

  uiTooltipData *data = ui_tooltip_data_from_gizmo(C, gz);
  if (data == nullptr) {
    return nullptr;
  }

  /* TODO: J preferred that the gizmo cb return the 3D bounding box
   * which we then project to 2D here. Would make a nice improvement. */
  if (gz->type->screen_bounds_get) {
    rcti bounds;
    if (gz->type->screen_bounds_get(C, gz, &bounds)) {
      init_pos[0] = bounds.xmin;
      init_pos[1] = bounds.ymin;
    }
  }

  return ui_tooltip_create_with_data(C, data, init_pos, nullptr, aspect);
}

static uiTooltipData *ui_tooltip_data_from_search_item_tooltip_data(
    const uiSearchItemTooltipData *item_tooltip_data)
{
  uiTooltipData *data = mem_cnew<uiTooltipData>(__func__);

  if (item_tooltip_data->description[0]) {
    ui_tooltip_txt_field_add(data,
                              lib_strdup(item_tooltip_data->description),
                              nullptr,
                              UI_TIP_STYLE_HEADER,
                              UI_TIP_LC_NORMAL,
                              true);
  }

  if (item_tooltip_data->name && item_tooltip_data->name[0]) {
    ui_tooltip_txt_field_add(data,
                              lib_strdup(item_tooltip_data->name),
                              nullptr,
                              UI_TIP_STYLE_NORMAL,
                              UI_TIP_LC_VALUE,
                              true);
  }
  if (item_tooltip_data->hint[0]) {
    ui_tooltip_txt_field_add(data,
                              lib_strdup(item_tooltip_data->hint),
                              nullptr,
                              UI_TIP_STYLE_NORMAL,
                              UI_TIP_LC_NORMAL,
                              true);
  }

  if (data->fields_len == 0) {
    mem_free(data);
    return nullptr;
  }
  return data;
}

ARgn *ui_tooltip_create_from_search_item_generic(
    Cxt *C,
    const ARgn *searchbox_rgn,
    const rcti *item_rect,
    const uiSearchItemTooltipData *item_tooltip_data)
{
  uiTooltipData *data = ui_tooltip_data_from_search_item_tooltip_data(item_tooltip_data);
  if (data == nullptr) {
    return nullptr;
  }

  const float aspect = 1.0f;
  const Win *win = cxt_win(C);
  float init_pos[2];
  init_pos[0] = win->evstate->xy[0];
  init_pos[1] = item_rect->ymin + searchbox_rgn->winrct.ymin - (UI_POPUP_MARGIN / 2);

  return ui_tooltip_create_with_data(C, data, init_pos, nullptr, aspect);
}

void ui_tooltip_free(Cxt *C, Screen *screen, ARgn *rgn)
{
  ui_rgn_temp_remove(C, screen, rgn);
}
