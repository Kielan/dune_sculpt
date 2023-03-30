/**
 * Evaluation engine entry-points for DGraph Engine.
 */

#pragma once

namespace dune::graph {

struct Graph;

/**
 * Evaluate all nodes tagged for updating,
 * This is usually done as part of main loop, but may also be
 * called from frame-change update.
 *
 * Time sources should be all valid!
 */
void graph_evaluate_on_refresh(Graph *graph);

}  // namespace dune::graph
