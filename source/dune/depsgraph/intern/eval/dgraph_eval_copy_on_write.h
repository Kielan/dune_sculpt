#pragma once

#include <stddef.h>

#include "types_id.h"

struct Id;

/* Uncomment this to have verbose log about original and CoW pointers
 * logged, with detailed information when they are allocated, expanded
 * and remapped.
 */
// #define DGRAPH_DEBUG_COW_PTRS

#ifdef DGRAPH_DEBUG_COW_PTRS
#  define DGRAPH_COW_PRINT(format, ...) printf(format, __VA_ARGS__);
#else
#  define DGRAPH_COW_PRINT(format, ...)
#endif

struct DGraph;

namespace dune::dgraph {

struct DGraph;
class DGraphNodeBuilder;
struct IdNode;

/**
 * Makes sure given CoW data-block is brought back to state of the original
 * data-block.
 */
Id *dgraph_update_copy_on_write_datablock(const struct DGraph *depsgraph, const IDNode *id_node);
Id *dgraph_update_copy_on_write_datablock(const struct DGraph *depsgraph, struct ID *id_orig);

/** Helper function which frees memory used by copy-on-written data-block. */
void deg_free_copy_on_write_datablock(struct ID *id_cow);

/**
 * Callback function for depsgraph operation node which ensures copy-on-write
 * data-block is ready for use by further evaluation routines.
 */
void deg_evaluate_copy_on_write(struct ::Depsgraph *depsgraph, const struct IDNode *id_node);

/**
 * Check that given ID is properly expanded and does not have any shallow
 * copies inside.
 */
bool deg_validate_copy_on_write_datablock(ID *id_cow);

/** Tag given ID block as being copy-on-written. */
void deg_tag_copy_on_write_id(struct ID *id_cow, const struct ID *id_orig);

/**
 * Check whether ID data-block is expanded.
 *
 * TODO(sergey): Make it an inline function or a macro.
 */
bool deg_copy_on_write_is_expanded(const struct ID *id_cow);

/**
 * Check whether copy-on-write data-block is needed for given ID.
 *
 * There are some exceptions on data-blocks which are covered by dependency graph
 * but which we don't want to start duplicating.
 *
 * This includes images.
 */
bool deg_copy_on_write_is_needed(const ID *id_orig);
bool deg_copy_on_write_is_needed(const ID_Type id_type);

}  // namespace dune::deg
