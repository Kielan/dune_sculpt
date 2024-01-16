#pragma once

#include "types_id.h"
#include "types_node.h"

#include "node_composite_register.hh"
#include "node_util.hh"

#include "NOD_composite.hh"
#include "NOD_socket.hh"
#include "NOD_socket_declarations.hh"

#define CMP_SCALE_MAX 12000

bool cmp_node_poll_default(const NodeType *ntype,
                           const NodeTree *ntree,
                           const char **r_disabled_hint);
void cmp_node_update_default(NodeTree *ntree, Node *node);
void cmp_node_type_base(NodeType *ntype, int type, const char *name, short nclass);
