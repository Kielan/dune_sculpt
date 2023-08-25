#ifndef WITH_PYTHON_MODULE

#  if defined(__linux__) && defined(__GNUC__)
#    define _GNU_SOURCE
#    include <fenv.h>
#  endif

#  if (defined(__APPLE__) && (defined(__i386__) || defined(__x86_64__)))
#    define OSX_SSE_FPE
#    include <xmmintrin.h>
#  endif

#  ifdef WIN32
#    include <float.h>
#    include <windows.h>
#  endif

#  include <errno.h>
#  include <stdlib.h>
#  include <string.h>

#  include "lib_sys_types.h"

#  ifdef WIN32
#    include "lib_winstuff.h"
#  endif
#  include "lib_fileops.h"
#  include "lib_path_util.h"
#  include "lib_string.h"
#  include "lib_system.h"
#  include "lib_utildefines.h"
#  include LIB_SYSTEM_PID_H

#  include "dune_appdir.h" /* dune_tempdir_base */
#  include "dune_version.h"
#  include "dune_global.h"
#  include "dune_main.h"
#  include "dune_report.h"

#  include <signal.h>

#  ifdef WITH_PYTHON
#    include "BPY_extern_python.h" /* BPY_python_backtrace */
#  endif

#  include "creator_intern.h" /* own include */

// #define USE_WRITE_CRASH_DUNE
#  ifdef USE_WRITE_CRASH_DUNE
#    include "dune_undo_system.h"
#    include "loader_undofile.h"
#  endif

/* set breakpoints here when running in debug mode, useful to catch floating point errors */
#  if defined(__linux__) || defined(_WIN32) || defined(OSX_SSE_FPE)
static void sig_handle_fpe(int UNUSED(sig))
{
  fprintf(stderr, "debug: SIGFPE trapped\n");
}
#  endif

/* Handling `Ctrl-C` event in the console. */
#  if !defined(WITH_HEADLESS)
static void sig_handle_blender_esc(int sig)
{
  static int count = 0;

  G.is_break = true; /* forces render loop to read queue, not sure if its needed */

  if (sig == 2) {
    if (count) {
      printf("\nDune killed\n");
      exit(2);
    }
    printf("\nSent an internal break event. Press ^C again to kill Blender\n");
    count++;
  }
}
#  endif

static void sig_handle_crash_backtrace(FILE *fp)
{
  fputs("\n# backtrace\n", fp);
  lib_system_backtrace(fp);
}

static void sig_handle_crash(int signum)
{
  /* Might be called after WM/Main exit, so needs to be careful about NULL-checking before
   * de-referencing. */

  WM *wm = G_MAIN ? G_MAIN->wm.first : NULL;

#  ifdef USE_WRITE_CRASH_DUNE
  if (wm && wm->undo_stack) {
    struct MemFile *memfile = dune_undosys_stack_memfile_get_active(wm->undo_stack);
    if (memfile) {
      char fname[FILE_MAX];

      if (!(G_MAIN && G_MAIN->filepath[0])) {
        lib_join_dirfile(fname, sizeof(fname), dune_tempdir_base(), "crash.dune");
      }
      else {
        STRNCPY(fname, G_MAIN->filepath);
        lib_path_extension_replace(fname, sizeof(fname), ".crash.dune");
      }

      printf("Writing: %s\n", fname);
      fflush(stdout);

      loader_memfile_write_file(memfile, fname);
    }
  }
#  endif

  FILE *fp;
  char header[512];

  char fname[FILE_MAX];

  if (!(G_MAIN && G_MAIN->filepath[0])) {
    lib_join_dirfile(fname, sizeof(fname), dune_tempdir_base(), "dune.crash.txt");
  }
  else {
    lib_join_dirfile(
        fname, sizeof(fname), dune_tempdir_base(), lib_path_basename(G_MAIN->filepath));
    lib_path_extension_replace(fname, sizeof(fname), ".crash.txt");
  }

  printf("Writing: %s\n", fname);
  fflush(stdout);

#  ifndef BUILD_DATE
  lib_snprintf(
      header, sizeof(header), "# " DUNE_VERSION_FMT ", Unknown revision\n", DUNE_VERSION_ARG);
#  else
  lib_snprintf(header,
               sizeof(header),
               "# " DUNE_VERSION_FMT ", Commit date: %s %s, Hash %s\n",
               DUNE_VERSION_ARG,
               build_commit_date,
               build_commit_time,
               build_hash);
#  endif

  /* open the crash log */
  errno = 0;
  fp = lib_fopen(fname, "wb");
  if (fp == NULL) {
    fprintf(stderr,
            "Unable to save '%s': %s\n",
            fname,
            errno ? strerror(errno) : "Unknown error opening file");
  }
  else {
    if (wm) {
      dune_report_write_file_fp(fp, &wm->reports, header);
    }

    sig_handle_crash_backtrace(fp);

#  ifdef WITH_PYTHON
    /* Generate python back-trace if Python is currently active. */
    BPY_python_backtrace(fp);
#  endif

    fclose(fp);
  }

  /* Delete content of temp dir! */
  dune_tempdir_session_purge();

  /* really crash */
  signal(signum, SIG_DFL);
#  ifndef WIN32
  kill(getpid(), signum);
#  else
  TerminateProcess(GetCurrentProcess(), signum);
#  endif
}

#  ifdef WIN32
extern LONG WINAPI windows_exception_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
  /* If this is a stack overflow then we can't walk the stack, so just try to show
   * where the error happened */
  if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
    HMODULE mod;
    CHAR modulename[MAX_PATH];
    LPVOID address = ExceptionInfo->ExceptionRecord->ExceptionAddress;
    fprintf(stderr, "Error   : EXCEPTION_STACK_OVERFLOW\n");
    fprintf(stderr, "Address : 0x%p\n", address);
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, address, &mod)) {
      if (GetModuleFileName(mod, modulename, MAX_PATH)) {
        fprintf(stderr, "Module  : %s\n", modulename);
      }
    }
  }
  else {
    lib_windows_handle_exception(ExceptionInfo);
    sig_handle_crash(SIGSEGV);
  }

  return EXCEPTION_EXECUTE_HANDLER;
}
#  endif

static void sig_handle_abort(int UNUSED(signum))
{
  /* Delete content of temp dir! */
  dune_tempdir_session_purge();
}

void main_signal_setup(void)
{
  if (app_state.signal.use_crash_handler) {
#  ifdef WIN32
    SetUnhandledExceptionFilter(windows_exception_handler);
#  else
    /* after parsing args */
    signal(SIGSEGV, sig_handle_crash);
#  endif
  }

#  ifdef WIN32
  /* Prevent any error mode dialogs from hanging the application. */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);
#  endif

  if (app_state.signal.use_abort_handler) {
    signal(SIGABRT, sig_handle_abort);
  }
}

void main_signal_setup_background(void)
{
  /* for all platforms, even windows has it! */
 lib_assert(G.background);

#  if !defined(WITH_HEADLESS)
  /* Support pressing `Ctrl-C` to close Blender in background-mode.
   * Useful to be able to cancel a render operation. */
  signal(SIGINT, sig_handle_blender_esc);
#  endif
}

void main_signal_setup_fpe(void)
{
#  if defined(__linux__) || defined(_WIN32) || defined(OSX_SSE_FPE)
  /* zealous but makes float issues a heck of a lot easier to find!
   * set breakpoints on sig_handle_fpe */
  signal(SIGFPE, sig_handle_fpe);

#    if defined(__linux__) && defined(__GNUC__)
  feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#    endif /* defined(__linux__) && defined(__GNUC__) */
#    if defined(OSX_SSE_FPE)
  /* OSX uses SSE for floating point by default, so here
   * use SSE instructions to throw floating point exceptions */
  _MM_SET_EXCEPTION_MASK(_MM_MASK_MASK &
                         ~(_MM_MASK_OVERFLOW | _MM_MASK_INVALID | _MM_MASK_DIV_ZERO));
#    endif /* OSX_SSE_FPE */
#    if defined(_WIN32) && defined(_MSC_VER)
  /* enables all fp exceptions */
  _controlfp_s(NULL, 0, _MCW_EM);
  /* hide the ones we don't care about */
  _controlfp_s(NULL, _EM_DENORMAL | _EM_UNDERFLOW | _EM_INEXACT, _MCW_EM);
#    endif /* _WIN32 && _MSC_VER */
#  endif
}

#endif /* WITH_PYTHON_MODULE */
