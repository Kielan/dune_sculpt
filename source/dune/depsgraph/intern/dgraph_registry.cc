#include "intern/dgraph_registry.h"

#include "lib_utildefines.h"

#include "intern/dgraph.h"

namespace dune::dgraph {

using GraphRegistry = Map<Main *, VectorSet<DGraph *>>;
static GraphRegistry &get_graph_registry()
{
  static GraphRegistry graph_registry;
  return graph_registry;
}

void register_graph(DGraph *dgraph)
{
  Main *dmain = dgraph->dmain;
  get_graph_registry().lookup_or_add_default(dmain).add_new(dgraph);
}

void unregister_graph(DGraph *dgraph)
{
  Main *dmain = dgraph->dmain;
  GraphRegistry &graph_registry = get_graph_registry();
  VectorSet<Depsgraph *> &graphs = graph_registry.lookup(dmain);
  graphs.remove(dgraph);

  /* If this was the last dgraph associated with the main, remove the main entry as well. */
  if (graphs.is_empty()) {
    graph_registry.remove(dmain);
  }
}

Span<DGraph *> get_all_registered_graphs(Main *dmain)
{
  VectorSet<DGraph *> *graphs = get_graph_registry().lookup_ptr(dmain);
  if (graphs != nullptr) {
    return *graphs;
  }
  return {};
}

}  // namespace dune::dgraph
