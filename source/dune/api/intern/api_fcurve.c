#include <stdlib.h>

#include "types_anim.h"
#include "types_curve.h"
#include "types_object.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lang_translation.h"

#include "dune_action.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_types.h"

#include "ed_keyframes_edit.h"
#include "ed_keyframing.h"

const EnumPropItem api_enum_fmod_type_items[] = {
    {FMOD_TYPE_NULL, "NULL", 0, "Invalid", ""},
    {FMOD_TYPE_GENERATOR,
     "GENERATOR",
     0,
     "Generator",
     "Generate a curve using a factorized or expanded polynomial"},
    {FMOD_TYPE_FN_GENERATOR,
     "FNGENERATOR",
     0,
     "Built-In Function",
     "Generate a curve using standard math functions such as sin and cos"},
    {FMOD_TYPE_ENVELOPE,
     "ENVELOPE",
     0,
     "Envelope",
     "Reshape F-Curve values, e.g. change amplitude of movements"},
    {FMOD_TYPE_CYCLES, "CYCLES", 0, "Cycles", "Cyclic extend/repeat keyframe sequence"},
    {FMOD_TYPE_NOISE, "NOISE", 0, "Noise", "Add pseudo-random noise on top of F-Curves"},
    // {FMOD_TYPE_FILTER, "FILTER", 0, "Filter", ""}, /* FIXME: not implemented yet! */
    // {FMOD_TYPE_PYTHON, "PYTHON", 0, "Python", ""}, /* FIXME: not implemented yet! */
    {FMOD_TYPE_LIMITS,
     "LIMITS",
     0,
     "Limits",
     "Restrict maximum and minimum values of F-Curve"},
    {FMOD_TYPE_STEPPED,
     "STEPPED",
     0,
     "Stepped Interpolation",
     "Snap values to nearest grid step, e.g. for a stop-motion look"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_fcurve_auto_smoothing_items[] = {
    {FCURVE_SMOOTH_NONE,
     "NONE",
     0,
     "None",
     "Automatic handles only take immediately adjacent keys into account"},
    {FCURVE_SMOOTH_CONT_ACCEL,
     "CONT_ACCEL",
     0,
     "Continuous Acceleration",
     "Automatic handles are adjusted to avoid jumps in acceleration, resulting "
     "in smoother curves. However, key changes may affect interpolation over a "
     "larger stretch of the curve"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_beztriple_keyframe_type_items[] = {
    {BEZT_KEYTYPE_KEYFRAME,
     "KEYFRAME",
     ICON_KEYTYPE_KEYFRAME_VEC,
     "Keyframe",
     "Normal keyframe, e.g. for key poses"},
    {BEZT_KEYTYPE_BREAKDOWN,
     "BREAKDOWN",
     ICON_KEYTYPE_BREAKDOWN_VEC,
     "Breakdown",
     "A breakdown pose, e.g. for transitions between key poses"},
    {BEZT_KEYTYPE_MOVEHOLD,
     "MOVING_HOLD",
     ICON_KEYTYPE_MOVING_HOLD_VEC,
     "Moving Hold",
     "A keyframe that is part of a moving hold"},
    {BEZT_KEYTYPE_EXTREME,
     "EXTREME",
     ICON_KEYTYPE_EXTREME_VEC,
     "Extreme",
     "An \"extreme\" pose, or some other purpose as needed"},
    {BEZT_KEYTYPE_JITTER,
     "JITTER",
     ICON_KEYTYPE_JITTER_VEC,
     "Jitter",
     "A filler or baked keyframe for keying on ones, or some other purpose as needed"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem rna_enum_beztriple_interpolation_easing_items[] = {
    /* XXX: auto-easing is currently using a placeholder icon... */
    {BEZT_IPO_EASE_AUTO,
     "AUTO",
     ICON_IPO_EASE_IN_OUT,
     "Automatic Easing",
     "Easing type is chosen automatically based on what the type of interpolation used "
     "(e.g. Ease In for transitional types, and Ease Out for dynamic effects)"},

    {BEZT_IPO_EASE_IN,
     "EASE_IN",
     ICON_IPO_EASE_IN,
     "Ease In",
     "Only on the end closest to the next keyframe"},
    {BEZT_IPO_EASE_OUT,
     "EASE_OUT",
     ICON_IPO_EASE_OUT,
     "Ease Out",
     "Only on the end closest to the first keyframe"},
    {BEZT_IPO_EASE_IN_OUT,
     "EASE_IN_OUT",
     ICON_IPO_EASE_IN_OUT,
     "Ease In and Out",
     "Segment between both keyframes"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_driver_target_rotation_mode_items[] = {
    {DTAR_ROTMODE_AUTO, "AUTO", 0, "Auto Euler", "Euler using the rotation order of the target"},
    {DTAR_ROTMODE_EULER_XYZ, "XYZ", 0, "XYZ Euler", "Euler using the XYZ rotation order"},
    {DTAR_ROTMODE_EULER_XZY, "XZY", 0, "XZY Euler", "Euler using the XZY rotation order"},
    {DTAR_ROTMODE_EULER_YXZ, "YXZ", 0, "YXZ Euler", "Euler using the YXZ rotation order"},
    {DTAR_ROTMODE_EULER_YZX, "YZX", 0, "YZX Euler", "Euler using the YZX rotation order"},
    {DTAR_ROTMODE_EULER_ZXY, "ZXY", 0, "ZXY Euler", "Euler using the ZXY rotation order"},
    {DTAR_ROTMODE_EULER_ZYX, "ZYX", 0, "ZYX Euler", "Euler using the ZYX rotation order"},
    {DTAR_ROTMODE_QUATERNION, "QUATERNION", 0, "Quaternion", "Quaternion rotation"},
    {DTAR_ROTMODE_SWING_TWIST_X,
     "SWING_TWIST_X",
     0,
     "Swing and X Twist",
     "Decompose into a swing rotation to aim the X axis, followed by twist around it"},
    {DTAR_ROTMODE_SWING_TWIST_Y,
     "SWING_TWIST_Y",
     0,
     "Swing and Y Twist",
     "Decompose into a swing rotation to aim the Y axis, followed by twist around it"},
    {DTAR_ROTMODE_SWING_TWIST_Z,
     "SWING_TWIST_Z",
     0,
     "Swing and Z Twist",
     "Decompose into a swing rotation to aim the Z axis, followed by twist around it"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "wm_api.h"

static ApiStruct *api_FModType_refine(struct ApiPtr *ptr)
{
  FMod *fcm = (FMod *)ptr->data;

  switch (fcm->type) {
    case FMOD_TYPE_GENERATOR:
      return &ApiFModifierGenerator;
    case FMOD_TYPE_FN_GENERATOR:
      return &ApiFModFnGenerator;
    case FMOD_TYPE_ENVELOPE:
      return &ApiFModEnvelope;
    case FMOD_TYPE_CYCLES:
      return &ApiFModCycles;
    case FMOD_TYPE_NOISE:
      return &ApiFModNoise;
#  if 0
    case FMOD_TYPE_FILTER:
      return &ApiFModFilter;
#  endif
    case FMOD_TYPE_PYTHON:
      return &ApiFModPython;
    case FMOD_TYPE_LIMITS:
      return &ApiFModLimits;
    case FMOD_TYPE_STEPPED:
      return &ApiFModStepped;
    default:
      return &ApiUnknownType;
  }
}

/* ****************************** */

#  include "dune_anim_data.h"
#  include "dune_fcurve.h"
#  include "dune_fcurve_driver.h"

#  include "graph.h"
#  include "graph_build.h"

static bool api_ChannelDriver_is_simple_expression_get(ApiPtr *ptr)
{
  ChannelDriver *driver = ptr->data;

  return dune_driver_has_simple_expression(driver);
}

static void api_ChannelDriver_update_data(Main *main, Scene *scene, ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  ChannelDriver *driver = ptr->data;

  driver->flag &= ~DRIVER_FLAG_INVALID;

  /* TODO: this really needs an update guard... */
  graph_relations_tag_update(main);
  graph_id_tag_update(id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);

  wm_main_add_notifier(NC_SCENE | ND_FRAME, scene);
}

static void api_ChannelDriver_update_expr(Main *main, Scene *scene, ApiPtr *ptr)
{
  ChannelDriver *driver = ptr->data;

  /* tag driver as needing to be recompiled */
  dune_driver_invalidate_expression(driver, true, false);

  /* update_data() clears invalid flag and schedules for updates */
  api_ChannelDriver_update_data(main, scene, ptr);
}

static void api_DriverTarget_update_data(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  ApiPtr driverptr;
  ChannelDriver *driver;
  FCurve *fcu;
  AnimData *adt = dune_animdata_from_id(ptr->owner_id);

  /* find the driver this belongs to and update it */
  for (fcu = adt->drivers.first; fcu; fcu = fcu->next) {
    driver = fcu->driver;
    fcu->flag &= ~FCURVE_DISABLED;

    if (driver) {
      /* FIXME: need to be able to search targets for required one. */
      // lib_findindex(&driver->targets, ptr->data) != -1)
      api_ptr_create(ptr->owner_id, &ApiDriver, driver, &driverptr);
      api_ChannelDriver_update_data(main, scene, &driverptr);
    }
  }
}

static void api_DriverTarget_update_name(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  ChannelDriver *driver = ptr->data;
  api_DriverTarget_update_data(bmain, scene, ptr);

  dune_driver_invalidate_expression(driver, false, true);
}

/* ----------- */

/* NOTE: this function exists only to avoid id refcounting. */
static void api_DriverTarget_id_set(ApiPtr *ptr,
                                    ApiPtr value,
                                    struct ReportList *UNUSED(reports))
{
  DriverTarget *dtar = (DriverTarget *)ptr->data;
  dtar->id = value.data;
}

static ApiStruct *api_DriverTarget_id_typef(ApiPtr *ptr)
{
  DriverTarget *dtar = (DriverTarget *)ptr->data;
  return id_code_to_api_type(dtar->idtype);
}

static int api_DriverTarget_id_editable(ApiPtr *ptr, const char **UNUSED(r_info))
{
  DriverTarget *dtar = (DriverTarget *)ptr->data;
  return (dtar->idtype) ? PROP_EDITABLE : 0;
}

static int api_DriverTarget_id_type_editable(ApiPtr *ptr, const char **UNUSED(r_info))
{
  DriverTarget *dtar = (DriverTarget *)ptr->data;

  /* when the id-type can only be object, don't allow editing
   * otherwise, there may be strange crashes */
  return ((dtar->flag & DTAR_FLAG_ID_OB_ONLY) == 0);
}

static void api_DriverTarget_id_type_set(ApiPtr *ptr, int value)
{
  DriverTarget *data = (DriverTarget *)(ptr->data);

  /* check if ID-type is settable */
  if ((data->flag & DTAR_FLAG_ID_OB_ONLY) == 0) {
    /* change ID-type to the new type */
    data->idtype = value;
  }
  else {
    /* make sure ID-type is Object */
    data->idtype = ID_OB;
  }

  /* clear the id-block if the type is invalid */
  if ((data->id) && (GS(data->id->name) != data->idtype)) {
    data->id = NULL;
  }
}

static void api_DriverTarget_ApiPath_get(ApiPtr *ptr, char *value)
{
  DriverTarget *dtar = (DriverTarget *)ptr->data;

  if (dtar->api_path) {
    strcpy(value, dtar->api_path);
  }
  else {
    value[0] = '\0';
  }
}

static int api_DriverTarget_ApiPath_length(ApiPtr *ptr)
{
  DriverTarget *dtar = (DriverTarget *)ptr->data;

  if (dtar->api_path) {
    return strlen(dtar->api_path);
  }
  else {
    return 0;
  }
}

static void api_DriverTarget_ApiPath_set(ApiPtr *ptr, const char *value)
{
  DriverTarget *dtar = (DriverTarget *)ptr->data;

  /* XXX in this case we need to be very careful,
   * as this will require some new dependencies to be added! */
  if (dtar->api_path) {
    mem_freen(dtar->api_path);
  }

  if (value[0]) {
    dtar->api_path = lib_strdup(value);
  }
  else {
    dtar->api_path = NULL;
  }
}

static void api_DriverVariable_type_set(ApiPtr *ptr, int value)
{
  DriverVar *dvar = (DriverVar *)ptr->data;

  /* call the API function for this */
  driver_change_variable_type(dvar, value);
}

void api_DriverVariable_name_set(ApiPtr *ptr, const char *value)
{
  DriverVar *data = (DriverVar *)(ptr->data);

  lib_strncpy_utf8(data->name, value, 64);
  driver_variable_name_validate(data);
  driver_variable_unique_name(data);
}

/* ----------- */

static DriverVar *api_Driver_new_variable(ChannelDriver *driver)
{
  /* call the API function for this */
  return driver_add_new_variable(driver);
}

static void api_Driver_remove_variable(ChannelDriver *driver,
                                       ReportList *reports,
                                       ApiPtr *dvar_ptr)
{
  DriverVar *dvar = dvar_ptr->data;
  if (lib_findindex(&driver->variables, dvar) == -1) {
    dune_report(reports, RPT_ERROR, "Variable does not exist in this driver");
    return;
  }

  driver_free_variable_ex(driver, dvar);
  API_PTR_INVALIDATE(dvar_ptr);
}

/* ****************************** */

static void api_FKeyframe_handle1_get(PointerRNA *ptr, float *values)
{
  BezTriple *bezt = (BezTriple *)ptr->data;

  values[0] = bezt->vec[0][0];
  values[1] = bezt->vec[0][1];
}

static void api_FKeyframe_handle1_set(ApiPtr *ptr, const float *values)
{
  BezTriple *bezt = (BezTriple *)ptr->data;

  bezt->vec[0][0] = values[0];
  bezt->vec[0][1] = values[1];
}

static void api_FKeyframe_handle2_get(ApiPtr *ptr, float *values)
{
  BezTriple *bezt = (BezTriple *)ptr->data;

  values[0] = bezt->vec[2][0];
  values[1] = bezt->vec[2][1];
}

static void api_FKeyframe_handle2_set(ApiPtr *ptr, const float *values)
{
  BezTriple *bezt = (BezTriple *)ptr->data;

  bezt->vec[2][0] = values[0];
  bezt->vec[2][1] = values[1];
}

static void api_FKeyframe_ctrlpoint_get(PointerRNA *ptr, float *values)
{
  BezTriple *bezt = (BezTriple *)ptr->data;

  values[0] = bezt->vec[1][0];
  values[1] = bezt->vec[1][1];
}

static void api_FKeyframe_ctrlpoint_set(PointerRNA *ptr, const float *values)
{
  BezTriple *bezt = (BezTriple *)ptr->data;

  bezt->vec[1][0] = values[0];
  bezt->vec[1][1] = values[1];
}

static void api_FKeyframe_ctrlpoint_ui_set(PointerRNA *ptr, const float *values)
{
  BezTriple *bezt = (BezTriple *)ptr->data;

  const float frame_delta = values[0] - bezt->vec[1][0];
  const float value_delta = values[1] - bezt->vec[1][1];

  /** To match the behavior of transforming the keyframe Co using the Graph Editor
   * (transform_convert_graph.c) flushTransGraphData(), we will also move the handles by
   * the same amount as the Co delta. */

  bezt->vec[0][0] += frame_delta;
  bezt->vec[0][1] += value_delta;

  bezt->vec[1][0] = values[0];
  bezt->vec[1][1] = values[1];

  bezt->vec[2][0] += frame_delta;
  bezt->vec[2][1] += value_delta;
}

/* ****************************** */

static void api_FCurve_ApiPath_get(ApiPtr *ptr, char *value)
{
  FCurve *fcu = (FCurve *)ptr->data;

  if (fcu->api_path) {
    strcpy(value, fcu->api_path);
  }
  else {
    value[0] = '\0';
  }
}

static int api_FCurve_ApiPath_length(ApiPtr *ptr)
{
  FCurve *fcu = (FCurve *)ptr->data;

  if (fcu->api_path) {
    return strlen(fcu->api_path);
  }
  else {
    return 0;
  }
}

static void api_FCurve_ApiPath_set(PointerRNA *ptr, const char *value)
{
  FCurve *fcu = (FCurve *)ptr->data;

  if (fcu->api_path) {
    mem_freen(fcu->api_path);
  }

  if (value[0]) {
    fcu->api_path = lib_strdup(value);
    fcu->flag &= ~FCURVE_DISABLED;
  }
  else {
    fcu->api_path = NULL;
  }
}

static void api_FCurve_group_set(ApiPtr *ptr,
                                 ApiPtr value,
                                 struct ReportList *UNUSED(reports))
{
  Id *pid = ptr->owner_id;
  Id *vid = value.owner_id;
  FCurve *fcu = ptr->data;
  Action *act = NULL;

  /* get action */
  if (ELEM(NULL, pid, vid)) {
    printf("ERROR: one of the ID's for the groups to assign to is invalid (ptr=%p, val=%p)\n",
           pid,
           vid);
    return;
  }
  else if (value.data && (pid != vid)) {
    /* ids differ, can't do this, should raise an error */
    printf("ERROR: IDs differ - ptr=%p vs value=%p\n", pid, vid);
    return;
  }

  if (GS(pid->name) == ID_AC && GS(vid->name) == ID_AC) {
    /* The ID given is the action already -
     * usually when F-Curve is obtained from an action's pointer. */
    act = (Action *)pid;
  }
  else {
    /* the ID given is the owner of the F-Curve (for drivers) */
    AnimData *adt = dune_animdata_from_id(ptr->owner_id);
    act = (adt) ? adt->action : NULL;
  }

  /* already belongs to group? */
  if (fcu->grp == value.data) {
    /* nothing to do */
    printf("ERROR: F-Curve already belongs to this group\n");
    return;
  }

  /* can only change group if we have info about the action the F-Curve is in
   * (i.e. for drivers or random F-Curves, this cannot be done)
   */
  if (act == NULL) {
    /* can't change the grouping of F-Curve when it doesn't belong to an action */
    printf("ERROR: cannot assign F-Curve to group, since F-Curve is not attached to any ID\n");
    return;
  }
  /* make sure F-Curve exists in this action first, otherwise we could still have been tricked */
  else if (lib_findindex(&act->curves, fcu) == -1) {
    printf("ERROR: F-Curve (%p) doesn't exist in action '%s'\n", fcu, act->id.name);
    return;
  }

  /* try to remove F-Curve from action (including from any existing groups) */
  action_groups_remove_channel(act, fcu);

  /* add the F-Curve back to the action now in the right place */
  /* TODO: make the api function handle the case where there isn't any group to assign to. */
  if (value.data) {
    /* add to its group using API function, which makes sure everything goes ok */
    action_groups_add_channel(act, value.data, fcu);
  }
  else {
    /* Need to add this back, but it can only go at the end of the list
     * (or else will corrupt groups). */
    lib_addtail(&act->curves, fcu);
  }
}

/* calculate time extents of F-Curve */
static void api_FCurve_range(FCurve *fcu, float range[2])
{
  dune_fcurve_calc_range(fcu, range, range + 1, false, false);
}

static bool api_FCurve_is_empty_get(ApiPtr *ptr)
{
  FCurve *fcu = (FCurve *)ptr->data;
  return dune_fcurve_is_empty(fcu);
}

static void api_tag_animation_update(Main *main, Id *id)
{
  const int tags = ID_RECALC_ANIMATION;
  AnimData *adt = dune_animdata_from_id(id);

  if (adt && adt->action) {
    /* Action is separate datablock, needs separate tag. */
    graph_id_tag_update_ex(main, &adt->action->id, tags);
  }

  graph_id_tag_update_ex(main, id, tags);
}

/* allow scripts to update curve after editing manually */
static void api_FCurve_update_data_ex(Id *id, FCurve *fcu, Main *main)
{
  sort_time_fcurve(fcu);
  calchandles_fcurve(fcu);

  api_tag_animation_update(main, id);
}

/* api update callback for F-Curves after curve shape changes */
static void api_FCurve_update_data(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  lib_assert(ptr->type == &ApiFCurve);
  api_FCurve_update_data_ex(ptr->owner_id, (FCurve *)ptr->data, main);
}

static void api_FCurve_update_data_relations(Main *main,
                                             Scene *UNUSED(scene),
                                             ApiPtr *UNUSED(ptr))
{
  graph_relations_tag_update(main);
}

/* api update callback for F-Curves to indicate that there are copy-on-write tagging/flushing
 * needed (e.g. for properties that affect how animation gets evaluated) */
static void api_FCurve_update_eval(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  api_tag_animation_update(main, ptr->owner_id);
}

static ApiPtr api_FCurve_active_mod_get(ApiPtr *ptr)
{
  FCurve *fcu = (FCurve *)ptr->data;
  FMod *fcm = find_active_fmod(&fcu->mods);
  return api_ptr_inherit_refine(ptr, &ApiFMod, fcm);
}

static void api_FCurve_active_mod_set(ApiPtr *ptr,
                                      ApiPtr value,
                                      struct ReportList *UNUSED(reports))
{
  FCurve *fcu = (FCurve *)ptr->data;
  set_active_fmod(&fcu->mods, (FMod *)value.data);
}

static FMod *api_FCurve_mods_new(FCurve *fcu, int type)
{
  return add_fmod(&fcu->mods, type, fcu);
}

static void api_FCurve_mods_remove(FCurve *fcu, ReportList *reports, ApiPtr *fcm_ptr)
{
  FMod *fcm = fcm_ptr->data;
  if (lib_findindex(&fcu->mods, fcm) == -1) {
    dune_reportf(reports, RPT_ERROR, "F-Curve modifier '%s' not found in F-Curve", fcm->name);
    return;
  }

  remove_fmod(&fcu->mods, fcm);
  API_PTR_INVALIDATE(fcm_ptr);
}

static void api_FMod_active_set(ApiPtr *ptr, bool UNUSED(value))
{
  FMod *fcm = (FMod *)ptr->data;

  /* don't toggle, always switch on */
  fcm->flag |= FMOD_FLAG_ACTIVE;
}

static void api_FMod_start_frame_set(PointerRNA *ptr, float value)
{
  FMod *fcm = (FMod *)ptr->data;

  CLAMP(value, MINAFRAMEF, MAXFRAMEF);
  fcm->sfra = value;

  /* XXX: maintain old offset? */
  if (fcm->sfra >= fcm->efra) {
    fcm->efra = fcm->sfra;
  }
}

static void api_FMod_end_frame_set(ApiPtr *ptr, float value)
{
  FMod *fcm = (FMod *)ptr->data;

  CLAMP(value, MINAFRAMEF, MAXFRAMEF);
  fcm->efra = value;

  /* XXX: maintain old offset? */
  if (fcm->efra <= fcm->sfra) {
    fcm->sfra = fcm->efra;
  }
}

static void api_FMod_start_frame_range(PointerRNA *UNUSED(ptr),
                                            float *min,
                                            float *max,
                                            float *UNUSED(softmin),
                                            float *UNUSED(softmax))
{
  // FModifier *fcm = (FModifier *)ptr->data;

  /* Technically, "sfra <= efra" must hold; however, we can't strictly enforce that,
   * or else it becomes tricky to adjust the range, see: T36844.
   *
   * NOTE: we do not set soft-limits on lower bounds, as it's too confusing when you
   *       can't easily use the slider to set things here
   */
  *min = MINAFRAMEF;
  *max = MAXFRAMEF;
}

static void api_FMod_end_frame_range(
    ApiPtr *ptr, float *min, float *max, float *softmin, float *softmax)
{
  FMod *fcm = (FMod *)ptr->data;

  /* Technically, "sfra <= efra" must hold; however, we can't strictly enforce that,
   * or else it becomes tricky to adjust the range, see: T36844. */
  *min = MINAFRAMEF;
  *softmin = (fcm->flag & FMOD_FLAG_RANGERESTRICT) ? fcm->sfra : MINAFRAMEF;

  *softmax = MAXFRAMEF;
  *max = MAXFRAMEF;
}

static void api_FMod_blending_range(
    ApiPtr *ptr, float *min, float *max, float *UNUSED(softmin), float *UNUSED(softmax))
{
  FMod *fcm = (FMod *)ptr->data;

  *min = 0.0f;
  *max = fcm->efra - fcm->sfra;
}

static void api_FMod_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  FMod *fcm = (FMod *)ptr->data;

  if (fcm->curve && fcm->type == FMOD_TYPE_CYCLES) {
    calchandles_fcurve(fcm->curve);
  }

  api_tag_animation_update(main, id);
}

static void api_FMod_verify_data_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  FMod *fcm = (FMod *)ptr->data;
  const FModTypeInfo *fmi = fmod_get_typeinfo(fcm);

  /* call the verify callback on the modifier if applicable */
  if (fmi && fmi->verify_data) {
    fmi->verify_data(fcm);
  }

  api_FMod_update(main, scene, ptr);
}

static void api_FMod_active_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  FMod *fm, *fmo = (FMod *)ptr->data;

  /* clear active state of other FModifiers in this list */
  for (fm = fmo->prev; fm; fm = fm->prev) {
    fm->flag &= ~FMOD_FLAG_ACTIVE;
  }
  for (fm = fmo->next; fm; fm = fm->next) {
    fm->flag &= ~FMOD_FLAG_ACTIVE;
  }

  api_FMod_update(main, scene, ptr);
}

static int api_FModGenerator_coefficients_get_length(ApiPtr *ptr,
                                                     int length[Api_MAX_ARRAY_DIMENSION])
{
  FMod *fcm = (FMod *)ptr->data;
  FModGenerator *gen = fcm->data;

  if (gen) {
    length[0] = gen->arraysize;
  }
  else {
    length[0] = 0;
  }

  return length[0];
}

static void api_FModGenerator_coefficients_get(ApiPtr *ptr, float *values)
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Generator *gen = fcm->data;
  memcpy(values, gen->coefficients, gen->arraysize * sizeof(float));
}

static void api_FModGenerator_coefficients_set(ApiPtr *ptr, const float *values)
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Generator *gen = fcm->data;
  memcpy(gen->coefficients, values, gen->arraysize * sizeof(float));
}

static void api_FModLimits_minx_set(ApiPtr *ptr, float value)
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Limits *data = fcm->data;

  data->rect.xmin = value;

  if (data->rect.xmin >= data->rect.xmax) {
    data->rect.xmax = data->rect.xmin;
  }
}

static void api_FModLimits_maxx_set(ApiPtr *ptr, float value)
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Limits *data = fcm->data;

  data->rect.xmax = value;

  if (data->rect.xmax <= data->rect.xmin) {
    data->rect.xmin = data->rect.xmax;
  }
}

static void api_FModLimits_miny_set(ApiPtr *ptr, float value)
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Limits *data = fcm->data;

  data->rect.ymin = value;

  if (data->rect.ymin >= data->rect.ymax) {
    data->rect.ymax = data->rect.ymin;
  }
}

static void api_FModLimits_maxy_set(PointerRNA *ptr, float value)
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Limits *data = fcm->data;

  data->rect.ymax = value;

  if (data->rect.ymax <= data->rect.ymin) {
    data->rect.ymin = data->rect.ymax;
  }
}

static void api_FModLimits_minx_range(ApiPtr *UNUSED(ptr),
                                           float *min,
                                           float *max,
                                           float *UNUSED(softmin),
                                           float *UNUSED(softmax))
{
  // FMod *fcm = (FMod *)ptr->data;
  // FMod_Limits *data = fcm->data;

  /* No soft-limits on lower bound -
   * it's too confusing when you can't easily use the slider to set things here. */
  *min = MINAFRAMEF;
  *max = MAXFRAMEF;
}

static void api_FModLimits_maxx_range(
    ApiPtr *ptr, float *min, float *max, float *softmin, float *softmax)
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Limits *data = fcm->data;

  *min = MINAFRAMEF;
  *softmin = (data->flag & FCM_LIMIT_XMIN) ? data->rect.xmin : MINAFRAMEF;

  *softmax = MAXFRAMEF;
  *max = MAXFRAMEF;
}

static void api_FModLimits_miny_range(ApiPtr *UNUSED(ptr),
                                      float *min,
                                      float *max,
                                      float *UNUSED(softmin),
                                      float *UNUSED(softmax))
{
  // FMod *fcm = (FMod *)ptr->data;
  // FMod_Limits *data = fcm->data;

  /* No soft-limits on lower bound -
   * it's too confusing when you can't easily use the slider to set things here. */
  *min = -FLT_MAX;
  *max = FLT_MAX;
}

static void api_FModLimits_maxy_range(
    ApiPtr *ptr, float *min, float *max, float *softmin, float *softmax)
{
  FMod *fcm = (FMod;
  FMod_Limits *data = fcm->data;

  *min = -FLT_MAX;
  *softmin = (data->flag & FCM_LIMIT_YMIN) ? data->rect.ymin : -FLT_MAX;

  *softmax = FLT_MAX;
  *max = FLT_MAX;
}

static void api_FModStepped_start_frame_range(
    ApiPtr *ptr, float *min, float *max, float *UNUSED(softmin), float *UNUSED(softmax))
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Stepped *data = fcm->data;

  *min = MINAFRAMEF;
  *max = (data->flag & FCM_STEPPED_NO_AFTER) ? data->end_frame : MAXFRAMEF;
}

static void api_FModStepped_end_frame_range(
    ApiPtr *ptr, float *min, float *max, float *UNUSED(softmin), float *UNUSED(softmax))
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Stepped *data = fcm->data;

  *min = (data->flag & FCM_STEPPED_NO_BEFORE) ? data->start_frame : MINAFRAMEF;
  *max = MAXFRAMEF;
}

static void api_FModStepped_frame_start_set(ApiPtr *ptr, float value)
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Stepped *data = fcm->data;

  float prop_clamp_min = -FLT_MAX, prop_clamp_max = FLT_MAX, prop_soft_min, prop_soft_max;
  api_FModStepped_start_frame_range(
      ptr, &prop_clamp_min, &prop_clamp_max, &prop_soft_min, &prop_soft_max);
  value = CLAMPIS(value, prop_clamp_min, prop_clamp_max);

  /* Need to set both step-data's start/end and the start/end on the base-data,
   * or else Restrict-Range doesn't work due to Api-prop shadowing (T52009)
   */
  data->start_frame = value;
  fcm->sfra = value;
}

static void api_FModStepped_frame_end_set(ApiPtr *ptr, float value)
{
  FMod *fcm = (FMod *)ptr->data;
  FMod_Stepped *data = fcm->data;

  float prop_clamp_min = -FLT_MAX, prop_clamp_max = FLT_MAX, prop_soft_min, prop_soft_max;
  api_FModStepped_end_frame_range(
      ptr, &prop_clamp_min, &prop_clamp_max, &prop_soft_min, &prop_soft_max);
  value = CLAMPIS(value, prop_clamp_min, prop_clamp_max);

  /* Need to set both step-data's start/end and the start/end on the base-data,
   * or else Restrict-Range doesn't work due to RNA-property shadowing (T52009) */
  data->end_frame = value;
  fcm->efra = value;
}

static BezTriple *api_FKeyframe_points_insert(
    Id *id, FCurve *fcu, Main *main, float frame, float value, int keyframe_type, int flag)
{
  int index = insert_vert_fcurve(
      fcu, frame, value, (char)keyframe_type, flag | INSERTKEY_NO_USERPREF);

  if ((fcu->bezt) && (index >= 0)) {
    api_tag_animation_update(main, id);

    return fcu->bezt + index;
  }

  return NULL;
}

static void api_FKeyframe_points_add(Id *id, FCurve *fcu, Main *main, int tot)
{
  if (tot > 0) {
    BezTriple *bezt;

    fcu->bezt = mem_recallocn(fcu->bezt, sizeof(BezTriple) * (fcu->totvert + tot));

    bezt = fcu->bezt + fcu->totvert;
    fcu->totvert += tot;

    while (tot--) {
      /* Defaults, ignoring user-preference gives predictable results for API. */
      bezt->f1 = bezt->f2 = bezt->f3 = SELECT;
      bezt->ipo = BEZT_IPO_BEZ;
      bezt->h1 = bezt->h2 = HD_AUTO_ANIM;
      bezt++;
    }

    api_tag_animation_update(main, id);
  }
}

static void api_FKeyframe_points_remove(
    Id *id, FCurve *fcu, Main *main, ReportList *reports, ApiPtr *bezt_ptr, bool do_fast)
{
  BezTriple *bezt = bezt_ptr->data;
  int index = (int)(bezt - fcu->bezt);
  if (index < 0 || index >= fcu->totvert) {
    dune_report(reports, RPT_ERROR, "Keyframe not in F-Curve");
    return;
  }

  delete_fcurve_key(fcu, index, !do_fast);
  API_PTR_INVALIDATE(bezt_ptr);

  api_tag_animation_update(main, id);
}

static FCM_EnvelopeData *api_FModEnvelope_points_add(
    Id *id, FModifier *fmod, Main *main, ReportList *reports, float frame)
{
  FCM_EnvelopeData fed;
  FMod_Envelope *env = (FMod_Envelope *)fmod->data;
  int i;

  api_tag_animation_update(main, id);

  /* init template data */
  fed.min = -1.0f;
  fed.max = 1.0f;
  fed.time = frame;
  fed.f1 = fed.f2 = 0;

  if (env->data) {
    bool exists;
    i = dune_fcm_envelope_find_index(env->data, frame, env->totvert, &exists);
    if (exists) {
      dune_reportf(reports, RPT_ERROR, "Already a control point at frame %.6f", frame);
      return NULL;
    }

    /* realloc memory for extra point */
    env->data = (FCM_EnvelopeData *)mem_reallocn((void *)env->data,
                                                 (env->totvert + 1) * sizeof(FCM_EnvelopeData));

    /* move the points after the added point */
    if (i < env->totvert) {
      memmove(env->data + i + 1, env->data + i, (env->totvert - i) * sizeof(FCM_EnvelopeData));
    }

    env->totvert++;
  }
  else {
    env->data = mem_mallocn(sizeof(FCM_EnvelopeData), "FCM_EnvelopeData");
    env->totvert = 1;
    i = 0;
  }

  /* add point to paste at index i */
  *(env->data + i) = fed;
  return (env->data + i);
}

static void api_FModEnvelope_points_remove(
    Id *id, FMod *fmod, Main *main, ReportList *reports, ApiPtr *point)
{
  FCM_EnvelopeData *cp = point->data;
  FMod_Envelope *env = (FMod_Envelope *)fmod->data;

  int index = (int)(cp - env->data);

  /* test point is in range */
  if (index < 0 || index >= env->totvert) {
    dune_report(reports, RPT_ERROR, "Control point not in Envelope F-Modifier");
    return;
  }

  api_tag_animation_update(main, id);

  if (env->totvert > 1) {
    /* move data after the removed point */

    memmove(env->data + index,
            env->data + (index + 1),
            sizeof(FCM_EnvelopeData) * ((env->totvert - index) - 1));

    /* realloc smaller array */
    env->totvert--;
    env->data = (FCM_EnvelopeData *)mem_reallocn((void *)env->data,
                                                 (env->totvert) * sizeof(FCM_EnvelopeData));
  }
  else {
    /* just free array, since the only vert was deleted */
    if (env->data) {
      mem_freen(env->data);
      env->data = NULL;
    }
    env->totvert = 0;
  }
  API_PTR_INVALIDATE(point);
}

static void api_Keyframe_update(Main *main, Scene *UNUSED(scene), ApiPtr *ptr)
{
  api_tag_animation_update(main, ptr->owner_id);
}

static void api_FMod_show_expanded_set(ApiPtr *ptr, bool value)
{
  FMod *fcm = ptr->data;
  SET_FLAG_FROM_TEST(fcm->ui_expand_flag, value, UI_PANEL_DATA_EXPAND_ROOT);
}

#else

static void api_def_fmod_generator(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem generator_mode_items[] = {
      {FCM_GENERATOR_POLYNOMIAL, "POLYNOMIAL", 0, "Expanded Polynomial", ""},
      {FCM_GENERATOR_POLYNOMIAL_FACTORISED,
       "POLYNOMIAL_FACTORISED",
       0,
       "Factorized Polynomial",
       ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "FModGenerator", "FMod");
  api_def_struct_ui_text(
      sapi, "Generator F-Mod", "Deterministically generate values for the modified F-Curve");
  api_def_struct_stype_from(sapi, "FMod_Generator", "data");

  /* define common props */
  prop = api_def_prop(sapi, "use_additive", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", FCM_GENERATOR_ADDITIVE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop,
                       "Additive",
                       "Values generated by this modifier are applied on top of "
                       "the existing values instead of overwriting them");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "api_FMod_update");

  prop = api_def_prop(sapi, "mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, generator_mode_items);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Mode", "Type of generator to use");
  api_def_prop_update(
      prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "api_FMod_verify_data_update");

  /* order of the polynomial */
  prop = api_def_prop(sapi, "poly_order", PROP_INT, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(
      prop,
      "Polynomial Order",
      "The highest power of 'x' for this polynomial (number of coefficients - 1)");
  api_def_prop_range(prop, 1, 100);
  api_def_prop_update(
      prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "api_FMod_verify_data_update");

  /* coefficients array */
  prop = api_def_prop(sapi, "coefficients", PROP_FLOAT, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_array(prop, 32);
  api_def_prop_flag(prop, PROP_DYNAMIC);
  api_def_prop_dynamic_array_fns(prop, "api_FModGenerator_coefficients_get_length");
  api_def_prop_float_fns(prop,
                         "api_FModGenerator_coefficients_get",
                         "api_FModGenerator_coefficients_set",
                         NULL);
  api_def_prop_ui_text(
      prop, "Coefficients", "Coefficients for 'x' (starting from lowest power of x^0)");
  api_def_prop_update(
      prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_verify_data_update");
}

/* --------- */

static void api_def_fmod_fn_generator(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp prop;

  static const EnumPropItem prop_type_items[] = {
      {0, "SIN", 0, "Sine", ""},
      {1, "COS", 0, "Cosine", ""},
      {2, "TAN", 0, "Tangent", ""},
      {3, "SQRT", 0, "Square Root", ""},
      {4, "LN", 0, "Natural Logarithm", ""},
      {5, "SINC", 0, "Normalized Sine", "sin(x) / x"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "FModFnGenerator", "FMod");
  api_def_struct_ui_text(
      sapi, "Built-In Function F-Mod", "Generate values using a built-in function");
  api_def_struct_stype_from(sapi, "FMod_FnGenerator", "data");

  /* coefficients */
  prop = api_def_prop(sapi, "amplitude", PROP_FLOAT, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(
      prop, "Amplitude", "Scale factor determining the maximum/minimum values");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "api_FMod_update");

  prop = api_def_prop(sapi, "phase_multiplier", PROP_FLOAT, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(
      prop, "Phase Multiple", "Scale factor determining the 'speed' of the function");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "api_FMod_update");

  prop = api_def_prop(sapi, "phase_offset", PROP_FLOAT, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop, "Phase Offset", "Constant factor to offset time by for function");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "api_FMod_update");

  prop = api_def_prop(sapi, "value_offset", PROP_FLOAT, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(prop, "Value Offset", "Constant factor to offset values by");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "api_FMod_update");

  /* flags */
  prop = api_def_prop(sapi, "use_additive", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_bool_stype(prop, NULL, "flag", FCM_GENERATOR_ADDITIVE);
  api_def_prop_ui_text(prop,
                       "Additive",
                       "Values generated by this modifier are applied on top of "
                       "the existing values instead of overwriting them");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = api_def_prop(sapi, "fn_type", PROP_ENUM, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_enum_sdna(prop, NULL, "type");
  api_def_prop_enum_items(prop, prop_type_items);
  api_def_prop_ui_text(prop, "Type", "Type of built-in function to use");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");
}

/* --------- */

static void api_def_fmod_envelope_ctrl(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "FModifierEnvelopeControlPoint", NULL);
  api_def_struct_ui_text(sapi, "Envelope Control Point", "Control point for envelope F-Modifier");
  api_def_struct_stype(sapi, "FCM_EnvelopeData");

  /* min/max extents
   * - for now, these are allowed to go past each other, so that we can have inverted action
   * - technically, the range is limited by the settings in the envelope-modifier data, not here...
   */
  prop = RNA_def_property(srna, "min", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "min");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Minimum Value", "Lower bound of envelope at this control-point");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "max", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "max");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Maximum Value", "Upper bound of envelope at this control-point");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  /* Frame */
  prop = api_def_prop(sapi, "frame", PROP_FLOAT, PROP_TIME);
  api_def_prop_float_stype(prop, NULL, "time");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Frame", "Frame this control-point occurs on");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  /* TODO: */
  /* - selection flags (not implemented in UI yet though) */
}

static void api_def_fmod_envelope_control_points(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "FModEnvelopeControlPoints");
  sapi = api_def_struct(dapi, "FModEnvelopeControlPoints", NULL);
  api_def_struct_stype(sapi, "FMod");

  api_def_struct_ui_text(
      sapi, "Control Points", "Control points defining the shape of the envelope");

  fn = api_def_fn(sapi, "add", "api_FModEnvelope_points_add");
  api_def_fn_ui_description(fn, "Add a control point to a FModifierEnvelope");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FUNC_USE_MAIN | FUNC_USE_REPORTS);
  parm = api_def_float(fn,
                       "frame",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "",
                       "Frame to add this control-point",
                       -FLT_MAX,
                       FLT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(
      fn, "point", "FModEnvelopeControlPoint", "", "Newly created control-point");
  api_def_fn_return(fn, parm);

  func = api_def_fn(sapi, "remove", "api_FModEnvelope_points_remove");
  api_def_fn_ui_description(fn, "Remove a control-point from an FModEnvelope");
  api_def_fn_flag(fn, FN_USE_SELF_ID | FN_USE_MAIN | FN_USE_REPORTS);
  parm = api_def_ptr(
      fn, "point", "FModEnvelopeControlPoint", "", "Control-point to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
}

static void api_def_fmod_envelope(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "FModEnvelope", "FMod");
  api_def_struct_ui_text(sapi, "Envelope F-Mod", "Scale the values of the modified F-Curve");
  api_def_struct_stype_from(sapi, "FMod_Envelope", "data");

  /* Collections */
  prop = api_def_prop(sapi, "control_points", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_sdna(prop, NULL, "data", "totvert");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_struct_type(prop, "FModEnvelopeControlPoint");
  api_def_prop_ui_text(
      prop, "Control Points", "Control points defining the shape of the envelope");
  api_def_fmod_envelope_control_points(dapi, prop);

  /* Range Settings */
  prop = api_def_prop(sapi, "ref_value", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "midval");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(
      prop, "Ref Value", "Value that envelope's influence is centered around / based on");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = api_def_prop(sapi, "default_min", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "min");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  api_def_prop_ui_text(
      prop, "Default Minimum", "Lower distance from Reference Value for 1:1 default influence");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = api_def_prop(sapi, "default_max", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "max");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(
      prop, "Default Maximum", "Upper distance from Reference Value for 1:1 default influence");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "api_FMod_update");
}

/* --------- */

static void api_def_fmod_cycles(DuneApi *dapi)
{
  ApiStruct sapi;
  ApiProp *prop;

  static const EnumProp prop_type_items[] = {
      {FCM_EXTRAPOLATE_NONE, "NONE", 0, "No Cycles", "Don't do anything"},
      {FCM_EXTRAPOLATE_CYCLIC, "REPEAT", 0, "Repeat Motion", "Repeat keyframe range as-is"},
      {FCM_EXTRAPOLATE_CYCLIC_OFFSET,
       "REPEAT_OFFSET",
       0,
       "Repeat with Offset",
       "Repeat keyframe range, but with offset based on gradient between "
       "start and end values"},
      {FCM_EXTRAPOLATE_MIRROR,
       "MIRROR",
       0,
       "Repeat Mirrored",
       "Alternate between forward and reverse playback of keyframe range"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "FModifierCycles", "FModifier");
  api_def_struct_ui_text(sapi, "Cycles F-Modifier", "Repeat the values of the modified F-Curve");
  api_def_struct_stype_from(sapi, "FMod_Cycles", "data");

  /* before */
  prop = RNA_def_property(srna, "mode_before", PROP_ENUM, PROP_NONE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_enum_sdna(prop, NULL, "before_mode");
  RNA_def_property_enum_items(prop, prop_type_items);
  RNA_def_property_ui_text(prop, "Before Mode", "Cycling mode to use before first keyframe");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "cycles_before", PROP_INT, PROP_NONE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_int_sdna(prop, NULL, "before_cycles");
  RNA_def_property_ui_text(
      prop,
      "Before Cycles",
      "Maximum number of cycles to allow before first keyframe (0 = infinite)");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  /* after */
  prop = api_def_prop(sapi, "mode_after", PROP_ENUM, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_enum_stype(prop, NULL, "after_mode");
  api_def_prop_enum_items(prop, prop_type_items);
  api_def_prop_ui_text(prop, "After Mode", "Cycling mode to use after last keyframe");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = api_def_prop(sapi, "cycles_after", PROP_INT, PROP_NONE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_int_stype(prop, NULL, "after_cycles");
  api_def_prop_ui_text(prop,
                       "After Cycles",
                       "Maximum number of cycles to allow after last keyframe (0 = infinite)");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "api_FMod_update");
}

/* --------- */

static void rna_def_fmodifier_python(BlenderRNA *brna)
{
  StructRNA *srna;
  // PropertyRNA *prop;

  srna = RNA_def_struct(brna, "FModifierPython", "FModifier");
  RNA_def_struct_ui_text(
      srna, "Python F-Modifier", "Perform user-defined operation on the modified F-Curve");
  RNA_def_struct_sdna_from(srna, "FMod_Python", "data");
}

/* --------- */

static void rna_def_fmodifier_limits(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "FModifierLimits", "FModifier");
  RNA_def_struct_ui_text(
      srna, "Limit F-Modifier", "Limit the time/value ranges of the modified F-Curve");
  RNA_def_struct_sdna_from(srna, "FMod_Limits", "data");

  prop = RNA_def_property(srna, "use_min_x", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FCM_LIMIT_XMIN);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Minimum X", "Use the minimum X value");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "use_min_y", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FCM_LIMIT_YMIN);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Minimum Y", "Use the minimum Y value");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "use_max_x", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FCM_LIMIT_XMAX);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Maximum X", "Use the maximum X value");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "use_max_y", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FCM_LIMIT_YMAX);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Maximum Y", "Use the maximum Y value");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "min_x", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "rect.xmin");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(
      prop, NULL, "rna_FModifierLimits_minx_set", "rna_FModifierLimits_minx_range");
  RNA_def_property_ui_text(prop, "Minimum X", "Lowest X value to allow");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "min_y", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "rect.ymin");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(
      prop, NULL, "rna_FModifierLimits_miny_set", "rna_FModifierLimits_miny_range");
  RNA_def_property_ui_text(prop, "Minimum Y", "Lowest Y value to allow");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "max_x", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "rect.xmax");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(
      prop, NULL, "rna_FModifierLimits_maxx_set", "rna_FModifierLimits_maxx_range");
  RNA_def_property_ui_text(prop, "Maximum X", "Highest X value to allow");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "max_y", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "rect.ymax");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(
      prop, NULL, "rna_FModifierLimits_maxy_set", "rna_FModifierLimits_maxy_range");
  RNA_def_property_ui_text(prop, "Maximum Y", "Highest Y value to allow");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");
}

/* --------- */

static void rna_def_fmodifier_noise(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem prop_modification_items[] = {
      {FCM_NOISE_MODIF_REPLACE, "REPLACE", 0, "Replace", ""},
      {FCM_NOISE_MODIF_ADD, "ADD", 0, "Add", ""},
      {FCM_NOISE_MODIF_SUBTRACT, "SUBTRACT", 0, "Subtract", ""},
      {FCM_NOISE_MODIF_MULTIPLY, "MULTIPLY", 0, "Multiply", ""},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "FModifierNoise", "FModifier");
  RNA_def_struct_ui_text(srna, "Noise F-Modifier", "Give randomness to the modified F-Curve");
  RNA_def_struct_sdna_from(srna, "FMod_Noise", "data");

  prop = RNA_def_property(srna, "blend_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "modification");
  RNA_def_property_enum_items(prop, prop_modification_items);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Blend Type", "Method of modifying the existing F-Curve");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "size");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Scale", "Scaling (in time) of the noise");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "strength", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "strength");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop,
      "Strength",
      "Amplitude of the noise - the amount that it modifies the underlying curve");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "phase", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "phase");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Phase", "A random seed for the noise effect");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "offset", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "offset");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Offset", "Time offset for the noise effect");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "depth", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "depth");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Depth", "Amount of fine level detail present in the noise");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");
}

/* --------- */

static void rna_def_fmodifier_stepped(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "FModifierStepped", "FModifier");
  RNA_def_struct_ui_text(
      srna,
      "Stepped Interpolation F-Modifier",
      "Hold each interpolated value from the F-Curve for several frames without "
      "changing the timing");
  RNA_def_struct_sdna_from(srna, "FMod_Stepped", "data");

  /* properties */
  prop = RNA_def_property(srna, "frame_step", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "step_size");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Step Size", "Number of frames to hold each value");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "frame_offset", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "offset");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop,
                           "Offset",
                           "Reference number of frames before frames get held "
                           "(use to get hold for '1-3' vs '5-7' holding patterns)");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "use_frame_start", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FCM_STEPPED_NO_BEFORE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop, "Use Start Frame", "Restrict modifier to only act after its 'start' frame");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "use_frame_end", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FCM_STEPPED_NO_AFTER);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop, "Use End Frame", "Restrict modifier to only act before its 'end' frame");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "frame_start", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "start_frame");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(prop,
                               NULL,
                               "rna_FModifierStepped_frame_start_set",
                               "rna_FModifierStepped_start_frame_range");
  RNA_def_property_ui_text(
      prop, "Start Frame", "Frame that modifier's influence starts (if applicable)");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");

  prop = RNA_def_property(srna, "frame_end", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "end_frame");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(
      prop, NULL, "rna_FModifierStepped_frame_end_set", "rna_FModifierStepped_end_frame_range");
  RNA_def_property_ui_text(
      prop, "End Frame", "Frame that modifier's influence ends (if applicable)");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FModifier_update");
}

/* --------- */

static void rna_def_fmodifier(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  /* base struct definition */
  srna = RNA_def_struct(brna, "FModifier", NULL);
  RNA_def_struct_refine_func(srna, "rna_FModifierType_refine");
  RNA_def_struct_ui_text(srna, "F-Modifier", "Modifier for values of F-Curve");

#  if 0  /* XXX not used yet */
  /* name */
  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);
  RNA_def_property_ui_text(prop, "Name", "Short description of F-Curve Modifier");
#  endif /* XXX not used yet */

  /* type */
  prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_enum_items(prop, rna_enum_fmodifier_type_items);
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_ACTION);
  RNA_def_property_ui_text(prop, "Type", "F-Curve Modifier Type");

  /* settings */
  prop = RNA_def_property(srna, "show_expanded", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NO_DEG_UPDATE);
  RNA_def_property_boolean_sdna(prop, NULL, "ui_expand_flag", 0);
  // RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_FModifier_show_expanded_set");
  RNA_def_property_ui_text(prop, "Expanded", "F-Curve Modifier's panel is expanded in UI");
  RNA_def_property_ui_icon(prop, ICON_DISCLOSURE_TRI_RIGHT, 1);

  prop = RNA_def_property(srna, "mute", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FMODIFIER_FLAG_MUTED);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Enabled", "Enable F-Curve modifier evaluation");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_update");
  RNA_def_property_ui_icon(prop, ICON_CHECKBOX_HLT, -1);

  prop = RNA_def_property(srna, "is_valid", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", FMODIFIER_FLAG_DISABLED);
  // RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop, "Disabled", "F-Curve Modifier has invalid settings and will not be evaluated");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_update");

  /* TODO: setting this to true must ensure that all others in stack are turned off too... */
  prop = RNA_def_property(srna, "active", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FMODIFIER_FLAG_ACTIVE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Active", "F-Curve modifier will show settings in the editor");
  RNA_def_property_boolean_funcs(prop, NULL, "rna_FModifier_active_set");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_active_update");
  RNA_def_property_ui_icon(prop, ICON_RADIOBUT_OFF, 1);

  /* restricted range */
  prop = RNA_def_property(srna, "use_restricted_range", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FMODIFIER_FLAG_RANGERESTRICT);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop,
      "Restrict Frame Range",
      "F-Curve Modifier is only applied for the specified frame range to help "
      "mask off effects in order to chain them");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_update");

  prop = RNA_def_property(srna, "frame_start", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "sfra");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(
      prop, NULL, "rna_FModifier_start_frame_set", "rna_FModifier_start_frame_range");
  RNA_def_property_ui_text(
      prop,
      "Start Frame",
      "Frame that modifier's influence starts (if Restrict Frame Range is in use)");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_update");

  prop = RNA_def_property(srna, "frame_end", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "efra");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(
      prop, NULL, "rna_FModifier_end_frame_set", "rna_FModifier_end_frame_range");
  RNA_def_property_ui_text(
      prop,
      "End Frame",
      "Frame that modifier's influence ends (if Restrict Frame Range is in use)");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_update");

  prop = RNA_def_property(srna, "blend_in", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "blendin");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(prop, NULL, NULL, "rna_FModifier_blending_range");
  RNA_def_property_ui_text(
      prop, "Blend In", "Number of frames from start frame for influence to take effect");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_update");

  prop = RNA_def_property(srna, "blend_out", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "blendout");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_float_funcs(prop, NULL, NULL, "rna_FModifier_blending_range");
  RNA_def_property_ui_text(
      prop, "Blend Out", "Number of frames from end frame for influence to fade out");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_update");

  /* influence */
  prop = RNA_def_property(srna, "use_influence", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FMODIFIER_FLAG_USEINFLUENCE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop, "Use Influence", "F-Curve Modifier's effects will be tempered by a default factor");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_update");

  prop = RNA_def_property(srna, "influence", PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_sdna(prop, NULL, "influence");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_float_default(prop, 1.0f);
  RNA_def_property_ui_text(
      prop, "Influence", "Amount of influence F-Curve Modifier will have when not fading in/out");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_FModifier_update");
}

/* *********************** */

static void rna_def_drivertarget(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem prop_trans_chan_items[] = {
      {DTAR_TRANSCHAN_LOCX, "LOC_X", 0, "X Location", ""},
      {DTAR_TRANSCHAN_LOCY, "LOC_Y", 0, "Y Location", ""},
      {DTAR_TRANSCHAN_LOCZ, "LOC_Z", 0, "Z Location", ""},
      {0, "", 0, NULL, NULL},
      {DTAR_TRANSCHAN_ROTX, "ROT_X", 0, "X Rotation", ""},
      {DTAR_TRANSCHAN_ROTY, "ROT_Y", 0, "Y Rotation", ""},
      {DTAR_TRANSCHAN_ROTZ, "ROT_Z", 0, "Z Rotation", ""},
      {DTAR_TRANSCHAN_ROTW, "ROT_W", 0, "W Rotation", ""},
      {0, "", 0, NULL, NULL},
      {DTAR_TRANSCHAN_SCALEX, "SCALE_X", 0, "X Scale", ""},
      {DTAR_TRANSCHAN_SCALEY, "SCALE_Y", 0, "Y Scale", ""},
      {DTAR_TRANSCHAN_SCALEZ, "SCALE_Z", 0, "Z Scale", ""},
      {DTAR_TRANSCHAN_SCALE_AVG, "SCALE_AVG", 0, "Average Scale", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem prop_local_space_items[] = {
      {0,
       "WORLD_SPACE",
       0,
       "World Space",
       "Transforms include effects of parenting/restpose and constraints"},
      {DTAR_FLAG_LOCALSPACE,
       "TRANSFORM_SPACE",
       0,
       "Transform Space",
       "Transforms don't include parenting/restpose or constraints"},
      {DTAR_FLAG_LOCALSPACE | DTAR_FLAG_LOCAL_CONSTS,
       "LOCAL_SPACE",
       0,
       "Local Space",
       "Transforms include effects of constraints but not "
       "parenting/restpose"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "DriverTarget", NULL);
  RNA_def_struct_ui_text(srna, "Driver Target", "Source of input values for driver variables");

  /* Target Properties - ID-block to Drive */
  prop = RNA_def_property(srna, "id", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "ID");
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_editable_func(prop, "rna_DriverTarget_id_editable");
  /* NOTE: custom set function is ONLY to avoid rna setting a user for this. */
  RNA_def_property_pointer_funcs(
      prop, NULL, "rna_DriverTarget_id_set", "rna_DriverTarget_id_typef", NULL);
  RNA_def_property_ui_text(prop,
                           "ID",
                           "ID-block that the specific property used can be found from "
                           "(id_type property must be set first)");
  RNA_def_property_update(prop, 0, "rna_DriverTarget_update_data");

  prop = RNA_def_property(srna, "id_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "idtype");
  RNA_def_property_enum_items(prop, rna_enum_id_type_items);
  RNA_def_property_enum_default(prop, ID_OB);
  RNA_def_property_enum_funcs(prop, NULL, "rna_DriverTarget_id_type_set", NULL);
  RNA_def_property_editable_func(prop, "rna_DriverTarget_id_type_editable");
  RNA_def_property_ui_text(prop, "ID Type", "Type of ID-block that can be used");
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_ID);
  RNA_def_property_update(prop, 0, "rna_DriverTarget_update_data");

  /* Target Properties - Property to Drive */
  prop = RNA_def_property(srna, "data_path", PROP_STRING, PROP_NONE);
  RNA_def_property_string_funcs(prop,
                                "rna_DriverTarget_RnaPath_get",
                                "rna_DriverTarget_RnaPath_length",
                                "rna_DriverTarget_RnaPath_set");
  RNA_def_property_ui_text(prop, "Data Path", "RNA Path (from ID-block) to property used");
  RNA_def_property_update(prop, 0, "rna_DriverTarget_update_data");

  prop = RNA_def_property(srna, "bone_target", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "pchan_name");
  RNA_def_property_ui_text(prop, "Bone Name", "Name of PoseBone to use as target");
  RNA_def_property_update(prop, 0, "rna_DriverTarget_update_data");

  prop = RNA_def_property(srna, "transform_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "transChan");
  RNA_def_property_enum_items(prop, prop_trans_chan_items);
  RNA_def_property_ui_text(prop, "Type", "Driver variable type");
  RNA_def_property_update(prop, 0, "rna_DriverTarget_update_data");

  prop = RNA_def_property(srna, "rotation_mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "rotation_mode");
  RNA_def_property_enum_items(prop, rna_enum_driver_target_rotation_mode_items);
  RNA_def_property_ui_text(prop, "Rotation Mode", "Mode for calculating rotation channel values");
  RNA_def_property_update(prop, 0, "rna_DriverTarget_update_data");

  prop = RNA_def_property(srna, "transform_space", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
  RNA_def_property_enum_items(prop, prop_local_space_items);
  RNA_def_property_ui_text(prop, "Transform Space", "Space in which transforms are used");
  RNA_def_property_update(prop, 0, "rna_DriverTarget_update_data");
}

static void rna_def_drivervar(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem prop_type_items[] = {
      {DVAR_TYPE_SINGLE_PROP,
       "SINGLE_PROP",
       ICON_RNA,
       "Single Property",
       "Use the value from some RNA property"},
      {DVAR_TYPE_TRANSFORM_CHAN,
       "TRANSFORMS",
       ICON_DRIVER_TRANSFORM,
       "Transform Channel",
       "Final transformation value of object or bone"},
      {DVAR_TYPE_ROT_DIFF,
       "ROTATION_DIFF",
       ICON_DRIVER_ROTATIONAL_DIFFERENCE,
       "Rotational Difference",
       "Use the angle between two bones"},
      {DVAR_TYPE_LOC_DIFF,
       "LOC_DIFF",
       ICON_DRIVER_DISTANCE,
       "Distance",
       "Distance between two bones or objects"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "DriverVariable", NULL);
  RNA_def_struct_sdna(srna, "DriverVar");
  RNA_def_struct_ui_text(
      srna, "Driver Variable", "Variable from some source/target for driver relationship");

  /* Variable Name */
  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_DriverVariable_name_set");
  RNA_def_property_ui_text(
      prop,
      "Name",
      "Name to use in scripted expressions/functions (no spaces or dots are allowed, "
      "and must start with a letter)");
  RNA_def_property_update(prop, 0, "rna_DriverTarget_update_name"); /* XXX */

  /* Enums */
  prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, prop_type_items);
  RNA_def_property_enum_funcs(prop, NULL, "rna_DriverVariable_type_set", NULL);
  RNA_def_property_ui_text(prop, "Type", "Driver variable type");
  RNA_def_property_update(prop, 0, "rna_ChannelDriver_update_data"); /* XXX */

  /* Targets */
  /* TODO: for nicer api, only expose the relevant props via subclassing,
   *       instead of exposing the collection of targets */
  prop = RNA_def_property(srna, "targets", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "targets", "num_targets");
  RNA_def_property_struct_type(prop, "DriverTarget");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Targets", "Sources of input data for evaluating this variable");

  /* Name Validity Flags */
  prop = RNA_def_property(srna, "is_name_valid", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", DVAR_FLAG_INVALID_NAME);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Is Name Valid", "Is this a valid name for a driver variable");
}

/* channeldriver.variables.* */
static void rna_def_channeldriver_variables(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  /* PropertyRNA *prop; */

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "ChannelDriverVariables");
  srna = RNA_def_struct(brna, "ChannelDriverVariables", NULL);
  RNA_def_struct_sdna(srna, "ChannelDriver");
  RNA_def_struct_ui_text(
      srna, "ChannelDriver Variables", "Collection of channel driver Variables");

  /* add variable */
  func = RNA_def_function(srna, "new", "rna_Driver_new_variable");
  RNA_def_function_ui_description(func, "Add a new variable for the driver");
  /* return type */
  parm = RNA_def_pointer(func, "var", "DriverVariable", "", "Newly created Driver Variable");
  RNA_def_function_return(func, parm);

  /* remove variable */
  func = RNA_def_function(srna, "remove", "rna_Driver_remove_variable");
  RNA_def_function_ui_description(func, "Remove an existing variable from the driver");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  /* target to remove */
  parm = RNA_def_pointer(
      func, "variable", "DriverVariable", "", "Variable to remove from the driver");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void rna_def_channeldriver(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem prop_type_items[] = {
      {DRIVER_TYPE_AVERAGE, "AVERAGE", 0, "Averaged Value", ""},
      {DRIVER_TYPE_SUM, "SUM", 0, "Sum Values", ""},
      {DRIVER_TYPE_PYTHON, "SCRIPTED", 0, "Scripted Expression", ""},
      {DRIVER_TYPE_MIN, "MIN", 0, "Minimum Value", ""},
      {DRIVER_TYPE_MAX, "MAX", 0, "Maximum Value", ""},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "Driver", NULL);
  RNA_def_struct_sdna(srna, "ChannelDriver");
  RNA_def_struct_ui_text(
      srna, "Driver", "Driver for the value of a setting based on an external value");
  RNA_def_struct_ui_icon(srna, ICON_DRIVER);

  /* Enums */
  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, prop_type_items);
  api_def_prop_ui_text(prop, "Type", "Driver type");
  api_def_prop_update(prop, 0, "api_ChannelDriver_update_data");

  /* String values */
  prop = api_def_prop(sapi, "expression", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Expression", "Expression to use for Scripted Expression");
  api_def_prop_update(prop, 0, "api_ChannelDriver_update_expr");

  /* Collections */
  prop = api_def_prop(sapi, "variables", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "variables", NULL);
  apo_def_prop_struct_type(prop, "DriverVariable");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Variables", "Properties acting as inputs for this driver");
  api_def_channeldriver_variables(dapi, prop);

  /* Settings */
  prop = api_def_prop(sapi, "use_self", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", DRIVER_FLAG_USE_SELF);
  api_def_prop_ui_text(
      prop,
      "Use Self",
      "Include a 'self' variable in the name-space, "
      "so drivers can easily reference the data being modified (object, bone, etc...)");

  /* State Info (for Debugging) */
  prop = api_def_prop(sapi, "is_valid", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", DRIVER_FLAG_INVALID);
  api_def_prop_ui_text(
      prop, "Invalid", "Driver could not be evaluated in past, so should be skipped");

  prop = api_def_prop(sapi, "is_simple_expression", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_ChannelDriver_is_simple_expression_get", NULL);
  api_def_prop_ui_text(
      prop,
      "Simple Expression",
      "The scripted expression can be evaluated without using the full python interpreter");

  /* Functions */
  api_api_drivers(sapi);
}

/* *********************** */

static void api_def_fpoint(DuneApi *dapi)
{
  ApiStruct *srna;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "FCurveSample", NULL);
  api_def_struct_stype(sapi, "FPoint");
  api_def_struct_ui_text(sapi, "F-Curve Sample", "Sample point for F-Curve");

  /* Boolean values */
  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", 1);
  api_def_prop_ui_text(prop, "Select", "Selection status");
  api_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_SELECTED, NULL);

  /* Vector value */
  prop = api_def_prop(sapi, "co", PROP_FLOAT, PROP_COORDS); /* keyframes are dimensionless */
  RNA_def_prop_float_stypr(prop, NULL, "vec");
  RNA_def_prop_array(prop, 2);
  RNA_def_prop_ui_text(prop, "Point", "Point coordinates");
  RNA_def_prop_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);
}

/* duplicate of BezTriple in rna_curve.c
 * but with F-Curve specific options updates/functionality */
static void api_def_fkeyframe(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Keyframe", NULL);
  api_def_struct_stype(sapi, "BezTriple");
  api_def_struct_ui_text(
      sapi, "Keyframe", "Bezier curve point with two handles defining a Keyframe on an F-Curve");

  /* Boolean values */
  prop = RNA_def_property(srna, "select_left_handle", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "f1", SELECT);
  RNA_def_property_ui_text(prop, "Handle 1 selected", "Left handle selection status");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_SELECTED, NULL);

  prop = RNA_def_property(srna, "select_right_handle", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "f3", SELECT);
  RNA_def_property_ui_text(prop, "Handle 2 selected", "Right handle selection status");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_SELECTED, NULL);

  prop = RNA_def_property(srna, "select_control_point", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "f2", SELECT);
  RNA_def_property_ui_text(prop, "Select", "Control point selection status");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_SELECTED, NULL);

  /* Enums */
  prop = RNA_def_property(srna, "handle_left_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "h1");
  RNA_def_property_enum_items(prop, rna_enum_keyframe_handle_type_items);
  RNA_def_property_ui_text(prop, "Left Handle Type", "Handle types");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_Keyframe_update");

  prop = RNA_def_property(srna, "handle_right_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "h2");
  RNA_def_property_enum_items(prop, rna_enum_keyframe_handle_type_items);
  RNA_def_property_ui_text(prop, "Right Handle Type", "Handle types");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_Keyframe_update");

  prop = RNA_def_property(srna, "interpolation", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "ipo");
  RNA_def_property_enum_items(prop, rna_enum_beztriple_interpolation_mode_items);
  RNA_def_property_ui_text(prop,
                           "Interpolation",
                           "Interpolation method to use for segment of the F-Curve from "
                           "this Keyframe until the next Keyframe");
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_ACTION);
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_Keyframe_update");

  prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "hide");
  RNA_def_property_enum_items(prop, rna_enum_beztriple_keyframe_type_items);
  RNA_def_property_ui_text(prop, "Type", "Type of keyframe (for visual purposes only)");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_Keyframe_update");

  prop = RNA_def_property(srna, "easing", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "easing");
  RNA_def_property_enum_items(prop, rna_enum_beztriple_interpolation_easing_items);
  RNA_def_property_ui_text(prop,
                           "Easing",
                           "Which ends of the segment between this and the next keyframe easing "
                           "interpolation is applied to");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_Keyframe_update");

  prop = RNA_def_property(srna, "back", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "back");
  RNA_def_property_ui_text(prop, "Back", "Amount of overshoot for 'back' easing");
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_ACTION);
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_Keyframe_update");

  prop = RNA_def_property(srna, "amplitude", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "amplitude");
  RNA_def_property_range(prop, 0.0f, FLT_MAX); /* only positive values... */
  RNA_def_property_ui_text(
      prop, "Amplitude", "Amount to boost elastic bounces for 'elastic' easing");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_Keyframe_update");

  prop = RNA_def_property(srna, "period", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "period");
  RNA_def_property_ui_text(prop, "Period", "Time between bounces for elastic easing");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, "rna_Keyframe_update");

  /* Vector values */
  prop = RNA_def_property(
      srna, "handle_left", PROP_FLOAT, PROP_COORDS); /* keyframes are dimensionless */
  RNA_def_property_array(prop, 2);
  RNA_def_property_float_funcs(
      prop, "rna_FKeyframe_handle1_get", "rna_FKeyframe_handle1_set", NULL);
  RNA_def_property_ui_text(
      prop, "Left Handle", "Coordinates of the left handle (before the control point)");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_Keyframe_update");

  prop = RNA_def_property(srna, "co", PROP_FLOAT, PROP_COORDS); /* keyframes are dimensionless */
  RNA_def_property_array(prop, 2);
  RNA_def_property_float_funcs(
      prop, "rna_FKeyframe_ctrlpoint_get", "rna_FKeyframe_ctrlpoint_set", NULL);
  RNA_def_property_ui_text(prop, "Control Point", "Coordinates of the control point");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_Keyframe_update");

  prop = RNA_def_property(
      srna, "co_ui", PROP_FLOAT, PROP_COORDS); /* keyframes are dimensionless */
  RNA_def_property_array(prop, 2);
  RNA_def_property_float_funcs(
      prop, "rna_FKeyframe_ctrlpoint_get", "rna_FKeyframe_ctrlpoint_ui_set", NULL);
  RNA_def_property_ui_text(
      prop,
      "Control Point",
      "Coordinates of the control point. Note: Changing this value also updates the handles "
      "similar to using the graph editor transform operator");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_Keyframe_update");

  prop = RNA_def_property(
      srna, "handle_right", PROP_FLOAT, PROP_COORDS); /* keyframes are dimensionless */
  RNA_def_property_array(prop, 2);
  RNA_def_property_float_funcs(
      prop, "rna_FKeyframe_handle2_get", "rna_FKeyframe_handle2_set", NULL);
  RNA_def_property_ui_text(
      prop, "Right Handle", "Coordinates of the right handle (after the control point)");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_Keyframe_update");
}

static void rna_def_fcurve_modifiers(BlenderRNA *brna, PropertyRNA *cprop)
{
  /* add modifiers */
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "FCurveModifiers");
  srna = RNA_def_struct(brna, "FCurveModifiers", NULL);
  RNA_def_struct_sdna(srna, "FCurve");
  RNA_def_struct_ui_text(srna, "F-Curve Modifiers", "Collection of F-Curve Modifiers");

  /* Collection active property */
  prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "FModifier");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_pointer_funcs(
      prop, "rna_FCurve_active_modifier_get", "rna_FCurve_active_modifier_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Active F-Curve Modifier", "Active F-Curve Modifier");

  /* Constraint collection */
  func = RNA_def_function(srna, "new", "rna_FCurve_modifiers_new");
  RNA_def_function_ui_description(func, "Add a constraint to this object");
  /* return type */
  parm = RNA_def_pointer(func, "fmodifier", "FModifier", "", "New fmodifier");
  RNA_def_function_return(func, parm);
  /* object to add */
  parm = RNA_def_enum(
      func, "type", rna_enum_fmodifier_type_items, 1, "", "Constraint type to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "remove", "rna_FCurve_modifiers_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Remove a modifier from this F-Curve");
  /* modifier to remove */
  parm = RNA_def_pointer(func, "modifier", "FModifier", "", "Removed modifier");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

/* fcurve.keyframe_points */
static void rna_def_fcurve_keyframe_points(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  static const EnumPropertyItem keyframe_flag_items[] = {
      {INSERTKEY_REPLACE,
       "REPLACE",
       0,
       "Replace",
       "Don't add any new keyframes, but just replace existing ones"},
      {INSERTKEY_NEEDED, "NEEDED", 0, "Needed", "Only adds keyframes that are needed"},
      {INSERTKEY_FAST,
       "FAST",
       0,
       "Fast",
       "Fast keyframe insertion to avoid recalculating the curve each time"},
      {0, NULL, 0, NULL, NULL},
  };

  RNA_def_property_srna(cprop, "FCurveKeyframePoints");
  srna = RNA_def_struct(brna, "FCurveKeyframePoints", NULL);
  RNA_def_struct_sdna(srna, "FCurve");
  RNA_def_struct_ui_text(srna, "Keyframe Points", "Collection of keyframe points");

  func = RNA_def_function(srna, "insert", "rna_FKeyframe_points_insert");
  RNA_def_function_ui_description(func, "Add a keyframe point to a F-Curve");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_USE_MAIN);
  parm = RNA_def_float(func,
                       "frame",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "",
                       "X Value of this keyframe point",
                       -FLT_MAX,
                       FLT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float(func,
                       "value",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "",
                       "Y Value of this keyframe point",
                       -FLT_MAX,
                       FLT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_enum_flag(func, "options", keyframe_flag_items, 0, "", "Keyframe options");
  RNA_def_enum(func,
               "keyframe_type",
               rna_enum_beztriple_keyframe_type_items,
               BEZT_KEYTYPE_KEYFRAME,
               "",
               "Type of keyframe to insert");
  parm = RNA_def_pointer(func, "keyframe", "Keyframe", "", "Newly created keyframe");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "add", "rna_FKeyframe_points_add");
  RNA_def_function_ui_description(func, "Add a keyframe point to a F-Curve");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_USE_MAIN);
  parm = RNA_def_int(
      func, "count", 1, 0, INT_MAX, "Number", "Number of points to add to the spline", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "remove", "rna_FKeyframe_points_remove");
  RNA_def_function_ui_description(func, "Remove keyframe from an F-Curve");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_USE_MAIN | FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "keyframe", "Keyframe", "", "Keyframe to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
  /* optional */
  RNA_def_boolean(
      func, "fast", 0, "Fast", "Fast keyframe removal to avoid recalculating the curve each time");
}

static void rna_def_fcurve(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  FunctionRNA *func;
  PropertyRNA *parm;

  static const EnumPropertyItem prop_mode_extend_items[] = {
      {FCURVE_EXTRAPOLATE_CONSTANT,
       "CONSTANT",
       0,
       "Constant",
       "Hold values of endpoint keyframes"},
      {FCURVE_EXTRAPOLATE_LINEAR,
       "LINEAR",
       0,
       "Linear",
       "Use slope of curve leading in/out of endpoint keyframes"},
      {0, NULL, 0, NULL, NULL},
  };
  static const EnumPropertyItem prop_mode_color_items[] = {
      {FCURVE_COLOR_AUTO_RAINBOW,
       "AUTO_RAINBOW",
       0,
       "Auto Rainbow",
       "Cycle through the rainbow, trying to give each curve a unique color"},
      {FCURVE_COLOR_AUTO_RGB,
       "AUTO_RGB",
       0,
       "Auto XYZ to RGB",
       "Use axis colors for transform and color properties, and auto-rainbow for the rest"},
      {FCURVE_COLOR_AUTO_YRGB,
       "AUTO_YRGB",
       0,
       "Auto WXYZ to YRGB",
       "Use axis colors for XYZ parts of transform, and yellow for the 'W' channel"},
      {FCURVE_COLOR_CUSTOM,
       "CUSTOM",
       0,
       "User Defined",
       "Use custom hand-picked color for F-Curve"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "FCurve", NULL);
  RNA_def_struct_ui_text(srna, "F-Curve", "F-Curve defining values of a period of time");
  RNA_def_struct_ui_icon(srna, ICON_ANIM_DATA);

  /* Enums */
  prop = RNA_def_property(srna, "extrapolation", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "extend");
  RNA_def_property_enum_items(prop, prop_mode_extend_items);
  RNA_def_property_ui_text(
      prop,
      "Extrapolation",
      "Method used for evaluating value of F-Curve outside first and last keyframes");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FCurve_update_data");

  /* Pointers */
  prop = RNA_def_property(srna, "driver", PROP_POINTER, PROP_NONE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Driver", "Channel Driver (only set for Driver F-Curves)");

  prop = RNA_def_property(srna, "group", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "grp");
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_PTR_NO_OWNERSHIP);
  RNA_def_property_ui_text(prop, "Group", "Action Group that this F-Curve belongs to");
  RNA_def_property_pointer_funcs(prop, NULL, "rna_FCurve_group_set", NULL, NULL);
  RNA_def_property_update(prop, NC_ANIMATION, NULL);

  /* Path + Array Index */
  prop = RNA_def_property(srna, "data_path", PROP_STRING, PROP_NONE);
  RNA_def_property_string_funcs(
      prop, "rna_FCurve_RnaPath_get", "rna_FCurve_RnaPath_length", "rna_FCurve_RnaPath_set");
  RNA_def_property_ui_text(prop, "Data Path", "RNA Path to property affected by F-Curve");
  /* XXX need an update callback for this to that animation gets evaluated */
  RNA_def_property_update(prop, NC_ANIMATION, "rna_FCurve_update_data_relations");

  /* called 'index' when given as function arg */
  prop = RNA_def_property(srna, "array_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_ui_text(
      prop, "RNA Array Index", "Index to the specific property affected by F-Curve if applicable");
  /* XXX need an update callback for this so that animation gets evaluated */
  RNA_def_property_update(prop, NC_ANIMATION, "rna_FCurve_update_data_relations");

  /* Color */
  prop = RNA_def_property(srna, "color_mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, prop_mode_color_items);
  RNA_def_property_ui_text(
      prop, "Color Mode", "Method used to determine color of F-Curve in Graph Editor");
  RNA_def_property_update(prop, NC_ANIMATION, NULL);

  prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
  RNA_def_property_array(prop, 3);
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_text(prop, "Color", "Color of the F-Curve in the Graph Editor");
  RNA_def_property_update(prop, NC_ANIMATION, NULL);

  /* Flags */
  prop = RNA_def_property(srna, "select", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FCURVE_SELECTED);
  RNA_def_property_ui_text(prop, "Select", "F-Curve is selected for editing");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_SELECTED, NULL);

  prop = RNA_def_property(srna, "lock", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FCURVE_PROTECTED);
  RNA_def_property_ui_text(prop, "Lock", "F-Curve's settings cannot be edited");
  RNA_def_property_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = RNA_def_property(srna, "mute", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", FCURVE_MUTED);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Muted", "Disable F-Curve evaluation");
  RNA_def_property_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, "rna_FCurve_update_eval");

  prop = RNA_def_property(srna, "hide", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", FCURVE_VISIBLE);
  RNA_def_property_ui_text(
      prop, "Hide", "F-Curve and its keyframes are hidden in the Graph Editor graphs");
  RNA_def_property_update(prop, NC_SPACE | ND_SPACE_GRAPH, NULL);

  prop = RNA_def_property(srna, "auto_smoothing", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_fcurve_auto_smoothing_items);
  RNA_def_property_ui_text(
      prop, "Auto Handle Smoothing", "Algorithm used to compute automatic handles");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, "rna_FCurve_update_data");

  /* State Info (for Debugging) */
  prop = RNA_def_property(srna, "is_valid", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", FCURVE_DISABLED);
  RNA_def_property_ui_text(
      prop,
      "Valid",
      "False when F-Curve could not be evaluated in past, so should be skipped "
      "when evaluating");
  RNA_def_property_update(prop, NC_ANIMATION | ND_KEYFRAME_PROP, NULL);

  prop = RNA_def_property(srna, "is_empty", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_FCurve_is_empty_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Empty",
                           "True if the curve contributes no animation due to lack of "
                           "keyframes or useful modifiers, and should be deleted");

  /* Collections */
  prop = RNA_def_property(srna, "sampled_points", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "fpt", "totvert");
  RNA_def_property_struct_type(prop, "FCurveSample");
  RNA_def_property_ui_text(prop, "Sampled Points", "Sampled animation data");

  prop = RNA_def_property(srna, "keyframe_points", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "bezt", "totvert");
  RNA_def_property_struct_type(prop, "Keyframe");
  RNA_def_property_ui_text(prop, "Keyframes", "User-editable keyframes");
  rna_def_fcurve_keyframe_points(brna, prop);

  prop = RNA_def_property(srna, "modifiers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "FModifier");
  RNA_def_property_ui_text(prop, "Modifiers", "Modifiers affecting the shape of the F-Curve");
  rna_def_fcurve_modifiers(brna, prop);

  /* Functions */
  /* -- evaluate -- */
  func = RNA_def_function(srna, "evaluate", "evaluate_fcurve"); /* calls the C/API direct */
  RNA_def_function_ui_description(func, "Evaluate F-Curve");
  parm = RNA_def_float(func,
                       "frame",
                       1.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "Frame",
                       "Evaluate F-Curve at given frame",
                       -FLT_MAX,
                       FLT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* return value */
  parm = RNA_def_float(func,
                       "value",
                       0,
                       -FLT_MAX,
                       FLT_MAX,
                       "Value",
                       "Value of F-Curve specific frame",
                       -FLT_MAX,
                       FLT_MAX);
  RNA_def_function_return(func, parm);

  /* -- update / recalculate -- */
  func = RNA_def_function(srna, "update", "rna_FCurve_update_data_ex");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID | FUNC_USE_MAIN);
  RNA_def_function_ui_description(
      func, "Ensure keyframes are sorted in chronological order and handles are set correctly");

  /* -- time extents/range -- */
  func = RNA_def_function(srna, "range", "rna_FCurve_range");
  RNA_def_function_ui_description(func, "Get the time extents for F-Curve");
  /* return value */
  parm = RNA_def_float_vector(
      func, "range", 2, NULL, -FLT_MAX, FLT_MAX, "Range", "Min/Max values", -FLT_MAX, FLT_MAX);
  RNA_def_parameter_flags(parm, PROP_THICK_WRAP, 0);
  RNA_def_function_output(func, parm);

  /* -- auto-flag validity (ensures valid handling for data type) -- */
  func = RNA_def_function(
      srna, "update_autoflags", "update_autoflags_fcurve"); /* calls the C/API direct */
  RNA_def_function_ui_description(
      func,
      "Update FCurve flags set automatically from affected property "
      "(currently, integer/discrete flags set when the property is not a float)");
  RNA_def_function_flag(func, FUNC_USE_CONTEXT | FUNC_USE_REPORTS);
  parm = RNA_def_pointer(
      func, "data", "AnyType", "Data", "Data containing the property controlled by given FCurve");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);

  /* Functions */
  RNA_api_fcurves(srna);
}

/* *********************** */

void RNA_def_fcurve(BlenderRNA *brna)
{
  rna_def_fcurve(brna);
  rna_def_fkeyframe(brna);
  rna_def_fpoint(brna);

  rna_def_drivertarget(brna);
  rna_def_drivervar(brna);
  rna_def_channeldriver(brna);

  rna_def_fmodifier(brna);

  rna_def_fmodifier_generator(brna);
  rna_def_fmodifier_function_generator(brna);

  rna_def_fmodifier_envelope(brna);
  rna_def_fmodifier_envelope_ctrl(brna);

  rna_def_fmodifier_cycles(brna);
  rna_def_fmodifier_python(brna);
  rna_def_fmodifier_limits(brna);
  rna_def_fmodifier_noise(brna);
  rna_def_fmodifier_stepped(brna);
}

#endif
