#include "intern/builder/graph_builder_transitive.h"

#include "mem_guardedalloc.h"

#include "intern/node/graph_node.h"
#include "intern/node/graph_node_component.h"
#include "intern/node/graph_node_operation.h"

#include "intern/debug/graph_debug.h"
#include "intern/graph.h"
#include "intern/graph_relation.h"

namespace dune::graph {

/* -------------------------------------------------- */

/* Performs a transitive reduction to remove redundant relations.
 * https://en.wikipedia.org/wiki/Transitive_reduction
 *
 * XXX The current implementation is somewhat naive and has O(V*E) worst case
 * runtime.
 * A more optimized algorithm can be implemented later, e.g.
 *
 *   http://www.sciencedirect.com/science/article/pii/0304397588900321/pdf?md5=3391e309b708b6f9cdedcd08f84f4afc&pid=1-s2.0-0304397588900321-main.pdf
 *
 * Care has to be taken to make sure the algorithm can handle the cyclic case
 * too! (unless we can to prevent this case early on).
 */

enum {
  OP_VISITED = 1,
  OP_REACHABLE = 2,
};

static void dgraph_tag_paths_recursive(Node *node)
{
  if (node->custom_flags & OP_VISITED) {
    return;
  }
  node->custom_flags |= OP_VISITED;
  for (Relation *rel : node->inlinks) {
    dgraph_tag_paths_recursive(rel->from);
    /* Do this only in inlinks loop, so the target node does not get
     * flagged. */
    rel->from->custom_flags |= OP_REACHABLE;
  }
}

void dgraph_transitive_reduction(DGraph *graph)
{
  int num_removed_relations = 0;
  Vector<Relation *> relations_to_remove;

  for (OpNode *target : graph->ops) {
    /* Clear tags. */
    for (OpNode *node : graph->ops) {
      node->custom_flags = 0;
    }
    /* Mark nodes from which we can reach the target
     * start with children, so the target node and direct children are not
     * flagged. */
    target->custom_flags |= OP_VISITED;
    for (Relation *rel : target->inlinks) {
      dgraph_tag_paths_recursive(rel->from);
    }
    /* Remove redundant paths to the target. */
    for (Relation *rel : target->inlinks) {
      if (rel->from->type == NodeType::TIMESOURCE) {
        /* HACK: time source nodes don't get "custom_flags" flag
         * set/cleared. */
        /* TODO: there will be other types in future, so iterators above
         * need modifying. */
        continue;
      }
      if (rel->from->custom_flags & OP_REACHABLE) {
        relations_to_remove.append(rel);
      }
    }
    for (Relation *rel : relations_to_remove) {
      rel->unlink();
      delete rel;
    }
    num_removed_relations += relations_to_remove.size();
    relations_to_remove.clear();
  }
  DGRAPH_DEBUG_PRINTF((::DGraph *)graph, BUILD, "Removed %d relations\n", num_removed_relations);
}

}  // namespace dune::dgraph
