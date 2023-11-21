set(INC
  ../include
  ../../font
  ../../dune
  ../../loader
  ../../lang
  ../../mesh
  ../../graph
  ../../drw
  ../../gpu
  ../../imbuf
  ../../api
  ../../render
  ../../winmngr
  ../../../../intern/clog

  # api_prototypes.h
  ${CMAKE_BINARY_DIR}/source/dune/apu
)

set(INC_SYS
)

set(SRC
  img_btns.cc
  img_drw.cc
  img_edit.cc
  img_ops.cc
  img_seq.cc
  img_undo.cc
  space_img.cc

  image_intern.h
)

set(LIB
  dunefile_dune
  PRIVATE dunefile::dunelib
  PRIVATE dunefile::types
  dunefile_editor_uvedit
  PRIVATE dunefile::intern::guardedalloc
)

if(WITH_IMAGE_OPENJPEG)
  add_definitions(-DWITH_OPENJPEG)
endif()

if(WITH_IMAGE_OPENEXR)
  add_definitions(-DWITH_OPENEXR)
endif()

if(WITH_IMAGE_CINEON)
  add_definitions(-DWITH_CINEON)
endif()

if(WITH_IMAGE_WEBP)
  add_definitions(-DWITH_WEBP)
endif()

dune_add_lib(dunefile_editor_space_img "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# api_prototypes.h types_type_offsets.h
add_dependencies(dunefile_editor_space_img dunefile_api)
