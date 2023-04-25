set(INC
  ../include
  ../io
  ../../dunekernel
  ../../dune/lib
  ../../gpu
  ../../types
  ../../api
  ../../windowmanager
  ../../../../intern/guardedalloc
)

set(INC_SYS

)

set(SRC
  spacetypes.c
)

set(LIB
  dunefile_editor_geometry
  dunefile_editor_space_action
  dunefile_editor_space_buttons
  dunefile_editor_space_clip
  dunefile_editor_space_console
  dunefile_editor_space_file
  dunefile_editor_space_graph
  dunefile_editor_space_image
  dunefile_editor_space_info
  dunefile_editor_space_nla
  dunefile_editor_space_node
  dunefile_editor_space_outliner
  dune_editor_space_script
  dunefile_editor_space_sequencer
  dunefile_editor_space_spreadsheet
  dunefile_editor_space_statusbar
  dunefile_editor_space_text
  dunefile_editor_space_topbar
  dunefile_editor_space_userpref
  dunefile_editor_space_view3d
)

dune_add_lib(bf_editor_space_api "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
