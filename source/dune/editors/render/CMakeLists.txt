set(INC
  ../include
  ../../dune/kernel
  ../../dune/lib
  ../../dune/loader
  ../../dune/i18n
  ../../dune/mesh
  ../../dune/graph
  ../../draw
  ../../gpu
  ../../imbuf
  ../../dune/types
  ../../dune/api
  ../../nodes
  ../../render
  ../../sequencer
  ../../windowmanager
  ../../../../intern/glew-mx
  ../../../../intern/guardedalloc
  # api_prototypes.h
  ${CMAKE_BINARY_DIR}/source/dune/api
)

set(SRC
  render_internal.cc
  render_opengl.cc
  render_ops.cc
  render_preview.cc
  render_shading.cc
  render_update.cc
  render_view.cc

  render_intern.hh
)

set(LIB
)

if(WITH_HEADLESS)
  add_definitions(-DWITH_HEADLESS)
else()
  list(APPEND LIB
    bf_editor_datafiles
  )
endif()


if(WITH_FREESTYLE)
  list(APPEND INC
    ../../freestyle
  )
  add_definitions(-DWITH_FREESTYLE)
endif()

dune_add_lib(dunefile_editor_render "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# api_prototypes.h
add_dependencies(dunefile_editor_render dunefile_api)
