/**
 * Implementation of tools for debugging the dgraph
 */

#include "lib_utildefines.h"

#include "types_scene.h"

#include "types_object.h"

#include "dgraph.h"
#include "dgraph_build.h"
#include "dgraph_debug.h"
#include "dgraph_query.h"

#include "intern/debug/dgraph_debug.h"
#include "intern/dgraph.h"
#include "intern/dgraph_relation.h"
#include "intern/dgraph_type.h"
#include "intern/node/dgraph_node_component.h"
#include "intern/node/dgraph_node_id.h"
#include "intern/node/dgraph_node_time.h"

namespace dune = dune::dgraph;

void DGRAPH_debug_flags_set(DGraph *dgraph, int flags)
{
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(dgraph);
  dgraph->debug.flags = flags;
}

int DGRAPH_debug_flags_get(const DGraph *dgraph)
{
  const dgraph::DGraph *dgraph = reinterpret_cast<const dgraph::DGraph *>(dgraph);
  return dgraph->debug.flags;
}

void DGRAPH_debug_name_set(struct DGraph *dgraph, const char *name)
{
  dgraph::DGraph *dgraph = reinterpret_cast<dgraph::DGraph *>(dgraph);
  dgraph->debug.name = name;
}

const char *DGRAPH_debug_name_get(struct DGraph *dgraph)
{
  const dgraph::DGraph *dgraph = reinterpret_cast<const dgraph::DGraph *>(dgraph);
  return dgraph->debug.name.c_str();
}

bool DGRAPH_debug_compare(const struct DGraph *graph1, const struct DGraph *graph2)
{
  lib_assert(graph1 != nullptr);
  lib_assert(graph2 != nullptr);
  const dgraph::DGraph *dgraph1 = reinterpret_cast<const dgraph::DGraph *>(graph1);
  const dgraph::DGraph *dgraph2 = reinterpret_cast<const dgraph::DGraph *>(graph2);
  if (dgraph1->ops.size() != deg_graph2->ops.size()) {
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

bool DGRAPH_debug_graph_relations_validate(DGraph *graph,
                                           Main *dmain,
                                           Scene *scene,
                                           ViewLayer *view_layer)
{
  DGraph *temp_dgraph = DGRAPH_graph_new(dmain, scene, view_layer, DGRAPH_get_mode(graph));
  bool valid = true;
  DGRAPH_graph_build_from_view_layer(temp_dgraph);
  if (!DGRAPH_debug_compare(temp_dgraph, graph)) {
    fprintf(stderr, "ERROR! DGraph wasn't tagged for update when it should have!\n");
    lib_assert_msg(0, "This should not happen!");
    valid = false;
  }
  DGRAPH_graph_free(temp_dgraph);
  return valid;
}

bool DGRAPH_debug_consistency_check(DGraph *graph)
{
  const dgraph::DGraph *dgraph = reinterpret_cast<const dgraph::DGraph *>(graph);
  /* Validate links exists in both directions. */
  for (dune::OpNode *node : dgraph->ops) {
    for (dune::Relation *rel : node->outlinks) {
      int counter1 = 0;
      for (deg::Relation *tmp_rel : node->outlinks) {
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
  for (dune::OpNode *node : dgraph->ops) {
    node->num_links_pending = 0;
    node->custom_flags = 0;
  }

  for (deg::OpNode *node : dgraph->ops) {
    if (node->custom_flags) {
      printf("Node %s is twice in the operations!\n", node->id().c_str());
      return false;
    }
    for (dune::Relation *rel : node->outlinks) {
      if (rel->to->type == dgraph::NodeType::OPERATION) {
        deg::OperationNode *to = (dgraph::OpNode *)rel->to;
        lib_assert(to->num_links_pending < to->inlinks.size());
        ++to->num_links_pending;
      }
    }
    node->custom_flags = 1;
  }

  for (deg::OpNode *node : dgraph->ops) {
    int num_links_pending = 0;
    for (dune::Relation *rel : node->inlinks) {
      if (rel->from->type == dgraph::NodeType::OPERATION) {
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

void DGRAPH_stats_simple(const DGraph *graph,
                      size_t *r_outer,
                      size_t *r_ops,
                      size_t *r_relations)
{
  const dgraph::DGraph *dgraph = reinterpret_cast<const dgraph::DGraph *>(graph);

  /* number of operations */
  if (r_ops) {
    /* All operations should be in this list, allowing us to count the total
     * number of nodes. */
    *r_ops = dgraph->ops.size();
  }

  /* Count number of outer nodes and/or relations between these. */
  if (r_outer || r_relations) {
    size_t tot_outer = 0;
    size_t tot_rels = 0;

    for (dgraph::IdNode *id_node : dgraph->id_nodes) {
      tot_outer++;
      for (dgraph::ComponentNode *comp_node : id_node->components.values()) {
        tot_outer++;
        for (dgraph::OpNode *op_node : comp_node->ops) {
          tot_rels += op_node->inlinks.size();
        }
      }
    }

    dgraph::TimeSourceNode *time_source = dgraph->find_time_source();
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

static dgraph::string dgraph_name_for_logging(struct DGraph *dgraph)
{
  const char *name = DGRAPH_debug_name_get(dgraph);
  if (name[0] == '\0') {
    return "";
  }
  return "[" + dgraph::string(name) + "]: ";
}

void DGRAPH_debug_print_begin(struct DGraph *dgraph)
{
  fprintf(stdout, "%s", dgraph_name_for_logging(dgraph).c_str());
}

void DGRAPH_debug_print_eval(struct DGraph *dgraph,
                          const char *fn_name,
                          const char *object_name,
                          const void *object_address)
{
  if ((DGRAPH_debug_flags_get(dgraph) & G_DEBUG_DGRAPH_EVAL) == 0) {
    return;
  }
  fprintf(stdout,
          "%s%s on %s %s(%p)%s\n",
          dgraph_name_for_logging(dgraph).c_str(),
          function_name,
          object_name,
          dgraph::color_for_ptr(object_address).c_str(),
          object_address,
          dgraph::color_end().c_str());
  fflush(stdout);
}

void DGRAPH_debug_print_eval_subdata(struct DGraph *dgraph,
                                  const char *function_name,
                                  const char *object_name,
                                  const void *object_address,
                                  const char *subdata_comment,
                                  const char *subdata_name,
                                  const void *subdata_address)
{
  if ((DGRAPH_debug_flags_get(dgraph) & G_DEBUG_DGRAPH_EVAL) == 0) {
    return;
  }
  fprintf(stdout,
          "%s%s on %s %s(%p)%s %s %s %s(%p)%s\n",
          dgraph_name_for_logging(dgraph).c_str(),
          function_name,
          object_name,
          dgraph::color_for_pointer(object_address).c_str(),
          object_address,
          dgraph::color_end().c_str(),
          subdata_comment,
          subdata_name,
          dgraph::color_for_ptr(subdata_address).c_str(),
          subdata_address,
          dgraph::color_end().c_str());
  fflush(stdout);
}

void DGRAPH_debug_print_eval_subdata_index(struct DGraph *dgraph,
                                        const char *function_name,
                                        const char *object_name,
                                        const void *object_address,
                                        const char *subdata_comment,
                                        const char *subdata_name,
                                        const void *subdata_address,
                                        const int subdata_index)
{
  if ((DGRAPH_debug_flags_get(dgraph) & G_DEBUG_DGRAPH_EVAL) == 0) {
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

void DGRAPH_debug_print_eval_parent_typed(struct DGraph *dgraph,
                                       const char *function_name,
                                       const char *object_name,
                                       const void *object_address,
                                       const char *parent_comment,
                                       const char *parent_name,
                                       const void *parent_address)
{
  if ((DGRAPH_debug_flags_get(dgraph) & G_DEBUG_DGRAPH_EVAL) == 0) {
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

void DGRAPH_debug_print_eval_time(struct DGraph *dgraph,
                               const char *function_name,
                               const char *object_name,
                               const void *object_address,
                               float time)
{
  if ((DGRAPH_debug_flags_get(dgraph) & G_DEBUG_DGRAPH_EVAL) == 0) {
    return;
  }
  fprintf(stdout,
          "%s%s on %s %s(%p)%s at time %f\n",
          dgraph_name_for_logging(dgraph).c_str(),
          function_name,
          object_name,
          dgraph::color_for_ptr(object_address).c_str(),
          object_address,
          dgraph::color_end().c_str(),
          time);
  fflush(stdout);
}
