#include "intern/depsgraph_registry.h"

#include "lib_utildefines.h"

#include "intern/depsgraph.h"

namespace dune::deg {

using GraphRegistry = Map<Main *, VectorSet<Depsgraph *>>;
static GraphRegistry &get_graph_registry()
{
  static GraphRegistry graph_registry;
  return graph_registry;
}

void register_graph(Depsgraph *depsgraph)
{
  Main *dmain = depsgraph->dmain;
  get_graph_registry().lookup_or_add_default(bmain).add_new(depsgraph);
}

void unregister_graph(Depsgraph *depsgraph)
{
  Main *dmain = depsgraph->dmain;
  GraphRegistry &graph_registry = get_graph_registry();
  VectorSet<Depsgraph *> &graphs = graph_registry.lookup(dmain);
  graphs.remove(depsgraph);

  /* If this was the last depsgraph associated with the main, remove the main entry as well. */
  if (graphs.is_empty()) {
    graph_registry.remove(dmain);
  }
}

Span<Depsgraph *> get_all_registered_graphs(Main *dmain)
{
  VectorSet<Depsgraph *> *graphs = get_graph_registry().lookup_ptr(bmain);
  if (graphs != nullptr) {
    return *graphs;
  }
  return {};
}

}  // namespace dune::deg
