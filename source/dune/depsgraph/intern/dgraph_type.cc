/** \file
 * \ingroup depsgraph
 *
 * Defines and code for core node types.
 */

#include <cstdlib> /* for BLI_assert() */

#include "lib_utildefines.h"

#include "types_customdata.h"

#include "dgraph.h"

#include "intern/dgraph_type.h"
#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_component.h"
#include "intern/node/dgraph_node_factory.h"
#include "intern/node/dgraph_node_operation.h"

namespace dgraph = dune::dgraph;

void dgraph_register_node_types()
{
  /* register node types */
  dgraph::dgraph_register_base_dnodes();
  dgraph::dgraph_register_component_dnodes();
  dgraph::dgraph_register_op_dnodes();
}

void dgraph_free_node_types()
{
}

dgraph::DGraphCustomDataMeshMasks::DGraphCustomDataMeshMasks(const CustomData_MeshMasks *other)
    : vert_mask(other->vmask),
      edge_mask(other->emask),
      face_mask(other->fmask),
      loop_mask(other->lmask),
      poly_mask(other->pmask)
{
}
