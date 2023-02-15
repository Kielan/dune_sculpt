#include <stdlib.h>
#include <string.h>

#include "types_object.h"
#include "types_rigidbody.h"
#include "types_scene.h"

#ifdef WITH_BULLET
#  include "RBI_api.h"
#endif

#include "dune_context.h"
#include "dune_main.h"
#include "dune_report.h"
#include "dune_rigidbody.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

#include "api_access.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ED_screen.h"

#include "physics_intern.h"

/* ********************************************** */
/* API */

/* check if there is an active rigid body world */
static bool ed_rigidbody_world_active_poll(dContext *C)
{
  Scene *scene = ctx_data_scene(C);
  return (scene && scene->rigidbody_world);
}
static bool ed_rigidbody_world_add_poll(dContext *C)
{
  Scene *scene = ctx_data_scene(C);
  return (scene && scene->rigidbody_world == NULL);
}

/* ********************************************** */
/* OPERATORS - Management */

/* ********** Add RigidBody World **************** */

static int rigidbody_world_add_exec(dContext *C, wmOperator *UNUSED(op))
{
  Main *dmain = ctx_data_main(C);
  Scene *scene = ctx_data_scene(C);
  RigidBodyWorld *rbw;

  rbw = dune_rigidbody_create_world(scene);
  //  dune_rigidbody_validate_sim_world(scene, rbw, false);
  scene->rigidbody_world = rbw;

  /* Full rebuild of DEG! */
  DEG_relations_tag_update(dmain);
  DEG_id_tag_update_ex(dmain, &scene->id, ID_RECALC_ANIMATION);

  return OPERATOR_FINISHED;
}

void RIGIDBODY_OT_world_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->idname = "RIGIDBODY_OT_world_add";
  ot->name = "Add Rigid Body World";
  ot->description = "Add Rigid Body simulation world to the current scene";

  /* callbacks */
  ot->exec = rigidbody_world_add_exec;
  ot->poll = ed_rigidbody_world_add_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ********** Remove RigidBody World ************* */

static int rigidbody_world_remove_exec(dContext *C, wmOperator *op)
{
  Main *dmain = ctx_data_main(C);
  Scene *scene = ctx_data_scene(C);
  RigidBodyWorld *rbw = scene->rigidbody_world;

  /* sanity checks */
  if (ELEM(NULL, scene, rbw)) {
    dune_report(op->reports, RPT_ERROR, "No Rigid Body World to remove");
    return OPERATOR_CANCELLED;
  }

  dune_rigidbody_free_world(scene);

  /* Full rebuild of DEG! */
  deg_relations_tag_update(dmain);
  deg_id_tag_update_ex(dmain, &scene->id, ID_RECALC_ANIMATION);

  /* done */
  return OPERATOR_FINISHED;
}

void RIGIDBODY_OT_world_remove(wmOperatorType *ot)
{
  /* identifiers */
  ot->idname = "RIGIDBODY_OT_world_remove";
  ot->name = "Remove Rigid Body World";
  ot->description = "Remove Rigid Body simulation world from the current scene";

  /* callbacks */
  ot->exec = rigidbody_world_remove_exec;
  ot->poll = ed_rigidbody_world_active_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ********************************************** */
/* UTILITY OPERATORS */

/* ********** Export RigidBody World ************* */

static int rigidbody_world_export_exec(dContext *C, wmOperator *op)
{
  Scene *scene = ctx_data_scene(C);
  RigidBodyWorld *rbw = scene->rigidbody_world;
  char path[FILE_MAX];

  /* sanity checks */
  if (ELEM(NULL, scene, rbw)) {
    dune_report(op->reports, RPT_ERROR, "No Rigid Body World to export");
    return OPERATOR_CANCELLED;
  }
  if (rbw->shared->physics_world == NULL) {
    dune_report(
        op->reports, RPT_ERROR, "Rigid Body World has no associated physics data to export");
    return OPERATOR_CANCELLED;
  }

  api_string_get(op->ptr, "filepath", path);
#ifdef WITH_BULLET
  RB_dworld_export(rbw->shared->physics_world, path);
#endif
  return OPERATOR_FINISHED;
}

static int rigidbody_world_export_invoke(dContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  if (!api_struct_prop_is_set(op->ptr, "relative_path")) {
    api_bool_set(op->ptr, "relative_path", (U.flag & USER_RELPATHS) != 0);
  }

  if (api_struct_prop_is_set(op->ptr, "filepath")) {
    return rigidbody_world_export_exec(C, op);
  }

  /* TODO: use the actual rigidbody world's name + .bullet instead of this temp crap */
  api_string_set(op->ptr, "filepath", "rigidbodyworld_export.bullet");
  wm_event_add_fileselect(C, op);

  return OPERATOR_RUNNING_MODAL;
}

void RIGIDBODY_OT_world_export(wmOperatorType *ot)
{
  /* identifiers */
  ot->idname = "RIGIDBODY_OT_world_export";
  ot->name = "Export Rigid Body World";
  ot->description =
      "Export Rigid Body world to simulator's own fileformat (i.e. '.bullet' for Bullet Physics)";

  /* callbacks */
  ot->invoke = rigidbody_world_export_invoke;
  ot->exec = rigidbody_world_export_exec;
  ot->poll = ED_rigidbody_world_active_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  wm_op_props_filesel(ot,
                                 FILE_TYPE_FOLDER,
                                 FILE_SPECIAL,
                                 FILE_SAVE,
                                 FILE_RELPATH,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);
}
