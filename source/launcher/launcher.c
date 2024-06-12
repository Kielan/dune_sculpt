#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#  include "utfconv.h"
#  include <windows.h>
#endif

#include "mem_guardedalloc.h"

#include "log.h"

#include "types_genfile.h"

#include "lib_args.h"
#include "lib_string.h"
#include "lib_system.h"
#include "lib_task.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

/* Mostly initialization functions. */
#include "dune_appdir.h"
#include "dune.h"
#include "dune_brush.h"
#include "dune_cachefile.h"
#include "dune_callbacks.h"
#include "dune_context.h"
#include "dune_global.h"
#include "dune_pen_modifier.h"
#include "dune_idtype.h"
#include "dune_image.h"
#include "dune_main.h"
#include "dune_material.h"
#include "dune_modifier.h"
#include "dune_node.h"
#include "dune_particle.h"
#include "dune_shader_fx.h"
#include "dune_sound.h"
#include "dune_vfont.h"
#include "dune_volume.h"

#include "graph.h"

#include "IMB_imbuf.h" /* For #IMB_init. */

#include "render_engine.h"
#include "render_texture.h"

#include "ed_datafiles.h"

#include "wm_api.h"
#include "wm_toolsystem.h"

#include "api_define.h"

#ifdef WITH_FREESTYLE
#  include "FRS_freestyle.h"
#endif

#include <signal.h>

#ifdef __FreeBSD__
#  include <floatingpoint.h>
#endif

#ifdef WITH_BINRELOC
#  include "binreloc.h"
#endif

#ifdef WITH_LIBMV
#  include "libmv-capi.h"
#endif

#ifdef WITH_SDL_DYNLOAD
#  include "sdlew.h"
#endif

#include "launcher_intern.h" /* Own include. */

/* -------------------------------------------------------------------- */
/** Local Application State **/

/* written to by 'launcher_args.c' */
struct ApplicationState app_state = {
    .signal =
        {
            .use_crash_handler = true,
            .use_abort_handler = true,
        },
    .exit_code_on_error =
        {
            .python = 0,
        },
};

/* -------------------------------------------------------------------- */
/** Application Level Callbacks
 * Initialize callbacks for the modules that need them. **/

static void cb_mem_error(const char *errorStr)
{
  fputs(errorStr, stderr);
  fflush(stderr);
}

static void main_cb_setup(void)
{
  /* Error output from the guarded allocation routines. */
  mem_set_error_cb(cb_mem_error);
}

/* free data on early exit (if Python calls 'sys.exit()' while parsing args for eg). */
struct CreatorAtExitData {
  Args *args;
#ifdef WIN32
  const char **argv;
  int argv_num;
#endif
};

static void cb_main_atexit(void *user_data)
{
  struct CreatorAtExitData *app_init_data = user_data;

  if (app_init_data->args) {
    lib_args_destroy(app_init_data->args);
    app_init_data->args = NULL;
  }

#ifdef WIN32
  if (app_init_data->argv) {
    while (app_init_data->argv_num) {
      free((void *)app_init_data->argv[--app_init_data->argv_num]);
    }
    free((void *)app_init_data->argv);
    app_init_data->argv = NULL;
  }
#endif
}

static void cb_log_fatal(void *fp)
{
  lib_system_backtrace(fp);
}

/* -------------------------------------------------------------------- */
/** GMP Allocator Workaround */

void *gmp_alloc(size_t size)
{
  return scalable_malloc(size);
}
void *gmp_realloc(void *ptr, size_t old_size, size_t new_size)
{
  return scalable_realloc(ptr, new_size);
}

void gmp_free(void *ptr, size_t size)
{
  scalable_free(ptr);
}
/**
 * Use TBB's scalable_allocator on Windows.
 * `TBBmalloc` correctly captures all allocations already,
 * however, GMP is built with MINGW since it doesn't build with MSVC,
 * which TBB has issues hooking into automatically.
 */
void gmp_dune_init_allocator()
{
  mp_set_memory_fns(gmp_alloc, gmp_realloc, gmp_free);
}
#endif

/* -------------------------------------------------------------------- */
/** Main Function */

/**
 * Dune's main function responsibilities are:
 * - setup subsystems.
 * - handle arguments.
 * - run wm_main() event loop,
 *   or exit immediately when running in background-mode.
 */
int main(int argc,
#ifdef WIN32
         const char **UNUSED(argv_c)
#else
         const char **argv
#endif
)
{
  Ctx *C;

  Args *args;

#ifdef WIN32
  char **argv;
  int argv_num;
#endif

  /* --- end declarations --- */

  /* Ensure we free data on early-exit. */
  struct CreatorAtExitData app_init_data = {NULL};
  dune_atexit_register(cb_main_atexit, &app_init_data);

  /* Un-buffered `stdout` makes `stdout` and `stderr` better synchronized, and helps
   * when stepping through code in a debugger (prints are immediately
   * visible). However disabling buffering causes lock contention on windows
   * see T76767 for details, since this is a debugging aid, we do not enable
   * the un-buffered behavior for release builds. */
#ifndef NDEBUG
  setvbuf(stdout, NULL, _IONBF, 0);
#endif

#ifdef WIN32
  /* We delay loading of OPENMP so we can set the policy here. */
#  if defined(_MSC_VER)
  _putenv_s("OMP_WAIT_POLICY", "PASSIVE");
#  endif

  /* Win32 Unicode Arguments. */
  /* NOTE: cannot use `guardedalloc` allocation here, as it's not yet initialized
   *       (it depends on the arguments passed in, which is what we're getting here!)
   */
  {
    wchar_t **argv_16 = CommandLineToArgvW(GetCommandLineW(), &argc);
    argv = malloc(argc * sizeof(char *));
    for (argv_num = 0; argv_num < argc; argv_num++) {
      argv[argv_num] = alloc_utf_8_from_16(argv_16[argv_num], 0);
    }
    LocalFree(argv_16);

    /* free on early-exit */
    app_init_data.argv = argv;
    app_init_data.argv_num = argv_num;
  }
#endif /* WIN32 */

  /* NOTE: Special exception for guarded allocator type switch:
   *       we need to perform switch from lock-free to fully
   *       guarded allocator before any allocation happened.
   */
  {
    int i;
    for (i = 0; i < argc; i++) {
      if (STR_ELEM(argv[i], "-d", "--debug", "--debug-memory", "--debug-all")) {
        printf("Switching to fully guarded memory allocator.\n");
        mem_use_guarded_allocator();
        break;
      }
      if (STREQ(argv[i], "--")) {
        break;
      }
    }
    mem_init_memleak_detection();
  }

#ifdef BUILD_DATE
  {
    time_t temp_time = build_commit_timestamp;
    struct tm *tm = gmtime(&temp_time);
    if (LIKELY(tm)) {
      strftime(build_commit_date, sizeof(build_commit_date), "%Y-%m-%d", tm);
      strftime(build_commit_time, sizeof(build_commit_time), "%H:%M", tm);
    }
    else {
      const char *unknown = "date-unknown";
      lib_strncpy(build_commit_date, unknown, sizeof(build_commit_date));
      lib_strncpy(build_commit_time, unknown, sizeof(build_commit_time));
    }
  }
#endif

#ifdef WITH_SDL_DYNLOAD
  sdlewInit();
#endif

  /* Initialize logging. */
  log_init();
  log_fatal_fn_set(cb_log_fatal);

  C = ctx_create();

#ifdef WITH_BINRELOC
  br_init(NULL);
#endif

#ifdef WITH_LIBMV
  libmv_initLogging(argv[0]);
#elif defined(WITH_CYCLES_LOGGING)
  CCL_init_logging(argv[0]);
#endif

  main_cb_setup();

#if defined(__APPLE__) && !defined(WITH_PYTHON_MODULE) && !defined(WITH_HEADLESS)
  /* Patch to ignore argument finder gives us (PID?) */
  if (argc == 2 && STRPREFIX(argv[1], "-psn_")) {
    extern int GHOST_HACK_getFirstFile(char buf[]);
    static char firstfilebuf[512];

    argc = 1;

    if (GHOST_HACK_getFirstFile(firstfilebuf)) {
      argc = 2;
      argv[1] = firstfilebuf;
    }
  }
#endif

#ifdef __FreeBSD__
  fpsetmask(0);
#endif

  /* Initialize path to executable. */
  dune_appdir_program_path_init(argv[0]);

  lib_threadapi_init();

  STRUCTS_types_current_init();

  dune_globals_init(); /* dune.c */

  dune_idtype_init();
  dune_cachefiles_init();
  dune_mod_init();
  dune_pen_mod_init();
  dune_shaderfx_init();
  dune_volumes_init();
  graph_register_node_types();

  dune_brush_system_init();
  render_texture_rng_init();

  dune_cb_global_init();

  /* First test for background-mode (Global.background) */
#ifndef WITH_PYTHON_MODULE
  args = lib_args_create(argc, (const char **)argv); /* skip binary path */

  /* Ensure we free on early exit. */
  app_init_data.args = args;

  main_args_setup(C, args);

  /* Begin argument parsing, ignore leaks so arguments that call #exit
   * (such as '--version' & '--help') don't report leaks. */
  mem_use_memleak_detection(false);

  /* Parse environment handling arguments. */
  lib_args_parse(ba, ARG_PASS_ENVIRONMENT, NULL, NULL);

#else
  /* Using preferences or user startup makes no sense for #WITH_PYTHON_MODULE. */
  G.factory_startup = true;
#endif

  /* After parsing ARG_PASS_ENVIRONMENT such as `--env-*`,
   * since they impact `dune_appdir` behavior. */
  dune_appdir_init();

  /* After parsing number of threads argument. */
  lib_task_scheduler_init();

  /* Initialize sub-systems that use `dune_appdir.h`. */
  imbuf_init();

  /* First test for background-mode (Global.background) */
  lib_args_parse(args, ARG_PASS_SETTINGS, NULL, NULL);

  main_signal_setup();

#ifdef WITH_FFMPEG
  /* Keep after ARG_PASS_SETTINGS since debug flags are checked. */
  imbuf_ffmpeg_init();
#endif

  /* After ARG_PASS_SETTINGS arguments, this is so win_main_playanim skips #RNA_init. */
  api_init();

  render_engines_init();
  dune_node_system_init();
  dune_particle_init_rng();
  /* End second initialization. */

  /* Background render uses this font too. */
  dune_vfont_builtin_register(datatoc_bfont_pfb, datatoc_bfont_pfb_size);

  /* Initialize FFMPEG if built in, also needed for background-mode if videos are
   * rendered via FFMPEG. */
  dune_sound_init_once();

  dune_materials_init();

  if (G.background == 0) {
    lib_args_parse(ba, ARG_PASS_SETTINGS_GUI, NULL, NULL);
  }
  lib_args_parse(ba, ARG_PASS_SETTINGS_FORCE, NULL, NULL);

  wm_init(C, argc, (const char **)argv);

  /* Need to be after WM init so that userpref are loaded. */
  render_engines_init_experimental();

  cxt_py_init_set(C, true);
  wm_keyconfig_init(C);

#ifdef WITH_FREESTYLE
  /* Initialize Freestyle. */
  FRS_init();
  FRS_set_context(C);
#endif

  /* Handles #ARG_PASS_FINAL. */
  main_args_setup_post(C, ba);

  /* Explicitly free data allocated for argument parsing:
   * - 'ba'
   * - 'argv' on WIN32 */
  cb_main_atexit(&app_init_data);
  dune_atexit_unregister(cb_main_atexit, &app_init_data);

  /* End argument parsing, allow memory leaks to be printed. */
  mem_use_memleak_detection(true);

  /* Paranoid, avoid accidental re-use. */
  args = NULL;
  (void)args;

#ifdef WIN32
  argv = NULL;
  (void)argv;
#endif

  if (G.background) {
    /* Using window-manager API in background-mode is a bit odd, but works fine. */
    wm_exit(C);
  }
  else {
    /* When no file is loaded, show the splash screen. */
    const char *dunefile_path = dune_main_dunefile_path_from_global();
    if (dunefile_path[0] == '\0') {
      wm_init_splash(C);
    }
    wm_main(C);
  }

  return 0;
} /* End of `int main(...)` function. */
