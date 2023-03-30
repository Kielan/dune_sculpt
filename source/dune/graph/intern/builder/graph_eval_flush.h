/** Core routines for how the Graph works. **/

#pragma once

namespace dune::graph {

struct Graph;

/**
 * Flush updates from tagged nodes outwards until all affected nodes are tagged.
 */
void graph_flush_updates(struct Graph *graph);

/**
 * Clear tags from all operation nodes.
 */
void graph_clear_tags(struct Graph *graph);

}  // namespace dune::graph
