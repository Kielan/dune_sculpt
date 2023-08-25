  # EGL Runtime Components
  if(WITH_GL_EGL)
    if(WIN32)
      install(FILES "${OPENGLES_DLL}"     DESTINATION ".")
      install(FILES "${OPENGLES_EGL_DLL}" DESTINATION ".")

      if(WITH_GL_ANGLE)
        install(FILES "${D3DCOMPILER_DLL}" DESTINATION ".")
      endif()
    endif()
  endif()

  if(WITH_CODEC_FFMPEG)
    install(
      FILES
        ${LIBDIR}/ffmpeg/lib/avcodec-58.dll
        ${LIBDIR}/ffmpeg/lib/avformat-58.dll
        ${LIBDIR}/ffmpeg/lib/avdevice-58.dll
        ${LIBDIR}/ffmpeg/lib/avutil-56.dll
        ${LIBDIR}/ffmpeg/lib/swscale-5.dll
        ${LIBDIR}/ffmpeg/lib/swresample-3.dll
      DESTINATION "."
    )
  endif()
  if(WITH_TBB)
    install(
      FILES
        ${LIBDIR}/tbb/bin/tbb.dll
      DESTINATION "."
      CONFIGURATIONS Release;RelWithDebInfo;MinSizeRel
    )
    install(
      FILES
        ${LIBDIR}/tbb/bin/tbb_debug.dll
      DESTINATION "."
      CONFIGURATIONS Debug
    )
  endif()
  if(WITH_TBB_MALLOC_PROXY)
    install(
      FILES
        ${LIBDIR}/tbb/bin/tbbmalloc.dll
        ${LIBDIR}/tbb/bin/tbbmalloc_proxy.dll
      DESTINATION "."
      CONFIGURATIONS Release;RelWithDebInfo;MinSizeRel
    )
    install(
      FILES
        ${LIBDIR}/tbb/bin/tbbmalloc_debug.dll
        ${LIBDIR}/tbb/bin/tbbmalloc_proxy_debug.dll
      DESTINATION "."
      CONFIGURATIONS Debug
    )
    list(APPEND LIB ${TBB_MALLOC_LIBS})
  endif()

  if(WITH_CODEC_SNDFILE)
    install(
      FILES ${LIBDIR}/sndfile/lib/libsndfile-1.dll
      DESTINATION "."
    )
  endif()

  if(WITH_OPENAL)
    install(
      FILES
        ${LIBDIR}/openal/lib/OpenAL32.dll
      DESTINATION "."
    )
  endif()

  if(WITH_SDL)
    install(
      FILES ${LIBDIR}/sdl/lib/SDL2.dll
      DESTINATION "."
    )
  endif()

  if(WITH_SYSTEM_AUDASPACE)
    install(
      FILES
        ${LIBDIR}/audaspace/lib/audaspace.dll
        ${LIBDIR}/audaspace/lib/audaspace-c.dll
        ${LIBDIR}/audaspace/lib/audaspace-py.dll
      DESTINATION "."
    )
  endif()

  install(
    FILES
      ${CMAKE_SOURCE_DIR}/release/windows/batch/dune_debug_gpu.cmd
      ${CMAKE_SOURCE_DIR}/release/windows/batch/dune_debug_gpu_glitchworkaround.cmd
      ${CMAKE_SOURCE_DIR}/release/windows/batch/dune_debug_log.cmd
      ${CMAKE_SOURCE_DIR}/release/windows/batch/dune_factory_startup.cmd
      ${CMAKE_SOURCE_DIR}/release/windows/batch/dune_oculus.cmd
      ${CMAKE_SOURCE_DIR}/release/windows/batch/oculus.json
    DESTINATION "."
  )

  if(WITH_DUNE_THUMBNAILER)
    install(
      TARGETS DuneThumb
      DESTINATION "."
    )
  endif()
elseif(APPLE)
  if(NOT WITH_PYTHON_MODULE)
    # Uppercase name for app bundle
    set_target_properties(dune PROPS OUTPUT_NAME Dune)
  

  set(OSX_APP_SOURCEDIR ${CMAKE_SOURCE_DIR}/release/darwin/Dune.app)

  # setup Info.plist
  execute_process(COMMAND date "+%Y-%m-%d"
                  OUTPUT_VARIABLE DUNE_DATE
                  OUTPUT_STRIP_TRAILING_WHITESPACE)

  set_target_properties(dune PROPS
    MACOSX_BUNDLE_INFO_PLIST ${OSX_APP_SOURCEDIR}/Contents/Info.plist
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${DUNE_VERSION}.${DUNE_VERSION_PATCH}"
    MACOSX_BUNDLE_LONG_VERSION_STRING "${DUNE_VERSION}.${DUNE_VERSION_PATCH} ${DUNE_DATE}")

  # Gather the date in finder-style
  execute_process(COMMAND date "+%m/%d/%Y/%H:%M"
  OUTPUT_VARIABLE SETFILE_DATE
  OUTPUT_STRIP_TRAILING_WHITESPACE)

  # Give the bundle actual creation/modification date
  #
  # Note that the directory might not yet exist, which happens when CMake is first run.
  if(NOT EXISTS ${EXECUTABLE_OUTPUT_PATH}/Dune.app)
    file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH}/Dune.app)
  endif()
  execute_process(COMMAND SetFile -d ${SETFILE_DATE} -m ${SETFILE_DATE}
                  ${EXECUTABLE_OUTPUT_PATH}/Dune.app)

  install(
    TARGETS dune
    DESTINATION "."
  )

  install(
    FILES ${OSX_APP_SOURCEDIR}/Contents/PkgInfo
    DESTINATION Dune.app/Contents
  )

  install_dir(
    ${OSX_APP_SOURCEDIR}/Contents/Resources
    Dune.app/Contents/
  )

  if(WITH_DUNE_THUMBNAILER)
      install(
        TARGETS dune-thumbnailer
        DESTINATION Dune.app/Contents/MacOS/
      )
  endif()

  if(WITH_OPENMP AND OPENMP_CUSTOM)
    install(
      FILES "${OpenMP_LIB}"
      DESTINATION "${MAC_DUNE_TARGET_DYLIBS_DIR}"
    )
  endif()

  if(WITH_COMPILER_ASAN)
    install(
      FILES "${COMPILER_ASAN_LIBRARY}"
      DESTINATION "${MAC_DUNE_TARGET_DYLIBS_DIR}"
    )
  endif()
endif()

# Generic Install, for all targets
if(DEFINED DUNE_TEXT_FILES_DESTINATION)

  configure_file(${CMAKE_SOURCE_DIR}/release/text/readme.html
                 ${CMAKE_BINARY_DIR}/release/text/readme.html
                 @ONLY
  )
  list(APPEND DUNE_TEXT_FILES
    ${CMAKE_BINARY_DIR}/release/text/readme.html
  )

  install(
    FILES ${DUNE_TEXT_FILES}
    DESTINATION "${DUNE_TEXT_FILES_DESTINATION}"
  )

  install(
    DIRECTORY
    ${CMAKE_SOURCE_DIR}/release/license
    DESTINATION "${DUNE_TEXT_FILES_DESTINATION}"
)
endif()

# install more files specified elsewhere
delayed_do_install(${TARGETDIR_VER})

unset(DUNE_TEXT_FILES)
unset(DUNE_TEXT_FILES_DESTINATION)

# Geometry icons.
get_property(_icon_names GLOBAL PROP ICON_GEOM_NAMES)
set(_icon_files)
foreach(_f ${_icon_names})
  list(APPEND _icon_files
    "${CMAKE_SOURCE_DIR}/release/datafiles/icons/${_f}.dat"
  )
endforeach()
install(
  FILES ${_icon_files}
  DESTINATION ${TARGETDIR_VER}/datafiles/icons
)

unset(_icon_names)
unset(_icon_files)
unset(_f)

# Studio Lights
install(
  DIRECTORY
    ${CMAKE_SOURCE_DIR}/release/datafiles/studiolights
  DESTINATION ${TARGETDIR_VER}/datafiles
)

# Setup link libs
add_dependencies(dune types)
target_link_libraries(dune ${LIB})
unset(LIB)

setup_platform_linker_flags(dune)
setup_platform_linker_libs(dune)

if(APPLE)
  set_target_properties(dune PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/osx_locals.map)
elseif(UNIX)
  set_target_properties(dune PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dune.map)
endif()

# USD registry.
# USD requires a set of JSON files that define the standard schemas. These
# files are required at runtime.
if(WITH_USD)
  add_definitions(-DWITH_USD)
  install(DIRECTORY
    ${USD_LIBRARY_DIR}/usd
    DESTINATION "${TARGETDIR_VER}/datafiles"
  )
endif()


# vcpkg substitutes our libs with theirs, which will cause issues when you
# you run these builds on other systems due to missing dlls. So we opt out
# the use of vcpkg
if(WIN32)
  set_target_properties(dune PROPERTIES VS_GLOBAL_VcpkgEnabled "false")
  set_target_properties(dune PROPERTIES
    PDB_NAME "dune_private"
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    if(WITH_WINDOWS_PDB AND WITH_WINDOWS_STRIPPED_PDB)
      # This is slightly messy, but single target generators like ninja will not have the
      # CMAKE_CFG_INTDIR variable and multitarget generators like msbuild will not have
      # CMAKE_BUILD_TYPE. This can be simplified by target_link_options and the $<CONFIG>
      # generator expression in newer cmake (2.13+) but until that time this fill have suffice.
      if(CMAKE_BUILD_TYPE)
        set_property(TARGET dune APPEND_STRING PROPERTY LINK_FLAGS  " /PDBSTRIPPED:${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/dune_public.pdb")
      else()
        set_property(TARGET dune APPEND_STRING PROPERTY LINK_FLAGS  " /PDBSTRIPPED:${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/dune_public.pdb")
      endif()
  endif()
endif()

# Setup launcher

if(WIN32 AND NOT WITH_PYTHON_MODULE)
  install(
    TARGETS dune dune-launcher
    COMPONENT Dune
    DESTINATION "."
  )
  set_target_properties(
    dune
    PROPERTIES
      VS_USER_PROPS "dune.Cpp.user.props"
  )
endif()

# -----------------------------------------------------------------------------
# Post-install script

if(POSTINSTALL_SCRIPT)
  install(SCRIPT ${POSTINSTALL_SCRIPT})
endif()
