#pragma once

namespace dune::deg {

struct Depsgraph;

/* Performs a transitive reduction to remove redundant relations. */
void deg_graph_transitive_reduction(Depsgraph *graph);

}  // namespace dune::deg
