/** Public API for Querying and Filtering Depsgraph */

#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct DGraph;
struct Scene;
struct ViewLayer;

/* ------------------------------------------------ */

/* NOTE: Those flags are same bit-mask as #G.debug_flags */

void dgraph_debug_flags_set(struct DGraph *dgraph, int flags);
int dgraph_debug_flags_get(const struct DGraph *dgraph);

void dgraph_debug_name_set(struct DGraph *dgraph, const char *name);
const char *dgraph_debug_name_get(struct DGraph *dgraph);

/* ------------------------------------------------ */

/**
 * Obtain simple statistics about the complexity of the depsgraph.
 * param[out] r_outer:      The number of outer nodes in the graph.
 * param[out] r_operations: The number of operation nodes in the graph.
 * param[out] r_relations:  The number of relations between (executable) nodes in the graph.
 */
void dgraph_stats_simple(const struct DGraph *graph,
                      size_t *r_outer,
                      size_t *r_operations,
                      size_t *r_relations);

/* ************************************************ */
/* Diagram-Based Graph Debugging */

void dgraph_debug_relations_graphviz(const struct DGraph *graph, FILE *fp, const char *label);

void dgraph_debug_stats_gnuplot(const struct DGraph *graph,
                                FILE *fp,
                                const char *label,
                                const char *output_filename);

/* ************************************************ */

/** Compare two dependency graphs. */
bool dgraph_debug_compare(const struct DGraph *graph1, const struct Depsgraph *graph2);

/** Check that dependencies in the graph are really up to date. */
bool dgraph_debug_graph_relations_validate(struct DGraph *graph,
                                        struct Main *dmain,
                                        struct Scene *scene,
                                        struct ViewLayer *view_layer);

/** Perform consistency check on the graph. */
bool dgraph_debug_consistency_check(struct DGraph *graph);

#ifdef __cplusplus
} /* extern "C" */
#endif
