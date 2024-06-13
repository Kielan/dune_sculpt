#include "node_api_define.hh"

#include "types_pen.h"

#include "dune_pen.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "geo_fillet_curves.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_curve_fillet_cc {

NODE_STORAGE_FNS(NodeGeoCurveFillet)

static void node_decl(NodeDeclBuilder &b)
{
  b.add_input<decl::Geo>("Curve").supported_type(
      {GeoComponent::Type::Curve, GeoComponent::Type::Pen});
  b.add_input<decl::Int>("Count").default_val(1).min(1).max(1000).field_on_all().make_available(
      [](Node &node) { node_storage(node).mode = GEO_NODE_CURVE_FILLET_POLY; });
  b.add_input<decl::Float>("Radius")
      .min(0.0f)
      .max(FLT_MAX)
      .subtype(PropSubType::PROP_DISTANCE)
      .default_val(0.25f)
      .field_on_all();
  b.add_input<decl::Bool>("Limit Radius")
      .description("Limit the max val of the radius in order to avoid overlapping fillets");
  b.add_output<decl::Geo>("Curve").propagate_all();
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiItemR(layout, ptr, "mode", UI_ITEM_R_EXPAND, nullptr, ICON_NONE);
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  NodeGeoCurveFillet *data = mem_cnew<NodeGeometryCurveFillet>(__func__);
  data->mode = GEO_NODE_CURVE_FILLET_BEZIER;
  node->storage = data;
}

static void node_update(NodeTree *ntree, Node *node)
{
  const NodeGeoCurveFillet &storage = node_storage(*node);
  const GeoNodeCurveFilletMode mode = (GeoNodeCurveFilletMode)storage.mode;
  NodeSocket *poly_socket = static_cast<NodeSocket *>(node->inputs.first)->next;
  dunee::nodeSetSocketAvailability(ntree, poly_socket, mode == GEO_NODE_CURVE_FILLET_POLY);
}

static bke::CurvesGeometry fillet_curve(const dunee::CurvesGeo &src_curves,
                                        const GeoNodeCurveFilletMode mode,
                                        const fn::FieldCxt &field_cxt,
                                        const std::optional<Field<int>> &count_field,
                                        const Field<float> &radius_field,
                                        const bool limit_radius,
                                        const AnonymousAttributePropagationInfo &propagation_info)
{
  fn::FieldEvaluator evaluator{field_cxt, src_curves.points_num()};
  evaluator.add(radius_field);

  switch (mode) {
    case GEO_NODE_CURVE_FILLET_BEZIER: {
      evaluator.eval();
      return geo::fillet_curves_bezier(src_curves,
                                       src_curves.curves_range(),
                                       evaluator.get_eval<float>(0),
                                       limit_radius,
                                       propagation_info);
    }
    case GEO_NODE_CURVE_FILLET_POLY: {
      evaluator.add(*count_field);
      evaluator.eval();
      return geo::fillet_curves_poly(src_curves,
                                     src_curves.curves_range(),
                                     evaluator.get_eval<float>(0),
                                     evaluator.get_eval<int>(1),
                                     limit_radius,
                                     propagation_info);
    }
  }
  return dune::CurvesGeo();
}

static void fillet_pen(Pen &pen,
                       const GeoNodeCurveFilletMode mode,
                       const std::optional<Field<int>> &count_field,
                       const Field<float> &radius_field,
                       const bool limit_radius,
                       const AnonymousAttrPropagationInfo &propagation_info)
{
  using namespace dune::pen;
  for (const int layer_index : pen.layers().index_range()) {
    Drawing *drawing = get_eval_pen_layer_drawing_for_write(pen, layer_index);
    if (drawing == nullptr) {
      continue;
    }
    const dune::CurvesGeo &src_curves = drawing->strokes();
    if (src_curves.points_num() == 0) {
      continue;
    }
    const dune::PenLayerFieldContext field_cxt(
        pen, AttrDomain::Curve, layer_index);
    dune::CurvesGeo dst_curves = fillet_curve(src_curves,
                                              mode,
                                              field_context,
                                              count_field,
                                              radius_field,
                                              limit_radius,
                                              propagation_info);
    drawing->strokes_for_write() = std::move(dst_curves);
    drawing->tag_topology_changed();
  }
}

static void node_geo_ex(GeoNodeExParams params)
{
  GeoSet geo_set = params.extract_input<GeoSet>("Curve");

  const NodeGeoCurveFillet &storage = node_storage(params.node());
  const GeoNodeCurveFilletMode mode = (GeoNodeCurveFilletMode)storage.mode;

  Field<float> radius_field = params.extract_input<Field<float>>("Radius");
  const bool limit_radius = params.extract_input<bool>("Limit Radius");

  std::optional<Field<int>> count_field;
  if (mode == GEO_NODE_CURVE_FILLET_POLY) {
    count_field.emplace(params.extract_input<Field<int>>("Count"));
  }

  const AnonymousAttrPropInfo &prop_info = params.get_output_prop_info(
      "Curve");

  geo_set.mod_geo_sets([&](GeoSet &geo_set) {
    if (geo_set.has_curves()) {
      const Curves &curves_id = *geo_set.get_curves();
      const dune::CurvesGeo &src_curves = curves_id.geometry.wrap();
      const dune::CurvesFieldCxt field_cxt{src_curves, AttrDomain::Point};
      dune::CurvesGeo dst_curves = fillet_curve(src_curves,
                                                mode,
                                                field_context,
                                                count_field,
                                                radius_field,
                                                    limit_radius,
                                                    propagation_info);
      Curves *dst_curves_id = dune::curves_new_nomain(std::move(dst_curves));
      dunee::curves_copy_params(curves_id, *dst_curves_id);
      geo_set.replace_curves(dst_curves_id);
    }
    if (geometry_set.has_grease_pencil()) {
      Pen &grease_pencil = *geometry_set.get_grease_pencil_for_write();
      fillet_grease_pencil(
          grease_pencil, mode, count_field, radius_field, limit_radius, propagation_info);
    }
  });

  params.set_output("Curve", std::move(geometry_set));
}

static void node_rna(StructRNA *srna)
{
  static EnumPropertyItem mode_items[] = {
      {GEO_NODE_CURVE_FILLET_BEZIER,
       "BEZIER",
       0,
       "Bezier",
       "Align Bezier handles to create circular arcs at each control point"},
      {GEO_NODE_CURVE_FILLET_POLY,
       "POLY",
       0,
       "Poly",
       "Add control points along a circular arc (handle type is vector if Bezier Spline)"},
      {0, nullptr, 0, nullptr, nullptr},
  };

  RNA_def_node_enum(srna,
                    "mode",
                    "Mode",
                    "How to choose number of vertices on fillet",
                    mode_items,
                    NOD_storage_enum_accessors(mode),
                    GEO_NODE_CURVE_FILLET_BEZIER);
}

static void node_register()
{
  static bNodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_FILLET_CURVE, "Fillet Curve", NODE_CLASS_GEOMETRY);
  ntype.draw_buttons = node_layout;
  node_type_storage(
      &ntype, "NodeGeometryCurveFillet", node_free_standard_storage, node_copy_standard_storage);
  ntype.declare = node_declare;
  ntype.initfunc = node_init;
  ntype.updatefunc = node_update;
  ntype.geometry_node_execute = node_geo_exec;
  nodeRegisterType(&ntype);

  node_rna(ntype.rna_ext.srna);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_curve_fillet_cc
