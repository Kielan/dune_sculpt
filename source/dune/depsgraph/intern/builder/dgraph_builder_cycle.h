#pragma once

namespace dune {
namespace dgraph {

struct DGraph;

/* Detect and solve dependency cycles. */
void dgraph_detect_cycles(DGraph *graph);

}  // namespace dgraph
}  // namespace dune
