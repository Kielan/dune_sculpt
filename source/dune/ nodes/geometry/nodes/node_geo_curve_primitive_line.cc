#include "dune_curves.hh"

#include "node_api_define.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_curve_primitive_line_cc {

NODE_STORAGE_FNS(NodeGeoCurvePrimitiveLine)

static void node_decl(NodeDeclBuilder &b)
{
  auto enable_direction = [](Node &node) {
    node_storage(node).mode = GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_DIRECTION;
  };

  b.add_input<decl::Vector>("Start")
      .subtype(PROP_TRANSLATION)
      .description("Position of the first control point");
  b.add_input<decl::Vector>("End")
      .default_val({0.0f, 0.0f, 1.0f})
      .subtype(PROP_TRANSLATION)
      .description("Position of the second control point")
      .make_available([](Node &node) {
        node_storage(node).mode = GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_POINTS;
      });
  b.add_input<decl::Vector>("Direction")
      .default_val({0.0f, 0.0f, 1.0f})
      .description("Direction the line is going in. The length of this vector does not matter")
      .make_available(enable_direction);
  b.add_input<decl::Float>("Length")
      .default_val(1.0f)
      .subtype(PROP_DISTANCE)
      .description("Distance between the two points")
      .make_available(enable_direction);
  b.add_output<decl::Geo>("Curve");
}

static void node_layout(uiLayout *layout, Cx * /*C*/, ApiPtr *ptr)
{
  uiItemR(layout, ptr, "mode", UI_ITEM_R_EXPAND, nullptr, ICON_NONE);
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  NodeGeoCurvePrimitiveLine *data = mem_cnew<NodeGeoCurvePrimitiveLine>(__func__);

  data->mode = GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_POINTS;
  node->storage = data;
}

static void node_update(NodeTree *ntree, Node *node)
{
  const NodeGeoCurvePrimitiveLine &storage = node_storage(*node);
  const GeoNodeCurvePrimitiveLineMode mode = (GeoNodeCurvePrimitiveLineMode)storage.mode;

  NodeSocket *p2_socket = static_cast<NodeSocket *>(node->inputs.first)->next;
  NodeSocket *direction_socket = p2_socket->next;
  NodeSocket *length_socket = direction_socket->next;

  dune::nodeSetSocketAvailability(
      ntree, p2_socket, mode == GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_POINTS);
  dune::nodeSetSocketAvailability(
      ntree, direction_socket, mode == GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_DIRECTION);
  dune::nodeSetSocketAvailability(
      ntree, length_socket, mode == GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_DIRECTION);
}

static Curves *create_point_line_curve(const float3 start, const float3 end)
{
  Curves *curves_id = dune::curves_new_nomain_single(2, CURVE_TYPE_POLY);
  dune::CurvesGeo &curves = curves_id->geo.wrap();

  curves.positions_for_write().first() = start;
  curves.positions_for_write().last() = end;

  return curves_id;
}

static Curves *create_direction_line_curve(const float3 start,
                                           const float3 direction,
                                           const float length)
{
  Curves *curves_id = dune::curves_new_nomain_single(2, CURVE_TYPE_POLY);
  dune::CurvesGeo &curves = curves_id->geo.wrap();

  curves.positions_for_write().first() = start;
  curves.positions_for_write().last() = math::normalize(direction) * length + start;

  return curves_id;
}

static void node_geo_ex(GeoNodeExParams params)
{
  const NodeGeoCurvePrimitiveLine &storage = node_storage(params.node());
  const GeoNodeCurvePrimitiveLineMode mode = (GeoNodeCurvePrimitiveLineMode)storage.mode;

  Curves *curves = nullptr;
  if (mode == GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_POINTS) {
    curves = create_point_line_curve(params.extract_input<float3>("Start"),
                                     params.extract_input<float3>("End"));
  }
  else if (mode == GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_DIRECTION) {
    curves = create_direction_line_curve(params.extract_input<float3>("Start"),
                                         params.extract_input<float3>("Direction"),
                                         params.extract_input<float>("Length"));
  }

  params.set_output("Curve", GeoSet::from_curves(curves));
}

static void node_api(ApiStruct *sapi)
{
  static const EnumPropItem mode_items[] = {
      {GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_POINTS,
       "POINTS",
       ICON_NONE,
       "Points",
       "Define the start and end points of the line"},
      {GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_DIRECTION,
       "DIRECTION",
       ICON_NONE,
       "Direction",
       "Define a line with a start point, direction and length"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  api_def_node_enum(sapi,
                    "mode",
                    "Mode",
                    "Method used to determine radius and placement",
                    mode_items,
                    node_storage_enum_accessors(mode),
                    GEO_NODE_CURVE_PRIMITIVE_LINE_MODE_POINTS);
}

static void node_register()
{
  static NodeType ntype;
  geo_node_type_base(&ntype, GEO_NODE_CURVE_PRIMITIVE_LINE, "Curve Line", NODE_CLASS_GEO);
  ntype.initfn = node_init;
  ntype.updatefn = node_update;
  node_type_storage(&ntype,
                    "NodeGeoCurvePrimitiveLine",
                    node_free_standard_storage,
                    node_copy_standard_storage);
  ntype.decl = node_decl;
  ntype.geo_node_ex = node_geo_ex;
  ntype.drw_btns = node_layout;
  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_curve_primitive_line_cc
