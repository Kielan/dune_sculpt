#pragma once

void register_nodes();

void register_node_type_frame();
void register_node_type_reroute();

void register_node_type_group_input();
void register_node_type_group_output();

void register_composite_nodes();
void register_fn_nodes();
void register_geometry_nodes();
void register_shader_nodes();
void register_texture_nodes();

/* This macro has 3 purposes:
 * - Serves as marker in src code that `discover_nodes.py` can search for to find nodes that
 *   need to be registered. This script generates code that calls the register fns of all
 *   nodes.
 * - Creates a non-static wrapper fn for the registration fn that is then called by
 *   the generated code. This wrapper is necessary because the normal registration is static and
 *   can't be called from somewhere else. It could be made non-static, but then it would require
 *   a declaration to avoid warnings.
 * - Reduces the amount of "magic" w how node registration works. The script could also
 *   search for `node_register` fns directly, but then it would not be apparent in the code
 *   that anything unusual is going on. */
#define NOD_REGISTER_NODE(REGISTER_FN) \
  void REGISTER_FN##_discover(); \
  void REGISTER_FN##_discover() \
  { \
    REGISTER_FN(); \
  }
