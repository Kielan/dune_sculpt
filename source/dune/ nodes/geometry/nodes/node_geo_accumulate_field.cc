#include "dune_attribute_math.hh"

#include "lib_array.hh"
#include "lib_generic_virtual_array.hh"
#include "lib_virtual_array.hh"

#include "node_api_define.hh"
#include "node_socket_search_link.hh"

#include "api_enum_types.hh"

#include "node_geometry_util.hh"

#include "ui.hh"
#include "ui_resources.hh"

namespace dune::nodes::node_geo_accumulate_field_cc {

NODE_STORAGE_FNS(NodeAccumulateField)

static void node_declare(NodeDeclarationBuilder &b)
{
  const Node *node = b.node_or_null();

  if (node != nullptr) {
    const eCustomDataType data_type = eCustomDataType(node_storage(*node).data_type);
    BaseSocketDeclarationBuilder *val_declaration = nullptr;
    switch (data_type) {
      case CD_PROP_FLOAT3:
        val_declaration = &b.add_input<decl::Vector>("Val").default_val({1.0f, 1.0f, 1.0f});
        break;
      case CD_PROP_FLOAT:
        val_declaration = &b.add_input<decl::Float>("Val").default_val(1.0f);
        break;
      case CD_PROP_INT32:
        val_declaration = &b.add_input<decl::Int>("Val").default_val(1);
        break;
      default:
        lib_assert_unreachable();
        break;
    }
    val_declaration->supports_field().description(N_("The values to be accumulated"));
  }

  b.add_input<decl::Int>("Group Id", "Group Index")
      .supports_field()
      .description("An index used to group vals together for multiple separate accumulations");

  if (node != nullptr) {
    const eCustomDataType data_type = eCustomDataType(node_storage(*node).data_type);
    b.add_output(data_type, "Leading")
        .field_src_ref_all()
        .description(N_("The running total of vals in the corresponding group, starting at the "
                        "first value"));
    b.add_output(data_type, "Trailing")
        .field_src_ref_all()
        .description(
            N_("The running total of values in the corresponding group, starting at zero"));
    b.add_output(data_type, "Total")
        .field_src_ref_all()
        .description(N_("The total of all of the values in the corresponding group"));
  }
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, PointerRNA *ptr)
{
  uiItemR(layout, ptr, "data_type", UI_ITEM_NONE, "", ICON_NONE);
  uiItemR(layout, ptr, "domain", UI_ITEM_NONE, "", ICON_NONE);
}

static void node_init(bNodeTree * /*tree*/, bNode *node)
{
  NodeAccumulateField *data = MEM_cnew<NodeAccumulateField>(__func__);
  data->data_type = CD_PROP_FLOAT;
  data->domain = int16_t(AttrDomain::Point);
  node->storage = data;
}

enum class AccumulationMode { Leading = 0, Trailing = 1 };

static std::optional<eCustomDataType> node_type_from_other_socket(const NodeSocket &socket)
{
  switch (socket.type) {
    case SOCK_FLOAT:
      return CD_PROP_FLOAT;
    case SOCK_BOOLEAN:
    case SOCK_INT:
      return CD_PROP_INT32;
    case SOCK_VECTOR:
    case SOCK_RGBA:
      return CD_PROP_FLOAT3;
    default:
      return {};
  }
}

static void node_gather_link_searches(GatherLinkSearchOpParams &params)
{
  const NodeDeclaration &declaration = *params.node_type().static_declaration;
  search_link_ops_for_declarations(params, declaration.inputs);

  const std::optional<eCustomDataType> type = node_type_from_other_socket(params.other_socket());
  if (!type) {
    return;
  }
  if (params.in_out() == SOCK_OUT) {
    params.add_item(
        IFACE_("Leading"),
        [type](LinkSearchOpParams &params) {
          bNode &node = params.add_node("GeometryNodeAccumulateField");
          node_storage(node).data_type = *type;
          params.update_and_connect_available_socket(node, "Leading");
        },
        0);
    params.add_item(
        IFACE_("Trailing"),
        [type](LinkSearchOpParams &params) {
          Node &node = params.add_node("GeometryNodeAccumulateField");
          node_storage(node).data_type = *type;
          params.update_and_connect_available_socket(node, "Trailing");
        },
        -1);
    params.add_item(
        IFACE_("Total"),
        [type](LinkSearchOpParams &params) {
          bNode &node = params.add_node("GeometryNodeAccumulateField");
          node_storage(node).data_type = *type;
          params.update_and_connect_available_socket(node, "Total");
        },
        -2);
  }
  else {
    params.add_item(
        IFACE_("Value"),
        [type](LinkSearchOpParams &params) {
          Node &node = params.add_node("GeometryNodeAccumulateField");
          node_storage(node).data_type = *type;
          params.update_and_connect_available_socket(node, "Val");
        },
        0);
  }
}

class AccumulateFieldInput final : public dune::GeometryFieldInput {
 private:
  GField input_;
  Field<int> group_index_;
  AttrDomain source_domain_;
  AccumulationMode accumulation_mode_;

 public:
  AccumulateFieldInput(const AttrDomain source_domain,
                       GField input,
                       Field<int> group_index,
                       AccumulationMode accumulation_mode)
      : dube::GeometryFieldInput(input.cpp_type(), "Accumulation"),
        input_(input),
        group_index_(group_index),
        source_domain_(source_domain),
        accumulation_mode_(accumulation_mode)
  {
  }

  GVArray get_var_for_cxt(const dune::GeometryFieldCxt &cxt,
                          const IndexMask & /*mask*/) const final
  {
    const AttrAccessor attrs = *cxt.attrs();
    const int64_t domain_size = attrs.domain_size(src_domain_);
    if (domain_size == 0) {
      return {};
    }

    const dune::GeometryFieldCxt src_cxt{cxt, src_domain_};
    fn::FieldEval eval{src_cxt, domain_size};
    eval.add(input_);
    eval.add(group_index_);
    eval.eval();
    const GVArray g_vals = eval.get_eval(0);
    const VArray<int> group_indices = evaluator.get_eval<int>(1);

    GVArray g_output;

    dune::attr_math::convert_to_static_type(g_vals.type(), [&](auto dummy) {
      using T = decltype(dummy);
      if constexpr (is_same_any_v<T, int, float, float3>) {
        Array<T> outputs(domain_size);
        const VArray<T> vals = g_vals.typed<T>();

        if (group_indices.is_single()) {
          T accumulation = T();
          if (accumulation_mode_ == AccumulationMode::Leading) {
            for (const int i : values.index_range()) {
              accumulation = values[i] + accumulation;
              outputs[i] = accumulation;
            }
          }
          else {
            for (const int i : values.index_range()) {
              outputs[i] = accumulation;
              accumulation = values[i] + accumulation;
            }
          }
        }
        else {
          Map<int, T> accumulations;
          if (accumulation_mode_ == AccumulationMode::Leading) {
            for (const int i : values.index_range()) {
              T &accumulation_value = accumulations.lookup_or_add_default(group_indices[i]);
              accumulation_value += values[i];
              outputs[i] = accumulation_value;
            }
          }
          else {
            for (const int i : values.index_range()) {
              T &accumulation_value = accumulations.lookup_or_add_default(group_indices[i]);
              outputs[i] = accumulation_value;
              accumulation_value += values[i];
            }
          }
        }

        g_output = VArray<T>::ForContainer(std::move(outputs));
      }
    });

    return attributes.adapt_domain(std::move(g_output), source_domain_, context.domain());
  }

  uint64_t hash() const override
  {
    return get_default_hash_4(input_, group_index_, source_domain_, accumulation_mode_);
  }

  bool is_equal_to(const fn::FieldNode &other) const override
  {
    if (const AccumulateFieldInput *other_accumulate = dynamic_cast<const AccumulateFieldInput *>(
            &other))
    {
      return input_ == other_accumulate->input_ &&
             group_index_ == other_accumulate->group_index_ &&
             source_domain_ == other_accumulate->source_domain_ &&
             accumulation_mode_ == other_accumulate->accumulation_mode_;
    }
    return false;
  }

  std::optional<AttrDomain> preferred_domain(
      const GeometryComponent & /*component*/) const override
  {
    return source_domain_;
  }
};

class TotalFieldInput final : public bke::GeometryFieldInput {
 private:
  GField input_;
  Field<int> group_index_;
  AttrDomain source_domain_;

 public:
  TotalFieldInput(const AttrDomain source_domain, GField input, Field<int> group_index)
      : dube::GeometryFieldInput(input.cpp_type(), "Total Value"),
        input_(input),
        group_index_(group_index),
        source_domain_(source_domain)
  {
  }

  GVArray get_varray_for_cxt(const dune::GeometryFieldCxt &cxt,
                             const IndexMask & /*mask*/) const final
  {
    const AttrAccessor attrs = *cxt.attrs();
    const int64_t domain_size = attrs.domain_size(src_domain_);
    if (domain_size == 0) {
      return {};
    }

    const dune::GeometryFieldCxt src_cxt{cxt, src_domain_};
    fn::FieldEval eval{src_cxt, domain_size};
    eval.add(input_);
    eval.add(group_index_);
    eval.eval();
    const GVArray g_vals = eval.get_eval(0);
    const VArray<int> group_indices = eval.get_eval<int>(1);

    GVArray g_outputs;

    dune::attribute_math::convert_to_static_type(g_vals.type(), [&](auto dummy) {
      using T = decltype(dummy);
      if constexpr (is_same_any_v<T, int, float, float3>) {
        const VArray<T> vals = g_vals.typed<T>();
        if (group_indices.is_single()) {
          T accumulation = {};
          for (const int i : values.index_range()) {
            accumulation = values[i] + accumulation;
          }
          g_outputs = VArray<T>::ForSingle(accumulation, domain_size);
        }
        else {
          Map<int, T> accumulations;
          for (const int i : vals.index_range()) {
            T &value = accumulations.lookup_or_add_default(group_indices[i]);
            value = value + values[i];
          }
          Array<T> outputs(domain_size);
          for (const int i : vals.index_range()) {
            outputs[i] = accumulations.lookup(group_indices[i]);
          }
          g_outputs = VArray<T>::ForContainer(std::move(outputs));
        }
      }
    });

    return attrs.adapt_domain(std::move(g_outputs), src_domain_, cxt.domain());
  }

  uint64_t hash() const override
  {
    return get_default_hash_3(input_, group_index_, source_domain_);
  }

  bool is_equal_to(const fn::FieldNode &other) const override
  {
    if (const TotalFieldInput *other_field = dynamic_cast<const TotalFieldInput *>(&other)) {
      return input_ == other_field->input_ && group_index_ == other_field->group_index_ &&
             source_domain_ == other_field->source_domain_;
    }
    return false;
  }

  std::optional<AttrDomain> preferred_domain(
      const GeometryComponent & /*component*/) const override
  {
    return source_domain_;
  }
};

static void node_geo_exec(GeoNodeExParams params)
{
  const NodeAccumulateField &storage = node_storage(params.node());
  const AttrDomain source_domain = AttrDomain(storage.domain);

  const Field<int> group_index_field = params.extract_input<Field<int>>("Group Index");
  const GField input_field = params.extract_input<GField>("Val");
  if (params.output_is_required("Leading")) {
    params.set_output<GField>(
        "Leading",
        GField{std::make_shared<AccumulateFieldInput>(
            source_domain, input_field, group_index_field, AccumulationMode::Leading)});
  }
  if (params.output_is_required("Trailing")) {
    params.set_output<GField>(
        "Trailing",
        GField{std::make_shared<AccumulateFieldInput>(
            source_domain, input_field, group_index_field, AccumulationMode::Trailing)});
  }
  if (params.output_is_required("Total")) {
    params.set_output<GField>(
        "Total",
        GField{std::make_shared<TotalFieldInput>(source_domain, input_field, group_index_field)});
  }
}

static void node_api(ApiStruct *sapi)
{
  api_def_node_enum(
      sapi,
      "data_type",
      "Data Type",
      "Type of data stored in attr",
      api_enum_attribute_type_items,
      node_storage_enum_accessors(data_type),
      CD_PROP_FLOAT,
      [](Cxt * /*C*/, ApiPtr * /*ptr*/, ApiProp * /*prop*/, bool *r_free) {
        *r_free = true;
        return enum_items_filter(rna_enum_attr_type_items, [](const EnumPropItem &item) {
          return elem(item.val, CD_PROP_FLOAT, CD_PROP_FLOAT3, CD_PROP_INT32);
        });
      });

  api_def_node_enum(sapi,
                    "domain",
                    "Domain",
                    "",
                    api_enum_attr_domain_items,
                    NOD_storage_enum_accessors(domain),
                    int(AttrDomain::Point),
                    enums::domain_experimental_pen_v3_fn);
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_ACCUMULATE_FIELD, "Accumulate Field", NODE_CLASS_CONVERTER);
  ntype.geometry_node_ex = node_geo_ex;
  ntype.initfn = node_init;
  ntype.drw_btns = node_layout;
  ntype.declare = node_declare;
  ntype.gather_link_search_ops = node_gather_link_searches;
  node_type_storage(
      &ntype, "NodeAccumulateField", node_free_standard_storage, node_copy_standard_storage);
  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_accumulate_field_cc
