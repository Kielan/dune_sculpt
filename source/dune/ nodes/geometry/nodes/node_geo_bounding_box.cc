#include "geo_mesh_primitive_cuboid.hh"

#include "node_geo_util.hh"

namespace dune::nodes::node_geo_bounding_box_cc {

static void node_decl(NodeDeclBuilder &b)
{
  b.add_input<decl::Geometry>("Geo");
  b.add_output<decl::Geometry>("Bounding Box");
  b.add_output<decl::Vector>("Min");
  b.add_output<decl::Vector>("Max");
}

static void node_geo_ex(GeoNodeExParams params)
{
  GeoSet geo_set = params.extract_input<GeoSet>("Geo");

  /* Compute the min and max of all realized geo for the two
   * vector outputs, which are only meant to consider real geo. */
  const std::optional<Bounds<float3>> bounds = geo_set.compute_boundbox_wo_instances();
  if (!bounds) {
    params.set_output("Min", float3(0));
    params.set_output("Max", float3(0));
  }
  else {
    params.set_output("Min", bounds->min);
    params.set_output("Max", bounds->max);
  }

  /* Generate the bounding box meshes inside each unique geo set (including individually for
   * every instance). Bc geo components are ref counted anyway, we can just
   * repurpose the original geo sets for the output. */
  if (params.output_is_required("Bounding Box")) {
    geo_set.modify_geo_sets([&](GeoSet &sub_geo) {
      std::optional<Bounds<float3>> sub_bounds;

      /* Reuse the min and max calc if this is the main "real" geometry set. */
      if (&sub_geo == &geom_set) {
        sub_bounds = bounds;
      }
      else {
        sub_bounds = sub_geo.compute_boundbox_without_instances();
      }

      if (!sub_bounds) {
        sub_geo.remove_geo_during_modify();
      }
      else {
        const float3 scale = sub_bounds->max - sub_bounds->min;
        const float3 center = sub_bounds->min + scale / 2.0f;
        Mesh *mesh = geo::create_cuboid_mesh(scale, 2, 2, 2, "uv_map");
        transform_mesh(*mesh, center, math::Quaternion::id(), float3(1));
        sub_geo.replace_mesh(mesh);
        sub_geo.keep_only_during_mod({GeoComponent::Type::Mesh});
      }
    });

    params.set_output("Bounding Box", std::move(geo_set));
  }
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_BOUNDING_BOX, "Bounding Box", NODE_CLASS_GEO));
  ntype.decl = node_decl;
  ntype.geo_node_ex = node_geo_ex;
  nodeRegisterType(&ntype);
}
REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_bounding_box_cc
