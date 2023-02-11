/**
 * Functions for marking and clearing assets.
 */

#include "TYPES_ID.h"

#include "dune_asset.h"
#include "dune_context.h"
#include "dune_icons.h"
#include "dune_idtype.h"
#include "dune_lib_id.h"
#include "dune_main.h"

#include "UI_interface_icons.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "ED_asset_list.h"
#include "ED_asset_mark_clear.h"
#include "ED_asset_type.h"

bool ED_asset_mark_id(ID *id)
{
  if (id->asset_data) {
    return false;
  }
  if (!dune_id_can_be_asset(id)) {
    return false;
  }

  id_fake_user_set(id);

  const IDTypeInfo *id_type_info = dune_idtype_get_info_from_id(id);
  id->asset_data = dune_asset_metadata_create();
  id->asset_data->local_type_info = id_type_info->asset_type_info;

  /* Important for asset storage to update properly! */
  ED_assetlist_storage_tag_main_data_dirty();

  return true;
}

void ED_asset_generate_preview(const duneContext *C, ID *id)
{
  PreviewImage *preview = dune_previewimg_id_get(id);
  if (preview) {
    dune_previewimg_clear(preview);
  }

  UI_icon_render_id(C, nullptr, id, ICON_SIZE_PREVIEW, true);
}

bool ED_asset_clear_id(ID *id)
{
  if (!id->asset_data) {
    return false;
  }
  dune_asset_metadata_free(&id->asset_data);
  id_fake_user_clear(id);

  /* Important for asset storage to update properly! */
  ED_assetlist_storage_tag_main_data_dirty();

  return true;
}

void ED_assets_pre_save(struct Main *dunemain)
{
  ID *id;
  FOREACH_MAIN_ID_BEGIN (dunemain, id) {
    if (!id->asset_data || !id->asset_data->local_type_info) {
      continue;
    }

    if (id->asset_data->local_type_info->pre_save_fn) {
      id->asset_data->local_type_info->pre_save_fn(id, id->asset_data);
    }
  }
  FOREACH_MAIN_ID_END;
}

bool ED_asset_can_mark_single_from_ctx(const duneContext *C)
{
  /* Context needs a "id" pointer to be set for #ASSET_OT_mark()/#ASSET_OT_clear() to use. */
  const ID *id = static_cast<ID *>(ctx_data_pointer_get_type_silent(C, "id", &API_ID).data);
  if (!id) {
    return false;
  }
  return ED_asset_type_is_supported(id);
}
