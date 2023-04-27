set(INC
  .
  ../atomic
  ../guardedalloc
)

set(INC_SYS

)

set(SRC
  log.c
  log.h
)

set(LIB
)

# Disabled for makesdna/makesrna.
add_definitions(-DWITH_LOG_PTHREADS)

dune_add_lib(dunefile_intern_clog "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
