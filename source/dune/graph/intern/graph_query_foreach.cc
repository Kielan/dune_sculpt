/** Implementation of Querying and Filtering API's **/

#include "mem_guardedalloc.h"

#include "lib_utildefines.h"

#include "types_object.h"
#include "types_scene.h"

#include "graph.h"
#include "graph_query.h"

#include "intern/graph.h"
#include "intern/graph_relation.h"
#include "intern/node/graph_node.h"
#include "intern/node/graph_node_component.h"
#include "intern/node/graph_node_id.h"
#include "intern/node/graph_node_op.h"

namespace graph = dune::graph;

/* ************************ DEG TRAVERSAL ********************* */

namespace dune::graph {
namespace {

using TraversalQueue = deque<OpNode *>;

using GraphForeachOp = void (*)(OpNode *, void *);

bool graph_foreach_needs_visit(const OpNode *op_node, const int flags)
{
  if (flags & GRAPH_FOREACH_COMPONENT_IGNORE_TRANSFORM_SOLVERS) {
    if (op_node->opcode == OpCode::RIGIDBODY_SIM) {
      return false;
    }
  }
  return true;
}

void graph_foreach_dependent_op(const Graph *UNUSED(graph),
                                const IdNode *target_id_node,
                                eGraphObjectComponentType source_component_type,
                                int flags,
                                GraphForeachOp cb,
                                void *user_data)
{
  if (target_id_node == nullptr) {
    /* TODO: Shall we inform or assert here about attempt to start
     * iterating over non-existing ID? */
    return;
  }
  /* Start with scheduling all operations from ID node. */
  TraversalQueue queue;
  Set<OpNode *> scheduled;
  for (ComponentNode *comp_node : target_id_node->components.values()) {
    if (comp_node->type == NodeType::VISIBILITY) {
      /* Visibility component is only used internally. It is not to be reporting dependencies to
       * the outer world. */
      continue;
    }

    if (source_component_type != GRAPH_OB_COMP_ANY &&
        nodeTypeToObjectComponent(comp_node->type) != source_component_type) {
      continue;
    }
    for (OpNode *op_node : comp_node->ops) {
      if (!graph_foreach_needs_visit(op_node, flags)) {
        continue;
      }
      queue.push_back(op_node);
      scheduled.add(op_node);
    }
  }
  /* Process the queue. */
  while (!queue.empty()) {
    /* get next operation node to process. */
    OpNode *op_node = queue.front();
    queue.pop_front();
    for (;;) {
      callback(op_node, user_data);
      /* Schedule outgoing operation nodes. */
      if (op_node->outlinks.size() == 1) {
        OpNode *to_node = (OpNode *)op_node->outlinks[0]->to;
        if (!scheduled.contains(to_node) && graph_foreach_needs_visit(to_node, flags)) {
          scheduled.add_new(to_node);
          op_node = to_node;
        }
        else {
          break;
        }
      }
      else {
        for (Relation *rel : op_node->outlinks) {
          OpNode *to_node = (OpNode *)rel->to;
          if (!scheduled.contains(to_node) && deg_foreach_needs_visit(to_node, flags)) {
            queue.push_front(to_node);
            scheduled.add_new(to_node);
          }
        }
        break;
      }
    }
  }
}

struct ForeachIdComponentData {
  GraphForeachIdComponentCb cb;
  void *user_data;
  IdNode *target_id_node;
  Set<ComponentNode *> visited;
};

void graph_foreach_dependent_component_cb(OpNode *op_node, void *user_data_v)
{
  ForeachIdComponentData *user_data = reinterpret_cast<ForeachIdComponentData *>(user_data_v);
  ComponentNode *comp_node = op_node->owner;
  IdNode *id_node = comp_node->owner;
  if (id_node != user_data->target_id_node && !user_data->visited.contains(comp_node)) {
    user_data->callback(
        id_node->id_orig, nodeTypeToObjectComponent(comp_node->type), user_data->user_data);
    user_data->visited.add_new(comp_node);
  }
}

void graph_foreach_dependent_id_component(const Graph *graph,
                                          const Id *id,
                                          eGraphObjectComponentType source_component_type,
                                          int flags,
                                          GraphForeachIdComponentCb cb,
                                          void *user_data)
{
  ForeachIdComponentData data;
  data.callback = callback;
  data.user_data = user_data;
  data.target_id_node = graph->find_id_node(id);
  dgraph_foreach_dependent_op(graph,
                                  data.target_id_node,
                                  source_component_type,
                                  flags,
                                  dgraph_foreach_dependent_component_cb,
                                  &data);
}

struct ForeachIdData {
  DGraphForeachIdCb cb;
  void *user_data;
  IAdNode *target_id_node;
  Set<IdNode *> visited;
};

void dgraph_foreach_dependent_Id_callback(OpNode *op_node, void *user_data_v)
{
  ForeachIdData *user_data = reinterpret_cast<ForeachIdData *>(user_data_v);
  ComponentNode *comp_node = op_node->owner;
  IdNode *id_node = comp_node->owner;
  if (id_node != user_data->target_id_node && !user_data->visited.contains(id_node)) {
    user_data->callback(id_node->id_orig, user_data->user_data);
    user_data->visited.add_new(id_node);
  }
}

void dgraph_foreach_dependent_Id(const DGraph *graph,
                              const Id *id,
                              DGraphForeachIdCb cb,
                              void *user_data)
{
  ForeachIdData data;
  data.callback = callback;
  data.user_data = user_data;
  data.target_id_node = graph->find_id_node(id);
  deg_foreach_dependent_op(
      graph, data.target_id_node, DGRAPH_OB_COMP_ANY, 0, deg_foreach_dependent_ID_callback, &data);
}

void dgraph_foreach_ancestor_Id(const DGraph *graph,
                             const Id *id,
                             DGraphForeachIdCb cb,
                             void *user_data)
{
  /* Start with getting ID node from the graph. */
  IdNode *target_id_node = graph->find_id_node(id);
  if (target_id_node == nullptr) {
    /* TODO: Shall we inform or assert here about attempt to start
     * iterating over non-existing ID? */
    return;
  }
  /* Start with scheduling all operations from ID node. */
  TraversalQueue queue;
  Set<OpNode *> scheduled;
  for (ComponentNode *comp_node : target_id_node->components.values()) {
    for (OpNode *op_node : comp_node->ops) {
      queue.push_back(op_node);
      scheduled.add(op_node);
    }
  }
  Set<IdNode *> visited;
  visited.add_new(target_id_node);
  /* Process the queue. */
  while (!queue.empty()) {
    /* get next operation node to process. */
    OpNode *op_node = queue.front();
    queue.pop_front();
    for (;;) {
      /* Check whether we need to inform callee about corresponding ID node. */
      ComponentNode *comp_node = op_node->owner;
      IdNode *id_node = comp_node->owner;
      if (!visited.contains(id_node)) {
        /* TODO: Is it orig or CoW? */
        callback(id_node->id_orig, user_data);
        visited.add_new(id_node);
      }
      /* Schedule incoming operation nodes. */
      if (op_node->inlinks.size() == 1) {
        Node *from = op_node->inlinks[0]->from;
        if (from->get_class() == NodeClass::OPERATION) {
          OpNode *from_node = (OpNode *)from;
          if (scheduled.add(from_node)) {
            op_node = from_node;
          }
          else {
            break;
          }
        }
      }
      else {
        for (Relation *rel : op_node->inlinks) {
          Node *from = rel->from;
          if (from->get_class() == NodeClass::OPERATION) {
            OpNode *from_node = (OpNode *)from;
            if (scheduled.add(from_node)) {
              queue.push_front(from_node);
            }
          }
        }
        break;
      }
    }
  }
}

void graph_foreach_id(const Graph *graph, GraphForeachIdCb cb, void *user_data)
{
  for (const IdNode *id_node : graph->id_nodes) {
    cb(id_node->id_orig, user_data);
  }
}

}  // namespace
}  // namespace dune::graph

void graph_foreach_dependent_id(const Graph *graph,
                                const Id *id,
                                GraphForeachIdCb cb,
                                void *user_data)
{
  graph::graph_foreach_dependent_id((const graph::Graph *)graph, id, cb, user_data);
}

void graph_foreach_dependent_id_component(const Graph *graph,
                                          const Id *id,
                                          eGraphObjectComponentType source_component_type,
                                          int flags,
                                          GraphForeachIdComponentCb cb,
                                          void *user_data)
{
  graph::graph_foreach_dependent_id_component(
      (const graph::Graph *)graph, id, source_component_type, flags, cb, user_data);
}

void graph_foreach_ancestor_id(const Graph *graph,
                               const Id *id,
                               GraphForeachIdCb cb,
                               void *user_data)
{
  graph::graph_foreach_ancestor_id((const graph::Graph *)graph, id, cb, user_data);
}

void graph_foreach_id(const Graph *graph, GraphForeachIdCb cb, void *user_data)
{
  graph::graph_foreach_id((const graph::Graph *)graph, cb, user_data);
}
