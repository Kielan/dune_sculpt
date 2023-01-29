set(INC
  ../include
  ../../dune_kernel
  ../../lib
  ../../translation
  ../../depsgraph
  ../../types
  ../../api
  ../../windowmanager
  ../../sequencer
)

set(INC_SYS

)

set(SRC
  scene_edit.c
)

set(LIB
  df_dune_kernel
  df_dune_lib
)


dune_add_lib(df_editor_scene "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
