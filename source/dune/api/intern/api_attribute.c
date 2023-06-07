#include <stdlib.h>

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "types_curves.h"
#include "types_customdata.h"
#include "types_mesh.h"
#include "types_meshdata.h"
#include "types_pointcloud.h"

#include "dune_attribute.h"
#include "dune_customdata.h"

#include "wm_types.h"

const EnumPropItem api_enum_attribute_type_items[] = {
    {CD_PROP_FLOAT, "FLOAT", 0, "Float", "Floating-point value"},
    {CD_PROP_INT32, "INT", 0, "Integer", "32-bit integer"},
    {CD_PROP_FLOAT3, "FLOAT_VECTOR", 0, "Vector", "3D vector with floating-point values"},
    {CD_PROP_COLOR, "FLOAT_COLOR", 0, "Color", "RGBA color with floating-point values"},
    {CD_MLOOPCOL, "BYTE_COLOR", 0, "Byte Color", "RGBA color with 8-bit values"},
    {CD_PROP_STRING, "STRING", 0, "String", "Text string"},
    {CD_PROP_BOOL, "BOOLEAN", 0, "Boolean", "True or false"},
    {CD_PROP_FLOAT2, "FLOAT2", 0, "2D Vector", "2D vector with floating-point values"},
    {CD_PROP_INT8, "INT8", 0, "8-Bit Integer", "Smaller integer with a range from -128 to 127"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_attribute_type_with_auto_items[] = {
    {CD_AUTO_FROM_NAME, "AUTO", 0, "Auto", ""},
    {CD_PROP_FLOAT, "FLOAT", 0, "Float", "Floating-point value"},
    {CD_PROP_INT32, "INT", 0, "Integer", "32-bit integer"},
    {CD_PROP_FLOAT3, "FLOAT_VECTOR", 0, "Vector", "3D vector with floating-point values"},
    {CD_PROP_COLOR, "FLOAT_COLOR", 0, "Color", "RGBA color with floating-point values"},
    {CD_MLOOPCOL, "BYTE_COLOR", 0, "Byte Color", "RGBA color with 8-bit values"},
    {CD_PROP_STRING, "STRING", 0, "String", "Text string"},
    {CD_PROP_BOOL, "BOOLEAN", 0, "Boolean", "True or false"},
    {CD_PROP_FLOAT2, "FLOAT2", 0, "2D Vector", "2D vector with floating-point values"},
    {CD_PROP_INT8, "INT8", 0, "8-Bit Integer", "Smaller integer with a range from -128 to 127"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_attribute_domain_items[] = {
    /* Not implement yet */
    // {ATTR_DOMAIN_GEOMETRY, "GEOMETRY", 0, "Geometry", "Attribute on (whole) geometry"},
    {ATTR_DOMAIN_POINT, "POINT", 0, "Point", "Attribute on point"},
    {ATTR_DOMAIN_EDGE, "EDGE", 0, "Edge", "Attribute on mesh edge"},
    {ATTR_DOMAIN_FACE, "FACE", 0, "Face", "Attribute on mesh faces"},
    {ATTR_DOMAIN_CORNER, "CORNER", 0, "Face Corner", "Attribute on mesh face corner"},
    /* Not implement yet */
    // {ATTR_DOMAIN_GRIDS, "GRIDS", 0, "Grids", "Attribute on mesh multires grids"},
    {ATTR_DOMAIN_CURVE, "CURVE", 0, "Spline", "Attribute on spline"},
    {ATTR_DOMAIN_INSTANCE, "INSTANCE", 0, "Instance", "Attribute on instance"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_attribute_domain_without_corner_items[] = {
    {ATTR_DOMAIN_POINT, "POINT", 0, "Point", "Attribute on point"},
    {ATTR_DOMAIN_EDGE, "EDGE", 0, "Edge", "Attribute on mesh edge"},
    {ATTR_DOMAIN_FACE, "FACE", 0, "Face", "Attribute on mesh faces"},
    {ATTR_DOMAIN_CURVE, "CURVE", 0, "Spline", "Attribute on spline"},
    {ATTR_DOMAIN_INSTANCE, "INSTANCE", 0, "Instance", "Attribute on instance"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_attribute_domain_with_auto_items[] = {
    {ATTR_DOMAIN_AUTO, "AUTO", 0, "Auto", ""},
    {ATTR_DOMAIN_POINT, "POINT", 0, "Point", "Attribute on point"},
    {ATTR_DOMAIN_EDGE, "EDGE", 0, "Edge", "Attribute on mesh edge"},
    {ATTR_DOMAIN_FACE, "FACE", 0, "Face", "Attribute on mesh faces"},
    {ATTR_DOMAIN_CORNER, "CORNER", 0, "Face Corner", "Attribute on mesh face corner"},
    {ATTR_DOMAIN_CURVE, "CURVE", 0, "Spline", "Attribute on spline"},
    {ATTR_DOMAIN_INSTANCE, "INSTANCE", 0, "Instance", "Attribute on instance"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "lib_math.h"

#  include "graph.h"

#  include "lang.h"

#  include "wm_api.h"

/* Attribute */

static char *api_Attribute_path(ApiPtr *ptr)
{
  CustomDataLayer *layer = ptr->data;
  return lib_sprintfn("attributes['%s']", layer->name);
}

static ApiStruct *sapi_by_custom_data_layer_type(const CustomDataType type)

  switch (type) {
    case CD_PROP_FLOAT:
      return &ApiFloatAttribute;
    case CD_PROP_INT32:
      return &ApiIntAttribute;
    case CD_PROP_FLOAT3:
      return &ApiFloatVectorAttribute;
    case CD_PROP_COLOR:
      return &ApiFloatColorAttribute;
    case CD_MLOOPCOL:
      return &ApiByteColorAttribute;
    case CD_PROP_STRING:
      return &ApiStringAttribute;
    case CD_PROP_BOOL:
      return &ApiBoolAttribute;
    case CD_PROP_FLOAT2:
      return &ApiFloat2Attribute;
    case CD_PROP_INT8:
      return &ApiByteIntAttribute;
    default:
      return NULL;
  }
}

static ApiStruct *api_Attribute_refine(ApiPtr *ptr)
{
  CustomDataLayer *layer = ptr->data;
  return sapi_by_custom_data_layer_type(layer->type);
}

static void api_Attribute_name_set(ApiPtr *ptr, const char *value)
{
  dune_id_attribute_rename(ptr->owner_id, ptr->data, value, NULL);
}

static int api_Attribute_name_editable(ApiPtr *ptr, const char **r_info)
{
  CustomDataLayer *layer = ptr->data;
  if (dune_id_attribute_required(ptr->owner_id, layer)) {
    *r_info = N_("Can't modify name of required geometry attribute");
    return false;
  }

  return true;
}

static int api_Attribute_type_get(ApiPtr *ptr)
{
  CustomDataLayer *layer = ptr->data;
  return layer->type;
}

const EnumPropItem *api_enum_attribute_domain_itemf(Id *id,
                                                    bool include_instances,
                                                    bool *r_free)
{
  EnumPropItem *item = NULL;
  const EnumPropItem *domain_item = NULL;
  const IdType id_type = GS(id->name);
  int totitem = 0, a;

  static EnumPropItem mesh_vertex_domain_item = {
      ATTR_DOMAIN_POINT, "POINT", 0, "Vertex", "Attribute per point/vertex"};

  for (a = 0; api_enum_attribute_domain_items[a].id; a++) {
    domain_item = &api_enum_attribute_domain_items[a];

    if (id_type == ID_PT && !ELEM(domain_item->value, ATTR_DOMAIN_POINT)) {
      continue;
    }
    if (id_type == ID_CV && !ELEM(domain_item->value, ATTR_DOMAIN_POINT, ATTR_DOMAIN_CURVE)) {
      continue;
    }
    if (id_type == ID_ME && ELEM(domain_item->value, ATTR_DOMAIN_CURVE)) {
      continue;
    }
    if (!include_instances && domain_item->value == ATTR_DOMAIN_INSTANCE) {
      continue;
    }

    if (domain_item->value == ATTR_DOMAIN_POINT && id_type == ID_ME) {
      api_enum_item_add(&item, &totitem, &mesh_vertex_domain_item);
    }
    else {
      api_enum_item_add(&item, &totitem, domain_item);
    }
  }
  api_enum_item_end(&item, &totitem);

  *r_free = true;
  return item;
}

static const EnumPropItem *api_Attribute_domain_itemf(Cxt *UNUSED(C),
                                                      ApiPtr *ptr,
                                                      ApiProp *UNUSED(prop),
                                                      bool *r_free)
{
  return api_enum_attribute_domain_itemf(ptr->owner_id, true, r_free);
}

static int api_Attribute_domain_get(ApiPtr *ptr)
{
  return dune_id_attribute_domain(ptr->owner_id, ptr->data);
}

static void api_Attribute_data_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;

  int length = dune_id_attribute_data_length(id, layer);
  size_t struct_size;

  switch (layer->type) {
    case CD_PROP_FLOAT:
      struct_size = sizeof(MFloatProp);
      break;
    case CD_PROP_INT32:
      struct_size = sizeof(MIntProp);
      break;
    case CD_PROP_FLOAT3:
      struct_size = sizeof(float[3]);
      break;
    case CD_PROP_COLOR:
      struct_size = sizeof(MPropCol);
      break;
    case CD_MLOOPCOL:
      struct_size = sizeof(MLoopCol);
      break;
    case CD_PROP_STRING:
      struct_size = sizeof(MStringProperty);
      break;
    case CD_PROP_BOOL:
      struct_size = sizeof(MBoolProperty);
      break;
    case CD_PROP_FLOAT2:
      struct_size = sizeof(float[2]);
      break;
    case CD_PROP_INT8:
      struct_size = sizeof(int8_t);
      break;
    default:
      struct_size = 0;
      length = 0;
      break;
  }

  api_iter_array_begin(iter, layer->data, struct_size, length, 0, NULL);
}

static int api_Attribute_data_length(ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  CustomDataLayer *layer = (CustomDataLayer *)ptr->data;
  return dune_id_attribute_data_length(id, layer);
}

static void api_Attribute_update_data(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  /* cheating way for importers to avoid slow updates */
  if (id->us > 0) {
    graph_id_tag_update(id, 0);
    wm_main_add_notifier(NC_GEOM | ND_DATA, id);
  }
}

/* Color Attribute */

static void api_ByteColorAttributeValue_color_get(ApiPtr *ptr, float *values)
{
  MLoopCol *mlcol = (MLoopCol *)ptr->data;
  srgb_to_linearrgb_uchar4(values, &mlcol->r);
}

static void api_ByteColorAttributeValue_color_set(ApiPtr *ptr, const float *values)
{
  MLoopCol *mlcol = (MLoopCol *)ptr->data;
  linearrgb_to_srgb_uchar4(&mlcol->r, values);
}

/* Int8 Attribute. */

static int api_ByteIntAttributeValue_get(ApiPtr *ptr)
{
  int8_t *value = (int8_t *)ptr->data;
  return (int)(*value);
}

static void api_ByteIntAttributeValue_set(ApiPtr *ptr, const int new_value)
{
  int8_t *value = (int8_t *)ptr->data;
  if (new_value > INT8_MAX) {
    *value = INT8_MAX;
  }
  else if (new_value < INT8_MIN) {
    *value = INT8_MIN;
  }
  else {
    *value = (int8_t)new_value;
  }
}

/* Attribute Group */

static ApiPtr api_AttributeGroup_new(
    Id *id, ReportList *reports, const char *name, const int type, const int domain)
{
  CustomDataLayer *layer = dune_id_attribute_new(id, name, type, domain, reports);
  graph_id_tag_update(id, ID_RECALC_GEOMETRY);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);

  ApiPtr ptr;
  api_ptr_create(id, &ApiAttribute, layer, &ptr);
  return ptr;
}

static void api_AttributeGroup_remove(Id *id, ReportList *reports, ApiPtr *attribute_ptr)
{
  CustomDataLayer *layer = (CustomDataLayer *)attribute_ptr->data;
  dune_id_attribute_remove(id, layer, reports);
  API_PTR_INVALIDATE(attribute_ptr);

  graph_id_tag_update(id, ID_RECALC_GEOMETRY);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);
}

static int api_Attributes_layer_skip(CollectionPropIter *UNUSED(iter), void *data)
{
  CustomDataLayer *layer = (CustomDataLayer *)data;
  return !(CD_TYPE_AS_MASK(layer->type) & CD_MASK_PROP_ALL);
}

/* Attributes are spread over multiple domains in separate CustomData, we use repeated
 * array iterators to loop over all. */
static void api_AttributeGroup_next_domain(Id *id,
                                           CollectionPropIter *iter,
                                           int(skip)(CollectionPropIter *iter, void *data))
{
  do {
    CustomDataLayer *prev_layers = (iter->internal.array.endptr == NULL) ?
                                       NULL :
                                       (CustomDataLayer *)iter->internal.array.endptr -
                                           iter->internal.array.length;
    CustomData *customdata = dune_id_attributes_iter_next_domain(id, prev_layers);
    if (customdata == NULL) {
      return;
    }
    api_iter_array_begin(
        iter, customdata->layers, sizeof(CustomDataLayer), customdata->totlayer, false, skip);
  } while (!iter->valid);
}

void api_AttributeGroup_iter_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  memset(&iter->internal.array, 0, sizeof(iter->internal.array));
  api_AttributeGroup_next_domain(ptr->owner_id, iter, api_Attributes_layer_skip);
}

void api_AttributeGroup_iter_next(CollectionPropIter *iter)
{
  api_iter_array_next(iter);

  if (!iter->valid) {
    Id *id = iter->parent.owner_id;
    api_AttributeGroup_next_domain(id, iter, api_Attributes_layer_skip);
  }
}

ApiPtr api_AttributeGroup_iter_get(CollectionPropIter *iter)
{
  /* refine to the proper type */
  CustomDataLayer *layer = api_iter_array_get(iter);
  ApiStruct *type = sapi_by_custom_data_layer_type(layer->type);
  if (type == NULL) {
    return ApiPtr_NULL;
  }
  return api_ptr_inherit_refine(&iter->parent, type, layer);
}

int api_AttributeGroup_length(ApiPtr *ptr)
{
  return dunr_id_attributes_length(ptr->owner_id, CD_MASK_PROP_ALL);
}

static int api_AttributeGroup_active_index_get(ApiPtr *ptr)
{
  return *dune_id_attributes_active_index_p(ptr->owner_id);
}

static ApiPtr api_AttributeGroup_active_get(ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  CustomDataLayer *layer = dune_id_attributes_active_get(id);

  ApiPtr attribute_ptr;
  api_ptr_create(id, &ApiAttribute, layer, &attribute_ptr);
  return attribute_ptr;
}

static void api_AttributeGroup_active_set(ApiPtr *ptr,
                                          ApiPtr attribute_ptr,
                                          ReportList *UNUSED(reports))
{
  Id *id = ptr->owner_id;
  CustomDataLayer *layer = attribute_ptr.data;
  dune_id_attributes_active_set(id, layer);
}

static void api_AttributeGroup_active_index_set(ApiPtr *ptr, int value)
{
  *dune_id_attributes_active_index_p(ptr->owner_id) = value;
}

static void api_AttributeGroup_active_index_range(
    ApiPtr *ptr, int *min, int *max, int *softmin, int *softmax)
{
  *min = 0;
  *max = dune_id_attributes_length(ptr->owner_id, CD_MASK_PROP_ALL);

  *softmin = *min;
  *softmax = *max;
}

static void api_AttributeGroup_update_active(Main *main, Scene *scene, ApiPtr *ptr)
{
  api_Attribute_update_data(main, scene, ptr);
}

#else

static void api_def_attribute_float(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "FloatAttribute", "Attribute");
  api_def_struct_stype(sapi, "CustomDataLayer");
  api_def_struct_ui_text(sapi, "Float Attribute", "Geometry attribute with floating-point values");

  prop = api_def_prop(sapi, "data", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "FloatAttributeValue");
  api_def_prop_collection_fns(prop,
                              "api_Attribute_data_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_Attribute_data_length",
                              NULL,
                              NULL,
                              NULL);

  sapi = api_def_struct(dapi, "FloatAttributeValue", NULL);
  api_def_struct_stype(sapi, "MFloatProp");
  api_def_struct_ui_text(
      sapi, "Float Attribute Value", "Floating-point value in geometry attribute");
  prop = api_def_prop(sapi, "value", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "f");
  api_def_prop_update(prop, 0, "api_Attribute_update_data");
}

static void api_def_attribute_float_vector(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* Float Vector Attribute */
  sapi = api_def_struct(dapi, "FloatVectorAttribute", "Attribute");
  api_def_struct_stype(sapi, "CustomDataLayer");
  api_def_struct_ui_text(
      sapi, "Float Vector Attribute", "Vector geometry attribute, with floating-point values");

  prop = api_def_prop(sapi, "data", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "FloatVectorAttributeValue");
  api_def_prop_collection_fns(prop,
                              "api_Attribute_data_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_Attribute_data_length",
                              NULL,
                              NULL,
                              NULL);

  /* Float Vector Attribute Value */
  sapi = api_def_struct(dapi, "FloatVectorAttributeValue", NULL);
  api_def_struct_stype(sapi, "vec3f");
  api_def_struct_ui_text(
      sapi, "Float Vector Attribute Value", "Vector value in geometry attribute");

  prop = api_def_prop(sapi, "vector", PROP_FLOAT, PROP_DIRECTION);
  api_def_prop_ui_text(prop, "Vector", "3D vector");
  api_def_prop_float_stype(prop, NULL, "x");
  api_def_prop_array(prop, 3);
  api_def_prop_update(prop, 0, "api_Attribute_update_data");
}

static void api_def_attribute_float_color(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* Float Color Attribute */
  sapi = api_def_struct(dapi, "FloatColorAttribute", "Attribute");
  api_def_struct_stype(sapi, "CustomDataLayer");
  api_def_struct_ui_text(
      sapi, "Float Color Attribute", "Color geometry attribute, with floating-point values");

  prop = api_def_prop(sapi, "data", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "FloatColorAttributeValue");
  api_def_prop_collection_fns(prop,
                              "api_Attribute_data_begin",
                              "api_iter_array_next",
                              "apo_iter_array_end",
                              "api",
                              "api_Attribute_data_length",
                              NULL,
                              NULL,
                              NULL);

  /* Float Color Attribute Value */
  sapi = api_def_struct(dapi, "FloatColorAttributeValue", NULL);
  api_def_struct_stype(sapi, "MPropCol");
  api_def_struct_ui_text(sapi, "Float Color Attribute Value", "Color value in geometry attribute");

  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_ui_text(prop, "Color", "RGBA color in scene linear color space");
  api_def_prop_float_stype(prop, NULL, "color");
  api_def_prop_array(prop, 4);
  api_def_prop_update(prop, 0, "api_Attribute_update_data");
}

static void api_def_attribute_byte_color(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* Byte Color Attribute */
  sapi = api_def_struct(dapi, "ByteColorAttribute", "Attribute");
  api_def_struct_stype(sapi, "CustomDataLayer");
  api_def_struct_ui_text(
      sapi, "Byte Color Attribute", "Color geometry attribute, with 8-bit values");

  prop = api_def_prop(sapi, "data", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "ByteColorAttributeValue");
  api_def_prop_collection_fns(prop,
                              "api_Attribute_data_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_Attribute_data_length",
                              NULL,
                              NULL,
                              NULL);

  /* Byte Color Attribute Value */
  sapi = api_def_struct(dapi, "ByteColorAttributeValue", NULL);
  api_def_struct_stype(sapi, "MLoopCol");
  api_def_struct_ui_text(sapi, "Byte Color Attribute Value", "Color value in geometry attribute");

  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_array(prop, 4);
  api_def_prop_range(prop, 0.0f, 1.0f);
  api_def_prop_float_fns(prop,
                         "api_ByteColorAttributeValue_color_get",
                         "api_ByteColorAttributeValue_color_set",
                         NULL);
  api_def_prop_ui_text(prop, "Color", "RGBA color in scene linear color space");
  api_def_prop_update(prop, 0, "api_Attribute_update_data");
}

static void api_def_attribute_int(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "IntAttribute", "Attribute");
  api_def_struct_stype(sapi, "CustomDataLayer");
  api_def_struct_ui_text(sapi, "Int Attribute", "Integer geometry attribute");

  prop = api_def_prop(sapi, "data", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "IntAttributeValue");
  api_def_prop_collection_fns(prop,
                              "api_Attribute_data_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_Attribute_data_length",
                              NULL,
                              NULL,
                              NULL);

  sapi = api_def_struct(dapi, "IntAttributeValue", NULL);
  api_def_struct_stype(sapi, "MIntProp");
  api_def_struct_ui_text(sapi, "Integer Attribute Value", "Integer value in geometry attribute");
  prop = api_def_prop(sapi, "value", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "i");
  api_def_prop_update(prop, 0, "api_Attribute_update_data");
}

static void api_def_attribute_string(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "StringAttribute", "Attribute");
  api_def_struct_stype(sapi, "CustomDataLayer");
  api_def_struct_ui_text(sapi, "String Attribute", "String geometry attribute");

  prop = api_def_prop(sapi, "data", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "StringAttributeValue");
  api_def_prop_collection_fns(prop,
                              "api_Attribute_data_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_Attribute_data_length",
                              NULL,
                              NULL,
                              NULL);

  sapi = api_def_struct(dapi, "StringAttributeValue", NULL);
  api_def_struct_stype(sapi, "MStringProp");
  api_def_struct_ui_text(sapi, "String Attribute Value", "String value in geometry attribute");
  prop = api_def_prop(sapi, "value", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "s");
  api_def_prop_update(prop, 0, "api_Attribute_update_data");
}

static void api_def_attribute_bool(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "BoolAttribute", "Attribute");
  api_def_struct_stype(sapi, "CustomDataLayer");
  api_def_struct_ui_text(sapi, "Bool Attribute", "Bool geometry attribute");

  prop = api_def_prop(sapi, "data", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "BoolAttributeValue");
  api_def_prop_collection_fns(prop,
                              "api_Attribute_data_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_Attribute_data_length",
                              NULL,
                              NULL,
                              NULL);

  sapi = api_def_struct(dapi, "BoolAttributeValue", NULL);
  api_def_struct_stype(sapi, "MBoolProp");
  api_def_struct_ui_text(sapi, "Bool Attribute Value", "Bool value in geometry attribute");
  prop = api_def_prop(sapi, "value", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "b", 0x01);
}

static void api_def_attribute_int8(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "ByteIntAttribute", "Attribute");
  api_def_struct_style(sapi, "CustomDataLayer");
  api_def_struct_ui_text(sapi, "8-bit Int Attribute", "8-bit int geometry attribute");

  prop = api_def_prop(sapi, "data", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "ByteIntAttributeValue");
  api_def_prop_collection_fns(prop,
                              "api_Attribute_data_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              "api_Attribute_data_length",
                              NULL,
                              NULL,
                              NULL);

  sapi = api_def_struct(dapi, "ByteIntAttributeValue", NULL);
  api_def_struct_stype(sapi, "MInt8Prop");
  api_def_struct_ui_text(
      sapi, "8-bit Integer Attribute Value", "8-bit value in geometry attribute");
  prop = api_def_prop(sapi, "value", PROP_INT, PROP_NONE);
  api_def_prop_int_fns(
      prop, "api_ByteIntAttributeValue_get", "api_ByteIntAttributeValue_set", NULL);
}

static void api_def_attribute_float2(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  /* Float2 Attribute */
  sapi = api_def_struct(dapi, "Float2Attribute", "Attribute");
  api_def_struct_stype(sapi, "CustomDataLayer");
  api_def_struct_ui_text(
      sapi, "Float2 Attribute", "2D vector geometry attribute, with floating-point values");

  prop = api_def_prop(sapi, "data", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "Float2AttributeValue");
  api_def_prop_collection_fns(prop,
                             "api_Attribute_data_begin",
                             "api_iter_array_next",
                             "api_iter_array_end",
                             "api_iter_array_get",
                             "api_Attribute_data_length",
                             NULL,
                             NULL,
                             NULL);

  /* Float2 Attribute Value */
  sapi = api_def_struct(dapi, "Float2AttributeValue", NULL);
  api_def_struct_stype(sapi, "vec2f");
  api_def_struct_ui_text(sapi, "Float2 Attribute Value", "2D Vector value in geometry attribute");

  prop = api_def_prop(sapi, "vector", PROP_FLOAT, PROP_DIRECTION);
  api_def_prop_ui_text(prop, "Vector", "2D vector");
  api_def_prop_float_stype(prop, NULL, "x");
  api_def_prop_array(prop, 2);
  api_def_prop_update(prop, 0, "api_Attribute_update_data");
}

static void api_def_attribute(DuneApi *dapi)
{
  ApiProp *prop;
  ApiStruct *sapi;

  sapi = api_def_struct(dapo, "Attribute", NULL);
  api_def_struct_stype(sapi, "CustomDataLayer");
  api_def_struct_ui_text(sapi, "Attribute", "Geometry attribute");
  api_def_struct_path_fn(sapi, "api_Attribute_path");
  api_def_struct_refine_fn(sapi, "api_Attribute_refine");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(prop, NULL, NULL, "rna_Attribute_name_set");
  api_def_prop_editable_fn(prop, "api_Attribute_name_editable");
  api_def_prop_ui_text(prop, "Name", "Name of the Attribute");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "data_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "type");
  api_def_prop_enum_items(prop, api_enum_attribute_type_items);
  api_def_prop_enum_fns(prop, "api_Attribute_type_get", NULL, NULL);
  api_def_prop_ui_text(prop, "Data Type", "Type of data stored in attribute");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "domain", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_attribute_domain_items);
  api_def_prop_enum_fns(
      prop, "apo_Attribute_domain_get", NULL, "api_Attribute_domain_itemf");
  api_def_prop_ui_text(prop, "Domain", "Domain of the Attribute");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  /* types */
  api_def_attribute_float(dapi);
  api_def_attribute_float_vector(dapi);
  api_def_attribute_float_color(dapi);
  api_def_attribute_byte_color(dapi);
  api_def_attribute_int(dapi);
  api_def_attribute_string(dapi);
  api_def_attribute_bool(dapi);
  api_def_attribute_float2(dapi);
  api_def_attribute_int8(dapi);
}

/* Mesh/PointCloud/Hair.attributes */
static void api_def_attribute_group(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;
  ApiFn *fn;
  ApiProp *parm;

  sapi = api_def_struct(dapi, "AttributeGroup", NULL);
  api_def_struct_ui_text(sapi, "Attribute Group", "Group of geometry attributes");
  api_def_struct_stype(sapi, "Id");

  /* API */
  fn = api_def_fn(sapi, "new", "api_AttributeGroup_new");
  api_def_fn_ui_description(fn, "Add an attribute");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_string(fn, "name", "Attribute", 0, "", "Attribute name");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(
      fn, "type", api_enum_attribute_type_items, CD_PROP_FLOAT, "Type", "Attribute type");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fn,
                      "domain",
                      api_enum_attribute_domain_items,
                      ATTR_DOMAIN_POINT,
                      "Domain",
                      "Type of element that attribute is stored on");
  apo_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "attribute", "Attribute", "", "New geometry attribute");
  api_def_param_flags(parm, 0, PARM_RNAPTR);
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "remove", "api_AttributeGroup_remove");
  api_def_fn_ui_description(fn, "Remove an attribute");
  api_def_fn_flag(fn, FN_USE_REPORTS);
  parm = api_def_ptr(fn, "attribute", "Attribute", "", "Geometry Attribute");
  api_def_param_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_APIPTR);
  api_def_param_clear_flags(parm, PROP_THICK_WRAP, 0);

  /* Active */
  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "Attribute");
  api_def_prop_ptr_fns(
      prop, "api_AttributeGroup_active_get", "api_AttributeGroup_active_set", NULL, NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_UNLINK);
  api_def_prop_ui_text(prop, "Active Attribute", "Active attribute");
  api_def_prop_update(prop, 0, "api_AttributeGroup_update_active");

  prop = api_def_prop(sapi, "active_index", PROP_INT, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_ANIMATABLE);
  api_def_prop_int_fns(prop,
                       "api_AttributeGroup_active_index_get",
                       "api_AttributeGroup_active_index_set",
                       "api_AttributeGroup_active_index_range");
  api_def_prop_update(prop, 0, "api_AttributeGroup_update_active");
}

void api_def_attributes_common(ApiStruct *sapi)
{
  ApiProp *prop;

  /* Attributes */
  prop = api_def_prop(sapi, "attributes", PROP_COLLECTION, PROP_NONE);
  apk_def_prop_collection_fns(prop,
                              "api_AttributeGroup_iter_begin",
                              "api_AttributeGroup_iter_next",
                              "api_iter_array_end",
                              "api_AttributeGroup_iter_get",
                              "api_AttributeGroup_length",
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_struct_type(prop, "Attribute");
  api_def_prop_ui_text(prop, "Attributes", "Geometry attributes");
  api_def_prop_sapi(prop, "AttributeGroup");
}

void api_def_attribute(DuneApi *dapi)
{
  api_def_attribute(dapi);
  api_def_attribute_group(dapi);
}
#endif
