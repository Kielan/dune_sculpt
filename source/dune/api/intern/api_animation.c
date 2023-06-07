#include <stdlib.h>

#include "types_action.h"
#include "types_anim.h"
#include "types_scene.h"

#include "lib_utildefines.h"

#include "translation.h"

#include "mem_guardedalloc.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_types.h"

#include "ed_keyframing.h"

/* exported for use in API */
const EnumPropItem api_enum_keyingset_path_grouping_items[] = {
    {KSP_GROUP_NAMED, "NAMED", 0, "Named Group", ""},
    {KSP_GROUP_NONE, "NONE", 0, "None", ""},
    {KSP_GROUP_KSNAME, "KEYINGSET", 0, "Keying Set Name", ""},
    {0, NULL, 0, NULL, NULL},
};

/* It would be cool to get rid of this 'INSERTKEY_' prefix in 'py strings' values,
 * but it would break existing
 * exported keyingset... : */
const EnumPropItem api_enum_keying_flag_items[] = {
    {INSERTKEY_NEEDED,
     "INSERTKEY_NEEDED",
     0,
     "Only Needed",
     "Only insert keyframes where they're needed in the relevant F-Curves"},
    {INSERTKEY_MATRIX,
     "INSERTKEY_VISUAL",
     0,
     "Visual Keying",
     "Insert keyframes based on 'visual transforms'"},
    {INSERTKEY_XYZ2RGB,
     "INSERTKEY_XYZ_TO_RGB",
     0,
     "XYZ=RGB Colors",
     "Color for newly added transformation F-Curves (Location, Rotation, Scale) "
     "and also Color is based on the transform axis"},
    {0, NULL, 0, NULL, NULL},
};

/* Contains additional flags suitable for use in Python API functions. */
const EnumPropItem api_enum_keying_flag_items_api[] = {
    {INSERTKEY_NEEDE
     "INSERTKEY_NEEDED",
     0,
     "Only Needed",
     "Only insert keyframes where they're needed in the relevant F-Curves"},
    {INSERTKEY_MATRIX,
     "INSERTKEY_VISUAL",
     0,
     "Visual Keying",
     "Insert keyframes based on 'visual transforms'"},
    {INSERTKEY_XYZ2R
     "INSERTKEY_XYZ_TO_RGB",
     0,
     "XYZ=RGB Colors",
     "Color for newly added transformation F-Curves (Location, Rotation, Scale) "
     "and also Color is based on the transform axis"},
    {INSERTKEY_REPLACE,
     "INSERTKEY_REPLACE",
     0,
     "Replace Existing",
     "Only replace existing keyframes"},
    {INSERTKEY_AVAILABLE,
     "INSERTKEY_AVAILABLE",
     0,
     "Only Available",
     "Don't create F-Curves when they don't already exist"},
    {INSERTKEY_CYCLE_AWARE,
     "INSERTKEY_CYCLE_AWARE",
     0,
     "Cycle Aware Keying",
     "When inserting into a curve with cyclic extrapolation, remap the keyframe inside "
     "the cycle time range, and if changing an end key, also update the other one"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "lib_math_base.h"

#  include "dune_anim_data.h"
#  include "dune_animsys.h"
#  include "dune_fcurve.h"
#  include "dune_nla.h"

#  include "graph.h"
#  include "graph_build.h"

#  include "types_object_types.h"

#  include "ed_anim_api.h"

#  include "wm_api.h"

static void api_AnimData_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  anim_id_update(main, id);
}

static void api_AnimData_dependency_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  graph_relations_tag_update(main);

  api_AnimData_update(main, scene, ptr);
}

static int api_AnimData_action_editable(ApiPtr *ptr, const char **UNUSED(r_info))
{
  AnimData *adt = (AnimData *)ptr->data;
  return dune_animdata_action_editable(adt) ? PROP_EDITABLE : 0;
}

static void api_AnimData_action_set(ApiPtr *ptr,
                                    ApiPtr value,
                                    struct ReportList *UNUSED(reports))
{
  Id *ownerId = ptr->owner_id;

  /* set action */
  dune_animdata_set_action(NULL, ownerId, value.data);
}

static void api_AnimData_tweakmode_set(PointerRNA *ptr, const bool value)
{
  AnimData *adt = (AnimData *)ptr->data;

  /* NOTE: technically we should also set/unset SCE_NLA_EDIT_ON flag on the
   * scene which is used to make polling tests faster, but this flag is weak
   * and can easily break e.g. by changing layer visibility. This needs to be
   * dealt with at some point. */

  if (value) {
    dune_nla_tweakmode_enter(adt);
  }
  else {
    dune_nla_tweakmode_exit(adt);
  }
}

/* ****************************** */

/* wrapper for poll callback */
static bool RKS_POLL_rna_internal(KeyingSetInfo *ksi, bContext *C)
{
  extern ApiFn api_KeyingSetInfo_poll_func;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;
  void *ret;
  int ok;

  api_ptr_create(NULL, ksi->rna_ext.srna, ksi, &ptr);
  func = &api_KeyingSetInfo_poll_fn; /* RNA_struct_find_function(&ptr, "poll"); */

  api_param_list_create(&list, &ptr, fn);
  {
    /* hook up arguments */
    api_param_set_lookup(&list, "ksi", &ksi);
    api_param_set_lookup(&list, "context", &C);

    /* execute the function */
    ksi->api_ext.call(C, &ptr, func, &list);

    /* read the result */
    api_param_get_lookup(&list, "ok", &ret);
    ok = *(bool *)ret;
  }
  api_param_list_free(&list);

  return ok;
}

/* wrapper for iterator callback */
static void RKS_ITER_api_internal(KeyingSetInfo *ksi, Cxt *C, KeyingSet *ks)
{
  extern ApiFn api_KeyingSetInfo_iter_fn;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, ksi->api_ext.sapi, ksi, &ptr);
  fn = &api_KeyingSetInfo_iter_fn; /* api_struct_find_function(&ptr, "poll"); */

  api_param_list_create(&list, &ptr, fn);
  {
    /* hook up arguments */
    api_param_set_lookup(&list, "ksi", &ksi);
    api_param_set_lookup(&list, "context", &C);
    api_param_set_lookup(&list, "ks", &ks);

    /* execute the function */
    ksi->api_ext.call(C, &ptr, fn, &list);
  }
  api_param_list_free(&list);
}

/* wrapper for generator callback */
static void RKS_GEN_api_internal(KeyingSetInfo *ksi, Ctx *C, KeyingSet *ks, ApiPtr *data)
{
  extern ApiFn api_KeyingSetInfo_gen_fn;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, ksi->api_ext.srna, ksi, &ptr);
  fn = &api_KeyingSetInfo_gen_fb; /* api_struct_find_gen(&ptr, "poll"); */

  api_param_list_create(&list, &ptr, fn);
  {
    /* hook up arguments */
    api_param_set_lookup(&list, "ksi", &ksi);
    api_param_set_lookup(&list, "context", &C);
    api_param_set_lookup(&list, "ks", &ks);
    api_param_set_lookup(&list, "data", data);

    /* execute the function */
    ksi->api_ext.call(C, &ptr, fn, &list);
  }
  api_param_list_free(&list);
}

/* ------ */

/* XXX: the exact purpose of this is not too clear...
 * maybe we want to revise this at some point? */
static ApiStruct *api_KeyingSetInfo_refine(ApiPtr *ptr)
{
  KeyingSetInfo *ksi = (KeyingSetInfo *)ptr->data;
  return (ksi->api_ext.srna) ? ksi->api_ext.srna : &ApiKeyingSetInfo;
}

static void api_KeyingSetInfo_unregister(Main *bmain, StructRNA *type)
{
  KeyingSetInfo *ksi = api_struct_dune_type_get(type);

  if (ksi == NULL) {
    return;
  }

  /* free RNA data referencing this */
  api_struct_free_extension(type, &ksi->rna_ext);
  api_struct_free(&DUNE_API, type);

  wm_main_add_notifier(NC_WINDOW, NULL);

  /* unlink Blender-side data */
  anim_keyingset_info_unregister(bmain, ksi);
}

static ApiStruct *api_KeyingSetInfo_register(Main *bmain,
                                             ReportList *reports,
                                             void *data,
                                             const char *id,
                                             StructValidateFn validate,
                                             StructCallbackFn call,
                                             StructFreeFn free)
{
  KeyingSetInfo dummyksi = {NULL};
  KeyingSetInfo *ksi;
  ApiPtr dummyptr = {NULL};
  int have_function[3];

  /* setup dummy type info to store static properties in */
  /* TODO: perhaps we want to get users to register
   * as if they're using 'KeyingSet' directly instead? */
  api_ptr_create(NULL, &ApiKeyingSetInfo, &dummyksi, &dummyptr);

  /* validate the python class */
  if (validate(&dummyptr, data, have_fn) != 0) {
    return NULL;
  }

  if (strlen(id) >= sizeof(dummyksi.idname)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Registering keying set info class: '%s' is too long, maximum length is %d",
                id,
                (int)sizeof(dummyksi.idname));
    return NULL;
  }

  /* check if we have registered this info before, and remove it */
  ksi = anim_keyingset_info_find_name(dummyksi.idname);
  if (ksi && ksi->api_ext.srna) {
    api_KeyingSetInfo_unregister(bmain, ksi->api_ext.srna);
  }

  /* create a new KeyingSetInfo type */
  ksi = mem_mallocn(sizeof(KeyingSetInfo), "python keying set info");
  memcpy(ksi, &dummyksi, sizeof(KeyingSetInfo));

  /* set RNA-extensions info */
  ksi->api_ext.sapi = api_def_struct_ptr(&DUNE_API, ksi->idname, &ApiKeyingSetInfo);
  ksi->api_ext.data = data;
  ksi->api_ext.call = call;
  ksi->api_ext.free = free;
  api_struct_dune_type_set(ksi->api_ext.srna, ksi);

  /* set callbacks */
  /* NOTE: we really should have all of these... */
  ksi->poll = (have_fn[0]) ? RKS_POLL_api_internal : NULL;
  ksi->iter = (have_fn[1]) ? RKS_ITER_api_internal : NULL;
  ksi->generate = (have_fn[2]) ? RKS_GEN_api_internal : NULL;

  /* add and register with other info as needed */
  anim_keyingset_info_register(ksi);

  wm_main_add_notifier(NC_WINDOW, NULL);

  /* return the struct-rna added */
  return ksi->api_ext.sapi;
}

/* ****************************** */

static ApiStruct *api_ksPath_id_typef(ApiPtr *ptr)
{
  KS_Path *ksp = (KS_Path *)ptr->data;
  return id_code_to_api_type(ksp->idtype);
}

static int api_ksPath_id_editable(ApiPtr *ptr, const char **UNUSED(r_info))
{
  KS_Path *ksp = (KS_Path *)ptr->data;
  return (ksp->idtype) ? PROP_EDITABLE : 0;
}

static void api_ksPath_id_type_set(ApiPtr *ptr, int value)
{
  KS_Path *data = (KS_Path *)(ptr->data);

  /* set the driver type, then clear the id-block if the type is invalid */
  data->idtype = value;
  if ((data->id) && (GS(data->id->name) != data->idtype)) {
    data->id = NULL;
  }
}

static void api_ksPath_ApiPath_get(ApiPtr *ptr, char *value)
{
  KS_Path *ksp = (KS_Path *)ptr->data;

  if (ksp->api_path) {
    strcpy(value, ksp->api_path);
  }
  else {
    value[0] = '\0';
  }
}
static int api_ksPath_ApiPath_length(ApiPtr *ptr)
{
  KS_Path *ksp = (KS_Path *)ptr->data;

  if (ksp->api_path) {
    return strlen(ksp->api_path);
  }
  else {
    return 0;
  }
}

static void api_ksPath_ApiPath_set(ApiPtr *ptr, const char *value)
{
  KS_Path *ksp = (KS_Path *)ptr->data;

  if (ksp->api_path) {
    mem_freen(ksp->api_path);
  }

  if (value[0]) {
    ksp->api_path = lib_strdup(value);
  }
  else {
    ksp->api_path = NULL;
  }
}

/* ****************************** */

static void api_KeyingSet_name_set(ApiPtr *ptr, const char *value)
{
  KeyingSet *ks = (KeyingSet *)ptr->data;

  /* update names of corresponding groups if name changes */
  if (!STREQ(ks->name, value)) {
    KS_Path *ksp;

    for (ksp = ks->paths.first; ksp; ksp = ksp->next) {
      if ((ksp->groupmode == KSP_GROUP_KSNAME) && (ksp->id)) {
        AnimData *adt = dune_animdata_from_id(ksp->id);

        /* TODO: NLA strips? */
        if (adt && adt->action) {
          ActionGroup *agrp;

          /* lazy check - should really find the F-Curve for the affected path and check its group
           * but this way should be faster and work well for most cases, as long as there are no
           * conflicts */
          for (agrp = adt->action->groups.first; agrp; agrp = agrp->next) {
            if (STREQ(ks->name, agrp->name)) {
              /* there should only be one of these in the action, so can stop... */
              lib_strncpy(agrp->name, value, sizeof(agrp->name));
              break;
            }
          }
        }
      }
    }
  }

  /* finally, update name to new value */
  lib_strncpy(ks->name, value, sizeof(ks->name));
}

static int api_KeyingSet_active_ksPath_editable(ApiPtr *ptr, const char **UNUSED(r_info))
{
  KeyingSet *ks = (KeyingSet *)ptr->data;

  /* only editable if there are some paths to change to */
  return (lib_list_is_empty(&ks->paths) == false) ? PROP_EDITABLE : 0;
}

static ApiPtr api_KeyingSet_active_ksPath_get(ApiPtr *ptr)
{
  KeyingSet *ks = (KeyingSet *)ptr->data;
  return api_ptr_inherit_refine(
      ptr, &ApiKeyingSetPath, lib_findlink(&ks->paths, ks->active_path - 1));
}

static void api_KeyingSet_active_ksPath_set(ApiPtr *ptr,
                                            ApiPtr value,
                                            struct ReportList *UNUSED(reports))
{
  KeyingSet *ks = (KeyingSet *)ptr->data;
  KS_Path *ksp = (KS_Path *)value.data;
  ks->active_path = lib_findindex(&ks->paths, ksp) + 1;
}

static int api_KeyingSet_active_ksPath_index_get(ApiPtr *ptr)
{
  KeyingSet *ks = (KeyingSet *)ptr->data;
  return MAX2(ks->active_path - 1, 0);
}

static void api_KeyingSet_active_ksPath_index_set(ApiPtr *ptr, int value)
{
  KeyingSet *ks = (KeyingSet *)ptr->data;
  ks->active_path = value + 1;
}

static void api_KeyingSet_active_ksPath_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  KeyingSet *ks = (KeyingSet *)ptr->data;

  *min = 0;
  *max = max_ii(0, lib_list_count(&ks->paths) - 1);
}

static ApiPtr api_KeyingSet_typeinfo_get(ApiPtr *ptr)
{
  KeyingSet *ks = (KeyingSet *)ptr->data;
  KeyingSetInfo *ksi = NULL;

  /* keying set info is only for builtin Keying Sets */
  if ((ks->flag & KEYINGSET_ABSOLUTE) == 0) {
    ksi = ANIM_keyingset_info_find_name(ks->typeinfo);
  }
  return api_ptr_inherit_refine(ptr, &ApiKeyingSetInfo, ksi);
}

static KS_Path *api_KeyingSet_paths_add(KeyingSet *keyingset,
                                        ReportList *reports,
                                        Id *id,
                                        const char api_path[],
                                        int index,
                                        int group_method,
                                        const char group_name[])
{
  KS_Path *ksp = NULL;
  short flag = 0;

  /* Special case when index = -1, we key the whole array
   * (as with other places where index is used). */
  if (index == -1) {
    flag |= KSP_FLAG_WHOLE_ARRAY;
    index = 0;
  }

  /* if data is valid, call the API function for this */
  if (keyingset) {
    ksp = dune_keyingset_add_path(keyingset, id, group_name, api_path, index, flag, group_method);
    keyingset->active_path = lib_list_count(&keyingset->paths);
  }
  else {
    dune_report(reports, RPT_ERROR, "Keying set path could not be added");
  }

  /* return added path */
  return ksp;
}

static void api_KeyingSet_paths_remove(KeyingSet *keyingset,
                                       ReportList *reports,
                                       ApiPtr *ksp_ptr)
{
  KS_Path *ksp = ksp_ptr->data;

  /* if data is valid, call the API function for this */
  if ((keyingset && ksp) == false) {
    dune_report(reports, RPT_ERROR, "Keying set path could not be removed");
    return;
  }

  /* remove the active path from the KeyingSet */
  dune_keyingset_free_path(keyingset, ksp);
  API_PTR_INVALIDATE(ksp_ptr);

  /* the active path number will most likely have changed */
  /* TODO: we should get more fancy and actually check if it was removed,
   * but this will do for now */
  keyingset->active_path = 0;
}

static void api_KeyingSet_paths_clear(KeyingSet *keyingset, ReportList *reports)
{
  /* if data is valid, call the API function for this */
  if (keyingset) {
    KS_Path *ksp, *kspn;

    /* free each path as we go to avoid looping twice */
    for (ksp = keyingset->paths.first; ksp; ksp = kspn) {
      kspn = ksp->next;
      dune_keyingset_free_path(keyingset, ksp);
    }

    /* reset the active path, since there aren't any left */
    keyingset->active_path = 0;
  }
  else {
    dune_report(reports, RPT_ERROR, "Keying set paths could not be removed");
  }
}

/* needs wrapper function to push notifier */
static NlaTrack *api_NlaTrack_new(Id *id, AnimData *adt, Main *main, Cxt *C, NlaTrack *track)
{
  NlaTrack *new_track = dune_nlatrack_add(adt, track, ID_IS_OVERRIDE_LIB(id));

  wm_event_add_notifier(C, NC_ANIMATION | ND_NLA | NA_ADDED, NULL);

  graph_relations_tag_update(main);
  graph_id_tag_update_ex(main, id, ID_RECALC_ANIMATION | ID_RECALC_COPY_ON_WRITE);

  return new_track;
}

static void api_NlaTrack_remove(
    Id *id, AnimData *adt, Main *main, Cxt *C, ReportList *reports, ApiPtr *track_ptr)
{
  NlaTrack *track = track_ptr->data;

  if (lib_findindex(&adt->nla_tracks, track) == -1) {
    dune_reportf(reports, RPT_ERROR, "NlaTrack '%s' cannot be removed", track->name);
    return;
  }

  dune_nlatrack_free(&adt->nla_tracks, track, true);
  API_PTR_INVALIDATE(track_ptr);

  wm_event_add_notifier(C, NC_ANIMATION | ND_NLA | NA_REMOVED, NULL);

  graph_relations_tag_update(main);
  graph_id_tag_update_ex(main, id, ID_RECALC_ANIMATION | ID_RECALC_COPY_ON_WRITE);
}

static ApiPtr api_NlaTrack_active_get(ApiPtr *ptr)
{
  AnimData *adt = (AnimData *)ptr->data;
  NlaTrack *track = dune_nlatrack_find_active(&adt->nla_tracks);
  return api_ptr_inherit_refine(ptr, &ApiNlaTrack, track);
}

static void api_NlaTrack_active_set(ApiPtr *ptr,
                                    ApiPtr value,
                                    struct ReportList *UNUSED(reports))
{
  AnimData *adt = (AnimData *)ptr->data;
  NlaTrack *track = (NlaTrack *)value.data;
  dune_nlatrack_set_active(&adt->nla_tracks, track);
}

static FCurve *api_Driver_from_existing(AnimData *adt, Cxt *C, FCurve *src_driver)
{
  /* verify that we've got a driver to duplicate */
  if (ELEM(NULL, src_driver, src_driver->driver)) {
    dune_report(cxt_wm_reports(C), RPT_ERROR, "No valid driver data to create copy of");
    return NULL;
  }
  else {
    /* just make a copy of the existing one and add to self */
    FCurve *new_fcu = dunr_fcurve_copy(src_driver);

    /* XXX: if we impose any ordering on these someday, this will be problematic */
    lib_addtail(&adt->drivers, new_fcu);
    return new_fcu;
  }
}

static FCurve *rna_Driver_new(
    Id *id, AnimData *adt, Main *main, ReportList *reports, const char *api_path, int array_index)
{
  if (api_path[0] == '\0') {
    dune_report(reports, RPT_ERROR, "F-Curve data path empty, invalid argument");
    return NULL;
  }

  if (dune_fcurve_find(&adt->drivers, api_path, array_index)) {
    dune_reportf(reports, RPT_ERROR, "Driver '%s[%d]' already exists", api_path, array_index);
    return NULL;
  }

  FCurve *fcu = verify_driver_fcurve(id, api_path, array_index, DRIVER_FCURVE_KEYFRAMES);
  lib_assert(fcu != NULL);

  graph_relations_tag_update(main);

  return fcu;
}

static void api_Driver_remove(AnimData *adt, Main *main, ReportList *reports, FCurve *fcu)
{
  if (!lib_remlink_safe(&adt->drivers, fcu)) {
    dune_report(reports, RPT_ERROR, "Driver not found in this animation data");
    return;
  }
  dune_fcurve_free(fcu);
  graph_relations_tag_update(main);
}

static FCurve *api_Driver_find(AnimData *adt,
                               ReportList *reports,
                               const char *data_path,
                               int index)
{
  if (data_path[0] == '\0') {
    dune_report(reports, RPT_ERROR, "F-Curve data path empty, invalid argument");
    return NULL;
  }

  /* Returns NULL if not found. */
  return dune_fcurve_find(&adt->drivers, data_path, index);
}

bool api_AnimaData_override_apply(Main *UNUSED(main),
                                  ApiPtr *ptr_dst,
                                  ApiPtr *ptr_src,
                                  ApiPtr *ptr_storage,
                                  ApiProp *prop_dst,
                                  ApiProp *prop_src,
                                  ApiProp *UNUSED(prop_storage),
                                  const int len_dst,
                                  const int len_src,
                                  const int len_storage,
                                  ApiPtr *UNUSED(ptr_item_dst),
                                  ApiPtr *UNUSED(ptr_item_src),
                                  ApiPtr *UNUSED(ptr_item_storage),
                                  IdOverrideLibPropOp *opop)
{
  lib_assert(len_dst == len_src && (!ptr_storage || len_dst == len_storage) && len_dst == 0);
  lib_assert(opop->op == IDOVERRIDE_LIB_OP_REPLACE &&
             "Unsupported api override operation on animdata pointer");
  UNUSED_VARS_NDEBUG(ptr_storage, len_dst, len_src, len_storage, opop);

  /* AnimData is a special case, since you cannot edit/replace it, it's either existent or not. */
  AnimData *adt_dst = api_prop_ptr_get(ptr_dst, prop_dst).data;
  AnimData *adt_src = api_prop_ptr_get(ptr_src, prop_src).data;

  if (adt_dst == NULL && adt_src != NULL) {
    /* Copy anim data from reference into final local ID. */
    dune_animdata_copy_id(NULL, ptr_dst->owner_id, ptr_src->owner_id, 0);
    return true;
  }
  else if (adt_dst != NULL && adt_src == NULL) {
    /* Override has cleared/removed anim data from its reference. */
    dune_animdata_free(ptr_dst->owner_id, true);
    return true;
  }

  return false;
}

bool api_NLA_tracks_override_apply(Main *main,
                                   ApiPtr *ptr_dst,
                                   ApiPtr *ptr_src,
                                   ApiPtr *UNUSED(ptr_storage),
                                   ApiProp *UNUSED(prop_dst),
                                   ApiProp *UNUSED(prop_src),
                                   ApiProp *UNUSED(prop_storage),
                                   const int UNUSED(len_dst),
                                   const int UNUSED(len_src),
                                   const int UNUSED(len_storage),
                                   ApiPtr *UNUSED(ptr_item_dst),
                                   ApiPtr *UNUSED(ptr_item_src),
                                   ApiPtr *UNUSED(ptr_item_storage),
                                   IdOverrideLibPropOp *opop)
{
  lib_assert(opop->op == IDOVERRIDE_LIB_OP_INSERT_AFTER &&
             "Unsupported api override operation on constraints collection");

  AnimData *anim_data_dst = (AnimData *)ptr_dst->data;
  AnimData *anim_data_src = (AnimData *)ptr_src->data;

  /* Remember that insertion operations are defined and stored in correct order, which means that
   * even if we insert several items in a row, we always insert first one, then second one, etc.
   * So we should always find 'anchor' track in both _src *and* _dst. */
  NlaTrack *nla_track_anchor = NULL;
#  if 0
  /* This is not working so well with index-based insertion, especially in case some tracks get
   * added to lib linked data. So we simply add locale tracks at the end of the list always, order
   * of override operations should ensure order of local tracks is preserved properly. */
  if (opop->subitem_ref_index >= 0) {
    nla_track_anchor = lib_findlink(&anim_data_dst->nla_tracks, opop->subitem_ref_index);
  }
  /* Otherwise we just insert in first position. */
#  else
  nla_track_anchor = anim_data_dst->nla_tracks.last;
#  endif

  NlaTrack *nla_track_src = NULL;
  if (opop->subitem_local_index >= 0) {
    nla_track_src = lib_findlink(&anim_data_src->nla_tracks, opop->subitem_local_index);
  }

  if (nla_track_src == NULL) {
    lib_assert(nla_track_src != NULL);
    return false;
  }

  NlaTrack *nla_track_dst = dune_nlatrack_copy(main, nla_track_src, true, 0);

  /* This handles NULL anchor as expected by adding at head of list. */
  lib_insertlinkafter(&anim_data_dst->nla_tracks, nla_track_anchor, nla_track_dst);

  // printf("%s: We inserted a NLA Track...\n", __func__);
  return true;
}

#else

/* helper function for Keying Set -> keying settings */
static void api_def_common_keying_flags(ApiStruct *sapi, short reg)
{
  ApiProp *prop;

  /* override scene/userpref defaults? */
  prop = api_def_prop(sapi, "use_insertkey_override_needed", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "keyingoverride", INSERTKEY_NEEDED);
  api_def_prop_ui_text(prop,
                       "Override Insert Keyframes Default- Only Needed",
                       "Override default setting to only insert keyframes where they're "
                       "needed in the relevant F-Curves");
  if (reg) {
    api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  }

  prop = api_def_prop(sapi, "use_insertkey_override_visual", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "keyingoverride", INSERTKEY_MATRIX);
  api_def_prop_ui_text(
      prop,
      "Override Insert Keyframes Default - Visual",
      "Override default setting to insert keyframes based on 'visual transforms'");
  if (reg) {
    api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  }

  prop = apk_def_prop(sapi, "use_insertkey_override_xyz_to_rgb", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "keyingoverride", INSERTKEY_XYZ2RGB);
  api_def_prop_ui_text(
      prop,
      "Override F-Curve Colors - XYZ to RGB",
      "Override default setting to set color for newly added transformation F-Curves "
      "(Location, Rotation, Scale) to be based on the transform axis");
  if (reg) {
    api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  }

  /* value to override defaults with */
  prop = api_def_prop(sapi, "use_insertkey_needed", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "keyingflag", INSERTKEY_NEEDED);
  api_def_prop_ui_text(prop,
                       "Insert Keyframes - Only Needed",
                       "Only insert keyframes where they're needed in the relevant F-Curves");
  if (reg) {
    api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  }

  prop = api_def_prop(sapi, "use_insertkey_visual", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "keyingflag", INSERTKEY_MATRIX);
  api_def_prop_ui_text(
      prop, "Insert Keyframes - Visual", "Insert keyframes based on 'visual transforms'");
  if (reg) {
    api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  }

  prop = api_def_prop(sapi, "use_insertkey_xyz_to_rgb", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stypr(prop, NULL, "keyingflag", INSERTKEY_XYZ2RGB);
  api_def_prop_ui_text(prop,
                      "F-Curve Colors - XYZ to RGB",
                      "Color for newly added transformation F-Curves (Location, Rotation, "
                      "Scale) is based on the transform axis");
  if (reg) {
    api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  }
}

/* --- */

/* To avoid repeating it twice! */
#  define KEYINGSET_IDNAME_DOC \
    "If this is set, the Keying Set gets a custom ID, otherwise it takes " \
    "the name of the class used to define the Keying Set (for example, " \
    "if the class name is \"BUILTIN_KSI_location\", and bl_idname is not " \
    "set by the script, then bl_idname = \"BUILTIN_KSI_location\")"

static void api_def_keyingset_info(BlenderRNA *brna)
{
  ApiStruct *sapi;
  ApiProp *prop;
  ApiFn *fn;
  ApiProp *parm;

  srna = api_def_struct(dapi, "KeyingSetInfo", NULL);
  api_def_struct_stype(sapi, "KeyingSetInfo");
  api_def_struct_ui_text(
      sapi, "Keying Set Info", "Callback function defines for builtin Keying Sets");
  api_def_struct_refine_fn(sapi, "api_KeyingSetInfo_refine");
  api_def_struct_register_fns(
      sapi, "api_KeyingSetInfo_register", "api_KeyingSetInfo_unregister", NULL);

  /* Properties --------------------- */

  api_define_verify_stype(0); /* not in sdna */

  prop = api_def_prop(sapi, "bl_idname", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "idname");
  api_def_prop_flag(prop, PROP_REGISTER);
  api_def_prop_ui_text(prop, "Id Name", KEYINGSET_IDNAME_DOC);

  prop = api_def_prop(sapi, "bl_label", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "name");
  api_def_prop_ui_text(prop, "UI Name", "");
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_flag(prop, PROP_REGISTER);

  prop = api_def_prop(sapi, "bl_description", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "description");
  api_def_prop_string_maxlength(prop, RNA_DYN_DESCR_MAX); /* else it uses the pointer size! */
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(prop, "Description", "A short description of the keying set");

  /* Regarding why we don't use rna_def_common_keying_flags() here:
   * - Using it would keep this case in sync with the other places
   *   where these options are exposed (which are optimized for being
   *   used in the UI).
   * - Unlike all the other places, this case is used for defining
   *   new "built in" Keying Sets via the Python API. In that case,
   *   it makes more sense to expose these in a way more similar to
   *   other places featuring bl_idname/label/description (i.e. operators)
   */
  prop = api_def_prop(sapi, "bl_options", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "keyingflag");
  api_def_prop_enum_items(prop, api_enum_keying_flag_items);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL | PROP_ENUM_FLAG);
  api_def_prop_ui_text(prop, "Options", "Keying Set options to use when inserting keyframes");

  api_define_verify_stype(1);

  /* Function Callbacks ------------- */
  /* poll */
  fn = api_def_fn(sapi, "poll", NULL);
  api_def_fn_ui_description(fn, "Test if Keying Set can be used or not");
  api_def_fn_flag(fn, FN_REGISTER);
  api_def_fn_return(fn, ali_def_bool(fn, "ok", 1, "", ""));
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* iterator */
  fn = api_def_fn(sapi, "iterator", NULL);
  api_def_fn_ui_description(
      fn, "Call generate() on the structs which have properties to be keyframed");
  api_def_fn_flag(fn, FN_REGISTER);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "ks", "KeyingSet", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* generate */
  fn = api_def_fn(sapi, "generate", NULL);
  api_def_fn_ui_description(
      fn, "Add Paths to the Keying Set to keyframe the properties of the given data");
  api_def_fn_flag(fn, FN_REGISTER);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "ks", "KeyingSet", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "data", "AnyType", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
}

static void api_def_keyingset_path(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "KeyingSetPath", NULL);
  api_def_struct_stype(sapi, "KS_Path");
  api_def_struct_ui_text(sapi, "Keying Set Path", "Path to a setting for use in a Keying Set");

  /* ID */
  prop = api_def_prop(sapi, "id", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "ID");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_editable_fn(prop, "rna_ksPath_id_editable");
  api_def_prop_ptr_fns(prop, NULL, NULL, "rna_ksPath_id_typef", NULL);
  api_def_prop_ui_text(prop,
                       "ID-Block",
                       "ID-Block that keyframes for Keying Set should be added to "
                       "(for Absolute Keying Sets only)");
  api_def_prop_update(
      prop, NC_SCENE | ND_KEYINGSET | NA_EDITED, NULL); /* XXX: maybe a bit too noisy */

  prop = api_def_prop(sapi, "id_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "idtype");
  api_def_prop_enum_items(prop, api_enum_id_type_items);
  api_def_prop_enum_default(prop, ID_OB);
  api_def_prop_enum_fns(prop, NULL, "rna_ksPath_id_type_set", NULL);
  api_def_prop_ui_text(prop, "ID Type", "Type of ID-block that can be used");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID);
  api_def_prop_update(
      prop, NC_SCENE | ND_KEYINGSET | NA_EDITED, NULL); /* XXX: maybe a bit too noisy */

  /* Group */
  prop = api_def_prop(sapi, "group", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(
      prop, "Group Name", "Name of Action Group to assign setting(s) for this path to");
  api_def_prop_update(
      prop, NC_SCENE | ND_KEYINGSET | NA_EDITED, NULL); /* XXX: maybe a bit too noisy */

  /* Grouping */
  prop = api_def_prop(sapi, "group_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "groupmode");
  api_def_prop_enum_items(prop, api_enum_keyingset_path_grouping_items);
  api_def_prop_ui_text(
      prop, "Grouping Method", "Method used to define which Group-name to use");
  api_def_prop_update(
      prop, NC_SCENE | ND_KEYINGSET | NA_EDITED, NULL); /* XXX: maybe a bit too noisy */

  /* Path + Array Index */
  prop = api_def_prop(sapi, "data_path", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(
      prop, "api_ksPath_RnaPath_get", "api_ksPath_RnaPath_length", "api_ksPath_ApiPath_set");
  api_def_prop_ui_text(prop, "Data Path", "Path to property setting");
  api_def_struct_name_prop(sapi, prop); /* XXX this is the best indicator for now... */
  api_def_prop_update(prop, NC_SCENE | ND_KEYINGSET | NA_EDITED, NULL);

  /* called 'index' when given as function arg */
  prop = api_def_prop(sapi, "array_index", PROP_INT, PROP_NONE);
  api_def_prop_ui_text(prop, "Api Array Index", "Index to the specific setting if applicable");
  api_def_prop_update(
      prop, NC_SCENE | ND_KEYINGSET | NA_EDITED, NULL); /* XXX: maybe a bit too noisy */

  /* Flags */
  prop = api_def_prop(sapi, "use_entire_array", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", KSP_FLAG_WHOLE_ARRAY);
  api_def_prop_ui_text(
      prop,
      "Entire Array",
      "When an 'array/vector' type is chosen (Location, Rotation, Color, etc.), "
      "entire array is to be used");
  api_def_prop_update(
      prop, NC_SCENE | ND_KEYINGSET | NA_EDITED, NULL); /* XXX: maybe a bit too noisy */

  /* Keyframing Settings */
  api_def_common_keying_flags(srna, 0);
}

/* keyingset.paths */
static void api_def_keyingset_paths(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  ApiFn *fn;
  ApiProp *parm;

  ApiProp *prop;

  api_def_prop_sapi(cprop, "KeyingSetPaths");
  sapi = api_def_struct(dapi, "KeyingSetPaths", NULL);
  api_def_struct_stype(sapi, "KeyingSet");
  api_def_struct_ui_text(sapi, "Keying set paths", "Collection of keying set paths");

  /* Add Path */
  fn = api_def_fn(sapi, "add", "api_KeyingSet_paths_add");
  api_def_fn_ui_description(fn, "Add a new path for the Keying Set");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  /* return arg */
  parm = api_def_ptr(
      fn, "ksp", "KeyingSetPath", "New Path", "Path created and added to the Keying Set");
  api_def_fn_return(fn, parm);
  /* ID-block for target */
  parm = api_def_ptr(
      fn, "target_id", "ID", "Target ID", "ID data-block for the destination");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* rna-path */
  /* XXX hopefully this is long enough */
  parm = api_def_string(
      fn, "data_path", NULL, 256, "Data-Path", "Api-Path to destination property");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* index (defaults to -1 for entire array) */
  api_def_int(fn,
              "index",
              -1,
              -1,
              INT_MAX,
              "Index",
              "The index of the destination property (i.e. axis of Location/Rotation/etc.), "
              "or -1 for the entire array",
              0,
              INT_MAX);
  /* grouping */
  api_def_enum(fn,
               "group_method",
               api_enum_keyingset_path_grouping_items,
               KSP_GROUP_KSNAME,
               "Grouping Method",
               "Method used to define which Group-name to use");
  api_def_string(
      fn,
      "group_name",
      NULL,
      64,
      "Group Name",
      "Name of Action Group to assign destination to (only if grouping mode is to use this name)");

  /* Remove Path */
  fn = api_def_fn(sapi, "remove", "rna_KeyingSet_paths_remove");
  api_def_fn_ui_description(fn, "Remove the given path from the Keying Set");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  /* path to remove */
  parm = api_def_ptr(fn, "path", "KeyingSetPath", "Path", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  /* Remove All Paths */
  fn = api_def_fn(sapi, "clear", "api_KeyingSet_paths_clear");
  api_def_fn_ui_description(fn, "Remove all the paths from the Keying Set");
  api_def_fn_flag(fn, FN_USE_REPORTS);

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "KeyingSetPath");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_editable_fn(prop, "api_KeyingSet_active_ksPath_editable");
  api_def_prop_ptr_fns(
      prop, "api_KeyingSet_active_ksPath_get", "api_KeyingSet_active_ksPath_set", NULL, NULL);
  api_def_prop_ui_text(
      prop, "Active Keying Set", "Active Keying Set used to insert/delete keyframes");

  prop = api_def_prop(sapi, "active_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "active_path");
  api_def_prop_int_fn(prop,
                      "api_KeyingSet_active_ksPath_index_get",
                      "api_KeyingSet_active_ksPath_index_set",
                      "api_KeyingSet_active_ksPath_index_range");
  api_def_prop_ui_text(prop, "Active Path Index", "Current Keying Set index");
}

static void api_def_keyingset(BlenderRNA *brna)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "KeyingSet", NULL);
  api_def_struct_ui_text(sapi, "Keying Set", "Settings that should be keyframed together");

  /* Id/Label */
  prop = api_def_prop(sapi, "bl_idname", PROP_STRING, PROP_NONE);
  api_def_prop_string_style(prop, NULL, "idname");
  api_def_prop_flag(prop, PROP_REGISTER);
  api_def_prop_ui_text(prop, "ID Name", KEYINGSET_IDNAME_DOC);
  /* NOTE: disabled, as ID name shouldn't be editable */
#  if 0
  api_def_prop_update(prop, NC_SCENE | ND_KEYINGSET | NA_RENAME, NULL);
#  endif

  prop = api_def_prop(sapi, "bl_label", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "name");
  api_def_prop_string_fns(prop, NULL, NULL, "rna_KeyingSet_name_set");
  api_def_prop_ui_text(prop, "UI Name", "");
  api_def_struct_ui_icon(sapi, ICON_KEYINGSET);
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_update(prop, NC_SCENE | ND_KEYINGSET | NA_RENAME, NULL);

  prop = api_def_prop(sapi, "bl_description", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "description");
  api_def_prop_string_maxlength(prop, API_DYN_DESCR_MAX); /* else it uses the pointer size! */
  api_def_prop_ui_text(prop, "Description", "A short description of the keying set");

  /* KeyingSetInfo (Type Info) for Builtin Sets only. */
  prop = api_def_prop(sapi, "type_info", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "KeyingSetInfo");
  api_def_prop_ptr_fns(prop, "api_KeyingSet_typeinfo_get", NULL, NULL, NULL);
  api_def_prop_ui_text(
      prop, "Type Info", "Callback function defines for built-in Keying Sets");

  /* Paths */
  prop = api_def_prop(sapi, "paths", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "paths", NULL);
  api_def_prop_struct_type(prop, "KeyingSetPath");
  api_def_prop_ui_text(
      prop, "Paths", "Keying Set Paths to define settings that get keyframed together");
  api_def_keyingset_paths(dapi, prop);

  /* Flags */
  prop = api_def_prop(sapi, "is_path_absolute", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", KEYINGSET_ABSOLUTE);
  api_def_prop_ui_text(prop,
                       "Absolute",
                       "Keying Set defines specific paths/settings to be keyframed "
                       "(i.e. is not reliant on context info)");

  /* Keyframing Flags */
  api_def_common_keying_flags(sapi, 0);

  /* Keying Set API */
  api_api_keyingset(sapi);
}

#  undef KEYINGSET_IDNAME_DOC
/* --- */

static void api_animdata_nla_tracks(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *parm;
  ApiFn *fn;

  ApiProp *prop;

  api_def_prop_sapi(cprop, "NlaTracks");
  sapi = api_def_struct(dapi, "NlaTracks", NULL);
  api_def_struct_stype(sapi, "AnimData");
  api_def_struct_ui_text(sapi, "NLA Tracks", "Collection of NLA Tracks");

  fn = api_def_fn(sapi, "new", "api_NlaTrack_new");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_MAIN | FN_USE_CXT);
  api_def_fn_ui_description(fn, "Add a new NLA Track");
  api_def_ptr(fn, "prev", "NlaTrack", "", "NLA Track to add the new one after");
  /* return type */
  parm = api_def_ptr(fn, "track", "NlaTrack", "", "New NLA Track");
  api_def_fn_return(fn, parm);

  fn = api_def_function(sapi, "remove", "api_NlaTrack_remove");
  api_def_fn_flag(fn,
                  FN_USE_SELF_ID | FN_USE_REPORTS | FN_USE_MAIN | FN_USE_CXT);
  api_def_fn_ui_description(fn, "Remove a NLA Track");
  parm = api_def_ptr(fn, "track", "NlaTrack", "", "NLA Track to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "NlaTrack");
  api_def_prop_ptr_fns(
      prop, "api_NlaTrack_active_get", "api_NlaTrack_active_set", NULL, NULL);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Active Track", "Active NLA Track");
  /* XXX: should (but doesn't) update the active track in the NLA window */
  api_def_prop_update(prop, NC_ANIMATION | ND_NLA | NA_SELECTED, NULL);
}

static void api_animdata_drivers(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *parm;
  ApiFn *fn;

  /* ApiProp *prop; */
  api_def_prop_sapi(cprop, "AnimDataDrivers");
  srna = api_def_struct(dapi, "AnimDataDrivers", NULL);
  api_def_struct_style(sapi, "AnimData");
  api_def_struct_ui_text(sapi, "Drivers", "Collection of Driver F-Curves");

  /* Match: ActionFCurves.new/remove */

  /* AnimData.drivers.new(...) */
  fn = api_def_fn(sapi, "new", "api_Driver_new");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_REPORTS | FN_USE_MAIN);
  parm = api_def_string(fn, "data_path", NULL, 0, "Data Path", "F-Curve data path to use");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_int(fn, "index", 0, 0, INT_MAX, "Index", "Array index", 0, INT_MAX);
  /* return type */
  parm = api_def_ptr(fn, "driver", "FCurve", "", "Newly Driver F-Curve");
  api_def_fn_return(fn, parm);

  /* AnimData.drivers.remove(...) */
  fn = api_def_fn(sapi, "remove", "api_Driver_remove");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_MAIN);
  parm = api_def_ptr(fn, "driver", "FCurve", "", "");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* AnimData.drivers.from_existing(...) */
  fn = api_def_fn(sapi, "from_existing", "rna_Driver_from_existing");
  api_def_fn_flag(fn, FN_USE_CXT);
  api_def_fn_ui_description(fn, "Add a new driver given an existing one");
  api_def_ptr(fn,
              "src_driver",
              "FCurve",
              "",
              "Existing Driver F-Curve to use as template for a new one");
  /* return type */
  parm = api_def_ptr(fn, "driver", "FCurve", "", "New Driver F-Curve");
  api_def_fn_return(fn, parm);

  /* AnimData.drivers.find(...) */
  fn = api_def_fn(sapi, "find", "api_Driver_find");
  api_def_fn_ui_description(
      fn,
      "Find a driver F-Curve. Note that this function performs a linear scan "
      "of all driver F-Curves.");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_string(fn, "data_path", NULL, 0, "Data Path", "F-Curve data path");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_int(fn, "index", 0, 0, INT_MAX, "Index", "Array index", 0, INT_MAX);
  /* return type */
  parm = api_def_ptr(
      fn, "fcurve", "FCurve", "", "The found F-Curve, or None if it doesn't exist");
  api_def_fn_return(fn, parm);
}

void api_def_animdata_common(StructRNA *srna)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "animation_data", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "adt");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_override_fns(prop, NULL, NULL, "rna_AnimaData_override_apply");
  api_def_prop_ui_text(prop, "Animation Data", "Animation data for this data-block");
}

static void api_def_animdata(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "AnimData", NULL);
  api_def_struct_ui_text(sapi, "Animation Data", "Animation data for data-block");
  api_def_struct_ui_icon(sapi, ICON_ANIM_DATA);

  /* NLA */
  prop = api_def_prop(sapi, "nla_tracks", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "nla_tracks", NULL);
  api_def_prop_struct_type(prop, "NlaTrack");
  api_def_prop_ui_text(prop, "NLA Tracks", "NLA Tracks (i.e. Animation Layers)");
  api_def_prop_override_flag(prop,
                             PROPOVERRIDE_OVERRIDABLE_LIB |
                               PROPOVERRIDE_LIB_INSERTION | PROPOVERRIDE_NO_PROP_NAME);
  api_def_prop_override_fns(prop, NULL, NULL, "api_NLA_tracks_override_apply");

  api_api_animdata_nla_tracks(dapi, prop);

  api_define_lib_overridable(true);

  /* Active Action */
  prop = api_def_prop(sapi, "action", PROP_PTR, PROP_NONE);
  /* this flag as well as the dynamic test must be defined for this to be editable... */
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_REFCOUNT);
  api_def_prop_ptr_fns(
      prop, NULL, "api_AnimData_action_set", NULL, "api_Action_id_poll");
  api_def_prop_editable_fn(prop, "api_AnimData_action_editable");
  api_def_prop_ui_text(prop, "Action", "Active Action for this data-block");
  api_def_prop_update(prop, NC_ANIMATION | ND_NLA_ACTCHANGE, "api_AnimData_dependency_update");

  /* Active Action Settings */
  prop = api_def_prop(sapi, "action_extrapolation", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "act_extendmode");
  api_def_prop_enum_items(prop, api_enum_nla_mode_extend_items);
  api_def_prop_ui_text(
      prop,
      "Action Extrapolation",
      "Action to take for gaps past the Active Action's range (when evaluating with NLA)");
  api_def_prop_update(prop, NC_ANIMATION | ND_NLA, "api_AnimData_update");

  prop = api_def_prop(sapi, "action_blend_type", PROP_ENUM, PROP_NONE);
  ali_def_prop_enum_stype(prop, NULL, "act_blendmode");
  api_def_prop_enum_items(prop, api_enum_nla_mode_blend_items);
  api_def_prop_ui_text(
      prop,
      "Action Blending",
      "Method used for combining Active Action's result with result of NLA stack");
  api_def_prop_update(prop, NC_ANIMATION | ND_NLA, "api_AnimData_update"); /* this will do? */

  prop = api_def_prop(sapi, "action_influence", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "act_influence");
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop,
                           "Action Influence",
                           "Amount the Active Action contributes to the result of the NLA stack");
  api_def_prop_update(prop, NC_ANIMATION | ND_NLA, "api_AnimData_update"); /* this will do? */

  /* Drivers */
  prop = api_def_prop(sapi, "drivers", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "drivers", NULL);
  api_def_prop_struct_type(prop, "FCurve");
  api_def_prop_ui_text(prop, "Drivers", "The Drivers/Expressions for this data-block");

  api_define_lib_overridable(false);

  api_animdata_drivers(dapi, prop);

  api_define_lib_overridable(true);

  /* General Settings */
  prop = api_def_prop(sapi, "use_nla", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_style(prop, NULL, "flag", ADT_NLA_EVAL_OFF);
  api_def_prop_ui_text(
      prop, "NLA Evaluation Enabled", "NLA stack is evaluated when evaluating this block");
  api_def_prop_update(prop, NC_ANIMATION | ND_NLA, "api_AnimData_update"); /* this will do? */

  prop = api_def_prop(sapi, "use_tweak_mode", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", ADT_NLA_EDIT_ON);
  api_def_prop_bool_fns(prop, NULL, "api_AnimData_tweakmode_set");
  api_def_prop_ui_text(
      prop, "Use NLA Tweak Mode", "Whether to enable or disable tweak mode in NLA");
  api_def_prop_update(prop, NC_ANIMATION | ND_NLA, "api_AnimData_update");

  prop = api_def_prop(sapi, "use_pin", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_NO_DEG_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "flag", ADT_CURVES_ALWAYS_VISIBLE);
  api_def_prop_ui_text(prop, "Pin in Graph Editor", "");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  api_define_lib_overridable(false);

  /* Animation Data API */
  api_api_animdata(sapi);
}

/* --- */

void api_def_animation(DuneApi *dapi)
{
  api_def_animdata(dapi);

  api_def_keyingset(dapi);
  api_def_keyingset_path(dapi);
  api_def_keyingset_info(dapi);
}

#endif
