#pragma once

#include "dune_node.h"

extern NodeTreeType *ntreeType_Geo;

void register_node_tree_type_geo();
void register_node_type_geo_custom_group(NodeType *ntype);

/* Returns true if the socket is a Named Layer Sel field. */
bool is_layer_sel_field(const NodeTreeInterfaceSocket &socket);
