#include "node_geom_util.hh"

#include "NOD_api_define.hh"

#include "ui.hh"
#include "ui_resources.hh"

#include "dune_attribute_math.hh"

#include "lib_task.hh"

#include "api_enum_types.hh"

#include "NOD_socket_search_link.hh"

namespace dune::nodes {

EvalAtIndexInput::EvalAtIndexInput(Field<int> index_field,
                                           GField val_field,
                                           AttrDomain val_field_domain)
    : dune::GeomFieldInput(val_field.cpp_type(), "Eval at Index"),
      index_field_(std::move(index_field)),
      val_field_(std::move(val_field)),
      val_field_domain_(val_field_domain)
{
}

GVArray EvalAtIndexInput::get_varr_for_cxt(const dune::GeometryFieldCxt &cxt,
                                                     const IndexMask &mask) const
{
  const std::optional<AttributeAccessor> attributes = cxt.attrs();
  if (!attributes) {
    return {};
  }

  const dune::GeometryFieldCxt val_cxt{cxt, val_field_domain_};
  FieldEvaluator val_evaluator{val_cxt, attributeqs->domain_size(value_field_domain_)};
  val_evaluator.add(val_field_);
  val_evaluator.eval();
  const GVArray &vals = val_evaluator.get_eval(0);

  FieldEvaluator index_evaluator{cxt, &mask};
  index_evaluator.add(index_field_);
  index_evaluator.eval();
  const VArray<int> indices = index_evaluator.get_eval<int>(0);

  GArray<> dst_arr(vals.type(), mask.min_arr_size());
  copy_with_checked_indices(vals, indices, mask, dst_arr);
  return GVArray::ForGArray(std::move(dst_array));
}

}  // namespace dune::nodes

namespace dune::nodes::node_geo_eval_at_index_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  const Node *node = b.node_or_null();

  b.add_input<decl::Int>("Index").min(0).supports_field();
  if (node != nullptr) {
    const eCustomDataType data_type = eCustomDataType(node->custom2);
    b.add_input(data_type, "Value").hide_value().supports_field();

    b.add_output(data_type, "Value").field_source_reference_all();
  }
}

static void node_layout(uiLayout *layout, bContext * /*C*/, PointerRNA *ptr)
{
  uiItemR(layout, ptr, "data_type", UI_ITEM_NONE, "", ICON_NONE);
  uiItemR(layout, ptr, "domain", UI_ITEM_NONE, "", ICON_NONE);
}

static void node_init(bNodeTree * /*tree*/, bNode *node)
{
  node->custom1 = int(AttrDomain::Point);
  node->custom2 = CD_PROP_FLOAT;
}

static void node_gather_link_searches(GatherLinkSearchOpParams &params)
{
  const NodeType &node_type = params.node_type();
  const std::optional<eCustomDataType> type = dune::socket_type_to_custom_data_type(
      eNodeSocketDatatype(params.other_socket().type));
  if (type && *type != CD_PROP_STRING) {
    params.add_item(IFACE_("Val"), [node_type, type](LinkSearchOpParams &params) {
      Node &node = params.add_node(node_type);
      node.custom2 = *type;
      params.update_and_connect_available_socket(node, "Val");
    });
    params.add_item(
        IFACE_("Index"),
        [node_type, type](LinkSearchOpParams &params) {
          bNode &node = params.add_node(node_type);
          node.custom2 = *type;
          params.update_and_connect_available_socket(node, "Index");
        },
        -1);
  }
}

static void node_geo_exec(GeoNodeExecParams params)
{
  const bNode &node = params.node();
  const AttrDomain domain = AttrDomain(node.custom1);

  GField output_field{std::make_shared<EvalAtIndexInput>(
      params.extract_input<Field<int>>("Index"), params.extract_input<GField>("Value"), domain)};
  params.set_output<GField>("Value", std::move(output_field));
}

static void node_api(ApiStruct *sapi)
{
  api_def_node_enum(sapi,
                    "domain",
                    "Domain",
                    "Domain the field is eval in",
                    api_enum_attr_domain_items,
                    NOD_inline_enum_accessors(custom1),
                    int(AttrDomain::Point),
                    enums::domain_experimental_pen_version3_fn);

  api_def_node_enum(sapi,
                    "data_type",
                    "Data Type",
                    "",
                    api_enum_attribute_type_items,
                    NOD_inline_enum_accessors(custom2),
                    CD_PROP_FLOAT,
                    enums::attr_type_type_with_socket_fn);
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(
      &ntype, GEO_NODE_EVAL_AT_INDEX, "Eval at Index", NODE_CLASS_CONVERTER);
  ntype.geom_node_ex = node_geo_ex;
  ntype.drw_btns = node_layout;
  ntype.initfn = node_init;
  ntype.decl = node_declare;
  ntype.gather_link_search_ops = node_gather_link_searches;
  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
NOD_REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_evaluate_at_index_cc
