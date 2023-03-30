#pragma once

struct Collection;
struct ListBase;

namespace dune {
namespace graph {

struct Graph;

ListBase *build_effector_relations(Graph *graph, Collection *collection);
ListBase *build_collision_relations(Graph *graph,
                                    Collection *collection,
                                    unsigned int modifier_type);
void clear_phys_relations(DGraph *graph);

}  // namespace dgraph
}  // namespace dune
