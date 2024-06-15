/* Generic cxt popup menus. */
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_scene.h"
#include "types_screen.h"

#include "lib_path_util.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_addon.h"
#include "dune_cxt.h"
#include "dune_idprop.h"
#include "dune_screen.h"

#include "ed_asset.h"
#include "ed_keyframing.h"
#include "de_screen.h"

#include "ui.h"

#include "ui_intern.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "win_api.h"
#include "win_types.h"

/* This hack is needed because we don't have a good way to
 * re-ref keymap items once added: T42944 */
#define USE_KEYMAP_ADD_HACK

/* Btn Cxt Menu */
static IdProp *shortcut_prop_from_api(Cxt *C, Btn *btn)
{
  /* Compute data path from cxt to prop. */
  /* If this returns null, we won't be able to bind shortcuts to these api props.
   * Support can be added at win_cxt_member_from_ptr. */
  char *final_data_path = win_cxt_path_resolve_prop_full(
      C, &btn->apiptr, btn->apiprop, btn->rapiindex);
  if (final_data_path == NULL) {
    return NULL;
  }

  /* Create Id prop of data path, to pass to the op. */
  const IdPropTemplate val = {0};
  IdProp *prop = IDP_New(IDP_GROUP, &val, __func__);
  IDP_AddToGroup(prop, IDP_NewString(final_data_path, "data_path", strlen(final_data_path) + 1));

  mem_free((void *)final_data_path);

  return prop;
}

static const char *shortcut_get_op_prop(Cxt *C, Btn *btn, IdProp **r_prop)
{
  if (btn->optype) {
    /* Op */
    *r_prop = (btn->opptr && btn->opptr->data) ? IDP_CopyProp(btn->opptr->data) : NULL;
    return but->optype->idname;
  }

  if (btn->apiprop) {
    const PropType apiprop_type = api_prop_type(btn->apiprop);

    if (apiprop_type == PROP_BOOL) {
      /* Bool */
      *r_prop = shortcut_prop_from_api(C, btn);
      if (*r_prop == NULL) {
        return NULL;
      }
      return "WM_OT_cxt_toggle";
    }
    if (apiprop_type == PROP_ENUM) {
      /* Enum */
      *r_prop = shortcut_prop_from_api(C, btn);
      if (*r_prop == NULL) {
        return NULL;
      }
      return "WM_OT_cxt_menu_enum";
    }
  }

  *r_prop = NULL;
  return NULL;
}

static void shortcut_free_op_prop(IdProp *prop)
{
  if (prop) {
    IDP_FreeProp(prop);
  }
}

static void btn_shortcut_name_fn(Cxt *C, void *arg1, int UNUSED(ev))
{
  Btn *btn = (Btn *)arg1;
  char shortcut_str[128];

  IdProp *prop;
  const char *idname = shortcut_get_op_prop(C, btn, &prop);
  if (idname == NULL) {
    return;
  }

  /* complex code to change name of btn */
  if (win_key_ev_op_string(
          C, idname, btn->opcxt, prop, true, shortcut_str, sizeof(shortcut_str))) {
    btn_add_shortcut(btn, shortcut_str, true);
  }
  else {
    /* simply strip the shortcut */
    btn_add_shortcut(btn, NULL, true);
  }

  shortcut_free_op_prop(prop);
}

static uiBlock *menu_change_shortcut(Cxt *C, ARgn *rgn, void *arg)
{
  WinMngr *wam = cxt_wm(C);
  Btn *btn = (Btn *)arg;
  ApiPtr ptr;
  const uiStyle *style = ui_style_get_dpi();
  IdProp *prop;
  const char *idname = shortcut_get_op_prop(C, btn, &prop);

  WinKeyMap *km;
  WinKeyMapItem *kmi = win_key_ev_op(C,
                                     idname,
                                     btn->opcxt,
                                     prop,
                                     EV_TYPE_MASK_HOTKEY_INCLUDE,
                                     EV_TYPE_MASK_HOTKEY_EXCLUDE,
                                     &km);
  U.runtime.is_dirty = true;

  lib_assert(kmi != NULL);

  api_ptr_create(&wm->id, &ApiKeyMapItem, kmi, &ptr);

  uiBlock *block = ui_block_begin(C, rgn, "_popup", UI_EMBOSS);
  ui_block_fn_handle_set(block, btn_shortcut_name_fn, btn);
  ui_block_flag_enable(block, UI_BLOCK_MOVEMOUSE_QUIT);
  ui_block_direction_set(block, UI_DIR_CENTER_Y);

  uiLayout *layout = ui_block_layout(block,
                                     UI_LAYOUT_VERT,
                                     UI_LAYOUT_PNL,
                                     0,
                                     0,
                                     U.widget_unit * 10,
                                     U.widget_unit * 2,
                                     0,
                                     style);

  uiItemL(layout, CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Change Shortcut"), ICON_HAND);
  uiItemR(layout, &ptr, "type", UI_ITEM_R_FULL_EVENT | UI_ITEM_R_IMMEDIATE, "", ICON_NONE);

  ui_block_bounds_set_popup(
      block, 6 * U.dpi_fac, (const int[2]){-100 * U.dpi_fac, 36 * U.dpi_fac});

  shortcut_free_op_prop(prop);

  return block;
}

#ifdef USE_KEYMAP_ADD_HACK
static int g_kmi_id_hack;
#endif

static uiBlock *menu_add_shortcut(Cxt *C, ARgn *rgn, void *arg)
{
  WinMngr *wm = cxt_wm(C);
  Btn *btn = (Btn *)arg;
  ApiPtr ptr;
  const uiStyle *style = ui_style_get_dpi();
  IdProp *prop;
  const char *idname = shortcut_get_op_prop(C, btn, &prop);

  /* this guess_opname can potentially return a different keymap
   * than being found on adding later... */
  WinKeyMap *km = win_keymap_guess_opname(C, idname);
  WinKeyMapItem *kmi = win_keymap_add_item(km, idname, EV_AKEY, KM_PRESS, 0, 0, KM_ANY);
  const int kmi_id = kmi->id;

  /* This takes ownership of prop, or prop can be NULL for reset. */
  win_keymap_item_props_reset(kmi, prop);

  /* update and get pointers again */
  win_keyconfig_update(wm);
  U.runtime.is_dirty = true;

  km = win_keymap_guess_opname(C, idname);
  kmi = win_keymap_item_find_id(km, kmi_id);

  api_ptr_create(&wm->id, &ApiKeyMapItem, kmi, &ptr);

  uiBlock *block = ui_block_begin(C, rgn, "_popup", UI_EMBOSS);
  ui_block_fn_handle_set(block, btn_shortcut_name_fn, btn);
  ui_block_direction_set(block, UI_DIR_CENTER_Y);

  uiLayout *layout = ui_block_layout(block,
                                     UI_LAYOUT_VERT,
                                     UI_LAYOUT_PNL,
                                     0,
                                     0,
                                     U.widget_unit * 10,
                                     U.widget_unit * 2,
                                     0,
                                     style);

  uiItemL(layout, CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Assign Shortcut"), ICON_HAND);
  uiItemR(layout, &ptr, "type", UI_ITEM_R_FULL_EVENT | UI_ITEM_R_IMMEDIATE, "", ICON_NONE);

  ui_block_bounds_set_popup(
      block, 6 * U.dpi_fac, (const int[2]){-100 * U.dpi_fac, 36 * U.dpi_fac});

#ifdef USE_KEYMAP_ADD_HACK
  g_kmi_id_hack = kmi_id;
#endif

  return block;
}

static void menu_add_shortcut_cancel(struct Cxt *C, void *arg1)
{
  Btn *btn = (Btn *)arg1;

  IdProp *prop;
  const char *idname = shortcut_get_op_prop(C, btn, &prop);

#ifdef USE_KEYMAP_ADD_HACK
  WinKeyMap *km = win_keymap_guess_opname(C, idname);
  const int kmi_id = g_kmi_id_hack;
  UNUSED_VARS(btn);
#else
  int kmi_id = win_key_ev_op_id(C, idname, btn->opcxt, prop, true, &km);
#endif

  shortcut_free_op_prop(prop);

  WinKeyMapItem *kmi = win_keymap_item_find_id(km, kmi_id);
  win_keymap_remove_item(km, kmi);
}

static void popup_change_shortcut_fn(Cxt *C, void *arg1, void *UNUSED(arg2))
{
  Btn *btn = (Btn *)arg1;
  ui_popup_block_invoke(C, menu_change_shortcut, btn, NULL);
}

static void remove_shortcut_fn(Cxt *C, void *arg1, void *UNUSED(arg2))
{
  Btn *btn = (Btn *)arg1;
  IdProp *prop;
  const char *idname = shortcut_get_op_prop(C, btn, &prop);

  WinKeyMap *km;
  WinKeyMapItem *kmi = win_key_ev_op(C,
                                     idname,
                                     btn->opcxt,
                                     prop,
                                     EV_TYPE_MASK_HOTKEY_INCLUDE,
                                     EV_TYPE_MASK_HOTKEY_EXCLUDE,
                                     &km);
  lib_assert(kmi != NULL);

  win_keymap_remove_item(km, kmi);
  U.runtime.is_dirty = true;

  shortcut_free_op_prop(prop);
  btn_shortcut_name_fn(C, btn, 0);
}

static void popup_add_shortcut_fn(Cxt *C, void *arg1, void *UNUSED(arg2))
{
  Btn *btn = (Btn *)arg1;
  ui_popup_block_ex(C, menu_add_shortcut, NULL, menu_add_shortcut_cancel, btn, NULL);
}

static bool btn_is_user_menu_compatible(Cxt *C, Btn *btn)
{
  bool result = false;
  if (btn->optype) {
    result = true;
  }
  else if (btn->apiprop) {
    if (api_prop_type(btn->apiprop) == PROP_BOOL) {
      char *data_path = win_cxt_path_resolve_full(C, &btn->apiptr);
      if (data_path != NULL) {
        mem_free(data_path);
        result = true;
      }
    }
  }
  else if (btn_menutype_get(btn)) {
    result = true;
  }

  return result;
}

static UserMenuItem *btn_user_menu_find(Cxt *C, Btn *btn, UserMenu *um)
{
  if (btn->optype) {
    IdProp *prop = (btn->opptr) ? btn->opptr->data : NULL;
    return (UserMenuItem *)ed_screen_user_menu_item_find_op(
        &um->items, btn->optype, prop, btn->opcxt);
  }
  if (btn->apiprop) {
    char *member_id_data_path = win_cxt_path_resolve_full(C, &btn->apiptr);
    const char *prop_id = api_prop_id(btn->apiprop);
    UserMenuItem *umi = (UserMenuItem *)ed_screen_user_menu_item_find_prop(
        &um->items, member_id_data_path, prop_id, btn->apiindex);
    mem_free(member_id_data_path);
    return umi;
  }

  MenuType *mt = btn_menutype_get(btn);
  if (mt != NULL) {
    return (UserMenuItem *)ed_screen_user_menu_item_find_menu(&um->items, mt);
  }
  return NULL;
}

static void btn_user_menu_add(Cxt *C, Btn *btn, UserMenu *um)
{
  lib_assert(btn_is_user_menu_compatible(C, btn));

  char drawstr[sizeof(btn->drawstr)];
  btn_drawstr_without_sep_char(btn, drawstr, sizeof(drawstr));

  MenuType *mt = NULL;
  if (btn->optype) {
    if (drawstr[0] == '\0') {
      /* Hard code overrides for generic ops. */
      if (btn_is_tool(btn)) {
        char idname[64];
        api_string_get(btn->opptr, "name", idname);
        STRNCPY(drawstr, idname);
      }
    }
    ed_screen_user_menu_item_add_op(
        &um->items, drawstr, btn->optype, btn->opptr ? btn->opptr->data : NULL, btn->opcxt);
  }
  else if (btn->apiprop) {
    /* NOTE: 'member_id' may be a path. */
    char *member_id_data_path = win_cxt_path_resolve_full(C, &btn->apiptr);
    const char *prop_id = api_prop_id(btn->apiprop);
    /* NOTE: ignore 'drawstr', use prop idname always. */
    ed_screen_user_menu_item_add_prop(&um->items, "", member_id_data_path, prop_id, btn->apiindex);
    mem_free(member_id_data_path);
  }
  else if ((mt = btn_menutype_get(btn))) {
    ed_screen_user_menu_item_add_menu(&um->items, drawstr, mt);
  }
}

static void popup_user_menu_add_or_replace_fn(Cxt *C, void *arg1, void *UNUSED(arg2))
{
  Btn *btn = arg1;
  UserMenu *um = ed_screen_user_menu_ensure(C);
  U.runtime.is_dirty = true;
  btn_user_menu_add(C, btn, um);
}

static void popup_user_menu_remove_fn(Cxt *UNUSED(C), void *arg1, void *arg2)
{
  UserMenu *um = arg1;
  UserMenuItem *umi = arg2;
  U.runtime.is_dirty = true;
  ed_screen_user_menu_item_remove(&um->items, umi);
}

static void btn_menu_add_path_ops(uiLayout *layout, ApiPtr *ptr, ApiProp *prop)
{
  const PropSubType subtype = api_prop_subtype(prop);
  WinOpType *ot = win_optype_find("WM_OT_path_open", true);
  char filepath[FILE_MAX];
  char dir[FILE_MAXDIR];
  char file[FILE_MAXFILE];
  ApiPtr props_ptr;

  lib_assert(ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH));
  UNUSED_VARS_NDEBUG(subtype);

  api_prop_string_get(ptr, prop, filepath);
  lib_split_dirfile(filepath, dir, file, sizeof(dir), sizeof(file));

  if (file[0]) {
    lib_assert(subtype == PROP_FILEPATH);
    uiItemFullO_ptr(layout,
                    ot,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Open File Externally"),
                    ICON_NONE,
                    NULL,
                    WIN_OP_INVOKE_DEFAULT,
                    0,
                    &props_ptr);
    api_string_set(&props_ptr, "filepath", filepath);
  }

  uiItemFullO_ptr(layout,
                  ot,
                  CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Open Location Externally"),
                  ICON_NONE,
                  NULL,
                  WIN_OP_INVOKE_DEFAULT,
                  0,
                  &props_ptr);
  api_string_set(&props_ptr, "filepath", dir);
}

bool ui_popup_cxt_menu_for_btn(Cxt *C, Btn *btn, const WinEv *ev)
{
  /* btn_is_interactive() may let some btns through that should not get a cxt menu - it
   * doesn't make sense for them. */
  if (ELEM(btn->type, BTYPE_LABEL, BTYPE_IMAGE)) {
    return false;
  }

  uiPopupMenu *pup;
  uiLayout *layout;
  CxtStore *previous_cxt = cxt_store_get(C);
  {
    uiStringInfo label = {BTN_GET_LABEL, NULL};

    /* highly unlikely getting the label ever fails */
    btn_string_info_get(C, btn, &label, NULL);

    pup = ui_popup_menu_begin(C, label.strinfo ? label.strinfo : "", ICON_NONE);
    layout = ui_popup_menu_layout(pup);
    if (label.strinfo) {
      mem_free(label.strinfo);
    }

    if (btn->cxt) {
      uiLayoutCxtCopy(layout, btn->cxt);
      cxt_store_set(C, uiLayoutGetCxtStore(layout));
    }
    uiLayoutSetOpCxt(layout, WIN_OP_INVOKE_DEFAULT);
  }

  const bool is_disabled = btn->flag & BTN_DISABLED;

  if (is_disabled) {
    /* Suppress editing commands. */
  }
  else if (btn->type == BTYPE_TAB) {
    BtnTab *tab = (BtnTab *)btn;
    if (tab->menu) {
      ui_menutype_draw(C, tab->menu, layout);
      uiItemS(layout);
    }
  }
  else if (btn->apiptr.data && btn->apiprop) {
    ApiPtr *ptr = &btn->apiptr;
    ApiProp *prop = btn->apiprop;
    const PropType type = api_prop_type(prop);
    const PropSubType subtype = a_prop_subtype(prop);
    bool is_anim = api_prop_animateable(ptr, prop);
    const bool is_idprop = api_prop_is_idprop(prop);

    /* second slower test,
     * saved people finding keyframe items in menus when its not possible */
    if (is_anim) {
      is_anim = api_prop_path_from_id_check(&btn->apiptr, btn->apiprop);
    }

    /* determine if we can key a single component of an array */
    const bool is_array = api_prop_array_length(&btn->apiptr, btn->apiprop) != 0;
    const bool is_array_component = (is_array && btn->apiindex != -1);
    const bool is_whole_array = (is_array && btn->apiindex == -1);

    const uint override_status = api_prop_override_lib_status(
        cxt_data_main(C), ptr, prop, -1);
    const bool is_overridable = (override_status & API_OVERRIDE_STATUS_OVERRIDABLE) != 0;

    /* Set the (btn_ptr, btn_prop)
     * and ptr data for Python access to the hovered UI element. */
    uiLayoutSetCxtFromBtn(layout, btn);

    /* Keyframes */
    if (btn->flag & BTN_ANIMATED_KEY) {
      /* Replace/delete keyframes. */
      if (is_array_component) {
        uiItemBoolO(layout,
                   CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Replace Keyframes"),
                   ICON_KEY_HLT,
                   "ANIM_OT_keyframe_insert_btn",
                   "all",
                   1);
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Replace Single Keyframe"),
                    ICON_NONE,
                    "ANIM_OT_keyframe_insert_btn",
                    "all",
                    0);
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Delete Keyframes"),
                    ICON_NONE,
                    "ANIM_OT_keyframe_delete_btn",
                    "all",
                    1);
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Delete Single Keyframe"),
                    ICON_NONE,
                    "ANIM_OT_keyframe_delete_btn",
                    "all",
                    0);
      }
      else {
        uiItemBoolO(layout,
                   CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Replace Keyframe"),
                   ICON_KEY_HLT,
                   "ANIM_OT_keyframe_insert_btn",
                   "all",
                   1);
        uiItemBoolO(layout,
                   CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Delete Keyframe"),
                   ICON_NONE,
                  "ANIM_OT_keyframe_delete_btn",
                       "all",
                       1);
      }

      /* keyframe settings */
      uiItemS(layout);
    }
    else if (btn->flag & BTN_DRIVEN) {
      /* pass */
    }
    else if (is_anim) {
      if (is_array_component) {
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Insert Keyframes"),
                    ICON_KEY_HLT,
                    "ANIM_OT_keyframe_insert_btn",
                    "all",
                    1);
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Insert Single Keyframe"),
                    ICON_NONE,
                    "ANIM_OT_keyframe_insert_btn",
                    "all",
                    0);
      }
      else {
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Insert Keyframe"),
                    ICON_KEY_HLT,
                    "ANIM_OT_keyframe_insert_btn",
                    "all",
                    1);
      }
    }

    if ((btn->flag & BTN_ANIMATED) && (btn->apiptr.type != &ApiNlaStrip)) {
      if (is_array_component) {
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Clear Keyframes"),
                    ICON_KEY_DEHLT,
                    "ANIM_OT_keyframe_clear_btn",
                    "all",
                    1);
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Clear Single Keyframes"),
                    ICON_NONE,
                    "ANIM_OT_keyframe_clear_btn",
                    "all",
                    0);
      }
      else {
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Clear Keyframes"),
                    ICON_KEY_DEHLT,
                    "ANIM_OT_keyframe_clear_btn",
                    "all",
                    1);
      }
    }

    /* Drivers */
    if (btn->flag & BTN_DRIVEN) {
      uiItemS(layout);

      if (is_array_component) {
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Delete Drivers"),
                    ICON_X,
                    "ANIM_OT_driver_btn_remove",
                    "all",
                    1);
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Delete Single Driver"),
                    ICON_NONE,
                    "ANIM_OT_driver_btn_remove",
                    "all",
                    0);
      }
      else {
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Delete Driver"),
                    ICON_X,
                    "ANIM_OT_driver_btn_remove",
                    "all",
                    1);
      }

      if (!is_whole_array) {
        uiItemO(layout,
                CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Copy Driver"),
                ICON_NONE,
                "ANIM_OT_copy_driver_btn");
        if (anim_driver_can_paste()) {
          uiItemO(layout,
                  CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Paste Driver"),
                  ICON_NONE,
                  "ANIM_OT_paste_driver_btn");
        }

        uiItemO(layout,
                CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Edit Driver"),
                ICON_DRIVER,
                "ANIM_OT_driver_btn_edit");
      }

      uiItemO(layout,
              CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Open Drivers Editor"),
              ICON_NONE,
              "SCREEN_OT_drivers_editor_show");
    }
    else if (btn->flag & (BTN_ANIMATED_KEY | BTN_ANIMATED)) {
      /* pass */
    }
    else if (is_anim) {
      uiItemS(layout);

      uiItemO(layout,
              CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Add Driver"),
              ICON_DRIVER,
              "ANIM_OT_driver_btn_add");

      if (!is_whole_array) {
        if (anim_driver_can_paste()) {
          uiItemO(layout,
                  CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Paste Driver"),
                  ICON_NONE,
                  "ANIM_OT_paste_driver_btn");
        }
      }

      uiItemO(layout,
              CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Open Drivers Editor"),
              ICON_NONE,
              "SCREEN_OT_drivers_editor_show");
    }

    /* Keying Sets */
    /* Check on modifyability of Keying Set when doing this */
    if (is_anim) {
      uiItemS(layout);

      if (is_array_component) {
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Add All to Keying Set"),
                    ICON_KEYINGSET,
                    "ANIM_OT_keyingset_btn_add",
                    "all",
                    1);
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Add Single to Keying Set"),
                    ICON_NONE,
                    "ANIM_OT_keyingset_btn_add",
                    "all",
                    0);
        uiItemO(layout,
                CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Remove from Keying Set"),
                ICON_NONE,
                "ANIM_OT_keyingset_btn_remove");
      }
      else {
        uiItemBoolO(layout,
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Add to Keying Set"),
                    ICON_KEYINGSET,
                    "ANIM_OT_keyingset_btn_add",
                    "all",
                    1);
        uiItemO(layout,
                CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Remove from Keying Set"),
                ICON_NONE,
                "ANIM_OT_keyingset_btn_remove");
      }
    }

    if (is_overridable) {
      WinOpType *ot;
      ApiPtr op_ptr;
      /* Override Ops */
      uiItemS(layout);

      if (btn->flag & BTN_OVERRIDDEN) {
        if (is_array_component) {
#if 0 /* Disabled for now. */
          ot = win_optype_find("UI_OT_override_type_set_btn", false);
          uiItemFullO_ptr(
              layout, ot, "Overrides Type", ICON_NONE, NULL, WIN_OP_INVOKE_DEFAULT, 0, &op_ptr);
          api_bool_set(&op_ptr, "all", true);
          uiItemFullO_ptr(layout,
                          ot,
                          "Single Override Type",
                          ICON_NONE,
                          NULL,
                          WIN_OP_INVOKE_DEFAULT,
                          0,
                          &op_ptr);
          api_bool_set(&op_ptr, "all", false);
#endif
          uiItemBoolO(layout,
                      CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Remove Overrides"),
                      ICON_X,
                      "UI_OT_override_remove_btn",
                      "all",
                      true);
          uiItemBoolO(layout,
                      CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Remove Single Override"),
                      ICON_X,
                      "UI_OT_override_remove_btn",
                      "all",
                      false);
        }
        else {
#if 0 /* Disabled for now. */
          uiItemFullO(layout,
                      "UI_OT_override_type_set_button",
                      "Override Type",
                      ICON_NONE,
                      NULL,
                      WIN_OP_INVOKE_DEFAULT,
                      0,
                      &op_ptr);
          api_bool_set(&op_ptr, "all", false);
#endif
          uiItemBoolO(layout,
                      CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Remove Override"),
                      ICON_X,
                      "UI_OT_override_remove_btn",
                      "all",
                      true);
        }
      }
      else {
        if (is_array_component) {
          ot = win_optype_find("UI_OT_override_type_set_btn", false);
          uiItemFullO_ptr(layout,
                          ot,
                          CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Define Overrides"),
                          ICON_NONE,
                          NULL,
                          WIN_OP_INVOKE_DEFAULT,
                          0,
                          &op_ptr);
          api_bool_set(&op_ptr, "all", true);
          uiItemFullO_ptr(layout,
                          ot,
                          CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Define Single Override"),
                          ICON_NONE,
                          NULL,
                          WIN_OP_INVOKE_DEFAULT,
                          0,
                          &op_ptr);
          api_bool_set(&op_ptr, "all", false);
        }
        else {
          uiItemFullO(layout,
                      "UI_OT_override_type_set_btn",
                      CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Define Override"),
                      ICON_NONE,
                      NULL,
                      WIN_OP_INVOKE_DEFAULT,
                      0,
                      &op_ptr);
          api_bool_set(&op_ptr, "all", false);
        }
      }
    }

    uiItemS(layout);

    /* Prop Ops */
    /* Copy Prop Value
     * Paste Prop Value */
    if (is_array_component) {
      uiItemBoolO(layout,
                  CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Reset All to Default Values"),
                  ICON_LOOP_BACK,
                  "UI_OT_reset_default_btn",
                  "all",
                  1);
      uiItemBoolO(layout,
                  CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Reset Single to Default Value"),
                  ICON_NONE,
                  "UI_OT_reset_default_btn",
                  "all",
                  0);
    }
    else {
      uiItemBoolO(layout,
                  CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Reset to Default Value"),
                  ICON_LOOP_BACK,
                  "UI_OT_reset_default_btn",
                  "all",
                  1);
    }

    if (is_idprop && !is_array && ELEM(type, PROP_INT, PROP_FLOAT)) {
      uiItemO(layout,
              CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Assign Value as Default"),
              ICON_NONE,
              "UI_OT_assign_default_btn");

      uiItemS(layout);
    }

    if (is_array_component) {
      uiItemBoolO(layout,
                  CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Copy All to Selected"),
                  ICON_NONE,
                  "UI_OT_copy_to_selected_btn",
                  "all",
                  true);
      uiItemBoolO(layout,
                  CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Copy Single to Selected"),
                  ICON_NONE,
                  "UI_OT_copy_to_selected_btn",
                  "all",
                  false);
    }
    else {
      uiItemBoolO(layout,
                  CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Copy to Selected"),
                  ICON_NONE,
                  "UI_OT_copy_to_selected_btn",
                  "all",
                  true);
    }

    uiItemO(layout,
            CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Copy Data Path"),
            ICON_NONE,
            "UI_OT_copy_data_path_btn");
    uiItemBoolO(layout,
                CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Copy Full Data Path"),
                ICON_NONE,
                "UI_OT_copy_data_path_btn",
                "full_path",
                true);

    if (ptr->owner_id && !is_whole_array &&
        ELEM(type, PROP_BOOL, PROP_INT, PROP_FLOAT, PROP_ENUM)) {
      uiItemO(layout,
              CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Copy as New Driver"),
              ICON_NONE,
              "UI_OT_copy_as_driver_btn");
    }

    uiItemS(layout);

    if (type == PROP_STRING && ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH)) {
      btn_menu_add_path_ops(layout, ptr, prop);
      uiItemS(layout);
    }
  }

  {
    const ARgn *rgn = cxt_win_menu(C) ? cxt_win_menu(C) : cxt_win_rgn(C);
    BtnTreeRow *treerow_btn = (BtnTreeRow *)ui_tree_row_find_mouse_over(rgn, ev->xy);
    if (treerow_btn) {
      lib_assert(treerow_btn->btn.type == BTYPE_TREEROW);
      ui_tree_view_item_cxt_menu_build(
          C, treerow_btn->tree_item, uiLayoutColumn(layout, false));
      uiItemS(layout);
    }
  }

  /* If the btn represents an id, it can set the "id" cxt ptr */
  if (ed_asset_can_mark_single_from_cxt(C)) {
    Id *id = cxt_data_ptr_get_type(C, "id", &ApiId).data;

    /* Gray out items depending on if data-block is an asset. Preferably this could be done via
     * op poll, but that doesn't work since the op also works with "selected_ids",
     * which isn't cheap to check. */
    uiLayout *sub = uiLayoutColumn(layout, true);
    uiLayoutSetEnabled(sub, !id->asset_data);
    uiItemO(sub, NULL, ICON_NONE, "ASSET_OT_mark");
    sub = uiLayoutColumn(layout, true);
    uiLayoutSetEnabled(sub, id->asset_data);
    uiItemO(sub, NULL, ICON_NONE, "ASSET_OT_clear");
    uiItemS(layout);
  }

  /* Ptr props and string props with
   * prop_search support jumping to target object/bone. */
  if (btn->apiptr.data && btn->apiprop) {
    const PropType prop_type = api_prop_type(btn->apiprop);
    if (((prop_type == PROP_PTR) ||
         (prop_type == PROP_STRING && btn->type == BTYPE_SEARCH_MENU &&
          ((BtnSearch *)btn)->items_update_fn == ui_api_collection_search_update_fn)) &&
        ui_jump_to_target_btn_poll(C)) {
      uiItemO(layout,
              CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Jump to Target"),
              ICON_NONE,
              "UI_OT_jump_to_target_btn");
      uiItemS(layout);
    }
  }

  /* Favorites Menu */
  if (btn_is_user_menu_compatible(C, btn)) {
    uiBlock *block = uiLayoutGetBlock(layout);
    const int w = uiLayoutGetWidth(layout);
    bool item_found = false;

    uint um_array_len;
    UserMenu **um_array = ed_screen_user_menus_find(C, &um_array_len);
    for (int um_index = 0; um_index < um_array_len; um_index++) {
      UserMenu *um = um_array[um_index];
      if (um == NULL) {
        continue;
      }
      UserMenuItem *umi = btn_user_menu_find(C, btn, um);
      if (umi != NULL) {
        Btn *btn2 = uiDefIconTextBtn(
            block,
            BTYPE_BTN,
            0,
            ICON_MENU_PANEL,
            CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Remove from Quick Favorites"),
            0,
            0,
            w,
            UI_UNIT_Y,
            NULL,
            0,
            0,
            0,
            0,
            "");
        btn_fn_set(btn2, popup_user_menu_remove_fn, um, umi);
        item_found = true;
      }
    }
    if (um_array) {
      mem_free(um_array);
    }

    if (!item_found) {
      Btn *btn2 = uiDefIconTxtBtn(
          block,
          BTYPE_BTN,
          0,
          ICON_MENU_PANEL,
          CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Add to Quick Favorites"),
          0,
          0,
          w,
          UI_UNIT_Y,
          NULL,
          0,
          0,
          0,
          0,
          "Add to a user defined cxt menu (stored in the user prefs)");
      btn_fn_set(btn2, popup_user_menu_add_or_replace_fn, btn, NULL);
    }

    uiItemS(layout);
  }

  /* Shortcut menu */
  IdProp *prop;
  const char *idname = shortcut_get_op_prop(C, btn, &prop);
  if (idname != NULL) {
    uiBlock *block = uiLayoutGetBlock(layout);
    const int w = uiLayoutGetWidth(layout);

    /* We want to know if this op has a shortcut, be it hotkey or not. */
    WinKeyMap *km;
    WinKeyMapItem *kmi = win_key_ev_op(
        C, idname, btn->opcxt, prop, EV_TYPE_MASK_ALL, 0, &km);

    /* We do have a shortcut, but only keyboard ones are editable that way... */
    if (kmi) {
      if (ISKEYBOARD(kmi->type) || ISNDOF_BTN(kmi->type)) {
#if 0 /* would rather use a block btn, btn gets weirdly positioned... */
        uiDefBlockBtn(block,
                      menu_change_shortcut,
                      btn,
                      "Change Shortcut",
                      0,
                      0,
                      uiLayoutGetWidth(layout),
                      UI_UNIT_Y,
                      "");
#endif

        Btn *btn2 = uiDefIconTxtBtn(
            block,
            BTYPE_BTN,
            0,
            ICON_HAND,
            CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Change Shortcut"),
            0,
            0,
            w,
            UI_UNIT_Y,
            NULL,
            0,
            0,
            0,
            0,
            "");
        btn_fn_set(btn2, popup_change_shortcut_fn, btn, NULL);
      }
      else {
        Btn *btn2 = BtnDefIconTxt(block,
                                  BTYPE_BTN,
                                  0,
                                  ICON_HAND,
                                  IFACE_("Non-Keyboard Shortcut"),
                                  0,
                                  0,
                                  w,
                                  UI_UNIT_Y,
                                  NULL,
                                  0,
                                  0,
                                  0,
                                  0,
                                  TIP_("Only keyboard shortcuts can be edited that way, "
                                       "please use User Prefs otherwise"));
        btn_flag_enable(btn2, BTN_DISABLED);
      }

      Btn *btn2 = BtnDefIconTxt(
          block,
          BTYPE_BTN,
          0,
          ICON_BLANK1,
          CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Remove Shortcut"),
          0,
          0,
          w,
          UI_UNIT_Y,
          NULL,
          0,
          0,
          0,
          0,
          "");
      btn_fn_set(btn2, remove_shortcut_fn, btn, NULL);
    }
    /* only show 'assign' if there's a suitable key map for it to go in */
    else if (win_keymap_guess_opname(C, idname)) {
      Btn *btn2 = BtnDefIconTxt(
          block,
          BTYPE_BTN,
          0,
          ICON_HAND,
          CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Assign Shortcut"),
          0,
          0,
          w,
          UI_UNIT_Y,
          NULL,
          0,
          0,
          0,
          0,
          "");
      btn_fn_set(btn2, popup_add_shortcut_fn, btn, NULL);
    }

    shortcut_free_op_prop(prop);

    /* Set the op ptr for python access */
    uiLayoutSetCxtFromBtn(layout, btn);

    uiItemS(layout);
  }

  { /* Docs */
    char buf[512];

    if (btn_online_manual_id(btn, buf, sizeof(buf))) {
      ApiPtr ptr_props;
      uiItemO(layout,
              CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Online Manual"),
              ICON_URL,
              "WIN_OT_doc_view_manual_ui_cxt");

      if (U.flag & USER_DEVELOPER_UI) {
        uiItemFullO(layout,
                    "WIN_OT_doc_view",
                    CXT_IFACE_(LANG_CXT_OP_DEFAULT, "Online Python Reference"),
                    ICON_NONE,
                    NULL,
                    WIN_OP_EX_DEFAULT,
                    0,
                    &ptr_props);
        api_string_set(&ptr_props, "doc_id", buf);
      }
    }
  }

  if (btn->optype && U.flag & USER_DEVELOPER_UI) {
    uiItemO(layout, NULL, ICON_NONE, "UI_OT_copy_python_command_btn");
  }

  /* perhaps we should move this into (G.debug & G_DEBUG) */
  if (U.flag & USER_DEVELOPER_UI) {
    if (ui_block_is_menu(btn->block) == false) {
      uiItemFullO(
          layout, "UI_OT_editsource", NULL, ICON_NONE, NULL, WIN_OP_INVOKE_DEFAULT, 0, NULL);
    }
  }

  if (dune_addon_find(&U.addons, "ui_lang")) {
    uiItemFullO(layout,
                "UI_OT_editlang_init",
                NULL,
                ICON_NONE,
                NULL,
                WIN_OP_INVOKE_DEFAULT,
                0,
                NULL);
  }

  /* Show header tools for header btns. */
  if (ui_block_is_popup_any(btn->block) == false) {
    const ARgn *rgn = cxt_win_rgn(C);

    if (!rgn) {
      /* skip */
    }
    else if (ELEM(rgn->rgntype, RGN_TYPE_HEADER, RGN_TYPE_TOOL_HEADER)) {
      uiItemMenuF(layout, IFACE_("Header"), ICON_NONE, ed_screens_header_tools_menu_create, NULL);
    }
    else if (rgn->rgntype == RGN_TYPE_NAV_BAR) {
      uiItemMenuF(layout,
                  IFACE_("Nav Bar"),
                  ICON_NONE,
                  ed_screens_nav_bar_tools_menu_create,
                  NULL);
    }
    else if (rgn->rgntype == RGN_TYPE_FOOTER) {
      uiItemMenuF(layout, IFACE_("Footer"), ICON_NONE, ed_screens_footer_tools_menu_create, NULL);
    }
  }

  /* UI List item cxt menu. Scripts can add items to it, by default there's nothing shown. */
  const ARgn *rgn = cxt_win_menu(C) ? cxt_win_menu(C) : cxt_win_rgn(C);
  const bool is_inside_listbox = ui_list_find_mouse_over(rgn, ev) != NULL;
  const bool is_inside_listrow = is_inside_listbox ?
                                     ui_list_row_find_mouse_over(rgn, ev->xy) != NULL :
                                     false;
  if (is_inside_listrow) {
    MenuType *mt = win_menutype_find("ui_mt_list_item_cxt_menu", true);
    if (mt) {
      ui_menutype_drw(C, mt, uiLayoutColumn(layout, false));
    }
  }

  MenuType *mt = win_menutype_find("WIN_MT_btn_cxt", true);
  if (mt) {
    ui_menutype_draw(C, mt, uiLayoutColumn(layout, false));
  }

  if (btn->cxt) {
    cxt_store_set(C, previous_cxt);
  }

  return ui_popup_menu_end_or_cancel(C, pup);
}

/* Panel Cxt Menu */
void ui_popup_cxt_menu_for_pnl(Cxt *C, ARgn *rgn, Pnl *pnl)
{
  Screen *screen = cxt_win_screen(C);
  const bool has_pnl_category = ui_pnl_category_is_visible(rgn);
  const bool any_item_visible = has_pnl_category;

  if (!any_item_visible) {
    return;
  }
  if (pnl->type->parent != NULL) {
    return;
  }
  if (!ui_pnl_can_be_pinned(pnl)) {
    return;
  }

  ApiPtr ptr;
  api_ptr_create(&screen->id, &ApiPnl, pnl, &ptr);

  uiPopupMenu *pup = ui_popup_menu_begin(C, IFACE_("Pnl"), ICON_NONE);
  uiLayout *layout = ui_popup_menu_layout(pup);

  if (has_pnl_category) {
    char tmpstr[80];
    li _snprintf(tmpstr,
                 sizeof(tmpstr),
                 "%s" UI_SEP_CHAR_S "%s",
                 IFACE_("Pin"),
                 IFACE_("Shift Left Mouse"));
    uiItemR(layout, &ptr, "use_pin", 0, tmpstr, ICON_NONE);

    /* evil, force shortcut flag */
    {
      uiBlock *block = uiLayoutGetBlock(layout);
      Btn *btn = block->btns.last;
      btn->flag |= BTN_HAS_SEP_CHAR;
    }
  }
  ui_popup_menu_end(C, pup);
}
