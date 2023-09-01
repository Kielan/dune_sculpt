set(INC
  .
  ../dune
  ../lib
  ../lang
  ../gpu
  ../imbuf
  ../types
  ../api
  ../../../intern/glew-mx
  ../../../intern/guardedalloc
)

set(INC_SYS
  ${FREETYPE_INCLUDE_DIRS}
)

set(SRC
  intern/font.c
  intern/font_default.c
  intern/font_dir.c
  intern/font_font.c
  intern/font_font_default.c
  intern/font_glyph.c
  intern/font_thumbs.c
  intern/font_util.c

  font_api.h
  intern/font_internal.h
  intern/font_internal_types.h
)

set(LIB
  bf_gpu
  bf_intern_guardedalloc

  ${FREETYPE_LIBRARIES} ${BROTLI_LIBRARIES}
)

if(WIN32)
  list(APPEND SRC
    intern/blf_font_win32_compat.c
  )
endif()

if(WITH_PYTHON)
  add_definitions(-DWITH_PYTHON)
  list(APPEND INC
    ../python
  )
endif()


blender_add_lib(bf_blenfont "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
