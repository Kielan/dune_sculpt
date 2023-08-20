#pragma once

#include "types_list.h"
#include "types_screen.h" /* for #ScrAreaMap */
#include "types_xr.h"     /* for #XrSessionSettings */

#include "types_id.h"

struct Window;
struct WM;

struct wmEvent;
struct wmGesture;
struct wmKeyConfig;
struct wmKeyMap;
struct wmMsgBus;
struct wmOp;
struct wmOpType;

/* Forward declarations: */
struct PtrAPI;
struct Report;
struct ReportList;
struct Stereo3dFormat;
struct Cxt;
struct Screen;
struct uiLayout;
struct wmTimer;

#define OP_MAX_TYPENAME 64
#define KMAP_MAX_NAME 64

/** Keep in sync with 'rna_enum_wm_report_items' in `wm_rna.c`. */
typedef enum eReportType {
  RPT_DEBUG = (1 << 0),
  RPT_INFO = (1 << 1),
  RPT_OP = (1 << 2),
  RPT_PROP = (1 << 3),
  RPT_WARNING = (1 << 4),
  RPT_ERROR = (1 << 5),
  RPT_ERROR_INVALID_INPUT = (1 << 6),
  RPT_ERROR_INVALID_CONTEXT = (1 << 7),
  RPT_ERROR_OUT_OF_MEMORY = (1 << 8),
} eReportType;

#define RPT_DEBUG_ALL (RPT_DEBUG)
#define RPT_INFO_ALL (RPT_INFO)
#define RPT_OP_ALL (RPT_OP)
#define RPT_PROP_ALL (RPT_PROP)
#define RPT_WARNING_ALL (RPT_WARNING)
#define RPT_ERROR_ALL \
  (RPT_ERROR | RPT_ERROR_INVALID_INPUT | RPT_ERROR_INVALID_CONTEXT | RPT_ERROR_OUT_OF_MEMORY)

enum ReportListFlags {
  RPT_PRINT = (1 << 0),
  RPT_STORE = (1 << 1),
  RPT_FREE = (1 << 2),
  RPT_OP_HOLD = (1 << 3), /* don't move them into the operator global list (caller will use) */
};

/* These two Lines with # tell types this struct can be excluded. */
#
#
typedef struct Report {
  struct Report *next, *prev;
  /** eReportType. */
  short type;
  short flag;
  /** `strlen(message)`, saves some time calculating the word wrap. */
  int len;
  const char *typestr;
  const char *message;
} Report;

/** Saved in the wm, don't remove. */
typedef struct ReportList {
  List list;
  /** eReportType. */
  int printlevel;
  /** eReportType. */
  int storelevel;
  int flag;
  char _pad[4];
  struct wmTimer *reporttimer;
} ReportList;

/* timer customdata to control reports display */
/* These two Lines with # tell types this struct can be excluded. */
#
#
typedef struct ReportTimerInfo {
  float col[4];
  float widthfac;
} ReportTimerInfo;

//#ifdef WITH_XR_OPENXR
typedef struct wmXrData {
  /** Runtime information for managing Dune specific behaviors. */
  struct wmXrRuntimeData *runtime;
  /** Permanent session settings (draw mode, feature toggles, etc). Stored in files and accessible
   * even before the session runs. */
  XrSessionSettings session_settings;
} wmXrData;

/** Window-manager is saved, tag WMAN. */
typedef struct WM {
  Id id;

  /** Separate active from drawable. */
  struct Window *windrawable;
  /** `cxt_wm_window(C)` is usually preferred.
   * Avoid relying on this where possible as this may become NULL during when handling
   * events that close or replace windows (opening a file for e.g.).
   * While this happens rarely in practice, it can cause difficult to reproduce bugs.  */
  struct Window *winactive;
  List windows;

  /** Set on file read. */
  short initialized;
  /** Indicator whether data was saved. */
  short file_saved;
  /** Op stack depth to avoid nested undo pushes. */
  short op_undo_depth;

  /** Set after selection to notify outliner to sync. Stores type of selection */
  short outliner_sync_select_dirty;

  /** Operator registry. */
  List ops;

  /** Refresh/redraw #wmNotifier structs. */
  List notifier_queue;

  /** Information and error reports. */
  struct ReportList reports;

  /** Threaded jobs manager. */
  List jobs;

  /** Extra overlay cursors to draw, like circles. */
  List paintcursors;

  /** Active dragged items. */
  List drags;

  /** Known key configurations. */
  List keyconfigs;
  /** Default configuration. */
  struct wmKeyConfig *defaultconf;
  /** Addon configuration. */
  struct wmKeyConfig *addonconf;
  /** User configuration. */
  struct wmKeyConfig *userconf;

  /** Active timers. */
  List timers;
  /** Timer for auto save. */
  struct wmTimer *autosavetimer;

  /** All undo history (runtime only). */
  struct UndoStack *undo_stack;

  /** Indicates whether interface is locked for user interaction. */
  char is_interface_locked;
  char _pad[7];

  struct wmMsgBus *message_bus;

  //#ifdef WITH_XR_OPENXR
  wmXrData xr;
  //#endif
} WM;
