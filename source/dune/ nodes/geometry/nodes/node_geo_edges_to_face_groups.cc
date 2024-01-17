#include "dune_mesh.hh"
#include "dune_mesh_mapping.hh"
#include "lib_atomic_disjoint_set.hh"
#include "node_geometry_util.hh"

namespace dune::nodes::node_geo_edges_to_face_groups_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Bool>("Boundary Edges")
      .default_val(true)
      .hide_val()
      .supports_field()
      .description("Edges used to split faces into separate groups");
  b.add_output<decl::Int>("Face Group Id")
      .dependent_field()
      .description("Index of the face group inside each boundary edge region");
}

/* Join all unique unordered combinations of indices. */
static void join_indices(AtomicDisjointSet &set, const Span<int> indices)
{
  for (const int i : indices.index_range().drop_back(1)) {
    set.join(indices[i], indices[i + 1]);
  }
}

class FaceSetFromBoundariesInput final : public dune::MeshFieldInput {
 private:
  Field<bool> non_boundary_edge_field_;

 public:
  FaceSetFromBoundariesInput(Field<bool> sel)
      : dune::MeshFieldInput(CPPType::get<int>(), "Edges to Face Groups"),
        non_boundary_edge_field_(std::move(sel))
  {
  }

  GVArray get_varr_for_cxt(const Mesh &mesh,
                                 const AttrDomain domain,
                                 const IndexMask & /*mask*/) const final
  {
    const dune::MeshFieldCxt context{mesh, AttrDomain::Edge};
    fn::FieldEvaluator evaluator{xt, mesh.edges_num};
    evaluator.add(non_boundary_edge_field_);
    evaluator.eval();
    const IndexMask non_boundary_edges = evaluator.get_eval_as_mask(0);

    const OffsetIndices faces = mesh.faces();

    Array<int> edge_to_face_offsets;
    Array<int> edge_to_face_indices;
    const GroupedSpan<int> edge_to_face_map = dune::mesh::build_edge_to_face_map(
        faces, mesh.corner_edges(), mesh.edges_num, edge_to_face_offsets, edge_to_face_indices);

    AtomicDisjointSet islands(faces.size());
    non_boundary_edges.foreach_index(
        GrainSize(2048), [&](const int edge) { join_indices(islands, edge_to_face_map[edge]); });

    Array<int> output(faces.size());
    islands.calc_reduced_ids(output);

    return mesh.attrs().adapt_domain(
        VArray<int>::ForContainer(std::move(output)), AttrDomain::Face, domain);
  }

  uint64_t hash() const override
  {
    return non_boundary_edge_field_.hash();
  }

  bool is_equal_to(const fn::FieldNode &other) const override
  {
    if (const auto *other_field = dynamic_cast<const FaceSetFromBoundariesInput *>(&other)) {
      return other_field->non_boundary_edge_field_ == non_boundary_edge_field_;
    }
    return false;
  }

  std::optional<AttrDomain> pref_domain(const Mesh & /*mesh*/) const final
  {
    return AttrDomain::Face;
  }
};

static void geo_node_ex(GeoNodeExParams params)
{
  Field<bool> boundary_edges = params.extract_input<Field<bool>>("Boundary Edges");
  Field<bool> non_boundary_edges = fn::invert_bool_field(std::move(boundary_edges));
  params.set_output(
      "Face Group Id",
      Field<int>(std::make_shared<FaceSetFromBoundariesInput>(std::move(non_boundary_edges))));
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(
      &ntype, GEO_NODE_EDGES_TO_FACE_GROUPS, "Edges to Face Groups", NODE_CLASS_INPUT);
  ntype.geom_node_ex = geo_node_ex;
  ntype.decl = node_declare;

  nodeRegisterType(&ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_edges_to_face_groups_cc
