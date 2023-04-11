#pragma once

#include "lib_compiler_attrs.h"

#include "mesh.h"

#ifndef NDEBUG
char *mesh_debug_info(Mesh *mesh) ATTR_NONNULL(1) ATTR_MALLOC ATTR_WARN_UNUSED_RESULT;
void mesh_debug_print(Mesh *mesh) ATTR_NONNULL(1);
#endif /* NDEBUG */
