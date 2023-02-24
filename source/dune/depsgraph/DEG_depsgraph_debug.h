/** Public API for Querying and Filtering Depsgraph */

#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Depsgraph;
struct Scene;
struct ViewLayer;

/* ------------------------------------------------ */

/* NOTE: Those flags are same bit-mask as #G.debug_flags */

void deg_debug_flags_set(struct Depsgraph *depsgraph, int flags);
int deg_debug_flags_get(const struct Depsgraph *depsgraph);

void deg_debug_name_set(struct Depsgraph *depsgraph, const char *name);
const char *deg_debug_name_get(struct Depsgraph *depsgraph);

/* ------------------------------------------------ */

/**
 * Obtain simple statistics about the complexity of the depsgraph.
 * param[out] r_outer:      The number of outer nodes in the graph.
 * param[out] r_operations: The number of operation nodes in the graph.
 * param[out] r_relations:  The number of relations between (executable) nodes in the graph.
 */
void deg_stats_simple(const struct Depsgraph *graph,
                      size_t *r_outer,
                      size_t *r_operations,
                      size_t *r_relations);

/* ************************************************ */
/* Diagram-Based Graph Debugging */

void deg_debug_relations_graphviz(const struct Depsgraph *graph, FILE *fp, const char *label);

void deg_debug_stats_gnuplot(const struct Depsgraph *graph,
                             FILE *fp,
                             const char *label,
                             const char *output_filename);

/* ************************************************ */

/** Compare two dependency graphs. */
bool deg_debug_compare(const struct Depsgraph *graph1, const struct Depsgraph *graph2);

/** Check that dependencies in the graph are really up to date. */
bool deg_debug_graph_relations_validate(struct Depsgraph *graph,
                                        struct Main *dmain,
                                        struct Scene *scene,
                                        struct ViewLayer *view_layer);

/** Perform consistency check on the graph. */
bool deg_debug_consistency_check(struct Depsgraph *graph);

#ifdef __cplusplus
} /* extern "C" */
#endif
