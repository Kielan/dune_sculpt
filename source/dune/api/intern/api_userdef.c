#include <limits.h>
#include <stdlib.h>

#include "types_brush.h"
#include "types_curve.h"
#include "types_scene.h"
#include "types_space.h"
#include "types_userdef.h"
#include "types_view3d.h"

#include "lib_math_base.h"
#include "lib_math_rotation.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_addon.h"
#include "dune_appdir.h"
#include "dune_sound.h"
#include "dune_studiolight.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "gpu_platform.h"

#include "ui_icons.h"

#include "api_internal.h"

#include "wm_api.h"
#include "wm_types.h"

#include "lang.h"

const EnumPropItem api_enum_pref_section_items[] = {
    {USER_SECTION_INTERFACE, "INTERFACE", 0, "Interface", ""},
    {USER_SECTION_THEME, "THEMES", 0, "Themes", ""},
    {USER_SECTION_VIEWPORT, "VIEWPORT", 0, "Viewport", ""},
    {USER_SECTION_LIGHT, "LIGHTS", 0, "Lights", ""},
    {USER_SECTION_EDITING, "EDITING", 0, "Editing", ""},
    {USER_SECTION_ANIMATION, "ANIMATION", 0, "Animation", ""},
    API_ENUM_ITEM_SEPR,
    {USER_SECTION_ADDONS, "ADDONS", 0, "Add-ons", ""},
#if 0 /* def WITH_USERDEF_WORKSPACES */
    API_ENUM_ITEM_SEPR,
    {USER_SECTION_WORKSPACE_CONFIG, "WORKSPACE_CONFIG", 0, "Configuration File", ""},
    {USER_SECTION_WORKSPACE_ADDONS, "WORKSPACE_ADDONS", 0, "Add-on Overrides", ""},
    {USER_SECTION_WORKSPACE_KEYMAPS, "WORKSPACE_KEYMAPS", 0, "Keymap Overrides", ""},
#endif
    API_ENUM_ITEM_SEPR,
    {USER_SECTION_INPUT, "INPUT", 0, "Input", ""},
    {USER_SECTION_NAVIGATION, "NAVIGATION", 0, "Navigation", ""},
    {USER_SECTION_KEYMAP, "KEYMAP", 0, "Keymap", ""},
    API_ENUM_ITEM_SEPR,
    {USER_SECTION_SYSTEM, "SYSTEM", 0, "System", ""},
    {USER_SECTION_SAVE_LOAD, "SAVE_LOAD", 0, "Save & Load", ""},
    {USER_SECTION_FILE_PATHS, "FILE_PATHS", 0, "File Paths", ""},
    API_ENUM_ITEM_SEPR,
    {USER_SECTION_EXPERIMENTAL, "EXPERIMENTAL", 0, "Experimental", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem audio_device_items[] = {
    {0, "None", 0, "None", "No device - there will be no audio output"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_navigation_mode_items[] = {
    {VIEW_NAVIGATION_WALK,
     "WALK",
     0,
     "Walk",
     "Interactively walk or free navigate around the scene"},
    {VIEW_NAVIGATION_FLY, "FLY", 0, "Fly", "Use fly dynamics to navigate the scene"},
    {0, NULL, 0, NULL, NULL},
};

#if defined(WITH_INTERNATIONAL) || !defined(API_RUNTIME)
static const EnumPropItem api_enum_language_default_items[] = {
    {0,
     "DEFAULT",
     0,
     "Automatic (Automatic)",
     "Automatically choose system's defined language if available, or fall-back to English"},
    {0, NULL, 0, NULL, NULL},
};
#endif

static const EnumPropItem api_enum_studio_light_type_items[] = {
    {STUDIOLIGHT_TYPE_STUDIO, "STUDIO", 0, "Studio", ""},
    {STUDIOLIGHT_TYPE_WORLD, "WORLD", 0, "World", ""},
    {STUDIOLIGHT_TYPE_MATCAP, "MATCAP", 0, "MatCap", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_userdef_viewport_aa_items[] = {
    {SCE_DISPLAY_AA_OFF,
     "OFF",
     0,
     "No Anti-Aliasing",
     "Scene will be rendering without any anti-aliasing"},
    {SCE_DISPLAY_AA_FXAA,
     "FXAA",
     0,
     "Single Pass Anti-Aliasing",
     "Scene will be rendered using a single pass anti-aliasing method (FXAA)"},
    {SCE_DISPLAY_AA_SAMPLES_5,
     "5",
     0,
     "5 Samples",
     "Scene will be rendered using 5 anti-aliasing samples"},
    {SCE_DISPLAY_AA_SAMPLES_8,
     "8",
     0,
     "8 Samples",
     "Scene will be rendered using 8 anti-aliasing samples"},
    {SCE_DISPLAY_AA_SAMPLES_11,
     "11",
     0,
     "11 Samples",
     "Scene will be rendered using 11 anti-aliasing samples"},
    {SCE_DISPLAY_AA_SAMPLES_16,
     "16",
     0,
     "16 Samples",
     "Scene will be rendered using 16 anti-aliasing samples"},
    {SCE_DISPLAY_AA_SAMPLES_32,
     "32",
     0,
     "32 Samples",
     "Scene will be rendered using 32 anti-aliasing samples"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_pref_gpu_backend_items[] = {
    {GPU_BACKEND_OPENGL, "OPENGL", 0, "OpenGL", "Use OpenGL backend"},
    {GPU_BACKEND_METAL, "METAL", 0, "Metal", "Use Metal backend"},
    {GPU_BACKEND_VULKAN, "VULKAN", 0, "Vulkan", "Use Vulkan backend"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "lib_math_vector.h"
#  include "lib_string_utils.h"

#  include "types_object.h"
#  include "types_screen.h"

#  include "dune.h"
#  include "dune_global.h"
#  include "dune_idprop.h"
#  include "dune_image.h"
#  include "dune_main.h"
#  include "dune_mesh_runtime.h"
#  include "dune_object.h"
#  include "dune_paint.h"
#  include "dune_pbvh.h"
#  include "dune_prefs.h"
#  include "dune_screen.h"

#  include "graph.h"

#  include "gpu_capabilities.h"
#  include "gpu_select.h"
#  include "gpu_texture.h"

#  include "BLF_api.h"

#  include "lib_path_util.h"

#  include "mem_CacheLimiterC-Api.h"
#  include "mem_guardedalloc.h"

#  include "ui_interface.h"

#  ifdef WITH_SDL_DYNLOAD
#    include "sdlew.h"
#  endif

static void api_userdef_version_get(ApiPtr *ptr, int *value)
{
  UserDef *userdef = (UserDef *)ptr->data;
  value[0] = userdef->versionfile / 100;
  value[1] = userdef->versionfile % 100;
  value[2] = userdef->subversionfile;
}

/** Mark the preferences as being changed so they are saved on exit. */
#  define USERDEF_TAG_DIRTY api_userdef_is_dirty_update_impl()

void api_userdef_is_dirty_update_impl(void)
{
  /* We can't use 'ptr->data' because this update function
   * is used for themes and other nested data. */
  if (U.runtime.is_dirty == false) {
    U.runtime.is_dirty = true;
    wm_main_add_notifier(NC_WINDOW, NULL);
  }
}

void api_userdef_is_dirty_update(Main *UNUSED(main),
                                 Scene *UNUSED(scene),
                                 ApiPtr *UNUSED(ptr))
{
  /* WARNING: never use 'ptr' unless its type is checked. */
  api_userdef_is_dirty_update_impl();
}

/** Take care not to use this if we expect 'is_dirty' to be tagged. */
static void api_userdef_ui_update(Main *UNUSED(main),
                                  Scene *UNUSED(scene),
                                  ApiPtr *UNUSED(ptr))
{
  wm_main_add_notifier(NC_WINDOW, NULL);
}

static void api_userdef_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *UNUSED(ptr))
{
  wm_main_add_notifier(NC_WINDOW, NULL);
  USERDEF_TAG_DIRTY;
}

static void api_userdef_theme_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  /* Recreate gizmos when changing themes. */
  wm_reinit_gizmomap_all(main);

  api_userdef_update(main, scene, ptr);
}

static void api_userdef_theme_text_style_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  const uiStyle *style = UI_style_get();
  BLF_default_size(style->widgetlabel.points);

  api_userdef_update(main, scene, ptr);
}

static void api_userdef_gizmo_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  wm_reinit_gizmomap_all(main);

  api_userdef_update(main, scene, ptr);
}

static void api_userdef_theme_update_icons(Main *main, Scene *scene, ApiPtr *ptr)
{
  if (!G.background) {
    ui_icons_reload_internal_textures();
  }
  api_userdef_theme_update(main, scene, ptr);
}

/* also used by buffer swap switching */
static void api_userdef_dpi_update(Main *UNUSED(main),
                                   Scene *UNUSED(scene),
                                   ApiPtr *UNUSED(ptr))
{
  /* font's are stored at each DPI level, without this we can easy load 100's of fonts */
  BLF_cache_clear();

  wm_main_add_notifier(NC_WINDOW, NULL);             /* full redraw */
  wm_main_add_notifier(NC_SCREEN | NA_EDITED, NULL); /* refresh region sizes */
  USERDEF_TAG_DIRTY;
}

static void api_userdef_screen_update(Main *UNUSED(main),
                                      Scene *UNUSED(scene),
                                      ApiPtr *UNUSED(ptr))
{
  wm_main_add_notifier(NC_WINDOW, NULL);
  wm_main_add_notifier(NC_SCREEN | NA_EDITED, NULL); /* refresh region sizes */
  USERDEF_TAG_DIRTY;
}

static void api_userdef_screen_update_header_default(Main *main, Scene *scene, ApiPtr *ptr)
{
  if (U.uiflag & USER_HEADER_FROM_PREF) {
    for (Screen *screen = main->screens.first; screen; screen = screen->id.next) {
      dune_screen_header_alignment_reset(screen);
    }
    api_userdef_screen_update(main, scene, ptr);
  }
  USERDEF_TAG_DIRTY;
}

static void api_userdef_font_update(Main *UNUSED(main),
                                    Scene *UNUSED(scene),
                                    ApiPtr *UNUSED(ptr))
{
  BLF_cache_clear();
  ui_reinit_font();
}

static void api_userdef_lang_update(Main *UNUSED(main),
                                    Scene *UNUSED(scene),
                                    ApiPtr *UNUSED(ptr))
{
  lang_set(NULL);

  const char *uilng = lang_get();
  if (STREQ(uilng, "en_US")) {
    U.transopts &= ~(USER_TR_IFACE | USER_TR_TOOLTIPS | USER_TR_NEWDATANAME);
  } else {
    U.transopts |= (USER_TR_IFACE | USER_TR_TOOLTIPS | USER_TR_NEWDATANAME);
  }

  USERDEF_TAG_DIRTY;
}

static void api_userdef_asset_lib_name_set(ApiPtr *ptr, const char *value)
{
  UserAssetLib *lib = (UserAssetLib *)ptr->data;
  dune_pref_asset_lib_name_set(&U, lib, value);
}

static void api_userdef_asset_lib_path_set(ApiPtr *ptr, const char *value)
{
  UserAssetLib *lib = UserAssetLib *)ptr->data;
  dune_pref_asset_lib_path_set(lib, value);
}

static void api_userdef_script_autoexec_update(Main *UNUSED(main),
                                               Scene *UNUSED(scene),
                                               ApiPtr *ptr)
{
  UserDef *userdef = (UserDef *)ptr->data;
  if (userdef->flag & USER_SCRIPT_AUTOEX_DISABLE) {
    G.f &= ~G_FLAG_SCRIPT_AUTOEX;
  } else {
    G.f |= G_FLAG_SCRIPT_AUTOEX;
  }

  USERDEF_TAG_DIRTY;
}

static void api_userdef_script_dir_name_set(ApiPtr *ptr, const char *value)
{
  UserScriptDir *script_dir = ptr->data;
  bool value_invalid = false;

  if (!value[0]) {
    value_invalid = true;
  }
  if (STREQ(value, "DEFAULT")) {
    value_invalid = true;
  }

  if (value_invalid) {
    value = DATA_("Untitled");
  }

  STRNCPY_UTF8(script_dir->name, value);
  lib_uniquename(&U.script_dirs,
                 script_dir,
                 value,
                 '.',
                 offsetof(UserScriptDir, name),
                 sizeof(script_dir->name));
}

static UserScriptDir *api_userdef_script_dir_new(void)
{
  UserScriptDir *script_dir = mem_callocn(sizeof(*script_dir), __func__);
  lib_addtail(&U.script_dirs, script_dir);
  USERDEF_TAG_DIRTY;
  return script_dir;
}

static void api_userdef_script_dir_remove(ReportList *reports, ApiPtr *ptr)
{
  UserScriptDir *script_dir = ptr->data;
  if (lib_findindex(&U.script_dirs, script_dir) == -1) {
    dune_report(reports, RPT_ERROR, "Script directory not found");
    return;
  }

  lib_freelinkn(&U.script_directories, script_dir);
  API_PTR_INVALIDATE(ptr);
  USERDEF_TAG_DIRTY;
}

static void api_userdef_load_ui_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  UserDef *userdef = (UserDef *)ptr->data;
  if (userdef->flag & USER_FILENOUI) {
    G.fileflags |= G_FILE_NO_UI;
  } else {
    G.fileflags &= ~G_FILE_NO_UI;
  }

  USERDEF_TAG_DIRTY;
}

static void api_userdef_anisotropic_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  gpu_samplers_update();
  api_userdef_update(main, scene, ptr);
}

static void api_userdef_gl_texture_limit_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  dune_image_free_all_gputextures(main);
  api_userdef_update(main, scene, ptr);
}

static void api_userdef_undo_steps_set(ApiPtr *ptr, int value)
{
  UserDef *userdef = (UserDef *)ptr->data;

  /* Do not allow 1 undo steps, useless and breaks undo/redo process (see #42531). */
  userdef->undosteps = (value == 1) ? 2 : value;
}

static int api_userdef_autokeymode_get(ApiPtr *ptr)
{
  UserDef *userdef = (UserDef *)ptr->data;
  short retval = userdef->autokey_mode;

  if (!(userdef->autokey_mode & AUTOKEY_ON)) {
    retval |= AUTOKEY_ON;
  }

  return retval;
}

static void api_userdef_autokeymode_set(ApiPtr *ptr, int value)
{
  UserDef *userdef = (UserDef *)ptr->data;

  if (value == AUTOKEY_MODE_NORMAL) {
    userdef->autokey_mode |= (AUTOKEY_MODE_NORMAL - AUTOKEY_ON);
    userdef->autokey_mode &= ~(AUTOKEY_MODE_EDITKEYS - AUTOKEY_ON);
  } else if (value == AUTOKEY_MODE_EDITKEYS) {
    userdef->autokey_mode |= (AUTOKEY_MODE_EDITKEYS - AUTOKEY_ON);
    userdef->autokey_mode &= ~(AUTOKEY_MODE_NORMAL - AUTOKEY_ON);
  }
}

static void api_userdef_anim_update(Main *UNUSED(main),
                                    Scene *UNUSED(scene),
                                    ApiPtr *UNUSED(ptr))
{
  wm_main_add_notifier(NC_SPACE | ND_SPACE_GRAPH, NULL);
  wm_main_add_notifier(NC_SPACE | ND_SPACE_DOPESHEET, NULL);
  USERDEF_TAG_DIRTY;
}

static void api_userdef_input_devices(Main *UNUSED(main),
                                      Scene *UNUSED(scene),
                                      ApiPtr *UNUSED(ptr))
{
  wm_init_input_devices();
  USERDEF_TAG_DIRTY;
}

#  ifdef WITH_INPUT_NDOF
static void api_userdef_ndof_deadzone_update(Main *UNUSED(main),
                                             Scene *UNUSED(scene),
                                             ApiPtr *ptr)
{
  UserDef *userdef = ptr->data;
  wm_ndof_deadzone_set(userdef->ndof_deadzone);
  USERDEF_TAG_DIRTY;
}
#  endif

static void api_userdef_keyconfig_reload_update(Ctx *C,
                                                Main *UNUSED(main),
                                                Scene *UNUSED(scene),
                                                ApiPtr *UNUSED(ptr))
{
  wm_keyconfig_reload(C);
  USERDEF_TAG_DIRTY;
}

static void api_userdef_timecode_style_set(ApiPtr *ptr, int value)
{
  UserDef *userdef = (UserDef *)ptr->data;
  int required_size = userdef->v2d_min_gridsize;

  /* Set the time-code style. */
  userdef->timecode_style = value;

  /* Adjust the v2d grid-size if needed so that time-codes don't overlap
   * NOTE: most of these have been hand-picked to avoid overlaps while still keeping
   * things from getting too blown out. */
  switch (value) {
    case USER_TIMECODE_MINIMAL:
    case USER_TIMECODE_SECONDS_ONLY:
      /* 35 is great most of the time, but not that great for full-blown */
      required_size = 35;
      break;
    case USER_TIMECODE_SMPTE_MSF:
      required_size = 50;
      break;
    case USER_TIMECODE_SMPTE_FULL:
      /* the granddaddy! */
      required_size = 65;
      break;
    case USER_TIMECODE_MILLISECONDS:
      required_size = 45;
      break;
  }

  if (U.v2d_min_gridsize < required_size) {
    U.v2d_min_gridsize = required_size;
  }
}

static int api_UserDef_mouse_emulate_3_btn_mod_get(ApiPtr *ptr)
{
#  if !defined(WIN32)
  UserDef *userdef = ptr->data;
  return userdef->mouse_emulate_3_button_modifier;
#  else
  UNUSED_VARS(ptr);
  return USER_EMU_MMB_MOD_ALT;
#  endif
}

static const EnumPropItem *api_UseDef_active_section_itemf(Cxt *UNUSED(C),
                                                           ApiPtr *ptr,
                                                           ApiProp *UNUSED(prop),
                                                           bool *r_free)
{
  UserDef *userdef = ptr->data;

  if ((userdef->flag & USER_DEVELOPER_UI) != 0) {
    *r_free = false;
    return api_enum_pref_section_items;
  }

  EnumPropItem *items = NULL;
  int totitem = 0;

  for (const EnumPropItem *it = api_enum_pref_section_items; it->id != NULL;
       it++) {
    if (it->value == USER_SECTION_EXPERIMENTAL) {
      continue;
    }
    api_enum_item_add(&items, &totitem, it);
  }

  api_enum_item_end(&items, &totitem);

  *r_free = true;
  return items;
}

static ApiPtr api_UserDef_view_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiPrefsView, ptr->data);
}

static ApiPtr api_UserDef_edit_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiPrefsEdit, ptr->data);
}

static ApiPtr api_UserDef_input_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiPrefsInput, ptr->data);
}

static ApiPtr api_UserDef_keymap_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiPrefsKeymap, ptr->data);
}

static ApiPtr api_UserDef_filepaths_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiPrefsFilePaths, ptr->data);
}

static ApiPtr api_UserDef_system_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiPrefsSystem, ptr->data);
}

static ApiPtr api_UserDef_apps_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiPrefsApps, ptr->data);
}

/* Reevaluate objects with a subsurf modifier as the last in their modifiers stacks. */
static void api_UserDef_subdivision_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  Object *ob;

  for (ob = main->objects.first; ob; ob = ob->id.next) {
    if (dune_object_get_last_subsurf_mod(ob) != NULL) {
      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    }
  }

  api_userdef_update(main, scene, ptr);
}

static void api_UserDef_audio_update(Main *main, Scene *UNUSED(scene), ApiPtr *UNUSED(ptr))
{
  dune_sound_init(main);
  USERDEF_TAG_DIRTY;
}

static void api_Userdef_memcache_update(Main *UNUSED(main),
                                        Scene *UNUSED(scene),
                                        ApiPtr *UNUSED(ptr))
{
  mem_CacheLimiter_set_maximum(((size_t)U.memcachelimit) * 1024 * 1024);
  USERDEF_TAG_DIRTY;
}

static void api_Userdef_disk_cache_dir_update(Main *UNUSED(main),
                                              Scene *UNUSED(scene),
                                              ApiPtr *UNUSED(ptr))
{
  if (U.seq_disk_cache_dir[0] != '\0') {
    lib_path_abs(U.seq_disk_cache_dir, dune_main_dunefile_path_from_global());
    lib_path_slash_ensure(U.seq_disk_cache_dir, sizeof(U.seq_disk_cache_dir));
    lib_path_make_safe(U.seq_disk_cache_dir);
  }

  USERDEF_TAG_DIRTY;
}

static void api_UserDef_weight_color_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  Object *ob;

  for (ob = main->objects.first; ob; ob = ob->id.next) {
    if (ob->mode & OB_MODE_WEIGHT_PAINT) {
      graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
    }
  }

  api_userdef_update(main, scene, ptr);
}

static void api_UserDef_viewport_lights_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  /* If all lights are off gpu_draw resets them all, see: #27627,
   * so disallow them all to be disabled. */
  if (U.light_param[0].flag == 0 && U.light_param[1].flag == 0 && U.light_param[2].flag == 0 &&
      U.light_param[3].flag == 0)
  {
    SolidLight *light = ptr->data;
    light->flag |= 1;
  }

  wm_main_add_notifier(NC_SPACE | ND_SPACE_VIEW3D | NS_VIEW3D_GPU, NULL);
  api_userdef_update(main, scene, ptr);
}

static void api_userdef_autosave_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  wmWindowManager *wm = main->wm.first;

  if (wm) {
    wm_file_autosave_init(wm);
  }
  api_userdef_update(main, scene, ptr);
}

#  define API_USERDEF_EXPERIMENTAL_BOOL_GET(member) \
    static bool api_userdef_experimental_##member##_get(ApiPtr *ptr) \
    { \
      UserDef *userdef = PTR_OFFSET(ptr->data, -offsetof(UserDef, experimental)); \
      return USER_EXPERIMENTAL_TEST(userdef, member); \
    }

static Addon *api_userdef_addon_new(void)
{
  List *addons_list = &U.addons;
  Addon *addon = dune_addon_new();
  lib_addtail(addons_list, addon);
  USERDEF_TAG_DIRTY;
  return addon;
}

static void api_userdef_addon_remove(ReportList *reports, ApiPtr *addon_ptr)
{
  List *addons_list = &U.addons;
  Addon *addon = addon_ptr->data;
  if (lib_findindex(addons_list, addon) == -1) {
    dune_report(reports, RPT_ERROR, "Add-on is no longer valid");
    return;
  }
  lib_remlink(addons_list, addon);
  dune_addon_free(addon);
  API_PTR_INVALIDATE(addon_ptr);
  USERDEF_TAG_DIRTY;
}

static PathCompare *api_userdef_pathcompare_new(void)
{
  PathCompare *path_cmp = mem_callocn(sizeof(PathCompare), "PathCompare");
  lib_addtail(&U.autoex_paths, path_cmp);
  USERDEF_TAG_DIRTY;
  return path_cmp;
}

static void api_userdef_pathcompare_remove(ReportList *reports, ApiPtr *path_cmp_ptr)
{
  PathCompare *path_cmp = path_cmp_ptr->data;
  if (lib_findindex(&U.autoex_paths, path_cmp) == -1) {
    dune_report(reports, RPT_ERROR, "Excluded path is no longer valid");
    return;
  }

  lib_freelinkn(&U.autoex_paths, path_cmp);
  API_PTR_INVALIDATE(path_cmp_ptr);
  USERDEF_TAG_DIRTY;
}

static void api_userdef_temp_update(Main *UNUSED(main),
                                    Scene *UNUSED(scene),
                                    ApiPtr *UNUSED(ptr))
{
  dune_tempdir_init(U.tempdir);
  USERDEF_TAG_DIRTY;
}

static void api_userdef_text_update(Main *UNUSED(main),
                                    Scene *UNUSED(scene),
                                    ApiPtr *UNUSED(ptr))
{
  BLF_cache_clear();
  ui_reinit_font();
  wm_main_add_notifier(NC_WINDOW, NULL);
  USERDEF_TAG_DIRTY;
}

static ApiPtr api_Theme_space_generic_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiThemeSpaceGeneric, ptr->data);
}

static ApiPtr api_Theme_gradient_colors_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiThemeGradientColors, ptr->data);
}

static ApiPtr api_Theme_space_gradient_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiThemeSpaceGradient, ptr->data);
}

static ApiPtr api_Theme_space_list_generic_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiThemeSpaceListGeneric, ptr->data);
}

static const EnumPropItem *api_userdef_audio_device_itemf(Cxt *UNUSED(C),
                                                          ApiPtr *UNUSED(ptr),
                                                          ApiProp *UNUSED(prop),
                                                          bool *r_free)
{
  int index = 0;
  int totitem = 0;
  EnumPropItem *item = NULL;

  int i;

  char **names = dune_sound_get_device_names();

  for (i = 0; names[i]; i++) {
    EnumPropItem new_item = {i, names[i], 0, names[i], names[i]};
    api_enum_item_add(&item, &totitem, &new_item);
  }

#  if !defined(NDEBUG) || !defined(WITH_AUDASPACE)
  if (i == 0) {
    EnumPropItem new_item = {i, "SOUND_NONE", 0, "No Sound", ""};
    api_enum_item_add(&item, &totitem, &new_item);
  }
#  endif

  /* may be unused */
  UNUSED_VARS(index, audio_device_items);

  api_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

#  ifdef WITH_INTERNATIONAL
static const EnumPropItem *api_lang_enum_props_itemf(Cxt *UNUSED(C),
                                                     ApiPtr *UNUSED(ptr),
                                                     ApiProp *UNUSED(prop),
                                                     bool *UNUSED(r_free))
{
  const EnumPropItem *items = lang_api_enum_prop();
  if (items == NULL) {
    items = api_enum_language_default_items;
  }
  return items;
}
#  else
static int api_lang_enum_props_get_no_international(ApiPtr *UNUSED(ptr))
{
  /* This simply prevents warnings when accessing language
   * (since the actual value wont be in the enum, unless already `DEFAULT`). */
  return 0;
}
#  endif

static IdProp **api_AddonPref_idprops(ApiPtr *ptr)
{
  return (IdProp **)&ptr->data;
}

static ApiPtr api_Addon_pref_get(ApiPtr *ptr)
{
  Addon *addon = (Addon *)ptr->data;
  AddonPrefType *apt = dune_addon_pref_type_find(addon->module, true);
  if (apt) {
    if (addon->prop == NULL) {
      IdPropTemplate val = {0};
      addon->prop = IDP_New(IDP_GROUP, &val, addon->module); /* name is unimportant. */
    }
    return api_ptr_inherit_refine(ptr, apt->api_ext.sapi, addon->prop);
  }
  else {
    return ApiPtr_NULL;
  }
}

static bool api_AddonPref_unregister(Main *UNUSED(main), ApiStruct *type)
{
  AddonPrefType *apt = api_struct_dune_type_get(type);

  if (!apt) {
    return false;
  }

  api_struct_free_extension(type, &apt->api_ext);
  api_struct_free(&DUNE_API, type);

  dune_addon_pref_type_remove(apt);

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);
  return true;
}

static ApiStruct *api_AddonPref_register(Main *main,
                                         ReportList *reports,
                                         void *data,
                                         const char *id,
                                         StructValidateFn validate,
                                         StructCbFn call,
                                         StructFreeFb free)
{
  const char *error_prefix = "Registering add-on prefs class:";
  AddonPrefType *apt, dummy_apt = {{'\0'}};
  Addon dummy_addon = {NULL};
  ApiPtr dummy_addon_ptr;
  // bool have_fn[1];

  /* Setup dummy add-on preference and it's type to store static properties in. */
  api_ptr_create(NULL, &ApiAddonPrefs, &dummy_addon, &dummy_addon_ptr);

  /* validate the python class */
  if (validate(&dummy_addon_ptr, data, NULL /* have_function */) != 0) {
    return NULL;
  }

  STRNCPY(dummy_apt.idname, dummy_addon.module);
  if (strlen(id) >= sizeof(dummy_apt.idname)) {
    dune_reportf(reports,
                RPT_ERROR,
                "%s '%s' is too long, maximum length is %d",
                error_prefix,
                id,
                (int)sizeof(dummy_apt.idname));
    return NULL;
  }

  /* Check if we have registered this add-on preference type before, and remove it. */
  apt = dune_addon_pref_type_find(dummy_addon.module, true);
  if (apt) {
    ApiStruct *sapi = apt->api_ext.sapi;
    if (!(sapi && api_AddonPref_unregister(main, sapi))) {
      dune_reportf(reports,
                  RPT_ERROR,
                  "%s '%s', bl_idname '%s' %s",
                  error_prefix,
                  id,
                  dummy_apt.idname,
                  sapi ? "is built-in" : "could not be unregistered");

      return NULL;
    }
  }

  /* Create a new add-on preference type. */
  apt = mem_mallocn(sizeof(AddonPrefType), "addonpreftype");
  memcpy(apt, &dummy_apt, sizeof(dummy_apt));
  dune_addon_pref_type_add(apt);

  apt->api_ext.sapi = api_def_struct_ptr(&DUNE_API, id, &ApiAddonPrefs);
  apt->api_ext.data = data;
  apt->api_ext.call = call;
  apt->api_ext.free = free;
  api_struct_dune_type_set(apt->api_ext.sapi, apt);

  //  apt->draw = (have_fn[0]) ? header_draw : NULL;
  /* update while dune is running */
  wm_main_add_notifier(NC_WINDOW, NULL);

  return apt->api_ext.sapi;
}

/* placeholder, doesn't do anything useful yet */
static ApiStruct *api_AddonPref_refine(ApiPtr *ptr)
{
  return (ptr->type) ? ptr->type : &ApiAddonPrefs;
}

static float api_ThemeUI_roundness_get(ApiPtr *ptr)
{
  /* Remap from relative radius to 0..1 range. */
  uiWidgetColors *tui = (uiWidgetColors *)ptr->data;
  return tui->roundness * 2.0f;
}

static void api_ThemeUI_roundness_set(ApiPtr *ptr, float value)
{
  uiWidgetColors *tui = (uiWidgetColors *)ptr->data;
  tui->roundness = value * 0.5f;
}

/* Studio Light */
static void api_UserDef_studiolight_begin(CollectionPropIter *iter,
                                          ApiPtr *UNUSED(ptr))
{
  api_iter_list_begin(iter, dune_studiolight_list(), NULL);
}

static void api_StudioLights_refresh(UserDef *UNUSED(userdef))
{
  dune_studiolight_refresh();
}

static void api_StudioLights_remove(UserDef *UNUSED(userdef), StudioLight *studio_light)
{
  dune_studiolight_remove(studio_light);
}

static StudioLight *api_StudioLights_load(UserDef *UNUSED(userdef), const char *filepath, int type)
{
  return dune_studiolight_load(filepath, type);
}

/* TODO: Make it accept arguments. */
static StudioLight *api_StudioLights_new(UserDef *userdef, const char *filepath)
{
  return dune_studiolight_create(filepath, userdef->light_param, userdef->light_ambient);
}

/* StudioLight.name */
static void api_UserDef_studiolight_name_get(ApiPtr *ptr, char *value)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  strcpy(value, sl->name);
}

static int api_UserDef_studiolight_name_length(ApiPtr *ptr)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  return strlen(sl->name);
}

/* StudioLight.path */
static void api_UserDef_studiolight_path_get(ApiPtr *ptr, char *value)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  strcpy(value, sl->filepath);
}

static int api_UserDef_studiolight_path_length(ApiPtr *ptr)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  return strlen(sl->filepath);
}

/* StudioLight.path_irr_cache */
static void api_UserDef_studiolight_path_irr_cache_get(ApiPtr *ptr, char *value)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  if (sl->path_irr_cache) {
    strcpy(value, sl->path_irr_cache);
  } else {
    value[0] = '\0';
  }
}

static int api_UserDef_studiolight_path_irr_cache_length(ApiPtr *ptr)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  if (sl->path_irr_cache) {
    return strlen(sl->path_irr_cache);
  }
  return 0;
}

/* StudioLight.path_sh_cache */
static void api_UserDef_studiolight_path_sh_cache_get(ApiPtr *ptr, char *value)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  if (sl->path_sh_cache) {
    strcpy(value, sl->path_sh_cache);
  } else {
    value[0] = '\0';
  }
}

static int api_UserDef_studiolight_path_sh_cache_length(ApiPtr *ptr)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  if (sl->path_sh_cache) {
    return strlen(sl->path_sh_cache);
  }
  return 0;
}

/* StudioLight.index */
static int api_UserDef_studiolight_index_get(ApiPtr *ptr)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  return sl->index;
}

/* StudioLight.is_user_defined */
static bool api_UserDef_studiolight_is_user_defined_get(ApiPtr *ptr)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  return (sl->flag & STUDIOLIGHT_USER_DEFINED) != 0;
}

/* StudioLight.is_user_defined */
static bool api_UserDef_studiolight_has_specular_highlight_pass_get(ApiPtr *ptr)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  return sl->flag & STUDIOLIGHT_SPECULAR_HIGHLIGHT_PASS;
}

/* StudioLight.type */
static int api_UserDef_studiolight_type_get(ApiPtr *ptr)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  return sl->flag & STUDIOLIGHT_FLAG_ORIENTATIONS;
}

static void api_UserDef_studiolight_spherical_harmonics_coefficients_get(ApiPtr *ptr,
                                                                         float *values)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  float *value = values;
  for (int i = 0; i < STUDIOLIGHT_SH_EFFECTIVE_COEFS_LEN; i++) {
    copy_v3_v3(value, sl->spherical_harmonics_coefs[i]);
    value += 3;
  }
}

/* StudioLight.solid_lights */
static void api_UserDef_studiolight_solid_lights_begin(CollectionPropIter *iter,
                                                       ApiPtr *ptr)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  api_iter_array_begin(iter, sl->light, sizeof(*sl->light), ARRAY_SIZE(sl->light), 0, NULL);
}

static int api_UserDef_studiolight_solid_lights_length(ApiPtr *UNUSED(ptr))
{
  return ARRAY_SIZE(((StudioLight *)NULL)->light);
}

/* StudioLight.light_ambient */
static void api_UserDef_studiolight_light_ambient_get(ApiPtr *ptr, float *values)
{
  StudioLight *sl = (StudioLight *)ptr->data;
  copy_v3_v3(values, sl->light_ambient);
}

int api_show_statusbar_vram_editable(struct ApiPtr *UNUSED(ptr), const char **UNUSED(r_info))
{
  return gpu_mem_stats_supported() ? PROP_EDITABLE : 0;
}

static const EnumPropItem *api_pref_gpu_backend_itemf(struct Cxt *UNUSED(C),
                                                      ApiPtr *UNUSED(ptr),
                                                      ApiProp *UNUSED(prop),
                                                      bool *r_free)
{
  int totitem = 0;
  EnumPropItem *result = NULL;
  for (int i = 0; api_enum_pref_gpu_backend_items[i].id != NULL; i++) {
    const EnumPropItem *item = &api_enum_pref_gpu_backend_items[i];
#  ifndef WITH_METAL_BACKEND
    if (item->value == GPU_BACKEND_METAL) {
      continue;
    }
#  endif
#  ifndef WITH_VULKAN_BACKEND
    if (item->value == GPU_BACKEND_VULKAN) {
      continue;
    }
#  endif
    api_enum_item_add(&result, &totitem, item);
  }

  api_enum_item_end(&result, &totitem);
  *r_free = true;
  return result;
}

#else

#  define USERDEF_TAG_DIRTY_PROP_UPDATE_ENABLE \
    api_define_fallback_prop_update(0, "api_userdef_is_dirty_update")

#  define USERDEF_TAG_DIRTY_PROP_UPDATE_DISABLE api_define_fallback_prop_update(0, NULL)

/* TODO: This technically belongs to dunelib, but we don't link
 * makesapi against it. */

/* Get maximum addressable memory in megabytes, */
static size_t max_memory_in_megabytes(void)
{
  /* Maximum addressable bytes on this platform. */
  const size_t limit_bytes = (((size_t)1) << (sizeof(size_t[8]) - 1));
  /* Convert it to megabytes and return. */
  return (limit_bytes >> 20);
}

/* Same as above, but clipped to int capacity. */
static int max_memory_in_megabytes_int(void)
{
  const size_t limit_megabytes = max_memory_in_megabytes();
  /* NOTE: The result will fit into integer. */
  return (int)min_zz(limit_megabytes, (size_t)INT_MAX);
}

static void api_def_userdef_theme_ui_font_style(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(sapi, "ThemeFontStyle", NULL);
  api_def_struct_sapi(sapi, "uiFontStyle");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Font Style", "Theme settings for Font");

  prop = api_def_prop(sapi, "points", PROP_FLOAT, PROP_UNSIGNED);
  api_def_prop_range(prop, 6.0f, 32.0f);
  api_def_prop_ui_range(prop, 8.0f, 20.0f, 10.0f, 1);
  api_def_prop_ui_text(prop, "Points", "Font size in points");
  api_def_prop_update(prop, 0, "api_userdef_dpi_update");

  prop = api_def_prop(sapi, "shadow", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 0, 5);
  api_def_prop_ui_text(prop, "Shadow Size", "Shadow size (0, 3 and 5 supported)");
  api_def_prop_update(prop, 0, "api_userdef_theme_text_style_update");

  prop = api_def_prop(sapi, "shadow_offset_x", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "shadx");
  api_def_prop(prop, -10, 10);
  api_def_prop_ui_text(prop, "Shadow X Offset", "Shadow offset in pixels");
  api_def_prop_update(prop, 0, "api_userdef_theme_text_style_update");

  prop = api_def_prop(sapi, "shadow_offset_y", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "shady");
  api_def_prop_range(prop, -10, 10);
  api_def_prop_ui_text(prop, "Shadow Y Offset", "Shadow offset in pixels");
  api_def_prop_update(prop, 0, "api_userdef_theme_text_style_update");

  prop = api_def_prop(sapi, "shadow_alpha", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "shadowalpha");
  api_def_prop_range(prop, 0.0f, 1.0
  api_def_prop_ui_text(prop, "Shadow Alpha", "")
  api_def_prop_update(prop, 0, "api_userdef_theme_text_style_update");

  prop = api_def_prop(sapi, "shadow_value", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "shadowcolor");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Shadow Brightness", "Shadow color in gray value");
  api_def_prop_update(prop, 0, "api_userdef_theme_text_style_update");
}

static void api_def_userdef_theme_ui_style(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_userdef_theme_ui_font_style(dapi);

  sapi = api_def_struct(dapi, "ThemeStyle", NULL);
  api_def_struct_sapi(sapi, "uiStyle");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Style", "Theme settings for style sets");

  prop = api_def_prop(sapi, "panel_title", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_sapi(prop, NULL, "paneltitle");
  api_def_prop_struct_type(prop, "ThemeFontStyle");
  api_def_prop_ui_text(prop, "Panel Title Font", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "widget_label", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_sapi(prop, NULL, "widgetlabel");
  api_def_prop_struct_type(prop, "ThemeFontStyle");
  api_def_prop_ui_text(prop, "Widget Label Style", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "widget", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_sapi(prop, NULL, "widget");
  api_def_prop_struct_type(prop, "ThemeFontStyle");
  api_def_prop_ui_text(prop, "Widget Style", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_ui_wcol(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThemeWidgetColors", NULL);
  api_def_struct_stype(sapi, "uiWidgetColors");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Widget Color Set", "Theme settings for widget color sets");

  prop = api_def_prop(sapi, "outline", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Outline", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Inner", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Inner Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "item", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Item", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "text_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Text Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "show_shaded", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "shaded", 1);
  api_def_prop_ui_text(prop, "Shaded", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "shadetop", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, -100, 100);
  api_def_prop_ui_text(prop, "Shade Top", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "shadedown", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, -100, 100);
  api_def_prop_ui_text(prop, "Shade Down", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "roundness", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_fns(
      prop, "api_ThemeUI_roundness_get", "api_ThemeUI_roundness_set", NULL);
  api_def_prop_ui_text(prop, "Roundness", "Amount of edge rounding");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_ui_wcol_state(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThemeWidgetStateColors", NULL);
  api_def_struct_stype(sapi, "uiWidgetStateColors");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(
      sapi, "Theme Widget State Color", "Theme settings for widget state colors");

  prop = api_def_prop(sapi, "inner_anim", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Animated", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_anim_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Animated Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_key", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Keyframe", "");
  api_def_prop_update(prop, 0, "rna_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_key_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Keyframe Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_driven", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Driven", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_driven_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Driven Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_overridden", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Overridden", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_overridden_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Overridden Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_changed", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Changed", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "inner_changed_sel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Changed Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "dune", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Dune", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_ui_panel(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThemePanelColors", NULL);
  api_def_struct_sapi(sapi, "uiPanelColors");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Panel Color", "Theme settings for panel colors");

  prop = api_def_prop(sapi, "header", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_ui_text(prop, "Header", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "back", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_ui_text(prop, "Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "sub_back", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_ui_text(prop, "Sub Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static const EnumPropItem api_enum_userdef_theme_background_types_items[] = {
    {TH_BACKGROUND_SINGLE_COLOR,
     "SINGLE_COLOR",
     0,
     "Single Color",
     "Use a solid color as viewport background"},
    {TH_BACKGROUND_GRADIENT_LINEAR,
     "LINEAR",
     0,
     "Linear Gradient",
     "Use a screen space vertical linear gradient as viewport background"},
    {TH_BACKGROUND_GRADIENT_RADIAL,
     "RADIAL",
     0,
     "Vignette",
     "Use a radial gradient as viewport background"},
    {0, NULL, 0, NULL, NULL},
};

static void api_def_userdef_theme_ui_gradient(DuneApi *sapi)
{
  /* Fake struct, keep this for compatible theme presets. */
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThemeGradientColors", NULL);
  api_def_struct_sapi(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(
      sapi, "Theme Background Color", "Theme settings for background colors and gradient");

  prop = api_def_prop(sapi, "background_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_sapi(prop, NULL, "background_type");
  api_def_prop_enum_items(prop, api_enum_userdef_theme_background_types_items);
  api_def_prop_ui_text(prop, "Background Type", "Type of background in the 3D viewport");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "high_gradient", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "back");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Gradient High/Off", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "gradient", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "back_grad");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Gradient Low", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_ui(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_userdef_theme_ui_wcol(dapi);
  api_def_userdef_theme_ui_wcol_state(dapi);
  api_def_userdef_theme_ui_panel(dapi);
  api_def_userdef_theme_ui_gradient(dapi);

  sapi = api_def_struct(dapi, "ThemeUserInterface", NULL);
  api_def_struct_sapi(sapi, "ThemeUI");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(
      sapi, "Theme User Interface", "Theme settings for user interface elements");

  prop = api_def_prop(sapi, "wcol_regular", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Regular Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_tool", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Tool Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_toolbar_item", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Toolbar Item Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_radio", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Radio Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_text", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Text Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_option", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Option Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_toggle", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Toggle Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_num", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Number Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_numslider", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Slider Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_box", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Box Backdrop Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_menu", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Menu Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_pulldown", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Pulldown Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_menu_back", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Menu Backdrop Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_pie_menu", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Pie Menu Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_tooltip", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Tooltip Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_menu_item", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Menu Item Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_scroll", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Scroll Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_progress", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Progress Bar Widget Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_list_item", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "List Item Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_view_item", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Data-View Item Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_state", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "State Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wcol_tab", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Tab Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "menu_shadow_fac", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Menu Shadow Strength", "Blending factor for menu shadows");
  api_def_prop_range(prop, 0.01f, 1.0f);
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "menu_shadow_width", PROP_INT, PROP_PIXEL);
  api_def_prop_ui_text(
      prop, "Menu Shadow Width", "Width of menu shadows, set to zero to disable");
  api_def_prop_range(prop, 0.0f, 24.0f);
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "icon_alpha", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(
      prop, "Icon Alpha", "Transparency of icons in the interface, to reduce contrast");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "icon_saturation", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(prop, "Icon Saturation", "Saturation of icons in the interface");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "widget_emboss", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_sapi(prop, NULL, "widget_emboss");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(
      prop, "Widget Emboss", "Color of the 1px shadow line underlying widgets");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "editor_outline", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_sapi(prop, NULL, "editor_outline");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Editor Outline", "Color of the outline of the editors and their round corners");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "widget_text_cursor", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_sapi(prop, NULL, "widget_text_cursor");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Text Cursor", "Color of the text insertion cursor (caret)");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "panel_roundness", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_ui_text(
      prop, "Panel Roundness", "Roundness of the corners of panels and sub-panels");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_float_default(prop, 0.4f);
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* Transparent Grid */
  prop = api_def_prop(sapi, "transparent_checker_primary", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_sapi(prop, NULL, "transparent_checker_primary");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Primary Color", "Primary color of checkerboard pattern indicating transparent areas");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "transparent_checker_secondary", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "transparent_checker_secondary");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop,
                       "Secondary Color",
                       "Secondary color of checkerboard pattern indicating transparent areas");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "transparent_checker_size", PROP_INT, PROP_PIXEL);
  api_def_prop_ui_txt(
      prop, "Checkerboard Size", "Size of checkerboard pattern indicating transparent areas");
  api_def_prop_range(prop, 2, 48);
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* axis */
  prop = api_def_prop(sapi, "axis_x", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "xaxis");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "X Axis", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "axis_y", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "yaxis");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Y Axis", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update"

  prop = api_def_prop(sapi, "axis_z", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "zaxis");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Z Axis", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* Generic gizmo colors. */
  prop = api_def_prop(sapi, "gizmo_hi", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "gizmo_hi");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Gizmo Highlight", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "gizmo_primary", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "gizmo_primary");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Gizmo Primary", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "gizmo_secondary", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "gizmo_secondary");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Gizmo Secondary", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "gizmo_view_align", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "gizmo_view_align");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Gizmo View Align", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "gizmo_a", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "gizmo_a");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Gizmo A", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "gizmo_b", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "gizmo_b");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Gizmo B", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* Icon colors. */
  prop = api_def_prop(sapi, "icon_scene", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "icon_scene");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Scene", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "icon_collection", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "icon_collection");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Collection", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "icon_object", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "icon_object");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Object", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "icon_object_data", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "icon_object_data");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Object Data", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "icon_mod", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "icon_mod");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Mod", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "icon_shading", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "icon_shading");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Shading", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "icon_folder", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "icon_folder");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "File Folders", "Color of folders in the file browser");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "icon_border_intensity", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "icon_border_intensity");
  api_def_prop_ui_text(
      prop, "Icon Border", "Control the intensity of the border around themes icons");
  api_def_prop_ui_range(prop, 0.0, 1.0, 0.1, 2);
  api_def_prop_update(prop, 0, "api_userdef_theme_update_icons");
}

static void api_def_userdef_theme_space_common(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "title", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Title", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "text_hi", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Text Highlight", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* header */
  prop = api_def_prop(sapi, "header", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Header", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "header_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Header Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "header_text_hi", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Header Text Highlight", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* panel settings */
  prop = api_def_prop(sapi, "panelcolors", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "Panel Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* buttons */
  // if (!ELEM(spacetype, SPACE_PROPS, SPACE_OUTLINER)) {
  prop = api_def_prop(sapi, "btn", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Region Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "btn_title", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Region Text Titles", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "btn_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Region Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "btn_text_hi", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Region Text Highlight", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "navigation_bar", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Navigation Bar Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "execution_btns", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Execution Region Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* tabs */
  prop = api_def_prop(sapi, "tab_active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Tab Active", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "tab_inactive", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Tab Inactive", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "tab_back", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Tab Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "tab_outline", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Tab Outline", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_gradient(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThemeSpaceGradient", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_ui_text(sapi, "Theme Space Settings", "");

  /* gradient/background settings */
  prop = api_def_prop(sapi, "gradients", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "ThemeGradientColors");
  api_def_prop_ptr_fns(prop, "api_Theme_gradient_colors_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Gradient Colors", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  api_def_userdef_theme_space_common(sapi);
}

static void api_def_userdef_theme_space_generic(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(sapi, "ThemeSpaceGeneric", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_ui_text(sapi, "Theme Space Settings", "");

  prop = api_def_prop(sapi, "back", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Window Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  api_def_userdef_theme_space_common(sapi);
}

/* list / channels */
static void api_def_userdef_theme_space_list_generic(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThemeSpaceListGeneric", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_ui_text(sapi, "Theme Space List Settings", "");

  prop = api_def_prop(sapi, "list", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Source List", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "list_title", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Source List Title", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "list_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Source List Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "list_text_hi", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Source List Text Highlight", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_spaces_main(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapo, "space", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "ThemeSpaceGeneric");
  api_def_prop_ptr_fns(prop, "api_Theme_space_generic_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Theme Space", "Settings for space");
}

static void api_def_userdef_theme_spaces_gradient(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "space", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "ThemeSpaceGradient");
  api_def_prop_ptr_fns(prop, "api_Theme_space_gradient_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Theme Space", "Settings for space");
}

static void api_def_userdef_theme_spaces_list_main(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "space_list", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "ThemeSpaceListGeneric");
  api_def_prop_ptr_fns(prop, "api_Theme_space_list_generic_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Theme Space List", "Settings for space list");
}

static void api_def_userdef_theme_spaces_vertex(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "vertex", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Vertex", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "vertex_select", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Vertex Select", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "vertex_active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Active Vertex", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "vertex_size", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 32);
  api_def_prop_ui_text(prop, "Vertex Size", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "vertex_bevel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Vertex Bevel", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "vertex_unrefd", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Vertex Group Unrefd", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_spaces_edge(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "edge_select", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Edge Select", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "edge_seam", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Edge Seam", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "edge_sharp", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Edge Sharp", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "edge_crease", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Edge Crease", "");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_WINDOWMANAGER);
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "edge_bevel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  pi_def_prop_ui_text(prop, "Edge Bevel", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "edge_facesel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Edge UV Face Select", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "freestyle_edge_mark", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Freestyle Edge Mark", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_spaces_face(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "face", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Face", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "face_select", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Face Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "face_dot", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Face Dot Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "facedot_size", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 10);
  api_def_prop_ui_text(prop, "Face Dot Size", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "freestyle_face_mark", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Freestyle Face Mark", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "face_retopology", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Face Retopology", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "face_back", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Face Orientation Back", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "face_front", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Face Orientation Front", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_spaces_paint_curves(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "paint_curve_handle", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Paint Curve Handle", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "paint_curve_pivot", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Paint Curve Pivot", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_spaces_curves(
    ApiStruct *sapi, bool incl_nurbs, bool incl_lastsel, bool incl_vector, bool incl_verthandle)
{
  ApiProp *prop;

  if (incl_nurbs) {
    prop = api_def_prop(sapi, "nurb_uline", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_stype(prop, NULL, "nurb_uline");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "NURBS U Lines", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");

    prop = api_def_prop(sapi, "nurb_vline", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_stype(prop, NULL, "nurb_vline");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "NURBS V Lines", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");

    prop = api_def_prop(sapi, "nurb_sel_uline", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_stype(sapi, NULL, "nurb_sel_uline");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "NURBS Active U Lines", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");

    prop = api_def_prop(sapi, "nurb_sel_vline", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_stype(prop, NULL, "nurb_sel_vline");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "NURBS Active V Lines", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");

    prop = api_def_prop(sapi, "act_spline", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_stype(prop, NULL, "act_spline");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "Active Spline", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");
  }

  prop = api_def_prop(sapi, "handle_free", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "handle_free");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Free Handle", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "handle_auto", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "handle_auto");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Auto Handle", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  if (incl_vector) {
    prop = api_def_prop(sapi, "handle_vect", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_stype(prop, NULL, "handle_vect");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "Vector Handle", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");

    prop = api_def_prop(sapi, "handle_sel_vect", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_stype(prop, NULL, "handle_sel_vect");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "Vector Handle Selected", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");
  }

  prop = api_def_prop(sapi, "handle_align", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "handle_align");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Align Handle", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "handle_sel_free", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "handle_sel_free");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Free Handle Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "handle_sel_auto", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "handle_sel_auto");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Auto Handle Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "handle_sel_align", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "handle_sel_align");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Align Handle Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  if (!incl_nurbs) {
    /* assume that when nurbs are off, this is for 2D (i.e. anim) editors */
    prop = api_def_prop(sapi, "handle_auto_clamped", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_sapi(prop, NULL, "handle_auto_clamped");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "Auto-Clamped Handle", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");

    prop = api_def_prop(sapi, "handle_sel_auto_clamped", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_stype(prop, NULL, "handle_sel_auto_clamped");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "Auto-Clamped Handle Selected", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");
  }

  if (incl_lastsel) {
    prop = api_def_prop(sapi, "lastsel_point", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_float_stype(prop, NULL, "lastsel_point");
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "Last Selected Point", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");
  }

  if (incl_verthandle) {
    prop = api_def_prop(sapi, "handle_vertex", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "Handle Vertex", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");

    prop = api_def_prop(sapi, "handle_vertex_select", PROP_FLOAT, PROP_COLOR_GAMMA);
    api_def_prop_array(prop, 3);
    api_def_prop_ui_text(prop, "Handle Vertex Select", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");

    prop = api_def_prop(sapi, "handle_vertex_size", PROP_INT, PROP_PIXEL);
    api_def_prop_range(prop, 1, 100);
    api_def_prop_ui_text(prop, "Handle Vertex Size", "");
    api_def_prop_update(prop, 0, "api_userdef_theme_update");
  }
}

static void api_def_userdef_theme_spaces_pen(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "pen_vertex", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Pen Vertex", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "pen_vertex_select", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Pen Vertex Select", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "pen_vertex_size", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 10);
  api_def_prop_ui_text(prop, "Pen Vertex Size", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_view3d(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_view3d */
  sapi = api_def_struct(dapi, "ThemeView3D", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme 3D Viewport", "Theme settings for the 3D viewport");

  api_def_userdef_theme_spaces_gradient(sapi);

  /* General Viewport options */
  prop = api_def_prop(sapi, "grid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Grid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "clipping_border_3d", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Clipping Border", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wire", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Wire", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wire_edit", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Wire Edit", "Color for wireframe when in edit mode, but edge selection is active");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "edge_width", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 32);
  api_def_prop_ui_text(prop, "Edge Width", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* Dune Pen */
  api_def_userdef_theme_spaces_pen(sapi);

  prop = api_def_prop(sapi, "text_pen", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "time_pen_keyframe");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Pen Keyframe", "Color for indicating Pen keyframes");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* Object specific options */
  prop = api_def_prop(sapi, "object_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Object Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "object_active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "active");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Active Object", "");
  api_def_prop_update(prop, 0, "apiserdef_theme_update");

  prop = api_prop(sapi, "text_keyframe", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "time_keyframe");
  apif_prop_array(prop, 3);
  api_prop_ui_text(prop, "Object Keyframe", "Color for indicating object keyframes");
  api_prop_update(prop, 0, "api_userdef_theme_update");

  /* Object type options */
  prop = api_def_prop(sapi, "camera", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Camera", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "empty", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Empty", "");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_ID);
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "light", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "lamp");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Light", "");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_LIGHT);
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "speaker", PROP_FLOAT, PROP_COLOR_GAMMA);
  type_def_prop_array(prop, 3);
  type_def_prop_ui_text(prop, "Speaker", "");
  type__def_prop_update(prop, 0, "api_userdef_theme_update");

  /* Mesh Object specific */
  api_def_userdef_theme_spaces_vertex(sapi);
  api_def_userdef_theme_spaces_edge(sapi);
  api_def_userdef_theme_spaces_face(sapi);

  /* Mesh Object specific curves. */
  api_def_userdef_theme_spaces_curves(sapi, true, true, true, false);

  prop = api_def_prop(sapi, "extra_edge_len", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Edge Length Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "extra_edge_angle", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Edge Angle Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "extra_face_angle", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Face Angle Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "extra_face_area", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Face Area Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "editmesh_active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Active Vertex/Edge/Face", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "normal", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Face Normal", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "vertex_normal", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Vertex Normal", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "split_normal", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "loop_normal");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Split Normal", "");
  api_def_prop_update(prop, 0, "api_userdef_update");

  /* Armature Object specific. */
  prop = api_def_prop(sapi, "bone_pose", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Bone Pose", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "bone_pose_active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Bone Pose Active", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "bone_solid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Bone Solid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "bone_locked_weight", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(
      prop,
      "Bone Locked Weight",
      "Shade for bones corresponding to a locked weight group during painting");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* misc */
  prop = api_def_prop(sapi, "bundle_solid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "bundle_solid");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Bundle Solid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "camera_path", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "camera_path");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Camera Path", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "camera_passepartout", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Camera Passepartout", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "skin_root", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Skin Root", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "view_overlay", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "View Overlay", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "transform", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Transform", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "frame_current", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "cframe");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Current Frame", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  api_def_userdef_theme_spaces_paint_curves(sapi);

  prop = api_def_prop(sapi, "outline_width", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 5);
  api_def_prop_ui_text(prop, "Outline Width", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "object_origin_size", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "obcenter_dia");
  api_def_prop_range(prop, 4, 10);
  api_def_prop_ui_text(
      prop, "Object Origin Size", "Diameter in pixels for object/light origin display");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}
                          
static void api_def_userdef_theme_space_graph(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_graph */
  sapi = api_def_struct(dapi, "ThemeGraphEditor", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Graph Editor", "Theme settings for the graph editor");

  api_def_userdef_theme_spaces_main(sapi);
  api_def_userdef_theme_spaces_list_main(sapi);

  prop = api_def_prop(sapi, "grid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Grid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "frame_current", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "cframe");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Current Frame", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_scrub_background", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Scrubbing/Markers Region", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "window_sliders", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "shade1");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Window Sliders", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "channels_region", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "shade2");
  api_def_prop_array(prop, 3);
  api_def_prope_ui_text(prop, "Channels Region", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "dopesheet_channel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "ds_channel");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Dope Sheet Channel", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "dopesheet_subchannel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "ds_subchannel");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Dope Sheet Sub-channel", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "channel_group", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "group");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Channel Group", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active_channels_group", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "group_active");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Active Channel Group", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_range", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "anim_preview_range");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Preview Range", "Color of preview range overlay");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  api_def_userdef_theme_spaces_vertex(sapi);
  api_def_userdef_theme_spaces_curves(sapi, false, true, true, true);
}

static void api_def_userdef_theme_space_file(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_file */
  sapi = api_def_struct(dapi, "ThemeFileBrowser", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme File Browser", "Theme settings for the File Browser");

  api_def_userdef_theme_spaces_main(sapi);

  prop = api_def_prop(sapi, "selected_file", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "hilite");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Selected File", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "row_alternate", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Alternate Rows", "Overlay color on every other row");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_outliner(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_outliner */
  sapi = api_def_struct(dapi, "ThemeOutliner", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Outliner", "Theme settings for the Outliner");

  api_def_userdef_theme_spaces_main(sapi);

  prop = api_def_prop(sapi, "match", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Filter Match", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "selected_highlight", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Selected Highlight", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Active Highlight", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "selected_object", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Selected Objects", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active_object", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Active Object", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "edited_object", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Edited Object", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "row_alternate", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Alternate Rows", "Overlay color on every other row");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_userpref(DuneApi *dapi)
{
  ApiStruct *sapi;

  /* space_userpref */
  sapi = api_def_struct(dapi, "ThemePrefs", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Prefs", "Theme settings for the Dune Prefs");

  api_def_userdef_theme_spaces_main(sapi);
}

static void api_def_userdef_theme_space_console(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_console */
  sapi = api_def_struct(dapi, "ThemeConsole", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Console", "Theme settings for the Console");

  api_def_userdef_theme_spaces_main(srna);

  prop = api_def_prop(sapi, "line_output", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "console_output");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Line Output", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "line_input", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "console_input");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Line Input", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "line_info", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "console_info");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Line Info", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "line_error", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "console_error");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Line Error", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "cursor", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "console_cursor");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Cursor", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "select", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "console_select");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Selection", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_info(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_info */
  sapi = api_def_struct(dapi, "ThemeInfo", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Info", "Theme settings for Info");

  api_def_userdef_theme_spaces_main(sapi);

  prop = api_def_prop(sapi, "info_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Selected Line Background", "Background color of selected line");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_selected_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Selected Line Text Color", "Text color of selected line");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_error", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Error Icon Background", "Background color of Error icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_error_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Error Icon Foreground", "Foreground color of Error icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_warning", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Warning Icon Background", "Background color of Warning icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_warning_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Warning Icon Foreground", "Foreground color of Warning icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_info", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Info Icon Background", "Background color of Info icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_info_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Info Icon Foreground", "Foreground color of Info icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_debug", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Debug Icon Background", "Background color of Debug icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_debug_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Debug Icon Foreground", "Foreground color of Debug icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_prop", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Prop Icon Background", "Background color of Prop icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_prop_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Prop Icon Foreground", "Foreground color of Prop icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_op", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Op Icon Background", "Background color of Operator icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "info_op_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Op Icon Foreground", "Foreground color of Operator icon");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_text(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_text */
  sapi = api_def_struct(dapi, "ThemeTextEditor", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Text Editor", "Theme settings for the Text Editor");

  api_def_userdef_theme_spaces_main(sapi);

  prop = api_def_prop(sapi, "line_numbers", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "line_numbers");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Line Numbers", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "line_numbers_background", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "grid");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Line Numbers Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  /* no longer used */
#  if 0
  prop = api_def_prop(sapi, "scroll_bar", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "shade1");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Scroll Bar", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
#  endif

  prop = api_def_prop(sapi, "selected_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "shade2");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Selected Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "cursor", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "hilite");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Cursor", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "syntax_builtin", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxb");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Syntax Built-In", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "syntax_symbols", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxs");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Syntax Symbols", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "syntax_special", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxv");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Syntax Special", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "syntax_preprocessor", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxd");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Syntax Preprocessor", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "syntax_reserved", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxr");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Syntax Reserved", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "syntax_comment", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxc");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Syntax Comment", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "syntax_string", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxl");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Syntax String", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "syntax_numbers", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxn");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Syntax Numbers", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_node(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_node */
  sapi = api_def_struct(dapi, "ThemeNodeEditor", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Node Editor", "Theme settings for the Node Editor");

  api_def_userdef_theme_spaces_main(sapi);
  api_def_userdef_theme_spaces_list_main(sapi);

  prop = api_def_prop(sapi, "grid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Grid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "node_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Node Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "node_active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "active");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Active Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wire", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "wire");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Wires", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wire_inner", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxr");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Wire Color", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wire_select", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "edge_select");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Wire Select", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "selected_text", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "shade2");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Selected Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "node_backdrop", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxl");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Node Backdrop", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "converter_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxv");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Converter Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "color_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxb");
  api_def_prope_array(prop, 3);
  api_def_prop_ui_text(prop, "Color Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "group_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxc");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Group Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "group_socket_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "console_output");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Group Socket Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "frame_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "movie");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Frame Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "matte_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxs");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Matte Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "distor_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxd");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Distort Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "noodle_curving", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "noodle_curving");
  api_def_prop_int_default(prop, 5);
  api_def_prop_range(prop, 0, 10);
  api_def_prop_ui_text(prop, "Noodle Curving", "Curving of the noodle");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "grid_levels", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "grid_levels");
  api_def_prop_int_default(prop, 3);
  api_def_prop_range(prop, 0, 3);
  api_def_prop_ui_text(
      prop, "Grid Levels", "Number of subdivisions for the dot grid displayed in the background");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "dash_alpha", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_ui_text(prop, "Dashed Lines Opacity", "Opacity for the dashed lines in wires");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "input_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "syntaxn");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Input Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "output_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_output");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Output Node", "");
  api_def_prop_update(prop, 0, "apo_userdef_theme_update");

  prop = api_def_prop(sapi, "filter_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_filter");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Filter Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "vector_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_vector");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Vector Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "texture_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_texture");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Texture Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "shader_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_shader");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Shader Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "script_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_script");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Script Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "pattern_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_pattern");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Pattern Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "layout_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_layout");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Layout Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "geometry_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_geometry");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Geometry Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "attribute_node", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nodeclass_attribute");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Attribute Node", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "simulation_zone", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "node_zone_simulation");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Simulation Zone", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_btns(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_buts */
  sapi = api_def_struct(dapi, "ThemeProps", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Props", "Theme settings for the Properties");

  prop = api_def_prop(sapi, "match", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Search Match", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active_mod", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "active");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Active Mod Outline", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  api_def_userdef_theme_spaces_main(sapi);
}

static void api_def_userdef_theme_space_image(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_image */
  sapi = api_def_struct(dapi, "ThemeImageEditor", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Image Editor", "Theme settings for the Image Editor");

  api_def_userdef_theme_spaces_main(sapi);

  prop = api_def_prop(sapi, "grid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Grid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  api_def_userdef_theme_spaces_vertex(sapi);
  api_def_userdef_theme_spaces_face(sapi);

  prop = api_def_prop(sapi, "editmesh_active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Active Vertex/Edge/Face", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "wire_edit", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Wire Edit", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "edge_width", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 32);
  api_def_prop_ui_text(prop, "Edge Width", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "edge_select", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop(prop, 3);
  api_def_prop_ui_text(prop, "Edge Select", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "scope_back", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "preview_back");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Scope Region Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_stitch_face", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "preview_stitch_face");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Stitch Preview Face", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_stitch_edge", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "preview_stitch_edge");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Stitch Preview Edge", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_stitch_vert", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "preview_stitch_vert");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Stitch Preview Vertex", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_stitch_stitchable", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "preview_stitch_stitchable");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Stitch Preview Stitchable", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_stitch_unstitchable", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "preview_stitch_unstitchable");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Stitch Preview Unstitchable", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_stitch_active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "preview_stitch_active");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Stitch Preview Active Island", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "uv_shadow", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "uv_shadow");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Texture Paint/Modifier UVs", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "frame_current", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "cframe");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Current Frame", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "metadatabg", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "metadatabg");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Metadata Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "metadatatext", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "metadatatext");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Metadata Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  api_def_userdef_theme_spaces_curves(sapi, false, false, false, true);

  api_def_userdef_theme_spaces_paint_curves(sapi);
}

static void api_def_userdef_theme_space_seq(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_seq */
  sapi = api_def_struct(dapi, "ThemeSeqEditor", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Seq Editor", "Theme settings for the Sequence Editor");

  api_def_userdef_theme_spaces_main(sapi);
  api_def_userdef_theme_spaces_list_main(sapi);

  prop = api_def_prop(sapi, "grid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Grid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "window_sliders", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "shade1");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Window Sliders", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "movie_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "movie");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Movie Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "movieclip_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "movieclip");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Clip Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "image_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "image");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Image Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "scene_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "scene");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Scene Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "audio_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "audio");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Audio Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "effect_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "effect");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Effect Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "color_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Color Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "meta_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "meta");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Meta Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "mask_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "mask");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Mask Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "text_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Text Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Active Strip", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "selected_strip", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Selected Strips", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "frame_current", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "cframe");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Current Frame", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_scrub_background", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Scrubbing/Markers Region", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "vertex_select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Keyframe", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "draw_action", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "bone_pose");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Draw Action", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_back", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "preview_back");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Preview Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "metadatabg", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "metadatabg");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Metadata Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "metadatatext", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "metadatatext");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Metadata Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_range", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "anim_preview_range");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Preview Range", "Color of preview range overlay");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "row_alternate", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Alternate Rows", "Overlay color on every other row");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_action(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_action */
  sapi = api_def_struct(dapi, "ThemeDopeSheet", NULL);
  api_def_struct_style(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Dope Sheet", "Theme settings for the Dope Sheet");

  api_def_userdef_theme_spaces_main(sapi);
  api_def_userdef_theme_spaces_list_main(sapi);

  prop = api_def_prop(sapi, "grid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Grid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapo, "frame_current", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "cframe");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Current Frame", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_scrub_background", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Scrubbing/Markers Region", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "value_sliders", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "face");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Value Sliders", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "view_sliders", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "shade1");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "View Sliders", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "dopesheet_channel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "ds_channel");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Dope Sheet Channel", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "dopesheet_subchannel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "ds_subchannel");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Dope Sheet Sub-channel", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "channels", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "shade2");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Channels", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "channels_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "hilite");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Channels Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "channel_group", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "group");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Channel Group", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active_channels_group", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "group_active");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Active Channel Group", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "long_key", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "strip");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Long Key", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "long_key_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "strip_select");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Long Key Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_keyframe");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Keyframe", "Color of Keyframe");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_keyframe_select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Keyframe Selected", "Color of selected keyframe");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_extreme", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_extreme");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Extreme Keyframe", "Color of extreme keyframe");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_extreme_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_extreme_select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Extreme Keyframe Selected", "Color of selected extreme keyframe");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_breakdown", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_breakdown");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Breakdown Keyframe", "Color of breakdown keyframe");
  api_def_prop_update(prop, 0, "rna_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_breakdown_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_breakdown_select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Breakdown Keyframe Selected", "Color of selected breakdown keyframe");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_jitter", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_jitter");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Jitter Keyframe", "Color of jitter keyframe");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_jitter_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_jitter_select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Jitter Keyframe Selected", "Color of selected jitter keyframe");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_movehold", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_movehold");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Moving Hold Keyframe", "Color of moving hold keyframe");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_movehold_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keytype_movehold_select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Moving Hold Keyframe Selected", "Color of selected moving hold keyframe");
  api_def_prop_update(prop, 0, "rna_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_border", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keyborder");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Keyframe Border", "Color of keyframe border");
  api_def_prop_update(prop, 0, "rna_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_border_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keyborder_select");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Keyframe Border Selected", "Color of selected keyframe border");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_scale_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "keyframe_scale_fac");
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_ui_text(
      prop, "Keyframe Scale Factor", "Scale factor for adjusting the height of keyframes");
  /* NOTE: These limits prevent buttons overlapping (min), and excessive size... (max). */
  api_def_prop_range(prop, 0.8f, 5.0f);
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_DOPESHEET, "rna_userdef_theme_update");

  prop = api_def_prop(sapi, "summary", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "anim_active");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Summary", "Color of summary channel");
  api_def_prop_update(prop, 0, "rna_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_range", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "anim_preview_range");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Preview Range", "Color of preview range overlay");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "interpolation_line", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "ds_ipoline");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(
      prop, "Interpolation Line", "Color of lines showing non-bezier interpolation modes");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "simulated_frames", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "simulated_frames");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Simulated Frames", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_nla(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_nla */
  sapi = api_def_struct(dapi, "ThemeNLAEditor", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Nonlinear Animation", "Theme settings for the NLA Editor");

  api_def_userdef_theme_spaces_main(sapi);
  api_def_userdef_theme_spaces_list_main(sapi);

  prop = api_def_prop(sapi, "grid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Grid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "view_sliders", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "shade1");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "View Sliders", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "dopesheet_channel", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "ds_channel");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Channel", "Nonlinear Animation Channel");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "nla_track", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nla_track");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Track", "Nonlinear Animation Track");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active_action", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "anim_active");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Active Action", "Animation data-block has active action");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active_action_unset", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "anim_non_active");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(
      prop, "No Active Action", "Animation data-block doesn't have active action");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "preview_range", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "anim_preview_range");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Preview Range", "Color of preview range overlay");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "strips", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "strip");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Strips", "Unselected Action-Clip Strip");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "strips_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "strip_select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Strips Selected", "Selected Action-Clip Strip");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "transition_strips", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nla_transition");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Transitions", "Unselected Transition Strip");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "transition_strips_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nla_transition_sel");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Transitions Selected", "Selected Transition Strip");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "meta_strips", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nla_meta");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Meta Strips", "Unselected Meta Strip (for grouping related strips)");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "meta_strips_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nla_meta_sel");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Meta Strips Selected", "Selected Meta Strip (for grouping related strips)");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "sound_strips", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nla_sound");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Sound Strips", "Unselected Sound Strip (for timing speaker sounds)");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "sound_strips_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nla_sound_sel");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Sound Strips Selected", "Selected Sound Strip (for timing speaker sounds)");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "tweak", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nla_tweaking");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Tweak", "Color for strip/action being \"tweaked\" or edited");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "tweak_duplicate", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "nla_tweakdupli");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop,
      "Tweak Duplicate Flag",
      "Warning/error indicator color for strips referencing the strip being tweaked");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_border", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keyborder");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Keyframe Border", "Color of keyframe border");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "keyframe_border_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "keyborder_select");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Keyframe Border Selected", "Color of selected keyframe border");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "frame_current", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "cframe");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Current Frame", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_scrub_background", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Scrubbing/Markers Region", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_colorset(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThemeBoneColorSet", NULL);
  api_def_struct_stype(sapi, "ThemeWireColor");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Bone Color Set", "Theme settings for bone color sets");

  prop = api_def_prop(sapi, "normal", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "solid");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Regular", "Color used for the surface of bones");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "select", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Select", "Color used for selected bones");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Active", "Color used for active bones");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "show_colored_constraints", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TH_WIRECOLOR_CONSTCOLS);
  api_def_prop_ui_text(
      prop, "Colored Constraints", "Allow the use of colors indicating constraints/keyed status");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_collection_color(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThemeCollectionColor", NULL);
  api_def_struct_stype(sapi, "ThemeCollectionColor");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Collection Color", "Theme settings for collection colors");

  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "color");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Color", "Collection Color Tag");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_strip_color(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ThemeStripColor", NULL);
  api_def_struct_stype(sapi, "ThemeStripColor");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Strip Color", "Theme settings for strip colors");

  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "color");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Color", "Strip Color");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");
}

static void api_def_userdef_theme_space_clip(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_clip */
  sapi = api_def_struct(dapi, "ThemeClipEditor", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Clip Editor", "Theme settings for the Movie Clip Editor");

  api_def_userdef_theme_spaces_main(sapi);
  api_def_userdef_theme_spaces_list_main(sapi);

  prop = api_def_prop(sapi, "grid", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Grid", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "marker_outline", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "marker_outline");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Marker Outline", "Color of marker's outline");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "marker", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "marker");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Marker", "Color of marker");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "active_marker", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "act_marker");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Active Marker", "Color of active marker");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "selected_marker", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "sel_marker");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Selected Marker", "Color of selected marker");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "disabled_marker", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "dis_marker");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Disabled Marker", "Color of disabled marker");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "locked_marker", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "lock_marker");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Locked Marker", "Color of locked marker");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "path_before", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "path_before");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Path Before", "Color of path before current frame");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "path_after", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "path_after");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Path After", "Color of path after current frame");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "path_keyframe_before", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Path Before", "Color of path before current frame");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "path_keyframe_after", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Path After", "Color of path after current frame");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "frame_current", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "cframe");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Current Frame", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_scrub_background", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Scrubbing/Markers Region", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "time_marker_line_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Marker Line Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "strips", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "strip");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Strips", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "strips_selected", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "strip_select");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Strips Selected", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "metadatabg", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "metadatabg");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Metadata Background", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  prop = api_def_prop(sapi, "metadatatext", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "metadatatext");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Metadata Text", "");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  api_def_userdef_theme_spaces_curves(sapi, false, false, false, true);
}

static void api_def_userdef_theme_space_topbar(DuneApi *dapi)
{
  ApiStruct *sapi;

  /* space_topbar */
  sapi = api_def_struct(dapi, "ThemeTopBar", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Top Bar", "Theme settings for the Top Bar");

  api_def_userdef_theme_spaces_main(sapi);
}

static void api_def_userdef_theme_space_statusbar(DuneApi *dapi)
{
  ApiStruct *sapi;

  /* space_statusbar */
  sapi = api_def_struct(dapi, "ThemeStatusBar", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Status Bar", "Theme settings for the Status Bar");

  api_def_userdef_theme_spaces_main(sapi);
}

static void api_def_userdef_theme_space_spreadsheet(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* space_spreadsheet */
  sapi = api_def_struct(dapi, "ThemeSpreadsheet", NULL);
  api_def_struct_stype(sapi, "ThemeSpace");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme Spreadsheet", "Theme settings for the Spreadsheet");

  prop = api_def_prop(sapi, "row_alternate", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Alternate Rows", "Overlay color on every other row");
  api_def_prop_update(prop, 0, "api_userdef_theme_update");

  api_def_userdef_theme_spaces_main(sapi);
  api_def_userdef_theme_spaces_list_main(sapi);
}

static void api_def_userdef_themes(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem active_theme_area[] = {
      {0, "USER_INTERFACE", ICON_WORKSPACE, "User Interface", ""},
      {19, "STYLE", ICON_FONTPREVIEW, "Text Style", ""},
      {18, "BONE_COLOR_SETS", ICON_COLOR, "Bone Color Sets", ""},
      {1, "VIEW_3D", ICON_VIEW3D, "3D Viewport", ""},
      {3, "GRAPH_EDITOR", ICON_GRAPH, "Graph Editor", ""},
      {4, "DOPESHEET_EDITOR", ICON_ACTION, "Dope Sheet", ""},
      {5, "NLA_EDITOR", ICON_NLA, "Nonlinear Animation", ""},
      {6, "IMAGE_EDITOR", ICON_IMAGE, "UV/Image Editor", ""},
      {7, "SEQUENCE_EDITOR", ICON_SEQUENCE, "Video Sequencer", ""},
      {8, "TEXT_EDITOR", ICON_TEXT, "Text Editor", ""},
      {9, "NODE_EDITOR", ICON_NODETREE, "Node Editor", ""},
      {11, "PROPERTIES", ICON_PROPERTIES, "Properties", ""},
      {12, "OUTLINER", ICON_OUTLINER, "Outliner", ""},
      {14, "PREFERENCES", ICON_PREFERENCES, "Preferences", ""},
      {15, "INFO", ICON_INFO, "Info", ""},
      {16, "FILE_BROWSER", ICON_FILEBROWSER, "File Browser", ""},
      {17, "CONSOLE", ICON_CONSOLE, "Python Console", ""},
      {20, "CLIP_EDITOR", ICON_TRACKER, "Movie Clip Editor", ""},
      {21, "TOPBAR", ICON_TOPBAR, "Top Bar", ""},
      {22, "STATUSBAR", ICON_STATUSBAR, "Status Bar", ""},
      {23, "SPREADSHEET", ICON_SPREADSHEET, "Spreadsheet"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "Theme", NULL);
  api_def_struct_stype(sapi, "bTheme");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Theme", "User interface styling and color settings");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Name of the theme");
  api_def_struct_name_prop(sapi, prop);
  /* XXX: for now putting this in presets is silly - its just Default */
  api_def_prop_flag(prop, PROP_SKIP_SAVE);

  prop = api_def_prop(sapi, "theme_area", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "active_theme_area");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
  api_def_prop_enum_items(prop, active_theme_area);
  api_def_prop_ui_text(prop, "Active Theme Area", "");

  prop = api_def_prop(sapi, "user_interface", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "tui");
  api_def_prop_struct_type(prop, "ThemeUserInterface");
  api_def_prop_ui_text(prop, "User Interface", "");

  /* Space Types */
  prop = api_def_prop(sapi, "view_3d", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_view3d");
  api_def_prop_struct_type(prop, "ThemeView3D");
  api_def_prop_ui_text(prop, "3D Viewport", "");

  prop = api_def_prop(sapi, "graph_editor", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_graph");
  api_def_prop_struct_type(prop, "ThemeGraphEditor");
  api_def_prop_ui_text(prop, "Graph Editor", "");

  prop = api_def_prop(sapi, "file_browser", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_file");
  api_def_prop_struct_type(prop, "ThemeFileBrowser");
  api_def_prop_ui_text(prop, "File Browser", "");

  prop = api_def_prop(sapi, "nla_editor", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_nla");
  api_def_prop_struct_type(prop, "ThemeNLAEditor");
  api_def_prop_ui_text(prop, "Nonlinear Animation", "");

  prop = api_def_prop(sapi, "dopesheet_editor", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_action");
  api_def_prop_struct_type(prop, "ThemeDopeSheet");
  api_def_prop_ui_text(prop, "Dope Sheet", "");

  prop = api_def_prop(sapi, "image_editor", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_image");
  api_def_prop_struct_type(prop, "ThemeImageEditor");
  api_def_prop_ui_text(prop, "Image Editor", "");

  prop = api_def_prop(sapi, "seq_editor", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_seq");
  api_def_prop_struct_type(prop, "ThemeSeqEditor");
  api_def_prop_ui_text(prop, "Seq Editor", "");

  prop = api_def_prop(sapi, "props", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_props");
  api_def_prop_struct_type(prop, "ThemeProps");
  api_def_prop_ui_text(prop, "Props", "");

  prop = api_def_prop(sapi, "text_editor", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_text");
  api_def_prop_struct_type(prop, "ThemeTextEditor");
  api_def_prop_ui_text(prop, "Text Editor", "");

  prop = api_def_prop(sapi, "node_editor", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_node");
  api_def_prop_struct_type(prop, "ThemeNodeEditor");
  api_def_prop_ui_text(prop, "Node Editor", "");

  prop = api_def_prop(sapi, "outliner", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_outliner");
  api_def_prop_struct_type(prop, "ThemeOutliner");
  api_def_prop_ui_text(prop, "Outliner", "");

  prop = api_def_prop(sapi, "info", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_info");
  api_def_prop_struct_type(prop, "ThemeInfo");
  api_def_prop_ui_text(prop, "Info", "");

  prop = api_def_prop(sapi, "prefs", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_prefs");
  api_def_prop_struct_type(prop, "ThemePrefs");
  api_def_prop_ui_text(prop, "Prefs", "");

  prop = api_def_prop(sapi, "console", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_console");
  api_def_prop_struct_type(prop, "ThemeConsole");
  api_def_prop_ui_text(prop, "Console", "");

  prop = api_def_prop(sapi, "clip_editor", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_clip");
  api_def_prop_struct_type(prop, "ThemeClipEditor");
  api_def_prop_ui_text(prop, "Clip Editor", "");

  prop = api_def_prop(sapi, "topbar", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_topbar");
  api_def_prop_struct_type(prop, "ThemeTopBar");
  api_def_prop_ui_text(prop, "Top Bar", "");

  prop = api_def_prop(sapi, "statusbar", PROP_POTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_statusbar");
  api_def_prop_struct_type(prop, "ThemeStatusBar");
  api_def_prop_ui_text(prop, "Status Bar", "");

  prop = api_def_prop(sapi, "spreadsheet", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "space_spreadsheet");
  api_def_prop_struct_type(prop, "ThemeSpreadsheet");
  api_def_prop_ui_text(prop, "Spreadsheet", "");
  /* end space types */

  prop = api_def_prop(sapi, "bone_color_sets", PROP_COLLECTION, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_collection_stype(prop, NULL, "tarm", "");
  api_def_prop_struct_type(prop, "ThemeBoneColorSet");
  api_def_prop_ui_text(prop, "Bone Color Sets", "");

  prop = api_def_prop(sapi, "collection_color", PROP_COLLECTION, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_collection_stype(prop, NULL, "collection_color", "");
  api_def_prop_struct_type(prop, "ThemeCollectionColor");
  api_def_prop_ui_text(prop, "Collection Color", "");

  prop = api_def_prop(sapi, "strip_color", PROP_COLLECTION, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_collection_stype(prop, NULL, "strip_color", "");
  api_def_prop_struct_type(prop, "ThemeStripColor");
  api_def_prop_ui_text(prop, "Strip Color", "");
}

static void api_def_userdef_addon(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Addon", NULL);
  api_def_struct_stype(sapi, "bAddon");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Add-on", "Python add-ons to be loaded automatically");

  prop = api_def_prop(sapi, "module", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Module", "Module name");
  api_def_struct_name_prop(sapi, prop);

  /* Collection active prop */
  prop = api_def_prop(sapi, "prefs", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "AddonPrefs");
  api_def_prop_ptr_fns(prop, "api_Addon_prefs_get", NULL, NULL, NULL);
}

static void api_def_userdef_studiolights(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "StudioLights", NULL);
  api_def_struct_stype(sapi, "UserDef");
  api_def_struct_ui_text(sapi, "Studio Lights", "Collection of studio lights");

  fn = api_def_fn(sapi, "load", "api_StudioLights_load");
  api_def_fn_ui_description(fn, "Load studiolight from file");
  parm = api_def_string(
      fn, "path", NULL, 0, "File Path", "File path where the studio light file can be found");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn,
                      "type",
                      api_enum_studio_light_type_items,
                      STUDIOLIGHT_TYPE_WORLD,
                      "Type",
                      "The type for the new studio light");
  api_def_prop_lang_cxt(parm, LANG_CXT_ID_LIGHT);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "studio_light", "StudioLight", "", "Newly created StudioLight");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new", "api_StudioLights_new");
  api_def_fn_ui_description(fn, "Create studiolight from default lighting");
  parm = api_def_string(
      fn,
      "path",
      NULL,
      0,
      "Path",
      "Path to the file that will contain the lighting info (without extension)");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "studio_light", "StudioLight", "", "Newly created StudioLight");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "apu_StudioLights_remove");
  api_def_fn_ui_description(fn, "Remove a studio light");
  parm = api_def_ptr(fn, "studio_light", "StudioLight", "", "The studio light to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  fn = api_def_fn(sapi, "refresh", "api_StudioLights_refresh");
  api_def_fn_ui_description(fn, "Refresh Studio Lights from disk");
}

static void api_def_userdef_studiolight(BlenderRNA *brna)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_define_verify_stype(false);
  sapi = api_def_struct(dapi, "StudioLight", NULL);
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Studio Light", "Studio light");

  prop = api_def_prop(sapi, "index", PROP_INT, PROP_NONE);
  api_def_prop_int_fns(prop, "api_UserDef_studiolight_index_get", NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Index", "");

  prop = api_def_prop(sapi, "is_user_defined", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_UserDef_studiolight_is_user_defined_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "User Defined", "");

  prop = api_def_prop(sapi, "has_specular_highlight_pass", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(
      prop, "api_UserDef_studiolight_has_specular_highlight_pass_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop,
      "Has Specular Highlight",
      "Studio light image file has separate \"diffuse\" and \"specular\" passes");

  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_studio_light_type_items);
  api_def_prop_enum_fns(prop, "api_UserDef_studiolight_type_get", NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Type", "");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_LIGHT);

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(
      prop, "api_UserDef_studiolight_name_get", "api_UserDef_studiolight_name_length", NULL);
  api_def_prop_ui_text(prop, "Name", "");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "path", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_fns(
      prop, "api_UserDef_studiolight_path_get", "api_UserDef_studiolight_path_length", NULL);
  api_def_prop_ui_text(prop, "Path", "");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "solid_lights", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "light_param", "");
  api_def_prop_struct_type(prop, "UserSolidLight");
  api_def_prop_collection_fns(prop,
                              "api_UserDef_studiolight_solid_lights_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_UserDef_studiolight_solid_lights_length",
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(
      prop, "Solid Lights", "Lights user to display objects in solid draw mode");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "light_ambient", PROP_FLOAT, PROP_COLOR);
  api_def_prop_array(prop, 3);
  api_def_prop_float_fns(prop, "api_UserDef_studiolight_light_ambient_get", NULL, NULL);
  api_def_prop_ui_text(
      prop, "Ambient Color", "Color of the ambient light that uniformly lit the scene");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "path_irr_cache", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_fns(prop,
                          "api_UserDef_studiolight_path_irr_cache_get",
                          "api_UserDef_studiolight_path_irr_cache_length",
                          NULL);
  api_def_prop_ui_text(
      prop, "Irradiance Cache Path", "Path where the irradiance cache is stored");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "path_sh_cache", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_fns(prop,
                          "api_UserDef_studiolight_path_sh_cache_get",
                          "api_UserDef_studiolight_path_sh_cache_length",
                          NULL);
  api_def_prop_ui_text(
      prop, "SH Cache Path", "Path where the spherical harmonics cache is stored");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  const int spherical_harmonics_dim[] = {STUDIOLIGHT_SH_EFFECTIVE_COEFS_LEN, 3};
  prop = api_def_prop(sapi, "spherical_harmonics_coefficients", PROP_FLOAT, PROP_COLOR);
  api_def_prop_multi_array(prop, 2, spherical_harmonics_dim);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_float_fns(
      prop, "api_UserDef_studiolight_spherical_harmonics_coefficients_get", NULL, NULL);

  api_define_verify_stype(true);
}

static void api_def_userdef_pathcompare(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  srna = api_def_struct(dapi, "PathCompare", NULL);
  api_def_struct_stype(sapi, "bPathCompare");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Path Compare", "Match paths against this value");

  prop = api_def_prop(sapi, "path", PROP_STRING, PROP_DIRPATH);
  api_def_prop_ui_text(prop, "Path", "");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "use_glob", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_PATHCMP_GLOB);
  api_def_prop_ui_text(prop, "Use Wildcard", "Enable wildcard globbing");
}

static void api_def_userdef_addon_pref(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "AddonPrefs", NULL);
  api_def_struct_ui_text(sapi, "Add-on Prefs", "");
  api_def_struct_stype(sapi, "bAddon"); /* WARNING: only a bAddon during registration */

  api_def_struct_refine_fn(sapi, "api_AddonPref_refine");
  api_def_struct_register_fns(sapi, "api_AddonPref_register", "api_AddonPref_unregister", NULL);
  api_def_struct_idprops_fn(sapi, "api_AddonPref_idprops");
  api_def_struct_flag(sapi, STRUCT_NO_DATABLOCK_IDPROPS); /* Mandatory! */

  USERDEF_TAG_DIRTY_PROP_UPDATE_DISABLE;

  /* registration */
  api_define_verify_stype(0);
  prop = api_def_prop(sapi, "bl_idname", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "module");
  api_def_prop_flag(prop, PROP_REGISTER);
  api_define_verify_stype(1);

  USERDEF_TAG_DIRTY_PROP_UPDATE_ENABLE;
}

static void api_def_userdef_dothemes(DuneApi *dapi)
{

  api_def_userdef_theme_ui_style(dapi);
  api_def_userdef_theme_ui(dapi);

  api_def_userdef_theme_space_generic(dapi);
  api_def_userdef_theme_space_gradient(dapi);
  api_def_userdef_theme_space_list_generic(dapi);

  api_def_userdef_theme_space_view3d(dapi);
  api_def_userdef_theme_space_graph(dapi);
  api_def_userdef_theme_space_file(dapi);
  api_def_userdef_theme_space_nla(dapi);
  api_def_userdef_theme_space_action(dapi);
  api_def_userdef_theme_space_image(dapi);
  api_def_userdef_theme_space_seq(dapi);
  api_def_userdef_theme_space_buts(dapi);
  api_def_userdef_theme_space_text(dapi);
  api_def_userdef_theme_space_node(dapi);
  api_def_userdef_theme_space_outliner(dapi);
  api_def_userdef_theme_space_info(dapi);
  api_def_userdef_theme_space_userpref(dapi);
  api_def_userdef_theme_space_console(dapi);
  api_def_userdef_theme_space_clip(dapi);
  api_def_userdef_theme_space_topbar(dapi);
  api_def_userdef_theme_space_statusbar(dapi);
  api_def_userdef_theme_space_spreadsheet(dapi);
  api_def_userdef_theme_colorset(dapi);
  api_def_userdef_theme_collection_color(dapi);
  api_def_userdef_theme_strip_color(dapi);
  api_def_userdef_themes(dapi);
}

static void api_def_userdef_solidlight(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;
  static float default_dir[3] = {0.0f, 0.0f, 1.0f};
  static float default_col[3] = {0.8f, 0.8f, 0.8f};

  sapi = api_def_struct(dapi, "UserSolidLight", NULL);
  api_def_struct_stype(sapi, "SolidLight");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(
      sapi, "Solid Light", "Light used for Studio lighting in solid shading mode");

  prop = api_def_prop(sapi, "use", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", 1);
  api_def_prop_bool_default(prop, true);
  api_def_prop_ui_text(prop, "Enabled", "Enable this light in solid shading mode");
  api_def_prop_update(prop, 0, "api_UserDef_viewport_lights_update");

  prop = api_def_prop(sapi, "smooth", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "smooth");
  api_def_prop_float_default(prop, 0.5f);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Smooth", "Smooth the lighting from this light");
  api_def_prop_update(prop, 0, "api_UserDef_viewport_lights_update");

  prop = api_def_prop(sapi, "direction", PROP_FLOAT, PROP_DIRECTION);
  api_def_prop_float_stype(prop, NULL, "vec");
  api_def_prop_array(prop, 3);
  api_def_prop_float_array_default(prop, default_dir);
  api_def_prop_ui_text(prop, "Direction", "Direction that the light is shining");
  api_def_prop_update(prop, 0, "api_UserDef_viewport_lights_update");

  prop = api_def_prop(sapi, "specular_color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "spec");
  api_def_prop_array(prop, 3);
  api_def_prop_float_array_default(prop, default_col);
  api_def_prop_ui_text(prop, "Specular Color", "Color of the light's specular highlight");
  api_def_prop_update(prop, 0, "api_UserDef_viewport_lights_update");

  prop = api_def_prop(sapi, "diffuse_color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "col");
  api_def_prop_array(prop, 3);
  api_def_prop_float_array_default(prop, default_col);
  api_def_prop_ui_text(prop, "Diffuse Color", "Color of the light's diffuse highlight");
  api_def_prop_update(prop, 0, "api_UserDef_viewport_lights_update");
}

static void api_def_userdef_walk_navigation(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "WalkNavigation", NULL);
  api_def_struct_stype(sapi, "WalkNavigation");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Walk Navigation", "Walk navigation settings");

  prop = api_def_prop(sapi, "mouse_speed", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.01f, 10.0f);
  api_def_prop_ui_text(
      prop,
      "Mouse Sensitivity",
      "Speed factor for when looking around, high values mean faster mouse movement");

  prop = api_def_prop(sapi, "walk_speed", PROP_FLOAT, PROP_VELOCITY);
  api_def_prop_range(prop, 0.01f, 100.0f);
  api_def_prop_ui_text(prop, "Walk Speed", "Base speed for walking and flying");

  prop = api_def_prop(sapi, "walk_speed_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.01f, 10.0f);
  api_def_prop_ui_text(
      prop, "Speed Factor", "Multiplication factor when using the fast or slow modifiers");

  prop = api_def_prop(sapi, "view_height", PROP_FLOAT, PROP_UNIT_LENGTH);
  api_def_prop_ui_range(prop, 0.1f, 10.0f, 0.1, 2);
  api_def_prop_range(prop, 0.0f, 1000.0f);
  api_def_prop_ui_text(prop, "View Height", "View distance from the floor when walking");

  prop = api_def_prop(sapi, "jump_height", PROP_FLOAT, PROP_UNIT_LENGTH);
  api_def_prop_ui_range(prop, 0.1f, 10.0f, 0.1, 2);
  api_def_prop_range(prop, 0.1f, 100.0f);
  api_def_prop_ui_text(prop, "Jump Height", "Maximum height of a jump");

  prop = api_def_prop(sapi, "teleport_time", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_text(
      prop, "Teleport Duration", "Interval of time warp when teleporting in navigation mode");

  prop = api_def_prop(sapi, "use_gravity", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_WALK_GRAVITY);
  api_def_prop_ui_text(prop, "Gravity", "Walk with gravity, or free navigate");

  prop = api_def_prop(sapi, "use_mouse_reverse", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_WALK_MOUSE_REVERSE);
  api_def_prop_ui_text(prop, "Reverse Mouse", "Reverse the vertical movement of the mouse");
}

static void api_def_userdef_view(DuneApi *dapi)
{
  static const EnumPropItem timecode_styles[] = {
      {USER_TIMECODE_MINIMAL,
       "MINIMAL",
       0,
       "Minimal Info",
       "Most compact representation, uses '+' as separator for sub-second frame numbers, "
       "with left and right truncation of the timecode as necessary"},
      {USER_TIMECODE_SMPTE_FULL,
       "SMPTE",
       0,
       "SMPTE (Full)",
       "Full SMPTE timecode (format is HH:MM:SS:FF)"},
      {USER_TIMECODE_SMPTE_MSF,
       "SMPTE_COMPACT",
       0,
       "SMPTE (Compact)",
       "SMPTE timecode showing minutes, seconds, and frames only - "
       "hours are also shown if necessary, but not by default"},
      {USER_TIMECODE_MILLISECONDS,
       "MILLISECONDS",
       0,
       "Compact with Milliseconds",
       "Similar to SMPTE (Compact), except that instead of frames, "
       "milliseconds are shown instead"},
      {USER_TIMECODE_SECONDS_ONLY,
       "SECONDS_ONLY",
       0,
       "Only Seconds",
       "Direct conversion of frame numbers to seconds"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem color_picker_types[] = {
      {USER_CP_CIRCLE_HSV,
       "CIRCLE_HSV",
       0,
       "Circle (HSV)",
       "A circular Hue/Saturation color wheel, with "
       "Value slider"},
      {USER_CP_CIRCLE_HSL,
       "CIRCLE_HSL",
       0,
       "Circle (HSL)",
       "A circular Hue/Saturation color wheel, with "
       "Lightness slider"},
      {USER_CP_SQUARE_SV,
       "SQUARE_SV",
       0,
       "Square (SV + H)",
       "A square showing Saturation/Value, with Hue slider"},
      {USER_CP_SQUARE_HS,
       "SQUARE_HS",
       0,
       "Square (HS + V)",
       "A square showing Hue/Saturation, with Value slider"},
      {USER_CP_SQUARE_HV,
       "SQUARE_HV",
       0,
       "Square (HV + S)",
       "A square showing Hue/Value, with Saturation slider"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem zoom_frame_modes[] = {
      {ZOOM_FRAME_MODE_KEEP_RANGE, "KEEP_RANGE", 0, "Keep Range", ""},
      {ZOOM_FRAME_MODE_SECONDS, "SECONDS", 0, "Seconds", ""},
      {ZOOM_FRAME_MODE_KEYFRAMES, "KEYFRAMES", 0, "Keyframes", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem line_width[] = {
      {-1, "THIN", 0, "Thin", "Thinner lines than the default"},
      {0, "AUTO", 0, "Default", "Automatic line width based on UI scale"},
      {1, "THICK", 0, "Thick", "Thicker lines than the default"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem render_display_types[] = {
      {USER_RENDER_DISPLAY_NONE,
       "NONE",
       0,
       "Keep User Interface",
       "Images are rendered without changing the user interface"},
      {USER_RENDER_DISPLAY_SCREEN,
       "SCREEN",
       0,
       "Maximized Area",
       "Images are rendered in a maximized Image Editor"},
      {USER_RENDER_DISPLAY_AREA,
       "AREA",
       0,
       "Image Editor",
       "Images are rendered in an Image Editor"},
      {USER_RENDER_DISPLAY_WINDOW,
       "WINDOW",
       0,
       "New Window",
       "Images are rendered in a new window"},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropItem temp_space_display_types[] = {
      {USER_TEMP_SPACE_DISPLAY_FULLSCREEN,
       "SCREEN", /* Could be FULLSCREEN, but keeping it consistent with render_display_types */
       0,
       "Maximized Area",
       "Open the temporary editor in a maximized screen"},
      {USER_TEMP_SPACE_DISPLAY_WINDOW,
       "WINDOW",
       0,
       "New Window",
       "Open the temporary editor in a new window"},
      {0, NULL, 0, NULL, NULL},
  };

  ApiProp *prop;
  ApiStruct *sapi;

  sapi = api_def_struct(dapi, "PrefsView", NULL);
  api_def_struct_stype(sapi, "UserDef");
  api_def_struct_nested(dapi, sapi, "Prefs");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "View & Controls", "Preferences related to viewing data");

  /* View. */
  prop = api_def_prop(sapi, "ui_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(
      prop, "UI Scale", "Changes the size of the fonts and widgets in the interface");
  api_def_prop_range(prop, 0.25f, 4.0f);
  api_def_prop_ui_range(prop, 0.5f, 2.0f, 1, 2);
  api_def_prop_update(prop, 0, "api_userdef_dpi_update");

  prop = api_def_prop(sapi, "ui_line_width", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, line_width);
  api_def_prop_ui_text(
      prop,
      "UI Line Width",
      "Changes the thickness of widget outlines, lines and dots in the interface");
  api_def_prop_update(prop, 0, "api_userdef_dpi_update");

  /* display */
  prop = api_def_prop(sapi, "show_tooltips", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_TOOLTIPS);
  api_def_prop_ui_text(
      prop, "Tooltips", "Display tooltips (when disabled, hold Alt to force display)");

  prop = api_def_prop(sapi, "show_tooltips_python", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_TOOLTIPS_PYTHON);
  api_def_prop_ui_text(prop, "Python Tooltips", "Show Python references in tooltips");

  prop = api_def_prop(sapi, "show_developer_ui", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_DEVELOPER_UI);
  api_def_prop_ui_text(
      prop,
      "Developer Extras",
      "Show options for developers (edit source in context menu, geometry indices)");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "show_object_info", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_DRAWVIEWINFO);
  api_def_prop_ui_text(prop,
                       "Display Object Info",
                       "Include the name of the active object and the current frame number in "
                       "the text info overlay");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "show_view_name", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_SHOW_VIEWPORTNAME);
  api_def_prop_ui_text(prop,
                           "Display View Name",
                           "Include the name of the view orientation in the text info overlay");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "show_splash", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "uiflag", USER_SPLASH_DISABLE);
  api_def_prop_ui_text(prop, "Show Splash", "Display splash screen on startup");

  prop = api_def_prop(sapi, "show_playback_fps", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_SHOW_FPS);
  api_def_prop_ui_text(prop,
                       "Display Playback Frame Rate (FPS)",
                       "Include the number of frames displayed per second in the text info "
                       "overlay while animation is played back");
  api_def_prop_update(prop, 0, "api_userdef_update");

  USERDEF_TAG_DIRTY_PROP_UPDATE_DISABLE;
  prop = api_def_prop(sapi, "show_addons_enabled_only", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "space_data.flag", USER_SPACEDATA_ADDONS_SHOW_ONLY_ENABLED);
  api_def_prop_ui_text(prop,
                       "Enabled Add-ons Only",
                       "Only show enabled add-ons. Un-check to see all installed add-ons");
  USERDEF_TAG_DIRTY_PROP_UPDATE_ENABLE;

  static const EnumPropItem factor_display_items[] = {
      {USER_FACTOR_AS_FACTOR, "FACTOR", 0, "Factor", "Display factors as values between 0 and 1"},
      {USER_FACTOR_AS_PERCENTAGE, "PERCENTAGE", 0, "Percentage", "Display factors as percentages"},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "factor_display_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, factor_display_items);
  api_def_prop_ui_text(prop, "Factor Display Type", "How factor values are displayed");
  api_def_prop_update(prop, 0, "api_userdef_update");

  /* Weight Paint */
  prop = api_def_prop(sapi, "use_weight_color_range", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_CUSTOM_RANGE);
  api_def_prop_ui_text(
      prop,
      "Use Weight Color Range",
      "Enable color range used for weight visualization in weight painting mode");
  api_def_prop_update(prop, 0, "api_UserDef_weight_color_update");

  prop = api_def_prop(sapi, "weight_color_range", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "coba_weight");
  api_def_prop_struct_type(prop, "ColorRamp");
  api_def_prop_ui_text(prop,
                       "Weight Color Range",
                       "Color range used for weight visualization in weight painting mode");
  api_def_prop_update(prop, 0, "api_UserDef_weight_color_update");

  prop = api_def_prop(sapi, "show_navigate_ui", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_SHOW_GIZMO_NAVIGATE);
  api_def_prop_ui_text(
      prop,
      "Navigation Controls",
      "Show navigation controls in 2D and 3D views which do not have scroll bars");
  api_def_prop_update(prop, 0, "api_userdef_gizmo_update");

  /* menus */
  prop = api_def_prop(sapi, "use_mouse_over_open", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_MENUOPENAUTO);
  api_def_prop_ui_text(
      prop,
      "Open on Mouse Over",
      "Open menu buttons and pulldowns automatically when the mouse is hovering");

  prop = api_def_prop(sapi, "open_toplevel_delay", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "menuthreshold1");
  api_def_prop_range(prop, 1, 40);
  api_def_prop_ui_text(
      prop,
      "Top Level Menu Open Delay",
      "Time delay in 1/10 seconds before automatically opening top level menus");

  prop = api_def_prop(sapi, "open_sublevel_delay", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "menuthreshold2");
  api_def_prop_range(prop, 1, 40);
  api_def_prop_ui_text(
      prop,
      "Sub Level Menu Open Delay",
      "Time delay in 1/10 seconds before automatically opening sub level menus");

  prop = api_def_prop(sapi, "color_picker_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, color_picker_types);
  api_def_prop_enum_stype(prop, NULL, "color_picker_type");
  api_def_prop_ui_text(
      prop, "Color Picker Type", "Different styles of displaying the color picker widget");
  api_def_prop_update(prop, 0, "api_userdef_update");

  /* pie menus */
  prop = api_def_prop(sapi, "pie_initial_timeout", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 0, 1000);
  api_def_prop_ui_text(
      prop,
      "Recenter Timeout",
      "Pie menus will use the initial mouse position as center for this amount of time "
      "(in 1/100ths of sec)");

  prop = api_def_prop(sapi, "pie_tap_timeout", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 0, 1000);
  api_def_prop_ui_text(prop,
                       "Tap Key Timeout",
                       "Pie menu button held longer than this will dismiss menu on release."
                       "(in 1/100ths of sec)");

  prop = api_def_prop(sapi, "pie_animation_timeout", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 0, 1000);
  api_def_prop_ui_text(
      prop,
      "Animation Timeout",
      "Time needed to fully animate the pie to unfolded state (in 1/100ths of sec)");

  prop = api_def_prop(sapi, "pie_menu_radius", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 0, 1000);
  api_def_prop_ui_text(prop, "Radius", "Pie menu size in pixels");

  prop = api_def_prop(sapi, "pie_menu_threshold", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 0, 1000);
  api_def_prop_ui_text(
      prop, "Threshold", "Distance from center needed before a selection can be made");

  prop = api_def_prop(sapi, "pie_menu_confirm", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 0, 1000);
  api_def_prop_ui_text(prop,
                       "Confirm Threshold",
                       "Distance threshold after which selection is made (zero to disable)");

  prop = api_def_prop(sapi, "use_save_prompt", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_SAVE_PROMPT);
  api_def_prop_ui_text(
      prop, "Save Prompt", "Ask for confirmation when quitting with unsaved changes");

  prop = api_def_prop(sapi, "show_column_layout", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_PLAINMENUS);
  api_def_prop_ui_text(prop, "Toolbox Column Layout", "Use a column layout for toolbox");

  prop = api_def_prop(sapi, "use_directional_menus", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "uiflag", USER_MENUFIXEDORDER);
  api_def_prop_ui_text(prop,
                       "Contents Follow Opening Direction",
                       "Otherwise menus, etc will always be top to bottom, left to right, "
                       "no matter opening direction");

  static const EnumPropItem header_align_items[] = {
      {0, "NONE", 0, "Keep Existing", "Keep existing header alignment"},
      {USER_HEADER_FROM_PREF, "TOP", 0, "Top", "Top aligned on load"},
      {USER_HEADER_FROM_PREF | USER_HEADER_BOTTOM,
       "BOTTOM",
       0,
       "Bottom",
       "Bottom align on load (except for property editors)"},
      {0, NULL, 0, NULL, NULL},
  };
  prop = api_def_prop(sapi, "header_align", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, header_align_items);
  api_def_prop_enum_bitflag_stype(prop, NULL, "uiflag");
  api_def_prop_ui_text(prop, "Header Position", "Default header position for new space-types");
  api_def_prop_update(prop, 0, "api_userdef_screen_update_header_default");

  prop = api_def_prop(sapi, "render_display_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, render_display_types);
  api_def_prop_ui_text(
      prop, "Render Display Type", "Default location where rendered images will be displayed in");

  prop = api_def_prop(sapi, "filebrowser_display_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, temp_space_display_types);
  api_def_prop_ui_text(prop,
                       "File Browser Display Type",
                       "Default location where the File Editor will be displayed in");

  static const EnumPropItem text_hinting_items[] = {
      {0, "AUTO", 0, "Auto", ""},
      {USER_TEXT_HINTING_NONE, "NONE", 0, "None", ""},
      {USER_TEXT_HINTING_SLIGHT, "SLIGHT", 0, "Slight", ""},
      {USER_TEXT_HINTING_FULL, "FULL", 0, "Full", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* mini axis */
  static const EnumPropItem mini_axis_type_items[] = {
      {USER_MINI_AXIS_TYPE_NONE, "NONE", 0, "Off", ""},
      {USER_MINI_AXIS_TYPE_MINIMAL, "MINIMAL", 0, "Simple Axes", ""},
      {USER_MINI_AXIS_TYPE_GIZMO, "GIZMO", 0, "Interactive Navigation", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "mini_axis_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, mini_axis_type_items);
  api_def_prop_ui_text(
      prop,
      "Mini Axes Type",
      "Show small rotating 3D axes in the top right corner of the 3D viewport");
  api_def_prop_update(prop, 0, "api_userdef_gizmo_update");

  prop = api_def_prop(sapi, "mini_axis_size", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "rvisize");
  api_def_prop_range(prop, 10, 64);
  api_def_prop_ui_text(prop, "Mini Axes Size", "The axes icon's size");
  api_def_prop_update(prop, 0, "api_userdef_gizmo_update");

  prop = api_def_prop(sapi, "mini_axis_brightness", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "rvibright");
  api_def_prop_range(prop, 0, 10);
  api_def_prop_ui_text(prop, "Mini Axes Brightness", "Brightness of the icon");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "smooth_view", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "smooth_viewtx");
  api_def_prop_range(prop, 0, 1000);
  api_def_prop_ui_text(
      prop, "Smooth View", "Time to animate the view in milliseconds, zero to disable");

  prop = api_def_prop(sapi, "rotation_angle", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "pad_rot_angle");
  api_def_prop_range(prop, 0, 90);
  api_def_prop_ui_text(
      prop, "Rotation Angle", "Rotation step for numerical pad keys (2 4 6 8)");

  /* 3D transform widget */
  prop = api_def_prop(sapi, "show_gizmo", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "gizmo_flag", USER_GIZMO_DRAW);
  api_def_prop_ui_text(prop, "Gizmos", "Use transform gizmos by default");
  api_def_prop_update(prop, 0, "pia_userdef_update");

  prop = api_def_prop(sapi, "gizmo_size", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "gizmo_size");
  api_def_prop_range(prop, 10, 200);
  api_def_prop_ui_text(prop, "Gizmo Size", "Diameter of the gizmo");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "gizmo_size_navigate_v3d", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 30, 200);
  api_def_prop_ui_text(prop, "Navigate Gizmo Size", "The Navigate Gizmo size");
  api_def_prop_update(prop, 0, "api_userdef_gizmo_update");

  /* Lookdev */
  prop = api_def_prop(sapi, "lookdev_sphere_size", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "lookdev_sphere_size");
  api_def_prop_range(prop, 50, 400);
  api_def_prop_ui_text(prop, "HDRI Preview Size", "Diameter of the HDRI preview spheres");
  api_def_prop_update(prop, 0, "api_userdef_update");

  /* View2D Grid Displays */
  prop = api_def_prop(sapi, "view2d_grid_spacing_min", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "v2d_min_gridsize");
  api_def_prop_range(
      prop, 1, 500); /* XXX: perhaps the lower range should only go down to 5? */
  api_def_prop_ui_text(prop,
                       "2D View Minimum Grid Spacing",
                       "Minimum number of pixels between each gridline in 2D Viewports");
  api_def_prop_update(prop, 0, "api_userdef_update");

  /* TODO: add a setter for this, so that we can bump up the minimum size as necessary... */
  prop = api_def_prop(sapi, "timecode_style", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, timecode_styles);
  api_def_prop_enum_stype(prop, NULL, "timecode_style");
  api_def_prop_enum_fns(prop, NULL, "api_userdef_timecode_style_set", NULL);
  api_def_prop_ui_text(
      prop,
      "Timecode Style",
      "Format of timecode displayed when not displaying timing in terms of frames");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "view_frame_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, zoom_frame_modes);
  api_def_prop_enum_stype(prop, NULL, "view_frame_type");
  api_def_prop_ui_text(
      prop, "Zoom to Frame Type", "How zooming to frame focuses around current frame");

  prop = api_def_prop(sapi, "view_frame_keyframes", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 1, 500);
  api_def_prop_ui_text(prop, "Zoom Keyframes", "Keyframes around cursor that we zoom around");

  prop = api_def_prop(sapi, "view_frame_seconds", PROP_FLOAT, PROP_TIME);
  api_def_prop_range(prop, 0.0, 10000.0);
  api_def_prop_ui_text(prop, "Zoom Seconds", "Seconds around cursor that we zoom around");

  /* Text. */
  prop = api_def_prop(sapi, "use_text_antialiasing", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "text_render", USER_TEXT_DISABLE_AA);
  api_def_prop_ui_text(
      prop, "Text Anti-Aliasing", "Smooth jagged edges of user interface text");
  api_def_prop_update(prop, 0, "api_userdef_text_update");

  prop = api_def_prop(sapi, "text_hinting", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "text_render");
  api_def_prop_enum_items(prop, text_hinting_items);
  api_def_prop_ui_text(
      prop, "Text Hinting", "Method for making user interface text render sharp");
  api_def_prop_update(prop, 0, "api_userdef_text_update");

  prop = api_def_prop(sapi, "font_path_ui", PROP_STRING, PROP_FILEPATH);
  api_def_prop_string_stype(prop, NULL, "font_path_ui");
  api_def_prop_ui_text(prop, "Interface Font", "Path to interface font");
  api_def_prop_update(prop, NC_WINDOW, "api_userdef_font_update");

  prop = api_def_prop(sapi, "font_path_ui_mono", PROP_STRING, PROP_FILEPATH);
  api_def_prop_string_stype(prop, NULL, "font_path_ui_mono");
  api_def_prop_ui_text(prop, "Monospaced Font", "Path to interface monospaced Font");
  api_def_prop_update(prop, NC_WINDOW, "api_userdef_font_update");

  /* Language. */
  prop = api_def_prop(sapi, "language", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_language_default_items);
#  ifdef WITH_INTERNATIONAL
  api_def_prop_enum_fns(prop, NULL, NULL, "api_lang_enum_properties_itemf");
#  else
  api_def_prop_enum_fns(prop, "api_lang_enum_props_get_no_international", NULL, NULL);
#  endif
  api_def_prop_ui_text(prop, "Language", "Language used for translation");
  api_def_prop_update(prop, NC_WINDOW, "api_userdef_language_update");

  prop = api_def_prop(sapi, "use_translate_tooltips", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "transopts", USER_TR_TOOLTIPS);
  api_def_prop_ui_text(prop,
                       "Translate Tooltips",
                       "Translate the descriptions when hovering UI elements (recommended)");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "use_translate_interface", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "transopts", USER_TR_IFACE);
  api_def_prop_ui_text(
      prop,
      "Translate Interface",
      "Translate all labels in menus, buttons and panels "
      "(note that this might make it hard to follow tutorials or the manual)");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "use_translate_new_dataname", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "transopts", USER_TR_NEWDATANAME);
  api_def_prop_ui_text(prop,
                       "Translate New Names",
                       "Translate the names of new data-blocks (objects, materials...)");
  api_def_prop_update(prop, 0, "api_userdef_update");

  /* Status-bar. */
  prop = api_def_prop(sapi, "show_statusbar_memory", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "statusbar_flag", STATUSBAR_SHOW_MEMORY);
  api_def_prope_ui_text(prop, "Show Memory", "Show Blender memory usage");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_INFO, "api_userdef_update");

  prop = api_def_prop(sapi, "show_statusbar_vram", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "statusbar_flag", STATUSBAR_SHOW_VRAM);
  api_def_prop_ui_text(prop, "Show VRAM", "Show GPU video memory usage");
  api_def_prop_editable_fn(prop, "api_show_statusbar_vram_editable");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_INFO, "api_userdef_update");

  prop = api_def_prop(sapi, "show_statusbar_version", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "statusbar_flag", STATUSBAR_SHOW_VERSION);
  api_def_prop_ui_text(prop, "Show Version", "Show Dune version string");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_INFO, "api_userdef_update");

  prop = api_def_prop(sapi, "show_statusbar_stats", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "statusbar_flag", STATUSBAR_SHOW_STATS);
  api_def_prop_ui_text(prop, "Show Statistics", "Show scene statistics");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_INFO, "api_userdef_update");

  prop = api_def_prop(sapi, "show_statusbar_scene_duration", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "statusbar_flag", STATUSBAR_SHOW_SCENE_DURATION);
  api_def_prop_ui_text(prop, "Show Scene Duration", "Show scene duration");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_INFO, "api_userdef_update");
}

static void api_def_userdef_edit(DuneApi *dapi)
{
  ApiProp *prop;
  ApiStruct *sapi;

  static const EnumPropItem auto_key_modes[] = {
      {AUTOKEY_MODE_NORMAL, "ADD_REPLACE_KEYS", 0, "Add/Replace", ""},
      {AUTOKEY_MODE_EDITKEYS, "REPLACE_KEYS", 0, "Replace", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem material_link_items[] = {
      {0,
       "OBDATA",
       0,
       "Object Data",
       "Toggle whether the material is linked to object data or the object block"},
      {USER_MAT_ON_OB,
       "OBJECT",
       0,
       "Object",
       "Toggle whether the material is linked to object data or the object block"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem object_align_items[] = {
      {0, "WORLD", 0, "World", "Align newly added objects to the world coordinate system"},
      {USER_ADD_VIEWALIGNED,
       "VIEW",
       0,
       "View",
       "Align newly added objects to the active 3D view orientation"},
      {USER_ADD_CURSORALIGNED,
       "CURSOR",
       0,
       "3D Cursor",
       "Align newly added objects to the 3D Cursor's rotation"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "PrefsEdit", NULL);
  api_def_struct_stype(sapi, "UserDef");
  api_def_struct_nested(dapi, sapi, "Prefs");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Edit Methods", "Settings for interacting with Blender data");

  /* Edit Methods */
  prop = api_def_prop(sapi, "material_link", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, material_link_items);
  api_def_prop_ui_text(
      prop,
      "Material Link To",
      "Toggle whether the material is linked to object data or the object block");

  prop = api_def_prop(sapi, "object_align", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, object_align_items);
  api_def_prop_ui_text(
      prop, "Align Object To", "The default alignment for objects added from a 3D viewport menu");

  prop = api_def_prop(sapi, "use_enter_edit_mode", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_ADD_EDITMODE);
  api_def_prop_ui_text(
      prop, "Enter Edit Mode", "Enter edit mode automatically after adding a new object");

  prop = api_def_prop(sapi, "collection_instance_empty_size", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.001f, FLT_MAX);
  api_def_prop_ui_text(prop,
                       "Collection Instance Empty Size",
                       "Display size of the empty when new collection instances are created");

  /* Text Editor */
  prop = api_def_prop(sapi, "use_text_edit_auto_close", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "text_flag", USER_TEXT_EDIT_AUTO_CLOSE);
  api_def_prop_ui_text(
      prop,
      "Auto Close Character Pairs",
      "Automatically close relevant character pairs when typing in the text editor");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_TEXT, NULL);

  /* Undo */

  prop = api_def_prop(sapi, "undo_steps", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "undosteps");
  api_def_prop_range(prop, 0, 256);
  api_def_prop_int_fns(prop, NULL, "api_userdef_undo_steps_set", NULL);
  api_def_prop_ui_text(
      prop, "Undo Steps", "Number of undo steps available (smaller values conserve memory)");

  prop = api_def_prop(sapi, "undo_memory_limit", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "undomemory");
  api_def_prop_range(prop, 0, max_memory_in_megabytes_int());
  api_def_prop_ui_text(
      prop, "Undo Memory Size", "Maximum memory usage in megabytes (0 means unlimited)");

  prop = api_def_prop(sapi, "use_global_undo", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_GLOBALUNDO);
  api_def_prop_ui_text(
      prop,
      "Global Undo",
      "Global undo works by keeping a full copy of the file itself in memory, "
      "so takes extra memory");

  /* auto keyframing */
  prop = api_def_prop(sapi, "use_auto_keying", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "autokey_mode", AUTOKEY_ON);
  api_def_prop_ui_text(prop,
                       "Auto Keying Enable",
                       "Automatic keyframe insertion for Objects and Bones "
                       "(default setting used for new Scenes)");

  prop = api_def_prop(sapi, "auto_keying_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, auto_key_modes);
  api_def_prop_enum_fns(
      prop, "api_userdef_autokeymode_get", "api_userdef_autokeymode_set", NULL);
  api_def_prop_ui_text(prop,
                       "Auto Keying Mode",
                       "Mode of automatic keyframe insertion for Objects and Bones "
                       "(default setting used for new Scenes)");

  prop = api_def_prop(sapi, "use_keyframe_insert_available", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "autokey_flag", AUTOKEY_FLAG_INSERTAVAIL);
  api_def_prop_ui_text(prop,
                       "Auto Keyframe Insert Available",
                       "Automatic keyframe insertion in available F-Curves");

  prop = api_def_prop(sapi, "use_auto_keying_warning", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "autokey_flag", AUTOKEY_FLAG_NOWARNING);
  api_def_prop_ui_text(
      prop,
      "Show Auto Keying Warning",
      "Show warning indicators when transforming objects and bones if auto keying is enabled");

  /* keyframing settings */
  prop = api_def_prop(sapi, "use_keyframe_insert_needed", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "autokey_flag", AUTOKEY_FLAG_INSERTNEEDED);
  api_def_prop_ui_text(
      prop, "Keyframe Insert Needed", "Keyframe insertion only when keyframe needed");

  prop = api_def_prop(sapi, "use_visual_keying", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "autokey_flag", AUTOKEY_FLAG_AUTOMATKEY);
  api_def_prop_ui_text(
      prop, "Visual Keying", "Use Visual keying automatically for constrained objects");

  prop = api_def_prop(sapi, "use_insertkey_xyz_to_rgb", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "autokey_flag", AUTOKEY_FLAG_XYZ2RGB);
  api_def_prop_ui_text(
      prop,
      "New F-Curve Colors - XYZ to RGB",
      "Color for newly added transformation F-Curves (Location, Rotation, Scale) "
      "and also Color is based on the transform axis");

  prop = api_def_prop(sapi, "use_anim_channel_group_colors", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "animation_flag", USER_ANIM_SHOW_CHANNEL_GROUP_COLORS);
  api_def_prop_ui_text(
      prop,
      "Channel Group Colors",
      "Use animation channel group colors; generally this is used to show bone group colors");
  api_def_prop_update(prop, 0, "api_userdef_anim_update");

  prop = api_def_prop(sapi, "fcurve_new_auto_smoothing", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_fcurve_auto_smoothing_items);
  api_def_prop_enum_stype(prop, NULL, "auto_smoothing_new");
  api_def_prop_ui_text(prop,
                       "New Curve Smoothing Mode",
                       "Auto Handle Smoothing mode used for newly added F-Curves");

  prop = api_def_prop(sapi, "keyframe_new_interpolation_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_beztriple_interpolation_mode_items);
  api_def_prop_enum_stype(prop, NULL, "ipo_new");
  api_def_prop_ui_text(prop,
                       "New Interpolation Type",
                       "Interpolation mode used for first keyframe on newly added F-Curves "
                       "(subsequent keyframes take interpolation from preceding keyframe)");
  api_def_prop_translation_ctx(prop, LANG_CXT_ID_ACTION);

  prop = api_def_prop(sapi, "keyframe_new_handle_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_keyframe_handle_type_items);
  api_def_prop_enum_stype(prop, NULL, "keyhandles_new");
  api_def_prop_ui_text(prop, "New Handles Type", "Handle type for handles of new keyframes");

  /* frame numbers */
  prop = api_def_prop(sapi, "use_negative_frames", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", USER_NONEGFRAMES);
  api_def_prop_ui_text(prop,
                       "Allow Negative Frames",
                       "Current frame number can be manually set to a negative value");

  /* fcurve opacity */
  prop = api_def_prop(sapi, "fcurve_unselected_alpha", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "fcu_inactive_alpha");
  api_def_prop_range(prop, 0.001f, 1.0f);
  api_def_prop_ui_text(prop,
                       "Unselected F-Curve Opacity",
                       "The opacity of unselected F-Curves against the "
                       "background of the Graph Editor");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_GRAPH, NULL);

  /* FCurve keyframe visibility. */
  prop = api_def_prop(sapi, "show_only_selected_curve_keyframes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "animation_flag", USER_ANIM_ONLY_SHOW_SELECTED_CURVE_KEYS);
  api_def_prop_ui_text(prop,
                       "Only Show Selected F-Curve Keyframes",
                       "Only keyframes of selected F-Curves are visible and editable");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_GRAPH, NULL);

  /* Graph Editor line drawing quality. */
  prop = api_def_prop(sapi, "use_fcurve_high_quality_drawing", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "animation_flag", USER_ANIM_HIGH_QUALITY_DRAWING);
  api_def_prop_ui_text(prop,
                       "F-Curve High Quality Drawing",
                       "Draw F-Curves using Anti-Aliasing (disable for better performance)");
  api_def_prop_update(prop, NC_SPACE | ND_SPACE_GRAPH, NULL);

  /* grease pencil */
  prop = api_def_prop(sapi, "pen_manhattan_distance", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "pen_manhattandist");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop,
                       "Dune Pen Manhattan Distance",
                       "Pixels moved by mouse per axis when drawing stroke");

  prop = api_def_prop(sapi, "pen_euclidean_distance", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "pen_euclideandist");
  api_def_prop_range(prop, 0, 100);
  api_def_prop_ui_text(prop,
                      "Pen Euclidean Distance",
                      "Distance moved by mouse when drawing stroke to include");

  prop = api_def_prop(sapi, "pen_eraser_radius", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "gp_eraser");
  api_def_prop_range(prop, 1, 500);
  api_def_prop_ui_text(prop, "Pen Eraser Radius", "Radius of eraser 'brush'");

  prop = api_def_prop(sapi, "pen_default_color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "pen_new_layer_col");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Annotation Default Color", "Color of new annotation layers");

  /* sculpt and paint */
  prop = api_def_prop(sapi, "sculpt_paint_overlay_color", PROP_FLOAT, PROP_COLOR_GAMMA);
  api_def_prop_float_stype(prop, NULL, "sculpt_paint_overlay_col");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Sculpt/Paint Overlay Color", "Color of texture overlay");

  /* duplication linking */
  prop = api_def_prop(sapi, "use_duplicate_mesh", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_MESH);
  api_def_prop_ui_text(
      prop, "Duplicate Mesh", "Causes mesh data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_surface", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_SURF);
  api_def_prop_ui_text(
      prop, "Duplicate Surface", "Causes surface data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_curve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_CURVE);
  api_def_prop_ui_text(
      prop, "Duplicate Curve", "Causes curve data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_lattice", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_LATTICE);
  api_def_prop_ui_text(
      prop, "Duplicate Lattice", "Causes lattice data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_text", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_FONT);
  api_def_prop_ui_text(
      prop, "Duplicate Text", "Causes text data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_metaball", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_MBALL);
  api_def_prop_ui_text(
      prop, "Duplicate Metaball", "Causes metaball data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_armature", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_ARM);
  api_def_prop_ui_text(
      prop, "Duplicate Armature", "Causes armature data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_camera", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_CAMERA);
  api_def_prop_ui_text(
      prop, "Duplicate Camera", "Causes camera data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_speaker", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_SPEAKER);
  api_def_prop_ui_text(
      prop, "Duplicate Speaker", "Causes speaker data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_light", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_LAMP);
  api_def_prop_ui_text(
      prop, "Duplicate Light", "Causes light data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_material", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_MAT);
  api_def_prop_ui_text(
      prop, "Duplicate Material", "Causes material data to be duplicated with the object");

  /* Not implemented, keep because this is useful functionality. */
#  if
  prop = api_def_prop(sapi, "use_duplicate_texture", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_TEX);
  api_def_prop_ui_text(
      prop, "Duplicate Texture", "Causes texture data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_fcurve", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_FCURVE);
  api_def_prop_ui_text(
      prop, "Duplicate F-Curve", "Causes F-Curve data to be duplicated with the object");
#  endif

  prop = api_def_prop(sapi, "use_duplicate_action", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_ACT);
  api_def_prop_ui_text(
      prop, "Duplicate Action", "Causes actions to be duplicated with the data-blocks");

  prop = api_def_prop(sapi, "use_duplicate_particle", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_PSYS);
  api_def_prop_ui_text(
      prop, "Duplicate Particle", "Causes particle systems to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_lightprobe", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_LIGHTPROBE);
  api_def_prop_ui_text(
      prop, "Duplicate Light Probe", "Causes light probe data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_pen", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_PEN);
  api_def_prop_ui_text(
      prop, "Duplicate Pen", "Causes grease pen data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_curves", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_CURVES);
  api_def_prop_ui_text(
      prop, "Duplicate Curves", "Causes curves data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_pointcloud", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_POINTCLOUD);
  api_def_prop_ui_text(
      prop, "Duplicate Point Cloud", "Causes point cloud data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_volume", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_VOLUME);
  api_def_prop_ui_text(
      prop, "Duplicate Volume", "Causes volume data to be duplicated with the object");

  prop = api_def_prop(sapi, "use_duplicate_node_tree", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "dupflag", USER_DUP_NTREE);
  api_def_prop_ui_text(prop,
                       "Duplicate Node Tree",
                       "Make copies of node groups when duplicating nodes in the node editor");

  /* Currently only used for insert offset (aka auto-offset),
   * maybe also be useful for later stuff though. */
  prop = api_def_prop(sapi, "node_margin", PROP_INT, PROP_PIXEL);
  api_def_prop_int_stype(prop, NULL, "node_margin");
  api_def_prop_ui_text(
      prop, "Auto-offset Margin", "Minimum distance between nodes for Auto-offsetting nodes");
  api_def_prop_update(prop, 0, "api_userdef_update");

  /* cursor */
  prop = api_def_prop(sapi, "use_cursor_lock_adjust", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_LOCK_CURSOR_ADJUST);
  api_def_prop_ui_text(
      prop,
      "Cursor Lock Adjust",
      "Place the cursor without 'jumping' to the new location (when lock-to-cursor is used)");

  prop = api_def_prop(sapi, "use_mouse_depth_cursor", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "uiflag", USER_DEPTH_CURSOR);
  api_def_prop_ui_text(
      prop, "Cursor Surface Project", "Use the surface depth for cursor placement");
}

static void api_def_userdef_system(DuneApi *dapi)
{
  ApiProp *prop;
  ApiStruct *sapi;

  static const EnumPropItem gl_texture_clamp_items[] = {
      {0, "CLAMP_OFF", 0, "Off", ""},
      {8192, "CLAMP_8192", 0, "8192", ""},
      {4096, "CLAMP_4096", 0, "4096", ""},
      {2048, "CLAMP_2048", 0, "2048", ""},
      {1024, "CLAMP_1024", 0, "1024", ""},
      {512, "CLAMP_512", 0, "512", ""},
      {256, "CLAMP_256", 0, "256", ""},
      {128, "CLAMP_128", 0, "128", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem anisotropic_items[] = {
      {1, "FILTER_0", 0, "Off", ""},
      {2, "FILTER_2", 0, "2x", ""},
      {4, "FILTER_4", 0, "4x", ""},
      {8, "FILTER_8", 0, "8x", ""},
      {16, "FILTER_16", 0, "16x", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem audio_mixing_samples_items[] = {
      {256, "SAMPLES_256", 0, "256 Samples", "Set audio mixing buffer size to 256 samples"},
      {512, "SAMPLES_512", 0, "512 Samples", "Set audio mixing buffer size to 512 samples"},
      {1024, "SAMPLES_1024", 0, "1024 Samples", "Set audio mixing buffer size to 1024 samples"},
      {2048, "SAMPLES_2048", 0, "2048 Samples", "Set audio mixing buffer size to 2048 samples"},
      {4096, "SAMPLES_4096", 0, "4096 Samples", "Set audio mixing buffer size to 4096 samples"},
      {8192, "SAMPLES_8192", 0, "8192 Samples", "Set audio mixing buffer size to 8192 samples"},
      {16384,
       "SAMPLES_16384",
       0,
       "16384 Samples",
       "Set audio mixing buffer size to 16384 samples"},
      {32768,
       "SAMPLES_32768",
       0,
       "32768 Samples",
       "Set audio mixing buffer size to 32768 samples"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem audio_rate_items[] = {
#  if 0
    {8000, "RATE_8000", 0, "8 kHz", "Set audio sampling rate to 8000 samples per second"},
    {11025, "RATE_11025", 0, "11.025 kHz", "Set audio sampling rate to 11025 samples per second"},
    {16000, "RATE_16000", 0, "16 kHz", "Set audio sampling rate to 16000 samples per second"},
    {22050, "RATE_22050", 0, "22.05 kHz", "Set audio sampling rate to 22050 samples per second"},
    {32000, "RATE_32000", 0, "32 kHz", "Set audio sampling rate to 32000 samples per second"},
#  endif
    {44100, "RATE_44100", 0, "44.1 kHz", "Set audio sampling rate to 44100 samples per second"},
    {48000, "RATE_48000", 0, "48 kHz", "Set audio sampling rate to 48000 samples per second"},
#  if 0
    {88200, "RATE_88200", 0, "88.2 kHz", "Set audio sampling rate to 88200 samples per second"},
#  endif
    {96000, "RATE_96000", 0, "96 kHz", "Set audio sampling rate to 96000 samples per second"},
    {192000, "RATE_192000", 0, "192 kHz", "Set audio sampling rate to 192000 samples per second"},
    {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem audio_format_items[] = {
      {0x01, "U8", 0, "8-bit Unsigned", "Set audio sample format to 8-bit unsigned integer"},
      {0x12, "S16", 0, "16-bit Signed", "Set audio sample format to 16-bit signed integer"},
      {0x13, "S24", 0, "24-bit Signed", "Set audio sample format to 24-bit signed integer"},
      {0x14, "S32", 0, "32-bit Signed", "Set audio sample format to 32-bit signed integer"},
      {0x24, "FLOAT", 0, "32-bit Float", "Set audio sample format to 32-bit float"},
      {0x28, "DOUBLE", 0, "64-bit Float", "Set audio sample format to 64-bit float"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem audio_channel_items[] = {
      {1, "MONO", 0, "Mono", "Set audio channels to mono"},
      {2, "STEREO", 0, "Stereo", "Set audio channels to stereo"},
      {4, "SURROUND4", 0, "4 Channels", "Set audio channels to 4 channels"},
      {6, "SURROUND51", 0, "5.1 Surround", "Set audio channels to 5.1 surround sound"},
      {8, "SURROUND71", 0, "7.1 Surround", "Set audio channels to 7.1 surround sound"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem image_draw_methods[] = {
      {IMAGE_DRAW_METHOD_AUTO,
       "AUTO",
       0,
       "Automatic",
       "Automatically choose method based on GPU and image"},
      {IMAGE_DRAW_METHOD_2DTEXTURE,
       "2DTEXTURE",
       0,
       "2D Texture",
       "Use CPU for display transform and display image with 2D texture"},
      {IMAGE_DRAW_METHOD_GLSL,
       "GLSL",
       0,
       "GLSL",
       "Use GLSL shaders for display transform and display image with 2D texture"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem seq_disk_cache_compression_levels[] = {
      {USER_SEQ_DISK_CACHE_COMPRESSION_NONE,
       "NONE",
       0,
       "None",
       "Requires fast storage, but uses minimum CPU resources"},
      {USER_SEQ_DISK_CACHE_COMPRESSION_LOW,
       "LOW",
       0,
       "Low",
       "Doesn't require fast storage and uses less CPU resources"},
      {USER_SEQ_DISK_CACHE_COMPRESSION_HIGH,
       "HIGH",
       0,
       "High",
       "Works on slower storage devices and uses most CPU resources"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem seq_proxy_setup_options[] = {
      {USER_SEQ_PROXY_SETUP_MANUAL, "MANUAL", 0, "Manual", "Set up proxies manually"},
      {USER_SEQ_PROXY_SETUP_AUTOMATIC,
       "AUTOMATIC",
       0,
       "Automatic",
       "Build proxies for added movie and image strips in each preview size"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "PrefsSystem", NULL);
  api_def_struct_stype(sapi, "UserDef");
  api_def_struct_nested(dapi, sapi, "Prefs");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "System & OpenGL", "Graphics driver and operating system settings");

  /* UI settings. */
  prop = api_def_prop(sapi, "ui_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_float_stype(prop, NULL, "scale_factor");
  api_def_prop_ui_text(
      prop,
      "UI Scale",
      "Size multiplier to use when displaying custom user interface elements, so that "
      "they are scaled correctly on screens with different DPI. This value is based "
      "on operating system DPI settings and Dune display scale");

  prop = api_def_prop(sapi, "ui_line_width", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_float_stype(prop, NULL, "pixelsize");
  api_def_prop_ui_text(
      prop,
      "UI Line Width",
      "Suggested line thickness and point size in pixels, for add-ons displaying custom "
      "user interface elements, based on operating system settings and Dune UI scale");

  prop = api_def_prop(sapi, "dpi", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "pixel_size", PROP_FLOAT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_float_stypr(prop, NULL, "pixelsize");

  /* Memory */
  prop = api_def_prop(sapi, "memory_cache_limit", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "memcachelimit");
  api_def_prop_range(prop, 0, max_memory_in_megabytes_int());
  api_def_prop_ui_text(prop, "Memory Cache Limit", "Memory cache limit (in megabytes)");
  api_def_prop_update(prop, 0, "api_Userdef_memcache_update");

  /* Sequencer disk cache */
  prop = api_def_prop(sapi, "use_seq_disk_cache", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(
      prop, NULL, "seq_disk_cache_flag", SEQ_CACHE_DISK_CACHE_ENABLE);
  api_def_prop_ui_text(prop, "Use Disk Cache", "Store cached images to disk");

  prop = api_def_prop(sapi, "seq_disk_cache_dir", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "seq_disk_cache_dir");
  api_def_prop_update(prop, 0, "api_Userdef_disk_cache_dir_update");
  api_def_prop_ui_text(prop, "Disk Cache Directory", "Override default directory");

  prop = api_def_prop(sapi, "seq_disk_cache_size_limit", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "seq_disk_cache_size_limit");
  api_def_prop_range(prop, 0, INT_MAX);
  api_def_prop_ui_text(prop, "Disk Cache Limit", "Disk cache limit (in gigabytes)");

  prop = api_def_prop(sapi, "seq_disk_cache_compression", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, seq_disk_cache_compression_levels);
  api_def_prop_enum_stype(prop, NULL, "seq_disk_cache_compression");
  api_def_prop_ui_text(
      prop,
      "Disk Cache Compression Level",
      "Smaller compression will result in larger files, but less decoding overhead");

  /* Sequencer proxy setup */
  prop = api_def_prop(srna, "seq_proxy_setup", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, seq_proxy_setup_options);
  api_def_prop_enum_stype(prop, NULL, "seq_proxy_setup");
  api_def_prop_ui_text(prop, "Proxy Setup", "When and how proxies are created");

  prop = api_def_prop(sapi, "scrollback", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "scrollback");
  api_def_prop_range(prop, 32, 32768);
  api_def_prop_ui_text(
      prop, "Scrollback", "Maximum number of lines to store for the console buffer");

  /* OpenGL */

  /* Viewport anti-aliasing */
  prop = api_def_prop(sapi, "use_overlay_smooth_wire", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "gpu_flag", USER_GPU_FLAG_OVERLAY_SMOOTH_WIRE);
  api_def_prop_ui_text(
      prop, "Overlay Smooth Wires", "Enable overlay smooth wires, reducing aliasing");
  api_def_prop_update(prop, 0, "api_userdef_dpi_update");

  prop = api_def_prop(sapi, "use_edit_mode_smooth_wire", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(
      prop, NULL, "gpu_flag", USER_GPU_FLAG_NO_EDIT_MODE_SMOOTH_WIRE);
  api_def_prop_ui_text(
      prop,
      "Edit Mode Smooth Wires",
      "Enable edit mode edge smoothing, reducing aliasing (requires restart)");
  api_def_prop_update(prop, 0, "api_userdef_dpi_update");

  prop = api_def_prop(sapi, "use_region_overlap", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag2", USER_REGION_OVERLAP);
  api_def_prop_ui_text(
      prop, "Region Overlap", "Display tool/property regions over the main region");
  api_def_prop_update(prop, 0, "api_userdef_dpi_update");

  prop = api_def_prop(sapi, "viewport_aa", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_userdef_viewport_aa_items);
  api_def_prop_ui_text(
      prop, "Viewport Anti-Aliasing", "Method of anti-aliasing in 3d viewport");
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "solid_lights", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "light_param", "");
  api_def_prop_struct_type(prop, "UserSolidLight");
  api_def_prop_ui_text(
      prop, "Solid Lights", "Lights used to display objects in solid shading mode");

  prop = api_def_prop(sapi, "light_ambient", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "light_ambient");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop, "Ambient Color", "Color of the ambient light that uniformly lit the scene");
  api_def_prop_update(prop, 0, "api_UserDef_viewport_lights_update");

  prop = api_def_prop(sapi, "use_studio_light_edit", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "edit_studio_light", 1);
  api_def_prop_ui_text(
      prop, "Edit Studio Light", "View the result of the studio light editor in the viewport");
  api_def_prop_update(prop, 0, "api_UserDef_viewport_lights_update");

  prop = api_def_prop(sapi, "gl_clip_alpha", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "glalphaclip");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Clip Alpha", "Clip alpha below this threshold in the 3D textured view");
  api_def_prop_update(prop, 0, "api_userdef_update");

  /* Textures */
  prop = api_def_prop(sapi, "image_draw_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, image_draw_methods);
  api_def_prop_enum_stype(prop, NULL, "image_draw_method");
  api_def_prop_ui_text(
      prop, "Image Display Method", "Method used for displaying images on the screen");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "anisotropic_filter", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "anisotropic_filter");
  api_def_prop_enum_items(prop, anisotropic_items);
  api_def_prop_enum_default(prop, 1);
  api_def_prop_ui_text(prop, "Anisotropic Filtering", "Quality of anisotropic filtering");
  api_def_prop_update(prop, 0, "api_userdef_anisotropic_update");

  prop = api_def_prop(sapi, "gl_texture_limit", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "glreslimit");
  api_def_prop_enum_items(prop, gl_texture_clamp_items);
  api_def_prop_ui_text(
      prop, "GL Texture Limit", "Limit the texture size to save graphics memory");
  api_def_prop_update(prop, 0, "api_userdef_gl_texture_limit_update");

  prop = api_def_prop(sapi, "texture_time_out", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "textimeout");
  api_def_prop_range(prop, 0, 3600);
  api_def_prop_ui_text(
      prop,
      "Texture Time Out",
      "Time since last access of a GL texture in seconds after which it is freed "
      "(set to 0 to keep textures allocated)");

  prop = api_def_prop(sapi, "texture_collection_rate", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "texcollectrate");
  api_def_prop_range(prop, 1, 3600);
  api_def_prop_ui_text(
      prop,
      "Texture Collection Rate",
      "Number of seconds between each run of the GL texture garbage collector");

  prop = api_def_prop(sapi, "vbo_time_out", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "vbotimeout");
  api_def_prop_range(prop, 0, 3600);
  api_def_prop_ui_text(
      prop,
      "VBO Time Out",
      "Time since last access of a GL vertex buffer object in seconds after which it is freed "
      "(set to 0 to keep VBO allocated)");

  prop = api_def_prop(sapi, "vbo_collection_rate", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "vbocollectrate");
  api_def_prop_range(prop, 1, 3600);
  api_def_prop_ui_text(
      prop,
      "VBO Collection Rate",
      "Number of seconds between each run of the GL vertex buffer object garbage collector");

  /* Select */
  prop = api_def_prop(sapi, "use_select_pick_depth", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "gpu_flag", USER_GPU_FLAG_NO_DEPT_PICK);
  api_def_prop_ui_text(prop,
                       "GPU Depth Picking",
                       "When making a selection in 3D View, use the GPU depth buffer to "
                       "ensure the frontmost object is selected first");

  /* GPU subdivision evaluation. */

  prop = api_def_prop(sapi, "use_gpu_subdivision", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "gpu_flag", USER_GPU_FLAG_SUBDIVISION_EVALUATION);
  api_def_prop_ui_text(prop,
                       "GPU Subdivision",
                       "Enable GPU acceleration for evaluating the last subdivision surface "
                       "modifiers in the stack");
  api_def_prop_update(prop, 0, "api_UserDef_subdivision_update");

  /* GPU backend selection */
  prop = api_def_prop(sapi, "gpu_backend", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "gpu_backend");
  api_def_prop_enum_items(prop, api_enum_pref_gpu_backend_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_pref_gpu_backend_itemf");
  api_def_prop_ui_text(
      prop,
      "GPU Backend",
      "GPU backend to use (requires restarting Blender for changes to take effect)");

  /* Audio */
  prop = api_def_prop(sapi, "audio_mixing_buffer", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mixbufsize");
  api_def_prop_enum_items(prop, audio_mixing_samples_items);
  api_def_prop_ui_text(
      prop, "Audio Mixing Buffer", "Number of samples used by the audio mixing buffer");
  api_def_prop_update(prop, 0, "api_UserDef_audio_update");

  prop = api_def_prop(sapi, "audio_device", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "audiodevice");
  api_def_prop_enum_items(prop, audio_device_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_userdef_audio_device_itemf");
  api_def_prop_ui_text(prop, "Audio Device", "Audio output device");
  api_def_prop_update(prop, 0, "api_UserDef_audio_update");

  prop = api_def_prop(sapi, "audio_sample_rate", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "audiorate");
  api_def_prop_enum_items(prop, audio_rate_items);
  api_def_prop_ui_text(prop, "Audio Sample Rate", "Audio sample rate");
  api_def_prop_update(prop, 0, "api_UserDef_audio_update");

  prop = api_def_prop(sapi, "audio_sample_format", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "audioformat");
  api_def_prop_enum_items(prop, audio_format_items);
  api_def_prop_ui_text(prop, "Audio Sample Format", "Audio sample format");
  api_def_prop_update(prop, 0, "api_UserDef_audio_update");

  prop = api_def_prop(sapi, "audio_channels", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "audiochannels");
  api_def_prop_enum_items(prop, audio_channel_items);
  api_def_prop_ui_text(prop, "Audio Channels", "Audio channel count");
  api_def_prop_update(prop, 0, "api_UserDef_audio_update");

#  ifdef WITH_CYCLES
  prop = api_def_prop(sapi, "legacy_compute_device_type", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "compute_device_type");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_flag(prop, PROP_HIDDEN);
  api_def_prop_ui_text(prop, "Legacy Compute Device Type", "For backwards compatibility only");
#  endif
}

static void api_def_userdef_input(DuneApi *dapi)
{
  ApiProp *prop;
  ApiStruct *sapi;

  static const EnumPropItem view_rotation_items[] = {
      {0, "TURNTABLE", 0, "Turntable", "Turntable keeps the Z-axis upright while orbiting"},
      {USER_TRACKBALL,
       "TRACKBALL",
       0,
       "Trackball",
       "Trackball allows you to tumble your view at any angle"},
      {0, NULL, 0, NULL, NULL},
  };

#  ifdef WITH_INPUT_NDOF
  static const EnumPropItem ndof_view_navigation_items[] = {
      {0, "FREE", 0, "Free", "Use full 6 degrees of freedom by default"},
      {NDOF_MODE_ORBIT, "ORBIT", 0, "Orbit", "Orbit about the view center by default"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem ndof_view_rotation_items[] = {
      {NDOF_TURNTABLE,
       "TURNTABLE",
       0,
       "Turntable",
       "Use turntable style rotation in the viewport"},
      {0, "TRACKBALL", 0, "Trackball", "Use trackball style rotation in the viewport"},
      {0, NULL, 0, NULL, NULL},
  };
#  endif /* WITH_INPUT_NDOF */

  static const EnumPropItem tablet_api[] = {
      {USER_TABLET_AUTOMATIC,
       "AUTOMATIC",
       0,
       "Automatic",
       "Automatically choose Wintab or Windows Ink depending on the device"},
      {USER_TABLET_NATIVE,
       "WINDOWS_INK",
       0,
       "Windows Ink",
       "Use native Windows Ink API, for modern tablet and pen devices. Requires Windows 8 or "
       "newer"},
      {USER_TABLET_WINTAB,
       "WINTAB",
       0,
       "Wintab",
       "Use Wintab driver for older tablets and Windows versions"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem view_zoom_styles[] = {
      {USER_ZOOM_CONTINUE,
       "CONTINUE",
       0,
       "Continue",
       "Continuous zooming. The zoom direction and speed depends on how far along the set Zoom "
       "Axis the mouse has moved"},
      {USER_ZOOM_DOLLY,
       "DOLLY",
       0,
       "Dolly",
       "Zoom in and out based on mouse movement along the set Zoom Axis"},
      {USER_ZOOM_SCALE,
       "SCALE",
       0,
       "Scale",
       "Zoom in and out as if you are scaling the view, mouse movements relative to center"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem view_zoom_axes[] = {
      {0, "VERTICAL", 0, "Vertical", "Zoom in and out based on vertical mouse movement"},
      {USER_ZOOM_HORIZ,
       "HORIZONTAL",
       0,
       "Horizontal",
       "Zoom in and out based on horizontal mouse movement"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "PreferencesInput", NULL);
  api_def_struct_stype(sapi, "UserDef");
  api_def_struct_nested(dapi, sapi, "Prefs");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Input", "Settings for input devices");

  prop = api_def_prop(sapi, "view_zoom_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "viewzoom");
  api_def_prop_enum_items(prop, view_zoom_styles);
  api_def_prop_ui_text(prop, "Zoom Style", "Which style to use for viewport scaling");

  prop = api_def_prop(sapi, "view_zoom_axis", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "uiflag");
  api_def_prop_enum_items(prop, view_zoom_axes);
  api_def_prop_ui_text(prop, "Zoom Axis", "Axis of mouse movement to zoom in or out on");

  prop = api_def_prop(sapi, "use_multitouch_gestures", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "uiflag", USER_NO_MULTITOUCH_GESTURES);
  api_def_prop_ui_text(
      prop,
      "Multi-touch Gestures",
      "Use multi-touch gestures for navigation with touchpad, instead of scroll wheel emulation");
  api_def_prop_update(prop, 0, "api_userdef_input_devices");

  prop = api_def_prop(sapi, "invert_mouse_zoom", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_ZOOM_INVERT);
  api_def_prop_ui_text(
      prop, "Invert Zoom Direction", "Invert the axis of mouse movement for zooming");

  prop = api_def_prop(sapi, "use_mouse_depth_navigate", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_DEPTH_NAVIGATE);
  api_def_prop_ui_text(
      prop,
      "Auto Depth",
      "Use the depth under the mouse to improve view pan/rotate/zoom functionality");

  /* view zoom */
  prop = api_def_prop(sapi, "use_zoom_to_mouse", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_ZOOM_TO_MOUSEPOS);
  api_def_prop_ui_text(prop,
                       "Zoom to Mouse Position",
                       "Zoom in towards the mouse pointer's position in the 3D view, "
                        "rather than the 2D window center");

  /* view rotation */
  prop = api_def_prop(sapi, "use_auto_perspective", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_AUTOPERSP);
  api_def_prop_ui_text(
      prop,
      "Auto Perspective",
      "Automatically switch between orthographic and perspective when changing "
      "from top/front/side views");

  prop = api_def_prop(sapi, "use_rotate_around_active", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_ORBIT_SELECTION);
  api_def_prop_ui_text(prop, "Orbit Around Selection", "Use selection as the pivot point");

  prop = api_def_prop(sapi, "view_rotate_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, view_rotation_items);
  api_def_prop_ui_text(prop, "Orbit Method", "Orbit method in the viewport");

  prop = api_def_prop(sapi, "use_mouse_continuous", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_CONTINUOUS_MOUSE);
  api_def_prop_ui_text(
      prop,
      "Continuous Grab",
      "Let the mouse wrap around the view boundaries so mouse movements are not limited by the "
      "screen size (used by transform, dragging of UI controls, etc.)");

  prop = api_def_prop(sapi, "use_drag_immediately", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_RELEASECONFIRM);
  api_def_prop_ui_text(prop,
                       "Release Confirms",
                       "Moving things with a mouse drag confirms when releasing the button");

  prop = api_def_prop(sapi, "use_numeric_input_advanced", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_FLAG_NUMINPUT_ADVANCED);
  api_def_prop_ui_text(prop,
                       "Default to Advanced Numeric Input",
                       "When entering numbers while transforming, "
                       "default to advanced mode for full math expression evaluation");

  /* View Navigation */
  prop = api_def_prop(sapi, "navigation_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "navigation_mode");
  api_def_prop_enum_items(prop, api_enum_navigation_mode_items);
  api_def_prop_ui_text(prop, "View Navigation", "Which method to use for viewport navigation");

  prop = api_def_prop(sapi, "walk_navigation", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "walk_navigation");
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "WalkNavigation");
  api_def_prop_ui_text(prop, "Walk Navigation", "Settings for walk navigation mode");

  prop = api_def_prop(sapi, "view_rotate_sensitivity_turntable", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_range(prop, DEG2RADF(0.001f), DEG2RADF(15.0f));
  api_def_prop_ui_range(prop, DEG2RADF(0.001f), DEG2RADF(15.0f), 1.0f, 2);
  api_def_prop_ui_text(prop,
                       "Orbit Sensitivity",
                       "Rotation amount per pixel to control how fast the viewport orbits");

  prop = api_def_prop(sapi, "view_rotate_sensitivity_trackball", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.1f, 10.0f);
  api_def_prop_ui_range(prop, 0.1f, 2.0f, 0.01f, 2);
  api_def_prop_ui_text(prop, "Orbit Sensitivity", "Scale trackball orbit sensitivity");

  /* Click-drag threshold for tablet & mouse. */
  prop = api_def_prop(sapi, "drag_threshold_mouse", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 255);
  api_def_prop_ui_text(prop,
                       "Mouse Drag Threshold",
                       "Number of pixels to drag before a drag event is triggered "
                       "for mouse/track-pad input "
                       "(otherwise click events are detected)");

  prop = api_def_prop(sapi, "drag_threshold_tablet", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 255);
  api_def_prop_ui_text(prop,
                       "Tablet Drag Threshold",
                       "Number of pixels to drag before a drag event is triggered "
                       "for tablet input "
                       "(otherwise click events are detected)");

  prop = api_def_prop(sapi, "drag_threshold", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 1, 255);
  api_def_prop_ui_text(prop,
                       "Drag Threshold",
                       "Number of pixels to drag before a drag event is triggered "
                       "for keyboard and other non mouse/tablet input "
                       "(otherwise click events are detected)");

  prop = api_def_prop(sapi, "move_threshold", PROP_INT, PROP_PIXEL);
  api_def_prop_range(prop, 0, 255);
  api_def_prop_ui_range(prop, 0, 10, 1, -1);
  api_def_prop_ui_text(prop,
                       "Motion Threshold",
                       "Number of pixels to before the cursor is considered to have moved "
                       "(used for cycling selected items on successive clicks)");

  /* tablet pressure curve */
  prop = api_def_prop(sapi, "pressure_threshold_max", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_range(prop, 0.0f, 1.0f, 0.01f, 3);
  api_def_prop_ui_text(
      prop, "Max Threshold", "Raw input pressure value that is interpreted as 100% by Blender");

  prop = api_def_prop(sapi, "pressure_softness", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, -FLT_MAX, FLT_MAX);
  api_def_prop_ui_range(prop, -1.0f, 1.0f, 0.1f, 2);
  api_def_prop_ui_text(
      prop, "Softness", "Adjusts softness of the low pressure response onset using a gamma curve");

  prop = api_def_prop(sapi, "tablet_api", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, tablet_api);
  api_def_prop_ui_text(prop,
                       "Tablet API",
                       "Select the tablet API to use for pressure sensitivity (may require "
                       "restarting Dune for changes to take effect)");
  api_def_prop_update(prop, 0, "api_userdef_input_devices");

#  ifdef WITH_INPUT_NDOF
  /* 3D mouse settings */
  /* global options */
  prop = api_def_prop(sapi, "ndof_sensitivity", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.01f, 40.0f);
  api_def_prop_ui_text(prop, "Sensitivity", "Overall sensitivity of the 3D Mouse for panning");

  prop = api_def_prop(sapi, "ndof_orbit_sensitivity", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.01f, 40.0f);
  api_def_prop_ui_text(
      prop, "Orbit Sensitivity", "Overall sensitivity of the 3D Mouse for orbiting");

  prop = api_def_prop(sapi, "ndof_deadzone", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Deadzone", "Threshold of initial movement needed from the device's rest position");
  api_def_prop_update(prop, 0, "api_userdef_ndof_deadzone_update");

  prop = api_def_prop(sapi, "ndof_pan_yz_swap_axis", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_PAN_YZ_SWAP_AXIS);
  api_def_prop_ui_text(
      prop, "Y/Z Swap Axis", "Pan using up/down on the device (otherwise forward/backward)");

  prop = api_def_prop(sapi, "ndof_zoom_invert", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_ZOOM_INVERT);
  api_def_prop_ui_text(prop, "Invert Zoom", "Zoom using opposite direction");

  /* 3D view */
  prop = api_def_prop(sapi, "ndof_show_guide", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_SHOW_GUIDE);

  /* TODO: update description when fly-mode visuals are in place
   * ("projected position in fly mode"). */
  api_def_prop_ui_text(
      prop, "Show Navigation Guide", "Display the center and axis during rotation");

  /* 3D view */
  prop = api_def_prop(sapi, "ndof_view_navigate_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "ndof_flag");
  api_def_prop_enum_items(prop, ndof_view_navigation_items);
  api_def_prop_ui_text(prop, "NDOF View Navigate", "Navigation style in the viewport");

  prop = api_def_prop(sapi, "ndof_view_rotate_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "ndof_flag");
  api_def_prop_enum_items(prop, ndof_view_rotation_items);
  api_def_prop_ui_text(prop, "NDOF View Rotation", "Rotation style in the viewport");

  /* 3D view: yaw */
  prop = api_def_prop(sapi, "ndof_rotx_invert_axis", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_ROTX_INVERT_AXIS);
  api_def_prop_ui_text(prop, "Invert Pitch (X) Axis", "");

  /* 3D view: pitch */
  prop = api_def_prop(sapi, "ndof_roty_invert_axis", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_ROTY_INVERT_AXIS);
  api_def_prop_ui_text(prop, "Invert Yaw (Y) Axis", "");

  /* 3D view: roll */
  prop = api_def_prop(sapi, "ndof_rotz_invert_axis", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_ROTZ_INVERT_AXIS);
  api_def_prop_ui_text(prop, "Invert Roll (Z) Axis", "");

  /* 3D view: pan x */
  prop = api_def_prop(sapi, "ndof_panx_invert_axis", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_PANX_INVERT_AXIS);
  api_def_prop_ui_text(prop, "Invert X Axis", "");

  /* 3D view: pan y */
  prop = api_def_prop(sapi, "ndof_pany_invert_axis", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_PANY_INVERT_AXIS);
  api_def_prop_ui_text(prop, "Invert Y Axis", "");

  /* 3D view: pan z */
  prop = api_def_prop(sapi, "ndof_panz_invert_axis", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_PANZ_INVERT_AXIS);
  api_def_prop_ui_text(prop, "Invert Z Axis", "");

  /* 3D view: fly */
  prop = api_def_prop(sapi, "ndof_lock_horizon", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_LOCK_HORIZON);
  api_def_prop_ui_text(prop, "Lock Horizon", "Keep horizon level while flying with 3D Mouse");

  prop = api_def_prop(sapi, "ndof_fly_helicopter", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_FLY_HELICOPTER);
  api_def_prop_ui_text(prop,
                       "Helicopter Mode",
                       "Device up/down directly controls the Z position of the 3D viewport");

  prop = api_def_prop(sapi, "ndof_lock_camera_pan_zoom", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ndof_flag", NDOF_CAMERA_PAN_ZOOM);
  api_def_prop_ui_text(
      prop,
      "Lock Camera Pan/Zoom",
      "Pan/zoom the camera view instead of leaving the camera view when orbiting");

  /* let Python know whether NDOF is enabled */
  prop = api_def_bool(sapi, "use_ndof", true, "", "");
#  else
  prop = api_def_bool(sapi, "use_ndof", false, "", "");
#  endif /* WITH_INPUT_NDOF */
  api_def_prop_flag(prop, PROP_IDPROP);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "mouse_double_click_time", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "dbl_click_time");
  api_def_prop_range(prop, 1, 1000);
  api_def_prop_ui_text(prop, "Double Click Timeout", "Time/delay (in ms) for a double click");

  prop = api_def_prop(sapi, "use_mouse_emulate_3_btn", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_TWOBTNMOUSE);
  api_def_prop_ui_text(
      prop, "Emulate 3 Button Mouse", "Emulate Middle Mouse with Alt+Left Mouse");
  api_def_prop_flag(prop, PROP_CTX_UPDATE);
  api_def_prop_update(prop, 0, "api_userdef_keyconfig_reload_update");

  static const EnumPropItem mouse_emulate_3_btn_mod[] = {
      {USER_EMU_MMB_MOD_ALT, "ALT", 0, "Alt", ""},
      {USER_EMU_MMB_MOD_OSKEY, "OSKEY", 0, "OS-Key", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = api_def_prop(sapi, "mouse_emulate_3_btn_mod", PROP_ENUM, PROP_NONE);
  /* Only needed because of WIN32 inability to support the option. */
  api_def_prop_enum_fns(prop, "rna_UserDef_mouse_emulate_3_button_modifier_get", NULL, NULL);
  api_def_prop_enum_items(prop, mouse_emulate_3_btn_mod);
  api_def_prop_ui_text(
      prop, "Emulate 3 Button Modifier", "Hold this modifier to emulate the middle mouse button");
  api_def_prop_flag(prop, PROP_CTX_UPDATE);
  api_def_prop_update(prop, 0, "rna_userdef_keyconfig_reload_update");

  prop = api_def_prop(sapi, "use_emulate_numpad", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_NONUMPAD);
  api_def_prop_ui_text(
      prop, "Emulate Numpad", "Main 1 to 0 keys act as the numpad ones (useful for laptops)");

  prop = api_def_prop(sapi, "invert_zoom_wheel", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_WHEELZOOMDIR);
  api_def_prop_ui_text(prop, "Wheel Invert Zoom", "Swap the Mouse Wheel zoom direction");
}

static void api_def_userdef_keymap(DuneApi *dapi)
{
  ApiProp *prop;

  ApiStruct *sapi = api_def_struct(dapi, "PrefsKeymap", NULL);
  api_def_struct_stype(sapi, "UserDef");
  api_def_struct_nested(dapi, sapi, "Prefs");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Keymap", "Shortcut setup for keyboards and other input devices");

  prop = api_def_prop(sapi, "show_ui_keyconfig", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(
      prop, NULL, "space_data.flag", USER_SPACEDATA_INPUT_HIDE_UI_KEYCONFIG);
  api_def_prop_ui_text(prop, "Show UI Key-Config", "");

  prop = api_def_prop(sapi, "active_keyconfig", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "keyconfigstr");
  api_def_prop_ui_text(prop, "Key Config", "The name of the active key configuration");
}

static void api_def_userdef_filepaths_asset_lib(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "UserAssetLib", NULL);
  api_def_struct_stype(sapi, "bUserAssetLib");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(
      srna, "Asset Lib", "Settings to define a reusable lib for Asset Browsers to use");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(
      prop, "Name", "Id (not necessarily unique) for the asset lib");
  api_def_prop_string_fns(prop, NULL, NULL, "api_userdef_asset_lib_name_set");
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "path", PROP_STRING, PROP_DIRPATH);
  api_def_prop_ui_text(
      prop, "Path", "Path to a directory with .dune files to use as an asset lib");
  api_def_prop_string_fns(prop, NULL, NULL, "api_userdef_asset_lib_path_set");
  api_def_prop_update(prop, 0, "api_userdef_update");

  static const EnumPropItem import_method_items[] = {
      {ASSET_IMPORT_LINK, "LINK", 0, "Link", "Import the assets as linked data-block"},
      {ASSET_IMPORT_APPEND,
       "APPEND",
       0,
       "Append",
       "Import the assets as copied data-block, with no link to the original asset data-block"},
      {ASSET_IMPORT_APPEND_REUSE,
       "APPEND_REUSE",
       0,
       "Append (Reuse Data)",
       "Import the assets as copied data-block while avoiding multiple copies of nested, "
       "typically heavy data. For example the textures of a material asset, or the mesh of an "
       "object asset, don't have to be copied every time this asset is imported. The instances of "
       "the asset share the data instead"},
      {0, NULL, 0, NULL, NULL},
  };
  prop = api_def_prop(sapi, "import_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, import_method_items);
  api_def_prop_ui_text(
      prop,
      "Default Import Method",
      "Determine how the asset will be imported, unless overridden by the Asset Browser");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "use_relative_path", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", ASSET_LIB_RELATIVE_PATH);
  api_def_prop_ui_text(
      prop, "Relative Path", "Use relative path when linking assets from this asset library");
}

static void api_def_userdef_script_directory(DuneApi *dapi)
{
  ApiStruct *sapi = api_def_struct(dapi, "ScriptDirectory", NULL);
  api_def_struct_stype(sapi, "UserScriptDirectory");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Python Scripts Directory", "");

  ApiProp *prop;

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Id for the Python scripts directory");
  api_def_prop_string_fns(prop, NULL, NULL, "api_userdef_script_directory_name_set");
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "directory", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "dir_path");
  api_def_prop_ui_text(
      prop,
      "Python Scripts Directory",
      "Alternate script path, matching the default layout with sub-directories: startup, add-ons, "
      "modules, and presets (requires restart)");
  /* TODO: editing should reset sys.path! */
}

static void api_def_userdef_script_directory_collection(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "ScriptDirectoryCollection");
  sapi = api_def_struct(dapi, "ScriptDirectoryCollection", NULL);
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Python Scripts Directories", "");

  fn = api_def_fn(sapi, "new", "api_userdef_script_directory_new");
  api_def_fn_flag(fn, FN_NO_SELF);
  api_def_fn_ui_description(fn, "Add a new python script directory");
  /* return type */
  parm = api_def_ptr(fn, "script_directory", "ScriptDirectory", "", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_userdef_script_directory_remove");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove a python script directory");
  parm = api_def_ptr(fn, "script_directory", "ScriptDirectory", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_userdef_filepaths(DuneApi *dapi)
{
  ApiProp *prop;
  AoiStruct *sapi;

  static const EnumPropItem anim_player_presets[] = {
      {0, "INTERNAL", 0, "Internal", "Built-in animation player"},
      {2, "DJV", 0, "DJV", "Open source frame player"},
      {3, "FRAMECYCLER", 0, "FrameCycler", "Frame player from IRIDAS"},
      {4, "RV", 0, "RV", "Frame player from Tweak Software"},
      {5, "MPLAYER", 0, "MPlayer", "Media player for video and PNG/JPEG/SGI image sequences"},
      {50, "CUSTOM", 0, "Custom", "Custom animation player executable path"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem preview_type_items[] = {
      {USER_FILE_PREVIEW_NONE, "NONE", 0, "None", "Do not create blend previews"},
      {USER_FILE_PREVIEW_AUTO, "AUTO", 0, "Auto", "Automatically select best preview type"},
      {USER_FILE_PREVIEW_SCREENSHOT, "SCREENSHOT", 0, "Screenshot", "Capture the entire window"},
      {USER_FILE_PREVIEW_CAMERA, "CAMERA", 0, "Camera View", "Workbench render of scene"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "PrefsFilePaths", NULL);
  api_def_struct_stype(sapi, "UserDef");
  api_def_struct_nested(dapi, sapi, "Prefs");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "File Paths", "Default paths for external files");

  prop = api_def_prop(sapi, "show_hidden_files_datablocks", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "uiflag", USER_HIDE_DOT);
  api_def_prop_ui_text(prop,
                       "Show Hidden Files/Data-Blocks",
                       "Show files and data-blocks that are normally hidden");

  prop = api_def_prop(sapi, "use_filter_files", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "uiflag", USER_FILTERFILEEXTS);
  api_def_prop_ui_text(prop, "Filter Files", "Enable filtering of files in the File Browser");

  prop = api_def_prop(sapi, "show_recent_locations", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "uiflag", USER_HIDE_RECENT);
  api_def_prop_ui_text(
      prop, "Show Recent Locations", "Show Recent locations list in the File Browser");

  prop = api_def_prop(sapi, "show_system_bookmarks", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "uiflag", USER_HIDE_SYSTEM_BOOKMARKS);
  api_def_prop_ui_text(
      prop, "Show System Locations", "Show System locations list in the File Browser");

  prop = api_def_prop(sapi, "use_relative_paths", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_RELPATHS);
  api_def_prop_ui_text(
      prop,
      "Relative Paths",
      "Default relative path option for the file selector, when no path is defined yet");

  prop = api_def_prop(sapi, "use_file_compression", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_FILECOMPRESS);
  api_def_prop_ui_text(
      prop, "Compress File", "Enable file compression when saving .blend files");

  prop = api_def_prop(sapi, "use_load_ui", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", USER_FILENOUI);
  api_def_prop_ui_text(prop, "Load UI", "Load user interface setup when loading .blend files");
  api_def_prop_update(prop, 0, "api_userdef_load_ui_update");

  prop = api_def_prop(sapi, "use_scripts_auto_execute", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", USER_SCRIPT_AUTOEXEC_DISABLE);
  api_def_prop_ui_text(prop,
                       "Auto Run Python Scripts",
                       "Allow any .dune file to run scripts automatically "
                       "(unsafe with blend files from an untrusted source)");
  api_def_prop_update(prop, 0, "api_userdef_script_autoexec_update");

  prop = api_def_prop(sapi, "use_tabs_as_spaces", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", USER_TXT_TABSTOSPACES_DISABLE);
  api_def_prop_ui_text(
      prop,
      "Tabs as Spaces",
      "Automatically convert all new tabs into spaces for new and loaded text files");

  /* Directories. */
  prop = api_def_prop(sapi, "font_directory", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "fontdir");
  api_def_prop_ui_text(
      prop, "Fonts Directory", "The default directory to search for loading fonts");

  prop = api_def_prop(sapi, "texture_directory", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "textudir");
  api_def_prop_ui_text(
      prop, "Textures Directory", "The default directory to search for textures");

  prop = api_def_prop(sapi, "render_output_directory", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "renderdir");
  api_def_prop_ui_text(prop,
                       "Render Output Directory",
                       "The default directory for rendering output, for new scenes");

  api_def_userdef_script_directory(dapi);

  prop = api_def_prop(sapi, "script_directories", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "ScriptDirectory");
  api_def_prop_ui_text(prop, "Python Scripts Directory", "");
  api_def_userdef_script_directory_collection(dapi, prop);

  prop = api_def_prop(sapi, "lang_branches_directory", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "i18ndir");
  api_def_prop_ui_text(
      prop,
      "Translation Branches Directory",
      "The path to the '/branches' directory of your local svn-translation copy, "
      "to allow translating from the UI");

  prop = api_def_prop(sapi, "sound_directory", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "sounddir");
  api_def_prop_ui_text(prop, "Sounds Directory", "The default directory to search for sounds");

  prop = api_def_prop(sapi, "temporary_directory", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "tempdir");
  api_def_prop_ui_text(
      prop, "Temporary Directory", "The directory for storing temporary save files");
  api_def_prop_update(prop, 0, "api_userdef_temp_update");

  prop = api_def_prop(sapi, "render_cache_directory", PROP_STRING, PROP_DIRPATH);
  api_def_prop_string_stype(prop, NULL, "render_cachedir");
  api_def_prop_ui_text(prop, "Render Cache Path", "Where to cache raw render results");

  prop = api_def_prop(sapi, "image_editor", PROP_STRING, PROP_FILEPATH);
  api_def_prop_string_stype(prop, NULL, "image_editor");
  api_def_prop_ui_text(prop, "Image Editor", "Path to an image editor");

  prop = api_def_prop(sapi, "animation_player", PROP_STRING, PROP_FILEPATH);
  api_def_prop_string_stype(prop, NULL, "anim_player");
  api_def_prop_ui_text(
      prop, "Animation Player", "Path to a custom animation/frame sequence player");

  prop = api_def_prop(sapi, "animation_player_preset", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "anim_player_preset");
  api_def_prop_enum_items(prop, anim_player_presets);
  api_def_prop_ui_text(
      prop, "Animation Player Preset", "Preset configs for external animation players");

  /* Autosave. */
  prop = api_def_prop(sapi, "save_version", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "versions");
  api_def_prop_range(prop, 0, 32);
  api_def_prop_ui_text(
      prop,
      "Save Versions",
      "The number of old versions to maintain in the current directory, when manually saving");

  prop = api_def_prop(sapi, "use_auto_save_temporary_files", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", USER_AUTOSAVE);
  api_def_prop_ui_text(prop,
                       "Auto Save Temporary Files",
                       "Automatic saving of temporary files in temp directory, "
                       "uses process ID.\n"
                       "Warning: Sculpt and edit mode data won't be saved");
  api_def_prop_update(prop, 0, "api_userdef_autosave_update");

  prop = api_def_prop(sapi, "auto_save_time", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "savetime");
  api_def_prop_range(prop, 1, 60);
  api_def_prop_ui_text(
      prop, "Auto Save Time", "The time (in minutes) to wait between automatic temporary saves");
  api_def_prop_update(prop, 0, "api_userdef_autosave_update");

  prop = api_def_prop(sapi, "recent_files", PROP_INT, PROP_NONE);
  api_def_prop_range(prop, 0, 30);
  api_def_prop_ui_text(
      prop, "Recent Files", "Maximum number of recently opened files to remember");

  prop = api_def_prop(sapi, "file_preview_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, preview_type_items);
  api_def_prop_ui_text(prop, "File Preview Type", "What type of blend preview to create");

  api_def_userdef_filepaths_asset_lib(dapi);

  prop = api_def_prop(sapi, "asset_libs", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "UserAssetLib");
  api_def_prop_ui_text(prop, "Asset Libs", "");

  prop = api_def_prop(sapi, "active_asset_lib", PROP_INT, PROP_NONE);
  apis_def_prop_ui_text(prop,
                        "Active Asset Lib",
                        "Index of the asset lib being edited in the Preferences UI");
  /* Tag for UI-only update, meaning preferences will not be tagged as changed. */
  api_def_prop_update(prop, 0, "api_userdef_ui_update");
}

static void api_def_userdef_apps(DuneApi *dapi)
{
  ApiProp *prop;
  ApiStruct *sapi;

  sapi = api_def_struct(dapi, "PrefsApps", NULL);
  api_def_struct_stype(sapi, "UserDef");
  api_def_struct_nested(dapi, sapi, "Prefs");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Apps", "Preferences that work only for apps");

  prop = api_def_prop(sapi, "show_corner_split", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "app_flag", USER_APP_LOCK_CORNER_SPLIT);
  api_def_prop_ui_text(
      prop, "Corner Splitting", "Split and join editors by dragging from corners");
  api_def_prop_update(prop, 0, "api_userdef_screen_update");

  prop = api_def_prop(sapi, "show_edge_resize", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "app_flag", USER_APP_LOCK_EDGE_RESIZE);
  api_def_prop_ui_text(prop, "Edge Resize", "Resize editors by dragging from the edges");
  api_def_prop_update(prop, 0, "api_userdef_screen_update");

  prop = api_def_prop(sapi, "show_regions_visibility_toggle", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "app_flag", USER_APP_HIDE_REGION_TOGGLE);
  api_def_prop_ui_text(
      prop, "Regions Visibility Toggle", "Header and side bars visibility toggles");
  api_def_prop_update(prop, 0, "api_userdef_screen_update");
}

static void api_def_userdef_experimental(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "PrefsExperimental", NULL);
  api_def_struct_stype(sapi, "UserDef_Experimental");
  api_def_struct_nested(dapi, sapi, "Prefs");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Experimental", "Experimental features");

  prop = api_def_prop(sapi, "use_undo_legacy", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_undo_legacy", 1);
  api_def_prop_ui_text(
      prop,
      "Undo Legacy",
      "Use legacy undo (slower than the new default one, but may be more stable in some cases)");

  prop = api_def_prop(sapi, "override_auto_resync", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "no_override_auto_resync", 1);
  api_def_prop_ui_text(
      prop,
      "Override Auto Resync",
      "Enable library overrides automatic resync detection and process on file load. Disable when "
      "dealing with older .dune files that need manual Resync (Enforce) handling");

  prop = api_def_prop(sapi, "use_new_point_cloud_type", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_new_point_cloud_type", 1);
  api_def_prop_ui_text(
      prop, "New Point Cloud Type", "Enable the new point cloud type in the ui");

  prop = api_def_prop(sapi, "use_full_frame_compositor", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_full_frame_compositor", 1);
  api_def_prop_ui_text(prop,
                       "Full Frame Compositor",
                       "Enable compositor full frame execution mode option (no tiling, "
                       "reduces execution time and memory usage)");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "use_new_curves_tools", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_new_curves_tools", 1);
  api_def_prop_ui_text(
      prop, "New Curves Tools", "Enable additional features for the new curves data block");

  prop = api_def_prop(sapi, "use_cycles_debug", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_cycles_debug", 1);
  api_def_prop_ui_text(prop, "Cycles Debug", "Enable Cycles debugging options for developers");
  api_def_prop_update(prop, 0, "api_userdef_update");

  prop = api_def_prop(sapi, "use_sculpt_tools_tilt", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_sculpt_tools_tilt", 1);
  api_def_prop_ui_text(
      prop, "Sculpt Mode Tilt Support", "Support for pen tablet tilt events in Sculpt Mode");

  prop = api_def_prop(sapi, "use_sculpt_texture_paint", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_sculpt_texture_paint", 1);
  api_def_prop_ui_text(prop, "Sculpt Texture Paint", "Use texture painting in Sculpt Mode");

  prop = api_def_prop(sapi, "use_extended_asset_browser", PROP_BOOL, PROP_NONE);
  api_def_prop_ui_text(prop,
                       "Extended Asset Browser",
                       "Enable Asset Browser editor and operators to manage regular "
                       "data-blocks as assets, not just poses");
  api_def_prop_update(prop, 0, "api_userdef_ui_update");

  prop = api_def_prop(sapi, "show_asset_debug_info", PROP_BOOL, PROP_NONE);
  api_def_prop_ui_text(prop,
                       "Asset Debug Info",
                       "Enable some extra fields in the Asset Browser to aid in debugging");
  api_def_prop_update(prop, 0, "api_userdef_ui_update");

  prop = api_def_prop(sapi, "use_asset_indexing", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "no_asset_indexing", 1);
  api_def_prop_ui_text(prop,
                       "Asset Indexing",
                       "Disabling the asset indexer forces every asset library refresh to "
                       "completely reread assets from disk");
  api_def_prop_update(prop, 0, "api_userdef_ui_update");

  prop = api_def_prop(sapi, "use_override_templates", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_override_templates", 1);
  api_def_prop_ui_text(
      prop, "Override Templates", "Enable library override template in the python API");

  prop = api_def_prop(sapi, "enable_eevee_next", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "enable_eevee_next", 1);
  api_def_prop_ui_text(prop, "EEVEE Next", "Enable the new EEVEE codebase, requires restart");

  prop = api_def_prop(sapi, "enable_workbench_next", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "enable_workbench_next", 1);
  api_def_prop_ui_text(prop,
                       "Workbench Next",
                       "Enable the new Workbench codebase, requires "
                       "restart");

  prop = api_def_prop(sapi, "use_viewport_debug", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "use_viewport_debug", 1);
  api_def_prop_ui_text(prop,
                       "Viewport Debug",
                       "Enable viewport debugging options for developers in the overlays "
                       "pop-over");
  api_def_prop_update(prop, 0, "api_userdef_ui_update");

  prop = api_def_prop(sapi, "use_all_linked_data_direct", PROP_BOOL, PROP_NONE);
  api_def_prop_ui_text(
      prop,
      "All Linked Data Direct",
      "Forces all linked data to be considered as directly linked. Workaround for current "
      "issues/limitations in BAT (Dune studio pipeline tool)");

  prop = api_def_prop(sapi, "use_new_volume_nodes", PROP_BOOL, PROP_NONE);
  api_def_prop_ui_text(
      prop, "New Volume Nodes", "Enables visibility of the new Volume nodes in the UI");
}

static void api_def_userdef_addon_collection(DuneApi *dapi, DuneApi *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "Addons");
  sapi = api_def_struct(dapi, "Addons", NULL);
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "User Add-ons", "Collection of add-ons");

  fn = api_def_fn(sapi, "new", "api_userdef_addon_new");
  api_def_fn_flag(fn, FN_NO_SELF);
  api_def_fn_ui_description(fn, "Add a new add-on");
  /* return type */
  parm = api_def_ptr(fn, "addon", "Addon", "", "Add-on data");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_userdef_addon_remove");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove add-on");
  parm = api_def_ptr(fn, "addon", "Addon", "", "Add-on to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_userdef_autoexec_path_collection(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "PathCompareCollection");
  sapi = api_def_struct(dapi, "PathCompareCollection", NULL);
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Paths Compare", "Collection of paths");

  fn = api_def_fn(sapi, "new", "api_userdef_pathcompare_new");
  api_def_fn_flag(fn, FN_NO_SELF);
  api_def_fn_ui_description(fn, "Add a new path");
  /* return type */
  parm = api_def_ptr(fn, "pathcmp", "PathCompare", "", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_userdef_pathcompare_remove");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Remove path");
  parm = api_def_ptr(fn, "pathcmp", "PathCompare", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

void api_def_userdef(DuneApi *dapi)
{
  USERDEF_TAG_DIRTY_PROP_UPDATE_ENABLE;

  ApiStruct *sapi;
  ApiProp *prop;

  api_def_userdef_dothemes(dapi);
  api_def_userdef_solidlight(dapi);
  api_def_userdef_walk_navigation(dapi);

  sapi = api_def_struct(dapi, "Prefs", NULL);
  api_def_struct_stype(sapi, "UserDef");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Prefs", "Global prefs");

  prop = api_def_prop(sapi, "active_section", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "space_data.section_active");
  api_def_prop_enum_items(prop, api_enum_pref_section_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_UseDef_active_section_itemf");
  api_def_prop_ui_text(
      prop, "Active Section", "Active section of the preferences shown in the user interface");
  api_def_prop_update(prop, 0, "api_userdef_ui_update");

  /* don't expose this directly via the UI, modify via an operator */
  prop = api_def_prop(sapi, "app_template", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "app_template");
  api_def_prop_ui_text(prop, "Application Template", "");

  prop = api_def_prop(sapi, "themes", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "themes", NULL);
  api_def_prop_struct_type(prop, "Theme");
  api_def_prop_ui_text(prop, "Themes", "");

  prop = api_def_prop(sapi, "ui_styles", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "uistyles", NULL);
  api_def_prop_struct_type(prop, "ThemeStyle");
  api_def_prop_ui_text(prop, "Styles", "");

  prop = api_def_prop(sapi, "addons", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "addons", NULL);
  api_def_prop_struct_type(prop, "Addon");
  api_def_prop_ui_text(prop, "Add-on", "");
  api_def_userdef_addon_collection(dapi, prop);

  prop = api_def_prop(sapi, "autoexec_paths", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "autoexec_paths", NULL);
  api_def_prop_struct_type(prop, "PathCompare");
  api_def_prop_ui_text(prop, "Auto-Execution Paths", "");
  api_def_userdef_autoexec_path_collection(dapi, prop);

  /* nested structs */
  prop = api_def_prop(sapi, "view", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "PrefsView");
  api_def_prop_ptr_fns(prop, "api_UserDef_view_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "View & Controls", "Preferences related to viewing data");

  prop = api_def_prop(sapi, "edit", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "PrefsEdit");
  api_def_prop_ptr_fns(prop, "api_UserDef_edit_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Edit Methods", "Settings for interacting with Blender data");

  prop = api_def_prop(sapi, "inputs", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "PrefsInput");
  api_def_prop_ptr_fns(prop, "api_UserDef_input_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Inputs", "Settings for input devices");

  prop = api_def_prop(sapi, "keymap", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "PrefsKeymap");
  api_def_prop_ptr_fns(prop, "api_UserDef_keymap_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Keymap", "Shortcut setup for keyboards and other input devices");

  prop = api_def_prop(sapi, "filepaths", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "PrefsFilePaths");
  api_def_prop_ptr_fns(prop, "api_UserDef_filepaths_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "File Paths", "Default paths for external files");

  prop = api_def_prop(sapi, "system", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "PrefsSystem");
  api_def_prop_ptr_fns(prop, "api_UserDef_system_get", NULL, NULL, NULL);
  api_def_prop_ui_text(
      prop, "System & OpenGL", "Graphics driver and operating system settings");

  prop = api_def_prop(sapi, "apps", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "PrefsApps");
  api_def_prop_ptr_fns(prop, "api_UserDef_apps_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Apps", "Preferences that work only for apps");

  prop = api_def_prop(sapi, "experimental", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "PrefsExperimental");
  api_def_prop_ui_text(
      prop,
      "Experimental",
      "Settings for features that are still early in their development stage");

  prop = api_def_int_vector(sapi,
                            "version",
                            3,
                            NULL,
                            0,
                            INT_MAX,
                            "Version",
                            "Version of Dune the userpref.dune was saved with",
                            0,
                            INT_MAX);
  api_def_prop_int_fn(prop, "api_userdef_version_get", NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_flag(prop, PROP_THICK_WRAP);

  /* StudioLight Collection */
  prop = api_def_prop(sapi, "studio_lights", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "StudioLight");
  api_def_prop_sapi(prop, "StudioLights");
  api_def_prop_collection_fns(prop,
                              "api_UserDef_studiolight_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(prop, "Studio Lights", "");

  /* Preferences Flags */
  prop = api_def_prop(sapi, "use_prefs_save", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "pref_flag", USER_PREF_FLAG_SAVE);
  api_def_prop_ui_text(prop,
                       "Save on Exit",
                       "Save preferences on exit when modified "
                       "(unless factory settings have been loaded)");

  prop = api_def_prop(sapi, "is_dirty", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "runtime.is_dirty", 0);
  api_def_prop_ui_text(prop, "Dirty", "Preferences have change");
  api_def_prop_update(prop, 0, "api_userdef_ui_update");

  api_def_userdef_view(dapi);
  api_def_userdef_edit(dapi);
  api_def_userdef_input(dapi);
  api_def_userdef_keymap(dapi);
  api_def_userdef_filepaths(dapi);
  api_def_userdef_system(dapi);
  api_def_userdef_addon(dapi);
  api_def_userdef_addon_pref(dapi);
  api_def_userdef_studiolights(dapi);
  api_def_userdef_studiolight(dapi);
  api_def_userdef_pathcompare(dapi);
  api_def_userdef_apps(dapi);
  api_def_userdef_experimental(dapi);

  USERDEF_TAG_DIRTY_PROP_UPDATE_DISABLE;
}

#endif
