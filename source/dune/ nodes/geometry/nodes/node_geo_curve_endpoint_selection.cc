#include "lib_task.hh"

#include "dune_curves.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_curve_endpoint_sel_cc {

static void node_decl(NodeDeclBuilder &b)
{
  b.add_input<decl::Int>("Start Size")
      .min(0)
      .default_val(1)
      .supports_field()
      .description("The amount of points to sel from the start of each spline");
  b.add_input<decl::Int>("End Size")
      .min(0)
      .default_val(1)
      .supports_field()
      .description("The amount of points to sel from the end of each spline");
  b.add_output<decl::Bool>("Sel")
      .field_src_ref_all()
      .description("The sel from the start and end of the splines based on the input sizes");
}

class EndpointFieldInput final : public dune::CurvesFieldInput {
  Field<int> start_size_;
  Field<int> end_size_;

 public:
  EndpointFieldInput(Field<int> start_size, Field<int> end_size)
      : dune::CurvesFieldInput(CPPType::get<bool>(), "Endpoint Sel node"),
        start_size_(start_size),
        end_size_(end_size)
  {
    category_ = Category::Generated;
  }

  GVArray get_varray_for_cxt(const dune::CurvesGeo &curves,
                                 const AttrDomain domain,
                                 const IndexMask & /*mask*/) const final
  {
    if (domain != AttrDomain::Point) {
      return {};
    }
    if (curves.points_num() == 0) {
      return {};
    }

    const dune::CurvesFieldCxt size_cxt{curves, AttrDomain::Curve};
    fn::FieldEval evaluator{size_cxt, curves.curves_num()};
    evaluator.add(start_size_);
    evaluator.add(end_size_);
    evaluator.eval();
    const VArray<int> start_size = eval.get_eval<int>(0);
    const VArray<int> end_size = eval.get_eval<int>(1);

    Array<bool> sel(curves.points_num(), false);
    MutableSpan<bool> sel_span = sel.as_mutable_span();
    const OffsetIndices points_by_curve = curves.points_by_curve();
    devirtualize_varray2(start_size, end_size, [&](const auto &start_size, const auto &end_size) {
      threading::parallel_for(curves.curves_range(), 1024, [&](IndexRange curves_range) {
        for (const int i : curves_range) {
          const IndexRange points = points_by_curve[i];
          const int start = std::max(start_size[i], 0);
          const int end = std::max(end_size[i], 0);

          selection_span.slice(points.take_front(start)).fill(true);
          selection_span.slice(points.take_back(end)).fill(true);
        }
      });
    });

    return VArray<bool>::ForContainer(std::move(selection));
  };

  void for_each_field_input_recursive(FnRef<void(const FieldInput &)> fn) const override
  {
    start_size_.node().for_each_field_input_recursive(fn);
    end_size_.node().for_each_field_input_recursive(fn);
  }

  uint64_t hash() const override
  {
    return get_default_hash_2(start_size_, end_size_);
  }

  bool is_equal_to(const fn::FieldNode &other) const override
  {
    if (const EndpointFieldInput *other_endpoint = dynamic_cast<const EndpointFieldInput *>(
            &other))
    {
      return start_size_ == other_endpoint->start_size_ && end_size_ == other_endpoint->end_size_;
    }
    return false;
  }

  std::optional<AttrDomain> preferred_domain(const CurvesGeo & /*curves*/) const
  {
    return AttrDomain::Point;
  }
};

static void node_geo_ex(GeoNodeExParams params)
{
  Field<int> start_size = params.extract_input<Field<int>>("Start Size");
  Field<int> end_size = params.extract_input<Field<int>>("End Size");
  Field<bool> sel_field{std::make_shared<EndpointFieldInput>(start_size, end_size)};
  params.set_output("Sel", std::move(sel_field));
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(
      &ntype, GEO_NODE_CURVE_ENDPOINT_SEL, "Endpoint Sel", NODE_CLASS_INPUT);
  ntype.decl = node_decl;
  ntype.geo_node_ex = node_geo_ex;

  nodeRegisterType(&ntype);
}
REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_curve_endpoint_sel_cc
