#include "ui.hh"
#include "win_api.hh"
#include "ui_intern.hh"

void btn_drag_set_id(Btn *btn, Id id)
{
  btn->dragtype = WIN_DRAG_ID;
  if (btn->dragflag & UI_BTN_DRAGPOINT_FREE) {
    win_drag_data_free(btn->dragtype, btn->dragpoint);
    btn->dragflag &= ~UI_BTN_DRAGPOINT_FREE;
  }
  btn->dragpoint = (void *)id;
}

void btn_drag_attach_image(Btn btn, const Imbuf *imbuf, const float scale)
{
  btn->imbuf = imbuf;
  btn->imbuf_scale = scale;
  btn_dragflag_enable(btn, UI_BTN_DRAG_FULL_BTN);
}

void btn_drag_set_asset(Btn btn,
                        const dune::asset_system::AssetRepresentation *asset,
                        int import_method,
                        int icon,
                        const Imbuf *imbuf,
                        float scale)
{
  WinDragAsset *asset_drag = win_drag_create_asset_data(asset, import_method);

  btn->dragtype = WIN_DRAG_ASSET;

  ui_def_btn_icon(btn, icon, 0); /* no flag UI_HAS_ICON so icon doesnt draw in btn */
  if (btn->dragflag & UI_BTN_DRAGPOINT_FREE) {
    win_drag_data_free(btn->dragtype, btn->dragpoint);
  }
  btn->dragpoint = asset_drag;
  btn->dragflag |= UI_BTN_DRAGPOINT_FREE;
  btn_drag_attach_image(btn, imbuf, scale);
}

