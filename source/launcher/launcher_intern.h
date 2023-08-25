#pragma once

/* launcher
 * Fn for main() initialization. */

struct duneArgs;
struct duneCxt;

/* launcher_args.c */
void main_args_setup(struct Cxt *C, struct Args *args);
void main_args_setup_post(struct Cxt *C, struct Args *args);

/* launcher_signals.c */
void main_signal_setup(void);
void main_signal_setup_background(void);
void main_signal_setup_fpe(void);

/* Shared data for argument handlers to store state in. */
struct ApplicationState {
  struct {
    bool use_crash_handler;
    bool use_abort_handler;
  } signal;

  /* we may want to set different exit codes for other kinds of errors */
  struct {
    unsigned char python;
  } exit_code_on_error;
};
extern struct ApplicationState app_state; /* creator.c */

/* Passes for use by main_args_setup.
 * Keep in order of execution */
enum {
  /* Run before sub-system initialization. */
  ARG_PASS_ENVIRONMENT = 1,
  /* General settings parsing, also animation player. */
  ARG_PASS_SETTINGS = 2,
  /* Windowing & graphical settings (ignored in background mode). */
  ARG_PASS_SETTINGS_GUI = 3,
  /* Currently use for audio devices. */
  ARG_PASS_SETTINGS_FORCE = 4,

  /* Actions & fall back to loading blend file. */
  ARG_PASS_FINAL = 5,
};

/* for the callbacks: */
#  define DUNE_VERSION_FMT "Dune %d.%d.%d"
#  define DUNE_VERSION_ARG (DUNE_VERSION / 100), (DUNE_VERSION % 100), DUNE_VERSION_PATCH

#ifdef WITH_BUILDINFO_HEADER
#  define BUILD_DATE
#endif

/* from buildinfo.c */
#ifdef BUILD_DATE
extern char build_date[];
extern char build_time[];
extern char build_hash[];
extern unsigned long build_commit_timestamp;

/* TODO: ideally size need to be in sync with buildinfo.c */
extern char build_commit_date[16];
extern char build_commit_time[16];

extern char build_branch[];
extern char build_platform[];
extern char build_type[];
extern char build_cflags[];
extern char build_cxxflags[];
extern char build_linkflags[];
extern char build_system[];
#endif /* BUILD_DATE */
