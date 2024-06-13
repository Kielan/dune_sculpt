#include "dune_curves.hh"
#include "dune_pen.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_curve_length_cc {

static void node_decl(NodeDeclBuilder &b)
{
  b.add_input<decl::Geo>("Curve").supported_type(
      {GeoComponent::Type::Curve, GeoComponent::Type::Pen});
  b.add_output<decl::Float>("Length");
}

static float curves_total_length(const dune::CurvesGeo &curves)
{
  const VArray<bool> cyclic = curves.cyclic();
  curves.ensure_eval_lengths();

  float total_length = 0.0f;
  for (const int i : curves.curves_range()) {
    total_length += curves.eval_length_total_for_curve(i, cyclic[i]);
  }
  return total_length;
}

static void node_geo_ex(GeoNodeExParams params)
{
  GeoSet geo_set = params.extract_input<GeoSet>("Curve");
  float length = 0.0f;
  if (geometry_set.has_curves()) {
    const Curves &curves_id = *geo_set.get_curves();
    const dune::CurvesGeo &curves = curves_id.geo.wrap();
    length += curves_total_length(curves);
  }
  else if (geo_set.has_pen()) {
    using namespace dune::pen;
    const Pen &pen = *geo_set.get_pen();
    for (const int layer_index : pen.layers().index_range()) {
      const Drawing *drawing = get_eval_pen_layer_drawing(pen, layer_index);
      if (drawing == nullptr) {
        continue;
      }
      const bke::CurvesGeometry &curves = drawing->strokes();
      length += curves_total_length(curves);
    }
  }
  else {
    params.set_default_remaining_outputs();
    return;
  }

  params.set_output("Length", length);
}

static void node_register()
{
  static bNodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_CURVE_LENGTH, "Curve Length", NODE_CLASS_GEOMETRY);
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  nodeRegisterType(&ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_curve_length_cc
