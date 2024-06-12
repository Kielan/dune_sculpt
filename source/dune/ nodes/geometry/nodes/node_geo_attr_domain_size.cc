#include "node_api_define.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "api_enum_types.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_attr_domain_size_cc {

static void node_decl(NodeDeclBuilder &b)
{
  b.add_input<decl::Geo>("Geo");
  b.add_output<decl::Int>("Point Count").make_available([](Node &node) {
    node.custom1 = int16_t(GeoComponent::Type::Mesh);
  });
  b.add_output<decl::Int>("Edge Count").make_available([](Node &node) {
    node.custom1 = int16_t(GeoComponent::Type::Mesh);
  });
  b.add_output<decl::Int>("Face Count").make_available([](Node &node) {
    node.custom1 = int16_t(GeoComponent::Type::Mesh);
  });
  b.add_output<decl::Int>("Face Corner Count").make_available([](Node &node) {
    node.custom1 = int16_t(GeoComponent::Type::Mesh);
  });
  b.add_output<decl::Int>("Spline Count").make_available([](Node &node) {
    node.custom1 = int16_t(GeoComponent::Type::Curve);
  });
  b.add_output<decl::Int>("Instance Count").make_available([](Node &node) {
    node.custom1 = int16_t(GeoComponent::Type::Instance);
  });
  b.add_output<decl::Int>("Layer Count").make_available([](Node &node) {
    node.custom1 = int16_t(GeoComponent::Type::Pen);
  });
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiItemR(layout, ptr, "component", UI_ITEM_NONE, "", ICON_NONE);
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  node->custom1 = int16_t(GeoComponent::Type::Mesh);
}

static void node_update(NodeTree *ntree, Node *node)
{
  NodeSocket *point_socket = static_cast<NodeSocket *>(node->outputs.first);
  NodeSocket *edge_socket = point_socket->next;
  NodeSocket *face_socket = edge_socket->next;
  NodeSocket *face_corner_socket = face_socket->next;
  NodeSocket *spline_socket = face_corner_socket->next;
  NodeSocket *instances_socket = spline_socket->next;
  NodeSocket *layers_socket = instances_socket->next;

  dune::nodeSetSocketAvailability(ntree,
                                 point_socket,
                                 elem(node->custom1,
                                      int16_t(GeoComponent::Type::Mesh),
                                      int16_t(GeoComponent::Type::Curve),
                                      int16_t(GeoComponent::Type::PointCloud)));
  dune::nodeSetSocketAvailability(
      ntree, edge_socket, node->custom1 == int16_t(GeoComponent::Type::Mesh));
  dune::nodeSetSocketAvailability(
      ntree, face_socket, node->custom1 == int16_t(GeoComponent::Type::Mesh));
  dune::nodeSetSocketAvailability(
      ntree, face_corner_socket, node->custom1 == int16_t(GeoComponent::Type::Mesh));
  dune::nodeSetSocketAvailability(
      ntree, spline_socket, node->custom1 == int16_t(GeoComponent::Type::Curve));
  dune::nodeSetSocketAvailability(
      ntree, instances_socket, node->custom1 == int16_t(GeoComponent::Type::Instance));
  dune::nodeSetSocketAvailability(
      ntree, layers_socket, node->custom1 == int16_t(GeoComponent::Type::Pen));
}

static void node_geo_ex(GeoNodeExParams params)
{
  const GeoComponent::Type component = GeoComponent::Type(params.node().custom1);
  const GeoSet geo_set = params.extract_input<GeoSet>("Geometry");

  switch (component) {
    case GeoComponent::Type::Mesh: {
      if (const MeshComponent *component = geo_set.get_component<MeshComponent>()) {
        const AttrAccessor attrs = *component->attrs();
        params.set_output("Point Count", attrs.domain_size(AttrDomain::Point));
        params.set_output("Edge Count", attrs.domain_size(AttrDomain::Edge));
        params.set_output("Face Count", attrs.domain_size(AttrDomain::Face));
        params.set_output("Face Corner Count", attrs.domain_size(AttrDomain::Corner));
      }
      else {
        params.set_default_remaining_outputs();
      }
      break;
    }
    case GeoComponent::Type::Curve: {
      if (const CurveComponent *component = geo_set.get_component<CurveComponent>()) {
        const AttrAccessor attrs = *component->attrs();
        params.set_output("Point Count", attrs.domain_size(AttrDomain::Point));
        params.set_output("Spline Count", attrs.domain_size(AttrDomain::Curve));
      }
      else {
        params.set_default_remaining_outputs();
      }
      break;
    }
    case GeoComponent::Type::PointCloud: {
      if (const PointCloudComponent *component = geo_set.get_component<PointCloudComponent>())
      {
        const AttrAccessor attrs = *component->attrs();
        params.set_output("Point Count", attrs.domain_size(AttrDomain::Point));
      }
      else {
        params.set_default_remaining_outputs();
      }
      break;
    }
    case GeoComponent::Type::Instance: {
      if (const InstancesComponent *component = geo_set.get_component<InstancesComponent>()) {
        const AttrAccessor attributes = *component->attrs();
        params.set_output("Instance Count", attributes.domain_size(AttrDomain::Instance));
      }
      else {
        params.set_default_remaining_outputs();
      }
      break;
    }
    case GeoComponent::Type::Pen: {
      if (const PenComponent *component =
              geo_set.get_component<PenComponent>())
      {
        const AttrAccessor attrs = *component->attrs();
        params.set_output("Layer Count", attrs.domain_size(AttrDomain::Layer));
      }
      else {
        params.set_default_remaining_outputs();
      }
      break;
    }
    default:
      lib_assert_unreachable();
  }
}

static void node_api(ApiStruct *sapi)
{
  api_def_node_enum(sapi,
                    "component",
                    "Component",
                    "",
                    api_enum_geometry_component_type_items,
                    node_inline_enum_accessors(custom1),
                    int(dune::GeometryComponent::Type::Mesh));
}

static void node_register()
{
  static NodeType ntype;
  geo_node_type_base(&ntype, GEO_NODE_ATTR_DOMAIN_SIZE, "Domain Size", NODE_CLASS_ATTR);
  ntype.geo_node_ex = node_geo_ex;
  ntype.decl = node_decl;
  ntype.drw_btns = node_layout;
  ntype.initfn = node_init;
  ntype.updatefn = node_update;

  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
NOD_REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_attr_domain_size_cc
