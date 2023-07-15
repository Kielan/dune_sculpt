#include <stdlib.h>

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_pointcloud.h"

#include "lib_math_base.h"
#include "lib_string.h"

#ifdef API_RUNTIME

#  include "lib_math_vector.h"

#  include "dune_pointcloud.h"

#  include "graph.h"

#  include "wm_api.h"
#  include "wm_types.h"

static PointCloud *api_pointcloud(ApiPtr *ptr)
{
  return (PointCloud *)ptr->owner_id;
}

static int api_Point_index_get(ApiPtr *ptr)
{
  const PointCloud *pointcloud = api_pointcloud(ptr);
  const float(*co)[3] = ptr->data;
  return (int)(co - pointcloud->co);
}

static void api_Point_location_get(ApiPtr *ptr, float value[3])
{
  copy_v3_v3(value, (const float *)ptr->data);
}

static void api_Point_location_set(ApiPtr *ptr, const float value[3])
{
  copy_v3_v3((float *)ptr->data, value);
}

static float api_Point_radius_get(ApiPtr *ptr)
{
  const PointCloud *pointcloud = api_pointcloud(ptr);
  if (pointcloud->radius == NULL) {
    return 0.0f;
  }
  const float(*co)[3] = ptr->data;
  return pointcloud->radius[co - pointcloud->co];
}

static void api_Point_radius_set(ApiPtr *ptr, float value)
{
  const PointCloud *pointcloud = api_pointcloud(ptr);
  if (pointcloud->radius == NULL) {
    return;
  }
  const float(*co)[3] = ptr->data;
  pointcloud->radius[co - pointcloud->co] = value;
}

static char *api_Point_path(ApiPtr *ptr)
{
  return lib_sprintfn("points[%d]", api_Point_index_get(ptr));
}

static void api_PointCloud_update_data(struct Main *UNUSED(main),
                                       struct Scene *UNUSED(scene),
                                       ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  /* cheating way for importers to avoid slow updates */
  if (id->us > 0) {
    graph_id_tag_update(id, 0);
    wm_main_add_notifier(NC_GEOM | ND_DATA, id);
  }
}

#else

static void api_def_point(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Point", NULL);
  api_def_struct_ui_text(sapi, "Point", "Point in a point cloud");
  api_def_struct_path_fn(sapi, "api_Point_path");

  prop = api_def_prop(sapi, "co", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_array(prop, 3);
  api_def_prop_float_fns(prop, "api_Point_location_get", "api_Point_location_set", NULL);
  api_def_prop_ui_text(prop, "Location", "");
  api_def_prop_update(prop, 0, "api_PointCloud_update_data");

  prop = api_def_prop(sapi, "radius", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_fns(prop, "api_Point_radius_get", "api_Point_radius_set", NULL);
  api_def_prop_ui_text(prop, "Radius", "");
  api_def_prop_update(prop, 0, "api_PointCloud_update_data");

  prop = api_def_prop(sapi, "index", PROP_INT, PROP_UNSIGNED);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_int_fns(prop, "api_Point_index_get", NULL, NULL);
  api_def_prop_ui_text(prop, "Index", "Index of this points");
}

static void api_def_pointcloud(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  srna = apu_def_struct(dapi, "PointCloud", "Id");
  api_def_struct_ui_text(sapi, "Point Cloud", "Point cloud data-block");
  api_def_struct_ui_icon(sapi, ICON_POINTCLOUD_DATA);

  /* geometry */
  /* TODO: better solution for (*co)[3] parsing issue. */
  api_define_verify_stype(0);
  prop = api_def_prop(sapi, "points", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "co", "totpoint");
  api_def_prop_struct_type(prop, "Point");
  api_def_prop_ui_text(prop, "Points", "");
  api_define_verify_stype(1);

  /* materials */
  prop = api_def_prop(sapi, "materials", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "mat", "totcol");
  api_def_prop_struct_type(prop, "Material");
  api_def_prop_ui_text(prop, "Materials", "");
  api_def_prop_sapi(prop, "IdMaterials"); /* see api_id.c */
  api_def_prop_collection_fns(
      prop, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "api_IdMaterials_assign_int");

  api_def_attributes_common(sapi);

  /* common */
  api_def_animdata_common(sapi);
}

void api_def_pointcloud(DuneApi *dapi)
{
  api_def_point(dapi);
  api_def_pointcloud(dapi);
}

#endif
