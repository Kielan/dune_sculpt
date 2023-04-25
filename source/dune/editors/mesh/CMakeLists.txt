set(INC
  ../include
  ../uvedit
  ../../dune/font
  ../../dune/kernel
  ../../dune/lib
  ../../dune/translation
  ../../dune/mesh
  ../../depsgraph
  ../../draw
  ../../gpu
  ../../imbuf
  ../../types
  ../../api
  ../../render
  ../../windowmanager
  ../../../../intern/clog
  ../../../../intern/glew-mx
  ../../../../intern/guardedalloc
  # RNA_prototypes.h
  ${CMAKE_BINARY_DIR}/source/dune/api
)

set(SRC
  editface.c
  editmesh_add.c
  editmesh_add_gizmo.c
  editmesh_automerge.c
  editmesh_bevel.c
  editmesh_bisect.c
  editmesh_extrude.c
  editmesh_extrude_screw.c
  editmesh_extrude_spin.c
  editmesh_extrude_spin_gizmo.c
  editmesh_inset.c
  editmesh_intersect.c
  editmesh_knife.c
  editmesh_knife_project.c
  editmesh_loopcut.c
  editmesh_mask_extract.c
  editmesh_path.c
  editmesh_polybuild.c
  editmesh_preselect_edgering.c
  editmesh_preselect_elem.c
  editmesh_rip.c
  editmesh_rip_edge.c
  editmesh_select.c
  editmesh_select_similar.c
  editmesh_tools.c
  editmesh_undo.c
  editmesh_utils.c
  mesh_data.c
  mesh_mirror.c
  mesh_ops.c
  meshtools.c

  mesh_intern.h
)

set(LIB
  dunefile_kernel
  dunefile_lib
  dunefile_windowmanager
)

if(WITH_FREESTYLE)
  add_definitions(-DWITH_FREESTYLE)
endif()

if(WITH_BULLET)
  add_definitions(-DWITH_BULLET)
endif()

if(WITH_GMP)
  add_definitions(-DWITH_GMP)
endif()


dune_add_lib(bf_editor_mesh "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# api_prototypes.h
add_dependencies(dunefile_editor_mesh dunefile_api)
