#pragma once

namespace dune::dgraph {

struct DGraph;

/* Performs a transitive reduction to remove redundant relations. */
void dgraph_transitive_reduction(DGraph *graph);

}  // namespace dune::dgraph
