set(INC
  .
  intern
  ../dune
  ../lib
  ../i18n
  ../graph
  ../draw
  ../gpu
  ../imbuf
  ../types
  ../api
  ../nodes
  ../sequencer
  ../simulation
  ../../../intern/atomic
  ../../../intern/guardedalloc
  ../../../intern/mikktspace
  ../../../intern/mantaflow/extern
)


set(SRC
  intern/bake.c
  intern/engine.c
  intern/initrender.c
  intern/multires_bake.c
  intern/pipeline.c
  intern/render_result.c
  intern/texture_image.c
  intern/texture_margin.cc
  intern/texture_pointdensity.c
  intern/texture_procedural.c
  intern/zbuf.c
  render_bake.h
  render_engine.h
  render_multires_bake.h
  render_pipeline.h
  render_texture.h
  render_texture_margin.h
  intern/pipeline.h
  intern/render_result.h
  intern/render_types.h
  intern/texture_common.h
  intern/zbuf.h
)

set(LIB)

if(WITH_IMAGE_OPENEXR)
  list(APPEND LIB
    dunefile_imbuf_openexr
  )
  add_definitions(-DWITH_OPENEXR)
endif()

if(WITH_FREESTYLE)
  list(APPEND INC
    ../freestyle
  )
  list(APPEND LIB
    dunefile_freestyle
  )
  add_definitions(-DWITH_FREESTYLE)
endif()


dune_add_lib_nolist(dune_render "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
Give feedback
