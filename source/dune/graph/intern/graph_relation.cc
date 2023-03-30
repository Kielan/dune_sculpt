#include "intern/graph_relation.h"
 /* own include */
#include "lib_utildefines.h"

#include "intern/graph_type.h"
#include "intern/node/graph_node.h"

namespace dune::graph {

Relation::Relation(Node *from, Node *to, const char *description)
    : from(from), to(to), name(description), flag(0)
{
  /* Hook it up to the nodes which use it.
   *
   * NOTE: We register relation in the nodes which this link connects to here
   * in constructor but we don't un-register it in the destructor.
   *
   * Reasoning:
   *
   * - Destructor is currently used on global graph destruction, so there's no
   *   real need in avoiding dangling pointers, all the memory is to be freed
   *   anyway.
   *
   * - Un-registering relation is not a cheap operation, so better to have it
   *   as an explicit call if we need this. */
  from->outlinks.append(this);
  to->inlinks.append(this);
}

Relation::~Relation()
{
  /* Sanity check. */
  lib_assert(from != nullptr && to != nullptr);
}

void Relation::unlink()
{
  /* Sanity check. */
  lib_assert(from != nullptr && to != nullptr);
  from->outlinks.remove_first_occurrence_and_reorder(this);
  to->inlinks.remove_first_occurrence_and_reorder(this);
}

}  // namespace dune::graph
