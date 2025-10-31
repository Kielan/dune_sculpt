#pragma once

#include "s_list.h"
#include "s_screen_types.h" /* for #ScrAreaMap */
#include "s_xr_types.h"     /* for #XrSessionSettings */

#include "s_ID.h"

struct wmWindow;
struct wmWindowManager;

struct wmEvent;
struct wmGesture;
struct wmKeyConfig;
struct wmKeyMap;
struct wmMsgBus;
struct wmOperator;
struct wmOperatorType;

/* Forward declarations: */
struct PtrAPI;
struct Report;
struct ReportList;
struct Stereo3dFormat;
struct Cx;
struct Screen;
struct uiLayout;
struct wmTimer;

#define OP_MAX_TYPENAME 64
#define KMAP_MAX_NAME 64

/* Keep in sync w 'api_enum_wm_report_items' in `wm_api.c`. */
typedef enum eReportType {
  RPT_DEBUG = (1 << 0),
  RPT_INFO = (1 << 1),
  RPT_OP = (1 << 2),
  RPT_PROP = (1 << 3),
  RPT_WARNING = (1 << 4),
  RPT_ERR = (1 << 5),
  RPT_ERR_INVALID_INPUT = (1 << 6),
  RPT_ERR_INVALID_CX = (1 << 7),
  RPT_ERR_OUT_OF_MEM = (1 << 8),
} eReportType;

#define RPT_DEBUG_ALL (RPT_DEBUG)
#define RPT_INFO_ALL (RPT_INFO)
#define RPT_OP_ALL (RPT_OP)
#define RPT_PROP_ALL (RPT_PROP)
#define RPT_WARNING_ALL (RPT_WARNING)
#define RPT_ERR_ALL \
  (RPT_ERR | RPT_ERR_INVALID_INPUT | RPT_ERR_INVALID_CX | RPT_ERR_OUT_OF_MEM)

enum ReportListFlags {
  RPT_PRINT = (1 << 0),
  RPT_STORE = (1 << 1),
  RPT_FREE = (1 << 2),
  RPT_OP_HOLD = (1 << 3), /* don't move them into the op global list (caller will use) */
};

/* These 2 Lines with # tell makesdna this struct can be excluded. */
#
#
typedef struct Report {
  struct Report *next, *prev;
  /* eReportType. */
  short type;
  short flag;
  /* `strlen(message)`, saves some time calculating the word wrap. */
  int len;
  const char *typestr;
  const char *message;
} Report;

/* Saved in the wm, don't remove. */
typedef struct ReportList {
  List list;
  /* eReportType. */
  int printlvl;
  /* eReportType. */
  int storelvl;
  int flag;
  char _pad[4];
  struct wmTimer *reporttimer;
} ReportList;

/* timer customdata to ctrl reports display */
/* These 2 Lines w # tell makestypes this struct can be excluded. */
#
#
typedef struct ReportTimerInfo {
  float col[4];
  float widthfac;
} ReportTimerInfo;

//#ifdef WITH_XR_OPENXR
typedef struct wmXrData {
  /* Runtime info for managing Dune spec behaviors. */
  struct wmXrRuntimeData *runtime;
  /* Permanent session settings (draw mode, feat toggles, etc). Stored in files and accessible
   * even before the session runs. */
  XrSessionSettings session_settings;
} wmXrData;

/* Window-manager is saved, tag WMAN. */
typedef struct wmWindowManager {
  ID id;

  /* Separate active from drawable. */
  struct wmWindow *windrawable;
  /* `cx_wm_window(C)` is usually preferred.
   * Avoid relying on this where possible as this may become NULL during when handling
   * events that close or replace windows (opening a file for e.g.).
   * While this happens rarely in practice, it can cause difficult to reproduce bugs. */
  struct wmWindow *winactive;
  List windows;

  /* Set on file read. */
  short initialized;
  /* Indicator whether data was saved. */
  short file_saved;
  /* Op stack depth to avoid nested undo pushes. */
  short op_undo_depth;

  /* Set after selection to notify outliner to sync. Stores type of selection */
  short outliner_sync_select_dirty;

  /* Op registry. */
  List ops;

  /* Refresh/redraw #wmNotifier structs. */
  List notifier_queue;

  /* Info and error reports. */
  struct ReportList reports;

  /* Threaded jobs manager. */
  List jobs;

  /* Extra overlay cursors to draw, like circles. */
  List paintcursors;

  /* Active dragged items. */
  List drags;

  /* Known key configs. */
  List keyconfigs;
  /* Default config. */
  struct wmKeyConfig *defaultconf;
  /* Addon config. */
  struct wmKeyConfig *addonconf;
  /* User config. */
  struct wmKeyConfig *userconf;

  /* Active timers. */
  List timers;
  /* Timer for auto save. */
  struct wmTimer *autosavetimer;

  /* All undo history (runtime only). */
  struct UndoStack *undo_stack;

  /* Indicates whether interface is locked for user interaction. */
  char is_interface_locked;
  char _pad[7];

  struct wmMsgBus *msg_bus;

  //#ifdef WITH_XR_OPENXR
  wmXrData xr;
  //#endif
} wmWindowManager;
