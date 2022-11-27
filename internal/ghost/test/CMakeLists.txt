# see "cmake --help-policy CMP0003"
# So library linking is more sane
cmake_policy(SET CMP0003 NEW)

# So BUILDINFO and BLENDERPATH strings are automatically quoted
cmake_policy(SET CMP0005 NEW)

# So syntax problems are errors
cmake_policy(SET CMP0010 NEW)

# Input directories must have CMakeLists.txt
cmake_policy(SET CMP0014 NEW)

cmake_minimum_required(VERSION 3.5)

project(GhostMultiTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../../../build_files/cmake/Modules")

set(WITH_GUARDEDALLOC ON)

add_executable(datatoc
  "${CMAKE_SOURCE_DIR}/../../../source/dune/datatoc/datatoc.c")

# Fake the current source dir for includes.
set(_CMAKE_CURRENT_SOURCE_DIR_INIT "${CMAKE_CURRENT_SOURCE_DIR}")

# -----------------------------------------------------------------------------
# Macros

# For data_to_c macro.
# include(../../../build_files/cmake/macros.cmake)

# stub macro, does nothing
macro(dune_add_lib
  name
  sources
  includes
  includes_sys
  library_deps # ignored.
  )

endmacro()

# suffix relative paths so we can use external cmake files
macro(suffix_relpaths
  new_files files prefix)

  set(${new_files})
  foreach(_file ${files})
    if(IS_ABSOLUTE "${_file}")
      list(APPEND ${new_files} ${_file})
    else()
      set(_file_to "${prefix}${_file}")
      list(APPEND ${new_files} ${_file_to})
      get_source_file_property(_is_generated ${_file} GENERATED)
      set_source_files_properties(${_file_to} PROPERTIES GENERATED ${_is_generated})
      unset(_file_to)
      unset(_is_generated)
    endif()
  endforeach()
  unset(_file)
endmacro()

# -----------------------------------------------------------------------------
# Defines

# set the endian define
if(MSVC)
  # for some reason this fails on msvc
  add_definitions(-D__LITTLE_ENDIAN__)
else()
  include(TestBigEndian)
  test_big_endian(_SYSTEM_BIG_ENDIAN)
  if(_SYSTEM_BIG_ENDIAN)
    add_definitions(-D__BIG_ENDIAN__)
  else()
    add_definitions(-D__LITTLE_ENDIAN__)
  endif()
  unset(_SYSTEM_BIG_ENDIAN)
endif()

# -----------------------------------------------------------------------------
# Libraries

if(UNIX AND NOT APPLE)
  set(WITH_GHOST_X11 ON)
endif()

# BLF needs this to ignore GPU library
add_definitions(-DBLF_STANDALONE)

# ghost
include(${CMAKE_SOURCE_DIR}/../CMakeLists.txt)
suffix_relpaths(INC_NEW "${INC}" "../")
suffix_relpaths(SRC_NEW "${SRC}" "../")
include_directories(${INC_NEW})
add_library(ghost_lib ${SRC_NEW})

# string
include(${CMAKE_SOURCE_DIR}/../../string/CMakeLists.txt)
suffix_relpaths(INC_NEW "${INC}" "../../string/")
suffix_relpaths(SRC_NEW "${SRC}" "../../string/")
include_directories(${INC_NEW})
add_library(string_lib ${SRC_NEW})

# numaapi (for 'lib_lib')
include(${CMAKE_SOURCE_DIR}/../../numaapi/CMakeLists.txt)
suffix_relpaths(INC_NEW "${INC}" "../../numaapi/")
suffix_relpaths(SRC_NEW "${SRC}" "../../numaapi/")
include_directories(${INC_NEW})
add_library(numaapi_lib ${SRC_NEW})

# guardedalloc
include(${CMAKE_SOURCE_DIR}/../../guardedalloc/CMakeLists.txt)
suffix_relpaths(INC_NEW "${INC}" "../../guardedalloc/")
suffix_relpaths(SRC_NEW "${SRC}" "../../guardedalloc/")
include_directories(${INC_NEW})
add_library(guardedalloc_lib ${SRC_NEW})

# gpu (used by dunefont)
add_definitions(-DGPU_STANDALONE)
set(CMAKE_CURRENT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../../../source/dune/gpu")
include(${CMAKE_SOURCE_DIR}/../../../source/dune/gpu/CMakeLists.txt)
suffix_relpaths(INC_NEW "${INC}" "../../../source/dune/gpu/")
suffix_relpaths(SRC_NEW "${SRC}" "../../../source/dune/gpu/")
include_directories(${INC_NEW})
add_library(gpu_lib ${SRC_NEW})
set(CMAKE_CURRENT_SOURCE_DIR "${_CMAKE_CURRENT_SOURCE_DIR_INIT}")

# blenfont
include(${CMAKE_SOURCE_DIR}/../../../source/blender/blenfont/CMakeLists.txt)
suffix_relpaths(INC_NEW "${INC}" "../../../source/blender/blenfont/")
suffix_relpaths(SRC_NEW "${SRC}" "../../../source/blender/blenfont/")
include_directories(${INC_NEW})
add_library(blenfont_lib ${SRC_NEW})

# wcwidth
include(${CMAKE_SOURCE_DIR}/../../../extern/wcwidth/CMakeLists.txt)
suffix_relpaths(INC_NEW "${INC}" "../../../extern/wcwidth/")
suffix_relpaths(SRC_NEW "${SRC}" "../../../extern/wcwidth/")
include_directories(${INC_NEW})
add_library(wcwidth_lib ${SRC_NEW})

# grr, blenfont needs BLI
include_directories(
  "../../../source/dune/lib"
)

# Needed for math_matrix.c to avoid eigen_capi.h dep.
add_definitions(-DMATH_STANDALONE)

add_library(dune_lib
  "../../../source/dune/lib/intern/fileops.c"
  "../../../source/dune/lib/intern/gsqueue.c"
  "../../../source/dune/lib/intern/rct.c"
  "../../../source/dune/lib/intern/string.c"
  "../../../source/dune/lib/intern/string_utf8.c"
  "../../../source/dune/lib/intern/listbase.c"
  "../../../source/dune/lib/intern/math_color.c"
  "../../../source/dune/lib/intern/math_geom.c"
  "../../../source/dune/lib/intern/math_matrix.c"
  "../../../source/dune/lib/intern/math_rotation.c"
  "../../../source/dune/lib/intern/math_vector.c"
  "../../../source/dune/lib/intern/storage.c"
  "../../../source/dune/lib/intern/task.c"
  "../../../source/dune/lib/intern/threads.c"
  "../../../source/dune/lib/intern/time.c"
  "../../../source/dune/lib/intern/path_util.c"
  "../../../source/dune/lib/intern/BLI_dynstr.c"
  "../../../source/dune/lib/intern/BLI_ghash.c"
  "../../../source/dune/lib/intern/BLI_ghash_utils.c"
  "../../../source/dune/lib/intern/BLI_linklist.c"
  "../../../source/dune/lib/intern/BLI_memarena.c"
  "../../../source/dune/lib/intern/BLI_mempool.c"
  "../../../source/dune/lib/intern/hash_mm2a.c"
  "../../../source/dune/lib/intern/string_utils.c"
  "../../../source/dune/lib/intern/system.c"
)

set(PLATFORM_CGLAGS)

find_package(OpenGL REQUIRED)

find_package(Freetype REQUIRED)

find_package(ZLIB REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/../)
include_directories(${OPENGL_INCLUDE_DIR})
include_directories(${FREETYPE_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/../../../source/blender/blenfont)

if(CMAKE_COMPILER_IS_GNUCC)
  set(PLATFORM_CFLAGS "-funsigned-char")
endif()

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)

  set(PLATFORM_LINKLIBS
    ${X11_X11_LIB}
    ${X11_Xinput_LIB}
    -lpthread
  )
endif()

string(APPEND CMAKE_C_FLAGS " ${PLATFORM_CFLAGS}")
string(APPEND CMAKE_CXX_FLAGS " ${PLATFORM_CFLAGS}")

# -----------------------------------------------------------------------------
# Executables

# Gears (C)
add_executable(gears_c
    ${CMAKE_SOURCE_DIR}/gears/GHOST_C-Test.c)

target_link_libraries(gears_c
    ghost_lib
    string_lib
    ${OPENGL_gl_LIBRARY}
    ${CMAKE_DL_LIBS}
    ${PLATFORM_LINKLIBS}
    )


# Gears (C++)
add_executable(gears_cpp
    ${CMAKE_SOURCE_DIR}/gears/GHOST_Test.cpp)

target_link_libraries(gears_cpp
    ghost_lib
    string_lib
    ${OPENGL_gl_LIBRARY}
    ${CMAKE_DL_LIBS}
    ${PLATFORM_LINKLIBS}
    )


# MultiTest (C)
add_executable(multitest_c
  ${CMAKE_SOURCE_DIR}/multitest/Basic.c
  ${CMAKE_SOURCE_DIR}/multitest/EventToBuf.c
  ${CMAKE_SOURCE_DIR}/multitest/MultiTest.c
  ${CMAKE_SOURCE_DIR}/multitest/ScrollBar.c
  ${CMAKE_SOURCE_DIR}/multitest/Util.c
  ${CMAKE_SOURCE_DIR}/multitest/WindowData.c
  ${CMAKE_SOURCE_DIR}/multitest/stubs.c
)


target_link_libraries(multitest_c
  dunefont_lib
  lib_lib
  gpu_lib
  # imbuf_lib
  ghost_lib
  lib_lib  # again...
  string_lib
  numaapi_lib
  guardedalloc_lib
  wcwidth_lib
  ${OPENGL_gl_LIBRARY}
  ${FREETYPE_LIBRARIES} ${BROTLI_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${CMAKE_DL_LIBS}
  ${PLATFORM_LINKLIBS}
)
