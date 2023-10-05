#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib_list.h"
#include "lib_string.h"
#include "lib_string_utils.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "dune_asset.h"
#include "dune_global.h"
#include "dune_idprop.h"
#include "dune_idtype.h"
#include "dune_lib_id.h"
#include "dune_lib_query.h"
#include "dune_main.h"
#include "dune_object.h"
#include "dune_scene.h"
#include "dune_workspace.h"

#include "types_object.h"
#include "types_scene.h"
#include "types_screen.h"
#include "types_wm.h"
#include "types_workspace.h"

#include "graph.h"

#include "mem_guardedalloc.h"

#include "loader_read_write.h"

static void workspace_init_data(Id *id)
{
  WorkSpace *workspace = (WorkSpace *)id;

  dune_asset_lib_ref_init_default(&workspace->asset_lib_ref);
}

static void workspace_free_data(Id *id)
{
  WorkSpace *workspace = (WorkSpace *)id;

  dune_workspace_relations_free(&workspace->hook_layout_relations);

  lib_freelistn(&workspace->owner_ids);
  lib_freelistn(&workspace->layouts);

  while (lib_list_is_empty(&workspace->tools)) {
    dune_workspace_tool_remove(workspace, workspace->tools.first);
  }

  MEM_SAFE_FREE(workspace->status_text);
}

static void workspace_foreach_id(Id *id, LibForeachIdData *data)
{
  WorkSpace *workspace = (WorkSpace *)id;

  LIST_FOREACH (WorkSpaceLayout *, layout, &workspace->layouts) {
    DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, layout->screen, IDWALK_CB_USER);
  }
}

static void workspace_dune_write(Writer *writer, Id *id, const void *id_address)
{
  WorkSpace *workspace = (WorkSpace *)id;

  loader_write_id_struct(writer, WorkSpace, id_address, &workspace->id);
  dune_id_dune_write(writer, &workspace->id);
  loader_write_struct_list(writer, WorkSpaceLayout, &workspace->layouts);
  loader_write_struct_list(writer, WorkSpaceDataRelation, &workspace->hook_layout_relations);
  loader_write_struct_list(writer, wmOwnerId, &workspace->owner_ids);
  loader_write_struct_list(writer, ToolRef, &workspace->tools);
  LIST_FOREACH (ToolRef *, tref, &workspace->tools) {
    if (tref->props) {
      IDP_DuneWrite(writer, tref->props);
    }
  }
}

static void workspace_dune_read_data(DuneDataReader *reader, Id *id)
{
  WorkSpace *workspace = (WorkSpace *)id;

  loader_read_list(reader, &workspace->layouts);
  loader_read_list(reader, &workspace->hook_layout_relations);
  loader_read_list(reader, &workspace->owner_ids);
  loader_read_list(reader, &workspace->tools);

  LIST_FOREACH (WorkSpaceDataRelation *, relation, &workspace->hook_layout_relations) {
    /* Parent pointer does not belong to workspace data and is therefore restored in lib_link step
     * of window manager. */
    loader_read_data_address(reader, &relation->value);
  }

  LIST_FOREACH (ToolRef *, tref, &workspace->tools) {
    tref->runtime = NULL;
    loader_read_data_address(reader, &tref->props);
    IDP_DuneDataRead(reader, &tref->props);
  }

  workspace->status_text = NULL;

  id_us_ensure_real(&workspace->id);
}

static void workspace_dune_read_lib(LibReader *reader, Id *id)
{
  WorkSpace *workspace = (WorkSpace *)id;
  Main *dunemain = loader_read_lib_get_main(reader);

  /* Restore proper 'parent' pointers to relevant data, and clean up unused/invalid entries. */
  LIST_FOREACH_MUTABLE (WorkSpaceDataRelation *, relation, &workspace->hook_layout_relations) {
    relation->parent = NULL;
    LIST_FOREACH (WM *, wm, &dunemain->wm) {
      LIST_FOREACH (wmWindow *, win, &wm->windows) {
        if (win->winid == relation->parentid) {
          relation->parent = win->workspace_hook;
        }
      }
    }
    if (relation->parent == NULL) {
      lib_freelinkn(&workspace->hook_layout_relations, relation);
    }
  }

  LIST_FOREACH_MUTABLE (WorkSpaceLayout *, layout, &workspace->layouts) {
    loader_read_id_address(reader, id->lib, &layout->screen);

    if (layout->screen) {
      if (ID_IS_LINKED(id)) {
        layout->screen->winid = 0;
        if (layout->screen->temp) {
          /* delete temp layouts when appending */
          dune_workspace_layout_remove(dunemain, workspace, layout);
        }
      }
    }
    else {
      /* If we're reading a layout without screen stored, it's useless and we shouldn't keep it
       * around. */
      dune_workspace_layout_remove(dunemain, workspace, layout);
    }
  }
}

static void workspace_dune_read_expand(Expander *expander, Id *id)
{
  WorkSpace *workspace = (WorkSpace *)id;

  LIST_FOREACH (WorkSpaceLayout *, layout, &workspace->layouts) {
    loader_expand(expander, dune_workspace_layout_screen_get(layout));
  }
}

IdTypeInfo IdTypeWS = {
    .id_code = ID_WS,
    .id_filter = FILTER_ID_WS,
    .main_list_index = INDEX_ID_WS,
    .struct_size = sizeof(WorkSpace),
    .name = "WorkSpace",
    .name_plural = "workspaces",
    .lang_cxt = LANG_CXT_ID_WORKSPACE,
    .flags = IDTYPE_FLAGS_NO_COPY | IDTYPE_FLAGS_ONLY_APPEND | IDTYPE_FLAGS_NO_ANIMDATA,
    .asset_type_info = NULL,

    .init_data = workspace_init_data,
    .copy_data = NULL,
    .free_data = workspace_free_data,
    .make_local = NULL,
    .foreach_id = workspace_foreach_id,
    .foreach_cache = NULL,
    .foreach_path = NULL,
    .owner_get = NULL,

    .dune_write = workspace_dune_write,
    .dune_read_data = workspace_dune_read_data,
    .dune_read_lib = workspace_dune_read_lib,
    .dune_read_expand = workspace_dune_read_expand,

    .dune_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

/* Internal Utils */
static void workspace_layout_name_set(WorkSpace *workspace,
                                      WorkSpaceLayout *layout,
                                      const char *new_name)
{
  lib_strncpy(layout->name, new_name, sizeof(layout->name));
  lib_uniquename(&workspace->layouts,
                 layout,
                 "Layout",
                 '.',
                 offsetof(WorkSpaceLayout, name),
                 sizeof(layout->name));
}

/* This should only be used directly when it is to be expected that there isn't
 * a layout within a workspace that wraps a screen. Usually - especially outside
 * of dune_workspace - dune_workspace_layout_find should be used! */
static WorkSpaceLayout *workspace_layout_find_ex(const WorkSpace *workspace,
                                                 const Screen *screen)
{
  return lib_findptr(&workspace->layouts, screen, offsetof(WorkSpaceLayout, screen));
}

static void workspace_relation_add(List *relation_list,
                                   void *parent,
                                   const int parentid,
                                   void *data)
{
  WorkSpaceDataRelation *relation = mem_callocN(sizeof(*relation), __func__);
  relation->parent = parent;
  relation->parentid = parentid;
  relation->value = data;
  /* add to head, if we switch back to it soon we find it faster. */
lib_addhead(relation_list, relation);
}
static void workspace_relation_remove(List *relation_list, WorkSpaceDataRelation *relation)
{
  lib_remlink(relation_list, relation);
  mem_freen(relation);
}

static void workspace_relation_ensure_updated(List *relation_list,
                                              void *parent,
                                              const int parentid,
                                              void *data)
{
  WorkSpaceDataRelation *relation = lib_list_bytes_find(
      relation_list, &parentid, sizeof(parentid), offsetof(WorkSpaceDataRelation, parentid));
  if (relation != NULL) {
    relation->parent = parent;
    relation->value = data;
    /* reinsert at the head of the list, so that more commonly used relations are found faster. */
    lib_remlink(relation_list, relation);
    lib_addhead(relation_list, relation);
  }
  else {
    /* no matching relation found, add new one */
    workspace_relation_add(relation_list, parent, parentid, data);
  }
}

static void *workspace_relation_get_data_matching_parent(const List *relation_list,
                                                         const void *parent)
{
  WorkSpaceDataRelation *relation = lib_findptr(
      relation_list, parent, offsetof(WorkSpaceDataRelation, parent));
  if (relation != NULL) {
    return relation->value;
  }

  return NULL;
}

/* Checks if a screen is already used within any workspace. A screen should never be assigned to
 * multiple WorkSpaceLayouts, but that should be ensured outside of the dune_workspace module
 * and without such checks.
 * Hence, this should only be used as assert check before assigning a screen to a workspace. */
#ifndef NDEBUG
static bool workspaces_is_screen_used
#else
static bool UNUSED_FN(workspaces_is_screen_used)
#endif
    (const Main *dunemain, Screen *screen)
{
  for (WorkSpace *workspace = dunemain->workspaces.first; workspace; workspace = workspace->id.next) {
    if (workspace_layout_find_ex(workspace, screen)) {
      return true;
    }
  }

  return false;
}

/* Create, Delete, Init */
WorkSpace *workspace_add(Main *dunemain, const char *name)
{
  WorkSpace *new_workspace = id_new(dunemain, IdWS, name);
  id_us_ensure_real(&new_workspace->id);
  return new_workspace;
}

void dune_workspace_remove(Main *dunemain, WorkSpace *workspace)
{
  for (WorkSpaceLayout *layout = workspace->layouts.first, *layout_next; layout;
       layout = layout_next) {
    layout_next = layout->next;
    workspace_layout_remove(dunemain, workspace, layout);
  }
  id_free(dunemain, workspace);
}

WorkSpaceInstanceHook *workspace_instance_hook_create(const Main *dunemain, const int winid)
{
  WorkSpaceInstanceHook *hook = mem_callocn(sizeof(WorkSpaceInstanceHook), __func__);

  /* set an active screen-layout for each possible window/workspace combination */
  for (WorkSpace *workspace = dunemain->workspaces.first; workspace; workspace = workspace->id.next) {
    workspace_active_layout_set(hook, winid, workspace, workspace->layouts.first);
  }

  return hook;
}
void workspace_instance_hook_free(const Main *dunemain, WorkSpaceInstanceHook *hook)
{
  /* workspaces should never be freed before wm (during which we call this function).
   * However, when running in background mode, loading a dune file may allocate windows (that need
   * to be freed) without creating workspaces. This happens in DunefileLoadingBaseTest. */
  lib_assert(!lib_list_is_empty(&dunemain->workspaces) || G.background);

  /* Free relations for this hook */
  for (WorkSpace *workspace = dunemain->workspaces.first; workspace; workspace = workspace->id.next) {
    for (WorkSpaceDataRelation *relation = workspace->hook_layout_relations.first, *relation_next;
         relation;
         relation = relation_next) {
      relation_next = relation->next;
      if (relation->parent == hook) {
        workspace_relation_remove(&workspace->hook_layout_relations, relation);
      }
    }
  }

  mem_freeNn(hook);
}

WorkSpaceLayout *dune_workspace_layout_add(Main *main,
                                           WorkSpace *workspace,
                                           Screen *screen,
                                           const char *name)
{
  WorkSpaceLayout *layout = mem_callocN(sizeof(*layout), __func__);

  lib_assert(!workspaces_is_screen_used(main, screen));
#ifndef DEBUG
  UNUSED_VARS(dunemain);
#endif
  layout->screen = screen;
  id_us_plus(&layout->screen->id);
  workspace_layout_name_set(workspace, layout, name);
  lib_addtail(&workspace->layouts, layout);

  return layout;
}

void workspace_layout_remove(Main *main, WorkSpace *workspace, WorkSpaceLayout *layout)
{
  /* Screen should usually be set, but we call this from file reading to get rid of invalid
   * layouts. */
  if (layout->screen) {
    id_us_min(&layout->screen->id);
    dune_id_free(main, layout->screen);
  }
  lib_freelinkn(&workspace->layouts, layout);
}

void dune_workspace_relations_free(ListB *relation_list)
{
  for (WorkSpaceDataRelation *relation = relation_list->first, *relation_next; relation;
       relation = relation_next) {
    relation_next = relation->next;
    workspace_relation_remove(relation_list, relation);
  }
}

/* General Utils */
WorkSpaceLayout *dune_workspace_layout_find(const WorkSpace *workspace, const Screen *screen)
{
  WorkSpaceLayout *layout = workspace_layout_find_exec(workspace, screen);
  if (layout) {
    return layout;
  }

  printf(
      "%s: Couldn't find layout in this workspace: '%s' screen: '%s'. "
      "This should not happen!\n",
      __func__,
      workspace->id.name + 2,
      screen->id.name + 2);

  return NULL;
}

WorkSpaceLayout *workspace_layout_find_global(const Main *main,
                                              const Screen *screen,
                                              WorkSpace **r_workspace)
{
  WorkSpaceLayout *layout;

  if (r_workspace) {
    *r_workspace = NULL;
  }

  for (WorkSpace *workspace = main->workspaces.first; workspace; workspace = workspace->id.next) {
    if ((layout = workspace_layout_find_exec(workspace, screen))) {
      if (r_workspace) {
        *r_workspace = workspace;
      }

      return layout;
    }
  }

  return NULL;
}

WorkSpaceLayout *dune_workspace_layout_iter_circular(const WorkSpace *workspace,
                                                    WorkSpaceLayout *start,
                                                    bool (*cb)(const WorkSpaceLayout *layout,
                                                                     void *arg),
                                                    void *arg,
                                                    const bool iter_backward)
{
  WorkSpaceLayout *iter_layout;

  if (iter_backward) {
    LIST_CIRCULAR_BACKWARD_BEGIN (&workspace->layouts, iter_layout, start) {
      if (!cb(iter_layout, arg)) {
        return iter_layout;
      }
    }
    LIST_CIRCULAR_BACKWARD_END(&workspace->layouts, iter_layout, start);
  }
  else {
    LIST_CIRCULAR_FORWARD_BEGIN (&workspace->layouts, iter_layout, start) {
      if (!cb(iter_layout, arg)) {
        return iter_layout;
      }
    }
    LIST_CIRCULAR_FORWARD_END(&workspace->layouts, iter_layout, start);
  }

  return NULL;
}

void workspace_tool_remove(struct WorkSpace *workspace, struct ToolRef *tref)
{
  if (tref->runtime) {
    mem_freen(tref->runtime);
  }
  if (tref->props) {
    IDP_FreeProp(tref->props);
  }
  lib_remlink(&workspace->tools, tref);
  mem_freen(tref);
}

bool workspace_owner_id_check(const WorkSpace *workspace, const char *owner_id)
{
  if ((*owner_id == '\0') || ((workspace->flags & WORKSPACE_USE_FILTER_BY_ORIGIN) == 0)) {
    return true;
  }

  /* We could use hash lookup, for now this list is highly likely under < ~16 items. */
  return LIB_findstring(&workspace->owner_ids, owner_id, offsetof(WinOwnerId, name)) != NULL;
}

void workspace_id_tag_all_visible(Main *main, int tag)
{
  main_id_tag_list(&main->workspaces, tag, false);
  wmWindowManager *wm = dunemain->wm.first;
  LIST_FOREACH (Win *, win, &wm->windows) {
    WorkSpace *workspace = KERNEL_workspace_active_get(win->workspace_hook);
    workspace->id.tag |= tag;
  }
}

/* -------------------------------------------------------------------- */
/** Getters/Setters */
WorkSpace *workspace_active_get(WorkSpaceInstanceHook *hook)
{
  return hook->active;
}
void workspace_active_set(WorkSpaceInstanceHook *hook, WorkSpace *workspace)
{
  /* DO NOT check for `hook->active == workspace` here. Caller code is supposed to do it if
   * that optimization is possible and needed.
   * This code can be called from places where we might have this equality, but still want to
   * ensure/update the active layout below.
   * Known case where this is buggy and will crash later due to NULL active layout: reading
   * a dune file, when the new read workspace ID happens to have the exact same memory address
   * as when it was saved in the blend file (extremely unlikely, but possible). */

  hook->active = workspace;
  if (workspace) {
    WorkSpaceLayout *layout = workspace_relation_get_data_matching_parent(
        &workspace->hook_layout_relations, hook);
    if (layout) {
      hook->act_layout = layout;
    }
  }
}

WorkSpaceLayout *workspace_active_layout_get(const WorkSpaceInstanceHook *hook)
{
  return hook->act_layout;
}

WorkSpaceLayout *workspace_active_layout_for_workspace_get(const WorkSpaceInstanceHook *hook,
                                                                  const WorkSpace *workspace)
{
  /* If the workspace is active, the active layout can be returned, no need for a lookup. */
  if (hook->active == workspace) {
    return hook->act_layout;
  }

  /* Inactive workspace */
  return workspace_relation_get_data_matching_parent(&workspace->hook_layout_relations, hook);
}

void workspace_active_layout_set(WorkSpaceInstanceHook *hook,
                                 const int winid,
                                 WorkSpace *workspace,
                                 WorkSpaceLayout *layout)
{
  hook->act_layout = layout;
  workspace_relation_ensure_updated(&workspace->hook_layout_relations, hook, winid, layout);
}

Screen *workspace_active_screen_get(const WorkSpaceInstanceHook *hook)
{
  return hook->act_layout->screen;
}
void workspace_active_screen_set(WorkSpaceInstanceHook *hook,
                                 const int winid,
                                 WorkSpace *workspace,
                                 Screen *screen)
{
  /* we need to find the WorkspaceLayout that wraps this screen */
  WorkSpaceLayout *layout = workspace_layout_find(hook->active, screen);
  workspace_active_layout_set(hook, winid, workspace, layout);
}

const char *workspace_layout_name_get(const WorkSpaceLayout *layout)
{
  return layout->name;
}
void workspace_layout_name_set(WorkSpace *workspace,
                               WorkSpaceLayout *layout,
                               const char *new_name)
{
  workspace_layout_name_set(workspace, layout, new_name);
}

Screen *workspace_layout_screen_get(const WorkSpaceLayout *layout)
{
  return layout->screen;
}
