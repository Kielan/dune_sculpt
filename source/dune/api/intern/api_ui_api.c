#include <stdio.h>
#include <stdlib.h>

#include "lib_utildefines.h"

#include "lang.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "types_screen.h"

#include "ui.h"
#include "ui_icons.h"
#include "ui_resources.h"

#include "api_internal.h"

#define DEF_ICON(name) {ICON_##name, (#name), 0, (#name), ""},
#define DEF_ICON_VECTOR(name) {ICON_##name, (#name), 0, (#name), ""},
#define DEF_ICON_COLOR(name) {ICON_##name, (#name), 0, (#name), ""},
#define DEF_ICON_BLANK(name)
const EnumPropItem api_enum_icon_items[] = {
#include "ui_icons.h"
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "types_asset.h"

const char *api_translate_ui_text(
    const char *text, const char *text_cxt, ApiStruct *type, ApiProp *prop, bool translate)
{
  /* Also return text if UI labels translation is disabled. */
  if (!text || !text[0] || !translate || !lang_translate_iface()) {
    return text;
  }

  /* If a text_ctxt is specified, use it! */
  if (text_cxt && text_cxt[0]) {
    return lang_pgettext(text_cxt, text);
  }

  /* Else, if an api type or prop is specified, use its context. */
#  if 0
  /* XXX Disabled for now. Unfortunately, their is absolutely no way from py code to get the RNA
   *     struct corresponding to the 'data' (in fns like prop() & co),
   *     as this is pure runtime data. Hence, messages extraction script can't determine the
   *     correct cxt it should use for such 'text' messages...
   *     So for now, one have to explicitly specify the 'text_cxt' when using prop() etc.
   *     functions, if default context is not suitable.  */
  if (prop) {
    return lang_pgettext(api_prop_translation_cxt(prop), text);
  }
#  else
  (void)prop;
#  endif
  if (type) {
    return lang_pgettext(api_struct_translation_cxt(type), text);
  }

  /* Else, default context! */
  return lang_pgettext(LANG_CXT_DEFAULT, text);
}

static void api_uiItemR(uiLayout *layout,
                        ApiPtr *ptr,
                        const char *propname,
                        const char *name,
                        const char *text_cxt,
                        bool translate,
                        int icon,
                        bool expand,
                        bool slider,
                        int toggle,
                        bool icon_only,
                        bool event,
                        bool full_event,
                        bool emboss,
                        int index,
                        int icon_value,
                        bool invert_checkbox)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);
  int flag = 0;

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }

  if (icon_value && !icon) {
    icon = icon_value;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, prop, translate);

  flag |= (slider) ? UI_ITEM_R_SLIDER : 0;
  flag |= (expand) ? UI_ITEM_R_EXPAND : 0;
  if (toggle == 1) {
    flag |= UI_ITEM_R_TOGGLE;
  }
  else if (toggle == 0) {
    flag |= UI_ITEM_R_ICON_NEVER;
  }
  flag |= (icon_only) ? UI_ITEM_R_ICON_ONLY : 0;
  flag |= (event) ? UI_ITEM_R_EVENT : 0;
  flag |= (full_event) ? UI_ITEM_R_FULL_EVENT : 0;
  flag |= (emboss) ? 0 : UI_ITEM_R_NO_BG;
  flag |= (invert_checkbox) ? UI_ITEM_R_CHECKBOX_INVERT : 0;

  uiItemFullR(layout, ptr, prop, index, 0, flag, name, icon);
}

static void api_uiItemR_with_popover(uiLayout *layout,
                                     struct ApiPtr *ptr,
                                     const char *propname,
                                     const char *name,
                                     const char *text_cxt,
                                     bool translate,
                                     int icon,
                                     bool icon_only,
                                     const char *panel_type)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }
  if ((api_prop_type(prop) != PROP_ENUM) &&
      !ELEM(api_prop_subtype(prop), PROP_COLOR, PROP_COLOR_GAMMA))
  {
    api_warning(
        "prop is not an enum or color: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }
  int flag = 0;

  flag |= (icon_only) ? UI_ITEM_R_ICON_ONLY : 0;

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, prop, translate);
  uiItemFullR_with_popover(layout, ptr, prop, -1, 0, flag, name, icon, panel_type);
}

static void api_uiItemR_with_menu(uiLayout *layout,
                                  struct ApiPtr *ptr,
                                  const char *propname,
                                  const char *name,
                                  const char *text_cxt,
                                  bool translate,
                                  int icon,
                                  bool icon_only,
                                  const char *menu_type)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }
  if (api_prop_type(prop) != PROP_ENUM) {
    api_warning("prop is not an enum: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }
  int flag = 0;

  flag |= (icon_only) ? UI_ITEM_R_ICON_ONLY : 0;

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, prop, translate);
  uiItemFullR_with_menu(layout, ptr, prop, -1, 0, flag, name, icon, menu_type);
}

static void api_uiItemMenuEnumR(uiLayout *layout,
                                struct ApiPtr *ptr,
                                const char *propname,
                                const char *name,
                                const char *text_cxt,
                                bool translate,
                                int icon)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, prop, translate);
  uiItemMenuEnumR_prop(layout, ptr, prop, name, icon);
}

static void api_uiItemTabsEnumR(uiLayout *layout,
                                Cxt *C,
                                struct ApiPtr *ptr,
                                const char *propname,
                                struct ApiPtr *ptr_highlight,
                                const char *propname_highlight,
                                bool icon_only)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }
  if (api_prop_type(prop) != PROP_ENUM) {
    api_warning("prop is not an enum: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }

  /* Get the highlight property used to gray out some of the tabs. */
  ApiProp *prop_highlight = NULL;
  if (!api_ptr_is_null(ptr_highlight)) {
    prop_highlight = api_struct_find_prop(ptr_highlight, propname_highlight);
    if (!prop_highlight) {
      api_warning("prop not found: %s.%s",
                  api_struct_id(ptr_highlight->type),
                  propname_highlight);
      return;
    }
    if (api_prop_type(prop_highlight) != PROP_BOOL) {
      api_warning("prop is not a bool: %s.%s",
                  api_struct_id(ptr_highlight->type),
                  propname_highlight);
      return;
    }
    if (!api_prop_array_check(prop_highlight)) {
      api_warning("prop is not an array: %s.%s",
                  api_struct_id(ptr_highlight->type),
                  propname_highlight);
      return;
    }
  }

  uiItemTabsEnumR_prop(layout, C, ptr, prop, ptr_highlight, prop_highlight, icon_only);
}

static void api_uiItemEnumR_string(uiLayout *layout,
                                   struct ApiPtr *ptr,
                                   const char *propname,
                                   const char *value,
                                   const char *name,
                                   const char *text_cxt,
                                   bool translate,
                                   int icon)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, prop, translate);

  uiItemEnumR_string_prop(layout, ptr, prop, value, name, icon);
}

static void api_uiItemPtrR(uiLayout *layout,
                           struct ApiPtr *ptr,
                           const char *propname,
                           struct ApiPtr *searchptr,
                           const char *searchpropname,
                           const char *name,
                           const char *text_cxt,
                           bool translate,
                           int icon,
                           const bool results_are_suggestions)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);
  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }
  ApiProp *searchprop = api_struct_find_prop(searchptr, searchpropname);
  if (!searchprop) {
    api_warning(
        "prop not found: %s.%s", api_struct_id(searchptr->type), searchpropname);
    return;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, prop, translate);

  uiItemPtrR_prop(
      layout, ptr, prop, searchptr, searchprop, name, icon, results_are_suggestions);
}

static ApiPtr api_uiItemO(uiLayout *layout,
                          const char *opname,
                          const char *name,
                          const char *text_cxt,
                          bool translate,
                          int icon,
                          bool emboss,
                          bool depress,
                          int icon_value)
{
  wmOpType *ot;

  ot = wm_optype_find(opname, false); /* print error next */
  if (!ot || !ot->sapi) {
    api_warning("%s '%s'", ot ? "unknown op" : "op missing sapi", opname);
    return ApiPtr_NULL;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, ot->sapi, NULL, translate);

  if (icon_value && !icon) {
    icon = icon_value;
  }
  int flag = (emboss) ? 0 : UI_ITEM_R_NO_BG;
  flag |= (depress) ? UI_ITEM_O_DEPRESS : 0;

  ApiPtr opptr;
  uiItemFullO_ptr(layout, ot, name, icon, NULL, uiLayoutGetOpCxt(layout), flag, &opptr);
  return opptr;
}

static ApiPtr api_uiItemOMenuHold(uiLayout *layout,
                                 const char *opname,
                                 const char *name,
                                 const char *text_cxt,
                                 bool translate,
                                 int icon,
                                 bool emboss,
                                 bool depress,
                                 int icon_value,
                                 const char *menu)
{
  wmOpType *ot = wm_optype_find(opname, false); /* print error next */
  if (!ot || !ot->sapi) {
    api_warning("%s '%s'", ot ? "unknown op" : "op missing sapi", opname);
    return ApiPtr_NULL;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, ot->sapi, NULL, translate);
  if (icon_value && !icon) {
    icon = icon_value;
  }
  int flag = (emboss) ? 0 : UI_ITEM_R_NO_BG;
  flag |= (depress) ? UI_ITEM_O_DEPRESS : 0;

  ApiPtr opptr;
  uiItemFullOMenuHold_ptr(
      layout, ot, name, icon, NULL, uiLayoutGetOpCxt(layout), flag, menu, &opptr);
  return opptr;
}

static void api_uiItemsEnumO(uiLayout *layout,
                             const char *opname,
                             const char *propname,
                             const bool icon_only)
{
  int flag = icon_only ? UI_ITEM_R_ICON_ONLY : 0;
  uiItemsFullEnumO(layout, opname, propname, NULL, uiLayoutGetOpCxt(layout), flag);
}

static ApiPtr api_uiItemMenuEnumO(uiLayout *layout,
                                  Cxt *C,
                                  const char *opname,
                                  const char *propname,
                                  const char *name,
                                  const char *text_cxt,
                                  bool translate,
                                  int icon)
{
  wmOpType *ot = wm_optype_find(opname, false); /* print error next */

  if (!ot || !ot->sapi) {
    api_warning("%s '%s'", ot ? "unknown operator" : "operator missing srna", opname);
    return ApiPtr_NULL;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, ot->sapi, NULL, translate);

  ApiPtr opptr;
  uiItemMenuEnumFullO_ptr(layout, C, ot, propname, name, icon, &opptr);
  return opptr;
}

static void api_uiItemL(uiLayout *layout,
                        const char *name,
                        const char *text_cxt,
                        bool translate,
                        int icon,
                        int icon_value)
{
  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, NULL, translate);

  if (icon_value && !icon) {
    icon = icon_value;
  }

  uiItemL(layout, name, icon);
}

static void api_uiItemM(uiLayout *layout,
                        const char *menuname,
                        const char *name,
                        const char *text_cxt,
                        bool translate,
                        int icon,
                        int icon_value)
{
  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, NULL, translate);

  if (icon_value && !icon) {
    icon = icon_value;
  }

  uiItemM(layout, menuname, name, icon);
}

static void api_uiItemM_contents(uiLayout *layout, const char *menuname)
{
  uiItemMContents(layout, menuname);
}

static void api_uiItemPopoverPanel(uiLayout *layout,
                                   Cxt *C,
                                   const char *panel_type,
                                   const char *name,
                                   const char *text_cxt,
                                   bool translate,
                                   int icon,
                                   int icon_value)
{
  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, NULL, translate);

  if (icon_value && !icon) {
    icon = icon_value;
  }

  uiItemPopoverPanel(layout, C, panel_type, name, icon);
}

static void api_uiItemPopoverPanelFromGroup(uiLayout *layout,
                                            Cxt *C,
                                            int space_id,
                                            int region_id,
                                            const char *cxt,
                                            const char *category)
{
  uiItemPopoverPanelFromGroup(layout, C, space_id, region_id, cxt, category);
}

static void api_uiTemplateId(uiLayout *layout,
                             Cxt *C,
                             ApiPtr *ptr,
                             const char *propname,
                             const char *newop,
                             const char *openop,
                             const char *unlinkop,
                             int filter,
                             const bool live_icon,
                             const char *name,
                             const char *text_cxt,
                             bool translate)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, prop, translate);

  uiTemplateId(layout, C, ptr, propname, newop, openop, unlinkop, filter, live_icon, name);
}

static void api_uiTemplateAnyId(uiLayout *layout,
                                ApiPtr *ptr,
                                const char *propname,
                                const char *proptypename,
                                const char *name,
                                const char *text_cxt,
                                bool translate)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, prop, translate);

  /* XXX This will search prop again :( */
  uiTemplateAnyId(layout, ptr, propname, proptypename, name);
}

void api_uiTemplateList(uiLayout *layout,
                        struct Cxt *C,
                        const char *listtype_name,
                        const char *list_id,
                        struct ApiPtr *dataptr,
                        const char *propname,
                        struct ApiPtr *active_dataptr,
                        const char *active_propname,
                        const char *item_dyntip_propname,
                        const int rows,
                        const int maxrows,
                        const int layout_type,
                        const int columns,
                        const bool sort_reverse,
                        const bool sort_lock)
{
  int flags = UI_TEMPLATE_LIST_FLAG_NONE;
  if (sort_reverse) {
    flags |= UI_TEMPLATE_LIST_SORT_REVERSE;
  }
  if (sort_lock) {
    flags |= UI_TEMPLATE_LIST_SORT_LOCK;
  }

  uiTemplateList(layout,
                 C,
                 listtype_name,
                 list_id,
                 dataptr,
                 propname,
                 active_dataptr,
                 active_propname,
                 item_dyntip_propname,
                 rows,
                 maxrows,
                 layout_type,
                 columns,
                 flags);
}

static void api_uiTemplateCacheFile(uiLayout *layout,
                                    Cxt *C,
                                    ApiPtr *ptr,
                                    const char *propname)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }

  uiTemplateCacheFile(layout, C, ptr, propname);
}

static void api_uiTemplateCacheFileVelocity(uiLayout *layout,
                                            ApiPtr *ptr,
                                            const char *propname)
{
  ApiPtr fileptr;
  if (!uiTemplateCacheFilePtr(ptr, propname, &fileptr)) {
    return;
  }

  uiTemplateCacheFileVelocity(layout, &fileptr);
}

static void api_uiTemplateCacheFileProcedural(uiLayout *layout,
                                              Cxt *C,
                                              ApiPtr *ptr,
                                              const char *propname)
{
  ApiPtr fileptr;
  if (!uiTemplateCacheFilePtr(ptr, propname, &fileptr)) {
    return;
  }

  uiTemplateCacheFileProcedural(layout, C, &fileptr);
}

static void api_uiTemplateCacheFileTimeSettings(uiLayout *layout,
                                                ApiPtr *ptr,
                                                const char *propname)
{
  ApiPtr fileptr;
  if (!uiTemplateCacheFilePtr(ptr, propname, &fileptr)) {
    return;
  }

  uiTemplateCacheFileTimeSettings(layout, &fileptr);
}

static void api_uiTemplateCacheFileLayers(uiLayout *layout,
                                          Cxt *C,
                                          ApiPtr *ptr,
                                          const char *propname)
{
  ApiPtr fileptr;
  if (!uiTemplateCacheFilePtr(ptr, propname, &fileptr)) {
    return;
  }

  uiTemplateCacheFileLayers(layout, C, &fileptr);
}

static void api_uiTemplatePathBuilder(uiLayout *layout,
                                      ApiPtr *ptr,
                                      const char *propname,
                                      ApiPtr *root_ptr,
                                      const char *name,
                                      const char *text_cxt,
                                      bool translate)
{
  ApiProp *prop = api_struct_find_prop(ptr, propname);

  if (!prop) {
    api_warning("prop not found: %s.%s", api_struct_id(ptr->type), propname);
    return;
  }

  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, prop, translate);

  /* XXX This will search prop again :( */
  uiTemplatePathBuilder(layout, ptr, propname, root_ptr, name);
}

static void api_uiTemplateEventFromKeymapItem(
    uiLayout *layout, wmKeyMapItem *kmi, const char *name, const char *text_ctxt, bool translate)
{
  /* Get translated name (label). */
  name = api_translate_ui_text(name, text_cxt, NULL, NULL, translate);
  uiTemplateEventFromKeymapItem(layout, name, kmi, true);
}

static void api_uiTemplateAssetView(uiLayout *layout,
                                    Cxt *C,
                                    const char *list_id,
                                    ApiPtr *asset_lib_dataptr,
                                    const char *asset_lib_propname,
                                    ApiPtr *assets_dataptr,
                                    const char *assets_propname,
                                    Apitr *active_dataptr,
                                    const char *active_propname,
                                    int filter_id_types,
                                    int display_flags,
                                    const char *activate_opname,
                                    ApiPtr *r_activate_op_props,
                                    const char *drag_opname,
                                    ApiPtr *r_drag_op_props)
{
  AssetFilterSettings filter_settings = {
      .id_types = filter_id_types ? filter_id_types : FILTER_ID_ALL,
  };

  uiTemplateAssetView(layout,
                      C,
                      list_id,
                      asset_lib_dataptr,
                      asset_lib_propname,
                      assets_dataptr,
                      assets_propname,
                      active_dataptr,
                      active_propname,
                      &filter_settings,
                      display_flags,
                      activate_opname,
                      r_activate_op_props,
                      drag_opname,
                      r_drag_op_props);
}

/** XXX Remove filter items that require more than 32 bits for storage. Api enums don't support
 * that currently */
static const EnumPropItem *api_uiTemplateAssetView_filter_id_types_itemf(
    Cxt *UNUSED(C), ApiPtr *UNUSED(ptr), ApiProp *UNUSED(prop), bool *r_free)
{
  EnumPropItem *items = NULL;
  int totitem = 0;

  for (int i = 0; api_enum_id_type_filter_items[i].id; i++) {
    if (api_enum_id_type_filter_items[i].flag > (1ULL << 31)) {
      continue;
    }

    EnumPropItem tmp = {0, "", 0, "", ""};
    tmp.value = api_enum_id_type_filter_items[i].flag;
    tmp.id = api_enum_id_type_filter_items[i].id;
    tmp.icon = api_enum_id_type_filter_items[i].icon;
    tmp.name = api_enum_id_type_filter_items[i].name;
    tmp.description = api_enum_id_type_filter_items[i].description;
    api_enum_item_add(&items, &totitem, &tmp);
  }
  api_enum_item_end(&items, &totitem);

  *r_free = true;
  return items;
}

static uiLayout *api_uiLayoutRowWithHeading(
    uiLayout *layout, bool align, const char *heading, const char *heading_cxt, bool translate)
{
  /* Get translated heading. */
  heading = api_translate_ui_text(heading, heading_cxt, NULL, NULL, translate);
  return uiLayoutRowWithHeading(layout, align, heading);
}

static uiLayout *api_uiLayoutColumnWithHeading(
    uiLayout *layout, bool align, const char *heading, const char *heading_cxt, bool translate)
{
  /* Get translated heading. */
  heading = api_translate_ui_text(heading, heading_cxt, NULL, NULL, translate);
  return uiLayoutColumnWithHeading(layout, align, heading);
}

static int api_ui_get_apiptr_icon(Cxt *C, ApiPtr *ptr_icon)
{
  return ui_icon_from_apiptr(C, ptr_icon, api_struct_ui_icon(ptr_icon->type), false);
}

static const char *api_ui_get_enum_name(Cxt *C,
                                        ApiPtr *ptr,
                                        const char *propname,
                                        const char *id)
{
  ApiProp *prop = NULL;
  const EnumPropItem *items = NULL;
  bool free;
  const char *name = "";

  prop = api_struct_find_prop(ptr, propname);
  if (!prop || (api_prop_type(prop) != PROP_ENUM)) {
    api_warning(
        "Prop not found or not an enum: %s.%s", api_struct_id(ptr->type), propname);
    return name;
  }

  api_prop_enum_items_gettexted(C, ptr, prop, &items, NULL, &free);

  if (items) {
    const int index = api_enum_from_id(items, id);
    if (index != -1) {
      name = items[index].name;
    }
    if (free) {
      mem_freen((void *)items);
    }
  }

  return name;
}

static const char *api_ui_get_enum_description(Cxt *C,
                                               ApiPtr *ptr,
                                               const char *propname,
                                               const char *id)
{
  ApiProp *prop = NULL;
  const EnumPropItem *items = NULL;
  bool free;
  const char *desc = "";

  prop = api_struct_find_prop(ptr, propname);
  if (!prop || (api_prop_type(prop) != PROP_ENUM)) {
    api_warning(
        "Prop not found or not an enum: %s.%s", api_struct_id(ptr->type), propname);
    return desc;
  }

  api_prop_enum_items_gettexted(C, ptr, prop, &items, NULL, &free);

  if (items) {
    const int index = api_enum_from_id(items, id);
    if (index != -1) {
      desc = items[index].description;
    }
    if (free) {
      mem_freen((void *)items);
    }
  }

  return desc;
}

static int api_ui_get_enum_icon(Cxt *C,
                                ApiPtr *ptr,
                                const char *propname,
                                const char *id)
{
  ApiProp *prop = NULL;
  const EnumPropItem *items = NULL;
  bool free;
  int icon = ICON_NONE;

  prop = api_struct_find_prop(ptr, propname);
  if (!prop || (api_prop_type(prop) != PROP_ENUM)) {
    api_warning(
        "Prop not found or not an enum: %s.%s", api_struct_id(ptr->type), propname);
    return icon;
  }

  api_prop_enum_items(C, ptr, prop, &items, NULL, &free);

  if (items) {
    const int index = api_enum_from_identifier(items, id);
    if (index != -1) {
      icon = items[index].icon;
    }
    if (free) {
      mem_freen((void *)items);
    }
  }

  return icon;
}

#else

static void api_ui_item_common_heading(FunctionRNA *func)
{
  api_def_string(fn,
                 "heading",
                 NULL,
                 UI_MAX_NAME_STR,
                 "Heading",
                 "Label to insert into the layout for this sub-layout");
  api_def_string(fn,
                 "heading_ctxt",
                 NULL,
                 0,
                 "",
                 "Override automatic translation context of the given heading");
  api_def_bool(
      fn, "translate", true, "", "Translate the given heading, when ui translation is enabled");
}

static void api_ui_item_common_text(ApiFn *fn)
{
  ApiProp *prop;

  prop = api_def_string(fn, "text", NULL, 0, "", "Override automatic text of the item");
  api_def_prop_clear_flag(prop, PROP_NEVER_NULL);
  prop = api_def_string(
      fn, "text_ctxt", NULL, 0, "", "Override automatic translation cxt of the given text");
  api_def_prop_clear_flag(prop, PROP_NEVER_NULL);
  api_def_bool(
      fn, "translate", true, "", "Translate the given text, when ui translation is enabled");
}

static void api_ui_item_common(ApiFn *fn)
{
  ApiProp *prop;

  api_ui_item_common_text(fn);

  prop = api_def_prop(fn, "icon", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_icon_items);
  api_def_prop_ui_text(prop, "Icon", "Override automatic icon of the item");
}

static void api_ui_item_op(ApiFn *fn)
{
  ApiProp *parm;
  parm = api_def_string(fn, "op", NULL, 0, "", "Id of the op");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

static void api_ui_item_op_common(ApiFn *fn)
{
  api_ui_item_op(fn);
  api_ui_item_common(fn);
}

static void api_ui_item_api_common(ApiFn *fn)
{
  ApiProp *parm;

  parm = api_def_ptr(fn, "data", "AnyType", "", "Data from which to take property");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Id of prop in data");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_api_ui_layout(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  static const EnumPropItem curve_type_items[] = {
      {0, "NONE", 0, "None", ""},
      {'v', "VECTOR", 0, "Vector", ""},
      {'c', "COLOR", 0, "Color", ""},
      {'h', "HUE", 0, "Hue", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem id_template_filter_items[] = {
      {UI_TEMPLATE_ID_FILTER_ALL, "ALL", 0, "All", ""},
      {UI_TEMPLATE_ID_FILTER_AVAILABLE, "AVAILABLE", 0, "Available", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem asset_view_template_options[] = {
      {UI_TEMPLATE_ASSET_DRAW_NO_NAMES,
       "NO_NAMES",
       0,
       "",
       "Do not display the name of each asset underneath preview images"},
      {UI_TEMPLATE_ASSET_DRAW_NO_FILTER,
       "NO_FILTER",
       0,
       "",
       "Do not display buttons for filtering the available assets"},
      {UI_TEMPLATE_ASSET_DRAW_NO_LIB,
       "NO_LIB",
       0,
       "",
       "Do not display buttons to choose or refresh an asset lib"},
      {0, NULL, 0, NULL, NULL},
  };

  static float node_socket_color_default[] = {0.0f, 0.0f, 0.0f, 1.0f};

  /* simple layout specifiers */
  fn = api_def_fn(sapi, "row", "api_uiLayoutRowWithHeading");
  parm = api_def_ptr(fn, "layout", "UILayout", "", "Sub-layout to put items in");
  api_def_fn_return(fn, parm);
  api_def_fn_ui_description(
      func,
      "Sub-layout. Items placed in this sublayout are placed next to each other "
      "in a row");
  api_def_bool(fn, "align", false, "", "Align buttons to each other");
  api_ui_item_common_heading(func);

  fn = api_def_fn(sapi, "column", "rna_uiLayoutColumnWithHeading");
  parm = api_def_ptr(fn, "layout", "UILayout", "", "Sub-layout to put items in");
  api_def_fn_return(fn, parm);
  api_def_fn_ui_description(
      fn,
      "Sub-layout. Items placed in this sublayout are placed under each other "
      "in a column");
  api_def_bool(fn, "align", false, "", "Align buttons to each other");
  api_ui_item_common_heading(fn);

  fn = api_def_fn(sapi, "column_flow", "uiLayoutColumnFlow");
  api_def_int(fn, "columns", 0, 0, INT_MAX, "", "Number of columns, 0 is automatic", 0, INT_MAX);
  parm = api_def_ptr(fn, "layout", "UILayout", "", "Sub-layout to put items in");
  api_def_fn_return(fn, parm);
  api_def_bool(fn, "align", false, "", "Align buttons to each other");

  fn = api_def_fn(sapi, "grid_flow", "uiLayoutGridFlow");
  api_def_bool(fn, "row_major", false, "", "Fill row by row, instead of column by column");
  api_def_int(
      fn,
      "columns",
      0,
      INT_MIN,
      INT_MAX,
      "",
      "Number of columns, positive are absolute fixed numbers, 0 is automatic, negative are "
      "automatic multiple numbers along major axis (e.g. -2 will only produce 2, 4, 6 etc. "
      "columns for row major layout, and 2, 4, 6 etc. rows for column major layout)",
      INT_MIN,
      INT_MAX);
  api_def_bool(fn, "even_columns", false, "", "All columns will have the same width");
  api_def_bool(fn, "even_rows", false, "", "All rows will have the same height");
  api_def_bool(fn, "align", false, "", "Align buttons to each other");
  parm = api_def_ptr(fn, "layout", "UILayout", "", "Sub-layout to put items in");
  api_def_fn_return(fn, parm);

  /* box layout */
  fn = api_def_fn(sapi, "box", "uiLayoutBox");
  parm = api_def_ptr(fn, "layout", "UILayout", "", "Sub-layout to put items in");
  api_def_fn_return(fn, parm);
  api_def_fn_ui_description(fn,
                            "Sublayout (items placed in this sublayout are placed "
                            "under each other in a column and are surrounded by a box)");

  /* split layout */
  fn = api_def_fn(sapi, "split", "uiLayoutSplit");
  parm = api_def_ptr(fn, "layout", "UILayout", "", "Sub-layout to put items in");
  api_def_fn_return(fn, parm);
  api_def_float(fn,
                "factor",
                0.0f,
                0.0f,
                1.0f,
                "Percentage",
                "Percentage of width to split at (leave unset for automatic calculation)",
                0.0f,
                1.0f);
  api_def_bool(fn, "align", false, "", "Align buttons to each other");

  /* radial/pie layout */
  fn = api_def_fn(sapi, "menu_pie", "uiLayoutRadial");
  parm = api_def_ptr(fn, "layout", "UILayout", "", "Sub-layout to put items in");
  api_def_fn_return(fn, parm);
  api_def_fn_ui_description(fn,
                            "Sublayout. Items placed in this sublayout are placed "
                            "in a radial fashion around the menu center)");

  /* Icon of an apa pointer */
  fn = api_def_fn(sapi, "icon", "api_ui_get_apiptr_icon");
  parm = api_def_int(fn, "icon_value", ICON_NONE, 0, INT_MAX, "", "Icon identifier", 0, INT_MAX);
  api_def_fn_return(fn, parm);
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  parm = api_def_ptr(fn, "data", "AnyType", "", "Data from which to take the icon");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_fn_ui_description(fn,
                            "Return the custom icon for this data, "
                            "use it e.g. to get materials or texture icons");

  /* UI name, description and icon of an enum item */
  fn = api_def_fn(sapi, "enum_item_name", "api_ui_get_enum_name");
  parm = api_def_string(fn, "name", NULL, 0, "", "UI name of the enum item");
  api_def_fn_return(fn, parm);
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  api_ui_item_api_common(fn);
  parm = api_def_string(fn, "id", NULL, 0, "", "Id of the enum item");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_fn_ui_description(fn, "Return the UI name for this enum item");

  fn = api_def_fn(sapi, "enum_item_description", "api_ui_get_enum_description");
  parm = api_def_string(fn, "description", NULL, 0, "", "UI description of the enum item");
  api_def_fn_return(fn, parm);
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  api_ui_item_api_common(fn);
  parm = api_def_string(fn, "id", NULL, 0, "", "Identifier of the enum item");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_fn_ui_description(fn, "Return the UI description for this enum item");

  fn = api_def_fn(sapi, "enum_item_icon", "api_ui_get_enum_icon");
  parm = api_def_int(fn, "icon_value", ICON_NONE, 0, INT_MAX, "", "Icon id", 0, INT_MAX);
  api_def_fn_return(fn, parm);
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_CXT);
  api_ui_item_api_common(fn);
  parm = api_def_string(fn, "identifier", NULL, 0, "", "Identifier of the enum item");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_fn_ui_description(fn, "Return the icon for this enum item");

  /* items */
  fn = api_def_fn(sapi, "prop", "api_uiItemR");
  api_def_fn_ui_description(fn, "Item. Exposes an RNA item and places it into the layout");
  api_ui_item_api_common(fn);
  api_ui_item_common(fn);
  api_def_bool(fn, "expand", false, "", "Expand button to show more detail");
  api_def_bool(fn, "slider", false, "", "Use slider widget for numeric values");
  api_def_int(fn,
              "toggle",
              -1,
              -1,
              1,
              "",
              "Use toggle widget for boolean values, "
              "or a checkbox when disabled "
              "(the default is -1 which uses toggle only when an icon is displayed)",
              -1,
              1);
  api_def_bool(fn, "icon_only", false, "", "Draw only icons in buttons, no text");
  api_def_bool(fn, "event", false, "", "Use button to input key events");
  api_def_bool(
      fn, "full_event", false, "", "Use button to input full events including modifiers");
  api_def_bool(fn,
                "emboss",
                true,
                "",
                "Draw the button itself, not just the icon/text. When false, corresponds to the "
                "'NONE_OR_STATUS' layout emboss type");
  api_def_int(fn,
              "index",
              /* API_NO_INDEX == -1 */
              -1,
              -2,
              INT_MAX,
              "",
              "The index of this button, when set a single member of an array can be accessed, "
              "when set to -1 all array members are used",
              -2,
              INT_MAX);
  parm = api_def_prop(fn, "icon_value", PROP_INT, PROP_UNSIGNED);
  api_def_prop_ui_text(parm, "Icon Value", "Override automatic icon of the item");
  api_def_bool(fn, "invert_checkbox", false, "", "Draw checkbox value inverted");

  fn = api_def_fn(sapi, "props_enum", "uiItemsEnumR");
  api_ui_item_api_common(fn);

  fn = api_def_fn(sapi, "prop_menu_enum", "api_uiItemMenuEnumR");
  api_ui_item_api_common(fn);
  api_ui_item_common(fn);

  fn = api_def_fn(sapi, "prop_with_popover", "api_uiItemR_with_popover");
  api_ui_item_api_common(fn);
  api_ui_item_common(fn);
  api_def_bool(fn, "icon_only", false, "", "Draw only icons in tabs, no text");
  parm = api_def_string(fn, "panel", NULL, 0, "", "Id of the panel");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "prop_with_menu", "api_uiItemR_with_menu");
  api_ui_item_api_common(fn);
  api_ui_item_common(fn);
  api_def_bool(fn, "icon_only", false, "", "Draw only icons in tabs, no text");
  parm = api_def_string(fn, "menu", NULL, 0, "", "Id of the menu");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "prop_tabs_enum", "api_uiItemTabsEnumR");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
  parm = api_def_ptr(
      fn, "data_highlight", "AnyType", "", "Data from which to take highlight prop");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_APIPTR);
  parm = api_def_string(
      fn, "prop_highlight", NULL, 0, "", "Id of highlight prop in data");
  api_def_bool(fn, "icon_only", false, "", "Draw only icons in tabs, no text");

  fn = api_def_fn(sapi, "prop_enum", "api_uiItemEnumR_string");
  api_ui_item_api_common(fn);
  parm = api_def_string(fn, "value", NULL, 0, "", "Enum prop value");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_ui_item_common(fn);

  fn = api_def_fn(sapi, "prop_search", "api_uiItemPtrR");
  api_ui_item_api_common(fn);
  parm = api_def_ptr(
      fn, "search_data", "AnyType", "", "Data from which to take collection to search in");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  parm = api_def_string(
      fn, "search_property", NULL, 0, "", "Identifier of search collection property");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_ui_item_common(fn);
  api_def_bool(
      fn, "results_are_suggestions", false, "", "Accept inputs that do not match any item");

  fn = api_def_fn(sapi, "prop_decorator", "uiItemDecoratorR");
  api_ui_item_api_common(fn);
  api_def_int(fn,
              "index",
              /* API_NO_INDEX == -1 */
              -1,
              -2,
              INT_MAX,
              "",
              "The index of this button, when set a single member of an array can be accessed, "
              "when set to -1 all array members are used",
              -2,
              INT_MAX);

  for (int is_menu_hold = 0; is_menu_hold < 2; is_menu_hold++) {
    fn = (is_menu_hold) ? api_def_fn(sapi, "op_menu_hold", "api_uiItemOMenuHold") :
                        api_def_fn(sapi, "op", "api_uiItemO");
    api_ui_item_op_common(fn);
    api_def_bool(fn, "emboss", true, "", "Draw the button itself, not just the icon/text");
    api_def_bool(fn, "depress", false, "", "Draw pressed in");
    parm = api_def_prop(fn, "icon_value", PROP_INT, PROP_UNSIGNED);
    api_def_prop_ui_text(parm, "Icon Value", "Override automatic icon of the item");
    if (is_menu_hold) {
      parm = api_def_string(fn, "menu", NULL, 0, "", "Id of the menu");
      api_def_param_flags(parm, 0, PARM_REQUIRED);
    }
    parm = api_def_ptr(
        fn, "props", "OpProps", "", "Op props to fill in");
    api_def_param_flags(parm, 0, PARM_REQUIRED | PARM_APIPTR);
    api_def_fn_return(fn, parm);
    api_def_fn_ui_description(fn, "Item. Places a button into the layout to call an Op");
  }

  fn = api_def_fn(sapi, "op_enum", "api_uiItemsEnumO");
  parm = api_def_string(fn, "op", NULL, 0, "", "Id of the op");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Id of prop in op");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "icon_only", false, "", "Draw only icons in buttons, no text");

  fn = api_def_fn(sapi, "op_menu_enum", "api_uiItemMenuEnumO");
  api_def_fn_flag(fn, FN_USE_CXT);
  /* Can't use #api_ui_item_op_common because property must come right after. */
  api_ui_item_op(fn);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Id of prop in op");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_ui_item_common(fn);
  parm = api_def_ptr(
      fn, "props", "OpProps", "", "Op props to fill in");
  api_def_param_flags(parm, 0, PARM_REQUIRED | PARM_APIPTR);
  api_def_fn_return(fn, parm);

  /* useful in C but not in python */
#  if 0

  fn = api_def_fn(sapi, "op_enum_single", "uiItemEnumO_string");
  api_ui_item_op_common(fn);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Ident of prop in op");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "value", NULL, 0, "", "Enum prop value");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "op_bool", "uiItemBoolO");
  api_ui_item_op_common(fn);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Id of prop in operator");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(
      fn, "value", false, "", "Value of the prop to call the op with");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "op_int", "uiItemIntO");
  api_ui_item_op_common(fn);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Id of prop in operator");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn,
                     "value",
                     0,
                     INT_MIN,
                     INT_MAX,
                     "",
                     "Value of the property to call the operator with",
                     INT_MIN,
                     INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "op_float", "uiItemFloatO");
  api_ui_item_op_common(fn);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Identifier of property in operator");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float(fn,
                       "value",
                       0,
                       -FLT_MAX,
                       FLT_MAX,
                       "",
                       "Value of the prop to call the op with",
                       -FLT_MAX,
                       FLT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "op_string", "uiItemStringO");
  api_ui_item_op_common(fn);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Id of prop in op");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(
      fn, "value", NULL, 0, "", "Value of the prop to call the op with");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
#  endif

  fn = api_def_fn(sapi, "label", "api_uiItemL");
  api_def_fn_ui_description(fn, "Item. Displays text and/or icon in the layout");
  api_ui_item_common(fn);
  parm = api_def_prop(fn, "icon_value", PROP_INT, PROP_UNSIGNED);
  api_def_prop_ui_text(parm, "Icon Value", "Override automatic icon of the item");

  fn = api_def_fn(sapi, "menu", "api_uiItemM");
  parm = api_def_string(fn, "menu", NULL, 0, "", "Id of the menu");
  api_ui_item_common(fn);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_prop(fn, "icon_value", PROP_INT, PROP_UNSIGNED);
  api_def_prop_ui_text(parm, "Icon Value", "Override automatic icon of the item");

  fn = api_def_fn(sapi, "menu_contents", "api_uiItemM_contents");
  parm = api_def_string(fn, "menu", NULL, 0, "", "Id of the menu");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "popover", "api_uiItemPopoverPanel");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_string(fn, "panel", NULL, 0, "", "Id of the panel");
  api_ui_item_common(fn);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_prop(fn, "icon_value", PROP_INT, PROP_UNSIGNED);
  api_def_prop_ui_text(parm, "Icon Value", "Override automatic icon of the item");

  fn = api_def_fn(sapi, "popover_group", "api_uiItemPopoverPanelFromGroup");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_enum(fn, "space_type", api_enum_space_type_items, 0, "Space Type", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(
      fn, "region_type", api_enum_region_type_items, RGN_TYPE_WINDOW, "Region Type", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "cxt", NULL, 0, "", "panel type context");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn, "category", NULL, 0, "", "panel type category");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "separator", "uiItemS_ex");
  api_def_fn_ui_description(fn, "Item. Inserts empty space into the layout between items");
  api_def_float(fn,
                "factor",
                1.0f,
                0.0f,
                FLT_MAX,
                "Percentage",
                "Percentage of width to space (leave unset for default space)",
                0.0f,
                FLT_MAX);

  fn = api_def_fn(sapi, "separator_spacer", "uiItemSpacer");
  api_def_fn_ui_description(
      fn, "Item. Inserts horizontal spacing empty space into the layout between items");

  /* context */
  fn = api_def_fn(sapi, "cxt_ptr_set", "uiLayoutSetCxtPtr");
  parm = api_def_string(fn, "name", NULL, 0, "Name", "Name of entry in the context");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "data", "AnyType", "", "Pointer to put in context");
  api_def_param_flags(parm, 0, PARM_REQUIRED | PARM_APIPTR);

  /* templates */
  fn = api_def_fn(sapi, "template_header", "uiTemplateHeader");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_ui_description(fn, "Inserts common Space header UI (editor type selector)");

  fn = api_def_fn(sapi, "template_ID", "api_uiTemplateID");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
  api_def_string(fn, "new", NULL, 0, "", "Op id to create a new Id block");
  api_def_string(
      fn, "open", NULL, 0, "", "Op id to open a file for creating a new Id block");
  api_def_string(fn, "unlink", NULL, 0, "", "Op id to unlink the Id block");
  api_def_enum(fn,
               "filter",
               id_template_filter_items,
               UI_TEMPLATE_ID_FILTER_ALL,
               "",
               "Optionally limit the items which can be selected");
  api_def_bool(fn, "live_icon", false, "", "Show preview instead of fixed icon");
  api_ui_item_common_text(fn);

  fn = api_def_fn(sapi, "template_id_preview", "uiTemplateIdPreview");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
  api_def_string(fn, "new", NULL, 0, "", "Op id to create a new Id block");
  api_def_string(
      fn, "open", NULL, 0, "", "Op id to open a file for creating a new Id block");
  api_def_string(fn, "unlink", NULL, 0, "", "Op id to unlink the Id block");
  api_def_int(
      fn, "rows", 0, 0, INT_MAX, "Number of thumbnail preview rows to display", "", 0, INT_MAX);
  api_def_int(fn,
              "cols",
              0,
              0,
              INT_MAX,
              "Number of thumbnail preview columns to display",
              "",
              0,
              INT_MAX);
  api_def_enum(fn,
               "filter",
               id_template_filter_items,
               UI_TEMPLATE_ID_FILTER_ALL,
               "",
               "Optionally limit the items which can be selected");
  api_def_bool(fn, "hide_buttons", false, "", "Show only list, no buttons");

  func = api_def_fn(sapi, "template_any_id", "api_uiTemplateAnyId");
  parm = api_def_ptr(fn, "data", "AnyType", "", "Data from which to take prop");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Id of prop in data");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(fn,
                        "type_prop",
                        NULL,
                        0,
                        "",
                        "Identifier of prop in data giving the type of the ID-blocks to use");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_ui_item_common_text(fn);

  fn = api_def_fn(sapi, "template_id_tabs", "uiTemplateIdTabs");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
  api_def_string(fn, "new", NULL, 0, "", "Op id to create a new Id block");
  api_def_string(fn, "menu", NULL, 0, "", "Cxt menu id");
  api_def_enum(fn,
               "filter",
               id_template_filter_items,
               UI_TEMPLATE_ID_FILTER_ALL,
               "",
               "Optionally limit the items which can be selected");

  fn = api_def_fn(sapi, "template_search", "uiTemplateSearch");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
  parm = api_def_ptr(
      fn, "search_data", "AnyType", "", "Data from which to take collection to search in");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(
      fn, "search_prop", NULL, 0, "", "Id of search collection prop");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_string(
      fn, "new", NULL, 0, "", "Op id to create a new item for the collection");
  api_def_string(fn,
                 "unlink",
                 NULL,
                 0,
                 "",
                 "Op id to unlink or delete the active "
                 "item from the collection");

  fn = api_def_fn(sapi, "template_search_preview", "uiTemplateSearchPreview");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
  parm = api_def_ptr(
      fn, "search_data", "AnyType", "", "Data from which to take collection to search in");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(
      fn, "search_prop", NULL, 0, "", "Id of search collection prop");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_string(
      fn, "new", NULL, 0, "", "Op id to create a new item for the collection");
  api_def_string(fn,
                 "unlink",
                 NULL,
                 0,
                 "",
                 "Op id to unlink or delete the active "
                 "item from the collection");
  api_def_int(
      fn, "rows", 0, 0, INT_MAX, "Number of thumbnail preview rows to display", "", 0, INT_MAX);
  api_def_int(fn,
              "cols",
              0,
              0,
              INT_MAX,
              "Number of thumbnail preview columns to display",
              "",
              0,
              INT_MAX);

  fn = api_def_fn(sapi, "template_path_builder", "api_uiTemplatePathBuilder");
  parm = api_def_ptr(fn, "data", "AnyType", "", "Data from which to take prop");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Id of property in data");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "root", "Id", "", "ID-block from which path is evaluated from");
  api_def_param_flags(parm, 0, PARM_REQUIRED | PARM_APIPTR);
  api_ui_item_common_text(fn);

  fn = api_def_fn(sapi, "template_mods", "uiTemplateModifiers");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_ui_description(fn, "Generates the UI layout for the modifier stack");

  fn = api_def_fn(sapi, "template_constraints", "uiTemplateConstraints");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_ui_description(fn, "Generates the panels for the constraint stack");
  api_def_bool(fn,
               "use_bone_constraints",
               true,
               "",
               "Add panels for bone constraints instead of object constraints");

  fn = api_def_fn(sapi, "template_pen_mod", "uiTemplatePenMod");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_ui_description(fn, "Generates the panels for the pen mod stack");

  fn = api_def_function(sapi, "template_shaderfx", "uiTemplateShaderFx");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_ui_description(fn, "Generates the panels for the shader effect stack");

  fn = api_def_fn(sapi, "template_pen_color", "uiTemplatePenColorPreview");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
  api_def_int(
      fn, "rows", 0, 0, INT_MAX, "Number of thumbnail preview rows to display", "", 0, INT_MAX);
  apip_def_int(fn,
              "cols",
              0,
              0,
              INT_MAX,
              "Number of thumbnail preview columns to display",
              "",
              0,
              INT_MAX);
  api_def_float(fn, "scale", 1.0f, 0.1f, 1.5f, "Scale of the image thumbnails", "", 0.5f, 1.0f);
  api_def_enum(fn,
               "filter",
               id_template_filter_items,
               UI_TEMPLATE_ID_FILTER_ALL,
               "",
               "Optionally limit the items which can be selected");

  fn = api_def_fn(sapi, "template_constraint_header", "uiTemplateConstraintHeader");
  api_def_fn_ui_description(fn, "Generates the header for constraint panels");
  parm = api_def_ptr(fn, "data", "Constraint", "", "Constraint data");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);

  fn = api_def_fn(sapi, "template_preview", "uiTemplatePreview");
  api_def_n_ui_description(
      fn, "Item. A preview window for materials, textures, lights or worlds");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_ptr(fn, "id", "Id", "", "Id data-block");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "show_buttons", true, "", "Show preview buttons?");
  api_def_ptr(fn, "parent", "Id", "", "Id data-block");
  api_def_ptr(fn, "slot", "TextureSlot", "", "Texture slot");
  api_def_string(
      fn,
      "preview_id",
      NULL,
      0,
      "",
      "Identifier of this preview widget, if not set the ID type will be used "
      "(i.e. all previews of materials without explicit ID will have the same size...)");

  fn = api_def_fn(sapi, "template_curve_mapping", "uiTemplateCurveMapping");
  api_def_fn_ui_description(
      fn, "Item. A curve mapping widget used for e.g falloff curves for lights");
  api_ui_item_api_common(fn);
  api_def_enum(fn, "type", curve_type_items, 0, "Type", "Type of curves to display");
  api_def_bool(fn, "levels", false, "", "Show black/white levels");
  api_def_bool(fn, "brush", false, "", "Show brush options");
  api_def_bool(fn, "use_negative_slope", false, "", "Use a negative slope by default");
  api_def_bool(fn, "show_tone", false, "", "Show tone options");

  fn = api_def_fn(sapi, "template_curveprofile", "uiTemplateCurveProfile");
  api_def_fn_ui_description(fn, "A profile path editor used for custom profiles");
  api_ui_item_api_common(fn);

  fn = api_def_fn(sapi, "template_color_ramp", "uiTemplateColorRamp");
  api_def_fn_ui_description(fn, "Item. A color ramp widget");
  api_ui_item_api_common(fn);
  api_def_bool(fn, "expand", false, "", "Expand button to show more detail");

  fn = api_def_fn(sapi, "template_icon", "uiTemplateIcon");
  api_def_fn_ui_description(fn, "Display a large icon");
  parm = api_def_int(fn, "icon_value", 0, 0, INT_MAX, "Icon to display", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_float(fn,
                "scale",
                1.0f,
                1.0f,
                100.0f,
                "Scale",
                "Scale the icon size (by the button size)",
                1.0f,
                100.0f);

  fn = api_def_fn(sapi, "template_icon_view", "uiTemplateIconView");
  api_def_fn_ui_description(fn, "Enum. Large widget showing Icon previews");
  api_ui_item_api_common(fn);
  api_def_bool(fn, "show_labels", false, "", "Show enum label in preview buttons");
  api_def_float(fn,
                "scale",
                6.0f,
                1.0f,
                100.0f,
                "UI Units",
                "Scale the button icon size (by the button size)",
                1.0f,
                100.0f);
  api_def_float(fn,
                "scale_popup",
                5.0f,
                1.0f,
                100.0f,
                "Scale",
                "Scale the popup icon size (by the button size)",
                1.0f,
                100.0f);

  fn = api_def_fn(sapi, "template_histogram", "uiTemplateHistogram");
  api_def_fn_ui_description(fn, "Item. A histogramm widget to analyze imaga data");
  api_ui_item_api_common(fn);

  fn = api_def_fn(sapi, "template_waveform", "uiTemplateWaveform");
  api_def_fn_ui_description(fn, "Item. A waveform widget to analyze imaga data");
  api_ui_item_api_common(fn);

  fn = api_def_fn(sapi, "template_vectorscope", "uiTemplateVectorscope");
  api_def_fn_ui_description(fn, "Item. A vectorscope widget to analyze imaga data");
  api_ui_item_api_common(fn);

  fn = api_def_fn(sapi, "template_layers", "uiTemplateLayers");
  api_ui_item_api_common(fn);
  parm = api_def_ptr(
      fn, "used_layers_data", "AnyType", "", "Data from which to take property");
  api_def_param_flags(parm, 0, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(
      fn, "used_layers_prop", NULL, 0, "", "Identifier of property in data");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "active_layer", 0, 0, INT_MAX, "Active Layer", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "template_color_picker", "uiTemplateColorPicker");
  api_def_fn_ui_description(fn, "Item. A color wheel widget to pick colors");
  api_ui_item_api_common(fn);
  api_def_bool(
      fn, "value_slider", false, "", "Display the value slider to the right of the color wheel");
  api_def_bool(fn,
               "lock",
               false,
               "",
               "Lock the color wheel display to value 1.0 regardless of actual color");
  api_def_bool(
      fn, "lock_luminosity", false, "", "Keep the color at its original vector length");
  api_def_bool(fn, "cubic", false, "", "Cubic saturation for picking values close to white");

  fn = api_def_fn(sapi, "template_palette", "uiTemplatePalette");
  api_def_fn_ui_description(fn, "Item. A palette used to pick colors");
  api_ui_item_api_common(fn);
  api_def_bool(fn, "color", 0, "", "Display the colors as colors or values");

  fn = api_def_fn(sapi, "template_image_layers", "uiTemplateImageLayers");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_ptr(fn, "image", "Image", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "image_user", "ImageUser", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "template_image", "uiTemplateImage");
  api_def_fn_ui_description(
      func, "Item(s). User interface for selecting images and their source paths");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
  parm = api_def_ptr(fn, "image_user", "ImageUser", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_bool(fn, "compact", false, "", "Use more compact layout");
  api_def_bool(fn, "multiview", false, "", "Expose Multi-View options");

  fn = api_def_fn(sapi, "template_image_settings", "uiTemplateImageSettings");
  api_def_fn_ui_description(fn, "User interface for setting image format options");
  parm = api_def_ptr(fn, "image_settings", "ImageFormatSettings", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_bool(fn, "color_management", false, "", "Show color management settings");

  fn = api_def_function(sapi, "template_image_stereo_3d", "uiTemplateImageStereo3d");
  api_def_fn_ui_description(fn, "User interface for setting image stereo 3d options");
  parm = api_def_ptr(fn, "stereo_3d_format", "Stereo3dFormat", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);

  fn = api_def_fn(sapi, "template_image_views", "uiTemplateImageViews");
  api_def_fn_ui_description(fn, "User interface for setting image views output options");
  parm = api_def_ptr(fn, "image_settings", "ImageFormatSettings", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);

  fn = api_def_fn(sapi, "template_movieclip", "uiTemplateMovieClip");
  api_def_fn_ui_description(
      fn, "Item(s). User interface for selecting movie clips and their source paths");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
  api_def_bool(fn, "compact", false, "", "Use more compact layout");

  fn = api_def_fn(sapi, "template_track", "uiTemplateTrack");
  api_def_fn_ui_description(fn, "Item. A movie-track widget to preview tracking image.");
  api_ui_item_api_common(fn);

  fn = api_def_fn(sapi, "template_marker", "uiTemplateMarker");
  api_def_fn_ui_description(fn, "Item. A widget to control single marker settings.");
  api_ui_item_api_common(fn);
  parm = api_def_ptr(fn, "clip_user", "MovieClipUser", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_ptr(fn, "track", "MovieTrackingTrack", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_bool(fn, "compact", false, "", "Use more compact layout");

  fn = api_def_fn(
      sapi, "template_movieclip_information", "uiTemplateMovieclipInformation");
  api_def_fn_ui_description(fn, "Item. Movie clip information data.");
  api_ui_item_api_common(fn);
  parm = api_def_ptr(fn, "clip_user", "MovieClipUser", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);

  fn = api_def_fn(sapi, "template_list", "api_uiTemplateList");
  api_def_fn_ui_description(fn, "Item. A list widget to display data, e.g. vertexgroups.");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_string(fn, "listtype_name", NULL, 0, "", "Id of the list type to use");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_string(
      fn,
      "list_id",
      NULL,
      0,
      "",
      "Id of this list widget (mandatory when using default \"" UI_UL_DEFAULT_CLASS_NAME
      "\" class). "
      "If this not an empty string, the uilist gets a custom Id, otherwise it takes the "
      "name of the class used to define the uilist (for example, if the "
      "class name is \"OBJECT_UL_vgroups\", and list_id is not set by the "
      "script, then bl_idname = \"OBJECT_UL_vgroups\")");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "dataptr", "AnyType", "", "Data from which to take the Collection prop");
  api_def_param_flags(parm, 0, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(
      fn, "propname", NULL, 0, "", "Id of the Collection prop in data");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn,
                     "active_dataptr",
                     "AnyType",
                     "",
                     "Data from which to take the integer property, index of the active item");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(
      fn,
      "active_propname",
      NULL,
      0,
      "",
      "Id of the integer prop in active_data, index of the active item");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_string(fn,
                 "item_dyntip_propname",
                 NULL,
                 0,
                 "",
                 "Id of a string prop in items, to use as tooltip content");
  api_def_int(fn,
              "rows",
              5,
              0,
              INT_MAX,
              "",
              "Default and minimum number of rows to display",
              0,
              INT_MAX);
  api_def_int(
      fn, "maxrows", 5, 0, INT_MAX, "", "Default maximum number of rows to display", 0, INT_MAX);
  api_def_enum(fn,
               "type",
               api_enum_uilist_layout_type_items,
               UILST_LAYOUT_DEFAULT,
               "Type",
               "Type of layout to use");
  api_def_int(fn,
              "columns",
              9,
              0,
              INT_MAX,
              "",
              "Number of items to display per row, for GRID layout",
              0,
              INT_MAX);
  api_def_bool(fn, "sort_reverse", false, "", "Display items in reverse order by default");
  api_def_bool(fn, "sort_lock", false, "", "Lock display order to default value");

  fn = api_def_function(sapi, "template_running_jobs", "uiTemplateRunningJobs");
  api_def_fn_flag(fn, FN_USE_CXT);

  api_def_fn(sapi, "template_op_search", "uiTemplateOpSearch");
  api_def_fn(sapi, "template_menu_search", "uiTemplateMenuSearch");

  func = api_def_fn(sapi, "template_header_3D_mode", "uiTemplateHeader3D_mode");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_ui_description(fn, "");

  fn = api_def_fn(sapi, "template_edit_mode_selection", "uiTemplateEditModeSelection");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_ui_description(
      fn, "Inserts common 3DView Edit modes header UI (selector for selection mode)");

  fn = api_def_function(sapi, "template_reports_banner", "uiTemplateReportsBanner");
  api_def_fn_flag(fn, FN_USE_CXT);

  fn = api_def_fn(sapi, "template_input_status", "uiTemplateInputStatus");
  api_def_fn_flag(fn, FN_USE_CXT);

  fn = api_def_fn(sapi, "template_node_link", "uiTemplateNodeLink");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_ptr(fn, "ntree", "NodeTree", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "node", "Node", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "socket", "NodeSocket", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "template_node_view", "uiTemplateNodeView");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_ptr(fn, "ntree", "NodeTree", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "node", "Node", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "socket", "NodeSocket", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "template_node_asset_menu_items", "uiTemplateNodeAssetMenuItems");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_string(fn, "catalog_path", NULL, 0, "", "");

  fn = api_def_fn(sapi, "template_texture_user", "uiTemplateTextureUser");
  api_def_fn_flag(fn, FN_USE_CXT);

  fn = api_def_fn(
      sapi, "template_keymap_item_properties", "uiTemplateKeymapItemProps");
  parm = api_def_ptr(fn, "item", "KeyMapItem", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);

  fn = api_def_fn(sapi, "template_component_menu", "uiTemplateComponentMenu");
  api_def_fn_ui_description(fn, "Item. Display expanded prop in a popup menu");
  parm = api_def_ptr(fn, "data", "AnyType", "", "Data from which to take prop");
  api_def_param_flags(parm, 0, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(fn, "prop", NULL, 0, "", "Id of prop in data");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_string(fn, "name", NULL, 0, "", "");

  /* color management templates */
  fn = api_def_fn(sapi, "template_colorspace_settings", "uiTemplateColorspaceSettings");
  api_def_fn_ui_description(fn, "Item. A widget to control input color space settings.");
  api_ui_item_api_common(fn);

  fn = api_def_fn(
      sapi, "template_colormanaged_view_settings", "uiTemplateColormanagedViewSettings");
  api_def_fn_ui_description(fn, "Item. A widget to control color managed view settings.");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);
#  if 0
  api_def_bool(fn,
               "show_global_settings",
               false,
               "",
               "Show widgets to control global color management settings");
#  endif

  /* node socket icon */
  fn = api_def_fn(sapi, "template_node_socket", "uiTemplateNodeSocket");
  api_def_fn_ui_description(fn, "Node Socket Icon");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_float_array(
      fn, "color", 4, node_socket_color_default, 0.0f, 1.0f, "Color", "", 0.0f, 1.0f);

  fn = api_def_fn(sapi, "template_cache_file", "api_uiTemplateCacheFile");
  api_def_fn_ui_description(
      fn, "Item(s). User interface for selecting cache files and their source paths");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);

  fn = api_def_fn(sapi, "template_cache_file_velocity", "api_uiTemplateCacheFileVelocity");
  api_def_fn_ui_description(fn, "Show cache files velocity properties");
  api_ui_item_api_common(fn);

  fn = api_def_fn(
      sapi, "template_cache_file_procedural", "api_uiTemplateCacheFileProcedural");
  api_def_fn_ui_description(fn, "Show cache files render procedural properties");
  api_def_fn_flag(fn, FN_USE_CTX);
  api_ui_item_api_common(fn);

  fn = api_def_fn(
      sapi, "template_cache_file_time_settings", "api_uiTemplateCacheFileTimeSettings");
  api_def_fn_ui_description(fn, "Show cache files time settings");
  api_ui_item_api_common(fn);

  fn = api_def_fn(sapi, "template_cache_file_layers", "api_uiTemplateCacheFileLayers");
  api_def_fn_ui_description(fn, "Show cache files override layers properties");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_ui_item_api_common(fn);

  fn = api_def_fn(sapi, "template_recent_files", "uiTemplateRecentFiles");
  api_def_fn_ui_description(fn, "Show list of recently saved .dune files");
  api_def_int(fn, "rows", 5, 1, INT_MAX, "", "Maximum number of items to show", 1, INT_MAX);
  parm = api_def_int(fn, "found", 0, 0, INT_MAX, "", "Number of items drawn", 0, INT_MAX);
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "template_file_select_path", "uiTemplateFileSelectPath");
  api_def_fn_ui_description(fn, "Item. A text button to set the active file browser path.");
  parm = api_def_ptr(fn, "params", "FileSelectParams", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_fn_flag(fn, FN_USE_CXT);

  fn = api_def_fn(
      sapi, "template_event_from_keymap_item", "api_uiTemplateEventFromKeymapItem");
  api_def_fn_ui_description(fn, "Display keymap item as icons/text");
  parm = api_def_prop(fn, "item", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(parm, "KeyMapItem");
  api_def_prop_ui_text(parm, "Item", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  api_ui_item_common_text(fn);

  fn = api_def_fn(sapi, "template_asset_view", "api_uiTemplateAssetView");
  api_def_fn_ui_description(fn, "Item. A scrollable list of assets in a grid view");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_string(fn,
                        "list_id",
                        NULL,
                        0,
                        "",
                        "Id of this asset view. Necessary to tell apart different asset "
                        "views and to idenify an asset view read from a .dune");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn,
                     "asset_lib_dataptr",
                     "AnyType",
                     "",
                     "Data from which to take the active asset lib prop");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(
      fn, "asset_lib_propname", NULL, 0, "", "Id of the asset lib prop");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "assets_dataptr", "AnyType", "", "Data from which to take the asset list property");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(
      fn, "assets_propname", NULL, 0, "", "Identifier of the asset list property");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn,
                     "active_dataptr",
                     "AnyType",
                     "",
                     "Data from which to take the integer prop, index of the active item");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(
      fn,
      "active_propname",
      NULL,
      0,
      "",
      "Id of the integer prop in active_data, index of the active item");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_prop(fn, "filter_id_types", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(parm, DummyApi_NULL_items);
  apu_def_prop_enum_fns(parm, NULL, NULL, "api_uiTemplateAssetView_filter_id_types_itemf");
  api_def_prop_flag(parm, PROP_ENUM_FLAG);
  api_def_enum_flag(fn,
                    "display_options",
                    asset_view_template_options,
                    0,
                    "",
                    "Displaying options for the asset view");
  api_def_string(fn,
                 "activate_op",
                 NULL,
                 0,
                 "",
                 "Name of a custom operator to invoke when activating an item");
  parm = api_def_ptr(
      fn,
      "activate_op_props",
      "OpProps",
      "",
      "Op props to fill in for the custom activate operator passed to the template");
  api_def_param_flags(parm, 0, PARM_APIPTR);
  api_def_fn_output(fn, parm);
  apu_def_string(fn,
                 "drag_op",
                 NULL,
                 0,
                 "",
                 "Name of a custom op to invoke when starting to drag an item. Never "
                 "invoked together with the `active_op` (if set), it's either the drag or "
                 "the activate one");
  parm = api_def_ptr(
      fn,
      "drag_op_props",
      "OpProps",
      "",
      "Op props to fill in for the custom drag op passed to the template");
  api_def_param_flags(parm, 0, PARM_APIPTR);
  api_def_fn_output(fn, parm);
}

#endif
