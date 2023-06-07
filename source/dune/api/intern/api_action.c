#include <stdlib.h>

#include "types_action.h"
#include "types_anim.h"
#include "types_scene.h"

#include "mem_guardedalloc.h"

#include "lib_utildefines.h"

#include "lang_translation.h"

#include "dune_action.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_types.h"

#ifdef API_RUNTIME

#  include "lib_math_base.h"

#  include "dune_fcurve.h"

#  include "graph.h"

#  include "ed_keyframing.h"

#  include "wm_api.h"

static void api_ActionGroup_channels_next(CollectionPropIter *iter)
{
  ListIter *internal = &iter->internal.list;
  FCurve *fcu = (FCurve *)internal->link;
  ActionGroup *grp = fcu->grp;

  /* only continue if the next F-Curve (if existent) belongs in the same group */
  if ((fcu->next) && (fcu->next->grp == grp)) {
    internal->link = (Link *)fcu->next;
  }
  else {
    internal->link = NULL;
  }

  iter->valid = (internal->link != NULL);
}

static ActionGroup *api_Action_groups_new(Action *act, const char name[])
{
  return action_groups_add_new(act, name);
}

static void api_Action_groups_remove(Action *act, ReportList *reports, ApiPtr *agrp_ptr)
{
  ActionGroup *agrp = agrp_ptr->data;
  FCurve *fcu, *fcn;

  /* try to remove the F-Curve from the action */
  if (lib_remlink_safe(&act->groups, agrp) == false) {
    dune_reportf(reports,
                RPT_ERROR,
                "Action group '%s' not found in action '%s'",
                agrp->name,
                act->id.name + 2);
    return;
  }

  /* Move every one of the group's F-Curves out into the Action again. */
  for (fcu = agrp->channels.first; (fcu) && (fcu->grp == agrp); fcu = fcn) {
    fcn = fcu->next;

    /* remove from group */
    action_groups_remove_channel(act, fcu);

    /* tack onto the end */
    lib_addtail(&act->curves, fcu);
  }

  mem_freen(agrp);
  API_PTR_INVALIDATE(agrp_ptr);

  graph_id_tag_update(&act->id, ID_RECALC_ANIMATION_NO_FLUSH);
  wn_main_add_notifier(NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);
}

static FCurve *rna_Action_fcurve_new(bAction *act,
                                     Main *bmain,
                                     ReportList *reports,
                                     const char *data_path,
                                     int index,
                                     const char *group)
{
  if (group && group[0] == '\0') {
    group = NULL;
  }

  if (data_path[0] == '\0') {
    dube_report(reports, RPT_ERROR, "F-Curve data path empty, invalid argument");
    return NULL;
  }

  /* Annoying, check if this exists. */
  if (ed_action_fcurve_find(act, data_path, index)) {
    dune_reportf(reports,
                RPT_ERROR,
                "F-Curve '%s[%d]' already exists in action '%s'",
                data_path,
                index,
                act->id.name + 2);
    return NULL;
  }
  return ED_action_fcurve_ensure(bmain, act, group, NULL, data_path, index);
}

static FCurve *rna_Action_fcurve_find(bAction *act,
                                      ReportList *reports,
                                      const char *data_path,
                                      int index)
{
  if (data_path[0] == '\0') {
    BKE_report(reports, RPT_ERROR, "F-Curve data path empty, invalid argument");
    return NULL;
  }

  /* Returns NULL if not found. */
  return dune_fcurve_find(&act->curves, data_path, index);
}

static void api_Action_fcurve_remove(Action *act, ReportList *reports, PointerRNA *fcu_ptr)
{
  FCurve *fcu = fcu_ptr->data;
  if (fcu->grp) {
    if (lib_findindex(&act->groups, fcu->grp) == -1) {
      dune_reportf(reports,
                  RPT_ERROR,
                  "F-Curve's action group '%s' not found in action '%s'",
                  fcu->grp->name,
                  act->id.name + 2);
      return;
    }

    action_groups_remove_channel(act, fcu);
    dune_fcurve_free(fcu);
    API_PTR_INVALIDATE(fcu_ptr);
  }
  else {
    if (lib_findindex(&act->curves, fcu) == -1) {
      dune_reportf(reports, RPT_ERROR, "F-Curve not found in action '%s'", act->id.name + 2);
      return;
    }

    lib_remlink(&act->curves, fcu);
    dune_fcurve_free(fcu);
    API_PTR_INVALIDATE(fcu_ptr);
  }

  grpg_id_tag_update(&act->id, ID_RECALC_ANIMATION_NO_FLUSH);
  wm_main_add_notifier(NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);
}

static TimeMarker *api_Action_pose_markers_new(Action *act, const char name[])
{
  TimeMarker *marker = mem_callocn(sizeof(TimeMarker), "TimeMarker");
  marker->flag = 1;
  marker->frame = 1;
  lib_strncpy_utf8(marker->name, name, sizeof(marker->name));
  lib_addtail(&act->markers, marker);
  return marker;
}

static void api_Action_pose_markers_remove(Action *act,
                                           ReportList *reports,
                                           ApiPtr *marker_ptr)
{
  TimeMarker *marker = marker_ptr->data;
  if (!lib_remlink_safe(&act->markers, marker)) {
    lib_reportf(reports,
                RPT_ERROR,
                "Timeline marker '%s' not found in action '%s'",
                marker->name,
                act->id.name + 2);
    return;
  }

  mem_freeN(marker);
  API_PTR_INVALIDATE(marker_ptr);
}

static ApiPtr api_Action_active_pose_marker_get(ApiPtr *ptr)
{
  Action *act = (Action *)ptr->data;
  return api_ptr_inherit_refine(
      ptr, &ApiTimelineMarker, lib_findlink(&act->markers, act->active_marker - 1));
}

static void api_Action_active_pose_marker_set(ApiPtr *ptr,
                                              ApiPtr value,
                                              struct ReportList *UNUSED(reports))
{
  Action *act = (Action *)ptr->data;
  act->active_marker = lib_findindex(&act->markers, value.data) + 1;
}

static int api_Action_active_pose_marker_index_get(ApiPtr *ptr)
{
  Action *act = (Action *)ptr->data;
  return MAX2(act->active_marker - 1, 0);
}

static void api_Action_active_pose_marker_index_set(ApiPtr *ptr, int value)
{
  Action *act = (Action *)ptr->data;
  act->active_marker = value + 1;
}

static void api_Action_active_pose_marker_index_range(
    ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  Action *act = (Action *)ptr->data;

  *min = 0;
  *max = max_ii(0, lib_list_count(&act->markers) - 1);
}

static void api_Action_frame_range_get(ApiPtr *ptr, float *r_values)
{
  dune_action_get_frame_range((Action *)ptr->owner_id, &r_values[0], &r_values[1]);
}

static void rna_Action_frame_range_set(ApiPtr *ptr, const float *values)
{
  Action *data = (Action *)ptr->owner_id;

  data->flag |= ACT_FRAME_RANGE;
  data->frame_start = values[0];
  data->frame_end = values[1];
  CLAMP_MIN(data->frame_end, data->frame_start);
}

static void api_Action_curve_frame_range_get(ApiPtr *ptr, float *values)
{ /* don't include modifiers because they too easily can have very large
   * ranges: MINAFRAMEF to MAXFRAMEF. */
  calc_action_range((Action *)ptr->owner_id, values, values + 1, false);
}

static void api_Action_use_frame_range_set(ApiPtr *ptr, bool value)
{
  Action *data = (Action *)ptr->owner_id;

  if (value) {
    /* If the frame range is blank, initialize it by scanning F-Curves. */
    if ((data->frame_start == data->frame_end) && (data->frame_start == 0)) {
      calc_action_range(data, &data->frame_start, &data->frame_end, false);
    }

    data->flag |= ACT_FRAME_RANGE;
  }
  else {
    data->flag &= ~ACT_FRAME_RANGE;
  }
}

static void api_Action_start_frame_set(ApiPtr *ptr, float value)
{
  Action *data = (Action *)ptr->owner_id;

  data->frame_start = value;
  CLAMP_MIN(data->frame_end, data->frame_start);
}

static void api_Action_end_frame_set(ApiPtr *ptr, float value)
{
  Action *data = (Action *)ptr->owner_id;

  data->frame_end = value;
  CLAMP_MAX(data->frame_start, data->frame_end);
}

/* Used to check if an action (value pointer)
 * is suitable to be assigned to the ID-block that is ptr. */
bool api_Action_id_poll(ApiPtr *ptr, ApiPtr value)
{
  Id *srcId = ptr->owner_id;
  Action *act = (Action *)value.owner_id;

  if (act) {
    /* there can still be actions that will have undefined id-root
     * (i.e. floating "action-library" members) which we will not
     * be able to resolve an idroot for automatically, so let these through
     */
    if (act->idroot == 0) {
      return 1;
    }
    else if (srcId) {
      return GS(srcId->name) == act->idroot;
    }
  }

  return 0;
}

/* Used to check if an action (value pointer)
 * can be assigned to Action Editor given current mode. */
bool api_Action_actedit_assign_poll(ApiPtr *ptr, ApiPtr value)
{
  SpaceAction *saction = (SpaceAction *)ptr->data;
  Action *act = (Action *)value.owner_id;

  if (act) {
    /* there can still be actions that will have undefined id-root
     * (i.e. floating "action-library" members) which we will not
     * be able to resolve an idroot for automatically, so let these through
     */
    if (act->idroot == 0) {
      return 1;
    }

    if (saction) {
      if (saction->mode == SACTCONT_ACTION) {
        /* this is only Object-level for now... */
        return act->idroot == ID_OB;
      }
      else if (saction->mode == SACTCONT_SHAPEKEY) {
        /* obviously shapekeys only */
        return act->idroot == ID_KE;
      }
    }
  }

  return 0;
}

static char *api_DopeSheet_path(ApiPtr *UNUSED(ptr))
{
  return lib_strdup("dopesheet");
}

#else

static void api_def_dopesheet(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  srna = api_def_struct(dapi, "DopeSheet", NULL);
  api_def_struct_stype(sapi, "bDopeSheet");
  api_def_struct_path_fn(sapi, "api_DopeSheet_path");
  api_def_struct_ui_text(
      srna, "Dope Sheet", "Settings for filtering the channels shown in animation editors");

  /* Source of DopeSheet data */
  /* XXX: make this obsolete? */
  prop = api_def_prop(sapi, "source", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "ID");
  api_def_prop_ui_text(
      prop, "Source", "ID-Block representing source data, usually ID_SCE (i.e. Scene)");

  /* Show data-block filters */
  prop = api_def_prop(sapi, "show_datablock_filters", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", ADS_FLAG_SHOW_DBFILTERS);
  api_def_prop_ui_text(
      prop,
      "Show Data-Block Filters",
      "Show options for whether channels related to certain types of data are included");
  api_def_prop_ui_icon(prop, ICON_DISCLOSURE_TRI_RIGHT, 1);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN, NULL);

  /* General Filtering Settings */
  prop = api_def_prop(sapi, "show_only_selected", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "filterflag", ADS_FILTER_ONLYSEL);
  api_def_prop_ui_text(
      prop, "Only Show Selected", "Only include channels relating to selected objects and data");
  api_def_prop_ui_icon(prop, ICON_RESTRICT_SELECT_OFF, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_hidden", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "filterflag", ADS_FILTER_INCL_HIDDEN);
  api_def_prop_ui_text(
      prop, "Show Hidden", "Include channels from objects/bone that are not visible");
  api_def_prop_ui_icon(prop, ICON_OBJECT_HIDDEN, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "use_datablock_sort", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_boo_negative_stype(prop, NULL, "flag", ADS_FLAG_NO_DB_SORT);
  api_def_prop_ui_text(prop,
                           "Sort Data-Blocks",
                           "Alphabetically sorts data-blocks - mainly objects in the scene "
                           "(disable to increase viewport speed)");
  api_def_prop_ui_icon(prop, ICON_SORTALPHA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "use_filter_invert", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", ADS_FLAG_INVERT_FILTER);
  api_def_prop_ui_text(prop, "Invert", "Invert filter search");
  api_def_prop_ui_icon(prop, ICON_ZOOM_IN, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* Debug Filtering Settings */
  prop = api_def_prop(stype, "show_only_errors", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "filterflag", ADS_FILTER_ONLY_ERRORS);
  api_def_prop_ui_text(prop,
                           "Only Show Errors",
                           "Only include F-Curves and drivers that are disabled or have errors");
  api_def_prop_ui_icon(prop, ICON_ERROR, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* Object Collection Filtering Settings */
  prop = api_def_prop(srna, "filter_collection", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "filter_grp");
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Filtering Collection", "Collection that included object should be a member of");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* FCurve Display Name Search Settings */
  prop = api_def_prop(sapi, "filter_fcurve_name", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "searchstr");
  api_def_prop_ui_text(prop, "F-Curve Name Filter", "F-Curve live filtering string");
  api_def_prop_ui_icon(prop, ICON_VIEWZOOM, 0);
  api_def_prop_flag(prop, PROP_TEXTEDIT_UPDATE);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* NLA Name Search Settings (Shared with FCurve setting, but with different labels) */
  prop = api_def_prop(sapi, "filter_text", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "searchstr");
  api_def_prop_ui_text(prop, "Name Filter", "Live filtering string")
  api_def_prop_flag(prop, PROP_TEXTEDIT_UPDATE)
  api_def_prop_ui_icon(prop, ICON_VIEWZOOM, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* Multi-word fuzzy search option for name/text filters */
  prop = api_def_prop(sapi, "use_multi_word_filter", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", ADS_FLAG_FUZZY_NAMES);
  api_def_prop_ui_text(prop,
                       "Multi-Word Fuzzy Filter",
                       "Perform fuzzy/multi-word matching.\n"
                       "Warning: May be slow");
  api_def_prop_ui_icon(prop, ICON_SORTALPHA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* NLA Specific Settings */
  prop = api_def_prop(sapi, "show_missing_nla", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NLA_NOACT);
  api_def_prop_ui_text(prop,
                       "Include Missing NLA",
                       "Include animation data-blocks with no NLA data (NLA editor only)");
  api_def_prop_ui_icon(prop, ICON_ACTION, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* Summary Settings (DopeSheet editors only) */
  prop = api_def_prop(sapi, "show_summary", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "filterflag", ADS_FILTER_SUMMARY);
  api_def_prop_ui_text(
      prop, "Display Summary", "Display an additional 'summary' line (Dope Sheet editors only)");
  api_def_prop_ui_icon(prop, ICON_BORDERMOVE, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_expanded_summary", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", ADS_FLAG_SUMMARY_COLLAPSED);
  api_def_prop_ui_text(
      prop,
      "Collapse Summary",
      "Collapse summary when shown, so all other channels get hidden (Dope Sheet editors only)");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* General DataType Filtering Settings */
  prop = api_def_prop(sapi, "show_transforms", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOOBJ);
  api_def_prop_ui_text(
      prop,
      "Display Transforms",
      "Include visualization of object-level animation data (mostly transforms)");
  api_def_prop_ui_icon(prop, ICON_ORIENTATION_GLOBAL, 0); /* XXX? */
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_shapekeys", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOSHAPEKEYS);
  api_def_prop_ui_text(
      prop, "Display Shape Keys", "Include visualization of shape key related animation data");
  api_def_prop_ui_icon(prop, ICON_SHAPEKEY_DATA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_modifiers", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOMODIFIERS);
  api_def_prop_ui_text(
      prop,
      "Display Modifier Data",
      "Include visualization of animation data related to data-blocks linked to modifiers");
  api_def_prop_ui_icon(prop, ICON_MODIFIER_DATA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_meshes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOMESH);
  api_def_prop_ui_text(
      prop, "Display Meshes", "Include visualization of mesh related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_MESH, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_lattices", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOLAT);
  api_def_prop_ui_text(
      prop, "Display Lattices", "Include visualization of lattice related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_LATTICE, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_cameras", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOCAM);
  api_def_prop_ui_text(
      prop, "Display Camera", "Include visualization of camera related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_CAMERA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_materials", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOMAT);
  api_def_prop_ui_text(
      prop, "Display Material", "Include visualization of material related animation data");
  api_def_prop_ui_icon(prop, ICON_MATERIAL_DATA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_lights", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOLAM);
  api_def_prop_ui_text(
      prop, "Display Light", "Include visualization of light related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_LIGHT, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_linestyles", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOLINESTYLE);
  api_def_prop_ui_text(
      prop, "Display Line Style", "Include visualization of Line Style related Animation data");
  api_def_prop_ui_icon(prop, ICON_LINE_DATA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_textures", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOTEX);
  api_def_prop_ui_text(
      prop, "Display Texture", "Include visualization of texture related animation data");
  api_def_prop_ui_icon(prop, ICON_TEXTURE_DATA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_curves", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOCUR);
  api_def_prop_ui_text(
      prop, "Display Curve", "Include visualization of curve related animation data");
  api_def_prop_ui_icon(prop, ICON_CURVE_DATA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_worlds", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOWOR);
  api_def_prop_ui_text(
      prop, "Display World", "Include visualization of world related animation data");
  api_def_prop_ui_icon(prop, ICON_WORLD_DATA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_scenes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOSCE);
  api_def_prop_ui_text(
      prop, "Display Scene", "Include visualization of scene related animation data");
  api_def_prop_ui_icon(prop, ICON_SCENE_DATA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_particles", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_style(prop, NULL, "filterflag", ADS_FILTER_NOPART);
  api_def_prop_ui_text(
      prop, "Display Particle", "Include visualization of particle related animation data");
  api_def_prop_ui_icon(prop, ICON_PARTICLE_DATA, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_metaballs", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOMBA);
  api_def_prop_ui_text(
      prop, "Display Metaball", "Include visualization of metaball related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_META, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_armatures", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOARM);
  api_def_prop_ui_text(
      prop, "Display Armature", "Include visualization of armature related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_ARMATURE, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_nodes", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NONTREE);
  api_def_prop_ui_text(
      prop, "Display Node", "Include visualization of node related animation data");
  api_def_prop_ui_icon(prop, ICON_NODETREE, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_speakers", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag", ADS_FILTER_NOSPK);
  api_def_prop_ui_text(
      prop, "Display Speaker", "Include visualization of speaker related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_SPEAKER, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_cache_files", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag2", ADS_FILTER_NOCACHEFILES);
  api_def_prop_ui_text(
      prop, "Display Cache Files", "Include visualization of cache file related animation data");
  api_def_prop_ui_icon(prop, ICON_FILE, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_hair_curves", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag2", ADS_FILTER_NOHAIR);
  api_def_prop_ui_text(
      prop, "Display Hair", "Include visualization of hair related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_CURVES, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_pointclouds", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag2", ADS_FILTER_NOPOINTCLOUD);
  api_def_prop_ui_text(
      prop, "Display Point Cloud", "Include visualization of point cloud related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_POINTCLOUD, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_volumes", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag2", ADS_FILTER_NOVOLUME);
  api_def_prop_ui_text(
      prop, "Display Volume", "Include visualization of volume related animation data");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_VOLUME, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_pen", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_sdna(prop, NULL, "filterflag", ADS_FILTER_NOPEN);
  api_def_prop_ui_text(
      prop,
      "Display Pen",
      "Include visualization of Pen related animation data and frames");
  api_def_prop_ui_icon(prop, ICON_OUTLINER_OB_PEN, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_movieclips", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "filterflag2", ADS_FILTER_NOMOVIECLIPS);
  api_def_prop_ui_text(
      prop, "Display Movie Clips", "Include visualization of movie clip related animation data");
  api_def_prop_ui_icon(prop, ICON_TRACKER, 0);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);
}

static void api_def_action_group(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ActionGroup", NULL);
  api_def_struct_stype(sapi, "ActionGroup");
  api_def_struct_ui_text(sapi, "Action Group", "Groups of F-Curves");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_ui_text(prop, "Name", "");
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* WARNING: be very careful when working with this list, since the endpoint is not
   * defined like a standard ListBase. Adding/removing channels from this list needs
   * extreme care, otherwise the F-Curve list running through adjacent groups does
   * not match up with the one stored in the Action, resulting in curves which do not
   * show up in animation editors. In extreme cases, animation may also selectively
   * fail to play back correctly.
   *
   * If such changes are required, these MUST go through the API functions for manipulating
   * these F-Curve groupings. Also, note that groups only apply in actions ONLY.
   */
  prop = api_def_prop(sapi, "channels", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "channels", NULL);
  api_def_prop_struct_type(prop, "FCurve");
  api_def_prop_collection_fns(
      prop, NULL, "api_ActionGroup_channels_next", NULL, NULL, NULL, NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Channels", "F-Curves in this group");

  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", AGRP_SELECTED);
  api_def_prop_ui_text(prop, "Select", "Action group is selected");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_SELECTED, NULL);

  prop = api_def_prop(sapi, "lock", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", AGRP_PROTECTED);
  api_def_prop_ui_text(prop, "Lock", "Action group is locked");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_expanded", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_NO_DEG_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "flag", AGRP_EXPANDED);
  api_def_prop_ui_text(prop, "Expanded", "Action group is expanded except in graph editor");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "show_expanded_graph", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_NO_DEG_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "flag", AGRP_EXPANDED_G);
  api_def_prop_ui_text(
      prop, "Expanded in Graph Editor", "Action group is expanded in graph editor");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "use_pin", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_NO_GRAPH_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "flag", ADT_CURVES_ALWAYS_VISIBLE);
  api_def_prop_ui_text(prop, "Pin in Graph Editor", "");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  /* color set */
  api_def_actionbone_group_common(sapi, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);
}

/* fcurve.keyframe_points */
static void api_def_action_groups(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "ActionGroups");
  sapi = api_def_struct(dapi, "ActionGroups", NULL);
  api_def_struct_stype(sapi, "Action");
  api_def_struct_ui_text(sapi, "Action Groups", "Collection of action groups");

  fn = api_def_fn(sapi, "new", "api_Action_groups_new");
  api_def_fn_ui_description(fn, "Create a new action group and add it to the action");
  parm = api_def_string(fn, "name", "Group", 0, "", "New name for the action group");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  parm = api_def_ptr(fn, "action_group", "ActionGroup", "", "Newly created action group");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Action_groups_remove");
  api_def_fn_ui_description(fn, "Remove action group");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "action_group", "ActionGroup", "", "Action group to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_action_fcurves(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "ActionFCurves");
  sapi = api_def_struct(dapi, "ActionFCurves", NULL);
  api_def_struct_stype(sapi, "Action");
  api_def_struct_ui_text(sapi, "Action F-Curves", "Collection of action F-Curves");

  /* Action.fcurves.new(...) */
  fn = api_def_fn(sapi, "new", "api_Action_fcurve_new");
  api_def_fn_ui_description(fn, "Add an F-Curve to the action");
  api_def_fn_flag(fn, FN_USE_REPORTS | FN_USE_MAIN);
  parm = api_def_string(fn, "data_path", NULL, 0, "Data Path", "F-Curve data path to use");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_int(fn, "index", 0, 0, INT_MAX, "Index", "Array index", 0, INT_MAX);
  api_def_string(
      fn, "action_group", NULL, 0, "Action Group", "Acton group to add this F-Curve into");

  parm = api_def_ptr(fn, "fcurve", "FCurve", "", "Newly created F-Curve");
  api_def_fn_return(fn, parm);

  /* Action.fcurves.find(...) */
  fn = api_def_fn(sapi, "find", "rna_Action_fcurve_find");
  api_def_fn_ui_description(
      fn,
      "Find an F-Curve. Note that this function performs a linear scan "
      "of all F-Curves in the action.");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_string(fn, "data_path", NULL, 0, "Data Path", "F-Curve data path");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_int(fn, "index", 0, 0, INT_MAX, "Index", "Array index", 0, INT_MAX);
  parm = api_def_ptr(
      fn, "fcurve", "FCurve", "", "The found F-Curve, or None if it doesn't exist");
  apj_def_fn_return(fn, parm);

  /* Action.fcurves.remove(...) */
  fn = api_def_fn(sapi, "remove", "api_Action_fcurve_remove");
  api_def_fn_ui_description(fn, "Remove action group");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "fcurve", "FCurve", "", "F-Curve to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_action_pose_markers(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  api_def_prop_sapi(cprop, "ActionPoseMarkers");
  sapi = api_def_struct(dapi, "ActionPoseMarkers", NULL);
  api_def_struct_stype(sapi, "Action");
  api_def_struct_ui_text(sapi, "Action Pose Markers", "Collection of timeline markers");

  fn = api_def_fn(sapi, "new", "api_Action_pose_markers_new");
  api_def_fn_ui_description(fn, "Add a pose marker to the action");
  parm = api_def_string(fb, "name", "Marker", 0, NULL, "New name for the marker (not unique)");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "marker", "TimelineMarker", "", "Newly created marker");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_Action_pose_markers_remove");
  api_def_fn_ui_description(fn, "Remove a timeline marker");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "marker", "TimelineMarker", "", "Timeline marker to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "TimelineMarker");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_LIB_EXCEPTION);
  api_def_prop_ptr_fns(
      prop, "api_Action_active_pose_marker_get", "api_Action_active_pose_marker_set", NULL, NULL);
  api_def_prop_ui_text(prop, "Active Pose Marker", "Active pose marker for this action");

  prop = api_def_prop(sapi, "active_index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "active_marker");
  api_def_prop_flag(prop, PROP_LIB_EXCEPTION);
  api_def_prop_int_fns(prop,
                       "api_Action_active_pose_marker_index_get",
                       "api_Action_active_pose_marker_index_set",
                       "api_Action_active_pose_marker_index_range");
  api_def_prop_ui_text(prop, "Active Pose Marker Index", "Index of active pose marker");
}

static void api_def_action(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Action", "Id");
  api_def_struct_sdna(sapi, "Action");
  api_def_struct_ui_text(sapi, "Action", "A collection of F-Curves for animation");
  api_def_struct_ui_icon(sapi, ICON_ACTION);

  /* collections */
  prop = api_def_prop(sapi, "fcurves", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "curves", NULL);
  api_def_prop_struct_type(prop, "FCurve");
  api_def_prop_ui_text(prop, "F-Curves", "The individual F-Curves that make up the action");
  api_def_action_fcurves(dapi, prop);

  prop = api_def_prop(sapi, "groups", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "groups", NULL);
  api_def_prop_struct_type(prop, "ActionGroup");
  api_def_prop_ui_text(prop, "Groups", "Convenient groupings of F-Curves");
  api_def_action_groups(dapi, prop);

  prop = api_def_prop(sapi, "pose_markers", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "markers", NULL);
  api_def_prop_struct_type(prop, "TimelineMarker");
  /* Use lib exception so the list isn't grayed out;
   * adding/removing is still banned though, see T45689. */
  api_def_prop_flag(prop, PROP_LIB_EXCEPTION);
  api_def_prop_ui_text(
      prop, "Pose Markers", "Markers specific to this action, for labeling poses");
  api_def_action_pose_markers(dapi, prop);

  /* properties */
  prop = api_def_prop(sapi, "use_frame_range", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", ACT_FRAME_RANGE);
  api_def_prop_bool_fns(prop, NULL, "api_Action_use_frame_range_set");
  api_def_prop_ui_text(
      prop,
      "Manual Frame Range",
      "Manually specify the intended playback frame range for the action "
      "(this range is used by some tools, but does not affect animation evaluation)");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "use_cyclic", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_stype(prop, NULL, "flag", ACT_CYCLIC);
  api_def_prop_ui_text(
      prop,
      "Cyclic Animation",
      "The action is intended to be used as a cycle looping over its manually set "
      "playback frame range (enabling this doesn't automatically make it loop)");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "frame_start", PROP_FLOAT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "frame_start");
  api_def_prop_float_fns(prop, NULL, "api_Action_start_frame_set", NULL);
  api_def_prop_ui_range(prop, MINFRAME, MAXFRAME, 100, 2);
  api_def_prop_ui_text(
      prop, "Start Frame", "The start frame of the manually set intended playback range");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_prop(sapi, "frame_end", PROP_FLOAT, PROP_TIME);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_float_stype(prop, NULL, "frame_end");
  api_def_prop_float_fns(prop, NULL, "rna_Action_end_frame_set", NULL);
  api_def_prop_ui_range(prop, MINFRAME, MAXFRAME, 100, 2);
  api_def_prop_ui_text(
      prop, "End Frame", "The end frame of the manually set intended playback range");
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_float_vector(
      sapi,
      "frame_range",
      2,
      NULL,
      0,
      0,
      "Frame Range",
      "The intended playback frame range of this action, using the manually set range "
      "if available, or the combined frame range of all F-Curves within this action "
      "if not (assigning sets the manual frame range)",
      0,
      0);
  api_def_prop_float_fns(
      prop, "api_Action_frame_range_get", "api_Action_frame_range_set", NULL);
  api_def_prop_update(prop, NC_ANIMATION | ND_ANIMCHAN | NA_EDITED, NULL);

  prop = api_def_float_vector(sapi,
                              "curve_frame_range",
                              2,
                              NULL,
                              0,
                              0,
                              "Curve Frame Range",
                              "The combined frame range of all F-Curves within this action",
                              0,
                              0);
  api_def_prop_float_fns(prop, "api_Action_curve_frame_range_get", NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  /* special "type" limiter - should not really be edited in general,
   * but is still available/editable in 'emergencies' */
  prop = api_def_prop(sapi, "id_root", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "idroot");
  api_def_prop_enum_items(prop, api_enum_id_type_items);
  api_def_prop_ui_text(prop,
                       "ID Root Type",
                       "Type of ID block that action can be used on - "
                       "DO NOT CHANGE UNLESS YOU KNOW WHAT YOU ARE DOING");
  api_def_prop_translation_cxt(prop, LANG_CXT_ID_ID);

  /* API calls */
  api_action(sapi);
}

/* --------- */

void api_def_action(DuneApi *dapi)
{
  api_def_action(dapi);
  api_def_action_group(dapi);
  api_def_dopesheet(dapi);
}

#endif
