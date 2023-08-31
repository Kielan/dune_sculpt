#include <string.h>

#include "mem_guardedalloc.h"

#include "types_armature.h"
#include "types_material.h"
#include "types_mod.h" /* for handling geometry nodes properties */
#include "types_object.h"   /* for OB_DATA_SUPPORT_ID */
#include "types_screen.h"
#include "types_text.h"

#include "lib_dunelib.h"
#include "lib_math_color.h"

#include "BLF_api.h"
#include "lang.h"

#include "dune_cxt.h"
#include "dune_global.h"
#include "dune_idprop.h"
#include "dune_layer.h"
#include "dune_lib_id.h"
#include "dune_lib_override.h"
#include "dune_material.h"
#include "dune_node.h"
#include "dune_report.h"
#include "dune_screen.h"
#include "dune_text.h"

#include "IMB_colormanagement.h"

#include "graph.h"

#include "api_access.h"
#include "api_define.h"
#include "api_prototypes.h"
#include "api_types.h"

#include "ui_interface.h"

#include "ui_intern.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ed_object.h"
#include "ed_paint.h"

/* for Copy As Driver */
#include "ed_keyframing.h"

/* only for UI_OT_editsource */
#include "dune_main.h"
#include "lib_ghash.h"
#include "ed_screen.h"
#include "ed_text.h"

/** Immediate redraw helper
 *
 * Generally handlers shouldn't do any redrawing, that includes the layout/btn definitions. That
 * violates the Model-View-Controller pattern.
 *
 * But there are some operators which really need to re-run the layout definitions for various
 * reasons. For example, "Edit Source" does it to find out which exact Python code added a button.
 * Other operators may need to access btns that aren't currently visible. In Dune's UI code
 * design that typically means just not adding the btn in the first place, for a particular
 * redraw. So the op needs to change cxt and re-create the layout, so the button becomes
 * available to act on. */

/* Copy Python Command Operator */

static bool copy_pycmd_btnpoll(Cxt *C)
{
  uiBtn *btn = ui_cxt_active_btn_get(C);

  if (btn && (btn->optype != NULL)) {
    return 1;
  }

  return 0;
}

static int copy_py_cmd_btnex(Cxt *C, wmOp *UNUSED(op))
{
  uiBtn *btn = ui_cxt_active_btn_get(C);

  if (btn && (btn->optype != NULL)) {
    ApiPtr *opptr;
    char *str;
    opptr = ui_btn_op_ptr_get(btn); /* allocated when needed, the btn owns it */

    str = wm_op_pystring_ex(C, NULL, false, true, btn->optype, opptr);

    wm_clipboard_txt_set(str, 0);

    mem_freen(str);

    return OP_FINISHED;
  }

  return OP_CANCELLED;
}

static void UI_OT_copy_pycmd_btn(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Copy Python Command";
  ot->idname = "UI_OT_copy_pycmd_btnn";
  ot->description = "Copy the Python command matching this button";

  /* callbacks */
  ot->ex = copy_py_cmd_btn_ex;
  ot->poll = copy_py_cmd_btn_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/* -------------------------------------------------------------------- */
/* Reset to Default Values Btn Operator **/

static int op_btnprop_finish(Cxt *C, ApiPtr *ptr, ApiProp *prop)
{
  Id *id = ptr->owner_id;

  /* perform updates required for this property */
  apiprop_update(C, ptr, prop);

  /* as if we pressed the button */
  ui_cxt_active_btnprop_handle(C, false);

  /* Since we don't want to undo _all_ edits to settings, eg window
   * edits on the screen or on operator settings.
   * it might be better to move undo's inline */
  if (id && ID_CHECK_UNDO(id)) {
    /* do nothing, go ahead with undo */
    return OP_FINISHED;
  }
  return OP_CANCELLED;
}

static int op_btnprop_finish_with_undo(Cxt *C,
                                       ApiPtr *ptr,
                                       ApiProp *prop)
{
  /* Perform updates required for this prop. */
  apiprop_update(C, ptr, prop);

  /* As if we pressed the btn. */
  ui_cxt_active_btnprop_handle(C, true);

  return OP_FINISHED;
}

static bool reset_default_btnpoll(Cxt *C)
{
  ApiProp ptr;
  ApiProp *prop;
  int index;

  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  return (ptr.data && prop && apiprop_editable(&ptr, prop));
}

static int reset_default_btnEx(Cxt *C, wmOp *op)
{
  ApiPtr ptr;
  ApiProp *prop;
  int index;
  const bool all = api_bool_get(op->ptr, "all");

  /* try to reset the nominated setting to its default value */
  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data && prop && apiProp_editable(&ptr, prop)) {
    if (ApiProp_reset(&ptr, prop, (all) ? -1 : index)) {
      return op_btnprop_finish_with_undo(C, &ptr, prop);
    }
  }

  return OP_CANCELLED;
}

static void UI_OT_reset_default_btn(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Reset to Default Value";
  ot->idname = "UI_OT_reset_default_btn";
  ot->description = "Reset this prop's value to its default value";

  /* callbacks */
  ot->poll = reset_default_btnpoll;
  ot->ex = reset_default_btnEx;

  /* flags */
  /* Don't set OPTYPE_UNDO because op_btnprop_finish_with_undo
   * is responsible for the undo push. */
  ot->flag = 0;

  /* properties */
  api_def_bool(ot->srna, "all", 1, "All", "Reset to default values all elements of the array");
}

/* -------------------------------------------------------------------- */
/** Assign Value as Default Button Operator **/

static bool assign_default_btn_poll(Cxt *C)
{
  ApiProp ptr;
  ApiProp *prop;
  int index;

  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  if (ptr.data && prop && ApiProp_editable(&ptr, prop)) {
    const PropType type = ApiProp_type(prop);

    return apiprop_is_idprop(prop) && !apiprop_array_check(prop) &&
           ELEM(type, PROP_INT, PROP_FLOAT);
  }

  return false;
}

static int assign_default_btnEx(Cxt *C, wmOp *UNUSED(op))
{
  ApiPtr ptr;
  ApiProp *prop;
  int index;

  /* try to reset the nominated setting to its default value */
  ui_cxt_active_btnProp_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data && prop && apiProp_editable(&ptr, prop)) {
    if (apiProp_assign_default(&ptr, prop)) {
      return op_btnProp_finish(C, &ptr, prop);
    }
  }

  return OP_CANCELLED;
}

static void UI_OT_assign_default_btn(wmOpType *ot)
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

  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  const uint override_status = apiprop_override_lib_status(
      cxt_data_main(C), &ptr, prop, index);

  return (ptr.data && prop && (override_status & API_OVERRIDE_STATUS_OVERRIDABLE));
}

static int override_type_set_btnex(Cxt *C, wmOp *op)
{
  ApiProp ptr;
  ApiProp *prop;
  int index;
  bool created;
  const bool all = api_bool_get(op->ptr, "all");
  const int op_type = api_enum_get(op->ptr, "type");

  short op;

  switch (op_type) {
    case UIOverride_Type_NOOP:
      operation = IDOVERRIDE_LIB_OP_NOOP;
      break;
    case UIOverride_Type_Replace:
      operation = IDOVERRIDE_LIB_OP_REPLACE;
      break;
    case UIOverride_Type_Difference:
      /* override code will automatically switch to subtract if needed. */
      operation = IDOVERRIDE_LIB_OP_ADD;
      break;
    case UIOverride_Type_Factor:
      operation = IDOVERRIDE_LIB_OP_MULTIPLY;
      break;
    default:
      operation = IDOVERRIDE_LIB_OP_REPLACE;
      lib_assert(0);
      break;
  }

  /* try to reset the nominated setting to its default value */
  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  lib_assert(ptr.owner_id != NULL);

  if (all) {
    index = -1;
  }

  IdOverrideLibPropOp *opop = apiprop_override_prop_op_get(
      cxt_data_main(C), &ptr, prop, op, index, true, NULL, &created);

  if (opop == NULL) {
    /* Sometimes e.g. API cannot generate a path to the given property. */
    dune_reportf(op->reports, RPT_WARNING, "Failed to create the override operation");
    return OP_CANCELLED;
  }

  if (!created) {
    opop->op = op;
  }

  /* Outliner e.g. has to be aware of this change. */
  wm_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return op_btnprop_finish(C, &ptr, prop);
}

static int override_type_set_btn_invoke(Cxt *C,
                                        wmOp *op,
                                        const wmEvent *UNUSED(event))
{
#if 0 /* Disabled for now */
  return wm_menu_invoke_ex(C, op, WM_OP_INVOKE_DEFAULT);
#else
  api_enum_set(op->ptr, "type", IDOVERRIDE_LIB_OP_REPLACE);
  return override_type_set_btnex(C, op);
#endif
}

static void UI_OT_override_type_set_btn(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Define Override Type";
  ot->idname = "UI_OT_override_type_set_btn";
  ot->description = "Create an override op, or set the type of an existing one";

  /* callbacks */
  ot->poll = override_type_set_btnpoll;
  ot->ex = override_type_set_btnex;
  ot->invoke = override_type_set_btninvoke;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  api_def_bool(ot->sapi, "all", 1, "All", "Reset to default values all elements of the array");
  ot->prop = api_def_enum(ot->sapi,
                          "type",
                          override_type_items,
                          UIOverride_Type_Replace,
                          "Type",
                          "Type of override op");
  /* TODO: add itemf callback, not all options are available for all data types... */
}

static bool override_remove_btnpoll(Cxt *C)
{
  ApiPtr ptr;
  ApiProp *prop;
  int index;

  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  const uint override_status = ApiProp_override_lib_status(
      cxt_data_main(C), &ptr, prop, index);

  return (ptr.data && ptr.owner_id && prop && (override_status & API_OVERRIDE_STATUS_OVERRIDDEN));
}

static int override_remove_btnex(Cxt *C, wmOp *op)
{
  Main *duneMain = cxt_data_main(C);
  ApiPtr ptr, id_refptr, src;
  ApiPtr *prop;
  int index;
  const bool all = api_bool_get(op->ptr, "all");

  /* try to reset the nominated setting to its default value */
  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  Id *id = ptr.owner_id;
  IdOverrideLibProp *oprop = apiprop_override_prop_find(duneMain, &ptr, prop, &id);
  lib_assert(oprop != NULL);
  lib_assert(id != NULL && id->override_library != NULL);

  const bool is_template = ID_IS_OVERRIDE_LIB_TEMPLATE(id);

  /* We need source (i.e. linked data) to restore values of deleted overrides...
   * If this is an override template, we obviously do not need to restore anything. */
  if (!is_template) {
    ApiProp *src_prop;
    apiid_ptr_create(id->override_lib->reference, &id_refptr);
    if (!apipath_resolve_prop(&id_refptr, oprop->api_path, &src, &src_prop)) {
      lib_assert_msg(0, "Failed to create matching source (linked data) API pointer");
    }
  }

  if (!all && index != -1) {
    bool is_strict_find;
    /* Remove override operation for given item,
     * add singular operations for the other items as needed. */
    IdOverrideLibPropOp *opop = dune_lib_override_libprop_op_find(
        oprop, NULL, NULL, index, index, false, &is_strict_find);
    lib_assert(opop != NULL);
    if (!is_strict_find) {
      /* No specific override operation, we have to get generic one,
       * and create item-specific override operations for all but given index,
       * before removing generic one. */
      for (int idx = apiprop_array_length(&ptr, prop); idx--;) {
        if (idx != index) {
          dune_lib_override_libprop_op_get(
              oprop, opop->op, NULL, NULL, idx, idx, true, NULL, NULL);
        }
      }
    }
    dune_lib_override_libprop_op_delete(oprop, opop);
    if (!is_template) {
      apiprop_copy(duneMain, &ptr, &src, prop, index);
    }
    if (lib_list_is_empty(&oprop->ops)) {
      dune_lib_override_libprop_delete(id->override_lib, oprop);
    }
  }
  else {
    /* Just remove whole generic override operation of this property. */
    dune_lib_override_libprop_delete(id->override_lib, oprop);
    if (!is_template) {
      apiProp_copy(duneMain, &ptr, &src, prop, -1);
    }
  }

  /* Outliner e.g. has to be aware of this change. */
  wm_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return op_btnprop_finish(C, &ptr, prop);
}

static void UI_OT_override_remove_btn(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Remove Override";
  ot->idname = "UI_OT_override_remove_btn";
  ot->description = "Remove an override operation";

  /* callbacks */
  ot->poll = override_remove_btnpoll;
  ot->ex = override_remove_btnex;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* properties */
  api_def_bool(ot->sapi, "all", 1, "All", "Reset to default values all elements of the array");
}

/* -------------------------------------------------------------------- */
/** Copy To Selected Operator **/

#define NOT_NULL(assignment) ((assignment) != NULL)
#define NOT_API_NULL(assignment) ((assignment).data != NULL)

static void ui_cxt_selected_bones_via_pose(Cxt *C, List *r_lb)
{
  List lb;
  lb = cxt_data_collection_get(C, "selected_pose_bones");

  if (!lib_list_is_empty(&lb)) {
    LIST_FOREACH (CollectionPtrLink *, link, &lb) {
      DunePoseChannel *pchan = link->ptr.data;
      apiptr_create(link->ptr.owner_id, &API_Bone, pchan->bone, &link->ptr);
    }
  }

  *r_lb = lb;
}

bool ui_cxt_copy_to_selected_list(DuneContext *C,
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
   * to preserve efficiency. Only props defined through API are handled, as
   * custom props cannot be assumed to be valid for all instances.
   *
   * Props owned by the ID are handled by the 'if (ptr->owner_id)' case below.  */
  if (!apiprop_is_idprop(prop) && api_struct_is_a(ptr->type, &api_PropGroup)) {
    ApiPtr owner_ptr;
    char *idpath = NULL;

    /* First, check the active PoseBone and PoseBone->Bone. */
    if (NOT_API_NULL(
            owner_ptr = cxt_data_ptr_get_type(C, "active_pose_bone", &api_PoseBone))) {
      if (NOT_NULL(idpath = apipath_from_struct_to_idprop(&owner_ptr, ptr->data))) {
        *r_lb = cxt_data_collection_get(C, "selected_pose_bones");
      }
      else {
        DunePoseChannel *pchan = owner_ptr.data;
        apiptr_create(owner_ptr.owner_id, &api_Bone, pchan->bone, &owner_ptr);

        if (NOT_NULL(idpath = api_path_from_struct_to_idprop(&owner_ptr, ptr->data))) {
          ui_cxt_selected_bones_via_pose(C, r_lb);
        }
      }
    }

    if (idpath == NULL) {
      /* Check the active EditBone if in edit mode. */
      if (NOT_API_NULL(
              owner_ptr = cxt_data_ptr_get_type_silent(C, "active_bone", &Api_EditBone)) &&
          NOT_NULL(idpath = apipath_from_struct_to_idprop(&owner_ptr, ptr->data))) {
        *r_lb = cxt_data_collection_get(C, "selected_editable_bones");
      }

      /* Add other simple cases here (Node, NodeSocket, Sequence, ViewLayer etc). */
    }

    if (idpath) {
      *r_path = lib_sprintfn("%s.%s", idpath, apiprop_id(prop));
      mem_freen(idpath);
      return true;
    }
  }

  if (api_struct_is_a(ptr->type, &api_EditBone)) {
    *r_lb = cxt_data_collection_get(C, "selected_editable_bones");
  }
  else if (api_struct_is_a(ptr->type, &api_PoseBone)) {
    *r_lb = cxt_data_collection_get(C, "selected_pose_bones");
  }
  else if (api_struct_is_a(ptr->type, &api_Bone)) {
    ui_cxt_selected_bones_via_pose(C, r_lb);
  }
  else if (api_struct_is_a(ptr->type, &api_Sequence)) {
    /* Special case when we do this for 'Sequence.lock'.
     * (if the sequence is locked, it won't be in "selected_editable_sequences"). */
    const char *prop_id = api_prop_id(prop);
    if (STREQ(prop_id, "lock")) {
      *r_lb = cxt_data_collection_get(C, "selected_sequences");
    }
    else {
      *r_lb = ctx_data_collection_get(C, "selected_editable_sequences");
    }
    /* Account for properties only being available for some sequence types. */
    ensure_list_items_contain_prop = true;
  }
  else if (api_struct_is_a(ptr->type, &api_FCurve)) {
    *r_lb = cxt_data_collection_get(C, "selected_editable_fcurves");
  }
  else if (api_struct_is_a(ptr->type, &api_Keyframe)) {
    *r_lb = cxt_data_collection_get(C, "selected_editable_keyframes");
  }
  else if (api_struct_is_a(ptr->type, &api_Action)) {
    *r_lb = cxt_data_collection_get(C, "selected_editable_actions");
  }
  else if (api_struct_is_a(ptr->type, &api_NlaStrip)) {
    *r_lb = cxt_data_collection_get(C, "selected_nla_strips");
  }
  else if (api_struct_is_a(ptr->type, &api_MovieTrackingTrack)) {
    *r_lb = cxt_data_collection_get(C, "selected_movieclip_tracks");
  }
  else if (api_struct_is_a(ptr->type, &api_Constraint) &&
           (path_from_bone = apipath_resolve_from_type_to_prop(ptr, prop, &api_PoseBone)) !=
               NULL) {
    *r_lb = cxt_data_collection_get(C, "selected_pose_bones");
    *r_path = path_from_bone;
  }
  else if (api_struct_is_a(ptr->type, &api_Node) || api_struct_is_a(ptr->type, &api_NodeSocket)) {
    List lb = {NULL, NULL};
    char *path = NULL;
    Node *node = NULL;

    /* Get the node we're editing */
    if (api_struct_is_a(ptr->type, &api_NodeSocket)) {
      NodeTree *ntree = (bNodeTree *)ptr->owner_id;
      NodeSocket *sock = ptr->data;
      if (nodeFindNode(ntree, sock, &node, NULL)) {
        if ((path = api_path_resolve_from_type_to_prop(ptr, prop, &RNA_Node)) != NULL) {
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
      lb = cxt_data_collection_get(C, "selected_nodes");

      LIST_FOREACH_MUTABLE (CollectionPtrLink *, link, &lb) {
        Node *node_data = link->ptr.data;

        if (node_data->type != node->type) {
          lib_remlink(&lb, link);
          mem_freen(link);
        }
      }
    }

    *r_lb = lb;
    *r_path = path;
  }
  else if (ptr->owner_id) {
    Id *id = ptr->owner_id;

    if (GS(id->name) == ID_OB) {
      *r_lb = cxt_data_collection_get(C, "selected_editable_objects");
      *r_use_path_from_id = true;
      *r_path = api_path_from_id_to_prop(ptr, prop);
    }
    else if (OB_DATA_SUPPORT_ID(GS(id->name))) {
      /* check we're using the active object */
      const short id_code = GS(id->name);
      List lb = cxt_data_collection_get(C, "selected_editable_objects");
      char *path = api_path_from_id_to_prop(ptr, prop);

      /* de-duplicate obdata */
      if (!lib_list_is_empty(&lb)) {
        LIST_FOREACH (CollectionPtrLink *, link, &lb) {
          Object *ob = (Object *)link->ptr.owner_id;
          if (ob->data) {
            Id *id_data = ob->data;
            id_data->tag |= LIB_TAG_DOIT;
          }
        }

        LIST_FOREACH_MUTABLE (CollectionPtrLink *, link, &lb) {
          Object *ob = (Object *)link->ptr.owner_id;
          Id *id_data = ob->data;

          if ((id_data == NULL) || (id_data->tag & LIB_TAG_DOIT) == 0 || ID_IS_LINKED(id_data) ||
              (GS(id_data->name) != id_code)) {
            lib_remlink(&lb, link);
            mem_freen(link);
          }
          else {
            /* Avoid prepending 'data' to the path. */
            api_id_ptr_create(id_data, &link->ptr);
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
      if ((*r_path = api_path_resolve_from_type_to_prop(ptr, prop, &Api_Seq)) != NULL) {
        /* Special case when we do this for 'Sequence.lock'.
         * (if the sequence is locked, it won't be in "selected_editable_sequences"). */
        const char *prop_id = api_prop_identifier(prop);
        if (STREQ(prop_id, "lock")) {
          *r_lb = cxt_data_collection_get(C, "selected_seqs");
        }
        else {
          *r_lb = cxt_data_collection_get(C, "selected_editable_seqs");
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
    const char *prop_id = api_prop_id(prop);
    LIST_FOREACH_MUTABLE (CollectionPtrLink *, link, r_lb) {
      if ((ptr->type != link->ptr.type) &&
          (api_struct_type_find_prop(link->ptr.type, prop_id) != prop)) {
        lib_remlink(r_lb, link);
        mem_freen(link);
      }
    }
  }

  return true;
}

bool ui_cxt_copy_to_selected_check(ApiPtr *ptr,
                                   ApiPtr *ptr_link,
                                   ApiProp *prop,
                                   const char *path,
                                   bool use_path_from_id,
                                   ApiPtr *r_ptr,
                                   ApiProp **r_prop)
{
  ApiPtr idptr;
  ApiProp *lprop;
  ApiPtr lptr;

  if (ptr_link->data == ptr->data) {
    return false;
  }

  if (use_path_from_id) {
    /* Path relative to ID. */
    lprop = NULL;
    api_id_ptr_create(ptr_link->owner_id, &idptr);
    api_path_resolve_prop(&idptr, path, &lptr, &lprop);
  }
  else if (path) {
    /* Path relative to elements from list. */
    lprop = NULL;
    api_path_resolve_prop(ptr_link, path, &lptr, &lprop);
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

  if (api_prop_type(lprop) != api_prop_type(prop)) {
    return false;
  }

  /* Check prop ptrs matching.
   * For ID props, these ptrs match:
   * - If the prop is API defined on an existing class (and they are equally named).
   * - Never for ID props on specific ID (even if they are equally named).
   * - Never for NodesModSettings props (even if they are equally named).
   *
   * Be permissive on ID props in the following cases:
   * - NodesModSettings props
   *   - (special check: only if the node-group matches, since the 'Input_n' props are name
   *      based and similar on potentially very different node-groups).
   * - ID props on specific ID
   *   - (no special check, copying seems OK [even if type does not match -- does not do anything
   *      then])
   */
  bool ignore_prop_eq = api_prop_is_idprop(lprop) && api_prop_is_idprop(prop);
  if (api_struct_is_a(lptr.type, &Api_NodesMod) &&
      api_struct_is_a(ptr->type, &Api_NodesMod)) {
    ignore_prop_eq = false;

    NodesModData *nmd_link = (NodesModData *)lptr.data;
    NodesModData *nmd_src = (NodesModData *)ptr->data;
    if (nmd_link->node_group == nmd_src->node_group) {
      ignore_prop_eq = true;
    }
  }

  if ((lprop != prop) && !ignore_prop_eq) {
    return false;
  }

  if (!api_prop_editable(&lptr, lprop)) {
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

bool ui_cxt_copy_to_selected_check(ApiPtr *ptr,
                                   ApiPtr *ptr_link,
                                   ApiProp *prop,
                                   const char *path,
                                   bool use_path_from_id,
                                   ApiPtr *r_ptr,
                                   ApiProp **r_prop)
{
  ApiPtr idptr;
  ApiProp *lprop;
  ApiPtr lptr;

  if (ptr_link->data == ptr->data) {
    return false;
  }

  if (use_path_from_id) {
    /* Path relative to ID. */
    lprop = NULL;
    api_id_ptr_create(ptr_link->owner_id, &idptr);
    api_path_resolve_prop(&idptr, path, &lptr, &lprop);
  }
  else if (path) {
    /* Path relative to elements from list. */
    lprop = NULL;
    api_path_resolve_prop(ptr_link, path, &lptr, &lprop);
  }
  else {
    lptr = *ptr_link;
    lprop = prop;
  }

  if (lptr.data == ptr->data) {
    /* temp_ptr might not be the same as ptr_link! */
    return false;
  }

  /* Skip non-existing props on link. This was previously covered with the `lprop != prop`
   * check but we are now more permissive when it comes to Id props, see below. */
  if (lprop == NULL) {
    return false;
  }

  if (api_prop_type(lprop) != api_prop_type(prop)) {
    return false;
  }

  /* Check prop ptrs matching.
   * For Id props, these ptrs match:
   * - If the prop is API defined on an existing class (and they are equally named).
   * - Never for ID props on specific ID (even if they are equally named).
   * - Never for NodesModSettings props (even if they are equally named).
   *
   * Be permissive on Id props in the following cases:
   * - NodesModSettings props
   *   - (special check: only if the node-group matches, since the 'Input_n' properties are name
   *      based and similar on potentially very different node-groups).
   * - ID props on specific ID
   *   - (no special check, copying seems OK [even if type does not match -- does not do anything
   *      then])
   */
  bool ignore_prop_eq = api_prop_is_idprop(lprop) && api_prop_is_idprop(prop);
  if (api_struct_is_a(lptr.type, &Api_NodesMod) &&
      api_struct_is_a(ptr->type, &Api_NodesMod)) {
    ignore_prop_eq = false;

    NodesModData *nmd_link = (NodesModData *)lptr.data;
    NodesModData *nmd_src = (NodesModData *)ptr->data;
    if (nmd_link->node_group == nmd_src->node_group) {
      ignore_prop_eq = true;
    }
  }

  if ((lprop != prop) && !ignore_prop_eq) {
    return false;
  }

  if (!api_prop_editable(&lptr, lprop)) {
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

/* Called from both exec & poll.
 *
 * Normally we wouldn't call a loop from within a poll fn,
 * however this is a special case, and for regular poll calls, getting
 * the context from the btn will fail early. */
static bool copy_to_selected_btn(Cxt *C, bool all, bool poll)
{
  Main *main = cxt_data_main(C);
  ApiPtr ptr, lptr;
  ApiProp *prop, *lprop;
  bool success = false;
  int index;

  /* try to reset the nominated setting to its default value */
  ui_cxt_active_but_prop_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data == NULL || prop == NULL) {
    return false;
  }

  char *path = NULL;
  bool use_path_from_id;
  List lb = {NULL};

  if (!ui_cxt_copy_to_selected_list(C, &ptr, prop, &lb, &use_path_from_id, &path)) {
    return false;
  }
  if (lib_list_is_empty(&lb)) {
    MEM_SAFE_FREE(path);
    return false;
  }

  LIST_FOREACH (CollectionPtrLink *, link, &lb) {
    if (link->ptr.data == ptr.data) {
      continue;
    }

    if (!ui_cxt_copy_to_selected_check(
            &ptr, &link->ptr, prop, path, use_path_from_id, &lptr, &lprop)) {
      continue;
    }

    if (poll) {
      success = true;
      break;
    }
    if (api_prop_copy(main, &lptr, &ptr, prop, (all) ? -1 : index)) {
      api_prop_update(C, &lptr, prop);
      success = true;
    }
  }

  MEM_SAFE_FREE(path);
  lib_freelistn(&lb);

  return success;
}

static bool copy_to_selected_btn_poll(Cxt *C)
{
  return copy_to_selected_btn(C, false, true);
}

static int copy_to_selected_btn_ex(Cxt *C, wmOp *op)
{
  bool success;

  const bool all = api_bool_get(op->ptr, "all");

  success = copy_to_selected_btn(C, all, false);

  return (success) ? OP_FINISHED : OPERATOR_CANCELLED;
}

static void UI_OT_copy_to_selected_button(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Copy to Selected";
  ot->idname = "UI_OT_copy_to_selected_btn";
  ot->description = "Copy prop from this object to selected objects or bones";

  /* callbacks */
  ot->poll = copy_to_selected_btn_poll;
  ot->ex = copy_to_selected_btn_ex;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* props */
  api_def_bool(ot->sapi, "all", true, "All", "Copy to selected all elements of the array");
}

/* Jump to Target Op */

/** Jump to the object or bone ref by the ptr, or check if it is possible. */
static bool jump_to_target_ptr(Cxt *C, ApiPtr ptr, const bool poll)
{
  if (api_ptr_is_null(&ptr)) {
    return false;
  }

  /* Verify pointer type. */
  char bone_name[MAXBONENAME];
  const ApiStruct *target_type = NULL;

  if (ELEM(ptr.type, &Api_EditBone, &Api_PoseBone, &Api_Bone)) {
    api_string_get(&ptr, "name", bone_name);
    if (bone_name[0] != '\0') {
      target_type = &Api_Bone;
    }
  }
  else if (api_struct_is_a(ptr.type, &RNA_Object)) {
    target_type = &Api_Object;
  }

  if (target_type == NULL) {
    return false;
  }

  /* Find the containing Object. */
  ViewLayer *view_layer = cxt_data_view_layer(C);
  Base *base = NULL;
  const short id_type = GS(ptr.owner_id->name);
  if (id_type == ID_OB) {
    base = dune_view_layer_base_find(view_layer, (Object *)ptr.owner_id);
  }
  else if (OB_DATA_SUPPORT_ID(id_type)) {
    base = ed_object_find_first_by_data_id(view_layer, ptr.owner_id);
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
    if (target_type == &Api_Bone) {
      ok = ed_object_jump_to_bone(C, base->object, bone_name, reveal_hidden);
    }
    else if (target_type == &Api_Object) {
      ok = ed_object_jump_to_object(C, base->object, reveal_hidden);
    }
    else {
      lib_assert(0);
    }
  }
  return ok;
}

/* Jump to the object or bone referred to by the current UI field value.
 *
 * quite heavy for a poll cb, but the op is only
 * used as a right click menu item for certain UI field types, and
 * this will fail quickly if the cxt is completely unsuitable */
static bool jump_to_target_btn(Cxt *C, bool poll)
{
  ApiPtr ptr, target_ptr;
  ApiProp *prop;
  int index;

  ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index);

  /* If there is a valid prop... */
  if (ptr.data && prop) {
    const PropType type = api_prop_type(prop);

    /* For pointer properties, use their value directly. */
    if (type == PROP_PTR) {
      target_ptr = api_prop_ptr_get(&ptr, prop);

      return jump_to_target_ptr(C, target_ptr, poll);
    }
    /* For string properties with prop_search, look up the search collection item. */
    if (type == PROP_STRING) {
      const uiBtn *btn = ui_cxt_active_btn_get(C);
      const uiBtnSearch *search_btn = (btn->type == UI_BTYPE_SEARCH_MENU) ? (uiBtnSearch *)btn :
                                                                            NULL;

      if (search_btn && search_btn->items_update_fn == ui_api_collection_search_update_fn) {
        uiApiCollectionSearch *coll_search = search_but->arg;

        char str_buf[MAXBONENAME];
        char *str_ptr = api_prop_string_get_alloc(&ptr, prop, str_buf, sizeof(str_buf), NULL);

        int found = api_prop_collection_lookup_string(
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

bool ui_jump_to_target_btn_poll(Cxt *C)
{
  return jump_to_target_btn(C, true);
}

static int jump_to_target_btn_ex(Cxt *C, wmOp *UNUSED(op))
{
  const bool success = jump_to_target_btn(C, false);

  return (success) ? OP_FINISHED : OP_CANCELLED;
}

static void UI_OT_jump_to_target_btn(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Jump to Target";
  ot->idname = "UI_OT_jump_to_target_btn";
  ot->description = "Switch to the target object or bone";

  /* callbacks */
  ot->poll = ui_jump_to_target_btn_poll;
  ot->ex = jump_to_target_btn_ex;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Edit Python Source Operator */

#ifdef WITH_PYTHON

/* ------------------------------------------------------------------------- */
/* EditSource Utility funcs and operator,
 * NOTE: this includes utility functions and button matching checks. */

typedef struct uiEditSourceStore {
  uiBtn btn_orig;
  GHash *hash;
} uiEditSourceStore;

typedef struct uiEditSourceBtnStore {
  char py_dbg_fn[FILE_MAX];
  int py_dbg_line_number;
} uiEditSourceBtnStore;

/* should only ever be set while the edit source operator is running */
static struct uiEditSourceStore *ui_editsource_info = NULL;

bool ui_editsource_enable_check(void)
{
  return (ui_editsource_info != NULL);
}

static void ui_editsource_active_btn_set(uiBtn *btn)
{
  lib_assert(ui_editsource_info == NULL);

  ui_editsource_info = mem_callocn(sizeof(uiEditSourceStore), __func__);
  memcpy(&ui_editsource_info->btn_orig, btn, sizeof(uiBtn));

  ui_editsource_info->hash = lib_ghash_ptr_new(__func__);
}

static void ui_editsource_active_btn_clear(void)
{
  lib_ghash_free(ui_editsource_info->hash, NULL, mem_freen);
  mem_freen(ui_editsource_info);
  ui_editsource_info = NULL;
}

static bool ui_editsource_uibtn_match(uiBtn *btn_a, uiBtn *btn_b)
{
#  if 0
  printf("matching btns: '%s' == '%s'\n", btn_a->drawstr, btn_b->drawstr);
#  endif

  /* this just needs to be a 'good-enough' comparison so we can know beyond
   * reasonable doubt that these buttons are the same between redraws.
   * if this fails it only means edit-source fails - campbell */
  if (lib_rctf_compare(&btn_a->rect, &btn_b->rect, FLT_EPSILON) && (btn_a->type == btn_b->type) &&
      (btn_a->apiprop == btn_b->apiprop) && (btn_a->optype == btn_b->optype) &&
      (btn_a->unit_type == btn_b->unit_type) &&
      STREQLEN(but_a->drawstr, btn_b->drawstr, UI_MAX_DRAW_STR)) {
    return true;
  }
  return false;
}

void ui_editsource_active_btn_test(uiBtn *btn)
{
  extern void PyC_FileAndNum_Safe(const char **r_filename, int *r_lineno);

  struct uiEditSourceBtnStore *btn_store = mem_callocn(sizeof(uiEditSourceBtnStore), __func__);

  const char *fn;
  int line_number = -1;

#  if 0
  printf("comparing btns: '%s' == '%s'\n", btn->drawstr, ui_editsource_info->btn_orig.drawstr);
#  endif

  PyC_FileAndNum_Safe(&fn, &line_number);

  if (line_number != -1) {
    lib_strncpy(btn_store->py_dbg_fn, fn, sizeof(btn_store->py_dbg_fn));
    btn_store->py_dbg_line_number = line_number;
  }
  else {
    btn_store->py_dbg_fn[0] = '\0';
    btn_store->py_dbg_line_number = -1;
  }

  lin_ghash_insert(ui_editsource_info->hash, btn, btn_store);
}

void ui_editsource_btn_replace(const uiBtn *old_btn, uiBtn *new_btn)
{
  uiEditSourceBtnStore *btn_store = lib_ghash_lookup(ui_editsource_info->hash, old_btn);
  if (btn_store) {
    lib_ghash_remove(ui_editsource_info->hash, old_btn, NULL, NULL);
    lib_ghash_insert(ui_editsource_info->hash, new_btn, btn_store);
  }
}

static int editsource_text_edit(Cxt *C,
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

static void UI_OT_edittranslation_init(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Edit Translation";
  ot->idname = "UI_OT_edittranslation_init";
  ot->description = "Edit i18n in current language for the active button";

  /* callbacks */
  ot->exec = edittranslation_exec;
}

#endif /* WITH_PYTHON */

/** \} */

/* -------------------------------------------------------------------- */
/** \name Reload Translation Operator
 * \{ */

static int reloadtranslation_exec(bContext *UNUSED(C), wmOperator *UNUSED(op))
{
  BLT_lang_init();
  BLF_cache_clear();
  BLT_lang_set(NULL);
  UI_reinit_font();
  return OPERATOR_FINISHED;
}

static void UI_OT_reloadtranslation(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reload Translation";
  ot->idname = "UI_OT_reloadtranslation";
  ot->description = "Force a full reload of UI translation";

  /* callbacks */
  ot->exec = reloadtranslation_exec;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Press Button Operator
 * \{ */

static int ui_button_press_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  bScreen *screen = CTX_wm_screen(C);
  const bool skip_depressed = RNA_boolean_get(op->ptr, "skip_depressed");
  ARegion *region_prev = CTX_wm_region(C);
  ARegion *region = screen ? BKE_screen_find_region_xy(screen, RGN_TYPE_ANY, event->xy) : NULL;

  if (region == NULL) {
    region = region_prev;
  }

  if (region == NULL) {
    return OPERATOR_PASS_THROUGH;
  }

  CTX_wm_region_set(C, region);
  uiBut *but = UI_context_active_but_get(C);
  CTX_wm_region_set(C, region_prev);

  if (but == NULL) {
    return OPERATOR_PASS_THROUGH;
  }
  if (skip_depressed && (but->flag & (UI_SELECT | UI_SELECT_DRAW))) {
    return OPERATOR_PASS_THROUGH;
  }

  /* Weak, this is a workaround for 'UI_but_is_tool', which checks the operator type,
   * having this avoids a minor drawing glitch. */
  void *but_optype = but->optype;

  UI_but_execute(C, region, but);

  but->optype = but_optype;

  WM_event_add_mousemove(CTX_wm_window(C));

  return OPERATOR_FINISHED;
}

static void UI_OT_btn_ex(wmOperatorType *ot)
{
  ot->name = "Press Button";
  ot->idname = "UI_OT_btn_ex";
  ot->description = "Presses active button";

  ot->invoke = ui_btn_press_invoke;
  ot->flag = OPTYPE_INTERNAL;

  api_def_bool(ot->srna, "skip_depressed", 0, "Skip Depressed", "");
}

/* -------------------------------------------------------------------- */
/** Text Button Clear Operator **/

static int btn_string_clear_ex(duneContext *C, wmOperator *UNUSED(op))
{
  uiBut *btn = UI_ctx_active_btn_get_respect_menu(C);

  if (but) {
    ui_btn_active_string_clear_and_exit(C, btn);
  }

  return OPERATOR_FINISHED;
}

static void UI_OT_btn_string_clear(wmOperatorType *ot)
{
  ot->name = "Clear Button String";
  ot->idname = "UI_OT_btn_string_clear";
  ot->description = "Unsets the text of the active button";

  ot->poll = ED_operator_regionactive;
  ot->exec = button_string_clear_exec;
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;
}

/* -------------------------------------------------------------------- */
/** Drop Color Operator **/

bool UI_drop_color_poll(struct bContext *C, wmDrag *drag, const wmEvent *UNUSED(event))
{
  /* should only return true for regions that include buttons, for now
   * return true always */
  if (drag->type == WM_DRAG_COLOR) {
    SpaceImage *sima = CTX_wm_space_image(C);
    ARegion *region = CTX_wm_region(C);

    if (UI_but_active_drop_color(C)) {
      return 1;
    }

    if (sima && (sima->mode == SI_MODE_PAINT) && sima->image &&
        (region && region->regiontype == RGN_TYPE_WINDOW)) {
      return 1;
    }
  }

  return 0;
}

void UI_drop_color_copy(wmDrag *drag, wmDropBox *drop)
{
  uiDragColorHandle *drag_info = drag->poin;

  RNA_float_set_array(drop->ptr, "color", drag_info->color);
  RNA_boolean_set(drop->ptr, "gamma", drag_info->gamma_corrected);
}

static int drop_color_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  ARegion *region = CTX_wm_region(C);
  uiBut *but = NULL;
  float color[4];
  bool gamma;

  RNA_float_get_array(op->ptr, "color", color);
  gamma = RNA_boolean_get(op->ptr, "gamma");

  /* find button under mouse, check if it has RNA color property and
   * if it does copy the data */
  but = ui_region_find_active_but(region);

  if (but && but->type == UI_BTYPE_COLOR && but->rnaprop) {
    const int color_len = api_prop_array_length(&but->rnapoin, but->rnaprop);
    LIB_assert(color_len <= 4);

    /* keep alpha channel as-is */
    if (color_len == 4) {
      color[3] = api_prop_float_get_index(&btn->apipoin, btn->apiprop, 3);
    }

    if (api_prop_subtype(btn->apiprop) == PROP_COLOR_GAMMA) {
      if (!gamma) {
        IMB_colormanagement_scene_linear_to_srgb_v3(color);
      }
      api_prop_float_set_array(&btn->apipoin, btn->apiprop, color);
      api_prop_update(C, &btn->apipoin, but->apiprop);
    }
    else if (api_prop_subtype(but->rnaprop) == PROP_COLOR) {
      if (gamma) {
        IMB_colormanagement_srgb_to_scene_linear_v3(color);
      }
      api_prop_float_set_array(&btn->apipoin, btn->apiprop, color);
      api_prop_update(C, &btn->apipoin, btn->apiprop);
    }
  }
  else {
    if (gamma) {
      srgb_to_linearrgb_v3_v3(color, color);
    }

    ED_imapaint_bucket_fill(C, color, op, event->mval);
  }

  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

static void UI_OT_drop_color(wmOperatorType *ot)
{
  ot->name = "Drop Color";
  ot->idname = "UI_OT_drop_color";
  ot->description = "Drop colors to buttons";

  ot->invoke = drop_color_invoke;
  ot->flag = OPTYPE_INTERNAL;

  api_def_float_color(ot->srna, "color", 3, NULL, 0.0, FLT_MAX, "Color", "Source color", 0.0, 1.0);
  api_def_bool(ot->srna, "gamma", 0, "Gamma Corrected", "The source color is gamma corrected");
}

/* -------------------------------------------------------------------- */
/** Drop Name Operator **/

static int drop_name_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  uiBut *but = UI_but_active_drop_name_button(C);
  char *str = RNA_string_get_alloc(op->ptr, "string", NULL, 0, NULL);

  if (str) {
    ui_but_set_string_interactive(C, but, str);
    MEM_freeN(str);
  }

  return OPERATOR_FINISHED;
}

static void UI_OT_drop_name(wmOperatorType *ot)
{
  ot->name = "Drop Name";
  ot->idname = "UI_OT_drop_name";
  ot->description = "Drop name to button";

  ot->poll = ED_operator_regionactive;
  ot->invoke = drop_name_invoke;
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;

  api_def_string(
      ot->srna, "string", NULL, 0, "String", "The string value to drop into the button");
}

/* -------------------------------------------------------------------- */
/** UI List Search Operator **/

static bool ui_list_focused_poll(duneContext *C)
{
  const ARegion *region = ctx_wm_region(C);
  const wmWindow *win = ctx_wm_window(C);
  const uiList *list = ui_list_find_mouse_over(region, win->eventstate);

  return list != NULL;
}

/**
 * Ensure the filter options are set to be visible in the UI list.
 * return if the visibility changed, requiring a redraw.
 */
static bool ui_list_unhide_filter_options(uiList *list)
{
  if (list->filter_flag & UILST_FLT_SHOW) {
    /* Nothing to be done. */
    return false;
  }

  list->filter_flag |= UILST_FLT_SHOW;
  return true;
}

static int ui_list_start_filter_invoke(duneContext *C, wmOperator *UNUSED(op), const wmEvent *event)
{
  ARegion *region = ctx_wm_region(C);
  uiList *list = ui_list_find_mouse_over(region, event);
  /* Poll should check. */
  LIB_assert(list != NULL);

  if (ui_list_unhide_filter_options(list)) {
    ui_region_redraw_immediately(C, region);
  }

  if (!ui_textbtn_activate_api(C, region, list, "filter_name")) {
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

static void UI_OT_list_start_filter(wmOperatorType *ot)
{
  ot->name = "List Filter";
  ot->idname = "UI_OT_list_start_filter";
  ot->description = "Start entering filter text for the list in focus";

  ot->invoke = ui_list_start_filter_invoke;
  ot->poll = ui_list_focused_poll;
}

/* -------------------------------------------------------------------- */
/** UI Tree-View Drop Operator **/

static bool ui_tree_view_drop_poll(duneContext *C)
{
  const wmWindow *win = ctx_wm_window(C);
  const ARegion *region = ctx_wm_region(C);
  const uiTreeViewItemHandle *hovered_tree_item = UI_block_tree_view_find_item_at(
      region, win->eventstate->xy);

  return hovered_tree_item != NULL;
}

static int ui_tree_view_drop_invoke(duneContext *C, wmOperator *UNUSED(op), const wmEvent *event)
{
  if (event->custom != EVT_DATA_DRAGDROP) {
    return OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH;
  }

  const ARegion *region = ctx_wm_region(C);
  uiTreeViewItemHandle *hovered_tree_item = UI_block_tree_view_find_item_at(region, event->xy);

  if (!UI_tree_view_item_drop_handle(C, hovered_tree_item, event->customdata)) {
    return OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH;
  }

  return OPERATOR_FINISHED;
}

static void UI_OT_tree_view_drop(wmOperatorType *ot)
{
  ot->name = "Tree View drop";
  ot->idname = "UI_OT_tree_view_drop";
  ot->description = "Drag and drop items onto a tree item";

  ot->invoke = ui_tree_view_drop_invoke;
  ot->poll = ui_tree_view_drop_poll;

  ot->flag = OPTYPE_INTERNAL;
}

/* -------------------------------------------------------------------- */
/** UI Tree-View Item Rename Operator
 *
 * General purpose renaming operator for tree-views. Thanks to this, to add a rename button to
 * context menus for example, tree-view API users don't have to implement their own renaming
 * operators with the same logic as they already have for their #ui::AbstractTreeViewItem::rename()
 * override.
 *
 **/

static bool ui_tree_view_item_rename_poll(duneContext *C)
{
  const ARegion *region = CTX_wm_region(C);
  const uiTreeViewItemHandle *active_item = UI_block_tree_view_find_active_item(region);
  return active_item != NULL && UI_tree_view_item_can_rename(active_item);
}

static int ui_tree_view_item_rename_ex(duneContext *C, wmOperator *UNUSED(op))
{
  ARegion *region = CTX_wm_region(C);
  uiTreeViewItemHandle *active_item = UI_block_tree_view_find_active_item(region);

  UI_tree_view_item_begin_rename(active_item);
  ED_region_tag_redraw(region);

  return OPERATOR_FINISHED;
}

static void UI_OT_tree_view_item_rename(wmOperatorType *ot)
{
  ot->name = "Rename Tree-View Item";
  ot->idname = "UI_OT_tree_view_item_rename";
  ot->description = "Rename the active item in the tree";

  ot->exec = ui_tree_view_item_rename_exec;
  ot->poll = ui_tree_view_item_rename_poll;
  /* Could get a custom tooltip via the `get_description()` callback and another overridable
   * function of the tree-view. */

  ot->flag = OPTYPE_INTERNAL;
}

/* -------------------------------------------------------------------- */
/** Material Drag/Drop Operator **/

static bool ui_drop_material_poll(duneContext *C)
{
  ApiPtr ptr = ctx_data_ptr_get_type(C, "object", &RNA_Object);
  Object *ob = ptr.data;
  if (ob == NULL) {
    return false;
  }

  ApiPtr mat_slot = CTX_data_ptr_get_type(C, "material_slot", &api_MaterialSlot);
  if (api_ptr_is_null(&mat_slot)) {
    return false;
  }

  return true;
}

static int ui_drop_material_ex(duneContext *C, wmOperator *op)
{
  Main *duneMain = ctx_data_main(C);

  if (!api_struct_prop_is_set(op->ptr, "session_uuid")) {
    return OPERATOR_CANCELLED;
  }
  const uint32_t session_uuid = (uint32_t)api_int_get(op->ptr, "session_uuid");
  Material *ma = (Material *)DUNE_libblock_find_session_uuid(duneMain, ID_MA, session_uuid);
  if (ma == NULL) {
    return OPERATOR_CANCELLED;
  }

  ApiPtr ptr = ctx_data_ptr_get_type(C, "object", &api_Object);
  Object *ob = ptr.data;
  LIB_assert(ob);

  ApiPtr mat_slot = ctx_data_ptr_get_type(C, "material_slot", &api_MaterialSlot);
  LIB_assert(mat_slot.data);
  const int target_slot = api_int_get(&mat_slot, "slot_index") + 1;

  /* only drop grease pencil material on grease pencil objects */
  if ((ma->gp_style != NULL) && (ob->type != OB_GPENCIL)) {
    return OPERATOR_CANCELLED;
  }

  DUNE_object_material_assign(duneMain, ob, ma, target_slot, DUNE_MAT_ASSIGN_USERPREF);

  WM_event_add_notifier(C, NC_OBJECT | ND_OB_SHADING, ob);
  WM_event_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, NULL);
  WM_event_add_notifier(C, NC_MATERIAL | ND_SHADING_LINKS, ma);
  DEG_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);

  return OPERATOR_FINISHED;
}

static void UI_OT_drop_material(wmOperatorType *ot)
{
  ot->name = "Drop Material in Material slots";
  ot->description = "Drag material to Material slots in Properties";
  ot->idname = "UI_OT_drop_material";

  ot->poll = ui_drop_materialpoll;
  ot->exec = ui_drop_materialex;
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;

  ApiProp *prop = api_def_int(ot->srna,
                                  "session_uuid",
                                  0,
                                  INT32_MIN,
                                  INT32_MAX,
                                  "Session UUID",
                                  "Session UUID of the data-block to assign",
                                  INT32_MIN,
                                  INT32_MAX);
  api_def_prop_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
}

/* -------------------------------------------------------------------- */
/** Operator & Keymap Registration */

void ED_operatortypes_ui(void)
{
  WM_operatortype_append(UI_OT_copy_data_path_button);
  WM_operatortype_append(UI_OT_copy_as_driver_button);
  WM_operatortype_append(UI_OT_copy_python_command_button);
  WM_operatortype_append(UI_OT_reset_default_button);
  WM_operatortype_append(UI_OT_assign_default_button);
  WM_operatortype_append(UI_OT_unset_property_button);
  WM_operatortype_append(UI_OT_override_type_set_button);
  WM_operatortype_append(UI_OT_override_remove_button);
  WM_operatortype_append(UI_OT_copy_to_selected_button);
  WM_operatortype_append(UI_OT_jump_to_target_button);
  WM_operatortype_append(UI_OT_drop_color);
  WM_operatortype_append(UI_OT_drop_name);
  WM_operatortype_append(UI_OT_drop_material);
#ifdef WITH_PYTHON
  WM_operatortype_append(UI_OT_editsource);
  WM_operatortype_append(UI_OT_edittranslation_init);
#endif
  WM_operatortype_append(UI_OT_reloadtranslation);
  WM_operatortype_append(UI_OT_button_execute);
  WM_operatortype_append(UI_OT_button_string_clear);

  WM_operatortype_append(UI_OT_list_start_filter);

  WM_operatortype_append(UI_OT_tree_view_drop);
  WM_operatortype_append(UI_OT_tree_view_item_rename);

  /* external */
  WM_operatortype_append(UI_OT_eyedropper_color);
  WM_operatortype_append(UI_OT_eyedropper_colorramp);
  WM_operatortype_append(UI_OT_eyedropper_colorramp_point);
  WM_operatortype_append(UI_OT_eyedropper_id);
  WM_operatortype_append(UI_OT_eyedropper_depth);
  WM_operatortype_append(UI_OT_eyedropper_driver);
  WM_operatortype_append(UI_OT_eyedropper_gpencil_color);
}

void ED_keymap_ui(wmKeyConfig *keyconf)
{
  WM_keymap_ensure(keyconf, "User Interface", 0, 0);

  eyedropper_modal_keymap(keyconf);
  eyedropper_colorband_modal_keymap(keyconf);
}
