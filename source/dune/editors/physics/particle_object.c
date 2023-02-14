#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_scene_types.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BKE_bvhutils.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_mesh.h"
#include "BKE_mesh_runtime.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_particle.h"
#include "BKE_pointcache.h"
#include "BKE_report.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_prototypes.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_object.h"
#include "ED_particle.h"
#include "ED_screen.h"

#include "UI_resources.h"

#include "particle_edit_utildefines.h"

#include "physics_intern.h"

static float I[4][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};

/********************** particle system slot operators *********************/

static int particle_system_add_exec(bContext *C, wmOperator *UNUSED(op))
{
  Main *bmain = CTX_data_main(C);
  Object *ob = ED_object_context(C);
  Scene *scene = CTX_data_scene(C);

  if (!scene || !ob) {
    return OPERATOR_CANCELLED;
  }

  object_add_particle_system(bmain, scene, ob, NULL);

  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, ob);
  WM_event_add_notifier(C, NC_OBJECT | ND_POINTCACHE, ob);

  return OPERATOR_FINISHED;
}

void OBJECT_OT_particle_system_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add Particle System Slot";
  ot->idname = "OBJECT_OT_particle_system_add";
  ot->description = "Add a particle system";

  /* api callbacks */
  ot->poll = ED_operator_object_active_local_editable;
  ot->exec = particle_system_add_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int particle_system_remove_exec(bContext *C, wmOperator *UNUSED(op))
{
  Main *bmain = CTX_data_main(C);
  Object *ob = ED_object_context(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  int mode_orig;

  if (!scene || !ob) {
    return OPERATOR_CANCELLED;
  }

  mode_orig = ob->mode;
  ParticleSystem *psys = psys_get_current(ob);
  object_remove_particle_system(bmain, scene, ob, psys);

  /* possible this isn't the active object
   * object_remove_particle_system() clears the mode on the last psys
   */
  if (mode_orig & OB_MODE_PARTICLE_EDIT) {
    if ((ob->mode & OB_MODE_PARTICLE_EDIT) == 0) {
      if (view_layer->basact && view_layer->basact->object == ob) {
        WM_event_add_notifier(C, NC_SCENE | ND_MODE | NS_MODE_OBJECT, NULL);
      }
    }
  }

  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, ob);
  WM_event_add_notifier(C, NC_OBJECT | ND_POINTCACHE, ob);

  return OPERATOR_FINISHED;
}

void OBJECT_OT_particle_system_remove(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Particle System Slot";
  ot->idname = "OBJECT_OT_particle_system_remove";
  ot->description = "Remove the selected particle system";

  /* api callbacks */
  ot->poll = ED_operator_object_active_local_editable;
  ot->exec = particle_system_remove_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/********************** new particle settings operator *********************/

static bool psys_poll(bContext *C)
{
  PointerRNA ptr = CTX_data_pointer_get_type(C, "particle_system", &RNA_ParticleSystem);
  return (ptr.data != NULL);
}

static int new_particle_settings_exec(bContext *C, wmOperator *UNUSED(op))
{
  Main *bmain = CTX_data_main(C);
  ParticleSystem *psys;
  ParticleSettings *part = NULL;
  Object *ob;
  PointerRNA ptr;

  ptr = CTX_data_pointer_get_type(C, "particle_system", &RNA_ParticleSystem);

  psys = ptr.data;

  /* add or copy particle setting */
  if (psys->part) {
    part = (ParticleSettings *)BKE_id_copy(bmain, &psys->part->id);
  }
  else {
    part = BKE_particlesettings_add(bmain, "ParticleSettings");
  }

  ob = (Object *)ptr.owner_id;

  if (psys->part) {
    id_us_min(&psys->part->id);
  }

  psys->part = part;

  psys_check_boid_data(psys);

  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_new(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "New Particle Settings";
  ot->idname = "PARTICLE_OT_new";
  ot->description = "Add new particle settings";

  /* api callbacks */
  ot->exec = new_particle_settings_exec;
  ot->poll = psys_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/********************** keyed particle target operators *********************/

static int new_particle_target_exec(bContext *C, wmOperator *UNUSED(op))
{
  Main *bmain = CTX_data_main(C);
  PointerRNA ptr = CTX_data_pointer_get_type(C, "particle_system", &RNA_ParticleSystem);
  ParticleSystem *psys = ptr.data;
  Object *ob = (Object *)ptr.owner_id;

  ParticleTarget *pt;

  if (!psys) {
    return OPERATOR_CANCELLED;
  }

  pt = psys->targets.first;
  for (; pt; pt = pt->next) {
    pt->flag &= ~PTARGET_CURRENT;
  }

  pt = MEM_callocN(sizeof(ParticleTarget), "keyed particle target");

  pt->flag |= PTARGET_CURRENT;
  pt->psys = 1;

  BLI_addtail(&psys->targets, pt);

  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_new_target(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "New Particle Target";
  ot->idname = "PARTICLE_OT_new_target";
  ot->description = "Add a new particle target";

  /* api callbacks */
  ot->exec = new_particle_target_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int remove_particle_target_exec(bContext *C, wmOperator *UNUSED(op))
{
  Main *bmain = CTX_data_main(C);
  PointerRNA ptr = CTX_data_pointer_get_type(C, "particle_system", &RNA_ParticleSystem);
  ParticleSystem *psys = ptr.data;
  Object *ob = (Object *)ptr.owner_id;

  ParticleTarget *pt;

  if (!psys) {
    return OPERATOR_CANCELLED;
  }

  pt = psys->targets.first;
  for (; pt; pt = pt->next) {
    if (pt->flag & PTARGET_CURRENT) {
      BLI_remlink(&psys->targets, pt);
      MEM_freeN(pt);
      break;
    }
  }
  pt = psys->targets.last;

  if (pt) {
    pt->flag |= PTARGET_CURRENT;
  }

  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);

  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, ob);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_target_remove(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Particle Target";
  ot->idname = "PARTICLE_OT_target_remove";
  ot->description = "Remove the selected particle target";

  /* api callbacks */
  ot->exec = remove_particle_target_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/************************ move up particle target operator *********************/

static int target_move_up_exec(bContext *C, wmOperator *UNUSED(op))
{
  PointerRNA ptr = CTX_data_pointer_get_type(C, "particle_system", &RNA_ParticleSystem);
  ParticleSystem *psys = ptr.data;
  Object *ob = (Object *)ptr.owner_id;
  ParticleTarget *pt;

  if (!psys) {
    return OPERATOR_CANCELLED;
  }

  pt = psys->targets.first;
  for (; pt; pt = pt->next) {
    if (pt->flag & PTARGET_CURRENT && pt->prev) {
      BLI_remlink(&psys->targets, pt);
      BLI_insertlinkbefore(&psys->targets, pt->prev, pt);

      DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
      WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, ob);
      break;
    }
  }

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_target_move_up(wmOperatorType *ot)
{
  ot->name = "Move Up Target";
  ot->idname = "PARTICLE_OT_target_move_up";
  ot->description = "Move particle target up in the list";

  ot->exec = target_move_up_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/************************ move down particle target operator *********************/

static int target_move_down_exec(bContext *C, wmOperator *UNUSED(op))
{
  PointerRNA ptr = CTX_data_pointer_get_type(C, "particle_system", &RNA_ParticleSystem);
  ParticleSystem *psys = ptr.data;
  Object *ob = (Object *)ptr.owner_id;
  ParticleTarget *pt;

  if (!psys) {
    return OPERATOR_CANCELLED;
  }
  pt = psys->targets.first;
  for (; pt; pt = pt->next) {
    if (pt->flag & PTARGET_CURRENT && pt->next) {
      BLI_remlink(&psys->targets, pt);
      BLI_insertlinkafter(&psys->targets, pt->next, pt);

      DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
      WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, ob);
      break;
    }
  }

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_target_move_down(wmOperatorType *ot)
{
  ot->name = "Move Down Target";
  ot->idname = "PARTICLE_OT_target_move_down";
  ot->description = "Move particle target down in the list";

  ot->exec = target_move_down_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/************************ refresh dupli objects *********************/

static int dupliob_refresh_exec(bContext *C, wmOperator *UNUSED(op))
{
  PointerRNA ptr = CTX_data_pointer_get_type(C, "particle_system", &RNA_ParticleSystem);
  ParticleSystem *psys = ptr.data;

  if (!psys) {
    return OPERATOR_CANCELLED;
  }

  psys_check_group_weights(psys->part);
  DEG_id_tag_update(&psys->part->id, ID_RECALC_GEOMETRY | ID_RECALC_PSYS_REDO);
  WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, NULL);

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_dupliob_refresh(wmOperatorType *ot)
{
  ot->name = "Refresh Instance Objects";
  ot->idname = "PARTICLE_OT_dupliob_refresh";
  ot->description = "Refresh list of instance objects and their weights";

  ot->exec = dupliob_refresh_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/************************ move up particle dupliweight operator *********************/

static int dupliob_move_up_exec(bContext *C, wmOperator *UNUSED(op))
{
  PointerRNA ptr = CTX_data_pointer_get_type(C, "particle_system", &RNA_ParticleSystem);
  ParticleSystem *psys = ptr.data;
  ParticleSettings *part;
  ParticleDupliWeight *dw;

  if (!psys) {
    return OPERATOR_CANCELLED;
  }

  part = psys->part;
  for (dw = part->instance_weights.first; dw; dw = dw->next) {
    if (dw->flag & PART_DUPLIW_CURRENT && dw->prev) {
      BLI_remlink(&part->instance_weights, dw);
      BLI_insertlinkbefore(&part->instance_weights, dw->prev, dw);

      DEG_id_tag_update(&part->id, ID_RECALC_GEOMETRY | ID_RECALC_PSYS_REDO);
      WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, NULL);
      break;
    }
  }

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_dupliob_move_up(wmOperatorType *ot)
{
  ot->name = "Move Up Instance Object";
  ot->idname = "PARTICLE_OT_dupliob_move_up";
  ot->description = "Move instance object up in the list";

  ot->exec = dupliob_move_up_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/********************** particle dupliweight operators *********************/

static int copy_particle_dupliob_exec(bContext *C, wmOperator *UNUSED(op))
{
  PointerRNA ptr = CTX_data_pointer_get_type(C, "particle_system", &RNA_ParticleSystem);
  ParticleSystem *psys = ptr.data;
  ParticleSettings *part;
  ParticleDupliWeight *dw;

  if (!psys) {
    return OPERATOR_CANCELLED;
  }
  part = psys->part;
  for (dw = part->instance_weights.first; dw; dw = dw->next) {
    if (dw->flag & PART_DUPLIW_CURRENT) {
      dw->flag &= ~PART_DUPLIW_CURRENT;
      dw = MEM_dupallocN(dw);
      dw->flag |= PART_DUPLIW_CURRENT;
      BLI_addhead(&part->instance_weights, dw);

      DEG_id_tag_update(&part->id, ID_RECALC_GEOMETRY | ID_RECALC_PSYS_REDO);
      WM_event_add_notifier(C, NC_OBJECT | ND_PARTICLE, NULL);
      break;
    }
  }

  return OPERATOR_FINISHED;
}

void PARTICLE_OT_dupliob_copy(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Copy Particle Instance Object";
  ot->idname = "PARTICLE_OT_dupliob_copy";
  ot->description = "Duplicate the current instance object";

  /* api callbacks */
  ot->exec = copy_particle_dupliob_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
