#pragma once

#include <stddef.h>

#include "types_id.h"

struct Id;

/* Uncomment this to have verbose log about original and CoW pointers
 * logged, with detailed information when they are allocated, expanded
 * and remapped.
 */
// #define GRAPH_DEBUG_COW_PTRS

#ifdef GRAPH_DEBUG_COW_PTRS
#  define GRAPH_COW_PRINT(format, ...) printf(format, __VA_ARGS__);
#else
#  define GRAPH_COW_PRINT(format, ...)
#endif

struct Graph;

namespace dune::graph {

struct Graph;
class GraphNodeBuilder;
struct IdNode;

/**
 * Makes sure given CoW data-block is brought back to state of the original
 * data-block.
 */
Id *dgraph_update_copy_on_write_datablock(const struct DGraph *dgraph, const IdNode *id_node);
Id *dgraph_update_copy_on_write_datablock(const struct DGraph *dgraph, struct Id *id_orig);

/** Helper function which frees memory used by copy-on-written data-block. */
void graph_free_copy_on_write_datablock(struct Id *id_cow);

/**
 * Callback function for dgraph operation node which ensures copy-on-write
 * data-block is ready for use by further evaluation routines.
 */
void graph_evaluate_copy_on_write(struct ::DGraph *dgraph, const struct IdNode *id_node);

/**
 * Check that gives id is properly expanded and does not have any shallow
 * copies inside.
 */
bool graph_validate_copy_on_write_datablock(Id *id_cow);

/** Tag given id block as being copy-on-written. */
void graph_tag_copy_on_write_id(struct Id *id_cow, const struct Id *id_orig);

/**
 * Check whether id data-block is expanded.
 *
 * TODO: Make it an inline function or a macro.
 */
bool graph_copy_on_write_is_expanded(const struct Id *id_cow);

/**
 * Check whether copy-on-write data-block is needed for given id.
 *
 * There are some exceptions on data-blocks which are covered by dependency graph
 * but which we don't want to start duplicating.
 *
 * This includes images.
 */
bool graph_copy_on_write_is_needed(const Id *id_orig);
bool graph_copy_on_write_is_needed(const IdType id_type);

}  // namespace dune::graph
