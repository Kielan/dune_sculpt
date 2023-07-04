#pragma once

/* Macros to help reduce code clutter in rna_mesh.c */
/* Define the accessors for a basic CustomDataLayer collection */
#define DEFINE_CUSTOMDATA_LAYER_COLLECTION(collection_name, customdata_type, layer_type) \
  /* check */ \
  static int api_##collection_name##_check(CollectionPropIter *UNUSED(iter), void *data) \
  { \
    CustomDataLayer *layer = (CustomDataLayer *)data; \
    return (layer->type != layer_type); \
  } \
  /* begin */ \
  static void api_Mesh_##collection_name##s_begin(CollectionPropIter iter, \
                                                  ApiPtr *ptr) \
  { \
    CustomData *data = api_mesh_##customdata_type(ptr); \
    if (data) { \
       api_iter_array_begin(iter, \
                               (void *)data->layers, \
                               sizeof(CustomDataLayer), \
                               data->totlayer, \
                               0, \
                               api_##collection_name##_check); \
    } \
    else { \
      api_iter_array_begin(iter, NULL, 0, 0, 0, NULL); \
    } \
  } \
  /* length */ \
  static int api_Mesh_##collection_name##s_length(ApiPtr *ptr) \
  { \
    CustomData *data = api_mesh_##customdata_type(ptr); \
    return data ? CustomData_number_of_layers(data, layer_type) : 0; \
  } \
  /* index range */ \
  static void api_Mesh_##collection_name##_index_range( \
      ApiPtr *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax)) \
  { \
    CustomData *data = api_mesh_##customdata_type(ptr); \
    *min = 0; \
    *max = data ? CustomData_number_of_layers(data, layer_type) - 1 : 0; \
    *max = MAX2(0, *max); \
  }

/* Define the accessors for special CustomDataLayers in the collection
 * (active, render, clone, stencil, etc) */
#define DEFINE_CUSTOMDATA_LAYER_COLLECTION_ACTIVEITEM( \
    collection_name, customdata_type, layer_type, active_type, layer_api_type) \
\
  static ApiPtr api_Mesh_##collection_name##_##active_type##_get(ApiPtr *ptr) \
  { \
    CustomData *data = api_mesh_##customdata_type(ptr); \
    CustomDataLayer *layer; \
    if (data) { \
      int index = CustomData_get_##active_type##_layer_index(data, layer_type); \
      layer = (index == -1) ? NULL : &data->layers[index]; \
    } \
    else { \
      layer = NULL; \
    } \
    return api_ptr_inherit_refine(ptr, &api_##layer_api_type, layer); \
  } \
\
  static void api_Mesh_##collection_name##_##active_type##_set( \
      ApiPtr *ptr, ApiPtr value, struct ReportList *UNUSED(reports)) \
  { \
    Mesh *me = api_mesh(ptr); \
    CustomData *data = api_mesh_##customdata_type(ptr); \
    int a; \
    if (data) { \
      CustomDataLayer *layer; \
      int layer_index = CustomData_get_layer_index(data, layer_type); \
      for (layer = data->layers + layer_index, a = 0; layer_index + a < data->totlayer; \
           layer++, a++) { \
        if (value.data == layer) { \
          CustomData_set_layer_##active_type(data, layer_type, a); \
          dune_mesh_update_customdata_pointers(me, true); \
          return; \
        } \
      } \
    } \
  } \
\
  static int api_Mesh_##collection_name##_##active_type##_index_get(ApiPtr *ptr) \
  { \
    CustomData *data = api_mesh_##customdata_type(ptr); \
    if (data) { \
      return CustomData_get_##active_type##_layer(data, layer_type); \
    } \
    else { \
      return 0; \
    } \
  } \
\
  static void api_Mesh_##collection_name##_##active_type##_index_set(ApiPtr *ptr, int value) \
  { \
    Mesh *me = api_mesh(ptr); \
    CustomData *data = api_mesh_##customdata_type(ptr); \
    if (data) { \
      CustomData_set_layer_##active_type(data, layer_type, value); \
      dune_mesh_update_customdata_ptrs(me, true); \
    } \
  }
