set(INC
  ../include
  ../../dune
  ../../lib
  ../../gpu
  ../../types
  ../../api
  ../../windowmanager
  ../../../../intern/glew-mx
  ../../../../intern/guardedalloc
)


set(SRC
  script_edit.c
  script_ops.c
  space_script.c
  script_intern.h
)

set(LIB
)

dune_add_lib(dune_editor_space_script "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
