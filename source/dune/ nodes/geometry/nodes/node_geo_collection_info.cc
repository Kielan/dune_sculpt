#include "lib_math_matrix.h"
#include "lib_string.h"

#include "types_collection.h"

#include "node_api_define.hh"

#include "ui.hh"
#include "ui_resources.hh"

#include "dune_collection.h"
#include "dune_instances.hh"

#include "node_geo_util.hh"

#include <algorithm>

namespace dune::nodes::node_geo_collection_info_cc {

NODE_STORAGE_FNS(NodeGeoCollectionInfo)

static void node_decl(NodeDeclBuilder &b)
{
  b.add_input<decl::Collection>("Collection").hide_label();
  b.add_input<decl::Bool>("Separate Children")
      .description(
          "Output each child of the collection as a separate instance, sorted alphabetically");
  b.add_input<decl::Bool>("Reset Children")
      .description(
          "Reset the transforms of every child instance in the output. Only used when Separate "
          "Children is enabled");
  b.add_output<decl::Geo>("Instances");
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiItemR(layout, ptr, "transform_space", UI_ITEM_R_EXPAND, nullptr, ICON_NONE);
}

static void node_node_init(NodeTree * /*tree*/, Node *node)
{
  NodeGeoCollectionInfo *data = mem_cnew<NodeGeoCollectionInfo>(__func__);
  data->transform_space = GEO_NODE_TRANSFORM_SPACE_ORIGINAL;
  node->storage = data;
}

struct InstanceListEntry {
  int handle;
  char *name;
  float4x4 transform;
};

static void node_geo_ex(GeoNodeExParams params)
{
  Collection *collection = params.get_input<Collection *>("Collection");

  if (collection == nullptr) {
    params.set_default_remaining_outputs();
    return;
  }
  const Ob *self_ob = params.self_ob();
  const bool is_recursive = dune_collection_has_object_recursive_instanced(
      collection, const_cast<Ob *>(self_object));
  if (is_recursive) {
    params.err_msg_add(NodeWarningType::Error, TIP_("Collection contains current ob"));
    params.set_default_remaining_outputs();
    return;
  }

  const NodeGeoCollectionInfo &storage = node_storage(params.node());
  const bool use_relative_transform = (storage.transform_space ==
                                       GEO_NODE_TRANSFORM_SPACE_RELATIVE);

  std::unique_ptr<dune::Instances> instances = std::make_unique<dune::Instances>();

  const bool separate_children = params.get_input<bool>("Separate Children");
  if (separate_children) {
    const bool reset_children = params.get_input<bool>("Reset Children");
    Vector<Collection *> children_collections;
    LIST_FOREACH (CollectionChild *, collection_child, &collection->children) {
      children_collections.append(collection_child->collection);
    }
    Vector<Ob *> children_obs;
    LIST_FOREACH (CollectionOb *, collection_ob, &collection->gob) {
      children_obs.append(collection_ob->ob);
    }

    Vector<InstanceListEntry> entries;
    entries.reserve(children_collections.size() + children_obs.size());

    for (Collection *child_collection : children_collections) {
      float4x4 transform = float4x4::id();
      if (!reset_children) {
        transform.location() += float3(child_collection->instance_offset);
        if (use_relative_transform) {
          transform = float4x4(self_ob->world_to_ob) * transform;
        }
        else {
          transform.location() -= float3(collection->instance_offset);
        }
      }
      const int handle = instances->add_ref(*child_collection);
      entries.append({handle, &(child_collection->id.name[2]), transform});
    }
    for (Ob *child_ob : children_obs) {
      const int handle = instances->add_ref(*child_ob);
      float4x4 transform = float4x4::id();
      if (!reset_children) {
        if (use_relative_transform) {
          transform = float4x4(self_ob->world_to_ob);
        }
        else {
          transform.location() -= float3(collection->instance_offset);
        }
        transform *= float4x4(child_ob->ob_to_world);
      }
      entries.append({handle, &(child_ob->id.name[2]), transform});
    }

    std::sort(entries.begin(),
              entries.end(),
              [](const InstanceListEntry &a, const InstanceListEntry &b) {
                return lib_strcasecmp_natural(a.name, b.name) < 0;
              });
    for (const InstanceListEntry &entry : entries) {
      instances->add_instance(entry.handle, entry.transform);
    }
  }
  else {
    float4x4 transform = float4x4::id();
    if (use_relative_transform) {
      transform.location() = collection->instance_offset;
      transform = float4x4_view(self_object->world_to_object) * transform;
    }

    const int handle = instances->add_ref(*collection);
    instances->add_instance(handle, transform);
  }

  params.set_output("Instances", GeometrySet::from_instances(instances.release()));
}

static void node_api(ApiStruct *sapi)
{
  static const EnumPropItem rna_node_geometry_collection_info_transform_space_items[] = {
      {GEO_NODE_TRANSFORM_SPACE_ORIGINAL,
       "ORIGINAL",
       0,
       "Original",
       "Output the geometry relative to the collection offset"},
      {GEO_NODE_TRANSFORM_SPACE_RELATIVE,
       "RELATIVE",
       0,
       "Relative",
       "Bring the input collection geo into the modified ob, maintaining the relative "
       "position between the objects in the scene"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  api_def_node_enum(
      sapi,
      "transform_space",
      "Transform Space",
      "The transformation of the instances output. Does not affect the internal geo",
      api_node_geo_collection_info_transform_space_items,
      node_storage_enum_accessors(transform_space),
      GEO_NODE_TRANSFORM_SPACE_ORIGINAL);
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_COLLECTION_INFO, "Collection Info", NODE_CLASS_INPUT);
  ntype.dec = node_decl;
  ntype.initfn = node_node_init;
  node_type_storage(&ntype,
                    "NodeGeoCollectionInfo",
                    node_free_standard_storage,
                    node_copy_standard_storage);
  ntype.geo_node_ex = node_geo_ex;
  ntype.draw_btns = node_layout;
  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_collection_info_cc
