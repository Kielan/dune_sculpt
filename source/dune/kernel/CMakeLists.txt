

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
  dune_blenfont
  dune_blenlib
  dune_blenloader
  dune_blentranslation
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

dune_add_lib(dune_kernel "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

dune_add_test_lib(dune_kernel_tests "${TEST_SRC}" "${INC};${TEST_INC}" "${INC_SYS}" "${LIB}")
