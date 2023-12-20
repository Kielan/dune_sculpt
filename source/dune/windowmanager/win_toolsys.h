#pragma once
/* types-savable WinStructs here */
#include "lib_compiler_attrs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct IdProp;
struct Main;
struct ApiPtr;
struct ScrArea;
struct ApiStruct;
struct WorkSpace;
struct Cxt;
struct ToolRefRuntime;
struct WinMsgSubKey;
struct WinMsgSubVal;
struct WinOpType;

/* win_toolsys.c */
#define WIN_TOOLSYS_SPACE_MASK \
  ((1 << SPACE_IMG) | (1 << SPACE_NODE) | (1 << SPACE_VIEW3D) | (1 << SPACE_SEQ))
/* Values that define a category of active tool. */
typedef struct ToolKey {
  int space_type;
  int mode;
} ToolKey;

struct ToolRef *win_toolsys_ref_from_cxt(struct Cxt *C);
struct ToolRef *win_toolsys_ref_find(struct WorkSpace *workspace, const ToolKey *tkey);
bool win_toolsys_ref_ensure(struct WorkSpace *workspace,
                            const ToolKey *tkey,
                            struct ToolRef **r_tref);

struct ToolRef *win_toolsys_ref_set_by_id_ex(struct bContext *C,
                                                struct WorkSpace *workspace,
                                                const bToolKey *tkey,
                                                const char *name,
                                                bool cycle);
struct ToolRef *win_toolsys_ref_set_by_id(struct bContext *C, const char *name);

struct ToolRefRuntime *win_toolsys_runtime_from_cxt(struct Cxt *C);
struct ToolRefRuntime *win_toolsys_runtime_find(struct WorkSpace *workspace,
                                                const ToolKey *tkey);

void win_toolsys_unlink(struct Cxt *C, struct WorkSpace *workspace, const ToolKey *tkey);
void win_toolsys_refresh(struct Cxt *C, struct WorkSpace *workspace, const ToolKey *tkey);
void win_toolsys_reinit(struct Cxt *C, struct WorkSpace *workspace, const ToolKey *tkey);

/* Op on all active tools. */
void win_toolsys_unlink_all(struct Cxt *C, struct WorkSpace *workspace);
void win_toolsys_refresh_all(struct Cxt *C, struct WorkSpace *workspace);
void win_toolsys_reinit_all(struct Cxt *C, struct Win *win);

void win_toolsys_ref_set_from_runtime(struct Cxt *C,
                                        struct WorkSpace *workspace,
                                        struct ToolRef *tref,
                                        const struct ToolRefRuntime *tref_rt,
                                        const char *idname);

/* Sync the internal active state of a tool back into the tool system,
 * this is needed for active brushes where the real active state is not stored in the tool system.
 *
 * see toolsys_ref_link */
void win_toolsys_ref_sync_from_cxt(struct Main *main,
                                   struct WorkSpace *workspace,
                                   struct ToolRef *tref);

void win_toolsys_init(struct bContext *C);

int win_toolsys_mode_from_spacetype(struct ViewLayer *view_layer,
                                      struct ScrArea *area,
                                      int space_type);
bool win_toolsys_key_from_cxt(struct ViewLayer *view_layer,
                                    struct ScrArea *area,
                                    ToolKey *tkey);

void win_toolsys_update_from_cxt_view3d(struct Cxt *C);
void win_toolsysupdate_from_cxt(struct Cxt *C,
                                struct WorkSpace *workspace,
                                struct ViewLayer *view_layer,
                                struct ScrArea *area);

/* For paint modes to support non-brush tools. */
bool win_toolsys_active_tool_is_brush(const struct Cxt *C);

/* Follow WinMsgNotifyFn spec. */
void win_toolsys_do_msg_notify_tag_refresh(struct Cxt *C,
                                           struct WinMsgSubKey *msg_key,
                                           struct WinMsgSubVal *msg_val);

struct IdProp *wm_toolsys_ref_props_get_idprops(struct ToolRef *tref);
struct IdProp *wm_toolsys_ref_props_ensure_idprops(struct ToolRef *tref);
void wm_toolsys_ref_props_ensure_ex(struct ToolRef *tref,
                                       const char *idname,
                                       struct ApiStruct *type,
                                       struct ApiPtr *r_ptr);

#define wm_toolsys_ref_props_ensure_from_op(tref, ot, r_ptr) \
  wm_toolsys_ref_props_ensure_ex(tref, (ot)->idname, (ot)->srna, r_ptr)
#define wm_toolsys_ref_props_ensure_from_gizmo_group(tref, gzgroup, r_ptr) \
  wm_toolsys_ref_props_ensure_ex(tref, (gzgroup)->idname, (gzgroup)->sapi, r_ptr)

bool wm_toolsys_ref_props_get_ex(struct ToolRef *tref,
                                  const char *idname,
                                  struct ApiStruct *type,
                                  struct ApiPtr *r_ptr);
#define wm_toolsystem_ref_props_get_from_op(tref, ot, r_ptr) \
  wm_toolsystem_ref_props_get_ex(tref, (ot)->idname, (ot)->sapi, r_ptr)
#define wm_toolsystem_ref_props_get_from_gizmo_group(tref, gzgroup, r_ptr) \
  wm_toolsystem_ref_props_get_ex(tref, (gzgroup)->idname, (gzgroup)->sapi, r_ptr)

void wm_toolsystem_ref_props_init_for_keymap(struct bToolRef *tref,
                                                  struct PointerRNA *dst_ptr,
                                                  struct PointerRNA *src_ptr,
                                                  struct wmOperatorType *ot);

/**
 * Use to update the active tool (shown in the top bar) in the least disruptive way.
 *
 * This is a little involved since there may be multiple valid active tools
 * depending on the mode and space type.
 *
 * Used when undoing since the active mode may have changed.
 */
void WM_toolsystem_refresh_active(struct bContext *C);

void WM_toolsystem_refresh_screen_area(struct WorkSpace *workspace,
                                       struct ViewLayer *view_layer,
                                       struct ScrArea *area);
void WM_toolsystem_refresh_screen_window(struct wmWindow *win);
void WM_toolsystem_refresh_screen_all(struct Main *bmain);

#ifdef __cplusplus
}
#endif
