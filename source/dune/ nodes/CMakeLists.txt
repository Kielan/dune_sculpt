set(DISCOVER_NODES_PATH ${CMAKE_CURRENT_SOURCE_DIR}/intern/discover_nodes.py)

# Creates a lib w the given name that exposes one fn which calls
# the register functions of all nodes in the provided src files.
function(add_node_discovery
  lib_name
  sources
  output_file
  output_fn
  )

  set(extra_args "")

  # See `add_definitions(-DUSE_MAKEFILE_WORKAROUND)` comment for `makesrna`.
  if(CMAKE_GENERATOR MATCHES ".*Unix Makefiles")
    set(extra_args "--use-makefile-workaround")
  endif()

  add_custom_command(
    OUTPUT
      ${output_file}
    COMMAND
      ${PYTHON_EXECUTABLE}
        ${DISCOVER_NODES_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${output_file}
        ${output_function}
        ${sources}
        ${extra_args}
    DEPENDS
      ${SRC}
      ${DISCOVER_NODES_PATH}
  )
  set_source_files_properties(${output_file} PROPERTIES GENERATED TRUE)
  add_library(${lib_name} ${output_file})
endfunction()

add_subdirectory(composite)
add_subdirectory(fn)
add_subdirectory(geometry)
add_subdirectory(shader)
add_subdirectory(texture)

set(INC
  .
  composite
  fn
  geometry
  intern
  shader
  texture
  ../editors/include
  ../dune
  ../loader
  ../lang
  ../mesh
  ../fns
  ../geometry
  ../gpu
  ../imbuf
  ../makesapi
  ../modifiers
  ../render
  ../windowmanager
  ../../../extern/fmtlib/include

  # api_prototypes.h
  ${CMAKE_BINARY_DIR}/source/dune/makesapi
)

set(INC_SYS
)

set(SRC
  intern/derived_node_tree.cc
  intern/geometry_nodes_ex.cc
  intern/geometry_nodes_lazy_fn.cc
  intern/geometry_nodes_log.cc
  intern/math_fns.cc
  intern/node_common.cc
  intern/node_declaration.cc
  intern/node_exec.cc
  intern/node_geometry_ex.cc
  intern/node_multi_fn.cc
  intern/node_register.cc
  intern/node_api_define.cc
  intern/node_socket.cc
  intern/node_socket_declarations.cc
  intern/node_util.cc
  intern/node_zone_socket_items.cc
  intern/socket_search_link.cc

  NOD_common.h
  NOD_composite.hh
  NOD_derived_node_tree.hh
  NOD_geometry.hh
  NOD_geometry_ex.hh
  NOD_geometry_nodes_ex.hh
  NOD_geometry_nodes_lazy_fn.hh
  NOD_geometry_nodes_log.hh
  NOD_math_fns.hh
  NOD_multi_fn.hh
  NOD_node_declaration.hh
  NOD_node_extra_info.hh
  node_register.hh
  node_api_define.hh
  node_shader.h
  node_socket.hh
  node_socket_declarations.hh
  node_socket_declarations_geometry.hh
  node_socket_items.hh
  node_socket_search_link.hh
  node_static_types.h
  node_texture.h
  node_zone_socket_items.hh
  intern/node_common.h
  intern/node_ex.hh
  intern/node_util.hh
)

set(LIB
  PRIVATE df::dunelib
  df_mesh
  PRIVATE df::graph
  PRIVATE df::types
  df_fns
  PRIVATE df::intern::guardedalloc
  df_nodes_composite
  df_nodes_fn
  df_nodes_geometry
  df_nodes_shader
  df_nodes_texture
  extern_fmtlib
)

if(WITH_BULLET)
  list(APPEND INC_SYS
    ${BULLET_INCLUDE_DIRS}
    ../../../intern/rigidbody
  )
  list(APPEND LIB
    ${BULLET_LIBRARIES}
  )
  add_definitions(-DWITH_BULLET)
endif()

if(WITH_TBB)
  list(APPEND INC_SYS
    ${TBB_INCLUDE_DIRS}
  )
  add_definitions(-DWITH_TBB)
  if(WIN32)
    # TBB includes Windows.h which will define min/max macros
    # that will collide with the stl versions.
    add_definitions(-DNOMINMAX)
  endif()
endif()

if(WITH_IMAGE_OPENEXR)
  add_definitions(-DWITH_OPENEXR)
endif()

if(WITH_FREESTYLE)
  add_definitions(-DWITH_FREESTYLE)
endif()

if(WITH_OPENSUBDIV)
  add_definitions(-DWITH_OPENSUBDIV)
endif()

if(WITH_GMP)
  add_definitions(-DWITH_GMP)

  list(APPEND INC_SYS
    ${GMP_INCLUDE_DIRS}
  )

  list(APPEND LIB
    ${GMP_LIBRARIES}
  )
endif()

if(WITH_OPENVDB)
  list(APPEND INC
    ../../../intern/openvdb
  )
  list(APPEND INC_SYS
    ${OPENVDB_INCLUDE_DIRS}
  )
  add_definitions(-DWITH_OPENVDB ${OPENVDB_DEFINITIONS})
endif()

blender_add_lib(bf_nodes "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# RNA_prototypes.h
add_dependencies(bf_nodes bf_rna)
