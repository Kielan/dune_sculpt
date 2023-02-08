/* a full doc with API notes can be found in
 * bf-blender/trunk/blender/doc/guides/interface_API.txt */

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "PIL_time.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_screen_types.h"
#include "DNA_userdef_types.h"

#include "BKE_context.h"
#include "BKE_screen.h"

#include "RNA_access.h"

#include "BLF_api.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_screen.h"

#include "UI_interface.h"
#include "UI_interface_icons.h"
#include "UI_resources.h"
#include "UI_view2d.h"

#include "GPU_batch_presets.h"
#include "GPU_immediate.h"
#include "GPU_matrix.h"
#include "GPU_state.h"

#include "interface_intern.h"

/* -------------------------------------------------------------------- */
/** \name Defines & Structs
 * \{ */

#define ANIMATION_TIME 0.30
#define ANIMATION_INTERVAL 0.02

typedef enum uiPanelRuntimeFlag {
  PANEL_LAST_ADDED = (1 << 0),
  PANEL_ACTIVE = (1 << 2),
  PANEL_WAS_ACTIVE = (1 << 3),
  PANEL_ANIM_ALIGN = (1 << 4),
  PANEL_NEW_ADDED = (1 << 5),
  PANEL_SEARCH_FILTER_MATCH = (1 << 7),
  /**
   * Use the status set by property search (#PANEL_SEARCH_FILTER_MATCH)
   * instead of #PNL_CLOSED. Set to true on every property search update.
   */
  PANEL_USE_CLOSED_FROM_SEARCH = (1 << 8),
  /** The Panel was before the start of the current / latest layout pass. */
  PANEL_WAS_CLOSED = (1 << 9),
  /**
   * Set when the panel is being dragged and while it animates back to its aligned
   * position. Unlike #PANEL_STATE_ANIMATION, this is applied to sub-panels as well.
   */
  PANEL_IS_DRAG_DROP = (1 << 10),
  /** Draw a border with the active color around the panel. */
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

static void panel_set_expansion_from_list_data(const bContext *C, Panel *panel);
static int get_panel_real_size_y(const Panel *panel);
static void panel_activate_state(const bContext *C, Panel *panel, uiHandlePanelState state);
static int compare_panel(const void *a, const void *b);
static bool panel_type_context_poll(ARegion *region,
                                    const PanelType *panel_type,
                                    const char *context);

/** \} */

/* -------------------------------------------------------------------- */
/** \name Local Functions
 * \{ */

static bool panel_active_animation_changed(ListBase *lb,
                                           Panel **r_panel_animation,
                                           bool *r_no_animation)
{
  LISTBASE_FOREACH (Panel *, panel, lb) {
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
    if ((bool)(panel->runtime_flag & PANEL_WAS_CLOSED) != UI_panel_is_closed(panel)) {
      *r_panel_animation = panel;
      return false;
    }

    if ((panel->runtime_flag & PANEL_ACTIVE) && !UI_panel_is_closed(panel)) {
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

/**
 * \return True if the properties editor switch tabs since the last layout pass.
 */
static bool properties_space_needs_realign(const ScrArea *area, const ARegion *region)
{
  if (area->spacetype == SPACE_PROPERTIES && region->regiontype == RGN_TYPE_WINDOW) {
    SpaceProperties *sbuts = area->spacedata.first;

    if (sbuts->mainbo != sbuts->mainb) {
      return true;
    }
  }

  return false;
}

static bool panels_need_realign(const ScrArea *area, ARegion *region, Panel **r_panel_animation)
{
  *r_panel_animation = NULL;

  if (properties_space_needs_realign(area, region)) {
    return true;
  }

  /* Detect if a panel was added or removed. */
  Panel *panel_animation = NULL;
  bool no_animation = false;
  if (panel_active_animation_changed(&region->panels, &panel_animation, &no_animation)) {
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

/** \} */

/* -------------------------------------------------------------------- */
/** \name Functions for Instanced Panels
 * \{ */

static Panel *panel_add_instanced(ARegion *region,
                                  ListBase *panels,
                                  PanelType *panel_type,
                                  PointerRNA *custom_data)
{
  Panel *panel = MEM_callocN(sizeof(Panel), __func__);
  panel->type = panel_type;
  BLI_strncpy(panel->panelname, panel_type->idname, sizeof(panel->panelname));

  panel->runtime.custom_data_ptr = custom_data;
  panel->runtime_flag |= PANEL_NEW_ADDED;

  /* Add the panel's children too. Although they aren't instanced panels, we can still use this
   * function to create them, as UI_panel_begin does other things we don't need to do. */
  LISTBASE_FOREACH (LinkData *, child, &panel_type->children) {
    PanelType *child_type = child->data;
    panel_add_instanced(region, &panel->children, child_type, custom_data);
  }

  /* Make sure the panel is added to the end of the display-order as well. This is needed for
   * loading existing files.
   *
   * NOTE: We could use special behavior to place it after the panel that starts the list of
   * instanced panels, but that would add complexity that isn't needed for now. */
  int max_sortorder = 0;
  LISTBASE_FOREACH (Panel *, existing_panel, panels) {
    if (existing_panel->sortorder > max_sortorder) {
      max_sortorder = existing_panel->sortorder;
    }
  }
  panel->sortorder = max_sortorder + 1;

  BLI_addtail(panels, panel);

  return panel;
}

Panel *UI_panel_add_instanced(const bContext *C,
                              ARegion *region,
                              ListBase *panels,
                              const char *panel_idname,
                              PointerRNA *custom_data)
{
  ARegionType *region_type = region->type;

  PanelType *panel_type = BLI_findstring(
      &region_type->paneltypes, panel_idname, offsetof(PanelType, idname));

  if (panel_type == NULL) {
    printf("Panel type '%s' not found.\n", panel_idname);
    return NULL;
  }

  Panel *new_panel = panel_add_instanced(region, panels, panel_type, custom_data);

  /* Do this after #panel_add_instatnced so all sub-panels are added. */
  panel_set_expansion_from_list_data(C, new_panel);

  return new_panel;
}

void UI_list_panel_unique_str(Panel *panel, char *r_name)
{
  /* The panel sort-order will be unique for a specific panel type because the instanced
   * panel list is regenerated for every change in the data order / length. */
  snprintf(r_name, INSTANCED_PANEL_UNIQUE_STR_LEN, "%d", panel->sortorder);
}

/**
 * Free a panel and its children. Custom data is shared by the panel and its children
 * and is freed by #UI_panels_free_instanced.
 *
 * \note The only panels that should need to be deleted at runtime are panels with the
 * #PANEL_TYPE_INSTANCED flag set.
 */
static void panel_delete(const bContext *C, ARegion *region, ListBase *panels, Panel *panel)
{
  /* Recursively delete children. */
  LISTBASE_FOREACH_MUTABLE (Panel *, child, &panel->children) {
    panel_delete(C, region, &panel->children, child);
  }
  BLI_freelistN(&panel->children);

  BLI_remlink(panels, panel);
  if (panel->activedata) {
    MEM_freeN(panel->activedata);
  }
  MEM_freeN(panel);
}

void UI_panels_free_instanced(const bContext *C, ARegion *region)
{
  /* Delete panels with the instanced flag. */
  LISTBASE_FOREACH_MUTABLE (Panel *, panel, &region->panels) {
    if ((panel->type != NULL) && (panel->type->flag & PANEL_TYPE_INSTANCED)) {
      /* Make sure the panel's handler is removed before deleting it. */
      if (C != NULL && panel->activedata != NULL) {
        panel_activate_state(C, panel, PANEL_STATE_EXIT);
      }

      /* Free panel's custom data. */
      if (panel->runtime.custom_data_ptr != NULL) {
        MEM_freeN(panel->runtime.custom_data_ptr);
      }

      /* Free the panel and its sub-panels. */
      panel_delete(C, region, &region->panels, panel);
    }
  }
}

bool UI_panel_list_matches_data(ARegion *region,
                                ListBase *data,
                                uiListPanelIDFromDataFunc panel_idname_func)
{
  /* Check for NULL data. */
  int data_len = 0;
  Link *data_link = NULL;
  if (data == NULL) {
    data_len = 0;
    data_link = NULL;
  }
  else {
    data_len = BLI_listbase_count(data);
    data_link = data->first;
  }

  int i = 0;
  LISTBASE_FOREACH (Panel *, panel, &region->panels) {
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
      panel_idname_func(data_link, panel_idname);
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

static void reorder_instanced_panel_list(bContext *C, ARegion *region, Panel *drag_panel)
{
  /* Without a type we cannot access the reorder callback. */
  if (drag_panel->type == NULL) {
    return;
  }
  /* Don't reorder if this instanced panel doesn't support drag and drop reordering. */
  if (drag_panel->type->reorder == NULL) {
    return;
  }

  char *context = NULL;
  if (!UI_panel_category_is_visible(region)) {
    context = drag_panel->type->context;
  }

  /* Find how many instanced panels with this context string. */
  int list_panels_len = 0;
  int start_index = -1;
  LISTBASE_FOREACH (const Panel *, panel, &region->panels) {
    if (panel->type) {
      if (panel->type->flag & PANEL_TYPE_INSTANCED) {
        if (panel_type_context_poll(region, panel->type, context)) {
          if (panel == drag_panel) {
            BLI_assert(start_index == -1); /* This panel should only appear once. */
            start_index = list_panels_len;
          }
          list_panels_len++;
        }
      }
    }
  }
  BLI_assert(start_index != -1); /* The drag panel should definitely be in the list. */

  /* Sort the matching instanced panels by their display order. */
  PanelSort *panel_sort = MEM_callocN(list_panels_len * sizeof(*panel_sort), __func__);
  PanelSort *sort_index = panel_sort;
  LISTBASE_FOREACH (Panel *, panel, &region->panels) {
    if (panel->type) {
      if (panel->type->flag & PANEL_TYPE_INSTANCED) {
        if (panel_type_context_poll(region, panel->type, context)) {
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

  MEM_freeN(panel_sort);

  if (move_to_index == start_index) {
    /* In this case, the reorder was not changed, so don't do any updates or call the callback. */
    return;
  }

  /* Set the bit to tell the interface to instanced the list. */
  drag_panel->flag |= PNL_INSTANCED_LIST_ORDER_CHANGED;

  CTX_store_set(C, drag_panel->runtime.context);

  /* Finally, move this panel's list item to the new index in its list. */
  drag_panel->type->reorder(C, drag_panel, move_to_index);

  CTX_store_set(C, NULL);
}

/**
 * Recursive implementation for #panel_set_expansion_from_list_data.
 *
 * \return Whether the closed flag for the panel or any sub-panels changed.
 */
static bool panel_set_expand_from_list_data_recursive(Panel *panel, short flag, short *flag_index)
{
  const bool open = (flag & (1 << *flag_index));
  bool changed = (open == UI_panel_is_closed(panel));

  SET_FLAG_FROM_TEST(panel->flag, !open, PNL_CLOSED);

  LISTBASE_FOREACH (Panel *, child, &panel->children) {
    *flag_index = *flag_index + 1;
    changed |= panel_set_expand_from_list_data_recursive(child, flag, flag_index);
  }
  return changed;
}

/**
 * Set the expansion of the panel and its sub-panels from the flag stored in the
 * corresponding list data. The flag has expansion stored in each bit in depth first order.
 */
static void panel_set_expansion_from_list_data(const bContext *C, Panel *panel)
{
  BLI_assert(panel->type != NULL);
  BLI_assert(panel->type->flag & PANEL_TYPE_INSTANCED);
  if (panel->type->get_list_data_expand_flag == NULL) {
    /* Instanced panel doesn't support loading expansion. */
    return;
  }

  const short expand_flag = panel->type->get_list_data_expand_flag(C, panel);
  short flag_index = 0;

  /* Start panel animation if the open state was changed. */
  if (panel_set_expand_from_list_data_recursive(panel, expand_flag, &flag_index)) {
    panel_activate_state(C, panel, PANEL_STATE_ANIMATION);
  }
}

/**
 * Set expansion based on the data for instanced panels.
 */
static void region_panels_set_expansion_from_list_data(const bContext *C, ARegion *region)
{
  LISTBASE_FOREACH (Panel *, panel, &region->panels) {
    if (panel->runtime_flag & PANEL_ACTIVE) {
      PanelType *panel_type = panel->type;
      if (panel_type != NULL && panel->type->flag & PANEL_TYPE_INSTANCED) {
        panel_set_expansion_from_list_data(C, panel);
      }
    }
  }
}

/**
 * Recursive implementation for #set_panels_list_data_expand_flag.
 */
static void get_panel_expand_flag(const Panel *panel, short *flag, short *flag_index)
{
  const bool open = !(panel->flag & PNL_CLOSED);
  SET_FLAG_FROM_TEST(*flag, open, (1 << *flag_index));

  LISTBASE_FOREACH (const Panel *, child, &panel->children) {
    *flag_index = *flag_index + 1;
    get_panel_expand_flag(child, flag, flag_index);
  }
}

/**
 * Call the callback to store the panel and sub-panel expansion settings in the list item that
 * corresponds to each instanced panel.
 *
 * \note This needs to iterate through all of the region's panels because the panel with changed
 * expansion might have been the sub-panel of an instanced panel, meaning it might not know
 * which list item it corresponds to.
 */
static void set_panels_list_data_expand_flag(const bContext *C, const ARegion *region)
{
  LISTBASE_FOREACH (Panel *, panel, &region->panels) {
    PanelType *panel_type = panel->type;
    if (panel_type == NULL) {
      continue;
    }

    /* Check for #PANEL_ACTIVE so we only set the expand flag for active panels. */
    if (panel_type->flag & PANEL_TYPE_INSTANCED && panel->runtime_flag & PANEL_ACTIVE) {
      short expand_flag;
      short flag_index = 0;
      get_panel_expand_flag(panel, &expand_flag, &flag_index);
      if (panel->type->set_list_data_expand_flag) {
        panel->type->set_list_data_expand_flag(C, panel, expand_flag);
      }
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Panels
 * \{ */

static bool panel_custom_data_active_get(const Panel *panel)
{
  /* The caller should make sure the panel is active and has a type. */
  BLI_assert(UI_panel_is_active(panel));
  BLI_assert(panel->type != NULL);

  if (panel->type->active_property[0] != '\0') {
    PointerRNA *ptr = UI_panel_custom_data_get(panel);
    if (ptr != NULL && !RNA_pointer_is_null(ptr)) {
      return RNA_boolean_get(ptr, panel->type->active_property);
    }
  }

  return false;
}

static void panel_custom_data_active_set(Panel *panel)
{
  /* Since the panel is interacted with, it should be active and have a type. */
  BLI_assert(UI_panel_is_active(panel));
  BLI_assert(panel->type != NULL);

  if (panel->type->active_property[0] != '\0') {
    PointerRNA *ptr = UI_panel_custom_data_get(panel);
    BLI_assert(RNA_struct_find_property(ptr, panel->type->active_property) != NULL);
    if (ptr != NULL && !RNA_pointer_is_null(ptr)) {
      RNA_boolean_set(ptr, panel->type->active_property, true);
    }
  }
}

/**
 * Set flag state for a panel and its sub-panels.
 */
static void panel_set_flag_recursive(Panel *panel, short flag, bool value)
{
  SET_FLAG_FROM_TEST(panel->flag, value, flag);

  LISTBASE_FOREACH (Panel *, child, &panel->children) {
    panel_set_flag_recursive(child, flag, value);
  }
}

/**
 * Set runtime flag state for a panel and its sub-panels.
 */
static void panel_set_runtime_flag_recursive(Panel *panel, short flag, bool value)
{
  SET_FLAG_FROM_TEST(panel->runtime_flag, value, flag);

  LISTBASE_FOREACH (Panel *, sub_panel, &panel->children) {
    panel_set_runtime_flag_recursive(sub_panel, flag, value);
  }
}

static void panels_collapse_all(ARegion *region, const Panel *from_panel)
{
  const bool has_category_tabs = UI_panel_category_is_visible(region);
  const char *category = has_category_tabs ? UI_panel_category_active_get(region, false) : NULL;
  const PanelType *from_pt = from_panel->type;

  LISTBASE_FOREACH (Panel *, panel, &region->panels) {
    PanelType *pt = panel->type;

    /* Close panels with headers in the same context. */
    if (pt && from_pt && !(pt->flag & PANEL_TYPE_NO_HEADER)) {
      if (!pt->context[0] || !from_pt->context[0] || STREQ(pt->context, from_pt->context)) {
        if ((panel->flag & PNL_PIN) || !category || !pt->category[0] ||
            STREQ(pt->category, category)) {
          panel->flag |= PNL_CLOSED;
        }
      }
    }
  }
}

static bool panel_type_context_poll(ARegion *region,
                                    const PanelType *panel_type,
                                    const char *context)
{
  if (!BLI_listbase_is_empty(&region->panels_category)) {
    return STREQ(panel_type->category, UI_panel_category_active_get(region, false));
  }

  if (panel_type->context[0] && STREQ(panel_type->context, context)) {
    return true;
  }

  return false;
}

Panel *UI_panel_find_by_type(ListBase *lb, const PanelType *pt)
{
  const char *idname = pt->idname;

  LISTBASE_FOREACH (Panel *, panel, lb) {
    if (STREQLEN(panel->panelname, idname, sizeof(panel->panelname))) {
      return panel;
    }
  }
  return NULL;
}

Panel *UI_panel_begin(
    ARegion *region, ListBase *lb, uiBlock *block, PanelType *pt, Panel *panel, bool *r_open)
{
  Panel *panel_last;
  const char *drawname = CTX_IFACE_(pt->translation_context, pt->label);
  const char *idname = pt->idname;
  const bool newpanel = (panel == NULL);

  if (newpanel) {
    panel = MEM_callocN(sizeof(Panel), __func__);
    panel->type = pt;
    BLI_strncpy(panel->panelname, idname, sizeof(panel->panelname));

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

    BLI_addtail(lb, panel);
  }
  else {
    /* Panel already exists. */
    panel->type = pt;
  }

  panel->runtime.block = block;

  BLI_strncpy(panel->drawname, drawname, sizeof(panel->drawname));

  /* If a new panel is added, we insert it right after the panel that was last added.
   * This way new panels are inserted in the right place between versions. */
  for (panel_last = lb->first; panel_last; panel_last = panel_last->next) {
    if (panel_last->runtime_flag & PANEL_LAST_ADDED) {
      BLI_remlink(lb, panel);
      BLI_insertlinkafter(lb, panel_last, panel);
      break;
    }
  }

  if (newpanel) {
    panel->sortorder = (panel_last) ? panel_last->sortorder + 1 : 0;

    LISTBASE_FOREACH (Panel *, panel_next, lb) {
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
    UI_block_theme_style_set(block, UI_BLOCK_THEME_STYLE_POPUP);
  }

  *r_open = false;

  if (UI_panel_is_closed(panel)) {
    return panel;
  }

  *r_open = true;

  return panel;
}

void UI_panel_header_buttons_begin(Panel *panel)
{
  uiBlock *block = panel->runtime.block;

  ui_block_new_button_group(block, UI_BUTTON_GROUP_LOCK | UI_BUTTON_GROUP_PANEL_HEADER);
}

void UI_panel_header_buttons_end(Panel *panel)
{
  uiBlock *block = panel->runtime.block;

  /* A button group should always be created in #UI_panel_header_buttons_begin. */
  BLI_assert(!BLI_listbase_is_empty(&block->button_groups));

  uiButtonGroup *button_group = block->button_groups.last;

  button_group->flag &= ~UI_BUTTON_GROUP_LOCK;

  /* Repurpose the first header button group if it is empty, in case the first button added to
   * the panel doesn't add a new group (if the button is created directly rather than through an
   * interface layout call). */
  if (BLI_listbase_is_single(&block->button_groups) &&
      BLI_listbase_is_empty(&button_group->buttons)) {
    button_group->flag &= ~UI_BUTTON_GROUP_PANEL_HEADER;
  }
  else {
    /* Always add a new button group. Although this may result in many empty groups, without it,
     * new buttons in the panel body not protected with a #ui_block_new_button_group call would
     * end up in the panel header group. */
    ui_block_new_button_group(block, 0);
  }
}

static float panel_region_offset_x_get(const ARegion *region)
{
  if (UI_panel_category_is_visible(region)) {
    if (RGN_ALIGN_ENUM_FROM_MASK(region->alignment) != RGN_ALIGN_RIGHT) {
      return UI_PANEL_CATEGORY_MARGIN_WIDTH;
    }
  }

  return 0.0f;
}
