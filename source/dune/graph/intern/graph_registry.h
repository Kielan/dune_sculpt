#pragma once

#include "intern/graph_type.h"

struct Main;

namespace dune {
namespace graph {

struct Graph;

void register_graph(Graph *graph);
void unregister_graph(Graph *graph);
Span<Graph *> get_all_registered_graphs(Main *dmain);

}  // namespace graph
}  // namespace dune
