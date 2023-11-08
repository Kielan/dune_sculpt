/* a full doc with API notes can be found in
 * bf-blender/trunk/blender/doc/guides/interface_API.txt */

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "PIL_time.h"

#include "lib_dune.h"
#include "lib_math.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "types_screen.h"
#include "types_userdef.h"

#include "dune_cxt.h"
#include "dune_screen.h"

#include "api_access.h"

#include "font_api.h"

#include "win_api.h"
#include "win_types.h"

#include "ed_screen.h"

#include "ui_.h"
#include "ui_icons.h"
#include "ui_resources.h"
#include "ui_view2d.h"

#include "gpu_batch_presets.h"
#include "gpu_immediate.h"
#include "gpu_matrix.h"
#include "gpi_state.h"

#include "ui_intern.h"

/* Defines & Structs **/
#define ANIM_TIME 0.30
#define ANIM_INTERVAL 0.02

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

typedef enum uiPnlHandleState {
  PNL_STATE_DRAG,
  PNL_STATE_ANIM,
  PNL_STATE_EXIT,
} uiPnlHandleState;

typedef struct uiPnlHandleData {
  uiPnlHandleState state;
  /* Animation. */
  WinTimer *animtimer;
  double starttime;
  /* Dragging. */
  int startx, starty;
  int startofsx, startofsy;
  float start_cur_xmin, start_cur_ymin;
} uiPnlHandleData;

typedef struct PnlSort {
  Pnl *pnl;
  int new_offset_x;
  int new_offset_y;
} PnlSort;

static void pnl_set_expansion_from_list_data(const Cxt *C, Pnl *pnl);
static int get_pnl_real_size_y(const Pnl *pnl);
static void pnl_activate_state(const Cxt *C, Pnl *pnl, uiPnlHandleState state);
static int compare_pnl(const void *a, const void *b);
static bool pnl_type_cxt_poll(ARgn *rgn,
                              const PnlType *pnl_type,
                              const char *cxt);

/* Local Fns */
static bool pnl_active_anim_changed(List *list,
                                    Pnl **r_pnl_anim,
                                    bool *r_no_anim)
{
  LIST_FOREACH (Pnl *, pnl, list) {
    /* Detect pnl active flag changes. */
    if (!(pnl->type && pnl->type->parent)) {
      if ((pnl->runtime_flag & PNL_WAS_ACTIVE) && !(pnl->runtime_flag & PNL_ACTIVE)) {
        return true;
      }
      if (!(pnl->runtime_flag & PNL_WAS_ACTIVE) && (pnl->runtime_flag & PNL_ACTIVE)) {
        return true;
      }
    }

    /* Detect changes in pnl expansions. */
    if ((bool)(pnl->runtime_flag & PNL_WAS_CLOSED) != ui_pnl_is_closed(pnl)) {
      *r_pnl_anim = pnl;
      return false;
    }

    if ((pnl->runtime_flag & PNL_ACTIVE) && !ui_pnl_is_closed(pnl)) {
      if (pnl_active_anim_changed(&pnl->children, r_pnl_anim, r_no_anim)) {
        return true;
      }
    }

    /* Detect anim */
    if (pnl->activedata) {
      uiHandlePnlData *data = pnl->activedata;
      if (data->state == PNL_STATE_ANIM) {
        *r_pnl_anim = pnl;
      }
      else {
        /* Don't anim while handling other interaction. */
        *r_no_anim = true;
      }
    }
    if ((pnl->runtime_flag & PNL_ANIM_ALIGN) && !(*r_pnl_anim)) {
      *r_pnl_anim = p l;
    }
  }

  return false;
}

/* rtrn true if the props ed switch tabs since the last layout pass. **/
static bool props_space_needs_realign(const ScrArea *area, const ARgn *rgn)
{
  if (area->spacetype == SPACE_PROPS && rgn->rgntype == RGN_TYPE_WIN) {
    SpaceProps *sbtns = area->spacedata.first;

    if (sbtns->mainbo != sbtns->mainb) {
      return true;
    }
  }

  return false;
}

static bool pnls_need_realign(const ScrArea *area, ARgn *rgn, Pnl **r_pnl_anim)
{
  *r_pnl_anim = NULL;

  if (props_space_needs_realign(area, rgn)) {
    return true;
  }

  /* Detect if a pnl was added or removed. */
  Pnl *pnl_anim = NULL;
  bool no_anim = false;
  if (pnl_active_anim_changed(&rgn->pnls, &pnl_anim, &no_anim)) {
    return true;
  }

  /* Detect pnl marked for anim, if we're not already animating. */
  if (pnl_anim) {
    if (!no_anim) {
      *r_pnl_anim = pnl_anim;
    }
    return true;
  }

  return false;
}

/* Fns for Instanced PnlList */
static PnlList *pnllist_add_instanced(ARgn *rgn,
                                  List *pmls,
                                  PnlType *pnl_type,
                                  ApiPtr *custom_data)
{
  PnlList *pnllist = mem_callocn(sizeof(Pnl), __func__);
  pnl->type = pnl_type;
  lib_strncpy(pnl->pnlname, panel_type->idname, sizeof(pnl->pnlname));

  pnl->runtime.custom_data_ptr = custom_data;
  pnl->runtime_flag |= PNL_NEW_ADDED;

  /* Add the pnl's children too. Although they aren't instanced pnls, we can still use this
   * fn to create them, as ui_pnl_begin does other things we don't need to do. */
  LIST_FOREACH (LinkData *, child, &pnl_type->children) {
    PnlType *child_type = child->data;
    pnllistinstanced_add(region, &pnl->children, child_type, custom_data);
  }

  /* Make sure the pnl is added to the end of the display-order as well. This is needed for
   * loading existing files.
   * NOTE: We could use special behavior to place it after the panel that starts the list of
   * instanced pnls, btn that would add complexity that isn't needed for now. */
  int max_sortorder = 0;
  LIST_FOREACH (Pnl *, existing_pnl, pnls) {
    if (existing_pnl->sortorder > max_sortorder) {
      max_sortorder = existing_pnl->sortorder;
    }
  }
  pnl->sortorder = max_sortorder + 1;

  lib_addtail(pnls, pnl);

  return pnl;
}

Pnl *pnllistinstanced_add(const Cxt *C,
                              ARgn *rgn,
                              List *pnls,
                              const char *pnl_idname,
                              ApiPtr *custom_data)
{
  ARgnType *rgn_type = rgn->type;

  PnlType *pnl_type = lib_findstring(
      &rgn_type->pnltypes, pnl_idname, offsetof(PnlType, idname));

  if (pnl_type == NULL) {
    printf("Pnl type '%s' not found.\n", pnl_idname);
    return NULL;
  }

  Pnl *new_pnl = pnllistinstanced_add(rgn, pnls, pnl_type, custom_data);

  /* Do this after pnllistinstanced_add so all sub-pnls are added. */
  pnl_set_expansion_from_list_data(C, new_pnl);

  return new_pnl;
}

void pnllist_unique_str(Pnl *pnl, char *r_name)
{
  /* Pnl sort-order will be unique for a specific pnl type bc the instanced
   * pnl list is regen'd for every change in the data order/length. */
  snprintf(r_name, INSTANCED_PNL_UNIQUE_STR_LEN, "%d", pnl->sortorder);
}

/* Free a pnl and its children. Custom data is shared by the pnl and its children
 * and is freed by ui_pnllistinstanced_free.
 * note: The only pnls that should need to be deleted at runtime are pnls with the
 * PNL_TYPE_INSTANCED flag set. */
static void panel_delete(const Cxt *C, ARgn *rgn, List *pnls, Pnl *pnl)
{
  /* Recursively delete children. */
  LIST_FOREACH_MUTABLE (Pnl *, child, &pnl->children) {
    pnl_delete(C, rgn, &pnl->children, child);
  }
  lib_freelistn(&pml->children);

  lib_remlink(pnls, pnl);
  if (pnl->activedata) {
    mem_freen(pnl->activedata);
  }
  mem_freen(pnl);
}

void pnllistinstanced_free(const Cxt *C, ARgn *rgn)
{
  /* Delete pnls with the instanced flag. */
  LIST_FOREACH_MUTABLE (Pnl *, pnl, &rgn->pnls) {
    if ((pnl->type != NULL) && (pnl->type->flag & PNL_TYPE_INSTANCED)) {
      /* Make sure the pnl's handler is removed before deleting it. */
      if (C != NULL && pnl->activedata != NULL) {
        pnl_activate_state(C, pnl, PNL_STATE_EXIT);
      }

      /* Free pnl's custom data. */
      if (pnl->runtime.custom_data_ptr != NULL) {
        mem_freen(pnl->runtime.custom_data_ptr);
      }

      /* Free the pnl and its sub-pnls. */
      pnl_delete(C, rgn, &rgn->pnls, pnl);
    }
  }
}

bool ui_pnllist_matches_data(ARgn *rgn,
                             List *data,
                             uiListPnlIdFromDataFn pnl_idname_fn)
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
  LIST_FOREACH (Pnl *, pnl, &rgn->pnls) {
    if (pnl->type != NULL && pnl->type->flag & PNL_TYPE_INSTANCED) {
      /* The panels were reordered by drag and drop. */
      if (pnl->flag & PNL_INSTANCED_LIST_ORDER_CHANGED) {
        return false;
      }

      /* We reached the last data item before the last instanced pnl. */
      if (data_link == NULL) {
        return false;
      }

      /* Check if the pnl type matches the pnl type from the data item. */
      char pnl_idname[MAX_NAME];
      pnl_idname_fn(data_link, pnl_idname);
      if (!STREQ(pnl_idname, pnl->type->idname)) {
        return false;
      }

      data_link = data_link->next;
      i++;
    }
  }

  /* If we didn't make it to the last list item, the pnl list isn't complete. */
  if (i != data_len) {
    return false;
  }

  return true;
}

static void pnllistinstanced_reorder(Cxt *C, ARgn *rgn, PnlList *pnllist_drag)
{
  /* Without a type we cannot access the reorder cb. */
  if (drag_pnl->type == NULL) {
    return;
  }
  /* Don't reorder if this instanced pnl doesn't support drag and drop reordering. */
  if (drag_pnl->type->reorder == NULL) {
    return;
  }

  char *cxt = NULL;
  if (!pnllist_category_is_visible(rgn)) {
    cxt = drag_pnl->type->cxt;
  }

  /* Find how many instanced pnls with this cxt string. */
  int pnllist_len = 0;
  int start_index = -1;
  LIST_FOREACH (const Pnl *, pnl, &rgn->pnllist) {
    if (pnl->type) {
      if (pnl->type->flag & PNL_TYPE_INSTANCED) {
        if (pnl_type_cxt_poll(rgn, pnl->type, cxt)) {
          if (pnl == drag_pnl) {
            lib_assert(start_index == -1); /* This panel should only appear once. */
            start_index = list_pnls_len;
          }
          pnllist_len++;
        }
      }
    }
  }
  lib_assert(start_index != -1); /* The drag pnl should definitely be in the list. */

  /* Sort the matching instanced pnls by their display order. */
  PnlSort *pnl_sort = mem_callocn(list_pnls_len * sizeof(*pnl_sort), __func__);
  PnlSort *sort_index = pnl_sort;
  LIST_FOREACH (Panel *, pnl, &rgn->pnls) {
    if (pnl->type) {
      if (pnl->type->flag & PNL_TYPE_INSTANCED) {
        if (pnl_type_cxt_poll(rgn, pnl->type, cxt)) {
          sort_index->panel = panel;
          sort_index++;
        }
      }
    }
  }
  qsort(pnl_sort, list_pnls_len, sizeof(*pnl_sort), compare_pnl);

  /* Find how many of those panels are above this pnl. */
  int move_to_index = 0;
  for (; move_to_index < list_panels_len; move_to_index++) {
    if (pnl_sort[move_to_index].pnl == drag_pnl) {
      break;
    }
  }

  mem_freen(pnl_sort);

  if (move_to_index == start_index) {
    /* This case: the reorder was not changed, don't do any updates or call the cb. */
    return;
  }

  /* Set the bit to tell the interface to instanced the list. */
  drag_pnl->flag |= PNL_INSTANCED_LIST_ORDER_CHANGED;

  cxt_store_set(C, drag_pnl->runtime.ctx);

  /* Finally, move this panel's list item to the new index in its list. */
  drag_panel->type->reorder(C, drag_panel, move_to_index);

  cxt_store_set(C, NULL);
}

/* Recursive implementation for pnllistdata_expansion.
 * return: Whether the closed flag for the panel or any sub-pnls changed. */
static bool pnllistdata_expand_recursive(Pnl *pnl, short flag, short *flag_index)
{
  const bool open = (flag & (1 << *flag_index));
  bool changed = (open == ui_pnl_is_closed(pnl));

  SET_FLAG_FROM_TEST(pnl->flag, !open, PNL_CLOSED);

  LIST_FOREACH (Pnl *, child, &pnl->children) {
    *flag_index = *flag_index + 1;
    changed |= pnllistdata_expand_recursive(child, flag, flag_index);
  }
  return changed;
}

/* Set the expansion of the pnl and its sub-pnls from the flag stored in the
 * corresponding list data. The flag has expansion stored in each bit in depth first order */
static void pnllistdata_set_expansion_from(const Cxt *C, Pnl *pnl)
{
  lib_assert(pnl->type != NULL);
  lib_assert(pnl->type->flag & PNL_TYPE_INSTANCED);
  if (pnl->type->get_list_data_expand_flag == NULL) {
    /* Instanced pnl doesn't support loading expansion. */
    return;
  }

  const short expand_flag = pnl->type->get_list_data_expand_flag(C, pnl);
  short flag_index = 0;

  /* Start panel animation if the open state was changed. */
  if (pnllistdata_expand_recursive(pml, expand_flag, &flag_index)) {
    pnl_activate_state(C, pnl, PNL_STATE_ANIM);
  }
}

/* Set expansion based on the data for instanced panels */
static void rgn_pnllistdata_expand(const Cxt *C, ARgn *rgn)
{
  LIST_FOREACH (Pnl *, pnl, &rgn->pnls) {
    if (pnl->runtime_flag & PNL_ACTIVE) {
      PnlType *panel_type = pnl->type;
      if (pnl_type != NULL && pnl->type->flag & PNl_TYPE_INSTANCED) {
        pnllistdata_expand(C, pnl);
      }
    }
  }
}

/* Recursive impl for pnllistdata_flag_expand_set */
static void pnllist_expand_flag_get(const Pnl *pnl, short *flag, short *flag_index)
{
  const bool open = !(pnllist->flag & PNL_CLOSED);
  SET_FLAG_FROM_TEST(*flag, open, (1 << *flag_index));

  LIST_FOREACH (const Pnl *, child, &pnl->children) {
    *flag_index = *flag_index + 1;
    get_pnl_expand_flag(child, flag, flag_index);
  }
}

/* Call the cb to store the pnl and sub-pnl expansion settings in the list item that
 * corresponds to each instanced panel.
 *
 * note: This needs to it through all of the rgn's panels because the pnl with changed
 * expansion might have been the sub-pnl of an instanced pnl, meaning it might not know
 * which list item it corresponds to. */
static void pnllistdata_expand_flag_set(const Cxt *C, const ARgn *rgn)
{
  LIST_FOREACH (Pnl *, pnl, &rgn->pnls) {
    PnlType *pnl_type = pnl->type;
    if (pnl_type == NULL) {
      continue;
    }

    /* Check for PNL_ACTIVE so we only set the expand flag for active pnls. */
    if (pnllist_type->flag & PNL_TYPE_INSTANCED && pnl->runtime_flag & PNL_ACTIVE) {
      short expand_flag;
      short flag_index = 0;
      pnllist_expand_flag_get(pnllist, &expand_flag, &flag_index);
      if (pnl->type->set_list_data_expand_flag) {
        pnllist->type->set_list_data_expand_flag(C, pnllist, expand_flag);
      }
    }
  }
}

/* Panels */
static bool pnllistdata_custom_active_get(const PnlList *pnllist)
{
  /* The caller should make sure the panel is active and has a type. */
  lib_assert(pnllist_is_active(pnl));
  lib_assert(pnllist->type != NULL);

  if (pnllist->type->active_prop[0] != '\0') {
    ApiPtr *ptr = pnllist_custom_data_get(panel);
    if (ptr != NULL && !api_ptr_is_null(ptr)) {
      return api_bool_get(ptr, pnl->type->active_prop);
    }
  }

  return false;
}

static void pnllist_custom_data_active_set(Pnl *pnl)
{
  /* Since the panel is interacted with, it should be active and have a type */
  lib_assert(pnllist_is_active(panel));
  lib_assert(pnllist->type != NULL);

  if (pnllist->type->active_prop[0] != '\0') {
    ApiPtr *ptr = pnllistdata_custom__get(pnl);
    lib_assert(api_struct_find_prop(ptr, pnllist->type->active_prop) != NULL);
    if (ptr != NULL && !api_ptr_is_null(ptr)) {
      api_bool_set(ptr, panellist->type->active_prop, true);
    }
  }
}

/* Set flag state for a panel and its sub-pnls. **/
static void pnllist_flag_set_recursive(Pnl *pnl, short flag, bool value)
{
  SET_FLAG_FROM_TEST(pnl->flag, value, flag);

  LIST_FOREACH (Pnl *, child, &pnl->children) {
    pnllist_flag_set_recursive(child, flag, value);
  }
}

/* Set runtime flag state for a panel and its sub-pnls. **/
static void pnllist_runtimeflag_set_recursive(Pnl *pnl, short flag, bool value)
{
  SET_FLAG_FROM_TEST(pnl->runtime_flag, value, flag);

  LIST_FOREACH (Pnl *, sub_pnl, &pnl->children) {
    pnl_set_runtime_flag_recursive(sub_pnl, flag, value);
  }
}

static void pnllist_collapse_all(ARgn *rgn, const PnlList *from_pnllist)
{
  const bool has_category_tabs = pnllist_category_is_visible(rgn);
  const char *category = has_category_tabs ? pnllist_category_active_get(rgn, false) : NULL;
  const PnlType *from_pt = from_pnl->type;

  LIST_FOREACH (Pnl *, pnl, &rgn->pnls) {
    PnlType *pt = pnl->type;

    /* Close panels with headers in the same context. */
    if (pt && from_pt && !(pt->flag & PML_TYPE_NO_HEADER)) {
      if (!pt->cxt[0] || !from_pt->cxt[0] || STREQ(pt->cxt, from_pt->cxt)) {
        if ((pnl->flag & PNL_PIN) || !category || !pt->category[0] ||
            STREQ(pt->category, category)) {
          pnl->flag |= PNL_CLOSED;
        }
      }
    }
  }
}

static bool pnllist_type_cxt_poll(ARgn *rgn,
                                    const PnlType *pnl_type,
                                    const char *cxt)
{
  if (!lib_list_is_empty(&rgn->p llist_category)) {
    return STREQ(pnl_type->category, pnllist_category_active_get(rgn, false));
  }

  if (pnl_type->cxt[0] && STREQ(pnl_type->cxt, cxt)) {
    return true;
  }

  return false;
}

PnlList *pnllist_find_by_type(List *list, const P lType *pt)
{
  const char *idname = pt->idname;

  LIST_FOREACH (Pnl *, pnl, list) {
    if (STREQLEN(pnl->pnlname, idname, sizeof(pnl->pnlname))) {
      return pnl;
    }
  }
  return NULL;
}

PnlList *pnllist_begin(
    ARgn *rgn, List *list, uiBlock *block, PnlType *pt, PnlList *pnllist, bool *r_open)
{
  PnlList *pnllist_last;
  const char *drawname = cxt_IFACE_(pt->lang_cxt, pt->label);
  const char *idname = pt->idname;
  const bool newpnl = (pnl == NULL);

  if (newpnl) {
    pnl = mem_callocn(sizeof(Pnl), __func__);
    pnl->type = pt;
    lib_strncpy(pnl->pnlname, idname, sizeof(pnl->pnlname));

    if (pt->flag & PNL_TYPE_DEFAULT_CLOSED) {
      panel->flag |= PNL_CLOSED;
      panel->runtime_flag |= PNL_WAS_CLOSED;
    }

    panel->ofsx = 0;
    panel->ofsy = 0;
    panel->sizex = 0;
    panel->sizey = 0;
    panel->blocksizex = 0;
    panel->blocksizey = 0;
    panel->runtime_flag |= PANEL_NEW_ADDED;

    lib_addtail(list, panel);
  }
  else {
    /* Panel already exists. */
    panel->type = pt;
  }

  panel->runtime.block = block;

  lib_strncpy(panel->drawname, drawname, sizeof(panel->drawname));

  /* If a new panel is added, we insert it right after the panel that was last added.
   * This way new panels are inserted in the right place between versions. */
  for (panel_last = list->first; panel_last; panel_last = panel_last->next) {
    if (panel_last->runtime_flag & PANEL_LAST_ADDED) {
      lib_remlink(list, panel);
      lib_insertlinkafter(list, panel_last, panel);
      break;
    }
  }

  if (newpanel) {
    panel->sortorder = (panel_last) ? panel_last->sortorder + 1 : 0;

    LIST_FOREACH (Panel *, panel_next, list) {
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
  if (rgn->alignment == RGN_ALIGN_FLOAT) {
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

  ui_block_new_btn_group(block, BTN_GROUP_LOCK | BTN_GROUP_PANEL_HEADER);
}

void ui_panellist_headerbtns_end(Panel *panel)
{
  uiBlock *block = panel->runtime.block;

  /* A btn group should always be created in ui_panellist_headerbtns_begin. */
  lib_assert(!lib_list_is_empty(&block->btn_groups));

  BtnGroup *btn_group = block->btn_groups.last;

  btn_group->flag &= ~BTN_GROUP_LOCK;

  /* Repurpose the first header btn group if it is empty, in case the first btn added to
   * the panel doesn't add a new group (if the btn is created directly rather than through an
   * interface layout call). */
  if (lib_list_is_single(&block->btn_groups) &&
      lib_list_is_empty(&btn_group->btns)) {
    btn_group->flag &= ~BTN_GROUP_PANEL_HEADER;
  }
  else {
    /* Always add a new btn group. Although this may result in many empty groups, without it,
     * new btns in the panel body not protected with a ui_block_new_btn_group call would
     * end up in the panel header group. */
    ui_block_btn_group_new(block, 0);
  }
}

static float panellist_rgn_offset_x_get(const ARgn *rgn)
{
  if (ui_panellist_category_visible(rgn)) {
    if (RGN_ALIGN_ENUM_FROM_MASK(rgn->alignment) != RGN_ALIGN_RIGHT) {
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

    /* prevent tiny or narrow rgns to get
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
  /* Instead of zero at least use a tiny offset, otherwise
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

  lib_rctf_trans(&curmasked, -xofs, -yofs);

  /* This flag set by outliner, for icons */
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

void ui_view2d_view_orthoSpecial(ARgn *rgn, View2D *v2d, const bool xaxis)
{
  rctf curmasked;
  float xofs, yofs;

  /* Pixel offsets (-GLA_PIXEL_OFS) are needed to get 1:1
   * correspondence with pixels for smooth UI drawing,
   * but only applied where requested. */
  /* temp. */
  xofs = 0.0f;  // (v2d->flag & V2D_PIXELOFS_X) ? GLA_PIXEL_OFS : 0.0f;
  yofs = 0.0f;  // (v2d->flag & V2D_PIXELOFS_Y) ? GLA_PIXEL_OFS : 0.0f;

  /* apply mask-based adjustments to cur rect (due to scrollers),
   * to eliminate scaling artifacts */
  view2d_map_cur_using_mask(v2d, &curmasked);

  /* only set matrix with 'cur' coordinates on relevant axes */
  if (xaxis) {
    winOrtho2(curmasked.xmin - xofs, curmasked.xmax - xofs, -yofs, rgn->winy - yofs);
  }
  else {
    winOrtho2(-xofs, rgn->winx - xofs, curmasked.ymin - yofs, curmasked.ymax - yofs);
  }
}

void ui_view2d_view_restore(const Cxt *C)
{
  ARgn *rgn = cxt_win_rgn(C);
  const int width = lib_rcti_size_x(&rgn->winrct) + 1;
  const int height = lib_rcti_size_y(&rgn->winrct) + 1;

  winOrtho2(0.0f, (float)width, 0.0f, (float)height);
  gpu_matrix_identity_set();

  //  ed_rgn_pixelspace(cxt_wm_rgn(C));
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
    ui_GetThemeColorBlendShade3ubv(colorid, TH_GRID, 0.25f, offset, grid_line_color);

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
  ui_GetThemeColorBlendShade3ubv(
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
  /* The factor applied to the min_step argument. This could be easily computed in runtime,
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

/* Scrollers */
/* View2DScrollers is typedef'd in ui_view2d.h
 * warning The start of this struct must not change, as view2d_ops.c uses this too.
 * For now, we don't need to have a separate (internal) header for structs like this... */
struct View2DScrollers {
  /* focus bubbles */
  /* focus bubbles */
  /* focus bubbles */
  int vert_min, vert_max; /* vert scrollbar */
  int hor_min, hor_max;   /* hor scrollbar */

  /* Exact size of slider backdrop. */
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
   * - These should always remain within the visible rgn of the scrollbar
   * - They represent the rgn of 'tot' that is visible in 'cur' */

  /* horizontal scrollers */
  if (scroll & V2D_SCROLL_HOR) {
    /* scroller 'button' extents */
    totsize = lib_rctf_size_x(&v2d->tot);
    scrollsize = (float)lib_rcti_size_x(&hor);
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
  if (scroll & V2D_SCROLL_VERT) {
    /* scroller 'button' extents */
    totsize = lib_rctf_size_y(&v2d->tot);
    scrollsize = (float)lib_rcti_size_y(&vert);
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

void ui_view2d_scrollers_draw(View2D *v2d, const rcti *mask_custom)
{
  View2DScrollers scrollers;
  ui_view2d_scrollers_calc(v2d, mask_custom, &scrollers);
  Theme *theme = ui_GetTheme();
  rcti vert, hor;
  const int scroll = view2d_scroll_mapped(v2d->scroll);
  const char emboss_alpha = theme->tui.widget_emboss[3];
  uchar scrollers_back_color[4];

  /* Color for scrollbar backs */
  ui_GetThemeColor4ubv(TH_BACK, scrollers_back_color);

  /* make copies of rects for less typing */
  vert = scrollers.vert;
  hor = scrollers.hor;

  /* horizontal scrollbar */
  if (scroll & V2D_SCROLL_HOR) {
    uiWidgetColors wcol = theme->tui.wcol_scroll;
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
    theme->tui.widget_emboss[3] *= alpha_fac; /* will be reset later */

    /* show zoom handles if:
     * - zooming on x-axis is allowed (no scroll otherwise)
     * - slider bubble is large enough (no overdraw confusion)
     * - scale is shown on the scroller
     *   (workaround to make sure that btn windws don't show these,
     *   and only the time-grids with their zoom-ability can do so). */
    if ((v2d->keepzoom & V2D_LOCKZOOM_X) == 0 && (v2d->scroll & V2D_SCROLL_HOR_HANDLES) &&
        (lib_rcti_size_x(&slider) > V2D_SCROLL_HANDLE_SIZE_HOTSPOT)) {
      state |= UI_SCROLL_ARROWS;
    }

    ui_draw_widget_scroll(&wcol, &hor, &slider, state);
  }

  /* vertical scrollbar */
  if (scroll & V2D_SCROLL_VERTICAL) {
    uiWidgetColors wcol = theme->tui.wcol_scroll;
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
    theme->tui.widget_emboss[3] *= alpha_fac; /* will be reset later */

    /* show zoom handles if:
     * - zooming on y-axis is allowed (no scroll otherwise)
     * - slider bubble is large enough (no overdraw confusion)
     * - scale is shown on the scroller
     *   (workaround to make sure that btn windws don't show these,
     *   and only the time-grids with their zoomability can do so) */
    if ((v2d->keepzoom & V2D_LOCKZOOM_Y) == 0 && (v2d->scroll & V2D_SCROLL_VERT_HANDLES) &&
        (lib_rcti_size_y(&slider) > V2D_SCROLL_HANDLE_SIZE_HOTSPOT)) {
      state |= UI_SCROLL_ARROWS;
    }

    ui_draw_widget_scroll(&wcol, &vert, &slider, state);
  }

  /* Was changed above, so reset. */
  theme->tui.widget_emboss[3] = emboss_alpha;
}

/* List View Utils */
void ui_view2d_listview_view_to_cell(float columnwidth,
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

/* Coordinate Conversions */
float ui_view2d_rgn_to_view_x(const struct View2D *v2d, float x)
{
  return (v2d->cur.xmin +
          (lib_rctf_size_x(&v2d->cur) * (x - v2d->mask.xmin) / lib_rcti_size_x(&v2d->mask)));
}
float ui_view2d_rgn_to_view_y(const struct View2D *v2d, float y)
{
  return (v2d->cur.ymin +
          (lib_rctf_size_y(&v2d->cur) * (y - v2d->mask.ymin) / lib_rcti_size_y(&v2d->mask)));
}

void ui_view2d_rgn_to_view(
    const View2D *v2d, float x, float y, float *r_view_x, float *r_view_y)
{
  *r_view_x = ui_view2d_rgn_to_view_x(v2d, x);
  *r_view_y = ui_view2d_rgn_to_view_y(v2d, y);
}

void ui_view2d_rgn_to_view_rctf(const View2D *v2d, const rctf *rect_src, rctf *rect_dst)
{
  const float cur_size[2] = {lib_rctf_size_x(&v2d->cur), lib_rctf_size_y(&v2d->cur)};
  const float mask_size[2] = {lib_rcti_size_x(&v2d->mask), lib_rcti_size_y(&v2d->mask)};

  rect_dst->xmin = (v2d->cur.xmin +
                    (cur_size[0] * (rect_src->xmin - v2d->mask.xmin) / mask_size[0]));
  rect_dst->xmax = (v2d->cur.xmin +
                    (cur_size[0] * (rect_src->xmax - v2d->mask.xmin) / mask_size[0]));
  rect_dst->ymin = (v2d->cur.ymin +
                    (cur_size[1] * (rect_src->ymin - v2d->mask.ymin) / mask_size[1]));
  rect_dst->ymax = (v2d->cur.ymin +
                    (cur_size[1] * (rect_src->ymax - v2d->mask.ymin) / mask_size[1]));
}

float ui_view2d_view_to_rgn_x(const View2D *v2d, float x)
{
  return (v2d->mask.xmin +
          (((x - v2d->cur.xmin) / lib_rctf_size_x(&v2d->cur)) * lib_rcti_size_x(&v2d->mask)));
}
float ui_view2d_view_to_rgn_y(const View2D *v2d, float y)
{
  return (v2d->mask.ymin +
          (((y - v2d->cur.ymin) / lib_rctf_size_y(&v2d->cur)) * lib_rcti_size_y(&v2d->mask)));
}

bool ui_view2d_view_to_rgn_clip(
    const View2D *v2d, float x, float y, int *r_rgn_x, int *r_rgn_y)
{
  /* express given coordinates as proportional values */
  x = (x - v2d->cur.xmin) / lib_rctf_size_x(&v2d->cur);
  y = (y - v2d->cur.ymin) / lib_rctf_size_y(&v2d->cur);

  /* check if values are within bounds */
  if ((x >= 0.0f) && (x <= 1.0f) && (y >= 0.0f) && (y <= 1.0f)) {
    *r_rgn_x = (int)(v2d->mask.xmin + (x * lib_rcti_size_x(&v2d->mask)));
    *r_rgn_y = (int)(v2d->mask.ymin + (y * lib_rcti_size_y(&v2d->mask)));

    return true;
  }

  /* set initial value in case coordinate lies outside of bounds */
  *r_rgn_x = *r_rhn_y = V2D_IS_CLIPPED;

  return false;
}

void ui_view2d_view_to_rgn(
    const View2D *v2d, float x, float y, int *r_rgn_x, int *r_rgn_y)
{
  /* Step 1: express given coordinates as proportional values. */
  x = (x - v2d->cur.xmin) / lib_rctf_size_x(&v2d->cur);
  y = (y - v2d->cur.ymin) / lib_rctf_size_y(&v2d->cur);

  /* Step 2: convert proportional distances to screen coordinates. */
  x = v2d->mask.xmin + (x * lib_rcti_size_x(&v2d->mask));
  y = v2d->mask.ymin + (y * lib_rcti_size_y(&v2d->mask));

  /* Although we don't clamp to lie within rgn bounds, we must avoid exceeding size of ints. */
  *r_rgn_x = clamp_float_to_int(x);
  *r_rgn_y = clamp_float_to_int(y);
}

void ui_view2d_view_to_rgn_fl(
    const View2D *v2d, float x, float y, float *r_region_x, float *r_region_y)
{
  /* express given coordinates as proportional values */
  x = (x - v2d->cur.xmin) / lib_rctf_size_x(&v2d->cur);
  y = (y - v2d->cur.ymin) / lib_rctf_size_y(&v2d->cur);

  /* convert proportional distances to screen coordinates */
  *r_rgn_x = v2d->mask.xmin + (x * lib_rcti_size_x(&v2d->mask));
  *r_rgn_y = v2d->mask.ymin + (y * lib_rcti_size_y(&v2d->mask));
}

void ui_view2d_view_to_rgn_rcti(const View2D *v2d, const rctf *rect_src, rcti *rect_dst)
{
  const float cur_size[2] = {lib_rctf_size_x(&v2d->cur), lib_rctf_size_y(&v2d->cur)};
  const float mask_size[2] = {lib_rcti_size_x(&v2d->mask), lib_rcti_size_y(&v2d->mask)};
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

void ui_view2d_view_to_rgn_m4(const View2D *v2d, float matrix[4][4])
{
  rctf mask;
  unit_m4(matrix);
  lib_rctf_rcti_copy(&mask, &v2d->mask);
  lib_rctf_transform_calc_m4_pivot_min(&v2d->cur, &mask, matrix);
}

bool ui_view2d_view_to_rgn_rcti_clip(const View2D *v2d, const rctf *rect_src, rcti *rect_dst)
{
  const float cur_size[2] = {lib_rctf_size_x(&v2d->cur), lib_rctf_size_y(&v2d->cur)};
  const float mask_size[2] = {lib_rcti_size_x(&v2d->mask), lib_rcti_size_y(&v2d->mask)};
  rctf rect_tmp;

  lib_assert(rect_src->xmin <= rect_src->xmax && rect_src->ymin <= rect_src->ymax);

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

/* Utils */
View2D *ui_view2d_fromcxt(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  ARgn *rgn = cxt_win_rgn(C);

  if (area == NULL) {
    return NULL;
  }
  if (rgn == NULL) {
    return NULL;
  }
  return &(rgn->v2d);
}

View2D *ui_view2d_fromcxt_rwin(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  ARgn *rgn = cxt_win_rgn(C);

  if (area == NULL) {
    return NULL;
  }
  if (rgn == NULL) {
    return NULL;
  }
  if (rgn->rgntype != RGN_TYPE_WIN) {
    ARgn *rgn_win = dune_area_find_rgn_type(area, RGN_TYPE_WIN);
    return rgn_win ? &(rgn_win->v2d) : NULL;
  }
  return &(rgn->v2d);
}

void ui_view2d_scroller_size_get(const View2D *v2d, float *r_x, float *r_y)
{
  const int scroll = view2d_scroll_mapped(v2d->scroll);

  if (r_x) {
    if (scroll & V2D_SCROLL_VERT) {
      *r_x = (scroll & V2D_SCROLL_VERT_HANDLES) ? V2D_SCROLL_HANDLE_WIDTH : V2D_SCROLL_WIDTH;
    }
    else {
      *r_x = 0;
    }
  }
  if (r_y) {
    if (scroll & V2D_SCROLL_HOR) {
      *r_y = (scroll & V2D_SCROLL_HOR_HANDLES) ? V2D_SCROLL_HANDLE_HEIGHT :
                                                        V2D_SCROLL_HEIGHT;
    }
    else {
      *r_y = 0;
    }
  }
}

void ui_view2d_scale_get(const View2D *v2d, float *r_x, float *r_y)
{
  if (r_x) {
    *r_x = ui_view2d_scale_get_x(v2d);
  }
  if (r_y) {
    *r_y = ui_view2d_scale_get_y(v2d);
  }
}
float ui_view2d_scale_get_x(const View2D *v2d)
{
  return lib_rcti_size_x(&v2d->mask) / lib_rctf_size_x(&v2d->cur);
}
float ui_view2d_scale_get_y(const View2D *v2d)
{
  return lib_rcti_size_y(&v2d->mask) / lib_rctf_size_y(&v2d->cur);
}
void ui_view2d_scale_get_inverse(const View2D *v2d, float *r_x, float *r_y)
{
  if (r_x) {
    *r_x = lib_rctf_size_x(&v2d->cur) / lib_rcti_size_x(&v2d->mask);
  }
  if (r_y) {
    *r_y = lib_rctf_size_y(&v2d->cur) / lib_rcti_size_y(&v2d->mask);
  }
}

void ui_view2d_center_get(const struct View2D *v2d, float *r_x, float *r_y)
{
  /* get center */
  if (r_x) {
    *r_x = lib_rctf_cent_x(&v2d->cur);
  }
  if (r_y) {
    *r_y = lib_rctf_cent_y(&v2d->cur);
  }
}
void ui_view2d_center_set(struct View2D *v2d, float x, float y)
{
  lib_rctf_recenter(&v2d->cur, x, y);

  /* make sure that 'cur' rect is in a valid state as a result of these changes */
  ui_view2d_curRect_validate(v2d);
}

void ui_view2d_offset(struct View2D *v2d, float xfac, float yfac)
{
  if (xfac != -1.0f) {
    const float xsize = lib_rctf_size_x(&v2d->cur);
    const float xmin = v2d->tot.xmin;
    const float xmax = v2d->tot.xmax - xsize;

    v2d->cur.xmin = (xmin * (1.0f - xfac)) + (xmax * xfac);
    v2d->cur.xmax = v2d->cur.xmin + xsize;
  }

  if (yfac != -1.0f) {
    const float ysize = lib_rctf_size_y(&v2d->cur);
    const float ymin = v2d->tot.ymin;
    const float ymax = v2d->tot.ymax - ysize;

    v2d->cur.ymin = (ymin * (1.0f - yfac)) + (ymax * yfac);
    v2d->cur.ymax = v2d->cur.ymin + ysize;
  }

  ui_view2d_curRect_validate(v2d);
}

char ui_view2d_mouse_in_scrollers_ex(const ARgn *rgn,
                                     const View2D *v2d,
                                     const int xy[2],
                                     int *r_scroll)
{
  const int scroll = view2d_scroll_mapped(v2d->scroll);
  *r_scroll = scroll;

  if (scroll) {
    /* Move to rgn-coordinates. */
    const int co[2] = {
        xy[0] - rgn->winrct.xmin,
        xy[1] - rgn->winrct.ymin,
    };
    if (scroll & V2D_SCROLL_HOR) {
      if (IN_2D_HOR_SCROLL(v2d, co)) {
        return 'h';
      }
    }
    if (scroll & V2D_SCROLL_VERT) {
      if (IN_2D_VERT_SCROLL(v2d, co)) {
        return 'v';
      }
    }
  }

  return 0;
}

char ui_view2d_rect_in_scrollers_ex(const ARgn *rgn,
                                    const View2D *v2d,
                                    const rcti *rect,
                                    int *r_scroll)
{
  const int scroll = view2d_scroll_mapped(v2d->scroll);
  *r_scroll = scroll;

  if (scroll) {
    /* Move to rgn-coordinates. */
    rcti rect_rgn = *rect;
    lib_rcti_translate(&rect_rgn, -rgn->winrct.xmin, rgn->winrct.ymin);
    if (scroll & V2D_SCROLL_HORIZONTAL) {
      if (IN_2D_HOR_SCROLL_RECT(v2d, &rect_rgn)) {
        return 'h';
      }
    }
    if (scroll & V2D_SCROLL_VERT) {
      if (IN_2D_VERT_SCROLL_RECT(v2d, &rect_rgn)) {
        return 'v';
      }
    }
  }

  return 0;
}

char ui_view2d_mouse_in_scrollers(const ARgn *rgn, const View2D *v2d, const int xy[2])
{
  int scroll_dummy = 0;
  return ui_view2d_mouse_in_scrollers_ex(rgn, v2d, xy, &scroll_dummy);
}

char ui_view2d_rect_in_scrollers(const ARgn *rgn, const View2D *v2d, const rcti *rect)
{
  int scroll_dummy = 0;
  return ui_view2d_rect_in_scrollers_ex(rgn, v2d, rect, &scroll_dummy);
}

/* View2D Text Drawing Cache */
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

void ui_view2d_text_cache_add(
    View2D *v2d, float x, float y, const char *str, size_t str_len, const uchar col[4])
{
  int mval[2];

  lib_assert(str_len == strlen(str));

  if (ui_view2d_view_to_rgn_clip(v2d, x, y, &mval[0], &mval[1])) {
    const int alloc_len = str_len + 1;
    View2DString *v2s;

    if (g_v2d_strings_arena == NULL) {
      g_v2d_strings_arena = lib_memarena_new(MEM_SIZE_OPTIMAL(1 << 14), __func__);
    }

    v2s = lib_memarena_alloc(g_v2d_strings_arena, sizeof(View2DString) + alloc_len);

    LIB_LINKS_PREPEND(g_v2d_strings, v2s);

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

  lib_assert(str_len == strlen(str));

  if (view2d_view_to_rgn_rcti_clip(v2d, rect_view, &rect)) {
    const int alloc_len = str_len + 1;
    View2DString *v2s;

    if (g_v2d_strings_arena == NULL) {
      g_v2d_strings_arena = lib_memarena_new(MEM_SIZE_OPTIMAL(1 << 14), __func__);
    }

    v2s = lib_memarena_alloc(g_v2d_strings_arena, sizeof(View2DString) + alloc_len);

    LIB_LINKS_PREPEND(g_v2d_strings, v2s);

    v2s->col.pack = *((const int *)col);

    v2s->rect = rect;

    v2s->mval[0] = v2s->rect.xmin;
    v2s->mval[1] = v2s->rect.ymin;

    memcpy(v2s->str, str, alloc_len);
  }
}

void view2d_txt_cache_draw(ARgn *rgn)
{
  View2DString *v2s;
  int col_pack_prev = 0;

  /* investigate using font_ascender() */
  const int font_id = font_default();

  font_set_default();
  const float default_height = g_v2d_strings ? font_height(font_id, "28", 3) : 0.0f;

  winOrtho2_rgn_pixelspace(rgn);

  for (v2s = g_v2d_strings; v2s; v2s = v2s->next) {
    int xofs = 0, yofs;

    yofs = ceil(0.5f * (lib_rcti_size_y(&v2s->rect) - default_height));
    if (yofs < 1) {
      yofs = 1;
    }

    if (col_pack_prev != v2s->col.pack) {
      font_color3ubv(font_id, v2s->col.ub);
      col_pack_prev = v2s->col.pack;
    }

    if (v2s->rect.xmin >= v2s->rect.xmax) {
      font_draw_default((float)(v2s->mval[0] + xofs),
                       (float)(v2s->mval[1] + yofs),
                       0.0,
                       v2s->str,
                       FONT_DRAW_STR_DUMMY_MAX);
    }
    else {
      font_enable(font_id, FONT_CLIPPING);
      font_clipping(
          font_id, v2s->rect.xmin - 4, v2s->rect.ymin - 4, v2s->rect.xmax + 4, v2s->rect.ymax + 4);
      font_draw_default(
          v2s->rect.xmin + xofs, v2s->rect.ymin + yofs, 0.0f, v2s->str, FONT_DRAW_STR_DUMMY_MAX);
      font_disable(font_id, FONT_CLIPPING);
    }
  }
  g_v2d_strings = NULL;

  if (g_v2d_strings_arena) {
    lib_memarena_free(g_v2d_strings_arena);
    g_v2d_strings_arena = NULL;
  }
}
