#pragma once

struct Collection;
struct ListBase;

namespace dune {
namespace dgraph {

struct DGraph;

ListBase *build_effector_relations(DGraph *graph, Collection *collection);
ListBase *build_collision_relations(DGraph *graph,
                                    Collection *collection,
                                    unsigned int modifier_type);
void clear_phys_relations(DGraph *graph);

}  // namespace dgraph
}  // namespace dune
