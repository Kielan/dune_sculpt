set(INC
  ../include
  ../../dune
  ../../duneloader
  ../../lang
  ../../graph
  ../../gpu
  ../../api
  ../../window

  # api_prototypes.h
  ${CMAKE_BINARY_DIR}/source/dune/api
)

set(INC_SYS
)

set(SRC
  graph_btns.cc
  graph_drw.cc
  graph_edit.cc
  graph_ops.cc
  graph_sel.cc
  graph_slider_ops.cc
  graph_utils.cc
  graph_view.cc
  space_graph.cc

  graph_intern.h
)

set(LIB
  dune_dunekernel
  PRIVATE dunefile::dunelib
  PRIVATE dunefile::types
  PRIVATE dunefile::intern::guardedalloc
  PRIVATE dunefile::animrig
)

if(WITH_AUDASPACE)
  list(APPEND LIB
    dunefile_intern_audaspace
  )
  list(APPEND INC_SYS
    ${AUDASPACE_C_INCLUDE_DIRS}
  )
  if(WITH_SYSTEM_AUDASPACE)
    list(APPEND LIB
      ${AUDASPACE_C_LIBs}
    )
  endif()
  add_definitions(-DWITH_AUDASPACE)
endif()


dune_add_lib(dunefile_editor_space_graph "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# api_prototypes.h types_type_offsets.h
add_dependencies(dunefile_editor_space_graph dunefile_api)
