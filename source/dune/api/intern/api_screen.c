#include <stddef.h>
#include <stdlib.h>

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_scene.h"
#include "types_screen.h"
#include "types_workspace.h"

#include "ed_info.h"

const EnumPropItem api_enum_region_type_items[] = {
    {RGN_TYPE_WINDOW, "WINDOW", 0, "Window", ""},
    {RGN_TYPE_HEADER, "HEADER", 0, "Header", ""},
    {RGN_TYPE_CHANNELS, "CHANNELS", 0, "Channels", ""},
    {RGN_TYPE_TEMPORARY, "TEMPORARY", 0, "Temporary", ""},
    {RGN_TYPE_UI, "UI", 0, "UI", ""},
    {RGN_TYPE_TOOLS, "TOOLS", 0, "Tools", ""},
    {RGN_TYPE_TOOL_PROPS, "TOOL_PROPS", 0, "Tool Properties", ""},
    {RGN_TYPE_PREVIEW, "PREVIEW", 0, "Preview", ""},
    {RGN_TYPE_HUD, "HUD", 0, "Floating Region", ""},
    {RGN_TYPE_NAV_BAR, "NAVIGATION_BAR", 0, "Navigation Bar", ""},
    {RGN_TYPE_EXECUTE, "EXECUTE", 0, "Execute Buttons", ""},
    {RGN_TYPE_FOOTER, "FOOTER", 0, "Footer", ""},
    {RGN_TYPE_TOOL_HEADER, "TOOL_HEADER", 0, "Tool Header", ""},
    {RGN_TYPE_XR, "XR", 0, "XR", ""},
    {0, NULL, 0, NULL, NULL},
};

#include "ed_screen.h"

#include "wm_api.h"
#include "wm_types.h"

#ifdef API_RUNTIME

#  include "api_access.h"

#  include "dune_global.h"
#  include "dune_screen.h"
#  include "dune_workspace.h"

#  include "graph.h"

#  include "ui_view2d.h"

#  ifdef WITH_PYTHON
#    include "BPY_extern.h"
#  endif

static void api_Screen_bar_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Screen *screen = (Screen *)ptr->data;
  screen->do_draw = true;
  screen->do_refresh = true;
}

static void api_Screen_redraw_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Screen *screen = (Screen *)ptr->data;

  /* the settings for this are currently only available from a menu in the TimeLine,
   * hence refresh=SPACE_ACTION, as timeline is now in there
   */
  ed_screen_animation_timer_update(screen, screen->redraws_flag);
}

static bool api_Screen_is_animation_playing_get(ApiPtr *UNUSED(ptr))
{
  /* can be NULL on file load, #42619 */
  wmWindowManager *wm = G_MAIN->wm.first;
  return wm ? (ed_screen_animation_playing(wm) != NULL) : 0;
}

static bool api_Screen_is_scrubbing_get(ApiPtr *ptr)
{
  Screen *screen = (Screen *)ptr->data;
  return screen->scrubbing;
}

static int api_region_alignment_get(ApiPtr *ptr)
{
  ARegion *region = ptr->data;
  return RGN_ALIGN_ENUM_FROM_MASK(region->alignment);
}

static bool api_Screen_fullscreen_get(ApiPtr *ptr)
{
  Screen *screen = (Screen *)ptr->data;
  return (screen->state == SCREENMAXIMIZED);
}

static int api_Area_type_get(ApiPtr *ptr)
{
  ScrArea *area = (ScrArea *)ptr->data;
  /* Usually 'spacetype' is used. It lags behind a bit while switching area
   * type though, then we use 'butspacetype' instead (#41435). */
  return (area->btnspacetype == SPACE_EMPTY) ? area->spacetype : area->butspacetype;
}

static void api_Area_type_set(ApiPtr *ptr, int value)
{
  if (ELEM(value, SPACE_TOPBAR, SPACE_STATUSBAR)) {
    /* Special case: An area can not be set to show the top-bar editor (or
     * other global areas). However it should still be possible to identify
     * its type from Python. */
    return;
  }

  ScrArea *area = (ScrArea *)ptr->data;
  /* Empty areas are locked. */
  if ((value == SPACE_EMPTY) || (area->spacetype == SPACE_EMPTY)) {
    return;
  }

  area->butspacetype = value;
}

static void api_Area_type_update(Cxt *C, ApiPtr *ptr)
{
  Screen *screen = (Screen *)ptr->owner_id;
  ScrArea *area = (ScrArea *)ptr->data;

  /* Running update without having called 'set', see: #64049 */
  if (area->butspacetype == SPACE_EMPTY) {
    return;
  }

  wmWindowManager *wm = cxt_wm_manager(C);
  wmWindow *win;
  /* XXX this call still use context, so we trick it to work in the right context */
  for (win = wm->windows.first; win; win = win->next) {
    if (screen == wm_window_get_active_screen(win)) {
      wmWindow *prevwin = cxt_wm_window(C);
      ScrArea *prevsa = cxt_wm_area(C);
      ARegion *prevar = cxt_wm_region(C);

      cxt_wm_window_set(C, win);
      cxt_wm_area_set(C, area);
      cxt_wm_region_set(C, NULL);

      ed_area_newspace(C, area, area->btnspacetype, true);
      ed_area_tag_redraw(area);

      /* Unset so that rna_Area_type_get uses spacetype instead. */
      area->btnspacetype = SPACE_EMPTY;

      /* It is possible that new layers becomes visible. */
      if (area->spacetype == SPACE_VIEW3D) {
        graph_tag_on_visible_update(cxt_data_main(C), false);
      }

      cxt_wm_window_set(C, prevwin);
      cxt_wm_area_set(C, prevsa);
      cxt_wm_region_set(C, prevar);
      break;
    }
  }
}

static const EnumPropItem *api_Area_ui_type_itemf(Cxt *C,
                                                  ApiPtr *ptr,
                                                  ApiProp *UNUSED(prop),
                                                  bool *r_free)
{
  EnumPropItem *item = NULL;
  int totitem = 0;

  ScrArea *area = (ScrArea *)ptr->data;
  const EnumPropItem *item_from = api_enum_space_type_items;
  if (area->spacetype != SPACE_EMPTY) {
    item_from += 1; /* +1 to skip SPACE_EMPTY */
  }

  for (; item_from->id; item_from++) {
    if (ELEM(item_from->value, SPACE_TOPBAR, SPACE_STATUSBAR)) {
      continue;
    }

    SpaceType *st = item_from->id[0] ? dune_spacetype_from_id(item_from->value) : NULL;
    int totitem_prev = totitem;
    if (st && st->space_subtype_item_extend != NULL) {
      st->space_subtype_item_extend(C, &item, &totitem);
      while (totitem_prev < totitem) {
        item[totitem_prev++].value |= item_from->value << 16;
      }
    }
    else {
      api_enum_item_add(&item, &totitem, item_from);
      item[totitem_prev++].value = item_from->value << 16;
    }
  }
  api_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static int api_Area_ui_type_get(ApiPtr *ptr)
{
  ScrArea *area = ptr->data;
  /* This is for the Python API which may inspect empty areas. */
  if (UNLIKELY(area->spacetype == SPACE_EMPTY)) {
    return SPACE_EMPTY;
  }
  const int area_type = api_Area_type_get(ptr);
  const bool area_changing = area->btnspacetype != SPACE_EMPTY;
  int value = area_type << 16;

  /* Area->type can be NULL when not yet initialized (for example when accessed
   * through the outliner or API when not visible), or it can be wrong while
   * the area type is changing.
   * So manually do the lookup in those cases, but do not actually change area->type
   * since that prevents a proper exit when the area type is changing.
   * Logic copied from `ED_area_init()`. */
  SpaceType *type = area->type;
  if (type == NULL || area_changing) {
    type = dune_spacetype_from_id(area_type);
    if (type == NULL) {
      type = dune_spacetype_from_id(SPACE_VIEW3D);
    }
    lib_assert(type != NULL);
  }
  if (type->space_subtype_item_extend != NULL) {
    value |= area_changing ? area->btnspacetype_subtype : type->space_subtype_get(area);
  }
  return value;
}

static void api_Area_ui_type_set(ApiPtr *ptr, int value)
{
  ScrArea *area = ptr->data;
  const int space_type = value >> 16;
  /* Empty areas are locked. */
  if ((space_type == SPACE_EMPTY) || (area->spacetype == SPACE_EMPTY)) {
    return;
  }
  SpaceType *st = dune_spacetype_from_id(space_type);

  api_Area_type_set(ptr, space_type);

  if (st && st->space_subtype_item_extend != NULL) {
    area->btnspacetype_subtype = value & 0xffff;
  }
}

static void api_Area_ui_type_update(Cxt *C, ApiPtr *ptr)
{
  ScrArea *area = ptr->data;
  SpaceType *st = dune_spacetype_from_id(area->btnspacetype);

  api_Area_type_update(C, ptr);

  if ((area->type == st) && (st->space_subtype_item_extend != NULL)) {
    st->space_subtype_set(area, area->btnspacetype_subtype);
  }
  area->btnspacetype_subtype = 0;

  ed_area_tag_refresh(area);
}

static ApiPtr api_Region_data_get(ApiPtr *ptr)
{
  Screen *screen = (Screen *)ptr->owner_id;
  ARegion *region = ptr->data;

  if (region->regiondata != NULL) {
    if (region->regiontype == RGN_TYPE_WINDOW) {
      /* We could make this static, it won't change at run-time. */
      SpaceType *st = dune_spacetype_from_id(SPACE_VIEW3D);
      if (region->type == dune_regiontype_from_id(st, region->regiontype)) {
        ApiPtr newptr;
        api_ptr_create(&screen->id, &Api_RegionView3D, region->regiondata, &newptr);
        return newptr;
      }
    }
  }
  return ApiPtr_NULL;
}

static void api_View2D_region_to_view(struct View2D *v2d, float x, float y, float result[2])
{
  ui_view2d_region_to_view(v2d, x, y, &result[0], &result[1]);
}

static void api_View2D_view_to_region(
    struct View2D *v2d, float x, float y, bool clip, int result[2])
{
  if (clip) {
    ui_view2d_view_to_region_clip(v2d, x, y, &result[0], &result[1]);
  }
  else {
    ui_view2d_view_to_region(v2d, x, y, &result[0], &result[1]);
  }
}

static const char *api_Screen_statusbar_info_get(struct Screen *UNUSED(screen),
                                                 Main *main,
                                                 Cxt *C)
{
  return ed_info_statusbar_string(main, cxt_data_scene(C), cxt_data_view_layer(C));
}

#else

/* Area.spaces */
static void api_def_area_spaces(DunrApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_prop_sapi(cprop, "AreaSpaces");
  sapi = api_def_struct(dapi, "AreaSpaces", NULL);
  api_def_struct_sdna(sapi, "ScrArea");
  api_def_struct_ui_text(sapi, "Area Spaces", "Collection of spaces");

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_sdna(prop, NULL, "spacedata.first");
  api_def_prop_struct_type(prop, "Space");
  api_def_prop_ui_text(prop, "Active Space", "Space currently being displayed in this area");
}

static void api_def_area_api(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  api_def_fn(sapi, "tag_redraw", "ed_area_tag_redraw");

  fn = api_def_fn(sapi, "header_text_set", "ed_area_status_text");
  api_def_fn_ui_description(fn, "Set the header status text")
  parm = api_def_string(
      fn, "text", NULL, 0, "Text", "New string for the header, None clears the text");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_prop_clear_flag(parm, PROP_NEVER_NULL);
}

static void api_def_area(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Area", NULL);
  api_def_struct_ui_text(sapi, "Area", "Area in a subdivided screen, containing an editor");
  api_def_struct_stype(sapi, "ScrArea");

  prop = api_def_prop(sapi, "spaces", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "spacedata", NULL);
  api_def_prop_struct_type(prop, "Space");
  api_def_prop_ui_text(prop,
                       "Spaces",
                       "Spaces contained in this area, the first being the active space "
                       "(NOTE: Useful for example to restore a previously used 3D view space "
                       "in a certain area to get the old view orientation)");
  api_def_area_spaces(dapi, prop);

  prop = api_def_prop(sapi, "regions", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "regionbase", NULL);
  api_def_prop_struct_type(prop, "Region");
  api_def_prop_ui_text(prop, "Regions", "Regions this area is subdivided in");

  prop = api_def_prop(sapi, "show_menus", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", HEADER_NO_PULLDOWN);
  api_def_prop_ui_text(prop, "Show Menus", "Show menus in the header");

  /* Note on space type use of #SPACE_EMPTY, this is not visible to the user,
   * and script authors should be able to assign this value, however the value may be set
   * and needs to be read back by script authors.
   *
   * This happens when an area is full-screen (when #ScrArea.full is set).
   * in this case reading the empty value is needed, but it should never be set, see: #87187. */
  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "spacetype");
  api_def_prop_enum_items(prop, api_enum_space_type_items);
  api_def_prop_enum_default(prop, SPACE_VIEW3D
  api_def_prop_enum_fns(prop, "rna_Area_type_get", "rna_Area_type_set", NULL);
  api_def_prop_ui_text(prop, "Editor Type", "Current editor type for this area");
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "rna_Area_type_update");

  prop = api_def_prop(sapi, "ui_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, DummyApi_NULL_items); /* in fact dummy */
  api_def_prop_enum_default(prop, SPACE_VIEW3D << 16);
  api_def_prop_enum_fns(
      prop, "api_Area_ui_type_get", "rna_Area_ui_type_set", "rna_Area_ui_type_itemf");
  api_def_prop_ui_text(prop, "Editor Type", "Current editor type for this area");
  api_def_prop_flag(prop, PROP_CONTEXT_UPDATE);
  apo_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "rna_Area_ui_type_update");

  prop = api_def_prop(sapi, "x", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "totrct.xmin");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "X Position", "The window relative vertical location of the area");

  prop = api_def_prop(sapi, "y", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "totrct.ymin");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Y Position", "The window relative horizontal location of the area");

  prop = api_def_prop(sapi, "width", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "winx");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Width", "Area width");

  prop = api_def_prop(sapi, "height", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_sapi(prop, NULL, "winy");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Height", "Area height");

  api_def_area_api(sapi);
}

static void api_def_view2d_api(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  static const float view_default[2] = {0.0f, 0.0f};
  static const int region_default[2] = {0.0f, 0.0f};

  fn = api_def_fn(sapi, "region_to_view", "rna_View2D_region_to_view");
  api_def_fn_ui_description(fn, "Transform region coordinates to 2D view");
  parm = api_def_float(fn, "x", 0, -FLT_MAX, FLT_MAX, "x", "Region x coordinate", -10000, 10000);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float(fn, "y", 0, -FLT_MAX, FLT_MAX, "y", "Region y coordinate", -10000, 10000);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float_array(fun,
                             "result",
                             2,
                             view_default,
                             -FLT_MAX,
                             FLT_MAX,
                             "Result",
                             "View coordinates",
                             -10000.0f,
                             10000.0f);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);

  fn = api_def_fn(sapi, "view_to_region", "api_View2D_view_to_region");
  api_def_fn_ui_description(fn, "Transform 2D view coordinates to region");
  parm = api_def_float(
      fn, "x", 0.0f, -FLT_MAX, FLT_MAX, "x", "2D View x coordinate", -10000.0f, 10000.0f);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float(
      fn, "y", 0.0f, -FLT_MAX, FLT_MAX, "y", "2D View y coordinate", -10000.0f, 10000.0f);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_bool(fn, "clip", 1, "Clip", "Clip coordinates to the visible region");
  parm = api_def_int_array(fn,
                           "result",
                           2,
                           region_default,
                           INT_MIN,
                           INT_MAX,
                           "Result",
                           "Region coordinates",
                           -10000,
                           10000);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);
}

static void api_def_view2d(DuneApi *dapi)
{
  ApiStruct *sapi;
  /* PropertyRNA *prop; */

  sapi = api_def_struct(dapi, "View2D", NULL);
  api_def_struct_ui_text(sapi, "View2D", "Scroll and zoom for a 2D region");
  api_def_struct_stype(sqpi, "View2D");

  /* TODO: more View2D properties could be exposed here (read-only). */

  api_def_view2d_api(api);
}

static void api_def_region(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem alignment_types[] = {
      {RGN_ALIGN_NONE, "NONE", 0, "None", "Don't use any fixed alignment, fill available space"},
      {RGN_ALIGN_TOP, "TOP", 0, "Top", ""},
      {RGN_ALIGN_BOTTOM, "BOTTOM", 0, "Bottom", ""},
      {RGN_ALIGN_LEFT, "LEFT", 0, "Left", ""},
      {RGN_ALIGN_RIGHT, "RIGHT", 0, "Right", ""},
      {RGN_ALIGN_HSPLIT, "HORIZONTAL_SPLIT", 0, "Horizontal Split", ""},
      {RGN_ALIGN_VSPLIT, "VERTICAL_SPLIT", 0, "Vertical Split", ""},
      {RGN_ALIGN_FLOAT,
       "FLOAT",
       0,
       "Float",
       "Region floats on screen, doesn't use any fixed alignment"},
      {RGN_ALIGN_QSPLIT,
       "QUAD_SPLIT",
       0,
       "Quad Split",
       "Region is split horizontally and vertically"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "Region", NULL);
  api_def_struct_ui_text(sapi, "Region", "Region in a subdivided screen area");
  api_def_struct_stype(sapi, "ARegion");

  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "regiontype");
  api_def_prop_enum_items(prop, rna_enum_region_type_items);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Region Type", "Type of this region");

  prop = api_def_prop(sapi, "x", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "winrct.xmin");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "X Position", "The window relative vertical location of the region");

  prop = api_def_prop(sapi, "y", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "winrct.ymin");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Y Position", "The window relative horizontal location of the region");

  prop = api_def_prop(sapi, "width", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_sdna(prop, NULL, "winx");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Width", "Region width");

  prop = api_def_prop(sapi, "height", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "winy");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Height", "Region height");

  prop = api_def_prop(sapi, "view2d", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "v2d");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "View2D", "2D view of the region");

  prop = api_def_prop(sapi, "alignment", PROP_ENUM, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_enum_items(prop, alignment_types);
  api_def_prop_enum_funcs(prop, "api_region_alignment_get", NULL, NULL);
  api_def_prop_ui_text(prop, "Alignment", "Alignment of the region within the area");

  prop = api_def_prop(sapi, "data", PROP_POINTER, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Region Data", "Region specific data (the type depends on the region type)");
  api_def_prop_struct_type(prop, "AnyType");
  api_def_prop_ptr_fns(prop, "api_Region_data_get", NULL, NULL, NULL);

  api_def_fn(sapi, "tag_redraw", "ed_region_tag_redraw");
}

static void api_def_screen(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "Screen", "ID");
  api_def_struct_stype(sapi, "Screen"); /* it is actually bScreen but for 2.5 the dna is patched! */
  api_def_struct_ui_text(
      sapi, "Screen", "Screen data-block, defining the layout of areas in a window");
  api_def_struct_ui_icon(sapi, ICON_WORKSPACE);

  /* collections */
  prop = api_def_prop(sapi, "areas", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_sdna(prop, NULL, "areabase", NULL);
  api_def_prop_struct_type(prop, "Area");
  api_def_prop_ui_text(prop, "Areas", "Areas the screen is subdivided into");

  /* readonly status indicators */
  prop = api_def_prop(sapi, "is_animation_playing", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_Screen_is_animation_playing_get", NULL);
  api_def_prop_ui_text(prop, "Animation Playing", "Animation playback is active");

  prop = api_def_prop(sapi, "is_scrubbing", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);l
  api_def_prop_bool_fns(prop, "api_Screen_is_scrubbing_get", NULL);
  api_def_prop_ui_text(
      prop, "User is Scrubbing", "True when the user is scrubbing through time");

  prop = api_def_prop(sapi, "is_temporary", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_stype(prop, NULL, "temp", 1);
  api_def_prop_ui_text(prop, "Temporary", "");

  prop = api_def_prop(sapi, "show_fullscreen", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_Screen_fullscreen_get", NULL);
  api_def_prop_ui_text(prop, "Maximize", "An area is maximized, filling this screen");

  /* Status Bar. */

  prop = api_def_prop(sapi, "show_statusbar", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_styoe(prop, NULL, "flag", SCREEN_COLLAPSE_STATUSBAR);
  api_def_prop_ui_text(prop, "Show Status Bar", "Show status bar");
  api_def_prop_update(prop, 0, "api_Screen_bar_update");

  fn = api_def_fn(sapi, "statusbar_info", "rna_Screen_statusbar_info_get");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_CXT);
  parm = api_def_string(fn, "statusbar_info", NULL, 0, "Status Bar Info", "");
  api_def_fn_return(fn, parm);

  /* Define Anim Playback Areas */
  prop = api_def_prop(sapi, "use_play_top_left_3d_editor", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "redraws_flag", TIME_REGION);
  api_def_prop_ui_text(prop, "Top-Left 3D Editor", "");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TIME, "rna_Screen_redraw_update");

  prop = api_def_prop(sapi, "use_play_3d_editors", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "redraws_flag", TIME_ALL_3D_WIN);
  api_def_prop_ui_text(prop, "All 3D Viewports", "");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TIME, "rna_Screen_redraw_update");

  prop = api_def_prop(sapi, "use_follow", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "redraws_flag", TIME_FOLLOW);
  api_def_prop_ui_text(prop, "Follow", "Follow current frame in editors");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TIME, "rna_Screen_redraw_update");

  prop = api_def_prop(sapi, "use_play_animation_editors", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "redraws_flag", TIME_ALL_ANIM_WIN);
  api_def_prop_ui_text(prop, "Animation Editors", "");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TIME, "api_Screen_redraw_update");

  prop = api_def_prop(sapi, "use_play_properties_editors", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "redraws_flag", TIME_ALL_BUTS_WIN);
  api_def_prop_ui_text(prop, "Property Editors", "");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TIME, "rna_Screen_redraw_update");

  prop = api_def_prop(sapi, "use_play_image_editors", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "redraws_flag", TIME_ALL_IMAGE_WIN);
  api_def_prop_ui_text(prop, "Image Editors", "");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TIME, "rna_Screen_redraw_update");

  prop = api_def_prop(sapi, "use_play_sequence_editors", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "redraws_flag", TIME_SEQ);
  api_def_prop_ui_text(prop, "Sequencer Editors", "");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TIME, "api_Screen_redraw_update");
    
  prop = api_def_prop(sapi, "use_play_node_editors", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "redraws_flag", TIME_NODES);
  api_def_prop_ui_text(prop, "Node Editors", "");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TIME, "api_Screen_redraw_update");

  prop = api_def_prop(sapi, "use_play_clip_editors", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "redraws_flag", TIME_CLIPS);
  api_def_prop_ui_text(prop, "Clip Editors", "");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TIME, "api_Screen_redraw_update");
}

void api_def_screen(DuneApi *api)
{
  api_def_screen(api);
  api_def_area(api);
  api_def_region(api);
  api_def_view2d(api);
}

#endif
