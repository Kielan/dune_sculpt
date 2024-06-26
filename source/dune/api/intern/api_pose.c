#include <stdlib.h>
#include <string.h>

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_action.h"
#include "types_armature.h"
#include "types_constraint.h"
#include "types_object.h"
#include "types_scene.h"

#include "lib_math.h"

#include "lang_translation.h"

#include "ui_resources.h"

#include "wm_types.h"

/* Bone and Group Color Sets */
const EnumPropItem api_enum_color_sets_items[] = {
    {0, "DEFAULT", 0, "Default Colors", ""},
    {1, "THEME01", ICON_COLORSET_01_VEC, "01 - Theme Color Set", ""},
    {2, "THEME02", ICON_COLORSET_02_VEC, "02 - Theme Color Set", ""},
    {3, "THEME03", ICON_COLORSET_03_VEC, "03 - Theme Color Set", ""},
    {4, "THEME04", ICON_COLORSET_04_VEC, "04 - Theme Color Set", ""},
    {5, "THEME05", ICON_COLORSET_05_VEC, "05 - Theme Color Set", ""},
    {6, "THEME06", ICON_COLORSET_06_VEC, "06 - Theme Color Set", ""},
    {7, "THEME07", ICON_COLORSET_07_VEC, "07 - Theme Color Set", ""},
    {8, "THEME08", ICON_COLORSET_08_VEC, "08 - Theme Color Set", ""},
    {9, "THEME09", ICON_COLORSET_09_VEC, "09 - Theme Color Set", ""},
    {10, "THEME10", ICON_COLORSET_10_VEC, "10 - Theme Color Set", ""},
    {11, "THEME11", ICON_COLORSET_11_VEC, "11 - Theme Color Set", ""},
    {12, "THEME12", ICON_COLORSET_12_VEC, "12 - Theme Color Set", ""},
    {13, "THEME13", ICON_COLORSET_13_VEC, "13 - Theme Color Set", ""},
    {14, "THEME14", ICON_COLORSET_14_VEC, "14 - Theme Color Set", ""},
    {15, "THEME15", ICON_COLORSET_15_VEC, "15 - Theme Color Set", ""},
    {16, "THEME16", ICON_COLORSET_16_VEC, "16 - Theme Color Set", ""},
    {17, "THEME17", ICON_COLORSET_17_VEC, "17 - Theme Color Set", ""},
    {18, "THEME18", ICON_COLORSET_18_VEC, "18 - Theme Color Set", ""},
    {19, "THEME19", ICON_COLORSET_19_VEC, "19 - Theme Color Set", ""},
    {20, "THEME20", ICON_COLORSET_20_VEC, "20 - Theme Color Set", ""},
    {-1, "CUSTOM", 0, "Custom Color Set", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "lib_ghash.h"
#  include "lib_string_utils.h"

#  include "BIK_api.h"
#  include "dune_action.h"
#  include "dune_armature.h"

#  include "types_userdef.h"

#  include "mem_guardedalloc.h"

#  include "dune_constraint.h"
#  include "dune_cxt.h"
#  include "dune_global.h"
#  include "dune_idprop.h"

#  include "graph.h"
#  include "graph_build.h"

#  include "ed_armature.h"
#  include "ed_object.h"

#  include "wm_api.h"

#  include "api_access.h"

static void api_Pose_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  /* XXX when to use this? ob->pose->flag |= (POSE_LOCKED|POSE_DO_UNLOCK); */

  graph_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
  wn_main_add_notifier(NC_OBJECT | ND_POSE, ptr->owner_id);
}

static void api_Pose_graph_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  graph_relations_tag_update(main);

  graph_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
  wm_main_add_notifier(NC_OBJECT | ND_POSE, ptr->owner_id);
}

static void api_Pose_IK_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  /* XXX when to use this? ob->pose->flag |= (POSE_LOCKED|POSE_DO_UNLOCK); */
  Object *ob = (Object *)ptr->owner_id;

  graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
  wm_main_add_notifier(NC_OBJECT | ND_POSE, ptr->owner_id);

  BIK_clear_data(ob->pose);
}

static char *api_Pose_path(ApiPtr *UNUSED(ptr))
{
  return lib_strdup("pose");
}

static char *api_PoseBone_path(ApiPtr *ptr)
{
  PoseChannel *pchan = ptr->data;
  char name_esc[sizeof(pchan->name) * 2];

  lib_str_escape(name_esc, pchan->name, sizeof(name_esc));
  return lib_sprintfN("pose.bones[\"%s\"]", name_esc);
}

/* Bone groups only. */
static bool api_bone_group_poll(Object *ob, ReportList *reports)
{
  if (ID_IS_OVERRIDE_LIB(ob)) {
    dune_report(reports, RPT_ERROR, "Cannot edit bone groups for proxies or lib overrides");
    return false;
  }

  return true;
}

static ActionGroup *api_bone_group_new(Id *id, Pose *pose, ReportList *reports, const char *name)
{
  if (!api_bone_group_poll((Object *)id, reports)) {
    return NULL;
  }

  ActionGroup *grp = dune_pose_add_group(pose, name);
  wm_main_add_notifier(NC_OBJECT | ND_POSE | NA_ADDED, id);
  return grp;
}

static void api_bone_group_remove(Id *id, Pose *pose, ReportList *reports, ApiPtr *grp_ptr)
{
  if (!api_bone_group_poll((Object *)id, reports)) {
    return;
  }

  ActionGroup *grp = grp_ptr->data;
  const int grp_idx = lib_findindex(&pose->agroups, grp);

  if (grp_idx == -1) {
    dune_reportf(reports, RPT_ERROR, "Bone group '%s' not found in this object", grp->name);
    return;
  }

  dune_pose_remove_group(pose, grp, grp_idx + 1);
  wm_main_add_notifier(NC_OBJECT | ND_POSE | NA_REMOVED, id);
}

/* shared for actions groups and bone groups */

void api_ActionGroup_colorset_set(ApiPtr *ptr, int value)
{
  Object *ob = (Object *)ptr->owner_id;
  if (!api_bone_group_poll(ob, NULL)) {
    return;
  }

  ActionGroup *grp = ptr->data;

  /* ensure only valid values get set */
  if ((value >= -1) && (value < 21)) {
    grp->customCol = value;

    /* sync colors stored with theme colors based on the index specified */
    action_group_colors_sync(grp, NULL);
  }
}

bool api_ActionGroup_is_custom_colorset_get(ApiPtr *ptr)
{
  ActionGroup *grp = ptr->data;

  return (grp->customCol < 0);
}

static void api_BoneGroup_name_set(ApiPtr *ptr, const char *value)
{
  Object *ob = (Object *)ptr->owner_id;
  if (!api_bone_group_poll(ob, NULL)) {
    return;
  }

  ActionGroup *agrp = ptr->data;

  /* copy the new name into the name slot */
  lib_strncpy_utf8(agrp->name, value, sizeof(agrp->name));

  lib_uniquename(&ob->pose->agroups,
                 agrp,
                 CXT_DATA_(LANG_CXT_ID_ARMATURE, "Group"),
                 '.',
                 offsetof(ActionGroup, name),
                 sizeof(agrp->name));
}

static IdProp **api_PoseBone_idprops(ApiPtr *ptr)
{
  PoseChannel *pchan = ptr->data;
  return &pchan->prop;
}

static void api_Pose_ik_solver_set(struct ApiPtr *ptr, int value)
{
  Pose *pose = (Pose *)ptr->data;

  if (pose->iksolver != value) {
    /* the solver has changed, must clean any temporary structures */
    BIK_clear_data(pose);
    if (pose->ikparam) {
      mem_freen(pose->ikparam);
      pose->ikparam = NULL;
    }
    pose->iksolver = value;
    dune_pose_ikparam_init(pose);
  }
}

static void api_Pose_ik_solver_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  Pose *pose = ptr->data;

  dune_pose_tag_recalc(main, pose); /* checks & sorts pose channels */
  graph_relations_tag_update(main);

  dune_pose_update_constraint_flags(pose);

  object_test_constraints(main, ob);

  graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY | ID_RECALC_TRANSFORM);
}

/* rotation - axis-angle */
static void api_PoseChannel_rotation_axis_angle_get(ApiPtr *ptr, float *value)
{
  PoseChannel *pchan = ptr->data;

  /* for now, assume that rotation mode is axis-angle */
  value[0] = pchan->rotAngle;
  copy_v3_v3(&value[1], pchan->rotAxis);
}

/* rotation - axis-angle */
static void api_PoseChannel_rotation_axis_angle_set(ApiPtr *ptr, const float *value)
{
  PoseChannel *pchan = ptr->data;

  /* for now, assume that rotation mode is axis-angle */
  pchan->rotAngle = value[0];
  copy_v3_v3(pchan->rotAxis, &value[1]);

  /* TODO: validate axis? */
}

static void api_PoseChannel_rotation_mode_set(ApiPtr *ptr, int value)
{
  PoseChannel *pchan = ptr->data;

  /* use API Method for conversions... */
  dune_rotMode_change_values(
      pchan->quat, pchan->eul, pchan->rotAxis, &pchan->rotAngle, pchan->rotmode, (short)value);

  /* finally, set the new rotation type */
  pchan->rotmode = value;
}

static float api_PoseChannel_length_get(PointerRNA *ptr)
{
  PoseChannel *pchan = ptr->data;
  return len_v3v3(pchan->pose_head, pchan->pose_tail);
}

static void api_PoseChannel_name_set(PointerRNA *ptr, const char *value)
{
  Object *ob = (Object *)ptr->owner_id;
  PoseChannel *pchan = (bPoseChannel *)ptr->data;
  char oldname[sizeof(pchan->name)], newname[sizeof(pchan->name)];

  /* need to be on the stack */
  lib_strncpy_utf8(newname, value, sizeof(pchan->name));
  lib_strncpy(oldname, pchan->name, sizeof(pchan->name));

  lib_assert(dune_id_is_in_global_main(&ob->id));
  lib_assert(dune_id_is_in_global_main(ob->data));
  ed_armature_bone_rename(G_MAIN, ob->data, oldname, newname);
}

/* See api_Bone_update_renamed() */
static void api_PoseChannel_name_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  Id *id = ptr->owner_id;

  /* redraw view */
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);

  /* update animation channels */
  wm_main_add_notifier(NC_ANIMATION | ND_ANIMCHAN, id);
}

static ApiPtr api_PoseChannel_bone_get(ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  ApiPtr tmp_ptr = *ptr;

  /* Replace the id_data pointer with the Armature Id. */
  tmp_ptr.owner_id = ob->data;

  return api_ptr_inherit_refine(&tmp_ptr, &Api_Bone, pchan->bone);
}

static bool api_PoseChannel_has_ik_get(ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  PoseChannel *pchan = (PoseChannel *)ptr->data;

  return dune_pose_channel_in_IK_chain(ob, pchan);
}

static ApiStruct *api_IKParam_refine(ApiPtr *ptr)
{
  IKParam *param = (IKParam *)ptr->data;

  switch (param->iksolver) {
    case IKSOLVER_ITASC:
      return &Api_Itasc;
    default:
      return &Api_IKParam;
  }
}

static ApiPtr api_Pose_ikparam_get(struct ApiPtr *ptr)
{
  Pose *pose = (Pose *)ptr->data;
  return api_ptr_inherit_refine(ptr, &Api_IKParam, pose->ikparam);
}

static ApiStruct *api_Pose_ikparam_typef(ApiPtr *ptr)
{
  Pose *pose = (Pose *)ptr->data;

  switch (pose->iksolver) {
    case IKSOLVER_ITASC:
      return &Api_Itasc;
    default:
      return &Api_IKParam;
  }
}

static void api_Itasc_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  Itasc *itasc = ptr->data;

  /* verify values */
  if (itasc->precision < 0.0001f) {
    itasc->precision = 0.0001f;
  }
  if (itasc->minstep < 0.001f) {
    itasc->minstep = 0.001f;
  }
  if (itasc->maxstep < itasc->minstep) {
    itasc->maxstep = itasc->minstep;
  }
  if (itasc->feedback < 0.01f) {
    itasc->feedback = 0.01f;
  }
  if (itasc->feedback > 100.0f) {
    itasc->feedback = 100.0f;
  }
  if (itasc->maxvel < 0.01f) {
    itasc->maxvel = 0.01f;
  }
  if (itasc->maxvel > 100.0f) {
    itasc->maxvel = 100.0f;
  }
  BIK_update_param(ob->pose);

  graph_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
}

static void api_Itasc_update_rebuild(Main *main, Scene *scene, ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  Pose *pose = ob->pose;

  dune_pose_tag_recalc(main, pose); /* checks & sorts pose channels */
  api_Itasc_update(main, scene, ptr);
}

static ApiPtr api_PoseChannel_bone_group_get(ApiPtr *ptr)
{
  Object *ob = (Object *)ptr->owner_id;
  Pose *pose = (ob) ? ob->pose : NULL;
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  ActionGroup *grp;

  if (pose) {
    grp = lib_findlink(&pose->agroups, pchan->agrp_index - 1);
  } else {
    grp = NULL;
  }

  return api_ptr_inherit_refine(ptr, &Api_BoneGroup, grp);
}

static void api_PoseChannel_bone_group_set(ApiPtr *ptr,
                                           ApiPtr value,
                                           struct ReportList *UNUSED(reports))
{
  Object *ob = (Object *)ptr->owner_id;
  Pose *pose = (ob) ? ob->pose : NULL;
  PoseChannel *pchan = (PoseChannel *)ptr->data;

  if (pose) {
    pchan->agrp_index = lib_findindex(&pose->agroups, value.data) + 1;
  } else {
    pchan->agrp_index = 0;
  }
}

static int api_PoseChannel_bone_group_index_get(ApiPtr *ptr)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  return MAX2(pchan->agrp_index - 1, 0);
}

static void api_PoseChannel_bone_group_index_set(ApiPtr *ptr, int value)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  pchan->agrp_index = value + 1;
}

static void api_PoseChannel_bone_group_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  Object *ob = (Object *)ptr->owner_id;
  bPose *pose = (ob) ? ob->pose : NULL;

  *min = 0;
  *max = pose ? max_ii(0, lib_list_count(&pose->agroups) - 1) : 0;
}

static ApiPtr api_Pose_active_bone_group_get(ApiPtr *ptr)
{
  Pose *pose = (Pose *)ptr->data;
  return api_ptr_inherit_refine(
      ptr, &Api_BoneGroup, lib_findlink(&pose->agroups, pose->active_group - 1));
}

static void api_Pose_active_bone_group_set(ApiPtr *ptr,
                                           ApiPtr value,
                                           struct ReportList *UNUSED(reports))
{
  Pose *pose = (Pose *)ptr->data;
  pose->active_group = lib_findindex(&pose->agroups, value.data) + 1;
}

static int api_Pose_active_bone_group_index_get(ApiPtr *ptr)
{
  Pose *pose = (Pose *)ptr->data;
  return MAX2(pose->active_group - 1, 0);
}

static void api_Pose_active_bone_group_index_set(ApiPtr *ptr, int value)
{
  Pose *pose = (Pose *)ptr->data;
  pose->active_group = value + 1;
}

static void api_Pose_active_bone_group_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  Pose *pose = (Pose *)ptr->data;

  *min = 0;
  *max = max_ii(0, lib_list_count(&pose->agroups) - 1);
}

#  if 0
static void api_pose_group_name_index_get(ApiPtr *ptr, char *value, int index)
{
  Pose *pose = (Pose *)ptr->data;
  ActionGroup *grp;

  grp = lib_findlink(&pose->agroups, index - 1);

  if (grp) {
    lib_strncpy(value, grp->name, sizeof(grp->name));
  } else {
    value[0] = '\0';
  }
}

static int api_pose_group_name_index_length(ApiPtr *ptr, int index)
{
  Pose *pose = (Pose *)ptr->data;
  ActionGroup *grp;

  grp = lib_findlink(&pose->agroups, index - 1);
  return (grp) ? strlen(grp->name) : 0;
}

static void api_pose_group_name_index_set(ApiPtr *ptr, const char *value, short *index)
{
  Pose *pose = (Pose *)ptr->data;
  ActionGroup *grp;
  int a;

  for (a = 1, grp = pose->agroups.first; grp; grp = grp->next, a++) {
    if (STREQ(grp->name, value)) {
      *index = a;
      return;
    }
  }

  *index = 0;
}

static void api_pose_pgroup_name_set(ApiPtr *ptr, const char *value, char *result, int maxlen)
{
  Pose *pose = (Pose *)ptr->data;
  ActionGroup *grp;

  for (grp = pose->agroups.first; grp; grp = grp->next) {
    if (STREQ(grp->name, value)) {
      lib_strncpy(result, value, maxlen);
      return;
    }
  }

  result[0] = '\0';
}
#  endif

static ApiPtr api_PoseChannel_active_constraint_get(ApiPtr *ptr)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  Constraint *con = dune_constraints_active_get(&pchan->constraints);
  return api_ptr_inherit_refine(ptr, &Api_Constraint, con);
}

static void api_PoseChannel_active_constraint_set(ApiPtr *ptr,
                                                  ApiPtr value,
                                                  struct ReportList *UNUSED(reports))
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  dune_constraints_active_set(&pchan->constraints, (Constraint *)value.data);
}

static Constraint *api_PoseChannel_constraints_new(Id *id,
                                                   PoseChannel *pchan,
                                                   Main *main,
                                                   int type)
{
  Object *ob = (Object *)id;
  Constraint *new_con = dune_constraint_add_for_pose(ob, pchan, NULL, type);

  ed_object_constraint_graph_tag_update(main, ob, new_con);
  wm_main_add_notifier(NC_OBJECT | ND_CONSTRAINT | NA_ADDED, id);

  return new_con;
}

static void api_PoseChannel_constraints_remove(
    Id *id, PoseChannel *pchan, Main *main, ReportList *reports, ApiPtr *con_ptr)
{
  Constraint *con = con_ptr->data;
  const bool is_ik = ELEM(con->type, CONSTRAINT_TYPE_KINEMATIC, CONSTRAINT_TYPE_SPLINEIK);
  Object *ob = (Object *)id;

  if (lib_findindex(&pchan->constraints, con) == -1) {
    dune_reportf(
        reports, RPT_ERROR, "Constraint '%s' not found in pose bone '%s'", con->name, pchan->name);
    return;
  }

  dune_constraint_remove(&pchan->constraints, con);
  API_PTR_INVALIDATE(con_ptr);

  ed_object_constraint_update(main, ob);

  /* is this really needed? */
  dune_constraints_active_set(&pchan->constraints, NULL);

  wm_main_add_notifier(NC_OBJECT | ND_CONSTRAINT | NA_REMOVED, id);

  if (is_ik) {
    BIK_clear_data(ob->pose);
  }
}

static void api_PoseChannel_constraints_move(
    Id *id, PoseChannel *pchan, Main *main, ReportList *reports, int from, int to)
{
  Object *ob = (Object *)id;

  if (from == to) {
    return;
  }

  if (!lib_list_move_index(&pchan->constraints, from, to)) {
    dune_reportf(reports, RPT_ERROR, "Could not move constraint from index '%d' to '%d'", from, to);
    return;
  }

  ed_object_constraint_tag_update(main, ob, NULL);
  wm_main_add_notifier(NC_OBJECT | ND_CONSTRAINT, ob);
}

static Constraint *api_PoseChannel_constraints_copy(Id *id,
                                                    PoseChannel *pchan,
                                                    Main *main,
                                                    ApiPtr *con_ptr)
{
  Object *ob = (Object *)id;
  Constraint *con = con_ptr->data;
  Constraint *new_con = dune_constraint_copy_for_pose(ob, pchan, con);
  new_con->flag |= CONSTRAINT_OVERRIDE_LIB_LOCAL;

  ed_object_constraint_graph_tag_update(main, ob, new_con);
  wm_main_add_notifier(NC_OBJECT | ND_CONSTRAINT | NA_ADDED, id);

  return new_con;
}

bool api_PoseChannel_constraints_override_apply(Main *UNUSED(main),
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
  lib_assert(opop->operation == IDOVERRIDE_LIB_OP_INSERT_AFTER &&
             "Unsupported api override operation on constraints collection");

  PoseChannel *pchan_dst = (PoseChannel *)ptr_dst->data;
  PoseChannel *pchan_src = (PoseChannel *)ptr_src->data;

  /* Remember that insertion operations are defined and stored in correct order, which means that
   * even if we insert several items in a row, we always insert first one, then second one, etc.
   * So we should always find 'anchor' constraint in both _src *and* _dst */
  const size_t name_offset = offsetof(Constraint, name);
  Constraint *con_anchor = lib_list_string_or_index_find(&pchan_dst->constraints,
                                                         opop->subitem_ref_name,
                                                         name_offset,
                                                         opop->subitem_ref_index);
  /* If `con_anchor` is NULL, `con_src` will be inserted in first position. */

  Constraint *con_src = lib_list_string_or_index_find(
      &pchan_src->constraints, opop->subitem_local_name, name_offset, opop->subitem_local_index);

  if (con_src == NULL) {
    lib_assert(con_src != NULL);
    return false;
  }

  Constraint *con_dst = dune_constraint_duplicate_ex(con_src, 0, true);

  /* This handles NULL anchor as expected by adding at head of list. */
  lib_insertlinkafter(&pchan_dst->constraints, con_anchor, con_dst);

  /* This should actually *not* be needed in typical cases.
   * However, if overridden source was edited,
   * we *may* have some new conflicting names. */
  dune_constraint_unique_name(con_dst, &pchan_dst->constraints);

  //  printf("%s: We inserted a constraint...\n", __func__);
  return true;
}

static int api_PoseChannel_proxy_editable(ApjPtr *ptr, const char **r_info)
{
  Object *ob = (Object *)ptr->owner_id;
  Armature *arm = ob->data;
  PoseChannel *pchan = (PoseChannel *)ptr->data;

  if (false && pchan->bone && (pchan->bone->layer & arm->layer_protected)) {
    *r_info = "Can't edit property of a proxy on a protected layer";
    return 0;
  }

  return PROP_EDITABLE;
}

static int api_PoseChannel_location_editable(ApiPtr *ptr, int index)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;

  /* only if the axis in question is locked, not editable... */
  if ((index == 0) && (pchan->protectflag & OB_LOCK_LOCX)) {
    return 0;
  } else if ((index == 1) && (pchan->protectflag & OB_LOCK_LOCY)) {
    return 0;
  } else if ((index == 2) && (pchan->protectflag & OB_LOCK_LOCZ)) {
    return 0;
  } else {
    return PROP_EDITABLE;
  }
}

static int api_PoseChannel_scale_editable(ApiPtr *ptr, int index)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;

  /* only if the axis in question is locked, not editable... */
  if ((index == 0) && (pchan->protectflag & OB_LOCK_SCALEX)) {
    return 0;
  } else if ((index == 1) && (pchan->protectflag & OB_LOCK_SCALEY)) {
    return 0;
  } else if ((index == 2) && (pchan->protectflag & OB_LOCK_SCALEZ)) {
    return 0;
  } else {
    return PROP_EDITABLE;
  }
}

static int api_PoseChannel_rotation_euler_editable(ApiPtr *ptr, int index)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;

  /* only if the axis in question is locked, not editable... */
  if ((index == 0) && (pchan->protectflag & OB_LOCK_ROTX)) {
    return 0;
  } else if ((index == 1) && (pchan->protectflag & OB_LOCK_ROTY)) {
    return 0;
  } else if ((index == 2) && (pchan->protectflag & OB_LOCK_ROTZ)) {
    return 0;
  } else {
    return PROP_EDITABLE;
  }
}

static int api_PoseChannel_rotation_4d_editable(ApiPtr *ptr, int index)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;

  /* only consider locks if locking components individually... */
  if (pchan->protectflag & OB_LOCK_ROT4D) {
    /* only if the axis in question is locked, not editable... */
    if ((index == 0) && (pchan->protectflag & OB_LOCK_ROTW)) {
      return 0;
    } else if ((index == 1) && (pchan->protectflag & OB_LOCK_ROTX)) {
      return 0;
    } else if ((index == 2) && (pchan->protectflag & OB_LOCK_ROTY)) {
      return 0;
    } else if ((index == 3) && (pchan->protectflag & OB_LOCK_ROTZ)) {
      return 0;
    }
  }

  return PROP_EDITABLE;
}

/* not essential, but much faster than the default lookup function */
static int api_PoseBones_lookup_string(ApiPtr *ptr, const char *key, ApiPtr *r_ptr)
{
  Pose *pose = (Pose *)ptr->data;
  PoseChannel *pchan = dune_pose_channel_find_name(pose, key);
  if (pchan) {
    api_ptr_create(ptr->owner_id, &Api_PoseBone, pchan, r_ptr);
    return true;
  } else {
    return false;
  }
}

static void api_PoseChannel_matrix_basis_get(ApiPtr *ptr, float *values)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  dune_pchan_to_mat4(pchan, (float(*)[4])values);
}

static void api_PoseChannel_matrix_basis_set(ApiPtr *ptr, const float *values)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  dune_pchan_apply_mat4(pchan, (float(*)[4])values, false); /* no compat for predictable result */
}

static void api_PoseChannel_matrix_set(ApiPtr *ptr, const float *values)
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  Object *ob = (Object *)ptr->owner_id;
  float tmat[4][4];

  dune_armature_mat_pose_to_bone_ex(NULL, ob, pchan, (float(*)[4])values, tmat);

  dune_pchan_apply_mat4(pchan, tmat, false); /* no compat for predictable result */
}

static PoseChannel *api_PoseChannel_ensure_own_pchan(Object *ob,
                                                     Object *ref_ob,
                                                     PoseChannel *ref_pchan)
{
  if (ref_ob != ob) {
    /* We are trying to set a pchan from another object! Forbidden,
     * try to find by name, or abort. */
    if (ref_pchan != NULL) {
      ref_pchan = dune_pose_channel_find_name(ob->pose, ref_pchan->name);
    }
  }
  return ref_pchan;
}

static void api_PoseChannel_custom_shape_transform_set(ApiPtr *ptr,
                                                       ApiPtr value,
                                                       struct ReportList *UNUSED(reports))
{
  PoseChannel *pchan = (PoseChannel *)ptr->data;
  Object *ob = (Object *)ptr->owner_id;

  pchan->custom_tx = api_PoseChannel_ensure_own_pchan(ob, (Object *)value.owner_id, value.data);
}

#else

void api_def_actionbone_group_common(ApiStruct *sapi, int update_flag, const char *update_cb)
{
  ApiProp *prop;

  /* color set + colors */
  prop = api_def_prop(sapi, "color_set", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "customCol");
  api_def_prop_enum_items(prop, api_enum_color_sets_items);
  api_def_prop_enum_fns(prop, NULL, "api_ActionGroup_colorset_set", NULL);
  api_def_prop_ui_text(prop, "Color Set", "Custom color set to use");
  api_def_prop_update(prop, update_flag, update_cb);

  prop = api_def_prop(sapi, "is_custom_color_set", PROP_BOOLEAN, PROP_NONE);
  woi_def_prop_bool_fns(prop, "api_ActionGroup_is_custom_colorset_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Custom Color Set", "Color set is user-defined instead of a fixed theme color set");

  /* TODO: editing the colors for this should result in changes to the color type... */
  prop = api_def_prop(sapi, "colors", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "ThemeBoneColorSet");
  /* NOTE: the types data is not really a pointer, but this code works :) */
  api_def_prop_ptr_stype(prop, NULL, "cs");
  api_def_prop_ui_text(
      prop, "Colors", "Copy of the colors associated with the group's color set");
  api_def_prop_update(prop, update_flag, update_cb);
}

static void api_def_bone_group(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* struct */
  sapi = api_def_struct(dapi, "BoneGroup", NULL);
  api_def_struct_stype(sapi, "bActionGroup");
  api_def_struct_ui_text(sapi, "Bone Group", "Groups of Pose Channels (Bones)");
  api_def_struct_ui_icon(sapi, ICON_GROUP_BONE);

  /* name */
  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "");
  api_def_prop_string_fn(prop, NULL, NULL, "api_BoneGroup_name_set");
  api_def_struct_name_prop(sapi, prop);

  /* TODO: add some runtime-collections stuff to access grouped bones. */

  /* color set */
  api_def_actionbone_group_common(srna, NC_OBJECT | ND_POSE, "rna_Pose_update");
}

static const EnumPropItem prop_iksolver_items[] = {
    {IKSOLVER_STANDARD, "LEGACY", 0, "Standard", "Original IK solver"},
    {IKSOLVER_ITASC, "ITASC", 0, "iTaSC", "Multi constraint, stateful IK solver"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem prop_solver_items[] = {
    {ITASC_SOLVER_SDLS, "SDLS", 0, "SDLS", "Selective Damped Least Square"},
    {ITASC_SOLVER_DLS, "DLS", 0, "DLS", "Damped Least Square with Numerical Filtering"},
    {0, NULL, 0, NULL, NULL},
};

static void api_def_pose_channel_constraints(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "PoseBoneConstraints");
  srna = api_def_struct(dapi, "PoseBoneConstraints", NULL);
  api_def_struct_stype(sapi, "PoseChannel");
  api_def_struct_ui_text(sapi, "PoseBone Constraints", "Collection of pose bone constraints");

  /* Collection active property */
  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Constraint");
  api_def_prop_ptr_fns(prop,
                       "api_PoseChannel_active_constraint_get",
                       "api_PoseChannel_active_constraint_set",
                       NULL,
                       NULL);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Active Constraint", "Active PoseChannel constraint");

  /* Constraint collection */
  fn = api_def_fn(sapi, "new", "api_PoseChannel_constraints_new");
  api_def_fn_ui_description(fn, "Add a constraint to this object");
  api_def_fn_flag(fn,
                  FN_USE_MAIN | FN_USE_SELF_ID); /* ID and Main needed for refresh */
  /* return type */
  parm = api_def_ptr(fn, "constraint", "Constraint", "", "New constraint");
  api_def_fn_return(fn, parm);
  /* constraint to add */
  parm = api_def_enum(
      fn, "type", api_enum_constraint_type_items, 1, "", "Constraint type to add");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "remove", "api_PoseChannel_constraints_remove");
  api_def_fn_ui_description(fn, "Remove a constraint from this object");
  api_def_fn_flag(
      fn, FN_USE_SELF_ID | FN_USE_MAIN | FN_USE_REPORTS); /* Id needed for refresh */
  /* constraint to remove */
  parm = api_def_ptr(fn, "constraint", "Constraint", "", "Removed constraint");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "move", "api_PoseChannel_constraints_move");
  api_def_fn_ui_description(fn, "Move a constraint to a different position");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_MAIN | FN_USE_REPORTS);
  parm = api_def_int(
      fn, "from_index", -1, INT_MIN, INT_MAX, "From Index", "Index to move", 0, 10000);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "to_index", -1, INT_MIN, INT_MAX, "To Index", "Target index", 0, 10000);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "copy", "api_PoseChannel_constraints_copy");
  api_def_fn_ui_description(fn, "Add a new constraint that is a copy of the given one");
  api_def_fn_flag(fn, FN_USE_MAIN | FN_USE_SELF_ID);
  /* constraint to copy */
  parm = api_def_ptr(fn,
                     "constraint",
                     "Constraint",
                     "",
                     "Constraint to copy - may belong to a different object");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
  /* return type */
  parm = api_def_ptr(fn, "new_constraint", "Constraint", "", "New constraint");
  api_def_n_return(fn, parm);
}

static void api_def_pose_channel(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "PoseBone", NULL);
  api_def_struct_stype(sapi, "PoseChannel");
  api_def_struct_ui_text(sapi, "Pose Bone", "Channel defining pose data for a bone in a Pose");
  api_def_struct_path_fn(sapi, "api_PoseBone_path");
  api_def_struct_idprops_fn(sapi, "api_PoseBone_idprops");
  api_def_struct_ui_icon(sapi, ICON_BONE_DATA);

  /* Bone Constraints */
  prop = api_def_prop(sapi, "constraints", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "Constraint");
  api_def_prop_override_flag(
      prop, PROPOVERRIDE_OVERRIDABLE_LIB | PROPOVERRIDE_LIBRARY_INSERTION);
  api_def_prop_ui_text(prop, "Constraints", "Constraints that act on this pose channel");
  api_def_prop_override_fns(prop, NULL, NULL, "rna_PoseChannel_constraints_override_apply");

  api_def_pose_channel_constraints(brna, prop);

  /* Name + Selection Status */
  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(prop, NULL, NULL, "api_PoseChannel_name_set");
  api_def_prop_ui_text(prop, "Name", "");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_update(prop, 0, "api_PoseChannel_name_update");

  /* Baked Bone Path cache data */
  api_def_motionpath_common(srna);

  /* Relationships to other bones */
  prop = api_def_prop(sapi, "bone", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "Bone");
  api_def_prop_ptr_fns(prop, "api_PoseChannel_bone_get", NULL, NULL, NULL);
  api_def_prop_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Bone", "Bone associated with this PoseBone");

  prop = api_def_prop(sapi, "parent", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "PoseBone");
  api_def_prop_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Parent", "Parent of this pose bone");

  prop = api_def_prop(srna, "child", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "PoseBone");
  api_def_prop_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Child", "Child of this pose bone");

  /* Transformation settings */
  prop = api_def_prop(sapi, "location", PROP_FLOAT, PROP_TRANSLATION);
  api_def_pro_float_stype(prop, NULL, "loc");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_editable_array_func(prop, "api_PoseChannel_location_editable");
  api_def_prop_ui_text(prop, "Location", "");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = apu_def_prop(sapu, "scale", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "size");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_editable_array_func(prop, "api_PoseChannel_scale_editable");
  api_def_prop_float_array_default(prop, api_default_scale_3d);
  api_def_prop_ui_text(prop, "Scale", "");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "rotation_quaternion", PROP_FLOAT, PROP_QUATERNION);
  api_def_prop_float_stype(prop, NULL, "quat");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_editable_array_func(prop, "api_PoseChannel_rotation_4d_editable");
  api_def_prop_float_array_default(prop, api_default_quaternion);
  api_def_prop_ui_text(prop, "Quaternion Rotation", "Rotation in Quaternions");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  /* XXX: for axis-angle, it would have been nice to have 2 separate fields for UI purposes, but
   * having a single one is better for Keyframing and other property-management situations...  */
  prop = api_def_prop(sapi, "rotation_axis_angle", PROP_FLOAT, PROP_AXISANGLE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_array(prop, 4);
  api_def_prop_float_fns(prop,
                         "api_PoseChannel_rotation_axis_angle_get",
                         "api_PoseChannel_rotation_axis_angle_set",
                         NULL);
  api_def_prop_editable_array_func(prop, "rna_PoseChannel_rotation_4d_editable");
  api_def_prop_float_array_default(prop, rna_default_axis_angle);
  api_def_prop_ui_text(
      prop, "Axis-Angle Rotation", "Angle of Rotation for Axis-Angle rotation representation");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_update");

  prop = api_def_prop(sapi, "rotation_euler", PROP_FLOAT, PROP_EULER);
  api_def_prop_float_stype(prop, NULL, "eul");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_editable_array_fn(prop, "api_PoseChannel_rotation_euler_editable");
  api_def_prop_ui_text(prop, "Euler Rotation", "Rotation in Eulers");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "rotation_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "rotmode");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_enum_items(prop, rna_enum_object_rotation_mode_items);
  api_def_prop_enum_fns(prop, NULL, "api_PoseChannel_rotation_mode_set", NULL);
  /* XXX... disabled, since proxy-locked layers are currently
   * used for ensuring proxy-syncing too */
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_ui_text(prop, "Rotation Mode", "");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_update");

  /* Curved bones settings - Applied on top of restpose values */
  api_def_bone_curved_common(sapi, true, false);

  /* Custom BBone next/prev sources */
  prop = api_def_prop(sapi, "bbone_custom_handle_start", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "bbone_prev");
  api_def_prop_struct_type(prop, "PoseBone");
  api_def_prop_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);
  api_def_prop_ui_text(
      prop, "B-Bone Start Handle", "Bone that serves as the start handle for the B-Bone curve");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_dependency_update");

  prop = api_def_prop(sapi, "bbone_custom_handle_end", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "bbone_next");
  api_def_prop_struct_type(prop, "PoseBone");
  api_def_prop_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_override_flag(prop, PROPOVERRIDE_NO_COMPARISON);
  api_def_prop_ui_text(
      prop, "B-Bone End Handle", "Bone that serves as the end handle for the B-Bone curve");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_dependency_update");

  /* transform matrices - should be read-only since these are set directly by AnimSys evaluation */
  prop = api_def_prop(sapi, "matrix_channel", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_float_stype(prop, NULL, "chan_mat");
  api_def_prop_multi_array(prop, 2, rna_matrix_dimsize_4x4);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Channel Matrix", "4x4 matrix, before constraints");

  /* writable because it touches loc/scale/rot directly */
  prop = api_def_prop(sapi, "matrix_basis", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_multi_array(prop, 2, api_matrix_dimsize_4x4);
  api_def_prop_ui_text(
      prop,
      "Basis Matrix",
      "Alternative access to location/scale/rotation relative to the parent and own rest bone");
  api_def_prop_float_fns(
      prop, "api_PoseChannel_matrix_basis_get", "api_PoseChannel_matrix_basis_set", NULL);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  /* final matrix */
  prop = api_def_prop(sapi, "matrix", PROP_FLOAT, PROP_MATRIX);
  api_def_prop_float_stype(prop, NULL, "pose_mat");
  api_def_prop_multi_array(prop, 2, api_matrix_dimsize_4x4);
  api_def_prop_float_fns(prop, NULL, "api_PoseChannel_matrix_set", NULL);
  api_def_prop_ui_text(
      prop,
      "Pose Matrix",
      "Final 4x4 matrix after constraints and drivers are applied (object space)");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  /* Head/Tail Coordinates (in Pose Space) - Automatically calculated... */
  prop = api_def_prop(sapi, "head", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_float_stype(prop, NULL, "pose_head");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Pose Head Position", "Location of head of the channel's bone");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);

  prop = api_def_prop(sapi, "tail", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_float_stype(prop, NULL, "pose_tail");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Pose Tail Position", "Location of tail of the channel's bone");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);

  prop = api_def_prop(sapi, "length", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_fns(prop, "api_PoseChannel_length_get", NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Length", "Length of the bone");

  /* IK Settings */
  prop = api_def_prop(sapi, "is_in_ik_chain", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_PoseChannel_has_ik_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Has IK", "Is part of an IK chain");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");
  prop = api_def_prop(sapi, "lock_ik_x", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ikflag", BONE_IK_NO_XDOF);
  api_def_prop_ui_icon(prop, ICON_UNLOCKED, true);
  api_def_prop_ui_text(prop, "IK X Lock", "Disallow movement around the X axis");
  api_def_prop_editable_func(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "lock_ik_y", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ikflag", BONE_IK_NO_YDOF);
  api_def_prop_ui_icon(prop, ICON_UNLOCKED, true);
  api_def_prop_ui_text(prop, "IK Y Lock", "Disallow movement around the Y axis");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "lock_ik_z", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ikflag", BONE_IK_NO_ZDOF);
  api_def_prop_ui_icon(prop, ICON_UNLOCKED, true);
  api_def_prop_ui_text(prop, "IK Z Lock", "Disallow movement around the Z axis");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "use_ik_limit_x", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ikflag", BONE_IK_XLIMIT);
  api_def_prop_ui_text(prop, "IK X Limit", "Limit movement around the X axis");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "use_ik_limit_y", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ikflag", BONE_IK_YLIMIT);
  api_def_prop_ui_text(prop, "IK Y Limit", "Limit movement around the Y axis");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "use_ik_limit_z", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ikflag", BONE_IK_ZLIMIT);
  api_def_prop_ui_text(prop, "IK Z Limit", "Limit movement around the Z axis");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_IK_update");

  prop = api_def_prop(sapi, "use_ik_rotation_control", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ikflag", BONE_IK_ROTCTL);
  api_def_prop_ui_text(prop, "IK Rotation Control", "Apply channel rotation as IK constraint");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "use_ik_linear_control", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ikflag", BONE_IK_LINCTL);
  api_def_prop_ui_text(
      prop, "IK Linear Control", "Apply channel size as IK constraint if stretching is enabled");
  api_def_prop_editable_fn(prop, "rna_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_min_x", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_sdna(prop, NULL, "limitmin[0]");
  api_def_prop_range(prop, -M_PI, 0.0f);
  api_def_prop_ui_text(prop, "IK X Minimum", "Minimum angles for IK Limit");
  api_def_prop_editable_fn(prop, "rna_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_max_x", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limitmax[0]");
  api_def_prop_range(prop, 0.0f, M_PI);
  api_def_prop_ui_text(prop, "IK X Maximum", "Maximum angles for IK Limit");
  api_def_prop_editable_fn(prop, "rna_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_min_y", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limitmin[1]");
  api_def_prop_range(prop, -M_PI, 0.0f);
  api_def_prop_ui_text(prop, "IK Y Minimum", "Minimum angles for IK Limit");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_max_y", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limitmax[1]");
  api_def_prop_range(prop, 0.0f, M_PI);
  api_def_prop_ui_text(prop, "IK Y Maximum", "Maximum angles for IK Limit");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_min_z", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limitmin[2]");
  api_def_prop_range(prop, -M_PI, 0.0f);
  api_def_prop_ui_text(prop, "IK Z Minimum", "Minimum angles for IK Limit");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_max_z", PROP_FLOAT, PROP_ANGLE);
  api_def_prop_float_stype(prop, NULL, "limitmax[2]");
  api_def_prop_range(prop, 0.0f, M_PI);
  api_def_prop_ui_text(prop, "IK Z Maximum", "Maximum angles for IK Limit");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_stiffness_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "stiffness[0]");
  api_def_prop_range(prop, 0.0f, 0.99f);
  api_def_prop_ui_text(prop, "IK X Stiffness", "IK stiffness around the X axis");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_stiffness_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "stiffness[1]");
  api_def_prop_range(prop, 0.0f, 0.99f);
  api_def_prop_ui_text(prop, "IK Y Stiffness", "IK stiffness around the Y axis");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_stiffness_z", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "stiffness[2]");
  api_def_prop_range(prop, 0.0f, 0.99f);
  api_def_prop_ui_text(prop, "IK Z Stiffness", "IK stiffness around the Z axis");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_stretch", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "ikstretch");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "IK Stretch", "Allow scaling of the bone for IK");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_IK_update");

  prop = api_def_prop(sapi, "ik_rotation_weight", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "ikrotweight");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "IK Rotation Weight", "Weight of rotation constraint for IK");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "ik_linear_weight", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_style(prop, NULL, "iklinweight");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "IK Lin Weight", "Weight of scale constraint for IK");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  /* custom bone shapes */
  prop = api_def_prop(sapi, "custom_shape", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "custom");
  api_def_prop_struct_type(prop, "Object");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_ID_REFCOUNT);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(
      prop, "Custom Object", "Object that defines custom display shape for this bone");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_graph_update");

  prop = api_def_prop(sapi, "custom_shape_scale_xyz", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "custom_scale_xyz");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_float_array_default(prop, rna_default_scale_3d);
  api_def_prop_ui_text(prop, "Custom Shape Scale", "Adjust the size of the custom shape");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "custom_shape_translation", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "custom_translation");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_ui_text(
      prop, "Custom Shape Translation", "Adjust the location of the custom shape");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "custom_shape_rotation_euler", PROP_FLOAT, PROP_EULER);
  api_def_prop_float_stype(prop, NULL, "custom_rotation_euler");
  api_def_prop_ui_text(
      prop, "Custom Shape Rotation", "Adjust the rotation of the custom shape");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "use_custom_shape_bone_size", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "drawflag", PCHAN_DRAW_NO_CUSTOM_BONE_SIZE);
  api_def_prop_ui_text(
      prop, "Scale to Bone Length", "Scale the custom object by the bone length");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "custom_shape_transform", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "custom_tx");
  api_def_prop_struct_type(prop, "PoseBone");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_PTR_NO_OWNERSHIP);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop,
                       "Custom Shape Transform",
                       "Bone that defines the display transform of this custom shape");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_ptr_fns(
      prop, NULL, "api_PoseChannel_custom_shape_transform_set", NULL, NULL);
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  /* bone groups */
  prop = api_def_prop(sapi, "bone_group_index", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "agrp_index");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_fbs(prop,
                       "api_PoseChannel_bone_group_index_get",
                       "api_PoseChannel_bone_group_index_set",
                       "api_PoseChannel_bone_group_index_range");
  api_def_prop_ui_text(
      prop, "Bone Group Index", "Bone group this pose channel belongs to (0 means no group)");
  api_def_prop_editable_func(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "bone_group", PROP_PRR, PROP_NONE);
  api_def_prop_struct_type(prop, "BoneGroup");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_funcs(
      prop, "api_PoseChannel_bone_group_get", "api_PoseChannel_bone_group_set", NULL, NULL);
  api_def_prop_ui_text(prop, "Bone Group", "Bone group this pose channel belongs to");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  /* transform locks */
  prop = api_def_prop(sapi, "lock_location", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "protectflag", OB_LOCK_LOCX);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Lock Location", "Lock editing of location when transforming");
  api_def_prop_ui_icon(prop, ICON_UNLOCKED, 1);
  api_def_prop_editable_fn(prop, "rna_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "lock_rotation", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "protectflag", OB_LOCK_ROTX);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Lock Rotation", "Lock editing of rotation when transforming");
  api_def_prop_ui_icon(prop, ICON_UNLOCKED, 1);
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  /* XXX this is sub-optimal - it really should be included above, but due to technical reasons
   *     we can't do this! */
  prop = api_def_prop(sapi, "lock_rotation_w", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "protectflag", OB_LOCK_ROTW);
  api_def_prop_ui_text(
      prop,
      "Lock Rotation (4D Angle)",
      "Lock editing of 'angle' component of four-component rotations when transforming");
  api_def_prop_ui_icon(prop, ICON_UNLOCKED, 1);
  api_def_prop_editable_fn(prop, "rna_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_update");

  /* XXX this needs a better name */
  prop = api_def_prop(sapi, "lock_rotations_4d", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "protectflag", OB_LOCK_ROT4D);
  api_def_prop_ui_text(
      prop,
      "Lock Rotations (4D)",
      "Lock editing of four component rotations by components (instead of as Eulers)");
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "lock_scale", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "protectflag", OB_LOCK_SCALEX);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Lock Scale", "Lock editing of scale when transforming");
  api_def_prop_ui_icon(prop, ICON_UNLOCKED, 1);
  api_def_prop_editable_fn(prop, "api_PoseChannel_proxy_editable");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  api_api_pose_channel(sapi);
}

static void api_def_pose_itasc(DuneApi *dapi)
{
  static const EnumPropItem prop_itasc_mode_items[] = {
      {0,
       "ANIMATION",
       0,
       "Animation",
       "Stateless solver computing pose starting from current action and non-IK constraints"},
      {ITASC_SIMULATION,
       "SIMULATION",
       0,
       "Simulation",
       "State-full solver running in real-time context and ignoring actions "
       "and non-IK constraints"},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropItem prop_itasc_reiteration_items[] = {
      {0,
       "NEVER",
       0,
       "Never",
       "The solver does not reiterate, not even on first frame (starts from rest pose)"},
      {ITASC_INITIAL_REITERATION,
       "INITIAL",
       0,
       "Initial",
       "The solver reiterates (converges) on the first frame but not on "
       "subsequent frame"},
      {ITASC_INITIAL_REITERATION | ITASC_REITERATION,
       "ALWAYS",
       0,
       "Always",
       "The solver reiterates (converges) on all frames"},
      {0, NULL, 0, NULL, NULL},
  };

  ApiStruct *sapii;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Itasc", "IKParam");
  api_def_struct_stype(sapi, "Itasc");
  api_def_struct_ui_text(sapi, "Itasc", "Params for the iTaSC IK solver");

  prop = api_def_prop(sapi, "precision", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "precision");
  api_def_prop_range(prop, 0.0f, 0.1f);
  api_def_prop_ui_text(prop, "Precision", "Precision of convergence in case of reiteration");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "iterations", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "numiter");
  api_def_prop_range(prop, 0, 1000);
  api_def_prop_ui_text(
      prop, "Iterations", "Maximum number of iterations for convergence in case of reiteration");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "step_count", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "numstep");
  api_def_prop_range(prop, 1.0f, 50.0f);
  api_def_prop_ui_text(prop, "Num Steps", "Divide the frame interval into this many steps");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, prop_itasc_mode_items);
  api_def_prop_ui_text(prop, "Mode", NULL);
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update_rebuild");

  prop = api_def_prop(sapi, "reiteration_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, prop_itasc_reiteration_items);
  api_def_prop_ui_text(prop,
                       "Reiteration",
                       "Defines if the solver is allowed to reiterate (converge until "
                       "precision is met) on none, first or all frames");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "use_auto_step", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", ITASC_AUTO_STEP);
  api_def_prop_ui_text(prop,
                       "Auto Step",
                       "Automatically determine the optimal number of steps for best "
                       "performance/accuracy trade off");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "step_min", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "minstep");
  api_def_prop_range(prop, 0.0f, 0.1f);
  api_def_prop_ui_text(
      prop, "Min Step", "Lower bound for timestep in second in case of automatic substeps");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "step_max", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "maxstep");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(
      prop, "Max Step", "Higher bound for timestep in second in case of automatic substeps");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "feedback", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "feedback");
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(
      prop,
      "Feedback",
      "Feedback coefficient for error correction, average response time is 1/feedback");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "velocity_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "maxvel");
  api_def_prop_range(prop, 0.0f, 100.0f);
  api_def_prop_ui_text(prop, "Max Velocity", "Maximum joint velocity in radians/second");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "solver", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "solver");
  api_def_prop_enum_items(prop, prop_solver_items);
  api_def_prop_ui_text(
      prop, "Solver", "Solving method selection: automatic damping or manual damping");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update_rebuild");

  prop = api_def_prop(sapi, "damping_max", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "dampmax");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop,
                       "Damp",
                       "Maximum damping coefficient when singular value is nearly 0 "
                       "(higher values produce results with more stability, less reactivity)");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Itasc_update");

  prop = api_def_prop(sapi, "damping_epsilon", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "dampeps");
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop,
                       "Epsilon",
                       "Singular value under which damping is progressively applied "
                       "(higher values produce results with more stability, less reactivity)");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Itasc_update");
}

static void api_def_pose_ikparam(BlenderRNA *brna)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "IKParam", NULL);
  api_def_struct_stype(sapi, "bIKParam");
  api_def_struct_ui_text(sapi, "IKParam", "Base type for IK solver parameters");
  api_def_struct_refine_fn(sapi, "api_IKParam_refine");

  prop = api_def_prop(sapi, "ik_solver", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "iksolver");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_enum_items(prop, prop_iksolver_items);
  api_def_prop_ui_text(prop, "IK Solver", "IK solver for which these parameters are defined");
}

/* pose.bone_groups */
static void api_def_bone_groups(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "BoneGroups");
  srna = api_def_struct(dapi, "BoneGroups", NULL);
  api_def_struct_stype(sapi, "bPose");
  api_def_struct_ui_text(sapi, "Bone Groups", "Collection of bone groups");

  fn = api_def_fn(sapi, "new", "rna_bone_group_new");
  api_def_fn_ui_description(fn, "Add a new bone group to the object");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FUNC_USE_REPORTS); /* ID needed for refresh */
  api_def_string(fn, "name", "Group", MAX_NAME, "", "Name of the new group");
  /* return type */
  parm = api_def_ptr(fn, "group", "BoneGroup", "", "New bone group");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_bone_group_remove");
  api_def_fn_ui_description(fn, "Remove a bone group from this object");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_SELF_ID); /* ID needed for refresh */
  /* bone group to remove */
  parm = api_def_ptr(fn, "group", "BoneGroup", "", "Removed bone group");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "BoneGroup");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(
      prop, "api_Pose_active_bone_group_get", "api_Pose_active_bone_group_set", NULL, NULL);
  api_def_prop_ui_text(prop, "Active Bone Group", "Active bone group for this pose");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_update");

  prop = api_def_prop(sapi, "active_index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "active_group");
  api_def_prop_int_fns(prop,
                       "api_Pose_active_bone_group_index_get",
                       "api_Pose_active_bone_group_index_set",
                       "api_Pose_active_bone_group_index_range");
  api_def_prop_ui_text(prop, "Active Bone Group Index", "Active index in bone groups array");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "rna_Pose_update");
}

static void api_def_pose(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* struct definition */
  sapi = api_def_struct(dapi, "Pose", NULL);
  api_def_struct_stype(sapi, "Pose");
  api_def_struct_ui_text(
      sapi, "Pose", "A collection of pose channels, including settings for animating bones");

  /* pose channels */
  prop = api_def_prop(sapi, "bones", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "chanbase", NULL);
  api_def_prop_struct_type(prop, "PoseBone");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Pose Bones", "Individual pose bones for the armature");
  /* can be removed, only for fast lookup */
  api_def_prop_collection_fns(
      prop, NULL, NULL, NULL, NULL, NULL, NULL, "api_PoseBones_lookup_string", NULL);

  /* bone groups */
  prop = api_def_prop(sapi, "bone_groups", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "agroups", NULL);
  api_def_prop_struct_type(prop, "BoneGroup");
  api_def_prop_ui_text(prop, "Bone Groups", "Groups of the bones");
  api_def_bone_groups(dapi, prop);

  /* ik solvers */
  prop = api_def_prop(stype, "ik_solver", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "iksolver");
  api_def_prop_enum_fns(prop, NULL, "api_Pose_ik_solver_set", NULL);
  api_def_prop_enum_items(prop, prop_iksolver_items);
  api_def_prop_ui_text(prop, "IK Solver", "Selection of IK solver for IK chain");
  api_def_prop_update(prop, NC_OBJECT | ND_POSE, "api_Pose_ik_solver_update");

  prop = api_def_prop(sapi, "ik_param", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "IKParam");
  api_def_prop_ptr_fns(
      prop, "api_Pose_ikparam_get", NULL, "api_Pose_ikparam_typef", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "IK Param", "Params for IK solver");

  api_define_lib_overridable(true);

  /* pose edit options */
  prop = api_def_prop(sapi, "use_mirror_x", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", POSE_MIRROR_EDIT);
  api_def_prop_ui_text(
      prop, "X-Axis Mirror", "Apply changes to matching bone on opposite side of X-Axis");
  api_def_struct_path_fn(sapi, "api_Pose_path");
  api_def_prop_update(prop, 0, "api_Pose_update");
  api_def_prop_flag(prop, PROP_LIB_EXCEPTION);

  prop = api_def_prop(sapi, "use_mirror_relative", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", POSE_MIRROR_RELATIVE);
  api_def_prop_ui_text(
      prop,
      "Relative Mirror",
      "Apply relative transformations in X-mirror mode (not supported with Auto IK)");
  api_def_struct_path_fn(sapi, "api_Pose_path");
  api_def_prop_update(prop, 0, "api_Pose_update");
  api_def_prop_flag(prop, PROP_LIB_EXCEPTION);

  prop = api_def_prop(sapi, "use_auto_ik", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", POSE_AUTO_IK);
  api_def_prop_ui_text(
      prop, "Auto IK", "Add temporary IK constraints while grabbing bones in Pose Mode");
  api_def_struct_path_fn(sapi, "api_Pose_path");
  api_def_prop_update(prop, 0, "api_Pose_update");
  api_def_prop_flag(prop, PROP_LIB_EXCEPTION);

  api_define_lib_overridable(false);

  /* animviz */
  api_def_animviz_common(sapi);

  api_api_pose(sapi);
}

void api_def_pose(DuneApi *dapi)
{
  api_def_pose(dapi);
  api_def_pose_channel(dapi);
  api_def_pose_ikparam(dapi);
  api_def_pose_itasc(dapi);
  api_def_bone_group(dapi);
}

#endif
