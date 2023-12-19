#pragma once

/* return val of handler-op call */
#define WIN_HANDLER_CONTINUE 0
#define WIN_HANDLER_BREAK 1
#define WIN_HANDLER_HANDLED 2
#define WIN_HANDLER_MODAL 4 /* MODAL|BREAK means unhandled */

struct ARgn;
struct GHOST_TabletData;
struct ScrArea;
enum WinOpCallCxt;

#ifdef WITH_XR_OPENXR
struct wmXrActionData;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* WinKeyMap is in types_winmngr.h, it's saveable */
/* Custom types for handlers, for signaling, freeing */
enum eWinEvHandlerType {
  WIN_HANDLER_TYPE_GIZMO = 1,
  WIN_HANDLER_TYPE_UI,
  WIN_HANDLER_TYPE_OP,
  WIN_HANDLER_TYPE_DROPBOX,
  WIN_HANDLER_TYPE_KEYMAP,
};

typedef bool (*EvHandlerPoll)(const ARgn *rgn, const WinEv *ev);

typedef struct WinEvHandler {
  struct WinEvHandler *next, *prev;

  enum eWinEvHandlerType type;
  char flag; /* WIN_HANDLER_BLOCKING, ... */

  EvHandlerPoll poll;
} WinEvHandler;

/* Run after the keymap item runs. */
struct WinEvHandlerKeymapPost {
  void (*post_fn)(WinKeyMap *keymap, WinKeyMapItem *kmi, void *user_data);
  void *user_data;
};

/* Support for a getter fn that looks up the keymap each access. */
struct WinEvHandlerKeymapDynamic {
  WinEvHandlerKeymapDynamicFn *keymap_fn;
  void *user_data;
};

/* WIN_HANDLER_TYPE_KEYMAP */
typedef struct WinEvHandlerKeymap {
  WinEvHandler head;

  /* Ptr to builtin/custom keymaps (never NULL). */
  WinKeyMap *keymap;

  struct WinEvHandlerKeymapPost post;
  struct WinEvHandlerKeymapDynamic dynamic;

  struct ToolRef *keymap_tool;
} WinEvHandlerKeymap;

/* WIN_HANDLER_TYPE_GIZMO */
typedef struct WinEvHandlerGizmo {
  WinEvHandler head;

  /* Gizmo handler (never NULL). */
  struct WinGizmoMap *gizmo_map;
} WinEvHandlerGizmo;

/* WM_HANDLER_TYPE_UI */
typedef struct WinEvHandlerUI {
  WinEvHandler head;

  WinUIHandlerFn handle_fn;       /* callback receiving events */
  WinUIHandlerRemoveFn remove_fn; /* callback when handler is removed */
  void *user_data;                 /* user data pointer */

  /* Store cxt for this handler for derived/modal handlers. */
  struct {
    struct ScrArea *area;
    struct ARgn *rgn;
    struct ARgn *menu;
  } cxt;
} WinEvHandlerUI;

/* WIN_HANDLER_TYPE_OP */
typedef struct WinEvHandlerOp {
  WinEvHandler head;

  /* Op can be NULL. */
  WinOp *op;

  /* Hack, special case for file-sel. */
  bool is_filesel;

  /* Store cxt for this handler for derived/modal handlers. */
  struct {
    /* To override the win, and hence the screen. Set for few cases only, usually window/screen
     * can be taken from current context. */
    struct Win *win;

    struct ScrArea *area;
    struct ARgn *rgn;
    short rgn_type;
  } cxt;
} WinEvHandler_Op;

/* WIN_HANDLER_TYPE_DROPBOX */
typedef struct WinEvHandlerDropbox {
  WinEvHandler head;

  /* Never NULL. */
  List *dropboxes;
} WinEvHandlerDropbox;

/* win_ev_system.c */
void win_ev_free_all(Win *win);
void win_ev_free(WinEvrc CD d *event);
void win_ev_free_handler(wmEventHandler *handler);

/* Goes over entire hierarchy: events -> window -> screen -> area -> region.
 *
 * Called in main loop.
 */
void wm_event_do_handlers(Cxt *C);

/* Windows store own event queues #wmWindow.event_queue (no Cxt here). */
void wm_event_add_ghostevent(wmWindowManager *wm, wmWindow *win, int type, void *customdata);
#ifdef WITH_XR_OPENXR
void wm_event_add_xrevent(wmWindow *win, struct wmXrActionData *actiondata, short val);
#endif

void wm_event_do_depsgraph(Cxt *C, bool is_after_open_file);
/* Was part of wm_event_do_notifiers,
 * split out so it can be called once before entering the WM_main loop.
 * This ensures operators don't run before the UI and graph are initialized. */
void wm_event_do_refresh_wm_and_graph(Cxt *C);
/* Called in main-loop. */
void wm_event_do_notifiers(Cxt *C);

void wm_event_handler_ui_cancel_ex(Cxt *C,
                                   wmWindow *win,
                                   ARegion *region,
                                   bool reactivate_btn);

/* wm_event_query.c */
/* Applies the global tablet pressure correction curve. */
float wm_pressure_curve(float raw_pressure);
void wm_tablet_data_from_ghost(const struct GHOST_TabletData *tablet_data, wmTabletData *wmtab);

/* wm_dropbox.c */

void wm_dropbox_free(void);
/* Additional work to cleanly end dragging. Additional because this doesn't actually remove the
 * drag items. Should be called whenever dragging is stopped
 * (successful or not, also when canceled). */
void wm_drags_exit(wmWindowManager *wm, wmWindow *win);
void wm_drop_prepare(Cxt *C, wmDrag *drag, wmDropBox *drop);
void wm_drop_end(Cxt *C, wmDrag *drag, wmDropBox *drop);
/* Called in inner handler loop, region cxt. */
void wm_drags_check_ops(Cxt *C, const wmEvent *event);
/* The op of a dropbox should always be executed in the context determined by the mouse
 * coordinates. The dropbox poll should check the context area and region as needed.
 * So this always returns #WM_OP_INVOKE_DEFAULT. */
wmOpCallCxt wm_drop_op_cxt_get(const wmDropBox *drop);
/* Called in wm_draw_window_onscreen.
 */
void wm_drags_draw(bContext *C, wmWindow *win);

#ifdef __cplusplus
}
#endif
