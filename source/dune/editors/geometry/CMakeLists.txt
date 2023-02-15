set(INC
  ../include
  ../../dune
  ../../lib
  ../../depsgraph
  ../../functions
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
  df_dunekernel
  df_depsgraph
  df_editor_object
  df_windowmanager
)

dune_add_lib(df_editor_geometry "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
