#include <stdio.h>
#include <string.h>

#include "lib_compiler_attrs.h"
#include "lib_list.h"
#include "lib_string.h"

#include "types_seq.h"

#include "dune_cxt.h"
#include "dune_global.h"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_main.h"
#include "dune_node.h"
#include "dune_report.h"
#include "dune_scene.h"

#include "graph.h"
#include "graph_build.h"

#include "lang.h"

#include "ed_object.h"
#include "ed_render.h"
#include "ed_scene.h"
#include "ed_screen.h"
#include "ed_util.h"

#include "seq_relations.h"
#include "seq_select.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "wm_api.h"
#include "wm_types.h"

/* Scene Utilities */
static Scene *scene_add(Main *main, Scene *scene_old, eSceneCopyMethod method)
{
  Scene *scene_new = NULL;
  if (method == SCE_COPY_NEW) {
    scene_new = dune_scene_add(main, DATA_("Scene"));
  }
  else { /* different kinds of copying */
    /* We are going to deep-copy collections, objects and various object data, we need to have
     * up-to-date obdata for that. */
    if (method == SCE_COPY_FULL) {
      ed_editors_flush_edits(main);
    }

    scene_new = dune_scene_duplicate(main, scene_old, method);
  }

  return scene_new;
}

/* Add a new scene in the seq editor. */
static Scene *ed_scene_seq_add(Main *main, Cxt *C, eSceneCopyMethod method)
{
  Seq *seq = NULL;
  Scene *scene_active = cxt_data_scene(C);
  Scene *scene_strip = NULL;
  /* Sequencer need to use as base the scene defined in the strip, not the main scene. */
  Editing *ed = scene_active->ed;
  if (ed) {
    seq = ed->act_seq;
    if (seq && seq->scene) {
      scene_strip = seq->scene;
    }
  }

  /* If no scene assigned to the strip, only NEW scene mode is logic. */
  if (scene_strip == NULL) {
    method = SCE_COPY_NEW;
  }

  Scene *scene_new = scene_add(main, scene_strip, method);

  /* As the scene is created in seq, do not set the new scene as active.
   * This is useful for story-boarding where we want to keep actual scene active.
   * The new scene is linked to the active strip and the viewport updated. */
  if (scene_new && seq) {
    seq->scene = scene_new;
    /* Do a refresh of the sequencer data. */
    seq_relations_invalidate_cache_raw(scene_active, seq);
    graph_id_tag_update(&scene_active->id, ID_RECALC_AUDIO | ID_RECALC_SEQUENCER_STRIPS);
    graph_relations_tag_update(main);
  }

  wm_event_add_notifier(C, NC_SCENE | ND_SE, scene_active);
  wm_event_add_notifier(C, NC_SCENE | ND_SCENEBROWSE, scene_active);

  return scene_new;
}

Scene *ed_scene_add(Main *duneMain, Cxt *C, Window *win, eSceneCopyMethod method)
{
  Scene *scene_old = wm_window_get_active_scene(win);
  Scene *scene_new = scene_add(duneMain, scene_old, method);

  wm_window_set_active_scene(duneMain, C, win, scene_new);

  wm_event_add_notifier(C, NC_SCENE | ND_SCENEBROWSE, scene_new);

  return scene_new;
}

bool ed_scene_delete(Cxt *C, Main *duneMain, Scene *scene)
{
  Scene *scene_new;

  /* kill running jobs */
  WindowManager *wm = duneMain->wm.first;
  wm_jobs_kill_type(wm, scene, WM_JOB_TYPE_ANY);

  if (scene->id.prev) {
    scene_new = scene->id.prev;
  }
  else if (scene->id.next) {
    scene_new = scene->id.next;
  }
  else {
    return false;
  }

  LIST_FOREACH (Window *, win, &wm->windows) {
    if (win->parent != NULL) { /* We only care about main windows here... */
      continue;
    }
    if (win->scene == scene) {
      wm_window_set_active_scene(duneMain, C, win, scene_new);
    }
  }

  dune_id_delete(duneMain, scene);

  return true;
}

void ed_scene_change_update(Main *main, Scene *scene, ViewLayer *layer)
{
  Graph *graph = dune_scene_ensure_graph(main, scene, layer);

  dune_scene_set_background(main, scene);
  graph_relations_update(graph);
  graph_tag_on_visible_update(main, false);

  ed_render_engine_changed(main, false);
  ed_update_for_newframe(main, graph);
}

static bool view_layer_remove_poll(const Scene *scene, const ViewLayer *layer)
{
  const int act = lib_findindex(&scene->view_layers, layer);

  if (act == -1) {
    return false;
  }
  if ((scene->view_layers.first == scene->view_layers.last) &&
      (scene->view_layers.first == layer)) {
    /* ensure 1 layer is kept */
    return false;
  }

  return true;
}

static void view_layer_remove_unset_nodetrees(const Main *duneMain, Scene *scene, ViewLayer *layer)
{
  int act_layer_index = lib_findindex(&scene->view_layers, layer);

  for (Scene *sce = duneMain->scenes.first; sce; sce = sce->id.next) {
    if (sce->nodetree) {
      DUNE_nodetree_remove_layer_n(sce->nodetree, scene, act_layer_index);
    }
  }
}

bool ed_scene_view_layer_delete(Main *main, Scene *scene, ViewLayer *layer, ReportList *reports)
{
  if (view_layer_remove_poll(scene, layer) == false) {
    if (reports) {
      dune_reportf(reports,
                  RPT_ERROR,
                  "View layer '%s' could not be removed from scene '%s'",
                  layer->name,
                  scene->id.name + 2);
    }

    return false;
  }

  /* We need to unset nodetrees before removing the layer, otherwise its index will be -1. */
  view_layer_remove_unset_nodetrees(main, scene, layer);

  lib_remlink(&scene->view_layers, layer);
  lib_assert(lib_list_is_empty(&scene->view_layers) == false);

  /* Remove from windows. */
  WindowManager *wm = duneMain->wm.first;
  LIST_FOREACH (Window *, win, &wm->windows) {
    if (win->scene == scene && STREQ(win->view_layer_name, layer->name)) {
      ViewLayer *first_layer = dune_view_layer_default_view(scene);
      STRNCPY(win->view_layer_name, first_layer->name);
    }
  }

  dune_scene_free_view_layer_graph(scene, layer);

  dune_view_layer_free(layer);

  graph_id_tag_update(&scene->id, 0);
  graph_relations_tag_update(duneMain);
  wm_main_add_notifier(NC_SCENE | ND_LAYER | NA_REMOVED, scene);

  return true;
}

/* Scene New Op */
static int scene_new_ex(Cxt *C, wmOp *op)
{
  Main *duneMain = cxt_data_main(C);
  Window *win = cxt_wm_window(C);
  int type = api_enum_get(op->ptr, "type");

  ed_scene_add(duneMain, C, win, type);

  return OP_FINISHED;
}

static EnumPropItem scene_new_items[] = {
    {SCE_COPY_NEW, "NEW", 0, "New", "Add a new, empty scene with default settings"},
    {SCE_COPY_EMPTY,
     "EMPTY",
     0,
     "Copy Settings",
     "Add a new, empty scene, and copy settings from the current scene"},
    {SCE_COPY_LINK_COLLECTION,
     "LINK_COPY",
     0,
     "Linked Copy",
     "Link in the collections from the current scene (shallow copy)"},
    {SCE_COPY_FULL, "FULL_COPY", 0, "Full Copy", "Make a full copy of the current scene"},
    {0, NULL, 0, NULL, NULL},
};

static void SCENE_OT_new(wmOpType *ot)
{

  /* identifiers */
  ot->name = "New Scene";
  ot->description = "Add new scene by type";
  ot->idname = "SCENE_OT_new";

  /* api callbacks */
  ot->exec = scene_new_ex;
  ot->invoke = wm_menu_invoke;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(ot->sapi, "type", scene_new_items, SCE_COPY_NEW, "Type", "");
}

/** Scene New Seq Operator **/
static int scene_new_seq_ex(Cxt *C, wmOp *op)
{
  Main *duneMain = cxt_data_main(C);
  int type = api_enum_get(op->ptr, "type");

  if (ed_scene_seq_add(duneMain, C, type) == NULL) {
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

static bool scene_new_seq_poll(Cxt *C)
{
  Scene *scene = cxt_data_scene(C);
  const Seq *seq = seq_select_active_get(scene);
  return (seq && (seq->type == SEQ_TYPE_SCENE));
}

static const EnumPropItem *scene_new_seq_enum_itemf(Cxt *C,
                                                    ApiPtr *UNUSED(ptr),
                                                    ApiProp *UNUSED(prop),
                                                    bool *r_free)
{
  EnumPropItem *item = NULL;
  int totitem = 0;
  uint item_index;

  item_index = api_enum_from_value(scene_new_items, SCE_COPY_NEW);
  api_enum_item_add(&item, &totitem, &scene_new_items[item_index]);

  bool has_scene_or_no_cxt = false;
  if (C == NULL) {
    /* For documentation generation. */
    has_scene_or_no_cxt = true;
  }
  else {
    Scene *scene = cxt_data_scene(C);
    Seq *seq = seq_select_active_get(scene);
    if ((seq && (seq->type == SEQ_TYPE_SCENE) && (seq->scene != NULL))) {
      has_scene_or_no_cxt = true;
    }
  }

  if (has_scene_or_no_cxt) {
    int values[] = {SCE_COPY_EMPTY, SCE_COPY_LINK_COLLECTION, SCE_COPY_FULL};
    for (int i = 0; i < ARRAY_SIZE(values); i++) {
      item_index = api_enum_from_value(scene_new_items, values[i]);
      api_enum_item_add(&item, &totitem, &scene_new_items[item_index]);
    }
  }

  api_enum_item_end(&item, &totitem);
  *r_free = true;
  return item;
}

static void SCENE_OT_new_seq(wmOpType *ot)
{

  /* identifiers */
  ot->name = "New Scene";
  ot->description = "Add new scene by type in the sequence editor and assign to active strip";
  ot->idname = "SCENE_OT_new_seq";

  /* api callbacks */
  ot->ex = scene_new_seq_ex;
  ot->invoke = wm_menu_invoke;
  ot->poll = scene_new_seq_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  ot->prop = api_def_enum(ot->sapi, "type", scene_new_items, SCE_COPY_NEW, "Type", "");
  api_def_enum_fns(ot->prop, scene_new_seq_enum_itemf);
  api_def_prop_flag(ot->prop, PROP_ENUM_NO_TRANSLATE);
}

/** Scene Delete Op **/
static bool scene_delete_poll(Cxt *C)
{
  Main *main = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  return dune_scene_can_be_removed(main, scene);
}

static int scene_delete_exec(Cxt *C, wmOp *UNUSED(op))
{
  Scene *scene = cxt_data_scene(C);

  if (ed_scene_delete(C, cxt_data_main(C), scene) == false) {
    return OP_CANCELLED;
  }

  if (G.debug & G_DEBUG) {
    printf("scene delete %p\n", scene);
  }

  wm_event_add_notifier(C, NC_SCENE | NA_REMOVED, scene);

  return OP_FINISHED;
}

static void SCENE_OT_delete(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Delete Scene";
  ot->description = "Delete active scene";
  ot->idname = "SCENE_OT_delete";

  /* api callbacks */
  ot->ex = scene_delete_ex;
  ot->poll = scene_delete_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** Registration **/

void ED_operatortypes_scene(void)
{
  WM_operatortype_append(SCENE_OT_new);
  WM_operatortype_append(SCENE_OT_delete);
  WM_operatortype_append(SCENE_OT_new_sequencer);
}
