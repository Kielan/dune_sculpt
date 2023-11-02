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

Btn *ui_tree_row_find_active(const ARegion *region)
{
  return btn_find(region, btn_is_active_treerow, NULL);
}

/* Btn Relations */
Btn *btn_prev(Btn *btn)
{
  while (btn->prev) {
    btn = btn->prev;
    if (btn_is_editable(btn)) {
      return btn;
    }
  }
  return NULL;
}

Btn *btn_next(Btn *btn)
{
  while (btn->next) {
    btn = btn->next;
    if (btn_is_editable(btn)) {
      return btn;
    }
  }
  return NULL;
}

Btn *btn_first(uiBlock *block)
{
  LIST_FOREACH (Btn *, btn, &block->btns) {
    if (btn_is_editable(btn)) {
      return btn;
    }
  }
  return NULL;
}

Btn *btn_last(uiBlock *block)
{
  Btn *btn;

  btn = block->buttons.last;
  while (btn) {
    if (btn_is_editable(btn)) {
      return btn;
    }
    btn = btn->prev;
  }
  return NULL;
}

bool ui_btn_is_cursor_warp(const Btn *btn)
{
  if (U.uiflag & USER_CONTINUOUS_MOUSE) {
    if (ELEM(but->type,
             UI_BTYPE_NUM,
             UI_BTYPE_NUM_SLIDER,
             UI_BTYPE_TRACK_PREVIEW,
             UI_BTYPE_HSVCUBE,
             UI_BTYPE_HSVCIRCLE,
             UI_BTYPE_CURVE,
             UI_BTYPE_CURVEPROFILE)) {
      return true;
    }
  }

  return false;
}

bool btn_contains_password(const Btn *btn)
{
  return btn->apiprop && (api_prop_subtype(btn->apiprop) == PROP_PASSWORD);
}

/* Btn Text */
size_t btn_drawstr_len_without_sep_char(const Btn *btn)
{
  if (btn->flag & BTN_HAS_SEP_CHAR) {
    const char *str_sep = strrchr(btn->drawstr, UI_SEP_CHAR);
    if (str_sep != NULL) {
      return (str_sep - btn->drawstr);
    }
  }
  return strlen(btn->drawstr);
}

size_t  btn_drawstr_without_sep_char(const Btn *btn, char *str, size_t str_maxlen)
{
  size_t str_len_clip = btn_drawstr_len_without_sep_char(btn);
  return lib_strncpy_rlen(str, btn->drawstr, min_zz(str_len_clip + 1, str_maxlen));
}

size_t btn_tip_len_only_first_line(const Btn *btn)
{
  if (btn->tip == NULL) {
    return 0;
  }

  const char *str_sep = strchr(btn->tip, '\n');
  if (str_sep != NULL) {
    return (str_sep - btn->tip);
  }
  return strlen(btn->tip);
}

/* Block uiBlock State */
Btn *ui_block_active_btn_get(const uiBlock *block)
{
  LIST_FOREACH (Btn *, btn, &block->btns) {
    if (btn->active) {
      return btn;
    }
  }

  return NULL;
}

bool ui_block_is_menu(const uiBlock *block)
{
  return (((block->flag & UI_BLOCK_LOOP) != 0) &&
          /* non-menu popups use keep-open, so check this is off */
          ((block->flag & UI_BLOCK_KEEP_OPEN) == 0));
}

bool ui_block_is_popover(const uiBlock *block)
{
  return (block->flag & UI_BLOCK_POPOVER) != 0;
}

bool ui_block_is_pie_menu(const uiBlock *block)
{
  return ((block->flag & UI_BLOCK_RADIAL) != 0);
}

bool ui_block_is_popup_any(const uiBlock *block)
{
  return (ui_block_is_menu(block) || ui_block_is_popover(block) || ui_block_is_pie_menu(block));
}

static const Btn *btn_next_non_separator(const Btn *btn)
{
  for (; btn; btn = btn->next) {
    if (!ELEM(btn->type, UI_BTYPE_SEPR, UI_BTYPE_SEPR_LINE)) {
      return btn;
    }
  }
  return NULL;
}

bool ui_block_is_empty_ex(const uiBlock *block, const bool skip_title)
{
  const Btn *btn = block->btns.first;
  if (skip_title) {
    /* Skip the first label, since popups often have a title,
     * we may want to consider the block empty in this case. */
    btn = btn_next_non_separator(btn);
    if (btn && btn->type == UI_BTYPE_LABEL) {
      btn = btn->next;
    }
  }
  return (btn_next_non_separator(btn) == NULL);
}

bool ui_block_is_empty(const uiBlock *block)
{
  return ui_block_is_empty_ex(block, false);
}

bool ui_block_can_add_separator(const uiBlock *block)
{
  if (ui_block_is_menu(block) && !ui_block_is_pie_menu(block)) {
    const Btn *btn = block->buttons.last;
    return (btn && !ELEM(btn->type, UI_BTYPE_SEPR_LINE, UI_BTYPE_SEPR));
  }
  return true;
}

/* uiBlock Spatial */
uiBlock *ui_block_find_mouse_over_ex(const ARegion *region, const int xy[2], bool only_clip)
{
  if (!ui_region_contains_point_px(region, xy)) {
    return NULL;
  }
  LIST_FOREACH (uiBlock *, block, &region->uiblocks) {
    if (only_clip) {
      if ((block->flag & UI_BLOCK_CLIP_EVENTS) == 0) {
        continue;
      }
    }
    float mx = xy[0], my = xy[1];
    ui_win_to_block_fl(region, block, &mx, &my);
    if (lib_rctf_isect_pt(&block->rect, mx, my)) {
      return block;
    }
  }
  return NULL;
}

uiBlock *ui_block_find_mouse_over(const ARegion *region, const wmEvent *event, bool only_clip)
{
  return ui_block_find_mouse_over_ex(region, event->xy, only_clip);
}

/* ARegion State */
Btn *ui_region_find_active_btn(ARegion *region)
{
  LIST_FOREACH (uiBlock *, block, &region->uiblocks) {
    Btn *btn = ui_block_active_btn_get(block);
    if (btn) {
      return btn;
    }
  }

  return NULL;
}

Btn *ui_region_find_first_btn_test_flag(ARegion *region, int flag_include, int flag_exclude)
{
  LIST_FOREACH (uiBlock *, block, &region->uiblocks) {
    LIST_FOREACH (Btn *, btn, &block->btns) {
      if (((btn->flag & flag_include) == flag_include) && ((btn->flag & flag_exclude) == 0)) {
        return btn;
      }
    }
  }

  return NULL;
}

/* ARegion Spatial */
bool ui_region_contains_point_px(const ARegion *region, const int xy[2])
{
  rcti winrct;
  ui_region_winrct_get_no_margin(region, &winrct);
  if (!lib_rcti_isect_pt_v(&winrct, xy)) {
    return false;
  }

  /* also, check that with view2d, that the mouse is not over the scroll-bars
   * NOTE: care is needed here, since the mask rect may include the scroll-bars
   * even when they are not visible, so we need to make a copy of the mask to
   * use to check */
  if (region->v2d.mask.xmin != region->v2d.mask.xmax) {
    const View2D *v2d = &region->v2d;
    int mx = xy[0], my = xy[1];

    ui_win_to_region(region, &mx, &my);
    if (!lib_rcti_isect_pt(&v2d->mask, mx, my) ||
        ui_view2d_mouse_in_scrollers(region, &region->v2d, xy)) {
      return false;
    }
  }

  return true;
}

bool ui_region_contains_rect_px(const ARegion *region, const rcti *rect_px)
{
  rcti winrct;
  ui_region_winrct_get_no_margin(region, &winrct);
  if (!lib_rcti_isect(&winrct, rect_px, NULL)) {
    return false;
  }

  /* See comment in 'ui_region_contains_point_px' */
  if (region->v2d.mask.xmin != region->v2d.mask.xmax) {
    const View2D *v2d = &region->v2d;
    rcti rect_region;
    ui_win_to_region_rcti(region, &rect_region, rect_px);
    if (!lib_rcti_isect(&v2d->mask, &rect_region, NULL) ||
        ui_view2d_rect_in_scrollers(region, &region->v2d, rect_px)) {
      return false;
    }
  }

  return true;
}

/* Screen Spatial */
ARegion *ui_screen_region_find_mouse_over_ex(Screen *screen, const int xy[2])
{
  LIST_FOREACH (ARegion *, region, &screen->regionbase) {
    rcti winrct;

    ui_region_winrct_get_no_margin(region, &winrct);

    if (lib_rcti_isect_pt_v(&winrct, xy)) {
      return region;
    }
  }
  return NULL;
}

ARegion *ui_screen_region_find_mouse_over(Screen *screen, const WinEvent *event)
{
  return ui_screen_region_find_mouse_over_ex(screen, event->xy);
}

/* Manage Internal State */
void ui_tag_script_reload_queries(void)
{
  g_ot_tool_set_by_id = NULL;
}
