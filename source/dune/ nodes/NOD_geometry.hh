#pragma once

#include "dune_node.h"

extern NodeTreeType *ntreeType_Geometry;

void register_node_tree_type_geo();
void register_node_type_geo_custom_group(NodeType *ntype);

/* Returns true if the socket is a Named Layer Selection field. */
bool is_layer_selection_field(const NodeTreeInterfaceSocket &socket);
