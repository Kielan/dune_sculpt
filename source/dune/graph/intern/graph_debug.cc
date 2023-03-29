/**
 * Implementation of tools for debugging the dgraph
 */

#include "lib_utildefines.h"

#include "types_scene.h"

#include "types_object.h"

#include "graph.h"
#include "graph_build.h"
#include "graph_debug.h"
#include "graph_query.h"

#include "intern/debug/graph_debug.h"
#include "intern/graph.h"
#include "intern/graph_relation.h"
#include "intern/graph_type.h"
#include "intern/node/graph_node_component.h"
#include "intern/node/graph_node_id.h"
#include "intern/node/graph_node_time.h"

namespace dune = dune::graph;

void graph_debug_flags_set(Graph *graph, int flags)
{
  graph::Graph *graph = reinterpret_cast<graph::Graph *>(graph);
  graph->debug.flags = flags;
}

int graph_debug_flags_get(const Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  return graph->debug.flags;
}

void graph_debug_name_set(struct Graph *graph, const char *name)
{
  graph::Graph *graph = reinterpret_cast<graph::Graph *>(graph);
  graph->debug.name = name;
}

const char *graph_debug_name_get(struct Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  return graph->debug.name.c_str();
}

bool graph_debug_compare(const struct Graph *graph1, const struct Graph *graph2)
{
  lib_assert(graph1 != nullptr);
  lib_assert(graph2 != nullptr);
  const graph::Graph *graph1 = reinterpret_cast<const graph::Graph *>(graph1);
  const graph::Graph *graph2 = reinterpret_cast<const graph::Graph *>(graph2);
  if (graph1->ops.size() != graph2->ops.size()) {
    return false;
  }
  /* TODO: Currently we only do real stupid check,
   * which is fast but which isn't 100% reliable.
   *
   * Would be cool to make it more robust, but it's good enough
   * for now. Also, proper graph check is actually NP-complex
   * problem. */
  return true;
}

bool graph_debug_graph_relations_validate(Graph *graph,
                                          Main *dmain,
                                          Scene *scene,
                                          ViewLayer *view_layer)
{
  Graph *temp_graph = graph_new(dmain, scene, view_layer, graph_get_mode(graph));
  bool valid = true;
  graph_build_from_view_layer(temp_graph);
  if (!graph_debug_compare(temp_graph, graph)) {
    fprintf(stderr, "ERROR! DGraph wasn't tagged for update when it should have!\n");
    lib_assert_msg(0, "This should not happen!");
    valid = false;
  }
  graph_free(temp_graph);
  return valid;
}

bool graph_debug_consistency_check(Graph *graph)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);
  /* Validate links exists in both directions. */
  for (dune::OpNode *node : graph->ops) {
    for (dune::Relation *rel : node->outlinks) {
      int counter1 = 0;
      for (graph::Relation *tmp_rel : node->outlinks) {
        if (tmp_rel == rel) {
          counter1++;
        }
      }
      int counter2 = 0;
      for (dune::Relation *tmp_rel : rel->to->inlinks) {
        if (tmp_rel == rel) {
          counter2++;
        }
      }
      if (counter1 != counter2) {
        printf(
            "Relation exists in outgoing direction but not in "
            "incoming (%d vs. %d).\n",
            counter1,
            counter2);
        return false;
      }
    }
  }

  for (dune::OpNode *node : dgraph->ops) {
    for (dune::Relation *rel : node->inlinks) {
      int counter1 = 0;
      for (dune::Relation *tmp_rel : node->inlinks) {
        if (tmp_rel == rel) {
          counter1++;
        }
      }
      int counter2 = 0;
      for (dune::Relation *tmp_rel : rel->from->outlinks) {
        if (tmp_rel == rel) {
          counter2++;
        }
      }
      if (counter1 != counter2) {
        printf("Relation exists in incoming direction but not in outcoming (%d vs. %d).\n",
               counter1,
               counter2);
      }
    }
  }

  /* Validate node valency calculated in both directions. */
  for (dune::OpNode *node : graph->ops) {
    node->num_links_pending = 0;
    node->custom_flags = 0;
  }

  for (graph::OpNode *node : graph->ops) {
    if (node->custom_flags) {
      printf("Node %s is twice in the operations!\n", node->id().c_str());
      return false;
    }
    for (dune::Relation *rel : node->outlinks) {
      if (rel->to->type == graph::NodeType::OPERATION) {
        graph::OpNode *to = (graph::OpNode *)rel->to;
        lib_assert(to->num_links_pending < to->inlinks.size());
        ++to->num_links_pending;
      }
    }
    node->custom_flags = 1;
  }

  for (deg::OpNode *node : dgraph->ops) {
    int num_links_pending = 0;
    for (dune::Relation *rel : node->inlinks) {
      if (rel->from->type == graph::NodeType::OPERATION) {
        num_links_pending++;
      }
    }
    if (node->num_links_pending != num_links_pending) {
      printf("Valency mismatch: %s, %u != %d\n",
             node->id().c_str(),
             node->num_links_pending,
             num_links_pending);
      printf("Number of inlinks: %d\n", (int)node->inlinks.size());
      return false;
    }
  }
  return true;
}

/* ------------------------------------------------ */

void graph_stats_simple(const Graph *graph,
                         size_t *r_outer,
                         size_t *r_ops,
                         size_t *r_relations)
{
  const graph::Graph *graph = reinterpret_cast<const graph::Graph *>(graph);

  /* number of operations */
  if (r_ops) {
    /* All operations should be in this list, allowing us to count the total
     * number of nodes. */
    *r_ops = graph->ops.size();
  }

  /* Count number of outer nodes and/or relations between these. */
  if (r_outer || r_relations) {
    size_t tot_outer = 0;
    size_t tot_rels = 0;

    for (graph::IdNode *id_node : graph->id_nodes) {
      tot_outer++;
      for (graph::ComponentNode *comp_node : id_node->components.values()) {
        tot_outer++;
        for (graph::OpNode *op_node : comp_node->ops) {
          tot_rels += op_node->inlinks.size();
        }
      }
    }

    graph::TimeSourceNode *time_source = graph->find_time_source();
    if (time_source != nullptr) {
      tot_rels += time_source->inlinks.size();
    }

    if (r_relations) {
      *r_relations = tot_rels;
    }
    if (r_outer) {
      *r_outer = tot_outer;
    }
  }
}

static graph::string graph_name_for_logging(struct Graph *graph)
{
  const char *name = graph_debug_name_get(dgraph);
  if (name[0] == '\0') {
    return "";
  }
  return "[" + graph::string(name) + "]: ";
}

void graph_debug_print_begin(struct Graph *graph)
{
  fprintf(stdout, "%s", graph_name_for_logging(dgraph).c_str());
}

void graph_debug_print_eval(struct Graph *graph,
                            const char *fn_name,
                            const char *object_name,
                            const void *object_address)
{
  if ((graph_debug_flags_get(graph) & G_DEBUG_GRAPH_EVAL) == 0) {
    return;
  }
  fprintf(stdout,
          "%s%s on %s %s(%p)%s\n",
          graph_name_for_logging(graph).c_str(),
          fn_name,
          object_name,
          graph::color_for_ptr(object_address).c_str(),
          object_address,
          graph::color_end().c_str());
  fflush(stdout);
}

void graph_debug_print_eval_subdata(struct Graph *graph,
                                    const char *fn_name,
                                    const char *object_name,
                                    const void *object_address,
                                    const char *subdata_comment,
                                    const char *subdata_name,
                                    const void *subdata_address)
{
  if ((graph_debug_flags_get(dgraph) & G_DEBUG_GRAPH_EVAL) == 0) {
    return;
  }
  fprintf(stdout,
          "%s%s on %s %s(%p)%s %s %s %s(%p)%s\n",
          graph_name_for_logging(graph).c_str(),
          fn_name,
          object_name,
          graph::color_for_ptr(object_address).c_str(),
          object_address,
          graph::color_end().c_str(),
          subdata_comment,
          subdata_name,
          graph::color_for_ptr(subdata_address).c_str(),
          subdata_address,
          graph::color_end().c_str());
  fflush(stdout);
}

void graph_debug_print_eval_subdata_index(struct DGraph *dgraph,
                                        const char *function_name,
                                        const char *object_name,
                                        const void *object_address,
                                        const char *subdata_comment,
                                        const char *subdata_name,
                                        const void *subdata_address,
                                        const int subdata_index)
{
  if ((dgraph_debug_flags_get(dgraph) & G_DEBUG_DGRAPH_EVAL) == 0) {
    return;
  }
  fprintf(stdout,
          "%s%s on %s %s(%p)%s %s %s[%d] %s(%p)%s\n",
          dgraph_name_for_logging(dgraph).c_str(),
          function_name,
          object_name,
          dgraph::color_for_ptr(object_address).c_str(),
          object_address,
          dgraph::color_end().c_str(),
          subdata_comment,
          subdata_name,
          subdata_index,
          dgraph::color_for_ptr(subdata_address).c_str(),
          subdata_address,
          dgraph::color_end().c_str());
  fflush(stdout);
}

void dgraph_debug_print_eval_parent_typed(struct DGraph *dgraph,
                                       const char *function_name,
                                       const char *object_name,
                                       const void *object_address,
                                       const char *parent_comment,
                                       const char *parent_name,
                                       const void *parent_address)
{
  if ((dgraph_debug_flags_get(dgraph) & G_DEBUG_DGRAPH_EVAL) == 0) {
    return;
  }
  fprintf(stdout,
          "%s%s on %s %s(%p) [%s] %s %s %s(%p)%s\n",
          dgraph_name_for_logging(dgraph).c_str(),
          function_name,
          object_name,
          dgraph::color_for_ptr(object_address).c_str(),
          object_address,
          dgraph::color_end().c_str(),
          parent_comment,
          parent_name,
          dgraph::color_for_ptr(parent_address).c_str(),
          parent_address,
          dgraph::color_end().c_str());
  fflush(stdout);
}

void dgraph_debug_print_eval_time(struct DGraph *graph,
                                  const char *fn_name,
                                  const char *object_name,
                                  const void *object_address,
                                  float time)
{
  if ((dgraph_debug_flags_get(dgraph) & G_DEBUG_DGRAPH_EVAL) == 0) {
    return;
  }
  fprintf(stdout,
          "%s%s on %s %s(%p)%s at time %f\n",
          dgraph_name_for_logging(dgraph).c_str(),
          fn_name,
          object_name,
          dgraph::color_for_ptr(object_address).c_str(),
          object_address,
          dgraph::color_end().c_str(),
          time);
  fflush(stdout);
}
