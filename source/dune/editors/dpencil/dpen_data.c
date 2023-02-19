/** \file
 * \ingroup edgpencil
 *
 * Operators for dealing with GP data-blocks and layers.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_string_utils.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_anim_types.h"
#include "DNA_brush_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"

#include "BKE_anim_data.h"
#include "BKE_animsys.h"
#include "BKE_brush.h"
#include "BKE_context.h"
#include "BKE_deform.h"
#include "BKE_fcurve_driver.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_paint.h"
#include "BKE_report.h"
#include "BKE_scene.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "ED_gpencil.h"
#include "ED_object.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "gpencil_intern.h"

/* ************************************************ */
/* Datablock Operators */

/* ******************* Add New Data ************************ */
static bool gpencil_data_add_poll(bContext *C)
{

  /* the base line we have is that we have somewhere to add Grease Pencil data */
  return ED_annotation_data_get_pointers(C, NULL) != NULL;
}

/* add new datablock - wrapper around API */
static int gpencil_data_add_exec(bContext *C, wmOperator *op)
{
  PointerRNA gpd_owner = {NULL};
  bGPdata **gpd_ptr = ED_annotation_data_get_pointers(C, &gpd_owner);

  if (gpd_ptr == NULL) {
    BKE_report(op->reports, RPT_ERROR, "Nowhere for grease pencil data to go");
    return OPERATOR_CANCELLED;
  }

  /* decrement user count and add new datablock */
  /* TODO: if a datablock exists,
   * we should make a copy of it instead of starting fresh (as in other areas) */
  Main *bmain = CTX_data_main(C);

  /* decrement user count of old GP datablock */
  if (*gpd_ptr) {
    bGPdata *gpd = (*gpd_ptr);
    id_us_min(&gpd->id);
  }

  /* Add new datablock, with a single layer ready to use
   * (so users don't have to perform an extra step). */
  bGPdata *gpd = BKE_gpencil_data_addnew(bmain, DATA_("Annotations"));
  *gpd_ptr = gpd;

  /* tag for annotations */
  gpd->flag |= GP_DATA_ANNOTATIONS;

  /* add new layer (i.e. a "note") */
  BKE_gpencil_layer_addnew(*gpd_ptr, DATA_("Note"), true, false);

  /* notifiers */
  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_annotation_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Annotation Add New";
  ot->idname = "GPENCIL_OT_annotation_add";
  ot->description = "Add new Annotation data-block";
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* callbacks */
  ot->exec = gpencil_data_add_exec;
  ot->poll = gpencil_data_add_poll;
}

/* ******************* Unlink Data ************************ */

/* poll callback for adding data/layers - special */
static bool gpencil_data_unlink_poll(bContext *C)
{
  bGPdata **gpd_ptr = ED_annotation_data_get_pointers(C, NULL);

  /* only unlink annotation datablocks */
  if ((gpd_ptr != NULL) && (*gpd_ptr != NULL)) {
    bGPdata *gpd = (*gpd_ptr);
    if ((gpd->flag & GP_DATA_ANNOTATIONS) == 0) {
      return false;
    }
  }
  /* if we have access to some active data, make sure there's a datablock before enabling this */
  return (gpd_ptr && *gpd_ptr);
}

/* unlink datablock - wrapper around API */
static int gpencil_data_unlink_exec(bContext *C, wmOperator *op)
{
  bGPdata **gpd_ptr = ED_annotation_data_get_pointers(C, NULL);

  if (gpd_ptr == NULL) {
    BKE_report(op->reports, RPT_ERROR, "Nowhere for grease pencil data to go");
    return OPERATOR_CANCELLED;
  }
  /* just unlink datablock now, decreasing its user count */
  bGPdata *gpd = (*gpd_ptr);

  id_us_min(&gpd->id);
  *gpd_ptr = NULL;

  /* notifiers */
  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_data_unlink(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Annotation Unlink";
  ot->idname = "GPENCIL_OT_data_unlink";
  ot->description = "Unlink active Annotation data-block";
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* callbacks */
  ot->exec = gpencil_data_unlink_exec;
  ot->poll = gpencil_data_unlink_poll;
}

/* ************************************************ */
/* Layer Operators */

/* ******************* Add New Layer ************************ */

/* add new layer - wrapper around API */
static int gpencil_layer_add_exec(bContext *C, wmOperator *op)
{
  const bool is_annotation = STREQ(op->idname, "GPENCIL_OT_layer_annotation_add");

  PointerRNA gpd_owner = {NULL};
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  bGPdata *gpd = NULL;

  if (is_annotation) {
    bGPdata **gpd_ptr = ED_annotation_data_get_pointers(C, &gpd_owner);
    /* if there's no existing Grease-Pencil data there, add some */
    if (gpd_ptr == NULL) {
      BKE_report(op->reports, RPT_ERROR, "Nowhere for grease pencil data to go");
      return OPERATOR_CANCELLED;
    }
    /* Annotations */
    if (*gpd_ptr == NULL) {
      *gpd_ptr = BKE_gpencil_data_addnew(bmain, DATA_("Annotations"));
    }

    /* mark as annotation */
    (*gpd_ptr)->flag |= GP_DATA_ANNOTATIONS;
    BKE_gpencil_layer_addnew(*gpd_ptr, DATA_("Note"), true, false);
    gpd = *gpd_ptr;
  }
  else {
    /* GP Object */
    Object *ob = CTX_data_active_object(C);
    if ((ob != NULL) && (ob->type == OB_GPENCIL)) {
      gpd = (bGPdata *)ob->data;
      bGPDlayer *gpl = BKE_gpencil_layer_addnew(gpd, DATA_("GP_Layer"), true, false);
      /* Add a new frame to make it visible in Dopesheet. */
      if (gpl != NULL) {
        gpl->actframe = BKE_gpencil_layer_frame_get(gpl, CFRA, GP_GETFRAME_ADD_NEW);
      }
    }
  }

  /* notifiers */
  if (gpd) {
    DEG_id_tag_update(&gpd->id,
                      ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
  }
  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_layer_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add New Layer";
  ot->idname = "GPENCIL_OT_layer_add";
  ot->description = "Add new layer or note for the active data-block";

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* callbacks */
  ot->exec = gpencil_layer_add_exec;
  ot->poll = gpencil_add_poll;
}

static bool gpencil_add_annotation_poll(bContext *C)
{
  return ED_annotation_data_get_pointers(C, NULL) != NULL;
}

void GPENCIL_OT_layer_annotation_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add New Annotation Layer";
  ot->idname = "GPENCIL_OT_layer_annotation_add";
  ot->description = "Add new Annotation layer or note for the active data-block";

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* callbacks */
  ot->exec = gpencil_layer_add_exec;
  ot->poll = gpencil_add_annotation_poll;
}
/* ******************* Remove Active Layer ************************* */

static int gpencil_layer_remove_exec(bContext *C, wmOperator *op)
{
  const bool is_annotation = STREQ(op->idname, "GPENCIL_OT_layer_annotation_remove");

  bGPdata *gpd = (!is_annotation) ? ED_gpencil_data_get_active(C) :
                                    ED_annotation_data_get_active(C);
  bGPDlayer *gpl = BKE_gpencil_layer_active_get(gpd);

  /* sanity checks */
  if (ELEM(NULL, gpd, gpl)) {
    return OPERATOR_CANCELLED;
  }

  if (gpl->flag & GP_LAYER_LOCKED) {
    BKE_report(op->reports, RPT_ERROR, "Cannot delete locked layers");
    return OPERATOR_CANCELLED;
  }

  /* make the layer before this the new active layer
   * - use the one after if this is the first
   * - if this is the only layer, this naturally becomes NULL
   */
  if (gpl->prev) {
    BKE_gpencil_layer_active_set(gpd, gpl->prev);
  }
  else {
    BKE_gpencil_layer_active_set(gpd, gpl->next);
  }

  /* delete the layer now... */
  BKE_gpencil_layer_delete(gpd, gpl);

  /* Reorder masking. */
  BKE_gpencil_layer_mask_sort_all(gpd);

  /* notifiers */
  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_SELECTED, NULL);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_layer_remove(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Layer";
  ot->idname = "GPENCIL_OT_layer_remove";
  ot->description = "Remove active Grease Pencil layer";

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* callbacks */
  ot->exec = gpencil_layer_remove_exec;
  ot->poll = gpencil_active_layer_poll;
}

static bool gpencil_active_layer_annotation_poll(bContext *C)
{
  bGPdata *gpd = ED_annotation_data_get_active(C);
  bGPDlayer *gpl = BKE_gpencil_layer_active_get(gpd);

  return (gpl != NULL);
}

void GPENCIL_OT_layer_annotation_remove(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Annotation Layer";
  ot->idname = "GPENCIL_OT_layer_annotation_remove";
  ot->description = "Remove active Annotation layer";

  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* callbacks */
  ot->exec = gpencil_layer_remove_exec;
  ot->poll = gpencil_active_layer_annotation_poll;
}
/* ******************* Move Layer Up/Down ************************** */

enum {
  GP_LAYER_MOVE_UP = -1,
  GP_LAYER_MOVE_DOWN = 1,
};

static int gpencil_layer_move_exec(bContext *C, wmOperator *op)
{
  const bool is_annotation = STREQ(op->idname, "GPENCIL_OT_layer_annotation_move");

  bGPdata *gpd = (!is_annotation) ? ED_gpencil_data_get_active(C) :
                                    ED_annotation_data_get_active(C);
  bGPDlayer *gpl = BKE_gpencil_layer_active_get(gpd);

  const int direction = RNA_enum_get(op->ptr, "type") * -1;

  /* sanity checks */
  if (ELEM(NULL, gpd, gpl)) {
    return OPERATOR_CANCELLED;
  }

  BLI_assert(ELEM(direction, -1, 0, 1)); /* we use value below */
  if (BLI_listbase_link_move(&gpd->layers, gpl, direction)) {
    /* Reorder masking. */
    BKE_gpencil_layer_mask_sort_all(gpd);

    DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  }

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_layer_move(wmOperatorType *ot)
{
  static const EnumPropertyItem slot_move[] = {
      {GP_LAYER_MOVE_UP, "UP", 0, "Up", ""},
      {GP_LAYER_MOVE_DOWN, "DOWN", 0, "Down", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Move Grease Pencil Layer";
  ot->idname = "GPENCIL_OT_layer_move";
  ot->description = "Move the active Grease Pencil layer up/down in the list";

  /* api callbacks */
  ot->exec = gpencil_layer_move_exec;
  ot->poll = gpencil_active_layer_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = RNA_def_enum(ot->srna, "type", slot_move, 0, "Type", "");
}

void GPENCIL_OT_layer_annotation_move(wmOperatorType *ot)
{
  static const EnumPropertyItem slot_move[] = {
      {GP_LAYER_MOVE_UP, "UP", 0, "Up", ""},
      {GP_LAYER_MOVE_DOWN, "DOWN", 0, "Down", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Move Annotation Layer";
  ot->idname = "GPENCIL_OT_layer_annotation_move";
  ot->description = "Move the active Annotation layer up/down in the list";

  /* api callbacks */
  ot->exec = gpencil_layer_move_exec;
  ot->poll = gpencil_active_layer_annotation_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = RNA_def_enum(ot->srna, "type", slot_move, 0, "Type", "");
}
/* ********************* Duplicate Layer ************************** */
enum {
  GP_LAYER_DUPLICATE_ALL = 0,
  GP_LAYER_DUPLICATE_EMPTY = 1,
};

static int gpencil_layer_copy_exec(bContext *C, wmOperator *op)
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  bGPDlayer *gpl = BKE_gpencil_layer_active_get(gpd);
  bGPDlayer *new_layer;
  const int mode = RNA_enum_get(op->ptr, "mode");
  const bool dup_strokes = (bool)(mode == GP_LAYER_DUPLICATE_ALL);
  /* sanity checks */
  if (ELEM(NULL, gpd, gpl)) {
    return OPERATOR_CANCELLED;
  }

  /* Make copy of layer, and add it immediately after or before the existing layer. */
  new_layer = BKE_gpencil_layer_duplicate(gpl, true, dup_strokes);
  if (dup_strokes) {
    BLI_insertlinkafter(&gpd->layers, gpl, new_layer);
  }
  else {
    /* For empty strokes is better add below. */
    BLI_insertlinkbefore(&gpd->layers, gpl, new_layer);
  }

  /* ensure new layer has a unique name, and is now the active layer */
  BLI_uniquename(&gpd->layers,
                 new_layer,
                 DATA_("GP_Layer"),
                 '.',
                 offsetof(bGPDlayer, info),
                 sizeof(new_layer->info));
  BKE_gpencil_layer_active_set(gpd, new_layer);

  /* notifiers */
  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_SELECTED, NULL);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_layer_duplicate(wmOperatorType *ot)
{
  static const EnumPropertyItem copy_mode[] = {
      {GP_LAYER_DUPLICATE_ALL, "ALL", 0, "All Data", ""},
      {GP_LAYER_DUPLICATE_EMPTY, "EMPTY", 0, "Empty Keyframes", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Duplicate Layer";
  ot->idname = "GPENCIL_OT_layer_duplicate";
  ot->description = "Make a copy of the active Grease Pencil layer";

  /* callbacks */
  ot->exec = gpencil_layer_copy_exec;
  ot->poll = gpencil_active_layer_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_enum(ot->srna, "mode", copy_mode, GP_LAYER_DUPLICATE_ALL, "Mode", "");
}

/* ********************* Duplicate Layer in a new object ************************** */
enum {
  GP_LAYER_COPY_OBJECT_ALL_FRAME = 0,
  GP_LAYER_COPY_OBJECT_ACT_FRAME = 1,
};

static bool gpencil_layer_duplicate_object_poll(bContext *C)
{
  Object *ob = CTX_data_active_object(C);
  if ((ob == NULL) || (ob->type != OB_GPENCIL)) {
    return false;
  }

  bGPdata *gpd = (bGPdata *)ob->data;
  bGPDlayer *gpl = BKE_gpencil_layer_active_get(gpd);

  if (gpl == NULL) {
    return false;
  }

  return true;
}

static int gpencil_layer_duplicate_object_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  const bool only_active = RNA_boolean_get(op->ptr, "only_active");
  const int mode = RNA_enum_get(op->ptr, "mode");

  Object *ob_src = CTX_data_active_object(C);
  bGPdata *gpd_src = (bGPdata *)ob_src->data;
  bGPDlayer *gpl_active = BKE_gpencil_layer_active_get(gpd_src);

  CTX_DATA_BEGIN (C, Object *, ob, selected_objects) {
    if ((ob == ob_src) || (ob->type != OB_GPENCIL)) {
      continue;
    }
    bGPdata *gpd_dst = (bGPdata *)ob->data;
    LISTBASE_FOREACH_BACKWARD (bGPDlayer *, gpl_src, &gpd_src->layers) {
      if ((only_active) && (gpl_src != gpl_active)) {
        continue;
      }
      /* Create new layer (adding at head of the list). */
      bGPDlayer *gpl_dst = BKE_gpencil_layer_addnew(gpd_dst, gpl_src->info, true, true);
      /* Need to copy some variables (not all). */
      gpl_dst->onion_flag = gpl_src->onion_flag;
      gpl_dst->thickness = gpl_src->thickness;
      gpl_dst->line_change = gpl_src->line_change;
      copy_v4_v4(gpl_dst->tintcolor, gpl_src->tintcolor);
      gpl_dst->opacity = gpl_src->opacity;

      /* Create all frames. */
      LISTBASE_FOREACH (bGPDframe *, gpf_src, &gpl_src->frames) {

        if ((mode == GP_LAYER_COPY_OBJECT_ACT_FRAME) && (gpf_src != gpl_src->actframe)) {
          continue;
        }

        /* Create new frame. */
        bGPDframe *gpf_dst = BKE_gpencil_frame_addnew(gpl_dst, gpf_src->framenum);

        /* Copy strokes. */
        LISTBASE_FOREACH (bGPDstroke *, gps_src, &gpf_src->strokes) {

          /* Make copy of source stroke. */
          bGPDstroke *gps_dst = BKE_gpencil_stroke_duplicate(gps_src, true, true);

          /* Check if material is in destination object,
           * otherwise add the slot with the material. */
          Material *ma_src = BKE_object_material_get(ob_src, gps_src->mat_nr + 1);
          if (ma_src != NULL) {
            int idx = BKE_gpencil_object_material_ensure(bmain, ob, ma_src);

            /* Reassign the stroke material to the right slot in destination object. */
            gps_dst->mat_nr = idx;
          }

          /* Add new stroke to frame. */
          BLI_addtail(&gpf_dst->strokes, gps_dst);
        }
      }
    }
    /* notifiers */
    DEG_id_tag_update(&gpd_dst->id,
                      ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
    DEG_id_tag_update(&ob->id, ID_RECALC_COPY_ON_WRITE);
  }
  CTX_DATA_END;

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_layer_duplicate_object(wmOperatorType *ot)
{
  PropertyRNA *prop;

  static const EnumPropertyItem copy_mode[] = {
      {GP_LAYER_COPY_OBJECT_ALL_FRAME, "ALL", 0, "All Frames", ""},
      {GP_LAYER_COPY_OBJECT_ACT_FRAME, "ACTIVE", 0, "Active Frame", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Duplicate Layer to New Object";
  ot->idname = "GPENCIL_OT_layer_duplicate_object";
  ot->description = "Make a copy of the active Grease Pencil layer to selected object";

  /* callbacks */
  ot->exec = gpencil_layer_duplicate_object_exec;
  ot->poll = gpencil_layer_duplicate_object_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = RNA_def_enum(ot->srna, "mode", copy_mode, GP_LAYER_COPY_OBJECT_ALL_FRAME, "Mode", "");

  prop = RNA_def_boolean(ot->srna,
                         "only_active",
                         true,
                         "Only Active",
                         "Copy only active Layer, uncheck to append all layers");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);
}

/* ********************* Duplicate Frame ************************** */
enum {
  GP_FRAME_DUP_ACTIVE = 0,
  GP_FRAME_DUP_ALL = 1,
};

static int gpencil_frame_duplicate_exec(bContext *C, wmOperator *op)
{
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  bGPDlayer *gpl_active = BKE_gpencil_layer_active_get(gpd);
  Scene *scene = CTX_data_scene(C);

  int mode = RNA_enum_get(op->ptr, "mode");

  /* sanity checks */
  if (ELEM(NULL, gpd, gpl_active)) {
    return OPERATOR_CANCELLED;
  }

  if (mode == 0) {
    BKE_gpencil_frame_addcopy(gpl_active, CFRA);
  }
  else {
    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
      if ((gpl->flag & GP_LAYER_LOCKED) == 0) {
        BKE_gpencil_frame_addcopy(gpl, CFRA);
      }
    }
  }
  /* notifiers */
  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_frame_duplicate(wmOperatorType *ot)
{
  static const EnumPropertyItem duplicate_mode[] = {
      {GP_FRAME_DUP_ACTIVE, "ACTIVE", 0, "Active", "Duplicate frame in active layer only"},
      {GP_FRAME_DUP_ALL, "ALL", 0, "All", "Duplicate active frames in all layers"},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Duplicate Frame";
  ot->idname = "GPENCIL_OT_frame_duplicate";
  ot->description = "Make a copy of the active Grease Pencil Frame";

  /* callbacks */
  ot->exec = gpencil_frame_duplicate_exec;
  ot->poll = gpencil_active_layer_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = RNA_def_enum(ot->srna, "mode", duplicate_mode, GP_FRAME_DUP_ACTIVE, "Mode", "");
}

/* ********************* Clean Fill Boundaries on Frame ************************** */
enum {
  GP_FRAME_CLEAN_FILL_ACTIVE = 0,
  GP_FRAME_CLEAN_FILL_ALL = 1,
};

static int gpencil_frame_clean_fill_exec(bContext *C, wmOperator *op)
{
  bool changed = false;
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  int mode = RNA_enum_get(op->ptr, "mode");

  CTX_DATA_BEGIN (C, bGPDlayer *, gpl, editable_gpencil_layers) {
    bGPDframe *init_gpf = gpl->actframe;
    if (mode == GP_FRAME_CLEAN_FILL_ALL) {
      init_gpf = gpl->frames.first;
    }

    for (bGPDframe *gpf = init_gpf; gpf; gpf = gpf->next) {
      if ((gpf == gpl->actframe) || (mode == GP_FRAME_CLEAN_FILL_ALL)) {

        if (gpf == NULL) {
          continue;
        }

        /* simply delete strokes which are no fill */
        LISTBASE_FOREACH_MUTABLE (bGPDstroke *, gps, &gpf->strokes) {
          /* skip strokes that are invalid for current view */
          if (ED_gpencil_stroke_can_use(C, gps) == false) {
            continue;
          }

          /* free stroke */
          if (gps->flag & GP_STROKE_NOFILL) {
            /* free stroke memory arrays, then stroke itself */
            if (gps->points) {
              MEM_freeN(gps->points);
            }
            if (gps->dvert) {
              BKE_gpencil_free_stroke_weights(gps);
              MEM_freeN(gps->dvert);
            }
            MEM_SAFE_FREE(gps->triangles);
            BLI_freelinkN(&gpf->strokes, gps);

            changed = true;
          }
        }
      }
    }
  }
  CTX_DATA_END;

  /* notifiers */
  if (changed) {
    DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  }

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_frame_clean_fill(wmOperatorType *ot)
{
  static const EnumPropertyItem duplicate_mode[] = {
      {GP_FRAME_CLEAN_FILL_ACTIVE, "ACTIVE", 0, "Active Frame Only", "Clean active frame only"},
      {GP_FRAME_CLEAN_FILL_ALL, "ALL", 0, "All Frames", "Clean all frames in all layers"},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Clean Fill Boundaries";
  ot->idname = "GPENCIL_OT_frame_clean_fill";
  ot->description = "Remove 'no fill' boundary strokes";

  /* callbacks */
  ot->exec = gpencil_frame_clean_fill_exec;
  ot->poll = gpencil_active_layer_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = RNA_def_enum(ot->srna, "mode", duplicate_mode, GP_FRAME_DUP_ACTIVE, "Mode", "");
}

/* ********************* Clean Loose Boundaries on Frame ************************** */
static int gpencil_frame_clean_loose_exec(bContext *C, wmOperator *op)
{
  bool changed = false;
  bGPdata *gpd = ED_gpencil_data_get_active(C);
  int limit = RNA_int_get(op->ptr, "limit");
  const bool is_multiedit = (bool)GPENCIL_MULTIEDIT_SESSIONS_ON(gpd);

  CTX_DATA_BEGIN (C, bGPDlayer *, gpl, editable_gpencil_layers) {
    bGPDframe *init_gpf = (is_multiedit) ? gpl->frames.first : gpl->actframe;

    for (bGPDframe *gpf = init_gpf; gpf; gpf = gpf->next) {
      if ((gpf == gpl->actframe) || ((gpf->flag & GP_FRAME_SELECT) && (is_multiedit))) {
        if (gpf == NULL) {
          continue;
        }

        /* simply delete strokes which are no loose */
        LISTBASE_FOREACH_MUTABLE (bGPDstroke *, gps, &gpf->strokes) {
          /* skip strokes that are invalid for current view */
          if (ED_gpencil_stroke_can_use(C, gps) == false) {
            continue;
          }

          /* free stroke */
          if (gps->totpoints <= limit) {
            /* free stroke memory arrays, then stroke itself */
            if (gps->points) {
              MEM_freeN(gps->points);
            }
            if (gps->dvert) {
              BKE_gpencil_free_stroke_weights(gps);
              MEM_freeN(gps->dvert);
            }
            MEM_SAFE_FREE(gps->triangles);
            BLI_freelinkN(&gpf->strokes, gps);

            changed = true;
          }
        }
      }

      /* If not multi-edit, exit loop. */
      if (!is_multiedit) {
        break;
      }
    }
  }
  CTX_DATA_END;

  /* notifiers */
  if (changed) {
    DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  }

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_frame_clean_loose(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clean Loose Points";
  ot->idname = "GPENCIL_OT_frame_clean_loose";
  ot->description = "Remove loose points";

  /* callbacks */
  ot->exec = gpencil_frame_clean_loose_exec;
  ot->poll = gpencil_active_layer_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  RNA_def_int(ot->srna,
              "limit",
              1,
              1,
              INT_MAX,
              "Limit",
              "Number of points to consider stroke as loose",
              1,
              INT_MAX);
}

/* ********************* Clean Duplicated Frames ************************** */
static bool gpencil_frame_is_equal(const bGPDframe *gpf_a, const bGPDframe *gpf_b)
{
  if ((gpf_a == NULL) || (gpf_b == NULL)) {
    return false;
  }
  /* If the number of strokes is different, cannot be equal. */
  const int totstrokes_a = BLI_listbase_count(&gpf_a->strokes);
  const int totstrokes_b = BLI_listbase_count(&gpf_b->strokes);
  if ((totstrokes_a == 0) || (totstrokes_b == 0) || (totstrokes_a != totstrokes_b)) {
    return false;
  }
  /* Loop all strokes and check. */
  const bGPDstroke *gps_a = gpf_a->strokes.first;
  const bGPDstroke *gps_b = gpf_b->strokes.first;
  for (int i = 0; i < totstrokes_a; i++) {
    /* If the number of points is different, cannot be equal. */
    if (gps_a->totpoints != gps_b->totpoints) {
      return false;
    }
    /* Check other variables. */
    if (!equals_v4v4(gps_a->vert_color_fill, gps_b->vert_color_fill)) {
      return false;
    }
    if (gps_a->thickness != gps_b->thickness) {
      return false;
    }
    if (gps_a->mat_nr != gps_b->mat_nr) {
      return false;
    }
    if (gps_a->caps[0] != gps_b->caps[0]) {
      return false;
    }
    if (gps_a->caps[1] != gps_b->caps[1]) {
      return false;
    }
    if (gps_a->hardeness != gps_b->hardeness) {
      return false;
    }
    if (!equals_v2v2(gps_a->aspect_ratio, gps_b->aspect_ratio)) {
      return false;
    }
    if (gps_a->uv_rotation != gps_b->uv_rotation) {
      return false;
    }
    if (!equals_v2v2(gps_a->uv_translation, gps_b->uv_translation)) {
      return false;
    }
    if (gps_a->uv_scale != gps_b->uv_scale) {
      return false;
    }

    /* Loop points and check if equals or not. */
    for (int p = 0; p < gps_a->totpoints; p++) {
      const bGPDspoint *pt_a = &gps_a->points[p];
      const bGPDspoint *pt_b = &gps_b->points[p];
      if (!equals_v3v3(&pt_a->x, &pt_b->x)) {
        return false;
      }
      if (pt_a->pressure != pt_b->pressure) {
        return false;
      }
      if (pt_a->strength != pt_b->strength) {
        return false;
      }
      if (pt_a->uv_fac != pt_b->uv_fac) {
        return false;
      }
      if (pt_a->uv_rot != pt_b->uv_rot) {
        return false;
      }
      if (!equals_v4v4(pt_a->vert_color, pt_b->vert_color)) {
        return false;
      }
    }

    /* Look at next pair of strokes. */
    gps_a = gps_a->next;
    gps_b = gps_b->next;
  }

  return true;
}

static int gpencil_frame_clean_duplicate_exec(bContext *C, wmOperator *op)
{
#define SELECTED 1

  bool changed = false;
  Object *ob = CTX_data_active_object(C);
  bGPdata *gpd = (bGPdata *)ob->data;
  const int type = RNA_enum_get(op->ptr, "type");

  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    /* Only editable and visible layers are considered. */
    if (BKE_gpencil_layer_is_editable(gpl) && (gpl->frames.first != NULL)) {
      bGPDframe *gpf = gpl->frames.first;

      if ((type == SELECTED) && ((gpf->flag & GP_FRAME_SELECT) == 0)) {
        continue;
      }

      while (gpf != NULL) {
        if (gpencil_frame_is_equal(gpf, gpf->next)) {
          /* Remove frame. */
          BKE_gpencil_layer_frame_delete(gpl, gpf->next);
          /* Tag for recalc. */
          changed = true;
        }
        else {
          gpf = gpf->next;
        }
      }
    }
  }

  /* notifiers */
  if (changed) {
    DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
    WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  }

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_frame_clean_duplicate(wmOperatorType *ot)
{
  static const EnumPropertyItem clean_type[] = {
      {0, "ALL", 0, "All Frames", ""},
      {1, "SELECTED", 0, "Selected Frames", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Clean Duplicated Frames";
  ot->idname = "GPENCIL_OT_frame_clean_duplicate";
  ot->description = "Remove any duplicated frame";

  /* callbacks */
  ot->exec = gpencil_frame_clean_duplicate_exec;
  ot->poll = gpencil_active_layer_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  ot->prop = RNA_def_enum(ot->srna, "type", clean_type, 0, "Type", "");
}

/* *********************** Hide Layers ******************************** */
