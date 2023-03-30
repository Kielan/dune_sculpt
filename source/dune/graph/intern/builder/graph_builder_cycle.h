#pragma once

namespace dune {
namespace graph {

struct Graph;

/* Detect and solve dependency cycles. */
void graph_detect_cycles(Graph *graph);

}  // namespace graph
}  // namespace dune
