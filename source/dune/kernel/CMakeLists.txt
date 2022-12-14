

set(INC
  .
  ../font
  ../lib
  ../loader
  ../translation
  ../bmesh
  ../depsgraph
  ../draw
  ../functions
  ../gpencil_modifiers
  ../gpu
  ../ikplugin
  ../imbuf

  ../modifiers
  ../nodes
  ../render
  ../sequencer
  ../shader_fx
  ../simulation
  ../../../intern/eigen
  ../../../intern/ghost
  ../../../intern/glew-mx
  ../../../intern/guardedalloc
  ../../../intern/iksolver/extern
  ../../../intern/atomic
  ../../../intern/clog
  ../../../intern/libmv
  ../../../intern/mantaflow/extern
  ../../../intern/memutil
  ../../../intern/mikktspace
  ../../../intern/opensubdiv
  ../../../extern/curve_fit_nd

  # dna_type_offsets.h
  # ${CMAKE_CURRENT_BINARY_DIR}/../makesdna/intern
  # RNA_prototypes.h
  # ${CMAKE_BINARY_DIR}/source/blender/makesrna
)

set(INC_SYS
  ${ZLIB_INCLUDE_DIRS}

  # For `vfontdata_freetype.c`.
  ${FREETYPE_INCLUDE_DIRS}
)

set(SRC
  ${CMAKE_SOURCE_DIR}/release/datafiles/userdef/userdef_default.c
  intern/CCGSubSurf.c
  intern/CCGSubSurf_legacy.c
  intern/CCGSubSurf_util.c
  intern/DerivedMesh.c
  intern/action.c
  intern/action_bones.c
  intern/action_mirror.c
  intern/addon.c
  intern/anim_data.c
  intern/anim_path.c
  intern/anim_sys.c
  intern/anim_visualization.c
  intern/anonymous_attribute.c
  intern/appdir.c
  intern/armature.c
  intern/armature_deform.c
  intern/armature_pose.c
  intern/armature_selection.c
  intern/armature_update.c
  intern/asset.c
)

set(LIB
  dune_font
  dune_lib
  dune_loader
  dune_translation
  dune_bmesh
  dune_depsgraph
  dune_draw
  dune_functions
  dune_gpencil_modifiers
  dune_gpu
  dune_ikplugin
  dune_imbuf
  dune_intern_clog
  dune_intern_ghost
  dune_intern_guardedalloc
  dune_intern_libmv  # Uses stub when disabled.
  dune_intern_mikktspace
  dune_intern_opensubdiv  # Uses stub when disabled.
  dune_modifiers
  dune_nodes
  dune_rna
  dune_shader_fx
  dune_simulation

  # For `vfontdata_freetype.c`.
  ${FREETYPE_LIBRARIES} ${BROTLI_LIBRARIES}
)

if(WITH_BINRELOC)
  list(APPEND INC_SYS
    ${BINRELOC_INCLUDE_DIRS}
  )
  list(APPEND LIB
    extern_binreloc
  )
  add_definitions(-DWITH_BINRELOC)
endif()


if(WIN32)
  list(APPEND INC
    ../../../intern/utfconv
  )
endif()

if(WITH_AUDASPACE)
  add_definitions(-DWITH_AUDASPACE)

  list(APPEND INC_SYS
    ${AUDASPACE_C_INCLUDE_DIRS}
  )
  list(APPEND LIB
    ${AUDASPACE_C_LIBRARIES}
    ${AUDASPACE_PY_LIBRARIES}
  )
endif()

if(WITH_BULLET)
  list(APPEND INC_SYS
    ${BULLET_INCLUDE_DIRS}
  )
  list(APPEND INC
    ../../../intern/rigidbody
  )

  if(NOT WITH_SYSTEM_BULLET)
    list(APPEND LIB
      extern_bullet
    )
  endif()

  list(APPEND LIB
    dune_intern_rigidbody

    ${BULLET_LIBRARIES}
  )
  add_definitions(-DWITH_BULLET)
endif()

if(WITH_IMAGE_OPENEXR)
  add_definitions(-DWITH_OPENEXR)
endif()

if(WITH_IMAGE_TIFF)
  add_definitions(-DWITH_TIFF)
endif()

if(WITH_OPENIMAGEIO)
  add_definitions(-DWITH_OPENIMAGEIO)
endif()

if(WITH_IMAGE_OPENJPEG)
  add_definitions(-DWITH_OPENJPEG)
endif()

if(WITH_IMAGE_DDS)
  add_definitions(-DWITH_DDS)
endif()

if(WITH_IMAGE_CINEON)
  add_definitions(-DWITH_CINEON)
endif()

if(WITH_IMAGE_FRAMESERVER)
  add_definitions(-DWITH_FRAMESERVER)
endif()

if(WITH_IMAGE_HDR)
  add_definitions(-DWITH_HDR)
endif()

if(WITH_CODEC_AVI)
  list(APPEND INC
    ../io/avi
  )
  add_definitions(-DWITH_AVI)
endif()

if(WITH_CODEC_FFMPEG)
  list(APPEND SRC
    intern/writeffmpeg.c
    BKE_writeffmpeg.h
  )
  list(APPEND INC
    ../../../intern/ffmpeg
  )
  list(APPEND INC_SYS
    ${FFMPEG_INCLUDE_DIRS}
  )
  list(APPEND LIB
    ${FFMPEG_LIBRARIES}
  )
  add_definitions(-DWITH_FFMPEG)
endif()

if(WITH_MOD_FLUID)
  list(APPEND LIB
    dune_intern_mantaflow
  )
  add_definitions(-DWITH_FLUID)
endif()

if(WITH_MOD_OCEANSIM)
  add_definitions(-DWITH_OCEANSIM)
endif()

if(WITH_JACK)
  add_definitions(-DWITH_JACK)
endif()

if(WITH_LZO)
  if(WITH_SYSTEM_LZO)
    list(APPEND INC_SYS
      ${LZO_INCLUDE_DIR}
    )
    list(APPEND LIB
      ${LZO_LIBRARIES}
    )
    add_definitions(-DWITH_SYSTEM_LZO)
  else()
    list(APPEND INC_SYS
      ../../../extern/lzo/minilzo
    )
    list(APPEND LIB
      extern_minilzo
    )
  endif()
  add_definitions(-DWITH_LZO)
endif()

if(WITH_LZMA)
  list(APPEND INC_SYS
    ../../../extern/lzma
  )
  list(APPEND LIB
    extern_lzma
  )
  add_definitions(-DWITH_LZMA)
endif()

if(WITH_LIBMV)
  add_definitions(-DWITH_LIBMV)
endif()

if(WITH_FFTW3)
  list(APPEND INC_SYS
    ${FFTW3_INCLUDE_DIRS}
  )
  list(APPEND LIB
    ${FFTW3_LIBRARIES}
  )
  add_definitions(-DFFTW3=1)
endif()

if(WITH_FREESTYLE)
  add_definitions(-DWITH_FREESTYLE)
endif()

if(WITH_ALEMBIC)
  list(APPEND INC
    ../io/alembic
  )
  add_definitions(-DWITH_ALEMBIC)
endif()

if(WITH_USD)
  list(APPEND INC
    ../io/usd
  )
  add_definitions(-DWITH_USD)
endif()

if(WITH_OPENSUBDIV)
  list(APPEND INC_SYS
    ${OPENSUBDIV_INCLUDE_DIRS}
  )
  list(APPEND LIB
    ${OPENSUBDIV_LIBRARIES}
  )
  add_definitions(-DWITH_OPENSUBDIV)
endif()

if(WITH_OPENVDB)
  list(APPEND INC
    ../../../intern/openvdb
  )
  list(APPEND INC_SYS
    ${OPENVDB_INCLUDE_DIRS}
  )
  list(APPEND LIB
    bf_intern_openvdb
    ${OPENVDB_LIBRARIES}
  )
  add_definitions(-DWITH_OPENVDB ${OPENVDB_DEFINITIONS})
endif()

if(WITH_QUADRIFLOW)
  list(APPEND INC
    ../../../intern/quadriflow
  )
  list(APPEND LIB
    bf_intern_quadriflow
  )
  add_definitions(-DWITH_QUADRIFLOW)
endif()

if(WITH_XR_OPENXR)
  add_definitions(-DWITH_XR_OPENXR)
endif()

if(WITH_TBB)
  add_definitions(-DWITH_TBB)

  list(APPEND INC_SYS
    ${TBB_INCLUDE_DIRS}
  )

  list(APPEND LIB
    ${TBB_LIBRARIES}
  )
endif()

if(WITH_GMP)
  add_definitions(-DWITH_GMP)

  list(APPEND INC_SYS
    ${GMP_INCLUDE_DIRS}
  )
endif()

dune_add_lib(dune_kernel "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

dune_add_test_lib(dune_kernel_tests "${TEST_SRC}" "${INC};${TEST_INC}" "${INC_SYS}" "${LIB}")
