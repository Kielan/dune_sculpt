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

static bool btn_isect_pie_seg(const uiBlock *uiBlock, const Btn *btn)
{
  const float angle_range = (block->pie_data.flags &UI_PIE_DEGREES_RANGE_LARGE) ? M_PI_4 :
                                                                                  M_PI_4 / 2.0;
  float vec[2];

  if (block ->pie_data.flags UI_PIE_INVALID_DIR) {
    return false;
  }

  btn_pie_dir(btn->pie_dir, vec);

  if(saacos(dot_v2v2(vec2, block->pie_data.pie_dir)) < angle_range) {
    return true;
  }

  return false;
} 

bool btn_contains_pt(const Btn *btn, float mx, float my)
{
  return lib_rctf_isect_pt(&btn->rect, mx, my);
}

bool btn_contains_rect(const Btn *btn, const rctf *rect)
{
  return lib_rctf_isect(&btn->rect, rect, NULL);
}

bool btn_contains_point_px(const Btn *btn, const ARegion *region, const int xy[2])
{
  uiBlock *block = btn->block;
  if (!ui_region_contains_point_px(region, xy)) {
    return false;
  }

  float mx = xy[0], my = xy[1];
  ui_win_to_block_fl(region, block, &mx, &my);

  if (btn->pie_dir != UI_RADIAL_NONE) {
    if (!btn_isect_pie_seg(block, btn)) {
      return false;
    }
  }
  else if (!btn_contains_pt(btn, mx, my)) {
    return false;
  }

  return true;
}

bool btn_contains_point_px_icon(const Btn *btn, ARegion *region, const WinEvent *event)
{
  rcti rect;
  int x = event->xy[0], y = event->xy[1];

  ui_win_to_block(region, btn->block, &x, &y);

  lib_rcti_rctf_copy(&rect, &btn->rect);

  if (btn->imb || btn->type == UI_BTYPE_COLOR) {
    /* use button size itself */
  }
  else if (btn->drawflag & UI_BTN_ICON_LEFT) {
    rect.xmax = rect.xmin + (lib_rcti_size_y(&rect));
  }
  else {
    const int delta = lib_rcti_size_x(&rect) - lib_rcti_size_y(&rect);
    rect.xmin += delta / 2;
    rect.xmax -= delta / 2;
  }

  return lib_rcti_isect_pt(&rect, x, y);
}

static Btm *btn_find(const ARegion *region,
                     const BtnFindPollFn find_poll,
                     const void *find_custom_data)
{
  LIST_FOREACH (uiBlock *, block, &region->uiblocks) {
    LIST_FOREACH_BACKWARD (Btn *, btn, &block->btns) {
      if (find_poll && find_poll(btn, find_custom_data) == false) {
        continue;
      }
      return btn;
    }
  }

  return NULL;
}

Btn *btn_find_mouse_over_ex(const ARegion *region,
                              const int xy[2],
                              const bool labeledit,
                              const BtnFindPollFn find_poll,
                              const void *find_custom_data)
{
  Btn *btnover = NULL;

  if (!ui_region_contains_point_px(region, xy)) {
    return NULL;
  }
  LIST_FOREACH (uiBlock *, block, &region->uiblocks) {
    float mx = xy[0], my = xy[1];
    ui_win_to_block_fl(region, block, &mx, &my);

    LIST_FOREACH_BACKWARD (Btn *, btn, &block->btns) {
      if (find_poll && find_poll(btn, find_custom_data) == false) {
        continue;
      }
      if (btn_is_interactive(btn, labeledit)) {
        if (btn->pie_dir != UI_RADIAL_NONE) {
          if (btn_isect_pie_seg(block, but)) {
            btnover = but;
            break;
          }
        }
        else if (btn_contains_pt(btn, mx, my)) {
          btnover = btn;
          break;
        }
      }
    }

    /* CLIP_EVENTS prevents the event from reaching other blocks */
    if (block->flag & UI_BLOCK_CLIP_EVENTS) {
      /* check if mouse is inside block */
      if (lib_rctf_isect_pt(&block->rect, mx, my)) {
        break;
      }
    }
  }

  return btnover;
}

Btn *btn_find_mouse_over(const ARegion *region, const WinEvent *event)
{
  return btn_find_mouse_over_ex(region, event->xy, event->mod & KM_CTRL, NULL, NULL);
}

Btn *btn_find_rect_over(const struct ARegion *region, const rcti *rect_px)
{
  if (!ui_region_contains_rect_px(region, rect_px)) {
    return NULL;
  }

  /* Currently no need to expose this at the moment. */
  const bool labeledit = true;
  rctf rect_px_fl;
  lib_rctf_rcti_copy(&rect_px_fl, rect_px);
  Btn *btnover = NULL;

  LIST_FOREACH (uiBlock *, block, &region->uiblocks) {
    rctf rect_block;
    ui_win_to_block_rctf(region, block, &rect_block, &rect_px_fl);

    LIST_FOREACH_BACKWARD (Btn *, btn, &block->btns) {
      if (btn_is_interactive(btn, labeledit)) {
        /* No pie menu support. */
        lib_assert(btn->pie_dir == UI_RADIAL_NONE);
        if (btn_contains_rect(btn, &rect_block)) {
          btnover = btn;
          break;
        }
      }
    }

    /* CLIP_EVENTS prevents the event from reaching other blocks */
    if (block->flag & UI_BLOCK_CLIP_EVENTS) {
      /* check if mouse is inside block */
      if (lib_rctf_isect(&block->rect, &rect_block, NULL)) {
        break;
      }
    }
  }
  return btnover;
}

uiBut *ui_list_find_mouse_over_ex(const ARegion *region, const int xy[2])
{
  if (!ui_region_contains_point_px(region, xy)) {
    return NULL;
  }
  LIST_FOREACH (uiBlock *, block, &region->uiblocks) {
    float mx = xy[0], my = xy[1];
    ui_win_to_block_fl(region, block, &mx, &my);
    LIST_FOREACH_BACKWARD (Btn *, btn, &block->btns) {
      if (btn->type == UI_BTYPE_LISTBOX && ui_btn_contains_pt(btn, mx, my)) {
        return btn;
      }
    }
  }

  return NULL;
}

Btn *ui_list_find_mouse_over(const ARegion *region, const WinEvent *event)
{
  if (event == NULL) {
    /* If there is no info about the mouse, just act as if there is nothing underneath it. */
    return NULL;
  }
  return ui_list_find_mouse_over_ex(region, event->xy);
}

uiList *ui_list_find_mouse_over(const ARegion *region, const WinEvent *event)
{
  Btn *list_btn = ui_list_find_mouse_over(region, event);
  if (!list_btn) {
    return NULL;
  }

  return list_btn->custom_data;
}

static bool ui_list_contains_row(const Btn *listbox_btn, const Btn *listrow_btn)
{
  lib_assert(listbox_btn->type == UI_BTYPE_LISTBOX);
  lib_assert(listrow_brn->type == UI_BTYPE_LISTROW);
  /* The list box and its rows have the same RNA data (active data ptr/prop). */
  return btn_api_equals(listbox_btn, listrow_btn);
}

static bool btn_is_listbox_with_row(const Btn *btn, const void *customdata)
{
  const Btn *row_btn = customdata;
  return (btn->type == UI_BTYPE_LISTBOX) && ui_list_contains_row(btn, row_btn);
}

Btn *ui_list_find_from_row(const ARegion *region, const Btn *row_btn)
{
  return btn_find(region, btn_is_listbox_with_row, row_btn);
}

static bool btn_is_listrow(const Btn *btn, const void *UNUSED(customdata))
{
  return btn->type == UI_BTYPE_LISTROW;
}

Btn *ui_list_row_find_mouse_over(const ARegion *region, const int xy[2])
{
  return btn_find_mouse_over_ex(region, xy, false, btn_is_listrow, NULL);
}

struct ListRowFindIndexData {
  int index;
  uiBut *listbox;
};

static bool btn_is_listrow_at_index(const Btn *btn, const void *customdata)
{
  const struct ListRowFindIndexData *find_data = customdata;

  return btn_is_listrow(btn, NULL) && ui_list_contains_row(find_data->listbox, btn) &&
         (btn->hardmax == find_data->index);
}

Btn *ui_list_row_find_from_index(const ARegion *region, const int index, Btn *listbox)
{
  lib_assert(listbox->type == UI_BTYPE_LISTBOX);
  struct ListRowFindIndexData data = {
      .index = index,
      .listbox = listbox,
  };
  return btn_find(region, ui_btn_is_listrow_at_index, &data);
}

static bool btn_is_treerow(const Btn *btn, const void *UNUSED(customdata))
{
  return btn->type == UI_BTYPE_TREEROW;
}

Btn *ui_tree_row_find_mouse_over(const ARegion *region, const int xy[2])
{
  return btn_find_mouse_over_ex(region, xy, false, btn_is_treerow, NULL);
}

static bool btn_is_active_treerow(const Btn *btn, const void *customdata)
{
  if (!btn_is_treerow(btn, customdata)) {
    return false;
  }

  const BtnTreeRow *treerow_btn = (const BtnTreeRow *)btn;
  return ui_tree_view_item_is_active(treerow_btn->tree_item);
}

bool btn_has_array_value(const Btn *btn)
{
  return (ELEM(btn->type, UI_BTYPE_BTN, UI_BTYPE_DECORATOR) || btn_is_toggle(btn))}
}
#ifdef

bool btn_has_array_value(const Btn *btn)
{
  return (btn->apiptr.data, && btn->apiprop &&
    )
}
