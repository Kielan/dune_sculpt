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

#include "rna_internal.h"

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
 * exported keyingset... :/
 */
const EnumPropItem rna_enum_keying_flag_items[] = {
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

static void api_AnimData_update(Main *bmain, Scene *UNUSED(scene), PointerRNA *ptr)
{
  Id *id = ptr->owner_id;

  anim_id_update(bmain, id);
}

static void api_AnimData_dependency_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  graph_relations_tag_update(bmain);

  api_AnimData_update(bmain, scene, ptr);
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
static void RKS_ITER_api_internal(KeyingSetInfo *ksi, bContext *C, KeyingSet *ks)
{
  extern ApiFn api_KeyingSetInfo_iterator_fn;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, ksi->api_ext.srna, ksi, &ptr);
  func = &api_KeyingSetInfo_iterator_fn; /* api_struct_find_function(&ptr, "poll"); */

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
  KeyingSetInfo *ksi = api_struct_blender_type_get(type);

  if (ksi == NULL) {
    return;
  }

  /* free RNA data referencing this */
  api_struct_free_extension(type, &ksi->rna_ext);
  api_struct_free(&DUNE_API, type);

  WM_main_add_notifier(NC_WINDOW, NULL);

  /* unlink Blender-side data */
  ANIM_keyingset_info_unregister(bmain, ksi);
}

static StructRNA *rna_KeyingSetInfo_register(Main *bmain,
                                             ReportList *reports,
                                             void *data,
                                             const char *identifier,
                                             StructValidateFunc validate,
                                             StructCallbackFunc call,
                                             StructFreeFunc free)
{
  KeyingSetInfo dummyksi = {NULL};
  KeyingSetInfo *ksi;
  PointerRNA dummyptr = {NULL};
  int have_function[3];

  /* setup dummy type info to store static properties in */
  /* TODO: perhaps we want to get users to register
   * as if they're using 'KeyingSet' directly instead? */
  RNA_pointer_create(NULL, &RNA_KeyingSetInfo, &dummyksi, &dummyptr);

  /* validate the python class */
  if (validate(&dummyptr, data, have_function) != 0) {
    return NULL;
  }

  if (strlen(identifier) >= sizeof(dummyksi.idname)) {
    BKE_reportf(reports,
                RPT_ERROR,
                "Registering keying set info class: '%s' is too long, maximum length is %d",
                identifier,
                (int)sizeof(dummyksi.idname));
    return NULL;
  }

  /* check if we have registered this info before, and remove it */
  ksi = ANIM_keyingset_info_find_name(dummyksi.idname);
  if (ksi && ksi->rna_ext.srna) {
    rna_KeyingSetInfo_unregister(bmain, ksi->rna_ext.srna);
  }

  /* create a new KeyingSetInfo type */
  ksi = MEM_mallocN(sizeof(KeyingSetInfo), "python keying set info");
  memcpy(ksi, &dummyksi, sizeof(KeyingSetInfo));

  /* set RNA-extensions info */
  ksi->rna_ext.srna = RNA_def_struct_ptr(&BLENDER_RNA, ksi->idname, &RNA_KeyingSetInfo);
  ksi->rna_ext.data = data;
  ksi->rna_ext.call = call;
  ksi->rna_ext.free = free;
  RNA_struct_blender_type_set(ksi->rna_ext.srna, ksi);

  /* set callbacks */
  /* NOTE: we really should have all of these... */
  ksi->poll = (have_function[0]) ? RKS_POLL_rna_internal : NULL;
  ksi->iter = (have_function[1]) ? RKS_ITER_rna_internal : NULL;
  ksi->generate = (have_function[2]) ? RKS_GEN_rna_internal : NULL;

  /* add and register with other info as needed */
  ANIM_keyingset_info_register(ksi);

  WM_main_add_notifier(NC_WINDOW, NULL);

  /* return the struct-rna added */
  return ksi->rna_ext.srna;
}

/* ****************************** */

static StructRNA *rna_ksPath_id_typef(PointerRNA *ptr)
{
  KS_Path *ksp = (KS_Path *)ptr->data;
  return ID_code_to_RNA_type(ksp->idtype);
}

static int rna_ksPath_id_editable(PointerRNA *ptr, const char **UNUSED(r_info))
{
  KS_Path *ksp = (KS_Path *)ptr->data;
  return (ksp->idtype) ? PROP_EDITABLE : 0;
}

static void rna_ksPath_id_type_set(PointerRNA *ptr, int value)
{
  KS_Path *data = (KS_Path *)(ptr->data);

  /* set the driver type, then clear the id-block if the type is invalid */
  data->idtype = value;
  if ((data->id) && (GS(data->id->name) != data->idtype)) {
    data->id = NULL;
  }
}

static void rna_ksPath_RnaPath_get(PointerRNA *ptr, char *value)
{
  KS_Path *ksp = (KS_Path *)ptr->data;

  if (ksp->rna_path) {
    strcpy(value, ksp->rna_path);
  }
  else {
    value[0] = '\0';
  }
}
