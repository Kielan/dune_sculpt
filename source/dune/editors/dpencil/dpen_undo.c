#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "types_dpen.h"
#include "types_listBase.h"
#include "types_object.h"
#include "types_windowmanager.h"

#include "lib_listbase.h"

#include "dune_undo.h"
#include "dune_context.h"
#include "dune_dpen.h"
#include "dune_undo_system.h"

#include "ed_dpen.h"

#include "wm_api.h"
#include "wm_types.h"

#include "DEG_depsgraph.h"

#include "dpen_intern.h"

typedef struct DPenUndoNode {
  struct DPenUndoNode *next, *prev;

  char name[NODE_UNDO_STR_MAX];
  struct DPenData *dpd;
} DPenUndoNode;

static ListBase undo_nodes = {NULL, NULL};
static DPenUndoNode *cur_node = NULL;

int ed_dpen_session_active(void)
{
  return (lib_listbase_is_empty(&undo_nodes) == false);
}

int ed_undo_dpen_step(dContext *C, const int step)
{
  DPendata **dpd_ptr = NULL, *new_dpd = NULL;

  fpd_ptr = ed_dpen_data_get_ptrs(C, NULL);

  const eUndoStepDir undo_step = (eUndoStepDir)step;
  if (undo_step == STEP_UNDO) {
    if (cur_node->prev) {
      cur_node = cur_node->prev;
      new_dpd = cur_node->dpd;
    }
  }
  else if (undo_step == STEP_REDO) {
    if (cur_node->next) {
      cur_node = cur_node->next;
      new_dpd = cur_node->dpd;
    }
  }

  if (new_dpd) {
    if (dpd_ptr) {
      if (*dpd_ptr) {
        DPenData *dpd = *dpd_ptr;
        DPenLayer *dpld;

        dune_dpen_free_layers(&dpd->layers);

        /* copy layers */
        lib_listbase_clear(&dpd->layers);

        LISTBASE_FOREACH (DPenLayer *, dpl, &dpd->layers) {
          /* make a copy of source layer and its data */
          dpld = dune_dpen_layer_duplicate(dpl, true, true);
          lib_addtail(&dpd->layers, dpld);
        }
      }
    }
    /* drawing batch cache is dirty now */
    DEG_id_tag_update(&new_dpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
    new_dpd->flag |= DPEN_DATA_CACHE_IS_DIRTY;
  }

  wm_event_add_notifier(C, NC_DPEN | NA_EDITED, NULL);

  return OP_FINISHED;
}

void dpen_undo_init(DPenData *dpd)
{
  dpen_undo_push(dpd);
}

static void dpen_undo_free_node(DPenUndoNode *undo_node)
{
  /* HACK: animdata wasn't duplicated, so it shouldn't be freed here,
   * or else the real copy will segfault when accessed
   */
  undo_node->dpd->adt = NULL;

  dune_dpen_free_data(undo_node->dpd, false);
  MEM_freeN(undo_node->dpd);
}

void dpen_undo_push(DPenData *dpd)
{
  DPenUndoNode *undo_node;

  if (cur_node) {
    /* Remove all undone nodes from stack. */
    undo_node = cur_node->next;

    while (undo_node) {
      DPenUndoNode *next_node = undo_node->next;

      dpen_undo_free_node(undo_node);
      lib_freelinkN(&undo_nodes, undo_node);

      undo_node = next_node;
    }
  }

  /* limit number of undo steps to the maximum undo steps
   * - to prevent running out of memory during **really**
   *   long drawing sessions (triggering swapping)
   */
  /* TODO: Undo-memory constraint is not respected yet,
   * but can be added if we have any need for it. */
  if (U.undosteps && !lib_listbase_is_empty(&undo_nodes)) {
    /* remove anything older than n-steps before cur_node */
    int steps = 0;

    undo_node = (cur_node) ? cur_node : undo_nodes.last;
    while (undo_node) {
      DPenUndoNode *prev_node = undo_node->prev;

      if (steps >= U.undosteps) {
        dpen_undo_free_node(undo_node);
        lib_freelinkN(&undo_nodes, undo_node);
      }

      steps++;
      undo_node = prev_node;
    }
  }

  /* create new undo node */
  undo_node = MEM_callocN(sizeof(DPenUndoNode), "dpen undo node");
  undo_node->dpd = dune_dpen_data_duplicate(NULL, dpd, true);

  cur_node = undo_node;

  lib_addtail(&undo_nodes, undo_node);
}

void dpen_undo_finish(void)
{
  DPenUndonmNode *undo_node = undo_nodes.first;

  while (undo_node) {
    dpen_undo_free_node(undo_node);
    undo_node = undo_node->next;
  }

  lib_freelistN(&undo_nodes);

  cur_node = NULL;
}
