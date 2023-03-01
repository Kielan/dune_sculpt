#pragma once

namespace dune::dgraph {

struct DGraph;

/* Remove all no-op nodes that have zero outgoing relations. */
void dgraph_remove_unused_noops(DGraph *graph);

}  // namespace dune::dgraph
