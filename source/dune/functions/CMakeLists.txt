set(INC
  .
  ../lib
  ../types
  ../../../intern/guardedalloc
)

set(INC_SYS
)

set(SRC
  intern/cpp_types.cc
  intern/field.cc
  intern/multi_fn.cc
  intern/multi_fn_builder.cc
  intern/multi_fn_params.cc
  intern/multi_fn_procedure.cc
  intern/multi_fn_procedure_builder.cc
  intern/multi_fn_procedure_executor.cc
  intern/multi_fn_procedure_optimization.cc

  fn_field.hh
  fn_field_cpp_type.hh
  fn_multi_fn.hh
  fn_multi_function_builder.hh
  fn_multi_fn_context.hh
  fn_multi_fn_data_type.hh
  fn_multi_function_param_type.hh
  fn_multi_fn_params.hh
  fn_multi_fn_procedure.hh
  fn_multi_fn_procedure_builder.hh
  fn_multi_fn_procedure_executor.hh
  fn_multi_fn_procedure_optimization.hh
  fn_multi_fn_signature.hh
)

set(LIB
  df_dunelib
)

if(WITH_TBB)
  add_definitions(-DWITH_TBB)
  if(WIN32)
    # TBB includes Windows.h which will define min/max macros
    # that will collide with the stl versions.
    add_definitions(-DNOMINMAX)
  endif()
  list(APPEND INC_SYS
    ${TBB_INCLUDE_DIRS}
  )

  list(APPEND LIB
    ${TBB_LIBRARIES}
  )
endif()

dune_add_lib(df_functions "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

if(WITH_GTESTS)
  set(TEST_SRC
    tests/fn_field_test.cc
    tests/dn_multi_fn_procedure_test.cc
    tests/fn_multi_fn_test.cc

    tests/fun_multi_fn_test_common.hh
  )
  set(TEST_LIB
    bf_functions
  )
  include(GTestTesting)
  dune_add_test_lib(df_functions_tests "${TEST_SRC}" "${INC};${TEST_INC}" "${INC_SYS}" "${LIB};${TEST_LIB}")
endif()
