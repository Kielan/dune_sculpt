set(INC
  ../include
  ../../dune
  ../../lang
  ../../gpu
  ../../api
  ../../seq
  ../../window
  ${CMAKE_BINARY_DIR}/source/dune/api
)

set(INC_SYS
)

set(SRC
  anim_channels_defines.cc
  anim_channels_edit.cc
  anim_deps.cc
  anim_drw.cc
  anim_filter.cc
  anim_ipo_utils.cc
  anim_markers.cc
  anim_motion_paths.cc
  anim_ops.cc
  drivers.cc
  fmod_ui.cc
  keyframes_drw.cc
  keyframes_edit.cc
  keyframes_general.cc
  keyframes_keylist.cc
  keyframing.cc
  keyingsets.cc
  time_scrub_ui.cc

  anim_intern.h
)

set(LIB
  file_dune
  PRIVATE file::animrig
  PRIVATE file::lib
  PRIVATE file::graph
  PRIVATE file::types
  PRIVATE file::intern::clog
  PRIVATE file::intern::guardedalloc
)

if(WITH_EXPERIMENTAL_FEATURES)
  add_definitions(-DWITH_PEN_V3)
endif()

dune_add_lib(file_editor_anim "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# RNA_prototypes.h
add_dependencies(file_editor_anim file_api)

if(WITH_GTESTS)
  set(TEST_SRC
    keyframes_keylist_test.cc
  )
  set(TEST_INC
  )
  set(TEST_LIB
  )
  include(GTestTesting)
  blender_add_test_lib(file_editor_anim_tests "${TEST_SRC}" "${INC};${TEST_INC}" "${INC_SYS}" "${LIB};${TEST_LIB}")
endif()
