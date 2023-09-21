#pragma once

/* types-savable wmStructs here */
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
struct ToolRef_Runtime;
struct wmMsgSubscribeKey;
struct wmMsgSubscribeValue;
struct wmOpType;

/* wm_toolsystem.c */
#define WM_TOOLSYSTEM_SPACE_MASK \
  ((1 << SPACE_IMAGE) | (1 << SPACE_NODE) | (1 << SPACE_VIEW3D) | (1 << SPACE_SEQ))
/* Values that define a category of active tool. */
typedef struct ToolKey {
  int space_type;
  int mode;
} ToolKey;

struct ToolRef *WM_toolsystem_ref_from_context(struct bContext *C);
struct ToolRef *WM_toolsystem_ref_find(struct WorkSpace *workspace, const bToolKey *tkey);
bool WM_toolsystem_ref_ensure(struct WorkSpace *workspace,
                              const bToolKey *tkey,
                              struct bToolRef **r_tref);

struct ToolRef *WM_toolsystem_ref_set_by_id_ex(struct bContext *C,
                                                struct WorkSpace *workspace,
                                                const bToolKey *tkey,
                                                const char *name,
                                                bool cycle);
struct ToolRef *WM_toolsystem_ref_set_by_id(struct bContext *C, const char *name);

struct ToolRef_Runtime *WM_toolsystem_runtime_from_context(struct bContext *C);
struct ToolRef_Runtime *WM_toolsystem_runtime_find(struct WorkSpace *workspace,
                                                    const bToolKey *tkey);

void WM_toolsystem_unlink(struct bContext *C, struct WorkSpace *workspace, const bToolKey *tkey);
void WM_toolsystem_refresh(struct bContext *C, struct WorkSpace *workspace, const bToolKey *tkey);
void WM_toolsystem_reinit(struct bContext *C, struct WorkSpace *workspace, const bToolKey *tkey);

/* Op on all active tools. */
void WM_toolsystem_unlink_all(struct bContext *C, struct WorkSpace *workspace);
void WM_toolsystem_refresh_all(struct bContext *C, struct WorkSpace *workspace);
void WM_toolsystem_reinit_all(struct bContext *C, struct wmWindow *win);

void WM_toolsystem_ref_set_from_runtime(struct bContext *C,
                                        struct WorkSpace *workspace,
                                        struct bToolRef *tref,
                                        const struct bToolRef_Runtime *tref_rt,
                                        const char *idname);

/* Sync the internal active state of a tool back into the tool system,
 * this is needed for active brushes where the real active state is not stored in the tool system.
 *
 * see toolsystem_ref_link */
void wm_toolsystem_ref_sync_from_context(struct Main *bmain,
                                         struct WorkSpace *workspace,
                                         struct bToolRef *tref);

void WM_toolsystem_init(struct bContext *C);

int WM_toolsystem_mode_from_spacetype(struct ViewLayer *view_layer,
                                      struct ScrArea *area,
                                      int space_type);
bool WM_toolsystem_key_from_context(struct ViewLayer *view_layer,
                                    struct ScrArea *area,
                                    ToolKey *tkey);

void WM_toolsystem_update_from_context_view3d(struct Cxt *C);
void WM_toolsystem_update_from_context(struct Cxt *C,
                                       struct WorkSpace *workspace,
                                       struct ViewLayer *view_layer,
                                       struct ScrArea *area);

/* For paint modes to support non-brush tools. */
bool WM_toolsystem_active_tool_is_brush(const struct Cxt *C);

/* Follow wmMsgNotifyFn spec. */
void WM_toolsystem_do_msg_notify_tag_refresh(struct Cxt *C,
                                             struct wmMsgSubscribeKey *msg_key,
                                             struct wmMsgSubscribeValue *msg_val);

struct IdProp *wm_toolsystem_ref_props_get_idprops(struct ToolRef *tref);
struct IdProp *wm_toolsystem_ref_props_ensure_idprops(struct ToolRef *tref);
void wm_toolsystem_ref_props_ensure_ex(struct ToolRef *tref,
                                       const char *idname,
                                       struct ApiStruct *type,
                                       struct ApiPtr *r_ptr);

#define wm_toolsystem_ref_props_ensure_from_op(tref, ot, r_ptr) \
  wm_toolsystem_ref_props_ensure_ex(tref, (ot)->idname, (ot)->srna, r_ptr)
#define wm_toolsystem_ref_props_ensure_from_gizmo_group(tref, gzgroup, r_ptr) \
  wm_toolsystem_ref_props_ensure_ex(tref, (gzgroup)->idname, (gzgroup)->srna, r_ptr)

bool wm_toolsystem_ref_props_get_ex(struct ToolRef *tref,
                                         const char *idname,
                                         struct ApiStruct *type,
                                         struct ApiPtr *r_ptr);
#define wm_toolsystem_ref_props_get_from_op(tref, ot, r_ptr) \
  wm_toolsystem_ref_props_get_ex(tref, (ot)->idname, (ot)->srna, r_ptr)
#define wm_toolsystem_ref_props_get_from_gizmo_group(tref, gzgroup, r_ptr) \
  wm_toolsystem_ref_props_get_ex(tref, (gzgroup)->idname, (gzgroup)->srna, r_ptr)

void wm_toolsystem_ref_properties_init_for_keymap(struct bToolRef *tref,
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
