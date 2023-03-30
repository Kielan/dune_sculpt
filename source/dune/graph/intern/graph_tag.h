#include "intern/node/graph_node.h"

struct Id;
struct Main;

namespace dune {
namespace graph {

struct Graph;

/* Get type of a node which corresponds to a ID_RECALC_GEOMETRY tag. */
NodeType geometry_tag_to_component(const Id *id);

/* Tag given ID for an update in all registered dependency graphs. */
void id_tag_update(Main *dmain, Id *id, int flag, eUpdateSource update_source);

/* Tag given ID for an update with in a given dependency graph. */
void graph_id_tag_update(
    Main *dmain, Graph *graph, Id *id, int flag, eUpdateSource update_source);

/* Tag IDs of the graph for the visibility update tags.
 * Will do nothing if the graph is not tagged for visibility update. */
void graph_tag_ids_for_visible_update(Graph *graph);

}  // namespace graph
}  // namespace dune
