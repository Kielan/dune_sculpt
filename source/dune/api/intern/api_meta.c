#include <stdlib.h>

#include "types_mesh.h"
#include "types_meta.h"

#include "lib_utildefines.h"

#include "api_access.h"
#include "api_define.h"
#include "apo_enum_types.h"

#include "api_internal.h"

#ifdef API_RUNTIME

#  include "lib_math.h"

#  include "mem_guardedalloc.h"

#  include "types_object.h"
#  include "types_scene.h"

#  include "dune_main.h"
#  include "dune_mball.h"
#  include "dune_scene.h"

#  include "graph.h"

#  include "wm_api.h"
#  include "wm_types.h"

static int api_Meta_texspace_editable(ApiPtr *ptr, const char **UNUSED(r_info))
{
  MetaBall *mb = (MetaBall *)ptr->data;
  return (mb->texflag & MB_AUTOSPACE) ? 0 : PROP_EDITABLE;
}

static void api_Meta_texspace_loc_get(ApiPtr *ptr, float *values)
{
  MetaBall *mb = (MetaBall *)ptr->data;

  /* tex_space_mball() needs object.. ugh */

  copy_v3_v3(values, mb->loc);
}

static void api_Meta_texspace_loc_set(ApiPtr *ptr, const float *values)
{
  MetaBall *mb = (MetaBall *)ptr->data;

  copy_v3_v3(mb->loc, values);
}

static void api_Meta_texspace_size_get(ApiPtr *ptr, float *values)
{
  MetaBall *mb = (MetaBall *)ptr->data;

  /* tex_space_mball() needs object.. ugh */

  copy_v3_v3(values, mb->size);
}

static void api_Meta_texspace_size_set(ApiPtr *ptr, const float *values)
{
  MetaBall *mb = (MetaBall *)ptr->data;

  copy_v3_v3(mb->size, values);
}

static void api_MetaBall_redraw_data(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  graph_id_tag_update(id, ID_RECALC_COPY_ON_WRITE);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);
}

static void api_MetaBall_update_data(Main *main, Scene *scene, ApiPtr *ptr)
{
  MetaBall *mb = (MetaBall *)ptr->owner_id;
  Object *ob;

  /* cheating way for importers to avoid slow updates */
  if (mb->id.us > 0) {
    for (ob = main->objects.first; ob; ob = ob->id.next) {
      if (ob->data == mb) {
        dune_mball_props_copy(scene, ob);
      }
    }

    graph_id_tag_update(&mb->id, 0);
    wm_main_add_notifier(NC_GEOM | ND_DATA, mb);
  }
}

static void api_MetaBall_update_rotation(Main *main, Scene *scene, ApiPtr *ptr)
{
  MetaElem *ml = ptr->data;
  normalize_qt(ml->quat);
  api_MetaBall_update_data(main, scene, ptr);
}

static MetaElem *api_MetaBall_elements_new(MetaBall *mb, int type)
{
  MetaElem *ml = dune_mball_element_add(mb, type);

  /* cheating way for importers to avoid slow updates */
  if (mb->id.us > 0) {
    graph_id_tag_update(&mb->id, 0);
    wm_main_add_notifier(NC_GEOM | ND_DATA, &mb->id);
  }

  return ml;
}

static void api_MetaBall_elements_remove(MetaBall *mb, ReportList *reports, ApiPtr *ml_ptr)
{
  MetaElem *ml = ml_ptr->data;

  if (lib_remlink_safe(&mb->elems, ml) == false) {
    dune_reportf(
        reports, RPT_ERROR, "Metaball '%s' does not contain spline given", mb->id.name + 2);
    return;
  }

  mem_freen(ml);
  API_PTR_INVALIDATE(ml_ptr);

  /* cheating way for importers to avoid slow updates */
  if (mb->id.us > 0) {
    graph_id_tag_update(&mb->id, 0);
    wm_main_add_notifier(NC_GEOM | ND_DATA, &mb->id);
  }
}

static void api_MetaBall_elements_clear(MetaBall *mb)
{
  lib_freelistn(&mb->elems);

  /* cheating way for importers to avoid slow updates */
  if (mb->id.us > 0) {
    graph_id_tag_update(&mb->id, 0);
    wm_main_add_notifier(NC_GEOM | ND_DATA, &mb->id);
  }
}

static bool api_Meta_is_editmode_get(PointerRNA *ptr)
{
  MetaBall *mb = (MetaBall *)ptr->owner_id;
  return (mb->editelems != NULL);
}

static char *api_MetaElement_path(PointerRNA *ptr)
{
  MetaBall *mb = (MetaBall *)ptr->owner_id;
  MetaElem *ml = ptr->data;
  int index = -1;

  if (mb->editelems) {
    index = lib_findindex(mb->editelems, ml);
  }
  if (index == -1) {
    index = lib_findindex(&mb->elems, ml);
  }
  if (index == -1) {
    return NULL;
  }

  return lib_sprintfn("elements[%d]", index);
}

#else

static void api_def_metaelement(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MetaElement", NULL);
  api_def_struct_stype(sapi, "MetaElem");
  api_def_struct_ui_text(sapi, "Metaball Element", "Blobby element in a metaball data-block");
  api_def_struct_path_fn(sapi, "api_MetaElement_path");
  api_def_struct_ui_icon(sapi, ICON_OUTLINER_DATA_META);

  /* enums */
  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_metaelem_type_items);
  api_def_prop_ui_text(prop, "Type", "Metaball types");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  /* number values */
  prop = api_def_prop(sapi, "co", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_float_stype(prop, NULL, "x");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Location", "");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  prop = api_def_prop(sapi, "rotation", PROP_FLOAT, PROP_QUATERNION);
  api_def_prop_float_stype(prop, NULL, "quat");
  api_def_prop_ui_text(prop, "Rotation", "Normalized quaternion rotation");
  api_def_prop_update(prop, 0, "api_MetaBall_update_rotation");

  prop = api_def_prop(sapi, "radius", PROP_FLOAT, PROP_UNSIGNED | PROP_UNIT_LENGTH);
  api_def_prop_float_stype(prop, NULL, "rad");
  api_def_prop_ui_text(prop, "Radius", "");
  api_def_prop_range(prop, 0.0f, FLT_MAX);
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  prop = api_def_prop(sapi, "size_x", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "expx")(
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_range(prop, 0.0f, 20.0f);
  api_def_prop_ui_text(
      prop, "Size X", "Size of element, use of components depends on element type");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  prop = api_def_prop(sapi, "size_y", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "expy");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_range(prop, 0.0f, 20.0f);
  api_def_prop_ui_text(
      prop, "Size Y", "Size of element, use of components depends on element type");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  prop = api_def_prop(sapi, "size_z", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "expz");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_range(prop, 0.0f, 20.0f);
  api_def_prop_ui_text(
      prop, "Size Z", "Size of element, use of components depends on element type");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  prop = api_def_prop(sapi, "stiffness", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "s");
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_text(prop, "Stiffness", "Stiffness defines how much of the element to fill");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  /* flags */
  prop = api_def_prop(sapi, "use_negative", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", MB_NEGATIVE);
  api_def_prop_ui_text(prop, "Negative", "Set metaball as negative one");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  prop = api_def_prop(sapi, "use_scale_stiffness", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_sdna(prop, NULL, "flag", MB_SCALE_RAD);
  api_def_prop_ui_text(prop, "Scale Stiffness", "Scale stiffness instead of radius");
  api_def_prop_update(prop, 0, "api_MetaBall_redraw_data");

  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", 1); /* SELECT */
  api_def_prop_ui_text(prop, "Select", "Select element");
  api_def_prop_update(prop, 0, "api_MetaBall_redraw_data");

  prop = api_def_prop(sapi, "hide", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", MB_HIDE);
  api_def_prop_ui_text(prop, "Hide", "Hide element");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");
}

/* mball.elements */
static void api_def_metaball_elements(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  api_def_prop_sapi(cprop, "MetaBallElements");
  sapi = api_def_struct(brna, "MetaBallElements", NULL);
  api_def_struct_stype(srna, "MetaBall");
  api_def_struct_ui_text(srna, "Metaball Elements", "Collection of metaball elements");

  fn = api_def_fn(sapi, "new", "api_MetaBall_elements_new");
  api_def_fn_ui_description(fn, "Add a new element to the metaball");
  api_def_enum(fn,
               "type",
               api_enum_metaelem_type_items,
               MB_BALL,
               "",
               "Type for the new metaball element");
  parm = api_def_ptr(fn, "element", "MetaElement", "", "The newly created metaball element");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_MetaBall_elements_remove");
  api_def_fn_ui_description(fn, "Remove an element from the metaball");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "element", "MetaElement", "", "The element to remove");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  fn = api_def_fn(sapi, "clear", "rna_MetaBall_elements_clear");
  api_def_fn_ui_description(fn, "Remove all elements from the metaball");

  prop = api_def_prop(sapi, "active", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "lastelem");
  api_def_prop_ui_text(prop, "Active Element", "Last selected element");
}

static void api_def_metaball(Dune *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;
  static const EnumPropItem prop_update_items[] = {
      {MB_UPDATE_ALWAYS, "UPDATE_ALWAYS", 0, "Always", "While editing, update metaball always"},
      {MB_UPDATE_HALFRES,
       "HALFRES",
       0,
       "Half",
       "While editing, update metaball in half resolution"},
      {MB_UPDATE_FAST, "FAST", 0, "Fast", "While editing, update metaball without polygonization"},
      {MB_UPDATE_NEVER, "NEVER", 0, "Never", "While editing, don't update metaball at all"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "MetaBall", "ID");
  api_def_struct_ui_text(dapi, "MetaBall", "Metaball data-block to defined blobby surfaces");
  api_def_struct_ui_icon(sapi, ICON_META_DATA);

  prop = api_def_prop(sapi, "elements", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "elems", NULL);
  api_def_prop_struct_type(prop, "MetaElement");
  api_def_prop_ui_text(prop, "Elements", "Metaball elements");
  api_def_metaball_elements(dapi, prop);

  /* enums */
  prop = api_def_prop(sapi, "update_method", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "flag");
  api_def_prop_enum_items(prop, prop_update_items);
  api_def_prop_ui_text(prop, "Update", "Metaball edit update behavior");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  /* number values */
  prop = api_def_prop(sapi, "resolution", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "wiresize");
  api_def_prop_range(prop, 0.005f, 10000.0f);
  api_def_prop_ui_range(prop, 0.05f, 1000.0f, 2.5f, 3);
  api_def_prop_ui_text(prop, "Wire Size", "Polygonization resolution in the 3D viewport");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  prop = api_def_prop(sapi, "render_resolution", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "rendersize");
  api_def_prop_range(prop, 0.005f, 10000.0f);
  api_def_prop_ui_range(prop, 0.025f, 1000.0f, 2.5f, 3);
  api_def_prop_ui_text(prop, "Render Size", "Polygonization resolution in rendering");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  prop = api_def_prop(sapi, "threshold", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "thresh");
  api_def_prop_range(prop, 0.0f, 5.0f);
  api_def_prop_ui_text(prop, "Threshold", "Influence of metaball elements");
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  /* texture space */
  prop = api_def_prop(sapi, "use_auto_texspace", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "texflag", MB_AUTOSPACE);
  spi_def_prop_ui_text(
      prop,
      "Auto Texture Space",
      "Adjust active object's texture space automatically when transforming object");

  prop = api_def_prop(sapi, "texspace_location", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Texture Space Location", "Texture space location");
  api_def_prop_editable_fn(prop, "api_Meta_texspace_editable");
  api_def_prop_float_fns(
      prop, "api_Meta_texspace_loc_get", "api_Meta_texspace_loc_set", NULL);
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  prop = api_def_prop(sapi, "texspace_size", PROP_FLOAT, PROP_XYZ);
  api_def_prop_array(prop, 3);
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_ui_text(prop, "Texture Space Size", "Texture space size");
  api_def_prop_editable_fn(prop, "api_Meta_texspace_editable");
  api_def_prop_float_fns(
      prop, "api_Meta_texspace_size_get", "api_Meta_texspace_size_set", NULL);
  api_def_prop_update(prop, 0, "api_MetaBall_update_data");

  /* not supported yet */
#  if 0
  prop = api_def_prop(sapi, "texspace_rot", PROP_FLOAT, PROP_EULER);
  api_def_prop_float(prop, NULL, "rot");
  api_def_prop_ui_text(prop, "Texture Space Rotation", "Texture space rotation");
  api_def_prop_editable_fn(prop, "rna_Meta_texspace_editable");
  api_def_prop_update(prop, 0, "rna_MetaBall_update_data");
#  endif

  /* materials */
  prop = api_def_prop(sapi, "materials", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "mat", "totcol");
  api_def_prop_struct_type(prop, "Material");
  api_def_prop_ui_text(prop, "Materials", "");
  api_def_prop_srna(prop, "IdMaterials"); /* see api_id.c */
  api_def_prop_collection_fns(
      prop, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "api_IdMaterials_assign_int");

  prop = api_def_prop(sapi, "is_editmode", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_Meta_is_editmode_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Is Editmode", "True when used in editmode");

  /* anim */
  api_def_animdata_common(sapi);

  api_api_meta(sapi);
}

void api_def_meta(DuneApi *dapi)
{
  api_def_metaelement(brna);
  api_def_metaball(brna);
}

#endif
