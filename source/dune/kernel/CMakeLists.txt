

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
intern/asset_catalog.cc
  intern/asset_catalog_path.cc
  intern/asset_library.cc
  intern/asset_library_service.cc
  intern/attribute.c
  intern/attribute_access.cc
  intern/attribute_math.cc
  intern/autoexec.c
  intern/blender.c
  intern/blender_copybuffer.c
  intern/blender_undo.c
  intern/blender_user_menu.c
  intern/blendfile.c
  intern/blendfile_link_append.c
  intern/boids.c
  intern/bpath.c
  intern/brush.c
  intern/bvhutils.cc
  intern/cachefile.c
  intern/callbacks.c
  intern/camera.c
  intern/cdderivedmesh.c
  intern/cloth.c
  intern/collection.c
  intern/collision.c
  intern/colorband.c
  intern/colortools.c
  intern/constraint.c
  intern/context.c
  intern/crazyspace.c
  intern/cryptomatte.cc
  intern/curve.cc
  intern/curves.cc
  intern/curves_geometry.cc
  intern/curve_bevel.c
  intern/curve_bezier.cc
  intern/curve_catmull_rom.cc
  intern/curve_convert.c
  intern/curve_decimate.c
  intern/curve_deform.c
  intern/curve_eval.cc
  intern/curve_nurbs.cc
  intern/curve_to_mesh_convert.cc
  intern/curveprofile.cc
  intern/customdata.cc
  intern/customdata_file.c
  intern/data_transfer.c
  intern/deform.c
  intern/displist.cc
  intern/displist_tangent.c
  intern/dynamicpaint.c
  intern/editlattice.c
  intern/editmesh.c
  intern/editmesh_bvh.c
  intern/editmesh_cache.c
  intern/editmesh_tangent.c
  intern/effect.c
  intern/fcurve.c
  intern/fcurve_cache.c
  intern/fcurve_driver.c
  intern/fluid.c
  intern/fmodifier.c
  intern/freestyle.c
  intern/geometry_component_curve.cc
  intern/geometry_component_curves.cc
  intern/geometry_component_instances.cc
  intern/geometry_component_mesh.cc
  intern/geometry_component_pointcloud.cc
  intern/geometry_component_volume.cc
  intern/geometry_set.cc
  intern/geometry_set_instances.cc
  intern/gpencil.c
  intern/gpencil_curve.c
  intern/gpencil_geom.cc
  intern/gpencil_modifier.c
  intern/gpencil_update_cache.c
  intern/icons.cc
  intern/icons_rasterize.c
  intern/idprop.c
  intern/idprop_create.cc
  intern/idprop_serialize.cc
  intern/idprop_utils.c
  intern/idtype.c
  intern/image.cc
  intern/image_format.cc
  intern/image_partial_update.cc
  intern/image_gen.c
  intern/image_gpu.cc
  intern/image_save.cc
  intern/ipo.c
  intern/kelvinlet.c
  intern/key.c
  intern/keyconfig.c
  intern/lattice.c
  intern/lattice_deform.c
  intern/layer.c
  intern/layer_utils.c
  intern/lib_id.c
  intern/lib_id_delete.c
  intern/lib_id_eval.c
  intern/lib_id_remapper.cc
  intern/lib_override.c
  intern/lib_override_proxy_conversion.c
  intern/lib_query.c
  intern/lib_remap.c
  intern/library.c
  intern/light.c
  intern/lightprobe.c
  intern/linestyle.c
  intern/main.c
  intern/main_idmap.c
  intern/mask.c
  intern/mask_evaluate.c
  intern/mask_rasterize.c
  intern/material.c
  intern/mball.c
  intern/mball_tessellate.c
  intern/mesh.cc
  intern/mesh_boolean_convert.cc
  intern/mesh_calc_edges.cc
  intern/mesh_convert.cc
  intern/mesh_debug.cc
  intern/mesh_evaluate.cc
  intern/mesh_fair.cc
  intern/mesh_iterators.c
  intern/mesh_mapping.c
  intern/mesh_merge.c
  intern/mesh_mirror.c
  intern/mesh_normals.cc
  intern/mesh_remap.c
  intern/mesh_remesh_voxel.cc
  intern/mesh_runtime.c
  intern/mesh_sample.cc
  intern/mesh_tangent.c
  intern/mesh_tessellate.c
  intern/mesh_validate.cc
  intern/mesh_wrapper.c
  intern/modifier.c
  intern/movieclip.c
  intern/multires.c
  intern/multires_reshape.c
  intern/multires_reshape_apply_base.c
  intern/multires_reshape_ccg.c
  intern/multires_reshape_smooth.c
  intern/multires_reshape_subdivide.c
  intern/multires_reshape_util.c
  intern/multires_reshape_vertcos.c
  intern/multires_subdiv.c
  intern/multires_unsubdivide.c
  intern/multires_versioning.c
  intern/nla.c
  intern/node.cc
  intern/node_tree_update.cc
  intern/object.cc
  intern/object_deform.c
  intern/object_dupli.cc
  intern/object_facemap.c
  intern/object_update.c
  intern/ocean.c
  intern/ocean_spectrum.c
  intern/outliner_treehash.c
  intern/packedFile.c
  intern/paint.c
  intern/paint_toolslots.c
  intern/particle.c
  intern/particle_child.c
  intern/particle_distribute.c
  intern/particle_system.c
  intern/pbvh.c
  intern/pbvh_bmesh.c
  intern/pointcache.c
  intern/pointcloud.cc
  intern/preferences.c
  intern/report.c
  intern/rigidbody.c
  intern/scene.c
  intern/screen.c
  intern/shader_fx.c
  intern/shrinkwrap.c
  intern/simulation.cc
  intern/softbody.c
  intern/sound.c
  intern/speaker.c
  intern/spline_base.cc
  intern/spline_bezier.cc
  intern/spline_nurbs.cc
  intern/spline_poly.cc
  intern/studiolight.c
  intern/subdiv.c
  intern/subdiv_ccg.c
  intern/subdiv_ccg_mask.c
  intern/subdiv_ccg_material.c
  intern/subdiv_converter.c
  intern/subdiv_converter_mesh.c
  intern/subdiv_deform.c
  intern/subdiv_displacement.c
  intern/subdiv_displacement_multires.c
  intern/subdiv_eval.c
  intern/subdiv_foreach.c
  intern/subdiv_mesh.c
  intern/subdiv_modifier.c
  intern/subdiv_stats.c
  intern/subdiv_topology.c
  intern/subsurf_ccg.c
  intern/text.c
  intern/text_suggestions.c
  intern/texture.c
  intern/tracking.c
  intern/tracking_auto.c
  intern/tracking_detect.c
  intern/tracking_plane_tracker.c
  intern/tracking_region_tracker.c
  intern/tracking_solver.c
  intern/tracking_stabilize.c
  intern/tracking_util.c
  intern/type_conversions.cc
  intern/undo_system.c
  intern/unit.c
  intern/vfont.c
  intern/vfontdata_freetype.c
  intern/volume.cc
  intern/volume_render.cc
  intern/volume_to_mesh.cc
  intern/workspace.c
  intern/world.c
  intern/writeavi.c

  KERNEL_DerivedMesh.h
  KERNEL_action.h
  KERNEL_action.hh
  KERNEL_addon.h
  KERNEL_anim_data.h
  KERNEL_anim_path.h
  KERNEL_anim_visualization.h
  KERNEL_animsys.h
  KERNEL_anonymous_attribute.h
  KERNEL_anonymous_attribute.hh
  KERNEL_appdir.h
  KERNEL_armature.h
  KERNEL_armature.hh
  KERNEL_asset.h
  KERNEL_asset_catalog.hh
  KERNEL_asset_catalog_path.hh
  KERNEL_asset_library.h
  KERNEL_asset_library.hh
  KERNEL_attribute.h
  KERNEL_attribute_access.hh
  KERNEL_attribute_math.hh
  KERNEL_autoexec.h
  KERNEL_dune.h
  KERNEL_dune_copybuffer.h
  KERNEL_dune_undo.h
  KERNEL_dune_user_menu.h
  KERNEL_dune_version.h
  KERNEL_dunefile.h
  KERNEL_dunefile_link_append.h
  KERNEL_boids.h
  KERNEL_bpath.h
  KERNEL_brush.h
  KERNEL_bvhutils.h
  KERNEL_cachefile.h
  KERNEL_callbacks.h
  KERNEL_camera.h
  KERNEL_ccg.h
  KERNEL_cdderivedmesh.h
  KERNEL_cloth.h
  KERNEL_collection.h
  KERNEL_collision.h
  KERNEL_colorband.h
  KERNEL_colortools.h
  KERNEL_constraint.h
  KERNEL_context.h
  KERNEL_crazyspace.h
  KERNEL_cryptomatte.h
  # KE_cryptomatte.hh
  KERNEL_curve.h
  KERNEL_curves.h
  # KE_curves.hh
  KERNEL_curve_to_mesh.h
  KERNEL_curveprofile.h
  KERNEL_customdata.h
  KERNEL_customdata_file.h
  KERNEL_data_transfer.h
  KERNEL_deform.h
  KERNEL_displist.h
  KERNEL_displist_tangent.h
  KERNEL_duplilist.h
  KERNEL_dynamicpaint.h
  KERNEL_editlattice.h
  KERNEL_editmesh.h
  KERNEL_editmesh_bvh.h
  KERNEL_editmesh_cache.h
  KERNEL_editmesh_tangent.h
  KERNEL_effect.h
  KERNEL_fcurve.h
  KERNEL_fcurve_driver.h
  KERNEL_fluid.h
  KERNEL_freestyle.h
  KERNEL_geometry_set.h
  #KERNEL_geometry_set.hh
  #KE_geometry_set_instances.hh
  KERNEL_global.h
  KERNEL_gpencil.h
  KERNEL_gpencil_curve.h
  KERNEL_gpencil_geom.h
  KERNEL_gpencil_modifier.h
  KERNEL_gpencil_update_cache.h
  KERNEL_icons.h
  KERNEL_idprop.h
  #KERNEL_idprop.hh
  KERNEL_idtype.h
  KERNEL_image.h
  KERNEL_image_format.h
  #KERNEL_image_partial_update.hh
  KERNEL_image_save.h
  KERNEL_ipo.h
  KERNEL_kelvinlet.h
  KERNEL_key.h
  KERNEL_keyconfig.h
  KERNEL_lattice.h
  KERNEL_layer.h
  KERNEL_lib_id.h
  KERNEL_lib_override.h
  KERNEL_lib_query.h
  KERNEL_lib_remap.h
  KERNEL_library.h
  KERNEL_light.h
  KERNEL_lightprobe.h
  KERNEL_linestyle.h
  KERNEL_main.h
  KERNEL_main_idmap.h
  KERNEL_mask.h
  KERNEL_material.h
  KERNEL_mball.h
  KERNEL_mball_tessellate.h
  KERNEL_mesh.h
  KERNEL_mesh_boolean_convert.hh
  KERNEL_mesh_fair.h
  KERNEL_mesh_iterators.h
  KERNEL_mesh_mapping.h
  KERNEL_mesh_mirror.h
  KERNEL_mesh_remap.h
  KERNEL_mesh_remesh_voxel.h
  KERNEL_mesh_runtime.h
  KERNEL_mesh_sample.hh
  KERNEL_mesh_tangent.h
  KERNEL_mesh_types.h
  KERNEL_mesh_wrapper.h
  KERNEL_modifier.h
  KERNEL_movieclip.h
  KERNEL_multires.h
  KERNEL_nla.h
  KERNEL_node.h
  KERNEL_node_tree_update.h
  KERNEL_object.h
  KERNEL_object_deform.h
  KERNEL_object_facemap.h
  KERNEL_ocean.h
  KERNEL_outliner_treehash.h
  KERNEL_packedFile.h
  KERNEL_paint.h
  KERNEL_particle.h
  KERNEL_pbvh.h
  KERNEL_pointcache.h
  KERNEL_pointcloud.h
  KERNEL_preferences.h
  KERNEL_report.h
  KERNEL_rigidbody.h
  KERNEL_scene.h
  KERNEL_screen.h
  KERNEL_sequencer_offscreen.h
  KERNEL_shader_fx.h
  KERNEL_shrinkwrap.h
  KERNEL_simulation.h
  KERNEL_softbody.h
  KERNEL_sound.h
  KERNEL_speaker.h
  KERNEL_spline.hh
  KERNEL_studiolight.h
  KERNEL_subdiv.h
  KERNEL_subdiv_ccg.h
  KERNEL_subdiv_deform.h
  KERNEL_subdiv_eval.h
  KERNEL_subdiv_foreach.h
  KERNEL_subdiv_mesh.h
  KERNEL_subdiv_modifier.h
  KERNEL_subdiv_topology.h
  KERNEL_subsurf.h
  KERNEL_text.h
  KERNEL_text_suggestions.h
  KERNEL_texture.h
  KERNEL_tracking.h
  #KERNEL_type_conversions.hh
  KERNEL_undo_system.h
  KERNEL_unit.h
  KERNEL_vfont.h
  KERNEL_vfontdata.h
  KERNEL_volume.h
  KERNEL_volume_render.h
  #KE_volume_to_mesh.hh
  KERNEL_workspace.h
  KERNEL_world.h
  KERNEL_writeavi.h

  nla_private.h
  particle_private.h
  tracking_private.h
  intern/CCGSubSurf.h
  intern/CCGSubSurf_inline.h
  intern/CCGSubSurf_intern.h
  intern/asset_library_service.hh
  intern/attribute_access_intern.hh
  intern/data_transfer_intern.h
  intern/lib_intern.h
  intern/multires_inline.h
  intern/multires_reshape.h
  intern/multires_unsubdivide.h
  intern/ocean_intern.h
  intern/pbvh_intern.h
  intern/subdiv_converter.h
  intern/subdiv_inline.h
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
    KERNEL_writeffmpeg.h
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
    dune_intern_openvdb
    ${OPENVDB_LIBRARIES}
  )
  add_definitions(-DWITH_OPENVDB ${OPENVDB_DEFINITIONS})
endif()

if(WITH_QUADRIFLOW)
  list(APPEND INC
    ../../../intern/quadriflow
  )
  list(APPEND LIB
    dune_intern_quadriflow
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
