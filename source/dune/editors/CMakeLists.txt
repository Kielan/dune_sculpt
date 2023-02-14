set(INC
  ../include
  ../../dune
  ../../lib
  ../../i18n
  ../../depsgraph
  ../../gpu
  ../../types
  ../../api
  ../../windowmanager
  ../../../../intern/clog
  ../../../../intern/glew-mx
  ../../../../intern/guardedalloc
  ../../../../intern/mantaflow/extern
  # api_prototypes.h
  ${CMAKE_BINARY_DIR}/source/dune/api
)

set(SRC
  dynamicpaint_ops.c
  particle_boids.c
  particle_edit.c
  particle_edit_undo.c
  particle_object.c
  physics_fluid.c
  physics_ops.c
  physics_pointcache.c
  rigidbody_constraint.c
  rigidbody_object.c
  rigidbody_world.c

  particle_edit_utildefines.h
  physics_intern.h
)

set(LIB
  df_dune
  df_dunelib
)

if(WITH_MOD_FLUID)
  list(APPEND LIB
    df_intern_mantaflow
  )
  add_definitions(-DWITH_FLUID)
endif()

if(WITH_BULLET)
  list(APPEND INC
    ../../../../intern/rigidbody
  )
  add_definitions(-DWITH_BULLET)
endif()


dune_add_lib(df_editor_physics "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# api_prototypes.h
add_dependencies(df_editor_physics bf_rna)
