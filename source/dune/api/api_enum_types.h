#pragma once

#include "api_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Id;
struct NodeSocketType;
struct NodeTreeType;
struct NodeType;

/* Types */
#define DEF_ENUM(id) extern const EnumPropItem id[];
#include "api_enum_items.h"

extern const EnumPropItem *api_enum_attribute_domain_itemf(struct Id *id,
                                                           bool include_instances,
                                                           bool *r_free);

/* For Id filters (FILTER_ID_AC, FILTER_ID_AR, ...) an int isn't enough. This version allows 64
 * bit integers. So can't use the regular EnumPropItem. Would be nice if api supported this
 * itself.
 * Meant to be used with api_def_prop_bool_stype() which supports 64 bit flags as well. */
struct IdFilterEnumPropItem {
  const uint64_t flag;
  const char *id;
  const int icon;
  const char *name;
  const char *description;
};
extern const struct IdFilterEnumPropItem api_enum_id_type_filter_items[];

/* API calls */
int api_node_tree_type_to_enum(struct NodeTreeType *typeinfo);
int api_node_tree_idname_to_enum(const char *idname);
struct NodeTreeType *api_node_tree_type_from_enum(int value);
const EnumPropItem *api_node_tree_type_itemf(void *data,
                                             bool (*poll)(void *data, struct NodeTreeType *),
                                             bool *r_free);

int api_node_type_to_enum(struct NodeType *typeinfo);
int api_node_idname_to_enum(const char *idname);
struct NodeType *api_node_type_from_enum(int value);
const EnumPropItem *api_node_type_itemf(void *data,
                                        bool (*poll)(void *data, struct NodeType *),
                                        bool *r_free);

int api_node_socket_type_to_enum(struct NodeSocketType *typeinfo);
int api_node_socket_idname_to_enum(const char *idname);
struct NodeSocketType *api_node_socket_type_from_enum(int value);
const EnumPropItem *api_node_socket_type_itemf(
    void *data, bool (*poll)(void *data, struct NodeSocketType *), bool *r_free);

struct ApiPtr;
struct ApiProp;
struct Cxt;

const EnumPropItem *api_TransformOrientation_itemf(struct Cxt *C,
                                                   struct ApiPtr *ptr,
                                                   struct ApiProp *prop,
                                                   bool *r_free);

/* Generic fns, return an enum from lib data, index is the position
 * in the linked list can add more for different types as needed. */
const EnumPropItem *api_action_itemf(struct Cxt *C,
                                    struct ApiPtr *ptr,
                                    struct ApiProp *prop,
                                    bool *r_free);
#if 0
EnumPropItem *api_action_local_itemf(struct Cxt *C,
                                     struct Apitr *ptr,
                                     struct ApiProp *prop,
                                     bool *r_free);
#endif
const EnumPropItem *api_collection_itemf(struct Cxt *C,
                                         struct ApiPtr *ptr,
                                         struct ApiProp *prop,
                                         bool *r_free);
const EnumPropItem *api_collection_local_itemf(struct Cxt *C,
                                              struct ApiPtr *ptr,
                                              struct ApiProp *prop,
                                              bool *r_free);
const EnumPropItem *api_image_itemf(struct Cxt *C,
                                    struct ApiPtr *ptr,
                                    struct ApiProp *prop,
                                    bool *r_free);
const EnumPropItem *api_image_local_itemf(struct Cxt *C,
                                          struct ApiPtr *ptr,
                                          struct ApiProp *prop,
                                          bool *r_free);
const EnumPropItem *api_scene_itemf(struct Cxt *C,
                                    struct ApiPtr *ptr,
                                    struct ApiProp *prop,
                                    bool *r_free);
const EnumPropItem *api_scene_without_active_itemf(struct Cxt *C,
                                                   struct ApiPtr *ptr,
                                                   struct ApiProp *prop,
                                                   bool *r_free);
const EnumPropItem *api_scene_local_itemf(struct Cxt *C,
                                          struct ApiPtr *ptr,
                                          struct ApiProp *prop,
                                          bool *r_free);
const EnumPropItem *api_movieclip_itemf(struct Cxt *C,
                                        struct ApiPtr *ptr,
                                        struct ApiProp *prop,
                                        bool *r_free);
const EnumPropItem *api_movieclip_local_itemf(struct Cxt *C,
                                              struct ApiPtr *ptr,
                                              struct ApiProp *prop,
                                              bool *r_free);
const EnumPropItem *api_mask_itemf(struct Cxt *C,
                                   struct ApiPtr *ptr,
                                   struct ApiProp *prop,
                                   bool *r_free);
const EnumPropItem *api_mask_local_itemf(struct Cxt *C,
                                         struct ApiPtr *ptr,
                                             struct ApiProp *prop,
                                             bool *r_free);

/* Non confirming, util fn. */
const EnumPropItem *api_enum_node_tree_types_itemf_impl(struct Cxt *C, bool *r_free);

#ifdef __cplusplus
}
#endif
