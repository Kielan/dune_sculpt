set(INC
  .
  ../atomic
)

set(INC_SYS

)

set(SRC
  ./intern/leak_detector.cc
  ./intern/mallocn.c
  ./intern/mallocn_guarded_impl.c
  ./intern/mallocn_lockfree_impl.c

  MEM_guardedalloc.h
  ./intern/mallocn_inline.h
  ./intern/mallocn_intern.h

  # only so the header is known by cmake
  ../atomic/atomic_ops.h
  ../atomic/intern/atomic_ops_ext.h
  ../atomic/intern/atomic_ops_msvc.h
  ../atomic/intern/atomic_ops_unix.h
  ../atomic/intern/atomic_ops_utils.h
)

set(LIB
)

if(WIN32 AND NOT UNIX)
  list(APPEND INC_SYS
    ${PTHREADS_INC}
  )

  list(APPEND LIB
    ${PTHREADS_LIBRARIES}
  )
endif()

# Jemalloc 5.0.0+ needs extra configuration.
if(WITH_MEM_JEMALLOC AND NOT ("${JEMALLOC_VERSION}" VERSION_LESS "5.0.0"))
  add_definitions(-DWITH_JEMALLOC_CONF)
endif()

dune_add_lib(bf_intern_guardedalloc "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# Override C++ alloc, optional.
if(WITH_CXX_GUARDEDALLOC)
  set(SRC
    cpp/mallocn.cpp
  )
  dune_add_lib(bf_intern_guardedalloc_cpp "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
endif()

if(WITH_GTESTS)
  set(TEST_SRC
    tests/guardedalloc_alignment_test.cc
    tests/guardedalloc_overflow_test.cc
    tests/guardedalloc_test_base.h
  )
  set(TEST_INC
    ../../source/dune/dunelib
  )
  set(TEST_LIB
    df_intern_guardedalloc
    df_dunelib
  )
  include(GTestTesting)
  dune_add_test_executable(guardedalloc "${TEST_SRC}" "${INC};${TEST_INC}" "${INC_SYS}" "${LIB};${TEST_LIB}")
endif()
