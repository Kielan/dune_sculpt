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

void btn_drag_set_api(Btn *btn, ApiPtr *ptr)
{
  btn->dragtype = WIN_DRAG_API;
  if (btn->dragflag & UI_BTN_DRAGPOINT_FREE) {
    win_drag_data_free(btn->dragtype, btn->dragpoint);
    btn->dragflag &= ~UI_BTN_DRAGPOINT_FREE;
  }
  btn->dragpoint = (void *)ptr;
}

void btn_drag_set_path(Btn *btn, const char *path)
{
  btn->dragtype = WIN_DRAG_PATH;
  if (btn->dragflag & UI_BTN_DRAGPOINT_FREE) {
    win_drag_data_free(btn->dragtype, btn->dragpoint);
  }
  btn->dragpoint = win_drag_create_path_data(path);
  btn->dragflag |= UI_BTN_DRAGPOINT_FREE;
}

void btn_drag_set_name(Btn *btn, const char *name)
{
  btn->dragtype = WIN_DRAG_NAME;
  if (btn->dragflag & UI_BTN_DRAGPOINT_FREE) {
    win_drag_data_free(btn->dragtype, btn->dragpoint);
    btn->dragflag &= ~UI_BTN_DRAGPOINT_FREE;
  }
  btn->dragpoint = (void *)name;
}

void btn_drag_set_value(Btn *btn)
{
  btn->dragtype = WIN_DRAG_VALUE;
}
void btn_drag_set_image(Btn *btn, const char *path, int icon, const ImBuf *imb, float scale)
{
  ui_def_btn_icon(btn, icon, 0); /* no flag UI_HAS_ICON, so icon doesn't draw in btn */
  btn_drag_set_path(btn, path);
  btn_drag_attach_image(btn, imb, scale);
}

void btn_drag_free(Btn *btn)
{
  if (btn->dragpoint && (btn->dragflag & UI_BTN_DRAGPOINT_FREE)) {
    win_drag_data_free(but->dragtype, btn->dragpoint);
  }
}

bool but_drag_is_draggable(const Btn *btn)
{
  return btn->dragpoint != nullptr;
}

void btn_drag_start(Cxt *C, Btn *btn)
{
  WinDrag *drag = win_drag_data_create(C,
                                     btn->icon,
                                     btn->dragtype,
                                     btn->dragpoint,
                                     btn_value_get(btn),
                                     (btn->dragflag & UI_BTN_DRAGPOINT_FREE) ? WIN_DRAG_FREE_DATA :
                                                                              WIN_DRAG_NOP);
  /* WinDrag has ownership over dragpoint now, stop messing with it. */
  btn->dragpoint = nullptr;

  if (btn->imb) {
    win_event_drag_image(drag, btn->imb, btn->imb_scale);
  }

  win_event_start_prepared_drag(C, drag);

  /* Special feature for assets: We add another drag item that supports multiple assets. It
   * gets the assets from cxt. */
  if (ELEM(btn->dragtype, WIN_DRAG_ASSET, WIN_DRAG_ID)) {
    win_event_start_drag(C, ICON_NONE, WIN_DRAG_ASSET_LIST, nullptr, 0, WIN_DRAG_NOP);
  }
}

