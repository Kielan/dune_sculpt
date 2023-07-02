/* NOTE: The original vertex color stuff is now just used for
 * getting info on the layers themselves, accessing the data is
 * done through the (not yet written) mpoly interfaces. */

#include <stdlib.h>

#include "mem_guardedalloc.h"

#include "types_material.h"
#include "types_mesh.h"
#include "types_meshdata.h"
#include "types_object.h"

#include "lib_math_base.h"
#include "lib_math_rotation.h"
#include "lib_utildefines.h"

#include "dune_editmesh.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"
#include "api_types.h"

#include "api_internal.h"

#include "wm_types.h"

const EnumPropItem api_enum_mesh_delimit_mode_items[] = {
    {BMO_DELIM_NORMAL, "NORMAL", 0, "Normal", "Delimit by face directions"},
    {BMO_DELIM_MATERIAL, "MATERIAL", 0, "Material", "Delimit by face material"},
    {BMO_DELIM_SEAM, "SEAM", 0, "Seam", "Delimit by edge seams"},
    {BMO_DELIM_SHARP, "SHARP", 0, "Sharp", "Delimit by sharp edges"},
    {BMO_DELIM_UV, "UV", 0, "UVs", "Delimit by UV coordinates"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem api_enum_mesh_remesh_mode_items[] = {
    {REMESH_VOXEL, "VOXEL", 0, "Voxel", "Use the voxel remesher"},
    {REMESH_QUAD, "QUAD", 0, "Quad", "Use the quad remesher"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "types_scene.h"

#  include "lib_math.h"

#  include "dune_customdata.h"
#  include "dune_main.h"
#  include "dune_mesh.h"
#  include "dune_mesh_runtime.h"
#  include "dune_report.h"

#  include "graph.h"

#  include "ed_mesh.h" /* XXX Bad level call */

#  include "wm_api.h"

#  include "api_mesh_utils.h"

/* -------------------------------------------------------------------- */
/** Generic Helpers */
static Mesh *api_mesh(ApiPtr *ptr)
{
  Mesh *me = (Mesh *)ptr->owner_id;
  return me;
}

static CustomData *api_mesh_vdata_helper(Mesh *me)
{
  return (me->edit_mesh) ? &me->edit_mesh->bm->vdata : &me->vdata;
}

static CustomData *api_mesh_edata_helper(Mesh *me)
{
  return (me->edit_mesh) ? &me->edit_mesh->bm->edata : &me->edata;
}

static CustomData *api_mesh_pdata_helper(Mesh *me)
{
  return (me->edit_mesh) ? &me->edit_mesh->bm->pdata : &me->pdata;
}

static CustomData *api_mesh_ldata_helper(Mesh *me)
{
  return (me->edit_mesh) ? &me->edit_mesh->bm->ldata : &me->ldata;
}

static CustomData *api_mesh_fdata_helper(Mesh *me)
{
  return (me->edit_mesh) ? NULL : &me->fdata;
}

static CustomData *api_mesh_vdata(ApiPtr *ptr)
{
  Mesh *me = rna_mesh(ptr);
  return api_mesh_vdata_helper(me);
}
#  if 0
static CustomData *api_mesh_edata(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return api_mesh_edata_helper(me);
}
#  endif
static CustomData *api_mesh_pdata(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return api_mesh_pdata_helper(me);
}

static CustomData *api_mesh_ldata(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return api_mesh_ldata_helper(me);
}

/* -------------------------------------------------------------------- */
/** Generic CustomData Layer Functions **/
static void api_cd_layer_name_set(CustomData *cdata, CustomDataLayer *cdl, const char *value)
{
  lib_strncpy_utf8(cdl->name, value, sizeof(cdl->name));
  CustomData_set_layer_unique_name(cdata, cdl - cdata->layers);
}

/* avoid using where possible!, ideally the type is known */
static CustomData *api_cd_from_layer(ApiPtr *ptr, CustomDataLayer *cdl)
{
  /* find out where we come from by */
  Mesh *me = (Mesh *)ptr->owner_id;
  CustomData *cd;

  /* rely on negative values wrapping */
#  define TEST_CDL(cmd) \
    if ((void)(cd = cmd(me)), ARRAY_HAS_ITEM(cdl, cd->layers, cd->totlayer)) { \
      return cd; \
    } \
    ((void)0)

  TEST_CDL(api_mesh_vdata_helper);
  TEST_CDL(api_mesh_edata_helper);
  TEST_CDL(api_mesh_pdata_helper);
  TEST_CDL(api_mesh_ldata_helper);
  TEST_CDL(api_mesh_fdata_helper);

#  undef TEST_CDL

  /* should _never_ happen */
  return NULL;
}

static void api_MeshVertexLayer_name_set(ApiPtr *ptr, const char *value)
{
  api_cd_layer_name_set(api_mesh_vdata(ptr), (CustomDataLayer *)ptr->data, value);
}
#  if 0
static void api_MeshEdgeLayer_name_set(ApiPtr *ptr, const char *value)
{
  api_cd_layer_name_set(rna_mesh_edata(ptr), (CustomDataLayer *)ptr->data, value);
}
#  endif
static void api_MeshPolyLayer_name_set(ApiPtr *ptr, const char *value)
{
  api_cd_layer_name_set(api_mesh_pdata(ptr), (CustomDataLayer *)ptr->data, value);
}
static void api_MeshLoopLayer_name_set(ApiPtr *ptr, const char *value)
{
  api_cd_layer_name_set(api_mesh_ldata(ptr), (CustomDataLayer *)ptr->data, value);
}
/* only for layers shared between types */
static void api_MeshAnyLayer_name_set(ApiPtr *ptr, const char *value)
{
  CustomData *cd = api_cd_from_layer(ptr, (CustomDataLayer *)ptr->data);
  api_cd_layer_name_set(cd, (CustomDataLayer *)ptr->data, value);
}

static bool api_Mesh_has_custom_normals_get(ApiPtr *ptr)
{
  Mesh *me = ptr->data;
  return dune_mesh_has_custom_loop_normals(me);
}

/* -------------------------------------------------------------------- */
/** Update Callbacks
 *
 * note Skipping meshes without users is a simple way to avoid updates on newly created meshes.
 * This speeds up importers that manipulate mesh data before linking it to an object & collection. **/

/**
 * warning This calls graph_id_tag_update(id, 0) which is something that should be phased out
 * see graph_node_tag_zero, for now it's kept since changes to updates must be carefully
 * tested to make sure there aren't any regressions.
 *
 * This function should be replaced with more specific update flags where possible.
 */
static void api_Mesh_update_data_legacy_deg_tag_all(Main *UNUSED(bmain),
                                                    Scene *UNUSED(scene),
                                                    PointerRNA *ptr)
{
  Id *id = ptr->owner_id;
  if (id->us <= 0) { /* See note in section heading. */
    return;
  }

  graph_id_tag_update(id, 0);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);
}

static void api_Mesh_update_geom_and_params(Main *UNUSED(main),
                                            Scene *UNUSED(scene),
                                            ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  if (id->us <= 0) { /* See note in section heading. */
    return;
  }

  graph_id_tag_update(id, ID_RECALC_GEOMETRY | ID_RECALC_PARAMETERS);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);
}

static void api_Mesh_update_data_edit_weight(Main *main, Scene *scene, ApiPtr *ptr)
{
  dune_mesh_batch_cache_dirty_tag(api_mesh(ptr), DUNE_MESH_BATCH_DIRTY_ALL);

  api_Mesh_update_data_legacy_graph_tag_all(main, scene, ptr);
}

static void api_Mesh_update_data_edit_active_color(Main *main, Scene *scene, ApiPtr *ptr)
{
  dune_mesh_batch_cache_dirty_tag(api_mesh(ptr), DUNE_MESH_BATCH_DIRTY_ALL);

  api_Mesh_update_data_legacy_deg_tag_all(main, scene, ptr);
}
static void api_Mesh_update_select(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  if (id->us <= 0) { /* See note in section heading. */
    return;
  }

  wm_main_add_notifier(NC_GEOM | ND_SELECT, id);
}

void api_Mesh_update_draw(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  if (id->us <= 0) { /* See note in section heading. */
    return;
  }

  wm_main_add_notifier(NC_GEOM | ND_DATA, id);
}

static void api_Mesh_update_vertmask(Main *main, Scene *scene, ApiPtr *ptr)
{
  Mesh *me = ptr->data;
  if ((me->editflag & ME_EDIT_PAINT_VERT_SEL) && (me->editflag & ME_EDIT_PAINT_FACE_SEL)) {
    me->editflag &= ~ME_EDIT_PAINT_FACE_SEL;
  }

  dune_mesh_batch_cache_dirty_tag(me, DUNE_MESH_BATCH_DIRTY_ALL);

  api_Mesh_update_draw(main, scene, ptr);
}

static void api_Mesh_update_facemask(Main *main, Scene *scene, PointerRNA *ptr)
{
  Mesh *me = ptr->data;
  if ((me->editflag & ME_EDIT_PAINT_VERT_SEL) && (me->editflag & ME_EDIT_PAINT_FACE_SEL)) {
    me->editflag &= ~ME_EDIT_PAINT_VERT_SEL;
  }

  dune_mesh_batch_cache_dirty_tag(me, DUNE_MESH_BATCH_DIRTY_ALL);

  api_Mesh_update_draw(main, scene, ptr);
}

/* -------------------------------------------------------------------- */
/** Property get/set Callbacks **/

static void api_MeshVertex_normal_get(ApiPtr *ptr, float *value)
{
  Mesh *mesh = api_mesh(ptr);
  const float(*vert_normals)[3] = dune_mesh_vertex_normals_ensure(mesh);

  const int index = (MeshVert *)ptr->data - mesh->mvert;
  lib_assert(index >= 0);
  lib_assert(index < mesh->totvert);

  copy_v3_v3(value, vert_normals[index]);
}

static void api_MeshVertex_normal_set(ApiPtr *ptr, const float *value)
{
  Mesh *mesh = api_mesh(ptr);
  float(*vert_normals)[3] = dune_mesh_vertex_normals_for_write(mesh);

  const int index = (MeshVert *)ptr->data - mesh->mvert;
  lib_assert(index >= 0);
  lib_assert(index < mesh->totvert);

  copy_v3_v3(vert_normals[index], value);
}

static float api_MeshVertex_bevel_weight_get(PointerRNA *ptr)
{
  MeshVert *mvert = (MVert *)ptr->data;
  return mvert->bweight / 255.0f;
}

static void api_MeshVertex_bevel_weight_set(ApiPtr *ptr, float value)
{
  MeshVert *mvert = (MeshVert *)ptr->data;
  mvert->bweight = round_fl_to_uchar_clamp(value * 255.0f);
}

static float api_MeshEdge_bevel_weight_get(ApiPtr *ptr)
{
  MeshEdge *medge = (MeshEdge *)ptr->data;
  return medge->bweight / 255.0f;
}

static void api_MeshEdge_bevel_weight_set(ApiPtr *ptr, float value)
{
  MeshEdge *medge = (MeshEdge *)ptr->data;
  medge->bweight = round_fl_to_uchar_clamp(value * 255.0f);
}

static float api_MeshEdge_crease_get(ApiPtr *ptr)
{
  MeshEdge *medge = (MeshEdge *)ptr->data;
  return medge->crease / 255.0f;
}

static void api_MeshEdge_crease_set(ApiPtr *ptr, float value)
{
  MeshEdge *medge = (MeshEdge *)ptr->data;
  medge->crease = round_fl_to_uchar_clamp(value * 255.0f);
}

static void api_MeshLoop_normal_get(ApiPtr *ptr, float *values)
{
  Mesh *me = api_mesh(ptr);
  MeshLoop *ml = (MeshLoop *)ptr->data;
  const float(*vec)[3] = CustomData_get(&me->ldata, (int)(ml - me->mloop), CD_NORMAL);

  if (!vec) {
    zero_v3(values);
  } else {
    copy_v3_v3(values, (const float *)vec);
  }
}

static void api_MeshLoop_normal_set(PointerRNA *ptr, const float *values)
{
  Mesh *me = api_mesh(ptr);
  MeshLoop *ml = (MLoop *)ptr->data;
  float(*vec)[3] = CustomData_get(&me->ldata, (int)(ml - me->mloop), CD_NORMAL);

  if (vec) {
    normalize_v3_v3(*vec, values);
  }
}

static void api_MeshLoop_tangent_get(PointerRNA *ptr, float *values)
{
  Mesh *me = api_mesh(ptr);
  MeshLoop *ml = (MLoop *)ptr->data;
  const float(*vec)[4] = CustomData_get(&me->ldata, (int)(ml - me->mloop), CD_MLOOPTANGENT);

  if (!vec) {
    zero_v3(values);
  }
  else {
    copy_v3_v3(values, (const float *)vec);
  }
}

static float api_MeshLoop_bitangent_sign_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshLoop *ml = (MeshLoop *)ptr->data;
  const float(*vec)[4] = CustomData_get(&me->ldata, (int)(ml - me->mloop), CD_MLOOPTANGENT);

  return (vec) ? (*vec)[3] : 0.0f;
}

static void api_MeshLoop_bitangent_get(ApiPtr *ptr, float *values)
{
  Mesh *me = api_mesh(ptr);
  MeshLoop *ml = (MeshLoop *)ptr->data;
  const float(*nor)[3] = CustomData_get(&me->ldata, (int)(ml - me->mloop), CD_NORMAL);
  const float(*vec)[4] = CustomData_get(&me->ldata, (int)(ml - me->mloop), CD_MLOOPTANGENT);

  if (nor && vec) {
    cross_v3_v3v3(values, (const float *)nor, (const float *)vec);
    mul_v3_fl(values, (*vec)[3]);
  }
  else {
    zero_v3(values);
  }
}

static void api_MeshPolygon_normal_get(ApiPtr *ptr, float *values)
{
  Mesh *me = api_mesh(ptr);
  MeshPoly *mp = (MeshPoly *)ptr->data;

  dune_mesh_calc_poly_normal(mp, me->mloop + mp->loopstart, me->mvert, values);
}

static void api_MeshPolygon_center_get(ApiPtr *ptr, float *values)
{
  Mesh *me = api_mesh(ptr);
  MeshPoly *mp = (MeshPoly *)ptr->data;

  dune_mesh_calc_poly_center(mp, me->mloop + mp->loopstart, me->mvert, values);
}

static float api_MeshPolygon_area_get(ApiPtr *ptr)
{
  Mesh *me = (Mesh *)ptr->owner_id;
  MeshPoly *mp = (MPoly *)ptr->data;

  return dune_mesh_calc_poly_area(mp, me->mloop + mp->loopstart, me->mvert);
}

static void api_MeshPolygon_flip(Id *id, MPoly *mp)
{
  Mesh *me = (Mesh *)id;

  dune_mesh_polygon_flip(mp, me->mloop, &me->ldata);
  dune_mesh_tessface_clear(me);
  dune_mesh_runtime_clear_geometry(me);
  dune_mesh_normals_tag_dirty(me);
}

static void api_MeshLoopTriangle_verts_get(ApiPtr *ptr, int *values)
{
  Mesh *me = api_mesh(ptr);
  MeshLoopTri *lt = (MeshLoopTri *)ptr->data;
  values[0] = me->mloop[lt->tri[0]].v;
  values[1] = me->mloop[lt->tri[1]].v;
  values[2] = me->mloop[lt->tri[2]].v;
}

static void api_MeshLoopTriangle_normal_get(ApiPtr *ptr, float *values)
{
  Mesh *me = api_mesh(ptr);
  MeshLoopTri *lt = (MeshLoopTri *)ptr->data;
  unsigned int v1 = me->mloop[lt->tri[0]].v;
  unsigned int v2 = me->mloop[lt->tri[1]].v;
  unsigned int v3 = me->mloop[lt->tri[2]].v;

  normal_tri_v3(values, me->mvert[v1].co, me->mvert[v2].co, me->mvert[v3].co);
}

static void api_MeshLoopTriangle_split_normals_get(ApiPtr *ptr, float *values)
{
  Mesh *me = api_mesh(ptr);
  const float(*lnors)[3] = CustomData_get_layer(&me->ldata, CD_NORMAL);

  if (!lnors) {
    zero_v3(values + 0);
    zero_v3(values + 3);
    zero_v3(values + 6);
  } else {
    MeshLoopTri *lt = (MeshLoopTri *)ptr->data;
    copy_v3_v3(values + 0, lnors[lt->tri[0]]);
    copy_v3_v3(values + 3, lnors[lt->tri[1]]);
    copy_v3_v3(values + 6, lnors[lt->tri[2]]);
  }
}

static float api_MeshLoopTriangle_area_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshLoopTri *lt = (MeshLoopTri *)ptr->data;
  unsigned int v1 = me->mloop[lt->tri[0]].v;
  unsigned int v2 = me->mloop[lt->tri[1]].v;
  unsigned int v3 = me->mloop[lt->tri[2]].v;

  return area_tri_v3(me->mvert[v1].co, me->mvert[v2].co, me->mvert[v3].co);
}

static void api_MeshLoopColor_color_get(ApiPtr *ptr, float *values)
{
  MeshLoopCol *mlcol = (MeshLoopCol *)ptr->data;

  values[0] = mlcol->r / 255.0f;
  values[1] = mlcol->g / 255.0f;
  values[2] = mlcol->b / 255.0f;
  values[3] = mlcol->a / 255.0f;
}

static void api_MeshLoopColor_color_set(ApiPtr *ptr, const float *values)
{
  MeshLoopCol *mlcol = (MeshLoopCol *)ptr->data;

  mlcol->r = round_fl_to_uchar_clamp(values[0] * 255.0f);
  mlcol->g = round_fl_to_uchar_clamp(values[1] * 255.0f);
  mlcol->b = round_fl_to_uchar_clamp(values[2] * 255.0f);
  mlcol->a = round_fl_to_uchar_clamp(values[3] * 255.0f);
}

static int api_Mesh_texspace_editable(ApiPtr *ptr, const char **UNUSED(r_info))
{
  Mesh *me = (Mesh *)ptr->data;
  return (me->texflag & ME_AUTOSPACE) ? 0 : PROP_EDITABLE;
}

static void api_Mesh_texspace_size_get(Apitr *ptr, float values[3])
{
  Mesh *me = (Mesh *)ptr->data;

  dune_mesh_texspace_ensure(me);

  copy_v3_v3(values, me->size);
}

static void api_Mesh_texspace_loc_get(PointerRNA *ptr, float values[3])
{
  Mesh *me = (Mesh *)ptr->data;

  dune_mesh_texspace_ensure(me);

  copy_v3_v3(values, me->loc);
}

static void api_MeshVertex_groups_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);

  if (me->dvert) {
    MeshVert *mvert = (MeshVert *)ptr->data;
    MeshDeformVert *dvert = me->dvert + (mvert - me->mvert);

    api_iter_array_begin(
        iter, (void *)dvert->dw, sizeof(MeshDeformWeight), dvert->totweight, 0, NULL);
  } else {
    api_iter_array_begin(iter, NULL, 0, 0, 0, NULL);
  }
}

static void api_MeshVertex_undeformed_co_get(Apitr *ptr, float values[3])
{
  Mesh *me = api_mesh(ptr);
  MeshVert *mvert = (MeshVert *)ptr->data;
  float(*orco)[3] = CustomData_get_layer(&me->vdata, CD_ORCO);

  if (orco) {
    /* orco is normalized to 0..1, we do inverse to match mvert->co */
    float loc[3], size[3];

    dune_mesh_texspace_get(me->texcomesh ? me->texcomesh : me, loc, size);
    madd_v3_v3v3v3(values, loc, orco[(mvert - me->mvert)], size);
  } else {
    copy_v3_v3(values, mvert->co);
  }
}

static int api_CustomDataLayer_active_get(PointerRNA *ptr, CustomData *data, int type, bool render)
{
  int n = ((CustomDataLayer *)ptr->data) - data->layers;

  if (render) {
    return (n == CustomData_get_render_layer_index(data, type));
  } else {
    return (n == CustomData_get_active_layer_index(data, type));
  }
}

static int api_CustomDataLayer_clone_get(ApiPtr *ptr, CustomData *data, int type)
{
  int n = ((CustomDataLayer *)ptr->data) - data->layers;

  return (n == CustomData_get_clone_layer_index(data, type));
}

static void api_CustomDataLayer_active_set(
    ApiPtr *ptr, CustomData *data, int value, int type, int render)
{
  Mesh *me = (Mesh *)ptr->owner_id;
  int n = (((CustomDataLayer *)ptr->data) - data->layers) - CustomData_get_layer_index(data, type);

  if (value == 0) {
    return;
  }

  if (render) {
    CustomData_set_layer_render(data, type, n);
  } else {
    CustomData_set_layer_active(data, type, n);
  }

  dune_mesh_update_customdata_pointers(me, true);
}

static void api_CustomDataLayer_clone_set(ApiPtr *ptr, CustomData *data, int value, int type)
{
  int n = ((CustomDataLayer *)ptr->data) - data->layers;

  if (value == 0) {
    return;
  }

  CustomData_set_layer_clone_index(data, type, n);
}

static bool api_MeshEdge_freestyle_edge_mark_get(PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshEdge *medge = (MeshEdge *)ptr->data;
  FreestyleEdge *fed = CustomData_get(&me->edata, (int)(medge - me->medge), CD_FREESTYLE_EDGE);

  return fed && (fed->flag & FREESTYLE_EDGE_MARK) != 0;
}

static void api_MeshEdge_freestyle_edge_mark_set(PointerRNA *ptr, bool value)
{
  Mesh *me = api_mesh(ptr);
  MeshEdge *medge = (MeshEdge *)ptr->data;
  FreestyleEdge *fed = CustomData_get(&me->edata, (int)(medge - me->medge), CD_FREESTYLE_EDGE);

  if (!fed) {
    fed = CustomData_add_layer(&me->edata, CD_FREESTYLE_EDGE, CD_CALLOC, NULL, me->totedge);
  }
  if (value) {
    fed->flag |= FREESTYLE_EDGE_MARK;
  } else {
    fed->flag &= ~FREESTYLE_EDGE_MARK;
  }
}

static bool api_MeshPoly_freestyle_face_mark_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshPoly *mpoly = (MeshPoly *)ptr->data;
  FreestyleFace *ffa = CustomData_get(&me->pdata, (int)(mpoly - me->mpoly), CD_FREESTYLE_FACE);

  return ffa && (ffa->flag & FREESTYLE_FACE_MARK) != 0;
}

static void api_MeshPoly_freestyle_face_mark_set(ApiPtr *ptr, int value)
{
  Mesh *me = api_mesh(ptr);
  MeshPoly *mpoly = (MeshPoly *)ptr->data;
  FreestyleFace *ffa = CustomData_get(&me->pdata, (int)(mpoly - me->mpoly), CD_FREESTYLE_FACE);

  if (!ffa) {
    ffa = CustomData_add_layer(&me->pdata, CD_FREESTYLE_FACE, CD_CALLOC, NULL, me->totpoly);
  }
  if (value) {
    ffa->flag |= FREESTYLE_FACE_MARK;
  } else {
    ffa->flag &= ~FREESTYLE_FACE_MARK;
  }
}

/* uv_layers */

DEFINE_CUSTOMDATA_LAYER_COLLECTION(uv_layer, ldata, CD_MLOOPUV)
DEFINE_CUSTOMDATA_LAYER_COLLECTION_ACTIVEITEM(uv_layer, ldata, CD_MLOOPUV, active, MeshUVLoopLayer)
DEFINE_CUSTOMDATA_LAYER_COLLECTION_ACTIVEITEM(uv_layer, ldata, CD_MLOOPUV, clone, MeshUVLoopLayer)
DEFINE_CUSTOMDATA_LAYER_COLLECTION_ACTIVEITEM(
    uv_layer, ldata, CD_MLOOPUV, stencil, MeshUVLoopLayer)
DEFINE_CUSTOMDATA_LAYER_COLLECTION_ACTIVEITEM(uv_layer, ldata, CD_MLOOPUV, render, MeshUVLoopLayer)

/* MeshUVLoopLayer */
static char *api_MeshUVLoopLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("uv_layers[\"%s\"]", name_esc);
}

static void api_MeshUVLoopLayer_data_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(
      iter, layer->data, sizeof(MLoopUV), (me->edit_mesh) ? 0 : me->totloop, 0, NULL);
}

static int api_MeshUVLoopLayer_data_length(PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);
  return (me->edit_mesh) ? 0 : me->totloop;
}

static bool api_MeshUVLoopLayer_active_render_get(PointerRNA *ptr)
{
  return api_CustomDataLayer_active_get(ptr, rna_mesh_ldata(ptr), CD_MLOOPUV, 1);
}

static bool api_MeshUVLoopLayer_active_get(PointerRNA *ptr)
{
  return api_CustomDataLayer_active_get(ptr, rna_mesh_ldata(ptr), CD_MLOOPUV, 0);
}

static bool api_MeshUVLoopLayer_clone_get(PointerRNA *ptr)
{
  return api_CustomDataLayer_clone_get(ptr, rna_mesh_ldata(ptr), CD_MLOOPUV);
}

static void api_MeshUVLoopLayer_active_render_set(PointerRNA *ptr, bool value)
{
  api_CustomDataLayer_active_set(ptr, rna_mesh_ldata(ptr), value, CD_MLOOPUV, 1);
}

static void api_MeshUVLoopLayer_active_set(PointerRNA *ptr, bool value)
{
  api_CustomDataLayer_active_set(ptr, rna_mesh_ldata(ptr), value, CD_MLOOPUV, 0);
}

static void api_MeshUVLoopLayer_clone_set(PointerRNA *ptr, bool value)
{
  api_CustomDataLayer_clone_set(ptr, rna_mesh_ldata(ptr), value, CD_MLOOPUV);
}

/* vertex_color_layers */
DEFINE_CUSTOMDATA_LAYER_COLLECTION(vertex_color, ldata, CD_MLOOPCOL)
DEFINE_CUSTOMDATA_LAYER_COLLECTION_ACTIVEITEM(
    vertex_color, ldata, CD_MLOOPCOL, active, MeshLoopColorLayer)

static void api_MeshLoopColorLayer_data_begin(CollectionPropIter iter, ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(
      iter, layer->data, sizeof(MLoopCol), (me->edit_mesh) ? 0 : me->totloop, 0, NULL);
}

static int api_MeshLoopColorLayer_data_length(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return (me->edit_mesh) ? 0 : me->totloop;
}

static bool api_MeshLoopColorLayer_active_render_get(ApiPtr *ptr)
{
  return api_CustomDataLayer_active_get(ptr, api_mesh_ldata(ptr), CD_MLOOPCOL, 1);
}

static bool api_MeshLoopColorLayer_active_get(ApiPtr *ptr)
{
  return api_CustomDataLayer_active_get(ptr, api_mesh_ldata(ptr), CD_MLOOPCOL, 0);
}

static void api_MeshLoopColorLayer_active_render_set(ApiPtr *ptr, bool value)
{
  api_CustomDataLayer_active_set(ptr, rna_mesh_ldata(ptr), value, CD_MLOOPCOL, 1);
}

static void api_MeshLoopColorLayer_active_set(ApiPtr *ptr, bool value)
{
  api_CustomDataLayer_active_set(ptr, api_mesh_ldata(ptr), value, CD_MLOOPCOL, 0);
}

/* sculpt_vertex_color_layers */
DEFINE_CUSTOMDATA_LAYER_COLLECTION(sculpt_vertex_color, vdata, CD_PROP_COLOR)
DEFINE_CUSTOMDATA_LAYER_COLLECTION_ACTIVEITEM(
    sculpt_vertex_color, vdata, CD_PROP_COLOR, active, MeshVertColorLayer)

static void api_MeshVertColorLayer_data_begin(CollectionPropIter *iter, PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(
      iter, layer->data, sizeof(MeshPropCol), (me->edit_mesh) ? 0 : me->totvert, 0, NULL);
}

static int api_MeshVertColorLayer_data_length(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return (me->edit_mesh) ? 0 : me->totvert;
}

static bool api_MeshVertColorLayer_active_render_get(ApiPtr *ptr)
{
  return api_CustomDataLayer_active_get(ptr, api_mesh_vdata(ptr), CD_PROP_COLOR, 1);
}

static bool api_MeshVertColorLayer_active_get(ApiPtr *ptr)
{
  return api_CustomDataLayer_active_get(ptr, rna_mesh_vdata(ptr), CD_PROP_COLOR, 0);
}

static void api_MeshVertColorLayer_active_render_set(ApiPtr *ptr, bool value)
{
  api_CustomDataLayer_active_set(ptr, api_mesh_vdata(ptr), value, CD_PROP_COLOR, 1);
}

static void api_MeshVertColorLayer_active_set(ApiPtr *ptr, bool value)
{
  api_CustomDataLayer_active_set(ptr, api_mesh_vdata(ptr), value, CD_PROP_COLOR, 0);
}

static int api_float_layer_check(CollectionPropIter *UNUSED(iter), void *data)
{
  CustomDataLayer *layer = (CustomDataLayer *)data;
  return (layer->type != CD_PROP_FLOAT);
}

static void api_Mesh_vertex_float_layers_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  CustomData *vdata = api_mesh_vdata(ptr);
  api_iter_array_begin(iter,
                       (void *)vdata->layers,
                       sizeof(CustomDataLayer),
                       vdata->totlayer,
                       0,
                       api_float_layer_check);
}
static void api_Mesh_polygon_float_layers_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  CustomData *pdata = api_mesh_pdata(ptr);
  api_iter_array_begin(iter,
                       (void *)pdata->layers,
                       sizeof(CustomDataLayer),
                       pdata->totlayer,
                       0,
                       api_float_layer_check);
}

static int api_Mesh_vertex_float_layers_length(PointerRNA *ptr)
{
  return CustomData_number_of_layers(rna_mesh_vdata(ptr), CD_PROP_FLOAT);
}
static int api_Mesh_polygon_float_layers_length(PointerRNA *ptr)
{
  return CustomData_number_of_layers(api_mesh_pdata(ptr), CD_PROP_FLOAT);
}

static int api_int_layer_check(CollectionPropIter *UNUSED(iter), void *data)
{
  CustomDataLayer *layer = (CustomDataLayer *)data;
  return (layer->type != CD_PROP_INT32);
}

static void api_Mesh_vertex_int_layers_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  CustomData *vdata = api_mesh_vdata(ptr);
  api_iter_array_begin(iter,
                       (void *)vdata->layers,
                       sizeof(CustomDataLayer),
                       vdata->totlayer,
                       0,
                       api_int_layer_check);
}
static void api_Mesh_polygon_int_layers_begin(CollectionPropIter *iter, PointerRNA *ptr)
{
  CustomData *pdata = api_mesh_pdata(ptr);
  api_iter_array_begin(iter,
                       (void *)pdata->layers,
                       sizeof(CustomDataLayer),
                       pdata->totlayer,
                       0,
                       api_int_layer_check);
}

static int api_Mesh_vertex_int_layers_length(ApiPtr *ptr)
{
  return CustomData_number_of_layers(api_mesh_vdata(ptr), CD_PROP_INT32);
}
static int api_Mesh_polygon_int_layers_length(ApiPtr *ptr)
{
  return CustomData_number_of_layers(api_mesh_pdata(ptr), CD_PROP_INT32);
}

static int api_string_layer_check(CollectionPropIter *UNUSED(iter), void *data)
{
  CustomDataLayer *layer = (CustomDataLayer *)data;
  return (layer->type != CD_PROP_STRING);
}

static void api_Mesh_vertex_string_layers_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  CustomData *vdata = api_mesh_vdata(ptr);
  api_iter_array_begin(iter,
                       (void *)vdata->layers,
                       sizeof(CustomDataLayer),
                       vdata->totlayer,
                       0,
                       api_string_layer_check);
}
static void api_Mesh_polygon_string_layers_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  CustomData *pdata = api_mesh_pdata(ptr);
  api_iter_array_begin(iter,
                       (void *)pdata->layers,
                       sizeof(CustomDataLayer),
                       pdata->totlayer,
                       0,
                       api_string_layer_check);
}

static int api_Mesh_vertex_string_layers_length(ApiPtr *ptr)
{
  return CustomData_number_of_layers(api_mesh_vdata(ptr), CD_PROP_STRING);
}
static int api_Mesh_polygon_string_layers_length(ApiPtr *ptr)
{
  return CustomData_number_of_layers(api_mesh_pdata(ptr), CD_PROP_STRING);
}

/* Skin vertices */
DEFINE_CUSTOMDATA_LAYER_COLLECTION(skin_vertice, vdata, CD_MVERT_SKIN)

static char *api_MeshSkinVertexLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("skin_vertices[\"%s\"]", name_esc);
}

static char *api_VertCustomData_data_path(ApiPtr *ptr, const char *collection, int type);
static char *api_MeshSkinVertex_path(ApiPtr *ptr)
{
  return api_VertCustomData_data_path(ptr, "skin_vertices", CD_MVERT_SKIN);
}

static void api_MeshSkinVertexLayer_data_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(iter, layer->data, sizeof(MeshVertSkin), me->totvert, 0, NULL);
}

static int api_MeshSkinVertexLayer_data_length(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->totvert;
}

/* End skin vertices */
/* Vertex creases */
DEFINE_CUSTOMDATA_LAYER_COLLECTION(vertex_crease, vdata, CD_CREASE)

static char *api_VertCustomData_data_path(PointerRNA *ptr, const char *collection, int type);
static char *api_MeshVertexCreaseLayer_path(PointerRNA *ptr)
{
  return api_VertCustomData_data_path(ptr, "vertex_creases", CD_CREASE);
}

static void api_MeshVertexCreaseLayer_data_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(iter, layer->data, sizeof(float), me->totvert, 0, NULL);
}

static int api_MeshVertexCreaseLayer_data_length(PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->totvert;
}

/* End vertex creases */

/* Paint mask */
DEFINE_CUSTOMDATA_LAYER_COLLECTION(vertex_paint_mask, vdata, CD_PAINT_MASK)

static char *api_MeshPaintMaskLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("vertex_paint_masks[\"%s\"]", name_esc);
}

static char *api_MeshPaintMask_path(ApiPtr *ptr)
{
  return api_VertCustomData_data_path(ptr, "vertex_paint_masks", CD_PAINT_MASK);
}

static void api_MeshPaintMaskLayer_data_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(
      iter, layer->data, sizeof(MeshFloatProp), (me->edit_mesh) ? 0 : me->totvert, 0, NULL);
}

static int api_MeshPaintMaskLayer_data_length(PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);
  return (me->edit_mesh) ? 0 : me->totvert;
}

/* End paint mask */
/* Face maps */
DEFINE_CUSTOMDATA_LAYER_COLLECTION(face_map, pdata, CD_FACEMAP)
DEFINE_CUSTOMDATA_LAYER_COLLECTION_ACTIVEITEM(
    face_map, pdata, CD_FACEMAP, active, MeshFaceMapLayer)

static char *api_MeshFaceMapLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("face_maps[\"%s\"]", name_esc);
}

static void api_MeshFaceMapLayer_data_begin(CollectionPropIter iter, ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(
      iter, layer->data, sizeof(int), (me->edit_mesh) ? 0 : me->totpoly, 0, NULL);
}

static int api_MeshFaceMapLayer_data_length(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return (me->edit_mesh) ? 0 : me->totpoly;
}

static ApiPtr api_Mesh_face_map_new(struct Mesh *me, ReportList *reports, const char *name)
{
  if (dune_mesh_ensure_facemap_customdata(me) == false) {
    dune_report(reports, RPT_ERROR, "Currently only single face map layers are supported");
    return ApiPtr_NULL;
  }

  CustomData *pdata = rna_mesh_pdata_helper(me);

  int index = CustomData_get_layer_index(pdata, CD_FACEMAP);
  lib_assert(index != -1);
  CustomDataLayer *cdl = &pdata->layers[index];
  api_cd_layer_name_set(pdata, cdl, name);

  ApiPtr ptr;
  api_ptr_create(&me->id, &Api_MeshFaceMapLayer, cdl, &ptr);
  return ptr;
}

static void api_Mesh_face_map_remove(struct Mesh *me,
                                     ReportList *reports,
                                     struct CustomDataLayer *layer)
{
  /* just for sanity check */
  {
    CustomData *pdata = api_mesh_pdata_helper(me);
    int index = CustomData_get_layer_index(pdata, CD_FACEMAP);
    if (index != -1) {
      CustomDataLayer *layer_test = &pdata->layers[index];
      if (layer != layer_test) {
        /* don't show name, its likely freed memory */
        dune_report(reports, RPT_ERROR, "Face map not in mesh");
        return;
      }
    }
  }

  if (dune_mesh_clear_facemap_customdata(me) == false) {
    dune_report(reports, RPT_ERROR, "Error removing face map");
  }
}

/* End face maps */
/* poly.vertices - this is faked loop access for convenience */
static int api_MeshPoly_vertices_get_length(ApiPtr *ptr, int length[API_MAX_ARRAY_DIMENSION])
{
  MeshPoly *mp = (MeshPoly *)ptr->data;
  /* NOTE: raw access uses dummy item, this _could_ crash,
   * watch out for this, mface uses it but it can't work here. */
  return (length[0] = mp->totloop);
}

static void api_MeshPoly_vertices_get(ApiPtr *ptr, int *values)
{
  Mesh *me = api_mesh(ptr);
  MeshPoly *mp = (MeshPoly *)ptr->data;
  MeshLoop *ml = &me->mloop[mp->loopstart];
  unsigned int i;
  for (i = mp->totloop; i > 0; i--, values++, ml++) {
    *values = ml->v;
  }
}

static void api_MeshPoly_vertices_set(ApiPtr *ptr, const int *values)
{
  Mesh *me = api_mesh(ptr);
  MeshPoly *mp = (MeshPoly *)ptr->data;
  MeshLoop *ml = &me->mloop[mp->loopstart];
  unsigned int i;
  for (i = mp->totloop; i > 0; i--, values++, ml++) {
    ml->v = *values;
  }
}

/* disabling, some importers don't know the total material count when assigning materials */
#  if 0
static void api_MeshPoly_material_index_range(
    ApiPtr *ptr, int *min, int *max, int *softmin, int *softmax)
{
  Mesh *me = api_mesh(ptr);
  *min = 0;
  *max = max_ii(0, me->totcol - 1);
}
#  endif

static int api_MeshVertex_index_get(PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshVert *vert = (MeshVert *)ptr->data;
  return (int)(vert - me->mvert);
}

static int api_MeshEdge_index_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshEdge *edge = (MeshEdge *)ptr->data;
  return (int)(edge - me->medge);
}

static int api_MeshLoopTriangle_index_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshLoopTri *ltri = (MeshLoopTri *)ptr->data;
  return (int)(ltri - me->runtime.looptris.array);
}

static int api_MeshLoopTriangle_material_index_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshLoopTri *ltri = (MeshLoopTri *)ptr->data;
  return me->mpoly[ltri->poly].mat_nr;
}

static bool api_MeshLoopTriangle_use_smooth_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshLoopTri *ltri = (MeshLoopTri *)ptr->data;
  return me->mpoly[ltri->poly].flag & ME_SMOOTH;
}

static int api_MeshPolygon_index_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshPoly *mpoly = (MeshPoly *)ptr->data;
  return (int)(mpoly - me->mpoly);
}

static int api_MeshLoop_index_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  MeshLoop *mloop = (MeshLoop *)ptr->data;
  return (int)(mloop - me->mloop);
}

/* path construction */

static char *api_VertexGroupElement_path(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr); /* XXX not always! */
  MeshDeformWeight *dw = (MeshDeformWeight *)ptr->data;
  MeshDeformVert *dvert;
  int a, b;

  for (a = 0, dvert = me->dvert; a < me->totvert; a++, dvert++) {
    for (b = 0; b < dvert->totweight; b++) {
      if (dw == &dvert->dw[b]) {
        return lib_sprintfn("vertices[%d].groups[%d]", a, b);
      }
    }
  }

  return NULL;
}

static char *api_MeshPolygon_path(PointerRNA *ptr)
{
  return lib_sprintfn("polygons[%d]", (int)((MPoly *)ptr->data - rna_mesh(ptr)->mpoly));
}

static char *api_MeshLoopTriangle_path(PointerRNA *ptr)
{
  return lib_sprintfn("loop_triangles[%d]",
                      (int)((MLoopTri *)ptr->data - rna_mesh(ptr)->runtime.looptris.array));
}

static char *api_MeshEdge_path(PointerRNA *ptr)
{
  return lib_sprintfn("edges[%d]", (int)((MEdge *)ptr->data - rna_mesh(ptr)->medge));
}

static char *api_MeshLoop_path(PointerRNA *ptr)
{
  return lib_sprintfn("loops[%d]", (int)((MLoop *)ptr->data - rna_mesh(ptr)->mloop));
}

static char *api_MeshVertex_path(PointerRNA *ptr)
{
  return lib_sprintfn("vertices[%d]", (int)((MVert *)ptr->data - rna_mesh(ptr)->mvert));
}

static char *api_VertCustomData_data_path(PointerRNA *ptr, const char *collection, int type)
{
  CustomDataLayer *cdl;
  Mesh *me = api_mesh(ptr);
  CustomData *vdata = api_mesh_vdata(ptr);
  int a, b, totvert = (me->edit_mesh) ? 0 : me->totvert;

  for (cdl = vdata->layers, a = 0; a < vdata->totlayer; cdl++, a++) {
    if (cdl->type == type) {
      b = ((char *)ptr->data - ((char *)cdl->data)) / CustomData_sizeof(type);
      if (b >= 0 && b < totvert) {
        char name_esc[sizeof(cdl->name) * 2];
        lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
        return lib_sprintfn("%s[\"%s\"].data[%d]", collection, name_esc, b);
      }
    }
  }

  return NULL;
}

static char *api_PolyCustomData_data_path(ApiPtr *ptr, const char *collection, int type)
{
  CustomDataLayer *cdl;
  Mesh *me = api_mesh(ptr);
  CustomData *pdata = api_mesh_pdata(ptr);
  int a, b, totpoly = (me->edit_mesh) ? 0 : me->totpoly;

  for (cdl = pdata->layers, a = 0; a < pdata->totlayer; cdl++, a++) {
    if (cdl->type == type) {
      b = ((char *)ptr->data - ((char *)cdl->data)) / CustomData_sizeof(type);
      if (b >= 0 && b < totpoly) {
        char name_esc[sizeof(cdl->name) * 2];
        lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
        return lib_sprintfn("%s[\"%s\"].data[%d]", collection, name_esc, b);
      }
    }
  }

  return NULL;
}

static char *api_LoopCustomData_data_path(ApiPtr *ptr, const char *collection, int type)
{
  CustomDataLayer *cdl;
  Mesh *me = api_mesh(ptr);
  CustomData *ldata = api_mesh_ldata(ptr);
  int a, b, totloop = (me->edit_mesh) ? 0 : me->totloop;

  for (cdl = ldata->layers, a = 0; a < ldata->totlayer; cdl++, a++) {
    if (cdl->type == type) {
      b = ((char *)ptr->data - ((char *)cdl->data)) / CustomData_sizeof(type);
      if (b >= 0 && b < totloop) {
        char name_esc[sizeof(cdl->name) * 2];
        lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
        return lib_sprintfn("%s[\"%s\"].data[%d]", collection, name_esc, b);
      }
    }
  }

  return NULL;
}

static void api_Mesh_vertex_normals_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  const Mesh *mesh = api_mesh(ptr);
  const float(*normals)[3] = dune_mesh_vertex_normals_ensure(mesh);
  api_iter_array_begin(iter, (void *)normals, sizeof(float[3]), mesh->totvert, false, NULL);
}

static int api_Mesh_vertex_normals_length(ApiPtr *ptr)
{
  const Mesh *mesh = api_mesh(ptr);
  return mesh->totvert;
}

static void api_Mesh_poly_normals_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  const Mesh *mesh = api_mesh(ptr);
  const float(*normals)[3] = dune_mesh_poly_normals_ensure(mesh);
  api_iter_array_begin(iter, (void *)normals, sizeof(float[3]), mesh->totpoly, false, NULL);
}

static int api_Mesh_poly_normals_length(ApiPtr *ptr)
{
  const Mesh *mesh = api_mesh(ptr);
  return mesh->totpoly;
}

static char *api_MeshUVLoop_path(ApiPtr *ptr)
{
  return api_LoopCustomData_data_path(ptr, "uv_layers", CD_MLOOPUV);
}

static char *api_MeshLoopColorLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("vertex_colors[\"%s\"]", name_esc);
}

static char *api_MeshColor_path(ApiPtr *ptr)
{
  return api_LoopCustomData_data_path(ptr, "vertex_colors", CD_MLOOPCOL);
}

static char *api_MeshVertColorLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("sculpt_vertex_colors[\"%s\"]", name_esc);
}

static char *api_MeshVertColor_path(PointerRNA *ptr)
{
  return api_VertCustomData_data_path(ptr, "sculpt_vertex_colors", CD_PROP_COLOR);
}

/**** Float Property Layer API ****/
static char *api_MeshVertexFloatPropLayer_path(PointerRNA *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("vertex_float_layers[\"%s\"]", name_esc);
}
static char *api_MeshPolygonFloatPropLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("polygon_float_layers[\"%s\"]", name_esc);
}

static char *api_MeshVertexFloatProp_path(ApiPtr *ptr)
{
  return api_VertCustomData_data_path(ptr, "vertex_layers_float", CD_PROP_FLOAT);
}
static char *api_MeshPolygonFloatProp_path(ApiPtr *ptr)
{
  return api_PolyCustomData_data_path(ptr, "polygon_layers_float", CD_PROP_FLOAT);
}

static void api_MeshVertexFloatPropLayer_data_begin(CollectionPropIter iter,
                                                    ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(iter, layer->data, sizeof(MFloatProperty), me->totvert, 0, NULL);
}
static void api_MeshPolygonFloatPropertyLayer_data_begin(CollectionPropertyIterator *iter,
                                                         PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(iter, layer->data, sizeof(MFloatProperty), me->totpoly, 0, NULL);
}

static int api_MeshVertexFloatPropertyLayer_data_length(PointerRNA *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->totvert;
}
static int api_MeshPolygonFloatPropLayer_data_length(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->totpoly;
}

/**** Int Property Layer API ****/
static char *api_MeshVertexIntPropLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("vertex_int_layers[\"%s\"]", name_esc);
}
static char *api_MeshPolygonIntPropLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("polygon_int_layers[\"%s\"]", name_esc);
}

static char *api_MeshVertexIntProp_path(ApiPtr *ptr)
{
  return api_VertCustomData_data_path(ptr, "vertex_layers_int", CD_PROP_INT32);
}
static char *api_MeshPolygonIntProp_path(ApiPtr *ptr)
{
  return api_PolyCustomData_data_path(ptr, "polygon_layers_int", CD_PROP_INT32);
}

static void api_MeshVertexIntPropLayer_data_begin(CollectionPropIter *iter,
                                                  ApiPtr *ptr)
{
  Mesh *me = rna_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(iter, layer->data, sizeof(MeshIntProp), me->totvert, 0, NULL);
}
static void api_MeshPolygonIntPropertyLayer_data_begin(CollectionPropIter *iter,
                                                       ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(iter, layer->data, sizeof(MeshIntProp), me->totpoly, 0, NULL);
}

static int api_MeshVertexIntPropLayer_data_length(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->totvert;
}
static int api_MeshPolygonIntPropLayer_data_length(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->totpoly;
}

/**** String Prop Layer API ****/
static char *api_MeshVertexStringPropLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("vertex_string_layers[\"%s\"]", name_esc);
}
static char *api_MeshPolygonStringPropLayer_path(ApiPtr *ptr)
{
  CustomDataLayer *cdl = ptr->data;
  char name_esc[sizeof(cdl->name) * 2];
  lib_str_escape(name_esc, cdl->name, sizeof(name_esc));
  return lib_sprintfn("polygon_string_layers[\"%s\"]", name_esc);
}

static char *api_MeshVertexStringProp_path(ApiPtr *ptr)
{
  return api_VertCustomData_data_path(ptr, "vertex_layers_string", CD_PROP_STRING);
}
static char *api_MeshPolygonStringProp_path(ApiPtr *ptr)
{
  return api_PolyCustomData_data_path(ptr, "polygon_layers_string", CD_PROP_STRING);
}

static void api_MeshVertexStringPropLayer_data_begin(CollectionProIter *iter,
                                                     ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(iter, layer->data, sizeof(MeshStringProp), me->totvert, 0, NULL);
}
static void api_MeshPolygonStringPropertyLayer_data_begin(CollectionPropIter *iter,
                                                          ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  api_iter_array_begin(iter, layer->data, sizeof(MeshStringProp), me->totpoly, 0, NULL);
}

static int api_MeshVertexStringPropertyLayer_data_length(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->totvert;
}
static int api_MeshPolygonStringPropLayer_data_length(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->totpoly;
}

/* XXX, we don't have proper byte string support yet, so for now use the (bytes + 1)
 * mesh API exposes correct python/byte-string access. */
void api_MeshStringProp_s_get(ApiPtr *ptr, char *value)
{
  MeshStringProp *ms = (MeshStringProp *)ptr->data;
  lib_strncpy(value, ms->s, (int)ms->s_len + 1);
}

int mesh_MeshStringProp_s_length(ApiPtr *ptr)
{
  MeshStringProp *ms = (MeshStringProp *)ptr->data;
  return (int)ms->s_len + 1;
}

void api_MeshStringProp_s_set(ApiPtr *ptr, const char *value)
{
  MeshStringProp *ms = (MeshStringProp)ptr->data;
  lib_strncpy(ms->s, value, sizeof(ms->s));
}

static char *api_MeshFaceMap_path(ApiPtr *ptr)
{
  return api_PolyCustomData_data_path(ptr, "face_maps", CD_FACEMAP);
}

/***************************************/

static int api_Mesh_tot_vert_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->edit_mesh ? me->edit_mesh->bm->totvertsel : 0;
}
static int api_Mesh_tot_edge_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->edit_mesh ? me->edit_mesh->bm->totedgesel : 0;
}
static int api_Mesh_tot_face_get(ApiPtr *ptr)
{
  Mesh *me = api_mesh(ptr);
  return me->edit_mesh ? me->edit_mesh->bm->totfacesel : 0;
}

static ApiPtr api_Mesh_vertex_color_new(struct Mesh *me,
                                        ReportList *reports,
                                        const char *name,
                                        const bool do_init)
{
  ApiPtr ptr;
  CustomData *ldata;
  CustomDataLayer *cdl = NULL;
  int index = ed_mesh_color_add(me, name, false, do_init, reports);

  if (index != -1) {
    ldata = api_mesh_ldata_helper(me);
    cdl = &ldata->layers[CustomData_get_layer_index_n(ldata, CD_MLOOPCOL, index)];
  }

  api_ptr_create(&me->id, &Api_MeshLoopColorLayer, cdl, &ptr);
  return ptr;
}

static void api_Mesh_vertex_color_remove(struct Mesh *me,
                                         ReportList *reports,
                                         CustomDataLayer *layer)
{
  if (ed_mesh_color_remove_named(me, layer->name) == false) {
    dune_reportf(reports, RPT_ERROR, "Vertex color '%s' not found", layer->name);
  }
}

static ApiPtt api_Mesh_sculpt_vertex_color_new(struct Mesh *me,
                                               ReportList *reports,
                                               const char *name,
                                               const bool do_init)
{
  ApiPtr ptr;
  CustomData *vdata;
  CustomDataLayer *cdl = NULL;
  int index = ed_mesh_sculpt_color_add(me, name, false, do_init, reports);

  if (index != -1) {
    vdata = api_mesh_vdata_helper(me);
    cdl = &vdata->layers[CustomData_get_layer_index_n(vdata, CD_PROP_COLOR, index)];
  }

  api_ptr_create(&me->id, &RNA_MeshVertColorLayer, cdl, &ptr);
  return ptr;
}

static void api_Mesh_sculpt_vertex_color_remove(struct Mesh *me,
                                                ReportList *reports,
                                                CustomDataLayer *layer)
{
  if (ed_mesh_sculpt_color_remove_named(me, layer->name) == false) {
    dune_reportf(reports, RPT_ERROR, "Sculpt vertex color '%s' not found", layer->name);
  }
}

#  define DEFINE_CUSTOMDATA_PROP_API( \
      elemname, datatype, cd_prop_type, cdata, countvar, layertype) \
    static ApiPtr api_Mesh_##elemname##_##datatype##_prop_new(struct Mesh *me, \
                                                                  const char *name) \
    { \
      ApiPtr ptr; \
      CustomDataLayer *cdl = NULL; \
      int index; \
\
      CustomData_add_layer_named(&me->cdata, cd_prop_type, CD_DEFAULT, NULL, me->countvar, name); \
      index = CustomData_get_named_layer_index(&me->cdata, cd_prop_type, name); \
\
      cdl = (index == -1) ? NULL : &(me->cdata.layers[index]); \
\
      api_ptr_create(&me->id, &Api_##layertype, cdl, &ptr); \
      return ptr; \
    }

DEFINE_CUSTOMDATA_PROP_API(
    vertex, float, CD_PROP_FLOAT, vdata, totvert, MeshVertexFloatPropertyLayer)
DEFINE_CUSTOMDATA_PROP_API(
    vertex, int, CD_PROP_INT32, vdata, totvert, MeshVertexIntPropertyLayer)
DEFINE_CUSTOMDATA_PROP_API(
    vertex, string, CD_PROP_STRING, vdata, totvert, MeshVertexStringPropertyLayer)
DEFINE_CUSTOMDATA_PROP_API(
    polygon, float, CD_PROP_FLOAT, pdata, totpoly, MeshPolygonFloatPropertyLayer)
DEFINE_CUSTOMDATA_PROP_API(
    polygon, int, CD_PROP_INT32, pdata, totpoly, MeshPolygonIntPropertyLayer)
DEFINE_CUSTOMDATA_PROP_API(
    polygon, string, CD_PROP_STRING, pdata, totpoly, MeshPolygonStringPropertyLayer)
#  undef DEFINE_CUSTOMDATA_PROP_API

static ApiPtr api_Mesh_uv_layers_new(struct Mesh *me,
                                         ReportList *reports,
                                         const char *name,
                                         const bool do_init)
{
  ApiPtr ptr;
  CustomData *ldata;
  CustomDataLayer *cdl = NULL;
  int index = ed_mesh_uv_texture_add(me, name, false, do_init, reports);

  if (index != -1) {
    ldata = api_mesh_ldata_helper(me);
    cdl = &ldata->layers[CustomData_get_layer_index_n(ldata, CD_MLOOPUV, index)];
  }

  api_ptr_create(&me->id, &RNA_MeshUVLoopLayer, cdl, &ptr);
  return ptr;
}

static void api_Mesh_uv_layers_remove(struct Mesh *me, ReportList *reports, CustomDataLayer *layer)
{
  if (ed_mesh_uv_texture_remove_named(me, layer->name) == false) {
    BKE_reportf(reports, RPT_ERROR, "Texture layer '%s' not found", layer->name);
  }
}

static bool rna_Mesh_is_editmode_get(PointerRNA *ptr)
{
  Mesh *me = rna_mesh(ptr);
  return (me->edit_mesh != NULL);
}

/* only to quiet warnings */
static void UNUSED_FUNCTION(rna_mesh_unused)(void)
{
  /* unused functions made by macros */
  (void)api_Mesh_skin_vertice_index_range;
  (void)api_Mesh_vertex_paint_mask_index_range;
  (void)api_Mesh_uv_layer_render_get;
  (void)api_Mesh_uv_layer_render_index_get;
  (void)api_Mesh_uv_layer_render_index_set;
  (void)api_Mesh_uv_layer_render_set;
  (void)api_Mesh_face_map_index_range;
  (void)api_Mesh_face_map_active_index_set;
  (void)api_Mesh_face_map_active_index_get;
  (void)api_Mesh_face_map_active_set;
  (void)api_Mesh_vertex_crease_index_range;
  /* end unused function block */
}

#else

/* -------------------------------------------------------------------- */
/** Api Mesh Definition */

static void api_def_mvert_group(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "VertexGroupElement", NULL);
  spi_def_struct_stype(sapi, "MeshDeformWeight");
  api_def_struct_path_fn(sapi, "api_VertexGroupElement_path");
  api_def_struct_ui_text(
      sapi, "Vertex Group Element", "Weight value of a vertex in a vertex group");
  api_def_struct_ui_icon(sapi, ICON_GROUP_VERTEX);

  /* we can't point to actual group, it is in the object and so
   * there is no unique group to point to, hence the index */
  prop = api_def_prop(sapi, "group", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "def_nr");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Group Index", "");
  api_def_prop_update(prop, 0, "api_Mesh_update_data_legacy_deg_tag_all");

  prop = api_def_prop(sapi, "weight", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_ui_text(prop, "Weight", "Vertex Weight");
  RNA_def_property_update(prop, 0, "api_Mesh_update_data_edit_weight");
}

static void api_def_mvert(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MeshVertex", NULL);
  api_def_struct_stype(sapi, "MVert");
  api_def_struct_ui_text(sapi, "Mesh Vertex", "Vertex in a Mesh data-block");
  api_def_struct_path_fn(sapi, "api_MeshVertex_path");
  api_def_struct_ui_icon(sapi, ICON_VERTEXSEL);

  prop = api_def_prop(sapi, "co", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_ui_text(prop, "Location", "");
  api_def_prop_update(prop, 0, "api_Mesh_update_data_legacy_deg_tag_all");

  prop = api_def_prop(sapi, "normal", PROP_FLOAT, PROP_DIRECTION);
  // api_def_prop_float_sdna(prop, NULL, "no");
  api_def_prop_array(prop, 3);
  api_def_prop_range(prop, -1.0f, 1.0f);
  api_def_prop_float_fns(
      prop, "api_MeshVertex_normal_get", "api_MeshVertex_normal_set", NULL);
  api_def_prop_ui_text(prop, "Normal", "Vertex Normal");

  prop = api_def_prop(srna, "select", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SELECT);
  api_def_prop_ui_text(prop, "Select", "");
  api_def_prop_update(prop, 0, "api_Mesh_update_select");

  prop = api_def_prop(sapi, "hide", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", ME_HIDE);
  api_def_prop_ui_text(prop, "Hide", "");
  api_def_prop_update(prop, 0, "api_Mesh_update_select");

  prop = api_def_prop(sapi, "bevel_weight", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_fns(
      prop, "api_MeshVertex_bevel_weight_get", "api_MeshVertex_bevel_weight_set", NULL);
  api_def_prop_ui_text(
      prop, "Bevel Weight", "Weight used by the Bevel modifier 'Only Vertices' option");
  api_def_prop_update(prop, 0, "api_Mesh_update_data_legacy_deg_tag_all");

  prop = api_def_prop(sapi, "groups", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_fns(prop,
                                    "api_MeshVertex_groups_begin",
                                    "api_iter_array_next",
                                    "api_iter_array_end",
                                    "api_iter_array_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  api_def_prop_struct_type(prop, "VertexGroupElement");
  api_def_prop_ui_text(
      prop, "Groups", "Weights for the vertex groups this vertex is member of");

  prop = api_def_prop(srna, "index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_int_fns(prop, "api_MeshVertex_index_get", NULL, NULL);
  api_def_prop_ui_text(prop, "Index", "Index of this vertex");

  prop = api_def_prop(sapi, "undeformed_co", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(
      prop,
      "Undeformed Location",
      "For meshes with modifiers applied, the coordinate of the vertex with no deforming "
      "modifiers applied, as used for generated texture coordinates");
  api_def_prop_float_fns(prop, "api_MeshVertex_undeformed_co_get", NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
}

static void api_def_medge(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "MeshEdge", NULL);
  api_def_struct_stype(sapi, "MeshEdge");
  api_def_struct_ui_text(sapi, "Mesh Edge", "Edge in a Mesh data-block");
  api_def_struct_path_fn(sapi, "api_MeshEdge_path");
  api_def_struct_ui_icon(sapi, ICON_EDGESEL);

  prop = api_def_prop(sapi, "vertices", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "v1");
  api_def_prop_array(prop, 2);
  api_def_prop_ui_text(prop, "Vertices", "Vertex indices");
  /* XXX allows creating invalid meshes */

  prop = api_def_prop(sapi, "crease", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_fns(prop, "api_MEdge_crease_get", "rna_MEdge_crease_set", NULL);
  api_def_prop_ui_text(
      prop, "Crease", "Weight used by the Subdivision Surface modifier for creasing");
  api_def_prop_update(prop, 0, "api_Mesh_update_data_legacy_deg_tag_all");

  prop = api_def_prop(sapi, "bevel_weight", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_fns(
      prop, "api_MeshEdge_bevel_weight_get", "rna_MEdge_bevel_weight_set", NULL);
  api_def_prop_ui_text(prop, "Bevel Weight", "Weight used by the Bevel modifier");
  api_def_prop_update(prop, 0, "api_Mesh_update_data_legacy_deg_tag_all");

  prop = api_def_prop(sapi, "select", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", SELECT);
  api_def_prop_ui_text(prop, "Select", "");
  api_def_prop_update(prop, 0, "api_Mesh_update_select");

  prop = api_def_prop(sapi, "hide", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", ME_HIDE);
  api_def_prop_ui_text(prop, "Hide", "");
  api_def_prop_update(prop, 0, "rna_Mesh_update_select");

  prop = RNA_def_property(srna, "use_seam", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_SEAM);
  RNA_def_property_ui_text(prop, "Seam", "Seam edge for UV unwrapping");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_select");

  prop = RNA_def_property(srna, "use_edge_sharp", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_SHARP);
  RNA_def_property_ui_text(prop, "Sharp", "Sharp edge for the Edge Split modifier");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "is_loose", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_LOOSEEDGE);
  RNA_def_property_ui_text(prop, "Loose", "Loose edge");

  prop = RNA_def_property(srna, "use_freestyle_mark", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(
      prop, "rna_MEdge_freestyle_edge_mark_get", "rna_MEdge_freestyle_edge_mark_set");
  RNA_def_property_ui_text(prop, "Freestyle Edge Mark", "Edge mark for Freestyle line rendering");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_int_funcs(prop, "rna_MeshEdge_index_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Index", "Index of this edge");
}

static void rna_def_mlooptri(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  const int splitnor_dim[] = {3, 3};

  srna = RNA_def_struct(brna, "MeshLoopTriangle", NULL);
  RNA_def_struct_sdna(srna, "MLoopTri");
  RNA_def_struct_ui_text(srna, "Mesh Loop Triangle", "Tessellated triangle in a Mesh data-block");
  RNA_def_struct_path_func(srna, "rna_MeshLoopTriangle_path");
  RNA_def_struct_ui_icon(srna, ICON_FACESEL);

  prop = RNA_def_property(srna, "vertices", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_array(prop, 3);
  RNA_def_property_int_funcs(prop, "rna_MeshLoopTriangle_verts_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Vertices", "Indices of triangle vertices");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "loops", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "tri");
  RNA_def_property_ui_text(prop, "Loops", "Indices of mesh loops that make up the triangle");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "polygon_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "poly");
  RNA_def_property_ui_text(
      prop, "Polygon", "Index of mesh polygon that the triangle is a part of");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "normal", PROP_FLOAT, PROP_DIRECTION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_range(prop, -1.0f, 1.0f);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_MeshLoopTriangle_normal_get", NULL, NULL);
  RNA_def_property_ui_text(
      prop, "Triangle Normal", "Local space unit length normal vector for this triangle");

  prop = RNA_def_property(srna, "split_normals", PROP_FLOAT, PROP_DIRECTION);
  RNA_def_property_multi_array(prop, 2, splitnor_dim);
  RNA_def_property_range(prop, -1.0f, 1.0f);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_MeshLoopTriangle_split_normals_get", NULL, NULL);
  RNA_def_property_ui_text(
      prop,
      "Split Normals",
      "Local space unit length split normals vectors of the vertices of this triangle "
      "(must be computed beforehand using calc_normals_split or calc_tangents)");

  prop = RNA_def_property(srna, "area", PROP_FLOAT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_MeshLoopTriangle_area_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Triangle Area", "Area of this triangle");

  prop = RNA_def_property(srna, "index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_int_funcs(prop, "rna_MeshLoopTriangle_index_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Index", "Index of this loop triangle");

  prop = RNA_def_property(srna, "material_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_int_funcs(prop, "rna_MeshLoopTriangle_material_index_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Material Index", "Material slot index of this triangle");

  prop = RNA_def_property(srna, "use_smooth", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_boolean_funcs(prop, "rna_MeshLoopTriangle_use_smooth_get", NULL);
  RNA_def_property_ui_text(prop, "Smooth", "");
}

static void rna_def_mloop(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "MeshLoop", NULL);
  RNA_def_struct_sdna(srna, "MLoop");
  RNA_def_struct_ui_text(srna, "Mesh Loop", "Loop in a Mesh data-block");
  RNA_def_struct_path_func(srna, "rna_MeshLoop_path");
  RNA_def_struct_ui_icon(srna, ICON_EDGESEL);

  prop = RNA_def_property(srna, "vertex_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "v");
  RNA_def_property_ui_text(prop, "Vertex", "Vertex index");

  prop = RNA_def_property(srna, "edge_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "e");
  RNA_def_property_ui_text(prop, "Edge", "Edge index");

  prop = RNA_def_property(srna, "index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_int_funcs(prop, "rna_MeshLoop_index_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Index", "Index of this loop");

  prop = RNA_def_property(srna, "normal", PROP_FLOAT, PROP_DIRECTION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_range(prop, -1.0f, 1.0f);
  RNA_def_property_float_funcs(prop, "rna_MeshLoop_normal_get", "rna_MeshLoop_normal_set", NULL);
  RNA_def_property_ui_text(
      prop,
      "Normal",
      "Local space unit length split normal vector of this vertex for this polygon "
      "(must be computed beforehand using calc_normals_split or calc_tangents)");

  prop = RNA_def_property(srna, "tangent", PROP_FLOAT, PROP_DIRECTION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_range(prop, -1.0f, 1.0f);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_MeshLoop_tangent_get", NULL, NULL);
  RNA_def_property_ui_text(
      prop,
      "Tangent",
      "Local space unit length tangent vector of this vertex for this polygon "
      "(must be computed beforehand using calc_tangents)");

  prop = RNA_def_property(srna, "bitangent_sign", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, -1.0f, 1.0f);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_MeshLoop_bitangent_sign_get", NULL, NULL);
  RNA_def_property_ui_text(
      prop,
      "Bitangent Sign",
      "Sign of the bitangent vector of this vertex for this polygon (must be computed "
      "beforehand using calc_tangents, bitangent = bitangent_sign * cross(normal, tangent))");

  prop = RNA_def_property(srna, "bitangent", PROP_FLOAT, PROP_DIRECTION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_range(prop, -1.0f, 1.0f);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_MeshLoop_bitangent_get", NULL, NULL);
  RNA_def_property_ui_text(
      prop,
      "Bitangent",
      "Bitangent vector of this vertex for this polygon (must be computed beforehand using "
      "calc_tangents, use it only if really needed, slower access than bitangent_sign)");
}

static void rna_def_mpolygon(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  FunctionRNA *func;

  srna = RNA_def_struct(brna, "MeshPolygon", NULL);
  RNA_def_struct_sdna(srna, "MPoly");
  RNA_def_struct_ui_text(srna, "Mesh Polygon", "Polygon in a Mesh data-block");
  RNA_def_struct_path_func(srna, "rna_MeshPolygon_path");
  RNA_def_struct_ui_icon(srna, ICON_FACESEL);

  /* Faked, actually access to loop vertex values, don't this way because manually setting up
   * vertex/edge per loop is very low level.
   * Instead we setup poly sizes, assign indices, then calc edges automatic when creating
   * meshes from rna/py. */
  prop = RNA_def_property(srna, "vertices", PROP_INT, PROP_UNSIGNED);
  /* Eek, this is still used in some cases but in fact we don't want to use it at all here. */
  RNA_def_property_array(prop, 3);
  RNA_def_property_flag(prop, PROP_DYNAMIC);
  RNA_def_property_dynamic_array_funcs(prop, "rna_MeshPoly_vertices_get_length");
  RNA_def_property_int_funcs(prop, "rna_MeshPoly_vertices_get", "rna_MeshPoly_vertices_set", NULL);
  RNA_def_property_ui_text(prop, "Vertices", "Vertex indices");

  /* these are both very low level access */
  prop = RNA_def_property(srna, "loop_start", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "loopstart");
  RNA_def_property_ui_text(prop, "Loop Start", "Index of the first loop of this polygon");
  /* also low level */
  prop = RNA_def_property(srna, "loop_total", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "totloop");
  RNA_def_property_ui_text(prop, "Loop Total", "Number of loops used by this polygon");

  prop = RNA_def_property(srna, "material_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "mat_nr");
  RNA_def_property_ui_text(prop, "Material Index", "Material slot index of this polygon");
#  if 0
  RNA_def_property_int_funcs(prop, NULL, NULL, "rna_MeshPoly_material_index_range");
#  endif
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "select", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_FACE_SEL);
  RNA_def_property_ui_text(prop, "Select", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_select");

  prop = RNA_def_property(srna, "hide", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_HIDE);
  RNA_def_property_ui_text(prop, "Hide", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_select");

  prop = RNA_def_property(srna, "use_smooth", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_SMOOTH);
  RNA_def_property_ui_text(prop, "Smooth", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "use_freestyle_mark", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(
      prop, "rna_MPoly_freestyle_face_mark_get", "rna_MPoly_freestyle_face_mark_set");
  RNA_def_property_ui_text(prop, "Freestyle Face Mark", "Face mark for Freestyle line rendering");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "normal", PROP_FLOAT, PROP_DIRECTION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_range(prop, -1.0f, 1.0f);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_MeshPolygon_normal_get", NULL, NULL);
  RNA_def_property_ui_text(
      prop, "Polygon Normal", "Local space unit length normal vector for this polygon");

  prop = RNA_def_property(srna, "center", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_array(prop, 3);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_MeshPolygon_center_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Polygon Center", "Center of this polygon");

  prop = RNA_def_property(srna, "area", PROP_FLOAT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_float_funcs(prop, "rna_MeshPolygon_area_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Polygon Area", "Read only area of this polygon");

  prop = RNA_def_property(srna, "index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_int_funcs(prop, "rna_MeshPolygon_index_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Index", "Index of this polygon");

  func = RNA_def_function(srna, "flip", "rna_MeshPolygon_flip");
  RNA_def_function_flag(func, FUNC_USE_SELF_ID);
  RNA_def_function_ui_description(func, "Invert winding of this polygon (flip its normal)");
}

/* mesh.loop_uvs */
static void rna_def_mloopuv(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "MeshUVLoopLayer", NULL);
  RNA_def_struct_sdna(srna, "CustomDataLayer");
  RNA_def_struct_path_func(srna, "rna_MeshUVLoopLayer_path");

  prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshUVLoop");
  RNA_def_property_collection_funcs(prop,
                                    "rna_MeshUVLoopLayer_data_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_MeshUVLoopLayer_data_length",
                                    NULL,
                                    NULL,
                                    NULL);

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_MeshLoopLayer_name_set");
  RNA_def_property_ui_text(prop, "Name", "Name of UV map");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "active", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(
      prop, "rna_MeshUVLoopLayer_active_get", "rna_MeshUVLoopLayer_active_set");
  RNA_def_property_ui_text(prop, "Active", "Set the map as active for display and editing");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "active_render", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "active_rnd", 0);
  RNA_def_property_boolean_funcs(
      prop, "rna_MeshUVLoopLayer_active_render_get", "rna_MeshUVLoopLayer_active_render_set");
  RNA_def_property_ui_text(prop, "Active Render", "Set the map as active for rendering");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "active_clone", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "active_clone", 0);
  RNA_def_property_boolean_funcs(
      prop, "rna_MeshUVLoopLayer_clone_get", "rna_MeshUVLoopLayer_clone_set");
  RNA_def_property_ui_text(prop, "Active Clone", "Set the map as active for cloning");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  srna = RNA_def_struct(brna, "MeshUVLoop", NULL);
  RNA_def_struct_sdna(srna, "MLoopUV");
  RNA_def_struct_path_func(srna, "rna_MeshUVLoop_path");

  prop = RNA_def_property(srna, "uv", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "pin_uv", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", MLOOPUV_PINNED);
  RNA_def_property_ui_text(prop, "UV Pinned", "");

  prop = RNA_def_property(srna, "select", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", MLOOPUV_VERTSEL);
  RNA_def_property_ui_text(prop, "UV Select", "");

  prop = RNA_def_property(srna, "select_edge", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", MLOOPUV_EDGESEL);
  RNA_def_property_ui_text(prop, "UV Edge Select", "");
}

static void rna_def_mloopcol(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "MeshLoopColorLayer", NULL);
  RNA_def_struct_ui_text(
      srna, "Mesh Vertex Color Layer", "Layer of vertex colors in a Mesh data-block");
  RNA_def_struct_sdna(srna, "CustomDataLayer");
  RNA_def_struct_path_func(srna, "rna_MeshLoopColorLayer_path");
  RNA_def_struct_ui_icon(srna, ICON_GROUP_VCOL);

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_MeshLoopLayer_name_set");
  RNA_def_property_ui_text(prop, "Name", "Name of Vertex color layer");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "active", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(
      prop, "rna_MeshLoopColorLayer_active_get", "rna_MeshLoopColorLayer_active_set");
  RNA_def_property_ui_text(prop, "Active", "Sets the layer as active for display and editing");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "active_render", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "active_rnd", 0);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_MeshLoopColorLayer_active_render_get",
                                 "rna_MeshLoopColorLayer_active_render_set");
  RNA_def_property_ui_text(prop, "Active Render", "Sets the layer as active for rendering");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshLoopColor");
  RNA_def_property_ui_text(prop, "Data", "");
  RNA_def_property_collection_funcs(prop,
                                    "rna_MeshLoopColorLayer_data_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_MeshLoopColorLayer_data_length",
                                    NULL,
                                    NULL,
                                    NULL);

  srna = RNA_def_struct(brna, "MeshLoopColor", NULL);
  RNA_def_struct_sdna(srna, "MLoopCol");
  RNA_def_struct_ui_text(srna, "Mesh Vertex Color", "Vertex loop colors in a Mesh");
  RNA_def_struct_path_func(srna, "rna_MeshColor_path");

  prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
  RNA_def_property_array(prop, 4);
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_float_funcs(
      prop, "rna_MeshLoopColor_color_get", "rna_MeshLoopColor_color_set", NULL);
  RNA_def_property_ui_text(prop, "Color", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");
}

static void rna_def_MPropCol(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "MeshVertColorLayer", NULL);
  RNA_def_struct_ui_text(srna,
                         "Mesh Sculpt Vertex Color Layer",
                         "Layer of sculpt vertex colors in a Mesh data-block");
  RNA_def_struct_sdna(srna, "CustomDataLayer");
  RNA_def_struct_path_func(srna, "rna_MeshVertColorLayer_path");
  RNA_def_struct_ui_icon(srna, ICON_GROUP_VCOL);

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_MeshVertexLayer_name_set");
  RNA_def_property_ui_text(prop, "Name", "Name of Sculpt Vertex color layer");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "active", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(
      prop, "rna_MeshVertColorLayer_active_get", "rna_MeshVertColorLayer_active_set");
  RNA_def_property_ui_text(
      prop, "Active", "Sets the sculpt vertex color layer as active for display and editing");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "active_render", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "active_rnd", 0);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_MeshVertColorLayer_active_render_get",
                                 "rna_MeshVertColorLayer_active_render_set");
  RNA_def_property_ui_text(
      prop, "Active Render", "Sets the sculpt vertex color layer as active for rendering");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshVertColor");
  RNA_def_property_ui_text(prop, "Data", "");
  RNA_def_property_collection_funcs(prop,
                                    "rna_MeshVertColorLayer_data_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_MeshVertColorLayer_data_length",
                                    NULL,
                                    NULL,
                                    NULL);

  srna = RNA_def_struct(brna, "MeshVertColor", NULL);
  RNA_def_struct_sdna(srna, "MPropCol");
  RNA_def_struct_ui_text(srna, "Mesh Sculpt Vertex Color", "Vertex colors in a Mesh");
  RNA_def_struct_path_func(srna, "rna_MeshVertColor_path");

  prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
  RNA_def_property_array(prop, 4);
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_text(prop, "Color", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");
}
static void rna_def_mproperties(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  /* Float */
#  define MESH_FLOAT_PROPERTY_LAYER(elemname) \
    srna = RNA_def_struct(brna, "Mesh" elemname "FloatPropertyLayer", NULL); \
    RNA_def_struct_sdna(srna, "CustomDataLayer"); \
    RNA_def_struct_ui_text(srna, \
                           "Mesh " elemname " Float Property Layer", \
                           "User defined layer of floating-point number values"); \
    RNA_def_struct_path_func(srna, "rna_Mesh" elemname "FloatPropertyLayer_path"); \
\
    prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE); \
    RNA_def_struct_name_property(srna, prop); \
    RNA_def_property_string_funcs(prop, NULL, NULL, "rna_MeshAnyLayer_name_set"); \
    RNA_def_property_ui_text(prop, "Name", ""); \
    RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all"); \
\
    prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE); \
    RNA_def_property_struct_type(prop, "Mesh" elemname "FloatProperty"); \
    RNA_def_property_ui_text(prop, "Data", ""); \
    RNA_def_property_collection_funcs(prop, \
                                      "rna_Mesh" elemname "FloatPropertyLayer_data_begin", \
                                      "rna_iterator_array_next", \
                                      "rna_iterator_array_end", \
                                      "rna_iterator_array_get", \
                                      "rna_Mesh" elemname "FloatPropertyLayer_data_length", \
                                      NULL, \
                                      NULL, \
                                      NULL); \
\
    srna = RNA_def_struct(brna, "Mesh" elemname "FloatProperty", NULL); \
    RNA_def_struct_sdna(srna, "MFloatProperty"); \
    RNA_def_struct_ui_text( \
        srna, \
        "Mesh " elemname " Float Property", \
        "User defined floating-point number value in a float properties layer"); \
    RNA_def_struct_path_func(srna, "rna_Mesh" elemname "FloatProperty_path"); \
\
    prop = RNA_def_property(srna, "value", PROP_FLOAT, PROP_NONE); \
    RNA_def_property_float_sdna(prop, NULL, "f"); \
    RNA_def_property_ui_text(prop, "Value", ""); \
    RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all"); \
    ((void)0)

  /* Int */
#  define MESH_INT_PROPERTY_LAYER(elemname) \
    srna = RNA_def_struct(brna, "Mesh" elemname "IntPropertyLayer", NULL); \
    RNA_def_struct_sdna(srna, "CustomDataLayer"); \
    RNA_def_struct_ui_text(srna, \
                           "Mesh " elemname " Int Property Layer", \
                           "User defined layer of integer number values"); \
    RNA_def_struct_path_func(srna, "rna_Mesh" elemname "IntPropertyLayer_path"); \
\
    prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE); \
    RNA_def_struct_name_property(srna, prop); \
    RNA_def_property_string_funcs(prop, NULL, NULL, "rna_MeshAnyLayer_name_set"); \
    RNA_def_property_ui_text(prop, "Name", ""); \
    RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all"); \
\
    prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE); \
    RNA_def_property_struct_type(prop, "Mesh" elemname "IntProperty"); \
    RNA_def_property_ui_text(prop, "Data", ""); \
    RNA_def_property_collection_funcs(prop, \
                                      "rna_Mesh" elemname "IntPropertyLayer_data_begin", \
                                      "rna_iterator_array_next", \
                                      "rna_iterator_array_end", \
                                      "rna_iterator_array_get", \
                                      "rna_Mesh" elemname "IntPropertyLayer_data_length", \
                                      NULL, \
                                      NULL, \
                                      NULL); \
\
    srna = RNA_def_struct(brna, "Mesh" elemname "IntProperty", NULL); \
    RNA_def_struct_sdna(srna, "MIntProperty"); \
    RNA_def_struct_ui_text(srna, \
                           "Mesh " elemname " Int Property", \
                           "User defined integer number value in an integer properties layer"); \
    RNA_def_struct_path_func(srna, "rna_Mesh" elemname "IntProperty_path"); \
\
    prop = RNA_def_property(srna, "value", PROP_INT, PROP_NONE); \
    RNA_def_property_int_sdna(prop, NULL, "i"); \
    RNA_def_property_ui_text(prop, "Value", ""); \
    RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all"); \
    ((void)0)

  /* String */
#  define MESH_STRING_PROPERTY_LAYER(elemname) \
    srna = RNA_def_struct(brna, "Mesh" elemname "StringPropertyLayer", NULL); \
    RNA_def_struct_sdna(srna, "CustomDataLayer"); \
    RNA_def_struct_ui_text(srna, \
                           "Mesh " elemname " String Property Layer", \
                           "User defined layer of string text values"); \
    RNA_def_struct_path_func(srna, "rna_Mesh" elemname "StringPropertyLayer_path"); \
\
    prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE); \
    RNA_def_struct_name_property(srna, prop); \
    RNA_def_property_string_funcs(prop, NULL, NULL, "rna_MeshAnyLayer_name_set"); \
    RNA_def_property_ui_text(prop, "Name", ""); \
    RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all"); \
\
    prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE); \
    RNA_def_property_struct_type(prop, "Mesh" elemname "StringProperty"); \
    RNA_def_property_ui_text(prop, "Data", ""); \
    RNA_def_property_collection_funcs(prop, \
                                      "rna_Mesh" elemname "StringPropertyLayer_data_begin", \
                                      "rna_iterator_array_next", \
                                      "rna_iterator_array_end", \
                                      "rna_iterator_array_get", \
                                      "rna_Mesh" elemname "StringPropertyLayer_data_length", \
                                      NULL, \
                                      NULL, \
                                      NULL); \
\
    srna = RNA_def_struct(brna, "Mesh" elemname "StringProperty", NULL); \
    RNA_def_struct_sdna(srna, "MStringProperty"); \
    RNA_def_struct_ui_text(srna, \
                           "Mesh " elemname " String Property", \
                           "User defined string text value in a string properties layer"); \
    RNA_def_struct_path_func(srna, "rna_Mesh" elemname "StringProperty_path"); \
\
    /* low level mesh data access, treat as bytes */ \
    prop = RNA_def_property(srna, "value", PROP_STRING, PROP_BYTESTRING); \
    RNA_def_property_string_sdna(prop, NULL, "s"); \
    RNA_def_property_string_funcs(prop, \
                                  "rna_MeshStringProperty_s_get", \
                                  "rna_MeshStringProperty_s_length", \
                                  "rna_MeshStringProperty_s_set"); \
    RNA_def_property_ui_text(prop, "Value", ""); \
    RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  MESH_FLOAT_PROPERTY_LAYER("Vertex");
  MESH_FLOAT_PROPERTY_LAYER("Polygon");
  MESH_INT_PROPERTY_LAYER("Vertex");
  MESH_INT_PROPERTY_LAYER("Polygon");
  MESH_STRING_PROPERTY_LAYER("Vertex")
  MESH_STRING_PROPERTY_LAYER("Polygon")
#  undef MESH_PROPERTY_LAYER
}

void rna_def_texmat_common(StructRNA *srna, const char *texspace_editable)
{
  PropertyRNA *prop;

  /* texture space */
  prop = RNA_def_property(srna, "auto_texspace", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "texflag", ME_AUTOSPACE);
  RNA_def_property_ui_text(
      prop,
      "Auto Texture Space",
      "Adjust active object's texture space automatically when transforming object");

  prop = RNA_def_property(srna, "texspace_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_float_sdna(prop, NULL, "loc");
  RNA_def_property_ui_text(prop, "Texture Space Location", "Texture space location");
  RNA_def_property_float_funcs(prop, "rna_Mesh_texspace_loc_get", NULL, NULL);
  RNA_def_property_editable_func(prop, texspace_editable);
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "texspace_size", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_float_sdna(prop, NULL, "size");
  RNA_def_property_flag(prop, PROP_PROPORTIONAL);
  RNA_def_property_ui_text(prop, "Texture Space Size", "Texture space size");
  RNA_def_property_float_funcs(prop, "rna_Mesh_texspace_size_get", NULL, NULL);
  RNA_def_property_editable_func(prop, texspace_editable);
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  /* materials */
  prop = RNA_def_property(srna, "materials", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "mat", "totcol");
  RNA_def_property_struct_type(prop, "Material");
  RNA_def_property_ui_text(prop, "Materials", "");
  RNA_def_property_srna(prop, "IDMaterials"); /* see rna_ID.c */
  RNA_def_property_collection_funcs(
      prop, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "rna_IDMaterials_assign_int");
}

/* scene.objects */
/* mesh.vertices */
static void rna_def_mesh_vertices(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  /*  PropertyRNA *prop; */

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "MeshVertices");
  srna = RNA_def_struct(brna, "MeshVertices", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Mesh Vertices", "Collection of mesh vertices");

  func = RNA_def_function(srna, "add", "ED_mesh_verts_add");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_int(
      func, "count", 0, 0, INT_MAX, "Count", "Number of vertices to add", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
#  if 0 /* BMESH_TODO Remove until BMesh merge */
  func = RNA_def_function(srna, "remove", "ED_mesh_verts_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_int(func, "count", 0, 0, INT_MAX, "Count", "Number of vertices to remove", 0, INT_MAX);
#  endif
}

/* mesh.edges */
static void rna_def_mesh_edges(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  /*  PropertyRNA *prop; */

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "MeshEdges");
  srna = RNA_def_struct(brna, "MeshEdges", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Mesh Edges", "Collection of mesh edges");

  func = RNA_def_function(srna, "add", "ED_mesh_edges_add");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_int(func, "count", 0, 0, INT_MAX, "Count", "Number of edges to add", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
#  if 0 /* BMESH_TODO Remove until BMesh merge */
  func = RNA_def_function(srna, "remove", "ED_mesh_edges_remove");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_int(func, "count", 0, 0, INT_MAX, "Count", "Number of edges to remove", 0, INT_MAX);
#  endif
}

/* mesh.loop_triangles */
static void rna_def_mesh_looptris(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  RNA_def_property_srna(cprop, "MeshLoopTriangles");
  srna = RNA_def_struct(brna, "MeshLoopTriangles", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(
      srna, "Mesh Loop Triangles", "Tessellation of mesh polygons into triangles");
}

/* mesh.loops */
static void rna_def_mesh_loops(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  // PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "MeshLoops");
  srna = RNA_def_struct(brna, "MeshLoops", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Mesh Loops", "Collection of mesh loops");

  func = RNA_def_function(srna, "add", "ED_mesh_loops_add");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_int(func, "count", 0, 0, INT_MAX, "Count", "Number of loops to add", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

/* mesh.polygons */
static void rna_def_mesh_polygons(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "MeshPolygons");
  srna = RNA_def_struct(brna, "MeshPolygons", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Mesh Polygons", "Collection of mesh polygons");

  prop = RNA_def_property(srna, "active", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "act_face");
  RNA_def_property_ui_text(prop, "Active Polygon", "The active polygon for this mesh");

  func = RNA_def_function(srna, "add", "ED_mesh_polys_add");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_int(
      func, "count", 0, 0, INT_MAX, "Count", "Number of polygons to add", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

/* Defines a read-only vector type since normals should not be modified manually. */
static void rna_def_normal_layer_value(BlenderRNA *brna)
{
  StructRNA *srna = RNA_def_struct(brna, "MeshNormalValue", NULL);
  RNA_def_struct_sdna(srna, "vec3f");
  RNA_def_struct_ui_text(srna, "Mesh Normal Vector", "Vector in a mesh normal array");

  PropertyRNA *prop = RNA_def_property(srna, "vector", PROP_FLOAT, PROP_DIRECTION);
  RNA_def_property_ui_text(prop, "Vector", "3D vector");
  RNA_def_property_float_sdna(prop, NULL, "x");
  RNA_def_property_array(prop, 3);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
}

static void rna_def_loop_colors(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "LoopColors");
  srna = RNA_def_struct(brna, "LoopColors", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Loop Colors", "Collection of vertex colors");

  func = RNA_def_function(srna, "new", "rna_Mesh_vertex_color_new");
  RNA_def_function_ui_description(func, "Add a vertex color layer to Mesh");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_string(func, "name", "Col", 0, "", "Vertex color name");
  RNA_def_boolean(func,
                  "do_init",
                  true,
                  "",
                  "Whether new layer's data should be initialized by copying current active one");
  parm = RNA_def_pointer(func, "layer", "MeshLoopColorLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Mesh_vertex_color_remove");
  RNA_def_function_ui_description(func, "Remove a vertex color layer");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "layer", "MeshLoopColorLayer", "", "The layer to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  RNA_def_property_clear_flag(parm, PROP_THICK_WRAP);

  prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshLoopColorLayer");
  RNA_def_property_pointer_funcs(
      prop, "rna_Mesh_vertex_color_active_get", "rna_Mesh_vertex_color_active_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  RNA_def_property_ui_text(prop, "Active Vertex Color Layer", "Active vertex color layer");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_edit_active_color");

  prop = RNA_def_property(srna, "active_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_funcs(prop,
                             "rna_Mesh_vertex_color_active_index_get",
                             "rna_Mesh_vertex_color_active_index_set",
                             "rna_Mesh_vertex_color_index_range");
  RNA_def_property_ui_text(prop, "Active Vertex Color Index", "Active vertex color index");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_edit_active_color");
}

static void rna_def_vert_colors(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "VertColors");
  srna = RNA_def_struct(brna, "VertColors", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Vert Colors", "Collection of sculpt vertex colors");

  func = RNA_def_function(srna, "new", "rna_Mesh_sculpt_vertex_color_new");
  RNA_def_function_ui_description(func, "Add a sculpt vertex color layer to Mesh");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_string(func, "name", "Col", 0, "", "Sculpt Vertex color name");
  RNA_def_boolean(func,
                  "do_init",
                  true,
                  "",
                  "Whether new layer's data should be initialized by copying current active one");
  parm = RNA_def_pointer(func, "layer", "MeshVertColorLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Mesh_sculpt_vertex_color_remove");
  RNA_def_function_ui_description(func, "Remove a vertex color layer");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "layer", "MeshVertColorLayer", "", "The layer to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  RNA_def_property_clear_flag(parm, PROP_THICK_WRAP);

  prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshVertColorLayer");
  RNA_def_property_pointer_funcs(prop,
                                 "rna_Mesh_sculpt_vertex_color_active_get",
                                 "rna_Mesh_sculpt_vertex_color_active_set",
                                 NULL,
                                 NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  RNA_def_property_ui_text(
      prop, "Active Sculpt Vertex Color Layer", "Active sculpt vertex color layer");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_edit_active_color");

  prop = RNA_def_property(srna, "active_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_funcs(prop,
                             "rna_Mesh_sculpt_vertex_color_active_index_get",
                             "rna_Mesh_sculpt_vertex_color_active_index_set",
                             "rna_Mesh_sculpt_vertex_color_index_range");
  RNA_def_property_ui_text(
      prop, "Active Sculpt Vertex Color Index", "Active sculpt vertex color index");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_edit_active_color");
}

static void rna_def_uv_layers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "UVLoopLayers");
  srna = RNA_def_struct(brna, "UVLoopLayers", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "UV Loop Layers", "Collection of uv loop layers");

  func = RNA_def_function(srna, "new", "rna_Mesh_uv_layers_new");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Add a UV map layer to Mesh");
  RNA_def_string(func, "name", "UVMap", 0, "", "UV map name");
  RNA_def_boolean(func,
                  "do_init",
                  true,
                  "",
                  "Whether new layer's data should be initialized by copying current active one, "
                  "or if none is active, with a default UVmap");
  parm = RNA_def_pointer(func, "layer", "MeshUVLoopLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Mesh_uv_layers_remove");
  RNA_def_function_ui_description(func, "Remove a vertex color layer");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "layer", "MeshUVLoopLayer", "", "The layer to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshUVLoopLayer");
  RNA_def_property_pointer_funcs(
      prop, "rna_Mesh_uv_layer_active_get", "rna_Mesh_uv_layer_active_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  RNA_def_property_ui_text(prop, "Active UV Loop Layer", "Active UV loop layer");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "active_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_funcs(prop,
                             "rna_Mesh_uv_layer_active_index_get",
                             "rna_Mesh_uv_layer_active_index_set",
                             "rna_Mesh_uv_layer_index_range");
  RNA_def_property_ui_text(prop, "Active UV Loop Layer Index", "Active UV loop layer index");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");
}

/* mesh float layers */
static void rna_def_vertex_float_layers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "VertexFloatProperties");
  srna = RNA_def_struct(brna, "VertexFloatProperties", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Vertex Float Properties", "Collection of float properties");

  func = RNA_def_function(srna, "new", "rna_Mesh_vertex_float_property_new");
  RNA_def_function_ui_description(func, "Add a float property layer to Mesh");
  RNA_def_string(func, "name", "Float Prop", 0, "", "Float property name");
  parm = RNA_def_pointer(
      func, "layer", "MeshVertexFloatPropertyLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);
}

/* mesh int layers */
static void rna_def_vertex_int_layers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "VertexIntProperties");
  srna = RNA_def_struct(brna, "VertexIntProperties", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Vertex Int Properties", "Collection of int properties");

  func = RNA_def_function(srna, "new", "rna_Mesh_vertex_int_property_new");
  RNA_def_function_ui_description(func, "Add a integer property layer to Mesh");
  RNA_def_string(func, "name", "Int Prop", 0, "", "Int property name");
  parm = RNA_def_pointer(
      func, "layer", "MeshVertexIntPropertyLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);
}

/* mesh string layers */
static void rna_def_vertex_string_layers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "VertexStringProperties");
  srna = RNA_def_struct(brna, "VertexStringProperties", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Vertex String Properties", "Collection of string properties");

  func = RNA_def_function(srna, "new", "rna_Mesh_vertex_string_property_new");
  RNA_def_function_ui_description(func, "Add a string property layer to Mesh");
  RNA_def_string(func, "name", "String Prop", 0, "", "String property name");
  parm = RNA_def_pointer(
      func, "layer", "MeshVertexStringPropertyLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);
}

/* mesh float layers */
static void rna_def_polygon_float_layers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "PolygonFloatProperties");
  srna = RNA_def_struct(brna, "PolygonFloatProperties", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Polygon Float Properties", "Collection of float properties");

  func = RNA_def_function(srna, "new", "rna_Mesh_polygon_float_property_new");
  RNA_def_function_ui_description(func, "Add a float property layer to Mesh");
  RNA_def_string(func, "name", "Float Prop", 0, "", "Float property name");
  parm = RNA_def_pointer(
      func, "layer", "MeshPolygonFloatPropertyLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);
}

/* mesh int layers */
static void rna_def_polygon_int_layers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "PolygonIntProperties");
  srna = RNA_def_struct(brna, "PolygonIntProperties", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Polygon Int Properties", "Collection of int properties");

  func = RNA_def_function(srna, "new", "rna_Mesh_polygon_int_property_new");
  RNA_def_function_ui_description(func, "Add a integer property layer to Mesh");
  RNA_def_string(func, "name", "Int Prop", 0, "", "Int property name");
  parm = RNA_def_pointer(
      func, "layer", "MeshPolygonIntPropertyLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);
}

/* mesh string layers */
static void rna_def_polygon_string_layers(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "PolygonStringProperties");
  srna = RNA_def_struct(brna, "PolygonStringProperties", NULL);
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Polygon String Properties", "Collection of string properties");

  func = RNA_def_function(srna, "new", "rna_Mesh_polygon_string_property_new");
  RNA_def_function_ui_description(func, "Add a string property layer to Mesh");
  RNA_def_string(func, "name", "String Prop", 0, "", "String property name");
  parm = RNA_def_pointer(
      func, "layer", "MeshPolygonStringPropertyLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);
}

static void rna_def_skin_vertices(BlenderRNA *brna, PropertyRNA *UNUSED(cprop))
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "MeshSkinVertexLayer", NULL);
  RNA_def_struct_ui_text(
      srna, "Mesh Skin Vertex Layer", "Per-vertex skin data for use with the Skin modifier");
  RNA_def_struct_sdna(srna, "CustomDataLayer");
  RNA_def_struct_path_func(srna, "rna_MeshSkinVertexLayer_path");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_MeshVertexLayer_name_set");
  RNA_def_property_ui_text(prop, "Name", "Name of skin layer");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshSkinVertex");
  RNA_def_property_ui_text(prop, "Data", "");
  RNA_def_property_collection_funcs(prop,
                                    "rna_MeshSkinVertexLayer_data_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_MeshSkinVertexLayer_data_length",
                                    NULL,
                                    NULL,
                                    NULL);

  /* SkinVertex struct */
  srna = RNA_def_struct(brna, "MeshSkinVertex", NULL);
  RNA_def_struct_sdna(srna, "MVertSkin");
  RNA_def_struct_ui_text(
      srna, "Skin Vertex", "Per-vertex skin data for use with the Skin modifier");
  RNA_def_struct_path_func(srna, "rna_MeshSkinVertex_path");

  prop = RNA_def_property(srna, "radius", PROP_FLOAT, PROP_UNSIGNED);
  RNA_def_property_array(prop, 2);
  RNA_def_property_ui_range(prop, 0.001, 100.0, 1, 3);
  RNA_def_property_ui_text(prop, "Radius", "Radius of the skin");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  /* Flags */

  prop = RNA_def_property(srna, "use_root", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", MVERT_SKIN_ROOT);
  RNA_def_property_ui_text(prop,
                           "Root",
                           "Vertex is a root for rotation calculations and armature generation, "
                           "setting this flag does not clear other roots in the same mesh island");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "use_loose", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", MVERT_SKIN_LOOSE);
  RNA_def_property_ui_text(
      prop, "Loose", "If vertex has multiple adjacent edges, it is hulled to them directly");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");
}

static void rna_def_vertex_creases(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "MeshVertexCreaseLayer", NULL);
  RNA_def_struct_ui_text(srna, "Mesh Vertex Crease Layer", "Per-vertex crease");
  RNA_def_struct_sdna(srna, "CustomDataLayer");
  RNA_def_struct_path_func(srna, "rna_MeshVertexCreaseLayer_path");

  prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshVertexCrease");
  RNA_def_property_ui_text(prop, "Data", "");
  RNA_def_property_collection_funcs(prop,
                                    "rna_MeshVertexCreaseLayer_data_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_MeshVertexCreaseLayer_data_length",
                                    NULL,
                                    NULL,
                                    NULL);

  /* VertexCrease struct */
  srna = RNA_def_struct(brna, "MeshVertexCrease", NULL);
  RNA_def_struct_sdna(srna, "MFloatProperty");
  RNA_def_struct_ui_text(srna, "Float Property", "");

  prop = RNA_def_property(srna, "value", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "f");
  RNA_def_property_ui_text(prop, "Value", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");
}

static void rna_def_paint_mask(BlenderRNA *brna, PropertyRNA *UNUSED(cprop))
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "MeshPaintMaskLayer", NULL);
  RNA_def_struct_ui_text(srna, "Mesh Paint Mask Layer", "Per-vertex paint mask data");
  RNA_def_struct_sdna(srna, "CustomDataLayer");
  RNA_def_struct_path_func(srna, "rna_MeshPaintMaskLayer_path");

  prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshPaintMaskProperty");
  RNA_def_property_ui_text(prop, "Data", "");

  RNA_def_property_collection_funcs(prop,
                                    "rna_MeshPaintMaskLayer_data_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_MeshPaintMaskLayer_data_length",
                                    NULL,
                                    NULL,
                                    NULL);

  srna = RNA_def_struct(brna, "MeshPaintMaskProperty", NULL);
  RNA_def_struct_sdna(srna, "MFloatProperty");
  RNA_def_struct_ui_text(srna, "Mesh Paint Mask Property", "Floating-point paint mask value");
  RNA_def_struct_path_func(srna, "rna_MeshPaintMask_path");

  prop = RNA_def_property(srna, "value", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "f");
  RNA_def_property_ui_text(prop, "Value", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");
}

static void rna_def_face_map(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "MeshFaceMapLayer", NULL);
  RNA_def_struct_ui_text(srna, "Mesh Face Map Layer", "Per-face map index");
  RNA_def_struct_sdna(srna, "CustomDataLayer");
  RNA_def_struct_path_func(srna, "rna_MeshFaceMapLayer_path");

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_struct_name_property(srna, prop);
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_MeshPolyLayer_name_set");
  RNA_def_property_ui_text(prop, "Name", "Name of face map layer");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  prop = RNA_def_property(srna, "data", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshFaceMap");
  RNA_def_property_ui_text(prop, "Data", "");
  RNA_def_property_collection_funcs(prop,
                                    "rna_MeshFaceMapLayer_data_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_MeshFaceMapLayer_data_length",
                                    NULL,
                                    NULL,
                                    NULL);

  /* FaceMap struct */
  srna = RNA_def_struct(brna, "MeshFaceMap", NULL);
  RNA_def_struct_sdna(srna, "MIntProperty");
  RNA_def_struct_ui_text(srna, "Int Property", "");
  RNA_def_struct_path_func(srna, "rna_MeshFaceMap_path");

  prop = RNA_def_property(srna, "value", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "i");
  RNA_def_property_ui_text(prop, "Value", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");
}

static void rna_def_face_maps(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  PropertyRNA *prop;

  RNA_def_property_srna(cprop, "MeshFaceMapLayers");
  srna = RNA_def_struct(brna, "MeshFaceMapLayers", NULL);
  RNA_def_struct_ui_text(srna, "Mesh Face Map Layer", "Per-face map index");
  RNA_def_struct_sdna(srna, "Mesh");
  RNA_def_struct_ui_text(srna, "Mesh Face Maps", "Collection of mesh face maps");

  /* add this since we only ever have one layer anyway, don't bother with active_index */
  prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshFaceMapLayer");
  RNA_def_property_pointer_funcs(prop, "rna_Mesh_face_map_active_get", NULL, NULL, NULL);
  RNA_def_property_ui_text(prop, "Active Face Map Layer", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  FunctionRNA *func;
  PropertyRNA *parm;

  func = RNA_def_function(srna, "new", "rna_Mesh_face_map_new");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  RNA_def_function_ui_description(func, "Add a float property layer to Mesh");
  RNA_def_string(func, "name", "Face Map", 0, "", "Face map name");
  parm = RNA_def_pointer(func, "layer", "MeshFaceMapLayer", "", "The newly created layer");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_Mesh_face_map_remove");
  RNA_def_function_ui_description(func, "Remove a face map layer");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "layer", "MeshFaceMapLayer", "", "The layer to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  RNA_def_property_clear_flag(parm, PROP_THICK_WRAP);
}

static void rna_def_mesh(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "Mesh", "ID");
  RNA_def_struct_ui_text(srna, "Mesh", "Mesh data-block defining geometric surfaces");
  RNA_def_struct_ui_icon(srna, ICON_MESH_DATA);

  prop = RNA_def_property(srna, "vertices", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "mvert", "totvert");
  RNA_def_property_struct_type(prop, "MeshVertex");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Vertices", "Vertices of the mesh");
  rna_def_mesh_vertices(brna, prop);

  prop = RNA_def_property(srna, "edges", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "medge", "totedge");
  RNA_def_property_struct_type(prop, "MeshEdge");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Edges", "Edges of the mesh");
  rna_def_mesh_edges(brna, prop);

  prop = RNA_def_property(srna, "loops", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "mloop", "totloop");
  RNA_def_property_struct_type(prop, "MeshLoop");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Loops", "Loops of the mesh (polygon corners)");
  rna_def_mesh_loops(brna, prop);

  prop = RNA_def_property(srna, "polygons", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "mpoly", "totpoly");
  RNA_def_property_struct_type(prop, "MeshPolygon");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Polygons", "Polygons of the mesh");
  rna_def_mesh_polygons(brna, prop);

  rna_def_normal_layer_value(brna);

  prop = RNA_def_property(srna, "vertex_normals", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshNormalValue");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop,
                           "Vertex Normals",
                           "The normal direction of each vertex, defined as the average of the "
                           "surrounding face normals");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_vertex_normals_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_Mesh_vertex_normals_length",
                                    NULL,
                                    NULL,
                                    NULL);

  prop = RNA_def_property(srna, "polygon_normals", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshNormalValue");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop,
                           "Polygon Normals",
                           "The normal direction of each polygon, defined by the winding order "
                           "and position of its vertices");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_poly_normals_begin",
                                    "rna_iterator_array_next",
                                    "rna_iterator_array_end",
                                    "rna_iterator_array_get",
                                    "rna_Mesh_poly_normals_length",
                                    NULL,
                                    NULL,
                                    NULL);

  prop = RNA_def_property(srna, "loop_triangles", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "runtime.looptris.array", "runtime.looptris.len");
  RNA_def_property_struct_type(prop, "MeshLoopTriangle");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Loop Triangles", "Tessellation of mesh polygons into triangles");
  rna_def_mesh_looptris(brna, prop);

  /* TODO: should this be allowed to be itself? */
  prop = RNA_def_property(srna, "texture_mesh", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "texcomesh");
  RNA_def_property_flag(prop, PROP_EDITABLE | PROP_ID_SELF_CHECK);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop,
      "Texture Mesh",
      "Use another mesh for texture indices (vertex indices must be aligned)");

  /* UV loop layers */
  prop = RNA_def_property(srna, "uv_layers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "ldata.layers", "ldata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_uv_layers_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_uv_layers_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshUVLoopLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "UV Loop Layers", "All UV loop layers");
  rna_def_uv_layers(brna, prop);

  prop = RNA_def_property(srna, "uv_layer_clone", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshUVLoopLayer");
  RNA_def_property_pointer_funcs(
      prop, "rna_Mesh_uv_layer_clone_get", "rna_Mesh_uv_layer_clone_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop, "Clone UV Loop Layer", "UV loop layer to be used as cloning source");

  prop = RNA_def_property(srna, "uv_layer_clone_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_funcs(prop,
                             "rna_Mesh_uv_layer_clone_index_get",
                             "rna_Mesh_uv_layer_clone_index_set",
                             "rna_Mesh_uv_layer_index_range");
  RNA_def_property_ui_text(prop, "Clone UV Loop Layer Index", "Clone UV loop layer index");

  prop = RNA_def_property(srna, "uv_layer_stencil", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshUVLoopLayer");
  RNA_def_property_pointer_funcs(
      prop, "rna_Mesh_uv_layer_stencil_get", "rna_Mesh_uv_layer_stencil_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Mask UV Loop Layer", "UV loop layer to mask the painted area");

  prop = RNA_def_property(srna, "uv_layer_stencil_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_funcs(prop,
                             "rna_Mesh_uv_layer_stencil_index_get",
                             "rna_Mesh_uv_layer_stencil_index_set",
                             "rna_Mesh_uv_layer_index_range");
  RNA_def_property_ui_text(prop, "Mask UV Loop Layer Index", "Mask UV loop layer index");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_data_legacy_deg_tag_all");

  /* Vertex colors */

  prop = RNA_def_property(srna, "vertex_colors", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "ldata.layers", "ldata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_vertex_colors_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_vertex_colors_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshLoopColorLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Vertex Colors", "All vertex colors");
  rna_def_loop_colors(brna, prop);

  /* Sculpt Vertex colors */

  prop = RNA_def_property(srna, "sculpt_vertex_colors", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "vdata.layers", "vdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_sculpt_vertex_colors_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_sculpt_vertex_colors_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshVertColorLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Sculpt Vertex Colors", "All vertex colors");
  rna_def_vert_colors(brna, prop);

  /* TODO: edge customdata layers (bmesh py api can access already). */
  prop = RNA_def_property(srna, "vertex_layers_float", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "vdata.layers", "vdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_vertex_float_layers_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_vertex_float_layers_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshVertexFloatPropertyLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Float Property Layers", "");
  rna_def_vertex_float_layers(brna, prop);

  prop = RNA_def_property(srna, "vertex_layers_int", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "vdata.layers", "vdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_vertex_int_layers_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_vertex_int_layers_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshVertexIntPropertyLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Int Property Layers", "");
  rna_def_vertex_int_layers(brna, prop);

  prop = RNA_def_property(srna, "vertex_layers_string", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "vdata.layers", "vdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_vertex_string_layers_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_vertex_string_layers_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshVertexStringPropertyLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "String Property Layers", "");
  rna_def_vertex_string_layers(brna, prop);

  prop = RNA_def_property(srna, "polygon_layers_float", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "pdata.layers", "pdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_polygon_float_layers_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_polygon_float_layers_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshPolygonFloatPropertyLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Float Property Layers", "");
  rna_def_polygon_float_layers(brna, prop);

  prop = RNA_def_property(srna, "polygon_layers_int", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "pdata.layers", "pdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_polygon_int_layers_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_polygon_int_layers_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshPolygonIntPropertyLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Int Property Layers", "");
  rna_def_polygon_int_layers(brna, prop);

  prop = RNA_def_property(srna, "polygon_layers_string", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "pdata.layers", "pdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_polygon_string_layers_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_polygon_string_layers_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshPolygonStringPropertyLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "String Property Layers", "");
  rna_def_polygon_string_layers(brna, prop);

  /* face-maps */
  prop = RNA_def_property(srna, "face_maps", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "pdata.layers", "pdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_face_maps_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_face_maps_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshFaceMapLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Face Map", "");
  rna_def_face_maps(brna, prop);

  /* Skin vertices */
  prop = RNA_def_property(srna, "skin_vertices", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "vdata.layers", "vdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_skin_vertices_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_skin_vertices_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshSkinVertexLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Skin Vertices", "All skin vertices");
  rna_def_skin_vertices(brna, prop);
  /* End skin vertices */

  /* Vertex Crease */
  prop = RNA_def_property(srna, "vertex_creases", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "MeshVertexCreaseLayer");
  RNA_def_property_collection_sdna(prop, NULL, "vdata.layers", "vdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_vertex_creases_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_vertex_creases_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Vertex Creases", "Sharpness of the vertices");
  rna_def_vertex_creases(brna);
  /* End vertex crease */

  /* Paint mask */
  prop = RNA_def_property(srna, "vertex_paint_masks", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "vdata.layers", "vdata.totlayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_Mesh_vertex_paint_masks_begin",
                                    NULL,
                                    NULL,
                                    NULL,
                                    "rna_Mesh_vertex_paint_masks_length",
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "MeshPaintMaskLayer");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_IGNORE);
  RNA_def_property_ui_text(prop, "Vertex Paint Mask", "Vertex paint mask");
  rna_def_paint_mask(brna, prop);
  /* End paint mask */

  /* Attributes */
  rna_def_attributes_common(srna);

  /* Remesh */
  prop = RNA_def_property(srna, "remesh_voxel_size", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "remesh_voxel_size");
  RNA_def_property_range(prop, 0.0001f, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0001f, FLT_MAX, 0.01, 4);
  RNA_def_property_ui_text(prop,
                           "Voxel Size",
                           "Size of the voxel in object space used for volume evaluation. Lower "
                           "values preserve finer details");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "remesh_voxel_adaptivity", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "remesh_voxel_adaptivity");
  RNA_def_property_range(prop, 0.0f, 1.0f);
  RNA_def_property_ui_range(prop, 0.0f, 1.0f, 0.01, 4);
  RNA_def_property_ui_text(
      prop,
      "Adaptivity",
      "Reduces the final face count by simplifying geometry where detail is not needed, "
      "generating triangles. A value greater than 0 disables Fix Poles");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "use_remesh_fix_poles", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_REMESH_FIX_POLES);
  RNA_def_property_ui_text(prop, "Fix Poles", "Produces less poles and a better topology flow");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "use_remesh_preserve_volume", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_REMESH_REPROJECT_VOLUME);
  RNA_def_property_ui_text(
      prop,
      "Preserve Volume",
      "Projects the mesh to preserve the volume and details of the original mesh");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "use_remesh_preserve_paint_mask", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_REMESH_REPROJECT_PAINT_MASK);
  RNA_def_property_boolean_default(prop, false);
  RNA_def_property_ui_text(prop, "Preserve Paint Mask", "Keep the current mask on the new mesh");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "use_remesh_preserve_sculpt_face_sets", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_REMESH_REPROJECT_SCULPT_FACE_SETS);
  RNA_def_property_boolean_default(prop, false);
  RNA_def_property_ui_text(
      prop, "Preserve Face Sets", "Keep the current Face Sets on the new mesh");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "use_remesh_preserve_vertex_colors", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_REMESH_REPROJECT_VERTEX_COLORS);
  RNA_def_property_boolean_default(prop, false);
  RNA_def_property_ui_text(
      prop, "Preserve Vertex Colors", "Keep the current vertex colors on the new mesh");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "remesh_mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "remesh_mode");
  RNA_def_property_enum_items(prop, rna_enum_mesh_remesh_mode_items);
  RNA_def_property_ui_text(prop, "Remesh Mode", "");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  /* End remesh */

  /* Symmetry */
  prop = RNA_def_property(srna, "use_mirror_x", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "symmetry", ME_SYMMETRY_X);
  RNA_def_property_ui_text(prop, "X", "Enable symmetry in the X axis");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "use_mirror_y", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "symmetry", ME_SYMMETRY_Y);
  RNA_def_property_ui_text(prop, "Y", "Enable symmetry in the Y axis");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "use_mirror_z", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "symmetry", ME_SYMMETRY_Z);
  RNA_def_property_ui_text(prop, "Z", "Enable symmetry in the Z axis");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");

  prop = RNA_def_property(srna, "use_mirror_vertex_groups", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "editflag", ME_EDIT_MIRROR_VERTEX_GROUPS);
  RNA_def_property_ui_text(prop,
                           "Mirror Vertex Groups",
                           "Mirror the left/right vertex groups when painting. The symmetry axis "
                           "is determined by the symmetry settings");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");
  /* End Symmetry */

  prop = RNA_def_property(srna, "use_auto_smooth", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", ME_AUTOSMOOTH);
  RNA_def_property_ui_text(
      prop,
      "Auto Smooth",
      "Auto smooth (based on smooth/sharp faces/edges and angle between faces), "
      "or use custom split normals data if available");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_geom_and_params");

  prop = RNA_def_property(srna, "auto_smooth_angle", PROP_FLOAT, PROP_ANGLE);
  RNA_def_property_float_sdna(prop, NULL, "smoothresh");
  RNA_def_property_range(prop, 0.0f, DEG2RADF(180.0f));
  RNA_def_property_ui_text(prop,
                           "Auto Smooth Angle",
                           "Maximum angle between face normals that will be considered as smooth "
                           "(unused if custom split normals data are available)");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_geom_and_params");

  RNA_define_verify_sdna(false);
  prop = RNA_def_property(srna, "has_custom_normals", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "", 0);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop, "Has Custom Normals", "True if there are custom split normals data in this mesh");
  RNA_def_property_boolean_funcs(prop, "rna_Mesh_has_custom_normals_get", NULL);
  RNA_define_verify_sdna(true);

  prop = RNA_def_property(srna, "texco_mesh", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "texcomesh");
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(
      prop, "Texture Space Mesh", "Derive texture coordinates from another mesh");

  prop = RNA_def_property(srna, "shape_keys", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "key");
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_clear_flag(prop, PROP_PTR_NO_OWNERSHIP);
  RNA_def_property_ui_text(prop, "Shape Keys", "");

  /* texture space */
  prop = RNA_def_property(srna, "use_auto_texspace", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "texflag", ME_AUTOSPACE);
  RNA_def_property_ui_text(
      prop,
      "Auto Texture Space",
      "Adjust active object's texture space automatically when transforming object");
  RNA_def_property_update(prop, 0, "rna_Mesh_update_geom_and_params");

#  if 0
  prop = RNA_def_property(srna, "texspace_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_ui_text(prop, "Texture Space Location", "Texture space location");
  RNA_def_property_editable_func(prop, "rna_Mesh_texspace_editable");
  RNA_def_property_float_funcs(
      prop, "rna_Mesh_texspace_loc_get", "rna_Mesh_texspace_loc_set", NULL);
  RNA_def_property_update(prop, 0, "rna_Mesh_update_draw");
#  endif

  /* editflag */
  prop = RNA_def_property(srna, "use_mirror_topology", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "editflag", ME_EDIT_MIRROR_TOPO);
  RNA_def_property_ui_text(prop,
                           "Topology Mirror",
                           "Use topology based mirroring "
                           "(for when both sides of mesh have matching, unique topology)");

  prop = RNA_def_property(srna, "use_paint_mask", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "editflag", ME_EDIT_PAINT_FACE_SEL);
  RNA_def_property_ui_text(prop, "Paint Mask", "Face selection masking for painting");
  RNA_def_property_ui_icon(prop, ICON_FACESEL, 0);
  RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_Mesh_update_facemask");

  prop = RNA_def_property(srna, "use_paint_mask_vertex", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "editflag", ME_EDIT_PAINT_VERT_SEL);
  RNA_def_property_ui_text(prop, "Vertex Selection", "Vertex selection masking for painting");
  RNA_def_property_ui_icon(prop, ICON_VERTEXSEL, 0);
  RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, "rna_Mesh_update_vertmask");

  /* customdata flags */
  prop = RNA_def_property(srna, "use_customdata_vertex_bevel", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "cd_flag", ME_CDFLAG_VERT_BWEIGHT);
  RNA_def_property_ui_text(prop, "Store Vertex Bevel Weight", "");

  prop = RNA_def_property(srna, "use_customdata_edge_bevel", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "cd_flag", ME_CDFLAG_EDGE_BWEIGHT);
  RNA_def_property_ui_text(prop, "Store Edge Bevel Weight", "");

  prop = RNA_def_property(srna, "use_customdata_vertex_crease", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "cd_flag", ME_CDFLAG_VERT_CREASE);
  RNA_def_property_ui_text(prop, "Store Vertex Crease", "");

  prop = RNA_def_property(srna, "use_customdata_edge_crease", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "cd_flag", ME_CDFLAG_EDGE_CREASE);
  RNA_def_property_ui_text(prop, "Store Edge Crease", "");

  /* readonly editmesh info - use for extrude menu */
  prop = RNA_def_property(srna, "total_vert_sel", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_funcs(prop, "rna_Mesh_tot_vert_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Selected Vertex Total", "Selected vertex count in editmode");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "total_edge_sel", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_funcs(prop, "rna_Mesh_tot_edge_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Selected Edge Total", "Selected edge count in editmode");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "total_face_sel", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_funcs(prop, "rna_Mesh_tot_face_get", NULL, NULL);
  RNA_def_property_ui_text(prop, "Selected Face Total", "Selected face count in editmode");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "is_editmode", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_Mesh_is_editmode_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Is Editmode", "True when used in editmode");

  /* pointers */
  rna_def_animdata_common(srna);
  rna_def_texmat_common(srna, "rna_Mesh_texspace_editable");

  RNA_api_mesh(srna);
}

void RNA_def_mesh(BlenderRNA *brna)
{
  rna_def_mesh(brna);
  rna_def_mvert(brna);
  rna_def_mvert_group(brna);
  rna_def_medge(brna);
  rna_def_mlooptri(brna);
  rna_def_mloop(brna);
  rna_def_mpolygon(brna);
  rna_def_mloopuv(brna);
  rna_def_mloopcol(brna);
  rna_def_MPropCol(brna);
  rna_def_mproperties(brna);
  rna_def_face_map(brna);
}

#endif
