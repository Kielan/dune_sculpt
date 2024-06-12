#include "lib_array.hh"
#include "lib_generic_array.hh"
#include "lib_index_mask.hh"
#include "lib_index_range.hh"
#include "lib_span.hh"
#include "lib_task.hh"
#include "lib_vector.hh"
#include "lib_virtual_array.hh"

#include "dune_attr_math.hh"
#include "dune_curves.hh"
#include "dune_geo_fields.hh"
#include "dune_pen.hh"
#include "dune_mesh.hh"
#include "dune_mesh_mapping.hh"

#include "node_api_define.hh"

#include "ui_interface.hh"
#include "ui_resources.hh"

#include "api_enum_types.hh"

#include "node_socket_search_link.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_blur_attr_cc {

static void node_decl(NodeDeclBuilder &b)
{
  const Node *node = b.node_or_null();

  if (node != nullptr) {
    const eCustomDataType data_type = eCustomDataType(node->custom1);
    b.add_input(data_type, "Val").supports_field().hide_value().is_default_link_socket();
  }
  b.add_input<decl::Int>("Iters")
      .default_val(1)
      .min(0)
      .description("How many times to blur the vals for all elems");
  b.add_input<decl::Float>("Weight")
      .default_val(1.0f)
      .subtype(PROP_FACTOR)
      .min(0.0f)
      .max(1.0f)
      .supports_field()
      .description("Relative mix weight of neighboring elems");

  if (node != nullptr) {
    const eCustomDataType data_type = eCustomDataType(node->custom1);
    b.add_output(data_type, "Val").field_src_ref_all().dependent_field();
  }
}

static void node_layout(uiLayout *layout, Cxt * /*C*/, ApiPtr *ptr)
{
  uiItemR(layout, ptr, "data_type", UI_ITEM_NONE, "", ICON_NONE);
}

static void node_init(NodeTree * /*tree*/, Node *node)
{
  node->custom1 = CD_PROP_FLOAT;
}

static void node_gather_link_searches(GatherLinkSearchOpParams &params)
{
  const NodeType &node_type = params.node_type();
  const NodeDecl &decl = *node_type.static_decl;

  /* Weight and Iters inputs don't change based on the data type. */
  search_link_ops_for_declarations(params, declaration.inputs);

  const std::optional<eCustomDataType> new_node_type = bke::socket_type_to_custom_data_type(
      eNodeSocketDatatype(params.other_socket().type));
  if (!new_node_type.has_value()) {
    return;
  }
  eCustomDataType fixed_data_type = *new_node_type;
  if (fixed_data_type == CD_PROP_STRING) {
    return;
  }
  if (fixed_data_type == CD_PROP_QUATERNION) {
    /* Don't implement quaternion blurring for now. */
    return;
  }
  if (fixed_data_type == CD_PROP_BOOL) {
    /* This node does not support boolean sockets, use integer instead. */
    fixed_data_type = CD_PROP_INT32;
  }
  params.add_item(IFACE_("Val"), [node_type, fixed_data_type](LinkSearchOpParams &params) {
    Node &node = params.add_node(node_type);
    node.custom1 = fixed_data_type;
    params.update_and_connect_available_socket(node, "Val");
  });
}

static void build_vert_to_vert_by_edge_map(const Span<int2> edges,
                                           const int verts_num,
                                           Array<int> &r_offsets,
                                           Array<int> &r_indices)
{
  dune::mesh::build_vert_to_edge_map(edges, verts_num, r_offsets, r_indices);
  const OffsetIndices<int> offsets(r_offsets);
  threading::parallel_for(IndexRange(verts_num), 2048, [&](const IndexRange range) {
    for (const int vert : range) {
      MutableSpan<int> neighbors = r_indices.as_mutable_span().slice(offsets[vert]);
      for (const int i : neighbors.index_range()) {
        neighbors[i] = dune::mesh::edge_other_vert(edges[neighbors[i]], vert);
      }
    }
  });
}

static void build_edge_to_edge_by_vert_map(const Span<int2> edges,
                                           const int verts_num,
                                           Array<int> &r_offsets,
                                           Array<int> &r_indices)
{
  Array<int> vert_to_edge_offset_data;
  Array<int> vert_to_edge_indices;
  const GroupedSpan<int> vert_to_edge = bke::mesh::build_vert_to_edge_map(
      edges, verts_num, vert_to_edge_offset_data, vert_to_edge_indices);
  const OffsetIndices<int> vert_to_edge_offsets(vert_to_edge_offset_data);

  r_offsets = Array<int>(edges.size() + 1, 0);
  threading::parallel_for(edges.index_range(), 1024, [&](const IndexRange range) {
    for (const int edge_i : range) {
      const int2 edge = edges[edge_i];
      r_offsets[edge_i] = vert_to_edge_offsets[edge[0]].size() - 1 +
                          vert_to_edge_offsets[edge[1]].size() - 1;
    }
  });
  const OffsetIndices offsets = offset_indices::accumulate_counts_to_offsets(r_offsets);
  r_indices.reinitialize(offsets.total_size());

  threading::parallel_for(edges.index_range(), 1024, [&](const IndexRange range) {
    for (const int edge_i : range) {
      const int2 edge = edges[edge_i];
      MutableSpan<int> neighbors = r_indices.as_mutable_span().slice(offsets[edge_i]);
      int count = 0;
      for (const Span<int> neighbor_edges : {vert_to_edge[edge[0]], vert_to_edge[edge[1]]}) {
        for (const int neighbor_edge : neighbor_edges) {
          if (neighbor_edge != edge_i) {
            neighbors[count] = neighbor_edge;
            count++;
          }
        }
      }
    }
  });
}

static void build_face_to_face_by_edge_map(const OffsetIndices<int> faces,
                                           const Span<int> corner_edges,
                                           const int edges_num,
                                           Array<int> &r_offsets,
                                           Array<int> &r_indices)
{
  Array<int> edge_to_face_offset_data;
  Array<int> edge_to_face_indices;
  const GroupedSpan<int> edge_to_face_map = bke::mesh::build_edge_to_face_map(
      faces, corner_edges, edges_num, edge_to_face_offset_data, edge_to_face_indices);
  const OffsetIndices<int> edge_to_face_offsets(edge_to_face_offset_data);

  r_offsets = Array<int>(faces.size() + 1, 0);
  threading::parallel_for(faces.index_range(), 4096, [&](const IndexRange range) {
    for (const int face_i : range) {
      for (const int edge : corner_edges.slice(faces[face_i])) {
        /* Subtract face itself from the number of faces connected to the edge. */
        r_offsets[face_i] += edge_to_face_offsets[edge].size() - 1;
      }
    }
  });
  const OffsetIndices<int> offsets = offset_indices::accumulate_counts_to_offsets(r_offsets);
  r_indices.reinitialize(offsets.total_size());

  threading::parallel_for(faces.index_range(), 1024, [&](IndexRange range) {
    for (const int face_i : range) {
      MutableSpan<int> neighbors = r_indices.as_mutable_span().slice(offsets[face_i]);
      if (neighbors.is_empty()) {
        continue;
      }
      int count = 0;
      for (const int edge : corner_edges.slice(faces[face_i])) {
        for (const int neighbor : edge_to_face_map[edge]) {
          if (neighbor != face_i) {
            neighbors[count] = neighbor;
            count++;
          }
        }
      }
    }
  });
}

static GroupedSpan<int> create_mesh_map(const Mesh &mesh,
                                        const AttrDomain domain,
                                        Array<int> &r_offsets,
                                        Array<int> &r_indices)
{
  switch (domain) {
    case AttrDomain::Point:
      build_vert_to_vert_by_edge_map(mesh.edges(), mesh.verts_num, r_offsets, r_indices);
      break;
    case AttrDomain::Edge:
      build_edge_to_edge_by_vert_map(mesh.edges(), mesh.verts_num, r_offsets, r_indices);
      break;
    case AttrDomain::Face:
      build_face_to_face_by_edge_map(
          mesh.faces(), mesh.corner_edges(), mesh.edges_num, r_offsets, r_indices);
      break;
    default:
      lib_assert_unreachable();
      break;
  }
  return {OffsetIndices<int>(r_offsets), r_indices};
}

template<typename T>
static Span<T> blur_on_mesh_ex(const Span<float> neighbor_weights,
                               const GroupedSpan<int> neighbors_map,
                               const int iters,
                               const MutableSpan<T> buf_a,
                               const MutableSpan<T> buf_b)
{
  /* Src is set to buf_b even tho it is actually in buf_a bc the loop below starts
   * with swapping both. */
  MutableSpan<T> src = buf_b;
  MutableSpan<T> dst = buf_a;

  for ([[maybe_unused]] const int64_t iter : IndexRange(iters)) {
    std::swap(src, dst);
    dune::attr_math::DefaultMixer<T> mixer{dst, IndexMask(0)};
    threading::parallel_for(dst.index_range(), 1024, [&](const IndexRange range) {
      for (const int64_t index : range) {
        const Span<int> neighbors = neighbors_map[index];
        const float neighbor_weight = neighbor_weights[index];
        mixer.set(index, src[index], 1.0f);
        for (const int neighbor : neighbors) {
          mixer.mix_in(index, src[neighbor], neighbor_weight);
        }
      }
      mixer.finalize(range);
    });
  }

  return dst;
}

static GSpan blur_on_mesh(const Mesh &mesh,
                          const AttrDomain domain,
                          const int iters,
                          const Span<float> neighbor_weights,
                          const GMutableSpan buf_a,
                          const GMutableSpan buf_b)
{
  Array<int> neighbor_offsets;
  Array<int> neighbor_indices;
  const GroupedSpan<int> neighbors_map = create_mesh_map(
      mesh, domain, neighbor_offsets, neighbor_indices);

  GSpan result_buf;
  dune::attr_math::convert_to_static_type(buf_a.type(), [&](auto dummy) {
    using T = decltype(dummy);
    if constexpr (!std::is_same_v<T, bool>) {
      result_buf = blur_on_mesh_ex<T>(
          neighbor_weights, neighbors_map, iters, buf_a.typed<T>(), buf_b.typed<T>());
    }
  });
  return result_buf;
}

template<typename T>
static Span<T> blur_on_curve_ex(const dune::CurvesGeo &curves,
                                const Span<float> neighbor_weights,
                                const int iters,
                                const MutableSpan<T> buf_a,
                                const MutableSpan<T> buf_b)
{
  MutableSpan<T> src = buf_b;
  MutableSpan<T> dst = buf_a;

  const OffsetIndices points_by_curve = curves.points_by_curve();
  const VArray<bool> cyclic = curves.cyclic();

  for ([[maybe_unused]] const int iter : IndexRange(iters)) {
    std::swap(src, dst);
    dune::attr_math::DefaultMixer<T> mixer{dst, IndexMask(0)};
    threading::parallel_for(curves.curves_range(), 256, [&](const IndexRange range) {
      for (const int curve_i : range) {
        const IndexRange points = points_by_curve[curve_i];
        if (points.size() == 1) {
          /* No mixing possible. */
          const int point_i = points[0];
          mixer.set(point_i, src[point_i], 1.0f);
          continue;
        }
        /* Inner points. */
        for (const int point_i : points.drop_front(1).drop_back(1)) {
          const float neighbor_weight = neighbor_weights[point_i];
          mixer.set(point_i, src[point_i], 1.0f);
          mixer.mix_in(point_i, src[point_i - 1], neighbor_weight);
          mixer.mix_in(point_i, src[point_i + 1], neighbor_weight);
        }
        const int first_i = points[0];
        const float first_neighbor_weight = neighbor_weights[first_i];
        const int last_i = points.last();
        const float last_neighbor_weight = neighbor_weights[last_i];

        /* First point. */
        mixer.set(first_i, src[first_i], 1.0f);
        mixer.mix_in(first_i, src[first_i + 1], first_neighbor_weight);
        /* Last point. */
        mixer.set(last_i, src[last_i], 1.0f);
        mixer.mix_in(last_i, src[last_i - 1], last_neighbor_weight);

        if (cyclic[curve_i]) {
          /* First point. */
          mixer.mix_in(first_i, src[last_i], first_neighbor_weight);
          /* Last point. */
          mixer.mix_in(last_i, src[first_i], last_neighbor_weight);
        }
      }
      mixer.finalize(points_by_curve[range]);
    });
  }

  return dst;
}

static GSpan blur_on_curves(const dune::CurvesGeo &curves,
                            const int iters,
                            const Span<float> neighbor_weights,
                            const GMutableSpan buf_a,
                            const GMutableSpan buf_b)
{
  GSpan result_buf;
  dune::attr_math::convert_to_static_type(buf_a.type(), [&](auto dummy) {
    using T = decltype(dummy);
    if constexpr (!std::is_same_v<T, bool>) {
      result_buf = blur_on_curve_ex<T>(
          curves, neighbor_weights, iters, buf_a.typed<T>(), buf_b.typed<T>());
    }
  });
  return result_buf;
}

class BlurAttrFieldInput final : public dune::GeoFieldInput {
 private:
  const Field<float> weight_field_;
  const GField val_field_;
  const int iters_;

 public:
  BlurAttrFieldInput(Field<float> weight_field, GField val_field, const int iterations)
      : dune::GeoFieldInput(val_field.cpp_type(), "Blur Attr"),
        weight_field_(std::move(weight_field)),
        value_field_(std::move(val_field)),
        iters_(iters)
  {
  }

  GVArray get_varray_for_cxt(const dune::GeoFieldCxt &cxt,
                             const IndexMask & /*mask*/) const final
  {
    const int64_t domain_size = cxt.attrs()->domain_size(cxt.domain());

    GArray<> buf_a(*type_, domain_size);

    FieldEval eval(cxt, domain_size);

    evaluator.add_with_destination(val_field_, buf_a.as_mutable_span());
    evaluator.add(weight_field_);
    evaluator.eval();

    /* Blurring does not make sense with a less than 2 elements. */
    if (domain_size <= 1) {
      return GVArray::ForGArray(std::move(buf_a));
    }

    if (iters_ <= 0) {
      return GVArray::ForGArray(std::move(buf_a));
    }

    VArraySpan<float> neighbor_weights = evaluator.get_eval<float>(1);
    GArray<> buf_b(*type_, domain_size);

    GSpan result_buf = buf_a.as_span();
    switch (cxt.type()) {
      case GeoComponent::Type::Mesh:
        if (elem(cxt.domain(), AttrDomain::Point, AttrDomain::Edge, AttrDomain::Face)) {
          if (const Mesh *mesh = cxt.mesh()) {
            result_buf = blur_on_mesh(
                *mesh, cxt.domain(), iters_, neighbor_weights, buf_a, buf_b);
          }
        }
        break;
      case GeoComponent::Type::Curve:
      case GeoComponent::Type::Pen:
        if (cxt.domain() == AttrDomain::Point) {
          if (const dune::CurvesGeo *curves = cxt.curves_or_strokes()) {
            result_buf = blur_on_curves(
                *curves, iters_, neighbor_weights, buf_a, buf_b);
          }
        }
        break;
      default:
        break;
    }

    lib_assert(elem(result_buf.data(), buf_a.data(), buf_b.data()));
    if (result_buf.data() == buf_a.data()) {
      return GVArray::ForGArray(std::move(buf_a));
    }
    return GVArray::ForGArray(std::move(buf_b));
  }

  void for_each_field_input_recursive(FnRef<void(const FieldInput &)> fn) const override
  {
    weight_field_.node().for_each_field_input_recursive(fn);
    val_field_.node().for_each_field_input_recursive(fn);
  }

  uint64_t hash() const override
  {
    return get_default_hash_3(iters_, weight_field_, val_field_);
  }

  bool is_equal_to(const fn::FieldNode &other) const override
  {
    if (const BlurAttrFieldInput *other_blur = dynamic_cast<const BlurAttrFieldInput *>(
            &other))
    {
      return weight_field_ == other_blur->weight_field_ &&
             val_field_ == other_blur->val_field_ && iters_ == other_blur->its_;
    }
    return false;
  }

  std::optional<AttrDomain> preferred_domain(const GeoComponent &component) const override
  {
    const std::optional<AttrDomain> domain = dune::try_detect_field_domain(component, val_field_);
    if (domain.has_val() && *domain == AttrDomain::Corner) {
      return AttrDomain::Point;
    }
    return domain;
  }
};

static void node_geo_ex(GeoNodeExParams params)
{
  const int iters = params.extract_input<int>("Iters");
  Field<float> weight_field = params.extract_input<Field<float>>("Weight");

  GField val_field = params.extract_input<GField>("Val");
  GField output_field{std::make_shared<BlurAttrFieldInput>(
      std::move(weight_field), std::move(val_field), iters)};
  params.set_output<GField>("Val", std::move(output_field));
}

static void node_api(ApiStruct *sapi)
{
  api_def_node_enum(
      sapi,
      "data_type",
      "Data Type",
      "",
      api_enum_attribute_type_items,
      node_inline_enum_accessors(custom1),
      CD_PROP_FLOAT,
      [](Cxt * /*C*/, ApiPtr * /*ptr*/, ApiProp * /*prop*/, bool *r_free) {
        *r_free = true;
        return enum_items_filter(api_enum_attr_type_items, [](const EnumPropItem &item) {
          return elem(item.val, CD_PROP_FLOAT, CD_PROP_FLOAT3, CD_PROP_COLOR, CD_PROP_INT32);
        });
      });
}

static void node_register()
{
  static NodeType ntype;
  geo_node_type_base(&ntype, GEO_NODE_BLUR_ATTR, "Blur Attribute", NODE_CLASS_ATTR);
  ntype.initfn = node_init;
  ntype.decl = node_decl;
  ntype.draw_btns = node_layout;
  ntype.geo_node_ex = node_geo_ex;
  ntype.gather_link_search_ops = node_gather_link_searches;
  nodeRegisterType(&ntype);

  node_api(ntype.api_ext.sapi);
}
REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_blur_attribute_cc
