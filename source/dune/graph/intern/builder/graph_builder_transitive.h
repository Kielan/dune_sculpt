#pragma once

namespace dune::graph {

struct Graph;

/* Performs a transitive reduction to remove redundant relations. */
void graph_transitive_reduction(Graph *graph);

}  // namespace dune::graph
