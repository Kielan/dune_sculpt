#include "types_pointcloud.h"

#include "dune_curves.hh"
#include "dune_pen.hh"
#include "dune_instances.hh"
#include "dune_material.h"
#include "dune_mesh.hh"

#include "geo_randomize.hh"

#include "node_geo_util.hh"

#ifdef WITH_BULLET
#  include "RBI_hull_api.h"
#endif

namespace dune::nodes::node_geo_convex_hull_cc {

static void node_decl(NodeDeclBuilder &b)
{
  b.add_input<decl::Geometry>("Geo");
  b.add_output<decl::Geometry>("Convex Hull");
}

#ifdef WITH_BULLET

static Mesh *hull_from_bullet(const Mesh *mesh, Span<float3> coords)
{
  plConvexHull hull = plConvexHullCompute((float(*)[3])coords.data(), coords.size());

  const int verts_num = plConvexHullNumVerts(hull);
  const int faces_num = verts_num <= 2 ? 0 : plConvexHullNumFaces(hull);
  const int loops_num = verts_num <= 2 ? 0 : plConvexHullNumLoops(hull);
  /* Half as many edges as loops, bc the mesh is manifold. */
  const int edges_num = verts_num == 2 ? 1 : verts_num < 2 ? 0 : loops_num / 2;

  /* Create Mesh *result with proper capacity. */
  Mesh *result;
  if (mesh) {
    result = dune_mesh_new_nomain_from_template(mesh, verts_num, edges_num, faces_num, loops_num);
  }
  else {
    result = dune_mesh_new_nomain(verts_num, edges_num, faces_num, loops_num);
    dune_id_material_eval_ensure_default_slot(&result->id);
  }
  dune::mesh_smooth_set(*result, false);

  /* Copy vertices. */
  MutableSpan<float3> dst_positions = result->vert_positions_for_write();
  for (const int i : IndexRange(verts_num)) {
    int original_index;
    plConvexHullGetVert(hull, i, dst_positions[i], &original_index);

    if (original_index >= 0 && original_index < coords.size()) {
#  if 0 /* Disabled bc it only works for meshes, not predictable enough. */
      /* Copy custom data on verts, like vert groups etc. */
      if (mesh && original_index < mesh->verts_num) {
        CustomData_copy_data(&mesh->vert_data, &result->vert_data, int(original_index), int(i), 1);
      }
#  endif
    }
    else {
      lib_assert_msg(0, "Unexpected new vertex in hull output");
    }
  }

  /* Copy edges and loops. */
  /* NOTE: ConvexHull from Bullet uses a half-edge data struct
   * for its mesh. To convert that, each half-edge needs to be converted
   * to a loop and edges need to be created from that. */
  Array<int> corner_verts(loops_num);
  Array<int> corner_edges(loops_num);
  uint edge_index = 0;
  MutableSpan<int2> edges = result->edges_for_write();

  for (const int i : IndexRange(loops_num)) {
    int v_from;
    int v_to;
    plConvexHullGetLoop(hull, i, &v_from, &v_to);

    corner_verts[i] = v_from;
    /* Add edges for ascending order loops only. */
    if (v_from < v_to) {
      edges[edge_index] = int2(v_from, v_to);

      /* Write edge index into both loops that have it. */
      int reverse_index = plConvexHullGetReversedLoopIndex(hull, i);
      corner_edges[i] = edge_index;
      corner_edges[reverse_index] = edge_index;
      edge_index++;
    }
  }
  if (edges_num == 1) {
    /* In this case there are no loops. */
    edges[0] = int2(0, 1);
    edge_index++;
  }
  lib_assert(edge_index == edges_num);

  /* Copy faces. */
  Array<int> loops;
  int j = 0;
  MutableSpan<int> face_offsets = result->face_offsets_for_write();
  MutableSpan<int> mesh_corner_verts = result->corner_verts_for_write();
  MutableSpan<int> mesh_corner_edges = result->corner_edges_for_write();
  int dst_corner = 0;

  for (const int i : IndexRange(faces_num)) {
    const int len = plConvexHullGetFaceSize(hull, i);

    lib_assert(len > 2);

    /* Get face loop indices. */
    loops.reinit(len);
    plConvexHullGetFaceLoops(hull, i, loops.data());

    face_offsets[i] = j;
    for (const int k : IndexRange(len)) {
      mesh_corner_verts[dst_corner] = corner_verts[loops[k]];
      mesh_corner_edges[dst_corner] = corner_edges[loops[k]];
      dst_corner++;
    }
    j += len;
  }

  plConvexHullDelete(hull);
  return result;
}

static Mesh *compute_hull(const GeoSet &geo_set)
{
  int span_count = 0;
  int count = 0;
  int total_num = 0;

  Span<float3> positions_span;

  if (const Mesh *mesh = geometry_set.get_mesh()) {
    count++;
    if (const VArray positions = *mesh->attributes().lookup<float3>("position")) {
      if (positions.is_span()) {
        span_count++;
        positions_span = positions.get_internal_span();
      }
      total_num += positions.size();
    }
  }

  if (const PointCloud *points = geo_set.get_pointcloud()) {
    count++;
    if (const VArray positions = *points->attributes().lookup<float3>("position")) {
      if (positions.is_span()) {
        span_count++;
        positions_span = positions.get_internal_span();
      }
      total_num += positions.size();
    }
  }

  if (const Curves *curves_id = geo_set.get_curves()) {
    count++;
    span_count++;
    const dune::CurvesGeo &curves = curves_id->geo.wrap();
    positions_span = curves.eval_positions();
    total_num += positions_span.size();
  }

  if (count == 0) {
    return nullptr;
  }

  /* If there is only one positions virtual array and it is already contiguous, avoid copying
   * all of the positions and instead pass the span directly to the convex hull function. */
  if (span_count == 1 && count == 1) {
    return hull_from_bullet(geo_set.get_mesh(), positions_span);
  }

  Array<float3> positions(total_num);
  int offset = 0;

  if (const Mesh *mesh = geo_set.get_mesh()) {
    if (const VArray varray = *mesh->attrs().lookup<float3>("position")) {
      varray.materialize(positions.as_mutable_span().slice(offset, varray.size()));
      offset += varray.size();
    }
  }

  if (const PointCloud *points = geo_set.get_pointcloud()) {
    if (const VArray varray = *points->attrs().lookup<float3>("position")) {
      varray.materialize(positions.as_mutable_span().slice(offset, varray.size()));
      offset += varray.size();
    }
  }

  if (const Curves *curves_id = geo_set.get_curves()) {
    const dune::CurvesGeo &curves = curves_id->geo.wrap();
    Span<float3> array = curves.eval_positions();
    positions.as_mutable_span().slice(offset, array.size()).copy_from(array);
    offset += array.size();
  }

  return hull_from_bullet(geo_set.get_mesh(), positions);
}

static void convex_hull_pen(GeoSet &geo_set)
{
  using namespace dune::pen;

  const Pen &pen = *geo_set.get_pen();
  Array<Mesh *> mesh_by_layer(pen.layers().size(), nullptr);

  for (const int layer_index : pen.layers().index_range()) {
    const Drawing *drawing = get_eval_pen_layer_drawing(pen, layer_index);
    if (drawing == nullptr) {
      continue;
    }
    const dune::CurvesGeo &curves = drawing->strokes();
    const Span<float3> positions_span = curves.eval_positions();
    if (positions_span.is_empty()) {
      continue;
    }
    mesh_by_layer[layer_index] = hull_from_bullet(nullptr, positions_span);
  }

  if (mesh_by_layer.is_empty()) {
    return;
  }

  InstancesComponent &instances_component =
      geo_set.get_component_for_write<InstancesComponent>();
  dune::Instances *instances = instances_component.get_for_write();
  if (instances == nullptr) {
    instances = new dune::Instances();
    instances_component.replace(instances);
  }
  for (Mesh *mesh : mesh_by_layer) {
    if (!mesh) {
      /* Add an empty ref so the num of layers and instances match.
       * This makes it easy to reconstruct the layers afterwards and keep their attributes.
       * In this particular case we don't propagate attributes. */
      const int handle = instances->add_ref(dune::InstanceRef());
      instances->add_instance(handle, float4x4::id());
      continue;
    }
    GeoSet tmp_set = GeoSet::from_mesh(mesh);
    const int handle = instances->add_ref(dune::InstanceRef{tmp_set});
    instances->add_instance(handle, float4x4::id());
  }
  geometry_set.replace_pen(nullptr);
}

#endif /* WITH_BULLET */

static void node_geo_ex(GeoNodeExParams params)
{
  GeoSet geo_set = params.extract_input<GeoSet>("Geo");

#ifdef WITH_BULLET

  geo_set.mod_geo_sets([&](GeoSet &geo_set) {
    Mesh *mesh = compute_hull(geo_set);
    if (mesh) {
      geo::debug_randomize_mesh_order(mesh);
    }
    geo_set.replace_mesh(mesh);
    if (geo_set.has_pen()) {
      convex_hull_pen(geo_set);
    }
    geo_set.keep_only_during_modify({GeoComponent::Type::Mesh});
  });

  params.set_output("Convex Hull", std::move(geo_set));
#else
  params.err_msg_add(NodeWarningType::Err,
                           TIP_("Disabled, Dune was compiled without Bullet"));
  params.set_default_remaining_outputs();
#endif /* WITH_BULLET */
}

static void node_register()
{
  static NodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_CONVEX_HULL, "Convex Hull", NODE_CLASS_GEO);
  ntype.decl = node_declare;
  ntype.geo_node_ex = node_geo_ex;
  nodeRegisterType(&ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace dune::nodes::node_geo_convex_hull_cc
