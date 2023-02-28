#pragma once

namespace dune {
namespace deg {

struct DGraph;

/* Detect and solve dependency cycles. */
void dgraph_detect_cycles(DGraph *graph);

}  // namespace deg
}  // namespace dune
