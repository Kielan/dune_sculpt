/**
 * High level `.dune` file read/write,
 * and functions for writing *partial* files (only selected data-blocks).
 */

#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "structs_scene_types.h"
#include "structs_screen_types.h"
#include "structs_workspace_types.h"

#include "LIB_listbase.h"
#include "LIB_path_util.h"
#include "LIB_string.h"
#include "LIB_system.h"
#include "LIB_utildefines.h"

#include "PIL_time.h"

#include "IMB_colormanagement.h"

#include "KERNEL_addon.h"
#include "KERNEL_appdir.h"
#include "KERNEL_dune.h"
#include "KERNEL_dune_version.h"
#include "KERNEL_dunefile.h"
#include "KERNEL_dunepath.h"
#include "KERNEL_colorband.h"
#include "KERNEL_context.h"
#include "KERNEL_global.h"
#include "KERNEL_ipo.h"
#include "KERNEL_keyconfig.h"
#include "KERNEL_layer.h"
#include "KERNEL_lib_id.h"
#include "KERNEL_lib_override.h"
#include "KERNEL_main.h"
#include "KERNEL_preferences.h"
#include "KERNEL_report.h"
#include "KERNEL_scene.h"
#include "KERNEL_screen.h"
#include "KERNEL_studiolight.h"
#include "KERNEL_undo_system.h"
#include "KERNEL_workspace.h"

#include "LOADER_readfile.h"
#include "LOADER_writefile.h"

#include "API_access.h"

#include "RE_pipeline.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

/* -------------------------------------------------------------------- */
/** High Level `.dune` file read/write. **/

static bool dunefile_or_libraries_versions_atleast(Main *dunemain,
                                                    const short versionfile,
                                                    const short subversionfile)
{
  if (!MAIN_VERSION_ATLEAST(dunemain, versionfile, subversionfile)) {
    return false;
  }

  LISTBASE_FOREACH (Library *, library, &dunemain->libraries) {
    if (!MAIN_VERSION_ATLEAST(library, versionfile, subversionfile)) {
      return false;
    }
  }

  return true;
}

static bool foreach_path_clean_cb(DunePathForeachPathData *UNUSED(dunepath_data),
                                  char *path_dst,
                                  const char *path_src)
{
  strcpy(path_dst, path_src);
  LIB_path_slash_native(path_dst);
  return !STREQ(path_dst, path_src);
}

/* make sure path names are correct for OS */
static void clean_paths(Main *bmain)
{
  KERNEL_dunepath_foreach_path_main(&(DunePathForeachPathData){
      .dunemain = dunemain,
      .callback_function = foreach_path_clean_cb,
      .flag = KERNEL_DUNEPATH_FOREACH_PATH_SKIP_MULTIFILE,
      .user_data = NULL,
  });

  LISTBASE_FOREACH (Scene *, scene, &dunemain->scenes) {
    LIB_path_slash_native(scene->r.pic);
  }
}

static bool wm_scene_is_visible(wmWindowManager *wm, Scene *scene)
{
  wmWindow *win;
  for (win = wm->windows.first; win; win = win->next) {
    if (win->scene == scene) {
      return true;
    }
  }
  return false;
}

static void setup_app_userdef(DuneFileData *dune_file_data)
{
  if (dune_file_data->user) {
    /* only here free userdef themes... */
    KERNEL_dune_userdef_data_set_and_free(dune_file_data->user);
    dune_file_data->user = NULL;

    /* Security issue: any dune file could include a USER block.
     *
     * Currently we load prefs from DUNE_STARTUP_FILE and later on load DUNE_USERPREF_FILE,
     * to load the preferences defined in the users home dir.
     *
     * This means we will never accidentally (or maliciously)
     * enable scripts auto-execution by loading a '.dune' file.
     */
    U.flag |= USER_SCRIPT_AUTOEXEC_DISABLE;
  }
}

/**
 * Context matching, handle no-UI case.
 *
 * note this is called on Undo so any slow conversion functions here
 * should be avoided or check (mode != LOAD_UNDO).
 *
 * param dune_file_data: Dune file data, freed by this function on exit.
 */
static void setup_app_data(duneContext *C,
                           DuneFileData *dune_file_data,
                           const struct DuneFileReadParams *params,
                           DuneFileReadReport *reports)
{
  Main *dunemain = G_MAIN;
  Scene *curscene = NULL;
  const bool recover = (G.fileflags & G_FILE_RECOVER_READ) != 0;
  const bool is_startup = params->is_startup;
  enum {
    LOAD_UI = 1,
    LOAD_UI_OFF,
    LOAD_UNDO,
  } mode;

  if (params->undo_direction != STEP_INVALID) {
    LIB_assert(dune_file_data->curscene != NULL);
    mode = LOAD_UNDO;
  }
  /* may happen with library files - UNDO file should never have NULL curscene (but may have a
   * NULL curscreen)... */
  else if (ELEM(NULL, dune_file_data->curscreen, dune_file_data->curscene)) {
    KERNEL_report(reports->reports, RPT_WARNING, "Library file, loading empty scene");
    mode = LOAD_UI_OFF;
  }
  else if (G.fileflags & G_FILE_NO_UI) {
    mode = LOAD_UI_OFF;
  }
  else {
    mode = LOAD_UI;
  }

  /* Free all render results, without this stale data gets displayed after loading files */
  if (mode != LOAD_UNDO) {
    RE_FreeAllRenderResults();
  }

  /* Only make filepaths compatible when loading for real (not undo) */
  if (mode != LOAD_UNDO) {
    clean_paths(dune_file_data->main);
  }

  /* XXX here the complex windowmanager matching */

  /* no load screens? */
  if (mode != LOAD_UI) {
    /* Logic for 'track_undo_scene' is to keep using the scene which the active screen has,
     * as long as the scene associated with the undo operation is visible
     * in one of the open windows.
     *
     * - 'curscreen->scene' - scene the user is currently looking at.
     * - 'dune_file_data->curscene' - scene undo-step was created in.
     *
     * This means users can have 2+ windows open and undo in both without screens switching.
     * But if they close one of the screens,
     * undo will ensure that the scene being operated on will be activated
     * (otherwise we'd be undoing on an off-screen scene which isn't acceptable).
     * see: T43424
     */
    wmWindow *win;
    duneScreen *curscreen = NULL;
    ViewLayer *cur_view_layer;
    bool track_undo_scene;

    /* comes from readfile.c */
    SWAP(ListBase, dunemain->wm, dune_file_data->main->wm);
    SWAP(ListBase, dunemain->workspaces, dune_file_data->main->workspaces);
    SWAP(ListBase, dunemain->screens, dune_file_data->main->screens);

    /* In case of actual new file reading without loading UI, we need to regenerate the session
     * uuid of the UI-related datablocks we are keeping from previous session, otherwise their uuid
     * will collide with some generated for newly read data. */
    if (mode != LOAD_UNDO) {
      ID *id;
      FOREACH_MAIN_LISTBASE_ID_BEGIN (&dune_file_data->main->wm, id) {
        KERNEL_lib_libblock_session_uuid_renew(id);
      }
      FOREACH_MAIN_LISTBASE_ID_END;

      FOREACH_MAIN_LISTBASE_ID_BEGIN (&dune_file_data->main->workspaces, id) {
        KERNEL_lib_libblock_session_uuid_renew(id);
      }
      FOREACH_MAIN_LISTBASE_ID_END;

      FOREACH_MAIN_LISTBASE_ID_BEGIN (&dune_file_data->main->screens, id) {
        KERNEL_lib_libblock_session_uuid_renew(id);
      }
      FOREACH_MAIN_LISTBASE_ID_END;
    }

    /* we re-use current window and screen */
    win = CTX_wm_window(C);
    curscreen = CTX_wm_screen(C);
    /* but use Scene pointer from new file */
    curscene = dune_file_data->curscene;
    cur_view_layer = dune_file_data->cur_view_layer;

    track_undo_scene = (mode == LOAD_UNDO && curscreen && curscene && dune_file_data->main->wm.first);

    if (curscene == NULL) {
      curscene = dune_file_data->main->scenes.first;
    }
    /* empty file, we add a scene to make Dune work */
    if (curscene == NULL) {
      curscene = KERNEL_scene_add(dune_file_data->main, "Empty");
    }
    if (cur_view_layer == NULL) {
      /* fallback to scene layer */
      cur_view_layer = KERNEL_view_layer_default_view(curscene);
    }

    if (track_undo_scene) {
      /* keep the old (free'd) scene, let 'blo_lib_link_screen_restore'
       * replace it with 'curscene' if its needed */
    }
    /* and we enforce curscene to be in current screen */
    else if (win) { /* The window may be NULL in background-mode. */
      win->scene = curscene;
    }

    /* KERNEL_dune_globals_clear will free G_MAIN, here we can still restore pointers */
    loader_lib_link_restore(dunemain, dune_file_data->main, CTX_wm_manager(C), curscene, cur_view_layer);
    if (win) {
      curscene = win->scene;
    }

    if (track_undo_scene) {
      wmWindowManager *wm = dune_file_data->main->wm.first;
      if (wm_scene_is_visible(wm, dune_file_data->curscene) == false) {
        curscene = dune_file_data->curscene;
        win->scene = curscene;
        KERNEL_screen_view3d_scene_sync(curscreen, curscene);
      }
    }

    /* We need to tag this here because events may be handled immediately after.
     * only the current screen is important because we won't have to handle
     * events from multiple screens at once. */
    if (curscreen) {
      KERNEL_screen_gizmo_tag_refresh(curscreen);
    }
  }

  /* free G_MAIN Main database */
  //  CTX_wm_manager_set(C, NULL);
  KERNEL_dune_globals_clear();

  dunemain = G_MAIN = dune_file_data->main;
  dune_file_data->main = NULL;

  CTX_data_main_set(C, dunemain);

  /* case G_FILE_NO_UI or no screens in file */
  if (mode != LOAD_UI) {
    /* leave entire context further unaltered? */
    CTX_data_scene_set(C, curscene);
  }
  else {
    CTX_wm_manager_set(C, dunemain->wm.first);
    CTX_wm_screen_set(C, dune_file_data->curscreen);
    CTX_data_scene_set(C, dune_file_data->curscene);
    CTX_wm_area_set(C, NULL);
    CTX_wm_region_set(C, NULL);
    CTX_wm_menu_set(C, NULL);
    curscene = dune_file_data->curscene;
  }

  /* Keep state from preferences. */
  const int fileflags_keep = G_FILE_FLAG_ALL_RUNTIME;
  G.fileflags = (G.fileflags & fileflags_keep) | (bfd->fileflags & ~fileflags_keep);

  /* this can happen when active scene was lib-linked, and doesn't exist anymore */
  if (CTX_data_scene(C) == NULL) {
    wmWindow *win = CTX_wm_window(C);

    /* in case we don't even have a local scene, add one */
    if (!dunemain->scenes.first) {
      KERNEL_scene_add(dunemain, "Empty");
    }

    CTX_data_scene_set(C, dunemain->scenes.first);
    win->scene = CTX_data_scene(C);
    curscene = CTX_data_scene(C);
  }

  LIB_assert(curscene == CTX_data_scene(C));

  /* special cases, override loaded flags: */
  if (G.f != dune_file_data->globalf) {
    const int flags_keep = G_FLAG_ALL_RUNTIME;
    dune_file_data->globalf &= G_FLAG_ALL_READFILE;
    dune_file_data->globalf = (dune_file_data->globalf & ~flags_keep) | (G.f & flags_keep);
  }

  G.f = dune_file_data->globalf;

#ifdef WITH_PYTHON
  /* let python know about new main */
  if (CTX_py_init_get(C)) {
    BPY_context_update(C);
  }
#endif

  /* FIXME: this version patching should really be part of the file-reading code,
   * but we still get too many unrelated data-corruption crashes otherwise... */
  if (dunemain->versionfile < 250) {
    do_versions_ipos_to_animato(bmain);
  }

  /* NOTE: readfile's `do_version` does not allow to create new IDs, and only operates on a single
   * library at a time. This code needs to operate on the whole Main at once. */
  /* NOTE: Check dunemain version (i.e. current dune file version), AND the versions of all the
   * linked libraries. */
  if (mode != LOAD_UNDO && !dunefile_or_libraries_versions_atleast(dunemain, 302, 1)) {
    KERNEL_lib_override_library_main_proxy_convert(dunemain, reports);
  }

  if (mode != LOAD_UNDO && !dunefile_or_libraries_versions_atleast(dunemain, 302, 3)) {
    KERNEL_lib_override_library_main_hierarchy_root_ensure(dunemain);
  }

  dunemain->recovered = 0;

  /* startup.dune or recovered startup */
  if (is_startup) {
    dunemain->filepath[0] = '\0';
  }
  else if (recover) {
    /* In case of autosave or quit.dune, use original filepath instead. */
    dunemain->recovered = 1;
    STRNCPY(dunemain->filepath, dune_file_data->filepath);
  }

  /* baseflags, groups, make depsgraph, etc */
  /* first handle case if other windows have different scenes visible */
  if (mode == LOAD_UI) {
    wmWindowManager *wm = dunemain->wm.first;

    if (wm) {
      LISTBASE_FOREACH (wmWindow *, win, &wm->windows) {
        if (win->scene && win->scene != curscene) {
          KERNEL_scene_set_background(dunemain, win->scene);
        }
      }
    }
  }

  /* Setting scene might require having a dependency graph, with copy on write
   * we need to make sure we ensure scene has correct color management before
   * constructing dependency graph.
   */
  if (mode != LOAD_UNDO) {
    IMB_colormanagement_check_file_config(dunemain);
  }

  KERNEL_scene_set_background(dunemain, curscene);

  if (mode != LOAD_UNDO) {
    /* TODO(sergey): Can this be also move above? */
    RE_FreeAllPersistentData();
  }

  if (mode == LOAD_UNDO) {
    /* In undo/redo case, we do a whole lot of magic tricks to avoid having to re-read linked
     * data-blocks from libraries (since those are not supposed to change). Unfortunately, that
     * means that we do not reset their user count, however we do increase that one when doing
     * lib_link on local IDs using linked ones.
     * There is no real way to predict amount of changes here, so we have to fully redo
     * refcounting.
     * Now that we re-use (and do not liblink in readfile.c) most local datablocks as well, we have
     * to recompute refcount for all local IDs too. */
    KERNEL_main_id_refcount_recompute(dunemain, false);
  }

  if (mode != LOAD_UNDO && !USER_EXPERIMENTAL_TEST(&U, no_override_auto_resync)) {
    reports->duration.lib_overrides_resync = PIL_check_seconds_timer();

    KERNEL_lib_override_library_main_resync(
        dunemain,
        curscene,
        dune_file_data->cur_view_layer ? dune_file_data->cur_view_layer : KERNEL_view_layer_default_view(curscene),
        reports);

    reports->duration.lib_overrides_resync = PIL_check_seconds_timer() -
                                             reports->duration.lib_overrides_resync;

    /* We need to rebuild some of the deleted override rules (for UI feedback purpose). */
    KERNEL_lib_override_library_main_operations_create(dunemain, true);
  }
}

static void setup_app_dune_file_data(duneContext *C,
                                      DuneFileData *dune_file_data,
                                      const struct DuneFileReadParams *params,
                                      DuneFileReadReport *reports)
{
  if ((params->skip_flags & LOADER_READ_SKIP_USERDEF) == 0) {
    setup_app_userdef(dune_file_data);
  }
  if ((params->skip_flags & LOADER_READ_SKIP_DATA) == 0) {
    setup_app_data(C, dune_file_data, params, reports);
  }
}

static void handle_subversion_warning(Main *main, DuneFileReadReport *reports)
{
  if (main->minversionfile > DUNE_FILE_VERSION ||
      (main->minversionfile == DUNE_FILE_VERSION &&
       main->minsubversionfile > DUNE_FILE_SUBVERSION)) {
    KERNEL_reportf(reports->reports,
                RPT_ERROR,
                "File written by newer Dune binary (%d.%d), expect loss of data!",
                main->minversionfile,
                main->minsubversionfile);
  }
}

void KERNEL_dunefile_read_setup_ex(duneContext *C,
                                 DuneFileData *dune_file_data,
                                 const struct DuneFileReadParams *params,
                                 DuneFileReadReport *reports,
                                 /* Extra args. */
                                 const bool startup_update_defaults,
                                 const char *startup_app_template)
{
  if (startup_update_defaults) {
    if ((params->skip_flags & LOADER_READ_SKIP_DATA) == 0) {
      LOADER_update_defaults_startup_dune(dune_file_data->main, startup_app_template);
    }
  }
  setup_app_dune_file_data(C, dune_file_data, params, reports);
  LOADER_dunefiledata_free(dune_file_data);
}

void KERNEL_dunefile_read_setup(duneContext *C,
                              DuneFileData *dune_file_data,
                              const struct DuneFileReadParams *params,
                              DuneFileReadReport *reports)
{
  KERNEL_dunefile_read_setup_ex(C, dune_file_data, params, reports, false, NULL);
}

struct DuneFileData *KERNEL_dunefile_read(const char *filepath,
                                         const struct DuneFileReadParams *params,
                                         DuneFileReadReport *reports)
{
  /* Don't print startup file loading. */
  if (params->is_startup == false) {
    printf("Read dune: %s\n", filepath);
  }

  DuneFileData *dune_file_data = LOADER_read_from_file(filepath, params->skip_flags, reports);
  if (bfd) {
    handle_subversion_warning(dune_file_data->main, reports);
  }
  else {
    KERNEL_reports_prependf(reports->reports, "Loading '%s' failed: ", filepath);
  }
  return dune_file_data;
}

struct DuneFileData *KERNEL_dunefile_read_from_memory(const void *filebuf,
                                                     int filelength,
                                                     const struct DuneFileReadParams *params,
                                                     ReportList *reports)
{
  DuneFileData *dune_file_data = LOADER_read_from_memory(filebuf, filelength, params->skip_flags, reports);
  if (dune_file_data) {
    /* Pass. */
  }
  else {
    KERNEL_reports_prepend(reports, "Loading failed: ");
  }
  return dune_file_data;
}

struct DuneFileData *KERNEL_dunefile_read_from_memfile(Main *dunemain,
                                                      struct MemFile *memfile,
                                                      const struct DuneFileReadParams *params,
                                                      ReportList *reports)
{
  DuneFileData *dune_file_data = LOADER_read_from_memfile(
      dunemain, KERNEL_main_dunefile_path(dunemain), memfile, params, reports);
  if (dune_file_data) {
    /* Removing the unused workspaces, screens and wm is useless here, setup_app_data will switch
     * those lists with the ones from old dunemain, which freeing is much more efficient than
     * individual calls to `KERNEL_id_free()`.
     * Further more, those are expected to be empty anyway with new memfile reading code. */
    LIB_assert(LIB_listbase_is_empty(&dune_file_data->main->wm));
    LIB_assert(LIB_listbase_is_empty(&dune_file_data->main->workspaces));
    LIB_assert(LIB_listbase_is_empty(&dune_file_data->main->screens));
  }
  else {
    KERNEL_reports_prepend(reports, "Loading failed: ");
  }
  return dune_file_data;
}

void KERNEL_dunefile_read_make_empty(duneContext *C)
{
  Main *dunemain = CTX_data_main(C);
  ListBase *lb;
  ID *id;

  FOREACH_MAIN_LISTBASE_BEGIN (dunemain, lb) {
    FOREACH_MAIN_LISTBASE_ID_BEGIN (lb, id) {
      if (ELEM(GS(id->name), ID_SCE, ID_SCR, ID_WM, ID_WS)) {
        break;
      }
      KERNEL_id_delete(dunemain, id);
    }
    FOREACH_MAIN_LISTBASE_ID_END;
  }
  FOREACH_MAIN_LISTBASE_END;
}

UserDef *KERNEL_dunefile_userdef_read(const char *filepath, ReportList *reports)
{
  DuneFileData *dune_file_data;
  UserDef *userdef = NULL;

  dune_file_data = LOADER_read_from_file(filepath,
                           LOADER_READ_SKIP_ALL & ~LOADER_READ_SKIP_USERDEF,
                           &(struct DuneFileReadReport){.reports = reports});
  if (dune_file_data) {
    if (dune_file_data->user) {
      userdef = dune_file_data->user;
    }
    KERNEL_main_free(dune_file_data->main);
    MEM_freeN(dune_file_data);
  }

  return userdef;
}

UserDef *KERNEL_dunefile_userdef_read_from_memory(const void *filebuf,
                                                int filelength,
                                                ReportList *reports)
{
  DuneFileData *dune_data_file;
  UserDef *userdef = NULL;

  dune_data_file = LOADER_read_from_memory(
      filebuf, filelength, LOADER_READ_SKIP_ALL & ~LOADER_READ_SKIP_USERDEF, reports);
  if (dune_data_file) {
    if (dune_data_file->user) {
      userdef = dune_data_file->user;
    }
    KERNEL_main_free(dune_data_file->main);
    MEM_freeN(dune_data_file);
  }
  else {
    KERNEL_reports_prepend(reports, "Loading failed: ");
  }

  return userdef;
}

UserDef *KERNEL_dunefile_userdef_from_defaults(void)
{
  UserDef *userdef = MEM_mallocN(sizeof(*userdef), __func__);
  memcpy(userdef, &U_default, sizeof(*userdef));

  /* Add-ons. */
  {
    const char *addons[] = {
        "io_anim_bvh",
        "io_curve_svg",
        "io_mesh_ply",
        "io_mesh_stl",
        "io_mesh_uv_layout",
        "io_scene_fbx",
        "io_scene_gltf2",
        "io_scene_obj",
        "io_scene_x3d",
        "cycles",
        "pose_library",
    };
    for (int i = 0; i < ARRAY_SIZE(addons); i++) {
      duneAddon *addon = KERNEL_addon_new();
      STRNCPY(addon->module, addons[i]);
      LIB_addtail(&userdef->addons, addon);
    }
  }

  /* Theme. */
  {
    duneTheme *dunetheme = MEM_mallocN(sizeof(*dunetheme), __func__);
    memcpy(dunetheme, &U_theme_default, sizeof(*dunetheme));

    LIB_addtail(&userdef->themes, dunetheme);
  }

#ifdef WITH_PYTHON_SECURITY
  /* use alternative setting for security nuts
   * otherwise we'd need to patch the binary blob - startup.blend.c */
  userdef->flag |= USER_SCRIPT_AUTOEXEC_DISABLE;
#else
  userdef->flag &= ~USER_SCRIPT_AUTOEXEC_DISABLE;
#endif

  /* System-specific fonts directory. */
  KERNEL_appdir_font_folder_default(userdef->fontdir);

  userdef->memcachelimit = min_ii(LIB_system_memory_max_in_megabytes_int() / 2,
                                  userdef->memcachelimit);

  /* Init weight paint range. */
  KERNEL_colorband_init(&userdef->coba_weight, true);

  /* Default studio light. */
  KERNEL_studiolight_default(userdef->light_param, userdef->light_ambient);

  KERNEL_preferences_asset_library_default_add(userdef);

  return userdef;
}

bool KERNEL_dunefile_userdef_write(const char *filepath, ReportList *reports)
{
  Main *main = MEM_callocN(sizeof(Main), "empty main");
  bool ok = false;

  if (LOADER_write_file(main,
                     filepath,
                     0,
                     &(const struct DuneFileWriteParams){
                         .use_userdef = true,
                     },
                     reports)) {
    ok = true;
  }

  MEM_freeN(main);

  return ok;
}

bool KERNEL_dunefile_userdef_write_app_template(const char *filepath, ReportList *reports)
{
  /* if it fails, overwrite is OK. */
  UserDef *userdef_default = KERNEL_dunefile_userdef_read(filepath, NULL);
  if (userdef_default == NULL) {
    return KERNEL_dunefile_userdef_write(filepath, reports);
  }

  KERNEL_dune_userdef_app_template_data_swap(&U, userdef_default);
  bool ok = KERNEL_dunefile_userdef_write(filepath, reports);
  KERNEL_dune_userdef_app_template_data_swap(&U, userdef_default);
  KERNEL_dune_userdef_data_free(userdef_default, false);
  MEM_freeN(userdef_default);
  return ok;
}

bool KERNEL_dunefile_userdef_write_all(ReportList *reports)
{
  char filepath[FILE_MAX];
  const char *cfgdir;
  bool ok = true;
  const bool use_template_userpref = BKE_appdir_app_template_has_userpref(U.app_template);

  if ((cfgdir = KERNEL_appdir_folder_id_create(DUNE_USER_CONFIG, NULL))) {
    bool ok_write;
    LIB_path_join(filepath, sizeof(filepath), cfgdir, DUNE_USERPREF_FILE, NULL);

    printf("Writing userprefs: '%s' ", filepath);
    if (use_template_userpref) {
      ok_write = KERNEL_dunefile_userdef_write_app_template(filepath, reports);
    }
    else {
      ok_write = KERNEL_dunefile_userdef_write(filepath, reports);
    }

    if (ok_write) {
      printf("ok\n");
      KERNEL_report(reports, RPT_INFO, "Preferences saved");
    }
    else {
      printf("fail\n");
      ok = false;
      KERNEL_report(reports, RPT_ERROR, "Saving preferences failed");
    }
  }
  else {
    KERNEL_report(reports, RPT_ERROR, "Unable to create userpref path");
  }

  if (use_template_userpref) {
    if ((cfgdir = KERNEL_appdir_folder_id_create(DUNE_USER_CONFIG, U.app_template))) {
      /* Also save app-template prefs */
      LIB_path_join(filepath, sizeof(filepath), cfgdir, DUNE_USERPREF_FILE, NULL);

      printf("Writing userprefs app-template: '%s' ", filepath);
      if (KERNEL_dunefile_userdef_write(filepath, reports) != 0) {
        printf("ok\n");
      }
      else {
        printf("fail\n");
        ok = false;
      }
    }
    else {
      KERNEL_report(reports, RPT_ERROR, "Unable to create app-template userpref path");
      ok = false;
    }
  }

  if (ok) {
    U.runtime.is_dirty = false;
  }
  return ok;
}

WorkspaceConfigFileData *KERNEL_dunefile_workspace_config_read(const char *filepath,
                                                             const void *filebuf,
                                                             int filelength,
                                                             ReportList *reports)
{
  DuneFileData *dune_file_data;
  WorkspaceConfigFileData *workspace_config = NULL;

  if (filepath) {
    dune_file_data = LOADER_read_from_file(
        filepath, LOADER_READ_SKIP_USERDEF, &(struct DuneFileReadReport){.reports = reports});
  }
  else {
    dune_file_data = LOADER_read_from_memory(filebuf, filelength, LOADER_READ_SKIP_USERDEF, reports);
  }

  if (dune_file_data) {
    workspace_config = MEM_callocN(sizeof(*workspace_config), __func__);
    workspace_config->main = dune_file_data->main;

    /* Only 2.80+ files have actual workspaces, don't try to use screens
     * from older versions. */
    if (dune_file_data->main->versionfile >= 280) {
      workspace_config->workspaces = dune_file_data->main->workspaces;
    }

    MEM_freeN(dune_file_data);
  }

  return workspace_config;
}

bool KERNEL_dunefile_workspace_config_write(Main *dunemain, const char *filepath, ReportList *reports)
{
  const int fileflags = G.fileflags & ~G_FILE_NO_UI;
  bool retval = false;

  KERNEL_dunefile_write_partial_begin(dunemain);

  for (WorkSpace *workspace = dunemain->workspaces.first; workspace; workspace = workspace->id.next) {
    KERNEL_dunefile_write_partial_tag_ID(&workspace->id, true);
  }

  if (KERNEL_dunefile_write_partial(
          dunemain, filepath, fileflags, LOADER_WRITE_PATH_REMAP_NONE, reports)) {
    retval = true;
  }

  KERNEL_dunefile_write_partial_end(bmain);

  return retval;
}

void KERNEL_dunefile_workspace_config_data_free(WorkspaceConfigFileData *workspace_config)
{
  KERNEL_main_free(workspace_config->main);
  MEM_freeN(workspace_config);
}

/* -------------------------------------------------------------------- */
/** Partial `.dune` file save. **/

void KERNEL_dunefile_write_partial_begin(Main *dunemain_src)
{
  KERNEL_main_id_tag_all(dunemain_src, LIB_TAG_NEED_EXPAND | LIB_TAG_DOIT, false);
}

void KERNEL_dunefile_write_partial_tag_ID(ID *id, bool set)
{
  if (set) {
    id->tag |= LIB_TAG_NEED_EXPAND | LIB_TAG_DOIT;
  }
  else {
    id->tag &= ~(LIB_TAG_NEED_EXPAND | LIB_TAG_DOIT);
  }
}

static void dunefile_write_partial_cb(void *UNUSED(handle), Main *UNUSED(bmain), void *vid)
{
  if (vid) {
    ID *id = vid;
    /* only tag for need-expand if not done, prevents eternal loops */
    if ((id->tag & LIB_TAG_DOIT) == 0) {
      id->tag |= LIB_TAG_NEED_EXPAND | LIB_TAG_DOIT;
    }

    if (id->lib && (id->lib->id.tag & LIB_TAG_DOIT) == 0) {
      id->lib->id.tag |= LIB_TAG_DOIT;
    }
  }
}

bool KERNEL_dunefile_write_partial(Main *dunemain_src,
                                 const char *filepath,
                                 const int write_flags,
                                 const int remap_mode,
                                 ReportList *reports)
{
  Main *dunemain_dst = MEM_callocN(sizeof(Main), "copybuffer");
  ListBase *lbarray_dst[INDEX_ID_MAX], *lbarray_src[INDEX_ID_MAX];
  int a, retval;

  void *path_list_backup = NULL;
  const eBPathForeachFlag path_list_flag = (KERNEL_DUNEPATH_FOREACH_PATH_SKIP_LINKED |
                                            KERNEL_DUNEPATH_FOREACH_PATH_SKIP_MULTIFILE);

  /* This is needed to be able to load that file as a real one later
   * (otherwise `main->filepath` will not be set at read time). */
  STRNCPY(dunemain_dst->filepath, dunemain_src->filepath);

  LOADER_main_expander(dunefile_write_partial_cb);
  LOADER_expand_main(NULL, dunemain_src);

  /* move over all tagged blocks */
  set_listbasepointers(dunemain_src, lbarray_src);
  a = set_listbasepointers(dunemain_dst, lbarray_dst);
  while (a--) {
    ID *id, *nextid;
    ListBase *lb_dst = lbarray_dst[a], *lb_src = lbarray_src[a];

    for (id = lb_src->first; id; id = nextid) {
      nextid = id->next;
      if (id->tag & LIB_TAG_DOIT) {
        LIB_remlink(lb_src, id);
        LIB_addtail(lb_dst, id);
      }
    }
  }

  /* Backup paths because remap relative will overwrite them.
   *
   * NOTE: we do this only on the list of data-blocks that we are writing
   * because the restored full list is not guaranteed to be in the same
   * order as before, as expected by KERNEL_dunepath_list_restore.
   *
   * This happens because id_sort_by_name does not take into account
   * string case or the library name, so the order is not strictly
   * defined for two linked data-blocks with the same name! */
  if (remap_mode != LOADER_WRITE_PATH_REMAP_NONE) {
    path_list_backup = KERNEL_bpath_list_backup(dunemain_dst, path_list_flag);
  }

  /* save the buffer */
  retval = LOADER_write_file(dunemain_dst,
                          filepath,
                          write_flags,
                          &(const struct DuneFileWriteParams){
                              .remap_mode = remap_mode,
                          },
                          reports);

  if (path_list_backup) {
    KERNEL_dunepath_list_restore(dunemain_dst, path_list_flag, path_list_backup);
    KERNEL_dunepath_list_free(path_list_backup);
  }

  /* move back the main, now sorted again */
  set_listbasepointers(dunemain_src, lbarray_dst);
  a = set_listbasepointers(dunemain_dst, lbarray_src);
  while (a--) {
    ID *id;
    ListBase *lb_dst = lbarray_dst[a], *lb_src = lbarray_src[a];

    while ((id = LIB_pophead(lb_src))) {
      LIB_addtail(lb_dst, id);
      id_sort_by_name(lb_dst, id, NULL);
    }
  }

  MEM_freeN(dunemain_dst);

  return retval;
}

void KERNEL_dunefile_write_partial_end(Main *dunemain_src)
{
  KERNEL_main_id_tag_all(dunemain_src, LIB_TAG_NEED_EXPAND | LIB_TAG_DOIT, false);
}
