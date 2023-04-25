set(INC
  ../include
  ../../dune
  ../../lib
  ../../graph
  ../../fns
  ../../types
  ../../api
  ../../windowmanager
  ../../../../intern/guardedalloc
)

set(INC_SYS
)

set(SRC
  geometry_attributes.cc
  geometry_ops.cc
  geometry_intern.hh
)

set(LIB
  dunefile_kernel
  dunefile_graph
  dunefile_editor_object
  dunefile_windowmanager
)

dune_add_lib(df_editor_geometry "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
