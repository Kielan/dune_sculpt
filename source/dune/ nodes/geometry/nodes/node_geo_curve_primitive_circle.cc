#include "dune_curves.hh"

#include "lib_math_geom.h"

#include "node_api_define.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_curve_primitive_circle_cc {

NODE_STORAGE_FNS(NodeGeoCurvePrimitiveCircle)

static void node_decl(NodeDeclBuilder &b)
{
  auto endable_points = [](Node &node) {
    node_storage(node).mode = GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_POINTS;
  };
  auto enable_radius = [](Node &node) {
    node_storage(node).mode = GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_RADIUS;
  };

  b.add_input<decl::Int>("Resolution")
      .default_val(32)
      .min(3)
      .max(512)
      .description("Number of points on the circle");
  b.add_input<decl::Vector>("Point 1")
      .default_val({-1.0f, 0.0f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description(
          "One of the three points on the circle. The point order determines the circle's "
          "direction")
      .make_available(endable_points);
  b.add_input<decl::Vector>("Point 2")
      .default_val({0.0f, 1.0f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description(
          "One of the three points on the circle. The point order determines the circle's "
          "direction")
      .make_available(endable_points);
  b.add_input<decl::Vector>("Point 3")
      .default_val({1.0f, 0.0f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description(
          "One of the three points on the circle. The point order determines the circle's "
          "direction")
      .make_available(endable_points);
  b.add_input<decl::Float>("Radius")
      .default_val(1.0f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Distance of the points from the origin")
      .make_available(enable_radius);
  b.add_output<decl::Geo>("Curve");
  b.add_output<decl::Vector>("Center").make_available(endable_points);
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiItemR(layout, ptr, "mode", UI_ITEM_R_EXPAND, nullptr, ICON_NONE);
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  NodeGeoCurvePrimitiveCircle *data = mem_cnew<NodeGeoCurvePrimitiveCircle>(__func__);

  data->mode = GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_RADIUS;
  node->storage = data;
}

static void node_update(NodeTree *ntree, Node *node)
{
  const NodeGeoCurvePrimitiveCircle &storage = node_storage(*node);
  const GeoNodeCurvePrimitiveCircleMode mode = (GeoNodeCurvePrimitiveCircleMode)
                                                        storage.mode;

  NodeSocket *start_socket = static_cast<NodeSocket *>(node->inputs.first)->next;
  NodeSocket *middle_socket = start_socket->next;
  NodeSocket *end_socket = middle_socket->next;
  NodeSocket *radius_socket = end_socket->next;

  NodeSocket *center_socket = static_cast<NodeSocket *>(node->outputs.first)->next;

  dune::nodeSetSocketAvailability(
      ntree, start_socket, mode == GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_POINTS);
  dune::nodeSetSocketAvailability(
      ntree, middle_socket, mode == GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_POINTS);
  dune::nodeSetSocketAvailability(
      ntree, end_socket, mode == GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_POINTS);
  dune::nodeSetSocketAvailability(
      ntree, center_socket, mode == GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_POINTS);
  dune::nodeSetSocketAvailability(
      ntree, radius_socket, mode == GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_RADIUS);
}

static bool colinear_f3_f3_f3(const float3 p1, const float3 p2, const float3 p3)
{
  const float3 a = math::normalize(p2 - p1);
  const float3 b = math::normalize(p3 - p1);
  return elem(a, b, b * -1.0f);
}

static Curves *create_point_circle_curve(
    const float3 p1, const float3 p2, const float3 p3, const int resolution, float3 &r_center)
{
  if (colinear_f3_f3_f3(p1, p2, p3)) {
    r_center = float3(0);
    return nullptr;
  }

  float3 center;
  /* Midpoints of `P1->P2` and `P2->P3`. */
  const float3 q1 = math::interpolate(p1, p2, 0.5f);
  const float3 q2 = math::interpolate(p2, p3, 0.5f);

  /* Normal Vectors of `P1->P2` and `P2->P3` */
  const float3 v1 = math::normalize(p2 - p1);
  const float3 v2 = math::normalize(p3 - p2);

  /* Normal of plane of main 2 segments P1->P2 and `P2->P3`. */
  const float3 v3 = math::normalize(math::cross(v1, v2));

  /* Normal of plane of first perpendicular bisector and `P1->P2`. */
  const float3 v4 = math::normalize(math::cross(v3, v1));

  /* Determine Center-point from the intersection of 3 planes. */
  float plane_1[4], plane_2[4], plane_3[4];
  plane_from_point_normal_v3(plane_1, q1, v3);
  plane_from_point_normal_v3(plane_2, q1, v1);
  plane_from_point_normal_v3(plane_3, q2, v2);

  /* If the 3 planes do not intersect at one point, just return empty geo. */
  if (!isect_plane_plane_plane_v3(plane_1, plane_2, plane_3, center)) {
    r_center = float3(0);
    return nullptr;
  }

  Curves *curves_id = dune::curves_new_nomain_single(resolution, CURVE_TYPE_POLY);
  dune::CurvesGeometry &curves = curves_id->geo.wrap();
  curves.cyclic_for_write().first() = true;

  MutableSpan<float3> positions = curves.positions_for_write();

  /* Get the radius from the center-point to p1. */
  const float r = math::distance(p1, center);
  const float theta_step = ((2 * M_PI) / float(resolution));
  for (const int i : IndexRange(resolution)) {

    /* Formula for a circle around a point and 2 unit vectors perpendicular
     * to each other and the axis of the circle from:
     * https://math.stackexchange.com/questions/73237/parametric-equation-of-a-circle-in-3d-space */

    const float theta = theta_step * i;
    positions[i] = center + r * sin(theta) * v1 + r * cos(theta) * v4;
  }

  r_center = center;
  return curves_id;
}

static Curves *create_radius_circle_curve(const int resolution, const float radius)
{
  Curves *curves_id = dune::curves_new_nomain_single(resolution, CURVE_TYPE_POLY);
  dune::CurvesGeo &curves = curves_id->geo.wrap();
  curves.cyclic_for_write().first() = true;

  MutableSpan<float3> positions = curves.positions_for_write();

  const float theta_step = (2.0f * M_PI) / float(resolution);
  for (int i : IndexRange(resolution)) {
    const float theta = theta_step * i;
    const float x = radius * cos(theta);
    const float y = radius * sin(theta);
    positions[i] = float3(x, y, 0.0f);
  }

  return curves_id;
}

static void node_geo_ex(GeoNodeExParams params)
{
  const NodeGeoCurvePrimitiveCircle &storage = node_storage(params.node());
  const GeoNodeCurvePrimitiveCircleMode mode = (GeoNodeCurvePrimitiveCircleMode)
                                                storage.mode;

  Curves *curves = nullptr;
  if (mode == GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_POINTS) {
    float3 center_point;
    curves = create_point_circle_curve(params.extract_input<float3>("Point 1"),
                                       params.extract_input<float3>("Point 2"),
                                       params.extract_input<float3>("Point 3"),
                                       std::max(params.extract_input<int>("Resolution"), 3),
                                       center_point);
    params.set_output("Center", center_point);
  }
  else if (mode == GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_RADIUS) {
    curves = create_radius_circle_curve(std::max(params.extract_input<int>("Resolution"), 3),
                                        params.extract_input<float>("Radius"));
  }

  if (curves) {
    params.set_output("Curve", GeoSet::from_curves(curves));
  }
  else {
    params.set_default_remaining_outputs();
  }
}

static void node_api(ApiStruct *sapi)
{
  static const EnumPropItem mode_items[] = {
      {GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_POINTS,
       "POINTS",
       ICON_NONE,
       "Points",
       "Define the radius and location with three points"},
      {GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_RADIUS,
       "RADIUS",
       ICON_NONE,
       "Radius",
       "Define the radius with a float"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  api_def_node_enum(sapi,
                    "mode",
                    "Mode",
                    "Method used to determine radius and placement",
                    mode_items,
                    node_storage_enum_accessors(mode),
                    GEO_NODE_CURVE_PRIMITIVE_CIRCLE_TYPE_RADIUS);
}

static void node_register()
{
  static NodeType ntype;
  geo_node_type_base(&ntype, GEO_NODE_CURVE_PRIMITIVE_CIRCLE, "Curve Circle", NODE_CLASS_GEO);

  ntype.initfn = node_init;
  ntype.updatefn = node_update;
  node_type_storage(&ntype,
                    "NodeGeometryCurvePrimitiveCircle",
                    node_free_standard_storage,
                    node_copy_standard_storage);
  ntype.decl = node_decl;
  ntype.geo_node_ex = node_geo_ex;
  ntype.drw_btns = node_layout;
  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_curve_primitive_circle_cc
