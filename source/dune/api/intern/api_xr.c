#include "lib_math.h"

#include "types_space.h"
#include "types_view3d.h"
#include "types_wm.h"
#include "types_xr.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "wm_types.h"

#include "api_internal.h"

#ifdef API_RUNTIME

#  include "lib_list.h"

#  include "wm_api.h"

/* -------------------------------------------------------------------- */

#  ifdef WITH_XR_OPENXR
static wmXrData *api_XrSession_wm_xr_data_get(ApiPtr *ptr)
{
  /* Callers could also get XrSessionState pointer through ptr->data, but prefer if we just
   * consistently pass wmXrData pointers to the wm_xr_xxx() API. */

  lib_assert(ELEM(ptr->type, &api_XrSessionSettings, &api_XrSessionState));

  wmWindowManager *wm = (wmWindowManager *)ptr->owner_id;
  lib_assert(wm && (GS(wm->id.name) == ID_WM));

  return &wm->xr;
}
#  endif

/* -------------------------------------------------------------------- */
/** XR Action Map **/

static XrComponentPath *api_XrComponentPath_new(XrActionMapBinding *amb, const char *path_str)
{
#  ifdef WITH_XR_OPENXR
  XrComponentPath *component_path = mem_callocn(sizeof(XrComponentPath), __func__);
  STRNCPY(component_path->path, path_str);
  lib_addtail(&amb->component_paths, component_path);
  return component_path;
#  else
  UNUSED_VARS(amb, path_str);
  return NULL;
#  endif
}

static void api_XrComponentPath_remove(XrActionMapBinding *amb, ApiPtr *component_path_ptr)
{
#  ifdef WITH_XR_OPENXR
  XrComponentPath *component_path = component_path_ptr->data;
  int idx = lib_findindex(&amb->component_paths, component_path);
  if (idx != -1) {
    lib_freelinkn(&amb->component_paths, component_path);
  }
  API_PTR_INVALIDATE(component_path_ptr);
#  else
  UNUSED_VARS(amb, component_path_ptr);
#  endif
}

static XrComponentPath *api_XrComponentPath_find(XrActionMapBinding *amb, const char *path_str)
{
#  ifdef WITH_XR_OPENXR
  return lib_findstring(&amb->component_paths, path_str, offsetof(XrComponentPath, path));
#  else
  UNUSED_VARS(amb, path_str);
  return NULL;
#  endif
}

static XrActionMapBinding *api_XrActionMapBinding_new(XrActionMapItem *ami,
                                                      const char *name,
                                                      bool replace_existing)
{
#  ifdef WITH_XR_OPENXR
  return wm_xr_actionmap_binding_new(ami, name, replace_existing);
#  else
  UNUSED_VARS(ami, name, replace_existing);
  return NULL;
#  endif
}

static XrActionMapBinding *api_XrActionMapBinding_new_from_binding(XrActionMapItem *ami,
                                                                   XrActionMapBinding *amb_src)
{
#  ifdef WITH_XR_OPENXR
  return wm_xr_actionmap_binding_add_copy(ami, amb_src);
#  else
  UNUSED_VARS(ami, amb_src);
  return NULL;
#  endif
}

static void api_XrActionMapBinding_remove(XrActionMapItem *ami,
                                          ReportList *reports,
                                          ApiPtr *amb_ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapBinding *amb = amb_ptr->data;
  if (wm_xr_actionmap_binding_remove(ami, amb) == false) {
    dunr_reportf(reports,
                RPT_ERROR,
                "ActionMapBinding '%s' cannot be removed from '%s'",
                amb->name,
                ami->name);
    return;
  }
  API_PTR_INVALIDATE(amb_ptr);
#  else
  UNUSED_VARS(ami, reports, amb_ptr);
#  endif
}

static XrActionMapBinding *api_XrActionMapBinding_find(XrActionMapItem *ami, const char *name)
{
#  ifdef WITH_XR_OPENXR
  return wm_xr_actionmap_binding_find(ami, name);
#  else
  UNUSED_VARS(ami, name);
  return NULL;
#  endif
}

static void api_XrActionMapBinding_component_paths_begin(CollectionPropIter *iter,
                                                         ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapBinding *amb = (XrActionMapBinding *)ptr->data;
  api_iterator_list_begin(iter, &amb->component_paths, NULL);
#  else
  UNUSED_VARS(iter, ptr);
#  endif
}

static int api_XrActionMapBinding_component_paths_length(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapBinding *amb = (XrActionMapBinding *)ptr->data;
  return lib_list_count(&amb->component_paths);
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static int api_XrActionMapBinding_axis0_region_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapBinding *amb = ptr->data;
  if ((amb->axis_flag & XR_AXIS0_POS) != 0) {
    return XR_AXIS0_POS;
  }
  if ((amb->axis_flag & XR_AXIS0_NEG) != 0) {
    return XR_AXIS0_NEG;
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  return 0;
}

static void api_XrActionMapBinding_axis0_region_set(ApiPtr *ptr, int value)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapBinding *amb = ptr->data;
  amb->axis_flag &= ~(XR_AXIS0_POS | XR_AXIS0_NEG);
  amb->axis_flag |= value;
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static int api_XrActionMapBinding_axis1_region_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapBinding *amb = ptr->data;
  if ((amb->axis_flag & XR_AXIS1_POS) != 0) {
    return XR_AXIS1_POS;
  }
  if ((amb->axis_flag & XR_AXIS1_NEG) != 0) {
    return XR_AXIS1_NEG;
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  return 0;
}

static void api_XrActionMapBinding_axis1_region_set(ApiPtr *ptr, int value)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapBinding *amb = ptr->data;
  amb->axis_flag &= ~(XR_AXIS1_POS | XR_AXIS1_NEG);
  amb->axis_flag |= value;
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static void api_XrActionMapBinding_name_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = main->wm.first;
  if (wm && wm->xr.runtime) {
    List *actionmaps = wm_xr_actionmaps_get(wm->xr.runtime);
    short idx = wm_xr_actionmap_selected_index_get(wm->xr.runtime);
    XrActionMap *actionmap = lib_findlink(actionmaps, idx);
    if (actionmap) {
      XrActionMapItem *ami = lib_findlink(&actionmap->items, actionmap->selitem);
      if (ami) {
        XrActionMapBinding *amb = ptr->data;
        wm_xr_actionmap_binding_ensure_unique(ami, amb);
      }
    }
  }
#  else
  UNUSED_VARS(main, ptr);
#  endif
}

static XrUserPath *api_XrUserPath_new(XrActionMapItem *ami, const char *path_str)
{
#  ifdef WITH_XR_OPENXR
  XrUserPath *user_path = mem_callocn(sizeof(XrUserPath), __func__);
  STRNCPY(user_path->path, path_str);
  lib_addtail(&ami->user_paths, user_path);
  return user_path;
#  else
  UNUSED_VARS(ami, path_str);
  return NULL;
#  endif
}

static void api_XrUserPath_remove(XrActionMapItem *ami, ApiPtr *user_path_ptr)
{
#  ifdef WITH_XR_OPENXR
  XrUserPath *user_path = user_path_ptr->data;
  int idx = lib_findindex(&ami->user_paths, user_path);
  if (idx != -1) {
    lib_freelinkn(&ami->user_paths, user_path);
  }
  API_PTR_INVALIDATE(user_path_ptr);
#  else
  UNUSED_VARS(ami, user_path_ptr);
#  endif
}

static XrUserPath *api_XrUserPath_find(XrActionMapItem *ami, const char *path_str)
{
#  ifdef WITH_XR_OPENXR
  return lib_findstring(&ami->user_paths, path_str, offsetof(XrUserPath, path));
#  else
  UNUSED_VARS(ami, path_str);
  return NULL;
#  endif
}

static XrActionMapItem *api_XrActionMapItem_new(XrActionMap *am,
                                                const char *name,
                                                bool replace_existing)
{
#  ifdef WITH_XR_OPENXR
  return wm_xr_actionmap_item_new(am, name, replace_existing);
#  else
  UNUSED_VARS(am, name, replace_existing);
  return NULL;
#  endif
}

static XrActionMapItem *api_XrActionMapItem_new_from_item(XrActionMap *am,
                                                          XrActionMapItem *ami_src)
{
#  ifdef WITH_XR_OPENXR
  return wm_xr_actionmap_item_add_copy(am, ami_src);
#  else
  UNUSED_VARS(am, ami_src);
  return NULL;
#  endif
}

static void api_XrActionMapItem_remove(XrActionMap *am, ReportList *reports, PointerRNA *ami_ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ami_ptr->data;
  if (wm_xr_actionmap_item_remove(am, ami) == false) {
    dune_reportf(
        reports, RPT_ERROR, "ActionMapItem '%s' cannot be removed from '%s'", ami->name, am->name);
    return;
  }
  API_PTR_INVALIDATE(ami_ptr);
#  else
  UNUSED_VARS(am, reports, ami_ptr);
#  endif
}

static XrActionMapItem *api_XrActionMapItem_find(XrActionMap *am, const char *name)
{
#  ifdef WITH_XR_OPENXR
  return wm_xr_actionmap_item_find(am, name);
#  else
  UNUSED_VARS(am, name);
  return NULL;
#  endif
}

static void api_XrActionMapItem_user_paths_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = (XrActionMapItem *)ptr->data;
  api_iter_list_begin(iter, &ami->user_paths, NULL);
#  else
  UNUSED_VARS(iter, ptr);
#  endif
}

static int api_XrActionMapItem_user_paths_length(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = (XrActionMapItem *)ptr->data;
  return lib_list_count(&ami->user_paths);
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static void api_XrActionMapItem_op_name_get(ApiPtr *ptr, char *value)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  if (ami->op[0]) {
    if (ami->op_props_ptr) {
      wmOpType *ot = wm_optype_find(ami->op, 1);
      if (ot) {
        strcpy(value, wm_optype_name(ot, ami->op_props_ptr));
        return;
      }
    }
    strcpy(value, ami->op);
    return;
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  value[0] = '\0';
}

static int api_XrActionMapItem_op_name_length(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  if (ami->op[0]) {
    if (ami->op_props_ptr) {
      wmOpType *ot = wm_optype_find(ami->op, 1);
      if (ot) {
        return strlen(wm_optype_name(ot, ami->op_props_ptr));
      }
    }
    return strlen(ami->op);
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  return 0;
}

static ApiPtr api_XrActionMapItem_op_props_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  if (ami->op_props_ptr) {
    return *(ami->op_props_ptr);
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  return ApiPtr_NULL;
}

static bool api_XrActionMapItem_bimanual_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  if ((ami->action_flag & XR_ACTION_BIMANUAL) != 0) {
    return true;
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  return false;
}

static void api_XrActionMapItem_bimanual_set(ApiPtr *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  SET_FLAG_FROM_TEST(ami->action_flag, value, XR_ACTION_BIMANUAL);
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool api_XrActionMapItem_haptic_match_user_paths_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  if ((ami->haptic_flag & XR_HAPTIC_MATCHUSERPATHS) != 0) {
    return true;
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  return false;
}

static void api_XrActionMapItem_haptic_match_user_paths_set(ApiPtr *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  SET_FLAG_FROM_TEST(ami->haptic_flag, value, XR_HAPTIC_MATCHUSERPATHS);
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static int api_XrActionMapItem_haptic_mode_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  if ((ami->haptic_flag & XR_HAPTIC_RELEASE) != 0) {
    return ((ami->haptic_flag & XR_HAPTIC_PRESS) != 0) ? (XR_HAPTIC_PRESS | XR_HAPTIC_RELEASE) :
                                                         XR_HAPTIC_RELEASE;
  }
  if ((ami->haptic_flag & XR_HAPTIC_REPEAT) != 0) {
    return XR_HAPTIC_REPEAT;
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  return XR_HAPTIC_PRESS;
}

static void api_XrActionMapItem_haptic_mode_set(ApiPtr *ptr, int value)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  ami->haptic_flag &= ~(XR_HAPTIC_PRESS | XR_HAPTIC_RELEASE | XR_HAPTIC_REPEAT);
  ami->haptic_flag |= value;
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool api_XrActionMapItem_pose_is_controller_grip_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  if ((ami->pose_flag & XR_POSE_GRIP) != 0) {
    return true;
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  return false;
}

static void api_XrActionMapItem_pose_is_controller_grip_set(PointerRNA *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  SET_FLAG_FROM_TEST(ami->pose_flag, value, XR_POSE_GRIP);
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool api_XrActionMapItem_pose_is_controller_aim_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  if ((ami->pose_flag & XR_POSE_AIM) != 0) {
    return true;
  }
#  else
  UNUSED_VARS(ptr);
#  endif
  return false;
}

static void api_XrActionMapItem_pose_is_controller_aim_set(ApiPtr *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  SET_FLAG_FROM_TEST(ami->pose_flag, value, XR_POSE_AIM);
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static void api_XrActionMapItem_bindings_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = (XrActionMapItem *)ptr->data;
  api_iter_list_begin(iter, &ami->bindings, NULL);
#  else
  UNUSED_VARS(iter, ptr);
#  endif
}

static int api_XrActionMapItem_bindings_length(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = (XrActionMapItem *)ptr->data;
  return lib_list_count(&ami->bindings);
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static void api_XrActionMapItem_name_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  WM *wm = main->wm.first;
  if (wm && wm->xr.runtime) {
    List *actionmaps = wm_xr_actionmaps_get(wm->xr.runtime);
    short idx = wm_xr_actionmap_selected_index_get(wm->xr.runtime);
    XrActionMap *actionmap = lib_findlink(actionmaps, idx);
    if (actionmap) {
      XrActionMapItem *ami = ptr->data;
      wm_xr_actionmap_item_ensure_unique(actionmap, ami);
    }
  }
#  else
  UNUSED_VARS(main, ptr);
#  endif
}

static void api_XrActionMapItem_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMapItem *ami = ptr->data;
  wm_xr_actionmap_item_props_update_ot(ami);
#  else
  UNUSED_VARS(ptr);
#  endif
}

static XrActionMap *api_XrActionMap_new(ApiPtr *ptr, const char *name, bool replace_existing)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  return wm_xr_actionmap_new(xr->runtime, name, replace_existing);
#  else
  UNUSED_VARS(ptr, name, replace_existing);
  return NULL;
#  endif
}

static XrActionMap *api_XrActionMap_new_from_actionmap(ApiPtr *ptr, XrActionMap *am_src)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  return wm_xr_actionmap_add_copy(xr->runtime, am_src);
#  else
  UNUSED_VARS(ptr, am_src);
  return NULL;
#  endif
}

static void api_XrActionMap_remove(ReportList *reports, ApiPtr *ptr, ApiPtr *actionmap_ptr)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  XrActionMap *actionmap = actionmap_ptr->data;
  if (wm_xr_actionmap_remove(xr->runtime, actionmap) == false) {
    dune_reportf(reports, RPT_ERROR, "ActionMap '%s' cannot be removed", actionmap->name);
    return;
  }
  API_PTR_INVALIDATE(actionmap_ptr);
#  else
  UNUSED_VARS(ptr, reports, actionmap_ptr);
#  endif
}

static XrActionMap api_XrActionMap_find(ApiPtr *ptr, const char *name)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  return wm_xr_actionmap_find(xr->runtime, name);
#  else
  UNUSED_VARS(ptr, name);
  return NULL;
#  endif
}

static void api_XrActionMap_items_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMap *actionmap = (XrActionMap *)ptr->data;
  api_iter_list_begin(iter, &actionmap->items, NULL);
#  else
  UNUSED_VARS(iter, ptr);
#  endif
}

static int api_XrActionMap_items_length(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  XrActionMap *actionmap = (XrActionMap *)ptr->data;
  return lib_list_count(&actionmap->items);
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static void api_XrActionMap_name_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = main->wm.first;
  if (wm && wm->xr.runtime) {
    XrActionMap *actionmap = ptr->data;
    wm_xr_actionmap_ensure_unique(wm->xr.runtime, actionmap);
  }
#  else
  UNUSED_VARS(main, ptr);
#  endif
}

/* -------------------------------------------------------------------- */
/** XR Session Settings **/

static bool api_XrSessionSettings_use_positional_tracking_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.flag & XR_SESSION_USE_POSITION_TRACKING) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void api_XrSessionSettings_use_positional_tracking_set(ApiPtr *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  SET_FLAG_FROM_TEST(xr->session_settings.flag, value, XR_SESSION_USE_POSITION_TRACKING);
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool api_XrSessionSettings_use_absolute_tracking_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.flag & XR_SESSION_USE_ABSOLUTE_TRACKING) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void api_XrSessionSettings_use_absolute_tracking_set(ApiPtr *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  SET_FLAG_FROM_TEST(xr->session_settings.flag, value, XR_SESSION_USE_ABSOLUTE_TRACKING);
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static int api_XrSessionSettings_icon_from_show_object_viewport_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  return api_object_type_visibility_icon_get_common(
      xr->session_settings.object_type_exclude_viewport,
#    if 0
    /* For the future when selection in VR is reliably supported. */
    &xr->session_settings.object_type_exclude_select
#    else
      NULL
#    endif
  );
#  else
  UNUSED_VARS(ptr);
  return ICON_NONE;
#  endif
}

/* -------------------------------------------------------------------- */
/** XR Session State **/

static bool api_XrSessionState_is_running(Cxt *C)
{
#  ifdef WITH_XR_OPENXR
  const wmWindowManager *wm = cxt_wm_manager(C);
  return wm_xr_session_exists(&wm->xr);
#  else
  UNUSED_VARS(C);
  return false;
#  endif
}

static void api_XrSessionState_reset_to_base_pose(Cxt *C)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = cxt_wm_manager(C);
  wm_xr_session_base_pose_reset(&wm->xr);
#  else
  UNUSED_VARS(C);
#  endif
}

static bool api_XrSessionState_action_set_create(Cxt *C, XrActionMap *actionmap)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = cxt_wm_manager(C);
  return wm_xr_action_set_create(&wm->xr, actionmap->name);
#  else
  UNUSED_VARS(C, actionmap);
  return false;
#  endif
}

static bool api_XrSessionState_action_create(Cxt *C,
                                             XrActionMap *actionmap,
                                             XrActionMapItem *ami)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = cxt_wm_manager(C);
  if (lib_is_empty(&ami->user_paths)) {
    return false;
  }

  const bool is_float_action = ELEM(ami->type, XR_FLOAT_INPUT, XR_VECTOR2F_INPUT);
  const bool is_byn_action = (is_float_action || ami->type == XR_BOOL_INPUT);
  wmOpType *ot = NULL;
  IdProp *op_props = NULL;
  int64_t haptic_duration_msec;

  if (is_btn_action) {
    if (ami->op[0]) {
      char idname[OP_MAX_TYPENAME];
      wm_op_bl_idname(idname, ami->op);
      ot = wm_optype_find(idname, true);
      if (ot) {
        op_props = ami->op_props;
      }
    }

    haptic_duration_msec = (int64_t)(ami->haptic_duration * 1000.0f);
  }

  return wm_xr_action_create(&wm->xr,
                             actionmap->name,
                             ami->name,
                             ami->type,
                             &ami->user_paths,
                             ot,
                             op_props,
                             is_btn_action ? ami->haptic_name : NULL,
                             is_btn_action ? &haptic_duration_msec : NULL,
                             is_btn_action ? &ami->haptic_frequency : NULL,
                             is_btn_action ? &ami->haptic_amplitude : NULL,
                             ami->op_flag,
                             ami->action_flag,
                             ami->haptic_flag);
#  else
  UNUSED_VARS(C, actionmap, ami);
  return false;
#  endif
}

static bool api_XrSessionState_action_binding_create(Cxt *C,
                                                     XrActionMap *actionmap,
                                                     XrActionMapItem *ami,
                                                     XrActionMapBinding *amb)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = cxt_wm_manager(C);
  const int count_user_paths = lib_list_count(&ami->user_paths);
  const int count_component_paths = lib_list_count(&amb->component_paths);
  if (count_user_paths < 1 || (count_user_paths != count_component_paths)) {
    return false;
  }

  const bool is_float_action = ELEM(ami->type, XR_FLOAT_INPUT, XR_VECTOR2F_INPUT);
  const bool is_btn_action = (is_float_action || ami->type == XR_BOOL_INPUT);
  const bool is_pose_action = (ami->type == XR_POSE_INPUT);
  float float_thresholds[2];
  eXrAxisFlag axis_flags[2];
  wmXrPose poses[2];

  if (is_float_action) {
    float_thresholds[0] = float_thresholds[1] = amb->float_threshold;
  }
  if (is_button_action) {
    axis_flags[0] = axis_flags[1] = amb->axis_flag;
  }
  if (is_pose_action) {
    copy_v3_v3(poses[0].position, amb->pose_location);
    eul_to_quat(poses[0].orientation_quat, amb->pose_rotation);
    normalize_qt(poses[0].orientation_quat);
    memcpy(&poses[1], &poses[0], sizeof(poses[1]));
  }

  return wm_xr_action_binding_create(&wm->xr,
                                     actionmap->name,
                                     ami->name,
                                     amb->profile,
                                     &ami->user_paths,
                                     &amb->component_paths,
                                     is_float_action ? float_thresholds : NULL,
                                     is_btn_action ? axis_flags : NULL,
                                     is_pose_action ? poses : NULL);
#  else
  UNUSED_VARS(C, actionmap, ami, amb);
  return false;
#  endif
}

bool api_XrSessionState_active_action_set_set(Cxt *C, const char *action_set_name)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = cxt_wm_manager(C);
  return wm_xr_active_action_set_set(&wm->xr, action_set_name, true);
#  else
  UNUSED_VARS(C, action_set_name);
  return false;
#  endif
}

bool api_XrSessionState_controller_pose_actions_set(Cxt *C,
                                                    const char *action_set_name,
                                                    const char *grip_action_name,
                                                    const char *aim_action_name)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = cxt_wm_manager(C);
  return wm_xr_controller_pose_actions_set(
      &wm->xr, action_set_name, grip_action_name, aim_action_name);
#  else
  UNUSED_VARS(C, action_set_name, grip_action_name, aim_action_name);
  return false;
#  endif
}

void api_XrSessionState_action_state_get(Cxt *C,
                                         const char *action_set_name,
                                         const char *action_name,
                                         const char *user_path,
                                         float r_state[2])
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = cxt_wm_manager(C);
  wmXrActionState state;
  if (wm_xr_action_state_get(&wm->xr, action_set_name, action_name, user_path, &state)) {
    switch (state.type) {
      case XR_BOOL_INPUT:
        r_state[0] = (float)state.state_bool;
        r_state[1] = 0.0f;
        return;
      case XR_FLOAT_INPUT:
        r_state[0] = state.state_float;
        r_state[1] = 0.0f;
        return;
      case XR_VECTOR2F_INPUT:
        copy_v2_v2(r_state, state.state_vector2f);
        return;
      case XR_POSE_INPUT:
      case XR_VIBRATION_OUTPUT:
        lib_assert_unreachable();
        break;
    }
  }
#  else
  UNUSED_VARS(C, action_set_name, action_name, user_path);
#  endif
  zero_v2(r_state);
}

bool api_XrSessionState_haptic_action_apply(Cxt *C,
                                            const char *action_set_name,
                                            const char *action_name,
                                            const char *user_path,
                                            float duration,
                                            float frequency,
                                            float amplitude)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = cxt_wm_manager(C);
  int64_t duration_msec = (int64_t)(duration * 1000.0f);
  return wm_xr_haptic_action_apply(&wm->xr,
                                   action_set_name,
                                   action_name,
                                   user_path[0] ? user_path : NULL,
                                   &duration_msec,
                                   &frequency,
                                   &amplitude);
#  else
  UNUSED_VARS(C, action_set_name, action_name, user_path, duration, frequency, amplitude);
  return false;
#  endif
}

void api_XrSessionState_haptic_action_stop(Cxt *C,
                                           const char *action_set_name,
                                           const char *action_name,
                                           const char *user_path)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = cxt_wm_manager(C);
  wm_xr_haptic_action_stop(&wm->xr, action_set_name, action_name, user_path[0] ? user_path : NULL);
#  else
  UNUSED_VARS(C, action_set_name, action_name, user_path);
#  endif
}

static void api_XrSessionState_controller_grip_location_get(Cxt *C,
                                                            int index,
                                                            float r_values[3])
{
#  ifdef WITH_XR_OPENXR
  const wmWindowManager *wm = cxt_wm_manager(C);
  wm_xr_session_state_controller_grip_location_get(&wm->xr, index, r_values);
#  else
  UNUSED_VARS(C, index);
  zero_v3(r_values);
#  endif
}

static void rn_XrSessionState_controller_grip_rotation_get(Cxt *C,
                                                            int index,
                                                            float r_values[4])
{
#  ifdef WITH_XR_OPENXR
  const wmWindowManager *wm = CTX_wm_manager(C);
  WM_xr_session_state_controller_grip_rotation_get(&wm->xr, index, r_values);
#  else
  UNUSED_VARS(C, index);
  unit_qt(r_values);
#  endif
}

static void api_XrSessionState_controller_aim_location_get(Cxt *C,
                                                           int index,
                                                           float r_values[3])
{
#  ifdef WITH_XR_OPENXR
  const WM *wm = cxt_wm_manager(C);
  wm_xr_session_state_controller_aim_location_get(&wm->xr, index, r_values);
#  else
  UNUSED_VARS(C, index);
  zero_v3(r_values);
#  endif
}

static void api_XrSessionState_controller_aim_rotation_get(Cxt *C,
                                                           int index,
                                                           float r_values[4])
{
#  ifdef WITH_XR_OPENXR
  const wmWindowManager *wm = cxt_wm_manager(C);
  wm_xr_session_state_controller_aim_rotation_get(&wm->xr, index, r_values);
#  else
  UNUSED_VARS(C, index);
  unit_qt(r_values);
#  endif
}

static void api_XrSessionState_viewer_pose_location_get(ApiPtr *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  wm_xr_session_state_viewer_pose_location_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  zero_v3(r_values);
#  endif
}

static void api_XrSessionState_viewer_pose_rotation_get(ApiPtr *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  wm_xr_session_state_viewer_pose_rotation_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  unit_qt(r_values);
#  endif
}

static void api_XrSessionState_nav_location_get(ApiPtr *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  _xr_session_state_nav_location_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  zero_v3(r_values);
#  endif
}

static void api_XrSessionState_nav_location_set(ApiPtr *ptr, const float *values)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  wm_xr_session_state_nav_location_set(xr, values);
#  else
  UNUSED_VARS(ptr, values);
#  endif
}

static void api_XrSessionState_nav_rotation_get(ApiPtr *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  wm_xr_session_state_nav_rotation_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  unit_qt(r_values);
#  endif
}

static void api_XrSessionState_nav_rotation_set(ApiPtr *ptr, const float *values)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  wm_xr_session_state_nav_rotation_set(xr, values);
#  else
  UNUSED_VARS(ptr, values);
#  endif
}

static float api_XrSessionState_nav_scale_get(ApiPtr *ptr)
{
  float value;
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  wm_xr_session_state_nav_scale_get(xr, &value);
#  else
  UNUSED_VARS(ptr);
  value = 1.0f;
#  endif
  return value;
}

static void api_XrSessionState_nav_scale_set(ApiPtr *ptr, float value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  wm_xr_session_state_nav_scale_set(xr, value);
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static void api_XrSessionState_actionmaps_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  List *lb = wm_xr_actionmaps_get(xr->runtime);
  api_iter_list_begin(iter, lb, NULL);
#  else
  UNUSED_VARS(iter, ptr);
#  endif
}

static int api_XrSessionState_actionmaps_length(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  List *lb = wm_xr_actionmaps_get(xr->runtime);
  return lib_list_count(lb);
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static int api_XrSessionState_active_actionmap_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  return wm_xr_actionmap_active_index_get(xr->runtime);
#  else
  UNUSED_VARS(ptr);
  return -1;
#  endif
}

static void api_XrSessionState_active_actionmap_set(ApiPtr *ptr, int value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  wm_xr_actionmap_active_index_set(xr->runtime, (short)value);
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static int api_XrSessionState_selected_actionmap_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  return wm_xr_actionmap_selected_index_get(xr->runtime);
#  else
  UNUSED_VARS(ptr);
  return -1;
#  endif
}

static void api_XrSessionState_selected_actionmap_set(ApiPtr *ptr, int value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = api_XrSession_wm_xr_data_get(ptr);
  wm_xr_actionmap_selected_index_set(xr->runtime, (short)value);
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

/** \} */

/* -------------------------------------------------------------------- */
/** XR Event Data **/

static void api_XrEventData_action_set_get(ApiPtr *ptr, char *r_value)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  strcpy(r_value, data->action_set);
#  else
  UNUSED_VARS(ptr);
  r_value[0] = '\0';
#  endif
}

static int api_XrEventData_action_set_length(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  return strlen(data->action_set);
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static void api_XrEventData_action_get(ApiPtr *ptr, char *r_value)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  strcpy(r_value, data->action);
#  else
  UNUSED_VARS(ptr);
  r_value[0] = '\0';
#  endif
}

static int api_XrEventData_action_length(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  return strlen(data->action);
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static void r_XrEventData_user_path_get(ApiPtr *ptr, char *r_value)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  strcpy(r_value, data->user_path);
#  else
  UNUSED_VARS(ptr);
  r_value[0] = '\0';
#  endif
}

static int api_XrEventData_user_path_length(ApPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  return strlen(data->user_path);
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static void api_XrEventData_user_path_other_get(Apitr *ptr, char *r_value)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  strcpy(r_value, data->user_path_other);
#  else
  UNUSED_VARS(ptr);
  r_value[0] = '\0';
#  endif
}

static int api_XrEventData_user_path_other_length(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  return strlen(data->user_path_other);
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static int api_XrEventData_type_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  return data->type;
#  else
  UNUSED_VARS(ptr);
  return 0;
#  endif
}

static void api_XrEventData_state_get(ApiPtr *ptr, float r_values[2])
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  copy_v2_v2(r_values, data->state);
#  else
  UNUSED_VARS(ptr);
  zero_v2(r_values);
#  endif
}

static void api_XrEventData_state_other_get(ApiPtr *ptr, float r_values[2])
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  copy_v2_v2(r_values, data->state_other);
#  else
  UNUSED_VARS(ptr);
  zero_v2(r_values);
#  endif
}

static float api_XrEventData_float_threshold_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  return data->float_threshold;
#  else
  UNUSED_VARS(ptr);
  return 0.0f;
#  endif
}

static void api_XrEventData_controller_location_get(ApiPtr *ptr, float r_values[3])
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  copy_v3_v3(r_values, data->controller_loc);
#  else
  UNUSED_VARS(ptr);
  zero_v3(r_values);
#  endif
}

static void api_XrEventData_controller_rotation_get(ApiPtr *ptr, float r_values[4])
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  copy_qt_qt(r_values, data->controller_rot);
#  else
  UNUSED_VARS(ptr);
  unit_qt(r_values);
#  endif
}

static void api_XrEventData_controller_location_other_get(ApiPtr *ptr, float r_values[3])
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  copy_v3_v3(r_values, data->controller_loc_other);
#  else
  UNUSED_VARS(ptr);
  zero_v3(r_values);
#  endif
}

static void api_XrEventData_controller_rotation_other_get(ApiPtr *ptr, float r_values[4])
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  copy_qt_qt(r_values, data->controller_rot_other);
#  else
  UNUSED_VARS(ptr);
  unit_qt(r_values);
#  endif
}

static bool api_XrEventData_bimanual_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrActionData *data = ptr->data;
  return data->bimanual;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

/** \} */

#else /* API_RUNTIME */

/* -------------------------------------------------------------------- */

static const EnumPropItem api_enum_xr_action_types[] = {
    {XR_FLOAT_INPUT,
     "FLOAT",
     0,
     "Float",
     "Float action, representing either a digital or analog button"},
    {XR_VECTOR2F_INPUT,
     "VECTOR2D",
     0,
     "Vector2D",
     "2D float vector action, representing a thumbstick or trackpad"},
    {XR_POSE_INPUT,
     "POSE",
     0,
     "Pose",
     "3D pose action, representing a controller's location and rotation"},
    {XR_VIBRATION_OUTPUT,
     "VIBRATION",
     0,
     "Vibration",
     "Haptic vibration output action, to be applied with a duration, frequency, and amplitude"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_xr_op_flags[] = {
    {XR_OP_PRESS,
     "PRESS",
     0,
     "Press",
     "Execute op on button press (non-modal ops only)"},
    {XR_OP_RELEASE,
     "RELEASE",
     0,
     "Release",
     "Execute op on btn release (non-modal ops only)"},
    {XR_OP_MODAL, "MODAL", 0, "Modal", "Use modal execution (modal ops only)"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_xr_haptic_flags[] = {
    {XR_HAPTIC_PRESS, "PRESS", 0, "Press", "Apply haptics on button press"},
    {XR_HAPTIC_RELEASE, "RELEASE", 0, "Release", "Apply haptics on button release"},
    {XR_HAPTIC_PRESS | XR_HAPTIC_RELEASE,
     "PRESS_RELEASE",
     0,
     "Press Release",
     "Apply haptics on button press and release"},
    {XR_HAPTIC_REPEAT,
     "REPEAT",
     0,
     "Repeat",
     "Apply haptics repeatedly for the duration of the button press"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_xr_axis0_flags[] = {
    {0, "ANY", 0, "Any", "Use any axis region for op execution"},
    {XR_AXIS0_POS,
     "POSITIVE",
     0,
     "Positive",
     "Use positive axis region only for op execution"},
    {XR_AXIS0_NEG,
     "NEGATIVE",
     0,
     "Negative",
     "Use negative axis region only for op execution"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_xr_axis1_flags[] = {
    {0, "ANY", 0, "Any", "Use any axis region for op execution"},
    {XR_AXIS1_POS,
     "POSITIVE",
     0,
     "Positive",
     "Use positive axis region only for operator execution"},
    {XR_AXIS1_NEG,
     "NEGATIVE",
     0,
     "Negative",
     "Use negative axis region only for operator execution"},
    {0, NULL, 0, NULL, NULL},
};

/* -------------------------------------------------------------------- */
/** XR Action Map **/

static void api_def_xr_component_paths(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "XrComponentPaths");
  sapi = api_def_struct(dapi, "XrComponentPaths", NULL);
  api_def_struct_stype(sapi, "XrActionMapBinding");
  api_def_struct_ui_text(sapi, "XR Component Paths", "Collection of OpenXR component paths");

  fn = api_def_fn(sapi, "new", "api_XrComponentPath_new");
  parm = api_def_string(
      fn, "path", NULL, XR_MAX_COMPONENT_PATH_LENGTH, "Path", "OpenXR component path");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "component_path", "XrComponentPath", "Component Path", "Added component path");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_XrComponentPath_remove");
  parm = api_def_ptr(fn, "component_path", "XrComponentPath", "Component Path", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "find", "api_XrComponentPath_find");
  parm = api_def_string(
      fn, "path", NULL, XR_MAX_COMPONENT_PATH_LENGTH, "Path", "OpenXR component path");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn,
                     "component_path",
                     "XrComponentPath",
                     "Component Path",
                     "The component path with the given path");
  api_def_fn_return(fn, parm);
}

static void api_def_xr_actionmap_bindings(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "XrActionMapBindings");
  sapi = api_def_struct(dapi, "XrActionMapBindings", NULL);
  api_def_struct_stype(sapi, "XrActionMapItem");
  api_def_struct_ui_text(sapi, "XR Action Map Bindings", "Collection of XR action map bindings");

  fn = api_def_fn(sapi, "new", "api_XrActionMapBinding_new");
  parm = api_def_string(fb, "name", NULL, MAX_NAME, "Name of the action map binding", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn,
                      "replace_existing",
                      true,
                      "Replace Existing",
                      "Replace any existing binding with the same name");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "binding", "XrActionMapBinding", "Binding", "Added action map binding");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_from_binding", "api_XrActionMapBinding_new_from_binding");
  parm = api_def_ptr(
      fn, "binding", "XrActionMapBinding", "Binding", "Binding to use as a ref");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "result", "XrActionMapBinding", "Binding", "Added action map binding");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_XrActionMapBinding_remove");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "binding", "XrActionMapBinding", "Binding", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "find", "api_XrActionMapBinding_find");
  parm = api_def_string(fn, "name", NULL, MAX_NAME, "Name", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn,
                     "binding",
                     "XrActionMapBinding",
                     "Binding",
                     "The action map binding with the given name");
  api_def_fn_return(fn, parm);
}

static void api_def_xr_user_paths(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "XrUserPaths");
  sapi = api_def_struct(dapi, "XrUserPaths", NULL);
  api_def_struct_stype(sapi, "XrActionMapItem");
  api_def_struct_ui_text(sapi, "XR User Paths", "Collection of OpenXR user paths");

  fn = api_def_fn(sapi, "new", "api_XrUserPath_new");
  parm = api_def_string(fn, "path", NULL, XR_MAX_USER_PATH_LENGTH, "Path", "OpenXR user path");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "user_path", "XrUserPath", "User Path", "Added user path");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_XrUserPath_remove");
  parm = api_def_ptr(fn, "user_path", "XrUserPath", "User Path", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "find", "api_XrUserPath_find");
  parm = api_def_string(fn, "path", NULL, XR_MAX_USER_PATH_LENGTH, "Path", "OpenXR user path");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "user_path", "XrUserPath", "User Path", "The user path with the given path");
  api_def_fn_return(fn, parm);
}

static void api_def_xr_actionmap_items(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "XrActionMapItems");
  sapi = api_def_struct(dapi, "XrActionMapItems", NULL);
  api_def_struct_stype(sapi, "XrActionMap");
  api_def_struct_ui_text(sapi, "XR Action Map Items", "Collection of XR action map items");

  fn = api_def_fn(sapi, "new", "api_XrActionMapItem_new");
  parm = api_def_string(fn, "name", NULL, MAX_NAME, "Name of the action map item", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn,
                      "replace_existing",
                      true,
                      "Replace Existing",
                      "Replace any existing item with the same name");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "item", "XrActionMapItem", "Item", "Added action map item");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_from_item", "api_XrActionMapItem_new_from_item");
  parm = api_def_ptr(fn, "item", "XrActionMapItem", "Item", "Item to use as a reference");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_ptr(fn, "result", "XrActionMapItem", "Item", "Added action map item");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_XrActionMapItem_remove");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "item", "XrActionMapItem", "Item", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "find", "api_XrActionMapItem_find");
  parm = api_def_string(fn, "name", NULL, MAX_NAME, "Name", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "item", "XrActionMapItem", "Item", "The action map item with the given name");
  api_def_fn_return(fn, parm);
}

static void api_def_xr_actionmaps(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "XrActionMaps");
  sapi = api_def_struct(dapi, "XrActionMaps", NULL);
  api_def_struct_ui_text(sapi, "XR Action Maps", "Collection of XR action maps");

  fn = api_def_fn(sapi, "new", "api_XrActionMap_new");
  api_def_fn_flag(fn, FN_NO_SELF);
  parm = api_def_ptr(fn, "xr_session_state", "XrSessionState", "XR Session State", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(fn, "name", NULL, MAX_NAME, "Name", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_bool(fn,
                      "replace_existing",
                      true,
                      "Replace Existing",
                      "Replace any existing actionmap with the same name");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "actionmap", "XrActionMap", "Action Map", "Added action map");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "new_from_actionmap", "api_XrActionMap_new_from_actionmap");
  api_def_fn_flag(fn, FN_NO_SELF);
  parm = api_def_ptr(fn, "xr_session_state", "XrSessionState", "XR Session State", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_ptr(
      fn, "actionmap", "XrActionMap", "Action Map", "Action map to use as a reference");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_ptr(fn, "result", "XrActionMap", "Action Map", "Added action map");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_XrActionMap_remove");
  api_def_fn_flag(fn, FN_NO_SELF | FN_USE_REPORTS);
  parm = api_def_ptr(fn, "xr_session_state", "XrSessionState", "XR Session State", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_ptr(fn, "actionmap", "XrActionMap", "Action Map", "Removed action map");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "find", "api_XrActionMap_find");
  api_def_fn_flag(fn, FN_NO_SELF);
  parm = api_def_ptr(fn, "xr_session_state", "XrSessionState", "XR Session State", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  parm = api_def_string(fn, "name", NULL, MAX_NAME, "Name", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "actionmap", "XrActionMap", "Action Map", "The action map with the given name");
  api_def_fn_return(fn, parm);
}

static void api_def_xr_actionmap(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* XrActionMap */
  sapi = api_def_struct(dapi, "XrActionMap", NULL);
  api_def_struct_stype(sapi, "XrActionMap");
  api_def_struct_ui_text(sapi, "XR Action Map", "");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Name of the action map");
  api_def_prop_update(prop, 0, "api_XrActionMap_name_update");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "actionmap_items", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "items", NULL);
  api_def_prop_struct_type(prop, "XrActionMapItem");
  api_def_prop_collection_fns(prop,
                              "api_XrActionMap_items_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              "api_XrActionMap_items_length",
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(
      prop,
      "Items",
      "Items in the action map, mapping an XR event to an op, pose, or haptic output");
  api_def_xr_actionmap_items(dapi, prop);

  prop = api_def_prop(sapi, "selected_item", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "selitem");
  api_def_prop_ui_text(prop, "Selected Item", "");

  /* XrUserPath */
  sapi = api_def_struct(dapi, "XrUserPath", NULL);
  api_def_struct_stype(sapi, "XrUserPath");
  api_def_struct_ui_text(sapi, "XR User Path", "");

  prop = api_def_prop(sapi, "path", PROP_STRING, PROP_NONE);
  api_def_prop_string_maxlength(prop, XR_MAX_USER_PATH_LENGTH);
  api_def_prop_ui_text(prop, "Path", "OpenXR user path");

  /* XrActionMapItem */
  sapi = api_def_struct(dapi, "XrActionMapItem", NULL);
  api_def_struct_stype(sapi, "XrActionMapItem");
  api_def_struct_ui_text(sapi, "XR Action Map Item", "");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "Name of the action map item");
  api_def_prop_update(prop, 0, "api_XrActionMapItem_name_update");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_xr_action_types);
  api_def_prop_ui_text(prop, "Type", "Action type");
  api_def_prop_update(prop, 0, "api_XrActionMapItem_update");

  prop = api_def_prop(sapi, "user_paths", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "XrUserPath");
  api_def_prop_collection_fns(prop,
                              "api_XrActionMapItem_user_paths_begin",
                              "api_iter_list_next",
                              "api_iter_list_end",
                              "api_iter_list_get",
                              "api_XrActionMapItem_user_paths_length",
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(prop, "User Paths", "OpenXR user paths");
  api_def_xr_user_paths(dapi, prop);

  prop = api_def_prop(sapi, "op", PROP_STRING, PROP_NONE);
  api_def_prop_string_maxlength(prop, OP_MAX_TYPENAME);
  api_def_prop_ui_text(prop, "Op", "Id of op to call on action event");
  api_def_prop_update(prop, 0, "api_XrActionMapItem_update");

  prop = api_def_prop(sapi, "op_name", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Op Name", "Name of op (translated) to call on action event");
  api_def_prop_string_fns(
      prop, "api_XrActionMapItem_op_name_get", "api_XrActionMapItem_op_name_length", NULL);

  prop = api_def_prop(sapi, "op_props", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "OpProps");
  api_def_prop_ptr_fns(prop, "api_XrActionMapItem_op_props_get", NULL, NULL, NULL);
  api_def_prop_ui_text(
      prop, "Op Props", "Props to set when the op is called");
  api_def_prop_update(prop, 0, "api_XrActionMapItem_update");

  prop = api_def_prop(sapi, "op_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "op_flag");
  api_def_prop_enum_items(prop, api_enum_xr_op_flags);
  api_def_prop_ui_text(prop, "Operator Mode", "Op execution mode");

  prop = api_def_prop(sapi, "bimanual", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(
      prop, "api_XrActionMapItem_bimanual_get", "api_XrActionMapItem_bimanual_set");
  api_def_prop_ui_text(
      prop, "Bimanual", "The action depends on the states/poses of both user paths");

  prop = api_def_prop(sapi, "pose_is_controller_grip", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_bool_fns(prop,
                                 "rna_XrActionMapItem_pose_is_controller_grip_get",
                                 "rna_XrActionMapItem_pose_is_controller_grip_set");
  RNA_def_property_ui_text(
      prop, "Is Controller Grip", "The action poses will be used for the VR controller grips");

  prop = RNA_def_property(srna, "pose_is_controller_aim", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrActionMapItem_pose_is_controller_aim_get",
                                 "rna_XrActionMapItem_pose_is_controller_aim_set");
  RNA_def_property_ui_text(
      prop, "Is Controller Aim", "The action poses will be used for the VR controller aims");

  prop = RNA_def_property(srna, "haptic_name", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(
      prop, "Haptic Name", "Name of the haptic action to apply when executing this action");

  prop = RNA_def_property(srna, "haptic_match_user_paths", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrActionMapItem_haptic_match_user_paths_get",
                                 "rna_XrActionMapItem_haptic_match_user_paths_set");
  RNA_def_property_ui_text(
      prop,
      "Haptic Match User Paths",
      "Apply haptics to the same user paths for the haptic action and this action");

  prop = RNA_def_property(srna, "haptic_duration", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_text(prop,
                           "Haptic Duration",
                           "Haptic duration in seconds. 0.0 is the minimum supported duration");

  prop = RNA_def_property(srna, "haptic_frequency", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.0, FLT_MAX);
  RNA_def_property_ui_text(prop,
                           "Haptic Frequency",
                           "Frequency of the haptic vibration in hertz. 0.0 specifies the OpenXR "
                           "runtime's default frequency");

  prop = RNA_def_property(srna, "haptic_amplitude", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.0, 1.0);
  RNA_def_property_ui_text(
      prop, "Haptic Amplitude", "Intensity of the haptic vibration, ranging from 0.0 to 1.0");

  prop = RNA_def_property(srna, "haptic_mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_xr_haptic_flags);
  RNA_def_property_enum_funcs(
      prop, "rna_XrActionMapItem_haptic_mode_get", "rna_XrActionMapItem_haptic_mode_set", NULL);
  RNA_def_property_ui_text(prop, "Haptic mode", "Haptic application mode");

  prop = RNA_def_property(srna, "bindings", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "XrActionMapBinding");
  RNA_def_property_collection_funcs(prop,
                                    "rna_XrActionMapItem_bindings_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    "rna_XrActionMapItem_bindings_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_ui_text(
      prop, "Bindings", "Bindings for the action map item, mapping the action to an XR input");
  rna_def_xr_actionmap_bindings(brna, prop);

  prop = RNA_def_property(srna, "selected_binding", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "selbinding");
  RNA_def_property_ui_text(prop, "Selected Binding", "Currently selected binding");

  /* XrComponentPath */
  srna = RNA_def_struct(brna, "XrComponentPath", NULL);
  RNA_def_struct_sdna(srna, "XrComponentPath");
  RNA_def_struct_ui_text(srna, "XR Component Path", "");

  prop = RNA_def_property(srna, "path", PROP_STRING, PROP_NONE);
  RNA_def_property_string_maxlength(prop, XR_MAX_COMPONENT_PATH_LENGTH);
  RNA_def_property_ui_text(prop, "Path", "OpenXR component path");

  /* XrActionMapBinding */
  srna = RNA_def_struct(brna, "XrActionMapBinding", NULL);
  RNA_def_struct_sdna(srna, "XrActionMapBinding");
  RNA_def_struct_ui_text(srna, "XR Action Map Binding", "Binding in an XR action map item");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(prop, "Name", "Name of the action map binding");
  RNA_def_property_update(prop, 0, "rna_XrActionMapBinding_name_update");
  RNA_def_struct_name_property(srna, prop);

  prop = RNA_def_property(srna, "profile", PROP_STRING, PROP_NONE);
  RNA_def_property_string_maxlength(prop, 256);
  RNA_def_property_ui_text(prop, "Profile", "OpenXR interaction profile path");

  prop = RNA_def_property(srna, "component_paths", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "XrComponentPath");
  RNA_def_property_collection_funcs(prop,
                                    "rna_XrActionMapBinding_component_paths_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    "rna_XrActionMapBinding_component_paths_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_ui_text(prop, "Component Paths", "OpenXR component paths");
  rna_def_xr_component_paths(brna, prop);

  prop = RNA_def_property(srna, "threshold", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "float_threshold");
  RNA_def_property_range(prop, 0.0, 1.0);
  RNA_def_property_ui_text(prop, "Threshold", "Input threshold for button/axis actions");

  prop = RNA_def_property(srna, "axis0_region", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_xr_axis0_flags);
  RNA_def_property_enum_funcs(prop,
                              "rna_XrActionMapBinding_axis0_region_get",
                              "rna_XrActionMapBinding_axis0_region_set",
                              NULL);
  RNA_def_property_ui_text(
      prop, "Axis 0 Region", "Action execution region for the first input axis");

  prop = RNA_def_property(srna, "axis1_region", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_xr_axis1_flags);
  RNA_def_property_enum_funcs(prop,
                              "rna_XrActionMapBinding_axis1_region_get",
                              "rna_XrActionMapBinding_axis1_region_set",
                              NULL);
  RNA_def_property_ui_text(
      prop, "Axis 1 Region", "Action execution region for the second input axis");

  prop = RNA_def_property(srna, "pose_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_ui_text(prop, "Pose Location Offset", "");

  prop = RNA_def_property(srna, "pose_rotation", PROP_FLOAT, PROP_EULER);
  RNA_def_property_ui_text(prop, "Pose Rotation Offset", "");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name XR Session Settings
 * \{ */

static void rna_def_xr_session_settings(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem base_pose_types[] = {
      {XR_BASE_POSE_SCENE_CAMERA,
       "SCENE_CAMERA",
       0,
       "Scene Camera",
       "Follow the active scene camera to define the VR view's base pose"},
      {XR_BASE_POSE_OBJECT,
       "OBJECT",
       0,
       "Object",
       "Follow the transformation of an object to define the VR view's base pose"},
      {XR_BASE_POSE_CUSTOM,
       "CUSTOM",
       0,
       "Custom",
       "Follow a custom transformation to define the VR view's base pose"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem controller_draw_styles[] = {
      {XR_CONTROLLER_DRAW_DARK, "DARK", 0, "Dark", "Draw dark controller"},
      {XR_CONTROLLER_DRAW_LIGHT, "LIGHT", 0, "Light", "Draw light controller"},
      {XR_CONTROLLER_DRAW_DARK_RAY,
       "DARK_RAY",
       0,
       "Dark + Ray",
       "Draw dark controller with aiming axis ray"},
      {XR_CONTROLLER_DRAW_LIGHT_RAY,
       "LIGHT_RAY",
       0,
       "Light + Ray",
       "Draw light controller with aiming axis ray"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "XrSessionSettings", NULL);
  RNA_def_struct_ui_text(srna, "XR Session Settings", "");

  prop = RNA_def_property(srna, "shading", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NEVER_NULL);
  RNA_def_property_ui_text(prop, "Shading Settings", "");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "base_pose_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_enum_items(prop, base_pose_types);
  RNA_def_property_ui_text(
      prop,
      "Base Pose Type",
      "Define where the location and rotation for the VR view come from, to which "
      "translation and rotation deltas from the VR headset will be applied to");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_pose_object", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Base Pose Object",
                           "Object to take the location and rotation to which translation and "
                           "rotation deltas from the VR headset will be applied to");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_pose_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_ui_text(prop,
                           "Base Pose Location",
                           "Coordinates to apply translation deltas from the VR headset to");
  RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_pose_angle", PROP_FLOAT, PROP_AXISANGLE);
  RNA_def_property_ui_text(
      prop,
      "Base Pose Angle",
      "Rotation angle around the Z-Axis to apply the rotation deltas from the VR headset to");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_ui_text(prop, "Base Scale", "Uniform scale to apply to VR view");
  RNA_def_property_range(prop, 1e-6f, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  RNA_def_property_float_default(prop, 1.0f);
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_floor", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_SHOW_GRIDFLOOR);
  RNA_def_property_ui_text(prop, "Display Grid Floor", "Show the ground plane grid");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_annotation", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_SHOW_ANNOTATION);
  RNA_def_property_ui_text(prop, "Show Annotation", "Show annotations for this view");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_selection", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_SHOW_SELECTION);
  RNA_def_property_ui_text(prop, "Show Selection", "Show selection outlines");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_controllers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_XR_SHOW_CONTROLLERS);
  RNA_def_property_ui_text(
      prop, "Show Controllers", "Show VR controllers (requires VR actions for controller poses)");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_custom_overlays", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_XR_SHOW_CUSTOM_OVERLAYS);
  RNA_def_property_ui_text(prop, "Show Custom Overlays", "Show custom VR overlays");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_object_extras", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_SHOW_OBJECT_EXTRAS);
  RNA_def_property_ui_text(
      prop, "Show Object Extras", "Show object extras, including empties, lights, and cameras");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "controller_draw_style", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_enum_items(prop, controller_draw_styles);
  RNA_def_property_ui_text(
      prop, "Controller Draw Style", "Style to use when drawing VR controllers");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "clip_start", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_prop_range(prop, 1e-6f, FLT_MAX);
  RNA_def_prop_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  RNA_def_prop_ui_text(prop, "Clip Start", "VR viewport near clipping distance");
  RNA_def_prop_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_prop(sapi, "clip_end", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_prop_range(prop, 1e-6f, FLT_MAX);
  RNA_def_prop_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  RNA_def_prop_ui_text(prop, "Clip End", "VR viewport far clipping distance");
  RNA_def_prop_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_prop(sapi, "use_positional_tracking", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop,
                        "api_XrSessionSettings_use_positional_tracking_get",
                        "api_XrSessionSettings_use_positional_tracking_set");
  api_def_prop_ui_text(
      prop,
      "Positional Tracking",
      "Allow VR headsets to affect the location in virtual space, in addition to the rotation");
  api_def_prop_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = api_def_prop(sapi, "use_absolute_tracking", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop,
                        "api_XrSessionSettings_use_absolute_tracking_get",
                        "api_XrSessionSettings_use_absolute_tracking_set");
  api_def_prop_ui_text(
      prop,
      "Absolute Tracking",
      "Allow the VR tracking origin to be defined independently of the headset location");
  api_def_prop_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  api_def_object_type_visibility_flags_common(srna, NC_WM | ND_XR_DATA_CHANGED, NULL);

  /* Helper for drawing the icon. */
  prop = api_def_prop(sapi, "icon_from_show_object_viewport", PROP_INT, PROP_NONE);
  api_def_prop_int_fns(
      prop, "api_XrSessionSettings_icon_from_show_object_viewport_get", NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Visibility Icon", "");
}

/** \} */

/* -------------------------------------------------------------------- */
/** XR Session State **/

static void api_def_xr_session_state(ApiDune *dapi)
{
  ApiStruct *sapi;
  ApiFn *fn;
  ApiProp *parm, *prop;

  sapi = api_def_struct(dapi, "XrSessionState", NULL);
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Session State", "Runtime state information about the VR session");

  func = RNA_def_fn(sapi, "is_running", "rna_XrSessionState_is_running");
  RNA_def_function_ui_description(fn, "Query if the VR session is currently running");
  RNA_def_function_flag(fn, FN_NO_SELF);
  parm = RNA_def_pointer(fn, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(fn, "result", 0, "Result", "");
  RNA_def_function_return(fn, parm);

  func = RNA_def_function(sapi, "reset_to_base_pose", "rna_XrSessionState_reset_to_base_pose");
  RNA_def_function_ui_description(fn, "Force resetting of position and rotation deltas");
  RNA_def_function_flag(fn, FN_NO_SELF);
  parm = RNA_def_pointer(fn, "cxt", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  func = RNA_def_function(sapi, "action_set_create", "rna_XrSessionState_action_set_create");
  RNA_def_function_ui_description(fn, "Create a VR action set");
  RNA_def_function_flag(fn, FN_NO_SELF);
  parm = RNA_def_pointer(fn, "cxt", "Cxt", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_pointer(fn, "actionmap", "XrActionMap", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(fn, "result", 0, "Result", "");
  RNA_def_function_return(fn, parm);

  func = RNA_def_function(sapi, "action_create", "rna_XrSessionState_action_create");
  RNA_def_function_ui_description(fn, "Create a VR action");
  RNA_def_function_flag(fn, FN_NO_SELF);
  parm = RNA_def_pointer(fn, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_pointer(fn, "actionmap", "XrActionMap", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_pointer(fn, "actionmap_item", "XrActionMapItem", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(fn, "result", 0, "Result", "");
  RNA_def_function_return(fn, parm);

  func = api_def_fn(
      srna, "action_binding_create", "api_XrSessionState_action_binding_create");
  RNA_def_function_ui_description(fn, "Create a VR action binding");
  RNA_def_function_flag(fn, FN_NO_SELF);
  parm = RNA_def_pointer(fn, "cxt", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_pointer(fn, "actionmap", "XrActionMap", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_pointer(fn, "actionmap_item", "XrActionMapItem", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_pointer(fn, "actionmap_binding", "XrActionMapBinding", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_bool(fn, "result", 0, "Result", "");
  RNA_def_function_return(fn, parm);

  func = RNA_def_function(
      srna, "active_action_set_set", "api_XrSessionState_active_action_set_set");
  RNA_def_function_ui_description(fn, "Set the active VR action set");
  RNA_def_function_flag(func, FN_NO_SELF);
  parm = RNA_def_pointer(fn, "cxt", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(fn, "action_set", NULL, MAX_NAME, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(fn, "result", 0, "Result", "");
  RNA_def_function_return(fn, parm);

  func = RNA_def_fn(
      srna, "controller_pose_actions_set", "rna_XrSessionState_controller_pose_actions_set");
  RNA_def_fn_ui_description(func, "Set the actions that determine the VR controller poses");
  RNA_def_fn_flag(fn, FUNC_NO_SELF);
  parm = RNA_def_ptr(fn, "context", "Context", "", "");
  RNA_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_string(func, "action_set", NULL, MAX_NAME, "Action Set", "Action set name");
  RNA_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func,
                        "grip_action",
                        NULL,
                        MAX_NAME,
                        "Grip Action",
                        "Name of the action representing the controller grips");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func,
                        "aim_action",
                        NULL,
                        MAX_NAME,
                        "Aim Action",
                        "Name of the action representing the controller aims");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "action_state_get", "rna_XrSessionState_action_state_get");
  RNA_def_function_ui_description(func, "Get the current state of a VR action");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set_name", NULL, MAX_NAME, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_name", NULL, MAX_NAME, "Action", "Action name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(
      func, "user_path", NULL, XR_MAX_USER_PATH_LENGTH, "User Path", "OpenXR user path");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_float_array(
      func,
      "state",
      2,
      NULL,
      -FLT_MAX,
      FLT_MAX,
      "Action State",
      "Current state of the VR action. Second float value is only set for 2D vector type actions",
      -FLT_MAX,
      FLT_MAX);
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_OUTPUT);

  func = RNA_def_function(srna, "haptic_action_apply", "rna_XrSessionState_haptic_action_apply");
  RNA_def_function_ui_description(func, "Apply a VR haptic action");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set_name", NULL, MAX_NAME, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_name", NULL, MAX_NAME, "Action", "Action name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(
      func,
      "user_path",
      NULL,
      XR_MAX_USER_PATH_LENGTH,
      "User Path",
      "Optional OpenXR user path. If not set, the action will be applied to all paths");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_float(func,
                       "duration",
                       0.0f,
                       0.0f,
                       FLT_MAX,
                       "Duration",
                       "Haptic duration in seconds. 0.0 is the minimum supported duration",
                       0.0f,
                       FLT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float(func,
                       "frequency",
                       0.0f,
                       0.0f,
                       FLT_MAX,
                       "Frequency",
                       "Frequency of the haptic vibration in hertz. 0.0 specifies the OpenXR "
                       "runtime's default frequency",
                       0.0f,
                       FLT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float(func,
                       "amplitude",
                       1.0f,
                       0.0f,
                       1.0f,
                       "Amplitude",
                       "Haptic amplitude, ranging from 0.0 to 1.0",
                       0.0f,
                       1.0f);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "haptic_action_stop", "rna_XrSessionState_haptic_action_stop");
  RNA_def_function_ui_description(func, "Stop a VR haptic action");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set_name", NULL, MAX_NAME, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_name", NULL, MAX_NAME, "Action", "Action name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(
      func,
      "user_path",
      NULL,
      XR_MAX_USER_PATH_LENGTH,
      "User Path",
      "Optional OpenXR user path. If not set, the action will be stopped for all paths");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  func = RNA_def_function(
      srna, "controller_grip_location_get", "rna_XrSessionState_controller_grip_location_get");
  RNA_def_function_ui_description(func,
                                  "Get the last known controller grip location in world space");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_int(func, "index", 0, 0, 255, "Index", "Controller index", 0, 255);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float_translation(func,
                                   "location",
                                   3,
                                   NULL,
                                   -FLT_MAX,
                                   FLT_MAX,
                                   "Location",
                                   "Controller grip location",
                                   -FLT_MAX,
                                   FLT_MAX);
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_OUTPUT);

  func = RNA_def_function(
      srna, "controller_grip_rotation_get", "rna_XrSessionState_controller_grip_rotation_get");
  RNA_def_function_ui_description(
      func, "Get the last known controller grip rotation (quaternion) in world space");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_int(func, "index", 0, 0, 255, "Index", "Controller index", 0, 255);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float_vector(func,
                              "rotation",
                              4,
                              NULL,
                              -FLT_MAX,
                              FLT_MAX,
                              "Rotation",
                              "Controller grip quaternion rotation",
                              -FLT_MAX,
                              FLT_MAX);
  parm->subtype = PROP_QUATERNION;
  RNA_def_property_ui_range(parm, -FLT_MAX, FLT_MAX, 1, 5);
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_OUTPUT);

  func = RNA_def_function(
      srna, "controller_aim_location_get", "rna_XrSessionState_controller_aim_location_get");
  RNA_def_function_ui_description(func,
                                  "Get the last known controller aim location in world space");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_int(func, "index", 0, 0, 255, "Index", "Controller index", 0, 255);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float_translation(func,
                                   "location",
                                   3,
                                   NULL,
                                   -FLT_MAX,
                                   FLT_MAX,
                                   "Location",
                                   "Controller aim location",
                                   -FLT_MAX,
                                   FLT_MAX);
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_OUTPUT);

  func = RNA_def_function(
      srna, "controller_aim_rotation_get", "rna_XrSessionState_controller_aim_rotation_get");
  RNA_def_function_ui_description(
      func, "Get the last known controller aim rotation (quaternion) in world space");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_int(func, "index", 0, 0, 255, "Index", "Controller index", 0, 255);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float_vector(func,
                              "rotation",
                              4,
                              NULL,
                              -FLT_MAX,
                              FLT_MAX,
                              "Rotation",
                              "Controller aim quaternion rotation",
                              -FLT_MAX,
                              FLT_MAX);
  parm->subtype = PROP_QUATERNION;
  RNA_def_property_ui_range(parm, -FLT_MAX, FLT_MAX, 1, 5);
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_OUTPUT);

  prop = RNA_def_property(srna, "viewer_pose_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_viewer_pose_location_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Viewer Pose Location",
      "Last known location of the viewer pose (center between the eyes) in world space");

  prop = RNA_def_property(srna, "viewer_pose_rotation", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_viewer_pose_rotation_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Viewer Pose Rotation",
      "Last known rotation of the viewer pose (center between the eyes) in world space");

  prop = RNA_def_property(srna, "navigation_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_float_funcs(
      prop, "rna_XrSessionState_nav_location_get", "rna_XrSessionState_nav_location_set", NULL);
  RNA_def_property_ui_text(
      prop,
      "Navigation Location",
      "Location offset to apply to base pose when determining viewer location");

  prop = RNA_def_property(srna, "navigation_rotation", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_float_funcs(
      prop, "rna_XrSessionState_nav_rotation_get", "rna_XrSessionState_nav_rotation_set", NULL);
  RNA_def_property_ui_text(
      prop,
      "Navigation Rotation",
      "Rotation offset to apply to base pose when determining viewer rotation");

  prop = RNA_def_property(srna, "navigation_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(
      prop, "rna_XrSessionState_nav_scale_get", "rna_XrSessionState_nav_scale_set", NULL);
  RNA_def_property_ui_text(
      prop,
      "Navigation Scale",
      "Additional scale multiplier to apply to base scale when determining viewer scale");

  prop = RNA_def_property(srna, "actionmaps", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "XrActionMap");
  RNA_def_property_collection_funcs(prop,
                                    "rna_XrSessionState_actionmaps_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    "rna_XrSessionState_actionmaps_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_ui_text(prop, "XR Action Maps", "");
  rna_def_xr_actionmaps(brna, prop);

  prop = RNA_def_property(srna, "active_actionmap", PROP_INT, PROP_NONE);
  RNA_def_property_int_funcs(prop,
                             "rna_XrSessionState_active_actionmap_get",
                             "rna_XrSessionState_active_actionmap_set",
                             NULL);
  RNA_def_property_ui_text(prop, "Active Action Map", "");

  prop = RNA_def_property(srna, "selected_actionmap", PROP_INT, PROP_NONE);
  RNA_def_property_int_funcs(prop,
                             "rna_XrSessionState_selected_actionmap_get",
                             "rna_XrSessionState_selected_actionmap_set",
                             NULL);
  RNA_def_property_ui_text(prop, "Selected Action Map", "");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name XR Event Data
 * \{ */

static void rna_def_xr_eventdata(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "XrEventData", NULL);
  RNA_def_struct_ui_text(srna, "XrEventData", "XR Data for Window Manager Event");

  prop = RNA_def_property(srna, "action_set", PROP_STRING, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_string_funcs(
      prop, "rna_XrEventData_action_set_get", "rna_XrEventData_action_set_length", NULL);
  RNA_def_property_ui_text(prop, "Action Set", "XR action set name");

  prop = RNA_def_property(srna, "action", PROP_STRING, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_string_funcs(
      prop, "rna_XrEventData_action_get", "rna_XrEventData_action_length", NULL);
  RNA_def_property_ui_text(prop, "Action", "XR action name");

  prop = RNA_def_property(srna, "user_path", PROP_STRING, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_string_funcs(
      prop, "rna_XrEventData_user_path_get", "rna_XrEventData_user_path_length", NULL);
  RNA_def_property_ui_text(prop, "User Path", "User path of the action. E.g. \"/user/hand/left\"");

  prop = RNA_def_property(srna, "user_path_other", PROP_STRING, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_string_funcs(
      prop, "rna_XrEventData_user_path_other_get", "rna_XrEventData_user_path_other_length", NULL);
  RNA_def_property_ui_text(
      prop, "User Path Other", "Other user path, for bimanual actions. E.g. \"/user/hand/right\"");

  prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_enum_items(prop, rna_enum_xr_action_types);
  RNA_def_property_enum_funcs(prop, "rna_XrEventData_type_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Type", "XR action type");

  prop = RNA_def_property(srna, "state", PROP_FLOAT, PROP_NONE);
  RNA_def_property_array(prop, 2);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_XrEventData_state_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "State", "XR action values corresponding to type");

  prop = RNA_def_property(srna, "state_other", PROP_FLOAT, PROP_NONE);
  RNA_def_property_array(prop, 2);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_XrEventData_state_other_get", NULL, NULL);
  RNA_def_property_ui_text(
      prop, "State Other", "State of the other user path for bimanual actions");

  prop = RNA_def_property(srna, "float_threshold", PROP_FLOAT, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_XrEventData_float_threshold_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Float Threshold", "Input threshold for float/2D vector actions");

  prop = RNA_def_property(srna, "controller_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_XrEventData_controller_location_get", NULL, NULL);
  RNA_def_property_ui_text(prop,
                           "Controller Location",
                           "Location of the action's corresponding controller aim in world space");

  prop = RNA_def_property(srna, "controller_rotation", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_XrEventData_controller_rotation_get", NULL, NULL);
  RNA_def_property_ui_text(prop,
                           "Controller Rotation",
                           "Rotation of the action's corresponding controller aim in world space");

  prop = RNA_def_property(srna, "controller_location_other", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_XrEventData_controller_location_other_get", NULL, NULL);
  RNA_def_property_ui_text(prop,
                           "Controller Location Other",
                           "Controller aim location of the other user path for bimanual actions");

  prop = RNA_def_property(srna, "controller_rotation_other", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_XrEventData_controller_rotation_other_get", NULL, NULL);
  RNA_def_property_ui_text(prop,
                           "Controller Rotation Other",
                           "Controller aim rotation of the other user path for bimanual actions");

  prop = RNA_def_property(srna, "bimanual", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_boolean_funcs(prop, "rna_XrEventData_bimanual_get", NULL);
  RNA_def_property_ui_text(prop, "Bimanual", "Whether bimanual interaction is occurring");
}

/** \} */

void RNA_def_xr(BlenderRNA *brna)
{
  RNA_define_animate_sdna(false);

  rna_def_xr_actionmap(brna);
  rna_def_xr_session_settings(brna);
  rna_def_xr_session_state(brna);
  rna_def_xr_eventdata(brna);

  RNA_define_animate_sdna(true);
}

#endif /* RNA_RUNTIME */
