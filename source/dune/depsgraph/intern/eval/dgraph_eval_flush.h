/**
 * Core routines for how the DGraph works.
 */

#pragma once

namespace dune::dgraph {

struct DGraph;

/**
 * Flush updates from tagged nodes outwards until all affected nodes are tagged.
 */
void dgraph_flush_updates(struct DGraph *graph);

/**
 * Clear tags from all operation nodes.
 */
void dgraph_clear_tags(struct DGraph *graph);

}  // namespace dune::dgraph
