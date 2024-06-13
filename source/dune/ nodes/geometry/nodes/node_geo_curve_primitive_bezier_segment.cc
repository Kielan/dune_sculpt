#include "dune_curves.hh"

#include "node_api_define.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_curve_primitive_bezier_segment_cc {

NODE_STORAGE_FNS(NodeGeometryCurvePrimitiveBezierSegment)

static void node_decl(NodeDeclBuilder &b)
{
  b.add_input<decl::Int>("Resolution")
      .default_val(16)
      .min(1)
      .max(256)
      .subtype(PROP_UNSIGNED)
      .description("The number of evaluated points on the curve");
  b.add_input<decl::Vector>("Start")
      .default_val({-1.0f, 0.0f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description("Position of the start control point of the curve");
  b.add_input<decl::Vector>("Start Handle")
      .default_val({-0.5f, 0.5f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description(
          "Position of the start handle used to define the shape of the curve. In Offset mode, "
          "relative to Start point");
  b.add_input<decl::Vector>("End Handle")
      .subtype(PROP_TRANSLATION)
      .description(
          "Position of the end handle used to define the shape of the curve. In Offset mode, "
          "relative to End point");
  b.add_input<decl::Vector>("End")
      .default_val({1.0f, 0.0f, 0.0f})
      .subtype(PROP_TRANSLATION)
      .description("Position of the end control point of the curve");
  b.add_output<decl::Geo>("Curve");
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiItemR(layout, ptr, "mode", UI_ITEM_R_EXPAND, nullptr, ICON_NONE);
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  NodeGeoCurvePrimitiveBezierSegment *data =
      mem_cnew<NodeGeoCurvePrimitiveBezierSegment>(__func__);

  data->mode = GEO_NODE_CURVE_PRIMITIVE_BEZIER_SEGMENT_POSITION;
  node->storage = data;
}

static Curves *create_bezier_segment_curve(const float3 start,
                                           const float3 start_handle_right,
                                           const float3 end,
                                           const float3 end_handle_left,
                                           const int resolution,
                                           const GeoNodeCurvePrimitiveBezierSegmentMode mode)
{
  Curves *curves_id = dune::curves_new_nomain_single(2, CURVE_TYPE_BEZIER);
  dune::CurvesGeo &curves = curves_id->geometry.wrap();
  curves.resolution_for_write().fill(resolution);

  MutableSpan<float3> positions = curves.positions_for_write();
  curves.handle_types_left_for_write().fill(BEZIER_HANDLE_ALIGN);
  curves.handle_types_right_for_write().fill(BEZIER_HANDLE_ALIGN);

  positions.first() = start;
  positions.last() = end;

  MutableSpan<float3> handles_right = curves.handle_positions_right_for_write();
  MutableSpan<float3> handles_left = curves.handle_positions_left_for_write();

  if (mode == GEO_NODE_CURVE_PRIMITIVE_BEZIER_SEGMENT_POSITION) {
    handles_left.first() = 2.0f * start - start_handle_right;
    handles_right.first() = start_handle_right;

    handles_left.last() = end_handle_left;
    handles_right.last() = 2.0f * end - end_handle_left;
  }
  else {
    handles_left.first() = start - start_handle_right;
    handles_right.first() = start + start_handle_right;

    handles_left.last() = end + end_handle_left;
    handles_right.last() = end - end_handle_left;
  }

  return curves_id;
}

static void node_geo_ex(GeoNodeExParams params)
{
  const NodeGeoCurvePrimitiveBezierSegment &storage = node_storage(params.node());
  const GeoNodeCurvePrimitiveBezierSegmentMode mode =
      (const GeoNodeCurvePrimitiveBezierSegmentMode)storage.mode;

  Curves *curves = create_bezier_segment_curve(
      params.extract_input<float3>("Start"),
      params.extract_input<float3>("Start Handle"),
      params.extract_input<float3>("End"),
      params.extract_input<float3>("End Handle"),
      std::max(params.extract_input<int>("Resolution"), 1),
      mode);
  params.set_output("Curve", GeoSet::from_curves(curves));
}

static void node_api(ApiStruct *sapi)
{
  static const EnumPropItem mode_items[] = {

      {GEO_NODE_CURVE_PRIMITIVE_BEZIER_SEGMENT_POSITION,
       "POSITION",
       ICON_NONE,
       "Position",
       "The start and end handles are fixed positions"},
      {GEO_NODE_CURVE_PRIMITIVE_BEZIER_SEGMENT_OFFSET,
       "OFFSET",
       ICON_NONE,
       "Offset",
       "The start and end handles are offsets from the spline's control points"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  api_def_node_enum(sapi,
                    "mode",
                    "Mode",
                    "Method used to determine control handles",
                    mode_items,
                    node_storage_enum_accessors(mode),
                    GEO_NODE_CURVE_PRIMITIVE_BEZIER_SEGMENT_POSITION);
}

static void node_register()
{
  static NodeType ntype;
  geo_node_type_base(
      &ntype, GEO_NODE_CURVE_PRIMITIVE_BEZIER_SEGMENT, "Bezier Segment", NODE_CLASS_GEO);
  ntype.initfn = node_init;
  node_type_storage(&ntype,
                    "NodeGeoCurvePrimitiveBezierSegment",
                    node_free_standard_storage,
                    node_copy_standard_storage);
  ntype.decl = node_decl;
  ntype.drw_btns = node_layout;
  ntype.geo_node_ex = node_geo_ex;
  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_curve_primitive_bezier_segment_cc
