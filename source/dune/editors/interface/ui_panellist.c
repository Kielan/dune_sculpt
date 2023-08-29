/* a full doc with API notes can be found in
 * bf-blender/trunk/blender/doc/guides/interface_API.txt */

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "PIL_time.h"

#include "lib_blenlib.h"
#include "lib_math.h"
#include "lib_utildefines.h"

#include "i18n_translation.h"

#include "types_screen.h"
#include "types_userdef.h"

#include "dune_cxt.h"
#include "dune_screen.h"

#include "api_access.h"

#include "BLF_api.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ed_screen.h"

#include "ui_interface.h"
#include "ui_interface_icons.h"
#include "ui_resources.h"
#include "ui_view2d.h"

#include "gpu_batch_presets.h"
#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gpi_state.h"

#include "interface_intern.h"

/* -------------------------------------------------------------------- */
/** Defines & Structs **/

#define ANIMATION_TIME 0.30
#define ANIMATION_INTERVAL 0.02

typedef enum uiPanelRuntimeFlag {
  PANEL_LAST_ADDED = (1 << 0),
  PANEL_ACTIVE = (1 << 2),
  PANEL_WAS_ACTIVE = (1 << 3),
  PANEL_ANIM_ALIGN = (1 << 4),
  PANEL_NEW_ADDED = (1 << 5),
  PANEL_SEARCH_FILTER_MATCH = (1 << 7),
  /* Use the status set by prop search (PANEL_SEARCH_FILTER_MATCH)
   * instead of PNL_CLOSED. Set to true on every property search update. */
  PANEL_USE_CLOSED_FROM_SEARCH = (1 << 8),
  /** The Panel was before the start of the current / latest layout pass. */
  PANEL_WAS_CLOSED = (1 << 9),
  /* Set when the panel is being dragged and while it animates back to its aligned
   * position. Unlike PANEL_STATE_ANIMATION, this is applied to sub-panels as well. */
  PANEL_IS_DRAG_DROP = (1 << 10),
  /* Draw a border with the active color around the panel. */
  PANEL_ACTIVE_BORDER = (1 << 11),
} uiPanelRuntimeFlag;

/* The state of the mouse position relative to the panel. */
typedef enum uiPanelMouseState {
  PANEL_MOUSE_OUTSIDE,        /** Mouse is not in the panel. */
  PANEL_MOUSE_INSIDE_CONTENT, /** Mouse is in the actual panel content. */
  PANEL_MOUSE_INSIDE_HEADER,  /** Mouse is in the panel header. */
} uiPanelMouseState;

typedef enum uiHandlePanelState {
  PANEL_STATE_DRAG,
  PANEL_STATE_ANIMATION,
  PANEL_STATE_EXIT,
} uiHandlePanelState;

typedef struct uiHandlePanelData {
  uiHandlePanelState state;
  /* Animation. */
  wmTimer *animtimer;
  double starttime;
  /* Dragging. */
  int startx, starty;
  int startofsx, startofsy;
  float start_cur_xmin, start_cur_ymin;
} uiHandlePanelData;

typedef struct PanelSort {
  Panel *panel;
  int new_offset_x;
  int new_offset_y;
} PanelSort;

static void panel_set_expansion_from_list_data(const Cxt *C, Panel *panel);
static int get_panel_real_size_y(const Panel *panel);
static void panel_activate_state(const Cxt *C, Panel *panel, uiHandlePanelState state);
static int compare_panel(const void *a, const void *b);
static bool panel_type_context_poll(ARegion *region,
                                    const PanelType *panel_type,
                                    const char *cxt);

/** Local Functions **/
static bool panel_active_animation_changed(List *lb,
                                           Panel **r_panel_animation,
                                           bool *r_no_animation)
{
  LIST_FOREACH (Panel *, panel, lb) {
    /* Detect panel active flag changes. */
    if (!(panel->type && panel->type->parent)) {
      if ((panel->runtime_flag & PANEL_WAS_ACTIVE) && !(panel->runtime_flag & PANEL_ACTIVE)) {
        return true;
      }
      if (!(panel->runtime_flag & PANEL_WAS_ACTIVE) && (panel->runtime_flag & PANEL_ACTIVE)) {
        return true;
      }
    }

    /* Detect changes in panel expansions. */
    if ((bool)(panel->runtime_flag & PANEL_WAS_CLOSED) != ui_panel_is_closed(panel)) {
      *r_panel_animation = panel;
      return false;
    }

    if ((panel->runtime_flag & PANEL_ACTIVE) && !ui_panel_is_closed(panel)) {
      if (panel_active_animation_changed(&panel->children, r_panel_animation, r_no_animation)) {
        return true;
      }
    }

    /* Detect animation. */
    if (panel->activedata) {
      uiHandlePanelData *data = panel->activedata;
      if (data->state == PANEL_STATE_ANIMATION) {
        *r_panel_animation = panel;
      }
      else {
        /* Don't animate while handling other interaction. */
        *r_no_animation = true;
      }
    }
    if ((panel->runtime_flag & PANEL_ANIM_ALIGN) && !(*r_panel_animation)) {
      *r_panel_animation = panel;
    }
  }

  return false;
}

/* return: true if the properties editor switch tabs since the last layout pass. **/
static bool props_space_needs_realign(const ScrArea *area, const ARegion *region)
{
  if (area->spacetype == SPACE_PROPS && region->regiontype == RGN_TYPE_WINDOW) {
    SpaceProps *sbtns = area->spacedata.first;

    if (sbtns->mainbo != sbtns->mainb) {
      return true;
    }
  }

  return false;
}

static bool panels_need_realign(const ScrArea *area, ARegion *region, Panel **r_panel_animation)
{
  *r_panel_animation = NULL;

  if (props_space_needs_realign(area, region)) {
    return true;
  }

  /* Detect if a panel was added or removed. */
  Panel *panel_animation = NULL;
  bool no_animation = false;
  if (panel_active_anim_changed(&region->panels, &panel_animation, &no_animation)) {
    return true;
  }

  /* Detect panel marked for animation, if we're not already animating. */
  if (panel_animation) {
    if (!no_animation) {
      *r_panel_animation = panel_animation;
    }
    return true;
  }

  return false;
}

/* Fns for Instanced PanelList **/
static PanelList *panellist_add_instanced(ARegion *region,
                                  List *panels,
                                  PanelType *panel_type,
                                  ApiPtr *custom_data)
{
  PanelList *panellist = mem_callocn(sizeof(Panel), __func__);
  panel->type = panel_type;
  lib_strncpy(panel->panelname, panel_type->idname, sizeof(panel->panelname));

  panel->runtime.custom_data_ptr = custom_data;
  panel->runtime_flag |= PANEL_NEW_ADDED;

  /* Add the panel's children too. Although they aren't instanced panels, we can still use this
   * function to create them, as UI_panel_begin does other things we don't need to do. */
  LIST_FOREACH (LinkData *, child, &panel_type->children) {
    PanelType *child_type = child->data;
    panellistinstanced_add(region, &panel->children, child_type, custom_data);
  }

  /* Make sure the panel is added to the end of the display-order as well. This is needed for
   * loading existing files.
   *
   * NOTE: We could use special behavior to place it after the panel that starts the list of
   * instanced panels, but that would add complexity that isn't needed for now. */
  int max_sortorder = 0;
  LIST_FOREACH (Panel *, existing_panel, panels) {
    if (existing_panel->sortorder > max_sortorder) {
      max_sortorder = existing_panel->sortorder;
    }
  }
  panel->sortorder = max_sortorder + 1;

  lib_addtail(panels, panel);

  return panel;
}

Panel *panellistinstanced_add(const Cxt *C,
                              ARegion *region,
                              List *panels,
                              const char *panel_idname,
                              ApiPtr *custom_data)
{
  ARegionType *region_type = region->type;

  PanelType *panel_type = lib_findstring(
      &region_type->paneltypes, panel_idname, offsetof(PanelType, idname));

  if (panel_type == NULL) {
    printf("Panel type '%s' not found.\n", panel_idname);
    return NULL;
  }

  Panel *new_panel = panellistinstanced_add(region, panels, panel_type, custom_data);

  /* Do this after #panellistinstanced_add so all sub-panels are added. */
  panel_set_expansion_from_list_data(C, new_panel);

  return new_panel;
}

void panellist_unique_str(Panel *panel, char *r_name)
{
  /* The panel sort-order will be unique for a specific panel type because the instanced
   * panel list is regenerated for every change in the data order / length. */
  snprintf(r_name, INSTANCED_PANEL_UNIQUE_STR_LEN, "%d", panel->sortorder);
}

/* Free a panel and its children. Custom data is shared by the panel and its children
 * and is freed by ui_panellistinstanced_free.
 *
 * note: The only panels that should need to be deleted at runtime are panels with the
 * PANEL_TYPE_INSTANCED flag set. */
static void panel_delete(const duneContext *C, ARegion *region, ListBase *panels, Panel *panel)
{
  /* Recursively delete children. */
  LIST_FOREACH_MUTABLE (Panel *, child, &panel->children) {
    panel_delete(C, region, &panel->children, child);
  }
  lib_freelistn(&panel->children);

  lib_remlink(panels, panel);
  if (panel->activedata) {
    mem_freen(panel->activedata);
  }
  mem_freen(panel);
}

void panellistinstanced_free(const Cxt *C, ARegion *region)
{
  /* Delete panels with the instanced flag. */
  LIST_FOREACH_MUTABLE (Panel *, panel, &region->panels) {
    if ((panel->type != NULL) && (panel->type->flag & PANEL_TYPE_INSTANCED)) {
      /* Make sure the panel's handler is removed before deleting it. */
      if (C != NULL && panel->activedata != NULL) {
        panel_activate_state(C, panel, PANEL_STATE_EXIT);
      }

      /* Free panel's custom data. */
      if (panel->runtime.custom_data_ptr != NULL) {
        mem_freen(panel->runtime.custom_data_ptr);
      }

      /* Free the panel and its sub-panels. */
      panel_delete(C, region, &region->panels, panel);
    }
  }
}

bool ui_panellist_matches_data(ARegion *region,
                               List *data,
                               uiListPanelIdFromDataFn panel_idname_fn)
{
  /* Check for NULL data. */
  int data_len = 0;
  Link *data_link = NULL;
  if (data == NULL) {
    data_len = 0;
    data_link = NULL;
  }
  else {
    data_len = lib_list_count(data);
    data_link = data->first;
  }

  int i = 0;
  LIST_FOREACH (Panel *, panel, &region->panels) {
    if (panel->type != NULL && panel->type->flag & PANEL_TYPE_INSTANCED) {
      /* The panels were reordered by drag and drop. */
      if (panel->flag & PNL_INSTANCED_LIST_ORDER_CHANGED) {
        return false;
      }

      /* We reached the last data item before the last instanced panel. */
      if (data_link == NULL) {
        return false;
      }

      /* Check if the panel type matches the panel type from the data item. */
      char panel_idname[MAX_NAME];
      panel_idname_fn(data_link, panel_idname);
      if (!STREQ(panel_idname, panel->type->idname)) {
        return false;
      }

      data_link = data_link->next;
      i++;
    }
  }

  /* If we didn't make it to the last list item, the panel list isn't complete. */
  if (i != data_len) {
    return false;
  }

  return true;
}

static void panellistinstanced_reorder(Cxt *C, ARegion *region, PanelList *panellist_drag)
{
  /* Without a type we cannot access the reorder callback. */
  if (drag_panel->type == NULL) {
    return;
  }
  /* Don't reorder if this instanced panel doesn't support drag and drop reordering. */
  if (drag_panel->type->reorder == NULL) {
    return;
  }

  char *cxt = NULL;
  if (!panellist_category_is_visible(region)) {
    cxt = drag_panel->type->cxt;
  }

  /* Find how many instanced panels with this context string. */
  int panellist_len = 0;
  int start_index = -1;
  LIST_FOREACH (const Panel *, panel, &region->panellist) {
    if (panel->type) {
      if (panel->type->flag & PANEL_TYPE_INSTANCED) {
        if (panel_type_cxt_poll(region, panel->type, cxt)) {
          if (panel == drag_panel) {
            lib_assert(start_index == -1); /* This panel should only appear once. */
            start_index = list_panels_len;
          }
          panellist_len++;
        }
      }
    }
  }
  lib_assert(start_index != -1); /* The drag panel should definitely be in the list. */

  /* Sort the matching instanced panels by their display order. */
  PanelSort *panel_sort = mem_callocn(list_panels_len * sizeof(*panel_sort), __func__);
  PanelSort *sort_index = panel_sort;
  LIST_FOREACH (Panel *, panel, &region->panels) {
    if (panel->type) {
      if (panel->type->flag & PANEL_TYPE_INSTANCED) {
        if (panel_type_cxt_poll(region, panel->type, cxt)) {
          sort_index->panel = panel;
          sort_index++;
        }
      }
    }
  }
  qsort(panel_sort, list_panels_len, sizeof(*panel_sort), compare_panel);

  /* Find how many of those panels are above this panel. */
  int move_to_index = 0;
  for (; move_to_index < list_panels_len; move_to_index++) {
    if (panel_sort[move_to_index].panel == drag_panel) {
      break;
    }
  }

  mem_freen(panel_sort);

  if (move_to_index == start_index) {
    /* In this case, the reorder was not changed, so don't do any updates or call the callback. */
    return;
  }

  /* Set the bit to tell the interface to instanced the list. */
  drag_panel->flag |= PNL_INSTANCED_LIST_ORDER_CHANGED;

  cxt_store_set(C, drag_panel->runtime.ctx);

  /* Finally, move this panel's list item to the new index in its list. */
  drag_panel->type->reorder(C, drag_panel, move_to_index);

  cxt_store_set(C, NULL);
}

/* Recursive implementation for #panellistdata_expansion.
 *
 * return: Whether the closed flag for the panel or any sub-panels changed. */
static bool panellistdata_expand_recursive(Panel *panel, short flag, short *flag_index)
{
  const bool open = (flag & (1 << *flag_index));
  bool changed = (open == ui_panel_is_closed(panel));

  SET_FLAG_FROM_TEST(panel->flag, !open, PNL_CLOSED);

  LIST_FOREACH (Panel *, child, &panel->children) {
    *flag_index = *flag_index + 1;
    changed |= panellistdata_expand_recursive(child, flag, flag_index);
  }
  return changed;
}

/* Set the expansion of the panel and its sub-panels from the flag stored in the
 * corresponding list data. The flag has expansion stored in each bit in depth first order */
static void panellistdata_set_expansion_from(const Cxt *C, Panel *panel)
{
  lib_assert(panel->type != NULL);
  lib_assert(panel->type->flag & PANEL_TYPE_INSTANCED);
  if (panel->type->get_list_data_expand_flag == NULL) {
    /* Instanced panel doesn't support loading expansion. */
    return;
  }

  const short expand_flag = panel->type->get_list_data_expand_flag(C, panel);
  short flag_index = 0;

  /* Start panel animation if the open state was changed. */
  if (panellistdata_expand_recursive(panel, expand_flag, &flag_index)) {
    panel_activate_state(C, panel, PANEL_STATE_ANIMATION);
  }
}

/* Set expansion based on the data for instanced panels */
static void region_panellistdata_expand(const Cxt *C, ARegion *region)
{
  LIST_FOREACH (Panel *, panel, &region->panels) {
    if (panel->runtime_flag & PANEL_ACTIVE) {
      PanelType *panel_type = panel->type;
      if (panel_type != NULL && panel->type->flag & PANEL_TYPE_INSTANCED) {
        panellistdata_expand(C, panel);
      }
    }
  }
}

/* Recursive implementation for #panellistdata_flag_expand_set */
static void panellist_expand_flag_get(const Panel *panel, short *flag, short *flag_index)
{
  const bool open = !(panellist->flag & PNL_CLOSED);
  SET_FLAG_FROM_TEST(*flag, open, (1 << *flag_index));

  LIST_FOREACH (const Panel *, child, &panel->children) {
    *flag_index = *flag_index + 1;
    get_panel_expand_flag(child, flag, flag_index);
  }
}

/* Call the callback to store the panel and sub-panel expansion settings in the list item that
 * corresponds to each instanced panel.
 *
 * note: This needs to iterate through all of the region's panels because the panel with changed
 * expansion might have been the sub-panel of an instanced panel, meaning it might not know
 * which list item it corresponds to. */
static void panellistdata_expand_flag_set(const duneContext *C, const ARegion *region)
{
  LIST_FOREACH (Panel *, panel, &region->panels) {
    PanelType *panel_type = panel->type;
    if (panel_type == NULL) {
      continue;
    }

    /* Check for #PANEL_ACTIVE so we only set the expand flag for active panels. */
    if (panellist_type->flag & PANEL_TYPE_INSTANCED && panel->runtime_flag & PANEL_ACTIVE) {
      short expand_flag;
      short flag_index = 0;
      panellist_expand_flag_get(panellist, &expand_flag, &flag_index);
      if (panel->type->set_list_data_expand_flag) {
        panellist->type->set_list_data_expand_flag(C, panellist, expand_flag);
      }
    }
  }
}

/** Panels **/
static bool panellistdata_custom_active_get(const PanelList *panellist)
{
  /* The caller should make sure the panel is active and has a type. */
  lib_assert(panellist_is_active(panel));
  lib_assert(panellist->type != NULL);

  if (panellist->type->active_prop[0] != '\0') {
    ApiPtr *ptr = panellist_custom_data_get(panel);
    if (ptr != NULL && !api_ptr_is_null(ptr)) {
      return api_bool_get(ptr, panel->type->active_prop);
    }
  }

  return false;
}

static void panellist_custom_data_active_set(Panel *panel)
{
  /* Since the panel is interacted with, it should be active and have a type. */
  lib_assert(panellist_is_active(panel));
  lib_assert(panellist->type != NULL);

  if (panellist->type->active_prop[0] != '\0') {
    ApiPtr *ptr =panellistdata_custom__get(panel);
    lib_assert(api_struct_find_prop(ptr, panellist->type->active_prop) != NULL);
    if (ptr != NULL && !api_ptr_is_null(ptr)) {
      api_bool_set(ptr, panellist->type->active_prop, true);
    }
  }
}

/** Set flag state for a panel and its sub-panels. **/
static void panellist_flag_set_recursive(Panel *panel, short flag, bool value)
{
  SET_FLAG_FROM_TEST(panel->flag, value, flag);

  LIST_FOREACH (Panel *, child, &panel->children) {
    panellist_flag_set_recursive(child, flag, value);
  }
}

/** Set runtime flag state for a panel and its sub-panels. **/
static void panellist_runtimeflag_set_recursive(Panel *panel, short flag, bool value)
{
  SET_FLAG_FROM_TEST(panel->runtime_flag, value, flag);

  LIST_FOREACH (Panel *, sub_panel, &panel->children) {
    panel_set_runtime_flag_recursive(sub_panel, flag, value);
  }
}

static void panellist_collapse_all(ARegion *region, const PanelList *from_panellist)
{
  const bool has_category_tabs = panellist_category_is_visible(region);
  const char *category = has_category_tabs ? panellist_category_active_get(region, false) : NULL;
  const PanelType *from_pt = from_panel->type;

  LIST_FOREACH (Panel *, panel, &region->panels) {
    PanelType *pt = panel->type;

    /* Close panels with headers in the same context. */
    if (pt && from_pt && !(pt->flag & PANEL_TYPE_NO_HEADER)) {
      if (!pt->ctx[0] || !from_pt->ctx[0] || STREQ(pt->ctx, from_pt->ctx)) {
        if ((panel->flag & PNL_PIN) || !category || !pt->category[0] ||
            STREQ(pt->category, category)) {
          panel->flag |= PNL_CLOSED;
        }
      }
    }
  }
}

static bool panellist_type_cxt_poll(ARegion *region,
                                    const PanelType *panel_type,
                                    const char *cxt)
{
  if (!lib_list_is_empty(&region->panellist_category)) {
    return STREQ(panel_type->category, panellist_category_active_get(region, false));
  }

  if (panel_type->context[0] && STREQ(panel_type->ctx, ctx)) {
    return true;
  }

  return false;
}

PanelList *panellist_find_by_type(ListBase *lb, const PanelType *pt)
{
  const char *idname = pt->idname;

  LIST_FOREACH (Panel *, panel, lb) {
    if (STREQLEN(panel->panelname, idname, sizeof(panel->panelname))) {
      return panel;
    }
  }
  return NULL;
}

PanelList *panellist_begin(
    ARegion *region, List *lb, uiBlock *block, PanelType *pt, PanelList *panellist, bool *r_open)
{
  PanelList *panellist_last;
  const char *drawname = cxt_IFACE_(pt->translation_cxt, pt->label);
  const char *idname = pt->idname;
  const bool newpanel = (panel == NULL);

  if (newpanel) {
    panel = mem_callocn(sizeof(Panel), __func__);
    panel->type = pt;
    lib_strncpy(panel->panelname, idname, sizeof(panel->panelname));

    if (pt->flag & PANEL_TYPE_DEFAULT_CLOSED) {
      panel->flag |= PNL_CLOSED;
      panel->runtime_flag |= PANEL_WAS_CLOSED;
    }

    panel->ofsx = 0;
    panel->ofsy = 0;
    panel->sizex = 0;
    panel->sizey = 0;
    panel->blocksizex = 0;
    panel->blocksizey = 0;
    panel->runtime_flag |= PANEL_NEW_ADDED;

    lib_addtail(lb, panel);
  }
  else {
    /* Panel already exists. */
    panel->type = pt;
  }

  panel->runtime.block = block;

  lib_strncpy(panel->drawname, drawname, sizeof(panel->drawname));

  /* If a new panel is added, we insert it right after the panel that was last added.
   * This way new panels are inserted in the right place between versions. */
  for (panel_last = lb->first; panel_last; panel_last = panel_last->next) {
    if (panel_last->runtime_flag & PANEL_LAST_ADDED) {
      lib_remlink(lb, panel);
      lib_insertlinkafter(lb, panel_last, panel);
      break;
    }
  }

  if (newpanel) {
    panel->sortorder = (panel_last) ? panel_last->sortorder + 1 : 0;

    LIST_FOREACH (Panel *, panel_next, lb) {
      if (panel_next != panel && panel_next->sortorder >= panel->sortorder) {
        panel_next->sortorder++;
      }
    }
  }

  if (panel_last) {
    panel_last->runtime_flag &= ~PANEL_LAST_ADDED;
  }

  /* Assign the new panel to the block. */
  block->panel = panel;
  panel->runtime_flag |= PANEL_ACTIVE | PANEL_LAST_ADDED;
  if (region->alignment == RGN_ALIGN_FLOAT) {
    uiblock_theme_style_set(block, UI_BLOCK_THEME_STYLE_POPUP);
  }

  *r_open = false;

  if (ui_panel_is_closed(panel)) {
    return panel;
  }

  *r_open = true;

  return panel;
}

void ui_panel_headerbtns_begin(Panel *panel)
{
  uiBlock *block = panel->runtime.block;

  ui_block_new_btn_group(block, UI_BTN_GROUP_LOCK | UI_BTN_GROUP_PANEL_HEADER);
}

void ui_panellist_headerbtns_end(Panel *panel)
{
  uiBlock *block = panel->runtime.block;

  /* A btn group should always be created in ui_panellist_headerbtns_begin. */
  lib_assert(!lib_list_is_empty(&block->btn_groups));

  uiBtnGroup *btn_group = block->btn_groups.last;

  btn_group->flag &= ~UI_BTN_GROUP_LOCK;

  /* Repurpose the first header button group if it is empty, in case the first button added to
   * the panel doesn't add a new group (if the button is created directly rather than through an
   * interface layout call). */
  if (lib_list_is_single(&block->btn_groups) &&
      lib_list_is_empty(&btn_group->btns)) {
    btn_group->flag &= ~UI_BTN_GROUP_PANEL_HEADER;
  }
  else {
    /* Always add a new btn group. Although this may result in many empty groups, without it,
     * new btns in the panel body not protected with a ui_block_new_btn_group call would
     * end up in the panel header group. */
    ui_block_btn_group_new(block, 0);
  }
}

static float panellist_region_offset_x_get(const ARegion *region)
{
  if (ui_panellist_category_visible(region)) {
    if (RGN_ALIGN_ENUM_FROM_MASK(region->alignment) != RGN_ALIGN_RIGHT) {
      return UI_PANEL_CATEGORY_MARGIN_WIDTH;
    }
  }

  return 0.0f;
}

/* mapping fn to ensure 'cur' draws extended over the area where sliders are */
static void view2d_map_cur_using_mask(const View2D *v2d, rctf *r_curmasked)
{
  *r_curmasked = v2d->cur;

  if (view2d_scroll_mapped(v2d->scroll)) {
    const float sizex = lib_rcti_size_x(&v2d->mask);
    const float sizey = lib_rcti_size_y(&v2d->mask);

    /* prevent tiny or narrow regions to get
     * invalid coordinates - mask can get negative even... */
    if (sizex > 0.0f && sizey > 0.0f) {
      const float dx = lib_rctf_size_x(&v2d->cur) / (sizex + 1);
      const float dy = lib_rctf_size_y(&v2d->cur) / (sizey + 1);

      if (v2d->mask.xmin != 0) {
        r_curmasked->xmin -= dx * (float)v2d->mask.xmin;
      }
      if (v2d->mask.xmax + 1 != v2d->winx) {
        r_curmasked->xmax += dx * (float)(v2d->winx - v2d->mask.xmax - 1);
      }

      if (v2d->mask.ymin != 0) {
        r_curmasked->ymin -= dy * (float)v2d->mask.ymin;
      }
      if (v2d->mask.ymax + 1 != v2d->winy) {
        r_curmasked->ymax += dy * (float)(v2d->winy - v2d->mask.ymax - 1);
      }
    }
  }
}

void ui_view2d_view_ortho(const View2D *v2d)
{
  rctf curmasked;
  const int sizex = lib_rcti_size_x(&v2d->mask);
  const int sizey = lib_rcti_size_y(&v2d->mask);
  const float eps = 0.001f;
  float xofs = 0.0f, yofs = 0.0f;

  /* Pixel offsets (-GLA_PIXEL_OFS) are needed to get 1:1
   * correspondence with pixels for smooth UI drawing,
   * but only applied where requested.  */
  /* XXX brecht: instead of zero at least use a tiny offset, otherwise
   * pixel rounding is effectively random due to float inaccuracy */
  if (sizex > 0) {
    xofs = eps * lib_rctf_size_x(&v2d->cur) / sizex;
  }
  if (sizey > 0) {
    yofs = eps * lib_rctf_size_y(&v2d->cur) / sizey;
  }

  /* apply mask-based adjustments to cur rect (due to scrollers),
   * to eliminate scaling artifacts */
  view2d_map_cur_using_mask(v2d, &curmasked);

  lib_rctf_translate(&curmasked, -xofs, -yofs);

  /* XXX ton: this flag set by outliner, for icons */
  if (v2d->flag & V2D_PIXELOFS_X) {
    curmasked.xmin = floorf(curmasked.xmin) - (eps + xofs);
    curmasked.xmax = floorf(curmasked.xmax) - (eps + xofs);
  }
  if (v2d->flag & V2D_PIXELOFS_Y) {
    curmasked.ymin = floorf(curmasked.ymin) - (eps + yofs);
    curmasked.ymax = floorf(curmasked.ymax) - (eps + yofs);
  }

  /* set matrix on all appropriate axes */
  wmOrtho2(curmasked.xmin, curmasked.xmax, curmasked.ymin, curmasked.ymax);
}

void ui_view2d_view_orthoSpecial(ARegion *region, View2D *v2d, const bool xaxis)
{
  rctf curmasked;
  float xofs, yofs;

  /* Pixel offsets (-GLA_PIXEL_OFS) are needed to get 1:1
   * correspondence with pixels for smooth UI drawing,
   * but only applied where requested. */
  /* XXX(ton): temp. */
  xofs = 0.0f;  // (v2d->flag & V2D_PIXELOFS_X) ? GLA_PIXEL_OFS : 0.0f;
  yofs = 0.0f;  // (v2d->flag & V2D_PIXELOFS_Y) ? GLA_PIXEL_OFS : 0.0f;

  /* apply mask-based adjustments to cur rect (due to scrollers),
   * to eliminate scaling artifacts */
  view2d_map_cur_using_mask(v2d, &curmasked);

  /* only set matrix with 'cur' coordinates on relevant axes */
  if (xaxis) {
    wmOrtho2(curmasked.xmin - xofs, curmasked.xmax - xofs, -yofs, region->winy - yofs);
  }
  else {
    wmOrtho2(-xofs, region->winx - xofs, curmasked.ymin - yofs, curmasked.ymax - yofs);
  }
}

void ui_view2d_view_restore(const bContext *C)
{
  ARegion *region = cxt_wm_region(C);
  const int width = lib_rcti_size_x(&region->winrct) + 1;
  const int height = lib_rcti_size_y(&region->winrct) + 1;

  wmOrtho2(0.0f, (float)width, 0.0f, (float)height);
  gpu_matrix_identity_set();

  //  ed_region_pixelspace(cxt_wm_region(C));
}

/* Grid-Line Drawing */
void ui_view2d_multi_grid_draw(
    const View2D *v2d, int colorid, float step, int level_size, int totlevels)
{
  /* Exit if there is nothing to draw */
  if (totlevels == 0) {
    return;
  }

  int offset = -10;
  float lstep = step;
  uchar grid_line_color[3];

  /* Make an estimate of at least how many vertices will be needed */
  uint vertex_count = 4;
  vertex_count += 2 * ((int)((v2d->cur.xmax - v2d->cur.xmin) / lstep) + 1);
  vertex_count += 2 * ((int)((v2d->cur.ymax - v2d->cur.ymin) / lstep) + 1);

  GPUVertFormat *format = immVertexFormat();
  const uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  uint color = gpu_vertformat_attr_add(
      format, "color", GPU_COMP_U8, 3, GPU_FETCH_INT_TO_FLOAT_UNIT);

  gpu_line_width(1.0f);

  immBindBuiltinProgram(GPU_SHADER_2D_FLAT_COLOR);
  immBeginAtMost(GPU_PRIM_LINES, vertex_count);

  for (int level = 0; level < totlevels; level++) {
    /* Blend the background color (colorid) with the grid color, to avoid either too low contrast
     * or high contrast grid lines. This only has an effect if colorid != TH_GRID. */
    UI_GetThemeColorBlendShade3ubv(colorid, TH_GRID, 0.25f, offset, grid_line_color);

    int i = (int)(v2d->cur.xmin / lstep);
    if (v2d->cur.xmin > 0.0f) {
      i++;
    }
    float start = i * lstep;

    for (; start < v2d->cur.xmax; start += lstep, i++) {
      if (i == 0 || (level < totlevels - 1 && i % level_size == 0)) {
        continue;
      }

      immAttrSkip(color);
      immVertex2f(pos, start, v2d->cur.ymin);
      immAttr3ubv(color, grid_line_color);
      immVertex2f(pos, start, v2d->cur.ymax);
    }

    i = (int)(v2d->cur.ymin / lstep);
    if (v2d->cur.ymin > 0.0f) {
      i++;
    }
    start = i * lstep;

    for (; start < v2d->cur.ymax; start += lstep, i++) {
      if (i == 0 || (level < totlevels - 1 && i % level_size == 0)) {
        continue;
      }

      immAttrSkip(color);
      immVertex2f(pos, v2d->cur.xmin, start);
      immAttr3ubv(color, grid_line_color);
      immVertex2f(pos, v2d->cur.xmax, start);
    }

    lstep *= level_size;
    offset -= 6;
  }

  /* X and Y axis */
  UI_GetThemeColorBlendShade3ubv(
      colorid, TH_GRID, 0.5f, -18 + ((totlevels - 1) * -6), grid_line_color);

  immAttrSkip(color);
  immVertex2f(pos, 0.0f, v2d->cur.ymin);
  immAttr3ubv(color, grid_line_color);
  immVertex2f(pos, 0.0f, v2d->cur.ymax);

  immAttrSkip(color);
  immVertex2f(pos, v2d->cur.xmin, 0.0f);
  immAttr3ubv(color, grid_line_color);
  immVertex2f(pos, v2d->cur.xmax, 0.0f);

  immEnd();
  immUnbindProgram();
}

static void grid_axis_start_and_count(
    const float step, const float min, const float max, float *r_start, int *r_count)
{
  *r_start = min;
  if (*r_start < 0.0f) {
    *r_start += -(float)fmod(min, step);
  }
  else {
    *r_start += step - (float)fabs(fmod(min, step));
  }

  if (*r_start > max) {
    *r_count = 0;
  }
  else {
    *r_count = (max - *r_start) / step + 1;
  }
}

typedef struct DotGridLevelInfo {
  /* The factor applied to the #min_step argument. This could be easily computed in runtime,
   * but seeing it together with the other values is helpful. */
  float step_factor;
  /* The normalized zoom level at which the grid level starts to fade in.
   * At lower zoom levels, the points will not be visible and the level will be skipped. */
  float fade_in_start_zoom;
  /* The normalized zoom level at which the grid finishes fading in.
   * At higher zoom levels, the points will be opaque. */
  float fade_in_end_zoom;
} DotGridLevelInfo;

static const DotGridLevelInfo level_info[9] = {
    {6.4f, -0.1f, 0.01f},
    {3.2f, 0.0f, 0.025f},
    {1.6f, 0.025f, 0.15f},
    {0.8f, 0.05f, 0.2f},
    {0.4f, 0.1f, 0.25f},
    {0.2f, 0.125f, 0.3f},
    {0.1f, 0.25f, 0.5f},
    {0.05f, 0.7f, 0.9f},
    {0.025f, 0.6f, 0.9f},
};

void ui_view2d_dot_grid_draw(const View2D *v2d,
                             const int grid_color_id,
                             const float min_step,
                             const int grid_levels)
{
  lib_assert(grid_levels >= 0 && grid_levels < 10);
  const float zoom_x = (float)(lib_rcti_size_x(&v2d->mask) + 1) / BLI_rctf_size_x(&v2d->cur);
  const float zoom_normalized = (zoom_x - v2d->minzoom) / (v2d->maxzoom - v2d->minzoom);

  GPUVertFormat *format = immVertexFormat();
  const uint pos = gpu_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  const uint color_id = gpu_vertformat_attr_add(format, "color", GPU_COMP_F32, 4, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_FLAT_COLOR);
  gpu_point_size(3.0f * UI_DPI_FAC);

  float color[4];
  ui_GetThemeColor3fv(grid_color_id, color);

  for (int level = 0; level < grid_levels; level++) {
    const DotGridLevelInfo *info = &level_info[level];
    const float step = min_step * info->step_factor * U.widget_unit;

    const float alpha_factor = (zoom_normalized - info->fade_in_start_zoom) /
                               (info->fade_in_end_zoom - info->fade_in_start_zoom);
    color[3] = clamp_f(lib_easing_cubic_ease_in_out(alpha_factor, 0.0f, 1.0f, 1.0f), 0.0f, 1.0f);
    if (color[3] == 0.0f) {
      break;
    }

    int count_x;
    float start_x;
    grid_axis_start_and_count(step, v2d->cur.xmin, v2d->cur.xmax, &start_x, &count_x);
    int count_y;
    float start_y;
    grid_axis_start_and_count(step, v2d->cur.ymin, v2d->cur.ymax, &start_y, &count_y);
    if (count_x == 0 || count_y == 0) {
      continue;
    }

    immBegin(GPU_PRIM_POINTS, count_x * count_y);

    /* Theoretically drawing on top of lower grid levels could be avoided, but it would also
     * increase the complexity of this loop, which isn't worth the time at the moment. */
    for (int i_y = 0; i_y < count_y; i_y++) {
      const float y = start_y + step * i_y;
      for (int i_x = 0; i_x < count_x; i_x++) {
        const float x = start_x + step * i_x;
        immAttr4fv(color_id, color);
        immVertex2f(pos, x, y);
      }
    }

    immEnd();
  }

  immUnbindProgram();
}

/* -------------------------------------------------------------------- */
/* Scrollers */

/* View2DScrollers is typedef'd in UI_view2d.h
 *
 * \warning The start of this struct must not change, as view2d_ops.c uses this too.
 * For now, we don't need to have a separate (internal) header for structs like this... */
struct View2DScrollers {
  /* focus bubbles */
  /* focus bubbles */
  /* focus bubbles */
  int vert_min, vert_max; /* vertical scrollbar */
  int hor_min, hor_max;   /* horizontal scrollbar */

  /** Exact size of slider backdrop. */
  rcti hor, vert;
  /* set if sliders are full, we don't draw them */
  /* int horfull, vertfull; */ /* UNUSED */
};

void ui_view2d_scrollers_calc(View2D *v2d,
                              const rcti *mask_custom,
                              struct View2DScrollers *r_scrollers)
{
  rcti vert, hor;
  float fac1, fac2, totsize, scrollsize;
  const int scroll = view2d_scroll_mapped(v2d->scroll);
  int smaller;

  /* Always update before drawing (for dynamically sized scrollers). */
  view2d_masks(v2d, mask_custom);

  vert = v2d->vert;
  hor = v2d->hor;

  /* slider rects need to be smaller than region and not interfere with splitter areas */
  hor.xmin += UI_HEADER_OFFSET;
  hor.xmax -= UI_HEADER_OFFSET;
  vert.ymin += UI_HEADER_OFFSET;
  vert.ymax -= UI_HEADER_OFFSET;

  /* width of sliders */
  smaller = (int)(0.1f * U.widget_unit);
  if (scroll & V2D_SCROLL_BOTTOM) {
    hor.ymin += smaller;
  }
  else {
    hor.ymax -= smaller;
  }

  if (scroll & V2D_SCROLL_LEFT) {
    vert.xmin += smaller;
  }
  else {
    vert.xmax -= smaller;
  }

  CLAMP_MAX(vert.ymin, vert.ymax - V2D_SCROLL_HANDLE_SIZE_HOTSPOT);
  CLAMP_MAX(hor.xmin, hor.xmax - V2D_SCROLL_HANDLE_SIZE_HOTSPOT);

  /* store in scrollers, used for drawing */
  r_scrollers->vert = vert;
  r_scrollers->hor = hor;

  /* scroller 'btns':
   * - These should always remain within the visible region of the scrollbar
   * - They represent the region of 'tot' that is visible in 'cur' */

  /* horizontal scrollers */
  if (scroll & V2D_SCROLL_HORIZONTAL) {
    /* scroller 'button' extents */
    totsize = lib_rctf_size_x(&v2d->tot);
    scrollsize = (float)BLI_rcti_size_x(&hor);
    if (totsize == 0.0f) {
      totsize = 1.0f; /* avoid divide by zero */
    }

    fac1 = (v2d->cur.xmin - v2d->tot.xmin) / totsize;
    if (fac1 <= 0.0f) {
      r_scrollers->hor_min = hor.xmin;
    }
    else {
      r_scrollers->hor_min = (int)(hor.xmin + (fac1 * scrollsize));
    }

    fac2 = (v2d->cur.xmax - v2d->tot.xmin) / totsize;
    if (fac2 >= 1.0f) {
      r_scrollers->hor_max = hor.xmax;
    }
    else {
      r_scrollers->hor_max = (int)(hor.xmin + (fac2 * scrollsize));
    }

    /* prevent inverted sliders */
    if (r_scrollers->hor_min > r_scrollers->hor_max) {
      r_scrollers->hor_min = r_scrollers->hor_max;
    }
    /* prevent sliders from being too small to grab */
    if ((r_scrollers->hor_max - r_scrollers->hor_min) < V2D_SCROLL_THUMB_SIZE_MIN) {
      r_scrollers->hor_max = r_scrollers->hor_min + V2D_SCROLL_THUMB_SIZE_MIN;

      CLAMP(r_scrollers->hor_max, hor.xmin + V2D_SCROLL_THUMB_SIZE_MIN, hor.xmax);
      CLAMP(r_scrollers->hor_min, hor.xmin, hor.xmax - V2D_SCROLL_THUMB_SIZE_MIN);
    }
  }

  /* vertical scrollers */
  if (scroll & V2D_SCROLL_VERTICAL) {
    /* scroller 'button' extents */
    totsize = BLI_rctf_size_y(&v2d->tot);
    scrollsize = (float)BLI_rcti_size_y(&vert);
    if (totsize == 0.0f) {
      totsize = 1.0f; /* avoid divide by zero */
    }

    fac1 = (v2d->cur.ymin - v2d->tot.ymin) / totsize;
    if (fac1 <= 0.0f) {
      r_scrollers->vert_min = vert.ymin;
    }
    else {
      r_scrollers->vert_min = (int)(vert.ymin + (fac1 * scrollsize));
    }

    fac2 = (v2d->cur.ymax - v2d->tot.ymin) / totsize;
    if (fac2 >= 1.0f) {
      r_scrollers->vert_max = vert.ymax;
    }
    else {
      r_scrollers->vert_max = (int)(vert.ymin + (fac2 * scrollsize));
    }

    /* prevent inverted sliders */
    if (r_scrollers->vert_min > r_scrollers->vert_max) {
      r_scrollers->vert_min = r_scrollers->vert_max;
    }
    /* prevent sliders from being too small to grab */
    if ((r_scrollers->vert_max - r_scrollers->vert_min) < V2D_SCROLL_THUMB_SIZE_MIN) {
      r_scrollers->vert_max = r_scrollers->vert_min + V2D_SCROLL_THUMB_SIZE_MIN;

      CLAMP(r_scrollers->vert_max, vert.ymin + V2D_SCROLL_THUMB_SIZE_MIN, vert.ymax);
      CLAMP(r_scrollers->vert_min, vert.ymin, vert.ymax - V2D_SCROLL_THUMB_SIZE_MIN);
    }
  }
}

void UI_view2d_scrollers_draw(View2D *v2d, const rcti *mask_custom)
{
  View2DScrollers scrollers;
  UI_view2d_scrollers_calc(v2d, mask_custom, &scrollers);
  bTheme *btheme = UI_GetTheme();
  rcti vert, hor;
  const int scroll = view2d_scroll_mapped(v2d->scroll);
  const char emboss_alpha = btheme->tui.widget_emboss[3];
  uchar scrollers_back_color[4];

  /* Color for scrollbar backs */
  UI_GetThemeColor4ubv(TH_BACK, scrollers_back_color);

  /* make copies of rects for less typing */
  vert = scrollers.vert;
  hor = scrollers.hor;

  /* horizontal scrollbar */
  if (scroll & V2D_SCROLL_HORIZONTAL) {
    uiWidgetColors wcol = btheme->tui.wcol_scroll;
    const float alpha_fac = v2d->alpha_hor / 255.0f;
    rcti slider;
    int state;

    slider.xmin = scrollers.hor_min;
    slider.xmax = scrollers.hor_max;
    slider.ymin = hor.ymin;
    slider.ymax = hor.ymax;

    state = (v2d->scroll_ui & V2D_SCROLL_H_ACTIVE) ? UI_SCROLL_PRESSED : 0;

    wcol.inner[3] *= alpha_fac;
    wcol.item[3] *= alpha_fac;
    wcol.outline[3] *= alpha_fac;
    btheme->tui.widget_emboss[3] *= alpha_fac; /* will be reset later */

    /* show zoom handles if:
     * - zooming on x-axis is allowed (no scroll otherwise)
     * - slider bubble is large enough (no overdraw confusion)
     * - scale is shown on the scroller
     *   (workaround to make sure that button windows don't show these,
     *   and only the time-grids with their zoom-ability can do so). */
    if ((v2d->keepzoom & V2D_LOCKZOOM_X) == 0 && (v2d->scroll & V2D_SCROLL_HORIZONTAL_HANDLES) &&
        (BLI_rcti_size_x(&slider) > V2D_SCROLL_HANDLE_SIZE_HOTSPOT)) {
      state |= UI_SCROLL_ARROWS;
    }

    UI_draw_widget_scroll(&wcol, &hor, &slider, state);
  }

  /* vertical scrollbar */
  if (scroll & V2D_SCROLL_VERTICAL) {
    uiWidgetColors wcol = btheme->tui.wcol_scroll;
    rcti slider;
    const float alpha_fac = v2d->alpha_vert / 255.0f;
    int state;

    slider.xmin = vert.xmin;
    slider.xmax = vert.xmax;
    slider.ymin = scrollers.vert_min;
    slider.ymax = scrollers.vert_max;

    state = (v2d->scroll_ui & V2D_SCROLL_V_ACTIVE) ? UI_SCROLL_PRESSED : 0;

    wcol.inner[3] *= alpha_fac;
    wcol.item[3] *= alpha_fac;
    wcol.outline[3] *= alpha_fac;
    btheme->tui.widget_emboss[3] *= alpha_fac; /* will be reset later */

    /* show zoom handles if:
     * - zooming on y-axis is allowed (no scroll otherwise)
     * - slider bubble is large enough (no overdraw confusion)
     * - scale is shown on the scroller
     *   (workaround to make sure that button windows don't show these,
     *   and only the time-grids with their zoomability can do so) */
    if ((v2d->keepzoom & V2D_LOCKZOOM_Y) == 0 && (v2d->scroll & V2D_SCROLL_VERTICAL_HANDLES) &&
        (BLI_rcti_size_y(&slider) > V2D_SCROLL_HANDLE_SIZE_HOTSPOT)) {
      state |= UI_SCROLL_ARROWS;
    }

    UI_draw_widget_scroll(&wcol, &vert, &slider, state);
  }

  /* Was changed above, so reset. */
  btheme->tui.widget_emboss[3] = emboss_alpha;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name List View Utilities
 * \{ */

void UI_view2d_listview_view_to_cell(float columnwidth,
                                     float rowheight,
                                     float startx,
                                     float starty,
                                     float viewx,
                                     float viewy,
                                     int *r_column,
                                     int *r_row)
{
  if (r_column) {
    if (columnwidth > 0) {
      /* Columns go from left to right (x increases). */
      *r_column = floorf((viewx - startx) / columnwidth);
    }
    else {
      *r_column = 0;
    }
  }

  if (r_row) {
    if (rowheight > 0) {
      /* Rows got from top to bottom (y decreases). */
      *r_row = floorf((starty - viewy) / rowheight);
    }
    else {
      *r_row = 0;
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Coordinate Conversions
 * \{ */

float UI_view2d_region_to_view_x(const struct View2D *v2d, float x)
{
  return (v2d->cur.xmin +
          (BLI_rctf_size_x(&v2d->cur) * (x - v2d->mask.xmin) / BLI_rcti_size_x(&v2d->mask)));
}
float UI_view2d_region_to_view_y(const struct View2D *v2d, float y)
{
  return (v2d->cur.ymin +
          (BLI_rctf_size_y(&v2d->cur) * (y - v2d->mask.ymin) / BLI_rcti_size_y(&v2d->mask)));
}

void UI_view2d_region_to_view(
    const View2D *v2d, float x, float y, float *r_view_x, float *r_view_y)
{
  *r_view_x = UI_view2d_region_to_view_x(v2d, x);
  *r_view_y = UI_view2d_region_to_view_y(v2d, y);
}

void UI_view2d_region_to_view_rctf(const View2D *v2d, const rctf *rect_src, rctf *rect_dst)
{
  const float cur_size[2] = {BLI_rctf_size_x(&v2d->cur), BLI_rctf_size_y(&v2d->cur)};
  const float mask_size[2] = {BLI_rcti_size_x(&v2d->mask), BLI_rcti_size_y(&v2d->mask)};

  rect_dst->xmin = (v2d->cur.xmin +
                    (cur_size[0] * (rect_src->xmin - v2d->mask.xmin) / mask_size[0]));
  rect_dst->xmax = (v2d->cur.xmin +
                    (cur_size[0] * (rect_src->xmax - v2d->mask.xmin) / mask_size[0]));
  rect_dst->ymin = (v2d->cur.ymin +
                    (cur_size[1] * (rect_src->ymin - v2d->mask.ymin) / mask_size[1]));
  rect_dst->ymax = (v2d->cur.ymin +
                    (cur_size[1] * (rect_src->ymax - v2d->mask.ymin) / mask_size[1]));
}

float UI_view2d_view_to_region_x(const View2D *v2d, float x)
{
  return (v2d->mask.xmin +
          (((x - v2d->cur.xmin) / BLI_rctf_size_x(&v2d->cur)) * BLI_rcti_size_x(&v2d->mask)));
}
float UI_view2d_view_to_region_y(const View2D *v2d, float y)
{
  return (v2d->mask.ymin +
          (((y - v2d->cur.ymin) / BLI_rctf_size_y(&v2d->cur)) * BLI_rcti_size_y(&v2d->mask)));
}

bool UI_view2d_view_to_region_clip(
    const View2D *v2d, float x, float y, int *r_region_x, int *r_region_y)
{
  /* express given coordinates as proportional values */
  x = (x - v2d->cur.xmin) / BLI_rctf_size_x(&v2d->cur);
  y = (y - v2d->cur.ymin) / BLI_rctf_size_y(&v2d->cur);

  /* check if values are within bounds */
  if ((x >= 0.0f) && (x <= 1.0f) && (y >= 0.0f) && (y <= 1.0f)) {
    *r_region_x = (int)(v2d->mask.xmin + (x * BLI_rcti_size_x(&v2d->mask)));
    *r_region_y = (int)(v2d->mask.ymin + (y * BLI_rcti_size_y(&v2d->mask)));

    return true;
  }

  /* set initial value in case coordinate lies outside of bounds */
  *r_region_x = *r_region_y = V2D_IS_CLIPPED;

  return false;
}

void UI_view2d_view_to_region(
    const View2D *v2d, float x, float y, int *r_region_x, int *r_region_y)
{
  /* Step 1: express given coordinates as proportional values. */
  x = (x - v2d->cur.xmin) / BLI_rctf_size_x(&v2d->cur);
  y = (y - v2d->cur.ymin) / BLI_rctf_size_y(&v2d->cur);

  /* Step 2: convert proportional distances to screen coordinates. */
  x = v2d->mask.xmin + (x * BLI_rcti_size_x(&v2d->mask));
  y = v2d->mask.ymin + (y * BLI_rcti_size_y(&v2d->mask));

  /* Although we don't clamp to lie within region bounds, we must avoid exceeding size of ints. */
  *r_region_x = clamp_float_to_int(x);
  *r_region_y = clamp_float_to_int(y);
}

void UI_view2d_view_to_region_fl(
    const View2D *v2d, float x, float y, float *r_region_x, float *r_region_y)
{
  /* express given coordinates as proportional values */
  x = (x - v2d->cur.xmin) / BLI_rctf_size_x(&v2d->cur);
  y = (y - v2d->cur.ymin) / BLI_rctf_size_y(&v2d->cur);

  /* convert proportional distances to screen coordinates */
  *r_region_x = v2d->mask.xmin + (x * BLI_rcti_size_x(&v2d->mask));
  *r_region_y = v2d->mask.ymin + (y * BLI_rcti_size_y(&v2d->mask));
}

void UI_view2d_view_to_region_rcti(const View2D *v2d, const rctf *rect_src, rcti *rect_dst)
{
  const float cur_size[2] = {BLI_rctf_size_x(&v2d->cur), BLI_rctf_size_y(&v2d->cur)};
  const float mask_size[2] = {BLI_rcti_size_x(&v2d->mask), BLI_rcti_size_y(&v2d->mask)};
  rctf rect_tmp;

  /* Step 1: express given coordinates as proportional values. */
  rect_tmp.xmin = (rect_src->xmin - v2d->cur.xmin) / cur_size[0];
  rect_tmp.xmax = (rect_src->xmax - v2d->cur.xmin) / cur_size[0];
  rect_tmp.ymin = (rect_src->ymin - v2d->cur.ymin) / cur_size[1];
  rect_tmp.ymax = (rect_src->ymax - v2d->cur.ymin) / cur_size[1];

  /* Step 2: convert proportional distances to screen coordinates. */
  rect_tmp.xmin = v2d->mask.xmin + (rect_tmp.xmin * mask_size[0]);
  rect_tmp.xmax = v2d->mask.xmin + (rect_tmp.xmax * mask_size[0]);
  rect_tmp.ymin = v2d->mask.ymin + (rect_tmp.ymin * mask_size[1]);
  rect_tmp.ymax = v2d->mask.ymin + (rect_tmp.ymax * mask_size[1]);

  clamp_rctf_to_rcti(rect_dst, &rect_tmp);
}

void UI_view2d_view_to_region_m4(const View2D *v2d, float matrix[4][4])
{
  rctf mask;
  unit_m4(matrix);
  BLI_rctf_rcti_copy(&mask, &v2d->mask);
  BLI_rctf_transform_calc_m4_pivot_min(&v2d->cur, &mask, matrix);
}

bool UI_view2d_view_to_region_rcti_clip(const View2D *v2d, const rctf *rect_src, rcti *rect_dst)
{
  const float cur_size[2] = {BLI_rctf_size_x(&v2d->cur), BLI_rctf_size_y(&v2d->cur)};
  const float mask_size[2] = {BLI_rcti_size_x(&v2d->mask), BLI_rcti_size_y(&v2d->mask)};
  rctf rect_tmp;

  BLI_assert(rect_src->xmin <= rect_src->xmax && rect_src->ymin <= rect_src->ymax);

  /* Step 1: express given coordinates as proportional values. */
  rect_tmp.xmin = (rect_src->xmin - v2d->cur.xmin) / cur_size[0];
  rect_tmp.xmax = (rect_src->xmax - v2d->cur.xmin) / cur_size[0];
  rect_tmp.ymin = (rect_src->ymin - v2d->cur.ymin) / cur_size[1];
  rect_tmp.ymax = (rect_src->ymax - v2d->cur.ymin) / cur_size[1];

  if (((rect_tmp.xmax < 0.0f) || (rect_tmp.xmin > 1.0f) || (rect_tmp.ymax < 0.0f) ||
       (rect_tmp.ymin > 1.0f)) == 0) {
    /* Step 2: convert proportional distances to screen coordinates. */
    rect_tmp.xmin = v2d->mask.xmin + (rect_tmp.xmin * mask_size[0]);
    rect_tmp.xmax = v2d->mask.ymin + (rect_tmp.xmax * mask_size[0]);
    rect_tmp.ymin = v2d->mask.ymin + (rect_tmp.ymin * mask_size[1]);
    rect_tmp.ymax = v2d->mask.ymin + (rect_tmp.ymax * mask_size[1]);

    clamp_rctf_to_rcti(rect_dst, &rect_tmp);

    return true;
  }

  rect_dst->xmin = rect_dst->xmax = rect_dst->ymin = rect_dst->ymax = V2D_IS_CLIPPED;
  return false;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Utilities
 * \{ */

View2D *UI_view2d_fromcontext(const bContext *C)
{
  ScrArea *area = CTX_wm_area(C);
  ARegion *region = CTX_wm_region(C);

  if (area == NULL) {
    return NULL;
  }
  if (region == NULL) {
    return NULL;
  }
  return &(region->v2d);
}

View2D *UI_view2d_fromcontext_rwin(const bContext *C)
{
  ScrArea *area = CTX_wm_area(C);
  ARegion *region = CTX_wm_region(C);

  if (area == NULL) {
    return NULL;
  }
  if (region == NULL) {
    return NULL;
  }
  if (region->regiontype != RGN_TYPE_WINDOW) {
    ARegion *region_win = BKE_area_find_region_type(area, RGN_TYPE_WINDOW);
    return region_win ? &(region_win->v2d) : NULL;
  }
  return &(region->v2d);
}

void UI_view2d_scroller_size_get(const View2D *v2d, float *r_x, float *r_y)
{
  const int scroll = view2d_scroll_mapped(v2d->scroll);

  if (r_x) {
    if (scroll & V2D_SCROLL_VERTICAL) {
      *r_x = (scroll & V2D_SCROLL_VERTICAL_HANDLES) ? V2D_SCROLL_HANDLE_WIDTH : V2D_SCROLL_WIDTH;
    }
    else {
      *r_x = 0;
    }
  }
  if (r_y) {
    if (scroll & V2D_SCROLL_HORIZONTAL) {
      *r_y = (scroll & V2D_SCROLL_HORIZONTAL_HANDLES) ? V2D_SCROLL_HANDLE_HEIGHT :
                                                        V2D_SCROLL_HEIGHT;
    }
    else {
      *r_y = 0;
    }
  }
}

void UI_view2d_scale_get(const View2D *v2d, float *r_x, float *r_y)
{
  if (r_x) {
    *r_x = UI_view2d_scale_get_x(v2d);
  }
  if (r_y) {
    *r_y = UI_view2d_scale_get_y(v2d);
  }
}
float UI_view2d_scale_get_x(const View2D *v2d)
{
  return BLI_rcti_size_x(&v2d->mask) / BLI_rctf_size_x(&v2d->cur);
}
float UI_view2d_scale_get_y(const View2D *v2d)
{
  return BLI_rcti_size_y(&v2d->mask) / BLI_rctf_size_y(&v2d->cur);
}
void UI_view2d_scale_get_inverse(const View2D *v2d, float *r_x, float *r_y)
{
  if (r_x) {
    *r_x = BLI_rctf_size_x(&v2d->cur) / BLI_rcti_size_x(&v2d->mask);
  }
  if (r_y) {
    *r_y = BLI_rctf_size_y(&v2d->cur) / BLI_rcti_size_y(&v2d->mask);
  }
}

void UI_view2d_center_get(const struct View2D *v2d, float *r_x, float *r_y)
{
  /* get center */
  if (r_x) {
    *r_x = BLI_rctf_cent_x(&v2d->cur);
  }
  if (r_y) {
    *r_y = BLI_rctf_cent_y(&v2d->cur);
  }
}
void UI_view2d_center_set(struct View2D *v2d, float x, float y)
{
  BLI_rctf_recenter(&v2d->cur, x, y);

  /* make sure that 'cur' rect is in a valid state as a result of these changes */
  UI_view2d_curRect_validate(v2d);
}

void UI_view2d_offset(struct View2D *v2d, float xfac, float yfac)
{
  if (xfac != -1.0f) {
    const float xsize = BLI_rctf_size_x(&v2d->cur);
    const float xmin = v2d->tot.xmin;
    const float xmax = v2d->tot.xmax - xsize;

    v2d->cur.xmin = (xmin * (1.0f - xfac)) + (xmax * xfac);
    v2d->cur.xmax = v2d->cur.xmin + xsize;
  }

  if (yfac != -1.0f) {
    const float ysize = BLI_rctf_size_y(&v2d->cur);
    const float ymin = v2d->tot.ymin;
    const float ymax = v2d->tot.ymax - ysize;

    v2d->cur.ymin = (ymin * (1.0f - yfac)) + (ymax * yfac);
    v2d->cur.ymax = v2d->cur.ymin + ysize;
  }

  UI_view2d_curRect_validate(v2d);
}

char UI_view2d_mouse_in_scrollers_ex(const ARegion *region,
                                     const View2D *v2d,
                                     const int xy[2],
                                     int *r_scroll)
{
  const int scroll = view2d_scroll_mapped(v2d->scroll);
  *r_scroll = scroll;

  if (scroll) {
    /* Move to region-coordinates. */
    const int co[2] = {
        xy[0] - region->winrct.xmin,
        xy[1] - region->winrct.ymin,
    };
    if (scroll & V2D_SCROLL_HORIZONTAL) {
      if (IN_2D_HORIZ_SCROLL(v2d, co)) {
        return 'h';
      }
    }
    if (scroll & V2D_SCROLL_VERTICAL) {
      if (IN_2D_VERT_SCROLL(v2d, co)) {
        return 'v';
      }
    }
  }

  return 0;
}

char UI_view2d_rect_in_scrollers_ex(const ARegion *region,
                                    const View2D *v2d,
                                    const rcti *rect,
                                    int *r_scroll)
{
  const int scroll = view2d_scroll_mapped(v2d->scroll);
  *r_scroll = scroll;

  if (scroll) {
    /* Move to region-coordinates. */
    rcti rect_region = *rect;
    BLI_rcti_translate(&rect_region, -region->winrct.xmin, region->winrct.ymin);
    if (scroll & V2D_SCROLL_HORIZONTAL) {
      if (IN_2D_HORIZ_SCROLL_RECT(v2d, &rect_region)) {
        return 'h';
      }
    }
    if (scroll & V2D_SCROLL_VERTICAL) {
      if (IN_2D_VERT_SCROLL_RECT(v2d, &rect_region)) {
        return 'v';
      }
    }
  }

  return 0;
}

char UI_view2d_mouse_in_scrollers(const ARegion *region, const View2D *v2d, const int xy[2])
{
  int scroll_dummy = 0;
  return UI_view2d_mouse_in_scrollers_ex(region, v2d, xy, &scroll_dummy);
}

char UI_view2d_rect_in_scrollers(const ARegion *region, const View2D *v2d, const rcti *rect)
{
  int scroll_dummy = 0;
  return UI_view2d_rect_in_scrollers_ex(region, v2d, rect, &scroll_dummy);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name View2D Text Drawing Cache
 * \{ */

typedef struct View2DString {
  struct View2DString *next;
  union {
    uchar ub[4];
    int pack;
  } col;
  rcti rect;
  int mval[2];

  /* str is allocated past the end */
  char str[0];
} View2DString;

/* assumes caches are used correctly, so for time being no local storage in v2d */
static MemArena *g_v2d_strings_arena = NULL;
static View2DString *g_v2d_strings = NULL;

void UI_view2d_text_cache_add(
    View2D *v2d, float x, float y, const char *str, size_t str_len, const uchar col[4])
{
  int mval[2];

  BLI_assert(str_len == strlen(str));

  if (UI_view2d_view_to_region_clip(v2d, x, y, &mval[0], &mval[1])) {
    const int alloc_len = str_len + 1;
    View2DString *v2s;

    if (g_v2d_strings_arena == NULL) {
      g_v2d_strings_arena = BLI_memarena_new(MEM_SIZE_OPTIMAL(1 << 14), __func__);
    }

    v2s = BLI_memarena_alloc(g_v2d_strings_arena, sizeof(View2DString) + alloc_len);

    BLI_LINKS_PREPEND(g_v2d_strings, v2s);

    v2s->col.pack = *((const int *)col);

    memset(&v2s->rect, 0, sizeof(v2s->rect));

    v2s->mval[0] = mval[0];
    v2s->mval[1] = mval[1];

    memcpy(v2s->str, str, alloc_len);
  }
}

void view2d_text_cache_add_rectf(
    View2D *v2d, const rctf *rect_view, const char *str, size_t str_len, const uchar col[4])
{
  rcti rect;

  BLI_assert(str_len == strlen(str));

  if (view2d_view_to_region_rcti_clip(v2d, rect_view, &rect)) {
    const int alloc_len = str_len + 1;
    View2DString *v2s;

    if (g_v2d_strings_arena == NULL) {
      g_v2d_strings_arena = LIB_memarena_new(MEM_SIZE_OPTIMAL(1 << 14), __func__);
    }

    v2s = LIB_memarena_alloc(g_v2d_strings_arena, sizeof(View2DString) + alloc_len);

    LIB_LINKS_PREPEND(g_v2d_strings, v2s);

    v2s->col.pack = *((const int *)col);

    v2s->rect = rect;

    v2s->mval[0] = v2s->rect.xmin;
    v2s->mval[1] = v2s->rect.ymin;

    memcpy(v2s->str, str, alloc_len);
  }
}

void view2d_txt_cache_draw(ARegion *region)
{
  View2DString *v2s;
  int col_pack_prev = 0;

  /* investigate using BLF_ascender() */
  const int font_id = BLF_default();

  BLF_set_default();
  const float default_height = g_v2d_strings ? BLF_height(font_id, "28", 3) : 0.0f;

  wmOrtho2_region_pixelspace(region);

  for (v2s = g_v2d_strings; v2s; v2s = v2s->next) {
    int xofs = 0, yofs;

    yofs = ceil(0.5f * (BLI_rcti_size_y(&v2s->rect) - default_height));
    if (yofs < 1) {
      yofs = 1;
    }

    if (col_pack_prev != v2s->col.pack) {
      BLF_color3ubv(font_id, v2s->col.ub);
      col_pack_prev = v2s->col.pack;
    }

    if (v2s->rect.xmin >= v2s->rect.xmax) {
      BLF_draw_default((float)(v2s->mval[0] + xofs),
                       (float)(v2s->mval[1] + yofs),
                       0.0,
                       v2s->str,
                       BLF_DRAW_STR_DUMMY_MAX);
    }
    else {
      BLF_enable(font_id, BLF_CLIPPING);
      BLF_clipping(
          font_id, v2s->rect.xmin - 4, v2s->rect.ymin - 4, v2s->rect.xmax + 4, v2s->rect.ymax + 4);
      BLF_draw_default(
          v2s->rect.xmin + xofs, v2s->rect.ymin + yofs, 0.0f, v2s->str, BLF_DRAW_STR_DUMMY_MAX);
      BLF_disable(font_id, BLF_CLIPPING);
    }
  }
  g_v2d_strings = NULL;

  if (g_v2d_strings_arena) {
    LIB_memarena_free(g_v2d_strings_arena);
    g_v2d_strings_arena = NULL;
  }
}
