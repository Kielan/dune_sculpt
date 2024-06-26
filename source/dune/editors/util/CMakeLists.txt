set(INC
  ../include
  ../space_sequencer
  ../../font
  ../../core
  ../../lib
  ../../i18n
  ../../mesh
  ../../graph
  ../../gpu
  ../../imbuf
  ../../types
  ../../api
  ../../sequencer
  ../../windowmanager
  ../../../../intern/clog
  ../../../../intern/glew-mx
  ../../../../intern/guardedalloc
)


set(SRC
  ed_draw.c
  ed_transverts.c
  ed_util.c
  ed_util_imbuf.c
  ed_util_ops.cc
  gizmo_utils.c
  numinput.c
  select_utils.c

  # general includes
  ../include/BIF_glutil.h
  ../include/ed_anim_api.h
  ../include/ed_armature.h
  ../include/ed_asset.h
  ../include/ed_buttons.h
  ../include/ed_clip.h
  ../include/ed_curve.h
  ../include/ed_curves.h
  ../include/ed_curves_sculpt.h
  ../include/ed_datafiles.h
  ../include/ed_file_indexer.h
  ../include/ed_fileselect.h
  ../include/ed_geometry.h
  ../include/ed_gizmo_library.h
  ../include/ed_gizmo_utils.h
  ../include/ed_pen.h
  ../include/ed_image.h
  ../include/ed_info.h
  ../include/ed_keyframes_draw.h
  ../include/ed_keyframes_edit.h
  ../include/ed_keyframes_keylist.h
  ../include/ed_keyframing.h
  ../include/ed_lattice.h
  ../include/ed_markers.h
  ../include/ed_mask.h
  ../include/ed_mball.h
  ../include/ed_mesh.h
  ../include/ed_node.h
  ../include/ed_numinput.h
  ../include/ed_object.h
  ../include/ed_outliner.h
  ../include/ed_paint.h
  ../include/ed_particle.h
  ../include/ed_physics.h
  ../include/ed_render.h
  ../include/ed_scene.h
  ../include/ed_screen.h
  ../include/ed_screen_types.h
  ../include/ed_sculpt.h
  ../include/ed_select_utils.h
  ../include/ed_sequencer.h
  ../include/ed_sound.h
  ../include/ed_space_api.h
  ../include/ed_spreadsheet.h
  ../include/ed_text.h
  ../include/ed_time_scrub_ui.h
  ../include/ed_transform.h
  ../include/ed_transform_snap_object_context.h
  ../include/ed_transverts.h
  ../include/ed_types.h
  ../include/ed_undo.h
  ../include/ed_userpref.h
  ../include/ed_util.h
  ../include/ed_util_imbuf.h
  ../include/ed_uvedit.h
  ../include/ed_view3d.h
  ../include/ed_view3d_offscreen.h
  ../include/ui_icons.h
  ../include/ui_interface.h
  ../include/ui_interface.hh
  ../include/ui_interface_icons.h
  ../include/ui_resources.h
  ../include/ui_tree_view.hh
  ../include/ui_view2d.h
)

set(LIB)

endif()

dune_add_lib(dunefile_editor_util "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
