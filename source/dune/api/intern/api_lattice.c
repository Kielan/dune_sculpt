#include <stdlib.h>

#include "types_curve.h"
#include "types_key.h"
#include "types_lattice.h"
#include "types_meshdata.h"
#include "types_object.h"

#include "lib_utildefines.h"

#include "api_define.h"
#include "api_enum_types.h"
#include "api_internal.h"

#ifdef API_RUNTIME

#  include "types_object.h"
#  include "types_scene.h"

#  include "dune_deform.h"
#  include "dune_lattice.h"
#  include "dune_main.h"
#  include "lib_string.h"

#  include "graph.h"

#  include "ed_lattice.h"
#  include "wm_api.h"
#  include "wm_types.h"

static void api_LatticePoint_co_get(ApiPtr *ptr, float *values)
{
  Lattice *lt = (Lattice *)ptr->owner_id;
  Point *bp = (Point *)ptr->data;
  int index = bp - lt->def;
  int u, v, w;

  dune_lattice_index_to_uvw(lt, index, &u, &v, &w);

  values[0] = lt->fu + u * lt->du;
  values[1] = lt->fv + v * lt->dv;
  values[2] = lt->fw + w * lt->dw;
}

static void api_LatticePoint_groups_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Lattice *lt = (Lattice *)ptr->owner_id;

  if (lt->dvert) {
    Point *bp = (Point *)ptr->data;
    MeshDeformVert *dvert = lt->dvert + (bp - lt->def);

    api_iter_array_begin(
        iter, (void *)dvert->dw, sizeof(MeshDeformWeight), dvert->totweight, 0, NULL);
  }
  else {
    api_iter_array_begin(iter, NULL, 0, 0, 0, NULL);
  }
}

static void api_Lattice_points_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Lattice *lt = (Lattice *)ptr->data;
  int tot = lt->pntsu * lt->pntsv * lt->pntsw;

  if (lt->editlatt && lt->editlatt->latt->def) {
    api_iter_array_begin(iter, (void *)lt->editlatt->latt->def, sizeof(BPoint), tot, 0, NULL);
  }
  else if (lt->def) {
    api_iter_array_begin(iter, (void *)lt->def, sizeof(Point), tot, 0, NULL);
  }
  else {
    api_iter_array_begin(iter, NULL, 0, 0, 0, NULL);
  }
}

static void api_Lattice_update_data(Main *UNUSED(main), Scene *UNUSED(scene), PointerRNA *ptr)
{
  Id *id = ptr->owner_id;

  graph_id_tag_update(id, 0);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);
}

/* copy settings to editlattice,
 * we could split this up differently (one update call per property)
 * but for now that's overkill */
static void api_Lattice_update_data_editlatt(Main *UNUSED(main),
                                             Scene *UNUSED(scene),
                                             ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  Lattice *lt = (Lattice *)ptr->owner_id;

  if (lt->editlatt) {
    Lattice *lt_em = lt->editlatt->latt;
    lt_em->typeu = lt->typeu;
    lt_em->typev = lt->typev;
    lt_em->typew = lt->typew;
    lt_em->flag = lt->flag;
    lib_strncpy(lt_em->vgroup, lt->vgroup, sizeof(lt_em->vgroup));
  }

  graph_id_tag_update(id, 0);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);
}

static void api_Lattice_update_size(Main *main, Scene *scene, ApiPtr *ptr)
{
  Lattice *lt = (Lattice *)ptr->owner_id;
  Object *ob;
  int newu, newv, neww;

  /* We don't modify the actual `pnts`, but go through `opnts` instead. */
  newu = (lt->opntsu > 0) ? lt->opntsu : lt->pntsu;
  newv = (lt->opntsv > 0) ? lt->opntsv : lt->pntsv;
  neww = (lt->opntsw > 0) ? lt->opntsw : lt->pntsw;

  /* dune_lattice_resize needs an object, any object will have the same result */
  for (ob = main->objects.first; ob; ob = ob->id.next) {
    if (ob->data == lt) {
      dune_lattice_resize(lt, newu, newv, neww, ob);
      if (lt->editlatt) {
        dune_lattice_resize(lt->editlatt->latt, newu, newv, neww, ob);
      }
      break;
    }
  }

  /* otherwise without, means old points are not repositioned */
  if (!ob) {
    dune_lattice_resize(lt, newu, newv, neww, NULL);
    if (lt->editlatt) {
      dune_lattice_resize(lt->editlatt->latt, newu, newv, neww, NULL);
    }
  }

  api_Lattice_update_data(main, scene, ptr);
}

static void api_Lattice_use_outside_set(PointerRNA *ptr, bool value)
{
  Lattice *lt = ptr->data;

  if (value) {
    lt->flag |= LT_OUTSIDE;
  }
  else {
    lt->flag &= ~LT_OUTSIDE;
  }

  outside_lattice(lt);

  if (lt->editlatt) {
    if (value) {
      lt->editlatt->latt->flag |= LT_OUTSIDE;
    }
    else {
      lt->editlatt->latt->flag &= ~LT_OUTSIDE;
    }

    outside_lattice(lt->editlatt->latt);
  }
}

static int api_Lattice_size_editable(PointerRNA *ptr, const char **UNUSED(r_info))
{
  Lattice *lt = (Lattice *)ptr->data;

  return (lt->key == NULL) ? PROP_EDITABLE : 0;
}

static void api_Lattice_points_u_set(PointerRNA *ptr, int value)
{
  Lattice *lt = (Lattice *)ptr->data;

  lt->opntsu = CLAMPIS(value, 1, 64);
}

static void api_Lattice_points_v_set(ApiPtr *ptr, int value)
{
  Lattice *lt = (Lattice *)ptr->data;

  lt->opntsv = CLAMPIS(value, 1, 64);
}

static void api_Lattice_points_w_set(ApiPtr *ptr, int value)
{
  Lattice *lt = (Lattice *)ptr->data;

  lt->opntsw = CLAMPIS(value, 1, 64);
}

static void api_Lattice_vg_name_set(ApiPtr *ptr, const char *value)
{
  Lattice *lt = ptr->data;
  lib_strncpy(lt->vgroup, value, sizeof(lt->vgroup));

  if (lt->editlatt) {
    lib_strncpy(lt->editlatt->latt->vgroup, value, sizeof(lt->editlatt->latt->vgroup));
  }
}

/* annoying, but is a consequence of api structures... */
static char *api_LatticePoint_path(ApiPtr *ptr)
{
  Lattice *lt = (Lattice *)ptr->owner_id;
  void *point = ptr->data;
  Point *points = NULL;

  if (lt->editlatt && lt->editlatt->latt->def) {
    points = lt->editlatt->latt->def;
  }
  else {
    points = lt->def;
  }

  if (points && point) {
    int tot = lt->pntsu * lt->pntsv * lt->pntsw;

    /* only return index if in range */
    if ((point >= (void *)points) && (point < (void *)(points + tot))) {
      int pt_index = (int)((Point *)point - points);

      return lib_sprintfn("points[%d]", pt_index);
    }
  }

  return lib_strdup("");
}

static bool api_Lattice_is_editmode_get(ApiPtr *ptr)
{
  Lattice *lt = (Lattice *)ptr->owner_id;
  return (lt->editlatt != NULL);
}

#else

static void api_def_latticepoint(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  srna = api_def_struct(dapi, "LatticePoint", NULL);
  api_def_struct_stype(sapi, "Point");
  api_def_struct_ui_text(sapi, "LatticePoint", "Point in the lattice grid");
  api_def_struct_path_fn(sapi, "api_LatticePoint_path");

  prop = api_def_prop(sapi, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "f1", SELECT);
  api_def_prop_ui_text(prop, "Point selected", "Selection status");

  prop = api_def_prop(srna, "co", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 3);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_float_fns(prop, "api_LatticePoint_co_get", NULL, NULL);
  api_def_prop_ui_text(
      prop,
      "Location",
      "Original undeformed location used to calculate the strength of the deform effect "
      "(edit/animate the Deformed Location instead)");

  prop = api_def_prop(sapi, "co_deform", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_float_stype(prop, NULL, "vec");
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Deformed Location", "");
  api_def_prop_update(prop, 0, "api_Lattice_update_data");

  prop = api_def_prop(sapi, "weight_softbody", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_sapi(prop, NULL, "weight");
  api_def_prop_range(prop, 0.01f, 100.0f);
  api_def_prop_ui_text(prop, "Weight", "Softbody goal weight");
  api_def_prop_update(prop, 0, "api_Lattice_update_data");

  prop = api_def_prop(sapi, "groups", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_fns(prop,
                              "api_LatticePoint_groups_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_struct_type(prop, "VertexGroupElement");
  api_def_prop_ui_text(
      prop, "Groups", "Weights for the vertex groups this point is member of");
}

static void api_def_lattice(DuneApi *dapi)
{
  ApiStruct sapi;
  ApiProp prop;

  sapi = api_def_struct(dapi, "Lattice", "Id");
  api_def_struct_ui_text(
      srna, "Lattice", "Lattice data-block defining a grid for deforming other objects");
  api_def_struct_ui_icon(sapi, ICON_LATTICE_DATA);

  prop = api_def_prop(sapi, "points_u", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pntsu");
  api_def_prop_int_fns(prop, NULL, "api_Lattice_points_u_set", NULL);
  api_def_prop_range(prop, 1, 64);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "U", "Point in U direction (can't be changed when there are shape keys)");
  api_def_prop_update(prop, 0, "api_Lattice_update_size");
  api_def_prop_editable_fn(prop, "api_Lattice_size_editable");

  prop = api_def_prop(sapi, "points_v", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pntsv");
  api_def_prop_int_fns(prop, NULL, "api_Lattice_points_v_set", NULL);
  api_def_prop_range(prop, 1, 64);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "V", "Point in V direction (can't be changed when there are shape keys)");
  api_def_prop_update(prop, 0, "api_Lattice_update_size");
  api_def_prop_editable_fn(prop, "api_Lattice_size_editable");

  prop = api_def_prop(sapi, "points_w", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "pntsw");
  api_def_prop_int_fns(prop, NULL, "rna_Lattice_points_w_set", NULL);
  api_def_prop_range(prop, 1, 64);
  apo_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_ui_text(
      prop, "W", "Point in W direction (can't be changed when there are shape keys)");
  api_def_prop_update(prop, 0, "api_Lattice_update_size");
  api_def_prop_editable_fn(prop, "api_Lattice_size_editable");

  prop = api_def_prop(sapi, "interpolation_type_u", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "typeu");
  api_def_prop_enum_items(prop, rna_enum_keyblock_type_items);
  api_def_prop_ui_text(prop, "Interpolation Type U", "");
  api_def_prop_update(prop, 0, "rna_Lattice_update_data_editlatt");

  prop = api_def_prop(sapi, "interpolation_type_v", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "typev");
  api_def_prop_enum_items(prop, rna_enum_keyblock_type_items);
  api_def_prop_ui_text(prop, "Interpolation Type V", "");
  api_def_prop_update(prop, 0, "rna_Lattice_update_data_editlatt");

  prop = api_def_prop(sapi, "interpolation_type_w", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "typew");
  api_def_prop_enum_items(prop, api_enum_keyblock_type_items);
  api_def_prop_ui_text(prop, "Interpolation Type W", "");
  api_def_prop_update(prop, 0, "api_Lattice_update_data_editlatt");

  prop = api_def_prop(sapi, "use_outside", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", LT_OUTSIDE);
  api_def_prop_bool_fns(prop, NULL, "api_Lattice_use_outside_set");
  api_def_prop_ui_text(
      prop, "Outside", "Only display and take into account the outer vertices");
  api_def_prop_update(prop, 0, "api_Lattice_update_data_editlatt");

  prop = api_def_prop(sapi, "vertex_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "vgroup");
  api_def_prop_ui_text(
      prop, "Vertex Group", "Vertex group to apply the influence of the lattice");
  api_def_prop_string_fns(prop, NULL, NULL, "api_Lattice_vg_name_set");
  api_def_prop_update(prop, 0, "api_Lattice_update_data_editlatt");

  prop = api_def_prop(sapi, "shape_keys", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "key");
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_clear_flag(prop, PROP_PTR_NO_OWNERSHIP);
  api_def_prop_ui_text(prop, "Shape Keys", "");

  prop = api_def_prop(sapi, "points", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "LatticePoint");
  RNA_def_property_collection_fns(prop,
                                  "api_Lattice_points_begin",
                                  "api_iter_array_next",
                                  "api_iter_array_end",
                                  "api_iter_array_get",
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL);
  api_def_prop_ui_text(prop, "Points", "Points of the lattice");

  prop = api_def_prop(sapi, "is_editmode", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "rna_Lattice_is_editmode_get", NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Is Editmode", "True when used in editmode");

  /* pointers */
  api_def_animdata_common(sapi);

  api_api_lattice(sapi);
}

void api_def_lattice(DuneApi *dapi)
{
  api_def_lattice(dapi);
  api_def_latticepoint(dapi);
}

#endif
