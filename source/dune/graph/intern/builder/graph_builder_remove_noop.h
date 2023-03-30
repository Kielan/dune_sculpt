#pragma once

namespace dune::graph {

struct Graph;

/* Remove all no-op nodes that have zero outgoing relations. */
void graph_remove_unused_noops(Graph *graph);

}  // namespace dune::graph
