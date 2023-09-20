add_definitions(-DWITH_DNA_GHASH)

dune_include_dirs(
  ../../../../intern/atomic
  ../../../../intern/guardedalloc
  ../../lib
  ../../imbuf
  ..
)


# -----------------------------------------------------------------------------
# Build types executable
set(SRC
  types_utils.c
  types.c
  ../../lib/intern/lib_assert.c
  ../../lib/intern/lib_ghash.c
  ../../lib/intern/lib_ghash_utils.c
  ../../lib/intern/lib_memarena.c
  ../../lib/intern/lib_mempool.c
  ../../lib/intern/hash_mm2a.c  # needed by 'lib_ghash_utils.c', not used directly.
  ../../../../intern/guardedalloc/intern/leak_detector.cc
  ../../../../intern/guardedalloc/intern/mallocn.c
  ../../../../intern/guardedalloc/intern/mallocn_guarded_impl.c
  ../../../../intern/guardedalloc/intern/mallocn_lockfree_impl.c
)

# SRC_DNA_INC is defined in the parent dir
add_cc_flags_custom_test(types)

add_executable(types ${SRC} ${SRC_DNA_INC})
setup_platform_linker_flags(types)

if(WIN32 AND NOT UNIX)
  target_link_libraries(types ${PTHREADS_LIBRARIES})
endif()

# Output dna.c
add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/types.c
    ${CMAKE_CURRENT_BINARY_DIR}/types_type_offsets.h
    ${CMAKE_CURRENT_BINARY_DIR}/types_verify.c
  COMMAND
    "$<TARGET_FILE:types>"
    ${CMAKE_CURRENT_BINARY_DIR}/types.c
    ${CMAKE_CURRENT_BINARY_DIR}/types_type_offsets.h
    ${CMAKE_CURRENT_BINARY_DIR}/types_verify.c
    ${CMAKE_SOURCE_DIR}/source/dune/types/
  DEPENDS makesdna
)


# -----------------------------------------------------------------------------
# Build bf_types library
set(INC
  ${CMAKE_CURRENT_BINARY_DIR}
)

set(INC_SYS

)

set(SRC
  types_defaults.c
  types_genfile.c
  types_utils.c
  ${CMAKE_CURRENT_BINARY_DIR}/types.c
  ${CMAKE_CURRENT_BINARY_DIR}/types_verify.c
  ${SRC_TYPES_INC}

  ${CMAKE_CURRENT_BINARY_DIR}/types_type_offsets.h
  types_rename_defs.h
  types_utils.h
)

set(LIB
)

set_source_files_properties(
  ${CMAKE_CURRENT_BINARY_DIR}/types.c
  ${CMAKE_CURRENT_BINARY_DIR}/types_type_offsets.h
  ${CMAKE_CURRENT_BINARY_DIR}/types_verify.c
  PROPERTIES GENERATED TRUE
)

dune_add_lib(dune_types "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")


# -----------------------------------------------------------------------------
# Build dune_types_dunelib library
set(INC

)

set(INC_SYS

)

set(SRC
  ../../lib/intern/lib_assert.c
  ../../lib/intern/lib_ghash.c
  ../../lib/intern/lib_ghash_utils.c
  ../../lib/intern/lib_linklist.c
  ../../lib/intern/lib_memarena.c
  ../../lib/intern/lib_mempool.c
  ../../lib/intern/endian_switch.c
  ../../lib/intern/hash_mm2a.c
  ../../lib/intern/list.c

  ../types_armature_defaults.h
  ../types_asset_defaults.h
  ../types_brush_defaults.h
  ../types_cachefile_defaults.h
  ../types_camera_defaults.h
  ../types_collection_defaults.h
  ../types_curve_defaults.h
  ../types_defaults.h
  ../types_fluid_defaults.h
  ../types_pen_mod_defaults.h
  ../types_curves_defaults.h
  ../types_image_defaults.h
  ../types_lattice_defaults.h
  ../types_light_defaults.h
  ../types_lightprobe_defaults.h
  ../types_linestyle_defaults.h
  ../types_material_defaults.h
  ../types_mesh_defaults.h
  ../types_meta_defaults.h
  ../types_mod_defaults.h
  ../DNA_mod_types.h
  ../DNA_movieclip_defaults.h
  ../DNA_object_defaults.h
  ../DNA_particle_defaults.h
  ../DNA_pointcloud_defaults.h
  ../types_scene_defaults.h
  ../DNA_simulation_defaults.h
  ../DNA_space_defaults.h
  ../DNA_speaker_defaults.h
  ../DNA_texture_defaults.h
  ../DNA_vec_defaults.h
  ../typew_view3d_defaults.h
  ../types_volume_defaults.h
  ../types_world_defaults.h
)

set(LIB
)

dune_add_lib(dune_types_dunelib "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
