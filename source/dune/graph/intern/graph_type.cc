/**
 * Defines and code for core node types.
 */

#include <cstdlib> /* for lib_assert() */

#include "lib_utildefines.h"

#include "types_customdata.h"

#include "graph.h"

#include "intern/graph_type.h"
#include "intern/node/graph_node.h"
#include "intern/node/graph_node_component.h"
#include "intern/node/graph_node_factory.h"
#include "intern/node/graph_node_op.h"

namespace graph = dune::graph;

void graph_register_node_types()
{
  /* register node types */
  graph::graph_register_base_nodes();
  graph::graph_register_component_nodes();
  graph::graph_register_op_nodes();
}

void graph_free_node_types()
{
}

graph::GraphCustomDataMeshMasks::GraphCustomDataMeshMasks(const CustomData_MeshMasks *other)
    : vert_mask(other->vmask),
      edge_mask(other->emask),
      face_mask(other->fmask),
      loop_mask(other->lmask),
      poly_mask(other->pmask)
{
}
