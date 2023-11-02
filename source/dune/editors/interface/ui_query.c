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
  
}
