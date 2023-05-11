#include <stdlib.h>

#include "BLI_math.h"

#include "DNA_brush_types.h"
#include "DNA_curve_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "WM_types.h"

/* parent type */
static const EnumPropertyItem parent_type_items[] = {
    {PAROBJECT, "OBJECT", 0, "Object", "The layer is parented to an object"},
    {PARSKEL, "ARMATURE", 0, "Armature", ""},
    {PARBONE, "BONE", 0, "Bone", "The layer is parented to a bone"},
    {0, NULL, 0, NULL, NULL},
};

#ifndef RNA_RUNTIME
static EnumPropertyItem rna_enum_gpencil_stroke_depth_order_items[] = {
    {GP_DRAWMODE_2D,
     "2D",
     0,
     "2D Layers",
     "Display strokes using grease pencil layers to define order"},
    {GP_DRAWMODE_3D, "3D", 0, "3D Location", "Display strokes using real 3D position in 3D space"},
    {0, NULL, 0, NULL, NULL},
};

static EnumPropertyItem rna_enum_gpencil_onion_modes_items[] = {
    {GP_ONION_MODE_ABSOLUTE,
     "ABSOLUTE",
     0,
     "Frames",
     "Frames in absolute range of the scene frame"},
    {GP_ONION_MODE_RELATIVE,
     "RELATIVE",
     0,
     "Keyframes",
     "Frames in relative range of the Grease Pencil keyframes"},
    {GP_ONION_MODE_SELECTED, "SELECTED", 0, "Selected", "Only selected keyframes"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem rna_enum_keyframe_type_items[] = {
    {BEZT_KEYTYPE_KEYFRAME,
     "KEYFRAME",
     ICON_KEYTYPE_KEYFRAME_VEC,
     "Keyframe",
     "Normal keyframe - e.g. for key poses"},
    {BEZT_KEYTYPE_BREAKDOWN,
     "BREAKDOWN",
     ICON_KEYTYPE_BREAKDOWN_VEC,
     "Breakdown",
     "A breakdown pose - e.g. for transitions between key poses"},
    {BEZT_KEYTYPE_MOVEHOLD,
     "MOVING_HOLD",
     ICON_KEYTYPE_MOVING_HOLD_VEC,
     "Moving Hold",
     "A keyframe that is part of a moving hold"},
    {BEZT_KEYTYPE_EXTREME,
     "EXTREME",
     ICON_KEYTYPE_EXTREME_VEC,
     "Extreme",
     "An 'extreme' pose, or some other purpose as needed"},
    {BEZT_KEYTYPE_JITTER,
     "JITTER",
     ICON_KEYTYPE_JITTER_VEC,
     "Jitter",
     "A filler or baked keyframe for keying on ones, or some other purpose as needed"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem rna_enum_onion_keyframe_type_items[] = {
    {-1, "ALL", 0, "All", "Include all Keyframe types"},
    {BEZT_KEYTYPE_KEYFRAME,
     "KEYFRAME",
     ICON_KEYTYPE_KEYFRAME_VEC,
     "Keyframe",
     "Normal keyframe - e.g. for key poses"},
    {BEZT_KEYTYPE_BREAKDOWN,
     "BREAKDOWN",
     ICON_KEYTYPE_BREAKDOWN_VEC,
     "Breakdown",
     "A breakdown pose - e.g. for transitions between key poses"},
    {BEZT_KEYTYPE_MOVEHOLD,
     "MOVING_HOLD",
     ICON_KEYTYPE_MOVING_HOLD_VEC,
     "Moving Hold",
     "A keyframe that is part of a moving hold"},
    {BEZT_KEYTYPE_EXTREME,
     "EXTREME",
     ICON_KEYTYPE_EXTREME_VEC,
     "Extreme",
     "An 'extreme' pose, or some other purpose as needed"},
    {BEZT_KEYTYPE_JITTER,
     "JITTER",
     ICON_KEYTYPE_JITTER_VEC,
     "Jitter",
     "A filler or baked keyframe for keying on ones, or some other purpose as needed"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem rna_enum_gplayer_move_type_items[] = {
    {-1, "UP", 0, "Up", ""},
    {1, "DOWN", 0, "Down", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropertyItem rna_enum_layer_blend_modes_items[] = {
    {eGplBlendMode_Regular, "REGULAR", 0, "Regular", ""},
    {eGplBlendMode_HardLight, "HARDLIGHT", 0, "Hard Light", ""},
    {eGplBlendMode_Add, "ADD", 0, "Add", ""},
    {eGplBlendMode_Subtract, "SUBTRACT", 0, "Subtract", ""},
    {eGplBlendMode_Multiply, "MULTIPLY", 0, "Multiply", ""},
    {eGplBlendMode_Divide, "DIVIDE", 0, "Divide", ""},
    {0, NULL, 0, NULL, NULL}};

static const EnumPropertyItem rna_enum_gpencil_caps_modes_items[] = {
    {GP_STROKE_CAP_ROUND, "ROUND", 0, "Rounded", ""},
    {GP_STROKE_CAP_FLAT, "FLAT", 0, "Flat", ""},
    {0, NULL, 0, NULL, NULL},
};
#endif

#ifdef RNA_RUNTIME

#  include "BLI_ghash.h"
#  include "BLI_listbase.h"
#  include "BLI_string_utils.h"

#  include "WM_api.h"

#  include "BKE_action.h"
#  include "BKE_animsys.h"
#  include "BKE_deform.h"
#  include "BKE_gpencil.h"
#  include "BKE_gpencil_curve.h"
#  include "BKE_gpencil_geom.h"
#  include "BKE_gpencil_update_cache.h"
#  include "BKE_icons.h"

#  include "DEG_depsgraph.h"
#  include "DEG_depsgraph_build.h"

static void rna_GPencil_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
#  if 0
  /* In case a property on a layer changed, tag it with a light update. */
  if (ptr->type == &RNA_GPencilLayer) {
    BKE_gpencil_tag_light_update((bGPdata *)(ptr->owner_id), (bGPDlayer *)(ptr->data), NULL, NULL);
  }
#  endif
  DEG_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
  WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
}

static void rna_GpencilLayerMatrix_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;

  loc_eul_size_to_mat4(gpl->layer_mat, gpl->location, gpl->rotation, gpl->scale);
  invert_m4_m4(gpl->layer_invmat, gpl->layer_mat);

  rna_GPencil_update(bmain, scene, ptr);
}

static void rna_GPencil_curve_edit_mode_toggle(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  ToolSettings *ts = scene->toolsettings;
  bGPdata *gpd = (bGPdata *)ptr->owner_id;

  /* Curve edit mode is turned on. */
  if (GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd)) {
    /* If the current select mode is segment and the Bezier mode is on, change
     * to Point because segment is not supported. */
    if (ts->gpencil_selectmode_edit == GP_SELECTMODE_SEGMENT) {
      ts->gpencil_selectmode_edit = GP_SELECTMODE_POINT;
    }

    BKE_gpencil_strokes_selected_update_editcurve(gpd);
  }
  /* Curve edit mode is turned off. */
  else {
    BKE_gpencil_strokes_selected_sync_selection_editcurve(gpd);
  }

  /* Standard update. */
  rna_GPencil_update(bmain, scene, ptr);
}

static void rna_GPencil_stroke_curve_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;

  if (GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd)) {
    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
      if (gpl->actframe != NULL) {
        bGPDframe *gpf = gpl->actframe;
        LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
          if (gps->editcurve != NULL) {
            gps->flag |= GP_STROKE_NEEDS_CURVE_UPDATE;
            BKE_gpencil_stroke_geometry_update(gpd, gps);
          }
        }
      }
    }
  }

  rna_GPencil_update(bmain, scene, ptr);
}

static void rna_GPencil_stroke_curve_resolution_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;

  if (GPENCIL_CURVE_EDIT_SESSIONS_ON(gpd)) {
    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
      if (gpl->actframe != NULL) {
        bGPDframe *gpf = gpl->actframe;
        LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
          if (gps->editcurve != NULL) {
            gps->flag |= GP_STROKE_NEEDS_CURVE_UPDATE;
            BKE_gpencil_stroke_geometry_update(gpd, gps);
          }
        }
      }
    }
  }
  rna_GPencil_update(bmain, scene, ptr);
}

static void rna_GPencil_dependency_update(Main *bmain, Scene *UNUSED(scene), PointerRNA *ptr)
{
  DEG_id_tag_update(ptr->owner_id, ID_RECALC_TRANSFORM);
  DEG_relations_tag_update(bmain);
  WM_main_add_notifier(NC_OBJECT | ND_PARENT, ptr->owner_id);

  DEG_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
  WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
}

static void rna_GPencil_uv_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  /* Force to recalc the UVs. */
  bGPDstroke *gps = (bGPDstroke *)ptr->data;

  /* Calc geometry data. */
  BKE_gpencil_stroke_geometry_update(gpd, gps);

  DEG_id_tag_update(ptr->owner_id, ID_RECALC_GEOMETRY);
  WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
}

static void rna_GPencil_autolock(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  BKE_gpencil_layer_autolock_set(gpd, true);

  /* standard update */
  rna_GPencil_update(bmain, scene, ptr);
}

static void rna_GPencil_editmode_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);

  /* Notify all places where GPencil data lives that the editing state is different */
  WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
  WM_main_add_notifier(NC_SCENE | ND_MODE | NC_MOVIECLIP, NULL);
}

/* Poll Callback to filter GP Datablocks to only show those for Annotations */
bool rna_GPencil_datablocks_annotations_poll(PointerRNA *UNUSED(ptr), const PointerRNA value)
{
  bGPdata *gpd = value.data;
  return (gpd->flag & GP_DATA_ANNOTATIONS) != 0;
}

/* Poll Callback to filter GP Datablocks to only show those for GP Objects */
bool rna_GPencil_datablocks_obdata_poll(PointerRNA *UNUSED(ptr), const PointerRNA value)
{
  bGPdata *gpd = value.data;
  return (gpd->flag & GP_DATA_ANNOTATIONS) == 0;
}

static char *rna_GPencilLayer_path(PointerRNA *ptr)
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;
  char name_esc[sizeof(gpl->info) * 2];

  BLI_str_escape(name_esc, gpl->info, sizeof(name_esc));

  return BLI_sprintfN("layers[\"%s\"]", name_esc);
}

static int rna_GPencilLayer_active_frame_editable(PointerRNA *ptr, const char **UNUSED(r_info))
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;

  /* surely there must be other criteria too... */
  if (gpl->flag & GP_LAYER_LOCKED) {
    return 0;
  }
  else {
    return PROP_EDITABLE;
  }
}

/* set parent */
static void set_parent(bGPDlayer *gpl, Object *par, const int type, const char *substr)
{
  if (type == PAROBJECT) {
    invert_m4_m4(gpl->inverse, par->obmat);
    gpl->parent = par;
    gpl->partype |= PAROBJECT;
    gpl->parsubstr[0] = 0;
  }
  else if (type == PARSKEL) {
    invert_m4_m4(gpl->inverse, par->obmat);
    gpl->parent = par;
    gpl->partype |= PARSKEL;
    gpl->parsubstr[0] = 0;
  }
  else if (type == PARBONE) {
    bPoseChannel *pchan = BKE_pose_channel_find_name(par->pose, substr);
    if (pchan) {
      float tmp_mat[4][4];
      mul_m4_m4m4(tmp_mat, par->obmat, pchan->pose_mat);

      invert_m4_m4(gpl->inverse, tmp_mat);
      gpl->parent = par;
      gpl->partype |= PARBONE;
      BLI_strncpy(gpl->parsubstr, substr, sizeof(gpl->parsubstr));
    }
    else {
      invert_m4_m4(gpl->inverse, par->obmat);
      gpl->parent = par;
      gpl->partype |= PAROBJECT;
      gpl->parsubstr[0] = 0;
    }
  }
}

/* set parent object and inverse matrix */
static void rna_GPencilLayer_parent_set(PointerRNA *ptr,
                                        PointerRNA value,
                                        struct ReportList *UNUSED(reports))
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;
  Object *par = (Object *)value.data;

  if (par != NULL) {
    set_parent(gpl, par, gpl->partype, gpl->parsubstr);
  }
  else {
    /* clear parent */
    gpl->parent = NULL;
  }
}

/* set parent type */
static void rna_GPencilLayer_parent_type_set(PointerRNA *ptr, int value)
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;
  Object *par = gpl->parent;
  gpl->partype = value;

  if (par != NULL) {
    set_parent(gpl, par, value, gpl->parsubstr);
  }
}

/* set parent bone */
static void rna_GPencilLayer_parent_bone_set(PointerRNA *ptr, const char *value)
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;

  Object *par = gpl->parent;
  gpl->partype = PARBONE;

  if (par != NULL) {
    set_parent(gpl, par, gpl->partype, value);
  }
}

static char *rna_GPencilLayerMask_path(PointerRNA *ptr)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  bGPDlayer *gpl = BKE_gpencil_layer_active_get(gpd);
  bGPDlayer_Mask *mask = (bGPDlayer_Mask *)ptr->data;

  char gpl_info_esc[sizeof(gpl->info) * 2];
  char mask_name_esc[sizeof(mask->name) * 2];

  BLI_str_escape(gpl_info_esc, gpl->info, sizeof(gpl_info_esc));
  BLI_str_escape(mask_name_esc, mask->name, sizeof(mask_name_esc));

  return BLI_sprintfN("layers[\"%s\"].mask_layers[\"%s\"]", gpl_info_esc, mask_name_esc);
}

static int rna_GPencil_active_mask_index_get(PointerRNA *ptr)
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;
  return gpl->act_mask - 1;
}

static void rna_GPencil_active_mask_index_set(PointerRNA *ptr, int value)
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;
  gpl->act_mask = value + 1;
}

static void rna_GPencil_active_mask_index_range(
    PointerRNA *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;

  *min = 0;
  *max = max_ii(0, BLI_listbase_count(&gpl->mask_layers) - 1);
}

/* parent types enum */
static const EnumPropertyItem *rna_Object_parent_type_itemf(bContext *UNUSED(C),
                                                            PointerRNA *ptr,
                                                            PropertyRNA *UNUSED(prop),
                                                            bool *r_free)
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;
  EnumPropertyItem *item = NULL;
  int totitem = 0;

  RNA_enum_items_add_value(&item, &totitem, parent_type_items, PAROBJECT);

  if (gpl->parent) {
    Object *par = gpl->parent;

    if (par->type == OB_ARMATURE) {
      /* special hack: prevents this being overridden */
      RNA_enum_items_add_value(&item, &totitem, &parent_type_items[1], PARSKEL);
      RNA_enum_items_add_value(&item, &totitem, parent_type_items, PARBONE);
    }
  }

  RNA_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static bool rna_GPencilLayer_is_parented_get(PointerRNA *ptr)
{
  bGPDlayer *gpl = (bGPDlayer *)ptr->data;
  return (gpl->parent != NULL);
}

static PointerRNA rna_GPencil_active_layer_get(PointerRNA *ptr)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;

  if (GS(gpd->id.name) == ID_GD) { /* why would this ever be not GD */
    bGPDlayer *gl;

    for (gl = gpd->layers.first; gl; gl = gl->next) {
      if (gl->flag & GP_LAYER_ACTIVE) {
        break;
      }
    }

    if (gl) {
      return rna_pointer_inherit_refine(ptr, &RNA_GPencilLayer, gl);
    }
  }

  return rna_pointer_inherit_refine(ptr, NULL, NULL);
}

static void rna_GPencil_active_layer_set(PointerRNA *ptr,
                                         PointerRNA value,
                                         struct ReportList *UNUSED(reports))
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;

  /* Don't allow setting active layer to NULL if layers exist
   * as this breaks various tools. Tools should be used instead
   * if it's necessary to remove layers
   */
  if (value.data == NULL) {
    printf("%s: Setting active layer to None is not allowed\n", __func__);
    return;
  }

  if (GS(gpd->id.name) == ID_GD) { /* why would this ever be not GD */
    bGPDlayer *gl;

    for (gl = gpd->layers.first; gl; gl = gl->next) {
      if (gl == value.data) {
        gl->flag |= GP_LAYER_ACTIVE;
      }
      else {
        gl->flag &= ~GP_LAYER_ACTIVE;
      }
    }

    WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
  }
}

static int rna_GPencil_active_layer_index_get(PointerRNA *ptr)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  bGPDlayer *gpl = BKE_gpencil_layer_active_get(gpd);

  return BLI_findindex(&gpd->layers, gpl);
}

static void rna_GPencil_active_layer_index_set(PointerRNA *ptr, int value)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  bGPDlayer *gpl = BLI_findlink(&gpd->layers, value);

  BKE_gpencil_layer_active_set(gpd, gpl);

  /* Now do standard updates... */
  DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);
  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);
}

static void rna_GPencil_active_layer_index_range(
    PointerRNA *ptr, int *min, int *max, int *softmin, int *softmax)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;

  *min = 0;
  *max = max_ii(0, BLI_listbase_count(&gpd->layers) - 1);

  *softmin = *min;
  *softmax = *max;
}

static const EnumPropertyItem *rna_GPencil_active_layer_itemf(bContext *C,
                                                              PointerRNA *ptr,
                                                              PropertyRNA *UNUSED(prop),
                                                              bool *r_free)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  bGPDlayer *gpl;
  EnumPropertyItem *item = NULL, item_tmp = {0};
  int totitem = 0;
  int i = 0;

  if (ELEM(NULL, C, gpd)) {
    return DummyRNA_NULL_items;
  }

  /* Existing layers */
  for (gpl = gpd->layers.first, i = 0; gpl; gpl = gpl->next, i++) {
    item_tmp.identifier = gpl->info;
    item_tmp.name = gpl->info;
    item_tmp.value = i;

    item_tmp.icon = (gpd->flag & GP_DATA_ANNOTATIONS) ? BKE_icon_gplayer_color_ensure(gpl) :
                                                        ICON_GREASEPENCIL;

    RNA_enum_item_add(&item, &totitem, &item_tmp);
  }

  RNA_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static void rna_GPencilLayer_info_set(PointerRNA *ptr, const char *value)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  bGPDlayer *gpl = ptr->data;

  char oldname[128] = "";
  BLI_strncpy(oldname, gpl->info, sizeof(oldname));

  /* copy the new name into the name slot */
  BLI_strncpy_utf8(gpl->info, value, sizeof(gpl->info));

  BLI_uniquename(
      &gpd->layers, gpl, DATA_("GP_Layer"), '.', offsetof(bGPDlayer, info), sizeof(gpl->info));

  /* now fix animation paths */
  BKE_animdata_fix_paths_rename_all(&gpd->id, "layers", oldname, gpl->info);

  /* Fix mask layers. */
  LISTBASE_FOREACH (bGPDlayer *, gpl_, &gpd->layers) {
    LISTBASE_FOREACH (bGPDlayer_Mask *, mask, &gpl_->mask_layers) {
      if (STREQ(mask->name, oldname)) {
        BLI_strncpy(mask->name, gpl->info, sizeof(mask->name));
      }
    }
  }
}

static void rna_GPencilLayer_mask_info_set(PointerRNA *ptr, const char *value)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  bGPDlayer_Mask *mask = ptr->data;
  char oldname[128] = "";
  BLI_strncpy(oldname, mask->name, sizeof(oldname));

  /* Really is changing the layer name. */
  bGPDlayer *gpl = BKE_gpencil_layer_named_get(gpd, oldname);
  if (gpl) {
    /* copy the new name into the name slot */
    BLI_strncpy_utf8(gpl->info, value, sizeof(gpl->info));

    BLI_uniquename(
        &gpd->layers, gpl, DATA_("GP_Layer"), '.', offsetof(bGPDlayer, info), sizeof(gpl->info));

    /* now fix animation paths */
    BKE_animdata_fix_paths_rename_all(&gpd->id, "layers", oldname, gpl->info);

    /* Fix mask layers. */
    LISTBASE_FOREACH (bGPDlayer *, gpl_, &gpd->layers) {
      LISTBASE_FOREACH (bGPDlayer_Mask *, mask_, &gpl_->mask_layers) {
        if (STREQ(mask_->name, oldname)) {
          BLI_strncpy(mask_->name, gpl->info, sizeof(mask_->name));
        }
      }
    }
  }
}

static bGPDstroke *rna_GPencil_stroke_point_find_stroke(const bGPdata *gpd,
                                                        const bGPDspoint *pt,
                                                        bGPDlayer **r_gpl,
                                                        bGPDframe **r_gpf)
{
  bGPDlayer *gpl;
  bGPDstroke *gps;

  /* sanity checks */
  if (ELEM(NULL, gpd, pt)) {
    return NULL;
  }

  if (r_gpl) {
    *r_gpl = NULL;
  }
  if (r_gpf) {
    *r_gpf = NULL;
  }

  /* there's no faster alternative than just looping over everything... */
  for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
    if (gpl->actframe) {
      for (gps = gpl->actframe->strokes.first; gps; gps = gps->next) {
        if ((pt >= gps->points) && (pt < &gps->points[gps->totpoints])) {
          /* found it */
          if (r_gpl) {
            *r_gpl = gpl;
          }
          if (r_gpf) {
            *r_gpf = gpl->actframe;
          }

          return gps;
        }
      }
    }
  }

  /* didn't find it */
  return NULL;
}

static void rna_GPencil_stroke_point_select_set(PointerRNA *ptr, const bool value)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  bGPDspoint *pt = ptr->data;
  bGPDstroke *gps = NULL;

  /* Ensure that corresponding stroke is set
   * - Since we don't have direct access, we're going to have to search
   * - We don't apply selection value unless we can find the corresponding
   *   stroke, so that they don't get out of sync
   */
  gps = rna_GPencil_stroke_point_find_stroke(gpd, pt, NULL, NULL);
  if (gps) {
    /* Set the new selection state for the point */
    if (value) {
      pt->flag |= GP_SPOINT_SELECT;
    }
    else {
      pt->flag &= ~GP_SPOINT_SELECT;
    }

    /* Check if the stroke should be selected or not... */
    BKE_gpencil_stroke_sync_selection(gpd, gps);
  }
}

static void rna_GPencil_stroke_point_add(
    ID *id, bGPDstroke *stroke, int count, float pressure, float strength)
{
  bGPdata *gpd = (bGPdata *)id;

  if (count > 0) {
    /* create space at the end of the array for extra points */
    stroke->points = MEM_recallocN_id(
        stroke->points, sizeof(bGPDspoint) * (stroke->totpoints + count), "gp_stroke_points");
    stroke->dvert = MEM_recallocN_id(
        stroke->dvert, sizeof(MDeformVert) * (stroke->totpoints + count), "gp_stroke_weight");

    /* init the pressure and strength values so that old scripts won't need to
     * be modified to give these initial values...
     */
    for (int i = 0; i < count; i++) {
      bGPDspoint *pt = stroke->points + (stroke->totpoints + i);
      MDeformVert *dvert = stroke->dvert + (stroke->totpoints + i);
      pt->pressure = pressure;
      pt->strength = strength;

      dvert->totweight = 0;
      dvert->dw = NULL;
    }

    stroke->totpoints += count;

    /* Calc geometry data. */
    BKE_gpencil_stroke_geometry_update(gpd, stroke);

    DEG_id_tag_update(&gpd->id,
                      ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);

    WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
  }
}

static void rna_GPencil_stroke_point_pop(ID *id,
                                         bGPDstroke *stroke,
                                         ReportList *reports,
                                         int index)
{
  bGPdata *gpd = (bGPdata *)id;
  bGPDspoint *pt_tmp = stroke->points;
  MDeformVert *pt_dvert = stroke->dvert;

  /* python style negative indexing */
  if (index < 0) {
    index += stroke->totpoints;
  }

  if (stroke->totpoints <= index || index < 0) {
    BKE_report(reports, RPT_ERROR, "GPencilStrokePoints.pop: index out of range");
    return;
  }

  stroke->totpoints--;

  stroke->points = MEM_callocN(sizeof(bGPDspoint) * stroke->totpoints, "gp_stroke_points");
  if (pt_dvert != NULL) {
    stroke->dvert = MEM_callocN(sizeof(MDeformVert) * stroke->totpoints, "gp_stroke_weights");
  }

  if (index > 0) {
    memcpy(stroke->points, pt_tmp, sizeof(bGPDspoint) * index);
    /* verify weight data is available */
    if (pt_dvert != NULL) {
      memcpy(stroke->dvert, pt_dvert, sizeof(MDeformVert) * index);
    }
  }

  if (index < stroke->totpoints) {
    memcpy(&stroke->points[index],
           &pt_tmp[index + 1],
           sizeof(bGPDspoint) * (stroke->totpoints - index));
    if (pt_dvert != NULL) {
      memcpy(&stroke->dvert[index],
             &pt_dvert[index + 1],
             sizeof(MDeformVert) * (stroke->totpoints - index));
    }
  }

  /* free temp buffer */
  MEM_freeN(pt_tmp);
  if (pt_dvert != NULL) {
    MEM_freeN(pt_dvert);
  }

  /* Calc geometry data. */
  BKE_gpencil_stroke_geometry_update(gpd, stroke);

  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);

  WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
}

static void rna_GPencil_stroke_point_update(ID *id, bGPDstroke *stroke)
{
  bGPdata *gpd = (bGPdata *)id;

  /* Calc geometry data. */
  if (stroke) {
    BKE_gpencil_stroke_geometry_update(gpd, stroke);

    DEG_id_tag_update(&gpd->id,
                      ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);

    WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
  }
}

static float rna_GPencilStrokePoints_weight_get(bGPDstroke *stroke,
                                                ReportList *reports,
                                                int vertex_group_index,
                                                int point_index)
{
  MDeformVert *dvert = stroke->dvert;
  if (dvert == NULL) {
    BKE_report(reports, RPT_ERROR, "Groups: No groups for this stroke");
    return -1.0f;
  }

  if (stroke->totpoints <= point_index || point_index < 0) {
    BKE_report(reports, RPT_ERROR, "GPencilStrokePoints: index out of range");
    return -1.0f;
  }

  MDeformVert *pt_dvert = stroke->dvert + point_index;
  if ((pt_dvert) && (pt_dvert->totweight <= vertex_group_index || vertex_group_index < 0)) {
    BKE_report(reports, RPT_ERROR, "Groups: index out of range");
    return -1.0f;
  }

  MDeformWeight *dw = BKE_defvert_find_index(pt_dvert, vertex_group_index);
  if (dw) {
    return dw->weight;
  }

  return -1.0f;
}

static void rna_GPencilStrokePoints_weight_set(
    bGPDstroke *stroke, ReportList *reports, int vertex_group_index, int point_index, float weight)
{
  BKE_gpencil_dvert_ensure(stroke);

  MDeformVert *dvert = stroke->dvert;
  if (dvert == NULL) {
    BKE_report(reports, RPT_ERROR, "Groups: No groups for this stroke");
    return;
  }

  if (stroke->totpoints <= point_index || point_index < 0) {
    BKE_report(reports, RPT_ERROR, "GPencilStrokePoints: index out of range");
    return;
  }

  MDeformVert *pt_dvert = stroke->dvert + point_index;
  MDeformWeight *dw = BKE_defvert_ensure_index(pt_dvert, vertex_group_index);
  if (dw) {
    dw->weight = weight;
  }
}

static bGPDstroke *rna_GPencil_stroke_new(bGPDframe *frame)
{
  bGPDstroke *stroke = BKE_gpencil_stroke_new(0, 0, 1.0f);
  BLI_addtail(&frame->strokes, stroke);

  return stroke;
}

static void rna_GPencil_stroke_remove(ID *id,
                                      bGPDframe *frame,
                                      ReportList *reports,
                                      PointerRNA *stroke_ptr)
{
  bGPdata *gpd = (bGPdata *)id;

  bGPDstroke *stroke = stroke_ptr->data;
  if (BLI_findindex(&frame->strokes, stroke) == -1) {
    BKE_report(reports, RPT_ERROR, "Stroke not found in grease pencil frame");
    return;
  }

  BLI_remlink(&frame->strokes, stroke);
  BKE_gpencil_free_stroke(stroke);
  RNA_POINTER_INVALIDATE(stroke_ptr);

  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
}

static void rna_GPencil_stroke_close(ID *id,
                                     bGPDframe *frame,
                                     ReportList *reports,
                                     PointerRNA *stroke_ptr)
{
  bGPdata *gpd = (bGPdata *)id;
  bGPDstroke *stroke = stroke_ptr->data;
  if (BLI_findindex(&frame->strokes, stroke) == -1) {
    BKE_report(reports, RPT_ERROR, "Stroke not found in grease pencil frame");
    return;
  }

  BKE_gpencil_stroke_close(stroke);

  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
}

static void rna_GPencil_stroke_select_set(PointerRNA *ptr, const bool value)
{
  bGPdata *gpd = (bGPdata *)ptr->owner_id;
  bGPDstroke *gps = ptr->data;
  bGPDspoint *pt;
  int i;

  /* set new value */
  if (value) {
    gps->flag |= GP_STROKE_SELECT;
    BKE_gpencil_stroke_select_index_set(gpd, gps);
  }
  else {
    gps->flag &= ~GP_STROKE_SELECT;
    BKE_gpencil_stroke_select_index_reset(gps);
  }

  /* ensure that the stroke's points are selected in the same way */
  for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
    if (value) {
      pt->flag |= GP_SPOINT_SELECT;
    }
    else {
      pt->flag &= ~GP_SPOINT_SELECT;
    }
  }
}

static void rna_GPencil_curve_select_set(PointerRNA *ptr, const bool value)
{
  bGPDcurve *gpc = ptr->data;

  /* Set new value. */
  if (value) {
    gpc->flag |= GP_CURVE_SELECT;
  }
  else {
    gpc->flag &= ~GP_CURVE_SELECT;
  }
  /* Ensure that the curves's points are selected in the same way. */
  for (int i = 0; i < gpc->tot_curve_points; i++) {
    bGPDcurve_point *gpc_pt = &gpc->curve_points[i];
    BezTriple *bezt = &gpc_pt->bezt;
    if (value) {
      gpc_pt->flag |= GP_CURVE_POINT_SELECT;
      BEZT_SEL_ALL(bezt);
    }
    else {
      gpc_pt->flag &= ~GP_CURVE_POINT_SELECT;
      BEZT_DESEL_ALL(bezt);
    }
  }
}

static bGPDframe *rna_GPencil_frame_new(bGPDlayer *layer,
                                        ReportList *reports,
                                        int frame_number,
                                        bool active)
{
  bGPDframe *frame;

  if (BKE_gpencil_layer_frame_find(layer, frame_number)) {
    BKE_reportf(reports, RPT_ERROR, "Frame already exists on this frame number %d", frame_number);
    return NULL;
  }

  frame = BKE_gpencil_frame_addnew(layer, frame_number);
  if (active) {
    layer->actframe = BKE_gpencil_layer_frame_get(layer, frame_number, GP_GETFRAME_USE_PREV);
  }
  WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);

  return frame;
}

static void rna_GPencil_frame_remove(bGPDlayer *layer, ReportList *reports, PointerRNA *frame_ptr)
{
  bGPDframe *frame = frame_ptr->data;
  if (BLI_findindex(&layer->frames, frame) == -1) {
    BKE_report(reports, RPT_ERROR, "Frame not found in grease pencil layer");
    return;
  }

  BKE_gpencil_layer_frame_delete(layer, frame);
  RNA_POINTER_INVALIDATE(frame_ptr);

  WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
}

static bGPDframe *rna_GPencil_frame_copy(bGPDlayer *layer, bGPDframe *src)
{
  bGPDframe *frame = BKE_gpencil_frame_duplicate(src, true);

  while (BKE_gpencil_layer_frame_find(layer, frame->framenum)) {
    frame->framenum++;
  }

  BLI_addtail(&layer->frames, frame);

  WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);

  return frame;
}

static bGPDlayer *rna_GPencil_layer_new(bGPdata *gpd, const char *name, bool setactive)
{
  bGPDlayer *gpl = BKE_gpencil_layer_addnew(gpd, name, setactive != 0, false);

  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);

  return gpl;
}

static void rna_GPencil_layer_remove(bGPdata *gpd, ReportList *reports, PointerRNA *layer_ptr)
{
  bGPDlayer *layer = layer_ptr->data;
  if (BLI_findindex(&gpd->layers, layer) == -1) {
    BKE_report(reports, RPT_ERROR, "Layer not found in grease pencil data");
    return;
  }

  BKE_gpencil_layer_delete(gpd, layer);
  RNA_POINTER_INVALIDATE(layer_ptr);

  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
}

static void rna_GPencil_layer_move(bGPdata *gpd,
                                   ReportList *reports,
                                   PointerRNA *layer_ptr,
                                   int type)
{
  bGPDlayer *gpl = layer_ptr->data;
  if (BLI_findindex(&gpd->layers, gpl) == -1) {
    BKE_report(reports, RPT_ERROR, "Layer not found in grease pencil data");
    return;
  }

  BLI_assert(ELEM(type, -1, 0, 1)); /* we use value below */

  const int direction = type * -1;

  if (BLI_listbase_link_move(&gpd->layers, gpl, direction)) {
    DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  }

  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
}

static void rna_GPencil_layer_mask_add(bGPDlayer *gpl, PointerRNA *layer_ptr)
{
  bGPDlayer *gpl_mask = layer_ptr->data;

  BKE_gpencil_layer_mask_add(gpl, gpl_mask->info);

  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
}

static void rna_GPencil_layer_mask_remove(bGPDlayer *gpl,
                                          ReportList *reports,
                                          PointerRNA *mask_ptr)
{
  bGPDlayer_Mask *mask = mask_ptr->data;
  if (BLI_findindex(&gpl->mask_layers, mask) == -1) {
    BKE_report(reports, RPT_ERROR, "Mask not found in mask list");
    return;
  }

  BKE_gpencil_layer_mask_remove(gpl, mask);
  RNA_POINTER_INVALIDATE(mask_ptr);

  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
}

static void rna_GPencil_frame_clear(bGPDframe *frame)
{
  BKE_gpencil_free_strokes(frame);

  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
}

static void rna_GPencil_layer_clear(bGPDlayer *layer)
{
  BKE_gpencil_free_frames(layer);

  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
}

static void rna_GPencil_clear(bGPdata *gpd)
{
  BKE_gpencil_free_layers(&gpd->layers);

  WM_main_add_notifier(NC_GPENCIL | ND_DATA | NA_EDITED, NULL);
}

static char *rna_GreasePencilGrid_path(PointerRNA *UNUSED(ptr))
{
  return BLI_strdup("grid");
}

static void rna_GpencilCurvePoint_BezTriple_handle1_get(PointerRNA *ptr, float *values)
{
  bGPDcurve_point *cpt = (bGPDcurve_point *)ptr->data;
  copy_v3_v3(values, cpt->bezt.vec[0]);
}

static void rna_GpencilCurvePoint_BezTriple_handle1_set(PointerRNA *ptr, const float *values)
{
  bGPDcurve_point *cpt = (bGPDcurve_point *)ptr->data;
  copy_v3_v3(cpt->bezt.vec[0], values);
}

static bool rna_GpencilCurvePoint_BezTriple_handle1_select_get(PointerRNA *ptr)
{
  bGPDcurve_point *cpt = (bGPDcurve_point *)ptr->data;
  return cpt->bezt.f1;
}

static void rna_GpencilCurvePoint_BezTriple_handle1_select_set(PointerRNA *ptr, const bool value)
{
  bGPDcurve_point *cpt = (bGPDcurve_point *)ptr->data;
  cpt->bezt.f1 = value;
}

static void rna_GpencilCurvePoint_BezTriple_handle2_get(PointerRNA *ptr, float *values)
{
  bGPDcurve_point *cpt = (bGPDcurve_point *)ptr->data;
  copy_v3_v3(values, cpt->bezt.vec[2]);
}

static void rna_GpencilCurvePoint_BezTriple_handle2_set(PointerRNA *ptr, const float *values)
{
  bGPDcurve_point *cpt = (bGPDcurve_point *)ptr->data;
  copy_v3_v3(cpt->bezt.vec[2], values);
}
