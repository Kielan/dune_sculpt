set(INC
  ../include
  ../../dune
  ../../lang
  ../../mesh
  ../../gpu
  ../../ikplugin
  ../../api
  ../../render
  ../../seq
  ../../win
  # api_prototypes.h
  ${CMAKE_BINARY_DIR}/source/dune/api
)
set(INC_SYS
)

set(SRC
  transform.cc
  transform_constraints.cc
  transform_convert.cc
  transform_convert_action.cc
  transform_convert_armature.cc
  transform_convert_cursor.cc
  transform_convert_curve.cc
  transform_convert_curves.cc
  transform_convert_pen_legacy.cc
  transform_convert_graph.cc
  transform_convert_pen.cc
  transform_convert_lattice.cc
  transform_convert_mask.cc
  transform_convert_mball.cc
  transform_convert_mesh.cc
  transform_convert_mesh_edge.cc
  transform_convert_mesh_skin.cc
  transform_convert_mesh_uv.cc
  transform_convert_mesh_vert_cdata.cc
  transform_convert_nla.cc
  transform_convert_node.cc
  transform_convert_ob.cc
  transform_convert_ob_texspace.cc
  transform_convert_paintcurve.cc
  transform_convert_particle.cc
  transform_convert_sculpt.cc
  transform_convert_seq.cc
  transform_convert_seq_img.cc
  transform_convert_seq_retiming.cc
  transform_convert_tracking.cc
  transform_convert_tracking_curves.cc
  transform_draw_cursors.cc
  transform_generics.cc
  transform_gizmo_2d.cc
  transform_gizmo_3d.cc
  transform_gizmo_3d_cage.cc
  transform_gizmo_3d_shear.cc
  transform_gizmo_extrude_3d.cc
  transform_input.cc
  transform_mode.cc
  transform_mode_align.cc
  transform_mode_baketime.cc
  transform_mode_bbone_resize.cc
  transform_mode_bend.cc
  transform_mode_boneenvelope.cc
  transform_mode_boneroll.cc
  transform_mode_curveshrinkfatten.cc
  transform_mode_customdata.cc
  transform_mode_edge_rotate_normal.cc
  transform_mode_edge_seq_slide.cc
  transform_mode_edge_slide.cc
  transform_mode_penopacity.cc
  transform_mode_penshrinkfatten.cc
  transform_mode_maskshrinkfatten.cc
  transform_mode_mirror.cc
  transform_mode_push_pull.cc
  transform_mode_resize.cc
  transform_mode_rotate.cc
  transform_mode_shear.cc
  transform_mode_shrink_fatten.cc
  transform_mode_skin_resize.cc
  transform_mode_snapsource.cc
  transform_mode_tilt.cc
  transform_mode_timescale.cc
  transform_mode_timeslide.cc
  transform_mode_timetranslate.cc
  transform_mode_tosphere.cc
  transform_mode_trackball.cc
  transform_mode_translate.cc
  transform_mode_vert_slide.cc
  transform_ops.cc
  transform_orientations.cc
  transform_snap.cc
  transform_snap_anim.cc
  transform_snap_ob.cc
  transform_snap_ob_armature.cc
  transform_snap_ob_camera.cc
  transform_snap_ob_curve.cc
  transform_snap_ob_editmesh.cc
  transform_snap_ob_mesh.cc
  transform_snap_seq.cc

  transform.hh
  transform_constraints.hh
  transform_convert.hh
  transform_data.hh
  transform_drw_cursors.hh
  transform_gizmo.hh
  transform_mode.hh
  transform_orientations.hh
  transform_snap.hh
  transform_snap_ob.hh
)

set(LIB
  PRIVATE dfile::animrig
  PRIVATE dfile::font
  dfil_dune
  PRIVATE dfile::lib
  dfile_mesh
  PRIVATE dfile::graph
  PRIVATE dfile::types
  dfile_editor_mask
  dfile_gpu
  PRIVATE dfile::intern::guardedalloc
)

dune_add_lib(dfile_editor_transform "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# api_prototypes.h
add_dependencies(dfile_editor_transform dfile_api)
