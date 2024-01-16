set(INC
  .
  ..
  ../intern
  ../../dune
  ../../lang
  ../../editors/include
  ../../fns
  ../../api
  ../../windowmanager
)

set(INC_SYS
)

set(SRC
  nodes/node_fn_align_euler_to_vector.cc
  nodes/node_fn_axis_angle_to_rotation.cc
  nodes/node_fn_bool_math.cc
  nodes/node_fn_combine_color.cc
  nodes/node_fn_cmp.cc
  nodes/node_fn_euler_to_rotation.cc
  nodes/node_fn_float_to_int.cc
  nodes/node_fn_input_bool.cc
  nodes/node_fn_input_color.cc
  nodes/node_fn_input_int.cc
  nodes/node_fn_input_special_characters.cc
  nodes/node_fn_input_string.cc
  nodes/node_fn_input_vector.cc
  nodes/node_fn_invert_rotation.cc
  nodes/node_fn_quaternion_to_rotation.cc
  nodes/node_fn_random_val.cc
  nodes/node_fn_replace_string.cc
  nodes/node_fn_rotate_euler.cc
  nodes/node_fn_rotate_vector.cc
  nodes/node_fn_rotation_to_axis_angle.cc
  nodes/node_fn_rotation_to_euler.cc
  nodes/node_fn_rotation_to_quaternion.cc
  nodes/node_fn_separate_color.cc
  nodes/node_fn_slice_string.cc
  nodes/node_fn_string_length.cc
  nodes/node_fn_val_to_string.cc

  node_fn_util.cc

  node_fn_util.hh
)

set(LIB
  PRIVATE df::dunelib
  PRIVATE df::types
  df_fns
  PRIVATE df::intern::guardedalloc
)

add_node_discovery(
  df_nodes_fns_generated
  "${SRC}"
  ${CMAKE_CURRENT_BINARY_DIR}/register_fn_nodes.cc
  register_fn_nodes
)

list(APPEND LIB
  df_nodes_fns_generated
)


dune_add_lib(df_nodes_fn "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
target_link_libraries(df_nodes_fns_generated df_nodes_fn)

dune_set_target_unity_build(df_nodes_fn 10)
