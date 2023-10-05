#pragma once

#include "lib_compiler_attrs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Main;
struct Screen;
struct ToolRef;

/* Create, Delete, Initialize */

struct WorkSpace *workspace_add(struct Main *main, const char *name);
/* Remove workspace by freeing itself and its data. This is a higher-level wrapper that
 * calls workspace_free_data (through id_free) to free the workspace data, and frees
 * other data-blocks owned by workspace and its layouts (currently that is screens only).
 *
 * Always use this to remove (and free) workspaces. Don't free non-ID workspace members here. */
void BKE_workspace_remove(struct Main *main, struct WorkSpace *workspace);

struct WorkSpaceInstanceHook *workspace_instance_hook_create(const struct Main *main,
                                                                 int winid);
void workspace_instance_hook_free(const struct Main *main,
                                      struct WorkSpaceInstanceHook *hook);

/* Add a new layout to workspace for screen. */
struct WorkSpaceLayout *workspace_layout_add(struct Main *main,
                                             struct WorkSpace *workspace,
                                             struct Screen *screen,
                                             const char *name) ATTR_NONNULL();
void workspace_layout_remove(struct Main *main,
                                 struct WorkSpace *workspace,
                                 struct WorkSpaceLayout *layout) ATTR_NONNULL();

void workspace_relations_free(List *relation_list)

/* General Utilities */
struct WorkSpaceLayout *workspace_layout_find(const struct WorkSpace *workspace,
                                              const struct Screen *screen)
    ATTR_NONNULL() ATTR_WARN_UNUSED_RESULT;
/* Find the layout for screen without knowing which workspace to look in.
 * Can also be used to find the workspace that contains screen.
 *
 * param r_workspace: Optionally return the workspace that contains the
 * looked up layout (if found) */
struct WorkSpaceLayout *workspace_layout_find_global(const struct Main *main,
                                                         const struct Screen *screen,
                                                         struct WorkSpace **r_workspace)
    ATTR_NONNULL(1, 2);

/* Circular workspace layout iterator.
 *
 * param cb: Custom fn which gets executed for each layout.
 * Can return false to stop iterating.
 * param arg: Custom data passed to each \a callback call.
 *
 * return the layout at which \a callback returned false.
 */
struct WorkSpaceLayout *workspace_layout_iter_circular(
    const struct WorkSpace *workspace,
    struct WorkSpaceLayout *start,
    bool (*cb)(const struct WorkSpaceLayout *layout, void *arg),
    void *arg,
    bool iter_backward);

void workspace_tool_remove(struct WorkSpace *workspace, struct ToolRef *tref)
    ATTR_NONNULL(1, 2);

/* Getters/Setters */
#define GETTER_ATTRS ATTR_NONNULL() ATTR_WARN_UNUSED_RESULT
#define SETTER_ATTRS ATTR_NONNULL(1)

struct WorkSpace *workspace_active_get(struct WorkSpaceInstanceHook *hook) GETTER_ATTRS;
void workspace_active_set(struct WorkSpaceInstanceHook *hook,
                              struct WorkSpace *workspace) SETTER_ATTRS;
/* Get the layout that is active for hook (which is the visible layout for the active workspace
 * in hook). */
struct WorkSpaceLayout *workspace_active_layout_get(const struct WorkSpaceInstanceHook *hook)
    GETTER_ATTRS;
/* Activate a layout
 *
 * Sets layout as active for workspace when activated through or already active in hook.
 * So when the active workspace of hook is workspace, layout becomes the active layout of
 * hook too. See workspace_active_set().
 *
 * workspace does not need to be active for this.
 *
 * WorkSpaceInstanceHook.act_layout should only be modified directly to update the layout ptr. */
void workspace_active_layout_set(struct WorkSpaceInstanceHook *hook,
                                 int winid,
                                 struct WorkSpace *workspace,
                                 struct WorkSpaceLayout *layout) SETTER_ATTRS;
struct Screen *workspace_active_screen_get(const struct WorkSpaceInstanceHook *hook)
    GETTER_ATTRS;
void workspace_active_screen_set(struct WorkSpaceInstanceHook *hook,
                                 int winid,
                                 struct WorkSpace *workspace,
                                 struct Screen *screen) SETTER_ATTRS;

const char *workspace_layout_name_get(const struct WorkSpaceLayout *layout) GETTER_ATTRS;
void workspace_layout_name_set(struct WorkSpace *workspace,
                                   struct WorkSpaceLayout *layout,
                                   const char *new_name) ATTR_NONNULL();
struct Screen *workspace_layout_screen_get(const struct WorkSpaceLayout *layout) GETTER_ATTRS;

/* Get the layout to be activated should \a workspace become or be the active workspace in \a hook. */
struct WorkSpaceLayout *workspace_active_layout_for_workspace_get(
    const struct WorkSpaceInstanceHook *hook, const struct WorkSpace *workspace) GETTER_ATTRS;

bool workspace_owner_id_check(const struct WorkSpace *workspace, const char *owner_id)
    ATTR_NONNULL();

void workspace_id_tag_all_visible(struct Main *main, int tag) ATTR_NONNULL();

#undef GETTER_ATTRS
#undef SETTER_ATTRS

#ifdef __cplusplus
}
