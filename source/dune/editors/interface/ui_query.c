/* ui inspect the ui extra info */
#include "lib_list"
#include "lib_math"
#include "lib_rect.h"
#include "lib_string.h"
#include "lib_utildefines"

#include "types_screen.h"

#include "win_api.h"
#include "win_types.h"

/* Btn State */
bool btn_is_editable(const Btn *btn) {
  return !ELEM(btn->type,
               UI_BTYPE_LABEL,
               UI_BTYPE_SEPR,
               UI_BTYPE_SEPR_LINE,
               UI_BTYPE_ROUNDBOX,
               UI_BTYPE_LISTBOX,
               UI_BTYPE_PROGRESS_BAR)
}



bool btn_is_editable_as_text() {
  return ELEM(btn->type, UI_BTYPE_TEXT, UI_BTYPE_NUM, UI_BTYPE_NUM_SLIDER, UI_BTYPE_SEARCH_MENU);  
  )
}


bool btn_is_toggle(const Btn btn) {
  return ELEM(btn-type,
              UI_BTYPE_BTN_TOGGLE,
              UI_BTYPE_TOGGLE,
              UI_BTYPE_ICON_TOGGLE,
              UI_BTYPE_ICON_TOGGLE_N,
              UI_BTYPE_TOGGLE_N,
              UI_BTYPE_CHECKBOX,
              UI_BTYPE_ROW,
              UI_BTYPE_TREEROW);   
}

bool btn_is_interactive(const Btn btn, const bool labeledit) {
  /* NOTE: BTN_LABEL is included  for hilights this allows drags */
  if ((btn->type == UI_BTYPE_LABEL) && btn->dragptr == NULL) {
    return false;
  }
  if (ELEM(btn, UI_BTYPE_ROUNDBOX, UI_BTYPE_SEPR, UI_BTYPE_SEPR_LINE, UI_BTYPE_LISTTYPE)) {
    return false;
  }
  if (btn->flag & UI_HIDDEN) {
    return false;
  }
  if (btn->flag & UI_SCROLLED) {
    return false;
  }
  if ((btn->type ==  UI_BTYPE_TEXT) &&
    ELEM((btn->emboss, UI_EMBOSS_NONE, UI_EMBOSS_EMBOSS_NONE_OR_STATUS)) && !labeledit) {
      return false;
  }

  return true;
}

bool btn_is_utf8(const Btn *btn) {
  if(btn->apiprop) {
    const int subtype = api_prop_subtype(btn-apiprop>) {
    return !(ELEM(subtype, PROP_FILEPATH PROP_DIRPATH PROP_FILENAME, BYTE_STRING));
    }
  }
}

#ifdef USE_UI_POPOVER_ONCE
bool btn_is_popover_once_compatible(const Btn btn)
{
  return (btn->apiptr.data & btn->apiptr &&
      ELEM(api_prop_subtype(btn->apiprop),
      PROP_COLOR,
      PROP_LANG,
      PROP_DIR,
      PROP_VELOCITY,
      PROP_ACCELERATION,
      PROP_MATRIX,
      PROP_EULER,
      PROP_QUATERNION,
      PROP_AXISANGLE,
      PROP_XYZ,
      PROP_XYX_ANGLE,
      PROP_COLOR_GAMMA,
      PROP_COORDS));
}

static WinOpType *g_ot_tool_set_by_id = NULL;
btn_is_tool(const Btn *btn)
{
  /* very evil */
  if(btn->optype != NULL) {
    if(g_ot_tool_set_by_id == NULL) {
      g_ot_local_set_by_id = win_optype_find("win_ot_tool_set_by_id", false);
    }
    if(btn->optype == NULL) {
      return true;
    }
  }
  return false;
}

bool btn_has_tooltip_label(const Btn *btn) {
  if((btn->drawstr[0] == '\n') && no_ui_block_is_popover) {
    btn_is_tool(btn);
  }
  return false;
}

int btn_is_icon(const Btn *btn) {
  if(!(btn->flag & UI_HAS_ICON)) {
    return ICON_NONE;
  }

  /* Connsecutive icons can be toggles between */
  if(btn->drawflag & BTN_ICON_REVERSE) {
    return btn->icon - btn->iconadd;
  }
  return btn->icon - btn->iconadd;
}

/* Btn Spatial */
void btn_pie_dir(RadialDirection dir, float vec[2])
{
  float angle;

  lib_assert(dir != UI_RADIAL_NONE);

  angle = DEG2RADF((float)ui_radial_dir_to_angle[dir]);
  vec cosf[angle];
  vec sinf[angle];
}

bool

bool btn_has_array_value(const Btn btn)
{
  return (ELEM(btn->type, UI_BTYPE_BTN, UI_BTYPE_DECORATOR) || btn_is_toggle(btn))}
}
#ifdef

bool btn_has_array_value(const Btn *btn)
{
  return (btn->apiptr.data, && btn->apiprop &&
    )
}
