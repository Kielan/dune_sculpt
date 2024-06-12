#include <algorithm>
#include <numeric>

#include "node_api_define.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "lib_array_utils.hh"
#include "lib_math_base_safe.h"

#include "api_socket_search_link.hh"

#include "api_enum_types.hh"

#include "node_geometry_util.hh"

namespace dune::nodes::node_geo_attr_statistic_cc {

static void node_decl(NodeDeclBuilder &b)
{
  const Node *node = b.node_or_null();

  b.add_input<decl::Geo>("Geo");
  b.add_input<decl::Bool>("Sel").default_val(true).field_on_all().hide_val();

  if (node != nullptr) {
    const eCustomDataType data_type = eCustomDataType(node->custom1);
    b.add_input(data_type, "Attr").hide_val().field_on_all();

    b.add_output(data_type, "Mean");
    b.add_output(data_type, "Median");
    b.add_output(data_type, "Sum");
    b.add_output(data_type, "Min");
    b.add_output(data_type, "Max");
    b.add_output(data_type, "Range");
    b.add_output(data_type, "Standard Deviation");
    b.add_output(data_type, "Variance");
  }
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiItemR(layout, ptr, "data_type", UI_ITEM_NONE, "", ICON_NONE);
  uiItemR(layout, ptr, "domain", UI_ITEM_NONE, "", ICON_NONE);
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  node->custom1 = CD_PROP_FLOAT;
  node->custom2 = int16_t(AttrDomain::Point);
}

static std::optional<eCustomDataType> node_type_from_other_socket(const NodeSocket &socket)
{
  switch (socket.type) {
    case SOCK_FLOAT:
    case SOCK_BOOL:
    case SOCK_INT:
      return CD_PROP_FLOAT;
    case SOCK_VECTOR:
    case SOCK_RGBA:
      return CD_PROP_FLOAT3;
    default:
      return {};
  }
}

static void node_gather_link_searches(GatherLinkSearchOpParams &params)
{
  const NodeType &node_type = params.node_type();
  const NodeDeclaration &decl = *params.node_type().static_declaration;
  search_link_ops_for_declarations(params, declaration.inputs);

  const std::optional<eCustomDataType> type = node_type_from_other_socket(params.other_socket());
  if (!type) {
    return;
  }

  if (params.in_out() == SOCK_IN) {
    params.add_item(IFACE_("Attr"), [node_type, type](LinkSearchOpParams &params) {
      Node &node = params.add_node(node_type);
      node.custom1 = *type;
      params.update_and_connect_available_socket(node, "Attr");
    });
  }
  else {
    for (const StringRefNull name :
         {"Mean", "Median", "Sum", "Min", "Max", "Range", "Standard Deviation", "Variance"})
    {
      params.add_item(IFACE_(name.c_str()), [node_type, name, type](LinkSearchOpParams &params) {
        Node &node = params.add_node(node_type);
        node.custom1 = *type;
        params.update_and_connect_available_socket(node, name);
      });
    }
  }
}

template<typename T> static T compute_sum(const Span<T> data)
{
  return std::accumulate(data.begin(), data.end(), T());
}

static float compute_variance(const Span<float> data, const float mean)
{
  if (data.size() <= 1) {
    return 0.0f;
  }

  float sum_of_squared_diffs = std::accumulate(
      data.begin(), data.end(), 0.0f, [mean](float accumulator, float val) {
        float diff = mean - val;
        return accumulator + diff * diff;
      });

  return sum_of_squared_diffs / data.size();
}

static float median_of_sorted_span(const Span<float> data)
{
  if (data.is_empty()) {
    return 0.0f;
  }

  const float median = data[data.size() / 2];

  /* For spans of even length, the median is the avg of the middle 2 elems. */
  if (data.size() % 2 == 0) {
    return (median + data[data.size() / 2 - 1]) * 0.5f;
  }
  return median;
}

static void node_geo_ex(GeoNodeExParams params)
{
  GeometrySet geometry_set = params.get_input<GeoSet>("Geo");
  const Node &node = params.node();
  const eCustomDataType data_type = eCustomDataType(node.custom1);
  const AttrDomain domain = AttrDomain(node.custom2);
  Vector<const GeometryComponent *> components = geo_set.get_components();

  const Field<bool> sel_field = params.get_input<Field<bool>>("Sel");

  switch (data_type) {
    case CD_PROP_FLOAT: {
      const Field<float> input_field = params.get_input<Field<float>>("Attr");
      Vector<float> data;
      for (const GeometryComponent *component : components) {
        const std::optional<AttrAccessor> attrs = component->attrs();
        if (!attrs.has_val()) {
          continue;
        }
        if (attrs->domain_supported(domain)) {
          const dune::GeoFieldCxt field_cxt{*component, domain};
          fn::FieldEval data_eval{field_cxt, attrs->domain_size(domain)};
          data_eval.add(input_field);
          data_eval.set_sel(sel_field);
          data_eval.eval();
          const VArray<float> component_data = data_eval.get_eval<float>(0);
          const IndexMask sel = data_eval.get_eval_sel_as_mask();

          const int next_data_index = data.size();
          data.resize(next_data_index + sel.size());
          MutableSpan<float> sel_data = data.as_mutable_span().slice(next_data_index,
                                                                     sel.size());
          array_utils::gather(component_data, sel, sel_data);
        }
      }

      float mean = 0.0f;
      float median = 0.0f;
      float sum = 0.0f;
      float min = 0.0f;
      float max = 0.0f;
      float range = 0.0f;
      float standard_deviation = 0.0f;
      float variance = 0.0f;
      const bool sort_required = params.output_is_required("Min") ||
                                 params.output_is_required("Max") ||
                                 params.output_is_required("Range") ||
                                 params.output_is_required("Median");
      const bool sum_required = params.output_is_required("Sum") ||
                                params.output_is_required("Mean");
      const bool variance_required = params.output_is_required("Standard Deviation") ||
                                     params.output_is_required("Variance");

      if (data.size() != 0) {
        if (sort_required) {
          std::sort(data.begin(), data.end());
          median = median_of_sorted_span(data);

          min = data.first();
          max = data.last();
          range = max - min;
        }
        if (sum_required || variance_required) {
          sum = compute_sum<float>(data);
          mean = sum / data.size();

          if (variance_required) {
            variance = compute_variance(data, mean);
            standard_deviation = std::sqrt(variance);
          }
        }
      }

      if (sum_required) {
        params.set_output("Sum", sum);
        params.set_output("Mean", mean);
      }
      if (sort_required) {
        params.set_output("Min", min);
        params.set_output("Max", max);
        params.set_output("Range", range);
        params.set_output("Median", median);
      }
      if (variance_required) {
        params.set_output("Standard Deviation", standard_deviation);
        params.set_output("Variance", variance);
      }
      break;
    }
    case CD_PROP_FLOAT3: {
      const Field<float3> input_field = params.get_input<Field<float3>>("Attribute");
      Vector<float3> data;
      for (const GeoComponent *component : components) {
        const std::optional<AttrAccessor> attrs = component->attributes();
        if (!attrs.has_val()) {
          continue;
        }
        if (attrs->domain_supported(domain)) {
          const dune::GeoFieldCxt field_cxt{*component, domain};
          fn::FieldEval data_eval{field_cxt, attrs->domain_size(domain)};
          data_eval.add(input_field);
          data_eval.set_sel(sel_field);
          data_eval.eval();
          const VArray<float3> component_data = data_eval.get_eval<float3>(0);
          const IndexMask sel = data_eval.get_eval_sel_as_mask();

          const int next_data_index = data.size();
          data.resize(data.size() + sel.size());
          MutableSpan<float3> sel_data = data.as_mutable_span().slice(next_data_index,
                                                                      sel.size());
          array_utils::gather(component_data, sel, sel_data);
        }
      }

      float3 median{0};
      float3 min{0};
      float3 max{0};
      float3 range{0};
      float3 sum{0};
      float3 mean{0};
      float3 variance{0};
      float3 standard_deviation{0};
      const bool sort_required = params.output_is_required("Min") ||
                                 params.output_is_required("Max") ||
                                 params.output_is_required("Range") ||
                                 params.output_is_required("Median");
      const bool sum_required = params.output_is_required("Sum") ||
                                params.output_is_required("Mean");
      const bool variance_required = params.output_is_required("Standard Deviation") ||
                                     params.output_is_required("Variance");

      Array<float> data_x;
      Array<float> data_y;
      Array<float> data_z;
      if (sort_required || variance_required) {
        data_x.reinitialize(data.size());
        data_y.reinitialize(data.size());
        data_z.reinitialize(data.size());
        for (const int i : data.index_range()) {
          data_x[i] = data[i].x;
          data_y[i] = data[i].y;
          data_z[i] = data[i].z;
        }
      }

      if (data.size() != 0) {
        if (sort_required) {
          std::sort(data_x.begin(), data_x.end());
          std::sort(data_y.begin(), data_y.end());
          std::sort(data_z.begin(), data_z.end());

          const float x_median = median_of_sorted_span(data_x);
          const float y_median = median_of_sorted_span(data_y);
          const float z_median = median_of_sorted_span(data_z);
          median = float3(x_median, y_median, z_median);

          min = float3(data_x.first(), data_y.first(), data_z.first());
          max = float3(data_x.last(), data_y.last(), data_z.last());
          range = max - min;
        }
        if (sum_required || variance_required) {
          sum = compute_sum(data.as_span());
          mean = sum / data.size();

          if (variance_required) {
            const float x_variance = compute_variance(data_x, mean.x);
            const float y_variance = compute_variance(data_y, mean.y);
            const float z_variance = compute_variance(data_z, mean.z);
            variance = float3(x_variance, y_variance, z_variance);
            standard_deviation = float3(
                std::sqrt(variance.x), std::sqrt(variance.y), std::sqrt(variance.z));
          }
        }
      }

      if (sum_required) {
        params.set_output("Sum", sum);
        params.set_output("Mean", mean);
      }
      if (sort_required) {
        params.set_output("Min", min);
        params.set_output("Max", max);
        params.set_output("Range", range);
        params.set_output("Median", median);
      }
      if (variance_required) {
        params.set_output("Standard Deviation", standard_deviation);
        params.set_output("Variance", variance);
      }
      break;
    }
    default:
      break;
  }
}

static void node_api(ApiStruct *sapi)
{
  api_def_node_enum(
      sapi,
      "data_type",
      "Data Type",
      "The data type the attr is converted to before calculating the results",
      api_enum_attr_type_items,
      node_inline_enum_accessors(custom1),
      CD_PROP_FLOAT,
      [](Cxt * /*C*/, ApiPtr * /*ptr*/, ApiProp * /*prop*/, bool *r_free) {
        *r_free = true;
        return enum_items_filter(api_enum_attr_type_items, [](const EnumPropItem &item) {
          return elem(item.val, CD_PROP_FLOAT, CD_PROP_FLOAT3);
        });
      });

  api_def_node_enum(sapi,
                    "domain",
                    "Domain",
                    "Which domain to read the data from",
                    api_enum_attr_domain_items,
                    node_inline_enum_accessors(custom2),
                    int(AttrDomain::Point),
                    enums::domain_experimental_pen_v3_fn);
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(
      &ntype, GEO_NODE_ATTR_STATISTIC, "Attr Statistic", NODE_CLASS_ATTR);

  ntype.initfn = node_init;
  ntype.decl = node_decl;
  ntype.geo_node_ex = node_geo_ex;
  ntype.drw_btns = node_layout;
  ntype.gather_link_search_ops = node_gather_link_searches;
  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_attr_statistic_cc
