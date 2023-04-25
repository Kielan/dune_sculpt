set(INC
  ../include
  ../../dune/kernel
  ../../dune/lib
  ../../dune/i18n
  ../../dune/graph
  ../../dune/types
  ../../dune/api
  ../../windowmanager
  ../../sequencer
)

set(INC_SYS

)

set(SRC
  scene_edit.c
)

set(LIB
  dunefile_dune_kernel
  dunefile_dune_lib
)


dune_add_lib(dunefile_editor_scene "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
