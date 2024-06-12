#include "node_api_define.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "dune_attr_math.hh"

#include "node_socket_search_link.hh"

#include "api_enum_types.hh"

#include "node_geometry_util.hh"

namespace dune::nodes::node_geo_attr_capture_cc {

NODE_STORAGE_FNS(NodeGeometryAttrCapture)

static void node_decl(NodeDeclBuilder &b)
{
  const Node *node = b.node_or_null();

  b.add_input<decl::Geometry>("Geometry");
  if (node != nullptr) {
    const eCustomDataType data_type = eCustomDataType(node_storage(*node).data_type);
    b.add_input(data_type, "Val").field_on_all();
  }

  b.add_output<decl::Geometry>("Geometry").propagate_all();
  if (node != nullptr) {
    const eCustomDataType data_type = eCustomDataType(node_storage(*node).data_type);
    b.add_output(data_type, "Attr").field_on_all();
  }
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);
  uiItemR(layout, ptr, "data_type", UI_ITEM_NONE, "", ICON_NONE);
  uiItemR(layout, ptr, "domain", UI_ITEM_NONE, "", ICON_NONE);
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  NodeGeometryAttrCapture *data = mem_cnew<NodeGeometryAttrCapture>(__func__);
  data->data_type = CD_PROP_FLOAT;
  data->domain = int8_t(AttrDomain::Point);

  node->storage = data;
}

static void node_gather_link_searches(GatherLinkSearchOpParams &params)
{
  const NodeDeclaration &decl = *params.node_type().static_decl;
  search_link_ops_for_declarations(params, decl.inputs);
  search_link_ops_for_declarations(params, decl.outputs);

  const NodeType &node_type = params.node_type();
  const std::optional<eCustomDataType> type = dune::socket_type_to_custom_data_type(
      eNodeSocketDatatype(params.other_socket().type));
  if (type && *type != CD_PROP_STRING) {
    if (params.in_out() == SOCK_OUT) {
      params.add_item(IFACE_("Attr"), [node_type, type](LinkSearchOpParams &params) {
        Node &node = params.add_node(node_type);
        node_storage(node).data_type = *type;
        params.update_and_connect_available_socket(node, "Attr");
      });
    }
    else {
      params.add_item(IFACE_("Val"), [node_type, type](LinkSearchOpParams &params) {
        Node &node = params.add_node(node_type);
        node_storage(node).data_type = *type;
        params.update_and_connect_available_socket(node, "Val");
      });
    }
  }
}

static void clean_unused_attrs(const AnonymousAttrPropInfo &prop_info,
                                    const Set<AttrIdRef> &skip,
                                    GeometryComponent &component)
{
  std::optional<MutableAttrAccessor> attrs = component.attributes_for_write();
  if (!attrs.has_val()) {
    return;
  }

  Vector<std::string> unused_ids;
  attrs->for_all([&](const AttrIdRef &id, const AttrMetaData /*meta_data*/) {
    if (!id.is_anonymous()) {
      return true;
    }
    if (skip.contains(id)) {
      return true;
    }
    if (prop_info.propagate(id.anonymous_id())) {
      return true;
    }
    unused_ids.append(id.name());
    return true;
  });

  for (const std::string &unused_id : unused_ids) {
    attrs->remove(unused_id);
  }
}

static void node_geo_ex(GeoNodeExParams params)
{
  GeoSet geo_set = params.extract_input<GeoSet>("Geo");

  if (!params.output_is_required("Geo")) {
    params.err_msg_add(
        NodeWarningType::Info,
        TIP_("The attr output can not be used wo the geo output"));
    params.set_default_remaining_outputs();
    return;
  }

  const NodeGeoAttrCapture &storage = node_storage(params.node());
  const AttrDomain domain = AttrDomain(storage.domain);

  AnonymousAttrIdPtr attr_id = params.get_output_anonymous_attr_id_if_needed(
      "Attr");
  if (!attr_id) {
    params.set_output("Geo", geo_set);
    params.set_default_remaining_outputs();
    return;
  }

  const GField field = params.extract_input<GField>("Val");

  const auto capture_on = [&](GeoComponent &component) {
    dune::try_capture_field_on_geo(component, *attr_id, domain, field);
    /* Changing of the anonymous attrs may require removing attrs that are no longer
     * needed. */
    clean_unused_attrs(
        params.get_output_prop_info("Geo"), {*attr_id}, component);
  };

  /* Run on the instances component separately to only affect the top level of instances. */
  if (domain == AttrDomain::Instance) {
    if (geo_set.has_instances()) {
      capture_on(geo_set.get_component_for_write(GeoComponent::Type::Instance));
    }
  }
  else {
    static const Array<GeoComponent::Type> types = {GeoComponent::Type::Mesh,
                                                    GeoComponent::Type::PointCloud,
                                                    GeoComponent::Type::Curve,
                                                    GeoComponent::Type::Pen};

    geo_set.modify_geo_sets([&](GeoSet &geo_set) {
      for (const GeoComponent::Type type : types) {
        if (geo_set.has(type)) {
          capture_on(geo_set.get_component_for_write(type));
        }
      }
    });
  }

  params.set_output("Geo", geo_set);
}

static void node_api(ApiStruct *sapi)
{
  api_def_node_enum(sapi,
                    "data_type",
                    "Data Type",
                    "Type of data stored in attr",
                    api_enum_attr_type_items,
                    node_storage_enum_accessors(data_type),
                    CD_PROP_FLOAT,
                    enums::attr_type_type_with_socket_fn);

  api_def_node_enum(sapi,
                    "domain",
                    "Domain",
                    "Which domain to store the data in",
                    api_enum_attr_domain_items,
                    node_storage_enum_accessors(domain),
                    int8_t(AttrDomain::Point),
                    enums::domain_experimental_pen_version3_fn);
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(
      &ntype, GEO_NODE_CAPTURE_ATTRIBUTE, "Capture Attr", NODE_CLASS_ATTRIBUTE);
  node_type_storage(&ntype,
                    "NodeGeoAttrCapture",
                    node_free_standard_storage,
                    node_copy_standard_storage);
  ntype.initfn = node_init;
  ntype.declare = node_declare;
  ntype.geo_node_ex = node_geo_ex;
  ntype.drw_btns = node_layout;
  ntype.gather_link_search_ops = node_gather_link_searches;
  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
NOD_REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_attr_capture_cc
