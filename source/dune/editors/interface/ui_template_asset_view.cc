#include "AS_asset_representation.hh"

#include "types_space.h"
#include "types_userdef.h"

#include "dune_screen.hh"

#include "lib_path_util.h"
#include "lib_string.h"
#include "lib_string_ref.hh"

#include "loader_readfile.h"

#include "ed_asset.hh"
#include "ed_screen.hh"

#include "mem_guardedalloc.h"

#include "api_access.hh"
#include "api_prototypes.h"

#include "ui.hh"

#include "win_api.hh"
#include "win_types.hh"

#include "ui_intern.hh"

using namespace dune;

struct AssetViewListData {
  AssetLibRef asset_lib_ref;
  AssetFilterSettings filter_settings;
  Screen *screen;
  bool show_names;
};

static void asset_view_item_btn_drag_set(Btn *btn, AssetHandle *asset_handle)
{
  dune::asset_system::AssetRepresentation *asset = ed_asset_handle_get_representation(
      asset_handle);

  btn_dragflag_enable(btn, BTN_DRAG_FULL_BTN);

  Id *id = asset->local_id();
  if (id != nullptr) {
    btn_drag_set_id(btn, id);
    return;
  }

  const eAssetImportMethod import_method = asset->get_import_method().value_or(
      ASSET_IMPORT_APPEND_REUSE);

  ImBuf *imbuf = ed_assetlist_asset_image_get(asset_handle);
  btn_drag_set_asset(
      btn, asset, import_method, ed_asset_handle_get_preview_icon_id(asset_handle), imbuf, 1.0f);
}

static void asset_view_draw_item(uiList *ui_list,
                                 const Cxt * /*C*/,
                                 uiLayout *layout,
                                 ApiPtr * /*dataptr*/,
                                 ApiPtr * /*itemptr*/,
                                 int /*icon*/,
                                 ApiPtr * /*active_dataptr*/,
                                 const char * /*active_propname*/,
                                 int index,
                                 int /*flt_flag*/)
{
  AssetViewListData *list_data = (AssetViewListData *)ui_list->dyn_data->customdata;

  AssetHandle asset_handle = ed_assetlist_asset_handle_get_by_index(&list_data->asset_lib_ref,
                                                                    index);

  ApiPtr file_ptr = api_ptr_create(&list_data->screen->id,
                                           &ApiFileSelEntry,
                                           const_cast<FileDirEntry *>(asset_handle.file_data));
  uiLayoutSetCxtPtr(layout, "active_file", &file_ptr);

  uiBlock *block = uiLayoutGetBlock(layout);
  const bool show_names = list_data->show_names;
  const float size_x = ui_preview_tile_size_x();
  const float size_y = show_names ? ui_preview_tile_size_y() : ui_preview_tile_size_y_no_label();
  Btn *btn = BtnIconTxt(
      block,
      BTYPE_PREVIEW_TILE,
      0,
      ed_asset_handle_get_preview_icon_id(&asset_handle),
      show_names ? ed_asset_handle_get_representation(&asset_handle)->get_name().c_str() : "",
      0,
      0,
      size_x,
      size_y,
      nullptr,
      0,
      0,
      0,
      0,
      "");
  btn_icon(btn,
               ed_asset_handle_get_preview_icon_id(&asset_handle),
               /* NOLINTNEXTLINE: bugprone-suspicious-enum-usage */
               UI_HAS_ICON | BTN_ICON_PREVIEW);
  btn->emboss = UI_EMBOSS_NONE;
  if (!ui_list->dyn_data->custom_drag_optype) {
    asset_view_item_btn_drag_set(btn, &asset_handle);
  }
}

static void asset_view_filter_items(uiList *ui_list,
                                    const Cxt *C,
                                    ApiPtr *dataptr,
                                    const char *propname)
{
  AssetViewListData *list_data = (AssetViewListData *)ui_list->dyn_data->customdata;
  AssetFilterSettings &filter_settings = list_data->filter_settings;

  uiListNameFilter name_filter(*ui_list);

  ui_list_filter_and_sort_items(
      ui_list,
      C,
      [&name_filter, list_data, &filter_settings](
          const ApiPtr &itemptr, dune::StringRefNull name, int index) {
        asset_system::AssetRepresentation *asset = ed_assetlist_asset_get_by_index(
            list_data->asset_lib_ref, index);

        if (!ed_asset_filter_matches_asset(&filter_settings, *asset)) {
          return UI_LIST_ITEM_NEVER_SHOW;
        }
        return name_filter(itemptr, name, index);
      },
      dataptr,
      propname,
      [list_data](const ApiPtr & /*itemptr*/, int index) -> std::string {
        asset_system::AssetRepresentation *asset = ed_assetlist_asset_get_by_index(
            list_data->asset_lib_ref, index);

        return asset->get_name();
      });
}

static void asset_view_listener(uiList * /*ui_list*/, WinRgnListenerParams *params)
{
  const WinNotifier *notifier = params->notifier;

  switch (notifier->category) {
    case NC_ID: {
      if (ELEM(notifier->action, NA_RENAME)) {
        ed_assetlist_storage_tag_main_data_dirty();
      }
      break;
    }
  }

  if (ed_assetlist_listen(params->notifier)) {
    ed_rgn_tag_redraw(params->rgn);
  }
}

uiListType *UI_UL_asset_view()
{
  uiListType *list_type = (uiListType *)mem_calloc(sizeof(*list_type), __func__);

  STRNCPY(list_type->idname, "UI_UL_asset_view");
  list_type->draw_item = asset_view_draw_item;
  list_type->filter_items = asset_view_filter_items;
  list_type->listener = asset_view_listener;

  return list_type;
}

static void populate_asset_collection(const AssetLibRef &asset_lib_ref,
                                      ApiPtr &assets_dataptr,
                                      const char *assets_propname)
{
  ApiProp *assets_prop = api_struct_find_prop(&assets_dataptr, assets_propname);
  if (!assets_prop) {
    api_warning("Asset collection not found");
    return;
  }
  if (api_prop_type(assets_prop) != PROP_COLLECTION) {
    api_warning("Expected a collection prop");
    return;
  }
  if (!api_struct_is_a(api_prop_ptr_type(&assets_dataptr, assets_prop), &ApiAssetHandle))
  {
    api_warning("Expected a collection prop for AssetHandle items");
    return;
  }

  api_prop_collection_clear(&assets_dataptr, assets_prop);

  ed_assetlist_iter(asset_lib_ref, [&](AssetHandle /*asset*/) {
    /* Creating a dummy ApiAssetHandle collection item. It's file_data will be null. This is
     * because the FileDirEntry may be freed while iterating, there's a cache for them with a
     * maximum size. Further code will query as needed it using the collection index. */

    ApiPtr itemptr;
    api_prop_collection_add(&assets_dataptr, assets_prop, &itemptr);
    ApiPtr fileptr = api_ptr_create(nullptr, &ApiFileSelEntry, nullptr);
    api_ptr_set(&itemptr, "file_data", fileptr);

    return true;
  });
}

void uiTemplateAssetView(uiLayout *layout,
                         const Cxt *C,
                         const char *list_id,
                         ApiPtr *asset_lib_dataptr,
                         const char *asset_lib_propname,
                         ApiPtr *assets_dataptr,
                         const char *assets_propname,
                         ApiPtr *active_dataptr,
                         const char *active_propname,
                         const AssetFilterSettings *filter_settings,
                         const int display_flags,
                         const char *activate_opname,
                         ApiPtr *r_activate_op_props,
                         const char *drag_opname,
                         ApiPtr *r_drag_op_props)
{
  if (!list_id || !list_id[0]) {
    api_warning("Asset view needs a valid id");
    return;
  }

  uiLayout *col = uiLayoutColumn(layout, false);

  ApiProp *asset_lib_prop = api_struct_find_prop(asset_lib_dataptr,
                                                 asset_lib_propname);
  AssetLibRef asset_lib_ref = ed_asset_lib_ref_from_enum_value(
      api_prop_enum_get(asset_lib_dataptr, asset_lib_prop));

  uiLayout *row = uiLayoutRow(col, true);
  if ((display_flags & UI_TEMPLATE_ASSET_DRAW_NO_LIB) == 0) {
    uiItemFullR(row,
                asset_lib_dataptr,
                asset_lib_prop,
                API_NO_INDEX,
                0,
                UI_ITEM_NONE,
                "",
                ICON_NONE);
    if (asset_lib_ref.type != ASSET_LIB_LOCAL) {
      uiItemO(row, "", ICON_FILE_REFRESH, "ASSET_OT_lib_refresh");
    }
  }

  ed_assetlist_storage_fetch(&asset_lib_ref, C);
  ed_assetlist_ensure_previews_job(&asset_lib_ref, C);
  const int tot_items = ed_assetlist_size(&asset_lib_ref);

  populate_asset_collection(asset_lib_ref, *assets_dataptr, assets_propname);

  AssetViewListData *list_data = (AssetViewListData *)mem_malloc(sizeof(*list_data),
                                                                  "AssetViewListData");
  list_data->asset_lib_ref = asset_lib_ref;
  list_data->filter_settings = *filter_settings;
  list_data->screen = cxt_win_screen(C);
  list_data->show_names = (display_flags & UI_TEMPLATE_ASSET_DRAW_NO_NAMES) == 0;

  uiTemplateListFlags template_list_flags = UI_TEMPLATE_LIST_NO_GRIP;
  if ((display_flags & UI_TEMPLATE_ASSET_DRAW_NO_NAMES) != 0) {
    template_list_flags |= UI_TEMPLATE_LIST_NO_NAMES;
  }
  if ((display_flags & UI_TEMPLATE_ASSET_DRAW_NO_FILTER) != 0) {
    template_list_flags |= UI_TEMPLATE_LIST_NO_FILTER_OPTIONS;
  }

  uiLayout *subcol = uiLayoutColumn(col, false);

  uiLayoutSetScaleX(subcol, 0.8f);
  uiLayoutSetScaleY(subcol, 0.8f);

  /* TODO can we have some kind of model-view API to handle refing, filtering and lazy loading
   * (of previews) of the items? */
  uiList *list = uiTemplateList_ex(subcol,
                                   C,
                                   "UI_UL_asset_view",
                                   list_id,
                                   assets_dataptr,
                                   assets_propname,
                                   active_dataptr,
                                   active_propname,
                                   nullptr,
                                   tot_items,
                                   0,
                                   UILST_LAYOUT_BIG_PREVIEW_GRID,
                                   0,
                                   template_list_flags,
                                   list_data);
  if (!list) {
    /* List creation failed. */
    mem_free(list_data);
    return;
  }

  if (activate_opname) {
    ApiPtr *ptr = ui_list_custom_activate_op_set(
        list, activate_opname, r_activate_op_props != nullptr);
    if (r_activate_op_props && ptr) {
      *r_activate_op_props = *ptr;
    }
  }
  if (drag_opname) {
    ApiPtr *ptr = ui_list_custom_drag_op_set(
        list, drag_opname, r_drag_op_props != nullptr);
    if (r_drag_op_props && ptr) {
      *r_drag_op_props = *ptr;
    }
  }
}
