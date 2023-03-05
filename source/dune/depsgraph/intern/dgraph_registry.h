#pragma once

#include "intern/dgraph_type.h"

struct Main;

namespace dune {
namespace dgraph {

struct DGraph;

void register_graph(DGraph *dgraph);
void unregister_graph(DGraph *dgraph);
Span<DGraph *> get_all_registered_graphs(Main *dmain);

}  // namespace dgraph
}  // namespace dune
