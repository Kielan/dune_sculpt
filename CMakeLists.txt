cmake_minimum_required(VERSION 3.10)

#-----------------------------------------------------------------------------
# Set policy

# see "cmake --help-policy CMP0003"
# So library linking is more sane
cmake_policy(SET CMP0003 NEW)

# So BUILDINFO and DUNEPATH strings are automatically quoted
cmake_policy(SET CMP0005 NEW)

# So syntax problems are errors
cmake_policy(SET CMP0010 NEW)

# Input directories must have CMakeLists.txt
cmake_policy(SET CMP0014 NEW)

# Silence draco warning on macOS, new policy works fine.
if(POLICY CMP0068)
  cmake_policy(SET CMP0068 NEW)
endif()

# find_package() uses <PackageName>_ROOT variables.
if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

# Install CODE|SCRIPT allow the use of generator expressions.
if(POLICY CMP0087)
  cmake_policy(SET CMP0087 NEW)
endif()

#-----------------------------------------------------------------------------
# Load some macros.
include(build_files/cmake/macros.cmake)

#-----------------------------------------------------------------------------
# Initialize project.

project(Dune)

enable_testing()

#-----------------------------------------------------------------------------
# Redirect output files

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin CACHE INTERNAL "" FORCE)
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib CACHE INTERNAL "" FORCE)

get_property(GENERATOR_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(GENERATOR_IS_MULTI_CONFIG)
  set(TESTS_OUTPUT_DIR ${EXECUTABLE_OUTPUT_PATH}/tests/$<CONFIG>/ CACHE INTERNAL "" FORCE)
else()
  set(TESTS_OUTPUT_DIR ${EXECUTABLE_OUTPUT_PATH}/tests/ CACHE INTERNAL "" FORCE)
endif()

#-----------------------------------------------------------------------------
# Set default config options

get_dune_version()


#-----------------------------------------------------------------------------
# Options

# Dune internal features
option(WITH_DUNE "Build dune (disable to build only the dune player)" ON)
mark_as_advanced(WITH_DUNE)

if(APPLE)
  # In future, can be used with `quicklookthumbnailing/qlthumbnailreply` to create file
  # thumbnails for say Finder. Turn it off for now.
  option(WITH_DUNE_THUMBNAILER "Build \"dune-thumbnailer\" thumbnail extraction utility" OFF)
elseif(WIN32)
  option(WITH_DUNE_THUMBNAILER "Build \"DuneThumb.dll\" helper for Windows explorer integration" ON)
else()
  option(WITH_DUNE_THUMBNAILER "Build \"dune-thumbnailer\" thumbnail extraction utility" ON)
endif()

option(WITH_INTERNATIONAL "Enable I18N (International fonts and text)" ON)

option(WITH_BUILDINFO     "Include extra build details (only disable for development & faster builds)" ON)
set(BUILDINFO_OVERRIDE_DATE "" CACHE STRING "Use instead of the current date for reproducible builds (empty string disables this option)")
set(BUILDINFO_OVERRIDE_TIME "" CACHE STRING "Use instead of the current time for reproducible builds (empty string disables this option)")
set(CPACK_OVERRIDE_PACKAGENAME "" CACHE STRING "Use instead of the standard packagename (empty string disables this option)")
mark_as_advanced(CPACK_OVERRIDE_PACKAGENAME)
mark_as_advanced(BUILDINFO_OVERRIDE_DATE)
mark_as_advanced(BUILDINFO_OVERRIDE_TIME)

option(WITH_IK_ITASC      "Enable ITASC IK solver (only disable for development & for incompatible C++ compilers)" ON)
option(WITH_IK_SOLVER     "Enable Legacy IK solver (only disable for development)" ON)
option(WITH_FFTW3         "Enable FFTW3 support (Used for smoke, ocean sim, and audio effects)" ON)
option(WITH_PUGIXML       "Enable PugiXML support (Used for OpenImageIO, Grease Pencil SVG export)" ON)
option(WITH_BULLET        "Enable Bullet (Physics Engine)" ON)
option(WITH_SYSTEM_BULLET "Use the systems bullet library (currently unsupported due to missing features in upstream!)" )
mark_as_advanced(WITH_SYSTEM_BULLET)
option(WITH_OPENCOLORIO   "Enable OpenColorIO color management" ON)
if(APPLE)
  # There's no OpenXR runtime in sight for macOS, neither is code well
  # tested there -> disable it by default.
  option(WITH_XR_OPENXR   "Enable VR features through the OpenXR specification" OFF)
  mark_as_advanced(WITH_XR_OPENXR)
else()
  option(WITH_XR_OPENXR   "Enable VR features through the OpenXR specification" ON)
endif()
option(WITH_GMP "Enable features depending on GMP (Exact Boolean)" ON)

# Compositor
option(WITH_COMPOSITOR         "Enable the tile based nodal compositor" ON)
option(WITH_OPENIMAGEDENOISE   "Enable the OpenImageDenoise compositing node" ON)

option(WITH_OPENSUBDIV    "Enable OpenSubdiv for surface subdivision" ON)

option(WITH_POTRACE       "Enable features relying on potrace" ON)
option(WITH_OPENVDB       "Enable features relying on OpenVDB" ON)
option(WITH_OPENVDB_BLOSC "Enable blosc compression for OpenVDB, only enable if OpenVDB was built with blosc support" ON)
option(WITH_OPENVDB_3_ABI_COMPATIBLE "Assume OpenVDB library has been compiled with version 3 ABI compatibility" OFF)
mark_as_advanced(WITH_OPENVDB_3_ABI_COMPATIBLE)
option(WITH_NANOVDB       "Enable usage of NanoVDB data structure for rendering on the GPU" ON)
option(WITH_HARU          "Enable features relying on Libharu (Grease pencil PDF export)" ON)

# GHOST Windowing Library Options
option(WITH_GHOST_DEBUG   "Enable debugging output for the GHOST library" OFF)
mark_as_advanced(WITH_GHOST_DEBUG)

option(WITH_GHOST_SDL    "Enable building Dune against SDL for windowing rather than the native APIs" OFF)
mark_as_advanced(WITH_GHOST_SDL)

if(UNIX AND NOT (APPLE OR HAIKU))
  option(WITH_GHOST_X11 "Enable building Dune against X11 for windowing" ON)
  mark_as_advanced(WITH_GHOST_X11)

  option(WITH_GHOST_WAYLAND "Enable building Dune against Wayland for windowing (under development)" OFF)
  mark_as_advanced(WITH_GHOST_WAYLAND)
endif()

if(WITH_GHOST_X11)
  option(WITH_GHOST_XDND    "Enable drag'n'drop support on X11 using XDND protocol" ON)
endif()

# Misc...
option(WITH_HEADLESS      "Build without graphical support (renderfarm, server mode only)" OFF)
mark_as_advanced(WITH_HEADLESS)

option(WITH_QUADRIFLOW    "Build with quadriflow remesher support" ON)

option(WITH_AUDASPACE    "Build with dunes audio library (only disable if you know what you're doing!)" ON)
option(WITH_SYSTEM_AUDASPACE "Build with external audaspace library installed on the system (only enable if you know what you're doing!)" OFF)
mark_as_advanced(WITH_AUDASPACE)
mark_as_advanced(WITH_SYSTEM_AUDASPACE)

set_and_warn_dependency(WITH_AUDASPACE WITH_SYSTEM_AUDASPACE OFF)

option(WITH_OPENMP        "Enable OpenMP (has to be supported by the compiler)" ON)
if(UNIX AND NOT APPLE)
  option(WITH_OPENMP_STATIC "Link OpenMP statically (only used by the release environment)" OFF)
  mark_as_advanced(WITH_OPENMP_STATIC)
endif()

if(WITH_GHOST_X11)
  option(WITH_X11_XINPUT    "Enable X11 Xinput (tablet support and unicode input)"  ON)
  option(WITH_X11_XF86VMODE "Enable X11 video mode switching"                       ON)
  option(WITH_X11_XFIXES    "Enable X11 XWayland cursor warping workaround"         ON)
  option(WITH_X11_ALPHA     "Enable X11 transparent background"                     ON)
endif()

if(UNIX AND NOT APPLE)
  option(WITH_SYSTEM_GLEW "Use GLEW OpenGL wrapper library provided by the operating system" OFF)
  option(WITH_SYSTEM_GLES "Use OpenGL ES library provided by the operating system"           ON)
  option(WITH_SYSTEM_FREETYPE "Use the freetype library provided by the operating system" OFF)
else()
  # not an option for other OS's
  set(WITH_SYSTEM_GLEW OFF)
  set(WITH_SYSTEM_GLES OFF)
  set(WITH_SYSTEM_FREETYPE OFF)
endif()


if(UNIX AND NOT APPLE)
  option(WITH_SYSTEM_EIGEN3 "Use the systems Eigen3 library" OFF)
endif()


# Modifiers
option(WITH_MOD_FLUID           "Enable Mantaflow Fluid Simulation Framework" ON)
option(WITH_MOD_REMESH          "Enable Remesh Modifier" ON)
option(WITH_MOD_OCEANSIM        "Enable Ocean Modifier" ON)

# Image format support
option(WITH_OPENIMAGEIO         "Enable OpenImageIO Support (http://www.openimageio.org)" ON)
option(WITH_IMAGE_OPENEXR       "Enable OpenEXR Support (http://www.openexr.com)" ON)
option(WITH_IMAGE_OPENJPEG      "Enable OpenJpeg Support (http://www.openjpeg.org)" ON)
option(WITH_IMAGE_TIFF          "Enable LibTIFF Support" ON)
option(WITH_IMAGE_DDS           "Enable DDS Image Support" ON)
option(WITH_IMAGE_CINEON        "Enable CINEON and DPX Image Support" ON)
option(WITH_IMAGE_HDR           "Enable HDR Image Support" ON)

# Audio/Video format support
option(WITH_CODEC_AVI           "Enable Dunes own AVI file support (raw/jpeg)" ON)
option(WITH_CODEC_FFMPEG        "Enable FFMPeg Support (http://ffmpeg.org)" ON)
option(WITH_CODEC_SNDFILE       "Enable libsndfile Support (http://www.mega-nerd.com/libsndfile)" ON)

# Alembic support
option(WITH_ALEMBIC             "Enable Alembic Support" ON)

# Universal Scene Description support
option(WITH_USD                 "Enable Universal Scene Description (USD) Support" ON)

# 3D format support
# Disable opencollada when we don't have precompiled libs
option(WITH_OPENCOLLADA   "Enable OpenCollada Support (http://www.opencollada.org)" ON)

# Sound output
option(WITH_SDL           "Enable SDL for sound and joystick support" ON)
option(WITH_OPENAL        "Enable OpenAL Support (http://www.openal.org)" ON)
if(APPLE)
  option(WITH_COREAUDIO    "Enable CoreAudio for audio support on macOS" ON)
else()
  set(WITH_COREAUDIO OFF)
endif()
if(NOT WIN32)
  option(WITH_JACK          "Enable JACK Support (http://www.jackaudio.org)" ON)
  if(UNIX AND NOT APPLE)
    option(WITH_JACK_DYNLOAD  "Enable runtime dynamic JACK libraries loading" OFF)
  endif()
else()
  set(WITH_JACK OFF)
endif()
if(UNIX AND NOT APPLE)
  option(WITH_SDL_DYNLOAD  "Enable runtime dynamic SDL libraries loading" OFF)
endif()
if(UNIX AND NOT APPLE)
  option(WITH_PULSEAUDIO   "Enable PulseAudio for audio support on Linux" ON)
  option(WITH_PULSEAUDIO_DYNLOAD  "Enable runtime dynamic PulseAudio libraries loading" OFF)
else()
  set(WITH_PULSEAUDIO OFF)
endif()
if(WIN32)
  option(WITH_WASAPI      "Enable Windows Audio Sessions API for audio support on Windows" ON)
else()
  set(WITH_WASAPI OFF)
endif()

# Compression
option(WITH_LZO           "Enable fast LZO compression (used for pointcache)" ON)
option(WITH_LZMA          "Enable best LZMA compression, (used for pointcache)" ON)
if(UNIX AND NOT APPLE)
  option(WITH_SYSTEM_LZO    "Use the system LZO library" OFF)
endif()
option(WITH_DRACO         "Enable Draco mesh compression Python module (used for glTF)" ON)

# Camera/motion tracking
option(WITH_LIBMV         "Enable Libmv structure from motion library" ON)
option(WITH_LIBMV_SCHUR_SPECIALIZATIONS "Enable fixed-size schur specializations." ON)
mark_as_advanced(WITH_LIBMV_SCHUR_SPECIALIZATIONS)

# Logging/unbit test libraries.
option(WITH_SYSTEM_GFLAGS   "Use system-wide Gflags instead of a bundled one" OFF)
option(WITH_SYSTEM_GLOG     "Use system-wide Glog instead of a bundled one" OFF)
mark_as_advanced(WITH_SYSTEM_GFLAGS)
mark_as_advanced(WITH_SYSTEM_GLOG)

# Freestyle
option(WITH_FREESTYLE     "Enable Freestyle (advanced edges rendering)" ON)

# Misc
if(WIN32 OR APPLE)
  option(WITH_INPUT_IME "Enable Input Method Editor (IME) for complex Asian character input" ON)
endif()
option(WITH_INPUT_NDOF "Enable NDOF input devices (SpaceNavigator and friends)" ON)
if(UNIX AND NOT APPLE)
  option(WITH_INSTALL_PORTABLE "Install redistributeable runtime, otherwise install into CMAKE_INSTALL_PREFIX" ON)
  option(WITH_STATIC_LIBS "Try to link with static libraries, as much as possible, to make blender more portable across distributions" OFF)
  if(WITH_STATIC_LIBS)
    option(WITH_BOOST_ICU "Boost uses ICU library (required for linking with static Boost built with libicu)." OFF)
    mark_as_advanced(WITH_BOOST_ICU)
  endif()
endif()

option(WITH_CPU_SIMD              "Enable SIMD instruction if they're detected on the host machine" ON)
mark_as_advanced(WITH_CPU_SIMD)

# Draw Manager
option(WITH_DRAW_DEBUG "Add extra debug capabilities to Draw Manager" OFF)
mark_as_advanced(WITH_DRAW_DEBUG)

# LLVM
option(WITH_LLVM                    "Use LLVM" OFF)
if(APPLE)
  option(LLVM_STATIC                  "Link with LLVM static libraries" ON) # we prefer static llvm build on Apple, dyn build possible though
else()
  option(LLVM_STATIC                  "Link with LLVM static libraries" OFF)
endif()
mark_as_advanced(LLVM_STATIC)
option(WITH_CLANG                   "Use Clang" OFF)

# disable for now, but plan to support on all platforms eventually
option(WITH_MEM_JEMALLOC   "Enable malloc replacement (http://www.canonware.com/jemalloc)" ON)
mark_as_advanced(WITH_MEM_JEMALLOC)

# currently only used for BLI_mempool
option(WITH_MEM_VALGRIND "Enable extended valgrind support for better reporting" OFF)
mark_as_advanced(WITH_MEM_VALGRIND)

# Debug
option(WITH_ASSERT_ABORT "Call abort() when raising an assertion through BLI_assert()" ON)
mark_as_advanced(WITH_ASSERT_ABORT)

if((UNIX AND NOT APPLE) OR (CMAKE_GENERATOR MATCHES "^Visual Studio.+"))
  option(WITH_CLANG_TIDY "Use Clang Tidy to analyze the source code (only enable for development on Linux using Clang, or Windows using the Visual Studio IDE)" OFF)
  mark_as_advanced(WITH_CLANG_TIDY)
endif()

option(WITH_BOOST "Enable features depending on boost" ON)
option(WITH_TBB   "Enable features depending on TBB (OpenVDB, OpenImageDenoise, sculpt multithreading)" ON)

# TBB malloc is only supported on for windows currently
if(WIN32)
  option(WITH_TBB_MALLOC_PROXY "Enable the TBB malloc replacement" ON)
endif()

# Unit testing
option(WITH_GTESTS "Enable GTest unit testing" OFF)
option(WITH_OPENGL_RENDER_TESTS "Enable OpenGL render related unit testing (Experimental)" OFF)
option(WITH_OPENGL_DRAW_TESTS "Enable OpenGL UI drawing related unit testing (Experimental)" OFF)

# OpenGL

option(WITH_OPENGL              "When off limits visibility of the opengl headers to just bf_gpu and gawain (temporary option for development purposes)" ON)
option(WITH_GLEW_ES             "Switches to experimental copy of GLEW that has support for OpenGL ES. (temporary option for development purposes)" OFF)
option(WITH_GL_EGL              "Use the EGL OpenGL system library instead of the platform specific OpenGL system library (CGL, glX, or WGL)"       OFF)
option(WITH_GL_PROFILE_ES20     "Support using OpenGL ES 2.0. (through either EGL or the AGL/WGL/XGL 'es20' profile)"                               OFF)
option(WITH_GPU_SHADER_BUILDER  "Shader builder is a developer option enabling linting on GLSL during compilation"                                  OFF)

mark_as_advanced(
  WITH_OPENGL
  WITH_GLEW_ES
  WITH_GL_EGL
  WITH_GL_PROFILE_ES20
  WITH_GPU_SHADER_BUILDER
)

# Metal

if (APPLE)
  option(WITH_METAL_BACKEND "Use Metal for graphics instead of (or as well as) OpenGL on macOS." OFF)
  mark_as_advanced(WITH_METAL_BACKEND)
else()
  set(WITH_METAL_BACKEND OFF)
endif()

if (WITH_METAL_BACKEND)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version" FORCE)
endif()

if(WIN32)
  option(WITH_GL_ANGLE "Link with the ANGLE library, an OpenGL ES 2.0 implementation based on Direct3D, instead of the system OpenGL library." OFF)
  mark_as_advanced(WITH_GL_ANGLE)
endif()

if(WITH_GLEW_ES AND WITH_SYSTEM_GLEW)
  message(WARNING Ignoring WITH_SYSTEM_GLEW and using WITH_GLEW_ES)
  set(WITH_SYSTEM_GLEW OFF)
endif()

if(WIN32)
  getDefaultWindowsPrefixBase(CMAKE_GENERIC_PROGRAM_FILES)
  set(CPACK_INSTALL_PREFIX ${CMAKE_GENERIC_PROGRAM_FILES}/${})
endif()

# Compiler tool-chain.
if(UNIX AND NOT APPLE)
  if(CMAKE_COMPILER_IS_GNUCC)
    option(WITH_LINKER_GOLD "Use ld.gold linker which is usually faster than ld.bfd" ON)
    mark_as_advanced(WITH_LINKER_GOLD)
    option(WITH_LINKER_LLD "Use ld.lld linker which is usually faster than ld.gold" OFF)
    mark_as_advanced(WITH_LINKER_LLD)
  endif()
  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    option(WITH_LINKER_MOLD "Use ld.mold linker which is usually faster than ld.gold & ld.lld." OFF)
    mark_as_advanced(WITH_LINKER_MOLD)
  endif()
endif()

option(WITH_COMPILER_ASAN "Build and link against address sanitizer (only for Debug & RelWithDebInfo targets)." OFF)
mark_as_advanced(WITH_COMPILER_ASAN)

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
  if(WITH_COMPILER_ASAN)
    set(_asan_defaults "\
-fsanitize=address \
-fsanitize=bool \
-fsanitize=bounds \
-fsanitize=enum \
-fsanitize=float-cast-overflow \
-fsanitize=float-divide-by-zero \
-fsanitize=nonnull-attribute \
-fsanitize=returns-nonnull-attribute \
-fsanitize=signed-integer-overflow \
-fsanitize=undefined \
-fsanitize=vla-bound \
-fno-sanitize=alignment \
")

    if(MSVC)
      # clang-cl doesn't support all sanitizers, but leak and object-size give errors/warnings.
      set(_asan_defaults "${_asan_defaults}")
    elseif(APPLE)
      # AppleClang doesn't support all sanitizers, but leak gives error.
      # Build type is not known for multi-config generator, so don't add object-size sanitizer.
      if(CMAKE_BUILD_TYPE MATCHES "Debug" OR GENERATOR_IS_MULTI_CONFIG)
        # Silence the warning that object-size is not effective in -O0.
        set(_asan_defaults "${_asan_defaults}")
      else()
        string(APPEND _asan_defaults " -fsanitize=object-size")
      endif()
    else()
      string(APPEND _asan_defaults " -fsanitize=leak -fsanitize=object-size")
    endif()

    set(COMPILER_ASAN_CFLAGS "${_asan_defaults}" CACHE STRING "C flags for address sanitizer")
    mark_as_advanced(COMPILER_ASAN_CFLAGS)

    unset(_asan_defaults)

    if(MSVC)
        find_library(
          COMPILER_ASAN_LIBRARY NAMES clang_rt.asan-x86_64
        PATHS
        [HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\LLVM\\LLVM;]/lib/clang/7.0.0/lib/windows
        [HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\LLVM\\LLVM;]/lib/clang/6.0.0/lib/windows
      )
    elseif(APPLE)
      execute_process(COMMAND ${CMAKE_CXX_COMPILER}
        -print-file-name=lib
        OUTPUT_VARIABLE CLANG_LIB_DIR
      )
      string(STRIP "${CLANG_LIB_DIR}" CLANG_LIB_DIR)
      find_library(
        COMPILER_ASAN_LIBRARY
        NAMES
          libclang_rt.asan_osx_dynamic.dylib
        PATHS
          "${CLANG_LIB_DIR}/darwin/"
      )
      unset(CLANG_LIB_DIR)
    else()
      find_library(
        COMPILER_ASAN_LIBRARY asan ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES}
      )
    endif()

    mark_as_advanced(COMPILER_ASAN_LIBRARY)
  endif()
endif()

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
  option(WITH_COMPILER_SHORT_FILE_MACRO "Make paths in macros like __FILE__ relative to top level source and build directories." ON)
  mark_as_advanced(WITH_COMPILER_SHORT_FILE_MACRO)
endif()

if(WIN32)
  # Use hardcoded paths or find_package to find externals
  option(WITH_WINDOWS_FIND_MODULES "Use find_package to locate libraries" OFF)
  mark_as_advanced(WITH_WINDOWS_FIND_MODULES)

  option(WINDOWS_PYTHON_DEBUG "Include the files needed for debugging python scripts with visual studio 2017+." OFF)
  mark_as_advanced(WINDOWS_PYTHON_DEBUG)

  option(WITH_WINDOWS_BUNDLE_CRT "Bundle the C runtime for install free distribution." ON)
  mark_as_advanced(WITH_WINDOWS_BUNDLE_CRT)

  option(WITH_WINDOWS_SCCACHE "Use sccache to speed up builds (Ninja builder only)" OFF)
  mark_as_advanced(WITH_WINDOWS_SCCACHE)

  option(WITH_WINDOWS_PDB "Generate a pdb file for client side stacktraces" ON)
  mark_as_advanced(WITH_WINDOWS_PDB)

  option(WITH_WINDOWS_STRIPPED_PDB "Use a stripped PDB file" ON)
  mark_as_advanced(WITH_WINDOWS_STRIPPED_PDB)

endif()

if(WIN32 OR XCODE)
  option(IDE_GROUP_SOURCES_IN_FOLDERS "Organize the source files in filters matching the source folders." ON)
  mark_as_advanced(IDE_GROUP_SOURCES_IN_FOLDERS)

  option(IDE_GROUP_PROJECTS_IN_FOLDERS "Organize the projects according to source folder structure." ON)
  mark_as_advanced(IDE_GROUP_PROJECTS_IN_FOLDERS)

  if(IDE_GROUP_PROJECTS_IN_FOLDERS)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
  endif()
endif()

if(UNIX)
  # See WITH_WINDOWS_SCCACHE for Windows.
  option(WITH_COMPILER_CCACHE "Use ccache to improve rebuild times (Works with Ninja, Makefiles and Xcode)" OFF)
  mark_as_advanced(WITH_COMPILER_CCACHE)
endif()

# The following only works with the Ninja generator in CMake >= 3.0.
if("${CMAKE_GENERATOR}" MATCHES "Ninja")
  option(WITH_NINJA_POOL_JOBS
         "Enable Ninja pools of jobs, to try to ease building on machines with 16GB of RAM or less (if not yet defined, will try to set best values based on detected machine specifications)."
         OFF)
  mark_as_advanced(WITH_NINJA_POOL_JOBS)
endif()

# Installation process.
set(POSTINSTALL_SCRIPT "" CACHE FILEPATH "Run given CMake script after installation process")
mark_as_advanced(POSTINSTALL_SCRIPT)

set(POSTCONFIGURE_SCRIPT "" CACHE FILEPATH "Run given CMake script as the last step of CMake configuration")
mark_as_advanced(POSTCONFIGURE_SCRIPT)

# end option(...)



# By default we want to install to the directory we are compiling our executables
# unless specified otherwise, which we currently do not allow
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(WIN32)
    set(CMAKE_INSTALL_PREFIX ${EXECUTABLE_OUTPUT_PATH}/\${BUILD_TYPE} CACHE PATH "default install path" FORCE)
  elseif(APPLE)
    set(CMAKE_INSTALL_PREFIX ${EXECUTABLE_OUTPUT_PATH}/\${BUILD_TYPE} CACHE PATH "default install path" FORCE)
  else()
    if(WITH_INSTALL_PORTABLE)
      set(CMAKE_INSTALL_PREFIX ${EXECUTABLE_OUTPUT_PATH} CACHE PATH "default install path" FORCE)
    endif()
  endif()
endif()



# Apple

if(APPLE)
  include(platform_apple_xcode)
endif()


#-----------------------------------------------------------------------------
# Check for conflicting/unsupported configurations.

set_and_warn_dependency(WITH_AUDASPACE WITH_OPENAL OFF)
set_and_warn_dependency(WITH_AUDASPACE WITH_COREAUDIO OFF)
set_and_warn_dependency(WITH_AUDASPACE WITH_JACK OFF)
set_and_warn_dependency(WITH_AUDASPACE WITH_PULSEAUDIO OFF)
set_and_warn_dependency(WITH_AUDASPACE WITH_WASAPI OFF)

if(NOT WITH_SDL AND WITH_GHOST_SDL)
  message(FATAL_ERROR "WITH_GHOST_SDL requires WITH_SDL")
endif()

set_and_warn_dependency(WITH_BOOST WITH_INTERNATIONAL  OFF)
set_and_warn_dependency(WITH_BOOST WITH_OPENVDB        OFF)
set_and_warn_dependency(WITH_BOOST WITH_OPENCOLORIO    OFF)
set_and_warn_dependency(WITH_BOOST WITH_QUADRIFLOW     OFF)
set_and_warn_dependency(WITH_BOOST WITH_USD            OFF)
set_and_warn_dependency(WITH_BOOST WITH_ALEMBIC        OFF)
set_and_warn_dependency(WITH_PUGIXML WITH_CYCLES_OSL   OFF)
set_and_warn_dependency(WITH_PUGIXML WITH_OPENIMAGEIO  OFF)

set_and_warn_dependency(WITH_TBB WITH_USD               OFF)
set_and_warn_dependency(WITH_TBB WITH_OPENIMAGEDENOISE  OFF)
set_and_warn_dependency(WITH_TBB WITH_OPENVDB           OFF)
set_and_warn_dependency(WITH_TBB WITH_MOD_FLUID         OFF)

# NanoVDB requires OpenVDB to convert the data structure
set_and_warn_dependency(WITH_OPENVDB WITH_NANOVDB       OFF)

# OpenVDB and OpenColorIO uses 'half' type from OpenEXR
set_and_warn_dependency(WITH_IMAGE_OPENEXR WITH_OPENVDB OFF)
set_and_warn_dependency(WITH_IMAGE_OPENEXR WITH_OPENCOLORIO  OFF)

# Haru needs `TIFFFaxBlackCodes` & `TIFFFaxWhiteCodes` symbols from TIFF.
set_and_warn_dependency(WITH_IMAGE_TIFF WITH_HARU       OFF)

# auto enable openimageio linking dependencies
if(WITH_OPENIMAGEIO)
  set(WITH_IMAGE_OPENEXR ON)
  set(WITH_IMAGE_TIFF ON)
endif()

# auto enable alembic linking dependencies
if(WITH_ALEMBIC)
  set(WITH_IMAGE_OPENEXR ON)
endif()

# don't store paths to libs for portable distribution
if(WITH_INSTALL_PORTABLE)
  set(CMAKE_SKIP_BUILD_RPATH TRUE)
endif()

if(WITH_GHOST_SDL OR WITH_HEADLESS)
  message(STATUS "Disabling Ghost Wayland, X11, Input IME, and OpenXR")
  set(WITH_GHOST_WAYLAND OFF)
  set(WITH_GHOST_X11     OFF)
  set(WITH_X11_XINPUT    OFF)
  set(WITH_X11_XF86VMODE OFF)
  set(WITH_X11_XFIXES    OFF)
  set(WITH_X11_ALPHA     OFF)
  set(WITH_GHOST_XDND    OFF)
  set(WITH_INPUT_IME     OFF)
  set(WITH_XR_OPENXR     OFF)
endif()

if(WITH_BUILDINFO)
  find_package(Git)
  if(NOT GIT_FOUND)
    message(WARNING "Git was not found, disabling WITH_BUILDINFO")
    set(WITH_BUILDINFO OFF)
  endif()
endif()

if(WITH_AUDASPACE)
  if(NOT WITH_SYSTEM_AUDASPACE)
    set(AUDASPACE_C_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/extern/audaspace/bindings/C" "${CMAKE_BINARY_DIR}/extern/
  endif()
endif()

#-----------------------------------------------------------------------------
# Check if submodules are cloned.

if(WITH_INTERNATIONAL)
  file(GLOB RESULT "${CMAKE_SOURCE_DIR}/release/datafiles/locale")
  list(LENGTH RESULT DIR_LEN)
  if(DIR_LEN EQUAL 0)
    message(WARNING
      "Translation path '${CMAKE_SOURCE_DIR}/release/datafiles/locale' is missing, "
      "This is a 'git submodule', which are known not to work with bridges to other version "
      "control systems, disabling 'WITH_INTERNATIONAL'."
    )
    set(WITH_INTERNATIONAL OFF)
  endif()
endif()

#-----------------------------------------------------------------------------
# Initialize un-cached vars, avoid unused warning

# linux only, not cached
set(WITH_BINRELOC OFF)

# MACOSX only, set to avoid uninitialized
set(EXETYPE "")

# C/C++ flags
set(PLATFORM_CFLAGS)

# these are added to later on.
set(C_WARNINGS)

# for gcc -Wno-blah-blah
set(C_REMOVE_STRICT_FLAGS)

# Libraries to link to targets in setup_platform_linker_libs
set(PLATFORM_LINKLIBS "")

# Added to target linker flags in setup_platform_linker_flags
# - CMAKE_EXE_LINKER_FLAGS
# - CMAKE_EXE_LINKER_FLAGS_DEBUG
set(PLATFORM_LINKFLAGS "")
set(PLATFORM_LINKFLAGS_DEBUG "")

if(NOT CMAKE_BUILD_TYPE MATCHES "Release")
  if(WITH_COMPILER_ASAN)
    if(NOT APPLE)
      # Avoid passing address sanitizer compiler flags to `try_compile`.
      # Since linker flags are not set, all compiler checks and `find_package`
      # calls that rely on `try_compile` will fail.
      # See CMP0066 also.
      string(APPEND CMAKE_C_FLAGS_DEBUG " ${COMPILER_ASAN_CFLAGS}")
      string(APPEND CMAKE_C_FLAGS_RELWITHDEBINFO " ${COMPILER_ASAN_CFLAGS}")

      string(APPEND CMAKE_CXX_FLAGS_DEBUG " ${COMPILER_ASAN_CXXFLAGS}")
      string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " ${COMPILER_ASAN_CXXFLAGS}")
    endif()
    if(MSVC)
      set(COMPILER_ASAN_LINKER_FLAGS "/FUNCTIONPADMIN:6")
    endif()

    if(APPLE AND COMPILER_ASAN_LIBRARY)
      string(REPLACE " " ";" _list_COMPILER_ASAN_CFLAGS ${COMPILER_ASAN_CFLAGS})
      set(_is_CONFIG_DEBUG "$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>")
      add_compile_options("$<${_is_CONFIG_DEBUG}:${_list_COMPILER_ASAN_CFLAGS}>")
      add_link_options("$<${_is_CONFIG_DEBUG}:-fno-omit-frame-pointer;-fsanitize=address>")
      unset(_list_COMPILER_ASAN_CFLAGS)
      unset(_is_CONFIG_DEBUG)
    elseif(COMPILER_ASAN_LIBRARY)
      set(PLATFORM_LINKLIBS "${PLATFORM_LINKLIBS};${COMPILER_ASAN_LIBRARY}")
      set(PLATFORM_LINKFLAGS "${COMPILER_ASAN_LIBRARY} ${COMPILER_ASAN_LINKER_FLAGS}")
      set(PLATFORM_LINKFLAGS_DEBUG "${COMPILER_ASAN_LIBRARY} ${COMPILER_ASAN_LINKER_FLAGS}")
    endif()
  endif()
endif()

# Test SIMD support, before platform includes to determine if sse2neon is needed.
if(WITH_CPU_SIMD)
  set(COMPILER_SSE_FLAG)
  set(COMPILER_SSE2_FLAG)

  # Test Neon first since macOS Arm can compile and run x86-64 SSE binaries.
  TEST_NEON_SUPPORT()
  if(NOT SUPPORT_NEON_BUILD)
    TEST_SSE_SUPPORT(COMPILER_SSE_FLAG COMPILER_SSE2_FLAG)
  endif()
endif()

# ----------------------------------------------------------------------------
# Main Platform Checks
#
# - UNIX
# - WIN32
# - APPLE

if(UNIX AND NOT APPLE)
  include(platform_unix)
elseif(WIN32)
  include(platform_win32)
elseif(APPLE)
  include(platform_apple)
endif()

#-----------------------------------------------------------------------------
# Common.

if(NOT WITH_FFTW3 AND WITH_MOD_OCEANSIM)
  message(FATAL_ERROR "WITH_MOD_OCEANSIM requires WITH_FFTW3 to be ON")
endif()

# Enable SIMD support if detected by TEST_SSE_SUPPORT() or TEST_NEON_SUPPORT().
#
# This is done globally, so that all modules can use it if available, and
# because these are used in headers used by many modules.
if(WITH_CPU_SIMD)
  if(SUPPORT_NEON_BUILD)
    # Neon
    if(SSE2NEON_FOUND)
      blender_include_dirs_sys("${SSE2NEON_INCLUDE_DIRS}")
      add_definitions(-DWITH_SSE2NEON)
    endif()
  else()
    # SSE
    if(SUPPORT_SSE_BUILD)
      string(PREPEND PLATFORM_CFLAGS "${COMPILER_SSE_FLAG} ")
      add_definitions(-D__SSE__ -D__MMX__)
    endif()
    if(SUPPORT_SSE2_BUILD)
      string(APPEND PLATFORM_CFLAGS " ${COMPILER_SSE2_FLAG}")
      add_definitions(-D__SSE2__)
      if(NOT SUPPORT_SSE_BUILD) # don't double up
        add_definitions(-D__MMX__)
      endif()
    endif()
  endif()

  # Print instructions used
  if(SUPPORT_NEON_BUILD)
    if(SSE2NEON_FOUND)
      message(STATUS "Neon SIMD instructions enabled")
    else()
      message(STATUS "Neon SIMD instructions detected but unused, requires sse2neon")
    endif()
  elseif(SUPPORT_SSE2_BUILD)
    message(STATUS "SSE2 SIMD instructions enabled")
  elseif(SUPPORT_SSE_BUILD)
    message(STATUS "SSE SIMD instructions enabled")
  else()
    message(STATUS "No SIMD instructions detected")
  endif()
else()
  message(STATUS "SIMD instructions disabled")
endif()

# set the endian define
if(MSVC)
  # for some reason this fails on msvc
  add_definitions(-D__LITTLE_ENDIAN__)

  # OSX-Note: as we do cross-compiling with specific set architecture,
  # endianness-detection and auto-setting is counterproductive
  # so we just set endianness according CMAKE_OSX_ARCHITECTURES

elseif(CMAKE_OSX_ARCHITECTURES MATCHES i386 OR CMAKE_OSX_ARCHITECTURES MATCHES x86_64 OR CMAKE_OSX_ARCHITECTURES MATCHES arm64)
  add_definitions(-D__LITTLE_ENDIAN__)
elseif(CMAKE_OSX_ARCHITECTURES MATCHES ppc OR CMAKE_OSX_ARCHITECTURES MATCHES ppc64)
  add_definitions(-D__BIG_ENDIAN__)

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
if(WITH_IMAGE_OPENJPEG)
  # Special handling of Windows platform where openjpeg is always static.
  if(WIN32)
    set(OPENJPEG_DEFINES "-DOPJ_STATIC")
  else()
    set(OPENJPEG_DEFINES "")
  endif()
endif()

if(NOT WITH_SYSTEM_EIGEN3)
  set(EIGEN3_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/extern/Eigen3)
endif()

if(WITH_OPENVDB)
  list(APPEND OPENVDB_DEFINITIONS -DWITH_OPENVDB)

  if(WITH_OPENVDB_3_ABI_COMPATIBLE)
    list(APPEND OPENVDB_DEFINITIONS -DOPENVDB_3_ABI_COMPATIBLE)
  endif()

  # OpenVDB headers use deprecated TBB headers, silence warning.
  list(APPEND OPENVDB_DEFINITIONS -DTBB_SUPPRESS_DEPRECATED_MESSAGES=1)

  list(APPEND OPENVDB_INCLUDE_DIRS
    ${BOOST_INCLUDE_DIR}
    ${TBB_INCLUDE_DIRS}
    ${OPENEXR_INCLUDE_DIRS})

  list(APPEND OPENVDB_LIBRARIES ${OPENEXR_LIBRARIES} ${ZLIB_LIBRARIES})

  if(WITH_OPENVDB_BLOSC)
    list(APPEND OPENVDB_DEFINITIONS -DWITH_OPENVDB_BLOSC)
    list(APPEND OPENVDB_LIBRARIES ${BLOSC_LIBRARIES} ${ZLIB_LIBRARIES})
  endif()

  list(APPEND OPENVDB_LIBRARIES ${BOOST_LIBRARIES} ${TBB_LIBRARIES})
endif()

#-----------------------------------------------------------------------------
# Configure OpenGL.

find_package(OpenGL)
blender_include_dirs_sys("${OPENGL_INCLUDE_DIR}")

if(WITH_OPENGL)
  add_definitions(-DWITH_OPENGL)
endif()

if(WITH_SYSTEM_GLES)
  find_package_wrapper(OpenGLES)
endif()

if(WITH_GL_PROFILE_ES20)
  if(WITH_SYSTEM_GLES)
    if(NOT OPENGLES_LIBRARY)
      message(FATAL_ERROR
        "Unable to find OpenGL ES libraries. "
        "Install them or disable WITH_SYSTEM_GLES."
      )
    endif()

    list(APPEND BLENDER_GL_LIBRARIES "${OPENGLES_LIBRARY}")

  else()
    set(OPENGLES_LIBRARY "" CACHE FILEPATH "OpenGL ES 2.0 library file")
    mark_as_advanced(OPENGLES_LIBRARY)

    list(APPEND BLENDER_GL_LIBRARIES "${OPENGLES_LIBRARY}")

    if(NOT OPENGLES_LIBRARY)
      message(FATAL_ERROR
        "To compile WITH_GL_EGL you need to set OPENGLES_LIBRARY "
        "to the file path of an OpenGL ES 2.0 library."
      )
    endif()

  endif()

  if(WIN32)
    # Setup paths to files needed to install and redistribute Windows Blender with OpenGL ES

    set(OPENGLES_DLL "" CACHE FILEPATH "OpenGL ES 2.0 redistributable DLL file")
    mark_as_advanced(OPENGLES_DLL)

    if(NOT OPENGLES_DLL)
      message(FATAL_ERROR
        "To compile WITH_GL_PROFILE_ES20 you need to set OPENGLES_DLL to the file "
        "path of an OpenGL ES 2.0 runtime dynamic link library (DLL)."
      )
    endif()

    if(WITH_GL_ANGLE)
      list(APPEND GL_DEFINITIONS -DWITH_ANGLE)

      set(D3DCOMPILER_DLL "" CACHE FILEPATH "Direct3D Compiler redistributable DLL file (needed by ANGLE)")

      get_filename_component(D3DCOMPILER_FILENAME "${D3DCOMPILER_DLL}" NAME)
      list(APPEND GL_DEFINITIONS "-DD3DCOMPILER=\"\\\"${D3DCOMPILER_FILENAME}\\\"\"")

      mark_as_advanced(D3DCOMPILER_DLL)

      if(D3DCOMPILER_DLL STREQUAL "")
        message(FATAL_ERROR
          "To compile WITH_GL_ANGLE you need to set D3DCOMPILER_DLL to the file "
          "path of a copy of the DirectX redistributable DLL file: D3DCompiler_46.dll"
        )
      endif()

    endif()

  endif()

else()
  if(OpenGL_GL_PREFERENCE STREQUAL "LEGACY" AND OPENGL_gl_LIBRARY)
    list(APPEND BLENDER_GL_LIBRARIES ${OPENGL_gl_LIBRARY})
  else()
    list(APPEND BLENDER_GL_LIBRARIES ${OPENGL_opengl_LIBRARY} ${OPENGL_glx_LIBRARY})
  endif()
endif()

if(WITH_GL_EGL)
  find_package(OpenGL REQUIRED EGL)
  list(APPEND BLENDER_GL_LIBRARIES OpenGL::EGL)

  list(APPEND GL_DEFINITIONS -DWITH_GL_EGL -DGLEW_EGL -DGLEW_INC_EGL)

  if(WITH_SYSTEM_GLES)
    if(NOT OPENGLES_EGL_LIBRARY)
      message(FATAL_ERROR
        "Unable to find OpenGL ES libraries. "
        "Install them or disable WITH_SYSTEM_GLES."
      )
    endif()

    list(APPEND BLENDER_GL_LIBRARIES ${OPENGLES_EGL_LIBRARY})

  else()
    set(OPENGLES_EGL_LIBRARY "" CACHE FILEPATH "EGL library file")
    mark_as_advanced(OPENGLES_EGL_LIBRARY)

    list(APPEND BLENDER_GL_LIBRARIES "${OPENGLES_LIBRARY}" "${OPENGLES_EGL_LIBRARY}")

    if(NOT OPENGLES_EGL_LIBRARY)
      message(FATAL_ERROR
        "To compile WITH_GL_EGL you need to set OPENGLES_EGL_LIBRARY "
        "to the file path of an EGL library."
      )
    endif()

  endif()

  if(WIN32)
    # Setup paths to files needed to install and redistribute Windows Blender with OpenGL ES

    set(OPENGLES_EGL_DLL "" CACHE FILEPATH "EGL redistributable DLL file")
    mark_as_advanced(OPENGLES_EGL_DLL)

    if(NOT OPENGLES_EGL_DLL)
      message(FATAL_ERROR
        "To compile WITH_GL_EGL you need to set OPENGLES_EGL_DLL "
        "to the file path of an EGL runtime dynamic link library (DLL)."
      )
    endif()

  endif()

endif()

if(WITH_GL_PROFILE_ES20)
  list(APPEND GL_DEFINITIONS -DWITH_GL_PROFILE_ES20)
else()
  list(APPEND GL_DEFINITIONS -DWITH_GL_PROFILE_CORE)
endif()

#-----------------------------------------------------------------------------
# Configure Metal.
if (WITH_METAL_BACKEND)
  add_definitions(-DWITH_METAL_BACKEND)

  # No need to add frameworks here, all the ones we need for Metal and
  # Metal-OpenGL Interop are already being added by
  # build_files/cmake/platform/platform_apple.cmake
endif()

#-----------------------------------------------------------------------------
# Configure OpenMP.
if(WITH_OPENMP)
  if(NOT OPENMP_CUSTOM)
    find_package(OpenMP)
  endif()

  if(OPENMP_FOUND)
    if(NOT WITH_OPENMP_STATIC)
      string(APPEND CMAKE_C_FLAGS " ${OpenMP_C_FLAGS}")
      string(APPEND CMAKE_EXE_LINKER_FLAGS " ${OpenMP_LINKER_FLAGS}")
      string(APPEND CMAKE_MODULE_LINKER_FLAGS " ${OpenMP_LINKER_FLAGS}")
    else()
      # Typically avoid adding flags as defines but we can't
      # pass OpenMP flags to the linker for static builds, meaning
      # we can't add any OpenMP related flags to CFLAGS variables
      # since they're passed to the linker as well.
      add_definitions("${OpenMP_C_FLAGS}")

      find_library_static(OpenMP_LIBRARIES gomp ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
    endif()
  else()
    message(STATUS "OpenMP not found, disabling WITH_OPENMP")
    set(WITH_OPENMP OFF)
  endif()

  mark_as_advanced(OpenMP_C_FLAGS)
endif()

#-----------------------------------------------------------------------------
# Configure GLEW

if(WITH_SYSTEM_GLEW)
  find_package(GLEW)

  # Note: There is an assumption here that the system GLEW is not a static library.

  if(NOT GLEW_FOUND)
    message(FATAL_ERROR "GLEW is required to build Blender. Install it or disable WITH_SYSTEM_GLEW.")
  endif()

  set(GLEW_INCLUDE_PATH "${GLEW_INCLUDE_DIR}")
  set(DUNE_GLEW_LIBRARIES ${GLEW_LIBRARY})
else()
  if(WITH_GLEW_ES)
    set(GLEW_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/extern/glew-es/include")

    list(APPEND GL_DEFINITIONS -DGLEW_STATIC -DWITH_GLEW_ES)

    # These definitions remove APIs from glew.h, making GLEW smaller, and catching unguarded API usage
    if(WITH_GL_PROFILE_ES20)
      list(APPEND GL_DEFINITIONS -DGLEW_ES_ONLY)
    else()
      # No ES functions are needed
      list(APPEND GL_DEFINITIONS -DGLEW_NO_ES)
    endif()

    if(WITH_GL_PROFILE_ES20)
      if(WITH_GL_EGL)
        list(APPEND GL_DEFINITIONS -DGLEW_USE_LIB_ES20)
      endif()

      # ToDo: This is an experiment to eliminate ES 1 symbols,
      # GLEW doesn't really properly provide this level of control
      # (for example, without modification it eliminates too many symbols)
      # so there are lots of modifications to GLEW to make this work,
      # and no attempt to make it work beyond Blender at this point.
      list(APPEND GL_DEFINITIONS -DGL_ES_VERSION_1_0=0 -DGL_ES_VERSION_CL_1_1=0 -DGL_ES_VERSION_CM_1_1=0)
    endif()

    set(DUNE_GLEW_LIBRARIES extern_glew_es bf_intern_glew_mx)

  else()
    set(GLEW_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/extern/glew/include")

    list(APPEND GL_DEFINITIONS -DGLEW_STATIC)

    # This won't affect the non-experimental glew library,
    # but is used for conditional compilation elsewhere.
    list(APPEND GL_DEFINITIONS -DGLEW_NO_ES)

    set(DUNE_GLEW_LIBRARIES extern_glew)

  endif()

endif()

list(APPEND GL_DEFINITIONS -DGLEW_NO_GLU)

#-----------------------------------------------------------------------------
# Configure Bullet

if(WITH_BULLET AND WITH_SYSTEM_BULLET)
  find_package(Bullet)
  if(NOT BULLET_FOUND)
    message(STATUS "Bullet not found, disabling WITH_BULLET")
    set(WITH_BULLET OFF)
  endif()
else()
  set(BULLET_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/extern/bullet2/src")
  # set(BULLET_LIBRARIES "")
endif()

#-----------------------------------------------------------------------------
# Configure GLog/GFlags

if(WITH_LIBMV OR WITH_GTESTS OR (WITH_CYCLES AND WITH_CYCLES_LOGGING))
  if(WITH_SYSTEM_GFLAGS)
    find_package(Gflags)
    if(NOT GFLAGS_FOUND)
      message(FATAL_ERROR "System wide Gflags is requested but was not found")
    endif()
    # FindGflags does not define this, and we are not even sure what to use here.
    set(GFLAGS_DEFINES)
  else()
    set(GFLAGS_DEFINES
      -DGFLAGS_DLL_DEFINE_FLAG=
      -DGFLAGS_DLL_DECLARE_FLAG=
      -DGFLAGS_DLL_DECL=
    )
    set(GFLAGS_NAMESPACE "gflags")
    set(GFLAGS_LIBRARIES extern_gflags)
    set(GFLAGS_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/extern/gflags/src")
  endif()

  if(WITH_SYSTEM_GLOG)
    find_package(Glog)
    if(NOT GLOG_FOUND)
      message(FATAL_ERROR "System wide Glog is requested but was not found")
    endif()
    # FindGlog does not define this, and we are not even sure what to use here.
    set(GLOG_DEFINES)
  else()
    set(GLOG_DEFINES
      -DGOOGLE_GLOG_DLL_DECL=
    )
    set(GLOG_LIBRARIES extern_glog)
    if(WIN32)
      set(GLOG_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/extern/glog/src/windows)
    else()
      set(GLOG_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/extern/glog/include)
    endif()
  endif()
endif()

#-----------------------------------------------------------------------------
# Configure Ceres

if(WITH_LIBMV)
  # We always have C++11 which includes unordered_map.
  set(CERES_DEFINES "-DCERES_STD_UNORDERED_MAP;-DCERES_USE_CXX_THREADS")
endif()

#-----------------------------------------------------------------------------
# Extra limits to number of jobs running in parallel for some kind os tasks.
# Only supported by Ninja build system currently.
if("${CMAKE_GENERATOR}" MATCHES "Ninja" AND WITH_NINJA_POOL_JOBS)
  if(NOT NINJA_MAX_NUM_PARALLEL_COMPILE_JOBS AND
     NOT NINJA_MAX_NUM_PARALLEL_COMPILE_HEAVY_JOBS AND
     NOT NINJA_MAX_NUM_PARALLEL_LINK_JOBS)
    # Try to define good default values.
    # Max mem of heavy cpp files compilation: about 2.5GB
    # Max mem during linking: about 3.3GB
    cmake_host_system_information(RESULT _NUM_CORES QUERY NUMBER_OF_LOGICAL_CORES)
    # Note: this gives mem in MB.
    cmake_host_system_information(RESULT _TOT_MEM QUERY TOTAL_PHYSICAL_MEMORY)

    # Heuristics... the more cores we have, the more free mem we have to keep for the non-heavy tasks too.
    if(${_TOT_MEM} LESS 8000 AND ${_NUM_CORES} GREATER 2)
      set(_compile_heavy_jobs "1")
    elseif(${_TOT_MEM} LESS 16000 AND ${_NUM_CORES} GREATER 4)
      set(_compile_heavy_jobs "2")
    elseif(${_TOT_MEM} LESS 24000 AND ${_NUM_CORES} GREATER 8)
      set(_compile_heavy_jobs "3")
    elseif(${_TOT_MEM} LESS 32000 AND ${_NUM_CORES} GREATER 16)
      set(_compile_heavy_jobs "4")
    elseif(${_TOT_MEM} LESS 64000 AND ${_NUM_CORES} GREATER 32)
      set(_compile_heavy_jobs "8")
    else()
      set(_compile_heavy_jobs "")
    endif()

    set(NINJA_MAX_NUM_PARALLEL_COMPILE_HEAVY_JOBS "${_compile_heavy_jobs}" CACHE STRING
        "Define the maximum number of concurrent heavy compilation jobs, for ninja build system (used for some targets which cpp files can take several GB each during compilation)." FORCE)
    mark_as_advanced(NINJA_MAX_NUM_PARALLEL_COMPILE_HEAVY_JOBS)
    set(_compile_heavy_jobs)

    # Only set regular compile jobs if we set heavy jobs, otherwise default (using all cores) if fine.
    if(NINJA_MAX_NUM_PARALLEL_COMPILE_HEAVY_JOBS)
      math(EXPR _compile_jobs "${_NUM_CORES} - 1")
    else()
      set(_compile_jobs "")
    endif()
    set(NINJA_MAX_NUM_PARALLEL_COMPILE_JOBS "${_compile_jobs}" CACHE STRING
        "Define the maximum number of concurrent compilation jobs, for ninja build system." FORCE)
    mark_as_advanced(NINJA_MAX_NUM_PARALLEL_COMPILE_JOBS)
    set(_compile_jobs)

    # In practice, even when there is RAM available, this proves to be quicker than running in parallel
    # (due to slow disks accesses).
    set(NINJA_MAX_NUM_PARALLEL_LINK_JOBS "1" CACHE STRING
        "Define the maximum number of concurrent link jobs, for ninja build system." FORCE)
    mark_as_advanced(NINJA_MAX_NUM_PARALLEL_LINK_JOBS)

    set(_NUM_CORES)
    set(_TOT_MEM)
  endif()

  if(NINJA_MAX_NUM_PARALLEL_COMPILE_JOBS)
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS compile_job_pool=${NINJA_MAX_NUM_PARALLEL_COMPILE_JOBS})
    set(CMAKE_JOB_POOL_COMPILE compile_job_pool)
  endif()

  if(NINJA_MAX_NUM_PARALLEL_COMPILE_HEAVY_JOBS)
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS compile_heavy_job_pool=${NINJA_MAX_NUM_PARALLEL_COMPILE_HEAVY_JOBS})
  endif()

  if(NINJA_MAX_NUM_PARALLEL_LINK_JOBS)
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS link_job_pool=${NINJA_MAX_NUM_PARALLEL_LINK_JOBS})
    set(CMAKE_JOB_POOL_LINK link_job_pool)
  endif()
endif()

#-----------------------------------------------------------------------------
# Extra compile flags
if(CMAKE_COMPILER_IS_GNUCC)

  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_ALL -Wall)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_CAST_ALIGN -Wcast-align)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_ERROR_IMPLICIT_FUNCTION_DECLARATION -Werror=implicit-function-declaration)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_ERROR_RETURN_TYPE  -Werror=return-type)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_ERROR_VLA -Werror=vla)
  # system headers sometimes do this, disable for now, was: -Werror=strict-prototypes
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_STRICT_PROTOTYPES  -Wstrict-prototypes)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_MISSING_PROTOTYPES -Wmissing-prototypes)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_NO_CHAR_SUBSCRIPTS -Wno-char-subscripts)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_NO_UNKNOWN_PRAGMAS -Wno-unknown-pragmas)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_POINTER_ARITH -Wpointer-arith)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_UNUSED_PARAMETER -Wunused-parameter)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_WRITE_STRINGS -Wwrite-strings)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_LOGICAL_OP -Wlogical-op)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_UNDEF -Wundef)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_INIT_SELF -Winit-self)  # needs -Wuninitialized
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_MISSING_INCLUDE_DIRS -Wmissing-include-dirs)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_NO_DIV_BY_ZERO -Wno-div-by-zero)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_TYPE_LIMITS -Wtype-limits)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_FORMAT_SIGN -Wformat-signedness)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_RESTRICT -Wrestrict)

  # C-only.
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_NO_NULL -Wnonnull)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_ABSOLUTE_VALUE -Wabsolute-value)

  # gcc 4.2 gives annoying warnings on every file with this
  if(NOT "${CMAKE_C_COMPILER_VERSION}" VERSION_LESS "4.3")
    ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_UNINITIALIZED -Wuninitialized)
  endif()

  # versions before gcc4.6 give many LIB_math warnings
   # versions before gcc4.8 include global name-space.
  if(NOT "${CMAKE_C_COMPILER_VERSION}" VERSION_LESS "4.8")
    ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_SHADOW -Wshadow)
  endif()

  if(NOT APPLE)
    ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_NO_ERROR_UNUSED_BUT_SET_VARIABLE -Wno-error=unused-but-set-variable)
  endif()

  # Use 'ATTR_FALLTHROUGH' macro to suppress.
  if(CMAKE_COMPILER_IS_GNUCC AND (NOT "${CMAKE_C_COMPILER_VERSION}" VERSION_LESS "7.0"))
    ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_IMPLICIT_FALLTHROUGH -Wimplicit-fallthrough=5)
  endif()
  # flags to undo strict flags
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_DEPRECATED_DECLARATIONS -Wno-deprecated-declarations)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNUSED_PARAMETER        -Wno-unused-parameter)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNUSED_FUNCTION         -Wno-unused-function)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_TYPE_LIMITS             -Wno-type-limits)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_INT_IN_BOOL_CONTEXT     -Wno-int-in-bool-context)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_FORMAT                  -Wno-format)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_SWITCH                  -Wno-switch)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNUSED_VARIABLE         -Wno-unused-variable)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNUSED_VARIABLE         -Wno-uninitialized)

  if(CMAKE_COMPILER_IS_GNUCC AND (NOT "${CMAKE_C_COMPILER_VERSION}" VERSION_LESS "7.0"))
    ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_IMPLICIT_FALLTHROUGH    -Wno-implicit-fallthrough)
  endif()

  if(NOT APPLE)
    ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_ERROR_UNUSED_BUT_SET_VARIABLE -Wno-error=unused-but-set-variable)
  endif()

elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")

  # strange, clang complains these are not supported, but then uses them.
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_ALL -Wall)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_ERROR_IMPLICIT_FUNCTION_DECLARATION -Werror=implicit-function-declaration)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_ERROR_RETURN_TYPE  -Werror=return-type)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_NO_AUTOLOGICAL_COMPARE -Wno-tautological-compare)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_NO_UNKNOWN_PRAGMAS -Wno-unknown-pragmas)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_NO_CHAR_SUBSCRIPTS -Wno-char-subscripts)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_STRICT_PROTOTYPES  -Wstrict-prototypes)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_MISSING_PROTOTYPES -Wmissing-prototypes)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_UNUSED_PARAMETER -Wunused-parameter)

  # flags to undo strict flags
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNUSED_PARAMETER -Wno-unused-parameter)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNUSED_VARIABLE  -Wno-unused-variable)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNUSED_MACROS    -Wno-unused-macros)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_MISLEADING_INDENTATION    -Wno-misleading-indentation)

  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_MISSING_VARIABLE_DECLARATIONS -Wno-missing-variable-declarations)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_INCOMPAT_PTR_DISCARD_QUAL -Wno-incompatible-pointer-types-discards-qualifiers)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNUSED_FUNCTION -Wno-unused-function)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_INT_TO_VOID_POINTER_CAST -Wno-int-to-void-pointer-cast)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_MISSING_PROTOTYPES -Wno-missing-prototypes)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_DUPLICATE_ENUM -Wno-duplicate-enum)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNDEF -Wno-undef)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_MISSING_NORETURN -Wno-missing-noreturn)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_UNUSED_BUT_SET_VARIABLE -Wno-unused-but-set-variable)
  ADD_CHECK_C_COMPILER_FLAG(C_REMOVE_STRICT_FLAGS C_WARN_NO_DEPRECATED_DECLARATIONS -Wno-deprecated-declarations)

elseif(CMAKE_C_COMPILER_ID MATCHES "Intel")

  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_ALL -Wall)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_POINTER_ARITH -Wpointer-arith)
  ADD_CHECK_C_COMPILER_FLAG(C_WARNINGS C_WARN_NO_UNKNOWN_PRAGMAS -Wno-unknown-pragmas)

  # disable numbered, false positives
  string(APPEND C_WARNINGS " -wd188,186,144,913,556,858,597,177,1292,167,279,592,94,2722,3199")
 
