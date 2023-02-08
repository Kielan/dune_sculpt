#include <string.h>

#include "MEM_guardedalloc.h"

#include "TYPES_armature.h"
#include "TYPES_material.h"
#include "TYPES_modifier.h" /* for handling geometry nodes properties */
#include "TYPES_object.h"   /* for OB_DATA_SUPPORT_ID */
#include "TYPES_screen.h"
#include "TYPES_text.h"

#include "LIB_dunelib.h"
#include "LIB_math_color.h"

#include "BLF_api.h"
#include "I18N_lang.h"

#include "DUNE_context.h"
#include "DUNE_global.h"
#include "DUNE_idprop.h"
#include "DUNE_layer.h"
#include "DUNE_lib_id.h"
#include "DUNE_lib_override.h"
#include "DUNE_material.h"
#include "DUNE_node.h"
#include "DUNE_report.h"
#include "DUNE_screen.h"
#include "DUNE_text.h"

#include "IMB_colormanagement.h"

#include "DEG_depsgraph.h"

#include "API_access.h"
#include "API_define.h"
#include "API_prototypes.h"
#include "API_types.h"

#include "UI_interface.h"

#include "interface_intern.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_object.h"
#include "ED_paint.h"

/* for Copy As Driver */
#include "ED_keyframing.h"

/* only for UI_OT_editsource */
#include "DUNE_main.h"
#include "LIB_ghash.h"
#include "ED_screen.h"
#include "ED_text.h"

/* -------------------------------------------------------------------- */
/** Immediate redraw helper
 *
 * Generally handlers shouldn't do any redrawing, that includes the layout/button definitions. That
 * violates the Model-View-Controller pattern.
 *
 * But there are some operators which really need to re-run the layout definitions for various
 * reasons. For example, "Edit Source" does it to find out which exact Python code added a button.
 * Other operators may need to access buttons that aren't currently visible. In Blender's UI code
 * design that typically means just not adding the button in the first place, for a particular
 * redraw. So the operator needs to change context and re-create the layout, so the button becomes
 * available to act on.
 *
 **/

/* -------------------------------------------------------------------- */
/** Copy Python Command Operator */

static bool copy_pycmd_btnpoll(DuneContext *C)
{
  uiBtn *btn = ui_ctx_active_btn_get(C);

  if (btn && (btn->optype != NULL)) {
    return 1;
  }

  return 0;
}

static int copy_py_cmd_btnex(DuneContext *C, wmOperator *UNUSED(op))
{
  uiBtn *btn = ui_ctx_active_btn_get(C);

  if (btn && (btn->optype != NULL)) {
    ApiPtr *opptr;
    char *str;
    opptr = ui_btn_op_ptr_get(btn); /* allocated when needed, the button owns it */

    str = wm_op_pystring_ex(C, NULL, false, true, btn->optype, opptr);

    wm_clipboard_txt_set(str, 0);

    MEM_freeN(str);

    return OPERATOR_FINISHED;
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_copy_pycmd_btn(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Copy Python Command";
  ot->idname = "UI_OT_copy_pycmd_btnn";
  ot->description = "Copy the Python command matching this button";

  /* callbacks */
  ot->exec = copy_py_cmd_btn_ex;
  ot->poll = copy_py_cmd_btn_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/* -------------------------------------------------------------------- */
/** Reset to Default Values Button Operator **/

static int op_btnprop_finish(DuneContext *C, ApiPtr *ptr, ApiProp *prop)
{
  ID *id = ptr->owner_id;

  /* perform updates required for this property */
  apiprop_update(C, ptr, prop);

  /* as if we pressed the button */
  ui_ctx_active_btnprop_handle(C, false);

  /* Since we don't want to undo _all_ edits to settings, eg window
   * edits on the screen or on operator settings.
   * it might be better to move undo's inline - campbell */
  if (id && ID_CHECK_UNDO(id)) {
    /* do nothing, go ahead with undo */
    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

static int op_btnprop_finish_with_undo(DuneContext *C,
                                                     ApiPtr *ptr,
                                                     ApiProp *prop)
{
  /* Perform updates required for this property. */
  apiprop_update(C, ptr, prop);

  /* As if we pressed the button. */
  ui_ctx_active_btnprop_handle(C, true);

  return OPERATOR_FINISHED;
}

static bool reset_default_btnpoll(DuneContext *C)
{
  ApiProp ptr;
  ApiProp *prop;
  int index;

  ui_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  return (ptr.data && prop && apiprop_editable(&ptr, prop));
}

static int reset_default_btnEx(DuneContext *C, wmOperator *op)
{
  ApiPtr ptr;
  ApiProp *prop;
  int index;
  const bool all = api_bool_get(op->ptr, "all");

  /* try to reset the nominated setting to its default value */
  UI_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data && prop && apiProp_editable(&ptr, prop)) {
    if (ApiProp_reset(&ptr, prop, (all) ? -1 : index)) {
      return op_btnprop_finish_with_undo(C, &ptr, prop);
    }
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_reset_default_btn(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reset to Default Value";
  ot->idname = "UI_OT_reset_default_btn";
  ot->description = "Reset this property's value to its default value";

  /* callbacks */
  ot->poll = reset_default_btnpoll;
  ot->exec = reset_default_btnEx;

  /* flags */
  /* Don't set #OPTYPE_UNDO because #op_btnprop_finish_with_undo
   * is responsible for the undo push. */
  ot->flag = 0;

  /* properties */
  api_def_bool(ot->srna, "all", 1, "All", "Reset to default values all elements of the array");
}

/* -------------------------------------------------------------------- */
/** Assign Value as Default Button Operator **/

static bool assign_default_btn_poll(DuneContext *C)
{
  ApiProp ptr;
  ApiProp *prop;
  int index;

  ui_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  if (ptr.data && prop && ApiProp_editable(&ptr, prop)) {
    const PropType type = ApiProp_type(prop);

    return apiprop_is_idprop(prop) && !apiprop_array_check(prop) &&
           ELEM(type, PROP_INT, PROP_FLOAT);
  }

  return false;
}

static int assign_default_btnEx(DuneContext *C, wmOperator *UNUSED(op))
{
  ApiPtr ptr;
  ApiProp *prop;
  int index;

  /* try to reset the nominated setting to its default value */
  ui_ctx_active_btnProp_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data && prop && apiProp_editable(&ptr, prop)) {
    if (apiProp_assign_default(&ptr, prop)) {
      return op_btnProp_finish(C, &ptr, prop);
    }
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_assign_default_btn(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Assign Value as Default";
  ot->idname = "UI_OT_assign_default_button";
  ot->description = "Set this property's current value as the new default";

  /* callbacks */
  ot->poll = assign_default_btn_poll;
  ot->exec = assign_default_btn_ex;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** Unset Property Button Operator **/

static int unset_btnprop_ex(DuneContext *C, wmOperator *UNUSED(op))
{
  ApiProp ptr;
  ApiProp *prop;
  int index;

  /* try to unset the nominated property */
  ui_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data && prop && apiprop_editable(&ptr, prop) &&
      /* apiprop_is_idprop(prop) && */
      apiprop_is_set(&ptr, prop)) {
    apiprop_unset(&ptr, prop);
    return op_btnprop_finish(C, &ptr, prop);
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_unset_btnprop(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Unset Property";
  ot->idname = "UI_OT_unset_btnprop";
  ot->description = "Clear the property and use default or generated value in operators";

  /* callbacks */
  ot->poll = ED_op_regionactive;
  ot->exec = unset_btnprop_ex;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* -------------------------------------------------------------------- */
/** Define Override Type Operator */

/* Note that we use different values for UI/UX than 'real' override operations, user does not care
 * whether it's added or removed for the differential operation e.g. */
enum {
  UIOverride_Type_NOOP = 0,
  UIOverride_Type_Replace = 1,
  UIOverride_Type_Difference = 2, /* Add/subtract */
  UIOverride_Type_Factor = 3,     /* Multiply */
  /* TODO: should/can we expose insert/remove ones for collections? Doubt it... */
};

static EnumPropertyItem override_type_items[] = {
    {UIOverride_Type_NOOP,
     "NOOP",
     0,
     "NoOp",
     "'No-Operation', place holder preventing automatic override to ever affect the property"},
    {UIOverride_Type_Replace,
     "REPLACE",
     0,
     "Replace",
     "Completely replace value from linked data by local one"},
    {UIOverride_Type_Difference,
     "DIFFERENCE",
     0,
     "Difference",
     "Store difference to linked data value"},
    {UIOverride_Type_Factor,
     "FACTOR",
     0,
     "Factor",
     "Store factor to linked data value (useful e.g. for scale)"},
    {0, NULL, 0, NULL, NULL},
};

static bool override_type_set_btnpoll(DuneContext *C)
{
  ApiPtr ptr;
  ApiPtr *prop;
  int index;

  UI_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  const uint override_status = apiprop_override_lib_status(
      CTX_data_main(C), &ptr, prop, index);

  return (ptr.data && prop && (override_status & RNA_OVERRIDE_STATUS_OVERRIDABLE));
}

static int override_type_set_btnex(DuneContext *C, wmOperator *op)
{
  ApiProp ptr;
  ApiProp *prop;
  int index;
  bool created;
  const bool all = api_bool_get(op->ptr, "all");
  const int op_type = api_enum_get(op->ptr, "type");

  short operation;

  switch (op_type) {
    case UIOverride_Type_NOOP:
      operation = IDOVERRIDE_LIBRARY_OP_NOOP;
      break;
    case UIOverride_Type_Replace:
      operation = IDOVERRIDE_LIBRARY_OP_REPLACE;
      break;
    case UIOverride_Type_Difference:
      /* override code will automatically switch to subtract if needed. */
      operation = IDOVERRIDE_LIBRARY_OP_ADD;
      break;
    case UIOverride_Type_Factor:
      operation = IDOVERRIDE_LIBRARY_OP_MULTIPLY;
      break;
    default:
      operation = IDOVERRIDE_LIBRARY_OP_REPLACE;
      LIB_assert(0);
      break;
  }

  /* try to reset the nominated setting to its default value */
  UI_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  LIB_assert(ptr.owner_id != NULL);

  if (all) {
    index = -1;
  }

  IDOverrideLibPropOp *opop = apiprop_override_prop_op_get(
      CTX_data_main(C), &ptr, prop, operation, index, true, NULL, &created);

  if (opop == NULL) {
    /* Sometimes e.g. API cannot generate a path to the given property. */
    DUNE_reportf(op->reports, RPT_WARNING, "Failed to create the override operation");
    return OPERATOR_CANCELLED;
  }

  if (!created) {
    opop->operation = operation;
  }

  /* Outliner e.g. has to be aware of this change. */
  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return op_btnprop_finish(C, &ptr, prop);
}

static int override_type_set_btn_invoke(DuneContext *C,
                                           wmOperator *op,
                                           const wmEvent *UNUSED(event))
{
#if 0 /* Disabled for now */
  return WM_menu_invoke_ex(C, op, WM_OP_INVOKE_DEFAULT);
#else
  api_enum_set(op->ptr, "type", IDOVERRIDE_LIBRARY_OP_REPLACE);
  return override_type_set_btnex(C, op);
#endif
}

static void UI_OT_override_type_set_btn(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Define Override Type";
  ot->idname = "UI_OT_override_type_set_btn";
  ot->description = "Create an override operation, or set the type of an existing one";

  /* callbacks */
  ot->poll = override_type_set_btnpoll;
  ot->exec = override_type_set_btnex;
  ot->invoke = override_type_set_btninvoke;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  api_def_bool(ot->srna, "all", 1, "All", "Reset to default values all elements of the array");
  ot->prop = api_def_enum(ot->srna,
                          "type",
                          override_type_items,
                          UIOverride_Type_Replace,
                          "Type",
                          "Type of override operation");
  /* TODO: add itemf callback, not all options are available for all data types... */
}

static bool override_remove_btnpoll(DuneContext *C)
{
  ApiPtr ptr;
  ApiProp *prop;
  int index;

  UI_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  const uint override_status = ApiProp_override_lib_status(
      CTX_data_main(C), &ptr, prop, index);

  return (ptr.data && ptr.owner_id && prop && (override_status & API_OVERRIDE_STATUS_OVERRIDDEN));
}

static int override_remove_btnex(DuneContext *C, wmOperator *op)
{
  Main *duneMain = CTX_data_main(C);
  ApiPtr ptr, id_refptr, src;
  ApiPtr *prop;
  int index;
  const bool all = api_bool_get(op->ptr, "all");

  /* try to reset the nominated setting to its default value */
  UI_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  ID *id = ptr.owner_id;
  IDOverrideLibProp *oprop = apiprop_override_prop_find(duneMain, &ptr, prop, &id);
  LIB_assert(oprop != NULL);
  LIB_assert(id != NULL && id->override_library != NULL);

  const bool is_template = ID_IS_OVERRIDE_LIBRARY_TEMPLATE(id);

  /* We need source (i.e. linked data) to restore values of deleted overrides...
   * If this is an override template, we obviously do not need to restore anything. */
  if (!is_template) {
    ApiProp *src_prop;
    apiid_ptr_create(id->override_lib->reference, &id_refptr);
    if (!apipath_resolve_prop(&id_refptr, oprop->api_path, &src, &src_prop)) {
      LIB_assert_msg(0, "Failed to create matching source (linked data) API pointer");
    }
  }

  if (!all && index != -1) {
    bool is_strict_find;
    /* Remove override operation for given item,
     * add singular operations for the other items as needed. */
    IDOverrideLibraryPropertyOperation *opop = DUNE_lib_override_libprop_op_find(
        oprop, NULL, NULL, index, index, false, &is_strict_find);
    LIB_assert(opop != NULL);
    if (!is_strict_find) {
      /* No specific override operation, we have to get generic one,
       * and create item-specific override operations for all but given index,
       * before removing generic one. */
      for (int idx = apiprop_array_length(&ptr, prop); idx--;) {
        if (idx != index) {
          DUNE_lib_override_libprop_op_get(
              oprop, opop->operation, NULL, NULL, idx, idx, true, NULL, NULL);
        }
      }
    }
    DUNE_lib_override_libprop_op_delete(oprop, opop);
    if (!is_template) {
      apiprop_copy(duneMain, &ptr, &src, prop, index);
    }
    if (LIB_listbase_is_empty(&oprop->operations)) {
      DUNE_lib_override_libprop_delete(id->override_library, oprop);
    }
  }
  else {
    /* Just remove whole generic override operation of this property. */
    DUNE_lib_override_libprop_delete(id->override_library, oprop);
    if (!is_template) {
      apiProp_copy(duneMain, &ptr, &src, prop, -1);
    }
  }

  /* Outliner e.g. has to be aware of this change. */
  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return op_btnprop_finish(C, &ptr, prop);
}

static void UI_OT_override_remove_btn(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Override";
  ot->idname = "UI_OT_override_remove_btn";
  ot->description = "Remove an override operation";

  /* callbacks */
  ot->poll = override_remove_btnpoll;
  ot->exec = override_remove_btnexec;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  api_def_bool(ot->srna, "all", 1, "All", "Reset to default values all elements of the array");
}

/* -------------------------------------------------------------------- */
/** Copy To Selected Operator **/

#define NOT_NULL(assignment) ((assignment) != NULL)
#define NOT_API_NULL(assignment) ((assignment).data != NULL)

static void ui_ctx_selected_bones_via_pose(DuneContext *C, ListBase *r_lb)
{
  ListBase lb;
  lb = CTX_data_collection_get(C, "selected_pose_bones");

  if (!LIB_listbase_is_empty(&lb)) {
    LISTBASE_FOREACH (CollectionPointerLink *, link, &lb) {
      DunePoseChannel *pchan = link->ptr.data;
      apiptr_create(link->ptr.owner_id, &API_Bone, pchan->bone, &link->ptr);
    }
  }

  *r_lb = lb;
}

bool UI_ctx_copy_to_selected_list(DuneContext *C,
                                      ApiPtr *ptr,
                                      ApiProp *prop,
                                      ListBase *r_lb,
                                      bool *r_use_path_from_id,
                                      char **r_path)
{
  *r_use_path_from_id = false;
  *r_path = NULL;
  /* special case for bone constraints */
  char *path_from_bone = NULL;
  /* Remove links from the collection list which don't contain 'prop'. */
  bool ensure_list_items_contain_prop = false;

  /* PropGroup objects don't have a reference to the struct that actually owns
   * them, so it is normally necessary to do a brute force search to find it. This
   * handles the search for non-ID owners by using the 'active' reference as a hint
   * to preserve efficiency. Only properties defined through API are handled, as
   * custom properties cannot be assumed to be valid for all instances.
   *
   * Properties owned by the ID are handled by the 'if (ptr->owner_id)' case below.
   */
  if (!apiprop_is_idprop(prop) && api_struct_is_a(ptr->type, &api_PropGroup)) {
    ApiPtr owner_ptr;
    char *idpath = NULL;

    /* First, check the active PoseBone and PoseBone->Bone. */
    if (NOT_API_NULL(
            owner_ptr = ctx_data_ptr_get_type(C, "active_pose_bone", &api_PoseBone))) {
      if (NOT_NULL(idpath = apipath_from_struct_to_idprop(&owner_ptr, ptr->data))) {
        *r_lb = ctx_data_collection_get(C, "selected_pose_bones");
      }
      else {
        DunePoseChannel *pchan = owner_ptr.data;
        apiptr_create(owner_ptr.owner_id, &api_Bone, pchan->bone, &owner_ptr);

        if (NOT_NULL(idpath = api_path_from_struct_to_idprop(&owner_ptr, ptr->data))) {
          ui_ctx_selected_bones_via_pose(C, r_lb);
        }
      }
    }

    if (idpath == NULL) {
      /* Check the active EditBone if in edit mode. */
      if (NOT_API_NULL(
              owner_ptr = ctx_data_ptr_get_type_silent(C, "active_bone", &RNA_EditBone)) &&
          NOT_NULL(idpath = apipath_from_struct_to_idprop(&owner_ptr, ptr->data))) {
        *r_lb = ctx_data_collection_get(C, "selected_editable_bones");
      }

      /* Add other simple cases here (Node, NodeSocket, Sequence, ViewLayer etc). */
    }

    if (idpath) {
      *r_path = LIB_sprintfN("%s.%s", idpath, apiprop_id(prop));
      MEM_freeN(idpath);
      return true;
    }
  }

  if (api_struct_is_a(ptr->type, &api_EditBone)) {
    *r_lb = ctx_data_collection_get(C, "selected_editable_bones");
  }
  else if (api_struct_is_a(ptr->type, &api_PoseBone)) {
    *r_lb = ctx_data_collection_get(C, "selected_pose_bones");
  }
  else if (api_struct_is_a(ptr->type, &api_Bone)) {
    ui_ctx_selected_bones_via_pose(C, r_lb);
  }
  else if (api_struct_is_a(ptr->type, &api_Sequence)) {
    /* Special case when we do this for 'Sequence.lock'.
     * (if the sequence is locked, it won't be in "selected_editable_sequences"). */
    const char *prop_id = api_prop_id(prop);
    if (STREQ(prop_id, "lock")) {
      *r_lb = ctx_data_collection_get(C, "selected_sequences");
    }
    else {
      *r_lb = ctx_data_collection_get(C, "selected_editable_sequences");
    }
    /* Account for properties only being available for some sequence types. */
    ensure_list_items_contain_prop = true;
  }
  else if (api_struct_is_a(ptr->type, &api_FCurve)) {
    *r_lb = ctx_data_collection_get(C, "selected_editable_fcurves");
  }
  else if (api_struct_is_a(ptr->type, &api_Keyframe)) {
    *r_lb = ctx_data_collection_get(C, "selected_editable_keyframes");
  }
  else if (api_struct_is_a(ptr->type, &api_Action)) {
    *r_lb = ctx_data_collection_get(C, "selected_editable_actions");
  }
  else if (api_struct_is_a(ptr->type, &api_NlaStrip)) {
    *r_lb = ctx_data_collection_get(C, "selected_nla_strips");
  }
  else if (api_struct_is_a(ptr->type, &api_MovieTrackingTrack)) {
    *r_lb = ctx_data_collection_get(C, "selected_movieclip_tracks");
  }
  else if (api_struct_is_a(ptr->type, &api_Constraint) &&
           (path_from_bone = apipath_resolve_from_type_to_prop(ptr, prop, &api_PoseBone)) !=
               NULL) {
    *r_lb = CTX_data_collection_get(C, "selected_pose_bones");
    *r_path = path_from_bone;
  }
  else if (api_struct_is_a(ptr->type, &api_Node) || api_struct_is_a(ptr->type, &api_NodeSocket)) {
    ListBase lb = {NULL, NULL};
    char *path = NULL;
    bNode *node = NULL;

    /* Get the node we're editing */
    if (api_struct_is_a(ptr->type, &api_NodeSocket)) {
      bNodeTree *ntree = (bNodeTree *)ptr->owner_id;
      bNodeSocket *sock = ptr->data;
      if (nodeFindNode(ntree, sock, &node, NULL)) {
        if ((path = api_path_resolve_from_type_to_property(ptr, prop, &RNA_Node)) != NULL) {
          /* we're good! */
        }
        else {
          node = NULL;
        }
      }
    }
    else {
      node = ptr->data;
    }

    /* Now filter by type */
    if (node) {
      lb = ctx_data_collection_get(C, "selected_nodes");

      LISTBASE_FOREACH_MUTABLE (CollectionPointerLink *, link, &lb) {
        bNode *node_data = link->ptr.data;

        if (node_data->type != node->type) {
          LIB_remlink(&lb, link);
          MEM_freeN(link);
        }
      }
    }

    *r_lb = lb;
    *r_path = path;
  }
  else if (ptr->owner_id) {
    ID *id = ptr->owner_id;

    if (GS(id->name) == ID_OB) {
      *r_lb = CTX_data_collection_get(C, "selected_editable_objects");
      *r_use_path_from_id = true;
      *r_path = RNA_path_from_ID_to_property(ptr, prop);
    }
    else if (OB_DATA_SUPPORT_ID(GS(id->name))) {
      /* check we're using the active object */
      const short id_code = GS(id->name);
      ListBase lb = ctx_data_collection_get(C, "selected_editable_objects");
      char *path = api_path_from_ID_to_property(ptr, prop);

      /* de-duplicate obdata */
      if (!BLI_listbase_is_empty(&lb)) {
        LISTBASE_FOREACH (CollectionPointerLink *, link, &lb) {
          Object *ob = (Object *)link->ptr.owner_id;
          if (ob->data) {
            ID *id_data = ob->data;
            id_data->tag |= LIB_TAG_DOIT;
          }
        }

        LISTBASE_FOREACH_MUTABLE (CollectionPointerLink *, link, &lb) {
          Object *ob = (Object *)link->ptr.owner_id;
          ID *id_data = ob->data;

          if ((id_data == NULL) || (id_data->tag & LIB_TAG_DOIT) == 0 || ID_IS_LINKED(id_data) ||
              (GS(id_data->name) != id_code)) {
            BLI_remlink(&lb, link);
            MEM_freeN(link);
          }
          else {
            /* Avoid prepending 'data' to the path. */
            RNA_id_pointer_create(id_data, &link->ptr);
          }

          if (id_data) {
            id_data->tag &= ~LIB_TAG_DOIT;
          }
        }
      }

      *r_lb = lb;
      *r_path = path;
    }
    else if (GS(id->name) == ID_SCE) {
      /* Sequencer's ID is scene :/ */
      /* Try to recursively find an RNA_Sequence ancestor,
       * to handle situations like T41062... */
      if ((*r_path = RNA_path_resolve_from_type_to_property(ptr, prop, &RNA_Sequence)) != NULL) {
        /* Special case when we do this for 'Sequence.lock'.
         * (if the sequence is locked, it won't be in "selected_editable_sequences"). */
        const char *prop_id = RNA_property_identifier(prop);
        if (STREQ(prop_id, "lock")) {
          *r_lb = CTX_data_collection_get(C, "selected_sequences");
        }
        else {
          *r_lb = CTX_data_collection_get(C, "selected_editable_sequences");
        }
        /* Account for properties only being available for some sequence types. */
        ensure_list_items_contain_prop = true;
      }
    }
    return (*r_path != NULL);
  }
  else {
    return false;
  }

  if (ensure_list_items_contain_prop) {
    const char *prop_id = RNA_property_identifier(prop);
    LISTBASE_FOREACH_MUTABLE (CollectionPointerLink *, link, r_lb) {
      if ((ptr->type != link->ptr.type) &&
          (RNA_struct_type_find_property(link->ptr.type, prop_id) != prop)) {
        BLI_remlink(r_lb, link);
        MEM_freeN(link);
      }
    }
  }

  return true;
}

bool UI_context_copy_to_selected_check(PointerRNA *ptr,
                                       PointerRNA *ptr_link,
                                       PropertyRNA *prop,
                                       const char *path,
                                       bool use_path_from_id,
                                       PointerRNA *r_ptr,
                                       PropertyRNA **r_prop)
{
  PointerRNA idptr;
  PropertyRNA *lprop;
  PointerRNA lptr;

  if (ptr_link->data == ptr->data) {
    return false;
  }

  if (use_path_from_id) {
    /* Path relative to ID. */
    lprop = NULL;
    RNA_id_pointer_create(ptr_link->owner_id, &idptr);
    RNA_path_resolve_property(&idptr, path, &lptr, &lprop);
  }
  else if (path) {
    /* Path relative to elements from list. */
    lprop = NULL;
    RNA_path_resolve_property(ptr_link, path, &lptr, &lprop);
  }
  else {
    lptr = *ptr_link;
    lprop = prop;
  }

  if (lptr.data == ptr->data) {
    /* temp_ptr might not be the same as ptr_link! */
    return false;
  }

  /* Skip non-existing properties on link. This was previously covered with the `lprop != prop`
   * check but we are now more permissive when it comes to ID properties, see below. */
  if (lprop == NULL) {
    return false;
  }

  if (RNA_property_type(lprop) != RNA_property_type(prop)) {
    return false;
  }

  /* Check property pointers matching.
   * For ID properties, these pointers match:
   * - If the property is API defined on an existing class (and they are equally named).
   * - Never for ID properties on specific ID (even if they are equally named).
   * - Never for NodesModifierSettings properties (even if they are equally named).
   *
   * Be permissive on ID properties in the following cases:
   * - #NodesModifierSettings properties
   *   - (special check: only if the node-group matches, since the 'Input_n' properties are name
   *      based and similar on potentially very different node-groups).
   * - ID properties on specific ID
   *   - (no special check, copying seems OK [even if type does not match -- does not do anything
   *      then])
   */
  bool ignore_prop_eq = RNA_property_is_idprop(lprop) && RNA_property_is_idprop(prop);
  if (RNA_struct_is_a(lptr.type, &RNA_NodesModifier) &&
      RNA_struct_is_a(ptr->type, &RNA_NodesModifier)) {
    ignore_prop_eq = false;

    NodesModifierData *nmd_link = (NodesModifierData *)lptr.data;
    NodesModifierData *nmd_src = (NodesModifierData *)ptr->data;
    if (nmd_link->node_group == nmd_src->node_group) {
      ignore_prop_eq = true;
    }
  }

  if ((lprop != prop) && !ignore_prop_eq) {
    return false;
  }

  if (!RNA_property_editable(&lptr, lprop)) {
    return false;
  }

  if (r_ptr) {
    *r_ptr = lptr;
  }
  if (r_prop) {
    *r_prop = lprop;
  }

  return true;
}


/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2009 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup edinterface
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_armature_types.h"
#include "DNA_material_types.h"
#include "DNA_modifier_types.h" /* for handling geometry nodes properties */
#include "DNA_object_types.h"   /* for OB_DATA_SUPPORT_ID */
#include "DNA_screen_types.h"
#include "DNA_text_types.h"

#include "BLI_blenlib.h"
#include "BLI_math_color.h"

#include "BLF_api.h"
#include "BLT_lang.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_lib_override.h"
#include "BKE_material.h"
#include "BKE_node.h"
#include "BKE_report.h"
#include "BKE_screen.h"
#include "BKE_text.h"

#include "IMB_colormanagement.h"

#include "DEG_depsgraph.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_prototypes.h"
#include "RNA_types.h"

#include "UI_interface.h"

#include "interface_intern.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_object.h"
#include "ED_paint.h"

/* for Copy As Driver */
#include "ED_keyframing.h"

/* only for UI_OT_editsource */
#include "BKE_main.h"
#include "BLI_ghash.h"
#include "ED_screen.h"
#include "ED_text.h"

/* -------------------------------------------------------------------- */
/** \name Immediate redraw helper
 *
 * Generally handlers shouldn't do any redrawing, that includes the layout/button definitions. That
 * violates the Model-View-Controller pattern.
 *
 * But there are some operators which really need to re-run the layout definitions for various
 * reasons. For example, "Edit Source" does it to find out which exact Python code added a button.
 * Other operators may need to access buttons that aren't currently visible. In Blender's UI code
 * design that typically means just not adding the button in the first place, for a particular
 * redraw. So the operator needs to change context and re-create the layout, so the button becomes
 * available to act on.
 *
 * \{ */

static void ui_region_redraw_immediately(bContext *C, ARegion *region)
{
  ED_region_do_layout(C, region);
  WM_draw_region_viewport_bind(region);
  ED_region_do_draw(C, region);
  WM_draw_region_viewport_unbind(region);
  region->do_draw = false;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Copy Data Path Operator
 * \{ */

static bool copy_data_path_button_poll(bContext *C)
{
  PointerRNA ptr;
  PropertyRNA *prop;
  char *path;
  int index;

  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  if (ptr.owner_id && ptr.data && prop) {
    path = apiPath_from_ID_to_prop(&ptr, prop);

    if (path) {
      MEM_freeN(path);
      return true;
    }
  }

  return false;
}

static int copy_data_path_btn_ex(DuneContext *C, wmOperator *op)
{
  Main *duneMain = CTX_data_main(C);
  ApiPtr ptr;
  ApiProp *prop;
  char *path;
  int index;
  ID *id;

  const bool full_path = api_bool_get(op->ptr, "full_path");

  /* try to create driver using property retrieved from UI */
  ui_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  if (ptr.owner_id != NULL) {
    if (full_path) {
      if (prop) {
        path = api_path_full_prop_py_ex(duneMain, &ptr, prop, index, true);
      }
      else {
        path = api_path_full_struct_py(duneMain, &ptr);
      }
    }
    else {
      path = api_path_from_realID_to_propid(duneMain, &ptr, prop, 0, -1, &id);

      if (!path) {
        path = api_path_from_ID_to_prop(&ptr, prop);
      }
    }

    if (path) {
      WM_clipboard_text_set(path, false);
      MEM_freeN(path);
      return OPERATOR_FINISHED;
    }
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_copy_data_path_button(wmOperatorType *ot)
{
  ApiProp *prop;

  /* identifiers */
  ot->name = "Copy Data Path";
  ot->idname = "UI_OT_copy_data_path_button";
  ot->description = "Copy the API data path for this property to the clipboard";

  /* callbacks */
  ot->exec = copy_data_path_btn_ex;
  ot->poll = copy_data_path_btn_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;

  /* properties */
  prop = api_def_bool(ot->srna, "full_path", false, "full_path", "Copy full data path");
  api_def_prop_flag(prop, PROP_SKIP_SAVE);
}

/* -------------------------------------------------------------------- */
/** Copy As Driver Operator **/

static bool copy_as_driver_btn_poll(duneContext *C)
{
  ApiPtr ptr;
  ApiProp *prop;
  char *path;
  int index;

  UI_ctx_active_btnProp_get(C, &ptr, &prop, &index);

  if (ptr.owner_id && ptr.data && prop &&
      ELEM(api_proptype(prop), PROP_BOOLEAN, PROP_INT, PROP_FLOAT, PROP_ENUM) &&
      (index >= 0 || !RNA_property_array_check(prop))) {
    path = api_path_from_ID_to_prop(&ptr, prop);

    if (path) {
      MEM_freeN(path);
      return true;
    }
  }

  return false;
}

static int copy_as_driver_btn_ex(duneContext *C, wmOperator *op)
{
  Main *duneMain = CTX_data_main(C);
  ApiPtr ptr;
  ApiProp *prop;
  int index;

  /* try to create driver using property retrieved from UI */
  UI_ctx_active_btnprop_get(C, &ptr, &prop, &index);

  if (ptr.owner_id && ptr.data && prop) {
    ID *id;
    const int dim = apiprop_array_dimension(&ptr, prop, NULL);
    char *path = apipath_from_real_ID_to_propid(duneMain, &ptr, prop, dim, index, &id);

    if (path) {
      ANIM_copy_as_driver(id, path, apiprop_id(prop));
      MEM_freeN(path);
      return OPERATOR_FINISHED;
    }

    DUNE_reportf(op->reports, RPT_ERROR, "Could not compute a valid data path");
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_copy_as_driver_btn(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Copy as New Driver";
  ot->idname = "UI_OT_copy_as_driver_btn";
  ot->description =
      "Create a new driver with this property as input, and copy it to the "
      "clipboard. Use Paste Driver to add it to the target property, or Paste "
      "Driver Variables to extend an existing driver";

  /* callbacks */
  ot->exec = copy_as_driver_btn_ex;
  ot->poll = copy_as_driver_btn_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/* -------------------------------------------------------------------- */
/** Copy Python Command Operator **/

static bool copy_pycmd_btnpoll(duneContext *C)
{
  uiBtn *btn = UI_ctx_active_btn_get(C);

  if (but && (but->optype != NULL)) {
    return 1;
  }

  return 0;
}

static int copy_pycmd_btnex(duneContext *C, wmOperator *UNUSED(op))
{
  uiBtn *btn = UI_ctx_active_btnget(C);

  if (but && (but->optype != NULL)) {
    ApiPtr *opptr;
    char *str;
    opptr = UI_btn_op_ptr_get(btn); /* allocated when needed, the button owns it */

    str = WM_op_pystring_ex(C, NULL, false, true, btn->optype, opptr);

    WM_clipboard_text_set(str, 0);

    MEM_freeN(str);

    return OPERATOR_FINISHED;
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_copy_pycmd_btn(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Copy Python Command";
  ot->idname = "UI_OT_copy_pycmd_btn";
  ot->description = "Copy the Python command matching this button";

  /* callbacks */
  ot->exec = copy_pycmd_btnex;
  ot->poll = copy_pycmd_btnpoll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/* -------------------------------------------------------------------- */
/** Reset to Default Values Button Operator **/

static int op_btnprop_finish(duneContext *C, PointerRNA *ptr, PropertyRNA *prop)
{
  ID *id = ptr->owner_id;

  /* perform updates required for this property */
  apiprop_update(C, ptr, prop);

  /* as if we pressed the button */
  UI_ctx_active_btnprop_handle(C, false);

  /* Since we don't want to undo _all_ edits to settings, eg window
   * edits on the screen or on operator settings.
   * it might be better to move undo's inline - campbell */
  if (id && ID_CHECK_UNDO(id)) {
    /* do nothing, go ahead with undo */
    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

static int op_btnprop_finish_with_undo(duneContext *C,
                                                     PointerRNA *ptr,
                                                     PropertyRNA *prop)
{
  /* Perform updates required for this property. */
  apiprop_update(C, ptr, prop);

  /* As if we pressed the button. */
  UI_ctx_active_btnprop_handle(C, true);

  return OPERATOR_FINISHED;
}

static bool reset_default_btnpoll(duneContext *C)
{
  PointerRNA ptr;
  PropertyRNA *prop;
  int index;

  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  return (ptr.data && prop && RNA_property_editable(&ptr, prop));
}

static int reset_default_button_exec(bContext *C, wmOperator *op)
{
  PointerRNA ptr;
  PropertyRNA *prop;
  int index;
  const bool all = RNA_boolean_get(op->ptr, "all");

  /* try to reset the nominated setting to its default value */
  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data && prop && RNA_property_editable(&ptr, prop)) {
    if (RNA_property_reset(&ptr, prop, (all) ? -1 : index)) {
      return operator_button_property_finish_with_undo(C, &ptr, prop);
    }
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_reset_default_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reset to Default Value";
  ot->idname = "UI_OT_reset_default_button";
  ot->description = "Reset this property's value to its default value";

  /* callbacks */
  ot->poll = reset_default_button_poll;
  ot->exec = reset_default_button_exec;

  /* flags */
  /* Don't set #OPTYPE_UNDO because #operator_button_property_finish_with_undo
   * is responsible for the undo push. */
  ot->flag = 0;

  /* properties */
  RNA_def_boolean(ot->srna, "all", 1, "All", "Reset to default values all elements of the array");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Assign Value as Default Button Operator
 * \{ */

static bool assign_default_button_poll(bContext *C)
{
  PointerRNA ptr;
  PropertyRNA *prop;
  int index;

  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  if (ptr.data && prop && RNA_property_editable(&ptr, prop)) {
    const PropertyType type = RNA_property_type(prop);

    return RNA_property_is_idprop(prop) && !RNA_property_array_check(prop) &&
           ELEM(type, PROP_INT, PROP_FLOAT);
  }

  return false;
}

static int assign_default_button_exec(bContext *C, wmOperator *UNUSED(op))
{
  PointerRNA ptr;
  PropertyRNA *prop;
  int index;

  /* try to reset the nominated setting to its default value */
  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data && prop && RNA_property_editable(&ptr, prop)) {
    if (RNA_property_assign_default(&ptr, prop)) {
      return operator_button_property_finish(C, &ptr, prop);
    }
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_assign_default_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Assign Value as Default";
  ot->idname = "UI_OT_assign_default_button";
  ot->description = "Set this property's current value as the new default";

  /* callbacks */
  ot->poll = assign_default_button_poll;
  ot->exec = assign_default_button_exec;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Unset Property Button Operator
 * \{ */

static int unset_property_button_exec(bContext *C, wmOperator *UNUSED(op))
{
  PointerRNA ptr;
  PropertyRNA *prop;
  int index;

  /* try to unset the nominated property */
  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data && prop && RNA_property_editable(&ptr, prop) &&
      /* RNA_property_is_idprop(prop) && */
      RNA_property_is_set(&ptr, prop)) {
    RNA_property_unset(&ptr, prop);
    return operator_button_property_finish(C, &ptr, prop);
  }

  return OPERATOR_CANCELLED;
}

static void UI_OT_unset_property_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Unset Property";
  ot->idname = "UI_OT_unset_property_button";
  ot->description = "Clear the property and use default or generated value in operators";

  /* callbacks */
  ot->poll = ED_operator_regionactive;
  ot->exec = unset_property_button_exec;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Define Override Type Operator
 * \{ */

/* Note that we use different values for UI/UX than 'real' override operations, user does not care
 * whether it's added or removed for the differential operation e.g. */
enum {
  UIOverride_Type_NOOP = 0,
  UIOverride_Type_Replace = 1,
  UIOverride_Type_Difference = 2, /* Add/subtract */
  UIOverride_Type_Factor = 3,     /* Multiply */
  /* TODO: should/can we expose insert/remove ones for collections? Doubt it... */
};

static EnumPropertyItem override_type_items[] = {
    {UIOverride_Type_NOOP,
     "NOOP",
     0,
     "NoOp",
     "'No-Operation', place holder preventing automatic override to ever affect the property"},
    {UIOverride_Type_Replace,
     "REPLACE",
     0,
     "Replace",
     "Completely replace value from linked data by local one"},
    {UIOverride_Type_Difference,
     "DIFFERENCE",
     0,
     "Difference",
     "Store difference to linked data value"},
    {UIOverride_Type_Factor,
     "FACTOR",
     0,
     "Factor",
     "Store factor to linked data value (useful e.g. for scale)"},
    {0, NULL, 0, NULL, NULL},
};

static bool override_type_set_button_poll(bContext *C)
{
  PointerRNA ptr;
  PropertyRNA *prop;
  int index;

  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  const uint override_status = RNA_property_override_library_status(
      CTX_data_main(C), &ptr, prop, index);

  return (ptr.data && prop && (override_status & RNA_OVERRIDE_STATUS_OVERRIDABLE));
}

static int override_type_set_button_exec(bContext *C, wmOperator *op)
{
  PointerRNA ptr;
  PropertyRNA *prop;
  int index;
  bool created;
  const bool all = RNA_boolean_get(op->ptr, "all");
  const int op_type = RNA_enum_get(op->ptr, "type");

  short operation;

  switch (op_type) {
    case UIOverride_Type_NOOP:
      operation = IDOVERRIDE_LIBRARY_OP_NOOP;
      break;
    case UIOverride_Type_Replace:
      operation = IDOVERRIDE_LIBRARY_OP_REPLACE;
      break;
    case UIOverride_Type_Difference:
      /* override code will automatically switch to subtract if needed. */
      operation = IDOVERRIDE_LIBRARY_OP_ADD;
      break;
    case UIOverride_Type_Factor:
      operation = IDOVERRIDE_LIBRARY_OP_MULTIPLY;
      break;
    default:
      operation = IDOVERRIDE_LIBRARY_OP_REPLACE;
      BLI_assert(0);
      break;
  }

  /* try to reset the nominated setting to its default value */
  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  BLI_assert(ptr.owner_id != NULL);

  if (all) {
    index = -1;
  }

  IDOverrideLibraryPropertyOperation *opop = RNA_property_override_property_operation_get(
      CTX_data_main(C), &ptr, prop, operation, index, true, NULL, &created);

  if (opop == NULL) {
    /* Sometimes e.g. RNA cannot generate a path to the given property. */
    BKE_reportf(op->reports, RPT_WARNING, "Failed to create the override operation");
    return OPERATOR_CANCELLED;
  }

  if (!created) {
    opop->operation = operation;
  }

  /* Outliner e.g. has to be aware of this change. */
  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return operator_button_property_finish(C, &ptr, prop);
}

static int override_type_set_button_invoke(bContext *C,
                                           wmOperator *op,
                                           const wmEvent *UNUSED(event))
{
#if 0 /* Disabled for now */
  return WM_menu_invoke_ex(C, op, WM_OP_INVOKE_DEFAULT);
#else
  RNA_enum_set(op->ptr, "type", IDOVERRIDE_LIBRARY_OP_REPLACE);
  return override_type_set_button_exec(C, op);
#endif
}

static void UI_OT_override_type_set_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Define Override Type";
  ot->idname = "UI_OT_override_type_set_button";
  ot->description = "Create an override operation, or set the type of an existing one";

  /* callbacks */
  ot->poll = override_type_set_button_poll;
  ot->exec = override_type_set_button_exec;
  ot->invoke = override_type_set_button_invoke;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna, "all", 1, "All", "Reset to default values all elements of the array");
  ot->prop = RNA_def_enum(ot->srna,
                          "type",
                          override_type_items,
                          UIOverride_Type_Replace,
                          "Type",
                          "Type of override operation");
  /* TODO: add itemf callback, not all options are available for all data types... */
}

static bool override_remove_button_poll(bContext *C)
{
  PointerRNA ptr;
  PropertyRNA *prop;
  int index;

  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  const uint override_status = RNA_property_override_library_status(
      CTX_data_main(C), &ptr, prop, index);

  return (ptr.data && ptr.owner_id && prop && (override_status & RNA_OVERRIDE_STATUS_OVERRIDDEN));
}

static int override_remove_button_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  PointerRNA ptr, id_refptr, src;
  PropertyRNA *prop;
  int index;
  const bool all = RNA_boolean_get(op->ptr, "all");

  /* try to reset the nominated setting to its default value */
  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  ID *id = ptr.owner_id;
  IDOverrideLibraryProperty *oprop = RNA_property_override_property_find(bmain, &ptr, prop, &id);
  BLI_assert(oprop != NULL);
  BLI_assert(id != NULL && id->override_library != NULL);

  const bool is_template = ID_IS_OVERRIDE_LIBRARY_TEMPLATE(id);

  /* We need source (i.e. linked data) to restore values of deleted overrides...
   * If this is an override template, we obviously do not need to restore anything. */
  if (!is_template) {
    PropertyRNA *src_prop;
    RNA_id_pointer_create(id->override_library->reference, &id_refptr);
    if (!RNA_path_resolve_property(&id_refptr, oprop->rna_path, &src, &src_prop)) {
      BLI_assert_msg(0, "Failed to create matching source (linked data) RNA pointer");
    }
  }

  if (!all && index != -1) {
    bool is_strict_find;
    /* Remove override operation for given item,
     * add singular operations for the other items as needed. */
    IDOverrideLibraryPropertyOperation *opop = BKE_lib_override_library_property_operation_find(
        oprop, NULL, NULL, index, index, false, &is_strict_find);
    BLI_assert(opop != NULL);
    if (!is_strict_find) {
      /* No specific override operation, we have to get generic one,
       * and create item-specific override operations for all but given index,
       * before removing generic one. */
      for (int idx = RNA_property_array_length(&ptr, prop); idx--;) {
        if (idx != index) {
          BKE_lib_override_library_property_operation_get(
              oprop, opop->operation, NULL, NULL, idx, idx, true, NULL, NULL);
        }
      }
    }
    BKE_lib_override_library_property_operation_delete(oprop, opop);
    if (!is_template) {
      RNA_property_copy(bmain, &ptr, &src, prop, index);
    }
    if (BLI_listbase_is_empty(&oprop->operations)) {
      BKE_lib_override_library_property_delete(id->override_library, oprop);
    }
  }
  else {
    /* Just remove whole generic override operation of this property. */
    BKE_lib_override_library_property_delete(id->override_library, oprop);
    if (!is_template) {
      RNA_property_copy(bmain, &ptr, &src, prop, -1);
    }
  }

  /* Outliner e.g. has to be aware of this change. */
  WM_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return operator_button_property_finish(C, &ptr, prop);
}

static void UI_OT_override_remove_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Override";
  ot->idname = "UI_OT_override_remove_button";
  ot->description = "Remove an override operation";

  /* callbacks */
  ot->poll = override_remove_button_poll;
  ot->exec = override_remove_button_exec;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna, "all", 1, "All", "Reset to default values all elements of the array");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Copy To Selected Operator
 * \{ */

#define NOT_NULL(assignment) ((assignment) != NULL)
#define NOT_RNA_NULL(assignment) ((assignment).data != NULL)

static void ui_context_selected_bones_via_pose(bContext *C, ListBase *r_lb)
{
  ListBase lb;
  lb = CTX_data_collection_get(C, "selected_pose_bones");

  if (!BLI_listbase_is_empty(&lb)) {
    LISTBASE_FOREACH (CollectionPointerLink *, link, &lb) {
      bPoseChannel *pchan = link->ptr.data;
      RNA_pointer_create(link->ptr.owner_id, &RNA_Bone, pchan->bone, &link->ptr);
    }
  }

  *r_lb = lb;
}

bool UI_context_copy_to_selected_list(bContext *C,
                                      PointerRNA *ptr,
                                      PropertyRNA *prop,
                                      ListBase *r_lb,
                                      bool *r_use_path_from_id,
                                      char **r_path)
{
  *r_use_path_from_id = false;
  *r_path = NULL;
  /* special case for bone constraints */
  char *path_from_bone = NULL;
  /* Remove links from the collection list which don't contain 'prop'. */
  bool ensure_list_items_contain_prop = false;

  /* PropertyGroup objects don't have a reference to the struct that actually owns
   * them, so it is normally necessary to do a brute force search to find it. This
   * handles the search for non-ID owners by using the 'active' reference as a hint
   * to preserve efficiency. Only properties defined through RNA are handled, as
   * custom properties cannot be assumed to be valid for all instances.
   *
   * Properties owned by the ID are handled by the 'if (ptr->owner_id)' case below.
   */
  if (!RNA_property_is_idprop(prop) && RNA_struct_is_a(ptr->type, &RNA_PropertyGroup)) {
    PointerRNA owner_ptr;
    char *idpath = NULL;

    /* First, check the active PoseBone and PoseBone->Bone. */
    if (NOT_RNA_NULL(
            owner_ptr = CTX_data_pointer_get_type(C, "active_pose_bone", &RNA_PoseBone))) {
      if (NOT_NULL(idpath = RNA_path_from_struct_to_idproperty(&owner_ptr, ptr->data))) {
        *r_lb = CTX_data_collection_get(C, "selected_pose_bones");
      }
      else {
        bPoseChannel *pchan = owner_ptr.data;
        RNA_pointer_create(owner_ptr.owner_id, &RNA_Bone, pchan->bone, &owner_ptr);

        if (NOT_NULL(idpath = RNA_path_from_struct_to_idproperty(&owner_ptr, ptr->data))) {
          ui_context_selected_bones_via_pose(C, r_lb);
        }
      }
    }

    if (idpath == NULL) {
      /* Check the active EditBone if in edit mode. */
      if (NOT_RNA_NULL(
              owner_ptr = CTX_data_pointer_get_type_silent(C, "active_bone", &RNA_EditBone)) &&
          NOT_NULL(idpath = RNA_path_from_struct_to_idproperty(&owner_ptr, ptr->data))) {
        *r_lb = CTX_data_collection_get(C, "selected_editable_bones");
      }

      /* Add other simple cases here (Node, NodeSocket, Sequence, ViewLayer etc). */
    }

    if (idpath) {
      *r_path = BLI_sprintfN("%s.%s", idpath, RNA_property_identifier(prop));
      MEM_freeN(idpath);
      return true;
    }
  }

  if (RNA_struct_is_a(ptr->type, &RNA_EditBone)) {
    *r_lb = CTX_data_collection_get(C, "selected_editable_bones");
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_PoseBone)) {
    *r_lb = CTX_data_collection_get(C, "selected_pose_bones");
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_Bone)) {
    ui_context_selected_bones_via_pose(C, r_lb);
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_Sequence)) {
    /* Special case when we do this for 'Sequence.lock'.
     * (if the sequence is locked, it won't be in "selected_editable_sequences"). */
    const char *prop_id = RNA_property_identifier(prop);
    if (STREQ(prop_id, "lock")) {
      *r_lb = CTX_data_collection_get(C, "selected_sequences");
    }
    else {
      *r_lb = CTX_data_collection_get(C, "selected_editable_sequences");
    }
    /* Account for properties only being available for some sequence types. */
    ensure_list_items_contain_prop = true;
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_FCurve)) {
    *r_lb = CTX_data_collection_get(C, "selected_editable_fcurves");
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_Keyframe)) {
    *r_lb = CTX_data_collection_get(C, "selected_editable_keyframes");
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_Action)) {
    *r_lb = CTX_data_collection_get(C, "selected_editable_actions");
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_NlaStrip)) {
    *r_lb = CTX_data_collection_get(C, "selected_nla_strips");
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_MovieTrackingTrack)) {
    *r_lb = CTX_data_collection_get(C, "selected_movieclip_tracks");
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_Constraint) &&
           (path_from_bone = RNA_path_resolve_from_type_to_property(ptr, prop, &RNA_PoseBone)) !=
               NULL) {
    *r_lb = CTX_data_collection_get(C, "selected_pose_bones");
    *r_path = path_from_bone;
  }
  else if (RNA_struct_is_a(ptr->type, &RNA_Node) || RNA_struct_is_a(ptr->type, &RNA_NodeSocket)) {
    ListBase lb = {NULL, NULL};
    char *path = NULL;
    bNode *node = NULL;

    /* Get the node we're editing */
    if (RNA_struct_is_a(ptr->type, &RNA_NodeSocket)) {
      bNodeTree *ntree = (bNodeTree *)ptr->owner_id;
      bNodeSocket *sock = ptr->data;
      if (nodeFindNode(ntree, sock, &node, NULL)) {
        if ((path = RNA_path_resolve_from_type_to_property(ptr, prop, &RNA_Node)) != NULL) {
          /* we're good! */
        }
        else {
          node = NULL;
        }
      }
    }
    else {
      node = ptr->data;
    }

    /* Now filter by type */
    if (node) {
      lb = CTX_data_collection_get(C, "selected_nodes");

      LISTBASE_FOREACH_MUTABLE (CollectionPointerLink *, link, &lb) {
        bNode *node_data = link->ptr.data;

        if (node_data->type != node->type) {
          BLI_remlink(&lb, link);
          MEM_freeN(link);
        }
      }
    }

    *r_lb = lb;
    *r_path = path;
  }
  else if (ptr->owner_id) {
    ID *id = ptr->owner_id;

    if (GS(id->name) == ID_OB) {
      *r_lb = CTX_data_collection_get(C, "selected_editable_objects");
      *r_use_path_from_id = true;
      *r_path = RNA_path_from_ID_to_property(ptr, prop);
    }
    else if (OB_DATA_SUPPORT_ID(GS(id->name))) {
      /* check we're using the active object */
      const short id_code = GS(id->name);
      ListBase lb = CTX_data_collection_get(C, "selected_editable_objects");
      char *path = RNA_path_from_ID_to_property(ptr, prop);

      /* de-duplicate obdata */
      if (!BLI_listbase_is_empty(&lb)) {
        LISTBASE_FOREACH (CollectionPointerLink *, link, &lb) {
          Object *ob = (Object *)link->ptr.owner_id;
          if (ob->data) {
            ID *id_data = ob->data;
            id_data->tag |= LIB_TAG_DOIT;
          }
        }

        LISTBASE_FOREACH_MUTABLE (CollectionPointerLink *, link, &lb) {
          Object *ob = (Object *)link->ptr.owner_id;
          ID *id_data = ob->data;

          if ((id_data == NULL) || (id_data->tag & LIB_TAG_DOIT) == 0 || ID_IS_LINKED(id_data) ||
              (GS(id_data->name) != id_code)) {
            BLI_remlink(&lb, link);
            MEM_freeN(link);
          }
          else {
            /* Avoid prepending 'data' to the path. */
            RNA_id_pointer_create(id_data, &link->ptr);
          }

          if (id_data) {
            id_data->tag &= ~LIB_TAG_DOIT;
          }
        }
      }

      *r_lb = lb;
      *r_path = path;
    }
    else if (GS(id->name) == ID_SCE) {
      /* Sequencer's ID is scene :/ */
      /* Try to recursively find an RNA_Sequence ancestor,
       * to handle situations like T41062... */
      if ((*r_path = RNA_path_resolve_from_type_to_property(ptr, prop, &RNA_Sequence)) != NULL) {
        /* Special case when we do this for 'Sequence.lock'.
         * (if the sequence is locked, it won't be in "selected_editable_sequences"). */
        const char *prop_id = RNA_property_identifier(prop);
        if (STREQ(prop_id, "lock")) {
          *r_lb = CTX_data_collection_get(C, "selected_sequences");
        }
        else {
          *r_lb = CTX_data_collection_get(C, "selected_editable_sequences");
        }
        /* Account for properties only being available for some sequence types. */
        ensure_list_items_contain_prop = true;
      }
    }
    return (*r_path != NULL);
  }
  else {
    return false;
  }

  if (ensure_list_items_contain_prop) {
    const char *prop_id = RNA_property_identifier(prop);
    LISTBASE_FOREACH_MUTABLE (CollectionPointerLink *, link, r_lb) {
      if ((ptr->type != link->ptr.type) &&
          (RNA_struct_type_find_property(link->ptr.type, prop_id) != prop)) {
        BLI_remlink(r_lb, link);
        MEM_freeN(link);
      }
    }
  }

  return true;
}

bool UI_context_copy_to_selected_check(PointerRNA *ptr,
                                       PointerRNA *ptr_link,
                                       PropertyRNA *prop,
                                       const char *path,
                                       bool use_path_from_id,
                                       PointerRNA *r_ptr,
                                       PropertyRNA **r_prop)
{
  PointerRNA idptr;
  PropertyRNA *lprop;
  PointerRNA lptr;

  if (ptr_link->data == ptr->data) {
    return false;
  }

  if (use_path_from_id) {
    /* Path relative to ID. */
    lprop = NULL;
    RNA_id_pointer_create(ptr_link->owner_id, &idptr);
    RNA_path_resolve_property(&idptr, path, &lptr, &lprop);
  }
  else if (path) {
    /* Path relative to elements from list. */
    lprop = NULL;
    RNA_path_resolve_property(ptr_link, path, &lptr, &lprop);
  }
  else {
    lptr = *ptr_link;
    lprop = prop;
  }

  if (lptr.data == ptr->data) {
    /* temp_ptr might not be the same as ptr_link! */
    return false;
  }

  /* Skip non-existing properties on link. This was previously covered with the `lprop != prop`
   * check but we are now more permissive when it comes to ID properties, see below. */
  if (lprop == NULL) {
    return false;
  }

  if (RNA_property_type(lprop) != RNA_property_type(prop)) {
    return false;
  }

  /* Check property pointers matching.
   * For ID properties, these pointers match:
   * - If the property is API defined on an existing class (and they are equally named).
   * - Never for ID properties on specific ID (even if they are equally named).
   * - Never for NodesModifierSettings properties (even if they are equally named).
   *
   * Be permissive on ID properties in the following cases:
   * - #NodesModifierSettings properties
   *   - (special check: only if the node-group matches, since the 'Input_n' properties are name
   *      based and similar on potentially very different node-groups).
   * - ID properties on specific ID
   *   - (no special check, copying seems OK [even if type does not match -- does not do anything
   *      then])
   */
  bool ignore_prop_eq = RNA_property_is_idprop(lprop) && RNA_property_is_idprop(prop);
  if (RNA_struct_is_a(lptr.type, &RNA_NodesModifier) &&
      RNA_struct_is_a(ptr->type, &RNA_NodesModifier)) {
    ignore_prop_eq = false;

    NodesModifierData *nmd_link = (NodesModifierData *)lptr.data;
    NodesModifierData *nmd_src = (NodesModifierData *)ptr->data;
    if (nmd_link->node_group == nmd_src->node_group) {
      ignore_prop_eq = true;
    }
  }

  if ((lprop != prop) && !ignore_prop_eq) {
    return false;
  }

  if (!RNA_property_editable(&lptr, lprop)) {
    return false;
  }

  if (r_ptr) {
    *r_ptr = lptr;
  }
  if (r_prop) {
    *r_prop = lprop;
  }

  return true;
}

/**
 * Called from both exec & poll.
 *
 * \note Normally we wouldn't call a loop from within a poll function,
 * however this is a special case, and for regular poll calls, getting
 * the context from the button will fail early.
 */
static bool copy_to_selected_button(bContext *C, bool all, bool poll)
{
  Main *bmain = CTX_data_main(C);
  PointerRNA ptr, lptr;
  PropertyRNA *prop, *lprop;
  bool success = false;
  int index;

  /* try to reset the nominated setting to its default value */
  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data == NULL || prop == NULL) {
    return false;
  }

  char *path = NULL;
  bool use_path_from_id;
  ListBase lb = {NULL};

  if (!UI_context_copy_to_selected_list(C, &ptr, prop, &lb, &use_path_from_id, &path)) {
    return false;
  }
  if (BLI_listbase_is_empty(&lb)) {
    MEM_SAFE_FREE(path);
    return false;
  }

  LISTBASE_FOREACH (CollectionPointerLink *, link, &lb) {
    if (link->ptr.data == ptr.data) {
      continue;
    }

    if (!UI_context_copy_to_selected_check(
            &ptr, &link->ptr, prop, path, use_path_from_id, &lptr, &lprop)) {
      continue;
    }

    if (poll) {
      success = true;
      break;
    }
    if (RNA_property_copy(bmain, &lptr, &ptr, prop, (all) ? -1 : index)) {
      RNA_property_update(C, &lptr, prop);
      success = true;
    }
  }

  MEM_SAFE_FREE(path);
  BLI_freelistN(&lb);

  return success;
}

static bool copy_to_selected_button_poll(bContext *C)
{
  return copy_to_selected_button(C, false, true);
}

static int copy_to_selected_button_exec(bContext *C, wmOperator *op)
{
  bool success;

  const bool all = RNA_boolean_get(op->ptr, "all");

  success = copy_to_selected_button(C, all, false);

  return (success) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

static void UI_OT_copy_to_selected_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Copy to Selected";
  ot->idname = "UI_OT_copy_to_selected_button";
  ot->description = "Copy property from this object to selected objects or bones";

  /* callbacks */
  ot->poll = copy_to_selected_button_poll;
  ot->exec = copy_to_selected_button_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna, "all", true, "All", "Copy to selected all elements of the array");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Jump to Target Operator
 * \{ */

/** Jump to the object or bone referenced by the pointer, or check if it is possible. */
static bool jump_to_target_ptr(bContext *C, PointerRNA ptr, const bool poll)
{
  if (RNA_pointer_is_null(&ptr)) {
    return false;
  }

  /* Verify pointer type. */
  char bone_name[MAXBONENAME];
  const StructRNA *target_type = NULL;

  if (ELEM(ptr.type, &RNA_EditBone, &RNA_PoseBone, &RNA_Bone)) {
    RNA_string_get(&ptr, "name", bone_name);
    if (bone_name[0] != '\0') {
      target_type = &RNA_Bone;
    }
  }
  else if (RNA_struct_is_a(ptr.type, &RNA_Object)) {
    target_type = &RNA_Object;
  }

  if (target_type == NULL) {
    return false;
  }

  /* Find the containing Object. */
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Base *base = NULL;
  const short id_type = GS(ptr.owner_id->name);
  if (id_type == ID_OB) {
    base = BKE_view_layer_base_find(view_layer, (Object *)ptr.owner_id);
  }
  else if (OB_DATA_SUPPORT_ID(id_type)) {
    base = ED_object_find_first_by_data_id(view_layer, ptr.owner_id);
  }

  bool ok = false;
  if ((base == NULL) || ((target_type == &RNA_Bone) && (base->object->type != OB_ARMATURE))) {
    /* pass */
  }
  else if (poll) {
    ok = true;
  }
  else {
    /* Make optional. */
    const bool reveal_hidden = true;
    /* Select and activate the target. */
    if (target_type == &RNA_Bone) {
      ok = ED_object_jump_to_bone(C, base->object, bone_name, reveal_hidden);
    }
    else if (target_type == &RNA_Object) {
      ok = ED_object_jump_to_object(C, base->object, reveal_hidden);
    }
    else {
      BLI_assert(0);
    }
  }
  return ok;
}

/**
 * Jump to the object or bone referred to by the current UI field value.
 *
 * \note quite heavy for a poll callback, but the operator is only
 * used as a right click menu item for certain UI field types, and
 * this will fail quickly if the context is completely unsuitable.
 */
static bool jump_to_target_button(bContext *C, bool poll)
{
  PointerRNA ptr, target_ptr;
  PropertyRNA *prop;
  int index;

  UI_context_active_but_prop_get(C, &ptr, &prop, &index);

  /* If there is a valid property... */
  if (ptr.data && prop) {
    const PropertyType type = RNA_property_type(prop);

    /* For pointer properties, use their value directly. */
    if (type == PROP_POINTER) {
      target_ptr = RNA_property_pointer_get(&ptr, prop);

      return jump_to_target_ptr(C, target_ptr, poll);
    }
    /* For string properties with prop_search, look up the search collection item. */
    if (type == PROP_STRING) {
      const uiBut *but = UI_context_active_but_get(C);
      const uiButSearch *search_but = (but->type == UI_BTYPE_SEARCH_MENU) ? (uiButSearch *)but :
                                                                            NULL;

      if (search_but && search_but->items_update_fn == ui_rna_collection_search_update_fn) {
        uiRNACollectionSearch *coll_search = search_but->arg;

        char str_buf[MAXBONENAME];
        char *str_ptr = RNA_property_string_get_alloc(&ptr, prop, str_buf, sizeof(str_buf), NULL);

        int found = RNA_property_collection_lookup_string(
            &coll_search->search_ptr, coll_search->search_prop, str_ptr, &target_ptr);

        if (str_ptr != str_buf) {
          MEM_freeN(str_ptr);
        }

        if (found) {
          return jump_to_target_ptr(C, target_ptr, poll);
        }
      }
    }
  }

  return false;
}

bool ui_jump_to_target_button_poll(bContext *C)
{
  return jump_to_target_button(C, true);
}

static int jump_to_target_button_exec(bContext *C, wmOperator *UNUSED(op))
{
  const bool success = jump_to_target_button(C, false);

  return (success) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

static void UI_OT_jump_to_target_button(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Jump to Target";
  ot->idname = "UI_OT_jump_to_target_button";
  ot->description = "Switch to the target object or bone";

  /* callbacks */
  ot->poll = ui_jump_to_target_button_poll;
  ot->exec = jump_to_target_button_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Python Source Operator
 * \{ */

#ifdef WITH_PYTHON

/* ------------------------------------------------------------------------- */
/* EditSource Utility funcs and operator,
 * NOTE: this includes utility functions and button matching checks. */

typedef struct uiEditSourceStore {
  uiBut but_orig;
  GHash *hash;
} uiEditSourceStore;

typedef struct uiEditSourceButStore {
  char py_dbg_fn[FILE_MAX];
  int py_dbg_line_number;
} uiEditSourceButStore;

/* should only ever be set while the edit source operator is running */
static struct uiEditSourceStore *ui_editsource_info = NULL;

bool UI_editsource_enable_check(void)
{
  return (ui_editsource_info != NULL);
}

static void ui_editsource_active_but_set(uiBut *but)
{
  BLI_assert(ui_editsource_info == NULL);

  ui_editsource_info = MEM_callocN(sizeof(uiEditSourceStore), __func__);
  memcpy(&ui_editsource_info->but_orig, but, sizeof(uiBut));

  ui_editsource_info->hash = BLI_ghash_ptr_new(__func__);
}

static void ui_editsource_active_but_clear(void)
{
  BLI_ghash_free(ui_editsource_info->hash, NULL, MEM_freeN);
  MEM_freeN(ui_editsource_info);
  ui_editsource_info = NULL;
}

static bool ui_editsource_uibut_match(uiBut *but_a, uiBut *but_b)
{
#  if 0
  printf("matching buttons: '%s' == '%s'\n", but_a->drawstr, but_b->drawstr);
#  endif

  /* this just needs to be a 'good-enough' comparison so we can know beyond
   * reasonable doubt that these buttons are the same between redraws.
   * if this fails it only means edit-source fails - campbell */
  if (BLI_rctf_compare(&but_a->rect, &but_b->rect, FLT_EPSILON) && (but_a->type == but_b->type) &&
      (but_a->rnaprop == but_b->rnaprop) && (but_a->optype == but_b->optype) &&
      (but_a->unit_type == but_b->unit_type) &&
      STREQLEN(but_a->drawstr, but_b->drawstr, UI_MAX_DRAW_STR)) {
    return true;
  }
  return false;
}

void UI_editsource_active_but_test(uiBut *but)
{
  extern void PyC_FileAndNum_Safe(const char **r_filename, int *r_lineno);

  struct uiEditSourceButStore *but_store = MEM_callocN(sizeof(uiEditSourceButStore), __func__);

  const char *fn;
  int line_number = -1;

#  if 0
  printf("comparing buttons: '%s' == '%s'\n", but->drawstr, ui_editsource_info->but_orig.drawstr);
#  endif

  PyC_FileAndNum_Safe(&fn, &line_number);

  if (line_number != -1) {
    BLI_strncpy(but_store->py_dbg_fn, fn, sizeof(but_store->py_dbg_fn));
    but_store->py_dbg_line_number = line_number;
  }
  else {
    but_store->py_dbg_fn[0] = '\0';
    but_store->py_dbg_line_number = -1;
  }

  BLI_ghash_insert(ui_editsource_info->hash, but, but_store);
}

void UI_editsource_but_replace(const uiBut *old_but, uiBut *new_but)
{
  uiEditSourceButStore *but_store = BLI_ghash_lookup(ui_editsource_info->hash, old_but);
  if (but_store) {
    BLI_ghash_remove(ui_editsource_info->hash, old_but, NULL, NULL);
    BLI_ghash_insert(ui_editsource_info->hash, new_but, but_store);
  }
}

static int editsource_text_edit(bContext *C,
                                wmOperator *op,
                                const char filepath[FILE_MAX],
                                const int line)
{
  struct Main *bmain = CTX_data_main(C);
  Text *text = NULL;

  /* Developers may wish to copy-paste to an external editor. */
  printf("%s:%d\n", filepath, line);

  LISTBASE_FOREACH (Text *, text_iter, &bmain->texts) {
    if (text_iter->filepath && BLI_path_cmp(text_iter->filepath, filepath) == 0) {
      text = text_iter;
      break;
    }
  }

  if (text == NULL) {
    text = BKE_text_load(bmain, filepath, BKE_main_blendfile_path(bmain));
  }

  if (text == NULL) {
    BKE_reportf(op->reports, RPT_WARNING, "File '%s' cannot be opened", filepath);
    return OPERATOR_CANCELLED;
  }

  txt_move_toline(text, line - 1, false);

  /* naughty!, find text area to set, not good behavior
   * but since this is a developer tool lets allow it - campbell */
  if (!ED_text_activate_in_screen(C, text)) {
    BKE_reportf(op->reports, RPT_INFO, "See '%s' in the text editor", text->id.name + 2);
  }

  WM_event_add_notifier(C, NC_TEXT | ND_CURSOR, text);

  return OPERATOR_FINISHED;
}

static int editsource_exec(bContext *C, wmOperator *op)
{
  uiBut *but = UI_context_active_but_get(C);

  if (but) {
    GHashIterator ghi;
    struct uiEditSourceButStore *but_store = NULL;

    ARegion *region = CTX_wm_region(C);
    int ret;

    /* needed else the active button does not get tested */
    UI_screen_free_active_but_highlight(C, CTX_wm_screen(C));

    // printf("%s: begin\n", __func__);

    /* take care not to return before calling ui_editsource_active_but_clear */
    ui_editsource_active_but_set(but);

    /* redraw and get active button python info */
    ui_region_redraw_immediately(C, region);

    for (BLI_ghashIterator_init(&ghi, ui_editsource_info->hash);
         BLI_ghashIterator_done(&ghi) == false;
         BLI_ghashIterator_step(&ghi)) {
      uiBut *but_key = BLI_ghashIterator_getKey(&ghi);
      if (but_key && ui_editsource_uibut_match(&ui_editsource_info->but_orig, but_key)) {
        but_store = BLI_ghashIterator_getValue(&ghi);
        break;
      }
    }

    if (but_store) {
      if (but_store->py_dbg_line_number != -1) {
        ret = editsource_text_edit(C, op, but_store->py_dbg_fn, but_store->py_dbg_line_number);
      }
      else {
        BKE_report(
            op->reports, RPT_ERROR, "Active button is not from a script, cannot edit source");
        ret = OPERATOR_CANCELLED;
      }
    }
    else {
      BKE_report(op->reports, RPT_ERROR, "Active button match cannot be found");
      ret = OPERATOR_CANCELLED;
    }

    ui_editsource_active_but_clear();

    // printf("%s: end\n", __func__);

    return ret;
  }

  BKE_report(op->reports, RPT_ERROR, "Active button not found");
  return OPERATOR_CANCELLED;
}

static void UI_OT_editsource(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Edit Source";
  ot->idname = "UI_OT_editsource";
  ot->description = "Edit UI source code of the active button";

  /* callbacks */
  ot->exec = editsource_exec;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Translation Operator
 * \{ */

/**
 * EditTranslation utility funcs and operator,
 *
 * \note this includes utility functions and button matching checks.
 * this only works in conjunction with a Python operator!
 */
static void edittranslation_find_po_file(const char *root,
                                         const char *uilng,
                                         char *path,
                                         const size_t maxlen)
{
  char tstr[32]; /* Should be more than enough! */

  /* First, full lang code. */
  BLI_snprintf(tstr, sizeof(tstr), "%s.po", uilng);
  BLI_join_dirfile(path, maxlen, root, uilng);
  BLI_path_append(path, maxlen, tstr);
  if (BLI_is_file(path)) {
    return;
  }

  /* Now try without the second iso code part (_ES in es_ES). */
  {
    const char *tc = NULL;
    size_t szt = 0;
    tstr[0] = '\0';

    tc = strchr(uilng, '_');
    if (tc) {
      szt = tc - uilng;
      if (szt < sizeof(tstr)) {            /* Paranoid, should always be true! */
        BLI_strncpy(tstr, uilng, szt + 1); /* +1 for '\0' char! */
      }
    }
    if (tstr[0]) {
      /* Because of some codes like sr_SR@latin... */
      tc = strchr(uilng, '@');
      if (tc) {
        BLI_strncpy(tstr + szt, tc, sizeof(tstr) - szt);
      }

      BLI_join_dirfile(path, maxlen, root, tstr);
      strcat(tstr, ".po");
      BLI_path_append(path, maxlen, tstr);
      if (BLI_is_file(path)) {
        return;
      }
    }
  }

  /* Else no po file! */
  path[0] = '\0';
}

static int edittranslation_exec(bContext *C, wmOperator *op)
{
  uiBut *but = UI_context_active_but_get(C);
  if (but == NULL) {
    BKE_report(op->reports, RPT_ERROR, "Active button not found");
    return OPERATOR_CANCELLED;
  }

  wmOperatorType *ot;
  PointerRNA ptr;
  char popath[FILE_MAX];
  const char *root = U.i18ndir;
  const char *uilng = BLT_lang_get();

  uiStringInfo but_label = {BUT_GET_LABEL, NULL};
  uiStringInfo rna_label = {BUT_GET_RNA_LABEL, NULL};
  uiStringInfo enum_label = {BUT_GET_RNAENUM_LABEL, NULL};
  uiStringInfo but_tip = {BUT_GET_TIP, NULL};
  uiStringInfo rna_tip = {BUT_GET_RNA_TIP, NULL};
  uiStringInfo enum_tip = {BUT_GET_RNAENUM_TIP, NULL};
  uiStringInfo rna_struct = {BUT_GET_RNASTRUCT_IDENTIFIER, NULL};
  uiStringInfo rna_prop = {BUT_GET_RNAPROP_IDENTIFIER, NULL};
  uiStringInfo rna_enum = {BUT_GET_RNAENUM_IDENTIFIER, NULL};
  uiStringInfo rna_ctxt = {BUT_GET_RNA_LABEL_CONTEXT, NULL};

  if (!BLI_is_dir(root)) {
    BKE_report(op->reports,
               RPT_ERROR,
               "Please set your Preferences' 'Translation Branches "
               "Directory' path to a valid directory");
    return OPERATOR_CANCELLED;
  }
  ot = WM_operatortype_find(EDTSRC_I18N_OP_NAME, 0);
  if (ot == NULL) {
    BKE_reportf(op->reports,
                RPT_ERROR,
                "Could not find operator '%s'! Please enable ui_translate add-on "
                "in the User Preferences",
                EDTSRC_I18N_OP_NAME);
    return OPERATOR_CANCELLED;
  }
  /* Try to find a valid po file for current language... */
  edittranslation_find_po_file(root, uilng, popath, FILE_MAX);
  // printf("po path: %s\n", popath);
  if (popath[0] == '\0') {
    BKE_reportf(
        op->reports, RPT_ERROR, "No valid po found for language '%s' under %s", uilng, root);
    return OPERATOR_CANCELLED;
  }

  UI_but_string_info_get(C,
                         but,
                         &but_label,
                         &rna_label,
                         &enum_label,
                         &but_tip,
                         &rna_tip,
                         &enum_tip,
                         &rna_struct,
                         &rna_prop,
                         &rna_enum,
                         &rna_ctxt,
                         NULL);

  WM_operator_properties_create_ptr(&ptr, ot);
  RNA_string_set(&ptr, "lang", uilng);
  RNA_string_set(&ptr, "po_file", popath);
  RNA_string_set(&ptr, "but_label", but_label.strinfo);
  RNA_string_set(&ptr, "rna_label", rna_label.strinfo);
  RNA_string_set(&ptr, "enum_label", enum_label.strinfo);
  RNA_string_set(&ptr, "but_tip", but_tip.strinfo);
  RNA_string_set(&ptr, "rna_tip", rna_tip.strinfo);
  RNA_string_set(&ptr, "enum_tip", enum_tip.strinfo);
  RNA_string_set(&ptr, "rna_struct", rna_struct.strinfo);
  RNA_string_set(&ptr, "rna_prop", rna_prop.strinfo);
  RNA_string_set(&ptr, "rna_enum", rna_enum.strinfo);
  RNA_string_set(&ptr, "rna_ctxt", rna_ctxt.strinfo);
  const int ret = WM_operator_name_call_ptr(C, ot, WM_OP_INVOKE_DEFAULT, &ptr, NULL);

  /* Clean up */
  if (but_label.strinfo) {
    MEM_freeN(but_label.strinfo);
  }
  if (rna_label.strinfo) {
    MEM_freeN(rna_label.strinfo);
  }
  if (enum_label.strinfo) {
    MEM_freeN(enum_label.strinfo);
  }
  if (but_tip.strinfo) {
    MEM_freeN(but_tip.strinfo);
  }
  if (rna_tip.strinfo) {
    MEM_freeN(rna_tip.strinfo);
  }
  if (enum_tip.strinfo) {
    MEM_freeN(enum_tip.strinfo);
  }
  if (rna_struct.strinfo) {
    MEM_freeN(rna_struct.strinfo);
  }
  if (rna_prop.strinfo) {
    MEM_freeN(rna_prop.strinfo);
  }
  if (rna_enum.strinfo) {
    MEM_freeN(rna_enum.strinfo);
  }
  if (rna_ctxt.strinfo) {
    MEM_freeN(rna_ctxt.strinfo);
  }

  return ret;
}
