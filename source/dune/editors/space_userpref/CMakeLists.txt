set(INC
  ../include
  ../../blenkernel
  ../../blenloader
  ../../makesrna
  ../../windowmanager

)

set(INC_SYS

)

set(SRC
  space_userpref.cc
  userpref_ops.cc

  userpref_intern.h
)

set(LIB
  PRIVATE bf::lib
  PRIVATE bf::types
  PRIVATE bf::intern::guardedalloc
)

dune_add_lib(bf_editor_space_userpref "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
