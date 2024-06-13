#pragma once
#include "dune_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal fns for editor. */
struct NodeSocket *node_group_find_input_socket(struct Node *groupnode, const char *id);
struct NodeSocket *node_group_find_output_socket(struct Node *groupnode, const char *id);

struct NodeSocket *node_group_input_find_socket(struct Node *node, const char *id);
struct NodeSocket *node_group_output_find_socket(struct Node *node, const char *id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace dune::nodes {

void node_group_decl(NodeDeclBuilder &b);

}  // namespace dune::nodes
