#include "MEM_guardedalloc.h"

#include "TYPES_defaults.h"
#include "TYPES_material.h"
#include "TYPES_object.h"
#include "TYPES_pointcloud.h"

#include "LIB_bounds.hh"
#include "LIB_index_range.hh"
#include "LIB_listbase.h"
#include "LIB_math_vec_types.hh"
#include "LIB_rand.h"
#include "LIB_span.hh"
#include "LIB_string.h"
#include "LIB_task.hh"
#include "LIB_utildefines.h"

#include "DUNE_anim_data.h"
#include "DUNE_customdata.h"
#include "DUNE_geometry_set.hh"
#include "DUNE_global.h"
#include "DUNE_idtype.h"
#include "DUNE_lib_id.h"
#include "DUNE_lib_query.h"
#include "DUNE_lib_remap.h"
#include "DUNE_main.h"
#include "DUNE_mesh_wrapper.h"
#include "DUNE_modifier.h"
#include "DUNE_object.h"
#include "DUNE_pointcloud.h"

#include "LANG_translation.h"

#include "DEG_depsgraph_query.h"

#include "LOADER_read_write.h"

using dune::float3;
using dune::IndexRange;
using dune::Span;

/* PointCloud datablock */

static void pointcloud_random(PointCloud *pointcloud);

const char *POINTCLOUD_ATTR_POSITION = "position";
const char *POINTCLOUD_ATTR_RADIUS = "radius";

static void pointcloud_init_data(ID *id)
{
  PointCloud *pointcloud = (PointCloud *)id;
  LIB_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(pointcloud, id));

  MEMCPY_STRUCT_AFTER(pointcloud, TYPES_struct_default_get(PointCloud), id);

  CustomData_reset(&pointcloud->pdata);
  CustomData_add_layer_named(&pointcloud->pdata,
                             CD_PROP_FLOAT3,
                             CD_CALLOC,
                             nullptr,
                             pointcloud->totpoint,
                             POINTCLOUD_ATTR_POSITION);
  DUNE_pointcloud_update_customdata_pointers(pointcloud);
}

static void pointcloud_copy_data(Main *UNUSED(duneMain), ID *id_dst, const ID *id_src, const int flag)
{
  PointCloud *pointcloud_dst = (PointCloud *)id_dst;
  const PointCloud *pointcloud_src = (const PointCloud *)id_src;
  pointcloud_dst->mat = static_cast<Material **>(MEM_dupallocN(pointcloud_src->mat));

  const eCDAllocType alloc_type = (flag & LIB_ID_COPY_CD_REFERENCE) ? CD_REFERENCE : CD_DUPLICATE;
  CustomData_copy(&pointcloud_src->pdata,
                  &pointcloud_dst->pdata,
                  CD_MASK_ALL,
                  alloc_type,
                  pointcloud_dst->totpoint);
  DUNE_pointcloud_update_customdata_pointers(pointcloud_dst);

  pointcloud_dst->batch_cache = nullptr;
}

static void pointcloud_free_data(ID *id)
{
  PointCloud *pointcloud = (PointCloud *)id;
  DUNE_animdata_free(&pointcloud->id, false);
  DUNE_pointcloud_batch_cache_free(pointcloud);
  CustomData_free(&pointcloud->pdata, pointcloud->totpoint);
  MEM_SAFE_FREE(pointcloud->mat);
}

static void pointcloud_foreach_id(ID *id, LibraryForeachIDData *data)
{
  PointCloud *pointcloud = (PointCloud *)id;
  for (int i = 0; i < pointcloud->totcol; i++) {
    DUNE_LIB_FOREACHID_PROCESS_IDSUPER(data, pointcloud->mat[i], IDWALK_CB_USER);
  }
}

static void dune_pointcloud_write(DuneWriter *writer, ID *id, const void *id_address)
{
  PointCloud *pointcloud = (PointCloud *)id;

  CustomDataLayer *players = nullptr, players_buff[CD_TEMP_CHUNK_SIZE];
  CustomData_write_prepare(
      &pointcloud->pdata, &players, players_buff, ARRAY_SIZE(players_buff));

  /* Write LibData */
  LOADER_write_id_struct(writer, PointCloud, id_address, &pointcloud->id);
  DUNE_id_write(writer, &pointcloud->id);

  /* Direct data */
  CustomData_write(
      writer, &pointcloud->pdata, players, pointcloud->totpoint, CD_MASK_ALL, &pointcloud->id);

  LOADER_write_pointer_array(writer, pointcloud->totcol, pointcloud->mat);
  if (pointcloud->adt) {
    DUNE_animdata_write(writer, pointcloud->adt);
  }

  /* Remove temporary data. */
  if (players && players != players_buff) {
    MEM_freeN(players);
  }
}

static void pointcloud_read_data(DuneDataReader *reader, ID *id)
{
  PointCloud *pointcloud = (PointCloud *)id;
  LOADER_read_data_address(reader, &pointcloud->adt);
  DUNE_animdata_read_data(reader, pointcloud->adt);

  /* Geometry */
  CustomData_read(reader, &pointcloud->pdata, pointcloud->totpoint);
  DUNE_pointcloud_update_customdata_pointers(pointcloud);

  /* Materials */
  LOADER_read_pointer_array(reader, (void **)&pointcloud->mat);
}

static void pointcloud_read_lib(DuneLibReader *reader, ID *id)
{
  PointCloud *pointcloud = (PointCloud *)id;
  for (int a = 0; a < pointcloud->totcol; a++) {
    LOADER_read_id_address(reader, pointcloud->id.lib, &pointcloud->mat[a]);
  }
}

static void pointcloud_read_expand(DuneExpander *expander, ID *id)
{
  PointCloud *pointcloud = (PointCloud *)id;
  for (int a = 0; a < pointcloud->totcol; a++) {
    LOADER_expand(expander, pointcloud->mat[a]);
  }
}

IDTypeInfo IDType_ID_PT = {
    /* id_code */ ID_PT,
    /* id_filter */ FILTER_ID_PT,
    /* main_listbase_index */ INDEX_ID_PT,
    /* struct_size */ sizeof(PointCloud),
    /* name */ "PointCloud",
    /* name_plural */ "pointclouds",
    /* translation_context */ LANG_I18NCONTEXT_ID_POINTCLOUD,
    /* flags */ IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    /* asset_type_info */ nullptr,

    /* init_data */ pointcloud_init_data,
    /* copy_data */ pointcloud_copy_data,
    /* free_data */ pointcloud_free_data,
    /* make_local */ nullptr,
    /* foreach_id */ pointcloud_foreach_id,
    /* foreach_cache */ nullptr,
    /* foreach_path */ nullptr,
    /* owner_get */ nullptr,

    /* dune_write */ pointcloud_write,
    /* dune_read_data */ pointcloud_read_data,
    /* dune_read_lib */ pointcloud_read_lib,
    /* dune_read_expand */ pointcloud_read_expand,

    /* dune_read_undo_preserve */ nullptr,

    /* lib_override_apply_post */ nullptr,
};

static void pointcloud_random(PointCloud *pointcloud)
{
  pointcloud->totpoint = 400;
  CustomData_realloc(&pointcloud->pdata, pointcloud->totpoint);
  DUNE_pointcloud_update_customdata_pointers(pointcloud);

  RNG *rng = LIB_rng_new(0);

  for (int i = 0; i < pointcloud->totpoint; i++) {
    pointcloud->co[i][0] = 2.0f * LIB_rng_get_float(rng) - 1.0f;
    pointcloud->co[i][1] = 2.0f * LIB_rng_get_float(rng) - 1.0f;
    pointcloud->co[i][2] = 2.0f * LIB_rng_get_float(rng) - 1.0f;
    pointcloud->radius[i] = 0.05f * LIB_rng_get_float(rng);
  }

  LIB_rng_free(rng);
}

void *DUNE_pointcloud_add(Main *duneMain, const char *name)
{
  PointCloud *pointcloud = static_cast<PointCloud *>(DUNE_id_new(duneMain, ID_PT, name));

  return pointcloud;
}

void *DUNE_pointcloud_add_default(Main *duneMain, const char *name)
{
  PointCloud *pointcloud = static_cast<PointCloud *>(DUNE_libblock_alloc(duneMain, ID_PT, name, 0));

  pointcloud_init_data(&pointcloud->id);

  CustomData_add_layer_named(&pointcloud->pdata,
                             CD_PROP_FLOAT,
                             CD_CALLOC,
                             nullptr,
                             pointcloud->totpoint,
                             POINTCLOUD_ATTR_RADIUS);
  pointcloud_random(pointcloud);

  return pointcloud;
}

PointCloud *DUNE_pointcloud_new_nomain(const int totpoint)
{
  PointCloud *pointcloud = static_cast<PointCloud *>(DUNE_libblock_alloc(
      nullptr, ID_PT, DUNE_idtype_idcode_to_name(ID_PT), LIB_ID_CREATE_LOCALIZE));

  pointcloud_init_data(&pointcloud->id);

  pointcloud->totpoint = totpoint;

  CustomData_add_layer_named(&pointcloud->pdata,
                             CD_PROP_FLOAT,
                             CD_CALLOC,
                             nullptr,
                             pointcloud->totpoint,
                             POINTCLOUD_ATTR_RADIUS);

  pointcloud->totpoint = totpoint;
  CustomData_realloc(&pointcloud->pdata, pointcloud->totpoint);
  DUNE_pointcloud_update_customdata_pointers(pointcloud);

  return pointcloud;
}

static std::optional<dune::bounds::MinMaxResult<float3>> point_cloud_bounds(
    const PointCloud &pointcloud)
{
  Span<float3> positions{reinterpret_cast<float3 *>(pointcloud.co), pointcloud.totpoint};
  if (pointcloud.radius) {
    Span<float> radii{pointcloud.radius, pointcloud.totpoint};
    return dune::bounds::min_max_with_radii(positions, radii);
  }
  return dune::bounds::min_max(positions);
}

bool DUNE_pointcloud_minmax(const PointCloud *pointcloud, float r_min[3], float r_max[3])
{
  using namespace dune;

  const std::optional<bounds::MinMaxResult<float3>> min_max = point_cloud_bounds(*pointcloud);
  if (!min_max) {
    return false;
  }

  copy_v3_v3(r_min, math::min(min_max->min, float3(r_min)));
  copy_v3_v3(r_max, math::max(min_max->max, float3(r_max)));

  return true;
}

BoundBox *DUNE_pointcloud_boundbox_get(Object *ob)
{
  LIB_assert(ob->type == OB_POINTCLOUD);

  if (ob->runtime.bb != nullptr && (ob->runtime.bb->flag & BOUNDBOX_DIRTY) == 0) {
    return ob->runtime.bb;
  }

  if (ob->runtime.bb == nullptr) {
    ob->runtime.bb = static_cast<BoundBox *>(MEM_callocN(sizeof(BoundBox), "pointcloud boundbox"));
  }

  float3 min, max;
  INIT_MINMAX(min, max);
  if (ob->runtime.geometry_set_eval != nullptr) {
    ob->runtime.geometry_set_eval->compute_boundbox_without_instances(&min, &max);
  }
  else {
    const PointCloud *pointcloud = static_cast<PointCloud *>(ob->data);
    DUNE_pointcloud_minmax(pointcloud, min, max);
  }
  DUNE_boundbox_init_from_minmax(ob->runtime.bb, min, max);

  return ob->runtime.bb;
}

void DUNE_pointcloud_update_customdata_pointers(PointCloud *pointcloud)
{
  pointcloud->co = static_cast<float(*)[3]>(
      CustomData_get_layer_named(&pointcloud->pdata, CD_PROP_FLOAT3, POINTCLOUD_ATTR_POSITION));
  pointcloud->radius = static_cast<float *>(
      CustomData_get_layer_named(&pointcloud->pdata, CD_PROP_FLOAT, POINTCLOUD_ATTR_RADIUS));
}

bool DUNE_pointcloud_customdata_required(PointCloud *UNUSED(pointcloud), CustomDataLayer *layer)
{
  return layer->type == CD_PROP_FLOAT3 && STREQ(layer->name, POINTCLOUD_ATTR_POSITION);
}

/* Dependency Graph */

PointCloud *DUNE_pointcloud_new_for_eval(const PointCloud *pointcloud_src, int totpoint)
{
  PointCloud *pointcloud_dst = static_cast<PointCloud *>(DUNE_id_new_nomain(ID_PT, nullptr));
  CustomData_free(&pointcloud_dst->pdata, pointcloud_dst->totpoint);

  STRNCPY(pointcloud_dst->id.name, pointcloud_src->id.name);
  pointcloud_dst->mat = static_cast<Material **>(MEM_dupallocN(pointcloud_src->mat));
  pointcloud_dst->totcol = pointcloud_src->totcol;

  pointcloud_dst->totpoint = totpoint;
  CustomData_copy(
      &pointcloud_src->pdata, &pointcloud_dst->pdata, CD_MASK_ALL, CD_CALLOC, totpoint);
  DUNE_pointcloud_update_customdata_pointers(pointcloud_dst);

  return pointcloud_dst;
}

PointCloud *DUNE_pointcloud_copy_for_eval(struct PointCloud *pointcloud_src, bool reference)
{
  int flags = LIB_ID_COPY_LOCALIZE;

  if (reference) {
    flags |= LIB_ID_COPY_CD_REFERENCE;
  }

  PointCloud *result = (PointCloud *)DUNE_id_copy_ex(nullptr, &pointcloud_src->id, nullptr, flags);
  return result;
}

static void pointcloud_evaluate_modifiers(struct Depsgraph *depsgraph,
                                          struct Scene *scene,
                                          Object *object,
                                          GeometrySet &geometry_set)
{
  /* Modifier evaluation modes. */
  const bool use_render = (DEG_get_mode(depsgraph) == DAG_EVAL_RENDER);
  const int required_mode = use_render ? eModifierMode_Render : eModifierMode_Realtime;
  ModifierApplyFlag apply_flag = use_render ? MOD_APPLY_RENDER : MOD_APPLY_USECACHE;
  const ModifierEvalContext mectx = {depsgraph, object, apply_flag};

  /* Get effective list of modifiers to execute. Some effects like shape keys
   * are added as virtual modifiers before the user created modifiers. */
  VirtualModifierData virtualModifierData;
  ModifierData *md = DUNE_modifiers_get_virtual_modifierlist(object, &virtualModifierData);

  /* Evaluate modifiers. */
  for (; md; md = md->next) {
    const ModifierTypeInfo *mti = DUNE_modifier_get_info((ModifierType)md->type);

    if (!DUNE_modifier_is_enabled(scene, md, required_mode)) {
      continue;
    }

    if (mti->modifyGeometrySet) {
      mti->modifyGeometrySet(md, &mectx, &geometry_set);
    }
  }
}

static PointCloud *take_pointcloud_ownership_from_geometry_set(GeometrySet &geometry_set)
{
  if (!geometry_set.has<PointCloudComponent>()) {
    return nullptr;
  }
  PointCloudComponent &pointcloud_component =
      geometry_set.get_component_for_write<PointCloudComponent>();
  PointCloud *pointcloud = pointcloud_component.release();
  if (pointcloud != nullptr) {
    /* Add back, but as read-only non-owning component. */
    pointcloud_component.replace(pointcloud, GeometryOwnershipType::ReadOnly);
  }
  else {
    /* The component was empty, we can also remove it. */
    geometry_set.remove<PointCloudComponent>();
  }
  return pointcloud;
}

void DUNE_pointcloud_data_update(struct Depsgraph *depsgraph, struct Scene *scene, Object *object)
{
  /* Free any evaluated data and restore original data. */
  DUNE_object_free_derived_caches(object);

  /* Evaluate modifiers. */
  PointCloud *pointcloud = static_cast<PointCloud *>(object->data);
  GeometrySet geometry_set = GeometrySet::create_with_pointcloud(pointcloud,
                                                                 GeometryOwnershipType::ReadOnly);
  pointcloud_evaluate_modifiers(depsgraph, scene, object, geometry_set);

  PointCloud *pointcloud_eval = take_pointcloud_ownership_from_geometry_set(geometry_set);

  /* If the geometry set did not contain a point cloud, we still create an empty one. */
  if (pointcloud_eval == nullptr) {
    pointcloud_eval = DUNE_pointcloud_new_nomain(0);
  }

  /* Assign evaluated object. */
  const bool eval_is_owned = pointcloud_eval != pointcloud;
  DUNE_object_eval_assign_data(object, &pointcloud_eval->id, eval_is_owned);
  object->runtime.geometry_set_eval = new GeometrySet(std::move(geometry_set));
}

/* Draw Cache */

void (*DUNE_pointcloud_batch_cache_dirty_tag_cb)(PointCloud *pointcloud, int mode) = nullptr;
void (*DUNE_pointcloud_batch_cache_free_cb)(PointCloud *pointcloud) = nullptr;

void DUNE_pointcloud_batch_cache_dirty_tag(PointCloud *pointcloud, int mode)
{
  if (pointcloud->batch_cache) {
    DUNE_pointcloud_batch_cache_dirty_tag_cb(pointcloud, mode);
  }
}

void DUNE_pointcloud_batch_cache_free(PointCloud *pointcloud)
{
  if (pointcloud->batch_cache) {
    DUNE_pointcloud_batch_cache_free_cb(pointcloud);
  }
}
