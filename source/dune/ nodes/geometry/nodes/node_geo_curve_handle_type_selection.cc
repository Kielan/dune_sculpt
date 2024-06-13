#include "dune_curves.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_curve_handle_type_sel_cc {

NODE_STORAGE_FNS(NodeGeoCurveSelHandles)

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_output<decl::Bool>("Selection").field_source();
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiItemR(layout, ptr, "mode", UI_ITEM_R_EXPAND, nullptr, ICON_NONE);
  uiItemR(layout, ptr, "handle_type", UI_ITEM_NONE, "", ICON_NONE);
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  NodeGeoCurveSelHandles *data = mem_cnew<NodeGeoCurveSelHandles>(__func__);

  data->handle_type = GEO_NODE_CURVE_HANDLE_AUTO;
  data->mode = GEO_NODE_CURVE_HANDLE_LEFT | GEO_NODE_CURVE_HANDLE_RIGHT;
  node->storage = data;
}

static HandleType handle_type_from_input_type(const GeoNodeCurveHandleType type)
{
  switch (type) {
    case GEO_NODE_CURVE_HANDLE_AUTO:
      return BEZIER_HANDLE_AUTO;
    case GEO_NODE_CURVE_HANDLE_ALIGN:
      return BEZIER_HANDLE_ALIGN;
    case GEO_NODE_CURVE_HANDLE_FREE:
      return BEZIER_HANDLE_FREE;
    case GEO_NODE_CURVE_HANDLE_VECTOR:
      return BEZIER_HANDLE_VECTOR;
  }
  lib_assert_unreachable();
  return BEZIER_HANDLE_AUTO;
}

static void sel_by_handle_type(const dune::CurvesGeo &curves,
                               const HandleType type,
                               const GeoNodeCurveHandleMode mode,
                               const MutableSpan<bool> r_sel)
{
  const OffsetIndices points_by_curve = curves.points_by_curve();
  VArray<int8_t> curve_types = curves.curve_types();
  VArray<int8_t> left = curves.handle_types_left();
  VArray<int8_t> right = curves.handle_types_right();

  for (const int i_curve : curves.curves_range()) {
    const IndexRange points = points_by_curve[i_curve];
    if (curve_types[i_curve] != CURVE_TYPE_BEZIER) {
      r_sel.slice(points).fill(false);
    }
    else {
      for (const int i_point : points) {
        r_sel[i_point] = (mode & GEO_NODE_CURVE_HANDLE_LEFT && left[i_point] == type) ||
                         (mode & GEO_NODE_CURVE_HANDLE_RIGHT && right[i_point] == type);
      }
    }
  }
}

class HandleTypeFieldInput final : public dune::CurvesFieldInput {
  HandleType type_;
  GeoNodeCurveHandleMode mode_;

 public:
  HandleTypeFieldInput(HandleType type, GeometryNodeCurveHandleMode mode)
      : dune::CurvesFieldInput(CPPType::get<bool>(), "Handle Type Selection node"),
        type_(type),
        mode_(mode)
  {
    category_ = Category::Generated;
  }

  GVArray get_varray_for_cxt(const dune::CurvesGeo &curves,
                                 const AttrDomain domain,
                                 const IndexMask &mask) const final
  {
    if (domain != AttrDomain::Point) {
      return {};
    }
    Array<bool> sel(mask.min_array_size());
    sel_by_handle_type(curves, type_, mode_, sel);
    return VArray<bool>::ForContainer(std::move(sel));
  }

  uint64_t hash() const override
  {
    return get_default_hash_2(int(mode_), int(type_));
  }

  bool is_equal_to(const fn::FieldNode &other) const override
  {
    if (const HandleTypeFieldInput *other_handle_sel =
            dynamic_cast<const HandleTypeFieldInput *>(&other))
    {
      return mode_ == other_handle_sel->mode_ && type_ == other_handle_sel->type_;
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
  const NodeGeoCurveSelHandles &storage = node_storage(params.node());
  const HandleType handle_type = handle_type_from_input_type(
      (GeoNodeCurveHandleType)storage.handle_type);
  const GeoNodeCurveHandleMode mode = (GeoNodeCurveHandleMode)storage.mode;

  Field<bool> sel_field{std::make_shared<HandleTypeFieldInput>(handle_type, mode)};
  params.set_output("Sel", std::move(sel_field));
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(
      &ntype, GEO_NODE_CURVE_HANDLE_TYPE_SEL, "Handle Type Selection", NODE_CLASS_INPUT);
  ntype.decl = node_declare;
  ntype.geo_node_ex = node_geo_ex;
  ntype.initfn = node_init;
  node_type_storage(&ntype,
                    "NodeGeoCurveSelHandles",
                    node_free_standard_storage,
                    node_copy_standard_storage);
  ntype.draw_btns = node_layout;

  nodeRegisterType(&ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_curve_handle_type_sel_cc
