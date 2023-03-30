#include "intern/graph_registry.h"

#include "lib_utildefines.h"

#include "intern/graph.h"

namespace dune::graph {

using GraphRegistry = Map<Main *, VectorSet<Graph *>>;
static GraphRegistry &get_graph_registry()
{
  static GraphRegistry graph_registry;
  return graph_registry;
}

void register_graph(Graph *graph)
{
  Main *dmain = graph->dmain;
  get_graph_registry().lookup_or_add_default(dmain).add_new(graph);
}

void unregister_graph(Graph *graph)
{
  Main *dmain = graph->dmain;
  GraphRegistry &graph_registry = get_graph_registry();
  VectorSet<Graph *> &graphs = graph_registry.lookup(dmain);
  graphs.remove(graph);

  /* If this was the last dgraph associated with the main, remove the main entry as well. */
  if (graphs.is_empty()) {
    graph_registry.remove(dmain);
  }
}

Span<Graph *> get_all_registered_graphs(Main *dmain)
{
  VectorSet<Graph *> *graphs = get_graph_registry().lookup_ptr(dmain);
  if (graphs != nullptr) {
    return *graphs;
  }
  return {};
}

}  // namespace dune::graph
