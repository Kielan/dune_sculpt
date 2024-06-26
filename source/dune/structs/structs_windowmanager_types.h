#pragma once

#include "STRUCTS_listBase.h"
#include "STRUCTS_screen_types.h" /* for #ScrAreaMap */
#include "STRUCTS_xr_types.h"     /* for #XrSessionSettings */

#include "STRUCTS_ID.h"

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

struct PointerAPI;
struct Report;
struct ReportList;
struct Stereo3dFormat;
struct bContext;
struct bScreen;
struct uiLayout;
struct wmTimer;

#define OP_MAX_TYPENAME 64
#define KMAP_MAX_NAME 64

/** Keep in sync with 'rna_enum_wm_report_items' in `wm_rna.c`. */
typedef enum eReportType {
  RPT_DEBUG = (1 << 0),
  RPT_INFO = (1 << 1),
  RPT_OPERATOR = (1 << 2),
  RPT_PROPERTY = (1 << 3),
  RPT_WARNING = (1 << 4),
  RPT_ERROR = (1 << 5),
  RPT_ERROR_INVALID_INPUT = (1 << 6),
  RPT_ERROR_INVALID_CONTEXT = (1 << 7),
  RPT_ERROR_OUT_OF_MEMORY = (1 << 8),
} eReportType;

#define RPT_DEBUG_ALL (RPT_DEBUG)
#define RPT_INFO_ALL (RPT_INFO)
#define RPT_OPERATOR_ALL (RPT_OPERATOR)
#define RPT_PROPERTY_ALL (RPT_PROPERTY)
#define RPT_WARNING_ALL (RPT_WARNING)
#define RPT_ERROR_ALL \
  (RPT_ERROR | RPT_ERROR_INVALID_INPUT | RPT_ERROR_INVALID_CONTEXT | RPT_ERROR_OUT_OF_MEMORY)

enum ReportListFlags {
  RPT_PRINT = (1 << 0),
  RPT_STORE = (1 << 1),
  RPT_FREE = (1 << 2),
  RPT_OP_HOLD = (1 << 3), /* don't move them into the operator global list (caller will use) */
};

/* These two Lines with # tell makesdna this struct can be excluded. */
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

/**
 * Saved in the wm, don't remove.
 */
typedef struct ReportList {
  ListBase list;
  /** eReportType. */
  int printlevel;
  /** eReportType. */
  int storelevel;
  int flag;
  char _pad[4];
  struct wmTimer *reporttimer;
} ReportList;

/* timer customdata to control reports display */
/* These two Lines with # tell makesdna this struct can be excluded. */
#
#
typedef struct ReportTimerInfo {
  float col[4];
  float widthfac;
} ReportTimerInfo;

//#ifdef WITH_XR_OPENXR
typedef struct wmXrData {
  /** Runtime information for managing Blender specific behaviors. */
  struct wmXrRuntimeData *runtime;
  /** Permanent session settings (draw mode, feature toggles, etc). Stored in files and accessible
   * even before the session runs. */
  XrSessionSettings session_settings;
} wmXrData;

/** Window-manager is saved, tag WMAN. */
typedef struct wmWindowManager {
  ID id;

  /** Separate active from drawable. */
  struct wmWindow *windrawable;
  /**
   * `CTX_wm_window(C)` is usually preferred.
   * Avoid relying on this where possible as this may become NULL during when handling
   * events that close or replace windows (opening a file for e.g.).
   * While this happens rarely in practice, it can cause difficult to reproduce bugs.
   */
  struct wmWindow *winactive;
  ListBase windows;

  /** Set on file read. */
  short initialized;
  /** Indicator whether data was saved. */
  short file_saved;
  /** Operator stack depth to avoid nested undo pushes. */
  short op_undo_depth;

  /** Set after selection to notify outliner to sync. Stores type of selection */
  short outliner_sync_select_dirty;

  /** Operator registry. */
  ListBase operators;

  /** Refresh/redraw #wmNotifier structs. */
  ListBase notifier_queue;

  /** Information and error reports. */
  struct ReportList reports;

  /** Threaded jobs manager. */
  ListBase jobs;

  /** Extra overlay cursors to draw, like circles. */
  ListBase paintcursors;

  /** Active dragged items. */
  ListBase drags;

  /** Known key configurations. */
  ListBase keyconfigs;
  /** Default configuration. */
  struct wmKeyConfig *defaultconf;
  /** Addon configuration. */
  struct wmKeyConfig *addonconf;
  /** User configuration. */
  struct wmKeyConfig *userconf;

  /** Active timers. */
  ListBase timers;
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
} wmWindowManager;
