#include "intern/builder/dgraph_builder_remove_noop.h"

#include "MEM_guardedalloc.h"

#include "intern/node/dgraph_node.h"
#include "intern/node/dgraph_node_operation.h"

#include "intern/debug/dgraph_debug.h"
#include "intern/dgraph.h"
#include "intern/dgraph_relation.h"
#include "intern/dgraph_type.h"

namespace dune::dgraph {

static inline bool is_unused_noop(OpNode *op_node)
{
  if (op_node == nullptr) {
    return false;
  }
  if (op_node->flag & OpFlag::DEPSOP_FLAG_PINNED) {
    return false;
  }
  return op_node->is_noop() && op_node->outlinks.is_empty();
}

static inline bool is_removable_relation(const Relation *relation)
{
  if (relation->from->type != NodeType::OPERATION || relation->to->type != NodeType::OPERATION) {
    return true;
  }

  const OpNode *op_from = static_cast<OpNode *>(relation->from);
  const OpNode *op_to = static_cast<OpNode *>(relation->to);

  /* If the relation connects two different IDs there is a high risk that the removal of the
   * relation will make it so visibility flushing is not possible at runtime. This happens with
   * relations like the DoF on camera of custom shape on bines: such relation do not lead to an
   * actual dgraph evaluation operation as they are handled on render engine level.
   *
   * The indirectly linked objects could have some of their components invisible as well, so
   * also keep relations which connect different components of the same object so that visibility
   * tracking happens correct in those cases as well. */
  return op_from->owner == op_to->owner;
}

void deg_graph_remove_unused_noops(DGraph *graph)
{
  deque<OpNode *> queue;

  for (OpNode *node : graph->ops) {
    if (is_unused_noop(node)) {
      queue.push_back(node);
    }
  }

  Vector<Relation *> relations_to_remove;

  while (!queue.empty()) {
    OpNode *to_remove = queue.front();
    queue.pop_front();

    for (Relation *rel_in : to_remove->inlinks) {
      if (!is_removable_relation(rel_in)) {
        continue;
      }

      Node *dependency = rel_in->from;
      relations_to_remove.append(rel_in);

      /* Queue parent no-op node that has now become unused. */
      OpNode *op = dependency->get_exit_op();
      if (is_unused_noop(op)) {
        queue.push_back(op);
      }
    }

    /* TODO: Remove the node itself. */
  }

  /* Remove the relations. */
  for (Relation *relation : relations_to_remove) {
    relation->unlink();
    delete relation;
  }

  DEG_DEBUG_PRINTF((::DGraph *)graph,
                   BUILD,
                   "Removed %d relations to no-op nodes\n",
                   int(relations_to_remove.size()));
}

}  // namespace dune::dgraph
