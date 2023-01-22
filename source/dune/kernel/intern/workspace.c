#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LI_listbase.h"
#include "LI_string.h"
#include "LI_string_utils.h"
#include "LI_utildefines.h"

#include "T_translation.h"

#include "KE_asset.h"
#include "KE_global.h"
#include "KE_idprop.h"
#include "KE_idtype.h"
#include "KE_lib_id.h"
#include "KE_lib_query.h"
#include "KE_main.h"
#include "KE_object.h"
#include "KE_scene.h"
#include "KE_workspace.h"

#include "structs_object_types.h"
#include "structs_scene_types.h"
#include "structs_screen_types.h"
#include "structs_windowmanager_types.h"
#include "structs_workspace_types.h"

#include "DEG_depsgraph.h"

#include "MEM_guardedalloc.h"

#include "LOADER_read_write.h"

/* -------------------------------------------------------------------- */

static void workspace_init_data(ID *id)
{
  WorkSpace *workspace = (WorkSpace *)id;

  KERNEL_asset_library_reference_init_default(&workspace->asset_library_ref);
}

static void workspace_free_data(ID *id)
{
  WorkSpace *workspace = (WorkSpace *)id;

  KERNEL_workspace_relations_free(&workspace->hook_layout_relations);

  LIB_freelistN(&workspace->owner_ids);
  LIB_freelistN(&workspace->layouts);

  while (!LIB_listbase_is_empty(&workspace->tools)) {
    KERNEL_workspace_tool_remove(workspace, workspace->tools.first);
  }

  MEM_SAFE_FREE(workspace->status_text);
}

static void workspace_foreach_id(ID *id, LibraryForeachIDData *data)
{
  WorkSpace *workspace = (WorkSpace *)id;

  LISTBASE_FOREACH (WorkSpaceLayout *, layout, &workspace->layouts) {
    KERNEL_LIB_FOREACHID_PROCESS_IDSUPER(data, layout->screen, IDWALK_CB_USER);
  }
}

static void workspace_dune_write(DuneWriter *writer, ID *id, const void *id_address)
{
  WorkSpace *workspace = (WorkSpace *)id;

  LOADER_write_id_struct(writer, WorkSpace, id_address, &workspace->id);
  KERNEL_id_dune_write(writer, &workspace->id);
  LOADER_write_struct_list(writer, WorkSpaceLayout, &workspace->layouts);
  LOADER_write_struct_list(writer, WorkSpaceDataRelation, &workspace->hook_layout_relations);
  LOADER_write_struct_list(writer, wmOwnerID, &workspace->owner_ids);
  LOADER_write_struct_list(writer, bToolRef, &workspace->tools);
  LISTBASE_FOREACH (bToolRef *, tref, &workspace->tools) {
    if (tref->properties) {
      IDP_DuneWrite(writer, tref->properties);
    }
  }
}

static void workspace_dune_read_data(DuneDataReader *reader, ID *id)
{
  WorkSpace *workspace = (WorkSpace *)id;

  LOADER_read_list(reader, &workspace->layouts);
  LOADER_read_list(reader, &workspace->hook_layout_relations);
  LOADER_read_list(reader, &workspace->owner_ids);
  LOADER_read_list(reader, &workspace->tools);

  LISTBASE_FOREACH (WorkSpaceDataRelation *, relation, &workspace->hook_layout_relations) {
    /* Parent pointer does not belong to workspace data and is therefore restored in lib_link step
     * of window manager. */
    LOADER_read_data_address(reader, &relation->value);
  }

  LISTBASE_FOREACH (bToolRef *, tref, &workspace->tools) {
    tref->runtime = NULL;
    LOADER_read_data_address(reader, &tref->properties);
    IDP_DuneDataRead(reader, &tref->properties);
  }

  workspace->status_text = NULL;

  id_us_ensure_real(&workspace->id);
}

static void workspace_dune_read_lib(DuneLibReader *reader, ID *id)
{
  WorkSpace *workspace = (WorkSpace *)id;
  Main *dunemain = LOADER_read_lib_get_main(reader);

  /* Restore proper 'parent' pointers to relevant data, and clean up unused/invalid entries. */
  LISTBASE_FOREACH_MUTABLE (WorkSpaceDataRelation *, relation, &workspace->hook_layout_relations) {
    relation->parent = NULL;
    LISTBASE_FOREACH (wmWindowManager *, wm, &dunemain->wm) {
      LISTBASE_FOREACH (wmWindow *, win, &wm->windows) {
        if (win->winid == relation->parentid) {
          relation->parent = win->workspace_hook;
        }
      }
    }
    if (relation->parent == NULL) {
      LIB_freelinkN(&workspace->hook_layout_relations, relation);
    }
  }

  LISTBASE_FOREACH_MUTABLE (WorkSpaceLayout *, layout, &workspace->layouts) {
    LOADER_read_id_address(reader, id->lib, &layout->screen);

    if (layout->screen) {
      if (ID_IS_LINKED(id)) {
        layout->screen->winid = 0;
        if (layout->screen->temp) {
          /* delete temp layouts when appending */
          KERNEL_workspace_layout_remove(dunemain, workspace, layout);
        }
      }
    }
    else {
      /* If we're reading a layout without screen stored, it's useless and we shouldn't keep it
       * around. */
      KERNEL_workspace_layout_remove(dunemain, workspace, layout);
    }
  }
}

static void workspace_dune_read_expand(duneExpander *expander, ID *id)
{
  WorkSpace *workspace = (WorkSpace *)id;

  LISTBASE_FOREACH (WorkSpaceLayout *, layout, &workspace->layouts) {
    LOADER_expand(expander, KERNEL_workspace_layout_screen_get(layout));
  }
}

IDTypeInfo IDType_ID_WS = {
    .id_code = ID_WS,
    .id_filter = FILTER_ID_WS,
    .main_listbase_index = INDEX_ID_WS,
    .struct_size = sizeof(WorkSpace),
    .name = "WorkSpace",
    .name_plural = "workspaces",
    .translation_context = TRANSLATION_I18NCONTEXT_ID_WORKSPACE,
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

/* -------------------------------------------------------------------- */
/** Internal Utils */

static void workspace_layout_name_set(WorkSpace *workspace,
                                      WorkSpaceLayout *layout,
                                      const char *new_name)
{
  LIB_strncpy(layout->name, new_name, sizeof(layout->name));
  LIB_uniquename(&workspace->layouts,
                 layout,
                 "Layout",
                 '.',
                 offsetof(WorkSpaceLayout, name),
                 sizeof(layout->name));
}

/**
 * This should only be used directly when it is to be expected that there isn't
 * a layout within a workspace that wraps a screen. Usually - especially outside
 * of KERNEL_workspace - KERNEL_workspace_layout_find should be used!
 */
static WorkSpaceLayout *workspace_layout_find_exec(const WorkSpace *workspace,
                                                   const duneScreen *screen)
{
  return LIB_findptr(&workspace->layouts, screen, offsetof(WorkSpaceLayout, screen));
}

static void workspace_relation_add(ListBase *relation_list,
                                   void *parent,
                                   const int parentid,
                                   void *data)
{
  WorkSpaceDataRelation *relation = MEM_callocN(sizeof(*relation), __func__);
  relation->parent = parent;
  relation->parentid = parentid;
  relation->value = data;
  /* add to head, if we switch back to it soon we find it faster. */
  LIB_addhead(relation_list, relation);
}
static void workspace_relation_remove(ListBase *relation_list, WorkSpaceDataRelation *relation)
{
  LIB_remlink(relation_list, relation);
  MEM_freeN(relation);
}

static void workspace_relation_ensure_updated(ListBase *relation_list,
                                              void *parent,
                                              const int parentid,
                                              void *data)
{
  WorkSpaceDataRelation *relation = LIB_listbase_bytes_find(
      relation_list, &parentid, sizeof(parentid), offsetof(WorkSpaceDataRelation, parentid));
  if (relation != NULL) {
    relation->parent = parent;
    relation->value = data;
    /* reinsert at the head of the list, so that more commonly used relations are found faster. */
    LIB_remlink(relation_list, relation);
    LIB_addhead(relation_list, relation);
  }
  else {
    /* no matching relation found, add new one */
    workspace_relation_add(relation_list, parent, parentid, data);
  }
}

static void *workspace_relation_get_data_matching_parent(const ListBase *relation_list,
                                                         const void *parent)
{
  WorkSpaceDataRelation *relation = LIB_findptr(
      relation_list, parent, offsetof(WorkSpaceDataRelation, parent));
  if (relation != NULL) {
    return relation->value;
  }

  return NULL;
}

/**
 * Checks if a screen is already used within any workspace. A screen should never be assigned to
 * multiple WorkSpaceLayouts, but that should be ensured outside of the KERNEL_workspace module
 * and without such checks.
 * Hence, this should only be used as assert check before assigning a screen to a workspace.
 */
#ifndef NDEBUG
static bool workspaces_is_screen_used
#else
static bool UNUSED_FUNCTION(workspaces_is_screen_used)
#endif
    (const Main *dunemain, duneScreen *screen)
{
  for (WorkSpace *workspace = dunemain->workspaces.first; workspace; workspace = workspace->id.next) {
    if (workspace_layout_find_exec(workspace, screen)) {
      return true;
    }
  }

  return false;
}

/* -------------------------------------------------------------------- */
/** Create, Delete, Init */

WorkSpace *KERNEL_workspace_add(Main *dunemain, const char *name)
{
  WorkSpace *new_workspace = KERNEL_id_new(dunemain, ID_WS, name);
  id_us_ensure_real(&new_workspace->id);
  return new_workspace;
}

void KERNEL_workspace_remove(Main *dunemain, WorkSpace *workspace)
{
  for (WorkSpaceLayout *layout = workspace->layouts.first, *layout_next; layout;
       layout = layout_next) {
    layout_next = layout->next;
    KERNEL_workspace_layout_remove(dunemain, workspace, layout);
  }
  KERNEL_id_free(dunemain, workspace);
}

WorkSpaceInstanceHook *KERNEL_workspace_instance_hook_create(const Main *dunemain, const int winid)
{
  WorkSpaceInstanceHook *hook = MEM_callocN(sizeof(WorkSpaceInstanceHook), __func__);

  /* set an active screen-layout for each possible window/workspace combination */
  for (WorkSpace *workspace = dunemain->workspaces.first; workspace; workspace = workspace->id.next) {
    KERNEL_workspace_active_layout_set(hook, winid, workspace, workspace->layouts.first);
  }

  return hook;
}
void KERNEL_workspace_instance_hook_free(const Main *dunemain, WorkSpaceInstanceHook *hook)
{
  /* workspaces should never be freed before wm (during which we call this function).
   * However, when running in background mode, loading a dune file may allocate windows (that need
   * to be freed) without creating workspaces. This happens in DunefileLoadingBaseTest. */
  LIB_assert(!LIB_listbase_is_empty(&dunemain->workspaces) || G.background);

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

  MEM_freeN(hook);
}

WorkSpaceLayout *KERNEL_workspace_layout_add(Main *dunemain,
                                          WorkSpace *workspace,
                                          duneScreen *screen,
                                          const char *name)
{
  WorkSpaceLayout *layout = MEM_callocN(sizeof(*layout), __func__);

  LIB_assert(!workspaces_is_screen_used(bmain, screen));
#ifndef DEBUG
  UNUSED_VARS(bmain);
#endif
  layout->screen = screen;
  id_us_plus(&layout->screen->id);
  workspace_layout_name_set(workspace, layout, name);
  LIB_addtail(&workspace->layouts, layout);

  return layout;
}

void KERNEL_workspace_layout_remove(Main *dunemain, WorkSpace *workspace, WorkSpaceLayout *layout)
{
  /* Screen should usually be set, but we call this from file reading to get rid of invalid
   * layouts. */
  if (layout->screen) {
    id_us_min(&layout->screen->id);
    KERNEL_id_free(dunemain, layout->screen);
  }
  LIB_freelinkN(&workspace->layouts, layout);
}

void KERNEL_workspace_relations_free(ListBase *relation_list)
{
  for (WorkSpaceDataRelation *relation = relation_list->first, *relation_next; relation;
       relation = relation_next) {
    relation_next = relation->next;
    workspace_relation_remove(relation_list, relation);
  }
}

/* -------------------------------------------------------------------- */
/** General Utils */

WorkSpaceLayout *KERNEL_workspace_layout_find(const WorkSpace *workspace, const duneScreen *screen)
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

WorkSpaceLayout *KERNEL_workspace_layout_find_global(const Main *dunemain,
                                                  const duneScreen *screen,
                                                  WorkSpace **r_workspace)
{
  WorkSpaceLayout *layout;

  if (r_workspace) {
    *r_workspace = NULL;
  }

  for (WorkSpace *workspace = bmain->workspaces.first; workspace; workspace = workspace->id.next) {
    if ((layout = workspace_layout_find_exec(workspace, screen))) {
      if (r_workspace) {
        *r_workspace = workspace;
      }

      return layout;
    }
  }

  return NULL;
}

WorkSpaceLayout *KERNEL_workspace_layout_iter_circular(const WorkSpace *workspace,
                                                    WorkSpaceLayout *start,
                                                    bool (*callback)(const WorkSpaceLayout *layout,
                                                                     void *arg),
                                                    void *arg,
                                                    const bool iter_backward)
{
  WorkSpaceLayout *iter_layout;

  if (iter_backward) {
    LISTBASE_CIRCULAR_BACKWARD_BEGIN (&workspace->layouts, iter_layout, start) {
      if (!callback(iter_layout, arg)) {
        return iter_layout;
      }
    }
    LISTBASE_CIRCULAR_BACKWARD_END(&workspace->layouts, iter_layout, start);
  }
  else {
    LISTBASE_CIRCULAR_FORWARD_BEGIN (&workspace->layouts, iter_layout, start) {
      if (!callback(iter_layout, arg)) {
        return iter_layout;
      }
    }
    LISTBASE_CIRCULAR_FORWARD_END(&workspace->layouts, iter_layout, start);
  }

  return NULL;
}

void KERNEL_workspace_tool_remove(struct WorkSpace *workspace, struct bToolRef *tref)
{
  if (tref->runtime) {
    MEM_freeN(tref->runtime);
  }
  if (tref->properties) {
    IDP_FreeProperty(tref->properties);
  }
  LIB_remlink(&workspace->tools, tref);
  MEM_freeN(tref);
}

bool KERNEL_workspace_owner_id_check(const WorkSpace *workspace, const char *owner_id)
{
  if ((*owner_id == '\0') || ((workspace->flags & WORKSPACE_USE_FILTER_BY_ORIGIN) == 0)) {
    return true;
  }

  /* We could use hash lookup, for now this list is highly likely under < ~16 items. */
  return LIB_findstring(&workspace->owner_ids, owner_id, offsetof(wmOwnerID, name)) != NULL;
}

void KERNEL_workspace_id_tag_all_visible(Main *bmain, int tag)
{
  KERNEL_main_id_tag_listbase(&dunemain->workspaces, tag, false);
  wmWindowManager *wm = dunemain->wm.first;
  LISTBASE_FOREACH (wmWindow *, win, &wm->windows) {
    WorkSpace *workspace = KERNEL_workspace_active_get(win->workspace_hook);
    workspace->id.tag |= tag;
  }
}

/* -------------------------------------------------------------------- */
/** Getters/Setters */

WorkSpace *KERNEL_workspace_active_get(WorkSpaceInstanceHook *hook)
{
  return hook->active;
}
void KERNEL_workspace_active_set(WorkSpaceInstanceHook *hook, WorkSpace *workspace)
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

WorkSpaceLayout *KERNEL_workspace_active_layout_get(const WorkSpaceInstanceHook *hook)
{
  return hook->act_layout;
}

WorkSpaceLayout *BKE_workspace_active_layout_for_workspace_get(const WorkSpaceInstanceHook *hook,
                                                               const WorkSpace *workspace)
{
  /* If the workspace is active, the active layout can be returned, no need for a lookup. */
  if (hook->active == workspace) {
    return hook->act_layout;
  }

  /* Inactive workspace */
  return workspace_relation_get_data_matching_parent(&workspace->hook_layout_relations, hook);
}

void KERNEL_workspace_active_layout_set(WorkSpaceInstanceHook *hook,
                                     const int winid,
                                     WorkSpace *workspace,
                                     WorkSpaceLayout *layout)
{
  hook->act_layout = layout;
  workspace_relation_ensure_updated(&workspace->hook_layout_relations, hook, winid, layout);
}

bScreen *KERNEL_workspace_active_screen_get(const WorkSpaceInstanceHook *hook)
{
  return hook->act_layout->screen;
}
void KERNEL_workspace_active_screen_set(WorkSpaceInstanceHook *hook,
                                     const int winid,
                                     WorkSpace *workspace,
                                     duneScreen *screen)
{
  /* we need to find the WorkspaceLayout that wraps this screen */
  WorkSpaceLayout *layout = KERNEL_workspace_layout_find(hook->active, screen);
  KERNEL_workspace_active_layout_set(hook, winid, workspace, layout);
}

const char *KERNEL_workspace_layout_name_get(const WorkSpaceLayout *layout)
{
  return layout->name;
}
void KERNEL_workspace_layout_name_set(WorkSpace *workspace,
                                   WorkSpaceLayout *layout,
                                   const char *new_name)
{
  workspace_layout_name_set(workspace, layout, new_name);
}

duneScreen *KERNEL_workspace_layout_screen_get(const WorkSpaceLayout *layout)
{
  return layout->screen;
}
