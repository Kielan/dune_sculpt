#pragma once

namespace dune {
namespace deg {

struct Depsgraph;

/* Detect and solve dependency cycles. */
void deg_graph_detect_cycles(Depsgraph *graph);

}  // namespace deg
}  // namespace dune
