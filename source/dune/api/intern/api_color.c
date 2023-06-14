#include <stdio.h>
#include <stdlib.h>

#include "types_color.h"
#include "types_texture.h"

#include "lib_utildefines.h"

#include "dune_node_tree_update.h"

#include "api_define.h"
#include "api_internal.h"

#include "wm_api.h"
#include "wm_types.h"

#ifdef API_RUNTIME

#  include "api_access.h"

#  include "types_image.h"
#  include "types_material.h"
#  include "types_movieclip.h"
#  include "types_node.h"
#  include "types_object.h"
#  include "types_particle.h"
#  include "types_sequence_types.h"

#  include "mem_guardedalloc.h"

#  include "dune_colorband.h"
#  include "dune_colortools.h"
#  include "dune_image.h"
#  include "dune_linestyle.h"
#  include "dune_movieclip.h"
#  include "dune_node.h"

#  include "graph.h"

#  include "ed_node.h"

#  include "imbuf_colormanagement.h"
#  include "imbuf.h"

#  include "seq_iterator.h"
#  include "seq_relations.h"

static int api_CurveMapping_curves_length(ApiPtr *ptr)
{
  CurveMapping *cumap = (CurveMapping *)ptr->data;
  int len;

  for (len = 0; len < CM_TOT; len++) {
    if (!cumap->cm[len].curve) {
      break;
    }
  }

  return len;
}

static void api_CurveMapping_curves_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  CurveMapping *cumap = (CurveMapping *)ptr->data;

  api_iter_array_begin(
      iter, cumap->cm, sizeof(CurveMap), api_CurveMapping_curves_length(ptr), 0, NULL);
}

static void api_CurveMapping_clip_set(ApiPtr *ptr, bool value)
{
  CurveMapping *cumap = (CurveMapping *)ptr->data;

  if (value) {
    cumap->flag |= CUMA_DO_CLIP;
  }
  else {
    cumap->flag &= ~CUMA_DO_CLIP;
  }

  dune_curvemapping_changed(cumap, false);
}

static void api_CurveMapping_black_level_set(ApiPtr *ptr, const float *values)
{
  CurveMapping *cumap = (CurveMapping *)ptr->data;
  cumap->black[0] = values[0];
  cumap->black[1] = values[1];
  cumap->black[2] = values[2];
  dune_curvemapping_set_black_white(cumap, NULL, NULL);
}

static void api_CurveMapping_white_level_set(ApiPtr *ptr, const float *values)
{
  CurveMapping *cumap = (CurveMapping *)ptr->data;
  cumap->white[0] = values[0];
  cumap->white[1] = values[1];
  cumap->white[2] = values[2];
  dune_curvemapping_set_black_white(cumap, NULL, NULL);
}

static void api_CurveMapping_tone_update(Main *UNUSED(main),
                                         Scene *UNUSED(scene),
                                         ApiPtr *UNUSED(ptr))
{
  wm_main_add_notifier(NC_NODE | NA_EDITED, NULL);
  wm_main_add_notifier(NC_SCENE | ND_SEQUENCER, NULL);
}

static void api_CurveMapping_extend_update(Main *UNUSED(main),
                                           Scene *UNUSED(scene),
                                           ApiPtr *UNUSED(ptr))
{
  wm_main_add_notifier(NC_NODE | NA_EDITED, NULL);
  wm_main_add_notifier(NC_SCENE | ND_SEQUENCER, NULL);
}

static void api_CurveMapping_clipminx_range(
    ApiPtr *ptr, float *min, float *max, float *UNUSED(softmin), float *UNUSED(softmax))
{
  CurveMapping *cumap = (CurveMapping *)ptr->data;

  *min = -100.0f;
  *max = cumap->clipr.xmax;
}

static void api_CurveMapping_clipminy_range(
    ApiPtr *ptr, float *min, float *max, float *UNUSED(softmin), float *UNUSED(softmax))
{
  CurveMapping *cumap = (CurveMapping *)ptr->data;

  *min = -100.0f;
  *max = cumap->clipr.ymax;
}

static void api_CurveMapping_clipmaxx_range(
   ApiPtr *ptr, float *min, float *max, float *UNUSED(softmin), float *UNUSED(softmax))
{
  CurveMapping *cumap = (CurveMapping *)ptr->data;

  *min = cumap->clipr.xmin;
  *max = 100.0f;
}

static void api_CurveMapping_clipmaxy_range(
    ApiPtr *ptr, float *min, float *max, float *UNUSED(softmin), float *UNUSED(softmax))
{
  CurveMapping *cumap = (CurveMapping *)ptr->data;

  *min = cumap->clipr.ymin;
  *max = 100.0f;
}

static char *api_ColorRamp_path(ApiPtr *ptr)
{
  char *path = NULL;

  /* handle the cases where a single data-block may have 2 ramp types */
  if (ptr->owner_id) {
    Id *id = ptr->owner_id;

    switch (GS(id->name)) {
      case ID_NT: {
        NodeTree *ntree = (NodeTree *)id;
        Node *node;
        ApiPtr node_ptr;
        char *node_path;

        for (node = ntree->nodes.first; node; node = node->next) {
          if (ELEM(node->type, SH_NODE_VALTORGB, CMP_NODE_VALTORGB, TEX_NODE_VALTORGB)) {
            if (node->storage == ptr->data) {
              /* all node color ramp properties called 'color_ramp'
               * prepend path from ID to the node
               */
              api_ptr_create(id, &ApiNode, node, &node_ptr);
              node_path = api_path_from_id_to_struct(&node_ptr);
              path = lib_sprintfn("%s.color_ramp", node_path);
              mem_freen(node_path);
            }
          }
        }
        break;
      }

      case ID_LS: {
        /* may be NULL */
        path = dune_linestyle_path_to_color_ramp((FreestyleLineStyle *)id, (ColorBand *)ptr->data);
        break;
      }

      default:
        /* everything else just uses 'color_ramp' */
        path = lib_strdup("color_ramp");
        break;
    }
  }
  else {
    /* everything else just uses 'color_ramp' */
    path = lib_strdup("color_ramp");
  }

  return path;
}

static char *api_ColorRampElement_path(PointerRNA *ptr)
{
  ApiPtr ramp_ptr;
  ApiProp *prop;
  char *path = NULL;
  int index;

  /* helper macro for use here to try and get the path
   * - this calls the standard code for getting a path to a texture...
   */

#  define COLRAMP_GETPATH \
    { \
      prop = api_struct_find_prop(&ramp_ptr, "elements"); \
      if (prop) { \
        index = api_prop_collection_lookup_index(&ramp_ptr, prop, ptr); \
        if (index != -1) { \
          char *texture_path = api_ColorRamp_path(&ramp_ptr); \
          path = lib_sprintfn("%s.elements[%d]", texture_path, index); \
          mem_freen(texture_path); \
        } \
      } \
    } \
    (void)0

  /* determine the path from the ID-block to the ramp */
  /* FIXME: this is a very slow way to do it, but it will have to suffice... */
  if (ptr->owner_id) {
    Id *id = ptr->owner_id;

    switch (GS(id->name)) {
      case ID_NT: {
        NodeTree *ntree = (NodeTree *)id;
        Node *node;

        for (node = ntree->nodes.first; node; node = node->next) {
          if (ELEM(node->type, SH_NODE_VALTORGB, CMP_NODE_VALTORGB, TEX_NODE_VALTORGB)) {
            api_ptr_create(id, &ApiColorRamp, node->storage, &ramp_ptr);
            COLRAMP_GETPATH;
          }
        }
        break;
      }
      case ID_LS: {
        List list
        LinkData *link;

        dune_linestyle_mod_list_color_ramps((FreestyleLineStyle *)id, &list);
        for (link = (LinkData *)list.first; link; link = link->next) {
          api_ptr_create(id, &ApiColorRamp, link->data, &ramp_ptr);
          COLRAMP_GETPATH;
        }
        lib_freelistn(&list);
        break;
      }

      default: /* everything else should have a "color_ramp" property */
      {
        /* create ptr to the Id block, and try to resolve "color_ramp" pointer */
        api_id_ptr_create(id, &ramp_ptr);
        if (api_path_resolve(&ramp_ptr, "color_ramp", &ramp_ptr, &prop)) {
          COLRAMP_GETPATH;
        }
        break;
      }
    }
  }

  /* cleanup the macro we defined */
#  undef COLRAMP_GETPATH

  return path;
}

static void api_ColorRamp_update(Main *bmain, Scene *UNUSED(scene), PointerRNA *ptr)
{
  if (ptr->owner_id) {
    Id *id = ptr->owner_id;

    switch (GS(id->name)) {
      case ID_MA: {
        Material *ma = (Material *)ptr->owner_id;

        graph_id_tag_update(&ma->id, 0);
        wm_main_add_notifier(NC_MATERIAL | ND_SHADING_DRAW, ma);
        break;
      }
      case ID_NT: {
        NodeTree *ntree = (NodeTree *)id;
        Node *node;

        for (node = ntree->nodes.first; node; node = node->next) {
          if (ELEM(node->type, SH_NODE_VALTORGB, CMP_NODE_VALTORGB, TEX_NODE_VALTORGB)) {
            dune_ntree_update_tag_node_prop(ntree, node);
            ed_node_tree_propagate_change(NULL, main, ntree);
          }
        }
        break;
      }
      case ID_TE: {
        Tex *tex = (Tex *)ptr->owner_id;

        graph_id_tag_update(&tex->id, 0);
        wm_main_add_notifier(NC_TEXTURE, tex);
        break;
      }
      case ID_LS: {
        FreestyleLineStyle *linestyle = (FreestyleLineStyle *)ptr->owner_id;

        wm_main_add_notifier(NC_LINESTYLE, linestyle);
        break;
      }
      /* ColorRamp for particle display is owned by the object (see T54422) */
      case ID_OB:
      case ID_PA: {
        ParticleSettings *part = (ParticleSettings *)ptr->owner_id;

        wm_main_add_notifier(NC_OBJECT | ND_PARTICLE | NA_EDITED, part);
      }
      default:
        break;
    }
  }
}

static void api_ColorRamp_eval(struct ColorBand *coba, float position, float color[4])
{
  api_colorband_evaluate(coba, position, color);
}

static CBData *api_ColorRampElement_new(struct ColorBand *coba,
                                        ReportList *reports,
                                        float position)
{
  CBData *element = dune_colorband_element_add(coba, position);

  if (element == NULL) {
    dune_reportf(reports, RPT_ERROR, "Unable to add element to colorband (limit %d)", MAXCOLORBAND);
  }

  return element;
}

static void api_ColorRampElement_remove(struct ColorBand *coba,
                                        ReportList *reports,
                                        ApiPtr *element_ptr)
{
  CBData *element = element_ptr->data;
  int index = (int)(element - coba->data);
  if (!dune_colorband_element_remove(coba, index)) {
    dune_report(reports, RPT_ERROR, "Element not found in element collection or last element");
    return;
  }
  
   API_PTR_INVALIDATE(element_ptr);
}

static void api_CurveMap_remove_point(CurveMap *cuma, ReportList *reports, ApiPtr *point_ptr)
{
  CurveMapPoint *point = point_ptr->data;
  if (dune_curvemap_remove_point(cuma, point) == false) {
    dune_report(reports, RPT_ERROR, "Unable to remove curve point");
    return;
  }

  API_PTR_INVALIDATE(point_ptr);
}

static void api_Scopes_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Scopes *s = (Scopes *)ptr->data;
  s->ok = 0;
}

static int api_ColorManagedDisplaySettings_display_device_get(struct ApiPtr *ptr)
{
  ColorManagedDisplaySettings *display = (ColorManagedDisplaySettings *)ptr->data;

  return imbuf_colormanagement_display_get_named_index(display->display_device);
}

static void api_ColorManagedDisplaySettings_display_device_set(struct ApiPtr *ptr, int value)
{
  ColorManagedDisplaySettings *display = (ColorManagedDisplaySettings *)ptr->data;
  const char *name = imbuf_colormanagement_display_get_indexed_name(value);

  if (name) {
    lib_strncpy(display->display_device, name, sizeof(display->display_device));
  }
}

static const EnumPropItem *api_ColorManagedDisplaySettings_display_device_itemf(
    Cxt *UNUSED(C), ApiPtr *UNUSED(ptr), ApiProp *UNUSED(prop), bool *r_free)
{
  EnumPropItem *items = NULL;
  int totitem = 0;

  imbuf_colormanagement_display_items_add(&items, &totitem);
  api_enum_item_end(&items, &totitem);

  *r_free = true;

  return items;
}

static void api_ColorManagedDisplaySettings_display_device_update(Main *main,
                                                                  Scene *UNUSED(scene),
                                                                  ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  if (!id) {
    return;
  }

  if (GS(id->name) == ID_SCE) {
    Scene *scene = (Scene *)id;

    imbuf_colormanagement_validate_settings(&scene->display_settings, &scene->view_settings);

    graph_id_tag_update(id, 0);
    wm_main_add_notifier(NC_SCENE | ND_SEQUENCER, NULL);

    /* Color management can be baked into shaders, need to refresh. */
    for (Material *ma = main->materials.first; ma; ma = ma->id.next) {
      graph_id_tag_update(&ma->id, ID_RECALC_COPY_ON_WRITE);
    }
  }
}

static char *api_ColorManagedDisplaySettings_path(ApiPtr *UNUSED(ptr))
{
  return lib_strdup("display_settings");
}

static int api_ColorManagedViewSettings_view_transform_get(ApiPtr *ptr)
{
  ColorManagedViewSettings *view = (ColorManagedViewSettings *)ptr->data;

  return imbuf_colormanagement_view_get_named_index(view->view_transform);
}

static void api_ColorManagedViewSettings_view_transform_set(ApiPtr *ptr, int value)
{
  ColorManagedViewSettings *view = (ColorManagedViewSettings *)ptr->data;

  const char *name = imbuf_colormanagement_view_get_indexed_name(value);

  if (name) {
    lib_strncpy(view->view_transform, name, sizeof(view->view_transform));
  }
}

static const EnumPropItem *api_ColorManagedViewSettings_view_transform_itemf(
    Cxt *C, ApiPtr *UNUSED(ptr), ApiProp *UNUSED(prop), bool *r_free)
{
  Scene *scene = cxt_data_scene(C);
  EnumPropItem *items = NULL;
  ColorManagedDisplaySettings *display_settings = &scene->display_settings;
  int totitem = 0;

  imbuf_colormanagement_view_items_add(&items, &totitem, display_settings->display_device);
  api_enum_item_end(&items, &totitem);

  *r_free = true;
  return items;
}

static int api_ColorManagedViewSettings_look_get(ApiPtr *ptr)
{
  ColorManagedViewSettings *view = (ColorManagedViewSettings *)ptr->data;

  return imbuf_colormanagement_look_get_named_index(view->look);
}

static void api_ColorManagedViewSettings_look_set(ApiPtr *ptr, int value)
{
  ColorManagedViewSettings *view = (ColorManagedViewSettings *)ptr->data;

  const char *name = imbuf_colormanagement_look_get_indexed_name(value);

  if (name) {
    lib_strncpy(view->look, name, sizeof(view->look));
  }
}

static const EnumPropItem *api_ColorManagedViewSettings_look_itemf(Cxt *UNUSED(C),
                                                                   ApiPtr *ptr,
                                                                   ApiProp *UNUSED(prop),
                                                                   bool *r_free)
{
  ColorManagedViewSettings *view = (ColorManagedViewSettings *)ptr->data;
  EnumPropItem *items = NULL;
  int totitem = 0;

  imbuf_colormanagement_look_items_add(&items, &totitem, view->view_transform);
  api_enum_item_end(&items, &totitem);

  *r_free = true;
  return items;
}

static void api_ColorManagedViewSettings_use_curves_set(ApiPtr *ptr, bool value)
{
  ColorManagedViewSettings *view_settings = (ColorManagedViewSettings *)ptr->data;

  if (value) {
    view_settings->flag |= COLORMANAGE_VIEW_USE_CURVES;

    if (view_settings->curve_mapping == NULL) {
      view_settings->curve_mapping = dune_curvemapping_add(4, 0.0f, 0.0f, 1.0f, 1.0f);
    }
  }
  else {
    view_settings->flag &= ~COLORMANAGE_VIEW_USE_CURVES;
  }
}

static char *api_ColorManagedViewSettings_path(ApiPtr *UNUSED(ptr))
{
  return lib_strdup("view_settings");
}

static bool api_ColorManagedColorspaceSettings_is_data_get(struct PointerRNA *ptr)
{
  ColorManagedColorspaceSettings *colorspace = (ColorManagedColorspaceSettings *)ptr->data;
  const char *data_name = imbuf_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA);
  return STREQ(colorspace->name, data_name);
}

static void api_ColorManagedColorspaceSettings_is_data_set(struct PointerRNA *ptr, bool value)
{
  ColorManagedColorspaceSettings *colorspace = (ColorManagedColorspaceSettings *)ptr->data;
  if (value) {
    const char *data_name = imbug_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA);
    STRNCPY(colorspace->name, data_name);
  }
}

static int api_ColorManagedColorspaceSettings_colorspace_get(struct PointerRNA *ptr)
{
  ColorManagedColorspaceSettings *colorspace = (ColorManagedColorspaceSettings *)ptr->data;

  return imbuf_colormanagement_colorspace_get_named_index(colorspace->name);
}

static void api_ColorManagedColorspaceSettings_colorspace_set(struct PointerRNA *ptr, int value)
{
  ColorManagedColorspaceSettings *colorspace = (ColorManagedColorspaceSettings *)ptr->data;
  const char *name = imbuf_colormanagement_colorspace_get_indexed_name(value);

  if (name && name[0]) {
    lib_strncpy(colorspace->name, name, sizeof(colorspace->name));
  }
}

static const EnumPropItem *api_ColorManagedColorspaceSettings_colorspace_itemf(
    Cxt *UNUSED(C), ApiPtr *UNUSED(ptr), ApiProp *UNUSED(prop), bool *r_free)
{
  EnumPropItem *items = NULL;
  int totitem = 0;

  imbuf_colormanagement_colorspace_items_add(&items, &totitem);
  api_enum_item_end(&items, &totitem);

  *r_free = true;

  return items;
}

typedef struct seq_colorspace_cb_data {
  ColorManagedColorspaceSettings *colorspace_settings;
  Seq *r_seq;
} seq_colorspace_cb_data;

static bool seq_find_colorspace_settings_cb(Seq *seq, void *user_data)
{
  seq_colorspace_cb_data *cd = (seq_colorspace_cb_data *)user_data;
  if (seq->strip && &seq->strip->colorspace_settings == cd->colorspace_settings) {
    cd->r_seq = seq;
    return false;
  }
  return true;
}

static bool seq_free_anim_cb(Seq *seq, void *UNUSED(user_data))
{
  seq_relations_seq_free_anim(seq);
  return true;
}

static void api_ColorManagedColorspaceSettings_reload_update(Main *main,
                                                             Scene *UNUSED(scene),
                                                             ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  if (GS(id->name) == ID_IM) {
    Image *ima = (Image *)id;

    graph_id_tag_update(&ima->id, 0);

    dune_image_signal(main, ima, NULL, IMA_SIGNAL_COLORMANAGE);

    wm_main_add_notifier(NC_IMAGE | ND_DISPLAY, &ima->id);
    wm_main_add_notifier(NC_IMAGE | NA_EDITED, &ima->id);
  }
  else if (GS(id->name) == ID_MC) {
    MovieClip *clip = (MovieClip *)id;

    graph_id_tag_update(&clip->id, ID_RECALC_SOURCE);
    seq_relations_invalidate_movieclip_strips(main, clip);

    wm_main_add_notifier(NC_MOVIECLIP | ND_DISPLAY, &clip->id);
    wm_main_add_notifier(NC_MOVIECLIP | NA_EDITED, &clip->id);
  }
  else if (GS(id->name) == ID_SCE) {
    Scene *scene = (Scene *)id;
    seq_relations_invalidate_scene_strips(bmain, scene);

    if (scene->ed) {
      ColorManagedColorspaceSettings *colorspace_settings = (ColorManagedColorspaceSettings *)
                                                                ptr->data;
      seq_colorspace_cb_data cb_data = {colorspace_settings, NULL};

      if (&scene->seq_colorspace_settings != colorspace_settings) {
        seq_for_each_cb(&scene->ed->seqbase, seq_find_colorspace_settings_cb, &cb_data);
      }
      Seq *seq = cb_data.r_seq;

      if (seq) {
        seq_relations_seq_free_anim(seq);

        if (seq->strip->proxy && seq->strip->proxy->anim) {
          imbuf_free_anim(seq->strip->proxy->anim);
          seq->strip->proxy->anim = NULL;
        }

        seq_relations_invalidate_cache_raw(scene, seq);
      }
      else {
        seq_for_each_cb(&scene->ed->seqbase, seq_free_anim_cb, NULL);
      }

      wm_main_add_notifier(NC_SCENE | ND_SEQ, NULL);
    }
  }
}

static char *api_ColorManagedSequencerColorspaceSettings_path(ApiPtr *UNUSED(ptr))
{
  return lib_strdup("sequencer_colorspace_settings");
}

static char *api_ColorManagedInputColorspaceSettings_path(ApiPtr *UNUSED(ptr))
{
  return lib_strdup("colorspace_settings");
}

static void api_ColorManagement_update(Main *UNUSED(main), Scene *UNUSED(scene), PointerRNA *ptr)
{
  Id *id = ptr->owner_id;

  if (!id) {
    return;
  }

  if (GS(id->name) == ID_SCE) {
    graph_id_tag_update(id, 0);
    wm_main_add_notifier(NC_SCENE | ND_SEQ, NULL);
  }
}

/* this function only exists because #dune_curvemap_evaluateF uses a 'const' qualifier */
static float api_CurveMapping_evaluateF(struct CurveMapping *cumap,
                                        ReportList *reports,
                                        struct CurveMap *cuma,
                                        float value)
{
  if (&cumap->cm[0] != cuma && &cumap->cm[1] != cuma && &cumap->cm[2] != cuma &&
      &cumap->cm[3] != cuma) {
    dune_report(reports, RPT_ERROR, "CurveMapping does not own CurveMap");
    return 0.0f;
  }

  if (!cuma->table) {
    dune_curvemapping_init(cumap);
  }
  return dune_curvemap_evaluateF(cumap, cuma, value);
}

static void api_CurveMap_initialize(struct CurveMapping *cumap)
{
  dune_curvemapping_init(cumap);
}
#else

static void api_def_curvemappoint(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;
  static const EnumPropItem prop_handle_type_items[] = {
      {0, "AUTO", 0, "Auto Handle", ""},
      {CUMA_HANDLE_AUTO_ANIM, "AUTO_CLAMPED", 0, "Auto Clamped Handle", ""},
      {CUMA_HANDLE_VECTOR, "VECTOR", 0, "Vector Handle", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "CurveMapPoint", NULL);
  api_def_struct_ui_text(sapi, "CurveMapPoint", "Point of a curve used for a curve mapping");

  prop = api_def_prop(sapi, "location", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "x");
  api_def_prop_array(prop, 2);
  api_def_prop_ui_text(prop, "Location", "X/Y coordinates of the curve point");

  prop = api_def_prop(sapi, "handle_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, prop_handle_type_items);
  api_def_prop_ui_text(
      prop, "Handle Type", "Curve interpolation at this point: Bezier or vector");

  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CUMA_SELECT);
  api_def_prop_ui_text(prop, "Select", "Selection state of the curve point");
}

static void api_def_curvemap_points_api(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *parm;
  ApiFn *fn;

  api_def_prop_sapi(cprop, "CurveMapPoints");
  sapi = api_def_struct(dapi, "CurveMapPoints", NULL);
  api_def_struct_stype(sapi, "CurveMap");
  api_def_struct_ui_text(sapi, "Curve Map Point", "Collection of Curve Map Points");

  fn = api_def_fn(sapi, "new", "dune_curvemap_insert");
  api_def_fn_ui_description(fn, "Add point to CurveMap");
  parm = api_def_float(fn,
                       "position",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "Position",
                       "Position to add point",
                       -FLT_MAX,
                       FLT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float(
      fn, "value", 0.0f, -FLT_MAX, FLT_MAX, "Value", "Value of point", -FLT_MAX, FLT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "point", "CurveMapPoint", "", "New point");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_CurveMap_remove_point");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  api_def_fn_ui_description(fn, "Delete point from CurveMap");
  parm = api_def_ptr(fn, "point", "CurveMapPoint", "", "PointElement to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_curvemap(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "CurveMap", NULL);
  api_def_struct_ui_text(sapi, "CurveMap", "Curve in a curve mapping");

  prop = api_def_prop(sapi, "points", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "curve", "totpoint");
  api_def_prop_struct_type(prop, "CurveMapPoint");
  api_def_prop_ui_text(prop, "Points", "");
  api_def_curvemap_points_api(dapi, prop);
}

static void api_def_curvemapping(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop, *parm;
  ApiFn *fn;

  static const EnumPropItem tone_items[] = {
      {CURVE_TONE_STANDARD, "STANDARD", 0, "Standard", ""},
      {CURVE_TONE_FILMLIKE, "FILMLIKE", 0, "Filmlike", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_extend_items[] = {
      {0, "HORIZONTAL", 0, "Horizontal", ""},
      {CUMA_EXTEND_EXTRAPOLATE, "EXTRAPOLATED", 0, "Extrapolated", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "CurveMapping", NULL);
  api_def_struct_ui_text(
      sapi,
      "CurveMapping",
      "Curve mapping to map color, vector and scalar values to other values using "
      "a user defined curve");

  prop = api_def_prop(sapi, "tone", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "tone");
  api_def_prop_enum_items(prop, tone_items);
  api_def_prop_ui_text(prop, "Tone", "Tone of the curve");
  api_def_prop_update(prop, 0, "api_CurveMapping_tone_update");

  prop = api_def_prop(sapi, "use_clip", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", CUMA_DO_CLIP);
  api_def_prop_ui_text(prop, "Clip", "Force the curve view to fit a defined boundary");
  apu_def_prop_bool_fns(prop, NULL, "api_CurveMapping_clip_set");

  prop = api_def_prop(sapi, "clip_min_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "clipr.xmin");
  api_def_prop_range(prop, -100.0f, 100.0f);
  api_def_prop_ui_text(prop, "Clip Min X", "");
  api_def_prop_float_fns(prop, NULL, NULL, "rna_CurveMapping_clipminx_range");

  prop = api_def_prop(sapi, "clip_min_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "clipr.ymin");
  api_def_prop_range(prop, -100.0f, 100.0f);
  api_def_prop_ui_text(prop, "Clip Min Y", "");
  api_def_prop_float_fns(prop, NULL, NULL, "api_CurveMapping_clipminy_range");

  prop = api_def_prop(sapi, "clip_max_x", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_style(prop, NULL, "clipr.xmax");
  api_def_prop_range(prop, -100.0f, 100.0f);
  api_def_prop_ui_text(prop, "Clip Max X", "");
  api_def_prop_float_fns(prop, NULL, NULL, "api_CurveMapping_clipmaxx_range");

  prop = api_def_prop(sapi, "clip_max_y", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "clipr.ymax");
  api_def_prop_range(prop, -100.0f, 100.0f);
  api_def_prop_ui_text(prop, "Clip Max Y", "");
  api_def_prop_float_fns(prop, NULL, NULL, "api_CurveMapping_clipmaxy_range");

  prop = api_def_prop(sapi, "extend", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_bitflag_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, prop_extend_items);
  api_def_prop_ui_text(prop, "Extend", "Extrapolate the curve or extend it horizontally");
  api_def_prop_update(prop, 0, "api_CurveMapping_extend_update");

  prop = api_def_prop(sapi, "curves", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_fns(prop,
                              "api_CurveMapping_curves_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_CurveMapping_curves_length",
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_struct_type(prop, "CurveMap");
  api_def_prop_ui_text(prop, "Curves", "");

  prop = api_def_prop(sapi, "black_level", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "black");
  api_def_prop_range(prop, -FLT_MAX, FLT_MAX);
  api_def_prop_ui_range(prop, -1000.0f, 1000.0f, 1, 3);
  api_def_prop_ui_text(
      prop, "Black Level", "For RGB curves, the color that black is mapped to");
  api_def_prop_float_fns(prop, NULL, "rna_CurveMapping_black_level_set", NULL);

  prop = api_def_prop(sapi, "white_level", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "white");
  api_def_prop_range(prop, -FLT_MAX, FLT_MAX);
  api_def_prop_ui_range(prop, -1000.0f, 1000.0f, 1, 3);
  api_def_prop_ui_text(
      prop, "White Level", "For RGB curves, the color that white is mapped to");
  api_def_prop_float_fns(prop, NULL, "api_CurveMapping_white_level_set", NULL);

  fn = api_def_fn(sapi, "update", "dune_curvemapping_changed_all");
  api_def_fn_ui_description(fn, "Update curve mapping after making changes");

  fn = api_def_fn(sapi, "reset_view", "dune_curvemapping_reset_view");
  api_def_fn_ui_description(fb, "Reset the curve mapping grid to its clipping size");

  fn = api_def_fn(sapi, "initialize", "rna_CurveMap_initialize");
  api_def_fn_ui_description(fn, "Initialize curve");

  fn = api_def_fn(sapi, "evaluate", "rna_CurveMapping_evaluateF");
  api_def_fn_flag(fn, FUNC_USE_REPORTS);
  api_def_fn_ui_description(fn, "Evaluate curve at given location");
  parm = api_def_ptr(fn, "curve", "CurveMap", "curve", "Curve to evaluate");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = api_def_float(fn,
                       "position",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "Position",
                       "Position to evaluate curve at",
                       -FLT_MAX,
                       FLT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_float(fn,
                       "value",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "Value",
                       "Value of curve at given location",
                       -FLT_MAX,
                       FLT_MAX);
  api_def_fn_return(fn, parm);
}

static void api_def_color_ramp_element(BlenderRNA *brna)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ColorRampElement", NULL);
  api_def_struct_stype(sapi, "CBData");
  api_def_struct_path_fn(sapi, "api_ColorRampElement_path");
  api_def_struct_ui_text(
      sapi, "Color Ramp Element", "Element defining a color at a position in the color ramp");

  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "r");
  api_def_prop_array(prop, 4);
  api_def_prop_ui_text(prop, "Color", "Set color of selected color stop");
  api_def_prop_update(prop, 0, "api_ColorRamp_update");

  prop = api_def_prop(sapi, "alpha", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "a");
  api_def_prop_ui_text(prop, "Alpha", "Set alpha of selected color stop");
  api_def_prop_update(prop, 0, "api_ColorRamp_update");

  prop = api_def_prop(sapi, "position", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "pos");
  api_def_prop_range(prop, 0, 1);
  api_def_prop_ui_range(prop, 0, 1, 1, 3);
  api_def_prop_ui_text(prop, "Position", "Set position of selected color stop");
  api_def_prop_update(prop, 0, "api_ColorRamp_update");
}

static void api_def_color_ramp_element_api(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *parm;
  ApiFn *fn;

  api_def_prop_sapi(cprop, "ColorRampElements");
  sapi = api_def_struct(dapi, "ColorRampElements", NULL);
  api_def_struct_stype(sapi, "ColorBand");
  api_def_struct_path_fn(sapi, "api_ColorRampElement_path");
  api_def_struct_ui_text(srna, "Color Ramp Elements", "Collection of Color Ramp Elements");

  /* TODO: make these functions generic in `texture.c`. */
  fn = api_def_fn(sapi, "new", "api_ColorRampElement_new");
  api_def_fn_ui_description(fn, "Add element to ColorRamp");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_float(
      fn, "position", 0.0f, 0.0f, 1.0f, "Position", "Position to add element", 0.0f, 1.0f);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return type */
  parm = api_def_ptr(fn, "element", "ColorRampElement", "", "New element");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_ColorRampElement_remove");
  api_def_fn_ui_description(fn, "Delete element from ColorRamp");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "element", "ColorRampElement", "", "Element to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void api_def_color_ramp(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  static const EnumPropItem prop_interpolation_items[] = {
      {COLBAND_INTERP_EASE, "EASE", 0, "Ease", ""},
      {COLBAND_INTERP_CARDINAL, "CARDINAL", 0, "Cardinal", ""},
      {COLBAND_INTERP_LINEAR, "LINEAR", 0, "Linear", ""},
      {COLBAND_INTERP_B_SPLINE, "B_SPLINE", 0, "B-Spline", ""},
      {COLBAND_INTERP_CONSTANT, "CONSTANT", 0, "Constant", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_mode_items[] = {
      {COLBAND_BLEND_RGB, "RGB", 0, "RGB", ""},
      {COLBAND_BLEND_HSV, "HSV", 0, "HSV", ""},
      {COLBAND_BLEND_HSL, "HSL", 0, "HSL", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_hsv_items[] = {
      {COLBAND_HUE_NEAR, "NEAR", 0, "Near", ""},
      {COLBAND_HUE_FAR, "FAR", 0, "Far", ""},
      {COLBAND_HUE_CW, "CW", 0, "Clockwise", ""},
      {COLBAND_HUE_CCW, "CCW", 0, "Counter-Clockwise", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "ColorRamp", NULL);
  api_def_struct_stype(sapi, "ColorBand");
  api_def_struct_path_fn(sapi, "api_ColorRamp_path");
  api_def_struct_ui_text(sapi, "Color Ramp", "Color ramp mapping a scalar value to a color");

  prop = api_def_prop(sapi, "elements", PROP_COLLECTION, PROP_COLOR);
  api_def_prop_collection_stype(prop, NULL, "data", "tot");
  api_def_prop_struct_type(prop, "ColorRampElement");
  api_def_prop_ui_text(prop, "Elements", "");
  api_def_prop_update(prop, 0, "rna_ColorRamp_update");
  api_def_color_ramp_element_api(brna, prop);

  prop = api_def_prop(sapi, "interpolation", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "ipotype");
  api_def_prop_enum_items(prop, prop_interpolation_items);
  api_def_prop_ui_text(prop, "Interpolation", "Set interpolation between color stops");
  api_def_prop_update(prop, 0, "rna_ColorRamp_update");

  prop = api_def_prop(sapi, "hue_interpolation", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "ipotype_hue");
  api_def_prop_enum_items(prop, prop_hsv_items);
  api_def_prop_ui_text(prop, "Color Interpolation", "Set color interpolation");
  api_def_prop_update(prop, 0, "api_ColorRamp_update");

  prop = api_def_prop(sapi, "color_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_sdna(prop, NULL, "color_mode");
  api_def_prop_enum_items(prop, prop_mode_items);
  api_def_prop_ui_text(prop, "Color Mode", "Set color mode to use for interpolation");
  api_def_prop_update(prop, 0, "api_ColorRamp_update");

#  if 0 /* use len(elements) */
  prop = api_def_prop_stype(, "total", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "tot");
  /* needs a function to do the right thing when adding elements like colorband_add_cb() */
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_range(prop, 0, 31); /* MAXCOLORBAND = 32 */
  api_def_prop_ui_text(prop, "Total", "Total number of elements");
  api_def_prop_update(prop, 0, "api_ColorRamp_update");
#  endif

  fn = api_def_fn(sapi, "evaluate", "api_ColorRamp_eval");
  api_def_fn_ui_description(fn, "Evaluate ColorRamp");
  parm = api_def_float(fn,
                       "position",
                       1.0f,
                       0.0f,
                       1.0f,
                       "Position",
                       "Evaluate ColorRamp at position",
                       0.0f,
                       1.0f);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  /* return */
  parm = api_def_float_color(fn,
                             "color",
                             4,
                             NULL,
                             -FLT_MAX,
                             FLT_MAX,
                             "Color",
                             "Color at given position",
                             -FLT_MAX,
                             FLT_MAX);
  api_def_param_flags(parm, PROP_THICK_WRAP, 0);
  api_def_fn_output(fn, parm);
}

static void api_def_histogram(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem prop_mode_items[] = {
      {HISTO_MODE_LUMA, "LUMA", 0, "Luma", "Luma"},
      {HISTO_MODE_RGB, "RGB", 0, "RGB", "Red Green Blue"},
      {HISTO_MODE_R, "R", 0, "R", "Red"},
      {HISTO_MODE_G, "G", 0, "G", "Green"},
      {HISTO_MODE_B, "B", 0, "B", "Blue"},
      {HISTO_MODE_ALPHA, "A", 0, "A", "Alpha"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "Histogram", NULL);
  api_def_struct_ui_text(sapi, "Histogram", "Statistical view of the levels of color in an image");

  prop = api_def_prop(sapi, "mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "mode");
  api_def_prop_enum_items(prop, prop_mode_items);
  api_def_prop_ui_text(prop, "Mode", "Channels to display in the histogram");

  prop = api_def_prop(sapi, "show_line", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", HISTO_FLAG_LINE);
  api_def_prop_ui_text(prop, "Show Line", "Display lines rather than filled shapes");
  api_def_prop_ui_icon(prop, ICON_GRAPH, 0);
}

static void api_def_scopes(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem prop_wavefrm_mode_items[] = {
      {SCOPES_WAVEFRM_LUMA, "LUMA", ICON_COLOR, "Luma", ""},
      {SCOPES_WAVEFRM_RGB_PARADE, "PARADE", ICON_COLOR, "Parade", ""},
      {SCOPES_WAVEFRM_YCC_601, "YCBCR601", ICON_COLOR, "YCbCr (ITU 601)", ""},
      {SCOPES_WAVEFRM_YCC_709, "YCBCR709", ICON_COLOR, "YCbCr (ITU 709)", ""},
      {SCOPES_WAVEFRM_YCC_JPEG, "YCBCRJPG", ICON_COLOR, "YCbCr (Jpeg)", ""},
      {SCOPES_WAVEFRM_RGB, "RGB", ICON_COLOR, "Red Green Blue", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "Scopes", NULL);
  api_def_struct_ui_text(sapi, "Scopes", "Scopes for statistical view of an image");

  prop = api_def_prop(sapi, "use_full_resolution", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, "Scopes", "sample_full", 1);
  api_def_prop_ui_text(prop, "Full Sample", "Sample every pixel of the image");
  api_def_prop_update(prop, 0, "api_Scopes_update");

  prop = api_def_prop(sapi, "accuracy", PROP_FLOAT, PROP_PERCENTAGE);
  api_def_prop_float_stype(prop, "Scopes", "accuracy");
  api_def_prop_range(prop, 0.0, 100.0);
  api_def_prop_ui_range(prop, 0.0, 100.0, 10, 1);
  api_def_prop_ui_text(
      prop, "Accuracy", "Proportion of original image source pixel lines to sample");
  api_def_prop_update(prop, 0, "rna_Scopes_update");

  prop = api_def_prop(sapi, "histogram", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, "Scopes", "hist");
  api_def_prop_struct_type(prop, "Histogram");
  api_def_prop_ui_text(prop, "Histogram", "Histogram for viewing image statistics");

  prop = api_def_prop(sapi, "waveform_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, "Scopes", "wavefrm_mode");
  api_def_prop_enum_items(prop, prop_wavefrm_mode_items);
  api_def_prop_ui_text(prop, "Waveform Mode", "");
  api_def_prop_update(prop, 0, "api_Scopes_update");

  prop = api_def_prop(sapi, "waveform_alpha", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, "Scopes", "wavefrm_alpha");
  api_def_prop_range(prop, 0, 1);
  api_def_prop_ui_text(prop, "Waveform Opacity", "Opacity of the points");

  prop = api_def_prop(sapi, "vectorscope_alpha", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, "Scopes", "vecscope_alpha");
  api_def_prop_range(prop, 0, 1);
  api_def_prop_ui_text(prop, "Vectorscope Opacity", "Opacity of the points");
}

static void api_def_colormanage(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem display_device_items[] = {
      {0, "NONE", 0, "None", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem look_items[] = {
      {0, "NONE", 0, "None", "Do not modify image in an artistic manner"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem view_transform_items[] = {
      {0,
       "NONE",
       0,
       "None",
       "Do not perform any color transform on display, use old non-color managed technique for "
       "display"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem color_space_items[] = {
      {0,
       "NONE",
       0,
       "None",
       "Do not perform any color transform on load, treat colors as in scene linear space "
       "already"},
      {0, NULL, 0, NULL, NULL},
  };

  /* ** Display Settings ** */
  sapi = api_def_struct(dapi, "ColorManagedDisplaySettings", NULL);
  api_def_struct_path_fn(sapi, "rna_ColorManagedDisplaySettings_path");
  api_def_struct_ui_text(
      sapi, "ColorManagedDisplaySettings", "Color management specific to display device");

  prop = api_def_prop(sapi, "display_device", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, display_device_items);
  api_def_prop_enum_fns(prop,
                        "api_ColorManagedDisplaySettings_display_device_get",
                        "api_ColorManagedDisplaySettings_display_device_set",
                        "appi_ColorManagedDisplaySettings_display_device_itemf");
  api_def_prop_ui_text(prop, "Display Device", "Display device name");
  api_def_prop_update(
      prop, NC_WINDOW, "api_ColorManagedDisplaySettings_display_device_update");

  /* ** View Settings ** */
  sapi = api_def_struct(dapi, "ColorManagedViewSettings", NULL);
  api_def_struct_path_fn(sapi, "api_ColorManagedViewSettings_path");
  api_def_struct_ui_text(sapi,
                         "ColorManagedViewSettings",
                         "Color management settings used for displaying images on the display");

  prop = api_def_prop(sapi, "look", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, look_items);
  api_def_prop_enum_fns(prop,
                       "api_ColorManagedViewSettings_look_get",
                       "api_ColorManagedViewSettings_look_set",
                       "api_ColorManagedViewSettings_look_itemf");
  api_def_prop_ui_text(
      prop, "Look", "Additional transform applied before view transform for artistic needs");
  api_def_prop_update(prop, NC_WINDOW, "api_ColorManagement_update");

  prop = api_def_prop(sapi, "view_transform", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, view_transform_items);
  api_def_prop_enum_fns(prop,
                        "api_ColorManagedViewSettings_view_transform_get",
                        "api_ColorManagedViewSettings_view_transform_set",
                        "api_ColorManagedViewSettings_view_transform_itemf");
  api_def_prop_ui_text(
      prop, "View Transform", "View used when converting image to a display space");
  api_def_prop_update(prop, NC_WINDOW, "api_ColorManagement_update");

  prop = api_def_prop(sapi, "exposure", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "exposure");
  api_def_prop_float_default(prop, 0.0f);
  api_def_prop_range(prop, -32.0f, 32.0f);
  api_def_prop_ui_range(prop, -10.0f, 10.0f, 1, 3);
  api_def_prop_ui_text(prop, "Exposure", "Exposure (stops) applied before display transform");
  api_def_prop_update(prop, NC_WINDOW, "api_ColorManagement_update");

  prop = api_def_prop(sapi, "gamma", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_stype(prop, NULL, "gamma");
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_range(prop, 0.0f, 5.0f);
  api_def_prop_ui_text(
      prop, "Gamma", "Amount of gamma modification applied after display transform");
  api_def_prop_update(prop, NC_WINDOW, "api_ColorManagement_update");

  prop = api_def_prop(sapi, "curve_mapping", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "curve_mapping");
  api_def_prop_ui_text(prop, "Curve", "Color curve mapping applied before display transform");
  api_def_prop_update(prop, NC_WINDOW, "api_ColorManagement_update");

  prop = api_def_prop(sapi, "use_curve_mapping", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", COLORMANAGE_VIEW_USE_CURVES);
  api_def_prop_bool_fns(prop, NULL, "api_ColorManagedViewSettings_use_curves_set");
  api_def_prop_ui_text(prop, "Use Curves", "Use RGB curved for pre-display transformation");
  api_def_prop_update(prop, NC_WINDOW, "api_ColorManagement_update");

  /* ** Colorspace ** */
  sapi = api_def_struct(dapi, "ColorManagedInputColorspaceSettings", NULL);
  api_def_struct_path_fn(sapi, "api_ColorManagedInputColorspaceSettings_path");
  api_def_struct_ui_text(
      sapi, "ColorManagedInputColorspaceSettings", "Input color space settings");

  prop = api_def_prop(sapi, "name", PROP_ENUM, PROP_NONE);
  api_def_prop_flag(prop, PROP_ENUM_NO_CONTEXT);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_enum_items(prop, color_space_items);
  api_def_prop_enum_fns(prop,
                        "api_ColorManagedColorspaceSettings_colorspace_get",
                        "api_ColorManagedColorspaceSettings_colorspace_set",
                        "api_ColorManagedColorspaceSettings_colorspace_itemf");
  api_def_prop_ui_text(
      prop,
      "Input Color Space",
      "Color space in the image file, to convert to and from when saving and loading the image");
  api_def_prop_update(prop, NC_WINDOW, "api_ColorManagedColorspaceSettings_reload_update");

  prop = api_def_prop(sapi, "is_data", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_bool_fns(prop,
                        "api_ColorManagedColorspaceSettings_is_data_get",
                        "api_ColorManagedColorspaceSettings_is_data_set");
  api_def_prop_ui_text(
      prop,
      "Is Data",
      "Treat image as non-color data without color management, like normal or displacement maps");
  api_def_prop_update(prop, NC_WINDOW, "api_ColorManagement_update");

  //
  sapi = api_def_struct(dapi, "ColorManagedSequencerColorspaceSettings", NULL);
  api_def_struct_path_fn(sapi, "api_ColorManagedSequencerColorspaceSettings_path");
  api_def_struct_ui_text(
      sapi, "ColorManagedSequencerColorspaceSettings", "Input color space settings");

  prop = api_def_prop(sapi, "name", PROP_ENUM, PROP_NONE);
  api_def_prop_flag(prop, PROP_ENUM_NO_CONTEXT);
  api_def_prop_enum_items(prop, color_space_items);
  api_def_prop_enum_fns(prop,
                              "api_ColorManagedColorspaceSettings_colorspace_get",
                              "api_ColorManagedColorspaceSettings_colorspace_set",
                              "api_ColorManagedColorspaceSettings_colorspace_itemf");
  api_def_prop_ui_text(prop, "Color Space", "Color space that the sequencer operates in");
  api_def_prop_update(prop, NC_WINDOW, "rna_ColorManagedColorspaceSettings_reload_update");
}

void api_def_color(DuneApi *dapi)
{
  api_def_curvemappoint(dapi);
  api_def_curvemap(dapi);
  api_def_curvemapping(dapi);
  api_def_color_ramp_element(dapi);
  api_def_color_ramp(dapi);
  api_def_histogram(dapi);
  api_def_scopes(dapi);
  api_def_colormanage(dapu);
}

#endif
