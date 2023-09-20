if(CMAKE_COMPILER_IS_GNUCC)
  # add here so we fail early.
  string(APPEND CMAKE_C_FLAGS " -Werror=implicit-function-declaration")
endif()

# files rna_access.c rna_define.c makesrna.c intentionally excluded.
set(DEFSRC
  api_ID.c
  api_action.c
  api_animation.c
  api_animviz.c
  api_armature.c
  api_asset.c
  api_attribute.c
  api_boid.c
  api_brush.c
  api_cachefile.c
  api_camera.c
  api_cloth.c
  api_collection.c
  api_color.c
  api_constraint.c
  api_cxt.c
  api_curve.c
  api_curveprofile.c
  api_graph.c
  api_dynamicpaint.c
  api_fcurve.c
  api_fluid.c
  api_pen.c
  api_pen_mod.c
  api_image.c
  api_key.c
  api_lattice.c
  api_layer.c
  api_light.c
  api_lightprobe.c
  api_linestyle.c
  api_main.c
  api_mask.c
  api_material.c
  api_mesh.c
  api_meta.c
  api_mod.c
  api_movieclip.c
  api_nla.c
  api_nodetree.c
  api_object.c
  api_object_force.c
  api_packedfile.c
  api_palette.c
  api_particle.c
  api_pointcloud.c
  api_pose.c
  api_render.c
  api_rigidbody.c
  api.c
  api_scene.c
  api_screen.c
  api_sculpt_paint.c
  api_seq.c
  api_shader_fx.c
  api_sound.c
  api_space.c
  api_speaker.c
  api_test.c
  api_text.c
  api_texture.c
  api_timeline.c
  api_tracking.c
  api_ui.c
  rna_userdef.c
  rna_vfont.c
  rna_volume.c
  rna_wm.c
  rna_wm_gizmo.c
  rna_workspace.c
  rna_world.c
  rna_xr.c
)

if(WITH_EXPERIMENTAL_FEATURES)
  add_definitions(-DWITH_SIMULATION_DATABLOCK)
  add_definitions(-DWITH_NEW_CURVES_TYPE)
  list(APPEND DEFSRC
    api_curves.c
    api_simulation.c
  )
endif()

set(APISRC
  api_action_api.c
  api_animation_api.c
  api_armature_api.c
  api_camera_api.c
  api_curve_api.c
  api_fcurve_api.c
  api_image_api.c
  api_lattice_api.c
  api_main_api.c
  api_material_api.c
  api_mesh_api.c
  api_meta_api.c
  api_object_api.c
  api_pose_api.c
  api_scene_api.c
  api_seq_api.c
  api_sound_api.c
  api_space_api.c
  api_text_api.c
  api_texture_api.c
  api_ui_api.c
  api_vfont_api.c
  api_wm_api.c
  api_wm_gizmo_api.c
  api_workspace_api.c
)

string(REGEX REPLACE "api_([a-zA-Z0-9_-]*).c" "${CMAKE_CURRENT_BINARY_DIR}/rna_\\1_gen.c" GENSRC "${DEFSRC}")
list(APPEND GENSRC
  "${CMAKE_CURRENT_BINARY_DIR}/rna_prototypes_gen.h"
  "${CMAKE_CURRENT_BINARY_DIR}/../RNA_prototypes.h"
)
set_source_files_properties(${GENSRC} PROPERTIES GENERATED TRUE)

# --------------------------
# CFLAGS for Generated Files
#
# less strict flags for generated source
set(GENSRC_CFLAGS)
if(CMAKE_COMPILER_IS_GNUCC OR (CMAKE_C_COMPILER_ID MATCHES "Clang"))
  set(GENSRC_CFLAGS "-Wno-missing-prototypes")
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  string(APPEND GENSRC_CFLAGS " -Wno-missing-variable-declarations")
endif()

if(GENSRC_CFLAGS)
  set_source_files_properties(${GENSRC} PROPERTIES COMPILE_FLAGS "${GENSRC_CFLAGS}")
endif()
unset(GENSRC_CFLAGS)


# NOTE: Disable clang-tidy because generated files are stored outside of the source,
# so the clang-tidy can not find our .clang-tidy and fall-backs to own set of rules
# which are too noisy for Blender.
#
# In the future clang-tidy would either need to be inlined checks and passed via the
# command line (instead of using .clang-tidy file). Or, maybe, there is a way to
# pass configuration file to the clang-tidy command.
unset(CMAKE_C_CLANG_TIDY)
unset(CMAKE_CXX_CLANG_TIDY)

set(SRC_API_INC
  ../api_access.h
  ../api_define.h
  ../api_documentation.h
  ../api_enum_items.h
  ../api_enum_types.h
  ../api_types.h
)

set(SRC
  api.c
  api_define.c
  ${DEFSRC}
  ${APISRC}
  ../../../../intern/clog/clog.c
  ../../../../intern/guardedalloc/intern/leak_detector.cc
  ../../../../intern/guardedalloc/intern/mallocn.c
  ../../../../intern/guardedalloc/intern/mallocn_guarded_impl.c
  ../../../../intern/guardedalloc/intern/mallocn_lockfree_impl.c

  # Needed for defaults.
  ../../../../release/datafiles/userdef/userdef_default.c
  ../../../../release/datafiles/userdef/userdef_default_theme.c
)

set(INC
  ../../../../intern/clog

  # Needed for defaults forward declarations.
  ../../loader

  ${CMAKE_BINARY_DIR}/source/dune/types/intern

  # types_type_offsets.h
  ${CMAKE_CURRENT_BINARY_DIR}/../../types/intern
  # api_prototypes.h
  ${CMAKE_CURRENT_BINARY_DIR}/../../api/
)

set(INC_SYS

)

if(WITH_CYCLES)
  add_definitions(-DWITH_CYCLES)
endif()

if(WITH_PYTHON)
  add_definitions(-DWITH_PYTHON)
  list(APPEND INC
    ../../python
  )
endif()

if(WITH_IMAGE_OPENEXR)
  add_definitions(-DWITH_OPENEXR)
endif()

if(WITH_OPENIMAGEIO)
  add_definitions(-DWITH_OPENIMAGEIO)
endif()

if(WITH_IMAGE_TIFF)
  add_definitions(-DWITH_TIFF)
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

if(WITH_IMAGE_HDR)
  add_definitions(-DWITH_HDR)
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

if(WITH_CODEC_FFMPEG)
  list(APPEND INC
    ../../../../intern/ffmpeg
  )
  list(APPEND INC_SYS
    ${FFMPEG_INCLUDE_DIRS}
  )
  list(APPEND LIB
    ${FFMPEG_LIBRARIES}
  )
  add_definitions(-DWITH_FFMPEG)
endif()

if(WITH_FFTW3)
  add_definitions(-DWITH_FFTW3)
endif()

if(WITH_MOD_FLUID)
  add_definitions(-DWITH_FLUID)
endif()

if(WITH_MOD_OCEANSIM)
  add_definitions(-DWITH_OCEANSIM)
endif()

if(WITH_SDL)
  if(WITH_SDL_DYNLOAD)
    add_definitions(-DWITH_SDL_DYNLOAD)
    list(APPEND INC
      ../../../../extern/sdlew/include
    )
  endif()
  add_definitions(-DWITH_SDL)
endif()

if(WITH_OPENAL)
  add_definitions(-DWITH_OPENAL)
endif()

if(WITH_COREAUDIO)
  add_definitions(-DWITH_COREAUDIO)
endif()

if(WITH_JACK)
  add_definitions(-DWITH_JACK)
endif()

if(WITH_PULSEAUDIO)
  add_definitions(-DWITH_PULSEAUDIO)
endif()

if(WITH_WASAPI)
  add_definitions(-DWITH_WASAPI)
endif()

if(WITH_OPENCOLLADA)
  add_definitions(-DWITH_COLLADA)
endif()

if(WITH_INTERNATIONAL)
  add_definitions(-DWITH_INTERNATIONAL)
endif()

if(WITH_ALEMBIC)
  list(APPEND INC
    ../../io/alembic
  )
  add_definitions(-DWITH_ALEMBIC)
endif()

if(WITH_BULLET)
  list(APPEND INC
    ../../../../intern/rigidbody
  )
  add_definitions(-DWITH_BULLET)
endif()

if(WITH_FREESTYLE)
  list(APPEND INC
    ../../freestyle
  )
  add_definitions(-DWITH_FREESTYLE)
endif()

if(WITH_OPENSUBDIV)
  list(APPEND INC
    ../../../../intern/opensubdiv
  )
  add_definitions(-DWITH_OPENSUBDIV)
endif()

if(WITH_OPENVDB)
  add_definitions(-DWITH_OPENVDB)

  if(WITH_OPENVDB_BLOSC)
    add_definitions(-DWITH_OPENVDB_BLOSC)
  endif()
endif()

if(WITH_INPUT_NDOF)
  add_definitions(-DWITH_INPUT_NDOF)
endif()

if(WITH_XR_OPENXR)
  add_definitions(-DWITH_XR_OPENXR)
endif()

if(WITH_GMP)
  add_definitions(-DWITH_GMP)
endif()

# Build api executable
dune_include_dirs(
  .
  ..
  ../../font
  ../../dune
  ../../lib
  ../../lang
  ../../mesh
  ../../graph
  ../../draw
  ../../gpu
  ../../ikplugin
  ../../imbuf
  ../../types
  ../../mods
  ../../nodes/
  ../../seq
  ../../simulation
  ../../wm
  ../../editors/include
  ../../render
  ../../../../intern/atomic
  ../../../../intern/glew-mx
  ../../../../intern/guardedalloc
  ../../../../intern/memutil
  ../../../../intern/mantaflow/extern
)

add_cc_flags_custom_test(api)

add_executable(api ${SRC} ${SRC_API_INC} ${SRC_TYPES_INC})
setup_platform_linker_flags(api)

target_link_libraries(api bf_dna)
target_link_libraries(api bf_types_dunelib)

if(WIN32 AND NOT UNIX)
  target_link_libraries(makesrna ${PTHREADS_LIBRARIES})
endif()

# Output api_*_gen.c
# note (linux only): with crashes try add this after COMMAND: valgrind --leak-check=full --track-origins=yes
add_custom_command(
  OUTPUT ${GENSRC}
  COMMAND "$<TARGET_FILE:makesrna>" ${CMAKE_CURRENT_BINARY_DIR}/  ${CMAKE_CURRENT_BINARY_DIR}/../
  DEPENDS api
)

# Build bf_api
set(SRC
  api_access.c
  api_access_compare_override.c
  ${GENSRC}

  ${SRC_API_INC}
  api_access_internal.h
  api_internal.h
  api_internal_types.h
  api_mesh_utils.h
)

set(LIB
  bf_dna
  bf_editor_space_api

  bf_editor_anim
  bf_editor_armature
  bf_editor_asset
  bf_editor_curve
  bf_editor_curves
  bf_editor_gizmo_lib
  bf_editor_pen
  bf_editor_io
  bf_editor_mesh
  bf_editor_object
  bf_editor_phys
  bf_editor_render
  bf_editor_scene
  bf_editor_sculpt_paint
  bf_editor_sound
  bf_editor_transform
  bf_editor_undo
)

add_definitions(${GL_DEFINITIONS})

dune_add_lib(bf_api "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# Needed so we can use dna_type_offsets.h for defaults initialization.
add_dependencies(bf_dune bf_dna)
