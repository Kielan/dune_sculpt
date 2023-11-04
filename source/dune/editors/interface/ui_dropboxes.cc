#include "dune_cxt.h"

#include "lib_string.h"
#include "lang.h"

#include "types_material.h"
#include "types_space.h"

#include "mem_guardedalloc.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "win_api.h"

#include "ui.h"

/* Tree View Drag/Drop Cbs */

static bool ui_tree_view_drop_poll(Cxt *C, WinDrag *drag, const WinEvent *event)
{
  const ARegion *region = cxt_win_region(C);
  const uiTreeViewItemHandle *hovered_tree_item = ui_block_tree_view_find_item_at(region,
                                                                                  event->xy);
  if (!hovered_tree_item) {
    return false;
  }

  if (drag->drop_state.free_disabled_info) {
    MEM_SAFE_FREE(drag->drop_state.disabled_info);
  }

  drag->drop_state.free_disabled_info = false;
  return ui_tree_view_item_can_drop(hovered_tree_item, drag, &drag->drop_state.disabled_info);
}

static char *ui_tree_view_drop_tooltip(Cxt *C,
                                       WinDrag *drag,
                                       const int xy[2],
                                       wmDropBox *UNUSED(drop))
{
  const ARegion *region = cxt_win_region(C);
  const uiTreeViewItemHandle *hovered_tree_item = ui_block_tree_view_find_item_at(region, xy);
  if (!hovered_tree_item) {
    return nullptr;
  }

  return ui_tree_view_item_drop_tooltip(hovered_tree_item, drag);
}

/* Name Drag/Drop Cbs */
static bool ui_drop_name_poll(struct Cxt *C, WinDrag *drag, const WinEvent *UNUSED(event))
{
  return btn_active_drop_name(C) && (drag->type == WIN_DRAG_ID);
}

static void ui_drop_name_copy(WinDrag *drag, WinDropBox *drop)
{
  const Id *id = win_drag_get_local_id(drag, 0);
  api_string_set(drop->ptr, "string", id->name + 2);
}

/* Material Drag/Drop Cbs */
static bool ui_drop_material_poll(Cxt *C, WinDrag *drag, const WinEvent *UNUSED(event))
{
  ApiPtr mat_slot = cxt_data_ptr_get_type(C, "material_slot", &ApiMaterialSlot);
  return win_drag_is_id_type(drag, ID_MA) && !api_ptr_is_null(&mat_slot);
}

static void ui_drop_material_copy(WinDrag *drag, WinDropBox *drop)
{
  const Id *id = win_drag_get_local_id_or_import_from_asset(drag, ID_MA);
  api_int_set(drop->ptr, "session_uuid", (int)id->session_uuid);
}

static char *ui_drop_material_tooltip(Cxt *C,
                                      WinDrag *drag,
                                      const int UNUSED(xy[2]),
                                      struct WinDropBox *UNUSED(drop))
{
  ApiPtr api_ptr = cxt_data_ptr_get_type(C, "object", &apiObject);
  Object *ob = (Object *)api_ptr.data;
  lib_assert(ob);

  ApiPtr mat_slot = cxt_data_ptr_get_type(C, "material_slot", &ApiMaterialSlot);
  lib_assert(mat_slot.data);

  const int target_slot = api_int_get(&mat_slot, "slot_index") + 1;

  ApiPtr api_prev_material = api_ptr_get(&mat_slot, "material");
  Material *prev_mat_in_slot = (Material *)api_prev_material.data;
  const char *dragged_material_name = win_drag_get_item_name(drag);

  char *result;
  if (prev_mat_in_slot) {
    const char *tooltip = TIP_("Drop %s on slot %d (replacing %s) of %s");
    result = lib_sprintfn(tooltip,
                          dragged_material_name,
                          target_slot,
                          prev_mat_in_slot->id.name + 2,
                          ob->id.name + 2);
  }
  else if (target_slot == ob->actcol) {
    const char *tooltip = TIP_("Drop %s on slot %d (active slot) of %s");
    result = lib_sprintfn(tooltip, dragged_material_name, target_slot, ob->id.name + 2);
  }
  else {
    const char *tooltip = TIP_("Drop %s on slot %d of %s");
    result = lib_sprintfn(tooltip, dragged_material_name, target_slot, ob->id.name + 2);
  }

  return result;
}

/* Add UI Drop Boxes */
void ed_dropboxes_ui()
{
  List *list = win_dropboxmap_find("User Interface", SPACE_EMPTY, 0);

  win_dropbox_add(list,
                 "UI_OT_tree_view_drop",
                 ui_tree_view_drop_poll,
                 nullptr,
                 nullptr,
                 ui_tree_view_drop_tooltip);
  win_dropbox_add(list,
                 "UI_OT_drop_name",
                 ui_drop_name_poll,
                 ui_drop_name_copy,
                 win_drag_free_imported_drag_id,
                 nullptr);
  win_dropbox_add(list,
                 "UI_OT_drop_material",
                 ui_drop_material_poll,
                 ui_drop_material_copy,
                 win_drag_free_imported_drag_id,
                 ui_drop_material_tooltip);
}
