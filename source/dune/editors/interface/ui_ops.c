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

#include "imbuf_colormanagement.h"

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

/* Immediate redraw helper
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

static void UI_OT_copy_pycmd_btn(WinOpType *ot)
{
  /* ids */
  ot->name = "Copy Python Command";
  ot->idname = "UI_OT_copy_pycmd_btnn";
  ot->description = "Copy the Python command matching this button";

  /* callbacks */
  ot->ex = copy_py_cmd_btn_ex;
  ot->poll = copy_py_cmd_btn_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}


/* Reset to Default Values Btn Op **/
static int op_btnprop_finish(Cxt *C, ApiPtr *ptr, ApiProp *prop)
{
  Id *id = ptr->owner_id;

  /* perform updates required for this prop */
  apiprop_update(C, ptr, prop);

  /* as if we pressed the btn */
  ui_cxt_active_btnprop_handle(C, false);

  /* Since we don't want to undo _all_ edits to settings, eg window
   * edits on the screen or on op settings.
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

static int reset_default_btnEx(Cxt *C, WinOp *op)
{
  ApiPtr ptr;
  ApiProp *prop;
  int index;
  const bool all = api_bool_get(op->ptr, "all");

  /* try to reset the nominated setting to its default value */
  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  /* if there is a valid prop that is editable... */
  if (ptr.data && prop && apiProp_editable(&ptr, prop)) {
    if (ApiProp_reset(&ptr, prop, (all) ? -1 : index)) {
      return op_btnprop_finish_with_undo(C, &ptr, prop);
    }
  }

  return OP_CANCELLED;
}

static void UI_OT_reset_default_btn(WinOpType *ot)
{
  /* ids */
  ot->name = "Reset to Default Value";
  ot->idname = "UI_OT_reset_default_btn";
  ot->description = "Reset this prop's value to its default value";

  /* cbs */
  ot->poll = reset_default_btnpoll;
  ot->ex = reset_default_btnEx;

  /* flags */
  /* Don't set OPTYPE_UNDO because op_btnprop_finish_with_undo
   * is responsible for the undo push. */
  ot->flag = 0;

  /* props */
  api_def_bool(ot->srna, "all", 1, "All", "Reset to default values all elements of the array");
}

/* Assign Value as Default Button Operator */
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

static int assign_default_btnEx(Cxt *C, WinOp *UNUSED(op))
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

static void UI_OT_assign_default_btn(WinOpType *ot)
{
  /* ids */
  ot->name = "Assign Value as Default";
  ot->idname = "UI_OT_assign_default_btn";
  ot->description = "Set this property's current value as the new default";

  /* cbs */
  ot->poll = assign_default_btn_poll;
  ot->ex = assign_default_btn_ex;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}


/* Unset Property Button Op */
static int unset_btnprop_ex(Cxt *C, WinOp *UNUSED(op))
{
  ApiProp ptr;
  ApiProp *prop;
  int index;

  /* try to unset the nominated prop */
  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  /* if there is a valid property that is editable... */
  if (ptr.data && prop && apiprop_editable(&ptr, prop) &&
      /* apiprop_is_idprop(prop) && */
      apiprop_is_set(&ptr, prop)) {
    apiprop_unset(&ptr, prop);
    return op_btnprop_finish(C, &ptr, prop);
  }

  return OP_CANCELLED;
}

static void UI_OT_unset_btnprop(WinOpType *ot)
{
  /* ids */
  ot->name = "Unset Prop";
  ot->idname = "UI_OT_unset_btnprop";
  ot->description = "Clear the prop and use default or generated value in ops";

  /* cbs */
  ot->poll = ED_op_regionactive;
  ot->ex = unset_btnprop_ex;

  /* flags */
  ot->flag = OPTYPE_UNDO;
}

/* Define Override Type Op */
/* Note that we use different values for UI/UX than 'real' override op, user does not care
 * whether it's added or removed for the differential op e.g. */
enum {
  UIOverride_Type_NOOP = 0,
  UIOverride_Type_Replace = 1,
  UIOverride_Type_Difference = 2, /* Add/subtract */
  UIOverride_Type_Factor = 3,     /* Multiply */
  /* TODO: should/can we expose insert/remove ones for collections? Doubt it... */
};

static EnumPropItem override_type_items[] = {
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

static bool override_type_set_btnpoll(Cxt *C)
{
  ApiPtr ptr;
  ApiPtr *prop;
  int index;

  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  const uint override_status = apiprop_override_lib_status(
      cxt_data_main(C), &ptr, prop, index);

  return (ptr.data && prop && (override_status & API_OVERRIDE_STATUS_OVERRIDABLE));
}

static int override_type_set_btnex(Cxt *C, WinOp *op)
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
  win_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return op_btnprop_finish(C, &ptr, prop);
}

static int override_type_set_btn_invoke(Cxt *C,
                                        WinOp *op,
                                        const WinEvent *UNUSED(event))
{
#if 0 /* Disabled for now */
  return win_menu_invoke_ex(C, op, WIN_OP_INVOKE_DEFAULT);
#else
  api_enum_set(op->ptr, "type", IDOVERRIDE_LIB_OP_REPLACE);
  return override_type_set_btnex(C, op);
#endif
}

static void UI_OT_override_type_set_btn(WinOpType *ot)
{
  /* ids */
  ot->name = "Define Override Type";
  ot->idname = "UI_OT_override_type_set_btn";
  ot->description = "Create an override op, or set the type of an existing one";

  /* cbs */
  ot->poll = override_type_set_btnpoll;
  ot->ex = override_type_set_btnex;
  ot->invoke = override_type_set_btninvoke;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* props */
  api_def_bool(ot->sapi, "all", 1, "All", "Reset to default values all elements of the array");
  ot->prop = api_def_enum(ot->sapi,
                          "type",
                          override_type_items,
                          UIOverride_Type_Replace,
                          "Type",
                          "Type of override op");
  /* TODO: add itemf cb, not all options are available for all data types... */
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

static int override_remove_btnex(Cxt *C, WinOp *op)
{
  Main *dunemain = cxt_data_main(C);
  ApiPtr ptr, id_refptr, src;
  ApiPtr *prop;
  int index;
  const bool all = api_bool_get(op->ptr, "all");

  /* try to reset the nominated setting to its default value */
  ui_cxt_active_btnprop_get(C, &ptr, &prop, &index);

  Id *id = ptr.owner_id;
  IdOverrideLibProp *oprop = apiprop_override_prop_find(dunemain, &ptr, prop, &id);
  lib_assert(oprop != NULL);
  lib_assert(id != NULL && id->override_lib != NULL);

  const bool is_template = ID_IS_OVERRIDE_LIB_TEMPLATE(id);

  /* We need source (i.e. linked data) to restore values of deleted overrides...
   * If this is an override template, we obviously do not need to restore anything. */
  if (!is_template) {
    ApiProp *src_prop;
    apiid_ptr_create(id->override_lib->ref, &id_refptr);
    if (!apipath_resolve_prop(&id_refptr, oprop->api_path, &src, &src_prop)) {
      lib_assert_msg(0, "Failed to create matching source (linked data) API ptr");
    }
  }

  if (!all && index != -1) {
    bool is_strict_find;
    /* Remove override op for given item,
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
    /* Just remove whole generic override op of this prop. */
    dune_lib_override_libprop_delete(id->override_lib, oprop);
    if (!is_template) {
      apiProp_copy(dunemain, &ptr, &src, prop, -1);
    }
  }

  /* Outliner e.g. has to be aware of this change. */
  wm_main_add_notifier(NC_WM | ND_LIB_OVERRIDE_CHANGED, NULL);

  return op_btnprop_finish(C, &ptr, prop);
}

static void UI_OT_override_remove_btn(WinOpType *ot)
{
  /* ids */
  ot->name = "Remove Override";
  ot->idname = "UI_OT_override_remove_btn";
  ot->description = "Remove an override operation";

  /* callbacks */
  ot->poll = override_remove_btnpoll;
  ot->ex = override_remove_btnex;

  /* flags */
  ot->flag = OPTYPE_UNDO;

  /* props */
  api_def_bool(ot->sapi, "all", 1, "All", "Reset to default values all elements of the array");
}

/* Copy To Selected Op */
#define NOT_NULL(assignment) ((assignment) != NULL)
#define NOT_API_NULL(assignment) ((assignment).data != NULL)

static void ui_cxt_selected_bones_via_pose(Cxt *C, List *r_lb)
{
  List list;
  lb = cxt_data_collection_get(C, "selected_pose_bones");

  if (!lib_list_is_empty(&list)) {
    LIST_FOREACH (CollectionPtrLink *, link, &list) {
      PoseChannel *pchan = link->ptr.data;
      apiptr_create(link->ptr.owner_id, &ApiBone, pchan->bone, &link->ptr);
    }
  }

  *r_lb = list;
}

bool ui_cxt_copy_to_selected_list(Cxt *C,
                                  ApiPtr *ptr,
                                  ApiProp *prop,
                                  List *r_list,
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
        *r_list = cxt_data_collection_get(C, "selected_pose_bones");
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
        *r_list = cxt_data_collection_get(C, "selected_editable_bones");
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
    ui_cxt_selected_bones_via_pose(C, r_list);
  }
  else if (api_struct_is_a(ptr->type, &apa_Sequence)) {
    /* Special case when we do this for 'Seq.lock'.
     * (if the seq is locked, it won't be in "selected_editable_seqs"). */
    const char *prop_id = api_prop_id(prop);
    if (STREQ(prop_id, "lock")) {
      *r_list = cxt_data_collection_get(C, "selected_seqs");
    }
    else {
      *r_list = cxt_data_collection_get(C, "selected_editable_seqs");
    }
    /* Account for props only being available for some seq types. */
    ensure_list_items_contain_prop = true;
  }
  else if (api_struct_is_a(ptr->type, &api_FCurve)) {
    *r_list = cxt_data_collection_get(C, "selected_editable_fcurves");
  }
  else if (api_struct_is_a(ptr->type, &api_Keyframe)) {
    *r_list = cxt_data_collection_get(C, "selected_editable_keyframes");
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
    List list = {NULL, NULL};
    char *path = NULL;
    Node *node = NULL;

    /* Get the node we're editing */
    if (api_struct_is_a(ptr->type, &ApiNodeSocket)) {
      NodeTree *ntree = (NodeTree *)ptr->owner_id;
      NodeSocket *sock = ptr->data;
      if (nodeFindNode(ntree, sock, &node, NULL)) {
        if ((path = api_path_resolve_from_type_to_prop(ptr, prop, &ApiNode)) != NULL) {
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
      nodelist = cxt_data_collection_get(C, "selected_nodes");

      LIST_FOREACH_MUTABLE (CollectionPtrLink *, link, &nodelist) {
        Node *node_data = link->ptr.data;

        if (node_data->type != node->type) {
          lib_remlink(&nodelist, link);
          mem_free(link);
        }
      }
    }

    *r_lb = lb;
    *r_path = path;
  }
  else if (ptr->owner_id) {
    Id *id = ptr->owner_id;

    if (GS(id->name) == ID_OB) {
      *r_list = cxt_data_collection_get(C, "selected_editable_objects");
      *r_use_path_from_id = true;
      *r_path = api_path_from_id_to_prop(ptr, prop);
    }
    else if (OB_DATA_SUPPORT_ID(GS(id->name))) {
      /* check we're using the active object */
      const short id_code = GS(id->name);
      List list = cxt_data_collection_get(C, "selected_editable_objects");
      char *path = api_path_from_id_to_prop(ptr, prop);

      /* de-duplicate obdata */
      if (!lib_list_is_empty(&lb)) {
        LIST_FOREACH (CollectionPtrLink *, link, &list) {
          Object *ob = (Object *)link->ptr.owner_id;
          if (ob->data) {
            Id *id_data = ob->data;
            id_data->tag |= LIB_TAG_DOIT;
          }
        }

        LIST_FOREACH_MUTABLE (CollectionPtrLink *, link, &list) {
          Object *ob = (Object *)link->ptr.owner_id;
          Id *id_data = ob->data;

          if ((id_data == NULL) || (id_data->tag & LIB_TAG_DOIT) == 0 || ID_IS_LINKED(id_data) ||
              (GS(id_data->name) != id_code)) {
            lib_remlink(&list, link);
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

      *r_list = list;
      *r_path = path;
    }
    else if (GS(id->name) == ID_SCE) {
      /* Seq's Id is scene :/ */
      /* Try to recursively find an ApiSeq ancestor,
       * to handle situations like T41062... */
      if ((*r_path = api_path_resolve_from_type_to_prop(ptr, prop, &ApiSeq)) != NULL) {
        /* Special case when we do this for 'Seq.lock'.
         * (if the sequence is locked, it won't be in "selected_editable_seqs"). */
        const char *prop_id = api_prop_id(prop);
        if (STREQ(prop_id, "lock")) {
          *r_list = cxt_data_collection_get(C, "selected_seqs");
        }
        else {
          *r_list = cxt_data_collection_get(C, "selected_editable_seqs");
        }
        /* Account for props only being available for some seq types. */
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
  ApiPtr linkptr;

  if (ptr_link->data == ptr->data) {
    return false;
  }

  if (use_path_from_id) {
    /* Path relative to Id. */
    lprop = NULL;
    api_id_ptr_create(ptr_link->owner_id, &idptr);
    api_path_resolve_prop(&idptr, path, &linkptr, &lprop);
  }
  else if (path) {
    /* Path relative to elements from list. */
    lprop = NULL;
    api_path_resolve_prop(ptr_link, path, &linkptr, &lprop);
  }
  else {
    linkptr = *ptr_link;
    lprop = prop;
  }

  if (linkptr.data == ptr->data) {
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
   * - Never for Id props on specific Id (even if they are equally named).
   * - Never for NodesModSettings props (even if they are equally named).
   *
   * Be permissive on Id props in the following cases:
   * - NodesModSettings props
   *   - (special check: only if the node-group matches, since the 'Input_n' props are name
   *      based and similar on potentially very different node-groups).
   * - Id props on specific Id
   *   - (no special check, copying seems OK [even if type does not match -- does not do anything
   *      then]) */
  bool ignore_prop_eq = api_prop_is_idprop(lprop) && api_prop_is_idprop(prop);
  if (api_struct_is_a(lptr.type, &ApiNodesMod) &&
      api_struct_is_a(ptr->type, &ApiNodesMod)) {
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

  if (!api_prop_editable(&linkptr, lprop)) {
    return false;
  }

  if (r_ptr) {
    *r_ptr = linkptr;
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
  ApiProp *linkprop;
  ApiPtr linkptr;

  if (ptr_link->data == ptr->data) {
    return false;
  }

  if (use_path_from_id) {
    /* Path relative to Id */
    linkprop = NULL;
    api_id_ptr_create(ptr_link->owner_id, &idptr);
    api_path_resolve_prop(&idptr, path, &linkptr, &linkprop);
  }
  else if (path) {
    /* Path relative to elements from list. */
    lprop = NULL;
    api_path_resolve_prop(ptr_link, path, &lptr, &lprop);
  }
  else {
    linkptr = *ptr_link;
    linkprop = prop;
  }

  if (linkptr.data == ptr->data) {
    /* temp_ptr might not be the same as ptr_link! */
    return false;
  }

  /* Skip non-existing props on link. This was previously covered with the `linkprop != prop`
   * check but we are now more permissive when it comes to Id props, see below. */
  if (lprop == NULL) {
    return false;
  }

  if (api_prop_type(linkprop) != api_prop_type(prop)) {
    return false;
  }

  /* Check prop ptrs matching.
   * For Id props, these ptrs match:
   * - If the prop is API defined on an existing class (and they are equally named).
   * - Never for Id props on specific Id (even if they are equally named).
   * - Never for NodesModSettings props (even if they are equally named).
   *
   * Be permissive on Id props in the following cases:
   * - NodesModSettings props
   *   - (special check: only if the node-group matches, since the 'Input_n' properties are name
   *      based and similar on potentially very different node-groups).
   * - Id props on specific Id
   *   - (no special check, copying seems OK [even if type does not match -- does not do anything
   *      then])
   */
  bool ignore_prop_eq = api_prop_is_idprop(linkprop) && api_prop_is_idprop(prop);
  if (api_struct_is_a(lptr.type, &ApiNodesMod) &&
      api_struct_is_a(ptr->type, &ApiNodesMod)) {
    ignore_prop_eq = false;

    NodesModData *nmd_link = (NodesModData *)linkptr.data;
    NodesModData *nmd_src = (NodesModData *)ptr->data;
    if (nmd_link->node_group == nmd_src->node_group) {
      ignore_prop_eq = true;
    }
  }

  if ((linkprop != prop) && !ignore_prop_eq) {
    return false;
  }

  if (!api_prop_editable(&lptr, linkprop)) {
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

/* Called from both ex & poll.
 * Normally we wouldn't call a loop from within a poll fn,
 * however this is a special case, and for regular poll calls, getting
 * the cxt from the btn will fail early. */
static bool copy_to_selected_btn(Cxt *C, bool all, bool poll)
{
  Main *main = cxt_data_main(C);
  ApiPtr ptr, lptr;
  ApiProp *prop, *lprop;
  bool success = false;
  int index;

  /* try to reset the nominated setting to its default value */
  ui_cxt_active_btn_prop_get(C, &ptr, &prop, &index);

  /* if there is a valid prop that is editable... */
  if (ptr.data == NULL || prop == NULL) {
    return false;
  }

  char *path = NULL;
  bool use_path_from_id;
  List list = {NULL};

  if (!ui_cxt_copy_to_selected_list(C, &ptr, prop, &list, &use_path_from_id, &path)) {
    return false;
  }
  if (lib_list_is_empty(&lb)) {
    MEM_SAFE_FREE(path);
    return false;
  }

  LIST_FOREACH (CollectionPtrLink *, link, &list) {
    if (link->ptr.data == ptr.data) {
      continue;
    }

    if (!ui_cxt_copy_to_selected_check(
            &ptr, &link->ptr, prop, path, use_path_from_id, &linkptr, &linkprop)) {
      continue;
    }

    if (poll) {
      success = true;
      break;
    }
    if (api_prop_copy(main, &linkptr, &ptr, prop, (all) ? -1 : index)) {
      api_prop_update(C, &linkptr, prop);
      success = true;
    }
  }

  MEM_SAFE_FREE(path);
  lib_freelist(&list);

  return success;
}

static bool copy_to_selected_btn_poll(Cxt *C)
{
  return copy_to_selected_btn(C, false, true);
}

static int copy_to_selected_btn_ex(Cxt *C, WinOp *op)
{
  bool success;

  const bool all = api_bool_get(op->ptr, "all");

  success = copy_to_selected_btn(C, all, false);

  return (success) ? OP_FINISHED : OP_CANCELLED;
}

static void UI_OT_copy_to_selected_btn(WinOpType *ot)
{
  /* ids */
  ot->name = "Copy to Selected";
  ot->idname = "UI_OT_copy_to_selected_btn";
  ot->description = "Copy prop from this object to selected objects or bones";

  /* cbs */
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

  /* Verify ptr type. */
  char bone_name[MAXBONENAME];
  const ApiStruct *target_type = NULL;

  if (ELEM(ptr.type, &ApiEditBone, &ApiPoseBone, &ApiBone)) {
    api_string_get(&ptr, "name", bone_name);
    if (bone_name[0] != '\0') {
      target_type = &ApiBone;
    }
  }
  else if (api_struct_is_a(ptr.type, &ApiObject)) {
    target_type = &ApiObject;
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
  if ((base == NULL) || ((target_type == &ApiBone) && (base->object->type != OB_ARMATURE))) {
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

    /* For pointer props, use their value directly. */
    if (type == PROP_PTR) {
      target_ptr = api_prop_ptr_get(&ptr, prop);

      return jump_to_target_ptr(C, target_ptr, poll);
    }
    /* For string properties with prop_search, look up the search collection item. */
    if (type == PROP_STRING) {
      const Btn *btn = ui_cxt_active_btn_get(C);
      const BtnSearch *search_btn = (btn->type == BTYPE_SEARCH_MENU) ? (BtnSearch *)btn :
                                                                            NULL;

      if (search_btn && search_btn->items_update_fn == ui_api_collection_search_update_fn) {
        uiApiCollectionSearch *coll_search = search_btn->arg;

        char str_buf[MAXBONENAME];
        char *str_ptr = api_prop_string_get_alloc(&ptr, prop, str_buf, sizeof(str_buf), NULL);

        int found = api_prop_collection_lookup_string(
            &coll_search->search_ptr, coll_search->search_prop, str_ptr, &target_ptr);

        if (str_ptr != str_buf) {
          mem_freen(str_ptr);
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

static int jump_to_target_btn_ex(Cxt *C, WinOp *UNUSED(op))
{
  const bool success = jump_to_target_btn(C, false);

  return (success) ? OP_FINISHED : OP_CANCELLED;
}

static void UI_OT_jump_to_target_btn(WinOpType *ot)
{
  /* ids */
  ot->name = "Jump to Target";
  ot->idname = "UI_OT_jump_to_target_btn";
  ot->description = "Switch to the target object or bone";

  /* cbss */
  ot->poll = ui_jump_to_target_btn_poll;
  ot->ex = jump_to_target_btn_ex;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Edit Python Source Operator */
#ifdef WITH_PYTHON

/* EditSource Utility fns and op,
 * NOTE: this includes util fns and btn matching checks. */

typedef struct uiEditSourceStore {
  uiBtn btn_orig;
  GHash *hash;
} uiEditSourceStore;

typedef struct uiEditSourceBtnStore {
  char py_dbg_fn[FILE_MAX];
  int py_dbg_line_number;
} uiEditSourceBtnStore;

/* should only ever be set while the edit source op is running */
static struct uiEditSourceStore *ui_editsource_info = NULL;

bool ui_editsource_enable_check(void)
{
  return (ui_editsource_info != NULL);
}

static void ui_editsource_active_btn_set(Btn *btn)
{
  lib_assert(ui_editsource_info == NULL);

  ui_editsource_info = mem_callocn(sizeof(uiEditSourceStore), __func__);
  memcpy(&ui_editsource_info->btn_orig, btn, sizeof(Btn));

  ui_editsource_info->hash = lib_ghash_ptr_new(__func__);
}

static void ui_editsource_active_btn_clear(void)
{
  lib_ghash_free(ui_editsource_info->hash, NULL, mem_freen);
  mem_freen(ui_editsource_info);
  ui_editsource_info = NULL;
}

static bool ui_editsource_btn_match(Btn *btn_a, Btn *btn_b)
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

void ui_editsource_active_btn_test(Btn *btn)
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

void ui_editsource_btn_replace(const Btn *old_btn, Btn *new_btn)
{
  uiEditSourceBtnStore *btn_store = lib_ghash_lookup(ui_editsource_info->hash, old_btn);
  if (btn_store) {
    lib_ghash_remove(ui_editsource_info->hash, old_btn, NULL, NULL);
    lib_ghash_insert(ui_editsource_info->hash, new_btn, btn_store);
  }
}

static int editsource_text_edit(Cxt *C,
                                WinOp *op,
                                const char filepath[FILE_MAX],
                                const int line)
{
  struct Main *main = cxt_data_main(C);
  Text *text = NULL;

  /* Developers may wish to copy-paste to an external editor. */
  printf("%s:%d\n", filepath, line);

  LIST_FOREACH (Text *, text_iter, &main->texts) {
    if (text_iter->filepath && lib_path_cmp(text_iter->filepath, filepath) == 0) {
      text = text_iter;
      break;
    }
  }

  if (text == NULL) {
    text = dune_text_load(main, filepath, dine_main_file_path(main));
  }

  if (text == NULL) {
    dune_reportf(op->reports, RPT_WARNING, "File '%s' cannot be opened", filepath);
    return OP_CANCELLED;
  }

  txt_move_toline(text, line - 1, false);

  /* naughty!, find text area to set, not good behavior
   * but since this is a developer tool lets allow it - campbell */
  if (!ed_text_activate_in_screen(C, text)) {
    dune_reportf(op->reports, RPT_INFO, "See '%s' in the text editor", text->id.name + 2);
  }

  wm_event_add_notifier(C, NC_TEXT | ND_CURSOR, text);

  return OP_FINISHED;
}

static int editsource_ex(Cxt *C, WinOp *op)
{
  Btn *btn = ui_cxt_active_btn_get(C);

  if (btn) {
    GHashIter ghi;
    struct uiEditSourceBtnStore *btn_store = NULL;

    ARegion *region = cxt_win_region(C);
    int ret;

    /* needed else the active btn does not get tested */
    ui_screen_free_active_btn_highlight(C, cxt_win_screen(C));

    // printf("%s: begin\n", __func__);

    /* take care not to return before calling ui_editsource_active_btn_clear */
    ui_editsource_active_btn_set(btn);

    /* redraw and get active btn python info */
    ui_region_redraw_immediately(C, region);

    for (lib_ghashIter_init(&ghi, ui_editsource_info->hash);
         lib_ghashIter_done(&ghi) == false;
         lib_ghashIter_step(&ghi)) {
      Btn *btn_key = lib_ghashIter_getKey(&ghi);
      if (btn_key && ui_editsource_btn_match(&ui_editsource_info->btn_orig, btn_key)) {
        btn_store = lib_ghashIter_getValue(&ghi);
        break;
      }
    }

    if (btn_store) {
      if (btn_store->py_dbg_line_number != -1) {
        ret = editsource_text_edit(C, op, btn_store->py_dbg_fn, btn_store->py_dbg_line_number);
      }
      else {
        dune_report(
            op->reports, RPT_ERROR, "Active btn is not from a script, cannot edit source");
        ret = OP_CANCELLED;
      }
    }
    else {
      dune_report(op->reports, RPT_ERROR, "Active btn match cannot be found");
      ret = OP_CANCELLED;
    }

    ui_editsource_active_btn_clear();

    // printf("%s: end\n", __func__);

    return ret;
  }

  dune_report(op->reports, RPT_ERROR, "Active btn not found");
  return OP_CANCELLED;
}

static void UI_OT_editsource(WinOpType *ot)
{
  /* ids */
  ot->name = "Edit Source";
  ot->idname = "UI_OT_editsource";
  ot->description = "Edit UI source code of the active btn";

  /* cbs */
  ot->ex = editsource_ex;
}

/* Edit Translation Op */
/* EditTranslation util fns and operator,
 * this includes util fns and btn matching checks.
 * this only works in conjunction with a Python op! */
static void edittranslation_find_po_file(const char *root,
                                         const char *uilng,
                                         char *path,
                                         const size_t maxlen)
{
  char tstr[32]; /* Should be more than enough! */

  /* First, full lang code. */
  lib_snprintf(tstr, sizeof(tstr), "%s.po", uilng);
  lib_join_dirfile(path, maxlen, root, uilng);
  lib_path_append(path, maxlen, tstr);
  if (lib_is_file(path)) {
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
        lib_strncpy(tstr, uilng, szt + 1); /* +1 for '\0' char! */
      }
    }
    if (tstr[0]) {
      /* Because of some codes like sr_SR@latin... */
      tc = strchr(uilng, '@');
      if (tc) {
        lib_strncpy(tstr + szt, tc, sizeof(tstr) - szt);
      }

      lib_join_dirfile(path, maxlen, root, tstr);
      strcat(tstr, ".po");
      lib_path_append(path, maxlen, tstr);
      if (lib_is_file(path)) {
        return;
      }
    }
  }

  /* Else no po file! */
  path[0] = '\0';
}

static int edittranslation_ex(Cxt *C, WinOp *op)
{
  uiBtn *btn = ui_cxt_active_btn_get(C);
  if (btn == NULL) {
    dune_report(op->reports, RPT_ERROR, "Active btn not found");
    return OP_CANCELLED;
  }

  WinOpType *ot;
  ApiPtr ptr;
  char popath[FILE_MAX];
  const char *root = U.i18ndir;
  const char *uilng = lang_get();

  uiStringInfo btn_label = {BTN_GET_LABEL, NULL};
  uiStringInfo api_label = {BTN_GET_API_LABEL, NULL};
  uiStringInfo enum_label = {BTN_GET_APIENUM_LABEL, NULL};
  uiStringInfo btn_tip = {BTN_GET_TIP, NULL};
  uiStringInfo api_tip = {BTN_GET_API_TIP, NULL};
  uiStringInfo enum_tip = {BTN_GET_APIENUM_TIP, NULL};
  uiStringInfo api_struct = {BTN_GET_APISTRUCT_ID, NULL};
  uiStringInfo api_prop = {BTN_GET_APIPROP_ID, NULL};
  uiStringInfo api_enum = {BTN_GET_APIENUM_ID, NULL};
  uiStringInfo api_cxt = {BTN_GET_API_LABEL_CXT, NULL};

  if (!lib_is_dir(root)) {
    dune_report(op->reports,
               RPT_ERROR,
               "Please set your Prefs' 'Translation Branches "
               "Directory' path to a valid directory");
    return OP_CANCELLED;
  }
  ot = win_optype_find(EDTSRC_LANG_OP_NAME, 0);
  if (ot == NULL) {
    dune_reportf(op->reports,
                RPT_ERROR,
                "Could not find op '%s'! Please enable ui_lang add-on "
                "in the User Prefs",
                EDTSRC_LANG_OP_NAME);
    return OP_CANCELLED;
  }
  /* Try to find a valid po file for current language... */
  edittranslation_find_po_file(root, uilng, popath, FILE_MAX);
  // printf("po path: %s\n", popath);
  if (popath[0] == '\0') {
    dune_reportf(
        op->reports, RPT_ERROR, "No valid po found for language '%s' under %s", uilng, root);
    return OP_CANCELLED;
  }

  btn_string_info_get(C,
                      btn,
                      &btn_label,
                      &api_label,
                      &enum_label,
                      &btn_tip,
                      &api_tip,
                      &enum_tip,
                      &api_struct,
                      &api_prop,
                      &api_enum,
                      &api_cxt,
                      NULL);

  win_op_props_create_ptr(&ptr, ot);
  api_string_set(&ptr, "lang", uilng);
  api_string_set(&ptr, "po_file", popath);
  api_string_set(&ptr, "btn_label", btn_label.strinfo);
  api_string_set(&ptr, "api_label", api_label.strinfo);
  api_string_set(&ptr, "enum_label", enum_label.strinfo);
  api_string_set(&ptr, "but_tip", btn_tip.strinfo);
  api_string_set(&ptr, "api_tip", api_tip.strinfo);
  api_string_set(&ptr, "enum_tip", enum_tip.strinfo);
  api_string_set(&ptr, "api_struct", api_struct.strinfo);
  api_string_set(&ptr, "api_prop", api_prop.strinfo);
  api_string_set(&ptr, "api_enum", api_enum.strinfo);
  api_string_set(&ptr, "api_cxt", api_cxt.strinfo);
  const int ret = win_op_name_call_ptr(C, ot, WIN_OP_INVOKE_DEFAULT, &ptr, NULL);

  /* Clean up */
  if (btn_label.strinfo) {
    mem_freen(btn_label.strinfo);
  }
  if (api_label.strinfo) {
    mem_freen(api_label.strinfo);
  }
  if (enum_label.strinfo) {
    mem_freen(enum_label.strinfo);
  }
  if (btn_tip.strinfo) {
    mem_freen(btn_tip.strinfo);
  }
  if (api_tip.strinfo) {
    mem_freen(api_tip.strinfo);
  }
  if (enum_tip.strinfo) {
    mem_freen(enum_tip.strinfo);
  }
  if (api_struct.strinfo) {
    mem_freen(api_struct.strinfo);
  }
  if (api_prop.strinfo) {
    mem_freen(api_prop.strinfo);
  }
  if (api_enum.strinfo) {
    mem_freen(api_enum.strinfo);
  }
  if (api_cxt.strinfo) {
    mem_freen(api_cxt.strinfo);
  }

  return ret;
}

static void UI_OT_edittranslation_init(WinOpType *ot)
{
  /* ids */
  ot->name = "Edit Translation";
  ot->idname = "UI_OT_edittranslation_init";
  ot->description = "Edit current lang for the active btn";

  /* cbs */
  ot->ex = edittranslation_ex;
}

#endif /* WITH_PYTHON */

/* Reload Translation Op */
static int reloadtranslation_ex(Cxt *UNUSED(C), WinOp *UNUSED(op))
{
  lang_init();
  font_cache_clear();
  lang_set(NULL);
  ui_reinit_font();
  return OP_FINISHED;
}

static void UI_OT_reloadtranslation(WinOpType *ot)
{
  /* ids */
  ot->name = "Reload Translation";
  ot->idname = "UI_OT_reloadtranslation";
  ot->description = "Force a full reload of UI translation";

  /* cbs */
  ot->ex = reloadtranslation_ex;
}

/* Press Btn Op */
static int ui_btn_press_invoke(Cxt *C, WinOp *op, const WinEvent *event)
{
  Screen *screen = cxt_win_screen(C);
  const bool skip_depressed = api_bool_get(op->ptr, "skip_depressed");
  ARegion *region_prev = cxt_win_region(C);
  ARegion *region = screen ? dune_screen_find_region_xy(screen, RGN_TYPE_ANY, event->xy) : NULL;

  if (region == NULL) {
    region = region_prev;
  }

  if (region == NULL) {
    return OP_PASS_THROUGH;
  }

  cxt_win_region_set(C, region);
  Btn *btn = ui_cxt_active_btn_get(C);
  cxt_win_region_set(C, region_prev);

  if (btn == NULL) {
    return OP_PASS_THROUGH;
  }
  if (skip_depressed && (btn->flag & (UI_SELECT | UI_SELECT_DRAW))) {
    return OP_PASS_THROUGH;
  }

  /* Weak, this is a workaround for 'ui_btn_is_tool', which checks the op type,
   * having this avoids a minor drawing glitch. */
  void *btn_optype = btn->optype;

  ui_btn_ex(C, region, btn);

  btn->optype = btn_optype;

  win_event_add_mousemove(cxt_win_window(C));

  return OP_FINISHED;
}

static void UI_OT_btn_ex(WinOpType *ot)
{
  ot->name = "Press Btn";
  ot->idname = "UI_OT_btn_ex";
  ot->description = "Presses active btn";

  ot->invoke = ui_btn_press_invoke;
  ot->flag = OPTYPE_INTERNAL;

  api_def_bool(ot->sapi, "skip_depressed", 0, "Skip Depressed", "");
}

/* Text Btn Clear Op */
static int btn_string_clear_ex(Cxt *C, WinOp *UNUSED(op))
{
  uiBtn *btn = ui_cxt_active_btn_get_respect_menu(C);

  if (btn) {
    ui_btn_active_string_clear_and_exit(C, btn);
  }

  return OP_FINISHED;
}

static void UI_OT_btn_string_clear(WinOpType *ot)
{
  ot->name = "Clear Btn String";
  ot->idname = "UI_OT_btn_string_clear";
  ot->description = "Unsets the text of the active button";

  ot->poll = ed_op_regionactive;
  ot->ex = btn_string_clear_ex;
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;
}

/* Drop Color Op **/
bool ui_drop_color_poll(struct Cxt *C, WinDrag *drag, const WinEvent *UNUSED(event))
{
  /* should only return true for regions that include buttons, for now
   * return true always */
  if (drag->type == WM_DRAG_COLOR) {
    SpaceImage *sima = cxt_win_space_image(C);
    ARegion *region = cxt_win_region(C);

    if (ui_btn_active_drop_color(C)) {
      return 1;
    }

    if (sima && (sima->mode == SI_MODE_PAINT) && sima->image &&
        (region && region->regiontype == RGN_TYPE_WINDOW)) {
      return 1;
    }
  }

  return 0;
}

void UI_drop_color_copy(WinDrag *drag, WinDropBox *drop)
{
  uiDragColorHandle *drag_info = drag->poin;

  api_float_set_array(drop->ptr, "color", drag_info->color);
  api_bool_set(drop->ptr, "gamma", drag_info->gamma_corrected);
}

static int drop_color_invoke(Cxt *C, WinOp *op, const WinEvent *event)
{
  ARegion *region = cxt_win_region(C);
  Btn *btn = NULL;
  float color[4];
  bool gamma;

  api_float_get_array(op->ptr, "color", color);
  gamma = api_bool_get(op->ptr, "gamma");

  /* find btn under mouse, check if it has api color prop and
   * if it does copy the data */
  btn = ui_region_find_active_btn(region);

  if (btn && btn->type == UI_BTYPE_COLOR && btn->apiprop) {
    const int color_len = api_prop_array_length(&btn->apiptr, but->apiprop);
    lib_assert(color_len <= 4);

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
    else if (api_prop_subtype(btn->apiprop) == PROP_COLOR) {
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

    ed_imapaint_bucket_fill(C, color, op, event->mval);
  }

  ed_region_tag_redraw(region);

  return OP_FINISHED;
}

static void UI_OT_drop_color(wmOpType *ot)
{
  ot->name = "Drop Color";
  ot->idname = "UI_OT_drop_color";
  ot->description = "Drop colors to buttons";

  ot->invoke = drop_color_invoke;
  ot->flag = OPTYPE_INTERNAL;

  api_def_float_color(ot->sapi, "color", 3, NULL, 0.0, FLT_MAX, "Color", "Source color", 0.0, 1.0);
  api_def_bool(ot->sapi, "gamma", 0, "Gamma Corrected", "The source color is gamma corrected");
}

/* Drop Name Op */
static int drop_name_invoke(Cxt *C, wmOp *op, const wmEvent *UNUSED(event))
{
  uiBtn *btn = ui_btn_active_drop_name_btn(C);
  char *str = api_string_get_alloc(op->ptr, "string", NULL, 0, NULL);

  if (str) {
    ui_btn_set_string_interactive(C, btn, str);
    mem_freen(str);
  }

  return OP_FINISHED;
}

static void UI_OT_drop_name(wmOpType *ot)
{
  ot->name = "Drop Name";
  ot->idname = "UI_OT_drop_name";
  ot->description = "Drop name to button";

  ot->poll = ed_op_regionactive;
  ot->invoke = drop_name_invoke;
  ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;

  api_def_string(
      ot->sapi, "string", NULL, 0, "String", "The string value to drop into the button");
}

/* UI List Search Operator **/
static bool ui_list_focused_poll(Cxt *C)
{
  const ARegion *region = cxt_wm_region(C);
  const Window *win = cxt_wm_window(C);
  const uiList *list = ui_list_find_mouse_over(region, win->eventstate);

  return list != NULL;
}

/* Ensure the filter options are set to be visible in the UI list.
 * return if the visibility changed, requiring a redraw. */
static bool ui_list_unhide_filter_options(uiList *list)
{
  if (list->filter_flag & UILST_FLT_SHOW) {
    /* Nothing to be done. */
    return false;
  }

  list->filter_flag |= UILST_FLT_SHOW;
  return true;
}

static int ui_list_start_filter_invoke(Cxt *C, wmOp *UNUSED(op), const wmEvent *event)
{
  ARegion *region = cxt_wm_region(C);
  uiList *list = ui_list_find_mouse_over(region, event);
  /* Poll should check. */
  lib_assert(list != NULL);

  if (ui_list_unhide_filter_options(list)) {
    ui_region_redraw_immediately(C, region);
  }

  if (!ui_textbtn_activate_api(C, region, list, "filter_name")) {
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

static void UI_OT_list_start_filter(wmOpType *ot)
{
  ot->name = "List Filter";
  ot->idname = "UI_OT_list_start_filter";
  ot->description = "Start entering filter text for the list in focus";

  ot->invoke = ui_list_start_filter_invoke;
  ot->poll = ui_list_focused_poll;
}

/** UI Tree-View Drop Operator **/
static bool ui_tree_view_drop_poll(Cxt *C)
{
  const Window *win = cxt_wm_window(C);
  const ARegion *region = cxt_wm_region(C);
  const uiTreeViewItemHandle *hovered_tree_item = ui_block_tree_view_find_item_at(
      region, win->eventstate->xy);

  return hovered_tree_item != NULL;
}

static int ui_tree_view_drop_invoke(Cxt *C, wmOp *UNUSED(op), const wmEvent *event)
{
  if (event->custom != EVT_DATA_DRAGDROP) {
    return OP_CANCELLED | OP_PASS_THROUGH;
  }

  const ARegion *region = cxt_wm_region(C);
  uiTreeViewItemHandle *hovered_tree_item = ui_block_tree_view_find_item_at(region, event->xy);

  if (!ui_tree_view_item_drop_handle(C, hovered_tree_item, event->customdata)) {
    return OP_CANCELLED | OP_PASS_THROUGH;
  }

  return OP_FINISHED;
}

static void UI_OT_tree_view_drop(wmOpType *ot)
{
  ot->name = "Tree View drop";
  ot->idname = "UI_OT_tree_view_drop";
  ot->description = "Drag and drop items onto a tree item";

  ot->invoke = ui_tree_view_drop_invoke;
  ot->poll = ui_tree_view_drop_poll;

  ot->flag = OPTYPE_INTERNAL;
}

/* UI Tree-View Item Rename Operator
 *
 * General purpose renaming op for tree-views. Thanks to this, to add a rename btn to
 * cxt menus for example, tree-view API users don't have to implement their own renaming
 * ops with the same logic as they already have for their ui::AbstractTreeViewItem::rename()
 * override. */

static bool ui_tree_view_item_rename_poll(Cxt *C)
{
  const ARegion *region = cxt_wm_region(C);
  const uiTreeViewItemHandle *active_item = ui_block_tree_view_find_active_item(region);
  return active_item != NULL && ui_tree_view_item_can_rename(active_item);
}

static int ui_tree_view_item_rename_ex(Cxt *C, wmOp *UNUSED(op))
{
  ARegion *region = cxt_wm_region(C);
  uiTreeViewItemHandle *active_item = UI_block_tree_view_find_active_item(region);

  ui_tree_view_item_begin_rename(active_item);
  ed_region_tag_redraw(region);

  return OP_FINISHED;
}

static void UI_OT_tree_view_item_rename(wmOpType *ot)
{
  ot->name = "Rename Tree-View Item";
  ot->idname = "UI_OT_tree_view_item_rename";
  ot->description = "Rename the active item in the tree";

  ot->ex = ui_tree_view_item_rename_ex;
  ot->poll = ui_tree_view_item_rename_poll;
  /* Could get a custom tooltip via the `get_description()` cb and another overridable
   * fn of the tree-view. */

  ot->flag = OPTYPE_INTERNAL;
}

/** Material Drag/Drop Operator **/
static bool ui_drop_material_poll(Cxt *C)
{
  ApiPtr ptr = cxt_data_ptr_get_type(C, "object", &Api_Object);
  Object *ob = ptr.data;
  if (ob == NULL) {
    return false;
  }

  ApiPtr mat_slot = cxt_data_ptr_get_type(C, "material_slot", &api_MaterialSlot);
  if (api_ptr_is_null(&mat_slot)) {
    return false;
  }

  return true;
}

static int ui_drop_material_ex(Cxt *C, wmOp *op)
{
  Main *duneMain = cxt_data_main(C);

  if (!api_struct_prop_is_set(op->ptr, "session_uuid")) {
    return OP_CANCELLED;
  }
  const uint32_t session_uuid = (uint32_t)api_int_get(op->ptr, "session_uuid");
  Material *ma = (Material *)dune_libblock_find_session_uuid(duneMain, ID_MA, session_uuid);
  if (ma == NULL) {
    return OP_CANCELLED;
  }

  ApiPtr ptr = cxt_data_ptr_get_type(C, "object", &api_Object);
  Object *ob = ptr.data;
  lib_assert(ob);

  ApiPtr mat_slot = cxt_data_ptr_get_type(C, "material_slot", &api_MaterialSlot);
  lib_assert(mat_slot.data);
  const int target_slot = api_int_get(&mat_slot, "slot_index") + 1;

  /* only drop pen material on pen objects */
  if ((ma->pen_style != NULL) && (ob->type != OB_PEN)) {
    return OP_CANCELLED;
  }

  dune_object_material_assign(duneMain, ob, ma, target_slot, DUNE_MAT_ASSIGN_USERPREF);

  wm_event_add_notifier(C, NC_OBJECT | ND_OB_SHADING, ob);
  wm_event_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, NULL);
  wm_event_add_notifier(C, NC_MATERIAL | ND_SHADING_LINKS, ma);
  graph_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);

  return OP_FINISHED;
}

static void UI_OT_drop_material(wmOpType *ot)
{
  ot->name = "Drop Material in Material slots";
  ot->description = "Drag material to Material slots in Properties";
  ot->idname = "UI_OT_drop_material";

  ot->poll = ui_drop_materialpoll;
  ot->ex = ui_drop_materialex;
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_INTERNAL;

  ApiProp *prop = api_def_int(ot->sapi,
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

/* Op & Keymap Registration */
void ed_optypes_ui(void)
{
  WM_optype_append(UI_OT_copy_data_path_btn);
  WM_optype_append(UI_OT_copy_as_driver_btn);
  WM_optype_append(UI_OT_copy_python_command_btn);
  WM_optype_append(UI_OT_reset_default_btn);
  WM_optype_append(UI_OT_assign_default_btn);
  WM_optype_append(UI_OT_unset_prop_btn);
  wm_optype_append(UI_OT_override_type_set_btn);
  wm_optype_append(UI_OT_override_remove_btn);
  wm_optype_append(UI_OT_copy_to_selected_btn);
  _optype_append(UI_OT_jump_to_target_btn);
  WM_optype_append(UI_OT_drop_color);
  WM_optype_append(UI_OT_drop_name);
  WM_optype_append(UI_OT_drop_material);
#ifdef WITH_PYTHON
  wm_optype_append(UI_OT_editsource);
  wm_optype_append(UI_OT_edittranslation_init);
#endif
  wm_optype_append(UI_OT_reloadtranslation);
  wm_optype_append(UI_OT_btn_execute);
  wm_optype_append(UI_OT_btn_string_clear);

  wm_optype_append(UI_OT_list_start_filter);

  wm_optype_append(UI_OT_tree_view_drop);
  wm_optype_append(UI_OT_tree_view_item_rename);

  /* external */
  wm_optype_append(UI_OT_eyedropper_color);
  wm_optype_append(UI_OT_eyedropper_colorramp);
  wm_optype_append(UI_OT_eyedropper_colorramp_point);
  wm_optype_append(UI_OT_eyedropper_id);
  wm_optype_append(UI_OT_eyedropper_depth);
  wm_optype_append(UI_OT_eyedropper_driver);
  wm_optype_append(UI_OT_eyedropper_pen_color);
}

void ed_keymap_ui(wmKeyConfig *keyconf)
{
  wm_keymap_ensure(keyconf, "User Interface", 0, 0);

  eyedropper_modal_keymap(keyconf);
  eyedropper_colorband_modal_keymap(keyconf);
}
