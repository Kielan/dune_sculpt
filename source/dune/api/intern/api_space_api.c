#include "types_object.h"

#include "api_access.h"
#include "api_define.h"

#include "api_internal.h"

#ifdef API_RUNTIME

#  include "dune_global.h"

#  include "ed_fileselect.h"
#  include "ed_screen.h"
#  include "ed_text.h"

int api_object_type_visibility_icon_get_common(int object_type_exclude_viewport,
                                               const int *object_type_exclude_select)
{
  const int view_value = (object_type_exclude_viewport != 0);

  if (object_type_exclude_select) {
    /* Ignore selection values when view is off,
     * intent is to show if visible objects aren't selectable. */
    const int select_value = (*object_type_exclude_select & ~object_type_exclude_viewport) != 0;
    return ICON_VIS_SEL_11 + (view_value << 1) + select_value;
  }

  return view_value ? ICON_HIDE_ON : ICON_HIDE_OFF;
}

static void api_RegionView3D_update(Id *id, RegionView3D *rv3d, Cxt *C)
{
  Screen *screen = (Screen *)id;

  ScrArea *area;
  ARegion *region;

  area_region_from_regiondata(screen, rv3d, &area, &region);

  if (area && region && area->spacetype == SPACE_VIEW3D) {
    Main *main = ctx_data_main(C);
    View3D *v3d = area->spacedata.first;
    wmWindowManager *wm = cxt_wm_manager(C);
    wmWindow *win;

    for (win = wm->windows.first; win; win = win->next) {
      if (wm_window_get_active_screen(win) == screen) {
        Scene *scene = wm_window_get_active_scene(win);
        ViewLayer *view_layer = wm_window_get_active_view_layer(win);
        Graph *graph = dune_scene_ensure_graph(main, scene, view_layer);

        ed_view3d_update_viewmat(graph, scene, v3d, region, NULL, NULL, NULL, false);
        break;
      }
    }
  }
}

static void api_SpaceTextEditor_region_location_from_cursor(
    Id *id, SpaceText *st, int line, int column, int r_pixel_pos[2])
{
  Screen *screen = (Screen *)id;
  ScrArea *area = dune_screen_find_area_from_space(screen, (SpaceLink *)st);
  if (area) {
    ARegion *region = dune_area_find_region_type(area, RGN_TYPE_WINDOW);
    const int cursor_co[2] = {line, column};
    ed_text_region_location_from_cursor(st, region, cursor_co, r_pixel_pos);
  }
}

#else

void api_region_view3d(ApiStruct *sapi)
{
  ApiFn *fn;

  fn = api_def_fn(sapi, "update", "api_RegionView3D_update");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_CXT);
  api_def_fn_ui_description(fn, "Recalculate the view matrices");
}

void api_space_node(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(
      sapi, "cursor_location_from_region", "api_SpaceNodeEditor_cursor_location_from_region");
  api_def_fn_ui_description(fb, "Set the cursor location using region coordinates");
  api_def_fn_flag(fn, FN_USE_CXT);
  parm = api_def_int(fn, "x", 0, INT_MIN, INT_MAX, "x", "Region x coordinate", -10000, 10000);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "y", 0, INT_MIN, INT_MAX, "y", "Region y coordinate", -10000, 10000);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
}

void api_space_text(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(
      sapi, "region_location_from_cursor", "api_SpaceTextEditor_region_location_from_cursor");
  api_def_fn_ui_description(
      fn, "Retrieve the region position from the given line and character position");
  api_def_fn_flag(fn, FN_USE_SELF_ID);
  parm = api_def_int(fn, "line", 0, INT_MIN, INT_MAX, "Line", "Line index", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "column", 0, INT_MIN, INT_MAX, "Column", "Column index", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int_array(
      fn, "result", 2, NULL, -1, INT_MAX, "", "Region coordinates", -1, INT_MAX);
  api_def_fn_output(fn, parm);
}

void api_def_object_type_visibility_flags_common(ApiStruct *sapi,
                                                 int noteflag,
                                                 const char *update_fn)
{
  ApiProp *prop;

  struct {
    const char *name;
    int type_mask;
    const char *identifier[2];
  } info[] = {
      {"Mesh", (1 << OB_MESH), {"show_object_viewport_mesh", "show_object_select_mesh"}},
      {"Curve",
       (1 << OB_CURVES_LEGACY),
       {"show_object_viewport_curve", "show_object_select_curve"}},
      {"Surface", (1 << OB_SURF), {"show_object_viewport_surf", "show_object_select_surf"}},
      {"Meta", (1 << OB_MBALL), {"show_object_viewport_meta", "show_object_select_meta"}},
      {"Font", (1 << OB_FONT), {"show_object_viewport_font", "show_object_select_font"}},
      {"Hair Curves",
       (1 << OB_CURVES),
       {"show_object_viewport_curves", "show_object_select_curves"}},
      {"Point Cloud",
       (1 << OB_POINTCLOUD),
       {"show_object_viewport_pointcloud", "show_object_select_pointcloud"}},
      {"Volume", (1 << OB_VOLUME), {"show_object_viewport_volume", "show_object_select_volume"}},
      {"Armature",
       (1 << OB_ARMATURE),
       {"show_object_viewport_armature", "show_object_select_armature"}},
      {"Lattice",
       (1 << OB_LATTICE),
       {"show_object_viewport_lattice", "show_object_select_lattice"}},
      {"Empty", (1 << OB_EMPTY), {"show_object_viewport_empty", "show_object_select_empty"}},
      {"Grease Pencil",
       (1 << OB_GPENCIL_LEGACY),
       {"show_object_viewport_grease_pencil", "show_object_select_grease_pencil"}},
      {"Camera", (1 << OB_CAMERA), {"show_object_viewport_camera", "show_object_select_camera"}},
      {"Light", (1 << OB_LAMP), {"show_object_viewport_light", "show_object_select_light"}},
      {"Speaker",
       (1 << OB_SPEAKER),
       {"show_object_viewport_speaker", "show_object_select_speaker"}},
      {"Light Probe",
       (1 << OB_LIGHTPROBE),
       {"show_object_viewport_light_probe", "show_object_select_light_probe"}},
  };

  const char *view_mask_member[2] = {
      "object_type_exclude_viewport",
      "object_type_exclude_select",
  };
  for (int mask_index = 0; mask_index < 2; mask_index++) {
    for (int type_index = 0; type_index < ARRAY_SIZE(info); type_index++) {
      prop = api_def_prop(
          sapi, info[type_index].id[mask_index], PROP_BOOL, PROP_NONE);
      api_def_prop_bool_negative_stype(
          prop, NULL, view_mask_member[mask_index], info[type_index].type_mask);
      api_def_prop_ui_text(prop, info[type_index].name, "");
      api_def_prop_update(prop, noteflag, update_func);
    }
  }
}

void api_space_filebrowser(ApiStruct *sapi)
{
  ApiFn *fn;
  ApiProp *parm;

  fn = api_def_fn(sapi, "activate_asset_by_id", "ED_fileselect_activate_by_id");
  api_def_fn_ui_description(
      fn, "Activate and select the asset entry that represents the given ID");

  parm = api_def_prop(fn, "id_to_activate", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(parm, "ID");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  parm = RNA_def_boolean(
      func,
      "deferred",
      0,
      "",
      "Whether to activate the ID immediately (false) or after the file browser refreshes (true)");

  /* Select file by relative path. */
  func = RNA_def_function(
      srna, "activate_file_by_relative_path", "ED_fileselect_activate_by_relpath");
  RNA_def_function_ui_description(func,
                                  "Set active file and add to selection based on relative path to "
                                  "current File Browser directory");
  RNA_def_property(func, "relative_path", PROP_STRING, PROP_FILEPATH);

  /* Deselect all files. */
  func = RNA_def_function(srna, "deselect_all", "ED_fileselect_deselect_all");
  RNA_def_function_ui_description(func, "Deselect all files");
}

#endif
