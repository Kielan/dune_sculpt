set(INC
  .
  ..
  ../../source/dune/lib
)

set(INC_SYS)


set(SRC
  intern/MEM_CacheLimiter.cpp

  MEM_Allocator.h
  MEM_CacheLimiter.h
  MEM_RefCounted.h
)

set(LIB
)

dune_add_lib(bf_intern_memutil "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
