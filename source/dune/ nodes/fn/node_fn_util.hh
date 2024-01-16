#pragma once

#include <string.h>

#include "lib_math_vector.hh"
#include "lib_utildefines.h"

#include "mem_guardedalloc.h"

#include "types_node.h"

#include "dune_node.hh"

#include "NOD_multi_fn.hh"
#include "NOD_register.hh"
#include "NOD_socket_declarations.hh"

#include "node_util.hh"

#include "fn_multi_fn_builder.hh"

#include "api_access.hh"

void fn_node_type_base(NodeType *ntype, int type, const char *name, short nclass);
