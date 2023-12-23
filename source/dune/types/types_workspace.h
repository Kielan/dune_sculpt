#pragma once

#include "types_id.h"
#include "types_asset.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ToolRef_Runtime.flag */
enum {
  /* This tool should use the fallback key-map.
   * Typically gizmos handle this but some tools (such as the knife tool) don't use a gizmo.   */
  TOOLREF_FLAG_FALLBACK_KEYMAP = (1 << 0),
};

#
#
typedef struct ToolRef_Runtime {
  int cursor;

  /* One of these 3 must be defined. */
  char keymap[64];
  char gizmo_group[64];
  char data_block[64];

  /* Keymap for ToolRef.idname_fallback, if set. */
  char keymap_fallback[64];

  /* Use to infer primary operator to use when setting accelerator keys. */
  char op[64];

  /* Index when a tool is a member of a group. */
  int index;
  /* Opts: `TOOLREF_FLAG_*`. */
  int flag;
} ToolRef_Runtime;

/* Stored per mode. */
typedef struct ToolRef {
  struct ToolRef *next, *prev;
  char idname[64];

  /* Optionally use these when not interacting directly with the primary tools gizmo. */
  char idname_fallback[64];

  /* Use to avoid init'ing the same tool mult times. */
  short tag;

  /* ToolKey (spacetype, mode), used in 'win_api.h' */
  short space_type;
  /* Val depends on the 'space_type', ob mode for 3D view, img editor has own mode too.
   * api needs to handle using item function. */
  int mode;

  /* Use for tool opts, each group's name must match a tool name:
   *
   *    {"Tool Name": {"SOME_OT_op": {...}, ..}, ..}
   *
   * This is done since diff tools may call the same ops w their own opts. */
  IdProp *props;

  /* Vars needed to op the tool. */
  ToolRef_Runtime *runtime;
} ToolRef;

/* Wrapper for Screen.
 *
 * Screens are Ids and thus stored in a main list.
 * We also want to store a list of them within the workspace
 * (so each workspace can have its own set of screen-layouts)
 * which would mess with the next/prev pointers.
 * So we use this struct to wrap a Screen ptr with another pair of next/prev ptrs. */
typedef struct WorkSpaceLayout {
  struct WorkSpaceLayout *next, *prev;

  struct Screen *screen;
  /* The name of this layout, we override the api name of the screen with this
   * (but not Id name itself) */
  /* MAX_NAME. */
  char name[64];
} WorkSpaceLayout;

/* Optional tags, which features to use, aligned with Addon names by convention. */
typedef struct WinOwnerId {
  struct WinOwnerId *next, *prev;
  /* MAX_NAME. */
  char name[64];
} WinOwnerId;

typedef struct WorkSpace {
  Id id;

  /* WorkSpaceLayout. */
  List layouts;
  /* Store for each hook (so for each window) which layout has
   * been activated the last time this workspace was visible. */
  /* WorkSpaceDataRelation. */
  List hook_layout_relations;

  /* Feature tagging (use for addons) */
  /* #WinOwnerId. */
  List owner_ids;

  /* List of ToolRef */
  List tools;

  char _pad[4];

  int ob_mode;

  /* Enum eWorkSpaceFlags. */
  int flags;

  /* Num for workspace tab reordering in the UI. */
  int order;

  /* Info txt from modal ops (runtime). */
  char *status_txt;

  /* Workspace-wide active asset lib, for asset UIs to use (e.g. asset view UI template). The
   * Asset Browser has its own and doesn't use this. */
  AssetLibRef asset_lib_ref;
} WorkSpace;

/* Generic (and simple/primitive) struct for storing a history of assignments/relations
 * of workspace data to non-workspace data in a listbase inside the workspace.
 *
 * Using this we can restore the old state of a workspace if the user switches back to it.
 *
 * Usage
 * =====
 * When activating a workspace, it should activate the screen-layout that was active in that
 * workspace before *in this window*.
 * More concretely:
 * * There are two windows, win1 and win2.
 * * Both show workspace ws1, but both also had workspace ws2 activated at some point before.
 * * Last time ws2 was active in win1, screen-layout sl1 was activated.
 * * Last time ws2 was active in win2, screen-layout sl2 was activated.
 * * When changing from ws1 to ws2 in win1, screen-layout sl1 should be activated again.
 * * When changing from ws1 to ws2 in win2, screen-layout sl2 should be activated again.
 * So that means we have to store the active screen-layout in a per workspace, per window
 * relation. This struct is used to store an active screen-layout for each window within the
 * workspace.
 * To find the screen-layout to activate for this window-workspace combination, simply lookup
 * the WorkSpaceDataRelation with the workspace-hook of the win set as parent. */
typedef struct WorkSpaceDataRelation {
  struct WorkSpaceDataRelation *next, *prev;

  /* The data used to id the relation
   * (e.g. to find screen-layout (= val) from/for a hook).
   * Now runtime only. */
  void *parent;
  /* The val for this parent-data/workspace relation. */
  void *val;

  /* Ref to the actual parent win, Win->winid. Used in read/write code. */
  int parentid;
  char _pad_0[4];
} WorkSpaceDataRelation;

/* Little wrapper to store data that is going to be per win, but coming from the workspace.
 * It allows us to keep workspace and win data completely separate. */
typedef struct WorkSpaceInstanceHook {
  WorkSpace *active;
  struct WorkSpaceLayout *act_layout;

  /* Needed bc we can't change workspaces/layouts in running handler loop,
   * it would break cxt. */
  WorkSpace *tmp_workspace_store;
  struct WorkSpaceLayout *tmp_layout_store;
} WorkSpaceInstanceHook;

typedef enum eWorkSpaceFlags {
  WORKSPACE_USE_FILTER_BY_ORIGIN = (1 << 1),
} eWorkSpaceFlags;

#ifdef __cplusplus
}
#endif
